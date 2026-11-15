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

// ---------------------------------------------------------------------------

namespace {

uint32_t crc32(const void *data, size_t size)
{
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

// Copies at most size-1 bytes without cutting a UTF-8 sequence.
void copy_utf8(char *dest, size_t size, const char *src, size_t length)
{
    if (length >= size) {
        length = size - 1U;
        while (length > 0U &&
               (static_cast<unsigned char>(src[length]) & 0xC0U) == 0x80U) {
            --length;
        }
    }
    std::memcpy(dest, src, length);
    std::memset(dest + length, 0, size - length);
}

void trim(char *text)
{
    size_t start = 0U;
    while (text[start] == ' ' || text[start] == '\t') {
        ++start;
    }
    size_t length = std::strlen(text + start);
    while (length > 0U &&
           (text[start + length - 1U] == ' ' ||
            text[start + length - 1U] == '\t')) {
        --length;
    }
    std::memmove(text, text + start, length);
    text[length] = '\0';
    for (char *c = text; *c != '\0'; ++c) {
        if (static_cast<unsigned char>(*c) < 0x20U) {
            *c = ' ';  // no control characters on the e-paper
        }
    }
}

// ---- Tiny JSON reader for the fixed editor schema ----

struct Reader {
    const char *p;
    const char *end;
    bool ok;
};

void skip_ws(Reader &r)
{
    while (r.p < r.end &&
           (*r.p == ' ' || *r.p == '\n' || *r.p == '\r' || *r.p == '\t')) {
        ++r.p;
    }
}

bool expect(Reader &r, char c)
{
    skip_ws(r);
    if (r.p < r.end && *r.p == c) {
        ++r.p;
        return true;
    }
    r.ok = false;
    return false;
}

bool peek(Reader &r, char c)
{
    skip_ws(r);
    return r.p < r.end && *r.p == c;
}

void append_utf8(char *out, size_t size, size_t &used, uint32_t cp)
{
    char buf[4];
    size_t n = 0U;
    if (cp < 0x80U) {
        buf[n++] = static_cast<char>(cp);
    } else if (cp < 0x800U) {
        buf[n++] = static_cast<char>(0xC0U | (cp >> 6));
        buf[n++] = static_cast<char>(0x80U | (cp & 0x3FU));
    } else if (cp < 0x10000U) {
        buf[n++] = static_cast<char>(0xE0U | (cp >> 12));
        buf[n++] = static_cast<char>(0x80U | ((cp >> 6) & 0x3FU));
        buf[n++] = static_cast<char>(0x80U | (cp & 0x3FU));
    } else {
        buf[n++] = static_cast<char>(0xF0U | (cp >> 18));
        buf[n++] = static_cast<char>(0x80U | ((cp >> 12) & 0x3FU));
        buf[n++] = static_cast<char>(0x80U | ((cp >> 6) & 0x3FU));
        buf[n++] = static_cast<char>(0x80U | (cp & 0x3FU));
    }
    if (used + n < size) {
        std::memcpy(out + used, buf, n);
        used += n;
    }
}

int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Reads a JSON string into out (truncated to size-1 bytes, UTF-8 safe).
bool read_string(Reader &r, char *out, size_t size)
{
    if (!expect(r, '"')) {
        return false;
    }
    char buffer[256];
    size_t used = 0U;
    while (r.p < r.end && *r.p != '"') {
        char c = *r.p++;
        if (c != '\\') {
            if (used + 1U < sizeof(buffer)) {
                buffer[used++] = c;
            }
            continue;
        }
        if (r.p >= r.end) {
            break;
        }
        c = *r.p++;
        switch (c) {
        case 'n':
        case 't':
        case 'r':
            append_utf8(buffer, sizeof(buffer), used, ' ');
            break;
        case 'b':
        case 'f':
            break;
        case 'u': {
            uint32_t cp = 0U;
            for (int i = 0; i < 4; ++i) {
                const int v = r.p < r.end ? hex_value(*r.p++) : -1;
                if (v < 0) {
                    r.ok = false;
                    return false;
                }
                cp = (cp << 4) | static_cast<uint32_t>(v);
            }
            if (cp >= 0xD800U && cp <= 0xDBFFU && r.end - r.p >= 6 &&
                r.p[0] == '\\' && r.p[1] == 'u') {
                uint32_t low = 0U;
                bool good = true;
                for (int i = 2; i < 6; ++i) {
                    const int v = hex_value(r.p[i]);
                    good = good && v >= 0;
                    low = (low << 4) | static_cast<uint32_t>(v < 0 ? 0 : v);
                }
                if (good && low >= 0xDC00U && low <= 0xDFFFU) {
                    cp = 0x10000U + ((cp - 0xD800U) << 10) + (low - 0xDC00U);
                    r.p += 6;
                }
            }
            append_utf8(buffer, sizeof(buffer), used, cp);
            break;
        }
        default:  // \" \\ \/
            append_utf8(buffer, sizeof(buffer), used,
                        static_cast<unsigned char>(c));
            break;
        }
    }
    if (!expect(r, '"')) {
        return false;
    }
    if (out != nullptr) {
        copy_utf8(out, size, buffer, used);
    }
    return true;
}

bool read_number(Reader &r, long &value)
{
    skip_ws(r);
    bool negative = false;
    if (r.p < r.end && *r.p == '-') {
        negative = true;
        ++r.p;
    }
    if (r.p >= r.end || *r.p < '0' || *r.p > '9') {
        r.ok = false;
        return false;
    }
    long v = 0;
    while (r.p < r.end && *r.p >= '0' && *r.p <= '9') {
        if (v < 100000) {
            v = v * 10 + (*r.p - '0');
        }
        ++r.p;
    }
    // Ignore any fraction / exponent.
    while (r.p < r.end && (*r.p == '.' || *r.p == 'e' || *r.p == 'E' ||
                           *r.p == '+' || *r.p == '-' ||
                           (*r.p >= '0' && *r.p <= '9'))) {
        ++r.p;
    }
    value = negative ? -v : v;
    return true;
}

void skip_value(Reader &r, int depth = 0)
{
    skip_ws(r);
    if (r.p >= r.end || depth > 8) {
        r.ok = false;
        return;
    }
    if (*r.p == '"') {
        read_string(r, nullptr, 0U);
    } else if (*r.p == '{' || *r.p == '[') {
        const char close = *r.p == '{' ? '}' : ']';
        const bool object = *r.p == '{';
        ++r.p;
        if (peek(r, close)) {
            ++r.p;
            return;
        }
        while (r.ok) {
            if (object) {
                read_string(r, nullptr, 0U);
                expect(r, ':');
            }
            skip_value(r, depth + 1);
            if (peek(r, ',')) {
                ++r.p;
                continue;
            }
            expect(r, close);
            return;
        }
    } else {
        // number, true, false, null
        while (r.p < r.end && *r.p != ',' && *r.p != '}' && *r.p != ']' &&
               *r.p != ' ' && *r.p != '\n') {
            ++r.p;
        }
    }
}

void read_habit(Reader &r, HabitsConfig &out)
{
    Habit habit = {};
    if (!expect(r, '{')) {
        return;
    }
    if (!peek(r, '}')) {
        while (r.ok) {
            char key[16];
            read_string(r, key, sizeof(key));
            expect(r, ':');
            if (std::strcmp(key, "text") == 0) {
                read_string(r, habit.text, sizeof(habit.text));
            } else if (std::strcmp(key, "icon") == 0) {
                long icon = 0;
                read_number(r, icon);
                habit.icon = icon >= 0 &&
                                     icon < static_cast<long>(HabitIcon::Count)
                                 ? static_cast<uint8_t>(icon)
                                 : 0U;
            } else {
                skip_value(r);
            }
            if (peek(r, ',')) {
                ++r.p;
                continue;
            }
            break;
        }
    }
    expect(r, '}');
    if (r.ok && out.count < kHabitsMax) {
        out.items[out.count++] = habit;
    }
}

// Writes a JSON-escaped copy of text.
size_t write_escaped(char *out, size_t size, size_t used, const char *text)
{
    for (const char *c = text; *c != '\0'; ++c) {
        char esc[8];
        const unsigned char u = static_cast<unsigned char>(*c);
        if (*c == '"' || *c == '\\') {
            std::snprintf(esc, sizeof(esc), "\\%c", *c);
        } else if (u < 0x20U) {
            std::snprintf(esc, sizeof(esc), "\\u%04x", u);
        } else {
            esc[0] = *c;
            esc[1] = '\0';
        }
        const size_t n = std::strlen(esc);
        if (used + n < size) {
            std::memcpy(out + used, esc, n);
        }
        used += n;
    }
    return used;
}

size_t write_raw(char *out, size_t size, size_t used, const char *text)
{
    const size_t n = std::strlen(text);
    if (used + n < size) {
        std::memcpy(out + used, text, n);
    }
    return used + n;
}

}  // namespace

void habits_progress_roll(HabitsProgress &progress, uint16_t today)
{
    if (progress.day != today) {
        progress.day = today;
        std::memset(progress.results, 0, sizeof(progress.results));
    }
}

uint16_t habits_streak(const HabitsProgress &progress, uint16_t today)
{
    if (progress.last_complete_day == 0U ||
        progress.last_complete_day + 1U < today) {
        return 0U;
    }
    return progress.streak;
}

void habits_progress_complete(HabitsProgress &progress, uint16_t today)
{
    if (progress.last_complete_day == today) {
        return;  // already counted today
    }
    if (progress.last_complete_day != 0U &&
        progress.last_complete_day + 1U == today) {
        ++progress.streak;
    } else {
        progress.streak = 1U;
    }
    progress.last_complete_day = today;
}

size_t habits_next_pending(const HabitsProgress &progress, size_t count,
                           size_t from)
{
    for (size_t i = from; i < count; ++i) {
        if (progress.results[i] ==
            static_cast<uint8_t>(HabitResult::Pending)) {
            return i;
        }
    }
    return count;
}

size_t habits_count_result(const HabitsProgress &progress, size_t count,
                           HabitResult result)
{
    size_t n = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (progress.results[i] == static_cast<uint8_t>(result)) {
            ++n;
        }
    }
    return n;
}

bool habits_sanitize(HabitsConfig &config)
{
    config.name[sizeof(config.name) - 1U] = '\0';
    trim(config.name);
    if (config.name[0] == '\0') {
        std::snprintf(config.name, sizeof(config.name), "My routine");
    }
    if (config.count > kHabitsMax) {
        config.count = kHabitsMax;
    }
    size_t write = 0U;
    for (size_t read = 0U; read < config.count; ++read) {
        Habit habit = config.items[read];
        habit.text[sizeof(habit.text) - 1U] = '\0';
        trim(habit.text);
        if (habit.text[0] == '\0') {
            continue;
        }
        if (habit.icon >= static_cast<uint8_t>(HabitIcon::Count)) {
            habit.icon = 0U;
        }
        config.items[write++] = habit;
    }
    for (size_t i = write; i < kHabitsMax; ++i) {
        std::memset(&config.items[i], 0, sizeof(Habit));
    }
    config.count = static_cast<uint8_t>(write);
    return write > 0U;
}

size_t habits_to_json(const HabitsConfig &config, char *out, size_t size)
{
    size_t used = write_raw(out, size, 0U, "{\"name\":\"");
    used = write_escaped(out, size, used, config.name);
    used = write_raw(out, size, used, "\",\"habits\":[");
    for (size_t i = 0U; i < config.count && i < kHabitsMax; ++i) {
        char head[32];
        std::snprintf(head, sizeof(head), "%s{\"icon\":%u,\"text\":\"",
                      i == 0U ? "" : ",",
                      static_cast<unsigned>(config.items[i].icon));
        used = write_raw(out, size, used, head);
        used = write_escaped(out, size, used, config.items[i].text);
        used = write_raw(out, size, used, "\"}");
    }
    used = write_raw(out, size, used, "]}");
    if (size > 0U) {
        out[used < size ? used : size - 1U] = '\0';
    }
    return used;
}

bool habits_from_json(const char *json, size_t length, HabitsConfig &out)
{
    std::memset(&out, 0, sizeof(out));
    Reader r = {json, json + length, true};
    if (!expect(r, '{')) {
        return false;
    }
    if (!peek(r, '}')) {
        while (r.ok) {
            char key[16];
            read_string(r, key, sizeof(key));
            expect(r, ':');
            if (!r.ok) {
                break;
            }
            if (std::strcmp(key, "name") == 0) {
                read_string(r, out.name, sizeof(out.name));
            } else if (std::strcmp(key, "habits") == 0) {
                expect(r, '[');
                if (peek(r, ']')) {
                    ++r.p;
                } else {
                    while (r.ok) {
                        read_habit(r, out);
                        if (peek(r, ',')) {
                            ++r.p;
                            continue;
                        }
                        expect(r, ']');
                        break;
                    }
                }
            } else {
                skip_value(r);
            }
            if (peek(r, ',')) {
                ++r.p;
                continue;
            }
            break;
        }
    }
    expect(r, '}');
    return r.ok;
}

void habits_seal(HabitsConfigRecord &record)
{
    record.magic = kHabitsConfigMagic;
    record.crc = crc32(&record.config, sizeof(record.config));
}

bool habits_valid(const HabitsConfigRecord &record)
{
    return record.magic == kHabitsConfigMagic &&
           record.config.count <= kHabitsMax &&
           record.crc == crc32(&record.config, sizeof(record.config));
}

void habits_seal(HabitsProgressRecord &record)
{
    record.magic = kHabitsProgressMagic;
    record.crc = crc32(&record.progress, sizeof(record.progress));
}

bool habits_valid(const HabitsProgressRecord &record)
{
    return record.magic == kHabitsProgressMagic &&
           record.crc == crc32(&record.progress, sizeof(record.progress));
}
