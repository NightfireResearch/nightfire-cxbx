// Shadow tests for the multiplayer bots' own code (game/drone/BOTWEAP.cpp, BOTSTATE.cpp, BOT.cpp): each original and
// ours run on the same made-up bots - drones with their BOT_vars_t, held weapons, ammo, players and the match's
// settings - and what each returns, writes and calls is compared. What needs a level (the hashtable, the drones'
// weapons and animations, the state machine) goes to logging fakes. Run at start with MenuShadowTests=on
// (MenuProbe.cpp); NIGHTFIRE_SHADOW_ONLY=botport runs it alone.
//
// The original side runs with every original of the group in place, so the originals reach each other; ours reach
// ours. BOT_startWeapon is ours: the original side is given it at 0x001639c8 and what it leaves there is read back.

#include "BotPortShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../game/weapon_stats.h"
#include "../game/drone/BOT.h"
#include "../game/drone/BOTWEAP.h"
#include "../game/drone/Drone.h"
#include "../game/mp/multiplayer.h"

#include <stdio.h>
#include <string.h>

namespace {

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define GameStartWeapon I16_AT(0x001639c8)

const int BOTS = 3;

struct World {
    obj_tag objs[BOTS];
    Drone_tag drones[BOTS];
    AnimState anims[BOTS];
    BOT_vars_t vars[BOTS];
    ushort list[0x60];
    int count;
    uint8_t item[16];           // what hashtable_getitem finds
    DCVars_tag dcv;
};
World world;

ShadowRegions regions;
int cases, differ;

uint32_t R(uint32_t n) { return ShadowRand(n); }

int Who(const void *p) {
    for (int i = 0; i < BOTS; i++) {
        if (p == &world.drones[i] || p == &world.objs[i] || p == &world.vars[i])
            return i;
    }
    if (p == NULL)
        return -1;
    if (p == (void *)-1)
        return -2;
    return 99;
}

// ---- the fakes

void *__cdecl FakeGetItem(HASHCODE hash) {
    ShadowLog("getitem(%x)", hash);
    return hash % 3 == 0 ? NULL : world.item;
}

bool __cdecl FakeDCVfromOBJ(obj_tag *obj, DCVars_tag *dcv) {
    ShadowLog("dcv(%d)", Who(obj));
    memset(dcv, 0, sizeof(*dcv));
    dcv->gameObj = obj;
    dcv->drone = (Drone_tag *)obj->extraObjectData;
    return true;
}

void __cdecl FakeChangeWeapon(DCVars_tag *dcv, int weapon) {
    ShadowLog("droneweapon(%d,%d)", Who(dcv->drone), weapon);
}

uint32_t __cdecl FakeIsPreferred(BOT_vars_t *bot, uint weapon) {
    ShadowLog("preferred(%d,%u)", Who(bot), weapon);
    return (weapon * 7 + bot->skin) % 3 == 0;
}

uint32_t __cdecl FakeOpponentIsMissile(Drone_tag *drone, obj_tag *obj) {
    ShadowLog("missile(%d,%d)", Who(drone), Who(obj));
    return drone->opponent != NULL && ((uintptr_t)drone->opponent & 4) != 0;
}

void InstallFakes() {
    ShadowHook_RemoveAll();
    ShadowHook_Install(0x0006b500, (const void *)&FakeGetItem);
    ShadowHook_Install(0x0002fac0, (const void *)&FakeDCVfromOBJ);
    ShadowHook_Install(0x00066b20, (const void *)&FakeChangeWeapon);
    ShadowHook_Install(0x0001c0d0, (const void *)&FakeIsPreferred);
    ShadowHook_Install(0x0001c600, (const void *)&FakeOpponentIsMissile);
}

// ---- the made-up world

// Weapon ids: mostly the ones the bot code walks, some above
short RandomWeapon() {
    if (R(10) == 0)
        return (short)(0x53 + R(0x1f));
    if (R(6) == 0) {
        static const short edges[] = {0, 1, 0x2a, 0x2c, 0x2d, 0x33, 0x34, 0x3b, 0x41, 0x45, 0x52, 0x53};
        return edges[R(COUNT(edges))];
    }
    return (short)R(0x53);
}

void RandomBot(int i) {
    obj_tag *obj = &world.objs[i];
    Drone_tag *drone = &world.drones[i];
    AnimState *anim = &world.anims[i];
    BOT_vars_t *vars = &world.vars[i];
    memset(obj, 0, sizeof(*obj));
    memset(drone, 0, sizeof(*drone));
    memset(anim, 0, sizeof(*anim));
    memset(vars, 0, sizeof(*vars));

    obj->objectType = OBJECTTYPE_DRONE;
    obj->extraObjectData = drone;
    obj->animState = anim;
    anim->currentWeaponId = (char)RandomWeapon();
    drone->gameObj = obj;
    drone->botVars = vars;
    drone->clip = (short)R(40);
    drone->clipSize = (short)R(40);
    drone->animSet = (short)R(30);
    drone->opponent = R(3) == 0 ? NULL : &world.objs[R(BOTS)];
    if (drone->opponent != NULL && R(2))
        drone->opponent = (obj_tag *)((uint8_t *)drone->opponent + 4);

    vars->drone = drone;
    vars->playerIndex = (short)(NUM_PLAYERS + i);
    vars->botIndex = (short)i;
    vars->weapon = (uchar)RandomWeapon();
    vars->skin = (uchar)(R(4) == 0 ? 0x1a : R(29));
    vars->stats.personality = (uchar)R(9);
    vars->ammoRegenTimer = R(1000);
    for (int w = 0; w < 114; w++) {
        BOT_weapon_t *weapon = &vars->weapons[w];
        weapon->zoomRoot = ShadowRandFloat(0.0f, 3.0f);
        weapon->rounds = R(3) == 0 ? 0 : (short)R(50) - 3;
        weapon->held = (uchar)(R(3) == 0 ? R(3) : 0);
        weapon->unknown7[0] = (uchar)R(256);
        weapon->unknown7[1] = (uchar)R(256);
        weapon->unknown7[2] = (uchar)R(256);
    }
    for (int a = 0; a < 33; a++) {
        short most = ammo_data[a].maybeMaxNumPerPlayer;
        switch (R(5)) {
            case 0: vars->reserveAmmo[a] = 0; break;
            case 1: vars->reserveAmmo[a] = most; break;
            case 2: vars->reserveAmmo[a] = (short)(most - 1); break;
            default: vars->reserveAmmo[a] = (short)R(most + 5); break;
        }
    }
}

void RandomWorld() {
    for (int i = 0; i < BOTS; i++)
        RandomBot(i);
    for (int p = 0; p < NUM_AGENTS; p++)
        MPSettings.Player[p].TeamId = (MPTeam)R(3);
    static const MultiplayerGameMode modes[] = {GM_ARENA, GM_PROTECTION, GM_DEMOLITION, GM_CTF, GM_ASSASSIN};
    MPSettings.GameMode = modes[R(COUNT(modes))];
    MPSettings.weaponSet = (WeaponSet)R(PICKUP_SETS);
    static const short starts[] = {6, 1, 2, 0x10, 0x2d, 0x45};
    BOT_startWeapon = R(2) ? weapon_data[PickupMatrix[R(PICKUP_SETS)][0]].weaponBaseNum : starts[R(COUNT(starts))];
    GameState.NumFramesUnpaused = R(4) == 0 ? 0 : R(100000);
    memset(world.list, 0x5a, sizeof(world.list));
    world.count = (int)R(1000);
}

// ---- running both sides

// Every original of the group at once, so that they reach each other rather than ours
struct OriginalBotWeapons {
    XbeOriginalScope reset{0x0001e540}, reload{0x0001e5b0}, ammoInGun{0x0001e4d0}, equipWeapon{0x0001e6f0},
        equipAmmo{0x0001e820}, checkLoaded{0x0001e920}, hasWeapon{0x0001e980}, decrRounds{0x0001e9a0},
        ammoAmount{0x0001ea60}, clipSize{0x0001ead0}, isValid{0x0001eaf0}, priority{0x0001eb70},
        animSet{0x0001edb0}, shouldChange{0x0001ee60}, reloadAnim{0x0001eeb0}, hasAmmo{0x0001ef30},
        init{0x0001ef90}, change{0x0001f320}, hasLoaded{0x0001f410}, explosive{0x0001f450}, list{0x0001f5c0},
        tooClose{0x0001f740};
};

uint32_t (*caseRun)(bool original);

uint32_t Theirs() {
    GameStartWeapon = BOT_startWeapon;
    uint32_t result;
    {
        OriginalBotWeapons originals;
        result = caseRun(true);
    }
    BOT_startWeapon = GameStartWeapon;
    return result;
}

uint32_t Ours() {
    return caseRun(false);
}

// A case draws its arguments from this, the same for both sides
uint32_t caseGen;
uint32_t A(uint32_t n) {
    caseGen = caseGen * 1664525u + 1013904223u;
    uint32_t v = caseGen ^ (caseGen >> 16);
    return n ? v % n : v;
}

short ArgWeapon() {
    if (A(10) == 0)
        return (short)(0x53 + A(0x1f));
    return (short)A(0x54);
}

short ArgCount() {
    static const short edges[] = {0, 1, 2, -1, 999, 30, 100, 0x7fff, -0x8000};
    if (A(3) == 0)
        return edges[A(COUNT(edges))];
    return (short)A(60);
}

float ArgDistance() {
    static const float edges[] = {0.0f, -0.0f, 1.0f, 2.0f, 4.0f, 5.0f, 8.0f};
    if (A(3) == 0)
        return edges[A(COUNT(edges))];
    return (float)A(1200) / 100.0f;
}

Drone_tag *ArgDrone() {
    return &world.drones[A(BOTS)];
}

#define CDECL(at, ours, type, ...) \
    (original ? (uint32_t)ORIGINAL(at, type)(__VA_ARGS__) : (uint32_t)ours(__VA_ARGS__))
#define CDECL_VOID(at, ours, type, ...) \
    do { if (original) ORIGINAL(at, type)(__VA_ARGS__); else ours(__VA_ARGS__); } while (0)

typedef uint32_t (__cdecl *DroneWeapon)(Drone_tag *, short);
typedef uint32_t (__cdecl *DroneWeaponCount)(Drone_tag *, short, short);
typedef uint32_t (__cdecl *Weapon)(short);

// ---- BOTWEAP

uint32_t RunReset(bool original) {
    obj_tag *obj = ArgDrone()->gameObj;
    CDECL_VOID(0x0001e540, BOTWEAP_resetWeapons, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunReload(bool original) {
    obj_tag *obj = ArgDrone()->gameObj;
    char check = (char)(A(3) == 0);
    return CDECL(0x0001e5b0, BOTWEAP_reload, uint32_t (__cdecl *)(obj_tag *, char), obj, check) & 0xff;
}

uint32_t RunAmmoInGun(bool original) {
    Drone_tag *drone = ArgDrone();
    short weapon = A(4) == 0 ? BOT_startWeapon : ArgWeapon();
    return CDECL(0x0001e4d0, BOTWEAP_AmmoInGun, DroneWeapon, drone, weapon) & 0xffff;
}

uint32_t RunEquipWeapon(bool original) {
    Drone_tag *drone = ArgDrone();
    short weapon = ArgWeapon(), count = ArgCount();
    return CDECL(0x0001e6f0, BOTWEAP_EquipWeapon, DroneWeaponCount, drone, weapon, count) & 0xff;
}

uint32_t RunEquipAmmo(bool original) {
    Drone_tag *drone = ArgDrone();
    short weapon = ArgWeapon(), count = ArgCount();
    return CDECL(0x0001e820, BOTWEAP_EquipAmmo, DroneWeaponCount, drone, weapon, count) & 0xff;
}

uint32_t RunCheckLoaded(bool original) {
    Drone_tag *drone = ArgDrone();
    CDECL_VOID(0x0001e920, BOTWEAP_CheckWeaponsLoaded, void (__cdecl *)(Drone_tag *), drone);
    return 0;
}

uint32_t RunHasWeapon(bool original) {
    Drone_tag *drone = ArgDrone();
    short weapon = ArgWeapon();
    return CDECL(0x0001e980, BOTWEAP_hasWeapon, DroneWeapon, drone, weapon) & 0xff;
}

uint32_t RunDecrRounds(bool original) {
    Drone_tag *drone = ArgDrone();
    if (A(3) == 0)
        drone->botVars->weapon = (uchar)BOT_startWeapon;
    short rounds = A(4) == 0 ? 1000 : (short)A(4);
    return CDECL(0x0001e9a0, BOTWEAP_decrRounds, DroneWeapon, drone, rounds) & 0xff;
}

uint32_t RunAmmoAmount(bool original) {
    Drone_tag *drone = ArgDrone();
    short weapon = ArgWeapon();
    return CDECL(0x0001ea60, BOTWEAP_getWeaponAmmoAmount, DroneWeapon, drone, weapon);
}

uint32_t RunClipSize(bool original) {
    Drone_tag *drone = ArgDrone();
    short weapon = ArgWeapon();
    return CDECL(0x0001ead0, BOTWEAP_getWeaponClipSize, DroneWeapon, drone, weapon);
}

uint32_t RunIsValid(bool original) {
    short weapon = (short)A(0x60) - 2;
    return CDECL(0x0001eaf0, BOTWEAP_isValidWeapon, Weapon, weapon) & 0xff;
}

uint32_t RunPriority(bool original) {
    short weapon = (short)A(0x60) - 2;
    return CDECL(0x0001eb70, BOTWEAP_getWeaponPriority, Weapon, weapon);
}

uint32_t RunAnimSet(bool original) {
    short weapon = (short)A(0x60) - 2;
    return CDECL(0x0001edb0, BOTWEAP_getAnimSet, Weapon, weapon) & 0xffff;
}

uint32_t RunShouldChange(bool original) {
    short weapon = (short)A(0x60) - 2;
    return CDECL(0x0001ee60, BOTWEAP_shouldChangeWeapon, Weapon, weapon) & 0xff;
}

uint32_t RunReloadAnim(bool original) {
    short weapon = (short)A(0x60) - 2;
    return CDECL(0x0001eeb0, BOTWEAP_hasReloadAnim, Weapon, weapon) & 0xff;
}

uint32_t RunHasAmmo(bool original) {
    Drone_tag *drone = ArgDrone();
    short weapon = A(4) == 0 ? BOT_startWeapon : ArgWeapon();
    return CDECL(0x0001ef30, BOTWEAP_hasAmmo, DroneWeapon, drone, weapon) & 0xff;
}

uint32_t RunInit(bool original) {
    Drone_tag *drone = ArgDrone();
    CDECL_VOID(0x0001ef90, BOTWEAP_InitWeapon, void (__cdecl *)(Drone_tag *), drone);
    return 0;
}

uint32_t RunChange(bool original) {
    Drone_tag *drone = ArgDrone();
    short weapon = ArgWeapon();
    if (A(2))
        drone->botVars->weapons[weapon].held = 1;
    return CDECL(0x0001f320, BOTWEAP_changeWeapon, DroneWeapon, drone, weapon) & 0xff;
}

uint32_t RunHasLoaded(bool original) {
    Drone_tag *drone = ArgDrone();
    short weapon = A(4) == 0 ? BOT_startWeapon : ArgWeapon();
    return CDECL(0x0001f410, BOTWEAP_hasLoadedWeapon, DroneWeapon, drone, weapon) & 0xff;
}

uint32_t RunExplosive(bool original) {
    Drone_tag *drone = ArgDrone();
    BOT_vars_t *vars = drone->botVars;
    static const short explosives[] = {0x2d, 0x2c, 0x2a, 0x33, 0x2b, 0x2e, 0x2f};
    for (int i = 0; i < COUNT(explosives); i++)
        vars->weapons[explosives[i]].held = (uchar)(A(3) == 0);
    float distance = ArgDistance();
    return CDECL(0x0001f450, BOTWEAP_getExplosiveWeapon, uint32_t (__cdecl *)(Drone_tag *, float), drone, distance) &
           0xffff;
}

uint32_t RunList(bool original) {
    Drone_tag *drone = ArgDrone();
    ushort *list = A(4) == 0 ? NULL : world.list;
    int *count = A(4) == 0 ? NULL : &world.count;
    char preferred = (char)A(2);
    return CDECL(0x0001f5c0, BOTWEAP_listHeldLoadedWeapons, uint32_t (__cdecl *)(Drone_tag *, ushort *, int *, char),
                 drone, list, count, preferred);
}

uint32_t RunTooClose(bool original) {
    Drone_tag *drone = ArgDrone();
    BOT_vars_t *vars = drone->botVars;
    // Few usable weapons, so that some bots have only explosives
    if (A(2)) {
        for (int w = 2; w < 0x53; w++)
            if (A(4) != 0)
                vars->weapons[w].held = 0;
    }
    short weapon = A(2) ? (short)(0x2a + A(10)) : ArgWeapon();
    float distance = ArgDistance();
    return CDECL(0x0001f740, BOTWEAP_tooCloseForWeapon, uint32_t (__cdecl *)(Drone_tag *, short, float), drone, weapon,
                 distance) & 0xff;
}

struct Case {
    const char *name;
    uint32_t (*run)(bool);
};

const Case kWeapons[] = {
    {"BOTWEAP_resetWeapons", RunReset},
    {"BOTWEAP_reload", RunReload},
    {"BOTWEAP_AmmoInGun", RunAmmoInGun},
    {"BOTWEAP_EquipWeapon", RunEquipWeapon},
    {"BOTWEAP_EquipAmmo", RunEquipAmmo},
    {"BOTWEAP_CheckWeaponsLoaded", RunCheckLoaded},
    {"BOTWEAP_hasWeapon", RunHasWeapon},
    {"BOTWEAP_decrRounds", RunDecrRounds},
    {"BOTWEAP_getWeaponAmmoAmount", RunAmmoAmount},
    {"BOTWEAP_getWeaponClipSize", RunClipSize},
    {"BOTWEAP_isValidWeapon", RunIsValid},
    {"BOTWEAP_getWeaponPriority", RunPriority},
    {"BOTWEAP_getAnimSet", RunAnimSet},
    {"BOTWEAP_shouldChangeWeapon", RunShouldChange},
    {"BOTWEAP_hasReloadAnim", RunReloadAnim},
    {"BOTWEAP_hasAmmo", RunHasAmmo},
    {"BOTWEAP_InitWeapon", RunInit},
    {"BOTWEAP_changeWeapon", RunChange},
    {"BOTWEAP_hasLoadedWeapon", RunHasLoaded},
    {"BOTWEAP_getExplosiveWeapon", RunExplosive},
    {"BOTWEAP_listHeldLoadedWeapons", RunList},
    {"BOTWEAP_tooCloseForWeapon", RunTooClose},
};

const Case *currentCase;
uint32_t caseSeed;

uint32_t RunCurrent(bool original) {
    caseGen = caseSeed;
    return currentCase->run(original);
}

void Group(const char *group, const Case *list, int count, int trials) {
    InstallFakes();
    int groupCases = cases, groupDiffer = differ;
    for (int c = 0; c < count; c++) {
        currentCase = &list[c];
        caseRun = RunCurrent;
        for (int trial = 0; trial < trials; trial++) {
            RandomWorld();
            caseSeed = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
            cases++;
            if (!ShadowCompare(regions, "botport", currentCase->name, trial, Theirs, Ours))
                differ++;
        }
    }
    printf("[botport] %s: %d cases, %d differ\n", group, cases - groupCases, differ - groupDiffer);
}

} // namespace

void BotPortShadow_Run(void) {
    // The table's later entries are built at start-up, after the tests
    WeaponDataTableInit();
    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&MPSettings, sizeof(MPSettings));
    regions.Add(&MPGame, sizeof(MPGame));
    regions.Add(&GameState, sizeof(GameState));
    regions.Add(&BOT_startWeapon, sizeof(BOT_startWeapon));
    static MPSettings_t shippedSettings;
    static MPGameStruct shippedGame;
    static GameState_t shippedState;
    shippedSettings = MPSettings;
    shippedGame = MPGame;
    shippedState = GameState;
    short shippedStart = GameStartWeapon, ourStart = BOT_startWeapon;

    Group("the bots' weapons", kWeapons, COUNT(kWeapons), 3000);

    ShadowHook_RemoveAll();
    MPSettings = shippedSettings;
    MPGame = shippedGame;
    GameState = shippedState;
    GameStartWeapon = shippedStart;
    BOT_startWeapon = ourStart;
    printf("[botport] %d cases, %d differ\n", cases, differ);
}
