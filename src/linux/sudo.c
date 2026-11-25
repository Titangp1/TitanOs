/* TitanOs - sudo : lancer une commande en administrateur, avec le mot de passe de session
 *
 * Le Terminal tourne sous le compte « titan », sans droits d'administrateur. sudo est le
 * programme de TitanOs lui-même (le même fichier que /init), installé en /usr/bin/sudo avec
 * le bit « setuid root ». Il demande le mot de passe de session (celui de l'écran de
 * verrouillage, Paramètres -> Compte), puis lance la commande en root. Comme le sudo
 * habituel : la réussite est gardée 5 minutes par terminal, trois essais au plus, et chaque
 * commande est notée dans le journal du noyau. Sans mot de passe défini, sudo refuse. */
#include "linux.h"
#include "../core/desk.h"

struct ktermios { uint32_t c_iflag, c_oflag, c_cflag, c_lflag; uint8_t c_line; uint8_t c_cc[19]; };
#define T_ECHO   0000010
#define T_ECHONL 0000100

static void out(int fd, const char *s) { sys_write(fd, s, strlen(s)); }

static void wipe(char *p, size_t n) { volatile char *v = p; while (n--) *v++ = 0; }

/* mot de passe lu sur le terminal, sans l'afficher */
static int read_pass(char *buf, int n, const char *prompt) {
    int fd = sys_open("/dev/tty", O_RDWR | O_CLOEXEC | O_NOCTTY, 0);
    if (fd < 0) return -1;
    struct ktermios t0, t;
    bool have = sys_ioctl(fd, TCGETS, &t0) == 0;
    if (have) {
        t = t0;
        t.c_lflag &= ~(uint32_t)(T_ECHO | T_ECHONL);
        sys_ioctl(fd, TCSETS, &t);
    }
    out(fd, prompt);
    int k = 0;
    for (;;) {
        char c;
        long r = sys_read(fd, &c, 1);
        if (r <= 0) { if (!k) k = -2; break; }
        if (c == '\n' || c == '\r') break;
        if (k >= 0 && k < n - 1) buf[k++] = c;
    }
    if (k >= 0) buf[k] = 0;
    if (have) sys_ioctl(fd, TCSETS, &t0);
    out(fd, "\n");
    sys_close(fd);
    return k < 0 ? -1 : k;
}

/* réussite gardée 5 minutes, par utilisateur et par terminal (session) */
static void ts_path(char *p, int n, int uid) { snprintf(p, (size_t)n, "/run/sudo/%d-%ld", uid, sc1(__NR_getsid, 0)); }
static bool ts_valid(int uid) {
    char p[64];
    ts_path(p, sizeof p, uid);
    struct stat st;
    if (sys_stat(p, &st) != 0 || st.st_uid != 0) return false;
    struct timespec now;
    sys_clock_gettime(CLOCK_REALTIME, &now);
    return (long)st.st_mtime <= now.tv_sec && now.tv_sec - (long)st.st_mtime < 300;
}
static void ts_touch(int uid) {
    sys_mkdir("/run/sudo", 0700);
    sc2(__NR_chmod, (long)"/run/sudo", 0700);
    char p[64];
    ts_path(p, sizeof p, uid);
    int fd = sys_open(p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd >= 0) { sys_write(fd, "1", 1); sys_close(fd); }
}
static void ts_clear(int uid) {
    char p[64];
    ts_path(p, sizeof p, uid);
    sys_unlink(p);
}

static void usage(void) {
    out(2, "utilisation : sudo commande [arguments]   lance la commande en administrateur (root)\n"
           "              sudo -i                      ouvre un shell root\n"
           "              sudo -k                      oublie le mot de passe tout de suite\n"
           "Le mot de passe demandé est celui de la session (Paramètres -> Compte).\n");
}

/* commande cherchée dans le PATH de root */
static bool find_cmd(const char *c, char *path, int n) {
    if (strchr(c, '/')) { strlcpy(path, c, (size_t)n); return sys_access(path, 1 /* X_OK */) == 0; }
    static const char *const dirs[] = { "/usr/local/sbin", "/usr/local/bin", "/usr/sbin", "/usr/bin", "/sbin", "/bin" };
    for (int i = 0; i < countof(dirs); i++) {
        snprintf(path, (size_t)n, "%s/%s", dirs[i], c);
        struct stat st;
        if (sys_stat(path, &st) == 0 && (st.st_mode & 0170000) == 0100000 && sys_access(path, 1) == 0) return true;
    }
    return false;
}

static const char *getenv_(const char *name) {
    size_t l = strlen(name);
    for (char **e = g_envp; e && *e; e++) if (!strncmp(*e, name, l) && (*e)[l] == '=') return *e;
    return NULL;
}

int sudo_main(int argc, char **argv) {
    int ruid = (int)sc0(__NR_getuid), rgid = (int)sc0(__NR_getgid);
    bool shell = false, login = false, reset = false, validate = false, nonint = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--")) { i++; break; }
        if (!strcmp(a, "-u")) {
            if (i + 1 >= argc || (strcmp(argv[i + 1], "root") && strcmp(argv[i + 1], "0"))) { out(2, "sudo : seul l'utilisateur root est disponible\n"); return 1; }
            i++;
            continue;
        }
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        if (!strcmp(a, "-V") || !strcmp(a, "--version")) { out(1, "sudo de TitanOs " TITANOS_VERSION "\n"); return 0; }
        for (const char *o = a + 1; *o; o++) {
            if (*o == 'i') shell = login = true;
            else if (*o == 's') shell = true;
            else if (*o == 'k' || *o == 'K') reset = true;
            else if (*o == 'v') validate = true;
            else if (*o == 'n') nonint = true;
            else if (*o == 'E' || *o == 'H' || *o == 'S') {}
            else { out(2, "sudo : option inconnue\n"); usage(); return 1; }
        }
    }
    bool has_cmd = i < argc;
    if (reset) { if (ruid != 0) ts_clear(ruid); if (!has_cmd && !shell && !validate) return 0; }
    if (!has_cmd && !shell && !validate) { usage(); return 1; }

    if (ruid != 0) {
        if (sc0(__NR_geteuid) != 0) { out(2, "sudo : ce programme doit être installé « setuid root »\n"); return 1; }
        /* réglages de TitanOs : le mot de passe de session */
        if (path_exists("/media/titanos/titanos.cfg"))
            plat_set_storage("/media/titanos/Utilisateur", "/media/titanos/titanos.cfg", "");
        else plat_set_storage("/root/Utilisateur", "/root/titanos.cfg", "");
        settings_load();
        if (!g_set.pass[0]) {
            out(2, "sudo : aucun mot de passe de session n'est défini.\n"
                   "       Choisissez-en un dans Paramètres -> Compte : il protégera sudo.\n");
            klog("sudo : refusé (aucun mot de passe de session) pour uid %d", ruid);
            return 1;
        }
        if (!ts_valid(ruid)) {
            if (nonint) { out(2, "sudo : un mot de passe est nécessaire\n"); return 1; }
            bool ok = false;
            for (int t = 0; t < 3 && !ok; t++) {
                char pw[128];
                int n = read_pass(pw, sizeof pw, "[sudo] mot de passe de session : ");
                if (n < 0) { out(2, "sudo : pas de terminal pour demander le mot de passe\n"); return 1; }
                ok = n > 0 && settings_pass_check(pw);
                wipe(pw, sizeof pw);
                if (!ok) {
                    sleep_ms(1500);
                    if (t < 2) out(2, "Désolé, essayez de nouveau.\n");
                }
            }
            if (!ok) {
                out(2, "sudo : 3 mots de passe incorrects\n");
                klog("sudo : mot de passe refusé 3 fois (uid %d)", ruid);
                return 1;
            }
        }
        ts_touch(ruid);
    }
    if (!has_cmd && !shell) return 0;                    /* sudo -v */

    char what[200];
    if (shell) strlcpy(what, login ? "shell root (sudo -i)" : "shell root (sudo -s)", sizeof what);
    else {
        int k = 0;
        what[0] = 0;
        for (int j = i; j < argc && k < (int)sizeof what - 2; j++)
            k += snprintf(what + k, sizeof what - (size_t)k, "%s%s", j > i ? " " : "", argv[j]);
    }
    klog("sudo : %s a lancé « %s »", ruid == 1000 ? "titan" : ruid == 0 ? "root" : "un utilisateur", what);

    /* root, avec un environnement propre */
    sc2(__NR_setgroups, 0, 0);
    if (sc3(__NR_setresgid, 0, 0, 0) != 0 || sc3(__NR_setresuid, 0, 0, 0) != 0) { out(2, "sudo : impossible de devenir root\n"); return 1; }
    static char *env[20];
    int k = 0;
    static const char *const keep[] = { "TERM", "LANG", "LC_ALL", "COLORTERM", "DISPLAY", "COLUMNS", "LINES" };
    for (int j = 0; j < countof(keep); j++) { const char *v = getenv_(keep[j]); if (v) env[k++] = (char *)v; }
    static char su[48], suid[32], sgid[32];
    snprintf(su, sizeof su, "SUDO_USER=%s", ruid == 1000 ? "titan" : "root");
    snprintf(suid, sizeof suid, "SUDO_UID=%d", ruid);
    snprintf(sgid, sizeof sgid, "SUDO_GID=%d", rgid);
    env[k++] = "HOME=/root";
    env[k++] = "USER=root";
    env[k++] = "LOGNAME=root";
    env[k++] = "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
    env[k++] = su;
    env[k++] = suid;
    env[k++] = sgid;
    bool bash = path_exists("/bin/bash");
    env[k++] = bash ? "SHELL=/bin/bash" : "SHELL=/bin/sh";
    env[k] = NULL;
    if (shell) {
        const char *sh = bash ? "/bin/bash" : "/bin/sh";
        char *av[] = { login ? (bash ? "-bash" : "-sh") : (char *)sh, NULL };
        if (login) sys_chdir("/root");
        sys_execve(sh, av, env);
        out(2, "sudo : shell introuvable\n");
        return 1;
    }
    char path[512];
    if (!find_cmd(argv[i], path, sizeof path)) {
        out(2, "sudo : commande introuvable : ");
        out(2, argv[i]);
        out(2, "\n");
        return 127;
    }
    long r = sys_execve(path, argv + i, env);
    if (r == -ENOEXEC) {                                 /* script sans « #! » */
        char *av[64];
        int n = 0;
        av[n++] = "sh";
        av[n++] = path;
        for (int j = i + 1; j < argc && n < 63; j++) av[n++] = argv[j];
        av[n] = NULL;
        sys_execve("/bin/sh", av, env);
    }
    out(2, "sudo : impossible de lancer ");
    out(2, argv[i]);
    out(2, "\n");
    return 126;
}
