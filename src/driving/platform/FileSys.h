#ifndef DRIVING_PLATFORM_FILESYS_H_
#define DRIVING_PLATFORM_FILESYS_H_

// EA's file system layer as the driving engine has it - FILESYS (asynchronous file operations on a worker
// thread), the FILE_ whole-file helpers on top, big (.viv) archives underneath, and what is left of ASYNCFILE.
// See FileSys.cpp, and docs/driving/filesys.md for the original.

#include "RealSystem.h"

#include <stdint.h>

typedef void *(*FileMallocFn)(const char *name, int size, int flags);
typedef void (*FileFreeFn)(void *data);
typedef void (*FsCallback)(unsigned handle, int status, int userData);   // an op's completion, on the worker
typedef int (*FsAtomicFn)(int priority, void *argument);
typedef char (*FsReadErrorFn)(void);

unsigned QueueKeyIdentity(void *node);   // 0x0014a540, shared beyond FILESYS

void FILESYS_setmemcallbacks(FileMallocFn allocate, FileFreeFn release);
bool FILESYS_init(int numFiles, int cdBuffer, int numOps);
bool FILESYS_initadr(int numFiles, int cdBuffer, int numOps, void *block);
void FILEDEV_setreaderror(FsReadErrorFn handler);

unsigned FILESYS_open(const char *name, int mode, int priority, int userData);
unsigned FILESYS_close(int slot, int priority, int userData);
unsigned FILESYS_read(int slot, int offset, void *buffer, int count, int priority, int userData);
unsigned FILESYS_write(int slot, int offset, const void *buffer, int count, int priority, int userData);
unsigned FILESYS_size(int slot, int priority, int userData);
unsigned FILESYS_exists(const char *name, int priority, int userData);
unsigned FILESYS_addbig(const char *name, int allocFlags, int priority, int userData);
unsigned FILESYS_delbig(int bigId, int priority, int userData);

int FILESYS_opstatus(unsigned handle);
int FILESYS_waitop(unsigned handle);
int FILESYS_completeop(unsigned handle);
void FILESYS_callbackop(unsigned handle, FsCallback callback);
void FILESYS_cancelop(unsigned handle);
void FILESYS_priorityop(unsigned handle, int priority);
int FILESYS_atomic(FsAtomicFn function, int worker, int priority, void *argument);

bool FILESYS_opensync(const char *name, int mode, int priority, int *outSlot);
bool FILESYS_closesync(int slot, int priority);
int FILESYS_sizesync(int slot, int priority);
bool FILESYS_addbigsync(const char *name, int allocFlags, int priority, int *outId);
bool FILESYS_delbigsync(int bigId, int priority);
bool FILESYS_existssync(const char *name, int priority);
int FILESYS_readsync(int slot, int offset, void *buffer, int count, int priority);
int FILESYS_writesync(int slot, int offset, const void *buffer, int count, int priority);

bool FILE_exists(char *path);
int FILE_sizez(char *path);
void* FILE_load(char *path, int flags);
void* FILE_loadz(char *path, int flags);
void* FILE_loadsizez(char *path, int *outSize, int flags);
int FILE_loadat(char *path, void *buffer, int size);
int FILE_loadatz(char *path, void *buffer, int size);
bool FILE_save(char *path, void *buffer, int size);
unsigned FILE_unpacksizez(char *path);
void* FILE_loadpackz(char *path, int flags);

int ASYNCFILE_init(int count, int allocFlags);
int ASYNCFILE_cancel(unsigned id);
void ASYNCFILE_restore();

// Called on the worker after every op it executes, when set (devtools/FileSysTrace.cpp).
typedef void (*FsTraceFn)(int type, int priority, const char *name, int status, int bytes);
extern FsTraceFn FsTrace;

// For the shadow test (devtools/FileSysShadow.cpp): a .viv directory lookup, as the worker does it.
const char *BIG_find(const uint8_t *directory, const char *name, int index, int *outOffset, int *outSize);
int BIG_dirsize(const uint8_t *header);

#endif // DRIVING_PLATFORM_FILESYS_H_
