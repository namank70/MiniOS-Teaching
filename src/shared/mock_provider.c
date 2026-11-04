#include "minios/mock_provider.h"

#include <stdint.h>

static int context_is_valid(const MiniOSContext *context)
{
    return context != NULL &&
           context->version == MINIOS_CONTEXT_VERSION &&
           context->minute_of_day < 24u * 60u &&
           context->zone <= MINIOS_ZONE_DESTINATION &&
           context->fuel_percent <= 100u;
}

static int model_is_valid(const MiniOSModelView *model)
{
    size_t expected_count;

    if (model == NULL || model->weights == NULL ||
        model->model_version != MINIOS_MOCK_MODEL_VERSION ||
        model->width == 0u || model->height == 0u ||
        (size_t)model->width > SIZE_MAX / (size_t)model->height) {
        return 0;
    }

    expected_count = (size_t)model->width * (size_t)model->height;
    return expected_count <= SIZE_MAX / sizeof(*model->weights) &&
           model->element_count == expected_count &&
           model->byte_length >= expected_count * sizeof(*model->weights);
}

static uint32_t model_checksum(const MiniOSModelView *model)
{
    uint32_t checksum = UINT32_C(2166136261);
    size_t index;

    for (index = 0; index < model->element_count; ++index) {
        checksum = (checksum ^ (uint32_t)model->weights[index]) *
                   UINT32_C(16777619);
    }

    return checksum;
}

static MiniOSSuggestion predict_rule(const MiniOSContext *context)
{
    if (context->zone == MINIOS_ZONE_STATION &&
        context->fuel_percent <= 15u) {
        return MINIOS_SUGGESTION_REFUEL;
    }

    if (context->zone == MINIOS_ZONE_REST_STOP &&
        context->idle_minutes >= 30u) {
        return MINIOS_SUGGESTION_TAKE_BREAK;
    }

    if (context->zone == MINIOS_ZONE_DESTINATION) {
        return MINIOS_SUGGESTION_PREPARE_ARRIVAL;
    }

    return MINIOS_SUGGESTION_NONE;
}

MiniOSMockStatus minios_mock_predict(
    const MiniOSContext *context,
    const MiniOSModelView *model,
    MiniOSMockResult *result)
{
    if (result == NULL) {
        return MINIOS_MOCK_BAD_ARGUMENT;
    }

    result->suggestion = MINIOS_SUGGESTION_NONE;
    result->model_version = 0u;
    result->checksum = 0u;

    if (!context_is_valid(context)) {
        return MINIOS_MOCK_BAD_CONTEXT;
    }

    if (!model_is_valid(model)) {
        return MINIOS_MOCK_BAD_MODEL;
    }

    result->suggestion = predict_rule(context);
    result->model_version = model->model_version;
    result->checksum = model_checksum(model);
    return MINIOS_MOCK_OK;
}
