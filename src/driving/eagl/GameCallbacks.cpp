#include "GameCallbacks.h"
#include "Loader.h"

// ---------------------------------------------------------------------------------------------------------------
// The game's EAGL hooks. RRenderer's constructor points EAGL's allocator at the game's memory manager through
// Device::SetNewOverride / SetDeleteOverride: EAGL_allocator takes from UMemory with flags 0x100 under EAGL's own
// allocation name, and EAGL_deallocator gives back (the size EAGL passes is not needed). RCARPFile::Resolve adds an
// EAGLNamespace for the object it has just loaded, so that CARP's symbolic references find its models by name.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0007aea0)
void* EAGLNamespace::NameLookup(const char *name, void *unused) {
    (void)unused;
    void *address = NULL;
    if (!loader->GetAddr((const char *)0x001a09e0u, name, &address))   // "Model"
        return NULL;
    return address;
}

// FUNC_AT(0x0007d040)
void* EAGL_allocator(uint32_t size, const char *name) {
    return ((void *(*)(uint32_t, uint32_t, const char *))0x00114470)(size, 0x100, name);   // UMemory::Alloc
}

// FUNC_AT(0x000c5700)
void EAGL_deallocator(void *pointer, uint32_t size) {
    (void)size;
    ((void (*)(void *))0x001144b0)(pointer);   // UMemory::Free
}
