#include "../actionhelpers.h"

#include <stddef.h>

typedef struct {
    char _pad_1[0x8];
    HASHCODE nextLevelHashcode;
    HASHCODE currentLevelHashcode;
} GameState_struct;

#define GameState (*(GameState_struct*)0x001f6580) // FIXME: It's somewhere near here

// With some of the optimisations the Xbox compiler applied, it's not trivial to know where the struct itself starts, but it 
// is often possible to locate some of the fields in the decomplied code.
// As a sanity check, let's pin a few of these known points down with asserts
static_assert(((int)(&GameState) + offsetof(GameState_struct, currentLevelHashcode)) == 0x001f658c, "Location of GameState, or offset of currentLevelHashcode not correct");



