#include "draftling_pages.h"

#include <cstdio>

#include "app_launcher_assets.h"
#include "canvas.h"
#include "pixel_asset.h"
#include "ui_widgets.h"

namespace {

constexpr int kCenterX = 240;
constexpr UiRect kOpenRect = {40, 600, 400, 80};
constexpr UiRect kBackRect = {90, 704, 300, 60};

void draw_sticker(Canvas &canvas, int cx, int top)
{
    const AppLauncherStickerAsset &asset =
        app_launcher_sticker_asset(AppLauncherAssetId::Draftling);
    const int x = cx - static_cast<int>(asset.black.width);
    // Twice the launcher size; gray dots and black ink layers.
    pixel_asset_draw(canvas, x, top, asset.gray, 2, GrayLevel::LightGray);
    pixel_asset_draw(canvas, x, top, asset.black, 2, GrayLevel::Black);
}

}  // namespace

void draftling_page_render(Canvas &canvas, const DraftlingView &view)
{
    canvas.clear(GrayLevel::White);
    ui_draw_centered_text(canvas, kCenterX, 40, "DRAFTLING", 5);
    ui_draw_divider(canvas, kCenterX, 160, 96);
    draw_sticker(canvas, kCenterX, 116);

    if (!view.installed) {
        const UiRect text = {30, 390, 420, 190};
        ui_draw_fitted_text(
            canvas, text,
            "Draftling is not installed yet. Build it with "
            "tools/build_draftling.sh and flash draftling.bin to 0x800000 "
            "(see README).",
            3, 2);
        ui_draw_button(canvas, kOpenRect, "BACK", true, 3);
        return;
    }

    const UiRect text = {30, 384, 420, 150};
    ui_draw_fitted_text(
        canvas, text,
        "Distraction-free writing with a Bluetooth keyboard. The Sticky "
        "restarts into Draftling.",
        3, 2);
    ui_draw_centered_text(canvas, kCenterX, 540,
                          "TO COME BACK: HOLD UP + DOWN", 2);
    ui_draw_centered_text(canvas, kCenterX, 566,
                          "FOR 2 S, OR F1 > BACK TO BUNNY", 2);
    if (view.version != nullptr && view.version[0] != '\0') {
        char line[48];
        std::snprintf(line, sizeof(line), "VERSION %s", view.version);
        ui_draw_centered_text(canvas, kCenterX, 776, line, 1);
    }
    ui_draw_button(canvas, kOpenRect, "OPEN DRAFTLING", true, 3);
    ui_draw_button(canvas, kBackRect, "BACK", false, 2);
}

DraftlingAction draftling_page_action_at(bool installed, int x, int y)
{
    if (kOpenRect.contains(x, y)) {
        return installed ? DraftlingAction::Open : DraftlingAction::Back;
    }
    if (installed && kBackRect.contains(x, y)) {
        return DraftlingAction::Back;
    }
    return DraftlingAction::None;
}
