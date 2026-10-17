#ifndef MEMORY_H_
#define MEMORY_H_

#include <stddef.h>
#include <stdint.h>

#include "actionhelpers.h"

void Mem_Init(void);
void* Mem_Malloc(size_t size, MallocFlags flags, uint32_t alignment);
void Mem_Free(void **ptr);
void Mem_Shrink(void **ptr, uint numBytes);
void* allocateXboxSpecialMemory(int size, uint32_t alignment);   // Mem_Malloc of type 0x1204
void FreeMemory(void *data);                                      // Mem_Free of a pointer that may be null
void Mem_PrintAllInfo(void);
uint32_t Mem_SetMallocMethod(uint32_t method);

void* Mem_Info(void);

// Mem_Init clears all 0x1c bytes; parsemap_block_Coll_Data_New adds to [0]
extern uint32_t MemStats[7];

#endif // MEMORY_H_