#include "import_transaction.h"
#include "companion.h"
#include "cJSON.h"
#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
static char s_pending[10];
static bool valid(const char *s)
{
    if(!s||strlen(s)!=9||s[0]!='v')return false;
    for(int i=1;i<9;i++)if(!isxdigit((unsigned char)s[i]))return false;
    return true;
}
static bool selected(const char *dir)
{
    companion_config_t c;companion_get_config(&c);char path[24];snprintf(path,sizeof(path),"/data/%.9s",dir);
    for(int i=0;i<3;i++)if(!strcmp(path,c.media[i]))return true;
    return false;
}
static bool exists(const char *dir,const char *name)
{char path[48];struct stat st;snprintf(path,sizeof(path),"/data/%.9s/%s",dir,name);return stat(path,&st)==0;}
static int discard(const char *dir,bool incomplete)
{
    if(!valid(dir)||selected(dir))return -1;
    if(incomplete&&exists(dir,"_timing.txt"))return -1;
    if(!incomplete&&!exists(dir,"_upload.txt"))return -1;
    DIR *d=opendir("/data");if(!d)return -1;
    int count=0;struct dirent *entry;
    while((entry=readdir(d))){
        const char *name=entry->d_name;if(*name=='/')name++;
        if(strncmp(name,dir,9)||name[9]!='/')continue;
        char path[48];snprintf(path,sizeof(path),"/data/%.32s",name);
        if(unlink(path)==0)count++;else{closedir(d);return -1;}
    }
    closedir(d);if(!strcmp(dir,s_pending))s_pending[0]=0;return count;
}
static bool clear_marker(const char *dir)
{char path[48];snprintf(path,sizeof(path),"/data/%.9s/_upload.txt",dir);return unlink(path)==0;}
void import_recover(void)
{
    // Interrupted imports carry a marker. Selected directories are committed
    // even if their final HTTP acknowledgement was lost before marker removal.
    while(true){
        DIR *d=opendir("/data");if(!d)return;struct dirent *entry;char found[10]={0};
        while((entry=readdir(d))){
            const char *n=entry->d_name;if(*n=='/')n++;
            if(strlen(n)==21&&!strcmp(n+9,"/_upload.txt")){memcpy(found,n,9);if(valid(found))break;found[0]=0;}
        }
        closedir(d);if(!found[0])return;
        if(selected(found)){if(!clear_marker(found))return;}else if(discard(found,false)<0)return;
    }
}
void import_failed(const char *filename)
{
    if(s_pending[0]&&!strncmp(filename,s_pending,9)&&filename[9]=='/')discard(s_pending,false);
}
esp_err_t import_transaction_handler(httpd_req_t *req)
{
    char body[193]={0};int used=0;
    if(req->content_len<1||req->content_len>=sizeof(body))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid import command");
    while(used<req->content_len){int n=httpd_req_recv(req,body+used,req->content_len-used);if(n<=0)return ESP_FAIL;used+=n;}
    cJSON *o=cJSON_Parse(body),*d=cJSON_GetObjectItemCaseSensitive(o,"directory"),*op=cJSON_GetObjectItemCaseSensitive(o,"op");
    const char *error=NULL;int removed=0;
    if(!cJSON_IsString(d)||!valid(d->valuestring)||!cJSON_IsString(op))error="invalid import directory/operation";
    else {
        const char *dir=d->valuestring;
        if(!strcmp(op->valuestring,"begin")){
            if(s_pending[0]&&selected(s_pending)){clear_marker(s_pending);s_pending[0]=0;}
            if(s_pending[0])error="another import is still in progress";
            else if(selected(dir))error="selected directory cannot be reused";
            else {
                DIR *root=opendir("/data");if(!root)error="storage unavailable";
                struct dirent *entry;
                while(root&&(entry=readdir(root))){const char *n=entry->d_name;if(*n=='/')n++;if(!strncmp(n,dir,9)&&n[9]=='/')error="directory already exists";}
                if(root)closedir(root);
                if(!error){
                    char path[48];snprintf(path,sizeof(path),"/data/%.9s/_upload.txt",dir);FILE *f=fopen(path,"wb");
                    if(!f)error="cannot reserve upload marker";
                    else{bool ok=fwrite("VIBE-UPLOAD-1",1,13,f)==13;if(fclose(f))ok=false;if(!ok){unlink(path);error="cannot reserve upload marker";}else strlcpy(s_pending,dir,sizeof(s_pending));}
                }
            }
        }else if(!strcmp(op->valuestring,"commit")){
            if(!selected(dir))error="upload has not been selected";else{clear_marker(dir);if(!strcmp(s_pending,dir))s_pending[0]=0;}
        }else if(!strcmp(op->valuestring,"abort")){
            if(selected(dir)){clear_marker(dir);if(!strcmp(s_pending,dir))s_pending[0]=0;}
            else if(exists(dir,"_upload.txt")){removed=discard(dir,false);if(removed<0)error="upload cleanup failed";}
        }else if(!strcmp(op->valuestring,"discard_incomplete")){
            removed=discard(dir,true);if(removed<0)error="directory is selected, complete, or unavailable";
        }else error="unknown import operation";
    }
    cJSON_Delete(o);cJSON *r=cJSON_CreateObject();cJSON_AddBoolToObject(r,"ok",!error);cJSON_AddNumberToObject(r,"files_removed",removed);
    if(error)cJSON_AddStringToObject(r,"error",error);
    char *text=cJSON_PrintUnformatted(r);cJSON_Delete(r);
    httpd_resp_set_type(req,"application/json");if(error)httpd_resp_set_status(req,"400 Bad Request");
    esp_err_t result=httpd_resp_sendstr(req,text?text:"{\"ok\":false}");free(text);return result;
}
