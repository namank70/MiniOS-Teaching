# Host worksheet drafts

These three exercises are bounded around behavior that already has a host-side
reference implementation. Each lists three acceptance cases and the command
that runs the related tests. They do not require Person A's process or kernel
interfaces.

These are worksheet specifications, not student starter packages. Before
assigning one, make a starter snapshot with the implementation portion removed
or replaced by a clearly marked exercise stub, and have someone other than the
author complete it.

## W06 — Deterministic mock-provider rules

**Level:** 1, independent  
**Objective:** Implement a deterministic rule provider that validates its
inputs, returns a stable suggestion ID, and observes rule precedence.  
**Student work:** Implement `minios_mock_predict()` in
`src/shared/mock_provider.c`. Use the public types and status values in
`include/minios/mock_provider.h`; do not add wall-clock, random, or external
service dependencies.

| Case | Input | Expected result |
|---|---|---|
| Threshold boundary | Valid model; `STATION`, fuel 15 | `MINIOS_MOCK_OK`, `REFUEL` |
| Above threshold | Valid model; `STATION`, fuel 16 | `MINIOS_MOCK_OK`, `NONE` |
| Invalid context | Valid model; fuel 101 | `MINIOS_MOCK_BAD_CONTEXT`; result is reset to `NONE`, version 0, checksum 0 |

**Run reference tests:** `make test-mock-provider`  
**Explain:** Why should invalid input not leave a previous successful result in
the output structure?

## W14 — Time cadence and idle gate

**Level:** 2; builds on the context and suggestion rules from W06  
**Objective:** Evaluate only when the policy has a time or zone trigger, and
apply the 30-minute idle gate to the triggered snapshot.  
**Student work:** Implement the cadence and idle-gate behavior in
`minios_policy_evaluate()` in `src/shared/suggestion_policy.c`. Use fixture
logical time and sequence values; do not sleep or read the system clock.

| Case | Setup and input | Expected result |
|---|---|---|
| Boundary reached, eligible | Start at minute 720; initialize at `now_ms=0` with `REST_STOP`, idle 29; at `now_ms=1800000`, sequence 2, same zone, idle 30 | `MINIOS_POLICY_EVENT_READY`, `TAKE_BREAK` |
| Just before boundary | Initialize with `ROAD` at `now_ms=0`; at `now_ms=1799999`, sequence 2, keep `ROAD` | `MINIOS_POLICY_NO_TRIGGER` |
| Trigger suppressed | Initialize with `ROAD` at `now_ms=0`; at `now_ms=1800000`, sequence 2, `REST_STOP`, idle 29 | `MINIOS_POLICY_SUPPRESSED_IDLE` |

**Run reference tests:** `make test-suggestion-policy`  
**Explain:** Why is a suppressed half-hour trigger consumed rather than retried
on every later context update in the same half-hour slot?

## W22 — Zone trigger and pending-event retry

**Level:** 3; builds on W06 and W14  
**Objective:** Trigger evaluation on a zone change and make event publication
safe to retry without accepting a changed input as the old event.  
**Student work:** Implement zone-trigger detection and the pending-event
protocol in `minios_policy_evaluate()` and `minios_policy_commit()` in
`src/shared/suggestion_policy.c`.

| Case | Setup and input | Expected result |
|---|---|---|
| Zone entry | Initialize with `ROAD` at `now_ms=0`; sequence 2 changes to `STATION`, fuel 10, idle 30 | `MINIOS_POLICY_EVENT_READY`, `REFUEL` |
| Exact retry | Evaluate the same pending context again before commit | `MINIOS_POLICY_EVENT_READY`; event fields match the first result |
| Changed pending input | Before commit, change fuel while keeping the pending timestamp and sequence | `MINIOS_POLICY_PENDING`; the old event is not returned as a match |

**Run reference tests:** `make test-suggestion-policy` and
`make test-scenarios`  
**Explain:** Why should the caller commit only after the event consumer accepts
the event?

## Current evidence and release boundary

The current reference implementation is host C11 code. The commands above run
the corresponding existing unit tests; they do not build the kernel, launch
worker processes, exercise shared memory, or prove that the command dispatcher
is connected to a booted shell. The worksheet cases are specifications, not
separate per-case executables.

Before course use, provide isolated starter snapshots, confirm that each
worksheet's three cases are independently runnable by students, and have a
second person complete each exercise from its starter. Keep the expected
outputs fixed for an assigned version.
