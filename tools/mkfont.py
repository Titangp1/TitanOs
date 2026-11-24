#!/usr/bin/env python3
"""Rasterise des polices TrueType (contours 'glyf') en glyphes lissés pour TitanOs.

Pas de FreeType ni de bibliothèque : lecture des tables TrueType, aplatissement des
courbes de Bézier quadratiques, puis calcul exact de la couverture de chaque pixel
(accumulation d'aires signées). Le résultat est un fichier C (tableaux de glyphes
8 bits) compilé avec le bureau.

Utilisé par build.py : mkfont.generate(ttc_bytes, specs, sortie_c)
"""
import math
import struct

# caractères générés : ASCII, Latin-1 et quelques signes utiles en français
CHARSET = (list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) +
           [0x152, 0x153, 0x178, 0x20AC, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x2013, 0x2014,
            0x2022, 0x2190, 0x2191, 0x2192, 0x2193, 0x2713, 0x2715, 0x25B2, 0x25BC, 0x2212, 0x2009])


class Face:
    def __init__(self, data, offset):
        self.d = data
        self.t = {}
        n = struct.unpack_from(">H", data, offset + 4)[0]
        for i in range(n):
            tag, _, off, ln = struct.unpack_from(">4sIII", data, offset + 12 + 16 * i)
            self.t[tag.decode("latin-1")] = (off, ln)
        head = self.t["head"][0]
        self.upem = struct.unpack_from(">H", data, head + 18)[0]
        self.loca_long = struct.unpack_from(">h", data, head + 50)[0] == 1
        hhea = self.t["hhea"][0]
        self.ascent, self.descent, self.gap = struct.unpack_from(">hhh", data, hhea + 4)
        self.nhm = struct.unpack_from(">H", data, hhea + 34)[0]
        self.nglyphs = struct.unpack_from(">H", data, self.t["maxp"][0] + 4)[0]
        self.cmap = self._cmap()

    def name(self, nid=4):
        o = self.t["name"][0]
        _, cnt, so = struct.unpack_from(">HHH", self.d, o)
        for i in range(cnt):
            pid, eid, lid, n_id, ln, lo = struct.unpack_from(">6H", self.d, o + 6 + 12 * i)
            if n_id == nid and pid == 3:
                return self.d[o + so + lo:o + so + lo + ln].decode("utf-16-be")
        return ""

    def _cmap(self):
        d, o = self.d, self.t["cmap"][0]
        n = struct.unpack_from(">H", d, o + 2)[0]
        best = None
        for i in range(n):
            pid, eid, off = struct.unpack_from(">HHI", d, o + 4 + 8 * i)
            fmt = struct.unpack_from(">H", d, o + off)[0]
            if fmt == 12 and (best is None or best[0] != 12):
                best = (12, o + off)
            elif fmt == 4 and best is None and pid == 3:
                best = (4, o + off)
        m = {}
        fmt, t = best
        if fmt == 12:
            ngroups = struct.unpack_from(">I", d, t + 12)[0]
            for g in range(ngroups):
                a, b, gid = struct.unpack_from(">III", d, t + 16 + 12 * g)
                for c in range(a, min(b, 0x2FFFF) + 1):
                    m[c] = gid + c - a
        else:
            segx2 = struct.unpack_from(">H", d, t + 6)[0]
            ends = struct.unpack_from(f">{segx2 // 2}H", d, t + 14)
            starts = struct.unpack_from(f">{segx2 // 2}H", d, t + 16 + segx2)
            deltas = struct.unpack_from(f">{segx2 // 2}h", d, t + 16 + 2 * segx2)
            roff_pos = t + 16 + 3 * segx2
            roffs = struct.unpack_from(f">{segx2 // 2}H", d, roff_pos)
            for s in range(segx2 // 2):
                for c in range(starts[s], ends[s] + 1):
                    if c == 0xFFFF:
                        continue
                    if roffs[s] == 0:
                        g = (c + deltas[s]) & 0xFFFF
                    else:
                        p = roff_pos + 2 * s + roffs[s] + 2 * (c - starts[s])
                        g = struct.unpack_from(">H", d, p)[0]
                        if g:
                            g = (g + deltas[s]) & 0xFFFF
                    m[c] = g
        return m

    def advance(self, gid):
        o = self.t["hmtx"][0]
        i = min(gid, self.nhm - 1)
        return struct.unpack_from(">H", self.d, o + 4 * i)[0]

    def _loc(self, gid):
        o = self.t["loca"][0]
        if self.loca_long:
            a, b = struct.unpack_from(">II", self.d, o + 4 * gid)
        else:
            a, b = struct.unpack_from(">HH", self.d, o + 2 * gid)
            a, b = a * 2, b * 2
        return self.t["glyf"][0] + a, b - a

    def contours(self, gid, depth=0):
        """Contours du glyphe : liste de listes de points (x, y, sur_la_courbe)."""
        off, ln = self._loc(gid)
        if ln == 0 or depth > 8:
            return []
        d = self.d
        nc = struct.unpack_from(">h", d, off)[0]
        p = off + 10
        if nc >= 0:
            ends = struct.unpack_from(f">{nc}H", d, p)
            p += 2 * nc
            ilen = struct.unpack_from(">H", d, p)[0]
            p += 2 + ilen
            npts = ends[-1] + 1 if nc else 0
            flags = []
            while len(flags) < npts:
                f = d[p]
                p += 1
                flags.append(f)
                if f & 8:
                    r = d[p]
                    p += 1
                    flags.extend([f] * r)
            xs, ys = [], []
            v = 0
            for f in flags:
                if f & 2:
                    dx = d[p]
                    p += 1
                    v += dx if f & 16 else -dx
                elif not f & 16:
                    v += struct.unpack_from(">h", d, p)[0]
                    p += 2
                xs.append(v)
            v = 0
            for f in flags:
                if f & 4:
                    dy = d[p]
                    p += 1
                    v += dy if f & 32 else -dy
                elif not f & 32:
                    v += struct.unpack_from(">h", d, p)[0]
                    p += 2
                ys.append(v)
            out, start = [], 0
            for e in ends:
                out.append([(xs[i], ys[i], bool(flags[i] & 1)) for i in range(start, e + 1)])
                start = e + 1
            return out
        # glyphe composé (lettres accentuées...)
        out = []
        while True:
            fl, sub = struct.unpack_from(">HH", d, p)
            p += 4
            if fl & 1:
                a1, a2 = struct.unpack_from(">hh", d, p)
                p += 4
            else:
                a1, a2 = struct.unpack_from(">bb", d, p)
                p += 2
            m = (1.0, 0.0, 0.0, 1.0)
            if fl & 8:
                s = struct.unpack_from(">h", d, p)[0] / 16384
                p += 2
                m = (s, 0.0, 0.0, s)
            elif fl & 0x40:
                sx, sy = struct.unpack_from(">hh", d, p)
                p += 4
                m = (sx / 16384, 0.0, 0.0, sy / 16384)
            elif fl & 0x80:
                a, b, c, e = struct.unpack_from(">hhhh", d, p)
                p += 8
                m = (a / 16384, b / 16384, c / 16384, e / 16384)
            dx, dy = (a1, a2) if fl & 2 else (0, 0)
            for c in self.contours(sub, depth + 1):
                out.append([(m[0] * x + m[2] * y + dx, m[1] * x + m[3] * y + dy, on) for x, y, on in c])
            if not fl & 0x20:
                break
        return out


def open_face(data, wanted):
    if data[:4] == b"ttcf":
        n = struct.unpack_from(">I", data, 8)[0]
        offs = struct.unpack_from(f">{n}I", data, 12)
    else:
        offs = [0]
    for o in offs:
        f = Face(data, o)
        if f.name() == wanted:
            return f
    raise SystemExit(f"police introuvable : {wanted}")


def flatten(contour, scale, ox, oy):
    """Segments de droite (coordonnées pixel, y vers le bas) d'un contour quadratique."""
    pts = [(x * scale + ox, oy - y * scale, on) for x, y, on in contour]
    if not pts:
        return []
    # point de départ sur la courbe
    if not pts[0][2]:
        if pts[-1][2]:
            pts = [pts[-1]] + pts[:-1]
        else:
            mx, my = (pts[0][0] + pts[-1][0]) / 2, (pts[0][1] + pts[-1][1]) / 2
            pts = [(mx, my, True)] + pts
    segs = []
    cur = pts[0]
    ctrl = None
    seq = pts[1:] + [pts[0]]
    for p in seq:
        if p[2]:
            if ctrl is None:
                segs.append((cur[0], cur[1], p[0], p[1]))
            else:
                segs.extend(quad(cur, ctrl, p))
                ctrl = None
            cur = p
        else:
            if ctrl is not None:
                mid = ((ctrl[0] + p[0]) / 2, (ctrl[1] + p[1]) / 2, True)
                segs.extend(quad(cur, ctrl, mid))
                cur = mid
            ctrl = p
    if ctrl is not None:
        segs.extend(quad(cur, ctrl, pts[0]))
    return segs


def quad(a, c, b):
    dd = abs(a[0] - 2 * c[0] + b[0]) + abs(a[1] - 2 * c[1] + b[1])
    n = max(1, min(24, int(math.sqrt(dd * 4)) + 1))
    out, px, py = [], a[0], a[1]
    for i in range(1, n + 1):
        t = i / n
        u = 1 - t
        x = u * u * a[0] + 2 * u * t * c[0] + t * t * b[0]
        y = u * u * a[1] + 2 * u * t * c[1] + t * t * b[1]
        out.append((px, py, x, y))
        px, py = x, y
    return out


def raster(segs, w, h):
    """Couverture exacte (0..1) par accumulation d'aires signées."""
    W = w + 3
    acc = [0.0] * (W * h + 4)
    for x0, y0, x1, y1 in segs:
        if abs(y0 - y1) < 1e-9:
            continue
        if y0 < y1:
            dirn = 1.0
        else:
            dirn = -1.0
            x0, y0, x1, y1 = x1, y1, x0, y0
        dxdy = (x1 - x0) / (y1 - y0)
        x = x0
        if y0 < 0:
            x -= y0 * dxdy
        for y in range(max(0, int(y0)), min(h, math.ceil(y1))):
            ls = y * W
            dy = min(y + 1, y1) - max(y, y0)
            xn = x + dxdy * dy
            d = dy * dirn
            a, b = (x, xn) if x < xn else (xn, x)
            af = math.floor(a)
            ai = int(af)
            bi = int(math.ceil(b))
            if ai < 0:
                ai, af = 0, 0.0
            if bi <= ai + 1:
                xmf = 0.5 * (x + xn) - af
                acc[ls + ai] += d - d * xmf
                acc[ls + ai + 1] += d * xmf
            else:
                s = 1.0 / (b - a)
                x0f = a - af
                a0 = 0.5 * s * (1 - x0f) * (1 - x0f)
                x1f = b - math.ceil(b) + 1
                am = 0.5 * s * x1f * x1f
                acc[ls + ai] += d * a0
                if bi == ai + 2:
                    acc[ls + ai + 1] += d * (1 - a0 - am)
                else:
                    a1 = s * (1.5 - x0f)
                    acc[ls + ai + 1] += d * (a1 - a0)
                    for xi in range(ai + 2, bi - 1):
                        acc[ls + xi] += d * s
                    a2 = a1 + (bi - ai - 3) * s
                    acc[ls + bi - 1] += d * (1 - a2 - am)
                acc[ls + bi] += d * am
            x = xn
    out = bytearray(w * h)
    for y in range(h):
        s = 0.0
        ls = y * W
        for xx in range(w):
            s += acc[ls + xx]
            v = abs(s)
            if v > 1:
                v = 1
            out[y * w + xx] = int(v * 255 + 0.5)
    return out


def render_glyph(face, gid, px):
    scale = px / face.upem
    cs = face.contours(gid)
    pts = [(x, y) for c in cs for x, y, _ in c]
    adv = face.advance(gid) * scale
    if not pts:
        return 0, 0, 0, 0, b"", adv
    x0 = math.floor(min(p[0] for p in pts) * scale)
    x1 = math.ceil(max(p[0] for p in pts) * scale)
    y0 = math.floor(-max(p[1] for p in pts) * scale)   # haut, y vers le bas
    y1 = math.ceil(-min(p[1] for p in pts) * scale)
    w, h = x1 - x0 + 1, y1 - y0 + 1
    segs = []
    for c in cs:
        segs.extend(flatten(c, scale, -x0, -y0))
    bm = raster(segs, w, h)
    # rognage des bords vides
    rows = [y for y in range(h) if any(bm[y * w:(y + 1) * w])]
    cols = [x for x in range(w) if any(bm[y * w + x] for y in range(h))]
    if not rows:
        return 0, 0, 0, 0, b"", adv
    ry0, ry1, rx0, rx1 = rows[0], rows[-1], cols[0], cols[-1]
    nw, nh = rx1 - rx0 + 1, ry1 - ry0 + 1
    out = bytearray(nw * nh)
    for y in range(nh):
        out[y * nw:(y + 1) * nw] = bm[(ry0 + y) * w + rx0:(ry0 + y) * w + rx0 + nw]
    return x0 + rx0, y0 + ry0, nw, nh, bytes(out), adv


def generate(ttc, specs, out_path):
    """specs : liste de (identifiant C, nom de la police, taille en px, caractères ou None)."""
    lines = ["/* Généré par tools/mkfont.py à partir de la police Inter (licence SIL OFL 1.1). */",
             '#include "font.h"', ""]
    for ident, face_name, px, chars in specs:
        face = open_face(ttc, face_name)
        chars = chars or CHARSET
        glyphs, bits = [], bytearray()
        for cp in sorted(set(chars)):
            gid = face.cmap.get(cp)
            if gid is None:
                continue
            ox, oy, w, h, bm, adv = render_glyph(face, gid, px)
            glyphs.append((cp, len(bits), w, h, ox, oy, int(round(adv * 64))))
            bits += bm
        scale = px / face.upem
        asc = int(round(face.ascent * scale))
        desc = int(round(-face.descent * scale))
        lines.append(f"static const uint8_t {ident}_bits[{max(1, len(bits))}] = {{")
        for i in range(0, len(bits), 24):
            lines.append("  " + ",".join(str(b) for b in bits[i:i + 24]) + ",")
        lines.append("};")
        lines.append(f"static const glyph_t {ident}_glyphs[{len(glyphs)}] = {{")
        for cp, off, w, h, ox, oy, adv in glyphs:
            lines.append(f"  {{{off},{cp},{w},{h},{ox},{oy},{adv}}},")
        lines.append("};")
        lines.append(f"const font_t {ident} = {{ {ident}_glyphs, {len(glyphs)}, {ident}_bits, "
                     f"{px}, {asc}, {desc}, {asc + desc + int(round(face.gap * scale))} }};")
        lines.append("")
        print(f"  police {ident:12} {face_name:24} {px:3d}px  {len(glyphs)} glyphes, {len(bits) // 1024} Ko")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
