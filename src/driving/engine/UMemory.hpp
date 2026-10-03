#pragma once

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// The game's memory manager (UMemory, 0x00114400-0x00114a50): a thin layer over EA's block allocator (the MEM_
// functions, platform/RealMemory.cpp) and a set of fixed-size pools for small objects. See UMemory.cpp.
//
// Every allocation goes through a table of the allocator's functions (MemoryFunctions below); UMemory::Init points
// it at the MEM_ functions and UMemory::Shutdown at a table whose allocator answers NULL. Every block handed out
// is zero-filled over its whole size, and every block given back is zero-filled before it goes.
// ---------------------------------------------------------------------------------------------------------------

// The allocator's functions, as a table of twelve (0x30 bytes). Three of the game's: before UMemory::Init
// (0x001d4834), the MEM_ functions (0x001d4894) and after UMemory::Shutdown (0x001d4864). UMemory calls only
// alloc, free and size; the other slots are named by what the MEM table holds.
struct MemoryFunctions {
    void *unknown00;                                              // +0x00
    void *(*alloc)(const char *name, int size, unsigned flags);   // +0x04 MEM_allocz; flags: the class number
    bool (*free)(void *block);                                    // +0x08 MEM_free
    void *(*resize)(void *block, int size);                       // +0x0c MEM_resize
    void *(*blockOf)(void *pointer);                              // +0x10 the block holding a pointer (0x001500a0)
    void *unknown14;                                              // +0x14
    size_t (*size)(void *block);                                  // +0x18 MEM_size
    void *unknown1c;                                              // +0x1c
    unsigned short (*type)(void *block);                          // +0x20 MEM_type
    int (*largestUnused)(unsigned flags);                         // +0x24 MEM_largestunused
    int (*totalUnused)(unsigned flags);                           // +0x28 MEM_totalunused
    bool (*validate)();                                           // +0x2c MEM_validate
};
static_assert(sizeof(MemoryFunctions) == 0x30, "the allocator's function table is twelve slots");

// A link in the fixed-size pools: both the 0x4080-byte blocks they carve and the entries carved from them keep
// their next at +4 (an entry's first word is left as the zero fill made it).
struct FastLink {
    uint32_t unknown00;
    FastLink *next;
};

// The pools (0x108 bytes; the game's own at 0x00242eb8): the blocks not yet carved, the blocks carved, and one free
// list per size, in steps of 16 bytes up to 0x400.
struct FastPool {
    FastLink *freeBlocks;     // +0x00
    FastLink *usedBlocks;     // +0x04
    FastLink *buckets[64];    // +0x08, buckets[n]: entries of (n + 1) * 16 bytes
};
static_assert(sizeof(FastPool) == 0x108, "the pools are 0x108 bytes");

class UMemory {
public:
    // Brings the memory manager up (0x00114880): a "RAM" class 0 over `bytes` at `base` (or over contiguous memory
    // of its own when `base` is NULL), the object heap class of `objectHeapBytes` (none when 0), the allocator
    // table, and EA's PRINT channels. Does nothing a second time.
    static void Init(unsigned int bytes, unsigned int objectHeapBytes, void *base);

    // Takes it down again (0x001149f0): the object heap, class 0, the pools, and the table that answers NULL.
    static void Shutdown();

    // A new memory class of `bytes` of contiguous memory (0x00114400); its number, or -1. `name` is not used: the
    // class is named "". `locked` gives it a mutex; `alignment` is the class's alignment.
    static int NewClass(unsigned int bytes, const char *name, bool locked, unsigned int alignment);

    // Forgets class `number` and frees its memory (0x001149a0); the object heap's class takes the pools with it.
    static void DeleteClass(int number);

    // A block of `size` bytes, zero-filled, labelled `name` for the allocator's own accounting. `flags` is passed
    // through to the allocator (the class number); every caller seen passes 0.
    static void* Alloc(unsigned int size, unsigned int flags, const char *name);

    // Zero-fills the block and gives it back.
    static void Free(void *block);

    // A block from the fixed-size pools, labelled `name` for the allocator's accounting (0x00114750); over 0x400
    // bytes, from the object heap instead.
    static void* FastAlloc(unsigned int size, const char *name);

    // Gives back a block from the fixed-size pools, of the size it was allocated with (the sized operator delete
    // every class's deleting destructor calls).
    static void FastFree(void *block, unsigned int size);

    // 0x0001cba0: a second entry to FastFree (a jump to it).
    static void FastFreeThunk(void *block, unsigned int size);

    // The allocator's size of a block (0x001143f0, a jump through the table; Ghidra: dummyGetNullValue).
    static size_t Size(void *block);

    // The pools' helpers (unnamed in either build)
    static void AddFastBlocks(unsigned int count);        // 0x001144e0
    static void ReleaseFastBlocks();                      // 0x00114540
    static void CarveFastBlock(unsigned int bucket);      // 0x001145b0

    // 0x00114840: an allocation that first makes sure the application's memory is configured.
    static void* ConfiguredAlloc(const char *name, int size, unsigned flags);
};

// EA's REAL library's allocator hooks, which Bond_StartUpSystem installs (0x00114630, 0x00114670).
void* UMemoryREALAllocCallback(const char *name, int size, unsigned flags);
bool UMemoryREALFreeCallback(void *block);

// The global operator new, delete and new[] (Ghidra: __builtin_new, __builtin_delete, __builtin_vec_new).
void* OperatorNew(unsigned int size);         // 0x001146a0
void OperatorDelete(void *block);             // 0x001146e0
void* OperatorNewArray(unsigned int size);    // 0x00114710
