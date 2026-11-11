#include "sticky_app_display_orientation.h"

bool sticky_app_display_rotation(StickyAppId app,
                                 StickyImuOrientation orientation,
                                 CanvasRotation &rotation)
{
    switch (app) {
    case StickyAppId::Pomodoro:
        if (orientation == StickyImuOrientation::Landscape0) {
            rotation = CanvasRotation::Deg90CounterClockwise;
            return true;
        }
        if (orientation == StickyImuOrientation::Landscape180) {
            rotation = CanvasRotation::Deg90Clockwise;
            return true;
        }
        return false;
    case StickyAppId::StatusBoard:
        if (orientation == StickyImuOrientation::Portrait0) {
            rotation = CanvasRotation::Deg180;
            return true;
        }
        if (orientation == StickyImuOrientation::Portrait180) {
            rotation = CanvasRotation::Deg0;
            return true;
        }
        return false;
    case StickyAppId::DesktopPet:
    case StickyAppId::BookOfAnswers:
    case StickyAppId::Flashcards:
    case StickyAppId::Todo:
        return false;
    }
    return false;
}

bool sticky_app_launcher_rotation(StickyImuOrientation orientation,
                                  CanvasRotation &rotation)
{
    switch (orientation) {
    case StickyImuOrientation::Landscape0:
        rotation = CanvasRotation::Deg90CounterClockwise;
        return true;
    case StickyImuOrientation::Landscape180:
        rotation = CanvasRotation::Deg90Clockwise;
        return true;
    case StickyImuOrientation::Portrait0:
        rotation = CanvasRotation::Deg180;
        return true;
    case StickyImuOrientation::Portrait180:
        rotation = CanvasRotation::Deg0;
        return true;
    case StickyImuOrientation::Unknown:
    case StickyImuOrientation::FaceUp:
    case StickyImuOrientation::FaceDown:
        return false;
    }
    return false;
}

const char *sticky_app_display_rotation_name(CanvasRotation rotation)
{
    switch (rotation) {
    case CanvasRotation::Deg0:
        return "0";
    case CanvasRotation::Deg90Clockwise:
        return "90_clockwise";
    case CanvasRotation::Deg180:
        return "180";
    case CanvasRotation::Deg90CounterClockwise:
        return "90_counter_clockwise";
    }
    return "unknown";
}
