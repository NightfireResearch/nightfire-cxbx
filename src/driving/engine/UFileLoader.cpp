#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "../platform/FILE.h"
#include "../platform/FileSys.h"
#include "../eagl/Realgraph.h"
#include "../data/DataUntested.h"
#include "../data/Tree.h"

#include "UFileLoader.h"
#include "FileNameList.h"
#include "UMemory.hpp"

#include <stdio.h>
#include <string.h>

#include "../devtools/FileDump.h"

// ---------------------------------------------------------------------------------------------------------------
// UFileLoader (see UFileLoader.h) and the file-name lists of its request logging (FileNameList.h).
//
// FileLoad and FileLoadz (the AUTOINJECT'd three functions further down) are the project's older replacements:
// besides what the original does they print every path, and with settings.ini's DumpFiles=on save a copy of every
// file loaded under dump_driving\ (devtools/FileDump.cpp). The functions above them are ports of the originals.
// ---------------------------------------------------------------------------------------------------------------

#define BigFileName ((char *)0x002431d8)          // [256], the big file's name, for the request lists' files
#define BigFilePath ((char *)0x002430d8)          // [256], cleared and not otherwise used
#define BigFileOpen BOOL8_AT(0x002434da)
#define BigFileId I32_AT(0x002434dc)
#define LogRequests BOOL8_AT(0x002434e0)
#define TrustBigFile I32_AT(0x001e4768)           // non-zero: add the big file without checking it exists

#define RequestedFiles (*(FileNameList *)0x00243508)      // every file loaded
#define FilesInBigFile (*(FileNameList *)0x00243514)      // found in the big file
#define FilesNotInBigFile (*(FileNameList *)0x002434e4)   // not found there

constexpr int kBigFileAllocFlags = 0x100;
constexpr int kBigFilePriority = 2;
constexpr int kDumpOpenMode = 0x26;                // FILESYS_open's mode: write, create, truncate
constexpr int kDumpPriority = 100;

// "|" + path, FILE_'s prefix for a file in an open archive.
static void BigFilePathOf(char *out, const char *path) {
    out[0] = '|';
    strcpy(out + 1, path);
}

// FUNC_AT(0x00116f00)
bool UFileLoader::LookupAbsolutePath(char *pathOut, const char *pathIn) {
    for (; *pathIn != 0; pathIn++, pathOut++)
        *pathOut = *pathIn == '/' ? '\\' : *pathIn;
    *pathOut = 0;
    return true;
}

// Inlined - can't be injected
void * UFileLoader::FileLoadDirectFromDisk(char* param_1, int param_2, bool z_variant) {
    if (z_variant) {
        return FILE_load(param_1, param_2);
    }
    return FILE_loadz(param_1, param_2);
}

// Inlined - can't be injected
void UFileLoader::AddFileToRequestList(char* fname) {
    if (LogRequests == 1) {
        RequestedFiles.AddFile(fname);
    }
}

// 0x00117530, reached only from FileLoad (ours)
void* UFileLoader::AttemptBigFileLoad(char *param_1, undefined4 param_2) {
    char path[256];
    BigFilePathOf(path, param_1);
    void* loaded = FILE_loadpackz(path, param_2);
    if (LogRequests != 0)
        (loaded != NULL ? FilesInBigFile : FilesNotInBigFile).AddFile(param_1);
    return loaded;
}

// FUNC_AT(0x00116e10)
bool UFileLoader::StartUsingBigFile(const char *directory, const char *name, bool logRequests) {
    BigFileName[0] = 0;
    BigFilePath[0] = 0;
    BigFileOpen = 0;
    LogRequests = logRequests;
    if (name == NULL)
        return false;
    strcpy(BigFileName, name);
    char path[256];
    sprintf(path, "%s\\%s.viv", directory, name);
    if (TrustBigFile != 0 || FILE_exists(path)) {
        if (FILESYS_addbigsync(path, kBigFileAllocFlags, kBigFilePriority, &BigFileId)) {
            BigFileOpen = 1;
            return true;
        }
    }
    return BigFileOpen;
}

// FUNC_AT(0x00116ed0)
void UFileLoader::StopUsingBigFile() {
    if (BigFileOpen == 1) {
        int id = BigFileId;
        BigFileOpen = 0;
        FILESYS_delbigsync(id, kBigFilePriority);
    }
}

// FUNC_AT(0x00116f30)
int UFileLoader::AttemptBigFileExists(char *path) {
    char packed[256];
    BigFilePathOf(packed, path);
    return FILE_exists(packed);
}

// FUNC_AT(0x00116f70)
int UFileLoader::AttemptBigFileSize(char *path) {
    char packed[256];
    BigFilePathOf(packed, path);
    int size = int(FILE_unpacksizez(packed));
    if (size == 0)
        size = FILE_sizez(packed);
    return size;
}

// FUNC_AT(0x00116fc0)
int UFileLoader::FileLoadAt(const char *path, void *buffer, int size) {
    char fixed[256];
    LookupAbsolutePath(fixed, path);
    return FILE_loadat(fixed, buffer, size);
}

// FUNC_AT(0x00117010)
int UFileLoader::FileExists(const char *path) {
    char fixed[256];
    char absolute[256];
    LookupAbsolutePath(fixed, path);
    if (AttemptBigFileExists(fixed) == 1)
        return 1;
    if (LookupAbsolutePath(absolute, path))
        return FILE_exists(absolute);
    return 0;
}

// FUNC_AT(0x001170a0)
int UFileLoader::FileSize(const char *path) {
    char fixed[256];
    LookupAbsolutePath(fixed, path);
    int size = AttemptBigFileSize(fixed);
    if (size == 0) {
        size = int(FILE_unpacksizez(fixed));
        if (size == 0)
            size = FILE_sizez(fixed);
    }
    return size;
}

// FUNC_AT(0x00117200)
void UFileLoader::DumpFileRequestList() {
    if (LogRequests != 1)
        return;
    char inFile[288];
    char requestFile[288];
    char outFile[288];
    sprintf(requestFile, "%s_req.txt", BigFileName);
    RequestedFiles.Dump(requestFile);
    if (BigFileOpen == 1) {
        sprintf(inFile, "%s_in.txt", BigFileName);
        FilesInBigFile.Dump(inFile);
        sprintf(outFile, "%s_out.txt", BigFileName);
        FilesNotInBigFile.Dump(outFile);
    }
}

// FUNC_AT(0x001173a0)
void UFileLoader::Startup() {
    FileNameNode *ignored;
    RequestedFiles.Erase(&ignored, RequestedFiles.head != NULL ? RequestedFiles.head->next : NULL, RequestedFiles.head);
    FilesInBigFile.Erase(&ignored, FilesInBigFile.head != NULL ? FilesInBigFile.head->next : NULL, FilesInBigFile.head);
    FilesNotInBigFile.Erase(&ignored, FilesNotInBigFile.head != NULL ? FilesNotInBigFile.head->next : NULL,
                            FilesNotInBigFile.head);
}

// FUNC_AT(0x001175a0)
void* UFileLoader::AttemptBigFileShapeLoad(char *path, int flags) {
    char packed[256];
    BigFilePathOf(packed, path);
    void *loaded = SHAPE_loadfilez(packed, flags);
    if (LogRequests != 0)
        (loaded != NULL ? FilesInBigFile : FilesNotInBigFile).AddFile(path);
    return loaded;
}

// FUNC_AT(0x001176f0)
void* UFileLoader::ShapeFileLoad(char *path, int flags, bool uncompressed) {
    char fixed[256];
    LookupAbsolutePath(fixed, path);
    void *loaded = AttemptBigFileShapeLoad(fixed, flags);
    if (loaded == NULL) {
        loaded = uncompressed ? SHAPE_loadfile(fixed, flags) : SHAPE_loadfilez(fixed, flags);
        if (loaded == NULL)
            return NULL;
    }
    // (the original flushes the CPU cache here, WBINVD, for the GPU's sake - nothing to do on a PC)
    if (loaded != NULL && LogRequests == 1)
        RequestedFiles.AddFile(fixed);
    return loaded;
}

// FUNC_AT(0x00117790)
void* UFileLoader::ShapeFileLoad(char *path, int flags) {
    return ShapeFileLoad(path, flags, true);
}

// FUNC_AT(0x000e3c20)
void* UFileLoader::FileLoadThunk(char *rawPath, int flags) {
    return FileLoad(rawPath, flags);
}

// ---- the request lists

// FUNC_AT(0x00117120)
FileNameNode* FileNameList::BuyNode(FileNameNode *next, FileNameNode *prev, const char *name) {
    FileNameNode *node = static_cast<FileNameNode *>(UMemory::FastAlloc(sizeof(FileNameNode), "STL"));
    if (node != NULL) {
        node->next = next;
        node->prev = prev;
        memcpy(node->name, name, sizeof(node->name));
    }
    return node;
}

// FUNC_AT(0x00117160)
void FileNameList::Dump(const char *path) {
    int file;
    FILESYS_opensync(path, kDumpOpenMode, kDumpPriority, &file);
    int offset = 0;
    for (FileNameNode *node = head != NULL ? head->next : NULL; node != head; node = node->next) {
        char line[200];
        sprintf(line, "%s\n", node->name);
        offset += FILESYS_writesync(file, offset, line, int(strlen(line)), kDumpPriority);
    }
    FILESYS_closesync(file, kDumpPriority);
}

// FUNC_AT(0x00117300)
FileNameNode* FileNameList::BuyHeadNode() {
    FileNameNode *node = static_cast<FileNameNode *>(UMemory::FastAlloc(sizeof(FileNameNode), "STL"));
    if (node != NULL)
        node->next = node;
    node->prev = node;   // unguarded in the original too (it tests &node->prev, never null)
    return node;
}

// FUNC_AT(0x00117330)
FileNameList* FileNameList::Construct() {
    head = BuyHeadNode();
    size = 0;
    return this;
}

// FUNC_AT(0x00117350)
void FileNameList::Destruct() {
    FileNameNode *ignored;
    Erase(&ignored, head != NULL ? head->next : NULL, head);
    if (head != NULL)
        UMemory::FastFree(head, sizeof(FileNameNode));
    head = NULL;
    size = 0;
}

// FUNC_AT(0x001172b0)
FileNameNode** FileNameList::Erase(FileNameNode **result, FileNameNode *first, FileNameNode *last) {
    while (first != last) {
        FileNameNode *node = first;
        first = first->next;
        if (node != head) {
            node->prev->next = node->next;
            node->next->prev = node->prev;
            UMemory::FastFree(node, sizeof(FileNameNode));
            size--;
        }
    }
    *result = first;
    return result;
}

// FUNC_AT(0x00117420)
void FileNameList::IncSize(uint32_t count) {
    if (0xffffff - size < count)
        TreeThrow("list<T> too long", kLengthErrorVtable, kLengthErrorThrowInfo);
    size += count;
}

// FUNC_AT(0x001174d0)
void FileNameList::AddFile(char *name) {
    char copy[256];
    strcpy(copy, name);
    FileNameNode *end = head;
    FileNameNode *node = BuyNode(end, end->prev, copy);
    IncSize(1);
    end->prev = node;
    node->prev->next = node;
}

// ---- the project's FileLoad (settings.ini DumpFiles=on also saves each file loaded: devtools/FileDump.cpp)

#include "../platform/RealMemory.h"   // MEM_size, ours now

// AUTOINJECT
void* UFileLoader::FileLoad(const char *rawPath, int param_2, bool param_3) {

    char fixedPath [256];

    // Normalise file path
    UFileLoader::LookupAbsolutePath(fixedPath, rawPath);

    // OUR DEBUG: Print details
    printf("-------- Loading file %s, %i, %s\n", fixedPath, param_2, param_3 ? "true" : "false");

    // First try from BIG archive
    void* loadedFile = UFileLoader::AttemptBigFileLoad(fixedPath, param_2);

    // Otherwise try direct file
    if (loadedFile == NULL) {
        loadedFile = UFileLoader::FileLoadDirectFromDisk(fixedPath, param_2, param_3);
    }

    // No luck either from archive or filesystem means the file is missing
    if(loadedFile == NULL)
        return NULL;

    FileDump_Save(fixedPath, loadedFile, MEM_size(loadedFile));   // settings.ini DumpFiles=on only

    UFileLoader::AddFileToRequestList(fixedPath);

    return loadedFile;
}

// The two-argument overload (0x001176b0), which the original writes as exactly this. It has to be replaced in
// its own right: the three-argument replacement used to be patched over both, and read a third argument from
// the stack of callers that had pushed two.
//
// AUTOINJECT
void* UFileLoader::FileLoad(const char *rawPath, int flags) {
    return FileLoad(rawPath, flags, true);
}

// AUTOINJECT
void* UFileLoader::FileLoadz(const char *fname, int flags) {
    return FileLoad(fname, flags, false);
}
