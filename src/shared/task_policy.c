#include "minios/task_policy.h"
void minios_tasks_wake(MiniOSTaskPolicy *tasks, size_t count, uint32_t now)
{
    for (size_t i = 0; i < count; ++i)
        if (tasks[i].state == TASK_SLEEPING &&
            (int32_t)(now - tasks[i].wake_tick) >= 0)
            tasks[i].state = TASK_READY;
}
size_t minios_task_select(const MiniOSTaskPolicy *tasks, size_t count, size_t idle)
{
    size_t best = count;
    for (size_t i = 0; i < count; ++i) {
        if ((tasks[i].state == TASK_READY || tasks[i].state == TASK_RUNNING) &&
            (best == count || tasks[i].priority < tasks[best].priority))
            best = i;
    }
    return best == count ? idle : best;
}
