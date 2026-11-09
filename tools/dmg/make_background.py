#!/usr/bin/env python3
"""Draws the DMG window background (background.png and background@2x.png) in the plugin's
"Atelier" style: a brushed metal panel between wood cheeks, and a copper arrow from each file to
its folder. Finder writes the file names in black or white depending on the Mac's appearance,
so the panel is a mid grey that both stay readable on. Text stays clear of the icons and names.
The icon positions here must match dmg_settings.py.  Needs Pillow:  python3 make_background.py"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
FONTS = ROOT / "Resources" / "Fonts"
W, H = 640, 420                     # window content in points (a little taller than the window shows)
LEFT_X, RIGHT_X = 170, 470          # icon centres (see dmg_settings.py)
ROWS = [128, 222, 316]
TAGS = ["AU  ·  Logic Pro", "VST3", "App"]


def font(name, size, s):
    return ImageFont.truetype(str(FONTS / name), int(size * s))


def draw(s):
    img = Image.new("RGB", (W * s, H * s))
    d = ImageDraw.Draw(img)
    for y in range(H * s):                       # brushed metal, a little darker at the bottom
        c = int(124 - 16 * y / (H * s))
        d.line([(0, y), (W * s, y)], fill=(c, c + 3, c + 7))
    for x in range(0, W * s, 2 * s):
        d.line([(x, 0), (x, H * s)], fill=(118, 121, 126) if (x // (2 * s)) % 3 else (128, 131, 136))

    cheek = 14 * s
    for left, name in ((True, "bois-gauche.webp"), (False, "bois-droite.webp")):
        wood = Image.open(ROOT / "docs" / "img" / name).convert("RGB").resize((cheek, H * s))
        img.paste(wood, (0 if left else W * s - cheek, 0))
    d = ImageDraw.Draw(img)

    title = font("CormorantSC-SemiBold.ttf", 26, s)
    small = font("BarlowSemiCondensed-SemiBold.ttf", 12, s)
    tag = font("BarlowSemiCondensed-SemiBold.ttf", 11.5, s)

    # title between two vents
    d.text((W * s / 2, 35 * s), "OSCar Editor", font=title, fill=(30, 31, 34), anchor="mm")
    d.text((W * s / 2, 34 * s), "OSCar Editor", font=title, fill=(247, 244, 238), anchor="mm")
    for x0 in (44, W - 44 - 150):
        for i in range(6):
            y = 25 + i * 4
            d.rectangle([x0 * s, y * s, (x0 + 150) * s, (y + 2.2) * s], fill=(40, 42, 46))
    d.text((W * s / 2, 62 * s), "Drag each file onto its folder", font=small, fill=(246, 243, 236), anchor="mm")

    # copper arrows with what each row is for, between the plates
    for y, t in zip(ROWS, TAGS):
        x0, x1 = LEFT_X + 102, RIGHT_X - 102
        d.line([(x0 * s, y * s), ((x1 - 9) * s, y * s)], fill=(166, 74, 24), width=3 * s)
        d.polygon([((x1 - 12) * s, (y - 6) * s), (x1 * s, y * s), ((x1 - 12) * s, (y + 6) * s)], fill=(166, 74, 24))
        d.text(((x0 + x1) / 2 * s, (y - 12) * s), t, font=tag, fill=(250, 247, 241), anchor="mm")

    d.text((W * s / 2, 382 * s), "Then unblock it once: see the install guide on the OSCar Editor website.",
           font=tag, fill=(246, 243, 236), anchor="mm")
    return img


if __name__ == "__main__":
    big = draw(2)
    big.save(HERE / "background@2x.png", optimize=True)
    big.resize((W, H), Image.LANCZOS).save(HERE / "background.png", optimize=True)
    print("background.png", (W, H), "background@2x.png", big.size)
