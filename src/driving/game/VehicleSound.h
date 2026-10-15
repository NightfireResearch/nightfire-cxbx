#ifndef DRIVING_GAME_VEHICLESOUND_H_
#define DRIVING_GAME_VEHICLESOUND_H_

// ---------------------------------------------------------------------------------------------------------------
// The engine sound every vehicle carries (game.audio): AVehicle, the scene object sound (ASceneObj, WeaponSound.h)
// of a vehicle - tyres on each surface, skids, scrapes, bumps, landings and collisions, with the horn (AHorn), the
// wind (AVehicleWind), and the engine models the player's vehicles share by name (AEngine, ARaceEngine, through
// URefCounter<AEngine>). The kinds derive from AVehicle: ATrafficVehicle and AHelicopter, which the vehicles' code
// reads, are below; the others are in VehicleSoundKinds.h, AFingoDeath in FingoDeathSound.h.
//
// Every sound's vtable stays the game's; the virtual methods are called through it (the Call* helpers). AVehicle's
// slots, from its vtable at 0x001a2680 (ASceneObj's at 0x001a2f80 has the same nine; WeaponSound.h):
//   0 Delete, 1 IsTransient (false), 2 GetName (ABaseSound's), 3 Play, 4 PlayLanding, 5 PlayCollision,
//   6 IsStillActive (false in every kind), 7 IsTracked (true for ASnowMobile and APlayerTank; ATrafficVehicle:
//   its type is kVehicleSoundSnowMobile), 8 a query every kind answers false but AUltraLite.
// See VehicleSound.cpp (AVehicle, AHorn) and VehicleSoundEngine.cpp (the wind, the engines, the play parameters).
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../audio/Sound.h"             // ABaseSound, ASoundPlayParams
#include "../audio/Voice.h"             // AVoice
#include "../audio/Mix.h"               // AMix
#include "../audio/ZoomObj.h"           // AZoomObj
#include "../data/CoordConvert.h"       // Coord3
#include "../engine/URefCounter.h"      // RefCounterTree, RefCounterValue
#include "WeaponSound.h"                // ASceneObj, AGun, ATurret

class ABank;
class AVehicle;

// ---- AVehicle

// AVehicle::vehicleType: what each kind's constructor writes (named after the kind; ATrafficVehicle writes
// kVehicleSoundSnowMobile for some engines, PBondCar::InitAudioObject kVehicleSoundPlayerCar for the car type
// "jungle_truck")
enum VehicleSoundType {
    kVehicleSoundNone = 0,
    kVehicleSoundTank = 1,          // APlayerTank
    kVehicleSoundHeli = 2,          // APlayerHeli
    kVehicleSoundSubmersible = 4,   // ASubmersible
    kVehicleSoundSnowMobile = 5,    // ASnowMobile
    kVehicleSoundUltraLite = 6,     // AUltraLite
    kVehicleSoundPlayerCar = 7,     // APlayerVehicle
};

// AVehicle::Construct's second argument, as the kinds pass it: kVehicleSkids gives the skid voices of bank slot 0
// (SFX_SkidFront1/Back1), anything else the squeals of the vehicle bank (SFX_SquealFront1/Back1) - and the
// smackables' collision sounds (`squeals`).
enum VehicleSkidVoices {
    kVehicleSqueals = 0xd,          // every kind but APlayerVehicle, ASubmersible and ATrafficVehicle
    kVehicleSkids = 0xe,
};

// AVehicle::voices, by the patch each plays
enum VehicleVoice {
    kVoiceOffRoad,                  // SFX_OffRN       (rumbleLevels[kRumbleOffRoad])
    kVoiceGravel,                   // SFX_GravelRN    (rumbleLevels[kRumbleGravel])
    kVoiceCobble,                   // SFX_CobbleRN    (rumbleLevels[kRumbleCobble])
    kVoiceSkidFront,                // SFX_SkidFront1 or SFX_SquealFront1
    kVoiceSkidBack,                 // SFX_SkidBack1 or SFX_SquealBack1
    kVoiceGrassSkidFront,           // SFX_GrassSkid
    kVoiceGrassSkidBack,            // SFX_GrassSkid
    kVoiceOffRoadSkidFront,         // SFX_OffRoadSkid
    kVoiceOffRoadSkidBack,          // SFX_OffRoadSkid
    kVoiceIceSkidFront,             // SFX_iceskidfront
    kVoiceIceSkidBack,              // SFX_iceskidback
    kVoiceScrapeMetal,              // SFX_ScrapeMetal (scrapeLevels[kScrapeMetal])
    kVoiceScrapeRock,               // SFX_ScrapeRock  (scrapeLevels[kScrapeRock])
    kVoiceScrapeSand,               // SFX_scrapesand  (scrapeLevels[kScrapeSand])
    kVehicleVoiceCount
};

enum VehicleRumble { kRumbleOffRoad, kRumbleGravel, kRumbleCobble, kVehicleRumbleCount };
enum VehicleScrape { kScrapeRock, kScrapeMetal, kScrapeSand, kVehicleScrapeCount };
// The bumps played as the vehicle covers ground: SFX_Cobble1/2 on wood, SFX_Railbump1/2 on rails, alternately
enum VehicleBump { kBumpWood, kBumpRail, kVehicleBumpCount };

// An object of two voices at AVehicle +0x1d0 (the name is ours): nothing in the game makes one, AVehicle's
// destructor deletes it.
struct AVehicleVoicePair {
    uint8_t unknown00[0xc];
    AVoice voices[2];               // +0x0c

    // The voices' views, the second voice's first (Ghidra: FUN_0011ec10)
    void Destruct();                                                            // 0x0011ec10
};
XBE_FIELD(AVehicleVoicePair, voices, 0xc);

class AHorn;
class AVehicleWind;

// A vehicle's sound (0x1f0 bytes; vtable 0x001a2680). Every kind derives from it. Field names are Ghidra's, the
// vehicles' (PBondCar::Simulate writes most of them each step) or ours.
class AVehicle : public ASceneObj {
public:
    int32_t vehicleType;            // +0xd0 VehicleSoundType
    int32_t surfaces[4];            // +0xd4 CarSurface under each wheel (BondCarState.h), 0 off the ground
    int32_t previousSurfaces[4];    // +0xe4 the step before's
    float suspensionCompression[4]; // +0xf4
    float frontSlipTarget;          // +0x104 PBondCar's slip, at most 1; frontSlip moves to it
    float rearSlipTarget;           // +0x108
    float frontSlip;                // +0x10c the front skids' level
    float rearSlip;                 // +0x110
    float skidMultiple;             // +0x114 one, or SFX_SKIDMULTIPLE when at least 1
    float throttle;                 // +0x118
    float rpm;                      // +0x11c
    float rpmWobble;                // +0x120 swings between -150 and 150, added to the rpm (name ours)
    float rpmWobbleDirection;       // +0x124 one or minus one (name ours)
    float rumbleLevels[kVehicleRumbleCount];    // +0x128 VehicleRumble
    float bumpDistances[kVehicleBumpCount];     // +0x134 covered since the last bump (name ours)
    float speed;                    // +0x13c the sound's own: its velocity's length, smoothed with a horn
    float carSpeed;                 // +0x140 the vehicle's
    float scrapeLevels[kVehicleScrapeCount];    // +0x144 VehicleScrape
    float landingForce;             // +0x150 PBondCar's: five times the hardest wheel compression
    bool hornHonk;                  // +0x154 the horn honks at the next Play, which clears it
    bool bumpToggles[kVehicleBumpCount];        // +0x155 which of each bump's two patches plays next
    uint8_t boost;                  // +0x157 set by PBondCar::EnableRocketBoost, cleared when the boost runs out
    uint8_t engineOn;               // +0x158
    bool squeals;                   // +0x159 not kVehicleSkids (Construct)
    uint8_t unknown15A[2];
    int32_t bumpPatches[kVehicleBumpCount][2];  // +0x15c in `bank`: SFX_Cobble1/2, SFX_Railbump1/2
    int32_t scrapeTimers[kVehicleScrapeCount];  // +0x16c audio frames left; PlayCollision adds 3, up to 10
    int16_t terrain;                // +0x178 zero (Construct, Reset)
    uint8_t gasButton;              // +0x17a PBondCar::Simulate's: the throttle and brake held
    uint8_t brakeButton;            // +0x17b
    uint8_t xyTrigger;              // +0x17c PBondCar::GetControllerInput's
    uint8_t xyReleaseTrigger;       // +0x17d
    uint8_t unknown17E[2];
    float steering;                 // +0x180
    float steeringVertical;         // +0x184
    AGun *gun;                      // +0x188 ActivateGun's
    ATurret *turret;                // +0x18c ActivateTurret's
    AZoomObj *zoom;                 // +0x190 ActivateZoom's
    ABank *bank;                    // +0x194 bank slot 1's at construction
    AVoice *voices[kVehicleVoiceCount];         // +0x198 VehicleVoice
    AVehicleVoicePair *unknown1d0;  // +0x1d0 NULL from the constructor
    AVoice *carAlarm;               // +0x1d4 NULL from the constructor; Play plays it at the listener's volume
    AVehicleWind *wind;             // +0x1d8 ActivateWind's
    AHorn *horn;                    // +0x1dc ChooseHorn's
    AMix *asphaltMix;               // +0x1e0 "Asphalt"
    AMix *offRoadMix;               // +0x1e4 "OffRoad"
    AMix *skidsMix;                 // +0x1e8 "Skids"
    AMix *scrapesMix;               // +0x1ec "Scrapes"

    // The sound of a vehicle in the mix `mixName` with the skids (or squeals) `skidVoices` gives.
    AVehicle* Construct(const char *mixName, int skidVoices);                   // 0x0011e2e0
    void Destruct();                                                            // 0x00120a10
    AVehicle* Delete(unsigned int flags);                                       // 0x00120d50
    // The engine inputs, levels and bump state back to rest (AIGroundVehicle::UnSpawn, Construct).
    void Reset();                                                               // 0x0011e230

    // Each makes its object the first time only.
    void ActivateGun(const char *mixName);                                      // 0x0011e9a0
    void ActivateTurret();                                                      // 0x0011ea10
    void ActivateZoom();                                                        // 0x0011ea80
    void ActivateWind();                                                        // 0x0011eaf0
    // A horn of the patch "SFX_<name>" in place of any before.
    void ChooseHorn(const char *name, float volume, float pitch);               // 0x00120960

    // speed from the velocity: with a horn, limited to 5..14 and moved by at most 0.1 per frame since the last
    // audio update.
    void CalculateSpeed();                                                      // 0x0011eb50
    // The wheels that were off the ground the step before.
    int GetAirborneWheels();                                                    // 0x0011ebf0
    // The engine's rpm as heard: a tracked vehicle's from its speed (log), one with a horn's from a gear of
    // 15 m/s, any other the rpm plus its wobble. Unrounded, as the x87 leaves it.
    double CalculateRpm();                                                      // 0x0011ec80

    // The virtual methods (slots 3-5)
    // The tyres, skids, rumbles, bumps and scrapes for the listener, then the wind, horn, gun, turret, zoom and
    // car alarm.
    void Play(ASoundPlayParams *params);                                        // 0x0011f9b0
    // A one-shot sound for the wheels touching down at `position`, chosen by the surfaces and how many were off
    // the ground (`volume` and `velocity` are not read).
    void PlayLanding(float volume, const Coord3 *position, const Coord3 *velocity);    // 0x0011ed30
    // A one-shot impact sound by strength on `surface` (CarSurface), and the scrape of that surface kept going.
    void PlayCollision(float strength, const Coord3 *position, const Coord3 *velocity, int surface);   // 0x0011f1a0

    // ---- calls through the vtable
    void CallPlay(ASoundPlayParams *params) {
        (this->*XbeVirtual<PlayMethod>(this, 3))(params);
    }
    void CallPlayLanding(float volume, const Coord3 *position, const Coord3 *velocity) {
        (this->*XbeVirtual<PlayLandingMethod>(this, 4))(volume, position, velocity);
    }
    void CallPlayCollision(float strength, const Coord3 *position, const Coord3 *velocity, int surface) {
        (this->*XbeVirtual<PlayCollisionMethod>(this, 5))(strength, position, velocity, surface);
    }

private:
    typedef void (AVehicle::*PlayMethod)(ASoundPlayParams *params);
    typedef void (AVehicle::*PlayLandingMethod)(float volume, const Coord3 *position, const Coord3 *velocity);
    typedef void (AVehicle::*PlayCollisionMethod)(float strength, const Coord3 *position, const Coord3 *velocity,
                                                  int surface);

public:
    // The sound's frame: its forward, right and up vectors (Ghidra: FUN_00061a60; the name is ours; BondCar code)
    void SetOrientation(const Coord3 *forward, const Coord3 *right, const Coord3 *up);         // 0x00061a60
};
XBE_FIELD(AVehicle, vehicleType, 0xd0);
XBE_FIELD(AVehicle, suspensionCompression, 0xf4);
XBE_FIELD(AVehicle, frontSlip, 0x10c);
XBE_FIELD(AVehicle, skidMultiple, 0x114);
XBE_FIELD(AVehicle, rpmWobble, 0x120);
XBE_FIELD(AVehicle, rumbleLevels, 0x128);
XBE_FIELD(AVehicle, speed, 0x13c);
XBE_FIELD(AVehicle, carSpeed, 0x140);
XBE_FIELD(AVehicle, landingForce, 0x150);
XBE_FIELD(AVehicle, boost, 0x157);
XBE_FIELD(AVehicle, engineOn, 0x158);
XBE_FIELD(AVehicle, squeals, 0x159);
XBE_FIELD(AVehicle, bumpPatches, 0x15c);
XBE_FIELD(AVehicle, scrapeTimers, 0x16c);
XBE_FIELD(AVehicle, terrain, 0x178);
XBE_FIELD(AVehicle, xyTrigger, 0x17c);
XBE_FIELD(AVehicle, steering, 0x180);
XBE_FIELD(AVehicle, gun, 0x188);
XBE_FIELD(AVehicle, bank, 0x194);
XBE_FIELD(AVehicle, voices, 0x198);
XBE_FIELD(AVehicle, unknown1d0, 0x1d0);
XBE_FIELD(AVehicle, horn, 0x1dc);
XBE_FIELD(AVehicle, scrapesMix, 0x1ec);
LAYOUT_32(sizeof(AVehicle) == 0x1f0, "AVehicle is 0x1f0 bytes");

// PlayCollision's count of metal gate impacts: SFX_MetalGate2 and SFX_MetalGate3 by turns (name ours)
extern uint32_t MetalGateCount;
extern int32_t CollisionTicks[12];

// ---- AHorn (0x70 bytes, from operator new): a horn that honks in bursts while asked to

class AHorn {
public:
    float volume;                   // +0x00
    float pitch;                    // +0x04
    AMix *mix;                      // +0x08 "Horns" (made if new)
    AVoice voice;                   // +0x0c bank slot 0's patch
    uint32_t honks;                 // +0x64 honks left in the burst (names ours)
    uint32_t honkTime;              // +0x68 audio frames the honk sounds on
    uint32_t pause;                 // +0x6c audio frames before the next honk

    AHorn* Construct(const char *patch, float volume, float pitch);             // 0x0011e0f0
    // Counts the honk and its pauses down (or starts a burst while `honk` is set) and plays the voice, silent
    // between honks.
    void Play(bool honk, ASoundPlayParams *params);                             // 0x0011e150
};
XBE_FIELD(AHorn, voice, 0xc);
XBE_FIELD(AHorn, honks, 0x64);
LAYOUT_32(sizeof(AHorn) == 0x70, "AHorn is 0x70 bytes");

// ---- AVehicleWind (0x24 bytes, from operator new): bank slot 4's SFX_windy, its level and pitch drifting at
// random, louder with speed

class AVehicleWind {
public:
    AVehicle *vehicle;              // +0x00
    AMix *mix;                      // +0x04 "Player Wind"
    AVoice *voice;                  // +0x08
    float level;                    // +0x0c drifts between 0.4 and 1 (names ours)
    float levelRate;                // +0x10 per frame since the last audio update
    float pitch;                    // +0x14 drifts between 1 and 2.8
    float pitchRate;                // +0x18
    float volume;                   // +0x1c one
    float azimuth;                  // +0x20 0.5

    AVehicleWind* Construct(AVehicle *vehicle);                                 // 0x0012e830
    void Destruct();                                                            // 0x0012ea60
    // `frames`: the simulation frames since the last audio update, as a float.
    void Play(ASoundPlayParams *params, float frames);                          // 0x0012e8f0
    // Silent.
    void Stop(ASoundPlayParams *params);                                        // 0x0012ea30
};
LAYOUT_32(sizeof(AVehicleWind) == 0x24, "AVehicleWind is 0x24 bytes");

// ---- the engine models: AEngine (0x24 bytes; vtable 0x001a3058) and its one kind, ARaceEngine (0x54 bytes,
// from operator new; vtable 0x001a310c), shared by name through URefCounter<AEngine> (AEngine::Get).
// Slot 0: Play (the vehicle's state into pitch and volumes), slot 1: the scalar deleting destructor.

class AEngine {
public:
    void **vtable;                  // +0x00
    float pitch;                    // +0x04 (names ours)
    float loadVolume;               // +0x08 the square root of the throttle
    float idleVolume;               // +0x0c the square root of one less the throttle
    float revTime;                  // +0x10 ARaceEngine's frames with the throttle at 0.2 or more
    float engineRpm;                // +0x14 follows the vehicle's rpm (plus wobble, less the rear slip)
    float rpmScale;                 // +0x18 one over Get's `rate`
    float engineThrottle;           // +0x1c follows the vehicle's throttle
    AVehicle *vehicle;              // +0x20

    AEngine* Construct();                                                       // 0x0012edd0
    // The base's destructor: its vtable back (Ghidra: FUN_0012ef20).
    void Destruct();                                                            // 0x0012ef20
    AEngine* Delete(unsigned int flags);                                        // 0x0012ef30
    void Play(ASoundPlayParams *params);                                        // 0x0012ede0

    // The engine `name` (an ARaceEngine made and registered the first time), set up for `vehicle`.
    static AEngine* Get(const char *name, AVehicle *vehicle, float rate);       // 0x0012f940
    // Every engine unregistered, each deleted once its last name goes.
    static void RemoveAll();                                                    // 0x0012f9e0

    void CallPlay(ASoundPlayParams *params) {
        (this->*XbeVirtual<PlayMethod>(this, 0))(params);
    }
    AEngine* CallDelete(unsigned int flags) {
        return (this->*XbeVirtual<DeleteMethod>(this, 1))(flags);
    }

private:
    typedef void (AEngine::*PlayMethod)(ASoundPlayParams *params);
    typedef AEngine* (AEngine::*DeleteMethod)(unsigned int flags);
};
LAYOUT_32(sizeof(AEngine) == 0x24, "AEngine is 0x24 bytes");

// ARaceEngine::voices, each an "IN" and an "OUT" patch of bank slot 9
enum RaceEngineVoice { kEngineIdle, kEngineLowLoad, kEngineMidLoad, kEngineHighLoad, kEngineCruise, kRaceEngineVoices };

class ARaceEngine : public AEngine {
public:
    AVoice *voices[kRaceEngineVoices][2];       // +0x24 SFX_IN-<voice> and SFX_OUT-<voice>
    float idleBlend;                // +0x4c below 2000 rpm, the square root of one less the rpm over 3000
    float loadBlend;                // +0x50 the square root of the rpm over 3000 (names ours)

    ARaceEngine* Construct();                                                   // 0x0012fc60
    // (Ghidra: FUN_00130590)
    void Destruct();                                                            // 0x00130590
    ARaceEngine* Delete(unsigned int flags);                                    // 0x001306b0
    void Play(ASoundPlayParams *params);                                        // 0x0012ffb0
};
XBE_FIELD(ARaceEngine, voices, 0x24);
LAYOUT_32(sizeof(ARaceEngine) == 0x54, "ARaceEngine is 0x54 bytes");

// URefCounter<AEngine>'s tree (EngineRefCounter's map, at 0x00243b74): its compiled copies of RefCounterTree's code
// (engine/URefCounter.h). Ghidra leaves them unnamed (0x0012ef80..0x0012f820).
class EngineRefTree : public RefCounterTree {
public:
    void EraseSubtree(RefCounterNode *node);                                    // 0x0012ef80
    RefCounterNode** EraseAt(RefCounterNode **result, RefCounterNode *where);   // 0x0012efd0
    RefCounterNode** InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where,
                              const RefCounterValue *value);                    // 0x0012f340
    RefCounterNode** EraseRange(RefCounterNode **result, RefCounterNode *first,
                                RefCounterNode *last);                          // 0x0012f530
    RefCounterInsertResult* InsertUnique(RefCounterInsertResult *result,
                                         const RefCounterValue *value);         // 0x0012f620
    void DestroyRange();                                                        // 0x0012f7e0
    void Destruct();                                                            // 0x0012f820
};
LAYOUT_32(sizeof(EngineRefTree) == 0xc, "a map is 12 bytes");

// The value every URefCounter's insert copies (the pair's constructor: the 128-byte key and the entry). The
// linker kept one copy, among the engines' (Ghidra: FUN_0012ef50); URefCounter.cpp calls it.
struct RefCounterPair : RefCounterValue {
    RefCounterPair* Construct(const char *key, const RefCounterEntry *entry);   // 0x0012ef50
};

// ---- the kinds the vehicles' code reads (their methods are in VehicleSoundKinds.cpp)

// The AI's cars' engine sound (0x240 bytes; vtable 0x001a2ed8): a load engine and exhaust voice by the engine's
// name, road noise, a siren, and the snowmobiles' landings
class ATrafficVehicle : public AVehicle {
public:
    char engineName[0x10];          // +0x1f0 the constructor's engine (GetName's answer)
    AVoice *engineVoice;            // +0x200 bank slot 0's SFX_<engine>LdEn (names ours)
    AVoice *exhaustVoice;           // +0x204 SFX_<engine>LdEx
    AVoice *roadVoice;              // +0x208 SFX_TrafficRN, or SFX_tanktreads in "Tank Treads" for the tank
    ABasic *siren;                  // +0x20c AddSiren's
    float engineRpm;                // +0x210 follows CalculateRpm's by at most 200 a frame
    float engineGain;               // +0x214 follows one (or 1/8 while the rpm falls fast) by 16/127 a frame
    float pitchOffset;              // +0x218 a random number below 0.2 from PBondCar::InitAudioObject, added to
                                    //        the engine's pitch (name ours)
    bool countedA;                  // +0x21c (not set by the constructor) the destructor counts TrafficCountA
    bool countedB;                  // +0x21d down when set, else TrafficCountB if countedA is
    bool sirenOn;                   // +0x21e the siren plays while set, stops otherwise
    uint8_t unknown21f;
    int16_t airborneWheels;         // +0x220 the snowmobiles': as ASnowMobile's
    bool jumped;                    // +0x222
    bool rearLifted;                // +0x223
    float restCompression[4];       // +0x224 the snowmobiles' only
    float rearCompression;          // +0x234
    float frontCompression;         // +0x238
    uint32_t unknown23c;

    // `engine` names the patches, `mixName` the mix.
    ATrafficVehicle* Construct(const char *engine, const char *mixName);        // 0x0012cb90
    void Destruct();                                                            // 0x0012d300
    ATrafficVehicle* Delete(unsigned int flags);                                // 0x0012d400
    // The engine voices by the eased rpm and gain, the road voice by speed, the snowmobiles' landings, the siren.
    void Play(ASoundPlayParams *params);                                        // 0x0012d430
    // Vtable slot 7: the type is kVehicleSoundSnowMobile.
    bool IsTracked();                                                           // 0x0012ce10
    // Vtable slot 2: the engine's name - APlayerVehicle's and ASubmersible's slot 2 too (one copy of the code).
    char* GetName();                                                            // 0x0012ce20
    // An ABasic of the patch in "Opponents", unless there is a siren (Ghidra: FUN_0012ce30; the name is ours).
    void AddSiren(const char *patch);                                           // 0x0012ce30
    // Landing thumps, a freewheel when the rear lifts under throttle and a rev for the first wheel leaving the
    // ground under throttle, all one-shots (`pitch` is not read).
    void PlaySnowmobileSpecifics(float pitch);                                  // 0x0012ceb0
};
XBE_FIELD(ATrafficVehicle, siren, 0x20c);
XBE_FIELD(ATrafficVehicle, pitchOffset, 0x218);
XBE_FIELD(ATrafficVehicle, airborneWheels, 0x220);
XBE_FIELD(ATrafficVehicle, restCompression, 0x224);
XBE_FIELD(ATrafficVehicle, frontCompression, 0x238);
LAYOUT_32(sizeof(ATrafficVehicle) == 0x240, "ATrafficVehicle is 0x240 bytes");

// A helicopter's engine sound (0x310 bytes; vtable 0x001a2e58): a rotor and a machine gun
class AHelicopter : public AVehicle {
public:
    AVoice rotorVoice;              // +0x1f0 bank slot 0's SFX_Heli, SFX_Helidie once a landing runs out
    AVoice gunVoice;                // +0x248 bank slot 4's SFX_HeliMachGun, heard while `firing` is set
    AVoice unknownVoice2A0;         // +0x2a0 given no patch
    float rotorLevel;               // +0x2f8 rises to 0.8 by a sixtieth a frame (names ours)
    float landingTime;              // +0x2fc PlayLanding's 0.3, counted down by a sixtieth a frame; -1 once spent
    float pitchOffset;              // +0x300 a random number below 0.2 (InitAudioObject) or 0.1 (PHelicopter's
                                    //        constructor), added to the rotor's pitch (name ours)
    uint8_t unknown304[0xc];

    AHelicopter* Construct(const char *mixName);                                // 0x0012c7f0
    void Destruct();                                                            // 0x0012cac0
    AHelicopter* Delete(unsigned int flags);                                    // 0x0012cb60
    void Play(ASoundPlayParams *params);                                        // 0x0012c950
    // Vtable slot 4: starts the landing time if it is not running (the arguments are not read).
    void PlayLanding(float volume, const Coord3 *position, const Coord3 *velocity);    // 0x0012c930
};
XBE_FIELD(AHelicopter, gunVoice, 0x248);
XBE_FIELD(AHelicopter, unknownVoice2A0, 0x2a0);
XBE_FIELD(AHelicopter, rotorLevel, 0x2f8);
XBE_FIELD(AHelicopter, pitchOffset, 0x300);
LAYOUT_32(sizeof(AHelicopter) == 0x310, "AHelicopter is 0x310 bytes");

#endif // DRIVING_GAME_VEHICLESOUND_H_
