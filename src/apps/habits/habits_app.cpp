#include "habits_app.h"

#include <cstring>

#include "app_log.h"
#include "canvas.h"
#include "flashcards_srs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "habits_model.h"
#include "habits_pages.h"
#include "habits_storage.h"
#include "habits_web.h"
#include "sticky_app_lifecycle.h"
#include "sticky_buzzer.h"
#include "sticky_display.h"
#include "sticky_rtc.h"
#include "sticky_touch.h"

namespace {

constexpr char kTag[] = "habits_app";
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(40);
constexpr uint32_t kTaskStackSize = 6144U;
constexpr UBaseType_t kTaskPriority = 3U;
constexpr uint32_t kIdleSleepTimeoutMs = 5U * 60U * 1000U;
constexpr unsigned kFullRefreshEveryFrames = 6U;

Canvas *s_canvas = nullptr;
TaskHandle_t s_task = nullptr;
StickyAppLifecycle s_lifecycle = {};

HabitsConfig s_config = {};
HabitsProgress s_progress = {};
HabitsPage s_page = HabitsPage::Start;
size_t s_index = 0U;
uint16_t s_today = 1U;
unsigned s_partial_frames = 0U;

// Phone page state.
char s_ssid[33] = {};
char s_password[16] = {};
bool s_web_running = false;
bool s_phone_connected = false;

uint16_t read_today()
{
    StickyRtcDateTime now = {};
    if (sticky_rtc_is_ready() && sticky_rtc_read(now) == ESP_OK) {
        const uint16_t day =
            flashcards_days_from_civil(now.year, now.month, now.day);
        if (day != 0U) {
            return day;
        }
    }
    return s_progress.day != 0U ? s_progress.day : s_today;
}

void roll_day()
{
    s_today = read_today();
    if (s_progress.day != s_today) {
        habits_progress_roll(s_progress, s_today);
        habits_storage_save_progress(s_progress);
    }
}

void render(bool partial)
{
    s_canvas->set_rotation(CanvasRotation::Deg90CounterClockwise);
    HabitResult results[kHabitsMax] = {};
    for (size_t i = 0U; i < kHabitsMax; ++i) {
        results[i] = static_cast<HabitResult>(s_progress.results[i]);
    }
    switch (s_page) {
    case HabitsPage::Start:
        habits_page_render_start(
            *s_canvas,
            {&s_config, results, habits_streak(s_progress, s_today)});
        break;
    case HabitsPage::Step:
        habits_page_render_step(*s_canvas, {&s_config, results, s_index});
        break;
    case HabitsPage::Complete:
        habits_page_render_complete(
            *s_canvas,
            {habits_count_result(s_progress, s_config.count,
                                 HabitResult::Done),
             habits_count_result(s_progress, s_config.count,
                                 HabitResult::Skipped),
             habits_streak(s_progress, s_today)});
        break;
    case HabitsPage::Phone:
        habits_page_render_phone(
            *s_canvas,
            {s_ssid, s_password, "http://192.168.4.1", s_phone_connected});
        break;
    }
    esp_err_t result = ESP_OK;
    if (partial && s_partial_frames < kFullRefreshEveryFrames) {
        ++s_partial_frames;
        result = sticky_display_refresh_partial();
    } else {
        s_partial_frames = 0U;
        result = sticky_display_refresh_monochrome();
    }
    if (result != ESP_OK) {
        STICKY_LOGE(kTag, "habits=display result=%s",
                    esp_err_to_name(result));
    }
    sticky_touch_clear_press();
}

void stop_web()
{
    if (s_web_running) {
        habits_web_stop();
        s_web_running = false;
    }
}

void go(HabitsPage page, bool partial)
{
    if (page != HabitsPage::Phone) {
        stop_web();
    }
    s_page = page;
    render(partial);
}

void start_routine()
{
    roll_day();
    if (s_config.count == 0U) {
        return;
    }
    // Run again from the top if everything is already resolved today.
    if (habits_next_pending(s_progress, s_config.count, 0U) >=
        s_config.count) {
        std::memset(s_progress.results, 0, sizeof(s_progress.results));
    }
    s_index = habits_next_pending(s_progress, s_config.count, 0U);
    go(HabitsPage::Step, false);
}

void answer(HabitResult result)
{
    s_progress.results[s_index] = static_cast<uint8_t>(result);
    if (result == HabitResult::Done) {
        sticky_buzzer_play_pattern(StickyBuzzerPattern::TaskDone);
    }
    const size_t next =
        habits_next_pending(s_progress, s_config.count, s_index + 1U);
    const size_t wrapped =
        next < s_config.count ? next
                              : habits_next_pending(s_progress,
                                                    s_config.count, 0U);
    if (wrapped >= s_config.count) {
        habits_progress_complete(s_progress, s_today);
        habits_storage_save_progress(s_progress);
        sticky_buzzer_play_pattern(StickyBuzzerPattern::SessionComplete);
        STICKY_LOGI(kTag, "habits=routine complete streak=%u",
                    habits_streak(s_progress, s_today));
        go(HabitsPage::Complete, false);
        return;
    }
    habits_storage_save_progress(s_progress);
    s_index = wrapped;
    render(true);
}

void open_phone_page()
{
    const esp_err_t result = habits_web_start(
        s_config, s_ssid, sizeof(s_ssid), s_password, sizeof(s_password));
    s_web_running = result == ESP_OK;
    s_phone_connected = false;
    if (!s_web_running) {
        STICKY_LOGE(kTag, "habits=web start=%s", esp_err_to_name(result));
        std::strncpy(s_ssid, "WI-FI ERROR", sizeof(s_ssid) - 1U);
        s_password[0] = '\0';
    }
    go(HabitsPage::Phone, false);
}

void apply_saved_config(const HabitsConfig &config)
{
    s_config = config;
    habits_storage_save_config(s_config);
    // Today's results no longer match the edited list.
    std::memset(s_progress.results, 0, sizeof(s_progress.results));
    habits_storage_save_progress(s_progress);
    sticky_buzzer_play_pattern(StickyBuzzerPattern::TaskDone);
    STICKY_LOGI(kTag, "habits=config updated habits=%u",
                static_cast<unsigned>(s_config.count));
}

void handle(HabitsAction action)
{
    switch (action) {
    case HabitsAction::None:
        return;
    case HabitsAction::Start:
        start_routine();
        return;
    case HabitsAction::PhoneSetup:
        open_phone_page();
        return;
    case HabitsAction::Done:
        answer(HabitResult::Done);
        return;
    case HabitsAction::Skip:
        answer(HabitResult::Skipped);
        return;
    case HabitsAction::Back:
    case HabitsAction::Home:
        roll_day();
        go(HabitsPage::Start, false);
        return;
    }
}

void task(void *)
{
    roll_day();
    render(false);
    while (true) {
        if (sticky_app_lifecycle_checkpoint(s_lifecycle)) {
            sticky_touch_clear_press();
            if (s_page == HabitsPage::Phone) {
                // The launcher took over; Wi-Fi was stopped on pause.
                s_page = HabitsPage::Start;
            }
            roll_day();
            if (s_page == HabitsPage::Step &&
                s_index >= s_config.count) {
                s_page = HabitsPage::Start;
            }
            render(false);
        }

        if (s_page == HabitsPage::Phone) {
            HabitsConfig saved = {};
            if (habits_web_take_saved(saved)) {
                apply_saved_config(saved);
                render(true);
            }
            const bool connected = habits_web_phone_connected();
            if (connected != s_phone_connected) {
                s_phone_connected = connected;
                render(true);
            }
        }

        StickyTouchPress press = {};
        if (sticky_touch_take_press(press)) {
            int x = 0;
            int y = 0;
            s_canvas->physical_to_logical(press.x, press.y, x, y);
            handle(habits_page_action_at(s_page, x, y));
        }
        vTaskDelay(kPollInterval);
    }
}

}  // namespace

esp_err_t habits_app_start(Canvas &canvas)
{
    app_log_register_tag(kTag);
    if (s_task != nullptr) {
        return ESP_OK;
    }
    s_canvas = &canvas;
    habits_storage_load(s_config, s_progress);
    if (xTaskCreate(task, "habits_app", kTaskStackSize, nullptr,
                    kTaskPriority, &s_task) != pdPASS) {
        s_canvas = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t habits_app_pause()
{
    const esp_err_t result = sticky_app_lifecycle_pause(s_lifecycle, s_task);
    if (result == ESP_OK) {
        stop_web();  // safe: the app task is parked at its checkpoint
    }
    return result;
}

esp_err_t habits_app_resume()
{
    if (s_task == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    sticky_app_lifecycle_resume(s_lifecycle);
    return ESP_OK;
}

bool habits_app_power_sleep_allowed()
{
    return s_page != HabitsPage::Phone;
}

esp_err_t habits_app_prepare_power_sleep()
{
    return habits_app_pause();
}

uint32_t habits_app_power_sleep_timeout_ms()
{
    return s_page == HabitsPage::Phone ? 0U : kIdleSleepTimeoutMs;
}
