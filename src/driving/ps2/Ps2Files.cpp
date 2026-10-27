#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// The PS2 disc's file names, and the one thing read from the Xbox disc: the render-method libraries in its
// misc.viv (docs/ps2-driving.md, "The render methods").

#include "Ps2Data.h"
#include "Ps2RenderMethods.h"

#include "../engine/UMemory.hpp"
#include "../platform/Host.h"
#include "../platform/RefPack.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace {

// The extensions the game asks for and the PS2 disc's for the same files
struct ExtensionPair {
    const char *xbox;
    const char *ps2;
};
const ExtensionPair kExtensions[] = {
    { ".xsh", ".ssh" },   // image containers (SHAPE)
    { ".xfn", ".sfn" },   // fonts
    { ".mad", ".mpc" },   // movies
};

uint32_t BigEndian32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

bool SameName(const char *a, const char *b) {
    for (;; a++, b++) {
        char x = *a == '/' ? '\\' : *a, y = *b == '/' ? '\\' : *b;
        if (x >= 'A' && x <= 'Z')
            x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z')
            y += 'a' - 'A';
        if (x != y)
            return false;
        if (x == 0)
            return true;
    }
}

// An entry of a BIGF archive on the host, as stored: its directory is big-endian {offset, size, name}
bool ReadBigEntry(const char *archive, const char *name, std::vector<uint8_t> *out) {
    FILE *f = fopen(archive, "rb");
    if (f == NULL)
        return false;
    uint8_t header[16];
    bool found = false;
    if (fread(header, 1, sizeof(header), f) == sizeof(header) && memcmp(header, "BIGF", 4) == 0) {
        uint32_t count = BigEndian32(header + 8);
        uint32_t directorySize = BigEndian32(header + 12);
        std::vector<uint8_t> directory(directorySize);
        if (fread(directory.data(), 1, directorySize, f) == directorySize) {
            size_t at = 0;
            for (uint32_t i = 0; i < count && at + 9 <= directorySize; i++) {
                uint32_t offset = BigEndian32(&directory[at]);
                uint32_t size = BigEndian32(&directory[at + 4]);
                const char *entry = reinterpret_cast<const char *>(&directory[at + 8]);
                at += 8 + strlen(entry) + 1;
                if (!SameName(entry, name))
                    continue;
                out->resize(size);
                found = fseek(f, long(offset), SEEK_SET) == 0 && fread(out->data(), 1, size, f) == size;
                break;
            }
        }
    }
    fclose(f);
    return found;
}

}  // namespace

const char *Ps2_FileName(const char *path, char *mapped, size_t size) {
    size_t length = strlen(path);
    for (const ExtensionPair &pair : kExtensions) {
        if (length < 4 || length >= size || !SameName(path + length - 4, pair.xbox))
            continue;
        memcpy(mapped, path, length - 4);
        memcpy(mapped + length - 4, pair.ps2, 5);
        return mapped;
    }
    return path;
}

void *Ps2_LoadXboxRenderMethods(const char *path) {
    char disc[260];
    HostGetProfileString("Settings", "XboxDiscPath", "", disc, sizeof(disc), ".\\settings.ini");
    disc[strcspn(disc, ";#")] = 0;
    for (size_t n = strlen(disc); n > 0 && (disc[n - 1] == ' ' || disc[n - 1] == '\t');)
        disc[--n] = 0;
    char archive[300];
    snprintf(archive, sizeof(archive), "%s\\driving\\misc.viv", disc);
    std::vector<uint8_t> entry;
    if (disc[0] == 0 || !ReadBigEntry(archive, path, &entry)) {
        printf("[ps2] %s: not found in the Xbox disc's misc.viv (settings.ini XboxDiscPath = \"%s\")\n", path, disc);
        return NULL;
    }
    unsigned unpacked = unpacksizez(entry.data());
    void *data = UMemoryREALAllocCallback(path, int(unpacked != 0 ? unpacked : entry.size()), 0);
    if (data == NULL)
        return NULL;
    if (unpacked != 0)
        UNPACK_unpack(entry.data(), static_cast<uint8_t *>(data));
    else
        memcpy(data, entry.data(), entry.size());
    Ps2_RegisterRenderMethodNames();
    return data;
}
