#ifndef MINIOS_MOCK_PROVIDER_H
#define MINIOS_MOCK_PROVIDER_H

#include <stddef.h>
#include <stdint.h>

#define MINIOS_CONTEXT_VERSION 1u
#define MINIOS_MOCK_MODEL_VERSION 1u

typedef enum {
    MINIOS_ZONE_ROAD = 0,
    MINIOS_ZONE_STATION = 1,
    MINIOS_ZONE_REST_STOP = 2,
    MINIOS_ZONE_DESTINATION = 3
} MiniOSZone;

typedef enum {
    MINIOS_SUGGESTION_NONE = 0,
    MINIOS_SUGGESTION_REFUEL = 1,
    MINIOS_SUGGESTION_TAKE_BREAK = 2,
    MINIOS_SUGGESTION_PREPARE_ARRIVAL = 3
} MiniOSSuggestion;

typedef enum {
    MINIOS_MOCK_OK = 0,
    MINIOS_MOCK_BAD_ARGUMENT,
    MINIOS_MOCK_BAD_CONTEXT,
    MINIOS_MOCK_BAD_MODEL
} MiniOSMockStatus;

typedef struct {
    uint32_t version;
    uint64_t now_ms;
    uint32_t minute_of_day;
    uint32_t zone;
    uint32_t fuel_percent;
    uint32_t idle_minutes;
    uint64_t input_sequence;
} MiniOSContext;

typedef struct {
    uint32_t model_version;
    uint32_t width;
    uint32_t height;
    size_t element_count;
    size_t byte_length;
    const int32_t *weights;
} MiniOSModelView;

typedef struct {
    MiniOSSuggestion suggestion;
    uint32_t model_version;
    uint32_t checksum;
} MiniOSMockResult;

MiniOSMockStatus minios_mock_predict(
    const MiniOSContext *context,
    const MiniOSModelView *model,
    MiniOSMockResult *result);

#endif
