#ifndef DRIVING_PLATFORM_XBOXXAPI_H_
#define DRIVING_PLATFORM_XBOXXAPI_H_

// XAPI - files, events, threads, sleeping, the heap, contiguous memory, time - replaced by the host's Win32.
// See XboxXapi.cpp. Call before the game's startup thread runs: the heap entry points have to be
// ours before the process heap is made.
void Inject_XboxXapi(void);

#include <windows.h>

// CreateFileA and DeleteFileA on an Xbox path ("D:\driving\..."), resolved onto the host. FILESYS calls them.
HANDLE __stdcall Xbox_CreateFileA(const char *path, DWORD access, DWORD share, SECURITY_ATTRIBUTES *security,
                                  DWORD disposition, DWORD flags, HANDLE templateFile);
BOOL __stdcall Xbox_DeleteFileA(const char *path);

// Sleep (0x0010e9ab) and CreateThread (0x0010ec6a), for the ports that call them (the sound library's driver).
void __stdcall Xbox_Sleep(DWORD milliseconds);
HANDLE __stdcall Xbox_CreateThread(SECURITY_ATTRIBUTES *security, SIZE_T stackSize, LPTHREAD_START_ROUTINE start,
                                   void *parameter, DWORD flags, DWORD *threadId);

// MmFreeContiguousMemory (0x0010e82c), XGetVideoFlags (0x0010e02b) and OutputDebugStringA (0x0010e832), for EAGL.
void __stdcall Xbox_MmFreeContiguousMemory(void *base);
DWORD Xbox_XGetVideoFlags(void);
void __stdcall Xbox_OutputDebugStringA(const char *text);

#endif // DRIVING_PLATFORM_XBOXXAPI_H_
