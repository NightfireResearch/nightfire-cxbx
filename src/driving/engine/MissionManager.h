#ifndef DRIVING_ENGINE_MISSIONMANAGER_H_
#define DRIVING_ENGINE_MISSIONMANAGER_H_

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// SMissionManager (one, glbMissionManager): not ported. Only the fields ported code reads are mapped.
// ---------------------------------------------------------------------------------------------------------------

class Missile;

// SMissionManager::missionState's values the ported code tests (neutral names)
enum MissionState : int32_t {
    kMissionState3 = 3,
    kMissionState4 = 4,
};

struct SMissionManager {
    uint8_t unknown000[0x474];
    int32_t missionState;       // +0x474 MissionState: 3 or 4, the mission is over (no targeting, no firing); 4: no
                                //        auto-drive camera input
    uint8_t autoDrive;          // +0x478 set in auto-drive: the hands fire the weapons; ResolveCollision weighs two
                                //        cars by car class and hit points
    uint8_t unknown479[3];
    Missile *driveMissile;      // +0x47c the missile being steered (SetDriveMissile), NULL for none; while there is
                                //        one, no auto-drive camera rotation, spin, zoom or aiming
    uint8_t unknown480[0x1c];
    float playerHealth;         // +0x49c the player's car's hit point location
    uint8_t unknown4a0[0x3c];
    int32_t unknown4dc;         // +0x4dc 0..3: the auto-drive camera's auto-aim limits are (3 - it) thirds
    uint8_t unknown4e0[0x10];
    int32_t cinematicMode;      // +0x4f0 non-zero: no targeting, no firing, no auto-drive camera spin
    uint8_t unknown4f4[0xc];
    int32_t unknown500;         // +0x500 one more for each scoreable sentry made
    uint8_t unknown504[0x20c];
    uint8_t unknown710;         // +0x710 set: the sniper rifle fires only while the camera is locked on; zero: the
                                //        auto-drive camera's lock-on input sets CameraLockOnFlag
};
static_assert(offsetof(SMissionManager, autoDrive) == 0x478 && offsetof(SMissionManager, driveMissile) == 0x47c &&
              offsetof(SMissionManager, playerHealth) == 0x49c && offsetof(SMissionManager, unknown4dc) == 0x4dc &&
              offsetof(SMissionManager, cinematicMode) == 0x4f0 && offsetof(SMissionManager, unknown500) == 0x500 &&
              offsetof(SMissionManager, unknown710) == 0x710, "mission manager layout");

#define glbMissionManager (*(SMissionManager **)0x00239220)

// Its methods, not ported (thiscall: the manager, then a dummy EDX)
#define SMissionManager_IncShotsFired ((void (__fastcall *)(SMissionManager *, int, int count))0x000b66f0)
#define SMissionManager_IncShotsHit ((void (__fastcall *)(SMissionManager *, int, bool hit))0x000b6720)
#define SMissionManager_IncKills ((void (__fastcall *)(SMissionManager *, int, int kills))0x000b6770)
#define SMissionManager_ProgrammerDefinedEvent ((void (__fastcall *)(SMissionManager *, int, int event, const char *text))0x000b72f0)
#define SMissionManager_IncMagicCounter ((void (__fastcall *)(SMissionManager *, int, int counter))0x000b7a40)
#define SMissionManager_SetMagicCounter ((void (__fastcall *)(SMissionManager *, int, int counter, int value))0x000b7ac0)
#define SMissionManager_InitPlayerCar ((void (__fastcall *)(SMissionManager *, int))0x000b8090)
#define SMissionManager_SetDriveMissile ((void (__fastcall *)(SMissionManager *, int, Missile *missile))0x000b80b0)

#endif // DRIVING_ENGINE_MISSIONMANAGER_H_
