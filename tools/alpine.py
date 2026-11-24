#!/usr/bin/env python3
"""Téléchargement et extraction de paquets Alpine Linux, sans outil externe.

TitanOs n'utilise d'Alpine que quelques briques : le noyau Linux (paquet linux-lts),
BusyBox (commandes du Terminal), les en-têtes du noyau (pour compiler) et une police.
Tout est mis en cache dans build/cache/ et vérifié (SHA-1 du contrôle, SHA-256 des données).
"""
import base64
import hashlib
import io
import os
import tarfile
import urllib.request
import zlib

MIRROR = "https://dl-cdn.alpinelinux.org/alpine"
BRANCH = "v3.24"
ARCH = "x86_64"


class Repo:
    def __init__(self, cache):
        self.cache = cache
        self.pkgs = {}      # nom -> champs de l'index (+ "repo")
        os.makedirs(cache, exist_ok=True)

    def _get(self, url, dest):
        if not os.path.exists(dest):
            print(f"  téléchargement {url.rsplit('/', 1)[-1]}")
            tmp = dest + ".part"
            with urllib.request.urlopen(url) as r, open(tmp, "wb") as f:
                while True:
                    b = r.read(1 << 20)
                    if not b:
                        break
                    f.write(b)
            os.replace(tmp, dest)
        return dest

    def load_index(self, repo):
        path = self._get(f"{MIRROR}/{BRANCH}/{repo}/{ARCH}/APKINDEX.tar.gz",
                         os.path.join(self.cache, f"APKINDEX-{repo}.tar.gz"))
        with tarfile.open(path) as t:
            text = t.extractfile("APKINDEX").read().decode()
        for block in text.split("\n\n"):
            d = {}
            for line in block.splitlines():
                if len(line) > 2 and line[1] == ":":
                    d.setdefault(line[0], line[2:])
            if "P" in d and d["P"] not in self.pkgs:
                d["repo"] = repo
                self.pkgs[d["P"]] = d

    def version(self, name):
        return self.pkgs[name]["V"]

    def resolve(self, names):
        """Paquets demandés + toutes leurs dépendances (y compris « so:libxxx.so »)."""
        provides = {}
        for n, d in self.pkgs.items():
            provides.setdefault(n, n)
            for p in d.get("p", "").split():
                provides.setdefault(p.split("=")[0], n)
        out, todo = [], list(names)
        while todo:
            n = todo.pop()
            n = provides.get(n.split("=")[0].split(">")[0].split("<")[0].split("~")[0], None)
            if not n or n in out:
                continue
            out.append(n)
            for dep in self.pkgs[n].get("D", "").split():
                if not dep.startswith("!") and not dep.startswith("/"):
                    todo.append(dep)
        return out

    def fetch(self, name):
        """Télécharge un paquet (si absent du cache) et vérifie son intégrité."""
        if name not in self.pkgs:
            raise SystemExit(f"paquet Alpine introuvable : {name}")
        d = self.pkgs[name]
        fname = f"{name}-{d['V']}.apk"
        path = self._get(f"{MIRROR}/{BRANCH}/{d['repo']}/{ARCH}/{fname}", os.path.join(self.cache, fname))
        members = gzip_members(open(path, "rb").read())
        if len(members) < 3:
            raise SystemExit(f"{fname} : format inattendu")
        ctrl_raw, data_raw = members[-2][0], members[-1][0]
        want = d.get("C", "")
        if want.startswith("Q1") and base64.b64decode(want[2:]) != hashlib.sha1(ctrl_raw).digest():
            os.remove(path)
            raise SystemExit(f"{fname} : somme de contrôle invalide (fichier supprimé, relancez)")
        info = tar_segment(members[-2][1]).get(".PKGINFO")
        if info:
            for line in info[1].decode().splitlines():
                if line.startswith("datahash = "):
                    if hashlib.sha256(data_raw).hexdigest() != line.split("=", 1)[1].strip():
                        os.remove(path)
                        raise SystemExit(f"{fname} : données corrompues (fichier supprimé, relancez)")
        return tar_segment(members[-1][1])


def gzip_members(blob):
    """Sépare les flux gzip concaténés d'un .apk : [(octets compressés, octets décompressés)]."""
    out, pos = [], 0
    while pos < len(blob):
        d = zlib.decompressobj(31)
        data = d.decompress(blob[pos:])
        used = len(blob) - pos - len(d.unused_data)
        out.append((blob[pos:pos + used], data))
        pos += used
        if not d.eof:
            break
    return out


def tar_segment(raw):
    """Fichiers d'un segment tar : {chemin: (type, contenu|cible, mode)}."""
    files = {}
    with tarfile.open(fileobj=io.BytesIO(raw), ignore_zeros=True) as t:
        for m in t:
            name = m.name.lstrip("./")
            if m.isfile():
                files[name] = ("f", t.extractfile(m).read(), m.mode)
            elif m.issym():
                files[name] = ("l", m.linkname, m.mode)
            elif m.isdir():
                files[name] = ("d", b"", m.mode)
    return files
