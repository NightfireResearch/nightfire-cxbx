#include "xbeRelocate.h"
#include "xbeOriginal.h"

#include <stdio.h>

// Through the original's bytes, so a reference under a jump over an entry moves too, for when a shadow test swaps
// the original back in
bool XbeRelocate(unsigned site, unsigned oldBase, unsigned size, const void *newBase) {
    unsigned value;
    XbeOriginal_Read(site, &value, 4);
    // A little before the start (a base folded for an index that starts at one: manager[i] through 0x0025f1c9 for
    // i + 1) and the end or a little past it (a loop's bound, which may be the next element's field)
    if (value < oldBase - 0x200 || value > oldBase + size + 0x200) {
        printf("[relocate] 0x%08x holds 0x%08x, not an address in 0x%08x..0x%08x - left alone\n", site, value,
               oldBase, oldBase + size);
        return false;
    }
    unsigned moved = (unsigned)(size_t)newBase + (value - oldBase);
    XbeOriginal_Write(site, &moved, 4);
    return true;
}
