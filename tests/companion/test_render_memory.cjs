const {spawnSync}=require('node:child_process');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const root=path.resolve(__dirname,'../..'),tmp=fs.mkdtempSync(path.join(os.tmpdir(),'vibe-render-test-'));
const output=path.join(tmp,process.platform==='win32'?'fixture.exe':'fixture');
const headers={
 'esp_err.h':'#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM 1\n#define ESP_ERR_INVALID_ARG 2\n#define ESP_ERR_INVALID_SIZE 3\nstatic inline const char *esp_err_to_name(int e){(void)e;return "error";}\n',
 'esp_lcd_panel_ops.h':'#pragma once\n#include <stdbool.h>\n#include "esp_err.h"\ntypedef void *esp_lcd_panel_handle_t;\n',
 'config.h':'#pragma once\n#define DISPLAY_WIDTH 160\n#define DISPLAY_HEIGHT 160\n',
 'storage_service.h':'#pragma once\nvoid storage_note_change(void);\n',
 'companion.h':'#pragma once\n#include <stdint.h>\n#include <stdbool.h>\ntypedef struct {uint16_t background;int foreground_size;uint32_t revision;int count;bool active;uint16_t base_background;} companion_visual_t;\nvoid companion_get_visual(int,companion_visual_t *);\n',
 'esp_spiffs.h':'#pragma once\n#include <stddef.h>\n#include "esp_err.h"\nesp_err_t esp_spiffs_info(const char *,size_t *,size_t *);\n',
 'esp_timer.h':'#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
 'esp_system.h':'#pragma once\nunsigned esp_get_free_heap_size(void);\n',
 // Consume log arguments so -Werror still checks format-path locals.
 'esp_log.h':'#pragma once\nvoid fixture_log(const char *,const char *,...);\n#define ESP_LOGI(...) fixture_log(__VA_ARGS__)\n#define ESP_LOGD(...) fixture_log(__VA_ARGS__)\n#define ESP_LOGW(...) fixture_log(__VA_ARGS__)\n#define ESP_LOGE(...) fixture_log(__VA_ARGS__)\n',
 'jpeg_decoder.h':'#pragma once\n#include <stdint.h>\n#include "esp_err.h"\n#define JPEG_IMAGE_FORMAT_RGB565 0\n#define JPEG_IMAGE_SCALE_0 0\ntypedef struct {uint8_t *indata;uint32_t indata_size;uint8_t *outbuf;uint32_t outbuf_size;int out_format,out_scale;struct {unsigned swap_color_bytes:1;} flags;} esp_jpeg_image_cfg_t;\ntypedef struct {uint16_t width,height;} esp_jpeg_image_output_t;\nesp_err_t esp_jpeg_decode(const esp_jpeg_image_cfg_t *,esp_jpeg_image_output_t *);\n',
 'mem_tracker.h':'#pragma once\n#include <stdlib.h>\nvoid *fixture_malloc(size_t);\nvoid *fixture_calloc(size_t,size_t);\nvoid fixture_free(void *);\n#define malloc fixture_malloc\n#define calloc fixture_calloc\n#define free fixture_free\n'
};
function run(cmd,args,options={}){const r=spawnSync(cmd,args,{stdio:'inherit',...options});if(r.error)throw r.error;if(r.status!==0)throw Error('C render fixture failed: '+r.status);}
try{
 for(const [name,source] of Object.entries(headers))fs.writeFileSync(path.join(tmp,name),source);
 const compiler=process.env.ZIG||process.env.CC||(process.platform==='win32'?'zig':'cc');
 const args=process.env.ZIG||path.basename(compiler).startsWith('zig')?['cc']:[];
 run(compiler,[...args,'-std=c11','-Wall','-Wextra','-Werror','-I',tmp,'-I',path.join(root,'main/display'),'-include',path.join(tmp,'mem_tracker.h'),path.join(__dirname,'test_render_memory.c'),path.join(root,'main/display/animation_cache.c'),path.join(root,'main/display/jpeg_decode.c'),path.join(root,'main/display/companion_render.c'),'-o',output]);
 run(output,[],{cwd:tmp});
}finally{
 if(path.dirname(path.resolve(tmp))!==path.resolve(os.tmpdir())||!path.basename(tmp).startsWith('vibe-render-test-'))throw Error('Unsafe test cleanup target');
 fs.rmSync(tmp,{recursive:true,force:true});
}
