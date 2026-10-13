"""Cuts the interface artwork out of the approved Mixology mockups.

Usage:  python3 -I tools/art/extract_assets.py <Images folder from the handoff zip> <output folder>
Writes: logo.png      beaker + atom + waveforms, transparent (glow kept as alpha: looks the same on dark)
        wordmark.png  chrome MIXOLOGY letters, transparent, solid metal (the dark look)
        (wordmark_light.png, the light look's title, is the user's own art, not cut from the mockups)
        gears.jpg     the gear texture from the header's right side
        scene_*.jpg   the seven Space pictures (reverb, delay, chorus, phaser, hall, room, special)

Only Pillow + NumPy. Re-run when better (higher resolution) art arrives; coordinates are for the
1448 x 1086 mockups."""

import sys
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

SRC = Path(sys.argv[1])
OUT = Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)


def load(name):
    return np.asarray(Image.open(SRC / name).convert("RGB")).astype(np.float32) / 255.0


def save_rgba(rgb, alpha, name):
    rgba = np.dstack([np.clip(rgb, 0, 1), np.clip(alpha, 0, 1)])
    Image.fromarray((rgba * 255 + 0.5).astype(np.uint8), "RGBA").save(OUT / name, optimize=True)


# --------------------------------------------------------------------------------------------
# Logo: light-on-black art -> "unscreen" (alpha = brightness above the background floor).
# Composited over a dark background it reproduces the original; the glow stays a soft glow.

def logo():
    img = load("12_Dynamics_Closeup.jpg")[20:232, 24:408]
    peak = img.max(axis=2)
    floor = 0.13                                 # dark metal behind the logo disappears below this
    a = np.clip((peak - floor) / (1.0 - floor), 0, 1)
    a = a ** 0.85                                # keep the glass body a little more solid
    rgb = np.where(a[..., None] > 1e-3, img / np.maximum(a[..., None], 1e-3), 0)   # un-premultiply against black
    # Soft fade at the left / right ends so the waveforms trail off instead of being cut.
    h, w = a.shape
    x = np.arange(w, dtype=np.float32)
    fade = np.clip(np.minimum(x / 26.0, (w - 1 - x) / 18.0), 0, 1)
    y = np.arange(h, dtype=np.float32)
    vfade = np.clip(np.minimum(y / 6.0, (h - 1 - y) / 6.0), 0, 1)
    a = a * fade[None, :] * vfade[:, None]
    save_rgba(rgb, a, "logo.png")


# --------------------------------------------------------------------------------------------
# Wordmark: chrome letters with dark reflections inside. Background = dark pixels connected to the
# border; small enclosed dark areas are reflections (kept), big ones are counters of O / G (removed).

def flood_from_border(dark):
    h, w = dark.shape
    seen = np.zeros_like(dark)
    q = deque()
    for x in range(w):
        for y in (0, h - 1):
            if dark[y, x] and not seen[y, x]:
                seen[y, x] = True
                q.append((y, x))
    for y in range(h):
        for x in (0, w - 1):
            if dark[y, x] and not seen[y, x]:
                seen[y, x] = True
                q.append((y, x))
    while q:
        y, x = q.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < h and 0 <= nx < w and dark[ny, nx] and not seen[ny, nx]:
                seen[ny, nx] = True
                q.append((ny, nx))
    return seen


def components(mask):
    h, w = mask.shape
    label = np.zeros((h, w), np.int32)
    sizes = [0]
    for sy in range(h):
        for sx in range(w):
            if not mask[sy, sx] or label[sy, sx]:
                continue
            n = len(sizes)
            q = deque([(sy, sx)])
            label[sy, sx] = n
            count = 0
            while q:
                y, x = q.popleft()
                count += 1
                for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not label[ny, nx]:
                        label[ny, nx] = n
                        q.append((ny, nx))
            sizes.append(count)
    return label, np.array(sizes)


def wordmark():
    full = load("12_Dynamics_Closeup.jpg")
    y0, y1, x0, x1 = 26, 142, 402, 1206
    img = full[y0:y1, x0:x1].copy()
    # The red waveform right of the Y is not part of the letters.
    img[75 - y0:, 1166 - x0:] = 0

    peak = img.max(axis=2)
    sat = peak - img.min(axis=2)
    dark = (peak < 0.20) | ((sat > 0.45) & (peak < 0.55))   # near-black, or the coloured glow around the letters
    background = flood_from_border(dark)
    inner = dark & ~background
    label, sizes = components(inner)
    counters = np.isin(label, np.nonzero(sizes > 140)[0]) & (label > 0)
    mask = (~background & ~counters).astype(np.float32)

    # Drop specks of glow that aren't attached to a letter.
    solid, solid_sizes = components(mask > 0.5)
    mask = (np.isin(solid, np.nonzero(solid_sizes > 160)[0]) & (solid > 0)).astype(np.float32)

    alpha = np.asarray(Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6))) / 255.0
    alpha = np.clip(alpha * 1.15, 0, 1)
    # Trim empty margins.
    ys, xs = np.nonzero(alpha > 0.05)
    t, b, l, r = max(ys.min() - 2, 0), ys.max() + 3, max(xs.min() - 2, 0), xs.max() + 3
    save_rgba(img[t:b, l:r], alpha[t:b, l:r], "wordmark.png")


# --------------------------------------------------------------------------------------------

def gears():
    im = Image.open(SRC / "12_Dynamics_Closeup.jpg").convert("RGB").crop((1236, 16, 1432, 232))
    im.save(OUT / "gears.jpg", quality=86)


SCENES = {
    "reverb": "13_Space_Reverb_Closeup.jpg",
    "delay": "17_Space_Delay.jpg",
    "chorus": "18_Space_Chorus.jpg",
    "phaser": "19_Space_Phaser.jpg",
    "hall": "20_Space_Hall.jpg",
    "room": "21_Space_Room.jpg",
    "special": "22_Space_Special_FX.jpg",
}


def scenes():
    for key, name in SCENES.items():
        im = Image.open(SRC / name).convert("RGB").crop((349, 357, 1068, 606))
        im.save(OUT / f"scene_{key}.jpg", quality=88)


if __name__ == "__main__":
    logo()
    wordmark()
    gears()
    scenes()
    print("written to", OUT)
