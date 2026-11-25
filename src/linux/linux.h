/* TitanOs - partie Linux : init, pilotes, écran, entrées */
#pragma once
#include "sys.h"

extern bool g_hosted;                /* bureau lancé par systemd, dans la base Debian */

/* modules.c : chargement des pilotes du noyau */
void modules_init(void);
bool module_load(const char *name);
int  modules_coldplug(bool display_only);      /* nombre de pilotes chargés */
int  uevent_open(void);
void uevent_handle(int fd);
void audio_alsa_conf(void);
bool module_reload(const char *name);       /* retire puis recharge un pilote */              /* /etc/asound.conf : bonne sortie son pour les applications */

/* display.c : écran (DRM/KMS ou /dev/fb0) */
bool display_open(void);
void display_close(void);
void display_present(const uint32_t *scene, int stride, rect_t r);
bool display_cursor_image(const uint32_t *px, int stride, int w, int h, int hx, int hy);   /* px NULL : caché */
void display_cursor_move(int x, int y);
bool display_gamma(float r, float g, float b);
int  display_w(void);
int  display_hz(void);
bool display_flipped(void);               /* fréquence de l écran */
int  display_h(void);
const char *display_driver(void);
bool display_better_available(void);           /* une vraie carte graphique a pris le relais ? */

/* input.c : clavier, souris, pavé tactile */
void input_scan(void);
void input_describe(char *b, int n);
bool input_ok(void);
extern int input_events;
int  input_fill_poll(struct pollfd *p, int max);
void input_read(struct pollfd *p, int n);
extern int input_screen_w, input_screen_h;

/* audio.c, wifi.c */
void audio_start(void);
void volumes_poll(void);              /* clés USB : montage automatique */
void hw_start(void);                 /* veilleur : Wi-Fi et batterie, hors du bureau */
extern uint64_t display_us;          /* mesure du temps d envoi des images */
void bat_read(battery_t *b);

/* nm.c : Wi-Fi par NetworkManager (base Debian), utilisé par le veilleur */
void nm_start(void);
int  nm_state(char *ssid, int n, int *signal);
int  nm_list(wifi_net_t *list, int max);
void nm_scan(void);
void nm_connect(const char *ssid, const char *psk, const char *identity);   /* identity : WPA2-Entreprise */
void nm_forget(const char *ssid);
bool nm_child(int pid, int status);            /* fin d'une connexion lancée en arrière-plan */
const char *nm_last(void);

/* plat.c */
int  plat_user_groups(uint32_t *g, int max);   /* groupes du compte titan (/etc/group) */
void plat_set_storage(const char *home, const char *config, const char *storage);
void plat_tick(void);
