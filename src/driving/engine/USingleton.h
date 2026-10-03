#ifndef DRIVING_ENGINE_USINGLETON_H_
#define DRIVING_ENGINE_USINGLETON_H_

#include <stddef.h>
#include <stdint.h>

#include "CoreContainers.h"   // GameVector

// ---------------------------------------------------------------------------------------------------------------
// USingleton: the base of the renderer's and the game's managers (the fog, the light, state, texture context and
// decal managers, the attribute system, ...), and USingletonManager, which keeps a list of them so they can all be
// reset when a track restarts and killed when the game ends. See USingleton.cpp.
// ---------------------------------------------------------------------------------------------------------------

class USingleton;

// The first three slots of a singleton's vtable
struct USingletonVtable {
    void *slot0;                                            // the deleting destructor
    void (__fastcall *reset)(USingleton *singleton, int);   // +0x04
    void (__fastcall *kill)(USingleton *singleton, int);    // +0x08
};

// The base class: only its vtable pointer (each manager's own fields follow it).
class USingleton {
public:
    const USingletonVtable *vtable;

    void Destruct();   // 0x0007d780: back to the base class's vtable (0x0018beb0)
};

class USingletonManager {
public:
    GameVector<USingleton *> singletons;   // +0x00

    void ResetAll();                       // 0x0011b7a0
    void KillAll();                        // 0x0011b7d0, and frees the list
    void Register(USingleton *singleton);  // 0x0011bb50
};
static_assert(sizeof(USingletonManager) == 0x10, "the singleton manager is a vector");

#endif // DRIVING_ENGINE_USINGLETON_H_
