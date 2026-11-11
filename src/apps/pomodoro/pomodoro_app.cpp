#include "pomodoro_app.h"

#include <cstdint>

#include "app_log.h"
#include "canvas.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pomodoro_countdown.h"
#include "pomodoro_custom_input.h"
#include "pomodoro_pages.h"
#include "pomodoro_render_policy.h"
#include "sticky_app_lifecycle.h"
#include "sticky_buzzer.h"
#include "sticky_display.h"
#include "sticky_touch.h"

namespace {

constexpr char kTag[] = "pomodoro_app";
constexpr uint32_t kDefaultDurationSeconds = 15U * 60U;
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(40);
constexpr uint32_t kTaskStackSize = 6144;
constexpr UBaseType_t kTaskPriority = 3;
constexpr uint32_t kSleepSnapshotMagic = 0x504F4D50U;
constexpr uint32_t kIdleSleepTimeoutMs = 5U * 60U * 1000U;
constexpr int64_t kCustomCleanupDelayUs = 2LL * 1000LL * 1000LL;

struct PomodoroSleepSnapshot {
    uint32_t magic;
    PomodoroPage page;
    PomodoroTimeField active_field;
    uint32_t selected_seconds;
    uint32_t total_seconds;
    uint8_t custom_hours;
    uint8_t custom_minutes;
    uint8_t custom_seconds;
    bool replace_field_on_digit;
    bool custom_backspace_pending;
    int64_t paused_remaining_us;
};

enum class CustomActionResult {
    None,
    Redraw,
    PageChanged,
};

RTC_NOINIT_ATTR PomodoroSleepSnapshot s_sleep_snapshot;

Canvas *s_canvas = nullptr;
TaskHandle_t s_app_task = nullptr;
StickyAppLifecycle s_lifecycle = {};

PomodoroPage s_page = PomodoroPage::Setup;
PomodoroTimeField s_active_field = PomodoroTimeField::Minutes;
uint32_t s_selected_seconds = kDefaultDurationSeconds;
uint32_t s_total_seconds = kDefaultDurationSeconds;
uint8_t s_custom_hours = 0;
uint8_t s_custom_minutes = 15;
uint8_t s_custom_seconds = 0;
bool s_replace_field_on_digit = true;
bool s_custom_backspace_pending = false;
int64_t s_custom_cleanup_deadline_us = 0;
int64_t s_timer_deadline_us = 0;
int64_t s_paused_remaining_us = 0;
uint32_t s_displayed_remaining_seconds = 0;
CanvasRotation s_display_rotation =
    CanvasRotation::Deg90CounterClockwise;

void log_refresh_failure(const char *mode, esp_err_t result)
{
    if (result != ESP_OK) {
        STICKY_LOGE(kTag,
                    "pomodoro=display refresh=%s result=%s",
                    mode,
                    esp_err_to_name(result));
    }
}

void render_current_page(
    bool partial_refresh = false,
    PomodoroRenderReason reason = PomodoroRenderReason::InteractiveChange)
{
    s_canvas->set_rotation(s_display_rotation);
    switch (s_page) {
    case PomodoroPage::Setup:
        pomodoro_page_render_setup(*s_canvas, s_selected_seconds);
        break;
    case PomodoroPage::CustomTime:
        pomodoro_page_render_custom_time(*s_canvas,
                                         s_custom_hours,
                                         s_custom_minutes,
                                         s_custom_seconds,
                                         s_active_field);
        break;
    case PomodoroPage::Running:
        pomodoro_page_render_timer(*s_canvas,
                                   s_page,
                                   static_cast<uint32_t>(
                                       (s_paused_remaining_us + 999999LL) /
                                       1000000LL),
                                   s_total_seconds);
        break;
    case PomodoroPage::Paused:
        pomodoro_page_render_timer(*s_canvas,
                                   s_page,
                                   static_cast<uint32_t>(
                                       (s_paused_remaining_us + 999999LL) /
                                       1000000LL),
                                   s_total_seconds);
        break;
    case PomodoroPage::EndConfirmation:
        pomodoro_page_render_end_confirmation(
            *s_canvas,
            static_cast<uint32_t>((s_paused_remaining_us + 999999LL) /
                                  1000000LL),
            s_total_seconds);
        break;
    case PomodoroPage::Alarm:
        pomodoro_page_render_alarm(*s_canvas, s_total_seconds);
        break;
    }

    const esp_err_t result = partial_refresh
                                 ? sticky_display_refresh_partial()
                                 : sticky_display_refresh_monochrome();
    log_refresh_failure(partial_refresh ? "partial" : "full", result);
    if (pomodoro_render_clears_pending_touch(reason)) {
        sticky_touch_clear_press();
    }
}

void change_page(PomodoroPage target, const char *reason, bool partial = false)
{
    const PomodoroPage previous = s_page;
    s_page = target;
    if (target != PomodoroPage::CustomTime) {
        s_custom_cleanup_deadline_us = 0;
    }
    STICKY_LOGI(kTag,
                "pomodoro=page from=%s to=%s reason=%s",
                pomodoro_page_name(previous),
                pomodoro_page_name(target),
                reason);
    render_current_page(partial);
}

uint8_t &active_custom_value()
{
    switch (s_active_field) {
    case PomodoroTimeField::Hours:
        return s_custom_hours;
    case PomodoroTimeField::Seconds:
        return s_custom_seconds;
    case PomodoroTimeField::Minutes:
    default:
        return s_custom_minutes;
    }
}

void select_custom_field(PomodoroTimeField field)
{
    s_active_field = field;
    s_replace_field_on_digit = true;
    s_custom_backspace_pending = false;
}

int action_digit(PomodoroAction action)
{
    if (action < PomodoroAction::Digit0 || action > PomodoroAction::Digit9) {
        return -1;
    }
    return static_cast<int>(action) - static_cast<int>(PomodoroAction::Digit0);
}

void enter_custom_digit(uint8_t digit)
{
    uint8_t &value = active_custom_value();
    pomodoro_custom_enter_digit(digit,
                                value,
                                s_replace_field_on_digit,
                                s_custom_backspace_pending);
    STICKY_LOGD(kTag,
                "pomodoro=custom_input field=%d value=%u",
                static_cast<int>(s_active_field),
                static_cast<unsigned>(value));
}

uint32_t custom_duration_seconds()
{
    return static_cast<uint32_t>(s_custom_hours) * 3600U +
           static_cast<uint32_t>(s_custom_minutes) * 60U +
           static_cast<uint32_t>(s_custom_seconds);
}

bool custom_time_is_valid()
{
    return s_custom_minutes < 60U && s_custom_seconds < 60U &&
           custom_duration_seconds() > 0U;
}

int64_t timer_remaining_us(int64_t now_us)
{
    return s_timer_deadline_us > now_us ? s_timer_deadline_us - now_us : 0;
}

uint32_t remaining_seconds_rounded_up(int64_t remaining_us)
{
    return static_cast<uint32_t>((remaining_us + 999999LL) / 1000000LL);
}

void start_timer()
{
    s_total_seconds = s_selected_seconds;
    s_paused_remaining_us = static_cast<int64_t>(s_total_seconds) * 1000000LL;
    s_timer_deadline_us = esp_timer_get_time() + s_paused_remaining_us;
    s_displayed_remaining_seconds = s_total_seconds;
    STICKY_LOGI(kTag,
                "pomodoro=timer state=started duration_s=%lu",
                static_cast<unsigned long>(s_total_seconds));
    sticky_buzzer_play_pattern(StickyBuzzerPattern::FocusStart);
    change_page(PomodoroPage::Running, "start_focus");
}

void pause_timer(const char *reason)
{
    s_paused_remaining_us = timer_remaining_us(esp_timer_get_time());
    STICKY_LOGI(kTag,
                "pomodoro=timer state=paused remaining_s=%lu reason=%s",
                static_cast<unsigned long>(
                    remaining_seconds_rounded_up(s_paused_remaining_us)),
                reason);
}

void resume_timer(const char *reason = "resume", bool partial = false)
{
    s_timer_deadline_us = esp_timer_get_time() + s_paused_remaining_us;
    s_displayed_remaining_seconds =
        remaining_seconds_rounded_up(s_paused_remaining_us);
    STICKY_LOGI(kTag,
                "pomodoro=timer state=resumed remaining_s=%lu",
                static_cast<unsigned long>(
                    remaining_seconds_rounded_up(s_paused_remaining_us)));
    change_page(PomodoroPage::Running, reason, partial);
}

void return_to_setup(const char *reason)
{
    // Keeps the most recently selected duration ready for the next session.
    // 保留最近一次选择的时长，供下一轮专注直接使用。
    s_total_seconds = s_selected_seconds;
    s_timer_deadline_us = 0;
    s_paused_remaining_us = 0;
    s_displayed_remaining_seconds = 0;
    change_page(PomodoroPage::Setup, reason);
}

void enter_alarm()
{
    s_paused_remaining_us = 0;
    STICKY_LOGI(kTag, "pomodoro=timer state=completed");
    const esp_err_t buzzer_result = sticky_buzzer_start_alarm();
    if (buzzer_result != ESP_OK) {
        STICKY_LOGW(kTag,
                    "pomodoro=alarm buzzer=failed result=%s",
                    esp_err_to_name(buzzer_result));
    }
    change_page(PomodoroPage::Alarm, "timer_completed");
}

void handle_setup_action(PomodoroAction action)
{
    uint32_t selected = 0;
    switch (action) {
    case PomodoroAction::Preset15Minutes:
        selected = 900;
        break;
    case PomodoroAction::Preset25Minutes:
        selected = 1500;
        break;
    case PomodoroAction::Preset60Minutes:
        selected = 3600;
        break;
    case PomodoroAction::OpenCustomTime:
        s_active_field = PomodoroTimeField::Minutes;
        s_replace_field_on_digit = true;
        s_custom_backspace_pending = false;
        change_page(PomodoroPage::CustomTime, "open_custom_time");
        return;
    case PomodoroAction::StartFocus:
        start_timer();
        return;
    default:
        return;
    }

    s_selected_seconds = selected;
    STICKY_LOGI(kTag,
                "pomodoro=duration source=preset value_s=%lu",
                static_cast<unsigned long>(selected));
    render_current_page(true);
}

CustomActionResult handle_custom_action(PomodoroAction action)
{
    const int digit = action_digit(action);
    if (digit >= 0) {
        enter_custom_digit(static_cast<uint8_t>(digit));
        return CustomActionResult::Redraw;
    }

    switch (action) {
    case PomodoroAction::SelectHours:
        select_custom_field(PomodoroTimeField::Hours);
        return CustomActionResult::Redraw;
    case PomodoroAction::SelectMinutes:
        select_custom_field(PomodoroTimeField::Minutes);
        return CustomActionResult::Redraw;
    case PomodoroAction::SelectSeconds:
        select_custom_field(PomodoroTimeField::Seconds);
        return CustomActionResult::Redraw;
    case PomodoroAction::Clear: {
        uint8_t &value = active_custom_value();
        pomodoro_custom_clear_field(value,
                                    s_replace_field_on_digit,
                                    s_custom_backspace_pending);
        STICKY_LOGI(kTag,
                    "pomodoro=custom_input action=clear field=%d value=%u",
                    static_cast<int>(s_active_field),
                    static_cast<unsigned>(value));
        return CustomActionResult::Redraw;
    }
    case PomodoroAction::Delete: {
        uint8_t &value = active_custom_value();
        pomodoro_custom_backspace(value,
                                  s_replace_field_on_digit,
                                  s_custom_backspace_pending);
        STICKY_LOGD(kTag,
                    "pomodoro=custom_input action=delete field=%d value=%u",
                    static_cast<int>(s_active_field),
                    static_cast<unsigned>(value));
        return CustomActionResult::Redraw;
    }
    case PomodoroAction::UseCustomTime:
        if (!custom_time_is_valid()) {
            STICKY_LOGW(kTag,
                        "pomodoro=custom_input result=invalid hours=%u minutes=%u seconds=%u",
                        static_cast<unsigned>(s_custom_hours),
                        static_cast<unsigned>(s_custom_minutes),
                        static_cast<unsigned>(s_custom_seconds));
            return CustomActionResult::None;
        }
        s_selected_seconds = custom_duration_seconds();
        STICKY_LOGI(kTag,
                    "pomodoro=duration source=custom value_s=%lu",
                    static_cast<unsigned long>(s_selected_seconds));
        change_page(PomodoroPage::Setup, "custom_time_accepted");
        return CustomActionResult::PageChanged;
    case PomodoroAction::Back:
        change_page(PomodoroPage::Setup, "custom_time_back");
        return CustomActionResult::PageChanged;
    default:
        return CustomActionResult::None;
    }
}

void handle_action(PomodoroAction action)
{
    if (action == PomodoroAction::None) {
        return;
    }

    if (s_page == PomodoroPage::Setup) {
        handle_setup_action(action);
    } else if (s_page == PomodoroPage::CustomTime) {
        if (handle_custom_action(action) == CustomActionResult::Redraw) {
            render_current_page(true, PomodoroRenderReason::QueuedInput);
            s_custom_cleanup_deadline_us =
                esp_timer_get_time() + kCustomCleanupDelayUs;
        }
    } else if (s_page == PomodoroPage::Running) {
        if (action == PomodoroAction::Pause) {
            pause_timer("pause_button");
            change_page(PomodoroPage::Paused, "pause");
        } else if (action == PomodoroAction::EndSession) {
            pause_timer("end_confirmation");
            change_page(
                PomodoroPage::EndConfirmation, "end_session", true);
        }
    } else if (s_page == PomodoroPage::Paused) {
        if (action == PomodoroAction::Resume) {
            resume_timer();
        } else if (action == PomodoroAction::EndSession) {
            change_page(
                PomodoroPage::EndConfirmation, "end_session", true);
        }
    } else if (s_page == PomodoroPage::EndConfirmation) {
        if (action == PomodoroAction::CancelEnd) {
            resume_timer("cancel_end", true);
        } else if (action == PomodoroAction::ConfirmEnd) {
            STICKY_LOGI(kTag, "pomodoro=timer state=ended_early");
            return_to_setup("confirm_end");
        }
    } else if (s_page == PomodoroPage::Alarm &&
               action == PomodoroAction::EndAlarm) {
        const esp_err_t buzzer_result = sticky_buzzer_stop();
        if (buzzer_result != ESP_OK) {
            STICKY_LOGW(kTag,
                        "pomodoro=alarm stop=failed result=%s",
                        esp_err_to_name(buzzer_result));
        }
        return_to_setup("alarm_ended");
    }
}

void update_running_timer()
{
    const int64_t remaining_us = timer_remaining_us(esp_timer_get_time());
    if (remaining_us == 0) {
        enter_alarm();
        return;
    }

    s_paused_remaining_us = remaining_us;
    const uint32_t remaining_seconds =
        remaining_seconds_rounded_up(remaining_us);

    // Every duration renders each visible second through partial fast refresh.
    // 所有时长都通过局部快刷显示每一秒的变化。
    if (pomodoro_countdown_should_render_frame(
            s_displayed_remaining_seconds, remaining_seconds)) {
        s_displayed_remaining_seconds = remaining_seconds;
#if STICKY_LOG_TIMER_TICKS_ENABLED
        STICKY_LOGD(kTag,
                    "pomodoro=timer state=checkpoint cadence=second remaining_s=%lu",
                    static_cast<unsigned long>(remaining_seconds));
#endif
        render_current_page(true, PomodoroRenderReason::CountdownTick);
    }
}

PomodoroAction action_for_press(const StickyTouchPress &press)
{
    int logical_x = 0;
    int logical_y = 0;
    s_canvas->physical_to_logical(
        press.x, press.y, logical_x, logical_y);
    const PomodoroAction action =
        pomodoro_page_action_at(s_page, logical_x, logical_y);
    if (action != PomodoroAction::None) {
        STICKY_LOGI(kTag,
                    "pomodoro=touch page=%s action=%s physical_x=%u physical_y=%u logical_x=%d logical_y=%d",
                    pomodoro_page_name(s_page),
                    pomodoro_action_name(action),
                    static_cast<unsigned>(press.x),
                    static_cast<unsigned>(press.y),
                    logical_x,
                    logical_y);
    }
    return action;
}

void handle_custom_press_batch(StickyTouchPress press)
{
    bool redraw_needed = false;
    unsigned batched_actions = 0U;

    while (s_page == PomodoroPage::CustomTime) {
        const PomodoroAction action = action_for_press(press);
        if (action == PomodoroAction::None) {
            break;
        }

        const CustomActionResult result = handle_custom_action(action);
        if (result == CustomActionResult::Redraw) {
            redraw_needed = true;
            ++batched_actions;
        } else if (result == CustomActionResult::PageChanged) {
            return;
        }

        if (!pomodoro_custom_action_can_batch(action) ||
            !sticky_touch_take_press(press)) {
            break;
        }
    }

    if (!redraw_needed || s_page != PomodoroPage::CustomTime) {
        return;
    }
    if (batched_actions > 1U) {
        STICKY_LOGI(kTag,
                    "pomodoro=custom_input_batch actions=%u refreshes=1",
                    batched_actions);
    }
    render_current_page(true, PomodoroRenderReason::QueuedInput);
    s_custom_cleanup_deadline_us =
        esp_timer_get_time() + kCustomCleanupDelayUs;
}

void app_task(void *)
{
    render_current_page(false);
    STICKY_LOGI(kTag,
                "pomodoro=ready page=setup default_duration_s=%lu result=ok",
                static_cast<unsigned long>(kDefaultDurationSeconds));

    while (true) {
        if (sticky_app_lifecycle_checkpoint(s_lifecycle)) {
            sticky_touch_clear_press();
            s_custom_cleanup_deadline_us = 0;
            if (s_page == PomodoroPage::Running) {
                s_paused_remaining_us =
                    timer_remaining_us(esp_timer_get_time());
            }
            render_current_page(false);
        }

        if (s_page == PomodoroPage::Running) {
            update_running_timer();
        }

        StickyTouchPress press = {};
        if (sticky_touch_take_press(press)) {
            if (s_page == PomodoroPage::CustomTime) {
                handle_custom_press_batch(press);
            } else {
                handle_action(action_for_press(press));
            }
        }

        const int64_t now_us = esp_timer_get_time();
        if (s_page == PomodoroPage::CustomTime &&
            s_custom_cleanup_deadline_us != 0 &&
            now_us >= s_custom_cleanup_deadline_us) {
            s_custom_cleanup_deadline_us = 0;
            STICKY_LOGI(kTag,
                        "pomodoro=display refresh=full reason=custom_input_cleanup");
            render_current_page(
                false, PomodoroRenderReason::MaintenanceCleanup);
        }

        vTaskDelay(kPollInterval);
    }
}

}  // namespace

esp_err_t pomodoro_app_start(Canvas &canvas)
{
    app_log_register_tag(kTag);
    if (s_app_task != nullptr) {
        return ESP_OK;
    }

    if (s_sleep_snapshot.magic == kSleepSnapshotMagic) {
        s_page = s_sleep_snapshot.page;
        s_active_field = s_sleep_snapshot.active_field;
        s_selected_seconds = s_sleep_snapshot.selected_seconds;
        s_total_seconds = s_sleep_snapshot.total_seconds;
        s_custom_hours = s_sleep_snapshot.custom_hours;
        s_custom_minutes = s_sleep_snapshot.custom_minutes;
        s_custom_seconds = s_sleep_snapshot.custom_seconds;
        s_replace_field_on_digit =
            s_sleep_snapshot.replace_field_on_digit;
        s_custom_backspace_pending =
            s_sleep_snapshot.custom_backspace_pending;
        s_paused_remaining_us = s_sleep_snapshot.paused_remaining_us;
        s_displayed_remaining_seconds = static_cast<uint32_t>(
            (s_paused_remaining_us + 999999LL) / 1000000LL);
        s_sleep_snapshot.magic = 0U;
    }
    s_canvas = &canvas;
    if (xTaskCreate(app_task,
                    "pomodoro_app",
                    kTaskStackSize,
                    nullptr,
                    kTaskPriority,
                    &s_app_task) != pdPASS) {
        s_canvas = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void pomodoro_app_set_display_rotation(CanvasRotation rotation)
{
    s_display_rotation = rotation;
}

esp_err_t pomodoro_app_pause()
{
    return sticky_app_lifecycle_pause(s_lifecycle, s_app_task);
}

esp_err_t pomodoro_app_resume()
{
    if (s_app_task == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    sticky_app_lifecycle_resume(s_lifecycle);
    return ESP_OK;
}

bool pomodoro_app_power_sleep_allowed()
{
    return s_page != PomodoroPage::Running &&
           s_page != PomodoroPage::Alarm &&
           s_page != PomodoroPage::EndConfirmation;
}

esp_err_t pomodoro_app_prepare_power_sleep()
{
    const esp_err_t result = pomodoro_app_pause();
    if (result != ESP_OK) {
        return result;
    }
    s_sleep_snapshot.page = s_page;
    s_sleep_snapshot.active_field = s_active_field;
    s_sleep_snapshot.selected_seconds = s_selected_seconds;
    s_sleep_snapshot.total_seconds = s_total_seconds;
    s_sleep_snapshot.custom_hours = s_custom_hours;
    s_sleep_snapshot.custom_minutes = s_custom_minutes;
    s_sleep_snapshot.custom_seconds = s_custom_seconds;
    s_sleep_snapshot.replace_field_on_digit = s_replace_field_on_digit;
    s_sleep_snapshot.custom_backspace_pending =
        s_custom_backspace_pending;
    s_sleep_snapshot.paused_remaining_us = s_paused_remaining_us;
    s_sleep_snapshot.magic = kSleepSnapshotMagic;
    return ESP_OK;
}

uint32_t pomodoro_app_power_sleep_timeout_ms()
{
    return s_page == PomodoroPage::Setup ||
                   s_page == PomodoroPage::Paused
               ? kIdleSleepTimeoutMs
               : 0U;
}
