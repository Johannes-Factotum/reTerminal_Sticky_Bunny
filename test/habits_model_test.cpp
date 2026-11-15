#include <cassert>
#include <cstring>

#include "habits_model.h"

int main()
{
    // Defaults are the four example habits.
    HabitsConfig config = {};
    habits_default_config(config);
    assert(config.count == 4U);
    assert(std::strcmp(config.items[0].text, "Do 3 sun salutations") == 0);
    assert(config.items[3].icon ==
           static_cast<uint8_t>(HabitIcon::MorningPages));

    // JSON round trip, including quotes and UTF-8.
    std::strcpy(config.items[2].text, "Eat \"big\" breakfast \xC3\xB1" "am");
    char json[2048];
    const size_t length = habits_to_json(config, json, sizeof(json));
    assert(length < sizeof(json));
    HabitsConfig parsed = {};
    assert(habits_from_json(json, std::strlen(json), parsed));
    assert(parsed.count == 4U);
    assert(std::strcmp(parsed.name, config.name) == 0);
    assert(std::strcmp(parsed.items[2].text, config.items[2].text) == 0);
    assert(parsed.items[1].icon == config.items[1].icon);

    // What the phone page sends: \u escapes, extra keys, whitespace.
    const char *phone =
        "{ \"name\" : \"Ma\\u00f1ana\", \"extra\": [1, {\"a\": null}],\n"
        "  \"habits\": [ {\"text\": \"Stretch \\ud83d\\ude00\", \"icon\": 1,"
        " \"id\": 7}, {\"icon\": 99, \"text\": \"  Read  \"},"
        " {\"text\": \"   \", \"icon\": 2} ] }";
    assert(habits_from_json(phone, std::strlen(phone), parsed));
    assert(std::strcmp(parsed.name, "Ma\xC3\xB1" "ana") == 0);
    assert(parsed.count == 3U);
    assert(std::strcmp(parsed.items[0].text,
                       "Stretch \xF0\x9F\x98\x80") == 0);
    assert(parsed.items[1].icon == 0U);  // out-of-range icon -> generic
    assert(habits_sanitize(parsed));
    assert(parsed.count == 2U);  // blank habit dropped
    assert(std::strcmp(parsed.items[1].text, "Read") == 0);

    // Too many habits are capped; long text is cut on a UTF-8 boundary.
    char big[8192];
    std::strcpy(big, "{\"name\":\"x\",\"habits\":[");
    for (int i = 0; i < 20; ++i) {
        std::strcat(big, i == 0 ? "" : ",");
        std::strcat(big, "{\"text\":\"");
        for (int j = 0; j < 60; ++j) {
            std::strcat(big, "\xC3\xA9");
        }
        std::strcat(big, "\",\"icon\":3}");
    }
    std::strcat(big, "]}");
    assert(habits_from_json(big, std::strlen(big), parsed));
    assert(parsed.count == kHabitsMax);
    const size_t text_len = std::strlen(parsed.items[0].text);
    assert(text_len < kHabitTextBytes && text_len % 2U == 0U);

    // Invalid JSON is rejected.
    assert(!habits_from_json("{\"name\":", 8, parsed));
    assert(!habits_from_json("[1,2]", 5, parsed));
    assert(habits_from_json("{}", 2, parsed));
    assert(!habits_sanitize(parsed));  // no habits
    assert(std::strcmp(parsed.name, "My routine") == 0);

    // Daily progress and streak.
    HabitsProgress progress = {};
    habits_progress_roll(progress, 100);
    assert(habits_streak(progress, 100) == 0U);
    progress.results[0] = static_cast<uint8_t>(HabitResult::Done);
    progress.results[1] = static_cast<uint8_t>(HabitResult::Skipped);
    assert(habits_next_pending(progress, 4, 0) == 2U);
    assert(habits_next_pending(progress, 4, 3) == 3U);
    assert(habits_count_result(progress, 4, HabitResult::Done) == 1U);
    habits_progress_complete(progress, 100);
    assert(habits_streak(progress, 100) == 1U);
    habits_progress_complete(progress, 100);  // same day counts once
    assert(habits_streak(progress, 100) == 1U);
    assert(habits_streak(progress, 101) == 1U);  // still alive next day
    habits_progress_roll(progress, 101);
    assert(progress.results[0] == 0U);  // new day clears results
    habits_progress_complete(progress, 101);
    assert(habits_streak(progress, 101) == 2U);
    assert(habits_streak(progress, 103) == 0U);  // missed day 102
    habits_progress_complete(progress, 103);
    assert(habits_streak(progress, 103) == 1U);

    // Records.
    HabitsConfigRecord record = {};
    record.config = config;
    habits_seal(record);
    assert(habits_valid(record));
    record.config.items[0].text[0] ^= 1;
    assert(!habits_valid(record));
    HabitsProgressRecord prog_record = {};
    prog_record.progress = progress;
    habits_seal(prog_record);
    assert(habits_valid(prog_record));
    return 0;
}
