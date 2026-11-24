/* TitanOs - écran : DRM/KMS (tampons « dumb ») ou, à défaut, /dev/fb0
 *
 * Avec le vrai pilote de la carte graphique, deux images sont gardées en mémoire vidéo :
 * on dessine dans celle qui n'est pas affichée, puis on demande à la carte de basculer au
 * prochain rafraîchissement de l'écran (page flip, synchronisé sur 60 Hz). Plus de
 * déchirure, et les animations suivent exactement le rythme de l'écran.
 *
 * Au démarrage, l'écran est celui préparé par le chargeur (VBE ou GOP), exposé par le
 * pilote simpledrm du noyau. Dès que le vrai pilote de la carte graphique est chargé
 * (nouveau sur le VAIO, i915 sur un PC Intel...), on rouvre l'écran à sa résolution native. */
#include "linux.h"
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <linux/fb.h>

static struct {
    int fd;
    bool drm, dirty_ok;
    uint32_t *map;              /* image où l'on dessine (celle qui n'est pas affichée en mode double) */
    size_t size;
    int w, h, pitch;            /* pitch en pixels */
    uint32_t fb_id, handle, crtc, conn;
    char driver[32];
    int card, hz;
    /* double tampon */
    bool flip;                  /* bascule synchronisée active */
    bool pending;               /* une bascule attend le prochain rafraîchissement */
    int back, misses;
    uint32_t *maps[2], fbs[2], handles[2];
    rect_t last;                /* zone changée à l'image précédente */
} D = { .fd = -1 };

int display_w(void) { return D.w; }
int display_h(void) { return D.h; }
const char *display_driver(void) { return D.driver; }

static int drm_driver_name(int fd, char *out, int n) {
    struct drm_version v = { 0 };
    v.name = out;
    v.name_len = (size_t)n - 1;
    if (sys_ioctl(fd, DRM_IOCTL_VERSION, &v) < 0) { out[0] = 0; return -1; }
    out[MIN((int)v.name_len, n - 1)] = 0;
    return 0;
}

/* choisit un connecteur branché et son mode préféré */
static bool drm_pick(int fd, uint32_t *conn_out, uint32_t *crtc_out, struct drm_mode_modeinfo *mode_out) {
    struct drm_mode_card_res res = { 0 };
    if (sys_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return false;
    uint32_t conns[32], crtcs[32], encs[32];
    res.count_connectors = MIN(res.count_connectors, 32u);
    res.count_crtcs = MIN(res.count_crtcs, 32u);
    res.count_encoders = MIN(res.count_encoders, 32u);
    res.count_fbs = 0;
    res.connector_id_ptr = (uint64_t)(uintptr_t)conns;
    res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
    res.encoder_id_ptr = (uint64_t)(uintptr_t)encs;
    if (sys_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return false;
    int best = -1, best_score = -1;
    struct drm_mode_modeinfo best_mode;
    uint32_t best_enc = 0, best_encs[16];
    int best_nenc = 0;
    for (uint32_t i = 0; i < res.count_connectors; i++) {
        struct drm_mode_get_connector c = { 0 };
        c.connector_id = conns[i];
        if (sys_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) continue;   /* sonde l'écran */
        if (c.connection != 1 || c.count_modes == 0) continue;
        struct drm_mode_modeinfo modes[64];
        uint32_t ce[16];
        c.count_modes = MIN(c.count_modes, 64u);
        c.count_encoders = MIN(c.count_encoders, 16u);
        c.count_props = 0;
        c.modes_ptr = (uint64_t)(uintptr_t)modes;
        c.encoders_ptr = (uint64_t)(uintptr_t)ce;
        c.props_ptr = c.prop_values_ptr = 0;
        if (sys_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0 || c.count_modes == 0) continue;
        int m = 0;
        for (uint32_t k = 0; k < c.count_modes; k++) if (modes[k].type & DRM_MODE_TYPE_PREFERRED) { m = (int)k; break; }
        /* écran interne des portables en priorité (LVDS = 7, eDP = 14) */
        int score = (c.connector_type == 7 || c.connector_type == 14) ? 2 : 1;
        if (score > best_score) {
            best_score = score;
            best = (int)i;
            best_mode = modes[m];
            best_enc = c.encoder_id;
            best_nenc = (int)c.count_encoders;
            memcpy(best_encs, ce, sizeof(uint32_t) * (size_t)best_nenc);
        }
    }
    if (best < 0) return false;
    uint32_t crtc = 0;
    if (best_enc) {
        struct drm_mode_get_encoder e = { 0 };
        e.encoder_id = best_enc;
        if (sys_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &e) == 0) crtc = e.crtc_id;
    }
    for (int k = 0; !crtc && k < best_nenc; k++) {
        struct drm_mode_get_encoder e = { 0 };
        e.encoder_id = best_encs[k];
        if (sys_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &e) < 0) continue;
        for (uint32_t j = 0; j < res.count_crtcs; j++)
            if (e.possible_crtcs & (1u << j)) { crtc = crtcs[j]; break; }
    }
    if (!crtc) return false;
    *conn_out = conns[best];
    *crtc_out = crtc;
    *mode_out = best_mode;
    return true;
}

static bool make_buffer(int fd, int w, int h, uint32_t *handle, uint32_t *fb_id, uint32_t **map, size_t *size, int *pitch) {
    struct drm_mode_create_dumb cd = { 0 };
    cd.width = (uint32_t)w;
    cd.height = (uint32_t)h;
    cd.bpp = 32;
    if (sys_ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) return false;
    struct drm_mode_fb_cmd fb = { 0 };
    fb.width = (uint32_t)w;
    fb.height = (uint32_t)h;
    fb.pitch = cd.pitch;
    fb.bpp = 32;
    fb.depth = 24;
    fb.handle = cd.handle;
    struct drm_mode_map_dumb md = { 0 };
    md.handle = cd.handle;
    void *m = (void *)-1;
    if (sys_ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fb) == 0 && sys_ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md) == 0)
        m = sys_mmap(NULL, (size_t)cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (long)md.offset);
    if ((long)m < 0 && (long)m > -4096) m = (void *)-1;
    if (m == (void *)-1) return false;
    memset(m, 0, (size_t)cd.size);
    *handle = cd.handle;
    *fb_id = fb.fb_id;
    *map = m;
    *size = (size_t)cd.size;
    *pitch = (int)(cd.pitch / 4);
    return true;
}

static bool drm_open(int card) {
    char path[32];
    snprintf(path, sizeof path, "/dev/dri/card%d", card);
    int fd = sys_open(path, O_RDWR | O_CLOEXEC, 0);
    if (fd < 0) return false;
    sys_ioctl(fd, DRM_IOCTL_SET_MASTER, NULL);
    struct drm_mode_modeinfo mode;
    uint32_t conn, crtc;
    if (!drm_pick(fd, &conn, &crtc, &mode)) { sys_close(fd); return false; }
    uint32_t handle, fb_id, *map;
    size_t size;
    int pitch;
    if (!make_buffer(fd, mode.hdisplay, mode.vdisplay, &handle, &fb_id, &map, &size, &pitch)) { sys_close(fd); return false; }
    struct drm_mode_crtc sc = { 0 };
    sc.crtc_id = crtc;
    sc.fb_id = fb_id;
    sc.set_connectors_ptr = (uint64_t)(uintptr_t)&conn;
    sc.count_connectors = 1;
    sc.mode = mode;
    sc.mode_valid = 1;
    if (sys_ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &sc) < 0) { sys_munmap(map, size); sys_close(fd); return false; }
    D.fd = fd;
    D.drm = true;
    D.dirty_ok = true;
    D.map = map;
    D.size = size;
    D.w = mode.hdisplay;
    D.h = mode.vdisplay;
    D.pitch = pitch;
    D.fb_id = fb_id;
    D.handle = handle;
    D.crtc = crtc;
    D.conn = conn;
    D.card = card;
    D.hz = mode.vrefresh;
    D.flip = false;
    D.pending = false;
    D.misses = 0;
    drm_driver_name(fd, D.driver, sizeof D.driver);
    /* double tampon avec le vrai pilote (simpledrm n'a pas de synchronisation verticale) */
    if (strcmp(D.driver, "simpledrm")) {
        int p2;
        size_t s2;
        if (make_buffer(fd, D.w, D.h, &D.handles[1], &D.fbs[1], &D.maps[1], &s2, &p2) && p2 == D.pitch) {
            D.maps[0] = map; D.fbs[0] = fb_id; D.handles[0] = handle;
            D.back = 1;
            D.map = D.maps[1];
            D.flip = true;
            D.last = R(0, 0, D.w, D.h);
        }
    }
    klog("écran : %s, %dx%d à %d Hz%s", D.driver, D.w, D.h, D.hz, D.flip ? ", double tampon synchronisé" : "");
    return true;
}

static bool fb_open(void) {
    int fd = sys_open("/dev/fb0", O_RDWR | O_CLOEXEC, 0);
    if (fd < 0) return false;
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (sys_ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0 || sys_ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0 || v.bits_per_pixel != 32) {
        sys_close(fd);
        return false;
    }
    void *map = sys_mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((long)map < 0 && (long)map > -4096) { sys_close(fd); return false; }
    D.fd = fd;
    D.drm = false;
    D.map = map;
    D.size = f.smem_len;
    D.w = (int)v.xres;
    D.h = (int)v.yres;
    D.pitch = (int)(f.line_length / 4);
    strlcpy(D.driver, "fbdev", sizeof D.driver);
    return true;
}

void display_close(void) {
    if (D.fd < 0) return;
    if (D.flip) {
        for (int i = 0; i < 2; i++) {
            sys_munmap(D.maps[i], D.size);
            sys_ioctl(D.fd, DRM_IOCTL_MODE_RMFB, &D.fbs[i]);
            struct drm_mode_destroy_dumb dd = { D.handles[i] };
            sys_ioctl(D.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        }
        D.flip = false;
    } else {
        if (D.map) sys_munmap(D.map, D.size);
        if (D.drm) {
            sys_ioctl(D.fd, DRM_IOCTL_MODE_RMFB, &D.fb_id);
            struct drm_mode_destroy_dumb dd = { D.handle };
            sys_ioctl(D.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        }
    }
    sys_close(D.fd);
    D.fd = -1;
    D.map = NULL;
}

static bool is_simple(int card) {
    char p[64], fd_name[32];
    snprintf(p, sizeof p, "/dev/dri/card%d", card);
    int fd = sys_open(p, O_RDWR | O_CLOEXEC, 0);
    if (fd < 0) return true;
    drm_driver_name(fd, fd_name, sizeof fd_name);
    sys_close(fd);
    return !strcmp(fd_name, "simpledrm");
}

bool display_open(void) {
    display_close();
    /* d'abord une vraie carte graphique, puis simpledrm, puis /dev/fb0 */
    for (int pass = 0; pass < 2; pass++)
        for (int c = 0; c < 8; c++) {
            char p[32];
            snprintf(p, sizeof p, "/dev/dri/card%d", c);
            if (!path_exists(p) || is_simple(c) != (pass == 1)) continue;
            if (drm_open(c)) return true;
        }
    return fb_open();
}

bool display_better_available(void) {
    if (D.drm && strcmp(D.driver, "simpledrm")) return false;
    for (int c = 0; c < 8; c++) {
        char p[32];
        snprintf(p, sizeof p, "/dev/dri/card%d", c);
        if (path_exists(p) && !is_simple(c)) return true;
    }
    return false;
}

uint64_t display_us;                   /* temps passé à envoyer les images (mesure) */
static uint64_t now_us(void) { struct timespec ts; sys_clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000; }

/* attend que la bascule demandée ait eu lieu (au plus 50 ms) */
static void wait_flip(void) {
    if (!D.pending) return;
    struct pollfd p = { D.fd, POLLIN, 0 };
    if (sys_poll(&p, 1, 50) > 0) {
        char ev[256];
        sys_read(D.fd, ev, sizeof ev);          /* événement « bascule faite » */
        D.misses = 0;
    } else if (++D.misses >= 3) {               /* le pilote ne répond pas : on repasse en simple */
        klog("écran : bascule synchronisée sans réponse, abandonnée");
        D.flip = false;
        D.map = D.maps[D.back ^ 1];
    }
    D.pending = false;
}

int display_hz(void) { return D.hz; }

void display_present(const uint32_t *scene, int stride, rect_t r) {
    if (!D.map) return;
    uint64_t t0 = now_us();
    if (D.flip) {
        r = rect_isect(r, R(0, 0, D.w, D.h));
        if (rect_empty(r)) return;
        wait_flip();
        if (D.flip) {
            /* l'image cachée date d'il y a deux images : on y recopie ce qui a changé depuis */
            rect_t u = rect_union(r, D.last);
            uint32_t *dst = D.maps[D.back];
            for (int j = 0; j < u.h; j++)
                memcpy(dst + (size_t)(u.y + j) * D.pitch + u.x, scene + (size_t)(u.y + j) * stride + u.x, (size_t)u.w * 4);
            struct drm_mode_crtc_page_flip pf = { 0 };
            pf.crtc_id = D.crtc;
            pf.fb_id = D.fbs[D.back];
            pf.flags = DRM_MODE_PAGE_FLIP_EVENT;
            long rr = sys_ioctl(D.fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf);
            if (rr == 0) {
                D.pending = true;
                D.back ^= 1;
                D.last = r;
                display_us += now_us() - t0;
                return;
            }
            klog("écran : bascule refusée (%ld), affichage simple", rr);
            D.flip = false;
            D.map = D.maps[D.back ^ 1];          /* l'image affichée */
        }
    }
    r = rect_isect(r, R(0, 0, D.w, D.h));
    if (rect_empty(r)) return;
    for (int j = 0; j < r.h; j++)
        memcpy(D.map + (size_t)(r.y + j) * D.pitch + r.x, scene + (size_t)(r.y + j) * stride + r.x, (size_t)r.w * 4);
    if (D.drm && D.dirty_ok) {
        struct drm_clip_rect clip = { (uint16_t)r.x, (uint16_t)r.y, (uint16_t)(r.x + r.w), (uint16_t)(r.y + r.h) };
        struct drm_mode_fb_dirty_cmd dc = { 0 };
        dc.fb_id = D.fb_id;
        dc.num_clips = 1;
        dc.clips_ptr = (uint64_t)(uintptr_t)&clip;
        long rr = sys_ioctl(D.fd, DRM_IOCTL_MODE_DIRTYFB, &dc);
        if (rr == -ENOSYS || rr == -EINVAL || rr == -EOPNOTSUPP) D.dirty_ok = false;
    }
    display_us += now_us() - t0;
}
