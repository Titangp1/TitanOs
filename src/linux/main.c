/* TitanOs - point d'entrée : /init (PID 1) et session graphique
 *
 * Le noyau Linux démarre ce programme en premier (c'est le /init de l'initramfs).
 *   - init    : monte /proc, /sys, /dev..., charge les pilotes des périphériques branchés
 *               à chaud, ramasse les processus terminés et relance la session si besoin ;
 *   - session : écran de démarrage animé pendant qu'un processus « démarrage » charge les
 *               pilotes et prépare la clé ; puis le son, le Wi-Fi, le réseau et le bureau.
 * Paramètre du noyau « titanos.shell » : ouvre directement un shell texte (dépannage). */
#include "linux.h"
#include "../core/desk.h"
#include <linux/kd.h>

__asm__(".text\n.global _start\n_start:\n  xor %ebp, %ebp\n  mov %rsp, %rdi\n  and $-16, %rsp\n  call start_c\n  hlt\n");

/* ---------------- utilitaires ---------------- */
static void quiet_child(void) {
    sys_setsid();
    int nul = sys_open("/dev/null", O_RDWR, 0);
    if (nul >= 0) { sys_dup2(nul, 0); sys_dup2(nul, 1); sys_dup2(nul, 2); }
}

static void spawn_quiet(char *const *argv) {
    int pid = sys_fork();
    if (pid != 0) return;
    quiet_child();
    sys_execve(argv[0], argv, g_envp);
    sys_exit(127);
}

/* lance une commande et attend sa fin ; renvoie son code de sortie (ou -1) */
static int run_wait(char *const *argv) {
    int pid = sys_fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        quiet_child();
        sys_execve(argv[0], argv, g_envp);
        sys_exit(127);
    }
    int st = 0;
    if (sys_wait4(pid, &st, 0) < 0) return -1;
    return (st & 0x7F) ? -1 : (st >> 8) & 0xFF;
}

static void iface_up(const char *name) {
    int s = sys_socket(2, 2 | O_CLOEXEC, 0);
    if (s < 0) return;
    struct { char name[16]; short flags; char pad[22]; } ifr = { 0 };
    strlcpy(ifr.name, name, sizeof ifr.name);
    if (sys_ioctl(s, 0x8913 /* SIOCGIFFLAGS */, &ifr) == 0) {
        ifr.flags |= 1 /* IFF_UP */;
        sys_ioctl(s, 0x8914 /* SIOCSIFFLAGS */, &ifr);
    }
    sys_close(s);
}

static bool cmdline_has(const char *word) {
    char buf[1024];
    read_file("/proc/cmdline", buf, sizeof buf);
    return strstr(buf, word) != NULL;
}

/* ================================================================== */
/* Réseau automatique                                                   */
/* ================================================================== */
static char started_ifs[16][16];
static int nstarted;

/* adresse automatique (DHCP) sur chaque carte réseau nouvellement apparue ;
 * pour le Wi-Fi, udhcpc attend simplement que la connexion soit établie */
static void net_cb(const char *name, int type, void *ctx) {
    (void)type; (void)ctx;
    if (!strcmp(name, "lo") || str_starts(name, "sit") || str_starts(name, "p2p")) return;
    for (int i = 0; i < nstarted; i++) if (!strcmp(started_ifs[i], name)) return;
    if (nstarted >= 16) return;
    strlcpy(started_ifs[nstarted++], name, 16);
    iface_up(name);
    char ifn[16];
    strlcpy(ifn, name, sizeof ifn);
    char pidf[48];
    snprintf(pidf, sizeof pidf, "/run/udhcpc.%s.pid", ifn);
    char *argv[] = { "/bin/busybox", "udhcpc", "-i", ifn, "-b", "-t", "4", "-T", "3", "-A", "5", "-p", pidf,
                     "-s", "/etc/udhcpc.script", NULL };
    spawn_quiet(argv);
    klog("réseau : DHCP lancé sur %s", name);
}
static void net_autostart(void) { for_each_dir("/sys/class/net", net_cb, NULL); }

/* ================================================================== */
/* La clé TITANOS : fichiers (FAT32) et applications Linux (ext4)       */
/* ================================================================== */
static bool is_titanos_volume(const char *dev) {
    char p[64];
    snprintf(p, sizeof p, "/dev/%s", dev);
    int fd = sys_open(p, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return false;
    uint8_t s[512];
    long n = sys_read(fd, s, sizeof s);
    sys_close(fd);
    if (n != 512 || s[510] != 0x55 || s[511] != 0xAA) return false;
    return !memcmp(s + 71, "TITANOS    ", 11) || !memcmp(s + 43, "TITANOS    ", 11);
}

static bool disk_of(const char *part, char *disk, int n);

/* signature du disque qui a démarré, donnée par le chargeur BIOS (titanos.disk=xxxxxxxx) */
static const char *boot_sig(void) {
    static char sig[16];
    static bool done;
    if (!done) {
        done = true;
        char cl[1024];
        read_file("/proc/cmdline", cl, sizeof cl);
        char *p = strstr(cl, "titanos.disk=");
        if (p) { strlcpy(sig, p + 13, 9); if (strlen(sig) != 8) sig[0] = 0; }
    }
    return sig;
}

static bool disk_has_sig(const char *part, const char *sig) {
    char disk[32], p[48];
    if (!disk_of(part, disk, sizeof disk)) return false;
    snprintf(p, sizeof p, "/dev/%s", disk);
    int fd = sys_open(p, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return false;
    uint8_t s[512];
    long n = sys_read(fd, s, sizeof s);
    sys_close(fd);
    if (n != 512) return false;
    char hex[12];
    snprintf(hex, sizeof hex, "%08x", (unsigned)(s[440] | s[441] << 8 | s[442] << 16 | (unsigned)s[443] << 24));
    return !strcmp(hex, sig);
}

static char key_dev[32];
static char other_dev[32];            /* un autre TitanOs (pas celui qui a démarré) */
static void block_cb(const char *name, int type, void *ctx) {
    (void)type; (void)ctx;
    if (key_dev[0] || str_starts(name, "loop") || str_starts(name, "ram") || str_starts(name, "zram")) return;
    if (!is_titanos_volume(name)) return;
    const char *sig = boot_sig();
    if (sig[0] && !disk_has_sig(name, sig)) { strlcpy(other_dev, name, sizeof other_dev); return; }
    strlcpy(key_dev, name, sizeof key_dev);
}

static int mount_tries;
static bool mount_key(void) {
    mount_tries++;
    key_dev[0] = other_dev[0] = 0;
    for_each_dir("/sys/class/block", block_cb, NULL);
    /* démarrage UEFI (pas de signature transmise) ou signature introuvable : la copie trouvée */
    /* la clé USB peut mettre quelques secondes à apparaître : on ne se rabat sur une autre copie
     * de TitanOs (le disque interne) qu'après l'avoir attendue */
    if (!key_dev[0] && other_dev[0] && (!boot_sig()[0] || mount_tries > 32)) strlcpy(key_dev, other_dev, sizeof key_dev);
    if (!key_dev[0]) return false;
    module_load("nls_cp437");
    module_load("nls_utf8");
    module_load("nls_iso8859_1");
    module_load("vfat");
    mkdirs("/media/titanos");
    char dev[64];
    snprintf(dev, sizeof dev, "/dev/%s", key_dev);
    int r = sys_mount(dev, "/media/titanos", "vfat", 1024 /* MS_NOATIME */, "iocharset=utf8,shortname=mixed,flush");
    if (r != 0) r = sys_mount(dev, "/media/titanos", "vfat", 1024, "");
    klog("clé TITANOS (%s) : %s", key_dev, r == 0 ? "montée" : "échec du montage");
    return r == 0;
}

/* disque qui porte une partition : /sys/class/block/sdb1 -> .../block/sdb/sdb1 */
static bool disk_of(const char *part, char *disk, int n) {
    char p[96], link[256];
    snprintf(p, sizeof p, "/sys/class/block/%s", part);
    long r = sys_readlink(p, link, sizeof link - 1);
    if (r <= 0) return false;
    link[r] = 0;
    char *last = strrchr(link, '/');
    if (!last) return false;
    *last = 0;
    char *prev = strrchr(link, '/');
    strlcpy(disk, prev ? prev + 1 : link, (size_t)n);
    return strcmp(disk, "block") != 0;
}

static void part_name(const char *disk, int num, char *out, int n) {
    int l = (int)strlen(disk);
    bool digit = l && disk[l - 1] >= '0' && disk[l - 1] <= '9';      /* mmcblk0p2, nvme0n1p2 */
    snprintf(out, (size_t)n, "%s%s%d", disk, digit ? "p" : "", num);
}

struct blkpg_part { long long start, length; int pno; char devname[64], volname[64]; };
struct blkpg_arg { int op, flags, datalen; void *data; };

/* Crée, une seule fois, une 2e partition (ext4) dans l'espace libre de la clé, après la
 * partition FAT32. Ne touche à rien si la table de partitions n'est pas exactement celle
 * de l'image TitanOs : une seule partition FAT32 qui commence au secteur 2048. */
static bool create_apps_partition(const char *disk, void (*say)(const char *)) {
    char dev[48], p[96], v[32];
    snprintf(dev, sizeof dev, "/dev/%s", disk);
    snprintf(p, sizeof p, "/sys/class/block/%s/size", disk);
    if (read_file(p, v, sizeof v) <= 0) return false;
    uint64_t total = (uint64_t)strtol(v, NULL, 10);
    int fd = sys_open(dev, O_RDWR | O_CLOEXEC, 0);
    if (fd < 0) return false;
    uint8_t mbr[512];
    bool okay = sys_read(fd, mbr, 512) == 512 && mbr[510] == 0x55 && mbr[511] == 0xAA;
    uint32_t s1 = 0, n1 = 0;
    if (okay) {
        memcpy(&s1, mbr + 446 + 8, 4);
        memcpy(&n1, mbr + 446 + 12, 4);
        okay = mbr[446 + 4] == 0x0C && s1 == 2048;
        for (int i = 1; i < 4 && okay; i++) if (mbr[446 + 16 * i + 4]) okay = false;
    }
    uint64_t start = ((uint64_t)s1 + n1 + 2047) & ~2047ull;
    uint64_t size = total > start + 4096 ? ((total - start - 2048) & ~2047ull) : 0;
    if (!okay || size < 2ull * 1024 * 1024) { sys_close(fd); return false; }   /* il faut au moins 1 Go libre */
    say("Préparation de la clé pour les applications (une seule fois)…");
    uint8_t *e = mbr + 446 + 16;
    memset(e, 0, 16);
    e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF; e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;   /* au-delà de la géométrie CHS */
    e[4] = 0x83;                                                                     /* Linux */
    uint32_t st32 = (uint32_t)start, sz32 = (uint32_t)MIN(size, 0xFFFFFFFFull);
    memcpy(e + 8, &st32, 4);
    memcpy(e + 12, &sz32, 4);
    sys_lseek(fd, 0, 0);
    if (sys_write(fd, mbr, 512) != 512) { sys_close(fd); return false; }
    sc1(__NR_fsync, fd);
    struct blkpg_part bp = { (long long)start * 512, (long long)sz32 * 512, 2, { 0 }, { 0 } };
    struct blkpg_arg ba = { 1 /* BLKPG_ADD_PARTITION */, 0, sizeof bp, &bp };
    sys_ioctl(fd, 0x1269 /* BLKPG */, &ba);
    sys_close(fd);
    char part[32], pdev[48];
    part_name(disk, 2, part, sizeof part);
    snprintf(pdev, sizeof pdev, "/dev/%s", part);
    for (int i = 0; i < 50 && !path_exists(pdev); i++) sleep_ms(100);
    say("Formatage de l'espace des applications…");
    char *argv[] = { "/sbin/mke2fs", "-q", "-F", "-t", "ext4", "-L", "TITANOS-APPS",
                     "-E", "lazy_itable_init=1,lazy_journal_init=1", pdev, NULL };
    int r = run_wait(argv);
    klog("partition des applications %s : %s", pdev, r == 0 ? "créée" : "échec du formatage");
    return r == 0;
}

/* Les applications installées (apk) vont dans la partition ext4 : elle est posée par-dessus
 * /usr, /lib, /etc... (overlayfs). Le système de base reste intact en dessous. */
static bool setup_apps(void (*say)(const char *)) {
    char disk[32], part[32], pdev[48];
    if (!key_dev[0] || !disk_of(key_dev, disk, sizeof disk)) return false;
    part_name(disk, 2, part, sizeof part);
    snprintf(pdev, sizeof pdev, "/dev/%s", part);
    module_load("ext4");
    module_load("overlay");
    if (!path_exists(pdev) && !create_apps_partition(disk, say)) return false;
    char *fsck[] = { "/sbin/e2fsck", "-p", pdev, NULL };
    run_wait(fsck);
    mkdirs("/.titan");
    if (sys_mount(pdev, "/.titan", "ext4", 1024 /* MS_NOATIME */, "") != 0) { klog("partition des applications : montage impossible"); return false; }
    static const char *const dirs[] = { "usr", "bin", "sbin", "lib", "etc", "var", "root" };
    int okn = 0;
    for (int i = 0; i < countof(dirs); i++) {
        char up[64], wk[64], tgt[16], opts[200];
        snprintf(up, sizeof up, "/.titan/upper/%s", dirs[i]);
        snprintf(wk, sizeof wk, "/.titan/work/%s", dirs[i]);
        snprintf(tgt, sizeof tgt, "/%s", dirs[i]);
        mkdirs(up);
        mkdirs(wk);
        snprintf(opts, sizeof opts, "lowerdir=%s,upperdir=%s,workdir=%s", tgt, up, wk);
        if (sys_mount("overlay", tgt, "overlay", 0, opts) == 0) okn++;
    }
    klog("applications Linux : %d/%d dossiers sur la clé", okn, countof(dirs));
    return okn == countof(dirs);
}

/* ================================================================== */
/* Démarrage : processus séparé, pendant que l'écran s'anime            */
/* ================================================================== */
static int boot_out = -1;
static void say(const char *s) {
    char m[160];
    int n = snprintf(m, sizeof m, "S%s\n", s);
    sys_write(boot_out, m, (size_t)n);
}
static void tell(const char *s) { sys_write(boot_out, s, strlen(s)); }

/* Journal de diagnostic, copié sur la clé toutes les 5 s pendant les 4 premières minutes :
 * messages du noyau, pilotes chargés, périphériques d'entrée et, pour chaque processus
 * TitanOs, l'endroit où il attend dans le noyau. Il ne dépend pas du bureau : même si
 * celui-ci se bloque, le journal continue. */
static void start_journal(void) {
    static const char script[] =
        "J=/media/titanos/journal-titanos.txt; i=0; while :; do {"
        " echo \"TitanOs " TITANOS_VERSION " - journal, $(cut -d' ' -f1 /proc/uptime) s apres le demarrage\";"
        " echo; echo '== processus (pid nom etat attente)';"
        " for p in /proc/[0-9]*; do echo \"${p#/proc/} $(cat $p/comm) $(cut -d' ' -f3 $p/stat) $(cat $p/wchan)\"; done;"
        " echo; echo '== piles des processus TitanOs';"
        " for p in /proc/[0-9]*; do [ \"$(cat $p/comm)\" = init ] && { echo \"-- ${p#/proc/}\"; cat $p/stack; }; done;"
        " echo; echo '== ligne de commande'; cat /proc/cmdline;"
        " echo; echo '== entrees'; ls -l /dev/input /dev/dri; cat /proc/bus/input/devices;"
        " echo; echo '== pilotes'; cut -d' ' -f1 /proc/modules | tr '\\n' ' '; echo;"
        " echo; echo '== blocs'; cat /proc/partitions; echo; echo '== montages'; cat /proc/mounts;"
        " echo; echo '== noyau'; dmesg;"
        " } > $J.tmp 2>&1; mv -f $J.tmp $J; sync; i=$((i+1)); [ $i -lt 48 ] && sleep 5 || sleep 300; done";
    char *argv[] = { "/bin/busybox", "sh", "-c", (char *)script, NULL };
    spawn_quiet(argv);
}

/* où TitanOs a démarré, en mots : clé USB ou disque interne (avec son modèle) */
static void storage_label(char *out, int n) {
    char disk[32], p[96], v[64], link[256];
    strlcpy(out, "Clé USB TITANOS", (size_t)n);
    if (!key_dev[0] || !disk_of(key_dev, disk, sizeof disk)) return;
    snprintf(p, sizeof p, "/sys/block/%s/removable", disk);
    bool usb = read_file(p, v, sizeof v) > 0 && v[0] == '1';
    snprintf(p, sizeof p, "/sys/block/%s", disk);
    long r = sys_readlink(p, link, sizeof link - 1);
    if (r > 0) { link[r] = 0; if (strstr(link, "/usb")) usb = true; }
    if (usb) return;
    snprintf(p, sizeof p, "/sys/block/%s/device/model", disk);
    if (read_file(p, v, sizeof v) > 0 && trim(v)[0]) snprintf(out, (size_t)n, "Disque interne (%s)", trim(v));
    else strlcpy(out, "Disque interne", (size_t)n);
}

static void boot_worker(int out) {
    boot_out = out;
    say("Démarrage du noyau Linux…");
    if (modules_coldplug(true) > 0) tell("G\n");             /* carte graphique d'abord */
    say("Pilotes des périphériques…");
    module_load("evdev");
    module_load("psmouse");
    uint64_t t0 = plat_ms();
    int quiet = 0;
    while (plat_ms() - t0 < 8000) {
        quiet = modules_coldplug(false) ? 0 : quiet + 1;
        if (quiet >= 3 && plat_ms() - t0 > 1500) break;
        sleep_ms(250);
    }
    tell("G\n");
    say("Recherche de la clé TitanOs…");
    bool on_key = false;
    for (int i = 0; i < 40 && !on_key; i++) {      /* une clé USB peut mettre quelques secondes à apparaître */
        on_key = mount_key();
        if (!on_key) { modules_coldplug(false); sleep_ms(250); }
    }
    if (on_key) {
        start_journal();
        say("Ouverture de vos fichiers…");
        bool apps = setup_apps(say);
        char m[160];
        char where[96];
        storage_label(where, sizeof where);
        snprintf(m, sizeof m, "K%s%s\n", where, apps ? " · avec les applications" : "");
        tell(m);
    }
    say("Ouverture de la session…");
    tell("D\n");
}

/* ligne de diagnostic affichée en bas de l'écran de verrouillage, seulement si la clé,
 * le clavier ou le pointeur manquent : si l'heure
 * s'y arrête, le bureau est bloqué ; si « événements » reste à 0, le clavier et la
 * souris n'arrivent pas jusqu'à TitanOs */
static const char *diag_key = "recherche";
void plat_diag(char *b, int n) {
    b[0] = 0;
    if (!strcmp(diag_key, "montée") && input_ok()) return;      /* tout va bien : rien à afficher */
    static char mods[32768];
    int nm = 0, len = read_file("/proc/modules", mods, sizeof mods);
    for (int i = 0; i < len; i++) if (mods[i] == '\n') nm++;
    char in[96];
    input_describe(in, sizeof in);
    uint64_t s = plat_ms() / 1000;
    snprintf(b, (size_t)n, "Diagnostic · pilotes : %d · clé : %s · entrées : %s · événements : %d · %d:%02d",
             nm, diag_key, in, input_events, (int)(s / 60), (int)(s % 60));
}

static void prepare_home(bool on_key, const char *storage) {
    const char *home = on_key ? "/media/titanos/Utilisateur" : "/root/Utilisateur";
    plat_set_storage(home, on_key ? "/media/titanos/titanos.cfg" : "/root/titanos.cfg",
                     on_key ? storage : "Mémoire vive (non conservé)");
    bool first = !path_exists(home);
    static const char *const sub[] = { "Documents", "Images", "Musique", "Téléchargements" };
    mkdirs(home);
    for (int i = 0; i < 4; i++) {
        char p[256];
        snprintf(p, sizeof p, "%s/%s", home, sub[i]);
        mkdirs(p);
    }
    if (first) {
        char p[256];
        snprintf(p, sizeof p, "%s/Bienvenue.txt", home);
        const char *t = "Bienvenue dans TitanOs (bêta 24) !\n\n"
                        "TitanOs tourne sur le noyau Linux : Linux fournit les pilotes (écran, clavier,\n"
                        "USB, Wi-Fi, son...), et tout le reste est écrit pour TitanOs.\n\n"
                        "Vos fichiers sont gardés sur la clé USB, dans le dossier Utilisateur.\n"
                        "Dans le Terminal : « aide » liste les commandes, et « apk add nom »\n"
                        "installe une application Linux (par exemple : apk add htop).\n";
        plat_writefile(p, t, strlen(t));
    }
}

static void upgrade_display(void) {
    if (!display_better_available()) return;
    sleep_ms(120);
    if (display_open()) {
        core_resize(display_w(), display_h());
        input_screen_w = display_w();
        input_screen_h = display_h();
    }
}

/* ================================================================== */
/* Plantages : la cause est écrite dans le journal du noyau              */
/* ================================================================== */
/* En cas d'erreur grave (mauvaise adresse mémoire...), on note le signal, l'adresse fautive
 * et la chaîne des appels (adresses dans le programme, à traduire avec titanos-init.debug). */
static void crash_handler(int sig, void *info, void *uctx) {
    const unsigned long *g = (const unsigned long *)((const char *)uctx + 40);   /* uc_mcontext.gregs */
    unsigned long rip = g[16], rsp = g[15], rbp = g[10];
    unsigned long addr = *(const unsigned long *)((const char *)info + 16);      /* si_addr */
    klog("PLANTAGE pid %d : signal %d, adresse %lx, rip %lx, rsp %lx", sys_getpid(), sig, addr, rip, rsp);
    char line[260];
    int n = snprintf(line, sizeof line, "appels :");
    for (int i = 0; i < 16 && rbp > rsp && rbp < rsp + (8ul << 20) && !(rbp & 7); i++) {
        const unsigned long *fr = (const unsigned long *)rbp;
        n += snprintf(line + n, sizeof line - (size_t)n, " %lx", fr[1]);
        if (fr[0] <= rbp) break;
        rbp = fr[0];
    }
    klog("%s", line);
    sys_exit(128 + sig);
}

static void crash_restorer(void) { sys_exit(126); }

static void crash_guard(void) {
    static uint8_t altstack[65536];
    struct { void *sp; int flags; int pad; size_t size; } ss = { altstack, 0, 0, sizeof altstack };
    sc2(__NR_sigaltstack, (long)&ss, 0);
    struct { void *handler; unsigned long flags; void *restorer; unsigned long mask; } sa = {
        (void *)crash_handler, 0x4 /* SA_SIGINFO */ | 0x08000000 /* SA_ONSTACK */ | 0x04000000 /* SA_RESTORER */,
        (void *)crash_restorer, 0 };
    static const int sigs[] = { 4 /* SIGILL */, 6 /* SIGABRT */, 7 /* SIGBUS */, 8 /* SIGFPE */, 11 /* SIGSEGV */ };
    for (int i = 0; i < countof(sigs); i++) sys_rt_sigaction(sigs[i], &sa, NULL);
}

/* Nettoyage au démarrage, en tâche de fond : restes de téléchargements et caches qui ne
 * servent plus. Les mises à jour de TitanOs réécrivent les fichiers du système à leur place
 * (tailles fixes) : aucune ancienne version ne s'accumule sur le disque. */
static void cleanup_start(void) {
    static const char script[] =
        "f=0; for d in /tmp/maj /var/cache/apk /root/.config/chromium/Crash\\ Reports/completed; do"
        " [ -d \"$d\" ] || continue; k=$(du -sk \"$d\" 2>/dev/null | cut -f1); f=$((f + ${k:-0})); rm -rf \"$d\"/* 2>/dev/null; done;"
        " rm -f /media/titanos/*.tmp /media/titanos/.titanos/*.tmp 2>/dev/null;"
        " echo \"<6>titanos: nettoyage : $((f / 1024)) Mo liberes\" > /dev/kmsg";
    char *argv[] = { "/bin/busybox", "sh", "-c", (char *)script, NULL };
    spawn_quiet(argv);
}

/* identifiant de la machine (demandé par D-Bus et Flatpak) */
static void machine_id(void) {
    if (path_exists("/etc/machine-id")) return;
    uint8_t r[16];
    plat_random(r, sizeof r);
    char hex[34];
    for (int i = 0; i < 16; i++) snprintf(hex + i * 2, 3, "%02x", r[i]);
    hex[32] = '\n';
    hex[33] = 0;
    write_file("/etc/machine-id", hex);
}

static int session_main(void) {
    crash_guard();
    /* la console texte du noyau ne doit plus dessiner ni lire le clavier */
    int tty = sys_open("/dev/tty1", O_RDWR | O_CLOEXEC | O_NOCTTY, 0);
    if (tty >= 0) {
        sys_ioctl(tty, KDSETMODE, (void *)(long)KD_GRAPHICS);
        sys_ioctl(tty, KDSKBMODE, (void *)(long)K_OFF);
    }
    modules_init();
    if (!display_open()) {
        modules_coldplug(true);              /* pas d'écran du BIOS : on attend la carte graphique */
        for (int i = 0; i < 50 && !display_open(); i++) sleep_ms(100);
    }
    if (display_w() <= 0) { klog("aucun écran utilisable"); return 1; }
    input_screen_w = display_w();
    input_screen_h = display_h();
    core_init(display_w(), display_h());
    /* démarrage BIOS sur l'écran provisoire (simpledrm, souvent pas à la bonne résolution) :
     * l'image du chargeur reste affichée jusqu'au vrai pilote de la carte graphique (8 s max) */
    bool hold = cmdline_has("titanos.boot=bios") && !strcmp(display_driver(), "simpledrm");
    uint64_t hold_until = plat_ms() + 8000;
    core_hold(hold);
    core_splash("Démarrage du noyau Linux…", 0);

    /* le travail de démarrage se fait à côté ; ici, les crêtes s'animent */
    int p[2];
    char storage[96] = "Clé USB TITANOS";
    bool on_key = false;
    if (sys_pipe2(p, O_CLOEXEC) == 0) {
        int pid = sys_fork();
        if (pid == 0) { sys_close(p[0]); boot_worker(p[1]); sys_exit(0); }
        sys_close(p[1]);
        char buf[512];
        int len = 0;
        bool done = pid < 0;
        while (!done) {
            struct pollfd pf = { p[0], POLLIN, 0 };
            if (sys_poll(&pf, 1, 16) > 0) {
                long r = sys_read(p[0], buf + len, sizeof buf - 1 - (size_t)len);
                if (r <= 0) done = true;
                else len += (int)r;
            }
            buf[len] = 0;
            char *nl;
            while ((nl = strchr(buf, '\n'))) {
                *nl = 0;
                if (buf[0] == 'S') core_splash(buf + 1, 0);
                else if (buf[0] == 'G') { upgrade_display(); if (strcmp(display_driver(), "simpledrm")) { hold = false; core_hold(false); } }
                else if (buf[0] == 'K') { on_key = true; strlcpy(storage, buf + 1, sizeof storage); }
                else if (buf[0] == 'D') done = true;
                int rest = len - (int)(nl + 1 - buf);
                memmove(buf, nl + 1, (size_t)rest + 1);
                len = rest;
            }
            if (hold && plat_ms() > hold_until) { hold = false; core_hold(false); }
            core_frame();
        }
        sys_close(p[0]);
        if (pid > 0) { int st; sys_wait4(pid, &st, 0); }
    }
    upgrade_display();
    core_hold(false);
    prepare_home(on_key, storage);
    settings_load();
    if (g_set.brightness >= 0) plat_brightness(g_set.brightness);
    audio_start();
    machine_id();
    hw_start();                              /* Wi-Fi et batterie : processus séparé */
    net_autostart();
    diag_key = on_key ? "montée" : "introuvable";
    module_load("evdev");                    /* au cas où le processus de démarrage aurait échoué */
    module_load("psmouse");
    input_scan();
    core_start();
    if (!on_key) wm_toast("Clé TITANOS introuvable : vos fichiers ne seront pas conservés.");

    /* périphériques branchés ou retirés : signalés par le noyau, plus besoin de tout
     * réexaminer toutes les secondes (un examen de secours reste fait toutes les 10 s) */
    int uev = uevent_open();
    cleanup_start();
    uint64_t next_scan = 0, next_net = 0, next_vol = 0, next_stat = plat_ms() + 10000;
    uint64_t st_frames = 0, st_us = 0, st_max = 0, st_slow = 0, st_stall = 0, st_stall_max = 0;
    for (;;) {
        struct pollfd pf[24];
        int n = input_fill_poll(pf, 23), nin = n;
        if (uev >= 0) { pf[n].fd = uev; pf[n].events = POLLIN; pf[n].revents = 0; n++; }
        struct timespec t0, t1;
        sys_clock_gettime(CLOCK_MONOTONIC, &t0);
        int wait = core_frame();
        sys_clock_gettime(CLOCK_MONOTONIC, &t1);
        uint64_t us = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000 + (uint64_t)((t1.tv_nsec - t0.tv_nsec) / 1000);
        if (us > 1000) { st_frames++; st_us += us; if (us > st_max) st_max = us; if (us > 20000) st_slow++; }
        if (plat_ms() >= next_stat) {        /* mesure de fluidité, dans le journal */
            if (st_frames || st_stall)
                klog("images : %d en 10 s, %d ms en moyenne (max %d, %d de plus de 20 ms), envoi à l'écran %d ms ; "
                     "retards du système : %d (max %d ms)",
                     (int)st_frames, st_frames ? (int)(st_us / st_frames / 1000) : 0, (int)(st_max / 1000), (int)st_slow,
                     st_frames ? (int)(display_us / st_frames / 1000) : 0, (int)st_stall, (int)st_stall_max);
            st_frames = st_us = st_max = st_slow = st_stall = st_stall_max = 0;
            display_us = 0;
            next_stat = plat_ms() + 10000;
        }
        if (display_flipped() && wait <= 16) wait = 0;
        uint64_t tp = plat_ms();
        sys_poll(pf, n, wait);
        /* réveil en retard : le processus n'a pas pu tourner (autre tâche, ou le
         * micrologiciel du PC qui a suspendu le processeur) */
        uint64_t late = plat_ms() - tp;
        if (wait >= 0 && late > (uint64_t)wait + 25) { st_stall++; if (late - (uint64_t)wait > st_stall_max) st_stall_max = late - (uint64_t)wait; }
        input_read(pf, nin);
        bool ev_in = false, ev_blk = false, ev_net = false;
        if (uev >= 0 && (pf[nin].revents & POLLIN)) {
            char ub[4096];
            long r;
            while ((r = sys_recv(uev, ub, sizeof ub - 1, 0)) > 0) {
                ub[r] = 0;
                for (long i = 0; i < r; i += (long)strlen(ub + i) + 1) {
                    if (!strcmp(ub + i, "SUBSYSTEM=input")) ev_in = true;
                    else if (!strcmp(ub + i, "SUBSYSTEM=block")) ev_blk = true;
                    else if (!strcmp(ub + i, "SUBSYSTEM=net")) ev_net = true;
                }
            }
        }
        plat_tick();
        uint64_t now = plat_ms();
        if (ev_in || now >= next_scan) { next_scan = now + 10000; input_scan(); }
        if (ev_net || now >= next_net) { next_net = now + 10000; net_autostart(); }
        if (ev_blk || now >= next_vol) { next_vol = now + 10000; volumes_poll(); }
    }
}

/* ================================================================== */
/* init (PID 1)                                                         */
/* ================================================================== */
/* En mode de secours : rapport automatique, affiché à l'écran et copié sur la clé */
static void rescue_report(void) {
    const char *m = "\n  Le bureau de TitanOs s'est arrêté plusieurs fois. Préparation du rapport...\n";
    sys_write(1, m, strlen(m));
    module_load("evdev");
    bool on_key = false;
    for (int i = 0; i < 60 && !on_key; i++) {      /* jusqu'à 15 s pour que la clé USB apparaisse */
        modules_coldplug(false);
        on_key = path_exists("/media/titanos/TITANOS") || mount_key();
        if (!on_key) sleep_ms(250);
    }
    static const char script[] =
        "{ echo \"TitanOs " TITANOS_VERSION " - rapport du mode de secours, $(cut -d' ' -f1 /proc/uptime) s\";"
        " echo; echo '== pilotes'; cut -d' ' -f1 /proc/modules | tr '\\n' ' '; echo;"
        " echo; echo '== entrees'; cat /proc/bus/input/devices;"
        " echo; echo '== blocs'; cat /proc/partitions; ls /sys/class/block;"
        " echo; echo '== montages'; cat /proc/mounts;"
        " echo; echo '== pci'; for d in /sys/bus/pci/devices/*; do echo \"${d##*/} $(cat $d/vendor):$(cat $d/device) $(cat $d/class) $(basename $(readlink $d/driver) 2>/dev/null)\"; done;"
        " echo; echo '== usb'; for d in /sys/bus/usb/devices/*; do [ -f $d/idVendor ] && echo \"${d##*/} $(cat $d/idVendor):$(cat $d/idProduct) $(cat $d/product 2>/dev/null)\"; done;"
        " echo; echo '== noyau'; dmesg; } > /run/rapport.txt 2>&1;"
        " [ -d /media/titanos/TITANOS ] && cp /run/rapport.txt /media/titanos/rapport-titanos.txt && sync;"
        " echo; echo '  Ce qui s est passe (a photographier) :'; echo;"
        " dmesg | grep -e 'titanos: P' -e 'titanos: appels' -e 'titanos: session' -e 'titanos: cl' -e 'titanos: aucun' -e segfault -e 'traps:' | tail -n 18;"
        " echo; echo \"  Disques : $(ls /sys/class/block | tr '\n' ' ')\";"
        " echo \"  Pilotes : $(cut -d' ' -f1 /proc/modules | tr '\n' ' ')\";"
        " echo; if [ -f /media/titanos/rapport-titanos.txt ]; then echo '  Rapport complet copie sur la cle : rapport-titanos.txt';"
        " else echo '  Cle TITANOS introuvable : rapport non copie.'; fi; echo";
    char *argv[] = { "/bin/busybox", "sh", "-c", (char *)script, NULL };
    int pid = sys_fork();
    if (pid == 0) { sys_execve(argv[0], argv, g_envp); sys_exit(127); }
    if (pid > 0) { int st; sys_wait4(pid, &st, 0); }
}

static int rescue_shell(void) {
    int pid = sys_fork();
    if (pid != 0) return pid;
    sys_setsid();
    int t = sys_open("/dev/tty1", O_RDWR, 0);
    if (t >= 0) {
        sys_ioctl(t, TIOCSCTTY, 0);
        sys_ioctl(t, KDSETMODE, (void *)(long)KD_TEXT);
        sys_ioctl(t, KDSKBMODE, (void *)(long)K_UNICODE);
        sys_dup2(t, 0); sys_dup2(t, 1); sys_dup2(t, 2);
    }
    rescue_report();
    const char *msg = "  TitanOs - mode de secours (shell BusyBox). Tapez « exit » pour relancer le bureau.\n\n";
    sys_write(1, msg, strlen(msg));
    char *argv[] = { "-sh", NULL };
    sys_execve("/bin/sh", argv, g_envp);
    sys_exit(127);
    return 0;
}

static int start_session(void) {
    int pid = sys_fork();
    if (pid == 0) sys_exit(session_main());
    return pid;
}

/* mémoire compressée : la moitié de la mémoire vive en zram, utilisée comme échange */
static void setup_zram(void) {
    char mi[256];
    if (read_file("/proc/meminfo", mi, sizeof mi) <= 0) return;
    long kb = strtol(mi + 9, NULL, 10);                 /* « MemTotal:  8061512 kB » */
    if (kb <= 0 || !module_load("zram")) return;
    for (int i = 0; i < 20 && !path_exists("/sys/block/zram0/disksize"); i++) sleep_ms(20);
    char sz[32];
    snprintf(sz, sizeof sz, "%ld", kb * 1024 / 2);
    if (write_file("/sys/block/zram0/disksize", sz) != 0) return;
    char *mk[] = { "/bin/busybox", "mkswap", "/dev/zram0", NULL };
    char *on[] = { "/bin/busybox", "swapon", "-p", "100", "/dev/zram0", NULL };
    if (run_wait(mk) == 0 && run_wait(on) == 0) klog("mémoire compressée (zram) : %ld Mo", kb / 2048);
}

static int init_main(void) {
    sys_mount("proc", "/proc", "proc", 0, NULL);
    sys_mount("sysfs", "/sys", "sysfs", 0, NULL);
    sys_mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    sys_mkdir("/dev/pts", 0755);
    sys_mount("devpts", "/dev/pts", "devpts", 0, "gid=5,mode=620,ptmxmode=666");
    sys_mkdir("/dev/shm", 01777);
    sys_mount("tmpfs", "/dev/shm", "tmpfs", 0, NULL);
    sys_mount("tmpfs", "/tmp", "tmpfs", 0, NULL);
    sys_mount("tmpfs", "/run", "tmpfs", 0, NULL);
    sys_symlink("/proc/self/fd", "/dev/fd");
    sys_symlink("/proc/self/fd/0", "/dev/stdin");
    sys_symlink("/proc/self/fd/1", "/dev/stdout");
    sys_symlink("/proc/self/fd/2", "/dev/stderr");
    mkdirs("/media");
    mkdirs("/var/run");
    write_file("/proc/sys/kernel/printk", "1 4 1 7");
    sys_sethostname("titanos", 7);
    sys_reboot(LINUX_REBOOT_CMD_CAD_OFF);        /* Ctrl+Alt+Suppr ne redémarre pas brutalement */
    iface_up("lo");
    /* commandes BusyBox : un lien par commande (ls, cp, vi, udhcpc, modprobe...) */
    char *bb[] = { "/bin/busybox", "--install", "-s", NULL };
    run_wait(bb);
    int uev = uevent_open();
    modules_init();
    klog("TitanOs " TITANOS_VERSION " : démarrage");
    setup_zram();

    bool shell = cmdline_has("titanos.shell");
    int session = shell ? -1 : start_session();
    int shell_pid = shell ? rescue_shell() : -1;
    uint64_t starts[5] = { 0 };
    int nstarts = 1;
    for (;;) {
        struct pollfd p = { uev, POLLIN, 0 };
        sys_poll(&p, uev >= 0 ? 1 : 0, 500);
        if (uev >= 0 && (p.revents & POLLIN)) uevent_handle(uev);
        for (;;) {
            int st;
            int pid = sys_wait4(-1, &st, WNOHANG_);
            if (pid <= 0) break;
            if (pid == shell_pid) { shell_pid = -1; session = start_session(); continue; }
            if (pid != session) continue;
            /* la session s'est arrêtée : on la relance, sauf si elle plante en boucle */
            struct timespec ts;
            sys_clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t now = (uint64_t)ts.tv_sec;
            starts[nstarts++ % 5] = now;
            bool looping = nstarts >= 5 && now - starts[nstarts % 5] < 60;
            if (st & 0x7F) klog("session arrêtée par le signal %d%s", st & 0x7F, looping ? " : mode de secours" : "");
            else klog("session arrêtée (code %d)%s", (st >> 8) & 0xFF, looping ? " : mode de secours" : "");
            sleep_ms(500);
            if (looping) { session = -1; shell_pid = rescue_shell(); nstarts = 0; }
            else session = start_session();
        }
    }
}

int start_c(long *sp) {
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    g_envp = argv + argc + 1;
    static char *default_env[] = { "PATH=/bin:/sbin:/usr/bin:/usr/sbin", "HOME=/root", "TERM=linux", NULL };
    if (!g_envp[0]) g_envp = default_env;
    if (sys_getpid() == 1) sys_exit(init_main());
    sys_exit(session_main());
}
