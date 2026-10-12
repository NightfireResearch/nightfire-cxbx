#ifndef DRIVING_GAME_WEAPONMANAGER_H_
#define DRIVING_GAME_WEAPONMANAGER_H_

// ---------------------------------------------------------------------------------------------------------------
// SWeaponManager (0x17c bytes, one, WeaponManager): the player's weapons. It keeps a working copy of the weapon
// table (one WeaponRecord per weapon: ammunition, the rounds loaded, the refire time, the firing and reloading
// animation systems), which weapon each fire button fires (primary, secondary, special), reads the fire, select
// and reload actions from its own action queue, fires the weapons (each shot is a stimulus to one of the weapon's
// animation systems on the player car, whose events make the weapon objects), reloads them, deploys and stows
// their animation systems, turns the car's turret towards the target, and runs the nuclear blast sequence. In
// auto-drive the hands (AICharacterHands) fire weapons 23 and up instead. See WeaponManager.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord3

class ActionQueue;
class RSceneObj;
struct WTargetable;

// The weapons, by their records' names in the weapon table
enum DrivingWeapon : int32_t {
    kWeaponGun = 0,
    kWeaponRocket = 1,
    kWeaponMissile = 2,
    kWeaponQBooster = 3,
    kWeaponTimeMine = 4,
    kWeaponProxMine = 5,
    kWeaponRemMine = 6,
    kWeaponFloatingMine = 7,
    kWeaponDetonator = 8,
    kWeaponSpikes = 9,
    kWeaponQSlick = 10,
    kWeaponQSmoke = 11,
    kWeaponQWorm = 12,
    kWeaponLaser = 13,
    kWeaponQPulse = 14,
    kWeaponRadar = 15,
    kWeaponCannon = 16,
    kWeaponQVision = 17,
    kWeapon2Wheels = 18,
    kWeaponGrenade = 19,
    kWeaponQCharge = 20,
    kWeaponTorpedo = 21,
    kWeaponRemoteTorpedo = 22,
    kWeaponP2K = 23,
    kWeaponSnowmobileGun = 24,
    kWeaponSnowmobileRocket = 25,
    kWeaponSniperRifle = 26,
    kWeaponUltralightGun = 27,
    kWeaponUltralightRocket = 28,
    kWeaponFodGun = 29,
    kWeaponFodRocket = 30,
    kWeaponCh6RemoteRocket = 31,

    kWeaponCount = 32,
    kFirstHandsWeapon = kWeaponP2K,     // auto-drive selects only from here on, the car only below
};

// WeaponRecord::flags (names ours, from what the manager does with them)
enum WeaponFlags : uint32_t {
    kWeaponLoaded = 0x01,               // LoadWeapon sets it, UnloadWeapon clears it: selectable with ammunition
    kWeaponAnimating = 0x02,            // its next firing system is playing: it cannot fire
    kWeaponReloading = 0x04,            // set by a shot; Update reloads until the systems come round again
    kWeaponDeploying = 0x20,            // Update plays its "Init" sound, then sets kWeaponDeployed
    kWeaponDeployed = 0x40,             // cleared while it is not selected
};

// One weapon's state (0x54 bytes): the weapon table (StaticInit.h's WeaponDefinitions, 0x001c94d8) holds one per
// weapon, and Reset copies it into the manager's array.
struct WeaponRecord {
    int32_t id;                 // +0x00 its index
    const char *name;           // +0x04 "Gun", "Rocket", ...
    int32_t textIds[3];         // +0x08
    uint32_t flags;             // +0x14 WeaponFlags
    int32_t ammo;               // +0x18
    int32_t loaded;             // +0x1c rounds ready: each shot takes one, each reload step adds one
    int32_t clipSize;           // +0x20
    int32_t fireSteps[4];       // +0x24 the simulation step each of its firing systems last fired
    int32_t damage;             // +0x34 LoadWeapon's last argument, when positive; the mines' and the grenade's
                                //       explosion size (name ours)
    uint8_t countsShots;        // +0x38 its shots go into the mission's shots fired
    int32_t refireSteps;        // +0x3c steps between shots (Reset: seconds times the steps a second)
    int32_t soundBank;          // +0x40 the bank of its "SFX_<sound>Reload" and "SFX_<sound>Init"; -1: none
    const char *sound;          // +0x44
    int32_t fireSystem;         // +0x48 the next of its systems (WeaponSystems' row) to fire
    int32_t reloadSystem;       // +0x4c the next to reload
    int32_t shotsFired;         // +0x50
};
static_assert(sizeof(WeaponRecord) == 0x54, "a weapon record is 0x54 bytes");
static_assert(offsetof(WeaponRecord, flags) == 0x14 && offsetof(WeaponRecord, fireSteps) == 0x24 &&
              offsetof(WeaponRecord, countsShots) == 0x38 && offsetof(WeaponRecord, refireSteps) == 0x3c &&
              offsetof(WeaponRecord, sound) == 0x44 && offsetof(WeaponRecord, shotsFired) == 0x50,
              "weapon record layout");

class SWeaponManager {
public:
    // nuclearState
    enum NuclearState : int32_t {
        kNuclearNone = 0,
        kNuclearCountdown = 1,          // StartNuclearSequence: the screen brightens until the blast
        kNuclearBlast = 2,              // the blast has gone off: the screen stays lit
    };

    int32_t primary;            // +0x00 what the primary fire fires; -1: nothing
    int32_t secondary;          // +0x04 the selected weapon, what the secondary fire fires
    int32_t special;            // +0x08 the gadget (SetSpecialWeapon); -1: none
    int32_t otherSecondary;     // +0x0c the other mode's selection, swapped with `secondary` when auto-drive
                                //       starts or ends
    WeaponRecord *weapons;      // +0x10 [kWeaponCount], from operator new[]
    RSceneObj *playerCar;       // +0x14 the player car's render object: its animation systems are the weapons'
    int32_t lastFired;          // +0x18 the weapon FireWeapon last fired; -1 after Reset
    WTargetable *lastTarget;    // +0x1c the selected target when it fired
    ActionQueue *actionQueue;   // +0x20 "WeaponManQ"
    bool firingPrimary;         // +0x24 the fire buttons held
    bool firingSecondary;       // +0x25
    bool firingSpecial;         // +0x26
    bool handsFired;            // +0x27 the hands fired the last shot; cleared with the fire buttons and by a
                                //       shot without ammunition
    bool reloading;             // +0x28
    uint8_t unknown29;          // +0x29 zeroed by Reset
    uint8_t unknown2A[6];
    uint8_t unknown30;          // +0x30 zeroed by Reset
    uint8_t unknown31[3];
    int32_t boostBlur;          // +0x34 120 when the Q-Booster fires, decaying each update: the motion blur
    int32_t unknown38;          // +0x38 zeroed by Reset
    bool doubleShot;            // +0x3c the launch flags' bit 3: the missile and the torpedo fire twice
    uint8_t unknown3D[3];
    int32_t switchStep;         // +0x40 until this step a new selection does not fire (a third of a second)
    int32_t nuclearState;       // +0x44 NuclearState
    uint8_t unknown48[8];
    Coord3 nuclearPosition;     // +0x50
    int32_t nuclearStep;        // +0x5c the step the blast goes off, a second after StartNuclearSequence
    float nuclearScale;         // +0x60 the explosion's size, over 10000
    int32_t clickCount;         // +0x64 updates since the last distance click
    float turretAngle;          // +0x68 the car's turret, in turns
    bool turretOnTarget;        // +0x6c the turret has turned all the way to the target's angle
    uint8_t unknown6D[3];
    const Coord3 *specialTrigger;   // +0x70 the gadget fires only within specialRange of it; NULL: anywhere
    float specialRange;         // +0x74
    int32_t sortRank[kWeaponCount];     // +0x78 each weapon's place in the selection order; -1: none
    int32_t sorted[kWeaponCount];       // +0xf8 the selectable weapons in that order (SortWeaponList)
    int32_t sortedCount;        // +0x178

    SWeaponManager* Construct();                                                                // 0x000bb220
    void Destruct();                                                                            // 0x000bad40
    // Back to the weapon table: only the gun loaded and selected, nothing firing
    void Reset();                                                                               // 0x000bad70
    void SetPlayerCar(RSceneObj *car);                                                          // 0x000ba990

    void LoadWeapon(int weapon, int ammo, int clipSize, int damage);                             // 0x000ba9f0
    void UnloadWeapon(int weapon);                                                              // 0x000baa70
    void SetSecondary(int weapon);                                                              // 0x000baab0
    // The gadget, fired within `range` of `trigger` (anywhere without one); the missile is loaded instead
    void SetSpecialWeapon(int weapon, const Coord3 *trigger, float range);                      // 0x000bae50
    void SelectSecondary(bool next);                                                            // 0x000baee0
    // The selectable weapons of the current mode (auto-drive's or the car's), in the selection order
    void SortWeaponList();                                                                      // 0x000ba7f0
    int GetPreviousSecondaryType();                                                             // 0x000baad0
    int GetNextSecondaryType();                                                                 // 0x000bab20

    // A stimulus to the weapon's `index`th animation system
    void DoWeaponAnim(int weapon, int stimulus, int index);                                     // 0x000bab80
    // The weapon one of whose systems is `system`; -1 for none (`this` is not read)
    int GetWeaponFromSystem(int system);                                                        // 0x000babb0
    void FireWeapon(int weapon);                                                                // 0x000bb2b0
    // The reload action: the hands reload the selection
    void UserReloading();                                                                       // 0x000babf0

    void StartNuclearSequence(const Coord3 *position, float scale);                             // 0x000bad00
    void HandleNuclearSequence();                                                               // 0x000bb9b0
    void UpdateTurret();                                                                        // 0x000baf60
    void Update();                                                                              // 0x000bbdc0

private:
    // The selection's place in `sorted`; -1 if it is not there (inlined in the game; PS2: FindSortedSecondary)
    int FindSortedSecondary();
    // With a reload under way in auto-drive, the selection loaded at once
    void FinishReload();
};
static_assert(sizeof(SWeaponManager) == 0x17c, "SWeaponManager is 0x17c bytes");
static_assert(offsetof(SWeaponManager, actionQueue) == 0x20 && offsetof(SWeaponManager, reloading) == 0x28 &&
              offsetof(SWeaponManager, boostBlur) == 0x34 && offsetof(SWeaponManager, switchStep) == 0x40 &&
              offsetof(SWeaponManager, nuclearPosition) == 0x50 && offsetof(SWeaponManager, turretAngle) == 0x68 &&
              offsetof(SWeaponManager, specialTrigger) == 0x70 && offsetof(SWeaponManager, sortRank) == 0x78 &&
              offsetof(SWeaponManager, sorted) == 0xf8 && offsetof(SWeaponManager, sortedCount) == 0x178,
              "SWeaponManager layout");

// The one manager (GameLoop_StartUp makes it)
#define WeaponManager (*(SWeaponManager **)0x0023923c)

#endif // DRIVING_GAME_WEAPONMANAGER_H_
