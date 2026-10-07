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

namespace CARP {
class Instance;
}
struct TriggerEvents;   // Trigger.h

// A trigger volume (0x40 bytes, one of WTriggerManager's): a box, a sphere or an upright cylinder whose events run
// when something it accepts touches it. Its methods are in Trigger.cpp (and Size and MakeMatrix in
// CollisionInstance.cpp); the manager's tests in TriggerManager.cpp.
class WTrigger {
public:
    enum Shape : uint8_t {
        kBox = 1,                  // width along right, depth along forward, height up from the position
        kSphere = 2,               // of the radius about the position
        kCylinder = 3,             // of the radius, height up from the position
    };

    // (the names of the touch bits are ours, from WTriggerManager::Process)
    enum Flag : uint16_t {
        kEnabled = 0x0001,         // tested at all
        kOnce = 0x0002,            // FireEvents clears kEnabled
        kRigidType1 = 0x0004,      // touched by rigid bodies whose unknown6d is 1
        kRigidType2 = 0x0008,      // by rigid bodies whose unknown6d is 2, simple bodies of types 2 and 8
        kSimpleType1 = 0x0010,     // by simple bodies of type 1
        kOthers = 0x0040,          // by the other rigid bodies, simple bodies of types 4 and 5, and ray shells
        kPlayer = 0x0080,          // by the player's simple bodies and ray shells; the others' skip it
        kFlag100 = 0x0100,         // Smackable::GoToSleep sets and clears it
        kInstances = 0x0200,       // touched by the path engine's instances; Init clears kWhileActive
        kWhileActive = 0x0400,     // only while WTriggerManager::active is set
        kDirectional = 0x0800,     // only by things moving along forward (TestDirection)
        kRotated = 0x1000,         // the up axis is computed from the other two
        kIgnored = 0x2000,         // never tested
    };

    Coord3 position;               // +0x00 the bottom's centre
    float radius;                  // +0x0c of a sphere or cylinder; position and radius make the Coord4 the grid
                                   //       keeps it by
    uint8_t unknown10;
    uint8_t shape;                 // +0x11 Shape
    uint16_t flags;                // +0x12 Flag
    float height;                  // +0x14
    TriggerEvents *events;         // +0x18 NULL: none
    uint32_t queryStamp;           // +0x1c the last WTriggerManager::Process that looked at it
    Coord3 right;                  // +0x20
    float width;                   // +0x2c a box's, along right
    Coord3 forward;                // +0x30
    union {                        // +0x3c
        float depth;               // a box's, along forward
        uint32_t packedSize;       // what Size reads: bit 31: the size is in bits 20-29, else in bits 0-9
                                   //   (doubled); bit 30 picks the unit (CARP::Instance::packedDimensions)
    };

    // The position and radius as one vector (the grid's position and radius)
    Coord4 *Bounds() { return reinterpret_cast<Coord4 *>(this); }

    double Size();                                       // 0x000cf1f0, answered unrounded (as on the x87 stack)
    void MakeMatrix(MATRIX4 *out, bool translate);       // 0x000cf260
    void FireEvents(bool flag, int index, CARP::Instance *instance);    // 0x000cf300
    bool TestDirection(const Coord4 *segment);           // 0x000cf440
    bool UpdateRotPos(const Coord4 *rotation, const Coord3 *position); // 0x000cf490
};
static_assert(sizeof(WTrigger) == 0x40, "a trigger is 64 bytes");
static_assert(offsetof(WTrigger, flags) == 0x12, "WTrigger::flags");
static_assert(offsetof(WTrigger, events) == 0x18, "WTrigger::events");
static_assert(offsetof(WTrigger, packedSize) == 0x3c, "WTrigger::packedSize");

#endif // DRIVING_WORLD_COLLISIONINSTANCE_H_
