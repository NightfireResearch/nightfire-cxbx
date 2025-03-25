#ifndef MEMORY_H_
#define MEMORY_H_

#include <stddef.h>
#include <stdint.h>

#include "actionhelpers.h"

void Mem_Init(void);
void* Mem_Malloc(size_t size, MallocFlags flags, uint32_t unknownMaybeAlignment);

#endif // MEMORY_H_