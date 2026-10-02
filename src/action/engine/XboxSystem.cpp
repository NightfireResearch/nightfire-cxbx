#include "XboxSystem.h"
#include "XboxFile.h"
#include "XboxSettings.h"   // LanguageNVSetting
#include "FS.h"
#include "Fmv.h"
#include "psiInput.h"
#include "psiSave.h"        // psiInternalLoadingDataState
#include "Direct3D/d3dSeam.h"
#include "../sound/dsndSeam.h"
#include "../game.h"        // timestamp, Graphics_IsSomeGraphicsRegion
#include "../util/crc.h"
#include "../../common/launchInfo.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The language.
//
// Language_Get settles it once: launch data from the other engine means it is already known; otherwise
// config.txt on the disc may name it (a two-letter code), and failing that the dashboard's language decides,
// with the language page shown first (AskForLanguage) unless that is American English, or a marker file on
// t:\ says this console's choice was already made for this dashboard language. The marker is
// t:\lang<dashboard><chosen>.dat, written when the language page is left.
//
// Two numberings: LanguageToLoad is the game's text language (0 English, 1 French, 2 German, 3 Spanish,
// 4 Italian, 5 Dutch, 6 American, 7 Japanese, 8 Swedish), and the "language code" Language_Get returns and the
// dashboard is mapped to is another (0 Dutch, 1 English, 2 French, 3 German, 4 Italian, 5 Japanese,
// 6 Spanish, 7 Swedish, 8 American).
// ---------------------------------------------------------------------------------------------------------------

enum TextLanguage { ENGLISH, FRENCH, GERMAN, SPANISH, ITALIAN, DUTCH, AMERICAN, JAPANESE, SWEDISH };

#define LanguageToLoad (*(int *)0x0025d7a0)            // the game's: the text code reads it
#define AskForLanguage (*(uint8_t *)0x0017d5f9)        // the boot flow shows the language page when set
#define DashboardLanguage (*(int *)0x002adf60)         // the language page's handlers read these two
#define ChosenLanguage (*(int *)0x002adf64)

// XBE_GLOBAL(0x002adf5c, 0x1)
static uint8_t ConfigNamedLanguage;
// XBE_GLOBAL(0x002adf68, 0x4)
static int MarkerDashboardLanguage;
// XBE_GLOBAL(0x002adf6c, 0x4)
static int MarkerChosenLanguage;
// XBE_GLOBAL(0x002ae2e8, 0x100)
static char LanguageMarkerPath[256];
// XBE_GLOBAL(0x002ae3e8, 0x1)
static uint8_t LanguageSettled;
// XBE_GLOBAL(0x001d56ec, 0x4)
static int CachedDashboardLanguage = -1;

#define LANGUAGE_MARKER "t:\\lang%02d%02d.dat"

// Sets the text language (game code)
// AUTOGEN
void FUN_00084080(int language);

// The dashboard's language as a language code: English is American on an NTSC console. (0x000e92a0)
// FUNC_AT(000e92a0)
int Language_FromDashboard(void) {
    if (CachedDashboardLanguage == -1) {
        switch (LanguageNVSetting()) {
        case 1: CachedDashboardLanguage = Graphics_IsSomeGraphicsRegion() ? 8 : 1; break;   // English
        case 2: CachedDashboardLanguage = 5; break;                                          // Japanese
        case 3: CachedDashboardLanguage = 3; break;                                          // German
        case 4: CachedDashboardLanguage = 2; break;                                          // French
        case 5: CachedDashboardLanguage = 6; break;                                          // Spanish
        case 6: CachedDashboardLanguage = 4; break;                                          // Italian
        default: CachedDashboardLanguage = 1; break;
        }
    }
    return CachedDashboardLanguage;
}

// Removes every language marker (0x000dbd10)
static void Language_DeleteMarkers(void) {
    for (int chosen = 0; chosen < 9; chosen++)
        for (int dashboard = 0; dashboard < 9; dashboard++) {
            sprintf(LanguageMarkerPath, LANGUAGE_MARKER, dashboard, chosen);
            File_Delete(LanguageMarkerPath);
        }
}

// Finds the language marker, if there is one (0x000dbd60)
static bool Language_FindMarker(void) {
    for (int chosen = 0; chosen < 9; chosen++)
        for (int dashboard = 0; dashboard < 9; dashboard++) {
            sprintf(LanguageMarkerPath, LANGUAGE_MARKER, dashboard, chosen);
            if (File_Exists(LanguageMarkerPath)) {
                MarkerDashboardLanguage = dashboard;
                MarkerChosenLanguage = chosen;
                return true;
            }
        }
    return false;
}

// The language page's choice, kept as the marker (0x000dbdc0)
// FUNC_AT(000dbdc0)
void Language_SaveChoice(void) {
    ChosenLanguage = LanguageToLoad;
    Language_DeleteMarkers();
    sprintf(LanguageMarkerPath, LANGUAGE_MARKER, DashboardLanguage, ChosenLanguage);
    MaybeFileWriteContents(LanguageMarkerPath, NULL, 0);
}

// AUTOINJECT
int Language_Get(void) {
    if (GetPTPData() == NULL && !LanguageSettled) {
        LanguageSettled = 1;
        LanguageToLoad = ENGLISH;
        FILE *config = fopen(FS_EurocomPath("config.txt"), "rb");
        if (config != NULL) {
            uint8_t code[2] = {0, 0};
            if (fread(code, 2, 1, config) == 1) {
                static const struct { char code[3]; int language; } named[] = {
                    {"GE", GERMAN}, {"EN", ENGLISH}, {"AM", AMERICAN}, {"DU", DUTCH}, {"FR", FRENCH},
                    {"SP", SPANISH}, {"IT", ITALIAN}, {"JA", JAPANESE}, {"SW", SWEDISH},
                };
                for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++)
                    if (code[0] == (uint8_t)named[i].code[0] && code[1] == (uint8_t)named[i].code[1]) {
                        LanguageToLoad = named[i].language;
                        ConfigNamedLanguage = 1;
                        break;
                    }
            }
            fclose(config);
        }
        if (ConfigNamedLanguage) {
            AskForLanguage = 0;
        } else {
            AskForLanguage = 1;
            static const int fromCode[9] = {DUTCH, ENGLISH, FRENCH, GERMAN, ITALIAN, JAPANESE, SPANISH, SWEDISH,
                                            AMERICAN};
            int dashboard = Language_FromDashboard();
            if (dashboard >= 0 && dashboard <= 8) {
                LanguageToLoad = fromCode[dashboard];
                if (dashboard == 8)
                    AskForLanguage = 0;
            }
            DashboardLanguage = Language_FromDashboard();
            if (AskForLanguage && Language_FindMarker() && DashboardLanguage == MarkerDashboardLanguage) {
                LanguageToLoad = MarkerChosenLanguage;
                AskForLanguage = 0;
            }
        }
        FUN_00084080(LanguageToLoad);
    }
    static const int toCode[9] = {1, 2, 3, 6, 4, 0, 8, 5, 7};
    unsigned language = (unsigned)LanguageToLoad;
    return language <= 8 ? toCode[language] : 1;
}

// ---------------------------------------------------------------------------------------------------------------
// The signed state file: a 29-byte header and the data, XOR-scrambled with a key from the clock. The action
// engine writes z:\state.bin before relaunching itself (WriteStateFileAndLaunch) and GetPTPData reads it back.
// The original signed it with XCalculateSignature; this writes a CRC-32 of the scrambled data in the first four
// bytes of the signature instead, since only our own code reads it (the console's signature key is not
// something we have, and no standalone build could ever write the original's).
// ---------------------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
struct StateFileHeader {
    uint32_t magic;          // STATE_FILE_MAGIC
    uint32_t fileSize;       // the header and the data
    uint8_t key;             // the scramble's starting key
    uint8_t signature[20];
};
#pragma pack(pop)
static_assert(sizeof(StateFileHeader) == 0x1d, "the state file header is 29 bytes");
#define STATE_FILE_MAGIC 0x1753584bu

// XORs each byte with the key (top bit set), the key advancing by 0x49 a byte. Its own inverse. (0x000e3320)
static void StateFile_Scramble(uint8_t key, uint8_t *data, int len) {
    for (int i = 0; i < len; i++, key += 0x49)
        data[i] ^= key | 0x80;
}

// Writes path: with data, signed and scrambled (the caller's buffer is scrambled and restored); without, empty.
// (0x000e3450)
// AUTOINJECT
bool MaybeFileWriteContents(char *path, uint8_t *data, uint32_t len) {
    if (path == NULL)
        return false;
    HANDLE file = createFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    if (data == NULL || (int)len <= 0) {
        DoNtClose(file);
        return true;
    }
    querySetSomeInfo(file, 0, NULL, FILE_BEGIN);
    StateFileHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = STATE_FILE_MAGIC;
    header.fileSize = len + sizeof(header);
    header.key = (uint8_t)(int64_t)timestamp();
    StateFile_Scramble(header.key, data, len);
    uint32_t crc = crc32buf(data, len);
    memcpy(header.signature, &crc, sizeof(crc));
    uint32_t written = 0;
    FileWrite(file, &header, sizeof(header), &written, NULL);
    bool ok = written == sizeof(header);
    written = 0;
    FileWrite(file, data, len, &written, NULL);
    ok = ok && written == len;
    DoNtClose(file);
    StateFile_Scramble(header.key, data, len);
    return ok;
}

// Reads a state file into data (len bytes at most, the rest zeroed), checking its header and signature
// (0x000e3580)
// FUNC_AT(000e3580)
bool StateFile_Read(char *path, void *data, uint32_t len) {
    if (path == NULL || data == NULL || (int)len <= 0)
        return false;
    HANDLE file = createFile(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    querySetSomeInfo(file, 0, NULL, FILE_BEGIN);
    StateFileHeader header;
    uint32_t got = 0;
    readFromFileBlocking(file, &header, sizeof(header), &got, NULL);
    bool headerOk = got == sizeof(header);
    LARGE_INTEGER size;
    long fileSize = getFileSize_LargeInteger(file, &size) ? (long)size.LowPart : -1;
    memset(data, 0, len);
    got = 0;
    readFromFileBlocking(file, data, len, &got, NULL);
    int dataLen = (int)got < (int)len ? (int)got : (int)len;
    DoNtClose(file);
    if ((int)got <= 0 || !headerOk || fileSize != (long)header.fileSize || header.magic != STATE_FILE_MAGIC)
        return false;
    uint32_t crc = crc32buf((uint8_t *)data, dataLen);
    if (memcmp(header.signature, &crc, sizeof(crc)) != 0)
        return false;
    StateFile_Scramble(header.key, (uint8_t *)data, dataLen);
    return true;
}

// (0x000e36c0)
// FUNC_AT(000e36c0)
bool File_Delete(char *path) {
    if (path == NULL)
        return false;
    return MaybeFileCreateNew(path) != 0;
}

// (0x000e36e0)
// FUNC_AT(000e36e0)
bool File_Exists(char *path) {
    if (path == NULL)
        return false;
    return Xbox_GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

// ---------------------------------------------------------------------------------------------------------------
// Moving between engines. Each engine is its own XBE, and a console moves between them by rebooting into the
// other with a page of launch data (common/launchInfo.cpp does it with a file). Going to the driving engine,
// the page is the mission's data; coming back, it is the driving engine's results. WriteStateFileAndLaunch
// relaunches the action engine itself, its state in z:\state.bin and the reason in the page.
// ---------------------------------------------------------------------------------------------------------------

#define LAUNCH_DATA_BYTES 0xc00
#define LAUNCH_TYPE_DATA 0         // the page is the data
#define LAUNCH_TYPE_STATE_FILE 2   // the data is in z:\state.bin

// XBE_GLOBAL(0x001d56e8, 0x1)
static uint8_t LaunchInfoUnread = 1;
// XBE_GLOBAL(0x002ff770, 0x1)
static uint8_t HaveLaunchData;
// XBE_GLOBAL(0x002ff778, 0xc00)
static uint32_t LaunchInfoData[LAUNCH_DATA_BYTES / 4];

static void *ReadLaunchData(void) {
    if (!LaunchInfoUnread)
        return HaveLaunchData ? LaunchInfoData : NULL;
    memset(LaunchInfoData, 0, sizeof(LaunchInfoData));
    LaunchInfoUnread = 0;
    int type = 0;
    if (XGetLaunchInfo(&type, LaunchInfoData) != 0)
        return NULL;
    HaveLaunchData = 0;
    if (type == LAUNCH_TYPE_DATA) {
        HaveLaunchData = 1;
        return LaunchInfoData;
    }
    if (type == LAUNCH_TYPE_STATE_FILE) {
        HaveLaunchData = StateFile_Read((char *)"z:\\state.bin", LaunchInfoData, LAUNCH_DATA_BYTES);
        MaybeFileCreateNew("z:\\state.bin");
        return HaveLaunchData ? LaunchInfoData : NULL;
    }
    return NULL;
}

// FUNC_AT(000e90f0)
void *GetPTPData(void) {
    return ReadLaunchData();
}

// FUNC_AT(000e9340)
void *GetPTPData_Copy(void) {
    return ReadLaunchData();
}

// AUTOINJECT
void maybeCleanupSystem(void) {
    maybeBackgroundMovieCleanup();
    maybeD3dShutdown();
    maybeSoundShutdown();
    maybeInputShutdown();
}

// Launches d:\<executableName> with data as its launch page (if that returns, the action engine instead)
// AUTOINJECT
void SetLaunchInfoAndLaunch(char *executableName, void *data, uint32_t len) {
    if (executableName == NULL)
        return;
    maybeCleanupSystem();
    char path[0x400];
    snprintf(path, sizeof(path), "d:\\%s", executableName);
    static uint8_t page[LAUNCH_DATA_BYTES];
    memset(page, 0, sizeof(page));
    if (data != NULL)
        memcpy(page, data, len < LAUNCH_DATA_BYTES ? len : LAUNCH_DATA_BYTES);
    XLaunchNewImageA(path, page);
    SetLaunchInfoAndLaunch((char *)"default.xbe", NULL, 0);
}

// AUTOINJECT
void WriteStateFileAndLaunch(uint32_t reason, void *data, int len) {
    MaybeFileCreateNew("z:\\state.bin");
    if (data != NULL && len > 0)
        MaybeFileWriteContents((char *)"z:\\state.bin", (uint8_t *)data, len);
    maybeCleanupSystem();
    static uint32_t page[LAUNCH_DATA_BYTES / 4];
    memset(page, 0, sizeof(page));
    page[0] = LAUNCH_TYPE_STATE_FILE;
    page[1] = 0;
    page[2] = 0x55;
    page[3] = reason;
    XLaunchNewImageA(NULL, page);
    SetLaunchInfoAndLaunch((char *)"default.xbe", NULL, 0);
}

// AUTOINJECT
void psiLaunchDriving(void *data, uint32_t len) {
    SetLaunchInfoAndLaunch((char *)"driving.xbe", data, len);
}

// The driving engine's results, if this run was launched from it
// AUTOINJECT
bool psiGetDrivingData(uint32_t *out) {
    memset(out, 0, LAUNCH_DATA_BYTES);
    void *page = GetPTPData();
    if (page == NULL)
        return false;
    memcpy(out, page, LAUNCH_DATA_BYTES);
    return true;
}

// (0x000dfbb0)
// FUNC_AT(000dfbb0)
int psiGetLoadingDataState(void) {
    return psiInternalLoadingDataState;
}

// ---------------------------------------------------------------------------------------------------------------
// Memory and the CPU cache
// ---------------------------------------------------------------------------------------------------------------

// 4 KB-aligned contiguous memory, from the kernel (the loader's MmAllocateContiguousMemoryEx, through the XBE's
// import slot). It has to come from there rather than the heap: the D3D code treats the address as the
// console's 0x8xxxxxxx alias and strips the top nibble, so it must sit below 0x10000000, which the loader
// guarantees (src/loader/kernel.cpp).
typedef void *(__stdcall *MmAllocateContiguousMemoryExFn)(uint32_t size, uint32_t lowest, uint32_t highest,
                                                        uint32_t alignment, uint32_t protection);
#define KernelMmAllocateContiguousMemoryEx (*(MmAllocateContiguousMemoryExFn *)0x0015d1dc)

// AUTOINJECT
void* allocateAligned0x1000(int numBytes) {
    void *memory = KernelMmAllocateContiguousMemoryEx(numBytes, 0, 0xffffffffu, 0x1000, PAGE_READWRITE);
    if (memory == NULL)
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return memory;
}

// The cache flush before the GPU reads what the CPU wrote (WBINVD). Nothing reads behind the CPU's back here:
// the graphics backend copies what it is told has changed.
// FUNC_AT(000e8f80)
void psiCacheFlush(void) {
}

// FUNC_AT(000dfb20)
void psiCacheFlush_Copy(void) {
}

// ---------------------------------------------------------------------------------------------------------------
// Upper-cases a string in place, a-z only (0x000f542e, which sits with XAPI's code). Menu_ValidateCodename uses
// it on a typed codename.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(000f542e)
void String_ToUpperAscii(char *text) {
    for (; *text != '\0'; text++)
        if (*text > '`' && *text < '{')
            *text -= 0x20;
}
