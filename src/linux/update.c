/* TitanOs - mises à jour depuis GitHub et installation sur le disque interne
 *
 * Mise à jour : la dernière version publiée sur github.com/Titangp1/TitanOs est comparée
 * à celle-ci (TITANOS_BUILD). Son numéro est lu dans le fichier VERSION joint à la version
 * marquée « dernière » (lien direct releases/latest/download/VERSION) : contrairement à
 * l'API de GitHub (60 demandes par heure sans compte), ce lien n'est pas limité. Si elle est plus récente, son paquet titanos-maj.tar.gz est
 * téléchargé (curl, en HTTPS, certificats vérifiés), ses sommes SHA-256 sont vérifiées, puis les
 * fichiers du système sont réécrits sur place : ils ont une taille fixe et donc toujours
 * les mêmes secteurs (voir tools/mkimage.py), ce qui permet au chargeur BIOS de les
 * retrouver. Le chargeur lui-même (MBR et 2e étape) est réécrit en dernier.
 *
 * Installation : la partie TitanOs de la clé (MBR, chargeur, partition FAT32) est copiée
 * au début du disque choisi, qui reçoit sa propre signature. L'espace des applications
 * y sera créé au premier démarrage, dans tout le reste du disque.
 *
 * Tout se fait dans un processus à part, qui envoie son état au bureau par un tube. */
#include "linux.h"
#include "../core/desk.h"

#define REPO      "Titangp1/TitanOs"
#define WORK      "/tmp/maj"

/* fichiers du système, dans la partition TitanOs ; tailles réservées : les mêmes que PADDED
 * dans tools/mkimage.py */
#define NSYS 5
static const struct { const char *path, *name; long size; } SYSFILES[NSYS] = {
    { "TITANOS/VMLINUZ",       "VMLINUZ",     32l << 20 },
    { "TITANOS/INITRD.IMG",    "INITRD.IMG",  64l << 20 },
    { "TITANOS/BOOT.IMG",      "BOOT.IMG",    2l << 20 },
    { "TITANOS/TAILLES",       "TAILLES",     4096 },
    { "EFI/BOOT/BOOTX64.EFI",  "BOOTX64.EFI", 1l << 20 },
};

/* ---------------- état transmis au bureau ---------------- */
typedef struct { int state, percent; char msg[160]; } job_msg_t;
typedef struct { int pid, fd; job_msg_t last; } job_t;
static job_t upd = { -1, -1, { UPD_IDLE, -1, "" } }, ins = { -1, -1, { UPD_IDLE, -1, "" } };
static int job_out = -1;

static void say(int state, int percent, const char *fmt, ...) {
    job_msg_t m = { state, percent, "" };
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m.msg, sizeof m.msg, fmt, ap);
    va_end(ap);
    if (job_out >= 0) sys_write(job_out, &m, sizeof m);
}

static bool job_start(job_t *j, void (*fn)(void *), void *arg) {
    if (j->pid > 0) return false;
    int p[2];
    if (sys_pipe2(p, O_CLOEXEC) < 0) return false;
    int pid = sys_fork();
    if (pid == 0) {
        sys_close(p[0]);
        job_out = p[1];
        fn(arg);
        sys_exit(0);
    }
    sys_close(p[1]);
    if (pid < 0) { sys_close(p[0]); return false; }
    sc3(__NR_fcntl, p[0], F_SETFL, O_NONBLOCK);
    if (j->fd >= 0) sys_close(j->fd);
    j->pid = pid;
    j->fd = p[0];
    return true;
}

static int job_state(job_t *j, char *msg, int n, int *percent) {
    if (j->fd >= 0) {
        job_msg_t m;
        long r;
        while ((r = sys_read(j->fd, &m, sizeof m)) == sizeof m) j->last = m;
        if (r == 0) {                                   /* le processus a fini */
            sys_close(j->fd);
            j->fd = -1;
            j->pid = -1;
            if (j->last.state == UPD_CHECKING || j->last.state == UPD_DOWNLOADING || j->last.state == UPD_INSTALLING) {
                j->last.state = UPD_ERROR;
                strlcpy(j->last.msg, "L'opération s'est arrêtée de façon inattendue.", sizeof j->last.msg);
            }
        }
    }
    if (msg) strlcpy(msg, j->last.msg, (size_t)n);
    if (percent) *percent = j->last.percent;
    return j->last.state;
}

/* ---------------- petits outils ---------------- */
static int run(char *const *argv) {
    int pid = sys_fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int nul = sys_open("/dev/null", O_RDWR, 0);
        if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 1); sys_dup2(nul, 2); }
        sys_execve(argv[0], argv, g_envp);
        sys_exit(127);
    }
    int st = 0;
    if (sys_wait4(pid, &st, 0) < 0) return -1;
    return (st & 0x7F) ? -1 : (st >> 8) & 0xFF;
}

static long file_size(const char *p) {
    struct stat st;
    return sys_stat(p, &st) == 0 ? (long)st.st_size : -1;
}

/* contenu d'un fichier (alloué), ou NULL */
static uint8_t *slurp(const char *p, long *len) {
    long n = file_size(p);
    if (n < 0) return NULL;
    int fd = sys_open(p, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return NULL;
    uint8_t *b = malloc((size_t)n + 1);
    long done = 0;
    while (b && done < n) {
        long r = sys_read(fd, b + done, (size_t)(n - done));
        if (r <= 0) break;
        done += r;
    }
    sys_close(fd);
    if (!b || done != n) { free(b); return NULL; }
    b[n] = 0;
    *len = n;
    return b;
}

static bool write_all(int fd, const void *p, long n) {
    const uint8_t *b = p;
    while (n > 0) {
        long r = sys_write(fd, b, (size_t)(n > (1 << 20) ? 1 << 20 : n));
        if (r <= 0) return false;
        b += r;
        n -= r;
    }
    return true;
}

static bool parent_of(const char *part, char *disk, int n) {
    char sys[96], link[256];
    snprintf(sys, sizeof sys, "/sys/class/block/%s", part);
    long r = sys_readlink(sys, link, sizeof link - 1);
    if (r <= 0) return false;
    link[r] = 0;
    char *last = strrchr(link, '/');
    if (!last) return false;
    *last = 0;
    char *prev = strrchr(link, '/');
    strlcpy(disk, prev ? prev + 1 : link, (size_t)n);
    return strcmp(disk, "block") != 0;
}

/* disque qui porte la clé (ou le disque) TitanOs en cours d'utilisation : « sdb » */
static bool boot_disk(char *disk, int n) {
    static char m[32768];                       /* les montages overlay ont de longues options */
    if (read_file("/proc/mounts", m, sizeof m) <= 0) return false;
    char *p = strstr(m, " /media/titanos ");
    if (!p) return false;
    char *s = p;
    while (s > m && s[-1] != '\n') s--;
    char part[48] = { 0 };
    strlcpy(part, s, (size_t)MIN((long)sizeof part, (long)(p - s + 1)));
    if (!str_starts(part, "/dev/")) return false;
    char sys[96], link[256];
    snprintf(sys, sizeof sys, "/sys/class/block/%s", part + 5);
    long r = sys_readlink(sys, link, sizeof link - 1);
    if (r <= 0) return false;
    link[r] = 0;
    char *last = strrchr(link, '/');
    if (!last) return false;
    *last = 0;
    char *prev = strrchr(link, '/');
    strlcpy(disk, prev ? prev + 1 : link, (size_t)n);
    return strcmp(disk, "block") != 0;
}

static bool disk_mounted(const char *disk) {
    static char m[32768];
    char pat[32];
    if (read_file("/proc/mounts", m, sizeof m) <= 0) return false;
    snprintf(pat, sizeof pat, "/dev/%s", disk);
    return strstr(m, pat) != NULL;
}

/* ================================================================== */
/* Mise à jour                                                          */
/* ================================================================== */
/* en-tête du chargeur BIOS (secteur 1) : emplacement des fichiers */
static bool s2_layout(const uint8_t *s, long n, uint32_t out[3]) {
    if (n < 40 || memcmp(s + 4, "TITN", 4)) return false;
    memcpy(&out[0], s + 8, 4);            /* noyau */
    memcpy(&out[1], s + 16, 4);           /* initramfs */
    memcpy(&out[2], s + 28, 4);           /* images de démarrage */
    return true;
}

/* pourquoi GitHub ne répond pas : adresse, passerelle, DNS ou HTTPS ? */
static void net_diagnose(char *out, int n) {
    char buf[4096];
    bool route = false;
    if (read_file("/proc/net/route", buf, sizeof buf) > 0)
        for (char *l = strchr(buf, '\n'); l && *l; l = strchr(l + 1, '\n')) {
            char *t = strchr(l + 1, '\t');
            if (t && !strncmp(t + 1, "00000000", 8)) { route = true; break; }
        }
    if (!route) { strlcpy(out, "Pas de connexion : aucune adresse reçue du réseau (box, câble ou téléphone).", (size_t)n); return; }
    if (read_file("/etc/resolv.conf", buf, sizeof buf) <= 0 || !strstr(buf, "nameserver")) {
        strlcpy(out, "Connecté, mais aucun serveur DNS n'a été donné par le réseau.", (size_t)n);
        return;
    }
    char *ns[] = { "/bin/busybox", "nslookup", "github.com", NULL };
    if (run(ns) != 0) { strlcpy(out, "Connecté, mais le nom github.com ne se résout pas (DNS).", (size_t)n); return; }
    char err[200] = "";
    read_file(WORK "/erreur", err, sizeof err);
    for (char *c = err; *c; c++) if (*c == '\n') { *c = 0; break; }
    if (strstr(err, "403") || strstr(err, "429"))
        strlcpy(out, "GitHub refuse pour l'instant (trop de demandes) : TitanOs réessaiera plus tard.", (size_t)n);
    else snprintf(out, (size_t)n, "Connexion sécurisée à GitHub impossible : %s", err[0] ? err : "erreur inconnue");
}

/* Réécrit sur place le système d'une partition TitanOs montée en `root`, sur le disque `dev`.
 * src[i] : contenu de chaque fichier du système (complété de zéros jusqu'à sa taille
 * réservée), puis le MBR (440 octets de code) et le chargeur BIOS (stage2). Les fichiers de
 * l'utilisateur ne sont pas touchés. */
static bool apply_system(const char *root, const char *dev, char *const src[NSYS], const uint8_t *mbr,
                         const uint8_t *s2, long s2n, int build) {
    uint8_t cur[512];
    int dfd = sys_open(dev, O_RDWR | O_CLOEXEC, 0);
    uint32_t a[3], b[3];
    bool same = s2 && dfd >= 0 && sys_lseek(dfd, 512, 0) == 512 && sys_read(dfd, cur, 512) == 512 &&
                s2_layout(cur, 512, a) && s2_layout(s2, s2n, b) && !memcmp(a, b, sizeof a);
    char path[160];
    for (int i = 0; same && i < NSYS; i++) {
        snprintf(path, sizeof path, "%s/%s", root, SYSFILES[i].path);
        if (file_size(path) != SYSFILES[i].size) same = false;
    }
    if (!same || s2n > 63 * 512) {
        if (dfd >= 0) sys_close(dfd);
        say(UPD_ERROR, -1, "Ce TitanOs est trop ancien pour se mettre à jour ainsi : réinstallez-le (Rufus ou « Installer sur ce disque »).");
        return false;
    }
    say(UPD_INSTALLING, 0, "Installation de la bêta %d… ne pas éteindre", build);
    static uint8_t zero[65536];
    long total = 0, done = 0;
    for (int i = 0; i < NSYS; i++) total += SYSFILES[i].size;
    for (int i = 0; i < NSYS; i++) {
        long len;
        uint8_t *data = slurp(src[i], &len);
        if (data && len > SYSFILES[i].size) len = SYSFILES[i].size;
        snprintf(path, sizeof path, "%s/%s", root, SYSFILES[i].path);
        int fd = data ? sys_open(path, O_WRONLY | O_CLOEXEC, 0) : -1;
        bool ok = fd >= 0 && write_all(fd, data, len);
        for (long left = SYSFILES[i].size - len; ok && left > 0; left -= (long)sizeof zero)
            ok = write_all(fd, zero, MIN(left, (long)sizeof zero));
        if (fd >= 0) { sc1(__NR_fsync, fd); sys_close(fd); }
        free(data);
        if (!ok) {
            sys_close(dfd);
            say(UPD_ERROR, -1, "Échec de l'écriture de %s : si TitanOs ne démarre plus, réinstallez-le avec Rufus.", SYSFILES[i].name);
            return false;
        }
        done += SYSFILES[i].size;
        say(UPD_INSTALLING, (int)(done * 90 / total), "Installation de la bêta %d… ne pas éteindre", build);
    }
    bool ok = mbr && sys_lseek(dfd, 0, 0) == 0 && write_all(dfd, mbr, 440) &&
              sys_lseek(dfd, 512, 0) == 512 && write_all(dfd, s2, s2n);
    sc1(__NR_fsync, dfd);
    sys_close(dfd);
    char v[16];
    snprintf(path, sizeof path, "%s/TITANOS/VERSION", root);
    snprintf(v, sizeof v, "%d\n", build);
    plat_writefile(path, v, strlen(v));                 /* numéro de la version installée */
    sys_sync();
    if (!ok) { say(UPD_ERROR, -1, "Échec de l'écriture du chargeur de démarrage."); return false; }
    return true;
}

/* paquet titanos-maj.tar.gz (téléchargé ou sur une clé) : vérifié puis installé sur le
 * TitanOs en cours */
static void install_package(const char *tgz, bool quiet) {
    char *tar[] = { "/bin/busybox", "tar", "-xzf", (char *)tgz, "-C", WORK, NULL };
    char *sum[] = { "/bin/busybox", "sh", "-c", "cd " WORK " && /bin/busybox sha256sum -c -s SHA256SUMS", NULL };
    say(UPD_INSTALLING, 0, "Vérification du paquet…");
    if (run(tar) != 0 || run(sum) != 0) { say(UPD_ERROR, -1, "Le paquet de mise à jour est abîmé : rien n'a été changé."); return; }
    char v[16];
    int build = read_file(WORK "/VERSION", v, sizeof v) > 0 ? atoi(v) : -1;
    if (build <= 0) { say(UPD_ERROR, -1, "Paquet de mise à jour incohérent."); return; }
    if (build <= TITANOS_BUILD) { say(quiet ? UPD_UPTODATE : UPD_ERROR, -1, "Ce paquet (bêta %d) n'est pas plus récent que TitanOs (" TITANOS_VERSION ").", build); return; }
    char disk[32], dev[48];
    if (!boot_disk(disk, sizeof disk)) { say(UPD_ERROR, -1, "Disque de TitanOs introuvable."); return; }
    snprintf(dev, sizeof dev, "/dev/%s", disk);
    long s2n, mn;
    uint8_t *s2 = slurp(WORK "/stage2.bin", &s2n), *mbr = slurp(WORK "/mbr.bin", &mn);
    char *src[NSYS];
    static char names[NSYS][64];
    for (int i = 0; i < NSYS; i++) { snprintf(names[i], sizeof names[i], WORK "/%s", SYSFILES[i].name); src[i] = names[i]; }
    bool ok = mn == 440 && apply_system("/media/titanos", dev, src, mbr, s2, s2n, build);
    free(s2);
    free(mbr);
    if (!ok) return;
    klog("mise à jour : bêta %d installée", build);
    say(UPD_DONE, 100, "Bêta %d installée : redémarrez pour l'utiliser.", build);
}

static void update_main(void *arg) {
    bool quiet = arg != NULL;              /* recherche automatique : pas d'erreur affichée */
    say(UPD_CHECKING, -1, "Recherche d'une mise à jour…");
    char *rm[] = { "/bin/busybox", "rm", "-rf", WORK, NULL };
    run(rm);
    mkdirs(WORK);
    char *ver[] = { "/bin/busybox", "sh", "-c", "/usr/bin/curl -fsSL --max-time 20 -o " WORK "/VERSION.latest "
                    "'https://github.com/" REPO "/releases/latest/download/VERSION' 2> " WORK "/erreur", NULL };
    if (run(ver) != 0) {
        char why[160];
        net_diagnose(why, sizeof why);
        klog("mise à jour : %s", why);
        say(quiet ? UPD_IDLE : UPD_ERROR, -1, "%s", why);
        return;
    }
    char vb[16] = "";
    read_file(WORK "/VERSION.latest", vb, sizeof vb);
    int build = vb[0] >= '0' && vb[0] <= '9' ? atoi(vb) : -1;
    if (build < 0) { say(quiet ? UPD_IDLE : UPD_ERROR, -1, "Réponse de GitHub incomprise."); return; }
    if (build <= TITANOS_BUILD) { say(UPD_UPTODATE, -1, "TitanOs est à jour (" TITANOS_VERSION ")."); return; }
    klog("mise à jour : bêta %d disponible", build);
    /* téléchargement, avec la taille reçue affichée */
    char *get[] = { "/usr/bin/curl", "-fsSL", "--max-time", "900", "-o", WORK "/maj.tar.gz",
                    "https://github.com/" REPO "/releases/latest/download/titanos-maj.tar.gz", NULL };
    int pid = sys_fork();
    if (pid == 0) {
        int nul = sys_open("/dev/null", O_RDWR, 0);
        if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 1); sys_dup2(nul, 2); }
        sys_execve(get[0], get, g_envp);
        sys_exit(127);
    }
    int st = -1;
    for (;;) {
        if (sys_wait4(pid, &st, WNOHANG_) == pid) break;
        long got = file_size(WORK "/maj.tar.gz");
        say(UPD_DOWNLOADING, -1, "Téléchargement de la bêta %d… %ld Mo", build, got > 0 ? got >> 20 : 0);
        sleep_ms(500);
    }
    if (st != 0) { say(quiet ? UPD_IDLE : UPD_ERROR, -1, "Le téléchargement a échoué."); return; }
    install_package(WORK "/maj.tar.gz", quiet);
}

/* ---- mise à jour depuis une clé USB : fichier titanos-maj.tar.gz posé sur la clé ---- */
static char pkg_path[160];
static void file_main(void *arg) {
    (void)arg;
    char *rm[] = { "/bin/busybox", "rm", "-rf", WORK, NULL };
    run(rm);
    mkdirs(WORK);
    install_package(pkg_path, false);
}

int plat_update_files(char paths[][160], int max) {
    volume_t v[8];
    int nv = plat_volumes(v, 8), n = 0;
    for (int i = 0; i < nv && n < max; i++) {
        snprintf(paths[n], 160, "%s/titanos-maj.tar.gz", v[i].path);
        if (file_size(paths[n]) > 0) n++;
    }
    return n;
}

bool plat_update_from_file(const char *path) {
    if (ins.pid > 0 || upd.pid > 0) return false;
    strlcpy(pkg_path, path, sizeof pkg_path);
    return job_start(&upd, file_main, NULL);
}

/* ---- depuis une clé TitanOs : mettre à jour le TitanOs installé sur un autre disque ---- */
static char other_part[32];
static void label_cb(const char *name, int type, void *ctx) {
    (void)type;
    const char *boot = ctx;
    char p[96], disk[32];
    snprintf(p, sizeof p, "/sys/class/block/%s/partition", name);
    if (other_part[0] || !path_exists(p) || !parent_of(name, disk, sizeof disk) || !strcmp(disk, boot)) return;
    snprintf(p, sizeof p, "/dev/%s", name);
    int fd = sys_open(p, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return;
    uint8_t b[512];
    if (sys_read(fd, b, 512) == 512 && b[510] == 0x55 && !memcmp(b + 71, "TITANOS    ", 11)) strlcpy(other_part, name, sizeof other_part);
    sys_close(fd);
}

/* un autre TitanOs (sur le disque interne quand on démarre d'une clé) : sa description */
bool plat_other_titanos(char *desc, int n) {
    char boot[32];
    if (!boot_disk(boot, sizeof boot)) return false;
    other_part[0] = 0;
    for_each_dir("/sys/class/block", label_cb, boot);
    if (!other_part[0]) return false;
    char disk[32], p[96], m[64];
    parent_of(other_part, disk, sizeof disk);
    snprintf(p, sizeof p, "/sys/block/%s/device/model", disk);
    if (read_file(p, m, sizeof m) > 0 && trim(m)[0]) snprintf(desc, (size_t)n, "%s (%s)", trim(m), disk);
    else snprintf(desc, (size_t)n, "disque %s", disk);
    return true;
}

static void other_main(void *arg) {
    (void)arg;
    char boot[32], disk[32], dev[48], bdev[48], part[48];
    if (!boot_disk(boot, sizeof boot) || !other_part[0] || !parent_of(other_part, disk, sizeof disk)) { say(UPD_ERROR, -1, "Aucun autre TitanOs trouvé."); return; }
    snprintf(dev, sizeof dev, "/dev/%s", disk);
    snprintf(bdev, sizeof bdev, "/dev/%s", boot);
    snprintf(part, sizeof part, "/dev/%s", other_part);
    say(UPD_INSTALLING, 0, "Préparation…");
    mkdirs("/run/cible");
    if (sys_mount(part, "/run/cible", "vfat", 1024, "iocharset=utf8,shortname=mixed") != 0) { say(UPD_ERROR, -1, "Impossible d'ouvrir le TitanOs du disque."); return; }
    /* le chargeur et le système de cette clé deviennent ceux du disque */
    uint8_t mbr[440];
    static uint8_t s2[63 * 512];
    int fd = sys_open(bdev, O_RDONLY | O_CLOEXEC, 0);
    bool ok = fd >= 0 && sys_read(fd, mbr, 440) == 440 && sys_lseek(fd, 512, 0) == 512 && sys_read(fd, s2, sizeof s2) == (long)sizeof s2;
    if (fd >= 0) sys_close(fd);
    char *src[NSYS];
    static char names[NSYS][64];
    for (int i = 0; i < NSYS; i++) { snprintf(names[i], sizeof names[i], "/media/titanos/%s", SYSFILES[i].path); src[i] = names[i]; }
    if (ok) ok = apply_system("/run/cible", dev, src, mbr, s2, sizeof s2, TITANOS_BUILD);
    else say(UPD_ERROR, -1, "Lecture de la clé impossible.");
    sys_sync();
    sys_umount2("/run/cible", 0);
    if (!ok) return;
    klog("mise à jour du disque %s : bêta %d", disk, TITANOS_BUILD);
    say(UPD_DONE, 100, "Le TitanOs du disque est passé en " TITANOS_VERSION ", vos fichiers sont gardés. Retirez la clé et redémarrez.");
}

bool plat_update_other(void) {
    if (ins.pid > 0 || upd.pid > 0) return false;
    char d[96];
    if (!plat_other_titanos(d, sizeof d)) return false;
    return job_start(&upd, other_main, NULL);
}

void plat_update_check(bool automatic) {
    static uint64_t last;
    if (ins.pid > 0) return;                           /* pas pendant une installation */
    int st = plat_update_state(NULL, 0, NULL);
    if (st == UPD_DONE) return;                        /* déjà installée : il reste à redémarrer */
    if (st == UPD_UPTODATE && last && plat_ms() - last < 60000) return;   /* vérifié il y a moins d'une minute */
    last = plat_ms();
    job_start(&upd, update_main, automatic ? (void *)1 : NULL);
}

int plat_update_state(char *msg, int n, int *percent) { return job_state(&upd, msg, n, percent); }

/* ================================================================== */
/* Installation sur un disque interne                                   */
/* ================================================================== */
static void disk_cb(const char *name, int type, void *ctx) {
    (void)type;
    struct { disk_info_t *list; int max, n; const char *boot; } *c = ctx;
    if (c->n >= c->max) return;
    if (!str_starts(name, "sd") && !str_starts(name, "nvme") && !str_starts(name, "mmcblk") && !str_starts(name, "vd")) return;
    char p[96], v[64];
    snprintf(p, sizeof p, "/sys/block/%s/removable", name);
    bool rem = read_file(p, v, sizeof v) > 0 && v[0] == '1';
    snprintf(p, sizeof p, "/sys/block/%s", name);
    char link[256];
    long lr = sys_readlink(p, link, sizeof link - 1);
    if (lr > 0) { link[lr] = 0; if (strstr(link, "/usb")) rem = true; }
    snprintf(p, sizeof p, "/sys/block/%s/size", name);
    if (read_file(p, v, sizeof v) <= 0) return;
    uint64_t bytes = (uint64_t)strtol(v, NULL, 10) * 512;
    if (bytes < (1ull << 30)) return;                   /* au moins 1 Go */
    disk_info_t *d = &c->list[c->n++];
    memset(d, 0, sizeof *d);
    strlcpy(d->dev, name, sizeof d->dev);
    d->bytes = bytes;
    d->removable = rem;
    d->current = !strcmp(name, c->boot);
    snprintf(p, sizeof p, "/sys/block/%s/device/model", name);
    if (read_file(p, v, sizeof v) > 0) strlcpy(d->model, trim(v), sizeof d->model);
    if (!d->model[0]) strlcpy(d->model, rem ? "Clé USB" : "Disque interne", sizeof d->model);
}

int plat_install_disks(disk_info_t *list, int max) {
    char boot[32] = "";
    boot_disk(boot, sizeof boot);
    struct { disk_info_t *list; int max, n; const char *boot; } c = { list, max, 0, boot };
    for_each_dir("/sys/block", disk_cb, &c);
    return c.n;
}

bool plat_install_possible(void) {
    char boot[32], p[64], v[8];
    if (!boot_disk(boot, sizeof boot)) return false;
    snprintf(p, sizeof p, "/sys/block/%s/removable", boot);
    /* démarré d'une clé (amovible, ou branchée en USB) */
    if (read_file(p, v, sizeof v) > 0 && v[0] == '1') return true;
    snprintf(p, sizeof p, "/sys/block/%s", boot);
    char link[256];
    long r = sys_readlink(p, link, sizeof link - 1);
    if (r <= 0) return false;
    link[r] = 0;
    return strstr(link, "/usb") != NULL;
}

static char install_target[32];

static void install_main(void *arg) {
    (void)arg;
    char boot[32], src[48], dst[48];
    if (!boot_disk(boot, sizeof boot)) { say(UPD_ERROR, -1, "Clé TitanOs introuvable."); return; }
    if (!strcmp(boot, install_target) || disk_mounted(install_target)) { say(UPD_ERROR, -1, "Ce disque est utilisé : impossible d'y installer TitanOs."); return; }
    snprintf(src, sizeof src, "/dev/%s", boot);
    snprintf(dst, sizeof dst, "/dev/%s", install_target);
    say(UPD_INSTALLING, 0, "Préparation…");
    sys_sync();
    int in = sys_open(src, O_RDONLY | O_CLOEXEC, 0), out = sys_open(dst, O_RDWR | O_CLOEXEC, 0);
    uint8_t mbr[512];
    if (in < 0 || out < 0 || sys_read(in, mbr, 512) != 512 || mbr[510] != 0x55 || mbr[511] != 0xAA) {
        if (in >= 0) sys_close(in);
        if (out >= 0) sys_close(out);
        say(UPD_ERROR, -1, "Lecture de la clé ou ouverture du disque impossible.");
        return;
    }
    uint32_t s1, n1;
    memcpy(&s1, mbr + 446 + 8, 4);
    memcpy(&n1, mbr + 446 + 12, 4);
    uint64_t total = ((uint64_t)s1 + n1) * 512;
    static uint8_t buf[1 << 20];
    uint64_t done = 0;
    sys_lseek(in, 0, 0);
    klog("installation : copie de %s vers %s (%d Mo)", src, dst, (int)(total >> 20));
    while (done < total) {
        long want = (long)MIN((uint64_t)sizeof buf, total - done);
        long r = sys_read(in, buf, (size_t)want);
        if (r <= 0 || !write_all(out, buf, r)) {
            sys_close(in);
            sys_close(out);
            say(UPD_ERROR, -1, "Erreur pendant la copie : le disque n'est pas utilisable en l'état.");
            return;
        }
        done += (uint64_t)r;
        say(UPD_INSTALLING, (int)(done * 95 / total), "Copie de TitanOs sur le disque… %d / %d Mo", (int)(done >> 20), (int)(total >> 20));
    }
    /* table de partitions du disque : seulement la partition TitanOs, et sa propre signature */
    uint8_t m2[512];
    memcpy(m2, mbr, 512);
    memset(m2 + 446 + 16, 0, 48);                       /* pas l'espace des applications de la clé */
    uint32_t sig = 0;
    sc3(__NR_getrandom, (long)&sig, 4, 0);
    if (!sig) sig = (uint32_t)plat_ms() * 2654435761u;
    memcpy(m2 + 440, &sig, 4);
    bool ok = sys_lseek(out, 0, 0) == 0 && write_all(out, m2, 512);
    sc1(__NR_fsync, out);
    sys_ioctl(out, 0x125F /* BLKRRPART */, NULL);
    sys_close(in);
    sys_close(out);
    sys_sync();
    if (!ok) { say(UPD_ERROR, -1, "Écriture de la table de partitions impossible."); return; }
    klog("installation terminée sur %s", dst);
    say(UPD_DONE, 100, "TitanOs est installé. Pour démarrer dessus, choisissez ce disque au démarrage de l'ordinateur.");
}

bool plat_install_start(const char *dev) {
    if (upd.pid > 0 && plat_update_state(NULL, 0, NULL) == UPD_INSTALLING) return false;
    strlcpy(install_target, dev, sizeof install_target);
    return job_start(&ins, install_main, NULL);
}

int plat_install_state(char *msg, int n, int *percent) { return job_state(&ins, msg, n, percent); }
