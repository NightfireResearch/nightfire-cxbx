#include "SingletonVector.h"
#include "DataUntested.h"

#include "../engine/UMemory.hpp"

#include <stddef.h>

// ---------------------------------------------------------------------------------------------------------------
// The singleton list's push_back and _Insert_n (see SingletonVector.h). The STL's own helpers - the uninitialised
// copies and fills, copy_backward, the length error and the deallocation - are called at their addresses, in the
// original's order with its arguments. The original's catch block (free the new storage, rethrow) has nothing to
// catch here: copying pointers cannot throw.
// ---------------------------------------------------------------------------------------------------------------

#define Vector_UninitializedCopy ((USingleton **(*)(USingleton **, USingleton **, USingleton **, SingletonVector *, uint32_t))0x000bfeb0)
#define Vector_UninitializedFillN ((void (*)(USingleton **, uint32_t, USingleton *const *, SingletonVector *, uint32_t))0x000b2ec0)
#define Vector_Deallocate ((void (__fastcall *)(SingletonVector *, int, USingleton **, uint32_t))0x000ad630)
#define Vector_LengthError ((void (__fastcall *)(SingletonVector *, int))0x00059ca0)
#define Vector_Ucopy ((USingleton **(__fastcall *)(SingletonVector *, int, USingleton **, USingleton **, USingleton **))0x000c1230)
#define Vector_Ufill ((void (__fastcall *)(SingletonVector *, int, USingleton **, uint32_t, USingleton *const *))0x000c1290)
#define Vector_CopyBackward ((void (*)(USingleton ***, USingleton **, USingleton **, USingleton **))0x000b2e40)
#define Vector_Fill ((void (*)(USingleton **, USingleton **, USingleton *const *))0x0004f3e0)

constexpr uint32_t kMaxSize = 0x3fffffff;

// FUNC_AT(0x0011b820)
void SingletonVector::InsertN(USingleton **where, uint32_t count, USingleton *const *value) {
    USingleton *copy = *value;   // the value is copied first: it may live in the vector
    uint32_t capacity = first != NULL ? uint32_t(end - first) : 0;
    if (count == 0)
        return;
    uint32_t size = first != NULL ? uint32_t(last - first) : 0;
    if (kMaxSize - size < count) {
        DATA_UNTESTED("SingletonVector::InsertN's length error");
        Vector_LengthError(this, 0);
        return;
    }
    if (capacity < size + count) {
        uint32_t grown = kMaxSize - capacity / 2 < capacity ? 0 : capacity + capacity / 2;
        if (grown < size + count)
            grown = size + count;
        uint32_t bytes = grown * sizeof(USingleton *);
        USingleton **storage = static_cast<USingleton **>(UMemory::FastAlloc(bytes, "STL"));
        USingleton **at = Vector_UninitializedCopy(first, where, storage, this, count);
        Vector_UninitializedFillN(at, count, &copy, this, count);
        Vector_UninitializedCopy(where, last, at + count, this, count);
        uint32_t newSize = count + (first != NULL ? uint32_t(last - first) : 0);
        if (first != NULL)
            Vector_Deallocate(this, 0, first, uint32_t(end - first));
        end = reinterpret_cast<USingleton **>(reinterpret_cast<uint8_t *>(storage) + bytes);
        last = storage + newSize;
        first = storage;
    } else if (uint32_t(last - where) < count) {
        // Room enough: push_back never comes here (it only inserts when the vector is full).
        DATA_UNTESTED("SingletonVector::InsertN in the middle");
        Vector_Ucopy(this, 0, where, last, where + count);
        Vector_Ufill(this, 0, last, count - uint32_t(last - where), &copy);
        last += count;
        Vector_Fill(where, last - count, &copy);
    } else {
        DATA_UNTESTED("SingletonVector::InsertN in the middle");
        USingleton **oldLast = last;
        last = Vector_Ucopy(this, 0, oldLast - count, oldLast, oldLast);
        USingleton **ignored;
        Vector_CopyBackward(&ignored, where, oldLast - count, oldLast);
        Vector_Fill(where, where + count, &copy);
    }
}

// FUNC_AT(0x0011bae0)
void SingletonVector::PushBack(USingleton *const *value) {
    uint32_t size = first != NULL ? uint32_t(last - first) : 0;
    if (first != NULL && size < uint32_t(end - first)) {
        Vector_UninitializedFillN(last, 1, value, this, uint32_t(uintptr_t(value)));
        last++;
        return;
    }
    InsertN(last, 1, value);
}
