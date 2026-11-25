/* TitanOs - Logithèque : installer des applications Linux en un clic
 *
 * Python et Node.js (paquets Alpine) s'ouvrent dans le Terminal. LibreOffice et GIMP
 * (paquets Alpine), VS Code et Discord (Flatpak, depuis Flathub) sont des applications
 * graphiques X11 : chacune tourne sur son propre serveur X en mémoire et s'affiche dans
 * une fenêtre TitanOs (voir src/linux/xbridge.c). Une fois installées, elles apparaissent
 * dans le menu Démarrer, avec leur propre icône, et peuvent être mises sur le bureau. */
#include "desk.h"

bool png_decode(const uint8_t *f, size_t n, canvas_t *out);

static char *find_nl(char *p, char *end) { while (p < end && *p != '\n') p++; return p < end ? p : NULL; }

#define FLATHUB "flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo && "
#define FLATPAK_BASE "mkdir -p /var/tmp && apk add --no-progress xvfb setxkbmap flatpak dbus pulseaudio pulseaudio-alsa && " FLATHUB
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

static sapp_t APPS[STORE_MAX] = {
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
      "exec flatpak run --filesystem=/media com.visualstudio.code --no-sandbox --disable-gpu",
      NULL, FP_ICONS("com.visualstudio.code"), 0x38BDF8, 0x1D4ED8, "</>" },
    { "discord", "Discord", "Discussions, salons et appels avec vos amis. Depuis Flathub.",
      "≈ 700 Mo avec la base Flatpak",
      FLATPAK_BASE "flatpak install -y --noninteractive --system flathub com.discordapp.Discord",
      FLATPAK_UNINSTALL("com.discordapp.Discord"),
      "/var/lib/flatpak/app/com.discordapp.Discord/current",
      "exec flatpak run --filesystem=/media com.discordapp.Discord --no-sandbox --disable-gpu",
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
#define NFIXED 6
static int napps = NFIXED;
#define NAPPS napps

/* Applications Flathub installées depuis la recherche : gardées dans l'espace des
 * applications (/var/lib/titanos/apps.list, « identifiant<TAB>nom »), avec le reste */
#define APPS_LIST "/var/lib/titanos/apps.list"
static struct { char id[96], name[64], install[400], remove[200], check[160], run[200], icons[4][180]; } custom[STORE_MAX - NFIXED];
static void custom_add(const char *id, const char *name) {
    if (napps >= STORE_MAX) return;
    for (int i = 0; i < napps; i++) if (APPS[i].check && strstr(APPS[i].check, id)) return;
    int k = napps - NFIXED;
    strlcpy(custom[k].id, id, sizeof custom[k].id);
    strlcpy(custom[k].name, name, sizeof custom[k].name);
    snprintf(custom[k].install, sizeof custom[k].install, FLATPAK_BASE "flatpak install -y --noninteractive --system flathub %s", id);
    snprintf(custom[k].remove, sizeof custom[k].remove, "flatpak uninstall -y --noninteractive %s && flatpak uninstall -y --noninteractive --unused", id);
    snprintf(custom[k].check, sizeof custom[k].check, "/var/lib/flatpak/app/%s/current", id);
    snprintf(custom[k].run, sizeof custom[k].run, "exec flatpak run --filesystem=/media %s", id);
    static const int sz[4] = { 128, 256, 512, 64 };
    for (int i = 0; i < 4; i++)
        snprintf(custom[k].icons[i], sizeof custom[k].icons[i], "/var/lib/flatpak/exports/share/icons/hicolor/%dx%d/apps/%s.png", sz[i], sz[i], id);
    sapp_t *a = &APPS[napps++];
    memset(a, 0, sizeof *a);
    a->id = custom[k].id;
    a->name = custom[k].name;
    a->desc = "Installée depuis Flathub.";
    a->size = "Flathub";
    a->install = custom[k].install;
    a->remove = custom[k].remove;
    a->check = custom[k].check;
    a->run = custom[k].run;
    for (int i = 0; i < 4; i++) a->icons[i] = custom[k].icons[i];
    a->c0 = 0x94A3B8;
    a->c1 = 0x334155;
    a->letters = "·";
}
static void custom_load(void) {
    static bool done;
    if (done) return;
    done = true;
    char *d;
    size_t n;
    if (plat_readfile(APPS_LIST, &d, &n) != 0) return;
    char *p = d, *end = d + n;
    while (p < end) {
        char *nl = find_nl(p, end);
        if (!nl) nl = end;
        *nl = 0;
        char *tab = strchr(p, '\t');
        if (tab) { *tab = 0; custom_add(p, tab + 1); }
        p = nl + 1;
    }
    free(d);
}
static void custom_save(void) {
    char buf[4096];
    int k = 0;
    for (int i = NFIXED; i < napps && k < (int)sizeof buf - 200; i++) k += snprintf(buf + k, sizeof buf - (size_t)k, "%s\t%s\n", APPS[i].id, APPS[i].name);
    plat_mkdir("/var/lib/titanos");
    plat_writefile(APPS_LIST, buf, (size_t)k);
}

/* ---------------- état : installée ? icône chargée ? ---------------- */
static struct { canvas_t mip[4]; bool tried; int inst; uint64_t checked; } ST[STORE_MAX];   /* inst : -1 inconnu */

int store_count(void) { custom_load(); return NAPPS; }
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
/* résultats d'une recherche : paquets Alpine (apk) et applications Flathub */
typedef struct { bool flathub; char name[64], id[96], desc[120]; } sres_t;
#define SRES_MAX 60
typedef struct {
    int hover_app, hover_btn;
    ui_progress_t prog[STORE_MAX];
    bool anim;
    int scroll;
    field_t search;
    int job;                          /* recherche en cours */
    bool searching, results;          /* recherche lancée, résultats affichés */
    sres_t res[SRES_MAX];
    int nres, rscroll, rhover;
    ui_progress_t rprog[SRES_MAX];
    char query[48];
} st_t;
static rect_t search_rect(canvas_t *c) { return R(c->w - 16 - 330, 22, 330, 40); }
static rect_t res_row(canvas_t *c, int k, int scroll) { return R(16, 96 + k * 66 - scroll, c->w - 32, 60); }
static rect_t res_btn(rect_t r) { return R(r.x + r.w - 14 - 130, r.y + 13, 130, 34); }
static char res_job_id[SRES_MAX][104];
static const char *res_key(const sres_t *r, char *out, int n) { snprintf(out, (size_t)n, "%s:%s", r->flathub ? "fp" : "apk", r->flathub ? r->id : r->name); return out; }

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

static void draw_results(st_t *s, canvas_t *c) {
    canvas_clip(c, R(0, 88, c->w, c->h - 88));
    if (s->searching) text_center(c, &font_body, R(0, 140, c->w, 24), "Recherche dans Alpine et Flathub…", C_MUTED);
    else if (!s->nres) text_center(c, &font_body, R(0, 140, c->w, 24), "Aucun résultat. Essayez un autre mot (en anglais : « editor », « music »…).", C_MUTED);
    for (int k = 0; k < s->nres; k++) {
        rect_t r = res_row(c, k, s->rscroll);
        if (r.y > c->h || r.y + r.h < 88) continue;
        const sres_t *x = &s->res[k];
        gfx_rrect(c, r.x, r.y, r.w, r.h, 12, s->rhover == k ? 0x1CFFFFFF : 0x10FFFFFF);
        icon_squircle(c, r.x + 12, r.y + 10, 40, x->flathub ? 0x60A5FA : 0x34D399, x->flathub ? 0x1D4ED8 : 0x0E7490);
        char ini[4] = { (char)(x->name[0] >= 'a' && x->name[0] <= 'z' ? x->name[0] - 32 : x->name[0]), 0 };
        text_center(c, &font_title, R(r.x + 12, r.y + 19, 40, 22), ini, 0xFFFFFFFF);
        text_fit(c, &font_bold, r.x + 66, r.y + 10, r.w - 300, x->name, C_TEXT);
        text_draw(c, &font_small, r.x + r.w - 14 - 130 - 80, r.y + 22, x->flathub ? "Flathub" : "Alpine", C_MUTED);
        text_fit(c, &font_small, r.x + 66, r.y + 32, r.w - 300, x->desc, C_TEXT2);
        char key[104], msg[160];
        int pc = -1, st = plat_app_state(res_key(x, key, sizeof key), msg, sizeof msg, &pc);
        if (st == INS_RUNNING || st == INS_WAITING)
            s->anim |= ui_progress(c, R(r.x + r.w - 14 - 130, r.y + 26, 130, 8), &s->rprog[k], st == INS_WAITING ? -1 : pc);
        else ui_button(c, res_btn(r), st == INS_DONE ? "Installé" : st == INS_FAILED ? "Réessayer" : "Installer",
                       st == INS_DONE ? BTN_NORMAL : BTN_ACCENT, s->rhover == k);
    }
    canvas_noclip(c);
}

static void st_draw(window_t *w, canvas_t *c) {
    st_t *s = w->data;
    s->anim = false;
    draw_icon(c, ICON_STORE, 24, 22, 48);
    text_draw(c, &font_h2, 86, 22, "Logithèque", C_TEXT);
    text_fit(c, &font_small, 86, 56, c->w - 110 - 340,
             s->results ? "Paquets Alpine (commandes du Terminal) et applications Flathub. Échap : retour." :
             "Des applications Linux, installées dans l'espace des applications. Elles rejoignent le menu Démarrer.", C_TEXT2);
    ui_field(c, search_rect(c), &s->search, "Chercher un paquet ou une application…");
    if (s->results) { draw_results(s, c); return; }
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

/* recherche : « apk search » et « flatpak search », résultats dans un fichier */
static void start_search(st_t *s) {
    char q[48];
    int k = 0;
    for (const char *p = s->search.s; *p && k < (int)sizeof q - 1; p++)          /* mot sûr pour le shell */
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '.' || *p == '+' || *p == '_')
            q[k++] = *p;
    q[k] = 0;
    if (!q[0]) { s->results = false; return; }
    strlcpy(s->query, q, sizeof s->query);
    static char script[900];
    snprintf(script, sizeof script,
             "f=/run/titanos-recherche.txt; : > $f.tmp;"
             " apk search -v -d -- '%s' 2>/dev/null | head -n 40 | sed 's/^/A\t/' >> $f.tmp;"
             " if [ -x /usr/bin/flatpak ]; then flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo >/dev/null 2>&1;"
             " flatpak update --appstream -y >/dev/null 2>&1; flatpak search --columns=name,description,application -- '%s' 2>/dev/null"
             " | head -n 20 | sed 's/^/F\t/' >> $f.tmp; fi; mv -f $f.tmp $f", q, q);
    const char *argv[] = { "/bin/busybox", "sh", "-c", script, NULL };
    s->job = plat_spawn(argv);
    s->searching = s->job >= 0;
    s->results = true;
    s->nres = 0;
    s->rscroll = 0;
}

static void read_results(st_t *s) {
    char *d;
    size_t n;
    s->nres = 0;
    if (plat_readfile("/run/titanos-recherche.txt", &d, &n) != 0) return;
    char *p = d, *end = d + n;
    while (p < end && s->nres < SRES_MAX) {
        char *nl = find_nl(p, end);
        if (!nl) nl = end;
        *nl = 0;
        sres_t *r = &s->res[s->nres];
        memset(r, 0, sizeof *r);
        if (p[0] == 'A' && p[1] == '\t') {                 /* « nom-1.2-r0 - description » */
            char *sep = strstr(p + 2, " - ");
            if (sep) { *sep = 0; strlcpy(r->desc, sep + 3, sizeof r->desc); }
            strlcpy(r->name, p + 2, sizeof r->name);
            char *v = strrchr(r->name, '-');                 /* -r0 */
            if (v && v[1] == 'r') { *v = 0; v = strrchr(r->name, '-'); if (v) *v = 0; }
            if (r->name[0]) s->nres++;
        } else if (p[0] == 'F' && p[1] == '\t') {          /* nom, description, identifiant */
            char *f1 = p + 2, *f2 = strchr(f1, '\t'), *f3 = f2 ? strchr(f2 + 1, '\t') : NULL;
            if (f2 && f3) {
                *f2 = *f3 = 0;
                r->flathub = true;
                strlcpy(r->name, f1, sizeof r->name);
                strlcpy(r->desc, f2 + 1, sizeof r->desc);
                strlcpy(r->id, f3 + 1, sizeof r->id);
                char *t = strchr(r->id, '\t');
                if (t) *t = 0;
                if (r->id[0] && strchr(r->id, '.')) s->nres++;
            }
        }
        p = nl + 1;
    }
    free(d);
}

static void install_result(st_t *s, int k) {
    sres_t *r = &s->res[k];
    char key[104], script[600];
    res_key(r, key, sizeof key);
    if (r->flathub) snprintf(script, sizeof script, FLATPAK_BASE "flatpak install -y --noninteractive --system flathub %s", r->id);
    else snprintf(script, sizeof script, "apk add --no-progress %s", r->name);
    if (!plat_app_install(key, script)) {
        char m[200];
        plat_app_state(key, m, sizeof m, NULL);
        dlg_message("Logithèque", m[0] ? m : "Installation impossible pour le moment.");
        return;
    }
    strlcpy(res_job_id[k], key, sizeof res_job_id[k]);
}

static void st_event(window_t *w, const event_t *e) {
    st_t *s = w->data;
    canvas_t *c = &w->surf;
    /* champ de recherche */
    if (e->type == EV_BUTTON && e->down) s->search.focus = rect_has(search_rect(c), e->x, e->y);
    if (e->type == EV_KEY && e->down && s->search.focus) {
        if (e->key == K_ENTER) start_search(s);
        else if (e->key == K_ESC) { ui_field_set(&s->search, ""); s->results = false; s->search.focus = false; }
        else ui_field_key(&s->search, e);
        wm_invalidate(w);
        return;
    }
    if (e->type == EV_KEY && e->down && e->key == K_ESC && s->results) { s->results = false; wm_invalidate(w); return; }
    if (s->results) {                                    /* liste des résultats */
        int total = 96 + s->nres * 66 + 10;
        if (e->type == EV_WHEEL) { s->rscroll = CLAMP(s->rscroll + e->dy * 40, 0, MAX(0, total - c->h)); wm_invalidate(w); return; }
        int h = -1;
        for (int k = 0; k < s->nres; k++) if (e->y >= 88 && rect_has(res_btn(res_row(c, k, s->rscroll)), e->x, e->y)) h = k;
        if (e->type == EV_MOTION) { if (h != s->rhover) { s->rhover = h; wm_invalidate(w); } return; }
        if (e->type == EV_BUTTON && e->down && e->button == BTN_LEFT && h >= 0) {
            char key[104];
            int st = plat_app_state(res_key(&s->res[h], key, sizeof key), NULL, 0, NULL);
            if (st == INS_NONE || st == INS_FAILED) { sound_play(SFX_CLICK); install_result(s, h); }
            wm_invalidate(w);
        }
        return;
    }
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
    if (s->job >= 0) {                                   /* recherche terminée */
        char out[64];
        if (plat_spawn_poll(s->job, out, sizeof out) != -1) { s->job = -1; s->searching = false; read_results(s); wm_invalidate(w); }
    }
    for (int k = 0; k < s->nres; k++) {                  /* installation d'un résultat terminée */
        if (!res_job_id[k][0]) continue;
        int st = plat_app_state(res_job_id[k], NULL, 0, NULL);
        if (st == INS_DONE || st == INS_FAILED) {
            char m[200];
            if (st == INS_DONE && s->res[k].flathub) {
                custom_add(s->res[k].id, s->res[k].name);
                custom_save();
                snprintf(m, sizeof m, "%s est installé : il est dans le menu Démarrer.", s->res[k].name);
            } else if (st == INS_DONE) snprintf(m, sizeof m, "%s est installé : tapez « %s » dans le Terminal.", s->res[k].name, s->res[k].name);
            else snprintf(m, sizeof m, "%s n'a pas pu être installé.", s->res[k].name);
            wm_toast(m);
            res_job_id[k][0] = 0;
            wm_invalidate(w);
        }
    }
    if (s->anim) wm_invalidate(w);                   /* barres de progression animées */
    else if (now >= next) { next = now + 500; wm_invalidate(w); }    /* début ou fin d'une installation */
    for (int k = 0; k < NAPPS; k++) {
        int st = plat_app_state(APPS[k].id, NULL, 0, NULL);
        static int last[STORE_MAX];
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
    s->job = -1;
    s->rhover = -1;
    custom_load();
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
