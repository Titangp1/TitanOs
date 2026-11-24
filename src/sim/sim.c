/* TitanOs - aperçu du bureau sous Windows (pour les captures d'écran)
 *
 * Compilé en DLL sans aucune dépendance : les fonctions plat_*() renvoient des données
 * d'exemple, et tools/preview.py pilote le bureau puis enregistre des images PNG. */
#include "../core/desk.h"

#define EXPORT __attribute__((dllexport))
int _fltused = 1;

/* ---------------- mémoire : grande réserve statique ---------------- */
static uint8_t arena[640u << 20] __attribute__((aligned(4096)));
static size_t arena_used;

void *plat_pages(size_t n) {
    n = (n + 4095) & ~(size_t)4095;
    if (arena_used + n > sizeof arena) return NULL;
    void *p = arena + arena_used;
    arena_used += n;
    memset(p, 0, n);
    return p;
}
void plat_pages_free(void *p, size_t n) { (void)p; (void)n; }

/* ---------------- temps et écran ---------------- */
static uint64_t sim_now;
static uint32_t *screen;
static int sw, sh;

uint64_t plat_ms(void) { return sim_now; }
int64_t plat_time(void) { return 1795530720 + (int64_t)(sim_now / 1000); }   /* 24/11/2026 14:32 */
void plat_log(const char *m) { (void)m; }
void plat_present(const uint32_t *scene, int stride, rect_t r) {
    for (int j = 0; j < r.h; j++)
        memcpy(screen + (size_t)(r.y + j) * sw + r.x, scene + (size_t)(r.y + j) * stride + r.x, (size_t)r.w * 4);
}

/* ---------------- fichiers d'exemple ---------------- */
static const char *home = "/media/titanos/Utilisateur";
const char *plat_home(void) { return home; }
const char *plat_config_path(void) { return "/media/titanos/titanos.cfg"; }
const char *plat_storage(void) { return "Clé USB TITANOS"; }

int plat_listdir(const char *path, dirent_t **out) {
    static const struct { const char *name; bool dir; uint64_t size; } home_ents[] = {
        { "Documents", true, 0 }, { "Images", true, 0 }, { "Musique", true, 0 }, { "Téléchargements", true, 0 },
        { "Bienvenue.txt", false, 1532 }, { "Idées pour TitanOs.txt", false, 842 }, { "vacances.png", false, 2400000 },
    };
    static const struct { const char *name; bool dir; uint64_t size; } docs[] = {
        { "Cours", true, 0 }, { "Liste de courses.txt", false, 214 }, { "Projet VAIO.md", false, 4210 },
    };
    dirent_t *d = calloc(16, sizeof(dirent_t));
    int n = 0;
    if (!strcmp(path, home))
        for (int i = 0; i < countof(home_ents); i++) { strlcpy(d[n].name, home_ents[i].name, 256); d[n].dir = home_ents[i].dir; d[n++].size = home_ents[i].size; }
    else if (str_ends(path, "/Documents"))
        for (int i = 0; i < countof(docs); i++) { strlcpy(d[n].name, docs[i].name, 256); d[n].dir = docs[i].dir; d[n++].size = docs[i].size; }
    *out = d;
    return n;
}
int plat_readfile(const char *path, char **data, size_t *len) {
    const char *t = "Bienvenue dans TitanOs (bêta 18) !\n\nCe Bloc-notes enregistre vos textes sur la clé USB.\n"
                    "Le système tourne sur le noyau Linux, mais tout ce que vous voyez est fait maison.\n";
    if (str_ends(path, ".cfg")) return -1;
    *len = strlen(t);
    *data = malloc(*len + 1);
    memcpy(*data, t, *len + 1);
    return 0;
}
int plat_writefile(const char *p, const void *d, size_t n) { (void)p; (void)d; (void)n; return 0; }
int plat_mkdir(const char *p) { (void)p; return 0; }
int plat_remove(const char *p) { (void)p; return 0; }
int plat_rename(const char *a, const char *b) { (void)a; (void)b; return 0; }
int plat_copy(const char *a, const char *b) { (void)a; (void)b; return 0; }
bool plat_exists(const char *p, bool *d) { (void)p; if (d) *d = true; return true; }

/* ---------------- terminal simulé ---------------- */
static bool pty_sent[8];
static int pty_next;
int plat_pty_open(int c, int r) { (void)c; (void)r; int id = pty_next++ % 8; pty_sent[id] = false; return id; }
int plat_pty_read(int id, char *buf, int n) {
    if (pty_sent[id & 7]) return 0;
    pty_sent[id & 7] = true;
    const char *s =
        "\x1b[1;35mTitanOs bêta 18\x1b[0m - noyau Linux 6.18.65 - BusyBox 1.37\r\n"
        "Tapez \x1b[1maide\x1b[0m pour la liste des commandes.\r\n\r\n"
        "\x1b[1;32mtitan\x1b[0m@\x1b[36mtitanos\x1b[0m:\x1b[1;34m~\x1b[0m$ ls\r\n"
        "\x1b[1;34mDocuments\x1b[0m  \x1b[1;34mImages\x1b[0m  \x1b[1;34mMusique\x1b[0m  Bienvenue.txt  vacances.png\r\n"
        "\x1b[1;32mtitan\x1b[0m@\x1b[36mtitanos\x1b[0m:\x1b[1;34m~\x1b[0m$ uname -a\r\n"
        "Linux titanos 6.18.65-0-lts #1-Alpine SMP PREEMPT_DYNAMIC x86_64 Linux\r\n"
        "\x1b[1;32mtitan\x1b[0m@\x1b[36mtitanos\x1b[0m:\x1b[1;34m~\x1b[0m$ free -m\r\n"
        "              total        used        free\r\n"
        "Mem:           3895         212        3683\r\n"
        "\x1b[1;32mtitan\x1b[0m@\x1b[36mtitanos\x1b[0m:\x1b[1;34m~\x1b[0m$ ";
    int l = (int)strlen(s);
    if (l > n) l = n;
    memcpy(buf, s, (size_t)l);
    return l;
}
void plat_pty_write(int id, const char *b, int n) { (void)id; (void)b; (void)n; }
void plat_pty_resize(int id, int c, int r) { (void)id; (void)c; (void)r; }
void plat_pty_close(int id) { (void)id; }
int plat_spawn(const char *const *argv) { (void)argv; return 1; }
int plat_spawn_poll(int id, char *out, int n) { (void)id; if (n) out[0] = 0; return 0; }

/* ---------------- système simulé (le VAIO) ---------------- */
void plat_sysinfo(sysinfo_t *s) {
    memset(s, 0, sizeof *s);
    strlcpy(s->kernel, "6.18.65-0-lts", sizeof s->kernel);
    strlcpy(s->cpu, "Intel Core i7 Q 720 @ 1.60GHz", sizeof s->cpu);
    strlcpy(s->gpu, "NVIDIA GeForce GT 330M", sizeof s->gpu);
    strlcpy(s->gpu_driver, "nouveau", sizeof s->gpu_driver);
    strlcpy(s->boot, "BIOS", sizeof s->boot);
    snprintf(s->display, sizeof s->display, "%d × %d, 32 bits", sw, sh);
    s->cores = 8;
    s->procs = 23;
    s->mem_total = 4084ull << 20;
    s->mem_free = 3700ull << 20;
    s->uptime_s = 754;
    s->cpu_temp = 52;
    s->gpu_temp = 61;
    s->cpu_mhz = 1596;
}
int plat_devices(device_t *l, int max) {
    static const char *d[][4] = {
        { "PCI", "8086:d132", "Intel Core i7 – contrôleur DMI", "" },
        { "PCI", "10de:0a29", "NVIDIA GeForce GT 330M", "nouveau" },
        { "PCI", "8086:3b3c", "Intel 5 Series – USB 2.0 (EHCI)", "ehci-pci" },
        { "PCI", "8086:3b56", "Intel 5 Series – High Definition Audio", "" },
        { "PCI", "11ab:4380", "Marvell Yukon 88E8057 – Ethernet", "sky2" },
        { "PCI", "8086:422c", "Intel Centrino Advanced-N 6200", "" },
        { "USB", "0951:1666", "Kingston DataTraveler (clé TITANOS)", "usb-storage" },
        { "USB", "8087:0020", "Intel concentrateur USB", "hub" },
    };
    int n = MIN(max, countof(d));
    for (int i = 0; i < n; i++) {
        strlcpy(l[i].bus, d[i][0], 8); strlcpy(l[i].id, d[i][1], 16);
        strlcpy(l[i].name, d[i][2], 96); strlcpy(l[i].driver, d[i][3], 32);
    }
    return n;
}
int plat_procs(proc_t *l, int max) {
    static const char *names[] = { "init", "titanos", "sh", "udhcpc", "kworker/0:1", "ksoftirqd/0" };
    int n = MIN(max, countof(names));
    for (int i = 0; i < n; i++) { l[i].pid = i < 2 ? i + 1 : 100 + i * 7; strlcpy(l[i].name, names[i], 32); l[i].rss_kb = i < 4 ? 900 + i * 4000 : 0; l[i].state = 'S'; }
    return n;
}
bool plat_kill(int pid) { (void)pid; return true; }
int plat_netifs(netif_t *l, int max) {
    if (max < 2) return 0;
    memset(l, 0, sizeof(netif_t) * 2);
    strlcpy(l[0].name, "eth0", 16); strlcpy(l[0].kind, "Ethernet", 16); strlcpy(l[0].mac, "54:42:49:a1:2b:3c", 18);
    l[0].up = true;
    strlcpy(l[1].name, "wlan0", 16); strlcpy(l[1].kind, "Wi-Fi", 16); strlcpy(l[1].mac, "00:27:10:5e:aa:01", 18);
    strlcpy(l[1].ip, "192.168.1.42", 16); l[1].up = l[1].carrier = true;
    return 2;
}
void plat_power(int a) { (void)a; }
static int bright = 70;
int plat_brightness(int set) { if (set >= 0) bright = set; return bright; }
void plat_sync(void) {}

/* son, batterie et Wi-Fi simulés */
static int volume = 70;
static bool muted;
void plat_sound(const int16_t *pcm, int frames) { (void)pcm; (void)frames; }
int  plat_volume(int set) { if (set >= 0) volume = set; return volume; }
bool plat_muted(int set) { if (set >= 0) muted = set != 0; return muted; }
EXPORT int sim_diag;
void plat_diag(char *buf, int n) {
    if (n) buf[0] = 0;
    if (sim_diag) snprintf(buf, (size_t)n, "Diagnostic · pilotes : 41 · clé : montée · entrées : 1 clavier, 1 souris, 1 pavé · événements : 0 · 1:07");
}
void plat_battery(battery_t *b) { b->present = true; b->charging = false; b->ac = false; b->percent = 76; b->minutes = 154; }
static const wifi_net_t nets[] = {
    { "Livebox-A3F2", 86, true, true, true }, { "Freebox-5G-Maison", 64, true, false, false },
    { "SFR_WiFi_FON", 48, false, false, false }, { "iPhone de Léa", 41, true, false, false },
    { "Bbox-7C1D9E", 22, true, false, false },
};
int  plat_wifi_status(char *ssid, int n, int *signal) { strlcpy(ssid, "Livebox-A3F2", (size_t)n); *signal = 86; return WIFI_CONNECTED; }
void plat_wifi_scan(void) {}
int  plat_wifi_list(wifi_net_t *l, int max) { int n = MIN(max, countof(nets)); memcpy(l, nets, sizeof(wifi_net_t) * (size_t)n); return n; }
bool plat_wifi_connect(const char *ssid, const char *psk) { (void)ssid; (void)psk; return true; }
void plat_wifi_forget(const char *ssid) { (void)ssid; }

/* ---------------- interface pour tools/preview.py ---------------- */
EXPORT void sim_init(int w, int h) {
    sw = w; sh = h;
    screen = plat_pages((size_t)w * h * 4);
    core_init(w, h);
}
EXPORT void sim_splash(const char *s, int p) { core_splash(s, p); }
EXPORT void sim_start(void) { core_start(); }
EXPORT void sim_time(uint64_t ms) { sim_now = ms; }
EXPORT int  sim_frame(void) { return core_frame(); }
EXPORT uint32_t *sim_pixels(void) { return screen; }
EXPORT void sim_key(int key, int ch, int mods, int down) {
    event_t e = { 0 };
    e.type = EV_KEY; e.key = key; e.ch = ch; e.mods = mods; e.down = down != 0;
    core_event(&e);
}
EXPORT void sim_move(int x, int y) {
    event_t e = { 0 };
    e.type = EV_MOTION; e.abs = true; e.x = x; e.y = y;
    core_event(&e);
}
EXPORT void sim_button(int b, int down) {
    event_t e = { 0 };
    e.type = EV_BUTTON; e.button = b; e.down = down != 0;
    core_event(&e);
}
EXPORT void sim_wheel(int dy) {
    event_t e = { 0 };
    e.type = EV_WHEEL; e.dy = dy;
    core_event(&e);
}
EXPORT void sim_open(int app) { app_open(app); }
rect_t wm_focused_rect(void);
EXPORT void sim_focused(int *r) { rect_t k = wm_focused_rect(); r[0] = k.x; r[1] = k.y; r[2] = k.w; r[3] = k.h; }

/* ---------------- mises à jour et installation (exemple) ---------------- */
void plat_update_check(bool automatic) { (void)automatic; }
int  plat_update_state(char *msg, int n, int *percent) {
    if (msg) strlcpy(msg, "TitanOs est à jour (" TITANOS_VERSION ").", (size_t)n);
    if (percent) *percent = -1;
    return UPD_UPTODATE;
}
bool plat_install_possible(void) { return true; }
int  plat_install_disks(disk_info_t *list, int max) {
    if (max < 1) return 0;
    memset(list, 0, sizeof *list);
    strlcpy(list->dev, "sda", sizeof list->dev);
    strlcpy(list->model, "ST9500325AS", sizeof list->model);
    list->bytes = 500107862016ull;
    if (max < 2) return 1;
    memset(list + 1, 0, sizeof *list);
    strlcpy(list[1].dev, "sdb", sizeof list[1].dev);
    strlcpy(list[1].model, "USB DISK 3.0", sizeof list[1].model);
    list[1].bytes = 15854469120ull;
    list[1].removable = true;
    return 2;
}
bool plat_install_start(const char *dev) { (void)dev; return true; }
int  plat_install_state(char *msg, int n, int *percent) { if (msg && n) msg[0] = 0; if (percent) *percent = -1; return UPD_IDLE; }

void plat_suspend(void) {}
void plat_rtc_save(void) {}
int  plat_volumes(void *list, int max) {
    volume_t *v = list;
    if (max < 1) return 0;
    strlcpy(v->name, "KINGSTON", sizeof v->name);
    strlcpy(v->path, "/media/KINGSTON", sizeof v->path);
    return 1;
}
bool plat_eject(const char *path) { (void)path; return true; }
void plat_random(void *buf, size_t n) { for (size_t i = 0; i < n; i++) ((uint8_t *)buf)[i] = (uint8_t)(i * 37 + 11); }
int  plat_browser_state(char *msg, int n, int *percent) {
    if (msg) strlcpy(msg, "Installation de Chromium… 112/249 (libdrm)", (size_t)n);
    if (percent) *percent = 45;
    return BR_INSTALLING;
}
bool plat_browser_install(void) { return true; }
bool plat_browser_start(int w, int h) { (void)w; (void)h; return false; }
bool plat_browser_frame(uint32_t *dst, int stride, int w, int h) { (void)dst; (void)stride; (void)w; (void)h; return false; }
bool plat_browser_running(void) { return false; }
void plat_browser_input(int type, int detail, int x, int y) { (void)type; (void)detail; (void)x; (void)y; }
void plat_browser_stop(void) {}
