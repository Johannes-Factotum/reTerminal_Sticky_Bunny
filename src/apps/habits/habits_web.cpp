#include "habits_web.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "app_log.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

constexpr char kTag[] = "habits_web";
constexpr size_t kMaxBody = 4096U;
constexpr size_t kJsonSize = 3072U;

bool s_netif_ready = false;
bool s_running = false;
httpd_handle_t s_server = nullptr;
esp_event_handler_instance_t s_wifi_handler = nullptr;
SemaphoreHandle_t s_lock = nullptr;
std::atomic<int> s_stations{0};
std::atomic<bool> s_saved_pending{false};
HabitsConfig s_published = {};
HabitsConfig s_saved = {};
char s_json[kJsonSize];
char s_body[kMaxBody + 1U];

void wifi_event(void *, esp_event_base_t, int32_t id, void *)
{
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        ++s_stations;
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED && s_stations > 0) {
        --s_stations;
    }
}

esp_err_t get_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kHabitsWebPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t get_habits(httpd_req_t *req)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    habits_to_json(s_published, s_json, sizeof(s_json));
    xSemaphoreGive(s_lock);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t post_habits(httpd_req_t *req)
{
    if (req->content_len == 0U || req->content_len > kMaxBody) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad size");
    }
    size_t received = 0U;
    while (received < req->content_len) {
        const int n = httpd_req_recv(req, s_body + received,
                                     req->content_len - received);
        if (n <= 0) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv");
        }
        received += static_cast<size_t>(n);
    }
    s_body[received] = '\0';
    HabitsConfig parsed = {};
    if (!habits_from_json(s_body, received, parsed) ||
        !habits_sanitize(parsed)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "invalid routine");
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_saved = parsed;
    s_published = parsed;
    xSemaphoreGive(s_lock);
    s_saved_pending = true;
    STICKY_LOGI(kTag, "habits=web saved habits=%u",
                static_cast<unsigned>(parsed.count));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// Any other URL (captive-portal probes etc.) redirects to the editor.
esp_err_t redirect(httpd_req_t *req, httpd_err_code_t)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, nullptr, 0);
}

void make_credentials(char *ssid, size_t ssid_size, char *password,
                      size_t password_size)
{
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    std::snprintf(ssid, ssid_size, "Sticky-Habits-%02X%02X", mac[4], mac[5]);
    static const char kAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
    const size_t length = password_size - 1U < 10U ? password_size - 1U : 10U;
    for (size_t i = 0U; i < length; ++i) {
        password[i] = kAlphabet[esp_random() % (sizeof(kAlphabet) - 1U)];
    }
    password[length] = '\0';
}

}  // namespace

esp_err_t habits_web_start(const HabitsConfig &current, char *ssid,
                           size_t ssid_size, char *password,
                           size_t password_size)
{
    app_log_register_tag(kTag);
    if (s_running) {
        return ESP_OK;
    }
    if (s_lock == nullptr) {
        s_lock = xSemaphoreCreateMutex();
    }
    habits_web_publish(current);
    make_credentials(ssid, ssid_size, password, password_size);

    esp_err_t result = ESP_OK;
    if (!s_netif_ready) {
        result = esp_netif_init();
        if (result != ESP_OK) {
            return result;
        }
        result = esp_event_loop_create_default();
        if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
            return result;
        }
        esp_netif_create_default_wifi_ap();
        s_netif_ready = true;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init);
    if (result != ESP_OK) {
        return result;
    }
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        wifi_event, nullptr, &s_wifi_handler);
    wifi_config_t ap = {};
    std::strncpy(reinterpret_cast<char *>(ap.ap.ssid), ssid,
                 sizeof(ap.ap.ssid) - 1U);
    ap.ap.ssid_len = static_cast<uint8_t>(std::strlen(ssid));
    std::strncpy(reinterpret_cast<char *>(ap.ap.password), password,
                 sizeof(ap.ap.password) - 1U);
    ap.ap.channel = 6;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    result = esp_wifi_set_mode(WIFI_MODE_AP);
    if (result == ESP_OK) {
        result = esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    if (result == ESP_OK) {
        result = esp_wifi_start();
    }
    if (result != ESP_OK) {
        esp_wifi_deinit();
        return result;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    result = httpd_start(&s_server, &config);
    if (result != ESP_OK) {
        esp_wifi_stop();
        esp_wifi_deinit();
        return result;
    }
    const httpd_uri_t page = {"/", HTTP_GET, get_page, nullptr};
    const httpd_uri_t api_get = {"/api/habits", HTTP_GET, get_habits,
                                 nullptr};
    const httpd_uri_t api_post = {"/api/habits", HTTP_POST, post_habits,
                                  nullptr};
    httpd_register_uri_handler(s_server, &page);
    httpd_register_uri_handler(s_server, &api_get);
    httpd_register_uri_handler(s_server, &api_post);
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, redirect);
    s_stations = 0;
    s_running = true;
    STICKY_LOGI(kTag, "habits=web started ssid=%s", ssid);
    return ESP_OK;
}

void habits_web_stop()
{
    if (!s_running) {
        return;
    }
    httpd_stop(s_server);
    s_server = nullptr;
    if (s_wifi_handler != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              s_wifi_handler);
        s_wifi_handler = nullptr;
    }
    esp_wifi_stop();
    esp_wifi_deinit();
    s_stations = 0;
    s_running = false;
    STICKY_LOGI(kTag, "habits=web stopped");
}

bool habits_web_phone_connected()
{
    return s_stations > 0;
}

bool habits_web_take_saved(HabitsConfig &out)
{
    if (!s_saved_pending.exchange(false)) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out = s_saved;
    xSemaphoreGive(s_lock);
    return true;
}

void habits_web_publish(const HabitsConfig &current)
{
    if (s_lock == nullptr) {
        s_lock = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_published = current;
    xSemaphoreGive(s_lock);
}
