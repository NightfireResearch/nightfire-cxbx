#ifndef DRIVING_DATA_INIFILES_H_
#define DRIVING_DATA_INIFILES_H_

// IniFiles: a Windows-style .ini file read through DAFI (the ini parser, DATA_C), for the world camera's settings
// (RCameraIniLoader::LoadFile reads data/render/camera.ini: [section] key=value). Reads answer the default when the
// section, the key or the value is missing.
//
// An encrypted variant (the constructor's second argument) loads the file into a MEM_ buffer and decrypts it in
// place with EncryptDecrypt - after printing "DO NOT USE INI FILE ENCYRPTION" a thousand times; the game never
// asks for it.

#include <stddef.h>
#include <stdint.h>

struct DAFI;

struct IniFiles {                // 0xc bytes
    const void *const *vtable;   // +0x00
    DAFI *dafi;                  // +0x04 the DAFI handle
    char *text;                  // +0x08 the file

    IniFiles* Construct(const char *path, bool encrypted);                       // 0x000e4030
    IniFiles* Delete(unsigned flags);                                            // 0x000e4240
    // atof of the value, as the double atof returns, or the default (0x000e40f0).
    double ReadFloat(const char *section, const char *key, float fallback);
    int ReadInteger(const char *section, const char *key, int fallback);         // 0x000e4150
    const char* ReadString(const char *section, const char *key, const char *fallback);   // 0x000e41c0
    bool FindSection(const char *section);                                       // 0x000e4220
};
static_assert(sizeof(IniFiles) == 0xc, "IniFiles");

// XORs count bytes of in into out with a key stream made from the key: each key character times four constants
// of a 12-entry table in turn (0x0011c5c0).
void EncryptDecrypt(const uint8_t *in, int count, uint8_t *out, const char *key);

#endif // DRIVING_DATA_INIFILES_H_
