#pragma once

#include <cstdint>

#include "pixel_asset.h"

enum class AppLauncherAssetId : uint8_t {
    Pet,
    Focus,
    Status,
    Answers,
    Cards,
    Todo,
};

struct AppLauncherStickerAsset {
    PixelAsset selection;
    PixelAsset gray;
    PixelAsset black;
};

// Returns the selection ring and both grayscale illustration layers.
// 返回应用选择贴纸的选中外圈、浅灰层与黑色层。
const AppLauncherStickerAsset &app_launcher_sticker_asset(
    AppLauncherAssetId id);
