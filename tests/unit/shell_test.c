#include "minios/shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char buffer[256];
    size_t length;
    int fail_writes;
} OutputBuffer;

static unsigned int assertions;

static void check(int condition, const char *message)
{
    ++assertions;
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static int capture_output(void *context, const char *text)
{
    OutputBuffer *output = (OutputBuffer *)context;
    size_t text_length = strlen(text);

    if (output->fail_writes ||
        text_length >= sizeof(output->buffer) - output->length) {
        return -1;
    }
    memcpy(output->buffer + output->length, text, text_length + 1u);
    output->length += text_length;
    return 0;
}

static void reset_output(OutputBuffer *output)
{
    memset(output, 0, sizeof(*output));
}

static void test_help_and_whitespace(void)
{
    OutputBuffer output = {{0}, 0u, 0};

    check(minios_shell_execute("  help \r\n", capture_output, &output) ==
              MINIOS_SHELL_OK,
          "help accepts surrounding whitespace and line ending");
    check(strcmp(output.buffer,
                 "Commands:\n"
                 "  help     Show available commands\n"
                 "  status   Show mock policy status\n"
                 "  suggest  Evaluate a fixed context\n"
                 "  test     Run deterministic recommendation checks\n") == 0,
          "help prints the command list exactly");

    reset_output(&output);
    check(minios_shell_execute(" \t \n", capture_output, &output) ==
              MINIOS_SHELL_OK && output.length == 0u,
          "empty command is ignored without output");
}

static void test_policy_commands(void)
{
    OutputBuffer output = {{0}, 0u, 0};

    check(minios_shell_execute("suggest", capture_output, &output) ==
              MINIOS_SHELL_OK,
          "suggest command succeeds");
    check(strcmp(output.buffer, "Proactive suggestion: REFUEL\n") == 0,
          "suggest command returns the fixed proactive result");

    reset_output(&output);
    check(minios_shell_execute("test", capture_output, &output) ==
              MINIOS_SHELL_OK,
          "test command succeeds");
    check(strcmp(output.buffer,
                 "PASS: deterministic recommendation test\n") == 0,
          "test command reports exact deterministic result");
}

static void test_errors_and_bounds(void)
{
    OutputBuffer output = {{0}, 0u, 0};
    char long_line[MINIOS_SHELL_MAX_LINE + 2u];

    check(minios_shell_execute("unknown", capture_output, &output) ==
              MINIOS_SHELL_UNKNOWN_COMMAND,
          "unknown command has an explicit status");
    check(strcmp(output.buffer, "Unknown command. Type 'help'.\n") == 0,
          "unknown command prints the help hint");

    reset_output(&output);
    check(minios_shell_execute("help extra", capture_output, &output) ==
              MINIOS_SHELL_BAD_ARGUMENT,
          "extra command tokens are rejected");

    reset_output(&output);
    memset(long_line, 'x', sizeof(long_line));
    long_line[sizeof(long_line) - 1u] = '\0';
    check(minios_shell_execute(long_line, capture_output, &output) ==
              MINIOS_SHELL_LINE_TOO_LONG && output.length == 0u,
          "overlong input is rejected without output");

    check(minios_shell_execute(NULL, capture_output, &output) ==
              MINIOS_SHELL_BAD_ARGUMENT,
          "null command is rejected");
    check(minios_shell_execute("help", NULL, &output) ==
              MINIOS_SHELL_BAD_ARGUMENT,
          "null output callback is rejected");
}

static void test_output_failure(void)
{
    OutputBuffer output = {{0}, 0u, 1};

    check(minios_shell_execute("help", capture_output, &output) ==
              MINIOS_SHELL_OUTPUT_ERROR,
          "console output failure is reported");
}

int main(void)
{
    test_help_and_whitespace();
    test_policy_commands();
    test_errors_and_bounds();
    test_output_failure();
    printf("PASS: shell commands (%u assertions)\n", assertions);
    return EXIT_SUCCESS;
}
