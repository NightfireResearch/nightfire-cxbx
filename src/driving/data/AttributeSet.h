#ifndef DRIVING_DATA_ATTRIBUTESET_H_
#define DRIVING_DATA_ATTRIBUTESET_H_

// AttributeSet: a handle on one attribute collection (AttributeSystem.h), by class and name - what the game's
// objects hold (PhysicsObject, PVehicle, WWorld, the AI) to look their tuning up by key. Constructing one loads
// the collection's file if that has not happened yet; lookups search the collection and then its parents, and
// mark the value used. See AttributeSet.cpp.

#include "AttributeSystem.h"

#include <stdint.h>

class AttributeSet {
public:
    AttributeCollection *collection;   // +0x00

    AttributeSet *Construct(const char *className, const char *name);                           // 0x00058f00
    AttributeSet *ConstructCopy(const AttributeSet &other);                                     // 0x00052020
    void Destruct();                                                                            // 0x00057a40
    void DestructThunk();   // the same, a second copy of the destructor                        // 0x000752f0
    // Moves the set to another name of the same class; with `keepAsParent` the old collection becomes the new
    // one's parent.
    void SetName(const char *name, bool keepAsParent);                                          // 0x00058f40
    const char *Name();                                                                         // 0x00052030

    // The value of `key` in the collection or its parents, its node's value marked used.
    bool FindAttribute(const char *key, AttributeValue **value);                                // 0x00058020

    // Each answers the value (a string value parsed as the type), and whether it was there through `found` if
    // given; a missing key answers 0, false, 0.0, the default vector (0,0,0,1), NULL or "".
    // (A bool lookup of a value that is not a bool answers the first byte of its data, unconverted.)
    uint8_t LookupBool(const char *key, bool *found);                                           // 0x000580e0
    int32_t LookupInt(const char *key, bool *found);                                            // 0x00058180
    uint32_t LookupUInt(const char *key, bool *found);                                          // 0x00058220
    float LookupFloat(const char *key, bool *found);                                            // 0x000582c0
    const AttributeVector *LookupVector(const char *key, bool *found);                          // 0x00058360
    const char *LookupString(const char *key, bool *found);                                     // 0x000583a0
    const char *LookupValidString(const char *key, bool *found);                                // 0x000583e0
    // The extension structure of `type` kept under `key`, made in this collection if it has none.
    void *LookupStruct(const char *key, uint32_t type, bool *found);                            // 0x00058420
};
static_assert(sizeof(AttributeSet) == 4, "an AttributeSet is a pointer");

#endif // DRIVING_DATA_ATTRIBUTESET_H_
