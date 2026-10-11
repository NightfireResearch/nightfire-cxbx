#ifndef DRIVING_GAME_WEAPONSOUND_H_
#define DRIVING_GAME_WEAPONSOUND_H_

// ---------------------------------------------------------------------------------------------------------------
// The game tier's sounds of things that move, shoot and get hit (game.audio):
//
//   - ASceneObj: the base of AVehicle, ASentry and ASmackable: the flags a gun and a turret are played from, and
//     the virtual methods the vehicles call (PlayLanding, PlayCollision, IsStillActive, IsTracked);
//   - AGun: a scene object's gun - a loop played while it fires, or a one-shot sound per shot, and the odd shell
//     casing; ATurret: its turret's motors (sideways, up and down, the servo);
//   - ASentry: a scene object with a turret and a gun; ASmackable: a thing that rolls and is hit;
//   - ATargeting: the HUD's lock sounds;
//   - AWorldSound's Play and destructor, and AVoice::Rewind (the classes are audio/Sound.h's and Voice.h's).
//
// The vtables stay the game's. AGun, ATurret and ATargeting are not sounds: plain objects, from operator new or
// UMemory's pools. See WeaponSound.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../audio/Sound.h"             // ABaseSound, ASoundPlayParams, AWorldSound
#include "../audio/Voice.h"             // AVoice
#include "../data/CoordConvert.h"       // Coord3
#include "../data/StdStreams.h"         // GameStd::String
#include "../world/Targeting.h"         // TargetLockState
#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../../common/xbeOverload.h"   // XbeVirtual

class ABank;
class AMix;

// ---- ASceneObj (0xd0 bytes; vtable 0x001a2f80)

class ASceneObj : public ABaseSound {
public:
    // The weapon flags (names ours, from what ATurret::Play and AGun::Play do with them)
    uint8_t servo;              // +0xc0 the turret's servo: "SFX_Kachung" and "SFX_servo" when set, "SFX_servodown"
                                //       when cleared
    uint8_t firing;             // +0xc1 the gun fires while it is exactly 1
    uint8_t turningSideways;    // +0xc2 the turret's "SFX_TurretLR" ("SFX_TurretLRStop" when cleared)
    uint8_t turningUpDown;      // +0xc3 the turret's "SFX_TurretUD" ("SFX_TurretUDStop" when cleared)
    uint8_t aiming;             // +0xc4 the player camera's, while it aims
    uint8_t gunLoops;           // +0xc5 one from the constructor: the gun's voice plays while it fires; clear: a
                                //       one-shot sound per shot
    uint8_t unknownC6[0xa];

    // A scene object in the mix `mixName`, heard by the active views.
    ASceneObj* Construct(const char *mixName);                                      // 0x0012e190
    void Destruct();                                                                // 0x0012e1e0
    ASceneObj* Delete(unsigned int flags);                                          // 0x0012e1f0

    // vtable slot 5: nothing (the slot's arguments: an impact, a position, a velocity and a surface)
    void PlayCollision(float impact, const Coord3 *position, const Coord3 *velocity, int surface);  // 0x00037a10

    // The virtual methods, through the vtable. Slot 3: Play (nothing in the base). Slot 4: PlayLanding (nothing).
    // Slot 5: PlayCollision. Slot 6: IsStillActive (false in the base). Slot 7: IsTracked (false in the base).
    bool CallIsTracked() {
        return (this->*XbeVirtual<IsTrackedMethod>(this, 7))();
    }

private:
    typedef bool (ASceneObj::*IsTrackedMethod)();
};
static_assert(offsetof(ASceneObj, servo) == 0xc0 && offsetof(ASceneObj, firing) == 0xc1 &&
              offsetof(ASceneObj, turningSideways) == 0xc2 && offsetof(ASceneObj, turningUpDown) == 0xc3 &&
              offsetof(ASceneObj, aiming) == 0xc4 && offsetof(ASceneObj, gunLoops) == 0xc5, "ASceneObj's flags");
static_assert(sizeof(ASceneObj) == 0xd0, "ASceneObj is 0xd0 bytes");

// ---- AGun (0x7c bytes, from operator new; AVehicle::ActivateGun's, ASentry's)

class AGun {
public:
    ASceneObj *owner;           // +0x00 the object it belongs to: where the shots are heard, and its flags
    ABank *bank;                // +0x04 bank slot 4
    AMix *mix;                  // +0x08 the mix named by the constructor
    AVoice *voice;              // +0x0c the fire patch, played while the owner's gun loops
    float fireTimer;            // +0x10 frames counted towards the next shot (5 from the constructor)
    float sustain;              // +0x14 frames the voice goes on for after the firing stops (names ours)
    uint32_t unknown18;         // +0x18 not written by the constructor
    float fireInterval;         // +0x1c frames between shots (6 from the constructor)
    float unknown20;            // +0x20 zero from the constructor
    GameStd::String firePatch;  // +0x24 "SFX_HeliMachGun" until SetFirePatch
    GameStd::String shellPatch; // +0x40 "SFX_ShellCasing"
    GameStd::String mixName;    // +0x5c the one-shot sounds' mix
    bool shells;                // +0x78 a shot sometimes drops a shell casing
    uint8_t unknown79[3];

    AGun* Construct(ASceneObj *owner, const char *mixName);                         // 0x00129550
    void Destruct();                                                                // 0x00129470
    // The fire patch, and a new voice of it. The string is passed by value, and destroyed here.
    void SetFirePatch(GameStd::String patch);                                       // 0x001296b0
    // One audio frame of `frames` simulation frames: a shot every fireInterval frames while firing (exactly 1),
    // then the voice played (while the owner's gun loops and the sustain lasts) or silenced.
    void Play(uint8_t firing, ASoundPlayParams *params, float frames);              // 0x00129250
};
static_assert(offsetof(AGun, fireInterval) == 0x1c && offsetof(AGun, firePatch) == 0x24 &&
              offsetof(AGun, shellPatch) == 0x40 && offsetof(AGun, mixName) == 0x5c &&
              offsetof(AGun, shells) == 0x78, "AGun layout");
static_assert(sizeof(AGun) == 0x7c, "AGun is 0x7c bytes");

// ---- ATurret (0x14 bytes, from UMemory::FastAlloc "ATurret"; AVehicle::ActivateTurret's, ASentry's)

class ATurret {
public:
    AMix *mix;                  // +0x00 "Weapons Init"
    AVoice *servoVoice;         // +0x04 (names ours)
    AVoice *sidewaysVoice;      // +0x08
    AVoice *upDownVoice;        // +0x0c
    bool servo;                 // +0x10 the flags as the last Play had them
    bool turningSideways;       // +0x11
    bool turningUpDown;         // +0x12
    uint8_t unknown13;

    ATurret* Construct();                                                           // 0x0012e220
    void Destruct();                                                                // 0x0012e250
    // A motor that starts gets its looping voice, one that stops its stop sound (played once); each voice is
    // played for the listener and freed when it is done.
    void Play(bool turningSideways, bool turningUpDown, bool servo, ASoundPlayParams *params);   // 0x0012e2b0
};
static_assert(offsetof(ATurret, servo) == 0x10 && sizeof(ATurret) == 0x14, "ATurret is 0x14 bytes");

// ---- ASentry (0x160 bytes; vtable 0x001a2f08): a scene object with a turret and a gun

class ASentry : public ASceneObj {
public:
    ATurret turret;             // +0xd0
    AGun gun;                   // +0xe4

    // In "World Sounds", firing "SFX_SALFire" one shot at a time.
    ASentry* Construct();                                                           // 0x0012d7f0
    void Destruct();                                                                // 0x0012d750
    ASentry* Delete(unsigned int flags);                                            // 0x0012d7c0

    // The turret and the gun from the flags (vtable slot 3).
    void Play(ASoundPlayParams *params);                                            // 0x0012d6f0
};
static_assert(offsetof(ASentry, turret) == 0xd0 && offsetof(ASentry, gun) == 0xe4, "ASentry layout");
static_assert(sizeof(ASentry) == 0x160, "ASentry is 0x160 bytes");

// ---- ASmackable (0xf0 bytes; vtable 0x001a2f38): a thing that rolls and is hit. Its patches are ids,
// bank << 7 | patch; -1 for none.

class ASmackable : public ASceneObj {
public:
    AVoice *collisionVoice;     // +0xd0 made by PlayCollision, freed when it is done
    AVoice *rollVoice;          // +0xd4 made by the constructor
    int32_t collisionIds[3];    // +0xd8 [0] for an impact up to 0.75, [1] and [2] in turn for harder ones
    int32_t rollFrames;         // +0xe4 PlayLanding adds the frames since the last audio update (up to 10), Play
                                //       counts them down
    float rollVolume;           // +0xe8 eased towards the speed's share of 10 by Play
    bool detached;              // +0xec Detatch's: the sound deletes itself once its collision is done
    uint8_t unknownED[3];       //       (names ours)

    ASmackable* Construct(int collisionId0, int collisionId1, int collisionId2, int rollId,
                          const char *mixName);                                     // 0x0012d8b0
    void Destruct();                    // Ghidra: FUN_0012dcb0                        0x0012dcb0
    ASmackable* Delete(unsigned int flags);     // Ghidra: FUN_0012dd50                0x0012dd50

    // The virtual methods (slots 2 to 6; entry points Ghidra had not made functions, names from the PS2 build)
    const char* GetName();                                                          // 0x0012dc80
    void Play(ASoundPlayParams *params);                                            // 0x0012da90
    void PlayLanding(float impact, const Coord3 *position, const Coord3 *velocity);            // 0x0012d9a0
    void PlayCollision(float impact, const Coord3 *position, const Coord3 *velocity, int surface);   // 0x0012d9c0
    bool IsStillActive();                                                           // 0x0012da80

    // The owner is done with it: deleted now, or once its collision voice is done.
    void Detatch();                                                                 // 0x0012dc50
};
static_assert(offsetof(ASmackable, collisionIds) == 0xd8 && offsetof(ASmackable, detached) == 0xec,
              "ASmackable layout");
static_assert(sizeof(ASmackable) == 0xf0, "ASmackable is 0xf0 bytes");

// Which of a smackable's two hard collision patches is next (name ours; WeaponSound.cpp)
extern int32_t NextHardCollision;

// ---- ATargeting (0xc bytes; WTargetPicker's and GHud's)

class ATargeting {
public:
    TargetLockState state;      // +0x00
    int32_t unknown04;          // +0x04 zero from the constructor
    AMix *mix;                  // +0x08 "Tracking System"

    ATargeting* Construct();                                                        // 0x0012e0f0
    // "SFX_targetlock" on locking, "SFX_targetunlock" on losing a lock (not while the sound manager is paused,
    // when the state does not change either).
    void SetState(TargetLockState state);                                           // 0x0012e120
};
static_assert(sizeof(ATargeting) == 0xc, "ATargeting is 0xc bytes");

#endif // DRIVING_GAME_WEAPONSOUND_H_
