#include "text_utf8.h"

#include <cstring>

#include "font.h"

namespace {

enum class Mark : uint8_t {
    None,
    Acute,
    Grave,
    Circumflex,
    Tilde,
    Diaeresis,
    Ring,
    Cedilla,
    Slash,
    Rotate180,
};

struct GlyphPlan {
    FontGlyph glyph;
    Mark mark;
    bool lowercase;
};

constexpr FontGlyph kDotlessI = {{0x00, 0x44, 0x7C, 0x40, 0x00}};
constexpr FontGlyph kSharpS = {{0x7E, 0x01, 0x49, 0x36, 0x00}};
constexpr FontGlyph kEllipsis = {{0x40, 0x00, 0x40, 0x00, 0x40}};

// Two rows of five columns each, written left to right as strings.
struct MarkRows {
    const char *top;
    const char *bottom;
};

MarkRows mark_rows(Mark mark)
{
    switch (mark) {
    case Mark::Acute:
        return {"...X.", "..X.."};
    case Mark::Grave:
        return {".X...", "..X.."};
    case Mark::Circumflex:
        return {"..X..", ".X.X."};
    case Mark::Tilde:
        return {".XX.X", "X..X."};
    case Mark::Diaeresis:
        return {".....", ".X.X."};
    case Mark::Ring:
        return {".XXX.", ".X.X."};
    case Mark::Cedilla:
        return {"..X..", ".XX.."};
    default:
        return {".....", "....."};
    }
}

struct LatinMapping {
    char base;
    Mark mark;
};

// U+00C0 .. U+00FF mapped onto an ASCII base and a diacritic.
constexpr LatinMapping kLatin[] = {
    {'A', Mark::Grave},     {'A', Mark::Acute},  {'A', Mark::Circumflex},
    {'A', Mark::Tilde},     {'A', Mark::Diaeresis}, {'A', Mark::Ring},
    {'A', Mark::None},      {'C', Mark::Cedilla}, {'E', Mark::Grave},
    {'E', Mark::Acute},     {'E', Mark::Circumflex}, {'E', Mark::Diaeresis},
    {'I', Mark::Grave},     {'I', Mark::Acute},  {'I', Mark::Circumflex},
    {'I', Mark::Diaeresis}, {'D', Mark::None},   {'N', Mark::Tilde},
    {'O', Mark::Grave},     {'O', Mark::Acute},  {'O', Mark::Circumflex},
    {'O', Mark::Tilde},     {'O', Mark::Diaeresis}, {'x', Mark::None},
    {'O', Mark::Slash},     {'U', Mark::Grave},  {'U', Mark::Acute},
    {'U', Mark::Circumflex}, {'U', Mark::Diaeresis}, {'Y', Mark::Acute},
    {'P', Mark::None},      {'s', Mark::None},   // U+00DF handled separately
    {'a', Mark::Grave},     {'a', Mark::Acute},  {'a', Mark::Circumflex},
    {'a', Mark::Tilde},     {'a', Mark::Diaeresis}, {'a', Mark::Ring},
    {'a', Mark::None},      {'c', Mark::Cedilla}, {'e', Mark::Grave},
    {'e', Mark::Acute},     {'e', Mark::Circumflex}, {'e', Mark::Diaeresis},
    {'i', Mark::Grave},     {'i', Mark::Acute},  {'i', Mark::Circumflex},
    {'i', Mark::Diaeresis}, {'d', Mark::None},   {'n', Mark::Tilde},
    {'o', Mark::Grave},     {'o', Mark::Acute},  {'o', Mark::Circumflex},
    {'o', Mark::Tilde},     {'o', Mark::Diaeresis}, {'/', Mark::None},
    {'o', Mark::Slash},     {'u', Mark::Grave},  {'u', Mark::Acute},
    {'u', Mark::Circumflex}, {'u', Mark::Diaeresis}, {'y', Mark::Acute},
    {'p', Mark::None},      {'y', Mark::Diaeresis},
};
static_assert(sizeof(kLatin) / sizeof(kLatin[0]) == 64,
              "Latin-1 letter table must cover U+00C0..U+00FF");

bool is_lower_ascii(char c)
{
    return c >= 'a' && c <= 'z';
}

GlyphPlan plan_ascii(char c, Mark mark = Mark::None)
{
    return {font_get_glyph(c), mark, is_lower_ascii(c)};
}

GlyphPlan plan_for(uint32_t cp)
{
    if (cp < 0x80U) {
        return plan_ascii(static_cast<char>(cp));
    }
    if (cp >= 0xC0U && cp <= 0xFFU) {
        if (cp == 0xDFU) {
            return {kSharpS, Mark::None, false};
        }
        const LatinMapping mapping = kLatin[cp - 0xC0U];
        if (mapping.base == 'i' && mapping.mark != Mark::None) {
            return {kDotlessI, mapping.mark, true};
        }
        return plan_ascii(mapping.base, mapping.mark);
    }
    switch (cp) {
    case 0xA1U:  // ¡
        return {font_get_glyph('!'), Mark::Rotate180, false};
    case 0xBFU:  // ¿
        return {font_get_glyph('?'), Mark::Rotate180, false};
    case 0xAAU:
        return plan_ascii('a');
    case 0xBAU:
    case 0xB0U:
        return plan_ascii('o');
    case 0xABU:
        return plan_ascii('<');
    case 0xBBU:
        return plan_ascii('>');
    case 0xB7U:
        return plan_ascii('.');
    case 0x2018U:
    case 0x2019U:
    case 0x00B4U:
        return plan_ascii('\'');
    case 0x201CU:
    case 0x201DU:
        return plan_ascii('"');
    case 0x2013U:
    case 0x2014U:
        return plan_ascii('-');
    case 0x2026U:
        return {kEllipsis, Mark::None, false};
    case 0x20ACU:
        return plan_ascii('E');
    default:
        return plan_ascii('?');
    }
}

uint8_t reverse7(uint8_t bits)
{
    uint8_t out = 0U;
    for (int row = 0; row < 7; ++row) {
        if ((bits & (1U << row)) != 0U) {
            out |= static_cast<uint8_t>(1U << (6 - row));
        }
    }
    return out;
}

void draw_mark_row(Canvas &canvas,
                   int x,
                   int y,
                   const char *row,
                   int scale,
                   GrayLevel color)
{
    for (int column = 0; column < 5; ++column) {
        if (row[column] == 'X') {
            canvas.fill_rect(x + column * scale, y, scale, scale, color);
        }
    }
}

void draw_plan(Canvas &canvas,
               int x,
               int y,
               GlyphPlan plan,
               int scale,
               GrayLevel color)
{
    FontGlyph glyph = plan.glyph;
    if (plan.mark == Mark::Rotate180) {
        FontGlyph rotated = {};
        for (int column = 0; column < 5; ++column) {
            rotated.columns[column] = reverse7(glyph.columns[4 - column]);
        }
        glyph = rotated;
    } else if (plan.mark == Mark::Slash) {
        const int top = plan.lowercase ? 2 : 1;
        for (int column = 0; column < 5; ++column) {
            glyph.columns[column] |=
                static_cast<uint8_t>(1U << (top + 4 - column));
        }
    }

    for (int column = 0; column < kFontWidth; ++column) {
        for (int row = 0; row < kFontHeight; ++row) {
            if ((glyph.columns[column] & (1U << row)) != 0U) {
                canvas.fill_rect(x + column * scale,
                                 y + row * scale,
                                 scale,
                                 scale,
                                 color);
            }
        }
    }

    if (plan.mark == Mark::None || plan.mark == Mark::Rotate180 ||
        plan.mark == Mark::Slash) {
        return;
    }
    const MarkRows rows = mark_rows(plan.mark);
    int top_row = plan.lowercase ? -1 : -3;
    if (plan.mark == Mark::Cedilla) {
        top_row = 7;
    }
    draw_mark_row(canvas, x, y + top_row * scale, rows.top, scale, color);
    draw_mark_row(canvas, x, y + (top_row + 1) * scale,
                  rows.bottom, scale, color);
}

bool is_continuation(unsigned char byte)
{
    return (byte & 0xC0U) == 0x80U;
}

}  // namespace

uint32_t utf8_next(const char *&cursor, const char *end)
{
    const unsigned char lead = static_cast<unsigned char>(*cursor++);
    if (lead < 0x80U) {
        return lead;
    }
    int extra = 0;
    uint32_t cp = 0U;
    if ((lead & 0xE0U) == 0xC0U) {
        extra = 1;
        cp = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
        extra = 2;
        cp = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
        extra = 3;
        cp = lead & 0x07U;
    } else {
        return 0xFFFDU;
    }
    for (int i = 0; i < extra; ++i) {
        if (cursor >= end ||
            !is_continuation(static_cast<unsigned char>(*cursor))) {
            return 0xFFFDU;
        }
        cp = (cp << 6) | (static_cast<unsigned char>(*cursor++) & 0x3FU);
    }
    return cp;
}

size_t utf8_cell_count(const char *text, size_t length)
{
    if (text == nullptr) {
        return 0U;
    }
    size_t count = 0U;
    const char *cursor = text;
    const char *end = text + length;
    while (cursor < end) {
        utf8_next(cursor, end);
        ++count;
    }
    return count;
}

size_t utf8_cell_count(const char *text)
{
    return text == nullptr ? 0U : utf8_cell_count(text, std::strlen(text));
}

int utf8_text_width(const char *text, size_t length, int scale)
{
    const size_t cells = utf8_cell_count(text, length);
    return cells == 0U
               ? 0
               : (static_cast<int>(cells) * (kFontWidth + kFontSpacing) -
                  kFontSpacing) * scale;
}

int utf8_text_width(const char *text, int scale)
{
    return text == nullptr ? 0
                           : utf8_text_width(text, std::strlen(text), scale);
}

void utf8_draw_text(Canvas &canvas,
                    int x,
                    int y,
                    const char *text,
                    size_t length,
                    int scale,
                    GrayLevel color)
{
    if (text == nullptr || scale <= 0) {
        return;
    }
    const char *cursor = text;
    const char *end = text + length;
    int cursor_x = x;
    while (cursor < end) {
        const uint32_t cp = utf8_next(cursor, end);
        draw_plan(canvas, cursor_x, y, plan_for(cp), scale, color);
        cursor_x += (kFontWidth + kFontSpacing) * scale;
    }
}

void utf8_draw_text(Canvas &canvas,
                    int x,
                    int y,
                    const char *text,
                    int scale,
                    GrayLevel color)
{
    if (text != nullptr) {
        utf8_draw_text(canvas, x, y, text, std::strlen(text), scale, color);
    }
}

size_t utf8_wrap(const char *text,
                 size_t max_cells,
                 Utf8Line *lines,
                 size_t max_lines)
{
    if (text == nullptr || max_cells == 0U || max_lines == 0U) {
        return 0U;
    }
    const size_t total = std::strlen(text);
    const char *end = text + total;
    size_t line_count = 0U;
    const char *cursor = text;

    while (cursor < end && line_count < max_lines) {
        while (cursor < end && *cursor == ' ') {
            ++cursor;
        }
        if (cursor >= end) {
            break;
        }
        const char *line_start = cursor;
        const char *last_break = nullptr;  // end of last word that fits
        const char *scan = cursor;
        size_t cells = 0U;
        while (scan < end && *scan != '\n') {
            if (*scan == ' ') {
                last_break = scan;
            }
            if (cells == max_cells) {
                break;
            }
            utf8_next(scan, end);
            ++cells;
        }
        const char *line_end = scan;
        const char *next = scan;
        if (scan < end && *scan == '\n') {
            next = scan + 1;
        } else if (scan < end && cells == max_cells && *scan != ' ') {
            if (last_break != nullptr && last_break > line_start) {
                line_end = last_break;
                next = last_break + 1;
            }
        }
        while (line_end > line_start && line_end[-1] == ' ') {
            --line_end;
        }
        lines[line_count++] = {
            static_cast<size_t>(line_start - text),
            static_cast<size_t>(line_end - line_start)};
        cursor = next;
    }
    return line_count;
}

void utf8_pop_back(char *text)
{
    if (text == nullptr) {
        return;
    }
    size_t length = std::strlen(text);
    if (length == 0U) {
        return;
    }
    --length;
    while (length > 0U &&
           is_continuation(static_cast<unsigned char>(text[length]))) {
        --length;
    }
    text[length] = '\0';
}
