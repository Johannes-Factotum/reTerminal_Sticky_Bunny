#include "sticky_buzzer.h"

#include <atomic>
#include <cstddef>

#include "app_log.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace {

constexpr char kTag[] = "sticky_buzzer";
constexpr ledc_mode_t kSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;
constexpr uint32_t kAlarmDuty = 256U;
constexpr uint32_t kDefaultFrequencyHz = 1047U;
constexpr TickType_t kCheckInterval = pdMS_TO_TICKS(20);
constexpr uint32_t kTaskStackSize = 3072U;
constexpr UBaseType_t kTaskPriority = 4U;

struct BuzzerNote {
    uint32_t frequency_hz;
    uint32_t duration_ms;
    uint32_t gap_ms;
    uint32_t duty;
};

struct BuzzerSequence {
    const BuzzerNote *notes;
    size_t count;
    const char *name;
};

// Pomodoro "time is up" jingle: a bright rising arpeggio answered by a
// short cadence, repeated after a pause until the user taps END.
constexpr BuzzerNote kAlarmNotes[] = {
    {1047U, 110U, 30U, kAlarmDuty},   // C6
    {1319U, 110U, 30U, kAlarmDuty},   // E6
    {1568U, 110U, 30U, kAlarmDuty},   // G6
    {2093U, 260U, 90U, kAlarmDuty},   // C7
    {1760U, 110U, 30U, kAlarmDuty},   // A6
    {1568U, 110U, 30U, kAlarmDuty},   // G6
    {1760U, 110U, 30U, kAlarmDuty},   // A6
    {2093U, 420U, 1400U, kAlarmDuty}, // C7, then rest
};

constexpr BuzzerNote kFocusStartNotes[] = {
    {1319U, 70U, 40U, 110U},
    {1760U, 120U, 0U, 120U},
};

constexpr BuzzerNote kTaskDoneNotes[] = {
    {1568U, 45U, 25U, 100U},
    {2093U, 90U, 0U, 110U},
};

constexpr BuzzerNote kSessionCompleteNotes[] = {
    {1047U, 90U, 30U, 120U},
    {1319U, 90U, 30U, 120U},
    {1568U, 90U, 30U, 120U},
    {2093U, 220U, 0U, 130U},
};

constexpr BuzzerNote kHatchNotes[] = {
    {784U, 110U, 70U, 140U},
    {988U, 130U, 70U, 140U},
    {1319U, 260U, 0U, 160U},
};

constexpr BuzzerNote kEatNotes[] = {
    {1568U, 35U, 25U, 75U},
    {1175U, 55U, 45U, 90U},
    {1568U, 35U, 25U, 75U},
    {1175U, 70U, 0U, 90U},
};

constexpr BuzzerNote kCooShyNotes[] = {
    {1047U, 55U, 40U, 80U},
    {1175U, 90U, 0U, 85U},
};

constexpr BuzzerNote kCooWarmNotes[] = {
    {784U, 65U, 40U, 80U},
    {988U, 75U, 40U, 90U},
    {1175U, 120U, 0U, 95U},
};

constexpr BuzzerNote kPlayNotes[] = {
    {988U, 50U, 30U, 100U},
    {1319U, 55U, 30U, 110U},
    {1568U, 70U, 30U, 115U},
    {1319U, 100U, 0U, 105U},
};

constexpr BuzzerNote kPlayTiredNotes[] = {
    {988U, 65U, 40U, 90U},
    {784U, 90U, 45U, 80U},
    {659U, 130U, 0U, 75U},
};

constexpr BuzzerNote kVoiceShyNotes[] = {
    {1175U, 55U, 35U, 80U},
    {1319U, 75U, 0U, 85U},
};

constexpr BuzzerNote kVoiceWarmNotes[] = {
    {988U, 55U, 30U, 85U},
    {1175U, 65U, 30U, 90U},
    {1319U, 90U, 0U, 95U},
};

constexpr BuzzerNote kVoiceCloseNotes[] = {
    {988U, 55U, 30U, 90U},
    {1319U, 65U, 30U, 100U},
    {1568U, 95U, 0U, 105U},
};

constexpr BuzzerNote kVoiceHungryNotes[] = {
    {784U, 90U, 55U, 85U},
    {988U, 70U, 45U, 90U},
    {784U, 120U, 0U, 80U},
};

constexpr BuzzerNote kVoiceTiredNotes[] = {
    {784U, 100U, 55U, 75U},
    {659U, 150U, 0U, 70U},
};

constexpr BuzzerNote kVoiceSadNotes[] = {
    {784U, 85U, 55U, 75U},
    {698U, 110U, 55U, 72U},
    {587U, 160U, 0U, 68U},
};

constexpr BuzzerNote kVoiceFoodieNotes[] = {
    {1175U, 45U, 25U, 90U},
    {1568U, 55U, 35U, 100U},
    {1319U, 85U, 0U, 95U},
};

constexpr BuzzerNote kVoiceAffectionateNotes[] = {
    {784U, 70U, 35U, 80U},
    {988U, 90U, 35U, 90U},
    {1175U, 130U, 0U, 95U},
};

constexpr BuzzerNote kVoiceActiveNotes[] = {
    {1175U, 40U, 25U, 95U},
    {1568U, 45U, 25U, 110U},
    {1760U, 55U, 25U, 115U},
    {1568U, 75U, 0U, 105U},
};

constexpr BuzzerNote kSleepNotes[] = {
    {988U, 100U, 55U, 85U},
    {784U, 120U, 55U, 80U},
    {659U, 180U, 0U, 70U},
};

constexpr BuzzerNote kWakeNotes[] = {
    {659U, 75U, 45U, 80U},
    {988U, 90U, 45U, 90U},
    {1319U, 140U, 0U, 100U},
};

constexpr BuzzerNote kPowerSleepNotes[] = {
    {784U, 110U, 0U, 72U},
};

TaskHandle_t s_buzzer_task = nullptr;
std::atomic<StickyBuzzerPattern> s_pattern = StickyBuzzerPattern::None;
std::atomic<uint8_t> s_variant = 0U;

template <size_t N>
constexpr BuzzerSequence sequence(const BuzzerNote (&notes)[N],
                                  const char *name)
{
    return {notes, N, name};
}

BuzzerSequence sequence_for_pattern(StickyBuzzerPattern pattern)
{
    switch (pattern) {
    case StickyBuzzerPattern::Alarm:
        return sequence(kAlarmNotes, "alarm");
    case StickyBuzzerPattern::Hatch:
        return sequence(kHatchNotes, "hatch");
    case StickyBuzzerPattern::Eat:
        return sequence(kEatNotes, "eat");
    case StickyBuzzerPattern::CooShy:
        return sequence(kCooShyNotes, "coo_shy");
    case StickyBuzzerPattern::CooWarm:
        return sequence(kCooWarmNotes, "coo_warm");
    case StickyBuzzerPattern::Play:
        return sequence(kPlayNotes, "play");
    case StickyBuzzerPattern::PlayTired:
        return sequence(kPlayTiredNotes, "play_tired");
    case StickyBuzzerPattern::VoiceShy:
        return sequence(kVoiceShyNotes, "voice_shy");
    case StickyBuzzerPattern::VoiceWarm:
        return sequence(kVoiceWarmNotes, "voice_warm");
    case StickyBuzzerPattern::VoiceClose:
        return sequence(kVoiceCloseNotes, "voice_close");
    case StickyBuzzerPattern::VoiceHungry:
        return sequence(kVoiceHungryNotes, "voice_hungry");
    case StickyBuzzerPattern::VoiceTired:
        return sequence(kVoiceTiredNotes, "voice_tired");
    case StickyBuzzerPattern::VoiceSad:
        return sequence(kVoiceSadNotes, "voice_sad");
    case StickyBuzzerPattern::VoiceFoodie:
        return sequence(kVoiceFoodieNotes, "voice_foodie");
    case StickyBuzzerPattern::VoiceAffectionate:
        return sequence(kVoiceAffectionateNotes, "voice_affectionate");
    case StickyBuzzerPattern::VoiceActive:
        return sequence(kVoiceActiveNotes, "voice_active");
    case StickyBuzzerPattern::Sleep:
        return sequence(kSleepNotes, "sleep");
    case StickyBuzzerPattern::Wake:
        return sequence(kWakeNotes, "wake");
    case StickyBuzzerPattern::PowerSleep:
        return sequence(kPowerSleepNotes, "power_sleep");
    case StickyBuzzerPattern::FocusStart:
        return sequence(kFocusStartNotes, "focus_start");
    case StickyBuzzerPattern::TaskDone:
        return sequence(kTaskDoneNotes, "task_done");
    case StickyBuzzerPattern::SessionComplete:
        return sequence(kSessionCompleteNotes, "session_complete");
    case StickyBuzzerPattern::None:
    default:
        return {nullptr, 0U, "none"};
    }
}

esp_err_t set_duty(uint32_t duty)
{
    ESP_RETURN_ON_ERROR(
        ledc_set_duty(kSpeedMode, kChannel, duty), kTag, "set duty");
    return ledc_update_duty(kSpeedMode, kChannel);
}

bool wait_while_pattern(StickyBuzzerPattern pattern, uint32_t duration_ms)
{
    TickType_t remaining = pdMS_TO_TICKS(duration_ms);
    while (remaining > 0 && s_pattern.load() == pattern) {
        const TickType_t slice = remaining > kCheckInterval
                                     ? kCheckInterval
                                     : remaining;
        vTaskDelay(slice);
        remaining -= slice;
    }
    return s_pattern.load() == pattern;
}

uint32_t frequency_for_variant(uint32_t frequency_hz, uint8_t variant)
{
    switch (variant % 3U) {
    case 1U:
        return frequency_hz * 106U / 100U;
    case 2U:
        return frequency_hz * 94U / 100U;
    case 0U:
    default:
        return frequency_hz;
    }
}

esp_err_t play_note(const BuzzerNote &note,
                    StickyBuzzerPattern pattern,
                    uint8_t variant)
{
    ESP_RETURN_ON_ERROR(
        ledc_set_freq(kSpeedMode,
                      kTimer,
                      frequency_for_variant(note.frequency_hz, variant)),
        kTag,
        "set note frequency");
    if (s_pattern.load() != pattern) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(set_duty(note.duty), kTag, "start note");
    const bool keep_playing = wait_while_pattern(pattern, note.duration_ms);
    ESP_RETURN_ON_ERROR(set_duty(0U), kTag, "finish note");
    if (keep_playing) {
        wait_while_pattern(pattern, note.gap_ms);
    }
    return ESP_OK;
}

esp_err_t play_one_shot(StickyBuzzerPattern pattern, uint8_t variant)
{
    const BuzzerSequence selected = sequence_for_pattern(pattern);
    esp_err_t result = ESP_OK;
    for (size_t index = 0U; index < selected.count; ++index) {
        if (s_pattern.load() != pattern) {
            break;
        }
        result = play_note(selected.notes[index], pattern, variant);
        if (result != ESP_OK) {
            break;
        }
    }

    StickyBuzzerPattern expected = pattern;
    const bool completed = s_pattern.compare_exchange_strong(
        expected, StickyBuzzerPattern::None);
    if (result == ESP_OK && completed) {
        STICKY_LOGD(kTag,
                    "buzzer=pattern name=%s state=finished result=ok",
                    selected.name);
    } else if (result != ESP_OK) {
        STICKY_LOGW(kTag,
                    "buzzer=pattern name=%s state=failed result=%s",
                    selected.name,
                    esp_err_to_name(result));
    }
    return result;
}

// Owns LEDC playback so screen refresh and touch polling remain independent.
// 在独立任务中驱动LEDC，让屏幕刷新和触摸轮询保持独立。
void buzzer_task(void *)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (s_pattern.load() == StickyBuzzerPattern::Alarm) {
            for (const BuzzerNote &note : kAlarmNotes) {
                if (s_pattern.load() != StickyBuzzerPattern::Alarm) {
                    break;
                }
                const esp_err_t result = play_note(
                    note, StickyBuzzerPattern::Alarm, 0U);
                if (result != ESP_OK) {
                    s_pattern.store(StickyBuzzerPattern::None);
                    set_duty(0U);
                    STICKY_LOGW(kTag,
                                "buzzer=alarm state=failed result=%s",
                                esp_err_to_name(result));
                    break;
                }
            }
        }

        const StickyBuzzerPattern one_shot = s_pattern.load();
        if (one_shot != StickyBuzzerPattern::None &&
            one_shot != StickyBuzzerPattern::Alarm) {
            play_one_shot(one_shot, s_variant.load());
        }
        set_duty(0U);
    }
}

// Replaces the active pet sound and wakes the background playback task.
// 替换当前宠物声音，并唤醒后台播放任务。
esp_err_t start_one_shot(StickyBuzzerPattern pattern, uint8_t variant)
{
    if (s_buzzer_task == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    s_variant.store(static_cast<uint8_t>(variant % 3U));
    s_pattern.store(pattern);
    xTaskNotifyGive(s_buzzer_task);
    STICKY_LOGD(kTag,
                "buzzer=pattern name=%s variant=%u state=started",
                sequence_for_pattern(pattern).name,
                static_cast<unsigned>(variant % 3U));
    return ESP_OK;
}

}  // namespace

esp_err_t sticky_buzzer_init()
{
    app_log_register_tag(kTag);

    // Uses the reference driver's GPIO48, low-speed LEDC, timer and 10-bit duty.
    // 沿用参考驱动的GPIO48、低速LEDC、定时器和10位占空比配置。
    ledc_timer_config_t timer_config = {};
    timer_config.speed_mode = kSpeedMode;
    timer_config.timer_num = kTimer;
    timer_config.duty_resolution = LEDC_TIMER_10_BIT;
    timer_config.freq_hz = kDefaultFrequencyHz;
    timer_config.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(
        ledc_timer_config(&timer_config), kTag, "configure timer");

    ledc_channel_config_t channel_config = {};
    channel_config.gpio_num = PIN_BUZZER;
    channel_config.speed_mode = kSpeedMode;
    channel_config.channel = kChannel;
    channel_config.intr_type = LEDC_INTR_DISABLE;
    channel_config.timer_sel = kTimer;
    channel_config.duty = 0U;
    channel_config.hpoint = 0;
    ESP_RETURN_ON_ERROR(
        ledc_channel_config(&channel_config), kTag, "configure channel");

    if (xTaskCreate(buzzer_task,
                    "sticky_buzzer",
                    kTaskStackSize,
                    nullptr,
                    kTaskPriority,
                    &s_buzzer_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    STICKY_LOGI(kTag,
                "buzzer=ready pin=%d patterns=alarm,hatch,pet_state result=ok",
                PIN_BUZZER);
    return ESP_OK;
}

esp_err_t sticky_buzzer_start_alarm()
{
    if (s_buzzer_task == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_pattern.exchange(StickyBuzzerPattern::Alarm) !=
        StickyBuzzerPattern::Alarm) {
        s_variant.store(0U);
        xTaskNotifyGive(s_buzzer_task);
        STICKY_LOGI(kTag, "buzzer=alarm state=started pattern=jingle");
    }
    return ESP_OK;
}

esp_err_t sticky_buzzer_play_hatch_chime()
{
    const esp_err_t result = start_one_shot(StickyBuzzerPattern::Hatch, 0U);
    if (result == ESP_OK) {
        STICKY_LOGI(kTag,
                    "buzzer=hatch_chime state=started pattern=gentle_three_note");
    }
    return result;
}

esp_err_t sticky_buzzer_play_power_sleep_chime()
{
    constexpr TickType_t kCompletionPoll = pdMS_TO_TICKS(10);
    constexpr TickType_t kCompletionTimeout = pdMS_TO_TICKS(300);
    const esp_err_t result = start_one_shot(
        StickyBuzzerPattern::PowerSleep, 0U);
    if (result != ESP_OK) {
        return result;
    }

    // Deep sleep turns off the buzzer rail, so this short confirmation tone
    // must finish before the peripheral shutdown sequence begins.
    // 深度睡眠会关闭蜂鸣器供电，因此需等待提示音结束后再关闭外设。
    const TickType_t started_at = xTaskGetTickCount();
    while (s_pattern.load() == StickyBuzzerPattern::PowerSleep &&
           xTaskGetTickCount() - started_at < kCompletionTimeout) {
        vTaskDelay(kCompletionPoll);
    }
    if (s_pattern.load() == StickyBuzzerPattern::PowerSleep) {
        s_pattern.store(StickyBuzzerPattern::None);
        set_duty(0U);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t sticky_buzzer_play_pattern(StickyBuzzerPattern pattern,
                                     uint8_t variant)
{
    if (pattern == StickyBuzzerPattern::None) {
        return ESP_OK;
    }
    if (pattern == StickyBuzzerPattern::Alarm ||
        pattern == StickyBuzzerPattern::Hatch) {
        return ESP_ERR_INVALID_ARG;
    }
    return start_one_shot(pattern, variant);
}

esp_err_t sticky_buzzer_stop()
{
    const StickyBuzzerPattern previous = s_pattern.exchange(
        StickyBuzzerPattern::None);
    ESP_RETURN_ON_ERROR(set_duty(0U), kTag, "stop alarm");
    if (previous != StickyBuzzerPattern::None) {
        STICKY_LOGI(kTag,
                    "buzzer=stopped previous=%s",
                    sequence_for_pattern(previous).name);
    }
    return ESP_OK;
}
