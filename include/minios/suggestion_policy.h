#ifndef MINIOS_SUGGESTION_POLICY_H
#define MINIOS_SUGGESTION_POLICY_H

#include <stddef.h>
#include <stdint.h>

#include "minios/mock_provider.h"

#define MINIOS_POLICY_HISTORY_CAPACITY 3u
#define MINIOS_RECOMMENDER_ACTOR_ID 3u

typedef enum {
    MINIOS_POLICY_NO_TRIGGER = 0,
    MINIOS_POLICY_SUPPRESSED_IDLE,
    MINIOS_POLICY_NO_SUGGESTION,
    MINIOS_POLICY_SUPPRESSED_REPEAT,
    MINIOS_POLICY_EVENT_READY,
    MINIOS_POLICY_BAD_ARGUMENT,
    MINIOS_POLICY_BAD_CONTEXT,
    MINIOS_POLICY_BAD_TIME,
    MINIOS_POLICY_PROVIDER_ERROR,
    MINIOS_POLICY_PENDING
} MiniOSPolicyStatus;

typedef struct {
    uint32_t actor_id;
    uint64_t at_ms;
    uint64_t input_sequence;
    uint32_t model_version;
    MiniOSSuggestion suggestion;
} MiniOSSuggestionEvent;

typedef struct {
    uint32_t start_minute_of_day;
    uint64_t last_now_ms;
    uint64_t last_input_sequence;
    uint64_t last_absolute_slot;
    uint32_t last_zone;
    MiniOSSuggestion recent[MINIOS_POLICY_HISTORY_CAPACITY];
    size_t recent_count;
    size_t recent_next;
    int initialized;
    int has_pending;
    MiniOSContext pending_context;
    MiniOSSuggestionEvent pending_event;
} MiniOSSuggestionPolicy;

MiniOSPolicyStatus minios_policy_init(
    MiniOSSuggestionPolicy *policy,
    uint32_t start_minute_of_day);

MiniOSPolicyStatus minios_policy_evaluate(
    MiniOSSuggestionPolicy *policy,
    const MiniOSContext *context,
    const MiniOSModelView *model,
    MiniOSSuggestionEvent *event);

MiniOSPolicyStatus minios_policy_commit(
    MiniOSSuggestionPolicy *policy,
    uint64_t input_sequence);

#endif
