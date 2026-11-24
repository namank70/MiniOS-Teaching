# Shared progress and results between kernel tasks

Two scheduled workers now read the existing shared model and publish progress,
partial sums, and final results. The shell acts as their supervisor: it can read
the latest snapshots or consume committed updates in order. This runs inside
the booted teaching kernel. All six tasks share one ring-0 address space; these
are **kernel tasks, not separate processes**.

## Demo for the professor

From the repository root, with Docker running:

```sh
./tools/teaching.sh run-teaching
```

At the `minios>` prompt:

```text
tasks
work
progress
```

`work` starts a new run and enables observation. Worker A (actor 5, scheduler
task 3) sums rows 0–19 of the existing 256×256 model. Worker B (actor 6, task 4)
sums rows 20–39. One row is one unit of work; each worker publishes 20 updates.
The model is `W[r,c] = ((17*r + 31*c) % 101) - 50`. The result field contains a
signed 64-bit running sum, with a final sum on `DONE`. No SNN inference is being
performed by these workers.

The first run ends with:

```text
[work] commit=39 run=1 actor=5 task=3 seq=20 completed=20/20 state=DONE result=-37 error=0
[work] commit=40 run=1 actor=6 task=4 seq=20 completed=20/20 state=DONE result=28 error=0
WORK DONE run=1 events=40 result_a=-37 result_b=28 PASS
```

`progress` retains both final snapshots even after the events have been read.
Run `work` again: the run ID increases, actor sequences restart at 1, and the
global commit sequence continues from 41. Results and actor ordering repeat.
The workers deliberately take turns using shared checkpoints (A1, B1, A2,
B2, …). That rule belongs to this demonstration, not to the publication API.
Elapsed time, heartbeat values, dispatch counts, and retry counts can vary.

To demonstrate a slow observer, wait for the previous run to finish, then type:

```text
work-pause
```

After roughly a second of kernel time, type:

```text
progress
status
work
updates
watch
```

- `progress` shows `pending=32/32`, a positive `full_retries`, and both workers
  at `16/20`. Their next update is waiting locally for space. Poll `progress`
  again if the queue has not filled yet; host emulation speed varies.
- `status` still works. Sleeping workers do not monopolize the CPU.
- `work` reports `Work start: BUSY`; it cannot discard an active run or unread
  results by starting over.
- `updates` consumes at most 32 committed events, freeing those slots. It does
  not resume automatic observation. Workers can now publish more updates.
- `watch` resumes automatic consumption, prints the remaining updates, and
  reports the same final results. Nothing was overwritten.

`help` lists the added commands. The original recommendation, shell commands,
runtime heartbeat, serial input, PS/2 input, and scheduler checks remain. Exit
QEMU with **Ctrl-a, then x**.

## API and ownership contract

This is an additive interface. `include/minios/runtime.h`, the legacy heartbeat
API, and Naman's host-prototype API are unchanged. The new contract is available
for review without changing either existing record layout. It is a teaching
API, not a frozen cross-platform binary ABI.

| Location | Responsibility |
|---|---|
| `include/minios/task_updates.h` | Value records, statuses, fixed storage, portable core API |
| `src/shared/task_updates.c` | Registration, validation, snapshots, ordered bounded history |
| `include/minios/kernel_work.h`, `src/platform/kernel/task_updates.c` | Static kernel store, IRQ guards, scheduler identity and supervisor ownership |
| `kernel/teaching/shared_work.c` | Two scheduled workers, row sums, shell commands and observation |
| `tests/unit/task_updates_test.c` | Host tests of the same core linked into the kernel |
| `tests/qemu/shell_smoke.py` | Real ISO demo, backpressure and shell regression tests |

The kernel adapter is initialized once before scheduling starts. Only shell
task 0 may begin a run or remove events. Up to eight actors can be registered
in the portable core, with unique nonzero actor IDs and unique task IDs. The
current kernel has only four eligible non-shell, non-idle task slots; the demo
registers two. Registration is fixed for a run, and each total must be positive.

Each `MiniOSWorkRecord` contains:

| Field | Meaning |
|---|---|
| `version` | Contract version, currently 1 |
| `actor_id`, `task_id` | Logical worker and its registered scheduler task |
| `run_id` | Positive 64-bit run identity, strictly greater than the previous run |
| `sequence` | Per-actor publication number, exactly previous + 1 |
| `completed`, `total` | Monotonic committed work and fixed positive target |
| `state` | `READY`, `RUNNING`, `DONE`, or `FAILED` |
| `result` | Signed 64-bit partial or final result |
| `error_code` | Zero for normal work; positive application-defined failure code |

Records and history contain values only, with no task-local pointers or strings.
The worker's model view and pending record stay local to its stack. A history
entry copies the whole record and adds a boot-lifetime `commit_sequence`.
Callers must not treat native C struct bytes/padding as a portable wire format.

`minios_kernel_work_publish` derives the calling task ID from the scheduler;
submitting someone else's actor/task fails. It checks version, identity, run,
sequence, fixed total, monotonic work, and state before committing anything.
`RUNNING` requires completed < total; `DONE` requires completed == total.
`FAILED` requires a positive error code and may retain partial work/results.
The demo uses error 1 for an invalid model and error 2 for peer failure.
`READY` is created only by registration and cannot be published. Both `DONE`
and `FAILED` are terminal within a run. Equal completed counts are permitted
for additional reports, but every committed publication advances the sequence.
The API validates structure and progression, not whether a computation's result
is mathematically correct; the supervisor and QEMU test check the demo's sums.

Begin succeeds only when the previous actors are terminal and the history is
empty. Otherwise it returns `BUSY`. There is no force-reset/discard command.
New runs reset actor sequences/checkpoints; old-run publications return
`BAD_RUN`. There is no individual worker generation or restart protocol.
Sequence/commit counters reject overflow with `EXHAUSTED`; run IDs never wrap.

## Commit, observation, and full history

The adapter disables interrupts briefly for each complete operation and restores
the previous interrupt flags. On this single CPU, another task cannot observe
half of a publication: the latest snapshot and its event become visible together.
Computing sums, printing, sleeping, and retrying happen outside that guard.
The portable core itself does not lock; host callers must serialize access too.

History is a fixed 32-slot FIFO with one destructive consumer. Each successful
`take` copies the oldest committed event and frees its slot. Empty `take` returns
`EMPTY` without changing the output. Snapshots do not consume history. All failed
operations leave record/event outputs and committed progress unchanged.

When all slots are unread, a valid publication returns `FULL` immediately.
Neither its snapshot nor either sequence advances. Only the diagnostic
`full_attempts` counter increments, saturating at `UINT32_MAX`. Invalid input is
rejected before capacity checking and does not increment this counter. The
worker retains the exact pending record, sleeps 20 ticks, and retries; it must
not skip to the next unit of work. No unread entry is overwritten, even when
the pending update is the final result.

Only the shell writes ordinary console output. It consumes at most 32 events
per observation slice, prints them, and restores the prompt and partially typed
input. A completed run is reported once, after both workers are terminal and
all events have been consumed. The shell computes a reference sum separately
from the workers before reporting `PASS`.

## Differences from the Linux prototype

Reference: [process-shared-regions.md at ad023cd](https://github.com/namank70/MiniOS-Teaching/blob/ad023cd/docs/teaching-os/process-shared-regions.md)
on `feature/shared-region-workers`. That branch was inspected, not merged or
modified. No POSIX implementation was copied into the kernel.

| Linux host prototype | Kernel implementation |
|---|---|
| Named POSIX mappings, fork/exec, reattachment | Existing static model and static progress store, scheduled ring-0 tasks |
| Robust process mutex and semaphores | Short interrupt guards on one CPU |
| Nonblocking backpressure or timed wait | Immediate `FULL`; worker sleeps and retries outside the API |
| Worker generation/restart handshake within a run | New run IDs only; no individual worker restart |
| 64×64 model | Existing 256×256 teaching model; two 20-row partitions |
| Unsigned result field | Signed result to preserve negative model sums |
| Independently scheduled process commit order | API accepts any valid commit order; demo explicitly alternates workers |
| Robust owner-death detection | No recovery from a crashing kernel task |

Both designs retain complete value records, per-actor sequences, global commit
order, snapshots, and a 32-entry history that does not overwrite unread data.
They deliberately have separate APIs and layouts. A later common semantic
interface can be coordinated without importing POSIX synchronization objects.

## Tests and observed results

Exact commands from the repository root:

```sh
# New portable API tests only
./tools/teaching.sh test-work-updates
# All existing host suites plus actual ISO build and QEMU demonstration
./tools/teaching.sh test-teaching
# Interactive demo
./tools/teaching.sh run-teaching
```

Native Linux/WSL with the documented toolchain can use `make test-work-updates`,
`make test-teaching`, and `make run-teaching` instead. Docker provides build/test
tools; QEMU boots the resulting MiniOS ISO.

Validation passed with the repository's Linux/amd64 Docker toolchain and QEMU
TCG, including:

- Five new host test groups: commit order and copied events; invalid inputs and
  atomic rejection; actor bounds; full-history retry and wraparound; terminal
  results, failure states, run lifecycle, stale updates and counter overflow.
- Existing mock provider (20 assertions), suggestion policy (20), scenarios
  (5 checks), dispatcher (14), scheduler selection and region bounds tests.
- Nine QEMU checks, including two identical 40-event worker runs and one
  paused-observer run that fills all 32 slots. QEMU independently computes every
  row's expected partial sum and verifies every event, run ID, actor/task ID,
  sequence, final result, snapshot, and continued scheduling.

QEMU saves `build/teaching/shell-smoke.log` and `build/teaching/qemu-stderr.log`.
Expected final results are A = -37, B = 28, with 40 events per successful run.
The existing CI `test-teaching` gate includes these new tests automatically.

An additional native macOS Clang run passed all five host groups with address
and undefined-behavior sanitizers and no sanitizer diagnostics:

```sh
mkdir -p build/teaching
clang -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
  tests/unit/task_updates_test.c src/shared/task_updates.c \
  -o build/teaching/work-updates-sanitized
./build/teaching/work-updates-sanitized
```

## Limits

- Single CPU and trusted task-context callers only. IRQ masking is not an SMP
  lock, memory isolation, or a defense against malicious ring-0 code. The const
  model view does not enforce read-only page protection.
- Static capacity and fixed task slots; no allocation, POSIX shared memory,
  mmap, fork, exec, external programs, separate processes, or new GUI.
- One consumer; consuming an event is not a durable output acknowledgment.
  No persistence, replay after consumption, consumer restart, cancellation,
  timeout, or crash recovery. If observation stops permanently, full-history
  workers remain asleep/retrying; they cannot finish until space is freed.
- Fixed-priority scheduling remains unchanged. Workers sleep between rows;
  arbitrary tasks that never block can still starve lower-priority tasks.
- This demo computes mock matrix sums. It does not replace the recommendation
  policy, integrate real SNN inference, or claim the host process prototype
  has been ported. Existing research sources and build targets are retained.
