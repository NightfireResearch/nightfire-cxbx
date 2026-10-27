#ifndef MEMORY_H_
#define MEMORY_H_

#include <stddef.h>
#include <stdint.h>

#include "actionhelpers.h"
#include "../driving/platform/Ptr32.h"   // the heap's words, and its blocks' headers

// A heap block's header. The block's data follows it; the next block follows the data, at size bytes from here.
#pragma pack(push, 1)
typedef struct MemBlock {
    Ptr32<struct MemBlock> prev;    // 0x0 - the previous block; the first block's is itself
    uint size;              // 0x4 - including this header
    ushort flags;           // 0x8 - the MallocFlags it was allocated with (low byte the alignment, high byte the
                            //       type); 4 once free
    uchar slack;            // 0xa - bytes at the end of the block past what was asked for (Mem_Shrink uses it)
    uchar isFree;           // 0xb
} MemBlock;
#pragma pack(pop)
static_assert(sizeof(MemBlock) == 0xc, "MemBlock is 0xc bytes");

// The heap: HeapByteSize bytes at PtrHeap (an address in the game window, Ptr32.h), all of it blocks
extern uint32_t PtrHeap;
extern uint32_t HeapByteSize;

void Mem_Init(void);
void Mem_TakeHeap(void);   // before Mem_Init, the standalone build's (memory.cpp)
void* Mem_Malloc(size_t size, MallocFlags flags, uint32_t alignment);
void Mem_Free(void **ptr);
void Mem_Shrink(void **ptr, uint numBytes);
void* allocateXboxSpecialMemory(int size, uint32_t alignment);   // Mem_Malloc of MALLOC_XBOX
void FreeMemory(void *data);                                      // Mem_Free of a pointer that may be null
void Mem_PrintAllInfo(void);

// How Mem_Malloc picks a free block: tMallocMethod in Ghidra (Malloc_Type_0..3). ResetMap_Load and the loaders
// switch between them.
enum tMallocMethod {
    MALLOC_NEXT_FIT = 0,    // the first big enough block from QuickBlock back to the start, else on to the end
    MALLOC_BEST_FIT = 1,    // the smallest big enough block (also any value above 3)
    MALLOC_LOW_END = 2,     // the first big enough block, allocated from its low end instead of its high end
    MALLOC_LAST_FIT = 3,    // the last big enough block
};
tMallocMethod Mem_SetMallocMethod(tMallocMethod method);

void* Mem_Info(void);

// Mem_Init clears all 0x1c bytes; parsemap_block_Coll_Data_New adds to [0]
extern uint32_t MemStats[7];

#endif // MEMORY_H_
