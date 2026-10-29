const {spawnSync}=require('node:child_process'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const root=path.resolve(__dirname,'../..'),tmp=fs.mkdtempSync(path.join(os.tmpdir(),'vibe-reset-'));
function run(cmd,args){const r=spawnSync(cmd,args,{stdio:'inherit'});if(r.error)throw r.error;if(r.status)throw Error('Factory reset fixture failed: '+r.status);}
try{
 const headers={
  'esp_err.h':'#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_STATE 2\n#define ESP_ERR_NVS_NOT_FOUND 3\n',
  'nvs.h':'#pragma once\n#include <stdint.h>\n#include "esp_err.h"\ntypedef int nvs_handle_t;\n#define NVS_READONLY 0\n#define NVS_READWRITE 1\nesp_err_t nvs_open(const char*,int,nvs_handle_t*);\nesp_err_t nvs_get_u8(nvs_handle_t,const char*,uint8_t*);\nesp_err_t nvs_set_u8(nvs_handle_t,const char*,uint8_t);\nesp_err_t nvs_erase_all(nvs_handle_t);\nesp_err_t nvs_erase_key(nvs_handle_t,const char*);\nesp_err_t nvs_commit(nvs_handle_t);\nvoid nvs_close(nvs_handle_t);\n',
  'esp_spiffs.h':'#pragma once\n#include "esp_err.h"\nesp_err_t esp_spiffs_format(const char*);\n',
  'esp_system.h':'#pragma once\nvoid esp_restart(void);\n',
  'esp_timer.h':'#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
  'storage_service.h':'#pragma once\n#include <stdbool.h>\nbool storage_service_lock(void);void storage_service_unlock(void);\nbool storage_import_busy(void);bool storage_async_busy(void);bool storage_maintenance_busy(void);void storage_maintenance_set(bool);\n',
  'wifi_provision.h':'#pragma once\n#include <stdbool.h>\ntypedef struct {bool busy;} wifi_provision_status_t;\nvoid wifi_provision_status(wifi_provision_status_t*);\n'
 };
 for(const [n,s] of Object.entries(headers))fs.writeFileSync(path.join(tmp,n),s);
 const compiler=process.env.ZIG||process.env.CC||(process.platform==='win32'?'zig':'cc');
 const args=process.env.ZIG||path.basename(compiler).startsWith('zig')?['cc']:[];
 const out=path.join(tmp,process.platform==='win32'?'fixture.exe':'fixture');
 run(compiler,[...args,'-std=c11','-Wall','-Wextra','-Werror','-I',tmp,'-I',path.join(root,'main/settings'),path.join(__dirname,'test_factory_reset.c'),'-o',out]);run(out,[]);
}finally{if(path.dirname(path.resolve(tmp))!==path.resolve(os.tmpdir())||!path.basename(tmp).startsWith('vibe-reset-'))throw Error('Unsafe fixture path');fs.rmSync(tmp,{recursive:true,force:true});}
