#include "xbeRelocate.h"

#include <stdio.h>
#include <string.h>

bool XbeRelocate(unsigned site, unsigned oldBase, unsigned size, const void *newBase) {
    unsigned value;
    memcpy(&value, (const void *)(size_t)site, 4);
    if (value < oldBase || value >= oldBase + size) {
        printf("[relocate] 0x%08x holds 0x%08x, not an address in 0x%08x..0x%08x - left alone\n", site, value,
               oldBase, oldBase + size);
        return false;
    }
    unsigned moved = (unsigned)(size_t)newBase + (value - oldBase);
    memcpy((void *)(size_t)site, &moved, 4);
    return true;
}
