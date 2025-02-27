#ifndef GAME_H
#define GAME_H

void Game_Run(void);
void GameFlow_Main(void);
bool movieFinished(void);
bool Graphics_IsPalI(void);
void mainloop(void);
void bootup_bootup(void);
void psiInitTimeIn100ths(void);
unsigned long long psiGetTimeIn100ths(void);

typedef unsigned int undefined4;
typedef unsigned char undefined;

#pragma pack(push, 1)
typedef struct {
    undefined4 field0_0x0;
    undefined4 field1_0x4;
    undefined4 NextLevelHashcode;
    undefined4 CurrentLevelHashcode;
    undefined4 MovieHashcode;
    undefined4 field5_0x14;
    undefined4 ReloadGame;
    undefined4 ReloadMenupage;
    undefined4 field8_0x20;
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
    undefined field23_0x50;
    char VibrationEnabled;
    undefined field25_0x52;
    undefined field26_0x53;
    undefined4 maybeIsMultiplayerMapLoading;
} GameState_t;
#pragma pack(pop)

static_assert(sizeof(GameState_t) == 0x58, "Bad size for GameState");

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

typedef enum MultiplayerGameMode {
    GM_QUICK=0,
    GM_ARENA=1,
    GM_TOPAGENT=16,
    GM_UNK3=32,
    GM_ASSASSIN=1024,
    TEAMGAME=536870912,
    GM_TEAMARENA=536870914,
    GM_CTF=536870916,
    GM_DEMOLITION=536870976,
    GM_PROTECTION=536871040,
    GM_BLUEPRINT=536871168,
    GM_GOLDENEYE=536871424,
    GM_KOTH=1073743872,
    GM_UPLINK=1610612744,
    GM_TEAMKOTH=1610616832,
    GM_FORCE_UINT32 = 0x7fffffff
} MultiplayerGameMode;

typedef enum WeaponSet {
    WEAPSET_NORMAL=0,
    WEAPSET_PISTOLS=1,
    WEAPSET_AUTOMATIC=2,
    WEAPSET_SNIPERS=3,
    WEAPSET_EXPLOSIVES=4,
    WEAPSET_EXPLOSIVES2=5,
    WEAPSET_MI6=6,
    WEAPSET_PHOENIX=7,
    WEAPSET_MODERN=8,
    WEAPSET_STEALTHY=9,
    WEAPSET_RANDOM=10,
    WEAPSET_FORCE_UINT32 = 0x7fffffff
} WeaponSet;


#pragma pack(push, 1)
typedef struct { // on Xbox, starts at 0025fe38

    char _unkno[0x1E0]; // Different on PS2 and Xbox.  1E0: Xbox

    undefined4 isMultiplayer; // on Xbox, at 00260018
    undefined4 field50_0x184;
    undefined4 Started;
    undefined4 field52_0x18c;
    undefined4 field53_0x190;
    undefined4 numPlayersAndBots;
    undefined4 FriendlyFire;
    undefined4 MaxPoints;
    undefined4 MaxDuration;
    enum MultiplayerGameMode GameMode;
    undefined4 multiplayerLevelHashcode;
    undefined4 numPlayers;
    undefined4 numBots;
    enum WeaponSet weaponSet;
    undefined4 GunEmplacementsEnabled;
    undefined4 TripleDamageModifierProfessionalMode;
    undefined4 RespawnSelectionMode;
    undefined4 ShowTeamAndNameOverhead;
    undefined4 LocationDamageEnabled;
    undefined4 MiniVehiclesEnabled;
    undefined4 GrappleEnabled;
    undefined4 ExplosiveSceneryEnabled;
    short numActivePickups;
    undefined field72_0x1da;
    undefined field73_0x1db;
} MPSettings_t;
#pragma pack(pop)

static_assert(sizeof(MPSettings_t) == 572, "MPSettings_t is wrong size");

//char (*__kaboom)[sizeof(MPSettings_t)] = 1;

#endif // GAME_H