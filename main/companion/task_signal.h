#pragma once
#include "task_status.h"
#include <string.h>

typedef enum { SIGNAL_STEADY, SIGNAL_TASK_CYCLE } signal_pattern_t;
typedef struct {
    task_scope_t scope;
    signal_pattern_t pattern;
    uint32_t completion_seq;
    int64_t completion_start_us, waiting_start_us;
    bool seen_completion, completion_active, waiting, legacy_armed;
} task_signal_t;
typedef struct {
    int cue_channel;
    bool waiting_lit;
    int64_t hold_start_us;
} task_signal_visual_t;

static inline task_signal_visual_t task_signal_update(task_signal_t *s,int64_t now,signal_pattern_t pattern,
    const task_counts_t *counts,const task_scope_t *scope)
{
    if(s->pattern!=pattern || s->scope.mode!=scope->mode || strcmp(s->scope.target,scope->target))
        *s=(task_signal_t){.pattern=pattern,.scope=*scope};
    task_signal_visual_t v={.waiting_lit=true};
    if(pattern==SIGNAL_STEADY)return v;
    if(counts->waiting){
        if(!s->waiting)s->waiting_start_us=now;
        v.waiting_lit=((now-s->waiting_start_us)/500000)%2==0;
    }
    s->waiting=counts->waiting>0;
    if(counts->working || counts->waiting){
        s->legacy_armed=true;s->completion_active=false;
    } else if(counts->done){
        // The bridge reports the maximum *unacknowledged* sequence: acknowledging
        // a newer task can decrease it. Only a new high watermark is a new cue.
        if(!s->seen_completion || counts->completion_seq>s->completion_seq || (!counts->detailed&&s->legacy_armed)){
            s->seen_completion=true;s->completion_seq=counts->completion_seq;
            s->completion_start_us=now;s->completion_active=true;s->legacy_armed=false;
        }
        if(s->completion_active){
            v.hold_start_us=s->completion_start_us+4000000;
            int64_t elapsed=now-s->completion_start_us;
            if(elapsed<4000000){
                static const int channels[]={1,2,3,2};
                v.cue_channel=channels[(elapsed/500000)%4];
            }
        }
    } else s->completion_active=false;
    return v;
}
