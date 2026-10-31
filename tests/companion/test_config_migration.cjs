const {spawnSync}=require('node:child_process'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const root=path.resolve(__dirname,'../..'),tmp=fs.mkdtempSync(path.join(os.tmpdir(),'vibe-config-test-'));
function extract(file,start,end){const s=fs.readFileSync(path.join(root,file),'utf8');return s.slice(s.indexOf(start),s.indexOf(end,s.indexOf(start)));}
function run(cmd,args){const r=spawnSync(cmd,args,{stdio:'inherit'});if(r.error)throw r.error;if(r.status)throw Error('Config fixture failed: '+r.status);}
try{
 fs.writeFileSync(path.join(tmp,'esp_err.h'),'#pragma once\ntypedef int esp_err_t;\n');
 const source=`#include "companion.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
static const char *modes[]={"all","project","session"};
static const char *idle_names[]={"off","colors","black"};
static const char *link_names[]={"independent","sync","stagger"};
static const char *signal_names[]={"steady","task_cycle"};
static const char *sound_names[]={"original","8bit"};
companion_config_t s_config;
static size_t strlcpy(char *out,const char *in,size_t size){size_t n=strlen(in);if(size){size_t m=n<size-1?n:size-1;memcpy(out,in,m);out[m]=0;}return n;}
bool task_scope_parse(cJSON *o,task_scope_t *scope){(void)o;(void)scope;return false;}
static int fail_at=-1,attempts,live;
static void *allocate(size_t n){if(attempts++==fail_at)return NULL;void *p=malloc(n);if(p)live++;return p;}
static void release(void *p){if(p){live--;free(p);}}
${extract('main/companion/task_status.c','void task_scope_json(', 'bool task_scope_equal(')}
${extract('main/companion/companion.c','static cJSON *config_json(', 'static bool integer_field(')}
${extract('main/companion/companion.c','static bool integer_field(', 'esp_err_t companion_init(')}
int main(void){
 cJSON_Hooks hooks={allocate,release};cJSON_InitHooks(&hooks);
 companion_config_t c={.colors={63488,65504,2016},.foreground_size=112,.brightness=26,.volume=40,.idle_playback=IDLE_PLAYBACK_BLACK,.idle_done_delay=10,.media={"/data/one","/data/two","/data/three"}};
 cJSON *good=config_json(&c);assert(good);int allocations=attempts;
 assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(good,"media"))==3);
 assert(!strcmp(cJSON_GetObjectItemCaseSensitive(good,"signal_pattern")->valuestring,"steady"));
 cJSON *style=cJSON_GetObjectItemCaseSensitive(good,"sound_style");assert(style&&!strcmp(style->valuestring,"original"));
 cJSON_Delete(good);assert(!live);
 for(int i=0;i<allocations;i++){
  fail_at=i;attempts=0;cJSON *o=config_json(&c);assert(!o);assert(!live);
 }
 fail_at=-1;attempts=0;good=config_json(&c);assert(good);
 char *text=cJSON_PrintUnformatted(good);assert(text);cJSON_free(text);cJSON_Delete(good);assert(!live);
 cJSON *setting=cJSON_Parse("{\\"signal_pattern\\":\\"task_cycle\\",\\"layout\\":\\"all\\"}");assert(setting);
 assert(parse_config(setting,&c) && c.signal_pattern==SIGNAL_TASK_CYCLE && !c.all_screens);cJSON_Delete(setting);
 good=config_json(&c);assert(!strcmp(cJSON_GetObjectItemCaseSensitive(good,"signal_pattern")->valuestring,"task_cycle"));cJSON_Delete(good);
 const char *invalid[]={"{\\"signal_pattern\\":true}","{\\"signal_pattern\\":\\"unknown\\"}"};
 for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){setting=cJSON_Parse(invalid[i]);assert(!parse_config(setting,&c));cJSON_Delete(setting);}
 setting=cJSON_Parse("{\\"signal_pattern\\":\\"steady\\",\\"layout\\":\\"all\\"}");assert(parse_config(setting,&c) && c.all_screens);cJSON_Delete(setting);
 setting=cJSON_Parse("{}");assert(parse_config(setting,&c) && c.signal_pattern==SIGNAL_STEADY && c.all_screens);cJSON_Delete(setting);assert(!live);
 setting=cJSON_Parse("{\\"sound_style\\":\\"8bit\\"}");assert(parse_config(setting,&c)&&c.sound_style==NOTIFY_8BIT);cJSON_Delete(setting);
 setting=cJSON_Parse("{\\"brightness\\":35}");assert(parse_config(setting,&c)&&c.sound_style==NOTIFY_8BIT);cJSON_Delete(setting);
 setting=cJSON_Parse("{}");assert(parse_stored_config(setting,&c)&&c.sound_style==NOTIFY_ORIGINAL);cJSON_Delete(setting);
 setting=cJSON_Parse("{\\"sound_style\\":\\"8bit\\"}");assert(parse_stored_config(setting,&c)&&c.sound_style==NOTIFY_8BIT);cJSON_Delete(setting);
 setting=cJSON_Parse("{\\"sound_style\\":\\"invalid\\"}");assert(!parse_config(setting,&c));cJSON_Delete(setting);assert(!live);
 setting=cJSON_Parse("{\\"display_rotation\\":45}");assert(!parse_config(setting,&c));cJSON_Delete(setting);
 for(int rotation=0;rotation<=270;rotation+=90){char json[64];snprintf(json,sizeof(json),"{\\"display_rotation\\":%d}",rotation);setting=cJSON_Parse(json);assert(parse_config(setting,&c)&&c.display_rotation==rotation);cJSON_Delete(setting);}
 c.display_rotation=180;setting=cJSON_Parse("{}");assert(parse_stored_config(setting,&c)&&c.display_rotation==180);cJSON_Delete(setting);assert(!live);
 puts("Configuration migration: every allocation failure refuses a partial record; supported fields preserved: PASS");
}
`;
 fs.writeFileSync(path.join(tmp,'fixture.c'),source);
 const compiler=process.env.ZIG||process.env.CC||(process.platform==='win32'?'zig':'cc');
 const args=process.env.ZIG||path.basename(compiler).startsWith('zig')?['cc']:[];
 const output=path.join(tmp,process.platform==='win32'?'fixture.exe':'fixture');
 run(compiler,[...args,'-std=c11','-Wall','-Wextra','-Werror','-I',tmp,'-I',path.join(root,'main/companion'),'-I',path.join(root,'main/display'),'-I',path.join(root,'components/cjson'),path.join(tmp,'fixture.c'),path.join(root,'components/cjson/cJSON.c'),'-lm','-o',output]);
 run(output,[]);
}finally{if(path.dirname(path.resolve(tmp))!==path.resolve(os.tmpdir())||!path.basename(tmp).startsWith('vibe-config-test-'))throw Error('Unsafe fixture path');fs.rmSync(tmp,{recursive:true,force:true});}
