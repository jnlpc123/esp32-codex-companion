#include "companion.h"
#include "notify_tone.h"
#include "app_tasks.h"
#include "alpha_cache.h"
#include "display_init.h"
#include "cJSON.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "fast_read.h"
#include "storage_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>

static SemaphoreHandle_t s_lock, s_config_lock;
static nvs_handle_t s_nvs;
static companion_config_t s_config;
static int64_t s_contact_us, s_wifi_contact_us, s_manual_until;
static uint32_t s_revision = 1, s_notifications;
static task_counts_t s_remote_counts, s_wifi_counts, s_manual_counts, s_last_counts;
static uint32_t s_boot_id, s_ack_id, s_ack_through;
static task_scope_t s_ack_scope;
static char s_notice_ids[3][17];
static const char *s_last_tone = "none";
static const char *names[] = {"idle", "working", "waiting", "done"};
static const char *idle_names[] = {"off", "colors", "black"};
static bool s_idle_playing;
static int64_t s_idle_since_us;
static uint32_t s_idle_completion_seq;


static bool connected(int64_t now) { return s_contact_us && now - s_contact_us < 10000000; }
static bool wifi_connected(int64_t now) { return s_wifi_contact_us && now - s_wifi_contact_us < 6000000; }
static const char *transport(int64_t now)
{
    // Give fresh USB precedence; let Wi-Fi take over four seconds after USB stops.
    if (connected(now) && (!wifi_connected(now) || now - s_contact_us < 4000000)) return "usb";
    if (wifi_connected(now)) return "wifi";
    return "none";
}
static task_counts_t effective_counts(int64_t now)
{
    if(now<s_manual_until)return s_manual_counts;
    const char *source=transport(now);
    task_counts_t c=!strcmp(source,"usb")?s_remote_counts:!strcmp(source,"wifi")?s_wifi_counts:(task_counts_t){0};
    if(!task_scope_equal(&c.scope,&s_config.scope))return (task_counts_t){0};
    return c;
}
static vibe_state_t effective(int64_t now){task_counts_t c=effective_counts(now);return task_counts_state(&c);}

// Called with s_lock held. Heartbeats never restart the completion hold.
static void update_idle_locked(int64_t now, const task_counts_t *counts)
{
    const char *source=transport(now);
    const task_counts_t *synced=!strcmp(source,"usb")?&s_remote_counts:!strcmp(source,"wifi")?&s_wifi_counts:NULL;
    bool eligible=s_config.idle_playback!=IDLE_PLAYBACK_OFF && !counts->working && !counts->waiting &&
        now>=s_manual_until && synced && task_scope_equal(&synced->scope,&s_config.scope);
    bool playing=false;
    if(!eligible){s_idle_since_us=0;s_idle_completion_seq=0;}
    else if(!counts->done){s_idle_since_us=0;s_idle_completion_seq=0;playing=true;}
    else {
        if(!s_idle_since_us || s_idle_completion_seq!=counts->completion_seq){
            s_idle_since_us=now;s_idle_completion_seq=counts->completion_seq;
        }
        playing=s_config.idle_done_delay>=0 && now-s_idle_since_us>=(int64_t)s_config.idle_done_delay*1000000;
    }
    if(playing!=s_idle_playing){s_idle_playing=playing;s_revision++;}
}

static cJSON *config_json(const companion_config_t *c)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddNumberToObject(o, "version", 1);
    task_scope_json(o,"scope",&c->scope);
    cJSON *colors=cJSON_AddArrayToObject(o,"colors");
    for(int i=0;i<3;i++)cJSON_AddItemToArray(colors,cJSON_CreateNumber(c->colors[i]));
    cJSON_AddNumberToObject(o, "volume", c->volume);
    cJSON_AddNumberToObject(o, "foreground_size", c->foreground_size);
    cJSON_AddNumberToObject(o, "brightness", c->brightness);
    cJSON_AddBoolToObject(o, "muted", c->muted);
    cJSON_AddStringToObject(o, "layout", c->all_screens ? "all" : "traffic");
    cJSON_AddStringToObject(o, "idle_playback", idle_names[c->idle_playback]);
    cJSON_AddNumberToObject(o, "idle_done_delay", c->idle_done_delay);
    cJSON *a = cJSON_AddArrayToObject(o, "media");
    for (int i = 0; i < 3; i++) cJSON_AddItemToArray(a, cJSON_CreateString(c->media[i]));
    return o;
}

static bool integer_field(cJSON *o, const char *key, int minimum, int maximum, int *value)
{
    cJSON *p = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!p) return true;
    if (!cJSON_IsNumber(p) || p->valuedouble < minimum || p->valuedouble > maximum ||
        p->valuedouble != (int)p->valuedouble) return false;
    *value = p->valueint;
    return true;
}

static bool valid_path(const char *p)
{
    if (!p || strlen(p) >= sizeof(s_config.media[0])) return false;
    if (!*p) return true;
    if (strncmp(p, "/data/", 6) || !p[6] || strstr(p, "..") || strstr(p, "//")) return false;
    for (const char *c = p + 6; *c; c++) if (!isalnum((unsigned char)*c) && *c != '_' && *c != '-' && *c != '/') return false;
    return p[strlen(p) - 1] != '/';
}

static bool parse_config(cJSON *o, companion_config_t *c)
{
    if (!cJSON_IsObject(o) || !integer_field(o, "volume", 0, 80, &c->volume) ||
        !integer_field(o, "foreground_size", 80, 144, &c->foreground_size) ||
        !integer_field(o, "brightness", 5, 100, &c->brightness) ||
        !integer_field(o, "idle_done_delay", -1, 300, &c->idle_done_delay)) return false;
    cJSON *idle=cJSON_GetObjectItemCaseSensitive(o,"idle_playback");
    if(idle){
        if(!cJSON_IsString(idle))return false;
        int mode=-1;for(int i=0;i<3;i++)if(!strcmp(idle->valuestring,idle_names[i]))mode=i;
        if(mode<0)return false;
        c->idle_playback=(idle_playback_t)mode;
    }
    cJSON *scope=cJSON_GetObjectItemCaseSensitive(o,"scope");
    if(scope&&!task_scope_parse(scope,&c->scope))return false;
    cJSON *colors=cJSON_GetObjectItemCaseSensitive(o,"colors");
    if(colors){
        if(!cJSON_IsArray(colors)||cJSON_GetArraySize(colors)!=3)return false;
        for(int i=0;i<3;i++){
            cJSON *v=cJSON_GetArrayItem(colors,i);
            if(!cJSON_IsNumber(v)||v->valuedouble<0||v->valuedouble>65535||v->valuedouble!=(int)v->valuedouble)return false;
            c->colors[i]=v->valueint;
        }
    }
    int version = 1;
    if (!integer_field(o, "version", 1, 1, &version)) return false;
    cJSON *p = cJSON_GetObjectItemCaseSensitive(o, "muted");
    if (p) { if (!cJSON_IsBool(p)) return false; c->muted = cJSON_IsTrue(p); }
    p = cJSON_GetObjectItemCaseSensitive(o, "layout");
    if (p) {
        if (!cJSON_IsString(p) || (strcmp(p->valuestring, "traffic") && strcmp(p->valuestring, "all"))) return false;
        c->all_screens = !strcmp(p->valuestring, "all");
    }
    p = cJSON_GetObjectItemCaseSensitive(o, "media");
    if (p) {
        if (!cJSON_IsArray(p) || cJSON_GetArraySize(p) != 3) return false;
        for (int i = 0; i < 3; i++) {
            cJSON *item = cJSON_GetArrayItem(p, i);
            if (!cJSON_IsString(item) || !valid_path(item->valuestring)) return false;
            strlcpy(c->media[i], item->valuestring, sizeof(c->media[i]));
        }
    }
    return true;
}

esp_err_t companion_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_config_lock = xSemaphoreCreateMutex();
    if (!s_lock || !s_config_lock) return ESP_ERR_NO_MEM;
    s_config = (companion_config_t){.volume=40, .foreground_size=112, .brightness=60, .colors={0xf800,0xffe0,0x07e0},
        .media={"/data/1", "/data/2_cleaned", "/data/3_cleaned"},.idle_playback=IDLE_PLAYBACK_BLACK,.idle_done_delay=10};
    esp_err_t ret = nvs_open("companion", NVS_READWRITE, &s_nvs);
    if (ret != ESP_OK) return ret;
    s_boot_id=esp_random();
    char *stored=malloc(1024); size_t size=1024;
    if(!stored)return ESP_ERR_NO_MEM;
    if (nvs_get_str(s_nvs, "config", stored, &size) == ESP_OK) {
        cJSON *o = cJSON_ParseWithOpts(stored, NULL, true);
        companion_config_t candidate = s_config;
        if (o && parse_config(o, &candidate)) s_config = candidate;
        cJSON_Delete(o);
    }
    free(stored);
    return ESP_OK;
}

void companion_get_config(companion_config_t *c)
{
    xSemaphoreTake(s_lock, portMAX_DELAY); *c = s_config; xSemaphoreGive(s_lock);
}

void companion_restore_media(void)
{
    companion_config_t c; companion_get_config(&c);
    for (int i = 0; i < 3; i++) anim_playback_start(c.media[i], i + 1);
}

void companion_tick(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t now=esp_timer_get_time();
    task_counts_t c=effective_counts(now);
    update_idle_locked(now,&c);
    if(!task_counts_equal(&c,&s_last_counts)){s_last_counts=c;s_revision++;}
    xSemaphoreGive(s_lock);
}

void companion_get_visual(int channel, companion_visual_t *v)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t now=esp_timer_get_time();
    task_counts_t c=effective_counts(now);
    update_idle_locked(now,&c);
    int count=channel==1?c.done:channel==2?c.waiting:c.working;
    int role=channel;
    if(s_config.all_screens){
        int state=task_counts_state(&c);role=state==3?1:state==2?2:3;
        count=role==1?c.done:role==2?c.waiting:c.working;
    }
    uint16_t color=s_config.colors[role-1];
    if(role==1&&(c.working||c.waiting))color=(color>>1)&0x7bef;
    v->background=(s_config.all_screens?task_counts_state(&c)!=0:count>0)?color:0x0841;
    v->count=count;v->active=count>0;
    if(s_idle_playing){
        v->background=s_config.idle_playback==IDLE_PLAYBACK_BLACK?0:s_config.colors[channel-1];
        v->count=0;v->active=true;
    }
    v->foreground_size = s_config.foreground_size;
    v->revision = s_revision;
    xSemaphoreGive(s_lock);
}

static esp_err_t update_config(cJSON *o, bool activate, const char **error)
{
    if(!storage_service_lock()){*error="storage busy; retry";return ESP_ERR_INVALID_STATE;}
    if(storage_maintenance_busy()){*error="cleanup in progress";storage_service_unlock();return ESP_ERR_INVALID_STATE;}
    xSemaphoreTake(s_config_lock, portMAX_DELAY);
    companion_config_t next, before;
    companion_get_config(&before); next = before;
    int expected = 0, selected = 0;
    bool valid = true;
    if (activate) {
        cJSON *dir = cJSON_GetObjectItemCaseSensitive(o, "dir");
        valid = integer_field(o, "channel", 1, 3, &selected) && selected &&
            integer_field(o, "frames", 1, 256, &expected) && expected &&
            cJSON_IsString(dir) && valid_path(dir->valuestring) && dir->valuestring[0];
        if (valid) strlcpy(next.media[selected - 1], dir->valuestring, sizeof(next.media[0]));
    } else valid = parse_config(cJSON_GetObjectItemCaseSensitive(o, "config"), &next);
    int changed = 0;
    for (int i = 0; valid && i < 3; i++) {
        bool differs = strcmp(next.media[i], before.media[i]) != 0;
        if ((differs || (activate && i == selected - 1)) && next.media[i][0] &&
            !anim_validate_directory(next.media[i], activate && i == selected - 1 ? expected : 0)) valid = false;
        if (differs || (activate && i == selected - 1)) changed++;
    }
    if (!valid || !anim_can_accept(changed)) {
        *error = valid ? "animation queue busy; retry" : "invalid configuration or incomplete image sequence";
        xSemaphoreGive(s_config_lock);storage_service_unlock();return ESP_ERR_INVALID_ARG;
    }
    cJSON *saved = config_json(&next);
    char encoded[1024];
    esp_err_t ret = ESP_ERR_NO_MEM;
    if (saved && cJSON_PrintPreallocated(saved, encoded, sizeof(encoded), false)) {
        ret = nvs_set_str(s_nvs, "config", encoded);
        if (ret == ESP_OK) ret = nvs_commit(s_nvs);
    }
    cJSON_Delete(saved);
    if (ret == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY); s_config = next; s_revision++; xSemaphoreGive(s_lock);
        storage_note_change();
        notify_tone_settings(next.volume, next.muted);
        display_set_brightness(next.brightness);
        for (int i = 0; i < 3; i++) if (strcmp(next.media[i], before.media[i]) || (activate && i == selected - 1))
            anim_playback_start(next.media[i], i + 1);
    } else *error = "configuration could not be saved";
    xSemaphoreGive(s_config_lock);
    storage_service_unlock();
    return ret;
}

static void cleanup_json(cJSON *reply,const anim_cleanup_status_t *job)
{
    cJSON *o=cJSON_AddObjectToObject(reply,"cleanup");
    cJSON_AddNumberToObject(o,"job_id",job->id);cJSON_AddBoolToObject(o,"pending",job->pending);
    cJSON_AddStringToObject(o,"scope",job->mode==2?"all":job->mode==3?"cache":"unused");
    cJSON_AddNumberToObject(o,"files_removed",job->stats.removed_files);
    cJSON_AddNumberToObject(o,"logical_bytes_removed",job->stats.removed_bytes);
    cJSON_AddNumberToObject(o,"errors",job->stats.errors);cJSON_AddNumberToObject(o,"result",job->result);
    cJSON_AddNumberToObject(o,"free_before",job->free_before);cJSON_AddNumberToObject(o,"free_after",job->free_after);
}
static void clear_media(cJSON *request,cJSON *reply,const char **error)
{
    cJSON *scope=cJSON_GetObjectItemCaseSensitive(request,"scope"),*confirm=cJSON_GetObjectItemCaseSensitive(request,"confirm");
    bool all=cJSON_IsString(scope)&&!strcmp(scope->valuestring,"all");
    bool cache=cJSON_IsString(scope)&&!strcmp(scope->valuestring,"cache");
    const char *expected=all?"DELETE_ALL_MEDIA":cache?"DELETE_UNUSED_CACHE":"DELETE_UNUSED_MEDIA";
    if(!cJSON_IsString(scope)||(!all&&!cache&&strcmp(scope->valuestring,"unused"))||!cJSON_IsString(confirm)||strcmp(confirm->valuestring,expected)){
        *error="cleanup confirmation does not match scope";return;
    }
    if(!storage_service_lock()){*error="storage busy; retry";return;}
    if(storage_import_busy()||storage_maintenance_busy()||!anim_cleanup_available()){
        *error="upload or cleanup in progress; finish it first";storage_service_unlock();return;
    }
    storage_maintenance_set(true);bool started=false;
    xSemaphoreTake(s_config_lock,portMAX_DELAY);
    companion_config_t c;companion_get_config(&c);
    if(all){
        for(int i=0;i<3;i++)c.media[i][0]=0;
        cJSON *saved=config_json(&c);char encoded[1024];esp_err_t result=ESP_ERR_NO_MEM;
        if(saved&&cJSON_PrintPreallocated(saved,encoded,sizeof(encoded),false)){
            result=nvs_set_str(s_nvs,"config",encoded);if(result==ESP_OK)result=nvs_commit(s_nvs);
        }
        cJSON_Delete(saved);
        if(result!=ESP_OK){*error="empty media selection could not be saved";goto finish;}
        xSemaphoreTake(s_lock,portMAX_DELAY);s_config=c;s_revision++;xSemaphoreGive(s_lock);
        storage_note_change();
    }
    uint32_t id=anim_clear_media_start(all?2:cache?3:1);anim_cleanup_status_t job;
    if(!id||!anim_cleanup_get(id,&job))*error="cleanup could not start; inspect current selection";
    else{started=true;cleanup_json(reply,&job);}
finish:
    if(!started)storage_maintenance_set(false);
    xSemaphoreGive(s_config_lock);storage_service_unlock();
}

esp_err_t companion_handle_json(const char *input, bool usb, char *output, size_t size)
{
    cJSON *request = cJSON_ParseWithOpts(input, NULL, true), *reply = cJSON_CreateObject();
    int id = 0;
    const char *error = NULL;
    char diagnostic[96];
    bool play = false, waiting = false;
    int tone_kind=0,pruned=-1;
    bool have_frame=false;alpha_cache_info_t frame_info={0};
    bool compact=false;
    cJSON *op = cJSON_GetObjectItemCaseSensitive(request, "op");
    cJSON *version = cJSON_GetObjectItemCaseSensitive(request, "v");
    if (!request || !cJSON_IsObject(request) || !integer_field(request, "id", 0, INT_MAX, &id) ||
        (version && (!cJSON_IsNumber(version) || version->valuedouble != 1)) || !cJSON_IsString(op)) error = "invalid protocol v1 command";
    if (!error) {
        const char *command = op->valuestring;
        if((compact=storage_service_command(request,reply,&error))){}
        else if(!strcmp(command,"clear_media")){clear_media(request,reply,&error);compact=true;}
        else if(!strcmp(command,"cleanup_status")){
            compact=true;int id=0;anim_cleanup_status_t job;
            if(!integer_field(request,"job_id",1,INT_MAX,&id)||!anim_cleanup_get(id,&job))error="unknown cleanup job";
            else{cleanup_json(reply,&job);if(!job.pending&&job.result<0)error="cleanup incomplete; inspect storage before retrying";}
        }
        else if (!strcmp(command, "state") || !strcmp(command,"manual_state") || (!usb && !strcmp(command, "sync"))) {
            cJSON *state = cJSON_GetObjectItemCaseSensitive(request, "state");
            cJSON *notify = cJSON_GetObjectItemCaseSensitive(request, "notify");
            int value = -1;
            if (cJSON_IsString(state)) for (int i = 0; i < 4; i++) if (!strcmp(state->valuestring, names[i])) value = i;
            task_counts_t counts;
            cJSON *kind=cJSON_GetObjectItemCaseSensitive(request,"notice_kind"),*notice=cJSON_GetObjectItemCaseSensitive(request,"notice_id");
            int nk=cJSON_IsString(kind)?(!strcmp(kind->valuestring,"waiting")?0:!strcmp(kind->valuestring,"partial")?1:!strcmp(kind->valuestring,"all")?2:-1):-1;
            bool valid_notice=(!kind&&!notice)||(nk>=0&&cJSON_IsString(notice)&&strlen(notice->valuestring)==16);
            if(valid_notice&&notice)for(const char *p=notice->valuestring;*p;p++)if(!isxdigit((unsigned char)*p))valid_notice=false;
            if(value<0||(notify&&!cJSON_IsBool(notify))||!valid_notice||!task_counts_parse(request,value,&counts))error="invalid state/counts/notice";
            else {
                xSemaphoreTake(s_lock,portMAX_DELAY);
                int64_t now=esp_timer_get_time();task_counts_t old=effective_counts(now);
                if(usb&&strcmp(command,"manual_state")){s_remote_counts=counts;s_contact_us=now;s_manual_until=0;}
                else if(!strcmp(command,"sync")){
                    if(!task_counts_equal(&counts,&s_wifi_counts))s_manual_until=0;
                    s_wifi_counts=counts;s_wifi_contact_us=now;
                }else{s_manual_counts=counts;s_manual_until=now+6000000;}
                task_counts_t current=effective_counts(now);
                if(!task_counts_equal(&old,&current))s_revision++;
                waiting=value==VIBE_WAITING;
                if(kind){
                    bool applicable=nk==0?current.waiting>0:nk==1?current.done>0:current.done>0&&!current.working&&!current.waiting;
                    play=cJSON_IsTrue(notify)&&applicable&&strcmp(s_notice_ids[nk],notice->valuestring);
                    if(play){strlcpy(s_notice_ids[nk],notice->valuestring,17);tone_kind=nk;}
                }else play=!counts.detailed&&task_counts_state(&old)!=task_counts_state(&current)&&cJSON_IsTrue(notify)&&(waiting||value==VIBE_DONE);
                if(!kind)tone_kind=waiting?0:2;
                if(play){s_notifications++;s_last_tone=tone_kind==0?"waiting":tone_kind==1?"partial":"done";}
                xSemaphoreGive(s_lock);
            }
        } else if(!strcmp(command,"ack")) {
            xSemaphoreTake(s_lock,portMAX_DELAY);
            task_counts_t current=effective_counts(esp_timer_get_time());
            if(current.done){s_ack_id++;s_ack_through=current.completion_seq;s_ack_scope=s_config.scope;}
            xSemaphoreGive(s_lock);
        } else if(!strcmp(command,"catalog")) {
            xSemaphoreTake(s_lock,portMAX_DELAY);if(!task_catalog_update(request))error="invalid catalog";xSemaphoreGive(s_lock);
        } else if(!strcmp(command,"get_catalog")) {
            xSemaphoreTake(s_lock,portMAX_DELAY);task_catalog_json(reply);xSemaphoreGive(s_lock);
        } else if(!strcmp(command,"storage_gc")) {
            bool driver=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(request,"driver"));
            storage_use_driver_reads(driver);
            esp_err_t result=ESP_ERR_NOT_FINISHED;
            for(int attempt=0;attempt<4&&result==ESP_ERR_NOT_FINISHED;attempt++)result=esp_spiffs_gc(NULL,65536);
            storage_use_driver_reads(false);
            if(result!=ESP_OK){snprintf(diagnostic,sizeof(diagnostic),"Storage GC failed (ESP %d, SPIFFS %d)",result,storage_last_gc_error());error=diagnostic;}
        } else if(!strcmp(command,"prune_cache")) {
            if(!storage_service_lock())error="storage busy; retry";
            else{
                if(storage_maintenance_busy()||storage_import_busy())error="storage cleanup busy; retry";
                else{pruned=anim_prune_cache();if(pruned<0)error="storage cleanup busy; retry";}
                storage_service_unlock();
            }
        } else if (!strcmp(command, "config") || !strcmp(command, "activate")) {
            update_config(request, !strcmp(command, "activate"), &error);
        } else if (!strcmp(command,"frame_info")) {
            cJSON *path=cJSON_GetObjectItemCaseSensitive(request,"path");int bg=0;
            bool safe=cJSON_IsString(path) && strlen(path->valuestring)<64 &&
                !strncmp(path->valuestring,"/data/",6) && !strstr(path->valuestring,"..") && !strstr(path->valuestring,"//");
            if(safe) {
                for(const char *p=path->valuestring+6;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='_'&&*p!='-'&&*p!='/'&&*p!='.')safe=false;
                const char *ext=strrchr(path->valuestring,'.');safe=safe&&ext&&!strcmp(ext,".a5");
            }
            if(!safe || !integer_field(request,"background",0,65535,&bg))error="invalid transparent frame path/background";
            else if(alpha_cache_inspect_mode(path->valuestring,bg,&frame_info,cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(request,"gray")))!=ESP_OK)error="invalid transparent frame";
            else have_frame=true;
        } else if (!strcmp(command, "tone")) {
            cJSON *kind = cJSON_GetObjectItemCaseSensitive(request, "kind");
            if (!cJSON_IsString(kind) || (strcmp(kind->valuestring, "waiting") && strcmp(kind->valuestring, "done"))) error = "unknown notification kind";
            else { play = true; waiting = !strcmp(kind->valuestring, "waiting"); tone_kind=waiting?0:2; }
        } else if (strcmp(command, "hello") && strcmp(command, "ping") && strcmp(command, "get")) error = "unknown command";
        if (!error && usb && (!strcmp(command,"state")||!strcmp(command,"ping"))) {
            xSemaphoreTake(s_lock, portMAX_DELAY); s_contact_us = esp_timer_get_time(); xSemaphoreGive(s_lock);
        }
    }
    bool queued = play && notify_tone_play_kind(tone_kind);
    if (!reply) {
        cJSON_Delete(request);
        snprintf(output, size, "{\"v\":1,\"id\":%d,\"ok\":false,\"error\":\"out of memory\"}", id);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(reply, "v", 1);
    cJSON_AddNumberToObject(reply, "id", id);
    cJSON_AddBoolToObject(reply, "ok", !error);
    if (error) cJSON_AddStringToObject(reply, "error", error);
    else if(!compact&&strcmp(op->valuestring,"get_catalog")) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        cJSON_AddStringToObject(reply, "product", "vibe-companion");
        if(!strcmp(op->valuestring,"hello")){
            cJSON *caps=cJSON_AddArrayToObject(reply,"capabilities");
            cJSON_AddItemToArray(caps,cJSON_CreateString("storage_v1"));cJSON_AddItemToArray(caps,cJSON_CreateString("usb_media_v1"));
        }
        cJSON_AddStringToObject(reply,"alpha_format","vaf1");
        if(pruned>=0)cJSON_AddNumberToObject(reply,"cache_files_removed",pruned);
        task_counts_t counts=effective_counts(esp_timer_get_time());
        update_idle_locked(esp_timer_get_time(),&counts);
        cJSON_AddBoolToObject(reply,"idle_playing",s_idle_playing);
        cJSON *cs=cJSON_AddObjectToObject(reply,"counts");
        cJSON_AddNumberToObject(cs,"working",counts.working);cJSON_AddNumberToObject(cs,"waiting",counts.waiting);cJSON_AddNumberToObject(cs,"done",counts.done);
        cJSON_AddBoolToObject(reply,"all_done",counts.done&&!counts.working&&!counts.waiting);
        cJSON_AddNumberToObject(reply,"completion_seq",counts.completion_seq);
        cJSON_AddNumberToObject(reply,"boot_id",s_boot_id);
        cJSON_AddNumberToObject(reply,"multitask_version",1);
        cJSON_AddNumberToObject(reply,"catalog_revision",task_catalog_revision());
        cJSON *ack=cJSON_AddObjectToObject(reply,"ack");cJSON_AddNumberToObject(ack,"id",s_ack_id);cJSON_AddNumberToObject(ack,"through",s_ack_through);task_scope_json(ack,"scope",&s_ack_scope);
        cJSON_AddStringToObject(reply, "state", names[effective(esp_timer_get_time())]);
        cJSON_AddBoolToObject(reply, "manual", esp_timer_get_time() < s_manual_until);
        cJSON_AddNumberToObject(reply, "free_heap", esp_get_free_heap_size());
        cJSON_AddBoolToObject(reply, "usb_connected", connected(esp_timer_get_time()));
        cJSON_AddBoolToObject(reply, "wifi_connected", wifi_connected(esp_timer_get_time()));
        cJSON_AddStringToObject(reply, "transport", transport(esp_timer_get_time()));
        cJSON_AddItemToObject(reply, "config", config_json(&s_config));
        cJSON_AddNumberToObject(reply, "notification_count", s_notifications);
        cJSON_AddStringToObject(reply, "last_tone", s_last_tone);
        xSemaphoreGive(s_lock);
        uint8_t mac[6];
        if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
            char identity[13];
            snprintf(identity, sizeof(identity), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            cJSON_AddStringToObject(reply, "device_id", identity);
        }
        cJSON_AddBoolToObject(reply, "sound_available", notify_tone_available());
        cJSON_AddBoolToObject(reply, "tone_queued", queued);
        cJSON_AddNumberToObject(reply, "tone_samples", notify_tone_samples());
        char ip[16] = "";
        esp_netif_ip_info_t info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK || !info.ip.addr)
            netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr)
            snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
        cJSON_AddStringToObject(reply, "wifi_ip", ip);
        cJSON *frames = cJSON_AddArrayToObject(reply, "frames");
        for (int i = 1; i <= 3; i++) cJSON_AddItemToArray(frames, cJSON_CreateNumber(anim_rendered_frames(i)));
        cJSON *colors = cJSON_AddArrayToObject(reply, "backgrounds");
        cJSON *active = cJSON_AddArrayToObject(reply, "active");
        for (int i = 1; i <= 3; i++) { companion_visual_t visual; companion_get_visual(i, &visual); cJSON_AddItemToArray(colors, cJSON_CreateNumber(visual.background)); cJSON_AddItemToArray(active,cJSON_CreateBool(visual.active)); }
        if(have_frame) {
            cJSON *frame=cJSON_AddObjectToObject(reply,"frame");
            cJSON_AddNumberToObject(frame,"width",frame_info.width);cJSON_AddNumberToObject(frame,"height",frame_info.height);
            cJSON_AddNumberToObject(frame,"payload_bytes",frame_info.payload_bytes);cJSON_AddNumberToObject(frame,"pixel_hash",frame_info.pixel_hash);
        }
    }
    bool printed = cJSON_PrintPreallocated(reply, output, size, false);
    cJSON_Delete(reply); cJSON_Delete(request);
    if (!printed) snprintf(output, size, "{\"v\":1,\"id\":%d,\"ok\":false,\"error\":\"response too large\"}", id);
    return error || !printed ? ESP_ERR_INVALID_ARG : ESP_OK;
}
