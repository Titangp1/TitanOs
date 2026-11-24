/* TitanOs - chargement automatique des pilotes (modules du noyau)
 *
 * Chaque périphérique publie dans /sys un « modalias » (ex. pci:v000010DEd00000A29...).
 * Le fichier modules.alias associe des motifs à des pilotes, et modules.dep donne les
 * dépendances de chaque pilote. On charge donc, pour chaque périphérique présent ou
 * branché à chaud, le pilote qui correspond — c'est le travail de udev, en 200 lignes. */
#include "linux.h"

#define MODDIR "/lib/modules/current"

typedef struct { char *name; char *path; char *deps; bool tried; } mod_t;
typedef struct { char *pattern; char *name; } alias_t;
static mod_t *mods;
static int nmods;
static alias_t *aliases;
static int naliases;

static char *load_text(const char *path) {
    int fd = sys_open(path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return NULL;
    struct stat st;
    if (sc2(__NR_fstat, fd, (long)&st) < 0) { sys_close(fd); return NULL; }
    char *b = malloc((size_t)st.st_size + 1);
    long n = b ? sys_read(fd, b, (size_t)st.st_size) : -1;
    sys_close(fd);
    if (n < 0) { free(b); return NULL; }
    b[n] = 0;
    return b;
}

static void normalize(char *s) { for (; *s; s++) if (*s == '-') *s = '_'; }

/* nom du module à partir du chemin : kernel/drivers/hid/hid-generic.ko -> hid_generic */
static char *name_of(const char *path) {
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    char tmp[96];
    strlcpy(tmp, b, sizeof tmp);
    char *dot = strstr(tmp, ".ko");
    if (dot) *dot = 0;
    normalize(tmp);
    return strdup(tmp);
}

void modules_init(void) {
    /* une seule fois : la session est une copie de /init, qui a déjà lu les tables.
     * Les relire ajouterait les entrées au-delà de la place réservée. */
    if (mods || aliases) return;
    char *dep = load_text(MODDIR "/modules.dep");
    if (dep) {
        int lines = 1;
        for (char *p = dep; *p; p++) if (*p == '\n') lines++;
        mods = calloc((size_t)lines, sizeof(mod_t));
        for (char *line = dep; line && *line;) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = 0;
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = 0;
                mods[nmods].path = line;
                mods[nmods].deps = trim(colon + 1);
                mods[nmods].name = name_of(line);
                nmods++;
            }
            line = nl ? nl + 1 : NULL;
        }
    }
    char *al = load_text(MODDIR "/modules.alias");
    if (al) {
        int lines = 1;
        for (char *p = al; *p; p++) if (*p == '\n') lines++;
        aliases = calloc((size_t)lines, sizeof(alias_t));
        for (char *line = al; line && *line;) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = 0;
            if (str_starts(line, "alias ")) {
                char *pat = line + 6, *sp = strchr(pat, ' ');
                if (sp) {
                    *sp = 0;
                    aliases[naliases].pattern = pat;
                    aliases[naliases].name = sp + 1;
                    normalize(sp + 1);
                    naliases++;
                }
            }
            line = nl ? nl + 1 : NULL;
        }
    }
    klog("%d pilotes, %d alias", nmods, naliases);
}

static mod_t *find_mod(const char *name) {
    for (int i = 0; i < nmods; i++) if (!strcmp(mods[i].name, name)) return &mods[i];
    return NULL;
}

static bool loaded(const char *name) {
    char p[160];
    snprintf(p, sizeof p, "/sys/module/%s", name);
    return path_exists(p);
}

static bool insmod(const char *relpath) {
    char p[256];
    snprintf(p, sizeof p, MODDIR "/%s", relpath);
    int fd = sys_open(p, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return false;
    int r = sys_finit_module(fd, "", 0);
    sys_close(fd);
    return r == 0 || r == -EEXIST;
}

bool module_load(const char *name) {
    char n[96];
    strlcpy(n, name, sizeof n);
    normalize(n);
    mod_t *m = find_mod(n);
    if (!m) return false;
    if (loaded(n)) return true;
    if (m->tried) return false;
    m->tried = true;
    /* dépendances : la liste se charge de la fin vers le début */
    char deps[2048];
    strlcpy(deps, m->deps, sizeof deps);
    char *items[64];
    int k = 0;
    for (char *t = deps; *t && k < 64;) {
        while (*t == ' ') t++;
        if (!*t) break;
        items[k++] = t;
        while (*t && *t != ' ') t++;
        if (*t) *t++ = 0;
    }
    for (int i = k - 1; i >= 0; i--) {
        char *dn = name_of(items[i]);
        if (!loaded(dn)) insmod(items[i]);
        free(dn);
    }
    bool ok = insmod(m->path);
    klog("pilote %s : %s", n, ok ? "chargé" : "échec");
    return ok;
}

/* correspondance de motifs façon shell : * ? [abc] [a-z] */
static bool glob(const char *p, const char *s) {
    for (; *p; p++, s++) {
        if (*p == '*') {
            while (p[1] == '*') p++;
            if (!p[1]) return true;
            for (; *s; s++) if (glob(p + 1, s)) return true;
            return false;
        }
        if (!*s) return false;
        if (*p == '?') continue;
        if (*p == '[') {
            bool neg = p[1] == '!' || p[1] == '^', ok = false;
            const char *q = p + 1 + neg;
            for (; *q && *q != ']'; q++) {
                if (q[1] == '-' && q[2] && q[2] != ']') { if (*s >= *q && *s <= q[2]) ok = true; q += 2; }
                else if (*q == *s) ok = true;
            }
            if (ok == neg || !*q) return false;
            p = q;
            continue;
        }
        if (*p != *s) return false;
    }
    return !*s;
}

static int load_for_alias(const char *alias) {
    int n = 0;
    for (int i = 0; i < naliases; i++)
        if (glob(aliases[i].pattern, alias) && !loaded(aliases[i].name)) n += module_load(aliases[i].name);
    return n;
}

/* ---- périphériques déjà présents (« coldplug ») ---- */
typedef struct { const char *bus; bool display_only; int loaded; } scan_t;

static void scan_dev(const char *name, int type, void *ctx) {
    (void)type;
    scan_t *s = ctx;
    char p[256], alias[256];
    if (s->display_only) {
        char cls[32];
        snprintf(p, sizeof p, "/sys/bus/pci/devices/%s/class", name);
        if (read_file(p, cls, sizeof cls) < 0 || !str_starts(cls, "0x03")) return;
    }
    snprintf(p, sizeof p, "/sys/bus/%s/devices/%s/modalias", s->bus, name);
    if (read_file(p, alias, sizeof alias) <= 0) return;
    s->loaded += load_for_alias(trim(alias));
}

static void scan_bus(const char *bus, int type, void *ctx) {
    (void)type;
    scan_t *s = ctx;
    s->bus = bus;
    char p[160];
    snprintf(p, sizeof p, "/sys/bus/%s/devices", bus);
    for_each_dir(p, scan_dev, s);
}

int modules_coldplug(bool display_only) {
    scan_t s = { 0 };
    s.display_only = display_only;
    if (display_only) { s.bus = "pci"; for_each_dir("/sys/bus/pci/devices", scan_dev, &s); }
    else for_each_dir("/sys/bus", scan_bus, &s);
    return s.loaded;
}

/* ---- périphériques branchés à chaud (messages « uevent » du noyau) ---- */
int uevent_open(void) {
    int fd = sys_socket(16 /* AF_NETLINK */, 2 /* SOCK_DGRAM */ | O_CLOEXEC | O_NONBLOCK, 15 /* NETLINK_KOBJECT_UEVENT */);
    if (fd < 0) return -1;
    struct { uint16_t family, pad; uint32_t pid, groups; } addr = { 16, 0, 0, 1 };
    if (sys_bind(fd, &addr, sizeof addr) < 0) { sys_close(fd); return -1; }
    return fd;
}

void uevent_handle(int fd) {
    char buf[4096];
    for (;;) {
        long n = sys_recv(fd, buf, sizeof buf - 1, 0);
        if (n <= 0) return;
        buf[n] = 0;
        bool add = str_starts(buf, "add@") || str_starts(buf, "bind@");
        if (!add) continue;
        for (long i = 0; i < n; i += (long)strlen(buf + i) + 1)
            if (str_starts(buf + i, "MODALIAS=")) load_for_alias(buf + i + 9);
    }
}
