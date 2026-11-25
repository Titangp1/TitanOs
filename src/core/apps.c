/* TitanOs - liste des applications, À propos, Calculatrice et Snake */
#include "desk.h"

static const char *const names[APP_COUNT] = {
    "Fichiers", "Terminal", "Bloc-notes", "Calculatrice", "Paramètres", "Système", "Réseau", "Snake", "À propos", "Mises à jour", "Chromium",
    "Logithèque",
};
const char *app_name(int icon) {
    if (icon >= STORE_BASE && icon < STORE_BASE + STORE_MAX) return store_name(icon - STORE_BASE);
    return icon >= 0 && icon < APP_COUNT ? names[icon] : "Fenêtre";
}

void app_open(int icon) {
    switch (icon) {
    case ICON_FILES:    app_files_open(NULL); break;
    case ICON_TERMINAL: app_terminal_open(); break;
    case ICON_NOTES:    app_notes_open(NULL); break;
    case ICON_CALC:     app_calc_open(); break;
    case ICON_SETTINGS: app_settings_open(); break;
    case ICON_SYSTEM:   app_system_open(); break;
    case ICON_NETWORK:  app_network_open(); break;
    case ICON_SNAKE:    app_snake_open(); break;
    case ICON_ABOUT:    app_about_open(); break;
    case ICON_UPDATE:   app_update_open(); break;
    case ICON_BROWSER:  app_browser_open(); break;
    case ICON_STORE:    app_store_open(); break;
    default: if (icon >= STORE_BASE && icon < STORE_BASE + STORE_MAX) store_launch(icon - STORE_BASE); break;
    }
}

/* fenêtre unique : si elle existe déjà, on la ramène devant */
static window_t *single(int icon) {
    window_t *w = wm_find(icon);
    if (w) wm_focus(w);
    return w;
}

/* ================= À propos ================= */
static void about_draw(window_t *w, canvas_t *c) {
    (void)w;
    sysinfo_t si;
    plat_sysinfo(&si);
    draw_logo(c, c->w / 2.0f, 26, 96, 255);
    text_center(c, &font_h1, R(0, 132, c->w, 44), "TitanOs", C_TEXT);
    text_center(c, &font_body, R(0, 176, c->w, 20), "Version " TITANOS_VERSION, C_TEXT2);
    rect_t p = R(28, 214, c->w - 56, 128);
    ui_panel(c, p, 16);
    char buf[160];
    snprintf(buf, sizeof buf, "Noyau Linux %s", si.kernel);
    text_draw(c, &font_bold, p.x + 18, p.y + 16, buf, C_TEXT);
    text_draw(c, &font_body, p.x + 18, p.y + 42, "Bureau, démarrage et applications écrits pour TitanOs,", C_TEXT2);
    text_draw(c, &font_body, p.x + 18, p.y + 62, "en C et sans bibliothèque. Linux ne fournit que les pilotes.", C_TEXT2);
    snprintf(buf, sizeof buf, "Utilisateur : %s", g_set.user);
    text_draw(c, &font_body, p.x + 18, p.y + 92, buf, C_MUTED);
    text_center(c, &font_small, R(0, c->h - 58, c->w, 18), "© 2026 Titangp1 · logiciel libre, licence GNU GPL v3", C_TEXT2);
    text_center(c, &font_small, R(0, c->h - 38, c->w, 18), "Noyau Linux et BusyBox : GPL · Police Inter : SIL OFL", C_MUTED);
}

void app_about_open(void) {
    if (single(ICON_ABOUT)) return;
    window_t *w = wm_open("À propos de TitanOs", ICON_ABOUT, 460, 420, WF_SINGLE);
    if (w) w->draw = about_draw;
}

/* ================= Calculatrice ================= */
typedef struct {
    char disp[32];
    double acc;
    int op;              /* 0, '+', '-', '*', '/' */
    bool fresh;          /* le prochain chiffre remplace l'affichage */
    char expr[64];
    int hover;
} calc_t;

static const char *const calc_keys[20] = {
    "C", "±", "%", "÷", "7", "8", "9", "×", "4", "5", "6", "−", "1", "2", "3", "+", "0", "", ",", "=",
};

static rect_t calc_key_rect(canvas_t *c, int i) {
    int cols = 4, gap = 10, top = 130, kw = (c->w - 32 - gap * 3) / cols, kh = (c->h - top - 16 - gap * 4) / 5;
    int col = i % cols, row = i / cols;
    if (i == 16) return R(16, top + row * (kh + gap), kw * 2 + gap, kh);
    return R(16 + col * (kw + gap), top + row * (kh + gap), kw, kh);
}

static void fmt_num(char *buf, size_t n, double v) {
    if (v != v || v > 1e15 || v < -1e15) { strlcpy(buf, "Erreur", n); return; }
    bool neg = v < 0;
    if (neg) v = -v;
    uint64_t ip = (uint64_t)v;
    int digits = 1;
    for (uint64_t t = ip; t >= 10; t /= 10) digits++;
    int dec = 12 - digits;
    if (dec < 0) dec = 0;
    char tmp[48];
    snprintf(tmp, sizeof tmp, "%s%.*f", neg ? "-" : "", dec, v);
    if (strchr(tmp, ',')) {               /* retire les zéros inutiles */
        int l = (int)strlen(tmp);
        while (l > 0 && tmp[l - 1] == '0') tmp[--l] = 0;
        if (l > 0 && tmp[l - 1] == ',') tmp[--l] = 0;
    }
    if (!strcmp(tmp, "-0")) strlcpy(tmp, "0", sizeof tmp);
    strlcpy(buf, tmp, n);
}

static double parse_num(const char *s) {
    bool neg = *s == '-';
    if (neg) s++;
    double v = 0, frac = 0, scale = 1;
    bool dot = false;
    for (; *s; s++) {
        if (*s == ',') { dot = true; continue; }
        if (*s < '0' || *s > '9') break;
        if (dot) { scale /= 10; frac += (*s - '0') * scale; }
        else v = v * 10 + (*s - '0');
    }
    return neg ? -(v + frac) : v + frac;
}

static double calc_apply(double a, double b, int op) {
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b == 0 ? 0.0 / 0.0 : a / b;
    }
    return b;
}

static const char *op_sym(int op) { return op == '+' ? "+" : op == '-' ? "−" : op == '*' ? "×" : op == '/' ? "÷" : ""; }

static void calc_press(calc_t *k, const char *key) {
    if (!strcmp(key, "C")) { strlcpy(k->disp, "0", sizeof k->disp); k->acc = 0; k->op = 0; k->fresh = true; k->expr[0] = 0; return; }
    if (!strcmp(k->disp, "Erreur")) { strlcpy(k->disp, "0", sizeof k->disp); k->fresh = true; }
    if (key[0] >= '0' && key[0] <= '9' && !key[1]) {
        if (k->fresh || !strcmp(k->disp, "0")) { k->disp[0] = key[0]; k->disp[1] = 0; k->fresh = false; }
        else if (strlen(k->disp) < 15) strlcat(k->disp, key, sizeof k->disp);
        return;
    }
    if (!strcmp(key, ",")) {
        if (k->fresh) { strlcpy(k->disp, "0", sizeof k->disp); k->fresh = false; }
        if (!strchr(k->disp, ',')) strlcat(k->disp, ",", sizeof k->disp);
        return;
    }
    if (!strcmp(key, "←")) {
        int l = (int)strlen(k->disp);
        if (k->fresh) return;
        if (l <= 1 || (l == 2 && k->disp[0] == '-')) strlcpy(k->disp, "0", sizeof k->disp);
        else k->disp[l - 1] = 0;
        return;
    }
    double v = parse_num(k->disp);
    if (!strcmp(key, "±")) { fmt_num(k->disp, sizeof k->disp, -v); return; }
    if (!strcmp(key, "%")) { fmt_num(k->disp, sizeof k->disp, k->op ? k->acc * v / 100 : v / 100); return; }
    int op = !strcmp(key, "+") ? '+' : !strcmp(key, "−") ? '-' : !strcmp(key, "×") ? '*' : !strcmp(key, "÷") ? '/' : 0;
    if (op) {
        if (k->op && !k->fresh) v = calc_apply(k->acc, v, k->op);
        else if (k->op && k->fresh) v = k->acc;
        k->acc = v;
        k->op = op;
        k->fresh = true;
        char a[32];
        fmt_num(a, sizeof a, v);
        fmt_num(k->disp, sizeof k->disp, v);
        snprintf(k->expr, sizeof k->expr, "%s %s", a, op_sym(op));
        return;
    }
    if (!strcmp(key, "=") && k->op) {
        char a[32], b[32];
        fmt_num(a, sizeof a, k->acc);
        fmt_num(b, sizeof b, v);
        snprintf(k->expr, sizeof k->expr, "%s %s %s =", a, op_sym(k->op), b);
        fmt_num(k->disp, sizeof k->disp, calc_apply(k->acc, v, k->op));
        k->op = 0;
        k->fresh = true;
    }
}

static void calc_draw(window_t *w, canvas_t *c) {
    calc_t *k = w->data;
    ui_panel(c, R(16, 12, c->w - 32, 106), 18);
    text_draw(c, &font_body, c->w - 32 - text_width(&font_body, k->expr), 26, k->expr, C_MUTED);
    const font_t *f = text_width(&font_h1, k->disp) > c->w - 64 ? &font_h2 : &font_h1;
    text_draw(c, f, c->w - 32 - text_width(f, k->disp), 108 - f->ascent - f->descent - 10, k->disp, C_TEXT);
    for (int i = 0; i < 20; i++) {
        if (i == 17) continue;
        rect_t r = calc_key_rect(c, i);
        bool op = (i % 4 == 3) || i == 19, fn = i < 3, hov = k->hover == i;
        uint32_t bg = op ? (hov ? col_mix(accent(), 0xFFFFFFFF, 50) : accent()) : fn ? (hov ? 0x48FFFFFF : 0x34FFFFFF) : (hov ? 0x30FFFFFF : 0x1EFFFFFF);
        gfx_rrect(c, r.x, r.y, r.w, r.h, MIN(r.h, r.w) / 2.0f, bg);
        gfx_rrect_stroke(c, r.x, r.y, r.w, r.h, MIN(r.h, r.w) / 2.0f, 1, 0x24FFFFFF);
        text_center(c, &font_h2, r, calc_keys[i], C_TEXT);
    }
}

static void calc_event(window_t *w, const event_t *e) {
    calc_t *k = w->data;
    if (e->type == EV_MOTION) {
        int h = -1;
        for (int i = 0; i < 20; i++) if (i != 17 && rect_has(calc_key_rect(&w->surf, i), e->x, e->y)) h = i;
        if (h != k->hover) { k->hover = h; wm_invalidate(w); }
    } else if (e->type == EV_BUTTON && e->down && e->button == BTN_LEFT && k->hover >= 0) {
        sound_play(SFX_CLICK);
        calc_press(k, calc_keys[k->hover]);
        wm_invalidate(w);
    } else if (e->type == EV_KEY && e->down) {
        const char *key = NULL;
        char d[2] = { 0 };
        if (e->key == K_CHAR && !(e->mods & MOD_CTRL)) {
            int ch = e->ch;
            if (ch >= '0' && ch <= '9') { d[0] = (char)ch; key = d; }
            else if (ch == '+') key = "+";
            else if (ch == '-') key = "−";
            else if (ch == '*' || ch == 'x') key = "×";
            else if (ch == '/' || ch == ':') key = "÷";
            else if (ch == ',' || ch == '.') key = ",";
            else if (ch == '%') key = "%";
            else if (ch == '=') key = "=";
        } else if (e->key == K_CHAR && (e->mods & MOD_CTRL) && (e->ch == 'c' || e->ch == 'C')) {
            clip_set(k->disp, (int)strlen(k->disp));
            wm_toast("Résultat copié.");
        }
        else if (e->key == K_ENTER) key = "=";
        else if (e->key == K_ESC || e->key == K_DELETE) key = "C";
        else if (e->key == K_BACKSPACE) key = "←";
        if (key) { calc_press(k, key); wm_invalidate(w); }
    }
}

static void free_data(window_t *w) { free(w->data); }

void app_calc_open(void) {
    if (single(ICON_CALC)) return;
    calc_t *k = calloc(1, sizeof *k);
    if (!k) return;
    strlcpy(k->disp, "0", sizeof k->disp);
    k->fresh = true;
    k->hover = -1;
    window_t *w = wm_open("Calculatrice", ICON_CALC, 340, 500, WF_SINGLE);
    if (!w) { free(k); return; }
    w->data = k;
    w->draw = calc_draw;
    w->event = calc_event;
    w->on_close = free_data;
}

/* ================= Snake ================= */
#define SN_W 26
#define SN_H 18
#define SN_CELL 22
typedef struct {
    int x[SN_W * SN_H], y[SN_W * SN_H], len;
    int dx, dy, ndx, ndy, fx, fy, score;
    bool over, paused;
    uint64_t next;
    uint32_t seed;
} snake_t;
static int snake_record;

static uint32_t rnd(snake_t *s) { s->seed = s->seed * 1103515245u + 12345u; return s->seed >> 8; }

static void snake_food(snake_t *s) {
    for (int tries = 0; tries < 1000; tries++) {
        int fx = (int)(rnd(s) % SN_W), fy = (int)(rnd(s) % SN_H);
        bool ok = true;
        for (int i = 0; i < s->len; i++) if (s->x[i] == fx && s->y[i] == fy) ok = false;
        if (ok) { s->fx = fx; s->fy = fy; return; }
    }
}

static void snake_reset(snake_t *s) {
    s->len = 4;
    for (int i = 0; i < s->len; i++) { s->x[i] = SN_W / 2 - i; s->y[i] = SN_H / 2; }
    s->dx = s->ndx = 1;
    s->dy = s->ndy = 0;
    s->score = 0;
    s->over = false;
    s->seed ^= (uint32_t)plat_ms();
    snake_food(s);
}

static void snake_draw(window_t *w, canvas_t *c) {
    snake_t *s = w->data;
    int ox = (c->w - SN_W * SN_CELL) / 2, oy = 46;
    char buf[64];
    snprintf(buf, sizeof buf, "Score : %d", s->score);
    text_draw(c, &font_bold, ox, 14, buf, C_TEXT);
    snprintf(buf, sizeof buf, "Record : %d", snake_record);
    text_draw(c, &font_body, ox + SN_W * SN_CELL - text_width(&font_body, buf), 14, buf, C_TEXT2);
    gfx_rrect(c, ox - 4, oy - 4, SN_W * SN_CELL + 8, SN_H * SN_CELL + 8, 14, 0x50000000);
    gfx_circle(c, ox + s->fx * SN_CELL + SN_CELL / 2.0f, oy + s->fy * SN_CELL + SN_CELL / 2.0f, SN_CELL / 2.0f - 3, 0xFFEF4444);
    for (int i = s->len - 1; i >= 0; i--) {
        uint32_t col = col_mix(0xFF86EFAC, 0xFF16A34A, i * 256 / MAX(1, s->len));
        gfx_rrect(c, ox + s->x[i] * SN_CELL + 1, oy + s->y[i] * SN_CELL + 1, SN_CELL - 2, SN_CELL - 2, 6, col);
    }
    if (s->over || s->paused) {
        rect_t b = R(c->w / 2 - 170, oy + SN_H * SN_CELL / 2 - 50, 340, 100);
        gfx_rrect(c, b.x, b.y, b.w, b.h, 20, 0xD00F172A);
        text_center(c, &font_h2, R(b.x, b.y + 14, b.w, 36), s->over ? "Perdu !" : "Pause", C_TEXT);
        text_center(c, &font_body, R(b.x, b.y + 56, b.w, 24), "Espace pour continuer", C_TEXT2);
    }
}

static void snake_tick(window_t *w, uint64_t now) {
    snake_t *s = w->data;
    if (s->over || s->paused || w != wm_focused() || now < s->next) return;
    s->next = now + (uint64_t)MAX(60, 130 - s->score * 2);
    s->dx = s->ndx;
    s->dy = s->ndy;
    int nx = s->x[0] + s->dx, ny = s->y[0] + s->dy;
    bool dead = nx < 0 || ny < 0 || nx >= SN_W || ny >= SN_H;
    for (int i = 0; i < s->len - 1 && !dead; i++) if (s->x[i] == nx && s->y[i] == ny) dead = true;
    if (dead) {
        s->over = true;
        sound_play(SFX_ERROR);
        wm_invalidate(w);
        return;
    }
    bool eat = nx == s->fx && ny == s->fy;
    if (eat && s->len < SN_W * SN_H) s->len++;
    for (int i = s->len - 1; i > 0; i--) { s->x[i] = s->x[i - 1]; s->y[i] = s->y[i - 1]; }
    s->x[0] = nx;
    s->y[0] = ny;
    if (eat) {
        s->score++;
        if (s->score > snake_record) snake_record = s->score;
        sound_play(SFX_TICK);
        snake_food(s);
    }
    wm_invalidate(w);
}

static void snake_event(window_t *w, const event_t *e) {
    snake_t *s = w->data;
    if (e->type != EV_KEY || !e->down) return;
    if (e->key == K_CHAR && e->ch == ' ') {
        if (s->over) snake_reset(s);
        else s->paused = !s->paused;
        wm_invalidate(w);
        return;
    }
    if (e->key == K_UP && s->dy != 1) { s->ndx = 0; s->ndy = -1; }
    if (e->key == K_DOWN && s->dy != -1) { s->ndx = 0; s->ndy = 1; }
    if (e->key == K_LEFT && s->dx != 1) { s->ndx = -1; s->ndy = 0; }
    if (e->key == K_RIGHT && s->dx != -1) { s->ndx = 1; s->ndy = 0; }
}

void app_snake_open(void) {
    if (single(ICON_SNAKE)) return;
    snake_t *s = calloc(1, sizeof *s);
    if (!s) return;
    s->seed = 12345;
    snake_reset(s);
    window_t *w = wm_open("Snake", ICON_SNAKE, SN_W * SN_CELL + 48, SN_H * SN_CELL + 70, WF_SINGLE);
    if (!w) { free(s); return; }
    w->data = s;
    w->draw = snake_draw;
    w->event = snake_event;
    w->tick = snake_tick;
    w->on_close = free_data;
}
