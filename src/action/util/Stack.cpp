#include "Stack.h"

#include "../memory.h"
#include <string.h>

// AUTOINJECT
void* Stack_Top(STACKINFO* stack) {

    if(stack == NULL)
        return NULL;
    
    if(stack->fillLevel == 0)
        return *stack->items;

    return stack->items[stack->fillLevel - 1];

}

// AUTOINJECT
bool Stack_Push(STACKINFO* stack, void* item) {

    if(stack == NULL)
        return false;

    if(stack->fillLevel >= stack->size)
        return false;

    stack->items[stack->fillLevel++] = item;
    return true;
}

// AUTOINJECT
void* Stack_Pop(STACKINFO* stack) {

    if(stack == NULL)
        return NULL;

    if(stack->fillLevel == 0)
        return stack->items[0];

    return stack->items[--stack->fillLevel];
}

// AUTOINJECT
bool Stack_IsEmpty(STACKINFO* stack) {

    if(stack == NULL)
        return false;

    return stack->fillLevel == 0;
}

// AUTOINJECT
bool Stack_Init(STACKINFO* stack, ushort size, int *mem) {

    if(stack == NULL)
        return false;

    stack->items = (void**)mem;
    stack->size = size;
    stack->fillLevel = 0;

    if(mem == NULL && size > 0) {
        stack->items = (void**)Mem_Malloc(size * sizeof(void*), 0x1B04, 0);
        memset(stack->items, 0, size * sizeof(void*));
    }

    return true;
}