#ifndef MINIOS_PROCESS_REGION_H
#define MINIOS_PROCESS_REGION_H

#include <stddef.h>
#include <stdint.h>

#define MINIOS_PROCESS_REGION_NAME_MAX 128u
#define MINIOS_PROCESS_REGION_MAX_ACTORS 8u
#define MINIOS_PROCESS_MODEL_MAX_ELEMENTS 1048576u
#define MINIOS_PROCESS_UPDATE_CAPACITY 32u

typedef enum {
    MINIOS_PROCESS_OK = 0,
    MINIOS_PROCESS_BAD_ARGUMENT,
    MINIOS_PROCESS_NOT_FOUND,
    MINIOS_PROCESS_IO_ERROR,
    MINIOS_PROCESS_NO_MEMORY,
    MINIOS_PROCESS_TRUNCATED,
    MINIOS_PROCESS_BAD_ABI,
    MINIOS_PROCESS_BAD_RANGE,
    MINIOS_PROCESS_BAD_PROGRESS,
    MINIOS_PROCESS_STALE_GENERATION,
    MINIOS_PROCESS_BAD_SEQUENCE,
    MINIOS_PROCESS_TIMEOUT,
    MINIOS_PROCESS_BACKPRESSURE,
    MINIOS_PROCESS_OWNER_DIED,
    MINIOS_PROCESS_UPDATE_CORRUPT
} MiniOSProcessStatus;

typedef enum {
    MINIOS_PROCESS_STATE_EMPTY = 0,
    MINIOS_PROCESS_STATE_RUNNING,
    MINIOS_PROCESS_STATE_DONE,
    MINIOS_PROCESS_STATE_FAILED
} MiniOSProcessState;

/* Process-local mapping handle; never serialize or place it in shared memory. */
typedef struct {
    int model_fd;
    int progress_fd;
    void *model_mapping;
    void *progress_mapping;
    size_t model_mapping_size;
    size_t progress_mapping_size;
    int owns_regions;
    char model_name[MINIOS_PROCESS_REGION_NAME_MAX];
    char progress_name[MINIOS_PROCESS_REGION_NAME_MAX];
} MiniOSProcessRegion;

typedef struct {
    const char *model_name;
    const char *progress_name;
    uint64_t run_id;
    uint32_t model_version;
    uint32_t width;
    uint32_t height;
    const uint32_t *actor_ids;
    const uint32_t *actor_totals;
    size_t actor_count;
} MiniOSProcessRegionSpec;

typedef struct {
    uint32_t model_version;
    uint32_t width;
    uint32_t height;
    size_t element_count;
    size_t byte_length;
    const int32_t *weights;
} MiniOSProcessModelView;

typedef struct {
    uint32_t actor_id;
    uint32_t generation;
    MiniOSProcessState state;
    uint32_t completed;
    uint32_t total;
    uint64_t sequence;
    uint64_t result;
} MiniOSProcessProgress;

typedef struct {
    uint64_t global_sequence;
    MiniOSProcessProgress progress;
} MiniOSProcessUpdate;

void minios_process_region_init(MiniOSProcessRegion *region);

MiniOSProcessStatus minios_process_region_create(
    MiniOSProcessRegion *region,
    const MiniOSProcessRegionSpec *spec);
MiniOSProcessStatus minios_process_region_attach(
    MiniOSProcessRegion *region,
    const char *model_name,
    const char *progress_name);
MiniOSProcessStatus minios_process_region_detach(
    MiniOSProcessRegion *region);
MiniOSProcessStatus minios_process_region_destroy(
    MiniOSProcessRegion *region);

const char *minios_process_region_model_name(
    const MiniOSProcessRegion *region);
const char *minios_process_region_progress_name(
    const MiniOSProcessRegion *region);

MiniOSProcessStatus minios_process_model_view(
    const MiniOSProcessRegion *region,
    MiniOSProcessModelView *out);
MiniOSProcessStatus minios_process_model_read(
    const MiniOSProcessRegion *region,
    size_t offset,
    size_t length,
    const void **out);

MiniOSProcessStatus minios_process_progress_snapshot(
    MiniOSProcessRegion *region,
    uint32_t actor_id,
    MiniOSProcessProgress *out);
MiniOSProcessStatus minios_process_progress_publish(
    MiniOSProcessRegion *region,
    uint32_t actor_id,
    uint32_t generation,
    uint64_t sequence,
    MiniOSProcessState state,
    uint32_t completed,
    uint32_t total,
    uint64_t result,
    uint32_t timeout_ms);
/* One supervisor consumes the bounded journal in global commit order. */
MiniOSProcessStatus minios_process_wait_update(
    MiniOSProcessRegion *region,
    MiniOSProcessUpdate *out,
    uint32_t timeout_ms);

#endif
