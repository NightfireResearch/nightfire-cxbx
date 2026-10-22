// Shadow tests for the cover, door, sniper and action anim states and their helpers (game/drone/NDrone2_Cover.cpp),
// and the goal, alert, search, seen and heard states and theirs (game/drone/NDrone2_Alert.cpp): each original and ours
// run on the same made-up drones, with what needs a level going to logging fakes. Run at start with
// MenuShadowTests=on (MenuProbe.cpp), before any level exists.
//
// An original runs with every original of the two files restored (OriginalPackage), so it reaches the other originals
// and ours reach ours: a state's test checks the helpers it calls as well.
//
// Faked, so not tested here: the state changes, anims and scripts asked for, the route planning (MoveToGoalPosition,
// the AI targets, the route's direction), sight and alerts, impacts and explosives, firing and aiming, speech and
// sounds, doors opened and objects kicked, and package A's cover checks that need the cover nodes' lines of sight
// (NDrone2_CoverNodeOK, NDrone2_CoverAvailable, NDrone2_DisownCover). The corner and stand-lean checks, the behaviour
// properties, the anim tables and the maths run for real. The handlers' returns are compared in AL: their only caller,
// NDrone2_ProcessStateMachine, reads no more.

#include "DroneCoverShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../game/drone/Behaviour.h"
#include "../game/drone/DroneTables.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/NDrone2_Alert.h"
#include "../game/drone/NDrone2_Cover.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/Door.h"
#include "../game/obj/Drone_AIHints.h"
#include "../game/sp/SwitchChannels.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords (GameSpan<uint32_t>{0x0018cdf8})

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int DRONES = 4;
const int DOORS = 4;
const int ROUTE_NODES = 6;

struct World {
    obj_tag objs[DRONES];
    Drone_tag drones[DRONES];
    obj_tag player;
    obj_tag opponent;
    obj_tag doors[DOORS];
    CoverNode_tag cover[2];
    AIRoute_tag route;
    AIPath_tag path;
    AINode_tag pathNodes[ROUTE_NODES];
    MAPNODE_tag mapNodes[ROUTE_NODES];
    ushort routeList[ROUTE_NODES];
    DCVars_tag dcv;
    MsgObject msg;
    obj_tag explosive;
    Drone_tag *droneArg;        // the handler's drone and gameObj arguments: usually the dcv's
    obj_tag *gameObjArg;

    // Arguments
    uint arg8;
    short argShort;
    float argFloat;
    uint argWord;
    _VECTOR vecA, vecB, vecC, vecD;
    plane_equ_tag plane;
    AIPoint_tag point;
    double doubleResult;
    float floatResult;
    uint editBits;              // garbage for the high bits of a register argument

    // What the fakes answer
    int setStateResult;
    uint moveResult;
    ushort coverOK;
    int coverAvailable;
    ushort lookResult;
    ushort alertResult;
    int inRange;
    int armed;
    short attackMode;
    int aimingAtMe;
    int facingMe;
    int altAttack;
    int finished;
    uint chooseSfx;
    int playerPresent;
    obj_tag *firstObject;
    float feetDrop;
};
World world;

ShadowRegions regions;
int cases, differ;

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

int Who(const Drone_tag *drone) {
    for (int i = 0; i < DRONES; i++)
        if (drone == &world.drones[i])
            return i;
    return drone == NULL ? -1 : 99;
}

int WhoSM(const StateMachineInfo_tag *sm) {
    for (int i = 0; i < DRONES; i++)
        if (sm == &world.drones[i].sm)
            return i;
    return 99;
}

int WhoObj(const obj_tag *obj) {
    for (int i = 0; i < DRONES; i++)
        if (obj == &world.objs[i])
            return i;
    for (int i = 0; i < DOORS; i++)
        if (obj == &world.doors[i])
            return 10 + i;
    if (obj == &world.player)
        return 100;
    if (obj == &world.opponent)
        return 101;
    if (obj == &world.explosive)
        return 102;
    return obj == NULL ? -1 : 199;
}

// A pointer into the world as an offset, so the logs compare by value
long Where(const void *p) {
    if (p == NULL)
        return -1;
    const uint8_t *at = (const uint8_t *)p, *base = (const uint8_t *)&world;
    if (at >= base && at < base + sizeof(world))
        return (long)(at - base);
    if (at >= (const uint8_t *)&NPCGlobals && at < (const uint8_t *)&NPCGlobals + sizeof(NPCGlobals_t))
        return 0x100000 + (long)(at - (const uint8_t *)&NPCGlobals);
    return 0x7fffffff;
}

// ---- the fakes

bool __cdecl FakeSetState(StateMachineInfo_tag *sm, DSTATE next, int param) {
    ShadowLog("setstate(%d,%d,%d)", WhoSM(sm), (int)next, param);
    return world.setStateResult != 0;
}

bool __cdecl FakeCallAnim(uint state, uint variant, short endState, uint endMsg, float speed, int unused,
                          DCVars_tag *dcv) {
    ShadowLog("callanim(%x,%u,%d,%u,%08x,%d,%d)", state, variant, endState, endMsg, Bits(speed), unused,
              Who(dcv->drone));
    return true;
}

bool __cdecl FakeHandleImpact(DCVars_tag *dcv, MsgObject *msg, uint nextState, char flag) {
    ShadowLog("handleimpact(%d,%x,%u,%d)", Who(dcv->drone), msg->msgType, nextState, flag);
    return true;
}

void __cdecl FakeConsiderExplosive(DCVars_tag *dcv, obj_tag *explosive, short nextState) {
    ShadowLog("considerexplosive(%d,%d,%d)", Who(dcv->drone), WhoObj(explosive), nextState);
}

void __cdecl FakeSeenAndAttacking(DCVars_tag *dcv) {
    ShadowLog("seenattacking(%d)", Who(dcv->drone));
}

void __cdecl FakeAlertStatusSet(char status, DCVars_tag *dcv) {
    ShadowLog("alertstatus(%d,%d)", status, Who(dcv->drone));
}

ushort __cdecl FakeLookForOpponent(DCVars_tag *dcv) {
    ShadowLog("lookfor(%d)", Who(dcv->drone));
    return world.lookResult;
}

ushort __cdecl FakeEnemyAlerts(DCVars_tag *dcv, MsgObject *msg) {
    ShadowLog("enemyalerts(%d,%x)", Who(dcv->drone), msg->msgType);
    return world.alertResult;
}

bool __cdecl FakeForcedAttack(DCVars_tag *dcv) {
    ShadowLog("forcedattack(%d)", Who(dcv->drone));
    return true;
}

uint __cdecl FakeMoveToGoal(DCVars_tag *dcv, CelPos_tag *dest, float radius, ushort flags) {
    ShadowLog("movetogoal(%d,%ld,%08x,%u)", Who(dcv->drone), Where(dest), Bits(radius), flags);
    return world.moveResult;
}

void __cdecl FakeDisownCover(DCVars_tag *dcv) {
    ShadowLog("disowncover(%d)", Who(dcv->drone));
}

ushort __cdecl FakeCoverNodeOK(DCVars_tag *dcv, CoverNode_tag *node, uint side, char flag) {
    ShadowLog("covernodeok(%d,%ld,%u,%d)", Who(dcv->drone), Where(node), side, flag);
    return world.coverOK;
}

bool __cdecl FakeCoverAvailable(DCVars_tag *dcv, char flag) {
    ShadowLog("coveravailable(%d,%d)", Who(dcv->drone), flag);
    return world.coverAvailable != 0;
}

void __cdecl FakeCombatMoveAnim(DCVars_tag *dcv, float distance) {
    ShadowLog("combatmoveanim(%d,%08x)", Who(dcv->drone), Bits(distance));
}

bool __cdecl FakeAttackTalk(DCVars_tag *dcv) {
    ShadowLog("attacktalk(%d)", Who(dcv->drone));
    return true;
}

bool __cdecl FakeCanAltAttack(DCVars_tag *dcv, char flag) {
    ShadowLog("canaltattack(%d,%d)", Who(dcv->drone), flag);
    return world.altAttack != 0;
}

void __cdecl FakeFire(char restart, DCVars_tag *dcv) {
    ShadowLog("fire(%d,%d)", restart, Who(dcv->drone));
}

bool __cdecl FakePlayFiringAnim(DCVars_tag *dcv) {
    ShadowLog("playfiringanim(%d)", Who(dcv->drone));
    return true;
}

bool __cdecl FakeAimTarget(char checkAngle, char keepTurning, DCVars_tag *dcv) {
    ShadowLog("aimtarget(%d,%d,%d)", checkAngle, keepTurning, Who(dcv->drone));
    return true;
}

bool __cdecl FakeAimingAtMe(DCVars_tag *dcv, float angle) {
    ShadowLog("aimingatme(%d,%08x)", Who(dcv->drone), Bits(angle));
    return world.aimingAtMe != 0;
}

bool __cdecl FakeFacingMe(DCVars_tag *dcv, float degrees) {
    ShadowLog("facingme(%d,%08x)", Who(dcv->drone), Bits(degrees));
    return world.facingMe != 0;
}

void __cdecl FakeSetAngleToObj(DCVars_tag *dcv, obj_tag *obj, float offset, char force) {
    ShadowLog("angletoobj(%d,%d,%08x,%d)", Who(dcv->drone), WhoObj(obj), Bits(offset), force);
}

DYNAMICSOUNDS *__cdecl FakePlaySFX(DCVars_tag *dcv, uint sfx, _VECTOR *pos, char check, float scale) {
    ShadowLog("playsfx(%d,%x,%ld,%d,%08x)", Who(dcv->drone), sfx, Where(pos), check, Bits(scale));
    return NULL;
}

DYNAMICSOUNDS *__cdecl FakeSpeechSFX(DCVars_tag *dcv, uint sfx, char force, char face, char needFace) {
    ShadowLog("speech(%d,%x,%d,%d,%d)", Who(dcv->drone), sfx, force, face, needFace);
    return NULL;
}

bool __cdecl FakeIsFinished(DYNAMICSOUNDS *handle) {
    ShadowLog("isfinished(%p)", handle);
    return world.finished != 0;
}

uint __cdecl FakeChooseSFX(uint first, uint last) {
    ShadowLog("choosesfx(%x,%x)", first, last);
    return world.chooseSfx;
}

uint __cdecl FakeChooseSFXByID(Drone_tag *drone, uint first, uint last) {
    ShadowLog("choosesfxbyid(%d,%x,%x)", Who(drone), first, last);
    return world.chooseSfx;
}

bool __cdecl FakeInjuredSFX(DCVars_tag *dcv, uint sfx) {
    ShadowLog("injuredsfx(%d,%x)", Who(dcv->drone), sfx);
    return true;
}

obj_tag *__cdecl FakePlayer(void) {
    ShadowLog("player");
    return world.playerPresent ? &world.player : NULL;
}

bool __cdecl FakeFeetPos(obj_tag *obj, _VECTOR *feet) {
    ShadowLog("feetpos(%d)", WhoObj(obj));
    *feet = obj->position;
    feet->y -= world.feetDrop;
    return true;
}

obj_tag *__cdecl FakeFirstObject(void) {
    ShadowLog("firstobject");
    return world.firstObject;
}

bool __cdecl FakeDoorActivate(obj_tag *obj, obj_tag *user, char touched) {
    ShadowLog("dooractivate(%d,%d,%d)", WhoObj(obj), WhoObj(user), touched);
    return true;
}

void __cdecl FakeUnPause(obj_tag *obj) {
    ShadowLog("unpause(%d)", WhoObj(obj));
}

void __cdecl FakeMoveTarget(DCVars_tag *dcv, CelPos_tag *target) {
    ShadowLog("movetarget(%d,%08x,%08x,%08x,%p)", Who(dcv->drone), Bits(target->pos.x), Bits(target->pos.y),
              Bits(target->pos.z), target->cel);
}

void __cdecl FakeMayhewTalk(uint sfx) {
    ShadowLog("mayhew(%x)", sfx);
}

void __cdecl FakeSendMsgSelf(uint msgType, void *extraData, uint delay, uint scope, DCVars_tag *dcv) {
    ShadowLog("sendself(%x,%ld,%u,%u,%d)", msgType, Where(extraData), delay, scope, Who(dcv->drone));
}

void __cdecl FakeSetHTAnim(HASHCODE hashcode, uint flags, ushort frame, Drone_tag *drone, ushort blend, float speed) {
    ShadowLog("sethtanim(%x,%x,%u,%d,%u,%08x)", (uint)hashcode, flags, frame, Who(drone), blend, Bits(speed));
}

void __cdecl FakeOnStartAnim(Drone_tag *drone) {
    ShadowLog("onstartanim(%d,%u)", Who(drone), drone->animEndMsg);
}

void __cdecl FakeInvalidateRoute(AIRoute_tag *route) {
    ShadowLog("invalidate(%ld)", Where(route));
}

void __cdecl FakeBuildAITarget(AITarget_tag *target, obj_tag *obj, float radius) {
    ShadowLog("buildtarget(%ld,%d,%08x)", Where(target), WhoObj(obj), Bits(radius));
}

void __cdecl FakeSetupGoal(AITarget_tag *target, AIPoint_tag *point, CelPos_tag *celPos, float radius) {
    ShadowLog("setupgoal(%ld,%ld,%ld,%08x)", Where(target), Where(point), Where(celPos), Bits(radius));
}

bool __cdecl FakeInRange(DCVars_tag *dcv, float range) {
    ShadowLog("inrange(%d,%08x)", Who(dcv->drone), Bits(range));
    return world.inRange != 0;
}

bool __cdecl FakeObjectIsArmed(obj_tag *obj) {
    ShadowLog("armed(%d)", WhoObj(obj));
    return world.armed != 0;
}

short __cdecl FakeChangeToAttack(Drone_tag *drone) {
    ShadowLog("changetoattack(%d)", Who(drone));
    return world.attackMode;
}

void InstallFakes() {
    ShadowHook_Install(0x0004e320, (const void *)&FakeSetState);
    ShadowHook_Install(0x000341c0, (const void *)&FakeCallAnim);
    ShadowHook_Install(0x0003e6a0, (const void *)&FakeHandleImpact);
    ShadowHook_Install(0x00038670, (const void *)&FakeConsiderExplosive);
    ShadowHook_Install(0x00039f60, (const void *)&FakeSeenAndAttacking);
    ShadowHook_Install(0x00031d30, (const void *)&FakeAlertStatusSet);
    ShadowHook_Install(0x00065360, (const void *)&FakeLookForOpponent);
    ShadowHook_Install(0x00065220, (const void *)&FakeEnemyAlerts);
    ShadowHook_Install(0x0003b5d0, (const void *)&FakeForcedAttack);
    ShadowHook_Install(0x00046f30, (const void *)&FakeMoveToGoal);
    ShadowHook_Install(0x00044210, (const void *)&FakeDisownCover);
    ShadowHook_Install(0x00044310, (const void *)&FakeCoverNodeOK);
    ShadowHook_Install(0x00046a10, (const void *)&FakeCoverAvailable);
    ShadowHook_Install(0x000354c0, (const void *)&FakeCombatMoveAnim);
    ShadowHook_Install(0x0003ba80, (const void *)&FakeAttackTalk);
    ShadowHook_Install(0x00043750, (const void *)&FakeCanAltAttack);
    ShadowHook_Install(0x000675f0, (const void *)&FakeFire);
    ShadowHook_Install(0x00035e60, (const void *)&FakePlayFiringAnim);
    ShadowHook_Install(0x00066700, (const void *)&FakeAimTarget);
    ShadowHook_Install(0x00066640, (const void *)&FakeAimingAtMe);
    ShadowHook_Install(0x00043ee0, (const void *)&FakeFacingMe);
    ShadowHook_Install(0x00043c60, (const void *)&FakeSetAngleToObj);
    ShadowHook_Install(0x00037340, (const void *)&FakePlaySFX);
    ShadowHook_Install(0x00037530, (const void *)&FakeSpeechSFX);
    ShadowHook_Install(0x000cbac0, (const void *)&FakeIsFinished);
    ShadowHook_Install(0x00036270, (const void *)&FakeChooseSFX);
    ShadowHook_Install(0x00036290, (const void *)&FakeChooseSFXByID);
    ShadowHook_Install(0x0003b830, (const void *)&FakeInjuredSFX);
    ShadowHook_Install(0x00035f90, (const void *)&FakePlayer);
    ShadowHook_Install(0x00036dc0, (const void *)&FakeFeetPos);
    ShadowHook_Install(0x0002cfc0, (const void *)&FakeFirstObject);
    ShadowHook_Install(0x0002f1f0, (const void *)&FakeDoorActivate);
    ShadowHook_Install(0x000c4270, (const void *)&FakeUnPause);
    ShadowHook_Install(0x00046080, (const void *)&FakeMoveTarget);
    ShadowHook_Install(0x0003c380, (const void *)&FakeMayhewTalk);
    ShadowHook_Install(0x0004e890, (const void *)&FakeSendMsgSelf);
    ShadowHook_Install(0x00033f20, (const void *)&FakeSetHTAnim);
    ShadowHook_Install(0x000331d0, (const void *)&FakeOnStartAnim);
    ShadowHook_Install(0x000479f0, (const void *)&FakeInvalidateRoute);
    ShadowHook_Install(0x00049930, (const void *)&FakeBuildAITarget);
    ShadowHook_Install(0x00049980, (const void *)&FakeSetupGoal);
    ShadowHook_Install(0x00035f50, (const void *)&FakeInRange);
    ShadowHook_Install(0x00064be0, (const void *)&FakeObjectIsArmed);
    ShadowHook_Install(0x000397f0, (const void *)&FakeChangeToAttack);
}

// Every original of the two files at once, so they reach each other rather than ours
const unsigned kPackage[] = {
    0x00033c10, 0x00033820, 0x00034750, 0x00043e00, 0x00043b20, 0x0003c9f0, 0x0003cb00, 0x0003c900, 0x00039e90,
    0x00039ec0, 0x00039f10, 0x00039df0, 0x00046160, 0x000446b0, 0x00034680, 0x0005ba50, 0x0004cd60, 0x0005c420,
    0x0004cf20, 0x0005c880, 0x0005cb00, 0x0004d080, 0x0005ce10, 0x0005cfd0, 0x0005d0c0, 0x0004d1d0, 0x0004be80,
    0x00052c00, 0x00052da0, 0x00052f20, 0x0004b680, 0x0005aba0, 0x0005ae60, 0x0005afb0, 0x0005b140, 0x00044e70,
    0x00039780, 0x00039790, 0x000397c0, 0x00039f50, 0x0003c6a0, 0x00047030, 0x00043ab0, 0x00043a40, 0x00043d50,
    0x00043db0, 0x0003c780, 0x000d6320, 0x00036f20, 0x00037000, 0x000371d0, 0x00058650, 0x00058a10, 0x00052a20,
    0x00058270, 0x00058be0, 0x0005a9c0, 0x0004d380, 0x0004d4c0, 0x0005d120, 0x0004d620, 0x0005d150, 0x0005d190,
    0x0005d390, 0x0005d790,
};
const int kPackageCount = sizeof(kPackage) / sizeof(kPackage[0]);

struct OriginalPackage {
    bool ok[kPackageCount];
    OriginalPackage() {
        for (int i = 0; i < kPackageCount; i++)
            ok[i] = XbeOriginal_Restore(kPackage[i], true);
    }
    ~OriginalPackage() {
        for (int i = kPackageCount - 1; i >= 0; i--)
            if (ok[i])
                XbeOriginal_Restore(kPackage[i], false);
    }
};

// ---- the made-up world

const HASHCODE kLevels[] = {
    HT_Level_HendersonA, HT_Level_TowerA, HT_Level_TowerB, HT_Level_TowerC, HT_Level_PowerStationA1,
    HT_Level_PowerStationA2, HT_Level_Tower2A, HT_Level_Tower2B, HT_Level_Tower2C, HT_Level_Tower2Elevator,
    HT_Level_EvilBase, HT_Level_EvilSilo, HT_Level_EvilBaseC, HT_Level_CastleIndoors1,
};

float RandomOf(const float *values, int count) {
    return values[ShadowRand(count)];
}

float RandomAlertness() {
    static const float values[] = {0.0f, 0.33f, 0.32999998f, 0.5f, 0.66f, 0.65999997f, 0.99999994f, 1.0f, 1.5f};
    if (ShadowRand(12) == 0)
        return NAN;
    return RandomOf(values, sizeof(values) / sizeof(values[0]));
}

void RandomVector(_VECTOR *v, float range) {
    v->x = ShadowRandFloat(-range, range);
    v->y = ShadowRandFloat(-range, range);
    v->z = ShadowRandFloat(-range, range);
}

void RandomObj(obj_tag *o, void *data) {
    memset(o, 0, sizeof(*o));
    o->extraObjectData = data;
    o->objectType = OBJECTTYPE_DRONE;
    RandomVector(&o->position, 4.0f);
    RandomVector(&o->rotation, 3.2f);
    o->flags = (char)ShadowRand(4);
}

void RandomCover(CoverNode_tag *node) {
    memset(node, 0, sizeof(*node));
    static const uchar types[] = {COVER_CORNER, COVER_CORNER, COVER_LOW, COVER_LOW, 2};
    node->type = types[ShadowRand(sizeof(types))];
    static const uchar lowAnims[] = {5, 5, 7, 10, 0x30, 0x58, 0, 0xff};
    node->lowAnim1 = lowAnims[ShadowRand(sizeof(lowAnims))];
    node->lowAnim2 = lowAnims[ShadowRand(sizeof(lowAnims))];
    node->flags = ShadowRand(0x200);
    node->angle = ShadowRandFloat(0.0f, 1.6f);
    RandomVector(&node->pos, 4.0f);
    RandomVector(&node->rot, 3.2f);
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    memset(d, 0, sizeof(*d));
    RandomObj(o, d);
    d->gameObj = o;
    d->prev = i > 0 ? &world.drones[i - 1] : NULL;
    d->next = i + 1 < DRONES ? &world.drones[i + 1] : NULL;

    static const short skinClasses[] = {0, 1, 1, 16, 5, 3};
    d->skinClass = skinClasses[ShadowRand(sizeof(skinClasses) / sizeof(skinClasses[0]))];
    static const short animSets[] = {0, 3, 4, 5, 8, 9, 0x12, 0x13, 0x13};
    d->animSet = animSets[ShadowRand(sizeof(animSets) / sizeof(animSets[0]))];
    static const uchar dtypes[] = {0, 7, 7, 0x16, 0x16, 2};
    d->dtype = dtypes[ShadowRand(sizeof(dtypes))];
    d->dtypeBase = (uchar)ShadowRand(30);
    d->side = (uchar)(1 + ShadowRand(3));
    d->unknown15 = (uchar)ShadowRand(2);
    d->unknown16 = (uchar)(ShadowRand(4) == 0);
    d->unknown33 = (uchar)ShadowRand(2);
    d->turnedToAlert = (uchar)ShadowRand(2);
    d->heardNoiseStage = (uchar)ShadowRand(6);
    d->atCornerCover = (uchar)ShadowRand(2);
    d->usingSecondBehaviour = (uchar)(ShadowRand(4) == 0 ? 2 : ShadowRand(2));
    d->firing = (uchar)ShadowRand(2);
    d->fired = (uchar)ShadowRand(2);
    d->burstOver = (uchar)ShadowRand(2);
    d->gunThroughWall = (uchar)ShadowRand(2);
    d->inPlayersSights = (uchar)ShadowRand(2);
    d->unknown38 = (uchar)ShadowRand(2);
    static const float healths[] = {100.0f, 1.0f, 0.0f, -0.0f, -5.0f};
    d->health = ShadowRand(10) == 0 ? NAN : RandomOf(healths, 5);
    d->alertness = RandomAlertness();
    d->baseAlertness1 = RandomAlertness();
    static const float steps[] = {0.0f, 0.1f, 0.10000001f, 0.2f, 0.20000002f, 0.5f};
    d->soundAlertStep = ShadowRand(12) == 0 ? NAN : RandomOf(steps, 6);
    static const float distances[] = {3.0f, 7.9999995f, 8.0f, 8.000001f, 20.0f, 3.9999998f, 4.0f};
    d->distanceToTarget = ShadowRand(12) == 0 ? NAN : RandomOf(distances, 7);
    static const float bearings[] = {0.0f, -0.0f, 1.0f, -1.0f, 2.3561945f, 2.356195f, -2.3561945f, -3.0f, 3.1f};
    d->opponentBearing = ShadowRand(12) == 0 ? NAN : RandomOf(bearings, 9);
    d->combatRanges[1] = ShadowRandFloat(5.0f, 30.0f);
    d->combatRanges[4] = ShadowRandFloat(5.0f, 30.0f);
    d->viewRange = ShadowRandFloat(5.0f, 30.0f);
    d->clip = (short)ShadowRand(4) - 1;
    d->clipSize = (short)ShadowRand(30);
    d->unseenFrames = ShadowRand(2) ? ShadowRand(14) : 14 + ShadowRand(4);
    d->sightFlags = ShadowRand(16);
    d->flags = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    d->flags2 = ShadowRand(0x10000) | (ShadowRand(4) << 20);
    d->unknown3f0 = ShadowRand(2) ? 0 : 0x8000000;
    for (int w = 0; w < 3; w++) {
        d->behaviour1[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
        d->behaviour2[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    }
    d->currentBehaviour = ShadowRand(2) ? d->behaviour1 : d->behaviour2;
    d->diVars.keys.mode = ShadowRand(3) ? 100 : ShadowRand(36);
    d->opponent = ShadowRand(8) ? &world.opponent : NULL;
    d->playScript = (HASHCODE)(ShadowRand(4) ? 0x06000100 + ShadowRand(16) : 0);
    static const short animStates[] = {0x35, 0xf, 0x21};
    d->animState = (short)(ShadowRand(3) ? ShadowRand(0x40) : animStates[ShadowRand(3)]);
    static const ushort rows[] = {0, 0x87, 0xab, 0x10, 0x86};
    d->animRow = rows[ShadowRand(sizeof(rows) / sizeof(rows[0]))];
    d->animMode = (uchar)ShadowRand(4);
    d->unknown434 = ShadowRand(4) ? NULL : (void *)&world.path;
    d->initialState = (short)ShadowRand(250);
    d->returnState = (short)(ShadowRand(4) ? ShadowRand(250) : 0x8000 + ShadowRand(16));
    d->nodeReturnState = (short)ShadowRand(250);
    d->stateScratch = ShadowRand(5);
    d->waitNode = (ushort)(ShadowRand(2) ? 0xffff : ShadowRand(ROUTE_NODES));
    d->guardZone = ShadowRand(3) ? ShadowRand(4) : 0xff;
    d->unknown23c = ShadowRand(3) ? NULL : &world.objs[(i + 1) % DRONES];
    for (int p = 0; p < 3; p++) {
        RandomVector(&d->guardPlanes[p].normal, 1.0f);
        d->guardPlanes[p].d = ShadowRandFloat(-4.0f, 4.0f);
    }

    d->coverNode = &world.cover[ShadowRand(2)];
    d->coverFlags = (ushort)(1 + ShadowRand(2) + (ShadowRand(4) << 2));
    d->coverStep = (uchar)ShadowRand(6);
    d->coverMode = (uchar)ShadowRand(5);
    RandomVector(&d->coverPos.pos, 4.0f);
    d->door = ShadowRand(5) ? &world.doors[ShadowRand(DOORS)] : NULL;
    d->activeRoute = &world.route;
    d->route1.remaining = ShadowRand(2) ? ShadowRandFloat(0.0f, 2.0f) : ShadowRandFloat(8.0f, 12.0f);

    d->aiPoint.set = (uchar)ShadowRand(2);
    RandomVector(&d->aiPoint.pos, 4.0f);
    d->aiPoint.radius = ShadowRandFloat(0.0f, 3.0f);
    d->aiPoint.distance = ShadowRandFloat(0.0f, 14.0f);
    RandomVector(&d->soundTarget.goal.pos, 6.0f);
    RandomVector(&d->returnPos, 4.0f);
    d->heardFrame = ShadowRand(5000);

    d->sm.id = 100 + i;
    d->sm.curState = ShadowRand(250);
    d->sm.nextState = ShadowRand(3) ? ShadowRand(250) : (ShadowRand(2) ? DSTATE_UnderCoverInit : DSTATE_UnderCoverIdle);
    d->sm.scratch1c = ShadowRand(5);
    d->timerA.on = (uchar)ShadowRand(2);
    d->timerA.dueFrame = ShadowRand(5000);
    d->timerB.on = (uchar)ShadowRand(2);
    d->timerB.dueFrame = ShadowRand(5000);
}

void RandomRoute() {
    AIRoute_tag *route = &world.route;
    memset(route, 0, sizeof(*route));
    memset(&world.path, 0, sizeof(world.path));
    world.path.runtimeNodes = world.pathNodes;
    static const uint nodeFlags[] = {0, 1, 2, 2, 4, 4, 8, 0x20, 0x40, 0x8004, 0x10000, 0x40000, 0x20001, 0x80002, 3, 6};
    for (int i = 0; i < ROUTE_NODES; i++) {
        AINode_tag *node = &world.pathNodes[i];
        memset(node, 0, sizeof(*node));
        MAPNODE_tag *map = &world.mapNodes[i];
        memset(map, 0, sizeof(*map));
        map->flags = nodeFlags[ShadowRand(sizeof(nodeFlags) / sizeof(nodeFlags[0]))];
        RandomVector(&map->pos, 6.0f);
        map->cel = ShadowRand(2) ? NULL : (cel_tag *)&world.path;
        node->fileNode = map;
        node->door = ShadowRand(4) ? &world.doors[ShadowRand(DOORS)] : NULL;
        world.routeList[i] = (ushort)ShadowRand(ROUTE_NODES);
    }
    route->nodes = world.routeList;
    route->path = &world.path;
    route->count = (ushort)ShadowRand(ROUTE_NODES + 1);
    route->current = (short)ShadowRand(ROUTE_NODES + 2) - 1;
    route->flags = (ushort)ShadowRand(4);
    RandomVector(&route->start.pos, 6.0f);
    RandomVector(&route->dest.pos, 6.0f);
    route->creep.active = (uchar)ShadowRand(2);
    route->creep.step = (short)ShadowRand(20);
    route->remaining = ShadowRand(2) ? ShadowRandFloat(0.0f, 2.0f) : ShadowRandFloat(8.0f, 12.0f);
}

void RandomWorld() {
    for (int i = 0; i < DRONES; i++)
        RandomDrone(i);
    for (int i = 0; i < 2; i++)
        RandomCover(&world.cover[i]);
    RandomRoute();
    RandomObj(&world.player, NULL);
    world.player.objectType = OBJECTTYPE_PLAYER;
    RandomObj(&world.opponent, &world.drones[DRONES - 1]);
    RandomObj(&world.explosive, NULL);
    world.explosive.objectType = OBJECTTYPE_BULLET;
    // Partners and door guard opponents near the drone, heights around the 2 m the checks use
    for (int i = 1; i < DRONES; i++) {
        if (ShadowRand(2)) {
            world.objs[i].position = world.objs[0].position;
            world.objs[i].position.x += ShadowRandFloat(-3.0f, 3.0f);
            world.objs[i].position.y += ShadowRand(2) ? ShadowRandFloat(-2.5f, 2.5f) : (ShadowRand(2) ? 2.0f : -2.0f);
        }
        if (ShadowRand(4) == 0)
            world.objs[i].objectType = OBJECTTYPE_DEAD_DRONE;
    }
    if (ShadowRand(3) == 0) {
        world.player.position = world.objs[0].position;
        world.player.position.x += ShadowRand(2) ? 3.0f : 2.9999998f;
    }
    world.opponent.position.y = world.objs[0].position.y + ShadowRandFloat(-2.5f, 2.5f);
    // Doors: a list for control_first_object, some within the 2 m FindElevatorDoor looks
    for (int i = 0; i < DOORS; i++) {
        obj_tag *door = &world.doors[i];
        RandomObj(door, NULL);
        static const char types[] = {OBJECTTYPE_DOOR, OBJECTTYPE_DOOR, OBJECTTYPE_DOOR, OBJECTTYPE_SCRIPTPLAYER, 7};
        door->objectType = types[ShadowRand(sizeof(types))];
        static const ushort states[] = {DoorState_Closed, DoorState_Open, DoorState_Opening, DoorState_Closing, 6, 0};
        door->curState = states[ShadowRand(sizeof(states) / sizeof(states[0]))];
        door->subState = (ushort)ShadowRand(3);
        door->nextObject = i + 1 < DOORS ? &world.doors[i + 1] : NULL;
        if (ShadowRand(2)) {
            door->position = world.objs[0].position;
            door->position.z += ShadowRandFloat(-2.1f, 2.1f);
        }
    }
    world.firstObject = ShadowRand(6) ? &world.doors[0] : NULL;

    world.dcv.gameObj = &world.objs[0];
    world.dcv.drone = &world.drones[0];
    world.dcv.cel = NULL;
    world.dcv.aiStateMachine = &world.drones[0].sm;
    world.droneArg = ShadowRand(5) ? &world.drones[0] : &world.drones[1];
    world.gameObjArg = ShadowRand(5) ? &world.objs[0] : &world.objs[1];

    world.setStateResult = ShadowRand(2);
    static const uint moves[] = {0, 0, 0, 1, 2, 2, 2, 3, 4, 6, 9, 10, 11, 12, 12, 13, 0x100};
    world.moveResult = moves[ShadowRand(sizeof(moves) / sizeof(moves[0]))];
    static const ushort oks[] = {0, 0, 1, 0x100};
    world.coverOK = oks[ShadowRand(4)];
    world.coverAvailable = ShadowRand(2);
    static const ushort looks[] = {0, 0, 0, DSTATE_Attack, 0xffff, 0x8001};
    world.lookResult = looks[ShadowRand(sizeof(looks) / sizeof(looks[0]))];
    world.alertResult = looks[ShadowRand(sizeof(looks) / sizeof(looks[0]))];
    world.inRange = ShadowRand(2);
    world.armed = ShadowRand(2);
    static const short attacks[] = {0, DSTATE_Attack, -1};
    world.attackMode = attacks[ShadowRand(3)];
    world.aimingAtMe = ShadowRand(3) == 0;
    world.facingMe = ShadowRand(2);
    world.altAttack = ShadowRand(4) == 0;
    world.finished = ShadowRand(2);
    world.chooseSfx = ShadowRand(3) ? 0xd0 + ShadowRand(4) : 0;
    world.playerPresent = ShadowRand(6) != 0;
    world.feetDrop = ShadowRandFloat(0.0f, 1.0f);

    // A speech channel now and then, for Drone_IsTalking
    world.drones[0].speechChannel = ShadowRand(3) ? NULL : (DYNAMICSOUNDS *)&world.explosive;

    world.arg8 = ShadowRand(10);
    world.argShort = (short)ShadowRand(5) - 2;
    static const float fractions[] = {0.1f, 0.08f, 0.5f, 1.0f};
    world.argFloat = RandomOf(fractions, 4);
    world.argWord = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    world.editBits = ShadowRand(0x10000) << 16;
    RandomVector(&world.vecA, 2.0f);
    RandomVector(&world.vecB, 2.0f);
    RandomVector(&world.vecC, 6.0f);
    RandomVector(&world.vecD, 6.0f);
    if (ShadowRand(4) == 0) {
        world.vecD = world.objs[0].position;
        world.vecD.x += ShadowRand(2) ? 0.0099f : 0.0101f;
    }
    memset(&world.point, 0, sizeof(world.point));
    RandomVector(&world.point.pos, 6.0f);
    world.doubleResult = 0.0;
    world.floatResult = 0.0f;

    static const uint frames[] = {1000, 12345, 0x7ffffff0, 0x80000010, 0xfffffff0};
    GameState.NumFramesUnpaused = frames[ShadowRand(sizeof(frames) / sizeof(frames[0]))] + ShadowRand(50);
    GameState.CurrentLevelHashcode = kLevels[ShadowRand(sizeof(kLevels) / sizeof(kLevels[0]))];
    if (ShadowRand(2)) {
        FRAME_RATE_INT = 60;
        FRAME_RATE_DIV = 1.0f;
    } else {
        FRAME_RATE_INT = 50;
        FRAME_RATE_DIV = 1.2f;
    }
    MPSettings.maybeDroneAIEnabled = ShadowRand(3) == 0;
    NPCGlobals.NDrone2List = &world.drones[0];
    NPCGlobals.unknown174 = ShadowRand(3) == 0;
    switch_channels[0x99] = (char)ShadowRand(2);
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

void RandomMessage() {
    memset(&world.msg, 0, sizeof(world.msg));
    switch (ShadowRand(12)) {
    case 0:
        world.msg.msgType = ShadowRand(2) ? DRONE_MSG_BotStateChanged : 0x40;
        break;
    case 1:
        world.msg.msgType = ShadowRand(2) ? 0xffffffffu : 0x80000003u;
        break;
    case 2:
    case 3:
        world.msg.msgType = DRONE_MSG_Enter;
        break;
    case 4:
    case 5:
        world.msg.msgType = DRONE_MSG_Update;
        break;
    default:
        world.msg.msgType = ShadowRand(0x27);
        break;
    }
    world.msg.sender = 100 + ShadowRand(DRONES);
    world.msg.receiver = 100;
    world.msg.extraData = ShadowRand(4) ? (void *)&world.explosive : NULL;
}

// ---- the functions under test

struct Test {
    const char *name;
    unsigned at;
    uint32_t (*run)(void);      // calls the function at `at` (the original's, or ours behind its jump)
};

const Test *current;

uint32_t RunTheirs() {
    OriginalPackage originals;
    return current->run();
}

uint32_t RunOurs() {
    return current->run();
}

void Compare(const char *tag, const Test *test, int trial) {
    current = test;
    cases++;
    if (!ShadowCompare(regions, tag, test->name, trial, RunTheirs, RunOurs))
        differ++;
}

#define CALL(at, type) ORIGINAL(at, type)
#define DCV &world.dcv

// DI the anim state (its high bits garbage), ESI dcv, the result in AX
__declspec(naked) uint32_t __cdecl CallValidateCoverAnim(DCVars_tag *dcv, uint32_t state) {
    _asm {
        push esi
        push edi
        mov esi, [esp + 0xc]
        mov edi, [esp + 0x10]
        mov eax, 0x00033c10
        call eax
        movzx eax, ax
        pop edi
        pop esi
        ret
    }
}

uint32_t RunValidate() {
    return CallValidateCoverAnim(DCV, world.editBits | world.drones[0].coverNode->lowAnim1);
}
uint32_t RunCoverModeAnim() {
    return CALL(0x00033820, ushort (__cdecl *)(DCVars_tag *, uchar))(DCV, world.drones[0].coverMode);
}
uint32_t RunCoverAnim() {
    return CALL(0x00034750, uint32_t (__cdecl *)(DCVars_tag *, uchar, short))(DCV, (uchar)world.arg8,
                                                                               world.argShort) & 0xff;
}
uint32_t RunSetAngleToCover() {
    return CALL(0x00043e00, uint32_t (__cdecl *)(DCVars_tag *, float))(DCV, world.argFloat) & 0xff;
}
uint32_t RunSlide() {
    CALL(0x00043b20, void (__cdecl *)(_VECTOR *, float, char, DCVars_tag *))(&world.vecD, world.argFloat,
                                                                              (char)(world.arg8 & 1), DCV);
    return 0;
}
uint32_t RunRunForCoverTalk() {
    CALL(0x0003c9f0, void (__cdecl *)(DCVars_tag *))(DCV);
    return 0;
}
uint32_t RunUnderCoverTalk() {
    CALL(0x0003cb00, void (__cdecl *)(DCVars_tag *))(DCV);
    return 0;
}
uint32_t RunUnderCoverAimTalk() {
    CALL(0x0003c900, void (__cdecl *)(DCVars_tag *))(DCV);
    return 0;
}
uint32_t RunHeardNoiseTalk() {
    CALL(0x0003c780, void (__cdecl *)(DCVars_tag *))(DCV);
    return 0;
}
uint32_t RunScaredTalk() {
    CALL(0x0003c6a0, void (__cdecl *)(DCVars_tag *))(DCV);
    return 0;
}

const Test kCoverHelpers[] = {
    {"DroneAnim_ValidateCoverAnim", 0x00033c10, RunValidate},
    {"DroneAnim_CoverModeAnim", 0x00033820, RunCoverModeAnim},
    {"DroneAnim_CoverAnim", 0x00034750, RunCoverAnim},
    {"NDrone2_SetAngleToCover", 0x00043e00, RunSetAngleToCover},
    {"NDrone2_SlideToPosition", 0x00043b20, RunSlide},
};

const Test kTalk[] = {
    {"DroneFunc_RunForCoverTalk", 0x0003c9f0, RunRunForCoverTalk},
    {"DroneFunc_UnderCoverTalk", 0x0003cb00, RunUnderCoverTalk},
    {"DroneFunc_UnderCoverAimTalk", 0x0003c900, RunUnderCoverAimTalk},
    {"DroneFunc_HeardNoiseTalk", 0x0003c780, RunHeardNoiseTalk},
    {"NDrone2_CivilianScaredTalk", 0x0003c6a0, RunScaredTalk},
};

uint32_t RunKick() {
    CALL(0x00039e90, void (__cdecl *)(DCVars_tag *))(DCV);
    return 0;
}
uint32_t RunOpenDoor() {
    CALL(0x00039ec0, void (__cdecl *)(DCVars_tag *))(DCV);
    return 0;
}
uint32_t RunDoorIsOpen() {
    if (world.arg8 == 0)
        world.drones[0].activeRoute = NULL;
    return CALL(0x00039f10, uint32_t (__cdecl *)(DCVars_tag *))(DCV) & 0xff;
}
uint32_t RunFindElevatorDoor() {
    CALL(0x00039df0, void (__cdecl *)(DCVars_tag *))(DCV);
    return 0;
}
uint32_t RunMoveToRouteNode() {
    CALL(0x00046160, void (__cdecl *)(DCVars_tag *, short, AIRoute_tag *))(DCV, world.argShort, &world.route);
    return 0;
}
uint32_t RunReachedDestNode() {
    return CALL(0x000446b0, uint32_t (__cdecl *)(AIRoute_tag *, DCVars_tag *))(&world.route, DCV) & 0xff;
}
uint32_t RunSetScript() {
    return CALL(0x00034680, uint32_t (__cdecl *)(HASHCODE, ushort, ushort, uchar, short, uint, uint, DCVars_tag *))(
               world.drones[0].playScript, (ushort)world.argWord, (ushort)(world.argWord >> 16), (uchar)world.arg8,
               world.argShort, world.arg8 * 3, world.argWord ^ 0x5a5a, DCV) & 0xff;
}

const Test kDoors[] = {
    {"NDrone2_KickObject", 0x00039e90, RunKick},
    {"NDrone2_OpenDoor", 0x00039ec0, RunOpenDoor},
    {"NDrone2_DoorIsOpen", 0x00039f10, RunDoorIsOpen},
    {"NDrone2_FindElevatorDoor", 0x00039df0, RunFindElevatorDoor},
    {"NDrone2_MoveTo_RouteNode", 0x00046160, RunMoveToRouteNode},
    {"NDrone2_ReachedDestNode", 0x000446b0, RunReachedDestNode},
    {"DroneAnim_SetScript", 0x00034680, RunSetScript},
};

uint32_t RunDistanceToAIPoint() {
    world.doubleResult = CALL(0x00044e70, double (__cdecl *)(DCVars_tag *, AIPoint_tag *))(DCV, &world.point);
    return 0;
}
uint32_t RunUsing2nd() {
    return CALL(0x00039780, uint32_t (__cdecl *)(Drone_tag *))(&world.drones[0]) & 0xff;
}
uint32_t RunSet1st() {
    return CALL(0x00039790, uint32_t (__cdecl *)(Drone_tag *))(&world.drones[0]) & 0xff;
}
uint32_t RunSet2nd() {
    return CALL(0x000397c0, uint32_t (__cdecl *)(Drone_tag *))(&world.drones[0]) & 0xff;
}
uint32_t RunIsCivilian() {
    return CALL(0x00039f50, uint32_t (__cdecl *)(DCVars_tag *))(DCV) & 0xff;
}
uint32_t RunMoveToAlert() {
    return CALL(0x00047030, uint (__cdecl *)(DCVars_tag *))(DCV);
}
uint32_t RunDroneAlertRoute() {
    return CALL(0x00043ab0, uint32_t (__cdecl *)(DCVars_tag *))(DCV) & 0xff;
}
uint32_t RunSoundAlertRoute() {
    return CALL(0x00043a40, uint32_t (__cdecl *)(DCVars_tag *))(DCV) & 0xff;
}
uint32_t RunSetAngleToAlert() {
    CALL(0x00043d50, void (__cdecl *)(DCVars_tag *, float))(DCV, world.argFloat);
    return 0;
}
uint32_t RunAngleDifferenceToAlert() {
    world.floatResult = CALL(0x00043db0, float (__cdecl *)(DCVars_tag *))(DCV);
    return 0;
}
uint32_t RunPlane() {
    CALL(0x000d6320, void (__cdecl *)(plane_equ_tag *, _VECTOR *, _VECTOR *, _VECTOR *))(&world.plane, &world.vecA,
                                                                                          &world.vecB, &world.vecC);
    return 0;
}
uint32_t RunGuardPlanes() {
    CALL(0x00036f20, void (__cdecl *)(_VECTOR *, _VECTOR *, _VECTOR *, Drone_tag *))(&world.vecC, &world.vecD,
                                                                                      &world.vecA, &world.drones[0]);
    return 0;
}
uint32_t RunSetPartner() {
    return CALL(0x00037000, uint32_t (__cdecl *)(uchar, DCVars_tag *))(world.drones[1].dtype, DCV) & 0xff;
}
uint32_t RunDoorGuard() {
    return CALL(0x000371d0, uint (__cdecl *)(DCVars_tag *))(DCV);
}

const Test kAlertHelpers[] = {
    {"NDrone2_DistanceToAIPoint", 0x00044e70, RunDistanceToAIPoint},
    {"DroneFunc_Using2ndBehaviour", 0x00039780, RunUsing2nd},
    {"DroneFunc_Set1stBehaviour", 0x00039790, RunSet1st},
    {"DroneFunc_Set2ndBehaviour", 0x000397c0, RunSet2nd},
    {"NDrone2_IsCivilian", 0x00039f50, RunIsCivilian},
    {"NDrone2_MoveToAlertPosition", 0x00047030, RunMoveToAlert},
    {"NDrone2_SetDroneAlertRoute", 0x00043ab0, RunDroneAlertRoute},
    {"NDrone2_SetSoundAlertRoute", 0x00043a40, RunSoundAlertRoute},
    {"NDrone2_SetAngleToAlert", 0x00043d50, RunSetAngleToAlert},
    {"NDrone2_GetAngleDifferenceToAlert", 0x00043db0, RunAngleDifferenceToAlert},
};

const Test kGuards[] = {
    {"plane_from_two_vectors_and_point", 0x000d6320, RunPlane},
    {"NDrone2_SetDoorGuardPlanes", 0x00036f20, RunGuardPlanes},
    {"NDrone2_SetDronePartner", 0x00037000, RunSetPartner},
    {"NDrone2_DoorGuardHandler", 0x000371d0, RunDoorGuard},
};

// The state handlers: one runner reads the address from the test
uint32_t RunState() {
    return CALL(current->at, uint32_t (__cdecl *)(DCVars_tag *, Drone_tag *, obj_tag *, MsgObject *))(
               &world.dcv, world.droneArg, world.gameObjArg, &world.msg) & 0xff;
}

const Test kCoverStates[] = {
    {"NDrone2_DSTATE_RunForCover", 0x0005ba50, RunState},
    {"NDrone2_DSTATE_UnderCoverInit", 0x0004cd60, RunState},
    {"NDrone2_DSTATE_UnderCoverIdle", 0x0005c420, RunState},
    {"NDrone2_DSTATE_UnderCoverAim", 0x0004cf20, RunState},
    {"NDrone2_DSTATE_UnderCoverFire", 0x0005c880, RunState},
    {"NDrone2_DSTATE_UnderCoverSniperFire", 0x0005cb00, RunState},
    {"NDrone2_DSTATE_UnderCoverSniperReload", 0x0004d080, RunState},
    {"NDrone2_DSTATE_UnderCoverReturn", 0x0005ce10, RunState},
    {"NDrone2_DSTATE_UnderCoverTypeChange", 0x0005cfd0, RunState},
    {"NDrone2_DSTATE_UnderCoverLeave", 0x0005d0c0, RunState},
    {"NDrone2_DSTATE_UnderCoverLeaveNow", 0x0004d1d0, RunState},
};

const Test kSniperStates[] = {
    {"NDrone2_DSTATE_GrenadeThrow", 0x0004be80, RunState},
    {"NDrone2_DSTATE_SniperIdle", 0x00052c00, RunState},
    {"NDrone2_DSTATE_SniperAim", 0x00052da0, RunState},
    {"NDrone2_DSTATE_SniperFire", 0x00052f20, RunState},
    {"NDrone2_DSTATE_SniperReload", 0x0004b680, RunState},
    {"NDrone2_DSTATE_OpenDoor", 0x0005aba0, RunState},
    {"NDrone2_DSTATE_KickObject", 0x0005ae60, RunState},
    {"NDrone2_DSTATE_ActionAnim", 0x0005afb0, RunState},
    {"NDrone2_DSTATE_StandFiddle", 0x0005b140, RunState},
};

const Test kAlertStates[] = {
    {"NDrone2_DSTATE_GoToGoalPosition", 0x00058650, RunState},
    {"NDrone2_DSTATE_HostageGoToGoalPosition", 0x00058a10, RunState},
    {"NDrone2_DSTATE_AllyGoToGoalPosition", 0x00052a20, RunState},
    {"NDrone2_DSTATE_AlertToPosition", 0x00058270, RunState},
    {"NDrone2_DSTATE_SearchArea", 0x00058be0, RunState},
    {"NDrone2_DSTATE_Investigate", 0x0005a9c0, RunState},
};

const Test kSeenStates[] = {
    {"NDrone2_DSTATE_SeenDeadBody", 0x0004d380, RunState},
    {"NDrone2_DSTATE_SeenSurrenderedDrone", 0x0004d4c0, RunState},
    {"NDrone2_DSTATE_SeenDroneShot", 0x0005d120, RunState},
    {"NDrone2_DSTATE_SeenExplosive", 0x0004d620, RunState},
    {"NDrone2_DSTATE_HeardNoise", 0x0005d150, RunState},
    {"NDrone2_DSTATE_HeardNoiseAware", 0x0005d190, RunState},
    {"NDrone2_DSTATE_HeardNoiseSuspect", 0x0005d390, RunState},
    {"NDrone2_DSTATE_HeardNoiseAlert", 0x0005d790, RunState},
};

struct Group {
    const char *what;
    const Test *tests;
    int count;
    int trials;
    bool messages;
};

#define GROUP(what, tests, trials, messages) {what, tests, sizeof(tests) / sizeof(tests[0]), trials, messages}

const Group kGroups[] = {
    GROUP("cover anims, the cover angle and the slide", kCoverHelpers, 6000, false),
    GROUP("the cover, heard-noise and scared lines", kTalk, 3000, false),
    GROUP("doors, kicks, route nodes and SetScript", kDoors, 6000, false),
    GROUP("goal, alert route and alert angle helpers", kAlertHelpers, 3000, false),
    GROUP("door guard partners and planes", kGuards, 6000, false),
    GROUP("cover states", kCoverStates, 5000, true),
    GROUP("grenade, sniper, door, kick, action and fiddle states", kSniperStates, 5000, true),
    GROUP("goal, alert, search and investigate states", kAlertStates, 6000, true),
    GROUP("seen and heard states", kSeenStates, 5000, true),
};

} // namespace

void DroneCoverShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], npc[sizeof(NPCGlobals_t)];
    static char channels[256];
    static uint32_t rand[2];
    uint32_t frameRate = FRAME_RATE_INT;
    float frameDiv = FRAME_RATE_DIV;
    uint32_t droneAI = MPSettings.maybeDroneAIEnabled;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(channels, switch_channels, sizeof(channels));
    XbeOursRead(rand, RandWords.at, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(switch_channels, 256);
    regions.Add(RandWords.game(), 8);
    regions.Add(&FRAME_RATE_INT, 4);
    regions.Add(&FRAME_RATE_DIV, 4);
    regions.Add(&MPSettings.maybeDroneAIEnabled, sizeof(MPSettings.maybeDroneAIEnabled));

    InstallFakes();
    NfWarnMuted++;

    for (const Group &group : kGroups) {
        int groupCases = cases, groupDiffer = differ;
        for (int t = 0; t < group.count; t++) {
            for (int trial = 0; trial < group.trials; trial++) {
                RandomWorld();
                if (group.messages)
                    RandomMessage();
                Compare("dronecover", &group.tests[t], trial);
            }
        }
        printf("[dronecover] %s: %d cases, %d differ\n", group.what, cases - groupCases, differ - groupDiffer);
    }

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(switch_channels, channels, sizeof(channels));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
    FRAME_RATE_INT = frameRate;
    FRAME_RATE_DIV = frameDiv;
    MPSettings.maybeDroneAIEnabled = droneAI;
}
