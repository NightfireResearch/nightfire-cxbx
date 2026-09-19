#ifndef XBOXFILE_H_
#define XBOXFILE_H_

#include <windows.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// The game's file I/O on Win32 - see XboxFile.cpp for why this is only five functions. The Xbox's XAPI is a
// Win32 clone, so these are CreateFileA, ReadFile, GetFileSizeEx, GetOverlappedResult and CloseHandle under
// other names, and everything the game layers on top of them (openOrCreateFile, maybeReadFile,
// FS_OperationInProgress, the archive reader) is already correct Win32 logic and is left untouched.
//
// Declared here so the generated injection table can see them. Not meant to be called from new code - use the
// Win32 functions directly.
// ---------------------------------------------------------------------------------------------------------------

HANDLE __stdcall createFile(const char *filename, uint32_t desiredAccess, uint32_t shareMode,
                            void *securityAttributes, uint32_t creationDisposition,
                            uint32_t flagsAndAttributes, HANDLE templateFile);

int __stdcall readFromFileBlocking(HANDLE fileHandle, void *buffer, uint32_t len,
                                   uint32_t *bytesRead, OVERLAPPED *overlapped);

// These three return int rather than bool on purpose - their callers test the whole of EAX, and a C++ bool
// return only sets AL. See XboxFile.cpp.
int __stdcall Xbox_GetOverlappedResult(HANDLE fileHandle, OVERLAPPED *overlapped,
                                       uint32_t *bytesTransferred, int wait);

int __stdcall getFileSize_LargeInteger(HANDLE fileHandle, LARGE_INTEGER *fileSize);

int __stdcall DoNtClose(HANDLE handle);

int __stdcall FileWrite(HANDLE fileHandle, void *buffer, uint32_t len,
                        uint32_t *bytesWritten, OVERLAPPED *overlapped);

int __stdcall file_flush(HANDLE fileHandle);

int __stdcall setSomeInfo(HANDLE fileHandle);                 // SetEndOfFile

uint32_t __stdcall querySetSomeInfo(HANDLE fileHandle, uint32_t distanceLow, uint32_t *distanceHigh,
                                    uint32_t moveMethod);     // SetFilePointer

int __stdcall Xbox_SetFilePointerEx(HANDLE fileHandle, uint32_t distanceLow, uint32_t distanceHigh,
                                    uint32_t *newPosition, uint32_t moveMethod);

int __stdcall MaybeFileCreateNew(const char *filename);       // DeleteFile

uint32_t __stdcall Xbox_GetFileAttributesA(const char *filename);   // GetFileAttributes

// Prints the streaming-read summary if PerfLog is on and enough time has passed. Called once a frame from
// the graphics backend's frame pacing, which is the only thing that reliably runs every frame.
void XboxFile_ReportStreamingIfDue(void);

uint32_t __stdcall Xbox_FindFirstFileA(const char *filename, WIN32_FIND_DATAA *findData);
int __stdcall Xbox_FindNextFileA(HANDLE findHandle, WIN32_FIND_DATAA *findData);

#endif // XBOXFILE_H_
