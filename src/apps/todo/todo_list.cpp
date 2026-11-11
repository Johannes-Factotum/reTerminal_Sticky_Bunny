#include "todo_list.h"

#include <cstring>

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

}  // namespace

void todo_copy_text(char *dest, size_t dest_size, const char *src)
{
    if (dest == nullptr || dest_size == 0U) {
        return;
    }
    size_t length = src == nullptr ? 0U : std::strlen(src);
    if (length >= dest_size) {
        length = dest_size - 1U;
        // Step back over UTF-8 continuation bytes so no sequence is cut.
        while (length > 0U &&
               (static_cast<unsigned char>(src[length]) & 0xC0U) == 0x80U) {
            --length;
        }
    }
    if (length > 0U) {
        std::memcpy(dest, src, length);
    }
    std::memset(dest + length, 0, dest_size - length);
}

bool todo_text_blank(const char *text)
{
    for (const char *c = text; c != nullptr && *c != '\0'; ++c) {
        if (*c != ' ') {
            return false;
        }
    }
    return true;
}

void todo_list_clear(TodoList &list)
{
    std::memset(&list, 0, sizeof(list));
}

int todo_list_add(TodoList &list, const char *text)
{
    if (list.count >= kTodoMaxItems || todo_text_blank(text)) {
        return -1;
    }
    TodoItem &item = list.items[list.count];
    item.done = 0U;
    todo_copy_text(item.text, sizeof(item.text), text);
    return list.count++;
}

bool todo_list_set_text(TodoList &list, size_t index, const char *text)
{
    if (index >= list.count || todo_text_blank(text)) {
        return false;
    }
    todo_copy_text(list.items[index].text, kTodoTextBytes, text);
    return true;
}

bool todo_list_remove(TodoList &list, size_t index)
{
    if (index >= list.count) {
        return false;
    }
    for (size_t i = index; i + 1U < list.count; ++i) {
        list.items[i] = list.items[i + 1U];
    }
    --list.count;
    std::memset(&list.items[list.count], 0, sizeof(TodoItem));
    return true;
}

bool todo_list_toggle(TodoList &list, size_t index)
{
    if (index >= list.count) {
        return false;
    }
    list.items[index].done = list.items[index].done != 0U ? 0U : 1U;
    return true;
}

size_t todo_list_clear_done(TodoList &list)
{
    size_t removed = 0U;
    size_t write = 0U;
    for (size_t read = 0U; read < list.count; ++read) {
        if (list.items[read].done != 0U) {
            ++removed;
            continue;
        }
        if (write != read) {
            list.items[write] = list.items[read];
        }
        ++write;
    }
    for (size_t i = write; i < list.count; ++i) {
        std::memset(&list.items[i], 0, sizeof(TodoItem));
    }
    list.count = static_cast<uint8_t>(write);
    return removed;
}

size_t todo_list_open_count(const TodoList &list)
{
    size_t open = 0U;
    for (size_t i = 0U; i < list.count; ++i) {
        if (list.items[i].done == 0U) {
            ++open;
        }
    }
    return open;
}

void todo_record_seal(TodoRecord &record)
{
    record.magic = kTodoRecordMagic;
    record.version = kTodoRecordVersion;
    record.reserved = 0U;
    record.crc = crc32(&record.list, sizeof(record.list));
}

bool todo_record_valid(const TodoRecord &record)
{
    return record.magic == kTodoRecordMagic &&
           record.version == kTodoRecordVersion &&
           record.list.count <= kTodoMaxItems &&
           record.crc == crc32(&record.list, sizeof(record.list));
}
