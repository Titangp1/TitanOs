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
    char pending[33];                 /* réseau en attente du mot de passe */
    char pending_id[128];             /* identifiant (WPA2-Entreprise) */
    char menu_ssid[33];               /* réseau visé par le menu (clic droit) */
} net_t;

enum { J_NONE, J_DHCP, J_PING };
enum { H_NONE = -1, H_REFRESH = 1000, H_PING = 1001, H_HIDDEN = 1002, H_WIRED = 2000 };

static window_t *cur_net;

/* ---- disposition ---- */
static int wired_count(net_t *s) { int n = 0; for (int i = 0; i < s->nifs; i++) if (strcmp(s->ifs[i].kind, "Wi-Fi")) n++; return n; }
static rect_t list_rect(canvas_t *c, net_t *s) {
    int wired_h = wired_count(s) ? 40 + wired_count(s) * 58 : 0;
    return R(16, 66, c->w - 32, c->h - 66 - wired_h - 70);
}
static rect_t refresh_rect(canvas_t *c) { return R(c->w - 16 - 130, 16, 130, 36); }
static rect_t hidden_rect(canvas_t *c) { return R(c->w - 16 - 130 - 10 - 160, 16, 160, 36); }
static rect_t ping_rect(canvas_t *c) { return R(16, c->h - 54, 190, 38); }
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
    text_fit(c, &font_body, 222, c->h - 45, c->w - 240, s->result, C_TEXT2);
}

/* ---- actions ---- */
static void connect_with(const char *ssid, const char *psk, net_t *s) {
    if (!plat_wifi_connect(ssid, psk)) { wm_toast("Impossible de se connecter à ce réseau."); return; }
    snprintf(s->result, sizeof s->result, "Connexion à « %s »…", ssid);
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
