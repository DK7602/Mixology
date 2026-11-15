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


def save(im, name, quality=90):
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

    # Sizes: big enough for the largest window (1.7x the design size) on a 2x (Retina / 4K) screen
    save(fit(trim(load("01_sonicology_rack_logo_emblem.png")), 860), "emblem.webp", quality=94)

    # The title without its old subtitle (rows 419 - 479 of the original); the new subtitle is text.
    save(fit(trim(load("02_sonicology_rack_lettering.png"), (0, 0, 2172, 417)), 2172), "lettering.webp", quality=92)

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
    save(fit(cap, 400), "knob_cap.webp", quality=92)
    ring = knob.copy()
    save(fit(ring, 400), "knob_ring.webp", quality=92)

    honeycombs()
    liquids()

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
      ivory: thin gold lines on ivory (the knob panel)"""
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
    OUTK = 2                             # the tiles are saved at 2x (sharp in big windows on a 2x screen)
    big = 3 * OUTK                       # drawn at 3x that, then shrunk (smooth walls)

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
    thin = Image.new("L", canvas.size, 0)     # the knob panel's thinner gold lines, the same lattice
    dt = ImageDraw.Draw(thin)
    THIN = 4.6
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
            dt.line(pts + [pts[0]], fill=255, width=round(THIN * big), joint="curve")
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
    glass = glass.resize((tw * OUTK, th * OUTK), Image.LANCZOS).filter(ImageFilter.UnsharpMask(radius=2.0, percent=40, threshold=2))
    save_rgb(glass, "honey_glass.webp")

    wall = soft.resize((tw * OUTK, th * OUTK), Image.LANCZOS)
    wall_glow = glow.resize((tw * OUTK, th * OUTK), Image.LANCZOS)
    lum = glass.convert("L")
    navy = Image.merge("RGB", (lum.point(lambda v: 5 + v * 0.07), lum.point(lambda v: 12 + v * 0.10), lum.point(lambda v: 30 + v * 0.20)))
    navy = Image.composite(Image.new("RGB", (tw * OUTK, th * OUTK), (150, 96, 24)), navy, wall_glow.point(lambda v: v * 0.30))
    save_rgb(Image.composite(glass, navy, wall), "honey_dark.webp")

    # the light view: pearly white cells (a faint pink / blue / gold sheen, from the glass's own shading) in the same
    # gold walls
    pearl = Image.merge("RGB", (lum.point(lambda v: 246 + (v - 170) * 0.07), lum.point(lambda v: 242 + (v - 170) * 0.07), lum.point(lambda v: 236 + (v - 170) * 0.08)))
    sheen = Image.linear_gradient("L").resize((tw * OUTK, th * OUTK)).rotate(35, expand=False)
    pearl = Image.composite(Image.new("RGB", (tw * OUTK, th * OUTK), (250, 236, 240)), pearl, sheen.point(lambda v: v * 0.35))
    pearl = Image.composite(Image.new("RGB", (tw * OUTK, th * OUTK), (232, 242, 250)), pearl, sheen.point(lambda v: (255 - v) * 0.30))
    pearl = Image.composite(Image.new("RGB", (tw * OUTK, th * OUTK), (246, 214, 140)), pearl, wall_glow.point(lambda v: v * 0.35))
    save_rgb(Image.composite(glass, pearl, wall), "honey_pearl.webp")

    # the knob panel: pearly ivory cells framed by thin lines of real-looking gold (the same layers as the glass
    # honeycomb's walls: a darker edge, gold, a bright middle, a warm glow), as the concept's knob panel
    cells_lum = canvas.convert("L")
    ivory = Image.merge("RGB", (cells_lum.point(lambda v: 242 + (v - 180) * 0.10), cells_lum.point(lambda v: 233 + (v - 180) * 0.10), cells_lum.point(lambda v: 214 + (v - 180) * 0.08)))
    t_glow = thin.filter(ImageFilter.GaussianBlur(3 * big))
    t_soft = thin.filter(ImageFilter.GaussianBlur(0.6 * big))
    t_ridge = thin.filter(ImageFilter.MinFilter(round(THIN * 0.40 * big) | 1)).filter(ImageFilter.GaussianBlur(0.6 * big))
    t_shine = thin.filter(ImageFilter.MinFilter(round(THIN * 0.70 * big) | 1)).filter(ImageFilter.GaussianBlur(0.5 * big))
    ivory = Image.composite(layer((236, 196, 116)), ivory, t_glow.point(lambda v: v * 0.40))
    ivory = Image.composite(layer((138, 84, 14)), ivory, t_soft)
    ivory = Image.composite(layer((232, 166, 46)), ivory, t_ridge)
    ivory = Image.composite(layer((255, 238, 166)), ivory, t_shine)
    # the concept's ivory haze over the gold: the lines show through a pearly film, softer and paler
    plain = Image.merge("RGB", (cells_lum.point(lambda v: 244 + (v - 180) * 0.16), cells_lum.point(lambda v: 236 + (v - 180) * 0.16), cells_lum.point(lambda v: 219 + (v - 180) * 0.14)))
    plain = Image.composite(layer((240, 214, 156)), plain, t_glow.point(lambda v: v * 0.30))   # the warm glow stays
    ivory = Image.blend(plain, ivory.filter(ImageFilter.GaussianBlur(0.5 * big)), 0.66)
    ivory = Image.composite(layer((255, 246, 214)), ivory, t_shine.point(lambda v: v * 0.35))  # the lines' bright shine
    ivory = ivory.resize((tw * OUTK, th * OUTK), Image.LANCZOS)
    save_rgb(ivory, "honey_ivory.webp")


def pearl_hex(im):
    """The hex cell for the light view: its navy glass turned to pearl, the gold frame kept."""
    r, g, b, a = im.split()
    gold = ImageChops.subtract(r, b).point(lambda v: max(0, min(255, (v - 25) * 4)))   # the gold frame (and red glow)
    lum = im.convert("L")
    blue = ImageChops.multiply(ImageChops.subtract(b, r).point(lambda v: max(0, min(255, (v - 70) * 3))), lum.point(lambda v: max(0, min(255, (v - 60) * 3))))
    gold = ImageChops.lighter(gold, blue)                                                 # ... and the blue glow
    w, h = im.size
    base = Image.merge("RGB", (lum.point(lambda v: min(255, 244 + v * 0.05)), lum.point(lambda v: min(255, 240 + v * 0.05)), lum.point(lambda v: min(255, 232 + v * 0.06))))
    shade = Image.radial_gradient("L").resize((w, h))            # brighter in the middle, a pearly edge
    base = Image.composite(Image.new("RGB", (w, h), (226, 214, 196)), base, shade.point(lambda v: max(0, v - 120) * 0.9))
    sheen = Image.linear_gradient("L").resize((w, h)).rotate(30)
    base = Image.composite(Image.new("RGB", (w, h), (242, 228, 236)), base, sheen.point(lambda v: v * 0.25))
    out = Image.composite(im.convert("RGB"), base, gold)
    out.putalpha(a)
    return out


def liquids():
    """The red / blue liquid glass that runs along the header (the concept: a wave under the logo flowing into the
    button bar, a swirl behind the track picker at the top right). Cut from the user's header art
    (03_red_blue_header_art.png): its bottom-left and top-right waves, keeping only the liquid itself (bright or
    strongly coloured pixels), not the dark panel inside the bar."""
    bar = load("03_red_blue_header_art.png")

    def liquid_only(im):
        r, g, b, a = im.split()
        hi = ImageChops.lighter(ImageChops.lighter(r, g), b)
        lo = ImageChops.darker(ImageChops.darker(r, g), b)
        sat = ImageChops.subtract(hi, lo)
        keep = ImageChops.lighter(sat.point(lambda v: max(0, min(255, (v - 50) * 3))), hi.point(lambda v: max(0, min(255, (v - 110) * 3))))
        return Image.merge("RGBA", (r, g, b, ImageChops.multiply(a, keep.filter(ImageFilter.GaussianBlur(1.2)))))

    # the light view's bar: the same chrome edges, a pearly white inside
    lq = liquid_only(bar)
    w, h = bar.size
    fill = Image.merge("RGB", tuple(Image.linear_gradient("L").resize((w, h)).point(lambda v, c=c: c[0] + (c[1] - c[0]) * v / 255) for c in ((255, 236), (253, 226), (247, 204))))
    sheen = Image.linear_gradient("L").rotate(90).resize((w, h))
    fill = Image.composite(Image.new("RGB", (w, h), (246, 236, 244)), fill, sheen.point(lambda v: v * 0.25))
    # the dark panel inside the chrome: its dark pixels, closed into one shape; outside it the chrome stays as it is
    lum = bar.convert("L")
    dark = ImageChops.multiply(lum.point(lambda v: 255 if v < 46 else 0), bar.getchannel("A").point(lambda v: 255 if v > 200 else 0))
    inside = dark.filter(ImageFilter.MaxFilter(15)).filter(ImageFilter.MinFilter(15)).filter(ImageFilter.GaussianBlur(3))
    pearl = bar.copy()
    pearl.paste(fill, (0, 0), inside)
    glow = ImageChops.multiply(lq.getchannel("A"), inside)
    pearl.paste(bar.convert("RGB"), (0, 0), glow)                # the liquid glints over the panel stay
    save(fit(trim(pearl), 2172), "bar_pearl.webp")

    save(fit(trim(liquid_only(bar.crop((0, 300, 760, 520)))), 760), "liquid_left.webp")
    save(fit(trim(liquid_only(bar.crop((1000, 140, 2172, 330)))), 1172), "liquid_right.webp")


def save_rgb(im, name, quality=86):
    name = "rack_" + name
    path = os.path.join(OUT, name)
    im.save(path, "WEBP", quality=quality, method=6)
    print(f"{name}: {im.size[0]} x {im.size[1]}, {os.path.getsize(path) // 1024} KB")


if __name__ == "__main__":
    sys.exit(main())
