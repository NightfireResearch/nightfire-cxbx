#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// The GameCube disc's file names, the conversions applied to its files as they load, and the one thing read from the
// Xbox disc: the render-method libraries in its misc.viv (docs/gc-driving.md).

#include "GcData.h"
#include "GcBytes.h"
#include "GcConvert.h"
#include "GcRenderMethods.h"

#include "../engine/UMemory.hpp"
#include "../platform/Host.h"
#include "../platform/RealMemory.h"
#include "../platform/RefPack.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace {

// The extensions the game asks for and the GameCube disc's for the same files
struct ExtensionPair {
    const char *xbox;
    const char *gc;
};
const ExtensionPair kExtensions[] = {
    { ".xsh", ".gsh" },   // image containers (SHAPE)
    { ".xfn", ".gfn" },   // fonts
};

// The GameCube pad's controls in the controller configurations (.def) and the Xbox pad's the game knows
// (XBoxPadDevice::Initialize's scalars, InputConfig.cpp's front-end slots), by the buttons' places on the pads: the
// GameCube's digital buttons as the Xbox's analogue ones, Z as Back
struct ControlPair {
    const char *gc;
    const char *xbox;
};
const ControlPair kControls[] = {
    { "DButtonA", "AButtonA" },                  { "DButtonB", "AButtonB" },
    { "DButtonX", "AButtonX" },                  { "DButtonY", "AButtonY" },
    { "DButtonLTrigger", "AButtonLTrigger" },    { "DButtonRTrigger", "AButtonRTrigger" },
    { "DButtonZTrigger", "DButtonBack" },
    { "LStick", "ButtonLThumb" },                { "RStick", "ButtonRThumb" },
    { "ButtonZTrigger", "ButtonBlack" },
};

constexpr char kAnimLookup[] = "alookup.bin";   // anim/AnimationDatabase.cpp: a table of words
constexpr uint32_t kBankHeaderBytes = 0x14;

bool IsSeparator(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
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

bool EndsWith(const char *path, const char *ending) {
    size_t length = strlen(path), n = strlen(ending);
    return length >= n && SameName(path + length - n, ending);
}

// An entry of a BIGF archive on the host, as stored: its directory is big-endian {offset, size, name}
bool ReadBigEntry(const char *archive, const char *name, std::vector<uint8_t> *out) {
    FILE *f = fopen(archive, "rb");
    if (f == NULL)
        return false;
    uint8_t header[16];
    bool found = false;
    if (fread(header, 1, sizeof(header), f) == sizeof(header) && memcmp(header, "BIGF", 4) == 0) {
        uint32_t count = GcBe32(header + 8);
        uint32_t directorySize = GcBe32(header + 12);
        std::vector<uint8_t> directory(directorySize);
        if (fread(directory.data(), 1, directorySize, f) == directorySize) {
            size_t at = 0;
            for (uint32_t i = 0; i < count && at + 9 <= directorySize; i++) {
                uint32_t offset = GcBe32(&directory[at]);
                uint32_t size = GcBe32(&directory[at + 4]);
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

bool Gc_ConvertControls(const char *path, const uint8_t *file, uint32_t size, std::vector<uint8_t> *converted) {
    if (!EndsWith(path, ".def"))
        return false;
    converted->clear();
    for (uint32_t i = 0; i < size;) {
        if (IsSeparator(file[i])) {
            converted->push_back(file[i++]);
            continue;
        }
        uint32_t end = i;
        while (end < size && !IsSeparator(file[end]))
            end++;
        const char *replacement = NULL;
        for (const ControlPair &pair : kControls)
            if (end - i == strlen(pair.gc) && memcmp(file + i, pair.gc, end - i) == 0)
                replacement = pair.xbox;
        if (replacement != NULL)
            converted->insert(converted->end(), replacement, replacement + strlen(replacement));
        else
            converted->insert(converted->end(), file + i, file + end);
        i = end;
    }
    return true;
}

void *Gc_ConvertFile(void *data, int *size, const char *path, int flags, FileMallocFn allocate,
                     FileFreeFn release) {
    if (data == NULL)
        return data;
    uint8_t *file = static_cast<uint8_t *>(data);
    uint32_t bytes = size != NULL ? uint32_t(*size) : uint32_t(MEM_size(data));
    if (EndsWith(path, kAnimLookup)) {
        for (uint32_t at = 0; at + 4 <= bytes; at += 4)
            GcSwap32(file + at);
        return data;
    }
    if (bytes >= kBankHeaderBytes && memcmp(file, "BNKb", 4) == 0) {
        // A sound bank (snd/Banks.h BankHeader, big-endian): its samples are DSP ADPCM, which the sound library
        // does not play, so it becomes a BNKl with no patches - silent
        memcpy(file, "BNKl", 4);
        GcSwap16(file + 4);
        GcPutLe16(file + 6, 0);
        GcSwap32(file + 8);
        GcSwap32(file + 12);
        GcSwap32(file + 16);
        return data;
    }
    std::vector<uint8_t> converted;
    if (!Gc_ConvertShapes(file, bytes, &converted) && !Gc_ConvertFont(file, bytes, &converted) &&
        !Gc_ConvertDataGroup(path, file, bytes, &converted) && !Gc_ConvertControls(path, file, bytes, &converted))
        return data;
    if (converted.size() == bytes) {   // the same block, when the size holds
        memcpy(data, converted.data(), bytes);
        return data;
    }
    void *result = allocate(path, int(converted.size()), flags);
    if (result == NULL)
        return data;
    memcpy(result, converted.data(), converted.size());
    release(data);
    if (size != NULL)
        *size = int(converted.size());
    return result;
}

void Gc_ConvertStreamChunk(uint32_t *chunk, int channels) {
    uint8_t *words = reinterpret_cast<uint8_t *>(chunk);
    for (int i = 0; i < 1 + channels; i++)
        GcSwap32(words + 8 + i * 4);
}

const char *Gc_FileName(const char *path, char *mapped, size_t size) {
    size_t length = strlen(path);
    for (const ExtensionPair &pair : kExtensions) {
        if (length < 4 || length >= size || !SameName(path + length - 4, pair.xbox))
            continue;
        memcpy(mapped, path, length - 4);
        memcpy(mapped + length - 4, pair.gc, 5);
        return mapped;
    }
    return path;
}

void *Gc_LoadXboxRenderMethods(const char *path) {
    char disc[260];
    HostGetProfileString("Settings", "XboxDiscPath", "", disc, sizeof(disc), ".\\settings.ini");
    disc[strcspn(disc, ";#")] = 0;
    for (size_t n = strlen(disc); n > 0 && (disc[n - 1] == ' ' || disc[n - 1] == '\t');)
        disc[--n] = 0;
    char archive[300];
    snprintf(archive, sizeof(archive), "%s\\driving\\misc.viv", disc);
    std::vector<uint8_t> entry;
    if (disc[0] == 0 || !ReadBigEntry(archive, path, &entry)) {
        printf("[gc] %s: not found in the Xbox disc's misc.viv (settings.ini XboxDiscPath = \"%s\")\n", path, disc);
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
    Gc_RegisterRenderMethodNames();
    Gc_MarkXboxObject(data);
    return data;
}
