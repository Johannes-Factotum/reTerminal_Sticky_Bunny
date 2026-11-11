#include <cassert>
#include <cstring>
#include <set>
#include <vector>

#include "flashcards_deck.h"
#include "flashcards_srs.h"

int main()
{
    // Calendar conversion.
    assert(flashcards_days_from_civil(1970, 1, 1) == 0U);
    assert(flashcards_days_from_civil(2000, 3, 1) == 11017U);
    assert(flashcards_days_from_civil(2026, 11, 11) ==
           flashcards_days_from_civil(2026, 11, 10) + 1U);
    assert(flashcards_days_from_civil(1969, 1, 1) == 0U);

    const uint16_t today = flashcards_days_from_civil(2026, 11, 11);

    // New card answered Good graduates to one day.
    FlashcardState card = {};
    assert(flashcards_is_new(card));
    assert(!flashcards_schedule(card, FlashcardGrade::Good, today));
    assert(card.interval_days == 1U);
    assert(card.due_day == today + 1U);
    assert(!flashcards_is_new(card));
    assert(!flashcards_is_due(card, today));
    assert(flashcards_is_due(card, today + 1U));

    // Growing intervals with Good; Easy grows faster than Good.
    FlashcardState good = card;
    FlashcardState easy = card;
    flashcards_schedule(good, FlashcardGrade::Good, today + 1U);
    flashcards_schedule(easy, FlashcardGrade::Easy, today + 1U);
    assert(good.interval_days > 1U);
    assert(easy.interval_days > good.interval_days);
    assert(easy.ease_permille > good.ease_permille);

    // Hard grows slower than Good and lowers ease.
    FlashcardState hard = good;
    FlashcardState good2 = good;
    flashcards_schedule(hard, FlashcardGrade::Hard, good.due_day);
    flashcards_schedule(good2, FlashcardGrade::Good, good.due_day);
    assert(hard.interval_days < good2.interval_days);
    assert(hard.interval_days > good.interval_days);
    assert(hard.ease_permille < good2.ease_permille);

    // Lapse: Again resets to learning, counts a lapse, asks for requeue.
    FlashcardState lapsed = good2;
    assert(flashcards_schedule(lapsed, FlashcardGrade::Again, today + 20U));
    assert(lapsed.interval_days == 0U);
    assert(lapsed.lapses == 1U);
    assert(lapsed.reps == 0U);
    assert(flashcards_is_due(lapsed, today + 20U));
    // Relearning Good returns to one day.
    assert(!flashcards_schedule(lapsed, FlashcardGrade::Good, today + 20U));
    assert(lapsed.interval_days == 1U);

    // New card Again stays in the session and is no longer "new".
    FlashcardState fresh = {};
    assert(flashcards_schedule(fresh, FlashcardGrade::Again, today));
    assert(!flashcards_is_new(fresh));
    assert(flashcards_is_due(fresh, today));

    // Ease never drops below the floor.
    FlashcardState floor_card = good2;
    for (int i = 0; i < 20; ++i) {
        flashcards_schedule(floor_card, FlashcardGrade::Again, today);
        flashcards_schedule(floor_card, FlashcardGrade::Good, today);
        flashcards_schedule(floor_card, FlashcardGrade::Hard, today + 1U);
    }
    assert(floor_card.ease_permille >= kFlashcardsMinEase);

    // Interval cap.
    FlashcardState big = {};
    big.due_day = today;
    big.interval_days = 3000U;
    big.ease_permille = 3000U;
    flashcards_schedule(big, FlashcardGrade::Easy, today);
    assert(big.interval_days == kFlashcardsMaxInterval);

    // Labels.
    char label[8];
    flashcards_interval_label(0U, label, sizeof(label));
    assert(std::strcmp(label, "<10m") == 0);
    flashcards_interval_label(4U, label, sizeof(label));
    assert(std::strcmp(label, "4d") == 0);
    flashcards_interval_label(60U, label, sizeof(label));
    assert(std::strcmp(label, "2mo") == 0);
    flashcards_interval_label(548U, label, sizeof(label));
    assert(std::strcmp(label, "1.5y") == 0);

    // Queue: due reviews plus limited new cards, all distinct.
    std::vector<FlashcardState> states(100);
    for (size_t i = 0; i < 30; ++i) {
        states[i].due_day = static_cast<uint16_t>(today - (i % 3));
        states[i].interval_days = 3U;
        states[i].reps = 1U;
    }
    for (size_t i = 30; i < 40; ++i) {
        states[i].due_day = static_cast<uint16_t>(today + 5U);  // not due
        states[i].interval_days = 5U;
    }
    uint16_t queue[256];
    size_t length = flashcards_build_queue(states.data(), states.size(),
                                           today, 20U, 500U, queue, 256U);
    assert(length == 50U);
    std::set<uint16_t> unique(queue, queue + length);
    assert(unique.size() == length);
    size_t new_seen = 0U;
    uint16_t last_new = 0U;
    for (size_t i = 0; i < length; ++i) {
        assert(queue[i] < 30U || queue[i] >= 40U);
        if (queue[i] >= 40U) {
            // New cards appear in deck order.
            assert(new_seen == 0U || queue[i] > last_new);
            last_new = queue[i];
            ++new_seen;
        }
    }
    assert(new_seen == 20U);
    // New cards are spread: neither all at the front nor all at the back.
    assert(queue[0] < 30U);
    assert(queue[length - 1U] < 30U);

    // Review limit and capacity are respected.
    length = flashcards_build_queue(states.data(), states.size(), today, 5U,
                                    10U, queue, 256U);
    assert(length == 15U);
    length = flashcards_build_queue(states.data(), states.size(), today, 50U,
                                    500U, queue, 12U);
    assert(length == 12U);

    // Only new cards.
    std::vector<FlashcardState> blank(10);
    length = flashcards_build_queue(blank.data(), blank.size(), today, 4U,
                                    500U, queue, 256U);
    assert(length == 4U);
    assert(queue[0] == 0U && queue[3] == 3U);

    // Requeue inserts a few cards later, or at the end.
    uint16_t small_queue[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    size_t small_length = 10U;
    assert(flashcards_requeue(small_queue, small_length, 16U, 0U, 1U));
    assert(small_length == 11U);
    assert(small_queue[1U + kFlashcardsRequeueGap] == 1U);
    assert(flashcards_requeue(small_queue, small_length, 16U, 10U, 99U));
    assert(small_queue[small_length - 1U] == 99U);

    // Header seal/validate and corruption detection.
    FlashcardsProgressHeader header = {};
    header.card_count = static_cast<uint16_t>(states.size());
    header.deck_hash = flashcards_deck_hash("Deck", states.size());
    header.sequence = 7U;
    flashcards_header_seal(header, states.data());
    assert(flashcards_header_valid(header, header.deck_hash, states.size()));
    assert(flashcards_payload_valid(header, states.data()));
    assert(!flashcards_header_valid(header, header.deck_hash + 1U,
                                    states.size()));
    states[5].interval_days ^= 1U;
    assert(!flashcards_payload_valid(header, states.data()));
    header.sequence = 8U;
    assert(!flashcards_header_valid(header,
                                    flashcards_deck_hash("Deck",
                                                         states.size()),
                                    states.size()));
    assert(flashcards_deck_hash("A", 10) != flashcards_deck_hash("A", 11));
    assert(sizeof(FlashcardsProgressHeader) == 32U);

    // Built-in deck data is well formed.
    assert(flashcards_deck_count() >= 1U);
    for (size_t d = 0; d < flashcards_deck_count(); ++d) {
        const FlashcardsDeck &deck = flashcards_deck(d);
        assert(deck.card_count > 0U);
        assert(std::strlen(deck.name) > 0U);
        for (size_t i = 0; i < deck.card_count; ++i) {
            assert(std::strlen(flashcards_deck_front(deck, i)) > 0U);
            assert(std::strlen(flashcards_deck_back(deck, i)) > 0U);
        }
    }
    const FlashcardsDeck &spanish = flashcards_deck(0);
    assert(spanish.card_count == 5000U);
    assert(std::strcmp(flashcards_deck_front(spanish, 0), "de") == 0);
    return 0;
}
