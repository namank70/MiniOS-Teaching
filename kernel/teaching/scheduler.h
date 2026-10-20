#ifndef TEACHING_SCHEDULER_H
#define TEACHING_SCHEDULER_H
#include "minios/task_policy.h"
#define NUM_TASKS 4u
#define TASK_STACK_SZ 8192u
typedef void (*TaskEntry)(void);
typedef struct {
    uint32_t esp, ebp; /* ABI offsets 0 and 4 used by scheduler.asm */
    const char *name;
    TaskEntry entry;
    uint32_t switches;
} Task;
typedef struct {
    uint32_t id, state, priority, wake_tick, switches;
    const char *name;
} TaskSnapshot;
void scheduler_init(const TaskEntry entries[NUM_TASKS]);
void scheduler_start(void) __attribute__((noreturn));
void scheduler_tick(void);
void task_sleep(uint32_t ticks);
void task_yield(void);
void task_idle(void);
uint32_t scheduler_tick_now(void);
uint64_t scheduler_now_ms(void);
void scheduler_snapshot(TaskSnapshot out[NUM_TASKS]);
void switch_context(Task *prev, Task *next);
void isr_pit(void);
#endif
