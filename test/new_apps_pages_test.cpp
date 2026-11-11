// Renders every Cards and To-Do page and checks that hit areas match what is
// drawn. PPM previews go to $TMPDIR (default /tmp).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "canvas.h"
#include "flashcards_pages.h"
#include "todo_list.h"
#include "todo_pages.h"

namespace {

constexpr uint16_t kWidth = 800U;
constexpr uint16_t kHeight = 480U;

std::string preview_path(const char *name)
{
    const char *dir = std::getenv("TMPDIR");
    return std::string(dir != nullptr ? dir : "/tmp") + "/" + name;
}

void write_preview(const Canvas &canvas, const char *name)
{
    // Logical portrait image, 480 x 800.
    std::ofstream out(preview_path(name), std::ios::binary);
    out << "P5\n" << canvas.width() << " " << canvas.height() << "\n255\n";
    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            const uint8_t value =
                static_cast<uint8_t>(static_cast<int>(canvas.pixel_at(x, y)) *
                                     85);
            out.write(reinterpret_cast<const char *>(&value), 1);
        }
    }
}

size_t black_pixels(const Canvas &canvas, int x0, int y0, int x1, int y1)
{
    size_t count = 0U;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (canvas.pixel_at(x, y) == GrayLevel::Black) {
                ++count;
            }
        }
    }
    return count;
}

// Nothing may be drawn under the global battery overlay.
void assert_overlay_area_clear(const Canvas &canvas)
{
    assert(black_pixels(canvas, canvas.width() - 104, 4,
                        canvas.width() - 6, 32) == 0U);
}

}  // namespace

int main()
{
    std::vector<uint8_t> buffer(kWidth / 4U * kHeight, 0xFF);
    Canvas canvas(kWidth, kHeight, buffer.data(), buffer.size());
    canvas.set_rotation(CanvasRotation::Deg90CounterClockwise);
    assert(canvas.width() == 480 && canvas.height() == 800);

    // ---- Flashcards ----
    FlashcardsHomeView home = {};
    home.deck_name = "Spanish Top 5000";
    home.deck_index = 0;
    home.deck_count = 1;
    home.card_count = 5000;
    home.due = 37;
    home.new_available = 20;
    home.learned = 812;
    home.mature = 344;
    home.new_per_day = 20;
    home.reverse = false;
    home.storage_ok = true;
    flashcards_page_render_home(canvas, home);
    assert_overlay_area_clear(canvas);
    write_preview(canvas, "flashcards_home.pgm");
    assert(flashcards_page_action_at(FlashcardsPage::Home, false, false, 240,
                                     650) == FlashcardsAction::Study);
    assert(flashcards_page_action_at(FlashcardsPage::Home, false, false, 120,
                                     550) ==
           FlashcardsAction::CycleNewPerDay);
    assert(flashcards_page_action_at(FlashcardsPage::Home, false, false, 350,
                                     550) ==
           FlashcardsAction::ToggleDirection);
    assert(flashcards_page_action_at(FlashcardsPage::Home, false, false, 240,
                                     735) == FlashcardsAction::ExtraNew);
    assert(flashcards_page_action_at(FlashcardsPage::Home, false, false, 40,
                                     160) == FlashcardsAction::None);
    assert(flashcards_page_action_at(FlashcardsPage::Home, false, true, 40,
                                     160) == FlashcardsAction::PrevDeck);

    FlashcardsReviewView review = {};
    review.prompt = "la ma\xC3\xB1" "ana";
    review.answer = "(n.) morning; tomorrow";
    review.answer_shown = false;
    review.is_new = true;
    review.position = 3;
    review.total = 57;
    review.interval_labels[0] = "<10m";
    review.interval_labels[1] = "<10m";
    review.interval_labels[2] = "1d";
    review.interval_labels[3] = "4d";
    flashcards_page_render_review(canvas, review);
    assert_overlay_area_clear(canvas);
    // Big prompt text inside the card.
    assert(black_pixels(canvas, 40, 300, 440, 420) > 1500U);
    write_preview(canvas, "flashcards_question.pgm");
    assert(flashcards_page_action_at(FlashcardsPage::Review, false, false,
                                     240, 680) ==
           FlashcardsAction::ShowAnswer);
    assert(flashcards_page_action_at(FlashcardsPage::Review, false, false,
                                     240, 300) ==
           FlashcardsAction::ShowAnswer);
    assert(flashcards_page_action_at(FlashcardsPage::Review, false, false, 60,
                                     36) == FlashcardsAction::Home);

    review.answer_shown = true;
    review.prompt = "\xC2\xBF" "D\xC3\xB3nde est\xC3\xA1 el ba\xC3\xB1o?";
    review.answer =
        "(phrase) Where is the bathroom? A very long answer that needs to "
        "wrap across several lines to test the fitting logic properly.";
    flashcards_page_render_review(canvas, review);
    assert_overlay_area_clear(canvas);
    write_preview(canvas, "flashcards_answer.pgm");
    const FlashcardsAction grades[4] = {
        FlashcardsAction::GradeAgain, FlashcardsAction::GradeHard,
        FlashcardsAction::GradeGood, FlashcardsAction::GradeEasy};
    for (int i = 0; i < 4; ++i) {
        const int x = 68 + i * 115;
        assert(flashcards_page_action_at(FlashcardsPage::Review, true, false,
                                         x, 690) == grades[i]);
        // Every grade button shows ink (label and interval).
        assert(black_pixels(canvas, x - 40, 640, x + 40, 730) > 200U);
    }
    // The card body no longer reveals on tap once the answer is visible.
    assert(flashcards_page_action_at(FlashcardsPage::Review, true, false, 240,
                                     300) == FlashcardsAction::None);

    FlashcardsDoneView done = {};
    done.reviewed = 64;
    done.again_count = 7;
    done.due_tomorrow = 41;
    flashcards_page_render_done(canvas, done);
    assert_overlay_area_clear(canvas);
    write_preview(canvas, "flashcards_done.pgm");
    assert(flashcards_page_action_at(FlashcardsPage::Done, false, false, 240,
                                     650) == FlashcardsAction::Home);

    // ---- To-Do ----
    TodoList list = {};
    todo_list_add(list, "Buy groceries");
    todo_list_add(list, "Call the dentist about the appointment next week");
    todo_list_add(list, "Practice espa\xC3\xB1ol for 20 minutes");
    todo_list_add(list, "Water plants");
    todo_list_toggle(list, 3);
    for (int i = 0; i < 6; ++i) {
        char text[32];
        std::snprintf(text, sizeof(text), "Extra task %d", i + 1);
        todo_list_add(list, text);
    }
    assert(todo_page_count(list) == 2U);
    todo_page_render_list(canvas, {&list, 0});
    assert_overlay_area_clear(canvas);
    write_preview(canvas, "todo_list.pgm");
    TodoHit hit = todo_page_list_hit(list, 0, 40, 128 + 33);
    assert(hit.action == TodoAction::ToggleItem && hit.index == 0U);
    hit = todo_page_list_hit(list, 0, 200, 128 + 66 * 2 + 10);
    assert(hit.action == TodoAction::ToggleItem && hit.index == 2U);
    hit = todo_page_list_hit(list, 0, 440, 128 + 66 + 30);
    assert(hit.action == TodoAction::EditItem && hit.index == 1U);
    hit = todo_page_list_hit(list, 1, 200, 128 + 66 + 30);
    assert(hit.action == TodoAction::ToggleItem && hit.index == 8U);
    hit = todo_page_list_hit(list, 1, 200, 128 + 66 * 4);
    assert(hit.action == TodoAction::None);
    assert(todo_page_list_hit(list, 0, 350, 710).action == TodoAction::Add);
    assert(todo_page_list_hit(list, 0, 100, 710).action ==
           TodoAction::ClearDone);
    assert(todo_page_list_hit(list, 0, 420, 630).action ==
           TodoAction::NextPage);

    TodoList empty = {};
    todo_page_render_list(canvas, {&empty, 0});
    write_preview(canvas, "todo_empty.pgm");
    assert(todo_page_list_hit(empty, 0, 420, 630).action == TodoAction::None);

    TodoEditorView editor = {"Comprar pan y caf\xC3\xA9", false,
                             TodoKeyboardMode::Letters, false};
    todo_page_render_editor(canvas, editor);
    assert_overlay_area_clear(canvas);
    write_preview(canvas, "todo_editor.pgm");
    hit = todo_page_editor_hit(false, TodoKeyboardMode::Letters, 28, 390);
    assert(hit.action == TodoAction::Key && hit.index == 0U);  // q
    char key[8];
    assert(std::strcmp(todo_key_text(TodoKeyboardMode::Letters, hit.index,
                                     true, key, sizeof(key)),
                       "Q") == 0);
    hit = todo_page_editor_hit(false, TodoKeyboardMode::Letters, 470, 390);
    assert(hit.action == TodoAction::Key && hit.index == 9U);  // p
    assert(todo_page_editor_hit(false, TodoKeyboardMode::Letters, 420, 540)
               .action == TodoAction::Backspace);
    assert(todo_page_editor_hit(false, TodoKeyboardMode::Letters, 300, 610)
               .action == TodoAction::Space);
    assert(todo_page_editor_hit(false, TodoKeyboardMode::Letters, 50, 610)
               .action == TodoAction::Mode);
    assert(todo_page_editor_hit(false, TodoKeyboardMode::Letters, 140, 610)
               .action == TodoAction::Shift);
    assert(todo_page_editor_hit(false, TodoKeyboardMode::Letters, 400, 710)
               .action == TodoAction::Save);
    assert(todo_page_editor_hit(false, TodoKeyboardMode::Letters, 80, 710)
               .action == TodoAction::Cancel);
    assert(todo_page_editor_hit(false, TodoKeyboardMode::Letters, 240, 710)
               .action == TodoAction::None);
    assert(todo_page_editor_hit(true, TodoKeyboardMode::Letters, 240, 710)
               .action == TodoAction::Delete);
    // Second row has nine keys; its right edge is empty in letter mode.
    hit = todo_page_editor_hit(false, TodoKeyboardMode::Symbols, 50, 466);
    assert(hit.action == TodoAction::Key && hit.index == 10U);
    assert(std::strcmp(todo_key_text(TodoKeyboardMode::Symbols, 10, false,
                                     key, sizeof(key)),
                       "\xC3\xA1") == 0);
    assert(std::strcmp(todo_key_text(TodoKeyboardMode::Symbols, 15, true,
                                     key, sizeof(key)),
                       "\xC3\x91") == 0);  // Ñ

    editor = {"", true, TodoKeyboardMode::Symbols, true};
    todo_page_render_editor(canvas, editor);
    write_preview(canvas, "todo_editor_symbols.pgm");
    return 0;
}
