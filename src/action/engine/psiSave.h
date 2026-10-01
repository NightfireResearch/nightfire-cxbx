#ifndef PSISAVE_H_
#define PSISAVE_H_

#include "../actionhelpers.h"

// Mirrors psiInternalLoadingDataState - the original overloads this single global as "the last save/load
// operation's result code", polled via psiLoadingData()/FUN_000dfbb0 by the save/load menu flow (XBox_DoSaveFlow
// etc.) to know when a (normally async, hard-drive-latency-bound) operation has finished, and whether it
// succeeded. Since our reimplementation completes synchronously, we just set the final result directly here -
// no separate "still in progress" state is ever actually observed by the poller.
#define psiInternalLoadingDataState U32_AT(0x002adf10)

// Real signature: psiSaveData(dataPtr, size, profileName) - "profileName" is the current Codename's name: the
// original threads it (as a char*) into FUN_000e3340 to build a per-profile folder on the "u:\" drive, so each
// Codename gets its own save slot rather than sharing one.
undefined4 psiSaveData(void *data, uint32_t size, const char *profileName);

// Real signature: psiLoadData(buffer, &sizeInOut, profileName) - sizeInOut is the destination buffer's
// capacity; profileName is threaded into FUN_000e3340 exactly as on the save side.
undefined4 psiLoadData(void *buffer, uint32_t *sizeInOut, const char *profileName);

// Starts a Codenames-list enumeration pass - see the block comment above SaveEnum_GetNextEntry's definition.
void psiStartSaveEnum(void);

// The "get next real entry" step of that enumeration, called repeatedly by psiGetNextSave's (untouched) inner
// implementation until it returns NULL. Renamed from Ghidra's auto-generated FUN_000e3880 (and given a real
// return type) now that we know what it does - see the block comment above its definition.
char* SaveEnum_GetNextEntry(void);

// The body of psiGetNextSave (Ghidra: FUN_000dfce0): the next save name, (char*)-1 to skip one, NULL at the end.
char* psiGetNextSaveName(void);
void psiEndSaveEnum(void);

#endif // PSISAVE_H_
