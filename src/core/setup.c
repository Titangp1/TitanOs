/* TitanOs - assistant d'installation (clé d'installation)
 *
 * Démarrée depuis la clé d'installation, TitanOs n'ouvre pas de session : cet assistant
 * plein écran installe TitanOs sur un disque de l'ordinateur, comme l'installation de
 * Windows. Étapes : bienvenue -> choix du disque -> confirmation -> copie -> terminé.
 * Si un TitanOs est déjà installé sur un disque, il peut aussi être mis à jour en gardant
 * les fichiers de l'utilisateur. Le travail lui-même est fait par la partie système
 * (plat_install_start, plat_update_other : src/linux/update.c).
 *
 * Le même écran sert à l'accueil du premier démarrage (comme la configuration de Windows) :
 * clavier, fuseau horaire, Internet, nom, mot de passe, fond d'écran et couleur. */
#include "desk.h"

static int flow;                          /* 0 : installation (clé), 1 : accueil du premier démarrage */
static void w_draw(canvas_t *c, rect_t r);
static bool w_motion(int x, int y);
static bool w_click(int x, int y);
static bool w_key(const event_t *e);
static bool w_tick(void);

enum { S_WELCOME, S_DISK, S_CONFIRM, S_RUN, S_DONE, S_ERROR };
enum { B_NONE = -1, B_NEXT = 100, B_BACK, B_UPDATE, B_OFF, B_REBOOT };

static int step, hover = B_NONE, sel = -1, ndisk;
static disk_info_t disks[8];
static char other[96];
static bool has_other, updating;          /* mise à jour d'un TitanOs déjà installé */
static ui_progress_t prog;
static char msg[200];
static int pc = -1, last_pc = -2;
static rect_t card;

rect_t setup_rect(int sw, int sh) {
    int w = MIN(sw - 80, 780), h = MIN(sh - 80, 540);
    return R((sw - w) / 2, (sh - h) / 2, w, h);
}

static void load_disks(void) {
    disk_info_t all[8];
    int n = plat_install_disks(all, 8);
    ndisk = 0;
    for (int i = 0; i < n; i++) if (!all[i].current) disks[ndisk++] = all[i];
    if (sel >= ndisk) sel = -1;
    if (sel < 0) for (int i = 0; i < ndisk && sel < 0; i++) if (!disks[i].removable) sel = i;   /* le disque interne d'abord */
    has_other = plat_other_titanos(other, sizeof other);
}

void setup_init(void) {
    flow = 0;
    step = S_WELCOME;
    load_disks();
}

static void size_text(uint64_t bytes, char *out, int n) {
    uint64_t gb = bytes / 1000000000ull;
    if (gb >= 1000) snprintf(out, (size_t)n, "%d,%d To", (int)(gb / 1000), (int)(gb % 1000 / 100));
    else snprintf(out, (size_t)n, "%d Go", (int)gb);
}

/* ---------------- disposition ---------------- */
static rect_t btn_main(void) { return R(card.x + card.w - 36 - 230, card.y + card.h - 36 - 44, 230, 44); }
static rect_t btn_second(void) { return R(card.x + card.w - 36 - 230 - 14 - 170, card.y + card.h - 36 - 44, 170, 44); }
static rect_t btn_off(void) { return R(card.x + 36, card.y + card.h - 36 - 44, 130, 44); }
static rect_t btn_update(void) { return R(card.x + 36, card.y + card.h - 36 - 44 - 58, card.w - 72, 44); }
static rect_t disk_row(int i) { return R(card.x + 36, card.y + 150 + i * 72, card.w - 72, 62); }

static int buttons(int *ids, rect_t *rs) {
    int n = 0;
    switch (step) {
    case S_WELCOME:
        ids[n] = B_NEXT; rs[n++] = btn_main();
        ids[n] = B_OFF; rs[n++] = btn_off();
        if (has_other) { ids[n] = B_UPDATE; rs[n++] = btn_update(); }
        break;
    case S_DISK:
        if (sel >= 0) { ids[n] = B_NEXT; rs[n++] = btn_main(); }
        ids[n] = B_BACK; rs[n++] = btn_second();
        break;
    case S_CONFIRM:
        ids[n] = B_NEXT; rs[n++] = btn_main();
        ids[n] = B_BACK; rs[n++] = btn_second();
        break;
    case S_DONE:
        ids[n] = B_REBOOT; rs[n++] = btn_main();
        ids[n] = B_OFF; rs[n++] = btn_off();
        break;
    case S_ERROR:
        ids[n] = B_BACK; rs[n++] = btn_main();
        ids[n] = B_OFF; rs[n++] = btn_off();
        break;
    }
    return n;
}

/* ---------------- dessin ---------------- */
static void paragraph(canvas_t *c, int x, int y, int w, const char *t, uint32_t col) {
    /* texte coupé aux espaces, sur plusieurs lignes */
    const font_t *f = &font_body;
    while (*t) {
        int n = (int)strlen(t), cut = n;
        if (text_width(f, t) > w) {
            cut = 0;
            for (int i = 1; i <= n; i++) if ((t[i] == ' ' || !t[i]) && text_width_n(f, t, i) <= w) cut = i;
            if (!cut) cut = n;
        }
        text_draw_n(c, f, x, y, t, cut, col);
        y += f->line + 2;
        t += cut;
        while (*t == ' ') t++;
    }
}

static void header(canvas_t *c, const char *title, const char *sub) {
    draw_logo(c, card.x + 36 + 26, card.y + 34, 52, 255);
    text_draw(c, &font_h2, card.x + 104, card.y + 36, title, C_TEXT);
    if (sub) text_draw(c, &font_small, card.x + 104, card.y + 70, sub, C_MUTED);
}

void setup_draw(canvas_t *c, rect_t r) {
    if (flow) { w_draw(c, r); return; }
    card = r;
    int x = card.x + 36, w = card.w - 72, y = card.y + 120;
    char t[400], sz[24];
    switch (step) {
    case S_WELCOME:
        header(c, "Installer TitanOs", "TitanOs " TITANOS_VERSION " · base Debian 13 · noyau Linux");
        paragraph(c, x, y, w, "Cette clé installe TitanOs sur un disque de cet ordinateur. Le système ne tourne pas "
                  "depuis la clé : une fois l'installation finie, retirez-la et l'ordinateur démarre sur TitanOs.", C_TEXT2);
        paragraph(c, x, y + 78, w, "Le disque choisi sera entièrement effacé. Comptez quelques minutes pour la copie, "
                  "puis quelques minutes au premier démarrage, le temps que Debian s'installe.", C_MUTED);
        if (has_other) {
            snprintf(t, sizeof t, "TitanOs est déjà installé sur %s : il peut être mis à jour sans toucher à vos fichiers.", other);
            paragraph(c, x, btn_update().y - 54, w, t, C_TEXT2);
        }
        break;
    case S_DISK:
        header(c, "Où installer TitanOs ?", "Choisissez le disque : il sera entièrement effacé.");
        if (!ndisk) paragraph(c, x, y + 30, w, "Aucun disque utilisable (16 Go au moins) n'a été trouvé. Branchez-en un, puis revenez en arrière et recommencez.", 0xFFFCA5A5);
        for (int i = 0; i < ndisk; i++) {
            rect_t row = disk_row(i);
            if (row.y + row.h > btn_main().y - 12) break;
            bool on = i == sel;
            gfx_rrect(c, row.x, row.y, row.w, row.h, 14, on ? ((accent() & 0xFFFFFF) | 0x55000000) : hover == i ? 0x22FFFFFF : 0x12FFFFFF);
            if (on) gfx_rrect_stroke(c, row.x, row.y, row.w, row.h, 14, 2, accent());
            draw_icon(c, ICON_SYSTEM, row.x + 14, row.y + 13, 36);
            text_fit(c, &font_bold, row.x + 64, row.y + 11, row.w - 220, disks[i].model, C_TEXT);
            size_text(disks[i].bytes, sz, sizeof sz);
            snprintf(t, sizeof t, "%s · /dev/%s%s", disks[i].removable ? "Clé ou disque USB" : "Disque interne", disks[i].dev,
                     disks[i].bytes < (16ull << 30) ? " · trop petit" : "");
            text_draw(c, &font_small, row.x + 64, row.y + 35, t, C_MUTED);
            text_draw(c, &font_title, row.x + row.w - 16 - text_width(&font_title, sz), row.y + 19, sz, C_TEXT);
        }
        break;
    case S_CONFIRM: {
        header(c, "Tout effacer sur ce disque ?", "Dernière étape avant l'installation.");
        disk_info_t *d = &disks[sel];
        size_text(d->bytes, sz, sizeof sz);
        snprintf(t, sizeof t, "Le disque « %s » (%s) va être entièrement effacé : tout ce qu'il contient, Windows compris, "
                 "sera perdu définitivement.", d->model, sz);
        paragraph(c, x, y, w, t, 0xFFFCA5A5);
        uint64_t apps = d->bytes / 3;
        apps = MAX(apps, 8ull << 30);
        apps = MIN(apps, 64ull << 30);
        char a[24], f[24];
        size_text(apps, a, sizeof a);
        size_text(d->bytes - apps, f, sizeof f);
        snprintf(t, sizeof t, "Le disque sera partagé ainsi : environ %s pour TitanOs et vos fichiers, %s pour les applications (Debian).", f, a);
        paragraph(c, x, y + 84, w, t, C_TEXT2);
        paragraph(c, x, y + 140, w, "Pendant la copie, ne retirez pas la clé et n'éteignez pas l'ordinateur.", C_MUTED);
        break;
    }
    case S_RUN:
        header(c, updating ? "Mise à jour de TitanOs…" : "Installation de TitanOs…", "Ne retirez pas la clé et n'éteignez pas l'ordinateur.");
        text_fit(c, &font_body, x, y + 70, w, msg[0] ? msg : "Préparation…", C_TEXT2);
        ui_progress(c, R(x, y + 106, w, 10), &prog, pc);
        break;
    case S_DONE:
        header(c, updating ? "TitanOs est à jour" : "TitanOs est installé", "Tout s'est bien passé.");
        paragraph(c, x, y, w, msg, 0xFF86EFAC);
        paragraph(c, x, y + 84, w, "Retirez la clé d'installation, puis redémarrez. Si l'ordinateur redémarre encore sur la clé, "
                  "choisissez le disque dans le menu de démarrage du BIOS (touche F11 sur les VAIO, ou F2 → Boot).", C_TEXT2);
        break;
    case S_ERROR:
        header(c, "L'opération n'a pas abouti", "Rien n'est perdu sur la clé : vous pouvez réessayer.");
        paragraph(c, x, y, w, msg, 0xFFFCA5A5);
        paragraph(c, x, y + 84, w, "Le détail est dans journal-titanos.txt, sur la clé.", C_MUTED);
        break;
    }
    int ids[4];
    rect_t rs[4];
    int nb = buttons(ids, rs);
    for (int i = 0; i < nb; i++) {
        const char *label = ids[i] == B_NEXT ? (step == S_WELCOME ? "Installer TitanOs" : step == S_CONFIRM ? "Effacer et installer" : "Suivant")
                          : ids[i] == B_BACK ? (step == S_ERROR ? "Recommencer" : "Retour")
                          : ids[i] == B_UPDATE ? "Mettre à jour le TitanOs déjà installé (fichiers gardés)"
                          : ids[i] == B_REBOOT ? "Redémarrer" : "Éteindre";
        int style = ids[i] == B_NEXT && step == S_CONFIRM ? BTN_DANGER : ids[i] == B_NEXT || ids[i] == B_REBOOT ? BTN_ACCENT : BTN_NORMAL;
        if (ids[i] == B_BACK && step == S_ERROR) style = BTN_ACCENT;
        ui_button(c, rs[i], label, style, hover == ids[i]);
    }
}

/* ---------------- actions ---------------- */
static void act(int id) {
    if (id == B_NONE) return;
    sound_play(SFX_CLICK);
    if (id == B_OFF) { plat_power(0); return; }
    if (id == B_REBOOT) { plat_power(1); return; }
    if (id == B_UPDATE && step == S_WELCOME) {
        if (plat_update_other()) { updating = true; step = S_RUN; pc = -1; msg[0] = 0; prog.started = false; }
        return;
    }
    if (id == B_BACK) {
        step = step == S_CONFIRM ? S_DISK : step == S_ERROR ? S_WELCOME : S_WELCOME;
        load_disks();
        return;
    }
    if (id == B_NEXT) {
        if (step == S_WELCOME) { load_disks(); step = S_DISK; }
        else if (step == S_DISK && sel >= 0) step = S_CONFIRM;
        else if (step == S_CONFIRM && sel >= 0) {
            if (plat_install_start(disks[sel].dev)) { updating = false; step = S_RUN; pc = -1; msg[0] = 0; prog.started = false; }
            else { strlcpy(msg, "L'installation n'a pas pu démarrer.", sizeof msg); step = S_ERROR; }
        }
    }
}

static int hit(int x, int y) {
    int ids[4];
    rect_t rs[4];
    int nb = buttons(ids, rs);
    for (int i = 0; i < nb; i++) if (rect_has(rs[i], x, y)) return ids[i];
    if (step == S_DISK)
        for (int i = 0; i < ndisk; i++) if (rect_has(disk_row(i), x, y) && disk_row(i).y + disk_row(i).h <= btn_main().y - 12) return i;
    return B_NONE;
}

bool setup_motion(int x, int y) {
    if (flow) return w_motion(x, y);
    int h = hit(x, y);
    if (h == hover) return false;
    hover = h;
    return true;
}

bool setup_click(int x, int y) {
    if (flow) return w_click(x, y);
    int h = hit(x, y);
    if (h == B_NONE) return false;
    if (h >= 0 && h < 100) {                         /* un disque */
        if (sel == h) act(B_NEXT);                    /* deuxième clic : suivant */
        else { sel = h; sound_play(SFX_CLICK); }
        return true;
    }
    act(h);
    hover = hit(x, y);
    return true;
}

bool setup_key(const event_t *e) {
    if (flow) return w_key(e);
    if (e->key == K_ENTER) {
        int ids[4];
        rect_t rs[4];
        if (buttons(ids, rs) > 0) act(ids[0]);          /* le bouton principal */
        return true;
    }
    if (e->key == K_ESC && (step == S_DISK || step == S_CONFIRM)) { act(B_BACK); return true; }
    if (step == S_DISK && ndisk && (e->key == K_DOWN || e->key == K_UP)) {
        sel = sel < 0 ? 0 : CLAMP(sel + (e->key == K_DOWN ? 1 : -1), 0, ndisk - 1);
        return true;
    }
    return false;
}

/* avancement de l'installation ; true : à redessiner */
bool setup_tick(void) {
    if (flow) return w_tick();
    if (step != S_RUN) return false;
    char m[200];
    int p = -1;
    int st = updating ? plat_update_state(m, sizeof m, &p) : plat_install_state(m, sizeof m, &p);
    bool changed = strcmp(m, msg) || p != last_pc;
    strlcpy(msg, m, sizeof msg);
    pc = p;
    last_pc = p;
    if (st == UPD_DONE) { step = S_DONE; sound_play(SFX_STARTUP); return true; }
    if (st == UPD_ERROR) { step = S_ERROR; sound_play(SFX_ERROR); return true; }
    return changed || prog.started;            /* la barre avance en douceur */
}

rect_t setup_card(void) { return card; }

/* ================================================================== */
/* Accueil du premier démarrage                                         */
/* ================================================================== */
enum { W_HELLO, W_KEYBOARD, W_TZ, W_NET, W_NAME, W_PASS, W_LOOK, W_DONE, W_COUNT };
enum { WB_NEXT = 100, WB_BACK, WB_SKIP, WB_CONNECT, WI_KBD = 200, WI_TZ = 300, WI_NET = 400, WI_WALL = 500, WI_ACC = 600,
       WF_NAME = 700, WF_PASS, WF_PASS2, WF_WIFI, WF_TRY };
static int wstep, whover = -1;
static field_t f_name, f_pass, f_pass2, f_wifi, f_try;
static bool pass_bad, pass_skip;
static uint64_t done_t0;
/* Internet */
static wifi_net_t nets[12];
static int nnet, wsel = -1, wstate = WIFI_NONE;
static bool online;
static char wcur[33], wmsg[160];
static uint64_t next_net;

void welcome_init(void) {
    flow = 1;
    wstep = W_HELLO;
    whover = -1;
    ui_field_set(&f_name, "");
    ui_field_set(&f_pass, "");
    ui_field_set(&f_pass2, "");
    ui_field_set(&f_wifi, "");
    ui_field_set(&f_try, "");
    f_pass.secret = f_pass2.secret = f_wifi.secret = true;
    f_name.plain = f_pass.plain = f_pass2.plain = f_wifi.plain = f_try.plain = true;
    pass_bad = pass_skip = false;
    wsel = -1;
    next_net = 0;
}

static field_t *focused_field(void) {
    field_t *all[] = { &f_name, &f_pass, &f_pass2, &f_wifi, &f_try };
    for (int i = 0; i < 5; i++) if (all[i]->focus) return all[i];
    return NULL;
}
static void focus(field_t *f) {
    field_t *all[] = { &f_name, &f_pass, &f_pass2, &f_wifi, &f_try };
    for (int i = 0; i < 5; i++) all[i]->focus = all[i] == f;
}

/* ---------------- disposition ---------------- */
static rect_t w_main(void) { return R(card.x + card.w - 36 - 200, card.y + card.h - 36 - 44, 200, 44); }
static rect_t w_back(void) { return R(card.x + card.w - 36 - 200 - 14 - 140, card.y + card.h - 36 - 44, 140, 44); }
static rect_t w_skip(void) { return R(card.x + 36, card.y + card.h - 36 - 44, 220, 44); }
static rect_t kbd_card(int i) { int w = (card.w - 72 - 20) / 2; return R(card.x + 36 + i * (w + 20), card.y + 124, w, 130); }
static rect_t tz_row(int i) { int w = (card.w - 72 - 16) / 2; return R(card.x + 36 + (i / 8) * (w + 16), card.y + 118 + (i % 8) * 38, w, 34); }
static rect_t net_row(int i) { return R(card.x + 36, card.y + 118 + i * 46, card.w - 72, 40); }
static rect_t wifi_field(void) { return R(card.x + 36, card.y + 118 + 5 * 46 + 6, card.w - 72 - 190, 40); }
static rect_t wifi_btn(void) { return R(card.x + card.w - 36 - 176, card.y + 118 + 5 * 46 + 6, 176, 40); }
static rect_t name_field(void) { return R(card.x + 36, card.y + 160, 420, 44); }
static rect_t pass_field(int k) { return R(card.x + 36, card.y + 150 + k * 80, 420, 44); }
static rect_t wall_box(int i) { int w = (card.w - 72 - 2 * 18) / 3, h = w * 7 / 16; return R(card.x + 36 + (i % 3) * (w + 18), card.y + 116 + (i / 3) * (h + 34), w, h); }
static rect_t acc_dot(int i) { return R(card.x + 36 + 150 + i * 44, card.y + card.h - 36 - 44 - 62, 30, 30); }

static bool can_next(void) {
    if (wstep == W_NAME) return f_name.s[0] != 0;
    if (wstep == W_PASS) return f_pass.s[0] != 0;
    return wstep != W_DONE;
}

static int w_buttons(int *ids, rect_t *rs) {
    int n = 0;
    if (wstep == W_DONE) return 0;
    if (can_next()) { ids[n] = WB_NEXT; rs[n++] = w_main(); }
    if (wstep > W_HELLO) { ids[n] = WB_BACK; rs[n++] = w_back(); }
    if (wstep == W_PASS) { ids[n] = WB_SKIP; rs[n++] = w_skip(); }
    if (wstep == W_NET && wsel >= 0 && wsel < nnet && !nets[wsel].current && !nets[wsel].enterprise) { ids[n] = WB_CONNECT; rs[n++] = wifi_btn(); }
    return n;
}

static int w_hit(int x, int y) {
    int ids[6];
    rect_t rs[6];
    int nb = w_buttons(ids, rs);
    for (int i = 0; i < nb; i++) if (rect_has(rs[i], x, y)) return ids[i];
    switch (wstep) {
    case W_KEYBOARD:
        for (int i = 0; i < 2; i++) if (rect_has(kbd_card(i), x, y)) return WI_KBD + i;
        if (rect_has(R(card.x + 36, card.y + 300, card.w - 72, 44), x, y)) return WF_TRY;
        break;
    case W_TZ: for (int i = 0; i < tz_count && i < 16; i++) if (rect_has(tz_row(i), x, y)) return WI_TZ + i; break;
    case W_NET:
        for (int i = 0; i < nnet && i < 5; i++) if (rect_has(net_row(i), x, y)) return WI_NET + i;
        if (rect_has(wifi_field(), x, y)) return WF_WIFI;
        break;
    case W_NAME: if (rect_has(name_field(), x, y)) return WF_NAME; break;
    case W_PASS:
        if (rect_has(pass_field(0), x, y)) return WF_PASS;
        if (rect_has(pass_field(1), x, y)) return WF_PASS2;
        break;
    case W_LOOK:
        for (int i = 0; i < WALLPAPER_COUNT; i++) if (rect_has(wall_box(i), x, y)) return WI_WALL + i;
        for (int i = 0; i < ACCENT_COUNT; i++) if (rect_has(acc_dot(i), x, y)) return WI_ACC + i;
        break;
    }
    return -1;
}

/* ---------------- dessin ---------------- */
static void signal_bars(canvas_t *c, int x, int y, int sig) {
    for (int k = 0; k < 4; k++) {
        int h = 5 + k * 4;
        gfx_rrect(c, x + k * 6, y + 18 - h, 4, h, 1.5f, sig > k * 25 ? 0xFFFFFFFF : 0x40FFFFFF);
    }
}

static void steps_dots(canvas_t *c) {
    int n = W_COUNT - 2, x = card.x + 36, y = card.y + card.h - 36 - 22;     /* bienvenue et fin exclues */
    if (wstep == W_PASS) return;                                             /* le bouton « Passer » est là */
    for (int i = 0; i < n; i++) {
        bool on = i + 1 == wstep, past = i + 1 < wstep;
        gfx_rrect(c, x, y - 3, on ? 22 : 6, 6, 3, on ? accent() : past ? 0xB3FFFFFF : 0x40FFFFFF);
        x += (on ? 22 : 6) + 7;
    }
}

static void w_draw(canvas_t *c, rect_t r) {
    card = r;
    int x = card.x + 36, w = card.w - 72, y = card.y + 120;
    char t[240];
    switch (wstep) {
    case W_HELLO: {
        draw_logo(c, card.x + card.w / 2.0f, card.y + 70, 84, 255);
        text_center(c, &font_h1, R(card.x, card.y + 180, card.w, 44), "Bonjour.", C_TEXT);
        text_center(c, &font_title, R(card.x, card.y + 238, card.w, 26), "Bienvenue dans TitanOs.", C_TEXT2);
        text_center(c, &font_body, R(card.x, card.y + 278, card.w, 22), "Quelques réglages, une minute à peine, et votre bureau est prêt.", C_MUTED);
        break;
    }
    case W_KEYBOARD: {
        header(c, "Quel est votre clavier ?", "Vous pourrez le changer plus tard dans Paramètres → Clavier et souris.");
        static const char *const big[2] = { "AZERTY", "QWERTY" };
        static const char *const sub[2] = { "Français (France, Belgique)", "Anglais (États-Unis)" };
        for (int i = 0; i < 2; i++) {
            rect_t k = kbd_card(i);
            bool on = g_set.layout == i;
            gfx_rrect(c, k.x, k.y, k.w, k.h, 16, on ? ((accent() & 0xFFFFFF) | 0x50000000) : whover == WI_KBD + i ? 0x22FFFFFF : 0x12FFFFFF);
            if (on) gfx_rrect_stroke(c, k.x, k.y, k.w, k.h, 16, 2, accent());
            text_center(c, &font_h1, R(k.x, k.y + 30, k.w, 44), big[i], C_TEXT);
            text_center(c, &font_body, R(k.x, k.y + 86, k.w, 22), sub[i], C_TEXT2);
        }
        text_draw(c, &font_small, x, card.y + 280, "Essayez ici :", C_MUTED);
        ui_field(c, R(x, card.y + 300, w, 44), &f_try, "Tapez quelques lettres : a, z, q, w, chiffres…");
        break;
    }
    case W_TZ:
        header(c, "Où êtes-vous ?", "Le fuseau horaire règle l'heure de TitanOs.");
        for (int i = 0; i < tz_count && i < 16; i++) {
            rect_t rr = tz_row(i);
            bool on = g_set.tz == i;
            if (on || whover == WI_TZ + i) gfx_rrect(c, rr.x, rr.y, rr.w, rr.h, 10, on ? ((accent() & 0xFFFFFF) | 0x60000000) : 0x1CFFFFFF);
            text_fit(c, on ? &font_bold : &font_body, rr.x + 12, rr.y + 9, rr.w - 24, tz_list[i].name, on ? C_TEXT : C_TEXT2);
        }
        break;
    case W_NET: {
        header(c, "Connectons-nous à Internet", "Pour l'heure exacte, les mises à jour et les applications.");
        if (wstate == WIFI_NONE && !online) paragraph(c, x, y, w, "Aucune carte Wi-Fi n'est prête. Branchez un câble réseau, ou continuez sans Internet : vous pourrez vous connecter plus tard avec l'application Réseau.", C_TEXT2);
        for (int i = 0; i < nnet && i < 5; i++) {
            rect_t rr = net_row(i);
            const wifi_net_t *n = &nets[i];
            bool on = wsel == i;
            gfx_rrect(c, rr.x, rr.y, rr.w, rr.h, 12, on ? ((accent() & 0xFFFFFF) | 0x50000000) : whover == WI_NET + i ? 0x22FFFFFF : 0x10FFFFFF);
            signal_bars(c, rr.x + 14, rr.y + 10, n->signal);
            text_fit(c, on ? &font_bold : &font_body, rr.x + 56, rr.y + 11, rr.w - 260, n->ssid, C_TEXT);
            const char *kind = n->current ? "Connecté" : n->enterprise ? "Entreprise" : n->secure ? (n->wpa3 ? "WPA3" : "Sécurisé") : "Ouvert";
            text_draw(c, &font_small, rr.x + rr.w - 14 - text_width(&font_small, kind), rr.y + 13, kind, n->current ? 0xFF86EFAC : C_MUTED);
        }
        int sy = card.y + 118 + 5 * 46 + 6;
        if (wsel >= 0 && wsel < nnet && !nets[wsel].current) {
            const wifi_net_t *n = &nets[wsel];
            if (n->enterprise) paragraph(c, x, sy + 8, w, "Réseau d'entreprise (identifiant et mot de passe) : connectez-vous plus tard avec l'application Réseau.", C_MUTED);
            else {
                if (n->secure) ui_field(c, wifi_field(), &f_wifi, "Mot de passe du réseau");
                else text_draw(c, &font_body, x, sy + 11, "Réseau ouvert : pas de mot de passe.", C_TEXT2);
                ui_button(c, wifi_btn(), "Se connecter", BTN_ACCENT, whover == WB_CONNECT);
            }
        }
        const char *st = online ? "Connecté à Internet." : wstate == WIFI_CONNECTING ? (wmsg[0] ? wmsg : "Connexion…")
                       : wstate == WIFI_REFUSED ? "Mot de passe refusé : vérifiez-le et réessayez." : wmsg;
        if (st && st[0]) text_fit(c, &font_small, x, sy + 52, w, st, online ? 0xFF86EFAC : wstate == WIFI_REFUSED ? 0xFFFCA5A5 : C_MUTED);
        break;
    }
    case W_NAME:
        header(c, "Qui va utiliser ce PC ?", "Votre nom s'affiche sur l'écran de verrouillage.");
        text_draw(c, &font_small, x, card.y + 138, "Votre nom", C_MUTED);
        ui_field(c, name_field(), &f_name, "Par exemple : Alex");
        break;
    case W_PASS:
        header(c, "Créez un mot de passe", "Il protège votre session et les commandes d'administration (sudo).");
        text_draw(c, &font_small, x, card.y + 128, "Mot de passe", C_MUTED);
        ui_field(c, pass_field(0), &f_pass, "Choisissez un mot de passe");
        text_draw(c, &font_small, x, card.y + 208, "Confirmation", C_MUTED);
        ui_field(c, pass_field(1), &f_pass2, "Tapez-le une seconde fois");
        if (pass_bad) text_draw(c, &font_small, x, card.y + 290, "Les deux mots de passe sont différents.", 0xFFFCA5A5);
        else paragraph(c, x, card.y + 290, w, "Sans mot de passe, la session s'ouvre d'un clic, mais sudo refuse les commandes d'administration.", C_MUTED);
        break;
    case W_LOOK:
        header(c, "Choisissez votre style", "Le fond d'écran et la couleur d'accent changent en direct.");
        for (int i = 0; i < WALLPAPER_COUNT; i++) {
            rect_t b = wall_box(i);
            wallpaper_thumb(c, b.x, b.y, b.w, b.h, i);
            if (g_set.wallpaper == i) gfx_rrect_stroke(c, b.x - 3, b.y - 3, b.w + 6, b.h + 6, 12, 2.5f, 0xFFFFFFFF);
            else if (whover == WI_WALL + i) gfx_rrect_stroke(c, b.x - 2, b.y - 2, b.w + 4, b.h + 4, 11, 1.5f, 0x80FFFFFF);
            text_draw(c, &font_small, b.x, b.y + b.h + 8, wallpaper_name(i), g_set.wallpaper == i ? C_TEXT : C_MUTED);
        }
        text_draw(c, &font_small, x, acc_dot(0).y + 8, "Couleur d'accent", C_MUTED);
        for (int i = 0; i < ACCENT_COUNT; i++) {
            rect_t d = acc_dot(i);
            if (g_set.accent == i) gfx_circle(c, d.x + 15, d.y + 15, 19, 0xFFFFFFFF);
            gfx_circle(c, d.x + 15, d.y + 15, g_set.accent == i ? 16 : whover == WI_ACC + i ? 16 : 14, accent_of(i));
        }
        break;
    case W_DONE:
        snprintf(t, sizeof t, "Tout est prêt, %s.", g_set.user);
        text_center(c, &font_h1, R(card.x, card.y + card.h / 2 - 70, card.w, 44), t, C_TEXT);
        text_center(c, &font_body, R(card.x, card.y + card.h / 2 - 14, card.w, 22), "Quelques instants, le temps d'ouvrir votre bureau…", C_TEXT2);
        wm_peaks(card.x + card.w / 2 - 64, card.y + card.h / 2 + 30);
        break;
    }
    steps_dots(c);
    if (wstep == W_DONE) return;
    int ids[6];
    rect_t rs[6];
    int nb = w_buttons(ids, rs);
    for (int i = 0; i < nb; i++) {
        if (ids[i] == WB_CONNECT) continue;                               /* déjà dessiné */
        const char *label = ids[i] == WB_BACK ? "Retour" : ids[i] == WB_SKIP ? "Continuer sans mot de passe"
                          : wstep == W_HELLO ? "Commencer" : wstep == W_NET && !online ? "Continuer sans Internet"
                          : wstep == W_LOOK ? "Terminer" : "Suivant";
        int style = ids[i] == WB_NEXT ? BTN_ACCENT : BTN_NORMAL;
        rect_t b = rs[i];
        if (ids[i] == WB_NEXT && wstep == W_NET && !online) { b.x -= 40; b.w += 40; style = BTN_NORMAL; }
        ui_button(c, b, label, style, whover == ids[i]);
    }
}

/* ---------------- actions ---------------- */
static void go_step(int s) {
    wstep = CLAMP(s, 0, W_DONE);
    whover = -1;
    focus(wstep == W_NAME ? &f_name : wstep == W_PASS ? &f_pass : wstep == W_KEYBOARD ? &f_try : NULL);
    if (wstep == W_NAME && !f_name.s[0] && strcmp(g_set.user, "Titan")) ui_field_set(&f_name, g_set.user);
    if (wstep == W_NET) { plat_wifi_scan(); next_net = 0; }
    if (wstep == W_DONE) done_t0 = plat_ms();
}

static void connect_selected(void) {
    if (wsel < 0 || wsel >= nnet || nets[wsel].enterprise) return;
    const wifi_net_t *n = &nets[wsel];
    if (n->secure && !f_wifi.s[0]) { focus(&f_wifi); return; }
    plat_wifi_connect(n->ssid, n->secure ? f_wifi.s : NULL);
    snprintf(wmsg, sizeof wmsg, "Connexion à « %s »…", n->ssid);
    wstate = WIFI_CONNECTING;
    next_net = plat_ms() + 800;
}

static void w_next(void) {
    switch (wstep) {
    case W_NAME:
        if (!f_name.s[0]) return;
        {
            const char *b = f_name.s;
            while (*b == ' ') b++;
            strlcpy(g_set.user, b, sizeof g_set.user);
            for (int k = (int)strlen(g_set.user) - 1; k >= 0 && g_set.user[k] == ' '; k--) g_set.user[k] = 0;
            if (!g_set.user[0]) return;
        }
        break;
    case W_PASS:
        if (!f_pass.s[0]) return;
        if (strcmp(f_pass.s, f_pass2.s)) { pass_bad = true; ui_field_set(&f_pass2, ""); focus(&f_pass2); sound_play(SFX_ERROR); return; }
        settings_pass_hash(f_pass.s, g_set.pass);
        pass_skip = false;
        break;
    }
    ui_field_set(&f_pass, wstep == W_PASS ? "" : f_pass.s);
    ui_field_set(&f_pass2, wstep == W_PASS ? "" : f_pass2.s);
    go_step(wstep + 1);
}

static void w_act(int id) {
    if (id < 0) return;
    if (id >= WF_NAME) {                                              /* un champ de texte */
        focus(id == WF_NAME ? &f_name : id == WF_PASS ? &f_pass : id == WF_PASS2 ? &f_pass2 : id == WF_WIFI ? &f_wifi : &f_try);
        return;
    }
    sound_play(SFX_CLICK);
    if (id == WB_NEXT) { w_next(); return; }
    if (id == WB_BACK) { go_step(wstep - 1); return; }
    if (id == WB_SKIP) { g_set.pass[0] = 0; pass_skip = true; ui_field_set(&f_pass, ""); ui_field_set(&f_pass2, ""); go_step(wstep + 1); return; }
    if (id == WB_CONNECT) { connect_selected(); return; }
    if (id >= WI_ACC) { g_set.accent = id - WI_ACC; wm_theme_changed(); return; }
    if (id >= WI_WALL) { g_set.wallpaper = id - WI_WALL; return; }
    if (id >= WI_NET) {
        int k = id - WI_NET;
        if (wsel != k) { wsel = k; ui_field_set(&f_wifi, ""); focus(nets[k].secure ? &f_wifi : NULL); wmsg[0] = 0; }
        return;
    }
    if (id >= WI_TZ) { if (g_set.tz != id - WI_TZ) { g_set.tz = id - WI_TZ; g_set.rtc_local = false; } return; }
    if (id >= WI_KBD) { g_set.layout = id - WI_KBD; return; }
}

static bool w_motion(int x, int y) {
    int h = w_hit(x, y);
    if (h == whover) return false;
    whover = h;
    return true;
}

static bool w_click(int x, int y) {
    int h = w_hit(x, y);
    if (h < WF_NAME) focus(NULL);
    w_act(h);
    whover = w_hit(x, y);
    return true;
}

static bool w_key(const event_t *e) {
    field_t *f = focused_field();
    if (e->key == K_ENTER) {
        if (wstep == W_NET && f == &f_wifi) connect_selected();
        else if (wstep == W_PASS && f == &f_pass && f_pass.s[0]) focus(&f_pass2);
        else if (can_next()) w_next();
        return true;
    }
    if (e->key == K_TAB && wstep == W_PASS) { focus(f == &f_pass ? &f_pass2 : &f_pass); return true; }
    if (e->key == K_ESC && wstep > W_HELLO && wstep < W_DONE && !f) { go_step(wstep - 1); return true; }
    if (f) {
        bool ch = ui_field_key(f, e);
        if (ch && f == &f_pass2) pass_bad = false;
        return true;
    }
    if (wstep == W_TZ && (e->key == K_DOWN || e->key == K_UP)) {
        int n = MIN(tz_count, 16);
        g_set.tz = (g_set.tz + (e->key == K_DOWN ? 1 : n - 1)) % n;
        g_set.rtc_local = false;
        return true;
    }
    if (wstep == W_KEYBOARD && (e->key == K_LEFT || e->key == K_RIGHT)) { g_set.layout = e->key == K_RIGHT; return true; }
    return false;
}

static bool w_tick(void) {
    uint64_t now = plat_ms();
    if (wstep == W_DONE) {
        if (now - done_t0 > 2600) { wm_welcome_done(); return false; }
        return true;                                                  /* crêtes animées */
    }
    if (wstep == W_NET && now >= next_net) {
        next_net = now + 1200;
        char ssid[33];
        int sig;
        wstate = plat_wifi_status(ssid, sizeof ssid, &sig);
        strlcpy(wcur, wstate == WIFI_CONNECTED ? ssid : "", sizeof wcur);
        netif_t ifs[8];
        int ni = plat_netifs(ifs, 8);
        online = false;
        for (int i = 0; i < ni; i++) if (ifs[i].internet) online = true;
        char sel[33] = "";
        if (wsel >= 0 && wsel < nnet) strlcpy(sel, nets[wsel].ssid, sizeof sel);
        nnet = plat_wifi_list(nets, 12);
        wsel = -1;
        for (int i = 0; i < nnet && i < 5; i++) if (sel[0] && !strcmp(nets[i].ssid, sel)) wsel = i;
        if (wstate == WIFI_CONNECTING) { char m[160]; plat_wifi_last(m, sizeof m); if (m[0]) strlcpy(wmsg, m, sizeof wmsg); }
        if (wstate == WIFI_CONNECTED) wmsg[0] = 0;
        return true;
    }
    return false;
}
