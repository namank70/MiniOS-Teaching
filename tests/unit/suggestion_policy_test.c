#include "minios/suggestion_policy.h"

#include <stdio.h>
#include <stdlib.h>

#define MATRIX_WIDTH 2u
#define MATRIX_HEIGHT 2u
#define MATRIX_SIZE ((size_t)MATRIX_WIDTH * MATRIX_HEIGHT)

static const int32_t weights[MATRIX_SIZE] = {-50, -19, -2, 29};
static unsigned int assertions;

static void check(int condition, const char *message)
{
    ++assertions;
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static MiniOSContext make_context(uint64_t now_ms, uint64_t sequence,
                                  uint32_t zone, uint32_t fuel,
                                  uint32_t idle)
{
    MiniOSContext context = {
        MINIOS_CONTEXT_VERSION,
        now_ms,
        (uint32_t)((720u + now_ms / UINT64_C(60000)) % 1440u),
        zone,
        fuel,
        idle,
        sequence
    };
    return context;
}

static MiniOSModelView make_model(void)
{
    MiniOSModelView model = {
        MINIOS_MOCK_MODEL_VERSION,
        MATRIX_WIDTH,
        MATRIX_HEIGHT,
        MATRIX_SIZE,
        sizeof(weights),
        weights
    };
    return model;
}

static void test_initial_zone_trigger_and_dedup(void)
{
    MiniOSSuggestionPolicy policy;
    MiniOSContext context = make_context(0u, 1u, MINIOS_ZONE_STATION, 10u, 30u);
    MiniOSModelView model = make_model();
    MiniOSSuggestionEvent event;
    MiniOSSuggestionEvent repeated;

    check(minios_policy_init(&policy, 720u) == MINIOS_POLICY_NO_TRIGGER,
          "policy initializes at the scenario start time");
    check(minios_policy_evaluate(&policy, &context, &model, &event) ==
              MINIOS_POLICY_EVENT_READY,
          "initial zone context triggers proactive evaluation");
    check(event.actor_id == MINIOS_RECOMMENDER_ACTOR_ID &&
              event.at_ms == 0u && event.input_sequence == 1u &&
              event.model_version == MINIOS_MOCK_MODEL_VERSION &&
              event.suggestion == MINIOS_SUGGESTION_REFUEL,
          "published event has stable semantic fields");
    check(minios_policy_evaluate(&policy, &context, &model, &repeated) ==
              MINIOS_POLICY_EVENT_READY,
          "re-evaluating a pending input returns the pending event");
    check(event.suggestion == repeated.suggestion &&
              event.input_sequence == repeated.input_sequence,
          "pending event retry is byte-semantically stable");
    context.fuel_percent = 90u;
    check(minios_policy_evaluate(&policy, &context, &model, &repeated) ==
              MINIOS_POLICY_PENDING,
          "changed context cannot reuse an event pending for an earlier input");
    context.fuel_percent = 10u;
    check(minios_policy_commit(&policy, 1u) == MINIOS_POLICY_EVENT_READY,
          "accepted event can be committed");
    check(minios_policy_evaluate(&policy, &context, &model, &repeated) ==
              MINIOS_POLICY_NO_TRIGGER,
          "same zone sequence does not emit a duplicate");
}

static void test_half_hour_and_idle_gate(void)
{
    MiniOSSuggestionPolicy policy;
    MiniOSContext context;
    MiniOSModelView model = make_model();
    MiniOSSuggestionEvent event;

    (void)minios_policy_init(&policy, 720u);
    context = make_context(0u, 1u, MINIOS_ZONE_ROAD, 80u, 30u);
    check(minios_policy_evaluate(&policy, &context, &model, &event) ==
              MINIOS_POLICY_NO_SUGGESTION,
          "initial road context is evaluated but emits no suggestion");

    context = make_context(UINT64_C(1799999), 2u,
                           MINIOS_ZONE_ROAD, 80u, 30u);
    check(minios_policy_evaluate(&policy, &context, &model, &event) ==
              MINIOS_POLICY_NO_TRIGGER,
          "no evaluation occurs before the half-hour boundary");

    context = make_context(UINT64_C(1800000), 3u,
                           MINIOS_ZONE_REST_STOP, 80u, 29u);
    check(minios_policy_evaluate(&policy, &context, &model, &event) ==
              MINIOS_POLICY_SUPPRESSED_IDLE,
          "idle 29 suppresses a boundary and location trigger");

    context = make_context(UINT64_C(1860000), 4u,
                           MINIOS_ZONE_REST_STOP, 80u, 30u);
    check(minios_policy_evaluate(&policy, &context, &model, &event) ==
              MINIOS_POLICY_NO_TRIGGER,
          "consumed boundary does not evaluate continuously within the slot");
}

static void test_zone_trigger_and_recent_history(void)
{
    MiniOSSuggestionPolicy policy;
    MiniOSContext context;
    MiniOSModelView model = make_model();
    MiniOSSuggestionEvent event;

    (void)minios_policy_init(&policy, 720u);
    context = make_context(0u, 1u, MINIOS_ZONE_REST_STOP, 80u, 30u);
    check(minios_policy_evaluate(&policy, &context, &model, &event) ==
              MINIOS_POLICY_EVENT_READY &&
              event.suggestion == MINIOS_SUGGESTION_TAKE_BREAK,
          "initial rest-stop context proactively suggests a break");
    (void)minios_policy_commit(&policy, 1u);

    context = make_context(1000u, 2u, MINIOS_ZONE_ROAD, 80u, 30u);
    check(minios_policy_evaluate(&policy, &context, &model, &event) ==
              MINIOS_POLICY_NO_SUGGESTION,
          "leaving the stop triggers evaluation with new context");

    context = make_context(2000u, 3u, MINIOS_ZONE_REST_STOP, 80u, 30u);
    check(minios_policy_evaluate(&policy, &context, &model, &event) ==
              MINIOS_POLICY_SUPPRESSED_REPEAT,
          "recent suggestion ID suppresses a repeated suggestion");
}

static void test_time_and_model_errors(void)
{
    MiniOSSuggestionPolicy policy;
    MiniOSContext context;
    MiniOSModelView model = make_model();
    MiniOSSuggestionEvent event;

    (void)minios_policy_init(&policy, 720u);
    context = make_context(1000u, 1u, MINIOS_ZONE_ROAD, 80u, 30u);
    check(minios_policy_evaluate(&policy, &context, &model, &event) ==
              MINIOS_POLICY_NO_SUGGESTION,
          "valid initial context is accepted");

    context = make_context(500u, 2u, MINIOS_ZONE_STATION, 10u, 30u);
    check(minios_policy_evaluate(&policy, &context, &model, &event) ==
              MINIOS_POLICY_BAD_TIME,
          "backwards logical time is rejected");

    context = make_context(2000u, 2u, MINIOS_ZONE_STATION, 10u, 30u);
    model.byte_length = sizeof(weights) - sizeof(weights[0]);
    check(minios_policy_evaluate(&policy, &context, &model, &event) ==
              MINIOS_POLICY_PROVIDER_ERROR,
          "invalid model view is reported as provider error");

    context = make_context(2000u, 2u, 99u, 10u, 30u);
    model = make_model();
    check(minios_policy_evaluate(&policy, &context, &model, &event) ==
              MINIOS_POLICY_BAD_CONTEXT,
          "unknown zone is reported as invalid context");

    context = make_context(2000u, 1u, MINIOS_ZONE_STATION, 10u, 30u);
    check(minios_policy_evaluate(&policy, &context, &model, &event) ==
              MINIOS_POLICY_BAD_CONTEXT,
          "zone changes cannot reuse a committed input sequence");
}

int main(void)
{
    test_initial_zone_trigger_and_dedup();
    test_half_hour_and_idle_gate();
    test_zone_trigger_and_recent_history();
    test_time_and_model_errors();
    printf("PASS: suggestion policy (%u assertions)\n", assertions);
    return EXIT_SUCCESS;
}
