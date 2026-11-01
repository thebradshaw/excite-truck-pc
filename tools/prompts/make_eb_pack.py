#!/usr/bin/env python3
"""Builds the Excitebots Xbox prompt pack (assets/prompts/excitebots), drawn the same way as
Excite Truck's (make_pack.py: plain shapes and the Inter typeface, no game artwork).

Each file replaces one texture of the US disc (RX3E01) and is named the way aurora's
Dolphin-style texture replacement expects: tex1_<w>x<h>_<xxh64>_<fmt>.png.

It replaces the Wii Remote pictures that ask for a motion with the A button, since A plays a
remote swing on the pad: the crash prompt's shaking remote ("SHAKE!!!") and the bar prompt's
pushed remote ("Push", grab and release a bar).

It also redraws the round button icons Excitebots shares with Excite Truck (common.res, same
hashes): Wii 2 becomes A (OK / accelerate), Wii 1 becomes B (back) and Wii A becomes B, since B
fires weapons in a race. + and - become Menu and View.

Usage: make_eb_pack.py <out dir> [scale]     (default scale 2: the texture is already 256x256)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_pack import Canvas, circle_mask  # noqa: E402

from PIL import Image  # noqa: E402


def build(scale):
    out = {}
    for name, label in (("c8e9868a2a851ed2", "B"),      # but_1: Wii 1 = back
                        ("82d8d8bfb09e0473", "A"),      # but_2: Wii 2 = OK / accelerate
                        ("3af32d71c3492a6c", "B"),      # but_a: Wii A = weapon (B in a race)
                        ("2d1f16a88dbc6ce7", "menu"),   # but_plus: pause
                        ("f75ed7456325a597", "view")):  # but_minus
        c = Canvas(64, 64, scale)
        c.face(0, 0, 64, 64, label)
        out[f"tex1_64x64_{name}_14.png"] = c.result()
    out["tex1_64x64_e38526d2efbddd13_0.png"] = circle_mask(64, 64, scale, 0.5, 0.5, 63.5, 63.5)
    out["tex1_64x64_08fba3554ebf64a8_0.png"] = circle_mask(64, 64, scale, 0.5, 0.5, 63.5, 63.5)

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

    # The bar prompt ("Push"): a 256x256 colour texture of one remote seen side on, cut out by an
    # I4 mask whose silhouette spans x 9-245, y 10-75. The A button goes in its middle.
    cx, cy = 127.5, 43
    c = Canvas(256, 256, scale, (0, 0, 0, 255))
    c.face(cx - r, cy - r, cx + r, cy + r, "A")
    out["tex1_256x256_ddf1c663d39c4ad2_14.png"] = c.result()
    out["tex1_256x256_aa8407612f74e6ba_0.png"] = circle_mask(256, 256, scale, cx - r, cy - r, cx + r, cy + r)
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
