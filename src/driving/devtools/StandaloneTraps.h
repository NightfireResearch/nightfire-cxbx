#ifndef DRIVING_DEVTOOLS_STANDALONETRAPS_H_
#define DRIVING_DEVTOOLS_STANDALONETRAPS_H_

// Test modes for the standalone build: what of Driving.xbe our code still executes or touches (StandaloneTraps.cpp,
// docs/driving/standalone.md). Off unless their variables are set.
//
//   NIGHTFIRE_TRAP_ORIGINAL=1   every instruction byte of the original code that no patch wrote becomes int3
//                          =2   the patches over function entries too (all but the entry point the loader calls)
//   NIGHTFIRE_TRAP_DATA=1       from the first simulation tick, every distinct (code, data) access to the image's
//                               .rdata/.data/.bss is logged to NIGHTFIRE_TRAP_DATA_FILE (trap_data.txt)
//                          =2   the same from the end of the injection, so the startup is logged too
//   NIGHTFIRE_HEAP_SHIFT=<bytes> the process heap, contiguous memory and anything else allocated after the
//                               injection start at other addresses

// First thing in Inject(): copies the original code before anything patches it (TRAP_ORIGINAL), and moves the
// allocators' starting point (HEAP_SHIFT).
void StandaloneTraps_BeforeInject(void);

// Last thing in Inject(): the int3 fill, and TRAP_DATA=2.
void StandaloneTraps_AfterInject(void);

// The first simulation tick (devtools/Teleport.cpp): TRAP_DATA=1.
void StandaloneTraps_FirstTick(void);

// Right after the process heap is made (platform/XboxStartup.cpp): HEAP_SHIFT's offset inside the heap.
void StandaloneTraps_HeapCreated(void *heap);

#endif // DRIVING_DEVTOOLS_STANDALONETRAPS_H_
