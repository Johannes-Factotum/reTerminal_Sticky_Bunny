#pragma once

class Canvas;

// Hand-off page shown before the device reboots into the Draftling
// writerdeck firmware stored in the second app slot.

enum class DraftlingAction {
    None,
    Open,
    Back,
};

struct DraftlingView {
    bool installed;
    const char *version;  // Draftling app version, when installed
};

void draftling_page_render(Canvas &canvas, const DraftlingView &view);
DraftlingAction draftling_page_action_at(bool installed, int x, int y);
