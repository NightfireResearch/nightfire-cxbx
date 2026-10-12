#ifndef DRIVING_GAME_MISSILE_H_
#define DRIVING_GAME_MISSILE_H_

// ---------------------------------------------------------------------------------------------------------------
// Missile (0x730 bytes, "weapon"/"Missile", physics object type 6; vtable 0x0018eef8): the rockets, missiles,
// torpedoes and the Q-Pulse - a simple body flown by SteerMissile (the user's steering when it is the mission's
// drive missile, homing on its target once the homing delay is over, a wobble about its path), with a smoke trail
// and a transit sound, exploding when it hits a body or the world, runs out of life or is told to. After that it
// lingers hidden while its trail fades, then is deleted. The Simulation keeps the live ones in a vector (the
// vehicles count a kill's hits from it, the world camera draws them). See Missile.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "WeaponEffects.h"              // RMissileStreak
#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../data/CoordConvert.h"       // Coord3
#include "../physics/PhysicsObject.h"

class ASound;
struct WTargetable;

class Missile : public PhysicsObject {
public:
    WTargetable *target;            // +0x6c homed on once the homing delay is over; one reference held
    float maxSpeed;                 // +0x70 the thrust accelerates the body below it and brakes it above
    float thrust;                   // +0x74 never above maxSpeed
    float turnRate;                 // +0x78 the homing turn's share of the way, before the ramp
    float unknown7C;                // +0x7c -1, 0 or 0.8 by weapon; not read here
    float steerScale;               // +0x80 the user steering's scale
    int32_t homingDelay;            // +0x84 steps after launch before it homes
    int32_t armStep;                // +0x88 bodies are not hit before this step
    int32_t lingerSteps;            // +0x8c hidden after the explosion, until deleted
    int32_t lifeSteps;              // +0x90 counted down; it explodes at zero
    int32_t launchStep;             // +0x94
    uint8_t unknown98[8];
    RMissileStreak streak;          // +0xa0
    uint8_t unknown6B0[0x40];
    ASound *sound;                  // +0x6f0 the transit sound
    int32_t weapon;                 // +0x6f4 DrivingWeapon (WeaponManager.h); the constructor flies any weapon
                                    //        but the rockets, missiles and torpedoes as its default rocket
    float steerX;                   // +0x6f8 the drive missile's turn, eased towards the user's
    float steerY;                   // +0x6fc ... and pitch
    float userSteerX;               // +0x700 SetUserSteerX's
    float userSteerY;               // +0x704 SetUserSteerY's
    uint8_t homingFull;             // +0x708 set: it homes at its full turn rate (else half), and at full speed
                                    //        while it has a target (else three tenths)
    uint8_t detonate;               // +0x709 set: it explodes at the next collision check
    uint8_t exploded;               // +0x70a set: its hit does not count, and it lingers
    uint8_t unknown70B;
    float fadeIn;                   // +0x70c the transit sound's volume, rising to 1
    float damage;                   // +0x710 its explosion's
    int32_t lockSoundStep;          // +0x714 the target lock sound's next step
    float wobbleX;                  // +0x718 the player's Missile's corkscrew, decaying
    float wobbleY;                  // +0x71c
    float wobblePhase;              // +0x720 turns, 0..1
    uint8_t unknown724[0xc];

    // Missile::Missile: a missile of the weapon fired along `direction` from `position` by the owner with the
    // signature, at `speed` plus the weapon's launch speed (some ignore it), with the damage and target (some find
    // their own, some drop it); `variant` 7 or 8 makes the player's Missile wobble wider.
    Missile* Construct(int weapon, float speed, const Coord3 *direction, const Coord3 *position, uint32_t ownerSig,
                       float damage, WTargetable *target, int variant);                         // 0x0005e8c0
    void Destruct();                                                                            // 0x0005f350
    Missile* Delete(unsigned flags);        // the scalar deleting destructor, vtable slot 0     // 0x0005f6a0

    // The trail and the light it gives (the Q-Pulse: a light only, once its homing delay is over); `firstView`:
    // the first view's draw, which adds the light and lays the trail
    void Render(bool firstView);                                                                // 0x0005f3d0
    // The user's turn and pitch for the drive missile: the stick scaled, or zero inside the dead zone
    void SetUserSteerX(float x);                                                                // 0x0005f600
    void SetUserSteerY(float y);                                                                // 0x0005f640
    void SetTarget(WTargetable *target);    // the old target keeps its reference                 // 0x0005f680
    // Vtable slot 4: steering and the collision check while it flies; then the linger and the deletion
    void Simulate();                                                                            // 0x00060a30
    void CheckForCollision();                                                                   // 0x0005f6d0
    void SteerMissile();                                                                        // 0x00060110
};
static_assert(sizeof(Missile) == 0x730, "a missile is 0x730 bytes");
static_assert(offsetof(Missile, target) == 0x6c && offsetof(Missile, launchStep) == 0x94 &&
              offsetof(Missile, streak) == 0xa0 && offsetof(Missile, sound) == 0x6f0 &&
              offsetof(Missile, homingFull) == 0x708 && offsetof(Missile, exploded) == 0x70a &&
              offsetof(Missile, damage) == 0x710 && offsetof(Missile, wobblePhase) == 0x720, "Missile layout");

// The missiles' tuning (Missile.cpp; the names are ours): the user steering's scale and rate, the CH-6 remote
// rocket's speed, thrust, launch speed, turn (times 0.05) and life in seconds, the player's level flight's
// clearance over the ground and the height below which it follows it; and the transit sound's pitch and the
// player's Missiles' wobble direction, each changed by every missile made
extern float MissileSteerScale;
extern float MissileSteerRate;
extern float RemoteRocketSpeed;
extern float RemoteRocketThrust;
extern float RemoteRocketLaunchSpeed;
extern float RemoteRocketTurn;
extern int32_t RemoteRocketLife;
extern float MissileGroundClearance;
extern float MissileGroundFollowHeight;
extern float MissileSoundPitch;
extern bool MissileWobbleFlip;

#endif // DRIVING_GAME_MISSILE_H_
