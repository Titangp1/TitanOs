/* TitanOs - Paramètres : apparence (fond, couleur, verre, nom) et système (clavier, heure, écran) */
#include "desk.h"

typedef struct {
    int tab, hover;
    field_t name;
    canvas_t thumbs[WALLPAPER_COUNT];
    int ntp_job;
    bool dragging_bright, dragging_ptr, dragging_vol;
} prefs_t;

enum {   /* zones cliquables */
    H_TAB0 = 1, H_TAB1, H_WALL0 = 10, H_ACC0 = 20, H_GLASS0 = 30, H_NAME = 40, H_SECONDS, H_LAYOUT0 = 50,
    H_PTR, H_BRIGHT, H_VOL, H_SOUNDS, H_TZ_PREV, H_TZ_NEXT, H_RTC, H_NTP,
};

static rect_t tabs_rect(canvas_t *c) { return R(c->w / 2 - 150, 12, 300, 36); }
#define TW 132
#define TH 76
static rect_t wall_rect(int i) { return R(28 + i * (TW + 14), 100, TW, TH); }
static rect_t acc_rect(int i) { return R(28 + i * 44, 250, 34, 34); }
static rect_t glass_rect(void) { return R(28, 330, 330, 36); }
static rect_t name_rect(void) { return R(28, 416, 300, 40); }
static rect_t seconds_rect(canvas_t *c) { return R(c->w - 28 - 52, 418, 52, 30); }

static rect_t row_rect(canvas_t *c, int i) { return R(20, 62 + i * 60, c->w - 40, 52); }
static rect_t layout_rect(canvas_t *c) { rect_t r = row_rect(c, 0); return R(r.x + r.w - 290, r.y + 8, 280, 36); }
static rect_t ptr_rect(canvas_t *c) { rect_t r = row_rect(c, 1); return R(r.x + r.w - 290, r.y + 6, 280, 40); }
static rect_t bright_rect(canvas_t *c) { rect_t r = row_rect(c, 2); return R(r.x + r.w - 290, r.y + 6, 280, 40); }
static rect_t vol_rect(canvas_t *c) { rect_t r = row_rect(c, 3); return R(r.x + r.w - 290, r.y + 6, 280, 40); }
static rect_t sounds_rect(canvas_t *c) { rect_t r = row_rect(c, 4); return R(r.x + r.w - 62, r.y + 11, 52, 30); }
static rect_t tz_prev(canvas_t *c) { rect_t r = row_rect(c, 5); return R(r.x + r.w - 330, r.y + 9, 34, 34); }
static rect_t tz_next(canvas_t *c) { rect_t r = row_rect(c, 5); return R(r.x + r.w - 44, r.y + 9, 34, 34); }
static rect_t rtc_rect(canvas_t *c) { rect_t r = row_rect(c, 6); return R(r.x + r.w - 62, r.y + 11, 52, 30); }
static rect_t ntp_rect(canvas_t *c) { rect_t r = row_rect(c, 7); return R(r.x + r.w - 230, r.y + 8, 220, 36); }
#define APPEARANCE_H 480
#define SYSTEM_H     556

static void section(canvas_t *c, int y, const char *t) { text_draw(c, &font_bold, 28, y, t, C_TEXT2); }

static void draw_appearance(prefs_t *p, canvas_t *c) {
    section(c, 72, "Fond d'écran");
    for (int i = 0; i < WALLPAPER_COUNT; i++) {
        rect_t r = wall_rect(i);
        if (!p->thumbs[i].px) {
            p->thumbs[i] = canvas_new(TW, TH);
            wallpaper_thumb(&p->thumbs[i], 0, 0, TW, TH, i);
            /* coins arrondis de la vignette */
            canvas_t m = canvas_new(TW, TH);
            gfx_rrect(&m, 0, 0, TW, TH, 12, 0xFFFFFFFF);
            for (int k = 0; k < TW * TH; k++) {
                uint32_t a = m.px[k] >> 24, v = p->thumbs[i].px[k];
                uint32_t rb = ((v & 0xFF00FF) * a >> 8) & 0xFF00FF, ag = (((v >> 8) & 0xFF00FF) * a) & 0xFF00FF00;
                p->thumbs[i].px[k] = rb | ag;
            }
            canvas_free(&m);
        }
        gfx_blit(c, r.x, r.y, &p->thumbs[i], 255);
        if (i == g_set.wallpaper) gfx_rrect_stroke(c, r.x - 3, r.y - 3, r.w + 6, r.h + 6, 15, 2.5f, accent());
        else if (p->hover == H_WALL0 + i) gfx_rrect_stroke(c, r.x - 2, r.y - 2, r.w + 4, r.h + 4, 14, 1.5f, 0x80FFFFFF);
        text_center(c, &font_small, R(r.x, r.y + r.h + 6, r.w, 18), wallpaper_name(i), i == g_set.wallpaper ? C_TEXT : C_TEXT2);
    }
    section(c, 222, "Couleur d'accent");
    for (int i = 0; i < ACCENT_COUNT; i++) {
        rect_t r = acc_rect(i);
        float cx = r.x + 17, cy = r.y + 17;
        if (i == g_set.accent) gfx_ring(c, cx, cy, 17, 2.5f, 0xFFFFFFFF);
        gfx_circle(c, cx, cy, i == g_set.accent ? 12 : (p->hover == H_ACC0 + i ? 14 : 13), accent_of(i));
    }
    section(c, 302, "Effet verre");
    static const char *gl[3] = { "Transparent", "Normal", "Dépoli" };
    ui_segments(c, glass_rect(), gl, 3, g_set.glass, p->hover >= H_GLASS0 && p->hover < H_GLASS0 + 3 ? p->hover - H_GLASS0 : -1);
    section(c, 390, "Nom de l'utilisateur");
    ui_field(c, name_rect(), &p->name, NULL);
    text_draw(c, &font_small, 340, 428, p->name.focus ? "Entrée pour valider" : "", C_MUTED);
    text_draw(c, &font_body, c->w - 28 - 52 - 12 - text_width(&font_body, "Secondes sur l'horloge"), 424, "Secondes sur l'horloge", C_TEXT);
    ui_toggle(c, seconds_rect(c), g_set.seconds);
}

static void row_label(canvas_t *c, rect_t r, const char *t, const char *sub) {
    ui_panel(c, r, 14);
    text_draw(c, &font_bold, r.x + 16, r.y + (sub ? 8 : 16), t, C_TEXT);
    if (sub) text_draw(c, &font_small, r.x + 16, r.y + 29, sub, C_MUTED);
}

static void draw_system(prefs_t *p, canvas_t *c) {
    row_label(c, row_rect(c, 0), "Disposition du clavier", "Aussi depuis la barre : FR / US");
    static const char *lay[2] = { "AZERTY (FR)", "QWERTY (US)" };
    ui_segments(c, layout_rect(c), lay, 2, g_set.layout, p->hover >= H_LAYOUT0 && p->hover < H_LAYOUT0 + 2 ? p->hover - H_LAYOUT0 : -1);
    row_label(c, row_rect(c, 1), "Vitesse du pointeur", NULL);
    ui_slider(c, ptr_rect(c), (g_set.pointer - 1) * 25);
    int b = plat_brightness(-1);
    row_label(c, row_rect(c, 2), "Luminosité de l'écran", b < 0 ? "Non réglable sur cet écran" : NULL);
    if (b >= 0) ui_slider(c, bright_rect(c), b);
    int v = plat_volume(-1);
    row_label(c, row_rect(c, 3), "Volume", v < 0 ? "Aucune carte son détectée" : NULL);
    if (v >= 0) ui_slider(c, vol_rect(c), g_set.volume);
    row_label(c, row_rect(c, 4), "Sons de l'interface", "Clics, notifications, démarrage");
    ui_toggle(c, sounds_rect(c), g_set.sounds);
    row_label(c, row_rect(c, 5), "Fuseau horaire", NULL);
    rect_t a = tz_prev(c), z = tz_next(c);
    gfx_rrect(c, a.x, a.y, a.w, a.h, 17, p->hover == H_TZ_PREV ? 0x38FFFFFF : 0x22FFFFFF);
    gfx_rrect(c, z.x, z.y, z.w, z.h, 17, p->hover == H_TZ_NEXT ? 0x38FFFFFF : 0x22FFFFFF);
    text_center(c, &font_bold, a, "←", C_TEXT);
    text_center(c, &font_bold, z, "→", C_TEXT);
    text_center(c, &font_body, R(a.x + a.w, a.y, z.x - a.x - a.w, a.h), tz_list[g_set.tz].name, C_TEXT);
    row_label(c, row_rect(c, 6), "Horloge du PC en heure locale", "Comme Windows. Désactivez si l'heure est décalée.");
    ui_toggle(c, rtc_rect(c), g_set.rtc_local);
    row_label(c, row_rect(c, 7), "Heure par Internet", p->ntp_job >= 0 ? "Synchronisation en cours…" : "Serveur pool.ntp.org");
    ui_button(c, ntp_rect(c), "Mettre à l'heure", BTN_NORMAL, p->hover == H_NTP);
}

static void prefs_draw(window_t *w, canvas_t *c) {
    prefs_t *p = w->data;
    static const char *tabs[2] = { "Apparence", "Système" };
    ui_segments(c, tabs_rect(c), tabs, 2, p->tab, p->hover == H_TAB0 ? 0 : p->hover == H_TAB1 ? 1 : -1);
    if (p->tab == 0) draw_appearance(p, c);
    else draw_system(p, c);
}

static int hit(prefs_t *p, canvas_t *c, int x, int y) {
    int t = ui_segment_at(tabs_rect(c), 2, x, y);
    if (t >= 0) return H_TAB0 + t;
    if (p->tab == 0) {
        for (int i = 0; i < WALLPAPER_COUNT; i++) if (rect_has(wall_rect(i), x, y)) return H_WALL0 + i;
        for (int i = 0; i < ACCENT_COUNT; i++) if (rect_has(acc_rect(i), x, y)) return H_ACC0 + i;
        int g = ui_segment_at(glass_rect(), 3, x, y);
        if (g >= 0) return H_GLASS0 + g;
        if (rect_has(name_rect(), x, y)) return H_NAME;
        if (rect_has(seconds_rect(c), x, y)) return H_SECONDS;
    } else {
        int l = ui_segment_at(layout_rect(c), 2, x, y);
        if (l >= 0) return H_LAYOUT0 + l;
        if (rect_has(ptr_rect(c), x, y)) return H_PTR;
        if (rect_has(bright_rect(c), x, y)) return H_BRIGHT;
        if (rect_has(vol_rect(c), x, y)) return H_VOL;
        if (rect_has(sounds_rect(c), x, y)) return H_SOUNDS;
        if (rect_has(tz_prev(c), x, y)) return H_TZ_PREV;
        if (rect_has(tz_next(c), x, y)) return H_TZ_NEXT;
        if (rect_has(rtc_rect(c), x, y)) return H_RTC;
        if (rect_has(ntp_rect(c), x, y)) return H_NTP;
    }
    return 0;
}

static void commit_name(prefs_t *p) {
    if (p->name.s[0] && strcmp(p->name.s, g_set.user)) {
        strlcpy(g_set.user, p->name.s, sizeof g_set.user);
        settings_save();
        wm_toast("Nom d'utilisateur enregistré.");
    }
}

static void prefs_event(window_t *w, const event_t *e) {
    prefs_t *p = w->data;
    canvas_t *c = &w->surf;
    if (e->type == EV_MOTION) {
        if (p->dragging_bright) { int v = MAX(5, ui_slider_value(bright_rect(c), e->x)); plat_brightness(v); g_set.brightness = v; wm_invalidate(w); return; }
        if (p->dragging_ptr) { g_set.pointer = 1 + (ui_slider_value(ptr_rect(c), e->x) + 12) / 25; wm_invalidate(w); return; }
        if (p->dragging_vol) { g_set.volume = ui_slider_value(vol_rect(c), e->x); plat_volume(g_set.volume); wm_invalidate(w); return; }
        int h = e->x < -500 ? 0 : hit(p, c, e->x, e->y);
        if (h != p->hover) { p->hover = h; wm_invalidate(w); }
        return;
    }
    if (e->type == EV_BUTTON && !e->down) {
        if (p->dragging_vol) sound_play(SFX_TICK);
        if (p->dragging_bright || p->dragging_ptr || p->dragging_vol) settings_save();
        p->dragging_bright = p->dragging_ptr = p->dragging_vol = false;
        return;
    }
    if (e->type == EV_BUTTON && e->down && e->button == BTN_LEFT) {
        int h = hit(p, c, e->x, e->y);
        if (h && h != H_NAME && h != H_PTR && h != H_BRIGHT) sound_play(SFX_CLICK);
        if (p->name.focus && h != H_NAME) { commit_name(p); p->name.focus = false; }
        bool changed = true, theme = false;
        if (h == H_TAB0 || h == H_TAB1) { p->tab = h - H_TAB0; wm_resize(w, c->w, p->tab ? SYSTEM_H : APPEARANCE_H); }
        else if (h >= H_WALL0 && h < H_WALL0 + WALLPAPER_COUNT) { g_set.wallpaper = h - H_WALL0; theme = true; }
        else if (h >= H_ACC0 && h < H_ACC0 + ACCENT_COUNT) { g_set.accent = h - H_ACC0; theme = true; }
        else if (h >= H_GLASS0 && h < H_GLASS0 + 3) { g_set.glass = h - H_GLASS0; theme = true; }
        else if (h == H_NAME) { p->name.focus = true; changed = false; }
        else if (h == H_SECONDS) g_set.seconds = !g_set.seconds;
        else if (h >= H_LAYOUT0 && h < H_LAYOUT0 + 2) { if (g_set.layout != h - H_LAYOUT0) wm_kbd_layout_toggle(); changed = false; }
        else if (h == H_PTR) { p->dragging_ptr = true; g_set.pointer = 1 + (ui_slider_value(ptr_rect(c), e->x) + 12) / 25; }
        else if (h == H_BRIGHT && plat_brightness(-1) >= 0) {
            p->dragging_bright = true;
            int v = MAX(5, ui_slider_value(bright_rect(c), e->x));
            plat_brightness(v);
            g_set.brightness = v;
        }
        else if (h == H_VOL && plat_volume(-1) >= 0) {
            p->dragging_vol = true;
            g_set.volume = ui_slider_value(vol_rect(c), e->x);
            plat_volume(g_set.volume);
        }
        else if (h == H_SOUNDS) g_set.sounds = !g_set.sounds;
        else if (h == H_TZ_PREV) { g_set.tz = (g_set.tz + tz_count - 1) % tz_count; g_set.rtc_local = false; }
        else if (h == H_TZ_NEXT) { g_set.tz = (g_set.tz + 1) % tz_count; g_set.rtc_local = false; }
        else if (h == H_RTC) g_set.rtc_local = !g_set.rtc_local;
        else if (h == H_NTP && p->ntp_job < 0) {
            static const char *const argv[] = { "/bin/busybox", "ntpd", "-n", "-q", "-p", "pool.ntp.org", NULL };
            p->ntp_job = plat_spawn(argv);
            if (p->ntp_job < 0) wm_toast("Impossible de lancer la synchronisation.");
        }
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
    (void)now;
    prefs_t *p = w->data;
    if (p->ntp_job < 0) return;
    char out[256];
    int r = plat_spawn_poll(p->ntp_job, out, sizeof out);
    if (r == -1) return;
    p->ntp_job = -1;
    if (r == 0) {
        g_set.rtc_local = false;      /* l'horloge système est maintenant en temps universel */
        settings_save();
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
    w = wm_open("Paramètres", ICON_SETTINGS, 900, APPEARANCE_H, WF_SINGLE);
    if (!w) { free(p); return; }
    w->data = p;
    w->draw = prefs_draw;
    w->event = prefs_event;
    w->tick = prefs_tick;
    w->on_close = prefs_close;
}
