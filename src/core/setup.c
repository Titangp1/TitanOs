/* TitanOs - assistant d'installation (clé d'installation)
 *
 * Démarrée depuis la clé d'installation, TitanOs n'ouvre pas de session : cet assistant
 * plein écran installe TitanOs sur un disque de l'ordinateur, comme l'installation de
 * Windows. Étapes : bienvenue -> choix du disque -> confirmation -> copie -> terminé.
 * Si un TitanOs est déjà installé sur un disque, il peut aussi être mis à jour en gardant
 * les fichiers de l'utilisateur. Le travail lui-même est fait par la partie système
 * (plat_install_start, plat_update_other : src/linux/update.c). */
#include "desk.h"

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
    int h = hit(x, y);
    if (h == hover) return false;
    hover = h;
    return true;
}

bool setup_click(int x, int y) {
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
