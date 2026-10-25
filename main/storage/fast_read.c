#include "fast_read.h"
#include "esp_partition.h"
#include "esp_log.h"
#include <string.h>
#include "spiffs.h"
#include "freertos/FreeRTOS.h"

static const esp_partition_t *s_partition;
static const uint8_t *s_mapping;
static esp_partition_mmap_handle_t s_handle;
static int s_gc_error;
static bool s_driver_reads;
static volatile unsigned s_mutations;
static portMUX_TYPE s_mutation_mux=portMUX_INITIALIZER_UNLOCKED;
static void mutation_begin(void){portENTER_CRITICAL(&s_mutation_mux);s_mutations++;portEXIT_CRITICAL(&s_mutation_mux);}
static void mutation_end(void){portENTER_CRITICAL(&s_mutation_mux);s_mutations--;portEXIT_CRITICAL(&s_mutation_mux);}
void storage_use_driver_reads(bool enabled){s_driver_reads=enabled;}
s32_t __real_SPIFFS_gc(spiffs *fs,u32_t size);
s32_t __wrap_SPIFFS_gc(spiffs *fs,u32_t size)
{
    mutation_begin();s32_t result=__real_SPIFFS_gc(fs,size);s_gc_error=result;mutation_end();return result;
}
spiffs_file __real_SPIFFS_open(spiffs*,const char*,spiffs_flags,spiffs_mode);
spiffs_file __wrap_SPIFFS_open(spiffs *fs,const char *path,spiffs_flags flags,spiffs_mode mode)
{
    bool writing=flags&(SPIFFS_WRONLY|SPIFFS_CREAT|SPIFFS_TRUNC|SPIFFS_APPEND);
    if(writing)mutation_begin();
    spiffs_file result=__real_SPIFFS_open(fs,path,flags,mode);
    if(writing)mutation_end();
    return result;
}
s32_t __real_SPIFFS_write(spiffs*,spiffs_file,void*,s32_t);
s32_t __wrap_SPIFFS_write(spiffs *fs,spiffs_file fh,void *data,s32_t len)
{mutation_begin();s32_t r=__real_SPIFFS_write(fs,fh,data,len);mutation_end();return r;}
#define WRAP_HANDLE(name) \
s32_t __real_##name(spiffs*,spiffs_file); \
s32_t __wrap_##name(spiffs *fs,spiffs_file fh){mutation_begin();s32_t r=__real_##name(fs,fh);mutation_end();return r;}
WRAP_HANDLE(SPIFFS_close)
WRAP_HANDLE(SPIFFS_fflush)
s32_t __real_SPIFFS_remove(spiffs*,const char*);
s32_t __wrap_SPIFFS_remove(spiffs *fs,const char *path)
{mutation_begin();s32_t r=__real_SPIFFS_remove(fs,path);mutation_end();return r;}
s32_t __real_SPIFFS_rename(spiffs*,const char*,const char*);
s32_t __wrap_SPIFFS_rename(spiffs *fs,const char *old,const char *next)
{mutation_begin();s32_t r=__real_SPIFFS_rename(fs,old,next);mutation_end();return r;}
s32_t __real_SPIFFS_update_meta(spiffs*,const char*,const void*);
s32_t __wrap_SPIFFS_update_meta(spiffs *fs,const char *path,const void *meta)
{mutation_begin();s32_t r=__real_SPIFFS_update_meta(fs,path,meta);mutation_end();return r;}
s32_t __real_SPIFFS_fupdate_meta(spiffs*,spiffs_file,const void*);
s32_t __wrap_SPIFFS_fupdate_meta(spiffs *fs,spiffs_file fh,const void *meta)
{mutation_begin();s32_t r=__real_SPIFFS_fupdate_meta(fs,fh,meta);mutation_end();return r;}
s32_t __real_SPIFFS_ftruncate(spiffs*,spiffs_file,u32_t);
s32_t __wrap_SPIFFS_ftruncate(spiffs *fs,spiffs_file fh,u32_t length)
{mutation_begin();s32_t r=__real_SPIFFS_ftruncate(fs,fh,length);mutation_end();return r;}
s32_t __real_spiffs_gc_check(spiffs*,u32_t);
s32_t __wrap_spiffs_gc_check(spiffs *fs,u32_t size)
{mutation_begin();s32_t r=__real_spiffs_gc_check(fs,size);mutation_end();return r;}
int storage_last_gc_error(void){return s_gc_error;}

esp_err_t __real_esp_partition_read(const esp_partition_t *partition,
                                   size_t offset, void *dst, size_t size);

// Read-only playback retains the mapped transport. SPIFFS modifications,
// metadata updates and GC must use driver reads for their entire operation:
// mapped reads inside GC reproduced ENOSPC despite reclaimable deleted pages.
// The counter also covers implicit GC and overlapping callers on this core.
esp_err_t __wrap_esp_partition_read(const esp_partition_t *partition,
                                   size_t offset, void *dst, size_t size)
{
    if (!s_driver_reads && !s_mutations && s_mapping && partition == s_partition && dst &&
        offset <= partition->size && size <= partition->size - offset) {
        memcpy(dst, s_mapping + offset, size);
        return ESP_OK;
    }
    return __real_esp_partition_read(partition, offset, dst, size);
}

void storage_enable_mapped_reads(void)
{
    if (s_mapping) return;
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    if (!partition || partition->size < 64) return;
    const void *mapping = NULL;
    // Do not use BLOCKS_WRITE: uploads and SPIFFS garbage collection must work.
    esp_err_t err = esp_partition_mmap(partition, 0, partition->size,
                                      ESP_PARTITION_MMAP_DATA, &mapping, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGW("storage_read", "Mapping unavailable, retaining driver reads: %s", esp_err_to_name(err));
        return;
    }
    uint8_t probe[64];
    const size_t offsets[] = {0, partition->size / 2, partition->size - sizeof(probe)};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        if (__real_esp_partition_read(partition, offsets[i], probe, sizeof(probe)) != ESP_OK ||
            memcmp(probe, (const uint8_t *)mapping + offsets[i], sizeof(probe))) {
            esp_partition_munmap(s_handle);
            ESP_LOGW("storage_read", "Mapping validation failed, retaining driver reads");
            return;
        }
    }
    s_partition = partition;
    s_mapping = mapping;
    ESP_LOGI("storage_read", "Mapped SPIFFS reads enabled, driver comparison passed");
}
