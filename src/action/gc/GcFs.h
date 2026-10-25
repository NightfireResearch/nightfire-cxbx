#ifndef GC_GCFS_H_
#define GC_GCFS_H_

// GcFs.cpp's own calls beside FS.h's. Built only with NIGHTFIRE_ACTION_DATA_GC.

#include <stdint.h>

// The next whole file FS_AllocateAndLoadBlocking loads goes into this buffer, if it fits; else memory of its own
void GcFs_LoadNextInto(void *buffer, uint32_t bytes);

// Where the last whole file went, and the bytes there it may use (the buffer's whole size when it went into one)
void *GcFs_LastLoad(uint32_t *bytes);

// How many level bundles (0x0700xxxx) have been loaded: one more means the level's heap has started again
uint32_t GcFs_LevelLoads(void);

#endif // GC_GCFS_H_
