const {spawnSync}=require('node:child_process'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const root=path.resolve(__dirname,'../..'),tmp=fs.mkdtempSync(path.join(os.tmpdir(),'vibe-route-test-'));
function extract(file,a,b){const s=fs.readFileSync(path.join(root,file),'utf8');return s.slice(s.indexOf(a),s.indexOf(b,s.indexOf(a)));}
function run(cmd,args){const r=spawnSync(cmd,args,{stdio:'inherit'});if(r.error)throw r.error;if(r.status)throw Error('Route fixture failed: '+r.status);}
try{
 let symbols='';for(const name of ['index','css','js','gif','alpha','wifi','wifi_js','network_js'])symbols+=`static const unsigned char asset_${name}[]={1,2};\n#define _binary_vibe_${name}_start asset_${name}\n#define _binary_vibe_${name}_end (asset_${name}+2)\n`;
 const code=`#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define HTTPD_404_NOT_FOUND 404
#define WIFI_MODE_NULL 0
#define WIFI_MODE_AP 1
#define WIFI_MODE_APSTA 2
#define WIFI_MODE_STA 3
typedef int wifi_mode_t;
typedef struct {const char *uri;} httpd_req_t;
typedef enum {WIFI_PROMPT_NONE,WIFI_PROMPT_AP,WIFI_PROMPT_CONNECTING,WIFI_PROMPT_SUCCESS} wifi_prompt_t;
static bool s_wifi_ready=true,ignored,connected;
static int mode=WIFI_MODE_AP,mode_result=ESP_OK,code;
static const void *served;static char redirect[32];
static bool station_has_ip(void){return connected;}
static bool wifi_setup_ignored(void){return ignored;}
static int esp_wifi_get_mode(int *out){*out=mode;return mode_result;}
static void httpd_resp_set_status(httpd_req_t *r,const char *s){(void)r;code=atoi(s);}
static void httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *v){(void)r;if(!strcmp(key,"Location"))snprintf(redirect,sizeof(redirect),"%s",v);}
static void httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;}
static int httpd_resp_send(httpd_req_t *r,const char *p,size_t n){(void)r;(void)n;served=p;return 0;}
static int httpd_resp_send_chunk(httpd_req_t *r,const char *p,size_t n){return httpd_resp_send(r,p,n);}
static int httpd_resp_send_err(httpd_req_t *r,int status,const char *s){(void)r;(void)s;code=status;return 0;}
static bool wifi_prompt_asset(wifi_prompt_t p,const uint8_t **data,size_t *bytes){(void)p;(void)data;(void)bytes;return false;}
${symbols}
${extract('main/wifi/wifi_manager.c','bool wifi_requires_setup(void)','esp_err_t wifi_connect_requested(')}
${extract('main/file_server/file_server.c','static bool safe_media_name(', 'static esp_err_t upload(')}
${extract('main/file_server/file_server.c','static esp_err_t serve(', 'esp_err_t example_start_file_server(')}
static void get(const char *uri){httpd_req_t r={uri};code=200;*redirect=0;served=NULL;assert(serve(&r)==0);}
int main(void){
 get("/");assert(code==302&&!strcmp(redirect,"/wifi"));
 get("/hotspot-detect.html");assert(code==302&&!strcmp(redirect,"/wifi"));
 get("/wifi");assert(code==200&&served==asset_wifi);
 get("/network.js");assert(code==200&&served==asset_network_js);
 get("/wifi.js");assert(code==200&&served==asset_wifi_js);
 connected=true;get("/");assert(code==200&&served==asset_index);
 connected=false;ignored=true;get("/");assert(code==200&&served==asset_index);
 ignored=false;mode=WIFI_MODE_STA;get("/");assert(code==200&&served==asset_index);
 mode=WIFI_MODE_AP;s_wifi_ready=false;get("/");assert(code==200&&served==asset_index);
 s_wifi_ready=true;mode_result=ESP_FAIL;get("/");assert(code==200&&served==asset_index);
 puts("Device HTTP routes: AP/probes to provisioning, connected/offline to management, dedicated assets: PASS");
}
`;
 fs.writeFileSync(path.join(tmp,'fixture.c'),code);
 const compiler=process.env.ZIG||process.env.CC||(process.platform==='win32'?'zig':'cc'),args=process.env.ZIG||path.basename(compiler).startsWith('zig')?['cc']:[],output=path.join(tmp,process.platform==='win32'?'fixture.exe':'fixture');
 run(compiler,[...args,'-std=c11','-Wall','-Wextra','-Werror',path.join(tmp,'fixture.c'),'-o',output]);run(output,[]);
}finally{if(path.dirname(path.resolve(tmp))!==path.resolve(os.tmpdir())||!path.basename(tmp).startsWith('vibe-route-test-'))throw Error('Unsafe fixture path');fs.rmSync(tmp,{recursive:true,force:true});}
