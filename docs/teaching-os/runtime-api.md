# Region/progress contract for Naman

Implemented header: `include/minios/runtime.h`. Kernel implementation: `src/platform/kernel/runtime.c`. Version 1 describes the current static, single-address-space backend, leaving a future POSIX backend separate.

## Initialization and model

`minios_runtime_init()` runs once during boot, before any task starts. It initializes a 256×256 int32 mock matrix with `W[r,c] = ((17*r + 31*c) % 101) - 50` (262,144 bytes) and clears progress records. Do not reset it while tasks are active.

`minios_runtime_model(MiniOSModelView *out)` returns a view compatible with Naman's existing provider. The data has static lifetime and is not modified after startup. Returned const pointers are local views and must not be serialized. They are not an MMU-enforced permission boundary between ring-0 tasks.

`minios_region_view(region, offset, length, &out)` accepts `MINIOS_REGION_MODEL=1`. It validates offsets using `length <= size - offset` after checking `offset <= size`, avoiding addition overflow. Zero-length one-past-end views are valid but cannot be dereferenced. Invalid region/null output returns `BAD_ARGUMENT`; invalid bounds returns `BAD_RANGE` and clears the output pointer. Callers requesting typed data must use a suitably aligned offset and length; the public view is a byte span.

## Progress

`minios_progress_publish(actor, completed, total)` atomically publishes a complete snapshot and records current kernel uptime. It rejects unknown IDs and `completed > total`. `minios_progress_snapshot(actor, &out)` returns a consistent copy. Both preserve prior interrupt state, including calls made with interrupts already disabled. They are task-context APIs, not multi-CPU primitives.

| Actor ID | Owner | Current measure |
|---|---|---|
| 1 | shell task (scheduler ID 0) | input-loop iterations and last heartbeat |
| 2 | runtime task (scheduler ID 2) | periodic runtime iterations and heartbeat |
| 3 | recommendation task (scheduler ID 1) | evaluation-loop iterations and heartbeat |
| 4 | reserved idle actor | zero/unpublished; inspect `tasks` for idle activity |

Current non-idle totals are `UINT32_MAX`; these are activity counters rather than a percentage toward completing an infinite service. Actor IDs are independent of task indices. `MiniOSProgress` has ABI version, actor ID, completed, total, and 64-bit heartbeat time. The shell's compact diagnostic prints the low 32 bits of heartbeat time; the API retains all 64 bits.

## Event ownership and retry

Naman's policy stays owned by the recommendation task. It posts a copy of each ready event into a bounded eight-entry kernel queue under a short interrupt guard. Only a successful enqueue calls `minios_policy_commit`. On a full queue the policy retains its pending event; later evaluations return it until space is available. The shell task alone drains the queue and renders output. This small queue is a new kernel adapter, not the historical priority-bus harness.

## Future changes

The shell's `suggest` command remains Naman's fixed fixture; it does not silently change to live state. The scheduled path already uses the shared region model. Commands can read `minios_runtime_model`, `minios_progress_snapshot`, and `scheduler_snapshot` without writing platform globals. Any future live-context or richer progress integration should preserve these ownership boundaries and agree an ABI change first.

No claim is made that mappings survive exec: there is no exec in this kernel. Automatic host-process reattachment is deferred by the agreed shell-first scope.
