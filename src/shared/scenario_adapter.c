#include "minios/suggestion_policy.h"

#include <stddef.h>
#include <stdint.h>

#define SCENARIO_MATRIX_WIDTH 256u
#define SCENARIO_MATRIX_HEIGHT 256u
#define SCENARIO_MATRIX_SIZE \
    ((size_t)SCENARIO_MATRIX_WIDTH * SCENARIO_MATRIX_HEIGHT)

static MiniOSSuggestionPolicy scenario_policy;
static int32_t scenario_weights[SCENARIO_MATRIX_SIZE];
static int scenario_model_initialized;

static void initialize_model(void)
{
    uint32_t row;
    uint32_t column;

    for (row = 0; row < SCENARIO_MATRIX_HEIGHT; ++row) {
        for (column = 0; column < SCENARIO_MATRIX_WIDTH; ++column) {
            scenario_weights[(size_t)row * SCENARIO_MATRIX_WIDTH + column] =
                (int32_t)(((17u * row + 31u * column) % 101u)) - 50;
        }
    }
    scenario_model_initialized = 1;
}

int minios_scenario_init(uint32_t start_minute_of_day)
{
    if (!scenario_model_initialized) {
        initialize_model();
    }
    return (int)minios_policy_init(&scenario_policy, start_minute_of_day);
}

int minios_scenario_evaluate(uint64_t now_ms,
                             uint32_t minute_of_day,
                             uint32_t zone,
                             uint32_t fuel_percent,
                             uint32_t idle_minutes,
                             uint64_t input_sequence,
                             uint32_t *actor_id,
                             uint32_t *model_version,
                             uint32_t *suggestion,
                             uint64_t *event_time_ms,
                             uint64_t *event_sequence)
{
    MiniOSContext context = {
        MINIOS_CONTEXT_VERSION,
        now_ms,
        minute_of_day,
        zone,
        fuel_percent,
        idle_minutes,
        input_sequence
    };
    MiniOSModelView model = {
        MINIOS_MOCK_MODEL_VERSION,
        SCENARIO_MATRIX_WIDTH,
        SCENARIO_MATRIX_HEIGHT,
        SCENARIO_MATRIX_SIZE,
        sizeof(scenario_weights),
        scenario_weights
    };
    MiniOSSuggestionEvent event;
    MiniOSPolicyStatus status;

    if (actor_id == NULL || model_version == NULL || suggestion == NULL ||
        event_time_ms == NULL || event_sequence == NULL) {
        return (int)MINIOS_POLICY_BAD_ARGUMENT;
    }

    status = minios_policy_evaluate(&scenario_policy, &context, &model, &event);
    if (status == MINIOS_POLICY_EVENT_READY) {
        *actor_id = event.actor_id;
        *model_version = event.model_version;
        *suggestion = (uint32_t)event.suggestion;
        *event_time_ms = event.at_ms;
        *event_sequence = event.input_sequence;
    }

    return (int)status;
}

int minios_scenario_commit(uint64_t input_sequence)
{
    return (int)minios_policy_commit(&scenario_policy, input_sequence);
}
