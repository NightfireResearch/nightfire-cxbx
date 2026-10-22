#ifndef LLNODE_H
#define LLNODE_H

#include "../../common/xbeClass.h"

#pragma pack(push, 1)
// Forward declaration of LLNODE
struct LLNODE_tag;

// Must be the first element in a struct
typedef struct LLNODE_tag {
  struct LLNODE_tag *prev;
  struct LLNODE_tag *next;
} LLNODE_tag;

// Forward declaration of LLISTINFO
struct LLISTINFO_tag;

// Must be the first element in a struct
typedef struct LLISTINFO_tag {
  struct LLNODE_tag *head;
  struct LLNODE_tag *tail;
  unsigned short count;
  unsigned short elementSize;
 } LLISTINFO_tag;

#pragma pack(pop)

// The comparisons of LList_InsertAt and QuickSort (util/Sort.cpp)
typedef int (__cdecl *CompareFunc)(const void *a, const void *b);

bool LList_Init(LLISTINFO_tag *list, unsigned short entrySize, unsigned short numEntries);
bool LList_Add(LLISTINFO_tag *list, LLNODE_tag *newElement);
void LList_Insert(LLISTINFO_tag *list, LLNODE_tag *insertionPoint, LLNODE_tag *newElement);
LLNODE_tag* LList_Cut(LLISTINFO_tag *list);
void LList_AllocnNodes(LLISTINFO_tag *list, int n);
// LList_Init and LList_AllocnNodes of nodes that are xboxEntrySize bytes on the Xbox (XboxHeap.h; XBOX_SIZEOF)
#if XBOX_SIZES_DIFFER
bool LList_InitXbox(LLISTINFO_tag *list, unsigned short entrySize, unsigned short xboxEntrySize,
                    unsigned short numEntries);
void LList_AllocnNodesXbox(LLISTINFO_tag *list, int n, int xboxEntrySize);
#else
#define LList_InitXbox(list, entrySize, xboxEntrySize, numEntries) LList_Init(list, entrySize, numEntries)
#define LList_AllocnNodesXbox(list, n, xboxEntrySize) LList_AllocnNodes(list, n)
#endif
LLNODE_tag* LList_Remove(LLISTINFO_tag *list, LLNODE_tag *element);
LLNODE_tag* LList_InsertAt(LLISTINFO_tag *list, LLNODE_tag *element, char fromHead, CompareFunc compare);

#endif