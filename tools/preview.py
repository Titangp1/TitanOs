#!/usr/bin/env python3
"""Aperçu du bureau TitanOs sous Windows : pilote la DLL de simulation et enregistre des PNG.

Utilisé par « python build.py preview ». Chaque scène est une suite d'actions
(clics, touches, ouverture d'applications) suivie d'une capture.
"""
import ctypes
import os
import struct
import zlib

W, H = 1600, 900
K = {"char": 1, "esc": 2, "enter": 3, "backspace": 4, "tab": 5, "super": 28}


def write_png(path, w, h, pixels):
    """pixels : bytes en BGRA (format de la scène) -> PNG RGB."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        row = pixels[y * w * 4:(y + 1) * w * 4]
        rgb = bytearray(w * 3)
        rgb[0::3] = row[2::4]
        rgb[1::3] = row[1::4]
        rgb[2::3] = row[0::4]
        raw += rgb

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


class Desk:
    def __init__(self, dll):
        self.d = ctypes.CDLL(dll)
        self.d.sim_pixels.restype = ctypes.POINTER(ctypes.c_uint32)
        self.d.sim_time.argtypes = [ctypes.c_uint64]
        self.t = 1000
        self.d.sim_time(self.t)
        self.d.sim_init(W, H)

    def frame(self, ms=16):
        self.t += ms
        self.d.sim_time(self.t)
        self.d.sim_frame()

    def wait(self, ms):
        """Laisse tourner les animations pendant ms millisecondes (images de 16 ms)."""
        for _ in range(max(1, ms // 16)):
            self.frame(16)

    def settle(self):
        self.wait(600)

    def move(self, x, y):
        self.d.sim_move(x, y)
        self.frame()

    def click(self, x, y, button=1, double=False):
        self.move(x, y)
        for _ in range(2 if double else 1):
            self.d.sim_button(button, 1)
            self.d.sim_button(button, 0)
            self.frame(40)

    def key(self, name, ch=0, mods=0):
        code = K[name] if isinstance(name, str) else name
        self.d.sim_key(code, ch, mods, 1)
        self.d.sim_key(code, ch, mods, 0)
        self.frame()

    def type(self, text):
        for c in text:
            self.key("char", ord(c))

    def shot(self, path, settle=True):
        if settle:
            self.settle()
        p = self.d.sim_pixels()
        buf = ctypes.string_at(p, W * H * 4)
        write_png(path, W, H, buf)
        print("  capture", os.path.basename(path))


K_F4, K_VOL_UP, MOD_ALT = 19, 32, 4


def render_all(dll, outdir):
    d = Desk(dll)
    out = lambda name: os.path.join(outdir, name)
    d.d.sim_splash("Chargement des pilotes…".encode("utf-8"), 45)
    d.wait(700)
    d.shot(out("01-demarrage.png"), settle=False)
    d.d.sim_start()
    d.wait(200)
    d.shot(out("02-fondu-vers-verrouillage.png"), settle=False)
    d.shot(out("03-verrouillage.png"))
    d.click(W // 2, H // 2)
    d.wait(150)
    d.shot(out("04-deverrouillage-anime.png"), settle=False)
    d.shot(out("05-bureau.png"))
    d.d.sim_open(1)   # Terminal
    d.settle()
    d.d.sim_open(0)   # Fichiers
    d.wait(90)
    d.shot(out("06-ouverture-animee.png"), settle=False)
    d.move(W // 2, H - 40)
    d.shot(out("07-fenetres.png"))
    d.key("super")
    d.shot(out("08-menu.png"))
    d.type("par")
    d.key("enter")
    d.shot(out("09-parametres.png"))
    d.d.sim_open(6)   # Réseau
    d.shot(out("10-reseau-wifi.png"))
    d.key(K_VOL_UP)
    d.wait(300)
    d.shot(out("11-volume.png"), settle=False)
    d.d.sim_open(5)   # Système
    d.settle()
    d.click(W - 80, H - 40)   # horloge -> calendrier
    d.shot(out("12-systeme-calendrier.png"))
    d.key("esc")
    d.d.sim_open(1)
    d.settle()
    d.d.sim_open(2)   # Bloc-notes
    d.settle()
    d.type("Bonjour depuis TitanOs !")
    d.d.sim_open(3)   # Calculatrice
    d.settle()
    d.type("12*3=")
    d.shot(out("13-notes-calculatrice.png"))
