#!/usr/bin/env python3
"""Derive the Cards and To-Do launcher stickers from existing artwork.

cards.png: the Book of Answers bunny now studies a stack of flashcards.
todo.png:  the Status Board bunny holds a checklist sign.
Output stays in the three-level palette (0 / 170 / 255) used by
tools/generate_app_launcher_assets.py.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "assets" / "app_launcher" / "firmware"
BLACK, GRAY, WHITE = 0, 170, 255

GLYPHS = {  # 5x7, rows top to bottom
    "A": [".XXX.", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X"],
    "a": [".....", ".....", ".XXX.", "....X", ".XXXX", "X...X", ".XXXX"],
    "?": [".XXX.", "X...X", "....X", "...X.", "..X..", ".....", "..X.."],
}


def rect(px, x0, y0, x1, y1, value):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px[x, y] = value


def outline(px, x0, y0, x1, y1, thickness=2):
    for t in range(thickness):
        for x in range(x0, x1 + 1):
            px[x, y0 + t] = BLACK
            px[x, y1 - t] = BLACK
        for y in range(y0, y1 + 1):
            px[x0 + t, y] = BLACK
            px[x1 - t, y] = BLACK


def glyph(px, x, y, char, scale=2):
    for row, bits in enumerate(GLYPHS[char]):
        for col, bit in enumerate(bits):
            if bit == "X":
                rect(px, x + col * scale, y + row * scale,
                     x + col * scale + scale - 1, y + row * scale + scale - 1,
                     BLACK)


def make_cards() -> None:
    image = Image.open(FIRMWARE / "answers.png").convert("L")
    px = image.load()
    # Replace the crystal ball (x 98..152, y 70..133) with background
    # texture sampled from the clear band right of it.
    for y in range(70, 134):
        for x in range(98, 153):
            source_x = 153 + (x - 98) % 8
            px[x, y] = px[source_x, y] if px[source_x, y] != BLACK else WHITE
    # Back card with a gray face and a drop shadow.
    rect(px, 109, 75, 153, 121, BLACK)
    rect(px, 106, 72, 150, 118, WHITE)
    for y in range(75, 116):
        for x in range(109, 148):
            if (x + y) % 2 == 0:
                px[x, y] = GRAY
    outline(px, 106, 72, 150, 118)
    # Front card sitting on the books.
    rect(px, 99, 87, 145, 132, BLACK)
    rect(px, 96, 84, 142, 129, WHITE)
    outline(px, 96, 84, 142, 129)
    glyph(px, 103, 96, "A", 3)
    glyph(px, 122, 96, "a", 3)
    for x in range(102, 137, 3):
        rect(px, x, 122, x + 1, 123, BLACK)
    image.save(FIRMWARE / "cards.png")


def make_todo() -> None:
    image = Image.open(FIRMWARE / "status.png").convert("L")
    px = image.load()
    # Erase the three "..." dots on the sign.
    rect(px, 113, 61, 145, 73, WHITE)
    # Row 1: ticked box and a line of text.
    outline(px, 112, 54, 120, 62, 2)
    for i in range(4):
        px[114 + i, 57 + i] = BLACK
        px[115 + i, 57 + i] = BLACK
    for i in range(6):
        px[117 + i, 60 - i] = BLACK
        px[118 + i, 60 - i] = BLACK
    rect(px, 125, 57, 147, 59, BLACK)
    # Row 2: empty box and a line of text.
    outline(px, 113, 67, 121, 75, 2)
    rect(px, 126, 70, 147, 72, BLACK)
    image.save(FIRMWARE / "todo.png")


if __name__ == "__main__":
    make_cards()
    make_todo()
    print("wrote cards.png and todo.png")
