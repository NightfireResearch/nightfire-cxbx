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

// ReadFile (0x0010f2c3), SetFilePointer (0x0010f50a), GetFileSize (0x0010f731) and CloseHandle (0x0010e9b9), for
// FILESYS and THREAD
BOOL __stdcall Xbox_ReadFile(HANDLE file, void *buffer, DWORD bytes, DWORD *read, OVERLAPPED *overlapped);
DWORD __stdcall Xbox_SetFilePointer(HANDLE file, LONG distance, LONG *distanceHigh, DWORD method);
DWORD __stdcall Xbox_GetFileSize(HANDLE file, DWORD *sizeHigh);
BOOL __stdcall Xbox_CloseHandle(HANDLE handle);

// CreateEventA (0x0010e864), SetEvent (0x0010e8c5), ResetEvent (0x0010e8e5), WaitForSingleObject (0x0010e999) and
// SleepEx (0x0010e947), for FILESYS, SIGNAL and THREAD
HANDLE __stdcall Xbox_CreateEventA(SECURITY_ATTRIBUTES *security, BOOL manualReset, BOOL initialState,
                                   const char *name);
BOOL __stdcall Xbox_SetEvent(HANDLE event);
BOOL __stdcall Xbox_ResetEvent(HANDLE event);
DWORD __stdcall Xbox_WaitForSingleObject(HANDLE handle, DWORD milliseconds);
DWORD __stdcall Xbox_SleepEx(DWORD milliseconds, BOOL alertable);

// Sleep (0x0010e9ab) and CreateThread (0x0010ec6a), for the ports that call them (the sound library's driver), and
// ResumeThread (0x0010ea61) and ExitThread (0x0010eadd), for THREAD.
void __stdcall Xbox_Sleep(DWORD milliseconds);
HANDLE __stdcall Xbox_CreateThread(SECURITY_ATTRIBUTES *security, SIZE_T stackSize, LPTHREAD_START_ROUTINE start,
                                   void *parameter, DWORD flags, DWORD *threadId);
DWORD __stdcall Xbox_ResumeThread(HANDLE thread);
void __stdcall Xbox_ExitThread(DWORD exitCode);

// IsBadReadPtr (0x0014bf05), for MEM's heap checks
BOOL __stdcall Xbox_IsBadReadPtr(const void *pointer, UINT_PTR bytes);

// MmFreeContiguousMemory (0x0010e82c), XGetVideoFlags (0x0010e02b) and OutputDebugStringA (0x0010e832), for EAGL.
void __stdcall Xbox_MmFreeContiguousMemory(void *base);

// XPhysicalAlloc (0x0010e7e9), for UMemory, and GetLocalTime (0x0010e684), for the dashboard clock
void *__stdcall Xbox_XPhysicalAlloc(ULONG size, ULONG address, ULONG alignment, ULONG protect);
void __stdcall Xbox_GetLocalTime(SYSTEMTIME *time);
DWORD Xbox_XGetVideoFlags(void);
void __stdcall Xbox_OutputDebugStringA(const char *text);
// WriteFile (0x0010f3b0), for PRINT's file log
BOOL __stdcall Xbox_WriteFile(HANDLE file, const void *buffer, DWORD bytes, DWORD *written, OVERLAPPED *overlapped);

#endif // DRIVING_PLATFORM_XBOXXAPI_H_
