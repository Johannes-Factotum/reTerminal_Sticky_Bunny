#pragma once

#include <cstddef>
#include <cstdint>

#include "canvas.h"

// UTF-8 text drawing on top of the 5x7 ASCII font. Latin-1 letters with
// diacritics (á é í ó ú ñ ü ç à è ...) are drawn as the base glyph plus a
// small mark; inverted ¿ and ¡ are drawn as mirrored ? and !. Capital
// accents are drawn up to three font rows above the cell, so callers should
// leave about 3 * scale pixels of space above each text line.

// Advances one UTF-8 code point and returns it (0xFFFD for invalid bytes).
uint32_t utf8_next(const char *&cursor, const char *end);

// Number of visible character cells in the first `length` bytes.
size_t utf8_cell_count(const char *text, size_t length);
size_t utf8_cell_count(const char *text);

// Pixel width of the text at the given scale (no trailing spacing).
int utf8_text_width(const char *text, size_t length, int scale);
int utf8_text_width(const char *text, int scale);

void utf8_draw_text(Canvas &canvas,
                    int x,
                    int y,
                    const char *text,
                    size_t length,
                    int scale,
                    GrayLevel color = GrayLevel::Black);
void utf8_draw_text(Canvas &canvas,
                    int x,
                    int y,
                    const char *text,
                    int scale,
                    GrayLevel color = GrayLevel::Black);

struct Utf8Line {
    size_t offset;
    size_t length;
};

// Greedy word wrap into lines of at most max_cells character cells. Words
// longer than one line are split. Returns the number of lines produced; text
// that does not fit into max_lines is truncated (last line ends early).
size_t utf8_wrap(const char *text,
                 size_t max_cells,
                 Utf8Line *lines,
                 size_t max_lines);

// Removes the last UTF-8 code point from a NUL-terminated buffer.
void utf8_pop_back(char *text);
