#pragma once

#include <cstddef>
#include <cstdint>

// Hardware-independent to-do list model.

constexpr size_t kTodoMaxItems = 40U;
constexpr size_t kTodoTextBytes = 64U;  // including NUL, UTF-8

struct TodoItem {
    uint8_t done;
    char text[kTodoTextBytes];
};

struct TodoList {
    uint8_t count;
    TodoItem items[kTodoMaxItems];
};

void todo_list_clear(TodoList &list);

// Appends an item (text is truncated on a UTF-8 boundary). Returns the new
// index, or -1 when the list is full or the text is blank.
int todo_list_add(TodoList &list, const char *text);

bool todo_list_set_text(TodoList &list, size_t index, const char *text);
bool todo_list_remove(TodoList &list, size_t index);
bool todo_list_toggle(TodoList &list, size_t index);

// Removes all completed items; returns how many were removed.
size_t todo_list_clear_done(TodoList &list);

size_t todo_list_open_count(const TodoList &list);

// Copies text into a fixed buffer without splitting a UTF-8 sequence.
void todo_copy_text(char *dest, size_t dest_size, const char *src);

// Returns true when text contains only spaces.
bool todo_text_blank(const char *text);

// Persistent record.
struct TodoRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t crc;
    TodoList list;
};

constexpr uint32_t kTodoRecordMagic = 0x544F444FU;  // "TODO"
constexpr uint16_t kTodoRecordVersion = 1U;

void todo_record_seal(TodoRecord &record);
bool todo_record_valid(const TodoRecord &record);
