#include <string.h>
#include <stdio.h>
#include "actionhelpers.h"
#include <stdlib.h>

#define Addr_MemStats 0x00223a60
#define Addr_PtrHeap 0x00223a80
#define Addr_HeapByteSize 0x00223a88

#define PtrHeap U32_AT(Addr_PtrHeap)
#define PtrHeapEnd U32_AT(0x00223a84)
#define HeapByteSize U32_AT(Addr_HeapByteSize)
#define MallocMethod U32_AT(0x00223a7c)
#define QuickBlock U32_AT(0x002237d8)
#define MallocSize U32_AT(0x00223a8c)

// AUTOGEN
void* allocateAligned0x1000(int a);

// 49MB of heap allocation from the Xbox kernel, then using an internal allocator
// This is very similar to what Halo does
#define HEAP_SIZE (40 * 1024 * 1024) // Reduced from 49MB to 40MB to free up video memory for resolution increase.

// Only called from Mem_Init, no need to inject
void psiMem_Init(uint *pMem_out, uint *size_out) {
    
    *size_out = HEAP_SIZE;
    void *mem = allocateAligned0x1000(HEAP_SIZE + 0x1000);

    NF_ASSERT(mem != NULL, "Could not allocate heap memory");

    // Align to 4KB boundary
    *pMem_out = (int)mem + 0xfffU & 0xfffff000;

}

// AUTOINJECT
void Mem_Init(void) {

    printf("Mem_Init\n");

    void *puVar1;

    memset((void*)Addr_MemStats, 0, 0x1c);

    MallocMethod = 1;

    if (PtrHeap == NULL) {
        printf("No heap yet, getting config...\n");
        psiMem_Init((uint *)Addr_PtrHeap, (uint*)Addr_HeapByteSize);
    }

    printf("Mem_Init: Heap: 0x%08x, size: 0x%08x\n", PtrHeap, HeapByteSize);

    PtrHeapEnd = (HeapByteSize + PtrHeap);

    memset((void*)PtrHeap, 0x98, HeapByteSize);

    puVar1 = (void*)PtrHeap;
    *(uint32_t *)((int)PtrHeap + 4) = HeapByteSize - 0xc;
    *(uint8_t *)((int)puVar1 + 11) = 1;
    *(void**)puVar1 = (void*)puVar1;
    *(uint16_t *)((int)puVar1 + 8) = 4;
    QuickBlock = PtrHeap;
    MallocSize = 0;

}

// Not auto generated or injected - we call the original allocator in some cases, but if we inject, we end up calling ourself
void* Mem_Malloc(size_t size, MallocFlags flags, uint32_t unknownMaybeAlignment) {

    //printf("Allocating %i bytes of type %02x\n", size, flags);

    // If it's type Xbox, must be allocated in the first 64MB - video memory must be in this region

    if(flags & 0xFF00 == 0x1200) { // Xbox memory type
        return reinterpret_cast<void * (*)(uint, MallocFlags, uint)>(0x00070ae0)(size, flags, unknownMaybeAlignment);
    }

    // TODO: The game does NOT free this memory, it just assumes the entire heap is wiped. So this results in a memory leak
    return malloc(size);
    
}

// AUTOGEN
void Mem_Free(void **ptr);

// AUTOGEN
void Mem_Shrink(void **param_1,uint param_2);
