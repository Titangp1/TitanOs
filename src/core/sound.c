/* TitanOs - sons de l'interface, synthétisés au démarrage (aucun fichier audio)
 *
 * Chaque son est une petite partition de notes : sinus avec quelques harmoniques,
 * attaque douce et extinction exponentielle (comme une note de marimba ou de verre).
 * Les volumes restent bas : ce sont des sons d'interface, pas des alarmes. */
#include "desk.h"

#define SR 48000

static int16_t *pcm[SFX_COUNT];
static int frames[SFX_COUNT];


typedef struct { float start, f0, f1, amp, attack, tau, len, h2, h3; } note_t;

static void render(int id, const note_t *notes, int n, float seconds, float noise) {
    int len = (int)(seconds * SR);
    float *mix = calloc((size_t)len, sizeof(float));
    if (!mix) return;
    for (int k = 0; k < n; k++) {
        const note_t *nt = &notes[k];
        int s0 = (int)(nt->start * SR), ns = (int)(nt->len * SR);
        float phase = 0, env_decay = 1, decay = 1.0f - 1.0f / (nt->tau * SR);
        for (int i = 0; i < ns && s0 + i < len; i++) {
            float t = (float)i / SR, p = (float)i / ns;
            float f = nt->f0 + (nt->f1 - nt->f0) * p;
            phase += 6.28318531f * f / SR;
            if (phase > 6.28318531f) phase -= 6.28318531f;
            float att = t < nt->attack ? t / nt->attack : 1.0f;
            float rel = p > 0.85f ? (1 - p) / 0.15f : 1.0f;    /* fin sans « clic » */
            float v = fsin(phase) + nt->h2 * fsin(2 * phase) + nt->h3 * fsin(3 * phase);
            mix[s0 + i] += v * nt->amp * att * env_decay * rel;
            env_decay *= decay;
        }
    }
    if (noise > 0) {                                     /* petit souffle pour le « clic » */
        uint32_t r = 12345;
        float e = 1;
        for (int i = 0; i < len && e > 0.001f; i++) {
            r = r * 1664525u + 1013904223u;
            mix[i] += ((float)(r >> 9) / 4194304.0f - 1.0f) * noise * e;
            e *= 1.0f - 1.0f / (0.0015f * SR);
        }
    }
    int16_t *out = malloc((size_t)len * 4);
    if (!out) { free(mix); return; }
    for (int i = 0; i < len; i++) {
        float v = mix[i];
        if (v > 0.95f) v = 0.95f;
        if (v < -0.95f) v = -0.95f;
        out[2 * i] = out[2 * i + 1] = (int16_t)(v * 32767);
    }
    free(mix);
    pcm[id] = out;
    frames[id] = len;
}

void sound_init(void) {
    /*        début   f0        f1        amp    attaque tau    durée  h2    h3 */
    const note_t click[] = { { 0, 1750, 1500, 0.13f, 0.0006f, 0.005f, 0.03f, 0.15f, 0 } };
    const note_t tick[] = { { 0, 980, 980, 0.16f, 0.001f, 0.018f, 0.07f, 0.2f, 0 } };
    const note_t open[] = { { 0, 520, 880, 0.09f, 0.008f, 0.05f, 0.16f, 0.1f, 0 },
                            { 0.01f, 780, 1320, 0.04f, 0.008f, 0.05f, 0.15f, 0, 0 } };
    const note_t close[] = { { 0, 760, 420, 0.09f, 0.005f, 0.045f, 0.14f, 0.1f, 0 } };
    const note_t notify[] = { { 0, 1318.5f, 1318.5f, 0.13f, 0.003f, 0.20f, 0.6f, 0.25f, 0.08f },
                              { 0.11f, 1975.5f, 1975.5f, 0.10f, 0.003f, 0.28f, 0.6f, 0.2f, 0.05f } };
    const note_t error[] = { { 0, 523.25f, 523.25f, 0.13f, 0.004f, 0.10f, 0.3f, 0.3f, 0.1f },
                             { 0.1f, 392, 392, 0.13f, 0.004f, 0.18f, 0.35f, 0.3f, 0.1f } };
    const note_t startup[] = {
        { 0.00f, 261.63f, 261.63f, 0.045f, 0.35f, 0.9f, 1.9f, 0.1f, 0 },          /* nappe douce : do-sol */
        { 0.00f, 392.00f, 392.00f, 0.035f, 0.35f, 0.9f, 1.9f, 0.1f, 0 },
        { 0.05f, 523.25f, 523.25f, 0.08f, 0.004f, 0.5f, 1.5f, 0.2f, 0.05f },      /* arpège do mi sol si ré */
        { 0.12f, 659.26f, 659.26f, 0.07f, 0.004f, 0.5f, 1.4f, 0.2f, 0.05f },
        { 0.19f, 783.99f, 783.99f, 0.07f, 0.004f, 0.5f, 1.3f, 0.2f, 0.05f },
        { 0.26f, 987.77f, 987.77f, 0.06f, 0.004f, 0.5f, 1.2f, 0.15f, 0 },
        { 0.33f, 1174.66f, 1174.66f, 0.05f, 0.004f, 0.6f, 1.3f, 0.1f, 0 },
    };
    const note_t lock[] = { { 0, 2093, 2093, 0.08f, 0.002f, 0.05f, 0.18f, 0.1f, 0 },
                            { 0.04f, 1568, 1568, 0.08f, 0.002f, 0.07f, 0.2f, 0.1f, 0 } };
    const note_t unlock[] = { { 0, 1568, 1568, 0.08f, 0.002f, 0.06f, 0.2f, 0.1f, 0 },
                              { 0.07f, 2093, 2093, 0.08f, 0.002f, 0.08f, 0.22f, 0.1f, 0 } };
    render(SFX_CLICK, click, 1, 0.04f, 0.04f);
    render(SFX_TICK, tick, 1, 0.08f, 0);
    render(SFX_OPEN, open, 2, 0.18f, 0);
    render(SFX_CLOSE, close, 1, 0.16f, 0);
    render(SFX_NOTIFY, notify, 2, 0.75f, 0);
    render(SFX_ERROR, error, 2, 0.5f, 0);
    render(SFX_STARTUP, startup, countof(startup), 2.0f, 0);
    render(SFX_LOCK, lock, 2, 0.26f, 0);
    render(SFX_UNLOCK, unlock, 2, 0.3f, 0);
}

void sound_play(int id) {
    if (!g_set.sounds || id < 0 || id >= SFX_COUNT || !pcm[id]) return;
    plat_sound(pcm[id], frames[id]);
}
