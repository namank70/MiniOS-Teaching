# Shell command layer

`include/minios/shell.h` and `src/shared/shell.c` provide a bounded command
dispatcher with an output callback. The caller owns the command loop and
console input; it passes one NUL-terminated line to `minios_shell_execute()`
and supplies a write callback that sends text to the chosen console.

The current commands are:

- `help`: list supported commands.
- `status`: identify the mock policy version.
- `suggest`: evaluate the fixed station/low-fuel teaching context.
- `test`: repeat that deterministic provider call and report pass/fail.

Input is limited to 64 characters. Leading/trailing whitespace and a trailing
line ending are accepted; arguments are not yet supported. Unknown commands,
overlong lines, invalid arguments, and console write failures return explicit
status values. No dynamic allocation or platform-specific I/O is used by the
dispatcher.

Run the host tests in Linux or WSL:

```sh
make test-shell
```

This is the command-dispatch layer and host test, not yet an interactive shell
inside the booted kernel. Person A's kernel console integration must provide
the read loop and output callback. The `suggest` command uses a tiny local
teaching fixture; it does not yet read the shared model region.
