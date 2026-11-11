#include "todo_app.h"

#include <cstring>

#include "app_log.h"
#include "canvas.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sticky_app_lifecycle.h"
#include "sticky_buzzer.h"
#include "sticky_display.h"
#include "sticky_touch.h"
#include "text_utf8.h"
#include "todo_list.h"
#include "todo_pages.h"
#include "todo_storage.h"

namespace {

constexpr char kTag[] = "todo_app";
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(40);
constexpr uint32_t kTaskStackSize = 6144U;
constexpr UBaseType_t kTaskPriority = 3U;
constexpr uint32_t kIdleSleepTimeoutMs = 5U * 60U * 1000U;
constexpr int64_t kEditorCleanupDelayUs = 2LL * 1000LL * 1000LL;
constexpr unsigned kFullRefreshEveryFrames = 15U;

enum class TodoPage : uint8_t {
    List,
    Editor,
};

Canvas *s_canvas = nullptr;
TaskHandle_t s_app_task = nullptr;
StickyAppLifecycle s_lifecycle = {};

TodoList s_list = {};
TodoPage s_page = TodoPage::List;
size_t s_list_page = 0U;
bool s_dirty = false;

// Editor state.
char s_draft[kTodoTextBytes] = {};
int s_editing_index = -1;  // -1 = new item
TodoKeyboardMode s_mode = TodoKeyboardMode::Letters;
bool s_shift = true;  // capitalise the first letter of a new task
int64_t s_cleanup_deadline_us = 0;
unsigned s_partial_frames = 0U;

void save_if_dirty(const char *reason)
{
    (void)reason;
    if (!s_dirty) {
        return;
    }
    const esp_err_t result = todo_storage_save(s_list);
    if (result == ESP_OK) {
        s_dirty = false;
    }
    STICKY_LOGI(kTag, "todo=save reason=%s items=%u result=%s", reason,
                static_cast<unsigned>(s_list.count),
                esp_err_to_name(result));
}

void render(bool partial)
{
    s_canvas->set_rotation(CanvasRotation::Deg90CounterClockwise);
    if (s_page == TodoPage::List) {
        const size_t pages = todo_page_count(s_list);
        if (s_list_page >= pages) {
            s_list_page = pages - 1U;
        }
        todo_page_render_list(*s_canvas, {&s_list, s_list_page});
    } else {
        todo_page_render_editor(
            *s_canvas, {s_draft, s_editing_index >= 0, s_mode, s_shift});
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
        STICKY_LOGE(kTag, "todo=display result=%s", esp_err_to_name(result));
    }
}

void open_editor(int index)
{
    s_editing_index = index;
    if (index >= 0) {
        todo_copy_text(s_draft, sizeof(s_draft), s_list.items[index].text);
        s_shift = false;
    } else {
        s_draft[0] = '\0';
        s_shift = true;
    }
    s_mode = TodoKeyboardMode::Letters;
    s_page = TodoPage::Editor;
    s_cleanup_deadline_us = 0;
    render(false);
}

void close_editor()
{
    s_page = TodoPage::List;
    s_cleanup_deadline_us = 0;
    save_if_dirty("editor_closed");
    render(false);
}

void append_text(const char *text)
{
    const size_t used = std::strlen(s_draft);
    const size_t extra = std::strlen(text);
    if (used + extra >= sizeof(s_draft)) {
        return;
    }
    std::memcpy(s_draft + used, text, extra + 1U);
}

// Returns true when the editor needs a redraw.
bool handle_editor_hit(const TodoHit &hit)
{
    char buffer[8];
    switch (hit.action) {
    case TodoAction::Key:
        append_text(todo_key_text(s_mode, hit.index, s_shift, buffer,
                                  sizeof(buffer)));
        if (s_mode == TodoKeyboardMode::Letters) {
            s_shift = false;  // one-shot shift
        }
        return true;
    case TodoAction::Space:
        if (s_draft[0] != '\0') {
            append_text(" ");
        }
        return true;
    case TodoAction::Backspace:
        utf8_pop_back(s_draft);
        return true;
    case TodoAction::Shift:
        s_shift = !s_shift;
        return true;
    case TodoAction::Mode:
        s_mode = s_mode == TodoKeyboardMode::Letters
                     ? TodoKeyboardMode::Symbols
                     : TodoKeyboardMode::Letters;
        return true;
    case TodoAction::Save:
        if (todo_text_blank(s_draft)) {
            return false;
        }
        if (s_editing_index >= 0) {
            todo_list_set_text(s_list, static_cast<size_t>(s_editing_index),
                               s_draft);
        } else if (todo_list_add(s_list, s_draft) >= 0) {
            s_list_page = todo_page_count(s_list) - 1U;
        }
        s_dirty = true;
        close_editor();
        return false;
    case TodoAction::Delete:
        if (s_editing_index >= 0) {
            todo_list_remove(s_list, static_cast<size_t>(s_editing_index));
            s_dirty = true;
        }
        close_editor();
        return false;
    case TodoAction::Cancel:
        close_editor();
        return false;
    default:
        return false;
    }
}

void handle_list_hit(const TodoHit &hit)
{
    switch (hit.action) {
    case TodoAction::ToggleItem:
        todo_list_toggle(s_list, hit.index);
        if (s_list.items[hit.index].done != 0U) {
            sticky_buzzer_play_pattern(StickyBuzzerPattern::TaskDone);
        }
        s_dirty = true;
        save_if_dirty("toggle");
        render(true);
        return;
    case TodoAction::EditItem:
        open_editor(static_cast<int>(hit.index));
        return;
    case TodoAction::Add:
        if (s_list.count < kTodoMaxItems) {
            open_editor(-1);
        }
        return;
    case TodoAction::ClearDone:
        if (todo_list_clear_done(s_list) > 0U) {
            s_dirty = true;
            save_if_dirty("clear_done");
            render(false);
        }
        return;
    case TodoAction::PrevPage:
        if (s_list_page > 0U) {
            --s_list_page;
            render(true);
        }
        return;
    case TodoAction::NextPage:
        if (s_list_page + 1U < todo_page_count(s_list)) {
            ++s_list_page;
            render(true);
        }
        return;
    default:
        return;
    }
}

void handle_press(StickyTouchPress press)
{
    int x = 0;
    int y = 0;
    s_canvas->physical_to_logical(press.x, press.y, x, y);
    if (s_page == TodoPage::List) {
        const TodoHit hit = todo_page_list_hit(s_list, s_list_page, x, y);
        if (hit.action != TodoAction::None) {
            STICKY_LOGI(kTag, "todo=touch page=list action=%s index=%u",
                        todo_action_name(hit.action),
                        static_cast<unsigned>(hit.index));
        }
        handle_list_hit(hit);
        sticky_touch_clear_press();
        return;
    }

    // Editor: apply every queued key press, then refresh once so fast
    // typing is not throttled by the e-paper refresh time.
    bool redraw = false;
    while (true) {
        const TodoHit hit =
            todo_page_editor_hit(s_editing_index >= 0, s_mode, x, y);
        const bool leaves_editor = hit.action == TodoAction::Save ||
                                   hit.action == TodoAction::Cancel ||
                                   hit.action == TodoAction::Delete;
        if (handle_editor_hit(hit)) {
            redraw = true;
        }
        if (leaves_editor && s_page != TodoPage::Editor) {
            return;
        }
        if (!sticky_touch_take_press(press)) {
            break;
        }
        s_canvas->physical_to_logical(press.x, press.y, x, y);
    }
    if (redraw) {
        render(true);
        s_cleanup_deadline_us = esp_timer_get_time() + kEditorCleanupDelayUs;
    }
}

void app_task(void *)
{
    render(false);
    while (true) {
        if (sticky_app_lifecycle_checkpoint(s_lifecycle)) {
            sticky_touch_clear_press();
            s_cleanup_deadline_us = 0;
            render(false);
        }

        StickyTouchPress press = {};
        if (sticky_touch_take_press(press)) {
            handle_press(press);
        }

        if (s_cleanup_deadline_us != 0 &&
            esp_timer_get_time() >= s_cleanup_deadline_us) {
            // One full refresh after typing pauses removes ghosting.
            s_cleanup_deadline_us = 0;
            render(false);
        }
        vTaskDelay(kPollInterval);
    }
}

}  // namespace

esp_err_t todo_app_start(Canvas &canvas)
{
    app_log_register_tag(kTag);
    if (s_app_task != nullptr) {
        return ESP_OK;
    }
    const esp_err_t load_result = todo_storage_load(s_list);
    STICKY_LOGI(kTag, "todo=load items=%u result=%s",
                static_cast<unsigned>(s_list.count),
                esp_err_to_name(load_result));
    (void)load_result;
    s_canvas = &canvas;
    if (xTaskCreate(app_task, "todo_app", kTaskStackSize, nullptr,
                    kTaskPriority, &s_app_task) != pdPASS) {
        s_canvas = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t todo_app_pause()
{
    const esp_err_t result =
        sticky_app_lifecycle_pause(s_lifecycle, s_app_task);
    if (result == ESP_OK) {
        save_if_dirty("pause");
    }
    return result;
}

esp_err_t todo_app_resume()
{
    if (s_app_task == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    sticky_app_lifecycle_resume(s_lifecycle);
    return ESP_OK;
}

bool todo_app_power_sleep_allowed()
{
    return true;
}

esp_err_t todo_app_prepare_power_sleep()
{
    const esp_err_t result = todo_app_pause();
    if (result == ESP_OK && s_page == TodoPage::Editor) {
        // Wake up on the list; an unsaved draft is discarded.
        s_page = TodoPage::List;
    }
    return result;
}

uint32_t todo_app_power_sleep_timeout_ms()
{
    return kIdleSleepTimeoutMs;
}
