#pragma once

#include <cstddef>
#include <cstdint>

// Hardware-independent habit routine model.

constexpr size_t kHabitsMax = 12U;
constexpr size_t kHabitTextBytes = 96U;
constexpr size_t kRoutineNameBytes = 32U;

// Order matches HabitArtId (habits_assets.h); Celebrate is page art only.
enum class HabitIcon : uint8_t {
    Generic,
    SunSalutation,
    Pullups,
    Breakfast,
    MorningPages,
    Count,
};

struct Habit {
    uint8_t icon;
    char text[kHabitTextBytes];
};

struct HabitsConfig {
    char name[kRoutineNameBytes];
    uint8_t count;
    Habit items[kHabitsMax];
};

enum class HabitResult : uint8_t {
    Pending,
    Done,
    Skipped,
};

void habits_default_config(HabitsConfig &config);
const char *habits_icon_name(HabitIcon icon);

// ---- Daily progress and streak ----

struct HabitsProgress {
    uint16_t day;                   // day the results belong to
    uint16_t streak;                // consecutive completed days
    uint16_t last_complete_day;     // 0 = never
    uint8_t results[kHabitsMax];    // HabitResult values
};

// Clears the results when `today` differs from progress.day.
void habits_progress_roll(HabitsProgress &progress, uint16_t today);

// Streak to show today: 0 once a full day was missed.
uint16_t habits_streak(const HabitsProgress &progress, uint16_t today);

// Records reaching the end of the routine today (skips allowed).
void habits_progress_complete(HabitsProgress &progress, uint16_t today);

// Index of the first pending habit at or after `from`, or count if none.
size_t habits_next_pending(const HabitsProgress &progress, size_t count,
                           size_t from);

size_t habits_count_result(const HabitsProgress &progress, size_t count,
                           HabitResult result);

// Trims, validates and clamps a config received from the phone. Returns
// false when it has no usable habit.
bool habits_sanitize(HabitsConfig &config);

// ---- JSON used by the phone editor ----

// {"name":"...","habits":[{"text":"...","icon":1},...]}
size_t habits_to_json(const HabitsConfig &config, char *out, size_t size);
bool habits_from_json(const char *json, size_t length, HabitsConfig &out);

// ---- Persistent records ----

struct HabitsConfigRecord {
    uint32_t magic;
    uint32_t crc;
    HabitsConfig config;
};

struct HabitsProgressRecord {
    uint32_t magic;
    uint32_t crc;
    HabitsProgress progress;
};

constexpr uint32_t kHabitsConfigMagic = 0x48434647U;    // "HCFG"
constexpr uint32_t kHabitsProgressMagic = 0x48505247U;  // "HPRG"

void habits_seal(HabitsConfigRecord &record);
bool habits_valid(const HabitsConfigRecord &record);
void habits_seal(HabitsProgressRecord &record);
bool habits_valid(const HabitsProgressRecord &record);
