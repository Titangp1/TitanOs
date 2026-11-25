#!/usr/bin/env python3
"""Compile TitanOs (base Linux) et produit l'image amorçable.

Prérequis : LLVM (clang, ld.lld, lld-link) et Python 3.14 ou plus. Rien d'autre :
le noyau Linux et BusyBox sont téléchargés une fois depuis Alpine Linux, vérifiés,
puis gardés dans build/cache/.

    python build.py            tout compiler -> build/titanos.img (+ .vhd)
    python build.py preview    aperçu du bureau sous Windows -> build/preview/*.png
    python build.py clean      supprimer build/ (sauf le cache des téléchargements)
"""
import gzip
import os
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "build")
GEN = os.path.join(OUT, "gen")
CACHE = os.path.join(OUT, "cache")
sys.path.insert(0, os.path.join(ROOT, "tools"))

CORE = ["lib.c", "gfx.c", "glass.c", "icons.c", "settings.c", "sound.c", "wm.c", "ui.c", "apps.c", "term.c", "files.c",
        "notes.c", "prefs.c", "sysapp.c", "netapp.c", "updapp.c", "png.c", "jpeg.c", "imgview.c", "browser.c", "store.c", "setup.c"]
LINUX = ["main.c", "sys.c", "modules.c", "display.c", "input.c", "plat.c", "audio.c", "wifi.c", "pcinames.c", "update.c", "volumes.c", "xbridge.c", "sudo.c", "nm.c"]

# polices générées : (nom C, police Inter, taille en pixels, caractères ou None = tous)
FONTS = [
    ("font_small", "Inter Regular", 12, None),
    ("font_body", "Inter Regular", 14, None),
    ("font_bold", "Inter SemiBold", 14, None),
    ("font_title", "Inter SemiBold", 17, None),
    ("font_h2", "Inter Display SemiBold", 24, None),
    ("font_h1", "Inter Display SemiBold", 36, None),
    ("font_clock", "Inter Display SemiBold", 128, [ord(c) for c in "0123456789:"]),
]

CFLAGS = ["-std=c11", "-O2", "-ffreestanding", "-fno-stack-protector", "-fno-math-errno", "-msse2",
          "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-missing-field-initializers",
          "-Wno-sign-compare", "-I", os.path.join(ROOT, "src", "core"), "-I", GEN]


def find_tool(name):
    path = shutil.which(name)
    if path:
        return path
    for d in (r"C:\Program Files\LLVM\bin", "/usr/bin", "/usr/local/opt/llvm/bin", "/opt/homebrew/opt/llvm/bin"):
        for cand in (os.path.join(d, name + ".exe"), os.path.join(d, name)):
            if os.path.isfile(cand):
                return cand
    sys.exit(f"outil introuvable : {name} (installez LLVM : winget install LLVM.LLVM)")


def run(cmd, quiet=False):
    if not quiet:
        print("  " + os.path.basename(cmd[0]) + " " + " ".join(
            os.path.relpath(c, ROOT) if os.path.isabs(c) and c.startswith(ROOT) else c for c in cmd[1:])[:160])
    r = subprocess.run(cmd)
    if r.returncode:
        sys.exit(r.returncode)


def newer(target, *sources):
    if not os.path.exists(target):
        return True
    t = os.path.getmtime(target)
    return any(os.path.getmtime(s) > t for s in sources)


def alpine_repo():
    import alpine
    repo = alpine.Repo(CACHE)
    repo.load_index("main")
    repo.load_index("community")
    return repo


def gen_sources(repo):
    """Polices : Inter (téléchargée) et la police 8x16 maison."""
    os.makedirs(GEN, exist_ok=True)
    fonts_c = os.path.join(GEN, "fonts.c")
    if newer(fonts_c, os.path.join(ROOT, "tools", "mkfont.py"), __file__):
        print("[polices]")
        import mkfont
        ttc = repo.fetch("font-inter")["usr/share/fonts/inter/Inter.ttc"][1]
        mkfont.generate(ttc, FONTS, fonts_c)
    mono_c = os.path.join(GEN, "font8x16.c")
    if newer(mono_c, os.path.join(ROOT, "tools", "fontgen.py")):
        import fontgen
        fontgen.write_c(fontgen.build(), mono_c)
    return [fonts_c, mono_c]


def compile_all(clang, sources, objdir, flags):
    os.makedirs(objdir, exist_ok=True)
    objs = []
    headers = [os.path.join(ROOT, "src", d, h) for d in ("core", "linux")
               for h in os.listdir(os.path.join(ROOT, "src", d)) if h.endswith(".h")]
    for src in sources:
        obj = os.path.join(objdir, os.path.basename(src).replace(".c", ".o"))
        if newer(obj, src, *headers, __file__):
            run([clang, *flags, "-c", src, "-o", obj])
        objs.append(obj)
    return objs


# ---------------------------------------------------------------- aperçu Windows
def build_preview(repo):
    clang, lld_link = find_tool("clang"), find_tool("lld-link")
    gen = gen_sources(repo)
    print("[aperçu : bureau compilé pour Windows]")
    flags = ["--target=x86_64-pc-windows-msvc", "-mno-stack-arg-probe", "-fno-ms-extensions", *CFLAGS]
    srcs = [os.path.join(ROOT, "src", "core", s) for s in CORE] + gen + [os.path.join(ROOT, "src", "sim", "sim.c")]
    objs = compile_all(clang, srcs, os.path.join(OUT, "obj-sim"), flags)
    dll = os.path.join(OUT, "preview", "titansim.dll")
    os.makedirs(os.path.dirname(dll), exist_ok=True)
    run([lld_link, "/dll", "/noentry", "/nodefaultlib", "/nologo", f"/out:{dll}", *objs])
    import preview
    preview.render_all(dll, os.path.join(OUT, "preview"))


def main():
    sys.stdout.reconfigure(line_buffering=True)
    arg = sys.argv[1] if len(sys.argv) > 1 else "all"
    if arg == "clean":
        for d in os.listdir(OUT) if os.path.isdir(OUT) else []:
            if d != "cache":
                p = os.path.join(OUT, d)
                shutil.rmtree(p) if os.path.isdir(p) else os.remove(p)
        print("build/ nettoyé (cache conservé)")
        return
    repo = alpine_repo()
    if arg == "preview":
        build_preview(repo)
        return
    import buildos
    buildos.build(repo, sys.modules[__name__])


if __name__ == "__main__":
    main()
