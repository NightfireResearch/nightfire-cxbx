#include <windows.h>

#include "XboxPaths.h"
#include "../actionhelpers.h"

// ---------------------------------------------------------------------------------------------------------------
// The game's file I/O, on Win32 instead of the Xbox kernel.
//
// Stage B step 4.1 of docs/cxbx-removal-plan.md. This turned out to be far smaller than the plan assumed,
// because the Xbox's XAPI is a Win32 clone and the game uses it exactly like Win32: the five functions below
// are CreateFileA, ReadFile, GetFileSizeEx, GetOverlappedResult and CloseHandle with different names. Even the
// struct the game calls IO_STATUS_BLOCK is a Win32 OVERLAPPED - Internal, InternalHigh, Offset, OffsetHigh,
// hEvent, twenty bytes - which is why FS.cpp's async read loop is already correct Win32 logic and needs no
// changes at all. Everything above this layer (openOrCreateFile, maybeReadFile, FS_OperationInProgress, and
// the archive reader) is left exactly as it is; only the bottom is swapped out.
//
// With these in place, every file the game opens through createFile is resolved by XboxPaths.cpp and read with
// real Win32 handles, so disc data comes from the DiscPath folder rather than from whatever CXBX mounted as
// D:. The one path still going through the kernel is the async reader at FUN_0010a6b0, which calls
// NtCreateFile/NtReadFile directly rather than coming through here.
//
// Calling conventions were taken from each function's RET immediate rather than from Ghidra's prototypes,
// which is how createFile's seventh parameter turned up: Ghidra reports six, the function is RET 0x1c, and the
// call site in openOrCreateFile pushes seven dwords with no caller cleanup. That last parameter is
// hTemplateFile, which makes the mapping to CreateFileA exact.
// ---------------------------------------------------------------------------------------------------------------

// Per-call logging for this layer. Off by default; set to 1 when bringing up a change here, because the
// failure mode is otherwise the game s own fatal error screen with nothing said about which file or which
// call went wrong. It is what found the last-error problem described below.
#define XBOXFILE_VERBOSE 0

#if XBOXFILE_VERBOSE
#define FILE_LOG(...) printf(__VA_ARGS__)
#else
#define FILE_LOG(...) ((void)0)
#endif

// The game does not read Win32's last-error value. XAPI keeps its own, in the XBE's TLS block:
//
//     XAPILIB::SetLastError(code)  ->  *(TLS[_tls_index] + 4) = code
//     XAPILIB::GetLastError()      ->  return *(TLS[_tls_index] + 4)
//
// which is separate storage from the TEB slot Win32's SetLastError writes. That distinction is load bearing
// rather than cosmetic: maybeReadFile decides whether an overlapped read is in flight or has failed by testing
// GetLastError() == ERROR_IO_PENDING, and if it reads a stale zero it concludes the read failed and calls
// FS_FatalErrorHandler - the "disc may be dirty or damaged" screen, which never returns. So every Win32 call
// below has its result mirrored into the XBE's slot.
typedef void(__stdcall *XapiSetLastErrorFn)(uint32_t code);
#define XapiSetLastError ((XapiSetLastErrorFn)0x000e9a4cu)

static void PublishLastError(DWORD err) {
    SetLastError(err);       // keep Win32's in step, for our own code and anything else that looks
    XapiSetLastError(err);   // and the one the game actually reads
}

// Resolves an Xbox path and reports failures once per distinct path, since a missing disc file otherwise shows
// up only as the game's own fatal error handler with nothing to say which file was missing.
static bool ResolveForOpen(const char *filename, char *out, size_t outSize) {
    if (!Xbox_ResolvePath(filename, out, outSize)) {
        printf("[file] cannot resolve path: %s\n", filename ? filename : "(null)");
        return false;
    }
    return true;
}

// CreateFileA. See the block comment above for the seventh parameter.
//
// FILE_FLAG_NO_BUFFERING is stripped deliberately. The game asks for it on every file openOrCreateFile opens
// (flags 0x60000000 = OVERLAPPED | NO_BUFFERING), which made sense for DVD reads on the real hardware. On a
// host filesystem it only imposes the constraint that every offset, length and buffer address be sector
// aligned - and the very first read the game makes, FS_Init pulling the archive header into
// FileSystem.ramCache at 0x002b0d28, is into a buffer that is not. CXBX does not honour the flag either, which
// is why this has never mattered before now.
//
// AUTOINJECT
HANDLE __stdcall createFile(const char *filename, uint32_t desiredAccess, uint32_t shareMode,
                            void *securityAttributes, uint32_t creationDisposition,
                            uint32_t flagsAndAttributes, HANDLE templateFile) {
    char hostPath[512];
    if (!ResolveForOpen(filename, hostPath, sizeof(hostPath))) {
        PublishLastError(ERROR_FILENAME_EXCED_RANGE);
        return INVALID_HANDLE_VALUE;
    }

    SetLastError(0);
    HANDLE handle = CreateFileA(hostPath, desiredAccess, shareMode,
                                (LPSECURITY_ATTRIBUTES)securityAttributes, creationDisposition,
                                flagsAndAttributes & ~(uint32_t)FILE_FLAG_NO_BUFFERING,
                                templateFile);
    DWORD openErr = GetLastError();   // CreateFileA also reports ERROR_ALREADY_EXISTS on a successful open
    FILE_LOG("[file] open %s -> %s : handle 0x%08x access 0x%08x share %u disp %u flags 0x%08x err %lu\n",
             filename, hostPath, (unsigned)(uintptr_t)handle, desiredAccess, shareMode,
             creationDisposition, flagsAndAttributes, openErr);
    PublishLastError(openErr);
    return handle;
}

// ReadFile. Returns 1 on success and 0 on failure, and leaves the last-error code alone on the way out,
// because the layer above distinguishes "failed" from "still in flight" by testing for ERROR_IO_PENDING -
// see maybeReadFile, which treats pending as success and lets FS_OperationInProgress poll it.
//
// AUTOINJECT
int __stdcall readFromFileBlocking(HANDLE fileHandle, void *buffer, uint32_t len,
                                   uint32_t *bytesRead, OVERLAPPED *overlapped) {
    if (bytesRead != NULL)
        *bytesRead = 0;

    if (overlapped == NULL) {
        DWORD read = 0;
        SetLastError(0);
        BOOL ok = ReadFile(fileHandle, buffer, len, &read, NULL);
        DWORD err = GetLastError();
        FILE_LOG("[file] read sync handle 0x%08x len %u -> %s read %lu err %lu\n",
                 (unsigned)(uintptr_t)fileHandle, len, ok ? "ok" : "FAILED", read, err);
        PublishLastError(err);
        if (!ok)
            return 0;
        if (bytesRead != NULL)
            *bytesRead = read;
        return 1;
    }

    // The caller has already filled in Offset/OffsetHigh. Marking it pending first matches the original and
    // means a caller inspecting the struct before the read returns sees the right thing.
    overlapped->Internal = (ULONG_PTR)STATUS_PENDING;
    overlapped->InternalHigh = 0;

    DWORD read = 0;
    SetLastError(0);
    BOOL ok = ReadFile(fileHandle, buffer, len, &read, overlapped);
    DWORD err = GetLastError();
    FILE_LOG("[file] read async handle 0x%08x len %u off %lu -> %s read %lu err %lu\n",
             (unsigned)(uintptr_t)fileHandle, len, overlapped->Offset,
             ok ? "ok" : (err == ERROR_IO_PENDING ? "pending" : "FAILED"), read, err);
    PublishLastError(err);   // the logging above must not disturb what the caller tests
    if (!ok)
        return 0;            // including ERROR_IO_PENDING, which the caller checks for

    if (bytesRead != NULL)
        *bytesRead = (uint32_t)overlapped->InternalHigh;
    return 1;
}

// GetOverlappedResult, which FS_OperationInProgress polls with wait = false. The out parameter is the
// transferred byte count.
//
// Returns int, not bool, and that matters. The original ends in "XOR EAX,EAX / INC EAX" or a bare
// "XOR EAX,EAX", i.e. it sets the whole of EAX - and its callers test the whole of EAX (Ghidra renders this as
// CONCAT31(extraout_var, result) != 0). A C++ bool return only sets AL under MSVC and leaves the top 24 bits
// as whatever happened to be in EAX, so a false could read as true. That turns "the read is still in flight"
// into "the read has finished", which is how a perfectly working async read ends up delivering nothing. Same
// reasoning for the two below.
//
// FUNC_AT(000e9a91)
int __stdcall Xbox_GetOverlappedResult(HANDLE fileHandle, OVERLAPPED *overlapped,
                                       uint32_t *bytesTransferred, int wait) {
    DWORD transferred = 0;
    SetLastError(0);
    BOOL ok = GetOverlappedResult(fileHandle, overlapped, &transferred, wait ? TRUE : FALSE);
    DWORD err = GetLastError();
    static int logged = 0;
    if (logged < 24) {           // polled every frame, so do not let it flood the log
        logged++;
        FILE_LOG("[file] overlapped handle 0x%08x -> %s transferred %lu err %lu internal 0x%08x\n",
                 (unsigned)(uintptr_t)fileHandle, ok ? "complete" : "incomplete", transferred, err,
                 (unsigned)overlapped->Internal);
    }
    if (bytesTransferred != NULL)
        *bytesTransferred = transferred;
    PublishLastError(err);   // FS_OperationInProgress polls for ERROR_IO_INCOMPLETE here
    return ok != FALSE ? 1 : 0;
}

// GetFileSizeEx.
//
// AUTOINJECT
int __stdcall getFileSize_LargeInteger(HANDLE fileHandle, LARGE_INTEGER *fileSize) {
    SetLastError(0);
    BOOL ok = GetFileSizeEx(fileHandle, fileSize);
    PublishLastError(GetLastError());
    return ok != FALSE ? 1 : 0;
}

// CloseHandle.
//
// AUTOINJECT
int __stdcall DoNtClose(HANDLE handle) {
    SetLastError(0);
    BOOL ok = CloseHandle(handle);
    PublishLastError(GetLastError());
    return ok != FALSE ? 1 : 0;
}
