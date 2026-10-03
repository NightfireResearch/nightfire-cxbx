#pragma once

#include "../drivinghelpers.h"

#include <stdint.h>

// A list of file names (an MSVC std::list<char[256]>, 12 bytes: the allocator's byte, the head node, the size).
// The loader keeps three while its request logging is on (UFileLoader::StartUsingBigFile): every file loaded,
// those found in the big file and those not, and writes them out as text at the end of the run
// (UFileLoader::DumpFileRequestList) - the lists the big file's contents were built from.

// A node: the links, then the name (0x108 bytes).
struct FileNameNode {
    FileNameNode *next;
    FileNameNode *prev;
    char name[256];
};
static_assert(sizeof(FileNameNode) == 0x108, "a file-name node is 0x108 bytes");

class FileNameList {
public:
    uint8_t allocator;
    uint8_t unknown1[3];
    FileNameNode *head;
    uint32_t size;

    // The constructor (0x00117330) and destructor (0x00117350).
    FileNameList* Construct();
    void Destruct();
    // Appends a name, as the loader does for every file it opens while its request logging is on (0x001174d0).
    void AddFile(char *name);
    // Writes the names to a file, one per line (0x00117160).
    void Dump(const char *path);

    // The list's own helpers: a node (0x00117120) and the head node (0x00117300), erase (0x001172b0), and the
    // size check behind every insert (0x00117420).
    FileNameNode* BuyNode(FileNameNode *next, FileNameNode *prev, const char *name);
    FileNameNode* BuyHeadNode();
    FileNameNode** Erase(FileNameNode **result, FileNameNode *first, FileNameNode *last);
    void IncSize(uint32_t count);
};
static_assert(sizeof(FileNameList) == 12, "a list is 12 bytes");
