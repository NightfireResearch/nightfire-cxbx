#pragma once

#include "../drivinghelpers.h"
#include "FileNameList.h"   // its methods are defined (and injected) in UFileLoader.cpp

// UFileLoader: the game's front to the file system. A path is normalised ('/' to '\'), tried in the big file
// opened at start-up ("|" + path: FILE_'s archive prefix) and then on the disc. While request logging is on,
// every file is recorded in FileNameList's three lists, written out by DumpFileRequestList.

class UFileLoader {
public:
    // '/' to '\', into pathOut (0x00116f00). Always true.
    static bool LookupAbsolutePath(char *pathOut, const char *pathIn);
    // From the big file ("|" + path) with FILE_loadpackz, recording the path in the in/not-in lists while
    // request logging is on (0x00117530).
    static void* AttemptBigFileLoad(const char *path, int flags);
    // A file: the big file first, then the disc, as stored (uncompressed) or unpacked (0x00117610); the
    // two-argument form loads it as stored (0x001176b0), FileLoadz unpacked (0x001176d0).
    static void* FileLoad(const char *path, int flags, bool uncompressed);
    static void* FileLoad(const char *path, int flags);
    static void* FileLoadz(const char *path, int flags);
    // The linker's thunk to the two-argument FileLoad (0x000e3c20).
    static void* FileLoadThunk(char *rawPath, int flags);

    // Opens directory\name.viv as the big file (0x00116e10); logRequests turns the request lists on.
    static bool StartUsingBigFile(const char *directory, const char *name, bool logRequests);
    static void StopUsingBigFile();                                  // 0x00116ed0
    static int AttemptBigFileExists(char *path);                     // 0x00116f30: 1 if the big file has it
    static int AttemptBigFileSize(char *path);                       // 0x00116f70
    static int FileLoadAt(const char *path, void *buffer, int size); // 0x00116fc0: into the caller's buffer
    static int FileExists(const char *path);                         // 0x00117010
    static int FileSize(const char *path);                           // 0x001170a0
    static void DumpFileRequestList();                               // 0x00117200
    static void Startup();                                           // 0x001173a0: the lists emptied
    static void* AttemptBigFileShapeLoad(char *path, int flags);     // 0x001175a0
    // A shape (texture) file, compressed (z false) or not (0x001176f0), and the two-argument form (0x00117790).
    static void* ShapeFileLoad(char *path, int flags, bool uncompressed);
    static void* ShapeFileLoad(char *path, int flags);
};
