#ifndef DRIVING_GAME_VEHICLESOUNDKINDS_H_
#define DRIVING_GAME_VEHICLESOUNDKINDS_H_

// ---------------------------------------------------------------------------------------------------------------
// The vehicle kinds' sounds (game.audio): the AVehicle each vehicle's PBondCar::InitAudioObject makes -
// the player's car (APlayerVehicle), helicopter (APlayerHeli), tank (APlayerTank), ultralight (AUltraLite),
// snowmobile (ASnowMobile) and submersible (ASubmersible) - with the AI's cars (ATrafficVehicle) and helicopters
// (AHelicopter), whose classes are in VehicleSound.h since the vehicles' code reads them.
//
// Each kind's Play is its vtable's slot 3 (AVehicle's slots, VehicleSound.h); AUltraLite's PlayMotor and
// ASubmersible's SetCreakLevel add a slot 9. The vtables stay the game's. See VehicleSoundKinds.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "VehicleSound.h"               // AVehicle, AEngine, AVehicleWind, ATrafficVehicle, AHelicopter
#include "WeaponSound.h"                // ATurret
#include "../../common/xbeClass.h"      // XBE_FIELD
#include "../../common/xbeOverload.h"   // XbeVirtual

class AOneShotSound;

// ---- the player's car (0x220 bytes; vtable 0x001a2abc): an engine model, and road noise by the surface

class APlayerVehicle : public AVehicle {
public:
    char engineName[0x10];          // +0x1f0 the constructor's engine (GetName's answer)
    AEngine *engine;                // +0x200 AEngine::Get's for the engine
    AVoice *roadVoice;              // +0x204 bank slot 0's road noise for the last surface a wheel changed to
    uint32_t unknown208;
    int32_t roadSurfaces[4];        // +0x20c surfaces as Play last saw them (names ours)
    uint32_t unknown21c;

    APlayerVehicle* Construct(const char *engineName);                          // 0x001284b0
    void Destruct();                                                            // 0x001285f0
    APlayerVehicle* Delete(unsigned int flags);                                 // 0x00128670
    // Below 500 the rpm becomes 500; AVehicle's sounds, the engine's, the road voice by speed (15 to 50) and,
    // for each wheel on a new surface, a new road voice.
    void Play(ASoundPlayParams *params);                                        // 0x001286a0
};
XBE_FIELD(APlayerVehicle, engine, 0x200);
XBE_FIELD(APlayerVehicle, roadSurfaces, 0x20c);
LAYOUT_32(sizeof(APlayerVehicle) == 0x220, "APlayerVehicle is 0x220 bytes");

// ---- the player's helicopter (0x210 bytes; vtable 0x001a2b14)

// The azimuths each of three voices is heard at when the listener is close (AUltraLite, APlayerHeli)
enum CloseVoice { kCloseLeft, kCloseRight, kCloseCentre, kCloseVoices };

class APlayerHeli : public AVehicle {
public:
    AVoice *changeVoices[kCloseVoices];     // +0x1f0 SFX_helichange (names ours)
    AVoice *rotorVoices[kCloseVoices];      // +0x1fc SFX_heli
    float moving;                   // +0x208 eases towards one while the velocity is 1 or more, to zero below
    uint32_t unknown20c;

    APlayerHeli* Construct();                                                   // 0x00128a40
    void Destruct();                                                            // 0x001290e0
    APlayerHeli* Delete(unsigned int flags);                                    // 0x00129220
    // AVehicle's sounds, then the change voices (still) and rotor voices (moving), one each at the listener's
    // azimuth from 2 away, three around the listener closer.
    void Play(ASoundPlayParams *params);                                        // 0x00128d10
};
XBE_FIELD(APlayerHeli, rotorVoices, 0x1fc);
XBE_FIELD(APlayerHeli, moving, 0x208);
LAYOUT_32(sizeof(APlayerHeli) == 0x210, "APlayerHeli is 0x210 bytes");

// ---- the ultralight (0x230 bytes; vtable 0x001a2bc4): three motor sounds by the vertical velocity

// AUltraLite::voices' second index, by patch (names ours)
enum UltraLiteVoice {
    kUltraLiteDive,                 // SFX_bultralightDive: falling faster than 5
    kUltraLiteLevel,                // SFX_BultraliteLdLW: within 5 of level
    kUltraLiteClimb,                // SFX_BultraliteLdHI: rising faster than 5
    kUltraLiteVoices
};

class AUltraLite : public AVehicle {
public:
    AVoice *voices[kCloseVoices][kUltraLiteVoices];     // +0x1f0 bank slot 0's
    float motorPitch;               // +0x214 (names ours)
    float levelGain;                // +0x218 one from the constructor
    float diveGain;                 // +0x21c
    float climbGain;                // +0x220
    uint8_t unknown224[0xc];

    AUltraLite* Construct();                                                    // 0x0012a440
    void Destruct();                                                            // 0x00129b20
    AUltraLite* Delete(unsigned int flags);                                     // 0x0012a410
    // PlayMotor (through the vtable), then AVehicle's sounds.
    void Play(ASoundPlayParams *params);                                        // 0x00129b00
    // The gains and pitch eased towards the vertical velocity's, then the nine voices (vtable slot 9).
    void PlayMotor(ASoundPlayParams *params);                                   // 0x00129cd0

    void CallPlayMotor(ASoundPlayParams *params) {
        (this->*XbeVirtual<PlayMethod>(this, 9))(params);
    }

private:
    typedef void (AUltraLite::*PlayMethod)(ASoundPlayParams *params);
};
XBE_FIELD(AUltraLite, motorPitch, 0x214);
XBE_FIELD(AUltraLite, climbGain, 0x220);
LAYOUT_32(sizeof(AUltraLite) == 0x230, "AUltraLite is 0x230 bytes");

// ---- the snowmobile (0x260 bytes; vtable 0x001a2c40): carving, landings, a motor revved in the air

class ASnowMobile : public AVehicle {
public:
    AVehicleWind *snowWind;         // +0x1f0 a second wind, heard while the camera looks along the vehicle
    AVoice *roadVoice;              // +0x1f4 SFX_roadRN, the rear wheels on paved ground (names ours)
    AVoice *snowVoice;              // +0x1f8 SFX_snowRN, the rear wheels on snow, ice or grass
    AVoice *roadCarveVoice;         // +0x1fc SFX_roadcarve, the front wheels on paved ground
    AVoice *carveVoice;             // +0x200 SFX_carve, the front wheels on snow, ice or grass
    AVoice *idleVoice;              // +0x204 bank slot 9's SFX_idle
    AVoice *cruiseVoice;            // +0x208 bank slot 9's SFX_cruz
    AVoice *revVoice;               // +0x20c SFX_rev1 or SFX_rev2 from leaving the ground until it is done
    AMix *carvingMix;               // +0x210 "OffRoad"
    AMix *motorMix;                 // +0x214 the sound's mix
    int16_t airborneWheels;         // +0x218 GetAirborneWheels' answer when it last changed
    bool jumped;                    // +0x21a set when the first wheel leaves the ground, cleared on landing
    bool rearLifted;                // +0x21b the rear wheels were uncompressed at the last PlayMotor
    bool revving;                   // +0x21c a rev voice was started
    uint8_t unknown21d[3];
    float cameraAlong;              // +0x220 the camera's aim along the forward vector
    float cameraAcross;             // +0x224 and along the right vector
    float motorPitch;               // +0x228
    float restCompression[4];       // +0x22c each wheel's compression at rest
    float unknown23c;               // +0x23c 0.078 from the constructor
    float rearCompression;          // +0x240 the rear wheels' average
    float frontCompression;         // +0x244 the front wheels'
    float cruiseGain;               // +0x248 falls by a quarter each PlayMotor while revving; one otherwise
    float cruiseLevel;              // +0x24c the smoothed rpm over 1000, at most one
    float unknown250;               // +0x250 0.09 from the constructor
    float smoothedRpm;              // +0x254
    uint8_t unknown258[8];

    // `name` is not read.
    ASnowMobile* Construct(const char *name);                                   // 0x0012b5f0
    void Destruct();                                                            // 0x0012a890
    ASnowMobile* Delete(unsigned int flags);                                    // 0x0012b510
    // The camera's aim, the second wind, AVehicle's sounds, the carving and the motor.
    void Play(ASoundPlayParams *params);                                        // 0x0012b540
    // The idle and cruise voices by the smoothed rpm, a freewheel when the rear lifts under throttle, and a rev
    // voice for the first wheel leaving the ground under throttle.
    void PlayMotor(ASoundPlayParams *params);                                   // 0x0012aa10
    // Landing thumps by the front compression, and carving voices by the surfaces at the camera's sides.
    void PlayCarving(ASoundPlayParams *params);                                 // 0x0012b060
};
XBE_FIELD(ASnowMobile, revVoice, 0x20c);
XBE_FIELD(ASnowMobile, airborneWheels, 0x218);
XBE_FIELD(ASnowMobile, cameraAlong, 0x220);
XBE_FIELD(ASnowMobile, restCompression, 0x22c);
XBE_FIELD(ASnowMobile, smoothedRpm, 0x254);
LAYOUT_32(sizeof(ASnowMobile) == 0x260, "ASnowMobile is 0x260 bytes");

// ---- the submersible (0x240 bytes; vtable 0x001a2d44): fans, servos, cavitation, hull creaks

class ASubmersible : public AVehicle {
public:
    char soundName[0x10];           // +0x1f0 the constructor's name (GetName's answer)
    AVoice *jetVoice;               // +0x200 bank slot 9's SFX_jetcav1 (names ours)
    AVoice *rudderVoice;            // +0x204 SFX_rudcav1
    AVoice *fanVoice;               // +0x208 SFX_load
    AOneShotSound *cavitationShot;  // +0x20c the last SFX_rudcav2/3
    AOneShotSound *servoShot;       // +0x210 the last servo one-shot
    int16_t servoOnCount;           // +0x214 alternates the servo-on patches
    int16_t servoOffCount;          // +0x216 the servo-off ones
    int16_t cavitationCount;        // +0x218 SFX_rudcav2 and 3
    int16_t unknown21a;             // +0x21a 10 from the constructor
    bool unknown21c;
    bool fansOn;                    // +0x21d
    bool unknown21e;                // +0x21e one from the constructor
    bool rudderOn;                  // +0x21f
    bool jetOn;                     // +0x220
    uint8_t unknown221[3];
    float jetLevel;                 // +0x224 the jet's volume, falling by levelStep once it stops
    float rudderLevel;              // +0x228
    float levelStep;                // +0x22c 0.06 from the constructor
    int16_t creakLevel;             // +0x230 SetCreakLevel's: SFX_creak1..4, nothing below 1
    int32_t creakPeriod;            // +0x234 the creaks' period in audio frames
    int32_t creakDelay;             // +0x238 this creak's: the period with a tenth of it in normal variation
    int32_t creakTime;              // +0x23c audio frames since the last creak

    ASubmersible* Construct(const char *name);                                  // 0x0012b960
    void Destruct();                                                            // 0x0012be30
    ASubmersible* Delete(unsigned int flags);                                   // 0x0012c4e0
    // AVehicle's sounds, then the fans, servos, cavitation and creaks.
    void Play(ASoundPlayParams *params);                                        // 0x0012c510
    // Vtable slot 9.
    void SetCreakLevel(int16_t level, int32_t period);                          // 0x0012bb60

    // SetCreakLevel through the vtable (slot 9), as ESetCreakLevel calls it: the level zero-extended
    void CallSetCreakLevel(uint16_t level, int32_t period) {
        (this->*XbeVirtual<void (ASubmersible::*)(uint16_t, int32_t)>(this, 9))(level, period);
    }
    void PlayCreaks(ASoundPlayParams *params);                                  // 0x0012bb80
    void PlayFanMotors(ASoundPlayParams *params);                               // 0x0012bc60
    void PlayCavitation(ASoundPlayParams *params);                              // 0x0012bf00
    void PlayServos(ASoundPlayParams *params);                                  // 0x0012c260
};
XBE_FIELD(ASubmersible, cavitationShot, 0x20c);
XBE_FIELD(ASubmersible, servoOnCount, 0x214);
XBE_FIELD(ASubmersible, fansOn, 0x21d);
XBE_FIELD(ASubmersible, jetLevel, 0x224);
XBE_FIELD(ASubmersible, creakLevel, 0x230);
XBE_FIELD(ASubmersible, creakTime, 0x23c);
LAYOUT_32(sizeof(ASubmersible) == 0x240, "ASubmersible is 0x240 bytes");

// ---- the player's tank (0x210 bytes; vtable 0x001a2e28): an engine model, treads and a turret

class APlayerTank : public AVehicle {
public:
    ATurret tankTurret;             // +0x1f0 an ATurret of its own (AVehicle::turret is ActivateTurret's)
    AEngine *engine;                // +0x204 AEngine::Get's for the engine
    AMix *treadMix;                 // +0x208 "Tank Treads"
    AVoice *treadVoice;             // +0x20c bank slot 0's SFX_TankTread

    APlayerTank* Construct(const char *engineName);                             // 0x0012c550
    void Destruct();                                                            // 0x0012c730
    APlayerTank* Delete(unsigned int flags);                                    // 0x0012c7c0
    // AVehicle's sounds, the engine's, the treads by speed (to 8) while the rear wheels are down, the turret.
    void Play(ASoundPlayParams *params);                                        // 0x0012c640
};
XBE_FIELD(APlayerTank, engine, 0x204);
XBE_FIELD(APlayerTank, treadVoice, 0x20c);
LAYOUT_32(sizeof(APlayerTank) == 0x210, "APlayerTank is 0x210 bytes");

#endif // DRIVING_GAME_VEHICLESOUNDKINDS_H_
