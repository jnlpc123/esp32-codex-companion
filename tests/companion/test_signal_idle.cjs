const {spawnSync}=require('node:child_process'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const root=path.resolve(__dirname,'../..'),tmp=fs.mkdtempSync(path.join(os.tmpdir(),'vibe-signal-idle-'));
function run(cmd,args){const r=spawnSync(cmd,args,{stdio:'inherit'});if(r.error)throw r.error;if(r.status)throw Error('Signal idle fixture failed: '+r.status);}
try{
 fs.writeFileSync(path.join(tmp,'esp_err.h'),'#pragma once\ntypedef int esp_err_t;\n');
 const actual=fs.readFileSync(path.join(root,'main/companion/companion.c'),'utf8');
 const idle=actual.slice(actual.indexOf('static void update_idle_locked('),actual.indexOf('static cJSON *config_json('));
 const visual=actual.slice(actual.indexOf('void companion_get_visual('),actual.indexOf('static esp_err_t update_config('));
 fs.writeFileSync(path.join(tmp,'fixture.c'),`#include "companion.h"
#include <assert.h>
#include <stdio.h>
static companion_config_t s_config;
static task_counts_t s_remote_counts,s_wifi_counts;
static int64_t s_manual_until,s_idle_since_us,s_animation_epoch;
static uint32_t s_revision=1,s_idle_completion_seq;
static bool s_idle_playing;
static task_signal_t s_signal;
static task_signal_visual_t s_signal_visual={.waiting_lit=true};
static int s_lock;
static int64_t test_now;
#define portMAX_DELAY 0
static void xSemaphoreTake(int lock,int delay){(void)lock;(void)delay;}
static void xSemaphoreGive(int lock){(void)lock;}
static int64_t esp_timer_get_time(void){return test_now;}
static const char *transport(int64_t now){(void)now;return "usb";}
static task_counts_t effective_counts(int64_t now){(void)now;return s_remote_counts;}
int task_counts_state(const task_counts_t *c){return c->waiting?2:c->working?1:c->done?3:0;}
bool task_scope_equal(const task_scope_t *a,const task_scope_t *b){return a->mode==b->mode&&!strcmp(a->target,b->target);}
${idle}
${visual}
static void reset(signal_pattern_t pattern,int delay,idle_playback_t idle){
 s_config=(companion_config_t){.signal_pattern=pattern,.idle_playback=idle,.idle_done_delay=delay,.colors={0xf800,0xffe0,0x07e0}};
 s_remote_counts=(task_counts_t){.done=1,.completion_seq=1,.detailed=true};
 s_idle_since_us=0;s_idle_completion_seq=0;s_idle_playing=false;s_signal=(task_signal_t){0};s_signal_visual=(task_signal_visual_t){.waiting_lit=true};
}
static companion_visual_t view(int64_t now,int channel){test_now=now;companion_visual_t v;companion_get_visual(channel,&v);return v;}
int main(void){
 reset(SIGNAL_TASK_CYCLE,10,IDLE_PLAYBACK_BLACK);
 const int channels[]={1,2,3,2,1,2,3,2};
 for(int step=0;step<8;step++)for(int ch=1;ch<=3;ch++){
  companion_visual_t v=view(1000000+step*500000,ch);
  assert(v.background==(ch==channels[step]?s_config.colors[ch-1]:0x0841));
  assert(v.count==(ch==1?1:0));assert(v.active==(ch==channels[step]));
 }
 assert(!s_idle_playing);view(14999999,1);assert(!s_idle_playing);
 companion_visual_t v=view(15000000,1);assert(s_idle_playing && v.background==0 && v.active && !v.count);
 reset(SIGNAL_TASK_CYCLE,0,IDLE_PLAYBACK_COLORS);view(1000000,1);assert(!s_idle_playing);
 view(4999999,1);assert(!s_idle_playing);view(5000000,1);assert(s_idle_playing);
 reset(SIGNAL_TASK_CYCLE,-1,IDLE_PLAYBACK_BLACK);view(1000000,1);view(90000000,1);assert(!s_idle_playing);
 s_remote_counts.done=0;view(90000001,1);assert(s_idle_playing);
 reset(SIGNAL_TASK_CYCLE,10,IDLE_PLAYBACK_OFF);view(1000000,1);v=view(90000000,1);assert(!s_idle_playing && v.background==0xf800);
 reset(SIGNAL_STEADY,10,IDLE_PLAYBACK_BLACK);view(1000000,1);view(10999999,1);assert(!s_idle_playing);
 view(11000000,1);assert(s_idle_playing);
 s_config.signal_pattern=SIGNAL_TASK_CYCLE;view(12000000,1);assert(!s_idle_playing);
 v=view(12500000,2);assert(v.background==0xffe0);
 s_config.signal_pattern=SIGNAL_STEADY;view(13000000,1);assert(!s_idle_playing);
 view(23000000,1);assert(s_idle_playing);
 reset(SIGNAL_TASK_CYCLE,10,IDLE_PLAYBACK_BLACK);view(1000000,1);s_remote_counts.waiting=1;
 v=view(1100000,2);assert(v.background==0xffe0 && v.active);
 v=view(1600000,2);assert(v.background==0x0841 && v.active && v.count==1);
 v=view(1600000,3);assert(v.background==0x0841);
 s_remote_counts.working=1;v=view(1700000,3);assert(v.background==0x07e0 && v.active);
 puts("Signal integration: true counts, colored cue panels, blinking without pausing GIF, 4s cue then configured idle hold, 0/-1/off and steady compatibility: PASS");
}
`);
 const compiler=process.env.ZIG||process.env.CC||(process.platform==='win32'?'zig':'cc');
 const args=process.env.ZIG||path.basename(compiler).startsWith('zig')?['cc']:[];
 const out=path.join(tmp,process.platform==='win32'?'fixture.exe':'fixture');
 run(compiler,[...args,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-I',tmp,'-I',path.join(root,'main/companion'),'-I',path.join(root,'main/display'),'-I',path.join(root,'components/cjson'),path.join(tmp,'fixture.c'),'-o',out]);run(out,[]);
}finally{if(path.dirname(path.resolve(tmp))!==path.resolve(os.tmpdir())||!path.basename(tmp).startsWith('vibe-signal-idle-'))throw Error('Unsafe fixture path');fs.rmSync(tmp,{recursive:true,force:true});}
