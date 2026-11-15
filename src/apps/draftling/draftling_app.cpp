#include "draftling_app.h"

#include <cstring>

#include "app_log.h"
#include "draftling_pages.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sticky_app_lifecycle.h"
#include "sticky_display.h"
#include "sticky_touch.h"

namespace {

constexpr char kTag[] = "draftling_app";
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(40);
constexpr uint32_t kIdleSleepTimeoutMs = 2U * 60U * 1000U;

Canvas *s_canvas = nullptr;
TaskHandle_t s_task = nullptr;
StickyAppLifecycle s_lifecycle = {};
bool s_installed = false;
char s_version[32] = {};
volatile bool s_exit_requested = false;

const esp_partition_t *draftling_partition()
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                    ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
}

void probe()
{
    s_installed = false;
    s_version[0] = '\0';
    const esp_partition_t *part = draftling_partition();
    esp_app_desc_t desc = {};
    if (part != nullptr &&
        esp_ota_get_partition_description(part, &desc) == ESP_OK) {
        s_installed = true;
        std::strncpy(s_version, desc.version, sizeof(s_version) - 1U);
    }
    STICKY_LOGI(kTag, "draftling=probe installed=%d version=%s",
                s_installed, s_version);
}

void render()
{
    s_canvas->set_rotation(CanvasRotation::Deg90CounterClockwise);
    draftling_page_render(*s_canvas, {s_installed, s_version});
    sticky_display_refresh_monochrome();
    sticky_touch_clear_press();
}

void open_draftling()
{
    const esp_partition_t *part = draftling_partition();
    const esp_err_t result =
        part != nullptr ? esp_ota_set_boot_partition(part) : ESP_ERR_NOT_FOUND;
    STICKY_LOGI(kTag, "draftling=switch result=%s", esp_err_to_name(result));
    if (result != ESP_OK) {
        s_installed = false;
        render();
        return;
    }
    // Clean frame so the panel does not keep the Bunny page while
    // Draftling boots.
    s_canvas->clear(GrayLevel::White);
    sticky_display_refresh_monochrome();
    esp_restart();
}

void task(void *)
{
    render();
    while (true) {
        if (sticky_app_lifecycle_checkpoint(s_lifecycle)) {
            probe();
            render();
        }
        StickyTouchPress press = {};
        if (sticky_touch_take_press(press)) {
            int x = 0;
            int y = 0;
            s_canvas->physical_to_logical(press.x, press.y, x, y);
            switch (draftling_page_action_at(s_installed, x, y)) {
            case DraftlingAction::Open:
                open_draftling();
                break;
            case DraftlingAction::Back:
                s_exit_requested = true;
                break;
            case DraftlingAction::None:
                break;
            }
        }
        vTaskDelay(kPollInterval);
    }
}

}  // namespace

esp_err_t draftling_app_start(Canvas &canvas)
{
    app_log_register_tag(kTag);
    if (s_task != nullptr) {
        return ESP_OK;
    }
    s_canvas = &canvas;
    probe();
    if (xTaskCreate(task, "draftling_app", 4096, nullptr, 3, &s_task) !=
        pdPASS) {
        s_canvas = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t draftling_app_pause()
{
    return sticky_app_lifecycle_pause(s_lifecycle, s_task);
}

esp_err_t draftling_app_resume()
{
    if (s_task == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    sticky_app_lifecycle_resume(s_lifecycle);
    return ESP_OK;
}

uint32_t draftling_app_power_sleep_timeout_ms()
{
    return kIdleSleepTimeoutMs;
}

esp_err_t draftling_app_prepare_power_sleep()
{
    return draftling_app_pause();
}

bool draftling_app_take_exit_request()
{
    const bool requested = s_exit_requested;
    s_exit_requested = false;
    return requested;
}
