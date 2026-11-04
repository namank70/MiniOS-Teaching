#include "minios/task_updates.h"
#include <stddef.h>

void minios_work_init(MiniOSWorkStore *s) {
    if (s) *s = (MiniOSWorkStore){0};
}
static int terminal(uint32_t state) {
    return state == MINIOS_WORK_DONE || state == MINIOS_WORK_FAILED;
}
static uint32_t actor_index(const MiniOSWorkStore *s, uint32_t actor) {
    for (uint32_t i = 0; i < s->actors; ++i)
        if (s->latest[i].actor_id == actor) return i;
    return s->actors;
}
MiniOSWorkStatus minios_work_begin(MiniOSWorkStore *s, uint64_t run,
                                  const MiniOSWorkActorSpec *actors, uint32_t count) {
    if (!s || !actors || !count || count > MINIOS_WORK_MAX_ACTORS)
        return MINIOS_WORK_BAD_ARGUMENT;
    if (!run || run <= s->run_id) return MINIOS_WORK_BAD_RUN;
    for (uint32_t i = 0; i < count; ++i) {
        if (!actors[i].actor_id || !actors[i].total) return MINIOS_WORK_BAD_ARGUMENT;
        for (uint32_t j = 0; j < i; ++j)
            if (actors[j].actor_id == actors[i].actor_id || actors[j].task_id == actors[i].task_id)
                return MINIOS_WORK_BAD_ARGUMENT;
    }
    if (s->count) return MINIOS_WORK_BUSY;
    for (uint32_t i = 0; i < s->actors; ++i)
        if (!terminal(s->latest[i].state)) return MINIOS_WORK_BUSY;
    for (uint32_t i = 0; i < count; ++i)
        s->latest[i] = (MiniOSWorkRecord){
            .version = MINIOS_WORK_VERSION, .actor_id = actors[i].actor_id,
            .task_id = actors[i].task_id, .run_id = run, .total = actors[i].total,
            .state = MINIOS_WORK_READY
        };
    s->run_id = run;
    s->actors = count;
    s->head = 0;
    s->full_attempts = 0;
    return MINIOS_WORK_OK;
}
MiniOSWorkStatus minios_work_publish(MiniOSWorkStore *s, uint32_t caller,
                                    const MiniOSWorkRecord *r) {
    if (!s || !r || r->version != MINIOS_WORK_VERSION) return MINIOS_WORK_BAD_ARGUMENT;
    uint32_t i = actor_index(s, r->actor_id);
    if (i == s->actors) return MINIOS_WORK_BAD_ACTOR;
    const MiniOSWorkRecord *old = &s->latest[i];
    if (caller != old->task_id || r->task_id != old->task_id) return MINIOS_WORK_BAD_TASK;
    if (r->run_id != s->run_id) return MINIOS_WORK_BAD_RUN;
    if (old->sequence == UINT64_MAX || s->last_commit == UINT64_MAX)
        return MINIOS_WORK_EXHAUSTED;
    if (r->sequence != old->sequence + 1) return MINIOS_WORK_BAD_SEQUENCE;
    if (terminal(old->state) || r->total != old->total || r->completed < old->completed ||
        r->completed > r->total) return MINIOS_WORK_BAD_PROGRESS;
    switch (r->state) {
    case MINIOS_WORK_RUNNING:
        if (r->completed == r->total || r->error_code) return MINIOS_WORK_BAD_PROGRESS;
        break;
    case MINIOS_WORK_DONE:
        if (r->completed != r->total || r->error_code) return MINIOS_WORK_BAD_PROGRESS;
        break;
    case MINIOS_WORK_FAILED:
        if (r->error_code <= 0) return MINIOS_WORK_BAD_PROGRESS;
        break;
    default:
        return MINIOS_WORK_BAD_PROGRESS;
    }
    if (s->count == MINIOS_WORK_HISTORY) {
        if (s->full_attempts != UINT32_MAX) ++s->full_attempts;
        return MINIOS_WORK_FULL;
    }
    /* The kernel adapter holds IRQs throughout validation and this commit. */
    s->latest[i] = *r;
    uint32_t tail = (s->head + s->count) % MINIOS_WORK_HISTORY;
    s->history[tail] = (MiniOSWorkEvent){++s->last_commit, *r};
    ++s->count;
    return MINIOS_WORK_OK;
}
MiniOSWorkStatus minios_work_snapshot(const MiniOSWorkStore *s, uint32_t actor, MiniOSWorkRecord *out) {
    if (!s || !out) return MINIOS_WORK_BAD_ARGUMENT;
    uint32_t i = actor_index(s, actor);
    if (i == s->actors) return MINIOS_WORK_BAD_ACTOR;
    *out = s->latest[i];
    return MINIOS_WORK_OK;
}
MiniOSWorkStatus minios_work_take(MiniOSWorkStore *s, MiniOSWorkEvent *out) {
    if (!s || !out) return MINIOS_WORK_BAD_ARGUMENT;
    if (!s->count) return MINIOS_WORK_EMPTY;
    *out = s->history[s->head];
    s->head = (s->head + 1) % MINIOS_WORK_HISTORY;
    --s->count;
    return MINIOS_WORK_OK;
}
MiniOSWorkStatus minios_work_stats(const MiniOSWorkStore *s, MiniOSWorkStats *out) {
    if (!s || !out) return MINIOS_WORK_BAD_ARGUMENT;
    *out = (MiniOSWorkStats){s->run_id, s->last_commit, s->actors, s->count, s->full_attempts};
    return MINIOS_WORK_OK;
}
const char *minios_work_status_name(MiniOSWorkStatus status) {
    static const char *const names[] = {"OK", "EMPTY", "FULL", "BAD_ARGUMENT", "BAD_ACTOR",
        "BAD_TASK", "BAD_RUN", "BAD_SEQUENCE", "BAD_PROGRESS", "BUSY", "EXHAUSTED"};
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status] : "UNKNOWN";
}
const char *minios_work_state_name(uint32_t state) {
    static const char *const names[] = {"READY", "RUNNING", "DONE", "FAILED"};
    return state < sizeof(names) / sizeof(names[0]) ? names[state] : "UNKNOWN";
}
