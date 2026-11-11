#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "flashcards_srs.h"

// Persists per-deck progress in the otherwise unused "assets" data partition
// using two alternating CRC-protected slots per deck (power-loss safe).

// Loads the newest valid slot. Returns ESP_ERR_NOT_FOUND when no progress
// exists yet (states are then zeroed and header defaults are filled in).
esp_err_t flashcards_storage_load(size_t deck_index,
                                  uint32_t deck_hash,
                                  size_t card_count,
                                  FlashcardState *states,
                                  FlashcardsProgressHeader &header);

// Writes the progress into the slot not holding the newest copy.
esp_err_t flashcards_storage_save(size_t deck_index,
                                  FlashcardsProgressHeader &header,
                                  const FlashcardState *states);

// Remembers which deck was open last (NVS).
size_t flashcards_storage_load_selected_deck();
void flashcards_storage_save_selected_deck(size_t deck_index);
