#include <stdio.h>
#include <cstdlib>
#include <cstdint>
#include <cstring>

// AUTOGEN
void* MEM_allocz(char* type, size_t numBytes, uint32_t maybeAlign);
// AUTOGEN
bool MEM_free(void* pData);

// AUTOINJECT
void* UMemoryREALAllocCallback(char* type, size_t numBytes, uint32_t maybeAlign) {
      // Original code does:
    // return UMemory::Alloc(numBytes, unknown, type);
    // This passes off to MEM_allocz via the vtable (never changes?), and zeros it
    //printf("Allocating %i bytes for %s, maybeAlign %i\n", numBytes, type, maybeAlign);
    void* data = MEM_allocz(type, numBytes, maybeAlign);
    memset(data, 0, numBytes);
    return data;
}

// AUTOINJECT
bool UMemoryREALFreeCallback(void* pData) {
    // UMemory::Free(pData);
    // Passes off to MEM_free via the vtable (never changes?)
    MEM_free(pData);
    return true;
}
