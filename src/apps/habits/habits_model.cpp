#include "habits_model.h"

#include <cstdio>
#include <cstring>

namespace {

void set(Habit &habit, HabitIcon icon, const char *text)
{
    habit.icon = static_cast<uint8_t>(icon);
    std::snprintf(habit.text, sizeof(habit.text), "%s", text);
}

}  // namespace

void habits_default_config(HabitsConfig &config)
{
    std::memset(&config, 0, sizeof(config));
    std::snprintf(config.name, sizeof(config.name), "Morning routine");
    set(config.items[0], HabitIcon::SunSalutation, "Do 3 sun salutations");
    set(config.items[1], HabitIcon::Pullups, "Lift weights / do pull-ups");
    set(config.items[2], HabitIcon::Breakfast, "Eat breakfast");
    set(config.items[3], HabitIcon::MorningPages, "Write morning pages");
    config.count = 4U;
}

const char *habits_icon_name(HabitIcon icon)
{
    switch (icon) {
    case HabitIcon::Generic:
        return "generic";
    case HabitIcon::SunSalutation:
        return "sun_salutation";
    case HabitIcon::Pullups:
        return "pullups";
    case HabitIcon::Breakfast:
        return "breakfast";
    case HabitIcon::MorningPages:
        return "morning_pages";
    case HabitIcon::Count:
        break;
    }
    return "unknown";
}
