#include "FS.h"

#include <string.h>
#include <stdio.h>

#include "../util/crc.h"

#define SOME_BUFFER_SIZE 0x13800

#pragma pack(push, 1)

typedef struct {
    char unknown[0x18];
    int numEntries;
    char unknown2[0x28-4-0x18];
} MaybeArchiveHeader;

typedef struct {
    uint32_t nameCrc;
    uint32_t dataCrc;
    //...
    char unknown[0x26-8];
} MaybeFileHeader;

static_assert(sizeof(MaybeArchiveHeader) == 0x28, "Bad size for MaybeArchiveHeader");
static_assert(sizeof(MaybeFileHeader) == 0x26, "Bad size for MaybeFileHeader");

typedef struct {
    char PathNormalisationTable[256];
    char ramCache[SOME_BUFFER_SIZE];
    MaybeArchiveHeader *maybeArchiveHeader;
    MaybeFileHeader *maybeFileHeader;
} FileSystem_t;

#pragma pack(pop)

#define FileSystem (*(FileSystem_t*)0x002b0c28)


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