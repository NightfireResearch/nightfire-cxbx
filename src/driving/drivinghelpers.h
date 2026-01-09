#ifndef DRIVINGHELPERS_H
#define DRIVINGHELPERS_H

#include "../helpers.h"

// In various places (eg UGroup::GroupLocateTag or UGroup::DataLocateTag)
// a 4-byte "tag" is needed. The tag seems to be meaningful ASCII chars:
// 0x52756c65 -> "Rule"
// 0x4d536574 -> "MSet"
// 0x4d617020 -> "Map "
// This macro allows us to bring the original meaning back into the code.
// It's cleaner than relying on multi-character character behaviour, which
// is implementation-defined.
template <size_t N> constexpr uint32_t TAG4(const char (&s)[N])
{
    static_assert(N > 1, "TAG4: empty string");
    static_assert(N <= 5, "TAG4: string too long");

    return (uint32_t(s[0]) << 24) |
           (uint32_t(N > 2 ? s[1] : ' ') << 16) |
           (uint32_t(N > 3 ? s[2] : ' ') << 8)  |
           (uint32_t(N > 4 ? s[3] : ' '));
}

static_assert(TAG4("Map") == TAG4("Map "), "TAG4 macro is broken (padding behaviour)");
static_assert(TAG4("Map") == 0x4d617020, "TAG4 macro is broken (byte order or functionality)");

#endif //DRIVINGHELPERS_H
