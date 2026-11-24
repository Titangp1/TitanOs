/* TitanOs - son : lecture (ALSA, sans bibliothèque) et volume (mixeur de la carte)
 *
 * Les sons de l'interface sont joués par un petit processus à part, qui reçoit par un
 * tube « joue ce son » et mélange les sons qui se chevauchent : l'interface ne se bloque
 * jamais. Le volume est réglé directement sur le mixeur de la carte (Master), ce qui vaut
 * aussi pour les applications Linux installées. */
#include "linux.h"
#include <sound/asound.h>

#define RATE   48000
#define PERIOD 1024

/* ---------------- choix de la carte : la sortie analogique, pas le HDMI ---------------- */
static int find_pcm(int *card_out, int *dev_out) {
    /* on interroge directement chaque sortie (/dev/snd/pcmCxDyp) : pas besoin de /proc */
    for (int c = 0; c < 8; c++)
        for (int d = 0; d < 16; d++) {
            char p[48];
            snprintf(p, sizeof p, "/dev/snd/pcmC%dD%dp", c, d);
            int fd = sys_open(p, O_RDWR | O_NONBLOCK | O_CLOEXEC, 0);
            if (fd < 0) continue;
            struct snd_pcm_info info;
            memset(&info, 0, sizeof info);
            long r = sys_ioctl(fd, SNDRV_PCM_IOCTL_INFO, &info);
            sys_close(fd);
            const char *id = (const char *)info.id, *nm = (const char *)info.name;
            if (r < 0 || strstr(id, "HDMI") || strstr(nm, "HDMI") || strstr(id, "hdmi")) continue;
            static bool told;
            if (!told) { told = true; klog("son : carte %d, sortie %d (%s)", c, d, nm); }
            *card_out = c;
            *dev_out = d;
            return 0;
        }
    static bool warned;
    if (!warned) { warned = true; klog("son : aucune sortie analogique trouvée"); }
    return -1;
}

/* ---------------- paramètres matériels (comme tinyalsa) ---------------- */
static struct snd_mask *pmask(struct snd_pcm_hw_params *p, int n) { return &p->masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK]; }
static struct snd_interval *pint(struct snd_pcm_hw_params *p, int n) { return &p->intervals[n - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL]; }

static void hw_init(struct snd_pcm_hw_params *p) {
    memset(p, 0, sizeof *p);
    for (int n = SNDRV_PCM_HW_PARAM_FIRST_MASK; n <= SNDRV_PCM_HW_PARAM_LAST_MASK; n++) {
        struct snd_mask *m = pmask(p, n);
        m->bits[0] = ~0u;
        m->bits[1] = ~0u;
    }
    for (int n = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; n <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; n++) {
        struct snd_interval *i = pint(p, n);
        i->min = 0;
        i->max = ~0u;
    }
    p->rmask = ~0u;
    p->info = ~0u;
}
static void hw_mask(struct snd_pcm_hw_params *p, int n, unsigned bit) {
    struct snd_mask *m = pmask(p, n);
    memset(m->bits, 0, sizeof m->bits);
    m->bits[bit >> 5] |= 1u << (bit & 31);
}
static void hw_int(struct snd_pcm_hw_params *p, int n, unsigned v) {
    struct snd_interval *i = pint(p, n);
    i->min = i->max = v;
    i->integer = 1;
}
static void hw_min(struct snd_pcm_hw_params *p, int n, unsigned v) { pint(p, n)->min = v; }

static int pcm_open(void) {
    int card, dev;
    if (find_pcm(&card, &dev) < 0) return -1;
    char path[48];
    snprintf(path, sizeof path, "/dev/snd/pcmC%dD%dp", card, dev);
    int fd = sys_open(path, O_RDWR | O_CLOEXEC, 0);
    if (fd < 0) { klog("son : ouverture de %s impossible (%d)", path, fd); return -1; }
    struct snd_pcm_hw_params hw;
    hw_init(&hw);
    hw_mask(&hw, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    hw_mask(&hw, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
    hw_mask(&hw, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
    hw_min(&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, PERIOD);
    hw_int(&hw, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 16);
    hw_int(&hw, SNDRV_PCM_HW_PARAM_FRAME_BITS, 32);
    hw_int(&hw, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
    hw_int(&hw, SNDRV_PCM_HW_PARAM_PERIODS, 4);
    hw_int(&hw, SNDRV_PCM_HW_PARAM_RATE, RATE);
    long hr = sys_ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hw);
    if (hr < 0) { klog("son : réglage de %s impossible (%ld)", path, hr); sys_close(fd); return -1; }
    unsigned period = pint(&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE)->min;
    unsigned buffer = pint(&hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE)->min;
    struct snd_pcm_sw_params sw;
    memset(&sw, 0, sizeof sw);
    sw.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
    sw.period_step = 1;
    sw.avail_min = period;
    sw.start_threshold = period;
    sw.stop_threshold = buffer;
    sw.boundary = buffer;
    while (sw.boundary * 2 <= 0x7FFFFFFFul - buffer) sw.boundary *= 2;
    sys_ioctl(fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw);
    sys_ioctl(fd, SNDRV_PCM_IOCTL_PREPARE, NULL);
    klog("son : sortie %s (%u images par période)", path, period);
    return fd;
}

static void pcm_write(int fd, const int16_t *buf, int frames) {
    while (frames > 0) {
        struct snd_xferi x = { 0 };
        x.buf = (void *)buf;
        x.frames = (snd_pcm_uframes_t)frames;
        long r = sys_ioctl(fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x);
        if (r == -EPIPE) { sys_ioctl(fd, SNDRV_PCM_IOCTL_PREPARE, NULL); continue; }   /* sous-alimentation */
        if (r < 0) return;
        buf += x.result * 2;
        frames -= (int)x.result;
    }
}

/* ---------------- processus de lecture ---------------- */
typedef struct { const int16_t *pcm; int frames; } snd_cmd_t;
typedef struct { const int16_t *pcm; int frames, pos; } voice_t;
static int sound_pipe = -1;

static void audio_main(int rd) {
    for (int fd = 3; fd < 256; fd++) if (fd != rd) sys_close(fd);   /* rien d'autre que le tube */
    int pcm = -1;
    voice_t voices[8];
    int nv = 0;
    static int16_t mix[PERIOD * 2];
    for (;;) {
        /* attend un son (bloquant si rien ne joue) */
        struct pollfd p = { rd, POLLIN, 0 };
        if (sys_poll(&p, 1, nv ? 0 : -1) > 0) {
            snd_cmd_t c;
            long r = sys_read(rd, &c, sizeof c);
            if (r == 0) sys_exit(0);
            if (r == sizeof c && nv < 8) voices[nv++] = (voice_t){ c.pcm, c.frames, 0 };
            continue;                                   /* lit tous les sons en attente d'abord */
        }
        if (!nv) continue;
        if (pcm < 0) {
            pcm = pcm_open();
            if (pcm < 0) { nv = 0; sleep_ms(500); continue; }
        }
        memset(mix, 0, sizeof mix);
        for (int v = 0; v < nv; v++) {
            int n = MIN(PERIOD, voices[v].frames - voices[v].pos);
            const int16_t *s = voices[v].pcm + voices[v].pos * 2;
            for (int i = 0; i < n * 2; i++) {
                int x = mix[i] + s[i];
                mix[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
            }
            voices[v].pos += n;
        }
        int k = 0;
        for (int v = 0; v < nv; v++) if (voices[v].pos < voices[v].frames) voices[k++] = voices[v];
        nv = k;
        pcm_write(pcm, mix, PERIOD);
        if (!nv) {                                      /* fin : on laisse le son se terminer proprement */
            memset(mix, 0, sizeof mix);
            pcm_write(pcm, mix, PERIOD);
            sys_ioctl(pcm, SNDRV_PCM_IOCTL_DRAIN, NULL);
            sys_ioctl(pcm, SNDRV_PCM_IOCTL_PREPARE, NULL);
        }
    }
}

void audio_start(void) {
    int p[2];
    if (sys_pipe2(p, O_CLOEXEC) < 0) return;
    int pid = sys_fork();
    if (pid == 0) { sys_close(p[1]); audio_main(p[0]); sys_exit(0); }
    sys_close(p[0]);
    if (pid < 0) { sys_close(p[1]); return; }
    sc3(__NR_fcntl, p[1], F_SETFL, O_NONBLOCK);
    sound_pipe = p[1];
}

/* les données des sons ont été créées avant la séparation des processus :
 * leur adresse est la même des deux côtés, on transmet donc juste le pointeur */
void plat_sound(const int16_t *pcm, int frames) {
    if (sound_pipe < 0) return;
    snd_cmd_t c = { pcm, frames };
    sys_write(sound_pipe, &c, sizeof c);
}

/* ---------------- mixeur : volume général et coupure ---------------- */
typedef struct { struct snd_ctl_elem_id id; int min, max, count; bool ok; } ctl_t;
static int mixer_fd = -2;
static ctl_t master_vol, master_sw;
static int cur_vol = -1;
static bool cur_mute;

static bool find_ctl(int fd, const char *name, ctl_t *out) {
    struct snd_ctl_elem_list list;
    memset(&list, 0, sizeof list);
    if (sys_ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) < 0 || !list.count) return false;
    struct snd_ctl_elem_id *ids = calloc(list.count, sizeof *ids);
    if (!ids) return false;
    list.space = list.count;
    list.pids = ids;
    bool found = false;
    if (sys_ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) == 0)
        for (unsigned i = 0; i < list.used && !found; i++) {
            if (strcmp((const char *)ids[i].name, name) || ids[i].index) continue;
            struct snd_ctl_elem_info info;
            memset(&info, 0, sizeof info);
            info.id = ids[i];
            if (sys_ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0) continue;
            out->id = info.id;
            out->count = (int)info.count;
            out->min = info.type == SNDRV_CTL_ELEM_TYPE_INTEGER ? (int)info.value.integer.min : 0;
            out->max = info.type == SNDRV_CTL_ELEM_TYPE_INTEGER ? (int)info.value.integer.max : 1;
            out->ok = true;
            found = true;
        }
    free(ids);
    return found;
}

static void ctl_set(int fd, ctl_t *c, int v) {
    if (!c->ok) return;
    struct snd_ctl_elem_value val;
    memset(&val, 0, sizeof val);
    val.id = c->id;
    for (int i = 0; i < c->count && i < 128; i++) val.value.integer.value[i] = CLAMP(v, c->min, c->max);
    sys_ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &val);
}

static bool mixer_open(void) {
    if (mixer_fd >= 0) return true;
    static uint64_t retry;
    if (mixer_fd == -1 && plat_ms() < retry) return false;
    int card, dev;
    mixer_fd = -1;
    retry = plat_ms() + 3000;
    if (find_pcm(&card, &dev) < 0) return false;
    char p[48];
    snprintf(p, sizeof p, "/dev/snd/controlC%d", card);
    int fd = sys_open(p, O_RDWR | O_CLOEXEC, 0);
    if (fd < 0) { klog("son : mixeur %s introuvable (%d)", p, fd); return false; }
    /* tout est réglé au maximum sauf Master, qui sert de volume général */
    static const char *const unmute[] = { "Speaker Playback Switch", "Headphone Playback Switch", "PCM Playback Switch", "Front Playback Switch" };
    static const char *const full[] = { "Speaker Playback Volume", "Headphone Playback Volume", "PCM Playback Volume", "Front Playback Volume" };
    ctl_t c;
    for (int i = 0; i < 4; i++) { memset(&c, 0, sizeof c); if (find_ctl(fd, unmute[i], &c)) ctl_set(fd, &c, 1); }
    bool has_master = find_ctl(fd, "Master Playback Volume", &master_vol);
    find_ctl(fd, "Master Playback Switch", &master_sw);
    for (int i = 0; i < 4; i++) {
        memset(&c, 0, sizeof c);
        if (!find_ctl(fd, full[i], &c)) continue;
        if (!has_master && !master_vol.ok) { master_vol = c; continue; }   /* pas de Master : PCM fait office */
        ctl_set(fd, &c, c.max);
    }
    if (!master_vol.ok) { klog("son : pas de réglage de volume sur %s", p); sys_close(fd); return false; }
    klog("son : volume réglé par « %s »", (const char *)master_vol.id.name);
    mixer_fd = fd;
    return true;
}

int plat_volume(int set) {
    if (!mixer_open()) return -1;
    if (set >= 0) {
        cur_vol = CLAMP(set, 0, 100);
        /* courbe douce : le bas de l'échelle reste utilisable */
        int range = master_vol.max - master_vol.min;
        ctl_set(mixer_fd, &master_vol, master_vol.min + (range * cur_vol + 50) / 100);
        ctl_set(mixer_fd, &master_sw, cur_mute || cur_vol == 0 ? 0 : 1);
    }
    return cur_vol < 0 ? 70 : cur_vol;
}

bool plat_muted(int set) {
    if (set >= 0) {
        cur_mute = set != 0;
        if (mixer_open()) ctl_set(mixer_fd, &master_sw, cur_mute ? 0 : 1);
    }
    return cur_mute;
}
