#include "Locks.h"

#include "../../util/Random.h"

// XBE_GLOBAL(0x0029aaf8, 0xff)
KeyCodeEntry KeyCodes[51];

// The 51 keycodes, four digits each, none discovered, after a run of the random sequence that depends on the frame
// AUTOINJECT
void Locks_Init(void) {

    // Seed the random number generator
    for(uint i = 0; i < ((GameState.NumFramesUnpaused * 10) & 0x1ff); i++) {
        Rand_Random();
    }

    // Set up each key code entry
    for(uint i = 0; i < ARRAY_SIZE(KeyCodes); i++) {

        for (uint digit = 0; digit < 4; digit++) {
#if FIX_ORIGINAL_GAME_BUGS
            KeyCodes[i].asciiDigit[digit] = '0' + (char)(Rand_Random() % 10);
#else
            KeyCodes[i].asciiDigit[digit] = '0' + (char)(Rand_Random() % 9);    // never a '9' (docs/original-bug-fixes.md)
#endif
        }

        KeyCodes[i].discovered = false;

    }

}
