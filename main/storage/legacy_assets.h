#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
// Exact legacy resource fingerprints. Never classify arbitrary root files or
// config.json as disposable, even when their names resemble old web assets.
typedef struct {const char *name;size_t bytes;uint32_t crc;} legacy_asset_t;
static const legacy_asset_t legacy_assets[]={
    {"ap-mode.jpg",5608,0xce446710u},
    {"audio-playback.js",5485,0x2d2ea5dau},
    {"audio-record.js",17532,0xc430cf9cu},
    {"favicon.ico",6429,0x78d447d2u},
    {"gif-tool.js",16754,0xfbcf1ea1u},
    {"index.html",16775,0x9d744d21u},
    {"media.js",9219,0x732ac0c2u},
    {"no-wifi.jpg",5986,0x77cbc800u},
    {"panel.js",9293,0xd508d118u},
    {"script.js",13623,0x545ee3ccu},
    {"styles.css",14877,0x04abe977u},
    {"sysinfo.js",1448,0xbbff4a6du},
    {"upload.js",1903,0x14e72e62u},
    {"wifi-connected.jpg",6578,0x76f6e139u},
    {"wifi-connecting.jpg",6138,0xc72ec509u},
    {"wifi.js",3639,0x47dfbd06u},
    {"ws-core.js",7343,0x43e4af3eu},
};
static inline bool legacy_asset_expected(const char *name,size_t bytes,uint32_t *crc)
{
    if(!name||!crc)return false;
    for(size_t i=0;i<sizeof(legacy_assets)/sizeof(*legacy_assets);i++)
        if(bytes==legacy_assets[i].bytes&&!strcmp(name,legacy_assets[i].name)){*crc=legacy_assets[i].crc;return true;}
    return false;
}
