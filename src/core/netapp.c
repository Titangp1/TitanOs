/* TitanOs - Réseau : Wi-Fi (recherche, connexion), câble et partage USB, test de connexion */
#include "desk.h"

#define ROW_H  46
#define MAX_NET 40

typedef struct {
    wifi_net_t nets[MAX_NET];
    int nnets, scroll, hover, wstate, wsignal;
    char wssid[33];
    netif_t ifs[8];
    int nifs;
    int job, job_kind;
    char job_if[16], result[160];
    uint64_t next_list, next_scan;
    uint64_t opened, scan_t0;         /* ouverture de la fenêtre ; recherche demandée (Actualiser) */
    char watch[33];                   /* réseau enregistré en cours de connexion : son mot de passe marche-t-il ? */
    uint64_t watch_t0;
    bool watch_eap;
    char conn[33];                    /* connexion demandée : suivie jusqu'au résultat */
    uint64_t conn_t0, conn_ok_t;
    char pending[33];                 /* réseau en attente du mot de passe */
    char pending_id[128];             /* identifiant (WPA2-Entreprise) */
    char menu_ssid[33];               /* réseau visé par le menu (clic droit) */
} net_t;

enum { J_NONE, J_DHCP, J_PING };
enum { H_NONE = -1, H_REFRESH = 1000, H_PING = 1001, H_HIDDEN = 1002, H_CONSOLE = 1003, H_WIRED = 2000 };
void app_netconsole_open(void);

static window_t *cur_net;

/* ---- disposition ---- */
static int wired_count(net_t *s) { int n = 0; for (int i = 0; i < s->nifs; i++) if (strcmp(s->ifs[i].kind, "Wi-Fi")) n++; return n; }
static rect_t list_rect(canvas_t *c, net_t *s) {
    int wired_h = wired_count(s) ? 40 + wired_count(s) * 58 : 0;
    return R(16, 66, c->w - 32, c->h - 66 - wired_h - 70);
}
static rect_t refresh_rect(canvas_t *c) { return R(c->w - 16 - 130, 16, 130, 36); }
static rect_t hidden_rect(canvas_t *c) { return R(c->w - 16 - 130 - 10 - 160, 16, 160, 36); }
static rect_t ping_rect(canvas_t *c) { return R(16, c->h - 54, 170, 38); }
static rect_t console_rect(canvas_t *c) { return R(16 + 170 + 10, c->h - 54, 120, 38); }

/* texte sur deux lignes au plus (coupé aux espaces) */
static void wrap2(canvas_t *c, const font_t *f, int x, int y, int maxw, const char *t, uint32_t col) {
    int n = (int)strlen(t), cut = n;
    if (text_width(f, t) > maxw) {
        cut = 0;
        for (int i = 1; i <= n; i++) if ((t[i] == ' ' || !t[i]) && text_width_n(f, t, i) <= maxw) cut = i;
        if (!cut) cut = n;
    }
    text_draw_n(c, f, x, cut < n ? y - 8 : y, t, cut, col);
    if (cut < n) text_fit(c, f, x, y + 9, maxw, t + cut + 1, col);
}
static rect_t wired_row(canvas_t *c, net_t *s, int k) {
    rect_t L = list_rect(c, s);
    return R(16, L.y + L.h + 40 + k * 58, c->w - 32, 52);
}
static rect_t dhcp_rect(canvas_t *c, net_t *s, int k) { rect_t r = wired_row(c, s, k); return R(r.x + r.w - 196, r.y + 9, 184, 34); }

static int level_of(int signal) { return signal > 75 ? 4 : signal > 50 ? 3 : signal > 25 ? 2 : 1; }

static void refresh(net_t *s) {
    s->wstate = plat_wifi_status(s->wssid, sizeof s->wssid, &s->wsignal);
    s->nnets = s->wstate == WIFI_NONE ? 0 : plat_wifi_list(s->nets, MAX_NET);
    netif_t all[8];
    int n = plat_netifs(all, 8);
    s->nifs = 0;
    for (int i = 0; i < n; i++) if (strcmp(all[i].name, "lo")) s->ifs[s->nifs++] = all[i];
}

/* ---- dessin ---- */
static void net_draw(window_t *w, canvas_t *c) {
    net_t *s = w->data;
    draw_wifi(c, 34, 36, 26, s->wstate == WIFI_CONNECTED ? level_of(s->wsignal) : 0, C_TEXT, 0x50FFFFFF);
    text_draw(c, &font_title, 58, 14, "Wi-Fi", C_TEXT);
    char st[160];
    if (s->wstate == WIFI_NONE) strlcpy(st, "Aucune carte Wi-Fi utilisable", sizeof st);
    else if (s->wstate == WIFI_CONNECTED) snprintf(st, sizeof st, "Connecté à « %s »", s->wssid);
    else if (s->wstate == WIFI_CONNECTING) strlcpy(st, "Connexion en cours…", sizeof st);
    else if (s->wstate == WIFI_BLOCKED) strlcpy(st, "Radio coupée par l'interrupteur", sizeof st);
    else if (s->wstate == WIFI_REFUSED) strlcpy(st, "Refusé (mot de passe ou identifiant) : clic droit sur le réseau, « Oublier », puis réessayez", sizeof st);
    else strlcpy(st, "Non connecté", sizeof st);
    for (int i = 0; i < s->nifs; i++)
        if (s->ifs[i].internet && strcmp(s->ifs[i].kind, "Wi-Fi")) {
            size_t l = strlen(st);
            snprintf(st + l, sizeof st - l, " · Internet passe par %s", !strcmp(s->ifs[i].kind, "Téléphone USB") ? "le téléphone" : "le câble");
        }
    text_draw(c, &font_small, 58, 36, st, s->wstate == WIFI_CONNECTED ? 0xFF86EFAC : C_MUTED);
    if (s->wstate != WIFI_NONE) {
        ui_button(c, refresh_rect(c), "Actualiser", BTN_NORMAL, s->hover == H_REFRESH);
        ui_button(c, hidden_rect(c), "Réseau masqué…", BTN_NORMAL, s->hover == H_HIDDEN);
    }

    rect_t L = list_rect(c, s);
    ui_panel(c, L, 16);
    rect_t old = c->clip;
    canvas_clip(c, rect_grow(L, -2));
    int vis = (L.h - 8) / ROW_H;
    if (s->wstate == WIFI_NONE)
        text_center(c, &font_body, R(L.x, L.y + 20, L.w, 24), "Branchez un câble Ethernet ou partagez la connexion d'un téléphone en USB.", C_MUTED);
    else if (s->wstate == WIFI_BLOCKED) {
        text_center(c, &font_body, R(L.x, L.y + 20, L.w, 24), "Le Wi-Fi est coupé par l'interrupteur de l'ordinateur.", C_TEXT);
        text_center(c, &font_body, R(L.x, L.y + 46, L.w, 24), "Sur le VAIO : glissez l'interrupteur WIRELESS (bord avant) sur ON.", C_MUTED);
    } else if (!s->nnets) {
        bool long_ = plat_ms() - s->opened > 15000;
        text_center(c, &font_body, R(L.x, L.y + 20, L.w, 24), long_ ? "Aucun réseau entendu pour l'instant." : "Recherche des réseaux…", C_MUTED);
        if (long_) text_center(c, &font_small, R(L.x, L.y + 46, L.w, 20),
                               "Vérifiez l'interrupteur WIRELESS du VAIO, rapprochez-vous de la box, puis « Actualiser ».", C_MUTED);
    }
    for (int i = s->scroll; i < s->nnets && i < s->scroll + vis + 1; i++) {
        wifi_net_t *n = &s->nets[i];
        rect_t r = R(L.x + 6, L.y + 4 + (i - s->scroll) * ROW_H, L.w - 12, ROW_H - 4);
        if (n->current) gfx_rrect(c, r.x, r.y, r.w, r.h, 12, (accent() & 0xFFFFFF) | 0x70000000);
        else if (i == s->hover) gfx_rrect(c, r.x, r.y, r.w, r.h, 12, 0x1EFFFFFF);
        draw_wifi(c, r.x + 24, r.y + 20, 22, level_of(n->signal), C_TEXT, 0x40FFFFFF);
        text_fit(c, &font_bold, r.x + 50, r.y + 5, r.w - 230, n->ssid, C_TEXT);
        const char *kind = n->enterprise ? "Identifiant et mot de passe (WPA2-Entreprise)" : n->wpa3 ? "WPA3"
                         : n->secure ? "Mot de passe (WPA2)" : "Ouvert : une page d'identification peut s'ouvrir dans Google Chrome";
        text_fit(c, &font_small, r.x + 50, r.y + 24, r.w - 230, kind, C_MUTED);
        const char *tag = n->current ? (s->wstate == WIFI_CONNECTED ? "Connecté" : "Connexion…") : n->known ? "Enregistré" : "";
        text_draw(c, &font_small, r.x + r.w - 130, r.y + 14, tag, n->current ? C_TEXT : C_MUTED);
        if (n->secure) draw_icon(c, ICON_LOCK, r.x + r.w - 34, r.y + 12, 18);
    }
    c->clip = old;
    ui_scrollbar(c, R(L.x + L.w - 8, L.y + 8, 4, L.h - 16), s->nnets, vis, s->scroll);

    int k = 0;
    if (wired_count(s)) text_draw(c, &font_bold, 20, L.y + L.h + 14, "Câble et partage USB", C_TEXT2);
    for (int i = 0; i < s->nifs; i++) {
        netif_t *f = &s->ifs[i];
        if (!strcmp(f->kind, "Wi-Fi")) continue;
        rect_t r = wired_row(c, s, k);
        ui_panel(c, r, 14);
        bool on = f->carrier && f->ip[0];
        draw_icon(c, ICON_ETHERNET, r.x + 14, r.y + 14, 24);
        char t[96];
        snprintf(t, sizeof t, "%s · %s", f->kind, f->name);
        text_draw(c, &font_bold, r.x + 50, r.y + 8, t, C_TEXT);
        bool usb = !strcmp(f->kind, "Téléphone USB");
        if (on) snprintf(t, sizeof t, "Connecté · %s%s", f->ip, f->internet ? " · Internet passe par ici" : "");
        else strlcpy(t, !f->up ? "Désactivé" : !f->carrier ? (usb ? "Partage de connexion désactivé sur le téléphone" : "Câble débranché")
                                                           : "Branché, demande d'une adresse…", sizeof t);
        text_draw(c, &font_small, r.x + 50, r.y + 29, t, on ? 0xFF86EFAC : C_MUTED);
        bool busy = s->job >= 0 && s->job_kind == J_DHCP && !strcmp(s->job_if, f->name);
        ui_button(c, dhcp_rect(c, s, k), busy ? "Recherche…" : "Obtenir une adresse", BTN_NORMAL, s->hover == H_WIRED + k);
        k++;
    }
    ui_button(c, ping_rect(c), s->job >= 0 && s->job_kind == J_PING ? "Test en cours…" : "Tester Internet", BTN_ACCENT, s->hover == H_PING);
    ui_button(c, console_rect(c), "Console", BTN_NORMAL, s->hover == H_CONSOLE);
    rect_t cr = console_rect(c);
    wrap2(c, &font_small, cr.x + cr.w + 14, c->h - 42, c->w - (cr.x + cr.w + 14) - 16, s->result, C_TEXT2);
}

/* ---- actions ---- */
static void track(net_t *s, const char *ssid) {
    strlcpy(s->conn, ssid, sizeof s->conn);
    s->conn_t0 = plat_ms();
    s->conn_ok_t = 0;
}

static void connect_with(const char *ssid, const char *psk, net_t *s) {
    if (!plat_wifi_connect(ssid, psk)) { wm_toast("Impossible de se connecter à ce réseau."); return; }
    snprintf(s->result, sizeof s->result, "Connexion à « %s »…", ssid);
    track(s, ssid);
    s->next_list = 0;
}

static void password_cb(const char *psk, void *ctx) {
    net_t *s = ctx;
    if (!cur_net || !cur_net->used || cur_net->data != s) return;
    if (strlen(psk) < 8) { dlg_message("Wi-Fi", "Le mot de passe d'un réseau Wi-Fi fait au moins 8 caractères."); return; }
    connect_with(s->pending, psk, s);
    wm_invalidate(cur_net);
}

static void eap_password_cb(const char *pw, void *ctx) {
    net_t *s = ctx;
    if (!cur_net || !cur_net->used || cur_net->data != s) return;
    if (!pw[0]) { dlg_message("Wi-Fi", "Le mot de passe est vide."); return; }
    if (!plat_wifi_connect_eap(s->pending, s->pending_id, pw)) { wm_toast("Impossible de se connecter à ce réseau."); return; }
    snprintf(s->result, sizeof s->result, "Connexion à « %s » avec l'identifiant %s…", s->pending, s->pending_id);
    track(s, s->pending);
    s->next_list = 0;
    wm_invalidate(cur_net);
}

static void eap_identity_cb(const char *id, void *ctx) {
    net_t *s = ctx;
    if (!cur_net || !cur_net->used || cur_net->data != s) return;
    if (!id[0]) { dlg_message("Wi-Fi", "L'identifiant est vide."); return; }
    strlcpy(s->pending_id, id, sizeof s->pending_id);
    char label[96];
    snprintf(label, sizeof label, "Mot de passe de %s", id);
    dlg_password("Connexion Wi-Fi", label, eap_password_cb, s);
}

/* réseau masqué : son nom, puis son mot de passe (vide s'il est ouvert) */
static void hidden_password_cb(const char *pw, void *ctx) {
    net_t *s = ctx;
    if (!cur_net || !cur_net->used || cur_net->data != s) return;
    if (pw[0] && strlen(pw) < 8) { dlg_message("Wi-Fi", "Le mot de passe d'un réseau Wi-Fi fait au moins 8 caractères."); return; }
    connect_with(s->pending, pw[0] ? pw : NULL, s);
    wm_invalidate(cur_net);
}
static void hidden_name_cb(const char *name, void *ctx) {
    net_t *s = ctx;
    if (!cur_net || !cur_net->used || cur_net->data != s || !name[0]) return;
    strlcpy(s->pending, name, sizeof s->pending);
    char label[96];
    snprintf(label, sizeof label, "Mot de passe de « %s » (vide si le réseau est ouvert)", name);
    dlg_password("Réseau masqué", label, hidden_password_cb, s);
}

/* (re)demande le mot de passe d'un réseau : après un refus, ou pour le changer */
static void ask_password(window_t *w, net_t *s, const char *ssid, bool eap, bool refused) {
    strlcpy(s->pending, ssid, sizeof s->pending);
    cur_net = w;
    char label[120];
    if (eap) {
        snprintf(label, sizeof label, "%sIdentifiant pour « %s »", refused ? "Refusé. " : "", ssid);
        dlg_input("Connexion Wi-Fi", label, s->pending_id, eap_identity_cb, s);
    } else {
        snprintf(label, sizeof label, refused ? "Le mot de passe enregistré pour « %s » ne marche pas : entrez-le" : "Nouveau mot de passe de « %s »", ssid);
        dlg_password("Connexion Wi-Fi", label, password_cb, s);
    }
}

static void choose(window_t *w, net_t *s, wifi_net_t *n) {
    if (n->current && s->wstate == WIFI_CONNECTED) return;
    if (n->enterprise && !n->known) {
        strlcpy(s->pending, n->ssid, sizeof s->pending);
        cur_net = w;
        char label[96];
        snprintf(label, sizeof label, "Identifiant pour « %s » (souvent une adresse e-mail)", n->ssid);
        dlg_input("Connexion Wi-Fi", label, "", eap_identity_cb, s);
        return;
    }
    if (!n->secure && !n->known) wm_toast("Réseau ouvert : si une page d'identification est demandée, ouvrez Google Chrome.");
    if (n->secure && n->known) {
        strlcpy(s->watch, n->ssid, sizeof s->watch);
        s->watch_t0 = plat_ms();
        s->watch_eap = n->enterprise;
    }
    if (n->secure && !n->known) {
        strlcpy(s->pending, n->ssid, sizeof s->pending);
        cur_net = w;
        char label[96];
        snprintf(label, sizeof label, "Mot de passe de « %s »", n->ssid);
        dlg_password("Connexion Wi-Fi", label, password_cb, s);
        return;
    }
    connect_with(n->ssid, NULL, s);
}

static void ctx_cb(int i, void *ctx) {
    window_t *w = ctx;
    if (!w->used || !w->data) return;
    net_t *s = w->data;
    /* le pointeur a quitté la liste pour aller dans le menu : on retrouve le réseau par son nom */
    wifi_net_t *n = NULL;
    for (int k = 0; k < s->nnets; k++) if (!strcmp(s->nets[k].ssid, s->menu_ssid)) n = &s->nets[k];
    if (!n) return;
    bool change = n->known && n->secure;                   /* menu : Se connecter, Changer..., Oublier */
    if (i == 0) choose(w, s, n);
    else if (change && i == 1) ask_password(w, s, n->ssid, n->enterprise, false);
    else {
        plat_wifi_forget(n->ssid);
        n->known = false;                 /* tout de suite à l'écran, sans attendre le veilleur */
        if (n->current) { n->current = false; s->wstate = WIFI_OFF; }
        wm_toast("Réseau oublié : le mot de passe vous sera redemandé.");
        s->next_list = 0;
        wm_invalidate(w);
    }
}

static void net_event(window_t *w, const event_t *e) {
    net_t *s = w->data;
    canvas_t *c = &w->surf;
    rect_t L = list_rect(c, s);
    int vis = (L.h - 8) / ROW_H;
    if (e->type == EV_MOTION) {
        int h = H_NONE;
        if (rect_has(L, e->x, e->y)) { int i = s->scroll + (e->y - L.y - 4) / ROW_H; if (i >= 0 && i < s->nnets) h = i; }
        if (s->wstate != WIFI_NONE && rect_has(refresh_rect(c), e->x, e->y)) h = H_REFRESH;
        if (s->wstate != WIFI_NONE && rect_has(hidden_rect(c), e->x, e->y)) h = H_HIDDEN;
        if (rect_has(ping_rect(c), e->x, e->y)) h = H_PING;
        if (rect_has(console_rect(c), e->x, e->y)) h = H_CONSOLE;
        for (int k = 0; k < wired_count(s); k++) if (rect_has(dhcp_rect(c, s, k), e->x, e->y)) h = H_WIRED + k;
        if (h != s->hover) { s->hover = h; wm_invalidate(w); }
        return;
    }
    if (e->type == EV_WHEEL) {
        s->scroll = CLAMP(s->scroll + e->dy, 0, MAX(0, s->nnets - vis));
        wm_invalidate(w);
        return;
    }
    if (e->type != EV_BUTTON || !e->down) return;
    if (s->hover != H_NONE) sound_play(SFX_CLICK);
    if (s->hover >= 0 && s->hover < s->nnets) {
        if (e->button == BTN_RIGHT) {
            rect_t cl = wm_client(w);
            strlcpy(s->menu_ssid, s->nets[s->hover].ssid, sizeof s->menu_ssid);
            wifi_net_t *n = &s->nets[s->hover];
            static const char *items3[] = { "Se connecter", "Changer le mot de passe…", "Oublier ce réseau" };
            static const char *items2[] = { "Se connecter", "Oublier ce réseau" };
            if (n->known && n->secure) wm_menu(cl.x + e->x, cl.y + e->y, items3, 3, ctx_cb, w);
            else wm_menu(cl.x + e->x, cl.y + e->y, items2, n->known ? 2 : 1, ctx_cb, w);
        } else choose(w, s, &s->nets[s->hover]);
    } else if (s->hover == H_CONSOLE) {
        app_netconsole_open();
        return;
    } else if (s->hover == H_HIDDEN) {
        cur_net = w;
        dlg_input("Réseau masqué", "Nom exact du réseau (SSID)", "", hidden_name_cb, s);
    } else if (s->hover == H_REFRESH) {
        plat_wifi_scan();
        s->next_list = plat_ms() + 2500;
        s->scan_t0 = plat_ms();
        strlcpy(s->result, "Recherche des réseaux Wi-Fi…", sizeof s->result);
    } else if (s->hover >= H_WIRED && s->job < 0) {
        int k = s->hover - H_WIRED, idx = -1;
        for (int i = 0, n = 0; i < s->nifs; i++) if (strcmp(s->ifs[i].kind, "Wi-Fi") && n++ == k) idx = i;
        if (idx >= 0) {
            strlcpy(s->job_if, s->ifs[idx].name, sizeof s->job_if);
            const char *argv[] = { "/bin/busybox", "udhcpc", "-i", s->job_if, "-n", "-q", "-t", "6", "-s", "/etc/udhcpc.script", NULL };
            s->job = plat_spawn(argv);
            s->job_kind = J_DHCP;
            snprintf(s->result, sizeof s->result, "Demande d'adresse sur %s…", s->job_if);
        }
    } else if (s->hover == H_PING && s->job < 0) {
        /* par étapes : route, box, Internet, noms : la réponse dit ce qui ne va pas */
        static const char script[] =
            "r=$(ip -4 route show default | head -n 1);"
            "[ -n \"$r\" ] || { echo \"Aucune carte n'a d'adresse : pas de chemin vers Internet.\"; exit 1; };"
            "dev=$(echo \"$r\" | sed -n 's/.* dev \\([^ ]*\\).*/\\1/p'); gw=$(echo \"$r\" | sed -n 's/.*via \\([^ ]*\\).*/\\1/p');"
            "[ -z \"$gw\" ] || ping -c 1 -W 2 \"$gw\" >/dev/null 2>&1 || ping -c 1 -W 2 \"$gw\" >/dev/null 2>&1 || { echo \"La box ($gw, par $dev) ne répond pas.\"; exit 1; };"
            "ping -c 2 -W 3 1.1.1.1 >/dev/null 2>&1 || { echo \"La box répond (par $dev), mais pas Internet.\"; exit 1; };"
            "nslookup github.com >/dev/null 2>&1 || { echo \"Internet répond, mais pas les noms de sites (DNS).\"; exit 1; };"
            "echo \"Internet fonctionne (par $dev).\"";
        const char *argv[] = { "/bin/busybox", "sh", "-c", script, NULL };
        s->job = plat_spawn(argv);
        s->job_kind = J_PING;
        strlcpy(s->result, "Test de la connexion…", sizeof s->result);
    }
    wm_invalidate(w);
}

static void net_tick(window_t *w, uint64_t now) {
    net_t *s = w->data;
    if (s->job >= 0) {
        char out[512];
        int r = plat_spawn_poll(s->job, out, sizeof out);
        if (r != -1) {
            if (s->job_kind == J_DHCP)
                snprintf(s->result, sizeof s->result, r == 0 ? "%s : adresse obtenue." : "%s : aucun serveur n'a répondu.", s->job_if);
            else {
                char *nl = strchr(out, '\n');
                if (nl) *nl = 0;
                strlcpy(s->result, out[0] ? out : r == 0 ? "Internet répond." : "Pas de réponse d'Internet.", sizeof s->result);
            }
            s->job = -1;
            s->next_list = 0;
            wm_invalidate(w);
        }
    }
    /* connexion demandée : étape en cours, puis un vrai résultat (ou la raison de l'échec) */
    if (s->conn[0]) {
        char last[120], msg[160];
        plat_wifi_last(last, sizeof last);
        int secs = (int)((now - s->conn_t0) / 1000);
        bool on = s->wstate == WIFI_CONNECTED && !strcmp(s->wssid, s->conn);
        const char *ip = NULL;
        for (int i = 0; i < s->nifs; i++) if (!strcmp(s->ifs[i].kind, "Wi-Fi") && s->ifs[i].ip[0]) ip = s->ifs[i].ip;
        bool done = true;
        if (on && ip) snprintf(msg, sizeof msg, "Connecté à « %s » · adresse %s.", s->conn, ip);
        else if (on) {
            if (!s->conn_ok_t) s->conn_ok_t = now;
            if (now - s->conn_ok_t > 20000)
                snprintf(msg, sizeof msg, "Connecté à « %s », mais la box ne donne pas d'adresse (DHCP). Voir la console.", s->conn);
            else { snprintf(msg, sizeof msg, "Connecté à « %s », demande d'une adresse… (%d s)", s->conn, secs); done = false; }
        } else if (s->wstate == WIFI_REFUSED)
            snprintf(msg, sizeof msg, "« %s » : %s.", s->conn, last[0] ? last : "mot de passe refusé");
        else if (now - s->conn_t0 > 35000) {
            if (last[0]) snprintf(msg, sizeof msg, "« %s » : la connexion n'aboutit pas (%s). Voir la console.", s->conn, last);
            else snprintf(msg, sizeof msg, "« %s » : la connexion n'aboutit pas. Voir la console.", s->conn);
        } else { snprintf(msg, sizeof msg, "Connexion à « %s »… %s (%d s)", s->conn, last, secs); done = false; }
        if (done) s->conn[0] = 0;
        if (strcmp(msg, s->result)) { strlcpy(s->result, msg, sizeof s->result); wm_invalidate(w); }
    }
    /* « Actualiser » : au bout de 5 s, le résultat remplace « Recherche… » */
    if (s->scan_t0 && now - s->scan_t0 > 5000) {
        s->scan_t0 = 0;
        if (s->nnets) snprintf(s->result, sizeof s->result, "%d réseau%s trouvé%s.", s->nnets, s->nnets > 1 ? "x" : "", s->nnets > 1 ? "s" : "");
        else strlcpy(s->result, "Aucun réseau trouvé.", sizeof s->result);
        wm_invalidate(w);
    }
    /* réseau enregistré : si son mot de passe est refusé (ou rien après 25 s), on le redemande */
    if (s->watch[0]) {
        if (s->wstate == WIFI_CONNECTED && !strcmp(s->wssid, s->watch)) s->watch[0] = 0;
        else if (s->wstate == WIFI_REFUSED || now - s->watch_t0 > 25000) {
            char ssid[33];
            strlcpy(ssid, s->watch, sizeof ssid);
            s->watch[0] = 0;
            ask_password(w, s, ssid, s->watch_eap, true);
        }
    }
    /* recherche régulière ; plus espacée une fois connecté (chaque recherche quitte un instant le canal) */
    if (now >= s->next_scan && s->wstate != WIFI_NONE && s->wstate != WIFI_CONNECTING) {
        s->next_scan = now + (s->wstate == WIFI_CONNECTED ? 30000 : 8000);
        plat_wifi_scan();
    }
    if (now >= s->next_list) {
        s->next_list = now + 1500;
        refresh(s);
        wm_invalidate(w);
    }
}

static void net_close(window_t *w) {
    if (cur_net == w) cur_net = NULL;
    free(w->data);
}

void app_network_open(void) {
    window_t *w = wm_find(ICON_NETWORK);
    if (w) { wm_focus(w); return; }
    net_t *s = calloc(1, sizeof *s);
    if (!s) return;
    s->job = -1;
    s->hover = H_NONE;
    s->opened = plat_ms();
    refresh(s);
    plat_wifi_scan();
    w = wm_open("Réseau", ICON_NETWORK, 640, 560, WF_SINGLE | WF_RESIZABLE);
    if (!w) { free(s); return; }
    w->minw = 480;
    w->minh = 400;
    w->data = s;
    w->draw = net_draw;
    w->event = net_event;
    w->tick = net_tick;
    w->on_close = net_close;
}

/* ================================================================== */
/* Console réseau : ce que font la carte Wi-Fi, wpa_supplicant et le DHCP */
/* ================================================================== */
#define CL_MAX 800
typedef struct { char t[12]; char s[150]; uint32_t col; } cline_t;
typedef struct {
    cline_t *l;
    int n, scroll, hover, job;
    bool follow;
    char status[160];
} con_t;
enum { C_DIAG = 1, C_SAVE, C_CLEAR };
static window_t *cur_con;

static uint32_t line_color(const char *s) {
    static const char *const bad[] = { "refus", "échou", "échec", "erreur", "error", "Error", "fail", "FAIL", "timed out", "REJECT",
                                       "DISCONNECTED", "TEMP-DISABLED", "ne répond pas", "introuvable", "pas de réponse", "débranché",
                                       "déconnecté", "deauth", "faux", "n'aboutit" };
    static const char *const good[] = { "CTRL-EVENT-CONNECTED", "connecté au Wi-Fi", ": adresse ", "associated", "clés échangées",
                                        "identifiant accepté", "Internet fonctionne", "branché sur", "partage de connexion USB" };
    for (int i = 0; i < countof(bad); i++) if (strstr(s, bad[i])) return 0xFFFCA5A5;
    for (int i = 0; i < countof(good); i++) if (strstr(s, good[i])) return 0xFF86EFAC;
    if (str_starts(s, "diag")) return 0xFFE2E8F0;
    if (str_starts(s, "wpa") || str_starts(s, "Wi-Fi") || str_starts(s, "réseau")) return 0xFFCBD5E1;
    return 0xFF94A3B8;                                    /* messages du noyau */
}

/* heure locale d'un message (microsecondes depuis le démarrage) */
static void line_time(uint64_t usec, char out[12]) {
    datetime_t d;
    local_now(&d);
    long now_s = d.hour * 3600 + d.min * 60 + d.sec;
    long ago = (long)(plat_ms() / 1000) - (long)(usec / 1000000);
    long t = ((now_s - ago) % 86400 + 86400) % 86400;
    snprintf(out, 12, "%02d:%02d:%02d", (int)(t / 3600), (int)(t / 60 % 60), (int)(t % 60));
}

static void con_add(con_t *c, const char *tm, const char *s) {
    uint32_t col = line_color(s);
    int len = (int)strlen(s), off = 0;
    while (off < len) {                                   /* lignes longues : coupées (sans casser un caractère) */
        int take = MIN(len - off, (int)sizeof c->l[0].s - 1);
        if (off + take < len) {
            int sp = take;
            while (sp > 60 && s[off + sp] != ' ') sp--;
            if (sp > 60) take = sp;
            while (take > 1 && ((uint8_t)s[off + take] & 0xC0) == 0x80) take--;
        }
        if (c->n >= CL_MAX) { memmove(c->l, c->l + 100, sizeof c->l[0] * (CL_MAX - 100)); c->n -= 100; c->scroll = MAX(0, c->scroll - 100); }
        cline_t *l = &c->l[c->n++];
        strlcpy(l->t, off ? "" : tm, sizeof l->t);
        memcpy(l->s, s + off, (size_t)take);
        l->s[take] = 0;
        l->col = col;
        off += take;
        while (off < len && s[off] == ' ') off++;
    }
}

static rect_t con_btn(canvas_t *c, int k) {             /* 1 diagnostic, 2 enregistrer, 3 effacer */
    static const int w[] = { 0, 190, 130, 110 };
    int x = c->w - 16;
    for (int i = 3; i >= k; i--) x -= w[i] + (i < 3 ? 10 : 0);
    return R(x, 16, w[k], 36);
}
static rect_t con_list(canvas_t *c) { return R(16, 66, c->w - 32, c->h - 66 - 40); }
#define CL_H 17

static void con_draw(window_t *w, canvas_t *c) {
    con_t *s = w->data;
    text_draw(c, &font_title, 20, 14, "Console réseau", C_TEXT);
    text_draw(c, &font_small, 20, 38, "Wi-Fi, wpa_supplicant, câble et DHCP, en direct", C_MUTED);
    ui_button(c, con_btn(c, C_DIAG), s->job >= 0 ? "Diagnostic en cours…" : "Diagnostic complet", BTN_ACCENT, s->hover == C_DIAG);
    ui_button(c, con_btn(c, C_SAVE), "Enregistrer", BTN_NORMAL, s->hover == C_SAVE);
    ui_button(c, con_btn(c, C_CLEAR), "Effacer", BTN_NORMAL, s->hover == C_CLEAR);
    rect_t L = con_list(c);
    gfx_rrect(c, L.x, L.y, L.w, L.h, 12, 0x70000000);
    rect_t old = c->clip;
    canvas_clip(c, rect_grow(L, -4));
    int vis = (L.h - 12) / CL_H;
    if (s->follow) s->scroll = MAX(0, s->n - vis);
    if (!s->n) text_center(c, &font_body, R(L.x, L.y + 20, L.w, 22), "Aucun message réseau pour l'instant.", C_MUTED);
    for (int i = s->scroll; i < s->n && i < s->scroll + vis + 1; i++) {
        int y = L.y + 6 + (i - s->scroll) * CL_H;
        text_draw(c, &font_small, L.x + 10, y, s->l[i].t, 0xFF64748B);
        text_fit(c, &font_small, L.x + 74, y, L.w - 90, s->l[i].s, s->l[i].col);
    }
    c->clip = old;
    ui_scrollbar(c, R(L.x + L.w - 8, L.y + 8, 4, L.h - 16), s->n, vis, s->scroll);
    text_fit(c, &font_small, 20, c->h - 30, c->w - 40, s->status[0] ? s->status :
             "« Enregistrer » crée Documents/console-reseau.txt : envoyez ce fichier (ou une photo de cet écran).", C_MUTED);
}

static void con_save(con_t *s) {
    size_t cap = (size_t)s->n * 170 + 256, k = 0;
    char *b = malloc(cap);
    if (!b) return;
    k += (size_t)snprintf(b + k, cap - k, "TitanOs " TITANOS_VERSION " - console réseau\n\n");
    for (int i = 0; i < s->n && k < cap - 200; i++)
        k += (size_t)snprintf(b + k, cap - k, "%-8s %s\n", s->l[i].t, s->l[i].s);
    char p[300];
    snprintf(p, sizeof p, "%s/Documents/console-reseau.txt", plat_home());
    bool ok = plat_writefile(p, b, k) == 0;
    free(b);
    snprintf(s->status, sizeof s->status, ok ? "Enregistré : Documents/console-reseau.txt (%d lignes)." : "Enregistrement impossible.", s->n);
}

static void con_event(window_t *w, const event_t *e) {
    con_t *s = w->data;
    canvas_t *c = &w->surf;
    rect_t L = con_list(c);
    int vis = (L.h - 12) / CL_H;
    if (e->type == EV_MOTION) {
        int h = 0;
        for (int k = C_DIAG; k <= C_CLEAR; k++) if (rect_has(con_btn(c, k), e->x, e->y)) h = k;
        if (h != s->hover) { s->hover = h; wm_invalidate(w); }
        return;
    }
    if (e->type == EV_WHEEL) {
        s->scroll = CLAMP(s->scroll + e->dy * 3, 0, MAX(0, s->n - vis));
        s->follow = s->scroll >= s->n - vis;
        wm_invalidate(w);
        return;
    }
    if (e->type != EV_BUTTON || !e->down || !s->hover) return;
    sound_play(SFX_CLICK);
    if (s->hover == C_DIAG && s->job < 0) {
        /* état complet du réseau, écrit dans le journal du noyau : il arrive ici comme le reste */
        static const char script[] =
            "{ echo '== cartes'; ip -4 addr; echo '== routes'; ip route; echo '== DNS'; cat /etc/resolv.conf;"
            " for n in /sys/class/net/*; do echo \"${n##*/} etat=$(cat $n/operstate) lien=$(cat $n/carrier 2>/dev/null)\"; done;"
            " for s in /run/wpa_supplicant/*; do [ -S \"$s\" ] || continue; i=${s##*/}; echo \"== wpa_supplicant $i\";"
            " wpa_cli -p /run/wpa_supplicant -i $i status | grep -vE 'passphrase|psk'; wpa_cli -p /run/wpa_supplicant -i $i list_networks; done;"
            " for w in /sys/class/net/*/wireless; do [ -e \"$w\" ] || continue; d=${w%/wireless}; d=${d##*/}; echo \"== liaison $d\";"
            " iw dev $d link; iw dev $d station dump | grep -E 'signal|bitrate'; done;"
            " echo '== pays'; iw reg get | head -n 4; echo '== test'; ping -c 2 -W 3 1.1.1.1 | tail -n 2;"
            " nslookup github.com 2>&1 | tail -n 2; } 2>&1 | while IFS= read -r l; do echo \"<6>titanos: diag : $l\" > /dev/kmsg; done";
        const char *argv[] = { "/bin/busybox", "sh", "-c", script, NULL };
        s->job = plat_spawn(argv);
        strlcpy(s->status, "Diagnostic en cours : adresses, routes, wpa_supplicant, liaison, test d'Internet…", sizeof s->status);
    } else if (s->hover == C_SAVE) con_save(s);
    else if (s->hover == C_CLEAR) { s->n = 0; s->scroll = 0; s->follow = true; s->status[0] = 0; }
    wm_invalidate(w);
}

static void con_tick(window_t *w, uint64_t now) {
    con_t *s = w->data;
    (void)now;
    bool added = false;
    char line[400], tm[12];
    uint64_t us;
    for (int k = 0; k < 300 && plat_netlog_next(line, sizeof line, &us); k++) {
        line_time(us, tm);
        con_add(s, tm, line);
        added = true;
    }
    if (s->job >= 0) {
        char out[64];
        int r = plat_spawn_poll(s->job, out, sizeof out);
        if (r != -1) { s->job = -1; strlcpy(s->status, "Diagnostic terminé.", sizeof s->status); added = true; }
    }
    if (added) wm_invalidate(w);
}

static void con_close(window_t *w) {
    con_t *s = w->data;
    if (cur_con == w) cur_con = NULL;
    free(s->l);
    free(s);
}

void app_netconsole_open(void) {
    if (cur_con && cur_con->used) { wm_focus(cur_con); return; }
    con_t *s = calloc(1, sizeof *s);
    if (!s) return;
    s->l = calloc(CL_MAX, sizeof *s->l);
    if (!s->l) { free(s); return; }
    s->job = -1;
    s->follow = true;
    window_t *w = wm_open("Console réseau", ICON_TERMINAL, 900, 540, WF_RESIZABLE);
    if (!w) { free(s->l); free(s); return; }
    w->minw = 560;
    w->minh = 320;
    w->data = s;
    w->draw = con_draw;
    w->event = con_event;
    w->tick = con_tick;
    w->on_close = con_close;
    cur_con = w;
    plat_netlog_rewind();                                 /* tout depuis le démarrage */
}
