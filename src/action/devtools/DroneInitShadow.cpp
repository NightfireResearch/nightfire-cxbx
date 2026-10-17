// Shadow tests for a drone's creation and initialisation (game/drone/DroneInit.cpp): each original and ours run on
// the same made-up drones, with what needs a level going to logging fakes. Run at start with MenuShadowTests=on
// (MenuProbe.cpp), before any level exists.
//
// Faked, so not tested here: the object makers (control_create_object, Control_CreateObjEx), the drone list's LList_Add,
// the ray under the feet and its hit list, the heap (Mem_Malloc hands out two made-up node lists), the mode and type
// settings (NDrone2_DoModeSettingsOLD/NEW set the fields they would from made-up values; DoTypeSettingsOLD only logs),
// the skin (AnimObjectNew), the anims (DroneAnim_SetAnimD), the body glow, the bot code, the player stats, and
// NDrone2_PostLoad_Init's opponent, path and state machine. Everything else runs for real: control_init_object,
// the vector helpers, Float_FRand (both sides start from the same Rand_Random state, compared afterwards), the
// behaviour words and AINetwork_ResetRoute.

#include "DroneInitShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/AINetwork.h"
#include "../engine/Anim.h"
#include "../engine/Collide.h"
#include "../engine/viewer.h"
#include "../game/drone/BOT.h"
#include "../game/drone/DroneInit.h"
#include "../game/drone/DroneTuning.h"
#include "../game/drone/NDrone2.h"
#include "../game/mp/multiplayer.h"
#include "../game/sp/SwitchChannels.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords ((uint32_t *)0x0018cdf8)

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int kDrones = 4;
const int kNodes = 40;

struct World {
    obj_tag objs[kDrones];
    Drone_tag drones[kDrones];
    AnimState anims[kDrones];
    obj_tag spawners[3];
    cel_tag cel;
    sAnimScript_tag script;
    HITDATA_tag rayHit;
    obj_tag bodyGlow;
    char glist[16];
    ushort nodes[2][kNodes];
    BOT_vars_t botVars;

    // What the fakes answer, and their state
    bool rayHits;
    int mallocs;
    int animObjectNew;
    obj_tag *opponent;
    uchar behaviourClass, side, modeFromOldTable, dtype;
    short initialState;
    float floatAcceleration;
    uint behaviour[3];
    obj_tag *createdObj;     // what Control_CreateObjEx answers
};
World world;

ShadowRegions regions;
int cases, differ;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "droneinit", what, trial, theirs, ours))
        differ++;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

void LogVector(const char *name, const _VECTOR *v) {
    ShadowLog("%s=%08x,%08x,%08x", name, Bits(v->x), Bits(v->y), Bits(v->z));
}

int WhoDrone(const Drone_tag *drone) {
    for (int i = 0; i < kDrones; i++)
        if (drone == &world.drones[i])
            return i;
    return drone == NULL ? -1 : 999;
}

int WhoObj(const obj_tag *obj) {
    for (int i = 0; i < kDrones; i++)
        if (obj == &world.objs[i])
            return i;
    for (int i = 0; i < 3; i++)
        if (obj == &world.spawners[i])
            return 10 + i;
    return obj == NULL ? -1 : 999;
}

// ---- the fakes

bool __cdecl FakeRay(_VECTOR *start, _VECTOR *end, cel_tag *cel, obj_tag *obj, obj_tag *p5, HITDATA_tag **hits, char p7,
                     uint flags, ushort p9) {
    LogVector("ray", start);
    LogVector("to", end);
    ShadowLog("(%d,%d,%p,%d,%x,%u)", cel == &world.cel, WhoObj(obj), p5, p7, flags, p9);
    *hits = world.rayHits ? &world.rayHit : NULL;
    return world.rayHits;
}

void __cdecl FakeFreeHits(HITDATA_tag **hits) {
    ShadowLog("freehits(%d)", *hits == &world.rayHit);
    *hits = NULL;
}

void *__cdecl FakeMalloc(size_t size, MallocFlags flags, uint32_t alignment) {
    ShadowLog("malloc(%u,%x,%u)", (unsigned)size, (unsigned)(ushort)flags, alignment);
    // Dirty, to see the clear
    void *block = world.nodes[world.mallocs++ & 1];
    memset(block, 0xcd, sizeof(world.nodes[0]));
    return block;
}

void __cdecl FakeDoModeSettings(Drone_tag *drone, uint mode, uint *behaviour, DIVars_tag *diVars) {
    ShadowLog("modesettings(%d,%u,%d,%d)", WhoDrone(drone), mode, behaviour == drone->diVars.keys.behaviour,
              diVars == &drone->diVars);
    memcpy(drone->behaviour1, world.behaviour, sizeof(world.behaviour));
    drone->currentBehaviour = drone->behaviour1;
    drone->behaviourClass = world.behaviourClass;
    drone->side = world.side;
    drone->modeFromOldTable = world.modeFromOldTable;
    drone->dtype = world.dtype;
    drone->initialState = world.initialState;
    drone->floatAcceleration = world.floatAcceleration;
}

void __cdecl FakeDoModeSettingsOLD(Drone_tag *drone, uint mode, uint *behaviour, DIVars_tag *diVars) {
    ShadowLog("old");
    FakeDoModeSettings(drone, mode, behaviour, diVars);
}

void __cdecl FakeDoModeSettingsNEW(Drone_tag *drone, uint mode, uint *behaviour, DIVars_tag *diVars) {
    ShadowLog("new");
    FakeDoModeSettings(drone, mode, behaviour, diVars);
}

void __cdecl FakeDoTypeSettingsOLD(Drone_tag *drone, uchar dtype) {
    ShadowLog("typesettings(%d,%u)", WhoDrone(drone), dtype);
}

void __cdecl FakeSetAnimD(int state, ushort variant, Drone_tag *drone) {
    ShadowLog("setanimd(%x,%u,%d)", state, variant, WhoDrone(drone));
}

int __cdecl FakeAnimObjectNew(obj_tag *obj, HASHCODE skin) {
    ShadowLog("animobjectnew(%d,%x)", WhoObj(obj), skin);
    return world.animObjectNew;
}

obj_tag *__cdecl FakeBodyGlow(obj_tag *obj) {
    ShadowLog("bodyglow(%d)", WhoObj(obj));
    return &world.bodyGlow;
}

void __cdecl FakeBotSetGender(Drone_tag *drone) {
    ShadowLog("setgender(%d)", WhoDrone(drone));
}

void __cdecl FakeBotPostLoadInit(Drone_tag *drone) {
    ShadowLog("botpostload(%d,%08x)", WhoDrone(drone), Bits(drone->health));
}

void __cdecl FakeEnemySpawned(void) {
    ShadowLog("enemyspawned");
}

void __cdecl FakeSpawnerInit(obj_tag *spawner) {
    ShadowLog("spawnerinit(%d)", WhoObj(spawner));
}

obj_tag *__cdecl FakeCreateObjEx(ushort size, _VECTOR *pos, _VECTOR *rot, _MATRIX *mtx, celglist_tag *glist,
                                 obj_tag *parent, char p7, ushort p8, float scale, ushort p10, uchar r, uchar g,
                                 uchar b) {
    LogVector("createex", pos);
    LogVector("rot", rot);
    ShadowLog("(%u,%p,%p,%p,%d,%u,%08x,%u,%u,%u,%u)", size, mtx, glist, parent, p7, p8, Bits(scale), p10, r, g, b);
    return world.createdObj;
}

int __cdecl FakeFindOpponent(Drone_tag *drone) {
    ShadowLog("findopponent(%d)", WhoDrone(drone));
    drone->opponent = world.opponent;
    return 1;
}

void __cdecl FakeGetOpponentInfo(Drone_tag *drone, obj_tag *obj) {
    ShadowLog("opponentinfo(%d,%d)", WhoDrone(drone), WhoObj(obj));
}

AIPath_tag *__cdecl FakeNavPath(_VECTOR *pos, cel_tag *cel, char moveTest) {
    LogVector("navpath", pos);
    ShadowLog("(%d,%d)", cel == &world.cel, moveTest);
    return (AIPath_tag *)&world.glist;
}

bool __cdecl FakeSMInit(obj_tag *obj) {
    ShadowLog("sminit(%d)", WhoObj(obj));
    return true;
}

uint __cdecl FakeChangeWeapon(Drone_tag *drone, short weapon) {
    ShadowLog("changeweapon(%d,%d)", WhoDrone(drone), weapon);
    return 0;
}

void InstallFakes() {
    ShadowFakes_Install();
    ShadowHook_Install(0x0002c4e0, (const void *)&FakeRay);
    ShadowHook_Install(0x000294d0, (const void *)&FakeFreeHits);
    ShadowHook_Install(0x00070ae0, (const void *)&FakeMalloc);
    ShadowHook_Install(0x000414d0, (const void *)&FakeDoModeSettingsOLD);
    ShadowHook_Install(0x00041230, (const void *)&FakeDoModeSettingsNEW);
    ShadowHook_Install(0x00040430, (const void *)&FakeDoTypeSettingsOLD);
    ShadowHook_Install(0x00035e10, (const void *)&FakeSetAnimD);
    ShadowHook_Install(0x00011440, (const void *)&FakeAnimObjectNew);
    ShadowHook_Install(0x000ce4b0, (const void *)&FakeBodyGlow);
    ShadowHook_Install(0x0001ad70, (const void *)&FakeBotSetGender);
    ShadowHook_Install(0x0001b8e0, (const void *)&FakeBotPostLoadInit);
    ShadowHook_Install(0x000b04f0, (const void *)&FakeEnemySpawned);
    ShadowHook_Install(0x00030670, (const void *)&FakeSpawnerInit);
    ShadowHook_Install(0x0002e320, (const void *)&FakeCreateObjEx);
    ShadowHook_Install(0x00037c10, (const void *)&FakeFindOpponent);
    ShadowHook_Install(0x00031260, (const void *)&FakeGetOpponentInfo);
    ShadowHook_Install(0x00049650, (const void *)&FakeNavPath);
    ShadowHook_Install(0x0004e2a0, (const void *)&FakeSMInit);
    ShadowHook_Install(0x0001f320, (const void *)&FakeChangeWeapon);
}

// Every original of this file at once, so they reach each other rather than ours
struct Originals {
    XbeOriginalScope createObj{0x0003f940}, postLoad{0x00042a90}, groups{0x00036e90}, enable{0x000376b0},
        collision{0x0003f9d0}, attacking{0x0003ace0}, defaultInit{0x000417f0};
};

// ---- the made-up world

uint Pick(const uint *values, int n) {
    return values[ShadowRand(n)];
}

float PickFloat(const float *values, int n) {
    return values[ShadowRand(n)];
}

uint Random32() {
    return ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

// The skins NDrone2_DefaultInit tells apart, and some it does not
const uint kSkins[] = {
    0x05000036, 0x05000037, 0x05000038, 0x05000039, 0x0500003a, 0x0500002e, 0x0500002f, 0x05000030, 0x05000004,
    0x05000006, 0x05000008, 0x0500004c, 0x05000025, 0x05000087, 0x05000090, 0x05000093, 0x05000054, 0x0500006d,
    0x0500006e, 0x0500007e, 0x0500008c, 0x050000b1, 0x050000b2, 0x0500001b, 0x0500004f, 0x0500000e, 0x0500000f,
    0x05000015, 0x05000020, 0x05000022, 0x05000023, 0x05000024, 0x0500003b, 0x0500003c, 0x05000019, 0x05000021,
    0x0500002b, 0x0500002d, 0x05000034, 0x0500005e, 0x05000017, 0x05000018, 0x0500001e, 0x05000035, 0x05000012,
    0x05000013, 0x05000014, 0x05000016, 0x0500008f, 0x050000ac, 0x05000026, 0x05000066, 0x05000010, 0x05000011,
    0x05000052, 0x05000053, 0x05000027, 0x05000028, 0x05000029, 0x0500002a, 0x05000031, 0x05000032, 0x05000033,
    0x05000078, 0x05000094, 0x050000ba, 0x05000001, 0x05000003, 0x050000bb, 0x05000005, 0x06000000,
};
const int kNumSkins = sizeof(kSkins) / sizeof(kSkins[0]);

const uint kWeapons[] = {0, 0, 1, 2, 6, 0xa, 0xb, 0xe, 0x10, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x12, 0x14, 0x15,
                         0x16, 0x18, 0x19, 0x1a, 0x1b, 0x1e, 0x1f, 0x24, 0x25, 0x1c, 0x1d, 0x2a, 0x2b, 0x2c, 0x2d,
                         0x30, 0x32, 0x33, 0x6a, 0x6e, 0x6f, 0x3, 0x10016, 0xffff0002};
const int kNumWeapons = sizeof(kWeapons) / sizeof(kWeapons[0]);

const uint kLevels[] = {HT_Level_EvilBase, HT_Level_EvilBase, HT_Level_Tower2A, HT_Level_EvilBaseC, HT_Level_TowerA,
                        HT_Level_TowerB, HT_Level_TowerC, HT_Level_CastleExterior, HT_Level_CastleCourtyard,
                        HT_Level_HendersonA};

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    AnimState *anim = &world.anims[i];
    uint8_t *bytes = (uint8_t *)d;
    for (size_t k = 0; k < sizeof(*d); k++)
        bytes[k] = (uint8_t)ShadowRand(256);
    memset(o, 0, sizeof(*o));
    memset(anim, 0, sizeof(*anim));

    o->objectType = OBJECTTYPE_DRONE;
    o->extraObjectData = d;
    o->animState = anim;
    o->inCel = ShadowRand(8) == 0 ? NULL : &world.cel;
    o->position.x = ShadowRandFloat(-50.0f, 50.0f);
    o->position.y = ShadowRandFloat(-50.0f, 50.0f);
    o->position.z = ShadowRandFloat(-50.0f, 50.0f);
    o->rotation.y = ShadowRandFloat(-3.0f, 3.0f);
    o->effectFlags = (int)Random32();
    o->flags = (char)ShadowRand(256);
    anim->currentWeaponId = (char)ShadowRand(256);
    anim->animFlags = (char)ShadowRand(256);

    d->prev = NULL;
    d->next = i + 1 < kDrones ? &world.drones[i + 1] : NULL;
    d->gameObj = o;
    d->animScript = ShadowRand(3) == 0 ? NULL : &world.script;
    d->botVars = &world.botVars;
    static const uint groups[] = {0, 0, 1, 2, 3, 30};
    d->group = (int)Pick(groups, 6);
    static const float ranges[] = {0.0f, -0.0f, 5.0f, 24.0f, 1000.0f, NAN};
    d->viewRange = PickFloat(ranges, 6);
    d->combatRanges[4] = PickFloat(ranges, 6);
    d->alertedFrame = ShadowRand(2) ? 0 : Random32();
    d->deathScript = (HASHCODE)(ShadowRand(3) ? 0 : ShadowRand(2) ? 0x0600021f : 0x06000100);
    d->flies = (uchar)(ShadowRand(4) == 0);
    static const uint classes[] = {0, 1, 0x13, 0x13, 0x18};
    d->skinClass = (short)Pick(classes, 5);
    static const uint dtypes[] = {0, 2, 0x17, 0x17, 0x1d};
    d->dtype = (uchar)Pick(dtypes, 5);

    // The creation vars, as NDrone2_DefaultInit reads them
    DIVars_tag *v = &d->diVars;
    v->gameObj = ShadowRand(16) == 0 ? NULL : o;
    v->position = o->position;
    v->rotation = o->rotation;
    v->position.y += 1.5f;
    v->glist = ShadowRand(3) == 0 ? (celglist_tag *)world.glist : NULL;
    DroneKeys *k = &v->keys;
    k->skin = (HASHCODE)kSkins[ShadowRand(kNumSkins)];
    static const uint scripts[] = {0x06000000, 0x06000000, 0x06000892, 0x0600089c, 0x06000123};
    k->playScript = (HASHCODE)Pick(scripts, 5);
    k->waitSwitchChannel = Random32();
    k->modeChangeSwitchChannel = Random32();
    k->deathSwitchChannel = Random32();
    k->group = ShadowRand(5);
    static const uint modes[] = {0, 5, 0x23, 0x24, 100, 0x80000000};
    k->mode = Pick(modes, 6);
    k->key7 = Random32();
    k->secondaryMode = Random32();
    k->weapon = kWeapons[ShadowRand(kNumWeapons)];
    k->drawWeapon = Random32();
    k->grenades = (ShadowRand(9) << 4) | ShadowRand(16) | (ShadowRand(4) == 0 ? 0x100000 : 0);
    static const uint deathScripts[] = {0, 0, 0x0600021f, 0x06000300};
    k->deathScript = (HASHCODE)Pick(deathScripts, 4);
    k->combatRange = ShadowRand(7);
}

void RandomWorld() {
    for (int i = 0; i < kDrones; i++)
        RandomDrone(i);
    for (int i = 0; i < 3; i++) {
        memset(&world.spawners[i], 0, sizeof(obj_tag));
        world.spawners[i].objectType = OBJECTTYPE_DRONE_SPAWNER;
    }
    memset(&world.cel, 0, sizeof(world.cel));
    memset(&world.script, 0, sizeof(world.script));
    world.script.speed = 0.5f;
    memset(&world.rayHit, 0, sizeof(world.rayHit));
    world.rayHit.hitPosition.x = ShadowRandFloat(-50.0f, 50.0f);
    world.rayHit.hitPosition.y = ShadowRand(10) == 0 ? NAN : ShadowRandFloat(-50.0f, 50.0f);
    world.rayHit.hitPosition.z = ShadowRandFloat(-50.0f, 50.0f);
    memset(&world.botVars, 0, sizeof(world.botVars));
    world.botVars.weapon = (uchar)ShadowRand(256);
    memset(world.nodes, 0xcd, sizeof(world.nodes));

    world.rayHits = ShadowRand(3) != 0;
    world.mallocs = 0;
    world.animObjectNew = (int)ShadowRand(2);
    world.opponent = ShadowRand(2) ? &world.objs[1] : NULL;
    static const uint behaviourClasses[] = {0, 9, 0xc, 0xc, 0x10};
    world.behaviourClass = (uchar)Pick(behaviourClasses, 5);
    world.side = (uchar)ShadowRand(4);
    world.modeFromOldTable = (uchar)ShadowRand(2);
    static const uint dtypes[] = {0, 1, 2, 0xc, 0xd, 0x11, 0x17, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e};
    world.dtype = (uchar)Pick(dtypes, 13);
    static const uint states[] = {DSTATE_Attack, DSTATE_CivilianScared, DSTATE_Idle, 0};
    world.initialState = (short)Pick(states, 4);
    static const float accelerations[] = {0.0f, 0.0f, -0.0f, 0.5f, NAN};
    world.floatAcceleration = PickFloat(accelerations, 5);
    for (int w = 0; w < 3; w++)
        world.behaviour[w] = Random32();
    world.createdObj = ShadowRand(5) == 0 ? NULL : &world.objs[3];
    if (world.createdObj != NULL && ShadowRand(4) == 0)
        world.objs[3].extraObjectData = NULL;

    // Some drones in a group already (NPCGlobals.groups is not cleared first)
    memset(&NPCGlobals, 0, sizeof(NPCGlobals));
    NPCGlobals.NDrone2List = ShadowRand(8) == 0 ? NULL : &world.drones[0];
    for (int g = 0; g < 4; g++) {
        NPCGlobals.groups[g] = ShadowRand(3) == 0 ? &world.drones[ShadowRand(kDrones)] : NULL;
        NPCGlobals.groupCounts[g] = (ushort)(ShadowRand(3) == 0 ? 0xffff : ShadowRand(3));
        NPCGlobals.spawners[g] = ShadowRand(2) ? &world.spawners[ShadowRand(3)] : NULL;
    }
    NPCGlobals.spawners[30] = ShadowRand(2) ? &world.spawners[0] : NULL;

    GameState.NumFramesUnpaused = 1000 + ShadowRand(100);
    GameState.CurrentLevelHashcode = (HASHCODE)kLevels[ShadowRand(10)];
    GameState.difficultyModifier = 1 + ShadowRand(3);
    MPSettings.maybeDroneAIEnabled = ShadowRand(6) == 0;
    // Without bots NDrone2_DefaultInit reads the anim state untested
    if (MPSettings.maybeDroneAIEnabled && ShadowRand(3) == 0)
        world.objs[0].animState = NULL;
    AIPath_MaxNodes = ShadowRand(4) == 0 ? 0 : ShadowRand(kNodes) + 1;
    for (int c = 0; c < 256; c++)
        switch_channels[c] = (char)ShadowRand(3);
    FRAME_RATE_DIV = ShadowRand(2) ? 1.0f : 1.2f;
    static const float mods[] = {1.0f, 1.5f, 2.0f, 0.5f, 0.0f};
    DroneCaptain_Mod_BulletDamage = PickFloat(mods, 4);
    DroneCaptain_Mod_BulletAccuracy = PickFloat(mods, 5);
    DroneCaptain_Mod_Health = PickFloat(mods, 4);
    RandWords[0] = Random32();
    RandWords[1] = Random32();
    ShadowFakes.createFails = ShadowRand(5) == 0;
}

// ---- the cases

char charArg;
DIVars_tag *diVarsArg;

void TestSmall() {
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        Compare("DroneFunc_SetAsAttacking", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x0003ace0, void(__cdecl *)(Drone_tag *))(&world.drones[0]);
                    return 0u;
                },
                [] {
                    DroneFunc_SetAsAttacking(&world.drones[0]);
                    return 0u;
                });
        charArg = (char)(ShadowRand(3) == 0 ? 0 : ShadowRand(256));
        Compare("NDrone2_Enable", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x000376b0, void(__cdecl *)(char, Drone_tag *))(charArg, &world.drones[0]);
                    return 0u;
                },
                [] {
                    NDrone2_Enable(charArg, &world.drones[0]);
                    return 0u;
                });
        Compare("DroneInit_Collision", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x0003f9d0, void(__cdecl *)(Drone_tag *))(&world.drones[0]);
                    return 0u;
                },
                [] {
                    DroneInit_Collision(&world.drones[0]);
                    return 0u;
                });
        Compare("NDrone2_SetupGroups", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x00036e90, uint32_t(__cdecl *)(void))() & 0xff;
                },
                [] { return (uint32_t)NDrone2_SetupGroups(); });
        diVarsArg = &world.drones[0].diVars;
        Compare("NDrone2_CreateObj", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x0003f940, uint32_t(__cdecl *)(DIVars_tag *))(diVarsArg);
                },
                [] { return (uint32_t)(uintptr_t)NDrone2_CreateObj(diVarsArg); });
    }
}

void TestDefaultInit() {
    for (int trial = 0; trial < 30000; trial++) {
        RandomWorld();
        diVarsArg = &world.drones[0].diVars;
        if (diVarsArg->gameObj == NULL)
            diVarsArg->gameObj = &world.objs[0];
        Compare("NDrone2_DefaultInit", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x000417f0, uint32_t(__cdecl *)(DIVars_tag *))(diVarsArg);
                },
                [] { return (uint32_t)(uintptr_t)NDrone2_DefaultInit(diVarsArg); });
    }
}

void TestPostLoad() {
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        world.drones[0].diVars.gameObj = &world.objs[0];
        Compare("NDrone2_PostLoad_Init", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00042a90, void(__cdecl *)(obj_tag *))(&world.objs[0]);
                    return 0u;
                },
                [] {
                    NDrone2_PostLoad_Init(&world.objs[0]);
                    return 0u;
                });
    }
}

} // namespace

void DroneInitShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], mpSettings[sizeof(MPSettings_t)], npc[sizeof(NPCGlobals_t)];
    static char channels[256];
    static uint32_t rand[2];
    uint32_t maxNodes = AIPath_MaxNodes;
    float frameDiv = FRAME_RATE_DIV;
    float modDamage = DroneCaptain_Mod_BulletDamage, modAccuracy = DroneCaptain_Mod_BulletAccuracy,
          modHealth = DroneCaptain_Mod_Health;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(mpSettings, &MPSettings, sizeof(mpSettings));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(channels, switch_channels, sizeof(channels));
    memcpy(rand, RandWords, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(RandWords, 8);
    ShadowFakes_AddRegions(regions);

    InstallFakes();
    NfWarnMuted++;
    cases = differ = 0;

    TestSmall();
    int smallCases = cases, smallDiffer = differ;
    TestDefaultInit();
    int initCases = cases, initDiffer = differ;
    TestPostLoad();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&MPSettings, mpSettings, sizeof(mpSettings));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(switch_channels, channels, sizeof(channels));
    memcpy(RandWords, rand, sizeof(rand));
    AIPath_MaxNodes = maxNodes;
    FRAME_RATE_DIV = frameDiv;
    DroneCaptain_Mod_BulletDamage = modDamage;
    DroneCaptain_Mod_BulletAccuracy = modAccuracy;
    DroneCaptain_Mod_Health = modHealth;

    printf("[droneinit] create, groups, enable, floor and attacking: %d cases, %d differ\n", smallCases, smallDiffer);
    printf("[droneinit] NDrone2_DefaultInit: %d cases, %d differ\n", initCases - smallCases, initDiffer - smallDiffer);
    printf("[droneinit] NDrone2_PostLoad_Init: %d cases, %d differ\n", cases - initCases, differ - initDiffer);
}
