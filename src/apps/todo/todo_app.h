#pragma once

#include "canvas.h"
#include "esp_err.h"

// Touch to-do list with an on-device keyboard (portrait). Saved in NVS.
esp_err_t todo_app_start(Canvas &canvas);
esp_err_t todo_app_pause();
esp_err_t todo_app_resume();
bool todo_app_power_sleep_allowed();
esp_err_t todo_app_prepare_power_sleep();
uint32_t todo_app_power_sleep_timeout_ms();
