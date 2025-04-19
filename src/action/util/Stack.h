#ifndef STACK_H
#define STACK_H

#include "../actionhelpers.h"

typedef struct {
    void** items;
    ushort fillLevel;
    ushort size;
} STACKINFO;

void* Stack_Top(STACKINFO* stack);
void* Stack_Pop(STACKINFO* stack);
bool Stack_Push(STACKINFO* stack, void* item);
bool Stack_IsEmpty(STACKINFO* stack);
bool Stack_Init(STACKINFO* stack, ushort size, int *mem);

#endif // STACK_H