/* TitanOs - clés USB et disques externes : montage automatique, éjection
 *
 * Toutes les 2 s, la session regarde les partitions des disques amovibles (sauf celui de
 * TitanOs). Chaque nouvelle partition est montée dans /media/<nom> (FAT, exFAT, NTFS ou
 * ext4) et apparaît dans Fichiers. « Éjecter » écrit tout sur la clé puis la démonte ; elle
 * n'est plus remontée tant qu'elle reste branchée. */
#include "linux.h"
#include "../core/desk.h"

typedef struct { char dev[16]; char path[96]; char name[48]; bool used, ejected; } vol_t;
static vol_t vols[8];

static char mounts[32768];
static void read_mounts(void) { if (read_file("/proc/mounts", mounts, sizeof mounts) <= 0) mounts[0] = 0; }

static bool parent_disk(const char *part, char *disk, int n) {
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

/* disque qui porte TitanOs (celui monté sur /media/titanos) */
static void boot_disk(char *disk, int n) {
    disk[0] = 0;
    char *p = strstr(mounts, " /media/titanos ");
    if (!p) return;
    char *s = p;
    while (s > mounts && s[-1] != '\n') s--;
    char dev[48] = { 0 };
    strlcpy(dev, s, (size_t)MIN((long)sizeof dev, (long)(p - s + 1)));
    if (str_starts(dev, "/dev/")) parent_disk(dev + 5, disk, n);
}

static bool removable(const char *disk) {
    char p[96], v[8], link[256];
    snprintf(p, sizeof p, "/sys/block/%s/removable", disk);
    if (read_file(p, v, sizeof v) > 0 && v[0] == '1') return true;
    snprintf(p, sizeof p, "/sys/block/%s", disk);
    long r = sys_readlink(p, link, sizeof link - 1);
    if (r <= 0) return false;
    link[r] = 0;
    return strstr(link, "/usb") != NULL;
}

/* nom lisible : étiquette FAT si elle existe, sinon le modèle du disque */
static void volume_name(const char *dev, const char *disk, char *out, int n) {
    char p[64];
    uint8_t s[512];
    snprintf(p, sizeof p, "/dev/%s", dev);
    int fd = sys_open(p, O_RDONLY | O_CLOEXEC, 0);
    out[0] = 0;
    if (fd >= 0) {
        if (sys_read(fd, s, sizeof s) == 512) {
            const uint8_t *lab = !memcmp(s + 82, "FAT32", 5) ? s + 71 : !memcmp(s + 54, "FAT", 3) ? s + 43 : NULL;
            if (lab && memcmp(lab, "NO NAME", 7)) {
                int k = 0;
                for (int i = 0; i < 11 && k < n - 1; i++) out[k++] = (char)lab[i];
                while (k > 0 && out[k - 1] == ' ') k--;
                out[k] = 0;
            }
        }
        sys_close(fd);
    }
    if (!out[0]) {
        char m[64];
        snprintf(p, sizeof p, "/sys/block/%s/device/model", disk);
        if (read_file(p, m, sizeof m) > 0 && trim(m)[0]) strlcpy(out, trim(m), (size_t)n);
        else strlcpy(out, "Clé USB", (size_t)n);
    }
}

static void part_cb(const char *name, int type, void *ctx) {
    (void)type;
    const char *boot = ctx;
    char p[96], disk[32];
    snprintf(p, sizeof p, "/sys/class/block/%s/partition", name);
    if (!path_exists(p) || !parent_disk(name, disk, sizeof disk) || !strcmp(disk, boot) || !removable(disk)) return;
    for (int i = 0; i < countof(vols); i++) if (vols[i].used && !strcmp(vols[i].dev, name)) return;
    char pat[32];
    snprintf(pat, sizeof pat, "/dev/%s ", name);
    if (strstr(mounts, pat)) return;                   /* déjà monté ailleurs */
    int slot = -1;
    for (int i = 0; i < countof(vols); i++) if (!vols[i].used) { slot = i; break; }
    if (slot < 0) return;
    vol_t *v = &vols[slot];
    memset(v, 0, sizeof *v);
    strlcpy(v->dev, name, sizeof v->dev);
    volume_name(name, disk, v->name, sizeof v->name);
    char safe[48];
    int k = 0;
    for (const char *c = v->name; *c && k < 40; c++) safe[k++] = (*c == '/' || *c == ' ') ? '_' : *c;
    safe[k] = 0;
    snprintf(v->path, sizeof v->path, "/media/%s", safe);
    for (int i = 2; path_exists(v->path) && i < 9; i++) snprintf(v->path, sizeof v->path, "/media/%s_%d", safe, i);
    mkdirs(v->path);
    char dev[48];
    snprintf(dev, sizeof dev, "/dev/%s", name);
    static const char *const fs[] = { "vfat", "exfat", "ntfs3", "ext4" };
    static const char *const opts[] = { "iocharset=utf8,shortname=mixed,flush", "iocharset=utf8", "iocharset=utf8", "" };
    for (int i = 0; i < countof(fs); i++) {
        module_load(fs[i]);
        if (sys_mount(dev, v->path, fs[i], 1024 /* MS_NOATIME */, opts[i]) == 0) {
            v->used = true;
            klog("volume %s (%s) monté sur %s", name, fs[i], v->path);
            return;
        }
    }
    sys_rmdir(v->path);
    v->used = true;                                    /* illisible : on ne réessaie pas en boucle */
    v->ejected = true;
}

void volumes_poll(void) {
    static bool mods;
    if (!mods) { mods = true; module_load("nls_utf8"); module_load("nls_cp437"); module_load("nls_iso8859_1"); }
    read_mounts();
    /* clés retirées : démontage forcé, place libérée */
    for (int i = 0; i < countof(vols); i++) {
        if (!vols[i].used) continue;
        char p[64];
        snprintf(p, sizeof p, "/sys/class/block/%s", vols[i].dev);
        if (path_exists(p)) continue;
        if (!vols[i].ejected) { sys_umount2(vols[i].path, 2 /* MNT_DETACH */); sys_rmdir(vols[i].path); }
        klog("volume %s retiré", vols[i].dev);
        vols[i].used = false;
    }
    char boot[32];
    boot_disk(boot, sizeof boot);
    for_each_dir("/sys/class/block", part_cb, boot);
}

int plat_volumes(void *list, int max) {
    volume_t *out = list;
    int n = 0;
    for (int i = 0; i < countof(vols) && n < max; i++) {
        if (!vols[i].used || vols[i].ejected) continue;
        strlcpy(out[n].name, vols[i].name, sizeof out[n].name);
        strlcpy(out[n].path, vols[i].path, sizeof out[n].path);
        n++;
    }
    return n;
}

bool plat_eject(const char *path) {
    for (int i = 0; i < countof(vols); i++) {
        if (!vols[i].used || vols[i].ejected || strcmp(vols[i].path, path)) continue;
        sys_sync();
        if (sys_umount2(path, 0) != 0) return false;   /* encore utilisé (fichier ouvert) */
        sys_rmdir(path);
        vols[i].ejected = true;
        sys_sync();
        klog("volume %s éjecté", vols[i].dev);
        return true;
    }
    return false;
}
