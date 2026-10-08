#ifndef DRIVING_ANIM_MANAGER_H_
#define DRIVING_ANIM_MANAGER_H_

// ActManager: the actor animation system. StartUp makes it and its databases (skeletons, textures, models,
// animations, weapons, events) and brings EAGLAnim up; ShutDown takes them down again. SetIRMode swaps the scene's
// lights for the infrared view's. Also three small helpers the linker placed among the Act* classes. See
// Manager.cpp.

#include <stddef.h>
#include <stdint.h>

#include "Character.h"                 // CharacterDrawOptions
#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../../helpers.h"              // BOOL8_AT
#include "../engine/UGroup.h"

class ActTextureDatabase;
class ActModelDatabase;
class ActSkeletonDatabase;
class ActAnimationDatabase;
class ActWeaponDatabase;
class ActEvents;

class ActManager {                    // 0x80 (operator new)
public:
    ActTextureDatabase *textures;     // +0x00
    ActModelDatabase *models;         // +0x04
    ActAnimationDatabase *animations; // +0x08
    ActSkeletonDatabase *skeletons;   // +0x0c
    ActWeaponDatabase *weapons;       // +0x10
    ActEvents *events;                // +0x14
    float frameTime;                  // +0x18 StartUp's argument
    uint32_t unknown1c;
    CharacterDrawOptions drawOptions; // +0x20 what the characters' draws use; from its +0x20 on, EAGL's
                                      //       "GAME::CharacterOptions"

    static void StartUp(float frameTime);                                       // 0x000172c0
    static void ShutDown();                                                     // 0x000173d0
    // Infrared lighting on or off: the scene's light block swapped with a copy whose four colours are white.
    static void SetIRMode(bool on);                                             // 0x00016ed0

    void Init(float frameTime);                                                 // 0x00017060 ActManagerInit
    void Destruct();                                                            // 0x00017200

    // The resolver the actors' object files are loaded with: ResolveEAGLReferences. Its callers pass no `this`.
    static void* GetSymbolResolver();                                           // 0x00016ec0
    // Answers the names "event.*" with their event ids (a symbol callback).
    static int SymbolResolver(const char *name, bool *found);                   // 0x00017010
};
static_assert(offsetof(ActManager, drawOptions) == 0x20, "the character options are at +0x20");
static_assert(sizeof(ActManager) == 0x80, "an ActManager is 0x80 bytes");

#define TheActManager (*(ActManager **)0x001dd9cc)
#define IRModeOn BOOL8_AT(0x001dd9dd)       // SetIRMode's

// ---- helpers the linker placed here

// A Coord4 of a Coord3 and w, returned by value (invented name).
Coord4* MakeCoord4(Coord4 *result, const Coord3 *v, float w);                   // 0x0001a9f0

// GroupLocateTag on a tag whose low 16 bits are `index`, unless index is -1 (invented names; a UGroup method).
struct IndexedTagGroup : UGroup {
    UGroup* LocateTag(uint32_t tag, int index);                                 // 0x0001aa40
};

// A getter of the field at +0x2c of the object RVehicle::PostLoad calls it on (invented names).
struct Field2cGetter {
    uint8_t unknown00[0x2c];
    void *unknown2c;

    void* Get();                                                                // 0x0001aa60
};

#endif // DRIVING_ANIM_MANAGER_H_
