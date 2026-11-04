#define _GNU_SOURCE

#include "minios/process_region.h"
#include "process_region_test_hooks.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static unsigned int checks;
static unsigned int failures;
static uint64_t name_nonce;

static void check(int condition, const char *description)
{
    ++checks;
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", description);
    }
}

static uint64_t next_run_id(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return ((uint64_t)(uint32_t)getpid() << 32) | ++name_nonce;
    }
    return ((uint64_t)(uint32_t)getpid() << 32) ^
           (uint64_t)now.tv_nsec ^ ++name_nonce;
}

static int make_names(uint64_t run_id,
                      char model_name[MINIOS_PROCESS_REGION_NAME_MAX],
                      char progress_name[MINIOS_PROCESS_REGION_NAME_MAX])
{
    int model_length = snprintf(model_name,
                                MINIOS_PROCESS_REGION_NAME_MAX,
                                "/minios-model-%" PRIu64, run_id);
    int progress_length = snprintf(progress_name,
                                   MINIOS_PROCESS_REGION_NAME_MAX,
                                   "/minios-progress-%" PRIu64, run_id);
    return model_length > 0 &&
           (size_t)model_length < MINIOS_PROCESS_REGION_NAME_MAX &&
           progress_length > 0 &&
           (size_t)progress_length < MINIOS_PROCESS_REGION_NAME_MAX;
}

static MiniOSProcessStatus create_region(
    MiniOSProcessRegion *region,
    uint64_t run_id,
    const uint32_t *actor_ids,
    const uint32_t *actor_totals,
    size_t actor_count)
{
    char model_name[MINIOS_PROCESS_REGION_NAME_MAX];
    char progress_name[MINIOS_PROCESS_REGION_NAME_MAX];
    MiniOSProcessRegionSpec spec;

    if (!make_names(run_id, model_name, progress_name)) {
        return MINIOS_PROCESS_BAD_ARGUMENT;
    }
    spec.model_name = model_name;
    spec.progress_name = progress_name;
    spec.run_id = run_id;
    spec.model_version = 1u;
    spec.width = 64u;
    spec.height = 64u;
    spec.actor_ids = actor_ids;
    spec.actor_totals = actor_totals;
    spec.actor_count = actor_count;
    return minios_process_region_create(region, &spec);
}

static int parse_u32(const char *text, uint32_t *out)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value > UINT32_MAX) {
        return 0;
    }
    *out = (uint32_t)value;
    return 1;
}

static uint64_t hash_values(uint64_t hash,
                            const int32_t *weights,
                            size_t begin,
                            size_t end)
{
    size_t index;
    if (hash == 0u) {
        hash = UINT64_C(14695981039346656037);
    }
    for (index = begin; index < end; ++index) {
        hash = (hash ^ (uint32_t)weights[index]) *
               UINT64_C(1099511628211);
    }
    return hash;
}

static int worker_run(const char *model_name,
                      const char *progress_name,
                      uint32_t actor_id,
                      uint32_t generation,
                      size_t begin,
                      size_t count,
                      uint32_t total,
                      uint32_t stop_after)
{
    MiniOSProcessRegion region;
    MiniOSProcessModelView model;
    MiniOSProcessProgress progress;
    MiniOSProcessStatus status;
    uint32_t step;
    uint64_t result;

    minios_process_region_init(&region);
    status = minios_process_region_attach(&region, model_name, progress_name);
    if (status != MINIOS_PROCESS_OK) {
        fprintf(stderr, "worker attach failed: %d\n", (int)status);
        return 20;
    }
    status = minios_process_model_view(&region, &model);
    if (status != MINIOS_PROCESS_OK ||
        begin > model.element_count ||
        count > model.element_count - begin) {
        (void)minios_process_region_detach(&region);
        return 21;
    }
    status = minios_process_progress_snapshot(&region, actor_id, &progress);
    if (status != MINIOS_PROCESS_OK || progress.total != total ||
        progress.generation + 1u < generation ||
        progress.generation + 1u > generation) {
        (void)minios_process_region_detach(&region);
        return 22;
    }
    result = progress.result;

    if (progress.generation != 0u &&
        generation == progress.generation + 1u) {
        status = minios_process_progress_publish(
            &region, actor_id, generation, progress.sequence + 1u,
            MINIOS_PROCESS_STATE_RUNNING, progress.completed, total,
            result, 10000u);
        if (status != MINIOS_PROCESS_OK) {
            (void)minios_process_region_detach(&region);
            return 25;
        }
        ++progress.sequence;
    }

    for (step = progress.completed; step < total && step < stop_after;
         ++step) {
        size_t local_begin = (size_t)(((uint64_t)step * count) / total);
        size_t local_end = (size_t)(((uint64_t)(step + 1u) * count) /
                                    total);
        MiniOSProcessState next_state =
            step + 1u == total
                ? MINIOS_PROCESS_STATE_DONE
                : MINIOS_PROCESS_STATE_RUNNING;

        result = hash_values(result, model.weights,
                             begin + local_begin, begin + local_end);
        status = minios_process_progress_publish(
            &region, actor_id, generation, progress.sequence + 1u,
            next_state, step + 1u, total, result, 10000u);
        if (status != MINIOS_PROCESS_OK) {
            fprintf(stderr, "worker publish failed: %d\n", (int)status);
            (void)minios_process_region_detach(&region);
            return 23;
        }
        ++progress.sequence;
    }
    status = minios_process_region_detach(&region);
    return status == MINIOS_PROCESS_OK ? 0 : 24;
}

static pid_t spawn_worker(const char *executable,
                          const char *model_name,
                          const char *progress_name,
                          uint32_t actor_id,
                          uint32_t generation,
                          size_t begin,
                          size_t count,
                          uint32_t total,
                          uint32_t stop_after)
{
    char actor_text[16];
    char generation_text[16];
    char begin_text[32];
    char count_text[32];
    char total_text[16];
    char stop_text[16];
    pid_t pid;

    (void)snprintf(actor_text, sizeof(actor_text), "%" PRIu32, actor_id);
    (void)snprintf(generation_text, sizeof(generation_text), "%" PRIu32,
                   generation);
    (void)snprintf(begin_text, sizeof(begin_text), "%zu", begin);
    (void)snprintf(count_text, sizeof(count_text), "%zu", count);
    (void)snprintf(total_text, sizeof(total_text), "%" PRIu32, total);
    (void)snprintf(stop_text, sizeof(stop_text), "%" PRIu32, stop_after);

    pid = fork();
    if (pid != 0) {
        return pid;
    }
    execl(executable, executable, "--worker", model_name, progress_name,
          actor_text, generation_text, begin_text, count_text, total_text,
          stop_text, (char *)NULL);
    _exit(127);
}

static int wait_for_child(pid_t pid, int *status)
{
    pid_t result;
    do {
        result = waitpid(pid, status, 0);
    } while (result < 0 && errno == EINTR);
    return result == pid;
}

static uint64_t expected_hash(const MiniOSProcessModelView *model,
                              size_t begin,
                              size_t count)
{
    return hash_values(0u, model->weights, begin, begin + count);
}

static void test_model_region_and_permissions(void)
{
    MiniOSProcessRegion owner;
    MiniOSProcessModelView view;
    const void *last_value = NULL;
    const void *invalid = (const void *)(uintptr_t)1u;
    uint32_t actors[] = {1u};
    uint32_t totals[] = {1u};
    uint64_t run_id = next_run_id();
    pid_t child;
    int status;

    minios_process_region_init(&owner);
    check(create_region(&owner, run_id, actors, totals, 1u) ==
              MINIOS_PROCESS_OK,
          "create paired model and progress regions");
    if (owner.model_mapping == NULL) {
        return;
    }
    check(minios_process_model_view(&owner, &view) == MINIOS_PROCESS_OK &&
              view.width == 64u && view.height == 64u &&
              view.element_count == 4096u && view.model_version == 1u,
          "model view exposes versioned metadata");
    check(view.weights[0] == -50 && view.weights[1] == -19,
          "worker-visible shared model contains deterministic sentinel values");
    check(minios_process_model_read(&owner,
              (view.element_count - 1u) * sizeof(int32_t),
              sizeof(int32_t), &last_value) == MINIOS_PROCESS_OK &&
              *(const int32_t *)last_value ==
                  view.weights[view.element_count - 1u],
          "offset API permits the final complete model element");
    check(minios_process_model_read(&owner, view.byte_length,
              1u, &invalid) == MINIOS_PROCESS_BAD_RANGE && invalid == NULL,
          "offset API rejects a byte beyond the model");
    check(minios_process_model_read(&owner, SIZE_MAX, 2u, &invalid) ==
              MINIOS_PROCESS_BAD_RANGE && invalid == NULL,
          "offset API rejects overflowing ranges");
    check(minios_process_model_read(&owner, view.byte_length, 0u,
                                    &invalid) == MINIOS_PROCESS_OK &&
              invalid == (const unsigned char *)view.weights +
                         view.byte_length,
          "zero-length view at the one-past-end boundary is valid");
    check(minios_process_region_detach(&owner) ==
              MINIOS_PROCESS_BAD_ARGUMENT,
          "region owner must destroy rather than detach and orphan its names");
    {
        MiniOSProcessRegion uninitialized;
        MiniOSProcessModelView invalid_view;
        minios_process_region_init(&uninitialized);
        check(minios_process_model_view(&uninitialized, &invalid_view) ==
                  MINIOS_PROCESS_BAD_ARGUMENT,
              "uninitialized region handles are rejected");
    }

    child = fork();
    if (child == 0) {
        struct rlimit no_core = {0u, 0u};
        MiniOSProcessRegion attached;
        MiniOSProcessModelView child_view;
        (void)setrlimit(RLIMIT_CORE, &no_core);
        minios_process_region_init(&attached);
        if (minios_process_region_attach(
                &attached,
                minios_process_region_model_name(&owner),
                minios_process_region_progress_name(&owner)) !=
                MINIOS_PROCESS_OK ||
            minios_process_model_view(&attached, &child_view) !=
                MINIOS_PROCESS_OK) {
            _exit(2);
        }
        *(volatile int32_t *)(uintptr_t)child_view.weights = 7;
        _exit(0);
    }
    check(child > 0 && wait_for_child(child, &status),
          "read-only model protection probe exits");
    if (child > 0 && WIFSIGNALED(status)) {
        check(WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS,
              "worker cannot write the read-only model mapping");
    } else {
        check(0, "worker cannot write the read-only model mapping");
    }
    check(minios_process_region_destroy(&owner) == MINIOS_PROCESS_OK,
          "owner unlinks and destroys its regions");
    fprintf(stdout, "PASS: model region validation, bounds, and read-only mapping\n");
}

static void test_live_multiworker_publication(const char *executable,
                                              int show_trace)
{
    MiniOSProcessRegion owner;
    MiniOSProcessModelView model;
    MiniOSProcessProgress snapshot;
    MiniOSProcessUpdate update;
    uint32_t actors[] = {1u, 2u};
    uint32_t totals[] = {20u, 20u};
    uint32_t last_actor_sequence[2] = {0u, 0u};
    uint32_t actor_updates[2] = {0u, 0u};
    uint64_t run_id = next_run_id();
    pid_t first;
    pid_t second;
    int first_status = 0;
    int second_status = 0;
    unsigned int index;

    minios_process_region_init(&owner);
    check(create_region(&owner, run_id, actors, totals, 2u) ==
              MINIOS_PROCESS_OK,
          "create shared work region for two independent workers");
    if (owner.progress_mapping == NULL ||
        minios_process_model_view(&owner, &model) != MINIOS_PROCESS_OK) {
        return;
    }
    first = spawn_worker(executable,
                         minios_process_region_model_name(&owner),
                         minios_process_region_progress_name(&owner),
                         1u, 1u, 0u, 2048u, 20u, 20u);
    second = spawn_worker(executable,
                          minios_process_region_model_name(&owner),
                          minios_process_region_progress_name(&owner),
                          2u, 1u, 2048u, 2048u, 20u, 20u);
    check(first > 0 && second > 0,
          "launch both workers as separate exec'd processes");
    if (first <= 0 || second <= 0) {
        if (first > 0) {
            (void)kill(first, SIGTERM);
            (void)wait_for_child(first, &first_status);
        }
        if (second > 0) {
            (void)kill(second, SIGTERM);
            (void)wait_for_child(second, &second_status);
        }
        (void)minios_process_region_destroy(&owner);
        return;
    }

    for (index = 0; index < 40u; ++index) {
        size_t actor_index;
        check(minios_process_wait_update(&owner, &update, 10000u) ==
                  MINIOS_PROCESS_OK,
              "supervisor receives each committed update before timeout");
        if (update.global_sequence != (uint64_t)index + 1u) {
            check(0, "shared update journal has no missing or reordered entries");
            break;
        }
        if (show_trace) {
            fprintf(stdout,
                    "commit=%" PRIu64 " actor=%" PRIu32
                    " generation=%" PRIu32 " sequence=%" PRIu64
                    " progress=%" PRIu32 "/%" PRIu32
                    " result=%016" PRIx64 "\n",
                    update.global_sequence, update.progress.actor_id,
                    update.progress.generation, update.progress.sequence,
                    update.progress.completed, update.progress.total,
                    update.progress.result);
        }
        check(update.progress.actor_id == 1u ||
                  update.progress.actor_id == 2u,
              "update identifies one of the registered workers");
        actor_index = update.progress.actor_id == 1u ? 0u : 1u;
        check(update.progress.sequence ==
                  (uint64_t)last_actor_sequence[actor_index] + 1u,
              "per-worker committed sequence advances exactly once");
        last_actor_sequence[actor_index] =
            (uint32_t)update.progress.sequence;
        ++actor_updates[actor_index];
        check(minios_process_progress_snapshot(
                  &owner, update.progress.actor_id, &snapshot) ==
                  MINIOS_PROCESS_OK &&
                  snapshot.sequence >= update.progress.sequence &&
                  snapshot.completed >= update.progress.completed,
              "supervisor can read committed progress directly from shared memory");
    }

    check(wait_for_child(first, &first_status) && WIFEXITED(first_status) &&
              WEXITSTATUS(first_status) == 0,
          "first worker exits successfully after publishing completion");
    check(wait_for_child(second, &second_status) && WIFEXITED(second_status) &&
              WEXITSTATUS(second_status) == 0,
          "second worker exits successfully after publishing completion");
    check(actor_updates[0] == 20u && actor_updates[1] == 20u,
          "supervisor observed every worker update");
    check(minios_process_progress_snapshot(&owner, 1u, &snapshot) ==
              MINIOS_PROCESS_OK &&
              snapshot.state == MINIOS_PROCESS_STATE_DONE &&
              snapshot.completed == 20u &&
              snapshot.result == expected_hash(&model, 0u, 2048u),
          "first worker's final result is readable in the parent");
    check(minios_process_progress_snapshot(&owner, 2u, &snapshot) ==
              MINIOS_PROCESS_OK &&
              snapshot.state == MINIOS_PROCESS_STATE_DONE &&
              snapshot.completed == 20u &&
              snapshot.result == expected_hash(&model, 2048u, 2048u),
          "second worker's final result is readable in the parent");
    check(minios_process_region_destroy(&owner) == MINIOS_PROCESS_OK,
          "supervisor reaps workers before unlinking the shared regions");
    fprintf(stdout, "PASS: fork/exec workers, live progress, and result publication\n");
}

static void test_backpressure_and_ordered_journal(void)
{
    MiniOSProcessRegion owner;
    MiniOSProcessUpdate update;
    uint32_t actors[] = {3u};
    uint32_t totals[] = {MINIOS_PROCESS_UPDATE_CAPACITY + 1u};
    uint64_t run_id = next_run_id();
    uint32_t sequence;

    minios_process_region_init(&owner);
    check(create_region(&owner, run_id, actors, totals, 1u) ==
              MINIOS_PROCESS_OK,
          "create bounded journal for backpressure test");
    if (owner.progress_mapping == NULL) {
        return;
    }
    for (sequence = 1u; sequence <= MINIOS_PROCESS_UPDATE_CAPACITY;
         ++sequence) {
        check(minios_process_progress_publish(
                  &owner, 3u, 1u, sequence, MINIOS_PROCESS_STATE_RUNNING,
                  sequence, totals[0], sequence, 0u) == MINIOS_PROCESS_OK,
              "journal accepts updates up to its fixed capacity");
    }
    check(minios_process_progress_publish(
              &owner, 3u, 1u, sequence, MINIOS_PROCESS_STATE_DONE,
              sequence, totals[0], sequence, 0u) ==
              MINIOS_PROCESS_BACKPRESSURE,
          "full update journal reports backpressure without overwriting data");
    check(minios_process_wait_update(&owner, &update, 0u) ==
              MINIOS_PROCESS_OK &&
              update.global_sequence == 1u,
          "supervisor drains the oldest committed journal entry first");
    check(minios_process_progress_publish(
              &owner, 3u, 1u, sequence, MINIOS_PROCESS_STATE_DONE,
              sequence, totals[0], sequence, 0u) == MINIOS_PROCESS_OK,
          "worker can publish after supervisor frees a journal slot");
    for (sequence = 2u; sequence <= MINIOS_PROCESS_UPDATE_CAPACITY + 1u;
         ++sequence) {
        check(minios_process_wait_update(&owner, &update, 0u) ==
                  MINIOS_PROCESS_OK &&
                  update.global_sequence == sequence,
              "bounded journal preserves remaining updates in order");
    }
    check(minios_process_region_destroy(&owner) == MINIOS_PROCESS_OK,
          "backpressure test cleans up both named regions");
    fprintf(stdout, "PASS: bounded journal backpressure and ordering\n");
}

static void test_exec_reattachment(const char *executable)
{
    MiniOSProcessRegion owner;
    MiniOSProcessProgress snapshot;
    MiniOSProcessUpdate update;
    uint32_t actors[] = {4u};
    uint32_t totals[] = {4u};
    uint64_t run_id = next_run_id();
    pid_t child;
    int child_status = 0;
    uint32_t index;

    minios_process_region_init(&owner);
    check(create_region(&owner, run_id, actors, totals, 1u) ==
              MINIOS_PROCESS_OK,
          "create run for exec reattachment");
    if (owner.progress_mapping == NULL) {
        return;
    }
    child = spawn_worker(executable,
                         minios_process_region_model_name(&owner),
                         minios_process_region_progress_name(&owner),
                         4u, 1u, 0u, 4096u, 4u, 2u);
    check(child > 0, "launch first worker generation");
    if (child <= 0) {
        (void)minios_process_region_destroy(&owner);
        return;
    }
    for (index = 1u; index <= 2u; ++index) {
        check(minios_process_wait_update(&owner, &update, 10000u) ==
                  MINIOS_PROCESS_OK &&
                  update.progress.completed == index,
              "first generation commits bounded progress before exit");
    }
    check(wait_for_child(child, &child_status) && WIFEXITED(child_status) &&
              WEXITSTATUS(child_status) == 0,
          "first generation exits after its simulated exec boundary");
    check(minios_process_progress_snapshot(&owner, 4u, &snapshot) ==
              MINIOS_PROCESS_OK &&
              snapshot.generation == 1u && snapshot.completed == 2u &&
              snapshot.state == MINIOS_PROCESS_STATE_RUNNING,
          "supervisor retains the last committed progress across worker exit");

    child = spawn_worker(executable,
                         minios_process_region_model_name(&owner),
                         minios_process_region_progress_name(&owner),
                         4u, 2u, 0u, 4096u, 4u, 4u);
    check(child > 0, "launch replacement worker as a new exec'd process");
    if (child <= 0) {
        (void)minios_process_region_destroy(&owner);
        return;
    }
    for (index = 0u; index < 3u; ++index) {
        uint32_t expected_completed = index == 0u ? 2u : index + 2u;
        check(minios_process_wait_update(&owner, &update, 10000u) ==
                  MINIOS_PROCESS_OK &&
                  update.progress.generation == 2u &&
                  update.progress.completed == expected_completed,
              "replacement resumes after the prior committed checkpoint");
    }
    check(wait_for_child(child, &child_status) && WIFEXITED(child_status) &&
              WEXITSTATUS(child_status) == 0,
          "replacement generation completes successfully");
    check(minios_process_progress_snapshot(&owner, 4u, &snapshot) ==
              MINIOS_PROCESS_OK &&
              snapshot.generation == 2u &&
              snapshot.sequence == 5u &&
              snapshot.completed == 4u &&
              snapshot.state == MINIOS_PROCESS_STATE_DONE,
          "final snapshot contains prior and replacement generation progress");
    check(minios_process_progress_publish(
              &owner, 4u, 1u, 6u, MINIOS_PROCESS_STATE_DONE,
              4u, 4u, snapshot.result, 0u) ==
              MINIOS_PROCESS_STALE_GENERATION,
          "late publication from the prior generation is rejected");
    check(minios_process_region_destroy(&owner) == MINIOS_PROCESS_OK,
          "exec reattachment test cleans up supervisor-owned regions");
    fprintf(stdout, "PASS: exec reattachment, checkpoint continuity, stale generation rejection\n");
}

static void test_owner_death(void)
{
    MiniOSProcessRegion owner;
    MiniOSProcessProgress snapshot;
    uint32_t actors[] = {5u};
    uint32_t totals[] = {1u};
    uint64_t run_id = next_run_id();
    pid_t child;
    int child_status = 0;

    minios_process_region_init(&owner);
    check(create_region(&owner, run_id, actors, totals, 1u) ==
              MINIOS_PROCESS_OK,
          "create region for robust-owner-death test");
    if (owner.progress_mapping == NULL) {
        return;
    }
    child = fork();
    if (child == 0) {
        MiniOSProcessRegion attached;
        minios_process_region_init(&attached);
        if (minios_process_region_attach(
                &attached,
                minios_process_region_model_name(&owner),
                minios_process_region_progress_name(&owner)) !=
                MINIOS_PROCESS_OK) {
            _exit(2);
        }
        minios_process_region_test_abandon_control_lock(&attached);
    }
    check(child > 0 && wait_for_child(child, &child_status) &&
              WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0,
          "writer exits while holding the process-shared robust mutex");
    check(minios_process_progress_snapshot(&owner, 5u, &snapshot) ==
              MINIOS_PROCESS_OWNER_DIED,
          "reader fails explicitly rather than consuming state after owner death");
    check(minios_process_region_destroy(&owner) == MINIOS_PROCESS_OK,
          "owner-death test still permits supervisor cleanup");
    fprintf(stdout, "PASS: robust owner-death detection and explicit failure\n");
}

static void test_attach_validation(void)
{
    char truncated_name[MINIOS_PROCESS_REGION_NAME_MAX];
    char corrupt_name[MINIOS_PROCESS_REGION_NAME_MAX];
    char missing_progress[MINIOS_PROCESS_REGION_NAME_MAX];
    MiniOSProcessRegion region;
    uint64_t run_id = next_run_id();
    int fd;

    (void)snprintf(truncated_name, sizeof(truncated_name),
                   "/minios-short-%" PRIu64, run_id);
    (void)snprintf(corrupt_name, sizeof(corrupt_name),
                   "/minios-corrupt-%" PRIu64, run_id);
    (void)snprintf(missing_progress, sizeof(missing_progress),
                   "/minios-missing-%" PRIu64, run_id);

    fd = shm_open(truncated_name, O_CREAT | O_EXCL | O_RDWR,
                  S_IRUSR | S_IWUSR);
    check(fd >= 0, "create deliberately truncated shared object");
    if (fd >= 0) {
        check(ftruncate(fd, 1) == 0,
              "truncate malformed object to a short header");
        (void)close(fd);
        minios_process_region_init(&region);
        check(minios_process_region_attach(
                  &region, truncated_name, missing_progress) ==
                  MINIOS_PROCESS_TRUNCATED,
              "truncated object is rejected before full-header access");
        (void)shm_unlink(truncated_name);
    }

    fd = shm_open(corrupt_name, O_CREAT | O_EXCL | O_RDWR,
                  S_IRUSR | S_IWUSR);
    check(fd >= 0, "create deliberately invalid ABI object");
    if (fd >= 0) {
        unsigned char invalid_header[4096] = {0};
        check(ftruncate(fd, (off_t)sizeof(invalid_header)) == 0,
              "size invalid object for ABI test");
        check(write(fd, invalid_header, sizeof(invalid_header)) ==
                  (ssize_t)sizeof(invalid_header),
              "write invalid object header");
        (void)close(fd);
        minios_process_region_init(&region);
        check(minios_process_region_attach(
                  &region, corrupt_name, missing_progress) ==
                  MINIOS_PROCESS_BAD_ABI,
              "invalid magic/version is rejected before mapping payload");
        (void)shm_unlink(corrupt_name);
    }

    minios_process_region_init(&region);
    check(minios_process_region_attach(
              &region, missing_progress, corrupt_name) ==
              MINIOS_PROCESS_NOT_FOUND,
          "missing named region returns an explicit attach error");
    fprintf(stdout, "PASS: truncated, incompatible, and missing region rejection\n");
}

static int run_parent_suite(const char *executable)
{
    test_attach_validation();
    test_model_region_and_permissions();
    test_backpressure_and_ordered_journal();
    test_live_multiworker_publication(executable, 0);
    test_exec_reattachment(executable);
    test_owner_death();
    fprintf(stdout, "Process-shared region tests: %u checks, %u failures\n",
            checks, failures);
    return failures == 0u ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--demo") == 0) {
        test_live_multiworker_publication(argv[0], 1);
        fprintf(stdout, "Shared-memory worker demo: %u checks, %u failures\n",
                checks, failures);
        return failures == 0u ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc == 10 && strcmp(argv[1], "--worker") == 0) {
        uint32_t actor;
        uint32_t generation;
        uint32_t total;
        uint32_t stop_after;
        char *begin_end = NULL;
        char *count_end = NULL;
        unsigned long long begin;
        unsigned long long count;
        if (!parse_u32(argv[4], &actor) ||
            !parse_u32(argv[5], &generation) ||
            !parse_u32(argv[8], &total) ||
            !parse_u32(argv[9], &stop_after)) {
            return 2;
        }
        errno = 0;
        begin = strtoull(argv[6], &begin_end, 10);
        if (errno != 0 || begin_end == argv[6] || *begin_end != '\0' ||
            begin > SIZE_MAX) {
            return 2;
        }
        errno = 0;
        count = strtoull(argv[7], &count_end, 10);
        if (errno != 0 || count_end == argv[7] || *count_end != '\0' ||
            count > SIZE_MAX) {
            return 2;
        }
        return worker_run(argv[2], argv[3], actor, generation,
                          (size_t)begin, (size_t)count, total, stop_after);
    }
    if (argc == 4 && strcmp(argv[1], "--abandon-lock") == 0) {
        MiniOSProcessRegion region;
        minios_process_region_init(&region);
        if (minios_process_region_attach(&region, argv[2], argv[3]) !=
            MINIOS_PROCESS_OK) {
            return 3;
        }
        minios_process_region_test_abandon_control_lock(&region);
    }
    if (argc == 4 && strcmp(argv[1], "--write-model") == 0) {
        struct rlimit no_core = {0u, 0u};
        MiniOSProcessRegion region;
        MiniOSProcessModelView model;
        (void)setrlimit(RLIMIT_CORE, &no_core);
        minios_process_region_init(&region);
        if (minios_process_region_attach(&region, argv[2], argv[3]) !=
                MINIOS_PROCESS_OK ||
            minios_process_model_view(&region, &model) != MINIOS_PROCESS_OK) {
            return 4;
        }
        *(volatile int32_t *)(uintptr_t)model.weights = 9;
        return 5;
    }
    return run_parent_suite(argv[0]);
}
