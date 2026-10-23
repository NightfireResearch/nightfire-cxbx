#include "XboxHeap.h"

#include "memory.h"
#include "memoryBlock.h"

#include <stdlib.h>
#include <string.h>

#include <map>

#include "../driving/platform/Host.h"

// The Xbox heap is the game's allocator run on a heap of its own: the heap's globals point at it for each call
static bool Active;
static bool Checking;           // 32-bit, with NIGHTFIRE_XBOX_HEAP_CHECK: each allocation must lie where the heap's does
static uint32_t RealHeap;       // the heap the game allocates from (PtrHeap when Mem_Init ran)
static uint32_t XboxHeapStart;
static uint32_t XboxQuickBlock;

// The game's allocations, by their address, and where each lies on the Xbox heap
struct XboxAllocation {
    uint32_t address;
    uint32_t size;
};
static std::map<uint32_t, XboxAllocation> Allocations;

// For one call of the game's allocator, the heap is the Xbox heap
struct OnXboxHeap {
    uint32_t heap, quick;
    OnXboxHeap() : heap(PtrHeap), quick(QuickBlock) {
        PtrHeap = XboxHeapStart;
        QuickBlock = XboxQuickBlock;
    }
    ~OnXboxHeap() {
        XboxQuickBlock = QuickBlock;
        PtrHeap = heap;
        QuickBlock = quick;
    }
};

static bool InRealHeap(const void *data) {
    return Active && data != NULL && PtrHeap == RealHeap &&
           (uintptr_t)data - (uintptr_t)PtrFrom32<char>(RealHeap) < HeapByteSize;
}

// The allocation data is in, if it is one
static std::map<uint32_t, XboxAllocation>::iterator Containing(const void *data) {
    uint32_t at = PtrTo32(data);
    std::map<uint32_t, XboxAllocation>::iterator it = Allocations.upper_bound(at);
    if (it == Allocations.begin())
        return Allocations.end();
    --it;
    return at - it->first <= it->second.size ? it : Allocations.end();
}

// The Xbox heap's pages: the host's, not the Xbox's contiguous memory, but in the game window (the allocator reaches
// the heap through 32-bit words), so on 64-bit below 4 GB and above where contiguous memory goes
static void *XboxHeapPages(uint32_t size) {
#if UINTPTR_MAX > 0xffffffffu
    return HostAllocatePages(size, 0x10000000, (uintptr_t)1 << 32);
#else
    return HostAllocatePages(size, 0, UINTPTR_MAX);
#endif
}

// As Mem_Init: one free block, every byte 0x98
void XboxHeap_Init(void) {
#if UINTPTR_MAX > 0xffffffffu
    Active = true;
#else
    Active = Checking = getenv("NIGHTFIRE_XBOX_HEAP_CHECK") != NULL;
#endif
    if (!Active)
        return;
    if (XboxHeapStart == 0) {
        void *mem = XboxHeapPages(HeapByteSize);
        NF_ASSERT(mem != NULL, "Could not allocate the Xbox heap");
        XboxHeapStart = PtrTo32(mem);
    }
    RealHeap = PtrHeap;
    memset(PtrFrom32<void>(XboxHeapStart), 0x98, HeapByteSize);
    MemBlock *heap = PtrFrom32<MemBlock>(XboxHeapStart);
    heap->size = HeapByteSize - sizeof(MemBlock);
    heap->isFree = 1;
    heap->prev = heap;
    heap->flags = MEM_FREE_FLAGS;
    XboxQuickBlock = XboxHeapStart;
    Allocations.clear();
}

static void Check(uint32_t real, uint32_t xbox) {
    if (!Checking || xbox - XboxHeapStart == real - RealHeap)
        return;
    printf("The Xbox heap put an allocation at +0x%x, the heap at +0x%x\n", xbox - XboxHeapStart, real - RealHeap);
    NF_ASSERT(false, "the Xbox heap and the heap agree");
}

void XboxHeap_Malloc(void *data, size_t size, size_t xboxSize, MallocFlags flags, uint32_t alignment) {
    if (!InRealHeap(data))
        return;
    void *xbox;
    {
        OnXboxHeap on;
        xbox = Mem_Allocate(xboxSize, flags, alignment);
    }
    NF_ASSERT(xbox != NULL, "the Xbox heap has room for every allocation the heap does");
    XboxAllocation allocation = {PtrTo32(xbox), (uint32_t)size};
    Allocations[PtrTo32(data)] = allocation;
    Check(PtrTo32(data), allocation.address);
}

void XboxHeap_Free(void *data) {
    if (!InRealHeap(data))
        return;
    std::map<uint32_t, XboxAllocation>::iterator it = Allocations.find(PtrTo32(data));
    if (it == Allocations.end())
        return;
    void *xbox = PtrFrom32<void>(it->second.address);
    Allocations.erase(it);
    OnXboxHeap on;
    Mem_Release(&xbox);
}

// Mem_Shrink splits the block into parts, of which it frees one: the same on the Xbox heap, at the same place in the
// block (only the map file's buffer is shrunk, and its bytes are the same on every host), and then the parts still
// allocated on each are paired
void XboxHeap_Shrink(void **ptr, uint numBytes) {
    char *data = (char *)*ptr;
    if (!InRealHeap(data)) {
        Mem_ShrinkBlock(ptr, numBytes);
        return;
    }
    std::map<uint32_t, XboxAllocation>::iterator it = Containing(data);
    NF_ASSERT(it != Allocations.end(), "Mem_Shrink of something the Xbox heap does not have");
    MemBlock *real = PtrFrom32<MemBlock>(it->first) - 1;
    MemBlock *xbox = PtrFrom32<MemBlock>(it->second.address) - 1;
    MemBlock *realEnd = Mem_NextBlock(real);
    MemBlock *xboxEnd = Mem_NextBlock(xbox);
    void *xboxData = PtrFrom32<char>(it->second.address) + (data - PtrFrom32<char>(it->first));
    Allocations.erase(it);

    Mem_ShrinkBlock(ptr, numBytes);
    {
        OnXboxHeap on;
        Mem_ShrinkBlock(&xboxData, numBytes);
    }

    while (real < realEnd && xbox < xboxEnd) {
        if (real->isFree) {
            real = Mem_NextBlock(real);
        } else if (xbox->isFree) {
            xbox = Mem_NextBlock(xbox);
        } else {
            XboxAllocation allocation = {PtrTo32(xbox + 1), real->size - (uint32_t)sizeof(MemBlock) - real->slack};
            Allocations[PtrTo32(real + 1)] = allocation;
            Check(PtrTo32(real + 1), allocation.address);
            real = Mem_NextBlock(real);
            xbox = Mem_NextBlock(xbox);
        }
    }
}

uint32_t Mem_GameAddress(const void *data) {
    if (!Active)
        return PtrTo32(data);
    std::map<uint32_t, XboxAllocation>::iterator it = Containing(data);
    NF_ASSERT(it != Allocations.end(), "Mem_GameAddress of something not in the heap");
    return it->second.address + (PtrTo32(data) - it->first);
}
