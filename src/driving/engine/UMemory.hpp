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
};
