/* TitanOs - logo et icônes, dessinés en code (formes lissées) */
#include "desk.h"

/* Logo TitanOs : une montagne stylisée (le « Titan ») avec une arête claire.
 * Base = 1,2 x hauteur ; dégradé bleu ciel -> violet, petite montagne sombre devant. */
void draw_logo(canvas_t *c, float cx, float top, float s, int alpha) {
    uint32_t a = (uint32_t)alpha << 24;
    float half = s * 0.6f, inner = s * 0.45f;
    gfx_tri(c, cx, top, cx - half, top + s, cx + half, top + s, a | 0x7DD3FC, a | 0x6D28D9);
    gfx_tri(c, cx, top + s - inner, cx - inner * 0.6f, top + s, cx + inner * 0.6f, top + s, a | 0x1E1B4B, a | 0x312E81);
    float rw = s / 50 < 1.6f ? 1.6f : s / 50;
    gfx_rrect(c, cx - rw / 2, top + s / 6, rw, s - s / 6 - inner, rw / 2, a | 0xE0F2FE);
}

/* fond d'icône d'application : carré arrondi en dégradé, reflet et liseré façon verre */
static void squircle(canvas_t *c, float x, float y, float s, uint32_t top, uint32_t bottom) {
    float r = s * 0.25f;
    gfx_rrect_grad(c, x, y, s, s, r, OPAQUE(top), OPAQUE(bottom));
    gfx_rrect_grad(c, x + 1, y + 1, s - 2, s * 0.48f, r - 1, 0x40FFFFFF, 0x00FFFFFF);
    gfx_rrect_stroke(c, x, y, s, s, r, s > 40 ? 1.2f : 1.0f, 0x50FFFFFF);
}

void icon_squircle(canvas_t *c, float x, float y, float s, uint32_t top, uint32_t bottom) { squircle(c, x, y, s, top, bottom); }

static void folder(canvas_t *c, float x, float y, float s, uint32_t back, uint32_t front_top, uint32_t front_bot) {
    gfx_rrect(c, x + s * 0.06f, y + s * 0.16f, s * 0.42f, s * 0.24f, s * 0.06f, OPAQUE(back));
    gfx_rrect_grad(c, x + s * 0.06f, y + s * 0.26f, s * 0.88f, s * 0.6f, s * 0.08f, OPAQUE(front_top), OPAQUE(front_bot));
    gfx_rrect(c, x + s * 0.06f, y + s * 0.26f, s * 0.88f, s * 0.05f, s * 0.02f, 0x60FFFFFF);
}

static void paper(canvas_t *c, float x, float y, float s, uint32_t header) {
    float px = x + s * 0.17f, pw = s * 0.66f;
    gfx_rrect(c, px, y + s * 0.06f, pw, s * 0.88f, s * 0.08f, 0xFFFFFFFF);
    if (header) gfx_rrect(c, px, y + s * 0.06f, pw, s * 0.2f, s * 0.08f, OPAQUE(header));
    if (header) gfx_fill(c, R((int)px, (int)(y + s * 0.18f), (int)pw + 1, (int)(s * 0.08f)), OPAQUE(header));
    for (int k = 0; k < 4; k++)
        gfx_rrect(c, px + s * 0.1f, y + s * (0.38f + k * 0.13f), pw - s * (k == 3 ? 0.34f : 0.2f), s * 0.045f + 0.5f, 1, 0xFF94A3B8);
}

void draw_icon(canvas_t *c, int icon, float x, float y, float s) {
    float cx = x + s / 2, cy = y + s / 2, t = s / 14 < 1.6f ? 1.6f : s / 14;
    if (icon >= STORE_BASE && icon < STORE_BASE + STORE_MAX) { store_draw_icon(c, icon - STORE_BASE, x, y, s); return; }
    switch (icon) {
    case ICON_TERMINAL:
        squircle(c, x, y, s, 0x283447, 0x0B1220);
        gfx_line(c, x + s * 0.24f, y + s * 0.36f, x + s * 0.42f, y + s * 0.52f, t, 0xFF34D399);
        gfx_line(c, x + s * 0.42f, y + s * 0.52f, x + s * 0.24f, y + s * 0.68f, t, 0xFF34D399);
        gfx_line(c, x + s * 0.5f, y + s * 0.68f, x + s * 0.76f, y + s * 0.68f, t, 0xFFE5E7EB);
        break;
    case ICON_FILES:
        squircle(c, x, y, s, 0x60A5FA, 0x2563EB);
        folder(c, x + s * 0.12f, y + s * 0.12f, s * 0.76f, 0xD97706, 0xFCD34D, 0xF59E0B);
        break;
    case ICON_FOLDER:
        folder(c, x, y, s, 0xD97706, 0xFCD34D, 0xF59E0B);
        break;
    case ICON_NOTES:
        squircle(c, x, y, s, 0xFDE68A, 0xF59E0B);
        paper(c, x + s * 0.08f, y + s * 0.08f, s * 0.84f, 0xF97316);
        break;
    case ICON_FILE:
        paper(c, x, y, s, 0);
        gfx_rrect_stroke(c, x + s * 0.17f, y + s * 0.06f, s * 0.66f, s * 0.88f, s * 0.08f, 1, 0xFFCBD5E1);
        break;
    case ICON_IMAGE:
        gfx_rrect_grad(c, x + s * 0.06f, y + s * 0.12f, s * 0.88f, s * 0.76f, s * 0.1f, 0xFF7DD3FC, 0xFF38BDF8);
        gfx_circle(c, x + s * 0.7f, y + s * 0.34f, s * 0.09f, 0xFFFDE68A);
        gfx_tri(c, x + s * 0.32f, y + s * 0.42f, x + s * 0.1f, y + s * 0.84f, x + s * 0.6f, y + s * 0.84f, 0xFF22C55E, 0xFF15803D);
        gfx_tri(c, x + s * 0.62f, y + s * 0.56f, x + s * 0.42f, y + s * 0.84f, x + s * 0.88f, y + s * 0.84f, 0xFF4ADE80, 0xFF16A34A);
        break;
    case ICON_CALC: {
        squircle(c, x, y, s, 0x475569, 0x1E293B);
        gfx_rrect(c, x + s * 0.16f, y + s * 0.15f, s * 0.68f, s * 0.2f, s * 0.05f, 0xFFCBD5E1);
        float k = s * 0.18f, g = s * 0.07f;
        for (int j = 0; j < 3; j++)
            for (int i = 0; i < 3; i++)
                gfx_rrect(c, x + s * 0.16f + i * (k + g), y + s * 0.43f + j * (k * 0.72f + g * 0.6f), k, k * 0.72f, s * 0.04f,
                          (i == 2 && j == 2) ? 0xFFF97316 : 0xFF64748B);
        break;
    }
    case ICON_SETTINGS: {
        squircle(c, x, y, s, 0x94A3B8, 0x475569);
        float rr = s * 0.25f;
        for (int k = 0; k < 4; k++) {
            float ang[4][2] = { { 1, 0 }, { 0, 1 }, { 0.7071f, 0.7071f }, { 0.7071f, -0.7071f } };
            float dx = ang[k][0] * (rr + s * 0.09f), dy = ang[k][1] * (rr + s * 0.09f);
            gfx_line(c, cx - dx, cy - dy, cx + dx, cy + dy, s * 0.13f, 0xFFF1F5F9);
        }
        gfx_circle(c, cx, cy, rr, 0xFFF1F5F9);
        gfx_circle(c, cx, cy, rr * 0.45f, 0xFF64748B);
        break;
    }
    case ICON_SYSTEM:
        squircle(c, x, y, s, 0x60A5FA, 0x1D4ED8);
        gfx_rrect(c, x + s * 0.2f, y + s * 0.24f, s * 0.6f, s * 0.4f, s * 0.05f, 0xFFE0F2FE);
        gfx_rrect(c, x + s * 0.25f, y + s * 0.29f, s * 0.5f, s * 0.3f, s * 0.03f, 0xFF1E3A8A);
        gfx_rrect(c, x + s * 0.42f, y + s * 0.64f, s * 0.16f, s * 0.08f, 1, 0xFFE0F2FE);
        gfx_rrect(c, x + s * 0.3f, y + s * 0.71f, s * 0.4f, s * 0.06f, s * 0.03f, 0xFFE0F2FE);
        draw_logo(c, cx, y + s * 0.33f, s * 0.2f, 255);
        break;
    case ICON_NETWORK: {
        squircle(c, x, y, s, 0x38BDF8, 0x0369A1);
        float rr = s * 0.3f;
        gfx_ring(c, cx, cy, rr, t * 0.8f, 0xFFE0F2FE);
        gfx_line(c, cx - rr, cy, cx + rr, cy, t * 0.7f, 0xFFE0F2FE);
        gfx_line(c, cx, cy - rr, cx, cy + rr, t * 0.7f, 0xFFE0F2FE);
        gfx_rrect_stroke(c, cx - rr * 0.45f, cy - rr, rr * 0.9f, rr * 2, rr * 0.45f, t * 0.7f, 0xFFE0F2FE);
        break;
    }
    case ICON_SNAKE:
        squircle(c, x, y, s, 0x22C55E, 0x14532D);
        gfx_line(c, x + s * 0.22f, y + s * 0.7f, x + s * 0.5f, y + s * 0.7f, s * 0.13f, 0xFFBBF7D0);
        gfx_line(c, x + s * 0.5f, y + s * 0.7f, x + s * 0.5f, y + s * 0.36f, s * 0.13f, 0xFF86EFAC);
        gfx_line(c, x + s * 0.5f, y + s * 0.36f, x + s * 0.72f, y + s * 0.36f, s * 0.13f, 0xFF4ADE80);
        gfx_circle(c, x + s * 0.76f, y + s * 0.7f, s * 0.08f, 0xFFEF4444);
        break;
    case ICON_ABOUT:
        squircle(c, x, y, s, 0x312E81, 0x1E1B4B);
        draw_logo(c, cx, y + s * 0.2f, s * 0.58f, 255);
        break;
    case ICON_BROWSER: {         /* navigateur : l'icône de Google Chrome, sinon une fenêtre avec un globe */
        if (browser_icon(c, x, y, s)) break;
        squircle(c, x, y, s, 0x38BDF8, 0x4338CA);
        gfx_rrect(c, x + s * 0.17f, y + s * 0.22f, s * 0.66f, s * 0.56f, s * 0.08f, 0xF2FFFFFF);
        gfx_rrect(c, x + s * 0.17f, y + s * 0.22f, s * 0.3f, s * 0.1f, s * 0.04f, 0xFF6366F1);
        gfx_rrect(c, x + s * 0.23f, y + s * 0.36f, s * 0.54f, s * 0.06f, s * 0.03f, 0xFFCBD5E1);
        gfx_ring(c, cx, y + s * 0.6f, s * 0.1f, t * 0.6f, 0xFF4338CA);
        gfx_line(c, cx - s * 0.1f, y + s * 0.6f, cx + s * 0.1f, y + s * 0.6f, t * 0.5f, 0xFF4338CA);
        break;
    }
    case ICON_STORE: {           /* Logithèque : sac avec une flèche de téléchargement */
        squircle(c, x, y, s, 0xFB923C, 0xDB2777);
        gfx_arc(c, cx, y + s * 0.36f, s * 0.13f, t * 0.8f, 3.14159f, 6.28318f, 0xFFFFFFFF);          /* anse */
        gfx_rrect(c, x + s * 0.24f, y + s * 0.34f, s * 0.52f, s * 0.44f, s * 0.08f, 0xF2FFFFFF);       /* sac */
        gfx_line(c, cx, y + s * 0.44f, cx, y + s * 0.64f, t * 0.8f, 0xFFDB2777);
        gfx_line(c, cx - s * 0.09f, y + s * 0.56f, cx, y + s * 0.65f, t * 0.8f, 0xFFDB2777);
        gfx_line(c, cx + s * 0.09f, y + s * 0.56f, cx, y + s * 0.65f, t * 0.8f, 0xFFDB2777);
        break;
    }
    case ICON_UPDATE: {          /* flèche circulaire : mise à jour */
        squircle(c, x, y, s, 0x34D399, 0x047857);
        float rr = s * 0.24f;
        gfx_arc(c, cx, cy, rr, t * 0.9f, -1.2f, 3.9f, 0xFFECFDF5);
        float ax = cx + fcos(-1.2f) * rr, ay = cy + fsin(-1.2f) * rr;
        gfx_tri(c, ax + s * 0.11f, ay - s * 0.02f, ax - s * 0.06f, ay - s * 0.11f, ax - s * 0.03f, ay + s * 0.09f, 0xFFECFDF5, 0xFFECFDF5);
        break;
    }
    case ICON_LOCK:
        gfx_ring(c, cx, y + s * 0.38f, s * 0.2f, t, 0xFFE2E8F0);
        gfx_rrect(c, x + s * 0.22f, y + s * 0.42f, s * 0.56f, s * 0.44f, s * 0.08f, 0xFFE2E8F0);
        gfx_circle(c, cx, y + s * 0.62f, s * 0.06f, 0xFF334155);
        break;
    case ICON_REBOOT:
    case ICON_POWER: {
        uint32_t col = icon == ICON_REBOOT ? 0xFFFBBF24 : 0xFFF87171;
        float rr = s * 0.32f, gap = icon == ICON_POWER ? 0.62f : 0.35f;
        gfx_arc(c, cx, cy, rr, t, -1.5708f + gap, 4.7124f - gap, col);       /* cercle ouvert en haut */
        if (icon == ICON_POWER) {
            gfx_line(c, cx, y + s * 0.12f, cx, y + s * 0.48f, t, col);
        } else {
            float ax = cx - fsin(gap) * rr, ay = cy - fcos(gap) * rr;       /* bout de l'arc : flèche */
            gfx_tri(c, ax - s * 0.13f, ay - s * 0.02f, ax + s * 0.06f, ay - s * 0.15f, ax + s * 0.05f, ay + s * 0.11f, col, col);
        }
        break;
    }
    case ICON_HOME:
        gfx_tri(c, cx, y + s * 0.12f, x + s * 0.08f, y + s * 0.5f, x + s * 0.92f, y + s * 0.5f, 0xFF93C5FD, 0xFF60A5FA);
        gfx_rrect(c, x + s * 0.2f, y + s * 0.46f, s * 0.6f, s * 0.42f, 2, 0xFF60A5FA);
        gfx_rrect(c, cx - s * 0.08f, y + s * 0.62f, s * 0.16f, s * 0.26f, 2, 0xFF1E3A8A);
        break;
    case ICON_USB:
        gfx_rrect(c, x + s * 0.3f, y + s * 0.3f, s * 0.4f, s * 0.6f, s * 0.08f, 0xFF94A3B8);
        gfx_rrect(c, x + s * 0.36f, y + s * 0.1f, s * 0.28f, s * 0.22f, 2, 0xFFE2E8F0);
        gfx_rrect(c, x + s * 0.41f, y + s * 0.15f, s * 0.06f, s * 0.06f, 1, 0xFF475569);
        gfx_rrect(c, x + s * 0.53f, y + s * 0.15f, s * 0.06f, s * 0.06f, 1, 0xFF475569);
        break;
    case ICON_DISK:
        gfx_rrect_grad(c, x + s * 0.08f, y + s * 0.28f, s * 0.84f, s * 0.44f, s * 0.1f, 0xFFCBD5E1, 0xFF94A3B8);
        gfx_circle(c, x + s * 0.76f, cy, s * 0.05f, 0xFF22C55E);
        break;
    case ICON_SEARCH:
        gfx_ring(c, x + s * 0.42f, y + s * 0.42f, s * 0.26f, t, 0xFFCBD5E1);
        gfx_line(c, x + s * 0.62f, y + s * 0.62f, x + s * 0.86f, y + s * 0.86f, t * 1.2f, 0xFFCBD5E1);
        break;
    case ICON_WIFI:
        draw_wifi(c, cx, cy, s, 4, 0xFFFFFFFF, 0xFF64748B);
        break;
    case ICON_ETHERNET:
        gfx_rrect(c, x + s * 0.3f, y + s * 0.12f, s * 0.4f, s * 0.26f, s * 0.05f, 0xFFFFFFFF);
        gfx_rrect(c, cx - t / 2, y + s * 0.38f, t, s * 0.18f, 1, 0xFFFFFFFF);
        gfx_rrect(c, x + s * 0.12f, y + s * 0.55f, s * 0.76f, t, 1, 0xFFFFFFFF);
        for (int k = 0; k < 3; k++)
            gfx_rrect(c, x + s * (0.1f + k * 0.3f), y + s * 0.55f, s * 0.2f, s * 0.3f, s * 0.04f, 0xFFFFFFFF);
        break;
    case ICON_VOLUME:
    case ICON_MUTE:
        draw_speaker(c, cx, cy, s, 3, icon == ICON_MUTE, 0xFFFFFFFF);
        break;
    case ICON_BATTERY:
        draw_battery(c, x + s * 0.08f, y + s * 0.3f, s * 0.84f, s * 0.4f, 70, false, 0xFFFFFFFF);
        break;
    case ICON_BRIGHTNESS:
        gfx_circle(c, cx, cy, s * 0.18f, 0xFFFFFFFF);
        for (int k = 0; k < 8; k++) {
            float a = k * 0.7854f;
            gfx_line(c, cx + fcos(a) * s * 0.3f, cy + fsin(a) * s * 0.3f, cx + fcos(a) * s * 0.42f, cy + fsin(a) * s * 0.42f,
                     t * 0.9f, 0xFFFFFFFF);
        }
        break;
    }
}

/* Wi-Fi : point et trois arcs, allumés selon le niveau du signal (0-4) */
void draw_wifi(canvas_t *c, float cx, float cy, float s, int level, uint32_t col, uint32_t dim) {
    float by = cy + s * 0.3f, t = s / 9 < 1.6f ? 1.6f : s / 9;
    gfx_circle(c, cx, by - t * 0.4f, t * 0.85f, level >= 1 ? col : dim);
    for (int k = 1; k <= 3; k++)
        gfx_arc(c, cx, by, s * (0.16f + 0.17f * k), t, -2.36f, -0.785f, level >= k + 1 ? col : dim);
}

void draw_battery(canvas_t *c, float x, float y, float w, float h, int percent, bool charging, uint32_t col) {
    float r = h * 0.28f, nub = w * 0.07f;
    gfx_rrect_stroke(c, x, y, w - nub, h, r, h / 9 < 1.3f ? 1.3f : h / 9, col);
    gfx_rrect(c, x + w - nub + 1, y + h * 0.3f, nub - 1, h * 0.4f, nub / 2, col);
    uint32_t fill = percent <= 10 ? 0xFFF87171 : percent <= 20 ? 0xFFFBBF24 : charging ? 0xFF4ADE80 : col;
    float in = h * 0.2f, fw = (w - nub - 2 * in) * CLAMP(percent, 0, 100) / 100.0f;
    if (fw > 0.5f) gfx_rrect(c, x + in, y + in, fw, h - 2 * in, r * 0.6f, fill);
    if (charging) {
        float cx = x + (w - nub) / 2, cy = y + h / 2;
        gfx_tri(c, cx + h * 0.08f, cy - h * 0.42f, cx - h * 0.22f, cy + h * 0.06f, cx + h * 0.02f, cy + h * 0.06f, 0xFF0F172A, 0xFF0F172A);
        gfx_tri(c, cx - h * 0.08f, cy + h * 0.42f, cx + h * 0.22f, cy - h * 0.06f, cx - h * 0.02f, cy - h * 0.06f, 0xFF0F172A, 0xFF0F172A);
    }
}

/* haut-parleur : niveau 0-3 (ondes), ou barré si coupé */
void draw_speaker(canvas_t *c, float cx, float cy, float s, int level, bool muted, uint32_t col) {
    float x = cx - s * 0.36f, t = s / 10 < 1.5f ? 1.5f : s / 10;
    gfx_rrect(c, x, cy - s * 0.12f, s * 0.16f, s * 0.24f, 1, col);
    gfx_tri(c, x + s * 0.1f, cy - s * 0.12f, x + s * 0.36f, cy - s * 0.34f, x + s * 0.1f, cy + s * 0.12f, col, col);
    gfx_tri(c, x + s * 0.1f, cy + s * 0.12f, x + s * 0.36f, cy - s * 0.34f, x + s * 0.36f, cy + s * 0.34f, col, col);
    if (muted) {
        gfx_line(c, cx + s * 0.1f, cy - s * 0.12f, cx + s * 0.34f, cy + s * 0.12f, t, col);
        gfx_line(c, cx + s * 0.1f, cy + s * 0.12f, cx + s * 0.34f, cy - s * 0.12f, t, col);
        return;
    }
    for (int k = 1; k <= 3; k++)
        if (level >= k) gfx_arc(c, cx + s * 0.02f, cy, s * (0.1f + 0.11f * k), t, -0.8f, 0.8f, col);
}
