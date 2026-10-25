#include "task_status.h"
#include <string.h>
#include <ctype.h>
#include <limits.h>
static const char *modes[]={"all","project","session"};
typedef struct { char id[49],label[33]; } entry_t;
typedef struct { int project_count,session_count;entry_t projects[6],sessions[8]; } catalog_t;
static catalog_t s_catalog;
static uint32_t s_revision;
static bool integer(cJSON *o,const char *key,int max,int *out)
{
    cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    if(!cJSON_IsNumber(v)||v->valuedouble<0||v->valuedouble>max||v->valuedouble!=(int)v->valuedouble)return false;
    *out=v->valueint;return true;
}
static bool identifier(const char *s)
{
    if(!s||strlen(s)>48)return false;
    for(;*s;s++)if(!isalnum((unsigned char)*s)&&*s!='-'&&*s!='_'&&*s!='.'&&*s!=':')return false;
    return true;
}
bool task_scope_parse(cJSON *object,task_scope_t *scope)
{
    if(!cJSON_IsObject(object))return false;
    cJSON *mode=cJSON_GetObjectItemCaseSensitive(object,"mode"),*target=cJSON_GetObjectItemCaseSensitive(object,"target");
    if(!cJSON_IsString(mode)||!cJSON_IsString(target)||!identifier(target->valuestring))return false;
    int selected=-1;for(int i=0;i<3;i++)if(!strcmp(mode->valuestring,modes[i]))selected=i;
    if(selected<0||(selected&&!target->valuestring[0])||(!selected&&target->valuestring[0]))return false;
    scope->mode=selected;strlcpy(scope->target,target->valuestring,sizeof(scope->target));return true;
}
void task_scope_json(cJSON *parent,const char *key,const task_scope_t *scope)
{
    cJSON *o=cJSON_AddObjectToObject(parent,key);cJSON_AddStringToObject(o,"mode",modes[scope->mode]);cJSON_AddStringToObject(o,"target",scope->target);
}
bool task_scope_equal(const task_scope_t *a,const task_scope_t *b){return a->mode==b->mode&&!strcmp(a->target,b->target);}
int task_counts_state(const task_counts_t *c){return c->waiting?2:c->working?1:c->done?3:0;}
bool task_counts_parse(cJSON *request,int state,task_counts_t *counts)
{
    memset(counts,0,sizeof(*counts));cJSON *o=cJSON_GetObjectItemCaseSensitive(request,"counts");
    if(!o){if(state==1)counts->working=1;if(state==2)counts->waiting=1;if(state==3)counts->done=1;return true;}
    int seq=0;
    if(!cJSON_IsObject(o)||!integer(o,"working",9999,&counts->working)||!integer(o,"waiting",9999,&counts->waiting)||
       !integer(o,"done",9999,&counts->done)||!integer(request,"completion_seq",INT_MAX,&seq)|| (counts->done&&!seq))return false;
    counts->detailed=true;counts->completion_seq=seq;
    cJSON *scope=cJSON_GetObjectItemCaseSensitive(request,"scope");
    return (!scope||task_scope_parse(scope,&counts->scope))&&task_counts_state(counts)==state;
}
bool task_counts_equal(const task_counts_t *a,const task_counts_t *b)
{return a->working==b->working&&a->waiting==b->waiting&&a->done==b->done&&a->completion_seq==b->completion_seq&&task_scope_equal(&a->scope,&b->scope);}
static bool entries(cJSON *array,entry_t *out,int limit,int *count)
{
    if(!cJSON_IsArray(array)||cJSON_GetArraySize(array)>limit)return false;
    *count=cJSON_GetArraySize(array);
    for(int i=0;i<*count;i++){
        cJSON *item=cJSON_GetArrayItem(array,i),*id=cJSON_GetObjectItemCaseSensitive(item,"id"),*label=cJSON_GetObjectItemCaseSensitive(item,"label");
        if(!cJSON_IsString(id)||!identifier(id->valuestring)||!id->valuestring[0]||!cJSON_IsString(label)||strlen(label->valuestring)>32)return false;
        strlcpy(out[i].id,id->valuestring,sizeof(out[i].id));strlcpy(out[i].label,label->valuestring,sizeof(out[i].label));
    }
    return true;
}
bool task_catalog_update(cJSON *request)
{
    catalog_t next={0};
    if(!entries(cJSON_GetObjectItemCaseSensitive(request,"projects"),next.projects,6,&next.project_count)||
       !entries(cJSON_GetObjectItemCaseSensitive(request,"sessions"),next.sessions,8,&next.session_count))return false;
    if(memcmp(&next,&s_catalog,sizeof(next))){s_catalog=next;s_revision++;}return true;
}
static void output(cJSON *o,const char *key,const entry_t *entries,int count)
{
    cJSON *array=cJSON_AddArrayToObject(o,key);
    for(int i=0;i<count;i++){cJSON *item=cJSON_CreateObject();cJSON_AddStringToObject(item,"id",entries[i].id);cJSON_AddStringToObject(item,"label",entries[i].label);cJSON_AddItemToArray(array,item);}
}
void task_catalog_json(cJSON *reply)
{
    cJSON *o=cJSON_AddObjectToObject(reply,"catalog");output(o,"projects",s_catalog.projects,s_catalog.project_count);output(o,"sessions",s_catalog.sessions,s_catalog.session_count);
    cJSON_AddNumberToObject(reply,"catalog_revision",s_revision);
}
uint32_t task_catalog_revision(void){return s_revision;}
