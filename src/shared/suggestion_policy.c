#include "minios/suggestion_policy.h"

#include <string.h>

static uint64_t absolute_half_hour_slot(
    uint32_t start_minute_of_day,
    uint64_t now_ms)
{
    uint64_t elapsed_minutes = now_ms / UINT64_C(60000);
    return ((uint64_t)start_minute_of_day + elapsed_minutes) / 30u;
}

static uint32_t expected_minute_of_day(
    uint32_t start_minute_of_day,
    uint64_t now_ms)
{
    uint64_t elapsed_minutes = now_ms / UINT64_C(60000);
    return (uint32_t)(((uint64_t)start_minute_of_day + elapsed_minutes) %
                      (24u * 60u));
}

static int history_contains(
    const MiniOSSuggestionPolicy *policy,
    MiniOSSuggestion suggestion)
{
    size_t index;

    for (index = 0; index < policy->recent_count; ++index) {
        if (policy->recent[index] == suggestion) {
            return 1;
        }
    }

    return 0;
}

static void commit_context(
    MiniOSSuggestionPolicy *policy,
    const MiniOSContext *context,
    uint64_t absolute_slot)
{
    policy->last_now_ms = context->now_ms;
    policy->last_input_sequence = context->input_sequence;
    policy->last_absolute_slot = absolute_slot;
    policy->last_zone = context->zone;
    policy->initialized = 1;
}

MiniOSPolicyStatus minios_policy_init(
    MiniOSSuggestionPolicy *policy,
    uint32_t start_minute_of_day)
{
    if (policy == NULL || start_minute_of_day >= 24u * 60u) {
        return MINIOS_POLICY_BAD_ARGUMENT;
    }

    memset(policy, 0, sizeof(*policy));
    policy->start_minute_of_day = start_minute_of_day;
    return MINIOS_POLICY_NO_TRIGGER;
}

MiniOSPolicyStatus minios_policy_evaluate(
    MiniOSSuggestionPolicy *policy,
    const MiniOSContext *context,
    const MiniOSModelView *model,
    MiniOSSuggestionEvent *event)
{
    MiniOSMockResult prediction;
    MiniOSMockStatus provider_status;
    uint64_t absolute_slot;
    int zone_trigger;
    int time_trigger;

    if (policy == NULL || context == NULL || model == NULL || event == NULL) {
        return MINIOS_POLICY_BAD_ARGUMENT;
    }

    if (policy->has_pending) {
        if (context->now_ms == policy->pending_context.now_ms &&
            context->input_sequence == policy->pending_context.input_sequence) {
            *event = policy->pending_event;
            return MINIOS_POLICY_EVENT_READY;
        }
        return MINIOS_POLICY_PENDING;
    }

    if (context->version != MINIOS_CONTEXT_VERSION ||
        context->minute_of_day >= 24u * 60u ||
        context->zone > MINIOS_ZONE_DESTINATION ||
        context->fuel_percent > 100u) {
        return MINIOS_POLICY_BAD_CONTEXT;
    }

    if (context->minute_of_day !=
            expected_minute_of_day(policy->start_minute_of_day,
                                   context->now_ms) ||
        (policy->initialized &&
         (context->now_ms < policy->last_now_ms ||
          context->input_sequence < policy->last_input_sequence))) {
        return MINIOS_POLICY_BAD_TIME;
    }

    absolute_slot = absolute_half_hour_slot(policy->start_minute_of_day,
                                            context->now_ms);
    if (policy->initialized &&
        context->input_sequence == policy->last_input_sequence) {
        if (context->zone != policy->last_zone) {
            return MINIOS_POLICY_BAD_CONTEXT;
        }
        if (context->now_ms == policy->last_now_ms) {
            return MINIOS_POLICY_NO_TRIGGER;
        }
    }

    zone_trigger = !policy->initialized ||
                   (context->input_sequence > policy->last_input_sequence &&
                    context->zone != policy->last_zone);
    time_trigger = !policy->initialized ||
                   absolute_slot > policy->last_absolute_slot;

    if (!zone_trigger && !time_trigger) {
        commit_context(policy, context, absolute_slot);
        return MINIOS_POLICY_NO_TRIGGER;
    }

    if (context->idle_minutes < 30u) {
        commit_context(policy, context, absolute_slot);
        return MINIOS_POLICY_SUPPRESSED_IDLE;
    }

    provider_status = minios_mock_predict(context, model, &prediction);
    if (provider_status != MINIOS_MOCK_OK) {
        return MINIOS_POLICY_PROVIDER_ERROR;
    }

    if (prediction.suggestion == MINIOS_SUGGESTION_NONE) {
        commit_context(policy, context, absolute_slot);
        return MINIOS_POLICY_NO_SUGGESTION;
    }

    if (history_contains(policy, prediction.suggestion)) {
        commit_context(policy, context, absolute_slot);
        return MINIOS_POLICY_SUPPRESSED_REPEAT;
    }

    policy->pending_context = *context;
    policy->pending_event.actor_id = MINIOS_RECOMMENDER_ACTOR_ID;
    policy->pending_event.at_ms = context->now_ms;
    policy->pending_event.input_sequence = context->input_sequence;
    policy->pending_event.model_version = prediction.model_version;
    policy->pending_event.suggestion = prediction.suggestion;
    policy->has_pending = 1;
    *event = policy->pending_event;
    return MINIOS_POLICY_EVENT_READY;
}

MiniOSPolicyStatus minios_policy_commit(
    MiniOSSuggestionPolicy *policy,
    uint64_t input_sequence)
{
    MiniOSContext committed_context;
    uint64_t absolute_slot;

    if (policy == NULL) {
        return MINIOS_POLICY_BAD_ARGUMENT;
    }
    if (!policy->has_pending ||
        policy->pending_event.input_sequence != input_sequence) {
        return MINIOS_POLICY_BAD_ARGUMENT;
    }

    committed_context = policy->pending_context;
    absolute_slot = absolute_half_hour_slot(policy->start_minute_of_day,
                                            committed_context.now_ms);
    policy->recent[policy->recent_next] = policy->pending_event.suggestion;
    policy->recent_next =
        (policy->recent_next + 1u) % MINIOS_POLICY_HISTORY_CAPACITY;
    if (policy->recent_count < MINIOS_POLICY_HISTORY_CAPACITY) {
        ++policy->recent_count;
    }

    commit_context(policy, &committed_context, absolute_slot);
    policy->has_pending = 0;
    memset(&policy->pending_context, 0, sizeof(policy->pending_context));
    memset(&policy->pending_event, 0, sizeof(policy->pending_event));
    return MINIOS_POLICY_EVENT_READY;
}
