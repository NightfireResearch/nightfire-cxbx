#include "FS.h"
#include "../../common/xbeClass.h"
#include "XboxFile.h"
#include "XboxStartup.h"   // Xbox_GetLastError
#include "XboxError.h"
#include "EDL.h"
#include "CRuntime.h"
#include "../memory.h"
#include "../game.h"          // timestamp
#include "../gfx/LowLevel.h"  // ShowLoadProgressScreen

#include <string.h>
#include <stdio.h>

#include "../util/crc.h"

#define FS_HEADER_CACHE_SIZE 0x13800   // the archive header and the file entries

#define FLAG_IS_EDL_COMPRESSED (1 << 1)

#pragma pack(push, 1)

typedef struct {
    char unknown[0x18];
    int numEntries;
    int numFilesysFiles;
    char unknown2[0x28-8-0x18];
} ArchiveHeader;

typedef struct {
    uint32_t nameCrc;
    uint32_t dataCrc;
    uint32_t unknown1;
    uint32_t offsetLow;
    uint32_t offsetHigh;
    uint32_t compressedLen;
    uint32_t uncompressedLen;
    uint8_t flags; // FLAG_IS_EDL_COMPRESSED
    uint8_t unknown2;
    uint32_t filesysIdx; // The filesys.dxx archive which contains this file
    uint32_t offsetOfFileWithinArchive;
} FileEntry;

static_assert(sizeof(ArchiveHeader) == 0x28, "Bad size for ArchiveHeader");
static_assert(sizeof(FileEntry) == 0x26, "Bad size for FileEntry");

typedef struct {
    char PathNormalisationTable[256];
    char headerCache[FS_HEADER_CACHE_SIZE];
    ArchiveHeader *archiveHeader;
    FileEntry *fileEntries;
    int filesysHandles[64];
    int CacheFileHandleIdx;
    char readSucceeded;
    char unknown[3];
    int activeSlot;
    int fileLoadState;
    int fileIdx;   // the file being loaded: its index in fileEntries
    void* compressedDataPtr;     // the caller's buffer, where the file ends up
    void* readPtr;               // where the file is read to: the buffer, or its tail when it is compressed
    int field_0x13a24;
    uint32_t offsetLow;          // the file's offset in the archive set (unused once read by archive)
    uint32_t offsetHigh;
    int readLength;              // the bytes read and checked: the compressed length
    int filesysIdx;
    int offsetOfFileWithinArchive;
    char unknown4[4];
} FileSystem_t;

LAYOUT_32(sizeof(FileSystem_t) == 0x13a40, "Bad size for FileSystem_t");

// One open file. The read is overlapped: the OVERLAPPED is Xbox's IO_STATUS_BLOCK under its Win32 name.
struct FileOperationXbox {
    uint8_t open;             // 0x00 the slot is open
    uint8_t succeeded;        // 0x01 the last operation succeeded
    uint8_t field2_0x2;
    uint8_t field3_0x3;
    HANDLE fileHandle;        // 0x04
    OVERLAPPED overlapped;    // 0x08
    uint8_t readPending;      // 0x1c a read has been issued and not yet reaped
    uint8_t field7_0x1d;
    uint8_t field8_0x1e;
    uint8_t field9_0x1f;
    int32_t fileSize;         // 0x20
    uint8_t fileReadMayFail;  // 0x24 not on the disc (a cache or save drive): failing is not fatal
    uint8_t field12_0x25;
    uint8_t field13_0x26;
    uint8_t field14_0x27;
};
LAYOUT_32(sizeof(FileOperationXbox) == 0x28, "FileOperationXbox must be 0x28 bytes");

#pragma pack(pop)

// XBE_GLOBAL(0x002b0c28, 0x13a40)
static FileSystem_t FileSystem;

// Slot 0 is never used: 0 is "no file" to every caller
#define MAX_FILE_OPERATIONS 64
// XBE_GLOBAL(0x002c4668, 0xa00)
static FileOperationXbox XboxFileOperations[MAX_FILE_OPERATIONS];

// The streams the sound code reads asynchronously (psiAsyncOpenFile and friends): which archive file each is.
// Slot 0 is never used.
struct AsyncStream {
    uint8_t open;
    uint8_t pad[3];
    int fileIdx;              // index in FileSystem.fileEntries
};
#define MAX_ASYNC_STREAMS 16
// XBE_GLOBAL(0x002c5068, 0x80)
static AsyncStream AsyncStreams[MAX_ASYNC_STREAMS];

// Helper - the compiler optimised this substantially, and it has also been inlined in various places
void FS_NormalisePath(char* src, char* dst) {

    while (*src) {
        *dst++ = FileSystem.PathNormalisationTable[*src++];
    }
    *dst = '\0';
}

// Custom calling convention, can't inject
int FS_MatchFilenameToHeader(char *filename) {

    // Prevent slashes or capitalisation from resulting in a non-match
    char normalisedName[256];
    FS_NormalisePath(filename, normalisedName);

    uint32_t crc = crc32buf((const uint8_t*)normalisedName, strlen(normalisedName));

    // Binary search for the normalised filename through the headers
    int low = 0;
    int high = (FileSystem.archiveHeader)->numEntries - 1;
    if (high >= 0) {
        do {

            int candidate = (high + low) / 2;
            
            // Match?
            if (FileSystem.fileEntries[candidate].nameCrc == crc) {
                //printf("FS_MatchFilenameToHeader: Located %s in %i\n", normalisedName, candidate);
                return candidate;
            }

            // Adjust bounds and repeat
            if (FileSystem.fileEntries[candidate].nameCrc < crc) {
                low = candidate + 1;
            }
            else {
                high = candidate - 1;
            }

        } while (low <= high);
    }
    printf("FS_MatchFilenameToHeader: %s not found!\n", normalisedName);
    return -1;
}

// ---------------------------------------------------------------------------------------------------------------
// The open-file table (0x000e24a0-0x000e2900). Reads are overlapped: FS_ReadFile issues one and
// FS_OperationInProgress polls it. The originals take their slot in a register (EAX, EDI); every caller is here
// now, so these are plain functions and the originals are dead.
// ---------------------------------------------------------------------------------------------------------------

// Opens a file in a free slot: read-only and shared, or (write) read-write and created if missing. Up to four
// tries; then a disc file is fatal, and anything else returns 0. (0x000e24a0)
// AUTOINJECT
int openOrCreateFile(char *nameRelated, char write) {
    int idx = 1;
    while (idx < MAX_FILE_OPERATIONS && XboxFileOperations[idx].open != 0)
        idx++;
    if (idx >= MAX_FILE_OPERATIONS)
        return 0;
    FileOperationXbox *op = &XboxFileOperations[idx];

    op->fileReadMayFail = !(nameRelated[0] == 'D' || nameRelated[0] == 'd');
    char fname[256];
    FS_NormalisePath(nameRelated, fname);

    for (int tries = 0;; tries++) {
        op->fileHandle = write ? createFile(fname, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, 0x60000000, NULL)
                               : createFile(fname, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0x60000000, NULL);
        if (op->fileHandle != INVALID_HANDLE_VALUE)
            break;
        if (tries > 2) {
            if (op->fileReadMayFail)
                return 0;
            FS_FatalErrorHandler();
        }
    }
    LARGE_INTEGER size;
    op->fileSize = getFileSize_LargeInteger(op->fileHandle, &size) ? (int32_t)size.LowPart : -1;
    op->open = 1;
    op->succeeded = 1;
    return idx;
}

// True while the slot's read is still in flight. Reaps it once done; a failed read of a disc file is fatal.
// (0x000e2650)
static bool FS_OperationInProgress(int idx) {
    FileOperationXbox *op = &XboxFileOperations[idx];
    if (op->open == 0 || op->readPending == 0)
        return false;
    uint32_t transferred = 0;
    int done = Xbox_GetOverlappedResult(op->fileHandle, &op->overlapped, &transferred, 0);
    op->readPending = done == 0;
    op->succeeded = 1;
    if (op->readPending) {
        DWORD err = Xbox_GetLastError();
        if (err == ERROR_IO_PENDING || err == ERROR_IO_INCOMPLETE)
            return true;
        op->readPending = 0;
        op->succeeded = 0;
        if (!op->fileReadMayFail)
            FS_FatalErrorHandler();
    }
    return false;
}

// Issues a read into the slot, after waiting out any read still in flight. (0x000e26e0)
static void FS_ReadFile(void *fileOut, uint32_t len, uint32_t offsetLow, uint32_t offsetHigh, int idx) {
    FileOperationXbox *op = &XboxFileOperations[idx];
    if (op->open == 0)
        return;
    while (FS_OperationInProgress(idx))
        ;
    memset(&op->overlapped, 0, sizeof(op->overlapped));
    op->overlapped.Offset = offsetLow;
    op->overlapped.OffsetHigh = offsetHigh;
    int ok = readFromFileBlocking(op->fileHandle, fileOut, len, NULL, &op->overlapped);
    op->readPending = !ok && Xbox_GetLastError() == ERROR_IO_PENDING;
    if (!op->fileReadMayFail && !ok && Xbox_GetLastError() != ERROR_IO_PENDING)
        FS_FatalErrorHandler();
}

// The archive path of a file in the eurocom folder on the disc, in a static buffer. Language_Get uses it for
// config.txt. (0x000e2840)
// XBE_GLOBAL(0x002c50e8, 0x100)
static char EurocomPath[256];
char* FS_EurocomPath(const char *name) {
    strcpy(EurocomPath, "d:\\eurocom\\");
    strcat(EurocomPath, name);
    return EurocomPath;
}

// AUTOINJECT
void FS_Init(void) {

    // Initialise to zero
    memset(&FileSystem, 0, sizeof(FileSystem));

    // Set up the path normalisation table
    // This lookup converts to lowercase, and uses backslash for separators
    for(int i = 0; i < 256; i++) {
        FileSystem.PathNormalisationTable[i] = i < 128 ? Crt::tolower(i) : i;
    }
    FileSystem.PathNormalisationTable['/'] = '\\';

    FileSystem.archiveHeader = (ArchiveHeader*)FileSystem.headerCache;
    FileSystem.fileEntries = (FileEntry*)(FileSystem.headerCache+0x20); // File list starts 32 bytes into the file 
    FileSystem.fileLoadState = 0;

    // Open all the on-disc files
    int filesysAt = 0;
    do {
        char filename[256];
        snprintf(filename, sizeof(filename), "d:\\eurocom\\filesys.d%02d", filesysAt);
        
        printf("Opening %s\n", filename);                                            
        FileSystem.filesysHandles[filesysAt] = openOrCreateFile(filename, 0);
        
        if(filesysAt == 0) {
            do {
                // Load the KXF header / metadata (only found in the first of the files)
                FS_ReadFile(FileSystem.headerCache, FS_HEADER_CACHE_SIZE, 0, 0, FileSystem.filesysHandles[0]);
                
                // Await completion of the file operation
                while(FS_OperationInProgress(FileSystem.filesysHandles[0]))
                    ;

            } while ((XboxFileOperations[FileSystem.filesysHandles[0]].open == 0) ||
                    (XboxFileOperations[FileSystem.filesysHandles[0]].succeeded == 0));

            
        }
        filesysAt++;
    } while(filesysAt < (FileSystem.archiveHeader)->numFilesysFiles);

    // TODO: Implement filesystem cache and anything else that follows
    // This seems to be a cache on the Z: drive to improve loading speed on Xbox hardware?
    // Irrelevant in the modern context where the "DVD" is an ISO sitting on an SSD.
    FileSystem.CacheFileHandleIdx = 0;

}

typedef enum {
    FLSM_FINISHED = 0,
    FLSM_BEGIN_LOADING = 1,
    FLSM_DECOMPRESS_IF_REQD = 2,
    FLSM_LOAD_FROM_DISC = 3,
    FLSM_CHECK_CRC = 4,
    FLSM_MAYBE_CACHE_FILE_WRITE = 5,
    FLSM_MAYBE_CACHE_HEADER_WRITE = 6,
    FLSM_CACHE_FILE = 7,
    FLSM_CACHED_FILE = 8,
    FLSM_LOAD_FROM_CACHE = 9,
    FLSM_A,
    FLSM_B,
    FLSM_C
} FileSystemStateMachine;

// Polls the loader's current read; once it is done, notes whether it succeeded, and drops the cache file's slot if
// that was what failed. (0x000e28a0)
// AUTOINJECT
bool FS_OpInProgressWithCleanup(void) {
    if (FS_OperationInProgress(FileSystem.activeSlot))
        return true;
    FileOperationXbox *op = &XboxFileOperations[FileSystem.activeSlot];
    FileSystem.readSucceeded = op->open != 0 ? op->succeeded : 0;
    if (FileSystem.readSucceeded == 0 && FileSystem.activeSlot == FileSystem.CacheFileHandleIdx)
        FileSystem.CacheFileHandleIdx = 0;
    return false;
}

// The original takes its arguments in registers (0x000e2a80)
static void FS_ReadFromActualFile(uint32_t len, void *fileOut, uint32_t offsetLow, uint32_t offsetHigh, int idx) {
    FileSystem.activeSlot = idx;
    FS_ReadFile(fileOut, len, offsetLow, offsetHigh, idx);
}

// AUTOINJECT
bool FS_StateMachineIterate(void) {

    // Reap any operation that has finished, before deciding there is nothing to do. This call is what
    // completes a deferred read - the name says so - and it has to happen even in the FINISHED state,
    // because that is exactly the state the machine sits in while it waits for one.
    //
    // Taking it after the early-out below, or behind the short-circuit in the condition, hangs the game:
    // ShowLoadProgressScreen (0x000dc8a0) spins on this function, the read it is waiting for is never
    // reaped, the state stays FLSM_FINISHED and the loading screen never ends. That reproduces reliably
    // in the macOS cross build; the last MSVC build to hand does not show it, so the two compilers land
    // differently on what is a latent bug either way. Worth checking against the original at 0x000e2d90
    // in Ghidra, which is the reference for what this should do.
    bool opInProgress = FS_OpInProgressWithCleanup();

    if(FileSystem.fileLoadState == FLSM_FINISHED)
        return false;

    // If no operation is in progress, but we're not in the FINISHED state, we need to set up
    // the next operation.
    if((FileSystem.fileLoadState == FLSM_BEGIN_LOADING) || !opInProgress) {

        uint32_t crc;

        switch(FileSystem.fileLoadState) {

            case FLSM_BEGIN_LOADING:
                // TODO: Cache checks would go here if we cared about caching
                FileSystem.fileLoadState = FLSM_LOAD_FROM_DISC;
                return true;

            case FLSM_LOAD_FROM_DISC:
                FS_ReadFromActualFile(FileSystem.readLength, FileSystem.readPtr, FileSystem.offsetOfFileWithinArchive,
                                      0, FileSystem.filesysHandles[FileSystem.filesysIdx]);
                FileSystem.fileLoadState = FLSM_CHECK_CRC;
                return true;
            
            case FLSM_CHECK_CRC:
                crc = crc32buf((uint8_t*)FileSystem.readPtr, FileSystem.readLength);
                if (FileSystem.fileEntries[FileSystem.fileIdx].dataCrc != crc) {
                    NF_ASSERT(false, "CRC mismatch found in file loader state machine");
                    FS_FatalErrorHandler();
                }
                FileSystem.fileLoadState = FileSystem.readSucceeded ? FLSM_MAYBE_CACHE_FILE_WRITE : FLSM_LOAD_FROM_DISC;
                return true;

            case FLSM_MAYBE_CACHE_FILE_WRITE:
            case FLSM_MAYBE_CACHE_HEADER_WRITE: // Intentional fallthrough
                // TODO: These cases need to be split and additional logic added if caching
                FileSystem.fileLoadState = FLSM_DECOMPRESS_IF_REQD;
                return true;

            case FLSM_DECOMPRESS_IF_REQD:
                if ((FileSystem.fileEntries[FileSystem.fileIdx].flags & FLAG_IS_EDL_COMPRESSED)) {
                    EDL_DecompressSection((char*)FileSystem.compressedDataPtr, (edl_section_t*)FileSystem.readPtr);
                }
                FileSystem.fileLoadState = FLSM_FINISHED;
                return true;
                            

        }        

        return true;

    }

    // An operation is still in progress, so the state machine has not finished and the caller has to
    // come back. Falling off the end here is undefined behaviour: MSVC happened to leave the right value
    // in EAX and got away with it, and clang does not - the file loader then never completes and the
    // game spins before it can load anything. See docs/macos-build.md.
    return true;
}

// AUTOINJECT
int FS_GetFileSize(char *filename) {

    int idx = FS_MatchFilenameToHeader(filename);
    if (idx == -1) {
        FS_FatalErrorHandler();
    }

    return (FileSystem.fileEntries[idx].uncompressedLen + (FileSystem.fileEntries[idx].flags & FLAG_IS_EDL_COMPRESSED ? 0x800 : 0));
}

// Starts loading a file into buffer, if the loader is idle (FS_StateMachineIterate then runs it). A compressed
// file is read into the tail of the buffer and decompressed down to its start. (0x000e3280)
// AUTOINJECT
void FS_LoadFileIfReady(char *fileName, void *buffer) {
    if (FS_StateMachineIterate())
        return;
    int idx = FS_MatchFilenameToHeader(fileName);
    FileSystem.fileIdx = idx;
    if (idx == -1)
        FS_FatalErrorHandler();
    FileEntry *h = &FileSystem.fileEntries[idx];
    FileSystem.fileLoadState = FLSM_BEGIN_LOADING;
    FileSystem.compressedDataPtr = buffer;
    FileSystem.readPtr = buffer;
    if (h->flags & FLAG_IS_EDL_COMPRESSED)
        FileSystem.readPtr = (char *)buffer + (h->uncompressedLen - h->compressedLen) + 0x800;
    FileSystem.offsetLow = h->offsetLow;
    FileSystem.offsetHigh = h->offsetHigh;
    FileSystem.readLength = h->compressedLen;
    FileSystem.filesysIdx = h->filesysIdx;
    FileSystem.offsetOfFileWithinArchive = h->offsetOfFileWithinArchive;
}

// XBE_GLOBAL(0x002adf38, 0x8)
double TimeSpentLoadingFiles;

// Loads a whole file, allocating for it, with the loading screen up meanwhile. A missing file is fatal: it is
// logged to the error screen. (0x000dc990)
// AUTOINJECT
void* FS_AllocateAndLoadBlocking(char *filename, MallocFlags allocType, int *sizeOut) {
    TimeSpentLoadingFiles = TimeSpentLoadingFiles - timestamp();
    int size = FS_GetFileSize(filename);
    if (size < 1) {
        logError("File missing..\n\n%s", filename);
        ShowFatalErrorScreen(true);
        XboxError_Halt("file missing from the archive", filename);
    }
    void *data = Mem_Malloc(size, allocType, 0);
    FS_LoadFileIfReady(filename, data);
    while (FS_StateMachineIterate())
        ShowLoadProgressScreen();
    if (sizeOut != NULL)
        *sizeOut = size;
    TimeSpentLoadingFiles = timestamp() + TimeSpentLoadingFiles;
    return data;
}

// ---------------------------------------------------------------------------------------------------------------
// The sound code's streams (0x000e3100-0x000e3280, and the psiAsync* wrappers at 0x000e0be0): an archive file
// read in pieces with overlapped reads, while the game carries on.
// ---------------------------------------------------------------------------------------------------------------

static FileEntry *StreamHeader(int stream) {
    return &FileSystem.fileEntries[AsyncStreams[stream].fileIdx];
}

// A free stream slot for the named file; running out is fatal. (0x000e3100)
static int FS_StreamOpen(char *fileName) {
    int stream = 1;
    while (stream < MAX_ASYNC_STREAMS && AsyncStreams[stream].open)
        stream++;
    if (stream >= MAX_ASYNC_STREAMS)
        FS_FatalErrorHandler();
    int idx = FS_MatchFilenameToHeader(fileName);
    if (idx == -1)
        FS_FatalErrorHandler();
    AsyncStreams[stream].fileIdx = idx;
    AsyncStreams[stream].open = 1;
    return stream;
}

// AUTOINJECT
int psiAsyncOpenFile(char *fileName) {
    int stream = FS_StreamOpen(fileName);
    return stream != 0 ? stream : -1;
}

// Polls the stream's archive file; true while its read is in flight. (0x000e31f0)
static bool FS_StreamBusy(int stream) {
    return FS_OperationInProgress(FileSystem.filesysHandles[StreamHeader(stream)->filesysIdx]);
}

// AUTOINJECT
void psiAsyncCloseFile(int stream) {
    while (FS_StreamBusy(stream))
        ;
    AsyncStreams[stream].open = 0;
    AsyncStreams[stream].fileIdx = 0;
}

// The file's whole size, for a read of all of it (0x000e3240)
static uint32_t FS_StreamLength(int stream) {
    if (stream < 1)
        return 0;
    FileEntry *h = StreamHeader(stream);
    return ((h->flags & FLAG_IS_EDL_COMPRESSED) ? 0x800 : 0) + h->uncompressedLen;
}

// Issues a read of part of the stream's file (0x000e31a0)
static void FS_StreamRead(int stream, void *dest, uint32_t len, uint32_t offsetLow, uint32_t offsetHigh) {
    if (stream < 1)
        return;
    FileEntry *h = StreamHeader(stream);
    uint64_t offset = (uint64_t)h->offsetOfFileWithinArchive + ((uint64_t)offsetHigh << 32 | offsetLow);
    FS_ReadFile(dest, len, (uint32_t)offset, (uint32_t)(offset >> 32), FileSystem.filesysHandles[h->filesysIdx]);
}

// Reads byteSize bytes at byteOffset into destBuffer, or the whole file for -1. The read is widened to whole 2 KB
// sectors, so it lands up to 2 KB before destBuffer and runs past its end: psiAsyncCreateBuffer leaves room.
// AUTOINJECT
void psiAsyncReadFile(int fileHandle, int byteOffset, int byteSize, void *destBuffer) {
    if (byteSize == -1) {
        FS_StreamRead(fileHandle, destBuffer, FS_StreamLength(fileHandle), 0, 0);
        return;
    }
    uint32_t start = byteOffset & 0xfffff800;
    FS_StreamRead(fileHandle, (char *)destBuffer + (int32_t)(start - byteOffset),
                  ((byteOffset + 0x7ff + byteSize) & 0xfffff800u) - start, start, (uint32_t)(byteOffset >> 31));
}

// AUTOINJECT
int psiAsyncHasReadFinished(int fileHandle) {
    return !FS_StreamBusy(fileHandle);
}
