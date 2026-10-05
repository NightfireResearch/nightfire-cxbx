#ifndef DRIVING_WORLD_COLLISIONINSTANCE_H_
#define DRIVING_WORLD_COLLISIONINSTANCE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "CollisionTypes.h"
#include "../engine/CoreContainers.h"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"

// ---------------------------------------------------------------------------------------------------------------
// The placed things the collision system tests against: the methods of the collision instances and objects
// (CollisionTypes.h has their layouts), the triggers' matrix and size, and the small geometric helpers the collision
// manager's queries call (the window records of CheckHitWindow, the 2D point-in-triangle tests, the distance from a
// barrier). See CollisionInstance.cpp.
// ---------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------
// The collision package's std::vectors (Dinkumware's, as the game compiled them; the layout is CoreContainers.h's
// GameVector) and the library's helpers their compiled copies call. The game has one copy of each helper per
// element size, the linker having folded the identical ones together; these templates are that code, so the same
// memory written in the same order. Storage comes from UMemory::FastAlloc ("STL") and goes back with FastFree at
// its size.
// ---------------------------------------------------------------------------------------------------------------

// The warning beside a provisional port in the collision package: code no shipped data reaches (the STL's "too
// long" throws, inserting in the middle of a vector), ported from the listing without a test. Once.
inline void WorldUntested(const char *what) {
    printf("[world] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define WORLD_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            WorldUntested(what); \
        } \
    } while (0)

namespace ColStl {

// uninitialized_copy: answers the end of the copy
template <class T>
T *UninitializedCopy(const T *first, const T *last, T *dest) {
    for (; first != last; first++, dest++)
        if (dest != NULL)
            *dest = *first;
    return dest;
}

// uninitialized_fill_n
template <class T>
void UninitializedFill(T *dest, uint32_t count, const T &value) {
    for (; count != 0; count--, dest++)
        if (dest != NULL)
            *dest = value;
}

template <class T>
void Fill(T *first, T *last, const T &value) {
    for (; first != last; first++)
        *first = value;
}

template <class T>
T *CopyBackward(const T *first, const T *last, T *dest) {
    while (first != last)
        *--dest = *--last;
    return dest;
}

// vector::_Tidy's storage release
template <class T>
void Tidy(GameVector<T> *vector) {
    if (vector->first != NULL)
        UMemory::FastFree(vector->first, uint32_t(vector->end - vector->first) * sizeof(T));
    vector->first = NULL;
    vector->last = NULL;
    vector->end = NULL;
}

// vector::reserve
template <class T, class Xlen>
void Reserve(ColVector<T> *vector, uint32_t count, uint32_t maxSize, Xlen xlen) {
    if (count > maxSize) {
        xlen();
        return;
    }
    if (vector->Capacity() < count) {
        uint32_t bytes = count * sizeof(T);
        T *storage = static_cast<T *>(UMemory::FastAlloc(bytes, "STL"));
        UninitializedCopy(vector->first, vector->last, storage);
        uint32_t size = vector->Size();
        if (vector->first != NULL)
            UMemory::FastFree(vector->first, uint32_t(vector->end - vector->first) * sizeof(T));
        vector->end = storage + count;
        vector->last = storage + size;
        vector->first = storage;
    }
}

// vector::_Insert_n
template <class T, class Xlen>
void InsertN(ColVector<T> *vector, T *where, uint32_t count, const T *value, uint32_t maxSize, Xlen xlen) {
    T copy = *value;   // copied first: it may live in the vector
    uint32_t capacity = vector->Capacity();
    if (count == 0)
        return;
    uint32_t size = vector->Size();
    if (maxSize - size < count) {
        xlen();
        return;
    }
    if (capacity < size + count) {
        uint32_t grown = maxSize - capacity / 2 < capacity ? 0 : capacity + capacity / 2;
        if (grown < size + count)
            grown = size + count;
        T *storage = static_cast<T *>(UMemory::FastAlloc(grown * sizeof(T), "STL"));
        T *at = UninitializedCopy(vector->first, where, storage);
        UninitializedFill(at, count, copy);
        UninitializedCopy(where, vector->last, at + count);
        uint32_t newSize = count + vector->Size();
        if (vector->first != NULL)
            UMemory::FastFree(vector->first, uint32_t(vector->end - vector->first) * sizeof(T));
        vector->end = storage + grown;
        vector->last = storage + newSize;
        vector->first = storage;
    } else if (uint32_t(vector->last - where) < count) {
        WORLD_UNTESTED("a collision vector's _Insert_n in the middle");   // push_back only inserts when full
        UninitializedCopy(where, vector->last, where + count);
        UninitializedFill(vector->last, count - uint32_t(vector->last - where), copy);
        vector->last += count;
        Fill(where, vector->last - count, copy);
    } else {
        WORLD_UNTESTED("a collision vector's _Insert_n in the middle");
        T *oldLast = vector->last;
        vector->last = UninitializedCopy(oldLast - count, oldLast, oldLast);
        CopyBackward(where, oldLast - count, oldLast);
        Fill(where, where + count, copy);
    }
}

// vector::push_back
template <class T, class Insert>
void PushBack(ColVector<T> *vector, const T *value, Insert insert) {
    if (vector->first != NULL && vector->Size() < vector->Capacity()) {
        UninitializedFill(vector->last, 1, *value);
        vector->last++;
    } else {
        insert(vector->last, 1, value);
    }
}

}  // namespace ColStl

// A trigger volume (WTriggerManager's): its matrix and its packed size. Only the fields read here; the object is
// larger.
class WTrigger {
public:
    enum Flag : uint8_t {
        kRotated = 0x10,           // flags: the up axis is computed from the other two
    };

    Coord3 position;               // +0x00
    uint8_t unknown0c[7];
    uint8_t flags;                 // +0x13 Flag
    uint8_t unknown14[0xc];
    Coord3 right;                  // +0x20
    uint32_t unknown2c;
    Coord3 forward;                // +0x30
    uint32_t packedSize;           // +0x3c bit 31: the size is in bits 20-29, else in bits 0-9 (doubled);
                                   //       bit 30 picks the unit

    double Size();                                       // 0x000cf1f0, answered unrounded (as on the x87 stack)
    void MakeMatrix(MATRIX4 *out, bool translate);       // 0x000cf260
};
static_assert(offsetof(WTrigger, flags) == 0x13, "WTrigger::flags");
static_assert(offsetof(WTrigger, packedSize) == 0x3c, "WTrigger::packedSize");

#endif // DRIVING_WORLD_COLLISIONINSTANCE_H_
