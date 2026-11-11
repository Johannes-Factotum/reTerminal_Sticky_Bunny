#include <array>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <utility>
#include <vector>

#include "app_pages.h"
#include "app_launcher_assets.h"
#include "canvas.h"

namespace {

constexpr uint16_t kWidth = 800U;
constexpr uint16_t kHeight = 480U;
constexpr size_t kStride = kWidth / 4U;

uint8_t pixel_level(const std::vector<uint8_t> &buffer, int x, int y)
{
    const size_t index = static_cast<size_t>(y) * kStride +
                         static_cast<size_t>(x) / 4U;
    const uint8_t shift = static_cast<uint8_t>((3 - (x & 0x03)) * 2);
    return static_cast<uint8_t>((buffer[index] >> shift) & 0x03U);
}

uint8_t logical_pixel_level(const std::vector<uint8_t> &buffer,
                            CanvasRotation rotation,
                            int x,
                            int y)
{
    int physical_x = x;
    int physical_y = y;
    if (rotation == CanvasRotation::Deg90CounterClockwise) {
        physical_x = y;
        physical_y = static_cast<int>(kHeight) - 1 - x;
    }
    return pixel_level(buffer, physical_x, physical_y);
}

// Verifies that visible label pixels have balanced top and bottom margins.
// 验证标签文字的可见像素拥有均衡的上下留白。
void assert_label_text_centered(const std::vector<uint8_t> &buffer,
                                CanvasRotation rotation,
                                int x,
                                int y,
                                int width,
                                int height,
                                GrayLevel ink_level)
{
    int first_ink_y = -1;
    int last_ink_y = -1;
    for (int logical_y = y + 1; logical_y < y + height - 1;
         ++logical_y) {
        for (int logical_x = x + 7; logical_x < x + width - 7;
             ++logical_x) {
            if (logical_pixel_level(buffer, rotation,
                                    logical_x, logical_y) !=
                static_cast<uint8_t>(ink_level)) {
                continue;
            }
            if (first_ink_y < 0) {
                first_ink_y = logical_y;
            }
            last_ink_y = logical_y;
        }
    }
    assert(first_ink_y >= 0);
    const int top_margin = first_ink_y - y;
    const int bottom_margin = y + height - 1 - last_ink_y;
    const int difference = top_margin >= bottom_margin
                               ? top_margin - bottom_margin
                               : bottom_margin - top_margin;
    assert(difference <= 1);
}

// Measures the complete visible composition instead of isolated components.
// 测量整组可见内容，而不是只检查其中某个局部元素。
void assert_content_vertically_centered(
    const std::vector<uint8_t> &buffer,
    CanvasRotation rotation,
    int width,
    int height)
{
    int first_ink_y = -1;
    int last_ink_y = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (logical_pixel_level(buffer, rotation, x, y) ==
                static_cast<uint8_t>(GrayLevel::White)) {
                continue;
            }
            if (first_ink_y < 0) {
                first_ink_y = y;
            }
            last_ink_y = y;
        }
    }
    assert(first_ink_y >= 0);
    const int top_margin = first_ink_y;
    const int bottom_margin = height - 1 - last_ink_y;
    const int difference = top_margin >= bottom_margin
                               ? top_margin - bottom_margin
                               : bottom_margin - top_margin;
    assert(difference <= 8);
}

void write_preview(const std::vector<uint8_t> &buffer, const char *path)
{
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << kWidth << " " << kHeight << "\n255\n";
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const uint8_t value =
                static_cast<uint8_t>(pixel_level(buffer, x, y) * 85U);
            output.write(reinterpret_cast<const char *>(&value), 1);
            output.write(reinterpret_cast<const char *>(&value), 1);
            output.write(reinterpret_cast<const char *>(&value), 1);
        }
    }
}

size_t count_level(const std::vector<uint8_t> &buffer, GrayLevel level)
{
    size_t count = 0U;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (pixel_level(buffer, x, y) ==
                static_cast<uint8_t>(level)) {
                ++count;
            }
        }
    }
    return count;
}

bool asset_pixel_set(const PixelAsset &asset, int x, int y)
{
    const size_t stride = (asset.width + 7U) / 8U;
    const size_t index = static_cast<size_t>(y) * stride +
                         static_cast<size_t>(x) / 8U;
    const uint8_t mask = static_cast<uint8_t>(
        1U << (7U - static_cast<uint8_t>(x & 0x07)));
    return (asset.data[index] & mask) != 0U;
}

AppLauncherAssetId launcher_asset_id(StickyAppId app)
{
    switch (app) {
    case StickyAppId::DesktopPet:
        return AppLauncherAssetId::Pet;
    case StickyAppId::Pomodoro:
        return AppLauncherAssetId::Focus;
    case StickyAppId::StatusBoard:
        return AppLauncherAssetId::Status;
    case StickyAppId::BookOfAnswers:
        return AppLauncherAssetId::Answers;
    case StickyAppId::Flashcards:
        return AppLauncherAssetId::Cards;
    case StickyAppId::Todo:
        return AppLauncherAssetId::Todo;
    }
    return AppLauncherAssetId::Pet;
}

std::pair<int, int> selection_probe(const AppLauncherStickerAsset &asset)
{
    // Uses the right-side outer ring, away from the separate corner marker.
    // 选取贴纸右侧的外圈像素，避开独立绘制的左上角选择标记。
    for (int x = static_cast<int>(asset.selection.width) - 1;
         x >= static_cast<int>(asset.selection.width) / 2;
         --x) {
        for (int y = 0; y < static_cast<int>(asset.selection.height); ++y) {
            if (asset_pixel_set(asset.selection, x, y) &&
                !asset_pixel_set(asset.gray, x, y) &&
                !asset_pixel_set(asset.black, x, y)) {
                return {x, y};
            }
        }
    }
    assert(false);
    return {0, 0};
}

void assert_selection_follows_current_app(
    Canvas &canvas,
    const std::vector<uint8_t> &buffer,
    CanvasRotation rotation,
    const std::array<std::pair<int, int>, 4> &asset_origins)
{
    constexpr std::array<StickyAppId, 4> kApps = {
        StickyAppId::DesktopPet,
        StickyAppId::Pomodoro,
        StickyAppId::Flashcards,
        StickyAppId::Todo,
    };

    for (size_t selected_index = 0; selected_index < kApps.size();
         ++selected_index) {
        app_page_render_launcher(canvas, kApps[selected_index]);
        for (size_t card_index = 0; card_index < kApps.size(); ++card_index) {
            const AppLauncherStickerAsset &asset =
                app_launcher_sticker_asset(
                    launcher_asset_id(kApps[card_index]));
            const auto probe = selection_probe(asset);
            const int x = asset_origins[card_index].first + probe.first;
            const int y = asset_origins[card_index].second + probe.second;
            const GrayLevel expected = card_index == selected_index
                                           ? GrayLevel::Black
                                           : GrayLevel::White;
            assert(logical_pixel_level(buffer, rotation, x, y) ==
                   static_cast<uint8_t>(expected));
        }
    }
}

}  // namespace

int main()
{
    std::vector<uint8_t> buffer(kStride * kHeight, 0xFFU);
    Canvas canvas(kWidth, kHeight, buffer.data(), buffer.size());

#if STICKY_ONBOARDING_TEST_MODE
    assert(app_page_launcher_tutorial_at(480, 800, 455, 112));
    assert(app_page_launcher_tutorial_at(800, 480, 775, 74));
    assert(!app_page_launcher_tutorial_at(480, 800, 240, 112));
#endif
    StickyAppId selected_app = StickyAppId::DesktopPet;

    canvas.set_rotation(CanvasRotation::Deg90CounterClockwise);
    app_page_render_launcher(canvas, StickyAppId::DesktopPet);
    assert(canvas.rotation() == CanvasRotation::Deg90CounterClockwise);
    assert(count_level(buffer, GrayLevel::Black) > 7000U);
    assert(count_level(buffer, GrayLevel::LightGray) > 2000U);
    assert(app_page_launcher_app_at(
        canvas.width(), canvas.height(), 132, 331, selected_app));
    assert(selected_app == StickyAppId::DesktopPet);
    assert(app_page_launcher_app_at(
        canvas.width(), canvas.height(), 348, 331, selected_app));
    assert(selected_app == StickyAppId::Pomodoro);
    assert(app_page_launcher_app_at(
        canvas.width(), canvas.height(), 132, 581, selected_app));
    assert(selected_app == StickyAppId::Flashcards);
    assert(app_page_launcher_app_at(
        canvas.width(), canvas.height(), 348, 581, selected_app));
    assert(selected_app == StickyAppId::Todo);
    assert(!app_page_launcher_app_at(
        canvas.width(), canvas.height(), 240, 400, selected_app));
    assert(!app_page_launcher_app_at(
        canvas.width(), canvas.height(), 132, 451, selected_app));
    assert_label_text_centered(
        buffer, CanvasRotation::Deg90CounterClockwise,
        31, 394, 190, 54, GrayLevel::White);
    assert_label_text_centered(
        buffer, CanvasRotation::Deg90CounterClockwise,
        259, 394, 190, 54, GrayLevel::Black);
    assert_content_vertically_centered(
        buffer, CanvasRotation::Deg90CounterClockwise,
        canvas.width(), canvas.height());
    assert_selection_follows_current_app(
        canvas,
        buffer,
        CanvasRotation::Deg90CounterClockwise,
        {{{38, 204}, {266, 204}, {38, 464}, {266, 464}}});
    app_page_render_launcher(canvas, StickyAppId::DesktopPet);
    write_preview(buffer, "/tmp/sticky_launcher_portrait.ppm");

    canvas.set_rotation(CanvasRotation::Deg0);
    app_page_render_launcher(canvas, StickyAppId::DesktopPet);
    assert(canvas.rotation() == CanvasRotation::Deg0);
    assert(count_level(buffer, GrayLevel::Black) > 7000U);
    assert(count_level(buffer, GrayLevel::LightGray) > 2000U);
    assert(app_page_launcher_app_at(
        canvas.width(), canvas.height(), 109, 286, selected_app));
    assert(selected_app == StickyAppId::DesktopPet);
    assert(app_page_launcher_app_at(
        canvas.width(), canvas.height(), 303, 286, selected_app));
    assert(selected_app == StickyAppId::Pomodoro);
    assert(app_page_launcher_app_at(
        canvas.width(), canvas.height(), 497, 286, selected_app));
    assert(selected_app == StickyAppId::Flashcards);
    assert(app_page_launcher_app_at(
        canvas.width(), canvas.height(), 691, 286, selected_app));
    assert(selected_app == StickyAppId::Todo);
    assert(!app_page_launcher_app_at(
        canvas.width(), canvas.height(), 206, 286, selected_app));
    assert_label_text_centered(
        buffer, CanvasRotation::Deg0,
        18, 366, 176, 54, GrayLevel::White);
    assert_label_text_centered(
        buffer, CanvasRotation::Deg0,
        214, 366, 176, 54, GrayLevel::Black);
    assert_content_vertically_centered(
        buffer, CanvasRotation::Deg0,
        canvas.width(), canvas.height());
    assert_selection_follows_current_app(
        canvas,
        buffer,
        CanvasRotation::Deg0,
        {{{18, 176}, {214, 176}, {410, 176}, {606, 176}}});
    app_page_render_launcher(canvas, StickyAppId::DesktopPet);
    write_preview(buffer, "/tmp/sticky_launcher_landscape.ppm");
    return 0;
}
