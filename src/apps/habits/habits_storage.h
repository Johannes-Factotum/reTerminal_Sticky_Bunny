#pragma once

#include "esp_err.h"
#include "habits_model.h"

// NVS persistence. Load falls back to the example routine.
void habits_storage_load(HabitsConfig &config, HabitsProgress &progress);
esp_err_t habits_storage_save_config(const HabitsConfig &config);
esp_err_t habits_storage_save_progress(const HabitsProgress &progress);
