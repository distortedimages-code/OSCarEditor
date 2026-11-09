#!/usr/bin/env python3
"""Draws the DMG window background (background.png and background@2x.png) in the plugin's
"Atelier" style: dark panel, wood cheeks, light plates under the icons (Finder writes the file
names in black or white depending on the Mac's appearance, so the plates are mid-grey), copper
arrows from each file to its folder, and the Terminal step at the bottom.
The icon positions here must match dmg_settings.py.  Needs Pillow:  python3 make_background.py"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
FONTS = ROOT / "Resources" / "Fonts"
W, H = 720, 540                     # window content, in points
LEFT_X, RIGHT_X = 196, 524          # icon centres (see dmg_settings.py)
ROWS = [166, 264, 362]
SCRIPT = (118, 470)


def font(name, size, s):
    return ImageFont.truetype(str(FONTS / name), int(size * s))


def draw(s):
    img = Image.new("RGB", (W * s, H * s), (34, 36, 40))
    d = ImageDraw.Draw(img)
    # brushed panel
    for x in range(0, W * s, 3 * s // 2 or 1):
        d.line([(x, 0), (x, H * s)], fill=(37, 39, 43))
    for y in range(H * s):
        t = y / (H * s)
        c = int(40 - 12 * t)
        d.line([(0, y), (W * s, y)], fill=(c, c + 2, c + 6)) if y % 2 else None

    # wood cheeks
    cheek = 16 * s
    for side, name in ((0, "bois-gauche.webp"), (1, "bois-droite.webp")):
        wood = Image.open(ROOT / "docs" / "img" / name).convert("RGB").resize((cheek, H * s))
        img.paste(wood, (0 if side == 0 else W * s - cheek, 0))
    d = ImageDraw.Draw(img)

    title = font("CormorantSC-SemiBold.ttf", 34, s)
    label = font("BarlowSemiCondensed-SemiBold.ttf", 13.5, s)
    bold = font("BarlowSemiCondensed-Bold.ttf", 13.5, s)
    small = font("BarlowSemiCondensed-SemiBold.ttf", 11.5, s)
    mono = font("ShareTechMono-Regular.ttf", 15, s)

    # vents and title
    for x0 in (40, W - 40 - 170):
        for i in range(8):
            y = 26 + i * 4
            d.rectangle([x0 * s, y * s, (x0 + 170) * s, (y + 2.4) * s], fill=(10, 11, 12))
    d.text((W * s / 2, 44 * s), "OSCar Editor", font=title, fill=(243, 239, 230), anchor="mm")
    d.text((W * s / 2, 74 * s), "OSCAR  ·  MIDI  ·  SYSEX  ·  TAUNTEK CC", font=small, fill=(150, 147, 140), anchor="mm")

    def chip(x, y, n):
        d.rounded_rectangle([x * s, (y - 12) * s, (x + 24) * s, (y + 12) * s], radius=3 * s, fill=(23, 11, 4), outline=(0, 0, 0))
        d.text(((x + 12) * s, y * s), n, font=mono, fill=(255, 157, 79), anchor="mm")

    chip(40, 112, "1")
    d.text((74 * s, 105 * s), "Drag each file onto its folder", font=bold, fill=(233, 229, 220), anchor="lm")
    d.text((74 * s, 120 * s), "Glisse chaque fichier sur son dossier", font=small, fill=(150, 147, 140), anchor="lm")
    d.text((W * s - 40 * s, 105 * s), "macOS may ask for your password", font=small, fill=(170, 167, 160), anchor="rm")
    d.text((W * s - 40 * s, 120 * s), "macOS peut demander ton mot de passe", font=small, fill=(130, 127, 120), anchor="rm")

    # plates under the icons (icon 64 pt, file name below)
    plate = Image.new("RGBA", img.size, (0, 0, 0, 0))
    p = ImageDraw.Draw(plate)
    def plate_at(cx, cy, w=136):
        box = [(cx - w / 2) * s, (cy - 40) * s, (cx + w / 2) * s, (cy + 52) * s]
        p.rounded_rectangle([b + 2 * s for b in box[:2]] + [b + 2 * s for b in box[2:]], radius=8 * s, fill=(0, 0, 0, 120))
        p.rounded_rectangle(box, radius=8 * s, fill=(112, 115, 121, 255), outline=(140, 143, 149, 255), width=max(1, s))
    for y in ROWS:
        plate_at(LEFT_X, y)
        plate_at(RIGHT_X, y)
    plate_at(*SCRIPT, w=136)
    plate = plate.filter(ImageFilter.GaussianBlur(0.3 * s))
    img.paste(plate, (0, 0), plate)
    d = ImageDraw.Draw(img)

    # copper arrows, with what each one is for
    tags = ["AU · Logic Pro", "VST3", "App"]
    for y, tag in zip(ROWS, tags):
        x0, x1 = LEFT_X + 78, RIGHT_X - 78
        d.line([(x0 * s, y * s), ((x1 - 10) * s, y * s)], fill=(200, 105, 48), width=3 * s)
        d.polygon([((x1 - 14) * s, (y - 7) * s), (x1 * s, y * s), ((x1 - 14) * s, (y + 7) * s)], fill=(200, 105, 48))
        d.text(((x0 + x1) / 2 * s, (y - 13) * s), tag, font=small, fill=(184, 180, 171), anchor="mm")

    # step 2: the Terminal
    box = [204 * s, 430 * s, (W - 36) * s, 512 * s]
    d.rounded_rectangle(box, radius=5 * s, fill=(23, 11, 4), outline=(0, 0, 0), width=max(1, s))
    chip(216, 452, "2")
    lines = [
        ("In Terminal, type  bash  and a space, drop Installer.command, press Return.", (255, 157, 79), label),
        ("It unblocks the plug-in, checks it for Logic Pro, and can install it all by itself.", (196, 128, 68), small),
        ("Terminal : tape  bash  et une espace, glisse Installer.command, puis Entrée.", (196, 128, 68), small),
        ("Il débloque le plugin, le vérifie pour Logic Pro et peut tout installer seul.", (150, 98, 52), small),
    ]
    for i, (t, col, f) in enumerate(lines):
        d.text((248 * s, (445 + i * 17) * s), t, font=f, fill=col, anchor="lm")
    return img


if __name__ == "__main__":
    big = draw(2)
    big.save(HERE / "background@2x.png", optimize=True)
    big.resize((W, H), Image.LANCZOS).save(HERE / "background.png", optimize=True)
    print("background.png", (W, H), "background@2x.png", big.size)
