/* TitanOs - Wi-Fi : pilote du noyau + wpa_supplicant, piloté par son interface de contrôle
 *
 * wpa_supplicant (Alpine) gère l'authentification WPA ; TitanOs lui parle par une socket
 * locale avec des commandes texte : SCAN, SCAN_RESULTS, STATUS, ADD_NETWORK...
 * Les réseaux enregistrés sont gardés sur la clé (.titanos/wifi.conf).
 *
 * Tout ce qui peut être lent (wpa_supplicant, batterie lue par l'ACPI) se fait dans un
 * processus « veilleur » séparé : il envoie régulièrement un état complet au bureau, qui
 * ne fait que le lire. L'interface ne peut donc jamais rester bloquée par le matériel. */
#include "linux.h"

static char iface[16];
static int ctrl = -1;
static uint64_t next_try;

/* ---- démarrage ---- */
static void rfkill_cb(const char *name, int type, void *ctx) {
    (void)type; (void)ctx;
    char p[96], v[8];
    snprintf(p, sizeof p, "/sys/class/rfkill/%s/soft", name);
    /* seulement si la radio est bloquée : sur le VAIO, chaque écriture (même identique) est
     * transmise au contrôleur par le pilote sony-laptop, qui relance alors la carte Wi-Fi */
    if (read_file(p, v, sizeof v) > 0 && v[0] == '1') {
        write_file(p, "0");
        klog("Wi-Fi : radio %s débloquée", name);
    }
}

static void gov_cb(const char *name, int type, void *ctx) {
    (void)type;
    if (strncmp(name, "cpu", 3) || name[3] < '0' || name[3] > '9') return;
    char p[96];
    snprintf(p, sizeof p, "/sys/devices/system/cpu/%s/cpufreq/scaling_governor", name);
    write_file(p, (const char *)ctx);
}

/* ================================================================== */
/* Câble Ethernet et partage USB d'un téléphone                         */
/* ================================================================== */
/* Le veilleur surveille le lien de chaque carte filaire : câble branché -> adresse demandée
 * aussitôt ; débranché -> adresse retirée (Internet passe alors par la carte suivante,
 * voir /etc/udhcpc.script). */
typedef struct { char name[16]; int carrier; bool told; uint64_t kick; } wired_t;
static wired_t wired[8];
static int nwired;

static void if_up(const char *name) {
    int s = sys_socket(2, 2 | O_CLOEXEC, 0);
    if (s < 0) return;
    struct { char name[16]; short flags; char pad[22]; } ifr = { 0 };
    strlcpy(ifr.name, name, sizeof ifr.name);
    if (sys_ioctl(s, 0x8913 /* SIOCGIFFLAGS */, &ifr) == 0 && !(ifr.flags & 1)) {
        ifr.flags |= 1 /* IFF_UP */;
        sys_ioctl(s, 0x8914 /* SIOCSIFFLAGS */, &ifr);
    }
    sys_close(s);
}

static void spawn_detached(char *const *argv, char *const *env) {
    int c = sys_fork();
    if (c == 0) {
        sys_setsid();
        int nul = sys_open("/dev/null", O_RDWR, 0);
        if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 1); sys_dup2(nul, 2); }
        sys_execve(argv[0], argv, env);
        sys_exit(127);
    }
}

/* arrête le udhcpc d'une carte (il rend son adresse : -R) */
static void dhcp_stop(const char *ifn) {
    char p[48], v[16], proc[32];
    snprintf(p, sizeof p, "/run/udhcpc.%s.pid", ifn);
    int pid = read_file(p, v, sizeof v) > 0 ? atoi(v) : 0;
    snprintf(proc, sizeof proc, "/proc/%d", pid);
    if (pid > 1 && path_exists(proc)) {
        sys_kill(pid, 15);
        for (int i = 0; i < 20 && path_exists(proc); i++) {
            int st;
            sys_wait4(-1, &st, WNOHANG_);
            sleep_ms(25);
        }
    }
    sys_unlink(p);
}

static void dhcp_deconfig(const char *ifn) {
    char ienv[32];
    snprintf(ienv, sizeof ienv, "interface=%s", ifn);
    char *env[] = { ienv, "PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL };
    char *argv[] = { "/bin/busybox", "sh", "/etc/udhcpc.script", "deconfig", NULL };
    spawn_detached(argv, env);
}

static void dhcp_start(const char *ifn, bool release) {
    dhcp_stop(ifn);
    char p[48];
    snprintf(p, sizeof p, "/run/udhcpc.%s.pid", ifn);
    char ifc[16];
    strlcpy(ifc, ifn, sizeof ifc);
    char *a[] = { "/bin/busybox", "udhcpc", "-i", ifc, "-b", "-t", "6", "-T", "2", "-A", "4", "-x", "hostname:titanos",
                  "-p", p, "-s", "/etc/udhcpc.script", release ? "-R" : NULL, NULL };
    char *env[] = { "PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL };
    spawn_detached(a, env);
}

static void wired_cb(const char *name, int type, void *ctx) {
    (void)type; (void)ctx;
    if (!strcmp(name, "lo") || str_starts(name, "sit") || str_starts(name, "p2p") || str_starts(name, "wl") ||
        str_starts(name, "ip6") || str_starts(name, "tun") || str_starts(name, "dummy")) return;
    char p[96], v[32];
    snprintf(p, sizeof p, "/sys/class/net/%s/wireless", name);
    if (path_exists(p)) return;
    snprintf(p, sizeof p, "/sys/class/net/%s/device", name);
    if (!path_exists(p)) return;                                 /* interfaces virtuelles : pas touchées */
    wired_t *w = NULL;
    for (int i = 0; i < nwired; i++) if (!strcmp(wired[i].name, name)) w = &wired[i];
    if (!w) {
        if (nwired >= countof(wired)) return;
        w = &wired[nwired++];
        memset(w, 0, sizeof *w);
        strlcpy(w->name, name, sizeof w->name);
        w->carrier = -1;
    }
    if_up(name);
    snprintf(p, sizeof p, "/sys/class/net/%s/carrier", name);
    int c = read_file(p, v, sizeof v) > 0 && v[0] == '1';
    if (!c && plat_ms() > w->kick) {                    /* toujours rien : interface relancée (le PHY se réveille) */
        if (w->kick) {
            int s = sys_socket(2, 2 | O_CLOEXEC, 0);
            if (s >= 0) {
                struct { char name[16]; short flags; char pad[22]; } ifr = { 0 };
                strlcpy(ifr.name, name, sizeof ifr.name);
                if (sys_ioctl(s, 0x8913, &ifr) == 0) { ifr.flags &= ~1; sys_ioctl(s, 0x8914, &ifr); ifr.flags |= 1; sys_ioctl(s, 0x8914, &ifr); }
                sys_close(s);
            }
        }
        w->kick = plat_ms() + 30000;
    }
    if (c == w->carrier) return;
    bool usb = false;
    char link[256];
    snprintf(p, sizeof p, "/sys/class/net/%s/device", name);
    long r = sys_readlink(p, link, sizeof link - 1);
    if (r > 0) { link[r] = 0; usb = strstr(link, "-") && strstr(link, ":") && !strstr(link, "0000:"); }
    if (c) {
        snprintf(p, sizeof p, "/sys/class/net/%s/speed", name);
        int mb = read_file(p, v, sizeof v) > 0 ? atoi(v) : 0;
        if (usb) klog("réseau : partage de connexion USB sur %s, demande d'adresse", name);
        else if (mb > 0) klog("réseau : câble branché sur %s (%d Mbit/s), demande d'adresse", name, mb);
        else klog("réseau : câble branché sur %s, demande d'adresse", name);
        dhcp_start(name, true);
    } else if (w->carrier == 1) {
        klog("réseau : %s débranché, adresse retirée", name);
        dhcp_stop(name);
        dhcp_deconfig(name);
    } else if (!w->told) {
        klog("réseau : %s prêt, pas de câble", name);
        w->told = true;
    }
    w->carrier = c;
}

/* connexion Wi-Fi établie : demande une adresse tout de suite, sans attendre le prochain
 * essai de udhcpc (qui peut être 5 à 20 s plus tard) */
static void dhcp_now(void) {
    char p[48], v[16];
    snprintf(p, sizeof p, "/run/udhcpc.%s.pid", iface);
    int pid = read_file(p, v, sizeof v) > 0 ? atoi(v) : 0;
    char proc[32];
    snprintf(proc, sizeof proc, "/proc/%d", pid);
    if (pid > 1 && path_exists(proc)) { sys_kill(pid, 10 /* SIGUSR1 : renouveler */); return; }
    int c = sys_fork();
    if (c == 0) {
        sys_setsid();
        int nul = sys_open("/dev/null", O_RDWR, 0);
        if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 1); sys_dup2(nul, 2); }
        char *argv[] = { "/bin/busybox", "udhcpc", "-i", iface, "-b", "-t", "4", "-T", "3", "-A", "5", "-x", "hostname:titanos",
                         "-p", p, "-s", "/etc/udhcpc.script", NULL };
        sys_execve(argv[0], argv, g_envp);
        sys_exit(127);
    }
    klog("Wi-Fi : connecté, demande d'adresse (DHCP)");
}

/* radio coupée par un interrupteur matériel (« hard ») ? */
static void hard_cb(const char *name, int type, void *ctx) {
    (void)type;
    char p[96], v[16];
    snprintf(p, sizeof p, "/sys/class/rfkill/%s/type", name);
    if (read_file(p, v, sizeof v) <= 0 || strncmp(v, "wlan", 4)) return;
    snprintf(p, sizeof p, "/sys/class/rfkill/%s/hard", name);
    if (read_file(p, v, sizeof v) > 0 && v[0] == '1') *(bool *)ctx = true;
}

static void find_cb(const char *name, int type, void *ctx) {
    (void)type; (void)ctx;
    char p[96];
    snprintf(p, sizeof p, "/sys/class/net/%s/wireless", name);
    if (!iface[0] && path_exists(p)) strlcpy(iface, name, sizeof iface);
}

static const char *conf_path(void) {
    return path_exists("/media/titanos") && is_dir("/media/titanos/.titanos") ? "/media/titanos/.titanos/wifi.conf" : "/run/wifi.conf";
}

/* dans le veilleur : prépare le Wi-Fi dès qu'une carte apparaît */
static bool ok(const char *cmd);
static void mon_open(void);

static void wifi_start(void) {
    if (ctrl >= 0 || plat_ms() < next_try) return;
    next_try = plat_ms() + 4000;
    /* chiffrement du Wi-Fi : sans ces modules, la recherche marche mais aucune connexion WPA */
    static bool crypto;
    if (!crypto) {
        crypto = true;
        static const char *const m[] = { "ccm", "ctr", "gcm", "ghash_generic", "cmac", "seqiv", "michael_mic", "aesni_intel", "libarc4" };
        for (int i = 0; i < countof(m); i++) module_load(m[i]);
    }
    iface[0] = 0;
    for_each_dir("/sys/class/net", find_cb, NULL);
    if (!iface[0]) return;
    for_each_dir("/sys/class/rfkill", rfkill_cb, NULL);
    if (path_exists("/media/titanos")) mkdirs("/media/titanos/.titanos");
    const char *conf = conf_path();
    if (!path_exists(conf)) {
        const char *base = "ctrl_interface=/run/wpa_supplicant\nupdate_config=1\nap_scan=1\ncountry=FR\n";
        plat_writefile(conf, base, strlen(base));
    }
    char sock[64];
    snprintf(sock, sizeof sock, "/run/wpa_supplicant/%s", iface);
    if (!path_exists(sock)) {
        int pid = sys_fork();
        if (pid == 0) {
            sys_setsid();
            int nul = sys_open("/dev/null", O_RDWR, 0);
            if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 1); sys_dup2(nul, 2); }
            char *argv[] = { "/sbin/wpa_supplicant", "-B", "-i", iface, "-c", (char *)conf, "-D", "nl80211,wext", NULL };
            sys_execve(argv[0], argv, g_envp);
            sys_exit(127);
        }
        for (int i = 0; i < 40 && !path_exists(sock); i++) sleep_ms(50);
        if (!path_exists(sock)) { klog("Wi-Fi : wpa_supplicant ne démarre pas sur %s", iface); return; }
    }
    /* socket locale, reliée à celle de wpa_supplicant */
    int fd = sys_socket(1 /* AF_UNIX */, 2 /* SOCK_DGRAM */ | O_CLOEXEC, 0);
    if (fd < 0) return;
    struct { uint16_t family; char path[108]; } local = { 1, { 0 } }, remote = { 1, { 0 } };
    snprintf(local.path, sizeof local.path, "/run/titanos-wpa-%d", sys_getpid());
    sys_unlink(local.path);
    strlcpy(remote.path, sock, sizeof remote.path);
    if (sys_bind(fd, &local, sizeof local) < 0 || sc3(__NR_connect, fd, (long)&remote, sizeof remote) < 0) {
        sys_close(fd);
        return;
    }
    ctrl = fd;
    /* pays : sans lui, la radio reste dans le mode « monde » (puissance et canaux réduits) */
    ok("SET country FR");
    ok("SET p2p_disabled 1");
    /* réception faible : un réseau entendu une fois reste dans la liste un moment, au lieu de
     * disparaître à la recherche suivante ; recherches automatiques de plus en plus espacées */
    ok("SET bss_expiration_age 600");
    ok("SET bss_expiration_scan_count 20");
    ok("SET filter_ssids 0");
    ok("SET autoscan exponential:3:60");
    ok("SET scan_cur_freq 0");
    mon_open();
    ok("SET sae_pwe 2");                                  /* WPA3 : les deux méthodes (téléphones récents) */
    if (path_exists("/usr/sbin/iw")) {                   /* économie d'énergie de la radio : coupée */
        int c = sys_fork();
        if (c == 0) {
            int nul = sys_open("/dev/null", O_RDWR, 0);
            if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 1); sys_dup2(nul, 2); }
            char *argv[] = { "/usr/sbin/iw", "dev", iface, "set", "power_save", "off", NULL };
            sys_execve(argv[0], argv, g_envp);
            sys_exit(127);
        }
    }
    klog("Wi-Fi : %s prêt", iface);
}

/* ---- événements de wpa_supplicant (ATTACH) : chaque étape d'une connexion, et la raison
 * d'un échec, notées dans le journal du noyau en clair (console réseau, journal-titanos.txt) */
static int mon = -1;
static char wlast[120];                          /* dernière étape ou erreur, pour l'application Réseau */

static void mon_open(void) {
    if (mon >= 0 || !iface[0]) return;
    char sock[64];
    snprintf(sock, sizeof sock, "/run/wpa_supplicant/%s", iface);
    int fd = sys_socket(1 /* AF_UNIX */, 2 /* SOCK_DGRAM */ | O_CLOEXEC, 0);
    if (fd < 0) return;
    struct { uint16_t family; char path[108]; } local = { 1, { 0 } }, remote = { 1, { 0 } };
    snprintf(local.path, sizeof local.path, "/run/titanos-wpamon-%d", sys_getpid());
    sys_unlink(local.path);
    strlcpy(remote.path, sock, sizeof remote.path);
    if (sys_bind(fd, &local, sizeof local) < 0 || sc3(__NR_connect, fd, (long)&remote, sizeof remote) < 0) { sys_close(fd); return; }
    char r[16];
    struct pollfd p = { fd, POLLIN, 0 };
    if (sc6(__NR_sendto, fd, (long)"ATTACH", 6, 0x40, 0, 0) < 0 || sys_poll(&p, 1, 1000) <= 0 || sys_recv(fd, r, sizeof r, 0) < 2 || r[0] != 'O') {
        sys_close(fd);
        return;
    }
    mon = fd;
    klog("wpa : écoute des événements de la connexion Wi-Fi");
}

static const char *reason_text(int r) {
    switch (r) {
    case 1: return "raison non précisée";
    case 2: return "authentification expirée";
    case 3: return "déconnexion demandée";
    case 4: return "inactivité";
    case 6: case 7: return "la borne ne reconnaît pas la carte";
    case 8: return "la carte quitte le réseau";
    case 14: return "erreur d'intégrité (mot de passe ?)";
    case 15: return "échange de clés expiré : mot de passe probablement faux";
    case 16: return "clé de groupe expirée";
    case 23: return "identifiant ou mot de passe refusé (802.1X)";
    case 34: return "trop de pertes : signal trop faible";
    default: return NULL;
    }
}

/* explication en français d'un événement (NULL : pas intéressant à expliquer) */
static const char *event_hint(const char *m, char *buf, int n) {
    if (strstr(m, "CTRL-EVENT-SSID-TEMP-DISABLED")) {
        if (strstr(m, "reason=WRONG_KEY")) return "mot de passe refusé";
        if (strstr(m, "reason=EAP_AUTH_FAILED") || strstr(m, "reason=AUTH_FAILED")) return "identifiant ou mot de passe refusé";
        if (strstr(m, "reason=CONN_FAILED")) return "la connexion échoue (signal trop faible ou borne qui refuse)";
        return "réseau mis de côté après plusieurs échecs";
    }
    if (strstr(m, "CTRL-EVENT-ASSOC-REJECT")) {
        const char *s = strstr(m, "status_code=");
        snprintf(buf, (size_t)n, "la borne refuse la connexion (code %d)%s", s ? atoi(s + 12) : 0,
                 s && atoi(s + 12) == 17 ? " : trop d'appareils connectés" : "");
        return buf;
    }
    if (strstr(m, "CTRL-EVENT-AUTH-REJECT")) return "la borne refuse l'authentification";
    if (strstr(m, "CTRL-EVENT-DISCONNECTED")) {
        const char *s = strstr(m, "reason=");
        int r = s ? atoi(s + 7) : 0;
        const char *t = reason_text(r);
        snprintf(buf, (size_t)n, "déconnecté%s%s%s", t ? " : " : "", t ? t : "", strstr(m, "locally_generated=1") ? " (par la carte)" : "");
        return buf;
    }
    if (strstr(m, "4-Way Handshake failed")) return "échange de clés échoué : mot de passe probablement faux";
    if (strstr(m, "CTRL-EVENT-EAP-FAILURE")) return "identifiant ou mot de passe refusé";
    if (strstr(m, "CTRL-EVENT-EAP-SUCCESS")) return "identifiant accepté";
    if (strstr(m, "CTRL-EVENT-EAP-STARTED")) return "identification (réseau d'entreprise)…";
    if (strstr(m, "CTRL-EVENT-CONNECTED")) return "connecté au Wi-Fi, demande d'une adresse…";
    if (strstr(m, "Trying to associate")) return "tentative de connexion…";
    if (strstr(m, "Trying to authenticate")) return "authentification auprès de la borne…";
    if (strstr(m, "Associated with")) return "associé, échange des clés…";
    if (strstr(m, "Key negotiation completed")) return "clés échangées";
    if (strstr(m, "timed out")) return "la borne ne répond pas (trop loin ?)";
    if (strstr(m, "CTRL-EVENT-NETWORK-NOT-FOUND")) return "réseau enregistré introuvable (hors de portée ?)";
    if (strstr(m, "CTRL-EVENT-SCAN-FAILED")) return "la carte refuse de chercher les réseaux";
    if (strstr(m, "CTRL-EVENT-REGDOM-CHANGE")) return "pays de la radio réglé";
    return NULL;
}

static void mon_read(void) {
    if (mon < 0) return;
    static char prev[160];
    static int repeats;
    char buf[1024];
    long n;
    while ((n = sys_recv(mon, buf, sizeof buf - 1, 0x40 /* MSG_DONTWAIT */)) > 0) {
        buf[n] = 0;
        char *m = buf;
        if (m[0] == '<') { char *e = strchr(m, '>'); if (e) m = e + 1; }
        char *nl = strchr(m, '\n');
        if (nl) *nl = 0;
        /* trop bavards : recherches, bornes ajoutées ou retirées, signal */
        if (strstr(m, "CTRL-EVENT-SCAN-STARTED") || strstr(m, "CTRL-EVENT-SCAN-RESULTS") || strstr(m, "CTRL-EVENT-BSS-") ||
            strstr(m, "CTRL-EVENT-SIGNAL-CHANGE") || strstr(m, "WPS-AP-AVAILABLE") || strstr(m, "CTRL-EVENT-SUBNET-STATUS-UPDATE")) continue;
        if (!strncmp(prev, m, sizeof prev - 1)) { repeats++; continue; }
        if (repeats) klog("wpa : (message précédent répété %d fois)", repeats);
        repeats = 0;
        strlcpy(prev, m, sizeof prev);
        char hb[120];
        const char *h = event_hint(m, hb, sizeof hb);
        klog("wpa : %.170s%s%s", m, h ? "  ->  " : "", h ? h : "");
        if (h) strlcpy(wlast, h, sizeof wlast);
    }
    if (n == 0 || (n < 0 && n != -EAGAIN)) { sys_close(mon); mon = -1; }
}

/* envoie une commande, renvoie la longueur de la réponse (ou -1) */
static int request(const char *cmd, char *reply, int n) {
    if (ctrl < 0) wifi_start();
    if (ctrl < 0) return -1;
    char drain[64];
    while (sys_recv(ctrl, drain, sizeof drain, 0x40 /* MSG_DONTWAIT */) > 0) {}
    /* envoi sans attente : si wpa_supplicant ne lit plus, on abandonne au lieu de bloquer */
    if (sc6(__NR_sendto, ctrl, (long)cmd, (long)strlen(cmd), 0x40 /* MSG_DONTWAIT */, 0, 0) < 0) {
        sys_close(ctrl);
        ctrl = -1;
        return -1;
    }
    struct pollfd p = { ctrl, POLLIN, 0 };
    if (sys_poll(&p, 1, 1500) <= 0) return -1;
    long r = sys_recv(ctrl, reply, (size_t)n - 1, 0);
    if (r < 0) return -1;
    reply[r] = 0;
    return (int)r;
}

static bool ok(const char *cmd) {
    char r[64];
    return request(cmd, r, sizeof r) > 0 && str_starts(r, "OK");
}

/* « \xc3\xa9 » -> octets (les noms de réseaux sont échappés par wpa_supplicant) */
static void unescape(const char *s, char *out, int n) {
    int k = 0;
    while (*s && k < n - 1) {
        if (s[0] == '\\' && s[1] == 'x' && s[2] && s[3]) {
            char h[3] = { s[2], s[3], 0 };
            out[k++] = (char)strtol(h, NULL, 16);
            s += 4;
        } else if (s[0] == '\\' && s[1]) {
            out[k++] = s[1];
            s += 2;
        } else out[k++] = *s++;
    }
    out[k] = 0;
}

static char *field(char *line, int idx) {       /* champ séparé par des tabulations */
    for (int i = 0; i < idx; i++) {
        line = strchr(line, '\t');
        if (!line) return NULL;
        line++;
    }
    return line;
}

static int net_id(const char *ssid) {
    static char buf[4096];
    if (request("LIST_NETWORKS", buf, sizeof buf) <= 0) return -1;
    char *line = strchr(buf, '\n');
    while (line && *line) {
        line++;
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *s = field(line, 1);
        if (s) {
            char *tab = strchr(s, '\t');
            if (tab) *tab = 0;
            char name[64];
            unescape(s, name, sizeof name);
            if (!strcmp(name, ssid)) return atoi(line);
        }
        line = nl;
    }
    return -1;
}

/* ---- commandes (exécutées dans le veilleur) ---- */
static int w_status(char *ssid, int n, int *signal) {
    ssid[0] = 0;
    *signal = 0;
    if (ctrl < 0) { wifi_start(); if (ctrl < 0) return WIFI_NONE; }
    char buf[1024];
    if (request("STATUS", buf, sizeof buf) <= 0) return WIFI_OFF;
    char *st = strstr(buf, "wpa_state="), *s = strstr(buf, "\nssid=");
    if (s) {
        s += 6;
        char *e = strchr(s, '\n');
        if (e) *e = 0;
        unescape(s, ssid, n);
        if (e) *e = '\n';
    }
    if (!st) return WIFI_OFF;
    st += 10;
    if (str_starts(st, "COMPLETED")) {
        char sp[256];
        if (request("SIGNAL_POLL", sp, sizeof sp) > 0) {
            char *r = strstr(sp, "RSSI=");
            if (r) *signal = CLAMP(2 * (atoi(r + 5) + 100), 0, 100);
        }
        return WIFI_CONNECTED;
    }
    if (str_starts(st, "ASSOC") || str_starts(st, "AUTH") || str_starts(st, "4WAY") || str_starts(st, "GROUP"))
        return WIFI_CONNECTING;
    return WIFI_OFF;
}

/* une recherche pendant l'authentification (4-way handshake) fait échouer la connexion */
static void w_scan(void) {
    char st[1024];
    if (request("STATUS", st, sizeof st) > 0 &&
        (strstr(st, "wpa_state=ASSOCIATING") || strstr(st, "wpa_state=ASSOCIATED") ||
         strstr(st, "wpa_state=AUTHENTICATING") || strstr(st, "wpa_state=4WAY_HANDSHAKE") ||
         strstr(st, "wpa_state=GROUP_HANDSHAKE")))
        return;
    if (!ok("SCAN") && path_exists("/usr/sbin/iw") && iface[0]) {
        int c = sys_fork();
        if (c == 0) {
            int nul = sys_open("/dev/null", O_RDWR, 0);
            if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 1); sys_dup2(nul, 2); }
            char *argv[] = { "/usr/sbin/iw", "dev", iface, "scan", "trigger", NULL };
            sys_execve(argv[0], argv, g_envp);
            sys_exit(127);
        }
    }
}

/* Bornes connues du noyau (« iw dev wlan0 scan dump ») : toutes celles entendues par les
 * dernières recherches (de wpa_supplicant ou d'ailleurs), même celles que wpa_supplicant
 * n'a pas gardées dans sa liste. Elles sont ajoutées aux réseaux affichés. */
static int kernel_count;
static int kernel_bss(wifi_net_t *list, int n, int max) {
    static char out[65536];
    static int outn;
    static uint64_t next;
    if (!iface[0] || !path_exists("/usr/sbin/iw")) return n;
    if (plat_ms() >= next) {
        next = plat_ms() + 4000;
        int p[2];
        if (sys_pipe2(p, O_CLOEXEC) == 0) {
            int c = sys_fork();
            if (c == 0) {
                sys_dup2(p[1], 1);
                int nul = sys_open("/dev/null", O_RDWR, 0);
                if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 2); }
                char *argv[] = { "/usr/sbin/iw", "dev", iface, "scan", "dump", NULL };
                sys_execve(argv[0], argv, g_envp);
                sys_exit(127);
            }
            sys_close(p[1]);
            outn = 0;
            if (c > 0) {
                uint64_t end = plat_ms() + 2000;
                for (;;) {
                    struct pollfd pf = { p[0], POLLIN, 0 };
                    if (sys_poll(&pf, 1, 200) <= 0) { if (plat_ms() > end) break; continue; }
                    long r = sys_read(p[0], out + outn, sizeof out - 1 - (size_t)outn);
                    if (r <= 0) break;
                    outn += (int)r;
                    if (outn >= (int)sizeof out - 1) break;
                }
                int st;
                sys_wait4(c, &st, 0);
            }
            sys_close(p[0]);
            out[outn] = 0;
        }
    }
    /* BSS xx:xx...  /  freq: 2412  /  signal: -67.00 dBm  /  SSID: nom  /  RSN, WPA, Privacy */
    char ssid[64] = "";
    int sig = -100, cnt = 0;
    bool sec = false, in = false, ent = false, sae = false;
    char *l = out;
    for (;;) {
        char *e = l ? strchr(l, '\n') : NULL;
        bool last = !l || !*l;
        if (last || str_starts(l, "BSS ")) {
            if (in && ssid[0]) {
                int s = CLAMP(2 * (sig + 100), 0, 100), k;
                for (k = 0; k < n; k++) if (!strcmp(list[k].ssid, ssid)) break;
                if (k == n && n < max) {
                    memset(&list[n], 0, sizeof list[n]);
                    strlcpy(list[n].ssid, ssid, sizeof list[n].ssid);
                    list[n].secure = sec;
                    list[n].enterprise = ent;
                    list[n].wpa3 = sae;
                    list[n].signal = s;
                    n++;
                } else if (k < n && s > list[k].signal) list[k].signal = s;
            }
            if (last) break;
            in = true;
            cnt++;
            ssid[0] = 0;
            sig = -100;
            sec = ent = sae = false;
        } else if (in) {
            const char *t = l;
            while (*t == '\t' || *t == ' ') t++;
            if (str_starts(t, "SSID: ")) {
                char raw[96];
                int len = e ? (int)(e - (t + 6)) : (int)strlen(t + 6);
                if (len > (int)sizeof raw - 1) len = sizeof raw - 1;
                memcpy(raw, t + 6, (size_t)len);
                raw[len] = 0;
                unescape(raw, ssid, sizeof ssid);
            } else if (str_starts(t, "signal: ")) sig = atoi(t + 8);
            else if (str_starts(t, "* Authentication suites:")) {
                char *nl2 = e ? e : (char *)t + strlen(t);
                char save = *nl2;
                *nl2 = 0;
                if (strstr(t, "802.1X")) ent = true;
                if (strstr(t, "SAE") && !strstr(t, "PSK")) sae = true;
                *nl2 = save;
            }
            else if (str_starts(t, "RSN:") || str_starts(t, "WPA:") || (str_starts(t, "capability:") && strstr(t, "Privacy"))) {
                char *nl2 = e ? e : (char *)t + strlen(t);
                char save = *nl2;
                *nl2 = 0;
                if (!str_starts(t, "capability:") || strstr(t, "Privacy")) sec = true;
                *nl2 = save;
            }
        }
        l = e ? e + 1 : NULL;
    }
    kernel_count = cnt;
    return n;
}

static int w_list(wifi_net_t *list, int max) {
    static char buf[16384];
    char cur[33];
    int sig;
    int state = w_status(cur, sizeof cur, &sig);
    if (request("SCAN_RESULTS", buf, sizeof buf) <= 0) return 0;
    int n = 0, bss = 0;
    for (char *q = strchr(buf, '\n'); q && q[1]; q = strchr(q + 1, '\n')) bss++;
    static int last_bss = -1;
    static uint64_t next_log;
    if (bss != last_bss || plat_ms() >= next_log) {
        /* détail pour le journal : nom, signal et canal de chaque borne entendue */
        char det[400];
        int k = 0;
        det[0] = 0;
        char *l = strchr(buf, '\n');
        for (int i = 0; l && l[1] && i < 10 && k < (int)sizeof det - 60; i++) {
            char *e = strchr(l + 1, '\n');
            char line[200];
            int len = e ? (int)(e - l - 1) : (int)strlen(l + 1);
            if (len > (int)sizeof line - 1) len = sizeof line - 1;
            memcpy(line, l + 1, (size_t)len);
            line[len] = 0;
            char *fq = field(line, 1), *lv = field(line, 2), *nm = field(line, 4);
            if (fq && lv) k += snprintf(det + k, sizeof det - (size_t)k, "%s%.24s (%d dBm, %d MHz)", i ? ", " : "", nm && *nm ? nm : "masqué", atoi(lv), atoi(fq));
            l = e;
        }
        klog("Wi-Fi : %d bornes pour wpa_supplicant, %d pour le noyau : %s", bss, kernel_count, det);
        last_bss = bss;
        next_log = plat_ms() + 60000;
    }
    char *line = strchr(buf, '\n');            /* première ligne : en-têtes */
    while (line && *line && n < max) {
        line++;
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *level = field(line, 2), *flags = field(line, 3), *name = field(line, 4);
        if (level && flags && name && *name) {
            char ssid[64];
            unescape(name, ssid, sizeof ssid);
            int dbm = atoi(level), s = CLAMP(2 * (dbm + 100), 0, 100);
            int k;
            for (k = 0; k < n; k++) if (!strcmp(list[k].ssid, ssid)) break;    /* un réseau, plusieurs bornes */
            if (k == n) {
                memset(&list[n], 0, sizeof list[n]);
                strlcpy(list[n].ssid, ssid, sizeof list[n].ssid);
                list[n].secure = strstr(flags, "WPA") || strstr(flags, "RSN") || strstr(flags, "WEP") || strstr(flags, "SAE");
                list[n].enterprise = strstr(flags, "EAP") != NULL;
                list[n].wpa3 = strstr(flags, "SAE") && !strstr(flags, "PSK");
                list[n].current = state != WIFI_OFF && !strcmp(ssid, cur);
                n++;
            }
            if (s > list[k].signal) list[k].signal = s;
        }
        line = nl;
    }
    n = kernel_bss(list, n, max);                     /* + bornes entendues par la carte */
    /* réseaux enregistrés */
    static char nets[4096];
    if (request("LIST_NETWORKS", nets, sizeof nets) > 0)
        for (int i = 0; i < n; i++) {
            char esc[64];
            snprintf(esc, sizeof esc, "\t%s\t", list[i].ssid);
            if (strstr(nets, esc)) list[i].known = true;
        }
    /* tri : réseau actuel, puis enregistrés, puis par signal */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            wifi_net_t *a = &list[j - 1], *b = &list[j];
            int ka = a->current * 1000 + a->known * 200 + a->signal, kb = b->current * 1000 + b->known * 200 + b->signal;
            if (kb <= ka) break;
            wifi_net_t t = *a; *a = *b; *b = t;
        }
    return n;
}

static bool wpa3_only(const char *ssid);
static bool w_connect(const char *ssid, const char *psk) {
    if (ctrl < 0) return false;
    wlast[0] = 0;
    char cmd[256], hex[80];
    int id = net_id(ssid);
    if (id >= 0 && !psk) {
        snprintf(cmd, sizeof cmd, "SELECT_NETWORK %d", id);
        return ok(cmd);
    }
    if (id < 0) {
        char r[32];
        if (request("ADD_NETWORK", r, sizeof r) <= 0 || r[0] < '0' || r[0] > '9') return false;
        id = atoi(r);
    }
    int k = 0;
    for (const uint8_t *p = (const uint8_t *)ssid; *p && k < 70; p++) k += snprintf(hex + k, sizeof hex - (size_t)k, "%02x", *p);
    snprintf(cmd, sizeof cmd, "SET_NETWORK %d ssid %s", id, hex);       /* nom en hexadécimal : aucun souci d'accents */
    if (!ok(cmd)) return false;
    if (psk) {
        snprintf(cmd, sizeof cmd, "SET_NETWORK %d psk \"%s\"", id, psk);
        if (!ok(cmd)) return false;
        /* WPA3 seul (partage de connexion des téléphones récents) : SAE, avec protection des
         * trames de gestion ; sinon WPA2, comme avant */
        if (wpa3_only(ssid)) {
            snprintf(cmd, sizeof cmd, "SET_NETWORK %d key_mgmt SAE", id);
            ok(cmd);
            snprintf(cmd, sizeof cmd, "SET_NETWORK %d ieee80211w 2", id);
            ok(cmd);
            klog("Wi-Fi : « %s » est en WPA3 seul (SAE)", ssid);
        } else {
            snprintf(cmd, sizeof cmd, "SET_NETWORK %d key_mgmt WPA-PSK", id);
            ok(cmd);
        }
    } else {
        snprintf(cmd, sizeof cmd, "SET_NETWORK %d key_mgmt NONE", id);
        ok(cmd);
    }
    snprintf(cmd, sizeof cmd, "SET_NETWORK %d scan_ssid 1", id);           /* aussi les réseaux masqués */
    ok(cmd);
    snprintf(cmd, sizeof cmd, "SELECT_NETWORK %d", id);
    bool r = ok(cmd);
    ok("SAVE_CONFIG");
    klog("Wi-Fi : connexion à « %s » %s", ssid, r ? "demandée" : "refusée");
    sys_sync();
    return r;
}

/* WPA2-Entreprise (802.1X) : identifiant et mot de passe, méthodes PEAP ou TTLS avec
 * MSCHAPv2 (eduroam, réseaux d'école ou d'entreprise) */
static bool w_connect_eap(const char *ssid, const char *identity, const char *password) {
    if (ctrl < 0) return false;
    wlast[0] = 0;
    char cmd[400], hex[80];
    int id = net_id(ssid);
    if (id < 0) {
        char r[32];
        if (request("ADD_NETWORK", r, sizeof r) <= 0 || r[0] < '0' || r[0] > '9') return false;
        id = atoi(r);
    }
    int k = 0;
    for (const uint8_t *p = (const uint8_t *)ssid; *p && k < 70; p++) k += snprintf(hex + k, sizeof hex - (size_t)k, "%02x", *p);
    snprintf(cmd, sizeof cmd, "SET_NETWORK %d ssid %s", id, hex);
    if (!ok(cmd)) return false;
    static const char *const set[] = { "key_mgmt WPA-EAP", "eap PEAP TTLS", "phase2 \"auth=MSCHAPV2\"", "scan_ssid 1" };
    for (int i = 0; i < countof(set); i++) {
        snprintf(cmd, sizeof cmd, "SET_NETWORK %d %s", id, set[i]);
        if (!ok(cmd) && i < 2) return false;
    }
    snprintf(cmd, sizeof cmd, "SET_NETWORK %d identity \"%s\"", id, identity);
    if (!ok(cmd)) return false;
    snprintf(cmd, sizeof cmd, "SET_NETWORK %d password \"%s\"", id, password);
    bool good = ok(cmd);
    memset(cmd, 0, sizeof cmd);
    if (!good) return false;
    snprintf(cmd, sizeof cmd, "SELECT_NETWORK %d", id);
    bool r = ok(cmd);
    ok("SAVE_CONFIG");
    klog("Wi-Fi : connexion à « %s » (identifiant) %s", ssid, r ? "demandée" : "refusée");
    sys_sync();
    return r;
}

/* le réseau n'accepte-t-il que WPA3 (SAE, sans PSK) ? d'après les résultats de recherche */
static bool wpa3_only(const char *ssid) {
    static char buf[16384];
    if (request("SCAN_RESULTS", buf, sizeof buf) <= 0) return false;
    bool sae = false, psk = false;
    for (char *l = strchr(buf, '\n'); l && l[1]; l = strchr(l + 1, '\n')) {
        char line[300];
        char *e = strchr(l + 1, '\n');
        int len = e ? (int)(e - l - 1) : (int)strlen(l + 1);
        if (len > (int)sizeof line - 1) len = sizeof line - 1;
        memcpy(line, l + 1, (size_t)len);
        line[len] = 0;
        char *flags = field(line, 3), *name = field(line, 4);
        if (!flags || !name) continue;
        char *tab = strchr(flags, '\t');
        if (tab) *tab = 0;
        char nm[64];
        unescape(name, nm, sizeof nm);
        if (strcmp(nm, ssid)) continue;
        if (strstr(flags, "SAE")) sae = true;
        if (strstr(flags, "PSK")) psk = true;
    }
    return sae && !psk;
}

static void wipe_mem(void *p, size_t n) { volatile char *v = p; while (n--) *v++ = 0; }   /* mots de passe effacés */

static void w_forget(const char *ssid) {
    /* toutes les entrées de ce nom (un réseau peut avoir été ajouté plusieurs fois) */
    for (int tries = 0; tries < 8; tries++) {
        int id = net_id(ssid);
        if (id < 0) break;
        char cmd[48];
        snprintf(cmd, sizeof cmd, "REMOVE_NETWORK %d", id);
        if (!ok(cmd)) break;
    }
    ok("SAVE_CONFIG");
    sys_sync();
    klog("Wi-Fi : réseau « %s » oublié", ssid);
}

/* ================================================================== */
/* Le veilleur : processus séparé qui interroge le matériel             */
/* ================================================================== */
#define SNAP_NETS 32
typedef struct {
    battery_t bat;
    int wstate, wsignal, nnets;
    char wssid[33];
    char wlast[120];                          /* dernière étape ou erreur de connexion */
    wifi_net_t nets[SNAP_NETS];
} snap_t;                                     /* < 4096 octets : écrit d'un seul bloc dans le tube */
typedef struct { int op; char ssid[33]; char psk[128]; char id[128]; } wcmd_t;
enum { OP_SCAN = 1, OP_CONNECT, OP_FORGET, OP_CONNECT_EAP };

static int snap_in = -1, cmd_out = -1;
static snap_t snap;
static bool have_snap;

static void helper_main(int out, int in) {
    for (int fd = 3; fd < 256; fd++) if (fd != out && fd != in) sys_close(fd);
    /* événements du noyau : chargeur branché ou débranché, batterie qui change d'état */
    int uev = uevent_open();
    uint64_t next = 0, next_bat = 0;
    battery_t bat_last;
    memset(&bat_last, 0, sizeof bat_last);
    for (;;) {
        uint64_t now = plat_ms();
        if (mon < 0 && ctrl >= 0) mon_open();
        struct pollfd p[3] = { { in, POLLIN, 0 }, { uev, POLLIN, 0 }, { mon, POLLIN, 0 } };
        if (sys_poll(p, 3, next > now ? (int)(next - now) : 0) > 0) {
            if (mon >= 0 && (p[2].revents & (POLLIN | POLLHUP | POLLERR))) { mon_read(); next = 0; }
            if (uev >= 0 && (p[1].revents & POLLIN)) {
                char ub[4096];
                long n;
                while ((n = sys_recv(uev, ub, sizeof ub - 1, 0)) > 0) {
                    ub[n] = 0;
                    for (long i = 0; i < n; i += (long)strlen(ub + i) + 1)
                        if (!strcmp(ub + i, "SUBSYSTEM=power_supply")) { next_bat = 0; next = 0; }
                }
            }
            if (!(p[0].revents & (POLLIN | POLLHUP))) continue;
            wcmd_t c;
            long r = sys_read(in, &c, sizeof c);
            if (r <= 0) sys_exit(0);                    /* le bureau s'est arrêté */
            if (r == sizeof c) {
                if (g_hosted) {                         /* Debian : NetworkManager */
                    if (c.op == OP_SCAN) nm_scan();
                    else if (c.op == OP_CONNECT) nm_connect(c.ssid, c.psk[0] ? c.psk : NULL, NULL);
                    else if (c.op == OP_FORGET) nm_forget(c.ssid);
                    else if (c.op == OP_CONNECT_EAP) nm_connect(c.ssid, c.psk, c.id);
                }
                else if (c.op == OP_SCAN) w_scan();
                else if (c.op == OP_CONNECT) w_connect(c.ssid, c.psk[0] ? c.psk : NULL);
                else if (c.op == OP_FORGET) w_forget(c.ssid);
                else if (c.op == OP_CONNECT_EAP) w_connect_eap(c.ssid, c.id, c.psk);
                wipe_mem(&c, sizeof c);
                next = plat_ms() + 600;                 /* nouvel état peu après */
            }
            continue;
        }
        int st, rp;
        while ((rp = sys_wait4(-1, &st, WNOHANG_)) > 0)  /* wpa_supplicant -B laisse un processus fini */
            if (g_hosted) nm_child(rp, st);
        snap_t s;
        memset(&s, 0, sizeof s);
        /* la batterie est lue par l'ACPI : sur certains portables (VAIO), chaque lecture
         * interrompt brièvement tout le processeur. Toutes les 15 s suffisent, et tout de
         * suite quand le noyau signale un changement (chargeur branché, batterie pleine). */
        if (plat_ms() >= next_bat) { bat_read(&bat_last); next_bat = plat_ms() + 15000; }
        s.bat = bat_last;
        static int gov = -1;                 /* 1 : performance (secteur), 0 : schedutil (batterie) */
        int want = s.bat.present && !s.bat.ac ? 0 : 1;
        if (want != gov) { gov = want; for_each_dir("/sys/devices/system/cpu", gov_cb, (void *)(want ? "performance" : "schedutil")); }
        if (g_hosted) {                                 /* Debian : NetworkManager fait tout le réseau */
            nm_start();
            for_each_dir("/sys/class/rfkill", rfkill_cb, NULL);
            s.wstate = nm_state(s.wssid, sizeof s.wssid, &s.wsignal);
            bool hard = false;
            for_each_dir("/sys/class/rfkill", hard_cb, &hard);
            if (hard && s.wstate != WIFI_CONNECTED) s.wstate = WIFI_BLOCKED;
            s.nnets = nm_list(s.nets, SNAP_NETS);
            strlcpy(s.wlast, nm_last(), sizeof s.wlast);
            static int nm_last_state = -9;
            if (s.wstate != nm_last_state) {
                static const char *const nm[] = { "aucune carte", "non connecté", "connexion", "connecté", "interrupteur sur OFF", "mot de passe refusé" };
                klog("Wi-Fi : %s%s%s (%d réseaux visibles)", nm[s.wstate + 1], s.wssid[0] ? " - " : "", s.wssid, s.nnets);
                nm_last_state = s.wstate;
            }
            sys_write(out, &s, sizeof s);
            next = plat_ms() + (s.wstate == WIFI_CONNECTING ? 1000 : 2500);
            continue;
        }
        wifi_start();
        s.wstate = ctrl >= 0 ? w_status(s.wssid, sizeof s.wssid, &s.wsignal) : WIFI_NONE;
        for_each_dir("/sys/class/rfkill", rfkill_cb, NULL);      /* l'interrupteur logiciel peut revenir */
        bool hard = false;
        for_each_dir("/sys/class/rfkill", hard_cb, &hard);
        if (hard) s.wstate = WIFI_BLOCKED;
        if (ctrl >= 0 && s.wstate != WIFI_CONNECTED && s.wstate != WIFI_BLOCKED) {
            /* wpa_supplicant met de côté un réseau dont la clé est refusée */
            static char lb[4096];
            if (request("LIST_NETWORKS", lb, sizeof lb) > 0 && strstr(lb, "[CURRENT]") && strstr(lb, "TEMP-DISABLED"))
                s.wstate = WIFI_REFUSED;
            /* recherche forcée seulement si aucun réseau n'est visible et qu'aucune connexion
             * n'est en cours : une recherche pendant l'authentification la fait échouer */
            static uint64_t next_scan;
            if (s.wstate == WIFI_OFF && w_list(s.nets, SNAP_NETS) == 0 && plat_ms() >= next_scan) {
                next_scan = plat_ms() + 30000;
                w_scan();
            }
        }
        static int last_state = -9;
        static uint64_t wifi_lost;
        if (s.wstate == WIFI_CONNECTED && last_state != WIFI_CONNECTED) { dhcp_now(); wifi_lost = 0; }
        if (s.wstate != WIFI_CONNECTED && last_state == WIFI_CONNECTED) wifi_lost = plat_ms();
        if (wifi_lost && plat_ms() - wifi_lost > 15000 && iface[0]) {   /* déconnecté depuis 15 s : adresse retirée */
            wifi_lost = 0;
            dhcp_stop(iface);
            dhcp_deconfig(iface);
            klog("Wi-Fi : déconnecté, adresse retirée");
        }
        for_each_dir("/sys/class/net", wired_cb, NULL);
        if (s.wstate != last_state) {
            static const char *const nm[] = { "aucune carte", "non connecté", "connexion", "connecté", "interrupteur sur OFF", "mot de passe refusé" };
            klog("Wi-Fi : %s%s%s (%d réseaux visibles)", nm[s.wstate + 1], s.wssid[0] ? " - " : "", s.wssid, s.nnets);
            last_state = s.wstate;
        }
        if (ctrl >= 0) s.nnets = w_list(s.nets, SNAP_NETS);
        strlcpy(s.wlast, wlast, sizeof s.wlast);
        sys_write(out, &s, sizeof s);                   /* tube non bloquant : perdu si le bureau est en retard */
        next = plat_ms() + 2000;
    }
}

void hw_start(void) {
    int a[2], b[2];
    if (sys_pipe2(a, O_CLOEXEC) < 0) return;
    if (sys_pipe2(b, O_CLOEXEC) < 0) { sys_close(a[0]); sys_close(a[1]); return; }
    int pid = sys_fork();
    if (pid == 0) {
        sys_close(a[0]);
        sys_close(b[1]);
        sc3(__NR_fcntl, a[1], F_SETFL, O_NONBLOCK);
        helper_main(a[1], b[0]);
        sys_exit(0);
    }
    sys_close(a[1]);
    sys_close(b[0]);
    if (pid < 0) { sys_close(a[0]); sys_close(b[1]); return; }
    sc3(__NR_fcntl, a[0], F_SETFL, O_NONBLOCK);
    sc3(__NR_fcntl, b[1], F_SETFL, O_NONBLOCK);
    snap_in = a[0];
    cmd_out = b[1];
}

static void hw_poll(void) {
    if (snap_in < 0) return;
    snap_t s;
    while (sys_read(snap_in, &s, sizeof s) == sizeof s) { snap = s; have_snap = true; }
}

static void send_cmd_id(int op, const char *ssid, const char *psk, const char *id) {
    if (cmd_out < 0) return;
    wcmd_t c;
    memset(&c, 0, sizeof c);
    if (id) strlcpy(c.id, id, sizeof c.id);
    c.op = op;
    if (ssid) strlcpy(c.ssid, ssid, sizeof c.ssid);
    if (psk) strlcpy(c.psk, psk, sizeof c.psk);
    sys_write(cmd_out, &c, sizeof c);
}
static void send_cmd(int op, const char *ssid, const char *psk) { send_cmd_id(op, ssid, psk, NULL); }

/* ---- interface pour le bureau : réponses immédiates, d'après le dernier état reçu ---- */
int plat_wifi_status(char *ssid, int n, int *signal) {
    hw_poll();
    strlcpy(ssid, have_snap ? snap.wssid : "", (size_t)n);
    *signal = have_snap ? snap.wsignal : 0;
    return have_snap ? snap.wstate : WIFI_NONE;
}

int plat_wifi_list(wifi_net_t *list, int max) {
    hw_poll();
    int n = have_snap ? MIN(max, snap.nnets) : 0;
    memcpy(list, snap.nets, sizeof(wifi_net_t) * (size_t)n);
    return n;
}

void plat_wifi_scan(void) { send_cmd(OP_SCAN, NULL, NULL); }
bool plat_wifi_connect(const char *ssid, const char *psk) {
    if (!have_snap || snap.wstate == WIFI_NONE) return false;
    send_cmd(OP_CONNECT, ssid, psk);
    return true;
}

bool plat_wifi_connect_eap(const char *ssid, const char *identity, const char *password) {
    if (!have_snap || snap.wstate == WIFI_NONE) return false;
    send_cmd_id(OP_CONNECT_EAP, ssid, password, identity);
    return true;
}
void plat_wifi_forget(const char *ssid) { send_cmd(OP_FORGET, ssid, NULL); }
void plat_wifi_last(char *buf, int n) { hw_poll(); strlcpy(buf, have_snap ? snap.wlast : "", (size_t)n); }

void plat_battery(battery_t *b) {
    hw_poll();
    if (have_snap) *b = snap.bat;
    else memset(b, 0, sizeof *b);
}
