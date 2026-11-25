/* TitanOs - mises à jour depuis GitHub et installation sur le disque interne
 *
 * Mise à jour : la dernière version publiée sur github.com/Titangp1/TitanOs est comparée
 * à celle-ci (TITANOS_BUILD). Son numéro est lu dans le fichier VERSION joint à la version
 * marquée « dernière » (lien direct releases/latest/download/VERSION) : contrairement à
 * l'API de GitHub (60 demandes par heure sans compte), ce lien n'est pas limité. Si elle est plus récente, son paquet titanos-maj.tar.gz est
 * téléchargé (curl, en HTTPS, certificats vérifiés), ses sommes SHA-256 sont vérifiées, puis les
 * fichiers du système sont réécrits sur place : ils ont une taille fixe et gardent donc leurs
 * secteurs (voir tools/mkimage.py). Le chargeur BIOS (MBR et 2e étape) est réécrit en dernier,
 * avec l'emplacement de ces fichiers sur ce disque-là (demandé au noyau : FIBMAP).
 *
 * Installation (clé d'installation) : le disque choisi est entièrement repartitionné. Une
 * partition FAT32 (système et fichiers de l'utilisateur) prend la plus grande part ; le reste
 * devient l'espace des applications au premier démarrage, où Debian s'installe.
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
/* ---------------- emplacement des fichiers du système sur le disque ---------------- */
/* Le chargeur BIOS lit le noyau, l'initramfs et l'image de démarrage par numéros de secteur :
 * chacun doit être d'un seul tenant. Le noyau donne l'emplacement de chaque bloc (FIBMAP). */
static bool file_lba(const char *path, uint64_t part_start, uint32_t *lba) {
    int fd = sys_open(path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return false;
    long size = file_size(path);
    int bsz = 0, first = 0;
    bool ok = size > 0 && sys_ioctl(fd, 2 /* FIGETBSZ */, &bsz) == 0 && bsz >= 512 && bsz % 512 == 0 &&
              sys_ioctl(fd, 1 /* FIBMAP */, &first) == 0 && first > 0;
    long nblk = ok ? (size + bsz - 1) / bsz : 0, step = ok ? MAX(1, 262144 / bsz) : 1;
    for (long k = step; ok; k += step) {                    /* d'un seul tenant : vérifié tous les 256 Ko */
        int b = (int)MIN(k, nblk - 1), phys = b;
        ok = sys_ioctl(fd, 1, &phys) == 0 && phys == first + b;
        if (b == nblk - 1) break;
    }
    sys_close(fd);
    if (ok) *lba = (uint32_t)(part_start + (uint64_t)first * (uint64_t)(bsz / 512));
    return ok;
}

/* partition montée sur root (sda1...) */
static bool mount_part(const char *root, char *part, int n) {
    static char m[32768];
    if (read_file("/proc/mounts", m, sizeof m) <= 0) return false;
    char key[96];
    snprintf(key, sizeof key, " %s ", root);
    char *p = strstr(m, key);
    if (!p) return false;
    char *s = p;
    while (s > m && s[-1] != '\n') s--;
    int len = (int)(p - s) - 5;
    if (strncmp(s, "/dev/", 5) || len <= 0 || len >= n) return false;
    memcpy(part, s + 5, (size_t)len);
    part[len] = 0;
    return true;
}

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

/* inscrit dans la 2e étape du chargeur l'emplacement du noyau, de l'initramfs et de l'image de
 * démarrage qui sont sous root (kernel : le noyau qui y sera, pour la taille de son en-tête) */
static bool place_stage2(uint8_t *s2, long s2n, const char *root, const char *kernel) {
    char part[48], p[160], v[32];
    if (s2n < 64 || memcmp(s2 + 4, "TITN", 4) || !mount_part(root, part, sizeof part)) return false;
    snprintf(p, sizeof p, "/sys/class/block/%s/start", part);
    uint64_t start = read_file(p, v, sizeof v) > 0 ? (uint64_t)strtol(v, NULL, 10) : 0;
    if (!start) return false;
    uint32_t k, in, b;
    snprintf(p, sizeof p, "%s/TITANOS/VMLINUZ", root);
    if (!file_lba(p, start, &k)) { klog("chargeur : noyau introuvable ou en plusieurs morceaux"); return false; }
    snprintf(p, sizeof p, "%s/TITANOS/INITRD.IMG", root);
    if (!file_lba(p, start, &in)) { klog("chargeur : initramfs introuvable ou en plusieurs morceaux"); return false; }
    snprintf(p, sizeof p, "%s/TITANOS/BOOT.IMG", root);
    if (!file_lba(p, start, &b)) { klog("chargeur : image de démarrage introuvable ou en plusieurs morceaux"); return false; }
    uint8_t kh[0x200];
    int fd = sys_open(kernel, O_RDONLY | O_CLOEXEC, 0);
    bool ok = fd >= 0 && sys_read(fd, kh, sizeof kh) == (long)sizeof kh;
    if (fd >= 0) sys_close(fd);
    if (!ok) return false;
    int setup = kh[0x1F1] ? kh[0x1F1] : 4;
    put32(s2 + 8, k + (uint32_t)setup + 1);              /* partie « protégée » du noyau */
    put32(s2 + 16, in);
    put32(s2 + 28, b);
    klog("chargeur : noyau au secteur %u, initramfs %u, image %u", k, in, b);
    return true;
}

static bool apply_system(const char *root, const char *dev, char *const src[NSYS], const uint8_t *mbr,
                         const uint8_t *s2_in, long s2n, int build) {
    int dfd = sys_open(dev, O_RDWR | O_CLOEXEC, 0);
    bool same = s2_in && dfd >= 0 && s2n <= 63 * 512;
    char path[160];
    for (int i = 0; same && i < NSYS; i++) {
        snprintf(path, sizeof path, "%s/%s", root, SYSFILES[i].path);
        if (file_size(path) != SYSFILES[i].size) same = false;
    }
    /* le chargeur reçoit l'emplacement des fichiers sur ce disque (ils sont réécrits sur place :
     * il ne change pas) ; vérifié avant de toucher à quoi que ce soit */
    static uint8_t s2[63 * 512];
    if (same) {
        memcpy(s2, s2_in, (size_t)s2n);
        same = place_stage2(s2, s2n, root, src[0]);
    }
    if (!same) {
        if (dfd >= 0) sys_close(dfd);
        say(UPD_ERROR, -1, "Ce TitanOs est trop ancien pour se mettre à jour ainsi : réinstallez-le avec la clé d'installation.");
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
static bool upd_ok;                    /* le dernier paquet a été installé */
static void install_package_(const char *tgz, bool quiet) {
    upd_ok = false;
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
    upd_ok = true;
    say(UPD_DONE, 100, "Bêta %d installée : redémarrez pour l'utiliser.", build);
}

/* le paquet et ses fichiers extraits (une centaine de Mo, en mémoire vive) sont effacés
 * dès que l'installation est finie, réussie ou non */
static void install_package(const char *tgz, bool quiet) {
    install_package_(tgz, quiet);
    char *rm[] = { "/bin/busybox", "rm", "-rf", WORK, NULL };
    run(rm);
}

/* ================================================================== */
/* Base Debian                                                          */
/* ================================================================== */
/* Un TitanOs autonome (installé avant la base Debian) télécharge ses deux archives dans
 * l'espace des applications, au contrôle des mises à jour : elle s'installe au redémarrage. */
static bool debian_missing(void) {
    if (g_hosted || !path_exists("/.titan/upper") || path_exists("/.titan/debian/etc/titanos-ready")) return false;
    if (path_exists("/media/titanos/TITANOS/DEBIAN.TXZ") && path_exists("/media/titanos/TITANOS/DEBS.TAR")) return false;
    return !(path_exists("/.titan/DEBIAN.TXZ") && path_exists("/.titan/DEBS.TAR"));
}

static long remote_size(const char *name) {
    char script[400];
    snprintf(script, sizeof script, "/usr/bin/curl -fsSIL --max-time 30 'https://github.com/" REPO "/releases/latest/download/%s'"
             " | tr -d '\\r' | grep -i '^content-length:' | tail -n 1 | cut -d' ' -f2 > " WORK "/taille", name);
    char *argv[] = { "/bin/busybox", "sh", "-c", script, NULL };
    char t[32] = "";
    if (run(argv) != 0 || read_file(WORK "/taille", t, sizeof t) <= 0) return 0;
    return strtol(t, NULL, 10);
}

static void debian_fetch(bool quiet) {
    struct { long type, bsize; unsigned long blocks, bfree, bavail, files, ffree; int fsid[2]; long namelen, frsize, flags, spare[4]; } fs;
    if (sc2(__NR_statfs, (long)"/.titan", (long)&fs) == 0 && (long)(fs.bavail * (unsigned long)fs.bsize >> 20) < 2200) {
        klog("base Debian : pas assez de place dans l'espace des applications");
        say(quiet ? UPD_UPTODATE : UPD_ERROR, -1, "Base Debian : il faut 2,2 Go libres dans l'espace des applications.");
        return;
    }
    mkdirs(WORK);
    static const char *const names[2] = { "DEBIAN.TXZ", "DEBS.TAR" };
    long size[2], total = 0, before = 0;
    for (int i = 0; i < 2; i++) total += size[i] = remote_size(names[i]);
    klog("base Debian : téléchargement (%ld Mo)", total >> 20);
    for (int i = 0; i < 2; i++) {
        char part[64], dst[64], url[160];
        snprintf(part, sizeof part, "/.titan/%s.part", names[i]);
        snprintf(dst, sizeof dst, "/.titan/%s", names[i]);
        snprintf(url, sizeof url, "https://github.com/" REPO "/releases/latest/download/%s", names[i]);
        char *get[] = { "/usr/bin/curl", "-fsSL", "--retry", "3", "--max-time", "7200", "-o", part, url, NULL };
        int pid = sys_fork();
        if (pid == 0) {
            int nul = sys_open("/dev/null", O_RDWR, 0);
            if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 1); sys_dup2(nul, 2); }
            sys_execve(get[0], get, g_envp);
            sys_exit(127);
        }
        int st = -1;
        for (;;) {
            if (pid <= 0 || sys_wait4(pid, &st, WNOHANG_) == pid) break;
            long got = file_size(part);
            if (got < 0) got = 0;
            if (total > 0) say(UPD_DOWNLOADING, (int)MIN(99, (before + got) * 100 / total), "Téléchargement de la base Debian… %ld Mo sur %ld",
                               (before + got) >> 20, total >> 20);
            else say(UPD_DOWNLOADING, -1, "Téléchargement de la base Debian… %ld Mo", (before + got) >> 20);
            sleep_ms(500);
        }
        if (st != 0 || (size[i] > 0 && file_size(part) != size[i]) || sys_rename(part, dst) != 0) {
            sys_unlink(part);
            klog("base Debian : téléchargement de %s interrompu", names[i]);
            say(quiet ? UPD_IDLE : UPD_ERROR, -1, "Le téléchargement de la base Debian a échoué : il reprendra plus tard.");
            return;
        }
        before += size[i];
    }
    sys_sync();
    klog("base Debian : téléchargée");
    say(UPD_DONE, 100, "Base Debian téléchargée : redémarrez pour l'installer (quelques minutes, une seule fois).");
}

/* Debian : mise à jour des programmes (apt), avec l'avancement d'apt (APT::Status-Fd) */
static void apt_upgrade(void) {
    say(UPD_INSTALLING, -1, "Mise à jour de la liste des programmes Debian…");
    char *upd[] = { "/usr/bin/apt-get", "-q", "-o", "DPkg::Lock::Timeout=120", "update", NULL };
    if (run(upd) != 0) { say(UPD_ERROR, -1, "Les dépôts Debian ne répondent pas : réessayez plus tard."); return; }
    int p[2];
    if (sys_pipe2(p, O_CLOEXEC) < 0) return;
    int pid = sys_fork();
    if (pid == 0) {
        int nul = sys_open("/dev/null", O_RDWR, 0);
        if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 2); }
        sys_dup2(p[1], 1);
        static char *env[] = { "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", "HOME=/root", "LANG=C.UTF-8",
                               "DEBIAN_FRONTEND=noninteractive", "APT_LISTCHANGES_FRONTEND=none", NULL };
        char *argv[] = { "/usr/bin/apt-get", "-y", "-q", "-o", "APT::Status-Fd=1", "-o", "Dpkg::Use-Pty=0", "-o", "DPkg::Lock::Timeout=300",
                         "-o", "Dpkg::Options::=--force-confold", "full-upgrade", NULL };
        sys_execve(argv[0], argv, env);
        sys_exit(127);
    }
    sys_close(p[1]);
    char buf[2048];
    int len = 0, pkgs = 0;
    char lastpk[64] = "";
    for (;;) {
        long r = pid > 0 ? sys_read(p[0], buf + len, sizeof buf - 1 - (size_t)len) : 0;
        if (r <= 0) break;
        len += (int)r;
        buf[len] = 0;
        char *nl;
        while ((nl = strchr(buf, '\n'))) {
            *nl = 0;
            bool dl = str_starts(buf, "dlstatus:"), pm = str_starts(buf, "pmstatus:");
            if (dl || pm) {
                char pk[64];
                int k = 0;
                for (const char *q = buf + 9; *q && *q != ':' && k < 63; q++) pk[k++] = *q;
                pk[k] = 0;
                if (pm && strcmp(pk, lastpk)) { pkgs++; strlcpy(lastpk, pk, sizeof lastpk); }
                const char *a = strchr(buf + 9, ':');
                int pc = a ? atoi(a + 1) : 0;
                say(UPD_INSTALLING, dl ? pc / 2 : 50 + pc / 2, dl ? "Téléchargement des mises à jour Debian… %s" : "Installation des mises à jour Debian… %s", pk);
            }
            int rest = len - (int)(nl + 1 - buf);
            memmove(buf, nl + 1, (size_t)rest + 1);
            len = rest;
        }
        if (len >= (int)sizeof buf - 1) len = 0;
    }
    sys_close(p[0]);
    int st = -1;
    if (pid > 0) sys_wait4(pid, &st, 0);
    char *clean[] = { "/usr/bin/apt-get", "clean", NULL };
    run(clean);
    sys_sync();
    if (st != 0) { say(UPD_ERROR, -1, "La mise à jour des programmes Debian a échoué (Terminal : sudo apt full-upgrade)."); return; }
    klog("mise à jour : programmes Debian à jour (%d paquets)", pkgs / 2);
    if (pkgs) say(UPD_DONE, 100, "TitanOs est à jour, et les programmes Debian ont été mis à jour.");
    else say(UPD_UPTODATE, -1, "TitanOs et les programmes Debian sont à jour (" TITANOS_VERSION ").");
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
    if (build <= TITANOS_BUILD) {
        if (debian_missing()) { debian_fetch(quiet); return; }
        if (g_hosted && !quiet) { apt_upgrade(); return; }
        say(UPD_UPTODATE, -1, "TitanOs est à jour (" TITANOS_VERSION ").");
        return;
    }
    klog("mise à jour : bêta %d disponible", build);
    /* taille du paquet, pour une barre de progression exacte */
    long total = 0;
    char *head[] = { "/bin/busybox", "sh", "-c", "/usr/bin/curl -fsSIL --max-time 20 "
                     "'https://github.com/" REPO "/releases/latest/download/titanos-maj.tar.gz' | tr -d '\\r' | "
                     "grep -i '^content-length:' | tail -n 1 | cut -d' ' -f2 > " WORK "/taille", NULL };
    if (run(head) == 0) {
        char t[32] = "";
        if (read_file(WORK "/taille", t, sizeof t) > 0) total = strtol(t, NULL, 10);
    }
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
        if (got < 0) got = 0;
        if (total > 0) say(UPD_DOWNLOADING, (int)MIN(99, got * 100 / total), "Téléchargement de la bêta %d… %ld Mo sur %ld",
                           build, got >> 20, (total + (1 << 19)) >> 20);
        else say(UPD_DOWNLOADING, -1, "Téléchargement de la bêta %d… %ld Mo", build, got >> 20);
        sleep_ms(250);
    }
    if (st != 0) { say(quiet ? UPD_IDLE : UPD_ERROR, -1, "Le téléchargement a échoué."); return; }
    install_package(WORK "/maj.tar.gz", quiet);
    if (upd_ok && debian_missing()) debian_fetch(quiet);     /* la base Debian, dans la foulée */
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

static bool copy_into(const char *from, const char *to, uint64_t *done, uint64_t total);

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
    /* la base Debian, pour un TitanOs qui ne l'a pas encore (installée à son prochain démarrage) */
    static const char *const deb[] = { "TITANOS/DEBIAN.TXZ", "TITANOS/DEBS.TAR" };
    struct { long type, bsize; unsigned long blocks, bfree, bavail, files, ffree; int fsid[2]; long namelen, frsize, flags, spare[4]; } fs;
    uint64_t need = 0, done = 0;
    for (int i = 0; i < 2; i++) { char q[96]; snprintf(q, sizeof q, "/media/titanos/%s", deb[i]); long n = file_size(q); need += n > 0 ? (uint64_t)n : 0; }
    if (ok && need && sc2(__NR_statfs, (long)"/run/cible", (long)&fs) == 0 && fs.bavail * (unsigned long)fs.bsize > need + (64ull << 20))
        for (int i = 0; i < 2; i++) {
            char a[96], b[96];
            snprintf(a, sizeof a, "/media/titanos/%s", deb[i]);
            snprintf(b, sizeof b, "/run/cible/%s", deb[i]);
            if (file_size(b) != file_size(a)) copy_into(a, b, &done, need);
        }
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

static void part_of(const char *disk, int num, char *out, int n) {
    int l = (int)strlen(disk);
    bool digit = l && disk[l - 1] >= '0' && disk[l - 1] <= '9';      /* nvme0n1p1, mmcblk0p1 */
    snprintf(out, (size_t)n, "%s%s%d", disk, digit ? "p" : "", num);
}

/* copie d'un fichier de la clé vers le disque ; done/total : avancement global */
static bool copy_into(const char *from, const char *to, uint64_t *done, uint64_t total) {
    int in = sys_open(from, O_RDONLY | O_CLOEXEC, 0);
    if (in < 0) return false;
    char dir[200];
    strlcpy(dir, to, sizeof dir);
    char *sl = strrchr(dir, '/');
    if (sl) { *sl = 0; mkdirs(dir); }
    int out = sys_open(to, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    static uint8_t buf[1 << 20];
    bool ok = out >= 0;
    while (ok) {
        long r = sys_read(in, buf, sizeof buf);
        if (r < 0) ok = false;
        if (r <= 0) break;
        ok = write_all(out, buf, r);
        *done += (uint64_t)r;
        say(UPD_INSTALLING, 12 + (int)(*done * 84 / (total + 1)), "Copie de TitanOs sur le disque… %d / %d Mo", (int)(*done >> 20), (int)(total >> 20));
    }
    if (out >= 0) { sc1(__NR_fsync, out); sys_close(out); }
    sys_close(in);
    return ok;
}

/* Installation sur un disque, depuis la clé d'installation : le disque est entièrement
 * repartitionné. Partition 1 (FAT32, « TITANOS ») : le système et les fichiers de
 * l'utilisateur ; le reste (un tiers du disque, entre 8 et 64 Go) devient l'espace des
 * applications au premier démarrage, et Debian s'y installe (archives copiées ici). */
static void install_main(void *arg) {
    (void)arg;
    char boot[32], src[48], dst[48], part[32], pdev[48], p[96], v[32];
    if (!boot_disk(boot, sizeof boot)) { say(UPD_ERROR, -1, "Clé TitanOs introuvable."); return; }
    if (!strcmp(boot, install_target) || disk_mounted(install_target)) { say(UPD_ERROR, -1, "Ce disque est utilisé : impossible d'y installer TitanOs."); return; }
    snprintf(src, sizeof src, "/dev/%s", boot);
    snprintf(dst, sizeof dst, "/dev/%s", install_target);
    snprintf(p, sizeof p, "/sys/block/%s/size", install_target);
    uint64_t sectors = read_file(p, v, sizeof v) > 0 ? (uint64_t)strtol(v, NULL, 10) : 0;
    if (sectors < (16ull << 30) / 512) { say(UPD_ERROR, -1, "Il faut un disque d'au moins 16 Go."); return; }
    /* partage : les applications (Debian) un tiers, entre 8 et 64 Go ; vos fichiers le reste */
    uint64_t apps = sectors / 3;
    apps = MAX(apps, (8ull << 30) / 512);
    apps = MIN(apps, (64ull << 30) / 512);
    uint64_t p1 = (sectors - 2048 - apps) & ~2047ull;
    if (p1 > 0xFFFFF000ull) p1 = 0xFFFFF000ull & ~2047ull;      /* limite des tables MBR (2 To) */

    say(UPD_INSTALLING, 1, "Préparation du disque…");
    sys_sync();
    uint8_t kmbr[512];
    static uint8_t s2[63 * 512];
    int in = sys_open(src, O_RDONLY | O_CLOEXEC, 0);
    bool ok = in >= 0 && sys_read(in, kmbr, 512) == 512 && kmbr[510] == 0x55 && sys_read(in, s2, sizeof s2) == (long)sizeof s2 &&
              !memcmp(s2 + 4, "TITN", 4);
    if (in >= 0) sys_close(in);
    if (!ok) { say(UPD_ERROR, -1, "Lecture du chargeur de la clé impossible."); return; }
    int out = sys_open(dst, O_RDWR | O_CLOEXEC, 0);
    if (out < 0) { say(UPD_ERROR, -1, "Ouverture du disque impossible."); return; }
    /* anciennes tables de partitions effacées (MBR et GPT, au début et à la fin du disque) */
    static uint8_t zero[1 << 20];
    ok = sys_lseek(out, 0, 0) == 0 && write_all(out, zero, sizeof zero) &&
         sys_lseek(out, (long)(sectors * 512 - sizeof zero), 0) >= 0 && write_all(out, zero, sizeof zero);
    uint8_t mbr[512];
    memset(mbr, 0, sizeof mbr);
    memcpy(mbr, kmbr, 440);
    uint32_t sig = 0;
    sc3(__NR_getrandom, (long)&sig, 4, 0);
    if (!sig) sig = (uint32_t)plat_ms() * 2654435761u;
    memcpy(mbr + 440, &sig, 4);
    uint8_t *e = mbr + 446;
    e[0] = 0x80;                                                      /* partition de démarrage */
    e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF; e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
    e[4] = 0x0C;                                                      /* FAT32 (LBA) */
    put32(e + 8, 2048);
    put32(e + 12, (uint32_t)p1);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    ok = ok && sys_lseek(out, 0, 0) == 0 && write_all(out, mbr, 512);
    sc1(__NR_fsync, out);
    sys_ioctl(out, 0x125F /* BLKRRPART */, NULL);
    sys_close(out);
    if (!ok) { say(UPD_ERROR, -1, "Écriture sur le disque impossible."); return; }
    part_of(install_target, 1, part, sizeof part);
    snprintf(pdev, sizeof pdev, "/dev/%s", part);
    for (int i = 0; i < 50 && !path_exists(pdev); i++) sleep_ms(100);
    klog("installation : %s, %d Go pour TitanOs et vos fichiers, %d Go pour les applications",
         dst, (int)(p1 >> 21), (int)((sectors - 2048 - p1) >> 21));

    say(UPD_INSTALLING, 4, "Formatage du disque…");
    char *mkfs[] = { "/bin/busybox", "mkfs.vfat", "-n", "TITANOS", pdev, NULL };
    char *mkfs_deb[] = { "/usr/sbin/mkfs.fat", "-F", "32", "-n", "TITANOS", pdev, NULL };      /* dans Debian (dosfstools) */
    if (!path_exists(pdev) || run(path_exists("/usr/sbin/mkfs.fat") ? mkfs_deb : mkfs) != 0) { say(UPD_ERROR, -1, "Le formatage du disque a échoué."); return; }
    mkdirs("/run/cible");
    if (sys_mount(pdev, "/run/cible", "vfat", 1024 /* MS_NOATIME */, "iocharset=utf8,shortname=mixed") != 0) {
        say(UPD_ERROR, -1, "Le disque formaté ne s'ouvre pas.");
        return;
    }
    /* copie : d'abord les fichiers du système, dans l'ordre, sur le disque encore vide (chacun
     * d'un seul tenant), puis la base Debian */
    static const char *const extra[] = { "TITANOS/VERSION", "LISEZMOI.TXT", "TITANOS/DEBIAN.TXZ", "TITANOS/DEBS.TAR" };
    const char *files[NSYS + 4];
    int nf = 0;
    for (int i = 0; i < NSYS; i++) files[nf++] = SYSFILES[i].path;
    for (int i = 0; i < 4; i++) files[nf++] = extra[i];
    uint64_t total = 0, done = 0;
    for (int i = 0; i < nf; i++) {
        snprintf(p, sizeof p, "/media/titanos/%s", files[i]);
        long n = file_size(p);
        if (n > 0) total += (uint64_t)n;
    }
    for (int i = 0; ok && i < nf; i++) {
        char from[160], to[160];
        snprintf(from, sizeof from, "/media/titanos/%s", files[i]);
        snprintf(to, sizeof to, "/run/cible/%s", files[i]);
        if (!path_exists(from)) { ok = i >= NSYS; continue; }         /* les fichiers du système sont obligatoires */
        ok = copy_into(from, to, &done, total);
        if (!ok) klog("installation : copie de %s échouée", files[i]);
    }
    sys_sync();
    /* le chargeur de démarrage, avec l'emplacement des fichiers sur ce disque */
    if (ok) {
        say(UPD_INSTALLING, 97, "Installation du chargeur de démarrage…");
        ok = place_stage2(s2, sizeof s2, "/run/cible", "/run/cible/TITANOS/VMLINUZ");
        if (!ok) klog("installation : fichiers du système éparpillés sur le disque");
    }
    sys_umount2("/run/cible", 0);
    sys_sync();
    if (ok) {
        out = sys_open(dst, O_RDWR | O_CLOEXEC, 0);
        ok = out >= 0 && sys_lseek(out, 512, 0) == 512 && write_all(out, s2, sizeof s2);
        if (out >= 0) { sc1(__NR_fsync, out); sys_close(out); }
    }
    if (!ok) { say(UPD_ERROR, -1, "L'installation n'a pas abouti : le disque n'est pas utilisable en l'état. Réessayez."); return; }
    klog("installation terminée sur %s", dst);
    say(UPD_DONE, 100, "TitanOs est installé. Retirez la clé, puis redémarrez : le premier démarrage installe Debian (quelques minutes).");
}

bool plat_install_start(const char *dev) {
    if (upd.pid > 0 && plat_update_state(NULL, 0, NULL) == UPD_INSTALLING) return false;
    strlcpy(install_target, dev, sizeof install_target);
    return job_start(&ins, install_main, NULL);
}

int plat_install_state(char *msg, int n, int *percent) { return job_state(&ins, msg, n, percent); }
