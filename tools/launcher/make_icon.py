#!/usr/bin/env python3
# Draws the launcher's icon (launcher/res/excite-pc.ico): an amber badge with "EX" in the
# launcher's own font. Our own art, so it can live in the repo.
import os, sys
from PIL import Image, ImageDraw, ImageFont

repo = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
font_path = os.path.join(repo, 'launcher', 'fonts', 'BarlowCondensed-ExtraBoldItalic.ttf')
S = 1024
img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
# Sunset badge: dark outline, amber-to-red fill.
d.rounded_rectangle((40, 40, S - 40, S - 40), radius=220, fill=(40, 14, 8, 255))
grad = Image.new('RGBA', (S, S))
for y in range(S):
    t = y / S
    r = int(255 * (1 - t) + 236 * t); g = int(206 * (1 - t) + 70 * t); b = int(70 * (1 - t) + 30 * t)
    ImageDraw.Draw(grad).line((0, y, S, y), fill=(r, g, b, 255))
mask = Image.new('L', (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle((84, 84, S - 84, S - 84), radius=180, fill=255)
img.paste(grad, (0, 0), mask)
font = ImageFont.truetype(font_path, 600)
text = 'EX'
box = d.textbbox((0, 0), text, font=font)
x = (S - (box[2] - box[0])) / 2 - box[0]
y = (S - (box[3] - box[1])) / 2 - box[1] - 10
for dx in range(-22, 23, 4):
    for dy in range(-22, 23, 4):
        if dx * dx + dy * dy <= 22 * 22:
            d.text((x + dx, y + dy), text, font=font, fill=(40, 14, 8, 255))
d.text((x, y), text, font=font, fill=(255, 255, 255, 255))
out = os.path.join(repo, 'launcher', 'res', 'excite-pc.ico')
img.save(out, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
img.resize((256, 256), Image.LANCZOS).save(sys.argv[1] if len(sys.argv) > 1 else os.devnull, 'PNG') if len(sys.argv) > 1 else None
print(out)
