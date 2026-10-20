#include "minios/shell.h"

#include "minios/mock_provider.h"

#include <stdint.h>

static const char help_text[] =
    "Commands:\n"
    "  help     Show available commands\n"
    "  status   Show mock policy status\n"
    "  suggest  Evaluate a fixed context\n"
    "  test     Run deterministic recommendation checks\n";

static const char status_text[] =
    "Policy: mock-v1; time/location gate: v1\n";

static int text_equal(const char *left, size_t left_length, const char *right)
{
    size_t index = 0;

    while (right[index] != '\0') {
        if (index >= left_length || left[index] != right[index]) {
            return 0;
        }
        ++index;
    }

    return index == left_length;
}

static MiniOSShellStatus write_text(
    MiniOSShellWrite write,
    void *write_context,
    const char *text,
    MiniOSShellStatus success_status)
{
    if (write(write_context, text) != 0) {
        return MINIOS_SHELL_OUTPUT_ERROR;
    }
    return success_status;
}

static MiniOSMockStatus evaluate_fixed_context(MiniOSSuggestion *suggestion)
{
    static const int32_t model_weights[4] = {-50, -19, -2, 29};
    const MiniOSModelView model = {
        MINIOS_MOCK_MODEL_VERSION,
        2u,
        2u,
        4u,
        sizeof(model_weights),
        model_weights
    };
    const MiniOSContext context = {
        MINIOS_CONTEXT_VERSION,
        0u,
        12u * 60u,
        MINIOS_ZONE_STATION,
        10u,
        30u,
        1u
    };
    MiniOSMockResult result;
    MiniOSMockStatus status = minios_mock_predict(&context, &model, &result);

    if (status == MINIOS_MOCK_OK) {
        *suggestion = result.suggestion;
    }
    return status;
}

static MiniOSShellStatus execute_suggest(
    MiniOSShellWrite write,
    void *write_context)
{
    MiniOSSuggestion suggestion;

    if (evaluate_fixed_context(&suggestion) != MINIOS_MOCK_OK ||
        suggestion != MINIOS_SUGGESTION_REFUEL) {
        return MINIOS_SHELL_OUTPUT_ERROR;
    }
    return write_text(write, write_context,
                      "Proactive suggestion: REFUEL\n", MINIOS_SHELL_OK);
}

static MiniOSShellStatus execute_test(
    MiniOSShellWrite write,
    void *write_context)
{
    MiniOSSuggestion first;
    MiniOSSuggestion second;

    if (evaluate_fixed_context(&first) != MINIOS_MOCK_OK ||
        evaluate_fixed_context(&second) != MINIOS_MOCK_OK ||
        first != MINIOS_SUGGESTION_REFUEL || first != second) {
        if (write(write_context,
                  "FAIL: deterministic recommendation test\n") != 0) {
            return MINIOS_SHELL_OUTPUT_ERROR;
        }
        return MINIOS_SHELL_POLICY_ERROR;
    }

    return write_text(write, write_context,
                      "PASS: deterministic recommendation test\n",
                      MINIOS_SHELL_OK);
}

MiniOSShellStatus minios_shell_execute(
    const char *line,
    MiniOSShellWrite write,
    void *write_context)
{
    size_t length = 0;
    size_t start = 0;
    size_t end;
    size_t index;

    if (line == NULL || write == NULL) {
        return MINIOS_SHELL_BAD_ARGUMENT;
    }

    while (line[length] != '\0' && length <= MINIOS_SHELL_MAX_LINE) {
        ++length;
    }
    if (length > MINIOS_SHELL_MAX_LINE) {
        return MINIOS_SHELL_LINE_TOO_LONG;
    }

    while (start < length &&
           (line[start] == ' ' || line[start] == '\t')) {
        ++start;
    }
    end = length;
    while (end > start &&
           (line[end - 1] == ' ' || line[end - 1] == '\t' ||
            line[end - 1] == '\r' || line[end - 1] == '\n')) {
        --end;
    }
    for (index = start; index < end; ++index) {
        if (line[index] == ' ' || line[index] == '\t' ||
            line[index] == '\r' || line[index] == '\n') {
            return write_text(write, write_context,
                              "Usage: enter one command at a time\n",
                              MINIOS_SHELL_BAD_ARGUMENT);
        }
    }

    if (start == end) {
        return MINIOS_SHELL_OK;
    }
    if (text_equal(line + start, end - start, "help")) {
        return write_text(write, write_context, help_text, MINIOS_SHELL_OK);
    }
    if (text_equal(line + start, end - start, "status")) {
        return write_text(write, write_context, status_text, MINIOS_SHELL_OK);
    }
    if (text_equal(line + start, end - start, "suggest")) {
        return execute_suggest(write, write_context);
    }
    if (text_equal(line + start, end - start, "test")) {
        return execute_test(write, write_context);
    }

    return write_text(write, write_context,
                      "Unknown command. Type 'help'.\n",
                      MINIOS_SHELL_UNKNOWN_COMMAND);
}
