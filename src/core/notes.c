/* TitanOs - Bloc-notes : éditeur de texte (UTF-8), sélection, presse-papiers, enregistrement */
#include "desk.h"

#define BAR_H  52
#define LINE_H 21
#define MARGIN 18

typedef struct {
    char *t;
    int len, cap, cur, anchor;     /* anchor < 0 : pas de sélection */
    int scroll_y, scroll_x, goal_x;
    char path[512];
    bool modified, selecting;
    int hover;
} notes_t;

static const font_t *NF = &font_body;

static bool reserve(notes_t *n, int extra) {
    if (n->len + extra + 1 <= n->cap) return true;
    int cap = MAX(n->cap * 2, n->len + extra + 256);
    char *p = realloc(n->t, (size_t)cap);
    if (!p) return false;
    n->t = p;
    n->cap = cap;
    return true;
}

static int line_start(notes_t *n, int pos) { while (pos > 0 && n->t[pos - 1] != '\n') pos--; return pos; }
static int line_end(notes_t *n, int pos) { while (pos < n->len && n->t[pos] != '\n') pos++; return pos; }
static int line_of(notes_t *n, int pos) { int l = 0; for (int i = 0; i < pos; i++) if (n->t[i] == '\n') l++; return l; }
static int line_count(notes_t *n) { return line_of(n, n->len) + 1; }
static int pos_of_line(notes_t *n, int line) {
    int p = 0;
    while (line > 0 && p < n->len) { if (n->t[p++] == '\n') line--; }
    return p;
}

static int x_of(notes_t *n, int pos) { int s = line_start(n, pos); return text_width_n(NF, n->t + s, pos - s); }

static int pos_at_x(notes_t *n, int ls, int x) {
    int le = line_end(n, ls), p = ls, w = 0;
    while (p < le) {
        int nx = utf8_next(n->t, p);
        int cw = text_width_n(NF, n->t + p, nx - p);
        if (w + cw / 2 > x) break;
        w += cw;
        p = nx;
    }
    return p;
}

static bool has_sel(notes_t *n) { return n->anchor >= 0 && n->anchor != n->cur; }
static int sel_a(notes_t *n) { return MIN(n->anchor, n->cur); }
static int sel_b(notes_t *n) { return MAX(n->anchor, n->cur); }

static void del_range(notes_t *n, int a, int b) {
    memmove(n->t + a, n->t + b, (size_t)(n->len - b + 1));
    n->len -= b - a;
    n->cur = a;
    n->anchor = -1;
    n->modified = true;
}

static void insert(notes_t *n, const char *s, int len) {
    if (has_sel(n)) del_range(n, sel_a(n), sel_b(n));
    n->anchor = -1;
    if (!reserve(n, len)) return;
    memmove(n->t + n->cur + len, n->t + n->cur, (size_t)(n->len - n->cur + 1));
    memcpy(n->t + n->cur, s, (size_t)len);
    n->len += len;
    n->cur += len;
    n->modified = true;
}

static void set_text(notes_t *n, const char *s, int len) {
    n->len = 0;
    if (!reserve(n, len)) return;
    memcpy(n->t, s, (size_t)len);
    n->len = len;
    n->t[len] = 0;
    n->cur = 0;
    n->anchor = -1;
    n->scroll_x = n->scroll_y = 0;
    n->modified = false;
}

static void update_title(window_t *w, notes_t *n) {
    const char *base = n->path[0] ? strrchr(n->path, '/') : NULL;
    char t[120];
    snprintf(t, sizeof t, "%s%s – Bloc-notes", n->modified ? "• " : "", base ? base + 1 : "Sans titre");
    if (strcmp(t, w->title)) wm_set_title(w, t);
}

/* ---- enregistrement et ouverture ---- */
static window_t *dlg_win;

static void save_to(window_t *w, notes_t *n, const char *path) {
    if (plat_writefile(path, n->t, (size_t)n->len) != 0) { dlg_message("Bloc-notes", "Impossible d'enregistrer ce fichier."); return; }
    strlcpy(n->path, path, sizeof n->path);
    n->modified = false;
    update_title(w, n);
    wm_invalidate(w);
    wm_toast("Fichier enregistré.");
}

static void save_as_cb(const char *name, void *ctx) {
    notes_t *n = ctx;
    if (!name[0] || !dlg_win || !dlg_win->used) return;
    char p[512];
    if (name[0] == '/') strlcpy(p, name, sizeof p);
    else {
        char dir[400];
        snprintf(dir, sizeof dir, "%s/Documents", plat_home());
        if (!plat_exists(dir, NULL)) plat_mkdir(dir);
        snprintf(p, sizeof p, "%s/%s", dir, name);
    }
    save_to(dlg_win, n, p);
}

static void notes_save(window_t *w, notes_t *n, bool ask) {
    dlg_win = w;
    if (n->path[0] && !ask) save_to(w, n, n->path);
    else dlg_input("Enregistrer", "Nom du fichier (dans Documents) ou chemin complet", "Sans titre.txt", save_as_cb, n);
}

static void load(window_t *w, notes_t *n, const char *path) {
    char *data;
    size_t len;
    if (plat_readfile(path, &data, &len) != 0) { dlg_message("Bloc-notes", "Impossible d'ouvrir ce fichier."); return; }
    set_text(n, data, (int)len);
    free(data);
    strlcpy(n->path, path, sizeof n->path);
    update_title(w, n);
    wm_invalidate(w);
}

static void open_cb(const char *name, void *ctx) {
    notes_t *n = ctx;
    if (!name[0] || !dlg_win || !dlg_win->used) return;
    char p[512];
    if (name[0] == '/') strlcpy(p, name, sizeof p);
    else snprintf(p, sizeof p, "%s/Documents/%s", plat_home(), name);
    load(dlg_win, n, p);
}

/* ---- dessin ---- */
static rect_t text_area(canvas_t *c) { return R(0, BAR_H, c->w, c->h - BAR_H - 28); }
static rect_t tb_btn(int i) { return R(14 + i * 118, 9, 110, 34); }
static const char *const tb_labels[3] = { "Nouveau", "Ouvrir…", "Enregistrer" };

static void scroll_to_cursor(window_t *w, notes_t *n) {
    rect_t a = text_area(&w->surf);
    int vis = MAX(1, (a.h - 16) / LINE_H);
    int l = line_of(n, n->cur);
    if (l < n->scroll_y) n->scroll_y = l;
    if (l >= n->scroll_y + vis) n->scroll_y = l - vis + 1;
    int x = x_of(n, n->cur), avail = a.w - 2 * MARGIN;
    if (x < n->scroll_x) n->scroll_x = MAX(0, x - 40);
    if (x > n->scroll_x + avail) n->scroll_x = x - avail + 40;
}

static void notes_draw(window_t *w, canvas_t *c) {
    notes_t *n = w->data;
    for (int i = 0; i < 3; i++) ui_button(c, tb_btn(i), tb_labels[i], i == 2 ? BTN_ACCENT : BTN_NORMAL, n->hover == i);
    gfx_fill(c, R(0, BAR_H - 1, c->w, 1), C_LINE);
    rect_t a = text_area(c);
    gfx_fill(c, a, 0x70080C16);
    rect_t old = c->clip;
    canvas_clip(c, R(a.x + 6, a.y, a.w - 12, a.h));
    int vis = (a.h - 16) / LINE_H + 1;
    int p = pos_of_line(n, n->scroll_y);
    int sa = has_sel(n) ? sel_a(n) : -1, sb = has_sel(n) ? sel_b(n) : -1;
    for (int l = 0; l < vis && p <= n->len; l++) {
        int e = line_end(n, p), y = a.y + 8 + l * LINE_H, x0 = a.x + MARGIN - n->scroll_x;
        if (sa >= 0 && sa <= e && sb >= p) {
            int s0 = MAX(sa, p), s1 = MIN(sb, e);
            int xa = x0 + text_width_n(NF, n->t + p, s0 - p), xb = x0 + text_width_n(NF, n->t + p, s1 - p);
            if (sb > e) xb += 6;
            gfx_fill(c, R(xa, y - 1, MAX(xb - xa, 2), LINE_H), (accent() & 0xFFFFFF) | 0x80000000);
        }
        text_draw_n(c, NF, x0, y + 2, n->t + p, e - p, C_TEXT);
        if (n->cur >= p && n->cur <= e && wm_focused() == w)
            gfx_fill(c, R(x0 + text_width_n(NF, n->t + p, n->cur - p), y, 2, LINE_H - 2), accent());
        p = e + 1;
    }
    c->clip = old;
    ui_scrollbar(c, R(c->w - 8, a.y + 4, 4, a.h - 8), line_count(n), vis - 1, n->scroll_y);
    char st[96];
    int l = line_of(n, n->cur);
    snprintf(st, sizeof st, "Ligne %d, colonne %d  ·  %d caractères", l + 1,
             utf8_len_n(n->t + line_start(n, n->cur), n->cur - line_start(n, n->cur)) + 1, utf8_len(n->t));
    text_draw(c, &font_small, 16, c->h - 22, st, C_MUTED);
    const char *hint = "Ctrl+S enregistrer · Ctrl+O ouvrir";
    text_draw(c, &font_small, c->w - 16 - text_width(&font_small, hint), c->h - 22, hint, C_MUTED);
}

static int pos_from_mouse(window_t *w, notes_t *n, int x, int y) {
    rect_t a = text_area(&w->surf);
    int l = n->scroll_y + MAX(0, (y - a.y - 8)) / LINE_H;
    l = MIN(l, line_count(n) - 1);
    return pos_at_x(n, pos_of_line(n, l), x - a.x - MARGIN + n->scroll_x);
}

static void move(notes_t *n, int pos, bool shift) {
    if (shift) { if (n->anchor < 0) n->anchor = n->cur; }
    else n->anchor = -1;
    n->cur = CLAMP(pos, 0, n->len);
}

static void notes_event(window_t *w, const event_t *e) {
    notes_t *n = w->data;
    rect_t a = text_area(&w->surf);
    if (e->type == EV_MOTION) {
        int h = -1;
        for (int i = 0; i < 3; i++) if (rect_has(tb_btn(i), e->x, e->y)) h = i;
        bool redraw = h != n->hover;
        n->hover = h;
        if (n->selecting && e->x > -500) { n->cur = pos_from_mouse(w, n, e->x, e->y); scroll_to_cursor(w, n); redraw = true; }
        if (redraw) wm_invalidate(w);
        return;
    }
    if (e->type == EV_WHEEL) {
        n->scroll_y = CLAMP(n->scroll_y + e->dy * 3, 0, MAX(0, line_count(n) - 3));
        wm_invalidate(w);
        return;
    }
    if (e->type == EV_BUTTON) {
        if (!e->down) { n->selecting = false; if (n->anchor == n->cur) n->anchor = -1; return; }
        if (e->button != BTN_LEFT) return;
        if (n->hover >= 0) sound_play(SFX_CLICK);
        if (n->hover == 0) { set_text(n, "", 0); n->path[0] = 0; update_title(w, n); wm_invalidate(w); return; }
        if (n->hover == 1) { dlg_win = w; dlg_input("Ouvrir", "Nom du fichier (dans Documents) ou chemin complet", "", open_cb, n); return; }
        if (n->hover == 2) { notes_save(w, n, false); return; }
        if (rect_has(a, e->x, e->y)) {
            n->cur = pos_from_mouse(w, n, e->x, e->y);
            n->anchor = (e->mods & MOD_SHIFT) && n->anchor >= 0 ? n->anchor : n->cur;
            n->selecting = true;
            n->goal_x = x_of(n, n->cur);
            wm_invalidate(w);
        }
        return;
    }
    if (e->type != EV_KEY || !e->down) return;
    bool shift = e->mods & MOD_SHIFT, ctrl = e->mods & MOD_CTRL;
    int vis = MAX(1, (a.h - 16) / LINE_H);
    bool vertical = false;
    switch (e->key) {
    case K_LEFT:
        if (has_sel(n) && !shift) move(n, sel_a(n), false); else move(n, utf8_prev(n->t, n->cur), shift);
        break;
    case K_RIGHT:
        if (has_sel(n) && !shift) move(n, sel_b(n), false); else move(n, utf8_next(n->t, n->cur), shift);
        break;
    case K_UP: case K_DOWN: case K_PGUP: case K_PGDN: {
        int l = line_of(n, n->cur), d = e->key == K_UP ? -1 : e->key == K_DOWN ? 1 : e->key == K_PGUP ? -vis : vis;
        int nl = CLAMP(l + d, 0, line_count(n) - 1);
        move(n, pos_at_x(n, pos_of_line(n, nl), n->goal_x), shift);
        vertical = true;
        break;
    }
    case K_HOME: move(n, ctrl ? 0 : line_start(n, n->cur), shift); break;
    case K_END: move(n, ctrl ? n->len : line_end(n, n->cur), shift); break;
    case K_ENTER: insert(n, "\n", 1); break;
    case K_TAB: insert(n, "    ", 4); break;
    case K_BACKSPACE:
        if (has_sel(n)) del_range(n, sel_a(n), sel_b(n));
        else if (n->cur > 0) del_range(n, utf8_prev(n->t, n->cur), n->cur);
        break;
    case K_DELETE:
        if (has_sel(n)) del_range(n, sel_a(n), sel_b(n));
        else if (n->cur < n->len) del_range(n, n->cur, utf8_next(n->t, n->cur));
        break;
    case K_CHAR:
        if (ctrl) {
            int ch = e->ch | 0x20;
            if (ch == 'a') { n->anchor = 0; n->cur = n->len; }
            else if ((ch == 'c' || ch == 'x') && has_sel(n)) {
                clip_set(n->t + sel_a(n), sel_b(n) - sel_a(n));
                if (ch == 'x') del_range(n, sel_a(n), sel_b(n));
            } else if (ch == 'v') { const char *p = clip_get(); insert(n, p, (int)strlen(p)); }
            else if (ch == 's') { notes_save(w, n, shift); return; }
            else if (ch == 'o') { dlg_win = w; dlg_input("Ouvrir", "Nom du fichier (dans Documents) ou chemin complet", "", open_cb, n); return; }
            else if (ch == 'n') { set_text(n, "", 0); n->path[0] = 0; }
            break;
        }
        if (e->ch >= 32 || e->ch == '\t') {
            char u[4];
            insert(n, u, utf8_encode(u, e->ch));
        }
        break;
    default: return;
    }
    if (!vertical) n->goal_x = x_of(n, n->cur);
    scroll_to_cursor(w, n);
    update_title(w, n);
    wm_invalidate(w);
}

static void discard_cb(bool ok, void *ctx) {
    window_t *w = ctx;
    if (!ok || !w->used || !w->data) return;
    ((notes_t *)w->data)->modified = false;
    wm_close(w);
}

static bool notes_can_close(window_t *w) {
    notes_t *n = w->data;
    if (!n || !n->modified) return true;
    dlg_confirm("Bloc-notes", "Ce texte n'est pas enregistré. Le fermer quand même ? (Ctrl+S pour enregistrer)",
                "Fermer", discard_cb, w);
    return false;
}

static void notes_close(window_t *w) {
    notes_t *n = w->data;
    if (dlg_win == w) dlg_win = NULL;
    free(n->t);
    free(n);
}

void app_notes_open(const char *path) {
    notes_t *n = calloc(1, sizeof *n);
    if (!n || !reserve(n, 256)) { free(n); return; }
    n->t[0] = 0;
    n->anchor = -1;
    n->hover = -1;
    window_t *w = wm_open("Sans titre – Bloc-notes", ICON_NOTES, 720, 500, WF_RESIZABLE);
    if (!w) { free(n->t); free(n); return; }
    w->minw = 380;
    w->data = n;
    w->draw = notes_draw;
    w->event = notes_event;
    w->on_close = notes_close;
    w->can_close = notes_can_close;
    w->cursor = CURSOR_TEXT;
    if (path) load(w, n, path);
}
