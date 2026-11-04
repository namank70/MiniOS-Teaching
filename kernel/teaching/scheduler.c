/* Adapted from External-Scheduler-ABI at 785d989 (see provenance.md).
 * The switch ABI/four fixed-priority tasks are retained. Timer advancement,
 * runnable selection, IRQ serialization and bootstrap ownership are corrected. */
#include "scheduler.h"
#include "platform.h"
#include "console.h"
#include <stddef.h>
static Task tasks[NUM_TASKS];
static MiniOSTaskPolicy policy[NUM_TASKS];
static uint8_t stacks[NUM_TASKS][TASK_STACK_SZ] __attribute__((aligned(16)));
static uint32_t ticks, current;
static uint64_t uptime;
_Static_assert(offsetof(Task,esp)==0,"ESP ABI");
_Static_assert(offsetof(Task,ebp)==4,"EBP ABI");
typedef struct __attribute__((packed)) { uint16_t lo,cs; uint8_t zero,flags; uint16_t hi; } Gate;
static Gate idt[256];
extern void *exception_stubs[32];
extern void isr_unexpected(void);
static void set_gate(unsigned n, void *handler) {
    uintptr_t p=(uintptr_t)handler;
    idt[n]=(Gate){(uint16_t)p,8,0,0x8e,(uint16_t)(p>>16)};
}
void kernel_exception(uint32_t vector, uint32_t error) {
    __asm__ volatile("cli");
    console_write("\nPANIC exception="); console_uint(vector);
    console_write(" error="); console_uint(error); console_write("\n");
    for(;;) __asm__ volatile("hlt");
}
static void hardware_init(void) {
    for(unsigned i=0;i<256;i++) set_gate(i,isr_unexpected);
    for(unsigned i=0;i<32;i++) set_gate(i,exception_stubs[i]);
    set_gate(32,isr_pit);
    struct __attribute__((packed)) { uint16_t size; uintptr_t base; } ptr={sizeof(idt)-1,(uintptr_t)idt};
    __asm__ volatile("lidt %0" :: "m"(ptr));
    outb(0x20,0x11); outb(0x80,0); outb(0xa0,0x11); outb(0x80,0);
    outb(0x21,0x20); outb(0x80,0); outb(0xa1,0x28); outb(0x80,0);
    outb(0x21,4); outb(0x80,0); outb(0xa1,2); outb(0x80,0);
    outb(0x21,1); outb(0x80,0); outb(0xa1,1); outb(0x80,0);
    outb(0x21,0xfe); outb(0xa1,0xff); /* only IRQ0; input is polled */
    outb(0x43,0x36); outb(0x40,1193&255); outb(0x40,1193>>8);
}
static void task_bootstrap(void) {
    /* Fresh tasks enter with IF clear; enable only after the switch is done. */
    __asm__ volatile("sti");
    tasks[current].entry();
    kernel_exception(255,1); /* task entries must not return */
}
void scheduler_init(const TaskEntry entries[NUM_TASKS]) {
    static const char *names[]={"shell","recommendation","runtime","idle"};
    for(unsigned i=0;i<NUM_TASKS;i++) {
        uint32_t *sp=(uint32_t *)(stacks[i]+TASK_STACK_SZ);
        *--sp=(uint32_t)task_bootstrap;
        uint32_t *bp=sp;
        *--sp=0;
        for(unsigned r=0;r<8;r++) *--sp=0;
        *--sp=2; /* EFLAGS IF=0; task_bootstrap enables interrupts */
        tasks[i]=(Task){(uint32_t)sp,(uint32_t)bp,names[i],entries[i],0};
        policy[i]=(MiniOSTaskPolicy){TASK_READY,(uint8_t)i,0};
    }
    hardware_init();
}
static void dispatch(void) {
    uint32_t best=(uint32_t)minios_task_select(policy,NUM_TASKS,NUM_TASKS-1);
    if(best==current) return;
    uint32_t prev=current;
    if(policy[prev].state==TASK_RUNNING) policy[prev].state=TASK_READY;
    policy[best].state=TASK_RUNNING; current=best; tasks[best].switches++;
    switch_context(&tasks[prev],&tasks[best]);
}
void scheduler_start(void) {
    static Task boot;
    current=0; policy[0].state=TASK_RUNNING; tasks[0].switches=1;
    switch_context(&boot,&tasks[0]);
    for(;;) __asm__ volatile("cli; hlt");
}
void scheduler_tick(void) {
    ++ticks; ++uptime;
    minios_tasks_wake(policy,NUM_TASKS,ticks);
    dispatch();
}
void task_sleep(uint32_t delay) {
    uint32_t f=irq_save();
    if(delay && delay<UINT32_C(0x80000000)) {
        policy[current].wake_tick=ticks+delay; policy[current].state=TASK_SLEEPING;
    }
    dispatch(); irq_restore(f);
}
void task_yield(void) { uint32_t f=irq_save(); dispatch(); irq_restore(f); }
void task_idle(void) { for(;;) __asm__ volatile("hlt"); }
uint32_t scheduler_tick_now(void) { uint32_t f=irq_save(), n=ticks; irq_restore(f); return n; }
uint64_t scheduler_now_ms(void) { uint32_t f=irq_save(); uint64_t n=uptime; irq_restore(f); return n; }
void scheduler_snapshot(TaskSnapshot out[NUM_TASKS]) {
    uint32_t f=irq_save();
    for(unsigned i=0;i<NUM_TASKS;i++) out[i]=(TaskSnapshot){i,policy[i].state,policy[i].priority,policy[i].wake_tick,tasks[i].switches,tasks[i].name};
    irq_restore(f);
}
