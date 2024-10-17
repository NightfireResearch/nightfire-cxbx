#include <stddef.h>

#include "../../math/math.h"


#define MAX_MP_AGENTS 8 // = 4 bots + 4 players? Or is it 7??

typedef struct {
    char _pad_1[0x1e0]; // TODO: Fill this out
    bool isMultiplayer;
    char _pad_2[0x4b];
    bool miniVehiclesEnabled; // bool, or enum/flags?
} MPSettings_struct;

#define MPSettings (*(MPSettings_struct*)0x0025fe38) // FIXME: Do this properly

static_assert((int)(&MPSettings) + offsetof(MPSettings_struct, isMultiplayer) == 0x00260018, "Location of MPSettings, or offset of isMultiplayer not correct");

static_assert((int)(&MPSettings) + offsetof(MPSettings_struct, miniVehiclesEnabled) == 0x00260064, "Location of MPSettings, or offset of miniVehiclesEnabled not correct");


// This struct is complete and correct
typedef struct {
    _VECTOR spawnPos;
    _VECTOR facingDir;
    short initialised;
    short _pad;
} MPSpawnPoint;

// True on Xbox, PS2 is larger due to padding of _VECTOR
static_assert(sizeof(MPSpawnPoint) == 0x1c, "Size of MPSpawnPoint not correct");
