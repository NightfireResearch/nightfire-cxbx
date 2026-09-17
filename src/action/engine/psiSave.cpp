#include "psiSave.h"
#include "../actionhelpers.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <direct.h>
#include <io.h>

// Mirrors psiInternalLoadingDataState - the original overloads this single global as "the last save/load
// operation's result code", polled via psiLoadingData()/FUN_000dfbb0 by the save/load menu flow (XBox_DoSaveFlow
// etc.) to know when a (normally async, hard-drive-latency-bound) operation has finished, and whether it
// succeeded. Since our reimplementation completes synchronously, we just set the final result directly here -
// no separate "still in progress" state is ever actually observed by the poller.
#define psiInternalLoadingDataState U32_AT(0x002adf10)

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
// a custom 29-byte header + the (still scrambled) payload via the raw NT kernel file APIs - all of which goes
// through CXBX's own emulation of the Xbox hard drive/utility-partition layer.
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
// real work happens in FUN_000dfce0 (untouched, still original code), which - the first time it's asked, per
// process - walks Xbox's "u:\" hard drive partition one entry at a time via SaveEnum_GetNextEntry (opened by
// FUN_000e3850, closed by FUN_000e3430), copying each name into a fixed cache table at 0x0029a2e68 (9 bytes -
// 8 chars + NUL - per entry). Once that walk reaches the end, FUN_000dfce0 latches DAT_002adf18 to 1 and from
// then on - for the rest of the process's life - every subsequent enumeration just replays that cache instead
// of ever walking "u:\" again. That's a sensible way to dodge Xbox hard-drive latency on real hardware; for us
// it means a Codename saved after the first time the menu was opened would never appear again without
// restarting the whole game. (This isn't a psiLaunch.bin thing - DAT_002adf18 and the cache table are ordinary
// BSS, zero-initialised fresh on every process start regardless of psiLaunch.bin's contents; the staleness is
// purely a within-one-process-lifetime problem.)
//
// SaveEnum_GetNextEntry below is replaced to walk our own "saves" folder instead of "u:\" (FUN_000e3850/
// FUN_000e3430 still run either side of it and still open/close a real "u:\" handle via CXBX in parallel -
// harmless, since this ignores it entirely). psiStartSaveEnum is replaced only to force DAT_002adf18 back to 0
// every time, so FUN_000dfce0 always does a fresh walk via SaveEnum_GetNextEntry instead of ever falling back
// to the stale cache. psiGetNextSave/psiEndSaveEnum/FUN_000dfce0/FUN_000e3850/FUN_000e3430 are all untouched.
// ---------------------------------------------------------------------------------------------------------------

#define DAT_002adf18 U8_AT(0x002adf18)

// Opens (FUN_000e3850) / closes (FUN_000e3430) the real "u:\" handle nothing else uses any more - still called
// so we don't have to touch FUN_000dfce0's open/close bookkeeping around them.
static void CallOriginal_FUN_000e3850() {
    void(__stdcall * fn)() = (void(__stdcall *)())0x000e3850;
    fn();
}

static intptr_t g_saveEnumHandle = -1;
static bool g_saveEnumStarted = false;
static char g_saveEnumNameBuf[16]; // matches the original's 9-byte (8 char + NUL) cache slot size

// AUTOINJECT
void psiStartSaveEnum(void) {
    U32_AT(0x002adf0c) = 0;
    U8_AT(0x0025edba) = 0;
    psiInternalLoadingDataState = 10;
    U32_AT(0x002adf1c) = 0;

    DAT_002adf18 = 0; // force FUN_000dfce0 to do a fresh walk instead of ever trusting the stale cache
    U32_AT(0x002adf14) = 0;
    CallOriginal_FUN_000e3850();

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
