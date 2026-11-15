#!/usr/bin/env python3
"""Sonicology Rack: makes the view's art and font (plugin/ui/rack/rack_*) from the user's originals in docs/rack/art.

Trims each picture to what it shows, sizes it for a 2x screen at the Rack's design size, splits the knob into a
still ring and a turning cap, cuts the old subtitle off the lettering, saves WebP. Run from the repo root:
    python3 tools/rack/make_art.py
Needs Pillow with WebP. The output is the same in all three plug-ins (plugin/ui/rack/ is shared)."""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "docs", "rack", "art")
OUT = os.path.join(ROOT, "plugin", "ui", "rack")


def load(name):
    return Image.open(os.path.join(SRC, name)).convert("RGBA")


def trim(im, box=None):
    if box is not None:
        im = im.crop(box)
    bbox = im.getchannel("A").point(lambda v: 255 if v > 6 else 0).getbbox()
    return im.crop(bbox)


def fit(im, longest):
    s = longest / max(im.size)
    if s >= 1.0:
        return im
    return im.resize((max(1, round(im.size[0] * s)), max(1, round(im.size[1] * s))), Image.LANCZOS)


def save(im, name, quality=88):
    name = "rack_" + name   # the plug-ins serve their files flat, by file name: every Rack file starts with rack_
    path = os.path.join(OUT, name)
    im.save(path, "WEBP", quality=quality, method=6)
    print(f"{name}: {im.size[0]} x {im.size[1]}, {os.path.getsize(path) // 1024} KB")


def square(im):
    """Pads to a square, centred (the knob's two parts must share one centre)."""
    n = max(im.size)
    sq = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    sq.alpha_composite(im, ((n - im.size[0]) // 2, (n - im.size[1]) // 2))
    return sq


def main():
    os.makedirs(OUT, exist_ok=True)

    save(fit(trim(load("01_sonicology_rack_logo_emblem.png")), 420), "emblem.webp")

    # The title without its old subtitle (rows 419 - 479 of the original); the new subtitle is text.
    save(fit(trim(load("02_sonicology_rack_lettering.png"), (0, 0, 2172, 417)), 1400), "lettering.webp")

    save(fit(trim(load("03_red_blue_header_art.png")), 2172), "bar.webp", quality=90)
    save(fit(trim(load("04_honeycomb_tile.png")), 220), "hex_cream.webp")
    save(fit(trim(load("06_hex_cell.png")), 260), "hex_navy.webp")
    save(fit(trim(load("07_frame_corner.png")), 260), "corner.webp")

    # Knob: the cap is the gold disc with its pointer (inside the dark ring, about 0.66 of the radius); it turns.
    # The ring (bezel, tabs, blue ring) stays still. Both on the same square canvas, so they share a centre.
    knob = square(trim(load("05_gold_knob.png")))
    n = knob.size[0]
    r_cap = 0.664 * n / 2   # measured: the dark ring starts at ~408 px of a 1191 px knob
    mask = Image.new("L", (n * 4, n * 4), 0)
    ImageDraw.Draw(mask).ellipse((n * 2 - r_cap * 4, n * 2 - r_cap * 4, n * 2 + r_cap * 4, n * 2 + r_cap * 4), fill=255)
    mask = mask.resize((n, n), Image.LANCZOS)
    cap = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    cap.paste(knob, (0, 0), mask)
    save(fit(cap, 300), "knob_cap.webp")
    ring = knob.copy()
    save(fit(ring, 300), "knob_ring.webp")

    # The font: Barlow Semi Condensed (SIL Open Font License, from Mixology's fonts), under rack_ names.
    fonts = os.path.join(ROOT, "plugin", "ui", "fonts")
    for src, dst in (("BarlowSemiCondensed-Medium.ttf", "rack_barlowsc-medium.ttf"),
                     ("BarlowSemiCondensed-SemiBold.ttf", "rack_barlowsc-semibold.ttf"),
                     ("BarlowSemiCondensed-Bold.ttf", "rack_barlowsc-bold.ttf"),
                     ("OFL-BarlowSemiCondensed.txt", "rack_OFL-BarlowSemiCondensed.txt")):
        with open(os.path.join(fonts, src), "rb") as f, open(os.path.join(OUT, dst), "wb") as g:
            g.write(f.read())
        print(dst)


if __name__ == "__main__":
    sys.exit(main())
