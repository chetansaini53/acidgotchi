#!/usr/bin/env python3
"""
gen_emotions.py - turn the source emotion GIFs into the C header the firmware eats.

Reads  acidgotchi/assets/<emotion>.gif
Writes acidgotchi/emotions.h  (RGB565 frame arrays + EMOS[] table)

The ESP32-C6 has no spare RAM/flash for a GIF decoder, so each animation is
baked at build time: every GIF is coalesced, down-sampled to 8 frames, fit into
a 132x110 box (aspect-preserved, floor-scaled) and emitted as flat uint16_t
RGB565 arrays that acidgotchi.ino blits straight with draw16bitRGBBitmap().

Swap the pet's face: drop your own <emotion>.gif files in acidgotchi/assets/
(same five names) and re-run this script. No other change needed.

Deps: Pillow  ->  pip install pillow
Usage: python tools/gen_emotions.py [--assets DIR] [--out FILE]
"""
import argparse
import os
import sys

try:
    from PIL import Image, ImageSequence
except ImportError:
    sys.exit("Pillow missing. Install it:  pip install pillow")

# emit order MUST match the enum the firmware uses:
#   enum { E_HAPPY, E_NEEDATT, E_OIDEKHO, E_SAD, E_EATING, E_COUNT };
EMOTIONS = ["happy", "needatt", "oidekho", "sad", "eating"]

MAX_W, MAX_H = 132, 110   # pet draw box on the 172x320 panel
FRAMES = 8                # frames kept per emotion
DELAY = 100               # ms per frame (firmware animation speed)

HERE = os.path.dirname(os.path.abspath(__file__))
DEF_ASSETS = os.path.normpath(os.path.join(HERE, "..", "acidgotchi", "assets"))
DEF_OUT = os.path.normpath(os.path.join(HERE, "..", "acidgotchi", "emotions.h"))


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def pick_indices(total, want):
    """Evenly sample `want` frame indices out of `total` available frames."""
    if total <= 1:
        return [0] * want
    return [round(i * (total - 1) / (want - 1)) for i in range(want)]


def load_frames(path):
    """Return every GIF frame flattened onto white, in RGBA->RGB."""
    im = Image.open(path)
    out = []
    for frame in ImageSequence.Iterator(im):
        rgba = frame.convert("RGBA")
        bg = Image.new("RGBA", rgba.size, (255, 255, 255, 255))
        out.append(Image.alpha_composite(bg, rgba).convert("RGB"))
    return out


def scaled_size(w, h):
    s = min(MAX_W / w, MAX_H / h)
    return max(1, int(w * s)), max(1, int(h * s))


def frame_to_565(img):
    w, h = scaled_size(*img.size)
    img = img.resize((w, h), Image.LANCZOS)
    px = img.load()
    data = [rgb565(*px[x, y]) for y in range(h) for x in range(w)]
    return w, h, data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", default=DEF_ASSETS, help="folder with <emotion>.gif files")
    ap.add_argument("--out", default=DEF_OUT, help="emotions.h path to write")
    args = ap.parse_args()

    for name in EMOTIONS:
        if not os.path.isfile(os.path.join(args.assets, name + ".gif")):
            sys.exit(f"missing {name}.gif in {args.assets}")

    lines = [
        "// auto-generated emotion frames - DO NOT edit by hand.",
        "// regenerate via:  python tools/gen_emotions.py   (reads acidgotchi/assets/*.gif)",
        "#pragma once",
        "",
    ]
    meta = []  # (name, w, h)
    for name in EMOTIONS:
        frames = load_frames(os.path.join(args.assets, name + ".gif"))
        idxs = pick_indices(len(frames), FRAMES)
        w = h = 0
        for fi, src_i in enumerate(idxs):
            w, h, data = frame_to_565(frames[src_i])
            body = ",".join(str(v) for v in data)
            lines.append(f"static const uint16_t {name}_{fi}[{w * h}] = {{{body}}};")
        ptrs = ",".join(f"{name}_{i}" for i in range(FRAMES))
        lines.append(f"static const uint16_t* {name}_fr[{FRAMES}] = {{{ptrs}}};")
        lines.append("")
        meta.append((name, w, h))

    lines.append("struct Emo { const uint16_t** fr; int w,h,n,delay; };")
    lines.append("enum { " + ",".join("E_" + n.upper() for n in EMOTIONS) + ",E_COUNT };")
    lines.append("static const Emo EMOS[E_COUNT] = {")
    for name, w, h in meta:
        lines.append(f"  {{{name}_fr,{w},{h},{FRAMES},{DELAY}}},")
    lines.append("};")

    with open(args.out, "w", encoding="ascii") as f:
        f.write("\n".join(lines) + "\n")

    kb = os.path.getsize(args.out) // 1024
    print("wrote", args.out, f"({kb} KB)")
    for name, w, h in meta:
        print(f"  {name:8s} {w}x{h} x{FRAMES}")


if __name__ == "__main__":
    main()
