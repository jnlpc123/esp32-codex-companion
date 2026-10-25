#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include <nvs_flash.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct settings_context *settings_handle_t;

/**
 * Open a settings namespace.
 * @param ns         NVS namespace name
 * @param read_write true for read-write, false for read-only
 * @return handle on success, NULL on failure
 */
settings_handle_t settings_open(const char *ns, bool read_write);

/**
 * Close the settings handle (commits if dirty).
 */
void settings_close(settings_handle_t handle);

/**
 * Get a string value.
 * @param handle        settings handle
 * @param key           key name
 * @param default_value fallback if key not found
 * @param out           output buffer (caller-allocated)
 * @param out_size      size of output buffer
 * @return out on success, NULL if handle is invalid
 */
char *settings_get_string(settings_handle_t handle, const char *key,
                          const char *default_value, char *out, size_t out_size);

/**
 * Set a string value.
 */
void settings_set_string(settings_handle_t handle, const char *key, const char *value);

/**
 * Get an int32 value.
 */
int32_t settings_get_int(settings_handle_t handle, const char *key, int32_t default_value);

/**
 * Set an int32 value.
 */
void settings_set_int(settings_handle_t handle, const char *key, int32_t value);

/**
 * Get a bool value.
 */
bool settings_get_bool(settings_handle_t handle, const char *key, bool default_value);

/**
 * Set a bool value.
 */
void settings_set_bool(settings_handle_t handle, const char *key, bool value);

/**
 * Erase a single key.
 */
void settings_erase_key(settings_handle_t handle, const char *key);

/**
 * Erase all keys in the namespace.
 */
void settings_erase_all(settings_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SETTINGS_H
