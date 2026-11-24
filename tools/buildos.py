#!/usr/bin/env python3
"""Construction complète de TitanOs (appelé par build.py).

  1. compile le programme TitanOs (init + bureau) pour Linux, sans libc ;
  2. prend le noyau Linux d'Alpine et ne garde que les pilotes utiles ;
  3. assemble l'initramfs (cpio + zstd) : /init, BusyBox, pilotes, micrologiciels, /etc, et les
     quelques programmes Alpine nécessaires (wpa_supplicant pour le Wi-Fi, apk pour installer
     des applications, mke2fs pour préparer la clé) ;
  4. compile les chargeurs (MBR + stage2 pour le BIOS, BOOTX64.EFI pour l'UEFI) ;
  5. écrit l'image disque (FAT32 « TITANOS ») et sa version VHD.
"""
import gzip
import hashlib
import os
import struct
import sys
import time

KERNEL_PKG = "linux-lts"
DISK_MB = 256
CMDLINE = "quiet loglevel=3 vt.global_cursor_default=0 consoleblank=0 nowatchdog"

# Pilotes embarqués (leurs dépendances sont ajoutées automatiquement)
MODULES = [
    # carte graphique du VAIO (NVIDIA), QEMU ; ailleurs, l'écran du BIOS (simpledrm) suffit
    "nouveau", "bochs", "virtio_gpu",
    # clavier, souris, pavés tactiles, couvercle et bouton d'alimentation, touches Fn du VAIO
    "evdev", "psmouse", "hid_generic", "usbhid", "hid_multitouch", "i2c_hid_acpi", "hid_apple",
    "hid_logitech_dj", "hid_logitech_hidpp", "hid_microsoft", "elan_i2c", "button", "video", "sony_laptop",
    # processeur (fréquence variable, température), batterie et secteur
    "acpi_cpufreq", "coretemp", "battery", "ac",
    # son : Intel HD Audio et ses codecs (Realtek, Conexant, IDT, Analog, HDMI NVIDIA/Intel)
    "snd_hda_intel", "snd_hda_codec_generic", "snd_hda_codec_alc269", "snd_hda_codec_alc662",
    "snd_hda_codec_alc882", "snd_hda_codec_alc861", "snd_hda_codec_alc268", "snd_hda_codec_conexant",
    "snd_hda_codec_idt", "snd_hda_codec_analog", "snd_hda_codec_hdmi", "snd_hda_codec_nvhdmi",
    "snd_hda_codec_intelhdmi",
    # USB et stockage (la clé TITANOS), systèmes de fichiers
    "ehci_pci", "ohci_pci", "uhci_hcd", "xhci_pci", "usb_storage", "uas", "sd_mod", "ahci", "ata_piix",
    "virtio_blk", "virtio_pci", "vfat", "nls_cp437", "nls_utf8", "nls_iso8859_1", "ext4", "overlay",
    # réseau : Ethernet du VAIO (Marvell), Intel, Realtek, partage USB des téléphones, VM
    "sky2", "e1000", "e1000e", "r8169", "rndis_host", "cdc_ether", "cdc_ncm", "r8152", "ax88179_178a",
    "asix", "virtio_net", "af_packet",
    # Wi-Fi du VAIO : Intel Centrino 6200 (iwldvm) ou Atheros AR9287 (ath9k)
    "iwlwifi", "iwldvm", "ath9k",
]

# Programmes d'Alpine embarqués (avec leurs bibliothèques)
PACKAGES = ["wpa_supplicant", "e2fsprogs", "apk-tools-static", "alpine-keys", "ca-certificates-bundle",
            "wireless-regdb"]

# Micrologiciels téléchargés séparément (miroir officiel de linux-firmware), vérifiés par leur SHA-256
FIRMWARE = [
    ("iwlwifi-6000-4.ucode", "https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/intel/iwlwifi/iwlwifi-6000-4.ucode",
     "5babe59eda1022a45b2ae0460e13302eff0b3575b776e86fa68e50dfce62f94f"),
]

# optimisé pour le processeur du VAIO (Core i7 « Nehalem ») : SSE4.2, toujours compatible x86-64-v2
LINUX_ARCH = ["-march=x86-64-v2", "-mtune=nehalem"]


def norm(name):
    return name.replace("-", "_")


def modname(path):
    b = path.rsplit("/", 1)[-1]
    return norm(b.split(".ko")[0])


def select_modules(files, kver):
    """Choisit les pilotes et leurs dépendances ; renvoie {chemin relatif: contenu}, dep, alias."""
    base = f"lib/modules/{kver}/"
    dep_lines = files[base + "modules.dep"][1].decode().splitlines()
    deps = {}
    paths = {}
    for line in dep_lines:
        if ":" not in line:
            continue
        p, d = line.split(":", 1)
        n = modname(p)
        paths[n] = p
        deps[n] = d.split()
    want, missing = set(), []
    for m in MODULES:
        n = norm(m)
        if n not in paths:
            missing.append(m)
            continue
        want.add(n)
        for d in deps[n]:
            want.add(modname(d))
    if missing:
        print("  (absents de ce noyau ou intégrés :", ", ".join(missing) + ")")
    out = {}
    for n in sorted(want):
        p = paths[n]
        raw = files[base + p][1]
        if p.endswith(".gz"):
            raw = gzip.decompress(raw)
        elif p.endswith(".zst"):
            from compression import zstd
            raw = zstd.decompress(raw)
        out[p.rsplit(".ko", 1)[0] + ".ko"] = raw
    strip = lambda p: p.rsplit(".ko", 1)[0] + ".ko"
    dep_txt = "".join(f"{strip(paths[n])}: {' '.join(strip(d) for d in deps[n])}\n" for n in sorted(want))
    alias_txt = "".join(line + "\n" for line in files[base + "modules.alias"][1].decode().splitlines()
                        if line.startswith("alias ") and norm(line.split()[-1]) in want)
    return out, dep_txt, alias_txt


# ---------------------------------------------------------------- cpio (format « newc »)
class Cpio:
    def __init__(self):
        self.parts = []
        self.ino = 1000

    def _entry(self, name, mode, data=b"", rdev=(0, 0)):
        self.ino += 1
        nb = name.encode("utf-8") + b"\0"
        hdr = "070701" + "".join(f"{v:08X}" for v in (
            self.ino, mode, 0, 0, 1, int(time.time()), len(data), 0, 0, rdev[0], rdev[1], len(nb), 0))
        blob = hdr.encode("ascii") + nb
        blob += b"\0" * ((4 - len(blob) % 4) % 4)
        blob += data + b"\0" * ((4 - len(data) % 4) % 4)
        self.parts.append(blob)

    def dir(self, name):
        self._entry(name, 0o040755)

    def file(self, name, data, mode=0o644):
        self._entry(name, 0o100000 | mode, data)

    def link(self, name, target):
        self._entry(name, 0o120777, target.encode("utf-8"))

    def chardev(self, name, major, minor):
        self._entry(name, 0o020600, b"", (major, minor))

    def bytes(self):
        self._entry("TRAILER!!!", 0)
        return b"".join(self.parts)


class Tree:
    """Arborescence de l'initramfs : chaque chemin une seule fois, dossiers parents créés."""
    def __init__(self):
        self.e = {}

    def _parents(self, path):
        parts = path.split("/")
        for i in range(1, len(parts)):
            self.e.setdefault("/".join(parts[:i]), ("d", b"", 0o755))

    def dir(self, path, mode=0o755):
        self._parents(path)
        self.e.setdefault(path, ("d", b"", mode))

    def file(self, path, data, mode=0o644):
        self._parents(path)
        self.e[path] = ("f", data, mode)

    def link(self, path, target):
        self._parents(path)
        self.e[path] = ("l", target, 0o777)

    def chardev(self, path, major, minor):
        self._parents(path)
        self.e[path] = ("c", (major, minor), 0o600)

    def cpio(self):
        c = Cpio()
        for path in sorted(self.e):
            kind, data, mode = self.e[path]
            if kind == "d":
                c._entry(path, 0o040000 | mode)
            elif kind == "f":
                c.file(path, data, mode)
            elif kind == "l":
                c.link(path, data)
            else:
                c.chardev(path, *data)
        return c.bytes()


def fetch_firmware(cache):
    """Micrologiciels absents d'Alpine en petit format : téléchargés une fois et vérifiés."""
    import urllib.request
    out = {}
    for name, url, sha in FIRMWARE:
        path = os.path.join(cache, name)
        if not os.path.exists(path):
            print(f"  téléchargement {name}")
            req = urllib.request.Request(url, headers={"User-Agent": "titanos-build"})
            data = urllib.request.urlopen(req, timeout=120).read()
            with open(path, "wb") as f:
                f.write(data)
        data = open(path, "rb").read()
        if hashlib.sha256(data).hexdigest() != sha:
            os.remove(path)
            raise SystemExit(f"{name} : empreinte SHA-256 inattendue (fichier supprimé, relancez)")
        out[name] = data
    return out


def build(repo, B):
    clang, ld_lld, lld_link = B.find_tool("clang"), B.find_tool("ld.lld"), B.find_tool("lld-link")
    out = B.OUT
    root = B.ROOT
    gen = B.gen_sources(repo)

    # en-têtes du noyau (pour compiler)
    hdr = os.path.join(out, "hdr")
    if not os.path.exists(os.path.join(hdr, "linux", "input.h")):
        print("[en-têtes du noyau]")
        for name, (kind, data, mode) in repo.fetch("linux-headers").items():
            if kind == "f" and name.startswith("usr/include/"):
                p = os.path.join(hdr, name[len("usr/include/"):])
                os.makedirs(os.path.dirname(p), exist_ok=True)
                with open(p, "wb") as f:
                    f.write(data)

    print("[programme TitanOs : init + bureau, pour Linux]")
    flags = ["--target=x86_64-linux-gnu", "-fno-pic", "-fno-pie", "-nostdlibinc", *LINUX_ARCH,
             "-isystem", hdr, "-isystem", os.path.join(root, "src", "linux", "include"), *B.CFLAGS]
    srcs = ([os.path.join(root, "src", "core", s) for s in B.CORE] + gen +
            [os.path.join(root, "src", "linux", s) for s in B.LINUX])
    objs = B.compile_all(clang, srcs, os.path.join(out, "obj-linux"), flags)
    titan = os.path.join(out, "titanos-init")
    B.run([ld_lld, "-static", "-e", "_start", "--gc-sections", "-s", "-o", titan, *objs])
    elf = open(titan, "rb").read()
    if elf[:4] != b"\x7fELF" or b".interp" in elf[:4096]:
        raise SystemExit("le programme TitanOs n'est pas un exécutable Linux statique")

    print(f"[noyau Linux : {KERNEL_PKG} {repo.version(KERNEL_PKG)}]")
    kfiles = repo.fetch(KERNEL_PKG)
    kver = next(n.split("/")[2] for n in kfiles if n.startswith("lib/modules/") and n.endswith("/modules.dep"))
    vmlinuz = kfiles[next(n for n in kfiles if n.startswith("boot/vmlinuz"))][1]
    mods, dep_txt, alias_txt = select_modules(kfiles, kver)
    print(f"  noyau {kver} : {len(vmlinuz) >> 10} Ko, {len(mods)} pilotes ({sum(map(len, mods.values())) >> 10} Ko)")
    busybox = repo.fetch("busybox-static")["bin/busybox.static"][1]

    print("[programmes Alpine embarqués]")
    pkgs = repo.resolve(PACKAGES)
    print("  " + ", ".join(pkgs))
    firmware = fetch_firmware(B.CACHE)

    print("[initramfs]")
    t = Tree()
    for d in ("bin", "sbin", "usr/bin", "usr/sbin", "etc", "proc", "sys", "dev", "tmp", "run", "root", "media",
              "var/run", "var/cache/apk", "lib/firmware", f"lib/modules/{kver}"):
        t.dir(d)
    t.dir("tmp", 0o1777)
    for pk in pkgs:                                  # programmes Alpine (wpa_supplicant, apk, mke2fs...)
        for name, (kind, data, mode) in repo.fetch(pk).items():
            if name.startswith(".") or name.startswith(("usr/share/man/", "usr/share/doc/", "usr/share/info/")):
                continue
            if kind == "d":
                t.dir(name)
            elif kind == "l":
                t.link(name, data)
            else:
                t.file(name, data, mode & 0o7777)
    t.link("sbin/apk", "apk.static")
    for d in ("lib/apk/db", "etc/apk"):
        t.dir(d)
    for f in ("lib/apk/db/installed", "lib/apk/db/triggers", "lib/apk/db/lock", "etc/apk/world"):
        t.file(f, b"")
    t.chardev("dev/console", 5, 1)
    t.file("init", elf, 0o755)
    t.file("bin/busybox", busybox, 0o755)
    t.link("bin/sh", "busybox")
    t.link("sbin/modprobe", "../bin/busybox")
    t.link("lib/modules/current", kver)
    for name, data in firmware.items():
        t.file(f"lib/firmware/{name}", data)
    for dirpath, _, names in os.walk(os.path.join(root, "rootfs")):
        rel = os.path.relpath(dirpath, os.path.join(root, "rootfs")).replace(os.sep, "/")
        for n in sorted(names):
            data = open(os.path.join(dirpath, n), "rb").read().replace(b"\r\n", b"\n")
            t.file(n if rel == "." else f"{rel}/{n}", data, 0o755 if data.startswith(b"#!") else 0o644)
    for p, data in mods.items():
        t.file(f"lib/modules/{kver}/{p}", data)
    t.file(f"lib/modules/{kver}/modules.dep", dep_txt.encode())
    t.file(f"lib/modules/{kver}/modules.alias", alias_txt.encode())
    for extra in ("modules.builtin", "modules.builtin.modinfo", "modules.order"):
        if f"lib/modules/{kver}/{extra}" in kfiles:
            t.file(f"lib/modules/{kver}/{extra}", kfiles[f"lib/modules/{kver}/{extra}"][1])
    raw = t.cpio()
    from compression import zstd
    initrd = zstd.compress(raw, level=12)
    print(f"  {len(raw) >> 10} Ko -> {len(initrd) >> 10} Ko (zstd)")

    print("[chargeur UEFI]")
    efi_obj = os.path.join(out, "obj-boot", "efiboot.o")
    os.makedirs(os.path.dirname(efi_obj), exist_ok=True)
    B.run([clang, "--target=x86_64-unknown-windows", "-std=c11", "-O2", "-ffreestanding", "-fno-stack-protector",
           "-mno-red-zone", "-mno-stack-arg-probe", "-mgeneral-regs-only", "-fshort-wchar", "-Wall",
           "-c", os.path.join(root, "boot", "efiboot.c"), "-o", efi_obj])
    efi_path = os.path.join(out, "BOOTX64.EFI")
    B.run([lld_link, "/subsystem:efi_application", "/entry:efi_main", "/nodefaultlib", "/nologo",
           f"/out:{efi_path}", efi_obj])

    print("[chargeur BIOS]")
    boot = {}
    for name, addr, sym in (("mbr", 0x7C00, "start"), ("stage2", 0x8000, "s2")):
        o = os.path.join(out, "obj-boot", name + ".o")
        b = os.path.join(out, "obj-boot", name + ".bin")
        B.run([clang, "--target=x86_64-unknown-elf", "-c", os.path.join(root, "boot", name + ".S"), "-o", o])
        B.run([ld_lld, "--image-base=0", "--oformat=binary", f"-Ttext=0x{addr:x}", "-e", sym, "-o", b, o])
        boot[name] = open(b, "rb").read()

    print("[image disque]")
    import mkimage
    readme = ("TitanOs beta 1 - systeme d'exploitation sur base Linux\r\n\r\n"
              "TITANOS\\VMLINUZ     noyau Linux\r\n"
              "TITANOS\\INITRD.IMG  systeme TitanOs (bureau, pilotes, BusyBox)\r\n"
              "EFI\\BOOT\\BOOTX64.EFI chargeur UEFI (Secure Boot desactive)\r\n"
              "Utilisateur\\        vos fichiers (cree au premier demarrage)\r\n").encode("ascii")
    img = os.path.join(out, "titanos.img")
    import bootimg
    ttc = repo.fetch("font-inter")["usr/share/fonts/inter/Inter.ttc"][1]
    disk, s2, info = mkimage.write_images(DISK_MB, open(efi_path, "rb").read(), boot["mbr"], boot["stage2"],
                                          vmlinuz, initrd, CMDLINE, readme, bootimg.build(ttc), img,
                                          os.path.join(out, "titanos.vhd"))
    with open(os.path.join(out, "obj-boot", "stage2-final.bin"), "wb") as f:
        f.write(s2)
    print(f"  noyau au secteur {info['kernel_lba']}, initramfs au secteur {info['initrd_lba']}, "
          f"chargé à {info['initrd_addr'] >> 20} Mo ; {info['free_mb']} Mo libres pour vos fichiers")
    print(f"\nTitanOs bêta 1 est prêt :")
    print(f"  build/titanos.img   image pour clé USB ({DISK_MB} Mo, démarre en BIOS et en UEFI)")
    print(f"  build/titanos.vhd   disque virtuel (VirtualBox, Hyper-V génération 1)")
