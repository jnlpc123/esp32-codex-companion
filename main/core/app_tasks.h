#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>
#include "media_storage.h"
#define ANIM_PATH_MAX 128

typedef struct { char dir_path[ANIM_PATH_MAX]; int channel; } anim_cmd_t;
typedef struct { QueueHandle_t cmd_queue; } anim_task_ctx_t;
BaseType_t anim_playback_init(void);
int anim_playback_start(const char *dir_path, int channel);
void AnimTask(void *context);
bool anim_can_accept(int count);
bool anim_validate_directory(const char *path, int expected_frames);
uint32_t anim_rendered_frames(int channel);
int anim_current_frame(int channel);
unsigned anim_stack_free_min(void);
// Runs inside the animation owner task; only evicts unused derived .r5 files.
int anim_prune_cache(void);
typedef struct {uint32_t id;bool pending;int mode,result;size_t free_before,free_after;media_storage_stats_t stats;} anim_cleanup_status_t;
bool anim_cleanup_available(void);
uint32_t anim_clear_media_start(int mode);
bool anim_cleanup_get(uint32_t id,anim_cleanup_status_t *result);
