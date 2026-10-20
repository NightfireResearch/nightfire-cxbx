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
#include "../game/drone/BOTSTATE.h"
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
    CelPos_tag pos;
    obj_tag targetObj;          // the protection or demolition object
    obj_tag *target;
    MP_PICKUP pickups[2];
    MP_OBJ_EXT exts[2];
    AIRoute_tag route;
    AIPath_tag path;
    AINode_tag nodes[6];
    MAPNODE_tag mapNodes[6];
    sAnimScript_tag scripts[2];
    ushort routeNodes[8];
    uint32_t answers;           // the fakes' answers, a bit a call
    int fakeCalls;
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

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

// ---- the fakes

bool Answer() {
    return (world.answers >> (world.fakeCalls++ & 31)) & 1;
}

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

bool __cdecl FakeCanAltAttack(DCVars_tag *dc, short variant) {
    ShadowLog("canalt(%d,%d)", Who(dc->drone), variant);
    return Answer();
}

bool __cdecl FakeCallAnim(uint state, uint variant, short endState, uint endMsg, float speed, int unused,
                          DCVars_tag *dc) {
    ShadowLog("callanim(%x,%u,%d,%u,%08x,%d,%d)", state, variant, endState, endMsg, Bits(speed), unused,
              Who(dc->drone));
    return Answer();
}

#define FAKE_MOVE(name)                                   \
    bool __cdecl Fake##name(DCVars_tag *dc) {             \
        ShadowLog(#name "(%d)", Who(dc->drone));          \
        return Answer();                                  \
    }
FAKE_MOVE(StrafeLeft)
FAKE_MOVE(StrafeRight)
FAKE_MOVE(StepLeft)
FAKE_MOVE(StepRight)
FAKE_MOVE(RollLeft)
FAKE_MOVE(RollRight)
FAKE_MOVE(Backoff)

bool __cdecl FakeOnHill(_VECTOR *pos) {
    ShadowLog("onhill(%d)", pos == (_VECTOR *)0x001ee420);
    return Answer();
}

short __cdecl FakeEvasiveMove(DCVars_tag *dc) {
    ShadowLog("evasive(%d)", Who(dc->drone));
    static const short moves[] = {0, 50, 116, 117, 118, 119, 120, 121, 122, 123};
    return moves[(world.answers >> 3) % 10];
}

void __cdecl FakeInvalidateRoute(DCVars_tag *dc) {
    ShadowLog("invalidate(%d)", Who(dc->drone));
}

int Offset(const void *p) {
    return (int)((const uint8_t *)p - (const uint8_t *)&world);
}

bool __cdecl FakeGoalToObj(AITarget_tag *target, AIPoint_tag *point, obj_tag *obj, float radius) {
    ShadowLog("goalobj(%x,%x,%d,%08x)", Offset(target), Offset(point), Who(obj), Bits(radius));
    return Answer();
}

void __cdecl FakeGoalPos(AITarget_tag *target, AIPoint_tag *point, CelPos_tag *pos, float radius) {
    ShadowLog("goalpos(%x,%x,%x,%08x)", Offset(target), Offset(point), Offset(pos), Bits(radius));
}

bool __cdecl FakeFeetPos(obj_tag *obj, _VECTOR *feet) {
    ShadowLog("feet(%d)", Who(obj));
    feet->x = obj->position.x + 0.25f;
    feet->y = obj->position.y - 1.0f;
    feet->z = obj->position.z;
    return true;
}

bool __cdecl FakeSetState(StateMachineInfo_tag *sm, DSTATE next, int param) {
    ShadowLog("setstate(%x,%d,%d)", Offset(sm), next, param);
    return Answer();
}

obj_tag *__cdecl FakeProtectionObj(void) {
    ShadowLog("protection");
    return world.target;
}

obj_tag *__cdecl FakeDemolitionObj(void) {
    ShadowLog("demolition");
    return world.target;
}

void __cdecl FakeScriptDelete(obj_tag *obj, sAnimScript_tag **slot) {
    ShadowLog("scriptdelete(%d,%x,%x)", Who(obj), Offset(slot), Offset(*slot));
}

void __cdecl FakeBotMessage(obj_tag *obj, uint msg, uint param3, uint param4) {
    ShadowLog("botmsg(%d,%x,%u,%u)", Who(obj), msg, param3, param4);
}

bool __cdecl FakePlayerIsDead(obj_tag *obj) {
    ShadowLog("isdead(%d)", Who(obj));
    return (obj->tweakA & 3) == 0;
}

void __cdecl FakeSetOpponent(Drone_tag *drone, obj_tag *opponent) {
    ShadowLog("setopponent(%d,%d)", Who(drone), Who(opponent));
}

bool __cdecl FakeSoundPlaying(DYNAMICSOUNDS *sound, uint ref) {
    ShadowLog("playing(%x,%u)", Offset(sound), ref);
    return Answer();
}

void __cdecl FakeSoundStop(DYNAMICSOUNDS *sound, uint ref) {
    ShadowLog("stop(%x,%u)", Offset(sound), ref);
}

DYNAMICSOUNDS *__cdecl FakePlaySFX(DCVars_tag *dc, uint sfx, _VECTOR *pos, char check, float scale) {
    ShadowLog("sfx(%d,%x,%d,%d,%08x)", Who(dc->drone), sfx, pos == NULL, check, Bits(scale));
    return Answer() ? (DYNAMICSOUNDS *)world.item : NULL;
}

uint __cdecl FakeGetSpawnPoint(short team, obj_tag *obj) {
    ShadowLog("spawnpoint(%d,%d,%d)", team, Who(obj), MPSettings.RespawnSelectionMode);
    return (world.answers >> 8) % 6 | (world.answers & 0x10000);
}

bool __cdecl FakeLinkToRoom(obj_tag *obj, char straddle, world_tag *level) {
    ShadowLog("linkroom(%d,%d,%p)", Who(obj), straddle, level);
    return true;
}

void __cdecl FakeLogHealth(float health, uint player) {
    ShadowLog("loghealth(%08x,%u)", Bits(health), player);
}

uint __cdecl FakeGameFlowState(void) {
    ShadowLog("flowstate");
    return (world.answers & 0x700) == 0 ? 3 : 2;
}

struct Fake {
    unsigned at;
    const void *fake;
};

const Fake kFakes[] = {
    {0x0006b500, (const void *)&FakeGetItem},
    {0x0002fac0, (const void *)&FakeDCVfromOBJ},
    {0x00066b20, (const void *)&FakeChangeWeapon},
    {0x0001c0d0, (const void *)&FakeIsPreferred},
    {0x0001c600, (const void *)&FakeOpponentIsMissile},
    {0x00043750, (const void *)&FakeCanAltAttack},
    {0x000341c0, (const void *)&FakeCallAnim},
    {0x00045720, (const void *)&FakeStrafeLeft},
    {0x000457c0, (const void *)&FakeStrafeRight},
    {0x000455f0, (const void *)&FakeStepLeft},
    {0x00045680, (const void *)&FakeStepRight},
    {0x00045a10, (const void *)&FakeRollLeft},
    {0x00045aa0, (const void *)&FakeRollRight},
    {0x000459a0, (const void *)&FakeBackoff},
    {0x0009e650, (const void *)&FakeOnHill},
    {0x00045d30, (const void *)&FakeEvasiveMove},
    {0x00043fa0, (const void *)&FakeInvalidateRoute},
    {0x00049a20, (const void *)&FakeGoalToObj},
    {0x00049980, (const void *)&FakeGoalPos},
    {0x00036dc0, (const void *)&FakeFeetPos},
    {0x0004e320, (const void *)&FakeSetState},
    {0x000a1600, (const void *)&FakeProtectionObj},
    {0x000a1610, (const void *)&FakeDemolitionObj},
    {0x0009e3e0, (const void *)&FakeBotMessage},
    {0x00015b40, (const void *)&FakeScriptDelete},
    {0x0009e6a0, (const void *)&FakePlayerIsDead},
    {0x00037a10, (const void *)&FakeSetOpponent},
    {0x000cbdc0, (const void *)&FakeSoundPlaying},
    {0x000cbda0, (const void *)&FakeSoundStop},
    {0x00037340, (const void *)&FakePlaySFX},
    {0x0009d3d0, (const void *)&FakeGetSpawnPoint},
    {0x00021110, (const void *)&FakeLinkToRoom},
    {0x000b0600, (const void *)&FakeLogHealth},
    {0x0006aa70, (const void *)&FakeGameFlowState},
};

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
    static const float healths[] = {0.0f, -0.0f, 0.5f, 1.0f, 50.0f, 100.0f, 300.0f};
    drone->health = R(3) ? healths[R(COUNT(healths))] : ShadowRandFloat(-10.0f, 400.0f);
    drone->accuracy = (uchar)R(10);
    drone->speed = (uchar)R(4);
    drone->sm.curState = 195 + R(55);
    drone->activeRoute = &world.route;
    drone->recover = (uchar)R(256);
    drone->sightFlags = R(16);
    obj->position.x = ShadowRandFloat(-10.0f, 10.0f);
    obj->position.y = ShadowRandFloat(-2.0f, 2.0f);
    obj->position.z = ShadowRandFloat(-10.0f, 10.0f);
    for (int k = 0; k < 5; k++)
        drone->combatRanges[k] = ShadowRandFloat(0.0f, 30.0f);
    for (int k = 0; k < 3; k++)
        vars->savedCombatRanges[k] = ShadowRandFloat(0.0f, 30.0f);
    vars->distraction = ShadowRandFloat(-5.0f, 1500.0f);
    vars->stats.health = (ushort)(R(4) == 0 ? R(3) : 50 + R(260));
    vars->stats.preferredWeaponClass = (uchar)R(7);
    vars->flags = R(16);
    vars->activeGoal = (uchar)(R(3) == 0 ? 0xff : R(2));
    vars->routeFailCount = (uchar)(R(3) == 0 ? 0 : R(5));
    vars->lastRouteFailFrame = GameState.NumFramesUnpaused - R(8);
    for (int g = 0; g < 2; g++) {
        BOT_goal_t *goal = &vars->goals[g];
        uint8_t *bytes = (uint8_t *)goal;
        for (size_t k = 0; k < sizeof(*goal); k++)
            bytes[k] = (uint8_t)R(256);
        goal->kind = (uchar)R(4);
        goal->subtype = (uchar)R(11);
        goal->target = R(3) ? (void *)&world.objs[R(BOTS)] : NULL;
        goal->timeBudget = ShadowRandFloat(0.0f, 2000.0f);
    }
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
    GameState.NumFramesUnpaused = R(4) == 0 ? 0 : R(100000);
    for (int i = 0; i < BOTS; i++)
        RandomBot(i);
    for (int p = 0; p < NUM_AGENTS; p++) {
        MPGame.players[p].playerObj = NULL;
        MPGame.players[p].flags = (ushort)R(0x20);
    }
    for (int i = 0; i < BOTS; i++)
        if (R(6) != 0)
            MPGame.players[NUM_PLAYERS + i].playerObj = &world.objs[i];
    MPGame.TimeIncPaused = ShadowRandFloat(0.0f, 600.0f);
    MPSettings.maybeIsTeamGame = R(2);
    world.answers = R(0x10000) | (R(0x10000) << 16);
    world.fakeCalls = 0;
    world.pos.pos.x = ShadowRandFloat(-10.0f, 10.0f);
    world.pos.cel = (cel_tag *)&world.item;

    obj_tag *target = &world.targetObj;
    memset(target, 0, sizeof(*target));
    target->objectType = (char)(R(2) ? 0x20 : OBJECTTYPE_DRONE);
    target->subState = (ushort)R(3);
    target->position.x = ShadowRandFloat(-10.0f, 10.0f);
    target->position.z = ShadowRandFloat(-10.0f, 10.0f);
    world.target = R(5) == 0 ? NULL : R(5) == 0 ? &world.objs[R(BOTS)] : target;
    for (int i = 0; i < 2; i++) {
        memset(&world.pickups[i], 0, sizeof(world.pickups[i]));
        memset(&world.exts[i], 0, sizeof(world.exts[i]));
        world.exts[i].gameObj = R(3) ? &world.objs[R(BOTS)] : target;
    }

    // A route of up to six nodes, along x
    AIRoute_tag *route = &world.route;
    memset(route, 0, sizeof(*route));
    memset(&world.path, 0, sizeof(world.path));
    route->count = (ushort)(R(3) == 0 ? 0 : R(7));
    route->path = &world.path;
    route->nodes = world.routeNodes;
    world.path.runtimeNodes = world.nodes;
    for (int i = 0; i < 6; i++) {
        memset(&world.nodes[i], 0, sizeof(world.nodes[i]));
        memset(&world.mapNodes[i], 0, sizeof(world.mapNodes[i]));
        world.nodes[i].fileNode = &world.mapNodes[i];
        world.mapNodes[i].pos.x = ShadowRandFloat(-10.0f, 10.0f);
        world.mapNodes[i].pos.y = ShadowRandFloat(-1.0f, 1.0f);
        world.mapNodes[i].pos.z = ShadowRandFloat(-10.0f, 10.0f);
    }
    for (int i = 0; i < 8; i++)
        world.routeNodes[i] = (ushort)R(6);
    route->start.pos.x = ShadowRandFloat(-10.0f, 10.0f);
    route->start.pos.z = ShadowRandFloat(-10.0f, 10.0f);
    route->dest.pos.x = ShadowRandFloat(-10.0f, 10.0f);
    route->dest.pos.z = ShadowRandFloat(-10.0f, 10.0f);
    for (int p = 0; p < NUM_AGENTS; p++)
        MPSettings.Player[p].TeamId = (MPTeam)R(3);
    static const MultiplayerGameMode modes[] = {GM_ARENA, GM_PROTECTION, GM_DEMOLITION, GM_CTF, GM_ASSASSIN, GM_KOTH,
                                                GM_TEAMKOTH};
    MPSettings.GameMode = modes[R(COUNT(modes))];
    MPSettings.weaponSet = (WeaponSet)R(PICKUP_SETS);
    static const short starts[] = {6, 1, 2, 0x10, 0x2d, 0x45};
    BOT_startWeapon = R(2) ? weapon_data[PickupMatrix[R(PICKUP_SETS)][0]].weaponBaseNum : starts[R(COUNT(starts))];
    memset(world.list, 0x5a, sizeof(world.list));
    world.count = (int)R(1000);
}

// ---- running both sides

uint32_t (*caseRun)(bool original);
unsigned groupOriginals[64];
int groupOriginalCount;

uint32_t Theirs() {
    GameStartWeapon = BOT_startWeapon;
    // Every original of the group at once, so that they reach each other rather than ours
    for (int i = 0; i < groupOriginalCount; i++)
        XbeOriginal_Restore(groupOriginals[i], true);
    uint32_t result = caseRun(true);
    for (int i = 0; i < groupOriginalCount; i++)
        XbeOriginal_Restore(groupOriginals[i], false);
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

DCVars_tag *ArgDc() {
    Drone_tag *drone = ArgDrone();
    memset(&world.dcv, 0, sizeof(world.dcv));
    world.dcv.gameObj = drone->gameObj;
    world.dcv.drone = drone;
    world.dcv.aiStateMachine = &drone->sm;
    return &world.dcv;
}

float ArgWeight() {
    static const float edges[] = {0.0f, 0.0f, -0.0f, 1.0f, 25.0f, 30.0f, 0.5f};
    if (A(2))
        return edges[A(COUNT(edges))];
    return (float)A(4000) / 100.0f;
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
    unsigned at;
    uint32_t (*run)(bool);
};

const Case kWeapons[] = {
    {"BOTWEAP_resetWeapons", 0x0001e540, RunReset},
    {"BOTWEAP_reload", 0x0001e5b0, RunReload},
    {"BOTWEAP_AmmoInGun", 0x0001e4d0, RunAmmoInGun},
    {"BOTWEAP_EquipWeapon", 0x0001e6f0, RunEquipWeapon},
    {"BOTWEAP_EquipAmmo", 0x0001e820, RunEquipAmmo},
    {"BOTWEAP_CheckWeaponsLoaded", 0x0001e920, RunCheckLoaded},
    {"BOTWEAP_hasWeapon", 0x0001e980, RunHasWeapon},
    {"BOTWEAP_decrRounds", 0x0001e9a0, RunDecrRounds},
    {"BOTWEAP_getWeaponAmmoAmount", 0x0001ea60, RunAmmoAmount},
    {"BOTWEAP_getWeaponClipSize", 0x0001ead0, RunClipSize},
    {"BOTWEAP_isValidWeapon", 0x0001eaf0, RunIsValid},
    {"BOTWEAP_getWeaponPriority", 0x0001eb70, RunPriority},
    {"BOTWEAP_getAnimSet", 0x0001edb0, RunAnimSet},
    {"BOTWEAP_shouldChangeWeapon", 0x0001ee60, RunShouldChange},
    {"BOTWEAP_hasReloadAnim", 0x0001eeb0, RunReloadAnim},
    {"BOTWEAP_hasAmmo", 0x0001ef30, RunHasAmmo},
    {"BOTWEAP_InitWeapon", 0x0001ef90, RunInit},
    {"BOTWEAP_changeWeapon", 0x0001f320, RunChange},
    {"BOTWEAP_hasLoadedWeapon", 0x0001f410, RunHasLoaded},
    {"BOTWEAP_getExplosiveWeapon", 0x0001f450, RunExplosive},
    {"BOTWEAP_listHeldLoadedWeapons", 0x0001f5c0, RunList},
    {"BOTWEAP_tooCloseForWeapon", 0x0001f740, RunTooClose},
};

// ---- BOTSTATE: combat moves and goals

uint32_t RunUnarmed(bool original) {
    DCVars_tag *dc = ArgDc();
    return CDECL(0x0001b970, BOTSTATE_chooseUnarmedAttackAnim, uint32_t (__cdecl *)(DCVars_tag *), dc) & 0xffff;
}

uint32_t RunCheckAttackMove(bool original) {
    DCVars_tag *dc = ArgDc();
    short state = (short)(205 + A(20));
    return CDECL(0x0001b9c0, BOTSTATE_checkAttackMove, uint32_t (__cdecl *)(DCVars_tag *, short), dc, state) & 0xffff;
}

uint32_t RunEvasive(bool original) {
    DCVars_tag *dc = ArgDc();
    return CDECL(0x0001bab0, BOTSTATE_EvasiveMove, uint32_t (__cdecl *)(DCVars_tag *), dc) & 0xffff;
}

uint32_t RunNearestNode(bool original) {
    DCVars_tag *dc = ArgDc();
    return CDECL(0x0001bb30, BOTSTATE_setNearestNavNode, uint32_t (__cdecl *)(DCVars_tag *), dc) & 0xff;
}

uint32_t RunUninitGoal(bool original) {
    BOT_vars_t *bot = ArgDrone()->botVars;
    uint slot = A(4) == 0 ? 0xff : A(2);
    CDECL_VOID(0x0001bb50, BOTSTATE_uninitGoal, void (__cdecl *)(BOT_vars_t *, uint), bot, slot);
    return 0;
}

uint32_t RunInitGoal(bool original) {
    BOT_vars_t *bot = ArgDrone()->botVars;
    int slot = (int)A(4) - 1;
    uchar kind = (uchar)A(4);
    CelPos_tag *pos = A(3) ? &world.pos : NULL;
    void *target = &world.objs[A(BOTS)];
    uchar subtype = (uchar)A(10);
    CDECL_VOID(0x0001bbc0, BOTSTATE_initGoal, void (__cdecl *)(BOT_vars_t *, int, uchar, CelPos_tag *, void *, uchar),
               bot, slot, kind, pos, target, subtype);
    return 0;
}

uint32_t RunGotoGoal(bool original) {
    DCVars_tag *dc = ArgDc();
    int slot = A(5) == 0 ? (int)A(4) - 1 : (int)A(2);
    uint next = 195 + A(55);
    return CDECL(0x0001bc30, BOTSTATE_gotoGoal, uint32_t (__cdecl *)(DCVars_tag *, int, uint), dc, slot, next) & 0xff;
}

uint32_t RunGoalComplete(bool original) {
    DCVars_tag *dc = ArgDc();
    uint slot = A(4);
    CDECL_VOID(0x0001beb0, BOTSTATE_setGoalComplete, void (__cdecl *)(DCVars_tag *, uint), dc, slot);
    return 0;
}

uint32_t RunActiveGoal(bool original) {
    Drone_tag *drone = ArgDrone();
    return CDECL(0x0001bed0, BOTSTATE_getActiveGoal, uint32_t (__cdecl *)(Drone_tag *), drone);
}

uint32_t RunPickPrefs(bool original) {
    BOT_vars_t *bot = ArgDrone()->botVars;
    uint slot = A(6) == 0 ? A(4) : A(2);
    float health = ArgWeight(), ammo = ArgWeight(), weapon = ArgWeight(), objective = ArgWeight();
    if (A(2))
        health = ammo = weapon = 0.0f;
    uchar distance = (uchar)(A(2) ? 0 : A(256));
    uchar flags = (uchar)A(256);
    CDECL_VOID(0x0001bef0, BOTSTATE_setGoalPickPrefs,
               void (__cdecl *)(BOT_vars_t *, uint, float, float, float, float, uchar, uchar), bot, slot, health,
               ammo, weapon, objective, distance, flags);
    return 0;
}

uint32_t RunIsPreferred(bool original) {
    BOT_vars_t *bot = ArgDrone()->botVars;
    uint weapon = A(0x56);
    return CDECL(0x0001c0d0, BOTSTATE_isPreferredWeapon, uint32_t (__cdecl *)(BOT_vars_t *, uint), bot, weapon) & 0xff;
}

uint32_t RunStateHasWeapon(bool original) {
    BOT_vars_t *bot = ArgDrone()->botVars;
    uint weapon = A(0x56);
    return CDECL(0x0001c180, BOTSTATE_hasWeapon, uint32_t (__cdecl *)(BOT_vars_t *, uint), bot, weapon) & 0xff;
}

uint32_t RunValidateRoute(bool original) {
    DCVars_tag *dc = ArgDc();
    uint status = A(5) == 0 ? A(0) : A(14);
    return CDECL(0x0001c240, BOTSTATE_validateRoute, uint32_t (__cdecl *)(DCVars_tag *, uint), dc, status) & 0xff;
}

const Case kStates[] = {
    {"BOTSTATE_chooseUnarmedAttackAnim", 0x0001b970, RunUnarmed},
    {"BOTSTATE_checkAttackMove", 0x0001b9c0, RunCheckAttackMove},
    {"BOTSTATE_EvasiveMove", 0x0001bab0, RunEvasive},
    {"BOTSTATE_setNearestNavNode", 0x0001bb30, RunNearestNode},
    {"BOTSTATE_uninitGoal", 0x0001bb50, RunUninitGoal},
    {"BOTSTATE_initGoal", 0x0001bbc0, RunInitGoal},
    {"BOTSTATE_gotoGoal", 0x0001bc30, RunGotoGoal},
    {"BOTSTATE_setGoalComplete", 0x0001beb0, RunGoalComplete},
    {"BOTSTATE_getActiveGoal", 0x0001bed0, RunActiveGoal},
    {"BOTSTATE_setGoalPickPrefs", 0x0001bef0, RunPickPrefs},
    {"BOTSTATE_isPreferredWeapon", 0x0001c0d0, RunIsPreferred},
    {"BOTSTATE_hasWeapon", 0x0001c180, RunStateHasWeapon},
    {"BOTSTATE_validateRoute", 0x0001c240, RunValidateRoute},
};

// ---- BOTSTATE: routes, distraction, dodges and the choice of weapon

uint32_t RunPathWithin(bool original) {
    Drone_tag *drone = ArgDrone();
    if (A(5) == 0)
        drone->activeRoute = NULL;
    obj_tag *obj = A(8) == 0 ? NULL : A(3) == 0 ? world.target : &world.objs[A(BOTS)];
    float range = A(4) == 0 ? 0.0f : (float)A(800) / 100.0f;
    int segments = (int)A(8) - 1;
    return CDECL(0x0001c320, BOTSTATE_isPathWithinObjectRange, uint32_t (__cdecl *)(Drone_tag *, obj_tag *, float, int),
                 drone, obj, range, segments) & 0xff;
}

uint32_t RunVisitTime(bool original) {
    Drone_tag *drone = A(6) == 0 ? NULL : ArgDrone();
    if (drone != NULL && A(4) == 0)
        drone->gameObj->objectType = OBJECTTYPE_PLAYER;
    MP_PICKUP *pickup = A(6) == 0 ? NULL : &world.pickups[A(2)];
    if (A(4) == 0)
        MPGame.TimeIncPaused = -45.0f;
    CDECL_VOID(0x0001c550, BOTSTATE_setPickupVisitTime, void (__cdecl *)(Drone_tag *, MP_PICKUP *), drone, pickup);
    return 0;
}

uint32_t RunRecovery(bool original) {
    DCVars_tag *dc = ArgDc();
    dc->drone->recover = (uchar)A(256);
    CDECL_VOID(0x0001c5a0, BOTSTATE_startRecovery, void (__cdecl *)(DCVars_tag *), dc);
    return 0;
}

uint32_t RunIsMissile(bool original) {
    Drone_tag *drone = ArgDrone();
    if (A(2))
        drone->opponent = world.target;
    obj_tag *obj = A(3) == 0 ? (obj_tag *)-1 : A(2) ? world.target : &world.objs[A(BOTS)];
    return CDECL(0x0001c600, BOTSTATE_opponentIsMissile, uint32_t (__cdecl *)(Drone_tag *, obj_tag *), drone, obj) &
           0xff;
}

uint32_t RunCloseRange(bool original) {
    Drone_tag *drone = ArgDrone();
    CDECL_VOID(0x0001c690, BOTSTATE_reallyCloseCombatRange, void (__cdecl *)(Drone_tag *), drone);
    return 0;
}

uint32_t RunDefaultRange(bool original) {
    Drone_tag *drone = ArgDrone();
    CDECL_VOID(0x0001c6c0, BOTSTATE_defaultCombatRange, void (__cdecl *)(Drone_tag *), drone);
    return 0;
}

void GoingToGoal(Drone_tag *drone) {
    if (A(4) != 0)
        drone->sm.curState = DSTATE_BotGotoGoalPosition;
    BOT_vars_t *bot = drone->botVars;
    static const float distractions[] = {0.0f, -0.0f, 5.0f, 100.0f, 1000.0f, -3.0f};
    bot->distraction = distractions[A(COUNT(distractions))];
}

uint32_t RunIncreaseDistraction(bool original) {
    Drone_tag *drone = ArgDrone();
    GoingToGoal(drone);
    float amount = A(3) == 0 ? -(float)A(200) : (float)A(2000) / 4.0f;
    return CDECL(0x0001c6f0, BOTSTATE_increaseDistraction, uint32_t (__cdecl *)(Drone_tag *, float), drone, amount) &
           0xff;
}

uint32_t RunIsDistracted(bool original) {
    Drone_tag *drone = ArgDrone();
    GoingToGoal(drone);
    return CDECL(0x0001c790, BOTSTATE_isDistracted, uint32_t (__cdecl *)(Drone_tag *), drone) & 0xff;
}

uint32_t RunSetStateChange(bool original) {
    Drone_tag *drone = ArgDrone();
    uint state = 195 + A(55);
    CDECL_VOID(0x0001c7f0, BOTSTATE_setStateChange, void (__cdecl *)(Drone_tag *, uint), drone, state);
    return 0;
}

uint32_t RunCombatMove(bool original) {
    DCVars_tag *dc = ArgDc();
    return CDECL(0x0001c820, BOTSTATE_chooseCombatMove, uint32_t (__cdecl *)(DCVars_tag *), dc) & 0xffff;
}

uint32_t RunDodge(bool original) {
    DCVars_tag *dc = ArgDc();
    uint directions = A(8);
    return CDECL(0x0001c8c0, BOTSTATE_chooseDodgeMove, uint32_t (__cdecl *)(DCVars_tag *, uint), dc, directions) &
           0xffff;
}

uint32_t RunRemoveGoals(bool original) {
    DCVars_tag *dc = ArgDc();
    BOT_vars_t *bot = dc->drone->botVars;
    obj_tag *obj = &world.objs[A(BOTS)];
    for (int g = 0; g < 2; g++) {
        if (bot->goals[g].kind == BOT_GOAL_OBJECTIVE)
            bot->goals[g].target = A(4) == 0 ? NULL : &world.exts[A(2)];
    }
    uint state = 195 + A(55);
    return CDECL(0x0001d6a0, BOTSTATE_removeGoalsForObject, uint32_t (__cdecl *)(DCVars_tag *, obj_tag *, uint), dc,
                 obj, state) & 0xff;
}

void Armed(Drone_tag *drone) {
    BOT_vars_t *bot = drone->botVars;
    if (A(3) != 0)
        drone->sightFlags |= DRONE_SIGHT_SEES_OPPONENT;
    // some explosives and the satchel, so the special cases come up
    static const short weapons[] = {1, 0x3b, 0x34, 0x2a, 0x2d, 0x10, 0x12, 0x06};
    for (int i = 0; i < COUNT(weapons); i++)
        if (A(2))
            bot->weapons[weapons[i]].held = 1;
    if (A(3) == 0)
        bot->stats.personality = BOT_PERSONALITY_BERSERKER;
}

uint32_t RunCombatChoice(bool original) {
    Drone_tag *drone = ArgDrone();
    Armed(drone);
    obj_tag *opponent = A(8) == 0 ? NULL : A(3) == 0 ? world.target : &world.objs[A(BOTS)];
    char keep = (char)A(2), noMessage = (char)A(2);
    return CDECL(0x0001d750, BOTSTATE_combatWeaponChangeChoice,
                 uint32_t (__cdecl *)(Drone_tag *, obj_tag *, char, char), drone, opponent, keep, noMessage) & 0xffff;
}

uint32_t RunStateChangeWeapon(bool original) {
    Drone_tag *drone = ArgDrone();
    Armed(drone);
    if (A(2))
        drone->opponent = A(3) == 0 ? world.target : &world.objs[A(BOTS)];
    uint weapon = A(2) ? 0 : (uint)ArgWeapon();
    char keep = (char)A(2), noChange = (char)(A(3) == 0);
    return CDECL(0x0001e3f0, BOTSTATE_changeWeapon, uint32_t (__cdecl *)(BOT_vars_t *, uint, char, char),
                 drone->botVars, weapon, keep, noChange);
}

uint32_t RunPickupChoice(bool original) {
    Drone_tag *drone = ArgDrone();
    Armed(drone);
    if (A(2))
        drone->opponent = A(3) == 0 ? world.target : &world.objs[A(BOTS)];
    short weapon = ArgWeapon();
    char held = (char)A(2), loaded = (char)A(2);
    return CDECL(0x0001e470, BOTSTATE_pickupWeaponChangeChoice, uint32_t (__cdecl *)(Drone_tag *, short, char, char),
                 drone, weapon, held, loaded) & 0xffff;
}

uint32_t RunStopAnim(bool original) {
    Drone_tag *drone = ArgDrone();
    memset(world.scripts, 0, sizeof(world.scripts));
    static const float frames[] = {0.0f, 1.0f, 1.5f, 12.75f, 30.0f, -2.5f, 70000.0f};
    world.scripts[0].curFrame = A(3) ? frames[A(COUNT(frames))] : (float)A(4000) / 16.0f;
    world.scripts[0].numFrames = (short)A(100);
    world.scripts[1].ownerSlot = (sAnimScript_tag **)&drone->unknown434;
    drone->animScript = &world.scripts[0];
    drone->unknown434 = A(2) ? &world.scripts[1] : NULL;
    CDECL_VOID(0x0001c2d0, BOTSTATE_stopAnim, void (__cdecl *)(Drone_tag *), drone);
    return 0;
}

const Case kStates2[] = {
    {"BOTSTATE_stopAnim", 0x0001c2d0, RunStopAnim},
    {"BOTSTATE_isPathWithinObjectRange", 0x0001c320, RunPathWithin},
    {"BOTSTATE_setPickupVisitTime", 0x0001c550, RunVisitTime},
    {"BOTSTATE_startRecovery", 0x0001c5a0, RunRecovery},
    {"BOTSTATE_opponentIsMissile", 0x0001c600, RunIsMissile},
    {"BOTSTATE_reallyCloseCombatRange", 0x0001c690, RunCloseRange},
    {"BOTSTATE_defaultCombatRange", 0x0001c6c0, RunDefaultRange},
    {"BOTSTATE_increaseDistraction", 0x0001c6f0, RunIncreaseDistraction},
    {"BOTSTATE_isDistracted", 0x0001c790, RunIsDistracted},
    {"BOTSTATE_setStateChange", 0x0001c7f0, RunSetStateChange},
    {"BOTSTATE_chooseCombatMove", 0x0001c820, RunCombatMove},
    {"BOTSTATE_chooseDodgeMove", 0x0001c8c0, RunDodge},
    {"BOTSTATE_removeGoalsForObject", 0x0001d6a0, RunRemoveGoals},
    {"BOTSTATE_combatWeaponChangeChoice", 0x0001d750, RunCombatChoice},
    {"BOTSTATE_changeWeapon", 0x0001e3f0, RunStateChangeWeapon},
    {"BOTSTATE_pickupWeaponChangeChoice", 0x0001e470, RunPickupChoice},
};

// ---- BOT

void Fighting(Drone_tag *drone) {
    static const uint flags[] = {0x100, 0x100, 0x10100, 0x10100, 0x300, 0x500, 0, 0x10000};
    drone->flags = flags[A(COUNT(flags))] | (A(2) ? 0x80000000 : 0);
    drone->gameObj->flags = (char)(A(5) == 0);
    if (A(6) == 0)
        drone->gameObj->objectType = OBJECTTYPE_DEAD_DRONE;
    static const float healths[] = {-1.0f, 0.0f, -0.0f, 0.5f, 1.0f, 30.0f, 100.0f};
    drone->health = healths[A(COUNT(healths))];
    drone->botVars->armour = (uchar)(A(3) == 0 ? 0 : A(256));
    drone->botVars->perPlayerSettings = &MPSettings.Player[drone->botVars->playerIndex];
}

uint32_t RunDyingOrDead(bool original) {
    Drone_tag *drone = ArgDrone();
    Fighting(drone);
    obj_tag *obj = A(8) == 0 ? NULL : drone->gameObj;
    return CDECL(0x0001a190, MPDrone_MaybeIsDyingOrDead, uint32_t (__cdecl *)(obj_tag *), obj) & 0xff;
}

uint32_t RunAlertMsg(bool original) {
    Drone_tag *drone = ArgDrone(), *from = A(4) == 0 ? NULL : ArgDrone();
    drone->botVars->perPlayerSettings = &MPSettings.Player[drone->botVars->playerIndex];
    if (from != NULL)
        from->botVars->perPlayerSettings = &MPSettings.Player[from->botVars->playerIndex];
    if (A(6) == 0)
        drone->botVars = NULL;
    else if (from != NULL && from != drone && A(6) == 0)
        from->botVars = NULL;
    uint msg = A(0x22);
    return CDECL(0x0001a260, BOT_reactToDroneAlertMsg, uint32_t (__cdecl *)(uint, Drone_tag *, Drone_tag *), msg,
                 drone, from) & 0xff;
}

uint32_t RunValidate(bool original) {
    DCVars_tag *dc = ArgDc();
    Drone_tag *drone = dc->drone;
    static const uint states[] = {0, DSTATE_BotGotoGoalPosition, DSTATE_BotGotoGoalPosition, DSTATE_BotAttack,
                                  DSTATE_BotDeathAnim, DSTATE_BotCoverIdle, DSTATE_BotImpactBullet, DSTATE_BotStuck,
                                  DSTATE_BotIdle, DSTATE_BotSeenOpponent};
    drone->sm.curState = A(3) ? states[A(COUNT(states))] : 195 + A(56);
    drone->animRow = (ushort)(0x33 + A(8));
    drone->distanceToTarget = A(3) == 0 ? (float)A(40) : (float)A(4000) / 100.0f;
    if (A(2))
        drone->opponent = A(3) ? &world.objs[A(BOTS)] : NULL;
    if (A(2))
        drone->botVars->weapon = 1;
    GoingToGoal(drone);
    static const uint news[] = {DSTATE_BotAttackRun, DSTATE_BotAttackRunChangePosition, DSTATE_BotImpactBullet,
                                DSTATE_BotHeardNoise, DSTATE_BotAttackChangeWeapon, DSTATE_BotSeenOpponent,
                                DSTATE_BotSeenOpponent, DSTATE_BotAttack, DSTATE_BotAttackFire, DSTATE_BotRespawn,
                                DSTATE_BotDead, DSTATE_BotSeenDroneShot, DSTATE_BotCoverRunTo};
    uint next = A(3) ? news[A(COUNT(news))] : 190 + A(64);
    return CDECL(0x0001a2d0, BOT_validateStateChange, uint32_t (__cdecl *)(DCVars_tag *, uint), dc, next) & 0xff;
}

uint32_t RunOpponentHistory(bool original) {
    Drone_tag *drone = ArgDrone();
    BOT_vars_t *bot = drone->botVars;
    static const uint flags[] = {0x10100, 0x10000, 0x100};
    drone->flags = flags[A(3)];
    for (int i = 0; i < 16; i++) {
        uint pick = A(6);
        bot->attackers[i] = pick < BOTS ? &world.objs[pick] : pick == BOTS ? &world.targetObj : NULL;
    }
    for (int i = 0; i < BOTS; i++)
        world.objs[i].tweakA = (uchar)A(256);
    world.targetObj.tweakA = (uchar)A(256);
    bot->attackerCursor = (uchar)A(18);
    if (A(2))
        drone->opponent = &world.objs[A(BOTS)];
    obj_tag *attacker = A(5) == 0 ? &world.targetObj : &world.objs[A(BOTS)];
    return CDECL(0x0001a990, BOT_handleOpponentHistory, uint32_t (__cdecl *)(Drone_tag *, obj_tag *), drone,
                 attacker);
}

uint32_t RunSoundEffect(bool original) {
    DCVars_tag *dc = ArgDc();
    dc->drone->unknown15 = (uchar)A(2);
    dc->drone->botVars->sound = A(2) ? (DYNAMICSOUNDS *)world.item : NULL;
    static const int kinds[] = {0, 1, 1, 29, 30, 30, 31, 31, 32};
    int kind = kinds[A(COUNT(kinds))];
    return CDECL(0x0001aca0, BOT_soundEffect, uint32_t (__cdecl *)(DCVars_tag *, int), dc, kind);
}

uint32_t RunSetGender(bool original) {
    Drone_tag *drone = ArgDrone();
    drone->botVars->skin = (uchar)A(30);
    drone->unknown15 = (uchar)A(3);
    CDECL_VOID(0x0001ad70, BOT_setGender, void (__cdecl *)(Drone_tag *), drone);
    return 0;
}

uint32_t RunFellOut(bool original) {
    Drone_tag *drone = ArgDrone();
    MPSettings.RespawnSelectionMode = (RespawnMode)A(3);
    for (int i = 0; i < 6; i++) {
        SpawnPoints[i].spawnPos.x = (float)A(100);
        SpawnPoints[i].facingDir.y = (float)A(7);
    }
    for (int i = 0; i < 16; i++)
        drone->botVars->attackers[i] = &world.objs[A(BOTS)];
    CDECL_VOID(0x0001adc0, BOT_fellOutMap, void (__cdecl *)(obj_tag *), drone->gameObj);
    return 0;
}

uint32_t RunTargetting(bool original) {
    DCVars_tag *dc = ArgDc();
    Drone_tag *drone = dc->drone;
    drone->opponent = A(6) == 0 ? NULL : &world.objs[A(BOTS)];
    drone->distanceToTarget = A(4) == 0 ? (float)A(4) : (float)A(6000) / 100.0f;
    drone->targetChangeFrame = GameState.NumFramesUnpaused - A(A(2) ? 300 : 20000);
    drone->targetStartedMoving = (uchar)A(2);
    BOT_vars_t *bot = drone->botVars;
    if (drone->opponent != NULL) {
        bot->opponentLastPos = drone->opponent->position;
        if (A(2))
            bot->opponentLastPos.x += (float)A(8) / 100.0f;
    }
    drone->aimOffset.x = (float)A(10) / 4.0f;
    CDECL_VOID(0x0001aee0, BOT_opponentTargetting, void (__cdecl *)(DCVars_tag *), dc);
    return 0;
}

uint32_t RunSetHealth(bool original) {
    Drone_tag *drone = ArgDrone();
    static const float healths[] = {-5.0f, 0.0f, -0.0f, 0.5f, 0.99f, 1.0f, 1.5f, 250.0f};
    float health = A(3) ? healths[A(COUNT(healths))] : (float)A(30000) / 100.0f;
    CDECL_VOID(0x0001b120, BOT_SetHealth, void (__cdecl *)(Drone_tag *, float), drone, health);
    return 0;
}

uint32_t RunDroneStats(bool original) {
    Drone_tag *drone = ArgDrone();
    BOT_stats_t *stats = &drone->botVars->stats;
    uint8_t *bytes = (uint8_t *)stats;
    for (size_t i = 0; i < sizeof(*stats); i++)
        bytes[i] = (uint8_t)A(256);
    if (A(3) == 0)
        stats->health = (ushort)A(2);
    CDECL_VOID(0x0001b600, BOT_setDroneStats, void (__cdecl *)(Drone_tag *, BOT_stats_t *), drone, stats);
    return 0;
}

uint32_t RunHandlePain(bool original) {
    DCVars_tag *dc = ArgDc();
    Fighting(dc->drone);
    if (A(2)) {
        dc->drone->flags = 0x10100;
        dc->drone->gameObj->flags = 0;
        dc->drone->gameObj->objectType = OBJECTTYPE_DRONE;
        dc->drone->health = (float)(1 + A(150));
    }
    MPSettings.LocationDamageEnabled = A(2);
    MPSettings.TripleDamageModifierProfessionalMode = A(3) == 0;
    GoingToGoal(dc->drone);
    static const float damages[] = {0.0f, -1.0f, 0.5f, 1.0f, 10.0f, 25.5f, 200.0f};
    float damage = A(3) ? damages[A(COUNT(damages))] : (float)A(10000) / 64.0f;
    ushort hitType = (ushort)A(10);
    static const short locations[] = {-1, 0, 5, 5, 20, 21, 32, 35, 49, 56, 57, 4, 30};
    short location = locations[A(COUNT(locations))];
    CDECL_VOID(0x0001b6b0, BOT_handlePain, void (__cdecl *)(DCVars_tag *, float, ushort, short), dc, damage, hitType,
               location);
    return 0;
}

uint32_t RunPostLoad(bool original) {
    Drone_tag *drone = ArgDrone();
    CDECL_VOID(0x0001b8e0, BOT_postLoadInit, void (__cdecl *)(Drone_tag *), drone);
    return 0;
}

const Case kBots[] = {
    {"MPDrone_MaybeIsDyingOrDead", 0x0001a190, RunDyingOrDead},
    {"BOT_reactToDroneAlertMsg", 0x0001a260, RunAlertMsg},
    {"BOT_validateStateChange", 0x0001a2d0, RunValidate},
    {"BOT_handleOpponentHistory", 0x0001a990, RunOpponentHistory},
    {"BOT_soundEffect", 0x0001aca0, RunSoundEffect},
    {"BOT_setGender", 0x0001ad70, RunSetGender},
    {"BOT_fellOutMap", 0x0001adc0, RunFellOut},
    {"BOT_opponentTargetting", 0x0001aee0, RunTargetting},
    {"BOT_SetHealth", 0x0001b120, RunSetHealth},
    {"BOT_setDroneStats", 0x0001b600, RunDroneStats},
    {"BOT_handlePain", 0x0001b6b0, RunHandlePain},
    {"BOT_postLoadInit", 0x0001b8e0, RunPostLoad},
};

const Case *currentCase;
uint32_t caseSeed;

uint32_t RunCurrent(bool original) {
    caseGen = caseSeed;
    world.fakeCalls = 0;
    return currentCase->run(original);
}

// The group's functions run as the originals on the original side; every fake but theirs stands in
void Group(const char *group, const Case *list, int count, int trials) {
    ShadowHook_RemoveAll();
    groupOriginalCount = 0;
    for (int c = 0; c < count; c++)
        groupOriginals[groupOriginalCount++] = list[c].at;
    for (const Fake &f : kFakes) {
        bool tested = false;
        for (int i = 0; i < groupOriginalCount; i++)
            tested = tested || groupOriginals[i] == f.at;
        if (!tested)
            ShadowHook_Install(f.at, f.fake);
    }
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
    regions.Add((void *)0x0018cdf8, 8);     // Rand_Random's state
    regions.Add(&SpawnPoints, sizeof(SpawnPoints));
    static MPSettings_t shippedSettings;
    static MPGameStruct shippedGame;
    static GameState_t shippedState;
    shippedSettings = MPSettings;
    shippedGame = MPGame;
    shippedState = GameState;
    short shippedStart = GameStartWeapon, ourStart = BOT_startWeapon;
    static MPSpawnPoint shippedSpawns[64];
    memcpy(shippedSpawns, SpawnPoints, sizeof(shippedSpawns));
    uint8_t shippedRandom[8];
    memcpy(shippedRandom, (void *)0x0018cdf8, 8);

    Group("the bots' weapons", kWeapons, COUNT(kWeapons), 3000);
    Group("the bots' combat moves and goals", kStates, COUNT(kStates), 3000);
    Group("the bots' distraction, dodges and weapon choice", kStates2, COUNT(kStates2), 3000);
    Group("the bots' alerts, pain, aim and respawn", kBots, COUNT(kBots), 3000);

    ShadowHook_RemoveAll();
    MPSettings = shippedSettings;
    MPGame = shippedGame;
    GameState = shippedState;
    GameStartWeapon = shippedStart;
    BOT_startWeapon = ourStart;
    memcpy(SpawnPoints, shippedSpawns, sizeof(shippedSpawns));
    memcpy((void *)0x0018cdf8, shippedRandom, 8);
    printf("[botport] %d cases, %d differ\n", cases, differ);
}
