#!/usr/bin/env python3
"""Builds the Xbox button prompt pack (assets/prompts/xbox) for the Excite Truck PC port.

Every image is drawn here from scratch: plain shapes plus the Inter typeface (SIL Open Font
Licence). No game or console-maker artwork is copied. Each file replaces one game texture and is
named the way aurora's Dolphin-style texture replacement expects: tex1_<w>x<h>_<xxh64>_<fmt>.png,
where the hash is of the texture's base level as stored on the US disc (REXE01).

The game draws most icons as two textures: a colour texture (CMPR, format 14) and a separate
4-bit alpha mask (I4, format 0) that cuts out its shape. The pack replaces the colours and, for the
round buttons, also draws a sharper circle mask. Shapes that stay Wii-shaped (the D-pad) keep the
disc's mask, so their colour art follows the mask's outline.

Usage: make_pack.py <out dir> [scale]     (default scale 4: a 64x64 icon becomes 256x256)
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

FONTS = {
    "bold": ["/usr/share/fonts/opentype/inter/Inter-ExtraBold.otf",
             "/usr/share/fonts/opentype/inter/Inter-Bold.otf",
             "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"],
    "italic": ["/usr/share/fonts/opentype/inter/Inter-ExtraBoldItalic.otf",
               "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf"],
}
FACE = (34, 34, 38)
RIM = (225, 225, 228)
WHITE = (242, 242, 242)
LETTER = {"A": (100, 196, 64), "B": (232, 62, 52), "X": (44, 132, 232), "Y": (246, 192, 32)}
SS = 4  # supersampling while drawing


def font(kind, px):
    for p in FONTS[kind]:
        if os.path.exists(p):
            return ImageFont.truetype(p, int(px))
    raise SystemExit(f"no {kind} font found")


def text_centre(d, cx, cy, s, f, fill, stroke=0, stroke_fill=None):
    l, t, r, b = d.textbbox((0, 0), s, font=f, anchor="lt", stroke_width=stroke)
    d.text((cx - (r - l) / 2 - l, cy - (b - t) / 2 - t), s, font=f, fill=fill, anchor="lt",
           stroke_width=stroke, stroke_fill=stroke_fill)


class Canvas:
    """Draws in texture pixel coordinates (of the original texture) at scale * SS."""

    def __init__(self, w, h, scale, bg=(0, 0, 0, 0)):
        self.w, self.h, self.scale = w, h, scale
        self.k = scale * SS
        self.im = Image.new("RGBA", (w * self.k, h * self.k), bg)
        self.d = ImageDraw.Draw(self.im)

    def box(self, x0, y0, x1, y1):
        k = self.k
        return [x0 * k, y0 * k, x1 * k, y1 * k]

    def result(self):
        return self.im.resize((self.w * self.scale, self.h * self.scale), Image.LANCZOS)

    # --- icons, each fitted to a box given in original texture pixels ---------------------
    def face(self, x0, y0, x1, y1, label, colour=None, rim=0.09, pressed=False):
        d, k = self.d, self.k
        size = min(x1 - x0, y1 - y0)
        r = rim * size
        d.ellipse(self.box(x0, y0, x1, y1), fill=RIM if not pressed else (170, 170, 174))
        d.ellipse(self.box(x0 + r, y0 + r, x1 - r, y1 - r), fill=FACE if not pressed else (20, 20, 22))
        cx, cy = (x0 + x1) / 2 * k, (y0 + y1) / 2 * k
        if label == "menu":
            lw = size * k * 0.075
            for j in (-1, 0, 1):
                y = cy + j * size * k * 0.15
                d.rounded_rectangle([cx - size * k * 0.2, y - lw / 2, cx + size * k * 0.2, y + lw / 2], lw / 2, fill=WHITE)
        elif label == "view":
            a = size * k * 0.22; lw = int(size * k * 0.06)
            ox, oy = cx - size * k * 0.23, cy - size * k * 0.25
            d.rounded_rectangle([ox, oy, ox + a, oy + a], size * k * 0.03, outline=WHITE, width=lw)
            d.rounded_rectangle([ox + a * 0.9, oy + a * 0.9, ox + a * 1.9, oy + a * 1.9], size * k * 0.03, fill=WHITE)
        elif label == "home":
            s = size * k
            d.polygon([(cx, cy - s * 0.24), (cx - s * 0.24, cy), (cx + s * 0.24, cy)], fill=WHITE)
            d.rectangle([cx - s * 0.16, cy - s * 0.02, cx + s * 0.16, cy + s * 0.2], fill=WHITE)
        elif label == "power":
            s = size * k; lw = int(s * 0.07)
            d.arc([cx - s * 0.22, cy - s * 0.22, cx + s * 0.22, cy + s * 0.22], -60, 240, fill=WHITE, width=lw)
            d.rounded_rectangle([cx - lw / 2, cy - s * 0.3, cx + lw / 2, cy], lw / 2, fill=WHITE)
        else:
            f = font("bold", size * k * (0.62 if len(label) == 1 else 0.4))
            text_centre(d, cx, cy, label, f, colour or LETTER.get(label, WHITE))

    def dpad(self, x0, y0, x1, y1, lit=None):
        """A cross filling the box (the disc's D-pad mask is a cross of about 1/3 arm width)."""
        d, k = self.d, self.k
        w = x1 - x0; arm = w * 0.36; cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        d.rectangle(self.box(x0, y0, x1, y1), fill=RIM)
        i = w * 0.07
        d.rounded_rectangle(self.box(cx - arm / 2 + i, y0 + i, cx + arm / 2 - i, y1 - i), w * 0.04 * k, fill=FACE)
        d.rounded_rectangle(self.box(x0 + i, cy - arm / 2 + i, x1 - i, cy + arm / 2 - i), w * 0.04 * k, fill=FACE)
        t = w * 0.07
        for (dx, dy) in ((0, -1), (1, 0), (0, 1), (-1, 0)):
            ax, ay = cx + dx * w * 0.3, cy + dy * w * 0.3
            col = (246, 192, 32) if lit == (dx, dy) else WHITE
            pts = [(ax + dx * t, ay + dy * t), (ax - dy * t - dx * t * 0.6, ay + dx * t - dy * t * 0.6),
                   (ax + dy * t - dx * t * 0.6, ay - dx * t - dy * t * 0.6)]
            d.polygon([(px * k, py * k) for px, py in pts], fill=col)

    def pill(self, x0, y0, x1, y1, label):
        d, k = self.d, self.k
        h = y1 - y0; r = h * 0.12
        d.rounded_rectangle(self.box(x0, y0, x1, y1), h * 0.3 * k, fill=RIM)
        d.rounded_rectangle(self.box(x0 + r, y0 + r, x1 - r, y1 - r), (h * 0.3 - r) * k, fill=FACE)
        text_centre(d, (x0 + x1) / 2 * k, (y0 + y1) / 2 * k, label, font("bold", min(h, x1 - x0) * k * 0.5), WHITE)


def circle_mask(w, h, scale, x0, y0, x1, y1):
    """An I4 alpha mask replacement: a white disc. I4 texels read as (I, I, I, I), so RGB and alpha
    both carry the mask."""
    c = Canvas(w, h, scale, (0, 0, 0, 0))
    c.d.ellipse(c.box(x0, y0, x1, y1), fill=(255, 255, 255, 255))
    im = c.result()
    v = im.getchannel("A")
    return Image.merge("RGBA", (v, v, v, v))


def build(scale):
    out = {}

    # common.res: the round prompt icons (menus, title screen, tutorials). One shared circle
    # mask (e38526d2efbddd13) cuts out 1, 2, + and -.
    for name, label in (("c8e9868a2a851ed2", "B"),      # but_1: Wii 1 = back / brake
                        ("82d8d8bfb09e0473", "A"),      # but_2: Wii 2 = OK / gas
                        ("3af32d71c3492a6c", "Y"),      # but_a: Wii A = reset (Y)
                        ("2d1f16a88dbc6ce7", "menu"),   # but_plus: pause (Start/Menu)
                        ("f75ed7456325a597", "view")):  # but_minus: Back/View
        c = Canvas(64, 64, scale)
        c.face(0, 0, 64, 64, label)
        out[f"tex1_64x64_{name}_14.png"] = c.result()
    out["tex1_64x64_e38526d2efbddd13_0.png"] = circle_mask(64, 64, scale, 0.5, 0.5, 63.5, 63.5)
    out["tex1_64x64_08fba3554ebf64a8_0.png"] = circle_mask(64, 64, scale, 0.5, 0.5, 63.5, 63.5)  # but_a
    c = Canvas(64, 64, scale)                            # but_dpad, on the disc's cross mask
    c.dpad(0, 0, 64, 64)
    out["tex1_64x64_ff0c80606ea1014a_14.png"] = c.result()

    # uiaction.res: the larger glossy 1 and 2 buttons, and the small dark pair (controlwii).
    for name, label in (("0df229b470d93f7f", "B"), ("5873395ad71f2288", "A")):
        c = Canvas(64, 64, scale)
        c.face(3, 3, 61, 61, label)
        out[f"tex1_64x64_{name}_14.png"] = c.result()
    out["tex1_64x64_4dfaa1be4fa28034_0.png"] = circle_mask(64, 64, scale, 3, 3, 61, 61)
    c = Canvas(64, 64, scale)
    c.face(1, 1, 25, 25, "B")
    c.face(1, 28, 25, 52, "A")
    out["tex1_64x64_61ed9f66815ac8b1_14.png"] = c.result()

    # race.res replayrvl.art: the replay controls, 4x4 cells of 32 px.
    c = Canvas(128, 128, scale)
    for i, lit in enumerate(((0, -1), (1, 0), (0, 1), (-1, 0))):
        c.dpad(32 * i + 1, 1, 32 * i + 31, 31, lit)
    c.face(1, 33, 31, 63, "A")          # Wii 2
    c.pill(37, 33, 59, 63, "")          # Wii B: no pad button does it, so the cell is left blank
    c.face(65, 33, 95, 63, "view")      # Wii -
    c.face(97, 33, 127, 63, "B")        # Wii 1
    c.face(33, 65, 63, 95, "menu")      # Wii +
    c.face(65, 65, 95, 95, "home")
    c.face(97, 65, 127, 95, "power")
    c.face(33, 97, 63, 127, "Y")        # Wii A
    c.dpad(65, 97, 95, 127)
    out["tex1_128x128_ad1b0c8c7cb2b444_14.png"] = c.result()

    # race.res tapbut.tex: "tap 2" after a crash (A does that on the pad). Left column: the button
    # up and pressed; right column: a burst and an arrow, redrawn as plain greys.
    c = Canvas(128, 128, scale)
    c.face(2, 4, 62, 64, "A")
    c.face(4, 68, 60, 124, "A", pressed=True)
    d, k = c.d, c.k
    for y in range(0, 48):                 # burst: dark centre fading to white spikes
        g = int(40 + 215 * abs(y - 24) / 24) if y < 48 else 255
    for x in range(64, 128):
        g = int(30 + 225 * min(1.0, abs(x - 96) / 30))
        d.line([(x * k, 0), (x * k, 48 * k)], fill=(g, g, g, 255), width=k)
    d.rectangle(c.box(64, 48, 128, 128), fill=(236, 236, 236, 255))
    d.rectangle(c.box(64, 90, 128, 128), fill=(200, 200, 204, 255))
    out["tex1_128x128_37121d027ff96534_14.png"] = c.result()

    # overlays.res press2.tex: "Press 2 to Start" with the text in the image (1-bit alpha).
    c = Canvas(256, 64, scale)
    f = font("italic", 25 * c.k)
    red, edge = (196, 24, 24), (255, 255, 255)
    text_centre(c.d, 50 * c.k, 32 * c.k, "Press", f, red, stroke=int(1.2 * c.k), stroke_fill=edge)
    c.face(92, 14, 128, 50, "A")
    text_centre(c.d, 186 * c.k, 32 * c.k, "to Start", f, red, stroke=int(1.2 * c.k), stroke_fill=edge)
    out["tex1_256x64_34ae5c4c1388b5e9_14.png"] = c.result()
    return out


if __name__ == "__main__":
    dest = sys.argv[1]
    scale = int(sys.argv[2]) if len(sys.argv) > 2 else 4
    os.makedirs(dest, exist_ok=True)
    for old in os.listdir(dest):
        if old.startswith("tex1_") and old.endswith(".png"):
            os.remove(os.path.join(dest, old))
    for name, im in build(scale).items():
        im.save(os.path.join(dest, name), optimize=True)
    print(f"wrote {len(os.listdir(dest))} files to {dest}")
