const {spawnSync}=require('node:child_process'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const root=path.resolve(__dirname,'../..'),tmp=fs.mkdtempSync(path.join(os.tmpdir(),'vibe-wifi-creds-'));
const headers={
 'esp_err.h':'#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_STATE 2\n#define ESP_ERR_NO_MEM 3\n#define ESP_ERR_NVS_NOT_FOUND 4\n',
 'nvs.h':'#pragma once\n#include <stddef.h>\n#include <stdint.h>\n#include \"esp_err.h\"\ntypedef int nvs_handle_t;\n#define NVS_READONLY 0\n#define NVS_READWRITE 1\nesp_err_t nvs_open(const char *,int,nvs_handle_t *);void nvs_close(nvs_handle_t);esp_err_t nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);esp_err_t nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);esp_err_t nvs_get_u8(nvs_handle_t,const char *,uint8_t *);esp_err_t nvs_get_str(nvs_handle_t,const char *,char *,size_t *);esp_err_t nvs_commit(nvs_handle_t);\n',
 'esp_system.h':'#pragma once\nvoid esp_restart(void);\n',
 'wifi_manager.h':'#pragma once\n#include <stdbool.h>\n#include "esp_err.h"\nbool wifi_is_ready(void);\nesp_err_t wifi_connect_requested(const char *,const char *);\nesp_err_t wifi_save_credential_checked(const char *,const char *);\n',
 'freertos/FreeRTOS.h':'#pragma once\ntypedef int BaseType_t;typedef int portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED 0\n#define portENTER_CRITICAL(p) ((void)(p))\n#define portEXIT_CRITICAL(p) ((void)(p))\n#define pdPASS 1\n#define pdMS_TO_TICKS(v) (v)\n',
 'freertos/task.h':'#pragma once\n#include "FreeRTOS.h"\nBaseType_t xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void *);\nvoid vTaskDelete(void *);void vTaskDelay(unsigned);\n',
 'mem_tracker.h':'#include <stdlib.h>\nvoid *fixture_malloc(size_t);void fixture_free(void *);\n#define malloc fixture_malloc\n#define free fixture_free\n'
};
function run(cmd,args,o={}){const r=spawnSync(cmd,args,{stdio:'inherit',...o});if(r.error)throw r.error;if(r.status)throw Error('Wi-Fi fixture failed: '+r.status);}
try{
 for(const [n,s] of Object.entries(headers)){const p=path.join(tmp,n);fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,s);}
 const compiler=process.env.ZIG||process.env.CC||(process.platform==='win32'?'zig':'cc');
 const args=process.env.ZIG||path.basename(compiler).startsWith('zig')?['cc']:[];
 const output=path.join(tmp,process.platform==='win32'?'fixture.exe':'fixture');
 run(compiler,[...args,'-std=c11','-Wall','-Wextra','-Werror','-I',tmp,'-I',path.join(root,'main/wifi'),path.join(__dirname,'test_wifi_credentials.c'),path.join(root,'main/wifi/wifi_credentials.c'),'-o',output]);
 run(output,[],{cwd:tmp});
}finally{if(path.dirname(path.resolve(tmp))!==path.resolve(os.tmpdir())||!path.basename(tmp).startsWith('vibe-wifi-creds-'))throw Error('Unsafe fixture path');fs.rmSync(tmp,{recursive:true,force:true});}
