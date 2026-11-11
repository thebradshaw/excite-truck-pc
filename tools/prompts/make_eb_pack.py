#!/usr/bin/env python3
"""Builds the Excitebots Xbox prompt pack (assets/prompts/excitebots), drawn the same way as
Excite Truck's (make_pack.py: plain shapes and the Inter typeface, no game artwork).

Each file replaces one texture of the US disc (RX3E01) and is named the way aurora's
Dolphin-style texture replacement expects: tex1_<w>x<h>_<xxh64>_<fmt>.png.

It replaces the Wii Remote pictures that ask for a motion with the left stick, which plays the
motion on the pad: the shaking remote (bar spin, dentures) becomes a stick pushed left and right
(but the A button during crash recovery, where A is the main way back up), and the bar prompt's pushed remote ("Push") a stick flicked up, or, on a bar, an A button
that shrinks and grows in place.

It also redraws the round button icons Excitebots shares with Excite Truck (common.res, same
hashes): Wii 2 becomes A (OK / accelerate), Wii 1 becomes B (back) and Wii A becomes B, since B
fires weapons in a race. + and - become Menu and View.

Usage: make_eb_pack.py <out dir> [scale]     (default scale 2: the texture is already 256x256)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_pack import Canvas, circle_mask, FACE, RIM, WHITE, font, text_centre  # noqa: E402

from PIL import Image  # noqa: E402


def stick(c, cx, cy, r, dx, dy):
    """A left stick in a disc of radius r: the well, the thumb cap pushed by (dx, dy) (-1..1) with
    an L on it, and an arrow in the direction it is pushed."""
    d, k = c.d, c.k
    d.ellipse(c.box(cx - r, cy - r, cx + r, cy + r), fill=RIM)
    w = r * 0.9
    d.ellipse(c.box(cx - w, cy - w, cx + w, cy + w), fill=(20, 20, 22))
    # The arrow, on the side the cap moves to.
    a, t = r * 0.66, r * 0.18
    ax, ay = cx + dx * a, cy + dy * a
    px, py = -dy, dx
    tip = (ax + dx * t, ay + dy * t)
    pts = [tip, (ax - dx * t * 0.4 + px * t, ay - dy * t * 0.4 + py * t), (ax - dx * t * 0.4 - px * t, ay - dy * t * 0.4 - py * t)]
    d.polygon([(x * k, y * k) for x, y in pts], fill=(246, 192, 32))
    # The cap, moved toward the arrow but kept inside the well.
    cr = r * 0.42
    ccx, ccy = cx + dx * r * 0.1, cy + dy * r * 0.1
    d.ellipse(c.box(ccx - cr, ccy - cr, ccx + cr, ccy + cr), fill=(70, 70, 76))
    ir = cr * 0.84
    d.ellipse(c.box(ccx - ir, ccy - ir, ccx + ir, ccy + ir), fill=FACE)
    text_centre(d, ccx * k, ccy * k, "L", font("bold", cr * k * 1.05), WHITE)


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
    # 164-221 (x 31-231), cut out by a separate I4 mask; the game swaps between the two to shake
    # it. Both become the left stick centred on the remote's place, pushed left in one and right
    # in the other, with a disc mask to match.
    c = Canvas(256, 256, scale, (0, 0, 0, 255))
    r = 29
    for cy, dx in ((52.5, -1), (192.5, 1)):
        stick(c, 131, cy, r, dx, 0)
    out["tex1_256x256_f2965209f9cff340_14.png"] = c.result()
    # The same prompt during crash recovery, where A is the main way back up (the stick shake
    # still works). The runtime hands aurora a copy of the texture with a marked head while the
    # crash meter is shown, so it hashes differently: A up in one frame, pressed in the other.
    c = Canvas(256, 256, scale, (0, 0, 0, 255))
    for cy, pressed in ((52.5, False), (192.5, True)):
        c.face(131 - r, cy - r, 131 + r, cy + r, "A", pressed=pressed)
    out["tex1_256x256_e15dafb8e52aaa7e_14.png"] = c.result()
    top = circle_mask(256, 256, scale, 131 - r, 52.5 - r, 131 + r, 52.5 + r)
    bottom = circle_mask(256, 256, scale, 131 - r, 192.5 - r, 131 + r, 192.5 + r)
    out["tex1_256x256_6f12f80be63edc3c_0.png"] = Image.composite(top, bottom, top.getchannel("A"))

    # The bar prompt ("Push"): a 256x256 colour texture of one remote seen side on, cut out by an
    # I4 mask whose silhouette spans x 9-245, y 10-75. A stick flicked up goes in its middle.
    cx, cy = 127.5, 43
    c = Canvas(256, 256, scale, (0, 0, 0, 255))
    stick(c, cx, cy, r, 0, -1)
    out["tex1_256x256_ddf1c663d39c4ad2_14.png"] = c.result()
    out["tex1_256x256_aa8407612f74e6ba_0.png"] = circle_mask(256, 256, scale, cx - r, cy - r, cx + r, cy + r)
    # On a bar the runtime keeps that sprite still and swaps its texture and mask for marked
    # copies, four sizes of the A button in turn (big, smaller, smallest and pressed, smaller), so
    # the hint shrinks and grows in place. The sprite is drawn 3/4 as wide as its texture, so the
    # button is drawn 4/3 wide to come out round.
    wide = 4.0 / 3.0
    sizes = (1.0, 0.86, 0.72, 0.86)
    colours = ("0036fa02b3604836", "b295e7019079280a", "eae01fd0710c5262", "072d96372fe85f18")
    masks = ("a1a43cb6f99787d1", "bab0e0f29acd15f9", "80119e72cbdeb87c", "92d69241a12093aa")
    for i, size in enumerate(sizes):
        button = Canvas(64, 64, scale * 2)
        button.face(0, 0, 64, 64, "A", pressed=(i == 2))
        ry = r * size
        rx = ry * wide
        im = Image.new("RGBA", (256 * scale, 256 * scale), (0, 0, 0, 255))
        face = button.result().resize((round(2 * rx * scale), round(2 * ry * scale)), Image.LANCZOS)
        im.paste(face, (round((cx - rx) * scale), round((cy - ry) * scale)), face)
        out[f"tex1_256x256_{colours[i]}_14.png"] = im
        out[f"tex1_256x256_{masks[i]}_0.png"] = circle_mask(256, 256, scale, cx - rx, cy - ry, cx + rx, cy + ry)
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
