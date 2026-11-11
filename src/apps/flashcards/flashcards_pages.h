#pragma once

#include <cstddef>
#include <cstdint>

class Canvas;

// Portrait (480 x 800 logical) pages for the flashcard app.

enum class FlashcardsAction : uint8_t {
    None,
    Study,
    PrevDeck,
    NextDeck,
    CycleNewPerDay,
    ToggleDirection,
    ExtraNew,
    ShowAnswer,
    GradeAgain,
    GradeHard,
    GradeGood,
    GradeEasy,
    Home,
};

struct FlashcardsHomeView {
    const char *deck_name;
    size_t deck_index;
    size_t deck_count;
    size_t card_count;
    size_t due;
    size_t new_available;  // new cards still allowed today
    size_t learned;
    size_t mature;
    uint16_t new_per_day;
    bool reverse;
    bool storage_ok;
};

struct FlashcardsReviewView {
    const char *prompt;      // side shown first
    const char *answer;      // side revealed
    bool answer_shown;
    bool is_new;
    size_t position;         // 1-based
    size_t total;
    const char *interval_labels[4];  // Again, Hard, Good, Easy
};

struct FlashcardsDoneView {
    size_t reviewed;
    size_t again_count;
    size_t due_tomorrow;
};

void flashcards_page_render_home(Canvas &canvas,
                                 const FlashcardsHomeView &view);
void flashcards_page_render_review(Canvas &canvas,
                                   const FlashcardsReviewView &view);
void flashcards_page_render_done(Canvas &canvas,
                                 const FlashcardsDoneView &view);

enum class FlashcardsPage : uint8_t {
    Home,
    Review,
    Done,
};

FlashcardsAction flashcards_page_action_at(FlashcardsPage page,
                                           bool answer_shown,
                                           bool multiple_decks,
                                           int x,
                                           int y);

const char *flashcards_action_name(FlashcardsAction action);
