/* TitanOs - fonctions plat_*() du bureau, version Linux */
#include "linux.h"
#include "../core/desk.h"
#include <asm/termios.h>
#include <linux/input.h>

const char *pci_vendor_name(uint16_t v);
const char *pci_device_name(uint16_t v, uint16_t d);
const char *pci_class_name(uint8_t c, uint8_t s, uint8_t p);
static int hwmon_temp(const char *chip);

/* ---------------- temps, mémoire, écran ---------------- */
uint64_t plat_ms(void) {
    struct timespec ts;
    sys_clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

int64_t plat_time(void) {
    struct timespec ts;
    sys_clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec;
}

void *plat_pages(size_t n) {
    void *p = sys_mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return ((long)p < 0 && (long)p > -4096) ? NULL : p;
}
void plat_pages_free(void *p, size_t n) { sys_munmap(p, n); }
void plat_present(const uint32_t *scene, int stride, rect_t r) { display_present(scene, stride, r); }
bool plat_cursor_image(const canvas_t *img, int hx, int hy) {
    return img ? display_cursor_image(img->px, img->stride, img->w, img->h, hx, hy) : display_cursor_image(NULL, 0, 0, 0, 0, 0);
}
void plat_cursor_move(int x, int y) { display_cursor_move(x, y); }
void plat_log(const char *msg) { klog("%s", msg); }

/* ---------------- emplacements ---------------- */
static char home_dir[256] = "/root", config_file[256] = "/root/titanos.cfg", storage_name[96] = "Mémoire (non conservé)";
void plat_set_storage(const char *home, const char *config, const char *storage) {
    strlcpy(home_dir, home, sizeof home_dir);
    strlcpy(config_file, config, sizeof config_file);
    strlcpy(storage_name, storage, sizeof storage_name);
}
const char *plat_home(void) { return home_dir; }
const char *plat_config_path(void) { return config_file; }
const char *plat_storage(void) { return storage_name; }

/* ---------------- fichiers ---------------- */
typedef struct { const char *dir; dirent_t *list; int n, cap; } list_ctx_t;

static void list_cb(const char *name, int type, void *ctx) {
    (void)type;
    list_ctx_t *l = ctx;
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        dirent_t *nl = realloc(l->list, sizeof(dirent_t) * (size_t)l->cap);
        if (!nl) return;
        l->list = nl;
    }
    dirent_t *d = &l->list[l->n];
    memset(d, 0, sizeof *d);
    strlcpy(d->name, name, sizeof d->name);
    char p[768];
    snprintf(p, sizeof p, "%s/%s", strcmp(l->dir, "/") ? l->dir : "", name);
    struct stat st;
    if (sys_stat(p, &st) == 0) {
        d->dir = (st.st_mode & 0170000) == 0040000;
        d->size = (uint64_t)st.st_size;
        d->mtime = (int64_t)st.st_mtime;
    }
    l->n++;
}

int plat_listdir(const char *path, dirent_t **out) {
    list_ctx_t l = { path, NULL, 0, 0 };
    if (for_each_dir(path, list_cb, &l) < 0) return -1;
    *out = l.list;
    return l.n;
}

int plat_readfile(const char *path, char **data, size_t *len) {
    int fd = sys_open(path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct stat st;
    if (sc2(__NR_fstat, fd, (long)&st) < 0 || st.st_size > 64 * 1024 * 1024) { sys_close(fd); return -1; }
    char *b = malloc((size_t)st.st_size + 1);
    size_t total = 0;
    while (b && total < (size_t)st.st_size) {
        long r = sys_read(fd, b + total, (size_t)st.st_size - total);
        if (r <= 0) break;
        total += (size_t)r;
    }
    sys_close(fd);
    if (!b) return -1;
    b[total] = 0;
    *data = b;
    *len = total;
    return 0;
}

/* écriture sûre : le fichier complet est d'abord écrit à côté, puis remplace l'ancien d'un
 * coup (renommage). Une coupure de courant laisse l'ancien ou le nouveau, jamais un mélange. */
int plat_writefile(const char *path, const void *data, size_t len) {
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.tmp~", path);
    int fd = sys_open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    size_t done = 0;
    while (done < len) {
        long r = sys_write(fd, (const char *)data + done, len - done);
        if (r <= 0) { sys_close(fd); sys_unlink(tmp); return -1; }
        done += (size_t)r;
    }
    sc1(__NR_fsync, fd);
    sys_close(fd);
    if (sc4(__NR_renameat, AT_FDCWD, (long)tmp, AT_FDCWD, (long)path) != 0) { sys_unlink(tmp); return -1; }
    return 0;
}

int plat_mkdir(const char *path) { int r = sys_mkdir(path, 0755); return (r == 0 || r == -EEXIST) ? 0 : -1; }

static void remove_cb(const char *name, int type, void *ctx) {
    (void)type;
    char p[768];
    snprintf(p, sizeof p, "%s/%s", (const char *)ctx, name);
    plat_remove(p);
}

int plat_remove(const char *path) {
    if (!strcmp(path, "/") || !strcmp(path, home_dir)) return -1;
    if (is_dir(path)) {
        for_each_dir(path, remove_cb, (void *)path);
        return sys_rmdir(path) == 0 ? 0 : -1;
    }
    return sys_unlink(path) == 0 ? 0 : -1;
}

int plat_rename(const char *a, const char *b) { return sys_rename(a, b) == 0 ? 0 : -1; }

int plat_copy(const char *a, const char *b) {
    char *d;
    size_t n;
    if (plat_readfile(a, &d, &n) != 0) return -1;
    int r = plat_writefile(b, d, n);
    free(d);
    return r;
}

bool plat_exists(const char *path, bool *dir) {
    struct stat st;
    if (sys_stat(path, &st) != 0) return false;
    if (dir) *dir = (st.st_mode & 0170000) == 0040000;
    return true;
}

/* ---------------- processus : terminal et commandes ---------------- */
static bool user_ok(void) { return path_exists("/home/titan"); }
static char *const *make_env(void) {
    static char henv[300];
    static char *env[12];
    snprintf(henv, sizeof henv, "HOME=%s", user_ok() ? "/home/titan" : home_dir);
    int k = 0;
    env[k++] = "PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";
    env[k++] = henv;
    env[k++] = "TERM=xterm-256color";
    env[k++] = "USER=titan";
    env[k++] = "LOGNAME=titan";
    env[k++] = path_exists("/bin/bash") ? "SHELL=/bin/bash" : "SHELL=/bin/sh";
    env[k++] = "EDITOR=nano";
    env[k++] = "LANG=fr_FR.UTF-8";
    env[k++] = "LC_ALL=fr_FR.UTF-8";
    env[k] = NULL;
    return env;
}

typedef struct { int pid, fd, status; bool used, done; char out[2048]; int outn; } job_t;
static job_t jobs[16];
typedef struct { int pid, fd; bool used, dead; } pty_t;
static pty_t ptys[8];

static void reap(void) {
    for (;;) {
        int st = 0;
        int pid = sys_wait4(-1, &st, WNOHANG_);
        if (pid <= 0) return;
        int code = (st & 0x7F) ? 128 + (st & 0x7F) : (st >> 8) & 0xFF;
        for (int i = 0; i < 16; i++)
            if (jobs[i].used && jobs[i].pid == pid) { jobs[i].done = true; jobs[i].status = code; }
        for (int i = 0; i < 8; i++)
            if (ptys[i].used && ptys[i].pid == pid) ptys[i].dead = true;
    }
}

int plat_pty_open(int cols, int rows) {
    int id = -1;
    for (int i = 0; i < 8; i++) if (!ptys[i].used) { id = i; break; }
    if (id < 0) return -1;
    int fd = sys_open("/dev/ptmx", O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK, 0);
    if (fd < 0) return -1;
    int zero = 0;
    unsigned num = 0;
    sys_ioctl(fd, TIOCSPTLCK, &zero);
    if (sys_ioctl(fd, TIOCGPTN, &num) < 0) { sys_close(fd); return -1; }
    struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
    sys_ioctl(fd, TIOCSWINSZ, &ws);
    char slave[32];
    snprintf(slave, sizeof slave, "/dev/pts/%u", num);
    char *const *env = make_env();
    int pid = sys_fork();
    if (pid < 0) { sys_close(fd); return -1; }
    if (pid == 0) {
        sys_setsid();
        int s = sys_open(slave, O_RDWR, 0);
        if (s < 0) sys_exit(126);
        sys_ioctl(s, TIOCSCTTY, 0);
        /* le Terminal n'est pas root : compte titan (uid 1000), sudo pour l'administration */
        if (user_ok()) {
            sc3(__NR_fchown, s, 1000, 5 /* tty */);
            static const uint32_t groups[] = { 1000, 10 /* wheel */, 18 /* audio */, 27 /* video */ };
            sc2(__NR_setgroups, countof(groups), (long)groups);
            if (sc3(__NR_setresgid, 1000, 1000, 1000) != 0 || sc3(__NR_setresuid, 1000, 1000, 1000) != 0) sys_exit(126);
        }
        sys_dup2(s, 0); sys_dup2(s, 1); sys_dup2(s, 2);
        if (s > 2) sys_close(s);
        if (!user_ok() || sys_chdir("/home/titan") != 0) sys_chdir(home_dir);
        bool bash = path_exists("/bin/bash");
        char *argv[] = { bash ? "-bash" : "-sh", NULL };
        sys_execve(bash ? "/bin/bash" : "/bin/sh", argv, env);
        sys_execve("/bin/sh", (char *[]){ "-sh", NULL }, env);
        sys_exit(127);
    }
    ptys[id] = (pty_t){ pid, fd, true, false };
    return id;
}

int plat_pty_read(int id, char *buf, int n) {
    if (id < 0 || id >= 8 || !ptys[id].used) return -1;
    long r = sys_read(ptys[id].fd, buf, (size_t)n);
    if (r > 0) return (int)r;
    if (r == -EAGAIN) { reap(); return 0; }
    return -1;                       /* -EIO : le shell est terminé */
}

void plat_pty_write(int id, const char *buf, int n) {
    if (id < 0 || id >= 8 || !ptys[id].used) return;
    while (n > 0) {
        long r = sys_write(ptys[id].fd, buf, (size_t)n);
        if (r == -EAGAIN) { sleep_ms(2); continue; }
        if (r <= 0) return;
        buf += r;
        n -= (int)r;
    }
}

void plat_pty_resize(int id, int cols, int rows) {
    if (id < 0 || id >= 8 || !ptys[id].used) return;
    struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
    sys_ioctl(ptys[id].fd, TIOCSWINSZ, &ws);
}

void plat_pty_close(int id) {
    if (id < 0 || id >= 8 || !ptys[id].used) return;
    if (!ptys[id].dead) sys_kill(ptys[id].pid, 1 /* SIGHUP */);
    sys_close(ptys[id].fd);
    ptys[id].used = false;
    reap();
}

int plat_spawn(const char *const *argv) {
    int id = -1;
    for (int i = 0; i < 16; i++) if (!jobs[i].used) { id = i; break; }
    if (id < 0) return -1;
    int p[2];
    if (sys_pipe2(p, O_CLOEXEC) < 0) return -1;
    char *const *env = make_env();
    int pid = sys_fork();
    if (pid < 0) { sys_close(p[0]); sys_close(p[1]); return -1; }
    if (pid == 0) {
        int nul = sys_open("/dev/null", O_RDONLY, 0);
        if (nul >= 0) sys_dup2(nul, 0);
        sys_dup2(p[1], 1);
        sys_dup2(p[1], 2);
        sys_execve(argv[0], (char *const *)argv, env);
        sys_exit(127);
    }
    sys_close(p[1]);
    sc3(__NR_fcntl, p[0], F_SETFL, O_NONBLOCK);
    job_t *j = &jobs[id];
    memset(j, 0, sizeof *j);
    j->used = true;
    j->pid = pid;
    j->fd = p[0];
    return id;
}

int plat_spawn_poll(int id, char *out, int outsz) {
    if (id < 0 || id >= 16 || !jobs[id].used) return 127;
    job_t *j = &jobs[id];
    for (;;) {
        char tmp[512];
        long r = sys_read(j->fd, tmp, sizeof tmp);
        if (r <= 0) break;
        int room = (int)sizeof j->out - 1 - j->outn;
        if (room > 0) { memcpy(j->out + j->outn, tmp, (size_t)MIN((long)room, r)); j->outn += (int)MIN((long)room, r); }
    }
    reap();
    if (!j->done) return -1;
    j->out[j->outn] = 0;
    if (outsz > 0) strlcpy(out, j->out, (size_t)outsz);
    sys_close(j->fd);
    j->used = false;
    return j->status;
}

/* ---------------- informations système ---------------- */
static uint64_t meminfo(const char *buf, const char *key) {
    const char *p = strstr(buf, key);
    return p ? (uint64_t)strtol(p + strlen(key), NULL, 10) * 1024 : 0;
}

static void driver_of(const char *devdir, char *out, int n) {
    char p[300], link[256];
    snprintf(p, sizeof p, "%s/driver", devdir);
    long r = sys_readlink(p, link, sizeof link - 1);
    if (r <= 0) { out[0] = 0; return; }
    link[r] = 0;
    const char *b = strrchr(link, '/');
    strlcpy(out, b ? b + 1 : link, (size_t)n);
}

static unsigned hexfile(const char *dir, const char *name) {
    char p[300], v[32];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    if (read_file(p, v, sizeof v) <= 0) return 0;
    return (unsigned)strtol(v, NULL, 16);
}

static void pci_name(unsigned ven, unsigned dev, unsigned cls, char *out, int n) {
    const char *d = pci_device_name((uint16_t)ven, (uint16_t)dev);
    if (d) snprintf(out, (size_t)n, "%s %s", pci_vendor_name((uint16_t)ven), d);
    else snprintf(out, (size_t)n, "%s – %s", pci_vendor_name((uint16_t)ven),
                  pci_class_name((uint8_t)(cls >> 16), (uint8_t)(cls >> 8), (uint8_t)cls));
}

typedef struct { char name[96], driver[32]; } gpu_ctx_t;
static void gpu_cb(const char *name, int type, void *ctx) {
    (void)type;
    gpu_ctx_t *g = ctx;
    char dir[160];
    snprintf(dir, sizeof dir, "/sys/bus/pci/devices/%s", name);
    unsigned cls = hexfile(dir, "class");
    if ((cls >> 16) != 0x03 || g->name[0]) return;
    pci_name(hexfile(dir, "vendor"), hexfile(dir, "device"), cls, g->name, sizeof g->name);
    driver_of(dir, g->driver, sizeof g->driver);
}

static void count_cb(const char *name, int type, void *ctx) { (void)type; if (name[0] >= '0' && name[0] <= '9') (*(int *)ctx)++; }

void plat_sysinfo(sysinfo_t *s) {
    memset(s, 0, sizeof *s);
    char buf[16384];
    read_file("/proc/sys/kernel/osrelease", buf, sizeof buf);
    strlcpy(s->kernel, trim(buf), sizeof s->kernel);
    read_file("/proc/cpuinfo", buf, sizeof buf);
    for (char *p = buf; (p = strstr(p, "processor")); p++) s->cores++;
    char *m = strstr(buf, "model name");
    if (m && (m = strchr(m, ':'))) {
        char *e = strchr(m, '\n');
        if (e) *e = 0;
        char clean[96] = { 0 };
        int k = 0;
        for (char *q = trim(m + 1); *q && k < 94; q++) {          /* retire (R), (TM) et les espaces doubles */
            if (*q == '(' && (str_starts(q, "(R)") || str_starts(q, "(TM)"))) { q = strchr(q, ')'); continue; }
            if (*q == ' ' && k && clean[k - 1] == ' ') continue;
            clean[k++] = *q;
        }
        strlcpy(s->cpu, clean, sizeof s->cpu);
    }
    read_file("/proc/meminfo", buf, sizeof buf);
    s->mem_total = meminfo(buf, "MemTotal:");
    s->mem_free = meminfo(buf, "MemAvailable:");
    read_file("/proc/uptime", buf, sizeof buf);
    s->uptime_s = (uint64_t)strtol(buf, NULL, 10);
    for_each_dir("/proc", count_cb, &s->procs);
    gpu_ctx_t g = { { 0 }, { 0 } };
    for_each_dir("/sys/bus/pci/devices", gpu_cb, &g);
    strlcpy(s->gpu, g.name[0] ? g.name : "Inconnue", sizeof s->gpu);
    strlcpy(s->gpu_driver, g.driver[0] ? g.driver : "aucun (écran du BIOS)", sizeof s->gpu_driver);
    strlcpy(s->boot, path_exists("/sys/firmware/efi") ? "UEFI" : "BIOS", sizeof s->boot);
    if (display_hz() > 0) snprintf(s->display, sizeof s->display, "%d × %d à %d Hz (%s)", display_w(), display_h(), display_hz(), display_driver());
    else snprintf(s->display, sizeof s->display, "%d × %d (%s)", display_w(), display_h(), display_driver());
    s->cpu_temp = hwmon_temp("coretemp");
    s->gpu_temp = hwmon_temp("nouveau");
    if (read_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", buf, sizeof buf) > 0) s->cpu_mhz = atoi(buf) / 1000;
    else s->cpu_mhz = -1;
}

typedef struct { device_t *list; int n, max; } dev_ctx_t;

static void pci_cb(const char *name, int type, void *ctx) {
    (void)type;
    dev_ctx_t *c = ctx;
    if (c->n >= c->max) return;
    char dir[160];
    snprintf(dir, sizeof dir, "/sys/bus/pci/devices/%s", name);
    device_t *d = &c->list[c->n++];
    unsigned ven = hexfile(dir, "vendor"), dev = hexfile(dir, "device"), cls = hexfile(dir, "class");
    strlcpy(d->bus, "PCI", sizeof d->bus);
    snprintf(d->id, sizeof d->id, "%04x:%04x", ven, dev);
    pci_name(ven, dev, cls, d->name, sizeof d->name);
    driver_of(dir, d->driver, sizeof d->driver);
}

static void usb_cb(const char *name, int type, void *ctx) {
    (void)type;
    dev_ctx_t *c = ctx;
    if (c->n >= c->max || strchr(name, ':')) return;
    char dir[160], p[200], prod[96], manu[64];
    snprintf(dir, sizeof dir, "/sys/bus/usb/devices/%s", name);
    device_t *d = &c->list[c->n++];
    strlcpy(d->bus, "USB", sizeof d->bus);
    snprintf(d->id, sizeof d->id, "%04x:%04x", hexfile(dir, "idVendor"), hexfile(dir, "idProduct"));
    snprintf(p, sizeof p, "%s/product", dir);
    read_file(p, prod, sizeof prod);
    snprintf(p, sizeof p, "%s/manufacturer", dir);
    read_file(p, manu, sizeof manu);
    if (str_starts(name, "usb")) strlcpy(d->name, "Concentrateur USB principal", sizeof d->name);
    else snprintf(d->name, sizeof d->name, "%s%s%s", trim(manu), manu[0] ? " " : "", prod[0] ? trim(prod) : "Périphérique USB");
    snprintf(p, sizeof p, "%s/%s:1.0", dir, name);
    driver_of(p, d->driver, sizeof d->driver);
    if (!d->driver[0]) driver_of(dir, d->driver, sizeof d->driver);
}

int plat_devices(device_t *list, int max) {
    dev_ctx_t c = { list, 0, max };
    for_each_dir("/sys/bus/pci/devices", pci_cb, &c);
    for_each_dir("/sys/bus/usb/devices", usb_cb, &c);
    return c.n;
}

typedef struct { proc_t *list; int n, max; } proc_ctx_t;
static void proc_cb(const char *name, int type, void *ctx) {
    (void)type;
    proc_ctx_t *c = ctx;
    if (name[0] < '0' || name[0] > '9' || c->n >= c->max) return;
    char p[64], buf[512];
    snprintf(p, sizeof p, "/proc/%s/stat", name);
    if (read_file(p, buf, sizeof buf) <= 0) return;
    proc_t *pr = &c->list[c->n];
    pr->pid = atoi(name);
    char *a = strchr(buf, '('), *b = strrchr(buf, ')');
    if (!a || !b) return;
    *b = 0;
    strlcpy(pr->name, a + 1, sizeof pr->name);
    pr->state = b[2];
    snprintf(p, sizeof p, "/proc/%s/statm", name);
    read_file(p, buf, sizeof buf);
    char *sp = strchr(buf, ' ');
    pr->rss_kb = sp ? (uint64_t)strtol(sp + 1, NULL, 10) * 4 : 0;
    c->n++;
}

int plat_procs(proc_t *list, int max) {
    proc_ctx_t c = { list, 0, max };
    for_each_dir("/proc", proc_cb, &c);
    return c.n;
}

bool plat_kill(int pid) { return pid > 1 && pid != sys_getpid() && sys_kill(pid, 15) == 0; }

/* ---------------- console réseau : le journal du noyau, filtré ---------------- */
static int kmsg_fd = -1;
static bool net_line(const char *m) {
    static const char *const keys[] = { "titanos: réseau", "titanos: Wi-Fi", "titanos: wpa", "titanos: diag", "wlan", "iwlwifi",
                                        "cfg80211", "ieee80211", "sky2", "eth0", "eth1", "usb0", "rndis", "cdc_ncm", "cdc_ether", "udhcpc" };
    for (int i = 0; i < countof(keys); i++) if (strstr(m, keys[i])) return true;
    return false;
}
void plat_netlog_rewind(void) { if (kmsg_fd >= 0) sys_close(kmsg_fd); kmsg_fd = -1; }
int plat_netlog_next(char *line, int n, uint64_t *usec) {
    if (kmsg_fd < 0) kmsg_fd = sys_open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC, 0);
    if (kmsg_fd < 0) return 0;
    static char rec[1200];
    for (int guard = 0; guard < 500; guard++) {
        long r = sys_read(kmsg_fd, rec, sizeof rec - 1);
        if (r == -EPIPE) continue;                       /* anciens messages écrasés : on continue */
        if (r <= 0) return 0;
        rec[r] = 0;
        char *semi = strchr(rec, ';');
        if (!semi) continue;
        char *c1 = strchr(rec, ','), *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
        char *msg = semi + 1, *nl = strchr(msg, '\n');
        if (nl) *nl = 0;
        if (!net_line(msg)) continue;
        if (str_starts(msg, "titanos: ")) msg += 9;
        strlcpy(line, msg, (size_t)n);
        *usec = c2 ? (uint64_t)strtol(c2 + 1, NULL, 10) : 0;
        return 1;
    }
    return 0;
}

/* ---------------- réseau ---------------- */
typedef struct { netif_t *list; int n, max; } net_ctx_t;

static void net_cb(const char *name, int type, void *ctx) {
    (void)type;
    net_ctx_t *c = ctx;
    if (c->n >= c->max) return;
    netif_t *f = &c->list[c->n++];
    memset(f, 0, sizeof *f);
    strlcpy(f->name, name, sizeof f->name);
    char dir[96], p[160], v[64];
    snprintf(dir, sizeof dir, "/sys/class/net/%s", name);
    snprintf(p, sizeof p, "%s/address", dir);
    read_file(p, v, sizeof v);
    strlcpy(f->mac, trim(v), sizeof f->mac);
    f->up = (hexfile(dir, "flags") & 1) != 0;
    snprintf(p, sizeof p, "%s/carrier", dir);
    f->carrier = f->up && read_file(p, v, sizeof v) > 0 && v[0] == '1';
    char drv[32];
    snprintf(p, sizeof p, "%s/device", dir);
    driver_of(p, drv, sizeof drv);
    snprintf(p, sizeof p, "%s/wireless", dir);
    if (path_exists(p) || str_starts(name, "wl")) strlcpy(f->kind, "Wi-Fi", sizeof f->kind);
    else if (!strcmp(drv, "rndis_host") || !strcmp(drv, "cdc_ether") || !strcmp(drv, "cdc_ncm")) strlcpy(f->kind, "Téléphone USB", sizeof f->kind);
    else if (!strcmp(name, "lo")) strlcpy(f->kind, "Boucle locale", sizeof f->kind);
    else strlcpy(f->kind, "Ethernet", sizeof f->kind);
    /* adresse IPv4 */
    int s = sys_socket(2 /* AF_INET */, 2 /* SOCK_DGRAM */ | O_CLOEXEC, 0);
    if (s >= 0) {
        struct { char name[16]; uint16_t family, port; uint8_t addr[4]; uint8_t pad[16]; } ifr = { 0 };
        strlcpy(ifr.name, name, sizeof ifr.name);
        if (sys_ioctl(s, 0x8915 /* SIOCGIFADDR */, &ifr) == 0)
            snprintf(f->ip, sizeof f->ip, "%u.%u.%u.%u", ifr.addr[0], ifr.addr[1], ifr.addr[2], ifr.addr[3]);
        sys_close(s);
    }
}

int plat_netifs(netif_t *list, int max) {
    net_ctx_t c = { list, 0, max };
    for_each_dir("/sys/class/net", net_cb, &c);
    /* la carte par laquelle passe Internet : route par défaut de plus faible métrique */
    static char rt[4096];
    if (read_file("/proc/net/route", rt, sizeof rt) > 0) {
        char best[16] = "";
        long bm = -1;
        for (char *l = strchr(rt, '\n'); l && l[1]; l = strchr(l + 1, '\n')) {
            char ifn[16] = "";
            int k = 0;
            const char *q = l + 1;
            while (*q && *q != '\t' && k < 15) ifn[k++] = *q++;
            ifn[k] = 0;
            if (*q != '\t' || strncmp(q + 1, "00000000", 8)) continue;          /* destination 0.0.0.0 */
            const char *f = q;
            for (int i = 0; i < 6 && f; i++) { f = strchr(f + 1, '\t'); }       /* 7e colonne : métrique */
            long m = f ? strtol(f + 1, NULL, 10) : 0;
            if (bm < 0 || m < bm) { bm = m; strlcpy(best, ifn, sizeof best); }
        }
        for (int i = 0; i < c.n; i++) list[i].internet = best[0] && !strcmp(list[i].name, best);
    }
    return c.n;
}

/* ---------------- alimentation, luminosité, son ---------------- */
void plat_sync(void) { sys_sync(); }

void plat_power(int action) {
    sys_sync();
    if (sys_umount2("/media/titanos", 0) != 0) sys_umount2("/media/titanos", 2 /* MNT_DETACH */);
    sys_sync();
    sleep_ms(300);
    sys_reboot(action ? LINUX_REBOOT_CMD_RESTART : LINUX_REBOOT_CMD_POWER_OFF);
}

/* l'heure exacte (après synchronisation) est écrite dans l'horloge du PC, en temps
 * universel : au prochain démarrage, Linux la relit telle quelle */
void plat_rtc_save(void) {
    struct timespec ts;
    sys_clock_gettime(CLOCK_REALTIME, &ts);
    datetime_t d;
    datetime_from((int64_t)ts.tv_sec, &d);
    struct { int sec, min, hour, mday, mon, year, wday, yday, isdst; } tm = {
        d.sec, d.min, d.hour, d.day, d.month - 1, d.year - 1900, d.wday, 0, 0 };
    int fd = sys_open("/dev/rtc0", O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return;
    long r = sys_ioctl(fd, 0x4024700a /* RTC_SET_TIME */, &tm);
    sys_close(fd);
    klog("horloge du PC : %s (%04d-%02d-%02d %02d:%02d UTC)", r == 0 ? "mise à l'heure" : "écriture refusée",
         d.year, d.month, d.day, d.hour, d.min);
}

void plat_random(void *buf, size_t n) {
    if (sc3(__NR_getrandom, (long)buf, (long)n, 0) != (long)n) {
        uint64_t x = plat_ms() * 6364136223846793005ull + 1442695040888963407ull;
        for (size_t i = 0; i < n; i++) { x = x * 6364136223846793005ull + 1; ((uint8_t *)buf)[i] = (uint8_t)(x >> 56); }
    }
}

void plat_suspend(void) {
    sys_sync();
    if (write_file("/sys/power/state", "mem") != 0) write_file("/sys/power/state", "freeze");
}

static char bl_dir[128];
static int bl_max;

static void bl_cb(const char *name, int type, void *ctx) {
    (void)type;
    int *best = ctx;
    char p[160], t[32];
    snprintf(p, sizeof p, "/sys/class/backlight/%s/type", name);
    read_file(p, t, sizeof t);
    int score = str_starts(t, "firmware") ? 3 : str_starts(t, "platform") ? 2 : 1;
    if (score > *best) {
        *best = score;
        snprintf(bl_dir, sizeof bl_dir, "/sys/class/backlight/%s", name);
    }
}

int plat_brightness(int set) {
    if (!bl_dir[0]) {
        int best = 0;
        for_each_dir("/sys/class/backlight", bl_cb, &best);
        if (!bl_dir[0]) return -1;
        char p[160], v[32];
        snprintf(p, sizeof p, "%s/max_brightness", bl_dir);
        read_file(p, v, sizeof v);
        bl_max = atoi(v);
        if (bl_max <= 0) { bl_dir[0] = 0; return -1; }
    }
    char p[160], v[32];
    if (set >= 0) {
        snprintf(p, sizeof p, "%s/brightness", bl_dir);
        snprintf(v, sizeof v, "%d", MAX(1, set * bl_max / 100));
        write_file(p, v);
        return set;
    }
    snprintf(p, sizeof p, "%s/brightness", bl_dir);
    read_file(p, v, sizeof v);
    return atoi(v) * 100 / bl_max;
}

/* appelé à chaque tour de la boucle principale */
void plat_tick(void) { reap(); }

/* ---------------- batterie ---------------- */
typedef struct { battery_t *b; long energy, power; } bat_ctx_t;
static void psu_cb(const char *name, int type, void *ctx) {
    (void)type;
    bat_ctx_t *c = ctx;
    char dir[96], p[160], v[64];
    snprintf(dir, sizeof dir, "/sys/class/power_supply/%s", name);
    snprintf(p, sizeof p, "%s/type", dir);
    read_file(p, v, sizeof v);
    if (str_starts(v, "Mains")) {
        snprintf(p, sizeof p, "%s/online", dir);
        if (read_file(p, v, sizeof v) > 0 && v[0] == '1') c->b->ac = true;
        return;
    }
    if (!str_starts(v, "Battery") || c->b->present) return;
    snprintf(p, sizeof p, "%s/present", dir);
    if (read_file(p, v, sizeof v) > 0 && v[0] == '0') return;
    c->b->present = true;
    snprintf(p, sizeof p, "%s/capacity", dir);
    if (read_file(p, v, sizeof v) > 0) c->b->percent = CLAMP(atoi(v), 0, 100);
    snprintf(p, sizeof p, "%s/status", dir);
    read_file(p, v, sizeof v);
    c->b->charging = str_starts(v, "Charging");
    static const char *const now_f[] = { "energy_now", "charge_now" }, *const rate_f[] = { "power_now", "current_now" };
    for (int i = 0; i < 2; i++) {
        snprintf(p, sizeof p, "%s/%s", dir, now_f[i]);
        if (read_file(p, v, sizeof v) <= 0) continue;
        c->energy = strtol(v, NULL, 10);
        snprintf(p, sizeof p, "%s/%s", dir, rate_f[i]);
        if (read_file(p, v, sizeof v) > 0) c->power = strtol(v, NULL, 10);
        break;
    }
}

/* lecture directe (lente sur certains portables : appelée par le veilleur, pas par le bureau) */
void bat_read(battery_t *b) {
    memset(b, 0, sizeof *b);
    bat_ctx_t c = { b, 0, 0 };
    for_each_dir("/sys/class/power_supply", psu_cb, &c);
    if (b->present && !b->charging && !b->ac && c.power > 0) b->minutes = (int)(c.energy * 60 / c.power);
}

/* ---------------- capteurs de température ---------------- */
typedef struct { const char *want; int temp; } hw_ctx_t;
static void hwmon_cb(const char *name, int type, void *ctx) {
    (void)type;
    hw_ctx_t *c = ctx;
    char p[128], v[32];
    snprintf(p, sizeof p, "/sys/class/hwmon/%s/name", name);
    if (c->temp > 0 || read_file(p, v, sizeof v) <= 0 || strcmp(trim(v), c->want)) return;
    snprintf(p, sizeof p, "/sys/class/hwmon/%s/temp1_input", name);
    if (read_file(p, v, sizeof v) > 0) c->temp = atoi(v) / 1000;
}
static int hwmon_temp(const char *chip) {
    hw_ctx_t c = { chip, -1 };
    for_each_dir("/sys/class/hwmon", hwmon_cb, &c);
    return c.temp;
}
