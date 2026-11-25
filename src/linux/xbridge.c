/* TitanOs - applications Linux graphiques (X11) dans des fenêtres TitanOs
 *
 * Chromium, LibreOffice, VS Code, Discord... ont besoin d'un serveur X11. Chaque application
 * a le sien : Xvfb, un serveur X qui dessine en mémoire (:1 pour Chromium, :2 à :6 pour les
 * autres). Son image est un fichier (/run/xvfbN/Xvfb_screen0, format XWD) que la fenêtre
 * TitanOs recopie. Les clics et les touches sont renvoyés au serveur X par l'extension
 * XTEST, avec un petit client X11 écrit ici (quelques requêtes). La forme du pointeur
 * (main sur un lien, barre de texte...) est lue par l'extension XFIXES.
 *
 * Les installations (apk pour les paquets Alpine, Flatpak pour VS Code et Discord) passent
 * par une file : une seule à la fois, dans l'ordre, dans l'espace des applications. */
#include "linux.h"
#include "../core/desk.h"

/* ================================================================== */
/* Installations : file d'attente, une à la fois                        */
/* ================================================================== */
typedef struct { char id[24]; char script[1100]; } job_req_t;
static job_req_t jq[8];
static int njq;
static int job_pid = -1, job_fd = -1, job_pc = -1, job_len, job_n, job_k;
static int job_from, job_to = 100;           /* étape en cours (ligne « @ de à message ») : partie de la barre */
static char job_id[24], job_line[512], job_msg[160], job_err[160];
/* résultat de la dernière installation de chaque application */
static struct { char id[24]; bool ok; char msg[160]; } results[16];
static int nresults;

static void job_result(const char *id, bool ok, const char *msg) {
    int k = -1;
    for (int i = 0; i < nresults; i++) if (!strcmp(results[i].id, id)) k = i;
    if (k < 0) { k = nresults < 16 ? nresults++ : 15; strlcpy(results[k].id, id, sizeof results[k].id); }
    results[k].ok = ok;
    strlcpy(results[k].msg, msg, sizeof results[k].msg);
}

static void job_next(void) {
    if (job_pid > 0 || !njq) return;
    job_req_t r = jq[0];
    memmove(jq, jq + 1, sizeof jq[0] * (size_t)(njq - 1));
    njq--;
    int p[2];
    if (sys_pipe2(p, O_CLOEXEC) < 0) { job_result(r.id, false, "Installation impossible (tube)."); return; }
    int pid = sys_fork();
    if (pid == 0) {
        sys_setsid();
        sys_dup2(p[1], 1);
        sys_dup2(p[1], 2);
        int nul = sys_open("/dev/null", O_RDONLY, 0);
        if (nul >= 0) sys_dup2(nul, 0);
        char *argv[] = { "/bin/busybox", "sh", "-c", r.script, NULL };
        static char *env[] = { "PATH=/usr/bin:/bin:/usr/sbin:/sbin", "HOME=/root", "LANG=C.UTF-8", "TERM=dumb",
                               "XDG_RUNTIME_DIR=/run/xdg", NULL };
        /* Debian : apt, sans questions ; ses étapes arrivent sur la sortie (APT::Status-Fd=1) */
        static char *denv[] = { "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", "HOME=/root", "LANG=C.UTF-8",
                                "TERM=dumb", "DEBIAN_FRONTEND=noninteractive", "APT_LISTCHANGES_FRONTEND=none", NULL };
        if (g_hosted) { argv[0] = "/bin/sh"; argv[1] = "-c"; argv[2] = r.script; argv[3] = NULL; }
        sys_execve(argv[0], argv, g_hosted ? denv : env);
        sys_exit(127);
    }
    sys_close(p[1]);
    if (pid < 0) { sys_close(p[0]); job_result(r.id, false, "Installation impossible (processus)."); return; }
    sc3(__NR_fcntl, p[0], F_SETFL, O_NONBLOCK);
    job_pid = pid;
    job_fd = p[0];
    job_pc = -1;
    job_len = job_n = job_k = 0;
    job_from = 0;
    job_to = 100;
    job_err[0] = 0;
    strlcpy(job_id, r.id, sizeof job_id);
    strlcpy(job_msg, "Préparation…", sizeof job_msg);
    klog("installation de %s : lancée", r.id);
}

bool plat_app_install(const char *id, const char *script) {
    if (!g_hosted && !path_exists("/.titan/upper")) {
        job_result(id, false, "Il faut l'espace des applications (TitanOs installé sur un disque ou une clé d'au moins 4 Go).");
        return false;
    }
    if (job_pid > 0 && !strcmp(job_id, id)) return true;
    for (int i = 0; i < njq; i++) if (!strcmp(jq[i].id, id)) return true;
    if (njq >= countof(jq) || strlen(script) >= sizeof jq[0].script) return false;
    strlcpy(jq[njq].id, id, sizeof jq[njq].id);
    strlcpy(jq[njq].script, script, sizeof jq[njq].script);
    njq++;
    for (int i = 0; i < nresults; i++) if (!strcmp(results[i].id, id)) results[i].id[0] = 0;   /* ancien résultat oublié */
    job_next();
    return true;
}

/* une ligne de sortie d'apk ou de flatpak -> pourcentage et message */
static void job_parse(char *l) {
    while (*l == ' ' || *l == '\t') l++;
    if (!*l) return;
    /* étape annoncée par le script : « @ 0 40 Téléchargement de Google Chrome… » */
    if (l[0] == '@' && l[1] == ' ') {
        char *q = l + 2;
        job_from = atoi(q);
        while (*q && *q != ' ') q++;
        while (*q == ' ') q++;
        job_to = atoi(q);
        while (*q && *q != ' ') q++;
        while (*q == ' ') q++;
        if (job_to <= job_from) job_to = 100;
        job_pc = job_from;
        if (*q) strlcpy(job_msg, q, sizeof job_msg);
        return;
    }
    /* apt (APT::Status-Fd) : « dlstatus:paquet:45.2:Téléchargement… », « pmstatus:paquet:60:Installation… » */
    bool dl = str_starts(l, "dlstatus:"), pm = str_starts(l, "pmstatus:");
    if (dl || pm) {
        char *a = strchr(l + 9, ':');
        if (!a) return;
        float pc = (float)atoi(a + 1);
        int span = job_to - job_from;
        /* téléchargement : première moitié de l'étape, installation : seconde moitié */
        int v = job_from + (int)((dl ? pc * 0.5f : 50 + pc * 0.5f) * span / 100.0f);
        job_pc = MIN(99, MAX(job_pc, v));
        char pk[48];
        int k = 0;
        for (const char *p = l + 9; *p && *p != ':' && k < 47; p++) pk[k++] = *p;
        pk[k] = 0;
        snprintf(job_msg, sizeof job_msg, dl ? "Téléchargement… %s" : "Installation… %s", pk);
        return;
    }
    if (str_starts(l, "E: ")) { strlcpy(job_err, l + 3, sizeof job_err); klog("installation de %s : %s", job_id, l); return; }
    /* apk : « (12/249) Installing libdrm (2.4.124-r0) » */
    if (l[0] == '(') {
        const char *q = l + 1;
        while (*q == ' ') q++;
        int a = atoi(q), b = 0;
        char *sl = strchr(l, '/');
        if (sl) b = atoi(sl + 1);
        if (a > 0 && b > 0) {
            job_pc = MIN(99, a * 100 / b);
            char *nm = strstr(l, "Installing ");
            if (nm) {
                char pk[48];
                strlcpy(pk, nm + 11, sizeof pk);
                char *sp = strchr(pk, ' ');
                if (sp) *sp = 0;
                snprintf(job_msg, sizeof job_msg, "Installation… %d/%d (%s)", a, b, pk);
            }
            return;
        }
    }
    /* flatpak : « Installing 2/3… », puis des pourcentages */
    char *ins = strstr(l, "Installing ");
    if (!ins) ins = strstr(l, "Updating ");
    if (ins) {
        char *d = ins + (ins[0] == 'I' ? 11 : 9);
        int a = atoi(d), b = 0;
        char *sl = strchr(d, '/');
        if (a > 0 && sl) b = atoi(sl + 1);
        if (a > 0 && b > 0) {
            job_k = a;
            job_n = b;
            job_pc = MIN(99, (a - 1) * 100 / b);
            snprintf(job_msg, sizeof job_msg, "Téléchargement et installation… élément %d sur %d", a, b);
        }
    }
    char *pcs = strrchr(l, '%');
    if (pcs && pcs > l && pcs[-1] >= '0' && pcs[-1] <= '9') {
        char *q = pcs - 1;
        while (q > l && q[-1] >= '0' && q[-1] <= '9') q--;
        if (q - l >= 2 && q[-1] == '.' && q[-2] >= '0' && q[-2] <= '9') {      /* « 45.6% » (curl) */
            q--;
            while (q > l && q[-1] >= '0' && q[-1] <= '9') q--;
        }
        int p = atoi(q);
        if (p >= 0 && p <= 100) {
            job_pc = job_n > 0 ? MIN(99, ((job_k - 1) * 100 + p) / job_n) : MIN(99, p);
            if (job_from > 0 || job_to < 100) job_pc = MIN(99, job_from + p * (job_to - job_from) / 100);
            if (job_n > 0) snprintf(job_msg, sizeof job_msg, "Téléchargement et installation… élément %d sur %d", job_k, job_n);
            else strlcpy(job_msg, "Téléchargement et installation…", sizeof job_msg);
        }
        return;
    }
    if (strstr(l, "fetch ")) strlcpy(job_msg, "Téléchargement de la liste des paquets…", sizeof job_msg);
    else if (strstr(l, "Looking for matches") || strstr(l, "Recherche")) strlcpy(job_msg, "Recherche de l'application…", sizeof job_msg);
    if (strstr(l, "ERROR") || strstr(l, "error:") || strstr(l, "Error") || strstr(l, "Erreur")) {
        strlcpy(job_err, l, sizeof job_err);
        klog("installation de %s : %s", job_id, l);
    }
}

static void job_poll(void) {
    if (job_fd < 0) { job_next(); return; }
    char buf[1024];
    long r;
    while ((r = sys_read(job_fd, buf, sizeof buf)) > 0) {
        for (long i = 0; i < r; i++) {
            char c = buf[i];
            if (c != '\n' && c != '\r') { if (job_len < (int)sizeof job_line - 1) job_line[job_len++] = c; continue; }
            job_line[job_len] = 0;
            job_len = 0;
            job_parse(job_line);
        }
    }
    if (r == 0) {                                   /* terminé */
        sys_close(job_fd);
        job_fd = -1;
        int st = 0;
        sys_wait4(job_pid, &st, 0);
        job_pid = -1;
        sys_sync();
        bool ok = st == 0;
        char m[160];
        if (ok) strlcpy(m, "Installé.", sizeof m);
        else if (job_err[0]) snprintf(m, sizeof m, "Échec : %s", job_err);
        else strlcpy(m, "L'installation a échoué : vérifiez la connexion à Internet et réessayez.", sizeof m);
        job_result(job_id, ok, m);
        klog("installation de %s : %s", job_id, ok ? "réussie" : "échec");
        job_id[0] = 0;
        job_next();
    }
}

int plat_app_state(const char *id, char *msg, int n, int *percent) {
    job_poll();
    if (percent) *percent = -1;
    if (job_pid > 0 && !strcmp(job_id, id)) {
        if (msg) strlcpy(msg, job_msg, (size_t)n);
        if (percent) *percent = job_pc;
        return INS_RUNNING;
    }
    for (int i = 0; i < njq; i++)
        if (!strcmp(jq[i].id, id)) { if (msg) strlcpy(msg, "En attente d'une autre installation…", (size_t)n); return INS_WAITING; }
    for (int i = 0; i < nresults; i++)
        if (!strcmp(results[i].id, id)) { if (msg) strlcpy(msg, results[i].msg, (size_t)n); return results[i].ok ? INS_DONE : INS_FAILED; }
    if (msg) msg[0] = 0;
    return INS_NONE;
}

/* ================================================================== */
/* Google Chrome : installation                                         */
/* ================================================================== */
/* Le vrai Google Chrome n'existe que pour les systèmes « glibc » : il est installé par
 * Flatpak (Flathub, com.google.Chrome), qui télécharge le paquet officiel de Google et
 * fournit les bibliothèques dont il a besoin. Avec lui : D-Bus (session) et PulseAudio (son).
 * L'ancien Chromium est désinstallé une fois Chrome en place. */
#define CHROME_ID "com.google.Chrome"
static const char CHROME_SCRIPT[] =
    "mkdir -p /var/tmp && apk add --no-progress xvfb setxkbmap flatpak dbus pulseaudio pulseaudio-alsa font-dejavu && "
    "flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo && "
    "flatpak install -y --noninteractive --system flathub " CHROME_ID " && "
    "{ apk del --no-progress chromium >/dev/null 2>&1; rm -rf /root/.config/chromium /root/.cache/chromium; true; }";

/* Debian : le paquet officiel de Google (.deb), qui ajoute aussi son dépôt pour les mises à jour */
#define APT_GET "apt-get -y -q -o APT::Status-Fd=1 -o Dpkg::Use-Pty=0 -o DPkg::Lock::Timeout=300 "
static const char CHROME_DEB_SCRIPT[] =
    "set -e; echo '@ 0 5 Mise à jour de la liste des paquets…'; " APT_GET "update >/dev/null 2>&1 || true; "
    "echo '@ 5 45 Téléchargement de Google Chrome…'; "
    "curl -fL --retry 3 -# -o /var/tmp/google-chrome.deb https://dl.google.com/linux/direct/google-chrome-stable_current_amd64.deb 2>&1 | tr '\\r' '\\n'; "
    "echo '@ 45 100 Installation de Google Chrome…'; " APT_GET "install /var/tmp/google-chrome.deb; rm -f /var/tmp/google-chrome.deb";
#define CHROME_DEB "/usr/bin/google-chrome-stable"

static const char *chromium_path(void) {
    if (path_exists("/usr/bin/chromium-browser")) return "/usr/bin/chromium-browser";
    if (path_exists("/usr/bin/chromium")) return "/usr/bin/chromium";
    return NULL;
}
static bool chrome_installed(void) {
    if (g_hosted) return path_exists(CHROME_DEB);
    return path_exists("/var/lib/flatpak/app/" CHROME_ID "/current") && path_exists("/usr/bin/flatpak");
}
static bool installed(void) { return (chrome_installed() || (!g_hosted && chromium_path())) && path_exists("/usr/bin/Xvfb"); }
bool plat_browser_is_chrome(void) { return chrome_installed() && path_exists("/usr/bin/Xvfb"); }

bool plat_browser_install(void) {
    if (plat_browser_is_chrome()) return false;
    return plat_app_install("chrome", g_hosted ? CHROME_DEB_SCRIPT : CHROME_SCRIPT);
}

int plat_browser_state(char *msg, int n, int *percent) {
    char m[160];
    int pc = -1, st = plat_app_state("chrome", m, sizeof m, &pc);
    if (percent) *percent = pc;
    int r;
    if (st == INS_RUNNING || st == INS_WAITING) r = BR_INSTALLING;
    else if (installed()) r = BR_READY;
    else if (st == INS_FAILED) r = BR_ERROR;
    else r = BR_NONE;
    if (msg) {
        if (r == BR_NONE) strlcpy(msg, "Google Chrome n'est pas encore installé.", (size_t)n);
        else if (r == BR_INSTALLING && st == INS_RUNNING) snprintf(msg, (size_t)n, "Google Chrome : %s", m);
        else strlcpy(msg, m, (size_t)n);
    }
    return r;
}

/* ================================================================== */
/* Serveurs X et applications                                           */
/* ================================================================== */
#define XN 6
typedef struct {
    bool used, started;
    int disp, xvfb_pid, app_pid, xfd, xtest_op, xfixes_op;
    int scr_w, scr_h, win_w, win_h;
    uint32_t xroot, cur_serial;
    uint32_t a_protocols, a_delete;
    uint32_t known[64];              /* fenêtres déjà placées */
    uint8_t centered[64];            /* 1 : dialogue ou petite fenêtre, centrée (pas agrandie) */
    int nknown;
    const uint8_t *fb;
    size_t fb_size;
    int fb_off, fb_pitch;
    char cmd[1200];                  /* commande de l'application (vide : le navigateur) */
    bool user;                       /* lancée sous le compte titan (pas root) */
} xs_t;
static xs_t XS[XN];
static char start_url[700];          /* adresse à ouvrir au lancement du navigateur */

/* Les applications tournent sous le compte « titan » (uid 1000), comme le Terminal : elles
 * n'ont pas les droits d'administrateur. Seul le serveur X reste à root. */
#define UID_USER 1000
static int spawn_as(char *const *argv, char *const *env, bool user) {
    int pid = sys_fork();
    if (pid == 0) {
        sys_setsid();
        int nul = sys_open("/dev/null", O_RDWR, 0);
        if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 1); sys_dup2(nul, 2); }
        if (user) {
            uint32_t groups[32];
            int ng = plat_user_groups(groups, 32);
            sc2(__NR_setgroups, ng, (long)groups);
            if (sc3(__NR_setresgid, UID_USER, UID_USER, UID_USER) != 0 || sc3(__NR_setresuid, UID_USER, UID_USER, UID_USER) != 0) sys_exit(126);
            sys_chdir("/home/titan");
        }
        sys_execve(argv[0], argv, env);
        sys_exit(127);
    }
    return pid;
}
static int spawn_env(char *const *argv, char *const *env) { return spawn_as(argv, env, false); }

static bool user_ok(void) { return path_exists("/home/titan"); }

/* Flatpak sous un compte ordinaire : il faut les espaces de noms utilisateur du noyau */
static bool userns_ok(void) {
    char v[32];
    if (read_file("/proc/sys/user/max_user_namespaces", v, sizeof v) > 0 && atoi(v) <= 0) return false;
    if (read_file("/proc/sys/kernel/unprivileged_userns_clone", v, sizeof v) > 0 && v[0] == '0') return false;
    return true;
}

/* environnement d'une application sur l'écran X n° disp */
static char **xenv_for(int disp, bool user) {
    static char dbuf[16];
    snprintf(dbuf, sizeof dbuf, "DISPLAY=:%d", disp);
    static char *env[20];
    int k = 0;
    env[k++] = dbuf;
    env[k++] = "LANG=fr_FR.UTF-8";
    env[k++] = "LANGUAGE=fr";
    env[k++] = g_hosted ? "PATH=/usr/local/bin:/usr/bin:/bin:/usr/local/games:/usr/games" : "PATH=/usr/bin:/bin:/usr/sbin:/sbin";
    env[k++] = "XDG_DATA_DIRS=/var/lib/flatpak/exports/share:/usr/local/share:/usr/share";
    env[k++] = "NO_AT_BRIDGE=1";
    env[k++] = "GTK_A11Y=none";
    if (user) {
        env[k++] = "HOME=/home/titan";
        env[k++] = "USER=titan";
        env[k++] = "LOGNAME=titan";
        env[k++] = "XDG_RUNTIME_DIR=/run/user/1000";
        if (path_exists("/run/user/1000/bus")) env[k++] = "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus";
    } else {
        env[k++] = "HOME=/root";
        env[k++] = "XDG_CONFIG_HOME=/root/.config";
        env[k++] = "XDG_RUNTIME_DIR=/run/xdg";
    }
    env[k] = NULL;
    return env;
}
static char **xenv(int disp) { return xenv_for(disp, false); }

/* ---- services de la session de titan : dossier d'exécution, D-Bus, son (PulseAudio) ---- */
static int bus_pid = -1, pulse_pid = -1;
static bool alive_pid(int pid) {
    if (pid <= 0) return false;
    char p[32];
    snprintf(p, sizeof p, "/proc/%d", pid);
    return path_exists(p);
}
static void snd_cb(const char *name, int type, void *ctx) {
    (void)type; (void)ctx;
    char p[64];
    snprintf(p, sizeof p, "/dev/snd/%s", name);
    sc3(__NR_chown, (long)p, 0, 18 /* audio */);
    sc2(__NR_chmod, (long)p, 0660);
}
static void user_services(void) {
    if (g_hosted) {
        /* Debian : la session de titan (D-Bus, PipeWire pour le son) est lancée par systemd
         * au démarrage (« linger ») ; on la relance seulement si elle manque */
        if (!path_exists("/run/user/1000/bus")) {
            int pid = sys_fork();
            if (pid == 0) {
                char *argv[] = { "/usr/bin/systemctl", "start", "user@1000.service", NULL };
                sys_execve(argv[0], argv, g_envp);
                sys_exit(127);
            }
            int st;
            if (pid > 0) sys_wait4(pid, &st, 0);
            for (int i = 0; i < 40 && !path_exists("/run/user/1000/bus"); i++) sleep_ms(50);
            klog("session de titan : %s", path_exists("/run/user/1000/bus") ? "prête" : "D-Bus absent");
        }
        return;
    }
    mkdirs("/run/user/1000");
    sc3(__NR_chown, (long)"/run/user", 0, 0);
    sc3(__NR_chown, (long)"/run/user/1000", UID_USER, UID_USER);
    sc2(__NR_chmod, (long)"/run/user/1000", 0700);
    for_each_dir("/dev/snd", snd_cb, NULL);                   /* la carte son, pour le groupe audio */
    char **env = xenv_for(0, true);
    if (!alive_pid(bus_pid) && path_exists("/usr/bin/dbus-daemon")) {
        sys_unlink("/run/user/1000/bus");
        char *argv[] = { "/usr/bin/dbus-daemon", "--session", "--address=unix:path=/run/user/1000/bus", "--nofork", "--nopidfile", NULL };
        bus_pid = spawn_as(argv, env, true);
        for (int i = 0; i < 40 && !path_exists("/run/user/1000/bus"); i++) sleep_ms(25);
        klog("session de titan : D-Bus %s", path_exists("/run/user/1000/bus") ? "prêt" : "absent");
        env = xenv_for(0, true);
    }
    if (!alive_pid(pulse_pid) && path_exists("/usr/bin/pulseaudio")) {
        /* sortie : « default » d'ALSA (la carte du haut-parleur, avec mélangeur logiciel) ;
         * libérée après 3 s de silence, pour les sons de TitanOs */
        static const char pa[] =
            "load-module module-alsa-sink device=default sink_name=titanos tsched=0\n"
            "load-module module-native-protocol-unix auth-anonymous=1\n"
            "load-module module-suspend-on-idle timeout=3\n"
            "load-module module-always-sink\n"
            "set-default-sink titanos\n";
        write_file("/run/user/1000/titanos.pa", pa);
        sc3(__NR_chown, (long)"/run/user/1000/titanos.pa", UID_USER, UID_USER);
        char *argv[] = { "/usr/bin/pulseaudio", "-n", "-F", "/run/user/1000/titanos.pa", "--daemonize=no",
                         "--exit-idle-time=-1", "--disallow-exit", NULL };
        pulse_pid = spawn_as(argv, env, true);
        klog("session de titan : son (PulseAudio) lancé (pid %d)", pulse_pid);
    }
}

/* dossiers de l'utilisateur (téléchargements, documents...) : ceux de TitanOs, visibles dans
 * Fichiers, au lieu de dossiers cachés */
static void user_dirs(void) {
    const char *h = plat_home();
    if (!h || !h[0]) return;
    char buf[1200];
    snprintf(buf, sizeof buf,
             "XDG_DESKTOP_DIR=\"%s\"\nXDG_DOWNLOAD_DIR=\"%s/Téléchargements\"\nXDG_DOCUMENTS_DIR=\"%s/Documents\"\n"
             "XDG_PICTURES_DIR=\"%s/Images\"\nXDG_MUSIC_DIR=\"%s/Musique\"\nXDG_VIDEOS_DIR=\"%s/Vidéos\"\n",
             h, h, h, h, h, h);
    char old[1200] = "";
    mkdirs("/root/.config");
    if (read_file("/root/.config/user-dirs.dirs", old, sizeof old) <= 0 || strcmp(old, buf)) write_file("/root/.config/user-dirs.dirs", buf);
    if (user_ok()) {
        mkdirs("/home/titan/.config");
        sc3(__NR_chown, (long)"/home/titan/.config", UID_USER, UID_USER);
        old[0] = 0;
        if (read_file("/home/titan/.config/user-dirs.dirs", old, sizeof old) <= 0 || strcmp(old, buf)) {
            write_file("/home/titan/.config/user-dirs.dirs", buf);
            sc3(__NR_chown, (long)"/home/titan/.config/user-dirs.dirs", UID_USER, UID_USER);
        }
        char t[256];
        snprintf(t, sizeof t, "%s/Téléchargements", h);
        struct stat st;
        if (sc4(__NR_newfstatat, AT_FDCWD, (long)"/home/titan/Downloads", (long)&st, 0x100) != 0) sys_symlink(t, "/home/titan/Downloads");
    }
    static const char *const sub[] = { "Téléchargements", "Documents", "Images", "Musique", "Vidéos" };
    for (int i = 0; i < countof(sub); i++) {
        char p[256];
        snprintf(p, sizeof p, "%s/%s", h, sub[i]);
        mkdirs(p);
    }
}

static int xs_start(int i, int w, int h, const char *cmd, bool user) {
    xs_t *x = &XS[i];
    if (x->used) return i;
    if (!path_exists("/usr/bin/Xvfb")) return -1;
    memset(x, 0, sizeof *x);
    x->used = true;
    x->user = user && user_ok();
    if (x->user) user_services();
    x->disp = i + 1;
    x->xfd = x->xvfb_pid = x->app_pid = -1;
    x->scr_w = x->win_w = w;
    x->scr_h = x->win_h = h;
    strlcpy(x->cmd, cmd ? cmd : "", sizeof x->cmd);
    char fbdir[32], sock[40], scr[48];
    snprintf(fbdir, sizeof fbdir, "/run/xvfb%d", x->disp);
    snprintf(sock, sizeof sock, "/tmp/.X11-unix/X%d", x->disp);
    snprintf(scr, sizeof scr, "%s/Xvfb_screen0", fbdir);
    mkdirs(fbdir);
    mkdirs("/tmp/.X11-unix");
    mkdirs("/run/xdg");
    sc2(__NR_chmod, (long)"/tmp/.X11-unix", 01777);
    sc2(__NR_chmod, (long)"/run/xdg", 0700);
    sys_unlink(sock);
    sys_unlink(scr);
    user_dirs();
    audio_alsa_conf();
    char geo[32], dn[8];
    snprintf(geo, sizeof geo, "%dx%dx24", w, h);
    snprintf(dn, sizeof dn, ":%d", x->disp);
    /* -nocursor : le pointeur est dessiné par TitanOs, à la forme demandée par l'application */
    char *argv[] = { "/usr/bin/Xvfb", dn, "-screen", "0", geo, "-fbdir", fbdir, "-nolisten", "tcp", "-ac", "-dpi", "96",
                     "-br", "-nocursor", NULL };
    x->xvfb_pid = spawn_env(argv, xenv(x->disp));
    x->started = x->xvfb_pid > 0;
    if (!x->started) { x->used = false; return -1; }
    klog("X :%d : serveur %dx%d lancé%s%s%s", x->disp, w, h, cmd ? " pour " : " pour le navigateur", cmd ? cmd : "",
         x->user ? " (compte titan)" : " (root)");
    return i;
}

/* ---- petit client X11 ---- */
static bool x_query_ext(int fd, const char *name, int *op) {
    uint8_t q[20] = { 98, 0, 0, 0 };
    int n = (int)strlen(name), len = (8 + n + 3) / 4;
    q[2] = (uint8_t)len;
    q[4] = (uint8_t)n;
    memcpy(q + 8, name, (size_t)n);
    uint8_t rep[32];
    if (sys_write(fd, q, (size_t)len * 4) != len * 4) return false;
    long got = 0;
    for (int t = 0; got < 32 && t < 40; t++) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (sys_poll(&p, 1, 50) <= 0) continue;
        long r = sys_read(fd, rep + got, (size_t)(32 - got));
        if (r <= 0) return false;
        got += r;
    }
    if (got < 32 || rep[0] != 1 || !rep[8]) return false;
    *op = rep[9];
    return true;
}

static bool x_connect(xs_t *x) {
    int fd = sys_socket(1 /* AF_UNIX */, 1 /* SOCK_STREAM */ | O_CLOEXEC, 0);
    if (fd < 0) return false;
    struct { uint16_t family; char path[108]; } a = { 1, { 0 } };
    snprintf(a.path, sizeof a.path, "/tmp/.X11-unix/X%d", x->disp);
    if (sc3(__NR_connect, fd, (long)&a, sizeof a) < 0) { sys_close(fd); return false; }
    uint8_t setup[12] = { 'l', 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (sys_write(fd, setup, 12) != 12) { sys_close(fd); return false; }
    uint8_t hdr[8];
    if (sys_read(fd, hdr, 8) != 8 || hdr[0] != 1) { sys_close(fd); return false; }
    size_t more = (size_t)(hdr[6] | hdr[7] << 8) * 4, got = 0;
    static uint8_t data[16384];
    while (got < more) {
        long r = sys_read(fd, data + MIN(got, sizeof data - 1), MIN(more - got, sizeof data - MIN(got, sizeof data - 1)));
        if (r <= 0) { sys_close(fd); return false; }
        got += (size_t)r;
    }
    /* fenêtre racine du premier écran : après l'en-tête (32 octets), le nom du fabricant et
     * les formats de pixels */
    if (more >= 32) {
        size_t vlen = data[16] | data[17] << 8, nfmt = data[21];
        size_t off = 32 + ((vlen + 3) & ~(size_t)3) + 8 * nfmt;
        if (off + 4 <= more && off + 4 <= sizeof data) x->xroot = data[off] | data[off + 1] << 8 | (uint32_t)data[off + 2] << 16 | (uint32_t)data[off + 3] << 24;
    }
    if (!x_query_ext(fd, "XTEST", &x->xtest_op)) { sys_close(fd); return false; }
    x->xfixes_op = -1;
    int op;
    if (x_query_ext(fd, "XFIXES", &op)) {
        /* QueryVersion 4.0 : obligatoire avant les autres requêtes XFIXES */
        uint8_t qv[12] = { (uint8_t)op, 0, 3, 0, 4, 0, 0, 0, 0, 0, 0, 0 }, rep[32];
        if (sys_write(fd, qv, 12) == 12) {
            long g = 0;
            for (int t = 0; g < 32 && t < 40; t++) {
                struct pollfd p = { fd, POLLIN, 0 };
                if (sys_poll(&p, 1, 50) <= 0) continue;
                long r = sys_read(fd, rep + g, (size_t)(32 - g));
                if (r <= 0) break;
                g += r;
            }
            if (g == 32 && rep[0] == 1) x->xfixes_op = op;
        }
    }
    sc3(__NR_fcntl, fd, F_SETFL, O_NONBLOCK);
    x->xfd = fd;
    return true;
}

static void x_fake(xs_t *x, int type, int detail, int px, int py) {
    if (x->xfd < 0) return;
    uint8_t junk[512];
    while (sys_read(x->xfd, junk, sizeof junk) > 0) {}         /* erreurs ou événements : ignorés */
    uint8_t r[36] = { 0 };
    r[0] = (uint8_t)x->xtest_op; r[1] = 2; r[2] = 9; r[3] = 0;
    r[4] = (uint8_t)type; r[5] = (uint8_t)detail;
    r[24] = (uint8_t)px; r[25] = (uint8_t)(px >> 8); r[26] = (uint8_t)py; r[27] = (uint8_t)(py >> 8);
    sys_write(x->xfd, r, 36);
}

/* requête avec réponse : on attend la réponse (au plus ms), en sautant les événements */
static int x_call(xs_t *x, const uint8_t *req, int len, uint8_t *rep, int cap, int ms) {
    if (x->xfd < 0) return -1;
    uint8_t junk[512];
    while (sys_read(x->xfd, junk, sizeof junk) > 0) {}
    if (sys_write(x->xfd, req, (size_t)len) != len) return -1;
    int got = 0, need = 32;
    uint64_t end = plat_ms() + (uint64_t)ms;
    while (got < need && plat_ms() < end) {
        struct pollfd p = { x->xfd, POLLIN, 0 };
        if (sys_poll(&p, 1, 20) <= 0) continue;
        long r = sys_read(x->xfd, rep + got, (size_t)(MIN(need, cap) - got));
        if (r <= 0) continue;
        got += (int)r;
        if (got >= 32) {
            if (rep[0] == 0) return -1;                                   /* erreur */
            if (rep[0] != 1) { memmove(rep, rep + 32, (size_t)(got - 32)); got -= 32; continue; }   /* événement */
            need = 32 + (int)((uint32_t)rep[4] | (uint32_t)rep[5] << 8 | (uint32_t)rep[6] << 16) * 4;
            if (need > cap) need = cap;
        }
    }
    return got >= need ? got : -1;
}

/* Petit gestionnaire de fenêtres : il n'y en a pas d'autre sur ce serveur X. Les fenêtres
 * principales prennent la taille de la fenêtre TitanOs ; les dialogues (WM_TRANSIENT_FOR)
 * sont centrés ; les menus et les bulles (override-redirect) ne sont pas touchés. */
static void x_configure(xs_t *x, uint32_t win, unsigned mask, const uint32_t *vals, int nv) {
    uint8_t cw[12 + 16] = { 12, 0, (uint8_t)(3 + nv), 0 };               /* ConfigureWindow */
    memcpy(cw + 4, &win, 4);
    cw[8] = (uint8_t)mask;
    memcpy(cw + 12, vals, (size_t)nv * 4);
    sys_write(x->xfd, cw, (size_t)(12 + nv * 4));
}

static void xs_manage(xs_t *x, bool all) {
    if (x->xfd < 0 || !x->xroot) return;
    uint8_t q[8] = { 15, 0, 2, 0 };
    memcpy(q + 4, &x->xroot, 4);
    static uint8_t rep[8192];
    int n = x_call(x, q, 8, rep, sizeof rep, 300);                       /* QueryTree */
    if (n < 32) return;
    int nch = rep[16] | rep[17] << 8;
    uint32_t seen[64];
    int nseen = 0;
    for (int i = 0; i < nch && 32 + i * 4 + 4 <= n; i++) {
        uint32_t win;
        memcpy(&win, rep + 32 + i * 4, 4);
        uint8_t ga[8] = { 3, 0, 2, 0 }, gar[44];                          /* GetWindowAttributes */
        memcpy(ga + 4, &win, 4);
        if (x_call(x, ga, 8, gar, sizeof gar, 150) < 44) continue;
        if (gar[27] || gar[26] != 2) continue;                            /* override-redirect, ou pas affichée */
        if (nseen < 64) seen[nseen++] = win;
        int ki = -1;
        for (int k = 0; k < x->nknown; k++) if (x->known[k] == win) ki = k;
        bool known = ki >= 0;
        if (known && (!all || x->centered[ki])) continue;
        uint8_t gp[24] = { 20, 0, 6, 0 };                                 /* GetProperty WM_TRANSIENT_FOR */
        memcpy(gp + 4, &win, 4);
        uint32_t prop = 68, type = 33, off = 0, len = 1;
        memcpy(gp + 8, &prop, 4); memcpy(gp + 12, &type, 4); memcpy(gp + 16, &off, 4); memcpy(gp + 20, &len, 4);
        uint8_t gpr[40];
        int pn = x_call(x, gp, 24, gpr, sizeof gpr, 150);
        bool center = pn >= 32 && (gpr[16] | gpr[17] << 8) > 0;
        int ww = 0, wh = 0;
        if (!known) {
            uint8_t g[8] = { 14, 0, 2, 0 }, gr[32];
            memcpy(g + 4, &win, 4);
            if (x_call(x, g, 8, gr, sizeof gr, 150) >= 32) { ww = gr[16] | gr[17] << 8; wh = gr[18] | gr[19] << 8; }
            if (ww < 400 || wh < 300) center = true;                      /* écran d'accueil, alerte : centrés */
        }
        if (center) {
            if (!known) {                                                 /* dialogue : centré */
                uint32_t v[2] = { (uint32_t)MAX(0, (x->win_w - ww) / 2), (uint32_t)MAX(0, (x->win_h - wh) / 2) };
                x_configure(x, win, 0x03, v, 2);
            }
        } else {
            uint32_t v[4] = { 0, 0, (uint32_t)x->win_w, (uint32_t)x->win_h };   /* fenêtre principale : toute la place */
            x_configure(x, win, 0x0F, v, 4);
        }
        if (!known && x->nknown < 64) { x->centered[x->nknown] = center; x->known[x->nknown++] = win; }
    }
    /* fenêtres disparues : oubliées */
    int k = 0;
    for (int i = 0; i < x->nknown; i++) {
        bool still = false;
        for (int j = 0; j < nseen; j++) if (seen[j] == x->known[i]) still = true;
        if (still) { x->centered[k] = x->centered[i]; x->known[k++] = x->known[i]; }
    }
    x->nknown = k;
}

/* fenêtres principales (pour la fermeture polie) */
static int x_top_windows(xs_t *x, uint32_t *out, int max) {
    xs_manage(x, false);
    int k = 0;
    for (int i = 0; i < x->nknown && k < max; i++) if (!x->centered[i]) out[k++] = x->known[i];
    return k;
}

static void xs_resize(xs_t *x, int w, int h) {
    x->win_w = CLAMP(w, 200, x->scr_w);
    x->win_h = CLAMP(h, 150, x->scr_h);
    xs_manage(x, true);
}

static uint32_t x_atom(xs_t *x, const char *name) {
    uint8_t q[40] = { 16, 0, 0, 0 };
    int n = (int)strlen(name), len = (8 + n + 3) / 4;
    q[2] = (uint8_t)len;
    q[4] = (uint8_t)n;
    memcpy(q + 8, name, (size_t)n);
    uint8_t rep[32];
    if (x_call(x, q, len * 4, rep, sizeof rep, 300) < 32) return 0;
    return rep[8] | rep[9] << 8 | (uint32_t)rep[10] << 16 | (uint32_t)rep[11] << 24;
}

/* fermeture polie : WM_DELETE_WINDOW, comme le bouton × d'un bureau Linux. L'application
 * peut alors demander d'enregistrer les documents. */
static bool xs_close_request(xs_t *x) {
    if (x->xfd < 0) return false;
    if (!x->a_delete) { x->a_protocols = x_atom(x, "WM_PROTOCOLS"); x->a_delete = x_atom(x, "WM_DELETE_WINDOW"); }
    if (!x->a_protocols || !x->a_delete) return false;
    uint32_t wins[16];
    int n = x_top_windows(x, wins, 16);
    for (int i = 0; i < n; i++) {
        uint8_t se[44] = { 25, 0, 11, 0 };                                /* SendEvent */
        memcpy(se + 4, &wins[i], 4);                                      /* destination ; masque 0 : au client */
        uint8_t *ev = se + 12;
        ev[0] = 33;                                                       /* ClientMessage */
        ev[1] = 32;                                                       /* format */
        memcpy(ev + 4, &wins[i], 4);
        memcpy(ev + 8, &x->a_protocols, 4);
        memcpy(ev + 12, &x->a_delete, 4);                                 /* data[0] ; data[1] = CurrentTime (0) */
        sys_write(x->xfd, se, 44);
    }
    return n > 0;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void map_fb(xs_t *x) {
    if (x->fb) return;
    char p[48];
    snprintf(p, sizeof p, "/run/xvfb%d/Xvfb_screen0", x->disp);
    int fd = sys_open(p, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return;
    struct stat st;
    if (sc2(__NR_fstat, fd, (long)&st) < 0 || st.st_size < 200) { sys_close(fd); return; }
    void *m = sys_mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    sys_close(fd);
    if ((long)m < 0 && (long)m > -4096) return;
    const uint8_t *h = m;
    uint32_t hsize = be32(h), bpp = be32(h + 44), pitch = be32(h + 48), ncolors = be32(h + 76), w = be32(h + 16), hh = be32(h + 20);
    if (bpp != 32 || w < (uint32_t)x->scr_w || hh < (uint32_t)x->scr_h || hsize + ncolors * 12 + (size_t)pitch * hh > (size_t)st.st_size) {
        sys_munmap(m, (size_t)st.st_size);
        return;
    }
    x->fb = h;
    x->fb_size = (size_t)st.st_size;
    x->fb_off = (int)(hsize + ncolors * 12);
    x->fb_pitch = (int)pitch;
}

/* l'application est lancée dès que son serveur X répond */
static void xs_launch(xs_t *x) {
    char *kb[] = { "/usr/bin/setxkbmap", g_set.layout ? "us" : "fr", NULL };
    spawn_env(kb, xenv(x->disp));
    if (x->cmd[0]) {
        char *argv[] = { "/bin/busybox", "sh", "-c", x->cmd, NULL };
        if (g_hosted) { argv[0] = "/bin/sh"; argv[1] = "-c"; argv[2] = x->cmd; argv[3] = NULL; }
        x->app_pid = spawn_as(argv, xenv_for(x->disp, x->user), x->user);
        klog("X :%d : application lancée (pid %d)", x->disp, x->app_pid);
        return;
    }
    char size[48];
    snprintf(size, sizeof size, "--window-size=%d,%d", x->win_w, x->win_h);
    const char *url = start_url[0] ? start_url : "https://www.google.fr";
    if (g_hosted && chrome_installed()) {
        /* Debian : Google Chrome tel quel, avec son bac à sable, sous le compte titan */
        char *argv[] = { CHROME_DEB, "--no-first-run", "--no-default-browser-check", "--password-store=basic",
                         "--disable-gpu", "--window-position=0,0", size, "--start-maximized", "--lang=fr",
                         x->user ? (char *)url : "--no-sandbox", x->user ? NULL : (char *)url, NULL };
        x->app_pid = spawn_as(argv, xenv_for(x->disp, x->user), x->user);
        klog("Google Chrome : lancé (pid %d)", x->app_pid);
    } else if (chrome_installed()) {
        /* Google Chrome : accès aux clés et au disque (/media) pour ouvrir et enregistrer */
        char *argv[] = { "/usr/bin/flatpak", "run", "--filesystem=/media", "--filesystem=xdg-download", CHROME_ID,
                         "--no-sandbox", "--test-type", "--disable-gpu", "--no-first-run", "--no-default-browser-check",
                         "--password-store=basic", "--window-position=0,0", size, "--start-maximized", "--lang=fr",
                         (char *)url, NULL };
        x->app_pid = spawn_as(argv, xenv_for(x->disp, x->user), x->user);
        klog("Google Chrome : lancé (pid %d)", x->app_pid);
    } else {
        char *argv[] = { (char *)chromium_path(), "--no-sandbox", "--test-type", "--disable-gpu", "--no-first-run",
                         "--no-default-browser-check", "--password-store=basic", "--disable-dev-shm-usage",
                         "--window-position=0,0", size, "--start-maximized", "--disk-cache-size=268435456", "--lang=fr",
                         x->user ? "--user-data-dir=/home/titan/.config/chromium" : "--user-data-dir=/root/.config/chromium",
                         (char *)url, NULL };
        x->app_pid = spawn_as(argv, xenv_for(x->disp, x->user), x->user);
        klog("Chromium : lancé (pid %d), en attendant Google Chrome", x->app_pid);
    }
    start_url[0] = 0;                       /* adresse transmise : pas rouverte au prochain lancement */
}

static bool xs_frame(xs_t *x, uint32_t *dst, int stride, int w, int h) {
    if (!x->used || !x->started) return false;
    if (x->xfd < 0) {
        char sock[40];
        snprintf(sock, sizeof sock, "/tmp/.X11-unix/X%d", x->disp);
        if (!path_exists(sock) || !x_connect(x)) return false;
        xs_launch(x);
    }
    map_fb(x);
    if (!x->fb) return false;
    w = MIN(w, x->scr_w);
    h = MIN(h, x->scr_h);
    bool changed = false;
    for (int y = 0; y < h; y++) {
        const uint32_t *s = (const uint32_t *)(x->fb + x->fb_off + (size_t)y * x->fb_pitch);
        uint32_t *d = dst + (size_t)y * stride;
        for (int i = 0; i < w; i++) {
            uint32_t v = s[i] | 0xFF000000u;
            if (d[i] != v) { d[i] = v; changed = true; }
        }
    }
    return changed;
}

static bool xs_running(xs_t *x) {
    if (!x->used) return false;
    if (x->app_pid > 0) {
        char p[32];
        snprintf(p, sizeof p, "/proc/%d", x->app_pid);
        if (!path_exists(p)) return false;
    }
    return x->started;
}

static void xs_stop(xs_t *x) {
    if (!x->used) return;
    if (x->app_pid > 0) { sys_kill(-x->app_pid, 15); sys_kill(x->app_pid, 15); }   /* tout le groupe de processus */
    if (x->xvfb_pid > 0) sys_kill(x->xvfb_pid, 15);
    if (x->xfd >= 0) sys_close(x->xfd);
    if (x->fb) sys_munmap((void *)x->fb, x->fb_size);
    klog("X :%d : arrêté", x->disp);
    memset(x, 0, sizeof *x);
}

/* forme du pointeur dans l'application (XFIXES GetCursorImage) : true si elle a changé */
static bool xs_cursor(xs_t *x, canvas_t *img, int *hx, int *hy) {
    if (x->xfd < 0 || x->xfixes_op < 0) return false;
    uint8_t q[4] = { (uint8_t)x->xfixes_op, 4, 1, 0 };
    static uint8_t rep[32 + 64 * 64 * 4];
    int n = x_call(x, q, 4, rep, sizeof rep, 60);
    if (n < 32) return false;
    int w = rep[12] | rep[13] << 8, h = rep[14] | rep[15] << 8;
    uint32_t serial = rep[20] | rep[21] << 8 | (uint32_t)rep[22] << 16 | (uint32_t)rep[23] << 24;
    if (serial == x->cur_serial || w <= 0 || h <= 0 || w > 64 || h > 64 || n < 32 + w * h * 4) return false;
    x->cur_serial = serial;
    canvas_free(img);
    *img = canvas_new(w, h);
    if (!img->px) return false;
    for (int j = 0; j < h; j++) memcpy(img->px + (size_t)j * img->stride, rep + 32 + (size_t)j * w * 4, (size_t)w * 4);
    *hx = rep[16] | rep[17] << 8;
    *hy = rep[18] | rep[19] << 8;
    return true;
}

/* ---- interface générique : poignée = numéro du serveur X ---- */
int plat_xapp_start(const char *cmd, int w, int h) {
    bool user = !strstr(cmd, "flatpak") || userns_ok();
    for (int i = 1; i < XN; i++) if (!XS[i].used) return xs_start(i, w, h, cmd, user);
    return -1;
}
bool plat_xapp_frame(int i, uint32_t *dst, int stride, int w, int h) { return i >= 0 && i < XN && xs_frame(&XS[i], dst, stride, w, h); }
bool plat_xapp_running(int i) { return i >= 0 && i < XN && xs_running(&XS[i]); }
void plat_xapp_input(int i, int type, int detail, int px, int py) {
    if (i < 0 || i >= XN || !XS[i].used) return;
    x_fake(&XS[i], type, detail, px < 0 ? px : CLAMP(px, 0, XS[i].scr_w - 1), py < 0 ? py : CLAMP(py, 0, XS[i].scr_h - 1));
}
void plat_xapp_resize(int i, int w, int h) { if (i >= 0 && i < XN && XS[i].used) xs_resize(&XS[i], w, h); }
void plat_xapp_manage(int i) { if (i >= 0 && i < XN && XS[i].used) xs_manage(&XS[i], false); }
bool plat_xapp_close(int i) { return i >= 0 && i < XN && xs_close_request(&XS[i]); }
void plat_xapp_stop(int i) { if (i >= 0 && i < XN) xs_stop(&XS[i]); }
bool plat_xapp_cursor(int i, canvas_t *img, int *hx, int *hy) { return i >= 0 && i < XN && xs_cursor(&XS[i], img, hx, hy); }

/* ---- le navigateur : serveur X n° 0 (écran :1) ---- */
bool plat_browser_start(int w, int h) {
    if (XS[0].used) return true;
    if (!installed()) return false;
    return xs_start(0, w, h, NULL, !chrome_installed() || userns_ok()) == 0;
}
bool plat_browser_frame(uint32_t *dst, int stride, int w, int h) { return xs_frame(&XS[0], dst, stride, w, h); }
bool plat_browser_running(void) { return xs_running(&XS[0]); }
void plat_browser_input(int type, int detail, int x, int y) { plat_xapp_input(0, type, detail, x, y); }
void plat_browser_resize(int w, int h) { if (XS[0].used) xs_resize(&XS[0], w, h); }
void plat_browser_stop(void) { xs_stop(&XS[0]); }

/* Navigateur déjà lancé : un second lancement avec le même profil transmet l'adresse à la
 * fenêtre ouverte (nouvel onglet). Sinon elle sera ouverte au démarrage. */
void plat_browser_open_url(const char *url) {
    if (XS[0].app_pid > 0 && plat_browser_running()) {
        bool u = XS[0].user;
        if (g_hosted && chrome_installed()) {
            char *argv[] = { CHROME_DEB, (char *)url, NULL };
            spawn_as(argv, xenv_for(1, u), u);
        } else if (chrome_installed()) {
            char *argv[] = { "/usr/bin/flatpak", "run", "--filesystem=/media", "--filesystem=xdg-download", CHROME_ID,
                             "--no-sandbox", "--test-type", (char *)url, NULL };
            spawn_as(argv, xenv_for(1, u), u);
        } else {
            char *argv[] = { (char *)chromium_path(), "--no-sandbox",
                             u ? "--user-data-dir=/home/titan/.config/chromium" : "--user-data-dir=/root/.config/chromium", (char *)url, NULL };
            spawn_as(argv, xenv_for(1, u), u);
        }
    } else strlcpy(start_url, url, sizeof start_url);
}
