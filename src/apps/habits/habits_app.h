#pragma once

#include "canvas.h"
#include "esp_err.h"

// Morning-routine habit pages with phone editing over Wi-Fi (portrait).
esp_err_t habits_app_start(Canvas &canvas);
esp_err_t habits_app_pause();
esp_err_t habits_app_resume();
bool habits_app_power_sleep_allowed();
esp_err_t habits_app_prepare_power_sleep();
uint32_t habits_app_power_sleep_timeout_ms();
