#include "media_storage.h"
#include <ctype.h>
#include <string.h>

size_t media_storage_reliable_free(size_t total,size_t used,size_t partition_bytes)
{
    if(used>total)return 0;
    size_t limit=(partition_bytes/4)*3;
    if(used>=limit)return 0;
    size_t available=limit-used;
    return available<total-used?available:total-used;
}

static const char *flat(const char *name) { return name && *name=='/' ? name+1 : name; }
media_file_kind_t media_storage_kind(const char *path)
{
    const char *name=flat(path);
    if(!name||!*name||strstr(name,"..")||*name=='/')return MEDIA_OTHER;
    size_t length=strlen(name);
    if((length==17||length==21)&&!strncmp(name,".anim_",6)&&
       (!strcmp(name+14,".r5")||!strcmp(name+14,".r5.tmp"))){
        for(int i=6;i<14;i++)if(!isxdigit((unsigned char)name[i]))return MEDIA_OTHER;
        return MEDIA_CACHE;
    }
    const char *slash=strchr(name,'/');
    if(!slash||slash==name||slash-name>31||strchr(slash+1,'/'))return MEDIA_OTHER;
    for(const char *p=name;p<slash;p++)if(!isalnum((unsigned char)*p)&&*p!='_'&&*p!='-')return MEDIA_OTHER;
    const char *base=slash+1;
    if(!strcmp(base,"_timing.txt")||!strcmp(base,"_preview.png")||!strcmp(base,"_upload.txt"))return MEDIA_FILE;
    if(strlen(base)!=7&&strlen(base)!=8)return MEDIA_OTHER;
    for(int i=0;i<4;i++)if(!isdigit((unsigned char)base[i]))return MEDIA_OTHER;
    return !strcmp(base+4,".a5")||!strcmp(base+4,".jpg") ? MEDIA_FILE : MEDIA_OTHER;
}
static bool current(const char *path,const char *const *keep)
{
    const char *name=flat(path);
    if(!keep)return false;
    for(size_t i=0;keep[i];i++){
        const char *dir=keep[i];if(!strncmp(dir,"/data/",6))dir+=6;else dir=flat(dir);
        size_t length=strlen(dir);
        if(length&&!strncmp(name,dir,length)&&name[length]=='/')return true;
    }
    return false;
}
int media_storage_step(void *context,media_storage_next_t next,
                      media_storage_remove_t remove_file,media_storage_live_t live_cache,
                      const char *const *keep,int mode,media_storage_stats_t *stats,unsigned budget)
{
    char name[64];size_t size;int result=1;
    for(unsigned i=0;i<budget;i++){
        result=next(context,name,sizeof(name),&size);if(result<=0)break;
        stats->files++;media_file_kind_t kind=media_storage_kind(name);bool selected=current(name,keep);
        if(kind==MEDIA_FILE){if(selected)stats->current_bytes+=size;else stats->unused_bytes+=size;}
        else if(kind==MEDIA_CACHE)stats->cache_bytes+=size;
        else stats->other_bytes+=size;
        bool candidate=(kind==MEDIA_FILE&&mode!=3&&(mode==2||!selected))||
            (kind==MEDIA_CACHE&&(!live_cache||!live_cache(context,name)));
        if(remove_file&&candidate){
            if(remove_file(context,name)){stats->errors++;continue;}
            stats->removed_files++;stats->removed_bytes+=size;
        }
    }
    if(result<0)stats->errors++;
    return result>0?1:stats->errors?-1:0;
}
int media_storage_run(void *context,media_storage_next_t next,
                      media_storage_remove_t remove_file,media_storage_live_t live_cache,
                      const char *const *keep,bool all,media_storage_stats_t *stats)
{
    memset(stats,0,sizeof(*stats));int result;
    do{result=media_storage_step(context,next,remove_file,live_cache,keep,all?2:1,stats,8);}while(result>0);
    return result;
}
