#ifndef DRIVING_DEVTOOLS_COREUTILSHADOW_H_
#define DRIVING_DEVTOOLS_COREUTILSHADOW_H_

// NIGHTFIRE_COREUTILSHADOW=1, at injection time: the engine core's memory manager (engine/UMemory.cpp), reference
// counters (URefCounter.cpp), containers and singletons (CoreContainers.cpp, USingleton.cpp) side by side with the
// originals in a scratch heap, and the data groups (UGroup.cpp) on every CARP, gallery and locale file on the disc.
// See CoreUtilShadow.cpp.
void CoreUtilShadow_Run(void);

#endif // DRIVING_DEVTOOLS_COREUTILSHADOW_H_
