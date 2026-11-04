#include "minios/task_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    MiniOSTaskPolicy t[4]={{TASK_RUNNING,0,0},{TASK_READY,1,0},{TASK_READY,2,0},{TASK_READY,3,0}};
    assert(minios_task_select(t,4,3)==0); /* running task retains priority */
    t[0].state=TASK_SLEEPING; t[0].wake_tick=10;
    minios_tasks_wake(t,4,9); assert(t[0].state==TASK_SLEEPING);
    assert(minios_task_select(t,4,3)==1);
    minios_tasks_wake(t,4,10); assert(t[0].state==TASK_READY);
    t[0].state=TASK_SLEEPING; t[0].wake_tick=2;
    minios_tasks_wake(t,4,UINT32_MAX); assert(t[0].state==TASK_SLEEPING);
    minios_tasks_wake(t,4,1); assert(t[0].state==TASK_SLEEPING);
    minios_tasks_wake(t,4,2); assert(t[0].state==TASK_READY);
    t[0].priority=1; assert(minios_task_select(t,4,3)==0); /* stable tie */
    for(unsigned i=0;i<4;i++) t[i].state=TASK_SLEEPING;
    assert(minios_task_select(t,4,3)==3);
    puts("PASS: scheduler running priority, sleep boundary, wraparound, tie and idle");
}
