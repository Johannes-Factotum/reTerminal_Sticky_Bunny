#include "flashcards_app.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "app_log.h"
#include "canvas.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "flashcards_deck.h"
#include "flashcards_pages.h"
#include "flashcards_srs.h"
#include "flashcards_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sticky_app_lifecycle.h"
#include "sticky_buzzer.h"
#include "sticky_display.h"
#include "sticky_rtc.h"
#include "sticky_touch.h"

namespace {

constexpr char kTag[] = "flashcards_app";
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(40);
constexpr uint32_t kTaskStackSize = 6144U;
constexpr UBaseType_t kTaskPriority = 3U;
constexpr uint32_t kIdleSleepTimeoutMs = 5U * 60U * 1000U;
constexpr size_t kQueueCapacity = 1024U;
constexpr unsigned kSaveEveryAnswers = 10U;
constexpr unsigned kFullRefreshEveryFrames = 12U;
constexpr size_t kExtraNewStep = 10U;

Canvas *s_canvas = nullptr;
TaskHandle_t s_app_task = nullptr;
StickyAppLifecycle s_lifecycle = {};

size_t s_deck_index = 0U;
const FlashcardsDeck *s_deck = nullptr;
FlashcardState *s_states = nullptr;
size_t s_states_capacity = 0U;
FlashcardsProgressHeader s_header = {};
bool s_storage_ok = false;
bool s_dirty = false;
unsigned s_unsaved_answers = 0U;

uint16_t s_today = 1U;
size_t s_bonus_new = 0U;
uint16_t s_bonus_day = 0U;

FlashcardsPage s_page = FlashcardsPage::Home;
uint16_t s_queue[kQueueCapacity];
size_t s_queue_length = 0U;
size_t s_position = 0U;
bool s_answer_shown = false;
size_t s_session_reviewed = 0U;
size_t s_session_again = 0U;
unsigned s_partial_frames = 0U;

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
    // Without a valid clock keep using the last known study day.
    return s_header.new_day != 0U ? s_header.new_day : s_today;
}

uint16_t new_per_day()
{
    const uint8_t index =
        s_header.new_per_day_index < kFlashcardsNewPerDayOptionCount
            ? s_header.new_per_day_index
            : kFlashcardsDefaultNewPerDayIndex;
    return kFlashcardsNewPerDayOptions[index];
}

void roll_day()
{
    s_today = read_today();
    if (s_header.new_day != s_today) {
        s_header.new_day = s_today;
        s_header.new_today = 0U;
        s_dirty = true;
    }
    if (s_bonus_day != s_today) {
        s_bonus_day = s_today;
        s_bonus_new = 0U;
    }
}

size_t new_allowed_today()
{
    const size_t limit = new_per_day() + s_bonus_new;
    return s_header.new_today >= limit ? 0U : limit - s_header.new_today;
}

void save_progress(const char *reason)
{
    (void)reason;
    if (!s_dirty || s_deck == nullptr || s_states == nullptr) {
        return;
    }
    const esp_err_t result =
        flashcards_storage_save(s_deck_index, s_header, s_states);
    s_storage_ok = result == ESP_OK;
    if (s_storage_ok) {
        s_dirty = false;
        s_unsaved_answers = 0U;
    }
    STICKY_LOGI(kTag, "flashcards=save reason=%s result=%s", reason,
                esp_err_to_name(result));
}

esp_err_t load_deck(size_t index)
{
    if (index >= flashcards_deck_count()) {
        index = 0U;
    }
    const FlashcardsDeck &deck = flashcards_deck(index);
    if (deck.card_count > s_states_capacity) {
        std::free(s_states);
        s_states = static_cast<FlashcardState *>(heap_caps_malloc(
            deck.card_count * sizeof(FlashcardState),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (s_states == nullptr) {
            s_states = static_cast<FlashcardState *>(
                std::malloc(deck.card_count * sizeof(FlashcardState)));
        }
        s_states_capacity = s_states == nullptr ? 0U : deck.card_count;
        if (s_states == nullptr) {
            s_deck = nullptr;
            return ESP_ERR_NO_MEM;
        }
    }
    s_deck_index = index;
    s_deck = &deck;
    const esp_err_t result = flashcards_storage_load(
        index, flashcards_deck_hash(deck.name, deck.card_count),
        deck.card_count, s_states, s_header);
    s_storage_ok = result == ESP_OK || result == ESP_ERR_NOT_FOUND;
    s_dirty = false;
    s_unsaved_answers = 0U;
    roll_day();
    STICKY_LOGI(kTag, "flashcards=deck index=%u name=%s cards=%u storage=%s",
                static_cast<unsigned>(index), deck.name,
                static_cast<unsigned>(deck.card_count),
                esp_err_to_name(result));
    return ESP_OK;
}

const char *card_prompt(uint16_t card)
{
    return s_header.reverse != 0U ? flashcards_deck_back(*s_deck, card)
                                  : flashcards_deck_front(*s_deck, card);
}

const char *card_answer(uint16_t card)
{
    return s_header.reverse != 0U ? flashcards_deck_front(*s_deck, card)
                                  : flashcards_deck_back(*s_deck, card);
}

void refresh(bool partial)
{
    esp_err_t result = ESP_OK;
    if (partial && s_partial_frames < kFullRefreshEveryFrames) {
        ++s_partial_frames;
        result = sticky_display_refresh_partial();
    } else {
        s_partial_frames = 0U;
        result = sticky_display_refresh_monochrome();
    }
    if (result != ESP_OK) {
        STICKY_LOGE(kTag, "flashcards=display result=%s",
                    esp_err_to_name(result));
    }
    sticky_touch_clear_press();
}

void render(bool partial)
{
    s_canvas->set_rotation(CanvasRotation::Deg90CounterClockwise);
    if (s_deck == nullptr) {
        FlashcardsDoneView view = {};
        flashcards_page_render_done(*s_canvas, view);
        refresh(false);
        return;
    }
    switch (s_page) {
    case FlashcardsPage::Home: {
        const FlashcardsDeckStats stats =
            flashcards_deck_stats(s_states, s_deck->card_count, s_today);
        const size_t allowed = new_allowed_today();
        FlashcardsHomeView view = {};
        view.deck_name = s_deck->name;
        view.deck_index = s_deck_index;
        view.deck_count = flashcards_deck_count();
        view.card_count = s_deck->card_count;
        view.due = stats.due;
        view.new_available = allowed < stats.unseen ? allowed : stats.unseen;
        view.learned = stats.learned;
        view.mature = stats.mature;
        view.new_per_day = new_per_day();
        view.reverse = s_header.reverse != 0U;
        view.storage_ok = s_storage_ok;
        flashcards_page_render_home(*s_canvas, view);
        break;
    }
    case FlashcardsPage::Review: {
        const uint16_t card = s_queue[s_position];
        const FlashcardState &state = s_states[card];
        char labels[4][8] = {};
        static const FlashcardGrade kGrades[4] = {
            FlashcardGrade::Again, FlashcardGrade::Hard,
            FlashcardGrade::Good, FlashcardGrade::Easy};
        FlashcardsReviewView view = {};
        for (int i = 0; i < 4; ++i) {
            flashcards_interval_label(
                flashcards_next_interval(state, kGrades[i]), labels[i],
                sizeof(labels[i]));
            view.interval_labels[i] = labels[i];
        }
        view.prompt = card_prompt(card);
        view.answer = card_answer(card);
        view.answer_shown = s_answer_shown;
        view.is_new = flashcards_is_new(state);
        view.position = s_position + 1U;
        view.total = s_queue_length;
        flashcards_page_render_review(*s_canvas, view);
        break;
    }
    case FlashcardsPage::Done: {
        const FlashcardsDeckStats tomorrow = flashcards_deck_stats(
            s_states, s_deck->card_count, static_cast<uint16_t>(s_today + 1U));
        FlashcardsDoneView view = {};
        view.reviewed = s_session_reviewed;
        view.again_count = s_session_again;
        view.due_tomorrow = tomorrow.due;
        flashcards_page_render_done(*s_canvas, view);
        break;
    }
    }
    refresh(partial);
}

void go_home(const char *reason)
{
    save_progress(reason);
    roll_day();
    s_page = FlashcardsPage::Home;
    s_answer_shown = false;
    render(false);
}

void start_session()
{
    roll_day();
    s_queue_length = flashcards_build_queue(
        s_states, s_deck->card_count, s_today, new_allowed_today(),
        kFlashcardsMaxReviewsPerSession, s_queue, kQueueCapacity);
    s_position = 0U;
    s_answer_shown = false;
    s_session_reviewed = 0U;
    s_session_again = 0U;
    STICKY_LOGI(kTag, "flashcards=session state=started cards=%u",
                static_cast<unsigned>(s_queue_length));
    if (s_queue_length == 0U) {
        render(true);  // stay on home; nothing to study
        return;
    }
    s_page = FlashcardsPage::Review;
    render(false);
}

void answer(FlashcardGrade grade)
{
    const uint16_t card = s_queue[s_position];
    FlashcardState &state = s_states[card];
    if (flashcards_is_new(state)) {
        ++s_header.new_today;
    }
    const bool again = flashcards_schedule(state, grade, s_today);
    s_dirty = true;
    ++s_unsaved_answers;
    ++s_session_reviewed;
    if (grade == FlashcardGrade::Again) {
        ++s_session_again;
    }
    if (again) {
        flashcards_requeue(s_queue, s_queue_length, kQueueCapacity,
                           s_position, card);
    }
    if (s_unsaved_answers >= kSaveEveryAnswers) {
        save_progress("periodic");
    }

    ++s_position;
    s_answer_shown = false;
    if (s_position >= s_queue_length) {
        save_progress("session_done");
        sticky_buzzer_play_pattern(StickyBuzzerPattern::SessionComplete);
        s_page = FlashcardsPage::Done;
        render(false);
        return;
    }
    render(true);
}

void switch_deck(int delta)
{
    const size_t count = flashcards_deck_count();
    if (count < 2U) {
        return;
    }
    save_progress("switch_deck");
    const size_t next =
        (s_deck_index + (delta < 0 ? count - 1U : 1U)) % count;
    load_deck(next);
    flashcards_storage_save_selected_deck(s_deck_index);
    render(true);
}

void handle_action(FlashcardsAction action)
{
    switch (action) {
    case FlashcardsAction::None:
        return;
    case FlashcardsAction::Study:
        start_session();
        return;
    case FlashcardsAction::PrevDeck:
        switch_deck(-1);
        return;
    case FlashcardsAction::NextDeck:
        switch_deck(1);
        return;
    case FlashcardsAction::CycleNewPerDay:
        s_header.new_per_day_index = static_cast<uint8_t>(
            (s_header.new_per_day_index + 1U) %
            kFlashcardsNewPerDayOptionCount);
        s_dirty = true;
        render(true);
        return;
    case FlashcardsAction::ToggleDirection:
        s_header.reverse = s_header.reverse != 0U ? 0U : 1U;
        s_dirty = true;
        render(true);
        return;
    case FlashcardsAction::ExtraNew:
        s_bonus_new += kExtraNewStep;
        render(true);
        return;
    case FlashcardsAction::ShowAnswer:
        s_answer_shown = true;
        render(true);
        return;
    case FlashcardsAction::GradeAgain:
        answer(FlashcardGrade::Again);
        return;
    case FlashcardsAction::GradeHard:
        answer(FlashcardGrade::Hard);
        return;
    case FlashcardsAction::GradeGood:
        answer(FlashcardGrade::Good);
        return;
    case FlashcardsAction::GradeEasy:
        answer(FlashcardGrade::Easy);
        return;
    case FlashcardsAction::Home:
        go_home("home_button");
        return;
    }
}

void app_task(void *)
{
    render(false);
    while (true) {
        if (sticky_app_lifecycle_checkpoint(s_lifecycle)) {
            sticky_touch_clear_press();
            if (s_page == FlashcardsPage::Home) {
                roll_day();
            }
            render(false);
        }

        StickyTouchPress press = {};
        if (s_deck != nullptr && sticky_touch_take_press(press)) {
            int x = 0;
            int y = 0;
            s_canvas->physical_to_logical(press.x, press.y, x, y);
            const FlashcardsAction action = flashcards_page_action_at(
                s_page, s_answer_shown, flashcards_deck_count() > 1U, x, y);
            if (action != FlashcardsAction::None) {
                STICKY_LOGI(kTag, "flashcards=touch action=%s x=%d y=%d",
                            flashcards_action_name(action), x, y);
            }
            handle_action(action);
        }
        vTaskDelay(kPollInterval);
    }
}

}  // namespace

esp_err_t flashcards_app_start(Canvas &canvas)
{
    app_log_register_tag(kTag);
    if (s_app_task != nullptr) {
        return ESP_OK;
    }
    s_canvas = &canvas;
    const esp_err_t result =
        load_deck(flashcards_storage_load_selected_deck());
    if (result != ESP_OK) {
        STICKY_LOGE(kTag, "flashcards=start deck=%s",
                    esp_err_to_name(result));
    }
    if (xTaskCreate(app_task, "flashcards_app", kTaskStackSize, nullptr,
                    kTaskPriority, &s_app_task) != pdPASS) {
        s_canvas = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t flashcards_app_pause()
{
    const esp_err_t result =
        sticky_app_lifecycle_pause(s_lifecycle, s_app_task);
    if (result == ESP_OK) {
        // Safe: the app task is parked at its checkpoint.
        save_progress("pause");
    }
    return result;
}

esp_err_t flashcards_app_resume()
{
    if (s_app_task == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    sticky_app_lifecycle_resume(s_lifecycle);
    return ESP_OK;
}

bool flashcards_app_power_sleep_allowed()
{
    return true;
}

esp_err_t flashcards_app_prepare_power_sleep()
{
    return flashcards_app_pause();
}

uint32_t flashcards_app_power_sleep_timeout_ms()
{
    return kIdleSleepTimeoutMs;
}
