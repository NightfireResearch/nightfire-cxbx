#include "psiSave.h"
#include "XboxFile.h"
#include "../actionhelpers.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <direct.h>
#include <io.h>


#define SAVE_DIR "saves"

// Builds "saves/<profileName>.dat", replacing anything that isn't alphanumeric/space/dash/underscore with an
// underscore - the original's own per-profile folder name (built by FUN_000e3340) is similarly limited to a
// fixed-size buffer of plain characters, so this should never need to touch a legitimate Codename name.
static void BuildSavePath(char *out, size_t outSize, const char *profileName) {
    char safeName[64];
    size_t i = 0;
    for (; profileName[i] != '\0' && i < sizeof(safeName) - 1; i++) {
        char c = profileName[i];
        safeName[i] = (isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ') ? c : '_';
    }
    safeName[i] = '\0';

    snprintf(out, outSize, "%s/%s.dat", SAVE_DIR, safeName);
}

// Original: psiSaveData -> FUN_000dfc60 (which also maintains an MRU list of save names, unrelated to the
// actual file write) -> maybeSaveFileRelated, which resolves a per-Codename folder on the "u:\" utility drive
// (Xbox's hard drive save partition, named via FUN_000e3340 from the profileName below), creates a
// "SaveMeta.xbx" sidecar file the Xbox dashboard needs to show save info outside the game, XOR-scrambles the
// buffer with a timestamp-derived byte and signs it with XCalculateSignature (tamper protection), then writes
// a custom 29-byte header + the (still scrambled) payload via the raw NT kernel file APIs - all of it against
// the Xbox hard drive/utility-partition layer.
//
// None of that Xbox-specific plumbing serves any purpose off real hardware, so - matching how XLaunchNewImageA
// bypasses the kernel's launch-data page entirely in favour of a plain host file (see launchInfo.cpp) - this
// writes the save buffer straight to a plain host file instead, with no header, scrambling, signing, or
// SaveMeta.xbx sidecar - just one file per Codename under a local "saves" folder.
//
// AUTOINJECT
undefined4 psiSaveData(void *data, uint32_t size, const char *profileName) {
    if (profileName == NULL || data == NULL || (int32_t)size < 1) {
        psiInternalLoadingDataState = 5; // matches maybeSaveFileRelated's "bad parameters" result
        return 1;
    }

    _mkdir(SAVE_DIR); // ignore error - EEXIST is fine

    char path[256];
    BuildSavePath(path, sizeof(path), profileName);

    bool ok = false;
    FILE *file = fopen(path, "wb");
    if (file != NULL) {
        ok = fwrite(data, 1, size, file) == size;
        fclose(file);
    }

    psiInternalLoadingDataState = ok ? 4 : 5; // 4 = success, 5 = failure (both matching the original's codes)
    return 1;
}

// Original: psiLoadData -> FUN_000e3700 (mirrors psiSaveData's per-Codename path resolution) -> FUN_000e3580,
// which reads the 29-byte header back, validates the magic/size/signature, then XOR-descrambles the payload
// using the key byte stored in the header. We skip all of that the same way psiSaveData's write side does.
//
// AUTOINJECT
undefined4 psiLoadData(void *buffer, uint32_t *sizeInOut, const char *profileName) {
    if (profileName == NULL || buffer == NULL || sizeInOut == NULL || (int32_t)*sizeInOut < 1) {
        psiInternalLoadingDataState = 2; // matches FUN_000e3700's "bad parameters" result
        return 1;
    }

    memset(buffer, 0, *sizeInOut);

    char path[256];
    BuildSavePath(path, sizeof(path), profileName);

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        psiInternalLoadingDataState = 3; // matches FUN_000e3580's "couldn't open/read" result
        return 1;
    }

    size_t bytesRead = fread(buffer, 1, *sizeInOut, file);
    fclose(file);

    psiInternalLoadingDataState = (bytesRead > 0) ? 1 : 3; // 1 = success, matching the original's code
    return 1;
}

// ---------------------------------------------------------------------------------------------------------------
// Codenames list enumeration.
//
// The chain the menu drives is psiStartSaveEnum -> repeated psiGetNextSave -> psiEndSaveEnum. psiGetNextSave's
// real work happens in psiGetNextSaveName (FUN_000dfce0), which walks the saves one entry at a time via
// SaveEnum_GetNextEntry (between SaveDrive_Open and SaveDrive_Close), copying each name into a fixed cache table
// at 0x002a2e68 (9 bytes - 8 chars + NUL - per entry). Once that walk reaches the end, it latches SaveGamesCached
// to 1 and from then on every subsequent enumeration just replays that cache instead of walking again. That's a
// sensible way to dodge Xbox hard-drive latency on real hardware; for us it would mean a Codename saved after the
// first time the menu was opened would never appear again without restarting the whole game.
//
// SaveEnum_GetNextEntry below is replaced to walk our own "saves" folder instead of "u:\". The original opened
// and closed a find handle on "u:\" either side of the walk (SaveDrive_Open / SaveDrive_Close); those are no
// longer needed and do nothing. psiStartSaveEnum clears SaveGamesCached every time, so psiGetNextSaveName always
// does a fresh walk. psiGetNextSaveName and psiEndSaveEnum are ours too; psiGetNextSave is untouched.
// ---------------------------------------------------------------------------------------------------------------

// Set once a walk of the saves has reached the end; from then on psiGetNextSaveName replays SaveGameCache instead
// of walking again. psiStartSaveEnum (ours) clears it every time, so in practice the replay is never used - see
// the section comment. Only psiStartSaveEnum, psiGetNextSaveName, psiEndSaveEnum and the dead FUN_000dfc60 (its
// only caller was the original psiSaveData, replaced) touch it. (PS2 has no cache; the name is ours.)
// XBE_GLOBAL(0x002adf18, 0x1)
static uint8_t SaveGamesCached;

// The next cache entry to replay (PS2: SaveGamesReturned). Only psiStartSaveEnum, psiGetNextSaveName and
// psiEndSaveEnum use it.
// XBE_GLOBAL(0x002adf1c, 0x4)
static int32_t SaveGamesReturned;

// Still the game's: FUN_000dfd80 (reached from FUN_000dfec0, the delete path) removes entries from the cache and
// decrements the count.
#define NumberSaveGamesGot (*(int32_t*)0x002adf14)                       // PS2's name for the count
#define SAVE_NAME_SLOT 9                                                 // 8 characters + NUL
#define SaveGameCache ((char(*)[SAVE_NAME_SLOT])0x002a2e68)                // no fixed length in the original

// How many entries Menu_EnumSaves takes from psiGetNextSave per frame: 2 while the drive is being walked, then
// 0x200 once the cache is complete. Menu_EnumSaves (original) reads it.
#define SaveEnum_EntriesPerFrame (*(int32_t*)0x00194804)
#define SAVE_ENUM_ENTRIES_PER_FRAME_CACHED 0x200

// A cache slot whose first byte is this was a "skip this entry" result, replayed as such
#define SAVE_CACHE_SKIPPED ((char)0xff)
#define SAVE_ENUM_SKIP ((char*)-1)

// psiInternalLoadingDataState value psiEndSaveEnum leaves behind
#define SAVE_STATE_ENUM_ENDED 11

// The original's find handle on "u:\" around a walk of the saves (0x000e3850 opens it, 0x000e3430 closes it).
// The walk is of our own folder now (SaveEnum_GetNextEntry), so there is nothing to open.
// FUNC_AT(000e3850)
void SaveDrive_Open(void) {
}

// FUNC_AT(000e3430)
void SaveDrive_Close(void) {
}

static intptr_t g_saveEnumHandle = -1;
static bool g_saveEnumStarted = false;
static char g_saveEnumNameBuf[16]; // matches the original's 9-byte (8 char + NUL) cache slot size

// AUTOINJECT
void psiStartSaveEnum(void) {
    U32_AT(0x002adf0c) = 0;
    U8_AT(0x0025edba) = 0;
    psiInternalLoadingDataState = 10;
    SaveGamesReturned = 0;

    SaveGamesCached = 0; // force psiGetNextSaveName to do a fresh walk instead of ever trusting the stale cache
    NumberSaveGamesGot = 0;
    SaveDrive_Open();

    if (g_saveEnumHandle != -1) {
        _findclose(g_saveEnumHandle);
        g_saveEnumHandle = -1;
    }
    g_saveEnumStarted = false;
}

// Replaces the "get next real entry" step of the enumeration - see the block comment above. Contract preserved
// exactly for FUN_000dfce0 above this: return a name pointer for a found entry, (char*)-1 to skip an entry
// while continuing (never produced here), or NULL once there are no more.
//
// AUTOINJECT
char* SaveEnum_GetNextEntry(void) {
    _finddata32_t findData;
    bool found;

    if (!g_saveEnumStarted) {
        g_saveEnumStarted = true;
        g_saveEnumHandle = _findfirst32(SAVE_DIR "/*.dat", &findData);
        found = g_saveEnumHandle != -1;
    } else if (g_saveEnumHandle != -1) {
        found = _findnext32(g_saveEnumHandle, &findData) == 0;
    } else {
        found = false;
    }

    if (!found) {
        if (g_saveEnumHandle != -1) {
            _findclose(g_saveEnumHandle);
            g_saveEnumHandle = -1;
        }
        return NULL;
    }

    // Recover the Codename by stripping the ".dat" extension back off the filename
    strncpy(g_saveEnumNameBuf, findData.name, sizeof(g_saveEnumNameBuf) - 1);
    g_saveEnumNameBuf[sizeof(g_saveEnumNameBuf) - 1] = '\0';
    char *dot = strrchr(g_saveEnumNameBuf, '.');
    if (dot != NULL)
        *dot = '\0';

    return g_saveEnumNameBuf;
}

// The body of psiGetNextSave (0x000dfe60, still original, which only turns the result into its counters): the
// next save name, (char*)-1 for an entry to skip, or NULL at the end. The PS2 build has all of this in
// psiGetNextSave itself, without the cache.
//
// Quirks kept from the original:
// - a live walk returns SaveEnum_GetNextEntry's own pointer, not the cache copy; only the replay returns cache
//   slots.
// - the copy into the 9-byte slot is an unbounded strcpy, and nothing limits the count: a name longer than 8
//   characters runs into the next slot (our SaveEnum_GetNextEntry hands out up to 15).
// - the replay index is compared signed (JL).
//
// FUNC_AT(000dfce0)
char* psiGetNextSaveName(void) {

    if(SaveGamesCached) {
        int idx = SaveGamesReturned;
        if(idx >= NumberSaveGamesGot)
            return NULL;
        char *slot = SaveGameCache[idx];
        SaveGamesReturned = idx + 1;
        if(slot[0] == SAVE_CACHE_SKIPPED)
            return SAVE_ENUM_SKIP;
        return slot;
    }

    char *name = SaveEnum_GetNextEntry();

    if(name == NULL) {
        // The walk is complete: replay from now on, much faster, and close the drive handle
        SaveGamesCached = 1;
        SaveEnum_EntriesPerFrame = SAVE_ENUM_ENTRIES_PER_FRAME_CACHED;
        SaveDrive_Close();
        return NULL;
    }

    if(name == SAVE_ENUM_SKIP) {
        // The original stores AL, the low byte of the -1 it is about to return
        SaveGameCache[NumberSaveGamesGot][0] = SAVE_CACHE_SKIPPED;
        NumberSaveGamesGot++;
        return SAVE_ENUM_SKIP;
    }

    strcpy(SaveGameCache[NumberSaveGamesGot], name);
    NumberSaveGamesGot++;
    return name;
}

// Ends an enumeration pass. The drive handle is closed here only if the walk did not reach its end - when it did,
// psiGetNextSaveName has closed it already.
// AUTOINJECT
void psiEndSaveEnum(void) {
    SaveGamesReturned = 0;
    if(!SaveGamesCached)
        SaveDrive_Close();
    psiInternalLoadingDataState = SAVE_STATE_ENUM_ENDED;
}

// ---------------------------------------------------------------------------------------------------------------
// The save drive. Deleting a Codename (0x000e3790, through XDeleteSaveGame on the original) removes its file;
// the space checks (0x000e37c0, 0x000e3810) count the drive in the dashboard's 16 KB blocks, as the "not enough
// space" message does.
// ---------------------------------------------------------------------------------------------------------------

#define SAVE_DELETED 8
#define SAVE_DELETE_FAILED 9

// FUNC_AT(000e3790)
int SaveDrive_Delete(const char *profileName) {
    if (profileName == NULL)
        return SAVE_DELETE_FAILED;
    char path[256];
    BuildSavePath(path, sizeof(path), profileName);
    return remove(path) == 0 ? SAVE_DELETED : SAVE_DELETE_FAILED;
}

// Free blocks on the save drive, 50000 at most (0x000e37c0)
// FUNC_AT(000e37c0)
uint32_t SaveDrive_FreeBlocks(void) {
    ULARGE_INTEGER freeBytes;
    if (!Xbox_GetDiskFreeSpaceExA("u:\\", &freeBytes, NULL, NULL))
        return 0;
    uint64_t blocks = freeBytes.QuadPart >> 14;
    return blocks < 50000 ? (uint32_t)blocks : 50000;
}

// The blocks a save of size bytes takes: whole clusters, two more for the folder and its metadata, in blocks
// rounded up (0x000e3810)
// FUNC_AT(000e3810)
int SaveDrive_BlocksFor(int size) {
    int cluster = (int)Xbox_GetVolumeClusterSize("u:\\");
    if (cluster < 1)
        return 0;
    int bytes = ((cluster + 0x1c + size) / cluster + 2) * cluster + 0x3fff;
    return bytes / 0x4000;
}

// The menu code's copies of the two (0x000dfbc0, 0x000dfbd0)
// FUNC_AT(000dfbc0)
uint32_t SaveDrive_FreeBlocks_Thunk(void) {
    return SaveDrive_FreeBlocks();
}

// FUNC_AT(000dfbd0)
int SaveDrive_BlocksFor_Thunk(int size) {
    return SaveDrive_BlocksFor(size);
}
