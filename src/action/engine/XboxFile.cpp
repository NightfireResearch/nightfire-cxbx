#include "XboxFile.h"
#include "../../common/xboxPath.h"
#include "../../driving/platform/Host.h"
#include "XboxSettings.h"
#include "../actionhelpers.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The game's file I/O, on the host layer (driving/platform/Host.h) instead of the Xbox kernel.
//
// Stage B step 4.1 of docs/cxbx-removal-plan.md. This turned out to be far smaller than the plan assumed,
// because the Xbox's XAPI is a Win32 clone and the game uses it exactly like Win32: the five functions below
// are CreateFileA, ReadFile, GetFileSizeEx, GetOverlappedResult and CloseHandle with different names. Even the
// struct the game calls IO_STATUS_BLOCK is a Win32 OVERLAPPED - Internal, InternalHigh, Offset, OffsetHigh,
// hEvent, twenty bytes - so FS.cpp's async read loop is Win32 logic and is left as it is. Everything above
// this layer (openOrCreateFile, FS_ReadFile, FS_OperationInProgress, and the archive reader) is left exactly
// as it is; only the bottom is swapped out.
//
// The host has no overlapped I/O, so an overlapped read or write is made there and then, at its offset, and is
// complete when the call returns: the game's polls find it done the first time, as they do in a time demo,
// which waited for every read (devtools/Demo.h).
//
// With these in place, every file the game opens through createFile is resolved by common/xboxPath.cpp and read
// with host handles, so disc data comes from the DiscPath folder. The one path still going through the kernel is
// the async reader at FUN_0010a6b0, which calls NtCreateFile/NtReadFile directly rather than coming through here.
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

// The game reads the last error through XAPI's GetLastError, which XboxStartup.cpp makes the host's
// (Xbox_GetLastError). It is load bearing: FS_ReadFile decides whether an overlapped read is in flight or has
// failed by testing GetLastError() == ERROR_IO_PENDING, and if it reads a stale zero it concludes the read failed
// and calls FS_FatalErrorHandler - the "disc may be dirty or damaged" screen, which never returns. So every host
// call below leaves its result as the last error.
static void PublishLastError(DWORD err) {
    HostSetLastError(err);
}

// The overlapped structure's status words, NTSTATUS values as the Xbox's IO_STATUS_BLOCK holds them
#define IO_STATUS_SUCCESS      0x00000000u
#define IO_STATUS_PENDING      0x00000103u
#define IO_STATUS_END_OF_FILE  0xc0000011u
#define IO_STATUS_UNSUCCESSFUL 0xc0000001u

// ---------------------------------------------------------------------------------------------------------------
// Which file a handle belongs to, and saying so when a read fails.
//
// The game's response to a failed read is FS_FatalErrorHandler - the "there's a problem with the disc you're
// using, it may be dirty or damaged" screen, which never returns and says nothing about what went wrong. That
// has now cost time twice, so the two places that can lead to it report themselves on the way past, whatever
// the verbose setting. The conditions are not guesses: FS_ReadFile calls the fatal handler when a read
// returns 0 with an error that is not ERROR_IO_PENDING, and FS_OperationInProgress does when
// GetOverlappedResult fails with anything but ERROR_IO_INCOMPLETE or ERROR_IO_PENDING.
//
// The handle table exists so those messages can name a file. It is small and fixed because the game holds a
// dozen or so handles at once - the filesys archives plus whatever is being streamed.
// ---------------------------------------------------------------------------------------------------------------

#define TRACKED_HANDLES 64

static struct { HANDLE handle; char path[160]; } g_openFiles[TRACKED_HANDLES];

static void RememberHandle(HANDLE handle, const char *xboxPath) {
    if (handle == INVALID_HANDLE_VALUE)
        return;
    for (int i = 0; i < TRACKED_HANDLES; i++) {
        if (g_openFiles[i].handle == NULL || g_openFiles[i].handle == handle) {
            g_openFiles[i].handle = handle;
            snprintf(g_openFiles[i].path, sizeof(g_openFiles[i].path), "%s", xboxPath ? xboxPath : "?");
            return;
        }
    }
}

static void ForgetHandle(HANDLE handle) {
    for (int i = 0; i < TRACKED_HANDLES; i++) {
        if (g_openFiles[i].handle == handle) {
            g_openFiles[i].handle = NULL;
            return;
        }
    }
}

static const char *PathForHandle(HANDLE handle) {
    for (int i = 0; i < TRACKED_HANDLES; i++) {
        if (g_openFiles[i].handle == handle)
            return g_openFiles[i].path;
    }
    return "(an untracked handle)";
}

// Printed whatever XBOXFILE_VERBOSE says, because the alternative is the disc-error screen with no
// explanation at all.
static void ReportFatalFileError(const char *what, HANDLE handle, uint32_t offset, uint32_t length,
                                 DWORD err) {
    printf("\n[file] ---------------------------------------------------------------\n");
    printf("[file] %s failed on %s\n", what, PathForHandle(handle));
    printf("[file]   handle 0x%08x, offset %u, length %u, error %lu\n",
           (unsigned)(uintptr_t)handle, offset, length, (unsigned long)err);
    printf("[file] The game treats this as a damaged disc and shows its fatal error\n"
           "[file] screen, which never returns. The error code above is the real cause.\n");
    printf("[file] ---------------------------------------------------------------\n\n");
    fflush(stdout);
}

// Streaming I/O accounting - see XboxFile_ReportStreamingIfDue at the end of this file for what these are
// for. Declared here because the read path below is what increments them.
static uint32_t g_reads = 0;
static uint32_t g_readsFailed = 0;
static uint64_t g_readBytes = 0;

// Defined with the directory enumeration at the end of this file; DoNtClose needs it before then, because a
// find handle arrives there looking like any other handle.
static bool CloseIfFindHandle(HANDLE handle);

// Resolves an Xbox path and reports failures once per distinct path, since a missing disc file otherwise shows
// up only as the game's own fatal error handler with nothing to say which file was missing.
static bool ResolveForOpen(const char *filename, char *out, size_t outSize) {
    if (!Xbox_ResolvePath(filename, out, outSize)) {
        printf("[file] cannot resolve path: %s\n", filename ? filename : "(null)");
        return false;
    }
    return true;
}

// CreateFileA. See the block comment above for the seventh parameter; the game passes NULL for it and for the
// security attributes.
//
// FILE_FLAG_NO_BUFFERING and FILE_FLAG_OVERLAPPED are stripped deliberately. The game asks for both on every
// file openOrCreateFile opens (flags 0x60000000), which made sense for DVD reads on the real hardware. On a
// host filesystem no buffering only imposes the constraint that every offset, length and buffer address be
// sector aligned - and the very first read the game makes, FS_Init pulling the archive header into
// FileSystem.ramCache at 0x002b0d28, is into a buffer that is not. (CXBX never honoured the flag either, which
// is why it did not matter while the game ran there.) The overlapped reads are made synchronously (above).
//
// AUTOINJECT
HANDLE __stdcall createFile(const char *filename, uint32_t desiredAccess, uint32_t shareMode,
                            void *securityAttributes, uint32_t creationDisposition,
                            uint32_t flagsAndAttributes, HANDLE templateFile) {
    (void)securityAttributes, (void)templateFile;
    char hostPath[512];
    if (!ResolveForOpen(filename, hostPath, sizeof(hostPath))) {
        PublishLastError(ERROR_FILENAME_EXCED_RANGE);
        return INVALID_HANDLE_VALUE;
    }

    HostSetLastError(0);
    HANDLE handle = HostCreateFile(hostPath, desiredAccess, shareMode, creationDisposition,
                                   flagsAndAttributes & ~(uint32_t)(FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED));
    DWORD openErr = HostGetLastError();   // CreateFileA also reports ERROR_ALREADY_EXISTS on a successful open
    FILE_LOG("[file] open %s -> %s : handle 0x%08x access 0x%08x share %u disp %u flags 0x%08x err %lu\n",
             filename, hostPath, (unsigned)(uintptr_t)handle, desiredAccess, shareMode,
             creationDisposition, flagsAndAttributes, openErr);
    PublishLastError(openErr);
    RememberHandle(handle, filename);
    return handle;
}

// An overlapped read or write at its offset, made there and then. ReadFile and WriteFile fail an overlapped call
// at or past the end of the file with ERROR_HANDLE_EOF, where a plain one succeeds with nothing moved: so does
// this. The overlapped structure is left saying what happened, as on completion.
static BOOL TransferAt(HANDLE fileHandle, void *buffer, uint32_t len, OVERLAPPED *overlapped, bool write,
                       DWORD *done) {
    *done = 0;
    LONG high = (LONG)overlapped->OffsetHigh;
    HostSetLastError(0);
    BOOL ok = HostSetFilePointer(fileHandle, (LONG)overlapped->Offset, &high, FILE_BEGIN) != 0xffffffffu ||
              HostGetLastError() == 0;
    if (ok)
        ok = write ? HostWriteFile(fileHandle, buffer, len, done) : HostReadFile(fileHandle, buffer, len, done);
    if (ok && !write && *done == 0 && len != 0) {
        HostSetLastError(ERROR_HANDLE_EOF);
        ok = FALSE;
    }
    overlapped->Internal = ok ? IO_STATUS_SUCCESS
                              : HostGetLastError() == ERROR_HANDLE_EOF ? IO_STATUS_END_OF_FILE : IO_STATUS_UNSUCCESSFUL;
    overlapped->InternalHigh = (ULONG_PTR)*done;
    return ok;
}

// ReadFile. Returns 1 on success and 0 on failure, and leaves the last-error code alone on the way out,
// because the layer above distinguishes "failed" from "still in flight" by testing for ERROR_IO_PENDING -
// see FS_ReadFile, which treats pending as success and lets FS_OperationInProgress poll it. Here no read is
// ever in flight.
//
// AUTOINJECT
int __stdcall readFromFileBlocking(HANDLE fileHandle, void *buffer, uint32_t len,
                                   uint32_t *bytesRead, OVERLAPPED *overlapped) {
    if (bytesRead != NULL)
        *bytesRead = 0;

    if (overlapped == NULL) {
        DWORD read = 0;
        HostSetLastError(0);
        BOOL ok = HostReadFile(fileHandle, buffer, len, &read);
        DWORD err = HostGetLastError();
        FILE_LOG("[file] read sync handle 0x%08x len %u -> %s read %lu err %lu\n",
                 (unsigned)(uintptr_t)fileHandle, len, ok ? "ok" : "FAILED", read, err);
        PublishLastError(err);
        if (!ok) {
            ReportFatalFileError("a synchronous read", fileHandle, 0, len, err);
            return 0;
        }
        if (bytesRead != NULL)
            *bytesRead = read;
        return 1;
    }

    // The caller has already filled in Offset/OffsetHigh. Marking it pending first matches the original and
    // means a caller inspecting the struct before the read returns sees the right thing.
    overlapped->Internal = (ULONG_PTR)IO_STATUS_PENDING;
    overlapped->InternalHigh = 0;

    DWORD read = 0;
    BOOL ok = TransferAt(fileHandle, buffer, len, overlapped, false, &read);
    DWORD err = ok ? 0 : HostGetLastError();
    FILE_LOG("[file] read at handle 0x%08x len %u off %lu -> %s read %lu err %lu\n",
             (unsigned)(uintptr_t)fileHandle, len, overlapped->Offset, ok ? "ok" : "FAILED", read, err);
    PublishLastError(err);   // the logging above must not disturb what the caller tests
    if (!ok) {
        g_readsFailed++;
        ReportFatalFileError("an overlapped read", fileHandle, overlapped->Offset, len, err);
        return 0;
    }
    g_reads++;
    g_readBytes += read;

    if (bytesRead != NULL)
        *bytesRead = (uint32_t)overlapped->InternalHigh;
    return 1;
}

// GetOverlappedResult, which FS_OperationInProgress polls with wait = false. The out parameter is the
// transferred byte count. Every read is complete by the time it is polled: the structure says how it went.
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
    (void)wait;
    DWORD err;
    switch ((uint32_t)overlapped->Internal) {
    case IO_STATUS_SUCCESS:     err = 0; break;
    case IO_STATUS_PENDING:     err = ERROR_IO_INCOMPLETE; break;
    case IO_STATUS_END_OF_FILE: err = ERROR_HANDLE_EOF; break;
    default:                    err = ERROR_READ_FAULT; break;
    }
    DWORD transferred = err == 0 ? (DWORD)overlapped->InternalHigh : 0;
    static int logged = 0;
    if (logged < 24) {           // polled every frame, so do not let it flood the log
        logged++;
        FILE_LOG("[file] overlapped handle 0x%08x -> %s transferred %lu err %lu internal 0x%08x\n",
                 (unsigned)(uintptr_t)fileHandle, err == 0 ? "complete" : "incomplete", transferred, err,
                 (unsigned)overlapped->Internal);
    }
    if (bytesTransferred != NULL)
        *bytesTransferred = transferred;
    PublishLastError(err);   // FS_OperationInProgress polls for ERROR_IO_INCOMPLETE here
    if (err != 0 && err != ERROR_IO_INCOMPLETE)
        ReportFatalFileError("waiting for an overlapped read", fileHandle, overlapped->Offset, 0, err);
    return err == 0 ? 1 : 0;
}

// GetFileSizeEx.
//
// AUTOINJECT
int __stdcall getFileSize_LargeInteger(HANDLE fileHandle, LARGE_INTEGER *fileSize) {
    HostSetLastError(0);
    int64_t size = 0;
    BOOL ok = HostGetFileSizeEx(fileHandle, &size);
    if (ok)
        fileSize->QuadPart = size;
    PublishLastError(HostGetLastError());
    return ok != FALSE ? 1 : 0;
}

// CloseHandle.
//
// AUTOINJECT
int __stdcall DoNtClose(HANDLE handle) {
    ForgetHandle(handle);

    // A directory enumeration handed out a find handle, which closing as a file would reject - see
    // Xbox_FindFirstFileA. Everything else is an ordinary file handle.
    if (CloseIfFindHandle(handle)) {
        PublishLastError(0);
        return 1;
    }

    HostSetLastError(0);
    BOOL ok = HostCloseHandle(handle);
    PublishLastError(HostGetLastError());
    return ok != FALSE ? 1 : 0;
}

// WriteFile. Mirrors readFromFileBlocking, made there and then as the reads are.
//
// AUTOINJECT
int __stdcall FileWrite(HANDLE fileHandle, void *buffer, uint32_t len,
                        uint32_t *bytesWritten, OVERLAPPED *overlapped) {
    if (bytesWritten != NULL)
        *bytesWritten = 0;

    if (overlapped == NULL) {
        DWORD written = 0;
        HostSetLastError(0);
        BOOL ok = HostWriteFile(fileHandle, buffer, len, &written);
        DWORD err = HostGetLastError();
        FILE_LOG("[file] write sync handle 0x%08x len %u -> %s written %lu err %lu\n",
                 (unsigned)(uintptr_t)fileHandle, len, ok ? "ok" : "FAILED", written, err);
        PublishLastError(err);
        if (!ok)
            return 0;
        if (bytesWritten != NULL)
            *bytesWritten = written;
        return 1;
    }

    overlapped->Internal = (ULONG_PTR)IO_STATUS_PENDING;
    overlapped->InternalHigh = 0;

    DWORD written = 0;
    BOOL ok = TransferAt(fileHandle, buffer, len, overlapped, true, &written);
    DWORD err = ok ? 0 : HostGetLastError();
    FILE_LOG("[file] write at handle 0x%08x len %u off %lu -> %s written %lu err %lu\n",
             (unsigned)(uintptr_t)fileHandle, len, overlapped->Offset, ok ? "ok" : "FAILED", written, err);
    PublishLastError(err);
    if (!ok)
        return 0;

    if (bytesWritten != NULL)
        *bytesWritten = (uint32_t)overlapped->InternalHigh;
    return 1;
}

// FlushFileBuffers.
//
// AUTOINJECT
int __stdcall file_flush(HANDLE fileHandle) {
    HostSetLastError(0);
    BOOL ok = HostFlushFile(fileHandle);
    PublishLastError(HostGetLastError());
    return ok != FALSE ? 1 : 0;
}

// SetEndOfFile. The original reads the current position (FilePositionInformation) and writes it to both
// EndOfFileInformation and AllocationInformation, which is what SetEndOfFile does.
//
// AUTOINJECT
int __stdcall setSomeInfo(HANDLE fileHandle) {
    HostSetLastError(0);
    BOOL ok = HostSetEndOfFile(fileHandle);
    PublishLastError(HostGetLastError());
    return ok != FALSE ? 1 : 0;
}

// SetFilePointer, including its awkward return convention: the new low word, or 0xFFFFFFFF for failure - so a
// genuine position of 0xFFFFFFFF is distinguished by the last-error code being zero, which is why the success
// path still publishes one.
//
// AUTOINJECT
uint32_t __stdcall querySetSomeInfo(HANDLE fileHandle, uint32_t distanceLow, uint32_t *distanceHigh,
                                    uint32_t moveMethod) {
    HostSetLastError(0);
    DWORD result = HostSetFilePointer(fileHandle, (LONG)distanceLow, (HostLong *)distanceHigh, moveMethod);
    PublishLastError(HostGetLastError());
    return result;
}

// SetFilePointerEx. The out parameter is a 64-bit position written as two dwords. SetFilePointer with the high
// word given moves the same way; it failed if it answers 0xFFFFFFFF with an error.
//
// FUNC_AT(000e9731)
int __stdcall Xbox_SetFilePointerEx(HANDLE fileHandle, uint32_t distanceLow, uint32_t distanceHigh,
                                    uint32_t *newPosition, uint32_t moveMethod) {
    LONG high = (LONG)distanceHigh;
    HostSetLastError(0);
    DWORD low = HostSetFilePointer(fileHandle, (LONG)distanceLow, &high, moveMethod);
    DWORD err = HostGetLastError();
    PublishLastError(err);
    if (low == 0xffffffffu && err != 0)
        return 0;

    if (newPosition != NULL) {
        newPosition[0] = low;
        newPosition[1] = (uint32_t)high;
    }
    return 1;
}

// DeleteFileA. The original opens with DELETE access, sets FileDispositionInformation and closes, which is
// what DeleteFile does. Takes an Xbox path, so it resolves like createFile - GetPTPData and
// WriteStateFileAndLaunch both use it on "z:\\state.bin".
//
// FUNC_AT(0x000ea6d4)
int __stdcall deleteFile(const char *filename) {
    char hostPath[512];
    if (!ResolveForOpen(filename, hostPath, sizeof(hostPath))) {
        PublishLastError(ERROR_FILENAME_EXCED_RANGE);
        return 0;
    }
    HostSetLastError(0);
    BOOL ok = HostDeleteFile(hostPath);
    DWORD err = HostGetLastError();
    FILE_LOG("[file] delete %s -> %s : %s err %lu\n", filename, hostPath, ok ? "ok" : "FAILED", err);
    PublishLastError(err);
    return ok != FALSE ? 1 : 0;
}

// GetFileAttributesA. The original builds an Xbox object name with RtlInitAnsiString and asks the kernel
// with NtQueryFullAttributesFile, returning the attributes word or 0xffffffff, so it needs replacing for the
// same reason every other function here did: those take paths in the Xbox object namespace, which is not
// something the standalone loader has.
//
// Its one caller is a file-existence helper at 0x000e36e0, used by the language scan in Language_Get that
// looks for t:\lang<nn><nn>.dat.
//
// That scan is reached only on a first run. Language_Get returns immediately if GetPTPData() has data, so
// once psiLaunch.bin exists the whole path is skipped - which is why this went unnoticed for so long on a
// machine that had been running the game all day, and turned up on a fresh install. It was reported from
// Wine on macOS and is nothing to do with either: deleting psiLaunch.bin reproduces it on Windows exactly,
// and restoring this injection is what fixes it.
//
// Two further gates, worth knowing before concluding a run has tested this: the scan is also skipped if
// config.txt supplies a language, and if the configured language resolves to American (English with an NTSC
// region does, English with PAL does not).
//
// Other XAPI functions still build Xbox object names the same way and have not been replaced, because
// nothing has reached them yet: 0x000e9f4d, 0x000eb34c, 0x000eb3b8, 0x000eb446,
// 0x000eb5c3, 0x000ed2e8, 0x000ee0d4. Linux has since reached two of the original list, 0x000eac45 and
// 0x000ea821, both implemented at the end of this file. Each remaining one will announce itself
// through the loader's kernel stub rather than
// misbehaving quietly, and each is a few lines like this one. 0x000eb6f6 was on that list until the save
// enumeration reached it; see Xbox_FindFirstFileA at the end of this file.
//
// FUNC_AT(000ea689)
uint32_t __stdcall Xbox_GetFileAttributesA(const char *filename) {
    char hostPath[512];
    if (!ResolveForOpen(filename, hostPath, sizeof(hostPath))) {
        PublishLastError(ERROR_FILENAME_EXCED_RANGE);
        return INVALID_FILE_ATTRIBUTES;
    }

    HostSetLastError(0);
    DWORD attributes = HostGetFileAttributes(hostPath);
    DWORD err = HostGetLastError();
    FILE_LOG("[file] attributes %s -> %s : 0x%08x err %lu\n", filename, hostPath, attributes, err);

    // A missing file is the expected answer here, not a failure - the caller is asking whether it exists -
    // but the error still has to be published, because that is how the game's own wrapper reports it.
    if (attributes == INVALID_FILE_ATTRIBUTES)
        PublishLastError(err != 0 ? err : ERROR_FILE_NOT_FOUND);
    else
        PublishLastError(0);
    return attributes;
}

// ---------------------------------------------------------------------------------------------------------------
// Directory enumeration: FindFirstFileA and FindNextFileA.
//
// The originals split the path at its last backslash, open the directory with NtOpenFile and walk it with
// NtQueryDirectoryFile, so they take paths in the Xbox object namespace and have to be replaced like the
// rest of this file. The structure they fill in is Win32's: the conversion at 0x000eb68a writes attributes at
// 0, three FILETIMEs at 4, 0xc and 0x14, the size high and low words at 0x1c and 0x20, the name at 0x2c and a
// terminator at 0x130 - which is WIN32_FIND_DATAA exactly, reserved fields and alternate name included. Their
// one caller (0x000ea280) reads the name and the directory attribute, so those are
// what is filled in; the times and sizes read zero.
//
// The one awkwardness is the handle. On the Xbox a find handle is an ordinary NT file handle, and the caller
// closes it with NtClose - which arrives here as DoNtClose. A host find handle is not a file handle and must
// be closed with HostFindClose, so the ones handed out here are remembered and DoNtClose checks that list
// first.
// ---------------------------------------------------------------------------------------------------------------

#define TRACKED_FINDS 16

static struct { HANDLE handle; char directory[512]; } g_finds[TRACKED_FINDS];

static void RememberFind(HANDLE handle, const char *directory) {
    for (int i = 0; i < TRACKED_FINDS; i++) {
        if (g_finds[i].handle == NULL) {
            g_finds[i].handle = handle;
            snprintf(g_finds[i].directory, sizeof(g_finds[i].directory), "%s", directory);
            return;
        }
    }
}

static const char *FindDirectory(HANDLE handle) {
    for (int i = 0; i < TRACKED_FINDS; i++) {
        if (g_finds[i].handle == handle)
            return g_finds[i].directory;
    }
    return "";
}

// True if this was one of ours, in which case it has also been closed and forgotten.
static bool CloseIfFindHandle(HANDLE handle) {
    for (int i = 0; i < TRACKED_FINDS; i++) {
        if (g_finds[i].handle == handle) {
            g_finds[i].handle = NULL;
            HostFindClose(handle);
            return true;
        }
    }
    return false;
}

// The entry's name and attributes; the rest zero
static void FillFindData(WIN32_FIND_DATAA *findData, const char *directory, const char *name) {
    memset(findData, 0, sizeof(*findData));
    snprintf(findData->cFileName, sizeof(findData->cFileName), "%s", name);
    char path[512 + HOST_MAX_PATH];
    snprintf(path, sizeof(path), "%s%s", directory, name);
    DWORD attributes = HostGetFileAttributes(path);
    findData->dwFileAttributes = attributes != HOST_INVALID_FILE_ATTRIBUTES ? attributes : FILE_ATTRIBUTE_NORMAL;
}

// FUNC_AT(000eb6f6)
uint32_t __stdcall Xbox_FindFirstFileA(const char *filename, WIN32_FIND_DATAA *findData) {
    char hostPath[512];
    if (!ResolveForOpen(filename, hostPath, sizeof(hostPath))) {
        PublishLastError(ERROR_FILENAME_EXCED_RANGE);
        return 0xFFFFFFFFu;
    }

    char name[HOST_MAX_PATH];
    HostSetLastError(0);
    HANDLE handle = HostFindFirstFile(hostPath, name);
    DWORD err = HostGetLastError();
    FILE_LOG("[file] find first %s -> %s : handle 0x%08x err %lu\n", filename, hostPath,
             (unsigned)(uintptr_t)handle, err);

    if (handle == INVALID_HANDLE_VALUE) {
        // An empty directory is an ordinary answer here, not a failure - the caller is enumerating.
        PublishLastError(err != 0 ? err : ERROR_FILE_NOT_FOUND);
        return 0xFFFFFFFFu;
    }

    // The directory searched, with its separator, for the entries' attributes
    char directory[512];
    snprintf(directory, sizeof(directory), "%s", hostPath);
    char *last = NULL;
    for (char *c = directory; *c != '\0'; c++)
        if (*c == '\\' || *c == '/')
            last = c;
    if (last != NULL)
        last[1] = '\0';
    else
        directory[0] = '\0';

    RememberFind(handle, directory);
    FillFindData(findData, directory, name);
    PublishLastError(0);
    return (uint32_t)(uintptr_t)handle;
}

// Returns int rather than bool for the reason given above Xbox_GetOverlappedResult: the original ends in
// "XOR EAX,EAX / INC EAX" or a bare "XOR EAX,EAX", and its caller tests the whole of EAX.
//
// FUNC_AT(000eb803)
int __stdcall Xbox_FindNextFileA(HANDLE findHandle, WIN32_FIND_DATAA *findData) {
    char name[HOST_MAX_PATH];
    HostSetLastError(0);
    BOOL ok = HostFindNextFile(findHandle, name);
    DWORD err = HostGetLastError();
    if (ok)
        FillFindData(findData, FindDirectory(findHandle), name);
    FILE_LOG("[file] find next handle 0x%08x -> %s err %lu\n",
             (unsigned)(uintptr_t)findHandle, ok ? "ok" : "end", err);

    // ERROR_NO_MORE_FILES is how the enumeration ends and is not a failure worth reporting.
    PublishLastError(err);
    return ok != FALSE ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Streaming I/O accounting, for "why is the frame rate like that".
//
// The game overlaps its disc reads with rendering: it issues an overlapped read, carries on drawing, and
// polls with GetOverlappedResult until the data arrives. Here every read is made before the call returns (see
// the top of this file), so the game thread waits for each one; the count and the bytes say how much it
// reads. Enabled by PerfLog in settings.ini.
// ---------------------------------------------------------------------------------------------------------------


void XboxFile_ReportStreamingIfDue(void) {
    if (!Settings_GetPerfLog())
        return;

    static uint32_t lastTick = 0;
    uint32_t now = HostTimeMs();
    if (lastTick == 0)
        lastTick = now;
    if (now - lastTick < 5000)
        return;
    lastTick = now;

    if (g_reads + g_readsFailed == 0)
        return;

    printf("[perf] disc reads: %u, %u failed, %llu KB\n", g_reads, g_readsFailed,
           (unsigned long long)(g_readBytes / 1024));
    fflush(stdout);
}

// The volume's allocation unit size, in bytes - what the original gets by opening the directory and asking
// NtQueryVolumeInformationFile for FileFsSizeInformation, then multiplying sectors-per-cluster by
// bytes-per-sector. Same Xbox object namespace problem as the rest of this file.
//
// Its one caller works out how many 16 KB blocks a save of a given size will occupy, which is the figure the
// save screens show, so this is reached from the menus rather than from anything in a level. It turned up on
// Linux at the main menu.
//
// GetDiskFreeSpaceA wants a root directory. A plain directory path works in practice, but not everywhere, so
// a failure falls back to the current disk's root - the save directory lives beside the executable, so that
// is the same volume either way.
//
// FUNC_AT(000eac45)
uint32_t __stdcall Xbox_GetVolumeClusterSize(const char *path) {
    char hostPath[512];
    if (!ResolveForOpen(path, hostPath, sizeof(hostPath))) {
        PublishLastError(ERROR_PATH_NOT_FOUND);
        return 0;
    }

    DWORD sectorsPerCluster = 0, bytesPerSector = 0, freeClusters = 0, totalClusters = 0;
    HostSetLastError(0);
    BOOL ok = HostGetDiskFreeSpace(hostPath, &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters);
    if (!ok)
        ok = HostGetDiskFreeSpace(NULL, &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters);

    if (!ok) {
        DWORD err = HostGetLastError();
        // The original turns "no such file" into "no such path" here, and its caller only checks for a
        // non-positive result, so keep both behaviours.
        PublishLastError(err == ERROR_FILE_NOT_FOUND ? ERROR_PATH_NOT_FOUND : err);
        FILE_LOG("[file] cluster size %s -> %s : FAILED err %lu\n", path, hostPath, err);
        return 0;
    }

    PublishLastError(0);
    FILE_LOG("[file] cluster size %s -> %s : %lu bytes\n", path, hostPath,
             sectorsPerCluster * bytesPerSector);
    return (uint32_t)(sectorsPerCluster * bytesPerSector);
}

// GetDiskFreeSpaceExA. The original asks the same FileFsSizeInformation as the cluster-size query above and
// multiplies out three figures: available units times cluster size for the free space, and total units times
// cluster size for the capacity. Win32 computes exactly those, so this is a direct mapping.
//
// The three outputs are all optional and the Xbox writes the same free-space value to the first and third,
// because a console has no per-user quotas to make them differ. The host can distinguish them; letting it is
// harmless and more truthful.
//
// Returns int rather than bool: the original ends "XOR EAX,EAX / INC EAX", so its callers see a full-width
// result - the same reason given above Xbox_GetOverlappedResult.
//
// FUNC_AT(000ea821)
int __stdcall Xbox_GetDiskFreeSpaceExA(const char *path, ULARGE_INTEGER *freeBytesAvailable,
                                       ULARGE_INTEGER *totalBytes, ULARGE_INTEGER *totalFreeBytes) {
    char hostPath[512];
    if (!ResolveForOpen(path, hostPath, sizeof(hostPath))) {
        PublishLastError(ERROR_PATH_NOT_FOUND);
        return 0;
    }

    uint64_t available = 0, capacity = 0, free = 0;

    HostSetLastError(0);
    BOOL ok = HostGetDiskFreeSpaceEx(hostPath, &available, &capacity, &free);
    if (!ok)
        ok = HostGetDiskFreeSpaceEx(NULL, &available, &capacity, &free);   // as above: fall back to this volume

    if (!ok) {
        DWORD err = HostGetLastError();
        PublishLastError(err == ERROR_FILE_NOT_FOUND ? ERROR_PATH_NOT_FOUND : err);
        FILE_LOG("[file] free space %s -> %s : FAILED err %lu\n", path, hostPath, err);
        return 0;
    }

    if (freeBytesAvailable != NULL) freeBytesAvailable->QuadPart = available;
    if (totalBytes != NULL)         totalBytes->QuadPart = capacity;
    if (totalFreeBytes != NULL)     totalFreeBytes->QuadPart = free;

    PublishLastError(0);
    FILE_LOG("[file] free space %s -> %s : %llu free of %llu\n", path, hostPath,
             (unsigned long long)available, (unsigned long long)capacity);
    return 1;
}
