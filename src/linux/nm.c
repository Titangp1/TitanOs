/* TitanOs - Wi-Fi par NetworkManager (base Debian)
 *
 * Dans Debian, NetworkManager s'occupe de toutes les cartes réseau : Wi-Fi (WPA2, WPA3,
 * WPA2-Entreprise, réseaux masqués), câble Ethernet, partage de connexion d'un téléphone.
 * Le veilleur (wifi.c) l'interroge avec nmcli, en sortie « terse » (champs séparés par « : »).
 *
 * Les réseaux enregistrés sont des fichiers de NetworkManager (/etc/NetworkManager/
 * system-connections, lisibles par root seulement) écrits ici : aucun mot de passe ne passe
 * sur une ligne de commande. Les réseaux enregistrés par l'ancien TitanOs (wifi.conf, sur la
 * clé) sont repris une fois. */
#include "linux.h"

#define nmcli(out, n, ...) nmv(out, n, (const char *const[]){ __VA_ARGS__, NULL })
static void nm_wipe(void *p, size_t n) { volatile uint8_t *v = p; while (n--) *v++ = 0; }

#define NM_DIR "/etc/NetworkManager/system-connections"
static char *nm_env[] = { "PATH=/usr/sbin:/usr/bin:/sbin:/bin", "LC_ALL=C", "LANG=C", NULL };
static char wdev[24];                   /* carte Wi-Fi (wlp2s0...) */
static char nlast[140];                 /* dernière étape ou erreur, en clair */
static int conn_pid = -1;               /* connexion en cours (nmcli connection up) */
static char conn_ssid[33];
static bool refused;                    /* mot de passe refusé : jusqu'à la prochaine tentative */
static wifi_net_t cache[48];            /* dernière liste : sécurité de chaque réseau */
static int ncache;

/* ---------------- commandes ---------------- */
/* lance nmcli (ou une autre commande) et lit sa sortie ; renvoie son code de sortie */
static int capture(char *const *argv, char *out, int n, int timeout_ms) {
    int p[2];
    if (sys_pipe2(p, O_CLOEXEC) < 0) return -1;
    int pid = sys_fork();
    if (pid == 0) {
        sys_dup2(p[1], 1);
        int nul = sys_open("/dev/null", O_RDWR, 0);
        if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 2); }
        sys_execve(argv[0], argv, nm_env);
        sys_exit(127);
    }
    sys_close(p[1]);
    int len = 0;
    uint64_t end = plat_ms() + (uint64_t)timeout_ms;
    while (pid > 0) {
        long left = (long)(end - plat_ms());
        if (left <= 0 || (int64_t)(end - plat_ms()) < 0) { sys_kill(pid, 9); break; }
        struct pollfd pf = { p[0], POLLIN, 0 };
        if (sys_poll(&pf, 1, (int)left) <= 0) continue;
        char junk[512];
        long r = len < n - 1 ? sys_read(p[0], out + len, (size_t)(n - 1 - len)) : sys_read(p[0], junk, sizeof junk);
        if (r <= 0) break;
        if (len < n - 1) len += (int)r;
    }
    if (n > 0) out[len] = 0;
    sys_close(p[0]);
    int st = -1;
    if (pid > 0) sys_wait4(pid, &st, 0);
    return (st & 0x7F) ? -1 : (st >> 8) & 0xFF;
}

static int nmv(char *out, int n, const char *const *a) {
    char *argv[12] = { "/usr/bin/nmcli" };
    int k = 1;
    for (int i = 0; a[i] && k < 11; i++) argv[k++] = (char *)a[i];
    argv[k] = NULL;
    static char dummy[256];
    return capture(argv, out ? out : dummy, out ? n : (int)sizeof dummy, 15000);
}

/* ligne « terse » : champs séparés par « : », avec « \: » et « \\ » échappés (sur place) */
static int fields(char *l, char **f, int max) {
    int k = 0;
    f[k++] = l;
    char *w = l;
    for (char *r = l; *r; r++) {
        if (*r == '\\' && r[1]) { *w++ = *++r; continue; }
        if (*r == ':' && k < max) { *w++ = 0; f[k++] = w; continue; }
        *w++ = *r;
    }
    *w = 0;
    return k;
}

/* ---------------- réseaux enregistrés : fichiers de NetworkManager ---------------- */
typedef struct { char path[200], uuid[40], ssid[33]; } saved_t;

static void unesc(const char *s, char *out, int n) {           /* valeur d'un fichier clé */
    int k = 0;
    for (; *s && k < n - 1; s++) {
        if (*s == '\\' && s[1]) {
            s++;
            out[k++] = *s == 's' ? ' ' : *s == 'n' ? '\n' : *s == 't' ? '\t' : *s == 'r' ? '\r' : *s;
        } else out[k++] = *s;
    }
    out[k] = 0;
}

typedef struct { saved_t *list; int n, max; } saved_ctx;
static void saved_cb(const char *name, int type, void *ctx) {
    (void)type;
    saved_ctx *c = ctx;
    if (c->n >= c->max || !strstr(name, ".nmconnection")) return;
    saved_t *s = &c->list[c->n];
    memset(s, 0, sizeof *s);
    snprintf(s->path, sizeof s->path, NM_DIR "/%s", name);
    static char buf[8192];
    if (read_file(s->path, buf, sizeof buf) <= 0) return;
    bool wifi = false, in_wifi = false;
    for (char *l = buf; l && *l; ) {
        char *nl = strchr(l, '\n');
        if (nl) *nl = 0;
        if (l[0] == '[') in_wifi = !strncmp(l, "[wifi]", 6) || !strncmp(l, "[802-11-wireless]", 17);
        else if (!strncmp(l, "type=", 5)) wifi = !strcmp(l + 5, "wifi") || !strcmp(l + 5, "802-11-wireless");
        else if (!strncmp(l, "uuid=", 5)) strlcpy(s->uuid, l + 5, sizeof s->uuid);
        else if (in_wifi && !strncmp(l, "ssid=", 5)) unesc(l + 5, s->ssid, sizeof s->ssid);
        l = nl ? nl + 1 : NULL;
    }
    if (wifi && s->ssid[0]) c->n++;
}
static int saved_list(saved_t *list, int max) {
    saved_ctx c = { list, 0, max };
    for_each_dir(NM_DIR, saved_cb, &c);
    return c.n;
}

/* valeur pour un fichier clé (format GKeyFile) */
static int esc(char *out, int n, const char *s) {
    int k = 0;
    for (const char *p = s; *p && k < n - 3; p++) {
        if (*p == '\\') { out[k++] = '\\'; out[k++] = '\\'; }
        else if (*p == '\n') { out[k++] = '\\'; out[k++] = 'n'; }
        else if (*p == '\t') { out[k++] = '\\'; out[k++] = 't'; }
        else if (*p == '\r') { out[k++] = '\\'; out[k++] = 'r'; }
        else if (*p == ' ' && p == s) { out[k++] = '\\'; out[k++] = 's'; }
        else out[k++] = *p;
    }
    out[k] = 0;
    return k;
}

static void new_uuid(char *out, int n) {
    uint8_t r[16];
    plat_random(r, sizeof r);
    r[6] = (uint8_t)((r[6] & 0x0F) | 0x40);
    r[8] = (uint8_t)((r[8] & 0x3F) | 0x80);
    snprintf(out, (size_t)n, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15]);
}

enum { SEC_OPEN, SEC_PSK, SEC_SAE, SEC_EAP };
/* écrit (ou réécrit) le réseau enregistré ; renvoie son uuid dans uuid_out */
static bool write_conn(const char *ssid, int sec, const char *psk, const char *identity, bool hidden, char *uuid_out, char *path_out) {
    saved_t list[32];
    int n = saved_list(list, 32);
    char path[200] = "", uuid[40] = "";
    for (int i = 0; i < n; i++) if (!strcmp(list[i].ssid, ssid)) { strlcpy(path, list[i].path, sizeof path); strlcpy(uuid, list[i].uuid, sizeof uuid); break; }
    if (!uuid[0]) new_uuid(uuid, sizeof uuid);
    if (!path[0]) {
        char safe[40];
        int k = 0;
        for (const char *p = ssid; *p && k < 32; p++) safe[k++] = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ? *p : '_';
        safe[k] = 0;
        snprintf(path, sizeof path, NM_DIR "/titanos-%s-%.8s.nmconnection", safe, uuid);
    }
    static char buf[2048];
    char e1[140], e2[300], e3[300];
    esc(e1, sizeof e1, ssid);
    int k = snprintf(buf, sizeof buf,
                     "# écrit par TitanOs\n[connection]\nid=%s\nuuid=%s\ntype=wifi\nautoconnect=true\n\n"
                     "[wifi]\nmode=infrastructure\nssid=%s\n%s\n", e1, uuid, e1, hidden ? "hidden=true\n" : "");
    if (sec == SEC_PSK || sec == SEC_SAE) {
        esc(e2, sizeof e2, psk ? psk : "");
        k += snprintf(buf + k, sizeof buf - (size_t)k, "[wifi-security]\nkey-mgmt=%s\npsk=%s\n%s\n",
                      sec == SEC_SAE ? "sae" : "wpa-psk", e2, sec == SEC_SAE ? "pmf=3\n" : "");
    } else if (sec == SEC_EAP) {
        esc(e2, sizeof e2, identity ? identity : "");
        esc(e3, sizeof e3, psk ? psk : "");
        k += snprintf(buf + k, sizeof buf - (size_t)k, "[wifi-security]\nkey-mgmt=wpa-eap\n\n"
                      "[802-1x]\neap=peap;ttls;\nidentity=%s\npassword=%s\nphase2-auth=mschapv2\n\n", e2, e3);
    }
    k += snprintf(buf + k, sizeof buf - (size_t)k, "[ipv4]\nmethod=auto\n\n[ipv6]\naddr-gen-mode=default\nmethod=auto\n");
    mkdirs(NM_DIR);
    char tmp[220];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = sys_open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool ok = sys_write(fd, buf, (size_t)k) == k;
    sc1(__NR_fsync, fd);
    sys_close(fd);
    nm_wipe(buf, sizeof buf);
    nm_wipe(e2, sizeof e2);
    nm_wipe(e3, sizeof e3);
    if (!ok || sys_rename(tmp, path) != 0) { sys_unlink(tmp); return false; }
    strlcpy(uuid_out, uuid, 40);
    if (path_out) strlcpy(path_out, path, 200);
    return true;
}

/* ---------------- réseaux de l'ancien TitanOs (wpa_supplicant) ---------------- */
static void wpa_value(const char *v, char *out, int n) {
    if (*v == '"') {
        strlcpy(out, v + 1, (size_t)n);
        char *q = strrchr(out, '"');
        if (q) *q = 0;
        return;
    }
    int k = 0;                                             /* nom en hexadécimal */
    for (; v[0] && v[1] && k < n - 1; v += 2) {
        int hi = v[0] <= '9' ? v[0] - '0' : (v[0] | 32) - 'a' + 10, lo = v[1] <= '9' ? v[1] - '0' : (v[1] | 32) - 'a' + 10;
        if (hi < 0 || hi > 15 || lo < 0 || lo > 15) break;
        out[k++] = (char)(hi << 4 | lo);
    }
    out[k] = 0;
}

static void import_old(void) {
    if (path_exists("/var/lib/titanos/wifi-repris")) return;
    static char buf[32768];
    long len = read_file("/media/titanos/.titanos/wifi.conf", buf, sizeof buf);
    mkdirs("/var/lib/titanos");
    write_file("/var/lib/titanos/wifi-repris", "1\n");
    if (len <= 0) return;
    int count = 0;
    for (char *b = strstr(buf, "network={"); b; b = strstr(b, "network={")) {
        char *e = strchr(b, '}');
        if (!e) break;
        *e = 0;
        char ssid[33] = "", psk[128] = "", id[128] = "", pw[128] = "", km[32] = "";
        bool hidden = false;
        for (char *l = b + 9; l && *l; ) {
            char *nl = strchr(l, '\n');
            if (nl) *nl = 0;
            while (*l == ' ' || *l == '\t') l++;
            if (!strncmp(l, "ssid=", 5)) wpa_value(l + 5, ssid, sizeof ssid);
            else if (!strncmp(l, "psk=", 4)) { if (l[4] == '"') wpa_value(l + 4, psk, sizeof psk); else strlcpy(psk, l + 4, sizeof psk); }
            else if (!strncmp(l, "identity=", 9)) wpa_value(l + 9, id, sizeof id);
            else if (!strncmp(l, "password=", 9)) wpa_value(l + 9, pw, sizeof pw);
            else if (!strncmp(l, "key_mgmt=", 9)) strlcpy(km, l + 9, sizeof km);
            else if (!strncmp(l, "scan_ssid=1", 11)) hidden = true;
            l = nl ? nl + 1 : NULL;
        }
        b = e + 1;
        if (!ssid[0]) continue;
        int sec = strstr(km, "EAP") ? SEC_EAP : strstr(km, "SAE") ? SEC_SAE : strstr(km, "NONE") ? SEC_OPEN : psk[0] ? SEC_PSK : SEC_OPEN;
        char uuid[40];
        (void)hidden;                       /* l'ancien TitanOs cherchait tous les réseaux comme masqués */
        if (write_conn(ssid, sec, sec == SEC_EAP ? pw : psk, id, false, uuid, NULL)) count++;
        nm_wipe(psk, sizeof psk);
        nm_wipe(pw, sizeof pw);
    }
    nm_wipe(buf, sizeof buf);
    if (count) {
        nmcli(NULL, 0, "connection", "reload");
        klog("Wi-Fi : %d réseau(x) enregistré(s) repris de l'ancien TitanOs", count);
    }
}

/* ---------------- état ---------------- */
void nm_start(void) {
    static bool done;
    if (done) return;
    done = true;
    import_old();
    char out[64];
    if (nmcli(out, sizeof out, "-t", "radio", "wifi") == 0 && strstr(out, "disabled"))
        nmcli(NULL, 0, "radio", "wifi", "on");
}

static const char *step_text(const char *st) {
    if (strstr(st, "prepare")) return "préparation";
    if (strstr(st, "config")) return "réglage de la carte";
    if (strstr(st, "auth")) return "authentification (mot de passe)";
    if (strstr(st, "IP config")) return "demande d'une adresse (DHCP)";
    if (strstr(st, "IP check") || strstr(st, "connectivity")) return "vérification de la connexion";
    if (strstr(st, "secondar")) return "connexions secondaires";
    return "en cours";
}

/* message d'erreur de nmcli -> phrase en français */
static void error_text(const char *out, char *msg, int n) {
    if (strstr(out, "Secrets were required") || strstr(out, "secrets"))
        { strlcpy(msg, "Mot de passe refusé par la borne.", (size_t)n); refused = true; }
    else if (strstr(out, "No network with SSID") || strstr(out, "not found"))
        strlcpy(msg, "Réseau introuvable : il n'est plus à portée (ou son nom a changé).", (size_t)n);
    else if (strstr(out, "IP configuration could not be reserved"))
        strlcpy(msg, "Connecté à la borne, mais aucune adresse reçue (DHCP).", (size_t)n);
    else if (strstr(out, "took too long") || strstr(out, "Timeout") || strstr(out, "timed out"))
        strlcpy(msg, "La borne n'a pas répondu à temps : rapprochez-vous et réessayez.", (size_t)n);
    else if (strstr(out, "supplicant"))
        strlcpy(msg, "Authentification refusée par la borne.", (size_t)n);
    else {
        const char *e = strstr(out, "Error: ");
        char first[110];
        strlcpy(first, e ? e + 7 : out, sizeof first);
        char *nl = strchr(first, '\n');
        if (nl) *nl = 0;
        snprintf(msg, (size_t)n, "Échec : %s", first[0] ? first : "connexion impossible.");
    }
}

/* fin d'une connexion lancée en arrière-plan (appelé par le veilleur) */
bool nm_child(int pid, int status) {
    if (pid != conn_pid || pid <= 0) return false;
    conn_pid = -1;
    static char out[2048];
    if (read_file("/run/titanos-nm.txt", out, sizeof out) <= 0) out[0] = 0;
    sys_unlink("/run/titanos-nm.txt");
    if (!(status & 0x7F) && ((status >> 8) & 0xFF) == 0) {
        snprintf(nlast, sizeof nlast, "Connecté à « %s ».", conn_ssid);
        klog("Wi-Fi : connecté à « %s »", conn_ssid);
    } else {
        error_text(out, nlast, sizeof nlast);
        char *nl = strchr(out, '\n');
        if (nl) *nl = 0;
        klog("Wi-Fi : connexion à « %s » échouée : %s", conn_ssid, out);
    }
    return true;
}

static void conn_up(const char *uuid, const char *ssid) {
    if (conn_pid > 0) sys_kill(conn_pid, 15);
    strlcpy(conn_ssid, ssid, sizeof conn_ssid);
    refused = false;
    snprintf(nlast, sizeof nlast, "Connexion à « %s »…", ssid);
    int fd = sys_open("/run/titanos-nm.txt", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    int pid = sys_fork();
    if (pid == 0) {
        int nul = sys_open("/dev/null", O_RDONLY, 0);
        if (nul >= 0) sys_dup2(nul, 0);
        if (fd >= 0) { sys_dup2(fd, 1); sys_dup2(fd, 2); }
        char *argv[] = { "/usr/bin/nmcli", "-w", "45", "connection", "up", "uuid", (char *)uuid, NULL };
        sys_execve(argv[0], argv, nm_env);
        sys_exit(127);
    }
    if (fd >= 0) sys_close(fd);
    conn_pid = pid;
    klog("Wi-Fi : connexion à « %s » demandée", ssid);
}

int nm_state(char *ssid, int n, int *signal) {
    ssid[0] = 0;
    *signal = 0;
    static char out[2048];
    if (nmcli(out, sizeof out, "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device") != 0) return WIFI_NONE;
    int state = WIFI_NONE;
    char st[64] = "";
    wdev[0] = 0;
    for (char *l = out; l && *l; ) {
        char *nl = strchr(l, '\n');
        if (nl) *nl = 0;
        char *f[4];
        if (fields(l, f, 4) == 4 && !strcmp(f[1], "wifi") && !wdev[0]) {
            strlcpy(wdev, f[0], sizeof wdev);
            strlcpy(st, f[2], sizeof st);
        }
        l = nl ? nl + 1 : NULL;
    }
    if (!wdev[0]) return WIFI_NONE;
    static int last = -9;
    if (str_starts(st, "connected")) state = WIFI_CONNECTED;
    else if (str_starts(st, "connecting") || conn_pid > 0) {
        state = WIFI_CONNECTING;
        if (conn_pid > 0 || str_starts(st, "connecting")) {
            char who[60];
            snprintf(who, sizeof who, "%s", conn_ssid[0] ? conn_ssid : "la borne");
            snprintf(nlast, sizeof nlast, "Connexion à « %s » : %s…", who, step_text(st));
        }
    }
    else state = refused ? WIFI_REFUSED : WIFI_OFF;
    /* la connexion vient d'échouer sans nous (connexion automatique) : la raison */
    if (last == WIFI_CONNECTING && state == WIFI_OFF && conn_pid <= 0) {
        char r[512];
        if (nmcli(r, sizeof r, "-t", "-f", "GENERAL.REASON", "device", "show", wdev) == 0 && strstr(r, "ecrets")) {
            refused = true;
            state = WIFI_REFUSED;
            strlcpy(nlast, "Mot de passe refusé par la borne.", sizeof nlast);
        }
    }
    if (state == WIFI_CONNECTED) refused = false;
    last = state;
    for (int i = 0; i < ncache; i++)
        if (cache[i].current) { strlcpy(ssid, cache[i].ssid, (size_t)n); *signal = cache[i].signal; }
    if (state == WIFI_CONNECTING && !ssid[0]) strlcpy(ssid, conn_ssid, (size_t)n);
    return state;
}

int nm_list(wifi_net_t *list, int max) {
    static char out[16384];
    if (nmcli(out, sizeof out, "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list", "--rescan", "no") != 0) return 0;
    saved_t saved[32];
    int ns = saved_list(saved, 32);
    int n = 0;
    for (char *l = out; l && *l; ) {
        char *nl = strchr(l, '\n');
        if (nl) *nl = 0;
        char *f[4];
        if (fields(l, f, 4) == 4 && f[1][0]) {
            int sig = atoi(f[2]);
            int k = -1;
            for (int i = 0; i < n; i++) if (!strcmp(list[i].ssid, f[1])) { k = i; break; }
            if (k < 0 && n < max) {
                k = n++;
                memset(&list[k], 0, sizeof list[k]);
                strlcpy(list[k].ssid, f[1], sizeof list[k].ssid);
                const char *sec = f[3];
                list[k].secure = sec[0] && strcmp(sec, "--");
                list[k].enterprise = strstr(sec, "802.1X") != NULL;
                list[k].wpa3 = strstr(sec, "WPA3") && !strstr(sec, "WPA2") && !strstr(sec, "WPA1");
                for (int i = 0; i < ns; i++) if (!strcmp(saved[i].ssid, f[1])) list[k].known = true;
            }
            if (k >= 0) {
                if (sig > list[k].signal) list[k].signal = sig;
                if (f[0][0] == '*') list[k].current = true;
            }
        }
        l = nl ? nl + 1 : NULL;
    }
    /* le réseau en cours d'abord, puis du plus fort au plus faible */
    for (int i = 1; i < n; i++) {
        wifi_net_t t = list[i];
        int j = i - 1;
        while (j >= 0 && ((t.current && !list[j].current) || (t.current == list[j].current && t.signal > list[j].signal))) { list[j + 1] = list[j]; j--; }
        list[j + 1] = t;
    }
    ncache = MIN(n, (int)countof(cache));
    memcpy(cache, list, sizeof(wifi_net_t) * (size_t)ncache);
    return n;
}

void nm_scan(void) { nmcli(NULL, 0, "device", "wifi", "rescan"); }

void nm_connect(const char *ssid, const char *psk, const char *identity) {
    saved_t saved[32];
    int ns = saved_list(saved, 32);
    const wifi_net_t *net = NULL;
    for (int i = 0; i < ncache; i++) if (!strcmp(cache[i].ssid, ssid)) net = &cache[i];
    if (!psk && !identity) {                         /* réseau enregistré, ou ouvert */
        for (int i = 0; i < ns; i++)
            if (!strcmp(saved[i].ssid, ssid)) {
                nmcli(NULL, 0, "connection", "load", saved[i].path);
                conn_up(saved[i].uuid, ssid);
                return;
            }
    }
    int sec = identity ? SEC_EAP : !psk ? SEC_OPEN : net && net->wpa3 ? SEC_SAE : SEC_PSK;
    char uuid[40], path[200];
    if (!write_conn(ssid, sec, psk, identity, net == NULL, uuid, path)) {
        strlcpy(nlast, "Réseau non enregistré (écriture impossible).", sizeof nlast);
        return;
    }
    nmcli(NULL, 0, "connection", "load", path);
    if (sec == SEC_SAE) klog("Wi-Fi : « %s » est en WPA3 seul (SAE)", ssid);
    conn_up(uuid, ssid);
}

void nm_forget(const char *ssid) {
    saved_t saved[32];
    int ns = saved_list(saved, 32), k = 0;
    for (int i = 0; i < ns; i++) {
        if (strcmp(saved[i].ssid, ssid)) continue;
        if (nmcli(NULL, 0, "connection", "delete", "uuid", saved[i].uuid) != 0) {
            sys_unlink(saved[i].path);
            nmcli(NULL, 0, "connection", "reload");
        }
        k++;
    }
    refused = false;
    nlast[0] = 0;
    klog("Wi-Fi : réseau « %s » oublié (%d)", ssid, k);
}

const char *nm_last(void) { return nlast; }
