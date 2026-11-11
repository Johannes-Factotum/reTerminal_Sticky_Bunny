#pragma once

#include "sticky_app_id.h"

class Canvas;

// Draws the four touch-selectable application cards (Pet, Pomodoro, Cards,
// To-Do) in the current rotation. Status Board and Book of Answers stay
// reachable by rotating or shaking the device while the launcher is open.
// 在当前屏幕方向绘制四张可触摸选择的应用卡片。
void app_page_render_launcher(Canvas &canvas, StickyAppId current_app);

// Maps one logical touch coordinate to the card drawn at the same position.
// 将逻辑触摸坐标映射到同一位置绘制的应用卡片。
bool app_page_launcher_app_at(int width,
                              int height,
                              int x,
                              int y,
                              StickyAppId &selected_app);

// Maps the Debug-only guide icon while keeping a generous invisible hit area.
// 映射仅Debug版显示的教程图标，并保留较大的透明触发区域。
bool app_page_launcher_tutorial_at(int width, int height, int x, int y);
