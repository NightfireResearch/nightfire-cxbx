#ifndef DRIVING_PLATFORM_XBOXXAPI_H_
#define DRIVING_PLATFORM_XBOXXAPI_H_

// XAPI - files, events, threads, sleeping, the heap, contiguous memory, time - replaced by the host's Win32 in a
// standalone run. See XboxXapi.cpp. Call before the game's startup thread runs: the heap entry points have to be
// ours before the process heap is made.
void Inject_XboxXapi(void);

#endif // DRIVING_PLATFORM_XBOXXAPI_H_
