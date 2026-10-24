// INTERIM, until milestone 7 of docs/ps2-data.md: the Xbox disc's meshes standing in for the PS2's skinned ones (see
// Ps2XboxStandIn.h). It reads the Xbox's archives itself, as FS.cpp does (which the PS2 build leaves out), with the
// host's own files: filesys.d00's header and file list, a file by the CRC32 of its lower-case name, EDL decompressed.

#include "Ps2XboxStandIn.h"
#include "../engine/EDL.h"
#include "../engine/XboxSettings.h"
#include "../game.h"            // GameState
#include "../memory.h"
#include "../util/crc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>

#define MALLOC_PS2_OBJECT ((MallocFlags)0x2804)

#define XBOX_ARCHIVE_EDL_COMPRESSED (1 << 1)
#define XBOX_BUNDLE_HEADER_BYTES 0x20
#define LEVEL_VARIANT_BITS 0x00f00000      // the PS2's variant bundles are the level's own on the Xbox

#define BLOCK_ENTITY_PARAMS 0x04
#define BLOCK_ENTITY_GEOMETRY 0x0d
#define BLOCK_END 0x1d

#pragma pack(push, 1)
struct XboxArchiveHeader {      // FS.cpp's ArchiveHeader
    uint8_t unknown[0x18];
    uint32_t numEntries;
    uint32_t numFilesysFiles;
    uint8_t unknown2[0x8];
};
struct XboxFileEntry {          // FS.cpp's FileEntry
    uint32_t nameCrc;
    uint32_t dataCrc;
    uint32_t unknown1;
    uint32_t offsetLow;
    uint32_t offsetHigh;
    uint32_t compressedLen;
    uint32_t uncompressedLen;
    uint8_t flags;
    uint8_t unknown2;
    uint32_t filesysIdx;
    uint32_t offsetOfFileWithinArchive;
};
#pragma pack(pop)
static_assert(sizeof(XboxArchiveHeader) == 0x28, "XboxArchiveHeader is 0x28 bytes");
static_assert(sizeof(XboxFileEntry) == 0x26, "XboxFileEntry is 0x26 bytes");
#define XBOX_FILE_LIST_OFFSET 0x20      // the list starts 32 bytes into filesys.d00

struct Mesh {
    const uint8_t *data;
    uint32_t size;
};

// The level bundle read last, and its meshes by entity hashcode
static uint32_t BundleHashcode;
static std::vector<uint8_t> Bundle;
static std::map<uint32_t, Mesh> Meshes;

static bool ReadAt(const char *path, uint32_t offset, void *out, uint32_t len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    bool ok = fseek(f, (long)offset, SEEK_SET) == 0 && fread(out, 1, len, f) == len;
    fclose(f);
    return ok;
}

// settings.ini's XboxDiscPath, else the environment's NIGHTFIRE_XBOX_DISC (a time demo's playback reads the settings
// the recording started from, which have no such key)
static const char *XboxDiscPath(void) {
    const char *path = Settings_GetXboxDiscPath();
    if (path[0] == '\0' && getenv("NIGHTFIRE_XBOX_DISC") != NULL)
        path = getenv("NIGHTFIRE_XBOX_DISC");
    return path;
}

static void ArchivePath(int index, char *out, size_t size) {
    snprintf(out, size, "%s/eurocom/filesys.d%02d", XboxDiscPath(), index);
}

// A file of the Xbox's archives, decompressed, as FS_LoadFileIfReady and FS_StateMachineIterate read one
static bool ReadXboxFile(const char *name, std::vector<uint8_t> &out) {
    char path[300];
    ArchivePath(0, path, sizeof(path));
    XboxArchiveHeader header;
    if (!ReadAt(path, 0, &header, sizeof(header)))
        return false;
    std::vector<XboxFileEntry> entries(header.numEntries);
    if (!ReadAt(path, XBOX_FILE_LIST_OFFSET, entries.data(), header.numEntries * sizeof(XboxFileEntry)))
        return false;
    char lower[64];
    size_t n = 0;
    for (; name[n] != '\0' && n + 1 < sizeof(lower); n++)
        lower[n] = name[n] == '/' ? '\\' : (char)tolower((unsigned char)name[n]);
    lower[n] = '\0';
    uint32_t crc = crc32buf((const uint8_t *)lower, n);
    for (const XboxFileEntry &e : entries) {
        if (e.nameCrc != crc)
            continue;
        ArchivePath((int)e.filesysIdx, path, sizeof(path));
        bool compressed = (e.flags & XBOX_ARCHIVE_EDL_COMPRESSED) != 0;
        out.assign(e.uncompressedLen + (compressed ? 0x800 : 0), 0);
        uint8_t *read = out.data() + (compressed ? e.uncompressedLen - e.compressedLen + 0x800 : 0);
        if (!ReadAt(path, e.offsetOfFileWithinArchive, read, e.compressedLen))
            return false;
        if (compressed)
            EDL_DecompressSection((char *)out.data(), (edl_section_t *)read);
        out.resize(e.uncompressedLen);
        return true;
    }
    return false;
}

// The meshes of every pack in the bundle: an entity is its mesh block, then its params block with its hashcode
static void IndexBundle(void) {
    Meshes.clear();
    if (Bundle.size() < XBOX_BUNDLE_HEADER_BYTES)
        return;
    uint32_t dirSize, count;
    memcpy(&dirSize, Bundle.data(), 4);
    memcpy(&count, Bundle.data() + 4, 4);
    const uint8_t *end = Bundle.data() + Bundle.size();
    const uint8_t *entry = Bundle.data() + XBOX_BUNDLE_HEADER_BYTES;
    const uint8_t *data = entry + dirSize;
    for (uint32_t i = 0; i < count && entry + 5 < end; i++) {
        uint32_t len;
        memcpy(&len, entry, 4);
        int type = entry[4];
        entry += 5 + strlen((const char *)entry + 5) + 1;
        const uint8_t *pack = data;
        data += len;
        if (data > end)
            break;
        if (type != 0 && type != 1 && type != 2 && type != 11)
            continue;
        // A pack: u32 version 0, then the blocks one after another
        Mesh last = { NULL, 0 };
        for (const uint8_t *block = pack + 4; block + 4 <= pack + len;) {
            uint32_t header;
            memcpy(&header, block, 4);
            uint32_t size = header & 0xffffff;
            int id = (int)(header >> 24);
            if (size < 4 || id == BLOCK_END)
                break;
            if (id == BLOCK_ENTITY_GEOMETRY)
                last = { block + 4, size - 4 };
            if (id == BLOCK_ENTITY_PARAMS && last.data != NULL) {
                uint32_t hashcode;
                memcpy(&hashcode, block + 4, 4);
                if (hashcode != 0xffffffff)
                    Meshes.emplace(hashcode, last);
                last.data = NULL;
            }
            block += size;
        }
    }
}

ModelData *Ps2_XboxStandInModel(uint32_t entityHashcode) {
    if (XboxDiscPath()[0] == '\0') {
        printf("[ps2] mesh 0x%08x: skinned; no XboxDiscPath to take the Xbox's from\n", entityHashcode);
        return NULL;
    }
    uint32_t level = (uint32_t)GameState.NextLevelHashcode & ~(uint32_t)LEVEL_VARIANT_BITS;
    if (level != BundleHashcode) {
        char name[16];
        snprintf(name, sizeof(name), "%8.8x.bin", level);
        BundleHashcode = level;
        if (!ReadXboxFile(name, Bundle)) {
            printf("[ps2] the Xbox disc (%s) has no %s\n", XboxDiscPath(), name);
            Bundle.clear();
        }
        IndexBundle();
        printf("[ps2] interim stand-ins: %zu meshes in the Xbox's %s\n", Meshes.size(), name);
    }
    auto found = Meshes.find(entityHashcode);
    if (found == Meshes.end()) {
        printf("[ps2] mesh 0x%08x: skinned, and not in the Xbox's bundle\n", entityHashcode);
        return NULL;
    }
    ModelData *model = (ModelData *)Mem_Malloc(found->second.size, MALLOC_PS2_OBJECT, 0);
    memcpy(model, found->second.data, found->second.size);
    return model;
}
