/* TitanOs - chargeur UEFI (EFI/BOOT/BOOTX64.EFI)
 *
 * 1. affiche l'écran de démarrage de TitanOs (images de \TITANOS\BOOT.IMG) sur l'écran du firmware ;
 * 2. lit \TITANOS\VMLINUZ (le noyau Linux) et \TITANOS\INITRD.IMG sur la clé ;
 * 3. publie l'initramfs par le protocole LoadFile2 (GUID LINUX_EFI_INITRD_MEDIA) :
 *    c'est là que le noyau va le chercher lui-même ;
 * 4. lance le noyau (son « EFI stub ») avec la ligne de commande. */
#include "efi.h"

static EFI_SYSTEM_TABLE *ST;
static EFI_BOOT_SERVICES *BS;
static void *initrd;
static UINTN initrd_size;

static const CHAR16 CMDLINE[] = u"TCMD"   /* remplacé par tools/mkimage.py (256 caractères au plus) */
    u"                                                                                                    "
    u"                                                                                                    "
    u"                                                    ";

static void *memcpy_(void *d, const void *s, UINTN n) {
    UINT8 *a = d;
    const UINT8 *b = s;
    while (n--) *a++ = *b++;
    return d;
}
void *memcpy(void *d, const void *s, UINTN n) { return memcpy_(d, s, n); }
void *memset(void *d, int c, UINTN n) { UINT8 *a = d; while (n--) *a++ = (UINT8)c; return d; }
static int memcmp_(const void *a, const void *b, UINTN n) {
    const UINT8 *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}

static void say(const CHAR16 *s) { ST->ConOut->OutputString(ST->ConOut, (CHAR16 *)s); }

static void fail(const CHAR16 *s) {
    say(u"\r\nTitanOs : ");
    say(s);
    say(u"\r\n");
    BS->Stall(8000000);
}

/* ---- écran de démarrage : le même que celui de TitanOs (images de \TITANOS\BOOT.IMG) ---- */
static UINT32 *fb;
static int W, H, P, rgb_order, frame = -1;
static const UINT8 *bi;

static UINT32 rd32(const UINT8 *p) { return (UINT32)p[0] | (UINT32)p[1] << 8 | (UINT32)p[2] << 16 | (UINT32)p[3] << 24; }
static int rd16(const UINT8 *p) { return p[0] | p[1] << 8; }
static int rd16s(const UINT8 *p) { return (short)(p[0] | p[1] << 8); }

static UINT32 grad(int y) {            /* même dégradé que l'écran de démarrage (wm.c) */
    UINT32 top = rd32(bi + 32), bot = rd32(bi + 36), c = 0;
    int t = H > 1 ? y * 256 / (H - 1) : 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        int a = (int)(top >> sh & 255), b = (int)(bot >> sh & 255);
        c |= (UINT32)(a + (((b - a) * t) >> 8)) << sh;
    }
    return c;
}

static void put(int x, int y, UINT32 c) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    fb[(UINTN)y * P + x] = rgb_order ? ((c & 0xFF) << 16 | (c & 0xFF00) | (c >> 16 & 0xFF)) : c;
}

static UINT32 over_a(UINT32 bg, UINT32 fg, int a) {
    UINT32 rb = ((fg & 0xFF00FF) * a + (bg & 0xFF00FF) * (256 - a)) >> 8 & 0xFF00FF;
    UINT32 g = ((fg & 0xFF00) * a + (bg & 0xFF00) * (256 - a)) >> 8 & 0xFF00;
    return rb | g;
}

static void blit_a(int x0, int y0, int w, int h, const UINT8 *src, UINT32 col) {
    for (int j = 0; j < h; j++) {
        UINT32 bg = grad(y0 + j);
        for (int i = 0; i < w; i++) if (src[j * w + i]) put(x0 + i, y0 + j, over_a(bg, col, src[j * w + i]));
    }
}

static void spinner(void) {
    if (!bi) return;
    int nf = rd16(bi + 16), aw = rd16(bi + 12), ah = rd16(bi + 14);
    int x0 = W / 2 - aw / 2, y0 = H / 2 + rd16s(bi + 28);
    frame = (frame + 1) % nf;
    for (int y = y0; y < y0 + ah; y++) {          /* efface la zone des crêtes */
        UINT32 bg = grad(y);
        for (int x = x0; x < x0 + aw; x++) put(x, y, bg);
    }
    blit_a(x0, y0, aw, ah, bi + rd32(bi + 48) + (UINTN)frame * aw * ah, rd32(bi + 52));
}

static void draw_splash(void) {
    EFI_GUID g = GOP_GUID;
    EFI_GOP *gop;
    if (!bi || EFI_ERROR(BS->LocateProtocol(&g, 0, (void **)&gop)) || !gop->Mode || !gop->Mode->Info) return;
    EFI_GOP_MODE_INFO *mi = gop->Mode->Info;
    if (mi->PixelFormat > 1) { bi = 0; return; }
    fb = (UINT32 *)gop->Mode->FrameBufferBase;
    W = (int)mi->HorizontalResolution;
    H = (int)mi->VerticalResolution;
    P = (int)mi->PixelsPerScanLine;
    rgb_order = mi->PixelFormat == 0;
    for (int y = 0; y < H; y++) {
        UINT32 bg = grad(y);
        for (int x = 0; x < W; x++) put(x, y, bg);
    }
    int lw = rd16(bi + 4), lh = rd16(bi + 6), lx = W / 2 - lw / 2, ly = H / 2 + rd16s(bi + 20);
    const UINT8 *logo = bi + rd32(bi + 40);
    for (int j = 0; j < lh; j++) {
        UINT32 bg = grad(ly + j);
        for (int i = 0; i < lw; i++) {
            const UINT8 *p = logo + ((UINTN)j * lw + i) * 4;
            if (!p[3]) continue;                   /* transparent : le fond reste */
            UINT32 ia = 255 - p[3];
            UINT32 c = ((((bg & 0xFF00FF) * ia) >> 8) & 0xFF00FF) | ((((bg & 0xFF00) * ia) >> 8) & 0xFF00);
            put(lx + i, ly + j, c + ((UINT32)p[2] << 16 | (UINT32)p[1] << 8 | p[0]));
        }
    }
    blit_a((W - rd16(bi + 26)) / 2 + rd16s(bi + 24), H / 2 + rd16s(bi + 22), rd16(bi + 8), rd16(bi + 10),
           bi + rd32(bi + 44), 0xF8FAFC);
    spinner();
}

/* ---- lecture d'un fichier sur la partition de démarrage (les crêtes s'animent pendant ce temps) ---- */
/* les fichiers du système ont une taille fixe (complétés de zéros, pour les mises à jour) :
 * \TITANOS\TAILLES donne la taille utile de chacun, on ne lit que celle-ci */
static EFI_STATUS read_file(EFI_FILE *root, CHAR16 *path, void **buf, UINTN *size, UINT64 limit) {
    EFI_FILE *f;
    EFI_STATUS s = root->Open(root, &f, path, 1 /* lecture */, 0);
    if (EFI_ERROR(s)) return s;
    UINT64 end = 0;
    f->SetPosition(f, 0xFFFFFFFFFFFFFFFFull);
    f->GetPosition(f, &end);
    f->SetPosition(f, 0);
    if (limit && limit < end) end = limit;
    s = BS->AllocatePool(EfiLoaderData, (UINTN)end + 16, buf);
    if (EFI_ERROR(s)) { f->Close(f); return s; }
    UINTN done = 0;
    while (done < (UINTN)end) {
        UINTN n = (UINTN)end - done;
        if (n > (1u << 20)) n = 1u << 20;
        s = f->Read(f, &n, (UINT8 *)*buf + done);
        if (EFI_ERROR(s) || n == 0) break;
        done += n;
        spinner();
    }
    f->Close(f);
    *size = done;
    return done == (UINTN)end ? EFI_SUCCESS : EFI_NOT_FOUND;
}

/* ---- initramfs fourni au noyau par LoadFile2 ---- */
static EFI_STATUS lf2_load(EFI_LOAD_FILE2 *self, EFI_DEVICE_PATH *path, BOOLEAN boot_policy, UINTN *size, void *buf) {
    (void)self; (void)path;
    if (boot_policy) return EFI_UNSUPPORTED;
    if (!buf || *size < initrd_size) { *size = initrd_size; return EFI_BUFFER_TOO_SMALL; }
    memcpy_(buf, initrd, initrd_size);
    *size = initrd_size;
    return EFI_SUCCESS;
}
static EFI_LOAD_FILE2 lf2 = { lf2_load };

static struct __attribute__((packed)) {
    EFI_DEVICE_PATH vendor;
    EFI_GUID guid;
    EFI_DEVICE_PATH end;
} initrd_path = {
    { 4, 3, { 20, 0 } }, LINUX_INITRD_GUID, { 0x7F, 0xFF, { 4, 0 } },
};

/* chemin complet du noyau (pour LoadImage) : chemin de la partition + nœud « fichier » */
static EFI_DEVICE_PATH *file_path(EFI_HANDLE dev, const CHAR16 *name) {
    EFI_GUID g = DEVICE_PATH_GUID;
    EFI_DEVICE_PATH *dp;
    if (EFI_ERROR(BS->HandleProtocol(dev, &g, (void **)&dp))) return 0;
    UINTN len = 0;
    EFI_DEVICE_PATH *n = dp;
    while (!(n->Type == 0x7F && n->SubType == 0xFF)) {
        UINTN l = n->Length[0] | (UINTN)n->Length[1] << 8;
        len += l;
        n = (EFI_DEVICE_PATH *)((UINT8 *)n + l);
    }
    UINTN nl = 0;
    while (name[nl]) nl++;
    UINTN fl = 4 + (nl + 1) * 2;
    UINT8 *out;
    if (EFI_ERROR(BS->AllocatePool(EfiLoaderData, len + fl + 4, (void **)&out))) return 0;
    memcpy_(out, dp, len);
    EFI_DEVICE_PATH *f = (EFI_DEVICE_PATH *)(out + len);
    f->Type = 4;              /* média */
    f->SubType = 4;           /* chemin de fichier */
    f->Length[0] = (UINT8)fl;
    f->Length[1] = (UINT8)(fl >> 8);
    memcpy_(out + len + 4, name, (nl + 1) * 2);
    EFI_DEVICE_PATH *e = (EFI_DEVICE_PATH *)(out + len + fl);
    e->Type = 0x7F; e->SubType = 0xFF; e->Length[0] = 4; e->Length[1] = 0;
    return (EFI_DEVICE_PATH *)out;
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    ST = st;
    BS = st->BootServices;
    BS->SetWatchdogTimer(0, 0, 0, 0);

    EFI_GUID lig = LOADED_IMAGE_GUID, fsg = SIMPLE_FS_GUID;
    EFI_LOADED_IMAGE *li;
    EFI_SIMPLE_FS *fs;
    EFI_FILE *root;
    if (EFI_ERROR(BS->HandleProtocol(image, &lig, (void **)&li)) ||
        EFI_ERROR(BS->HandleProtocol(li->DeviceHandle, &fsg, (void **)&fs)) ||
        EFI_ERROR(fs->OpenVolume(fs, &root))) {
        fail(u"impossible de lire la cle USB.");
        return EFI_NOT_FOUND;
    }
    /* tailles utiles : « noyau initramfs images », en décimal */
    UINT64 sz[3] = { 0, 0, 0 };
    void *tl;
    UINTN tls;
    if (!EFI_ERROR(read_file(root, u"\\TITANOS\\TAILLES", &tl, &tls, 512))) {
        const char *t = tl;
        for (int k = 0, i = 0; k < 3 && i < (int)tls; k++) {
            while (i < (int)tls && (t[i] < '0' || t[i] > '9')) i++;
            while (i < (int)tls && t[i] >= '0' && t[i] <= '9') sz[k] = sz[k] * 10 + (UINT64)(t[i++] - '0');
        }
    }
    void *img;
    UINTN isz;
    if (!EFI_ERROR(read_file(root, u"\\TITANOS\\BOOT.IMG", &img, &isz, sz[2])) && isz > 56 && !memcmp_(img, "TBIM", 4)) {
        bi = img;
        draw_splash();
    }
    void *kernel;
    UINTN ksize;
    if (EFI_ERROR(read_file(root, u"\\TITANOS\\VMLINUZ", &kernel, &ksize, sz[0]))) { fail(u"noyau \\TITANOS\\VMLINUZ introuvable."); return EFI_NOT_FOUND; }
    if (EFI_ERROR(read_file(root, u"\\TITANOS\\INITRD.IMG", &initrd, &initrd_size, sz[1]))) { fail(u"\\TITANOS\\INITRD.IMG introuvable."); return EFI_NOT_FOUND; }

    EFI_HANDLE h = 0;
    EFI_GUID dpg = DEVICE_PATH_GUID, lfg = LOAD_FILE2_GUID;
    BS->InstallMultipleProtocolInterfaces(&h, &dpg, &initrd_path, &lfg, &lf2, (void *)0);

    EFI_HANDLE k;
    EFI_STATUS s = BS->LoadImage(0, image, file_path(li->DeviceHandle, u"\\TITANOS\\VMLINUZ"), kernel, ksize, &k);
    if (EFI_ERROR(s)) { fail(u"le noyau Linux n'a pas pu etre charge (Secure Boot actif ?)."); return s; }
    EFI_LOADED_IMAGE *kli;
    BS->HandleProtocol(k, &lig, (void **)&kli);
    kli->LoadOptions = (void *)CMDLINE;
    UINTN n = 0;
    while (CMDLINE[n]) n++;
    kli->LoadOptionsSize = (UINT32)((n + 1) * 2);
    s = BS->StartImage(k, 0, 0);
    fail(u"le noyau Linux s'est arrete.");
    return s;
}
