#ifndef ACTION_ENGINE_CRUNTIME_H_
#define ACTION_ENGINE_CRUNTIME_H_

#include <stddef.h>
#include <stdint.h>

#include "../../driving/platform/CRuntime.h"

// The action engine's C runtime. Default.xbe links the same XDK runtime as Driving.xbe, function for function (at
// other addresses), so its port is the driving engine's: src/driving/platform/CRuntimeCore.cpp (the heap, the locks,
// the fatal stop, errno, the character tests), CRuntimeX87.cpp (the _CI functions and the x87 set-up),
// CRuntimeFormat.cpp and CRuntimeFloat.cpp (sprintf), compiled into this engine too, in namespace Crt. CRuntime.cpp
// gives them what is this engine's (its process heap, kernel calls and class table) and holds the two helpers the
// driving engine's port has no need of.

namespace Crt {

void _ftol2();                                                   // 0x000ee3dc: ST0 truncated, in EDX:EAX
void* memcpy(void *destination, const void *source, size_t count);   // 0x000ee4b0

} // namespace Crt

// Points the runtime's entry points the game still calls at ours. Called from Inject().
void Inject_CRuntime(void);

#endif // ACTION_ENGINE_CRUNTIME_H_
