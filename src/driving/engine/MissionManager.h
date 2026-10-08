#ifndef DRIVING_ENGINE_MISSIONMANAGER_H_
#define DRIVING_ENGINE_MISSIONMANAGER_H_

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// SMissionManager (one, glbMissionManager): not ported. Only the fields ported code reads are mapped.
// ---------------------------------------------------------------------------------------------------------------

struct SMissionManager {
    uint8_t unknown000[0x474];
    int32_t unknown474;         // +0x474 3 or 4: no targeting; 4: no auto-drive camera input
    uint8_t unknown478;         // +0x478 set: ResolveCollision weighs two cars by car class and hit points
    uint8_t unknown479[3];
    int32_t unknown47c;         // +0x47c non-zero: no auto-drive camera rotation, spin, zoom or aiming
    uint8_t unknown480[0x1c];
    float playerHealth;         // +0x49c the player's car's hit point location
    uint8_t unknown4a0[0x3c];
    int32_t unknown4dc;         // +0x4dc 0..3: the auto-drive camera's auto-aim limits are (3 - it) thirds
    uint8_t unknown4e0[0x10];
    int32_t unknown4f0;         // +0x4f0 non-zero: no targeting, no auto-drive camera spin
    uint8_t unknown4f4[0x21c];
    uint8_t unknown710;         // +0x710 zero: the auto-drive camera's lock-on input sets CameraLockOnFlag
};
static_assert(offsetof(SMissionManager, unknown478) == 0x478 && offsetof(SMissionManager, unknown47c) == 0x47c &&
              offsetof(SMissionManager, playerHealth) == 0x49c && offsetof(SMissionManager, unknown4dc) == 0x4dc &&
              offsetof(SMissionManager, unknown4f0) == 0x4f0 && offsetof(SMissionManager, unknown710) == 0x710,
              "mission manager layout");

#define glbMissionManager (*(SMissionManager **)0x00239220)

#endif // DRIVING_ENGINE_MISSIONMANAGER_H_
