#include "shared_work.h"
#include "console.h"
#include "scheduler.h"
#include "string.h"
#include "minios/kernel_work.h"
#include "minios/runtime.h"

enum { ACTOR_A = 5, ACTOR_B = 6, TASK_A = 3, TASK_B = 4, ROWS_PER_WORKER = 20 };
/* These control/summary fields belong only to the shell task. Workers discover
 * their run and checkpoint exclusively through the shared record API. */
static int watching;
static uint64_t reported_run, first_commit;
static int64_t expected_a, expected_b;

static int model_valid(const MiniOSModelView *model) {
    return model->weights && model->width == 256 && model->height == 256 &&
        model->element_count == 256u * 256u && model->byte_length == 256u * 256u * sizeof(int32_t);
}
static int64_t row_sum(const MiniOSModelView *model, uint32_t row) {
    int64_t sum = 0;
    for (uint32_t col = 0; col < model->width; ++col)
        sum += model->weights[row * model->width + col];
    return sum;
}
static void worker(uint32_t actor, uint32_t other_actor, uint32_t first_row) {
    MiniOSModelView model;
    int valid = minios_runtime_model(&model) == MINIOS_RUNTIME_OK && model_valid(&model);
    for (;;) {
        MiniOSWorkRecord own, other;
        if (minios_kernel_work_snapshot(actor, &own) != MINIOS_WORK_OK ||
            own.state == MINIOS_WORK_DONE || own.state == MINIOS_WORK_FAILED ||
            minios_kernel_work_snapshot(other_actor, &other) != MINIOS_WORK_OK ||
            own.run_id != other.run_id) {
            task_sleep(20);
            continue;
        }
        if (!valid || other.state == MINIOS_WORK_FAILED) {
            own.state = MINIOS_WORK_FAILED;
            own.error_code = valid ? 2 : 1; /* peer failed / invalid shared model */
        } else {
            /* Demo-only turn rule makes A1,B1,A2,B2,... reproducible regardless
             * of wake timing. The publication API imposes no such ordering. */
            int turn = actor == ACTOR_A ? own.completed == other.completed : own.completed < other.completed;
            if (!turn) { task_sleep(20); continue; }
            own.result += row_sum(&model, first_row + own.completed);
            ++own.completed;
            own.state = own.completed == own.total ? MINIOS_WORK_DONE : MINIOS_WORK_RUNNING;
        }
        ++own.sequence;
        MiniOSWorkStatus rc;
        do {
            rc = minios_kernel_work_publish(&own);
            /* Retain this exact pending value; no progress/sequence advanced
             * in shared state until the queue has room for the whole commit. */
            if (rc == MINIOS_WORK_FULL) task_sleep(20);
        } while (rc == MINIOS_WORK_FULL);
        /* Any other rejection is a programming error; stop this task visibly
         * in the exception diagnostics rather than pretend work succeeded. */
        if (rc != MINIOS_WORK_OK) {
            extern void kernel_exception(uint32_t vector, uint32_t error);
            kernel_exception(254, (uint32_t)rc);
        }
        task_sleep(20);
    }
}
void shared_worker_a(void) { worker(ACTOR_A, ACTOR_B, 0); }
void shared_worker_b(void) { worker(ACTOR_B, ACTOR_A, ROWS_PER_WORKER); }

static void print_record(const MiniOSWorkRecord *r) {
    console_write(" run="); console_uint64(r->run_id);
    console_write(" actor="); console_uint(r->actor_id);
    console_write(" task="); console_uint(r->task_id);
    console_write(" seq="); console_uint64(r->sequence);
    console_write(" completed="); console_uint(r->completed); console_write("/"); console_uint(r->total);
    console_write(" state="); console_write(minios_work_state_name(r->state));
    console_write(" result="); console_int64(r->result);
    console_write(" error="); console_int64(r->error_code); console_write("\n");
}
static int report_completion(void) {
    MiniOSWorkStats stats;
    MiniOSWorkRecord a, b;
    minios_kernel_work_stats(&stats);
    if (!stats.run_id || stats.run_id == reported_run || stats.pending ||
        minios_kernel_work_snapshot(ACTOR_A, &a) != MINIOS_WORK_OK ||
        minios_kernel_work_snapshot(ACTOR_B, &b) != MINIOS_WORK_OK ||
        a.state < MINIOS_WORK_DONE || b.state < MINIOS_WORK_DONE) return 0;
    uint64_t events = stats.last_commit - first_commit;
    int ok = a.state == MINIOS_WORK_DONE && b.state == MINIOS_WORK_DONE &&
        a.completed == ROWS_PER_WORKER && b.completed == ROWS_PER_WORKER &&
        a.result == expected_a && b.result == expected_b && events == 2 * ROWS_PER_WORKER;
    console_write("WORK DONE run="); console_uint64(stats.run_id);
    console_write(" events="); console_uint64(events);
    console_write(" result_a="); console_int64(a.result);
    console_write(" result_b="); console_int64(b.result);
    console_write(ok ? " PASS\n" : " FAIL\n");
    reported_run = stats.run_id;
    return 1;
}
static int drain(void) {
    MiniOSWorkEvent event;
    unsigned count = 0;
    /* Bound the console slice so commands/recommendations continue to run. */
    while (count < MINIOS_WORK_HISTORY && minios_kernel_work_take(&event) == MINIOS_WORK_OK) {
        if (!count) console_write("\n");
        console_write("[work] commit="); console_uint64(event.commit_sequence);
        print_record(&event.record);
        ++count;
    }
    int reported = report_completion();
    return count != 0 || reported;
}
int shared_work_observe(void) { return watching ? drain() : 0; }
void shared_work_progress(void) {
    MiniOSWorkStats stats;
    minios_kernel_work_stats(&stats);
    console_write("WORK run="); console_uint64(stats.run_id);
    console_write(" pending="); console_uint(stats.pending); console_write("/32 full_retries=");
    console_uint(stats.full_attempts); console_write(" observer="); console_write(watching ? "watching\n" : "paused\n");
    for (uint32_t actor = ACTOR_A; actor <= ACTOR_B; ++actor) {
        MiniOSWorkRecord r;
        if (minios_kernel_work_snapshot(actor, &r) == MINIOS_WORK_OK) {
            console_write("[snapshot]"); print_record(&r);
        }
    }
}
static void start(int observe) {
    MiniOSWorkStats stats;
    MiniOSModelView model;
    minios_kernel_work_stats(&stats);
    if (stats.run_id == UINT64_MAX) { console_write("Work start: EXHAUSTED\n"); return; }
    if (minios_runtime_model(&model) != MINIOS_RUNTIME_OK || !model_valid(&model)) {
        console_write("Work start: invalid shared model\n"); return;
    }
    const MiniOSWorkActorSpec actors[] = {{ACTOR_A, TASK_A, ROWS_PER_WORKER}, {ACTOR_B, TASK_B, ROWS_PER_WORKER}};
    MiniOSWorkStatus rc = minios_kernel_work_begin(stats.run_id + 1, actors, 2);
    if (rc != MINIOS_WORK_OK) {
        console_write("Work start: "); console_write(minios_work_status_name(rc)); console_write("\n"); return;
    }
    watching = observe;
    first_commit = stats.last_commit;
    expected_a = expected_b = 0;
    /* Independently traverse each partition for the supervisor's result check.
     * This calculation and all console writes run outside the IRQ guard. */
    for (uint32_t row = 0; row < ROWS_PER_WORKER; ++row) {
        expected_a += row_sum(&model, row);
        expected_b += row_sum(&model, ROWS_PER_WORKER + row);
    }
    console_write("WORK START run="); console_uint64(stats.run_id + 1);
    console_write(observe ? " observer=watching\n" : " observer=paused\n");
    console_write("Two kernel tasks share the static model; no separate processes.\n");
}
int shared_work_command(const char *line) {
    if (!strcmp(line, "work")) start(1);
    else if (!strcmp(line, "work-pause")) start(0);
    else if (!strcmp(line, "updates")) { if (!drain()) console_write("No committed updates pending.\n"); }
    else if (!strcmp(line, "watch")) { watching = 1; console_write("Work observer resumed.\n"); }
    else return 0;
    return 1;
}
