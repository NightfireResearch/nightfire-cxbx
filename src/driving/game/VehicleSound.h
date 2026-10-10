#ifndef DRIVING_GAME_VEHICLESOUND_H_
#define DRIVING_GAME_VEHICLESOUND_H_

// ---------------------------------------------------------------------------------------------------------------
// AVehicle and the kinds the vehicles make (not ported): a vehicle's engine sound - PBondCar::InitAudioObject's
// APlayerVehicle, ATrafficVehicle and the others, PHelicopter's AHelicopter - and its GetAudio's answer.
// Provisional: the fields the ported vehicle and camera code use, under Ghidra's names where it has them, until
// the audio classes are ported.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../audio/Sound.h"             // ABaseSound
#include "../data/CoordConvert.h"       // Coord3

class AVehicle : public ABaseSound {
public:
    uint8_t weaponFlags;            // +0xc0 the weapon flags PBondCar::Simulate works out
    uint8_t unknownC1;
    uint8_t flagC2;                 // +0xc2 the camera's: rotating about y, or spinning
    uint8_t flagC3;                 // +0xc3 rotating about x
    uint8_t flagC4;                 // +0xc4 aiming
    uint8_t unknownC5;              // +0xc5 cleared with the machine gun
    uint8_t unknownC6[0xa];
    int32_t unknownD0;              // +0xd0 7 with the machine gun
    int32_t surfaces[4];            // +0xd4 under each wheel, 0 off the ground
    int32_t previousSurfaces[4];    // +0xe4 the step before's
    float suspensionCompression[4]; // +0xf4
    float frontSlip;                // +0x104 at most 1
    float rearSlip;                 // +0x108 at most 1
    uint8_t unknown10C[8];
    float skidMultiple;             // +0x114 SFX_SKIDMULTIPLE when at least 1
    float throttle;                 // +0x118
    float rpm;                      // +0x11c
    uint8_t unknown120[0x20];
    float speed;                    // +0x140 the vehicle's
    uint8_t unknown144[0xc];
    float unknown150;
    uint8_t unknown154[3];
    uint8_t boost;                  // +0x157 set by PBondCar::EnableRocketBoost, cleared when the boost runs out
    uint8_t engineOn;               // +0x158
    uint8_t unknown159[0x21];
    uint8_t unknown17A;
    uint8_t unknown17B;
    uint8_t xyTrigger;              // +0x17c PBondCar::GetControllerInput's
    uint8_t xyReleaseTrigger;       // +0x17d
    uint8_t unknown17E[2];
    float steering;                 // +0x180
    float steeringVertical;         // +0x184
    void *gun;                      // +0x188 ActivateGun's AGun

    // The sound's frame: its forward, right and up vectors (Ghidra: FUN_00061a60; the name is ours)
    void SetOrientation(const Coord3 *forward, const Coord3 *right, const Coord3 *up);         // 0x00061a60
};
static_assert(offsetof(AVehicle, weaponFlags) == 0xc0 && offsetof(AVehicle, flagC2) == 0xc2 &&
              offsetof(AVehicle, unknownD0) == 0xd0 && offsetof(AVehicle, frontSlip) == 0x104 &&
              offsetof(AVehicle, skidMultiple) == 0x114 && offsetof(AVehicle, speed) == 0x140 &&
              offsetof(AVehicle, boost) == 0x157 && offsetof(AVehicle, engineOn) == 0x158 &&
              offsetof(AVehicle, xyTrigger) == 0x17c && offsetof(AVehicle, steering) == 0x180 &&
              offsetof(AVehicle, gun) == 0x188, "AVehicle layout");

// The AI's cars' engine sound (0x240 bytes)
class ATrafficVehicle : public AVehicle {
public:
    uint8_t unknown18C[0x8c];
    float randomPhase;              // +0x218 a random number below 0.2 from PBondCar::InitAudioObject (name ours)
    uint8_t unknown21C[0x24];
};
static_assert(offsetof(ATrafficVehicle, randomPhase) == 0x218 && sizeof(ATrafficVehicle) == 0x240,
              "ATrafficVehicle layout");

// A helicopter's engine sound (0x310 bytes)
class AHelicopter : public AVehicle {
public:
    uint8_t unknown18C[0x174];
    float randomPhase;              // +0x300 a random number below 0.2 (InitAudioObject) or 0.1 (PHelicopter's
                                    //        constructor; name ours)
    uint8_t unknown304[0xc];
};
static_assert(offsetof(AHelicopter, randomPhase) == 0x300 && sizeof(AHelicopter) == 0x310, "AHelicopter layout");

#endif // DRIVING_GAME_VEHICLESOUND_H_
