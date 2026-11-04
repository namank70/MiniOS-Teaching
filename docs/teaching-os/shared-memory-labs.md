# Shared-memory worksheet sequence (draft)

## Goal

Give students a framework where multiple processes cooperate through memory
and produce visible results for a supervisor. The exercises should teach both
OS mechanisms and C programming: layouts, bounds, synchronization, process
lifetimes, recovery, and reproducible testing.

The current POSIX host runtime is a reference prototype, not a frozen course
ABI. The entries below are **draft specifications**. Do not assign them as
ready-made worksheets until each has a starter snapshot, instructor reference,
three automated tests, and an independent student trial.

## Learning path

| Worksheet | Level / prerequisites | Student-built feature | Three required cases | Status |
|---|---|---|---|---|
| **M01 — Inspect a shared model** | Beginner; no prerequisite | Add a `mem` command or inspector that reports model version/dimensions and reads a selected element through the safe offset API. | Normal: read `(0,0)` and `(0,1)`; boundary: read final element and zero-length end view; failure: reject a one-byte overrun and an uninitialized region handle. | Prototype APIs and range tests exist; command/student starter not yet made. |
| **M02 — Partition model work** | Beginner; M01 | Split a deterministic model scan into worker-owned ranges and show committed progress from the supervisor. | Normal: two non-overlapping ranges cover all elements; boundary: uneven element count has no gap/overlap; failure: reject duplicate or out-of-range assignment. | Two-worker reference scan exists; assignment API is a student extension. |
| **M03 — Publish a reduction** | Beginner; M02 | Workers compute partial sums/counts; supervisor combines them from shared result records. | Normal: parallel sum matches single-process reference; boundary: empty/single-element range; failure: reject duplicate completion or incompatible model version. | Not implemented. |
| **M04 — Consume live progress** | Intermediate; M02 | Display ordered worker updates and snapshots while work runs; add a batch read interface. | Normal: every committed sequence is observed once; boundary: update journal fills; failure: consumer timeout is distinct from corrupted/missing data. | Ordered bounded journal exists; batched consumption is an extension. |
| **M05 — Add backpressure policy** | Intermediate; M04 | Implement a queue policy (block, batch, or reject) and explain producer/consumer trade-offs. | Normal: consumer keeps up; boundary: journal exactly full; failure: stalled supervisor produces explicit backpressure/timeout without silent overwrite. | Blocking bounded journal exists; alternate policy is not implemented. |
| **M06 — Resume a worker** | Intermediate; M02, M04 | Restart a worker with a new generation and continue after its last committed checkpoint. | Normal: 2/4 committed then resume to 4/4; boundary: restart at 0/total; failure: old generation publication is rejected. | `fork`/`exec` reattachment and generation test exist; student starter not made. |
| **M07 — Handle worker failure** | Advanced; M06 | Supervisor detects exit/failure, decides whether to restart, and reports final run state. | Normal: clean completion; boundary: worker exits after final publish before acknowledgment; failure: child crashes before commit and does not advance progress. | Owner-death detection is tested; restart policy is not implemented. |
| **M08 — Compare communication designs** | Advanced; M02, M04 | Run the same deterministic workload with shared-memory updates and a message-based baseline; report throughput/latency. | Normal: both produce identical results; boundary: small and large update batches; failure: mismatch or missing messages fails the run. | Message baseline and performance harness are future work. |

## Suggested final student project

Build a miniature parallel matrix analyzer:

1. A supervisor creates the immutable model and writable control region.
2. Two or more workers attach independently and scan disjoint chunks.
3. Each worker publishes a partial statistic and sequenced progress record.
4. The supervisor displays live progress, combines completed partials, and
   verifies the result against a deterministic single-process reference.
5. If a worker exits early, the supervisor may relaunch it at a new generation
   from the last committed checkpoint.

This makes shared memory observable and purposeful: students can see the
supervisor's progress/result change as workers commit, then investigate what
happens at bounds, queue capacity, process restart, and synchronization
failure.

## Scope and evolution

The first starter set should target Linux host processes. It should not imply
that the current booted MiniOS kernel has user processes or POSIX mappings.
After student trials and professor feedback, the team can revise the ABI,
worksheet ordering, actor count, synchronization primitive, or whether the
kernel later adopts any of these ideas. The first prototype is a learning
platform to evaluate—not a requirement that future designs remain unchanged.
