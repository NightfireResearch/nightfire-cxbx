#ifndef DRIVING_PHYSICS_PHYSICSNAMESPACE_H_
#define DRIVING_PHYSICS_PHYSICSNAMESPACE_H_

#include <stddef.h>
#include <stdint.h>

#include "../data/AttributeSet.h"

// ---------------------------------------------------------------------------------------------------------------
// PhysicsNamespace (4 bytes, a vtable; RRenderHigh::InitGameRender makes one and adds it to the symbol table as
// "PHYS"): looks a name up as the "PhysicsData" structure of the "smackable" collection of that name. Its
// constructor registers that structure's type and fields with the attribute system. See PhysicsNamespace.cpp.
// ---------------------------------------------------------------------------------------------------------------

// The "smackable" class's "PhysicsData" structure (0x2c bytes), each field named as its attribute
struct PhysicsData {
    AttributeSet attributes;    // +0x00 the collection's
    float mass;                 // +0x04 MASS
    float hitPoints;            // +0x08 HITPOINTS
    uint8_t forceDetach;        // +0x0c FORCE_DETACH; MOMENT, a vector, is registered at this offset too
    uint8_t unknown0d[3];
    int32_t defaultSound;       // +0x10 DEFAULTSOUND, and the five sounds after it: -1 none
    int32_t warningSound;       // +0x14 WARNINGSOUND
    int32_t impactSoundLo;      // +0x18 IMPACTSOUNDLO
    int32_t impactSoundMed;     // +0x1c IMPACTSOUNDMED
    int32_t impactSoundHi;      // +0x20 IMPACTSOUNDHI
    int32_t scrapeSound;        // +0x24 SCRAPESOUND
    float description;          // +0x28 DESCRIPTION, registered with the float parser
};
static_assert(sizeof(PhysicsData) == 0x2c, "PhysicsData is 0x2c bytes");

// A new PhysicsData's defaults (the type's AttributeExtensionInit)                               0x0006ee10
void InitPhysicsData(const char *className, const char *collectionName, const char *attributeName, uint32_t type,
                     void *data);

class PhysicsNamespace {
public:
    void **vtable;              // +0x00

    PhysicsNamespace* Construct();                                                              // 0x0006ee90
    // vtable slot 0: the PhysicsData of `name` ("Bench"'s if it has none), and the name's size with its NUL
    void* NameLookup(const char *name, uint32_t *size);                                         // 0x0006ed40
    PhysicsNamespace* Delete(unsigned flags);   // the scalar deleting destructor, slot 1        // 0x0006f050
};
static_assert(sizeof(PhysicsNamespace) == 4, "a physics namespace is a vtable");

#endif // DRIVING_PHYSICS_PHYSICSNAMESPACE_H_
