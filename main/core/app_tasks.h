#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>
#define ANIM_PATH_MAX 128

typedef struct { char dir_path[ANIM_PATH_MAX]; int channel; } anim_cmd_t;
typedef struct { QueueHandle_t cmd_queue; } anim_task_ctx_t;
BaseType_t anim_playback_init(void);
int anim_playback_start(const char *dir_path, int channel);
void AnimTask(void *context);
bool anim_can_accept(int count);
bool anim_validate_directory(const char *path, int expected_frames);
uint32_t anim_rendered_frames(int channel);
// Runs inside the animation owner task; only evicts unused derived .r5 files.
int anim_prune_cache(void);
