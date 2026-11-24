#!/usr/bin/env python3
"""Images de l'écran de démarrage pour les chargeurs (TITANOS/BOOT.IMG).

Le chargeur BIOS (boot/stage2.S) et le chargeur UEFI (boot/efiboot.c) dessinent dès
l'allumage le même écran que TitanOs : dégradé, logo, nom et cercle de points animé.
Ce module précalcule ces petites images avec exactement la même géométrie que le code C
(src/core/wm.c, draw_splash) : le passage du chargeur au système est invisible.

Format (petit-boutiste) :
  0   "TBIM"
  4   u16 logo_w, logo_h        logo en BGRA prémultiplié
  8   u16 word_w, word_h        nom « TitanOs » : couverture 8 bits (blanc)
  12  u16 dot_w, dot_h          point du cercle : couverture 8 bits (blanc)
  16  u16 nframes, ndots        positions des points pour chaque image de l'animation
  20  i16 logo_top              haut du logo, par rapport au centre de l'écran
  22  i16 word_top, word_ox     haut et décalage gauche de l'image du nom
  26  u16 word_tw               largeur du texte (pour le centrer)
  28  i16 spin_y, u16 spin_box  centre du cercle (par rapport au centre) et demi-taille de sa zone
  32  u32 bg_top, bg_bottom     couleurs du dégradé (0x00RRGGBB)
  40  u32 off_logo, off_word, off_dot, off_frames
  56  données ; images : nframes x ndots x (i8 dx, i8 dy) = coin du point par rapport au centre
"""
import math
import struct

# mêmes valeurs que src/core/wm.c
LOGO_TOP, LOGO_SIZE, WORD_TOP, SPIN_Y, SPIN_R, DOT_R = -150, 110, -22, 100, 18, 3.2
BG_TOP, BG_BOTTOM = 0x070B18, 0x161237
FPS = 18.2                      # le compteur du BIOS avance 18,2 fois par seconde
NFRAMES = 36                    # un tour complet de l'animation dure 36 tops (2 s)


def mix(a, b, t):
    """col_mix de gfx.c : t de 0 à 256."""
    return tuple((a[i] * (256 - t) + b[i] * t) >> 8 for i in range(3))


def rgb(c):
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255)


def tri_dist(px, py, pts):
    """Distance signée au triangle (positive dehors), comme gfx_tri."""
    (x0, y0), (x1, y1), (x2, y2) = pts
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    if area < 0:
        pts = [pts[0], pts[2], pts[1]]
    d = -1e9
    for e in range(3):
        ax, ay = pts[e]
        bx, by = pts[(e + 1) % 3]
        lx, ly = bx - ax, by - ay
        ln = math.hypot(lx, ly)
        nx, ny = ly / ln, -lx / ln
        d = max(d, nx * px + ny * py - (nx * ax + ny * ay))
    return d


def logo_sprite(size):
    half, inner = size * 0.6, size * 0.45
    w = int(math.ceil(half)) * 2 + 2
    h = int(math.ceil(size)) + 1
    cx = w / 2
    layers = [
        ([(cx, 0), (cx - half, size), (cx + half, size)], rgb(0x7DD3FC), rgb(0x6D28D9)),
        ([(cx, size - inner), (cx - inner * 0.6, size), (cx + inner * 0.6, size)], rgb(0x1E1B4B), rgb(0x312E81)),
    ]
    rw = max(1.6, size / 50)
    ridge = (cx - rw / 2, size / 6, rw, size - size / 6 - inner)
    px = [[(0, 0, 0, 0)] * w for _ in range(h)]
    for j in range(h):
        for i in range(w):
            r = g = b = a = 0.0
            for pts, top, bot in layers:
                miny = min(p[1] for p in pts)
                maxy = max(p[1] for p in pts)
                cov = max(0.0, min(1.0, 0.5 - tri_dist(i + 0.5, j + 0.5, pts)))
                if cov <= 0:
                    continue
                col = mix(top, bot, int((j + 0.5 - miny) * 256 / (maxy - miny)))
                r, g, b, a = (col[0] * cov + r * (1 - cov), col[1] * cov + g * (1 - cov),
                              col[2] * cov + b * (1 - cov), cov + a * (1 - cov))
            x0, y0, rw_, rh = ridge
            cxr = max(0.0, min(1.0, min(i + 1, x0 + rw_) - max(i, x0)))
            cyr = max(0.0, min(1.0, min(j + 1, y0 + rh) - max(j, y0)))
            cov = cxr * cyr
            if cov > 0:
                r, g, b, a = (0xE0 * cov + r * (1 - cov), 0xF2 * cov + g * (1 - cov), 0xFE * cov + b * (1 - cov),
                              cov + a * (1 - cov))
            px[j][i] = (r, g, b, a)
    data = bytearray()
    for j in range(h):
        for i in range(w):
            r, g, b, a = px[j][i]            # déjà « prémultiplié » : r = couleur x couverture
            data += bytes((int(b + 0.5), int(g + 0.5), int(r + 0.5), int(a * 255 + 0.5)))
    return w, h, bytes(data)


def word_sprite(ttc, text="TitanOs", face_name="Inter Display SemiBold", px=36):
    import mkfont
    face = mkfont.open_face(ttc, face_name)
    scale = px / face.upem
    ascent = int(round(face.ascent * scale))
    glyphs, pen = [], 0
    for ch in text:
        ox, oy, w, h, bm, adv = mkfont.render_glyph(face, face.cmap[ord(ch)], px)
        gx = ((pen + 32) >> 6) + ox
        gy = ascent + oy
        glyphs.append((gx, gy, w, h, bm))
        pen += int(round(adv * 64))
    tw = (pen + 32) >> 6
    x0 = min(g[0] for g in glyphs)
    y0 = min(g[1] for g in glyphs)
    x1 = max(g[0] + g[2] for g in glyphs)
    y1 = max(g[1] + g[3] for g in glyphs)
    W, H = x1 - x0, y1 - y0
    img = bytearray(W * H)
    for gx, gy, w, h, bm in glyphs:
        for j in range(h):
            for i in range(w):
                k = (gy - y0 + j) * W + gx - x0 + i
                img[k] = max(img[k], bm[j * w + i])
    return W, H, bytes(img), x0, y0, tw


def dot_sprite(r):
    n = int(math.ceil(r)) * 2 + 2
    c = n / 2
    img = bytearray(n * n)
    for j in range(n):
        for i in range(n):
            d = math.hypot(i + 0.5 - c, j + 0.5 - c) - r
            img[j * n + i] = int(max(0.0, min(1.0, 0.5 - d)) * 255 + 0.5)
    return n, n, bytes(img)


def spinner_frames(dot_w):
    """Même mouvement que draw_spinner (wm.c) : cinq points qui ralentissent en haut."""
    frames = bytearray()
    for f in range(NFRAMES):
        t = f / FPS
        for i in range(5):
            p = (t - i * 0.11) / 2.0
            p -= math.floor(p)
            ang = -math.pi / 2 + 2 * math.pi * (p - math.sin(2 * math.pi * p) / (2 * math.pi))
            x = math.cos(ang) * SPIN_R - dot_w / 2
            y = math.sin(ang) * SPIN_R - dot_w / 2
            frames += struct.pack("<bb", int(round(x)), int(round(y)))
    return bytes(frames)


def build(ttc):
    lw, lh, logo = logo_sprite(LOGO_SIZE)
    ww, wh, word, wox, woy, wtw = word_sprite(ttc)
    dw, dh, dot = dot_sprite(DOT_R)
    frames = spinner_frames(dw)
    off_logo = 56
    off_word = off_logo + len(logo)
    off_dot = off_word + len(word)
    off_frames = off_dot + len(dot)
    hdr = struct.pack("<4sHHHHHHHHhhhHhHIIIIII", b"TBIM", lw, lh, ww, wh, dw, dh, NFRAMES, 5,
                      LOGO_TOP, WORD_TOP + woy, wox, wtw, SPIN_Y, SPIN_R + dw, BG_TOP, BG_BOTTOM,
                      off_logo, off_word, off_dot, off_frames)
    assert len(hdr) == 56
    return hdr + logo + word + dot + frames


def render_preview(img, W, H, frame=0):
    """Ce que dessinent les chargeurs (pour comparer avec l'écran de TitanOs)."""
    (lw, lh, ww, wh, dw, dh, nf, nd, ltop, wtop, wox, wtw, sy, sbox, bgt, bgb,
     ol, ow, od, of) = struct.unpack_from("<HHHHHHHHhhhHhHIIIIII", img, 4)
    out = bytearray(W * H * 3)
    t, b = rgb(bgt), rgb(bgb)
    rows = [mix(t, b, y * 256 // (H - 1)) for y in range(H)]
    for y in range(H):
        out[y * W * 3:(y + 1) * W * 3] = bytes(rows[y]) * W
    cx, cy = W // 2, H // 2

    def put(x, y, c):
        out[(y * W + x) * 3:(y * W + x) * 3 + 3] = bytes(c)
    for j in range(lh):
        for i in range(lw):
            pb, pg, pr, pa = img[ol + (j * lw + i) * 4:ol + (j * lw + i) * 4 + 4]
            x, y = cx - lw // 2 + i, cy + ltop + j
            bg = rows[y]
            put(x, y, (pr + bg[0] * (255 - pa) // 255, pg + bg[1] * (255 - pa) // 255, pb + bg[2] * (255 - pa) // 255))
    for j in range(wh):
        for i in range(ww):
            a = img[ow + j * ww + i]
            x, y = (W - wtw) // 2 + wox + i, cy + wtop + j
            bg = rows[y]
            put(x, y, tuple(bg[k] + (0xF8 - bg[k]) * a // 255 for k in range(3)))
    for d in range(nd):
        dx, dy = struct.unpack_from("<bb", img, of + (frame * nd + d) * 2)
        for j in range(dh):
            for i in range(dw):
                a = img[od + j * dw + i]
                x, y = cx + dx + i, cy + sy + dy + j
                bg = rows[y]
                put(x, y, tuple(bg[k] + (255 - bg[k]) * a // 255 for k in range(3)))
    return bytes(out)
