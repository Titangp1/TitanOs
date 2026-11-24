/* TitanOs - petite bibliothèque C maison (pas de libc) */
#include "base.h"

/* ---------------- mémoire et chaînes ---------------- */
void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *dd = d;
    const uint8_t *ss = s;
    while (n >= 8) { uint64_t v; __builtin_memcpy(&v, ss, 8); __builtin_memcpy(dd, &v, 8); dd += 8; ss += 8; n -= 8; }
    while (n--) *dd++ = *ss++;
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    uint8_t *dd = d;
    const uint8_t *ss = s;
    if (dd == ss || n == 0) return d;
    if (dd < ss || dd >= ss + n) return memcpy(d, s, n);
    while (n--) dd[n] = ss[n];
    return d;
}

void *memset(void *d, int c, size_t n) {
    uint8_t *dd = d;
    uint64_t v = (uint8_t)c * 0x0101010101010101ull;
    while (n >= 8) { __builtin_memcpy(dd, &v, 8); dd += 8; n -= 8; }
    while (n--) *dd++ = (uint8_t)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b) return (uint8_t)*a - (uint8_t)*b;
        if (!*a) return 0;
    }
    return 0;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int strcasecmp(const char *a, const char *b) {
    while (*a && lower((uint8_t)*a) == lower((uint8_t)*b)) { a++; b++; }
    return lower((uint8_t)*a) - lower((uint8_t)*b);
}

char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

char *strrchr(const char *s, int c) {
    const char *r = NULL;
    for (;; s++) {
        if (*s == (char)c) r = s;
        if (!*s) return (char *)r;
    }
}

char *strstr(const char *h, const char *n) {
    size_t ln = strlen(n);
    for (; *h; h++)
        if (!strncmp(h, n, ln)) return (char *)h;
    return ln ? NULL : (char *)h;
}

size_t strlcpy(char *d, const char *s, size_t n) {
    size_t l = strlen(s);
    if (n) {
        size_t c = l < n - 1 ? l : n - 1;
        memcpy(d, s, c);
        d[c] = 0;
    }
    return l;
}

size_t strlcat(char *d, const char *s, size_t n) {
    size_t l = strlen(d);
    if (l >= n) return l + strlen(s);
    return l + strlcpy(d + l, s, n - l);
}

char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

bool str_starts(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }

bool str_ends(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcasecmp(s + a - b, suf);
}

/* lettre de base d'un caractère Latin-1 accentué (é -> e) */
static int fold(int c) {
    static const char map[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    if (c >= 0xC0 && c <= 0xFF) c = (uint8_t)map[c - 0xC0];
    return lower(c);
}

bool str_icontains(const char *hay, const char *needle) {
    if (!*needle) return true;
    for (const char *h = hay; *h; h += utf8_next(h, 0)) {
        const char *a = h, *b = needle;
        for (;;) {
            if (!*b) return true;
            if (!*a) break;
            int ca = utf8_decode(&a), cb = utf8_decode(&b);
            if (fold(ca) != fold(cb)) break;
        }
    }
    return false;
}

/* ---------------- printf ---------------- */
static void out_c(char **p, char *end, char c) { if (*p < end) **p = c; (*p)++; }

static void out_num(char **p, char *end, uint64_t v, int base, bool upper, bool neg, int width, char pad, bool left) {
    char tmp[24];
    int n = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { tmp[n++] = dig[v % base]; v /= base; } while (v);
    int len = n + (neg ? 1 : 0);
    if (!left && pad == '0' && neg) out_c(p, end, '-');
    if (!left) for (int i = len; i < width; i++) out_c(p, end, pad);
    if (neg && !(pad == '0' && !left)) out_c(p, end, '-');
    while (n) out_c(p, end, tmp[--n]);
    if (left) for (int i = len; i < width; i++) out_c(p, end, ' ');
}

int vsnprintf(char *buf, size_t n, const char *f, va_list ap) {
    char *p = buf, *end = n ? buf + n - 1 : buf;
    for (; *f; f++) {
        if (*f != '%') { out_c(&p, end, *f); continue; }
        f++;
        bool left = false;
        char pad = ' ';
        int width = 0, prec = -1, lng = 0;
        for (;; f++) {
            if (*f == '-') left = true;
            else if (*f == '0') pad = '0';
            else break;
        }
        if (*f == '*') { width = va_arg(ap, int); f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') {
            f++;
            prec = 0;
            if (*f == '*') { prec = va_arg(ap, int); f++; }
            while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
        }
        while (*f == 'l') { lng++; f++; }
        if (*f == 'z') { lng = 2; f++; }
        switch (*f) {
        case 'd': case 'i': {
            int64_t v = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
            out_num(&p, end, v < 0 ? -(uint64_t)v : (uint64_t)v, 10, false, v < 0, width, pad, left);
            break;
        }
        case 'u': case 'x': case 'X': {
            uint64_t v = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            out_num(&p, end, v, *f == 'u' ? 10 : 16, *f == 'X', false, width, pad, left);
            break;
        }
        case 'p':
            out_c(&p, end, '0'); out_c(&p, end, 'x');
            out_num(&p, end, (uintptr_t)va_arg(ap, void *), 16, false, false, 0, ' ', false);
            break;
        case 'c': out_c(&p, end, (char)va_arg(ap, int)); break;
        case 'f': {
            double v = va_arg(ap, double);
            if (prec < 0) prec = 2;
            bool neg = v < 0;
            if (neg) v = -v;
            uint64_t scale = 1;
            for (int i = 0; i < prec; i++) scale *= 10;
            uint64_t q = (uint64_t)(v * (double)scale + 0.5);
            out_num(&p, end, q / scale, 10, false, neg, width > prec + 1 ? width - prec - 1 : 0, pad, false);
            if (prec) {
                out_c(&p, end, ',');
                out_num(&p, end, q % scale, 10, false, false, prec, '0', false);
            }
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = 0;
            while (s[len] && (prec < 0 || len < prec)) len++;
            int chars = 0;
            for (int i = 0; i < len; i++) if (((uint8_t)s[i] & 0xC0) != 0x80) chars++;
            if (!left) for (int i = chars; i < width; i++) out_c(&p, end, ' ');
            for (int i = 0; i < len; i++) out_c(&p, end, s[i]);
            if (left) for (int i = chars; i < width; i++) out_c(&p, end, ' ');
            break;
        }
        case '%': out_c(&p, end, '%'); break;
        default: out_c(&p, end, '%'); out_c(&p, end, *f); break;
        }
    }
    if (n) *(p < end ? p : end) = 0;
    return (int)(p - buf);
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

long strtol(const char *s, char **end, int base) {
    while (*s == ' ' || *s == '\t') s++;
    bool neg = false;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    if (base == 0) base = 10;
    long v = 0;
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (lower(*s) >= 'a' && lower(*s) <= 'z') d = lower(*s) - 'a' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }

void fmt_size(char *buf, size_t n, uint64_t b) {
    if (b < 1024) snprintf(buf, n, "%u o", (unsigned)b);
    else if (b < 1024 * 1024) snprintf(buf, n, "%.1f Ko", b / 1024.0);
    else if (b < 1024ull * 1024 * 1024) snprintf(buf, n, "%.1f Mo", b / 1048576.0);
    else snprintf(buf, n, "%.1f Go", b / 1073741824.0);
}

/* ---------------- UTF-8 ---------------- */
int utf8_decode(const char **sp) {
    const uint8_t *s = (const uint8_t *)*sp;
    int c = *s;
    if (!c) return 0;
    int n = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
    if (n) c &= 0x3F >> n;
    s++;
    for (int i = 0; i < n; i++) {
        if ((*s & 0xC0) != 0x80) { c = 0xFFFD; break; }
        c = (c << 6) | (*s++ & 0x3F);
    }
    *sp = (const char *)s;
    return c;
}

int utf8_encode(char *o, int c) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) {
        o[0] = (char)(0xE0 | c >> 12); o[1] = (char)(0x80 | ((c >> 6) & 0x3F)); o[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | c >> 18); o[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((c >> 6) & 0x3F)); o[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

int utf8_prev(const char *s, int pos) {
    if (pos <= 0) return 0;
    pos--;
    while (pos > 0 && ((uint8_t)s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

int utf8_next(const char *s, int pos) {
    if (!s[pos]) return pos;
    pos++;
    while (s[pos] && ((uint8_t)s[pos] & 0xC0) == 0x80) pos++;
    return pos;
}

int utf8_len(const char *s) { int n = 0; while (utf8_decode(&s)) n++; return n; }

int utf8_len_n(const char *s, int bytes) {
    int n = 0;
    for (int i = 0; i < bytes && s[i]; i++) if (((uint8_t)s[i] & 0xC0) != 0x80) n++;
    return n;
}

/* ---------------- rectangles ---------------- */
rect_t rect_isect(rect_t a, rect_t b) {
    int x0 = MAX(a.x, b.x), y0 = MAX(a.y, b.y);
    int x1 = MIN(a.x + a.w, b.x + b.w), y1 = MIN(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0) return R(0, 0, 0, 0);
    return R(x0, y0, x1 - x0, y1 - y0);
}

rect_t rect_union(rect_t a, rect_t b) {
    if (rect_empty(a)) return b;
    if (rect_empty(b)) return a;
    int x0 = MIN(a.x, b.x), y0 = MIN(a.y, b.y);
    int x1 = MAX(a.x + a.w, b.x + b.w), y1 = MAX(a.y + a.h, b.y + b.h);
    return R(x0, y0, x1 - x0, y1 - y0);
}

/* ---------------- allocateur ----------------
 * Petits blocs : classes de tailles (puissances de 2), tirées de pages de 1 Mo.
 * Gros blocs (> 64 Ko) : demandés directement au système et rendus à la libération. */
#define BIG_LIMIT (64 * 1024)
#define NCLASS    13                 /* 16 o ... 64 Ko */
typedef struct { uint64_t size; uint64_t cls; } blkhdr_t;   /* 16 octets : alignement conservé */
static void *freelist[NCLASS];
static uint8_t *slab_cur, *slab_end;

static int class_of(size_t n) {
    int c = 0;
    size_t s = 16;
    while (s < n) { s <<= 1; c++; }
    return c;
}

void *malloc(size_t n) {
    if (n == 0) n = 1;
    size_t total = n + sizeof(blkhdr_t);
    if (total > BIG_LIMIT) {
        size_t sz = (total + 4095) & ~(size_t)4095;
        blkhdr_t *h = plat_pages(sz);
        if (!h) return NULL;
        h->size = sz;
        h->cls = 0xFF;
        return h + 1;
    }
    int c = class_of(total);
    if (freelist[c]) {
        blkhdr_t *h = freelist[c];
        freelist[c] = *(void **)(h + 1);
        return h + 1;
    }
    size_t sz = (size_t)16 << c;
    if (slab_cur + sz > slab_end) {
        slab_cur = plat_pages(1 << 20);
        if (!slab_cur) return NULL;
        slab_end = slab_cur + (1 << 20);
    }
    blkhdr_t *h = (blkhdr_t *)slab_cur;
    slab_cur += sz;
    h->size = sz;
    h->cls = (uint64_t)c;
    return h + 1;
}

void free(void *p) {
    if (!p) return;
    blkhdr_t *h = (blkhdr_t *)p - 1;
    if (h->cls == 0xFF) { plat_pages_free(h, h->size); return; }
    *(void **)p = freelist[h->cls];
    freelist[h->cls] = h;
}

void *calloc(size_t n, size_t sz) {
    void *p = malloc(n * sz);
    if (p) memset(p, 0, n * sz);
    return p;
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    blkhdr_t *h = (blkhdr_t *)p - 1;
    size_t have = h->size - sizeof(blkhdr_t);
    if (n <= have) return p;
    void *q = malloc(n);
    if (!q) return NULL;
    memcpy(q, p, have);
    free(p);
    return q;
}

/* tri par insertion binaire puis fusion : simple et stable */
void sort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
    uint8_t *a = base;
    uint8_t tmp[512];
    if (sz > sizeof tmp) return;
    for (size_t i = 1; i < n; i++) {
        memcpy(tmp, a + i * sz, sz);
        size_t lo = 0, hi = i;
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (cmp(a + mid * sz, tmp) <= 0) lo = mid + 1; else hi = mid;
        }
        memmove(a + (lo + 1) * sz, a + lo * sz, (i - lo) * sz);
        memcpy(a + lo * sz, tmp, sz);
    }
}

/* arrondis (le compilateur les appelle quand le processeur n'a pas SSE4.1) */
float floorf(float x) { int i = (int)x; return (float)(x < (float)i ? i - 1 : i); }
float ceilf(float x) { int i = (int)x; return (float)(x > (float)i ? i + 1 : i); }

/* sinus sans bibliothèque mathématique : réduction à [-pi/2, pi/2] puis série */
float fsin(float x) {
    const float PI = 3.14159265f, TWO_PI = 6.28318531f;
    int k = (int)(x / TWO_PI + (x >= 0 ? 0.5f : -0.5f));
    x -= k * TWO_PI;
    if (x > PI / 2) x = PI - x;
    else if (x < -PI / 2) x = -PI - x;
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}
float fcos(float x) { return fsin(x + 1.57079633f); }
