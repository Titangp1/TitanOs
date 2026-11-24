/* TitanOs - images PNG : écriture (captures d'écran) et lecture (visionneuse)
 *
 * Écriture : données non compressées (blocs « stored » de deflate), simple et rapide.
 * Lecture : décompression deflate complète (codes de Huffman fixes et dynamiques), filtres
 * PNG, images 8 bits en niveaux de gris, RGB, RGBA ou à palette, sans entrelacement. */
#include "desk.h"

/* ---------------- sommes de contrôle ---------------- */
static uint32_t crc_table[256];
static uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n) {
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    c = ~c;
    while (n--) c = crc_table[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* ---------------- écriture ---------------- */
/* image BGRA (prémultipliée, opaque à l'écran) -> fichier PNG RGB ; renvoie la taille, 0 si échec */
size_t png_encode(const uint32_t *px, int w, int h, int stride, uint8_t **out) {
    size_t raw = (size_t)(w * 3 + 1) * h;
    size_t nblk = (raw + 65534) / 65535;
    size_t idat = 2 + raw + nblk * 5 + 4;
    size_t total = 8 + 25 + 12 + idat + 12;
    uint8_t *b = malloc(total);
    if (!b) return 0;
    uint8_t *q = b;
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    memcpy(q, sig, 8); q += 8;
    put32(q, 13); memcpy(q + 4, "IHDR", 4);
    put32(q + 8, (uint32_t)w); put32(q + 12, (uint32_t)h);
    q[16] = 8; q[17] = 2; q[18] = 0; q[19] = 0; q[20] = 0;
    put32(q + 21, crc32_update(0, q + 4, 17)); q += 25;
    put32(q, (uint32_t)idat); memcpy(q + 4, "IDAT", 4);
    uint8_t *d = q + 8, *start = q + 4;
    *d++ = 0x78; *d++ = 0x01;                     /* en-tête zlib */
    uint32_t a1 = 1, a2 = 0;                      /* Adler-32 */
    size_t left = raw, row_pos = 0;
    int y = 0;
    while (left) {
        size_t n = left > 65535 ? 65535 : left;
        left -= n;
        *d++ = left ? 0 : 1;
        *d++ = (uint8_t)n; *d++ = (uint8_t)(n >> 8); *d++ = (uint8_t)~n; *d++ = (uint8_t)(~n >> 8);
        for (size_t k = 0; k < n; k++) {
            uint8_t v;
            if (row_pos == 0) v = 0;              /* filtre : aucun */
            else {
                size_t i = (row_pos - 1) / 3, c = (row_pos - 1) % 3;
                uint32_t p = px[(size_t)y * stride + i];
                v = (uint8_t)(c == 0 ? p >> 16 : c == 1 ? p >> 8 : p);
            }
            *d++ = v;
            a1 = (a1 + v) % 65521; a2 = (a2 + a1) % 65521;
            if (++row_pos == (size_t)w * 3 + 1) { row_pos = 0; y++; }
        }
    }
    put32(d, a2 << 16 | a1); d += 4;
    put32(d, crc32_update(0, start, (size_t)(d - start))); d += 4;
    put32(d, 0); memcpy(d + 4, "IEND", 4); put32(d + 8, crc32_update(0, (const uint8_t *)"IEND", 4)); d += 12;
    *out = b;
    return (size_t)(d - b);
}

/* ---------------- deflate ---------------- */
typedef struct { const uint8_t *p, *end; uint32_t bits; int nb; bool bad; } bs_t;
static int getbit(bs_t *s) {
    if (!s->nb) { if (s->p >= s->end) { s->bad = true; return 0; } s->bits = *s->p++; s->nb = 8; }
    int b = s->bits & 1;
    s->bits >>= 1; s->nb--;
    return b;
}
static int getbits(bs_t *s, int n) { int v = 0; for (int i = 0; i < n; i++) v |= getbit(s) << i; return v; }

typedef struct { uint16_t count[16], sym[320]; } huff_t;
static void huff_build(huff_t *h, const uint8_t *len, int n) {
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    uint16_t off[16];
    off[1] = 0;
    for (int i = 1; i < 15; i++) off[i + 1] = (uint16_t)(off[i] + h->count[i]);
    for (int i = 0; i < n; i++) if (len[i]) h->sym[off[len[i]]++] = (uint16_t)i;
}
static int huff_decode(bs_t *s, const huff_t *h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        code |= getbit(s);
        int c = h->count[l];
        if (code - c < first) return h->sym[index + (code - first)];
        index += c; first += c; first <<= 1; code <<= 1;
        if (s->bad) return -1;
    }
    s->bad = true;
    return -1;
}

static const uint16_t lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

/* décompresse un flux zlib dans out (taille connue d'avance) ; renvoie le nombre d'octets */
static size_t inflate_zlib(const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    if (n < 2) return 0;
    bs_t s = { in + 2, in + n, 0, 0, false };
    size_t o = 0;
    huff_t lh, dh;
    for (;;) {
        int final = getbit(&s), type = getbits(&s, 2);
        if (s.bad) return o;
        if (type == 0) {
            s.nb = 0;
            if (s.end - s.p < 4) return o;
            unsigned len = s.p[0] | s.p[1] << 8;
            s.p += 4;
            if ((size_t)(s.end - s.p) < len || o + len > cap) return o;
            memcpy(out + o, s.p, len);
            o += len; s.p += len;
        } else if (type == 1 || type == 2) {
            uint8_t lens[320];
            if (type == 1) {
                for (int i = 0; i < 288; i++) lens[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
                huff_build(&lh, lens, 288);
                for (int i = 0; i < 30; i++) lens[i] = 5;
                huff_build(&dh, lens, 30);
            } else {
                int nlen = getbits(&s, 5) + 257, ndist = getbits(&s, 5) + 1, ncode = getbits(&s, 4) + 4;
                static const uint8_t ord[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
                uint8_t cl[19] = { 0 };
                for (int i = 0; i < ncode; i++) cl[ord[i]] = (uint8_t)getbits(&s, 3);
                huff_t ch;
                huff_build(&ch, cl, 19);
                int i = 0;
                while (i < nlen + ndist) {
                    int sym = huff_decode(&s, &ch);
                    if (sym < 0) return o;
                    if (sym < 16) lens[i++] = (uint8_t)sym;
                    else {
                        int rep, v = 0;
                        if (sym == 16) { if (!i) return o; v = lens[i - 1]; rep = 3 + getbits(&s, 2); }
                        else if (sym == 17) rep = 3 + getbits(&s, 3);
                        else rep = 11 + getbits(&s, 7);
                        while (rep-- && i < nlen + ndist) lens[i++] = (uint8_t)v;
                    }
                }
                huff_build(&lh, lens, nlen);
                huff_build(&dh, lens + nlen, ndist);
            }
            for (;;) {
                int sym = huff_decode(&s, &lh);
                if (sym < 0) return o;
                if (sym < 256) { if (o >= cap) return o; out[o++] = (uint8_t)sym; }
                else if (sym == 256) break;
                else {
                    sym -= 257;
                    if (sym >= 29) return o;
                    int len = lbase[sym] + getbits(&s, lext[sym]);
                    int ds = huff_decode(&s, &dh);
                    if (ds < 0 || ds >= 30) return o;
                    size_t dist = dbase[ds] + (size_t)getbits(&s, dext[ds]);
                    if (dist > o || o + (size_t)len > cap) return o;
                    for (int k = 0; k < len; k++, o++) out[o] = out[o - dist];
                }
            }
        } else return o;
        if (final) return o;
    }
}

/* ---------------- lecture ---------------- */
static int iabs(int v) { return v < 0 ? -v : v; }
static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = iabs(p - a), pb = iabs(p - b), pc = iabs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* fichier PNG -> canvas BGRA prémultiplié ; false si le format n'est pas pris en charge */
bool png_decode(const uint8_t *f, size_t n, canvas_t *out) {
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (n < 33 || memcmp(f, sig, 8) || memcmp(f + 12, "IHDR", 4)) return false;
    int w = (int)get32(f + 16), h = (int)get32(f + 20), depth = f[24], ct = f[25], inter = f[28];
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || depth != 8 || inter) return false;
    int ch = ct == 0 ? 1 : ct == 2 ? 3 : ct == 3 ? 1 : ct == 4 ? 2 : ct == 6 ? 4 : 0;
    if (!ch) return false;
    uint8_t pal[256 * 4];
    memset(pal, 255, sizeof pal);
    /* morceaux IDAT mis bout à bout */
    size_t zn = 0, zcap = n;
    uint8_t *z = malloc(zcap);
    if (!z) return false;
    for (size_t o = 8; o + 12 <= n;) {
        size_t len = get32(f + o);
        if (o + 12 + len > n) break;
        const uint8_t *t = f + o + 4, *d = f + o + 8;
        if (!memcmp(t, "IDAT", 4)) { memcpy(z + zn, d, len); zn += len; }
        else if (!memcmp(t, "PLTE", 4)) for (size_t i = 0; i < len / 3 && i < 256; i++) { pal[i * 4] = d[i * 3]; pal[i * 4 + 1] = d[i * 3 + 1]; pal[i * 4 + 2] = d[i * 3 + 2]; }
        else if (!memcmp(t, "tRNS", 4) && ct == 3) for (size_t i = 0; i < len && i < 256; i++) pal[i * 4 + 3] = d[i];
        else if (!memcmp(t, "IEND", 4)) break;
        o += 12 + len;
    }
    size_t rowb = (size_t)w * ch, rawn = (rowb + 1) * h;
    uint8_t *raw = malloc(rawn);
    if (!raw) { free(z); return false; }
    size_t got = inflate_zlib(z, zn, raw, rawn);
    free(z);
    if (got < rawn) { free(raw); return false; }
    /* filtres, ligne par ligne */
    for (int y = 0; y < h; y++) {
        uint8_t *r = raw + (rowb + 1) * y, *cur = r + 1, *prev = y ? raw + (rowb + 1) * (y - 1) + 1 : NULL;
        int ft = r[0];
        for (size_t i = 0; i < rowb; i++) {
            int a = i >= (size_t)ch ? cur[i - ch] : 0, b = prev ? prev[i] : 0, c = prev && i >= (size_t)ch ? prev[i - ch] : 0;
            switch (ft) {
            case 1: cur[i] = (uint8_t)(cur[i] + a); break;
            case 2: cur[i] = (uint8_t)(cur[i] + b); break;
            case 3: cur[i] = (uint8_t)(cur[i] + ((a + b) >> 1)); break;
            case 4: cur[i] = (uint8_t)(cur[i] + paeth(a, b, c)); break;
            }
        }
    }
    *out = canvas_new(w, h);
    if (!out->px) { free(raw); return false; }
    for (int y = 0; y < h; y++) {
        const uint8_t *s = raw + (rowb + 1) * y + 1;
        uint32_t *d = out->px + (size_t)y * out->stride;
        for (int x = 0; x < w; x++, s += ch) {
            uint32_t R_, G, B, A = 255;
            if (ct == 0) R_ = G = B = s[0];
            else if (ct == 4) { R_ = G = B = s[0]; A = s[1]; }
            else if (ct == 3) { const uint8_t *p = pal + s[0] * 4; R_ = p[0]; G = p[1]; B = p[2]; A = p[3]; }
            else { R_ = s[0]; G = s[1]; B = s[2]; if (ct == 6) A = s[3]; }
            d[x] = A << 24 | (R_ * A / 255) << 16 | (G * A / 255) << 8 | (B * A / 255);
        }
    }
    free(raw);
    return true;
}

/* fichier BMP 24 ou 32 bits non compressé -> canvas */
bool bmp_decode(const uint8_t *f, size_t n, canvas_t *out) {
    if (n < 54 || f[0] != 'B' || f[1] != 'M') return false;
    uint32_t off = f[10] | f[11] << 8 | (uint32_t)f[12] << 16 | (uint32_t)f[13] << 24;
    int w = f[18] | f[19] << 8 | f[20] << 16 | f[21] << 24;
    int h = f[22] | f[23] << 8 | f[24] << 16 | f[25] << 24;
    int bpp = f[28] | f[29] << 8, comp = f[30];
    bool top = h < 0;
    if (top) h = -h;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || (bpp != 24 && bpp != 32) || (comp != 0 && comp != 3)) return false;
    size_t rowb = ((size_t)w * (bpp / 8) + 3) & ~(size_t)3;
    if (off + rowb * h > n) return false;
    *out = canvas_new(w, h);
    if (!out->px) return false;
    for (int y = 0; y < h; y++) {
        const uint8_t *s = f + off + rowb * (top ? y : h - 1 - y);
        uint32_t *d = out->px + (size_t)y * out->stride;
        for (int x = 0; x < w; x++, s += bpp / 8) d[x] = 0xFF000000u | (uint32_t)s[2] << 16 | (uint32_t)s[1] << 8 | s[0];
    }
    return true;
}
