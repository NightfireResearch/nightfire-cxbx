#pragma once

// The game's memory manager (Ghidra: UMemory). Only the two entry points our code calls so far, each calling the
// original: both go through the allocator object's vtable at UMemory_vtable, which the game sets up itself.
class UMemory {
public:
    // A block of `size` bytes, zero-filled, labelled `name` for the allocator's own accounting. `flags` is passed
    // through to the allocator; every caller seen passes 0.
    // AUTOGEN
    static void *Alloc(unsigned int size, unsigned int flags, const char *name);

    // Zero-fills the block and gives it back.
    // AUTOGEN
    static void Free(void *block);

    // A block from the fixed-size pools, labelled `name` for the allocator's accounting (0x00114750).
    // AUTOGEN
    static void *FastAlloc(unsigned int size, const char *name);

    // Gives back a block from the fixed-size pools, of the size it was allocated with (the sized operator delete
    // every class's deleting destructor calls).
    // AUTOGEN
    static void FastFree(void *block, unsigned int size);
};

// The game's operator new[] and operator delete[] (0x00114710, 0x001146e0; both __cdecl), for arrays the
// originals allocate that way and free elsewhere. A class array's MSVC count cookie is the caller's to write.
typedef void *(__cdecl *GameArrayNewFn)(unsigned int size);
typedef void (__cdecl *GameArrayDeleteFn)(void *block);
#define GameArrayNew ((GameArrayNewFn)0x00114710)
#define GameArrayDelete ((GameArrayDeleteFn)0x001146e0)
