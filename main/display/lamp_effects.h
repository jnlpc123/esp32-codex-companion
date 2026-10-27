#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum { LIGHT_BLINK, LIGHT_DOUBLE, LIGHT_BREATHE, LIGHT_CHASE } light_effect_t;
typedef enum { ANIMATION_INDEPENDENT, ANIMATION_SYNC, ANIMATION_STAGGER } animation_link_t;

// Eight brightness steps keep background traffic bounded on the shared SPI bus.
static inline unsigned light_effect_level(light_effect_t effect, uint64_t ms,
                                         unsigned period, unsigned channel, int linked)
{
    if(!period)return 8;
    unsigned phase=(unsigned)(ms%period);
    if(effect==LIGHT_CHASE)return phase*3/period==channel ? 8 : 0;
    if(!linked)phase=(phase+channel*period/3)%period;
    if(effect==LIGHT_BLINK)return phase<period/2 ? 8 : 0;
    if(effect==LIGHT_DOUBLE){unsigned p=phase*8/period;return p==0||p==2 ? 8 : 0;}
    unsigned ramp=phase*16/period;return ramp<=8 ? ramp : 16-ramp;
}
static inline uint16_t light_effect_color(uint16_t color,unsigned level)
{
    return (((color>>11)*level/8)<<11)|((((color>>5)&63)*level/8)<<5)|((color&31)*level/8);
}
static inline unsigned animation_link_frame(const uint16_t *delays,unsigned count,
                                            uint64_t duration,uint64_t cycle,uint64_t elapsed,
                                            animation_link_t link,unsigned channel)
{
    if(!delays||!count||!duration||!cycle)return 0;
    uint64_t phase=elapsed%cycle;
    if(link==ANIMATION_STAGGER)phase=(phase+channel*cycle/3)%cycle;
    uint64_t at=phase*duration/cycle,cumulative=0;
    for(unsigned i=0;i<count;i++){cumulative+=(uint64_t)delays[i]*1000;if(at<cumulative)return i;}
    return count-1;
}
