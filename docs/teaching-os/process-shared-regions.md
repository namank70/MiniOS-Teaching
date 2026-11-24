# Process-shared model and progress regions

## Purpose and status

This Linux host prototype is the next step beyond the booted teaching kernel's
static region. It demonstrates the professor's proposed direction: workers
communicate committed progress and results through common memory, while a
supervisor reads them without a per-update pipe or socket message.

The prototype is deliberately separate from the kernel runtime. It is not yet
the production process supervisor, a general allocator, or an OS-level MiniOS
feature. It is an executable design slice for testing the shared-memory ABI,
worker publication contract, reattachment model, and teaching exercises before
committing to a larger architecture.

## Architecture

The supervisor creates two named POSIX shared-memory objects for one run:

1. **Model region:** a versioned, deterministic 64×64 `int32_t` matrix. It is
   finalized before workers start and attached with a read-only mapping.
2. **Progress region:** a versioned control area with actor records, a
   process-shared robust mutex, and a bounded update journal. Workers attach
   writable progress state; the supervisor reads snapshots and ordered update
   records.

Workers receive only the two region names and small role/range metadata across
`exec`. Each process calls the runtime attach function and receives its own
local mappings. Shared records contain IDs, versions, counters, result values,
and offsets—not process-local pointers. Model offsets are checked as
`offset <= size` and `length <= size - offset`.

For each publication, a worker:

1. Reserves a journal slot (or waits up to the supplied timeout).
2. Takes the process-shared robust mutex.
3. Validates actor ID, generation, sequence, state, and monotonic progress.
4. Writes the complete progress record and an immutable journal entry.
5. Unlocks and signals the supervisor.

The supervisor waits on a process-shared semaphore, consumes journal entries
in global commit order, and can independently snapshot the latest actor record.
The journal is bounded to 32 entries. A nonblocking publish to a full journal
returns `MINIOS_PROCESS_BACKPRESSURE`; a timed publish waits for a slot and
returns `MINIOS_PROCESS_TIMEOUT` if its deadline expires. It never overwrites
unconsumed updates. Reading an entry frees a slot.

## Reattachment and generation contract

The integration test starts workers with `fork` + `exec`; the replacement
program does not rely on inherited mapping addresses. It explicitly reattaches
using the named regions. A worker restart increments its generation and first
publishes a reattachment handshake at the last committed progress value. It
then continues work, keeping the per-actor sequence monotonic.

The runtime rejects:

- unknown actors and invalid progress (`completed > total`);
- sequence gaps, repeats, or stale sequence numbers;
- skipped or stale worker generations;
- a generation change that resets committed progress;
- completion before all assigned work is committed.

These are prototype rules, not a frozen course ABI. The professor and team
should review the record layout, generation policy, journal size, and error
semantics before students depend on them.

## Build and run

On Linux or WSL with GCC, Make, POSIX shared memory, pthreads, and process-shared
semaphores:

```sh
make test-process-regions
```

For a visible two-worker progress trace:

```sh
make demo-process-regions
```

The demo prints each committed event's global sequence, actor, worker
generation, progress count, and deterministic partial result, followed by
parent-side completion checks.

The target compiles with `-Wall -Wextra -Werror -pedantic`, creates temporary
test binaries, starts real `exec` workers, and cleans up its temporary build
directory. It is also included in `make test-person-b` and therefore in the
integrated `make test-teaching` gate.

The integration executable exercises:

- deterministic model initialization and final-element/overflow-safe offset
  boundaries;
- actual read-only protection on worker model mappings;
- rejection of missing, truncated, and incompatible region objects;
- two independent `exec` workers publishing 40 progress/result updates while
  the supervisor reads both the journal and shared snapshots;
- bounded-journal ordering and explicit backpressure rather than silent
  overwrite;
- worker generation 1 committing 2/4, followed by generation 2 reattaching,
  reporting the preserved 2/4 checkpoint, and completing 4/4;
- rejection of a late write from the previous generation; and
- robust-mutex owner death returning an explicit failure instead of accepting
  a possibly partial record.

The integration test prints per-case `PASS` lines and a final number of checks.
Passing this host target does not by itself prove the kernel scheduler,
keyboard, or QEMU path; those remain covered by `make test-teaching`.

The shared control record contains native `pthread_mutex_t` and `sem_t`
objects. All participants must use a compatible Linux architecture, libc,
compiler ABI, and runtime version. The version/size header detects expected
layout mismatches; it does not make these opaque synchronization objects
portable across operating systems or libc implementations. The creator must
call `destroy` only after workers are stopped/reaped; attached workers call
`detach`. The single supervisor is the only supported journal consumer.

## Worksheet opportunities

The framework provides visible and testable student extensions rather than
only low-level memory exercises:

- Inspect the model region and safely read a selected row/column.
- Partition matrix work among workers and display each worker's progress.
- Add a reduction (for example, partial sums or deterministic threshold
  counts) and compare the supervisor's final result with a single-process
  reference.
- Implement bounded-journal policies such as backpressure, batching, or
  acknowledged snapshots, then measure how they change update visibility.
- Add supervisor restart/recovery behavior and show which committed results
  survive a worker restart.
- Add a shell command that displays model metadata, current worker generations,
  progress, and the last committed results.

The draft sequence with prerequisites and three test cases per exercise is in
[`shared-memory-labs.md`](shared-memory-labs.md). Each worksheet should have a
starter snapshot, a normal case, a boundary case, a failure/recovery case, and
deterministic expected output. Host exercises can be assigned before
attempting kernel process isolation.

## Current limitations and deliberate scope

- Linux is the supported first host. POSIX semaphore and robust process-shared
  mutex support varies across systems; macOS/other Unix support is not claimed.
- The supervisor explicitly creates, attaches, waits, reaps, and destroys the
  run. This is not yet a general-purpose automatic launcher.
- The prototype passes POSIX shared-memory names across `exec`; descriptor
  passing and automatic runtime startup are possible later alternatives.
- One supervisor consumes the ordered update journal. Multi-supervisor
  consumption, dynamic actor registration, journal resizing, and persistence
  are not implemented.
- The model is fixed mock data, and the result is a deterministic demonstration
  value, not SNN inference or a live sensor recommendation.
- The kernel backend remains separate static memory in one ring-0 address
  space. This host prototype does not add process isolation or `mmap`/`exec` to
  the booted kernel.
- Robust owner death is treated as run failure. Automatic repair of a
  potentially half-written update is intentionally not attempted.

This is a substantial prototype and a basis for review—not a claim that the
final memory architecture is settled. Follow-up design or implementation may
change the ABI, synchronization primitive, persistence model, worker protocol,
or platform support as requirements become clearer.
