#ifndef DRIVING_DEVTOOLS_CRUNTIMESHADOWB_H_
#define DRIVING_DEVTOOLS_CRUNTIMESHADOWB_H_

// NIGHTFIRE_CRUNTIMESHADOWB=1: the C++ half of the game's C runtime (src/driving/platform/CRuntimeStd.cpp) against
// the originals: the library's locks and mutexes, the global locale and the facet list, _Locinfo, ctype's tables,
// ios_base, strstreambuf, istrstream's and ostrstream's destructors, _Stod, std::exception, the throws and the
// array iterators. Call it from the first Teleport_Tick (the runtime's initialisers run, on the main thread).
void CRuntimeShadowB_Run(void);

#endif // DRIVING_DEVTOOLS_CRUNTIMESHADOWB_H_
