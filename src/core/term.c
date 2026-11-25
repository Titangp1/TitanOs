/* TitanOs - Terminal : émulateur VT100/xterm relié à un shell (BusyBox) par un pseudo-terminal
 *
 * Gère les couleurs (16, 256 et 24 bits), les déplacements du curseur, l'effacement,
 * les zones de défilement, l'écran alternatif (vi, top...), l'historique (molette ou
 * Maj+Page haut/bas) et le collage (Ctrl+Maj+V). Police : le 8x16 maison de TitanOs. */
#include "desk.h"

#define CW 8
#define CH 16
#define PAD 10
#define HIST 1000
#define DEF_FG 0xFFE2E8F0u

enum { A_BOLD = 1, A_REV = 2, A_UNDER = 4, A_DIM = 8 };
typedef struct { int32_t cp; uint32_t fg, bg; uint8_t attr; } cell_t;

typedef struct {
    int cols, rows;
    cell_t *scr, *alt, *hist;      /* écran, écran alternatif, historique circulaire */
    int hist_n, hist_head, view;   /* view : lignes remontées dans l'historique */
    int cx, cy, scx, scy, top, bot;
    bool wrap_next, cursor_on, alt_on, autowrap, app_cursor, dec_gfx;
    uint32_t fg, bg;
    uint8_t attr;
    int st, np, priv;              /* analyseur de séquences */
    int par[16];
    int utf_need, utf_cp;
    int pty;
    bool dead;
} term_t;

enum { ST_NORM, ST_ESC, ST_CSI, ST_OSC, ST_OSC_ESC, ST_CHARSET };

static uint32_t palette(int i) {
    static const uint32_t base[16] = {
        0x334155, 0xF87171, 0x4ADE80, 0xFACC15, 0x60A5FA, 0xC084FC, 0x22D3EE, 0xE2E8F0,
        0x64748B, 0xFCA5A5, 0x86EFAC, 0xFDE68A, 0x93C5FD, 0xE9D5FF, 0x67E8F9, 0xFFFFFF,
    };
    if (i < 16) return OPAQUE(base[i]);
    if (i < 232) {
        i -= 16;
        static const int lv[6] = { 0, 95, 135, 175, 215, 255 };
        return OPAQUE(lv[i / 36] << 16 | lv[(i / 6) % 6] << 8 | lv[i % 6]);
    }
    int g = 8 + (i - 232) * 10;
    return OPAQUE(g << 16 | g << 8 | g);
}

static cell_t blank(term_t *t) { cell_t c = { ' ', 0, t->bg, 0 }; return c; }
static cell_t *row(term_t *t, int y) { return t->scr + (size_t)y * t->cols; }

static void clear_cells(term_t *t, int y, int x0, int x1) {
    cell_t b = blank(t);
    cell_t *r = row(t, y);
    for (int x = MAX(0, x0); x < MIN(t->cols, x1); x++) r[x] = b;
}

static void push_history(term_t *t, const cell_t *line) {
    if (t->alt_on || !t->hist) return;
    int idx = (t->hist_head + t->hist_n) % HIST;
    if (t->hist_n == HIST) t->hist_head = (t->hist_head + 1) % HIST;
    else t->hist_n++;
    memcpy(t->hist + (size_t)idx * t->cols, line, sizeof(cell_t) * t->cols);
}

static void scroll_up(term_t *t, int top, int bot, int n) {
    for (int k = 0; k < n; k++) {
        if (top == 0) push_history(t, row(t, 0));
        memmove(row(t, top), row(t, top + 1), sizeof(cell_t) * t->cols * (bot - top));
        clear_cells(t, bot, 0, t->cols);
    }
}

static void scroll_down(term_t *t, int top, int bot, int n) {
    for (int k = 0; k < n; k++) {
        memmove(row(t, top + 1), row(t, top), sizeof(cell_t) * t->cols * (bot - top));
        clear_cells(t, top, 0, t->cols);
    }
}

static void newline(term_t *t) {
    if (t->cy == t->bot) scroll_up(t, t->top, t->bot, 1);
    else if (t->cy < t->rows - 1) t->cy++;
}

static void put_char(term_t *t, int cp) {
    if (t->dec_gfx && cp >= 'j' && cp <= 'x') {
        static const char map[] = "+++++-----++++|";   /* j k l m n o p q r s t u v w x */
        cp = map[cp - 'j'];
    }
    if (t->wrap_next) {
        t->wrap_next = false;
        if (t->autowrap) { t->cx = 0; newline(t); }
    }
    cell_t *c = row(t, t->cy) + t->cx;
    c->cp = cp;
    c->fg = t->fg;
    c->bg = t->bg;
    c->attr = t->attr;
    if (t->cx == t->cols - 1) t->wrap_next = true;
    else t->cx++;
}

static void reply(term_t *t, const char *s) { if (t->pty >= 0) plat_pty_write(t->pty, s, (int)strlen(s)); }

static void set_alt(term_t *t, bool on) {
    if (on == t->alt_on) return;
    cell_t *tmp = t->scr;
    t->scr = t->alt;
    t->alt = tmp;
    t->alt_on = on;
    if (on) {
        t->scx = t->cx; t->scy = t->cy;
        for (int y = 0; y < t->rows; y++) clear_cells(t, y, 0, t->cols);
    } else {
        t->cx = t->scx; t->cy = t->scy;
    }
}

static void sgr(term_t *t) {
    if (t->np == 0) { t->np = 1; t->par[0] = 0; }
    for (int i = 0; i < t->np; i++) {
        int p = t->par[i];
        if (p == 0) { t->fg = 0; t->bg = 0; t->attr = 0; }
        else if (p == 1) t->attr |= A_BOLD;
        else if (p == 2) t->attr |= A_DIM;
        else if (p == 4) t->attr |= A_UNDER;
        else if (p == 7) t->attr |= A_REV;
        else if (p == 22) t->attr &= ~(A_BOLD | A_DIM);
        else if (p == 24) t->attr &= ~A_UNDER;
        else if (p == 27) t->attr &= ~A_REV;
        else if (p >= 30 && p <= 37) t->fg = palette(p - 30);
        else if (p == 39) t->fg = 0;
        else if (p >= 40 && p <= 47) t->bg = palette(p - 40);
        else if (p == 49) t->bg = 0;
        else if (p >= 90 && p <= 97) t->fg = palette(p - 90 + 8);
        else if (p >= 100 && p <= 107) t->bg = palette(p - 100 + 8);
        else if ((p == 38 || p == 48) && i + 1 < t->np) {
            uint32_t col = 0;
            if (t->par[i + 1] == 5 && i + 2 < t->np) { col = palette(CLAMP(t->par[i + 2], 0, 255)); i += 2; }
            else if (t->par[i + 1] == 2 && i + 4 < t->np) {
                col = OPAQUE((t->par[i + 2] & 255) << 16 | (t->par[i + 3] & 255) << 8 | (t->par[i + 4] & 255));
                i += 4;
            } else continue;
            if (p == 38) t->fg = col; else t->bg = col;
        }
    }
}

static void csi(term_t *t, int f) {
    int p0 = t->np > 0 ? t->par[0] : 0, p1 = t->np > 1 ? t->par[1] : 0;
    int n = p0 ? p0 : 1;
    t->wrap_next = false;
    switch (f) {
    case '@': {
        cell_t *r = row(t, t->cy);
        n = MIN(n, t->cols - t->cx);
        memmove(r + t->cx + n, r + t->cx, sizeof(cell_t) * (t->cols - t->cx - n));
        clear_cells(t, t->cy, t->cx, t->cx + n);
        break;
    }
    case 'A': t->cy = MAX(t->cy - n, t->cy >= t->top ? t->top : 0); break;
    case 'B': t->cy = MIN(t->cy + n, t->cy <= t->bot ? t->bot : t->rows - 1); break;
    case 'C': t->cx = MIN(t->cx + n, t->cols - 1); break;
    case 'D': t->cx = MAX(t->cx - n, 0); break;
    case 'E': t->cy = MIN(t->cy + n, t->rows - 1); t->cx = 0; break;
    case 'F': t->cy = MAX(t->cy - n, 0); t->cx = 0; break;
    case 'G': case '`': t->cx = CLAMP(n - 1, 0, t->cols - 1); break;
    case 'd': t->cy = CLAMP(n - 1, 0, t->rows - 1); break;
    case 'H': case 'f':
        t->cy = CLAMP((p0 ? p0 : 1) - 1, 0, t->rows - 1);
        t->cx = CLAMP((p1 ? p1 : 1) - 1, 0, t->cols - 1);
        break;
    case 'J':
        if (p0 == 0) { clear_cells(t, t->cy, t->cx, t->cols); for (int y = t->cy + 1; y < t->rows; y++) clear_cells(t, y, 0, t->cols); }
        else if (p0 == 1) { clear_cells(t, t->cy, 0, t->cx + 1); for (int y = 0; y < t->cy; y++) clear_cells(t, y, 0, t->cols); }
        else { for (int y = 0; y < t->rows; y++) clear_cells(t, y, 0, t->cols); if (p0 == 3) t->hist_n = 0; }
        break;
    case 'K':
        if (p0 == 0) clear_cells(t, t->cy, t->cx, t->cols);
        else if (p0 == 1) clear_cells(t, t->cy, 0, t->cx + 1);
        else clear_cells(t, t->cy, 0, t->cols);
        break;
    case 'L': if (t->cy >= t->top && t->cy <= t->bot) scroll_down(t, t->cy, t->bot, MIN(n, t->bot - t->cy + 1)); break;
    case 'M': if (t->cy >= t->top && t->cy <= t->bot) {
            for (int k = 0; k < MIN(n, t->bot - t->cy + 1); k++) {
                memmove(row(t, t->cy), row(t, t->cy + 1), sizeof(cell_t) * t->cols * (t->bot - t->cy));
                clear_cells(t, t->bot, 0, t->cols);
            }
        }
        break;
    case 'P': {
        cell_t *r = row(t, t->cy);
        n = MIN(n, t->cols - t->cx);
        memmove(r + t->cx, r + t->cx + n, sizeof(cell_t) * (t->cols - t->cx - n));
        clear_cells(t, t->cy, t->cols - n, t->cols);
        break;
    }
    case 'X': clear_cells(t, t->cy, t->cx, t->cx + n); break;
    case 'S': scroll_up(t, t->top, t->bot, n); break;
    case 'T': scroll_down(t, t->top, t->bot, n); break;
    case 'm': sgr(t); break;
    case 'r':
        t->top = CLAMP((p0 ? p0 : 1) - 1, 0, t->rows - 1);
        t->bot = CLAMP((p1 ? p1 : t->rows) - 1, t->top, t->rows - 1);
        t->cx = 0; t->cy = 0;
        break;
    case 's': t->scx = t->cx; t->scy = t->cy; break;
    case 'u': t->cx = t->scx; t->cy = t->scy; break;
    case 'n':
        if (p0 == 6) { char b[32]; snprintf(b, sizeof b, "\x1b[%d;%dR", t->cy + 1, t->cx + 1); reply(t, b); }
        else if (p0 == 5) reply(t, "\x1b[0n");
        break;
    case 'c': if (!t->priv) reply(t, "\x1b[?6c"); break;
    case 'h': case 'l': {
        bool on = f == 'h';
        for (int i = 0; i < t->np; i++) {
            int m = t->par[i];
            if (t->priv == '?') {
                if (m == 25) t->cursor_on = on;
                else if (m == 7) t->autowrap = on;
                else if (m == 1) t->app_cursor = on;
                else if (m == 1049 || m == 47 || m == 1047) set_alt(t, on);
            }
        }
        break;
    }
    }
}

static void feed(term_t *t, const char *buf, int n) {
    for (int i = 0; i < n; i++) {
        uint8_t b = (uint8_t)buf[i];
        /* décodage UTF-8 du texte normal */
        if (t->st == ST_NORM && b >= 0x80) {
            if (t->utf_need) {
                t->utf_cp = (t->utf_cp << 6) | (b & 0x3F);
                if (--t->utf_need == 0) put_char(t, t->utf_cp);
                continue;
            }
            if (b >= 0xF0) { t->utf_need = 3; t->utf_cp = b & 7; }
            else if (b >= 0xE0) { t->utf_need = 2; t->utf_cp = b & 15; }
            else if (b >= 0xC0) { t->utf_need = 1; t->utf_cp = b & 31; }
            else put_char(t, b);          /* Latin-1 brut */
            continue;
        }
        t->utf_need = 0;
        switch (t->st) {
        case ST_NORM:
            switch (b) {
            case 7: sound_play(SFX_TICK); break;
            case 8: if (t->cx > 0) t->cx--; t->wrap_next = false; break;
            case 9: t->cx = MIN((t->cx / 8 + 1) * 8, t->cols - 1); break;
            case 10: case 11: case 12: newline(t); t->wrap_next = false; break;
            case 13: t->cx = 0; t->wrap_next = false; break;
            case 27: t->st = ST_ESC; break;
            case 14: case 15: break;
            default: if (b >= 32) put_char(t, b);
            }
            break;
        case ST_ESC:
            t->st = ST_NORM;
            switch (b) {
            case '[': t->st = ST_CSI; t->np = 0; t->priv = 0; memset(t->par, 0, sizeof t->par); break;
            case ']': t->st = ST_OSC; break;
            case '(': case ')': t->st = ST_CHARSET; break;
            case '7': t->scx = t->cx; t->scy = t->cy; break;
            case '8': t->cx = t->scx; t->cy = t->scy; break;
            case 'D': newline(t); break;
            case 'E': t->cx = 0; newline(t); break;
            case 'M': if (t->cy == t->top) scroll_down(t, t->top, t->bot, 1); else if (t->cy > 0) t->cy--; break;
            case 'c':
                t->fg = t->bg = 0; t->attr = 0; t->cx = t->cy = 0; t->top = 0; t->bot = t->rows - 1;
                for (int y = 0; y < t->rows; y++) clear_cells(t, y, 0, t->cols);
                break;
            }
            break;
        case ST_CHARSET: t->dec_gfx = b == '0'; t->st = ST_NORM; break;
        case ST_OSC:
            if (b == 7) t->st = ST_NORM;
            else if (b == 27) t->st = ST_OSC_ESC;
            break;
        case ST_OSC_ESC: t->st = b == '\\' ? ST_NORM : ST_OSC; break;
        case ST_CSI:
            if (b >= '0' && b <= '9') {
                if (t->np == 0) t->np = 1;
                t->par[t->np - 1] = t->par[t->np - 1] * 10 + (b - '0');
            } else if (b == ';') {
                if (t->np == 0) t->np = 1;
                if (t->np < 16) t->np++;
            } else if (b == '?' || b == '>' || b == '=') {
                t->priv = b;
            } else if (b >= 0x40 && b <= 0x7E) {
                csi(t, b);
                t->st = ST_NORM;
            }
            break;
        }
    }
}

/* ---------------- redimensionnement ---------------- */
static void term_resize(term_t *t, int cols, int rows) {
    cols = MAX(cols, 20);
    rows = MAX(rows, 5);
    if (cols == t->cols && rows == t->rows) return;
    cell_t *ns = malloc(sizeof(cell_t) * cols * rows), *na = malloc(sizeof(cell_t) * cols * rows);
    cell_t *nh = malloc(sizeof(cell_t) * cols * HIST);
    if (!ns || !na || !nh) { free(ns); free(na); free(nh); return; }
    cell_t b = { ' ', 0, 0, 0 };
    for (int i = 0; i < cols * rows; i++) ns[i] = na[i] = b;
    /* on garde le bas de l'écran (là où se trouve l'invite) */
    int shift = t->scr ? MAX(0, t->cy - (rows - 1)) : 0;
    if (t->scr)
        for (int y = 0; y < MIN(rows, t->rows - shift); y++)
            for (int x = 0; x < MIN(cols, t->cols); x++) ns[y * cols + x] = t->scr[(y + shift) * t->cols + x];
    if (t->hist) {
        for (int k = 0; k < t->hist_n; k++) {
            cell_t *src = t->hist + (size_t)((t->hist_head + k) % HIST) * t->cols;
            for (int x = 0; x < cols; x++) nh[(size_t)k * cols + x] = x < t->cols ? src[x] : b;
        }
        t->hist_head = 0;
    }
    free(t->scr); free(t->alt); free(t->hist);
    if (t->alt_on) { t->scr = na; t->alt = ns; } else { t->scr = ns; t->alt = na; }
    t->hist = nh;
    t->cy = CLAMP(t->cy - shift, 0, rows - 1);
    t->cx = CLAMP(t->cx, 0, cols - 1);
    t->cols = cols;
    t->rows = rows;
    t->top = 0;
    t->bot = rows - 1;
    t->view = 0;
    if (t->pty >= 0) plat_pty_resize(t->pty, cols, rows);
}

/* ---------------- fenêtre ---------------- */
static void term_draw(window_t *w, canvas_t *c) {
    term_t *t = w->data;
    term_resize(t, (c->w - 2 * PAD) / CW, (c->h - PAD) / CH);
    gfx_fill(c, R(0, 0, c->w, c->h), 0xB8080C16);
    for (int y = 0; y < t->rows; y++) {
        int src = y - t->view;          /* ligne affichée : historique si < 0 */
        const cell_t *r;
        if (src >= 0) r = row(t, src);
        else {
            int k = t->hist_n + src;
            if (k < 0) continue;
            r = t->hist + (size_t)((t->hist_head + k) % HIST) * t->cols;
        }
        int py = PAD / 2 + y * CH;
        for (int x = 0; x < t->cols; x++) {
            const cell_t *ce = &r[x];
            uint32_t fg = ce->fg ? ce->fg : DEF_FG, bg = ce->bg;
            if (ce->attr & A_REV) { uint32_t tmp = fg; fg = bg ? bg : 0xFF0B1220; bg = tmp; }
            if ((ce->attr & A_BOLD) && !ce->fg) fg = 0xFFFFFFFF;
            if (ce->attr & A_DIM) fg = col_mix(fg, 0xFF000000, 100);
            int px = PAD + x * CW;
            if (bg) gfx_fill(c, R(px, py, CW, CH), bg);
            if (ce->cp > 32) mono_char(c, px, py, ce->cp, fg);
            if (ce->attr & A_UNDER) gfx_fill(c, R(px, py + CH - 2, CW, 1), fg);
        }
    }
    if (t->cursor_on && !t->view && !t->dead) {
        int px = PAD + t->cx * CW, py = PAD / 2 + t->cy * CH;
        bool active = wm_focused() == w;
        if (active) {
            gfx_fill(c, R(px, py, CW, CH), (accent() & 0xFFFFFF) | 0xD0000000);
            const cell_t *ce = row(t, t->cy) + t->cx;
            if (ce->cp > 32) mono_char(c, px, py, ce->cp, 0xFFFFFFFF);
        } else {
            gfx_rrect_stroke(c, px, py, CW, CH, 0, 1, accent());
        }
    }
    if (t->view) {
        char b[48];
        snprintf(b, sizeof b, "Historique : -%d", t->view);
        int bw = text_width(&font_small, b) + 20;
        gfx_rrect(c, c->w - bw - 12, 8, bw, 24, 12, 0xC0334155);
        text_draw(c, &font_small, c->w - bw - 2, 12, b, C_TEXT);
    }
    if (t->dead) text_draw(c, &font_body, PAD, c->h - 26, "Le shell s'est arrêté. Entrée pour le relancer.", C_MUTED);
}

static void send(term_t *t, const char *s, int n) {
    if (t->pty >= 0) plat_pty_write(t->pty, s, n);
    t->view = 0;
}

static void term_start(term_t *t) {
    t->pty = plat_pty_open(t->cols > 0 ? t->cols : 80, t->rows > 0 ? t->rows : 24);
    t->dead = t->pty < 0;
    if (t->dead) feed(t, "\x1b[31mImpossible de lancer le shell (/bin/sh).\x1b[0m\r\n", 52);
}

static void term_event(window_t *w, const event_t *e) {
    term_t *t = w->data;
    if (e->type == EV_WHEEL) {
        t->view = CLAMP(t->view - e->dy * 3, 0, t->hist_n);   /* molette vers le haut : historique */
        wm_invalidate(w);
        return;
    }
    if (e->type != EV_KEY || !e->down) return;
    if (t->dead) {
        if (e->key == K_ENTER) { t->dead = false; term_start(t); wm_invalidate(w); }
        return;
    }
    bool shift = e->mods & MOD_SHIFT, ctrl = e->mods & MOD_CTRL, alt = e->mods & MOD_ALT;
    if (shift && (e->key == K_PGUP || e->key == K_PGDN)) {
        t->view = CLAMP(t->view + (e->key == K_PGUP ? t->rows / 2 : -t->rows / 2), 0, t->hist_n);
        wm_invalidate(w);
        return;
    }
    if (ctrl && shift && e->key == K_CHAR && (e->ch == 'V' || e->ch == 'v')) {
        const char *p = clip_get();
        send(t, p, (int)strlen(p));
        return;
    }
    if (ctrl && shift && e->key == K_CHAR && (e->ch == 'C' || e->ch == 'c')) {
        char *buf = malloc((size_t)t->rows * (t->cols * 4 + 1) + 1);
        if (!buf) return;
        int n = 0;
        for (int y = 0; y < t->rows; y++) {
            int end = t->cols;
            while (end > 0 && row(t, y)[end - 1].cp == ' ') end--;
            for (int x = 0; x < end; x++) n += utf8_encode(buf + n, row(t, y)[x].cp);
            buf[n++] = '\n';
        }
        clip_set(buf, n);
        free(buf);
        wm_toast("Contenu du terminal copié.");
        return;
    }
    const char *seq = NULL;
    char tmp[8];
    switch (e->key) {
    case K_ENTER: seq = "\r"; break;
    case K_BACKSPACE: seq = "\x7f"; break;
    case K_TAB: seq = shift ? "\x1b[Z" : "\t"; break;
    case K_ESC: seq = "\x1b"; break;
    case K_UP: seq = t->app_cursor ? "\x1bOA" : "\x1b[A"; break;
    case K_DOWN: seq = t->app_cursor ? "\x1bOB" : "\x1b[B"; break;
    case K_RIGHT: seq = t->app_cursor ? "\x1bOC" : "\x1b[C"; break;
    case K_LEFT: seq = t->app_cursor ? "\x1bOD" : "\x1b[D"; break;
    case K_HOME: seq = "\x1b[H"; break;
    case K_END: seq = "\x1b[F"; break;
    case K_PGUP: seq = "\x1b[5~"; break;
    case K_PGDN: seq = "\x1b[6~"; break;
    case K_INSERT: seq = "\x1b[2~"; break;
    case K_DELETE: seq = "\x1b[3~"; break;
    case K_F1: seq = "\x1bOP"; break;
    case K_F2: seq = "\x1bOQ"; break;
    case K_F3: seq = "\x1bOR"; break;
    case K_F4: seq = "\x1bOS"; break;
    case K_F5: seq = "\x1b[15~"; break;
    case K_F10: seq = "\x1b[21~"; break;
    case K_CHAR: {
        int ch = e->ch, n = 0;
        if (alt) tmp[n++] = 27;
        if (ctrl && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == ' ' || ch == '[' || ch == '\\' || ch == ']')) {
            tmp[n++] = (char)(ch == ' ' ? 0 : (ch & 0x1F));
        } else {
            n += utf8_encode(tmp + n, ch);
        }
        send(t, tmp, n);
        return;
    }
    }
    if (seq) send(t, seq, (int)strlen(seq));
}

static void term_tick(window_t *w, uint64_t now) {
    (void)now;
    term_t *t = w->data;
    if (t->pty < 0 || t->dead) return;
    char buf[8192];
    int total = 0;
    for (int k = 0; k < 16; k++) {
        int n = plat_pty_read(t->pty, buf, sizeof buf);
        if (n < 0) {
            plat_pty_close(t->pty);
            t->pty = -1;
            wm_close(w);           /* « exit » ferme la fenêtre, comme partout */
            return;
        }
        if (n == 0) break;
        feed(t, buf, n);
        total += n;
    }
    if (total) { t->view = 0; wm_invalidate(w); }
}

static void term_close(window_t *w) {
    term_t *t = w->data;
    if (t->pty >= 0) plat_pty_close(t->pty);
    free(t->scr); free(t->alt); free(t->hist);
    free(t);
}

void app_terminal_open(void) {
    term_t *t = calloc(1, sizeof *t);
    if (!t) return;
    t->cursor_on = t->autowrap = true;
    t->pty = -1;
    window_t *w = wm_open("Terminal", ICON_TERMINAL, 92 * CW + 2 * PAD, 28 * CH + PAD, WF_RESIZABLE);
    if (!w) { free(t); return; }
    w->minw = 360;
    w->data = t;
    w->draw = term_draw;
    w->event = term_event;
    w->tick = term_tick;
    w->on_close = term_close;
    w->cursor = CURSOR_TEXT;
    term_resize(t, (w->surf.w - 2 * PAD) / CW, (w->surf.h - PAD) / CH);
    term_start(t);
}

/* Terminal qui lance une commande (Python, Node.js...) : elle est tapée pour l'utilisateur */
void app_terminal_run(const char *cmd) {
    app_terminal_open();
    window_t *w = wm_focused();
    if (!w || w->icon != ICON_TERMINAL || !cmd) return;
    term_t *t = w->data;
    if (t && t->pty >= 0) {
        plat_pty_write(t->pty, cmd, (int)strlen(cmd));
        plat_pty_write(t->pty, "\n", 1);
    }
}
