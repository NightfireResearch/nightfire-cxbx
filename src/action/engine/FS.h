#ifndef FS_H_
#define FS_H_

#include "../actionhelpers.h"

int FS_MatchFilenameToHeader(char* filename);
void FS_Init(void);
bool FS_StateMachineIterate(void);
int FS_GetFileSize(char *filename);
int openOrCreateFile(char *nameRelated, char write);
bool FS_OpInProgressWithCleanup(void);
void FS_LoadFileIfReady(char *fileName, void *buffer);
void* FS_AllocateAndLoadBlocking(char *filename, MallocFlags allocType, int *sizeOut);
char* FS_EurocomPath(const char *name);   // "d:\eurocom\<name>", in a static buffer

// The sound code's streams: an archive file read in pieces, asynchronously
int psiAsyncOpenFile(char *fileName);
void psiAsyncCloseFile(int fileHandle);
void psiAsyncReadFile(int fileHandle, int byteOffset, int byteSize, void *destBuffer);
int psiAsyncHasReadFinished(int fileHandle);


#endif // FS_H_