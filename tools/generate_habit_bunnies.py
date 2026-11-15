#!/usr/bin/env python3
"""Compose the habit-page bunnies from existing desktop-pet line art.

Each output is a 1-bit 240x240 PNG in assets/habits/firmware/. Props are
drawn with the same ~4 px black line weight as the pet art; the bunny's
silhouette is filled white so props behind it stay hidden.
"""

from __future__ import annotations

from collections import deque
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
PET = ROOT / "assets" / "desktop_pet" / "firmware"
OUT = ROOT / "assets" / "habits" / "firmware"
SIZE = 240
LINE = 4


def load(name: str, scale: float = 1.0) -> Image.Image:
    image = Image.open(PET / f"{name}.png").convert("L")
    if scale != 1.0:
        size = (round(image.width * scale), round(image.height * scale))
        image = image.resize(size, Image.LANCZOS)
    return image.point(lambda v: 0 if v < 128 else 255)


def silhouette(image: Image.Image) -> Image.Image:
    """White-outside flood fill: returns mask (255 = part of subject)."""
    w, h = image.size
    px = image.load()
    outside = [[False] * w for _ in range(h)]
    queue: deque[tuple[int, int]] = deque()
    for x in range(w):
        queue.extend([(x, 0), (x, h - 1)])
    for y in range(h):
        queue.extend([(0, y), (w - 1, y)])
    while queue:
        x, y = queue.popleft()
        if x < 0 or y < 0 or x >= w or y >= h or outside[y][x]:
            continue
        if px[x, y] < 128:
            continue
        outside[y][x] = True
        queue.extend([(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)])
    mask = Image.new("L", (w, h), 0)
    mp = mask.load()
    for y in range(h):
        for x in range(w):
            if not outside[y][x]:
                mp[x, y] = 255
    return mask


def paste_subject(canvas: Image.Image, subject: Image.Image, x: int, y: int):
    canvas.paste(subject, (x, y), silhouette(subject))


def new_canvas() -> tuple[Image.Image, ImageDraw.ImageDraw]:
    image = Image.new("L", (SIZE, SIZE), 255)
    return image, ImageDraw.Draw(image)


def sun(draw: ImageDraw.ImageDraw, cx: int, cy: int, r: int):
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=0, width=LINE)
    import math
    for i in range(8):
        a = i * math.pi / 4
        x0 = cx + math.cos(a) * (r + 7)
        y0 = cy + math.sin(a) * (r + 7)
        x1 = cx + math.cos(a) * (r + 16)
        y1 = cy + math.sin(a) * (r + 16)
        draw.line([x0, y0, x1, y1], fill=0, width=LINE)


def sparkle(draw: ImageDraw.ImageDraw, cx: int, cy: int, s: int):
    draw.line([cx - s, cy, cx + s, cy], fill=0, width=3)
    draw.line([cx, cy - s, cx, cy + s], fill=0, width=3)


def sun_salutation() -> Image.Image:
    image, draw = new_canvas()
    sun(draw, 46, 46, 20)
    sparkle(draw, 206, 40, 7)
    # Yoga mat with a rolled end.
    draw.rounded_rectangle([18, 214, 222, 228], radius=7, outline=0,
                           width=LINE)
    draw.ellipse([196, 206, 226, 234], outline=0, width=LINE, fill=255)
    draw.ellipse([206, 215, 216, 225], outline=0, width=3)
    bunny = load("idle_stretch", 0.92)
    paste_subject(image, bunny, (SIZE - bunny.width) // 2 + 8,
                  214 - bunny.height + 6)
    return image


def pullups() -> Image.Image:
    image, draw = new_canvas()
    bunny = load("child_idle_stretch", 0.8)
    bx = (SIZE - bunny.width) // 2
    by = 2
    # Paws sit roughly 26 % down the stretch pose; the bar goes there.
    bar_y = by + int(bunny.height * 0.27)
    draw.line([14, bar_y, 226, bar_y], fill=0, width=LINE + 2)
    draw.line([18, bar_y, 18, 232], fill=0, width=LINE)
    draw.line([222, bar_y, 222, 232], fill=0, width=LINE)
    draw.line([6, 232, 30, 232], fill=0, width=LINE)
    draw.line([210, 232, 234, 232], fill=0, width=LINE)
    paste_subject(image, bunny, bx, by)
    # Bar drawn again across the paws so they grip it.
    draw.line([bx + 40, bar_y, bx + bunny.width - 40, bar_y], fill=0,
              width=LINE + 2)
    sparkle(draw, 46, 150, 6)
    sparkle(draw, 196, 168, 6)
    return image


def breakfast() -> Image.Image:
    image, draw = new_canvas()
    # Table edge.
    draw.line([6, 222, 234, 222], fill=0, width=LINE)
    bunny = load("feed", 0.9)
    paste_subject(image, bunny, 18, 222 - bunny.height + 4)
    # Bowl with cereal.
    draw.chord([146, 166, 234, 222], 0, 180, outline=0, width=LINE,
               fill=255)
    draw.line([146, 194, 234, 194], fill=0, width=LINE)
    for x, y in ((162, 186), (176, 182), (190, 187), (204, 183),
                 (218, 188)):
        draw.ellipse([x - 5, y - 5, x + 5, y + 5], outline=0, width=3)
    draw.line([204, 140, 226, 180], fill=0, width=LINE)  # spoon
    draw.ellipse([196, 128, 212, 146], outline=0, width=3, fill=255)
    # Steam.
    for x in (168, 186):
        draw.arc([x - 8, 140, x + 8, 160], 90, 270, fill=0, width=3)
        draw.arc([x - 8, 124, x + 8, 144], 270, 90, fill=0, width=3)
    return image


def morning_pages() -> Image.Image:
    image, draw = new_canvas()
    bunny = load("child_idle", 0.8)
    paste_subject(image, bunny, 0, 236 - bunny.height)
    # Open notebook resting in front of the bunny.
    left = [(96, 168), (162, 156), (162, 230), (96, 238)]
    right = [(162, 156), (228, 168), (228, 238), (162, 230)]
    draw.polygon(left, outline=0, fill=255, width=LINE)
    draw.polygon(right, outline=0, fill=255, width=LINE)
    for i in range(4):
        y = 178 + i * 13
        draw.line([106, y + 2 - i, 152, y - 6 - i], fill=0, width=2)
        draw.line([172, y - 6 - i, 214 - (18 if i == 3 else 0), y + 2 - i],
                  fill=0, width=2)
    # Pencil.
    draw.line([156, 136, 200, 108], fill=0, width=14)
    draw.line([158, 135, 198, 109], fill=255, width=6)
    draw.line([192, 103, 206, 125], fill=0, width=3)
    draw.polygon([(150, 128), (142, 148), (162, 142)], outline=0, fill=255,
                 width=3)
    draw.polygon([(144, 143), (142, 148), (147, 147)], fill=0)
    sparkle(draw, 210, 140, 6)
    sparkle(draw, 120, 120, 5)
    return image


def celebrate() -> Image.Image:
    image, draw = new_canvas()
    bunny = load("youth_affectionate_signature", 0.82)
    paste_subject(image, bunny, (SIZE - bunny.width) // 2,
                  SIZE - bunny.height - 4)
    for cx, cy, size in ((36, 50, 9), (204, 40, 11), (30, 150, 6),
                         (214, 130, 7), (120, 18, 6)):
        sparkle(draw, cx, cy, size)
    return image


def generic() -> Image.Image:
    image, draw = new_canvas()
    bunny = load("child_idle", 0.9)
    paste_subject(image, bunny, (SIZE - bunny.width) // 2,
                  SIZE - bunny.height - 4)
    sparkle(draw, 40, 60, 8)
    sparkle(draw, 200, 70, 8)
    return image


# Order must match HabitIcon in src/apps/habits/habits_model.h.
ART = (
    ("Generic", "generic", generic),
    ("SunSalutation", "sun_salutation", sun_salutation),
    ("Pullups", "pullups", pullups),
    ("Breakfast", "breakfast", breakfast),
    ("MorningPages", "morning_pages", morning_pages),
    ("Celebrate", "celebrate", celebrate),
)
PAGE_SIZE = 300


def pack(image: Image.Image) -> bytes:
    data = bytearray()
    px = image.load()
    for y in range(image.height):
        for x in range(0, image.width, 8):
            value = 0
            for bit in range(8):
                if x + bit < image.width and px[x + bit, y] < 128:
                    value |= 1 << (7 - bit)
            data.append(value)
    return bytes(data)


def write_cpp(images: list[tuple[str, Image.Image]]) -> None:
    header = ROOT / "src" / "ui" / "assets" / "habits_assets.h"
    source = ROOT / "src" / "ui" / "assets" / "habits_assets.cpp"
    header.write_text(
        "#pragma once\n\n#include <cstdint>\n\n#include \"pixel_asset.h\"\n\n"
        "// Generated by tools/generate_habit_bunnies.py - do not edit.\n"
        "enum class HabitArtId : uint8_t {\n"
        + "".join(f"    {enum},\n" for enum, _ in images)
        + "};\n\nconst PixelAsset &habits_art_asset(HabitArtId id);\n",
        encoding="utf-8")
    out = ["// Generated by tools/generate_habit_bunnies.py - do not edit.\n",
           '#include "habits_assets.h"\n\nnamespace {\n\n']
    for enum, image in images:
        data = pack(image)
        out.append(f"constexpr uint8_t k{enum}Data[] = {{\n")
        for i in range(0, len(data), 16):
            out.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16])
                       + ",\n")
        out.append("};\n\n")
    out.append("constexpr PixelAsset kAssets[] = {\n")
    for enum, image in images:
        out.append(f"    {{{image.width}, {image.height}, k{enum}Data}},\n")
    out.append("};\n\n}  // namespace\n\n"
               "const PixelAsset &habits_art_asset(HabitArtId id)\n{\n"
               "    const auto index = static_cast<uint8_t>(id);\n"
               "    return kAssets[index < sizeof(kAssets) / sizeof(kAssets[0])"
               " ? index : 0U];\n}\n")
    source.write_text("".join(out), encoding="utf-8")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    packed = []
    for enum, name, build in ART:
        image = build().point(lambda v: 0 if v < 128 else 255)
        image.save(OUT / f"{name}.png")
        page = image.resize((PAGE_SIZE, PAGE_SIZE), Image.LANCZOS)
        packed.append((enum, page.point(lambda v: 0 if v < 150 else 255)))
        print(f"wrote {name}.png")
    write_cpp(packed)


if __name__ == "__main__":
    main()
