#pragma once

#include <cstddef>
#include <cstdint>

// Built-in flashcard decks generated from assets/flashcards/decks/*.tsv by
// tools/generate_flashcards_decks.py. Card i uses strings 2*i (front) and
// 2*i+1 (back) inside one NUL-separated UTF-8 blob.
struct FlashcardsDeck {
    const char *name;
    uint16_t card_count;
    const char *blob;
    const uint32_t *offsets;
};

size_t flashcards_deck_count();
const FlashcardsDeck &flashcards_deck(size_t index);

inline const char *flashcards_deck_front(const FlashcardsDeck &deck,
                                         size_t card)
{
    return deck.blob + deck.offsets[card * 2U];
}

inline const char *flashcards_deck_back(const FlashcardsDeck &deck,
                                        size_t card)
{
    return deck.blob + deck.offsets[card * 2U + 1U];
}
