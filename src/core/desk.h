/* TitanOs - bureau : fenêtres, réglages, widgets et applications */
#pragma once
#include "gfx.h"
#include "glass.h"

/* ---------------- icônes (icons.c) ---------------- */
enum {
    ICON_FILES, ICON_TERMINAL, ICON_NOTES, ICON_CALC, ICON_SETTINGS, ICON_SYSTEM,
    ICON_NETWORK, ICON_SNAKE, ICON_ABOUT,                     /* les applications */
    ICON_FOLDER, ICON_FILE, ICON_IMAGE, ICON_LOCK, ICON_REBOOT, ICON_POWER,
    ICON_HOME, ICON_USB, ICON_DISK, ICON_SEARCH, ICON_WIFI, ICON_ETHERNET, ICON_VOLUME, ICON_MUTE,
    ICON_BATTERY, ICON_BRIGHTNESS, ICON_COUNT
};
#define APP_COUNT 9
const char *app_name(int icon);
void        app_open(int icon);
void        draw_wifi(canvas_t *c, float cx, float cy, float s, int level, uint32_t col, uint32_t dim);  /* level 0-4 */
void        draw_battery(canvas_t *c, float x, float y, float w, float h, int percent, bool charging, uint32_t col);
void        draw_speaker(canvas_t *c, float cx, float cy, float s, int level, bool muted, uint32_t col);

/* ---------------- sons (sound.c) ---------------- */
enum { SFX_CLICK, SFX_TICK, SFX_OPEN, SFX_CLOSE, SFX_NOTIFY, SFX_ERROR, SFX_STARTUP, SFX_LOCK, SFX_UNLOCK, SFX_COUNT };
void  sound_init(void);
void  sound_play(int id);

/* ---------------- réglages (settings.c) ---------------- */
#define WALLPAPER_COUNT 6
#define ACCENT_COUNT    6
typedef struct {
    int  wallpaper, accent;
    char user[32];
    bool seconds;
    int  layout;          /* 0 AZERTY, 1 QWERTY */
    int  tz;              /* fuseau horaire (index de tz_list) */
    bool rtc_local;       /* horloge du PC réglée en heure locale (comme Windows) */
    int  brightness;      /* 0-100, -1 si non réglable */
    int  glass;           /* 0 clair, 1 normal, 2 dépoli */
    bool sounds;          /* sons de l'interface */
    int  volume;          /* volume général, 0-100 */
    int  pointer;         /* vitesse du pointeur, 1-5 */
} settings_t;
extern settings_t g_set;
void     settings_load(void);
void     settings_save(void);
uint32_t accent(void);
uint32_t accent_of(int i);
const char *wallpaper_name(int i);
void     wallpaper_render(canvas_t *c, int idx);
void     wallpaper_thumb(canvas_t *c, int x, int y, int w, int h, int idx);

typedef struct { const char *name; int offset_min; int dst; } tz_t;   /* dst : 0 aucune, 1 Europe, 2 Amérique */
extern const tz_t tz_list[];
extern const int tz_count;
typedef struct { int year, month, day, hour, min, sec, wday; } datetime_t;   /* wday : 0 = lundi */
void     local_now(datetime_t *dt);
void     datetime_from(int64_t t, datetime_t *dt);
extern const char *const month_names[12];
extern const char *const day_names[7];

/* ---------------- fenêtres (wm.c) ---------------- */
#define TITLE_H 44
#define WIN_R   16
enum { WF_RESIZABLE = 1, WF_DIALOG = 2, WF_SINGLE = 4 };

typedef struct window window_t;
struct window {
    bool     used, minimized, maximized, dirty, glass_ok, closing;
    int      anim;                  /* animation en cours : ouverture, fermeture, réduction */
    uint64_t anim_t0;
    unsigned flags;
    char     title[80];
    int      icon;
    rect_t   r, restore;            /* cadre complet, à l'écran */
    int      minw, minh;
    canvas_t surf;                  /* contenu (zone client) */
    canvas_t glass;                 /* verre en cache */
    int      mx, my;                /* pointeur dans la zone client (-1 : dehors) */
    window_t *owner;                /* pour les dialogues */
    void    *data;
    void   (*draw)(window_t *w, canvas_t *c);
    void   (*event)(window_t *w, const event_t *e);
    void   (*tick)(window_t *w, uint64_t now);
    void   (*on_close)(window_t *w);
};

extern int  scr_w, scr_h;
window_t *wm_open(const char *title, int icon, int w, int h, unsigned flags);
void      wm_close(window_t *w);
void      wm_invalidate(window_t *w);
void      wm_set_title(window_t *w, const char *t);
void      wm_resize(window_t *w, int client_w, int client_h);
void      wm_focus(window_t *w);
window_t *wm_focused(void);
window_t *wm_find(int icon);
rect_t    wm_client(window_t *w);
void      wm_toast(const char *msg);
void      wm_menu(int x, int y, const char *const *items, int n, void (*cb)(int i, void *ctx), void *ctx);
void      wm_lock(void);
void      wm_theme_changed(void);
void      wm_power(int action);          /* 0 éteindre, 1 redémarrer, avec confirmation */
void      clip_set(const char *s, int n);
const char *clip_get(void);
bool      wm_kbd_layout_toggle(void);

/* ---------------- widgets (ui.c) ---------------- */
/* couleurs du thème sombre en verre */
#define C_TEXT     0xFFF8FAFC
#define C_TEXT2    0xFFCBD5E1
#define C_MUTED    0xFF94A3B8
#define C_PANEL    0x1AFFFFFF      /* panneau translucide */
#define C_PANEL2   0x2EFFFFFF
#define C_LINE     0x24FFFFFF
#define C_FIELD    0x40000000
#define C_DANGER   0xFFEF4444

enum { BTN_NORMAL, BTN_ACCENT, BTN_DANGER, BTN_FLAT };
void ui_panel(canvas_t *c, rect_t r, float radius);
void ui_button(canvas_t *c, rect_t r, const char *label, int style, bool hover);
void ui_icon_button(canvas_t *c, rect_t r, int icon, bool hover);
void ui_toggle(canvas_t *c, rect_t r, bool on);
void ui_slider(canvas_t *c, rect_t r, int value);         /* 0-100 */
int  ui_slider_value(rect_t r, int x);
void ui_segments(canvas_t *c, rect_t r, const char *const *items, int n, int sel, int hover);
int  ui_segment_at(rect_t r, int n, int x, int y);
void ui_scrollbar(canvas_t *c, rect_t r, int total, int visible, int offset);
int  ui_wrap(const font_t *f, const char *s, int maxw, int *starts, int *lens, int max);

/* champ de texte d'une ligne */
typedef struct { char s[256]; int cur; bool focus, secret; } field_t;
void ui_field(canvas_t *c, rect_t r, field_t *f, const char *placeholder);
bool ui_field_key(field_t *f, const event_t *e);            /* true si le texte a changé */
void ui_field_set(field_t *f, const char *s);

/* dialogues */
void dlg_message(const char *title, const char *text);
void dlg_confirm(const char *title, const char *text, const char *ok, void (*cb)(bool ok, void *ctx), void *ctx);
void dlg_input(const char *title, const char *label, const char *initial, void (*cb)(const char *text, void *ctx), void *ctx);
void dlg_password(const char *title, const char *label, void (*cb)(const char *text, void *ctx), void *ctx);

/* ---------------- applications ---------------- */
void app_files_open(const char *path);
void app_notes_open(const char *path);
void app_terminal_open(void);
void app_calc_open(void);
void app_settings_open(void);
void app_system_open(void);
void app_network_open(void);
void app_snake_open(void);
void app_about_open(void);
