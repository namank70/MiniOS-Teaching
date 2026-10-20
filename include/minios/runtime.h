#ifndef MINIOS_RUNTIME_H
#define MINIOS_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
#include "minios/mock_provider.h"
#define MINIOS_REGION_MODEL 1u
#define MINIOS_RUNTIME_VERSION 1u
#define MINIOS_PROGRESS_ACTORS 4u
typedef enum { MINIOS_RUNTIME_OK, MINIOS_RUNTIME_BAD_ARGUMENT, MINIOS_RUNTIME_BAD_RANGE } MiniOSRuntimeStatus;
typedef struct {
    uint32_t version, actor_id, completed, total;
    uint64_t heartbeat_ms;
} MiniOSProgress;
/* Kernel backend: bounded static storage in one address space, no mmap/exec.
 * Views are process-local const pointers; do not serialize them. */
void minios_runtime_init(void);
MiniOSRuntimeStatus minios_region_view(uint32_t region, size_t offset, size_t length, const void **out);
MiniOSRuntimeStatus minios_runtime_model(MiniOSModelView *out);
MiniOSRuntimeStatus minios_progress_publish(uint32_t actor, uint32_t completed, uint32_t total);
MiniOSRuntimeStatus minios_progress_snapshot(uint32_t actor, MiniOSProgress *out);
int minios_region_bounds(size_t size, size_t offset, size_t length);
#endif
