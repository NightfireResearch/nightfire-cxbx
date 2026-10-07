#ifndef DRIVING_ENGINE_MISSIONMANAGER_H_
#define DRIVING_ENGINE_MISSIONMANAGER_H_

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// SMissionManager (one, glbMissionManager): not ported. Only the fields ported code reads are mapped.
// ---------------------------------------------------------------------------------------------------------------

struct SMissionManager {
    uint8_t unknown000[0x474];
    int32_t unknown474;         // +0x474 3 or 4: no targeting
    uint8_t unknown478;         // +0x478 set: ResolveCollision weighs two cars by car class and hit points
    uint8_t unknown479[0x23];
    float playerHealth;         // +0x49c the player's car's hit point location
    uint8_t unknown4a0[0x50];
    int32_t unknown4f0;         // +0x4f0 non-zero: no targeting
};
static_assert(offsetof(SMissionManager, unknown478) == 0x478 && offsetof(SMissionManager, playerHealth) == 0x49c &&
              offsetof(SMissionManager, unknown4f0) == 0x4f0, "mission manager layout");

#define glbMissionManager (*(SMissionManager **)0x00239220)

#endif // DRIVING_ENGINE_MISSIONMANAGER_H_
