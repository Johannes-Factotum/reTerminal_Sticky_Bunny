#pragma once

#include "canvas.h"
#include "esp_err.h"

// Launcher entry that reboots into the Draftling firmware in slot ota_1.
esp_err_t draftling_app_start(Canvas &canvas);
esp_err_t draftling_app_pause();
esp_err_t draftling_app_resume();
uint32_t draftling_app_power_sleep_timeout_ms();
esp_err_t draftling_app_prepare_power_sleep();

// Returns true once the user tapped BACK; the launcher then goes home.
bool draftling_app_take_exit_request();
