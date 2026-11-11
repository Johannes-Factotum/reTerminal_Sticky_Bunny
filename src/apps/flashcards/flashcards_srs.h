#pragma once

#include <cstddef>
#include <cstdint>

// Hardware-independent spaced-repetition core (SM-2 / Anki style).
// All days are counted as days since 1970-01-01 (fits uint16 until 2149).

struct FlashcardState {
    uint16_t due_day = 0U;        // 0 = never shown (new card)
    uint16_t interval_days = 0U;  // 0 = learning (re-show today)
    uint16_t ease_permille = 0U;  // 2500 = 250 %
    uint8_t reps = 0U;            // successful reviews in a row
    uint8_t lapses = 0U;
};
static_assert(sizeof(FlashcardState) == 8, "card state must stay 8 bytes");

enum class FlashcardGrade : uint8_t {
    Again,
    Hard,
    Good,
    Easy,
};

constexpr uint16_t kFlashcardsDefaultEase = 2500U;
constexpr uint16_t kFlashcardsMinEase = 1300U;
constexpr uint16_t kFlashcardsMaxEase = 3500U;
constexpr uint16_t kFlashcardsMaxInterval = 3650U;
constexpr size_t kFlashcardsRequeueGap = 5U;

bool flashcards_is_new(const FlashcardState &state);

// True for a card that has been seen and is due on or before `today`.
bool flashcards_is_due(const FlashcardState &state, uint16_t today);

// Interval in days the grade would produce; 0 means "again today".
uint16_t flashcards_next_interval(const FlashcardState &state,
                                  FlashcardGrade grade);

// Applies one answer. Returns true when the card must be shown again in the
// current session (Again, or a still-learning card).
bool flashcards_schedule(FlashcardState &state,
                         FlashcardGrade grade,
                         uint16_t today);

// Short label such as "<1m", "1d", "12d", "3mo", "1.5y".
void flashcards_interval_label(uint16_t days, char *out, size_t out_size);

// Proleptic Gregorian date to days since 1970-01-01 (0 if out of range).
uint16_t flashcards_days_from_civil(int year, unsigned month, unsigned day);

struct FlashcardsDeckStats {
    size_t due = 0U;      // seen cards due today
    size_t unseen = 0U;   // never shown
    size_t learned = 0U;  // seen at least once
    size_t mature = 0U;   // interval >= 21 days
};

FlashcardsDeckStats flashcards_deck_stats(const FlashcardState *states,
                                          size_t count,
                                          uint16_t today);

// Builds today's study queue: due cards (oldest index first) interleaved with
// up to `new_allowed` unseen cards in deck order. Returns queue length.
size_t flashcards_build_queue(const FlashcardState *states,
                              size_t count,
                              uint16_t today,
                              size_t new_allowed,
                              size_t max_reviews,
                              uint16_t *queue,
                              size_t capacity);

// Re-inserts `card` a few positions after `position` (or at the end).
bool flashcards_requeue(uint16_t *queue,
                        size_t &length,
                        size_t capacity,
                        size_t position,
                        uint16_t card);

uint32_t flashcards_crc32(const void *data, size_t size, uint32_t seed = 0U);

// Stable deck identity used to reject progress saved for another deck.
uint32_t flashcards_deck_hash(const char *name, size_t card_count);

// Persistent record header written in front of the card-state array.
struct FlashcardsProgressHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t card_count;
    uint32_t sequence;
    uint32_t deck_hash;
    uint16_t new_day;          // day `new_today` belongs to
    uint16_t new_today;        // new cards introduced on new_day
    uint8_t new_per_day_index;
    uint8_t reverse;           // 1 = show back side first
    uint16_t reserved;
    uint32_t payload_crc;
    uint32_t header_crc;       // CRC of all preceding header bytes
};

constexpr uint32_t kFlashcardsProgressMagic = 0x46435244U;  // "FCRD"
constexpr uint16_t kFlashcardsProgressVersion = 1U;

void flashcards_header_seal(FlashcardsProgressHeader &header,
                            const FlashcardState *states);

bool flashcards_header_valid(const FlashcardsProgressHeader &header,
                             uint32_t deck_hash,
                             size_t card_count);

bool flashcards_payload_valid(const FlashcardsProgressHeader &header,
                              const FlashcardState *states);

// Options cycled from the home page.
constexpr uint16_t kFlashcardsNewPerDayOptions[] = {5U, 10U, 20U, 30U, 50U};
constexpr size_t kFlashcardsNewPerDayOptionCount =
    sizeof(kFlashcardsNewPerDayOptions) /
    sizeof(kFlashcardsNewPerDayOptions[0]);
constexpr uint8_t kFlashcardsDefaultNewPerDayIndex = 2U;
constexpr size_t kFlashcardsMaxReviewsPerSession = 500U;
