/* TitanOs - Mises à jour : version installée, recherche des mises à jour sur GitHub,
 * et installation de TitanOs sur le disque interne de l'ordinateur. */
#include "desk.h"

#define MAX_DISKS 8
enum { H_NONE = -1, H_CHECK = -2, H_INSTALL = -3 };

typedef struct {
    int hover, sel, ndisks;
    disk_info_t disks[MAX_DISKS];
    bool can_install;
    uint64_t next;
} upd_t;

static rect_t check_rect(canvas_t *c) { return R(c->w - 16 - 210, 64, 210, 38); }
static rect_t disk_row(canvas_t *c, int i) { return R(28, 262 + i * 52, c->w - 56, 46); }
static rect_t install_rect(canvas_t *c) { return R(c->w - 16 - 260, c->h - 54, 260, 40); }

static void fmt_bytes(char *b, size_t n, uint64_t v) {
    if (v >= (1000ull << 30)) snprintf(b, n, "%d,%d To", (int)(v / 1000000000000ull), (int)(v / 100000000000ull % 10));
    else snprintf(b, n, "%d Go", (int)(v / 1000000000ull));
}

static void progress_bar(canvas_t *c, rect_t r, int percent) {
    gfx_rrect(c, r.x, r.y, r.w, r.h, r.h / 2.0f, 0x30FFFFFF);
    if (percent > 0) gfx_rrect(c, r.x, r.y, MAX((float)r.h, r.w * MIN(percent, 100) / 100.0f), r.h, r.h / 2.0f, accent());
}

static void upd_draw(window_t *w, canvas_t *c) {
    upd_t *s = w->data;
    draw_logo(c, 44, 18, 40, 255);
    text_draw(c, &font_title, 80, 18, "TitanOs " TITANOS_VERSION, C_TEXT);
    char msg[160];
    int pc = -1, st = plat_update_state(msg, sizeof msg, &pc);
    if (st == UPD_IDLE) strlcpy(msg, "Les mises à jour se téléchargent et s'installent toutes seules.", sizeof msg);
    uint32_t col = st == UPD_ERROR ? 0xFFFCA5A5 : st == UPD_DONE || st == UPD_UPTODATE ? 0xFF86EFAC : C_TEXT2;
    text_draw(c, &font_small, 80, 46, msg, col);
    bool busy = st == UPD_CHECKING || st == UPD_DOWNLOADING || st == UPD_INSTALLING;
    if (!busy) ui_button(c, check_rect(c), "Rechercher maintenant", BTN_NORMAL, s->hover == H_CHECK);
    if (st == UPD_INSTALLING && pc >= 0) progress_bar(c, R(80, 74, c->w - 80 - 250, 8), pc);
    text_draw(c, &font_small, 28, 118, "Source : github.com/Titangp1/TitanOs (versions publiées)", C_MUTED);

    /* installation sur le disque */
    gfx_fill(c, R(16, 150, c->w - 32, 1), 0x20FFFFFF);
    text_draw(c, &font_bold, 28, 168, "Installer TitanOs sur cet ordinateur", C_TEXT);
    int ipc = -1, ist = plat_install_state(msg, sizeof msg, &ipc);
    if (!s->can_install) {
        text_draw(c, &font_body, 28, 198, "TitanOs fonctionne déjà depuis le disque de cet ordinateur.", C_TEXT2);
        return;
    }
    text_draw(c, &font_body, 28, 196, "Le disque choisi est entièrement effacé (Windows, fichiers…) puis TitanOs", C_TEXT2);
    text_draw(c, &font_body, 28, 218, "y est copié avec vos fichiers. Ensuite, l'ordinateur démarre sans la clé.", C_TEXT2);
    if (!s->ndisks) text_draw(c, &font_body, 28, 262, "Aucun disque interne utilisable n'a été trouvé.", C_MUTED);
    for (int i = 0; i < s->ndisks; i++) {
        rect_t r = disk_row(c, i);
        bool sel = i == s->sel;
        gfx_rrect(c, r.x, r.y, r.w, r.h, 12, sel ? (accent() & 0xFFFFFF) | 0x60000000 : i == s->hover ? 0x1EFFFFFF : 0x10FFFFFF);
        draw_icon(c, ICON_DISK, r.x + 12, r.y + 11, 24);
        char sz[32], line[96];
        fmt_bytes(sz, sizeof sz, s->disks[i].bytes);
        snprintf(line, sizeof line, "%s · %s", s->disks[i].model, sz);
        text_fit(c, &font_bold, r.x + 48, r.y + 13, r.w - 160, line, C_TEXT);
        text_draw(c, &font_small, r.x + r.w - 90, r.y + 15, s->disks[i].dev, C_MUTED);
    }
    if (ist == UPD_INSTALLING) {
        text_draw(c, &font_small, 28, c->h - 64, msg, C_TEXT2);
        progress_bar(c, R(28, c->h - 38, c->w - 56, 10), ipc);
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

static void upd_event(window_t *w, const event_t *e) {
    upd_t *s = w->data;
    canvas_t *c = &w->surf;
    if (e->type == EV_MOTION) {
        int h = H_NONE;
        if (rect_has(check_rect(c), e->x, e->y)) h = H_CHECK;
        if (s->can_install) {
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
    if (s->hover == H_CHECK) plat_update_check(false);
    else if (s->hover >= 0) { if (ist != UPD_INSTALLING) s->sel = s->hover; }
    else if (s->hover == H_INSTALL && ist != UPD_INSTALLING && ist != UPD_DONE) {
        if (s->sel < 0) { wm_toast("Choisissez d'abord le disque."); return; }
        disk_info_t *d = &s->disks[s->sel];
        char sz[32], text[300];
        fmt_bytes(sz, sizeof sz, d->bytes);
        snprintf(text, sizeof text, "Tout le contenu du disque « %s » (%s) sera effacé définitivement, "
                 "y compris Windows et vos fichiers. TitanOs y sera installé. Continuer ?", d->model, sz);
        dlg_confirm("Installer TitanOs", text, "Effacer et installer", confirm_cb, w);
    }
    wm_invalidate(w);
}

static void upd_tick(window_t *w, uint64_t now) {
    upd_t *s = w->data;
    if (now < s->next) return;
    s->next = now + 400;                 /* l'état vient d'un autre processus : on le relit souvent */
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
    s->can_install = plat_install_possible();
    if (s->can_install) s->ndisks = plat_install_disks(s->disks, MAX_DISKS);
    if (s->ndisks == 1) s->sel = 0;
    window_t *w = wm_open("Mises à jour", ICON_UPDATE, 680, 330 + MAX(1, s->ndisks) * 52, WF_SINGLE);
    if (!w) { free(s); return; }
    w->data = s;
    w->draw = upd_draw;
    w->event = upd_event;
    w->tick = upd_tick;
    w->on_close = upd_close;
}
