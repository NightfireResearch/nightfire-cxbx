// Shadow tests for a drone's mode and type settings (game/drone/DroneModes.cpp): each original and ours run on the
// same made-up drone, behaviour block and stats. Run at start with MenuShadowTests=on (MenuProbe.cpp), before any
// level exists; nothing here needs one. behaviour_util_*, Drone_DCVfromOBJ and the tables are the same for both
// sides; the type initialisers the tables point at are swapped with the rest, so the original reaches originals.

#include "DroneModeShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../game/drone/Behaviour.h"
#include "../game/drone/DroneModes.h"
#include "../game/drone/DroneTables.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

typedef void (*DroneFunc)(Drone_tag *drone);

struct World {
    Drone_tag drone;
    obj_tag obj;
    AnimState anim;
    DIVars_tag diVars;
    uint behaviour[64];
};
World world;

ShadowRegions regions;
int cases, differ;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "dronemode", what, trial, theirs, ours))
        differ++;
}

// Every original of DroneModes.cpp at once (0x3fae0 GetDTYPENEW to 0x41230 DoModeSettingsNEW's end, OLD included),
// and ChangeToAttackMode
struct Originals {
    Originals() {
        XbeOriginal_RestoreRange(0x0003fae0, 0x000417f0, true);
        XbeOriginal_Restore(0x000397f0, true);
    }
    ~Originals() {
        XbeOriginal_Restore(0x000397f0, false);
        XbeOriginal_RestoreRange(0x0003fae0, 0x000417f0, false);
    }
};

template <typename T> T Pick(const T *values, int n) {
    return values[ShadowRand(n)];
}

uint32_t RandWord() {
    return ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

// A behaviour block, mostly valid (docs/drone/behaviours/README.md 2.1)
void RandomBehaviourBlock() {
    uint *p = world.behaviour;
    for (int i = 0; i < 64; i++)
        p[i] = RandWord();
    *p++ = ShadowRand(8) == 0 ? NUM_BEHAVIOUR_CLASSES + ShadowRand(3) : ShadowRand(NUM_BEHAVIOUR_CLASSES);
    for (int b = 0; b < 2; b++) {
        *p++ = ShadowRand(10) == 0 ? 6 + ShadowRand(2) : ShadowRand(6);
        uint count = ShadowRand(12) == 0 ? ShadowRand(5) : 1 + ShadowRand(3);
        uint props = ShadowRand(12) == 0 ? 0x5c : 1 + ShadowRand(0x5b);
        *p++ = count | (props << 16);
        p += count;
    }
    switch (ShadowRand(3)) {
    case 0:
        *p = ShadowRand(0x1000);
        break;
    case 1:
        *p = 0x80010000 | (3 * (1 + ShadowRand(3)));
        break;
    default:
        *p = 0x80000000 | ShadowRand(0x10000);
        break;
    }
}

const float kAlertness[] = {0.0f, -0.0f, 0.5f, 0.66f, 0.99999994f, 1.0f, 1.5f, NAN};
const short kStates[] = {0, 4, 5, 6, 8, 10, 0x10, 0x12, 0x1c, 0x29, 0x2a, 0x2e, 0x30, 0x31, 0x32, 0x36, 0x37,
                         0x56, 0x85, 0x89, 0xa6, 0xa7, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, -1, 3};

void RandomWorld() {
    uint8_t *bytes = (uint8_t *)&world;
    for (size_t i = 0; i < sizeof(world); i++)
        bytes[i] = (uint8_t)ShadowRand(256);
    RandomBehaviourBlock();

    Drone_tag *d = &world.drone;
    d->gameObj = ShadowRand(10) == 0 ? NULL : &world.obj;
    world.obj.animState = &world.anim;
    world.obj.extraObjectData = d;
    world.obj.objectType = ShadowRand(2) ? OBJECTTYPE_DRONE : OBJECTTYPE_DEAD_DRONE;
    d->currentBehaviour = ShadowRand(4) == 0 ? d->behaviour2 : d->behaviour1;
    static const short skins[] = {0, 5, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14, -1};
    d->skinClass = Pick(skins, 13);
    d->alertness = Pick(kAlertness, 8);
    d->baseAlertness1 = Pick(kAlertness, 8);
    d->baseAlertness2 = Pick(kAlertness, 8);
    d->behaviourClass = (uchar)ShadowRand(NUM_BEHAVIOUR_CLASSES + 2);
    d->behaviour1Mode = (uchar)ShadowRand(8);
    d->behaviour2Mode = ShadowRand(3) == 0 ? d->behaviour1Mode : (uchar)ShadowRand(8);
    static const short secondary[] = {0, 0, 1, 2, 3, 9, 0x13, 0x1d, 0x23, 0x65, 100};
    d->secondaryMode = Pick(secondary, 11);
    static const uchar channels[] = {0, 0, 3};
    d->modeChangeSwitchChannel = Pick(channels, 3);
    d->unknown15 = (uchar)ShadowRand(2);
    d->dtype = (uchar)ShadowRand(DTYPE_Bot + 1);
    d->dtype2 = ShadowRand(3) == 0 ? 0 : (uchar)ShadowRand(DTYPE_Bot + 1);
    d->initialState = Pick(kStates, sizeof(kStates) / sizeof(kStates[0]));
    static const short seconds[] = {0, 0, 0x56, 0x12, 0x2a, 0xa7, 0x85};
    d->secondState = Pick(seconds, 7);
    static const uint keyModes[] = {0, 5, 100};
    d->diVars.keys.mode = Pick(keyModes, 3);

    static const HASHCODE scripts[] = {HT_Script_00011d, HT_Script_00011f, HT_Script_000124,
                                       HT_Script_000126, HT_Script_DeathScript21F, HT_Script_HASHCODE_BASE,
                                       HT_Script_000125};
    world.diVars.keys.playScript = Pick(scripts, 7);

    static const HASHCODE levels[] = {HT_Level_CastleIndoors1, HT_Level_CastleIndoors2, HT_Level_TowerA,
                                      HT_Level_TowerB, HT_Level_TowerC, HT_Level_HendersonA};
    GameState.CurrentLevelHashcode = Pick(levels, 6);

    // The stats DoModeSettingsNEW reads: some classes with no health
    for (int c = 0; c < NUM_BEHAVIOUR_CLASSES; c++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < DRONE_STATS_FIELDS; k++)
                drone_stats[c][j].field[k] = k == DRONE_STAT_HEALTH && ShadowRand(3) == 0 ? 0 : ShadowRand(256);
}

// ---- the cases

struct Initialiser {
    const char *name;
    unsigned at;
    DroneFunc ours;
};
const Initialiser kInitialisers[] = {
    {"NDrone2_init_DMODE_Defaults", 0x00040600, NDrone2_init_DMODE_Defaults},
    {"NDrone2_init_DMODE_Normal (the jump to Defaults)", 0x000408a0, NDrone2_init_DMODE_Defaults},
    {"NDrone2_init_DMODE_Sniper", 0x000408b0, NDrone2_init_DMODE_Sniper},
    {"NDrone2_init_DMODE_Stealth", 0x00040960, NDrone2_init_DMODE_Stealth},
    {"NDrone2_init_DMODE_RunToPoint", 0x00040980, NDrone2_init_DMODE_RunToPoint},
    {"NDrone2_init_DMODE_HostageKiller", 0x000409b0, NDrone2_init_DMODE_HostageKiller},
    {"NDrone2_init_DMODE_HostageTied", 0x00040a80, NDrone2_init_DMODE_HostageTied},
    {"NDrone2_init_DMODE_JustStand4Demo", 0x00040ae0, NDrone2_init_DMODE_JustStand4Demo},
    {"NDrone2_init_DMODE_Civilian", 0x00040b00, NDrone2_init_DMODE_Civilian},
    {"NDrone2_init_DMODE_CivilianScared", 0x00040c10, NDrone2_init_DMODE_CivilianScared},
    {"NDrone2_init_DMODE_MissionFailer", 0x00040ce0, NDrone2_init_DMODE_MissionFailer},
    {"NDrone2_init_DMODE_Mayhew", 0x00040d10, NDrone2_init_DMODE_Mayhew},
    {"NDrone2_init_DMODE_AlarmRaiser", 0x00040d30, NDrone2_init_DMODE_AlarmRaiser},
    {"NDrone2_init_DMODE_SearchLight", 0x00040d60, NDrone2_init_DMODE_SearchLight},
    {"NDrone2_init_DMODE_Ambush", 0x00040d80, NDrone2_init_DMODE_Ambush},
    {"NDrone2_init_DMODE_Zoe", 0x00040da0, NDrone2_init_DMODE_Zoe},
    {"NDrone2_init_DMODE_PartyGirl", 0x00040dd0, NDrone2_init_DMODE_PartyGirl},
    {"NDrone2_init_DMODE_CivilianGuard", 0x00040e80, NDrone2_init_DMODE_CivilianGuard},
    {"NDrone2_init_DMODE_CivDoorGuard", 0x00040f50, NDrone2_init_DMODE_CivDoorGuard},
    {"NDrone2_init_DMODE_CastleChatGuard1", 0x00041030, NDrone2_init_DMODE_CastleChatGuard1},
    {"NDrone2_init_DMODE_CastleChatGuard2", 0x00041050, NDrone2_init_DMODE_CastleChatGuard2},
    {"NDrone2_init_DMODE_SniperAlert", 0x00041080, NDrone2_init_DMODE_SniperAlert},
    {"NDrone2_init_DMODE_PartyGirlLooker", 0x00041120, NDrone2_init_DMODE_PartyGirlLooker},
    {"NDrone2_initDTYPE_Sniper", 0x000411f0, NDrone2_initDTYPE_Sniper},
    {"NDrone2_initDTYPE_SniperAlert", 0x00041200, NDrone2_initDTYPE_SniperAlert},
};
const int kNumInitialisers = sizeof(kInitialisers) / sizeof(kInitialisers[0]);

const Initialiser *initArg;
uint modeArg;
DIVars_tag *diVarsArg;
uchar dtypeArg;

void TestInitialisers() {
    int before = differ, start = cases;
    for (int trial = 0; trial < 400; trial++) {
        for (int i = 0; i < kNumInitialisers; i++) {
            RandomWorld();
            initArg = &kInitialisers[i];
            Compare(initArg->name, trial,
                    [] {
                        Originals o;
                        ORIGINAL(initArg->at, DroneFunc)(&world.drone);
                        return 0u;
                    },
                    [] {
                        initArg->ours(&world.drone);
                        return 0u;
                    });
        }
    }
    printf("[dronemode] the mode and type initialisers: %d cases, %d differ\n", cases - start, differ - before);
}

void TestSettings() {
    int before = differ, start = cases;
    for (int trial = 0; trial < 12000; trial++) {
        RandomWorld();
        modeArg = ShadowRand(6) == 0 ? 0x24 + ShadowRand(100) : ShadowRand(DMODE_COUNT);
        diVarsArg = ShadowRand(6) == 0 ? NULL : &world.diVars;
        world.drone.gameObj = &world.obj;   // modes 9, 10 and 0x13 write through it unchecked
        Compare("NDrone2_DoModeSettingsOLD", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x000414d0, void(__cdecl *)(Drone_tag *, uint, uint *, DIVars_tag *))(
                        &world.drone, modeArg, world.behaviour, diVarsArg);
                    return 0u;
                },
                [] {
                    NDrone2_DoModeSettingsOLD(&world.drone, modeArg, world.behaviour, diVarsArg);
                    return 0u;
                });

        RandomWorld();
        modeArg = 0x24 + ShadowRand(100);
        diVarsArg = ShadowRand(6) == 0 ? NULL : &world.diVars;
        Compare("NDrone2_DoModeSettingsNEW", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00041230, void(__cdecl *)(Drone_tag *, uint, uint *, DIVars_tag *))(
                        &world.drone, modeArg, world.behaviour, diVarsArg);
                    return 0u;
                },
                [] {
                    NDrone2_DoModeSettingsNEW(&world.drone, modeArg, world.behaviour, diVarsArg);
                    return 0u;
                });

        RandomWorld();
        Compare("NDrone2_GetDroneTypeAttackTypeFriend", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x0003fe30, DroneFunc)(&world.drone);
                    return 0u;
                },
                [] {
                    NDrone2_GetDroneTypeAttackTypeFriend(&world.drone);
                    return 0u;
                });

        RandomWorld();
        dtypeArg = (uchar)ShadowRand(DTYPE_COUNT);
        Compare("NDrone2_DoTypeSettingsOLD", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00040430, void(__cdecl *)(Drone_tag *, uchar))(&world.drone, dtypeArg);
                    return 0u;
                },
                [] {
                    NDrone2_DoTypeSettingsOLD(&world.drone, dtypeArg);
                    return 0u;
                });

        RandomWorld();
        Compare("NDrone2_DefaultInitialState", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x000403b0, uint32_t(__cdecl *)(Drone_tag *))(&world.drone) & 0xffff;
                },
                [] { return (uint32_t)(ushort)NDrone2_DefaultInitialState(&world.drone); });

        RandomWorld();
        Compare("NDrone2_ChangeToAttackMode", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x000397f0, uint32_t(__cdecl *)(Drone_tag *))(&world.drone) & 0xffff;
                },
                [] { return (uint32_t)(ushort)NDrone2_ChangeToAttackMode(&world.drone); });
    }
    printf("[dronemode] mode and type settings: %d cases, %d differ\n", cases - start, differ - before);
}

} // namespace

void DroneModeShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)];
    static DroneStatsRecord stats[NUM_BEHAVIOUR_CLASSES][3];
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(stats, &drone_stats, sizeof(stats));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&drone_stats, sizeof(stats));
    regions.Add(&GameState, sizeof(GameState_t));
    NfWarnMuted++;

    cases = differ = 0;
    TestInitialisers();
    TestSettings();

    NfWarnMuted--;
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&drone_stats, stats, sizeof(stats));
}
