#ifndef ACTION_DEVTOOLS_MEMTRACE_H_
#define ACTION_DEVTOOLS_MEMTRACE_H_

// The game heap's use over a run, every 300 frames: bytes in use, the largest free block, the block count and the
// bytes in use by allocation type. NIGHTFIRE_MEM_TRACE=<file> turns it on.
void MemTrace_Frame(void);

#endif // ACTION_DEVTOOLS_MEMTRACE_H_
