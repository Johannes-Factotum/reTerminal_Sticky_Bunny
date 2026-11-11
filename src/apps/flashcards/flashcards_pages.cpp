#include "flashcards_pages.h"

#include <cstdio>

#include "canvas.h"
#include "text_utf8.h"
#include "ui_widgets.h"

namespace {

constexpr int kWidth = 480;
constexpr int kCenterX = kWidth / 2;

// Home page geometry.
constexpr UiRect kPrevDeckRect = {16, 128, 64, 64};
constexpr UiRect kNextDeckRect = {400, 128, 64, 64};
constexpr UiRect kNewPerDayRect = {24, 520, 210, 76};
constexpr UiRect kDirectionRect = {246, 520, 210, 76};
constexpr UiRect kStudyRect = {40, 618, 400, 72};
constexpr UiRect kExtraNewRect = {90, 710, 300, 56};

// Review page geometry.
constexpr UiRect kHomeRect = {16, 14, 120, 48};
constexpr UiRect kCardRect = {24, 96, 432, 512};
constexpr UiRect kShowAnswerRect = {40, 640, 400, 80};
constexpr UiRect kGradeRects[4] = {
    {16, 628, 104, 112},
    {130, 628, 104, 112},
    {246, 628, 104, 112},
    {360, 628, 104, 112},
};

// Done page geometry.
constexpr UiRect kDoneHomeRect = {40, 618, 400, 72};

void draw_stat(Canvas &canvas,
               int center_x,
               int y,
               const char *label,
               size_t value)
{
    char number[16];
    std::snprintf(number, sizeof(number), "%u",
                  static_cast<unsigned>(value));
    ui_draw_centered_text(canvas, center_x, y, number, 6);
    ui_draw_centered_text(canvas, center_x, y + 56, label, 2);
}

void draw_arrow(Canvas &canvas, const UiRect &rect, bool left)
{
    const int cx = rect.x + rect.width / 2;
    const int cy = rect.y + rect.height / 2;
    for (int i = 0; i < 14; ++i) {
        const int x = left ? cx - 7 + i : cx + 7 - i;
        canvas.fill_rect(x, cy - i, 2, i * 2 + 1, GrayLevel::Black);
    }
}

void draw_card_frame(Canvas &canvas, const UiRect &rect)
{
    canvas.fill_rect(rect.x + 6, rect.y + 6, rect.width, rect.height,
                     GrayLevel::Black);
    canvas.fill_rect(rect.x, rect.y, rect.width, rect.height,
                     GrayLevel::White);
    for (int i = 0; i < 3; ++i) {
        canvas.draw_rect(rect.x + i, rect.y + i,
                         rect.width - i * 2, rect.height - i * 2,
                         GrayLevel::Black);
    }
}

}  // namespace

void flashcards_page_render_home(Canvas &canvas,
                                 const FlashcardsHomeView &view)
{
    canvas.clear(GrayLevel::White);
    ui_draw_centered_text(canvas, kCenterX, 52, "FLASHCARDS", 5);
    ui_draw_divider(canvas, kCenterX, 160, 108);

    const UiRect name_rect = {88, 128, 304, 64};
    ui_draw_fitted_text(canvas, name_rect, view.deck_name, 3, 2);
    if (view.deck_count > 1U) {
        draw_arrow(canvas, kPrevDeckRect, true);
        draw_arrow(canvas, kNextDeckRect, false);
    }

    draw_stat(canvas, 130, 236, "DUE", view.due);
    draw_stat(canvas, 350, 236, "NEW TODAY", view.new_available);

    char line[48];
    std::snprintf(line, sizeof(line), "LEARNED %u / %u",
                  static_cast<unsigned>(view.learned),
                  static_cast<unsigned>(view.card_count));
    ui_draw_centered_text(canvas, kCenterX, 360, line, 2);

    // Progress bar of learned cards.
    const UiRect bar = {60, 390, 360, 22};
    canvas.draw_rect(bar.x, bar.y, bar.width, bar.height, GrayLevel::Black);
    canvas.draw_rect(bar.x + 1, bar.y + 1, bar.width - 2, bar.height - 2,
                     GrayLevel::Black);
    if (view.card_count > 0U) {
        const int learned_w = static_cast<int>(
            (bar.width - 6) * view.learned / view.card_count);
        const int mature_w = static_cast<int>(
            (bar.width - 6) * view.mature / view.card_count);
        // Checkerboard instead of gray so partial refreshes stay crisp.
        for (int y = bar.y + 3; y < bar.y + bar.height - 3; ++y) {
            for (int x = bar.x + 3 + (y & 1); x < bar.x + 3 + learned_w;
                 x += 2) {
                canvas.draw_pixel(x, y, GrayLevel::Black);
            }
        }
        canvas.fill_rect(bar.x + 3, bar.y + 3, mature_w, bar.height - 6,
                         GrayLevel::Black);
    }
    std::snprintf(line, sizeof(line), "MATURE (21D+) %u",
                  static_cast<unsigned>(view.mature));
    ui_draw_centered_text(canvas, kCenterX, 426, line, 2);
    if (!view.storage_ok) {
        ui_draw_centered_text(canvas, kCenterX, 470,
                              "PROGRESS NOT SAVED!", 2);
    }

    char per_day[16];
    std::snprintf(per_day, sizeof(per_day), "%u / DAY",
                  static_cast<unsigned>(view.new_per_day));
    ui_draw_two_line_button(canvas, kNewPerDayRect, "NEW", per_day, false);
    ui_draw_two_line_button(canvas,
                            kDirectionRect,
                            "SHOW",
                            view.reverse ? "BACK FIRST" : "FRONT FIRST",
                            false);

    const bool can_study = view.due + view.new_available > 0U;
    ui_draw_button(canvas,
                   kStudyRect,
                   can_study ? "STUDY NOW" : "ALL DONE TODAY",
                   can_study,
                   3);
    ui_draw_button(canvas, kExtraNewRect, "+10 NEW CARDS", false, 2);
}

void flashcards_page_render_review(Canvas &canvas,
                                   const FlashcardsReviewView &view)
{
    canvas.clear(GrayLevel::White);
    ui_draw_button(canvas, kHomeRect, "< HOME", false, 2);

    char progress[24];
    std::snprintf(progress, sizeof(progress), "%u/%u",
                  static_cast<unsigned>(view.position),
                  static_cast<unsigned>(view.total));
    utf8_draw_text(canvas, 156, 30, progress, 2);
    if (view.is_new) {
        const UiRect tag = {262, 22, 64, 32};
        canvas.fill_rect(tag.x, tag.y, tag.width, tag.height,
                         GrayLevel::Black);
        ui_draw_centered_text(canvas, tag.x + tag.width / 2, tag.y + 9,
                              "NEW", 2, GrayLevel::White);
    }

    draw_card_frame(canvas, kCardRect);
    if (!view.answer_shown) {
        const UiRect prompt_rect = {kCardRect.x + 20, kCardRect.y + 20,
                                    kCardRect.width - 40,
                                    kCardRect.height - 40};
        ui_draw_fitted_text(canvas, prompt_rect, view.prompt, 6, 2);
        ui_draw_button(canvas, kShowAnswerRect, "SHOW ANSWER", true, 3);
        return;
    }

    const UiRect prompt_rect = {kCardRect.x + 20, kCardRect.y + 16,
                                kCardRect.width - 40, 150};
    ui_draw_fitted_text(canvas, prompt_rect, view.prompt, 5, 2);
    ui_draw_divider(canvas, kCenterX, 180, kCardRect.y + 184);
    const UiRect answer_rect = {kCardRect.x + 20, kCardRect.y + 208,
                                kCardRect.width - 40,
                                kCardRect.height - 226};
    ui_draw_fitted_text(canvas, answer_rect, view.answer, 5, 2);

    static const char *const kGradeNames[4] = {"AGAIN", "HARD", "GOOD",
                                               "EASY"};
    for (int i = 0; i < 4; ++i) {
        const UiRect &rect = kGradeRects[i];
        const bool filled = i == 2;
        if (filled) {
            canvas.fill_rect(rect.x, rect.y, rect.width, rect.height,
                             GrayLevel::Black);
        } else {
            canvas.fill_rect(rect.x, rect.y, rect.width, rect.height,
                             GrayLevel::White);
            for (int t = 0; t < 3; ++t) {
                canvas.draw_rect(rect.x + t, rect.y + t,
                                 rect.width - t * 2, rect.height - t * 2,
                                 GrayLevel::Black);
            }
        }
        const GrayLevel ink = filled ? GrayLevel::White : GrayLevel::Black;
        ui_draw_centered_text(canvas, rect.x + rect.width / 2, rect.y + 30,
                              kGradeNames[i], 2, ink);
        const char *label = view.interval_labels[i] != nullptr
                                ? view.interval_labels[i]
                                : "";
        ui_draw_centered_text(canvas, rect.x + rect.width / 2, rect.y + 64,
                              label, 3, ink);
    }
}

void flashcards_page_render_done(Canvas &canvas,
                                 const FlashcardsDoneView &view)
{
    canvas.clear(GrayLevel::White);
    ui_draw_centered_text(canvas, kCenterX, 120, "WELL DONE!", 5);
    ui_draw_divider(canvas, kCenterX, 160, 180);
    ui_draw_centered_text(canvas, kCenterX, 220, "SESSION COMPLETE", 3);

    draw_stat(canvas, 130, 300, "REVIEWS", view.reviewed);
    draw_stat(canvas, 350, 300, "AGAIN", view.again_count);

    char line[40];
    std::snprintf(line, sizeof(line), "DUE TOMORROW: %u",
                  static_cast<unsigned>(view.due_tomorrow));
    ui_draw_centered_text(canvas, kCenterX, 460, line, 2);
    ui_draw_centered_text(canvas, kCenterX, 500,
                          "SEE YOU TOMORROW / HASTA MA\xC3\x91" "ANA", 2);
    ui_draw_button(canvas, kDoneHomeRect, "BACK TO DECK", true, 3);
}

FlashcardsAction flashcards_page_action_at(FlashcardsPage page,
                                           bool answer_shown,
                                           bool multiple_decks,
                                           int x,
                                           int y)
{
    switch (page) {
    case FlashcardsPage::Home:
        if (multiple_decks && kPrevDeckRect.contains(x, y)) {
            return FlashcardsAction::PrevDeck;
        }
        if (multiple_decks && kNextDeckRect.contains(x, y)) {
            return FlashcardsAction::NextDeck;
        }
        if (kNewPerDayRect.contains(x, y)) {
            return FlashcardsAction::CycleNewPerDay;
        }
        if (kDirectionRect.contains(x, y)) {
            return FlashcardsAction::ToggleDirection;
        }
        if (kStudyRect.contains(x, y)) {
            return FlashcardsAction::Study;
        }
        if (kExtraNewRect.contains(x, y)) {
            return FlashcardsAction::ExtraNew;
        }
        return FlashcardsAction::None;
    case FlashcardsPage::Review:
        if (kHomeRect.contains(x, y)) {
            return FlashcardsAction::Home;
        }
        if (!answer_shown) {
            if (kShowAnswerRect.contains(x, y) || kCardRect.contains(x, y)) {
                return FlashcardsAction::ShowAnswer;
            }
            return FlashcardsAction::None;
        }
        if (kGradeRects[0].contains(x, y)) {
            return FlashcardsAction::GradeAgain;
        }
        if (kGradeRects[1].contains(x, y)) {
            return FlashcardsAction::GradeHard;
        }
        if (kGradeRects[2].contains(x, y)) {
            return FlashcardsAction::GradeGood;
        }
        if (kGradeRects[3].contains(x, y)) {
            return FlashcardsAction::GradeEasy;
        }
        return FlashcardsAction::None;
    case FlashcardsPage::Done:
        return kDoneHomeRect.contains(x, y) ? FlashcardsAction::Home
                                            : FlashcardsAction::None;
    }
    return FlashcardsAction::None;
}

const char *flashcards_action_name(FlashcardsAction action)
{
    switch (action) {
    case FlashcardsAction::None:
        return "none";
    case FlashcardsAction::Study:
        return "study";
    case FlashcardsAction::PrevDeck:
        return "prev_deck";
    case FlashcardsAction::NextDeck:
        return "next_deck";
    case FlashcardsAction::CycleNewPerDay:
        return "cycle_new_per_day";
    case FlashcardsAction::ToggleDirection:
        return "toggle_direction";
    case FlashcardsAction::ExtraNew:
        return "extra_new";
    case FlashcardsAction::ShowAnswer:
        return "show_answer";
    case FlashcardsAction::GradeAgain:
        return "grade_again";
    case FlashcardsAction::GradeHard:
        return "grade_hard";
    case FlashcardsAction::GradeGood:
        return "grade_good";
    case FlashcardsAction::GradeEasy:
        return "grade_easy";
    case FlashcardsAction::Home:
        return "home";
    }
    return "unknown";
}
