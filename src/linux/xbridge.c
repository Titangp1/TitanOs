/* TitanOs - Chromium dans une fenêtre TitanOs
 *
 * Chromium (la base libre de Google Chrome) a besoin d'un serveur X11. TitanOs lance Xvfb,
 * un serveur X qui dessine en mémoire : son image est un fichier (/run/xvfb/Xvfb_screen0,
 * format XWD) que la fenêtre TitanOs recopie. Les clics et les touches sont renvoyés au
 * serveur X par l'extension XTEST, avec un petit client X11 écrit ici (quelques requêtes).
 *
 * Chromium et ses bibliothèques (environ 370 Mo à télécharger, 900 Mo installés) vont
 * dans l'espace des applications, avec apk, au premier accès à Internet. */
#include "linux.h"
#include "../core/desk.h"

#define FBDIR   "/run/xvfb"
#define XSOCK   "/tmp/.X11-unix/X1"
static const char *const PKGS[] = { "chromium", "xvfb", "setxkbmap", "font-dejavu", "font-liberation", NULL };

/* ---------------- installation (apk) ---------------- */
static int inst_pid = -1, inst_fd = -1, inst_state = BR_NONE, inst_pc = -1;
static char inst_msg[160], inst_line[256];
static int inst_len;

static const char *chromium_path(void) {
    if (path_exists("/usr/bin/chromium-browser")) return "/usr/bin/chromium-browser";
    if (path_exists("/usr/bin/chromium")) return "/usr/bin/chromium";
    return NULL;
}
static bool installed(void) { return chromium_path() && path_exists("/usr/bin/Xvfb"); }

bool plat_browser_install(void) {
    if (inst_pid > 0 || installed()) return false;
    if (!path_exists("/.titan/upper")) {
        inst_state = BR_ERROR;
        strlcpy(inst_msg, "Il faut l'espace des applications (TitanOs sur une clé ou un disque d'au moins 4 Go).", sizeof inst_msg);
        return false;
    }
    int p[2];
    if (sys_pipe2(p, O_CLOEXEC) < 0) return false;
    int pid = sys_fork();
    if (pid == 0) {
        sys_setsid();
        sys_dup2(p[1], 1);
        sys_dup2(p[1], 2);
        int nul = sys_open("/dev/null", O_RDONLY, 0);
        if (nul >= 0) sys_dup2(nul, 0);
        char *argv[16] = { "/sbin/apk", "add", "--no-progress", NULL };
        int k = 3;
        for (int i = 0; PKGS[i]; i++) argv[k++] = (char *)PKGS[i];
        argv[k] = NULL;
        sys_execve(argv[0], argv, g_envp);
        sys_exit(127);
    }
    sys_close(p[1]);
    if (pid < 0) { sys_close(p[0]); return false; }
    sc3(__NR_fcntl, p[0], F_SETFL, O_NONBLOCK);
    inst_pid = pid;
    inst_fd = p[0];
    inst_state = BR_INSTALLING;
    inst_pc = 0;
    inst_len = 0;
    strlcpy(inst_msg, "Préparation de l'installation…", sizeof inst_msg);
    klog("Chromium : installation lancée");
    return true;
}

/* lignes d'apk : « (12/249) Installing libdrm (2.4.124-r0) » */
static void inst_poll(void) {
    if (inst_fd < 0) return;
    char buf[512];
    long r;
    while ((r = sys_read(inst_fd, buf, sizeof buf)) > 0) {
        for (long i = 0; i < r; i++) {
            char c = buf[i];
            if (c != '\n' && inst_len < (int)sizeof inst_line - 1) { inst_line[inst_len++] = c; continue; }
            inst_line[inst_len] = 0;
            inst_len = 0;
            int a = 0, b = 0;
            if (inst_line[0] == '(') {
                a = atoi(inst_line + 1);
                char *sl = strchr(inst_line, '/');
                if (sl) b = atoi(sl + 1);
            }
            if (a > 0 && b > 0) {
                inst_pc = a * 100 / b;
                char *name = strstr(inst_line, "Installing ");
                if (name) {
                    char pk[64];
                    strlcpy(pk, name + 11, sizeof pk);
                    char *sp = strchr(pk, ' ');
                    if (sp) *sp = 0;
                    snprintf(inst_msg, sizeof inst_msg, "Installation de Chromium… %d/%d (%s)", a, b, pk);
                }
            } else if (inst_line[0]) {
                if (strstr(inst_line, "fetch")) strlcpy(inst_msg, "Téléchargement de la liste des paquets…", sizeof inst_msg);
                if (strstr(inst_line, "ERROR") || strstr(inst_line, "error")) klog("Chromium : %s", inst_line);
            }
        }
    }
    if (r == 0) {                                   /* apk a terminé */
        sys_close(inst_fd);
        inst_fd = -1;
        int st = 0;
        sys_wait4(inst_pid, &st, 0);
        inst_pid = -1;
        sys_sync();
        if (installed()) { inst_state = BR_READY; strlcpy(inst_msg, "Chromium est installé.", sizeof inst_msg); klog("Chromium : installé"); }
        else { inst_state = BR_ERROR; strlcpy(inst_msg, "L'installation a échoué : vérifiez la connexion à Internet et réessayez.", sizeof inst_msg); }
    }
}

/* ---------------- serveur X et Chromium ---------------- */
static int xvfb_pid = -1, chrome_pid = -1, xfd = -1, xtest_op = -1;
static int scr_w_, scr_h_;
static const uint8_t *fb;
static size_t fb_size;
static int fb_off, fb_pitch;
static bool started;

static int spawn_env(char *const *argv, char *const *env) {
    int pid = sys_fork();
    if (pid == 0) {
        sys_setsid();
        int nul = sys_open("/dev/null", O_RDWR, 0);
        if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 1); sys_dup2(nul, 2); }
        sys_execve(argv[0], argv, env);
        sys_exit(127);
    }
    return pid;
}

static char *xenv[] = { "DISPLAY=:1", "HOME=/root", "LANG=fr_FR.UTF-8", "PATH=/usr/bin:/bin:/usr/sbin:/sbin",
                        "XDG_CONFIG_HOME=/root/.config", "XDG_RUNTIME_DIR=/run/xdg", NULL };

bool plat_browser_start(int w, int h) {
    if (started || !installed()) return started;
    scr_w_ = w;
    scr_h_ = h;
    mkdirs(FBDIR);
    mkdirs("/tmp/.X11-unix");
    mkdirs("/run/xdg");
    sc2(__NR_chmod, (long)"/tmp/.X11-unix", 01777);
    sys_unlink(XSOCK);
    sys_unlink(FBDIR "/Xvfb_screen0");
    char geo[32];
    snprintf(geo, sizeof geo, "%dx%dx24", w, h);
    char *argv[] = { "/usr/bin/Xvfb", ":1", "-screen", "0", geo, "-fbdir", FBDIR, "-nolisten", "tcp", "-ac", "-dpi", "96", NULL };
    xvfb_pid = spawn_env(argv, xenv);
    started = xvfb_pid > 0;
    klog("Chromium : serveur X %dx%d lancé", w, h);
    return started;
}

/* petit client X11 : connexion, puis extension XTEST */
static bool x_connect(void) {
    int fd = sys_socket(1 /* AF_UNIX */, 1 /* SOCK_STREAM */ | O_CLOEXEC, 0);
    if (fd < 0) return false;
    struct { uint16_t family; char path[108]; } a = { 1, { 0 } };
    strlcpy(a.path, XSOCK, sizeof a.path);
    if (sc3(__NR_connect, fd, (long)&a, sizeof a) < 0) { sys_close(fd); return false; }
    uint8_t setup[12] = { 'l', 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (sys_write(fd, setup, 12) != 12) { sys_close(fd); return false; }
    uint8_t hdr[8];
    if (sys_read(fd, hdr, 8) != 8 || hdr[0] != 1) { sys_close(fd); return false; }
    size_t more = (size_t)(hdr[6] | hdr[7] << 8) * 4;
    static uint8_t junk[16384];
    while (more) { long r = sys_read(fd, junk, MIN(more, sizeof junk)); if (r <= 0) { sys_close(fd); return false; } more -= (size_t)r; }
    /* QueryExtension("XTEST") */
    uint8_t q[16] = { 98, 0, 4, 0, 5, 0, 0, 0, 'X', 'T', 'E', 'S', 'T', 0, 0, 0 };
    uint8_t rep[32];
    if (sys_write(fd, q, 16) != 16 || sys_read(fd, rep, 32) != 32 || rep[0] != 1 || !rep[8]) { sys_close(fd); return false; }
    xtest_op = rep[9];
    sc3(__NR_fcntl, fd, F_SETFL, O_NONBLOCK);
    xfd = fd;
    return true;
}

static void x_fake(int type, int detail, int x, int y) {
    if (xfd < 0) return;
    uint8_t junk[256];
    while (sys_read(xfd, junk, sizeof junk) > 0) {}           /* erreurs ou événements : ignorés */
    uint8_t r[36] = { 0 };
    r[0] = (uint8_t)xtest_op; r[1] = 2; r[2] = 9; r[3] = 0;
    r[4] = (uint8_t)type; r[5] = (uint8_t)detail;
    r[24] = (uint8_t)x; r[25] = (uint8_t)(x >> 8); r[26] = (uint8_t)y; r[27] = (uint8_t)(y >> 8);
    sys_write(xfd, r, 36);
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void map_fb(void) {
    if (fb) return;
    int fd = sys_open(FBDIR "/Xvfb_screen0", O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return;
    struct stat st;
    if (sc2(__NR_fstat, fd, (long)&st) < 0 || st.st_size < 200) { sys_close(fd); return; }
    void *m = sys_mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    sys_close(fd);
    if ((long)m < 0 && (long)m > -4096) return;
    const uint8_t *h = m;
    uint32_t hsize = be32(h), bpp = be32(h + 44), pitch = be32(h + 48), ncolors = be32(h + 76), w = be32(h + 16), hh = be32(h + 20);
    if (bpp != 32 || w < (uint32_t)scr_w_ || hh < (uint32_t)scr_h_ || hsize + ncolors * 12 + (size_t)pitch * hh > (size_t)st.st_size) {
        sys_munmap(m, (size_t)st.st_size);
        return;
    }
    fb = h;
    fb_size = (size_t)st.st_size;
    fb_off = (int)(hsize + ncolors * 12);
    fb_pitch = (int)pitch;
}

/* image actuelle de Chromium : true si elle a changé depuis la dernière fois */
bool plat_browser_frame(uint32_t *dst, int stride, int w, int h) {
    if (!started) return false;
    if (xfd < 0) {
        if (!path_exists(XSOCK) || !x_connect()) return false;
        /* clavier du serveur X : même disposition que TitanOs */
        char *kb[] = { "/usr/bin/setxkbmap", g_set.layout ? "us" : "fr", NULL };
        spawn_env(kb, xenv);
        const char *cp = chromium_path();
        char size[48];
        snprintf(size, sizeof size, "--window-size=%d,%d", scr_w_, scr_h_);
        char *argv[] = { (char *)cp, "--no-sandbox", "--test-type", "--disable-gpu", "--no-first-run", "--no-default-browser-check",
                         "--password-store=basic", "--disable-dev-shm-usage", "--window-position=0,0", size, "--start-maximized",
                         "--lang=fr", "--user-data-dir=/root/.config/chromium", "https://www.google.fr", NULL };
        chrome_pid = spawn_env(argv, xenv);
        klog("Chromium : lancé (pid %d)", chrome_pid);
    }
    map_fb();
    if (!fb) return false;
    w = MIN(w, scr_w_);
    h = MIN(h, scr_h_);
    bool changed = false;
    for (int y = 0; y < h; y++) {
        const uint32_t *s = (const uint32_t *)(fb + fb_off + (size_t)y * fb_pitch);
        uint32_t *d = dst + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            uint32_t v = s[x] | 0xFF000000u;
            if (d[x] != v) { d[x] = v; changed = true; }
        }
    }
    return changed;
}

bool plat_browser_running(void) {
    if (chrome_pid > 0) {
        char p[32];
        snprintf(p, sizeof p, "/proc/%d", chrome_pid);
        if (!path_exists(p)) return false;
    }
    return started;
}

void plat_browser_input(int type, int detail, int x, int y) {
    x = CLAMP(x, 0, scr_w_ - 1);
    y = CLAMP(y, 0, scr_h_ - 1);
    x_fake(type, detail, x, y);
}

void plat_browser_stop(void) {
    if (chrome_pid > 0) sys_kill(chrome_pid, 15);
    if (xvfb_pid > 0) sys_kill(xvfb_pid, 15);
    if (xfd >= 0) sys_close(xfd);
    if (fb) sys_munmap((void *)fb, fb_size);
    fb = NULL;
    xfd = -1;
    chrome_pid = xvfb_pid = -1;
    started = false;
}

int plat_browser_state(char *msg, int n, int *percent) {
    inst_poll();
    int st = inst_state;
    if (st == BR_NONE || st == BR_READY) st = installed() ? BR_READY : BR_NONE;
    if (msg) strlcpy(msg, st == BR_NONE ? "Chromium n'est pas encore installé." : inst_msg, (size_t)n);
    if (percent) *percent = inst_pc;
    return st;
}
