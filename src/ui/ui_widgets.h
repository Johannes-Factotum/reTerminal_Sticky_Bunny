#pragma once

#include <cstddef>

#include "canvas.h"

// Small shared drawing helpers for the Cards and To-Do apps. All text goes
// through the UTF-8 renderer so accented characters display correctly.

struct UiRect {
    int x;
    int y;
    int width;
    int height;

    bool contains(int px, int py) const
    {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

// Thick-bordered or filled button with centered label.
void ui_draw_button(Canvas &canvas,
                    const UiRect &rect,
                    const char *label,
                    bool filled,
                    int scale = 3);

// Button with a main label and a smaller caption line underneath.
void ui_draw_two_line_button(Canvas &canvas,
                             const UiRect &rect,
                             const char *label,
                             const char *caption,
                             bool filled);

void ui_draw_centered_text(Canvas &canvas,
                           int center_x,
                           int y,
                           const char *text,
                           int scale,
                           GrayLevel color = GrayLevel::Black);

// Dotted divider with a centre diamond, matching the launcher title style.
void ui_draw_divider(Canvas &canvas, int center_x, int half_width, int y);

// Word-wraps and draws text inside rect using the largest scale in
// [min_scale, max_scale] that fits. Returns the scale used.
int ui_draw_fitted_text(Canvas &canvas,
                        const UiRect &rect,
                        const char *text,
                        int max_scale,
                        int min_scale,
                        GrayLevel color = GrayLevel::Black);

// Same as ui_draw_fitted_text but only measures.
int ui_fit_scale(const UiRect &rect,
                 const char *text,
                 int max_scale,
                 int min_scale,
                 int *out_height = nullptr);

// Draws an empty or checked checkbox at (x, y) with given size.
void ui_draw_checkbox(Canvas &canvas, int x, int y, int size, bool checked);
