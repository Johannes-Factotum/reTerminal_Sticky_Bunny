#include "sticky_app.h"

#include <cstdint>

#include "app_log.h"
#include "app_pages.h"
#include "board_charger.h"
#include "board_power.h"
#include "book_of_answers_app.h"
#include "canvas.h"
#include "desktop_pet_app.h"
#include "esp_timer.h"
#include "flashcards_app.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "onboarding_app.h"
#include "pomodoro_app.h"
#include "status_board_app.h"
#include "sticky_app_display_orientation.h"
#include "sticky_app_gesture.h"
#include "sticky_app_id.h"
#include "sticky_app_power_policy.h"
#include "sticky_app_router.h"
#include "sticky_buzzer.h"
#include "sticky_button.h"
#include "sticky_display.h"
#include "sticky_imu.h"
#include "sticky_touch.h"
#include "todo_app.h"

#ifndef STICKY_ONBOARDING_TEST_MODE
#define STICKY_ONBOARDING_TEST_MODE 0
#endif

namespace {

constexpr char kTag[] = "sticky_app";
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(40);
constexpr uint8_t kInitialImuSampleAttempts = 7U;
constexpr TickType_t kInitialImuSampleRetry = pdMS_TO_TICKS(10);
constexpr uint32_t kTaskStackSize = 5120U;
constexpr UBaseType_t kTaskPriority = 4U;
constexpr uint32_t kSleepContextMagic = 0x53504C50U;
constexpr uint32_t kScheduledWakeLeadSeconds = 15U;
constexpr int64_t kBackgroundEventWindowUs = 20000000LL;
constexpr uint32_t kLauncherIdleCloseMs = 30000U;
#ifndef STICKY_POWER_TEST_MODE
#define STICKY_POWER_TEST_MODE 0
#endif
#if STICKY_POWER_TEST_MODE
constexpr uint32_t kPetIdleSleepMs = 60000U;
#else
constexpr uint32_t kPetIdleSleepMs = 10U * 60U * 1000U;
#endif

Canvas *s_canvas = nullptr;
TaskHandle_t s_app_task = nullptr;
StickyAppId s_current_app = StickyAppId::DesktopPet;
bool s_pet_started = false;
bool s_status_started = false;
bool s_pomodoro_started = false;
bool s_book_started = false;
bool s_flashcards_started = false;
bool s_todo_started = false;
bool s_background_timer_wake = false;
int64_t s_background_sleep_deadline_us = 0;
uint32_t s_last_user_activity_ms = 0U;

struct StickySleepContext {
    uint32_t magic;
    StickyAppId app;
    CanvasRotation rotation;
};

RTC_NOINIT_ATTR StickySleepContext s_sleep_context;

struct LauncherImuPrestart {
    bool active = false;
    bool restarted = false;
    int64_t started_at_us = 0;
    StickyImuOrientation baseline = StickyImuOrientation::Unknown;
    StickyImuOrientation last_settled = StickyImuOrientation::Unknown;
};

bool app_started(StickyAppId app)
{
    switch (app) {
    case StickyAppId::DesktopPet:
        return s_pet_started;
    case StickyAppId::Pomodoro:
        return s_pomodoro_started;
    case StickyAppId::StatusBoard:
        return s_status_started;
    case StickyAppId::BookOfAnswers:
        return s_book_started;
    case StickyAppId::Flashcards:
        return s_flashcards_started;
    case StickyAppId::Todo:
        return s_todo_started;
    }
    return false;
}

void mark_app_started(StickyAppId app)
{
    switch (app) {
    case StickyAppId::DesktopPet:
        s_pet_started = true;
        break;
    case StickyAppId::Pomodoro:
        s_pomodoro_started = true;
        break;
    case StickyAppId::StatusBoard:
        s_status_started = true;
        break;
    case StickyAppId::BookOfAnswers:
        s_book_started = true;
        break;
    case StickyAppId::Flashcards:
        s_flashcards_started = true;
        break;
    case StickyAppId::Todo:
        s_todo_started = true;
        break;
    }
}

esp_err_t pause_app(StickyAppId app)
{
    switch (app) {
    case StickyAppId::DesktopPet:
        return desktop_pet_app_pause();
    case StickyAppId::Pomodoro:
        return pomodoro_app_pause();
    case StickyAppId::StatusBoard:
        return status_board_app_pause();
    case StickyAppId::BookOfAnswers:
        return book_of_answers_app_pause();
    case StickyAppId::Flashcards:
        return flashcards_app_pause();
    case StickyAppId::Todo:
        return todo_app_pause();
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t start_app(StickyAppId app)
{
    switch (app) {
    case StickyAppId::DesktopPet:
        return desktop_pet_app_start(*s_canvas);
    case StickyAppId::Pomodoro:
        return pomodoro_app_start(*s_canvas);
    case StickyAppId::StatusBoard:
        return status_board_app_start(*s_canvas);
    case StickyAppId::BookOfAnswers:
        return book_of_answers_app_start(*s_canvas);
    case StickyAppId::Flashcards:
        return flashcards_app_start(*s_canvas);
    case StickyAppId::Todo:
        return todo_app_start(*s_canvas);
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t resume_app(StickyAppId app)
{
    switch (app) {
    case StickyAppId::DesktopPet:
        return desktop_pet_app_resume();
    case StickyAppId::Pomodoro:
        return pomodoro_app_resume();
    case StickyAppId::StatusBoard:
        return status_board_app_resume();
    case StickyAppId::BookOfAnswers:
        return book_of_answers_app_resume();
    case StickyAppId::Flashcards:
        return flashcards_app_resume();
    case StickyAppId::Todo:
        return todo_app_resume();
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t activate_app(StickyAppId app)
{
    sticky_touch_clear_press();
    sticky_touch_clear_interaction();
    if (!app_started(app)) {
        const esp_err_t result = start_app(app);
        if (result == ESP_OK) {
            mark_app_started(app);
        }
        return result;
    }
    return resume_app(app);
}

esp_err_t set_imu_running(bool running)
{
    return running ? sticky_imu_start_monitoring()
                   : sticky_imu_stop_monitoring();
}

bool power_sleep_allowed()
{
    if (s_current_app == StickyAppId::DesktopPet) {
        return desktop_pet_app_power_sleep_allowed();
    }
    if (s_current_app == StickyAppId::Pomodoro) {
        return pomodoro_app_power_sleep_allowed();
    }
    if (s_current_app == StickyAppId::BookOfAnswers) {
        return book_of_answers_app_power_sleep_allowed();
    }
    if (s_current_app == StickyAppId::Flashcards) {
        return flashcards_app_power_sleep_allowed();
    }
    if (s_current_app == StickyAppId::Todo) {
        return todo_app_power_sleep_allowed();
    }
    return true;
}

uint32_t power_sleep_timeout_ms()
{
    if (s_current_app == StickyAppId::DesktopPet) {
        return kPetIdleSleepMs;
    }
    if (s_current_app == StickyAppId::StatusBoard) {
        return status_board_app_power_sleep_timeout_ms();
    }
    if (s_current_app == StickyAppId::Pomodoro) {
        return pomodoro_app_power_sleep_timeout_ms();
    }
    if (s_current_app == StickyAppId::BookOfAnswers) {
        return book_of_answers_app_power_sleep_timeout_ms();
    }
    if (s_current_app == StickyAppId::Flashcards) {
        return flashcards_app_power_sleep_timeout_ms();
    }
    if (s_current_app == StickyAppId::Todo) {
        return todo_app_power_sleep_timeout_ms();
    }
    return 0U;
}

esp_err_t prepare_app_power_sleep(uint32_t &current_epoch,
                                  uint32_t &next_event_epoch)
{
    current_epoch = 0U;
    next_event_epoch = 0U;
    if (s_current_app == StickyAppId::DesktopPet) {
        return desktop_pet_app_prepare_power_sleep(
            current_epoch, next_event_epoch);
    }
    if (s_current_app == StickyAppId::StatusBoard) {
        return status_board_app_prepare_power_sleep();
    }
    if (s_current_app == StickyAppId::Pomodoro) {
        return pomodoro_app_prepare_power_sleep();
    }
    if (s_current_app == StickyAppId::BookOfAnswers) {
        return book_of_answers_app_prepare_power_sleep();
    }
    if (s_current_app == StickyAppId::Flashcards) {
        return flashcards_app_prepare_power_sleep();
    }
    if (s_current_app == StickyAppId::Todo) {
        return todo_app_prepare_power_sleep();
    }
    return ESP_ERR_INVALID_ARG;
}

void draw_sleep_indicator()
{
    // A compact crescent remains visible in the logical top-right corner of
    // every orientation without replacing the current app page.
    // 小型月牙会保留在各方向页面的逻辑右上角，同时不替换当前APP画面。
    const int left = static_cast<int>(s_canvas->width()) - 45;
    constexpr int top = 10;
    s_canvas->fill_rect(left, top, 35, 35, GrayLevel::White);
    s_canvas->fill_circle(left + 16, top + 17, 11, GrayLevel::Black);
    s_canvas->fill_circle(left + 21, top + 12, 10, GrayLevel::White);
    s_canvas->fill_rect(left + 28, top + 25, 3, 3, GrayLevel::Black);
    s_canvas->draw_pixel(left + 30, top + 23, GrayLevel::Black);
    s_canvas->draw_pixel(left + 30, top + 29, GrayLevel::Black);
    s_canvas->draw_pixel(left + 26, top + 27, GrayLevel::Black);
    s_canvas->draw_pixel(left + 34, top + 27, GrayLevel::Black);
}

void enter_power_sleep(StickyAppRouterState &router,
                       StickyPowerSleepTrigger trigger)
{
    const char *source = sticky_power_sleep_trigger_name(trigger);
    if (!power_sleep_allowed()) {
        STICKY_LOGI(kTag,
                    "power=sleep_request source=%s app=%s result=blocked",
                    source,
                    sticky_app_id_name(s_current_app));
        return;
    }

    if (router.launcher_open) {
        sticky_app_router_close(router);
    }
    uint32_t current_epoch = 0U;
    uint32_t next_event_epoch = 0U;
    const esp_err_t pause_result = prepare_app_power_sleep(
        current_epoch, next_event_epoch);
    if (pause_result != ESP_OK) {
        STICKY_LOGE(kTag,
                    "power=sleep_request source=%s app=%s pause=%s result=failed",
                    source,
                    sticky_app_id_name(s_current_app),
                    esp_err_to_name(pause_result));
        return;
    }

    s_sleep_context.magic = kSleepContextMagic;
    s_sleep_context.app = s_current_app;
    s_sleep_context.rotation = s_canvas->rotation();

    sticky_display_set_battery_overlay_sleep_layout(true);
    draw_sleep_indicator();
    // The final frame stays visible throughout deep sleep, so force a full
    // monochrome waveform to clean accumulated partial-refresh ghosting.
    // 最后一帧会在深睡期间长期保留，因此强制全刷以清理局刷积累的残影。
    sticky_display_cancel_app_transition_refresh();
    const esp_err_t indicator_result =
        sticky_display_refresh_monochrome();
    if (indicator_result != ESP_OK) {
        sticky_display_set_battery_overlay_sleep_layout(false);
        resume_app(s_current_app);
        STICKY_LOGE(kTag,
                    "power=sleep_indicator refresh=%s result=failed",
                    esp_err_to_name(indicator_result));
        return;
    }

    if (sticky_power_sleep_chime_enabled(trigger)) {
        const esp_err_t sleep_chime_result =
            sticky_buzzer_play_power_sleep_chime();
        if (sleep_chime_result != ESP_OK) {
            STICKY_LOGW(kTag,
                        "power=sleep_chime result=%s",
                        esp_err_to_name(sleep_chime_result));
        }
    }
    sticky_buzzer_stop();
    set_imu_running(false);
    const esp_err_t touch_result = sticky_touch_stop();
    if (touch_result != ESP_OK) {
        STICKY_LOGE(kTag,
                    "power=sleep peripheral=touch result=%s",
                    esp_err_to_name(touch_result));
    }
    const esp_err_t display_result = sticky_display_sleep();
    if (display_result != ESP_OK) {
        STICKY_LOGE(kTag,
                    "power=sleep peripheral=display result=%s",
                    esp_err_to_name(display_result));
    }

    uint64_t timer_wakeup_us = 0U;
    uint32_t wake_epoch = 0U;
    if (next_event_epoch > current_epoch) {
        wake_epoch = next_event_epoch > kScheduledWakeLeadSeconds
                         ? next_event_epoch - kScheduledWakeLeadSeconds
                         : next_event_epoch;
        if (wake_epoch <= current_epoch) {
            wake_epoch = current_epoch + 1U;
        }
        timer_wakeup_us = static_cast<uint64_t>(
            wake_epoch - current_epoch) * 1000000ULL;
    }
    STICKY_LOGI(kTag,
                "power=sleep_request source=%s app=%s current=%u next_event=%u wake_at=%u result=ready",
                source,
                sticky_app_id_name(s_current_app),
                static_cast<unsigned>(current_epoch),
                static_cast<unsigned>(next_event_epoch),
                static_cast<unsigned>(wake_epoch));
    board_power_enter_deep_sleep(timer_wakeup_us);
}

void render_launcher(StickyImuOrientation orientation)
{
    sticky_touch_clear_press();
    sticky_touch_clear_interaction();

    CanvasRotation launcher_rotation = s_canvas->rotation();
    const bool rotation_applied = sticky_app_launcher_rotation(
        orientation, launcher_rotation);
    if (rotation_applied) {
        s_canvas->set_rotation(launcher_rotation);
    }
    STICKY_LOGI(kTag,
                "launcher=orientation imu=%s display_rotation=%s result=%s",
                sticky_imu_orientation_name(orientation),
                sticky_app_display_rotation_name(s_canvas->rotation()),
                rotation_applied ? "applied" : "retained");

    app_page_render_launcher(*s_canvas, s_current_app);
    const esp_err_t result = sticky_display_refresh_partial();
    if (result != ESP_OK) {
        STICKY_LOGE(kTag,
                    "launcher=display refresh=partial result=%s",
                    esp_err_to_name(result));
    }
}

StickyImuState read_initial_launcher_imu_state()
{
    // Gives the new monitor task a short scheduling window and keeps the first
    // physical pose as the route baseline before display work begins.
    // 给新IMU任务留出很短的调度时间，并在屏幕工作前保存最初实际姿态。
    StickyImuState state = {};
    for (uint8_t attempt = 0U;
         attempt < kInitialImuSampleAttempts;
         ++attempt) {
        if (sticky_imu_get_state(state) == ESP_OK) {
            return state;
        }
        vTaskDelay(kInitialImuSampleRetry);
    }
    return {};
}

// Starts one launcher-owned IMU session on the physical press event and keeps
// its first pose until the click type has been resolved.
// 在物理按下事件中启动选择器专属IMU会话，并保留起始姿态直到完成点击类型判定。
bool prestart_launcher_imu(LauncherImuPrestart &prestart)
{
    if (prestart.active) {
        return true;
    }

    prestart = {};
    prestart.started_at_us = esp_timer_get_time();
    prestart.restarted =
        s_current_app == StickyAppId::BookOfAnswers;
    if (prestart.restarted) {
        const esp_err_t stop_result = set_imu_running(false);
        if (stop_result != ESP_OK) {
            STICKY_LOGE(kTag,
                        "launcher=imu phase=press_start app=book_of_answers stop=%s result=failed",
                        esp_err_to_name(stop_result));
            prestart = {};
            return false;
        }
    }

    const esp_err_t start_result = set_imu_running(true);
    if (start_result != ESP_OK) {
        STICKY_LOGE(kTag,
                    "launcher=imu phase=press_start start=%s result=failed",
                    esp_err_to_name(start_result));
        prestart = {};
        return false;
    }

    const StickyImuState imu_state = read_initial_launcher_imu_state();
    if (imu_state.valid) {
        prestart.baseline =
            imu_state.orientation != StickyImuOrientation::Unknown
                ? imu_state.orientation
                : imu_state.observed_orientation;
        prestart.last_settled = imu_state.orientation;
    }
    prestart.active = true;
    STICKY_LOGI(kTag,
                "launcher=imu phase=started_on_press current_app=%s baseline=%s restart=%d result=ok",
                sticky_app_id_name(s_current_app),
                sticky_imu_orientation_name(prestart.baseline),
                prestart.restarted);
    return true;
}

bool open_launcher(StickyAppRouterState &router,
                   StickyImuOrientation &last_settled,
                   LauncherImuPrestart &prestart)
{
    if (!prestart_launcher_imu(prestart)) {
        return false;
    }

    sticky_app_router_open(router, prestart.baseline);
    last_settled = prestart.last_settled;

    const int64_t pause_started_at_us = esp_timer_get_time();
    const esp_err_t pause_result = pause_app(s_current_app);
    if (pause_result != ESP_OK) {
        sticky_app_router_close(router);
        if (!prestart.restarted) {
            set_imu_running(false);
        }
        resume_app(s_current_app);
        STICKY_LOGE(kTag,
                    "launcher=open app=%s pause=%s imu=%s result=failed",
                    sticky_app_id_name(s_current_app),
                    esp_err_to_name(pause_result),
                    prestart.restarted ? "retained" : "stopped");
        prestart = {};
        return false;
    }

    const int64_t display_started_at_us = esp_timer_get_time();
    render_launcher(router.baseline_orientation);
    STICKY_LOGI(kTag,
                "launcher=opened current_app=%s input=touch,rotation,shake baseline=%s imu=started pause_ms=%lld display_ms=%lld total_ms=%lld result=ok",
                sticky_app_id_name(s_current_app),
                sticky_imu_orientation_name(
                    router.baseline_orientation),
                static_cast<long long>(
                    (display_started_at_us - pause_started_at_us) /
                    1000LL),
                static_cast<long long>(
                    (esp_timer_get_time() - display_started_at_us) /
                    1000LL),
                static_cast<long long>(
                    (esp_timer_get_time() - prestart.started_at_us) /
                    1000LL));
    prestart = {};
    return true;
}

void cancel_launcher(StickyAppRouterState &router)
{
    sticky_app_router_close(router);
    esp_err_t imu_result = set_imu_running(false);
    if (s_current_app == StickyAppId::BookOfAnswers) {
        if (imu_result == ESP_OK) {
            imu_result = set_imu_running(true);
        }
    }
    sticky_touch_clear_press();
    sticky_touch_clear_interaction();
    const esp_err_t resume_result = resume_app(s_current_app);
    STICKY_LOGI(kTag,
                "launcher=cancelled app=%s imu=%s resume=%s result=%s",
                sticky_app_id_name(s_current_app),
                s_current_app == StickyAppId::BookOfAnswers
                    ? esp_err_to_name(imu_result)
                    : "stopped",
                esp_err_to_name(resume_result),
                imu_result == ESP_OK && resume_result == ESP_OK
                    ? "ok"
                    : "failed");
}

void handle_launcher_gesture(
    const StickyTouchInteraction &interaction,
    StickyAppRouterState &router,
    StickyImuOrientation &last_settled,
    LauncherImuPrestart &imu_prestart)
{
    int logical_start_x = 0;
    int logical_start_y = 0;
    int logical_end_x = 0;
    int logical_end_y = 0;
    s_canvas->physical_to_logical(interaction.start_x,
                                  interaction.start_y,
                                  logical_start_x,
                                  logical_start_y);
    s_canvas->physical_to_logical(interaction.end_x,
                                  interaction.end_y,
                                  logical_end_x,
                                  logical_end_y);
    const StickyAppGestureSample sample = {
        static_cast<int>(s_canvas->width()),
        static_cast<int>(s_canvas->height()),
        logical_start_x,
        logical_start_y,
        logical_end_x,
        logical_end_y,
        interaction.ended_at_ms - interaction.started_at_ms,
        router.launcher_open,
    };
    const StickyAppGestureAction action =
        sticky_app_gesture_classify(sample);
    if (action == StickyAppGestureAction::OpenLauncher) {
        STICKY_LOGI(kTag,
                    "launcher=gesture action=open start_x=%d start_y=%d end_x=%d end_y=%d duration_ms=%u",
                    logical_start_x,
                    logical_start_y,
                    logical_end_x,
                    logical_end_y,
                    static_cast<unsigned>(sample.duration_ms));
        open_launcher(router, last_settled, imu_prestart);
    } else if (action == StickyAppGestureAction::CloseLauncher) {
        STICKY_LOGI(kTag,
                    "launcher=gesture action=close start_x=%d start_y=%d end_x=%d end_y=%d duration_ms=%u",
                    logical_start_x,
                    logical_start_y,
                    logical_end_x,
                    logical_end_y,
                    static_cast<unsigned>(sample.duration_ms));
        cancel_launcher(router);
        imu_prestart = {};
    }
}

void complete_selection(StickyAppId selected_app,
                        const char *source,
                        bool preserve_shake_session,
                        StickyImuOrientation final_orientation)
{
    const StickyAppId previous_app = s_current_app;
    esp_err_t imu_result = ESP_OK;
    if (selected_app == StickyAppId::BookOfAnswers) {
        book_of_answers_app_prepare_entry(preserve_shake_session);
        if (!preserve_shake_session) {
            imu_result = set_imu_running(false);
            if (imu_result == ESP_OK) {
                imu_result = set_imu_running(true);
            }
        }
    } else {
        imu_result = set_imu_running(false);
    }

    CanvasRotation display_rotation = CanvasRotation::Deg0;
    if (sticky_app_display_rotation(selected_app,
                                    final_orientation,
                                    display_rotation)) {
        if (selected_app == StickyAppId::Pomodoro) {
            pomodoro_app_set_display_rotation(display_rotation);
        } else if (selected_app == StickyAppId::StatusBoard) {
            status_board_app_set_display_rotation(display_rotation);
        }
        STICKY_LOGI(kTag,
                    "launcher=orientation app=%s imu=%s display_rotation=%s result=applied",
                    sticky_app_id_name(selected_app),
                    sticky_imu_orientation_name(final_orientation),
                    sticky_app_display_rotation_name(display_rotation));
    }

    if (imu_result == ESP_OK) {
        sticky_display_prepare_app_transition_refresh();
    }
    const esp_err_t activation_result =
        imu_result == ESP_OK ? activate_app(selected_app) : imu_result;
    if (activation_result == ESP_OK) {
        s_current_app = selected_app;
        STICKY_LOGI(
            kTag,
            "launcher=selected app_from=%s app_to=%s input=%s imu=%s result=ok",
            sticky_app_id_name(previous_app),
            sticky_app_id_name(s_current_app),
            source,
            s_current_app == StickyAppId::BookOfAnswers
                ? "started"
                : "stopped");
        return;
    }

    sticky_display_cancel_app_transition_refresh();
    if (selected_app == StickyAppId::BookOfAnswers) {
        book_of_answers_app_prepare_entry(false);
        set_imu_running(false);
    }
    if (previous_app == StickyAppId::BookOfAnswers) {
        set_imu_running(true);
    }
    resume_app(previous_app);
    STICKY_LOGE(kTag,
                "launcher=selected app=%s activation=%s fallback=%s result=failed",
                sticky_app_id_name(selected_app),
                esp_err_to_name(activation_result),
                sticky_app_id_name(previous_app));
}

void return_to_desktop_pet(StickyAppRouterState &router)
{
    const esp_err_t buzzer_result = sticky_buzzer_stop();
    if (buzzer_result != ESP_OK) {
        STICKY_LOGW(kTag,
                    "launcher=home input=button_double_click buzzer=%s",
                    esp_err_to_name(buzzer_result));
    }

    if (s_current_app == StickyAppId::DesktopPet) {
        if (router.launcher_open) {
            sticky_app_router_close(router);
        }
        const esp_err_t imu_result = set_imu_running(false);
        sticky_display_prepare_app_transition_refresh();
        const esp_err_t home_result = desktop_pet_app_return_home();
        if (home_result != ESP_OK) {
            sticky_display_cancel_app_transition_refresh();
            resume_app(StickyAppId::DesktopPet);
        }
        STICKY_LOGI(kTag,
                    "launcher=home input=button_double_click app_from=desktop_pet app_to=desktop_pet page=root imu=%s navigation=%s result=%s",
                    esp_err_to_name(imu_result),
                    esp_err_to_name(home_result),
                    imu_result == ESP_OK && home_result == ESP_OK
                        ? "ok"
                        : "failed");
        return;
    }

    if (router.launcher_open) {
        sticky_app_router_close(router);
    } else {
        const esp_err_t pause_result = pause_app(s_current_app);
        if (pause_result != ESP_OK) {
            STICKY_LOGE(kTag,
                        "launcher=home input=button_double_click app_from=%s pause=%s result=failed",
                        sticky_app_id_name(s_current_app),
                        esp_err_to_name(pause_result));
            return;
        }
    }

    complete_selection(StickyAppId::DesktopPet,
                       "button_double_click",
                       false,
                       StickyImuOrientation::Unknown);
}

// Temporarily gives the display and touch controller to the full tutorial.
// 临时把屏幕和触摸控制权交给完整教程，结束后恢复桌宠。
void open_tutorial_from_desktop_pet()
{
    const esp_err_t pause_result = pause_app(StickyAppId::DesktopPet);
    if (pause_result != ESP_OK) {
        STICKY_LOGE(kTag,
                    "tutorial=reopen source=desktop_pet pause=%s result=failed",
                    esp_err_to_name(pause_result));
        return;
    }

    set_imu_running(false);
    sticky_touch_clear_press();
    sticky_touch_clear_interaction();
    const esp_err_t tutorial_result = onboarding_app_run(*s_canvas);
    const esp_err_t resume_result = resume_app(StickyAppId::DesktopPet);
    s_last_user_activity_ms = static_cast<uint32_t>(
        esp_timer_get_time() / 1000LL);
    STICKY_LOGI(kTag,
                "tutorial=reopen source=desktop_pet tutorial=%s resume=%s result=%s",
                esp_err_to_name(tutorial_result),
                esp_err_to_name(resume_result),
                tutorial_result == ESP_OK && resume_result == ESP_OK
                    ? "ok"
                    : "failed");
}

void handle_launcher_touch(const StickyTouchPress &press,
                           StickyAppRouterState &router)
{
    int logical_x = 0;
    int logical_y = 0;
    s_canvas->physical_to_logical(
        press.x, press.y, logical_x, logical_y);
#if STICKY_ONBOARDING_TEST_MODE
    if (app_page_launcher_tutorial_at(s_canvas->width(),
                                      s_canvas->height(),
                                      logical_x,
                                      logical_y)) {
        const esp_err_t reset_result = onboarding_app_reset_completion();
        STICKY_LOGI(kTag,
                    "launcher=tutorial action=reset_and_restart result=%s",
                    esp_err_to_name(reset_result));
        if (reset_result == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(80));
            esp_restart();
        }
        return;
    }
#endif
    StickyAppId selected_app = StickyAppId::DesktopPet;
    if (!app_page_launcher_app_at(s_canvas->width(),
                                  s_canvas->height(),
                                  logical_x,
                                  logical_y,
                                  selected_app)) {
        STICKY_LOGD(kTag,
                    "launcher=touch action=none physical_x=%u physical_y=%u logical_x=%d logical_y=%d",
                    press.x,
                    press.y,
                    logical_x,
                    logical_y);
        return;
    }

    sticky_app_router_close(router);
    STICKY_LOGI(kTag,
                "launcher=touch action=select app=%s logical_x=%d logical_y=%d",
                sticky_app_id_name(selected_app),
                logical_x,
                logical_y);
    complete_selection(selected_app,
                       "touch",
                       false,
                       StickyImuOrientation::Unknown);
}

void log_route(const StickyAppRouteResult &route, const char *source)
{
    STICKY_LOGI(kTag,
                "launcher=route action=%s input=%s from=%s to=%s app=%s",
                sticky_app_route_action_name(route.action),
                source,
                sticky_imu_orientation_name(route.from_orientation),
                sticky_imu_orientation_name(route.to_orientation),
                sticky_app_id_name(route.selected_app));
}

void handle_route(const StickyAppRouteResult &route, const char *source)
{
    if (route.action == StickyAppRouteAction::BaselineCaptured) {
        STICKY_LOGI(kTag,
                    "launcher=baseline orientation=%s source=%s result=ready",
                    sticky_imu_orientation_name(route.to_orientation),
                    source);
        log_route(route, source);
        return;
    }
    if (route.action != StickyAppRouteAction::AppSelected) {
        return;
    }

    log_route(route, source);
    complete_selection(route.selected_app,
                       source,
                       route.selected_app ==
                           StickyAppId::BookOfAnswers,
                       route.to_orientation);
}

void app_task(void *)
{
    StickyAppRouterState router = {};
    LauncherImuPrestart imu_prestart = {};
    StickyImuOrientation last_settled = StickyImuOrientation::Unknown;
    STICKY_LOGI(kTag,
                "launcher=ready trigger=top_button,bottom_swipe selection=touch,rotation,shake apps=6 imu=on_demand shake_select_ms=%u current_app=%s result=ok",
                static_cast<unsigned>(kStickyLauncherShakeSelectMs),
                sticky_app_id_name(s_current_app));

    while (true) {
        StickyButtonEvent button_event = StickyButtonEvent::None;
        if (sticky_button_take_event(button_event)) {
            s_last_user_activity_ms = static_cast<uint32_t>(
                esp_timer_get_time() / 1000LL);
            if (s_background_timer_wake) {
                s_background_timer_wake = false;
                s_background_sleep_deadline_us = 0;
                STICKY_LOGI(kTag,
                            "power=background_window state=cancelled source=button result=ok");
            }
            if (button_event == StickyButtonEvent::PressDown) {
                if (!router.launcher_open) {
                    prestart_launcher_imu(imu_prestart);
                }
            } else if (button_event == StickyButtonEvent::DoubleClick) {
                return_to_desktop_pet(router);
                if (imu_prestart.active &&
                    s_current_app == StickyAppId::DesktopPet) {
                    set_imu_running(false);
                }
                imu_prestart = {};
            } else if (button_event == StickyButtonEvent::SingleClick) {
                if (router.launcher_open) {
                    cancel_launcher(router);
                } else {
                    open_launcher(router,
                                  last_settled,
                                  imu_prestart);
                }
            } else if (button_event == StickyButtonEvent::SleepChord) {
                enter_power_sleep(
                    router, StickyPowerSleepTrigger::SideButtonChord);
            }
        }

        const uint32_t touch_activity_ms =
            sticky_touch_last_activity_ms();
        if (static_cast<int32_t>(
                touch_activity_ms - s_last_user_activity_ms) > 0) {
            s_last_user_activity_ms = touch_activity_ms;
            if (s_background_timer_wake) {
                s_background_timer_wake = false;
                s_background_sleep_deadline_us = 0;
                STICKY_LOGI(kTag,
                            "power=background_window state=cancelled source=touch result=ok");
            }
        }

        StickyTouchInteraction interaction = {};
        while (sticky_touch_take_interaction(interaction)) {
            handle_launcher_gesture(interaction,
                                    router,
                                    last_settled,
                                    imu_prestart);
        }

        if (!router.launcher_open &&
            s_current_app == StickyAppId::DesktopPet &&
            desktop_pet_app_take_onboarding_request()) {
            open_tutorial_from_desktop_pet();
        }

        if (router.launcher_open) {
            StickyTouchPress press = {};
            if (sticky_touch_take_press(press)) {
                handle_launcher_touch(press, router);
            }
        }

        if (router.launcher_open) {
            StickyImuState imu_state = {};
            const bool shake_active = sticky_imu_is_shaking();
            if (shake_active) {
                handle_route(sticky_app_router_shaking(
                                 router,
                                 sticky_imu_shake_duration_ms()),
                             "shake");
            }

            if (sticky_imu_get_state(imu_state) == ESP_OK) {
                if (router.baseline_orientation ==
                    StickyImuOrientation::Unknown) {
                    handle_route(sticky_app_router_observed(
                                     router,
                                     imu_state.observed_orientation),
                                 "observed");
                }

                if (router.launcher_open &&
                    imu_state.observed_orientation !=
                        StickyImuOrientation::Unknown &&
                    imu_state.observed_orientation != last_settled) {
                    const StickyAppRouteResult route =
                        sticky_app_router_rotation_candidate(
                            router,
                            imu_state.observed_orientation,
                            imu_state.orientation_stable_samples,
                            shake_active);
                    if (route.action != StickyAppRouteAction::None) {
                        last_settled = imu_state.observed_orientation;
                        handle_route(route, "rotation_fast");
                    }
                }
            }
        }


        const uint32_t now_ms = static_cast<uint32_t>(
            esp_timer_get_time() / 1000LL);
        if (router.launcher_open &&
            now_ms - s_last_user_activity_ms >= kLauncherIdleCloseMs) {
            cancel_launcher(router);
            s_last_user_activity_ms = now_ms;
            STICKY_LOGI(kTag,
                        "launcher=timeout idle_ms=%u result=closed",
                        static_cast<unsigned>(kLauncherIdleCloseMs));
        }

        if (s_background_timer_wake &&
            s_background_sleep_deadline_us > 0 &&
            esp_timer_get_time() >= s_background_sleep_deadline_us) {
            s_background_timer_wake = false;
            s_background_sleep_deadline_us = 0;
            enter_power_sleep(
                router,
                StickyPowerSleepTrigger::ScheduledEventComplete);
        }
        const uint32_t idle_sleep_timeout_ms = power_sleep_timeout_ms();
        if (!s_background_timer_wake && !router.launcher_open &&
            !board_charger_external_power_present() &&
            idle_sleep_timeout_ms > 0U && power_sleep_allowed() &&
            now_ms - s_last_user_activity_ms >= idle_sleep_timeout_ms) {
            enter_power_sleep(
                router, StickyPowerSleepTrigger::AppIdleTimeout);
        }

        vTaskDelay(kPollInterval);
    }
}

}  // namespace

esp_err_t sticky_app_start(Canvas &canvas)
{
    app_log_register_tag(kTag);
    if (s_app_task != nullptr) {
        return ESP_OK;
    }

    s_canvas = &canvas;
    const esp_sleep_wakeup_cause_t wake_cause =
        esp_sleep_get_wakeup_cause();
    const bool restore_sleep_context =
        wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED &&
        s_sleep_context.magic == kSleepContextMagic;
    const StickyAppId initial_app = restore_sleep_context
                                        ? s_sleep_context.app
                                        : StickyAppId::DesktopPet;
    const CanvasRotation initial_rotation = restore_sleep_context
                                                ? s_sleep_context.rotation
                                                : CanvasRotation::Deg0;
    s_sleep_context.magic = 0U;
    esp_err_t result = sticky_button_init();
    if (result != ESP_OK) {
        s_canvas = nullptr;
        return result;
    }

    if (initial_app == StickyAppId::Pomodoro) {
        pomodoro_app_set_display_rotation(initial_rotation);
    } else if (initial_app == StickyAppId::StatusBoard) {
        status_board_app_set_display_rotation(initial_rotation);
    }
    if (initial_app == StickyAppId::BookOfAnswers) {
        result = set_imu_running(true);
        if (result != ESP_OK) {
            s_canvas = nullptr;
            return result;
        }
    }
    result = start_app(initial_app);
    if (result != ESP_OK) {
        s_canvas = nullptr;
        return result;
    }
    mark_app_started(initial_app);
    s_current_app = initial_app;
    s_background_timer_wake =
        wake_cause == ESP_SLEEP_WAKEUP_TIMER &&
        initial_app == StickyAppId::DesktopPet;
    s_background_sleep_deadline_us = s_background_timer_wake
        ? esp_timer_get_time() + kBackgroundEventWindowUs
        : 0;
    s_last_user_activity_ms = static_cast<uint32_t>(
        esp_timer_get_time() / 1000LL);
    STICKY_LOGI(kTag,
                "power=restore wake=%d context=%s app=%s background_window_s=%u result=ok",
                static_cast<int>(wake_cause),
                restore_sleep_context ? "valid" : "default",
                sticky_app_id_name(initial_app),
                s_background_timer_wake
                    ? static_cast<unsigned>(kBackgroundEventWindowUs /
                                            1000000LL)
                    : 0U);

    if (xTaskCreate(app_task,
                    "sticky_launcher",
                    kTaskStackSize,
                    nullptr,
                    kTaskPriority,
                    &s_app_task) != pdPASS) {
        s_canvas = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
