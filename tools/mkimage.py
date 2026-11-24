#!/usr/bin/env python3
"""Crée l'image disque amorçable de TitanOs, sans outil externe.

Disque hybride, démarrable en UEFI *et* en BIOS classique :

  LBA 0         MBR : code de démarrage BIOS (boot/mbr.S) + table de partitions
  LBA 1-64      chargeur BIOS de 2e étape (boot/stage2.S), complété ici :
                emplacement du noyau et de l'initramfs, « zero page » Linux, ligne de commande
  LBA 2048-fin  partition FAT32 active, nommée TITANOS :
                  EFI/BOOT/BOOTX64.EFI   chargeur UEFI (boot/efiboot.c)
                  TITANOS/VMLINUZ        noyau Linux
                  TITANOS/INITRD.IMG     système TitanOs (initramfs)
                  TITANOS/BOOT.IMG       images de l'écran de démarrage (logo, crêtes animées)
                  Utilisateur/           vos fichiers (créé au premier démarrage)

Les fichiers sont écrits d'un seul tenant : le chargeur BIOS les lit directement par
leurs numéros de secteurs.
"""
import struct
import time
import uuid

SECTOR = 512
PART_START = 2048
STAGE2_LBA = 1
STAGE2_MAX = 64
SPC = 4                      # secteurs par cluster (2 Ko)


def fat_datetime():
    t = time.localtime()
    return ((t.tm_year - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday, (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)


def short_name(name):
    base, _, ext = name.upper().partition(".")
    if not (1 <= len(base) <= 8 and len(ext) <= 3):
        raise ValueError(f"nom 8.3 invalide : {name}")
    return base.ljust(8).encode("ascii") + ext.ljust(3).encode("ascii")


def dir_entry(name11, attr, cluster, size):
    date, tim = fat_datetime()
    return struct.pack("<11sBBBHHHHHHHI", name11, attr, 0, 0, tim, date, date, cluster >> 16, tim, date,
                       cluster & 0xFFFF, size)


class Fat32:
    def __init__(self, nsec, hidden):
        self.nsec, self.hidden = nsec, hidden
        self.rsvd, self.nfats, self.spc = 32, 2, SPC
        fatsz = 1
        while True:
            clusters = (nsec - self.rsvd - self.nfats * fatsz) // self.spc
            need = ((clusters + 2) * 4 + SECTOR - 1) // SECTOR
            if need <= fatsz:
                break
            fatsz = need
        self.fatsz = fatsz
        self.clusters = (nsec - self.rsvd - self.nfats * fatsz) // self.spc
        if self.clusters < 65525:
            raise ValueError("volume trop petit pour du FAT32")
        self.data_start = self.rsvd + self.nfats * fatsz
        self.fat = [0] * (self.clusters + 2)
        self.fat[0], self.fat[1] = 0x0FFFFFF8, 0x0FFFFFFF
        self.next = 2
        self.img = bytearray(nsec * SECTOR)
        self.placed = {}          # chemin -> (premier cluster, taille)

    def csize(self):
        return self.spc * SECTOR

    def lba_of(self, cluster):
        """Numéro de secteur absolu (sur le disque) d'un cluster."""
        return self.hidden + self.data_start + (cluster - 2) * self.spc

    def alloc(self, nbytes):
        n = max(1, (nbytes + self.csize() - 1) // self.csize())
        first = self.next
        if first + n > self.clusters + 2:
            raise ValueError("volume plein")
        for i in range(n):
            c = first + i
            self.fat[c] = c + 1 if i < n - 1 else 0x0FFFFFFF
        self.next += n
        return first

    def write(self, cluster, data):
        off = (self.data_start + (cluster - 2) * self.spc) * SECTOR
        self.img[off:off + len(data)] = data

    def add_dir(self, tree, cluster, parent, prefix, is_root=False):
        entries = []
        if is_root:
            entries.append(dir_entry(b"TITANOS    ", 0x08, 0, 0))
        else:
            entries.append(dir_entry(b".          ", 0x10, cluster, 0))
            entries.append(dir_entry(b"..         ", 0x10, parent, 0))
        for name, content in tree.items():
            if isinstance(content, dict):
                c = self.alloc((len(content) + 3) * 32)
                self.add_dir(content, c, 0 if is_root else cluster, prefix + name + "/")
                entries.append(dir_entry(short_name(name), 0x10, c, 0))
            else:
                c = self.alloc(len(content)) if content else 0
                if content:
                    self.write(c, content)
                self.placed[prefix + name] = (c, len(content))
                entries.append(dir_entry(short_name(name), 0x20, c, len(content)))
        self.write(cluster, b"".join(entries))

    def build(self, tree):
        root = self.alloc((len(tree) + 1) * 32)
        assert root == 2
        self.add_dir(tree, root, 0, "", is_root=True)
        bs = bytearray(SECTOR)
        bs[0:3] = b"\xEB\x58\x90"
        bs[3:11] = b"TITANOS "
        struct.pack_into("<HBHBHHBHHHII", bs, 11, SECTOR, self.spc, self.rsvd, self.nfats,
                         0, 0, 0xF8, 0, 63, 255, self.hidden, self.nsec)
        struct.pack_into("<IHHIHH", bs, 36, self.fatsz, 0, 0, 2, 1, 6)
        struct.pack_into("<BBBI11s8s", bs, 64, 0x80, 0, 0x29, 0x7174A905, b"TITANOS    ", b"FAT32   ")
        bs[510:512] = b"\x55\xAA"
        fsinfo = bytearray(SECTOR)
        struct.pack_into("<I", fsinfo, 0, 0x41615252)
        struct.pack_into("<III", fsinfo, 484, 0x61417272, self.clusters + 2 - self.next, self.next)
        struct.pack_into("<I", fsinfo, 508, 0xAA550000)
        for base in (0, 6):
            self.img[base * SECTOR:(base + 1) * SECTOR] = bs
            self.img[(base + 1) * SECTOR:(base + 2) * SECTOR] = fsinfo
            self.img[(base + 2) * SECTOR + 510:(base + 2) * SECTOR + 512] = b"\x55\xAA"
        fat_bytes = struct.pack(f"<{len(self.fat)}I", *self.fat)
        for i in range(self.nfats):
            off = (self.rsvd + i * self.fatsz) * SECTOR
            self.img[off:off + len(fat_bytes)] = fat_bytes
        return self.img


def chs(lba):
    c, rem = divmod(lba, 255 * 63)
    h, s = divmod(rem, 63)
    if c > 1023:
        return b"\xFE\xFF\xFF"
    return bytes([h, (s + 1) | ((c >> 2) & 0xC0), c & 0xFF])


def zero_page(kernel, initrd_addr, initrd_size, cmdline_addr):
    """« Zero page » Linux préparée d'avance : en-tête du noyau + nos réglages.
    Le chargeur BIOS n'y ajoute que la carte mémoire (E820) et l'écran (VBE)."""
    if kernel[0x202:0x206] != b"HdrS":
        raise SystemExit("le noyau n'est pas un bzImage Linux")
    version = struct.unpack_from("<H", kernel, 0x206)[0]
    if version < 0x020C:
        raise SystemExit(f"protocole de démarrage Linux trop ancien ({version:#x})")
    zp = bytearray(4096)
    end = 0x202 + kernel[0x201]
    zp[0x1F1:end] = kernel[0x1F1:end]
    zp[0x210] = 0xFF                               # type_of_loader : chargeur non répertorié
    zp[0x211] |= 0x80 | 0x20                       # CAN_USE_HEAP, QUIET_FLAG
    struct.pack_into("<H", zp, 0x1FA, 0xFFFF)      # vid_mode : laissé tel quel
    struct.pack_into("<II", zp, 0x218, initrd_addr, initrd_size)
    struct.pack_into("<I", zp, 0x228, cmdline_addr)
    return zp


def build_disk(disk_mb, efi, mbr_code, stage2, kernel, initrd, cmdline, readme, bootimg):
    total = disk_mb * 1024 * 1024 // SECTOR
    part_sectors = total - PART_START
    if len(mbr_code) > 440:
        raise ValueError(f"code MBR trop grand ({len(mbr_code)} > 440 octets)")
    if len(stage2) > STAGE2_MAX * SECTOR:
        raise ValueError("chargeur de 2e étape trop grand")

    # ligne de commande du noyau, inscrite dans les deux chargeurs
    efi = bytearray(efi)
    m = efi.find("TCMD".encode("utf-16-le"))
    if m < 0:
        raise ValueError("emplacement de la ligne de commande introuvable dans BOOTX64.EFI")
    ucmd = (cmdline + " titanos.boot=uefi").encode("utf-16-le") + b"\0\0"
    if len(ucmd) > 257 * 2:
        raise ValueError("ligne de commande trop longue")
    efi[m:m + len(ucmd)] = ucmd

    tree = {
        "EFI": {"BOOT": {"BOOTX64.EFI": bytes(efi)}},
        "TITANOS": {"VMLINUZ": kernel, "INITRD.IMG": initrd, "BOOT.IMG": bootimg},
        "LISEZMOI.TXT": readme,
    }
    fat = Fat32(part_sectors, PART_START)
    fs = fat.build(tree)
    kc, ksize = fat.placed["TITANOS/VMLINUZ"]
    ic, isize = fat.placed["TITANOS/INITRD.IMG"]
    bc, bsize = fat.placed["TITANOS/BOOT.IMG"]

    # chargeur BIOS : emplacement des fichiers et zero page
    s2 = bytearray(stage2)
    hdr = s2.find(b"TITN")
    zpo = s2.find(b"TZPG")
    cmo = s2.find(b"TCMD")
    if min(hdr, zpo, cmo) < 0:
        raise ValueError("marqueurs introuvables dans le chargeur BIOS")
    setup_sects = kernel[0x1F1] or 4
    pm_off = (setup_sects + 1) * SECTOR
    pm_sects = (ksize - pm_off + SECTOR - 1) // SECTOR
    init_size = struct.unpack_from("<I", kernel, 0x260)[0]
    # l'initramfs doit rester loin de la zone où le noyau se décompresse (16 Mo + init_size)
    initrd_addr = max(0x8000000, (0x1000000 + init_size + 0x800000 + 0xFFFFF) & ~0xFFFFF)
    addr_max = struct.unpack_from("<I", kernel, 0x22C)[0]
    if initrd_addr + isize > addr_max:
        raise ValueError("initramfs trop grand pour ce noyau")
    struct.pack_into("<IIIIIII", s2, hdr + 4, fat.lba_of(kc) + setup_sects + 1, pm_sects,
                     fat.lba_of(ic), (isize + SECTOR - 1) // SECTOR, initrd_addr,
                     fat.lba_of(bc), (bsize + SECTOR - 1) // SECTOR)
    bios_cmd = (cmdline + " titanos.boot=bios").encode("ascii") + b"\0"
    if len(bios_cmd) > 512:
        raise ValueError("ligne de commande trop longue")
    s2[cmo:cmo + 512] = bios_cmd.ljust(512, b"\0")
    s2[zpo:zpo + 4096] = zero_page(kernel, initrd_addr, isize, 0x8000 + cmo)

    disk = bytearray(total * SECTOR)
    disk[PART_START * SECTOR:PART_START * SECTOR + len(fs)] = fs
    mbr = bytearray(SECTOR)
    mbr[0:len(mbr_code)] = mbr_code
    mbr[440:444] = uuid.uuid4().bytes[:4]
    struct.pack_into("<B3sB3sII", mbr, 446, 0x80, chs(PART_START), 0x0C, chs(total - 1), PART_START, part_sectors)
    mbr[510:512] = b"\x55\xAA"
    disk[0:SECTOR] = mbr
    disk[STAGE2_LBA * SECTOR:STAGE2_LBA * SECTOR + len(s2)] = s2
    info = {"kernel_lba": fat.lba_of(kc), "initrd_lba": fat.lba_of(ic), "initrd_addr": initrd_addr,
            "init_size": init_size, "free_mb": (fat.clusters + 2 - fat.next) * fat.csize() >> 20}
    return disk, bytes(s2), info


def vhd_footer(size):
    ts = size // SECTOR
    if ts > 65535 * 16 * 255:
        ts = 65535 * 16 * 255
    if ts >= 65535 * 16 * 63:
        spt, heads = 255, 16
        cth = ts // spt
    else:
        spt = 17
        cth = ts // spt
        heads = max((cth + 1023) // 1024, 4)
        if cth >= heads * 1024 or heads > 16:
            spt, heads = 31, 16
            cth = ts // spt
        if cth >= heads * 1024:
            spt, heads = 63, 16
            cth = ts // spt
    cyl = cth // heads
    f = bytearray(struct.pack(">8sIIQI4sI4sQQHBBII16sB", b"conectix", 2, 0x00010000, 0xFFFFFFFFFFFFFFFF,
                              int(time.time()) - 946684800, b"tios", 0x00010000, b"Wi2k", size, size, cyl, heads,
                              spt, 2, 0, uuid.uuid4().bytes, 0)).ljust(512, b"\0")
    struct.pack_into(">I", f, 64, (~sum(f)) & 0xFFFFFFFF)
    return bytes(f)


def read_back(disk, path):
    """Relit un fichier dans l'image (vérification)."""
    first = struct.unpack_from("<I", disk, 446 + 8)[0]
    base = first * SECTOR
    bps, spc, rsvd, nfats = struct.unpack_from("<HBHB", disk, base + 11)
    fatsz = struct.unpack_from("<I", disk, base + 36)[0]
    root = struct.unpack_from("<I", disk, base + 44)[0]
    data = rsvd + nfats * fatsz
    fat_off = base + rsvd * bps

    def chain(c):
        out = []
        while c < 0x0FFFFFF8:
            out.append(c)
            c = struct.unpack_from("<I", disk, fat_off + c * 4)[0] & 0x0FFFFFFF
        return out

    def read(c, size=None):
        buf = b"".join(disk[base + (data + (x - 2) * spc) * bps:base + (data + (x - 2) * spc + spc) * bps] for x in chain(c))
        return buf if size is None else buf[:size]

    c, size = root, None
    for part in path.split("/"):
        d = read(c)
        for i in range(0, len(d), 32):
            e = d[i:i + 32]
            if e[0] == 0:
                raise FileNotFoundError(path)
            if e[:11] == short_name(part) and not e[11] & 0x08:     # pas l'étiquette du volume
                c = (struct.unpack_from("<H", e, 20)[0] << 16) | struct.unpack_from("<H", e, 26)[0]
                size = struct.unpack_from("<I", e, 28)[0]
                break
        else:
            raise FileNotFoundError(path)
    return read(c, size)


def write_images(disk_mb, efi, mbr_code, stage2, kernel, initrd, cmdline, readme, bootimg, img_path, vhd_path=None):
    disk, s2, info = build_disk(disk_mb, efi, mbr_code, stage2, kernel, initrd, cmdline, readme, bootimg)
    if read_back(disk, "TITANOS/VMLINUZ") != kernel or read_back(disk, "TITANOS/INITRD.IMG") != initrd:
        raise SystemExit("échec de la vérification de l'image")
    if disk[info["kernel_lba"] * SECTOR:info["kernel_lba"] * SECTOR + 1024] != kernel[:1024]:
        raise SystemExit("échec de la vérification : le noyau n'est pas là où l'attend le chargeur BIOS")
    with open(img_path, "wb") as f:
        f.write(disk)
    if vhd_path:
        with open(vhd_path, "wb") as f:
            f.write(disk)
            f.write(vhd_footer(len(disk)))
    return disk, s2, info
