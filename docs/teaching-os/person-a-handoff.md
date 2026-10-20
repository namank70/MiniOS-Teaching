# Person A handoff — kernel shell integration

## What is runnable

The teaching ISO boots to a real text shell. Serial input/output and PS/2 keyboard input are wired. Naman's `help`, `status`, `suggest`, and `test` dispatcher is linked unchanged. Kernel adapters add `tasks`, `progress`, actual status, and kernel self-checks. A scheduled task independently publishes a mock recommendation after approximately two seconds.

Run from this branch with Docker running:

```sh
./tools/teaching.sh run-teaching
```

Exit with Ctrl-a then x. For the professor, show the prompt, wait for the unsolicited recommendation, then type `help`, `status`, `tasks`, `progress`, and `test`.

## Exact integration points

- Entry: `kernel/teaching/boot.asm:_start` calls `kernel/teaching/kernel.c:kernel_main`.
- Command loop: `kernel/teaching/kernel.c:shell_task`.
- Dispatcher bridge: `execute` calls `minios_shell_execute`; `shell_write` is the callback.
- Console hooks: `kernel/teaching/console.h` exposes `console_read()` (nonblocking, -1 if empty) and `console_write()`.
- Task state/time: `kernel/teaching/scheduler.h` exposes snapshots, sleep/yield, nominal ticks and 64-bit uptime.
- Region/progress header: `include/minios/runtime.h`; detailed contract is in `runtime-api.md`.

The minimal shell loop was added as an adapter so the system is runnable now. Naman can own future command/loop behavior without editing scheduler assembly. Further dispatcher changes belong in `src/shared/shell.c`; transport changes belong in `kernel/teaching/console.c`. Coordinate changes to `kernel.c` at this boundary to avoid overlapping edits.

## Branch relationship

`feature/person-a-shared-memory` is based on Naman's `feature/person-b-proactive-engine` at `8af8f36cabaefab3a776fbf54e0423b7f0e77312`. It includes his original commits, rather than copying or rewriting their history. The branch name follows the requested name; its delivered scope is the shell-first integration and a small static region/progress interface.

Main remains the stable branch. Review the integration before merging. There is no need to independently cherry-pick the same Person B changes if this descendant branch is merged.

## Validation and honest limits

`./tools/teaching.sh test-teaching` builds and tests both contributions, then boots the actual ISO in QEMU. It checks serial commands, a QMP-generated PS/2 command, proactive output, task progress, timer ownership/sleep, model access, and input errors. See `kernel-shell.md` for all commands and limits.

Original SNN/research/CarPlay sources and build targets remain in place. The demonstrated provider is deterministic mock v1. This is not a POSIX process shell: no mmap/exec, external programs, pipelines, or user-mode isolation. The historical safety harness's missing files have not been fabricated or claimed integrated. The new kernel adapter uses Naman's actual published code.

## Suggested update to Naman

The boot/kernel integration is runnable on my branch. Your dispatcher and policy are linked unchanged, with a minimal kernel input loop and output callback. Both serial and PS/2 input work, and the full test command boots QEMU and exercises them. The entry is `kernel/teaching/kernel.c:kernel_main`; console hooks are in `kernel/teaching/console.h`; region/progress contracts are in `include/minios/runtime.h`. Run `./tools/teaching.sh run-teaching`. We can show the real shell and scheduled mock recommendation, while explicitly deferring native mmap/exec and real-SNN integration.

This is a prepared handoff; no message has been sent on your behalf.
