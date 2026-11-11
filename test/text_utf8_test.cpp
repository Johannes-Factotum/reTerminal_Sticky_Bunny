#include <cassert>
#include <cstring>
#include <vector>

#include "canvas.h"
#include "text_utf8.h"

namespace {

int count_black(const Canvas &canvas, int x0, int y0, int x1, int y1)
{
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (canvas.pixel_at(x, y) == GrayLevel::Black) {
                ++count;
            }
        }
    }
    return count;
}

}  // namespace

int main()
{
    // Cell counting treats each code point as one cell.
    assert(utf8_cell_count("abc") == 3U);
    assert(utf8_cell_count("ma\xC3\xB1" "ana") == 6U);       // mañana
    assert(utf8_cell_count("\xC2\xBF" "Qu\xC3\xA9?") == 5U);  // ¿Qué?
    assert(utf8_text_width("ab", 2) == (2 * 6 - 1) * 2);

    // Wrapping breaks on spaces and never exceeds the cell limit.
    Utf8Line lines[8];
    const char *text = "el perro come la manzana roja";
    size_t count = utf8_wrap(text, 10, lines, 8);
    assert(count == 4U);
    for (size_t i = 0; i < count; ++i) {
        assert(utf8_cell_count(text + lines[i].offset, lines[i].length) <=
               10U);
    }
    assert(std::strncmp(text + lines[0].offset, "el perro", 8) == 0);

    // Long words are split.
    count = utf8_wrap("abcdefghijklmno", 6, lines, 8);
    assert(count == 3U);
    assert(lines[0].length == 6U);

    // Multi-byte characters are never split across lines.
    const char *accents = "\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA";
    count = utf8_wrap(accents, 2, lines, 8);
    assert(count == 3U);
    assert(lines[0].length == 4U);

    // Truncation to max_lines.
    count = utf8_wrap("a b c d e f", 1, lines, 2);
    assert(count == 2U);

    // Backspace removes a whole code point.
    char buffer[16] = "ni\xC3\xB1o";
    utf8_pop_back(buffer);
    assert(std::strcmp(buffer, "ni\xC3\xB1") == 0);
    utf8_pop_back(buffer);
    assert(std::strcmp(buffer, "ni") == 0);

    // Rendering: accented lowercase letter draws a mark above the body.
    std::vector<uint8_t> pixels(800 / 4 * 480, 0xFF);
    Canvas canvas(800, 480, pixels.data(), pixels.size());
    canvas.clear();
    utf8_draw_text(canvas, 10, 20, "a", 2);
    const int plain_top = count_black(canvas, 10, 14, 22, 24);
    canvas.clear();
    utf8_draw_text(canvas, 10, 20, "\xC3\xA1", 2);
    const int accent_top = count_black(canvas, 10, 14, 22, 24);
    assert(accent_top > plain_top);

    // Capital accents sit above the cell.
    canvas.clear();
    utf8_draw_text(canvas, 10, 40, "\xC3\x89", 2);  // É
    assert(count_black(canvas, 10, 34, 22, 40) > 0);

    // Inverted question mark differs from the plain one.
    canvas.clear();
    utf8_draw_text(canvas, 10, 20, "?", 2);
    const int q_top = count_black(canvas, 10, 20, 22, 26);
    canvas.clear();
    utf8_draw_text(canvas, 10, 20, "\xC2\xBF", 2);
    const int inv_top = count_black(canvas, 10, 20, 22, 26);
    assert(q_top != inv_top);
    return 0;
}
