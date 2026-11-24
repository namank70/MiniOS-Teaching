# MiniOS Teaching — shared-memory progress report

**Updated:** November 4, 2026
**Work branch:** `feature/shared-region-workers`
**Scope:** Coordinated host-process prototype and kernel-task shared-progress work

## Current combined progress

PR #1, which integrated the proactive engine and bootable teaching-kernel
shell/scheduler work, has been merged into `main` as merge commit `23f4071`.
The shared-memory work now has two complementary implementations with
different boundaries:

- **Person B — host proactive engine and process prototype:** the deterministic
  mock provider, suggestion policy, scenario runner, and platform-neutral
  dispatcher form the host-side engine and its test foundation. On the separate
  `feature/shared-region-workers` branch, commit `ad023cd` adds a Linux
  process-shared-memory prototype with real `fork`/`exec` workers, an immutable
  model region, a writable progress/result region, ordered bounded updates,
  backpressure, and checkpoint reattachment. Its detailed design and test
  results are below.
- **Person A — bootable kernel integration and kernel-task progress:** the
  merged PR #1 brought the engine into a source-built teaching kernel with a
  shell, scheduler/runtime integration, and static shared model/progress API.
  The new PR #2, `codex/kernel-shared-progress` into `main`, adds structured
  per-worker progress/results, a bounded ordered update history, and two
  scheduled kernel workers that compute and publish row sums for the shell to
  observe. It includes `work`, `work-pause`, `updates`, and `watch` commands,
  host tests, QEMU checks, and kernel-specific documentation.

The kernel implementation is **shared state between scheduled kernel tasks in
one ring-0 address space**; it is not separate-process or POSIX shared memory.
The host prototype demonstrates the separate-process design on Linux, but is
not yet integrated into the booted kernel. These implementations are related
experiments, not a single merged runtime or API.

### Branch and review status

| Work | Branch / destination | Status |
|---|---|---|
| Bootable shell, scheduler, and initial kernel runtime integration | PR #1 into `main` | Merged |
| Kernel worker progress/results | PR #2: `codex/kernel-shared-progress` into `main` | Open; GitHub `build-and-boot` check passed |
| Linux POSIX process-shared-memory prototype | `feature/shared-region-workers` | Pushed at `ad023cd`; no PR opened |

PR #2 starts from the updated `main`. I fetched its branch for inspection only;
I did not merge or cherry-pick it into the host-prototype branch. GitHub lists
one successful `build-and-boot` check for PR #2. Its PR description reports
passing host tests, QEMU checks, and native macOS sanitizer checks; those
results are reported by the PR author and were not independently rerun here.

## Why this work

In the October 22 meeting, Professor Andreopoulos described reducing
communication between processes by having them relate through shared memory.
Workers should publish progress/results into a common region so that a main
process can observe committed updates in real time. He identified this as a
novel direction and encouraged student extensions built on top of the
framework.

The prior bootable teaching-kernel milestone has a static model/progress area
shared by ring-0 tasks in one address space. It does not provide separate
processes, POSIX shared-memory mappings, or reattachment across `exec`. This
work adds a separate host-side process prototype to explore those requirements
without changing Pranay's kernel integration branch or claiming that the
kernel already supports process-level shared memory.

## Implemented prototype

The new runtime lives in `include/minios/process_region.h` and
`src/platform/posix/process_region.c`. It creates two named, versioned regions
per run:

- a finalized deterministic model matrix, attached read-only by workers;
- a writable progress/control region containing worker records, a
  process-shared robust mutex, semaphores, and a bounded 32-entry commit
  journal.

Shared records use run IDs, actor IDs, generations, sequence numbers, offsets,
progress counts, and deterministic result values. They do not serialize raw
virtual pointers. Model views are local process mappings, and range reads use
overflow-safe bounds validation.

The integration test,
`tests/posix/process_region_integration.c`, launches real workers with
`fork`/`exec`. The replacement process reopens the regions by name and attaches
at its own virtual mapping. It demonstrates:

1. Two workers processing separate model ranges and publishing 40 progress
   and result updates.
2. The supervisor consuming ordered commit records and independently reading
   consistent snapshots from shared memory.
3. A bounded journal that reports backpressure instead of overwriting unread
   updates.
4. A worker stopping after 2/4 committed steps and a replacement generation
   reattaching, preserving that checkpoint, then completing 4/4.
5. Rejection of stale generations and malformed/truncated region objects.
6. Read-only model mappings and robust-mutex owner-death reporting.

The test is exposed as `make test-process-regions`, included in
`make test-person-b`, and therefore included in the host portion of the
existing integrated `make test-teaching` gate. `make demo-process-regions`
prints an ordered trace of the two-worker run for a meeting/demo.

## Validation

The new POSIX test compiled with GCC 15.2 under strict C11 warnings
(`-Wall -Wextra -Werror -pedantic`) and passed:

```text
PASS: truncated, incompatible, and missing region rejection
PASS: model region validation, bounds, and read-only mapping
PASS: bounded journal backpressure and ordering
PASS: fork/exec workers, live progress, and result publication
PASS: exec reattachment, checkpoint continuity, stale generation rejection
PASS: robust owner-death detection and explicit failure
Process-shared region tests: 275 checks, 0 failures
```

`make test-person-b test-person-a` passed in WSL, including all prior Person B
host tests, the 275 process-region checks, and scheduler/region host tests.
The new process-region target was then repeated five additional times; every
run passed all 275 checks, and no named `minios-*` objects remained in
`/dev/shm` after cleanup. `make demo-process-regions` also passed and printed
the two workers' ordered progress/result trace.
The full `make test-teaching` gate, including the freshly built ISO and QEMU
shell checks, has not yet been rerun on this branch: the Docker daemon was
unavailable during this session, and QEMU/GRUB tools are not installed in the
WSL distribution used for the host tests.

## Worksheet and project opportunities

The prototype gives students concrete extensions that are visible in the
shell/demo:

- add a safe `mem` command to inspect model dimensions and sample values;
- partition model work among actors and show live progress;
- implement deterministic partial reductions and compare with a
  single-process result;
- experiment with journal batching/backpressure and quantify update latency;
- extend restart behavior with supervisor-owned worker restart policy;
- add durable checkpoints or an explicit descriptor-passing reattachment
  method; and
- compare memory sharing against pipe/message passing for the same workload.

Each exercise can build from a completed runtime and supply deterministic
normal, boundary, and failure cases. Kernel-process worksheets should wait
until the target OS exposes process and mapping primitives; the current
host-side runtime must not be presented as a kernel feature.

## Limits and openness to revision

This is an initial Linux prototype, not a final ABI or architecture decision.
The team should review region names, the ABI/version layout, worker-generation
rules, journal capacity, failure semantics, and how/when the supervisor
reaps and unlinks resources. Multi-supervisor readers, dynamic actor
registration, persistent storage, crash repair, broader POSIX portability,
and kernel process isolation remain future work.

The current teaching kernel still has one shared ring-0 address space and
static storage. The new host prototype uses POSIX shared memory and real
`exec` reattachment, but it does not yet integrate the original policy worker
or shell with this runtime. That integration is a natural next milestone after
the team agrees on the API. Requirements may evolve; the design and code
should be improved or changed as professor feedback and student worksheet
trials reveal better choices.

“Live” here means the parent consumes sequenced committed updates while
workers run. No hard real-time latency bound or performance advantage over a
message-based design has been measured yet; benchmarking is a proposed
student/project extension.

## Suggested review questions

1. Is one immutable model region plus one writable progress/result region the
   right first shared-memory split?
2. Is an ordered bounded journal with backpressure appropriate for the first
   teaching version, or should students implement the queue policy themselves?
3. Should worker reattachment use POSIX names, inherited file descriptors, or
   a supervisor manifest?
4. Which extension would make the strongest first worksheet/demo: work
   partitioning, reductions, `mem` inspection, or restart/recovery?
5. Should this host runtime remain a teaching companion, or later become a
   stepping stone toward kernel-level process support?

## Discord message

Hi everyone — a substantial update on the shared-memory direction:

PR #1 has been merged into `main`, bringing together my work as Person B on the
deterministic proactive engine (mock provider, suggestion policy, scenario
runner, and platform-neutral dispatcher) with Person A (Pranay)'s work on the
bootable teaching-kernel shell, scheduler/runtime integration, and initial
static shared model/progress API.

Building on that foundation, I’ve pushed a separate Linux host prototype on
`feature/shared-region-workers`. It uses real `fork`/`exec` workers attached
to shared model and progress/result regions. The supervisor can observe
ordered updates and snapshots; the prototype also covers bounded
history/backpressure, validation, read-only model mappings, and worker
reattachment from a checkpoint. The integrated host tests passed, including
275 process-region checks, and the worker demo passed 168 checks.

Person A (Pranay) has opened PR #2, `codex/kernel-shared-progress` into `main`,
for the kernel-side counterpart: two scheduled kernel tasks publish
structured progress and final results for the shell to observe, with ordered
bounded history and backpressure that preserves unread events. The shell can
start, pause, inspect, and resume observation of the work. PR #2’s GitHub
`build-and-boot` check passed; the PR reports host, QEMU, and sanitizer
validation as well.

These are deliberately distinct layers: my Linux prototype uses separate
processes, while the teaching-kernel work shares state between kernel tasks in
one address space. Neither should be described as adding isolated processes
to the kernel. The approach is open to review and revision. A useful next
step is to review PR #2 and the host prototype together, then choose a student
extension such as work partitioning/reduction, a live progress view, or
experiments comparing shared memory with message passing. I’ve attached the
updated progress report with the branch status, design boundaries, and
validation details.
