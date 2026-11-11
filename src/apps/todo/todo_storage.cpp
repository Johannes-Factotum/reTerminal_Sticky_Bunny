#include "todo_storage.h"

#include <cstring>

#include "nvs.h"

namespace {

constexpr char kNamespace[] = "todo";
constexpr char kKey[] = "list";

TodoRecord s_record;

}  // namespace

esp_err_t todo_storage_load(TodoList &list)
{
    todo_list_clear(list);
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (result != ESP_OK) {
        return result == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : result;
    }
    size_t size = sizeof(s_record);
    result = nvs_get_blob(handle, kKey, &s_record, &size);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (result != ESP_OK) {
        return result;
    }
    if (size != sizeof(s_record) || !todo_record_valid(s_record)) {
        return ESP_ERR_INVALID_CRC;
    }
    list = s_record.list;
    return ESP_OK;
}

esp_err_t todo_storage_save(const TodoList &list)
{
    std::memset(&s_record, 0, sizeof(s_record));
    s_record.list = list;
    todo_record_seal(s_record);
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_blob(handle, kKey, &s_record, sizeof(s_record));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}
