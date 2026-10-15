#ifndef COMMON_LINUX_WIN32CALLS_H_
#define COMMON_LINUX_WIN32CALLS_H_

// The handful of Win32 calls the shared D3D9 backend makes (gfx/d3d9Backend.cpp, gfx/textureReplace.cpp), on POSIX,
// for the Linux build. Their types are dxvk-native's (its windows.h), which the backend compiles against for D3D9.
// Paths may use either separator.

#include <windows.h>

UINT GetPrivateProfileIntA(LPCSTR section, LPCSTR key, INT defaultValue, LPCSTR file);
DWORD GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR defaultValue, LPSTR value, DWORD size, LPCSTR file);

BOOL QueryPerformanceCounter(LARGE_INTEGER *counter);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *frequency);
void Sleep(DWORD milliseconds);
#if defined(__aarch64__)
#define YieldProcessor() __builtin_arm_yield()
#else
#define YieldProcessor() __builtin_ia32_pause()
#endif
#define timeBeginPeriod(milliseconds) 0   // the scheduler's resolution is already fine enough

BOOL CreateDirectoryA(LPCSTR path, SECURITY_ATTRIBUTES *security);

// A directory search: the pattern's last component may hold * and ?, matched without regard to case
struct WIN32_FIND_DATAA {
    char cFileName[MAX_PATH];
};
HANDLE FindFirstFileA(LPCSTR pattern, WIN32_FIND_DATAA *found);
BOOL FindNextFileA(HANDLE search, WIN32_FIND_DATAA *found);
BOOL FindClose(HANDLE search);

#endif // COMMON_LINUX_WIN32CALLS_H_
