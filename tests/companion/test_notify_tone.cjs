const {spawnSync}=require('node:child_process'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),tmp=fs.mkdtempSync(path.join(os.tmpdir(),'vibe-tone-'));
function run(cmd,args){const r=spawnSync(cmd,args,{stdio:'inherit'});if(r.error)throw r.error;if(r.status)throw Error('Tone fixture failed: '+r.status);}
try{
 const headers={
 'driver/i2s_types.h':'#pragma once\ntypedef void *i2s_chan_handle_t;\n',
 'driver/gpio.h':'#pragma once\n',
 'esp_err.h':'#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n',
 'driver/i2s_std.h':'#pragma once\n#include <stddef.h>\n#include "i2s_types.h"\n#include "esp_err.h"\nesp_err_t i2s_channel_write(i2s_chan_handle_t,const void*,size_t,size_t*,unsigned);\n',
 'freertos/FreeRTOS.h':'#pragma once\n#define pdTRUE 1\n#define pdPASS 1\n#define portMAX_DELAY 0xffffffff\n',
 'freertos/queue.h':'#pragma once\n#include <stddef.h>\ntypedef void *QueueHandle_t;\nQueueHandle_t xQueueCreate(unsigned,size_t);int xQueueReceive(QueueHandle_t,void*,unsigned);int xQueueOverwrite(QueueHandle_t,const void*);void vQueueDelete(QueueHandle_t);\n',
 'freertos/task.h':'#pragma once\nint xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,void*);\n'
 };
 for(const [n,s] of Object.entries(headers)){const p=path.join(tmp,n);fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,s);}
 const codec=fs.readFileSync(path.join(root,'main/audio/es8311.c'),'utf8');
 const mapping=codec.slice(codec.indexOf('static int codec_volume('),codec.indexOf('struct es8311_context'));
 fs.writeFileSync(path.join(tmp,'fixture.c'),`#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <setjmp.h>
${mapping}
#include "${path.join(root,'main/companion/notify_tone.c').replaceAll('\\','/')}"
static FILE *output;static jmp_buf done;static int calls,kind,style;
es8311_handle_t es8311_init(int volume){(void)volume;return (es8311_handle_t)1;}
void es8311_deinit(es8311_handle_t h){(void)h;}
esp_err_t es8311_set_volume(es8311_handle_t h,int v){(void)h;(void)v;return ESP_OK;}
i2s_chan_handle_t es8311_get_tx_handle(es8311_handle_t h){return h;}
QueueHandle_t xQueueCreate(unsigned n,size_t size){(void)n;(void)size;return (void*)1;}
int xQueueReceive(QueueHandle_t q,void *value,unsigned timeout){(void)q;(void)timeout;if(calls++)longjmp(done,1);*(int*)value=kind;return pdTRUE;}
int xQueueOverwrite(QueueHandle_t q,const void *v){(void)q;(void)v;return pdTRUE;}
void vQueueDelete(QueueHandle_t q){(void)q;}
int xTaskCreate(void(*f)(void*),const char*n,unsigned stack,void*p,unsigned priority,void*h){(void)f;(void)n;(void)stack;(void)p;(void)priority;(void)h;return pdPASS;}
esp_err_t i2s_channel_write(i2s_chan_handle_t h,const void *data,size_t bytes,size_t *written,unsigned timeout){
 (void)h;(void)timeout;const int16_t *pcm=data;assert(bytes<=1024&&bytes%4==0);
 for(size_t i=0;i<bytes/2;i+=2){assert(pcm[i]==pcm[i+1]);assert(abs(pcm[i])<=20000);}
 assert(fwrite(data,1,bytes,output)==bytes);*written=bytes;return ESP_OK;
}
int main(int argc,char **argv){assert(argc==4);kind=atoi(argv[1]);style=atoi(argv[2]);output=fopen(argv[3],"wb");assert(output);notify_tone_init(40,false,(notify_sound_style_t)style);
 assert(codec_volume(0)==0&&codec_volume(1)==0&&codec_volume(20)==56&&codec_volume(40)==84&&codec_volume(60)==92&&codec_volume(80)==96);
 for(int v=1;v<=100;v++)assert(codec_volume(v)>=codec_volume(v-1)&&codec_volume(v)<=96);
 assert(notify_tone_available());if(!setjmp(done))tone_task(NULL);fclose(output);
 assert(notify_tone_samples()==(style==NOTIFY_8BIT?(kind==0?14560:kind==2?21120:2240):(kind==0?5520:kind==2?6880:1760)));return 0;
}
`);
 const compiler=process.env.ZIG||process.env.CC||(process.platform==='win32'?'zig':'cc');
 const args=process.env.ZIG||path.basename(compiler).startsWith('zig')?['cc']:[];
 const exe=path.join(tmp,'fixture.exe');
 run(compiler,[...args,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I',tmp,'-I',path.join(root,'main'),path.join(tmp,'fixture.c'),'-o',exe]);
 for(const style of [0,1])for(const kind of [0,2,1]){
  const raw=path.join(tmp,style+'-'+kind+'.pcm');run(exe,[String(kind),String(style),raw]);const pcm=fs.readFileSync(raw);
  for(let n=0;n<(style===1&&kind!==1?100:4);n+=2)assert.equal(pcm.readInt16LE(n),0,'starts quietly');
  for(let n=pcm.length-80;n<pcm.length;n+=2)assert.equal(pcm.readInt16LE(n),0,'ends quietly');
  let peak=0;for(let n=0;n<pcm.length;n+=2)peak=Math.max(peak,Math.abs(pcm.readInt16LE(n)));
  assert.ok(style===1?peak>14000&&peak<20000:peak>4500&&peak<=6000,'theme amplitude stays bounded without digital clipping');
  if(kind===0){
   // G5 in the brighter NES register, with the same rhythm as the audition.
   let changes=0,last=0;for(let frame=500;frame<1500;frame++){const v=pcm.readInt16LE(frame*4);if(v>=0&&last<0)changes++;last=v;}
   assert.ok(style===1?changes>=48&&changes<=50:changes>=54&&changes<=56,'theme-specific reply pitch');
  }
  if(process.env.VIBE_TONE_PREVIEW){fs.mkdirSync(process.env.VIBE_TONE_PREVIEW,{recursive:true});fs.copyFileSync(raw,path.join(process.env.VIBE_TONE_PREVIEW,style+'-'+kind+'.pcm'));}
 }
 console.log('Actual notification task: NES pulse pitch, exact melody lengths, bounded/equal stereo PCM and silent endpoints: PASS');
}finally{if(path.dirname(path.resolve(tmp))!==path.resolve(os.tmpdir())||!path.basename(tmp).startsWith('vibe-tone-'))throw Error('Unsafe fixture path');fs.rmSync(tmp,{recursive:true,force:true});}
