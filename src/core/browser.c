/* TitanOs - Google Chrome : fenêtre qui affiche le navigateur et lui transmet souris et
 * clavier (le serveur X et Chrome tournent à côté, voir src/linux/xbridge.c) */
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
    text_center(c, &font_h2, R(0, c->h / 2 - 54, c->w, 32), "Google Chrome", C_TEXT);
    if (st == BR_READY) {
        text_center(c, &font_body, R(0, c->h / 2 - 12, c->w, 22), "Démarrage du navigateur…", C_TEXT2);
    } else if (st == BR_INSTALLING) {
        text_center(c, &font_body, R(0, c->h / 2 - 12, c->w, 22), msg, C_TEXT2);
        ui_progress(c, R(c->w / 2 - 220, c->h / 2 + 22, 440, 10), &b->prog, pc);
        text_center(c, &font_small, R(0, c->h / 2 + 46, c->w, 20), "Vous pouvez continuer à utiliser TitanOs pendant ce temps.", C_MUTED);
    } else {
        text_center(c, &font_body, R(0, c->h / 2 - 12, c->w, 22),
                    st == BR_ERROR ? msg : "Google Chrome n'est pas installé.", st == BR_ERROR ? 0xFFFCA5A5 : C_TEXT2);
        text_center(c, &font_small, R(0, c->h / 2 + 12, c->w, 20),
                    plat_debian() ? "Dans le Terminal : installer-chrome. Ou avec ce bouton (environ 120 Mo)." :
                                    "Environ 1 Go à télécharger, installé une fois pour toutes.", C_MUTED);
        ui_button(c, install_btn(c), st == BR_ERROR ? "Réessayer" : "Installer Google Chrome", BTN_ACCENT, b->hover);
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
    window_t *w = wm_open(plat_browser_is_chrome() || plat_browser_state(NULL, 0, NULL) != BR_READY ? "Google Chrome" : "Chromium",
                          ICON_BROWSER, 1100, 700, WF_SINGLE | WF_RESIZABLE);
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

/* (plus utilisée : Google Chrome ne s'installe que si on le demande, dans le Terminal avec
 * installer-chrome ou avec le bouton de sa fenêtre) */
void browser_autoinstall(bool online) {
    static bool tried;
    if (tried || !online) return;
    if (plat_browser_is_chrome() || plat_browser_state(NULL, 0, NULL) == BR_INSTALLING) return;
    tried = true;
    if (plat_browser_install()) wm_toast("Installation de Google Chrome en arrière-plan…");
}

/* icône du navigateur : celle de Google Chrome une fois installé (son fichier PNG) */
bool png_decode(const uint8_t *f, size_t n, canvas_t *out);
bool browser_icon(canvas_t *c, float x, float y, float s) {
    static canvas_t mip[4];
    static bool tried;
    static uint64_t next;
    if (!mip[0].px && !tried && plat_browser_is_chrome()) {
        tried = true;
        static const char *const paths[] = {
            "/var/lib/flatpak/exports/share/icons/hicolor/128x128/apps/com.google.Chrome.png",
            "/var/lib/flatpak/exports/share/icons/hicolor/256x256/apps/com.google.Chrome.png",
            "/var/lib/flatpak/exports/share/icons/hicolor/64x64/apps/com.google.Chrome.png" };
        for (int i = 0; i < 3 && !mip[0].px; i++) {
            char *d;
            size_t n;
            if (plat_readfile(paths[i], &d, &n) != 0) continue;
            if (!png_decode((const uint8_t *)d, n, &mip[0])) canvas_free(&mip[0]);
            free(d);
        }
        for (int m = 1; m < 4 && mip[0].px; m++) {         /* réductions par moitiés : icône nette à toute taille */
            const canvas_t *sc = &mip[m - 1];
            if (sc->w < 32) break;
            mip[m] = canvas_new(sc->w / 2, sc->h / 2);
            if (!mip[m].px) break;
            for (int yy = 0; yy < mip[m].h; yy++)
                for (int xx = 0; xx < mip[m].w; xx++) {
                    const uint32_t *a = sc->px + (size_t)(2 * yy) * sc->stride + 2 * xx, *b2 = a + sc->stride;
                    uint32_t r = 0;
                    for (int sh = 0; sh < 32; sh += 8)
                        r |= ((((a[0] >> sh) & 255) + ((a[1] >> sh) & 255) + ((b2[0] >> sh) & 255) + ((b2[1] >> sh) & 255) + 2) / 4) << sh;
                    mip[m].px[(size_t)yy * mip[m].stride + xx] = r;
                }
        }
    }
    if (!mip[0].px) {                                      /* pas encore installé : on réessaiera plus tard */
        uint64_t now = plat_ms();
        if (tried && now >= next) { next = now + 10000; tried = false; }
        return false;
    }
    int m = 0;
    while (m < 3 && mip[m + 1].px && mip[m + 1].w >= s) m++;
    gfx_blit_scaled(c, x, y, s, s, &mip[m], R(0, 0, mip[m].w, mip[m].h), 255);
    return true;
}
