#ifndef GAME_H
#define GAME_H

#include "actionhelpers.h"

void Game_Run(void);
void GameFlow_Main(void);
bool movieFinished(void);
void __profiling_or_debugging_hook_point(void);
bool Graphics_IsPalI(void);
bool IsNotPalI(void);
bool Graphics_IsSomeGraphicsRegion(void);
int GetTvType(void);
HASHCODE maybeGetNextMissionInSequence(HASHCODE movie);
bool Graphics_IsWidescreen(void);
bool Graphics_IsSomeRegionBasedThing(void);
void mainloop(void);
void bootup_bootup(void);
void Boot_GetPTPData(void **data, uint *size);
void __stdcall Boot_LoadPTPData(void);
void psiInitTimeIn100ths(void);
unsigned long long psiGetTimeIn100ths(void);

// Milliseconds from a free-running host clock. Reimplemented rather than used from the image, because
// the original divides the CPU's cycle counter by the Xbox's own 733 MHz - see game.cpp.
double timestamp(void);

// The XAPI's own performance counter pair, replaced for the same reason - see game.cpp. Patched by
// address, so these names are ours and only the addresses matter. The parameter type is spelled by its
// tag so that this header does not have to drag in windows.h, which collides with the Xbox-shaped
// XINPUT structures elsewhere in the tree.
union _LARGE_INTEGER;
uint32_t __stdcall Xbox_QueryPerformanceCounter(union _LARGE_INTEGER *counter);
uint32_t __stdcall Xbox_QueryPerformanceFrequency(union _LARGE_INTEGER *frequency);
void Reset_MapLoadSettings(void);
uint GameFlow_GetState(void);
void ResetMap_LevelToLoad(HASHCODE level, bool warmReset, bool skipFmv);
void ResetMap_GameInit(void);
void ResetMap_GenLoadScreen(void);
void ResetMap_Load(void);
void GameFlow_PushState(int state, float time, uint colour);
uint GameFlow_PopState(void);
void set_InhibitGameDrawIfRequired(void);
// The loading screen's picture for a level and its hint and objective text (0x000bedf0). The original takes its
// arguments in registers: ResetMap_LevelCode2Img is the entry for that, _ResetMap_LevelCode2Img the C++ under it.
HASHCODE ResetMap_LevelCode2Img(HASHCODE level, char **hintOut, char **objectiveOut);
HASHCODE _ResetMap_LevelCode2Img(HASHCODE level, char **hintOut, char **objectiveOut);
void psiStopBackgroundMovie(void);
void maybeStartBackgroundMovie(void);
void psiStartBackgroundMovie(HASHCODE hashcode, char looping, int volume);
void GS_SetRefreshRate(int gameFrameRate, int videoFrameRate);
void GS_PauseGame(bool pause);
void GS_PausePlayer(char pause, ushort playerNum);
bool GS_IsPaused(short playerNum);

#pragma pack(push, 1)
typedef struct {
    undefined4 field0_0x0;
    uint32_t collisionStamp;   // 0x04 - Collide_StraddleCels takes the next one per query, to mark portals visited
    HASHCODE NextLevelHashcode;
    HASHCODE CurrentLevelHashcode;
    undefined4 MovieHashcode;
    uchar field5_0x14;     // 0x14 - cleared with MovieHashcode by Trigger_Update, Trigger_Activate and ResetMap_Load
    undefined field5_0x15[3];
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
    uchar InhibitGameDraw;      // 0x40 - a byte: every reader and writer takes one
    undefined field19_0x41[3];
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

extern GameState_t GameState;
// The hashcode of the background movie that is playing, 0 when none
extern uint32_t BackgroundMovieHashcode;

struct CheatInfo_t {
    undefined4 Immortal;
    undefined4 AllWeapons;
    undefined4 UnlimitedAmmo;
    undefined4 pauseCleared; // P_PAUSE clears it as the page opens; nothing reads it
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


// The action engine's part of the page handed between the engines (Boot_GetPTPData and Boot_LoadPTPData): the
// codename's settings, which the driving engine hands back
typedef struct {
    char codename[32];          // 0x00 ls.codename
    char unknown20[0x60];
    uint codenameSlot;          // 0x80 ls.slot
    uint canSave;               // 0x84 ls.canSave
    uint crouchToggle;          // 0x88 player 1's PlayerInput settings
    uint manualAimToggle;       // 0x8c
    uint autoAim;               // 0x90
    uint autoSwitchBetterWeapon; // 0x94
    uint input05;               // 0x98 PlayerInput::field5_0x5
    uint levelViewOnPace;       // 0x9c
    uint rememberZoom;          // 0xa0
    uint sfxLanguage;           // 0xa4
    uint flashingObjects;       // 0xa8 PlayerInput::field12_0xc
    int controlStyle;           // 0xac
    uint mpSomeField2;          // 0xb0 MPSettings.Player[0]'s
    int healthModifier;         // 0xb4
    uint autoAimMultiplayer;    // 0xb8
    uint splitScreenLayout;     // 0xbc MultiplayerLayout_LeftRightOrTopBtm
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
    int musicVolume;            // 0xe8
    int sfxVolume;              // 0xec
    char defaultCodename[0x38]; // 0xf0 the second default codename (def_codename[1]), whole
    uint unlockEverything;      // 0x128 menu_unlock_everything
    undefined4 Cheat_Immortal;  // 0x12c
    undefined4 Cheat_UnlimitedAmmo;
    undefined4 Cheat_AllWeapons;
    undefined4 NightfireStatus; // 0x138
    uint bonusHi;               // 0x13c Menu_GetBonus's, player 1's
    uint bonusLo;               // 0x140
    int difficulty;             // 0x144
    uint mainMenuSound;         // 0x148 ls.relatedToMainMenuSound
    HASHCODE level;             // 0x14c the level to go to
    uint weaponUpgrade;         // 0x150 GameState.WeaponUpgradeRelated
    uint bestScores[12];        // 0x154 PlrStats_GetDrivingData's
    uint bondMoments[12];       // 0x184
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
        // Named apart from the first union's asBytes. Both unions are anonymous, so their members are
        // members of this struct, and MSVC tolerates the repeated name where every other compiler rejects
        // it. Neither member is referenced anywhere - they record that each buffer is 1200 bytes.
        char asBytesEAC[1200];
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
    int fControllerConfig;      // 0x98c
    uint fControllerPort;
    uint fVibration;
    undefined4 fHudAlwaysVisible;
    undefined4 fCrossHairOff;
    undefined4 fCheat;
    undefined4 fEasterEgg;
    undefined4 Scoring[9][3];
    int fControllerConfigDriving; // 0xa14
    undefined4 fLoadedFromMemCard;
    undefined4 TvType;
    undefined4 fLoadedFromCDOrDVD;
    uint fControllerInvert;     // 0xa24
    undefined4 fSoundMode;
    undefined4 fSubTitles;
    undefined4 fScreenAdjustX;
    undefined4 fScreenAdjustY;
    undefined4 fAntialiasMode;
    undefined4 fMedalAwarded;
    uint fAutoAim;              // 0xa40
    uint fManualAimToggle;
    uint fAutoSwitchWeapons;
    undefined4 field57_0xa4c;
} sNightFireShared_tag;

typedef struct {
    obj_tag *keypadPlayer;     // 0x00 - who opened a lock's keypad (Lock_Activate)
    BLData *keypadBLData;      // 0x04 - and his BLData
    char DecoderTarget[4];     // 0x08 - the lock's code, copied with its KeyCodeEntry
    undefined DecoderTargetKnown; // 0x0c - the KeyCodeEntry's discovered
    char DecoderDisplay[4];
    undefined field5_0x11;
    uchar DecoderNumDigits;
    uchar DecoderDigitAt;  // the next digit C_KEYPAD_Handler writes
    uchar DecoderDisplayShown; // 0x14 - set while a lock shows DecoderDisplay on the decryptor (Player_WeaponFiring)
    undefined field9_0x15;
    undefined field10_0x16;
    undefined field11_0x17;
} GlobalVars_t;


static_assert(sizeof(sNightFireShared_tag) == 2640, "Size of sNightFireShared not correct");
static_assert(sizeof(PTP_Eurocom) == 0x1b4, "PTP_Eurocom is 0x1b4 bytes");
static_assert(offsetof(PTP_Eurocom, MP_MaxDuration) == 0xc0, "PTP_Eurocom MP_MaxDuration offset incorrect");
static_assert(offsetof(PTP_Eurocom, Cheat_Immortal) == 0x12c, "PTP_Eurocom Cheat_Immortal offset incorrect");
static_assert(offsetof(sNightFireShared_tag, fControllerConfig) == 0x98c, "fControllerConfig offset incorrect");
static_assert(offsetof(sNightFireShared_tag, fControllerConfigDriving) == 0xa14, "fControllerConfigDriving offset incorrect");
static_assert(offsetof(sNightFireShared_tag, fAutoAim) == 0xa40, "fAutoAim offset incorrect");

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

// One entry per weapon variant; weapon_data[115] at 0x0018cfa0. Entries 0x35.. are built by
// WeaponDataTableInit (0x000f5530). See docs/weapons.md.
typedef struct weapon_definition_tag {
    short weaponVariantNum;           // 0x000 this entry's index
    short weaponBaseNum;              // 0x002 base variant of the group
    bool isBaseWeapon;                // 0x004 owns the group's inventory slot
    int8_t offsetToNextAltFireVariant; // 0x005 entry offset to the alt-fire variant (signed)
    uchar botWeaponClass;             // 0x006 1 handgun 2 auto 3 sniper 4 explosive
    uchar _pad07;                     // 0x007
    float explodeRadius;              // 0x008 explosion size, units
    float damage;                     // 0x00c damage per hit / explosion strength
    ushort damageClass;               // 0x010 weapon class bit, see doc
    uchar _pad12[2];                  // 0x012
    float autoAimStrength;            // 0x014 percent
    uchar numBulletsPerShot;          // 0x018 projectiles per shot
    uchar _pad19[3];                  // 0x019
    float range;                      // 0x01c max travel / use range, units
    float projectileSpeed;            // 0x020 units per 60 Hz frame
    float baseSpread;                 // 0x024 x0.0014 rad
    short shotsPerTrigger[2];         // 0x028 per fire mode, 999 = auto
    short droneBurstLength;           // 0x02c shots per drone burst
    char numFireModes;                // 0x02e modes the alt-fire button cycles
    uchar _pad2f;                     // 0x02f
    Action_TranslatedText fireModeName[2]; // 0x030 HUD text per fire mode
    Action_TranslatedText nameLong;   // 0x038 single-player HUD name
    Action_TranslatedText nameShort;  // 0x03c multiplayer HUD name
    int refireDelay;                  // 0x040 frames between shots (min 1)
    ushort bulletSpawnFrame;          // 0x044 fire-anim frame that spawns the bullet
    uchar _pad46[2];                  // 0x046
    HASHCODE muzzleFlash1stPerson;    // 0x048 first-person flash entity (0x02)
    HASHCODE muzzleFlash3rdPerson;    // 0x04c third-person flash entity (0x02)
    uchar weaponDatum;                // 0x050 first weapon datum on the skeleton, 0xff none
    uchar muzzleFlashAlphaMin;        // 0x051 flash sprite brightness range
    uchar muzzleFlashAlphaMax;        // 0x052
    uchar _pad53;                     // 0x053
    uchar muzzleFlash_b;              // 0x054 light colour, blue
    uchar muzzleFlash_g;              // 0x055 light colour, green
    uchar muzzleFlash_r;              // 0x056 light colour, red
    uchar _pad57;                     // 0x057
    float muzzleLightRadius;          // 0x058 Light_Create radius
    HASHCODE projectileGfx;           // 0x05c projectile entity (0x02)
    ushort fireSound3rdPerson;        // 0x060 SFX id for drones/scripts
    uchar _pad62[2];                  // 0x062
    uchar unk64;                      // 0x064 0/2/3, no reader found
    uchar _pad65[3];                  // 0x065
    uint weaponFlags;                 // 0x068 see doc
    uint projectileFlags;             // 0x06c see doc
    uint impactFlags;                 // 0x070 see doc
    short swooshInterval;             // 0x074 frames between trail segments
    short swooshLifetime;             // 0x076 trail segment lifetime, frames
    uchar casingDelayFrames;          // 0x078 frames before the casing appears
    uchar _pad79;                     // 0x079
    short effectDurationFrames;       // 0x07a 15 s / 60 s, no reader found
    HASHCODE suppressorGfx;           // 0x07c silencer entity (0x02)
    HASHCODE worldModelGfx;           // 0x080 held/pickup entity (0x02)
    uchar animSet;                    // 0x084 third-person AnimSet
    uchar _pad85[3];                  // 0x085
    float maxZoom;                    // 0x088 max scope zoom factor
    float scopeSway;                  // 0x08c sway x (zoom - 1)
    uchar ammoType;                   // 0x090 index into ammo_data / BLData.ammo
    uchar ammoPerShot;                // 0x091
    short clipSize;                   // 0x092 clip or max charge
    uchar rumble;                     // 0x094 rumble strength 0-100
    uchar _pad95[3];                  // 0x095
    float spreadPerShot;              // 0x098 bloom per shot
    float unk9c;                      // 0x09c no reader found
    HASHCODE animIdle;                // 0x0a0
    HASHCODE animReload;              // 0x0a4
    HASHCODE animReloadStart;         // 0x0a8
    HASHCODE animReloadEnd;           // 0x0ac
    HASHCODE animFire;                // 0x0b0
    HASHCODE animFireAlt;             // 0x0b4
    HASHCODE animScopeIn;             // 0x0b8
    HASHCODE animScopeOut;            // 0x0bc
    HASHCODE animRaise;               // 0x0c0
    HASHCODE animLower;               // 0x0c4
    HASHCODE animModeSwitch;          // 0x0c8
    HASHCODE animFidget;              // 0x0cc
    HASHCODE animRelax;               // 0x0d0
    HASHCODE animRelaxEnd;            // 0x0d4
    HASHCODE animLowerRelaxed;        // 0x0d8
    HASHCODE weaponModelHashcode;     // 0x0dc first-person model (0x05)
    float viewOffset[3];              // 0x0e0 view-model offset, SP standing
    float viewOffsetAlt[3];           // 0x0ec MP / SP crouched
    float casingEjectVelocity[3];     // 0x0f8 bone space, units per frame
    void (*onFired)(obj_tag *player); // 0x104 always NULL
    void (*onImpact)(obj_tag *bullet, plane_equ_tag *plane, _VECTOR *pos); // 0x108 always NULL
} weapon_definition_tag;

static_assert(sizeof(weapon_definition_tag) == 0x10c, "weapon_definition_tag size");
static_assert(offsetof(weapon_definition_tag, botWeaponClass) == 0x006, "botWeaponClass");
static_assert(offsetof(weapon_definition_tag, damageClass) == 0x010, "damageClass");
static_assert(offsetof(weapon_definition_tag, range) == 0x01c, "range");
static_assert(offsetof(weapon_definition_tag, shotsPerTrigger) == 0x028, "shotsPerTrigger");
static_assert(offsetof(weapon_definition_tag, numFireModes) == 0x02e, "numFireModes");
static_assert(offsetof(weapon_definition_tag, refireDelay) == 0x040, "refireDelay");
static_assert(offsetof(weapon_definition_tag, weaponDatum) == 0x050, "weaponDatum");
static_assert(offsetof(weapon_definition_tag, fireSound3rdPerson) == 0x060, "fireSound3rdPerson");
static_assert(offsetof(weapon_definition_tag, weaponFlags) == 0x068, "weaponFlags");
static_assert(offsetof(weapon_definition_tag, casingDelayFrames) == 0x078, "casingDelayFrames");
static_assert(offsetof(weapon_definition_tag, animSet) == 0x084, "animSet");
static_assert(offsetof(weapon_definition_tag, ammoType) == 0x090, "ammoType");
static_assert(offsetof(weapon_definition_tag, clipSize) == 0x092, "clipSize");
static_assert(offsetof(weapon_definition_tag, animIdle) == 0x0a0, "animIdle");
static_assert(offsetof(weapon_definition_tag, weaponModelHashcode) == 0x0dc, "weaponModelHashcode");
static_assert(offsetof(weapon_definition_tag, casingEjectVelocity) == 0x0f8, "casingEjectVelocity");
static_assert(offsetof(weapon_definition_tag, onImpact) == 0x108, "onImpact");

typedef struct {
    uint paused;
    uint victories;         // the debriefing's "Victories"
    uint deaths;            // the debriefing's "Deaths" (Ghidra: pointsScored)
    char unknown0c[4];
    uint unknown10;         // 0x10 MP_PlayerKilled adds one with each victory and clears it on a suicide
    char unknown14[4];
    float points;           // the debriefing's "Points"
    obj_tag* playerObj;
    short maybeIdxOfLastInjurer; // Index of who or what last dealt me damage? -2 = environment?
    short friendlyFireLabelTimer;
    short friendlyFireProtectionLabelTimer;
    ushort flags;           // 0x26 MPGamePlayerFlags; NDrone2_FindOpponent tests the low 4 bits
    short maybeIdxOfMyAssassin;
    char unknown2a[2];
    float hillSoundTime;    // 0x2c MPGame.TimeIncPaused when MP_KOHUpdate last played the hill's entry sound
} MPGamePlayer;

enum MPGamePlayerFlags : ushort {
    MPPLAYER_FLAG_1 = 1,        // a CTF bot with it heads for its base (BOTSTATE_pickGoal)
    MPPLAYER_FLAG_2 = 2,        // a Blueprint bot with it heads for the espionage base; MP_BluePrintReachedBase clears it
    MPPLAYER_GOLDENEYE_KEY = 4,     // holds the GoldenEye key (MP_GoldenEyeUpdate)
    MPPLAYER_GOLDENEYE_CRYSTAL = 8, // holds the GoldenEye crystal (MP_GoldenEyeUpdate)
    MPPLAYER_IN_HILL = 0x10,    // inside the King of the Hill object's box (MP_KOHUpdate); a bot with it picks no objective
};

static_assert(sizeof(MPGamePlayer) == 0x30, "MPGamePlayer is wrong size"); // Determined from stride length in various funcs
static_assert(offsetof(MPGamePlayer, playerObj) == 0x1c, "Wrong offset for MPGamePlayer.playerObj");
static_assert(offsetof(MPGamePlayer, flags) == 0x26, "Offset of MPGamePlayer.flags wrong");
static_assert(offsetof(MPGamePlayer, hillSoundTime) == 0x2c, "Offset of MPGamePlayer.hillSoundTime wrong");

// The marks over the assassin and the target a player sees
typedef struct {
    sprite *target;
    sprite *assassin;
} MPRadarMarks;

typedef struct {
  // Note that PS2 and Xbox have different number of entries in MPGame! PS2 has 8, Xbox has 10
  MPGamePlayer players[NUM_AGENTS];
  // Immediately following is more state related to MP game
  float teamScore[2]; // by MPTeam: Phoenix, MI6 (MP_SortOutWhoWon, the debriefing)
  uint EndGameFlowState;
  uint unknown_3; // MP_CheckForEndCondition: the highest score, team or player
  float TimeUnpaused; // seconds of unpaused play, against TimeLimit (MP_CheckForEndCondition)
  float TimeLimit;
  float restartScenarioTimeout;
  float TimeIncPaused; // seconds of unpaused play, counted even with switch_MP4EVER: pickup visit times, bot goals
  float winStateTimeout; // MP init and update
  float lastTimePaused; // end conditions
  union {
    struct {
      short unknown_maybe_capture_state; // player status / goals
      short unknown_maybe_unused; // restart
      short unknown_9; // uplink, goldeneye, blueprint timers?
    };
    ushort teamObjectiveFlags[3]; // by MPTeam: bits 1 and 2 match an objective's MP_OBJ_EXT.flags, and a bot drops
                                  // its goal for an objective whose bit is set (BOTSTATE_processGoals). BOTSTATE_pickGoal
                                  // reads it for NO_TEAM too, which is unknown_9
  };
  short maybe_pad;
  MPRadarMarks radarMarks[NUM_PLAYERS]; // 0x210 - per human player, shown over the others in Assassin (HUD_RadarUpdate)
} MPGameStruct;

static_assert(sizeof(MPGameStruct) == 0x230, "MPGameStruct is wrong size"); // Determined from MP_Init

static_assert(offsetof(MPGameStruct, lastTimePaused) == 0x204, "Offset of lastTimePaused wrong");
static_assert(offsetof(MPGameStruct, TimeIncPaused) == 0x1fc, "Offset of TimeIncPaused wrong");
static_assert(offsetof(MPGameStruct, teamObjectiveFlags) == 0x208, "Offset of teamObjectiveFlags wrong");

#pragma pack(pop)


#define MPGame (*(MPGameStruct(*))0x00262738)

#endif // GAME_H
