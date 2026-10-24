#ifndef DRIVING_DEVTOOLS_CRUNTIMESHADOWA_H_
#define DRIVING_DEVTOOLS_CRUNTIMESHADOWA_H_

// NIGHTFIRE_CRUNTIMESHADOWA=1: the C library half of the game's C runtime (src/driving/platform/CRuntime*.cpp)
// against the originals. Two parts:
//  - CRuntimeShadowA_RunHeap: malloc, free, calloc, realloc and _msize on a heap lent for the test, compared by the
//    heap's state. Call it at injection time, before the game's startup makes the process heap and before any other
//    thread can allocate.
//  - CRuntimeShadowA_Run: printf, scanf, atof, strtod, atol, strtol, qsort, rand on two threads, atexit, the
//    character classes and the x87 helpers. Needs the runtime's initialisers run (the float conversion pointers) and a
//    mission's archives on the disc: call it from the first Teleport_Tick.
void CRuntimeShadowA_RunHeap(void);
void CRuntimeShadowA_Run(void);

#endif // DRIVING_DEVTOOLS_CRUNTIMESHADOWA_H_
