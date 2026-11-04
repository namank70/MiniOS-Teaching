# MiniOS Teaching Integration Baseline

Base repository: https://github.com/Castellanos02/MiniOS
Base branch: main
Base commit: 59e331d7f7b8f5760fa41a5a6db85bf287397123

New team repository: https://github.com/namank70/MiniOS-Teaching
Shared branch: main

Scheduler branch references (not merged):
- miniOS-scheduler-version: 14346c51583a24e988908b10dc7335e738f9d3ea
- External-Scheduler_API-and-WIP-scheduler-integration: 785d989ac869676a293e50813d3cbb6fa9751912

Status: Original main history pushed as the starting baseline.
No donor scheduler branches have been merged.

## Person A shell integration

The Person A branch is based on Person B commit `8af8f36cabaefab3a776fbf54e0423b7f0e77312`. It adds a separate `teaching-iso` target, a kernel shell adapter, serial/PS2 console hooks, and a corrected adaptation of the scheduler ABI donor. No donor branch was merged wholesale; see `docs/teaching-os/provenance.md` for the selective source reuse and `docs/teaching-os/person-a-handoff.md` for runtime entry points. Original main targets and research sources are retained.
