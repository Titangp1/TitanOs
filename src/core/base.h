/* TitanOs - en-tête commun du bureau
 *
 * Le dossier src/core/ ne dépend d'aucun système : il dessine dans un tampon
 * mémoire et appelle les fonctions plat_*() fournies par src/linux/ (le vrai
 * système) ou src/sim/ (aperçu sous Windows pour les captures d'écran). */
#pragma once

typedef __UINT8_TYPE__   uint8_t;
typedef __UINT16_TYPE__  uint16_t;
typedef __UINT32_TYPE__  uint32_t;
typedef __UINT64_TYPE__  uint64_t;
typedef __INT8_TYPE__    int8_t;
typedef __INT16_TYPE__   int16_t;
typedef __INT32_TYPE__   int32_t;
typedef __INT64_TYPE__   int64_t;
typedef __SIZE_TYPE__    size_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __INTPTR_TYPE__  intptr_t;
typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, t)   __builtin_va_arg(v, t)
#define bool  _Bool
#define true  1
#define false 0
#define NULL  ((void *)0)

#define TITANOS_VERSION "Bêta 31"
#define TITANOS_BUILD   31             /* numéro de version, comparé aux mises à jour publiées */
#define countof(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

/* ---------------- lib.c ---------------- */
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
int    strcasecmp(const char *a, const char *b);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *h, const char *n);
size_t strlcpy(char *d, const char *s, size_t n);
size_t strlcat(char *d, const char *s, size_t n);
char  *strdup(const char *s);
int    vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int    snprintf(char *buf, size_t n, const char *fmt, ...);
long   strtol(const char *s, char **end, int base);
int    atoi(const char *s);
bool   str_starts(const char *s, const char *prefix);
bool   str_ends(const char *s, const char *suffix);
bool   str_icontains(const char *hay, const char *needle);   /* sans casse ni accents */
void   fmt_size(char *buf, size_t n, uint64_t bytes);         /* "12,3 Mo" */

int    utf8_decode(const char **s);        /* code du caractère suivant, 0 à la fin */
int    utf8_encode(char *out, int cp);     /* écrit 1 à 4 octets, renvoie la longueur */
int    utf8_prev(const char *s, int pos);  /* début du caractère précédent */
int    utf8_next(const char *s, int pos);  /* début du caractère suivant */
int    utf8_len(const char *s);            /* nombre de caractères */
int    utf8_len_n(const char *s, int bytes);

void  *malloc(size_t n);
void  *calloc(size_t n, size_t sz);
void  *realloc(void *p, size_t n);
void   free(void *p);
void   sort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *));
float  fsin(float x);                       /* sinus et cosinus sans libm */
float  floorf(float x);
float  ceilf(float x);
float  fcos(float x);

/* ---------------- rectangles ---------------- */
typedef struct { int x, y, w, h; } rect_t;
static inline rect_t R(int x, int y, int w, int h) { rect_t r = { x, y, w, h }; return r; }
static inline bool rect_empty(rect_t r) { return r.w <= 0 || r.h <= 0; }
static inline bool rect_has(rect_t r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
static inline rect_t rect_grow(rect_t r, int d) { return R(r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d); }
rect_t rect_isect(rect_t a, rect_t b);
rect_t rect_union(rect_t a, rect_t b);
static inline bool rect_hits(rect_t a, rect_t b) { return !rect_empty(rect_isect(a, b)); }

/* ---------------- événements ---------------- */
enum { EV_KEY = 1, EV_MOTION, EV_BUTTON, EV_WHEEL };
enum { MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4, MOD_ALTGR = 8, MOD_SUPER = 16 };
enum { BTN_LEFT = 1, BTN_RIGHT = 2, BTN_MIDDLE = 3 };
enum {
    K_NONE, K_CHAR, K_ESC, K_ENTER, K_BACKSPACE, K_TAB, K_DELETE, K_INSERT, K_HOME, K_END,
    K_PGUP, K_PGDN, K_LEFT, K_RIGHT, K_UP, K_DOWN,
    K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12,
    K_SUPER, K_POWER, K_BRIGHT_UP, K_BRIGHT_DOWN, K_VOL_UP, K_VOL_DOWN, K_MUTE, K_LID_CLOSE,
    K_PRINT,
};
typedef struct {
    int  type;
    int  key;          /* K_* ; K_CHAR : caractère dans ch */
    int  ch;           /* caractère Unicode produit (selon la disposition) */
    bool down;         /* touche ou bouton enfoncé */
    int  mods;         /* MOD_* */
    int  x, y;         /* position (absolue) du pointeur */
    int  dx, dy;       /* déplacement relatif, ou crans de molette */
    bool abs;          /* EV_MOTION : x,y absolus (tablette) plutôt que dx,dy */
    int  button;       /* BTN_* */
} event_t;

/* ---------------- plateforme ---------------- */
uint64_t plat_ms(void);                       /* horloge monotone, en ms */
int64_t  plat_time(void);                     /* horloge système, secondes depuis 1970 */
void    *plat_pages(size_t n);
void     plat_pages_free(void *p, size_t n);
void     plat_present(const uint32_t *scene, int stride, rect_t r);
void     plat_log(const char *msg);

typedef struct { char name[256]; bool dir; uint64_t size; int64_t mtime; } dirent_t;
int   plat_listdir(const char *path, dirent_t **out);       /* nombre d'entrées (malloc) ou -1 */
int   plat_readfile(const char *path, char **data, size_t *len);
int   plat_writefile(const char *path, const void *data, size_t len);
int   plat_mkdir(const char *path);
int   plat_remove(const char *path);                        /* récursif pour un dossier */
int   plat_rename(const char *from, const char *to);
int   plat_copy(const char *from, const char *to);
bool  plat_exists(const char *path, bool *is_dir);
const char *plat_home(void);                                /* dossier de l'utilisateur */
const char *plat_config_path(void);                         /* fichier des réglages */
const char *plat_storage(void);                             /* où sont gardés les fichiers */

int   plat_pty_open(int cols, int rows);                    /* lance le shell, renvoie un identifiant */
int   plat_pty_read(int id, char *buf, int n);              /* >0 lu, 0 rien, -1 terminé */
void  plat_pty_write(int id, const char *buf, int n);
void  plat_pty_resize(int id, int cols, int rows);
void  plat_pty_close(int id);
int   plat_spawn(const char *const *argv);                  /* commande en arrière-plan : identifiant */
int   plat_spawn_poll(int id, char *out, int outsz);        /* -1 en cours, sinon code de sortie */

typedef struct {
    char kernel[64], cpu[96], gpu[96], gpu_driver[32], boot[32], display[48];
    int cores, procs;
    int cpu_temp, gpu_temp, cpu_mhz;                        /* -1 si inconnu */
    uint64_t mem_total, mem_free, uptime_s;
} sysinfo_t;
void  plat_sysinfo(sysinfo_t *s);
typedef struct { char bus[8], id[16], name[96], driver[32]; } device_t;
int   plat_devices(device_t *list, int max);
typedef struct { int pid; char name[32]; uint64_t rss_kb; char state; } proc_t;
int   plat_procs(proc_t *list, int max);
bool  plat_kill(int pid);
typedef struct { char name[16], mac[18], ip[16], kind[16]; bool up, carrier, internet; } netif_t;   /* internet : route par défaut */
int   plat_netifs(netif_t *list, int max);
void  plat_power(int action);                               /* 0 éteindre, 1 redémarrer */
void  plat_suspend(void);                                   /* mise en veille */
int   plat_volumes(void *list, int max);                  /* volumes branchés (volume_t) */
bool  plat_eject(const char *path);                       /* démonte un volume */
void  plat_random(void *buf, size_t n);                    /* octets aléatoires */
void  plat_rtc_save(void);
/* navigateur Google Chrome (Flatpak, serveur X en mémoire) */
enum { BR_NONE, BR_INSTALLING, BR_READY, BR_ERROR };
int   plat_browser_state(char *msg, int n, int *percent);   /* BR_* */
bool  plat_browser_install(void);
bool  plat_browser_start(int w, int h);
bool  plat_browser_frame(uint32_t *dst, int stride, int w, int h);   /* true si l'image a changé */
bool  plat_browser_running(void);
void  plat_browser_input(int type, int detail, int x, int y);       /* événement X11 (XTEST) */
void  plat_browser_stop(void);
void  plat_browser_resize(int w, int h);
void  plat_browser_open_url(const char *url);              /* nouvel onglet (ou au démarrage) */
bool  plat_browser_is_chrome(void);                         /* Google Chrome installé (sinon : rien, ou l'ancien Chromium) */
/* installations d'applications (apk, Flatpak) : une à la fois, dans l'ordre */
enum { INS_NONE, INS_WAITING, INS_RUNNING, INS_DONE, INS_FAILED };
bool  plat_app_install(const char *id, const char *script); /* script : commandes shell */
int   plat_app_state(const char *id, char *msg, int n, int *percent);   /* INS_* */
/* applications X11 : chacune sur son propre serveur X en mémoire ; poignée >= 0 */
int   plat_xapp_start(const char *cmd, int w, int h);       /* -1 : impossible */
bool  plat_xapp_frame(int x, uint32_t *dst, int stride, int w, int h);   /* true si l'image a changé */
bool  plat_xapp_running(int x);
void  plat_xapp_input(int x, int type, int detail, int px, int py);
void  plat_xapp_resize(int x, int w, int h);
void  plat_xapp_manage(int x);                              /* place les nouvelles fenêtres de l'application */
bool  plat_xapp_close(int x);                               /* demande polie (WM_DELETE_WINDOW) */
void  plat_xapp_stop(int x);
#define BROWSER_XAPP 0                                      /* le navigateur : poignée 0 */
/* pointeur matériel (plan « curseur » de la carte graphique) */
void  plat_cursor_move(int x, int y);
int   plat_brightness(int set);                             /* set<0 : lecture ; 0-100 ou -1 */
void  plat_sync(void);

/* son : PCM 48 kHz stéréo 16 bits ; volume 0-100 (-1 : pas de carte son) */
void  plat_sound(const int16_t *pcm, int frames);
int   plat_volume(int set);
bool  plat_muted(int set);                                  /* set<0 : lecture ; 0/1 : réglage */

typedef struct { bool present, charging, ac; int percent, minutes; } battery_t;
void  plat_battery(battery_t *b);
void  plat_diag(char *buf, int n);                   /* ligne de diagnostic (bêta), vide sinon */

/* mises à jour (GitHub) et installation sur le disque interne */
enum { UPD_IDLE, UPD_CHECKING, UPD_UPTODATE, UPD_DOWNLOADING, UPD_INSTALLING, UPD_DONE, UPD_ERROR };
typedef struct { char dev[16]; char model[48]; uint64_t bytes; bool removable, current; } disk_info_t;
void  plat_update_check(bool automatic);                   /* recherche, télécharge et installe */
int   plat_update_state(char *msg, int n, int *percent);   /* UPD_* ; percent -1 si inconnu */
bool  plat_install_possible(void);                         /* démarré depuis une clé USB */
int   plat_install_disks(disk_info_t *list, int max);      /* disques et clés utilisables (sauf celui en cours) */
bool  plat_install_start(const char *dev);                 /* efface le disque et y copie TitanOs */
int   plat_install_state(char *msg, int n, int *percent);
int   plat_update_files(char paths[][160], int max);        /* paquets titanos-maj.tar.gz sur les clés */
bool  plat_update_from_file(const char *path);             /* installe un paquet posé sur une clé */
bool  plat_other_titanos(char *desc, int n);                /* un autre TitanOs (disque interne) ? */
bool  plat_update_other(void);                              /* lui donne la version de cette clé */

/* Wi-Fi (wpa_supplicant) */
/* enterprise : WPA2-Entreprise (identifiant + mot de passe) ; wpa3 : WPA3 seul (SAE) */
typedef struct { char ssid[33]; int signal; bool secure, known, current, enterprise, wpa3; } wifi_net_t;
enum { WIFI_NONE = -1, WIFI_OFF, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_BLOCKED, WIFI_REFUSED };   /* BLOCKED : interrupteur sur OFF ; REFUSED : mot de passe refusé */
int   plat_wifi_status(char *ssid, int n, int *signal);     /* WIFI_* ; signal 0-100 */
void  plat_wifi_scan(void);
int   plat_wifi_list(wifi_net_t *list, int max);
bool  plat_wifi_connect(const char *ssid, const char *psk); /* psk NULL : réseau ouvert ou connu */
bool  plat_wifi_connect_eap(const char *ssid, const char *identity, const char *password);   /* WPA2-Entreprise */
void  plat_wifi_last(char *buf, int n);                     /* dernière étape ou erreur de connexion, en clair */
/* console réseau : lignes du journal du noyau qui concernent le réseau ; 1 tant qu'il y en a */
int   plat_netlog_next(char *line, int n, uint64_t *usec);
void  plat_netlog_rewind(void);                             /* la prochaine lecture repart du début */
void  plat_wifi_forget(const char *ssid);

/* ---------------- bureau (appelé par la plateforme) ---------------- */
void core_init(int w, int h);
void core_resize(int w, int h);
void core_splash(const char *step, int percent);           /* texte de l'écran de démarrage */
void core_start(void);
void core_event(const event_t *e);
bool core_raw_key(int code, bool down, int mods);   /* touche brute : true si une fenêtre la prend */
bool core_raw_key_wanted(void);                     /* la fenêtre active veut-elle les touches brutes ? */
int  core_frame(void);                                       /* renvoie l'attente conseillée (ms) */
