#pragma once

#include <cstddef>
#include <cstdint>

#include "todo_list.h"

class Canvas;

// Portrait (480 x 800 logical) pages for the to-do app.

constexpr size_t kTodoRowsPerPage = 7U;

enum class TodoKeyboardMode : uint8_t {
    Letters,
    Symbols,
};

enum class TodoAction : uint8_t {
    None,
    ToggleItem,
    EditItem,
    Add,
    ClearDone,
    PrevPage,
    NextPage,
    Key,
    Shift,
    Mode,
    Space,
    Backspace,
    Save,
    Cancel,
    Delete,
};

struct TodoHit {
    TodoAction action = TodoAction::None;
    size_t index = 0U;  // list item index or key index
};

struct TodoListView {
    const TodoList *list;
    size_t page;
};

struct TodoEditorView {
    const char *text;
    bool editing_existing;
    TodoKeyboardMode mode;
    bool shift;
};

size_t todo_page_count(const TodoList &list);

void todo_page_render_list(Canvas &canvas, const TodoListView &view);
void todo_page_render_editor(Canvas &canvas, const TodoEditorView &view);

TodoHit todo_page_list_hit(const TodoList &list, size_t page, int x, int y);
TodoHit todo_page_editor_hit(bool editing_existing,
                             TodoKeyboardMode mode,
                             int x,
                             int y);

// UTF-8 text produced by a character key (respecting shift).
const char *todo_key_text(TodoKeyboardMode mode,
                          size_t key_index,
                          bool shift,
                          char *buffer,
                          size_t buffer_size);

const char *todo_action_name(TodoAction action);
