/* TitanOs - Système : aperçu de la machine, matériel détecté (pilotes) et processus */
#include "desk.h"

#define MAX_DEV  128
#define MAX_PROC 256

typedef struct {
    int tab, hover, scroll, sel;
    sysinfo_t si;
    device_t dev[MAX_DEV];
    int ndev;
    proc_t proc[MAX_PROC];
    int nproc;
    uint64_t next;
} sys_t;

static rect_t tabs_rect(canvas_t *c) { return R(c->w / 2 - 210, 12, 420, 36); }
static rect_t list_rect(canvas_t *c) { return R(16, 64, c->w - 32, c->h - 64 - 64); }
static rect_t kill_rect(canvas_t *c) { return R(c->w - 16 - 220, c->h - 52, 220, 38); }

static void refresh(sys_t *s) {
    plat_sysinfo(&s->si);
    if (s->tab == 1) s->ndev = plat_devices(s->dev, MAX_DEV);
    if (s->tab == 2) {
        int sel_pid = s->sel >= 0 && s->sel < s->nproc ? s->proc[s->sel].pid : -1;
        s->nproc = plat_procs(s->proc, MAX_PROC);
        s->sel = -1;
        for (int i = 0; i < s->nproc; i++) if (s->proc[i].pid == sel_pid) s->sel = i;
    }
}

static void card(canvas_t *c, rect_t r, const char *title, const char *value, const char *sub) {
    ui_panel(c, r, 16);
    text_draw(c, &font_small, r.x + 16, r.y + 12, title, C_MUTED);
    text_fit(c, &font_bold, r.x + 16, r.y + 32, r.w - 32, value, C_TEXT);
    if (sub) text_fit(c, &font_small, r.x + 16, r.y + 54, r.w - 32, sub, C_TEXT2);
}

static void draw_overview(sys_t *s, canvas_t *c) {
    sysinfo_t *si = &s->si;
    draw_logo(c, 70, 70, 60, 255);
    text_draw(c, &font_h2, 118, 70, "TitanOs " TITANOS_VERSION, C_TEXT);
    char buf[160], sub[160];
    snprintf(buf, sizeof buf, "Noyau Linux %s · démarrage %s", si->kernel, si->boot);
    text_draw(c, &font_body, 118, 102, buf, C_TEXT2);
    int cw = (c->w - 48) / 2, x0 = 16, x1 = 16 + cw + 16, y = 150, h = 78, g = 12;
    int k = snprintf(sub, sizeof sub, "%d cœurs logiques", si->cores);
    if (si->cpu_mhz > 0) k += snprintf(sub + k, sizeof sub - (size_t)k, " · %d MHz", si->cpu_mhz);
    if (si->cpu_temp > 0) snprintf(sub + k, sizeof sub - (size_t)k, " · %d °C", si->cpu_temp);
    card(c, R(x0, y, cw, h), "Processeur", si->cpu, sub);
    char a[32], b[32];
    fmt_size(a, sizeof a, si->mem_total - si->mem_free);
    fmt_size(b, sizeof b, si->mem_total);
    snprintf(buf, sizeof buf, "%s utilisés sur %s", a, b);
    card(c, R(x1, y, cw, h), "Mémoire", buf, NULL);
    rect_t bar = R(x1 + 16, y + h - 20, cw - 32, 6);
    gfx_rrect(c, bar.x, bar.y, bar.w, bar.h, 3, 0x30FFFFFF);
    if (si->mem_total) gfx_rrect(c, bar.x, bar.y, (float)bar.w * (si->mem_total - si->mem_free) / si->mem_total, bar.h, 3, accent());
    y += h + g;
    k = snprintf(sub, sizeof sub, "Pilote : %s", si->gpu_driver);
    if (si->gpu_temp > 0) snprintf(sub + k, sizeof sub - (size_t)k, " · %d °C", si->gpu_temp);
    card(c, R(x0, y, cw, h), "Carte graphique", si->gpu, sub);
    card(c, R(x1, y, cw, h), "Affichage", si->display, NULL);
    y += h + g;
    uint64_t up = si->uptime_s;
    battery_t bt;
    plat_battery(&bt);
    snprintf(sub, sizeof sub, "Allumé depuis %u h %02u min · %d processus", (unsigned)(up / 3600), (unsigned)(up / 60 % 60), si->procs);
    if (bt.present) {
        snprintf(buf, sizeof buf, "%d %% · %s", bt.percent, bt.charging ? "en charge" : bt.ac ? "sur secteur" : "sur batterie");
        card(c, R(x0, y, cw, h), "Batterie", buf, sub);
    } else {
        snprintf(buf, sizeof buf, "%u h %02u min", (unsigned)(up / 3600), (unsigned)(up / 60 % 60));
        snprintf(sub, sizeof sub, "%d processus", si->procs);
        card(c, R(x0, y, cw, h), "Allumé depuis", buf, sub);
    }
    card(c, R(x1, y, cw, h), "Vos fichiers", plat_storage(), plat_home());
}

static void draw_list_row(canvas_t *c, rect_t r, bool sel, bool hov) {
    if (sel) gfx_rrect(c, r.x, r.y, r.w, r.h, 10, (accent() & 0xFFFFFF) | 0x90000000);
    else if (hov) gfx_rrect(c, r.x, r.y, r.w, r.h, 10, 0x18FFFFFF);
}

static void draw_devices(sys_t *s, canvas_t *c) {
    rect_t L = list_rect(c);
    ui_panel(c, L, 16);
    rect_t old = c->clip;
    canvas_clip(c, rect_grow(L, -2));
    int vis = (L.h - 12) / 34;
    for (int i = s->scroll; i < s->ndev && i < s->scroll + vis + 1; i++) {
        device_t *d = &s->dev[i];
        rect_t r = R(L.x + 8, L.y + 6 + (i - s->scroll) * 34, L.w - 16, 32);
        draw_list_row(c, r, false, i == s->hover);
        gfx_rrect(c, r.x + 8, r.y + 7, 40, 18, 9, strcmp(d->bus, "USB") ? 0x403B82F6 : 0x4022C55E);
        text_center(c, &font_small, R(r.x + 8, r.y + 7, 40, 18), d->bus, C_TEXT);
        text_fit(c, &font_body, r.x + 60, r.y + 8, r.w - 300, d->name, C_TEXT);
        text_draw(c, &font_small, r.x + r.w - 230, r.y + 10, d->id, C_MUTED);
        text_draw(c, &font_small, r.x + r.w - 130, r.y + 10, d->driver[0] ? d->driver : "—", d->driver[0] ? 0xFF86EFAC : C_MUTED);
    }
    c->clip = old;
    ui_scrollbar(c, R(L.x + L.w - 8, L.y + 8, 4, L.h - 16), s->ndev, vis, s->scroll);
    char buf[96];
    snprintf(buf, sizeof buf, "%d périphériques · la colonne de droite indique le pilote Linux utilisé", s->ndev);
    text_draw(c, &font_small, 20, c->h - 40, buf, C_MUTED);
}

static void draw_procs(sys_t *s, canvas_t *c) {
    rect_t L = list_rect(c);
    ui_panel(c, L, 16);
    rect_t old = c->clip;
    canvas_clip(c, rect_grow(L, -2));
    int vis = (L.h - 12) / 30;
    for (int i = s->scroll; i < s->nproc && i < s->scroll + vis + 1; i++) {
        proc_t *p = &s->proc[i];
        rect_t r = R(L.x + 8, L.y + 6 + (i - s->scroll) * 30, L.w - 16, 28);
        draw_list_row(c, r, i == s->sel, i == s->hover);
        char buf[48];
        snprintf(buf, sizeof buf, "%d", p->pid);
        text_draw(c, &font_small, r.x + 12, r.y + 7, buf, C_MUTED);
        text_draw(c, &font_body, r.x + 80, r.y + 5, p->name, C_TEXT);
        fmt_size(buf, sizeof buf, p->rss_kb * 1024);
        text_draw(c, &font_small, r.x + r.w - 110, r.y + 7, p->rss_kb ? buf : "—", C_TEXT2);
    }
    c->clip = old;
    ui_scrollbar(c, R(L.x + L.w - 8, L.y + 8, 4, L.h - 16), s->nproc, vis, s->scroll);
    ui_button(c, kill_rect(c), "Arrêter le processus", s->sel >= 0 ? BTN_DANGER : BTN_NORMAL, s->hover == 1000);
}

static void sys_draw(window_t *w, canvas_t *c) {
    sys_t *s = w->data;
    static const char *tabs[3] = { "Aperçu", "Matériel", "Processus" };
    ui_segments(c, tabs_rect(c), tabs, 3, s->tab, s->hover <= -10 ? -10 - s->hover : -1);
    if (s->tab == 0) draw_overview(s, c);
    else if (s->tab == 1) draw_devices(s, c);
    else draw_procs(s, c);
}

static int row_h(sys_t *s) { return s->tab == 1 ? 34 : 30; }

static void sys_event(window_t *w, const event_t *e) {
    sys_t *s = w->data;
    canvas_t *c = &w->surf;
    rect_t L = list_rect(c);
    int count = s->tab == 1 ? s->ndev : s->nproc, vis = (L.h - 12) / row_h(s);
    if (e->type == EV_MOTION) {
        int h = -1;
        int t = ui_segment_at(tabs_rect(c), 3, e->x, e->y);
        if (t >= 0) h = -10 - t;
        else if (s->tab && rect_has(L, e->x, e->y)) { int i = s->scroll + (e->y - L.y - 6) / row_h(s); if (i < count) h = i; }
        else if (s->tab == 2 && rect_has(kill_rect(c), e->x, e->y)) h = 1000;
        if (h != s->hover) { s->hover = h; wm_invalidate(w); }
    } else if (e->type == EV_WHEEL) {
        s->scroll = CLAMP(s->scroll + e->dy * 3, 0, MAX(0, count - vis));
        wm_invalidate(w);
    } else if (e->type == EV_BUTTON && e->down && e->button == BTN_LEFT) {
        if (s->hover <= -10 || s->hover == 1000) sound_play(SFX_CLICK);
        if (s->hover <= -10) { s->tab = -10 - s->hover; s->scroll = 0; s->sel = -1; refresh(s); }
        else if (s->hover == 1000 && s->sel >= 0) {
            if (!plat_kill(s->proc[s->sel].pid)) wm_toast("Impossible d'arrêter ce processus.");
            else wm_toast("Processus arrêté.");
            refresh(s);
        } else if (s->tab == 2 && s->hover >= 0) s->sel = s->hover;
        wm_invalidate(w);
    }
}

static void sys_tick(window_t *w, uint64_t now) {
    sys_t *s = w->data;
    if (now < s->next) return;
    s->next = now + 2000;
    if (s->tab != 1) { refresh(s); wm_invalidate(w); }
}

static void free_data(window_t *w) { free(w->data); }

void app_system_open(void) {
    window_t *w = wm_find(ICON_SYSTEM);
    if (w) { wm_focus(w); return; }
    sys_t *s = calloc(1, sizeof *s);
    if (!s) return;
    s->sel = -1;
    s->hover = -1;
    refresh(s);
    w = wm_open("Système", ICON_SYSTEM, 760, 444, WF_RESIZABLE | WF_SINGLE);
    if (!w) { free(s); return; }
    w->minw = 600;
    w->minh = 400;
    w->data = s;
    w->draw = sys_draw;
    w->event = sys_event;
    w->tick = sys_tick;
    w->on_close = free_data;
}
