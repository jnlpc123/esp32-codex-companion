#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "task_status.h"
#include "lamp_effects.h"

typedef enum { VIBE_IDLE, VIBE_WORKING, VIBE_WAITING, VIBE_DONE } vibe_state_t;
typedef enum { IDLE_PLAYBACK_OFF, IDLE_PLAYBACK_COLORS, IDLE_PLAYBACK_BLACK } idle_playback_t;
typedef struct {
    int volume, foreground_size, brightness;
    bool muted, all_screens;
    char media[3][48];
    task_scope_t scope;
    int colors[3];
    idle_playback_t idle_playback;
    int idle_done_delay;
    bool effects_enabled, effects_linked;
    light_effect_t effect;
    int effect_period;
    animation_link_t animation_link;
} companion_config_t;
typedef struct {
    uint16_t background;
    int foreground_size;
    uint32_t revision;
    int count;
    bool active;
    uint16_t base_background;
    animation_link_t animation_link;
    uint64_t animation_time_us;
} companion_visual_t;

esp_err_t companion_init(void);
void companion_get_config(companion_config_t *config);
void companion_restore_media(void);
void companion_get_visual(int channel, companion_visual_t *visual);
void companion_tick(void);
// Same validated command service for USB and local configuration HTTP.
esp_err_t companion_handle_json(const char *input, bool usb, char *output, size_t size);
esp_err_t companion_usb_start(void);
