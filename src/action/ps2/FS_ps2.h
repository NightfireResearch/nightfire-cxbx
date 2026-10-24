#ifndef PS2_FS_PS2_H_
#define PS2_FS_PS2_H_

// What FS_ps2.cpp adds to FS.h's calls for the PS2's loader (Ps2Loader.cpp). Built only with
// NIGHTFIRE_ACTION_DATA_PS2.

#include <stdint.h>

// The next whole file FS_AllocateAndLoadBlocking loads goes into buffer, if it fits in bytes, instead of new memory
void Ps2Fs_LoadNextInto(void *buffer, uint32_t bytes);

// The memory the last whole file was loaded into, and how many bytes of it there are to use again
void *Ps2Fs_LastLoad(uint32_t *bytes);

#endif // PS2_FS_PS2_H_
