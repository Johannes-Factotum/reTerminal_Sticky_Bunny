#pragma once

enum class StickyAppId {
    DesktopPet,
    Pomodoro,
    StatusBoard,
    BookOfAnswers,
    Flashcards,
    Todo,
};

// Returns the stable English identifier used by launcher logs.
// 返回应用选择日志使用的稳定英文标识。
const char *sticky_app_id_name(StickyAppId app);
