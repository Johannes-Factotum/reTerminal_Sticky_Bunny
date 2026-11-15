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
