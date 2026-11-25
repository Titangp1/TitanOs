/* TitanOs - Logithèque : installer des applications Linux en un clic
 *
 * Python et Node.js (paquets Alpine) s'ouvrent dans le Terminal. LibreOffice et GIMP
 * (paquets Alpine), VS Code et Discord (Flatpak, depuis Flathub) sont des applications
 * graphiques X11 : chacune tourne sur son propre serveur X en mémoire et s'affiche dans
 * une fenêtre TitanOs (voir src/linux/xbridge.c). Une fois installées, elles apparaissent
 * dans le menu Démarrer, avec leur propre icône, et peuvent être mises sur le bureau. */
#include "desk.h"

bool png_decode(const uint8_t *f, size_t n, canvas_t *out);

#define FLATHUB "flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo && "
#define FLATPAK_BASE "mkdir -p /var/tmp && apk add --no-progress xvfb setxkbmap flatpak && " FLATHUB
#define FLATPAK_UNINSTALL(id) "flatpak uninstall -y --noninteractive " id " && flatpak uninstall -y --noninteractive --unused"
#define FP_ICONS(id) { "/var/lib/flatpak/exports/share/icons/hicolor/128x128/apps/" id ".png", \
                       "/var/lib/flatpak/exports/share/icons/hicolor/256x256/apps/" id ".png", \
                       "/var/lib/flatpak/exports/share/icons/hicolor/512x512/apps/" id ".png", \
                       "/var/lib/flatpak/exports/share/icons/hicolor/64x64/apps/" id ".png" }

typedef struct {
    const char *id, *name, *desc, *size;
    const char *install, *remove;      /* scripts shell */
    const char *check;                 /* fichiers dont l'un existe une fois installée (séparés par |) */
    const char *run;                   /* commande X11 ; NULL : dans le Terminal */
    const char *term;                  /* commande tapée dans le Terminal */
    const char *icons[4];              /* icônes PNG de l'application (après installation) */
    uint32_t c0, c1;                   /* couleurs de l'icône de remplacement */
    const char *letters;
} sapp_t;

static const sapp_t APPS[] = {
    { "libreoffice", "LibreOffice", "Traitement de texte, tableur et présentations, compatibles avec Word, Excel et PowerPoint.",
      "≈ 600 Mo",
      "apk add --no-progress xvfb setxkbmap libreoffice-writer libreoffice-calc libreoffice-impress libreoffice-lang-fr "
      "font-dejavu font-liberation",
      "apk del --no-progress libreoffice-writer libreoffice-calc libreoffice-impress libreoffice-lang-fr",
      "/usr/lib/libreoffice/program/soffice|/usr/bin/libreoffice",
      "exec $(command -v libreoffice || echo /usr/lib/libreoffice/program/soffice) --nologo", NULL,
      { "/usr/share/icons/hicolor/128x128/apps/libreoffice-startcenter.png", "/usr/share/icons/hicolor/256x256/apps/libreoffice-startcenter.png",
        "/usr/share/icons/hicolor/64x64/apps/libreoffice-startcenter.png", "/usr/share/icons/hicolor/48x48/apps/libreoffice-startcenter.png" },
      0x34D399, 0x0E7490, "LO" },
    { "vscode", "VS Code", "L'éditeur de code de Microsoft, avec ses extensions. Depuis Flathub.",
      "≈ 900 Mo avec la base Flatpak",
      FLATPAK_BASE "flatpak install -y --noninteractive --system flathub com.visualstudio.code",
      FLATPAK_UNINSTALL("com.visualstudio.code"),
      "/var/lib/flatpak/app/com.visualstudio.code/current",
      "exec flatpak run com.visualstudio.code --no-sandbox --disable-gpu --user-data-dir=/root/.var/app/com.visualstudio.code/data/vscode",
      NULL, FP_ICONS("com.visualstudio.code"), 0x38BDF8, 0x1D4ED8, "</>" },
    { "discord", "Discord", "Discussions, salons et appels avec vos amis. Depuis Flathub.",
      "≈ 700 Mo avec la base Flatpak",
      FLATPAK_BASE "flatpak install -y --noninteractive --system flathub com.discordapp.Discord",
      FLATPAK_UNINSTALL("com.discordapp.Discord"),
      "/var/lib/flatpak/app/com.discordapp.Discord/current",
      "exec flatpak run com.discordapp.Discord --no-sandbox --disable-gpu",
      NULL, FP_ICONS("com.discordapp.Discord"), 0x818CF8, 0x4338CA, "D" },
    { "python", "Python 3", "Le langage de programmation, avec pip pour ajouter des modules. S'ouvre dans le Terminal.",
      "≈ 60 Mo",
      "apk add --no-progress python3 py3-pip", "apk del --no-progress python3 py3-pip",
      "/usr/bin/python3", NULL, "python3", { NULL }, 0x60A5FA, 0x1E3A8A, "Py" },
    { "nodejs", "Node.js", "JavaScript hors du navigateur, avec npm. S'ouvre dans le Terminal.",
      "≈ 80 Mo",
      "apk add --no-progress nodejs npm", "apk del --no-progress nodejs npm",
      "/usr/bin/node", NULL, "node", { NULL }, 0x86EFAC, 0x15803D, "JS" },
    { "gimp", "GIMP", "Retouche de photos et dessin.",
      "≈ 300 Mo",
      "apk add --no-progress xvfb setxkbmap gimp font-dejavu", "apk del --no-progress gimp",
      "/usr/bin/gimp", "exec gimp", NULL,
      { "/usr/share/icons/hicolor/256x256/apps/gimp.png", "/usr/share/icons/hicolor/128x128/apps/gimp.png",
        "/usr/share/icons/hicolor/64x64/apps/gimp.png", "/usr/share/icons/hicolor/48x48/apps/gimp.png" },
      0xFDBA74, 0x9A3412, "G" },
};
#define NAPPS ((int)countof(APPS))

/* ---------------- état : installée ? icône chargée ? ---------------- */
static struct { canvas_t mip[4]; bool tried; int inst; uint64_t checked; } ST[NAPPS];   /* inst : -1 inconnu */

int store_count(void) { return NAPPS; }
const char *store_name(int k) { return k >= 0 && k < NAPPS ? APPS[k].name : "Application"; }

bool store_installed(int k) {
    if (k < 0 || k >= NAPPS) return false;
    uint64_t now = plat_ms();
    if (ST[k].checked && now - ST[k].checked < 3000) return ST[k].inst > 0;
    ST[k].checked = now;
    char list[256];
    strlcpy(list, APPS[k].check, sizeof list);
    bool ok = false;
    for (char *p = list; p && *p && !ok;) {
        char *bar = strchr(p, '|');
        if (bar) *bar = 0;
        ok = plat_exists(p, NULL);
        p = bar ? bar + 1 : NULL;
    }
    if (ST[k].inst == 0 && ok) ST[k].tried = false;       /* vient d'être installée : icône à charger */
    ST[k].inst = ok;
    return ok;
}
static void store_recheck(int k) { ST[k].checked = 0; }

/* icône de l'application : son PNG (réduit par moitiés successives, pour rester net), sinon
 * un carré arrondi avec ses initiales */
static void load_icon(int k) {
    if (ST[k].tried) return;
    ST[k].tried = true;
    for (int i = 0; i < 4; i++) canvas_free(&ST[k].mip[i]);
    for (int i = 0; i < 4 && APPS[k].icons[i]; i++) {
        char *d;
        size_t n;
        if (plat_readfile(APPS[k].icons[i], &d, &n) != 0) continue;
        bool ok = png_decode((const uint8_t *)d, n, &ST[k].mip[0]);
        free(d);
        if (!ok) continue;
        for (int m = 1; m < 4; m++) {
            const canvas_t *s = &ST[k].mip[m - 1];
            if (s->w < 32) break;
            canvas_t h = canvas_new(s->w / 2, s->h / 2);
            if (!h.px) break;
            for (int y = 0; y < h.h; y++)
                for (int x = 0; x < h.w; x++) {
                    const uint32_t *a = s->px + (size_t)(2 * y) * s->stride + 2 * x, *b = a + s->stride;
                    uint32_t r = 0;
                    for (int sh = 0; sh < 32; sh += 8)
                        r |= ((((a[0] >> sh) & 255) + ((a[1] >> sh) & 255) + ((b[0] >> sh) & 255) + ((b[1] >> sh) & 255) + 2) / 4) << sh;
                    h.px[(size_t)y * h.stride + x] = r;
                }
            ST[k].mip[m] = h;
        }
        return;
    }
}

void store_draw_icon(canvas_t *c, int k, float x, float y, float s) {
    if (k < 0 || k >= NAPPS) return;
    if (store_installed(k)) load_icon(k);
    if (ST[k].mip[0].px) {
        int m = 0;
        while (m < 3 && ST[k].mip[m + 1].px && ST[k].mip[m + 1].w >= s) m++;
        const canvas_t *im = &ST[k].mip[m];
        gfx_blit_scaled(c, x, y, s, s, im, R(0, 0, im->w, im->h), 255);
        return;
    }
    icon_squircle(c, x, y, s, APPS[k].c0, APPS[k].c1);
    const font_t *f = s >= 56 ? &font_h2 : s >= 30 ? &font_title : &font_small;
    text_center(c, f, R((int)x, (int)(y + s / 2 - f->size * 0.62f), (int)s, (int)(f->size * 1.3f)), APPS[k].letters, 0xFFFFFFFF);
}

/* ================================================================== */
/* Fenêtre d'une application X11                                        */
/* ================================================================== */
typedef struct {
    int app, x;
    bool running, closing;
    uint64_t next, next_cur, next_manage, t0;
    int sent_w, sent_h, W, H;
} xw_t;

static void xw_draw(window_t *w, canvas_t *c) {
    xw_t *s = w->data;
    if (s->running) return;                      /* l'image de l'application est déjà dans la surface */
    gfx_fill(c, R(0, 0, c->w, c->h), 0x30000000);
    store_draw_icon(c, s->app, c->w / 2 - 40, c->h / 2 - 120, 80);
    text_center(c, &font_h2, R(0, c->h / 2 - 24, c->w, 32), APPS[s->app].name, C_TEXT);
    const char *m = s->x < 0 ? "Impossible de démarrer l'application (serveur X absent ou trop d'applications ouvertes)."
                  : plat_ms() - s->t0 > 15000 ? "Le premier démarrage peut prendre un moment…" : "Démarrage…";
    text_center(c, &font_body, R(0, c->h / 2 + 18, c->w, 22), m, s->x < 0 ? 0xFFFCA5A5 : C_TEXT2);
}

static void xw_event(window_t *w, const event_t *e) {
    xw_t *s = w->data;
    if (!s->running) return;
    if (e->type == EV_MOTION) {
        plat_xapp_input(s->x, 6, 0, e->x, e->y);
        uint64_t now = plat_ms();
        if (now >= s->next_cur) {
            s->next_cur = now + 50;
            if (plat_xapp_cursor(s->x, &w->cursor_img, &w->cursor_hx, &w->cursor_hy)) { w->cursor = CURSOR_APP; w->cursor_ver++; }
        }
    } else if (e->type == EV_BUTTON)
        plat_xapp_input(s->x, e->down ? 4 : 5, e->button == BTN_RIGHT ? 3 : e->button == BTN_MIDDLE ? 2 : 1, e->x, e->y);
    else if (e->type == EV_WHEEL) {
        int btn = e->dy > 0 ? 5 : 4;
        for (int i = 0; i < (e->dy > 0 ? e->dy : -e->dy); i++) { plat_xapp_input(s->x, 4, btn, -1, -1); plat_xapp_input(s->x, 5, btn, -1, -1); }
    }
}

static void xw_raw_key(window_t *w, int code, bool down) {
    xw_t *s = w->data;
    if (s->running) plat_xapp_input(s->x, down ? 2 : 3, code + 8, -1, -1);
}

static void xw_tick(window_t *w, uint64_t now) {
    xw_t *s = w->data;
    if (!s->running) {
        w->cursor = s->x >= 0 ? CURSOR_BUSY : CURSOR_ARROW;
        if (now < s->next) return;
        s->next = now + 250;
        if (s->x < 0) return;
        if (plat_xapp_frame(s->x, w->surf.px, w->surf.stride, w->surf.w, w->surf.h)) {
            s->running = true;
            w->dirty = false;
            w->cursor = CURSOR_ARROW;
            wm_damage_client(w);
            return;
        }
        if (now - s->t0 > 5000 && !plat_xapp_running(s->x)) {
            char m[120];
            snprintf(m, sizeof m, "%s n'a pas pu démarrer.", APPS[s->app].name);
            wm_toast(m);
            wm_close(w);
            return;
        }
        wm_invalidate(w);
        return;
    }
    if (now < s->next) return;
    s->next = now + 33;                               /* environ 30 images par seconde */
    if (w->surf.w != s->sent_w || w->surf.h != s->sent_h) {   /* fenêtre agrandie ou redimensionnée */
        s->sent_w = w->surf.w;
        s->sent_h = w->surf.h;
        plat_xapp_resize(s->x, s->sent_w, s->sent_h);
    } else if (now >= s->next_manage) {               /* nouvelles fenêtres de l'application : placées */
        s->next_manage = now + 500;
        plat_xapp_manage(s->x);
    }
    if (!plat_xapp_running(s->x)) { wm_close(w); return; }
    if (plat_xapp_frame(s->x, w->surf.px, w->surf.stride, w->surf.w, w->surf.h)) wm_damage_client(w);
}

/* × : d'abord une demande polie (l'application peut proposer d'enregistrer), puis, au
 * second clic, arrêt */
static bool xw_can_close(window_t *w) {
    xw_t *s = w->data;
    if (!s->running || s->closing) return true;
    if (!plat_xapp_close(s->x)) return true;
    s->closing = true;
    char m[160];
    snprintf(m, sizeof m, "Fermeture demandée à %s. Cliquez encore sur × pour forcer.", APPS[s->app].name);
    wm_toast(m);
    return false;
}

static void xw_close(window_t *w) {
    xw_t *s = w->data;
    plat_xapp_stop(s->x);
    canvas_free(&w->cursor_img);
    free(s);
}

static void open_xapp(int k) {
    window_t *f = wm_find(STORE_BASE + k);
    if (f) { wm_focus(f); return; }
    xw_t *s = calloc(1, sizeof *s);
    if (!s) return;
    s->app = k;
    s->W = scr_w - 16;
    s->H = scr_h - DOCK_H_PUBLIC - 30 - TITLE_H;
    window_t *w = wm_open(APPS[k].name, STORE_BASE + k, 1100, 700, WF_RESIZABLE);
    if (!w) { free(s); return; }
    w->minw = 480;
    w->minh = 320;
    wm_maximize(w);
    s->sent_w = w->surf.w;
    s->sent_h = w->surf.h;
    s->t0 = plat_ms();
    s->x = plat_xapp_start(APPS[k].run, s->W, s->H);
    if (s->x >= 0) plat_xapp_resize(s->x, s->sent_w, s->sent_h);
    w->data = s;
    w->draw = xw_draw;
    w->event = xw_event;
    w->tick = xw_tick;
    w->raw_key = xw_raw_key;
    w->can_close = xw_can_close;
    w->on_close = xw_close;
    w->cursor = CURSOR_BUSY;
}

void app_store_open(void);

void store_launch(int k) {
    if (k < 0 || k >= NAPPS) return;
    if (!store_installed(k)) { app_store_open(); return; }
    if (APPS[k].run) open_xapp(k);
    else app_terminal_run(APPS[k].term);
}

/* ================================================================== */
/* La Logithèque                                                        */
/* ================================================================== */
enum { B_NONE = -1, B_MAIN = 0, B_REMOVE = 1 };
typedef struct {
    int hover_app, hover_btn;
    ui_progress_t prog[NAPPS];
    bool anim;
    int scroll;
} st_t;

#define CARD_H 136

/* texte sur deux lignes au plus (coupé aux espaces), « … » si trop long */
static void wrap2(canvas_t *c, const font_t *f, int x, int y, int maxw, const char *t, uint32_t col) {
    int n = (int)strlen(t), cut = n;
    if (text_width(f, t) > maxw) {
        cut = 0;
        for (int i = 1; i <= n; i++)
            if ((t[i] == ' ' || t[i] == 0) && text_width_n(f, t, i) <= maxw) cut = i;
        if (!cut) cut = n;
    }
    text_draw_n(c, f, x, y, t, cut, col);
    if (cut < n) text_fit(c, f, x, y + f->line, maxw, t + cut + 1, col);
}
static rect_t card_rect(canvas_t *c, int k, int scroll) {
    int cw = (c->w - 3 * 16) / 2;
    return R(16 + (k % 2) * (cw + 16), 96 + (k / 2) * (CARD_H + 14) - scroll, cw, CARD_H);
}
static rect_t main_btn(rect_t r) { return R(r.x + r.w - 16 - 120, r.y + r.h - 16 - 34, 120, 34); }
static rect_t remove_btn(rect_t r) { return R(r.x + r.w - 16 - 120 - 10 - 120, r.y + r.h - 16 - 34, 120, 34); }

static void st_draw(window_t *w, canvas_t *c) {
    st_t *s = w->data;
    s->anim = false;
    draw_icon(c, ICON_STORE, 24, 22, 48);
    text_draw(c, &font_h2, 86, 22, "Logithèque", C_TEXT);
    text_fit(c, &font_small, 86, 56, c->w - 110,
             "Des applications Linux, installées dans l'espace des applications. Elles rejoignent ensuite le menu Démarrer.", C_TEXT2);
    canvas_clip(c, R(0, 88, c->w, c->h - 88));
    for (int k = 0; k < NAPPS; k++) {
        rect_t r = card_rect(c, k, s->scroll);
        if (r.y > c->h || r.y + r.h < 88) continue;
        gfx_rrect(c, r.x, r.y, r.w, r.h, 16, s->hover_app == k ? 0x1CFFFFFF : 0x12FFFFFF);
        store_draw_icon(c, k, r.x + 16, r.y + 16, 64);
        text_draw(c, &font_title, r.x + 96, r.y + 16, APPS[k].name, C_TEXT);
        wrap2(c, &font_small, r.x + 96, r.y + 42, r.w - 112, APPS[k].desc, C_TEXT2);
        char msg[160];
        int pc = -1, st = plat_app_state(APPS[k].id, msg, sizeof msg, &pc);
        bool inst = store_installed(k);
        rect_t mb = main_btn(r);
        if (st == INS_RUNNING || st == INS_WAITING) {
            s->anim |= ui_progress(c, R(r.x + 96, r.y + r.h - 30, r.w - 112, 8), &s->prog[k], st == INS_WAITING ? -1 : pc);
            text_fit(c, &font_small, r.x + 96, r.y + r.h - 52, r.w - 112, msg, C_MUTED);
        } else {
            s->prog[k].started = false;
            int sy = r.y + r.h - 40, sw = r.w - 112 - (inst ? 260 : 130);
            if (st == INS_FAILED && !inst) wrap2(c, &font_small, r.x + 96, sy - 8, sw, msg, 0xFFFCA5A5);
            else text_fit(c, &font_small, r.x + 96, sy, sw, inst ? "Installée" : APPS[k].size, inst ? 0xFF86EFAC : C_MUTED);
            ui_button(c, mb, inst ? "Ouvrir" : st == INS_FAILED ? "Réessayer" : "Installer", BTN_ACCENT,
                      s->hover_app == k && s->hover_btn == B_MAIN);
            if (inst) ui_button(c, remove_btn(r), "Désinstaller", BTN_NORMAL, s->hover_app == k && s->hover_btn == B_REMOVE);
        }
    }
    canvas_noclip(c);
}

static void remove_cb(bool ok, void *ctx) {
    int k = (int)(intptr_t)ctx;
    if (!ok) return;
    if (plat_app_install(APPS[k].id, APPS[k].remove)) {
        char m[120];
        snprintf(m, sizeof m, "Désinstallation de %s…", APPS[k].name);
        wm_toast(m);
    }
    store_recheck(k);
}

static void st_event(window_t *w, const event_t *e) {
    st_t *s = w->data;
    canvas_t *c = &w->surf;
    if (e->type == EV_WHEEL) {
        int rows = (NAPPS + 1) / 2, total = 96 + rows * (CARD_H + 14) + 10;
        s->scroll = CLAMP(s->scroll + e->dy * 40, 0, MAX(0, total - c->h));
        wm_invalidate(w);
        return;
    }
    if (e->type == EV_MOTION) {
        int ha = -1, hb = B_NONE;
        for (int k = 0; k < NAPPS; k++) {
            rect_t r = card_rect(c, k, s->scroll);
            if (!rect_has(r, e->x, e->y) || e->y < 88) continue;
            ha = k;
            if (rect_has(main_btn(r), e->x, e->y)) hb = B_MAIN;
            else if (store_installed(k) && rect_has(remove_btn(r), e->x, e->y)) hb = B_REMOVE;
        }
        if (ha != s->hover_app || hb != s->hover_btn) { s->hover_app = ha; s->hover_btn = hb; wm_invalidate(w); }
        return;
    }
    if (e->type != EV_BUTTON || !e->down || e->button != BTN_LEFT || s->hover_app < 0 || s->hover_btn == B_NONE) return;
    int k = s->hover_app;
    int st = plat_app_state(APPS[k].id, NULL, 0, NULL);
    if (st == INS_RUNNING || st == INS_WAITING) return;
    sound_play(SFX_CLICK);
    if (s->hover_btn == B_MAIN) {
        if (store_installed(k)) store_launch(k);
        else if (!plat_app_install(APPS[k].id, APPS[k].install)) {
            char m[200];
            plat_app_state(APPS[k].id, m, sizeof m, NULL);
            dlg_message("Logithèque", m[0] ? m : "Installation impossible pour le moment.");
        }
    } else {
        char t[200];
        snprintf(t, sizeof t, "Désinstaller %s ? Ses réglages sont gardés ; vos documents ne sont pas touchés.", APPS[k].name);
        dlg_confirm("Désinstaller", t, "Désinstaller", remove_cb, (void *)(intptr_t)k);
    }
    store_recheck(k);
    wm_invalidate(w);
}

static void st_tick(window_t *w, uint64_t now) {
    st_t *s = w->data;
    static uint64_t next;
    if (s->anim) wm_invalidate(w);                   /* barres de progression animées */
    else if (now >= next) { next = now + 500; wm_invalidate(w); }    /* début ou fin d'une installation */
    for (int k = 0; k < NAPPS; k++) {
        int st = plat_app_state(APPS[k].id, NULL, 0, NULL);
        static int last[NAPPS];
        if (st != last[k]) {
            if (last[k] == INS_RUNNING && st == INS_DONE) {
                store_recheck(k);
                char m[160];
                snprintf(m, sizeof m, store_installed(k) ? "%s est installé : il est dans le menu Démarrer." : "%s a été désinstallé.", APPS[k].name);
                wm_toast(m);
            }
            last[k] = st;
        }
    }
}

static void st_close(window_t *w) { free(w->data); }

void app_store_open(void) {
    window_t *f = wm_find(ICON_STORE);
    if (f) { wm_focus(f); return; }
    st_t *s = calloc(1, sizeof *s);
    if (!s) return;
    s->hover_app = -1;
    s->hover_btn = B_NONE;
    window_t *w = wm_open("Logithèque", ICON_STORE, 880, 530, WF_SINGLE | WF_RESIZABLE);
    if (!w) { free(s); return; }
    w->minw = 640;
    w->minh = 400;
    w->data = s;
    w->draw = st_draw;
    w->event = st_event;
    w->tick = st_tick;
    w->on_close = st_close;
}
