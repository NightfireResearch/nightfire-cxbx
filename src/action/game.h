#ifndef GAME_H
#define GAME_H

#include "actionhelpers.h"

void Game_Run(void);
void GameFlow_Main(void);
bool movieFinished(void);
bool Graphics_IsPalI(void);
void mainloop(void);
void bootup_bootup(void);
void psiInitTimeIn100ths(void);
unsigned long long psiGetTimeIn100ths(void);
void Reset_MapLoadSettings(void);
uint GameFlow_GetState(void);
void GameFlow_PushState(int state, float param_2, uint param_3);
void ResetMap_LevelToLoad(HASHCODE level, bool warmReset, bool skipFmv);
void psiStopBackgroundMovie(void);
void psiStartBackgroundMovie(HASHCODE hashcode, char looping, int volume);
void GS_SetRefreshRate(int gameFrameRate, int videoFrameRate);
void GS_PauseGame(bool pause);
void GS_PausePlayer(char pause, ushort playerNum);
bool GS_IsPaused(short playerNum);

#pragma pack(push, 1)
typedef struct {
    undefined4 field0_0x0;
    undefined4 field1_0x4;
    HASHCODE NextLevelHashcode;
    HASHCODE CurrentLevelHashcode;
    undefined4 MovieHashcode;
    undefined4 field5_0x14;
    undefined4 ReloadGame;
    undefined4 ReloadMenupage;
    undefined4 ReloadMainMenu;
    undefined4 maybePaused;
    undefined4 difficultyModifier;
    undefined maybeUnused; // no apparent sites where this is used
    undefined field12_0x2d;
    undefined field13_0x2e;
    undefined field14_0x2f;
    undefined4 maybeLoadingBlobs;
    undefined4 NumFramesUnpaused;
    undefined4 VideoFrames;
    undefined4 NumFrames;
    undefined4 InhibitGameDraw;
    undefined4 field20_0x44;
    undefined4 BaseMapHashCode;
    undefined4 LoadTimeStart;
    undefined SomeAlternatePauseState;
    char VibrationEnabled;
    undefined1 WeaponUpgradeRelated; // upgrades Kowloon to automatic
    undefined field26_0x53;
    undefined4 isMultiplayerLevel;
} GameState_t;

static_assert(sizeof(GameState_t) == 0x58, "Bad size for GameState");

#define GameState (*((GameState_t*)0x001f6580))

struct CheatInfo_t {
    undefined4 Immortal;
    undefined4 AllWeapons;
    undefined4 UnlimitedAmmo;
    undefined field3_0xc;
    undefined field4_0xd;
    undefined field5_0xe;
    undefined field6_0xf;
    undefined4 someThing;
    undefined field11_0x14;
    undefined field12_0x15;
    undefined field13_0x16;
    undefined field14_0x17;
    undefined field15_0x18;
    undefined field16_0x19;
    undefined field17_0x1a;
    undefined field18_0x1b;
    undefined field19_0x1c;
    undefined field20_0x1d;
    undefined field21_0x1e;
    undefined field22_0x1f;
    undefined field23_0x20;
    undefined field24_0x21;
    undefined field25_0x22;
    undefined field26_0x23;
    undefined field27_0x24;
    undefined field28_0x25;
    undefined field29_0x26;
    undefined field30_0x27;
    undefined field31_0x28;
    undefined field32_0x29;
    undefined field33_0x2a;
    undefined field34_0x2b;
    undefined field35_0x2c;
    undefined field36_0x2d;
    undefined field37_0x2e;
    undefined field38_0x2f;
};


typedef struct {
    char unknown[560]; // 464 bytes on PS2
} MPGame_t;


typedef struct {
    char unknown[0xc0];
    undefined4 MP_MaxDuration;
    undefined4 MP_MaxPoints;
    undefined4 MP_FriendlyFire;
    undefined4 MP_WeaponSet;
    undefined4 MP_ProfessionalModeDamage;
    undefined4 MP_LocationDamage;
    undefined4 MP_ShowTeamAndName;
    undefined4 MP_RespawnMode;
    undefined4 MP_GunEmplacements;
    undefined4 MP_ExplosiveScenery;
    char unknown1[68];
    undefined4 Cheat_Immortal;
    undefined4 Cheat_UnlimitedAmmo;
    undefined4 Cheat_AllWeapons;
    undefined4 NightfireStatus;
} PTP_Eurocom;

typedef struct {
    char unknown[300*4]; // No clue any of this so far
} PTP_EA;

typedef struct { /* PTP Data */
    union {
        PTP_Eurocom ECDataBuf;
        char asBytes[1200];
    };
    union {
        PTP_EA EACDataBuf;
        char asBytes[1200];
    };
    undefined4 Version;
    undefined4 SoundVol;
    undefined4 MusicVol;
    undefined4 ProgressiveScan;
    undefined4 WideScreen;
    undefined4 LevelToLoad;
    undefined4 LevelStatus;
    undefined4 fNextModule;
    undefined4 fShouldRebootIOP;
    undefined4 fSkillLevel;
    undefined4 fLanguage;
    short fControllerConfig;
    undefined field14_0x98e;
    undefined field15_0x98f;
    bool fControllerPort;
    undefined field17_0x991;
    undefined field18_0x992;
    undefined field19_0x993;
    bool fVibration;
    undefined field21_0x995;
    undefined field22_0x996;
    undefined field23_0x997;
    undefined4 fHudAlwaysVisible;
    undefined4 fCrossHairOff;
    undefined4 fCheat;
    undefined4 fEasterEgg;
    undefined4 Scoring[9][3];
    short fControllerConfigDriving;
    undefined field30_0xa16;
    undefined field31_0xa17;
    undefined4 fLoadedFromMemCard;
    undefined4 TvType;
    undefined4 fLoadedFromCDOrDVD;
    bool fControllerInvert;
    undefined field36_0xa25;
    undefined field37_0xa26;
    undefined field38_0xa27;
    undefined4 fSoundMode;
    undefined4 fSubTitles;
    undefined4 fScreenAdjustX;
    undefined4 fScreenAdjustY;
    undefined4 fAntialiasMode;
    undefined4 fMedalAwarded;
    bool fAutoAim;
    undefined field46_0xa41;
    undefined field47_0xa42;
    undefined field48_0xa43;
    bool fManualAimToggle;
    undefined field50_0xa45;
    undefined field51_0xa46;
    undefined field52_0xa47;
    bool fAutoSwitchWeapons;
    undefined field54_0xa49;
    undefined field55_0xa4a;
    undefined field56_0xa4b;
    undefined4 field57_0xa4c;
} sNightFireShared_tag;

typedef struct {
    undefined4 field0_0x0;
    undefined4 field1_0x4;
    char DecoderTarget[4];
    undefined field3_0xc;
    char DecoderDisplay[4];
    undefined field5_0x11;
    char DecoderNumDigits;
    char DecoderDigitAt;
    undefined field8_0x14;
    undefined field9_0x15;
    undefined field10_0x16;
    undefined field11_0x17;
} GlobalVars_t;


static_assert(sizeof(sNightFireShared_tag) == 2640, "Size of sNightFireShared not correct");

typedef enum WeaponBaseNum {
    Weap_None=0,
    Weap_SubTorpedo=48,
    Weap_LaserBeamFromSamurai=51,
    Weap_RemoteMine=55,
    Weap_Satchel=59,
    Weap_OddjobHat=69,
    Weap_MaybeHandsOnlyOrLadder = 71,
    Weap_Ronin=82,
    Weap_Camera=84,
    Weap_Camera_Upgraded=85,
    Weap_Decryptor=86,
    Weap_Decryptor_Upgraded=87,
    Weap_QWorm = 88,
    Weap_MaybeNightvision = 0x5d,
    Weap_CopterGunCastle=97,
    Weap_CopterMissileCastle=98,
    Weap_Laser=103,
    Weap_SubLaser=104,
    Weap_SmokeGrenade=105,
    Weap_LaserBurst1=106,
    Weap_SpaceLaser=107,
    Weap_CopterMissile1=109,
    Weap_LaserBurst2=110,
    Weap_Samurai=111,
    Weap_CopterGun1=113,
    NUM_WEAPONS=114,
} WeaponBaseNum;

typedef struct {
    short weaponVariantNum; /* Unique index into this array */
    char weaponBaseNum; /* WeaponBaseNum: Upgrades/variants will have the same base number */
    undefined field2_0x3;
    bool isBaseWeapon;
    uchar offsetToNextAltFireVariant;
    undefined1 maybeFlags;
    undefined field6_0x7;
    float maybeExplodeRange;
    float damage;
    ushort field9_0x10;
    undefined field10_0x12;
    undefined field11_0x13;
    float autoaimRelated;
    undefined1 numBulletsPerShot;
    undefined field14_0x19;
    undefined field15_0x1a;
    undefined field16_0x1b;
    float someDistance;
    float unknownPurposeMaybeFloat;
    float maybeAccuracyRelated;
    short indexIntoSomePlayerAmmoArray;
    short maybeUnused;
    short droneBulletBurstTimeRelated;
    short unk15;
    Action_TranslatedText fireModePrimary;
    Action_TranslatedText fireModeSecondary;
    Action_TranslatedText weaponNameLongSp;
    Action_TranslatedText weaponNameShortMp;
    int unk16;
    uint unk17;
    HASHCODE muzzleFlash1stPerson;
    HASHCODE muzzleFlash3rdPerson;
    byte animDatumRelated3;
    undefined field33_0x51;
    undefined field34_0x52;
    undefined field35_0x53;
    undefined1 muzzleFlash_b; /* Created by retype action */
    undefined1 muzzleFlash_g; /* Created by retype action */
    undefined1 muzzleFlash_r; /* Created by retype action */
    undefined field39_0x57;
    float muzzleFlashBrightness; /* Created by retype action */
    HASHCODE projectileGfx;
    ushort animDatumRelated2;
    undefined field43_0x62;
    undefined field44_0x63;
    undefined field45_0x64;
    undefined field46_0x65;
    undefined field47_0x66;
    undefined field48_0x67;
    uint someFlags; /* Created by retype action */
    uint flagsForSwooshAndCasing; /* Created by retype action */
    uint someFlagsRelatedToExplosiveTimer;
    short unk18;
    short swooshRelated;
    short casingDelayFrames; /* Created by retype action, size unclear */
    short unk19;
    HASHCODE suppressorGfx;
    HASHCODE wpn3rdPersonGfx; /* Created by retype action */
    uint weaponAnimationSet; /* Created by retype action */
    float maxZoom;
    float cameraSwingAmt; /* Created by retype action */
    uchar ammoType; /* Created by retype action */
    uint8_t cooldownTimerIncreaseAmount;
    short clipSizeOrCooldown; /* Created by retype action */
    uint8_t rumble; /* Created by retype action */
    uint8_t _pad1;
    uint8_t _pad2;
    uint8_t _pad3;
    float accuracyModifierSomehow;
    float unk22;
    HASHCODE animScriptTag; /* Created by retype action */
    HASHCODE someAnimhc3;
    HASHCODE someAnimHsh;
    HASHCODE maybeAnAnimScript;
    HASHCODE maybeWeaponFireAnimHashcode2;
    HASHCODE maybeWeaponFireAnimHashcode;
    HASHCODE someAnimHC2;
    HASHCODE someAnimHC;
    HASHCODE animationHashcode;
    HASHCODE animSpeedRelated; /* Could also be a hashcode? */
    HASHCODE field80_0xc8;
    HASHCODE anotherAnimScriptTag; /* Created by retype action */
    HASHCODE field82_0xd0;
    HASHCODE field83_0xd4;
    HASHCODE animationScript;
    HASHCODE weaponModelHashcode;
    float animRelated1[3];
    float animRelated2[3];
    float casingSpawnPos[3];
    undefined4 multiplayerWeaponFiredCallback; /* Created by retype action */
    undefined field90_0x108;
    undefined field91_0x109;
    undefined field92_0x10a;
    undefined field93_0x10b;
} weapon_definition_tag;

static_assert(sizeof(weapon_definition_tag) == 0x10c, "Size of weapon_definition_tag not correct");
static_assert(offsetof(weapon_definition_tag,someDistance) == 0x1c, "someDistance is in the wrong place");

typedef struct {
    uint paused;
    char unknown_pad[0x1c-4];
    obj_tag* playerObj;
    short maybeIdxOfLastInjurer; // Index of who or what last dealt me damage? -2 = environment?
    short friendlyFireLabelTimer;
    short friendlyFireProtectionLabelTimer;
    char unknown_pad1[2];
    short maybeIdxOfMyAssassin;
    char unknown_pad2[0x30-0x2a];
} MPGamePlayer;

static_assert(sizeof(MPGamePlayer) == 0x30, "MPGamePlayer is wrong size"); // Determined from stride length in various funcs

typedef struct {
  // Note that PS2 and Xbox have different number of entries in MPGame! PS2 has 8, Xbox has 10
  MPGamePlayer players[10];
  // Immediately following is more state related to MP game
  uint unknown_1; // end conditions / debriefing / objective related
  uint unknown_2; // end conditions / debriefing
  uint EndGameFlowState;
  uint unknown_3; // end conditions
  uint TimeUnpaused;
  float TimeLimit;
  float restartScenarioTimeout;
  uint TimeIncPaused; // pickups, opponent selection, visit times?? possibly misidentified?
  float winStateTimeout; // MP init and update
  float lastTimePaused; // end conditions
  short unknown_maybe_capture_state; // player status / goals
  short unknown_maybe_unused; // restart
  short unknown_9; // uplink, goldeneye, blueprint timers?
  short maybe_pad;
  sprite* radar_related[2 * 4]; // One pair per human participant
} MPGameStruct;

static_assert(sizeof(MPGameStruct) == 0x230, "MPGameStruct is wrong size"); // Determined from MP_Init

static_assert(offsetof(MPGameStruct, lastTimePaused) == 0x204, "Offset of lastTimePaused wrong");

#pragma pack(pop)


#define MPGame (*(MPGameStruct(*))0x00262738)

#endif // GAME_H