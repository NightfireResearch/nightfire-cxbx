#include "psiInput.h"
#include <assert.h>
#include "../actionhelpers.h"

// AUTOGEN
void psiInput_ResetInputState(unsigned int i);
// AUTOGEN
void psiInput_RumbleSetIntensity(unsigned int i, unsigned short a, unsigned short b);

// Array of 4 uint32_t entries, all initialised to 0xFFFFFFFF
#define controller_maybeRumbleTimeout ((unsigned int*)0x0019481c)

// AUTOINJECT
void psiInput_ResetRumble(unsigned int i) {

    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    assert(i <= 3);

    psiInput_RumbleSetIntensity(i, 0, 0);
    controller_maybeRumbleTimeout[i] = 0xffffffff;

}

// AUTOINJECT
void psiInputReset(void) {

    psiInput_ResetInputState(0);
    psiInput_ResetInputState(1);
    psiInput_ResetInputState(2);
    psiInput_ResetInputState(3);
    
    psiInput_ResetRumble(0);
    psiInput_ResetRumble(1);
    psiInput_ResetRumble(2);
    psiInput_ResetRumble(3);    

}