#include "media_storage.h"
#include "legacy_assets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { const char *name; size_t size; int deleted; } file_t;
typedef struct { file_t *files; size_t count, cursor; const char *fail; } fixture_t;
static int next(void *context, char *name, size_t length, size_t *size)
{
    fixture_t *f=context;
    if(f->cursor==f->count)return 0;
    file_t *p=&f->files[f->cursor++];
    assert(strlen(p->name)<length);strcpy(name,p->name);*size=p->size;return 1;
}
static int remove_file(void *context,const char *name)
{
    fixture_t *f=context;if(f->fail&&!strcmp(name,f->fail))return -1;
    for(size_t i=0;i<f->count;i++)if(!strcmp(name,f->files[i].name)){f->files[i].deleted=1;return 0;}
    return -1;
}
static bool live(void *context,const char *name)
{(void)context;return !strcmp(name,".anim_0123abcd.r5");}
int main(void)
{
    uint32_t crc;
    for(size_t i=0;i<sizeof(legacy_assets)/sizeof(*legacy_assets);i++){
        const legacy_asset_t *a=&legacy_assets[i];assert(legacy_asset_expected(a->name,a->bytes,&crc)&&crc==a->crc);
        assert(!legacy_asset_expected(a->name,a->bytes+1,&crc));
        char path[80];snprintf(path,sizeof(path),"v0123abcd/%s",a->name);assert(!legacy_asset_expected(path,a->bytes,&crc));
        snprintf(path,sizeof(path),"/%s",a->name);assert(!legacy_asset_expected(path,a->bytes,&crc));
    }
    assert(!legacy_asset_expected("config.json",176,&crc));
    assert(!legacy_asset_expected("my-image.jpg",6578,&crc));
    assert(media_storage_reliable_free(2583041,1947509,2818048)==166027);
    assert(media_storage_reliable_free(2583041,2041885,2818048)==71651);
    assert(media_storage_reliable_free(1000,1100,1200)==0);
    assert(media_storage_reliable_free(1000,950,10000)==50);
    const char *keep[]={"/data/v30ba1805","/data/ve640e6b7","/data/vdffe6960",NULL};
    assert(media_storage_kind("v30ba1805/0001.a5")==MEDIA_FILE);
    assert(media_storage_kind("/3_cleaned/_timing.txt")==MEDIA_FILE);
    assert(media_storage_kind("v30ba1805/_preview.png")==MEDIA_FILE);
    assert(media_storage_kind(".anim_0123abcd.r5.tmp")==MEDIA_CACHE);
    for(const char **p=(const char*[]){"config.json","index.html","../0001.jpg","v30ba1805/../0001.a5","v30ba1805/custom.txt","v30ba1805/sub/0001.a5","//1/0001.jpg",".anim_zzzzzzzz.r5",NULL};*p;p++)
        assert(media_storage_kind(*p)==MEDIA_OTHER);
    file_t files[]={{"v30ba1805/0001.a5",100,0},{"ve640e6b7/_preview.png",20,0},
        {"vdffe6960/_timing.txt",10,0},{"v30ba18050/0001.a5",200,0},
        {"1/0001.jpg",300,0},{"1/_timing.txt",4,0},{".anim_0123abcd.r5",80,0},
        {".anim_12345678.r5",90,0},{".anim_12345678.r5.tmp",5,0},{"config.json",15,0},
        {"wifi.js",30,0},{"1/custom.txt",12,0}};
    fixture_t f={files,sizeof(files)/sizeof(files[0]),0,NULL};media_storage_stats_t stats;
    memset(&stats,0,sizeof(stats));
    assert(media_storage_step(&f,next,remove_file,live,keep,1,&stats,1)==1);
    assert(f.cursor==1&&stats.files==1&&!files[0].deleted);
    f.cursor=0;
    assert(media_storage_run(&f,next,NULL,live,keep,false,&stats)==0);
    assert(stats.current_bytes==130&&stats.unused_bytes==504&&stats.cache_bytes==175&&stats.other_bytes==57);
    for(size_t i=0;i<f.count;i++)assert(!files[i].deleted);
    f.cursor=0;assert(media_storage_run(&f,next,remove_file,live,keep,false,&stats)==0);
    assert(stats.removed_files==5&&stats.removed_bytes==599);
    assert(!files[0].deleted&&!files[1].deleted&&!files[2].deleted&&!files[6].deleted);
    assert(!files[9].deleted&&!files[10].deleted&&!files[11].deleted);
    for(size_t i=0;i<f.count;i++)files[i].deleted=0;
    f.cursor=0;assert(media_storage_run(&f,next,remove_file,NULL,keep,true,&stats)==0);
    assert(stats.removed_files==9&&stats.removed_bytes==809);
    assert(files[0].deleted&&files[6].deleted&&!files[9].deleted);
    f.cursor=0;f.fail=files[3].name;
    assert(media_storage_run(&f,next,remove_file,live,keep,false,&stats)<0&&stats.errors==1);
    puts("Media cleanup: current/shared dirs protected, full clear, caches, config preservation, preview and partial failures: PASS");
}
