#ifndef LLNODE_H
#define LLNODE_H

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

bool LList_Add(LLISTINFO_tag *list, LLNODE_tag *newElement);
void LList_Insert(LLISTINFO_tag *list, LLNODE_tag *insertionPoint, LLNODE_tag *newElement);
LLNODE_tag* LList_Cut(LLISTINFO_tag *list);
void LList_AllocnNodes(LLISTINFO_tag *list, int n);

#endif