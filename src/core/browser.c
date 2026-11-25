/* TitanOs - Chromium : fenêtre qui affiche le navigateur et lui transmet souris et clavier
 * (le serveur X et Chromium tournent à côté, voir src/linux/browser.c) */
#include "desk.h"

typedef struct {
    bool running;
    int hover;
    uint64_t next;
    int w, h;                    /* taille de l'écran X : celle de la fenêtre agrandie */
    int sent_w, sent_h;          /* dernière taille donnée à Chromium */
    ui_progress_t prog;
    uint64_t next_cur, next_manage;
} br_t;

static rect_t install_btn(canvas_t *c) { return R(c->w / 2 - 150, c->h / 2 + 40, 300, 42); }

static void br_draw(window_t *w, canvas_t *c) {
    br_t *b = w->data;
    if (b->running) return;                      /* l'image de Chromium est déjà dans la surface */
    gfx_fill(c, R(0, 0, c->w, c->h), 0x30000000);
    char msg[160];
    int pc = -1, st = plat_browser_state(msg, sizeof msg, &pc);
    draw_icon(c, ICON_BROWSER, c->w / 2 - 40, c->h / 2 - 150, 80);
    text_center(c, &font_h2, R(0, c->h / 2 - 54, c->w, 32), "Chromium", C_TEXT);
    if (st == BR_READY) {
        text_center(c, &font_body, R(0, c->h / 2 - 12, c->w, 22), "Démarrage du navigateur…", C_TEXT2);
    } else if (st == BR_INSTALLING) {
        text_center(c, &font_body, R(0, c->h / 2 - 12, c->w, 22), msg, C_TEXT2);
        ui_progress(c, R(c->w / 2 - 220, c->h / 2 + 22, 440, 10), &b->prog, pc);
        text_center(c, &font_small, R(0, c->h / 2 + 46, c->w, 20), "Vous pouvez continuer à utiliser TitanOs pendant ce temps.", C_MUTED);
    } else {
        text_center(c, &font_body, R(0, c->h / 2 - 12, c->w, 22),
                    st == BR_ERROR ? msg : "Le navigateur web, base libre de Google Chrome.", st == BR_ERROR ? 0xFFFCA5A5 : C_TEXT2);
        text_center(c, &font_small, R(0, c->h / 2 + 12, c->w, 20), "Environ 370 Mo à télécharger, installé une fois pour toutes.", C_MUTED);
        ui_button(c, install_btn(c), st == BR_ERROR ? "Réessayer" : "Installer Chromium", BTN_ACCENT, b->hover);
    }
}

/* boutons TitanOs -> boutons X11 (1 gauche, 2 milieu, 3 droit, 4/5 molette) */
static void br_event(window_t *w, const event_t *e) {
    br_t *b = w->data;
    canvas_t *c = &w->surf;
    if (!b->running) {
        if (e->type == EV_MOTION) {
            bool h = rect_has(install_btn(c), e->x, e->y);
            if (h != b->hover) { b->hover = h; wm_invalidate(w); }
        } else if (e->type == EV_BUTTON && e->down && b->hover && plat_browser_state(NULL, 0, NULL) != BR_INSTALLING) {
            sound_play(SFX_CLICK);
            plat_browser_install();
            wm_invalidate(w);
        }
        return;
    }
    if (e->type == EV_MOTION) {
        plat_browser_input(6, 0, e->x, e->y);
        uint64_t now = plat_ms();
        if (now >= b->next_cur) {
            b->next_cur = now + 50;
            if (plat_xapp_cursor(BROWSER_XAPP, &w->cursor_img, &w->cursor_hx, &w->cursor_hy)) { w->cursor = CURSOR_APP; w->cursor_ver++; }
        }
    }
    else if (e->type == EV_BUTTON) plat_browser_input(e->down ? 4 : 5, e->button == BTN_RIGHT ? 3 : e->button == BTN_MIDDLE ? 2 : 1, e->x, e->y);
    else if (e->type == EV_WHEEL) {
        int btn = e->dy > 0 ? 5 : 4;
        for (int i = 0; i < (e->dy > 0 ? e->dy : -e->dy); i++) { plat_browser_input(4, btn, -1, -1); plat_browser_input(5, btn, -1, -1); }
    }
}

/* touches brutes (codes du clavier) : le serveur X applique lui-même la disposition */
static void br_raw_key(window_t *w, int code, bool down) {
    br_t *b = w->data;
    if (b->running) plat_browser_input(down ? 2 : 3, code + 8, -1, -1);
}

static void br_tick(window_t *w, uint64_t now) {
    br_t *b = w->data;
    if (!b->running) {
        int st = plat_browser_state(NULL, 0, NULL);
        w->cursor = st == BR_READY ? CURSOR_BUSY : CURSOR_ARROW;          /* démarrage : pointeur « occupé » */
        if (st == BR_INSTALLING) wm_invalidate(w);                        /* barre animée */
        if (now < b->next) return;
        b->next = now + 300;
        if (st == BR_READY && plat_browser_start(b->w, b->h)) {
            if (plat_browser_frame(w->surf.px, w->surf.stride, w->surf.w, w->surf.h)) { b->running = true; w->dirty = false; w->cursor = CURSOR_ARROW; }
        }
        wm_invalidate(w);
        return;
    }
    if (now < b->next) return;
    b->next = now + 33;                               /* environ 30 images par seconde */
    if (w->surf.w != b->sent_w || w->surf.h != b->sent_h) {   /* fenêtre agrandie ou redimensionnée */
        b->sent_w = w->surf.w;
        b->sent_h = w->surf.h;
        plat_browser_resize(b->sent_w, b->sent_h);
    } else if (now >= b->next_manage) {                /* nouvelles fenêtres (dialogues) : placées */
        b->next_manage = now + 500;
        plat_xapp_manage(BROWSER_XAPP);
    }
    if (!plat_browser_running()) { b->running = false; plat_browser_stop(); wm_close(w); return; }
    if (plat_browser_frame(w->surf.px, w->surf.stride, w->surf.w, w->surf.h)) wm_damage_client(w);
}

static void br_close(window_t *w) {
    plat_browser_stop();
    canvas_free(&w->cursor_img);
    free(w->data);
}

void app_browser_open(void) {
    window_t *f = wm_find(ICON_BROWSER);
    if (f) { wm_focus(f); return; }
    br_t *b = calloc(1, sizeof *b);
    if (!b) return;
    /* l'écran X a la taille de la fenêtre agrandie ; la fenêtre s'ouvre en grand */
    b->w = scr_w - 16;
    b->h = scr_h - DOCK_H_PUBLIC - 30 - TITLE_H;
    window_t *w = wm_open("Chromium", ICON_BROWSER, 1100, 700, WF_SINGLE | WF_RESIZABLE);
    if (!w) { free(b); return; }
    w->minw = 480;
    w->minh = 320;
    wm_maximize(w);
    b->sent_w = w->surf.w;
    b->sent_h = w->surf.h;
    w->data = b;
    w->draw = br_draw;
    w->event = br_event;
    w->tick = br_tick;
    w->raw_key = br_raw_key;
    w->on_close = br_close;
}

/* installation automatique : Chromium est installé dès que TitanOs a Internet */
void browser_autoinstall(bool online) {
    static bool tried;
    if (tried || !online) return;
    if (plat_browser_state(NULL, 0, NULL) != BR_NONE) return;
    tried = true;
    if (plat_browser_install()) wm_toast("Installation de Chromium en arrière-plan…");
}
