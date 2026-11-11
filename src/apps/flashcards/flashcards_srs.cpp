#include "flashcards_srs.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {

uint16_t clamp_ease(int ease)
{
    if (ease < kFlashcardsMinEase) {
        return kFlashcardsMinEase;
    }
    if (ease > kFlashcardsMaxEase) {
        return kFlashcardsMaxEase;
    }
    return static_cast<uint16_t>(ease);
}

uint16_t clamp_interval(uint32_t days)
{
    if (days < 1U) {
        return 1U;
    }
    return days > kFlashcardsMaxInterval
               ? kFlashcardsMaxInterval
               : static_cast<uint16_t>(days);
}

uint16_t effective_ease(const FlashcardState &state)
{
    return state.ease_permille == 0U ? kFlashcardsDefaultEase
                                     : state.ease_permille;
}

}  // namespace

bool flashcards_is_new(const FlashcardState &state)
{
    return state.due_day == 0U;
}

bool flashcards_is_due(const FlashcardState &state, uint16_t today)
{
    return !flashcards_is_new(state) && state.due_day <= today;
}

uint16_t flashcards_next_interval(const FlashcardState &state,
                                  FlashcardGrade grade)
{
    const bool learning = state.interval_days == 0U;
    if (grade == FlashcardGrade::Again) {
        return 0U;
    }
    if (learning) {
        // New or relearning card: graduate with fixed starting intervals.
        switch (grade) {
        case FlashcardGrade::Hard:
            return state.lapses > 0U ? 1U : 0U;
        case FlashcardGrade::Good:
            return 1U;
        case FlashcardGrade::Easy:
            return 4U;
        default:
            return 0U;
        }
    }

    const uint32_t interval = state.interval_days;
    const uint32_t ease = effective_ease(state);
    uint32_t good = (interval * ease + 500U) / 1000U;
    if (good <= interval) {
        good = interval + 1U;
    }
    uint32_t next = good;
    if (grade == FlashcardGrade::Hard) {
        next = interval * 12U / 10U;
        if (next <= interval) {
            next = interval + 1U;
        }
        if (next > good) {
            next = good;
        }
    } else if (grade == FlashcardGrade::Easy) {
        next = (interval * ease * 13U + 5000U) / 10000U;
        if (next <= good) {
            next = good + 1U;
        }
    }
    return clamp_interval(next);
}

bool flashcards_schedule(FlashcardState &state,
                         FlashcardGrade grade,
                         uint16_t today)
{
    const bool was_review = state.interval_days > 0U;
    int ease = effective_ease(state);
    const uint16_t next = flashcards_next_interval(state, grade);

    switch (grade) {
    case FlashcardGrade::Again:
        if (was_review) {
            ease -= 200;
            if (state.lapses < 255U) {
                ++state.lapses;
            }
        }
        state.reps = 0U;
        break;
    case FlashcardGrade::Hard:
        if (was_review) {
            ease -= 150;
        }
        break;
    case FlashcardGrade::Good:
        break;
    case FlashcardGrade::Easy:
        ease += 150;
        break;
    }

    state.ease_permille = clamp_ease(ease);
    state.interval_days = next;
    state.due_day = static_cast<uint16_t>(today + next);
    if (next > 0U && state.reps < 255U) {
        ++state.reps;
    }
    return next == 0U;
}

void flashcards_interval_label(uint16_t days, char *out, size_t out_size)
{
    if (out == nullptr || out_size == 0U) {
        return;
    }
    if (days == 0U) {
        std::snprintf(out, out_size, "<10m");
    } else if (days < 31U) {
        std::snprintf(out, out_size, "%ud", static_cast<unsigned>(days));
    } else if (days < 365U) {
        std::snprintf(out, out_size, "%umo",
                      static_cast<unsigned>((days + 15U) / 30U));
    } else {
        const unsigned tenths = (days * 10U + 182U) / 365U;
        std::snprintf(out, out_size, "%u.%uy", tenths / 10U, tenths % 10U);
    }
}

uint16_t flashcards_days_from_civil(int year, unsigned month, unsigned day)
{
    if (year < 1970 || year > 2140 || month < 1U || month > 12U ||
        day < 1U || day > 31U) {
        return 0U;
    }
    // Howard Hinnant's days_from_civil.
    const int y = year - (month <= 2U ? 1 : 0);
    const int era = y / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned mp = (month + 9U) % 12U;
    const unsigned doy = (153U * mp + 2U) / 5U + day - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    const long days = static_cast<long>(era) * 146097L +
                      static_cast<long>(doe) - 719468L;
    return days > 0 && days < 65536 ? static_cast<uint16_t>(days) : 0U;
}

FlashcardsDeckStats flashcards_deck_stats(const FlashcardState *states,
                                          size_t count,
                                          uint16_t today)
{
    FlashcardsDeckStats stats = {};
    for (size_t i = 0U; i < count; ++i) {
        const FlashcardState &state = states[i];
        if (flashcards_is_new(state)) {
            ++stats.unseen;
            continue;
        }
        ++stats.learned;
        if (state.due_day <= today) {
            ++stats.due;
        }
        if (state.interval_days >= 21U) {
            ++stats.mature;
        }
    }
    return stats;
}

size_t flashcards_build_queue(const FlashcardState *states,
                              size_t count,
                              uint16_t today,
                              size_t new_allowed,
                              size_t max_reviews,
                              uint16_t *queue,
                              size_t capacity)
{
    if (states == nullptr || queue == nullptr || capacity == 0U) {
        return 0U;
    }

    constexpr size_t kMaxNewPerSession = 64U;
    uint16_t new_cards[kMaxNewPerSession];
    if (new_allowed > kMaxNewPerSession) {
        new_allowed = kMaxNewPerSession;
    }

    size_t reviews = 0U;
    size_t news = 0U;
    const size_t limit = count < 0x10000U ? count : 0x10000U;
    for (size_t i = 0U; i < limit; ++i) {
        if (flashcards_is_due(states[i], today) && reviews < max_reviews &&
            reviews < capacity) {
            queue[reviews++] = static_cast<uint16_t>(i);
        }
    }
    for (size_t i = 0U; i < limit && news < new_allowed &&
                        reviews + news < capacity;
         ++i) {
        if (flashcards_is_new(states[i])) {
            new_cards[news++] = static_cast<uint16_t>(i);
        }
    }

    // Merge from the back: new card j is placed after
    // (j + 1) * reviews / (news + 1) review cards, spreading them evenly.
    const size_t total = reviews + news;
    size_t review_index = reviews;
    size_t new_index = news;
    for (size_t out = total; out > 0U; --out) {
        const size_t reviews_before_new =
            new_index == 0U ? 0U : new_index * reviews / (news + 1U);
        const bool take_new =
            new_index > 0U && review_index <= reviews_before_new;
        queue[out - 1U] = take_new ? new_cards[--new_index]
                                   : queue[--review_index];
    }
    return total;
}

bool flashcards_requeue(uint16_t *queue,
                        size_t &length,
                        size_t capacity,
                        size_t position,
                        uint16_t card)
{
    if (queue == nullptr || length >= capacity) {
        return false;
    }
    size_t target = position + 1U + kFlashcardsRequeueGap;
    if (target > length) {
        target = length;
    }
    for (size_t i = length; i > target; --i) {
        queue[i] = queue[i - 1U];
    }
    queue[target] = card;
    ++length;
    return true;
}

uint32_t flashcards_crc32(const void *data, size_t size, uint32_t seed)
{
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    uint32_t crc = ~seed;
    for (size_t i = 0U; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

uint32_t flashcards_deck_hash(const char *name, size_t card_count)
{
    uint32_t hash = 2166136261U;
    for (const char *c = name; c != nullptr && *c != '\0'; ++c) {
        hash ^= static_cast<uint8_t>(*c);
        hash *= 16777619U;
    }
    for (int shift = 0; shift < 32; shift += 8) {
        hash ^= static_cast<uint8_t>(card_count >> shift);
        hash *= 16777619U;
    }
    return hash;
}

void flashcards_header_seal(FlashcardsProgressHeader &header,
                            const FlashcardState *states)
{
    header.magic = kFlashcardsProgressMagic;
    header.version = kFlashcardsProgressVersion;
    header.reserved = 0U;
    header.payload_crc = flashcards_crc32(
        states, sizeof(FlashcardState) * header.card_count);
    header.header_crc = flashcards_crc32(
        &header, offsetof(FlashcardsProgressHeader, header_crc));
}

bool flashcards_header_valid(const FlashcardsProgressHeader &header,
                             uint32_t deck_hash,
                             size_t card_count)
{
    return header.magic == kFlashcardsProgressMagic &&
           header.version == kFlashcardsProgressVersion &&
           header.deck_hash == deck_hash &&
           header.card_count == card_count &&
           header.header_crc ==
               flashcards_crc32(&header,
                                offsetof(FlashcardsProgressHeader,
                                         header_crc));
}

bool flashcards_payload_valid(const FlashcardsProgressHeader &header,
                              const FlashcardState *states)
{
    return header.payload_crc ==
           flashcards_crc32(states,
                            sizeof(FlashcardState) * header.card_count);
}
