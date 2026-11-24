/* TitanOs - Wi-Fi : pilote du noyau + wpa_supplicant, piloté par son interface de contrôle
 *
 * wpa_supplicant (Alpine) gère l'authentification WPA ; TitanOs lui parle par une socket
 * locale avec des commandes texte : SCAN, SCAN_RESULTS, STATUS, ADD_NETWORK...
 * Les réseaux enregistrés sont gardés sur la clé (.titanos/wifi.conf). */
#include "linux.h"

static char iface[16];
static int ctrl = -1;
static uint64_t next_try;

/* ---- démarrage ---- */
static void rfkill_cb(const char *name, int type, void *ctx) {
    (void)type; (void)ctx;
    char p[96];
    snprintf(p, sizeof p, "/sys/class/rfkill/%s/soft", name);
    write_file(p, "0");                         /* débloque la radio (bouton logiciel) */
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

/* à appeler quand les pilotes sont chargés, puis de temps en temps (carte branchée plus tard) */
void wifi_start(void) {
    if (ctrl >= 0 || plat_ms() < next_try) return;
    next_try = plat_ms() + 4000;
    iface[0] = 0;
    for_each_dir("/sys/class/net", find_cb, NULL);
    if (!iface[0]) return;
    for_each_dir("/sys/class/rfkill", rfkill_cb, NULL);
    if (path_exists("/media/titanos")) mkdirs("/media/titanos/.titanos");
    const char *conf = conf_path();
    if (!path_exists(conf)) {
        const char *base = "ctrl_interface=/run/wpa_supplicant\nupdate_config=1\nap_scan=1\n";
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
    klog("Wi-Fi : %s prêt", iface);
}

/* envoie une commande, renvoie la longueur de la réponse (ou -1) */
static int request(const char *cmd, char *reply, int n) {
    if (ctrl < 0) wifi_start();
    if (ctrl < 0) return -1;
    char drain[64];
    while (sys_recv(ctrl, drain, sizeof drain, 0x40 /* MSG_DONTWAIT */) > 0) {}
    if (sys_write(ctrl, cmd, strlen(cmd)) < 0) { sys_close(ctrl); ctrl = -1; return -1; }
    struct pollfd p = { ctrl, POLLIN, 0 };
    if (sys_poll(&p, 1, 2500) <= 0) return -1;
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

/* ---- interface pour le bureau ---- */
int plat_wifi_status(char *ssid, int n, int *signal) {
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

void plat_wifi_scan(void) { ok("SCAN"); }

int plat_wifi_list(wifi_net_t *list, int max) {
    static char buf[16384];
    char cur[33];
    int sig;
    int state = plat_wifi_status(cur, sizeof cur, &sig);
    if (request("SCAN_RESULTS", buf, sizeof buf) <= 0) return 0;
    int n = 0;
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
                list[n].current = state != WIFI_OFF && !strcmp(ssid, cur);
                n++;
            }
            if (s > list[k].signal) list[k].signal = s;
        }
        line = nl;
    }
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

bool plat_wifi_connect(const char *ssid, const char *psk) {
    if (ctrl < 0) return false;
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
    } else {
        snprintf(cmd, sizeof cmd, "SET_NETWORK %d key_mgmt NONE", id);
        ok(cmd);
    }
    snprintf(cmd, sizeof cmd, "SELECT_NETWORK %d", id);
    bool r = ok(cmd);
    ok("SAVE_CONFIG");
    sys_sync();
    return r;
}

void plat_wifi_forget(const char *ssid) {
    int id = net_id(ssid);
    if (id < 0) return;
    char cmd[48];
    snprintf(cmd, sizeof cmd, "REMOVE_NETWORK %d", id);
    ok(cmd);
    ok("SAVE_CONFIG");
}

const char *wifi_iface(void) { return iface; }
