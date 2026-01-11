#include "FS.h"

#include <string.h>
#include <stdio.h>

#include "../util/crc.h"

#define SOME_BUFFER_SIZE 0x13800

#define FLAG_IS_EDL_COMPRESSED (1 << 1)

#pragma pack(push, 1)

typedef struct {
    char unknown[0x18];
    int numEntries;
    int numFilesysFiles;
    char unknown2[0x28-8-0x18];
} MaybeArchiveHeader;

typedef struct {
    uint32_t nameCrc;
    uint32_t dataCrc;
    uint32_t unknown1;
    uint32_t offsetLow;
    uint32_t offsetHigh;
    uint32_t len;
    uint32_t maybeSizeLow;
    uint8_t maybeFlags; // size high?
    uint8_t unknown2;
    char unknown3[8];
} MaybeFileHeader;

static_assert(sizeof(MaybeArchiveHeader) == 0x28, "Bad size for MaybeArchiveHeader");
static_assert(sizeof(MaybeFileHeader) == 0x26, "Bad size for MaybeFileHeader");

typedef struct {
    char PathNormalisationTable[256];
    char ramCache[SOME_BUFFER_SIZE];
    MaybeArchiveHeader *maybeArchiveHeader;
    MaybeFileHeader *maybeFileHeader;
    int filesysHandles[64];
    int CacheFileHandleIdx;
    char maybeNoMorePending;
    char unknown[3];
    int maybeActiveFile;
    int maybeFileLoadState;
    int maybeActiveArchiveNum;
    int maybeNumCompressedBlocks;
    void* someDataPtr;
    char unknown3[12];
    int someDataLen;
    int filesysIdx;
    int someOffset;
    char unknown4[4];
} FileSystem_t;

static_assert(sizeof(FileSystem_t) == 0x13a40, "Bad size for FileSystem_t");

struct IO_STATUS_BLOCK {
    int32_t Status;
    uint32_t field1_0x4;
    uint32_t byteOffsetLow;
    uint32_t byteOffsetHigh;
    HANDLE evtHandle;
};

struct FileOperationXbox {
    uint8_t maybeStatus;
    uint8_t field1_0x1;
    uint8_t field2_0x2;
    uint8_t field3_0x3;
    HANDLE fileHandle;
    struct IO_STATUS_BLOCK ioStatusBlock;
    uint8_t readSucceeded; /* Created by retype action */
    uint8_t field7_0x1d;
    uint8_t field8_0x1e;
    uint8_t field9_0x1f;
    long fileSize;
    uint8_t fileReadMayFail; /* Created by retype action */
    uint8_t field12_0x25;
    uint8_t field13_0x26;
    uint8_t field14_0x27;
};



#pragma pack(pop)

#define FileSystem (*(FileSystem_t*)0x002b0c28)

#define XboxFileOperations ((FileOperationXbox*)0x002c4668)


// Helper - the compiler optimised this substantially, and it has also been inlined in various places
void FS_NormalisePath(char* src, char* dst) {

    while (*src) {
        *dst++ = FileSystem.PathNormalisationTable[*src++];
    }
    *dst = '\0';
}

// Custom calling convention, can't inject
int _FS_MatchFilenameToHeader(char *filename) {

    // Prevent slashes or capitalisation from resulting in a non-match
    char normalisedName[256];
    FS_NormalisePath(filename, normalisedName);

    uint32_t crc = crc32buf((const uint8_t*)normalisedName, strlen(normalisedName));

    // Binary search for the normalised filename through the headers
    int low = 0;
    int high = (FileSystem.maybeArchiveHeader)->numEntries - 1;
    if (high >= 0) {
        do {

            int candidate = (high + low) / 2;
            
            // Match?
            if (FileSystem.maybeFileHeader[candidate].nameCrc == crc) {
                //printf("_FS_MatchFilenameToHeader: Located %s in %i\n", normalisedName, candidate);
                return candidate;
            }

            // Adjust bounds and repeat
            if (FileSystem.maybeFileHeader[candidate].nameCrc < crc) {
                low = candidate + 1;
            }
            else {
                high = candidate - 1;
            }

        } while (low <= high);
    }
    printf("_FS_MatchFilenameToHeader: %s not found!\n", normalisedName);
    return -1;
}

// AUTOLTCG
int __declspec(naked) FS_MatchFilenameToHeader(char* filename) {
    // The original function is provided with its parameter in EAX, but we need to call our reimplementation
    // which doesn't have custom calling convention
    _asm {
        push eax
        call _FS_MatchFilenameToHeader
        add esp, 4
        ret
    }

}

// AUTOGEN
int maybeToLower(int characterIn);

// AUTOGEN
int openOrCreateFile(char *nameRelated,char shareAccess); // "shareAccess" might actually be "createIfNotExists"

// AUTOGEN
int readFromFileBlocking(HANDLE fileHandle, void* buffer, ULONG len, undefined4 *error_code, IO_STATUS_BLOCK *param_5);

void call_maybeReadFile(void *fileOut, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 idx) {
    __asm {
        // Move idx into EDI as required
        mov     edi, idx

        // Push stack arguments right-to-left
        push    param_4
        push    param_3
        push    param_2
        push    fileOut

        // Call the original function
        mov ecx, 0x000E26E0
        call ecx

        // Clean up stack (4 args × 4 bytes)
        add     esp, 16
    }
}

bool call_FS_OperationInProgress(undefined4 idx)
{
    bool result;

    __asm {
        // argument in EAX
        mov     eax, idx

        // call original function
        mov ecx,    0x000e2650
        call ecx
        // AL contains the boolean return value
        mov     result, al
    }

    return result != 0;
}


// AUTOINJECT
void FS_Init(void) {

    // Initialise to zero
    memset(&FileSystem, 0, sizeof(FileSystem));

    // Set up the path normalisation table
    // This lookup converts to lowercase, and uses backslash for separators
    for(int i = 0; i < 256; i++) {
        FileSystem.PathNormalisationTable[i] = i < 128 ? maybeToLower(i) : i;
    }
    FileSystem.PathNormalisationTable['/'] = '\\';

    // ??
    FileSystem.maybeArchiveHeader = (MaybeArchiveHeader*)FileSystem.ramCache;
    FileSystem.maybeFileHeader = (MaybeFileHeader*)(FileSystem.ramCache+0x20);
    FileSystem.maybeFileLoadState = 0;

    // Open all the on-disc files
    int filesysAt = 0;
    do {
        char filename[256];
        snprintf(filename, sizeof(filename), "d:\\eurocom\\filesys.d%02d", filesysAt);
        
        printf("Opening %s\n", filename);                                            
        FileSystem.filesysHandles[filesysAt] = openOrCreateFile(filename, 0);
        
        // The first filesys file contains the metadata - load it
        if(filesysAt == 0) {
            do {
                /* This corresponds with the number of 0 bytes in filesys.dxx */
                call_maybeReadFile(FileSystem.ramCache, 0x13800, 0, 0, FileSystem.filesysHandles[0]);
                
                // Await completion of the file operation
                while(call_FS_OperationInProgress(FileSystem.filesysHandles[0]))
                    ;

            } while ((XboxFileOperations[FileSystem.filesysHandles[0]].maybeStatus == 0) ||
                    (XboxFileOperations[FileSystem.filesysHandles[0]].field1_0x1 == 0));

            
        }
        filesysAt++;
    } while(filesysAt < (FileSystem.maybeArchiveHeader)->numFilesysFiles);

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

// AUTOGEN
void maybeEDL_DecompressSection(int numBlocks, uint32_t *param_2);

// AUTOGEN
bool FS_OpInProgressWithCleanup(void);

// AUTOGEN
void maybeFatalErrorHandler(void);

// Not autoinjected - calling convention is mangled in real code
void FS_ReadFromActualFile(undefined4 len, void *fileOut, undefined4 offsetLow, undefined4 offsetHigh, uint idx) {
    FileSystem.maybeActiveFile = idx;
    call_maybeReadFile(fileOut, len, offsetLow, offsetHigh, idx);
}

// AUTOINJECT
bool FS_StateMachineIterate(void) {

    if(FileSystem.maybeFileLoadState == FLSM_FINISHED)
        return false;

    // If no operation is in progress, but we're not in the FINISHED state, we need to set up
    // the next operation.
    if((FileSystem.maybeFileLoadState == FLSM_BEGIN_LOADING) || !FS_OpInProgressWithCleanup()) {

        uint32_t crc;

        switch(FileSystem.maybeFileLoadState) {

            case FLSM_BEGIN_LOADING:
                // TODO: Cache checks would go here if we cared about caching
                FileSystem.maybeFileLoadState = FLSM_LOAD_FROM_DISC;
                return true;

            case FLSM_LOAD_FROM_DISC:
                FS_ReadFromActualFile(FileSystem.someDataLen, FileSystem.someDataPtr, FileSystem.someOffset, 0, FileSystem.filesysHandles[FileSystem.filesysIdx]);
                FileSystem.maybeFileLoadState = FLSM_CHECK_CRC;
                return true;
            
            case FLSM_CHECK_CRC:
                crc = crc32buf((uint8_t*)FileSystem.someDataPtr, FileSystem.someDataLen);
                if (FileSystem.maybeFileHeader[FileSystem.maybeActiveArchiveNum].dataCrc != crc) {
                    NF_ASSERT(false, "CRC mismatch found in file loader state machine");
                    maybeFatalErrorHandler();
                }
                FileSystem.maybeFileLoadState = FileSystem.maybeNoMorePending ? FLSM_MAYBE_CACHE_FILE_WRITE : FLSM_LOAD_FROM_DISC;
                return true;

            case FLSM_MAYBE_CACHE_FILE_WRITE:
            case FLSM_MAYBE_CACHE_HEADER_WRITE: // Intentional fallthrough
                // TODO: These cases need to be split and additional logic added if caching
                FileSystem.maybeFileLoadState = FLSM_DECOMPRESS_IF_REQD;
                return true;

            case FLSM_DECOMPRESS_IF_REQD:
                if ((FileSystem.maybeFileHeader[FileSystem.maybeActiveArchiveNum].maybeFlags & FLAG_IS_EDL_COMPRESSED)) {
                    maybeEDL_DecompressSection(FileSystem.maybeNumCompressedBlocks, (uint32_t*)FileSystem.someDataPtr);
                }
                FileSystem.maybeFileLoadState = FLSM_FINISHED;
                return true;
                            


        }        

        return true;

    }
}
