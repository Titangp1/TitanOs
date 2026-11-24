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
}

void settings_load(void) {
    defaults();
    char *data;
    size_t len;
    if (plat_readfile(plat_config_path(), &data, &len) != 0) return;
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
            else if (!strcmp(k, "clavier")) g_set.layout = !strcmp(v, "us");
            else if (!strcmp(k, "fuseau")) g_set.tz = CLAMP(n, 0, tz_count - 1);
            else if (!strcmp(k, "horloge_locale")) g_set.rtc_local = n != 0;
            else if (!strcmp(k, "luminosite")) g_set.brightness = CLAMP(n, -1, 100);
            else if (!strcmp(k, "verre")) g_set.glass = CLAMP(n, 0, 2);
            else if (!strcmp(k, "sons")) g_set.sounds = n != 0;
            else if (!strcmp(k, "volume")) g_set.volume = CLAMP(n, 0, 100);
            else if (!strcmp(k, "pointeur")) g_set.pointer = CLAMP(n, 1, 5);
            else if (!strcmp(k, "mot_de_passe")) strlcpy(g_set.pass, v, sizeof g_set.pass);
        }
        p = nl + 1;
    }
    free(data);
    if (!g_set.user[0]) strlcpy(g_set.user, "Titan", sizeof g_set.user);
}

/* empreinte d'un mot de passe : le mot de passe lui-même n'est jamais enregistré
 * (FNV-1a 64 bits salé, répété 20 000 fois) */
void settings_pass_hash(const char *pw, char out[20]) {
    uint64_t h = 1469598103934665603ull;
    const char *salt = "TitanOs-session";
    for (int r = 0; r < 20000; r++) {
        for (const char *p = salt; *p; p++) h = (h ^ (uint8_t)*p) * 1099511628211ull;
        for (const char *p = pw; *p; p++) h = (h ^ (uint8_t)*p) * 1099511628211ull;
        h ^= (uint64_t)r;
    }
    snprintf(out, 20, "%08x%08x", (unsigned)(h >> 32), (unsigned)h);
}

void settings_save(void) {
    char buf[700];
    int n = snprintf(buf, sizeof buf,
                     "# Réglages de TitanOs\nfond=%d\naccent=%d\nutilisateur=%s\nsecondes=%d\nclavier=%s\n"
                     "fuseau=%d\nhorloge_locale=%d\nluminosite=%d\nverre=%d\nsons=%d\nvolume=%d\npointeur=%d\nmot_de_passe=%s\n",
                     g_set.wallpaper, g_set.accent, g_set.user, g_set.seconds, g_set.layout ? "us" : "fr",
                     g_set.tz, g_set.rtc_local, g_set.brightness, g_set.glass, g_set.sounds, g_set.volume, g_set.pointer, g_set.pass);
    plat_writefile(plat_config_path(), buf, (size_t)n);
}
