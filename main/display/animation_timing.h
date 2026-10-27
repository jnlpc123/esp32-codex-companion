#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum { ANIMATION_INDEPENDENT, ANIMATION_SYNC, ANIMATION_STAGGER } animation_link_t;

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
