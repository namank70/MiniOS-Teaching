#ifndef MINIOS_KERNEL_WORK_H
#define MINIOS_KERNEL_WORK_H
#include "minios/task_updates.h"

/* Single-CPU, task-context adapter. Call init once before scheduling starts.
 * Shell (task 0) owns begin/take; workers own their registered publication.
 * All readers receive copies. Not a process isolation/security boundary. */
void minios_kernel_work_init(void);
MiniOSWorkStatus minios_kernel_work_begin(uint64_t run, const MiniOSWorkActorSpec *actors, uint32_t count);
MiniOSWorkStatus minios_kernel_work_publish(const MiniOSWorkRecord *record);
MiniOSWorkStatus minios_kernel_work_snapshot(uint32_t actor, MiniOSWorkRecord *out);
MiniOSWorkStatus minios_kernel_work_take(MiniOSWorkEvent *out);
MiniOSWorkStatus minios_kernel_work_stats(MiniOSWorkStats *out);
#endif
