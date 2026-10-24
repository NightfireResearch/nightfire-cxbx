#ifndef DRIVING_EAGL_GAMECALLBACKS_H_
#define DRIVING_EAGL_GAMECALLBACKS_H_

// The game's side of EAGL's hooks (docs/driving/eagl.md 2.13, 6.1): the allocator pair RRenderer installs and the
// symbol-table namespace RCARPFile::Resolve adds for an object's models. See GameCallbacks.cpp.

#include <stddef.h>
#include <stdint.h>

class DynamicLoader;

// RCARPFile::Resolve's stack object (vtable 0x001913b4) that USymbolTable asks for names under "EAGL".
struct EAGLNamespace {
    const void *vtable;
    DynamicLoader *loader;           // +0x04

    void* NameLookup(const char *name, void *unused);                    // 0x0007aea0
};

void* EAGL_allocator(uint32_t size, const char *name);                   // 0x0007d040
void EAGL_deallocator(void *pointer, uint32_t size);                     // 0x000c5700

#endif // DRIVING_EAGL_GAMECALLBACKS_H_
