/* TitanOs - matériau « liquid glass »
 *
 * Pour une forme (rectangle arrondi) posée sur la scène :
 *   1. on réduit la zone située dessous (x2 ou x4) et on la floute (flou de boîte répété,
 *      proche d'un flou gaussien) ;
 *   2. on règle les couleurs du fond flouté : saturation, luminosité, teinte ;
 *   3. pour chaque pixel de la forme on relit ce fond par interpolation. Près des bords,
 *      la lecture est décalée vers l'intérieur comme à travers une lentille (réfraction),
 *      avec une légère dispersion des couleurs ;
 *   4. on ajoute un liseré lumineux (éclairage venant d'en haut à gauche), un reflet
 *      doux en haut, et un bord lissé.
 * Le résultat est une image prémultipliée de la taille de la forme, que le gestionnaire
 * de fenêtres garde en cache tant que rien ne change dessous. */
#include "glass.h"

#define sqrtf __builtin_sqrtf
#define fabsf __builtin_fabsf

static uint32_t *tmp_a, *tmp_b, *rowbuf;
static size_t tmp_n;
static int rowbuf_n;

/* mélange de deux couleurs 0x00RRGGBB, t de 0 à 256 (deux canaux à la fois) */
static inline uint32_t lerp_px(uint32_t a, uint32_t b, uint32_t t) {
    uint32_t it = 256 - t;
    uint32_t rb = (((a & 0xFF00FF) * it + (b & 0xFF00FF) * t) >> 8) & 0xFF00FF;
    uint32_t g = (((a & 0x00FF00) * it + (b & 0x00FF00) * t) >> 8) & 0x00FF00;
    return rb | g;
}

static bool ensure(size_t n) {
    if (n <= tmp_n) return true;
    free(tmp_a);
    free(tmp_b);
    tmp_a = malloc(n * 4);
    tmp_b = malloc(n * 4);
    tmp_n = (tmp_a && tmp_b) ? n : 0;
    return tmp_n != 0;
}

/* flou de boîte séparable, rayon k, sur une image 0x00RRGGBB */
static void box_h(const uint32_t *src, uint32_t *dst, int w, int h, int k) {
    int div = 2 * k + 1;
    for (int y = 0; y < h; y++) {
        const uint32_t *s = src + (size_t)y * w;
        uint32_t *d = dst + (size_t)y * w;
        int r = 0, g = 0, b = 0;
        for (int i = -k; i <= k; i++) {
            uint32_t p = s[CLAMP(i, 0, w - 1)];
            r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255;
        }
        for (int x = 0; x < w; x++) {
            d[x] = (uint32_t)(r / div) << 16 | (uint32_t)(g / div) << 8 | (uint32_t)(b / div);
            uint32_t pa = s[MIN(x + k + 1, w - 1)], pr = s[MAX(x - k, 0)];
            r += (int)(pa >> 16 & 255) - (int)(pr >> 16 & 255);
            g += (int)(pa >> 8 & 255) - (int)(pr >> 8 & 255);
            b += (int)(pa & 255) - (int)(pr & 255);
        }
    }
}

static void box_v(const uint32_t *src, uint32_t *dst, int w, int h, int k) {
    int div = 2 * k + 1;
    for (int x = 0; x < w; x++) {
        int r = 0, g = 0, b = 0;
        for (int i = -k; i <= k; i++) {
            uint32_t p = src[(size_t)CLAMP(i, 0, h - 1) * w + x];
            r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255;
        }
        for (int y = 0; y < h; y++) {
            dst[(size_t)y * w + x] = (uint32_t)(r / div) << 16 | (uint32_t)(g / div) << 8 | (uint32_t)(b / div);
            uint32_t pa = src[(size_t)MIN(y + k + 1, h - 1) * w + x], pr = src[(size_t)MAX(y - k, 0) * w + x];
            r += (int)(pa >> 16 & 255) - (int)(pr >> 16 & 255);
            g += (int)(pa >> 8 & 255) - (int)(pr >> 8 & 255);
            b += (int)(pa & 255) - (int)(pr & 255);
        }
    }
}

/* lecture bilinéaire (coordonnées en 1/256 de pixel) */
static inline void sample(const uint32_t *img, int w, int h, int u, int v, int *r, int *g, int *b) {
    int x = u >> 8, y = v >> 8, fx = u & 255, fy = v & 255;
    if (x < 0) { x = 0; fx = 0; }
    if (y < 0) { y = 0; fy = 0; }
    if (x >= w - 1) { x = w - 1; fx = 0; }
    if (y >= h - 1) { y = h - 1; fy = 0; }
    int x1 = MIN(x + 1, w - 1), y1 = MIN(y + 1, h - 1);
    uint32_t p00 = img[(size_t)y * w + x], p10 = img[(size_t)y * w + x1];
    uint32_t p01 = img[(size_t)y1 * w + x], p11 = img[(size_t)y1 * w + x1];
    int w00 = (256 - fx) * (256 - fy), w10 = fx * (256 - fy), w01 = (256 - fx) * fy, w11 = fx * fy;
    *r = (int)(((p00 >> 16 & 255) * w00 + (p10 >> 16 & 255) * w10 + (p01 >> 16 & 255) * w01 + (p11 >> 16 & 255) * w11) >> 16);
    *g = (int)(((p00 >> 8 & 255) * w00 + (p10 >> 8 & 255) * w10 + (p01 >> 8 & 255) * w01 + (p11 >> 8 & 255) * w11) >> 16);
    *b = (int)(((p00 & 255) * w00 + (p10 & 255) * w10 + (p01 & 255) * w01 + (p11 & 255) * w11) >> 16);
}

void glass_render(canvas_t *out, const canvas_t *scene, rect_t r, float radius, const glass_t *g) {
    int f = g->down < 1 ? 1 : g->down;
    int margin = (int)(g->refract * 1.3f) + f * g->radius * g->passes + 2;
    rect_t S = rect_isect(rect_grow(r, margin), R(0, 0, scene->w, scene->h));
    int dw = (S.w + f - 1) / f, dh = (S.h + f - 1) / f;
    if (rect_empty(S) || !ensure((size_t)dw * dh)) { canvas_clear(out, 0); return; }

    /* 1. réduction : blocs complets additionnés deux canaux à la fois, bords au cas par cas */
    bool pow2 = f == 2 || f == 4;
    int fullw = pow2 ? S.w / f : 0, fullh = pow2 ? S.h / f : 0, sh = f == 4 ? 4 : 2;
    for (int y = 0; y < fullh; y++)
        for (int x = 0; x < fullw; x++) {
            uint32_t rb = 0, gg = 0;
            for (int j = 0; j < f; j++) {
                const uint32_t *row = scene->px + (size_t)(S.y + y * f + j) * scene->stride + S.x + x * f;
                for (int i = 0; i < f; i++) { rb += row[i] & 0xFF00FF; gg += row[i] & 0x00FF00; }
            }
            tmp_a[(size_t)y * dw + x] = ((rb >> sh) & 0xFF00FF) | ((gg >> sh) & 0x00FF00);
        }
    for (int y = 0; y < dh; y++)
        for (int x = 0; x < dw; x++) {
            if (y < fullh && x < fullw && f > 1) continue;
            int rr = 0, gg = 0, bb = 0, n = 0;
            for (int j = 0; j < f; j++) {
                int sy = S.y + y * f + j;
                if (sy >= S.y + S.h) break;
                const uint32_t *row = scene->px + (size_t)sy * scene->stride;
                for (int i = 0; i < f; i++) {
                    int sx = S.x + x * f + i;
                    if (sx >= S.x + S.w) break;
                    uint32_t p = row[sx];
                    rr += p >> 16 & 255; gg += p >> 8 & 255; bb += p & 255; n++;
                }
            }
            tmp_a[(size_t)y * dw + x] = (uint32_t)(rr / n) << 16 | (uint32_t)(gg / n) << 8 | (uint32_t)(bb / n);
        }

    /* 2. flou */
    for (int p = 0; p < g->passes; p++) {
        box_h(tmp_a, tmp_b, dw, dh, g->radius);
        box_v(tmp_b, tmp_a, dw, dh, g->radius);
    }

    /* 3. couleurs du verre : saturation, luminosité, teinte */
    float tr = (float)(g->tint >> 16 & 255), tg = (float)(g->tint >> 8 & 255), tb = (float)(g->tint & 255);
    for (size_t i = 0; i < (size_t)dw * dh; i++) {
        uint32_t p = tmp_a[i];
        float cr = (float)(p >> 16 & 255), cg = (float)(p >> 8 & 255), cb = (float)(p & 255);
        float l = 0.299f * cr + 0.587f * cg + 0.114f * cb;
        cr = (l + (cr - l) * g->sat) * g->bright;
        cg = (l + (cg - l) * g->sat) * g->bright;
        cb = (l + (cb - l) * g->sat) * g->bright;
        cr = cr + (tr - cr) * g->tint_a;
        cg = cg + (tg - cg) * g->tint_a;
        cb = cb + (tb - cb) * g->tint_a;
        int ir = (int)CLAMP(cr, 0.f, 255.f), ig = (int)CLAMP(cg, 0.f, 255.f), ib = (int)CLAMP(cb, 0.f, 255.f);
        tmp_a[i] = (uint32_t)ir << 16 | (uint32_t)ig << 8 | (uint32_t)ib;
    }

    /* 4. forme, réfraction, liseré, reflet */
    float w = (float)r.w, h = (float)r.h, cx = w / 2, cy = h / 2, hx = w / 2, hy = h / 2;
    float rad = MIN(radius, MIN(hx, hy));
    float band = MIN(g->band, MIN(hx, hy));
    float ox = (float)(r.x - S.x), oy = (float)(r.y - S.y);
    const float lx = -0.6f, ly = -0.8f;      /* direction de la lumière */
    /* au-delà de B pixels du bord, un pixel n'a ni réfraction ni liseré et il est opaque :
     * seul le reflet (constant sur une ligne) s'ajoute au fond flouté */
    float B = MAX(g->refract > 0 ? band : 0.0f, g->rim > 0 ? 2.2f : 0.0f);
    if (B < 0.5f) B = 0.5f;
    B += 1.0f;                                               /* marge : le bord exact reste au calcul complet */
    for (int j = 0; j < r.h; j++) {
        float py = j + 0.5f;
        uint32_t *op = out->px + (size_t)j * out->stride;
        float sheen = g->sheen * (1.0f - py / (h * 0.55f));
        if (sheen < 0) sheen = 0;
        /* partie intérieure de la ligne : [i0, i1) */
        int i0 = r.w, i1 = r.w;
        float ay = fabsf(py - cy);
        if (ay <= hy - B) {
            float half;
            float qy = ay - (hy - rad);
            if (rad > B && qy > 0) {
                float ri = rad - B;
                half = qy < ri ? (hx - rad) + sqrtf(ri * ri - qy * qy) : -1;
            } else half = hx - B;
            if (half > 0) {
                i0 = (int)ceilf(cx - half - 0.5f);
                i1 = (int)floorf(cx + half - 0.5f) + 1;
                if (i0 < 0) i0 = 0;
                if (i1 > r.w) i1 = r.w;
                if (i1 < i0) i0 = i1 = r.w;
            }
        }
        if (i0 < i1) {
            if (rowbuf_n < dw) { free(rowbuf); rowbuf = malloc((size_t)dw * 4); rowbuf_n = rowbuf ? dw : 0; }
            if (!rowbuf) { i0 = i1 = r.w; }
        }
        if (i0 < i1) {
            /* ligne du fond réduit, interpolée verticalement, avec le reflet (linéaire) */
            int v = (int)(((py + oy) / f - 0.5f) * 256);
            int y = v >> 8, fy = v & 255;
            if (y < 0) { y = 0; fy = 0; }
            if (y >= dh - 1) { y = dh - 1; fy = 0; }
            const uint32_t *ra = tmp_a + (size_t)y * dw, *rb2 = tmp_a + (size_t)MIN(y + 1, dh - 1) * dw;
            uint32_t ad = (uint32_t)MIN((int)(sheen * 256 + 0.5f), 256);
            for (int x = 0; x < dw; x++) {
                uint32_t p = lerp_px(ra[x], rb2[x], (uint32_t)fy);
                if (ad) p = lerp_px(p, 0xFFFFFF, ad);
                rowbuf[x] = p;
            }
            /* puis horizontalement, par pas de 1/f */
            int u = (int)(((i0 + 0.5f + ox) / f - 0.5f) * 256), du = 256 / f;
            for (int i = i0; i < i1; i++, u += du) {
                int x = u >> 8, fx = u & 255;
                if (x < 0) { x = 0; fx = 0; }
                if (x >= dw - 1) { x = dw - 1; fx = 0; }
                op[i] = 0xFF000000u | (fx ? lerp_px(rowbuf[x], rowbuf[x + 1], (uint32_t)fx) : rowbuf[x]);
            }
        }
        for (int i = 0; i < r.w; i++) {
            if (i == i0) { i = i1 - 1; continue; }
            float px = i + 0.5f;
            float qx = fabsf(px - cx) - (hx - rad), qy = fabsf(py - cy) - (hy - rad);
            float d, nx, ny;
            if (qx > 0 && qy > 0) {
                float len = sqrtf(qx * qx + qy * qy);
                d = len - rad;
                nx = qx / len; ny = qy / len;
            } else if (qx > qy) {
                d = qx - rad; nx = 1; ny = 0;
            } else {
                d = qy - rad; nx = 0; ny = 1;
            }
            if (px < cx) nx = -nx;
            if (py < cy) ny = -ny;
            if (d >= 0.5f) { op[i] = 0; continue; }
            float cov = d <= -0.5f ? 1.0f : 0.5f - d;
            float t = -d;                                    /* profondeur depuis le bord */
            float sx = px, sy = py;
            int cr, cg, cb;
            if (t < band && g->refract > 0) {
                float k = 1.0f - t / band;
                float m = g->refract * k * k;
                sx -= nx * m; sy -= ny * m;
                int u = (int)(((sx + ox) / f - 0.5f) * 256), v = (int)(((sy + oy) / f - 0.5f) * 256);
                int du = (int)(nx * m * 0.18f / f * 256), dv = (int)(ny * m * 0.18f / f * 256);
                int dummy1, dummy2;
                sample(tmp_a, dw, dh, u, v, &cr, &cg, &cb);
                sample(tmp_a, dw, dh, u + du, v + dv, &cr, &dummy1, &dummy2);
                sample(tmp_a, dw, dh, u - du, v - dv, &dummy1, &dummy2, &cb);
                float lift = 1.0f + g->edge_glow * k;      /* les bords captent la lumière */
                cr = MIN((int)(cr * lift), 255); cg = MIN((int)(cg * lift), 255); cb = MIN((int)(cb * lift), 255);
            } else {
                int u = (int)(((sx + ox) / f - 0.5f) * 256), v = (int)(((sy + oy) / f - 0.5f) * 256);
                sample(tmp_a, dw, dh, u, v, &cr, &cg, &cb);
            }
            float add = sheen;
            if (t < 2.2f && g->rim > 0) {
                float rk = 1.0f - t / 2.2f;
                float dl = nx * lx + ny * ly;
                float lit = 0.22f + 0.78f * (dl > 0 ? dl * dl : 0) + 0.5f * (dl < 0 ? dl * dl : 0);
                add += g->rim * rk * rk * lit;
            }
            if (add > 0) {
                if (add > 1) add = 1;
                cr += (int)((255 - cr) * add); cg += (int)((255 - cg) * add); cb += (int)((255 - cb) * add);
            }
            uint32_t a = (uint32_t)(cov * 255 + 0.5f);
            if (a >= 255) op[i] = 0xFF000000u | (uint32_t)cr << 16 | (uint32_t)cg << 8 | (uint32_t)cb;
            else op[i] = a << 24 | (uint32_t)(cr * a / 255) << 16 | (uint32_t)(cg * a / 255) << 8 | (uint32_t)(cb * a / 255);
        }
    }
}
