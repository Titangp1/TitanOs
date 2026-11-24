/* TitanOs - partie Linux : init, pilotes, écran, entrées */
#pragma once
#include "sys.h"

/* modules.c : chargement des pilotes du noyau */
void modules_init(void);
bool module_load(const char *name);
int  modules_coldplug(bool display_only);      /* nombre de pilotes chargés */
int  uevent_open(void);
void uevent_handle(int fd);

/* display.c : écran (DRM/KMS ou /dev/fb0) */
bool display_open(void);
void display_close(void);
void display_present(const uint32_t *scene, int stride, rect_t r);
int  display_w(void);
int  display_h(void);
const char *display_driver(void);
bool display_better_available(void);           /* une vraie carte graphique a pris le relais ? */

/* input.c : clavier, souris, pavé tactile */
void input_scan(void);
int  input_fill_poll(struct pollfd *p, int max);
void input_read(struct pollfd *p, int n);
extern int input_screen_w, input_screen_h;

/* audio.c, wifi.c */
void audio_start(void);
void wifi_start(void);

/* plat.c */
void plat_set_storage(const char *home, const char *config, const char *storage);
void plat_tick(void);
