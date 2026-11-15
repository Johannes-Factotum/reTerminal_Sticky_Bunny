#pragma once

#include <cstddef>
#include <cstdint>

#include "habits_model.h"

class Canvas;

// Portrait (480 x 800 logical) pages for the habit routine app.

enum class HabitsAction : uint8_t {
    None,
    Start,
    PhoneSetup,
    Done,
    Skip,
    Back,
    Home,
};

struct HabitsStartView {
    const HabitsConfig *config;
    const HabitResult *results;  // today's results, config->count entries
    uint16_t streak_days;
};

struct HabitsStepView {
    const HabitsConfig *config;
    const HabitResult *results;
    size_t index;  // 0-based habit shown
};

struct HabitsCompleteView {
    size_t done;
    size_t skipped;
    uint16_t streak_days;
};

struct HabitsPhoneView {
    const char *ssid;
    const char *password;
    const char *url;
    bool phone_connected;
};

void habits_page_render_start(Canvas &canvas, const HabitsStartView &view);
void habits_page_render_step(Canvas &canvas, const HabitsStepView &view);
void habits_page_render_complete(Canvas &canvas,
                                 const HabitsCompleteView &view);
void habits_page_render_phone(Canvas &canvas, const HabitsPhoneView &view);

enum class HabitsPage : uint8_t {
    Start,
    Step,
    Complete,
    Phone,
};

HabitsAction habits_page_action_at(HabitsPage page, int x, int y);

// Draws a QR code for text with its top-left at (x, y); returns the size
// in pixels (0 when the text does not fit).
int habits_draw_qr(Canvas &canvas, int x, int y, const char *text,
                   int module_px);
