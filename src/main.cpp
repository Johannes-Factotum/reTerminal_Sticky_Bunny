#include "app_log.h"
#include "board_charger.h"
#include "board_power.h"
#include "board_sensor_bus.h"
#include "board_shared_spi.h"
#include "canvas.h"
#include "onboarding_app.h"
#include "sticky_app.h"
#include "sticky_battery.h"
#include "sticky_buzzer.h"
#include "sticky_display.h"
#include "sticky_imu.h"
#include "sticky_rtc.h"
#include "sticky_touch.h"

#include <cinttypes>

#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#ifndef STICKY_FIRMWARE_VERSION
#define STICKY_FIRMWARE_VERSION "unknown"
#endif

#ifndef STICKY_BUILD_PROFILE
#define STICKY_BUILD_PROFILE "unknown"
#endif

namespace {

constexpr char kTag[] = "sticky_boot";
constexpr TickType_t kHeartbeatInterval = pdMS_TO_TICKS(30000);

// Keeps the device alive after a required startup component fails.
// 必要组件启动失败后保持设备运行，方便串口持续保留最后一条错误信息。
[[noreturn]] void halt_after_error(const char *component, esp_err_t result)
{
    STICKY_LOGE(kTag,
                "phase=halt component=%s result=%s",
                component,
                esp_err_to_name(result));
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

const char *reset_reason_name(esp_reset_reason_t reason)
{
    // Convert hardware reset codes into stable text fields for log comparison.
    // 将硬件复位代码转换成稳定文本，方便对比不同测试日志。
    switch (reason) {
    case ESP_RST_POWERON:
        return "power_on";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:
        return "task_watchdog";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep_sleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    case ESP_RST_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *wakeup_reason_name(esp_sleep_wakeup_cause_t cause)
{
    // Deep-sleep wake sources use a separate ESP-IDF enumeration from reset reasons.
    // 深度睡眠唤醒原因使用另一套枚举，因此在这里单独转换。
    switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0:
        return "ext0";
    case ESP_SLEEP_WAKEUP_EXT1:
        return "ext1";
    case ESP_SLEEP_WAKEUP_TIMER:
        return "timer";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        return "touchpad";
    case ESP_SLEEP_WAKEUP_ULP:
        return "ulp";
    case ESP_SLEEP_WAKEUP_GPIO:
        return "gpio";
    case ESP_SLEEP_WAKEUP_UART:
        return "uart";
    case ESP_SLEEP_WAKEUP_WIFI:
        return "wifi";
    case ESP_SLEEP_WAKEUP_COCPU:
        return "coprocessor";
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
        return "coprocessor_trap";
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    case ESP_SLEEP_WAKEUP_ALL:
    default:
        return "undefined";
    }
}

#if STICKY_LOG_BOOT_DETAILS_ENABLED
void log_system_details()
{
    // These diagnostics are compiled only into profiles that request boot details.
    // 这些硬件诊断只会编译进启用了启动详情的固件配置。
    esp_chip_info_t chip = {};
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    const esp_err_t flash_result = esp_flash_get_size(nullptr, &flash_size);

    STICKY_LOGD(kTag,
                "system=idf version=%s chip_model=%d revision=%d cores=%d",
                esp_get_idf_version(),
                static_cast<int>(chip.model),
                static_cast<int>(chip.revision),
                static_cast<int>(chip.cores));

    if (flash_result == ESP_OK) {
        STICKY_LOGD(kTag,
                    "memory=flash size_bytes=%" PRIu32,
                    flash_size);
    } else {
        STICKY_LOGW(kTag,
                    "memory=flash result=%s",
                    esp_err_to_name(flash_result));
    }

    STICKY_LOGD(
        kTag,
        "memory=heap internal_free=%u internal_total=%u psram_free=%u psram_total=%u",
        static_cast<unsigned>(
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(
            heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
        static_cast<unsigned>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM)));
}
#endif

}  // namespace

extern "C" void app_main()
{
    app_log_init();
    app_log_register_tag(kTag);

    STICKY_LOGI(kTag,
                "phase=start firmware=%s profile=%s compile_level=%d",
                STICKY_FIRMWARE_VERSION,
                STICKY_BUILD_PROFILE,
                STICKY_LOG_COMPILE_LEVEL);
    STICKY_LOGI(kTag,
                "boot=reason reset=%s wake=%s",
                reset_reason_name(esp_reset_reason()),
                wakeup_reason_name(esp_sleep_get_wakeup_cause()));

    const esp_err_t power_result = board_power_init();
    if (power_result != ESP_OK) {
        halt_after_error("board_power", power_result);
    }

    // Enable the same active-low battery charging path as the hardware demo.
    // 启用与硬件示例相同的低电平有效电池充电路径。
    const esp_err_t charger_result = board_charger_init();
    if (charger_result != ESP_OK) {
        halt_after_error("board_charger", charger_result);
    }

    // Isolate the other SPI2 device before the e-paper driver owns the bus.
    // 在电子纸驱动接管SPI2前，先让共享总线上的SD卡进入确定的空闲状态。
    const esp_err_t shared_spi_result = board_shared_spi_prepare();
    if (shared_spi_result != ESP_OK) {
        halt_after_error("board_shared_spi", shared_spi_result);
    }

    const esp_err_t sensor_bus_result = board_sensor_bus_init();
    if (sensor_bus_result != ESP_OK) {
        halt_after_error("board_sensor_bus", sensor_bus_result);
    }

    // Attach the read-only clock before the desktop pet reads its saved time.
    // 在桌宠读取存档时间前，先挂载只读RTC设备。
    const esp_err_t rtc_result =
        sticky_rtc_init(board_sensor_i2c_bus());
    if (rtc_result != ESP_OK) {
        STICKY_LOGW(kTag,
                    "component=sticky_rtc result=%s fallback=app_timer",
                    esp_err_to_name(rtc_result));
    }

    // Read percentage from the same BQ27220 fuel gauge used by the hardware
    // reference instead of estimating charge from battery voltage.
    // 使用硬件示例中的BQ27220直接读取百分比，不通过电压估算电量。
    const esp_err_t battery_result =
        sticky_battery_init(board_sensor_i2c_bus());
    if (battery_result != ESP_OK) {
        STICKY_LOGW(kTag,
                    "component=sticky_battery result=%s fallback=unknown_percent",
                    esp_err_to_name(battery_result));
    }

    const esp_err_t display_result = sticky_display_init();
    if (display_result != ESP_OK) {
        halt_after_error("sticky_display_init", display_result);
    }

    Canvas *canvas = sticky_display_canvas();
    if (canvas == nullptr) {
        halt_after_error("sticky_display_canvas", ESP_ERR_INVALID_STATE);
    }

    // A physical white full refresh removes the image retained by e-paper
    // before the first desktop-pet frame becomes the new baseline.
    // 先对白屏执行一次实体全刷，清除电子纸保留的旧画面，再建立桌宠首帧基线。
    const bool woke_from_deep_sleep =
        esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
    if (!woke_from_deep_sleep) {
        const esp_err_t clear_result = sticky_display_clear();
        if (clear_result != ESP_OK) {
            halt_after_error("sticky_display_clear", clear_result);
        }
    } else {
        STICKY_LOGI(kTag,
                    "display=clear action=skipped reason=deep_sleep_wake");
    }

    const esp_err_t touch_result = sticky_touch_init();
    if (touch_result != ESP_OK) {
        halt_after_error("sticky_touch_init", touch_result);
    }

    const esp_err_t buzzer_result = sticky_buzzer_init();
    if (buzzer_result != ESP_OK) {
        halt_after_error("sticky_buzzer_init", buzzer_result);
    }

    const esp_err_t imu_result =
        sticky_imu_init(board_sensor_i2c_bus());
    if (imu_result != ESP_OK) {
        halt_after_error("sticky_imu_init", imu_result);
    }

    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // A changed partition layout (or a newer NVS format written by the
        // Draftling firmware) leaves an unreadable NVS: start fresh.
        nvs_flash_erase();
        nvs_result = nvs_flash_init();
    }
    if (nvs_result != ESP_OK) {
        halt_after_error("nvs_flash_init", nvs_result);
    }

    // Presents the six-page first-boot guide before background app tasks run.
    // 在后台APP任务启动前展示六页首次开机教程。
    const esp_err_t onboarding_result =
        onboarding_app_run_if_needed(*canvas);
    if (onboarding_result != ESP_OK) {
        STICKY_LOGW(kTag,
                    "onboarding=start result=%s fallback=continue",
                    esp_err_to_name(onboarding_result));
    }

    // Starts the default pet and top-button application launcher.
    // 启动默认桌宠和顶部按键应用选择器。
    const esp_err_t app_result = sticky_app_start(*canvas);
    if (app_result != ESP_OK) {
        halt_after_error("sticky_app_start", app_result);
    }

#if STICKY_LOG_BOOT_DETAILS_ENABLED
    log_system_details();
#endif

    STICKY_LOGI(kTag, "phase=ready result=ok");

    while (true) {
#if STICKY_LOG_HEARTBEAT_ENABLED
        STICKY_LOGT(
            kTag,
            "heartbeat=alive uptime_ms=%" PRIu32 " internal_free=%u psram_free=%u",
            static_cast<uint32_t>(esp_log_timestamp()),
            static_cast<unsigned>(
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
#endif
        vTaskDelay(kHeartbeatInterval);
    }
}
