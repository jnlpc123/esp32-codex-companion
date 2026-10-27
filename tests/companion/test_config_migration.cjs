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
static const char *modes[]={"all","project","session"};
static const char *idle_names[]={"off","colors","black"};
static const char *link_names[]={"independent","sync","stagger"};
static int fail_at=-1,attempts,live;
static void *allocate(size_t n){if(attempts++==fail_at)return NULL;void *p=malloc(n);if(p)live++;return p;}
static void release(void *p){if(p){live--;free(p);}}
${extract('main/companion/task_status.c','void task_scope_json(', 'bool task_scope_equal(')}
${extract('main/companion/companion.c','static cJSON *config_json(', 'static bool integer_field(')}
int main(void){
 cJSON_Hooks hooks={allocate,release};cJSON_InitHooks(&hooks);
 companion_config_t c={.colors={63488,65504,2016},.foreground_size=112,.brightness=26,.volume=40,.idle_playback=IDLE_PLAYBACK_BLACK,.idle_done_delay=10,.media={"/data/one","/data/two","/data/three"}};
 cJSON *good=config_json(&c);assert(good);int allocations=attempts;
 assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(good,"media"))==3);
 cJSON_Delete(good);assert(!live);
 for(int i=0;i<allocations;i++){
  fail_at=i;attempts=0;cJSON *o=config_json(&c);assert(!o);assert(!live);
 }
 fail_at=-1;attempts=0;good=config_json(&c);assert(good);
 char *text=cJSON_PrintUnformatted(good);assert(text);cJSON_free(text);cJSON_Delete(good);assert(!live);
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
