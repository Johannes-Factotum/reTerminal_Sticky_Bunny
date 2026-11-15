#!/usr/bin/env python3
"""Build the Habits and Draftling launcher stickers (176x176, 0/170/255).

The sticker frame is taken from the existing Pet sticker; its interior is
replaced with a dotted sky and a pixel bunny from assets/pixel_bunnies.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "assets" / "app_launcher" / "firmware"
BUNNIES = ROOT / "assets" / "pixel_bunnies" / "firmware"
BLACK, GRAY, WHITE = 0, 170, 255


def frame() -> Image.Image:
    base = Image.open(FIRMWARE / "pet.png").convert("L")
    # Silhouette: everything not reachable from the border through white.
    mask = Image.new("L", base.size, 0)
    work = base.point(lambda v: 255 if v > 220 else 0).convert("1")
    flood = work.convert("L")
    ImageDraw.floodfill(flood, (0, 0), 128)
    mask = flood.point(lambda v: 0 if v == 128 else 255)
    interior = mask.filter(ImageFilter.MinFilter(17))
    out = base.copy()
    px, ip = out.load(), interior.load()
    for y in range(out.height):
        for x in range(out.width):
            if ip[x, y]:
                # Sparse gray dots in the upper sky, white below.
                dotted = y < 118 and (x + (y // 2) * 2) % 4 == 0 and y % 2 == 0
                px[x, y] = GRAY if dotted else WHITE
    return out


def paste_bunny(sticker: Image.Image, name: str, scale: int, x: int, y: int):
    bunny = Image.open(BUNNIES / name).convert("L")
    bunny = bunny.resize((bunny.width * scale, bunny.height * scale),
                         Image.NEAREST)
    bp, sp = bunny.load(), sticker.load()
    # White inside the bunny's bounding outline, black ink on top.
    for by in range(bunny.height):
        row = [bx for bx in range(bunny.width) if bp[bx, by] < 128]
        if not row:
            continue
        for bx in range(row[0], row[-1] + 1):
            sx, sy = x + bx, y + by
            if 0 <= sx < sticker.width and 0 <= sy < sticker.height:
                sp[sx, sy] = BLACK if bp[bx, by] < 128 else WHITE


def make_habits() -> None:
    sticker = frame()
    draw = ImageDraw.Draw(sticker)
    draw.ellipse([112, 40, 134, 62], outline=BLACK, width=3, fill=WHITE)
    for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0), (1, 1), (-1, -1),
                   (1, -1), (-1, 1)):
        cx, cy = 123, 51
        draw.line([cx + dx * 15, cy + dy * 15, cx + dx * 20, cy + dy * 20],
                  fill=BLACK, width=3)
    bunny = Image.open(BUNNIES / "pet_animation" / "pet_celebrate.png")
    paste_bunny(sticker, "pet_animation/pet_celebrate.png",
                1, 18, 150 - bunny.height)
    # Checklist card at the right.
    draw.rectangle([108, 96, 150, 146], outline=BLACK, width=3, fill=WHITE)
    for i, y in enumerate((106, 120, 134)):
        draw.rectangle([114, y - 3, 120, y + 3], outline=BLACK, width=2)
        if i < 2:
            draw.line([115, y, 117, y + 2], fill=BLACK, width=2)
            draw.line([117, y + 2, 122, y - 4], fill=BLACK, width=2)
        draw.line([125, y, 144, y], fill=BLACK, width=2)
    sticker.save(FIRMWARE / "habits.png")


def make_draftling() -> None:
    sticker = frame()
    bunny = Image.open(BUNNIES / "80x80" / "focusing.png")
    paste_bunny(sticker, "80x80/focusing.png", 2,
                (sticker.width - bunny.width * 2) // 2 + 4, 26)
    draw = ImageDraw.Draw(sticker)
    # Floating paper with text lines.
    draw.polygon([(26, 40), (58, 34), (64, 72), (32, 78)], outline=BLACK,
                 fill=WHITE, width=3)
    for i in range(3):
        draw.line([34, 50 + i * 8, 54, 46 + i * 8], fill=BLACK, width=2)
    sticker.save(FIRMWARE / "draftling.png")


if __name__ == "__main__":
    make_habits()
    make_draftling()
    print("wrote habits.png and draftling.png")
