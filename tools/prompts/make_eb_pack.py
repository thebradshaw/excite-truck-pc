#!/usr/bin/env python3
"""Builds the Excitebots Xbox prompt pack (assets/prompts/excitebots), drawn the same way as
Excite Truck's (make_pack.py: plain shapes and the Inter typeface, no game artwork).

Each file replaces one texture of the US disc (RX3E01) and is named the way aurora's
Dolphin-style texture replacement expects: tex1_<w>x<h>_<xxh64>_<fmt>.png.

So far it replaces the crash prompt's shaking Wii Remote ("SHAKE!!!", overlays.res
shakecont.art) with the A button, since A plays a remote swing on the pad.

Usage: make_eb_pack.py <out dir> [scale]     (default scale 2: the texture is already 256x256)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_pack import Canvas, circle_mask  # noqa: E402

from PIL import Image  # noqa: E402


def build(scale):
    out = {}
    # shakecont.art: a 256x256 colour texture (CMPR) holding the remote twice, at y 24-81 and
    # 164-221 (x 31-231), cut out by a separate I4 mask. Both become an A button centred on the
    # remote's place, the lower one pressed, with a disc mask to match.
    c = Canvas(256, 256, scale, (0, 0, 0, 255))
    r = 29
    for cy, pressed in ((52.5, False), (192.5, True)):
        c.face(131 - r, cy - r, 131 + r, cy + r, "A", pressed=pressed)
    out["tex1_256x256_f2965209f9cff340_14.png"] = c.result()
    top = circle_mask(256, 256, scale, 131 - r, 52.5 - r, 131 + r, 52.5 + r)
    bottom = circle_mask(256, 256, scale, 131 - r, 192.5 - r, 131 + r, 192.5 + r)
    out["tex1_256x256_6f12f80be63edc3c_0.png"] = Image.composite(top, bottom, top.getchannel("A"))
    return out


if __name__ == "__main__":
    dest = sys.argv[1]
    scale = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    os.makedirs(dest, exist_ok=True)
    for old in os.listdir(dest):
        if old.startswith("tex1_") and old.endswith(".png"):
            os.remove(os.path.join(dest, old))
    for name, im in build(scale).items():
        im.save(os.path.join(dest, name), optimize=True)
    print(f"wrote {len(os.listdir(dest))} files to {dest}")
