#ifndef XBOX_HEAP_H_
#define XBOX_HEAP_H_

// Where the game's allocations would lie on the Xbox. On 64-bit the structures that hold pointers are bigger, so the
// heap's blocks lie elsewhere than the game put them, and the game reads one such address: a drone's breath keeps
// its phase by where the drone lies (Drone_Update). The Xbox heap runs the game's allocator again, on a second heap
// of headers alone with each allocation's Xbox size, and so has the address each allocation would have had.
//
// On 32-bit the addresses are the game's own and the Xbox heap is off; NIGHTFIRE_XBOX_HEAP_CHECK=1 runs it there with
// the real sizes, and checks that it puts every allocation where the heap does.

#include <stddef.h>
#include <stdint.h>

#include "actionhelpers.h"

// Mem_Init, Mem_Malloc, Mem_Free and Mem_Shrink do the same on the Xbox heap
void XboxHeap_Init(void);
void XboxHeap_Malloc(void *data, size_t size, size_t xboxSize, MallocFlags flags, uint32_t alignment);
void XboxHeap_Free(void *data);
void XboxHeap_Shrink(void **ptr, uint numBytes);

// The address an allocation (or a place inside one) would have on the Xbox
uint32_t Mem_GameAddress(const void *data);

#endif // XBOX_HEAP_H_
