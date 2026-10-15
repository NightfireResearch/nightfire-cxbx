#ifndef DRIVING_PLATFORM_WIN32TYPES_H_
#define DRIVING_PLATFORM_WIN32TYPES_H_

// The Win32 types and numbers the driving engine still speaks. XAPI is Win32's API on the console - the same types,
// error numbers and flags - so the XAPI layer (XboxXapi.cpp, XboxStartup.cpp) and the game code above it are written
// in them, and the host layer (Host.h) passes them through. On Windows they are windows.h's. Elsewhere this header
// defines the ones in use, as windows.h defines them: DWORD is 32 bits (an unsigned long on a 32-bit host), BOOL an
// int, a handle a pointer. The Win32 functions themselves are not here: the code calls the host layer for those.

#ifdef _WIN32

#include <windows.h>

#else

#include <stddef.h>
#include <stdint.h>

#ifndef __stdcall
#define __stdcall __attribute__((stdcall))
#endif
#ifndef __cdecl
#define __cdecl __attribute__((cdecl))
#endif

#define WINAPI   __stdcall
#define CALLBACK __stdcall

typedef unsigned char BYTE, UCHAR, BOOLEAN;
typedef unsigned short WORD, USHORT;
typedef char CHAR;
typedef short SHORT;
typedef int INT, BOOL;
typedef unsigned int UINT;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
#if UINTPTR_MAX == 0xffffffffu
typedef unsigned long DWORD, ULONG;
typedef long LONG;
typedef int INT_PTR;
typedef unsigned int UINT_PTR;
typedef long LONG_PTR;
typedef unsigned long ULONG_PTR, DWORD_PTR, SIZE_T;
#else
// LP64: long is 64 bits, so DWORD and LONG are the fixed-width types (Host.h's HostDword and HostLong), and the
// pointer-sized ones are 64-bit Windows'
typedef uint32_t DWORD, ULONG;
typedef int32_t LONG;
typedef int64_t INT_PTR, LONG_PTR;
typedef uint64_t UINT_PTR, ULONG_PTR, DWORD_PTR, SIZE_T;
#endif
static_assert(sizeof(DWORD) == 4 && sizeof(LONG) == 4 && sizeof(ULONG_PTR) == sizeof(void *), "Win32's sizes");

typedef void *PVOID, *LPVOID, *HANDLE, *HMODULE, *HWND;
typedef const void *LPCVOID;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef DWORD *LPDWORD;
typedef LONG *PLONG;

typedef int (__stdcall *FARPROC)(void);
typedef DWORD (__stdcall *LPTHREAD_START_ROUTINE)(LPVOID parameter);
typedef void (__stdcall *LPTIMECALLBACK)(UINT id, UINT message, DWORD_PTR user, DWORD_PTR reserved1,
                                         DWORD_PTR reserved2);

typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    DWORD Offset;
    DWORD OffsetHigh;
    HANDLE hEvent;
} OVERLAPPED;

typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;

typedef struct _TIME_ZONE_INFORMATION {
    LONG Bias;
    uint16_t StandardName[32];
    SYSTEMTIME StandardDate;
    LONG StandardBias;
    uint16_t DaylightName[32];
    SYSTEMTIME DaylightDate;
    LONG DaylightBias;
} TIME_ZONE_INFORMATION;

#define TRUE  1
#define FALSE 0

#define MAX_PATH 260
#define INFINITE 0xffffffff
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)

#define WAIT_OBJECT_0 0x00000000
#define WAIT_TIMEOUT  0x00000102
#define WAIT_FAILED   0xffffffff
#define STILL_ACTIVE  0x00000103

#define ERROR_SUCCESS             0
#define ERROR_FILE_NOT_FOUND      2
#define ERROR_PATH_NOT_FOUND      3
#define ERROR_TOO_MANY_OPEN_FILES 4
#define ERROR_ACCESS_DENIED       5
#define ERROR_INVALID_HANDLE      6
#define ERROR_NOT_ENOUGH_MEMORY   8
#define ERROR_BAD_FORMAT          11
#define ERROR_READ_FAULT          30
#define ERROR_BADNAME             161

#define GENERIC_READ  0x80000000
#define GENERIC_WRITE 0x40000000
#define FILE_SHARE_READ  0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define CREATE_NEW        1
#define CREATE_ALWAYS     2
#define OPEN_EXISTING     3
#define OPEN_ALWAYS       4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_BEGIN   0
#define FILE_CURRENT 1
#define FILE_END     2

#define CREATE_SUSPENDED 0x00000004
#define THREAD_PRIORITY_IDLE          (-15)
#define THREAD_PRIORITY_LOWEST        (-2)
#define THREAD_PRIORITY_BELOW_NORMAL  (-1)
#define THREAD_PRIORITY_NORMAL        0
#define THREAD_PRIORITY_ABOVE_NORMAL  1
#define THREAD_PRIORITY_HIGHEST       2
#define THREAD_PRIORITY_TIME_CRITICAL 15

#define HEAP_NO_SERIALIZE          0x00000001
#define HEAP_GROWABLE              0x00000002
#define HEAP_GENERATE_EXCEPTIONS   0x00000004
#define HEAP_ZERO_MEMORY           0x00000008
#define HEAP_REALLOC_IN_PLACE_ONLY 0x00000010

#define MEM_COMMIT  0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_RELEASE 0x00008000
#define MEM_FREE    0x00010000
#define PAGE_READWRITE         0x04
#define PAGE_EXECUTE_READWRITE 0x40

#define EXCEPTION_NONCONTINUABLE 0x1

#define TIME_PERIODIC  0x0001
#define TIMERR_NOERROR 0

#endif // _WIN32

#endif // DRIVING_PLATFORM_WIN32TYPES_H_
