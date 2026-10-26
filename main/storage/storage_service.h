#pragma once
#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "media_storage.h"

esp_err_t storage_service_init(void);
bool storage_service_lock(void);
void storage_service_unlock(void);
bool storage_import_busy(void); // Caller holds service lock.
bool storage_maintenance_busy(void);
void storage_maintenance_set(bool active);
void storage_note_change(void);
bool storage_service_command(const cJSON *request,cJSON *reply,const char **error);
esp_err_t storage_system_json(cJSON *reply);
esp_err_t storage_http_import(httpd_req_t *request);
esp_err_t storage_http_upload(httpd_req_t *request);
void *storage_cleanup_open(media_storage_live_t live,void *live_context);
int storage_cleanup_step(void *context,const char *const *keep,int mode,media_storage_stats_t *stats);
void storage_cleanup_close(void *context);
