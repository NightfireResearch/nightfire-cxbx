#ifndef STACK_H
#define STACK_H

#include <stddef.h> // for offsetof
#include "../../common/xbeClass.h"
#include "../../helpers.h"

#pragma pack(push, 1)

typedef struct {
    void** items;
    ushort fillLevel;
    ushort size;
} STACKINFO;

LAYOUT_32(sizeof(STACKINFO) == 0x8, "STACKINFO size incorrect");

#pragma pack(pop)

void* Stack_Top(STACKINFO* stack);
void* Stack_Pop(STACKINFO* stack);
bool Stack_Push(STACKINFO* stack, void* item);
bool Stack_IsEmpty(STACKINFO* stack);
bool Stack_Init(STACKINFO* stack, ushort size, int *mem);

#endif // STACK_H