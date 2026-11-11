#pragma once

#include <cstdint>

#include "esp_err.h"
#include "sticky_buzzer_pattern.h"

// Initializes the onboard buzzer in a silent state.
// 初始化板载蜂鸣器，并保持静音状态。
esp_err_t sticky_buzzer_init();

// Starts the repeating Pomodoro jingle; it plays until stop is called.
// 启动循环三音提醒，直到调用停止函数才结束。
esp_err_t sticky_buzzer_start_alarm();

// Plays one gentle rising three-note hatching chime in the background.
// 在后台播放一次轻柔的三音上行孵化提示。
esp_err_t sticky_buzzer_play_hatch_chime();

// Plays one short sleep confirmation tone and waits for it to finish.
// 播放一声短促的休眠确认音，并等待声音结束。
esp_err_t sticky_buzzer_play_power_sleep_chime();

// Plays one state-selected pet pattern in the background.
// 在后台播放一个由宠物状态选出的声音模式。
esp_err_t sticky_buzzer_play_pattern(StickyBuzzerPattern pattern,
                                     uint8_t variant = 0U);

// Stops the active alarm or pet sound immediately.
// 立即停止当前响铃或宠物声音。
esp_err_t sticky_buzzer_stop();
