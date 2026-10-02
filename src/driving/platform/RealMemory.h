#ifndef DRIVING_PLATFORM_REALMEMORY_H_
#define DRIVING_PLATFORM_REALMEMORY_H_

// EA's block allocator, the MEM_ functions of its portable library, as the driving engine has it. See RealMemory.cpp.

#include <stddef.h>

typedef int (*MemOutOfMemoryFn)(int unused, int size, unsigned flags);   // 1: try the allocation again

int MEM_initblock(void *block, const char *name, int size, int tail, unsigned flags, void *previous, void *next);
int MEM_tailsize(const char *name, int flags);
size_t MEM_size(void *data);
unsigned short MEM_type(void *data);
void* MEM_allocalign(const char *label, int size, int alignment, int offset, unsigned flags);
void* MEM_alloc(const char *label, int size, unsigned flags);
void* MEM_allocz(const char *label, int size, unsigned flags);
bool MEM_free(void *data);
bool MEM_free_copy(void *data);
void* MEM_resize(void *data, int size);
int MEM_largestunused(unsigned flags);
int MEM_totalunused(unsigned flags);
char checksentinel(void *pointer);
bool MEM_validate();

int MEMCLASS_init(unsigned number, const char *name, void *base, int size, int alignment, int otherAlignment,
                  int tail, char endMarkers, char option1000, char locked);
int MEMCLASS_restore(unsigned number);
int MEMCLASS_findfree();

#endif // DRIVING_PLATFORM_REALMEMORY_H_
