/* TitanOs - Paramètres
 *
 * Une barre latérale de catégories ; chaque page présente ses réglages en lignes : titre
 * et explication à gauche, contrôle à droite. Tout réglage s'applique et s'enregistre
 * immédiatement (titanos.cfg). */
#include "desk.h"

enum { P_LOOK, P_SCREEN, P_SOUND, P_INPUT, P_NET, P_TIME, P_ACCOUNT, P_SYSTEM, P_COUNT };
static const char *const page_names[P_COUNT] = {
    "Personnalisation", "Écran", "Son", "Clavier et souris", "Réseau", "Date et heure", "Compte", "Système",
};
static const char *const page_subs[P_COUNT] = {
    "Fond d'écran, couleur et effet verre",
    "Luminosité et affichage",
    "Volume et sons de l'interface",
    "Disposition du clavier, pointeur",
    "Wi-Fi, câble et partage de connexion",
    "Fuseau horaire et heure par Internet",
    "Nom et mot de passe de session",
    "Version, mises à jour et matériel",
};

typedef struct {
    int page, hover;
    field_t name;
    canvas_t thumbs[WALLPAPER_COUNT];
    int ntp_job;
    int drag;                       /* curseur en cours de glissement (identifiant), 0 sinon */
    uint64_t next;
} prefs_t;

enum {   /* zones cliquables */
    H_NONE = 0, H_SIDE0 = 1, H_WALL0 = 20, H_ACC0 = 40, H_GLASS0 = 50, H_BRIGHT = 60, H_VOL, H_SOUNDS,
    H_LAYOUT0 = 70, H_PTR = 80, H_NETOPEN, H_TZ_PREV, H_TZ_NEXT, H_NTP, H_SECONDS, H_RTC, H_NAME, H_PASS,
    H_PASS_DEL, H_UPDATES, H_ABOUT,
};

#define WIN_W   980
#define WIN_H   620
#define SIDE_W  232
#define TW      160
#define TH      92

/* ---------------- géométrie ---------------- */
static rect_t side_item(int i) { return R(14, 16 + i * 58, SIDE_W - 28, 50); }
static int content_x(void) { return SIDE_W + 20; }
static int content_w(canvas_t *c) { return c->w - SIDE_W - 44; }
static rect_t row_rect(canvas_t *c, int i) { return R(content_x(), 92 + i * 66, content_w(c), 58); }
static rect_t row_ctrl(canvas_t *c, int i, int w, int h) { rect_t r = row_rect(c, i); return R(r.x + r.w - 14 - w, r.y + (r.h - h) / 2, w, h); }
static rect_t wall_rect(int i) { return R(content_x() + (i % 4) * (TW + 14), 120 + (i / 4) * (TH + 34), TW, TH); }
static rect_t acc_rect(int i) { int y = 120 + ((WALLPAPER_COUNT + 3) / 4) * (TH + 34) + 34; return R(content_x() + i * 46, y, 34, 34); }
static rect_t glass_rect(void) { rect_t a = acc_rect(0); return R(content_x(), a.y + 82, 360, 36); }

/* ---------------- dessin commun ---------------- */
static void page_title(canvas_t *c, int page) {
    text_draw(c, &font_h2, content_x(), 22, page_names[page], C_TEXT);
    text_draw(c, &font_small, content_x(), 56, page_subs[page], C_MUTED);
}

static void row(canvas_t *c, int i, const char *title, const char *sub) {
    rect_t r = row_rect(c, i);
    ui_panel(c, r, 14);
    text_draw(c, &font_bold, r.x + 18, r.y + (sub ? 10 : 19), title, C_TEXT);
    if (sub) text_fit(c, &font_small, r.x + 18, r.y + 32, r.w / 2, sub, C_MUTED);
}

static void label(canvas_t *c, int x, int y, const char *t) { text_draw(c, &font_bold, x, y, t, C_TEXT2); }

/* ---------------- pages ---------------- */
static void draw_look(prefs_t *p, canvas_t *c) {
    label(c, content_x(), 94, "Fond d'écran");
    for (int i = 0; i < WALLPAPER_COUNT; i++) {
        rect_t r = wall_rect(i);
        if (!p->thumbs[i].px) {
            p->thumbs[i] = canvas_new(TW, TH);
            wallpaper_thumb(&p->thumbs[i], 0, 0, TW, TH, i);
            canvas_t m = canvas_new(TW, TH);                 /* coins arrondis */
            gfx_rrect(&m, 0, 0, TW, TH, 12, 0xFFFFFFFF);
            for (int k = 0; k < TW * TH; k++) {
                uint32_t a = m.px[k] >> 24, v = p->thumbs[i].px[k];
                p->thumbs[i].px[k] = (((v & 0xFF00FF) * a >> 8) & 0xFF00FF) | ((((v >> 8) & 0xFF00FF) * a) & 0xFF00FF00);
            }
            canvas_free(&m);
        }
        gfx_blit(c, r.x, r.y, &p->thumbs[i], 255);
        if (i == g_set.wallpaper) gfx_rrect_stroke(c, r.x - 3, r.y - 3, r.w + 6, r.h + 6, 15, 2.5f, accent());
        else if (p->hover == H_WALL0 + i) gfx_rrect_stroke(c, r.x - 2, r.y - 2, r.w + 4, r.h + 4, 14, 1.5f, 0x80FFFFFF);
        text_center(c, &font_small, R(r.x, r.y + r.h + 6, r.w, 18), wallpaper_name(i), i == g_set.wallpaper ? C_TEXT : C_TEXT2);
    }
    rect_t a0 = acc_rect(0);
    label(c, content_x(), a0.y - 28, "Couleur d'accent");
    for (int i = 0; i < ACCENT_COUNT; i++) {
        rect_t r = acc_rect(i);
        float cx = r.x + 17, cy = r.y + 17;
        if (i == g_set.accent) gfx_ring(c, cx, cy, 17, 2.5f, 0xFFFFFFFF);
        gfx_circle(c, cx, cy, i == g_set.accent ? 12 : (p->hover == H_ACC0 + i ? 14 : 13), accent_of(i));
    }
    rect_t g = glass_rect();
    label(c, content_x(), g.y - 28, "Effet verre");
    static const char *gl[3] = { "Transparent", "Normal", "Dépoli" };
    ui_segments(c, g, gl, 3, g_set.glass, p->hover >= H_GLASS0 && p->hover < H_GLASS0 + 3 ? p->hover - H_GLASS0 : -1);
}

static void draw_screen(prefs_t *p, canvas_t *c) {
    int b = plat_brightness(-1);
    row(c, 0, "Luminosité", b < 0 ? "Non réglable sur cet écran" : "Aussi avec les touches Fn du clavier");
    if (b >= 0) ui_slider(c, row_ctrl(c, 0, 300, 40), b);
    sysinfo_t si;
    plat_sysinfo(&si);
    row(c, 1, "Affichage", si.display);
    row(c, 2, "Carte graphique", si.gpu);
    text_fit(c, &font_small, row_rect(c, 2).x + row_rect(c, 2).w - 330, row_rect(c, 2).y + 21, 316, si.gpu_driver, C_TEXT2);
    (void)p;
}

static void draw_sound(prefs_t *p, canvas_t *c) {
    int v = plat_volume(-1);
    row(c, 0, "Volume", v < 0 ? "Aucune carte son détectée" : "Aussi avec les touches Fn du clavier");
    if (v >= 0) ui_slider(c, row_ctrl(c, 0, 300, 40), g_set.volume);
    row(c, 1, "Sons de l'interface", "Clics, notifications, démarrage");
    ui_toggle(c, row_ctrl(c, 1, 52, 30), g_set.sounds);
    (void)p;
}

static void draw_input(prefs_t *p, canvas_t *c) {
    row(c, 0, "Disposition du clavier", "Aussi depuis la barre : FR / US");
    static const char *lay[2] = { "AZERTY (FR)", "QWERTY (US)" };
    ui_segments(c, row_ctrl(c, 0, 280, 36), lay, 2, g_set.layout, p->hover >= H_LAYOUT0 && p->hover < H_LAYOUT0 + 2 ? p->hover - H_LAYOUT0 : -1);
    row(c, 1, "Vitesse du pointeur", "Souris et pavé tactile");
    ui_slider(c, row_ctrl(c, 1, 300, 40), (g_set.pointer - 1) * 25);
}

static void draw_net(prefs_t *p, canvas_t *c) {
    char ssid[33];
    int sig, st = plat_wifi_status(ssid, sizeof ssid, &sig);
    char sub[96];
    if (st == WIFI_CONNECTED) snprintf(sub, sizeof sub, "Connecté à « %s »", ssid);
    else if (st == WIFI_NONE) strlcpy(sub, "Aucune carte Wi-Fi utilisable", sizeof sub);
    else if (st == WIFI_BLOCKED) strlcpy(sub, "Radio coupée par l'interrupteur de l'ordinateur", sizeof sub);
    else if (st == WIFI_CONNECTING) strlcpy(sub, "Connexion en cours…", sizeof sub);
    else strlcpy(sub, "Non connecté", sizeof sub);
    row(c, 0, "Wi-Fi", sub);
    ui_button(c, row_ctrl(c, 0, 220, 36), "Ouvrir Réseau", BTN_ACCENT, p->hover == H_NETOPEN);
    row(c, 1, "Câble et partage USB", "Connexion automatique dès le branchement");
}

static void draw_time(prefs_t *p, canvas_t *c) {
    row(c, 0, "Fuseau horaire", NULL);
    rect_t a = row_ctrl(c, 0, 360, 34), pv = R(a.x, a.y, 34, 34), nx = R(a.x + a.w - 34, a.y, 34, 34);
    gfx_rrect(c, pv.x, pv.y, pv.w, pv.h, 17, p->hover == H_TZ_PREV ? 0x38FFFFFF : 0x22FFFFFF);
    gfx_rrect(c, nx.x, nx.y, nx.w, nx.h, 17, p->hover == H_TZ_NEXT ? 0x38FFFFFF : 0x22FFFFFF);
    text_center(c, &font_bold, pv, "←", C_TEXT);
    text_center(c, &font_bold, nx, "→", C_TEXT);
    text_center(c, &font_body, R(pv.x + 34, a.y, a.w - 68, 34), tz_list[g_set.tz].name, C_TEXT);
    row(c, 1, "Heure par Internet", p->ntp_job >= 0 ? "Synchronisation en cours…" : "Automatique à chaque connexion (pool.ntp.org)");
    ui_button(c, row_ctrl(c, 1, 220, 36), "Mettre à l'heure", BTN_NORMAL, p->hover == H_NTP);
    row(c, 2, "Secondes sur l'horloge", NULL);
    ui_toggle(c, row_ctrl(c, 2, 52, 30), g_set.seconds);
    row(c, 3, "Horloge du PC en heure locale", "Seulement si Windows est aussi installé sur ce PC");
    ui_toggle(c, row_ctrl(c, 3, 52, 30), g_set.rtc_local);
}

static void draw_account(prefs_t *p, canvas_t *c) {
    row(c, 0, "Nom d'utilisateur", p->name.focus ? "Entrée pour valider" : "Affiché à l'écran de verrouillage");
    ui_field(c, row_ctrl(c, 0, 300, 40), &p->name, NULL);
    row(c, 1, "Mot de passe de session", g_set.pass[0] ? "Demandé à l'écran de verrouillage" : "Aucun : la session s'ouvre d'un clic");
    ui_button(c, row_ctrl(c, 1, 170, 36), g_set.pass[0] ? "Changer…" : "Définir…", BTN_NORMAL, p->hover == H_PASS);
    if (g_set.pass[0]) {
        rect_t b = row_ctrl(c, 1, 170, 36);
        ui_button(c, R(b.x - 140, b.y, 130, 36), "Retirer", BTN_NORMAL, p->hover == H_PASS_DEL);
    }
}

static void draw_system(prefs_t *p, canvas_t *c) {
    sysinfo_t si;
    plat_sysinfo(&si);
    char sub[160];
    snprintf(sub, sizeof sub, "Noyau Linux %s · démarrage %s", si.kernel, si.boot);
    row(c, 0, "TitanOs " TITANOS_VERSION, sub);
    char msg[160];
    int st = plat_update_state(msg, sizeof msg, NULL);
    row(c, 1, "Mises à jour", st == UPD_IDLE ? "Automatiques, depuis GitHub" : msg);
    ui_button(c, row_ctrl(c, 1, 220, 36), "Ouvrir Mises à jour", BTN_NORMAL, p->hover == H_UPDATES);
    snprintf(sub, sizeof sub, "%s · %d cœurs", si.cpu, si.cores);
    row(c, 2, "Processeur", sub);
    row(c, 3, "Vos fichiers", plat_storage());
    row(c, 4, "Matériel et processus", "Détails dans l'application Système");
    ui_button(c, row_ctrl(c, 4, 220, 36), "Ouvrir Système", BTN_NORMAL, p->hover == H_ABOUT);
}

static void prefs_draw(window_t *w, canvas_t *c) {
    prefs_t *p = w->data;
    /* barre latérale */
    gfx_fill(c, R(SIDE_W, 0, 1, c->h), 0x18FFFFFF);
    for (int i = 0; i < P_COUNT; i++) {
        rect_t r = side_item(i);
        bool sel = i == p->page;
        if (sel) gfx_rrect(c, r.x, r.y, r.w, r.h, 12, (accent() & 0xFFFFFF) | 0x60000000);
        else if (p->hover == H_SIDE0 + i) gfx_rrect(c, r.x, r.y, r.w, r.h, 12, 0x16FFFFFF);
        if (sel) gfx_rrect(c, r.x + 4, r.y + 14, 4, r.h - 28, 2, accent());
        text_draw(c, sel ? &font_bold : &font_body, r.x + 18, r.y + 15, page_names[i], sel ? C_TEXT : C_TEXT2);
    }
    page_title(c, p->page);
    switch (p->page) {
    case P_LOOK: draw_look(p, c); break;
    case P_SCREEN: draw_screen(p, c); break;
    case P_SOUND: draw_sound(p, c); break;
    case P_INPUT: draw_input(p, c); break;
    case P_NET: draw_net(p, c); break;
    case P_TIME: draw_time(p, c); break;
    case P_ACCOUNT: draw_account(p, c); break;
    case P_SYSTEM: draw_system(p, c); break;
    }
}

/* ---------------- zones cliquables ---------------- */
static int hit(prefs_t *p, canvas_t *c, int x, int y) {
    for (int i = 0; i < P_COUNT; i++) if (rect_has(side_item(i), x, y)) return H_SIDE0 + i;
    switch (p->page) {
    case P_LOOK: {
        for (int i = 0; i < WALLPAPER_COUNT; i++) if (rect_has(wall_rect(i), x, y)) return H_WALL0 + i;
        for (int i = 0; i < ACCENT_COUNT; i++) if (rect_has(acc_rect(i), x, y)) return H_ACC0 + i;
        int g = ui_segment_at(glass_rect(), 3, x, y);
        if (g >= 0) return H_GLASS0 + g;
        break;
    }
    case P_SCREEN: if (rect_has(row_ctrl(c, 0, 300, 40), x, y)) return H_BRIGHT; break;
    case P_SOUND:
        if (rect_has(row_ctrl(c, 0, 300, 40), x, y)) return H_VOL;
        if (rect_has(row_ctrl(c, 1, 52, 30), x, y)) return H_SOUNDS;
        break;
    case P_INPUT: {
        int l = ui_segment_at(row_ctrl(c, 0, 280, 36), 2, x, y);
        if (l >= 0) return H_LAYOUT0 + l;
        if (rect_has(row_ctrl(c, 1, 300, 40), x, y)) return H_PTR;
        break;
    }
    case P_NET: if (rect_has(row_ctrl(c, 0, 220, 36), x, y)) return H_NETOPEN; break;
    case P_TIME: {
        rect_t a = row_ctrl(c, 0, 360, 34);
        if (rect_has(R(a.x, a.y, 34, 34), x, y)) return H_TZ_PREV;
        if (rect_has(R(a.x + a.w - 34, a.y, 34, 34), x, y)) return H_TZ_NEXT;
        if (rect_has(row_ctrl(c, 1, 220, 36), x, y)) return H_NTP;
        if (rect_has(row_ctrl(c, 2, 52, 30), x, y)) return H_SECONDS;
        if (rect_has(row_ctrl(c, 3, 52, 30), x, y)) return H_RTC;
        break;
    }
    case P_ACCOUNT: {
        if (rect_has(row_ctrl(c, 0, 300, 40), x, y)) return H_NAME;
        rect_t b = row_ctrl(c, 1, 170, 36);
        if (rect_has(b, x, y)) return H_PASS;
        if (g_set.pass[0] && rect_has(R(b.x - 140, b.y, 130, 36), x, y)) return H_PASS_DEL;
        break;
    }
    case P_SYSTEM:
        if (rect_has(row_ctrl(c, 1, 220, 36), x, y)) return H_UPDATES;
        if (rect_has(row_ctrl(c, 4, 220, 36), x, y)) return H_ABOUT;
        break;
    }
    return H_NONE;
}

/* ---------------- actions ---------------- */
static void pass_cb(const char *pw, void *ctx) {
    window_t *w = ctx;
    if (strlen(pw) < 4) { dlg_message("Mot de passe", "Choisissez au moins 4 caractères."); return; }
    settings_pass_hash(pw, g_set.pass);
    settings_save();
    wm_toast("Mot de passe enregistré : il sera demandé pour ouvrir la session.");
    if (w && w->used) wm_invalidate(w);
}

static void commit_name(prefs_t *p) {
    if (p->name.s[0] && strcmp(p->name.s, g_set.user)) {
        strlcpy(g_set.user, p->name.s, sizeof g_set.user);
        settings_save();
        wm_toast("Nom d'utilisateur enregistré.");
    }
}

static void slide(prefs_t *p, canvas_t *c, int id, int x) {
    if (id == H_BRIGHT) {
        int v = MAX(5, ui_slider_value(row_ctrl(c, 0, 300, 40), x));
        plat_brightness(v);
        g_set.brightness = v;
    } else if (id == H_VOL) {
        g_set.volume = ui_slider_value(row_ctrl(c, 0, 300, 40), x);
        plat_volume(g_set.volume);
    } else if (id == H_PTR) {
        g_set.pointer = 1 + (ui_slider_value(row_ctrl(c, 1, 300, 40), x) + 12) / 25;
    }
    (void)p;
}

static void prefs_event(window_t *w, const event_t *e) {
    prefs_t *p = w->data;
    canvas_t *c = &w->surf;
    if (e->type == EV_MOTION) {
        if (p->drag) { slide(p, c, p->drag, e->x); wm_invalidate(w); return; }
        int h = e->x < -500 ? H_NONE : hit(p, c, e->x, e->y);
        if (h != p->hover) { p->hover = h; wm_invalidate(w); }
        return;
    }
    if (e->type == EV_BUTTON && !e->down) {
        if (p->drag == H_VOL) sound_play(SFX_TICK);
        if (p->drag) settings_save();
        p->drag = 0;
        return;
    }
    if (e->type == EV_BUTTON && e->down && e->button == BTN_LEFT) {
        int h = hit(p, c, e->x, e->y);
        if (h && h != H_NAME && h != H_PTR && h != H_BRIGHT && h != H_VOL) sound_play(SFX_CLICK);
        if (p->name.focus && h != H_NAME) { commit_name(p); p->name.focus = false; }
        bool changed = true, theme = false;
        if (h >= H_SIDE0 && h < H_SIDE0 + P_COUNT) { p->page = h - H_SIDE0; changed = false; }
        else if (h >= H_WALL0 && h < H_WALL0 + WALLPAPER_COUNT) { g_set.wallpaper = h - H_WALL0; theme = true; }
        else if (h >= H_ACC0 && h < H_ACC0 + ACCENT_COUNT) { g_set.accent = h - H_ACC0; theme = true; }
        else if (h >= H_GLASS0 && h < H_GLASS0 + 3) { g_set.glass = h - H_GLASS0; theme = true; }
        else if (h == H_BRIGHT && plat_brightness(-1) >= 0) { p->drag = h; slide(p, c, h, e->x); }
        else if (h == H_VOL && plat_volume(-1) >= 0) { p->drag = h; slide(p, c, h, e->x); }
        else if (h == H_PTR) { p->drag = h; slide(p, c, h, e->x); }
        else if (h == H_SOUNDS) g_set.sounds = !g_set.sounds;
        else if (h >= H_LAYOUT0 && h < H_LAYOUT0 + 2) { if (g_set.layout != h - H_LAYOUT0) wm_kbd_layout_toggle(); changed = false; }
        else if (h == H_NETOPEN) { app_network_open(); changed = false; }
        else if (h == H_TZ_PREV) { g_set.tz = (g_set.tz + tz_count - 1) % tz_count; g_set.rtc_local = false; }
        else if (h == H_TZ_NEXT) { g_set.tz = (g_set.tz + 1) % tz_count; g_set.rtc_local = false; }
        else if (h == H_SECONDS) g_set.seconds = !g_set.seconds;
        else if (h == H_RTC) g_set.rtc_local = !g_set.rtc_local;
        else if (h == H_NTP && p->ntp_job < 0) {
            static const char *const argv[] = { "/bin/busybox", "ntpd", "-n", "-q", "-p", "pool.ntp.org", NULL };
            p->ntp_job = plat_spawn(argv);
            if (p->ntp_job < 0) wm_toast("Impossible de lancer la synchronisation.");
            changed = false;
        }
        else if (h == H_NAME) { p->name.focus = true; changed = false; }
        else if (h == H_PASS) { dlg_password("Mot de passe de session", "Nouveau mot de passe (4 caractères au moins)", pass_cb, w); changed = false; }
        else if (h == H_PASS_DEL) { g_set.pass[0] = 0; wm_toast("Mot de passe retiré."); }
        else if (h == H_UPDATES) { app_update_open(); changed = false; }
        else if (h == H_ABOUT) { app_system_open(); changed = false; }
        else changed = false;
        if (changed) { settings_save(); if (theme) wm_theme_changed(); }
        wm_invalidate(w);
        return;
    }
    if (e->type == EV_KEY && e->down && p->name.focus) {
        if (e->key == K_ENTER) { commit_name(p); p->name.focus = false; }
        else if (e->key == K_ESC) { ui_field_set(&p->name, g_set.user); p->name.focus = false; }
        else ui_field_key(&p->name, e);
        wm_invalidate(w);
    }
}

static void prefs_tick(window_t *w, uint64_t now) {
    prefs_t *p = w->data;
    /* pages qui montrent un état extérieur (réseau, mises à jour) : relues régulièrement */
    if ((p->page == P_NET || p->page == P_SYSTEM) && now >= p->next) { p->next = now + 1500; wm_invalidate(w); }
    if (p->ntp_job < 0) return;
    char out[256];
    int r = plat_spawn_poll(p->ntp_job, out, sizeof out);
    if (r == -1) return;
    p->ntp_job = -1;
    if (r == 0) {
        g_set.rtc_local = false;      /* l'horloge système est maintenant en temps universel */
        settings_save();
        plat_rtc_save();              /* et l'horloge du PC aussi : plus de décalage au redémarrage */
        wm_toast("Heure mise à jour depuis Internet.");
    } else {
        wm_toast("Échec : vérifiez la connexion Internet.");
    }
    wm_invalidate(w);
}

static void prefs_close(window_t *w) {
    prefs_t *p = w->data;
    for (int i = 0; i < WALLPAPER_COUNT; i++) canvas_free(&p->thumbs[i]);
    free(p);
}

void app_settings_open(void) {
    window_t *w = wm_find(ICON_SETTINGS);
    if (w) { wm_focus(w); return; }
    prefs_t *p = calloc(1, sizeof *p);
    if (!p) return;
    p->ntp_job = -1;
    ui_field_set(&p->name, g_set.user);
    w = wm_open("Paramètres", ICON_SETTINGS, WIN_W, WIN_H, WF_SINGLE);
    if (!w) { free(p); return; }
    w->data = p;
    w->draw = prefs_draw;
    w->event = prefs_event;
    w->tick = prefs_tick;
    w->on_close = prefs_close;
}
