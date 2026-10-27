#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {WIFI_PROMPT_NONE,WIFI_PROMPT_AP,WIFI_PROMPT_CONNECTING,WIFI_PROMPT_SUCCESS} wifi_prompt_t;
static inline wifi_prompt_t wifi_prompt_select(bool ignored,wifi_prompt_t phase,int64_t now,int64_t until)
{return ignored||(phase==WIFI_PROMPT_SUCCESS&&now>=until)?WIFI_PROMPT_NONE:phase;}
