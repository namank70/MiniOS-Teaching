# Source provenance and preservation

| Source | Revision | Use |
|---|---|---|
| Original MiniOS main | `59e331d7f7b8f5760fa41a5a6db85bf287397123` | Original repository history, kernels, linker script, research and demo targets retained |
| MiniOS-Teaching shared main | `38cdeebe4335acfedd4648131a6c9b37021b0753` | Team integration baseline |
| Naman's proactive-engine branch | `8af8f36cabaefab3a776fbf54e0423b7f0e77312` | Dispatcher, mock provider, policy, host scenarios and tests retained and linked |
| Castellanos02 scheduler API branch | `785d989ac869676a293e50813d3cbb6fa9751912` | Four-task fixed-priority structure, stack-switch ESP/EBP ABI, assembly switch sequence, PIT/PIC setup adapted into teaching platform |
| Earlier scheduler branch | `14346c51583a24e988908b10dc7335e738f9d3ea` | Historical reference; not imported as a second scheduler |

Scheduler donor: [External-Scheduler-ABI](https://github.com/Castellanos02/MiniOS/tree/785d989ac869676a293e50813d3cbb6fa9751912/External-Scheduler-ABI).

The integration separates platform code from host-testable task policy, considers RUNNING tasks during selection, advances time only on timer interrupts, guards scheduler mutations, handles sleep-counter wraparound, and acknowledges the PIC before changing task stacks. A dedicated GDT and exception diagnostics avoid relying on GRUB's descriptor-table lifetime. The first two Task fields preserve the donor assembly ABI and have compile-time offset assertions.

Original kernel variants are not overwritten. The new teaching entry/console/task adapters are a separate build target; no two duplicate scheduler runtimes are linked. Naman's shared dispatcher/provider/policy source is unchanged by the Person A integration. The old safety-harness README/writeup is documentation only at the supplied public revision and is not represented as recovered source.
