#ifndef FILE_H_
#define FILE_H_


#include "../drivinghelpers.h"

// Ours now: platform/FileSys.cpp (FileSys.h has the rest of the layer).
void* FILE_load(char* path, int flags);
void* FILE_loadz(char* path, int flags);
void* FILE_loadpackz(char* path, int flags);

#endif // FILE_H_