#include "wifi_prompt_assets.h"
extern const uint8_t ap_start[] asm("_binary_wifi_ap_start"),ap_end[] asm("_binary_wifi_ap_end");
extern const uint8_t connecting_start[] asm("_binary_wifi_connecting_start"),connecting_end[] asm("_binary_wifi_connecting_end");
extern const uint8_t connected_start[] asm("_binary_wifi_connected_start"),connected_end[] asm("_binary_wifi_connected_end");
bool wifi_prompt_asset(wifi_prompt_t prompt,const uint8_t **data,size_t *size)
{
    if(prompt==WIFI_PROMPT_AP){*data=ap_start;*size=ap_end-ap_start;}
    else if(prompt==WIFI_PROMPT_CONNECTING){*data=connecting_start;*size=connecting_end-connecting_start;}
    else if(prompt==WIFI_PROMPT_SUCCESS){*data=connected_start;*size=connected_end-connected_start;}
    else return false;
    return true;
}
