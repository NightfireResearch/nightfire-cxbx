#ifndef STACK_H
#define STACK_H

#include "../actionhelpers.h"

typedef struct {
    void* items;
    ushort fillLevel;
    ushort size;
} STACKINFO;

#endif // STACK_H