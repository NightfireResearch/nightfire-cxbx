#include "UMemory.hpp"

#include "GameLoop.h"                  // ApplicationMemoryHeapConfig
#include "../../helpers.h"
#include "../platform/RealMemory.h"   // MEMCLASS_init, MEMCLASS_restore, MEMCLASS_findfree
#include "../platform/RealPrint.h"    // MEM_fill, MEM_clear, PRINT_*, REAL_exit
#include "../platform/XboxXapi.h"     // Xbox_MmFreeContiguousMemory

// ---------------------------------------------------------------------------------------------------------------
// UMemory, the game's memory manager (0x00114400-0x00114a50), ported from the listing. Everything the game
// allocates comes through here, so the sizes, names, class numbers and the order of the calls into the allocator
// are the originals', as are the zero fills (the whole block, by the allocator's own idea of its size, on the way
// out and on the way back): lockstep runs depend on the heap being laid out exactly as the original lays it out.
//
// Small objects (up to 0x400 bytes) come from 64 free lists, one per 16-byte size. A list that runs dry is filled
// by carving a 0x4080-byte block (0x4000 and room to align) into entries of its size, aligned to the size or to
// 0x80, whichever is smaller. Blocks are allocated from the object heap's class ("FastBlock") as needed, and only
// given back when that class is deleted. Entries go back on their list zero-filled; the block is never freed.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address

#define XPhysicalAlloc ((void *(__stdcall *)(uint32_t size, uint32_t highest, uint32_t alignment, uint32_t protect))0x0010e7e9)   // ours (XboxXapi.cpp), through its jump

// ---- globals

#define Functions (*(MemoryFunctions **)0x001d48c4)   // the allocator's table in use
#define Pools (*(FastPool **)0x001d48c8)              // the pools in use (DefaultPools until Init)
#define DefaultPools (*(FastPool *)0x00242eb8)
#define MemFunctions (*(MemoryFunctions *)0x001d4894) // the MEM_ functions
#define DeadFunctions (*(MemoryFunctions *)0x001d4864) // after Shutdown: alloc answers NULL
#define Initialised BOOL8_AT(0x00242fc0)
#define PageSize U32_AT(0x00242fc4)                   // 0x1000, set by Init and NewClass; nothing reads it
#define ObjectHeapClass I32_AT(0x00242fc8)            // the class operator new and big FastAllocs use
#define ClassMemory ((void **)0x00242fd0)             // [64]: each class's contiguous memory
#define OwnsRam BOOL8_AT(0x001d4830)                  // class 0's memory is Init's own (and is freed with it)

// XPhysicalAlloc's arguments for a class's memory: anywhere, 0x80-aligned, PAGE_READWRITE.
static const uint32_t kAnyAddress = 0xffffffff;
static const uint32_t kClassAlignment = 0x80;
static const uint32_t kPageReadWrite = 4;

static const unsigned int kFastLimit = 0x400;         // the largest size the pools serve
static const unsigned int kFastBlockBytes = 0x4080;   // a block the pools carve
static const unsigned int kFastAlignmentLimit = 0x80;

// A pool entry's size in bytes, and the list it belongs on
static unsigned int BucketOf(unsigned int size) { return (size - 1) >> 4; }
static unsigned int BucketBytes(unsigned int bucket) { return (bucket + 1) << 4; }

// The allocator, through its table, as every UMemory function does: allocate and zero-fill over the block's size;
// zero-fill and free.
static void *AllocZeroed(const char *name, int size, unsigned flags) {
    void *block = Functions->alloc(name, size, flags);
    MEM_fill(block, 0, (int)Functions->size(block));
    return block;
}

static void FreeZeroed(void *block) {
    MEM_fill(block, 0, (int)Functions->size(block));
    Functions->free(block);
}

// FUNC_AT(0x00114400)
int UMemory::NewClass(unsigned int bytes, const char *name, bool locked, unsigned int alignment) {
    (void)name;
    int number = MEMCLASS_findfree();
    if (PageSize == 0)
        PageSize = 0x1000;
    void *memory = XPhysicalAlloc(bytes, kAnyAddress, kClassAlignment, kPageReadWrite);
    ClassMemory[number] = memory;
    if (memory == NULL)
        return -1;
    MEMCLASS_init(number, "", memory, bytes, alignment, 0x80, 0, 0, 0, locked);
    return number;
}

// FUNC_AT(0x00114470)
void* UMemory::Alloc(unsigned int size, unsigned int flags, const char *name) {
    return AllocZeroed(name, size, flags);
}

// FUNC_AT(0x001144b0)
void UMemory::Free(void *block) {
    FreeZeroed(block);
}

// FUNC_AT(0x001144e0)
void UMemory::AddFastBlocks(unsigned int count) {
    for (; count != 0; count--) {
        FastLink *block = (FastLink *)AllocZeroed("FastBlock", kFastBlockBytes, 0);
        block->next = Pools->freeBlocks;
        Pools->freeBlocks = block;
    }
}

// Every carved block goes back on the uncarved list, and every block on it back to the allocator. The two list
// heads are left pointing at freed memory, as the original leaves them (only DeleteClass calls this, and
// Shutdown then clears the pools).
// FUNC_AT(0x00114540)
void UMemory::ReleaseFastBlocks() {
    FastPool *pool = Pools;
    for (FastLink *block = pool->usedBlocks; block != NULL;) {
        FastLink *next = block->next;
        block->next = pool->freeBlocks;
        pool->freeBlocks = block;
        block = next;
    }
    for (FastLink *block = pool->freeBlocks; block != NULL;) {
        FastLink *next = block->next;   // before the fill clears it
        FreeZeroed(block);
        block = next;
    }
    for (int i = 0; i < 64; i++)
        pool->buckets[i] = NULL;
}

// FUNC_AT(0x001145b0)
void UMemory::CarveFastBlock(unsigned int bucket) {
    if (Pools->freeBlocks == NULL)
        AddFastBlocks(1);
    FastPool *pool = Pools;
    FastLink *block = pool->freeBlocks;
    pool->freeBlocks = block->next;
    block->next = pool->usedBlocks;
    pool->usedBlocks = block;

    unsigned int size = BucketBytes(bucket);
    uint8_t *end = (uint8_t *)block + kFastBlockBytes;
    unsigned int alignment = size < kFastAlignmentLimit ? size : kFastAlignmentLimit;
    uint8_t *entry = (uint8_t *)(((uintptr_t)block + alignment + 7) / alignment * alignment);
    while ((uintptr_t)(end - entry) >= size) {
        FastLink *link = (FastLink *)entry;
        link->next = pool->buckets[bucket];
        pool->buckets[bucket] = link;
        entry += size;
    }
}

// FUNC_AT(0x00114630)
void* UMemoryREALAllocCallback(const char *name, int size, unsigned flags) {
    return AllocZeroed(name, size, flags);
}

// FUNC_AT(0x00114670)
bool UMemoryREALFreeCallback(void *block) {
    FreeZeroed(block);
    return true;
}

// FUNC_AT(0x001146a0)
void* OperatorNew(unsigned int size) {
    if (size == 0)
        size = 1;
    return AllocZeroed("new", size, ObjectHeapClass);
}

// FUNC_AT(0x001146e0)
void OperatorDelete(void *block) {
    if (block != NULL)
        FreeZeroed(block);
}

// FUNC_AT(0x00114710)
void* OperatorNewArray(unsigned int size) {
    if (size == 0)
        size = 1;
    return AllocZeroed("new[]", size, 0);
}

// FUNC_AT(0x00114750)
void* UMemory::FastAlloc(unsigned int size, const char *name) {
    if (size > kFastLimit)
        return AllocZeroed(name, size, ObjectHeapClass);
    unsigned int bucket = BucketOf(size);
    if (Pools->buckets[bucket] == NULL)
        CarveFastBlock(bucket);
    FastPool *pool = Pools;
    FastLink *entry = pool->buckets[bucket];
    pool->buckets[bucket] = entry->next;
    MEM_fill(entry, 0, BucketBytes(bucket));
    return entry;
}

// FUNC_AT(0x001147d0)
void UMemory::FastFree(void *block, unsigned int size) {
    if (size > kFastLimit) {
        FreeZeroed(block);
        return;
    }
    unsigned int bucket = BucketOf(size);
    FastPool *pool = Pools;
    MEM_fill(block, 0, BucketBytes(bucket));
    FastLink *entry = (FastLink *)block;
    entry->next = pool->buckets[bucket];
    pool->buckets[bucket] = entry;
}

// FUNC_AT(0x0001cba0)
void UMemory::FastFreeThunk(void *block, unsigned int size) {
    FastFree(block, size);
}

// FUNC_AT(0x001143f0)
size_t UMemory::Size(void *block) {
    return Functions->size(block);
}

// FUNC_AT(0x00114840)
void* UMemory::ConfiguredAlloc(const char *name, int size, unsigned flags) {
    void *block = NULL;
    if (ApplicationMemoryHeapConfig())
        block = AllocZeroed(name, size, flags);
    return block;
}

// FUNC_AT(0x00114880)
void UMemory::Init(unsigned int bytes, unsigned int objectHeapBytes, void *base) {
    if (ClassMemory[0] != NULL)
        return;
    PageSize = 0x1000;
    Initialised = 1;
    Functions = &MemFunctions;
    MEM_fill(Pools, 0, sizeof(FastPool));
    void *memory = base;
    if (memory == NULL) {
        if (PageSize == 0)   // as the original: it was set just above
            PageSize = 0x1000;
        memory = XPhysicalAlloc(bytes, kAnyAddress, kClassAlignment, kPageReadWrite);
        OwnsRam = 1;
    } else {
        OwnsRam = 0;
    }
    ClassMemory[0] = memory;
    if (memory == NULL) {
        REAL_exit();
    } else {
        MEMCLASS_init(0, "RAM", memory, bytes, 0x80, 0x80, 0, 0, 0, 1);
        if (objectHeapBytes != 0) {
            ObjectHeapClass = NewClass(objectHeapBytes, "objectHeap", true, 0x10);
            if (ObjectHeapClass == -1)
                REAL_exit();
        } else {
            ObjectHeapClass = 0;
        }
    }
    PRINT_init();
    PRINT_setdevicestate(1, 0);
    PRINT_setdevicestate(2, 1);
    PRINT_setchannelname(0xe, "NFSPrintf");
    PRINT_setchannelstate(0, 1);
    PRINT_setchannelstate(0xe, 1);
}

// FUNC_AT(0x001149a0)
void UMemory::DeleteClass(int number) {
    if (number == ObjectHeapClass)
        ReleaseFastBlocks();
    MEMCLASS_restore(number);
    if (number != 0 || OwnsRam)
        Xbox_MmFreeContiguousMemory(ClassMemory[number]);
    ClassMemory[number] = NULL;
}

// FUNC_AT(0x001149f0)
void UMemory::Shutdown() {
    if (ClassMemory[0] == NULL || !Initialised)
        return;
    PRINT_restore();
    DeleteClass(ObjectHeapClass);
    ClassMemory[0] = NULL;
    Pools = &DefaultPools;
    Functions = &DeadFunctions;
    ClassMemory[ObjectHeapClass] = NULL;
    MEM_clear(&DefaultPools, sizeof(FastPool));
}
