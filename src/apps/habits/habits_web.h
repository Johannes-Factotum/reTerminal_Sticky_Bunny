#pragma once

#include <cstddef>

#include "esp_err.h"
#include "habits_model.h"

// Wi-Fi hotspot plus a tiny web server so the routine can be edited from
// a phone browser. Only runs while the "Edit on phone" page is open.

// Starts the access point and HTTP server. `current` is served on GET;
// fills in the generated SSID and password for the QR codes.
esp_err_t habits_web_start(const HabitsConfig &current,
                           char *ssid, size_t ssid_size,
                           char *password, size_t password_size);
void habits_web_stop();

// True while at least one phone is connected to the hotspot.
bool habits_web_phone_connected();

// Takes a config saved from the phone, if one arrived since the last call.
bool habits_web_take_saved(HabitsConfig &out);

// Updates the copy served on GET.
void habits_web_publish(const HabitsConfig &current);

// The editor page (generated from assets/habits/web/index.html).
extern const char kHabitsWebPage[];
