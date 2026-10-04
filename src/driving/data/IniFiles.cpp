// IniFiles and EncryptDecrypt. See IniFiles.h.

#include "IniFiles.h"

#include "Dafi.h"
#include "DebugVarUntested.h"
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../../helpers.h"
#include "../platform/RealMemory.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define IniFilesVtable ((const void *const *)0x001a0998)
#define MemDefaultClass U32_AT(0x00242cdc)   // the MEM_ allocation flags in force

#define Printf ((int (__cdecl *)(const char *, ...))0x00132192)
#define Atof ((double (__cdecl *)(const char *))0x00133e84)
#define Atol ((long (__cdecl *)(const char *))0x00133d51)

namespace {

static const int kEncryptedLoadLimit = 50000;   // bytes FileLoadAt may load
static const int kLoadFlags = 0x100;
static const char kIniKey[] = "b8D;V`fj";

// The value of key in section, or null when either is missing.
const char *FindValue(DAFI *dafi, const char *section, const char *key, bool *found) {
    *found = false;
    if (DAFI_getsectionindex(dafi, section) < 0)
        return NULL;
    DAFI_setsection(dafi, section);
    if (DAFI_getkeyindex(dafi, key) < 0)
        return NULL;
    *found = true;
    return DAFI_getvalue(dafi, key);
}

} // namespace

// FUNC_AT(0x000e4030)
IniFiles* IniFiles::Construct(const char *path, bool encrypted) {
    vtable = IniFilesVtable;
    dafi = NULL;
    text = NULL;
    int size = 0;
    if (encrypted) {
        DEBUGVAR_UNTESTED("IniFiles::Construct (encrypted)");
        unsigned flags = MemDefaultClass;
        text = (char *)MEM_alloc("INIFile buffer", UFileLoader::FileSize(path), flags);
        size = UFileLoader::FileLoadAt(path, text, kEncryptedLoadLimit);
        for (int i = 0; i < 1000; i++)
            Printf("DO NOT USE INI FILE ENCYRPTION\n");
        EncryptDecrypt((const uint8_t *)text, size, (uint8_t *)text, kIniKey);
    } else {
        text = (char *)UFileLoader::FileLoad(path, kLoadFlags);
        if (text != NULL)
            size = int(MEM_size(text));
    }
    dafi = DAFI_open(text, size);
    return this;
}

// FUNC_AT(0x000e4240)
IniFiles* IniFiles::Delete(unsigned flags) {
    vtable = IniFilesVtable;
    if (text != NULL) {
        MEM_free(text);
        text = NULL;
    }
    if (dafi != NULL) {
        DAFI_close(dafi);
        dafi = NULL;
    }
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x000e40f0)
double IniFiles::ReadFloat(const char *section, const char *key, float fallback) {
    bool found;
    const char *value = FindValue(dafi, section, key, &found);
    if (value != NULL)
        return Atof(value);
    return fallback;
}

// The original takes atol's result through the FPU and __ftol2, which gives every int back unchanged.
//
// FUNC_AT(0x000e4150)
int IniFiles::ReadInteger(const char *section, const char *key, int fallback) {
    bool found;
    const char *value = FindValue(dafi, section, key, &found);
    if (value != NULL)
        return Atol(value);
    return fallback;
}

// A key without a value answers null, not the default.
//
// FUNC_AT(0x000e41c0)
const char* IniFiles::ReadString(const char *section, const char *key, const char *fallback) {
    bool found;
    const char *value = FindValue(dafi, section, key, &found);
    return found ? value : fallback;
}

// FUNC_AT(0x000e4220)
bool IniFiles::FindSection(const char *section) {
    return DAFI_getsectionindex(dafi, section) >= 0;
}

// FUNC_AT(0x0011c5c0)
void EncryptDecrypt(const uint8_t *in, int count, uint8_t *out, const char *key) {
    static const int kTable[12] = {
        0x359, 0xbb7, 0xeb7, 0x11e7, 0xbf5, 0x281, 0x833, 0x943, 0xc83, 0x1271, 0x1327, 0xda3 };
    int keyLength = int(strlen(key));
    int streamLength = keyLength * 4;
    uint8_t *stream = (uint8_t *)OperatorNewArray(streamLength);
    int entry = 0;
    for (int i = 0; i < keyLength; i++) {   // each key byte times the low bytes of four table entries
        for (int k = 0; k < 4; k++)
            stream[i * 4 + k] = uint8_t(uint8_t(kTable[entry + k]) * uint8_t(key[i]));
        entry = (entry + 4) % 12;
    }
    for (int i = 0; i < count; i++)
        out[i] = stream[i % streamLength] ^ in[i];
    OperatorDelete(stream);   // delete, not delete[]
}
