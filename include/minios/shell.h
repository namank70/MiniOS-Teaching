#ifndef MINIOS_SHELL_H
#define MINIOS_SHELL_H

#include <stddef.h>

#define MINIOS_SHELL_MAX_LINE 64u

typedef int (*MiniOSShellWrite)(void *context, const char *text);

typedef enum {
    MINIOS_SHELL_OK = 0,
    MINIOS_SHELL_UNKNOWN_COMMAND,
    MINIOS_SHELL_LINE_TOO_LONG,
    MINIOS_SHELL_BAD_ARGUMENT,
    MINIOS_SHELL_OUTPUT_ERROR,
    MINIOS_SHELL_POLICY_ERROR
} MiniOSShellStatus;

MiniOSShellStatus minios_shell_execute(
    const char *line,
    MiniOSShellWrite write,
    void *write_context);

#endif
