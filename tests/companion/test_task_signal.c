#include "task_signal.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    task_signal_t engine={0};task_scope_t scope={0};
    task_counts_t c={.working=2,.detailed=true};
    task_signal_visual_t v=task_signal_update(&engine,100,SIGNAL_TASK_CYCLE,&c,&scope);
    assert(!v.cue_channel && v.waiting_lit);
    c.waiting=1;
    v=task_signal_update(&engine,1000000,SIGNAL_TASK_CYCLE,&c,&scope);assert(v.waiting_lit);
    v=task_signal_update(&engine,1499999,SIGNAL_TASK_CYCLE,&c,&scope);assert(v.waiting_lit);
    v=task_signal_update(&engine,1500000,SIGNAL_TASK_CYCLE,&c,&scope);assert(!v.waiting_lit);
    v=task_signal_update(&engine,2000000,SIGNAL_TASK_CYCLE,&c,&scope);assert(v.waiting_lit);
    c.done=1;c.completion_seq=1;
    v=task_signal_update(&engine,2100000,SIGNAL_TASK_CYCLE,&c,&scope);assert(!v.cue_channel);
    c.working=c.waiting=0;
    const int64_t start=3000000;
    const int channels[]={1,2,3,2,1,2,3,2};
    for(int i=0;i<8;i++){
        v=task_signal_update(&engine,start+i*500000,SIGNAL_TASK_CYCLE,&c,&scope);
        assert(v.cue_channel==channels[i] && v.hold_start_us==start+4000000);
        v=task_signal_update(&engine,start+i*500000+499999,SIGNAL_TASK_CYCLE,&c,&scope);
        assert(v.cue_channel==channels[i]);
    }
    v=task_signal_update(&engine,start+4000000,SIGNAL_TASK_CYCLE,&c,&scope);
    assert(!v.cue_channel && v.hold_start_us==start+4000000);
    v=task_signal_update(&engine,start+14000000,SIGNAL_TASK_CYCLE,&c,&scope);assert(!v.cue_channel);
    c.done=2;c.completion_seq=2;
    v=task_signal_update(&engine,18000000,SIGNAL_TASK_CYCLE,&c,&scope);assert(v.cue_channel==1);
    v=task_signal_update(&engine,23000000,SIGNAL_TASK_CYCLE,&c,&scope);assert(!v.cue_channel);
    c.done=1;c.completion_seq=1;
    v=task_signal_update(&engine,24000000,SIGNAL_TASK_CYCLE,&c,&scope);assert(!v.cue_channel);
    // A USB/Wi-Fi handoff or reconnect with the same completion never replays.
    task_counts_t disconnected={0};
    task_signal_update(&engine,25000000,SIGNAL_TASK_CYCLE,&disconnected,&scope);
    v=task_signal_update(&engine,26000000,SIGNAL_TASK_CYCLE,&c,&scope);assert(!v.cue_channel);
    c.completion_seq=3;
    v=task_signal_update(&engine,30000000,SIGNAL_TASK_CYCLE,&c,&scope);assert(v.cue_channel==1);
    c.waiting=1;
    v=task_signal_update(&engine,30100000,SIGNAL_TASK_CYCLE,&c,&scope);assert(!v.cue_channel && v.waiting_lit);
    c.waiting=0;c.working=1;
    v=task_signal_update(&engine,30200000,SIGNAL_TASK_CYCLE,&c,&scope);assert(!v.cue_channel);
    c.working=0;c.completion_seq=4;
    v=task_signal_update(&engine,30300000,SIGNAL_TASK_CYCLE,&c,&scope);assert(v.cue_channel==1);
    v=task_signal_update(&engine,30400000,SIGNAL_STEADY,&c,&scope);assert(!v.cue_channel && !v.hold_start_us && v.waiting_lit);
    v=task_signal_update(&engine,30500000,SIGNAL_TASK_CYCLE,&c,&scope);assert(v.cue_channel==1);
    scope.mode=1;snprintf(scope.target,sizeof(scope.target),"another-project");
    v=task_signal_update(&engine,31000000,SIGNAL_TASK_CYCLE,&c,&scope);assert(v.cue_channel==1);
    // Acknowledgement or loss of the selected task stops a cue permanently.
    task_signal_update(&engine,31100000,SIGNAL_TASK_CYCLE,&disconnected,&scope);
    v=task_signal_update(&engine,31200000,SIGNAL_TASK_CYCLE,&c,&scope);assert(!v.cue_channel);
    // Legacy/manual states carry no completion sequence: work re-arms them.
    engine=(task_signal_t){0};scope=(task_scope_t){0};c=(task_counts_t){.done=1};
    v=task_signal_update(&engine,1,SIGNAL_TASK_CYCLE,&c,&scope);assert(v.cue_channel==1);
    v=task_signal_update(&engine,5000000,SIGNAL_TASK_CYCLE,&c,&scope);assert(!v.cue_channel);
    c.done=0;c.working=1;task_signal_update(&engine,5100000,SIGNAL_TASK_CYCLE,&c,&scope);
    c.done=1;c.working=0;v=task_signal_update(&engine,5200000,SIGNAL_TASK_CYCLE,&c,&scope);assert(v.cue_channel==1);
    puts("Task signal: 500ms waiting blink, R-Y-G-Y twice, heartbeat dedup, interruption, reconnect, scope and steady compatibility: PASS");
}
