#include <cassert>
#include <cstring>

#include "todo_list.h"

int main()
{
    TodoList list = {};
    todo_list_clear(list);
    assert(todo_list_add(list, "   ") == -1);
    assert(todo_list_add(list, "Buy milk") == 0);
    assert(todo_list_add(list, "Call mam\xC3\xA1") == 1);
    assert(todo_list_add(list, "Study espa\xC3\xB1ol") == 2);
    assert(list.count == 3U);
    assert(todo_list_open_count(list) == 3U);

    assert(todo_list_toggle(list, 1));
    assert(list.items[1].done == 1U);
    assert(todo_list_open_count(list) == 2U);
    assert(!todo_list_toggle(list, 7));

    assert(todo_list_set_text(list, 0, "Buy oat milk"));
    assert(std::strcmp(list.items[0].text, "Buy oat milk") == 0);
    assert(!todo_list_set_text(list, 0, ""));

    assert(todo_list_clear_done(list) == 1U);
    assert(list.count == 2U);
    assert(std::strcmp(list.items[1].text, "Study espa\xC3\xB1ol") == 0);

    assert(todo_list_remove(list, 0));
    assert(list.count == 1U);
    assert(!todo_list_remove(list, 3));

    // Truncation never splits a multi-byte character.
    char long_text[200];
    std::memset(long_text, 0, sizeof(long_text));
    for (int i = 0; i < 40; ++i) {
        std::strcat(long_text, "\xC3\xB1");  // ñ, 2 bytes each
    }
    char dest[kTodoTextBytes];
    todo_copy_text(dest, sizeof(dest), long_text);
    assert(std::strlen(dest) == 62U);
    // odd cut: prefix one ASCII byte
    char odd[200] = "x";
    std::strcat(odd, long_text);
    todo_copy_text(dest, sizeof(dest), odd);
    assert(std::strlen(dest) == 63U);
    assert((static_cast<unsigned char>(dest[62]) & 0xC0U) == 0x80U);

    // Capacity.
    TodoList full = {};
    for (size_t i = 0; i < kTodoMaxItems; ++i) {
        assert(todo_list_add(full, "task") == static_cast<int>(i));
    }
    assert(todo_list_add(full, "one more") == -1);

    // Record integrity.
    TodoRecord record = {};
    record.list = list;
    todo_record_seal(record);
    assert(todo_record_valid(record));
    record.list.items[0].text[0] ^= 1;
    assert(!todo_record_valid(record));
    return 0;
}
