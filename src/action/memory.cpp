#include <string.h>
#include <stdio.h>
#include "actionhelpers.h"

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

// Only called from Mem_Init, no need to inject
void psiMem_Init(uint *param_1, uint *param_2) {
    void *pvVar1;
    
    *param_2 = 0x3100000;
    pvVar1 = allocateAligned0x1000(0x3101000);
    *param_1 = (int)pvVar1 + 0xfffU & 0xfffff000;
    return;
}

// AUTOINJECT
void Mem_Init(void) {

    printf("Mem_Init\n");

    void *puVar1;

    memset((void*)Addr_MemStats,0,0x1c);

    MallocMethod = 1;

    if (PtrHeap == 0) {
        printf("No heap yet, getting config...\n");
        psiMem_Init((uint *)Addr_PtrHeap, (uint*)Addr_HeapByteSize);
    }

    printf("Mem_Init: Heap: 0x%08x, size: 0x%08x\n", PtrHeap, HeapByteSize);

    PtrHeapEnd = (HeapByteSize + PtrHeap);

    memset((void*)PtrHeap,0x98,HeapByteSize);

    puVar1 = (void*)PtrHeap;
    *(uint32_t *)((int)PtrHeap + 4) = HeapByteSize - 0xc;
    *(uint8_t *)((int)puVar1 + 11) = 1;
    *(void**)puVar1 = (void*)puVar1;
    *(uint16_t *)((int)puVar1 + 8) = 4;
    QuickBlock = PtrHeap;
    MallocSize = 0;

}

// AUTOGEN
void* Mem_Malloc(size_t size, uint32_t flags, uint32_t unknownMaybeAlignment);

// AUTOGEN
void Mem_Free(void **ptr);

// AUTOGEN
void Mem_Shrink(void **param_1,uint param_2);
