/* TitanOs - widgets (boutons, champs, interrupteurs...) et boîtes de dialogue */
#include "desk.h"

/* ---------------- widgets ---------------- */
void ui_panel(canvas_t *c, rect_t r, float radius) {
    gfx_rrect(c, r.x, r.y, r.w, r.h, radius, C_PANEL);
    gfx_rrect_stroke(c, r.x, r.y, r.w, r.h, radius, 1, 0x1CFFFFFF);
}

void ui_button(canvas_t *c, rect_t r, const char *label, int style, bool hover) {
    float rad = r.h / 2.0f;
    uint32_t bg, fg = C_TEXT;
    switch (style) {
    case BTN_ACCENT: bg = hover ? col_mix(accent(), 0xFFFFFFFF, 40) : accent(); break;
    case BTN_DANGER: bg = hover ? 0xFFF87171 : C_DANGER; break;
    case BTN_FLAT:   bg = hover ? 0x22FFFFFF : 0; break;
    default:         bg = hover ? 0x38FFFFFF : 0x24FFFFFF; break;
    }
    if (bg) gfx_rrect(c, r.x, r.y, r.w, r.h, rad, bg);
    if (style == BTN_NORMAL || style == BTN_ACCENT || style == BTN_DANGER) {
        gfx_rrect_grad(c, r.x + 1, r.y + 1, r.w - 2, r.h / 2.0f, rad - 1, 0x22FFFFFF, 0x00FFFFFF);
        gfx_rrect_stroke(c, r.x, r.y, r.w, r.h, rad, 1, 0x30FFFFFF);
    }
    text_center(c, &font_bold, r, label, fg);
}

void ui_icon_button(canvas_t *c, rect_t r, int icon, bool hover) {
    if (hover) gfx_rrect(c, r.x, r.y, r.w, r.h, 10, 0x26FFFFFF);
    int s = MIN(r.w, r.h) - 12;
    draw_icon(c, icon, r.x + (r.w - s) / 2.0f, r.y + (r.h - s) / 2.0f, s);
}

void ui_toggle(canvas_t *c, rect_t r, bool on) {
    float h = r.h, w = r.w;
    gfx_rrect(c, r.x, r.y, w, h, h / 2, on ? accent() : 0x40FFFFFF);
    gfx_rrect_stroke(c, r.x, r.y, w, h, h / 2, 1, 0x30FFFFFF);
    float kx = on ? r.x + w - h / 2 : r.x + h / 2;
    gfx_circle(c, kx, r.y + h / 2 + 1, h / 2 - 3, 0x40000000);
    gfx_circle(c, kx, r.y + h / 2, h / 2 - 3, 0xFFFFFFFF);
}

void ui_slider(canvas_t *c, rect_t r, int v) {
    v = CLAMP(v, 0, 100);
    float cy = r.y + r.h / 2.0f, x0 = r.x + 10, x1 = r.x + r.w - 10, kx = x0 + (x1 - x0) * v / 100;
    gfx_rrect(c, x0, cy - 3, x1 - x0, 6, 3, 0x40FFFFFF);
    gfx_rrect(c, x0, cy - 3, kx - x0, 6, 3, accent());
    gfx_circle(c, kx, cy + 1, 10, 0x40000000);
    gfx_circle(c, kx, cy, 10, 0xFFFFFFFF);
}

int ui_slider_value(rect_t r, int x) {
    float x0 = r.x + 10, x1 = r.x + r.w - 10;
    return CLAMP((int)((x - x0) * 100 / (x1 - x0) + 0.5f), 0, 100);
}

void ui_segments(canvas_t *c, rect_t r, const char *const *items, int n, int sel, int hover) {
    gfx_rrect(c, r.x, r.y, r.w, r.h, r.h / 2.0f, 0x30000000);
    gfx_rrect_stroke(c, r.x, r.y, r.w, r.h, r.h / 2.0f, 1, 0x1CFFFFFF);
    int w = r.w / n;
    for (int i = 0; i < n; i++) {
        rect_t s = R(r.x + i * w + 3, r.y + 3, w - 6, r.h - 6);
        if (i == sel) {
            gfx_rrect(c, s.x, s.y, s.w, s.h, s.h / 2.0f, 0x40FFFFFF);
            gfx_rrect_stroke(c, s.x, s.y, s.w, s.h, s.h / 2.0f, 1, 0x40FFFFFF);
        } else if (i == hover) {
            gfx_rrect(c, s.x, s.y, s.w, s.h, s.h / 2.0f, 0x18FFFFFF);
        }
        text_center(c, i == sel ? &font_bold : &font_body, s, items[i], i == sel ? C_TEXT : C_TEXT2);
    }
}

int ui_segment_at(rect_t r, int n, int x, int y) {
    if (!rect_has(r, x, y)) return -1;
    return CLAMP((x - r.x) * n / r.w, 0, n - 1);
}

void ui_scrollbar(canvas_t *c, rect_t r, int total, int visible, int offset) {
    if (total <= visible || total <= 0) return;
    int th = MAX(30, r.h * visible / total);
    int ty = r.y + (r.h - th) * offset / MAX(1, total - visible);
    gfx_rrect(c, r.x, ty, r.w, th, r.w / 2.0f, 0x50FFFFFF);
}

/* ---------------- champ de texte ---------------- */
void ui_field_set(field_t *f, const char *s) {
    strlcpy(f->s, s, sizeof f->s);
    f->cur = (int)strlen(f->s);
}

void ui_field(canvas_t *c, rect_t r, field_t *f, const char *placeholder) {
    gfx_rrect(c, r.x, r.y, r.w, r.h, 12, C_FIELD);
    gfx_rrect_stroke(c, r.x, r.y, r.w, r.h, 12, f->focus ? 1.5f : 1, f->focus ? accent() : 0x24FFFFFF);
    int ty = r.y + (r.h - font_body.ascent - font_body.descent) / 2;
    rect_t old = c->clip;
    canvas_clip(c, rect_isect(old, R(r.x + 10, r.y, r.w - 20, r.h)));
    char shown[256 * 3];
    const char *txt = f->s;
    int cur = f->cur;
    if (f->secret) {                       /* mot de passe : des points à la place des caractères */
        int n = 0, k = 0;
        for (int p = 0; f->s[p] && n < 250; p = utf8_next(f->s, p), n++) k += utf8_encode(shown + k, 0x2022);
        shown[k] = 0;
        int before = utf8_len_n(f->s, f->cur);
        cur = 0;
        for (int i = 0; i < before; i++) cur = utf8_next(shown, cur);
        txt = shown;
    }
    int cw = text_width_n(&font_body, txt, cur), shift = 0;
    if (cw > r.w - 34) shift = cw - (r.w - 34);
    if (!f->s[0] && placeholder) {
        draw_icon(c, ICON_SEARCH, r.x + 12, r.y + (r.h - 18) / 2.0f, 18);
        text_draw(c, &font_body, r.x + 38, ty, placeholder, C_MUTED);
    } else {
        text_draw(c, &font_body, r.x + 14 - shift, ty, txt, C_TEXT);
    }
    if (f->focus) gfx_fill(c, R(r.x + 14 - shift + cw, ty - 1, 2, font_body.ascent + font_body.descent + 2), accent());
    c->clip = old;
}

bool ui_field_key(field_t *f, const event_t *e) {
    if (e->type != EV_KEY || !e->down) return false;
    int len = (int)strlen(f->s);
    if (e->key == K_CHAR && (e->mods & MOD_CTRL)) {
        if (e->ch == 'v' || e->ch == 'V') {
            const char *p = clip_get();
            bool changed = false;
            while (*p && *p != '\n' && len + 4 < (int)sizeof f->s) {
                int n = utf8_next(p, 0);
                memmove(f->s + f->cur + n, f->s + f->cur, (size_t)(len - f->cur + 1));
                memcpy(f->s + f->cur, p, (size_t)n);
                f->cur += n; len += n; p += n;
                changed = true;
            }
            return changed;
        }
        if ((e->ch == 'c' || e->ch == 'C') && !f->secret) { clip_set(f->s, len); return false; }
        return false;
    }
    switch (e->key) {
    case K_CHAR: {
        if (e->ch < 32) return false;
        char u[4];
        int n = utf8_encode(u, e->ch);
        if (len + n >= (int)sizeof f->s) return false;
        memmove(f->s + f->cur + n, f->s + f->cur, (size_t)(len - f->cur + 1));
        memcpy(f->s + f->cur, u, (size_t)n);
        f->cur += n;
        return true;
    }
    case K_BACKSPACE: {
        if (!f->cur) return false;
        int p = utf8_prev(f->s, f->cur);
        memmove(f->s + p, f->s + f->cur, (size_t)(len - f->cur + 1));
        f->cur = p;
        return true;
    }
    case K_DELETE: {
        if (f->cur >= len) return false;
        int n = utf8_next(f->s, f->cur);
        memmove(f->s + f->cur, f->s + n, (size_t)(len - n + 1));
        return true;
    }
    case K_LEFT:  f->cur = utf8_prev(f->s, f->cur); return false;
    case K_RIGHT: f->cur = utf8_next(f->s, f->cur); return false;
    case K_HOME:  f->cur = 0; return false;
    case K_END:   f->cur = len; return false;
    }
    return false;
}

/* découpe en lignes : renvoie le nombre de lignes, starts/lens en octets */
int ui_wrap(const font_t *f, const char *s, int maxw, int *starts, int *lens, int max) {
    int n = 0, pos = 0;
    while (s[pos] && n < max) {
        int line_end = pos, last_space = -1, p = pos;
        while (s[p] && s[p] != '\n') {
            int nx = utf8_next(s, p);
            if (text_width_n(f, s + pos, nx - pos) > maxw && p > pos) break;
            if (s[p] == ' ') last_space = p;
            p = nx;
        }
        if (s[p] && s[p] != '\n' && last_space > pos) line_end = last_space;
        else line_end = p;
        starts[n] = pos;
        lens[n] = line_end - pos;
        n++;
        pos = line_end;
        if (s[pos] == ' ' || s[pos] == '\n') pos++;
    }
    return n;
}

/* ---------------- boîtes de dialogue ---------------- */
enum { DLG_MSG, DLG_CONFIRM, DLG_INPUT };
typedef struct {
    int kind;
    char text[512];
    char label[96];
    char ok[32];
    field_t field;
    bool password;                 /* champ mot de passe, avec « Afficher » */
    rect_t eye;                    /* bouton Afficher / Masquer */
    int hover;                     /* 0 annuler, 1 ok, 2 afficher */
    void (*cb_bool)(bool, void *);
    void (*cb_text)(const char *, void *);
    void *ctx;
} dlg_t;

static rect_t dlg_btn(window_t *w, int i) {
    dlg_t *d = w->data;
    int bw = 128, y = w->surf.h - 60;
    if (d->kind == DLG_MSG) return R(w->surf.w - 24 - bw, y, bw, 40);
    return i ? R(w->surf.w - 24 - bw, y, bw, 40) : R(w->surf.w - 24 - 2 * bw - 12, y, bw, 40);
}

static void dlg_draw(window_t *w, canvas_t *c) {
    dlg_t *d = w->data;
    int starts[12], lens[12];
    int n = ui_wrap(&font_body, d->text, c->w - 48, starts, lens, 12);
    for (int i = 0; i < n; i++) text_draw_n(c, &font_body, 24, 16 + i * 22, d->text + starts[i], lens[i], C_TEXT2);
    if (d->kind == DLG_INPUT) {
        int y = 16 + n * 22 + 6;
        if (d->label[0]) { text_draw(c, &font_small, 24, y, d->label, C_MUTED); y += 20; }
        int fw = c->w - 48 - (d->password ? 112 : 0);
        ui_field(c, R(24, y, fw, 40), &d->field, NULL);
        if (d->password) {
            d->eye = R(24 + fw + 10, y, 102, 40);
            ui_button(c, d->eye, d->field.secret ? "Afficher" : "Masquer", BTN_NORMAL, d->hover == 2);
        }
    }
    if (d->kind != DLG_MSG) ui_button(c, dlg_btn(w, 0), "Annuler", BTN_NORMAL, d->hover == 0);
    ui_button(c, dlg_btn(w, 1), d->ok[0] ? d->ok : "OK",
              (!strcmp(d->ok, "Éteindre") || !strcmp(d->ok, "Supprimer")) ? BTN_DANGER : BTN_ACCENT, d->hover == 1);
}

static void dlg_finish(window_t *w, bool ok) {
    dlg_t *d = w->data;
    void (*cb_bool)(bool, void *) = d->cb_bool;
    void (*cb_text)(const char *, void *) = d->cb_text;
    void *ctx = d->ctx;
    char text[256];
    strlcpy(text, d->field.s, sizeof text);
    int kind = d->kind;
    wm_close(w);
    if (kind == DLG_CONFIRM && cb_bool) cb_bool(ok, ctx);
    if (kind == DLG_INPUT && cb_text && ok) cb_text(text, ctx);
}

static void dlg_event(window_t *w, const event_t *e) {
    dlg_t *d = w->data;
    if (e->type == EV_MOTION) {
        int h = rect_has(dlg_btn(w, 1), e->x, e->y) ? 1 : (d->kind != DLG_MSG && rect_has(dlg_btn(w, 0), e->x, e->y)) ? 0 : -1;
        if (d->password && rect_has(d->eye, e->x, e->y)) h = 2;
        if (h != d->hover) { d->hover = h; wm_invalidate(w); }
    } else if (e->type == EV_BUTTON && e->down && e->button == BTN_LEFT) {
        if (d->password && rect_has(d->eye, e->x, e->y)) { sound_play(SFX_CLICK); d->field.secret = !d->field.secret; wm_invalidate(w); }
        else if (rect_has(dlg_btn(w, 1), e->x, e->y)) { sound_play(SFX_CLICK); dlg_finish(w, true); }
        else if (d->kind != DLG_MSG && rect_has(dlg_btn(w, 0), e->x, e->y)) { sound_play(SFX_CLICK); dlg_finish(w, false); }
    } else if (e->type == EV_KEY && e->down) {
        if (e->key == K_ENTER) dlg_finish(w, true);
        else if (e->key == K_ESC) dlg_finish(w, false);
        else if (d->kind == DLG_INPUT) { ui_field_key(&d->field, e); wm_invalidate(w); }
    }
}

static void dlg_close(window_t *w) { free(w->data); }

static window_t *dlg_new(int kind, const char *title, const char *text) {
    dlg_t *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->kind = kind;
    d->hover = -1;
    strlcpy(d->text, text, sizeof d->text);
    int starts[12], lens[12];
    int n = ui_wrap(&font_body, text, 420 - 48, starts, lens, 12);
    int h = 16 + n * 22 + 24 + 60 + (kind == DLG_INPUT ? 70 : 0);
    window_t *owner = wm_focused();
    window_t *w = wm_open(title, owner ? owner->icon : ICON_ABOUT, 420, h, WF_DIALOG);
    if (!w) { free(d); return NULL; }
    w->owner = owner;
    w->data = d;
    w->draw = dlg_draw;
    w->event = dlg_event;
    w->on_close = dlg_close;
    return w;
}

void dlg_message(const char *title, const char *text) { dlg_new(DLG_MSG, title, text); sound_play(SFX_ERROR); }

void dlg_confirm(const char *title, const char *text, const char *ok, void (*cb)(bool, void *), void *ctx) {
    window_t *w = dlg_new(DLG_CONFIRM, title, text);
    if (!w) return;
    dlg_t *d = w->data;
    strlcpy(d->ok, ok ? ok : "OK", sizeof d->ok);
    d->cb_bool = cb;
    d->ctx = ctx;
}

void dlg_input(const char *title, const char *label, const char *initial, void (*cb)(const char *, void *), void *ctx) {
    window_t *w = dlg_new(DLG_INPUT, title, "");
    if (!w) return;
    dlg_t *d = w->data;
    strlcpy(d->label, label ? label : "", sizeof d->label);
    strlcpy(d->ok, "Valider", sizeof d->ok);
    ui_field_set(&d->field, initial ? initial : "");
    d->field.focus = true;
    d->cb_text = cb;
    d->ctx = ctx;
}

void dlg_password(const char *title, const char *label, void (*cb)(const char *, void *), void *ctx) {
    dlg_input(title, label, "", cb, ctx);
    window_t *w = wm_focused();
    if (w && (w->flags & WF_DIALOG)) {
        dlg_t *d = w->data;
        d->field.secret = true;
        d->password = true;
        strlcpy(d->ok, strstr(title, "Wi-Fi") ? "Se connecter" : "Valider", sizeof d->ok);
    }
}

/* ---------------- barre de progression fluide ---------------- */
bool ui_progress(canvas_t *c, rect_t r, ui_progress_t *p, int percent) {
    uint64_t now = plat_ms();
    float dt = p->t ? (float)MIN(now - p->t, 100) : 16;
    p->t = now;
    float rad = r.h / 2.0f;
    gfx_rrect(c, r.x, r.y, r.w, r.h, rad, 0x26FFFFFF);
    uint32_t acc = accent();
    uint32_t light = 0xFF000000u;
    for (int sh = 0; sh < 24; sh += 8) {                      /* haut de la barre un peu plus clair */
        uint32_t v = acc >> sh & 255;
        light |= (v + (255 - v) / 4) << sh;
    }
    if (percent < 0) {                                        /* durée inconnue : un segment qui va et vient */
        p->phase += dt / 1500.0f;
        if (p->phase >= 1) p->phase -= 1;
        float t = p->phase < 0.5f ? p->phase * 2 : 2 - p->phase * 2;
        t = t * t * (3 - 2 * t);
        float sw = r.w * 0.3f, x = r.x + (r.w - sw) * t;
        gfx_rrect_grad(c, x, r.y, sw, r.h, rad, light, acc);
        p->started = false;
        return true;
    }
    float target = (float)CLAMP(percent, 0, 100);
    if (!p->started || target < p->shown - 8) { p->shown = target > 3 ? target - 3 : 0; p->started = true; }   /* nouvelle tâche */
    if (p->shown < target) p->shown += (target - p->shown) * (dt / (dt + 170.0f)) + dt * 0.002f;   /* glisse vers la valeur */
    else if (target < 100 && p->shown < target + 2.5f) p->shown += dt * 0.0006f;                      /* avance doucement */
    if (p->shown > 100) p->shown = 100;
    float fw = MAX((float)r.h, r.w * p->shown / 100.0f);
    gfx_rrect_grad(c, r.x, r.y, fw, r.h, rad, light, acc);
    /* reflet lumineux qui parcourt la partie remplie */
    p->phase += dt / 1700.0f;
    if (p->phase >= 1) p->phase -= 1;
    float bx = r.x - 40 + (fw + 80) * p->phase;
    for (int i = -36; i <= 36; i += 2) {
        float xx = bx + i;
        if (xx < r.x + rad || xx + 2 > r.x + fw - rad) continue;
        float a = 1 - (float)(i * i) / 1296.0f;
        gfx_fill(c, R((int)xx, r.y, 2, r.h), (uint32_t)(a * 70) << 24 | 0xFFFFFF);
    }
    return true;
}
