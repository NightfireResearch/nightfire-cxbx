#ifndef MUSIC_H_
#define MUSIC_H_

#include "../actionhelpers.h"

void Music_Event(uint param_1,undefined4 param_2);
uint32_t __cdecl Get_Music_Event(int evtId);

#pragma pack(push, 1)

// The music's view of the fighting (0x001ee260). NDrone2_SeenAndAttacking fills it in from the drones; the music
// code (UpdateMusicalEvents and the per-level MFX updates, still the game's) sets the thresholds and reads it. Only
// the fields NDrone2_SeenAndAttacking uses are named.
typedef struct MusicVars_t {
    ushort combatThreshold;     // 0x00 - attackers it takes to start the combat music
    ushort calmThreshold;       // 0x02 - Drone_InitComms ends the combat music when no more drones than this count
    uint seenSeconds;           // 0x04 - a drone counts while it saw its opponent this recently
    float closeRange;           // 0x08 - or, once the combat music is on, while it is this close
    ushort combat;              // 0x0c - the combat music is on
    ushort attackers;           // 0x0e - drones counted
    uint combatFrame;           // 0x10 - GameState.NumFramesUnpaused when it was last started
    float nearestOpponent;      // 0x14 - the least distance to an opponent, of every drone
    float nearestSeenOpponent;  // 0x18 - the same, of drones that see theirs
    uint lastSeenFrame;         // 0x1c - the latest a drone saw its opponent
    uint countingStarted;       // 0x20 - 0: NDrone2_SeenAndAttacking starts its list of counted drones afresh
} MusicVars_t;

#pragma pack(pop)

static_assert(offsetof(MusicVars_t, countingStarted) == 0x20, "Wrong offset for countingStarted");

// XBE_GLOBAL(0x001ee260, 0x24)
#define MusicVars (*(MusicVars_t *)0x001ee260)


#endif // MUSIC_H_