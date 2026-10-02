#ifndef DRIVING_PLATFORM_XBOXXAPI_H_
#define DRIVING_PLATFORM_XBOXXAPI_H_

// XAPI - files, events, threads, sleeping, the heap, contiguous memory, time - replaced by the host's Win32 in a
// standalone run. See XboxXapi.cpp. Call before the game's startup thread runs: the heap entry points have to be
// ours before the process heap is made.
void Inject_XboxXapi(void);

#include <windows.h>

// CreateFileA and DeleteFileA on an Xbox path ("D:\driving\..."), resolved onto the host. FILESYS calls them.
HANDLE __stdcall Xbox_CreateFileA(const char *path, DWORD access, DWORD share, SECURITY_ATTRIBUTES *security,
                                  DWORD disposition, DWORD flags, HANDLE templateFile);
BOOL __stdcall Xbox_DeleteFileA(const char *path);

#endif // DRIVING_PLATFORM_XBOXXAPI_H_
