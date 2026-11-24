/* TitanOs - Fichiers : explorateur (dossiers, création, renommage, copie, suppression) */
#include "desk.h"

#define SIDE_W 196
#define BAR_H  56
#define ROW_H  36

typedef struct {
    char path[512];
    dirent_t *ent;
    int n, sel, hover, scroll, side_hover, bar_hover;
    volume_t vols[6];
    int nvols;
    uint64_t next_vols;
} files_t;

/* lignes de la barre latérale : emplacements, puis clés USB (identifiants 100+i, éjecter 200+i) */
static rect_t place_rect(int i) { return R(10, 36 + i * 38, SIDE_W - 20, 34); }
static rect_t vol_rect(int i) { return R(10, 36 + 6 * 38 + 34 + i * 38, SIDE_W - 20, 34); }
static rect_t eject_rect(int i) { rect_t r = vol_rect(i); return R(r.x + r.w - 32, r.y + 4, 26, 26); }

static const char *const place_names[] = { "Accueil", "Documents", "Images", "Musique", "Téléchargements", "Système" };
static const int place_icons[] = { ICON_HOME, ICON_FOLDER, ICON_IMAGE, ICON_FOLDER, ICON_FOLDER, ICON_DISK };

static void place_path(int i, char *out, size_t n) {
    if (i == 5) { strlcpy(out, "/", n); return; }
    if (i == 0) { strlcpy(out, plat_home(), n); return; }
    snprintf(out, n, "%s/%s", plat_home(), place_names[i]);
}

static void path_join(char *out, size_t n, const char *dir, const char *name) {
    if (!strcmp(dir, "/")) snprintf(out, n, "/%s", name);
    else snprintf(out, n, "%s/%s", dir, name);
}

static int ent_cmp(const void *a, const void *b) {
    const dirent_t *x = a, *y = b;
    if (x->dir != y->dir) return x->dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

static void files_load(window_t *w, files_t *f) {
    free(f->ent);
    f->ent = NULL;
    f->n = 0;
    dirent_t *all;
    int n = plat_listdir(f->path, &all);
    if (n < 0) { wm_toast("Impossible d'ouvrir ce dossier."); n = 0; all = NULL; }
    int k = 0;
    for (int i = 0; i < n; i++) if (all[i].name[0] != '.') all[k++] = all[i];
    sort(all, (size_t)k, sizeof(dirent_t), ent_cmp);
    f->ent = all;
    f->n = k;
    f->sel = k ? 0 : -1;
    f->scroll = 0;
    const char *base = strrchr(f->path, '/');
    char title[96];
    snprintf(title, sizeof title, "Fichiers – %s", (base && base[1]) ? base + 1 : f->path);
    wm_set_title(w, title);
    wm_invalidate(w);
}

static void files_go(window_t *w, files_t *f, const char *path) {
    strlcpy(f->path, path, sizeof f->path);
    files_load(w, f);
}

static void files_up(window_t *w, files_t *f) {
    char *s = strrchr(f->path, '/');
    if (!s) return;
    if (s == f->path) f->path[1] = 0;
    else *s = 0;
    files_load(w, f);
}

/* ---- types de fichiers et application qui les ouvre ---- */
static bool ext_in(const char *n, const char *const *ext) {
    for (int i = 0; ext[i]; i++) if (str_ends(n, ext[i])) return true;
    return false;
}
static const char *const TEXT_EXT[] = { ".txt", ".md", ".cfg", ".conf", ".sh", ".log", ".c", ".h", ".cpp", ".hpp", ".py", ".js",
    ".ts", ".css", ".json", ".ini", ".csv", ".xml", ".yml", ".yaml", ".toml", ".srt", ".nfo", ".tex", ".bat", ".ps1", ".rs",
    ".go", ".java", ".lua", ".sql", ".diff", ".patch", NULL };
static const char *const IMAGE_EXT[] = { ".png", ".jpg", ".jpeg", ".jpe", ".bmp", ".gif", ".ppm", ".pgm", NULL };
/* ouverts dans Chromium : documents, médias, pages web, images modernes */
static const char *const WEB_EXT[] = { ".pdf", ".html", ".htm", ".svg", ".webp", ".avif", ".mp4", ".m4v", ".webm", ".mkv", ".mov",
    ".ogv", ".mp3", ".m4a", ".ogg", ".oga", ".opus", ".wav", ".flac", ".aac", NULL };
static const char *const ARCHIVE_EXT[] = { ".zip", ".tar", ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tbz2", ".gz", NULL };

/* fichier sans extension connue : texte s'il ne contient pas d'octet nul au début */
static bool looks_like_text(const char *path) {
    char *d;
    size_t n;
    if (plat_readfile(path, &d, &n) != 0) return false;
    bool t = n < 4 * 1024 * 1024;
    for (size_t i = 0; t && i < n && i < 4096; i++) if (d[i] == 0) t = false;
    free(d);
    return t;
}

static int file_icon(const dirent_t *e) {
    if (e->dir) return ICON_FOLDER;
    if (ext_in(e->name, IMAGE_EXT) || str_ends(e->name, ".webp") || str_ends(e->name, ".svg")) return ICON_IMAGE;
    return ICON_FILE;
}

static void file_url(char *out, size_t n, const char *path) {
    size_t k = (size_t)snprintf(out, n, "file://");
    for (const unsigned char *p = (const unsigned char *)path; *p && k + 4 < n; p++) {
        if (*p <= ' ' || *p == '#' || *p == '%' || *p == '?' || *p >= 0x80) k += (size_t)snprintf(out + k, n - k, "%%%02X", *p);
        else out[k++] = (char)*p;
    }
    out[k] = 0;
}

static void extract(const char *path);

void open_path(const char *path) {
    bool dir = false;
    if (!plat_exists(path, &dir)) { wm_toast("Ce fichier n'existe plus."); return; }
    if (dir) { app_files_open(path); return; }
    if (ext_in(path, TEXT_EXT)) { app_notes_open(path); return; }
    if (ext_in(path, IMAGE_EXT)) { app_image_open(path); return; }
    if (ext_in(path, ARCHIVE_EXT)) { extract(path); return; }
    if (ext_in(path, WEB_EXT)) {
        if (plat_browser_state(NULL, 0, NULL) != BR_READY) {
            dlg_message("Fichiers", "Ce fichier s'ouvre avec Chromium, qui n'est pas encore installé : il s'installe dès que TitanOs a Internet.");
            return;
        }
        char url[700];
        file_url(url, sizeof url, path);
        app_browser_open();
        plat_browser_open_url(url);
        return;
    }
    if (looks_like_text(path)) { app_notes_open(path); return; }
    dlg_message("Fichiers", "TitanOs ne sait pas ouvrir ce type de fichier. Les documents PDF, vidéos, musiques et pages web s'ouvrent dans Chromium.");
}

/* archives : extraites dans un dossier du même nom, à côté */
static void extract(const char *path) {
    char dir[512];
    strlcpy(dir, path, sizeof dir);
    static const char *const strip[] = { ".tar.gz", ".tar.xz", ".tar.bz2", ".tgz", ".txz", ".tbz2", ".zip", ".tar", ".gz", NULL };
    for (int i = 0; strip[i]; i++)
        if (str_ends(dir, strip[i])) { dir[strlen(dir) - strlen(strip[i])] = 0; break; }
    plat_mkdir(dir);
    static char cmd[1200];
    if (str_ends(path, ".zip")) snprintf(cmd, sizeof cmd, "cd \"%s\" && /bin/busybox unzip -o -q \"%s\"", dir, path);
    else if (str_ends(path, ".gz") && !str_ends(path, ".tar.gz")) snprintf(cmd, sizeof cmd, "/bin/busybox gunzip -c \"%s\" > \"%s\"/\"$(basename \"%s\" .gz)\"", path, dir, path);
    else snprintf(cmd, sizeof cmd, "cd \"%s\" && /bin/busybox tar -xf \"%s\"", dir, path);
    static const char *argv[] = { "/bin/busybox", "sh", "-c", NULL, NULL };
    argv[3] = cmd;
    if (plat_spawn(argv) >= 0) {
        const char *b = strrchr(dir, '/');
        char msg[200];
        snprintf(msg, sizeof msg, "Extraction dans le dossier « %s »…", b ? b + 1 : dir);
        wm_toast(msg);
    } else wm_toast("Extraction impossible.");
}

static void files_open_sel(window_t *w, files_t *f) {
    if (f->sel < 0 || f->sel >= f->n) return;
    dirent_t *e = &f->ent[f->sel];
    char p[512];
    path_join(p, sizeof p, f->path, e->name);
    if (e->dir) files_go(w, f, p);
    else open_path(p);
}

/* ---- actions avec dialogue ---- */
static window_t *cur_win;    /* fenêtre concernée par le dialogue en cours */

static void do_mkdir(const char *name, void *ctx) {
    files_t *f = ctx;
    if (!name[0] || strchr(name, '/')) return;
    char p[512];
    path_join(p, sizeof p, f->path, name);
    if (plat_mkdir(p) != 0) wm_toast("Impossible de créer le dossier.");
    if (cur_win && cur_win->used) files_load(cur_win, f);
}

static void do_mkfile(const char *name, void *ctx) {
    files_t *f = ctx;
    if (!name[0] || strchr(name, '/')) return;
    char p[512];
    path_join(p, sizeof p, f->path, name);
    if (plat_exists(p, NULL)) { wm_toast("Ce nom existe déjà."); return; }
    if (plat_writefile(p, "", 0) != 0) wm_toast("Impossible de créer le fichier.");
    if (cur_win && cur_win->used) files_load(cur_win, f);
}

static void do_rename(const char *name, void *ctx) {
    files_t *f = ctx;
    if (!name[0] || strchr(name, '/') || f->sel < 0) return;
    char a[512], b[512];
    path_join(a, sizeof a, f->path, f->ent[f->sel].name);
    path_join(b, sizeof b, f->path, name);
    if (plat_rename(a, b) != 0) wm_toast("Impossible de renommer.");
    if (cur_win && cur_win->used) files_load(cur_win, f);
}

static void do_delete(bool ok, void *ctx) {
    files_t *f = ctx;
    if (!ok || f->sel < 0) return;
    char p[512];
    path_join(p, sizeof p, f->path, f->ent[f->sel].name);
    if (plat_remove(p) != 0) wm_toast("Impossible de supprimer.");
    else wm_toast("Supprimé.");
    if (cur_win && cur_win->used) files_load(cur_win, f);
}

static void do_duplicate(window_t *w, files_t *f) {
    if (f->sel < 0) return;
    dirent_t *e = &f->ent[f->sel];
    if (e->dir) { wm_toast("La copie de dossiers n'est pas encore gérée."); return; }
    char a[512], b[512], name[300];
    path_join(a, sizeof a, f->path, e->name);
    const char *dot = strrchr(e->name, '.');
    for (int k = 2; k < 100; k++) {
        if (dot && dot != e->name) snprintf(name, sizeof name, "%.*s (%d)%s", (int)(dot - e->name), e->name, k, dot);
        else snprintf(name, sizeof name, "%s (%d)", e->name, k);
        path_join(b, sizeof b, f->path, name);
        if (!plat_exists(b, NULL)) break;
    }
    if (plat_copy(a, b) != 0) wm_toast("Impossible de dupliquer.");
    files_load(w, f);
}

static void ask_rename(window_t *w, files_t *f) {
    if (f->sel < 0) return;
    cur_win = w;
    dlg_input("Renommer", "Nouveau nom", f->ent[f->sel].name, do_rename, f);
}

static void ask_delete(window_t *w, files_t *f) {
    if (f->sel < 0) return;
    cur_win = w;
    char msg[400];
    snprintf(msg, sizeof msg, "Supprimer définitivement « %s »%s ?", f->ent[f->sel].name,
             f->ent[f->sel].dir ? " et tout son contenu" : "");
    dlg_confirm("Supprimer", msg, "Supprimer", do_delete, f);
}

/* menu contextuel : ouvrir d'abord, puis modifier, puis créer */
enum { C_OPEN, C_NOTES, C_DESK, C_RENAME, C_DUP, C_DEL, C_MKDIR, C_MKFILE, C_SEP = -1 };
static const char *const ctx_file[] = { "Ouvrir", "Ouvrir avec le Bloc-notes", "Ajouter au bureau", "-", "Renommer", "Dupliquer",
                                        "Supprimer", "-", "Nouveau dossier", "Nouveau fichier" };
static const signed char act_file[] = { C_OPEN, C_NOTES, C_DESK, C_SEP, C_RENAME, C_DUP, C_DEL, C_SEP, C_MKDIR, C_MKFILE };
static const char *const ctx_dir[] = { "Ouvrir", "Ajouter au bureau", "-", "Renommer", "Dupliquer", "Supprimer", "-",
                                       "Nouveau dossier", "Nouveau fichier" };
static const signed char act_dir[] = { C_OPEN, C_DESK, C_SEP, C_RENAME, C_DUP, C_DEL, C_SEP, C_MKDIR, C_MKFILE };
static const char *const ctx_empty[] = { "Nouveau dossier", "Nouveau fichier" };
static const signed char act_empty[] = { C_MKDIR, C_MKFILE };
static const signed char *ctx_act;
static int ctx_nact;

static void ctx_cb(int i, void *ctx) {
    window_t *w = ctx;
    if (!w->used || i < 0 || i >= ctx_nact) return;
    files_t *f = w->data;
    cur_win = w;
    bool has = f->sel >= 0 && f->sel < f->n;
    char p[512];
    if (has) path_join(p, sizeof p, f->path, f->ent[f->sel].name);
    switch (ctx_act[i]) {
    case C_OPEN: files_open_sel(w, f); break;
    case C_NOTES: if (has && !f->ent[f->sel].dir) app_notes_open(p); break;
    case C_DESK: if (has) wm_desk_add_path(p); break;
    case C_RENAME: ask_rename(w, f); break;
    case C_DUP: do_duplicate(w, f); break;
    case C_DEL: ask_delete(w, f); break;
    case C_MKDIR: dlg_input("Nouveau dossier", "Nom du dossier", "Nouveau dossier", do_mkdir, f); break;
    case C_MKFILE: dlg_input("Nouveau fichier", "Nom du fichier", "Nouveau texte.txt", do_mkfile, f); break;
    }
}

/* ---- dessin ---- */
static rect_t bar_btn(canvas_t *c, int i) {     /* 0 parent, 1 nouveau dossier, 2 nouveau fichier */
    if (i == 0) return R(SIDE_W + 12, 10, 36, 36);
    return R(c->w - 16 - (3 - i) * 150 + 10, 10, 140, 36);
}
static rect_t list_rect(canvas_t *c) { return R(SIDE_W, BAR_H, c->w - SIDE_W, c->h - BAR_H - 30); }

static void files_draw(window_t *w, canvas_t *c) {
    files_t *f = w->data;
    /* barre latérale */
    gfx_fill(c, R(0, 0, SIDE_W, c->h), 0x22000000);
    gfx_fill(c, R(SIDE_W - 1, 0, 1, c->h), C_LINE);
    text_draw(c, &font_small, 20, 14, "EMPLACEMENTS", C_MUTED);
    for (int i = 0; i < countof(place_names); i++) {
        char p[512];
        place_path(i, p, sizeof p);
        rect_t r = place_rect(i);
        bool cur = !strcmp(p, f->path);
        if (cur) gfx_rrect(c, r.x, r.y, r.w, r.h, 10, (accent() & 0xFFFFFF) | 0xA0000000);
        else if (f->side_hover == i) gfx_rrect(c, r.x, r.y, r.w, r.h, 10, 0x1EFFFFFF);
        draw_icon(c, place_icons[i], r.x + 10, r.y + 7, 20);
        text_draw(c, &font_body, r.x + 40, r.y + 8, place_names[i], C_TEXT);
    }
    if (f->nvols) text_draw(c, &font_small, 20, vol_rect(0).y - 22, "PÉRIPHÉRIQUES", C_MUTED);
    for (int i = 0; i < f->nvols; i++) {
        rect_t r = vol_rect(i);
        bool cur = str_starts(f->path, f->vols[i].path);
        if (cur) gfx_rrect(c, r.x, r.y, r.w, r.h, 10, (accent() & 0xFFFFFF) | 0xA0000000);
        else if (f->side_hover == 100 + i) gfx_rrect(c, r.x, r.y, r.w, r.h, 10, 0x1EFFFFFF);
        draw_icon(c, ICON_USB, r.x + 10, r.y + 7, 20);
        text_fit(c, &font_body, r.x + 40, r.y + 8, r.w - 80, f->vols[i].name, C_TEXT);
        rect_t e = eject_rect(i);
        if (f->side_hover == 200 + i) gfx_circle(c, e.x + 13, e.y + 13, 13, 0x30FFFFFF);
        gfx_tri(c, e.x + 13, e.y + 6, e.x + 6, e.y + 15, e.x + 20, e.y + 15, 0xFFE2E8F0, 0xFFE2E8F0);   /* symbole Éjecter */
        gfx_fill(c, R(e.x + 6, e.y + 18, 14, 2), 0xFFE2E8F0);
    }
    char st[96];
    snprintf(st, sizeof st, "Stockage : %s", plat_storage());
    text_fit(c, &font_small, 14, c->h - 30, SIDE_W - 24, st, C_MUTED);
    /* barre d'outils */
    rect_t b0 = bar_btn(c, 0);
    if (f->bar_hover == 0) gfx_rrect(c, b0.x, b0.y, b0.w, b0.h, 18, 0x26FFFFFF);
    text_center(c, &font_title, b0, "←", C_TEXT);
    text_fit(c, &font_bold, SIDE_W + 58, 19, bar_btn(c, 1).x - SIDE_W - 70, f->path, C_TEXT);
    ui_button(c, bar_btn(c, 1), "Nouveau dossier", BTN_NORMAL, f->bar_hover == 1);
    ui_button(c, bar_btn(c, 2), "Nouveau fichier", BTN_NORMAL, f->bar_hover == 2);
    gfx_fill(c, R(SIDE_W, BAR_H - 1, c->w - SIDE_W, 1), C_LINE);
    /* liste */
    rect_t L = list_rect(c);
    rect_t old = c->clip;
    canvas_clip(c, L);
    int vis = L.h / ROW_H;
    for (int i = f->scroll; i < f->n && i < f->scroll + vis + 1; i++) {
        dirent_t *e = &f->ent[i];
        rect_t r = R(L.x + 8, L.y + 6 + (i - f->scroll) * ROW_H, L.w - 16, ROW_H - 2);
        if (i == f->sel) gfx_rrect(c, r.x, r.y, r.w, r.h, 10, (accent() & 0xFFFFFF) | 0x90000000);
        else if (i == f->hover) gfx_rrect(c, r.x, r.y, r.w, r.h, 10, 0x18FFFFFF);
        draw_icon(c, file_icon(e), r.x + 10, r.y + 6, 22);
        text_fit(c, &font_body, r.x + 44, r.y + 9, r.w - 170, e->name, C_TEXT);
        char sz[32];
        if (e->dir) strlcpy(sz, "Dossier", sizeof sz);
        else fmt_size(sz, sizeof sz, e->size);
        text_draw(c, &font_small, r.x + r.w - 14 - text_width(&font_small, sz), r.y + 11, sz, C_MUTED);
    }
    if (!f->n) text_center(c, &font_body, R(L.x, L.y + 40, L.w, 30), "Ce dossier est vide", C_MUTED);
    ui_scrollbar(c, R(c->w - 8, L.y + 4, 4, L.h - 8), f->n, vis, f->scroll);
    c->clip = old;
    snprintf(st, sizeof st, "%d élément%s", f->n, f->n > 1 ? "s" : "");
    text_draw(c, &font_small, SIDE_W + 16, c->h - 24, st, C_MUTED);
    text_draw(c, &font_small, c->w - 16 - text_width(&font_small, "F2 renommer · Suppr effacer"), c->h - 24,
              "F2 renommer · Suppr effacer", C_MUTED);
}

static void ensure_visible(files_t *f, int vis) {
    if (f->sel < f->scroll) f->scroll = f->sel;
    if (f->sel >= f->scroll + vis) f->scroll = f->sel - vis + 1;
    if (f->scroll < 0) f->scroll = 0;
}

static void files_event(window_t *w, const event_t *e) {
    files_t *f = w->data;
    canvas_t *c = &w->surf;
    rect_t L = list_rect(c);
    int vis = MAX(1, L.h / ROW_H);
    int row_at = (e->type == EV_MOTION || e->type == EV_BUTTON) && rect_has(L, e->x, e->y) ? f->scroll + (e->y - L.y - 6) / ROW_H : -1;
    if (row_at >= f->n) row_at = -1;
    if (e->type == EV_MOTION) {
        int sh = -1, bh = -1;
        for (int i = 0; i < countof(place_names); i++) if (rect_has(place_rect(i), e->x, e->y)) sh = i;
        for (int i = 0; i < f->nvols; i++) if (rect_has(vol_rect(i), e->x, e->y)) sh = rect_has(eject_rect(i), e->x, e->y) ? 200 + i : 100 + i;
        for (int i = 0; i < 3; i++) if (rect_has(bar_btn(c, i), e->x, e->y)) bh = i;
        if (sh != f->side_hover || bh != f->bar_hover || row_at != f->hover) {
            f->side_hover = sh; f->bar_hover = bh; f->hover = row_at;
            wm_invalidate(w);
        }
        return;
    }
    if (e->type == EV_WHEEL) {
        f->scroll = CLAMP(f->scroll + e->dy * 3, 0, MAX(0, f->n - vis));
        wm_invalidate(w);
        return;
    }
    cur_win = w;
    if (e->type == EV_BUTTON && e->down) {
        if ((f->side_hover >= 0 || f->bar_hover >= 0) && e->button == BTN_LEFT) sound_play(SFX_CLICK);
        if (f->side_hover >= 200 && f->side_hover < 200 + f->nvols && e->button == BTN_LEFT) {
            volume_t *v = &f->vols[f->side_hover - 200];
            char msg[96];
            if (plat_eject(v->path)) { snprintf(msg, sizeof msg, "« %s » peut être retiré.", v->name); if (str_starts(f->path, v->path)) files_go(w, f, plat_home()); }
            else snprintf(msg, sizeof msg, "« %s » est encore utilisé : fermez ses fichiers.", v->name);
            wm_toast(msg);
            f->nvols = plat_volumes(f->vols, 6);
            f->side_hover = -1;
            wm_invalidate(w);
            return;
        }
        if (f->side_hover >= 100 && f->side_hover < 100 + f->nvols && e->button == BTN_LEFT) { files_go(w, f, f->vols[f->side_hover - 100].path); return; }
        if (f->side_hover >= 0 && f->side_hover < countof(place_names) && e->button == BTN_LEFT) {
            char p[512];
            place_path(f->side_hover, p, sizeof p);
            if (!plat_exists(p, NULL)) plat_mkdir(p);
            files_go(w, f, p);
            return;
        }
        if (f->bar_hover == 0) { files_up(w, f); return; }
        if (f->bar_hover == 1) { dlg_input("Nouveau dossier", "Nom du dossier", "Nouveau dossier", do_mkdir, f); return; }
        if (f->bar_hover == 2) { dlg_input("Nouveau fichier", "Nom du fichier", "Nouveau texte.txt", do_mkfile, f); return; }
        if (rect_has(L, e->x, e->y)) {
            f->sel = row_at;
            wm_invalidate(w);
            if (e->button == BTN_LEFT && e->dx && row_at >= 0) files_open_sel(w, f);   /* double-clic */
            if (e->button == BTN_RIGHT) {
                rect_t cl = wm_client(w);
                const char *const *items = ctx_empty;
                if (row_at < 0) { ctx_act = act_empty; ctx_nact = countof(act_empty); }
                else if (f->ent[row_at].dir) { items = ctx_dir; ctx_act = act_dir; ctx_nact = countof(act_dir); }
                else { items = ctx_file; ctx_act = act_file; ctx_nact = countof(act_file); }
                wm_menu(cl.x + e->x, cl.y + e->y, items, ctx_nact, ctx_cb, w);
            }
        }
        return;
    }
    if (e->type != EV_KEY || !e->down) return;
    switch (e->key) {
    case K_UP: if (f->sel > 0) f->sel--; break;
    case K_DOWN: if (f->sel < f->n - 1) f->sel++; break;
    case K_HOME: f->sel = f->n ? 0 : -1; break;
    case K_END: f->sel = f->n - 1; break;
    case K_PGUP: f->sel = MAX(0, f->sel - vis); break;
    case K_PGDN: f->sel = MIN(f->n - 1, f->sel + vis); break;
    case K_ENTER: files_open_sel(w, f); return;
    case K_BACKSPACE: files_up(w, f); return;
    case K_DELETE: ask_delete(w, f); return;
    case K_F2: ask_rename(w, f); return;
    case K_F5: files_load(w, f); return;
    case K_CHAR:
        if ((e->mods & MOD_CTRL) && (e->ch == 'n' || e->ch == 'N')) dlg_input("Nouveau dossier", "Nom du dossier", "Nouveau dossier", do_mkdir, f);
        else if ((e->mods & MOD_CTRL) && (e->ch == 'd' || e->ch == 'D')) do_duplicate(w, f);
        return;
    default: return;
    }
    ensure_visible(f, vis);
    wm_invalidate(w);
}

static void files_tick(window_t *w, uint64_t now) {
    files_t *f = w->data;
    if (now < f->next_vols) return;
    f->next_vols = now + 2000;
    volume_t v[6];
    int n = plat_volumes(v, 6);
    if (n != f->nvols || memcmp(v, f->vols, sizeof(volume_t) * (size_t)n)) {
        memcpy(f->vols, v, sizeof(volume_t) * (size_t)n);
        f->nvols = n;
        wm_invalidate(w);
    }
}

static void files_close(window_t *w) {
    files_t *f = w->data;
    if (cur_win == w) cur_win = NULL;
    free(f->ent);
    free(f);
}

void app_files_open(const char *path) {
    files_t *f = calloc(1, sizeof *f);
    if (!f) return;
    f->hover = f->side_hover = f->bar_hover = -1;
    window_t *w = wm_open("Fichiers", ICON_FILES, 820, 520, WF_RESIZABLE);
    if (!w) { free(f); return; }
    w->minw = 560;
    w->data = f;
    w->draw = files_draw;
    w->event = files_event;
    w->on_close = files_close;
    w->tick = files_tick;
    files_go(w, f, path ? path : plat_home());
}
