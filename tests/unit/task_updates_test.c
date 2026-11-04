#include "minios/task_updates.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const MiniOSWorkActorSpec pair[] = {{5, 3, 3}, {6, 4, 3}};
static MiniOSWorkRecord next(MiniOSWorkStore *s, uint32_t actor, uint32_t completed, int64_t result) {
    MiniOSWorkRecord r;
    assert(minios_work_snapshot(s, actor, &r) == MINIOS_WORK_OK);
    ++r.sequence;
    r.completed = completed;
    r.result = result;
    r.state = completed == r.total ? MINIOS_WORK_DONE : MINIOS_WORK_RUNNING;
    return r;
}
static void commit(MiniOSWorkStore *s, uint32_t actor, uint32_t completed, int64_t result) {
    MiniOSWorkRecord r = next(s, actor, completed, result);
    assert(minios_work_publish(s, r.task_id, &r) == MINIOS_WORK_OK);
}
static void rejected(MiniOSWorkStore *s, uint32_t task, const MiniOSWorkRecord *r, MiniOSWorkStatus status) {
    unsigned char before[sizeof(*s)];
    memcpy(before, s, sizeof(*s));
    assert(minios_work_publish(s, task, r) == status);
    assert(memcmp(before, s, sizeof(*s)) == 0);
}
static void ordering_and_results(void) {
    MiniOSWorkStore s;
    minios_work_init(&s);
    assert(minios_work_begin(&s, 1, pair, 2) == MINIOS_WORK_OK);
    MiniOSWorkRecord r;
    assert(minios_work_snapshot(&s, 5, &r) == MINIOS_WORK_OK);
    assert(r.state == MINIOS_WORK_READY && r.sequence == 0 && r.completed == 0);
    /* Deliberately not the demo's alternating order: API orders actual commits. */
    commit(&s, 6, 1, 11);
    commit(&s, 5, 1, -12);
    commit(&s, 5, 2, -30);
    commit(&s, 6, 3, INT64_MAX);
    commit(&s, 5, 3, INT64_MIN);
    const uint32_t actors[] = {6, 5, 5, 6, 5};
    const uint64_t sequences[] = {1, 1, 2, 2, 3};
    const int64_t results[] = {11, -12, -30, INT64_MAX, INT64_MIN};
    for (unsigned i = 0; i < 5; ++i) {
        MiniOSWorkEvent e;
        assert(minios_work_take(&s, &e) == MINIOS_WORK_OK);
        assert(e.commit_sequence == i + 1 && e.record.run_id == 1);
        assert(e.record.actor_id == actors[i] && e.record.sequence == sequences[i]);
        assert(e.record.result == results[i]);
        assert(e.record.state == (i >= 3 ? MINIOS_WORK_DONE : MINIOS_WORK_RUNNING));
    }
    MiniOSWorkEvent empty = {.commit_sequence = 123};
    assert(minios_work_take(&s, &empty) == MINIOS_WORK_EMPTY && empty.commit_sequence == 123);
    assert(minios_work_snapshot(&s, 5, &r) == MINIOS_WORK_OK);
    assert(r.result == INT64_MIN && r.state == MINIOS_WORK_DONE && r.sequence == 3);
    ++r.sequence;
    rejected(&s, 3, &r, MINIOS_WORK_BAD_PROGRESS);
    puts("PASS: commit order, copied history, retained final results and terminal state");
}
static void invalid_inputs(void) {
    MiniOSWorkStore s;
    minios_work_init(&s);
    MiniOSWorkRecord r = {0}, out = {.actor_id = 99};
    MiniOSWorkEvent event;
    MiniOSWorkStats stats;
    assert(minios_work_begin(NULL, 1, pair, 2) == MINIOS_WORK_BAD_ARGUMENT);
    assert(minios_work_begin(&s, 1, NULL, 2) == MINIOS_WORK_BAD_ARGUMENT);
    assert(minios_work_begin(&s, 1, pair, 0) == MINIOS_WORK_BAD_ARGUMENT);
    assert(minios_work_begin(&s, 1, pair, MINIOS_WORK_MAX_ACTORS + 1) == MINIOS_WORK_BAD_ARGUMENT);
    assert(minios_work_begin(&s, 0, pair, 2) == MINIOS_WORK_BAD_RUN);
    assert(minios_work_snapshot(NULL, 5, &out) == MINIOS_WORK_BAD_ARGUMENT);
    assert(minios_work_snapshot(&s, 5, NULL) == MINIOS_WORK_BAD_ARGUMENT);
    assert(minios_work_snapshot(&s, 5, &out) == MINIOS_WORK_BAD_ACTOR && out.actor_id == 99);
    assert(minios_work_take(NULL, &event) == MINIOS_WORK_BAD_ARGUMENT);
    assert(minios_work_take(&s, NULL) == MINIOS_WORK_BAD_ARGUMENT);
    assert(minios_work_stats(NULL, &stats) == MINIOS_WORK_BAD_ARGUMENT);
    assert(minios_work_stats(&s, NULL) == MINIOS_WORK_BAD_ARGUMENT);
    assert(minios_work_publish(NULL, 3, &r) == MINIOS_WORK_BAD_ARGUMENT);
    assert(minios_work_begin(&s, 1, pair, 2) == MINIOS_WORK_OK);
    rejected(&s, 3, NULL, MINIOS_WORK_BAD_ARGUMENT);
    r = next(&s, 5, 1, 17);
    MiniOSWorkRecord good = r;
    r.version = 99; rejected(&s, 3, &r, MINIOS_WORK_BAD_ARGUMENT); r = good;
    r.actor_id = 0; rejected(&s, 3, &r, MINIOS_WORK_BAD_ACTOR); r = good;
    rejected(&s, 4, &r, MINIOS_WORK_BAD_TASK);
    r.task_id = 4; rejected(&s, 3, &r, MINIOS_WORK_BAD_TASK); r = good;
    r.run_id = 0; rejected(&s, 3, &r, MINIOS_WORK_BAD_RUN); r = good;
    r.run_id = 2; rejected(&s, 3, &r, MINIOS_WORK_BAD_RUN); r = good;
    r.sequence = 0; rejected(&s, 3, &r, MINIOS_WORK_BAD_SEQUENCE); r = good;
    r.sequence = 2; rejected(&s, 3, &r, MINIOS_WORK_BAD_SEQUENCE); r = good;
    r.total = 0; rejected(&s, 3, &r, MINIOS_WORK_BAD_PROGRESS); r = good;
    r.total = 4; rejected(&s, 3, &r, MINIOS_WORK_BAD_PROGRESS); r = good;
    r.completed = 4; rejected(&s, 3, &r, MINIOS_WORK_BAD_PROGRESS); r = good;
    r.completed = 3; rejected(&s, 3, &r, MINIOS_WORK_BAD_PROGRESS); r = good;
    r.state = MINIOS_WORK_DONE; rejected(&s, 3, &r, MINIOS_WORK_BAD_PROGRESS); r = good;
    r.state = MINIOS_WORK_READY; rejected(&s, 3, &r, MINIOS_WORK_BAD_PROGRESS); r = good;
    r.state = 99; rejected(&s, 3, &r, MINIOS_WORK_BAD_PROGRESS); r = good;
    r.error_code = 1; rejected(&s, 3, &r, MINIOS_WORK_BAD_PROGRESS); r = good;
    r.state = MINIOS_WORK_FAILED; rejected(&s, 3, &r, MINIOS_WORK_BAD_PROGRESS);
    r.error_code = -1; rejected(&s, 3, &r, MINIOS_WORK_BAD_PROGRESS);
    commit(&s, 5, 1, 17);
    rejected(&s, 3, &good, MINIOS_WORK_BAD_SEQUENCE);
    r = next(&s, 5, 0, 0); rejected(&s, 3, &r, MINIOS_WORK_BAD_PROGRESS);
    r = next(&s, 5, 3, 0); r.error_code = 1; rejected(&s, 3, &r, MINIOS_WORK_BAD_PROGRESS);
    puts("PASS: invalid pointers, identities, versions, runs, sequences and progress are atomic rejections");
}
static void actor_bounds(void) {
    MiniOSWorkStore s;
    MiniOSWorkActorSpec specs[MINIOS_WORK_MAX_ACTORS];
    for (unsigned i = 0; i < MINIOS_WORK_MAX_ACTORS; ++i)
        specs[i] = (MiniOSWorkActorSpec){100 + i, i, 1};
    minios_work_init(&s);
    specs[7].actor_id = specs[0].actor_id;
    assert(minios_work_begin(&s, 1, specs, 8) == MINIOS_WORK_BAD_ARGUMENT);
    specs[7].actor_id = 107; specs[7].task_id = 0;
    assert(minios_work_begin(&s, 1, specs, 8) == MINIOS_WORK_BAD_ARGUMENT);
    specs[7].task_id = 7; specs[7].total = 0;
    assert(minios_work_begin(&s, 1, specs, 8) == MINIOS_WORK_BAD_ARGUMENT);
    specs[7].total = 1; specs[7].actor_id = 0;
    assert(minios_work_begin(&s, 1, specs, 8) == MINIOS_WORK_BAD_ARGUMENT);
    specs[7].actor_id = 107;
    assert(s.run_id == 0 && s.actors == 0 && s.count == 0);
    assert(minios_work_begin(&s, 1, specs, 8) == MINIOS_WORK_OK);
    for (unsigned i = 0; i < 8; ++i) commit(&s, 100 + i, 1, i);
    assert(s.count == 8);
    puts("PASS: registration bounds, duplicate identities and all eight actor slots");
}
static void full_history_and_wrap(void) {
    MiniOSWorkStore s;
    const MiniOSWorkActorSpec spec = {5, 3, 33};
    minios_work_init(&s);
    assert(minios_work_begin(&s, 1, &spec, 1) == MINIOS_WORK_OK);
    for (unsigned i = 1; i <= 32; ++i) commit(&s, 5, i, -(int64_t)i);
    MiniOSWorkRecord final = next(&s, 5, 33, -1234567890123LL);
    MiniOSWorkStore before;
    memcpy(&before, &s, sizeof(s));
    assert(minios_work_publish(&s, 3, &final) == MINIOS_WORK_FULL);
    ++before.full_attempts;
    assert(memcmp(&before, &s, sizeof(s)) == 0);
    assert(s.latest[0].state == MINIOS_WORK_RUNNING && s.latest[0].completed == 32);
    /* Bad publications cannot consume capacity or inflate the FULL counter. */
    rejected(&s, 4, &final, MINIOS_WORK_BAD_TASK);
    MiniOSWorkEvent e;
    assert(minios_work_take(&s, &e) == MINIOS_WORK_OK && e.commit_sequence == 1);
    assert(minios_work_publish(&s, 3, &final) == MINIOS_WORK_OK);
    for (unsigned i = 2; i <= 33; ++i) {
        assert(minios_work_take(&s, &e) == MINIOS_WORK_OK);
        assert(e.commit_sequence == i && e.record.sequence == i);
    }
    assert(e.record.state == MINIOS_WORK_DONE && e.record.result == final.result);
    assert(minios_work_take(&s, &e) == MINIOS_WORK_EMPTY);
    assert(minios_work_begin(&s, 2, &spec, 1) == MINIOS_WORK_OK);
    for (unsigned i = 1; i <= 33; ++i) {
        commit(&s, 5, i, i);
        assert(minios_work_take(&s, &e) == MINIOS_WORK_OK);
        assert(e.commit_sequence == 33 + i && e.record.run_id == 2 && e.record.sequence == i);
    }
    assert(s.head == 1 && s.count == 0); /* wrap with an observer keeping up */
    puts("PASS: full history preserves unread events and snapshot; retry commits final result; ring wraps");
}
static void lifecycle_failure_and_overflow(void) {
    MiniOSWorkStore s;
    minios_work_init(&s);
    assert(minios_work_begin(&s, 1, pair, 2) == MINIOS_WORK_OK);
    assert(minios_work_begin(&s, 2, pair, 2) == MINIOS_WORK_BUSY);
    commit(&s, 5, 3, 42);
    MiniOSWorkRecord failure = next(&s, 6, 1, -7);
    failure.state = MINIOS_WORK_FAILED; failure.error_code = 9;
    assert(minios_work_publish(&s, 4, &failure) == MINIOS_WORK_OK);
    ++failure.sequence;
    rejected(&s, 4, &failure, MINIOS_WORK_BAD_PROGRESS);
    assert(minios_work_begin(&s, 2, pair, 2) == MINIOS_WORK_BUSY); /* unread terminal events */
    MiniOSWorkEvent e;
    assert(minios_work_take(&s, &e) == MINIOS_WORK_OK);
    assert(minios_work_take(&s, &e) == MINIOS_WORK_OK);
    assert(e.record.state == MINIOS_WORK_FAILED && e.record.error_code == 9 && e.record.result == -7);
    assert(minios_work_begin(&s, 1, pair, 2) == MINIOS_WORK_BAD_RUN);
    assert(minios_work_begin(&s, 2, pair, 2) == MINIOS_WORK_OK);
    rejected(&s, 4, &failure, MINIOS_WORK_BAD_RUN);
    MiniOSWorkStats stats;
    assert(minios_work_stats(&s, &stats) == MINIOS_WORK_OK);
    assert(stats.run_id == 2 && stats.last_commit == 2 && stats.actors == 2 && stats.pending == 0);
    MiniOSWorkRecord r = next(&s, 5, 1, 1);
    s.last_commit = UINT64_MAX; /* fault injection at unreachably large counter */
    rejected(&s, 3, &r, MINIOS_WORK_EXHAUSTED);
    s.last_commit = 2; s.latest[0].sequence = UINT64_MAX;
    rejected(&s, 3, &r, MINIOS_WORK_EXHAUSTED);
    s.latest[0].sequence = 0;
    for (unsigned i = 0; i < 32; ++i) commit(&s, 5, 1, i);
    r = next(&s, 5, 2, 0);
    s.full_attempts = UINT32_MAX;
    assert(minios_work_publish(&s, 3, &r) == MINIOS_WORK_FULL && s.full_attempts == UINT32_MAX);
    puts("PASS: run lifecycle, busy history, failure results, stale updates and counter overflow");
}
int main(void) {
    ordering_and_results();
    invalid_inputs();
    actor_bounds();
    full_history_and_wrap();
    lifecycle_failure_and_overflow();
    puts("Shared work update tests passed (5 groups).");
    return 0;
}
