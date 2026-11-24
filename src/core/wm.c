/* TitanOs - bureau et gestionnaire de fenêtres
 *
 * Composition : la scène est redessinée seulement dans la zone abîmée (« damage »),
 * de l'arrière vers l'avant : fond, icônes, fenêtres, dock, menus. Chaque élément en
 * verre garde son rendu en cache ; il n'est recalculé que si quelque chose a changé
 * *sous* lui (chaque zone abîmée porte le niveau de l'élément qui l'a causée).
 *
 * Animations : un élément qui apparaît ou disparaît est dessiné dans un calque, puis
 * posé sur la scène avec une transparence et un décalage qui varient dans le temps.
 * Le verre garde son cache (calculé à la position finale), l'animation reste fluide. */
#include "desk.h"

int scr_w, scr_h;
static canvas_t scene, wall, layer, snap;
static canvas_t *T;                       /* où l'on dessine : la scène ou le calque */
static int wall_id = -1;
static uint64_t now;

/* durées (ms) : courtes à l'ouverture, un peu plus longues quand la fenêtre voyage */
#define T_WIN_IN   240
#define T_WIN_OUT  170
#define T_MIN      300
#define T_MAX      230
#define T_EL_IN    190
#define T_EL_OUT   140
#define T_TRANS    480

enum { A_NONE, A_OPEN, A_CLOSE, A_MIN, A_RESTORE, A_MAX };

/* ---------------- état ---------------- */
#define MAX_WIN 24
static window_t wins[MAX_WIN];
static int zord[MAX_WIN], zn;              /* du fond vers l'avant */
static window_t *focused, *capture;
static int mx = 200, my = 200, cur_ox = -100, cur_oy = -100;
static bool cursor_moved;
static int mods;
enum { MODE_SPLASH, MODE_LOCK, MODE_DESK };
static int mode = MODE_SPLASH;

/* ombre douce gardée en image : elle ne dépend que de la taille et du style */
typedef struct { canvas_t c; int w, h, pad; float r, blur, dy; uint32_t col; } shadow_t;
static void draw_shadow(canvas_t *dst, shadow_t *s, rect_t r, float radius, float blur, float dy, uint32_t col) {
    int pad = (int)(blur + (dy > 0 ? dy : -dy)) + 2;
    if (s->w != r.w || s->h != r.h || s->r != radius || s->blur != blur || s->dy != dy || s->col != col || !s->c.px) {
        if (s->c.w != r.w + 2 * pad || s->c.h != r.h + 2 * pad) { canvas_free(&s->c); s->c = canvas_new(r.w + 2 * pad, r.h + 2 * pad); }
        canvas_noclip(&s->c);
        canvas_clear(&s->c, 0);
        gfx_shadow(&s->c, (float)pad, (float)pad, (float)r.w, (float)r.h, radius, blur, dy, col);
        s->w = r.w; s->h = r.h; s->pad = pad; s->r = radius; s->blur = blur; s->dy = dy; s->col = col;
    }
    gfx_blit(dst, r.x - s->pad, r.y - s->pad, &s->c, 255);
}

typedef struct { rect_t r; bool ok, shown, leaving; canvas_t cache; int level; uint64_t t0; shadow_t sh; } glass_el_t;
static field_t lock_pw;                    /* mot de passe tapé à l'écran de verrouillage */
static bool lock_bad;
static glass_el_t g_power, g_dock, g_tray, g_menu, g_cal, g_pop, g_toast, g_osd, g_avatar, g_hint;

/* transitions plein écran : l'ancienne image (snap) s'efface par-dessus la nouvelle */
enum { TR_NONE, TR_FADE, TR_UNLOCK };
static int trans;
static uint64_t trans_t0;

/* déplacement / redimensionnement */
enum { DR_NONE, DR_MOVE, DR_SIZE };
static bool glass_stale;                   /* verres figés pendant un déplacement de fenêtre */
static uint64_t last_drag_move;
static bool glass_frozen(void);
static int drag_kind, drag_dx, drag_dy;
static window_t *drag_win;
static rect_t drag_start;

/* survol */
enum { HV_NONE, HV_CLOSE, HV_MAX, HV_MIN };
static window_t *hover_win;
static int hover_btn;
static int dock_hover = -1, tray_hover = -1, desk_sel = -1;

/* menu TitanOs, calendrier, menu contextuel, notification, indicateur de volume */
static bool menu_open, cal_open, pop_open;
static field_t menu_search;
static int menu_hover = -1, menu_sel;
static int cal_y, cal_m;
static const char *pop_items[16];
static int pop_n, pop_hover = -1, pop_x, pop_y;
static void (*pop_cb)(int, void *);
static void *pop_ctx;
static char toast_text[160];
static uint64_t toast_until, osd_until;
static int osd_kind, osd_value;            /* 0 volume, 1 luminosité */

static inline int abs_i(int v) { return v < 0 ? -v : v; }
static uint64_t last_click_ms;
static int last_click_x, last_click_y;
static datetime_t now_dt;
static char *clipboard;
static char splash_step[96];

/* état matériel affiché dans la barre */
static battery_t bat;
static int wifi_state = WIFI_NONE, wifi_signal;
static char wifi_ssid[33];
static bool net_wired;
static bool bat_warned;

/* ---------------- courbes d'animation ---------------- */
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float ease_out(float p) { p = 1 - clamp01(p); return 1 - p * p * p; }
static float ease_in(float p) { p = clamp01(p); return p * p; }
static float ease_in_out(float p) { p = clamp01(p); return p < 0.5f ? 4 * p * p * p : 1 - (-2 * p + 2) * (-2 * p + 2) * (-2 * p + 2) / 2; }
static float progress(uint64_t t0, int dur) { return now <= t0 ? 0 : clamp01((float)(now - t0) / dur); }

/* ---------------- zones abîmées ---------------- */
typedef struct { rect_t r; int level; } dmg_t;
static dmg_t dmg[32];
static int ndmg;

static void damage(rect_t r, int level) {
    r = rect_isect(r, R(0, 0, scr_w, scr_h));
    if (rect_empty(r)) return;
    if (ndmg == countof(dmg)) {
        int lv = level;
        for (int i = 0; i < ndmg; i++) { r = rect_union(r, dmg[i].r); lv = MIN(lv, dmg[i].level); }
        ndmg = 0;
        level = lv;
    }
    dmg[ndmg].r = r;
    dmg[ndmg].level = level;
    ndmg++;
}
static void damage_all(void) { damage(R(0, 0, scr_w, scr_h), -1); }

/* ---------------- styles de verre ---------------- */
static glass_t style_for(int kind) {
    /* 0 : dock et barres, 1 : fenêtres, 2 : menus et bulles */
    static const float tint_by_level[3][3] = { { 0.08f, 0.30f, 0.30f }, { 0.16f, 0.46f, 0.40f }, { 0.28f, 0.62f, 0.55f } };
    glass_t g = { 0 };
    int lv = CLAMP(g_set.glass, 0, 2);
    g.down = kind == 0 ? 2 : 4;
    g.radius = 3;
    g.passes = 3;
    g.sat = 1.45f;
    g.bright = kind == 0 ? 1.05f : 0.95f;
    g.tint = kind == 0 ? 0x1B2236 : 0x0B1020;
    g.tint_a = tint_by_level[lv][kind];
    if (kind == 1) {          /* grandes surfaces : bords discrets, pas de bande en bas */
        g.refract = 5;
        g.band = 9;
        g.edge_glow = 0;
        g.rim = 0.32f;
        g.sheen = 0.035f;
    } else {                  /* petits éléments : l'effet « liquide » se voit vraiment */
        g.refract = 12;
        g.band = 18;
        g.edge_glow = 0.10f;
        g.rim = kind == 0 ? 0.55f : 0.42f;
        g.sheen = kind == 0 ? 0.10f : 0.06f;
    }
    return g;
}

/* ---------------- géométrie du bureau ---------------- */
static const int pinned[] = { ICON_FILES, ICON_BROWSER, ICON_TERMINAL, ICON_NOTES, ICON_CALC, ICON_SETTINGS, ICON_SYSTEM };
static const int desk_icons[] = { ICON_FILES, ICON_BROWSER, ICON_TERMINAL, ICON_NOTES, ICON_SETTINGS, ICON_SYSTEM, ICON_ABOUT };

/* glisser une icône du bureau */
static int desk_drag = -1;                 /* icône tenue (bouton enfoncé) */
static bool desk_dragging;                 /* elle a vraiment bougé */
static int desk_drag_dx, desk_drag_dy, desk_drag_x0, desk_drag_y0;

static int desk_rows(void);
static int desk_cols(void);
static rect_t ghost_last, target_last;     /* zones à redessiner pendant le glissement */
static void desk_cell(int i, int *col, int *row) {
    int ic = desk_icons[i], rows = desk_rows(), cols = desk_cols();
    if (g_set.desk_col[ic] >= 0 && g_set.desk_col[ic] < cols && g_set.desk_row[ic] < rows) {
        *col = g_set.desk_col[ic];
        *row = g_set.desk_row[ic];
        return;
    }
    /* place par défaut : la n-ième case libre, colonne par colonne */
    int k = 0;
    for (int j = 0; j < i; j++) if (g_set.desk_col[desk_icons[j]] < 0) k++;
    for (int cell = 0; cell < rows * cols; cell++) {
        int c = cell / rows, r = cell % rows;
        bool taken = false;
        for (int j = 0; j < countof(desk_icons); j++) {
            int o = desk_icons[j];
            if (g_set.desk_col[o] == c && g_set.desk_row[o] == r) taken = true;
        }
        if (taken) continue;
        if (k-- == 0) { *col = c; *row = r; return; }
    }
    *col = 0;
    *row = 0;
}
#define DOCK_H   64
#define DOCK_IC  44
#define DOCK_GAP 10

typedef struct { int icon; window_t *win; float grow; } dock_item_t;
static dock_item_t dock_items[24];
static int dock_n;
static float start_grow;

static bool alive(window_t *w) { return w && w->used && !w->closing; }

static void dock_build(void) {
    float old[24] = { 0 };
    int oic[24];
    int on = dock_n;
    for (int i = 0; i < on; i++) { old[i] = dock_items[i].grow; oic[i] = dock_items[i].icon; }
    dock_n = 0;
    for (int i = 0; i < countof(pinned); i++) dock_items[dock_n++] = (dock_item_t){ pinned[i], NULL, 0 };
    for (int k = 0; k < zn; k++) {
        window_t *w = &wins[zord[k]];
        if (!alive(w) || (w->flags & WF_DIALOG)) continue;
        bool found = false;
        for (int i = 0; i < dock_n; i++)
            if (dock_items[i].icon == w->icon) { if (!dock_items[i].win) dock_items[i].win = w; found = true; break; }
        if (!found && dock_n < countof(dock_items)) dock_items[dock_n++] = (dock_item_t){ w->icon, w, 0 };
    }
    for (int i = 0; i < dock_n; i++)
        for (int j = 0; j < on; j++) if (oic[j] == dock_items[i].icon) dock_items[i].grow = old[j];
}

static rect_t dock_rect(void) {
    int w = 12 + (DOCK_IC + DOCK_GAP) * (dock_n + 1) + 14;
    return R((scr_w - w) / 2, scr_h - DOCK_H - 12, w, DOCK_H);
}
static rect_t dock_item_rect(int i) {      /* -1 : bouton TitanOs */
    rect_t d = dock_rect();
    int x = d.x + 12 + (i + 1) * (DOCK_IC + DOCK_GAP) + (i >= 0 ? 14 : 0);
    return R(x, d.y + (DOCK_H - DOCK_IC) / 2, DOCK_IC, DOCK_IC);
}

/* zone de notification : réseau, volume, batterie, clavier, horloge */
enum { TP_NET, TP_VOL, TP_BAT, TP_KBD, TP_CLOCK, TP_COUNT };
static rect_t tparts[TP_COUNT];
/* bouton d'arrêt, en bas à gauche */
static rect_t power_rect(void) { return R(12, scr_h - DOCK_H - 12, DOCK_H, DOCK_H); }

static rect_t tray_rect(void) {
    static const int widths[TP_COUNT] = { 40, 40, 70, 40, 118 };
    int w = 16;
    for (int i = 0; i < TP_COUNT; i++) if (i != TP_BAT || bat.present) w += widths[i] + 4;
    rect_t t = R(scr_w - 12 - w, scr_h - DOCK_H - 12, w, DOCK_H);
    int x = t.x + 10;
    for (int i = 0; i < TP_COUNT; i++) {
        if (i == TP_BAT && !bat.present) { tparts[i] = R(0, 0, 0, 0); continue; }
        tparts[i] = R(x, t.y + (i == TP_CLOCK ? 8 : 12), widths[i], i == TP_CLOCK ? 48 : 40);
        x += widths[i] + 4;
    }
    return t;
}
/* menu : 6 applications par ligne ; sa hauteur suit le nombre de lignes (toutes les
 * applications, même filtrées, pour qu'il ne change pas de taille pendant la recherche) */
#define MENU_COLS 6
static rect_t menu_rect(void) {
    int rows = (APP_COUNT + MENU_COLS - 1) / MENU_COLS;
    int w = 700, h = MIN(136 + rows * 112 + 100, scr_h - DOCK_H - 48);
    return R((scr_w - w) / 2, scr_h - DOCK_H - 24 - h, w, h);
}
static rect_t cal_rect(void) { rect_t t = tray_rect(); return R(scr_w - 12 - 320, t.y - 12 - 356, 320, 356); }
static rect_t osd_rect(void) { return R((scr_w - 300) / 2, scr_h - DOCK_H - 12 - 76, 300, 56); }
/* icônes du bureau : grille de cases de 100 x 104. Une icône déplacée garde sa case ;
 * les autres se rangent en colonnes dans les cases restées libres. */
#define CELL_W 100
#define CELL_H 104
static int desk_rows(void) { return MAX(1, (scr_h - DOCK_H - 60) / CELL_H); }
static int desk_cols(void) { return MAX(1, (scr_w - 40) / CELL_W); }
static void desk_cell(int i, int *col, int *row);
static rect_t cell_rect(int col, int row) { return R(20 + col * CELL_W, 20 + row * CELL_H, 92, 96); }
static rect_t desk_icon_rect(int i) {
    int c, r;
    desk_cell(i, &c, &r);
    return cell_rect(c, r);
}
static rect_t pop_rect(void) {
    int w = 200;
    for (int i = 0; i < pop_n; i++) w = MAX(w, text_width(&font_body, pop_items[i]) + 48);
    int h = 12;
    for (int i = 0; i < pop_n; i++) h += strcmp(pop_items[i], "-") ? 32 : 9;
    return R(pop_x, pop_y, w, h);
}
static rect_t toast_rect(void) {
    int w = MIN(scr_w - 40, text_width(&font_body, toast_text) + 64);
    return R(scr_w - w - 20, 20, w, 48);
}

/* ---------------- éléments animés (menus, bulles) ---------------- */
static void el_show(glass_el_t *e) {
    if (e->shown && !e->leaving) return;
    e->shown = true;
    e->leaving = false;
    e->t0 = now;
}
static void el_hide(glass_el_t *e) {
    if (!e->shown || e->leaving) return;
    e->leaving = true;
    e->t0 = now;
}
static float el_alpha(glass_el_t *e) {
    if (!e->shown) return 0;
    if (e->leaving) {
        float p = progress(e->t0, T_EL_OUT);
        if (p >= 1) { e->shown = false; return 0; }
        return 1 - ease_in(p);
    }
    return ease_out(progress(e->t0, T_EL_IN));
}
static bool el_animating(glass_el_t *e) {
    return e->shown && now - e->t0 < (uint64_t)(e->leaving ? T_EL_OUT : T_EL_IN) + 20;
}

/* ---------------- fenêtres ---------------- */
static int win_level(window_t *w) {
    for (int i = 0; i < zn; i++) if (&wins[zord[i]] == w) return 1 + i;
    return 1;
}
rect_t wm_client(window_t *w) { return R(w->r.x, w->r.y + TITLE_H, w->r.w, w->r.h - TITLE_H); }
static rect_t win_outer(window_t *w) { return R(w->r.x - 40, w->r.y - 30, w->r.w + 80, w->r.h + 120); }   /* avec l'ombre et l'animation */
static rect_t win_btn(window_t *w, int b) {
    int x = w->r.x + w->r.w - 16 - 26;
    if (b == HV_MAX) x -= 34;
    if (b == HV_MIN) x -= (w->flags & WF_RESIZABLE) ? 68 : 34;
    return R(x, w->r.y + 9, 26, 26);
}
static bool has_btn(window_t *w, int b) {
    if (b == HV_CLOSE) return true;
    if (w->flags & WF_DIALOG) return false;
    if (b == HV_MAX) return (w->flags & WF_RESIZABLE) != 0;
    return true;
}

static void surf_alloc(window_t *w) {
    rect_t c = wm_client(w);
    if (w->surf.w != c.w || w->surf.h != c.h) { canvas_free(&w->surf); w->surf = canvas_new(c.w, c.h); }
    if (w->glass.w != w->r.w || w->glass.h != w->r.h) { canvas_free(&w->glass); w->glass = canvas_new(w->r.w, w->r.h); }
    w->glass_ok = false;
    w->dirty = true;
}

/* arrondit les coins du bas du contenu pour épouser la fenêtre */
static void mask_corners(canvas_t *c) {
    float r = WIN_R;
    for (int j = 0; j < (int)r && j < c->h; j++) {
        int y = c->h - 1 - j;
        float dy = r - (j + 0.5f);
        for (int i = 0; i < (int)r; i++) {
            float dx = r - (i + 0.5f);
            float cov = 0.5f - (__builtin_sqrtf(dx * dx + dy * dy) - r);
            if (cov >= 1) continue;
            uint32_t a = cov <= 0 ? 0 : (uint32_t)(cov * 255);
            for (int side = 0; side < 2; side++) {
                int x = side ? c->w - 1 - i : i;
                uint32_t p = c->px[(size_t)y * c->stride + x];
                uint32_t rb = ((p & 0xFF00FF) * a >> 8) & 0xFF00FF, ag = (((p >> 8) & 0xFF00FF) * a) & 0xFF00FF00;
                c->px[(size_t)y * c->stride + x] = rb | ag;
            }
        }
    }
}

window_t *wm_open(const char *title, int icon, int w, int h, unsigned flags) {
    window_t *win = NULL;
    for (int i = 0; i < MAX_WIN; i++) if (!wins[i].used) { win = &wins[i]; break; }
    if (!win) { wm_toast("Trop de fenêtres ouvertes."); return NULL; }
    memset(win, 0, sizeof *win);
    win->used = true;
    win->flags = flags;
    win->icon = icon;
    win->mx = win->my = -1;
    win->minw = 260;
    win->minh = 160;
    strlcpy(win->title, title, sizeof win->title);
    h += TITLE_H;
    w = MIN(w, scr_w - 40);
    h = MIN(h, scr_h - DOCK_H - 40);
    static int cascade;
    int k = (flags & WF_DIALOG) ? 0 : cascade % 6;
    int x = (scr_w - w) / 2 + k * 36 - 60, y = (scr_h - DOCK_H - h) / 2 + k * 30 - 50;
    if (flags & WF_DIALOG) { x = (scr_w - w) / 2; y = (scr_h - DOCK_H - h) / 2 - 20; }
    else cascade++;
    win->r = R(CLAMP(x, 10, scr_w - w - 10), CLAMP(y, 10, MAX(10, scr_h - DOCK_H - 24 - h)), w, h);
    win->anim = A_OPEN;
    win->anim_t0 = now;
    surf_alloc(win);
    zord[zn++] = (int)(win - wins);
    wm_focus(win);
    dock_build();
    damage(rect_grow(dock_rect(), 20), 100);
    sound_play(SFX_OPEN);
    return win;
}

static void focus_next(void) {
    focused = NULL;
    for (int i = zn - 1; i >= 0; i--) {
        window_t *o = &wins[zord[i]];
        if (alive(o) && !o->minimized && o->anim != A_MIN) { focused = o; break; }
    }
    if (focused) damage(win_outer(focused), win_level(focused));
}

/* fermeture demandée par l'utilisateur : la fenêtre peut d'abord demander confirmation */
void wm_damage_client(window_t *w) { if (alive(w)) damage(wm_client(w), win_level(w)); }

/* touches brutes pour une fenêtre qui les veut (Chromium) ; les raccourcis de TitanOs
 * (touche Windows, Alt+Tab, Alt+F4) et les touches Fn restent à TitanOs */
bool core_raw_key_wanted(void) {
    return mode == MODE_DESK && !menu_open && !pop_open && alive(focused) && focused->raw_key && !focused->minimized;
}

bool core_raw_key(int code, bool down, int m) {
    if (mode != MODE_DESK || menu_open || pop_open || !alive(focused) || !focused->raw_key || focused->minimized) return false;
    if (code == 125 || code == 126 || (m & MOD_SUPER)) return false;                     /* touche Windows */
    if ((m & MOD_ALT) && (code == 15 || code == 62)) return false;                       /* Alt+Tab, Alt+F4 */
    if (code == 113 || code == 114 || code == 115 || code == 116 || code == 224 || code == 225 || code == 99) return false;
    now = plat_ms();
    focused->raw_key(focused, code, down);
    return true;
}

void wm_request_close(window_t *w) {
    if (!alive(w)) return;
    if (w->can_close && !w->can_close(w)) return;
    wm_close(w);
}

/* Impr. écran : l'écran entier enregistré en PNG dans Images */
size_t png_encode(const uint32_t *px, int w, int h, int stride, uint8_t **out);
static void screenshot(void) {
    uint8_t *png;
    size_t n = png_encode(scene.px, scr_w, scr_h, scene.stride, &png);
    if (!n) { wm_toast("Capture impossible : mémoire insuffisante."); return; }
    datetime_t d;
    local_now(&d);
    char dir[400], path[512];
    snprintf(dir, sizeof dir, "%s/Images", plat_home());
    plat_mkdir(dir);
    snprintf(path, sizeof path, "%s/Capture %04d-%02d-%02d %02dh%02d %02d.png", dir, d.year, d.month, d.day, d.hour, d.min, d.sec);
    int r = plat_writefile(path, png, n);
    free(png);
    sound_play(r == 0 ? SFX_NOTIFY : SFX_ERROR);
    wm_toast(r == 0 ? "Capture d'écran enregistrée dans Images." : "Impossible d'enregistrer la capture.");
}

void wm_close(window_t *w) {
    if (!alive(w)) return;
    for (int i = 0; i < MAX_WIN; i++)          /* ses dialogues partent avec elle */
        if (alive(&wins[i]) && wins[i].owner == w) wm_close(&wins[i]);
    if (w->on_close) w->on_close(w);
    w->on_close = NULL;
    w->draw = NULL;
    w->event = NULL;
    w->tick = NULL;
    w->data = NULL;
    w->closing = true;
    w->anim = A_CLOSE;
    w->anim_t0 = now;
    w->dirty = false;
    if (capture == w) capture = NULL;
    if (drag_win == w) drag_win = NULL, drag_kind = DR_NONE;
    if (hover_win == w) hover_win = NULL;
    rect_t old_dock = dock_rect();
    if (focused == w) focus_next();
    dock_build();
    damage(rect_grow(rect_union(old_dock, dock_rect()), 20), 0);
    damage(win_outer(w), 0);
    sound_play(SFX_CLOSE);
}

static void finish_close(window_t *w) {
    damage(win_outer(w), 0);
    canvas_free(&w->surf);
    canvas_free(&w->glass);
    w->used = false;
    int k = 0;
    for (int i = 0; i < zn; i++) if (&wins[zord[i]] != w) zord[k++] = zord[i];
    zn = k;
}

void wm_invalidate(window_t *w) { if (alive(w)) w->dirty = true; }

void wm_set_title(window_t *w, const char *t) {
    strlcpy(w->title, t, sizeof w->title);
    damage(R(w->r.x, w->r.y, w->r.w, TITLE_H), win_level(w));
}

window_t *wm_focused(void) { return focused; }

void wm_focus(window_t *w) {
    if (!alive(w)) return;
    if (w->minimized || w->anim == A_MIN) { w->minimized = false; w->anim = A_RESTORE; w->anim_t0 = now; }
    int idx = (int)(w - wins), k = 0;
    if (zn && zord[zn - 1] == idx && focused == w) return;
    for (int i = 0; i < zn; i++) if (zord[i] != idx) zord[k++] = zord[i];
    zord[k++] = idx;
    zn = k;
    window_t *old = focused;
    focused = w;
    w->glass_ok = false;
    damage(win_outer(w), 0);
    if (alive(old) && old != w) damage(R(old->r.x, old->r.y, old->r.w, TITLE_H), win_level(old));
    damage(rect_grow(dock_rect(), 20), 100);
}

window_t *wm_find(int icon) {
    for (int i = 0; i < MAX_WIN; i++)
        if (alive(&wins[i]) && wins[i].icon == icon && !(wins[i].flags & WF_DIALOG)) return &wins[i];
    return NULL;
}

static void minimize(window_t *w) {
    w->anim = A_MIN;
    w->anim_t0 = now;
    if (focused == w) focus_next();
    damage(win_outer(w), 0);
    damage(rect_grow(dock_rect(), 20), 100);
    sound_play(SFX_CLOSE);
}

static void set_rect(window_t *w, rect_t r) {
    damage(win_outer(w), win_level(w));
    bool resized = r.w != w->r.w || r.h != w->r.h;
    w->r = r;
    if (resized) surf_alloc(w);
    w->glass_ok = false;
    damage(win_outer(w), win_level(w));
}

static void toggle_max(window_t *w);
void wm_maximize(window_t *w) { if (alive(w) && !w->maximized) toggle_max(w); }

void wm_resize(window_t *w, int cw, int ch) {
    if (!alive(w) || w->maximized) return;
    rect_t r = w->r;
    int nh = MIN(ch + TITLE_H, scr_h - DOCK_H - 30);
    r.y = CLAMP(r.y - (nh - r.h) / 2, 8, MAX(8, scr_h - DOCK_H - 24 - nh));
    r.w = MIN(cw, scr_w - 20);
    r.h = nh;
    if (r.w != w->r.w || r.h != w->r.h) set_rect(w, r);
}

static void toggle_max(window_t *w) {
    if (!(w->flags & WF_RESIZABLE)) return;
    w->anim_from = w->r;
    if (w->maximized) { w->maximized = false; set_rect(w, w->restore); }
    else { w->restore = w->r; w->maximized = true; set_rect(w, R(8, 8, scr_w - 16, scr_h - DOCK_H - 30)); }
    w->anim = A_MAX;
    w->anim_t0 = now;
}

/* ---------------- divers ---------------- */
void clip_set(const char *s, int n) {
    free(clipboard);
    clipboard = malloc((size_t)n + 1);
    if (clipboard) { memcpy(clipboard, s, (size_t)n); clipboard[n] = 0; }
}
const char *clip_get(void) { return clipboard ? clipboard : ""; }

void wm_toast(const char *msg) {
    if (g_toast.shown) damage(rect_grow(toast_rect(), 40), 103);
    strlcpy(toast_text, msg, sizeof toast_text);
    toast_until = now + 3500;
    g_toast.ok = false;
    if (!g_toast.shown || g_toast.leaving) el_show(&g_toast);
    damage(rect_grow(toast_rect(), 40), 103);
    sound_play(SFX_NOTIFY);
}

void wm_menu(int x, int y, const char *const *items, int n, void (*cb)(int, void *), void *ctx) {
    if (pop_open) damage(rect_grow(pop_rect(), 40), 102);
    pop_n = MIN(n, countof(pop_items));
    for (int i = 0; i < pop_n; i++) pop_items[i] = items[i];
    pop_cb = cb;
    pop_ctx = ctx;
    pop_x = x;
    pop_y = y;
    rect_t r = pop_rect();
    if (r.x + r.w > scr_w - 6) pop_x = scr_w - 6 - r.w;
    if (r.y + r.h > scr_h - 6) pop_y = MAX(6, y - r.h);
    pop_open = true;
    pop_hover = -1;
    g_pop.ok = false;
    g_pop.shown = false;
    el_show(&g_pop);
    damage(rect_grow(pop_rect(), 40), 102);
}

static void close_pop(void) {
    if (!pop_open) return;
    pop_open = false;
    el_hide(&g_pop);
}

static void close_menu(void) {
    if (!menu_open) return;
    menu_open = false;
    el_hide(&g_menu);
    damage(rect_grow(dock_rect(), 20), 100);
}

static void open_menu(void) {
    close_pop();
    if (cal_open) { cal_open = false; el_hide(&g_cal); }
    menu_open = true;
    menu_hover = -1;
    menu_sel = 0;
    ui_field_set(&menu_search, "");
    menu_search.focus = true;
    g_menu.ok = false;
    el_show(&g_menu);
    damage(rect_grow(dock_rect(), 20), 100);
    sound_play(SFX_CLICK);
}

static void toggle_cal(void) {
    close_menu();
    cal_open = !cal_open;
    cal_y = now_dt.year;
    cal_m = now_dt.month;
    if (cal_open) { g_cal.ok = false; el_show(&g_cal); }
    else el_hide(&g_cal);
    damage(tray_rect(), 100);
    sound_play(SFX_CLICK);
}

static void snapshot(void) {
    if (snap.w != scr_w || snap.h != scr_h) { canvas_free(&snap); snap = canvas_new(scr_w, scr_h); }
    memcpy(snap.px, scene.px, (size_t)scr_w * scr_h * 4);
}

/* mise en veille : la session se verrouille d'abord, l'écran de verrouillage attend au réveil */
void wm_suspend(void) {
    settings_save();
    wm_lock();
    trans = TR_NONE;
    damage_all();
    core_frame();
    plat_suspend();
    now = plat_ms();
    damage_all();
}

void wm_lock(void) {
    if (mode != MODE_DESK) return;
    close_menu();
    close_pop();
    if (cal_open) { cal_open = false; el_hide(&g_cal); }
    capture = NULL;
    drag_kind = DR_NONE;
    snapshot();                              /* le bureau s'efface sous l'écran de verrouillage */
    ui_field_set(&lock_pw, "");
    lock_bad = false;
    mode = MODE_LOCK;
    trans = TR_FADE;
    trans_t0 = now;
    damage_all();
    sound_play(SFX_LOCK);
}

static void draw_lock(void);
static rect_t lock_field_rect(void) { return R(scr_w / 2 - 170, scr_h / 2 + 110 + 112, 340, 44); }
static void unlock(void) {
    /* l'écran de verrouillage glisse vers le haut en s'effaçant */
    if (snap.w != scr_w || snap.h != scr_h) { canvas_free(&snap); snap = canvas_new(scr_w, scr_h); }
    canvas_t *old = T;
    T = &snap;
    canvas_noclip(&snap);
    draw_lock();
    T = old;
    mode = MODE_DESK;
    trans = TR_UNLOCK;
    trans_t0 = now;
    damage_all();
    sound_play(SFX_UNLOCK);
}

bool wm_kbd_layout_toggle(void) {
    g_set.layout = !g_set.layout;
    settings_save();
    damage(tray_rect(), 100);
    wm_toast(g_set.layout ? "Clavier : QWERTY (US)" : "Clavier : AZERTY (FR)");
    return g_set.layout;
}

void wm_theme_changed(void) {
    for (int i = 0; i < MAX_WIN; i++) if (alive(&wins[i])) wins[i].dirty = true;
    damage_all();
}

static void power_cb(bool ok, void *ctx) {
    if (!ok) return;
    int action = (int)(intptr_t)ctx;
    settings_save();
    core_splash(action ? "Redémarrage…" : "Arrêt en cours…", -1);
    for (int i = 0; i < 3; i++) core_frame();
    plat_sync();
    plat_power(action);
}

void wm_power(int action) {
    dlg_confirm(action ? "Redémarrer" : "Éteindre",
                action ? "Redémarrer l'ordinateur maintenant ?" : "Éteindre l'ordinateur maintenant ?",
                action ? "Redémarrer" : "Éteindre", power_cb, (void *)(intptr_t)action);
}

static void show_osd(int kind, int value) {
    osd_kind = kind;
    osd_value = value;
    osd_until = now + 1500;
    if (!g_osd.shown || g_osd.leaving) { g_osd.ok = false; el_show(&g_osd); }
    damage(rect_grow(osd_rect(), 40), 104);
}

static void change_volume(int delta) {
    if (plat_volume(-1) < 0) return;
    if (delta) {
        g_set.volume = CLAMP(g_set.volume + delta, 0, 100);
        plat_volume(g_set.volume);
        if (plat_muted(-1)) plat_muted(0);
    }
    settings_save();
    show_osd(0, plat_muted(-1) ? 0 : g_set.volume);
    damage(tray_rect(), 100);
    sound_play(SFX_TICK);
}

/* ---------------- dessin : verre ---------------- */
static void draw_glass_ex(glass_el_t *e, rect_t r, float radius, int kind, float shadow, uint32_t shadow_col) {
    if (e->cache.w != r.w || e->cache.h != r.h) {
        canvas_free(&e->cache);
        e->cache = canvas_new(r.w, r.h);
        e->ok = false;
    }
    if (e->r.x != r.x || e->r.y != r.y || e->r.w != r.w || e->r.h != r.h) e->ok = false;
    e->r = r;
    if (!e->ok) {
        glass_t g = style_for(kind);
        rect_t old = scene.clip;
        canvas_noclip(&scene);
        glass_render(&e->cache, &scene, r, radius, &g);
        scene.clip = old;
        e->ok = true;
    }
    if (shadow > 0) draw_shadow(T, &e->sh, r, radius, shadow, shadow / 4, shadow_col);
    gfx_blit(T, r.x, r.y, &e->cache, 255);
}
static void draw_glass(glass_el_t *e, rect_t r, float radius, int kind) { draw_glass_ex(e, r, radius, kind, 0, 0); }

/* dessine fn() dans le calque puis le pose avec transparence et décalage vertical */
static void with_layer(rect_t area, float alpha, int dy, void (*fn)(void)) {
    if (alpha <= 0.003f) return;
    area = rect_isect(area, R(0, 0, scr_w, scr_h));
    if (alpha >= 0.997f && dy == 0) { fn(); return; }
    if (layer.w != scr_w || layer.h != scr_h) { canvas_free(&layer); layer = canvas_new(scr_w, scr_h); }
    canvas_clip(&layer, area);
    canvas_clear(&layer, 0);
    canvas_t *old = T;
    T = &layer;
    fn();
    T = old;
    gfx_blit_rect(&scene, area.x, area.y + dy, &layer, area, (int)(alpha * 255));
}

/* ---------------- dessin : bureau ---------------- */
static void draw_wallpaper(void) {
    if (wall_id != g_set.wallpaper || wall.w != scr_w || wall.h != scr_h) {
        if (wall.w != scr_w || wall.h != scr_h) { canvas_free(&wall); wall = canvas_new(scr_w, scr_h); }
        wallpaper_render(&wall, g_set.wallpaper);
        wall_id = g_set.wallpaper;
    }
    gfx_copy(T, T->clip.x, T->clip.y, &wall, T->clip);
}

static void desk_target(int *col, int *row) {
    *col = CLAMP((mx - desk_drag_dx + 46 - 20) / CELL_W, 0, desk_cols() - 1);
    *row = CLAMP((my - desk_drag_dy + 48 - 20) / CELL_H, 0, desk_rows() - 1);
}

static void draw_desk_icon(rect_t r, int i, int alpha) {
    uint32_t a = (uint32_t)alpha << 24;
    draw_icon(T, desk_icons[i], r.x + (r.w - 56) / 2, r.y + 8, 56);
    const char *n = app_name(desk_icons[i]);
    int x = r.x + (r.w - text_width(&font_body, n)) / 2;
    text_draw(T, &font_body, x, r.y + 71, n, (uint32_t)(alpha * 0x99 / 255) << 24);
    text_draw(T, &font_body, x, r.y + 70, n, a | (C_TEXT & 0xFFFFFF));
}

static void draw_desktop(void) {
    draw_wallpaper();
    if (desk_dragging) {
        int c, r;
        desk_target(&c, &r);
        rect_t t = cell_rect(c, r);
        gfx_rrect(T, t.x, t.y, t.w, t.h, 14, 0x22FFFFFF);
        gfx_rrect_stroke(T, t.x, t.y, t.w, t.h, 14, 1.5f, (accent() & 0xFFFFFF) | 0xC0000000);
    }
    for (int i = 0; i < countof(desk_icons); i++) {
        rect_t r = desk_icon_rect(i);
        if (desk_dragging && i == desk_drag) continue;
        if (!rect_hits(r, T->clip)) continue;
        if (i == desk_sel) {
            gfx_rrect(T, r.x, r.y, r.w, r.h, 14, 0x30FFFFFF);
            gfx_rrect_stroke(T, r.x, r.y, r.w, r.h, 14, 1, 0x40FFFFFF);
        }
        draw_desk_icon(r, i, 255);
    }
    if (desk_dragging) {
        rect_t r = R(mx - desk_drag_dx, my - desk_drag_dy, 92, 96);
        if (rect_hits(r, T->clip)) {
            draw_desk_icon(r, desk_drag, 255);
        }
    }
}

static void draw_win_button(window_t *w, int b, bool active) {
    rect_t r = win_btn(w, b);
    bool hov = hover_win == w && hover_btn == b;
    float cx = r.x + 13, cy = r.y + 13;
    uint32_t bg = hov ? (b == HV_CLOSE ? 0xFFE11D48 : 0x40FFFFFF) : (active ? 0x22FFFFFF : 0x14FFFFFF);
    gfx_circle(T, cx, cy, 13, bg);
    uint32_t ic = hov ? 0xFFFFFFFF : active ? 0xFFE2E8F0 : 0xFF94A3B8;
    if (b == HV_CLOSE) {
        gfx_line(T, cx - 4.5f, cy - 4.5f, cx + 4.5f, cy + 4.5f, 1.8f, ic);
        gfx_line(T, cx - 4.5f, cy + 4.5f, cx + 4.5f, cy - 4.5f, 1.8f, ic);
    } else if (b == HV_MAX) {
        gfx_rrect_stroke(T, cx - 5, cy - 5, 10, 10, 2.5f, 1.6f, ic);
    } else {
        gfx_line(T, cx - 5, cy, cx + 5, cy, 1.8f, ic);
    }
}

/* icône du dock d'une fenêtre : là où elle part quand on la réduit */
static rect_t dock_icon_of(window_t *w) {
    for (int i = 0; i < dock_n; i++) if (dock_items[i].win == w) return dock_item_rect(i);
    for (int i = 0; i < dock_n; i++) if (dock_items[i].icon == w->icon) return dock_item_rect(i);
    rect_t d = dock_rect();
    return R(d.x + d.w / 2 - 24, d.y, 48, 48);
}

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* où et comment poser l'image d'une fenêtre animée : zoom à l'ouverture et à la fermeture,
 * voyage vers l'icône du dock (réduction) et retour, passage d'une taille à l'autre */
static void anim_geom(window_t *w, rect_t S, float *x, float *y, float *ww, float *hh, float *a) {
    float cx = w->r.x + w->r.w / 2.0f, cy = w->r.y + w->r.h / 2.0f, s = 1;
    *a = 1;
    if (w->anim == A_OPEN) {
        float p = ease_out(progress(w->anim_t0, T_WIN_IN));
        s = 0.92f + 0.08f * p;
        *a = p;
    } else if (w->anim == A_CLOSE) {
        float p = ease_in(progress(w->anim_t0, T_WIN_OUT));
        s = 1 - 0.06f * p;
        *a = 1 - p;
    } else if (w->anim == A_MIN || w->anim == A_RESTORE) {
        float p = progress(w->anim_t0, T_MIN);
        if (w->anim == A_RESTORE) p = 1 - p;
        p = ease_in_out(p);
        rect_t ic = dock_icon_of(w);
        float tx = ic.x + ic.w / 2.0f, ty = ic.y + ic.h / 2.0f;
        float ts = (float)ic.w / (float)MAX(w->r.w, w->r.h);
        s = lerpf(1, ts, p);
        cx = lerpf(cx, tx, p);
        cy = lerpf(cy, ty, p);
        *a = 1 - 0.85f * p * p;
    } else if (w->anim == A_MAX) {
        float p = ease_out(progress(w->anim_t0, T_MAX));
        rect_t F = w->anim_from;
        /* le cadre passe de l'ancien rectangle au nouveau ; l'image (nouvelle taille) suit */
        float fx = lerpf((float)F.x, (float)w->r.x, p), fy = lerpf((float)F.y, (float)w->r.y, p);
        float fw = lerpf((float)F.w, (float)w->r.w, p), fh = lerpf((float)F.h, (float)w->r.h, p);
        float sx = fw / w->r.w, sy = fh / w->r.h;
        *x = fx + (S.x - w->r.x) * sx;
        *y = fy + (S.y - w->r.y) * sy;
        *ww = S.w * sx;
        *hh = S.h * sy;
        return;
    }
    *x = cx + (S.x - (w->r.x + w->r.w / 2.0f)) * s;
    *y = cy + (S.y - (w->r.y + w->r.h / 2.0f)) * s;
    *ww = S.w * s;
    *hh = S.h * s;
}

/* zone de l'écran touchée par l'animation d'une fenêtre (à redessiner à chaque image) */
static rect_t anim_area(window_t *w) {
    rect_t o = win_outer(w);
    if (w->anim == A_MIN || w->anim == A_RESTORE) return rect_union(o, rect_grow(dock_rect(), 30));
    if (w->anim == A_MAX) return rect_union(o, rect_grow(w->anim_from, 60));
    return o;
}

static window_t *cur_win;
static shadow_t win_shadow[MAX_WIN];
static void draw_window_body(void) {
    window_t *w = cur_win;
    bool active = w == focused;
    draw_shadow(T, &win_shadow[w - wins], w->r, WIN_R, active ? 34 : 24, 10, active ? 0x70000000 : 0x50000000);
    gfx_blit(T, w->r.x, w->r.y, &w->glass, 255);
    gfx_fill(T, R(w->r.x + 1, w->r.y + TITLE_H - 1, w->r.w - 2, 1), 0x18FFFFFF);
    draw_icon(T, w->icon, w->r.x + 14, w->r.y + 11, 22);
    int bx = has_btn(w, HV_MIN) ? win_btn(w, HV_MIN).x : has_btn(w, HV_MAX) ? win_btn(w, HV_MAX).x : win_btn(w, HV_CLOSE).x;
    text_fit(T, &font_bold, w->r.x + 46, w->r.y + 13, bx - w->r.x - 58, w->title, active ? C_TEXT : C_MUTED);
    for (int b = HV_CLOSE; b <= HV_MIN; b++) if (has_btn(w, b)) draw_win_button(w, b, active);
    rect_t c = wm_client(w);
    gfx_blit(T, c.x, c.y, &w->surf, 255);
    if (!active) gfx_rrect(T, w->r.x, w->r.y, w->r.w, w->r.h, WIN_R, 0x18000000);
}

static void draw_window(window_t *w) {
    if (!w->glass_ok) {
        glass_t g = style_for(1);
        rect_t old = scene.clip;
        canvas_noclip(&scene);
        glass_render(&w->glass, &scene, w->r, WIN_R, &g);
        scene.clip = old;
        w->glass_ok = true;
    }
    cur_win = w;
    if (!w->anim) { with_layer(win_outer(w), 1, 0, draw_window_body); return; }
    /* animation : la fenêtre est dessinée une fois dans un calque à part, puis seulement
     * posée, agrandie ou réduite, avec sa transparence, à chaque image */
    static canvas_t wl;
    static window_t *wl_win;
    static uint64_t wl_t0;
    static rect_t wl_r;
    rect_t area = rect_isect(win_outer(w), R(0, 0, scr_w, scr_h));
    if (wl.w != scr_w || wl.h != scr_h) { canvas_free(&wl); wl = canvas_new(scr_w, scr_h); wl_win = NULL; }
    if (wl_win != w || wl_t0 != w->anim_t0 || wl_r.x != w->r.x || wl_r.y != w->r.y || wl_r.w != w->r.w || wl_r.h != w->r.h) {
        canvas_clip(&wl, area);
        canvas_clear(&wl, 0);
        canvas_t *old = T;
        T = &wl;
        draw_window_body();
        T = old;
        wl_win = w; wl_t0 = w->anim_t0; wl_r = w->r;
    }
    float x, y, ww, hh, a;
    anim_geom(w, area, &x, &y, &ww, &hh, &a);
    gfx_blit_scaled(&scene, x, y, ww, hh, &wl, area, (int)(a * 255));
}

static void draw_dock_body(void) {
    rect_t d = dock_rect();
    draw_glass_ex(&g_dock, d, 24, 0, 24, 0x50000000);
    rect_t s = dock_item_rect(-1);
    if (menu_open || dock_hover == -2)
        gfx_rrect(T, s.x - 2, s.y - 2, s.w + 4, s.h + 4, 14, menu_open ? (accent() & 0xFFFFFF) | 0x90000000 : 0x28FFFFFF);
    float sg = start_grow * 6;
    draw_logo(T, s.x + s.w / 2.0f, s.y + 8 - sg, 28 + sg, 255);
    int sep = dock_item_rect(0).x - 12;
    gfx_fill(T, R(sep, d.y + 16, 1, d.h - 32), 0x30FFFFFF);
    for (int i = 0; i < dock_n; i++) {
        rect_t r = dock_item_rect(i);
        float g = dock_items[i].grow * 10;          /* l'icône grandit vers le haut sous la souris */
        draw_icon(T, dock_items[i].icon, r.x - g / 2, r.y - g, r.w + g);
        if (alive(dock_items[i].win) && dock_items[i].win->anim != A_MIN && !dock_items[i].win->minimized) {
            bool act = dock_items[i].win == focused;
            gfx_circle(T, r.x + r.w / 2.0f, d.y + d.h - 5, act ? 2.6f : 2, act ? 0xFFFFFFFF : 0xB0FFFFFF);
        } else if (alive(dock_items[i].win)) {
            gfx_circle(T, r.x + r.w / 2.0f, d.y + d.h - 5, 2, 0x70FFFFFF);
        }
    }
}

static rect_t tooltip_area(void) {
    if (dock_hover == -1 || menu_open) return R(0, 0, 0, 0);
    rect_t r = dock_hover == -2 ? dock_item_rect(-1) : dock_item_rect(dock_hover);
    return R(r.x - 120, r.y - 54, r.w + 240, 40);
}
static void draw_tooltip(void) {
    if (dock_hover == -1 || menu_open) return;
    const char *t = dock_hover == -2 ? "TitanOs" : app_name(dock_items[dock_hover].icon);
    rect_t r = dock_hover == -2 ? dock_item_rect(-1) : dock_item_rect(dock_hover);
    int w = text_width(&font_body, t) + 22;
    rect_t b = R(r.x + r.w / 2 - w / 2, r.y - 50, w, 28);
    if (!rect_hits(b, T->clip)) return;
    gfx_rrect(T, b.x, b.y, b.w, b.h, 9, 0xD80F172A);
    gfx_rrect_stroke(T, b.x, b.y, b.w, b.h, 9, 1, 0x30FFFFFF);
    text_center(T, &font_body, b, t, C_TEXT);
}

void wm_suspend(void);
static void power_menu_cb(int i, void *ctx) {
    (void)ctx;
    if (i == 0) wm_suspend();
    else wm_power(i == 1 ? 1 : 0);
}

static void draw_power(void) {
    rect_t r = power_rect();
    if (!rect_hits(rect_grow(r, 30), T->clip)) return;
    draw_glass_ex(&g_power, r, r.w / 2.0f, 0, 24, 0x50000000);
    bool hov = rect_has(r, mx, my);
    if (hov) gfx_circle(T, r.x + r.w / 2.0f, r.y + r.h / 2.0f, r.w / 2.0f - 6, 0x30FFFFFF);
    draw_icon(T, ICON_POWER, r.x + (r.w - 28) / 2, r.y + (r.h - 28) / 2, 28);
}

static void draw_tray(void) {
    rect_t t = tray_rect();
    if (!rect_hits(rect_grow(t, 20), T->clip)) return;
    draw_glass_ex(&g_tray, t, 24, 0, 24, 0x50000000);
    for (int i = 0; i < TP_COUNT; i++)
        if (!rect_empty(tparts[i]) && (tray_hover == i || (i == TP_CLOCK && cal_open)))
            gfx_rrect(T, tparts[i].x, tparts[i].y, tparts[i].w, tparts[i].h, 14, 0x22FFFFFF);
    /* réseau : Wi-Fi (barres selon le signal) ou câble */
    rect_t p = tparts[TP_NET];
    float cx = p.x + p.w / 2.0f, cy = p.y + p.h / 2.0f;
    if (wifi_state == WIFI_CONNECTED && !net_wired) {
        int lv = wifi_signal > 75 ? 4 : wifi_signal > 50 ? 3 : wifi_signal > 25 ? 2 : 1;
        draw_wifi(T, cx, cy, 22, lv, 0xFFFFFFFF, 0x50FFFFFF);
    } else if (net_wired) {
        draw_icon(T, ICON_ETHERNET, cx - 10, cy - 10, 20);
    } else if (wifi_state != WIFI_NONE) {
        draw_wifi(T, cx, cy, 22, wifi_state == WIFI_CONNECTING ? 1 : 0, 0xFFFBBF24, 0x50FFFFFF);
    } else {
        draw_wifi(T, cx, cy, 22, 0, 0xFFFFFFFF, 0x50FFFFFF);
    }
    /* volume */
    p = tparts[TP_VOL];
    int vol = plat_volume(-1);
    bool muted = vol < 0 || plat_muted(-1) || vol == 0;
    draw_speaker(T, p.x + p.w / 2.0f, p.y + p.h / 2.0f, 22, vol > 66 ? 3 : vol > 33 ? 2 : 1, muted, vol < 0 ? 0x60FFFFFF : 0xFFFFFFFF);
    /* batterie */
    if (bat.present) {
        p = tparts[TP_BAT];
        draw_battery(T, p.x + 4, p.y + 13, 26, 14, bat.percent, bat.charging, 0xFFFFFFFF);
        char b[8];
        snprintf(b, sizeof b, "%d%%", bat.percent);
        text_draw(T, &font_small, p.x + 34, p.y + 12, b, C_TEXT);
    }
    text_center(T, &font_bold, tparts[TP_KBD], g_set.layout ? "US" : "FR", C_TEXT);
    p = tparts[TP_CLOCK];
    char hm[16], dmy[40], dn[4];
    if (g_set.seconds) snprintf(hm, sizeof hm, "%02d:%02d:%02d", now_dt.hour, now_dt.min, now_dt.sec);
    else snprintf(hm, sizeof hm, "%02d:%02d", now_dt.hour, now_dt.min);
    strlcpy(dn, day_names[now_dt.wday], 4);
    snprintf(dmy, sizeof dmy, "%s. %d %s", dn, now_dt.day, month_names[now_dt.month - 1]);
    if (text_width(&font_small, dmy) > p.w - 8) snprintf(dmy, sizeof dmy, "%02d/%02d/%04d", now_dt.day, now_dt.month, now_dt.year);
    text_draw(T, &font_title, p.x + (p.w - text_width(&font_title, hm)) / 2, p.y + 4, hm, C_TEXT);
    text_draw(T, &font_small, p.x + (p.w - text_width(&font_small, dmy)) / 2, p.y + 27, dmy, C_TEXT2);
}

/* menu TitanOs : recherche et grille d'applications */
static int menu_apps[APP_COUNT], menu_napps;
static void menu_filter(void) {
    menu_napps = 0;
    for (int i = 0; i < APP_COUNT; i++)
        if (str_icontains(app_name(i), menu_search.s)) menu_apps[menu_napps++] = i;
    if (menu_sel >= menu_napps) menu_sel = MAX(0, menu_napps - 1);
}
static rect_t menu_app_rect(int k) {
    rect_t m = menu_rect();
    int cols = MENU_COLS, cw = (m.w - 48) / cols;
    return R(m.x + 24 + (k % cols) * cw, m.y + 136 + (k / cols) * 112, cw, 104);
}
static rect_t menu_power_rect(int i) {   /* 0 verrouiller, 1 redémarrer, 2 éteindre */
    rect_t m = menu_rect();
    return R(m.x + m.w - 24 - (3 - i) * 48 + 4, m.y + m.h - 64, 40, 40);
}

static void user_initial(char *out) {
    const char *u = g_set.user;
    int cp = utf8_decode(&u);
    if (cp >= 'a' && cp <= 'z') cp -= 32;
    out[utf8_encode(out, cp ? cp : 'T')] = 0;
}

static void draw_menu(void) {
    rect_t m = menu_rect();
    draw_glass_ex(&g_menu, m, 28, 2, 40, 0x70000000);
    ui_field(T, R(m.x + 24, m.y + 24, m.w - 48, 44), &menu_search, "Rechercher une application");
    text_draw(T, &font_bold, m.x + 28, m.y + 96, menu_search.s[0] ? "Résultats" : "Applications", C_TEXT2);
    menu_filter();
    for (int k = 0; k < menu_napps; k++) {
        rect_t r = menu_app_rect(k);
        bool sel = k == menu_hover || (menu_search.s[0] && k == menu_sel);
        if (sel) gfx_rrect(T, r.x + 4, r.y, r.w - 8, r.h, 16, 0x26FFFFFF);
        draw_icon(T, menu_apps[k], r.x + (r.w - 54) / 2, r.y + 12, 54);
        const char *n = app_name(menu_apps[k]);
        text_draw(T, &font_body, r.x + (r.w - text_width(&font_body, n)) / 2, r.y + 74, n, C_TEXT);
    }
    if (!menu_napps) text_draw(T, &font_body, m.x + 28, m.y + 140, "Aucune application ne correspond.", C_MUTED);
    gfx_fill(T, R(m.x + 20, m.y + m.h - 84, m.w - 40, 1), C_LINE);
    gfx_circle(T, m.x + 44, m.y + m.h - 44, 20, accent());
    char ini[8];
    user_initial(ini);
    text_center(T, &font_title, R(m.x + 24, m.y + m.h - 64, 40, 40), ini, C_TEXT);
    text_draw(T, &font_bold, m.x + 76, m.y + m.h - 61, g_set.user, C_TEXT);
    text_draw(T, &font_small, m.x + 76, m.y + m.h - 40, "TitanOs " TITANOS_VERSION " · Linux", C_MUTED);
    static const int pic[3] = { ICON_LOCK, ICON_REBOOT, ICON_POWER };
    for (int i = 0; i < 3; i++) {
        rect_t r = menu_power_rect(i);
        if (menu_hover == 100 + i) gfx_circle(T, r.x + 20, r.y + 20, 20, 0x30FFFFFF);
        draw_icon(T, pic[i], r.x + 8, r.y + 8, 24);
    }
}

static int days_in_month(int y, int m) {
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) ? 29 : d[m - 1];
}
static int first_wday(int y, int m) {   /* 0 = lundi */
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int yy = m < 3 ? y - 1 : y;
    return ((yy + yy / 4 - yy / 100 + yy / 400 + t[m - 1] + 1) % 7 + 6) % 7;
}
static rect_t cal_arrow(int k) { rect_t c = cal_rect(); return R(k ? c.x + c.w - 48 : c.x + 16, c.y + 78, 32, 30); }

static void draw_calendar(void) {
    rect_t c = cal_rect();
    draw_glass_ex(&g_cal, c, 24, 2, 34, 0x70000000);
    char buf[64];
    snprintf(buf, sizeof buf, "%02d:%02d", now_dt.hour, now_dt.min);
    text_draw(T, &font_h1, c.x + 18, c.y + 12, buf, C_TEXT);
    snprintf(buf, sizeof buf, "%s %d %s %d", day_names[now_dt.wday], now_dt.day, month_names[now_dt.month - 1], now_dt.year);
    text_draw(T, &font_body, c.x + 20, c.y + 54, buf, C_TEXT2);
    for (int k = 0; k < 2; k++) {
        rect_t a = cal_arrow(k);
        gfx_rrect(T, a.x, a.y, a.w, a.h, 10, C_PANEL);
        text_center(T, &font_bold, a, k ? "→" : "←", C_TEXT);
    }
    snprintf(buf, sizeof buf, "%s %d", month_names[cal_m - 1], cal_y);
    text_center(T, &font_bold, R(c.x, c.y + 78, c.w, 30), buf, C_TEXT);
    static const char *heads[7] = { "lu", "ma", "me", "je", "ve", "sa", "di" };
    int gx = c.x + 16, gy = c.y + 120, cw = (c.w - 32) / 7, ch = 34;
    for (int i = 0; i < 7; i++) text_center(T, &font_small, R(gx + i * cw, gy, cw, 20), heads[i], C_MUTED);
    int first = first_wday(cal_y, cal_m), nd = days_in_month(cal_y, cal_m);
    for (int d = 1; d <= nd; d++) {
        int cell = first + d - 1, col = cell % 7, row = cell / 7;
        rect_t r = R(gx + col * cw, gy + 24 + row * ch, cw, ch);
        bool today = d == now_dt.day && cal_m == now_dt.month && cal_y == now_dt.year;
        if (today) gfx_circle(T, r.x + r.w / 2.0f, r.y + r.h / 2.0f, 15, accent());
        snprintf(buf, sizeof buf, "%d", d);
        text_center(T, today ? &font_bold : &font_body, r, buf, today ? C_TEXT : col >= 5 ? C_MUTED : C_TEXT2);
    }
}

static void draw_pop(void) {
    rect_t r = pop_rect();
    draw_glass_ex(&g_pop, r, 14, 2, 26, 0x70000000);
    int y = r.y + 6;
    for (int i = 0; i < pop_n; i++) {
        if (!strcmp(pop_items[i], "-")) { gfx_fill(T, R(r.x + 12, y + 4, r.w - 24, 1), C_LINE); y += 9; continue; }
        if (i == pop_hover && pop_open) gfx_rrect(T, r.x + 6, y, r.w - 12, 32, 9, (accent() & 0xFFFFFF) | 0xC0000000);
        text_draw(T, &font_body, r.x + 18, y + 7, pop_items[i], C_TEXT);
        y += 32;
    }
}
static int pop_item_at(int x, int y) {
    rect_t r = pop_rect();
    if (!rect_has(r, x, y)) return -1;
    int yy = r.y + 6;
    for (int i = 0; i < pop_n; i++) {
        int h = strcmp(pop_items[i], "-") ? 32 : 9;
        if (y >= yy && y < yy + h) return h == 32 ? i : -1;
        yy += h;
    }
    return -1;
}

static void draw_toast(void) {
    rect_t r = toast_rect();
    draw_glass_ex(&g_toast, r, 24, 2, 26, 0x60000000);
    gfx_circle(T, r.x + 26, r.y + 24, 6, accent());
    text_draw(T, &font_body, r.x + 44, r.y + 15, toast_text, C_TEXT);
}

static void draw_osd(void) {
    rect_t r = osd_rect();
    draw_glass_ex(&g_osd, r, 28, 2, 26, 0x60000000);
    if (osd_kind == 0) draw_speaker(T, r.x + 32, r.y + 28, 24, osd_value > 66 ? 3 : osd_value > 33 ? 2 : 1, osd_value == 0, C_TEXT);
    else draw_icon(T, ICON_BRIGHTNESS, r.x + 18, r.y + 15, 26);
    rect_t b = R(r.x + 62, r.y + 25, r.w - 120, 6);
    gfx_rrect(T, b.x, b.y, b.w, b.h, 3, 0x40FFFFFF);
    if (osd_value > 0) gfx_rrect(T, b.x, b.y, MAX(6, b.w * osd_value / 100), b.h, 3, 0xFFFFFFFF);
    char t[8];
    snprintf(t, sizeof t, "%d", osd_value);
    text_draw(T, &font_bold, r.x + r.w - 46, r.y + 19, t, C_TEXT);
}

/* ---------------- écrans plein écran ---------------- */
static void draw_lock(void) {
    draw_wallpaper();
    gfx_fill_grad(T, R(0, 0, scr_w, scr_h), 0x30000000, 0x80000000);
    char buf[64];
    snprintf(buf, sizeof buf, "%02d:%02d", now_dt.hour, now_dt.min);
    int tw = text_width(&font_clock, buf), y = scr_h / 2 - 230;
    text_draw(T, &font_clock, (scr_w - tw) / 2, y + 3, buf, 0x40000000);
    text_draw(T, &font_clock, (scr_w - tw) / 2, y, buf, 0xF2FFFFFF);
    snprintf(buf, sizeof buf, "%s %d %s", day_names[now_dt.wday], now_dt.day, month_names[now_dt.month - 1]);
    text_draw(T, &font_h2, (scr_w - text_width(&font_h2, buf)) / 2, y - 26, buf, 0xE6FFFFFF);
    int ay = scr_h / 2 + 110;
    draw_glass_ex(&g_avatar, R(scr_w / 2 - 46, ay - 46, 92, 92), 46, 0, 30, 0x60000000);
    char ini[8];
    user_initial(ini);
    text_center(T, &font_h1, R(scr_w / 2 - 46, ay - 46, 92, 92), ini, C_TEXT);
    text_draw(T, &font_h2, (scr_w - text_width(&font_h2, g_set.user)) / 2, ay + 60, g_set.user, C_TEXT);
    char dg[200];
    plat_diag(dg, sizeof dg);
    if (dg[0]) text_draw(T, &font_small, (scr_w - text_width(&font_small, dg)) / 2, scr_h - 34, dg, 0xB3FFFFFF);
    if (g_set.pass[0]) {                    /* mot de passe demandé */
        rect_t f = lock_field_rect();
        draw_glass(&g_hint, f, 22, 0);
        lock_pw.secret = true;
        lock_pw.focus = true;
        if (lock_pw.s[0]) ui_field(T, f, &lock_pw, NULL);
        else {
            text_center(T, &font_body, f, "Mot de passe", C_MUTED);
            gfx_fill(T, R(f.x + 20, f.y + 12, 2, 20), accent());
        }
        const char *m = lock_bad ? "Mot de passe incorrect" : "Entrée pour ouvrir la session";
        text_center(T, &font_small, R(0, f.y + f.h + 10, scr_w, 18), m, lock_bad ? 0xFFFCA5A5 : 0xB3FFFFFF);
    } else {
        const char *hint = "Cliquez ou appuyez sur une touche pour ouvrir la session";
        int hw = text_width(&font_body, hint) + 48;
        draw_glass(&g_hint, R((scr_w - hw) / 2, ay + 112, hw, 44), 22, 0);
        text_center(T, &font_body, R((scr_w - hw) / 2, ay + 112, hw, 44), hint, C_TEXT);
    }
    if (bat.present) {
        char b[24];
        snprintf(b, sizeof b, "%d %%", bat.percent);
        draw_battery(T, scr_w - 110, 26, 30, 15, bat.percent, bat.charging, 0xE6FFFFFF);
        text_draw(T, &font_body, scr_w - 72, 24, b, 0xE6FFFFFF);
    }
}

/* Écran de démarrage. La disposition est la même que celle dessinée par les chargeurs
 * (boot/stage2.S, boot/efiboot.c à partir de TITANOS/BOOT.IMG) : le passage est invisible. */
#define SPL_LOGO_TOP   (-150)
#define SPL_LOGO_SIZE  110
#define SPL_WORD_TOP   (-22)
#define SPL_ANIM_TOP   82              /* zone des crêtes : 128 x 32, centrée */
#define SPL_ANIM_W     128
#define SPL_ANIM_H     32
#define SPL_TEXT_Y     150

static rect_t spinner_rect(void) { return R(scr_w / 2 - SPL_ANIM_W / 2, scr_h / 2 + SPL_ANIM_TOP, SPL_ANIM_W, SPL_ANIM_H); }
static rect_t splash_text_rect(void) { return R(0, scr_h / 2 + SPL_TEXT_Y, scr_w, 24); }

/* les « crêtes » de TitanOs : cinq petits sommets, comme celui du logo, qui grandissent
 * et s'éclairent l'un après l'autre, en vague (même calcul que tools/bootimg.py) */
static void draw_peaks(int x0, int y0, float t) {
    static const float hgt[5] = { 12, 17, 22, 17, 12 };
    const float period = 36 / 18.2f, base = (float)y0 + 30;
    for (int i = 0; i < 5; i++) {
        float p = t / period - i * 0.12f;
        p -= floorf(p);
        float w = 0;
        if (p < 0.5f) { w = fsin(3.14159265f * p / 0.5f); w *= w; }
        uint32_t a = (uint32_t)((0.28f + 0.72f * w) * 255 + 0.5f) << 24;
        float cx = x0 + SPL_ANIM_W / 2.0f + (i - 2) * 24, half = hgt[i] * 0.6f;
        gfx_tri(T, cx, base - hgt[i] - 6 * w, cx - half, base, cx + half, base, a | 0xE0F2FE, a | 0xE0F2FE);
    }
}

static void draw_splash(void) {
    rect_t clip = T->clip;
    gfx_fill_grad(T, R(0, 0, scr_w, scr_h), OPAQUE(0x070B18), OPAQUE(0x161237));
    int cy = scr_h / 2;
    if (rect_hits(clip, R(0, cy + SPL_LOGO_TOP, scr_w, 160))) {
        draw_logo(T, scr_w / 2.0f, cy + SPL_LOGO_TOP, SPL_LOGO_SIZE, 255);
        text_draw(T, &font_h1, (scr_w - text_width(&font_h1, "TitanOs")) / 2, cy + SPL_WORD_TOP, "TitanOs", C_TEXT);
    }
    draw_peaks(scr_w / 2 - SPL_ANIM_W / 2, cy + SPL_ANIM_TOP, (float)now / 1000.0f);
    text_draw(T, &font_small, (scr_w - text_width(&font_small, splash_step)) / 2, cy + SPL_TEXT_Y, splash_step, 0xFF64748B);
}

/* ---------------- composition ---------------- */
static void present_rect(rect_t P);

static void collect_glass(glass_el_t **list, int *n) {
    *n = 0;
    if (mode != MODE_DESK) return;
    g_dock.level = 100; g_tray.level = 100; g_power.level = 100;
    g_power.r = power_rect();
    g_menu.level = 101; g_cal.level = 101;
    g_pop.level = 102; g_toast.level = 103; g_osd.level = 104;
    g_dock.r = dock_rect();
    g_tray.r = tray_rect();
    list[(*n)++] = &g_dock;
    list[(*n)++] = &g_tray;
    list[(*n)++] = &g_power;
    glass_el_t *opt[] = { &g_menu, &g_cal, &g_pop, &g_toast, &g_osd };
    rect_t rr[] = { menu_rect(), cal_rect(), pop_rect(), toast_rect(), osd_rect() };
    for (int i = 0; i < countof(opt); i++)
        if (opt[i]->shown) { opt[i]->r = rr[i]; list[(*n)++] = opt[i]; }
}

static bool held;                          /* rien n'est dessiné : l'image du chargeur reste */
void core_hold(bool h) { if (held && !h) damage_all(); held = h; }

static void compose(void) {
    if (!ndmg || held) return;
    rect_t D = R(0, 0, 0, 0);
    const int M = 40;
    if (mode == MODE_DESK) {
        /* 1. ce qui a changé sous un élément en verre l'oblige à se recalculer */
        glass_el_t *gl[8];
        int ng;
        collect_glass(gl, &ng);
        bool frozen = glass_frozen();
        for (int k = 0; k < ndmg && !frozen; k++) {
            for (int i = 0; i < zn; i++) {
                window_t *w = &wins[zord[i]];
                if (!w->minimized && dmg[k].level < 1 + i && rect_hits(dmg[k].r, rect_grow(w->r, M))) w->glass_ok = false;
            }
            for (int i = 0; i < ng; i++)
                if (dmg[k].level < gl[i]->level && rect_hits(dmg[k].r, rect_grow(gl[i]->r, M))) gl[i]->ok = false;
        }
        for (int k = 0; k < ndmg; k++) D = rect_union(D, dmg[k].r);
        /* 2. un verre à recalculer a besoin de toute la zone située dessous */
        for (int i = 0; i < zn; i++) {
            window_t *w = &wins[zord[i]];
            if (!w->minimized && !w->glass_ok) D = rect_union(D, rect_grow(w->r, M));
        }
        for (int i = 0; i < ng; i++) if (!gl[i]->ok) D = rect_union(D, rect_grow(gl[i]->r, M));
    } else {
        for (int k = 0; k < ndmg; k++) D = rect_union(D, dmg[k].r);
    }
    ndmg = 0;
    D = rect_isect(D, R(0, 0, scr_w, scr_h));
    if (rect_empty(D)) return;
    T = &scene;
    canvas_clip(&scene, D);
    if (mode == MODE_SPLASH) draw_splash();
    else if (mode == MODE_LOCK) draw_lock();
    else {
        draw_desktop();
        for (int i = 0; i < zn; i++) {
            window_t *w = &wins[zord[i]];
            if (!w->minimized && rect_hits(win_outer(w), D)) draw_window(w);
        }
        with_layer(rect_grow(dock_rect(), 40), 1, 0, draw_dock_body);
        draw_tray();
        draw_power();
        float a;
        if ((a = el_alpha(&g_cal)) > 0) with_layer(rect_grow(cal_rect(), 44), a, (int)((1 - a) * 14), draw_calendar);
        if ((a = el_alpha(&g_menu)) > 0) with_layer(rect_grow(menu_rect(), 50), a, (int)((1 - a) * 22), draw_menu);
        if ((a = el_alpha(&g_pop)) > 0) with_layer(rect_grow(pop_rect(), 36), a, (int)((1 - a) * 8), draw_pop);
        if ((a = el_alpha(&g_toast)) > 0) {
            int dx = (int)((1 - a) * 60);
            rect_t tr = rect_grow(toast_rect(), 36);
            if (dx) {      /* la notification glisse depuis la droite */
                if (layer.w != scr_w || layer.h != scr_h) { canvas_free(&layer); layer = canvas_new(scr_w, scr_h); }
                canvas_clip(&layer, tr);
                canvas_clear(&layer, 0);
                T = &layer;
                draw_toast();
                T = &scene;
                gfx_blit_rect(&scene, tr.x + dx, tr.y, &layer, tr, (int)(a * 255));
            } else with_layer(tr, a, 0, draw_toast);
        }
        if ((a = el_alpha(&g_osd)) > 0) with_layer(rect_grow(osd_rect(), 36), a, (int)((1 - a) * 10), draw_osd);
        draw_tooltip();
    }
    /* transition : l'image précédente s'efface par-dessus */
    if (trans) {
        float p = progress(trans_t0, T_TRANS);
        if (p >= 1) trans = TR_NONE;
        else {
            float a = 1 - ease_out(p);
            int dy = trans == TR_UNLOCK ? -(int)(ease_out(p) * scr_h * 0.08f) : 0;
            gfx_blit_rect(&scene, 0, dy, &snap, R(0, 0, scr_w, scr_h), (int)(a * 255));
        }
    }
    canvas_noclip(&scene);
    present_rect(D);
}

/* ---------------- pointeur ---------------- */
static canvas_t cursor_img;
static uint32_t cursor_save[24 * 24];

static bool in_poly(const float *p, int n, float x, float y) {
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float xi = p[2 * i], yi = p[2 * i + 1], xj = p[2 * j], yj = p[2 * j + 1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

static void cursor_make(void) {
    static const float outer[] = { 1, 1, 1, 19.5f, 5.6f, 15.4f, 8.7f, 22.2f, 11.9f, 20.8f, 8.9f, 14.3f, 15, 14.3f };
    static const float inner[] = { 2.4f, 4.2f, 2.4f, 16.4f, 6.1f, 13.1f, 9.4f, 20.3f, 10.2f, 19.9f, 7.1f, 12.8f, 11.8f, 12.8f };
    cursor_img = canvas_new(24, 24);
    for (int j = 0; j < 24; j++)
        for (int i = 0; i < 24; i++) {
            int no = 0, ni = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float x = i + (sx + 0.5f) / 4, y = j + (sy + 0.5f) / 4;
                    no += in_poly(outer, 7, x, y);
                    ni += in_poly(inner, 7, x, y);
                }
            uint32_t a = (uint32_t)no * 255 / 16, v = (uint32_t)ni * 255 / 16;
            cursor_img.px[j * 24 + i] = a << 24 | v << 16 | v << 8 | v;
        }
}

static void present_rect(rect_t P) {
    rect_t cr = R(mx - 1, my - 1, 24, 24), old = R(cur_ox - 1, cur_oy - 1, 24, 24);
    if (cursor_moved) P = rect_union(rect_union(P, old), cr);
    P = rect_isect(P, R(0, 0, scr_w, scr_h));
    cursor_moved = false;
    cur_ox = mx;
    cur_oy = my;
    if (rect_empty(P)) return;
    bool show = mode != MODE_SPLASH;
    rect_t ci = rect_isect(cr, R(0, 0, scr_w, scr_h));
    if (show) {
        for (int j = 0; j < ci.h; j++)
            memcpy(cursor_save + j * 24, scene.px + (size_t)(ci.y + j) * scene.stride + ci.x, (size_t)ci.w * 4);
        gfx_blit(&scene, cr.x, cr.y, &cursor_img, 255);
    }
    plat_present(scene.px, scene.stride, P);
    if (show)
        for (int j = 0; j < ci.h; j++)
            memcpy(scene.px + (size_t)(ci.y + j) * scene.stride + ci.x, cursor_save + j * 24, (size_t)ci.w * 4);
}

/* ---------------- entrées ---------------- */
static void dispatch_client(window_t *w, const event_t *e) {
    if (!alive(w) || !w->event) return;
    rect_t c = wm_client(w);
    event_t ev = *e;
    ev.x = e->x - c.x;
    ev.y = e->y - c.y;
    w->event(w, &ev);
}

static void set_hover_win(window_t *w, int x, int y) {
    for (int i = 0; i < MAX_WIN; i++) {
        window_t *o = &wins[i];
        if (!alive(o)) continue;
        rect_t c = wm_client(o);
        bool inside = o == w && rect_has(c, x, y);
        int nx = inside ? x - c.x : -1, ny = inside ? y - c.y : -1;
        if (nx != o->mx || ny != o->my) {
            bool was_in = o->mx >= 0;
            o->mx = nx;
            o->my = ny;
            if (inside || was_in) {
                event_t ev = { 0 };
                ev.type = EV_MOTION;
                ev.x = x;
                ev.y = y;
                ev.mods = mods;
                if (!inside) { ev.x = c.x - 1000; ev.y = c.y - 1000; }
                dispatch_client(o, &ev);
            }
        }
    }
}

static window_t *window_at(int x, int y) {
    for (int i = zn - 1; i >= 0; i--) {
        window_t *w = &wins[zord[i]];
        if (alive(w) && !w->minimized && w->anim != A_MIN && rect_has(rect_grow(w->r, 4), x, y)) return w;
    }
    return NULL;
}

static bool near_resize(window_t *w, int x, int y) {
    return (w->flags & WF_RESIZABLE) && !w->maximized && x >= w->r.x + w->r.w - 18 && y >= w->r.y + w->r.h - 18 &&
           x < w->r.x + w->r.w + 4 && y < w->r.y + w->r.h + 4;
}

static void update_hover(void) {
    static bool power_hov;
    bool ph = mode == MODE_DESK && rect_has(power_rect(), mx, my);
    if (ph != power_hov) { power_hov = ph; damage(rect_grow(power_rect(), 30), 100); }
    window_t *hw = NULL;
    int hb = HV_NONE;
    window_t *top = (mode == MODE_DESK && !menu_open && !pop_open) ? window_at(mx, my) : NULL;
    if (top)
        for (int b = HV_CLOSE; b <= HV_MIN; b++)
            if (has_btn(top, b) && rect_has(win_btn(top, b), mx, my)) { hw = top; hb = b; }
    if (hw != hover_win || hb != hover_btn) {
        if (alive(hover_win)) damage(R(hover_win->r.x, hover_win->r.y, hover_win->r.w, TITLE_H), win_level(hover_win));
        hover_win = hw;
        hover_btn = hb;
        if (hw) damage(R(hw->r.x, hw->r.y, hw->r.w, TITLE_H), win_level(hw));
    }
    int dh = -1, th = -1;
    if (mode == MODE_DESK && !pop_open) {
        if (rect_has(rect_grow(dock_rect(), 2), mx, my)) {
            if (rect_has(rect_grow(dock_item_rect(-1), 5), mx, my)) dh = -2;
            for (int i = 0; i < dock_n; i++) if (rect_has(rect_grow(dock_item_rect(i), 5), mx, my)) dh = i;
        }
        tray_rect();
        for (int i = 0; i < TP_COUNT; i++) if (!rect_empty(tparts[i]) && rect_has(tparts[i], mx, my)) th = i;
    }
    if (dh != dock_hover) {
        damage(rect_union(rect_grow(dock_rect(), 20), tooltip_area()), 100);
        dock_hover = dh;
        damage(rect_union(rect_grow(dock_rect(), 20), tooltip_area()), 100);
    }
    if (th != tray_hover) { tray_hover = th; damage(tray_rect(), 100); }
    if (menu_open) {
        int mh = -1;
        for (int k = 0; k < menu_napps; k++) if (rect_has(menu_app_rect(k), mx, my)) mh = k;
        for (int i = 0; i < 3; i++) if (rect_has(menu_power_rect(i), mx, my)) mh = 100 + i;
        if (mh != menu_hover) { menu_hover = mh; damage(menu_rect(), 101); }
    }
    if (pop_open) {
        int ph = pop_item_at(mx, my);
        if (ph != pop_hover) { pop_hover = ph; damage(pop_rect(), 102); }
    }
    set_hover_win(capture ? capture : (mode == MODE_DESK && !menu_open && !pop_open) ? top : NULL, mx, my);
}

static void desk_menu_cb(int i, void *ctx) {
    (void)ctx;
    if (i == 0) app_terminal_open();
    else if (i == 1) app_files_open(NULL);
    else if (i == 3) { g_set.wallpaper = (g_set.wallpaper + 1) % WALLPAPER_COUNT; settings_save(); wm_theme_changed(); }
    else if (i == 4) app_settings_open();
}

static void click_desktop(int x, int y, int button, bool dbl) {
    int hit = -1;
    for (int i = 0; i < countof(desk_icons); i++) if (rect_has(desk_icon_rect(i), x, y)) hit = i;
    if (hit != desk_sel) {
        if (desk_sel >= 0) damage(desk_icon_rect(desk_sel), 0);
        desk_sel = hit;
        if (hit >= 0) damage(desk_icon_rect(hit), 0);
    }
    if (button == BTN_LEFT && dbl && hit >= 0) { app_open(desk_icons[hit]); return; }
    if (button == BTN_LEFT && hit >= 0) {
        rect_t r = desk_icon_rect(hit);
        desk_drag = hit;
        desk_dragging = false;
        desk_drag_dx = x - r.x;
        desk_drag_dy = y - r.y;
        desk_drag_x0 = x;
        desk_drag_y0 = y;
    }
    if (button == BTN_RIGHT) {
        static const char *items[] = { "Ouvrir le Terminal", "Ouvrir les Fichiers", "-", "Fond d'écran suivant", "Paramètres…" };
        wm_menu(x, y, items, countof(items), desk_menu_cb, NULL);
    }
}

static void dock_menu_cb(int i, void *ctx) {
    dock_item_t *it = ctx;
    if (i == 0) app_open(it->icon);
    else if (i == 1 && alive(it->win)) wm_request_close(it->win);
}

static void battery_toast(void) {
    char b[96];
    if (bat.charging) snprintf(b, sizeof b, "Batterie : %d %%, en charge", bat.percent);
    else if (bat.ac) snprintf(b, sizeof b, "Batterie : %d %%, sur secteur", bat.percent);
    else if (bat.minutes > 0) snprintf(b, sizeof b, "Batterie : %d %%, environ %d h %02d restantes", bat.percent, bat.minutes / 60, bat.minutes % 60);
    else snprintf(b, sizeof b, "Batterie : %d %%", bat.percent);
    wm_toast(b);
}

static void mouse_button(const event_t *e) {
    bool dbl = false;
    if (e->down && e->button == BTN_LEFT) {
        dbl = now - last_click_ms < 420 && abs_i(mx - last_click_x) < 5 && abs_i(my - last_click_y) < 5;
        last_click_ms = dbl ? 0 : now;
        last_click_x = mx;
        last_click_y = my;
    }
    if (mode == MODE_LOCK) {
        if (e->down && !trans && !g_set.pass[0]) unlock();
        return;
    }
    if (mode != MODE_DESK) return;

    if (!e->down) {
        if (desk_drag >= 0) {
            if (desk_dragging) {
                int c, r, oc, orow;
                desk_target(&c, &r);
                desk_cell(desk_drag, &oc, &orow);
                for (int j = 0; j < countof(desk_icons); j++) {     /* la case est prise : échange */
                    int jc, jr;
                    desk_cell(j, &jc, &jr);
                    if (j != desk_drag && jc == c && jr == r) {
                        g_set.desk_col[desk_icons[j]] = (signed char)oc;
                        g_set.desk_row[desk_icons[j]] = (signed char)orow;
                    }
                }
                /* les icônes sans case choisie gardent leur place actuelle */
                for (int j = 0; j < countof(desk_icons); j++) {
                    if (j == desk_drag || g_set.desk_col[desk_icons[j]] >= 0) continue;
                    int jc, jr;
                    desk_cell(j, &jc, &jr);
                    g_set.desk_col[desk_icons[j]] = (signed char)jc;
                    g_set.desk_row[desk_icons[j]] = (signed char)jr;
                }
                g_set.desk_col[desk_icons[desk_drag]] = (signed char)c;
                g_set.desk_row[desk_icons[desk_drag]] = (signed char)r;
                settings_save();
                sound_play(SFX_TICK);
                damage(R(0, 0, scr_w, scr_h - DOCK_H - 20), 0);
            }
            desk_drag = -1;
            desk_dragging = false;
        }
        if (drag_kind) { drag_kind = DR_NONE; drag_win = NULL; }
        if (capture) { window_t *c = capture; capture = NULL; dispatch_client(c, e); }
        return;
    }
    if (pop_open) {
        int i = pop_item_at(mx, my);
        void (*cb)(int, void *) = pop_cb;
        void *ctx = pop_ctx;
        close_pop();
        if (i >= 0 && cb) { sound_play(SFX_CLICK); cb(i, ctx); }
        return;
    }
    if (menu_open) {
        if (rect_has(menu_rect(), mx, my)) {
            if (menu_hover >= 0 && menu_hover < 100) { int a = menu_apps[menu_hover]; close_menu(); app_open(a); }
            else if (menu_hover == 100) { close_menu(); wm_lock(); }
            else if (menu_hover >= 101) { close_menu(); sound_play(SFX_CLICK); wm_power(menu_hover == 101); }
            return;
        }
        close_menu();
        if (rect_has(dock_item_rect(-1), mx, my)) return;
    }
    if (cal_open && rect_has(cal_rect(), mx, my)) {
        for (int k = 0; k < 2; k++)
            if (rect_has(cal_arrow(k), mx, my)) {
                cal_m += k ? 1 : -1;
                if (cal_m < 1) { cal_m = 12; cal_y--; }
                if (cal_m > 12) { cal_m = 1; cal_y++; }
                damage(cal_rect(), 101);
                sound_play(SFX_CLICK);
            }
        return;
    }
    if (rect_has(dock_rect(), mx, my)) {
        if (cal_open) toggle_cal();
        if (dock_hover == -2) { open_menu(); return; }
        if (dock_hover >= 0) {
            dock_item_t *it = &dock_items[dock_hover];
            if (e->button == BTN_RIGHT) {
                static const char *items[] = { "Nouvelle fenêtre", "Fermer" };
                wm_menu(mx, dock_rect().y - 80, items, alive(it->win) ? 2 : 1, dock_menu_cb, it);
            } else if (alive(it->win)) {
                if (it->win == focused && !it->win->minimized && it->win->anim != A_MIN) minimize(it->win);
                else { wm_focus(it->win); sound_play(SFX_OPEN); }
            } else {
                app_open(it->icon);
            }
        }
        return;
    }
    if (rect_has(power_rect(), mx, my)) {
        sound_play(SFX_CLICK);
        static const char *const items[] = { "Mettre en veille", "Redémarrer", "Éteindre" };
        rect_t r = power_rect();
        wm_menu(r.x, r.y - 8, items, 3, power_menu_cb, NULL);
        return;
    }
    if (rect_has(tray_rect(), mx, my)) {
        if (tray_hover == TP_CLOCK) toggle_cal();
        else if (tray_hover == TP_KBD) { sound_play(SFX_CLICK); wm_kbd_layout_toggle(); }
        else if (tray_hover == TP_NET) { sound_play(SFX_CLICK); app_network_open(); }
        else if (tray_hover == TP_VOL && plat_volume(-1) >= 0) { plat_muted(!plat_muted(-1)); change_volume(0); }
        else if (tray_hover == TP_BAT) battery_toast();
        return;
    }
    if (cal_open) toggle_cal();
    window_t *w = window_at(mx, my);
    if (w) {
        if (w != focused || zord[zn - 1] != (int)(w - wins)) wm_focus(w);
        for (int b = HV_CLOSE; b <= HV_MIN; b++)
            if (has_btn(w, b) && rect_has(win_btn(w, b), mx, my) && e->button == BTN_LEFT) {
                if (b == HV_CLOSE) wm_request_close(w);
                else if (b == HV_MAX) { sound_play(SFX_CLICK); toggle_max(w); }
                else minimize(w);
                return;
            }
        if (e->button == BTN_LEFT && near_resize(w, mx, my)) {
            drag_kind = DR_SIZE; drag_win = w; drag_start = w->r; drag_dx = mx; drag_dy = my;
            return;
        }
        if (my < w->r.y + TITLE_H) {
            if (e->button == BTN_LEFT && dbl) { toggle_max(w); return; }
            if (e->button == BTN_LEFT) {
                if (w->maximized) {
                    int rw = w->restore.w;
                    w->maximized = false;
                    set_rect(w, R(mx - rw / 2, w->r.y, rw, w->restore.h));
                }
                drag_kind = DR_MOVE; drag_win = w; drag_dx = mx - w->r.x; drag_dy = my - w->r.y;
            }
            return;
        }
        if (rect_has(wm_client(w), mx, my)) {
            capture = w;
            event_t ev = *e;
            ev.x = mx; ev.y = my;
            ev.dx = dbl;           /* double-clic signalé dans dx */
            dispatch_client(w, &ev);
        }
        return;
    }
    click_desktop(mx, my, e->button, dbl);
}

static void motion(void) {
    if (desk_drag >= 0) {
        if (!desk_dragging && abs_i(mx - desk_drag_x0) + abs_i(my - desk_drag_y0) > 6) desk_dragging = true;
        if (desk_dragging) {
            int c, r;
            desk_target(&c, &r);
            rect_t g = rect_grow(R(mx - desk_drag_dx, my - desk_drag_dy, 92, 96), 4), t = rect_grow(cell_rect(c, r), 4);
            damage(ghost_last, 0); damage(target_last, 0); damage(g, 0); damage(t, 0);
            damage(rect_grow(desk_icon_rect(desk_drag), 4), 0);
            ghost_last = g;
            target_last = t;
            return;
        }
    }
    if (drag_kind == DR_MOVE && alive(drag_win)) {
        int nx = mx - drag_dx, ny = CLAMP(my - drag_dy, 0, scr_h - 60);
        bool had = drag_win->glass_ok;
        set_rect(drag_win, R(nx, ny, drag_win->r.w, drag_win->r.h));
        /* pendant le déplacement, le verre garde l'image déjà calculée : il est recalculé
         * dès que la fenêtre s'arrête un instant ou qu'on la lâche */
        if (had) { drag_win->glass_ok = true; glass_stale = true; }
        last_drag_move = now;
        return;
    }
    if (drag_kind == DR_SIZE && alive(drag_win)) {
        int nw = MAX(drag_win->minw, drag_start.w + mx - drag_dx), nh = MAX(drag_win->minh + TITLE_H, drag_start.h + my - drag_dy);
        if (nw != drag_win->r.w || nh != drag_win->r.h) set_rect(drag_win, R(drag_start.x, drag_start.y, nw, nh));
        return;
    }
    update_hover();
    if (alive(capture)) {
        event_t ev = { 0 };
        ev.type = EV_MOTION;
        ev.x = mx; ev.y = my; ev.mods = mods;
        dispatch_client(capture, &ev);
    }
}

static void cycle_windows(void) {
    for (int i = 0; i < zn; i++) {
        window_t *w = &wins[zord[i]];
        if (alive(w) && w != focused) { wm_focus(w); return; }
    }
}

static void menu_key(const event_t *e) {
    if (e->key == K_ESC || e->key == K_SUPER) { close_menu(); return; }
    if (e->key == K_ENTER) {
        menu_filter();
        if (menu_napps) { int a = menu_apps[menu_sel]; close_menu(); app_open(a); }
        return;
    }
    int cols = MENU_COLS;
    if (e->key == K_RIGHT) menu_sel = MIN(menu_sel + 1, menu_napps - 1);
    else if (e->key == K_LEFT) menu_sel = MAX(menu_sel - 1, 0);
    else if (e->key == K_DOWN) menu_sel = MIN(menu_sel + cols, menu_napps - 1);
    else if (e->key == K_UP) menu_sel = MAX(menu_sel - cols, 0);
    else if (ui_field_key(&menu_search, e)) { menu_sel = 0; menu_hover = -1; }
    if (!menu_search.s[0] && (e->key == K_RIGHT || e->key == K_LEFT || e->key == K_UP || e->key == K_DOWN)) menu_hover = menu_sel;
    damage(menu_rect(), 101);
}

void core_event(const event_t *e) {
    now = plat_ms();
    if (e->type == EV_KEY) {
        mods = e->mods;
        if (!e->down) {
            if (alive(focused) && focused->event && mode == MODE_DESK && !menu_open) focused->event(focused, e);
            return;
        }
        if (mode == MODE_SPLASH) return;
        switch (e->key) {
        case K_POWER: if (mode == MODE_DESK) wm_power(0); return;
        case K_LID_CLOSE: wm_lock(); return;
        case K_VOL_UP: change_volume(6); return;
        case K_VOL_DOWN: change_volume(-6); return;
        case K_MUTE: if (plat_volume(-1) >= 0) { plat_muted(!plat_muted(-1)); change_volume(0); } return;
        case K_BRIGHT_UP: case K_BRIGHT_DOWN: {
            int b = plat_brightness(-1);
            if (b >= 0) {
                b = CLAMP(b + (e->key == K_BRIGHT_UP ? 10 : -10), 5, 100);
                plat_brightness(b);
                g_set.brightness = b;
                settings_save();
                if (mode == MODE_DESK) show_osd(1, b);
            }
            return;
        }
        }
        if (mode == MODE_LOCK) {
            if (trans) return;
            if (!g_set.pass[0]) { unlock(); return; }
            if (e->key == K_ENTER) {
                bool good = settings_pass_check(lock_pw.s);
                ui_field_set(&lock_pw, "");
                if (good) { lock_bad = false; unlock(); }
                else { lock_bad = true; sound_play(SFX_ERROR); }
            } else if (e->key == K_ESC) ui_field_set(&lock_pw, "");
            else { ui_field_key(&lock_pw, e); lock_bad = false; }
            damage(R(0, lock_field_rect().y - 40, scr_w, 120), 0);
            return;
        }
        if (pop_open && e->key == K_ESC) { close_pop(); return; }
        if (menu_open) { menu_key(e); return; }
        if (e->key == K_SUPER) { open_menu(); return; }
        if ((e->mods & MOD_ALT) && e->key == K_TAB) { cycle_windows(); return; }
        if ((e->mods & MOD_ALT) && e->key == K_F4) { if (alive(focused)) wm_request_close(focused); return; }
        if (e->key == K_PRINT) { screenshot(); return; }
        if ((e->mods & (MOD_CTRL | MOD_ALT)) == (MOD_CTRL | MOD_ALT) && e->key == K_CHAR && (e->ch == 't' || e->ch == 'T')) {
            app_terminal_open();
            return;
        }
        if ((e->mods & MOD_SUPER) && e->key == K_CHAR && (e->ch == 'l' || e->ch == 'L')) { wm_lock(); return; }
        if (cal_open && e->key == K_ESC) { toggle_cal(); return; }
        if (alive(focused) && focused->event && !focused->minimized) focused->event(focused, e);
        else if (e->key == K_ENTER && desk_sel >= 0) app_open(desk_icons[desk_sel]);
        return;
    }
    if (e->type == EV_MOTION) {
        int speed = g_set.pointer ? g_set.pointer : 3;
        if (e->abs) { mx = e->x; my = e->y; }
        else { mx += e->dx * (speed + 1) / 4; my += e->dy * (speed + 1) / 4; }
        mx = CLAMP(mx, 0, scr_w - 1);
        my = CLAMP(my, 0, scr_h - 1);
        cursor_moved = true;
        if (mode == MODE_DESK) motion();
        return;
    }
    if (e->type == EV_BUTTON) {
        mods = e->mods;
        mouse_button(e);
        update_hover();
        return;
    }
    if (e->type == EV_WHEEL && mode == MODE_DESK && !menu_open) {
        if (rect_has(tparts[TP_VOL], mx, my)) { change_volume(e->dy < 0 ? 6 : -6); return; }
        window_t *w = alive(capture) ? capture : window_at(mx, my);
        if (w && rect_has(wm_client(w), mx, my)) {
            event_t ev = *e;
            ev.x = mx; ev.y = my;
            dispatch_client(w, &ev);
        }
    }
}

/* ---------------- boucle ---------------- */
void core_init(int w, int h) {
    settings_load();
    sound_init();
    scr_w = w;
    scr_h = h;
    scene = canvas_new(w, h);
    T = &scene;
    cursor_make();
    mx = w / 2;
    my = h / 2;
    now = plat_ms();
}

void core_resize(int w, int h) {
    if (w == scr_w && h == scr_h) return;
    canvas_free(&scene);
    scr_w = w;
    scr_h = h;
    scene = canvas_new(w, h);
    T = &scene;
    mx = CLAMP(mx, 0, w - 1);
    my = CLAMP(my, 0, h - 1);
    for (int i = 0; i < MAX_WIN; i++)
        if (alive(&wins[i])) {
            window_t *win = &wins[i];
            rect_t r = win->r;
            r.w = MIN(r.w, w - 20);
            r.h = MIN(r.h, h - DOCK_H - 30);
            r.x = CLAMP(r.x, 0, w - r.w);
            r.y = CLAMP(r.y, 0, h - r.h);
            win->r = r;
            surf_alloc(win);
        }
    damage_all();
}

void core_splash(const char *step, int percent) {
    (void)percent;
    static bool drawn;
    bool first = mode != MODE_SPLASH || !drawn;
    drawn = true;
    strlcpy(splash_step, step, sizeof splash_step);
    mode = MODE_SPLASH;
    now = plat_ms();
    if (first) damage_all();
    else damage(splash_text_rect(), 0);
    compose();
}

void core_start(void) {
    now = plat_ms();
    local_now(&now_dt);
    plat_battery(&bat);
    if (g_set.volume >= 0) plat_volume(g_set.volume);
    snapshot();                     /* l'écran de démarrage s'efface sur l'écran de verrouillage */
    mode = MODE_LOCK;
    trans = TR_FADE;
    trans_t0 = now;
    dock_build();
    damage_all();
    sound_play(SFX_STARTUP);
}

/* heure par Internet : une fois par démarrage, dès qu'une connexion existe ; l'horloge du PC
 * reçoit ensuite l'heure exacte (en temps universel), elle ne dérive donc plus au redémarrage */
static void time_sync(bool online) {
    static int job = -1;
    static bool done;
    static uint64_t next_try;
    if (job >= 0) {
        char out[128];
        int r = plat_spawn_poll(job, out, sizeof out);
        if (r == -1) return;
        job = -1;
        if (r == 0) {
            done = true;
            g_set.rtc_local = false;           /* l'heure système est maintenant en temps universel */
            settings_save();
            plat_rtc_save();
            local_now(&now_dt);
            if (mode == MODE_LOCK) damage_all(); else damage(tray_rect(), 100);
        } else next_try = now + 120000;
        return;
    }
    if (done || !online || now < next_try) return;
    static const char *const argv[] = { "/bin/busybox", "ntpd", "-n", "-q", "-p", "pool.ntp.org", NULL };
    job = plat_spawn(argv);
    next_try = now + 120000;
}



static void poll_hardware(void) {
    static uint64_t next, next_update;
    static int last_upd = UPD_IDLE;
    if (now < next) return;
    next = now + 2500;
    /* mises à jour : recherche 90 s après le démarrage puis toutes les 6 heures */
    if (!next_update) next_update = now + 90000;
    if (now >= next_update) { next_update = now + 6ull * 3600 * 1000; plat_update_check(true); }
    char um[160];
    int us = plat_update_state(um, sizeof um, NULL);
    if (us != last_upd) {
        if (us == UPD_DOWNLOADING && last_upd == UPD_CHECKING) wm_toast("Une mise à jour de TitanOs est en cours de téléchargement.");
        if (us == UPD_DONE) wm_toast(um);
        last_upd = us;
    }
    battery_t b;
    plat_battery(&b);
    char ssid[33] = { 0 };
    int sig = 0, ws = plat_wifi_status(ssid, sizeof ssid, &sig);
    netif_t ifs[8];
    int n = plat_netifs(ifs, 8);
    bool wired = false;
    for (int i = 0; i < n; i++)
        if (strcmp(ifs[i].name, "lo") && strcmp(ifs[i].kind, "Wi-Fi") && ifs[i].carrier && ifs[i].ip[0]) wired = true;
    bool changed = memcmp(&b, &bat, sizeof b) || ws != wifi_state || (sig / 25) != (wifi_signal / 25) ||
                   wired != net_wired || strcmp(ssid, wifi_ssid);
    if (b.present != bat.present) damage(rect_grow(tray_rect(), 30), 0);
    if (b.present && !b.charging && !b.ac && b.percent <= 10 && !bat_warned) {
        bat_warned = true;
        wm_toast("Batterie faible : branchez le chargeur.");
    }
    if (b.ac || b.percent > 15) bat_warned = false;
    bat = b;
    wifi_state = ws;
    wifi_signal = sig;
    time_sync(ws == WIFI_CONNECTED || wired);
    browser_autoinstall(ws == WIFI_CONNECTED || wired);
    char bm[160];
    static int last_br = -1;
    int br = plat_browser_state(bm, sizeof bm, NULL);
    if (last_br == BR_INSTALLING && br == BR_READY) wm_toast("Chromium est installé : il est dans le dock.");
    last_br = br;
    strlcpy(wifi_ssid, ssid, sizeof wifi_ssid);
    net_wired = wired;
    if (changed) damage(rect_grow(tray_rect(), 30), mode == MODE_DESK ? 100 : 0);
}

static bool glass_frozen(void) { return drag_kind == DR_MOVE && alive(drag_win) && now - last_drag_move < 120; }

int core_frame(void) {
    now = plat_ms();
    /* fin d'un déplacement (pause ou bouton lâché) : les verres se remettent à jour */
    if (glass_stale && !glass_frozen()) {
        glass_stale = false;
        for (int i = 0; i < zn; i++) wins[zord[i]].glass_ok = false;
        g_dock.ok = g_tray.ok = g_menu.ok = g_cal.ok = g_pop.ok = g_toast.ok = g_osd.ok = false;
        damage_all();
    }
    bool anim = false;
    if (mode == MODE_SPLASH) {
        damage(spinner_rect(), 0);
        compose();
        return 16;
    }
    datetime_t dt;
    local_now(&dt);
    if (dt.min != now_dt.min || dt.hour != now_dt.hour || (g_set.seconds && dt.sec != now_dt.sec)) {
        now_dt = dt;
        if (mode == MODE_LOCK) damage_all();
        else { damage(tray_rect(), 100); if (cal_open) damage(cal_rect(), 101); }
    }
    if (mode == MODE_LOCK && dt.sec != now_dt.sec) damage(R(0, scr_h - 44, scr_w, 30), 0);   /* diagnostic */
    now_dt = dt;
    poll_hardware();
    if (toast_until && now >= toast_until) { toast_until = 0; el_hide(&g_toast); }
    if (osd_until && now >= osd_until) { osd_until = 0; el_hide(&g_osd); }
    /* animations des éléments */
    glass_el_t *els[] = { &g_menu, &g_cal, &g_pop, &g_toast, &g_osd };
    rect_t tr = toast_rect();
    rect_t er[] = { rect_grow(menu_rect(), 50), rect_grow(cal_rect(), 44), rect_grow(pop_rect(), 36),
                    R(tr.x - 40, tr.y - 40, tr.w + 120, tr.h + 80), rect_grow(osd_rect(), 36) };
    int lv[] = { 101, 101, 102, 103, 104 };
    for (int i = 0; i < countof(els); i++)
        if (el_animating(els[i])) { damage(er[i], lv[i]); anim = true; }
    /* fenêtres : animations et fermetures terminées */
    for (int i = 0; i < MAX_WIN; i++) {
        window_t *w = &wins[i];
        if (!w->used || !w->anim) continue;
        int dur = w->anim == A_OPEN ? T_WIN_IN : w->anim == A_CLOSE ? T_WIN_OUT : w->anim == A_MAX ? T_MAX : T_MIN;
        damage(anim_area(w), 0);
        anim = true;
        if (now - w->anim_t0 >= (uint64_t)dur) {
            if (w->anim == A_CLOSE) finish_close(w);
            else if (w->anim == A_MIN) { w->minimized = true; w->anim = A_NONE; damage(anim_area(w), 0); }
            else { damage(anim_area(w), 0); w->anim = A_NONE; }
        }
    }
    /* dock : les icônes survolées grandissent en douceur */
    bool dock_moving = false;
    for (int i = -1; i < dock_n; i++) {
        float *g = i < 0 ? &start_grow : &dock_items[i].grow;
        float target = (dock_hover == (i < 0 ? -2 : i)) ? 1.0f : 0.0f;
        if (*g != target) {
            *g += (target - *g) * 0.35f;
            if (*g - target < 0.02f && target - *g < 0.02f) *g = target;
            dock_moving = true;
        }
    }
    if (dock_moving) { damage(rect_grow(dock_rect(), 20), 100); anim = true; }
    if (trans) { damage_all(); anim = true; }
    int wait = anim ? 16 : 500;
    if (glass_stale) wait = MIN(wait, 130);
    for (int i = 0; i < MAX_WIN; i++) {
        window_t *w = &wins[i];
        if (alive(w) && w->tick) { w->tick(w, now); wait = MIN(wait, 15); }
    }
    for (int i = 0; i < MAX_WIN; i++) {
        window_t *w = &wins[i];
        if (!alive(w) || !w->dirty) continue;
        w->dirty = false;
        canvas_noclip(&w->surf);
        canvas_clear(&w->surf, 0);
        if (w->draw) w->draw(w, &w->surf);
        mask_corners(&w->surf);
        damage(wm_client(w), win_level(w));
    }
    if (ndmg) compose();
    else if (cursor_moved) present_rect(R(0, 0, 0, 0));
    if (g_toast.shown || g_osd.shown) wait = MIN(wait, 100);
    return wait;
}

/* position de la fenêtre active (aperçu et tests) */
rect_t wm_focused_rect(void) { return alive(focused) ? focused->r : R(0, 0, 0, 0); }
