#ifndef PSISAVE_H_
#define PSISAVE_H_

#include "../actionhelpers.h"

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

#endif // PSISAVE_H_
