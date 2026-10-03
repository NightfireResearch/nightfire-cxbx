#ifndef DRIVING_DATA_SINGLETONVECTOR_H_
#define DRIVING_DATA_SINGLETONVECTOR_H_

// std::vector<USingleton *>'s growth, as the game compiled it (USingletonManager::Register's push_back and the
// _Insert_n under it). USingletonManager itself is another package's (engine/USingleton.h); this is the vector
// it is, under a name of its own until the two are joined.

#include <stdint.h>

class USingleton;

struct SingletonVector {
    uint32_t allocator;
    USingleton **first;
    USingleton **last;
    USingleton **end;

    // push_back (0x0011bae0, Ghidra: FUN_0011bae0).
    void PushBack(USingleton *const *value);
    // _Insert_n (0x0011b820, Ghidra: FUN_0011b820): `count` copies of *value before `where`, growing by half.
    void InsertN(USingleton **where, uint32_t count, USingleton *const *value);
};
static_assert(sizeof(SingletonVector) == 16, "a vector is 16 bytes");

#endif // DRIVING_DATA_SINGLETONVECTOR_H_
