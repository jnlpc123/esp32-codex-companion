#include "app_tasks.h"
#include "animation_cache.h"
#include "animation_crc.h"
#include "alpha_cache.h"
#include "storage_service.h"
#include "companion.h"
#include "companion_render.h"
#include "display_init.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include "freertos/semphr.h"
#define TAG "animation"

/* ---- Independent three-screen animation playback ------------------------ */
#define ANIM_FRAME_DELAY_MS 33
#define ANIM_MAX_FILES 256
#define ANIM_CHANNELS 3
static QueueHandle_t s_anim_cmd_queue;
static volatile uint32_t s_total_frames[3];
static SemaphoreHandle_t s_gc_lock;
static volatile bool s_gc_requested;
static volatile int s_gc_result;
static int s_gc_mode;
static volatile bool s_anim_ready;
static anim_cleanup_status_t s_cleanup_job;
static portMUX_TYPE s_job_mux=portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_cleanup_sequence;

typedef struct {
    char **files;
    char source_dir[ANIM_PATH_MAX];
    int count, index;
    uint16_t delays[ANIM_MAX_FILES];
    int64_t due_us, stats_us, remaining_us;
    bool has_frame, last_active;
    int current_index;
    unsigned rendered;
} anim_channel_state_t;

static int prune_unused_cache(anim_channel_state_t *states)
{
    DIR *dir=opendir("/data");if(!dir)return -1;
    struct dirent *entry;int removed=0;
    while((entry=readdir(dir))){
        const char *name=entry->d_name;
        if(name[0]=='/')name++;
        if(strlen(name)!=17||strncmp(name,".anim_",6)||strcmp(name+14,".r5"))continue;
        bool valid=true;
        for(int i=6;i<14;i++)if(!isxdigit((unsigned char)name[i]))valid=false;
        if(!valid)continue;
        char path[40];snprintf(path,sizeof(path),"/data/%.17s",name);
        bool used=false;
        for(int ch=0;ch<ANIM_CHANNELS&&!used;ch++)
            for(int i=0;i<states[ch].count;i++)if(!strcmp(path,states[ch].files[i])){used=true;break;}
        if(!used&&unlink(path)==0){removed++;storage_note_change();}
    }
    closedir(dir);return removed;
}

int anim_prune_cache(void)
{
    if(!s_gc_lock||xSemaphoreTake(s_gc_lock,0)!=pdTRUE)return -1;
    if(s_gc_requested){xSemaphoreGive(s_gc_lock);return -1;}
    s_gc_result=-1;s_gc_mode=0;s_gc_requested=true;
    int64_t deadline=esp_timer_get_time()+8000000;
    while(s_gc_requested&&esp_timer_get_time()<deadline)vTaskDelay(1);
    int result=s_gc_requested?-1:s_gc_result;
    xSemaphoreGive(s_gc_lock);return result;
}

bool anim_cleanup_available(void){return s_anim_ready&&!s_gc_requested&&!s_cleanup_job.pending;}
uint32_t anim_clear_media_start(int mode)
{
    if(!anim_cleanup_available()||!s_gc_lock||xSemaphoreTake(s_gc_lock,0)!=pdTRUE)return 0;
    if(!anim_cleanup_available()){xSemaphoreGive(s_gc_lock);return 0;}
    s_cleanup_sequence=(s_cleanup_sequence+1)&0x7fffffff;if(!s_cleanup_sequence)s_cleanup_sequence=1;
    portENTER_CRITICAL(&s_job_mux);
    s_cleanup_job=(anim_cleanup_status_t){.id=s_cleanup_sequence,.pending=true,.mode=mode};
    portEXIT_CRITICAL(&s_job_mux);
    s_gc_mode=mode;s_gc_requested=true;xSemaphoreGive(s_gc_lock);return s_cleanup_sequence;
}
bool anim_cleanup_get(uint32_t id,anim_cleanup_status_t *result)
{
    portENTER_CRITICAL(&s_job_mux);*result=s_cleanup_job;portEXIT_CRITICAL(&s_job_mux);
    return id&&id==result->id;
}

static void anim_clear_channel(anim_channel_state_t *state)
{
    for (int i = 0; i < state->count; i++) free(state->files[i]);
    free(state->files);
    memset(state, 0, sizeof(*state));
}

static bool cache_in_use(void *context,const char *name)
{
    anim_channel_state_t *states=context;char path[80];snprintf(path,sizeof(path),"/data/%s",name);
    for(int ch=0;ch<ANIM_CHANNELS;ch++)for(int i=0;i<states[ch].count;i++)
        if(!strcmp(path,states[ch].files[i]))return true;
    return false;
}

static bool is_jpg_file(const char *name)
{
    const char *ext = strrchr(name, '.');
    return ext && strcasecmp(ext, ".jpg") == 0;
}
static bool is_alpha_file(const char *name)
{
    const char *ext=strrchr(name,'.');return ext && strcasecmp(ext,".a5")==0;
}
static bool is_frame_file(const char *name) { return is_jpg_file(name)||is_alpha_file(name); }

static bool anim_load_channel(anim_channel_state_t *state, const char *dir_path, int channel)
{
    char **file_list = NULL;
    int file_count = 0;
    uint16_t frame_delays[ANIM_MAX_FILES] = {0};
        /* List .jpg files in the directory */
        DIR *dir = opendir(dir_path);
        if (!dir) {
            ESP_LOGE(TAG, "[anim] failed to open dir: %s", dir_path);
            return false;
        }

        /* First pass: count .jpg files */
        int jpg_count = 0;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG && is_frame_file(entry->d_name)) {
                jpg_count++;
            }
        }

        if (jpg_count == 0) {
            closedir(dir);
            ESP_LOGW(TAG, "[anim] no JPG files in dir: %s", dir_path);
            return false;
        }

        if (jpg_count > ANIM_MAX_FILES) {
            ESP_LOGW(TAG, "[anim] limiting %d files to %d", jpg_count, ANIM_MAX_FILES);
            jpg_count = ANIM_MAX_FILES;
        }

        /* Allocate file list */
        file_list = (char **)calloc((size_t)jpg_count, sizeof(char *));
        if (!file_list) {
            closedir(dir);
            ESP_LOGE(TAG, "[anim] failed to allocate file list");
            return false;
        }

        /* Second pass: collect full paths */
        rewinddir(dir);
        int idx = 0;
        while ((entry = readdir(dir)) != NULL && idx < jpg_count) {
            if (entry->d_type == DT_REG && is_frame_file(entry->d_name)) {
                size_t len = strlen(dir_path) + strlen(entry->d_name) + 2;
                file_list[idx] = (char *)malloc(len);
                if (file_list[idx]) {
                    snprintf(file_list[idx], len, "%s/%s",
                             dir_path, entry->d_name);
                    idx++;
                }
            }
        }
        closedir(dir);
        file_count = idx;

        /* Sort alphabetically by full path */
        for (int i = 0; i < file_count - 1; i++) {
            for (int j = i + 1; j < file_count; j++) {
                if (file_list[i] && file_list[j] &&
                    strcmp(file_list[i], file_list[j]) > 0) {
                    char *tmp    = file_list[i];
                    file_list[i] = file_list[j];
                    file_list[j] = tmp;
                }
            }
        }

        ESP_LOGI(TAG, "[anim] dir=%s ch=%d files=%d",
                 dir_path, channel, file_count);

        for (int i = 0; i < file_count; i++) frame_delays[i] = ANIM_FRAME_DELAY_MS;
        char timing_path[ANIM_PATH_MAX + 16];
        snprintf(timing_path, sizeof(timing_path), "%s/_timing.txt", dir_path);
        FILE *timing = fopen(timing_path, "r");
        if (timing) {
            for (int i = 0; i < file_count; i++) {
                unsigned delay_ms;
                if (fscanf(timing, "%u", &delay_ms) != 1) break;
                if (delay_ms >= 17 && delay_ms <= 60000) frame_delays[i] = delay_ms;
            }
            fclose(timing);
        }


    if (!file_count) { free(file_list); return false; }
    int cached = 0;
    for (int i = 0; i < file_count; i++) {
        if (is_alpha_file(file_list[i])) { cached++; continue; }
        char cache_path[64];
        if (animation_cache_prepare(file_list[i], cache_path, sizeof(cache_path))) {
            char *replacement = strdup(cache_path);
            if (replacement) { free(file_list[i]); file_list[i] = replacement; cached++; }
        }
        vTaskDelay(1);
    }
    ESP_LOGI(TAG, "[anim] ch=%d display cache ready: %d/%d", channel, cached, file_count);
    anim_clear_channel(state);
    state->files = file_list;
    state->count = file_count;
    strlcpy(state->source_dir,dir_path,sizeof(state->source_dir));
    memcpy(state->delays, frame_delays, sizeof(frame_delays));
    state->due_us = state->stats_us = esp_timer_get_time();
    return true;
}

BaseType_t anim_playback_init(void)
{
    if (s_anim_cmd_queue) return pdFAIL;
    animation_crc_init();
    s_gc_lock=xSemaphoreCreateMutex();if(!s_gc_lock)return pdFAIL;
    if (!alpha_cache_selftest()) { ESP_LOGE(TAG,"Alpha cache self-test failed"); return pdFAIL; }
    s_anim_cmd_queue = xQueueCreate(6, sizeof(anim_cmd_t));
    if (!s_anim_cmd_queue) return pdFAIL;
    anim_task_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) { vQueueDelete(s_anim_cmd_queue); s_anim_cmd_queue = NULL; return pdFAIL; }
    ctx->cmd_queue = s_anim_cmd_queue;
    if (xTaskCreate(AnimTask, "anim", 4096, ctx, 5, NULL) != pdPASS) {
        free(ctx); vQueueDelete(s_anim_cmd_queue); s_anim_cmd_queue = NULL; return pdFAIL;
    }
    ESP_LOGI(TAG, "[anim] three-screen playback subsystem initialized");
    return pdPASS;
}

int anim_playback_start(const char *dir_path, int channel)
{
    if (!s_anim_cmd_queue || !dir_path || channel < 1 || channel > 3 ||
        strlen(dir_path) >= ANIM_PATH_MAX) return -1;
    anim_cmd_t cmd = {.channel = channel};
    strlcpy(cmd.dir_path, dir_path, sizeof(cmd.dir_path));
    if (xQueueSend(s_anim_cmd_queue, &cmd, 0) != pdTRUE) return -1;
    ESP_LOGI(TAG, "[anim] queued: %s ch=%d", dir_path, channel);
    return 0;
}

void AnimTask(void *pvParameters)
{
    anim_task_ctx_t *ctx = pvParameters;
    QueueHandle_t queue = ctx->cmd_queue;
    free(ctx);
    // Decode serially, keeping only one full-size pixel buffer in memory.
    anim_channel_state_t *states = calloc(ANIM_CHANNELS, sizeof(*states));
    if (!states) { ESP_LOGE(TAG, "[anim] state allocation failed"); vTaskDelete(NULL); return; }
    s_anim_ready=true;
    int cursor = 0;
    uint32_t revisions[3] = {0};
    void *cleanup_walk=NULL;
    companion_config_t cleanup_config;
    const char *cleanup_keep[7]={0};
    media_storage_stats_t cleanup_stats={0};
    bool cleanup_started=false;size_t cleanup_total=0,cleanup_before=0;
    while (1) {
        anim_cmd_t cmd;
        while (xQueueReceive(queue, &cmd, 0) == pdTRUE) {
            if (cmd.dir_path[0]) anim_load_channel(&states[cmd.channel - 1], cmd.dir_path, cmd.channel);
            else anim_clear_channel(&states[cmd.channel - 1]);
            revisions[cmd.channel - 1] = 0;
        }
        if(s_gc_requested&&s_gc_mode){
            if(!cleanup_started){
                companion_get_config(&cleanup_config);int n=0;
                for(int ch=0;ch<3;ch++)cleanup_keep[n++]=cleanup_config.media[ch];
                for(int ch=0;ch<3;ch++){
                    if(s_gc_mode==2)anim_clear_channel(&states[ch]);
                    else if(states[ch].source_dir[0])cleanup_keep[n++]=states[ch].source_dir;
                }
                cleanup_keep[n]=NULL;
                if(s_gc_mode==2)memset(revisions,0,sizeof(revisions));
                memset(&cleanup_stats,0,sizeof(cleanup_stats));
                esp_spiffs_info(NULL,&cleanup_total,&cleanup_before);
                cleanup_walk=storage_cleanup_open(cache_in_use,states);cleanup_started=true;
            }
            int result=cleanup_walk?storage_cleanup_step(cleanup_walk,cleanup_keep,s_gc_mode,&cleanup_stats):-1;
            if(result<=0){storage_cleanup_close(cleanup_walk);cleanup_walk=NULL;}
            size_t after=0,total=0;
            if(result<=0&&(esp_spiffs_info(NULL,&total,&after)!=ESP_OK||after>total))result=-1;
            portENTER_CRITICAL(&s_job_mux);
            s_cleanup_job.stats=cleanup_stats;s_cleanup_job.result=result;
            s_cleanup_job.free_before=cleanup_total-cleanup_before;
            if(result<=0){s_cleanup_job.free_after=total-after;s_cleanup_job.pending=false;}
            portEXIT_CRITICAL(&s_job_mux);
            if(result<=0){cleanup_started=false;s_gc_requested=false;storage_maintenance_set(false);}
        }else if(s_gc_requested){s_gc_result=prune_unused_cache(states);s_gc_requested=false;}
        companion_tick();
        int selected = -1;
        int64_t now = esp_timer_get_time();
        for (int i = 0; i < ANIM_CHANNELS; i++) {
            int ch = (cursor + i) % ANIM_CHANNELS;
            companion_visual_t visual;
            companion_get_visual(ch + 1, &visual);
            if(states[ch].last_active!=visual.active){
                if(!visual.active)states[ch].remaining_us=states[ch].due_us>now?states[ch].due_us-now:0;
                else states[ch].due_us=now+states[ch].remaining_us;
                states[ch].last_active=visual.active;
            }
            if (revisions[ch] != visual.revision || (visual.active && states[ch].count && states[ch].due_us <= now)) { selected = ch; break; }
        }
        if (selected < 0) { vTaskDelay(1); continue; }
        anim_channel_state_t *state = &states[selected];
        int64_t started = esp_timer_get_time();
        // Paths were enumerated when loading this channel. Opening/decoding
        // already validates them; avoid another SPIFFS stat scan every frame.
        companion_visual_t visual;
        companion_get_visual(selected + 1, &visual);
        bool scheduled = !state->has_frame || (visual.active && state->due_us <= started);
        int frame_index = scheduled?state->index:state->current_index;
        esp_err_t result = companion_render_frame(selected + 1, state->count ? state->files[frame_index] : NULL);
        revisions[selected] = visual.revision;
        if (result == ESP_OK) s_total_frames[selected]++;
        if (result == ESP_OK) state->rendered++;
        else ESP_LOGW(TAG, "[anim] ch=%d frame=%d: %s", selected + 1, state->index, esp_err_to_name(result));
        int64_t interval_us = (int64_t)(state->count ? state->delays[frame_index] : 33) * 1000;
        // Preserve cadence, but discard missed deadlines instead of adding an
        // extra interval after an overrun. Round-robin still serves every screen.
        if (scheduled) {
            int64_t next_due_us = state->due_us + interval_us;
            state->due_us = next_due_us > started ? next_due_us : started;
            if (state->count) state->index = (frame_index + 1) % state->count;
            state->current_index=frame_index;state->has_frame=true;
            if(!visual.active)state->remaining_us=interval_us;
        }
        now = esp_timer_get_time();
        if (now - state->stats_us >= 5000000) {
            ESP_LOGI(TAG, "[anim] rendered fps=%.2f ch=%d frame_work=%lldus",
                     state->rendered * 1000000.0 / (now - state->stats_us), selected + 1,
                     (long long)(now - started));
            state->stats_us = now;
            state->rendered = 0;
        }
        cursor = (selected + 1) % ANIM_CHANNELS;
        // Successful rendering blocks on DMA completion (the IDF SPI queue),
        // allowing the idle task to run. An additional tick wastes throughput.
        // Failed frames may not wait for DMA, so retain a blocking error backoff.
        if (result == ESP_OK) taskYIELD();
        else vTaskDelay(1);
    }
}

uint32_t anim_rendered_frames(int channel)
{
    return channel >= 1 && channel <= 3 ? s_total_frames[channel - 1] : 0;
}

bool anim_can_accept(int count)
{
    return s_anim_cmd_queue && count >= 0 && uxQueueSpacesAvailable(s_anim_cmd_queue) >= (unsigned)count;
}

bool anim_validate_directory(const char *path, int expected)
{
    DIR *dir = opendir(path);
    if (!dir) return false;
    int count = 0, format = 0;
    bool valid = true;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG || !is_frame_file(entry->d_name)) continue;
        int current=is_alpha_file(entry->d_name)?2:1;
        if(format && format!=current){valid=false;break;}format=current;
        char full[128];
        if (snprintf(full, sizeof(full), "%s/%s", path, entry->d_name) >= sizeof(full)) { valid = false; break; }
        if(current==2) {
            if(!alpha_cache_validate(full) || ++count>ANIM_MAX_FILES){valid=false;break;}
            continue;
        }
        FILE *f = fopen(full, "rb");
        if (!f) { valid = false; break; }
        unsigned char head[2], tail[2];
        fseek(f, 0, SEEK_END); long length = ftell(f);
        rewind(f);
        valid = length >= 32 && length <= 65536 && fread(head, 1, 2, f) == 2;
        if (valid) valid = fseek(f, -2, SEEK_END) == 0 && fread(tail, 1, 2, f) == 2 &&
            head[0] == 0xff && head[1] == 0xd8 && tail[0] == 0xff && tail[1] == 0xd9;
        fclose(f);
        if (!valid || ++count > ANIM_MAX_FILES) { valid = false; break; }
    }
    closedir(dir);
    return valid && count > 0 && (!expected || expected == count);
}
