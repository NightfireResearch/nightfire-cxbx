#include "FileSysShadow.h"

#include "../platform/FileSys.h"
#include "../platform/RefPack.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xboxPath.h"

#include <windows.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_FSSHADOW=1, at injection time: the .viv directory lookup (platform/FileSys.cpp's BIG_find and what it
// calls - the format, the big-endian widths, the footer test, the case-insensitive compare) against the
// original's, 0x0010de40, which is not patched, on the directory of every archive in D:\driving. Every entry is
// looked up by index and by name - as stored, in lower case, and with '/' for '\' - and a few names that are not
// there; the two must agree on the entry found, its offset and its size.
//
// And EA's packer (platform/RefPack.cpp): every packed entry of every archive is unpacked by the original
// (0x0014bff0 and the decoder it calls, 0x0014c220, swapped back in for the call) and by the port, and the sizes
// and bytes compared, along with unpacksizez's answer for every entry, packed or not.
// ---------------------------------------------------------------------------------------------------------------

typedef const char *(*BigFindFn)(const uint8_t *dir, const char *name, int index, int *outOffset, int *outSize);
#define OriginalBigFind ((BigFindFn)0x0010de40u)

static int g_failures, g_lookups, g_packed, g_packedBytes;

typedef unsigned (*UnpackSizeFn)(const uint8_t *packed);
typedef int (*UnpackFn)(const uint8_t *packed, uint8_t *out);

static void CompareUnpack(HANDLE file, int offset, int size, const char *entry, const char *archive) {
    if (size < 6)
        return;
    uint8_t *data = (uint8_t *)malloc((size_t)size + 16);
    memset(data + size, 0, 16);
    DWORD got = 0;
    SetFilePointer(file, offset, NULL, FILE_BEGIN);
    ReadFile(file, data, (DWORD)size, &got, NULL);
    unsigned sizeO;
    {
        XbeOriginalScope original(0x0014c0e0);
        sizeO = ((UnpackSizeFn)0x0014c0e0)(data);
    }
    unsigned sizeP = unpacksizez(data);
    if (sizeO != sizeP && g_failures++ < 20)
        printf("[fsshadow] %s: %s: unpacksizez original %u, port %u\n", archive, entry, sizeO, sizeP);
    if (sizeP != 0 && sizeP < (64u << 20)) {
        uint8_t *outO = (uint8_t *)calloc(sizeP + 64, 1), *outP = (uint8_t *)calloc(sizeP + 64, 1);
        int resultO, resultP;
        {
            XbeOriginalScope unpack(0x0014bff0), decode(0x0014c220);
            resultO = ((UnpackFn)0x0014bff0)(data, outO);
        }
        resultP = UNPACK_unpack(data, outP);
        g_packed++;
        g_packedBytes += (int)sizeP;
        if ((resultO != resultP || memcmp(outO, outP, sizeP + 64) != 0) && g_failures++ < 20)
            printf("[fsshadow] %s: %s: unpacked original %d, port %d bytes, or different\n", archive, entry,
                   resultO, resultP);
        free(outO);
        free(outP);
    }
    free(data);
}

static void Compare(const uint8_t *dir, const char *name, int index, const char *archive) {
    int offsetO = -7, sizeO = -7, offsetP = -7, sizeP = -7;
    const char *o = OriginalBigFind(dir, name, index, &offsetO, &sizeO);
    const char *p = BIG_find(dir, name, index, &offsetP, &sizeP);
    g_lookups++;
    if (o != p || offsetO != offsetP || sizeO != sizeP) {
        if (g_failures++ < 20)
            printf("[fsshadow] %s: %s #%d: original %s +%d/%d, port %s +%d/%d\n", archive, name ? name : "(index)",
                   index, o ? o : "-", offsetO, sizeO, p ? p : "-", offsetP, sizeP);
    }
}

static void CheckArchive(const char *hostPath, const char *archive) {
    HANDLE file = CreateFileA(hostPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    uint8_t header[16];
    DWORD got = 0;
    ReadFile(file, header, sizeof(header), &got, NULL);
    int size = got == sizeof(header) ? BIG_dirsize(header) : 0;
    if (size < 16) {
        printf("[fsshadow] %s: not an archive we know\n", archive);
        CloseHandle(file);
        return;
    }
    uint8_t *dir = (uint8_t *)malloc((size_t)size);
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    ReadFile(file, dir, (DWORD)size, &got, NULL);

    int entries = 0;
    for (;; entries++) {
        Compare(dir, NULL, entries, archive);
        int entryOffset, entrySize;
        const char *entry = BIG_find(dir, NULL, entries, &entryOffset, &entrySize);
        if (entry == NULL)
            break;
        CompareUnpack(file, entryOffset, entrySize, entry, archive);
        char variant[256];
        Compare(dir, entry, 0, archive);
        snprintf(variant, sizeof(variant), "%s", entry);
        for (char *c = variant; *c; c++)
            *c = (char)tolower((unsigned char)*c);
        Compare(dir, variant, 0, archive);
        for (char *c = variant; *c; c++)
            if (*c == '\\')
                *c = '/';
        Compare(dir, variant, 0, archive);
        snprintf(variant, sizeof(variant), "%s.missing", entry);
        Compare(dir, variant, 0, archive);
    }
    Compare(dir, "", 0, archive);
    Compare(dir, "data\\nothing\\here", 0, archive);
    printf("[fsshadow] %s: %d entries\n", archive, entries);
    CloseHandle(file);
    free(dir);
}

void FileSysShadow_Run(void) {
    if (getenv("NIGHTFIRE_FSSHADOW") == NULL)
        return;
    char folder[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
    if (!Xbox_ResolvePath("D:\\driving", folder, sizeof(folder))) {
        printf("[fsshadow] no D:\\driving\n");
        return;
    }
    snprintf(pattern, sizeof(pattern), "%s\\*.viv", folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    int archives = 0;
    if (search != INVALID_HANDLE_VALUE) {
        do {
            snprintf(path, sizeof(path), "%s\\%s", folder, found.cFileName);
            CheckArchive(path, found.cFileName);
            archives++;
        } while (FindNextFileA(search, &found));
        FindClose(search);
    }
    printf("[fsshadow] %d packed entries unpacked, %d bytes\n", g_packed, g_packedBytes);
    printf("[fsshadow] %d archives, %d lookups: %s\n", archives, g_lookups,
           g_failures == 0 && archives > 0 ? "the same as the original" : "FAILED");
    fflush(stdout);
}
