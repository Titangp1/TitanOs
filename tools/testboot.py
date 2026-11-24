#!/usr/bin/env python3
"""Test du démarrage BIOS de TitanOs, sans machine.

Exécute le MBR et le chargeur de 2e étape dans un émulateur de processeur (Unicorn),
avec un faux BIOS (disque, écran VBE, carte mémoire E820), jusqu'à l'entrée du noyau
Linux. Vérifie ensuite que le noyau, l'initramfs et la « zero page » sont corrects.

    pip install unicorn
    python tools/testboot.py [build/titanos.img]
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
try:
    from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_INSN, UC_HOOK_CODE, UC_SECOND_SCALE
    from unicorn import x86_const as X
except ImportError:
    sys.exit("Ce test a besoin d'Unicorn : pip install unicorn")
import mkimage

IMG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "titanos.img")
disk = open(IMG, "rb").read()
mu = Uc(UC_ARCH_X86, UC_MODE_16)
mu.mem_map(0, 0x10000000)                       # 256 Mo de mémoire
mu.mem_map(0xE0000000, 16 << 20)                # mémoire vidéo du mode VBE simulé
mu.mem_write(0x7C00, disk[:512])                # le BIOS charge le MBR
mu.reg_write(X.UC_X86_REG_EDX, 0x80)            # disque de démarrage
mu.reg_write(X.UC_X86_REG_ESP, 0x7000)
text, unknown, vbe_set = [], [], [None]
# écran simulé : modes VBE proposés et résolution native (EDID).
# TESTBOOT_VAIO=1 imite la carte NVIDIA du VAIO : pas de mode 1920x1080, mais un 1366x768.
if os.environ.get("TESTBOOT_VAIO") == "2":       # le vrai VAIO : rien en 16:9, 1280x1024 étiré par l'écran
    MODES = {0x118: (1024, 768), 0x11B: (1280, 1024)}
    EXPECT = (1280, 1024)
elif os.environ.get("TESTBOOT_VAIO"):
    MODES = {0x118: (1024, 768), 0x11B: (1280, 1024), 0x160: (1366, 768), 0x161: (1600, 1200)}
    EXPECT = (1366, 768)
else:
    MODES = {0x118: (1024, 768), 0x144: (1600, 900), 0x145: (1920, 1080), 0x146: (2560, 1440)}
    EXPECT = (1920, 1080)
NATIVE = (1920, 1080)
E820 = [(0, 0x9FC00, 1), (0x9FC00, 0x400, 2), (0xF0000, 0x10000, 2), (0x100000, 0x7FE00000, 1), (0xFEC00000, 0x1400000, 2)]


def reg(r):
    return mu.reg_read(getattr(X, "UC_X86_REG_" + r))


def setreg(r, v):
    mu.reg_write(getattr(X, "UC_X86_REG_" + r), v)


def carry(on):
    f = reg("EFLAGS")
    setreg("EFLAGS", (f | 1) if on else (f & ~1))


def bios(uc, intno, _):
    ax = reg("AX")
    ah = ax >> 8
    es_di = (reg("ES") << 4) + reg("DI")
    if intno == 0x10:
        if ah == 0x0E:
            text.append(chr(ax & 0xFF))
        elif ax == 0x4F00:                           # informations VBE
            blk = bytearray(512)
            blk[0:4] = b"VESA"
            struct.pack_into("<HxxxxxxxHHH", blk, 4, 0x0300, 0x6000, 0, 256)
            uc.mem_write(es_di, bytes(blk))
            uc.mem_write(0x6000, struct.pack(f"<{len(MODES) + 1}H", *MODES, 0xFFFF))
            setreg("AX", 0x4F)
        elif ax == 0x4F01:                           # informations sur un mode
            w, h = MODES.get(reg("CX"), (0, 0))
            mi = bytearray(256)
            struct.pack_into("<H", mi, 0, 0x9B)
            struct.pack_into("<HHH", mi, 16, w * 4, w, h)
            mi[25], mi[27] = 32, 6
            mi[31:39] = bytes([8, 16, 8, 8, 8, 0, 8, 24])
            struct.pack_into("<I", mi, 40, 0xE0000000)
            struct.pack_into("<H", mi, 50, w * 4)
            uc.mem_write(es_di, bytes(mi))
            setreg("AX", 0x4F)
        elif ax == 0x4F15 and (reg("BX") & 0xFF) == 1:  # EDID de l'écran
            edid = bytearray(128)
            edid[0:8] = bytes([0, 255, 255, 255, 255, 255, 255, 0])
            w, h = NATIVE
            edid[56], edid[58] = w & 0xFF, (w >> 8) << 4
            edid[59], edid[61] = h & 0xFF, (h >> 8) << 4
            uc.mem_write(es_di, bytes(edid))
            setreg("AX", 0x4F)
        elif ax == 0x4F02:
            vbe_set[0] = reg("BX")
            setreg("AX", 0x4F)
        else:
            unknown.append(f"int 10h ax={ax:04x}")
    elif intno == 0x13:
        if ah == 0x41:
            setreg("BX", 0xAA55); setreg("CX", 1); carry(False)
        elif ah == 0x42:
            _, _, cnt, off, seg, lba = struct.unpack("<BBHHHQ", bytes(uc.mem_read((reg("DS") << 4) + reg("SI"), 16)))
            uc.mem_write((seg << 4) + off, disk[lba * 512:(lba + cnt) * 512].ljust(cnt * 512, b"\0"))
            setreg("AX", 0); carry(False)
        elif ah == 0:
            carry(False)
        else:
            unknown.append(f"int 13h ah={ah:02x}"); carry(True)
    elif intno == 0x15:
        if ax == 0x2401:
            setreg("AX", 0); carry(False)
        elif reg("EAX") == 0xE820:
            i = reg("EBX")
            b, l, t = E820[i]
            uc.mem_write(es_di, struct.pack("<QQII", b, l, t, 1))
            setreg("EAX", 0x534D4150); setreg("ECX", 24); setreg("EBX", i + 1 if i + 1 < len(E820) else 0)
            carry(False)
        else:
            unknown.append(f"int 15h ax={ax:04x}"); carry(True)
    else:
        unknown.append(f"int {intno:#x}")


ports = {}
mu.hook_add(UC_HOOK_INTR, bios)
mu.hook_add(UC_HOOK_INSN, lambda uc, port, size, _: ports.get(port, 0), None, 1, 0, X.UC_X86_INS_IN)
mu.hook_add(UC_HOOK_INSN, lambda uc, port, size, v, _: ports.__setitem__(port, v), None, 1, 0, X.UC_X86_INS_OUT)
reached = [False]


def at_kernel(uc, addr, size, _):
    reached[0] = True
    uc.emu_stop()


mu.hook_add(UC_HOOK_CODE, at_kernel, None, 0x100000, 0x100000)
try:
    mu.emu_start(0x7C00, 0x100001, timeout=120 * UC_SECOND_SCALE, count=300_000_000)
except UcError as e:
    print("erreur d'émulation :", e, "à", hex(reg("EIP")))

print("--- écran du chargeur ---\n" + "".join(text))
if unknown:
    print("appels BIOS non simulés :", unknown[:10])
if not reached[0]:
    print("ÉCHEC : le noyau n'a pas été atteint (EIP =", hex(reg("EIP")), ")")
    sys.exit(1)

zp = bytes(mu.mem_read(reg("ESI"), 4096))
ksz, isz, _ = map(int, mkimage.read_back(disk, "TITANOS/TAILLES").split(b"\0")[0].split())   # tailles utiles
kernel = mkimage.read_back(disk, "TITANOS/VMLINUZ")[:ksz]
initrd = mkimage.read_back(disk, "TITANOS/INITRD.IMG")[:isz]
pm = kernel[((kernel[0x1F1] or 4) + 1) * 512:]
rd_addr, rd_size = struct.unpack_from("<II", zp, 0x218)
cmd = bytes(mu.mem_read(struct.unpack_from("<I", zp, 0x228)[0], 512)).split(b"\0")[0].decode()
checks = {
    "entrée en mode protégé 32 bits (CS=0x10, DS=0x18)": reg("CS") == 0x10 and reg("DS") == 0x18 and reg("CR0") & 1,
    "interruptions coupées, EBP=EDI=EBX=0": not reg("EFLAGS") & 0x200 and reg("EBP") == reg("EDI") == reg("EBX") == 0,
    "noyau copié à 1 Mo": bytes(mu.mem_read(0x100000, len(pm))) == pm,
    f"initramfs copié à {rd_addr >> 20} Mo": rd_size == len(initrd) and bytes(mu.mem_read(rd_addr, rd_size)) == initrd,
    "en-tête du noyau (HdrS)": zp[0x202:0x206] == b"HdrS" and zp[0x210] == 0xFF,
    f"carte mémoire E820 ({zp[0x1E8]} zones)": zp[0x1E8] == len(E820),
    f"écran VBE décrit ({EXPECT[0]}x{EXPECT[1]}x32)": zp[0x0F] == 0x23 and struct.unpack_from("<HHH", zp, 0x12) == (*EXPECT, 32),
    f"ligne de commande : {cmd}": "quiet" in cmd and f"titanos.disk={struct.unpack_from('<I', disk, 440)[0]:08x}" in cmd,
}
# ce que le chargeur a dessiné à l'écran : le logo au centre, le fond en dégradé
FBW, FBH = EXPECT


def pixel(x, y):
    return struct.unpack_from("<I", bytes(mu.mem_read(0xE0000000 + (y * FBW + x) * 4, 4)))[0] & 0xFFFFFF


def lum(c):
    return (c >> 16 & 255) + (c >> 8 & 255) + (c & 255)


checks["écran de démarrage dessiné (logo au centre)"] = lum(pixel(FBW // 2 + 20, FBH // 2 - 50)) > 3 * lum(pixel(40, FBH // 2 - 50))
checks["fond en dégradé (haut plus sombre que le bas)"] = lum(pixel(10, 10)) < lum(pixel(10, FBH - 10))
if len(sys.argv) > 2:              # capture de l'écran dessiné par le chargeur
    import zlib
    raw = bytes(mu.mem_read(0xE0000000, FBW * FBH * 4))
    rgb = bytearray(FBW * FBH * 3)
    rgb[0::3], rgb[1::3], rgb[2::3] = raw[2::4], raw[1::4], raw[0::4]
    rows = b"".join(b"\x00" + bytes(rgb[y * FBW * 3:(y + 1) * FBW * 3]) for y in range(FBH))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    with open(sys.argv[2], "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", FBW, FBH, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))
for name, ok in checks.items():
    print(("  OK    " if ok else "  ÉCHEC ") + name)
ok = all(checks.values())
print("\nRÉSULTAT :", "le chargeur BIOS démarre Linux correctement" if ok else "ÉCHEC")
sys.exit(0 if ok else 1)
