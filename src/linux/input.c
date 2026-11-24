/* TitanOs - entrées : claviers, souris, pavés tactiles et tablettes (evdev)
 *
 * Le noyau fournit les événements bruts (/dev/input/eventN). On traduit ici les
 * touches avec les dispositions AZERTY et QWERTY de TitanOs (les codes
 * de touches Linux sont les scancodes PS/2), avec les touches mortes ^ et ¨. */
#include "linux.h"
#include <linux/input.h>
#include "../core/desk.h"

int input_screen_w = 1024, input_screen_h = 768;

/* boutons du bureau (base.h) : les noms BTN_* sont repris par le noyau pour ses propres codes */
#define BTN_LEFT_   1
#define BTN_RIGHT_  2
#define BTN_MIDDLE_ 3

/* ---------------- dispositions (codes de touches Linux = scancodes « set 1 ») ---------------- */
#define DEAD_CIRC 0x01
#define DEAD_UML  0x02
static const uint16_t fr_norm[0x59] = {
    [0x02] = '&', [0x03] = 0xE9, [0x04] = '"', [0x05] = '\'', [0x06] = '(', [0x07] = '-',
    [0x08] = 0xE8, [0x09] = '_', [0x0A] = 0xE7, [0x0B] = 0xE0, [0x0C] = ')', [0x0D] = '=',
    [0x10] = 'a', [0x11] = 'z', [0x12] = 'e', [0x13] = 'r', [0x14] = 't', [0x15] = 'y',
    [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p', [0x1A] = DEAD_CIRC, [0x1B] = '$',
    [0x1E] = 'q', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h',
    [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = 'm', [0x28] = 0xF9, [0x29] = 0xB2,
    [0x2B] = '*',
    [0x2C] = 'w', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n',
    [0x32] = ',', [0x33] = ';', [0x34] = ':', [0x35] = '!',
    [0x37] = '*', [0x39] = ' ',
    [0x47] = '7', [0x48] = '8', [0x49] = '9', [0x4A] = '-', [0x4B] = '4', [0x4C] = '5',
    [0x4D] = '6', [0x4E] = '+', [0x4F] = '1', [0x50] = '2', [0x51] = '3', [0x52] = '0',
    [0x53] = '.', [0x56] = '<',
};
static const uint16_t fr_shift[0x59] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5', [0x07] = '6',
    [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0', [0x0C] = 0xB0, [0x0D] = '+',
    [0x10] = 'A', [0x11] = 'Z', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y',
    [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P', [0x1A] = DEAD_UML, [0x1B] = 0xA3,
    [0x1E] = 'Q', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
    [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = 'M', [0x28] = '%',
    [0x2B] = 0xB5,
    [0x2C] = 'W', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N',
    [0x32] = '?', [0x33] = '.', [0x34] = '/', [0x35] = 0xA7,
    [0x37] = '*', [0x39] = ' ', [0x56] = '>',
};
static const uint16_t fr_altgr[0x59] = {
    [0x03] = '~', [0x04] = '#', [0x05] = '{', [0x06] = '[', [0x07] = '|', [0x08] = '`',
    [0x09] = '\\', [0x0A] = '^', [0x0B] = '@', [0x0C] = ']', [0x0D] = '}',
    [0x12] = 0x20AC, [0x1B] = 0xA4,
};
static const uint16_t us_norm[0x59] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5', [0x07] = '6',
    [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't', [0x15] = 'y',
    [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h',
    [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n',
    [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x37] = '*', [0x39] = ' ',
    [0x47] = '7', [0x48] = '8', [0x49] = '9', [0x4A] = '-', [0x4B] = '4', [0x4C] = '5',
    [0x4D] = '6', [0x4E] = '+', [0x4F] = '1', [0x50] = '2', [0x51] = '3', [0x52] = '0',
    [0x53] = '.', [0x56] = '\\',
};
static const uint16_t us_shift[0x59] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%', [0x07] = '^',
    [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y',
    [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
    [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N',
    [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?',
    [0x37] = '*', [0x39] = ' ', [0x56] = '|',
};

static int mods, dead;
static bool caps, super_used;

static int compose(int d, int c) {
    static const char base[] = "aeiouAEIOU";
    static const uint8_t circ[] = { 0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0xC2, 0xCA, 0xCE, 0xD4, 0xDB };
    static const uint8_t uml[] = { 0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xC4, 0xCB, 0xCF, 0xD6, 0xDC };
    for (int i = 0; i < 10; i++) if (base[i] == c) return d == DEAD_CIRC ? circ[i] : uml[i];
    if (d == DEAD_UML && c == 'y') return 0xFF;
    return 0;
}

static int special_key(int code) {
    switch (code) {
    case KEY_ESC: return K_ESC;
    case KEY_ENTER: case KEY_KPENTER: return K_ENTER;
    case KEY_BACKSPACE: return K_BACKSPACE;
    case KEY_TAB: return K_TAB;
    case KEY_DELETE: return K_DELETE;
    case KEY_INSERT: return K_INSERT;
    case KEY_HOME: return K_HOME;
    case KEY_END: return K_END;
    case KEY_PAGEUP: return K_PGUP;
    case KEY_PAGEDOWN: return K_PGDN;
    case KEY_LEFT: return K_LEFT;
    case KEY_RIGHT: return K_RIGHT;
    case KEY_UP: return K_UP;
    case KEY_DOWN: return K_DOWN;
    case KEY_LEFTMETA: case KEY_RIGHTMETA: return K_SUPER;
    case KEY_POWER: return K_POWER;
    case KEY_BRIGHTNESSUP: return K_BRIGHT_UP;
    case KEY_BRIGHTNESSDOWN: return K_BRIGHT_DOWN;
    case KEY_VOLUMEUP: return K_VOL_UP;
    case KEY_VOLUMEDOWN: return K_VOL_DOWN;
    case KEY_MUTE: return K_MUTE;
    case KEY_KPSLASH: return K_CHAR;
    }
    if (code >= KEY_F1 && code <= KEY_F10) return K_F1 + (code - KEY_F1);
    if (code == KEY_F11) return K_F11;
    if (code == KEY_F12) return K_F12;
    return K_NONE;
}

static void emit(event_t *e) { e->mods = mods; core_event(e); }

static void key_event(int code, int value) {
    bool down = value != 0;
    int bit = 0;
    switch (code) {
    case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: bit = MOD_SHIFT; break;
    case KEY_LEFTCTRL: case KEY_RIGHTCTRL: bit = MOD_CTRL; break;
    case KEY_LEFTALT: bit = MOD_ALT; break;
    case KEY_RIGHTALT: bit = g_set.layout == 0 ? MOD_ALTGR : MOD_ALT; break;
    case KEY_LEFTMETA: case KEY_RIGHTMETA:
        if (down) mods |= MOD_SUPER; else mods &= ~MOD_SUPER;
        break;
    }
    if (bit) { if (down) mods |= bit; else mods &= ~bit; return; }
    if (code == KEY_CAPSLOCK) { if (value == 1) caps = !caps; return; }

    if ((mods & MOD_SUPER) && code != KEY_LEFTMETA && code != KEY_RIGHTMETA) super_used = true;
    event_t e = { 0 };
    e.type = EV_KEY;
    e.down = down;
    int sk = special_key(code);
    if (code == KEY_KPSLASH) { e.key = K_CHAR; e.ch = '/'; emit(&e); return; }
    if (sk != K_NONE) {
        /* la touche Windows seule ouvre le menu : on la signale au relâchement */
        if (sk == K_SUPER) {
            if (down) { super_used = false; return; }
            if (super_used) return;
            e.down = true;
        }
        e.key = sk;
        emit(&e);
        return;
    }
    if (code >= 0x59) return;
    bool fr = g_set.layout == 0;
    bool altgr = (mods & MOD_ALTGR) || (fr && (mods & (MOD_CTRL | MOD_ALT)) == (MOD_CTRL | MOD_ALT));
    bool shift = (mods & MOD_SHIFT) != 0;
    int ch;
    if (altgr && fr) ch = fr_altgr[code];
    else ch = shift ? (fr ? fr_shift : us_shift)[code] : (fr ? fr_norm : us_norm)[code];
    if (!ch) return;
    if (caps && ch < 0x100 && ((ch >= 'a' && ch <= 'z') || (ch >= 0xE0 && ch <= 0xFE && ch != 0xF7))) ch -= 32;
    else if (caps && ch < 0x100 && ((ch >= 'A' && ch <= 'Z') && shift)) ch += 32;
    if (!down) { e.key = K_CHAR; e.ch = ch; emit(&e); return; }
    if (ch == DEAD_CIRC || ch == DEAD_UML) {
        if (dead) { e.key = K_CHAR; e.ch = dead == DEAD_CIRC ? '^' : 0xA8; emit(&e); dead = 0; return; }
        dead = ch;
        return;
    }
    if (dead && !(mods & MOD_CTRL)) {
        int c = compose(dead, ch);
        int d = dead;
        dead = 0;
        if (c) ch = c;
        else {
            e.key = K_CHAR; e.ch = d == DEAD_CIRC ? '^' : 0xA8; emit(&e);
            if (ch == ' ') return;
        }
    }
    /* avec Ctrl, on transmet la lettre de base (Ctrl+C, Ctrl+V...) */
    e.key = K_CHAR;
    e.ch = ch;
    if ((mods & MOD_CTRL) && !altgr) {
        int base = (fr ? fr_norm : us_norm)[code];
        if (base >= 'a' && base <= 'z') e.ch = shift ? base - 32 : base;
    }
    emit(&e);
}

/* ---------------- périphériques ---------------- */
enum { DEV_KBD = 1, DEV_MOUSE = 2, DEV_TOUCHPAD = 4, DEV_ABS = 8, DEV_KEYS = 16, DEV_LID = 32 };
typedef struct {
    int fd, kind, num;
    int xmin, xmax, ymin, ymax, res;
    /* pavé tactile */
    bool touch, two, moved;
    int lx, ly, x, y, sx, sy, fingers, scroll_acc;
    uint64_t t_down;
    float fx, fy;
    int rel_x, rel_y;
} dev_t_;

#define MAX_DEV 24
static dev_t_ devs[MAX_DEV];
static int ndevs;

#define TEST(bits, b) (((bits)[(b) / 8] >> ((b) % 8)) & 1)

static bool has_dev(int num) {
    for (int i = 0; i < ndevs; i++) if (devs[i].num == num) return true;
    return false;
}

static void add_dev(const char *name, int type, void *ctx) {
    (void)type; (void)ctx;
    if (!str_starts(name, "event")) return;
    int num = atoi(name + 5);
    if (has_dev(num) || ndevs >= MAX_DEV) return;
    char p[64];
    snprintf(p, sizeof p, "/dev/input/%s", name);
    int fd = sys_open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC, 0);
    if (fd < 0) return;
    uint8_t ev[4] = { 0 }, keys[KEY_MAX / 8 + 1] = { 0 }, rel[4] = { 0 }, abs_[ABS_MAX / 8 + 1] = { 0 }, props[4] = { 0 };
    sys_ioctl(fd, EVIOCGBIT(0, sizeof ev), ev);
    sys_ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
    sys_ioctl(fd, EVIOCGBIT(EV_REL, sizeof rel), rel);
    sys_ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs_), abs_);
    sys_ioctl(fd, EVIOCGPROP(sizeof props), props);
    dev_t_ d = { 0 };
    d.fd = fd;
    d.num = num;
    if (TEST(ev, EV_KEY) && TEST(keys, KEY_A) && TEST(keys, KEY_Z)) d.kind |= DEV_KBD;
    /* touches spéciales seules : bouton d'alimentation, touches Fn du VAIO (volume, luminosité) */
    if (TEST(ev, EV_KEY) && (TEST(keys, KEY_POWER) || TEST(keys, KEY_VOLUMEUP) || TEST(keys, KEY_BRIGHTNESSUP) || TEST(keys, KEY_MUTE)))
        d.kind |= DEV_KEYS;
    uint8_t sw[4] = { 0 };
    sys_ioctl(fd, EVIOCGBIT(EV_SW, sizeof sw), sw);
    if (TEST(ev, EV_SW) && TEST(sw, SW_LID)) d.kind |= DEV_LID;
    if (TEST(ev, EV_REL) && TEST(rel, REL_X) && TEST(rel, REL_Y)) d.kind |= DEV_MOUSE;
    if (TEST(ev, EV_ABS) && TEST(abs_, ABS_X) && TEST(abs_, ABS_Y)) {
        struct input_absinfo ax, ay;
        sys_ioctl(fd, EVIOCGABS(ABS_X), &ax);
        sys_ioctl(fd, EVIOCGABS(ABS_Y), &ay);
        d.xmin = ax.minimum; d.xmax = ax.maximum; d.ymin = ay.minimum; d.ymax = ay.maximum;
        d.res = ax.resolution;
        if (TEST(keys, BTN_TOOL_FINGER) && !TEST(props, INPUT_PROP_DIRECT)) d.kind |= DEV_TOUCHPAD;
        else if (TEST(keys, BTN_LEFT) || TEST(keys, BTN_TOUCH)) d.kind |= DEV_ABS;
    }
    if (!d.kind) { sys_close(fd); return; }
    devs[ndevs++] = d;
    char nm[80] = { 0 };
    sys_ioctl(fd, EVIOCGNAME(sizeof nm - 1), nm);
    klog("entrée %s : %s (%s%s%s%s)", name, nm, d.kind & DEV_KBD ? "clavier " : "", d.kind & DEV_MOUSE ? "souris " : "",
         d.kind & DEV_TOUCHPAD ? "pavé tactile " : "", d.kind & DEV_ABS ? "pointeur absolu" : "");
}

void input_scan(void) { for_each_dir("/dev/input", add_dev, NULL); }

int input_events;
bool input_ok(void) {
    bool k = false, m = false;
    for (int i = 0; i < ndevs; i++) {
        if (devs[i].kind & DEV_KBD) k = true;
        if (devs[i].kind & (DEV_MOUSE | DEV_ABS | DEV_TOUCHPAD)) m = true;
    }
    return k && m;
}
void input_describe(char *b, int n) {
    int k = 0, m = 0, t = 0;
    for (int i = 0; i < ndevs; i++) {
        if (devs[i].kind & DEV_KBD) k++;
        if (devs[i].kind & (DEV_MOUSE | DEV_ABS)) m++;
        if (devs[i].kind & DEV_TOUCHPAD) t++;
    }
    snprintf(b, (size_t)n, "%d clavier%s, %d souris, %d pavé%s", k, k > 1 ? "s" : "", m, t, t > 1 ? "s" : "");
}

int input_fill_poll(struct pollfd *p, int max) {
    int n = 0;
    for (int i = 0; i < ndevs && n < max; i++) { p[n].fd = devs[i].fd; p[n].events = POLLIN; p[n].revents = 0; n++; }
    return n;
}

static void button(int b, bool down) {
    event_t e = { 0 };
    e.type = EV_BUTTON;
    e.button = b;
    e.down = down;
    emit(&e);
}

static void move_rel(int dx, int dy) {
    if (!dx && !dy) return;
    event_t e = { 0 };
    e.type = EV_MOTION;
    e.dx = dx;
    e.dy = dy;
    emit(&e);
}

static uint64_t now_ms(void) {
    struct timespec ts;
    sys_clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* pavé tactile : un doigt déplace, un tapotement clique, deux doigts font défiler */
static void touchpad_sync(dev_t_ *d) {
    float range = (float)MAX(1, d->xmax - d->xmin);
    float mm = d->res > 0 ? (float)d->res : range / 100.0f;      /* unités par millimètre */
    if (d->touch) {
        if (d->lx >= 0) {
            float dx = (float)(d->x - d->lx) / mm, dy = (float)(d->y - d->ly) / mm;
            if (d->fingers >= 2) {
                d->scroll_acc += (int)((d->y - d->ly) * 10 / mm);
                while (d->scroll_acc >= 25 || d->scroll_acc <= -25) {
                    event_t e = { 0 };
                    e.type = EV_WHEEL;
                    e.dy = d->scroll_acc > 0 ? 1 : -1;
                    emit(&e);
                    d->scroll_acc -= d->scroll_acc > 0 ? 25 : -25;
                }
            } else {
                float speed = __builtin_sqrtf(dx * dx + dy * dy);
                float gain = 3.2f + MIN(speed, 6.0f) * 1.3f;           /* accélération douce */
                d->fx += dx * gain;
                d->fy += dy * gain;
                int ix = (int)d->fx, iy = (int)d->fy;
                d->fx -= ix;
                d->fy -= iy;
                move_rel(ix, iy);
            }
            if (__builtin_fabsf(dx) + __builtin_fabsf(dy) > 0.01f) {
                int mx = d->x - d->sx, my = d->y - d->sy;
                if ((float)(mx * mx + my * my) > 4 * mm * mm) d->moved = true;
            }
        }
        d->lx = d->x;
        d->ly = d->y;
    }
}

static void handle(dev_t_ *d, struct input_event *ev) {
    if (ev->type == EV_KEY) {
        int c = ev->code;
        if (c == BTN_LEFT || c == BTN_RIGHT || c == BTN_MIDDLE) {
            if (ev->value == 2) return;
            int b = c == BTN_LEFT ? BTN_LEFT_ : c == BTN_RIGHT ? BTN_RIGHT_ : BTN_MIDDLE_;
            /* sur un pavé tactile, deux doigts + bouton gauche = clic droit */
            if (d->kind & DEV_TOUCHPAD && b == BTN_LEFT_ && d->fingers >= 2) b = BTN_RIGHT_;
            button(b, ev->value != 0);
            if (d->kind & DEV_TOUCHPAD) d->moved = true;
            return;
        }
        if (d->kind & (DEV_TOUCHPAD | DEV_ABS)) {
            if (c == BTN_TOUCH) {
                if (d->kind & DEV_ABS) { button(BTN_LEFT_, ev->value != 0); return; }
                if (ev->value) {
                    d->touch = true; d->lx = -1; d->moved = false; d->t_down = now_ms();
                    d->sx = d->x; d->sy = d->y; d->scroll_acc = 0;
                } else {
                    d->touch = false;
                    uint64_t dt = now_ms() - d->t_down;
                    if (!d->moved && dt < 220) {             /* tapotement */
                        int b = d->fingers >= 2 ? BTN_RIGHT_ : BTN_LEFT_;
                        button(b, true);
                        button(b, false);
                    }
                    d->fingers = 0;
                }
                return;
            }
            if (c == BTN_TOOL_FINGER && ev->value) { if (d->fingers != 1) d->lx = -1; d->fingers = 1; return; }
            if (c == BTN_TOOL_DOUBLETAP && ev->value) { d->fingers = 2; d->lx = -1; return; }
            if (c == BTN_TOOL_TRIPLETAP && ev->value) { d->fingers = 3; return; }
            if (c >= BTN_MISC && c < KEY_OK) return;
        }
        if (c < 0x100 || c >= KEY_OK) key_event(c, ev->value);
        return;
    }
    if (ev->type == EV_REL) {
        if (ev->code == REL_X) d->rel_x += ev->value;
        else if (ev->code == REL_Y) d->rel_y += ev->value;
        else if (ev->code == REL_WHEEL) {
            event_t e = { 0 };
            e.type = EV_WHEEL;
            e.dy = -ev->value;            /* molette vers soi = défiler vers le bas */
            emit(&e);
        }
        return;
    }
    if (ev->type == EV_ABS) {
        if (ev->code == ABS_X) d->x = ev->value;
        else if (ev->code == ABS_Y) d->y = ev->value;
        return;
    }
    if (ev->type == EV_SW && ev->code == SW_LID && ev->value == 1) {   /* écran rabattu : on verrouille */
        event_t e = { 0 };
        e.type = EV_KEY;
        e.key = K_LID_CLOSE;
        e.down = true;
        emit(&e);
        return;
    }
    if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
        if (d->rel_x || d->rel_y) { move_rel(d->rel_x, d->rel_y); d->rel_x = d->rel_y = 0; }
        if (d->kind & DEV_TOUCHPAD) touchpad_sync(d);
        else if (d->kind & DEV_ABS) {
            event_t e = { 0 };
            e.type = EV_MOTION;
            e.abs = true;
            e.x = (int)((int64_t)(d->x - d->xmin) * (input_screen_w - 1) / MAX(1, d->xmax - d->xmin));
            e.y = (int)((int64_t)(d->y - d->ymin) * (input_screen_h - 1) / MAX(1, d->ymax - d->ymin));
            emit(&e);
        }
    }
}

void input_read(struct pollfd *p, int n) {
    for (int k = 0; k < n; k++) {
        if (!p[k].revents) continue;
        dev_t_ *d = NULL;
        for (int i = 0; i < ndevs; i++) if (devs[i].fd == p[k].fd) d = &devs[i];
        if (!d) continue;
        struct input_event evs[64];
        for (;;) {
            long r = sys_read(d->fd, evs, sizeof evs);
            if (r == -EAGAIN) break;
            if (r <= 0) {                        /* périphérique débranché */
                sys_close(d->fd);
                *d = devs[--ndevs];
                break;
            }
            static bool seen;
            if (!seen) { seen = true; klog("entrée : premiers événements reçus"); }
            input_events += (int)(r / (long)sizeof(struct input_event));
            for (int i = 0; i < (int)(r / (long)sizeof(struct input_event)); i++) handle(d, &evs[i]);
        }
    }
}
