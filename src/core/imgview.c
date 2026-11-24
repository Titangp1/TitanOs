/* TitanOs - Visionneuse d'images (PNG et BMP)
 *
 * L'image est ajustée à la fenêtre ; la version réduite est calculée une fois par taille
 * de fenêtre (moyenne des pixels couverts, pour une réduction nette). Flèches gauche et
 * droite : image précédente ou suivante du même dossier. */
#include "desk.h"

bool png_decode(const uint8_t *f, size_t n, canvas_t *out);
bool bmp_decode(const uint8_t *f, size_t n, canvas_t *out);

typedef struct {
    char path[512];
    canvas_t img, fit;          /* image d'origine, et réduite à la taille affichée */
    int fit_w, fit_h;
    bool bad;
} view_t;

static bool is_image(const char *n) { return str_ends(n, ".png") || str_ends(n, ".PNG") || str_ends(n, ".bmp") || str_ends(n, ".BMP"); }

static void load(window_t *w, view_t *v, const char *path) {
    canvas_free(&v->img);
    canvas_free(&v->fit);
    v->fit_w = v->fit_h = 0;
    strlcpy(v->path, path, sizeof v->path);
    char *data;
    size_t n;
    v->bad = true;
    if (plat_readfile(path, &data, &n) == 0) {
        v->bad = !(png_decode((const uint8_t *)data, n, &v->img) || bmp_decode((const uint8_t *)data, n, &v->img));
        free(data);
    }
    const char *base = strrchr(path, '/');
    char t[96];
    snprintf(t, sizeof t, "%s", base ? base + 1 : path);
    wm_set_title(w, t);
    wm_invalidate(w);
}

/* réduction par moyenne des surfaces (ou agrandissement au plus proche) */
static void make_fit(view_t *v, int W, int H) {
    canvas_free(&v->fit);
    v->fit = canvas_new(W, H);
    if (!v->fit.px) return;
    const canvas_t *s = &v->img;
    for (int y = 0; y < H; y++) {
        int y0 = y * s->h / H, y1 = MAX(y0 + 1, (y + 1) * s->h / H);
        for (int x = 0; x < W; x++) {
            int x0 = x * s->w / W, x1 = MAX(x0 + 1, (x + 1) * s->w / W);
            uint32_t a = 0, r = 0, g = 0, b = 0, cnt = 0;
            int step = (x1 - x0) * (y1 - y0) > 64 ? 2 : 1;        /* très grande réduction : un pixel sur deux */
            for (int j = y0; j < y1; j += step)
                for (int i = x0; i < x1; i += step) {
                    uint32_t p = s->px[(size_t)j * s->stride + i];
                    a += p >> 24; r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; cnt++;
                }
            v->fit.px[(size_t)y * v->fit.stride + x] = (a / cnt) << 24 | (r / cnt) << 16 | (g / cnt) << 8 | (b / cnt);
        }
    }
    v->fit_w = W;
    v->fit_h = H;
}

static void view_draw(window_t *w, canvas_t *c) {
    view_t *v = w->data;
    gfx_fill(c, R(0, 0, c->w, c->h), 0x40000000);
    if (v->bad) {
        text_center(c, &font_body, R(0, c->h / 2 - 12, c->w, 24), "Image illisible : formats pris en charge, PNG et BMP.", C_MUTED);
        return;
    }
    int bw = c->w - 32, bh = c->h - 56;
    float s = MIN((float)bw / v->img.w, (float)bh / v->img.h);
    if (s > 1) s = 1;                                       /* pas d'agrandissement au-delà de 100 % */
    int W = MAX(1, (int)(v->img.w * s)), H = MAX(1, (int)(v->img.h * s));
    if (W != v->fit_w || H != v->fit_h) make_fit(v, W, H);
    int x = (c->w - W) / 2, y = 16 + (bh - H) / 2;
    gfx_shadow(c, x, y, W, H, 4, 18, 6, 0x60000000);
    gfx_blit(c, x, y, &v->fit, 255);
    char info[96];
    snprintf(info, sizeof info, "%d × %d · %d %% · ← → images du dossier", v->img.w, v->img.h, (int)(s * 100 + 0.5f));
    text_center(c, &font_small, R(0, c->h - 32, c->w, 20), info, C_MUTED);
}

/* image voisine dans le dossier (ordre alphabétique) */
static void neighbour(window_t *w, view_t *v, int dir) {
    char folder[512];
    strlcpy(folder, v->path, sizeof folder);
    char *slash = strrchr(folder, '/');
    if (!slash) return;
    *slash = 0;
    const char *cur = slash + 1;
    dirent_t *list;
    int n = plat_listdir(folder[0] ? folder : "/", &list);
    if (n <= 0) return;
    const char *best = NULL;
    for (int i = 0; i < n; i++) {
        if (list[i].dir || !is_image(list[i].name)) continue;
        int c = strcmp(list[i].name, cur);
        if (dir > 0 && c > 0 && (!best || strcmp(list[i].name, best) < 0)) best = list[i].name;
        if (dir < 0 && c < 0 && (!best || strcmp(list[i].name, best) > 0)) best = list[i].name;
    }
    if (best) {
        char p[512];
        snprintf(p, sizeof p, "%s/%s", folder, best);
        load(w, v, p);
    }
    free(list);
}

static void view_event(window_t *w, const event_t *e) {
    view_t *v = w->data;
    if (e->type != EV_KEY || !e->down) return;
    if (e->key == K_RIGHT) neighbour(w, v, 1);
    else if (e->key == K_LEFT) neighbour(w, v, -1);
    else if (e->key == K_ESC) wm_close(w);
}

static void view_close(window_t *w) {
    view_t *v = w->data;
    canvas_free(&v->img);
    canvas_free(&v->fit);
    free(v);
}

void app_image_open(const char *path) {
    view_t *v = calloc(1, sizeof *v);
    if (!v) return;
    window_t *w = wm_open("Image", ICON_IMAGE, 900, 640, WF_RESIZABLE);
    if (!w) { free(v); return; }
    w->minw = 360;
    w->data = v;
    w->draw = view_draw;
    w->event = view_event;
    w->on_close = view_close;
    load(w, v, path);
}
