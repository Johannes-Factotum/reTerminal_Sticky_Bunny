#include "habits_storage.h"

#include <cstring>

#include "nvs.h"

namespace {

constexpr char kNamespace[] = "habits";
constexpr char kConfigKey[] = "config";
constexpr char kProgressKey[] = "progress";

HabitsConfigRecord s_config_record;
HabitsProgressRecord s_progress_record;

template <typename T>
bool read_blob(nvs_handle_t handle, const char *key, T &record)
{
    size_t size = sizeof(record);
    return nvs_get_blob(handle, key, &record, &size) == ESP_OK &&
           size == sizeof(record) && habits_valid(record);
}

template <typename T>
esp_err_t write_blob(const char *key, T &record)
{
    habits_seal(record);
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_blob(handle, key, &record, sizeof(record));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

}  // namespace

void habits_storage_load(HabitsConfig &config, HabitsProgress &progress)
{
    habits_default_config(config);
    std::memset(&progress, 0, sizeof(progress));
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    if (read_blob(handle, kConfigKey, s_config_record) &&
        s_config_record.config.count > 0U) {
        config = s_config_record.config;
    }
    if (read_blob(handle, kProgressKey, s_progress_record)) {
        progress = s_progress_record.progress;
    }
    nvs_close(handle);
}

esp_err_t habits_storage_save_config(const HabitsConfig &config)
{
    std::memset(&s_config_record, 0, sizeof(s_config_record));
    s_config_record.config = config;
    return write_blob(kConfigKey, s_config_record);
}

esp_err_t habits_storage_save_progress(const HabitsProgress &progress)
{
    std::memset(&s_progress_record, 0, sizeof(s_progress_record));
    s_progress_record.progress = progress;
    return write_blob(kProgressKey, s_progress_record);
}
