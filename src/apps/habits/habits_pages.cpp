#include "habits_pages.h"

#include <cstdio>
#include <cstring>

#include "canvas.h"
#include "habits_assets.h"
#include "pixel_asset.h"
#include "qrcodegen.h"
#include "text_utf8.h"
#include "ui_widgets.h"

namespace {

constexpr int kWidth = 480;
constexpr int kCenterX = kWidth / 2;

// Shared frame, matching the tutorial pages: a rounded panel with a footer.
constexpr int kFrameLeft = 12;
constexpr int kFrameRight = 467;
constexpr int kFrameTop = 96;
constexpr int kFooterTop = 676;
constexpr int kFrameBottom = 788;

// Step page footer buttons.
constexpr UiRect kSkipRect = {24, 690, 196, 86};
constexpr UiRect kDoneRect = {260, 690, 196, 86};
constexpr UiRect kStepHomeRect = {0, 0, 150, 90};

// Start page.
constexpr UiRect kStartRect = {40, 604, 400, 76};
constexpr UiRect kPhoneRect = {70, 700, 340, 60};

// Complete / phone pages.
constexpr UiRect kBackRect = {40, 690, 400, 76};

void draw_badge(Canvas &canvas, int cx, int cy, const char *text)
{
    canvas.fill_circle(cx, cy, 27, GrayLevel::Black);
    ui_draw_centered_text(canvas, cx, cy - 10, text, 3, GrayLevel::White);
}

void draw_frame(Canvas &canvas, int top, int bottom, bool footer)
{
    constexpr int kCut = 10;
    for (int t = 0; t < 3; ++t) {
        canvas.draw_line(kFrameLeft + kCut, top + t, kFrameRight - kCut,
                         top + t);
        canvas.draw_line(kFrameLeft + kCut, bottom - t, kFrameRight - kCut,
                         bottom - t);
        canvas.draw_line(kFrameLeft + t, top + kCut, kFrameLeft + t,
                         bottom - kCut);
        canvas.draw_line(kFrameRight - t, top + kCut, kFrameRight - t,
                         bottom - kCut);
        canvas.draw_line(kFrameLeft + t, top + kCut, kFrameLeft + kCut,
                         top + t);
        canvas.draw_line(kFrameRight - t, top + kCut, kFrameRight - kCut,
                         top + t);
        canvas.draw_line(kFrameLeft + t, bottom - kCut, kFrameLeft + kCut,
                         bottom - t);
        canvas.draw_line(kFrameRight - t, bottom - kCut, kFrameRight - kCut,
                         bottom - t);
    }
    if (footer) {
        canvas.fill_rect(kFrameLeft, kFooterTop, kFrameRight - kFrameLeft,
                         3, GrayLevel::Black);
    }
}

// Row of small markers: filled = done, crossed = skipped, ring = pending,
// enlarged ring = current.
void draw_progress(Canvas &canvas, const HabitResult *results, size_t count,
                   size_t current, int y)
{
    constexpr int kPitch = 30;
    const int left = kCenterX - static_cast<int>(count - 1U) * kPitch / 2;
    for (size_t i = 0U; i < count; ++i) {
        const int cx = left + static_cast<int>(i) * kPitch;
        const HabitResult result = results[i];
        if (result == HabitResult::Done) {
            canvas.fill_circle(cx, y, 8, GrayLevel::Black);
        } else if (result == HabitResult::Skipped) {
            canvas.draw_circle(cx, y, 8, GrayLevel::Black);
            canvas.draw_line(cx - 5, y - 5, cx + 5, y + 5, GrayLevel::Black);
            canvas.draw_line(cx - 5, y + 5, cx + 5, y - 5, GrayLevel::Black);
        } else {
            canvas.draw_circle(cx, y, 8, GrayLevel::Black);
            if (i == current) {
                canvas.draw_circle(cx, y, 11, GrayLevel::Black);
                canvas.draw_circle(cx, y, 12, GrayLevel::Black);
            }
        }
    }
}

void draw_check(Canvas &canvas, int x, int y, int s, GrayLevel color)
{
    for (int t = -2; t <= 1; ++t) {
        canvas.draw_line(x, y + t, x + s / 3, y + s / 3 + t, color);
        canvas.draw_line(x + s / 3, y + s / 3 + t, x + s, y - s / 2 + t,
                         color);
    }
}

void draw_cross(Canvas &canvas, int x, int y, int s, GrayLevel color)
{
    for (int t = -1; t <= 1; ++t) {
        canvas.draw_line(x + t, y - s / 2, x + s + t, y + s / 2, color);
        canvas.draw_line(x + t, y + s / 2, x + s + t, y - s / 2, color);
    }
}

void draw_footer_button(Canvas &canvas, const UiRect &rect, const char *label,
                        bool filled, bool check)
{
    if (filled) {
        canvas.fill_rect(rect.x, rect.y, rect.width, rect.height,
                         GrayLevel::Black);
    } else {
        for (int t = 0; t < 3; ++t) {
            canvas.draw_rect(rect.x + t, rect.y + t, rect.width - t * 2,
                             rect.height - t * 2, GrayLevel::Black);
        }
    }
    const GrayLevel ink = filled ? GrayLevel::White : GrayLevel::Black;
    const int text_w = utf8_text_width(label, 4);
    const int total = text_w + 40;
    const int x = rect.x + (rect.width - total) / 2;
    const int cy = rect.y + rect.height / 2;
    if (check) {
        draw_check(canvas, x, cy, 26, ink);
    } else {
        draw_cross(canvas, x + 2, cy, 20, ink);
    }
    utf8_draw_text(canvas, x + 40, cy - 14, label, 4, ink);
}

}  // namespace

void habits_page_render_start(Canvas &canvas, const HabitsStartView &view)
{
    const HabitsConfig &config = *view.config;
    canvas.clear(GrayLevel::White);
    const UiRect title = {24, 40, 330, 56};
    ui_draw_fitted_text(canvas, title, config.name, 4, 2);
    char streak[24];
    std::snprintf(streak, sizeof(streak), "%u", view.streak_days);
    // Kept below the battery indicator in the top-right corner.
    draw_badge(canvas, 412, 68, streak);
    ui_draw_centered_text(canvas, 412, 102, "DAY STREAK", 1);

    constexpr int kListTop = 116;
    draw_frame(canvas, kListTop, 580, false);
    size_t done = 0U;
    for (size_t i = 0U; i < config.count; ++i) {
        // Up to 12 habits share the list: rows shrink from 56 to 37 px.
        const int pitch =
            config.count <= 8U ? 56 : (580 - kListTop - 24) / config.count;
        const int y = kListTop + 18 + static_cast<int>(i) * pitch;
        const HabitResult result = view.results[i];
        char number[4];
        std::snprintf(number, sizeof(number), "%u",
                      static_cast<unsigned>(i + 1U));
        canvas.fill_circle(52, y + 18, 16, GrayLevel::Black);
        ui_draw_centered_text(canvas, 52, y + 11, number, 2,
                              GrayLevel::White);
        const UiRect text = {84, y, 300, 40};
        utf8_draw_text(canvas, text.x, y + 10, config.items[i].text, 2);
        if (result == HabitResult::Done) {
            ++done;
            draw_check(canvas, 410, y + 22, 30, GrayLevel::Black);
        } else if (result == HabitResult::Skipped) {
            draw_cross(canvas, 414, y + 20, 22, GrayLevel::Black);
        }
        if (pitch >= 50) {
            for (int x = 40; x < 440; x += 6) {
                canvas.draw_pixel(x, y + 46, GrayLevel::Black);
            }
        }
    }
    (void)done;
    ui_draw_button(canvas, kStartRect, "START ROUTINE", true, 3);
    ui_draw_button(canvas, kPhoneRect, "EDIT ON PHONE", false, 2);
}

void habits_page_render_step(Canvas &canvas, const HabitsStepView &view)
{
    const HabitsConfig &config = *view.config;
    const Habit &habit = config.items[view.index];
    canvas.clear(GrayLevel::White);

    char number[4];
    std::snprintf(number, sizeof(number), "%u",
                  static_cast<unsigned>(view.index + 1U));
    draw_badge(canvas, 52, 50, number);
    const UiRect title = {92, 24, 270, 30};
    ui_draw_fitted_text(canvas, title, config.name, 3, 2);
    draw_progress(canvas, view.results, config.count, view.index, 70);

    draw_frame(canvas, kFrameTop, kFrameBottom, true);
    const HabitArtId art = habit.icon < static_cast<uint8_t>(HabitIcon::Count)
                               ? static_cast<HabitArtId>(habit.icon)
                               : HabitArtId::Generic;
    pixel_asset_draw_centered(canvas, kCenterX, 268, habits_art_asset(art));

    const UiRect text = {36, 432, 408, 170};
    ui_draw_fitted_text(canvas, text, habit.text, 5, 3);

    char counter[16];
    std::snprintf(counter, sizeof(counter), "%u / %u",
                  static_cast<unsigned>(view.index + 1U),
                  static_cast<unsigned>(config.count));
    ui_draw_centered_text(canvas, kCenterX, 628, counter, 3);

    draw_footer_button(canvas, kSkipRect, "SKIP", false, false);
    draw_footer_button(canvas, kDoneRect, "DONE", true, true);
}

void habits_page_render_complete(Canvas &canvas,
                                 const HabitsCompleteView &view)
{
    canvas.clear(GrayLevel::White);
    ui_draw_centered_text(canvas, kCenterX, 40, "ROUTINE DONE!", 4);
    draw_frame(canvas, kFrameTop, 660, false);
    pixel_asset_draw_centered(canvas, kCenterX, 280,
                              habits_art_asset(HabitArtId::Celebrate));
    char line[40];
    std::snprintf(line, sizeof(line), "%u DONE  /  %u SKIPPED",
                  static_cast<unsigned>(view.done),
                  static_cast<unsigned>(view.skipped));
    ui_draw_centered_text(canvas, kCenterX, 470, line, 3);
    std::snprintf(line, sizeof(line), "STREAK: %u DAYS", view.streak_days);
    ui_draw_centered_text(canvas, kCenterX, 530, line, 3);
    ui_draw_centered_text(canvas, kCenterX, 590, "SEE YOU TOMORROW", 2);
    ui_draw_button(canvas, kBackRect, "BACK", true, 3);
}

void habits_page_render_phone(Canvas &canvas, const HabitsPhoneView &view)
{
    canvas.clear(GrayLevel::White);
    ui_draw_centered_text(canvas, kCenterX, 34, "EDIT ON PHONE", 4);
    draw_frame(canvas, 84, 666, false);

    draw_badge(canvas, 56, 130, "1");
    utf8_draw_text(canvas, 96, 112, "JOIN WI-FI", 3);
    char wifi[96];
    std::snprintf(wifi, sizeof(wifi), "WIFI:T:WPA;S:%s;P:%s;;", view.ssid,
                  view.password);
    const int qr1 = habits_draw_qr(canvas, 40, 170, wifi, 5);
    utf8_draw_text(canvas, 50 + qr1, 186, view.ssid, 2);
    utf8_draw_text(canvas, 50 + qr1, 214, "PASSWORD:", 2);
    utf8_draw_text(canvas, 50 + qr1, 240, view.password, 2);

    draw_badge(canvas, 56, 396, "2");
    utf8_draw_text(canvas, 96, 378, "OPEN THE PAGE", 3);
    const int qr2 = habits_draw_qr(canvas, 40, 436, view.url, 5);
    utf8_draw_text(canvas, 50 + qr2, 470, view.url, 2);
    utf8_draw_text(canvas, 50 + qr2, 510,
                   view.phone_connected ? "PHONE CONNECTED" : "WAITING...",
                   2);

    ui_draw_button(canvas, kBackRect, "DONE", true, 3);
}

HabitsAction habits_page_action_at(HabitsPage page, int x, int y)
{
    switch (page) {
    case HabitsPage::Start:
        if (kStartRect.contains(x, y)) {
            return HabitsAction::Start;
        }
        return kPhoneRect.contains(x, y) ? HabitsAction::PhoneSetup
                                         : HabitsAction::None;
    case HabitsPage::Step:
        if (kSkipRect.contains(x, y)) {
            return HabitsAction::Skip;
        }
        if (kDoneRect.contains(x, y)) {
            return HabitsAction::Done;
        }
        return kStepHomeRect.contains(x, y) ? HabitsAction::Home
                                            : HabitsAction::None;
    case HabitsPage::Complete:
    case HabitsPage::Phone:
        return kBackRect.contains(x, y) ? HabitsAction::Back
                                        : HabitsAction::None;
    }
    return HabitsAction::None;
}

int habits_draw_qr(Canvas &canvas, int x, int y, const char *text,
                   int module_px)
{
    static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
    static uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
    if (!qrcodegen_encodeText(text, temp, qr, qrcodegen_Ecc_MEDIUM, 1, 6,
                              qrcodegen_Mask_AUTO, true)) {
        return 0;
    }
    const int size = qrcodegen_getSize(qr);
    constexpr int kQuiet = 2;
    const int total = (size + kQuiet * 2) * module_px;
    canvas.fill_rect(x, y, total, total, GrayLevel::White);
    for (int my = 0; my < size; ++my) {
        for (int mx = 0; mx < size; ++mx) {
            if (qrcodegen_getModule(qr, mx, my)) {
                canvas.fill_rect(x + (mx + kQuiet) * module_px,
                                 y + (my + kQuiet) * module_px, module_px,
                                 module_px, GrayLevel::Black);
            }
        }
    }
    return total;
}
