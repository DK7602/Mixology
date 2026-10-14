"""High-definition Space pictures: cuts the seven scenes out of the approved mockups (the same region as
extract_assets.py) and upscales them 4x with Real-ESRGAN (x4plus), which rebuilds detail and removes the
mockups' JPEG blockiness. Writes plugin/ui/assets/scene_<name>.webp (2876 x 996).

Usage (CPU is fine, about a minute per picture):
    python3 -m venv venv && venv/bin/pip install torch --index-url https://download.pytorch.org/whl/cpu && venv/bin/pip install numpy pillow
    curl -L -o RealESRGAN_x4plus.pth https://github.com/xinntao/Real-ESRGAN/releases/download/v0.1.0/RealESRGAN_x4plus.pth
    venv/bin/python tools/art/upscale_scenes.py <Images folder from the handoff zip> RealESRGAN_x4plus.pth plugin/ui/assets
"""

import sys
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from PIL import Image

SCENES = {
    "reverb": "13_Space_Reverb_Closeup.jpg",
    "delay": "17_Space_Delay.jpg",
    "chorus": "18_Space_Chorus.jpg",
    "phaser": "19_Space_Phaser.jpg",
    "hall": "20_Space_Hall.jpg",
    "room": "21_Space_Room.jpg",
    "special": "22_Space_Special_FX.jpg",
}
CROP = (349, 357, 1068, 606)   # the scene inside a 1448 x 1086 mockup


# Real-ESRGAN's generator (RRDBNet, 23 blocks), as published by xinntao/Real-ESRGAN.
class RDB(nn.Module):
    def __init__(s, nf=64, gc=32):
        super().__init__()
        s.conv1 = nn.Conv2d(nf, gc, 3, 1, 1); s.conv2 = nn.Conv2d(nf + gc, gc, 3, 1, 1)
        s.conv3 = nn.Conv2d(nf + 2 * gc, gc, 3, 1, 1); s.conv4 = nn.Conv2d(nf + 3 * gc, gc, 3, 1, 1)
        s.conv5 = nn.Conv2d(nf + 4 * gc, nf, 3, 1, 1); s.lrelu = nn.LeakyReLU(0.2, True)
    def forward(s, x):
        x1 = s.lrelu(s.conv1(x)); x2 = s.lrelu(s.conv2(torch.cat((x, x1), 1)))
        x3 = s.lrelu(s.conv3(torch.cat((x, x1, x2), 1))); x4 = s.lrelu(s.conv4(torch.cat((x, x1, x2, x3), 1)))
        return s.conv5(torch.cat((x, x1, x2, x3, x4), 1)) * 0.2 + x

class RRDB(nn.Module):
    def __init__(s, nf=64, gc=32):
        super().__init__(); s.rdb1 = RDB(nf, gc); s.rdb2 = RDB(nf, gc); s.rdb3 = RDB(nf, gc)
    def forward(s, x): return s.rdb3(s.rdb2(s.rdb1(x))) * 0.2 + x

class RRDBNet(nn.Module):
    def __init__(s, nf=64, nb=23, gc=32):
        super().__init__()
        s.conv_first = nn.Conv2d(3, nf, 3, 1, 1); s.body = nn.Sequential(*[RRDB(nf, gc) for _ in range(nb)])
        s.conv_body = nn.Conv2d(nf, nf, 3, 1, 1); s.conv_up1 = nn.Conv2d(nf, nf, 3, 1, 1); s.conv_up2 = nn.Conv2d(nf, nf, 3, 1, 1)
        s.conv_hr = nn.Conv2d(nf, nf, 3, 1, 1); s.conv_last = nn.Conv2d(nf, 3, 3, 1, 1); s.lrelu = nn.LeakyReLU(0.2, True)
    def forward(s, x):
        f = s.conv_first(x); f = f + s.conv_body(s.body(f))
        f = s.lrelu(s.conv_up1(F.interpolate(f, scale_factor=2, mode="nearest")))
        f = s.lrelu(s.conv_up2(F.interpolate(f, scale_factor=2, mode="nearest")))
        return s.conv_last(s.lrelu(s.conv_hr(f)))



def upscale(net, img, tile=160, pad=16):
    x = torch.from_numpy(img.transpose(2, 0, 1)).unsqueeze(0)
    h, w, _ = img.shape
    out = np.zeros((h * 4, w * 4, 3), np.float32)
    with torch.no_grad():
        for y0 in range(0, h, tile):
            for x0 in range(0, w, tile):
                y1, x1 = min(y0 + tile, h), min(x0 + tile, w)
                py0, px0, py1, px1 = max(y0 - pad, 0), max(x0 - pad, 0), min(y1 + pad, h), min(x1 + pad, w)
                o = net(x[:, :, py0:py1, px0:px1]).squeeze(0).clamp(0, 1).numpy().transpose(1, 2, 0)
                oy, ox = (y0 - py0) * 4, (x0 - px0) * 4
                out[y0 * 4:y1 * 4, x0 * 4:x1 * 4] = o[oy:oy + (y1 - y0) * 4, ox:ox + (x1 - x0) * 4]
    return out


if __name__ == "__main__":
    src, weights, dst = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    net = RRDBNet()
    w = torch.load(weights, map_location="cpu")
    net.load_state_dict(w["params_ema"] if "params_ema" in w else w, strict=True)
    net.eval()
    for key, name in SCENES.items():
        img = np.asarray(Image.open(src / name).convert("RGB").crop(CROP)).astype(np.float32) / 255.0
        hd = upscale(net, img)
        Image.fromarray((hd * 255 + 0.5).astype(np.uint8)).save(dst / f"scene_{key}.webp", quality=90, method=6)
        print("wrote", dst / f"scene_{key}.webp")
