#ifndef MINIOS_TASK_POLICY_H
#define MINIOS_TASK_POLICY_H
#include <stddef.h>
#include <stdint.h>
enum { TASK_READY, TASK_RUNNING, TASK_SLEEPING };
typedef struct {
    uint32_t state;
    uint8_t priority;
    uint32_t wake_tick;
} MiniOSTaskPolicy;
/* Caller serializes mutations. Delays must be less than 2^31 ticks. */
void minios_tasks_wake(MiniOSTaskPolicy *tasks, size_t count, uint32_t now);
size_t minios_task_select(const MiniOSTaskPolicy *tasks, size_t count, size_t idle);
#endif
