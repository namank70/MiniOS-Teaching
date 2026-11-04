#include "minios/mock_provider.h"

#include <stdio.h>
#include <stdlib.h>

#define MATRIX_WIDTH 256u
#define MATRIX_HEIGHT 256u
#define MATRIX_SIZE ((size_t)MATRIX_WIDTH * MATRIX_HEIGHT)

static int32_t mock_weights[MATRIX_SIZE];
static unsigned int assertions;

static void check(int condition, const char *message)
{
    ++assertions;
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void initialize_model(void)
{
    uint32_t row;
    uint32_t column;

    for (row = 0; row < MATRIX_HEIGHT; ++row) {
        for (column = 0; column < MATRIX_WIDTH; ++column) {
            int32_t value =
                (int32_t)(((17u * row + 31u * column) % 101u)) - 50;
            mock_weights[(size_t)row * MATRIX_WIDTH + column] = value;
        }
    }
}

static MiniOSModelView valid_model(void)
{
    MiniOSModelView model = {
        MINIOS_MOCK_MODEL_VERSION,
        MATRIX_WIDTH,
        MATRIX_HEIGHT,
        MATRIX_SIZE,
        sizeof(mock_weights),
        mock_weights
    };
    return model;
}

static MiniOSContext context_for(uint32_t zone, uint32_t fuel, uint32_t idle)
{
    MiniOSContext context = {
        MINIOS_CONTEXT_VERSION,
        0u,
        12u * 60u,
        zone,
        fuel,
        idle,
        1u
    };
    return context;
}

static void test_station_fuel_boundaries(void)
{
    MiniOSContext context = context_for(MINIOS_ZONE_STATION, 10u, 0u);
    MiniOSModelView model = valid_model();
    MiniOSMockResult first;
    MiniOSMockResult second;

    check(mock_weights[0] == -50 && mock_weights[1] == -19,
          "mock matrix starts with the planned sentinel values");
    check(minios_mock_predict(&context, &model, &first) == MINIOS_MOCK_OK,
          "station prediction succeeds");
    check(first.suggestion == MINIOS_SUGGESTION_REFUEL,
          "fuel 10 at station suggests refuel");
    check(first.model_version == MINIOS_MOCK_MODEL_VERSION,
          "result carries the model version");
    check(first.checksum != 0u, "model data is read for the checksum");

    check(minios_mock_predict(&context, &model, &second) == MINIOS_MOCK_OK,
          "repeated station prediction succeeds");
    check(first.suggestion == second.suggestion &&
              first.checksum == second.checksum &&
              first.model_version == second.model_version,
          "identical input and model produce identical output");

    context.fuel_percent = 15u;
    check(minios_mock_predict(&context, &model, &first) == MINIOS_MOCK_OK &&
              first.suggestion == MINIOS_SUGGESTION_REFUEL,
          "fuel 15 at station is included in the refuel threshold");

    context.fuel_percent = 16u;
    check(minios_mock_predict(&context, &model, &first) == MINIOS_MOCK_OK &&
              first.suggestion == MINIOS_SUGGESTION_NONE,
          "fuel 16 at station does not suggest refuel");
}

static void test_rule_precedence_and_idle_threshold(void)
{
    MiniOSContext context = context_for(MINIOS_ZONE_REST_STOP, 50u, 29u);
    MiniOSModelView model = valid_model();
    MiniOSMockResult result;

    check(minios_mock_predict(&context, &model, &result) == MINIOS_MOCK_OK &&
              result.suggestion == MINIOS_SUGGESTION_NONE,
          "idle 29 at rest stop does not suggest a break");

    context.idle_minutes = 30u;
    check(minios_mock_predict(&context, &model, &result) == MINIOS_MOCK_OK &&
              result.suggestion == MINIOS_SUGGESTION_TAKE_BREAK,
          "idle 30 at rest stop suggests a break");

    context.zone = MINIOS_ZONE_DESTINATION;
    check(minios_mock_predict(&context, &model, &result) == MINIOS_MOCK_OK &&
              result.suggestion == MINIOS_SUGGESTION_PREPARE_ARRIVAL,
          "destination context suggests arrival preparation");
}

static void test_invalid_inputs(void)
{
    MiniOSContext context = context_for(MINIOS_ZONE_STATION, 101u, 0u);
    MiniOSModelView model = valid_model();
    MiniOSMockResult result = {
        MINIOS_SUGGESTION_REFUEL,
        MINIOS_MOCK_MODEL_VERSION,
        123u
    };

    check(minios_mock_predict(&context, &model, &result) ==
              MINIOS_MOCK_BAD_CONTEXT,
          "fuel above 100 is rejected");
    check(result.suggestion == MINIOS_SUGGESTION_NONE &&
              result.model_version == 0u && result.checksum == 0u,
          "invalid context leaves no success-shaped result");

    context.fuel_percent = 10u;
    context.version = MINIOS_CONTEXT_VERSION + 1u;
    check(minios_mock_predict(&context, &model, &result) ==
              MINIOS_MOCK_BAD_CONTEXT,
          "unsupported context version is rejected");

    context.version = MINIOS_CONTEXT_VERSION;
    context.zone = 99u;
    check(minios_mock_predict(&context, &model, &result) ==
              MINIOS_MOCK_BAD_CONTEXT,
          "unknown zone is rejected");

    context.zone = MINIOS_ZONE_STATION;
    model.element_count -= 1u;
    check(minios_mock_predict(&context, &model, &result) ==
              MINIOS_MOCK_BAD_MODEL,
          "model descriptor with a mismatched element count is rejected");

    model = valid_model();
    model.byte_length -= sizeof(int32_t);
    check(minios_mock_predict(&context, &model, &result) ==
              MINIOS_MOCK_BAD_MODEL,
          "truncated model view is rejected");

    model = valid_model();
    model.model_version += 1u;
    check(minios_mock_predict(&context, &model, &result) ==
              MINIOS_MOCK_BAD_MODEL,
          "unsupported model version is rejected");

    check(minios_mock_predict(&context, &model, NULL) ==
              MINIOS_MOCK_BAD_ARGUMENT,
          "null result pointer is rejected");
}

int main(void)
{
    initialize_model();
    test_station_fuel_boundaries();
    test_rule_precedence_and_idle_threshold();
    test_invalid_inputs();
    printf("PASS: mock provider (%u assertions)\n", assertions);
    return EXIT_SUCCESS;
}
