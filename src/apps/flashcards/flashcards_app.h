#pragma once

#include "canvas.h"
#include "esp_err.h"

// Spaced-repetition flashcards (portrait). Progress is saved to flash.
esp_err_t flashcards_app_start(Canvas &canvas);
esp_err_t flashcards_app_pause();
esp_err_t flashcards_app_resume();
bool flashcards_app_power_sleep_allowed();
esp_err_t flashcards_app_prepare_power_sleep();
uint32_t flashcards_app_power_sleep_timeout_ms();
