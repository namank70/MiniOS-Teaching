#include "minios/kernel_work.h"
#include "platform.h"
#include "scheduler.h"

static MiniOSWorkStore store;
void minios_kernel_work_init(void) { minios_work_init(&store); }
MiniOSWorkStatus minios_kernel_work_begin(uint64_t run, const MiniOSWorkActorSpec *actors, uint32_t count) {
    if (!actors || !count || count > MINIOS_WORK_MAX_ACTORS) return MINIOS_WORK_BAD_ARGUMENT;
    for (uint32_t i = 0; i < count; ++i)
        if (!actors[i].task_id || actors[i].task_id >= NUM_TASKS - 1) return MINIOS_WORK_BAD_TASK;
    uint32_t flags = irq_save();
    MiniOSWorkStatus rc = scheduler_current_task() == 0 ?
        minios_work_begin(&store, run, actors, count) : MINIOS_WORK_BAD_TASK;
    irq_restore(flags);
    return rc;
}
MiniOSWorkStatus minios_kernel_work_publish(const MiniOSWorkRecord *record) {
    uint32_t flags = irq_save();
    MiniOSWorkStatus rc = minios_work_publish(&store, scheduler_current_task(), record);
    irq_restore(flags);
    return rc;
}
MiniOSWorkStatus minios_kernel_work_snapshot(uint32_t actor, MiniOSWorkRecord *out) {
    uint32_t flags = irq_save();
    MiniOSWorkStatus rc = minios_work_snapshot(&store, actor, out);
    irq_restore(flags);
    return rc;
}
MiniOSWorkStatus minios_kernel_work_take(MiniOSWorkEvent *out) {
    uint32_t flags = irq_save();
    MiniOSWorkStatus rc = scheduler_current_task() == 0 ?
        minios_work_take(&store, out) : MINIOS_WORK_BAD_TASK;
    irq_restore(flags);
    return rc;
}
MiniOSWorkStatus minios_kernel_work_stats(MiniOSWorkStats *out) {
    uint32_t flags = irq_save();
    MiniOSWorkStatus rc = minios_work_stats(&store, out);
    irq_restore(flags);
    return rc;
}
