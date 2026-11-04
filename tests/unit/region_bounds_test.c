#include "minios/runtime.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(minios_region_bounds(64,8,4));
    assert(minios_region_bounds(64,64,0));
    assert(!minios_region_bounds(64,63,2));
    assert(!minios_region_bounds(64,SIZE_MAX,2));
    assert(!minios_region_bounds(64,1,SIZE_MAX));
    puts("PASS: region boundaries and overflow");
}
