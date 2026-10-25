#ifndef PSISAVE_H_
#define PSISAVE_H_

#include "../actionhelpers.h"

// Mirrors psiInternalLoadingDataState - the original overloads this single global as "the last save/load
// operation's result code", polled via psiLoadingData()/FUN_000dfbb0 by the save/load menu flow (XBox_DoSaveFlow
// etc.) to know when a (normally async, hard-drive-latency-bound) operation has finished, and whether it
// succeeded. Since our reimplementation completes synchronously, we just set the final result directly here -
// no separate "still in progress" state is ever actually observed by the poller.
extern uint32_t psiInternalLoadingDataState;

// psiInternalLoadingDataState's values: what the last load, save, delete or listing came to
enum PsiSaveState {
    PSISAVE_IDLE = 0,
    PSISAVE_LOADED = 1,
    PSISAVE_LOAD_BAD_ARGS = 2,
    PSISAVE_LOAD_FAILED = 3,        // the save could not be opened or read
    PSISAVE_SAVED = 4,
    PSISAVE_SAVE_FAILED = 5,        // bad arguments, or the save could not be written
    PSISAVE_6 = 6,                  // only XBox_DoSaveFlow tests these two
    PSISAVE_7 = 7,
    PSISAVE_DELETED = 8,
    PSISAVE_DELETE_FAILED = 9,
    PSISAVE_ENUM_RUNNING = 10,      // psiStartSaveEnum's
    PSISAVE_ENUM_ENDED = 11,        // psiEndSaveEnum's
    PSISAVE_12 = 12,                // only XBox_DoSaveFlow tests it
};

// psiLoadingData's, psiSavingData's and psiDeletingData's answers (PS2 names)
enum PsiPollResult {
    PSI_DONE = 2,
    PSI_NOT_DONE = 4,
};

// The entries psiGetNextSave has handed out in this enumeration, skipped ones included
extern uint32_t SavesListed;

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
char* psiGetNextSave(void);
void psiEndSaveEnum(void);

int psiLoadingData(void);       // PSI_DONE when the save was read, else PSI_NOT_DONE
int psiSavingData(void);        // PSI_DONE when the save was written, else PSI_NOT_DONE
int psiDeletingData(void);      // always PSI_NOT_DONE
bool psiDeleteSave(const char *profileName);     // true if the codename's save was deleted
int SaveGame_Delete(const char *profileName);   // PSISAVE_DELETED (and out of the cache), else PSISAVE_DELETE_FAILED

void SaveDrive_Open(void);
void SaveDrive_Close(void);
int SaveDrive_Delete(const char *profileName);   // PSISAVE_DELETED or PSISAVE_DELETE_FAILED
uint32_t SaveDrive_FreeBlocks(void);              // 16 KB blocks free on the save drive
int SaveDrive_BlocksFor(int size);                // 16 KB blocks a save of size bytes takes
uint32_t SaveDrive_FreeBlocks_Thunk(void);
int SaveDrive_BlocksFor_Thunk(int size);

#endif // PSISAVE_H_
