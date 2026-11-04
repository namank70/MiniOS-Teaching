#include "minios/runtime.h"
int minios_region_bounds(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}
