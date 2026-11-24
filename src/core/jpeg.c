/* TitanOs - images JPEG (photos), GIF et PPM
 *
 * JPEG : décodeur complet pour les photos : séquentiel (appareils photo, téléphones) et
 * progressif (images du web), tables de Huffman, quantification, transformée inverse
 * (IDCT), sous-échantillonnage des couleurs (4:2:0, 4:2:2, 4:4:4), niveaux de gris,
 * intervalles de reprise, et orientation EXIF (photo prise en portrait remise droite).
 * GIF : première image, palette, transparence, entrelacement. */
#include "desk.h"

/* ================================================================== */
/* JPEG                                                                 */
/* ================================================================== */
#define JFAST 9                                    /* codes de Huffman courts : lus d'un coup */
typedef struct { uint8_t bits[17]; uint8_t vals[256]; int mincode[17], maxcode[18], valptr[17]; uint16_t fast[1 << JFAST]; bool ok; } jhuff_t;
typedef struct {
    int id, h, v, tq, td, ta, pred;
    int bpl, brows;                                /* blocs par ligne et lignes de blocs (MCU entières) */
    int cbw, cbh;                                  /* blocs couverts par l'image (balayage d'une seule composante) */
    int16_t *coef;                                 /* coefficients de tous les blocs */
    uint8_t *px;
    int stride;
} jcomp_t;
typedef struct {
    const uint8_t *p, *end;
    uint32_t bits;
    int nbits;
    bool marker;                  /* un marqueur a été rencontré dans les données */
    bool progressive;
    int ss, se, ah, al, eobrun;   /* balayage en cours (mode progressif) */
    uint16_t q[4][64];
    jhuff_t dc[4], ac[4];
    jcomp_t c[3];
    int nc, w, h, hmax, vmax, restart, mcux, mcuy;
} jdec_t;

static const uint8_t zz[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

static void jhuff_build(jhuff_t *t) {
    int code = 0, k = 0;
    memset(t->fast, 0, sizeof t->fast);
    for (int l = 1; l <= 16; l++) {
        t->valptr[l] = k;
        t->mincode[l] = code;
        for (int i = 0; i < t->bits[l]; i++, code++, k++)
            if (l <= JFAST && code < (1 << l)) {
                int first = code << (JFAST - l), cnt = 1 << (JFAST - l);
                for (int j = 0; j < cnt; j++) t->fast[first + j] = (uint16_t)(l << 8 | t->vals[k]);
            }
        t->maxcode[l] = t->bits[l] ? code - 1 : -1;
        code <<= 1;
    }
    t->maxcode[17] = 0x7FFFFFFF;
    t->ok = true;
}

static void jfill(jdec_t *d) {
    while (d->nbits <= 24) {
        uint32_t b = 0;
        if (!d->marker && d->p < d->end) {
            b = *d->p++;
            if (b == 0xFF) {
                uint8_t n = d->p < d->end ? *d->p : 0;
                if (n == 0) d->p++;                   /* 0xFF 0x00 : un 0xFF dans les données */
                else { d->marker = true; d->p--; b = 0; }
            }
        }
        d->bits |= b << (24 - d->nbits);
        d->nbits += 8;
    }
}
static int jbits(jdec_t *d, int n) {
    if (!n) return 0;
    jfill(d);
    int v = (int)(d->bits >> (32 - n));
    d->bits <<= n;
    d->nbits -= n;
    return v;
}
static int jbit(jdec_t *d) { return jbits(d, 1); }
static int jextend(int v, int n) { return n && v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }
static int jdecode(jdec_t *d, const jhuff_t *t) {
    jfill(d);
    int f = t->fast[d->bits >> (32 - JFAST)];
    if (f) {
        int l = f >> 8;
        d->bits <<= l;
        d->nbits -= l;
        return f & 255;
    }
    for (int l = JFAST + 1; l <= 16; l++) {
        int code = (int)(d->bits >> (32 - l));
        if (t->maxcode[l] >= 0 && code <= t->maxcode[l]) {
            d->bits <<= l;
            d->nbits -= l;
            return t->vals[t->valptr[l] + code - t->mincode[l]];
        }
    }
    return -1;
}

/* transformée inverse 8 x 8 (séparable, flottante) */
static float idct_c[8][8];
static void idct_init(void) {
    if (idct_c[0][0] != 0) return;
    for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++) {
            float cu = u ? 1.0f : 0.70710678f;
            /* cos((2x+1) u pi / 16) */
            float a = (float)((2 * x + 1) * u) * 3.14159265f / 16.0f;
            idct_c[x][u] = cu * fcos(a) / 2;
        }
}
static void idct8x8(const int *in, uint8_t *out, int stride) {
    bool flat = true;                                 /* bloc uni (fréquent) : pas de calcul */
    for (int i = 1; i < 64 && flat; i++) if (in[i]) flat = false;
    if (flat) {
        int v = (in[0] + 4 * 128 * 2 + 4) >> 3;       /* in[0] / 8 + 128, arrondi */
        uint8_t b = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        for (int y = 0; y < 8; y++) memset(out + y * stride, b, 8);
        return;
    }
    float tmp[64];
    for (int y = 0; y < 8; y++)
        for (int u = 0; u < 8; u++) {                 /* lignes : sur v */
            float s = 0;
            for (int v = 0; v < 8; v++) s += idct_c[y][v] * (float)in[v * 8 + u];
            tmp[y * 8 + u] = s;
        }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) s += idct_c[x][u] * tmp[y * 8 + u];
            int v = (int)(s + 128.5f);
            out[y * stride + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
}

/* ---------------- lecture d'un bloc selon le type de balayage ---------------- */
static bool jblock(jdec_t *d, jcomp_t *c, int16_t *blk) {
    if (!d->progressive) {                            /* séquentiel : le bloc entier */
        int t = jdecode(d, &d->dc[c->td]);
        if (t < 0 || t > 16) return false;
        c->pred += jextend(jbits(d, t), t);
        blk[0] = (int16_t)c->pred;
        for (int k = 1; k < 64;) {
            int rs = jdecode(d, &d->ac[c->ta]);
            if (rs < 0) return false;
            int r = rs >> 4, sz = rs & 15;
            if (!sz) { if (r != 15) break; k += 16; continue; }
            k += r;
            if (k > 63) return false;
            blk[zz[k]] = (int16_t)jextend(jbits(d, sz), sz);
            k++;
        }
        return true;
    }
    if (d->ss == 0) {                                 /* progressif : coefficient continu (DC) */
        if (d->ah == 0) {
            int t = jdecode(d, &d->dc[c->td]);
            if (t < 0 || t > 16) return false;
            c->pred += jextend(jbits(d, t), t);
            blk[0] = (int16_t)(c->pred * (1 << d->al));
        } else if (jbit(d)) blk[0] |= (int16_t)(1 << d->al);
        return true;
    }
    if (d->ah == 0) {                                 /* progressif : premiers coefficients AC */
        if (d->eobrun) { d->eobrun--; return true; }
        for (int k = d->ss; k <= d->se;) {
            int rs = jdecode(d, &d->ac[c->ta]);
            if (rs < 0) return false;
            int r = rs >> 4, sz = rs & 15;
            if (!sz) {
                if (r < 15) { d->eobrun = (1 << r) - 1; if (r) d->eobrun += jbits(d, r); break; }
                k += 16;
                continue;
            }
            k += r;
            if (k > 63) return false;
            blk[zz[k]] = (int16_t)(jextend(jbits(d, sz), sz) * (1 << d->al));
            k++;
        }
        return true;
    }
    /* progressif : affinage des coefficients AC (un bit de plus) */
    int bit = 1 << d->al, k = d->ss;
    if (d->eobrun) {
        d->eobrun--;
        for (; k <= d->se; k++) {
            int16_t *p = &blk[zz[k]];
            if (*p && jbit(d) && !(*p & bit)) *p = (int16_t)(*p > 0 ? *p + bit : *p - bit);
        }
        return true;
    }
    while (k <= d->se) {
        int rs = jdecode(d, &d->ac[c->ta]);
        if (rs < 0) return false;
        int r = rs >> 4, sz = rs & 15, val = 0;
        if (!sz) {
            if (r < 15) { d->eobrun = (1 << r) - 1; if (r) d->eobrun += jbits(d, r); r = 64; }
        } else val = jbit(d) ? bit : -bit;
        while (k <= d->se) {
            int16_t *p = &blk[zz[k++]];
            if (*p) {
                if (jbit(d) && !(*p & bit)) *p = (int16_t)(*p > 0 ? *p + bit : *p - bit);
            } else {
                if (r == 0) { *p = (int16_t)val; break; }
                r--;
            }
        }
    }
    return true;
}

static void jrestart(jdec_t *d) {                     /* intervalle de reprise : marqueur RSTn */
    d->nbits = 0;
    d->bits = 0;
    while (d->p + 1 < d->end && !(d->p[0] == 0xFF && d->p[1] >= 0xD0 && d->p[1] <= 0xD7)) d->p++;
    if (d->p + 1 < d->end) d->p += 2;
    d->marker = false;
    d->eobrun = 0;
    for (int i = 0; i < d->nc; i++) d->c[i].pred = 0;
}

/* un balayage : les composantes listées, entrelacées (MCU) ou une seule (bloc par bloc) */
static bool jscan(jdec_t *d, jcomp_t **sc, int ns) {
    int left = d->restart;
    d->eobrun = 0;
    for (int i = 0; i < d->nc; i++) d->c[i].pred = 0;
    if (ns == 1) {
        jcomp_t *c = sc[0];
        for (int by = 0; by < c->cbh; by++)
            for (int bx = 0; bx < c->cbw; bx++) {
                if (d->restart && left-- == 0) { jrestart(d); left = d->restart - 1; }
                if (!jblock(d, c, c->coef + ((size_t)by * c->bpl + bx) * 64)) return false;
            }
        return true;
    }
    for (int my = 0; my < d->mcuy; my++)
        for (int mx = 0; mx < d->mcux; mx++) {
            if (d->restart && left-- == 0) { jrestart(d); left = d->restart - 1; }
            for (int i = 0; i < ns; i++) {
                jcomp_t *c = sc[i];
                for (int by = 0; by < c->v; by++)
                    for (int bx = 0; bx < c->h; bx++) {
                        size_t b = (size_t)(my * c->v + by) * c->bpl + (size_t)(mx * c->h + bx);
                        if (!jblock(d, c, c->coef + b * 64)) return false;
                    }
            }
        }
    return true;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* orientation EXIF (1 à 8) : bloc APP1 « Exif », champ 0x0112 du premier répertoire */
static int exif_orientation(const uint8_t *s, size_t len) {
    if (len < 14 || memcmp(s, "Exif\0\0", 6)) return 1;
    const uint8_t *t = s + 6;
    size_t n = len - 6;
    bool le = t[0] == 'I';
    #define RD16(o) (le ? (uint32_t)(t[o] | t[(o) + 1] << 8) : (uint32_t)(t[o] << 8 | t[(o) + 1]))
    #define RD32(o) (le ? (uint32_t)t[o] | (uint32_t)t[(o) + 1] << 8 | (uint32_t)t[(o) + 2] << 16 | (uint32_t)t[(o) + 3] << 24 \
                        : (uint32_t)t[o] << 24 | (uint32_t)t[(o) + 1] << 16 | (uint32_t)t[(o) + 2] << 8 | (uint32_t)t[(o) + 3])
    if (n < 8 || RD16(2) != 42) return 1;
    size_t ifd = RD32(4);
    if (ifd + 2 > n) return 1;
    int cnt = (int)RD16(ifd);
    for (int i = 0; i < cnt; i++) {
        size_t e = ifd + 2 + (size_t)i * 12;
        if (e + 12 > n) break;
        if (RD16(e) == 0x0112) { int v = (int)RD16(e + 8); return v >= 1 && v <= 8 ? v : 1; }
    }
    #undef RD16
    #undef RD32
    return 1;
}

/* image remise droite selon l'orientation EXIF */
static void orient(canvas_t *img, int o) {
    if (o <= 1 || !img->px) return;
    int w = img->w, h = img->h, W = o >= 5 ? h : w, H = o >= 5 ? w : h;
    canvas_t r = canvas_new(W, H);
    if (!r.px) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int dx, dy;
            switch (o) {
            case 2: dx = w - 1 - x; dy = y; break;
            case 3: dx = w - 1 - x; dy = h - 1 - y; break;
            case 4: dx = x; dy = h - 1 - y; break;
            case 5: dx = y; dy = x; break;
            case 6: dx = h - 1 - y; dy = x; break;
            case 7: dx = h - 1 - y; dy = w - 1 - x; break;
            default: dx = y; dy = w - 1 - x; break;          /* 8 */
            }
            r.px[(size_t)dy * r.stride + dx] = img->px[(size_t)y * img->stride + x];
        }
    canvas_free(img);
    *img = r;
}

/* prochain marqueur après les données d'un balayage */
static size_t next_marker(const uint8_t *f, size_t n, size_t o) {
    while (o + 1 < n && !(f[o] == 0xFF && f[o + 1] != 0 && !(f[o + 1] >= 0xD0 && f[o + 1] <= 0xD7) && f[o + 1] != 0xFF)) o++;
    return o;
}

bool jpeg_decode(const uint8_t *f, size_t n, canvas_t *out) {
    if (n < 4 || f[0] != 0xFF || f[1] != 0xD8) return false;
    jdec_t *d = calloc(1, sizeof *d);
    if (!d) return false;
    idct_init();
    bool ok = false, frame = false, scanned = false;
    int orientation = 1;
    size_t o = 2;
    while (o + 4 <= n) {
        if (f[o] != 0xFF) { o++; continue; }
        uint8_t m = f[o + 1];
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7) || m == 0xFF) { o += m == 0xFF ? 1 : 2; continue; }
        if (m == 0xD9) break;
        size_t len = be16(f + o + 2);
        const uint8_t *s = f + o + 4;
        if (len < 2 || o + 2 + len > n) break;
        if (m == 0xDB) {                               /* tables de quantification */
            for (size_t k = 0; k + 65 <= len - 2;) {
                int pq = s[k] >> 4, tq = s[k] & 3;
                if (k + 1 + (pq ? 128 : 64) > len - 2) break;
                for (int i = 0; i < 64; i++) d->q[tq][i] = pq ? be16(s + k + 1 + i * 2) : s[k + 1 + i];
                k += 1 + (pq ? 128 : 64);
            }
        } else if (m == 0xC4) {                        /* tables de Huffman */
            for (size_t k = 0; k + 17 <= len - 2;) {
                int tc = s[k] >> 4, th = s[k] & 3, total = 0;
                jhuff_t *t = tc ? &d->ac[th] : &d->dc[th];
                for (int l = 1; l <= 16; l++) { t->bits[l] = s[k + l]; total += t->bits[l]; }
                if (total > 256 || k + 17 + (size_t)total > len - 2) break;
                memcpy(t->vals, s + k + 17, (size_t)total);
                jhuff_build(t);
                k += 17 + (size_t)total;
            }
        } else if (m == 0xE1 && orientation == 1) {    /* EXIF */
            orientation = exif_orientation(s, len - 2);
        } else if ((m == 0xC0 || m == 0xC1 || m == 0xC2) && !frame) {   /* image : séquentielle ou progressive */
            if (len < 8 || s[0] != 8) break;
            d->progressive = m == 0xC2;
            d->h = be16(s + 1);
            d->w = be16(s + 3);
            d->nc = s[5];
            if (d->w <= 0 || d->h <= 0 || d->w > 12000 || d->h > 12000 || (size_t)d->w * d->h > 50000000 || (d->nc != 1 && d->nc != 3) || len < 8 + 3 * (size_t)d->nc) break;
            for (int i = 0; i < d->nc; i++) {
                jcomp_t *c = &d->c[i];
                c->id = s[6 + i * 3];
                c->h = s[7 + i * 3] >> 4;
                c->v = s[7 + i * 3] & 15;
                c->tq = s[8 + i * 3] & 3;
                if (c->h < 1 || c->h > 2 || c->v < 1 || c->v > 2) goto done;
                d->hmax = MAX(d->hmax, c->h);
                d->vmax = MAX(d->vmax, c->v);
            }
            if (d->nc == 1) d->hmax = d->vmax = d->c[0].h = d->c[0].v = 1;   /* une seule composante : pas d'échantillonnage */
            d->mcux = (d->w + 8 * d->hmax - 1) / (8 * d->hmax);
            d->mcuy = (d->h + 8 * d->vmax - 1) / (8 * d->vmax);
            for (int i = 0; i < d->nc; i++) {
                jcomp_t *c = &d->c[i];
                c->bpl = d->mcux * c->h;
                c->brows = d->mcuy * c->v;
                c->cbw = ((d->w * c->h + d->hmax - 1) / d->hmax + 7) / 8;
                c->cbh = ((d->h * c->v + d->vmax - 1) / d->vmax + 7) / 8;
                c->stride = c->bpl * 8;
                c->coef = calloc((size_t)c->bpl * c->brows * 64, sizeof(int16_t));
                if (!c->coef) goto done;
            }
            frame = true;
        } else if (m >= 0xC3 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            goto done;                                  /* sans perte ou arithmétique : pas pris en charge */
        } else if (m == 0xDD) {
            d->restart = be16(s);
        } else if (m == 0xDA) {                         /* données d'un balayage */
            if (!frame) break;
            int ns = s[0];
            if (ns < 1 || ns > 3 || len < 6 + 2 * (size_t)ns) break;
            jcomp_t *sc[3];
            for (int i = 0; i < ns; i++) {
                int id = s[1 + i * 2], tables = s[2 + i * 2];
                sc[i] = NULL;
                for (int k = 0; k < d->nc; k++)
                    if (d->c[k].id == id) { sc[i] = &d->c[k]; d->c[k].td = tables >> 4 & 3; d->c[k].ta = tables & 3; }
                if (!sc[i]) goto done;
            }
            d->ss = s[1 + ns * 2];
            d->se = s[2 + ns * 2];
            d->ah = s[3 + ns * 2] >> 4;
            d->al = s[3 + ns * 2] & 15;
            if (!d->progressive) { d->ss = 0; d->se = 63; d->ah = d->al = 0; }
            if (d->ss > d->se || d->se > 63 || (d->ss > 0 && ns != 1)) goto done;
            for (int i = 0; i < ns; i++) {
                bool need_dc = d->ss == 0 && d->ah == 0, need_ac = d->ss > 0 || !d->progressive;
                if ((need_dc && !d->dc[sc[i]->td].ok) || (need_ac && !d->ac[sc[i]->ta].ok)) goto done;
            }
            d->p = f + o + 2 + len;
            d->end = f + n;
            d->bits = 0;
            d->nbits = 0;
            d->marker = false;
            if (!jscan(d, sc, ns)) goto done;
            scanned = true;
            o = next_marker(f, n, (size_t)(d->p - f));
            continue;
        }
        o += 2 + len;
    }
    ok = frame && scanned;
    /* coefficients -> pixels : quantification inverse et IDCT, bloc par bloc */
    for (int i = 0; ok && i < d->nc; i++) {
        jcomp_t *c = &d->c[i];
        c->px = malloc((size_t)c->stride * c->brows * 8);
        if (!c->px) { ok = false; break; }
        const uint16_t *q = d->q[c->tq];
        int blk[64];
        for (int by = 0; by < c->brows; by++)
            for (int bx = 0; bx < c->bpl; bx++) {
                const int16_t *cf = c->coef + ((size_t)by * c->bpl + bx) * 64;
                for (int k = 0; k < 64; k++) blk[zz[k]] = cf[zz[k]] * q[k];
                idct8x8(blk, c->px + (size_t)by * 8 * c->stride + bx * 8, c->stride);
            }
        free(c->coef);
        c->coef = NULL;
    }
    if (ok) {
        *out = canvas_new(d->w, d->h);
        if (!out->px) ok = false;
    }
    if (ok) {
        for (int y = 0; y < d->h; y++) {
            uint32_t *row = out->px + (size_t)y * out->stride;
            if (d->nc == 1) {
                const uint8_t *g = d->c[0].px + (size_t)y * d->c[0].stride;
                for (int x = 0; x < d->w; x++) row[x] = 0xFF000000u | (uint32_t)g[x] << 16 | (uint32_t)g[x] << 8 | g[x];
                continue;
            }
            const uint8_t *py = d->c[0].px + (size_t)(y * d->c[0].v / d->vmax) * d->c[0].stride;
            const uint8_t *pb = d->c[1].px + (size_t)(y * d->c[1].v / d->vmax) * d->c[1].stride;
            const uint8_t *pr = d->c[2].px + (size_t)(y * d->c[2].v / d->vmax) * d->c[2].stride;
            int h0 = d->c[0].h, h1 = d->c[1].h, h2 = d->c[2].h, hm = d->hmax;
            for (int x = 0; x < d->w; x++) {
                int Y = py[x * h0 / hm], cb = pb[x * h1 / hm] - 128, cr = pr[x * h2 / hm] - 128;
                int r = Y + ((91881 * cr) >> 16), g = Y - ((22554 * cb + 46802 * cr) >> 16), b = Y + ((116130 * cb) >> 16);
                r = CLAMP(r, 0, 255); g = CLAMP(g, 0, 255); b = CLAMP(b, 0, 255);
                row[x] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
            }
        }
        orient(out, orientation);
    }
done:
    for (int i = 0; i < 3; i++) { free(d->c[i].px); free(d->c[i].coef); }
    free(d);
    return ok;
}

/* ================================================================== */
/* GIF (première image)                                                 */
/* ================================================================== */
bool gif_decode(const uint8_t *f, size_t n, canvas_t *out) {
    if (n < 13 || memcmp(f, "GIF8", 4)) return false;
    int W = f[6] | f[7] << 8, H = f[8] | f[9] << 8, flags = f[10], bg = f[11];
    (void)bg;
    if (W <= 0 || H <= 0 || W > 8192 || H > 8192) return false;
    uint8_t gpal[768], pal[768];
    int gsize = 0, trans = -1;
    size_t o = 13;
    if (flags & 0x80) {
        gsize = 2 << (flags & 7);
        if (o + (size_t)gsize * 3 > n) return false;
        memcpy(gpal, f + o, (size_t)gsize * 3);
        o += (size_t)gsize * 3;
    }
    while (o < n) {
        uint8_t b = f[o++];
        if (b == 0x21) {                                 /* extension */
            if (o >= n) return false;
            uint8_t label = f[o++];
            while (o < n && f[o]) {
                uint8_t len = f[o];
                if (label == 0xF9 && len >= 4 && o + 4 < n && (f[o + 1] & 1)) trans = f[o + 4];
                o += 1 + len;
            }
            o++;
        } else if (b == 0x2C) {                          /* image */
            if (o + 9 > n) return false;
            int ix = f[o] | f[o + 1] << 8, iy = f[o + 2] | f[o + 3] << 8, iw = f[o + 4] | f[o + 5] << 8, ih = f[o + 6] | f[o + 7] << 8;
            int lf = f[o + 8];
            o += 9;
            int psize = gsize;
            memcpy(pal, gpal, sizeof pal);
            if (lf & 0x80) {
                psize = 2 << (lf & 7);
                if (o + (size_t)psize * 3 > n) return false;
                memcpy(pal, f + o, (size_t)psize * 3);
                o += (size_t)psize * 3;
            }
            bool inter = (lf & 0x40) != 0;
            if (o >= n) return false;
            int minc = f[o++];
            if (minc < 2 || minc > 11) return false;
            /* données LZW, en sous-blocs */
            size_t cap = n - o, zl = 0;
            uint8_t *z = malloc(cap + 4);
            if (!z) return false;
            while (o < n && f[o]) {
                uint8_t len = f[o++];
                if (o + len > n) break;
                memcpy(z + zl, f + o, len);
                zl += len;
                o += len;
            }
            uint8_t *idx = malloc((size_t)iw * ih + 1);
            static uint16_t prefix[4096];
            static uint8_t suffix[4096], stack[4097];
            if (!idx) { free(z); return false; }
            int clear = 1 << minc, eoi = clear + 1, next = clear + 2, size = minc + 1, prev = -1;
            uint8_t first = 0;
            size_t outn = 0, total = (size_t)iw * ih;
            uint32_t acc = 0;
            int nb = 0;
            size_t zp = 0;
            for (int i = 0; i < clear; i++) { prefix[i] = 0xFFFF; suffix[i] = (uint8_t)i; }
            while (outn < total) {
                while (nb < size && zp < zl) { acc |= (uint32_t)z[zp++] << nb; nb += 8; }
                if (nb < size) break;
                int code = (int)(acc & ((1u << size) - 1));
                acc >>= size;
                nb -= size;
                if (code == clear) { next = clear + 2; size = minc + 1; prev = -1; continue; }
                if (code == eoi) break;
                int sp = 0, cur = code;
                if (code >= next) { if (prev < 0) break; stack[sp++] = first; cur = prev; }
                while (cur >= clear && sp < 4096) { stack[sp++] = suffix[cur]; cur = prefix[cur]; }
                if (cur >= clear) break;
                stack[sp++] = (uint8_t)cur;
                first = (uint8_t)cur;
                while (sp && outn < total) idx[outn++] = stack[--sp];
                if (prev >= 0 && next < 4096) { prefix[next] = (uint16_t)prev; suffix[next] = first; next++; if (next == (1 << size) && size < 12) size++; }
                prev = code;
            }
            free(z);
            *out = canvas_new(W, H);
            if (!out->px) { free(idx); return false; }
            /* lignes dans l'ordre, ou entrelacées (1/8, 1/8 décalé de 4, 1/4, 1/2) */
            static const int start[4] = { 0, 4, 2, 1 }, step[4] = { 8, 8, 4, 2 };
            int row = 0, pass = 0;
            for (int r = 0; r < ih; r++) {
                int y = r;
                if (inter) {
                    while (pass < 4 && start[pass] + row * step[pass] >= ih) { pass++; row = 0; }
                    if (pass >= 4) break;
                    y = start[pass] + row * step[pass];
                    row++;
                }
                if (iy + y >= H) continue;
                uint32_t *d = out->px + (size_t)(iy + y) * out->stride;
                for (int x = 0; x < iw && ix + x < W; x++) {
                    size_t k = (size_t)r * iw + x;
                    int c = k < outn ? idx[k] : 0;
                    if (c == trans || c >= MAX(psize, 1)) continue;
                    d[ix + x] = 0xFF000000u | (uint32_t)pal[c * 3] << 16 | (uint32_t)pal[c * 3 + 1] << 8 | pal[c * 3 + 2];
                }
            }
            free(idx);
            return true;
        } else if (b == 0x3B) break;
        else return false;
    }
    return false;
}

/* PPM / PGM binaires (P6, P5) */
bool ppm_decode(const uint8_t *f, size_t n, canvas_t *out) {
    if (n < 8 || f[0] != 'P' || (f[1] != '6' && f[1] != '5')) return false;
    int v[3], k = 0;
    size_t o = 2;
    while (k < 3 && o < n) {
        while (o < n && (f[o] == ' ' || f[o] == '\n' || f[o] == '\r' || f[o] == '\t')) o++;
        if (o < n && f[o] == '#') { while (o < n && f[o] != '\n') o++; continue; }
        int x = 0;
        while (o < n && f[o] >= '0' && f[o] <= '9') x = x * 10 + (f[o++] - '0');
        v[k++] = x;
    }
    o++;
    int w = v[0], h = v[1], ch = f[1] == '6' ? 3 : 1;
    if (k < 3 || w <= 0 || h <= 0 || w > 8192 || h > 8192 || v[2] != 255 || o + (size_t)w * h * ch > n) return false;
    *out = canvas_new(w, h);
    if (!out->px) return false;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *p = f + o + ((size_t)y * w + x) * ch;
            uint32_t r = p[0], g = ch == 3 ? p[1] : p[0], b = ch == 3 ? p[2] : p[0];
            out->px[(size_t)y * out->stride + x] = 0xFF000000u | r << 16 | g << 8 | b;
        }
    return true;
}
