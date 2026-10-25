#include "settings.h"

#include <stdlib.h>
#include <string.h>
#include <esp_log.h>
#include <nvs_flash.h>

#define TAG "Settings"
#define NS_MAX_LEN 16

struct settings_context {
    char ns[NS_MAX_LEN];
    nvs_handle_t nvs_handle;
    bool read_write;
    bool dirty;
};

settings_handle_t settings_open(const char *ns, bool read_write) {
    settings_handle_t handle = (settings_handle_t)calloc(1, sizeof(struct settings_context));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate settings handle");
        return NULL;
    }

    strncpy(handle->ns, ns, NS_MAX_LEN - 1);
    handle->ns[NS_MAX_LEN - 1] = '\0';
    handle->read_write = read_write;
    handle->dirty = false;

    esp_err_t err = nvs_open(ns, read_write ? NVS_READWRITE : NVS_READONLY,
                             &handle->nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace '%s': %s", ns, esp_err_to_name(err));
        handle->nvs_handle = 0;
    }

    return handle;
}

void settings_close(settings_handle_t handle) {
    if (!handle) return;

    if (handle->nvs_handle != 0) {
        if (handle->read_write && handle->dirty) {
            nvs_commit(handle->nvs_handle);
        }
        nvs_close(handle->nvs_handle);
    }
    free(handle);
}

char *settings_get_string(settings_handle_t handle, const char *key,
                          const char *default_value, char *out, size_t out_size) {
    if (!handle || handle->nvs_handle == 0) {
        if (default_value) {
            strncpy(out, default_value, out_size - 1);
            out[out_size - 1] = '\0';
        } else {
            out[0] = '\0';
        }
        return out;
    }

    size_t length = 0;
    if (nvs_get_str(handle->nvs_handle, key, NULL, &length) != ESP_OK) {
        if (default_value) {
            strncpy(out, default_value, out_size - 1);
            out[out_size - 1] = '\0';
        } else {
            out[0] = '\0';
        }
        return out;
    }

    if (length > out_size) length = out_size;
    nvs_get_str(handle->nvs_handle, key, out, &length);

    /* Strip trailing nulls */
    while (length > 0 && out[length - 1] == '\0') {
        length--;
    }
    out[length] = '\0';

    return out;
}

void settings_set_string(settings_handle_t handle, const char *key, const char *value) {
    if (!handle) return;
    if (handle->read_write && handle->nvs_handle != 0) {
        nvs_set_str(handle->nvs_handle, key, value);
        handle->dirty = true;
    } else {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", handle->ns);
    }
}

int32_t settings_get_int(settings_handle_t handle, const char *key, int32_t default_value) {
    if (!handle || handle->nvs_handle == 0) {
        return default_value;
    }

    int32_t value;
    if (nvs_get_i32(handle->nvs_handle, key, &value) != ESP_OK) {
        return default_value;
    }
    return value;
}

void settings_set_int(settings_handle_t handle, const char *key, int32_t value) {
    if (!handle) return;
    if (handle->read_write && handle->nvs_handle != 0) {
        nvs_set_i32(handle->nvs_handle, key, value);
        handle->dirty = true;
    } else {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", handle->ns);
    }
}

bool settings_get_bool(settings_handle_t handle, const char *key, bool default_value) {
    if (!handle || handle->nvs_handle == 0) {
        return default_value;
    }

    uint8_t value;
    if (nvs_get_u8(handle->nvs_handle, key, &value) != ESP_OK) {
        return default_value;
    }
    return value != 0;
}

void settings_set_bool(settings_handle_t handle, const char *key, bool value) {
    if (!handle) return;
    if (handle->read_write && handle->nvs_handle != 0) {
        nvs_set_u8(handle->nvs_handle, key, value ? 1 : 0);
        handle->dirty = true;
    } else {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", handle->ns);
    }
}

void settings_erase_key(settings_handle_t handle, const char *key) {
    if (!handle) return;
    if (handle->read_write && handle->nvs_handle != 0) {
        esp_err_t ret = nvs_erase_key(handle->nvs_handle, key);
        if (ret != ESP_ERR_NVS_NOT_FOUND && ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase key '%s': %s", key, esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", handle->ns);
    }
}

void settings_erase_all(settings_handle_t handle) {
    if (!handle) return;
    if (handle->read_write && handle->nvs_handle != 0) {
        nvs_erase_all(handle->nvs_handle);
    } else {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", handle->ns);
    }
}
