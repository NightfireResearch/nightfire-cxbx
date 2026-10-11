#ifndef DRIVING_GAME_FINGODEATHSOUND_H_
#define DRIVING_GAME_FINGODEATHSOUND_H_

// ---------------------------------------------------------------------------------------------------------------
// AFingoDeath: the vehicle sound PBondCar::InitAudioObject makes for the car type "fodbase" - an AVehicle with a
// gun firing "SFX_fingergunfire" one shot at a time (with shell casings) and a turret, and a reload loop played
// while its recharge is under one, then a one-shot when it is full. See FingoDeathSound.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "VehicleSound.h"               // AVehicle
#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning

// (0x200 bytes; vtable 0x001a2b60)
class AFingoDeath : public AVehicle {
public:
    AVoice *reloadVoice;        // +0x1f0 bank slot 4's "SFX_fingerReloadLP" while recharging (names ours)
    float rechargePercent;      // +0x1f4 one from the constructor: charged
    bool recharging;            // +0x1f8
    uint8_t unknown1f9[7];

    AFingoDeath* Construct();                                                       // 0x00129a30
    void Destruct();                                                                // 0x001297a0
    AFingoDeath* Delete(unsigned int flags);                                        // 0x00129a00

    // AVehicle's Play, then the reload: its loop while the recharge is under one, pitched up with it, and
    // "SFX_fingerReloadST" once when it is full again (vtable slot 3).
    void Play(ASoundPlayParams *params);                                            // 0x00129830
    // vtable slot 9
    void SetRechargePercent(float percent);                                         // 0x00129820
};
static_assert(offsetof(AFingoDeath, reloadVoice) == 0x1f0 && offsetof(AFingoDeath, recharging) == 0x1f8,
              "AFingoDeath layout");
static_assert(sizeof(AFingoDeath) == 0x200, "AFingoDeath is 0x200 bytes");

#endif // DRIVING_GAME_FINGODEATHSOUND_H_
