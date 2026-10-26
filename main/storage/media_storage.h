#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum { MEDIA_OTHER, MEDIA_FILE, MEDIA_CACHE } media_file_kind_t;
size_t media_storage_reliable_free(size_t total,size_t used,size_t partition_bytes);
typedef struct {
    size_t current_bytes, unused_bytes, cache_bytes, other_bytes;
    size_t removed_bytes;
    unsigned files, removed_files, errors;
} media_storage_stats_t;
typedef int (*media_storage_next_t)(void *, char *, size_t, size_t *);
typedef int (*media_storage_remove_t)(void *, const char *);
typedef bool (*media_storage_live_t)(void *, const char *);
media_file_kind_t media_storage_kind(const char *name);
// A NULL remove callback is a strictly read-only inventory. Names are SPIFFS
// flat paths, not recursive OS directories. Unknown files are never deleted.
int media_storage_run(void *context, media_storage_next_t next,
                      media_storage_remove_t remove_file, media_storage_live_t live_cache,
                      const char *const *keep_directories, bool all,
                      media_storage_stats_t *stats);
// mode1=unused,2=all,3=cache only. Returns1 while more work remains.
int media_storage_step(void *context,media_storage_next_t next,media_storage_remove_t remove_file,
                       media_storage_live_t live_cache,const char *const *keep_directories,
                       int mode,media_storage_stats_t *stats,unsigned budget);
