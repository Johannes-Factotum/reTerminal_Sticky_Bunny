#include "ui_widgets.h"

#include <cstring>

#include "text_utf8.h"

namespace {

constexpr int kCellWidth = 6;    // 5 px glyph + 1 px spacing
constexpr int kLinePitch = 11;   // 7 px glyph + 3 accent rows + 1 gap
constexpr int kAccentRows = 3;
constexpr size_t kMaxLines = 24U;

void draw_frame(Canvas &canvas, const UiRect &rect, int thickness)
{
    for (int i = 0; i < thickness; ++i) {
        canvas.draw_rect(rect.x + i,
                         rect.y + i,
                         rect.width - i * 2,
                         rect.height - i * 2,
                         GrayLevel::Black);
    }
}

size_t wrap_for_scale(const UiRect &rect,
                      const char *text,
                      int scale,
                      Utf8Line *lines)
{
    const int cells = (rect.width + 1) / (kCellWidth * scale);
    if (cells <= 0) {
        return 0U;
    }
    return utf8_wrap(text, static_cast<size_t>(cells), lines, kMaxLines);
}

bool fits(const UiRect &rect, const char *text, int scale, int &height)
{
    Utf8Line lines[kMaxLines];
    const size_t count = wrap_for_scale(rect, text, scale, lines);
    if (count == 0U) {
        height = 0;
        return true;
    }
    size_t consumed = lines[count - 1U].offset + lines[count - 1U].length;
    while (text[consumed] == ' ' || text[consumed] == '\n') {
        ++consumed;
    }
    height = static_cast<int>(count) * kLinePitch * scale - scale;
    return text[consumed] == '\0' && height <= rect.height;
}

}  // namespace

void ui_draw_centered_text(Canvas &canvas,
                           int center_x,
                           int y,
                           const char *text,
                           int scale,
                           GrayLevel color)
{
    utf8_draw_text(canvas,
                   center_x - utf8_text_width(text, scale) / 2,
                   y,
                   text,
                   scale,
                   color);
}

void ui_draw_button(Canvas &canvas,
                    const UiRect &rect,
                    const char *label,
                    bool filled,
                    int scale)
{
    if (filled) {
        canvas.fill_rect(rect.x, rect.y, rect.width, rect.height,
                         GrayLevel::Black);
    } else {
        canvas.fill_rect(rect.x, rect.y, rect.width, rect.height,
                         GrayLevel::White);
        draw_frame(canvas, rect, 3);
    }
    const GrayLevel ink = filled ? GrayLevel::White : GrayLevel::Black;
    ui_draw_centered_text(canvas,
                          rect.x + rect.width / 2,
                          rect.y + (rect.height - 7 * scale) / 2,
                          label,
                          scale,
                          ink);
}

void ui_draw_two_line_button(Canvas &canvas,
                             const UiRect &rect,
                             const char *label,
                             const char *caption,
                             bool filled)
{
    if (filled) {
        canvas.fill_rect(rect.x, rect.y, rect.width, rect.height,
                         GrayLevel::Black);
    } else {
        canvas.fill_rect(rect.x, rect.y, rect.width, rect.height,
                         GrayLevel::White);
        draw_frame(canvas, rect, 3);
    }
    const GrayLevel ink = filled ? GrayLevel::White : GrayLevel::Black;
    const int center_x = rect.x + rect.width / 2;
    constexpr int kLabelScale = 3;
    constexpr int kCaptionScale = 2;
    const int content = 7 * kLabelScale + 10 + 7 * kCaptionScale;
    const int top = rect.y + (rect.height - content) / 2;
    ui_draw_centered_text(canvas, center_x, top, label, kLabelScale, ink);
    if (caption != nullptr) {
        ui_draw_centered_text(canvas,
                              center_x,
                              top + 7 * kLabelScale + 10,
                              caption,
                              kCaptionScale,
                              ink);
    }
}

void ui_draw_divider(Canvas &canvas, int center_x, int half_width, int y)
{
    for (int x = center_x - half_width; x <= center_x + half_width; x += 12) {
        canvas.fill_rect(x, y, 3, 3, GrayLevel::Black);
    }
    canvas.fill_rect(center_x - 4, y - 3, 9, 9, GrayLevel::Black);
}

int ui_fit_scale(const UiRect &rect,
                 const char *text,
                 int max_scale,
                 int min_scale,
                 int *out_height)
{
    int height = 0;
    for (int scale = max_scale; scale > min_scale; --scale) {
        if (fits(rect, text, scale, height)) {
            if (out_height != nullptr) {
                *out_height = height;
            }
            return scale;
        }
    }
    fits(rect, text, min_scale, height);
    if (out_height != nullptr) {
        *out_height = height > rect.height ? rect.height : height;
    }
    return min_scale;
}

int ui_draw_fitted_text(Canvas &canvas,
                        const UiRect &rect,
                        const char *text,
                        int max_scale,
                        int min_scale,
                        GrayLevel color)
{
    if (text == nullptr || *text == '\0') {
        return min_scale;
    }
    int height = 0;
    const int scale = ui_fit_scale(rect, text, max_scale, min_scale, &height);
    Utf8Line lines[kMaxLines];
    const size_t count = wrap_for_scale(rect, text, scale, lines);
    const int pitch = kLinePitch * scale;
    int y = rect.y + (rect.height - height) / 2 + kAccentRows * scale;
    for (size_t i = 0U; i < count; ++i) {
        if (y + 7 * scale > rect.y + rect.height + kAccentRows * scale) {
            break;
        }
        const char *line = text + lines[i].offset;
        const int width = utf8_text_width(line, lines[i].length, scale);
        utf8_draw_text(canvas,
                       rect.x + (rect.width - width) / 2,
                       y,
                       line,
                       lines[i].length,
                       scale,
                       color);
        y += pitch;
    }
    return scale;
}

void ui_draw_checkbox(Canvas &canvas, int x, int y, int size, bool checked)
{
    canvas.fill_rect(x, y, size, size, GrayLevel::White);
    for (int i = 0; i < 3; ++i) {
        canvas.draw_rect(x + i, y + i, size - i * 2, size - i * 2,
                         GrayLevel::Black);
    }
    if (!checked) {
        return;
    }
    // Bold tick mark.
    const int s = size;
    for (int t = -2; t <= 2; ++t) {
        canvas.draw_line(x + s / 5, y + s / 2 + t,
                         x + s * 2 / 5, y + s * 3 / 4 + t, GrayLevel::Black);
        canvas.draw_line(x + s * 2 / 5, y + s * 3 / 4 + t,
                         x + s * 4 / 5, y + s / 4 + t, GrayLevel::Black);
    }
}
