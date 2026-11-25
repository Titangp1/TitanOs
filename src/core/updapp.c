/* TitanOs - Mises à jour : version installée, recherche des mises à jour sur GitHub,
 * et installation de TitanOs sur le disque interne de l'ordinateur. */
#include "desk.h"

#define MAX_DISKS 8
enum { H_NONE = -1, H_CHECK = -2, H_INSTALL = -3, H_OTHER = -4, H_FILE = -5 };
#define OY 140                      /* hauteur de la partie « par clé USB » */

typedef struct {
    int hover, sel, ndisks;
    disk_info_t disks[MAX_DISKS];
    bool can_install;
    uint64_t next;
    bool other;                     /* un autre TitanOs (disque interne) à mettre à jour depuis cette clé */
    char other_desc[96];
    int nfiles;                     /* paquets titanos-maj.tar.gz trouvés sur des clés */
    char files[2][160];
    ui_progress_t pu, pi;           /* barres : mise à jour, installation sur un disque */
    bool anim;
} upd_t;

static rect_t other_rect(canvas_t *c) { return R(c->w - 16 - 240, 196, 240, 38); }
static rect_t file_rect(canvas_t *c) { return R(c->w - 16 - 240, 244, 240, 38); }

static void refresh_usb(upd_t *s) {
    s->other = plat_other_titanos(s->other_desc, sizeof s->other_desc);
    s->nfiles = plat_update_files(s->files, 2);
}

static rect_t check_rect(canvas_t *c) { return R(c->w - 16 - 210, 64, 210, 38); }
static rect_t disk_row(canvas_t *c, int i) { return R(28, OY + 262 + i * 52, c->w - 56, 46); }
static rect_t install_rect(canvas_t *c) { return R(c->w - 16 - 260, c->h - 54, 260, 40); }

static void fmt_bytes(char *b, size_t n, uint64_t v) {
    if (v >= (1000ull << 30)) snprintf(b, n, "%d,%d To", (int)(v / 1000000000000ull), (int)(v / 100000000000ull % 10));
    else snprintf(b, n, "%d Go", (int)(v / 1000000000ull));
}

static void upd_draw(window_t *w, canvas_t *c) {
    upd_t *s = w->data;
    s->anim = false;
    draw_logo(c, 44, 18, 40, 255);
    text_draw(c, &font_title, 80, 18, "TitanOs " TITANOS_VERSION, C_TEXT);
    char msg[160];
    int pc = -1, st = plat_update_state(msg, sizeof msg, &pc);
    if (st == UPD_IDLE) strlcpy(msg, "Les mises à jour se téléchargent et s'installent toutes seules.", sizeof msg);
    uint32_t col = st == UPD_ERROR ? 0xFFFCA5A5 : st == UPD_DONE || st == UPD_UPTODATE ? 0xFF86EFAC : C_TEXT2;
    text_draw(c, &font_small, 80, 46, msg, col);
    bool busy = st == UPD_CHECKING || st == UPD_DOWNLOADING || st == UPD_INSTALLING;
    if (!busy) ui_button(c, check_rect(c), "Rechercher maintenant", BTN_NORMAL, s->hover == H_CHECK);
    if (busy) s->anim |= ui_progress(c, R(80, 76, c->w - 80 - 250, 8), &s->pu, st == UPD_CHECKING ? -1 : pc);
    else s->pu.started = false;
    text_draw(c, &font_small, 28, 118, "Source : github.com/Titangp1/TitanOs (versions publiées)", C_MUTED);

    /* mise à jour par clé USB, sans Internet */
    gfx_fill(c, R(16, 150, c->w - 32, 1), 0x20FFFFFF);
    text_draw(c, &font_bold, 28, 168, "Mettre à jour par clé USB", C_TEXT);
    if (s->other) {
        char t[200];
        snprintf(t, sizeof t, "TitanOs est aussi installé sur %s.", s->other_desc);
        text_fit(c, &font_body, 28, 197, c->w - 300, t, C_TEXT2);
        text_fit(c, &font_small, 28, 219, c->w - 300, "Il peut prendre la version de cette clé, en gardant vos fichiers.", C_MUTED);
        if (!busy) ui_button(c, other_rect(c), "Mettre à jour le disque", BTN_ACCENT, s->hover == H_OTHER);
    } else {
        text_fit(c, &font_body, 28, 197, c->w - 56, "Démarrez sur une clé TitanOs plus récente, ou posez titanos-maj.tar.gz sur une clé USB.", C_TEXT2);
        text_draw(c, &font_small, 28, 219, "Le fichier se trouve dans chaque version publiée sur GitHub.", C_MUTED);
    }
    if (s->nfiles) {
        char t[200];
        snprintf(t, sizeof t, "Paquet trouvé : %s", s->files[0]);
        text_fit(c, &font_body, 28, 253, c->w - 300, t, C_TEXT2);
        if (!busy) ui_button(c, file_rect(c), "Installer ce paquet", BTN_ACCENT, s->hover == H_FILE);
    }

    /* installation sur le disque */
    gfx_fill(c, R(16, OY + 150, c->w - 32, 1), 0x20FFFFFF);
    text_draw(c, &font_bold, 28, OY + 168, "Installer TitanOs sur un disque ou une clé USB", C_TEXT);
    int ipc = -1, ist = plat_install_state(msg, sizeof msg, &ipc);
    text_draw(c, &font_body, 28, OY + 196, "Sur un disque interne ou une clé USB : il est entièrement effacé, puis TitanOs", C_TEXT2);
    text_draw(c, &font_body, 28, OY + 218, "y est copié avec vos fichiers. Branchez la clé avant d'ouvrir cette fenêtre.", C_TEXT2);
    if (!s->ndisks) text_draw(c, &font_body, 28, OY + 262, "Aucun disque ni clé USB trouvé.", C_MUTED);
    for (int i = 0; i < s->ndisks; i++) {
        rect_t r = disk_row(c, i);
        bool sel = i == s->sel;
        gfx_rrect(c, r.x, r.y, r.w, r.h, 12, sel ? (accent() & 0xFFFFFF) | 0x60000000 : i == s->hover ? 0x1EFFFFFF : 0x10FFFFFF);
        draw_icon(c, s->disks[i].removable ? ICON_USB : ICON_DISK, r.x + 12, r.y + 11, 24);
        char sz[32], line[96];
        fmt_bytes(sz, sizeof sz, s->disks[i].bytes);
        snprintf(line, sizeof line, "%s · %s", s->disks[i].model, sz);
        text_fit(c, &font_bold, r.x + 48, r.y + 13, r.w - 160, line, C_TEXT);
        text_draw(c, &font_small, r.x + r.w - 90, r.y + 15, s->disks[i].dev, C_MUTED);
        if (s->disks[i].current) text_draw(c, &font_small, r.x + r.w - 300, r.y + 15, "TitanOs en cours", 0xFF86EFAC);
    }
    if (ist == UPD_INSTALLING) {
        text_draw(c, &font_small, 28, c->h - 64, msg, C_TEXT2);
        s->anim |= ui_progress(c, R(28, c->h - 38, c->w - 56, 10), &s->pi, ipc);
    } else {
        if (ist == UPD_DONE || ist == UPD_ERROR)
            text_fit(c, &font_small, 28, c->h - 42, c->w - 330, msg, ist == UPD_DONE ? 0xFF86EFAC : 0xFFFCA5A5);
        if (s->ndisks && ist != UPD_DONE)
            ui_button(c, install_rect(c), "Installer sur ce disque…", s->sel >= 0 ? BTN_DANGER : BTN_NORMAL, s->hover == H_INSTALL);
    }
}

static void confirm_cb(bool ok, void *ctx) {
    window_t *w = ctx;
    upd_t *s = w->data;
    if (!ok || s->sel < 0 || s->sel >= s->ndisks) return;
    if (!plat_install_start(s->disks[s->sel].dev)) wm_toast("Installation impossible pour le moment.");
    wm_invalidate(w);
}

static void other_cb(bool ok, void *ctx) {
    window_t *w = ctx;
    if (ok && !plat_update_other()) wm_toast("Mise à jour du disque impossible pour le moment.");
    if (w->used) wm_invalidate(w);
}

static void upd_event(window_t *w, const event_t *e) {
    upd_t *s = w->data;
    canvas_t *c = &w->surf;
    if (e->type == EV_MOTION) {
        int h = H_NONE;
        if (rect_has(check_rect(c), e->x, e->y)) h = H_CHECK;
        if (s->other && rect_has(other_rect(c), e->x, e->y)) h = H_OTHER;
        if (s->nfiles && rect_has(file_rect(c), e->x, e->y)) h = H_FILE;
        {
            for (int i = 0; i < s->ndisks; i++) if (rect_has(disk_row(c, i), e->x, e->y)) h = i;
            if (s->ndisks && rect_has(install_rect(c), e->x, e->y)) h = H_INSTALL;
        }
        if (h != s->hover) { s->hover = h; wm_invalidate(w); }
        return;
    }
    if (e->type != EV_BUTTON || !e->down || e->button != BTN_LEFT) return;
    if (s->hover == H_NONE) return;
    sound_play(SFX_CLICK);
    int ist = plat_install_state(NULL, 0, NULL);
    int ust = plat_update_state(NULL, 0, NULL);
    bool ubusy = ust == UPD_CHECKING || ust == UPD_DOWNLOADING || ust == UPD_INSTALLING;
    if (s->hover == H_CHECK) plat_update_check(false);
    else if (s->hover == H_OTHER && !ubusy) {
        char t[300];
        snprintf(t, sizeof t, "Le TitanOs installé sur %s va prendre la version de cette clé (" TITANOS_VERSION "). "
                 "Vos fichiers et vos applications sont gardés. Continuer ?", s->other_desc);
        dlg_confirm("Mettre à jour le disque", t, "Mettre à jour", other_cb, w);
    }
    else if (s->hover == H_FILE && !ubusy) { if (!plat_update_from_file(s->files[0])) wm_toast("Mise à jour impossible pour le moment."); }
    else if (s->hover >= 0) {
        if (s->disks[s->hover].current) { wm_toast("TitanOs fonctionne depuis ce disque : choisissez-en un autre."); return; }
        if (ist != UPD_INSTALLING) s->sel = s->hover;
    }
    else if (s->hover == H_INSTALL && ist != UPD_INSTALLING && ist != UPD_DONE) {
        if (s->sel < 0) { wm_toast("Choisissez d'abord le disque."); return; }
        disk_info_t *d = &s->disks[s->sel];
        char sz[32], text[300];
        fmt_bytes(sz, sizeof sz, d->bytes);
        snprintf(text, sizeof text, "Tout le contenu %s « %s » (%s) sera effacé définitivement. "
                 "TitanOs y sera installé avec vos fichiers. Continuer ?", d->removable ? "de la clé" : "du disque", d->model, sz);
        dlg_confirm("Installer TitanOs", text, "Effacer et installer", confirm_cb, w);
    }
    wm_invalidate(w);
}

static void upd_tick(window_t *w, uint64_t now) {
    upd_t *s = w->data;
    if (s->anim) wm_invalidate(w);                /* barre de progression animée */
    if (now < s->next) return;
    s->next = now + 400;                 /* l'état vient d'un autre processus : on le relit souvent */
    static uint64_t next_disks;
    if (now >= next_disks && plat_install_state(NULL, 0, NULL) != UPD_INSTALLING) {   /* clé branchée entre-temps */
        next_disks = now + 3000;
        char keep[16] = "";
        if (s->sel >= 0 && s->sel < s->ndisks) strlcpy(keep, s->disks[s->sel].dev, sizeof keep);
        s->ndisks = plat_install_disks(s->disks, MAX_DISKS);
        s->sel = -1;
        for (int i = 0; i < s->ndisks; i++) if (!strcmp(s->disks[i].dev, keep)) s->sel = i;
        refresh_usb(s);
    }
    wm_invalidate(w);
}

static void upd_close(window_t *w) { free(w->data); }

void app_update_open(void) {
    window_t *f = wm_find(ICON_UPDATE);
    if (f) { wm_focus(f); return; }
    upd_t *s = calloc(1, sizeof *s);
    if (!s) return;
    s->hover = H_NONE;
    s->sel = -1;
    s->can_install = true;                   /* toujours : vers un disque ou une clé USB */
    s->ndisks = plat_install_disks(s->disks, MAX_DISKS);
    for (int i = 0; i < s->ndisks && s->sel < 0; i++) if (!s->disks[i].current) s->sel = i;
    refresh_usb(s);
    window_t *w = wm_open("Mises à jour", ICON_UPDATE, 720, OY + 330 + MAX(2, s->ndisks) * 52, WF_SINGLE);
    if (!w) { free(s); return; }
    w->data = s;
    w->draw = upd_draw;
    w->event = upd_event;
    w->tick = upd_tick;
    w->on_close = upd_close;
}
