#ifndef MINIOS_TASK_UPDATES_H
#define MINIOS_TASK_UPDATES_H
#include <stdint.h>

#define MINIOS_WORK_VERSION 1u
#define MINIOS_WORK_MAX_ACTORS 8u
#define MINIOS_WORK_HISTORY 32u

typedef enum {
    MINIOS_WORK_OK, MINIOS_WORK_EMPTY, MINIOS_WORK_FULL,
    MINIOS_WORK_BAD_ARGUMENT, MINIOS_WORK_BAD_ACTOR, MINIOS_WORK_BAD_TASK,
    MINIOS_WORK_BAD_RUN, MINIOS_WORK_BAD_SEQUENCE, MINIOS_WORK_BAD_PROGRESS,
    MINIOS_WORK_BUSY, MINIOS_WORK_EXHAUSTED
} MiniOSWorkStatus;
typedef enum {
    MINIOS_WORK_READY, MINIOS_WORK_RUNNING, MINIOS_WORK_DONE, MINIOS_WORK_FAILED
} MiniOSWorkState;

/* Value records only: no pointers, handles, task stacks, or borrowed strings. */
typedef struct {
    uint64_t run_id, sequence;
    int64_t result;
    uint32_t version, actor_id, task_id, completed, total, state;
    int32_t error_code;
} MiniOSWorkRecord;
typedef struct {
    uint64_t commit_sequence;
    MiniOSWorkRecord record;
} MiniOSWorkEvent;
typedef struct { uint32_t actor_id, task_id, total; } MiniOSWorkActorSpec;
typedef struct {
    uint64_t run_id, last_commit;
    uint32_t actors, pending, full_attempts;
} MiniOSWorkStats;

/* Storage is public for static allocation, not for client mutation. Initialize
 * once, then serialize ALL operations externally. One destructive consumer. */
typedef struct {
    MiniOSWorkRecord latest[MINIOS_WORK_MAX_ACTORS];
    MiniOSWorkEvent history[MINIOS_WORK_HISTORY];
    uint64_t run_id, last_commit;
    uint32_t actors, head, count, full_attempts;
} MiniOSWorkStore;

void minios_work_init(MiniOSWorkStore *store);
MiniOSWorkStatus minios_work_begin(MiniOSWorkStore *store, uint64_t run_id,
                                  const MiniOSWorkActorSpec *actors, uint32_t count);
/* Caller identity must come from the scheduler, not from the submitted record.
 * FULL commits nothing: keep the same record and retry after yielding/sleeping. */
MiniOSWorkStatus minios_work_publish(MiniOSWorkStore *store, uint32_t caller_task,
                                    const MiniOSWorkRecord *record);
MiniOSWorkStatus minios_work_snapshot(const MiniOSWorkStore *store, uint32_t actor,
                                     MiniOSWorkRecord *out);
MiniOSWorkStatus minios_work_take(MiniOSWorkStore *store, MiniOSWorkEvent *out);
MiniOSWorkStatus minios_work_stats(const MiniOSWorkStore *store, MiniOSWorkStats *out);
const char *minios_work_status_name(MiniOSWorkStatus status);
const char *minios_work_state_name(uint32_t state);
#endif
