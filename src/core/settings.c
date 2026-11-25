/* TitanOs - réglages, thèmes, fonds d'écran et heure locale */
#include "desk.h"

settings_t g_set;

/* ---------------- couleurs d'accent ---------------- */
static const uint32_t accents[ACCENT_COUNT] = { 0x6366F1, 0x3B82F6, 0x06B6D4, 0x22C55E, 0xF97316, 0xEC4899 };
uint32_t accent_of(int i) { return OPAQUE(accents[(i >= 0 && i < ACCENT_COUNT) ? i : 0]); }
uint32_t accent(void) { return accent_of(g_set.accent); }

/* ---------------- fonds d'écran ----------------
 * Un dégradé diagonal à trois couleurs, enrichi de grandes taches de lumière
 * colorées : c'est ce qui donne de la matière au verre posé dessus. */
typedef struct { float x, y, r; uint32_t col; float a; } blob_t;
typedef struct { const char *name; uint32_t c[3]; blob_t blob[4]; } wall_t;
static const wall_t walls[WALLPAPER_COUNT] = {
    { "Titan", { 0x0B1226, 0x1E1B4B, 0x4C1D95 },
      { { 0.18f, 0.22f, 0.42f, 0x3B82F6, 0.75f }, { 0.82f, 0.30f, 0.38f, 0xA855F7, 0.80f },
        { 0.60f, 0.92f, 0.45f, 0xEC4899, 0.55f }, { 0.05f, 0.85f, 0.30f, 0x22D3EE, 0.45f } } },
    { "Océan", { 0x03142B, 0x0C4A6E, 0x0E7490 },
      { { 0.20f, 0.25f, 0.40f, 0x0EA5E9, 0.70f }, { 0.85f, 0.20f, 0.35f, 0x2DD4BF, 0.65f },
        { 0.55f, 0.90f, 0.45f, 0x3B82F6, 0.60f }, { 0.02f, 0.80f, 0.30f, 0x22D3EE, 0.40f } } },
    { "Aurore", { 0x1E0B36, 0x831843, 0xC2410C },
      { { 0.15f, 0.20f, 0.40f, 0xA855F7, 0.70f }, { 0.85f, 0.25f, 0.38f, 0xF97316, 0.75f },
        { 0.55f, 0.95f, 0.45f, 0xEC4899, 0.65f }, { 0.40f, 0.45f, 0.25f, 0xFBBF24, 0.35f } } },
    { "Forêt", { 0x041F14, 0x14532D, 0x4D7C0F },
      { { 0.20f, 0.20f, 0.40f, 0x10B981, 0.65f }, { 0.85f, 0.30f, 0.38f, 0x84CC16, 0.60f },
        { 0.55f, 0.95f, 0.45f, 0x14B8A6, 0.55f }, { 0.95f, 0.90f, 0.30f, 0xFDE68A, 0.30f } } },
    { "Nuit", { 0x020617, 0x0F172A, 0x1E293B },
      { { 0.20f, 0.20f, 0.40f, 0x1E3A8A, 0.70f }, { 0.85f, 0.30f, 0.35f, 0x4338CA, 0.55f },
        { 0.55f, 0.95f, 0.45f, 0x0F766E, 0.45f }, { 0.95f, 0.05f, 0.25f, 0x64748B, 0.30f } } },
    { "Braise", { 0x1C0A00, 0x7C2D12, 0xB45309 },
      { { 0.18f, 0.22f, 0.40f, 0xDC2626, 0.65f }, { 0.85f, 0.25f, 0.38f, 0xF59E0B, 0.70f },
        { 0.55f, 0.95f, 0.45f, 0xF97316, 0.60f }, { 0.05f, 0.85f, 0.28f, 0xFBBF24, 0.35f } } },
};

const char *wallpaper_name(int i) { return walls[(i >= 0 && i < WALLPAPER_COUNT) ? i : 0].name; }

static void wall_pixels(canvas_t *c, rect_t area, int fw, int fh, int idx) {
    const wall_t *wp = &walls[(idx >= 0 && idx < WALLPAPER_COUNT) ? idx : 0];
    float diag = __builtin_sqrtf((float)fw * fw + (float)fh * fh);
    for (int j = 0; j < area.h; j++) {
        uint32_t *row = c->px + (size_t)(area.y + j) * c->stride + area.x;
        float fy = (float)j / fh;
        for (int i = 0; i < area.w; i++) {
            float fx = (float)i / fw;
            float t = (fx * fw + fy * fh) / (fw + fh) * 2;    /* diagonale, 0 à 2 */
            uint32_t a = t < 1 ? wp->c[0] : wp->c[1], b = t < 1 ? wp->c[1] : wp->c[2];
            float k = t < 1 ? t : t - 1;
            float r = (float)(a >> 16 & 255) + ((float)(b >> 16 & 255) - (float)(a >> 16 & 255)) * k;
            float g = (float)(a >> 8 & 255) + ((float)(b >> 8 & 255) - (float)(a >> 8 & 255)) * k;
            float bl = (float)(a & 255) + ((float)(b & 255) - (float)(a & 255)) * k;
            for (int n = 0; n < 4; n++) {
                const blob_t *o = &wp->blob[n];
                float dx = (fx - o->x) * fw / diag, dy = (fy - o->y) * fh / diag;
                float q = (dx * dx + dy * dy) / (o->r * o->r * 0.5f);
                float f = o->a / ((1 + q) * (1 + q));
                r += ((float)(o->col >> 16 & 255) - r) * f;
                g += ((float)(o->col >> 8 & 255) - g) * f;
                bl += ((float)(o->col & 255) - bl) * f;
            }
            /* léger bruit pour éviter les bandes dans les dégradés */
            uint32_t hsh = (uint32_t)(i * 374761393u + j * 668265263u);
            hsh = (hsh ^ (hsh >> 13)) * 1274126177u;
            float n = (float)((hsh >> 24) & 3) - 1.5f;
            int ir = (int)CLAMP(r + n, 0.f, 255.f), ig = (int)CLAMP(g + n, 0.f, 255.f), ib = (int)CLAMP(bl + n, 0.f, 255.f);
            row[i] = 0xFF000000u | (uint32_t)ir << 16 | (uint32_t)ig << 8 | (uint32_t)ib;
        }
    }
}

void wallpaper_render(canvas_t *c, int idx) {
    canvas_noclip(c);
    wall_pixels(c, R(0, 0, c->w, c->h), c->w, c->h, idx);
    /* chaîne de montagnes du logo, en filigrane en bas de l'écran */
    float s = c->h * 0.42f, base = (float)c->h;
    draw_logo(c, c->w * 0.80f, base - s, s, 34);
    draw_logo(c, c->w * 0.64f, base - s * 0.62f, s * 0.62f, 26);
    draw_logo(c, c->w * 0.95f, base - s * 0.55f, s * 0.55f, 22);
}

void wallpaper_thumb(canvas_t *c, int x, int y, int w, int h, int idx) {
    canvas_t t = canvas_new(w, h);
    if (!t.px) return;
    wall_pixels(&t, R(0, 0, w, h), w, h, idx);
    float s = h * 0.42f;
    draw_logo(&t, w * 0.80f, h - s, s, 34);
    gfx_blit(c, x, y, &t, 255);
    canvas_free(&t);
}

/* ---------------- fuseaux horaires ---------------- */
const tz_t tz_list[] = {
    { "Paris, Bruxelles, Genève (UTC+1)", 60, 1 },
    { "Londres, Lisbonne (UTC)", 0, 1 },
    { "Athènes, Helsinki (UTC+2)", 120, 1 },
    { "Montréal, New York (UTC-5)", -300, 2 },
    { "Chicago (UTC-6)", -360, 2 },
    { "Los Angeles (UTC-8)", -480, 2 },
    { "Martinique, Guadeloupe (UTC-4)", -240, 0 },
    { "Guyane (UTC-3)", -180, 0 },
    { "La Réunion, Maurice (UTC+4)", 240, 0 },
    { "Mayotte, Moscou (UTC+3)", 180, 0 },
    { "Dakar, Abidjan (UTC)", 0, 0 },
    { "Alger, Tunis, Kinshasa (UTC+1)", 60, 0 },
    { "Tokyo (UTC+9)", 540, 0 },
    { "Nouméa (UTC+11)", 660, 0 },
    { "Tahiti (UTC-10)", -600, 0 },
    { "Temps universel (UTC)", 0, 0 },
};
const int tz_count = countof(tz_list);

const char *const month_names[12] = { "janvier", "février", "mars", "avril", "mai", "juin", "juillet",
                                      "août", "septembre", "octobre", "novembre", "décembre" };
const char *const day_names[7] = { "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche" };

static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void datetime_from(int64_t t, datetime_t *dt) {
    int64_t days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
    int64_t secs = t - days * 86400;
    dt->hour = (int)(secs / 3600);
    dt->min = (int)(secs / 60 % 60);
    dt->sec = (int)(secs % 60);
    dt->wday = (int)((days % 7 + 7 + 3) % 7);        /* 1970-01-01 était un jeudi */
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    dt->day = (int)(doy - (153 * mp + 2) / 5 + 1);
    dt->month = (int)(mp < 10 ? mp + 3 : mp - 9);
    dt->year = (int)(yoe + era * 400 + (dt->month <= 2));
}

/* dimanche numéro n (1 = premier, -1 = dernier) d'un mois, en jours depuis 1970 */
static int64_t nth_sunday(int y, int m, int n) {
    if (n > 0) {
        int64_t d = days_from_civil(y, m, 1);
        int wd = (int)((d % 7 + 7 + 3) % 7);
        return d + (6 - wd) + 7 * (n - 1);
    }
    int64_t d = days_from_civil(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - 1;
    int wd = (int)((d % 7 + 7 + 3) % 7);
    return d - (wd + 1) % 7;
}

static bool dst_active(int64_t utc, const tz_t *tz) {
    datetime_t dt;
    datetime_from(utc, &dt);
    if (tz->dst == 1) {      /* Europe : du dernier dimanche de mars au dernier d'octobre, 1 h UTC */
        int64_t a = nth_sunday(dt.year, 3, -1) * 86400 + 3600, b = nth_sunday(dt.year, 10, -1) * 86400 + 3600;
        return utc >= a && utc < b;
    }
    if (tz->dst == 2) {      /* Amérique du Nord : 2e dimanche de mars au 1er de novembre, 2 h locale */
        int64_t off = tz->offset_min * 60;
        int64_t a = nth_sunday(dt.year, 3, 2) * 86400 + 7200 - off, b = nth_sunday(dt.year, 11, 1) * 86400 + 3600 - off;
        return utc >= a && utc < b;
    }
    return false;
}

void local_now(datetime_t *dt) {
    int64_t t = plat_time();
    if (!g_set.rtc_local) {
        const tz_t *tz = &tz_list[(g_set.tz >= 0 && g_set.tz < tz_count) ? g_set.tz : 0];
        t += tz->offset_min * 60 + (dst_active(t, tz) ? 3600 : 0);
    }
    datetime_from(t, dt);
}

/* ---------------- lecture et écriture ---------------- */
static void defaults(void) {
    memset(&g_set, 0, sizeof g_set);
    strlcpy(g_set.user, "Titan", sizeof g_set.user);
    g_set.sounds = true;
    g_set.volume = 70;
    g_set.rtc_local = true;     /* le VAIO sort de Windows : son horloge est en heure locale */
    g_set.brightness = -1;
    g_set.glass = 1;
    g_set.pointer = 3;
    g_set.welcome = 1;
    static const int def[] = { ICON_FILES, ICON_BROWSER, ICON_TERMINAL, ICON_NOTES, ICON_SETTINGS, ICON_SYSTEM, ICON_ABOUT };
    g_set.ndesk = 0;
    for (int i = 0; i < (int)(sizeof def / sizeof def[0]); i++) {
        desk_item_t *d = &g_set.desk[g_set.ndesk++];
        d->app = (signed char)def[i];
        d->col = d->row = -1;
        d->path[0] = 0;
    }
}

void settings_load(void) {
    defaults();
    char *data;
    size_t len;
    if (plat_readfile(plat_config_path(), &data, &len) != 0) { g_set.welcome = 0; return; }   /* tout nouveau : accueil */
    char *p = data, *end = data + len;
    while (p < end) {
        char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        *nl = 0;
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = 0;
            const char *k = p, *v = eq + 1;
            int n = atoi(v);
            if (!strcmp(k, "fond")) g_set.wallpaper = CLAMP(n, 0, WALLPAPER_COUNT - 1);
            else if (!strcmp(k, "accent")) g_set.accent = CLAMP(n, 0, ACCENT_COUNT - 1);
            else if (!strcmp(k, "utilisateur")) strlcpy(g_set.user, v, sizeof g_set.user);
            else if (!strcmp(k, "secondes")) g_set.seconds = n != 0;
            else if (!strcmp(k, "nuit")) g_set.night = CLAMP(n, 0, 2);
            else if (!strcmp(k, "clavier")) g_set.layout = !strcmp(v, "us");
            else if (!strcmp(k, "fuseau")) g_set.tz = CLAMP(n, 0, tz_count - 1);
            else if (!strcmp(k, "accueil")) g_set.welcome = n != 0;
            else if (!strcmp(k, "horloge_locale")) g_set.rtc_local = n != 0;
            else if (!strcmp(k, "luminosite")) g_set.brightness = CLAMP(n, -1, 100);
            else if (!strcmp(k, "verre")) g_set.glass = CLAMP(n, 0, 2);
            else if (!strcmp(k, "sons")) g_set.sounds = n != 0;
            else if (!strcmp(k, "volume")) g_set.volume = CLAMP(n, 0, 100);
            else if (!strcmp(k, "pointeur")) g_set.pointer = CLAMP(n, 1, 5);
            else if (!strcmp(k, "mot_de_passe")) strlcpy(g_set.pass, v, sizeof g_set.pass);
            else if (!strcmp(k, "icones")) {               /* ancien format (bêtas 18 à 22) : « icône:colonne:ligne,... » */
                for (const char *q = v; *q;) {
                    int ic = atoi(q), col = -1, row = -1;
                    const char *c1 = strchr(q, ':');
                    if (c1) { col = atoi(c1 + 1); const char *c2 = strchr(c1 + 1, ':'); if (c2) row = atoi(c2 + 1); }
                    for (int i = 0; i < g_set.ndesk; i++)
                        if (g_set.desk[i].app == ic && col >= 0 && row >= 0 && col < 100 && row < 100) { g_set.desk[i].col = (signed char)col; g_set.desk[i].row = (signed char)row; }
                    const char *nx = strchr(q, ',');
                    if (!nx) break;
                    q = nx + 1;
                }
            }
            else if (!strcmp(k, "bureau")) {               /* « a,app,col,ligne|f,col,ligne,chemin|... » */
                g_set.ndesk = 0;
                for (const char *q = v; *q && g_set.ndesk < DESK_MAX;) {
                    const char *end = strchr(q, '|');
                    char item[200];
                    size_t len = end ? (size_t)(end - q) : strlen(q);
                    if (len >= sizeof item) len = sizeof item - 1;
                    memcpy(item, q, len);
                    item[len] = 0;
                    desk_item_t *d = &g_set.desk[g_set.ndesk];
                    memset(d, 0, sizeof *d);
                    if (item[0] == 'a' && item[1] == ',') {
                        int a = atoi(item + 2), col = -1, row = -1;
                        char *c1 = strchr(item + 2, ',');
                        if (c1) { col = atoi(c1 + 1); char *c2 = strchr(c1 + 1, ','); if (c2) row = atoi(c2 + 1); }
                        if ((a >= 0 && a < APP_COUNT) || (a >= STORE_BASE && a < STORE_BASE + STORE_MAX)) { d->app = (signed char)a; d->col = (signed char)col; d->row = (signed char)row; g_set.ndesk++; }
                    } else if (item[0] == 'f' && item[1] == ',') {
                        int col = atoi(item + 2), row = -1;
                        char *c1 = strchr(item + 2, ','), *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
                        if (c1) row = atoi(c1 + 1);
                        if (c2 && c2[1]) { d->app = -1; d->col = (signed char)col; d->row = (signed char)row; strlcpy(d->path, c2 + 1, sizeof d->path); g_set.ndesk++; }
                    }
                    if (!end) break;
                    q = end + 1;
                }
            }        }
        p = nl + 1;
    }
    free(data);
    if (!g_set.user[0]) strlcpy(g_set.user, "Titan", sizeof g_set.user);
}

/* ---------------- mot de passe de session ----------------
 * Seule une empreinte est enregistrée : PBKDF2-HMAC-SHA256, sel aléatoire de 8 octets,
 * 20 000 tours (« s2$sel$empreinte »). Les anciennes empreintes restent acceptées. */
typedef struct { uint32_t h[8]; uint8_t buf[64]; uint64_t len; } sha256_t;
static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(sha256_t *s, const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha_init(sha256_t *s) {
    static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(s->h, iv, sizeof iv);
    s->len = 0;
}
static void sha_update(sha256_t *s, const uint8_t *p, size_t n) {
    while (n--) {
        s->buf[s->len++ % 64] = *p++;
        if (s->len % 64 == 0) sha_block(s, s->buf);
    }
}
static void sha_final(sha256_t *s, uint8_t out[32]) {
    uint64_t bits = s->len * 8;
    uint8_t one = 0x80, zero = 0;
    sha_update(s, &one, 1);
    while (s->len % 64 != 56) sha_update(s, &zero, 1);
    for (int i = 7; i >= 0; i--) { uint8_t b = (uint8_t)(bits >> (i * 8)); sha_update(s, &b, 1); }
    for (int i = 0; i < 8; i++) { out[i * 4] = (uint8_t)(s->h[i] >> 24); out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16); out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8); out[i * 4 + 3] = (uint8_t)s->h[i]; }
}
static void hmac(const uint8_t *key, size_t kn, const uint8_t *msg, size_t mn, uint8_t out[32]) {
    uint8_t k[64] = { 0 }, pad[64];
    if (kn > 64) { sha256_t t; sha_init(&t); sha_update(&t, key, kn); sha_final(&t, k); } else memcpy(k, key, kn);
    sha256_t s;
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    sha_init(&s); sha_update(&s, pad, 64); sha_update(&s, msg, mn); sha_final(&s, out);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    sha_init(&s); sha_update(&s, pad, 64); sha_update(&s, out, 32); sha_final(&s, out);
}
static void pbkdf2(const char *pw, const uint8_t salt[8], uint8_t out[16]) {
    uint8_t m[12], u[32], t[32];
    memcpy(m, salt, 8);
    m[8] = 0; m[9] = 0; m[10] = 0; m[11] = 1;
    hmac((const uint8_t *)pw, strlen(pw), m, 12, u);
    memcpy(t, u, 32);
    for (int i = 1; i < 20000; i++) {
        hmac((const uint8_t *)pw, strlen(pw), u, 32, u);
        for (int k = 0; k < 32; k++) t[k] ^= u[k];
    }
    memcpy(out, t, 16);
}
static void hex(char *o, const uint8_t *p, int n) { for (int i = 0; i < n; i++) snprintf(o + i * 2, 3, "%02x", p[i]); }

static void pass_hash_salt(const char *pw, const uint8_t salt[8], char out[64]) {
    uint8_t d[16];
    pbkdf2(pw, salt, d);
    char hs[17], hd[33];
    hex(hs, salt, 8);
    hex(hd, d, 16);
    snprintf(out, 64, "s2$%s$%s", hs, hd);
}

void settings_pass_hash(const char *pw, char out[64]) {
    uint8_t salt[8];
    plat_random(salt, sizeof salt);
    pass_hash_salt(pw, salt, out);
}

/* ancienne empreinte (bêta 16) : FNV-1a 64 bits salé, répété */
static void old_hash(const char *pw, char out[20]) {
    uint64_t h = 1469598103934665603ull;
    const char *salt = "TitanOs-session";
    for (int r = 0; r < 20000; r++) {
        for (const char *p = salt; *p; p++) h = (h ^ (uint8_t)*p) * 1099511628211ull;
        for (const char *p = pw; *p; p++) h = (h ^ (uint8_t)*p) * 1099511628211ull;
        h ^= (uint64_t)r;
    }
    snprintf(out, 20, "%08x%08x", (unsigned)(h >> 32), (unsigned)h);
}

bool settings_pass_check(const char *pw) {
    if (!g_set.pass[0]) return true;
    if (str_starts(g_set.pass, "s2$") && strlen(g_set.pass) >= 3 + 16 + 1 + 32) {
        uint8_t salt[8];
        for (int i = 0; i < 8; i++) {
            char b[3] = { g_set.pass[3 + i * 2], g_set.pass[4 + i * 2], 0 };
            salt[i] = (uint8_t)strtol(b, NULL, 16);
        }
        char h[64];
        pass_hash_salt(pw, salt, h);
        return !strcmp(h, g_set.pass);
    }
    char h[20];
    old_hash(pw, h);
    return !strcmp(h, g_set.pass);
}

void settings_save(void) {
    static char buf[8192];
    int n = snprintf(buf, sizeof buf,
                     "# Réglages de TitanOs\nfond=%d\naccent=%d\nutilisateur=%s\nsecondes=%d\nnuit=%d\nclavier=%s\n"
                     "fuseau=%d\nhorloge_locale=%d\nluminosite=%d\nverre=%d\nsons=%d\nvolume=%d\npointeur=%d\nmot_de_passe=%s\naccueil=%d\n",
                     g_set.wallpaper, g_set.accent, g_set.user, g_set.seconds, g_set.night, g_set.layout ? "us" : "fr",
                     g_set.tz, g_set.rtc_local, g_set.brightness, g_set.glass, g_set.sounds, g_set.volume, g_set.pointer, g_set.pass, g_set.welcome);
    n += snprintf(buf + n, sizeof buf - (size_t)n, "bureau=");
    for (int i = 0; i < g_set.ndesk; i++) {
        const desk_item_t *d = &g_set.desk[i];
        if (d->app >= 0) n += snprintf(buf + n, sizeof buf - (size_t)n, "%sa,%d,%d,%d", i ? "|" : "", d->app, d->col, d->row);
        else n += snprintf(buf + n, sizeof buf - (size_t)n, "%sf,%d,%d,%s", i ? "|" : "", d->col, d->row, d->path);
    }
    n += snprintf(buf + n, sizeof buf - (size_t)n, "\n");
    plat_writefile(plat_config_path(), buf, (size_t)n);
}
