#ifndef DRIVING_DATA_ATTRIBUTEUNTESTED_H_
#define DRIVING_DATA_ATTRIBUTEUNTESTED_H_

// The warning beside a provisional port in the attribute system: code no shipped data reaches (the STL's "too
// long" and "invalid iterator" throws, the heap sort a store-block list of more than 32 blocks would need, the
// edit-configuration map nothing fills), ported from the listing without a test. It says so once, the first time
// it runs, so that whatever first reaches it gets checked against the original.

#include <stdio.h>

inline void AttributeUntested(const char *what) {
    printf("[data] WARNING: %s ran - a provisional port of the attribute system that no shipped data reaches, "
           "UNTESTED. Check what it computes against the original.\n", what);
    fflush(stdout);
}

#define ATTRIBUTE_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            AttributeUntested(what); \
        } \
    } while (0)

#endif // DRIVING_DATA_ATTRIBUTEUNTESTED_H_
