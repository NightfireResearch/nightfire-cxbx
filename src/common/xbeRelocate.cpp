#include "xbeRelocate.h"
#include "xbeOriginal.h"

#include <stdio.h>

// Through the original's bytes, so a reference under a jump over an entry moves too, for when a shadow test swaps
// the original back in
bool XbeRelocate(unsigned site, unsigned oldBase, unsigned size, const void *newBase) {
    unsigned value;
    XbeOriginal_Read(site, &value, 4);
    if (value < oldBase || value > oldBase + size) {   // the end too: a loop's bound
        printf("[relocate] 0x%08x holds 0x%08x, not an address in 0x%08x..0x%08x - left alone\n", site, value,
               oldBase, oldBase + size);
        return false;
    }
    unsigned moved = (unsigned)(size_t)newBase + (value - oldBase);
    XbeOriginal_Write(site, &moved, 4);
    return true;
}
