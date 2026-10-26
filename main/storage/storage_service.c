#include "storage_service.h"
#include "companion.h"
#include "animation_crc.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "esp_private/esp_clk.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdatomic.h>

#define FILE_LIMIT (256*1024)
#define BLOCK_LIMIT 768
static SemaphoreHandle_t s_lock;
static char s_directory[10],s_import_token[17],s_file_token[17],s_filename[24],s_last_file[17];
static FILE *s_file;
static bool s_file_position_dirty;
static size_t s_written,s_length;
static uint32_t s_crc,s_expected_crc;
static int64_t s_last_access;
static atomic_uint s_revision;
static volatile bool s_maintenance;
static SemaphoreHandle_t s_report_lock;
static cJSON *s_report;
static bool s_report_running,s_report_failed;
static esp_err_t s_report_error;
static unsigned s_report_revision;
static SemaphoreHandle_t s_io_lock;
static volatile bool s_io_pending;
static char s_io_token[17];
static cJSON *s_io_request,*s_io_result;
static bool storage_command_sync(const cJSON *o,cJSON *r,const char **error);
static void storage_io_task(void *unused);
static const char *begin_import(const char *dir,cJSON *reply);
static bool file_path(const char *name,char full[80]);
static bool upload_room(size_t length)
{
    size_t total,used;
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,NULL);
    if(!p||esp_spiffs_info(NULL,&total,&used)!=ESP_OK)return false;
    size_t pages=(length+250)/251,index=1+(pages>103?(pages-103+123)/124:0);
    return media_storage_reliable_free(total,used,p->size)>=131072+(pages+index)*256;
}

static const char *str(const cJSON *o,const char *key)
{const cJSON *p=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(p)?p->valuestring:NULL;}
static bool number(const cJSON *o,const char *key,unsigned maximum,unsigned *out)
{const cJSON *p=cJSON_GetObjectItemCaseSensitive(o,key);if(!cJSON_IsNumber(p)||p->valuedouble<0||p->valuedouble>maximum||p->valuedouble!=(double)(unsigned)p->valuedouble)return false;*out=(unsigned)p->valuedouble;return true;}
static bool directory_valid(const char *dir)
{if(!dir||strlen(dir)!=9||*dir!='v')return false;for(int i=1;i<9;i++)if(!isxdigit((unsigned char)dir[i]))return false;return true;}
static bool selected(const char *dir)
{companion_config_t c;companion_get_config(&c);char p[24];snprintf(p,sizeof(p),"/data/%s",dir);for(int i=0;i<3;i++)if(!strcmp(p,c.media[i]))return true;return false;}
static void token(char out[17])
{snprintf(out,17,"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());}
static bool match(const char *a,const char *b){return a&&*b&&!strcmp(a,b);}
static bool file_crc_valid(FILE *file,uint32_t expected,size_t length)
{
    if(fflush(file)||fseek(file,0,SEEK_SET))return false;
    unsigned char bytes[256];uint32_t crc=0;size_t total=0,n;
    while((n=fread(bytes,1,sizeof(bytes),file))){crc=animation_crc32_le(crc,bytes,n);total+=n;}
    return !ferror(file)&&total==length&&crc==expected;
}
static void abort_file(void)
{if(s_file){fclose(s_file);s_file=NULL;}s_file_token[0]=0;s_filename[0]=0;}
static bool marker(const char *dir)
{char p[48];struct stat st;snprintf(p,sizeof(p),"/data/%s/_upload.txt",dir);return !stat(p,&st);}
static int discard(const char *dir)
{
    if(!directory_valid(dir)||selected(dir)||!marker(dir))return -1;
    DIR *d=opendir("/data");if(!d)return -1;struct dirent *e;int removed=0;
    for(;;){errno=0;e=readdir(d);if(!e){if(errno)removed=-1;break;}const char *n=e->d_name;if(*n=='/')n++;if(strncmp(n,dir,9)||n[9]!='/')continue;
        char p[80];if(snprintf(p,sizeof(p),"/data/%s",n)>=(int)sizeof(p)||unlink(p)){removed=-1;break;}removed++;}
    closedir(d);s_revision++;return removed;
}
static void finish_import(void)
{char p[48];snprintf(p,sizeof(p),"/data/%s/_upload.txt",s_directory);unlink(p);s_directory[0]=0;s_import_token[0]=0;s_revision++;}
static void recover(void)
{
    for(;;){DIR *d=opendir("/data");if(!d)return;struct dirent *e;char dir[10]={0};
        while((e=readdir(d))){const char *n=e->d_name;if(*n=='/')n++;if(strlen(n)==21&&!strcmp(n+9,"/_upload.txt")){memcpy(dir,n,9);if(directory_valid(dir))break;dir[0]=0;}}
        closedir(d);if(!*dir)return;
        if(selected(dir)){char p[48];snprintf(p,sizeof(p),"/data/%s/_upload.txt",dir);if(unlink(p))return;}
        else if(discard(dir)<0)return;
    }
}
esp_err_t storage_service_init(void)
{s_lock=xSemaphoreCreateMutex();s_report_lock=xSemaphoreCreateMutex();s_io_lock=xSemaphoreCreateMutex();if(!s_lock||!s_report_lock||!s_io_lock)return ESP_ERR_NO_MEM;recover();return ESP_OK;}
bool storage_service_lock(void){return s_lock&&xSemaphoreTake(s_lock,0)==pdTRUE;}
void storage_service_unlock(void){xSemaphoreGive(s_lock);}
bool storage_maintenance_busy(void){return s_maintenance;}
bool storage_async_busy(void){return s_io_pending;}
void storage_maintenance_set(bool active){s_maintenance=active;}
void storage_note_change(void){s_revision++;}
bool storage_import_busy(void)
{
    if(*s_directory&&esp_timer_get_time()-s_last_access>30000000){
        abort_file();if(selected(s_directory))finish_import();else if(discard(s_directory)>=0){s_directory[0]=0;s_import_token[0]=0;}
    }
    return *s_directory||s_file;
}

static esp_err_t http_json(httpd_req_t *req,const char *error,int removed)
{
    cJSON *r=cJSON_CreateObject();cJSON_AddBoolToObject(r,"ok",!error);
    cJSON_AddNumberToObject(r,"files_removed",removed);
    if(error){cJSON_AddStringToObject(r,"error",error);httpd_resp_set_status(req,"409 Conflict");}
    char *text=cJSON_PrintUnformatted(r);cJSON_Delete(r);if(!text)return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json");esp_err_t ret=httpd_resp_sendstr(req,text);free(text);return ret;
}
esp_err_t storage_http_import(httpd_req_t *req)
{
    char body[193]={0};if(req->content_len<1||req->content_len>=sizeof(body))return http_json(req,"invalid import command",0);
    int used=0;while(used<req->content_len){int n=httpd_req_recv(req,body+used,req->content_len-used);if(n<=0)return ESP_FAIL;used+=n;}
    cJSON *o=cJSON_Parse(body);const char *dir=str(o,"directory"),*op=str(o,"op"),*error=NULL;int removed=0;
    if(!directory_valid(dir)||!op){cJSON_Delete(o);return http_json(req,"invalid import directory/operation",0);}
    if(!storage_service_lock()){cJSON_Delete(o);return http_json(req,"storage busy; retry",0);}
    if(s_maintenance){storage_service_unlock();cJSON_Delete(o);return http_json(req,"cleanup in progress",0);}
    if(!strcmp(op,"begin")){cJSON *r=cJSON_CreateObject();error=begin_import(dir,r);cJSON_Delete(r);}
    else if(!strcmp(op,"commit")){
        if(!selected(dir))error="upload has not been selected";
        else if(!strcmp(dir,s_directory)&&!s_file)finish_import();
        else if(marker(dir))error="another import is in progress";
    }else if(!strcmp(op,"abort")){
        if(!strcmp(dir,s_directory))abort_file();
        if(selected(dir)){if(!strcmp(dir,s_directory))finish_import();}
        else if(marker(dir)){removed=discard(dir);if(removed<0)error="upload cleanup failed";}
        if(!error&&!strcmp(dir,s_directory)){s_directory[0]=0;s_import_token[0]=0;}
    }else error="unknown import operation";
    storage_service_unlock();cJSON_Delete(o);return http_json(req,error,removed);
}
esp_err_t storage_http_upload(httpd_req_t *req)
{
    const char *name=req->uri+strlen("/upload/");char path[80];
    if(!storage_service_lock())return http_json(req,"storage busy; retry",0);
    if(s_maintenance||req->content_len<1||req->content_len>FILE_LIMIT||strlen(name)<11||
       !*s_directory||strncmp(name,s_directory,9)||name[9]!='/'||s_file||!file_path(name+10,path)){
        storage_service_unlock();return http_json(req,"invalid upload or import busy",0);
    }
    struct stat st;if(!stat(path,&st)){storage_service_unlock();return http_json(req,"file already exists",0);}
    if(!upload_room(req->content_len)){storage_service_unlock();return http_json(req,"safe SPIFFS upload budget exhausted; clear unused media",0);}
    FILE *f=fopen(path,"wb");const char *error=f?NULL:"cannot create upload file";
    char buffer[1024];int received=0;while(!error&&received<req->content_len){
        int amount=req->content_len-received;if(amount>(int)sizeof(buffer))amount=sizeof(buffer);
        int n=httpd_req_recv(req,buffer,amount);if(n<=0){error="upload receive failed";break;}
        received+=n;if(fwrite(buffer,1,n,f)!=(size_t)n)error="upload write failed";
    }
    if(f&&fclose(f))error="upload flush failed";
    if(error){unlink(path);if(discard(s_directory)>=0){s_directory[0]=0;s_import_token[0]=0;}}
    else{s_last_access=esp_timer_get_time();s_revision++;}
    storage_service_unlock();return http_json(req,error,0);
}

esp_err_t storage_system_json(cJSON *r)
{
    size_t total,used;uint32_t flash;esp_chip_info_t chip;esp_chip_info(&chip);
    if(esp_spiffs_info(NULL,&total,&used)!=ESP_OK||used>total||esp_flash_get_size(NULL,&flash)!=ESP_OK)return ESP_FAIL;
    const esp_app_desc_t *a=esp_app_get_description();char sha[65],stamp[40];
    for(int i=0;i<32;i++)snprintf(sha+i*2,3,"%02x",a->app_elf_sha256[i]);
    snprintf(stamp,sizeof(stamp),"%s %s",a->date,a->time);
    cJSON_AddStringToObject(r,"project_name",a->project_name);cJSON_AddStringToObject(r,"app_version",a->version);
    cJSON_AddStringToObject(r,"compile_time",stamp);cJSON_AddStringToObject(r,"idf_version",a->idf_ver);cJSON_AddStringToObject(r,"elf_sha256",sha);
    cJSON_AddNumberToObject(r,"chip_revision",chip.revision);cJSON_AddNumberToObject(r,"chip_cores",chip.cores);
    cJSON_AddNumberToObject(r,"cpu_freq_hz",esp_clk_cpu_freq());cJSON_AddNumberToObject(r,"flash_size_mb",flash/1048576);
    cJSON_AddNumberToObject(r,"free_heap",esp_get_free_heap_size());
    cJSON_AddNumberToObject(r,"spiffs_total",total);cJSON_AddNumberToObject(r,"spiffs_used",used);cJSON_AddNumberToObject(r,"spiffs_free",total-used);
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,NULL);
    if(p){cJSON_AddNumberToObject(r,"storage_offset",p->address);cJSON_AddNumberToObject(r,"storage_partition_bytes",p->size);
        size_t free=media_storage_reliable_free(total,used,p->size);
        cJSON_AddNumberToObject(r,"spiffs_reliable_free",free);cJSON_AddNumberToObject(r,"spiffs_upload_budget",free>131072?free-131072:0);}
    return ESP_OK;
}
typedef struct {DIR *dir;media_storage_live_t live;void *live_context;int64_t deadline;} walk_t;
static int walk_next(void *context,char *name,size_t length,size_t *size)
{
    walk_t *w=context;if(esp_timer_get_time()>w->deadline)return -2;
    errno=0;struct dirent *e=readdir(w->dir);if(!e)return errno?-1:0;
    const char *n=e->d_name;if(*n=='/')n++;if(strlen(n)>=length)return -1;
    strcpy(name,n);char p[80];struct stat st;
    if(snprintf(p,sizeof(p),"/data/%s",n)>=(int)sizeof(p)||stat(p,&st)||st.st_size<0)return -1;
    *size=(size_t)st.st_size;return 1;
}
static int walk_remove(void *context,const char *name)
{(void)context;char p[80];if(snprintf(p,sizeof(p),"/data/%s",name)>=(int)sizeof(p))return -1;int result=unlink(p);if(!result)s_revision++;vTaskDelay(1);return result;}
static bool walk_live(void *context,const char *name)
{walk_t *w=context;return w->live&&w->live(w->live_context,name);}
void *storage_cleanup_open(media_storage_live_t live,void *ctx)
{
    walk_t *w=calloc(1,sizeof(*w));if(!w)return NULL;
    *w=(walk_t){.dir=opendir("/data"),.live=live,.live_context=ctx,.deadline=esp_timer_get_time()+30000000};
    if(!w->dir){free(w);return NULL;}return w;
}
int storage_cleanup_step(void *ctx,const char *const *keep,int mode,media_storage_stats_t *stats)
{return media_storage_step(ctx,walk_next,walk_remove,walk_live,keep,mode,stats,4);}
void storage_cleanup_close(void *ctx)
{walk_t *w=ctx;if(w){closedir(w->dir);free(w);}}
static const char *const bucket_names[]={"current","uploaded","incomplete","cache","legacy_named","unknown"};
typedef struct {char names[32][33];bool pending[32];unsigned count,cursor;} bucket_cache_t;
static unsigned bucket(const char *name,const companion_config_t *config,bucket_cache_t *cache)
{
    media_file_kind_t kind=media_storage_kind(name);
    if(kind==MEDIA_CACHE)return 3;
    if(kind==MEDIA_FILE){
        for(int i=0;i<3;i++){
            const char *dir=config->media[i];if(!strncmp(dir,"/data/",6))dir+=6;
            size_t n=strlen(dir);if(n&&!strncmp(name,dir,n)&&name[n]=='/')return 0;
        }
        const char *slash=strchr(name,'/');char dir[33]={0};
        if(slash&&slash-name<32){
            memcpy(dir,name,slash-name);
            for(unsigned i=0;i<cache->count;i++)if(!strcmp(dir,cache->names[i]))return cache->pending[i]?2:1;
            unsigned index=cache->count<32?cache->count++:(cache->cursor++%32);
            strcpy(cache->names[index],dir);cache->pending[index]=marker(dir);
            if(cache->pending[index])return 2;
        }
        return 1;
    }
    static const char *const legacy[]={"index.html","styles.css","script.js","ws-core.js","gif-tool.js","media.js","panel.js","wifi.js","upload.js","sysinfo.js","audio-record.js","audio-playback.js","favicon.ico","ap-mode.jpg","no-wifi.jpg","wifi-connected.jpg","wifi-connecting.jpg",NULL};
    for(int i=0;legacy[i];i++)if(!strcmp(name,legacy[i]))return 4;
    return 5;
}
static esp_err_t report(cJSON *r)
{
    if(storage_system_json(r)!=ESP_OK)return ESP_FAIL;
    companion_config_t c;companion_get_config(&c);
    bucket_cache_t cache={0};
    walk_t w={.dir=opendir("/data"),.deadline=esp_timer_get_time()+5000000};if(!w.dir)return ESP_FAIL;
    size_t bytes[6]={0},size;unsigned files[6]={0},count=0;char name[64];int result;
    while((result=walk_next(&w,name,sizeof(name),&size))>0){unsigned k=bucket(name,&c,&cache);bytes[k]+=size;files[k]++;count++;if(!(count%4))vTaskDelay(1);}
    closedir(w.dir);if(result<0)return result==-2?ESP_ERR_TIMEOUT:ESP_FAIL;
    cJSON *o=cJSON_AddObjectToObject(r,"storage");
    cJSON_AddNumberToObject(o,"current_media_bytes",bytes[0]);cJSON_AddNumberToObject(o,"other_media_bytes",bytes[1]+bytes[2]);
    cJSON_AddNumberToObject(o,"cache_bytes",bytes[3]);cJSON_AddNumberToObject(o,"system_unknown_bytes",bytes[4]+bytes[5]);
    size_t logical=0;cJSON *categories=cJSON_AddObjectToObject(o,"categories");
    for(int i=0;i<6;i++){logical+=bytes[i];cJSON *group=cJSON_AddObjectToObject(categories,bucket_names[i]);cJSON_AddNumberToObject(group,"bytes",bytes[i]);cJSON_AddNumberToObject(group,"files",files[i]);}
    cJSON_AddNumberToObject(o,"logical_bytes",logical);cJSON_AddNumberToObject(o,"files",count);
    cJSON_AddNumberToObject(o,"revision",s_revision);return ESP_OK;
}
static void report_task(void *unused)
{
    (void)unused;unsigned revision=s_revision;cJSON *snapshot=cJSON_CreateObject();
    esp_err_t result=snapshot?report(snapshot):ESP_ERR_NO_MEM;
    bool failed=result!=ESP_OK||revision!=s_revision;
    xSemaphoreTake(s_report_lock,portMAX_DELAY);
    cJSON_Delete(s_report);s_report=failed?NULL:snapshot;if(failed)cJSON_Delete(snapshot);
    s_report_revision=revision;s_report_error=result;s_report_failed=failed;s_report_running=false;
    xSemaphoreGive(s_report_lock);vTaskDelete(NULL);
}
static esp_err_t report_request(cJSON *reply)
{
    xSemaphoreTake(s_report_lock,portMAX_DELAY);
    if(!s_report_running&&s_report_revision==s_revision&&(s_report||s_report_failed)){
        if(s_report_failed){s_report_failed=false;esp_err_t error=s_report_error;xSemaphoreGive(s_report_lock);return error==ESP_OK?ESP_FAIL:error;}
        cJSON *copy=cJSON_Duplicate(s_report,true);
        if(!copy){xSemaphoreGive(s_report_lock);return ESP_ERR_NO_MEM;}
        while(copy->child){cJSON *item=cJSON_DetachItemViaPointer(copy,copy->child);char key[40];strlcpy(key,item->string,sizeof(key));cJSON_AddItemToObject(reply,key,item);}
        cJSON_Delete(copy);xSemaphoreGive(s_report_lock);return ESP_OK;
    }
    if(!s_report_running){
        s_report_running=true;
        // Match AnimTask's priority: three continuously active displays can
        // otherwise starve a lower-priority scan until its deadline expires.
        // The scan yields every four entries; USB state handling remains at 6.
        if(xTaskCreate(report_task,"storage_report",8192,NULL,5,NULL)!=pdPASS){s_report_running=false;xSemaphoreGive(s_report_lock);return ESP_ERR_NO_MEM;}
    }
    cJSON_AddBoolToObject(reply,"pending",true);xSemaphoreGive(s_report_lock);return ESP_OK;
}
static esp_err_t list(cJSON *r,unsigned cursor,unsigned limit)
{
    walk_t w={.dir=opendir("/data"),.deadline=esp_timer_get_time()+5000000};if(!w.dir)return ESP_FAIL;
    cJSON *files=cJSON_AddArrayToObject(r,"files");char name[64];size_t size;unsigned i=0;int result;
    companion_config_t config;companion_get_config(&config);
    bucket_cache_t cache={0};
    // Skip the prefix using directory metadata only; do not stat every earlier
    // file again for each page (quadratic flash lookup cost).
    result=1;
    while(i<cursor){errno=0;if(!readdir(w.dir)){result=errno?-1:0;break;}i++;}
    while(result>0&&(result=walk_next(&w,name,sizeof(name),&size))>0){
        if(i++>=cursor+limit)break;
        cJSON *f=cJSON_CreateObject();cJSON_AddStringToObject(f,"name",name);cJSON_AddNumberToObject(f,"bytes",size);
        cJSON_AddNumberToObject(f,"kind",media_storage_kind(name));cJSON_AddStringToObject(f,"category",bucket_names[bucket(name,&config,&cache)]);cJSON_AddItemToArray(files,f);
    }
    closedir(w.dir);if(result<0)return ESP_FAIL;
    cJSON_AddBoolToObject(r,"complete",result==0);cJSON_AddNumberToObject(r,"next_cursor",cursor+cJSON_GetArraySize(files));
    cJSON_AddNumberToObject(r,"revision",s_revision);return ESP_OK;
}
static const char *begin_import(const char *dir,cJSON *r)
{
    if(!directory_valid(dir))return "invalid import directory";
    if(*s_directory&&selected(s_directory)&&!s_file)finish_import();
    if(storage_import_busy())return "another import is still in progress";
    if(selected(dir))return "selected directory cannot be reused";
    if(!upload_room(13))return "safe SPIFFS upload budget exhausted; clear unused media";
    DIR *d=opendir("/data");if(!d)return "storage unavailable";struct dirent *e;bool exists=false,scan_failed=false;
    for(;;){errno=0;e=readdir(d);if(!e){scan_failed=errno!=0;break;}const char *n=e->d_name;if(*n=='/')n++;if(!strncmp(n,dir,9)&&n[9]=='/'){exists=true;break;}}
    closedir(d);if(scan_failed)return "storage enumeration failed";if(exists)return "directory already exists";
    char p[48];snprintf(p,sizeof(p),"/data/%s/_upload.txt",dir);FILE *f=fopen(p,"wb");if(!f)return "cannot reserve upload marker";
    bool ok=fwrite("VIBE-UPLOAD-1",1,13,f)==13;if(fclose(f))ok=false;if(!ok){unlink(p);return "cannot reserve upload marker";}
    strcpy(s_directory,dir);token(s_import_token);s_last_access=esp_timer_get_time();s_revision++;
    cJSON_AddStringToObject(r,"token",s_import_token);return NULL;
}
static bool file_path(const char *name,char full[80])
{if(!name||!*s_directory||strlen(name)>=sizeof(s_filename)||!strcmp(name,"_upload.txt"))return false;
    char flatname[64];snprintf(flatname,sizeof(flatname),"%s/%s",s_directory,name);
    if(media_storage_kind(flatname)!=MEDIA_FILE)return false;
    snprintf(full,80,"/data/%s",flatname);return true;}
static const char *read_file(const cJSON *o,cJSON *r)
{
    const char *p=str(o,"path");unsigned offset,size;
    if(!p||strncmp(p,"/data/",6)||media_storage_kind(p+6)!=MEDIA_FILE||strstr(p,"/_upload.txt")||
       !number(o,"offset",FILE_LIMIT,&offset)||!number(o,"size",BLOCK_LIMIT,&size)||!size)return "invalid read path/range";
    FILE *f=fopen(p,"rb");if(!f)return "file unavailable";struct stat st;
    if(stat(p,&st)||st.st_size>FILE_LIMIT||offset>(unsigned)st.st_size||fseek(f,offset,SEEK_SET)){fclose(f);return "invalid read offset";}
    unsigned char raw[BLOCK_LIMIT],encoded[1025];size_t n=fread(raw,1,size,f),encoded_size=0;bool failed=ferror(f);fclose(f);
    if(failed||mbedtls_base64_encode(encoded,sizeof(encoded),&encoded_size,raw,n))return "file read failed";
    encoded[encoded_size]=0;cJSON_AddStringToObject(r,"data",(char*)encoded);cJSON_AddNumberToObject(r,"offset",offset);
    cJSON_AddNumberToObject(r,"length",st.st_size);cJSON_AddBoolToObject(r,"eof",offset+n==(size_t)st.st_size);return NULL;
}
static bool storage_command_sync(const cJSON *o,cJSON *r,const char **error)
{
    const char *op=str(o,"op");if(!op)return false;
    bool known=!strcmp(op,"sysinfo")||!strcmp(op,"storage_info")||!strcmp(op,"storage_list")||!strcmp(op,"file_read")||
        !strncmp(op,"import_",7)||!strcmp(op,"file_begin")||!strcmp(op,"file_chunk")||!strcmp(op,"file_end")||!strcmp(op,"file_status");
    if(!known)return false;
    if(!storage_service_lock()){*error="storage busy; retry";return true;}
    if(s_maintenance&&strcmp(op,"sysinfo")){*error="cleanup in progress";storage_service_unlock();return true;}
    if(!strcmp(op,"sysinfo")){if(storage_system_json(r)!=ESP_OK)*error="storage statistics unavailable";}
    else if(!strcmp(op,"storage_info")){esp_err_t status=report_request(r);if(status!=ESP_OK)*error=status==ESP_ERR_TIMEOUT?"storage inventory scan timed out; retry":status==ESP_ERR_NO_MEM?"storage inventory allocation failed; retry":"storage inventory scan failed; retry";}
    else if(!strcmp(op,"storage_list")){unsigned cursor,limit;if(!number(o,"cursor",65535,&cursor)||!number(o,"limit",8,&limit)||!limit)*error="invalid list page";else if(list(r,cursor,limit)!=ESP_OK)*error="storage enumeration failed";}
    else if(!strcmp(op,"file_read"))*error=read_file(o,r);
    else if(!strcmp(op,"import_begin"))*error=begin_import(str(o,"directory"),r);
    else if(!strcmp(op,"import_commit")||!strcmp(op,"import_abort")){
        if(!match(str(o,"token"),s_import_token))*error="unknown import token";
        else if(!strcmp(op,"import_commit")){if(s_file||!selected(s_directory))*error="upload has not been selected";else finish_import();}
        else{abort_file();if(selected(s_directory))finish_import();else if(discard(s_directory)<0)*error="upload cleanup failed";else{s_directory[0]=0;s_import_token[0]=0;}}
    }else if(!strcmp(op,"file_begin")){
        unsigned length,crc;char path[80];struct stat st;
        if(!match(str(o,"token"),s_import_token)||!file_path(str(o,"name"),path)||!number(o,"length",FILE_LIMIT,&length)||!length||!number(o,"crc32",UINT32_MAX,&crc))*error="invalid file begin";
        else if(s_file)*error="another file is open";
        else if(!upload_room(length)){*error="safe SPIFFS upload budget exhausted; clear unused media";cJSON_AddNumberToObject(r,"errno",ENOSPC);}
        else if(!stat(path,&st))*error="file already exists";
        else if(!(s_file=fopen(path,"w+b")))*error="cannot create upload file";
        else{strcpy(s_filename,str(o,"name"));token(s_file_token);s_length=length;s_written=0;s_crc=0;s_expected_crc=crc;s_file_position_dirty=false;s_last_access=esp_timer_get_time();cJSON_AddStringToObject(r,"file_token",s_file_token);}
    }else if(!strcmp(op,"file_chunk")){
        unsigned offset,crc;unsigned char raw[BLOCK_LIMIT],old[BLOCK_LIMIT];size_t n=0;const char *data=str(o,"data");
        if(!s_file||!match(str(o,"file_token"),s_file_token)||!number(o,"offset",FILE_LIMIT,&offset)||!number(o,"crc32",UINT32_MAX,&crc)||!data||strlen(data)>1024||mbedtls_base64_decode(raw,sizeof(raw),&n,(const unsigned char*)data,strlen(data))||!n||animation_crc32_le(0,raw,n)!=crc)*error="invalid file chunk/CRC";
        else if(offset>s_written||n>s_length-offset)*error="invalid chunk offset/length";
        else if(offset<s_written){s_file_position_dirty=true;if(offset+n>s_written||fseek(s_file,offset,SEEK_SET)||fread(old,1,n,s_file)!=n||memcmp(old,raw,n))*error="conflicting duplicate chunk";}
        // Sequential chunks are already positioned at the end. Seeking on
        // every block flushes SPIFFS' partial page and amplifies metadata/GC.
        else if(s_file_position_dirty&&fseek(s_file,s_written,SEEK_SET)){*error="upload seek failed";cJSON_AddNumberToObject(r,"errno",errno);}
        else if(fwrite(raw,1,n,s_file)!=n){*error="upload data write failed";cJSON_AddNumberToObject(r,"errno",errno);}
        else if(fflush(s_file)){*error="upload flush failed";cJSON_AddNumberToObject(r,"errno",errno);}
        else{s_file_position_dirty=false;s_written+=n;s_crc=animation_crc32_le(s_crc,raw,n);s_revision++;}
        s_last_access=esp_timer_get_time();cJSON_AddNumberToObject(r,"offset",s_written);
    }else if(!strcmp(op,"file_status")){
        if(!match(str(o,"file_token"),s_file_token))*error="unknown file token";else{cJSON_AddNumberToObject(r,"offset",s_written);cJSON_AddNumberToObject(r,"length",s_length);}
    }else if(!strcmp(op,"file_end")){
        const char *t=str(o,"file_token");if(match(t,s_last_file)){}
        else if(!s_file||!match(t,s_file_token))*error="unknown file token";
        else if(s_written!=s_length||s_crc!=s_expected_crc||!file_crc_valid(s_file,s_expected_crc,s_length))*error="incomplete file/CRC";
        else{bool ok=!fflush(s_file);if(fclose(s_file))ok=false;s_file=NULL;if(!ok)*error="file flush failed";else{strcpy(s_last_file,s_file_token);s_file_token[0]=0;s_revision++;}}
    }else *error="unknown storage operation";
    storage_service_unlock();return true;
}

static void storage_io_task(void *unused)
{
    (void)unused;cJSON *result=cJSON_CreateObject();const char *error=NULL;
    if(result)storage_command_sync(s_io_request,result,&error);
    if(error)cJSON_AddStringToObject(result,"error",error);
    cJSON_Delete(s_io_request);
    xSemaphoreTake(s_io_lock,portMAX_DELAY);
    s_io_request=NULL;s_io_result=result;s_io_pending=false;
    xSemaphoreGive(s_io_lock);vTaskDelete(NULL);
}

bool storage_service_command(const cJSON *o,cJSON *r,const char **error)
{
    const char *op=str(o,"op");if(!op)return false;
    bool status=!strcmp(op,"storage_job_status");
    bool writing=!strncmp(op,"import_",7)||!strcmp(op,"file_begin")||!strcmp(op,"file_chunk")||!strcmp(op,"file_end");
    if(!status&&!writing)return storage_command_sync(o,r,error);
    if(xSemaphoreTake(s_io_lock,0)!=pdTRUE){*error="storage I/O busy; retry";return true;}
    if(status){
        if(!match(str(o,"job_token"),s_io_token))*error="unknown storage I/O job";
        else if(s_io_pending){cJSON_AddBoolToObject(r,"pending",true);cJSON_AddStringToObject(r,"storage_job",s_io_token);}
        else if(!s_io_result)*error="storage I/O allocation failed";
        else{
            cJSON *copy=cJSON_Duplicate(s_io_result,true);
            if(!copy)*error="storage I/O allocation failed";
            else{
                while(copy->child){cJSON *item=cJSON_DetachItemViaPointer(copy,copy->child);char key[40];strlcpy(key,item->string,sizeof(key));cJSON_AddItemToObject(r,key,item);}
                cJSON_Delete(copy);if(cJSON_HasObjectItem(r,"error"))*error="storage operation failed";
            }
        }
    }else if(s_io_pending)*error="storage I/O busy; retry";
    else{
        s_io_request=cJSON_Duplicate(o,true);
        if(!s_io_request)*error="storage I/O allocation failed";
        else{
            cJSON_Delete(s_io_result);s_io_result=NULL;token(s_io_token);s_io_pending=true;
            if(xTaskCreate(storage_io_task,"storage_io",8192,NULL,5,NULL)!=pdPASS){
                cJSON_Delete(s_io_request);s_io_request=NULL;s_io_pending=false;s_io_token[0]=0;*error="storage I/O allocation failed";
            }else{cJSON_AddBoolToObject(r,"pending",true);cJSON_AddStringToObject(r,"storage_job",s_io_token);}
        }
    }
    xSemaphoreGive(s_io_lock);return true;
}
