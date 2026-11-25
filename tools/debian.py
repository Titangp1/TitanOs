"""TitanOs - base Debian : paquets, dépendances, archive à installer au premier démarrage.

Sans Linux sous la main (ni debootstrap, ni dpkg), on fait ici la « première étape » de
debootstrap : lire l'index des paquets de Debian, résoudre les dépendances, télécharger les
.deb (vérifiés par leur SHA-256) et préparer une arborescence où les paquets essentiels
sont déjà extraits. La « seconde étape » (dpkg, scripts des paquets, configuration) se fait
au premier démarrage, sur le PC, par TitanOs (rootfs/titanos-setup.sh).
"""
import hashlib, io, lzma, os, re, sys, tarfile, urllib.request

MIRROR = "https://deb.debian.org/debian"
SUITE = "trixie"
COMPONENTS = ["main", "contrib", "non-free-firmware"]
ARCH = "amd64"


def _get(url, dest):
    if os.path.exists(dest):
        return dest
    print(f"  téléchargement {url.rsplit('/', 1)[-1]}")
    tmp = dest + ".part"
    req = urllib.request.Request(url, headers={"User-Agent": "TitanOs-build"})
    with urllib.request.urlopen(req) as r, open(tmp, "wb") as f:
        while True:
            b = r.read(1 << 20)
            if not b:
                break
            f.write(b)
    os.replace(tmp, dest)
    return dest


def _zstd_decompress(data):
    from compression import zstd
    return zstd.decompress(data)


class Repo:
    def __init__(self, cache):
        self.cache = os.path.join(cache, "debian")
        os.makedirs(self.cache, exist_ok=True)
        self.pkgs = {}          # nom -> champs
        self.provides = {}      # nom virtuel -> [paquets]

    def load(self):
        for comp in COMPONENTS:
            path = _get(f"{MIRROR}/dists/{SUITE}/{comp}/binary-{ARCH}/Packages.xz",
                        os.path.join(self.cache, f"Packages-{SUITE}-{comp}.xz"))
            text = lzma.decompress(open(path, "rb").read()).decode("utf-8", "replace")
            for block in text.split("\n\n"):
                d, key = {}, None
                for line in block.split("\n"):
                    if not line:
                        continue
                    if line[0] in " \t" and key:
                        d[key] += "\n" + line.strip()
                        continue
                    k, _, v = line.partition(":")
                    key = k
                    d[k] = v.strip()
                name = d.get("Package")
                if not name or name in self.pkgs:
                    continue
                self.pkgs[name] = d
                for p in self._split(d.get("Provides", "")):
                    for alt in p:
                        self.provides.setdefault(alt, []).append(name)
        return self

    @staticmethod
    def _split(field):
        """« a (>= 1), b | c » -> [[a], [b, c]] (sans versions ni :any)"""
        out = []
        for part in field.split(","):
            alts = [re.sub(r"\s*\(.*?\)|\s*\[.*?\]|\s*<.*?>", "", a).strip().split(":")[0] for a in part.split("|")]
            alts = [a for a in alts if a]
            if alts:
                out.append(alts)
        return out

    def real(self, name):
        if name in self.pkgs:
            return name
        for p in self.provides.get(name, []):
            return p
        return None

    def resolve(self, names, avoid=()):
        """paquets demandés + dépendances (Depends, Pre-Depends) ; premier choix d'une alternative"""
        want, todo = [], list(names)
        seen = set()
        while todo:
            n = self.real(todo.pop(0))
            if not n or n in seen or n in avoid:
                continue
            seen.add(n)
            want.append(n)
            d = self.pkgs[n]
            for alts in self._split(d.get("Pre-Depends", "")) + self._split(d.get("Depends", "")):
                chosen = None
                for a in alts:                         # déjà choisi ? sinon le premier qui existe
                    r = self.real(a)
                    if r and r in seen:
                        chosen = r
                        break
                if not chosen:
                    for a in alts:
                        r = self.real(a)
                        if r and r not in avoid:
                            chosen = r
                            break
                if chosen and chosen not in seen:
                    todo.append(chosen)
        return want

    def required(self):
        return sorted(n for n, d in self.pkgs.items() if d.get("Priority") == "required" and d.get("Section", "").split("/")[-1] != "kernel")

    def fetch(self, name):
        d = self.pkgs[name]
        fn = d["Filename"]
        path = _get(f"{MIRROR}/{fn}", os.path.join(self.cache, fn.rsplit("/", 1)[-1]))
        data = open(path, "rb").read()
        if hashlib.sha256(data).hexdigest() != d["SHA256"]:
            os.remove(path)
            sys.exit(f"paquet abîmé : {name}")
        return path, data


def deb_members(data):
    """archive « ar » d'un .deb -> {nom: contenu}"""
    assert data[:8] == b"!<arch>\n", "pas un .deb"
    o, out = 8, {}
    while o + 60 <= len(data):
        h = data[o:o + 60]
        name = h[:16].decode().strip().rstrip("/")
        size = int(h[48:58].decode().strip())
        out[name] = data[o + 60:o + 60 + size]
        o += 60 + size + (size & 1)
    return out


def deb_tar(blob, name):
    if name.endswith(".xz"):
        return lzma.decompress(blob)
    if name.endswith(".zst"):
        return _zstd_decompress(blob)
    if name.endswith(".gz"):
        import gzip
        return gzip.decompress(blob)
    return blob


def deb_data(data):
    m = deb_members(data)
    for k, v in m.items():
        if k.startswith("data.tar"):
            return tarfile.open(fileobj=io.BytesIO(deb_tar(v, k)))
    raise ValueError("data.tar introuvable")


def deb_control(data):
    m = deb_members(data)
    for k, v in m.items():
        if k.startswith("control.tar"):
            t = tarfile.open(fileobj=io.BytesIO(deb_tar(v, k)))
            f = t.extractfile("./control") or t.extractfile("control")
            return f.read().decode()
    return ""


USR_MERGE = {"bin": "usr/bin", "sbin": "usr/sbin", "lib": "usr/lib", "lib64": "usr/lib64"}


def merged(path):
    """chemin d'un paquet -> chemin réel avec /usr fusionné (/bin -> /usr/bin...)"""
    p = path.lstrip("./")
    first, _, rest = p.partition("/")
    if first in USR_MERGE:
        return USR_MERGE[first] + ("/" + rest if rest else "")
    return p


# --------------------------------------------------------------------------------------------
# Archive de la base Debian, installée au premier démarrage
# --------------------------------------------------------------------------------------------
# Programmes de la base TitanOs (en plus des paquets « required » de Debian)
BASE = ["apt", "debian-archive-keyring", "ca-certificates", "systemd", "systemd-sysv", "udev", "dbus", "dbus-user-session",
        "libpam-systemd", "systemd-timesyncd", "kmod", "polkitd", "busybox",
        # réseau : NetworkManager s'occupe du Wi-Fi (WPA2, WPA3, Entreprise) et du câble
        "network-manager", "wpasupplicant", "iw", "wireless-regdb", "firmware-iwlwifi", "iproute2", "iputils-ping",
        # son : PipeWire (Chrome, Discord...), avec ALSA
        "pipewire", "pipewire-pulse", "pipewire-alsa", "wireplumber", "alsa-utils",
        # applications graphiques : serveur X en mémoire, polices
        "xvfb", "x11-xkb-utils", "xauth", "fonts-dejavu-core", "fonts-liberation2", "fontconfig",
        # langue, heure, outils du Terminal
        "locales", "tzdata", "curl", "wget", "gnupg", "xz-utils", "zstd", "unzip", "zip", "bzip2", "less", "nano", "htop",
        "bash-completion", "pciutils", "usbutils", "file", "tree", "bc", "psmisc", "lsof", "procps", "dosfstools",
        "exfatprogs", "ntfs-3g", "rsync", "adduser", "passwd"]
AVOID = {"initramfs-tools", "linux-initramfs-tool", "dracut", "kbd", "console-setup", "keyboard-configuration"}


class TarOut:
    """Archive tar (format GNU) écrite au fil de l'eau : dossiers, fichiers, liens."""
    def __init__(self, fileobj):
        self.t = tarfile.open(fileobj=fileobj, mode="w|", format=tarfile.GNU_FORMAT)
        self.seen = set()

    def _info(self, name, kind, mode, size=0, link=""):
        ti = tarfile.TarInfo(name)
        ti.type, ti.mode, ti.size, ti.linkname = kind, mode, size, link
        ti.uid = ti.gid = 0
        ti.uname = ti.gname = "root"
        ti.mtime = 1735689600
        return ti

    def dir(self, name, mode=0o755):
        if name in self.seen:
            return
        self.seen.add(name)
        self.t.addfile(self._info(name, tarfile.DIRTYPE, mode))

    def parents(self, name):
        parts = name.split("/")
        for i in range(1, len(parts)):
            p = "/".join(parts[:i])
            if p and p not in self.seen and p not in USR_MERGE:
                self.dir(p)

    def file(self, name, data, mode=0o644):
        self.parents(name)
        self.seen.add(name)
        self.t.addfile(self._info(name, tarfile.REGTYPE, mode, len(data)), io.BytesIO(data))

    def link(self, name, target):
        self.parents(name)
        self.seen.add(name)
        self.t.addfile(self._info(name, tarfile.SYMTYPE, 0o777, 0, target))

    def hard(self, name, target):
        self.parents(name)
        self.seen.add(name)
        self.t.addfile(self._info(name, tarfile.LNKTYPE, 0o644, 0, target))

    def close(self):
        self.t.close()


def build_base(repo, kfiles, kver, extra_files, out_dir):
    """Écrit out_dir/DEBIAN.TXZ (paquets essentiels extraits, réglages de TitanOs, pilotes du
    noyau) et out_dir/DEBS.TAR (tous les .deb). Renvoie le nombre d'entrées et les tailles."""
    import gzip
    required = repo.resolve(repo.required(), AVOID)
    allp = repo.resolve(repo.required() + BASE, AVOID)
    base = [p for p in allp if p not in required]
    print(f"  Debian {SUITE} : {len(required)} paquets essentiels, {len(base)} autres")
    debs = {}
    for n in allp:
        path, data = repo.fetch(n)
        debs[n] = (os.path.basename(path), data)

    # DEBS.TAR : les paquets tels quels (déjà compressés)
    with open(os.path.join(out_dir, "DEBS.TAR"), "wb") as f:
        t = tarfile.open(fileobj=f, mode="w", format=tarfile.GNU_FORMAT)
        for n in allp:
            fn, data = debs[n]
            ti = tarfile.TarInfo(fn)
            ti.size, ti.mode, ti.mtime = len(data), 0o644, 1735689600
            t.addfile(ti, io.BytesIO(data))
        t.close()

    # DEBIAN.TXZ : la « première étape »
    count = 0
    # contrôle CRC32 : celui que tout décodeur xz connaît (BusyBox compris)
    with lzma.open(os.path.join(out_dir, "DEBIAN.TXZ"), "wb", preset=6, check=lzma.CHECK_CRC32) as xz:
        out = TarOut(xz)
        for src, dst in USR_MERGE.items():                  # /usr fusionné : /bin -> usr/bin...
            out.dir(dst)
            out.link(src, dst)
        for n in required:                                   # paquets essentiels extraits
            tf = deb_data(debs[n][1])
            for m in tf:
                name = merged(m.name)
                if not name or name in out.seen and not m.isfile():
                    continue
                if m.isdir():
                    if name not in USR_MERGE:
                        out.parents(name)
                        out.dir(name, m.mode)
                elif m.issym():
                    out.link(name, m.linkname)
                elif m.islnk():
                    out.hard(name, merged(m.linkname))
                elif m.isfile():
                    out.file(name, tf.extractfile(m).read(), m.mode)
                count += 1
        for d in ("var/lib/dpkg/info", "var/lib/dpkg/updates", "var/lib/dpkg/triggers", "var/cache/apt/archives/partial",
                  "proc", "sys", "dev", "run", "tmp", "media/titanos", ".titan", "home"):
            out.parents(d + "/x")
            out.dir(d, 0o1777 if d == "tmp" else 0o755)
        out.file("var/lib/dpkg/status", b"")
        out.file("var/lib/dpkg/available", b"")
        out.file("var/lib/dpkg/arch", b"amd64\n")
        out.file("etc/fstab", b"# TitanOs : les partitions sont montees au demarrage par TitanOs\n")
        out.file("etc/hostname", b"titanos\n")
        # pilotes du noyau de TitanOs : recompressés en zstd (.ko.zst), que le kmod de Debian lit
        # (il ne lit pas le gzip d'Alpine, et le noyau ne décompresse pas lui-même)
        from compression import zstd
        from concurrent.futures import ThreadPoolExecutor
        mods = [(merged(name), kind, data) for name, (kind, data, mode) in sorted(kfiles.items())
                if name.startswith(f"lib/modules/{kver}/")]

        def conv(item):
            rel, kind, data = item
            if kind == "f" and rel.endswith(".ko.gz"):
                return rel[:-3] + ".zst", kind, zstd.compress(gzip.decompress(data), level=12)
            return item
        nmods = 0
        with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            for rel, kind, data in pool.map(conv, mods):
                if kind == "f":
                    if rel.endswith((".dep", ".alias", ".symbols", ".softdep", ".devname", ".bin")):
                        continue                             # refaits par depmod au premier démarrage
                    out.file(rel, data, 0o644)
                    nmods += 1
                elif kind == "l":
                    out.link(rel, data)
        # TitanOs : listes, script de seconde étape, réglages
        out.file("titanos/required.list", ("\n".join(required) + "\n").encode())
        out.file("titanos/base.list", ("\n".join(base) + "\n").encode())
        out.file("titanos/kernel", (kver + "\n").encode())
        for rel, (data, mode) in sorted(extra_files.items()):
            out.file("titanos/" + rel, data, mode)
        out.close()
    return {"required": len(required), "base": len(base), "entries": count, "modules": nmods,
            "txz": os.path.getsize(os.path.join(out_dir, "DEBIAN.TXZ")), "debs": os.path.getsize(os.path.join(out_dir, "DEBS.TAR"))}
