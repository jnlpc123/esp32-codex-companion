#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool raw,pressed,ready,initialized;
    uint32_t changed_at,pressed_at;
} factory_button_t;

// Monotonic milliseconds, wrap-safe. Require an initial stable release and
// debounce both edges; a held button never causes a reboot into ROM download.
static inline bool factory_button_update(factory_button_t *b,bool low,uint32_t now)
{
    if(!b->initialized){b->initialized=true;b->raw=low;b->changed_at=now;return false;}
    if(b->raw!=low){b->raw=low;b->changed_at=now;return false;}
    if((uint32_t)(now-b->changed_at)<50)return false;
    if(!b->ready){if(!low&&(uint32_t)(now-b->changed_at)>=100)b->ready=true;return false;}
    if(low&&!b->pressed){b->pressed=true;b->pressed_at=b->changed_at;}
    else if(!low&&b->pressed){b->pressed=false;return (uint32_t)(b->changed_at-b->pressed_at)>=10000;}
    return false;
}
