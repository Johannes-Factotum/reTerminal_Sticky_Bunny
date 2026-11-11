#include "todo_pages.h"

#include <cstdio>
#include <cstring>

#include "canvas.h"
#include "text_utf8.h"
#include "ui_widgets.h"

namespace {

constexpr int kWidth = 480;
constexpr int kCenterX = kWidth / 2;

// List page.
constexpr int kListTop = 128;
constexpr int kRowHeight = 66;
constexpr int kCheckboxX = 20;
constexpr int kCheckboxSize = 40;
constexpr int kTextX = 76;
constexpr int kEditX = 408;
constexpr UiRect kPrevPageRect = {16, 604, 80, 56};
constexpr UiRect kNextPageRect = {384, 604, 80, 56};
constexpr UiRect kClearDoneRect = {16, 676, 210, 76};
constexpr UiRect kAddRect = {240, 676, 224, 76};

// Editor page.
constexpr UiRect kTextBoxRect = {16, 84, 448, 252};
constexpr int kKeyRowY[4] = {356, 432, 508, 584};
constexpr int kKeyHeight = 68;
constexpr int kKeyPitch = 47;
constexpr int kKeyWidth = 44;
constexpr UiRect kModeRect = {6, 584, 92, 68};
constexpr UiRect kShiftRect = {104, 584, 80, 68};
constexpr UiRect kSpaceRect = {190, 584, 284, 68};
constexpr UiRect kBackspaceRect = {382, 508, 92, 68};
constexpr UiRect kCancelRect = {16, 676, 140, 76};
constexpr UiRect kDeleteRect = {170, 676, 140, 76};
constexpr UiRect kSaveRect = {324, 676, 140, 76};

// Character keys: rows of UTF-8 single characters.
const char *const kLetterRows[3][10] = {
    {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
    {"a", "s", "d", "f", "g", "h", "j", "k", "l", nullptr},
    {"z", "x", "c", "v", "b", "n", "m", nullptr, nullptr, nullptr},
};
const char *const kSymbolRows[3][10] = {
    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
    {"\xC3\xA1", "\xC3\xA9", "\xC3\xAD", "\xC3\xB3", "\xC3\xBA",
     "\xC3\xB1", "\xC3\xBC", "\xC2\xBF", "\xC2\xA1", nullptr},
    {".", ",", "?", "!", "-", ":", "/", nullptr, nullptr, nullptr},
};
constexpr int kRowKeyCount[3] = {10, 9, 7};
constexpr int kRowStartX[3] = {6, 29, 53};

const char *key_label(TodoKeyboardMode mode, size_t index)
{
    const size_t row = index / 10U;
    const size_t column = index % 10U;
    if (row >= 3U) {
        return nullptr;
    }
    return mode == TodoKeyboardMode::Letters ? kLetterRows[row][column]
                                             : kSymbolRows[row][column];
}

UiRect key_rect(size_t index)
{
    const size_t row = index / 10U;
    const size_t column = index % 10U;
    return {kRowStartX[row] + static_cast<int>(column) * kKeyPitch,
            kKeyRowY[row], kKeyWidth, kKeyHeight};
}

void draw_key(Canvas &canvas, const UiRect &rect, const char *label,
              bool filled, int scale = 3)
{
    if (filled) {
        canvas.fill_rect(rect.x, rect.y, rect.width, rect.height,
                         GrayLevel::Black);
    } else {
        canvas.draw_rect(rect.x, rect.y, rect.width, rect.height,
                         GrayLevel::Black);
        canvas.draw_rect(rect.x + 1, rect.y + 1, rect.width - 2,
                         rect.height - 2, GrayLevel::Black);
        // Small shadow for a key-cap feel.
        canvas.fill_rect(rect.x + 2, rect.y + rect.height - 4,
                         rect.width - 4, 2, GrayLevel::Black);
    }
    const GrayLevel ink = filled ? GrayLevel::White : GrayLevel::Black;
    ui_draw_centered_text(canvas, rect.x + rect.width / 2,
                          rect.y + (rect.height - 7 * scale) / 2 + scale,
                          label, scale, ink);
}

// Draws one item's text left-aligned in up to two lines with ellipsis.
void draw_item_text(Canvas &canvas, int x, int y, int width, int height,
                    const char *text, bool done)
{
    int scale = 3;
    Utf8Line lines[3];
    size_t cells = static_cast<size_t>((width + 1) / (6 * scale));
    size_t count = utf8_wrap(text, cells, lines, 2);
    const bool one_line_fits =
        count == 1U && lines[0].offset + lines[0].length >= std::strlen(text);
    if (!one_line_fits) {
        scale = 2;
        cells = static_cast<size_t>((width + 1) / (6 * scale));
        count = utf8_wrap(text, cells, lines, 2);
    }
    const int pitch = 11 * scale;
    const int block = static_cast<int>(count) * pitch - 4 * scale;
    int line_y = y + (height - block) / 2;
    for (size_t i = 0U; i < count; ++i) {
        const char *line = text + lines[i].offset;
        size_t length = lines[i].length;
        const bool last = i + 1U == count;
        const bool truncated =
            last && lines[i].offset + lines[i].length < std::strlen(text);
        char buffer[kTodoTextBytes + 4];
        if (truncated) {
            // Replace the tail with an ellipsis.
            std::snprintf(buffer, sizeof(buffer), "%.*s",
                          static_cast<int>(length), line);
            while (utf8_cell_count(buffer) + 1U > cells) {
                utf8_pop_back(buffer);
            }
            std::strncat(buffer, "\xE2\x80\xA6",
                         sizeof(buffer) - std::strlen(buffer) - 1U);
            line = buffer;
            length = std::strlen(buffer);
        }
        utf8_draw_text(canvas, x, line_y, line, length, scale);
        if (done) {
            const int w = utf8_text_width(line, length, scale);
            canvas.fill_rect(x - 2, line_y + 3 * scale, w + 4, 2,
                             GrayLevel::Black);
        }
        line_y += pitch;
    }
}

void draw_edit_dots(Canvas &canvas, int x, int y)
{
    for (int i = 0; i < 3; ++i) {
        canvas.fill_circle(x + 14 + i * 14, y, 4, GrayLevel::Black);
    }
}

void draw_arrow(Canvas &canvas, const UiRect &rect, bool left)
{
    const int cx = rect.x + rect.width / 2;
    const int cy = rect.y + rect.height / 2;
    for (int i = 0; i < 14; ++i) {
        const int x = left ? cx - 7 + i : cx + 7 - i;
        canvas.fill_rect(x, cy - i, 2, i * 2 + 1, GrayLevel::Black);
    }
}

}  // namespace

size_t todo_page_count(const TodoList &list)
{
    return list.count == 0U
               ? 1U
               : (list.count + kTodoRowsPerPage - 1U) / kTodoRowsPerPage;
}

void todo_page_render_list(Canvas &canvas, const TodoListView &view)
{
    const TodoList &list = *view.list;
    canvas.clear(GrayLevel::White);
    ui_draw_centered_text(canvas, kCenterX, 36, "TO-DO", 5);
    char subtitle[32];
    std::snprintf(subtitle, sizeof(subtitle), "%u OPEN / %u",
                  static_cast<unsigned>(todo_list_open_count(list)),
                  static_cast<unsigned>(list.count));
    ui_draw_centered_text(canvas, kCenterX, 90, subtitle, 2);
    ui_draw_divider(canvas, kCenterX, 200, 116);

    if (list.count == 0U) {
        ui_draw_centered_text(canvas, kCenterX, 300, "NOTHING TO DO!", 3);
        ui_draw_centered_text(canvas, kCenterX, 350,
                              "TAP + ADD TO CREATE A TASK", 2);
    }

    const size_t first = view.page * kTodoRowsPerPage;
    for (size_t row = 0U; row < kTodoRowsPerPage; ++row) {
        const size_t index = first + row;
        if (index >= list.count) {
            break;
        }
        const TodoItem &item = list.items[index];
        const int y = kListTop + static_cast<int>(row) * kRowHeight;
        ui_draw_checkbox(canvas, kCheckboxX,
                         y + (kRowHeight - kCheckboxSize) / 2,
                         kCheckboxSize, item.done != 0U);
        draw_item_text(canvas, kTextX, y, kEditX - kTextX - 8, kRowHeight,
                       item.text, item.done != 0U);
        draw_edit_dots(canvas, kEditX, y + kRowHeight / 2);
        for (int x = 20; x < kWidth - 20; x += 6) {
            canvas.draw_pixel(x, y + kRowHeight - 1, GrayLevel::Black);
        }
    }

    const size_t pages = todo_page_count(list);
    if (pages > 1U) {
        draw_arrow(canvas, kPrevPageRect, true);
        draw_arrow(canvas, kNextPageRect, false);
        char label[16];
        std::snprintf(label, sizeof(label), "%u / %u",
                      static_cast<unsigned>(view.page + 1U),
                      static_cast<unsigned>(pages));
        ui_draw_centered_text(canvas, kCenterX, 622, label, 3);
    }

    ui_draw_button(canvas, kClearDoneRect, "CLEAR DONE", false, 2);
    ui_draw_button(canvas, kAddRect,
                   list.count >= kTodoMaxItems ? "LIST FULL" : "+ ADD",
                   true, 3);
}

void todo_page_render_editor(Canvas &canvas, const TodoEditorView &view)
{
    canvas.clear(GrayLevel::White);
    ui_draw_centered_text(canvas, kCenterX, 30,
                          view.editing_existing ? "EDIT TASK" : "NEW TASK",
                          4);

    for (int i = 0; i < 3; ++i) {
        canvas.draw_rect(kTextBoxRect.x + i, kTextBoxRect.y + i,
                         kTextBoxRect.width - i * 2,
                         kTextBoxRect.height - i * 2, GrayLevel::Black);
    }
    // Text plus a block cursor, left aligned and wrapped.
    char shown[kTodoTextBytes + 2];
    std::snprintf(shown, sizeof(shown), "%s_", view.text);
    constexpr int kScale = 3;
    const size_t cells =
        static_cast<size_t>((kTextBoxRect.width - 32 + 1) / (6 * kScale));
    Utf8Line lines[6];
    const size_t count = utf8_wrap(shown, cells, lines, 6);
    int y = kTextBoxRect.y + 18 + 3 * kScale;
    for (size_t i = 0U; i < count; ++i) {
        utf8_draw_text(canvas, kTextBoxRect.x + 16, y,
                       shown + lines[i].offset, lines[i].length, kScale);
        y += 11 * kScale;
    }
    char counter[16];
    std::snprintf(counter, sizeof(counter), "%u/%u",
                  static_cast<unsigned>(std::strlen(view.text)),
                  static_cast<unsigned>(kTodoTextBytes - 1U));
    utf8_draw_text(canvas,
                   kTextBoxRect.x + kTextBoxRect.width - 14 -
                       utf8_text_width(counter, 2),
                   kTextBoxRect.y + kTextBoxRect.height - 26, counter, 2);

    char buffer[8];
    for (size_t row = 0U; row < 3U; ++row) {
        for (int column = 0; column < kRowKeyCount[row]; ++column) {
            const size_t index = row * 10U + static_cast<size_t>(column);
            draw_key(canvas, key_rect(index),
                     todo_key_text(view.mode, index, view.shift, buffer,
                                   sizeof(buffer)),
                     false);
        }
    }
    draw_key(canvas, kBackspaceRect, "DEL", false, 2);
    draw_key(canvas, kModeRect,
             view.mode == TodoKeyboardMode::Letters ? "123" : "ABC", false,
             2);
    draw_key(canvas, kShiftRect, "SHIFT", view.shift, 2);
    draw_key(canvas, kSpaceRect, "SPACE", false, 2);

    ui_draw_button(canvas, kCancelRect, "CANCEL", false, 2);
    if (view.editing_existing) {
        ui_draw_button(canvas, kDeleteRect, "DELETE", false, 2);
    }
    ui_draw_button(canvas, kSaveRect, "SAVE", true, 3);
}

TodoHit todo_page_list_hit(const TodoList &list, size_t page, int x, int y)
{
    TodoHit hit = {};
    if (kAddRect.contains(x, y)) {
        hit.action = TodoAction::Add;
        return hit;
    }
    if (kClearDoneRect.contains(x, y)) {
        hit.action = TodoAction::ClearDone;
        return hit;
    }
    if (todo_page_count(list) > 1U) {
        if (kPrevPageRect.contains(x, y)) {
            hit.action = TodoAction::PrevPage;
            return hit;
        }
        if (kNextPageRect.contains(x, y)) {
            hit.action = TodoAction::NextPage;
            return hit;
        }
    }
    if (y < kListTop ||
        y >= kListTop + static_cast<int>(kTodoRowsPerPage) * kRowHeight) {
        return hit;
    }
    const size_t row = static_cast<size_t>((y - kListTop) / kRowHeight);
    const size_t index = page * kTodoRowsPerPage + row;
    if (index >= list.count) {
        return hit;
    }
    hit.index = index;
    hit.action = x >= kEditX ? TodoAction::EditItem : TodoAction::ToggleItem;
    return hit;
}

TodoHit todo_page_editor_hit(bool editing_existing,
                             TodoKeyboardMode mode,
                             int x,
                             int y)
{
    TodoHit hit = {};
    for (size_t row = 0U; row < 3U; ++row) {
        for (int column = 0; column < kRowKeyCount[row]; ++column) {
            const size_t index = row * 10U + static_cast<size_t>(column);
            const UiRect rect = key_rect(index);
            // Expand hit boxes over the small gaps between keys.
            const UiRect hit_rect = {rect.x - 2, rect.y - 4, kKeyPitch,
                                     kKeyHeight + 8};
            if (hit_rect.contains(x, y) && key_label(mode, index) != nullptr) {
                hit.action = TodoAction::Key;
                hit.index = index;
                return hit;
            }
        }
    }
    if (kBackspaceRect.contains(x, y)) {
        hit.action = TodoAction::Backspace;
    } else if (kModeRect.contains(x, y)) {
        hit.action = TodoAction::Mode;
    } else if (kShiftRect.contains(x, y)) {
        hit.action = TodoAction::Shift;
    } else if (kSpaceRect.contains(x, y)) {
        hit.action = TodoAction::Space;
    } else if (kCancelRect.contains(x, y)) {
        hit.action = TodoAction::Cancel;
    } else if (editing_existing && kDeleteRect.contains(x, y)) {
        hit.action = TodoAction::Delete;
    } else if (kSaveRect.contains(x, y)) {
        hit.action = TodoAction::Save;
    }
    return hit;
}

const char *todo_key_text(TodoKeyboardMode mode,
                          size_t key_index,
                          bool shift,
                          char *buffer,
                          size_t buffer_size)
{
    const char *label = key_label(mode, key_index);
    if (label == nullptr || buffer == nullptr || buffer_size < 3U) {
        return "";
    }
    std::snprintf(buffer, buffer_size, "%s", label);
    if (!shift) {
        return buffer;
    }
    unsigned char *bytes = reinterpret_cast<unsigned char *>(buffer);
    if (bytes[0] >= 'a' && bytes[0] <= 'z') {
        bytes[0] = static_cast<unsigned char>(bytes[0] - 32U);
    } else if (bytes[0] == 0xC3U && bytes[1] >= 0xA0U && bytes[1] <= 0xBEU &&
               bytes[1] != 0xB7U) {
        // Latin-1 lowercase letters U+00E0..U+00FE map to U+00C0..U+00DE.
        bytes[1] = static_cast<unsigned char>(bytes[1] - 0x20U);
    }
    return buffer;
}

const char *todo_action_name(TodoAction action)
{
    switch (action) {
    case TodoAction::None:
        return "none";
    case TodoAction::ToggleItem:
        return "toggle_item";
    case TodoAction::EditItem:
        return "edit_item";
    case TodoAction::Add:
        return "add";
    case TodoAction::ClearDone:
        return "clear_done";
    case TodoAction::PrevPage:
        return "prev_page";
    case TodoAction::NextPage:
        return "next_page";
    case TodoAction::Key:
        return "key";
    case TodoAction::Shift:
        return "shift";
    case TodoAction::Mode:
        return "mode";
    case TodoAction::Space:
        return "space";
    case TodoAction::Backspace:
        return "backspace";
    case TodoAction::Save:
        return "save";
    case TodoAction::Cancel:
        return "cancel";
    case TodoAction::Delete:
        return "delete";
    }
    return "unknown";
}
