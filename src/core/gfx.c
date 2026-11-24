/* TitanOs - moteur graphique 2D lissé (formes par champs de distance) */
#include "gfx.h"

#define fabsf  __builtin_fabsf
#define sqrtf  __builtin_sqrtf
#define floorf __builtin_floorf
#define ceilf  __builtin_ceilf

/* ---------------- surfaces ---------------- */
canvas_t canvas_new(int w, int h) {
    canvas_t c = { 0 };
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    c.px = calloc((size_t)w * h, 4);
    if (c.px) { c.w = w; c.h = h; c.stride = w; c.clip = R(0, 0, w, h); }
    return c;
}

void canvas_free(canvas_t *c) { free(c->px); c->px = NULL; c->w = c->h = 0; }
void canvas_clip(canvas_t *c, rect_t r) { c->clip = rect_isect(r, R(0, 0, c->w, c->h)); }
void canvas_noclip(canvas_t *c) { c->clip = R(0, 0, c->w, c->h); }

void canvas_clear(canvas_t *c, uint32_t v) {
    rect_t r = c->clip;
    for (int j = 0; j < r.h; j++) {
        uint32_t *p = c->px + (size_t)(r.y + j) * c->stride + r.x;
        for (int i = 0; i < r.w; i++) p[i] = v;
    }
}

/* ---------------- couleurs ---------------- */
uint32_t col_mix(uint32_t a, uint32_t b, int t) {
    if (t <= 0) return a;
    if (t >= 256) return b;
    uint32_t rb = (((a & 0xFF00FF) * (256 - t) + (b & 0xFF00FF) * t) >> 8) & 0xFF00FF;
    uint32_t ag = ((((a >> 8) & 0xFF00FF) * (256 - t) + ((b >> 8) & 0xFF00FF) * t)) & 0xFF00FF00;
    return rb | ag;
}

uint32_t col_scale(uint32_t c, int f) {
    int r = ((c >> 16) & 0xFF) * f >> 8, g = ((c >> 8) & 0xFF) * f >> 8, b = (c & 0xFF) * f >> 8;
    return (c & 0xFF000000) | (uint32_t)(MIN(r, 255) << 16 | MIN(g, 255) << 8 | MIN(b, 255));
}

/* mélange « source sur destination » : couleur RGB non prémultipliée, opacité a (0-255) */
static inline uint32_t blend(uint32_t d, uint32_t c, uint32_t a) {
    if (a == 0) return d;
    if (a >= 255) return c | 0xFF000000u;
    uint32_t ia = 255 - a;
    uint32_t rb = (c & 0xFF00FF) * a + (d & 0xFF00FF) * ia + 0x800080;
    rb = ((rb + ((rb >> 8) & 0xFF00FF)) >> 8) & 0xFF00FF;
    uint32_t ag = ((((c >> 8) & 0xFF) | 0xFF0000) * a) + ((d >> 8) & 0xFF00FF) * ia + 0x800080;
    ag = (ag + ((ag >> 8) & 0xFF00FF)) & 0xFF00FF00;
    return rb | ag;
}

/* source prémultipliée sur destination */
static inline uint32_t over(uint32_t d, uint32_t s) {
    uint32_t sa = s >> 24;
    if (sa == 255) return s;
    if (sa == 0) return d;
    uint32_t ia = 255 - sa;
    uint32_t rb = (d & 0xFF00FF) * ia + 0x800080;
    rb = ((rb + ((rb >> 8) & 0xFF00FF)) >> 8) & 0xFF00FF;
    uint32_t ag = ((d >> 8) & 0xFF00FF) * ia + 0x800080;
    ag = (ag + ((ag >> 8) & 0xFF00FF)) & 0xFF00FF00;
    return s + rb + ag;
}

static inline uint32_t pm_scale(uint32_t s, uint32_t a) {
    uint32_t rb = (s & 0xFF00FF) * a + 0x800080;
    rb = ((rb + ((rb >> 8) & 0xFF00FF)) >> 8) & 0xFF00FF;
    uint32_t ag = ((s >> 8) & 0xFF00FF) * a + 0x800080;
    ag = (ag + ((ag >> 8) & 0xFF00FF)) & 0xFF00FF00;
    return rb | ag;
}

static inline void span(canvas_t *c, int y, int x0, int x1, uint32_t argb, uint32_t a) {
    if (x0 < c->clip.x) x0 = c->clip.x;
    if (x1 > c->clip.x + c->clip.w) x1 = c->clip.x + c->clip.w;
    if (x0 >= x1 || a == 0) return;
    uint32_t *p = c->px + (size_t)y * c->stride;
    if (a >= 255) {
        uint32_t v = argb | 0xFF000000u;
        for (int i = x0; i < x1; i++) p[i] = v;
    } else {
        for (int i = x0; i < x1; i++) p[i] = blend(p[i], argb, a);
    }
}

static inline void dot(canvas_t *c, int x, int y, uint32_t argb, uint32_t a) {
    if (!rect_has(c->clip, x, y) || a == 0) return;
    uint32_t *p = c->px + (size_t)y * c->stride + x;
    *p = blend(*p, argb, a);
}

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* ---------------- rectangles ---------------- */
void gfx_fill(canvas_t *c, rect_t r, uint32_t argb) {
    r = rect_isect(r, c->clip);
    for (int j = 0; j < r.h; j++) span(c, r.y + j, r.x, r.x + r.w, argb, argb >> 24);
}

void gfx_fill_grad(canvas_t *c, rect_t r, uint32_t top, uint32_t bottom) {
    rect_t k = rect_isect(r, c->clip);
    for (int j = 0; j < k.h; j++) {
        int y = k.y + j;
        uint32_t col = col_mix(top, bottom, r.h > 1 ? (y - r.y) * 256 / (r.h - 1) : 0);
        span(c, y, k.x, k.x + k.w, col, col >> 24);
    }
}

static inline float sd_rrect(float px, float py, float cx, float cy, float hx, float hy, float r) {
    float qx = fabsf(px - cx) - (hx - r), qy = fabsf(py - cy) - (hy - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}

static void rrect_fill(canvas_t *c, float x, float y, float w, float h, float r, uint32_t top, uint32_t bot) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
    rect_t k = rect_isect(R(x0, y0, x1 - x0, y1 - y0), c->clip);
    if (rect_empty(k)) return;
    float cx = x + w / 2, cy = y + h / 2, hx = w / 2, hy = h / 2;
    for (int j = k.y; j < k.y + k.h; j++) {
        float py = j + 0.5f;
        uint32_t col = top == bot ? top : col_mix(top, bot, (int)((py - y) * 256 / h));
        float ca = (float)(col >> 24);
        float vcov = clamp01(MIN(j + 1.0f, y + h) - MAX((float)j, y));
        bool vc = r > 0 && (py < y + r || py > y + h - r);
        int ia = (int)ceilf(vc ? x + r : x), ib = (int)floorf(vc ? x + w - r : x + w);
        if (ia < k.x) ia = k.x;
        if (ib > k.x + k.w) ib = k.x + k.w;
        if (ib < ia) ib = ia;
        for (int i = k.x; i < ia; i++) {
            float px = i + 0.5f, cov;
            if (vc && (px < x + r || px > x + w - r)) cov = clamp01(0.5f - sd_rrect(px, py, cx, cy, hx, hy, r));
            else cov = clamp01(MIN(i + 1.0f, x + w) - MAX((float)i, x)) * vcov;
            dot(c, i, j, col, (uint32_t)(cov * ca + 0.5f));
        }
        span(c, j, ia, ib, col, (uint32_t)(vcov * ca + 0.5f));
        for (int i = ib; i < k.x + k.w; i++) {
            float px = i + 0.5f, cov;
            if (vc && (px < x + r || px > x + w - r)) cov = clamp01(0.5f - sd_rrect(px, py, cx, cy, hx, hy, r));
            else cov = clamp01(MIN(i + 1.0f, x + w) - MAX((float)i, x)) * vcov;
            dot(c, i, j, col, (uint32_t)(cov * ca + 0.5f));
        }
    }
}

void gfx_rrect(canvas_t *c, float x, float y, float w, float h, float r, uint32_t argb) {
    rrect_fill(c, x, y, w, h, r, argb, argb);
}

void gfx_rrect_grad(canvas_t *c, float x, float y, float w, float h, float r, uint32_t top, uint32_t bottom) {
    rrect_fill(c, x, y, w, h, r, top, bottom);
}

void gfx_rrect_stroke(canvas_t *c, float x, float y, float w, float h, float r, float t, uint32_t argb) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int x0 = (int)floorf(x) - 1, y0 = (int)floorf(y) - 1, x1 = (int)ceilf(x + w) + 1, y1 = (int)ceilf(y + h) + 1;
    rect_t k = rect_isect(R(x0, y0, x1 - x0, y1 - y0), c->clip);
    float cx = x + w / 2, cy = y + h / 2, hx = w / 2, hy = h / 2, ca = (float)(argb >> 24);
    float inner = t + r + 1.5f;
    for (int j = k.y; j < k.y + k.h; j++) {
        float py = j + 0.5f;
        bool mid_row = py > y + inner && py < y + h - inner;
        for (int i = k.x; i < k.x + k.w; i++) {
            float px = i + 0.5f;
            if (mid_row && px > x + inner && px < x + w - inner) { int to = (int)(x + w - inner); if (to > i) { i = to; continue; } }
            float d = sd_rrect(px, py, cx, cy, hx, hy, r);
            float cov = clamp01(0.5f - (fabsf(d + t / 2) - t / 2));
            if (cov > 0) dot(c, i, j, argb, (uint32_t)(cov * ca + 0.5f));
        }
    }
}

void gfx_circle(canvas_t *c, float cx, float cy, float r, uint32_t argb) {
    rrect_fill(c, cx - r, cy - r, 2 * r, 2 * r, r, argb, argb);
}

void gfx_ring(canvas_t *c, float cx, float cy, float r, float t, uint32_t argb) {
    gfx_rrect_stroke(c, cx - r, cy - r, 2 * r, 2 * r, r, t, argb);
}

void gfx_line(canvas_t *c, float x0, float y0, float x1, float y1, float t, uint32_t argb) {
    float hw = t / 2;
    int bx0 = (int)floorf(MIN(x0, x1) - hw - 1), by0 = (int)floorf(MIN(y0, y1) - hw - 1);
    int bx1 = (int)ceilf(MAX(x0, x1) + hw + 1), by1 = (int)ceilf(MAX(y0, y1) + hw + 1);
    rect_t k = rect_isect(R(bx0, by0, bx1 - bx0, by1 - by0), c->clip);
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy, ca = (float)(argb >> 24);
    for (int j = k.y; j < k.y + k.h; j++)
        for (int i = k.x; i < k.x + k.w; i++) {
            float px = i + 0.5f - x0, py = j + 0.5f - y0;
            float h = len2 > 0 ? clamp01((px * dx + py * dy) / len2) : 0;
            float ex = px - dx * h, ey = py - dy * h;
            float cov = clamp01(hw + 0.5f - sqrtf(ex * ex + ey * ey));
            if (cov > 0) dot(c, i, j, argb, (uint32_t)(cov * ca + 0.5f));
        }
}

/* arc de cercle épais, tracé par petits segments à bouts ronds */
void gfx_arc(canvas_t *c, float cx, float cy, float r, float t, float a0, float a1, uint32_t argb) {
    int n = (int)((a1 > a0 ? a1 - a0 : a0 - a1) * r / 2.5f) + 4;
    float px = cx + fcos(a0) * r, py = cy + fsin(a0) * r;
    for (int i = 1; i <= n; i++) {
        float a = a0 + (a1 - a0) * i / n;
        float x = cx + fcos(a) * r, y = cy + fsin(a) * r;
        gfx_line(c, px, py, x, y, t, argb);
        px = x;
        py = y;
    }
}

/* triangle plein, dégradé vertical sur sa hauteur */
void gfx_tri(canvas_t *c, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t top, uint32_t bottom) {
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area == 0) return;
    if (area < 0) { float tx = x1, ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty; }
    float ex[3] = { x0, x1, x2 }, ey[3] = { y0, y1, y2 };
    float nx[3], ny[3], nc[3];
    for (int e = 0; e < 3; e++) {
        float ax = ex[e], ay = ey[e], bx = ex[(e + 1) % 3], by = ey[(e + 1) % 3];
        float lx = bx - ax, ly = by - ay, l = sqrtf(lx * lx + ly * ly);
        nx[e] = ly / l; ny[e] = -lx / l;          /* normale extérieure */
        nc[e] = -(nx[e] * ax + ny[e] * ay);
    }
    float minx = MIN(x0, MIN(x1, x2)), maxx = MAX(x0, MAX(x1, x2));
    float miny = MIN(y0, MIN(y1, y2)), maxy = MAX(y0, MAX(y1, y2));
    rect_t k = rect_isect(R((int)floorf(minx) - 1, (int)floorf(miny) - 1, (int)(maxx - minx) + 3, (int)(maxy - miny) + 3), c->clip);
    for (int j = k.y; j < k.y + k.h; j++) {
        float py = j + 0.5f;
        uint32_t col = col_mix(top, bottom, (int)((py - miny) * 256 / (maxy - miny)));
        float ca = (float)(col >> 24);
        for (int i = k.x; i < k.x + k.w; i++) {
            float px = i + 0.5f;
            float d = -1e9f;
            for (int e = 0; e < 3; e++) {
                float v = nx[e] * px + ny[e] * py + nc[e];
                if (v > d) d = v;
            }
            float cov = clamp01(0.5f - d);
            if (cov > 0) dot(c, i, j, col, (uint32_t)(cov * ca + 0.5f));
        }
    }
}

/* ombre douce d'un rectangle arrondi, décalée de dy vers le bas ; rien n'est dessiné
 * sous la forme elle-même (elle sera couverte), mais tout le reste de l'ombre l'est */
void gfx_shadow(canvas_t *c, float x, float y, float w, float h, float r, float blur, float dy, uint32_t argb) {
    int x0 = (int)floorf(x - blur), y0 = (int)floorf(y + dy - blur);
    int x1 = (int)ceilf(x + w + blur), y1 = (int)ceilf(y + dy + h + blur);
    rect_t k = rect_isect(R(x0, y0, x1 - x0, y1 - y0), c->clip);
    float cx = x + w / 2, cy = y + h / 2, ca = (float)(argb >> 24);
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int j = k.y; j < k.y + k.h; j++) {
        float py = j + 0.5f;
        bool mid_row = py > y + r + 2 && py < y + h - r - 2;      /* ligne entièrement couverte au milieu */
        for (int i = k.x; i < k.x + k.w; i++) {
            float px = i + 0.5f;
            if (mid_row && px > x + 2 && px < x + w - 2) { int to = (int)(x + w - 2); if (to > i) { i = to; continue; } }
            float d0 = sd_rrect(px, py, cx, cy, w / 2, h / 2, r);          /* la forme qui couvre */
            if (d0 < -0.5f) continue;
            float d = sd_rrect(px, py - dy, cx, cy, w / 2, h / 2, r);      /* l'ombre */
            float t = clamp01((d + 1.5f) / blur);
            float a = (1 - t) * (1 - t) * ca;
            if (d0 < 0.5f) a *= clamp01(d0 + 0.5f);
            if (a >= 0.5f) dot(c, i, j, argb, (uint32_t)(a + 0.5f));
        }
    }
}

/* ---------------- copies ---------------- */
void gfx_blit_rect(canvas_t *dst, int x, int y, const canvas_t *src, rect_t sr, int alpha) {
    sr = rect_isect(sr, R(0, 0, src->w, src->h));
    rect_t d = rect_isect(R(x, y, sr.w, sr.h), dst->clip);
    if (rect_empty(d) || alpha <= 0) return;
    for (int j = 0; j < d.h; j++) {
        uint32_t *dp = dst->px + (size_t)(d.y + j) * dst->stride + d.x;
        const uint32_t *sp = src->px + (size_t)(sr.y + d.y - y + j) * src->stride + sr.x + d.x - x;
        if (alpha >= 255) for (int i = 0; i < d.w; i++) dp[i] = over(dp[i], sp[i]);
        else for (int i = 0; i < d.w; i++) dp[i] = over(dp[i], pm_scale(sp[i], (uint32_t)alpha));
    }
}

void gfx_blit(canvas_t *dst, int x, int y, const canvas_t *src, int alpha) {
    gfx_blit_rect(dst, x, y, src, R(0, 0, src->w, src->h), alpha);
}

void gfx_copy(canvas_t *dst, int x, int y, const canvas_t *src, rect_t sr) {
    sr = rect_isect(sr, R(0, 0, src->w, src->h));
    rect_t d = rect_isect(R(x, y, sr.w, sr.h), dst->clip);
    for (int j = 0; j < d.h; j++)
        memcpy(dst->px + (size_t)(d.y + j) * dst->stride + d.x,
               src->px + (size_t)(sr.y + d.y - y + j) * src->stride + sr.x + d.x - x, (size_t)d.w * 4);
}

/* ---------------- texte ---------------- */
static const glyph_t *find_glyph(const font_t *f, int cp) {
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (f->glyphs[m].cp == cp) return &f->glyphs[m];
        if (f->glyphs[m].cp < cp) lo = m + 1; else hi = m - 1;
    }
    return cp == '?' ? NULL : find_glyph(f, '?');
}

int text_width_n(const font_t *f, const char *s, int n) {
    const char *end = s + n;
    int w = 0;
    while (s < end && *s) {
        const glyph_t *g = find_glyph(f, utf8_decode(&s));
        if (g) w += g->adv;
    }
    return (w + 32) >> 6;
}

int text_width(const font_t *f, const char *s) { return text_width_n(f, s, 1 << 30); }

int text_draw_n(canvas_t *c, const font_t *f, int x, int y, const char *s, int n, uint32_t argb) {
    const char *end = s + n;
    int pen = x << 6, base = y + f->ascent;
    uint32_t ca = argb >> 24;
    while (s < end && *s) {
        const glyph_t *g = find_glyph(f, utf8_decode(&s));
        if (!g) continue;
        int gx = ((pen + 32) >> 6) + g->ox, gy = base + g->oy;
        rect_t k = rect_isect(R(gx, gy, g->w, g->h), c->clip);
        for (int j = k.y; j < k.y + k.h; j++) {
            const uint8_t *src = f->bits + g->off + (j - gy) * g->w;
            uint32_t *dp = c->px + (size_t)j * c->stride;
            for (int i = k.x; i < k.x + k.w; i++) {
                uint32_t a = src[i - gx];
                if (a) dp[i] = blend(dp[i], argb, (a * ca + 127) / 255);
            }
        }
        pen += g->adv;
    }
    return (pen + 32) >> 6;
}

int text_draw(canvas_t *c, const font_t *f, int x, int y, const char *s, uint32_t argb) {
    return text_draw_n(c, f, x, y, s, 1 << 30, argb);
}

void text_fit(canvas_t *c, const font_t *f, int x, int y, int maxw, const char *s, uint32_t argb) {
    if (text_width(f, s) <= maxw) { text_draw(c, f, x, y, s, argb); return; }
    int ell = text_width(f, "…"), n = 0;
    for (int p = 0; s[p]; p = utf8_next(s, p)) {
        if (text_width_n(f, s, utf8_next(s, p)) + ell > maxw) break;
        n = utf8_next(s, p);
    }
    int ex = text_draw_n(c, f, x, y, s, n, argb);
    text_draw(c, f, ex, y, "…", argb);
}

void text_center(canvas_t *c, const font_t *f, rect_t r, const char *s, uint32_t argb) {
    int w = text_width(f, s);
    text_draw(c, f, r.x + (r.w - w) / 2, r.y + (r.h - f->ascent - f->descent) / 2, s, argb);
}

int text_index_at(const font_t *f, const char *s, int x) {
    int pos = 0, w = 0;
    while (s[pos]) {
        int nx = utf8_next(s, pos);
        int cw = text_width_n(f, s + pos, nx - pos);
        if (w + cw / 2 > x) break;
        w += cw;
        pos = nx;
    }
    return pos;
}

void mono_char(canvas_t *c, int x, int y, int cp, uint32_t argb) {
    if (cp > 255) {
        switch (cp) {            /* quelques équivalents pour les caractères hors Latin-1 */
        case 0x2018: case 0x2019: cp = '\''; break;
        case 0x201C: case 0x201D: cp = '"'; break;
        case 0x2013: case 0x2014: case 0x2500: cp = '-'; break;
        case 0x2502: cp = '|'; break;
        case 0x20AC: cp = 0x80; break;
        default: cp = '?';
        }
    }
    const uint8_t *g = font8x16[cp & 0xFF];
    uint32_t a = argb >> 24;
    for (int j = 0; j < 16; j++) {
        uint8_t b = g[j];
        if (!b) continue;
        for (int i = 0; i < 8; i++)
            if (b & (0x80 >> i)) dot(c, x + i, y + j, argb, a);
    }
}
