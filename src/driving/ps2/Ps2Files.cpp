#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// The PS2 disc's file names, the conversions applied to its files as they load, and the one thing read from the
// Xbox disc: the render-method libraries in its misc.viv (docs/ps2-driving.md).

#include "Ps2Data.h"
#include "Ps2Convert.h"
#include "Ps2RenderMethods.h"

#include "../engine/UMemory.hpp"
#include "../platform/Host.h"
#include "../platform/RealMemory.h"
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

// The PS2 pad's controls in the controller configurations (.def) and the Xbox pad's the game knows
// (XBoxPadDevice::Initialize's scalars, InputConfig.cpp's front-end slots), by the buttons' places on the pads
struct ControlPair {
    const char *ps2;
    const char *xbox;
};
const ControlPair kControls[] = {
    { "AButtonCross", "AButtonA" },        { "DButtonCross", "AButtonA" },
    { "AButtonCircle", "AButtonB" },       { "DButtonCircle", "AButtonB" },
    { "AButtonSquare", "AButtonX" },       { "DButtonSquare", "AButtonX" },
    { "AButtonTriangle", "AButtonY" },     { "DButtonTriangle", "AButtonY" },
    { "DButtonL1", "AButtonLTrigger" },    { "DButtonR1", "AButtonRTrigger" },
    { "DButtonL2", "AButtonWhite" },       { "DButtonR2", "AButtonBlack" },
    { "DButtonL3", "DButtonLThumb" },      { "DButtonR3", "DButtonRThumb" },
    { "DButtonSelect", "DButtonBack" },
    { "ButtonCross", "ButtonA" },          { "ButtonCircle", "ButtonB" },
    { "ButtonSquare", "ButtonX" },         { "ButtonTriangle", "ButtonY" },
    { "ButtonL1", "ButtonLTrigger" },      { "ButtonR1", "ButtonRTrigger" },
    { "ButtonL2", "ButtonWhite" },         { "ButtonR2", "ButtonBlack" },
    { "ButtonLStick", "ButtonLThumb" },    { "ButtonRStick", "ButtonRThumb" },
};

bool IsSeparator(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

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

bool Ps2_ConvertControls(const char *path, const uint8_t *file, int size, std::vector<uint8_t> *converted) {
    size_t length = strlen(path);
    if (length < 4 || !SameName(path + length - 4, ".def"))
        return false;
    converted->clear();
    for (int i = 0; i < size;) {
        if (IsSeparator(file[i])) {
            converted->push_back(file[i++]);
            continue;
        }
        int end = i;
        while (end < size && !IsSeparator(file[end]))
            end++;
        const char *replacement = NULL;
        for (const ControlPair &pair : kControls)
            if (size_t(end - i) == strlen(pair.ps2) && memcmp(file + i, pair.ps2, end - i) == 0)
                replacement = pair.xbox;
        if (replacement != NULL)
            converted->insert(converted->end(), replacement, replacement + strlen(replacement));
        else
            converted->insert(converted->end(), file + i, file + end);
        i = end;
    }
    return true;
}

void *Ps2_ConvertFile(void *data, int *size, const char *path, int flags, FileMallocFn allocate,
                      FileFreeFn release) {
    if (data == NULL)
        return data;
    const uint8_t *file = static_cast<const uint8_t *>(data);
    int bytes = size != NULL ? *size : int(MEM_size(data));
    std::vector<uint8_t> converted;
    if (!Ps2_ConvertShapes(file, &converted) && !Ps2_ConvertFont(file, bytes, &converted) &&
        !Ps2_ConvertGallery(path, file, bytes, &converted) && !Ps2_ConvertControls(path, file, bytes, &converted))
        return data;
    void *result = allocate(path, int(converted.size()), flags);
    if (result == NULL)
        return data;
    memcpy(result, converted.data(), converted.size());
    release(data);
    if (size != NULL)
        *size = int(converted.size());
    return result;
}

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
