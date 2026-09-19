#include <windows.h>

#include "XboxPaths.h"
#include "XboxSettings.h"
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

// ---------------------------------------------------------------------------------------------------------------
// Which file a handle belongs to, and saying so when a read fails.
//
// The game's response to a failed read is FS_FatalErrorHandler - the "there's a problem with the disc you're
// using, it may be dirty or damaged" screen, which never returns and says nothing about what went wrong. That
// has now cost time twice, so the two places that can lead to it report themselves on the way past, whatever
// the verbose setting. The conditions are not guesses: maybeReadFile calls the fatal handler when a read
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
           (unsigned)(uintptr_t)handle, offset, length, err);
    printf("[file] The game treats this as a damaged disc and shows its fatal error\n"
           "[file] screen, which never returns. The error code above is the real cause.\n");
    printf("[file] ---------------------------------------------------------------\n\n");
    fflush(stdout);
}

// Streaming I/O accounting - see XboxFile_ReportStreamingIfDue at the end of this file for what these are
// for. Declared here because the read path below is what increments them.
static uint32_t g_readsPending = 0;      // deferred by the host, which is what the game is designed around
static uint32_t g_readsSynchronous = 0;  // completed before ReadFile returned
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
    RememberHandle(handle, filename);
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
    if (!ok) {
        if (err == ERROR_IO_PENDING) {
            g_readsPending++;        // the host deferred it, which is what lets the game overlap
            g_readBytes += len;      // requested rather than delivered: the count is not known yet
        } else {
            g_readsFailed++;
            ReportFatalFileError("an overlapped read", fileHandle, overlapped->Offset, len, err);
        }
        return 0;            // including ERROR_IO_PENDING, which the caller checks for
    }
    g_readsSynchronous++;
    g_readBytes += read;     // known already, because it finished before returning

    // The read completed there and then rather than going pending. Windows has already filled these in; some
    // other host may not, and the caller reads the byte count out of this structure rather than from us, so
    // make sure it says what happened. Writing them after a *pending* read would be a bug, which is why this
    // is only on the synchronous-success path.
    overlapped->Internal = 0;                      // STATUS_SUCCESS
    overlapped->InternalHigh = (ULONG_PTR)read;

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
    if (!ok && err != ERROR_IO_INCOMPLETE && err != ERROR_IO_PENDING)
        ReportFatalFileError("waiting for an overlapped read", fileHandle, overlapped->Offset, 0, err);
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
    ForgetHandle(handle);

    // A directory enumeration handed out a Win32 find handle, which CloseHandle would reject - see
    // Xbox_FindFirstFileA. Everything else is an ordinary file handle.
    if (CloseIfFindHandle(handle)) {
        PublishLastError(0);
        return 1;
    }

    SetLastError(0);
    BOOL ok = CloseHandle(handle);
    PublishLastError(GetLastError());
    return ok != FALSE ? 1 : 0;
}

// WriteFile. Mirrors readFromFileBlocking exactly, including the pending case - see there.
//
// AUTOINJECT
int __stdcall FileWrite(HANDLE fileHandle, void *buffer, uint32_t len,
                        uint32_t *bytesWritten, OVERLAPPED *overlapped) {
    if (bytesWritten != NULL)
        *bytesWritten = 0;

    if (overlapped == NULL) {
        DWORD written = 0;
        SetLastError(0);
        BOOL ok = WriteFile(fileHandle, buffer, len, &written, NULL);
        DWORD err = GetLastError();
        FILE_LOG("[file] write sync handle 0x%08x len %u -> %s written %lu err %lu\n",
                 (unsigned)(uintptr_t)fileHandle, len, ok ? "ok" : "FAILED", written, err);
        PublishLastError(err);
        if (!ok)
            return 0;
        if (bytesWritten != NULL)
            *bytesWritten = written;
        return 1;
    }

    overlapped->Internal = (ULONG_PTR)STATUS_PENDING;
    overlapped->InternalHigh = 0;

    DWORD written = 0;
    SetLastError(0);
    BOOL ok = WriteFile(fileHandle, buffer, len, &written, overlapped);
    DWORD err = GetLastError();
    FILE_LOG("[file] write async handle 0x%08x len %u off %lu -> %s written %lu err %lu\n",
             (unsigned)(uintptr_t)fileHandle, len, overlapped->Offset,
             ok ? "ok" : (err == ERROR_IO_PENDING ? "pending" : "FAILED"), written, err);
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
    SetLastError(0);
    BOOL ok = FlushFileBuffers(fileHandle);
    PublishLastError(GetLastError());
    return ok != FALSE ? 1 : 0;
}

// SetEndOfFile. The original reads the current position (FilePositionInformation) and writes it to both
// EndOfFileInformation and AllocationInformation, which is what SetEndOfFile does.
//
// AUTOINJECT
int __stdcall setSomeInfo(HANDLE fileHandle) {
    SetLastError(0);
    BOOL ok = SetEndOfFile(fileHandle);
    PublishLastError(GetLastError());
    return ok != FALSE ? 1 : 0;
}

// SetFilePointer, including its awkward return convention: the new low word, or 0xFFFFFFFF for failure - so a
// genuine position of 0xFFFFFFFF is distinguished by the last-error code being zero, which is why the success
// path still publishes one.
//
// AUTOINJECT
uint32_t __stdcall querySetSomeInfo(HANDLE fileHandle, uint32_t distanceLow, uint32_t *distanceHigh,
                                    uint32_t moveMethod) {
    SetLastError(0);
    DWORD result = SetFilePointer(fileHandle, (LONG)distanceLow, (PLONG)distanceHigh, moveMethod);
    PublishLastError(GetLastError());
    return result;
}

// SetFilePointerEx. The out parameter is a 64-bit position written as two dwords.
//
// FUNC_AT(000e9731)
int __stdcall Xbox_SetFilePointerEx(HANDLE fileHandle, uint32_t distanceLow, uint32_t distanceHigh,
                                    uint32_t *newPosition, uint32_t moveMethod) {
    LARGE_INTEGER distance;
    distance.LowPart = distanceLow;
    distance.HighPart = (LONG)distanceHigh;

    LARGE_INTEGER newPos;
    newPos.QuadPart = 0;

    SetLastError(0);
    BOOL ok = SetFilePointerEx(fileHandle, distance, &newPos, moveMethod);
    PublishLastError(GetLastError());
    if (!ok)
        return 0;

    if (newPosition != NULL) {
        newPosition[0] = newPos.LowPart;
        newPosition[1] = (uint32_t)newPos.HighPart;
    }
    return 1;
}

// DeleteFileA. The original opens with DELETE access, sets FileDispositionInformation and closes, which is
// what DeleteFile does. Takes an Xbox path, so it resolves like createFile - GetPTPData and
// WriteStateFileAndLaunch both use it on "z:\\state.bin".
//
// AUTOINJECT
int __stdcall MaybeFileCreateNew(const char *filename) {
    char hostPath[512];
    if (!ResolveForOpen(filename, hostPath, sizeof(hostPath))) {
        PublishLastError(ERROR_FILENAME_EXCED_RANGE);
        return 0;
    }
    SetLastError(0);
    BOOL ok = DeleteFileA(hostPath);
    DWORD err = GetLastError();
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
// nothing has reached them yet: 0x000e9f4d, 0x000ea821, 0x000eac45, 0x000eb34c, 0x000eb3b8, 0x000eb446,
// 0x000eb5c3, 0x000ed2e8, 0x000ee0d4 - less 0x000eac45, which Linux reached. Each will announce itself
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

    SetLastError(0);
    DWORD attributes = GetFileAttributesA(hostPath);
    DWORD err = GetLastError();
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
// rest of this file. What makes them easy is that the structure they fill in is Win32's: the conversion at
// 0x000eb68a writes attributes at 0, three FILETIMEs at 4, 0xc and 0x14, the size high and low words at 0x1c
// and 0x20, the name at 0x2c and a terminator at 0x130 - which is WIN32_FIND_DATAA exactly, reserved fields
// and alternate name included. So the data goes straight across with no translation at all.
//
// The one awkwardness is the handle. On the Xbox a find handle is an ordinary NT file handle, and the caller
// closes it with NtClose - which arrives here as DoNtClose. A Win32 find handle is not a file handle and must
// be closed with FindClose, so the ones handed out here are remembered and DoNtClose checks that list first.
// ---------------------------------------------------------------------------------------------------------------

#define TRACKED_FINDS 16

static HANDLE g_findHandles[TRACKED_FINDS];

static void RememberFind(HANDLE handle) {
    for (int i = 0; i < TRACKED_FINDS; i++) {
        if (g_findHandles[i] == NULL) { g_findHandles[i] = handle; return; }
    }
}

// True if this was one of ours, in which case it has also been closed and forgotten.
static bool CloseIfFindHandle(HANDLE handle) {
    for (int i = 0; i < TRACKED_FINDS; i++) {
        if (g_findHandles[i] == handle) {
            g_findHandles[i] = NULL;
            FindClose(handle);
            return true;
        }
    }
    return false;
}

// FUNC_AT(000eb6f6)
uint32_t __stdcall Xbox_FindFirstFileA(const char *filename, WIN32_FIND_DATAA *findData) {
    char hostPath[512];
    if (!ResolveForOpen(filename, hostPath, sizeof(hostPath))) {
        PublishLastError(ERROR_FILENAME_EXCED_RANGE);
        return 0xFFFFFFFFu;
    }

    SetLastError(0);
    HANDLE handle = FindFirstFileA(hostPath, findData);
    DWORD err = GetLastError();
    FILE_LOG("[file] find first %s -> %s : handle 0x%08x err %lu\n", filename, hostPath,
             (unsigned)(uintptr_t)handle, err);

    if (handle == INVALID_HANDLE_VALUE) {
        // An empty directory is an ordinary answer here, not a failure - the caller is enumerating.
        PublishLastError(err != 0 ? err : ERROR_FILE_NOT_FOUND);
        return 0xFFFFFFFFu;
    }

    RememberFind(handle);
    PublishLastError(0);
    return (uint32_t)(uintptr_t)handle;
}

// Returns int rather than bool for the reason given above Xbox_GetOverlappedResult: the original ends in
// "XOR EAX,EAX / INC EAX" or a bare "XOR EAX,EAX", and its caller tests the whole of EAX.
//
// FUNC_AT(000eb803)
int __stdcall Xbox_FindNextFileA(HANDLE findHandle, WIN32_FIND_DATAA *findData) {
    SetLastError(0);
    BOOL ok = FindNextFileA(findHandle, findData);
    DWORD err = GetLastError();
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
// polls with GetOverlappedResult until the data arrives. That only overlaps anything if the host actually
// defers the read. Windows does. A host that completes every overlapped read synchronously is correct but
// serialises the game - the main thread blocks for the whole read instead of rendering during it - and the
// symptom is a frame rate that is low but perfectly steady, and only in the parts of the game that stream
// continuously. A level that is entirely loaded, such as a multiplayer map with bots, looks fine.
//
// So the useful thing to know is the ratio, and it is two counters. Enabled by PerfLog in settings.ini.
// ---------------------------------------------------------------------------------------------------------------


void XboxFile_ReportStreamingIfDue(void) {
    if (!Settings_GetPerfLog())
        return;

    static uint32_t lastTick = 0;
    uint32_t now = GetTickCount();
    if (lastTick == 0)
        lastTick = now;
    if (now - lastTick < 5000)
        return;
    lastTick = now;

    uint32_t total = g_readsPending + g_readsSynchronous;
    if (total == 0)
        return;

    printf("[perf] disc reads: %u deferred, %u completed synchronously (%u%%), %u failed, %llu KB\n",
           g_readsPending, g_readsSynchronous, (unsigned)((g_readsSynchronous * 100ull) / total),
           g_readsFailed, (unsigned long long)(g_readBytes / 1024));
    if (g_readsSynchronous > g_readsPending) {
        printf("[perf]   most reads are not being deferred, so the game is not overlapping them with\n"
               "[perf]   drawing - it stalls for each one. That is a host behaviour, not the game's.\n");
    }
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
    SetLastError(0);
    BOOL ok = GetDiskFreeSpaceA(hostPath, &sectorsPerCluster, &bytesPerSector,
                                &freeClusters, &totalClusters);
    if (!ok)
        ok = GetDiskFreeSpaceA(NULL, &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters);

    if (!ok) {
        DWORD err = GetLastError();
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
