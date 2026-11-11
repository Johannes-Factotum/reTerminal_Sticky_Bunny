#pragma once

#include "esp_err.h"
#include "todo_list.h"

// Loads the saved list from NVS; returns ESP_ERR_NOT_FOUND for a new list.
esp_err_t todo_storage_load(TodoList &list);
esp_err_t todo_storage_save(const TodoList &list);
