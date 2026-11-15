#!/usr/bin/env python3
"""Sonicology Rack: makes the view's art and font (plugin/ui/rack/rack_*) from the user's originals in docs/rack/art.

Trims each picture to what it shows, sizes it for a 2x screen at the Rack's design size, splits the knob into a
still ring and a turning cap, cuts the old subtitle off the lettering, saves WebP. Run from the repo root:
    python3 tools/rack/make_art.py
Needs Pillow with WebP. The output is the same in all three plug-ins (plugin/ui/rack/ is shared)."""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageChops

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
    # (04_honeycomb_tile.png is not used: the honeycomb backgrounds are made from the concept art, see honeycombs())
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

    honeycombs()

    # The font: Barlow Semi Condensed (SIL Open Font License, from Mixology's fonts), under rack_ names.
    fonts = os.path.join(ROOT, "plugin", "ui", "fonts")
    for src, dst in (("BarlowSemiCondensed-Medium.ttf", "rack_barlowsc-medium.ttf"),
                     ("BarlowSemiCondensed-SemiBold.ttf", "rack_barlowsc-semibold.ttf"),
                     ("BarlowSemiCondensed-Bold.ttf", "rack_barlowsc-bold.ttf"),
                     ("OFL-BarlowSemiCondensed.txt", "rack_OFL-BarlowSemiCondensed.txt")):
        with open(os.path.join(fonts, src), "rb") as f, open(os.path.join(OUT, dst), "wb") as g:
            g.write(f.read())
        print(dst)


def honeycombs():
    """The honeycomb backgrounds, made to match the concept art: blue-grey glass cells with glowing gold walls.
    The glass is the concept's own: the cells of the clean honeycomb under the left column's list in
    docs/rack/concepts/rack-coach.png, each cut out inside its walls. They fill a perfectly regular lattice (pointy-top
    cells, 2x the concept's size) whose tile repeats without a seam; the gold walls are drawn over them in the concept's
    colours. Three versions:
      glass: as in the concept (the left column, the Chain Flow strip, the Modules page's strips)
      dark:  the gold walls on navy glass (the honeycomb behind the logo)
      ivory: faint light lines on ivory (the knob panel)"""
    import random
    src = Image.open(os.path.join(ROOT, "docs", "rack", "concepts", "rack-coach.png")).convert("RGB")
    # the concept's cell centres (x, y), measured from its walls (its cells: about 46 px wide, radius about 27)
    centres = [(43, 833), (88, 833), (135, 833), (185, 833),
               (67, 876), (111, 876), (160, 876), (209, 876),
               (47, 916), (89, 916), (136, 916), (187, 916)]
    R = 53.0                             # the tile's cell radius (centre to corner), 2x the concept's
    W = math.sqrt(3) * R                 # a cell's width (91.8)
    WALL = 9.0                           # the gold wall's width, 2x
    cols, pairs = 4, 2
    tw, th = round(cols * W), round(pairs * 3 * R)   # repeats without a seam (to a fraction of a pixel)
    big = 3                              # drawn at 3x, then shrunk (smooth walls)

    def hexagon(cx, cy, r):
        return [(cx + r * math.cos(math.radians(60 * k + 30)), cy + r * math.sin(math.radians(60 * k + 30))) for k in range(6)]

    cut = 20                             # a concept cell's glass, inside its walls (corner radius in the concept)
    inner = (R - WALL / 2 + 1.0) * big   # where the glass goes in the tile (corner radius, 3x)
    cells = [src.crop((x - cut, y - cut, x + cut, y + cut)).resize((round(2 * inner), round(2 * inner)), Image.LANCZOS) for (x, y) in centres]
    mask = Image.new("L", cells[0].size, 0)
    ImageDraw.Draw(mask).polygon(hexagon(inner, inner, inner), fill=255)

    rnd = random.Random(11)
    canvas = Image.new("RGB", (round(tw * big), round(th * big)), (120, 84, 30))
    walls = Image.new("L", canvas.size, 0)
    dw = ImageDraw.Draw(walls)
    picks = {}
    for row in range(-1, pairs * 2 + 1):
        for col in range(-1, cols + 1):
            key = (col % cols, row % (pairs * 2))
            if key not in picks:
                picks[key] = rnd.randrange(len(cells))
            cx = col * W + (W / 2 if row % 2 else 0)
            cy = row * 1.5 * R
            canvas.paste(cells[picks[key]], (round(cx * big - inner), round(cy * big - inner)), mask)
            pts = [(px * big, py * big) for (px, py) in hexagon(cx, cy, R)]
            dw.line(pts + [pts[0]], fill=255, width=round(WALL * big), joint="curve")
    glow = walls.filter(ImageFilter.GaussianBlur(5 * big))
    soft = walls.filter(ImageFilter.GaussianBlur(1.0 * big))
    ridge = walls.filter(ImageFilter.MinFilter(round(WALL * 0.45 * big) | 1)).filter(ImageFilter.GaussianBlur(1.2 * big))
    shine = walls.filter(ImageFilter.MinFilter(round(WALL * 0.62 * big) | 1)).filter(ImageFilter.GaussianBlur(0.9 * big))

    def layer(colour):
        return Image.new("RGB", canvas.size, colour)
    glass = Image.composite(layer((255, 196, 84)), canvas, glow.point(lambda v: v * 0.46))   # the warm glow on the glass
    glass = Image.composite(layer((150, 92, 18)), glass, soft)                                 # the wall's darker edge
    glass = Image.composite(layer((246, 182, 58)), glass, ridge)                               # gold
    glass = Image.composite(layer((255, 238, 168)), glass, shine)                              # its bright middle
    glass = glass.resize((tw, th), Image.LANCZOS).filter(ImageFilter.UnsharpMask(radius=1.0, percent=40, threshold=2))
    save_rgb(glass, "honey_glass.webp")

    wall = soft.resize((tw, th), Image.LANCZOS)
    wall_glow = glow.resize((tw, th), Image.LANCZOS)
    lum = glass.convert("L")
    navy = Image.merge("RGB", (lum.point(lambda v: 5 + v * 0.07), lum.point(lambda v: 12 + v * 0.10), lum.point(lambda v: 30 + v * 0.20)))
    navy = Image.composite(Image.new("RGB", (tw, th), (150, 96, 24)), navy, wall_glow.point(lambda v: v * 0.30))
    save_rgb(Image.composite(glass, navy, wall), "honey_dark.webp")

    # the knob panel's lines are thinner than the glass honeycomb's walls
    wall = wall.filter(ImageFilter.MinFilter(5)).filter(ImageFilter.GaussianBlur(0.6))
    wall_glow = wall.filter(ImageFilter.GaussianBlur(3.5))
    ivory = Image.merge("RGB", (lum.point(lambda v: 240 + (v - 180) * 0.10), lum.point(lambda v: 231 + (v - 180) * 0.10), lum.point(lambda v: 211 + (v - 180) * 0.08)))
    ivory = Image.composite(Image.new("RGB", (tw, th), (198, 178, 136)), ivory, wall_glow.point(lambda v: v * 0.62))
    ivory = Image.composite(Image.new("RGB", (tw, th), (255, 253, 247)), ivory, wall.point(lambda v: v * 0.95))
    save_rgb(ivory, "honey_ivory.webp")


def save_rgb(im, name, quality=86):
    name = "rack_" + name
    path = os.path.join(OUT, name)
    im.save(path, "WEBP", quality=quality, method=6)
    print(f"{name}: {im.size[0]} x {im.size[1]}, {os.path.getsize(path) // 1024} KB")


if __name__ == "__main__":
    sys.exit(main())
