#include "sticky_app_id.h"

const char *sticky_app_id_name(StickyAppId app)
{
    switch (app) {
    case StickyAppId::DesktopPet:
        return "desktop_pet";
    case StickyAppId::Pomodoro:
        return "pomodoro";
    case StickyAppId::StatusBoard:
        return "status_board";
    case StickyAppId::BookOfAnswers:
        return "book_of_answers";
    case StickyAppId::Flashcards:
        return "flashcards";
    case StickyAppId::Todo:
        return "todo";
    }
    return "unknown";
}
