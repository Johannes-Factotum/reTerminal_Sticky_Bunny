#include "flashcards_storage.h"

#include <algorithm>
#include <cstring>

#include "app_log.h"
#include "esp_partition.h"
#include "nvs.h"

namespace {

constexpr char kTag[] = "flashcards_store";
constexpr char kPartitionLabel[] = "assets";
constexpr char kNvsNamespace[] = "flashcards";
constexpr char kNvsDeckKey[] = "deck";
constexpr size_t kSectorSize = 4096U;
constexpr size_t kSlotSize = 128U * 1024U;
constexpr size_t kDeckRegionSize = kSlotSize * 2U;

// Slot each deck last loaded from / saved to; the next save uses the other.
uint8_t s_active_slot[64] = {};

const esp_partition_t *partition()
{
    static const esp_partition_t *s_partition = nullptr;
    if (s_partition == nullptr) {
        s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                               ESP_PARTITION_SUBTYPE_ANY,
                                               kPartitionLabel);
    }
    return s_partition;
}

size_t slot_bytes(size_t card_count)
{
    const size_t raw = sizeof(FlashcardsProgressHeader) +
                       card_count * sizeof(FlashcardState);
    return (raw + kSectorSize - 1U) / kSectorSize * kSectorSize;
}

esp_err_t slot_offset(size_t deck_index, uint8_t slot, size_t card_count,
                      size_t &offset)
{
    const esp_partition_t *part = partition();
    if (part == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }
    if (slot_bytes(card_count) > kSlotSize ||
        deck_index >= sizeof(s_active_slot)) {
        return ESP_ERR_INVALID_SIZE;
    }
    offset = deck_index * kDeckRegionSize + slot * kSlotSize;
    if (offset + kSlotSize > part->size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

bool read_header(size_t offset, FlashcardsProgressHeader &header)
{
    return esp_partition_read(partition(), offset, &header,
                              sizeof(header)) == ESP_OK;
}

}  // namespace

esp_err_t flashcards_storage_load(size_t deck_index,
                                  uint32_t deck_hash,
                                  size_t card_count,
                                  FlashcardState *states,
                                  FlashcardsProgressHeader &header)
{
    app_log_register_tag(kTag);
    std::fill_n(states, card_count, FlashcardState{});
    std::memset(&header, 0, sizeof(header));
    header.deck_hash = deck_hash;
    header.card_count = static_cast<uint16_t>(card_count);
    header.new_per_day_index = kFlashcardsDefaultNewPerDayIndex;

    size_t offsets[2] = {};
    for (uint8_t slot = 0U; slot < 2U; ++slot) {
        const esp_err_t result =
            slot_offset(deck_index, slot, card_count, offsets[slot]);
        if (result != ESP_OK) {
            STICKY_LOGE(kTag, "flashcards=storage load deck=%u result=%s",
                        static_cast<unsigned>(deck_index),
                        esp_err_to_name(result));
            return result;
        }
    }

    FlashcardsProgressHeader candidates[2] = {};
    bool valid[2] = {};
    for (uint8_t slot = 0U; slot < 2U; ++slot) {
        valid[slot] = read_header(offsets[slot], candidates[slot]) &&
                      flashcards_header_valid(candidates[slot], deck_hash,
                                              card_count);
    }

    // Try the newest valid slot first, then fall back to the other one.
    uint8_t order[2] = {0U, 1U};
    if (valid[1] && (!valid[0] ||
                     candidates[1].sequence > candidates[0].sequence)) {
        order[0] = 1U;
        order[1] = 0U;
    }
    for (uint8_t slot : order) {
        if (!valid[slot]) {
            continue;
        }
        const esp_err_t read_result = esp_partition_read(
            partition(),
            offsets[slot] + sizeof(FlashcardsProgressHeader),
            states,
            card_count * sizeof(FlashcardState));
        if (read_result == ESP_OK &&
            flashcards_payload_valid(candidates[slot], states)) {
            header = candidates[slot];
            s_active_slot[deck_index] = slot;
            STICKY_LOGI(kTag,
                        "flashcards=storage load deck=%u slot=%u seq=%u result=ok",
                        static_cast<unsigned>(deck_index),
                        static_cast<unsigned>(slot),
                        static_cast<unsigned>(header.sequence));
            return ESP_OK;
        }
    }

    std::fill_n(states, card_count, FlashcardState{});
    s_active_slot[deck_index] = 1U;  // first save goes to slot 0
    STICKY_LOGI(kTag, "flashcards=storage load deck=%u result=fresh",
                static_cast<unsigned>(deck_index));
    return ESP_ERR_NOT_FOUND;
}

esp_err_t flashcards_storage_save(size_t deck_index,
                                  FlashcardsProgressHeader &header,
                                  const FlashcardState *states)
{
    if (deck_index >= sizeof(s_active_slot)) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t target = s_active_slot[deck_index] == 0U ? 1U : 0U;
    size_t offset = 0U;
    esp_err_t result =
        slot_offset(deck_index, target, header.card_count, offset);
    if (result != ESP_OK) {
        return result;
    }

    ++header.sequence;
    flashcards_header_seal(header, states);

    result = esp_partition_erase_range(partition(), offset,
                                       slot_bytes(header.card_count));
    if (result == ESP_OK) {
        result = esp_partition_write(
            partition(), offset + sizeof(FlashcardsProgressHeader), states,
            header.card_count * sizeof(FlashcardState));
    }
    if (result == ESP_OK) {
        // Header goes last: a power cut before this point leaves the old
        // slot as the newest valid copy.
        result = esp_partition_write(partition(), offset, &header,
                                     sizeof(header));
    }
    if (result == ESP_OK) {
        s_active_slot[deck_index] = target;
    }
    STICKY_LOGI(kTag, "flashcards=storage save deck=%u slot=%u seq=%u result=%s",
                static_cast<unsigned>(deck_index),
                static_cast<unsigned>(target),
                static_cast<unsigned>(header.sequence),
                esp_err_to_name(result));
    return result;
}

size_t flashcards_storage_load_selected_deck()
{
    nvs_handle_t handle = 0;
    uint8_t value = 0U;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, kNvsDeckKey, &value);
        nvs_close(handle);
    }
    return value;
}

void flashcards_storage_save_selected_deck(size_t deck_index)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_u8(handle, kNvsDeckKey, static_cast<uint8_t>(deck_index));
    nvs_commit(handle);
    nvs_close(handle);
}
