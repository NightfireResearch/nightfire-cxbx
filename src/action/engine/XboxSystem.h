#ifndef XBOXSYSTEM_H_
#define XBOXSYSTEM_H_

#include <stdint.h>

// The Xbox layer's system services (0x000dbcd0-0x000dc0d0, 0x000e3320-0x000e3700, 0x000e89d0, 0x000e8f80-
// 0x000e93f0): the language, moving between the two engines, the signed state file, contiguous memory.

int Language_Get(void);                 // the language, as the game's language code (0 Dutch .. 8 American)
void Language_SaveChoice(void);         // remembers the language picked on the language page
int Language_FromDashboard(void);       // the dashboard's language, as a language code

void *GetPTPData(void);                 // the launch data page, or null if there is none
void *GetPTPData_Copy(void);            // a second copy of GetPTPData, at 0x000e9340
void SetLaunchInfoAndLaunch(char *executableName, void *data, uint32_t len);
void WriteStateFileAndLaunch(uint32_t reason, void *data, int len);
void psiLaunchDriving(void *data, uint32_t len);
bool psiGetDrivingData(uint32_t *out);
void maybeCleanupSystem(void);
int psiGetLoadingDataState(void);

bool MaybeFileWriteContents(char *path, uint8_t *data, uint32_t len);
bool StateFile_Read(char *path, void *data, uint32_t len);
bool File_Delete(char *path);
bool File_Exists(char *path);

void* allocateAligned0x1000(int numBytes);
void psiCacheFlush(void);
void psiCacheFlush_Copy(void);
void String_ToUpperAscii(char *text);   // a-z only, in place

#endif // XBOXSYSTEM_H_
