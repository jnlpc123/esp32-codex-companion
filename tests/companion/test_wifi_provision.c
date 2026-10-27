#include "wifi_provision.h"
#include "wifi_prompt.h"
#include "freertos/task.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#undef malloc
#undef free
static bool ready,task_failure;static esp_err_t connect_result,save_result;
static unsigned saves,restarts,finished;static void (*worker)(void *);static void *argument;static size_t allocated;
void *fixture_malloc(size_t size){allocated=size;return malloc(size);}
void fixture_free(void *p){for(size_t i=0;i<allocated;i++)assert(!((unsigned char *)p)[i]);free(p);}
bool wifi_is_ready(void){return ready;}
esp_err_t wifi_connect_requested(const char *ssid,const char *password){assert(!strcmp(ssid,"home")&&!strcmp(password,"12345678"));return connect_result;}
esp_err_t wifi_save_credential_checked(const char *ssid,const char *password){assert(!strcmp(ssid,"home")&&!strcmp(password,"12345678"));saves++;return save_result;}
BaseType_t xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){(void)name;(void)stack;(void)priority;(void)handle;if(task_failure)return 0;worker=fn;argument=arg;return 1;}
void vTaskDelete(void *task){(void)task;}
void vTaskDelay(unsigned ticks){(void)ticks;}
void esp_restart(void){restarts++;}
void wifi_configuration_connected(void){}
void wifi_configuration_failed(void){}
void wifi_configuration_complete(void){finished++;}
int main(void){
 assert(wifi_prompt_select(false,WIFI_PROMPT_AP,0,0)==WIFI_PROMPT_AP);
 assert(wifi_prompt_select(true,WIFI_PROMPT_AP,0,0)==WIFI_PROMPT_NONE);
 assert(wifi_prompt_select(true,WIFI_PROMPT_CONNECTING,0,0)==WIFI_PROMPT_NONE);
 assert(wifi_prompt_select(false,WIFI_PROMPT_SUCCESS,1999,2000)==WIFI_PROMPT_SUCCESS);
 assert(wifi_prompt_select(false,WIFI_PROMPT_SUCCESS,2000,2000)==WIFI_PROMPT_NONE);

 assert(wifi_provision_valid("home",""));assert(wifi_provision_valid("home","12345678"));
 assert(wifi_provision_valid("中中中中中中中中中中aa","12345678"));
 assert(!wifi_provision_valid("中中中中中中中中中中中","12345678"));
 assert(!wifi_provision_valid("","12345678"));assert(!wifi_provision_valid("home","short"));
 assert(wifi_provision_valid("home","0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
 assert(!wifi_provision_valid("home","gggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggg"));
 uint32_t id=0;assert(wifi_provision_begin("home","12345678",&id)==ESP_ERR_INVALID_STATE);
 ready=true;assert(wifi_provision_begin("home","short",&id)==ESP_ERR_INVALID_ARG);
 task_failure=true;assert(wifi_provision_begin("home","12345678",&id)==ESP_ERR_NO_MEM);task_failure=false;
 assert(wifi_provision_begin("home","12345678",&id)==ESP_OK);assert(id);
 wifi_provision_status_t state;wifi_provision_status(&state);assert(state.busy&&state.state==WIFI_SETUP_CONNECTING);
 assert(wifi_provision_begin("home","12345678",&id)==ESP_ERR_INVALID_STATE);
 connect_result=ESP_FAIL;worker(argument);wifi_provision_status(&state);
 assert(!state.busy&&state.state==WIFI_SETUP_FAILED&&!state.saved&&!saves&&!restarts);
 connect_result=ESP_OK;save_result=ESP_FAIL;assert(wifi_provision_begin("home","12345678",&id)==ESP_OK);worker(argument);wifi_provision_status(&state);
 assert(state.state==WIFI_SETUP_FAILED&&!state.saved&&saves==1&&!restarts);
 save_result=ESP_OK;assert(wifi_provision_begin("home","12345678",&id)==ESP_OK);worker(argument);wifi_provision_status(&state);
 assert(state.state==WIFI_SETUP_CONNECTED&&state.saved&&saves==2&&!restarts&&finished==1&&!state.busy);
 puts("Wi-Fi credentials, startup/busy gate, no save/restart on failure, success and credential wiping: PASS");
 return 0;
}
