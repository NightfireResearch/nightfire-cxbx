#ifndef MEMORY_BLOCK_H_
#define MEMORY_BLOCK_H_

// The heap's own parts, for memory.cpp and XboxHeap.cpp: a block's header, the heap's globals and the allocator's
// steps without the Xbox heap's bookkeeping.

#include <stddef.h>
#include <stdint.h>

#include "actionhelpers.h"
#include "../driving/platform/Ptr32.h"

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

#define MEM_FREE_FLAGS 4

extern uint32_t PtrHeap;
extern uint32_t HeapByteSize;
extern uint32_t MallocMethod;
extern uint32_t QuickBlock;

// The end of the walk over the blocks: every walk stops at the last header-sized slot of the heap
static inline MemBlock* Mem_HeapStart(void) { return PtrFrom32<MemBlock>(PtrHeap); }
static inline MemBlock* Mem_HeapEnd(void) { return PtrFrom32<MemBlock>(PtrHeap + HeapByteSize - (uint32_t)sizeof(MemBlock)); }
static inline MemBlock* Mem_NextBlock(MemBlock *block) { return (MemBlock *)((char *)block + block->size); }

// Mem_Malloc, Mem_Free and Mem_Shrink on the heap PtrHeap names, without the Xbox heap's bookkeeping
void* Mem_Allocate(size_t size, MallocFlags flags, uint32_t alignment);
void Mem_Release(void **ptr);
void Mem_ShrinkBlock(void **ptr, uint numBytes);

#endif // MEMORY_BLOCK_H_
