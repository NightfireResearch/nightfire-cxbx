// Shadow tests for combat and aiming (NDrone2_Combat.cpp): each function, the original and ours, runs on the same
// made-up drones, cover nodes and messages, with every callee that needs a level (state changes, anims, speech,
// routes and the AI network, the weapons, the sight helpers, impacts) going to a logging fake. Our other combat
// ports run for real underneath both. Run at start with MenuShadowTests=on (MenuProbe.cpp), before any level exists.
//
// What cannot be tested here: what the faked callees would do in a level (the routes planned, the anims played, the
// state the machine changes to). The drones' anim tables are the game's own, so DroneAnim_CanDoAnimState answers for
// the anim sets drawn. Bool returns are compared in AL and short ones in AX: no original caller reads more
// (narrow_returns.py and the callers' MOVSX / TEST AX).

#include "DroneCombatShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../game/drone/Behaviour.h"
#include "../game/drone/DroneTables.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/NDrone2_Combat.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/Drone_AIHints.h"
#include "../game/sp/SwitchChannels.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords (GameSpan<uint32_t>{0x0018cdf8})
#define switch_BLIND_DRONES U32_AT(XbeOurs(0x001df9a0))

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int DRONES = 3;
const int NODES = 6;
const int PATH_NODES = 24;

struct World {
    obj_tag objs[DRONES];
    Drone_tag drones[DRONES];
    AnimState anims[DRONES];
    obj_tag player;
    obj_tag explosive;
    CoverNode_tag nodes[NODES + 1];     // one past the count, which FindCover reads
    uchar flood[NODES][PATH_NODES];
    AIPath_tag path;
    AIPath_tag otherPath;
    DCVars_tag dcv;
    MsgObject msg;
    Drone_tag *droneArg;
    obj_tag *gameObjArg;

    // Arguments
    float floatArg;
    ushort ushortArg;
    short shortArg;
    char charArg;
    uint firstArg, lastArg;
    CoverNode_tag *nodeArg;
    obj_tag *objArg;
    _VECTOR posOut;
    _VECTOR rotOut;
    int rotWanted;

    // What the fakes answer: a sequence drawn per call, the same for both runs (the world is restored)
    uint32_t answers;
    uint answerBias;        // 0..3: how often the bool fakes say yes
    int isFinished;
    short attackMode;
    short nearestNode;
    uint routeResult;
    uint followResult;
    uint moveResult;
    float routeDistance;
    float nextNodeDistance;
    double nextRouteAngle;
    _VECTOR furthest;
    int furthestOK;
    cel_tag *cel;
    DYNAMICSOUNDS *speech;
};
World world;

ShadowRegions regions;

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

uint32_t Answer(uint32_t n) {
    world.answers = world.answers * 1103515245u + 12345u;
    return (world.answers >> 16) % n;
}

bool Yes() {
    return Answer(4) < world.answerBias;
}

template <class T, size_t N> T Pick(const T (&values)[N]) {
    return values[ShadowRand(N)];
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
    if (obj == &world.player)
        return 100;
    if (obj == &world.explosive)
        return 101;
    return obj == NULL ? -1 : 102;
}

// Where a pointer into the world is, as an offset (pointers differ in nothing else between the runs)
long Off(const void *p) {
    if (p == NULL)
        return -1;
    return (long)((const char *)p - (const char *)&world);
}

// ---- the fakes

bool __cdecl FakeSetState(StateMachineInfo_tag *sm, DSTATE next, int param) {
    ShadowLog("setstate(%d,%d,%d)", WhoSM(sm), (int)next, param);
    return Yes();
}

bool __cdecl FakeCallAnim(uint state, uint variant, short endState, uint endMsg, float speed, int unused,
                          DCVars_tag *dcv) {
    ShadowLog("callanim(%x,%u,%d,%u,%08x,%d,%d)", state, variant, endState, endMsg, Bits(speed), unused,
              Who(dcv->drone));
    return Yes();
}

DYNAMICSOUNDS *__cdecl FakeSpeech(DCVars_tag *dcv, uint sfx, char force, char face, char needFace) {
    ShadowLog("speech(%d,%x,%d,%d,%d)", Who(dcv->drone), sfx, force, face, needFace);
    return world.speech;
}

bool __cdecl FakeIsFinished(DYNAMICSOUNDS *handle) {
    ShadowLog("isfinished(%p)", handle);
    return world.isFinished != 0;
}

void __cdecl FakeTargetSeen(Drone_tag *drone, _VECTOR *pos) {
    ShadowLog("targetseen(%d,%ld)", Who(drone), Off(pos));
}

bool __cdecl FakeCheckSurrender(DCVars_tag *dcv) {
    ShadowLog("checksurrender(%d)", Who(dcv->drone));
    return Yes();
}

void __cdecl FakeAlertToObject(DCVars_tag *dcv, uint msgType, obj_tag *obj, float a, float b, float c) {
    ShadowLog("alerttoobject(%d,%x,%d,%08x,%08x,%08x)", Who(dcv->drone), msgType, WhoObj(obj), Bits(a), Bits(b),
              Bits(c));
}

void __cdecl FakeSendAttackMessage(DCVars_tag *dcv) {
    ShadowLog("sendattack(%d)", Who(dcv->drone));
}

void __cdecl FakeSetAsAttacking(Drone_tag *drone) {
    ShadowLog("setasattacking(%d)", Who(drone));
    drone->flags |= 0x2000000;
}

short __cdecl FakeChangeToAttack(Drone_tag *drone) {
    ShadowLog("changetoattack(%d)", Who(drone));
    return world.attackMode;
}

void __cdecl FakeFailLabel(uint text) {
    ShadowLog("faillabel(%x)", text);
}

bool __cdecl FakeOkToUpdate(void) {
    ShadowLog("oktoupdate");
    return Yes();
}

bool __cdecl FakeMoveTestRelative(_VECTOR *offset, Drone_tag *drone, obj_tag *obj, char unscaled, int param5) {
    ShadowLog("movetestrelative(%08x,%08x,%08x,%d,%d,%d,%d)", Bits(offset->x), Bits(offset->y), Bits(offset->z),
              Who(drone), WhoObj(obj), unscaled, param5);
    return Yes();
}

bool __cdecl FakeAimingAtMe(DCVars_tag *dcv, float angle) {
    ShadowLog("aimingatme(%d,%08x)", Who(dcv->drone), Bits(angle));
    return Yes();
}

void __cdecl FakeFire(char restart, DCVars_tag *dcv) {
    ShadowLog("fire(%d,%d)", restart, Who(dcv->drone));
}

bool __cdecl FakeAimTarget(char checkAngle, char keepTurning, DCVars_tag *dcv) {
    ShadowLog("aimtarget(%d,%d,%d)", checkAngle, keepTurning, Who(dcv->drone));
    return Yes();
}

bool __cdecl FakeCanThrowGrenade(DCVars_tag *dcv) {
    ShadowLog("canthrowgrenade(%d)", Who(dcv->drone));
    return Answer(4) == 0;
}

void __cdecl FakeChangeWeapon(DCVars_tag *dcv, int weapon) {
    ShadowLog("changeweapon(%d,%d)", Who(dcv->drone), weapon);
}

bool __cdecl FakeFiringAnim(DCVars_tag *dcv) {
    ShadowLog("firinganim(%d)", Who(dcv->drone));
    return Yes();
}

void __cdecl FakeTurnToObj(DCVars_tag *dcv, obj_tag *obj, float offset, float tolerance) {
    ShadowLog("turntoobj(%d,%d,%08x,%08x)", Who(dcv->drone), WhoObj(obj), Bits(offset), Bits(tolerance));
}

void __cdecl FakeSetAngleToObj(DCVars_tag *dcv, obj_tag *obj, float offset, char force) {
    ShadowLog("setangletoobj(%d,%d,%08x,%d)", Who(dcv->drone), WhoObj(obj), Bits(offset), force);
}

void __cdecl FakeSeenAndAttacking(DCVars_tag *dcv) {
    ShadowLog("seenandattacking(%d)", Who(dcv->drone));
}

bool __cdecl FakeHandleImpact(DCVars_tag *dcv, MsgObject *msg, uint nextState, char flag) {
    ShadowLog("handleimpact(%d,%x,%u,%d)", Who(dcv->drone), msg->msgType, nextState, flag);
    return Yes();
}

void __cdecl FakeConsiderExplosive(DCVars_tag *dcv, obj_tag *explosive, short nextState) {
    ShadowLog("considerexplosive(%d,%d,%d)", Who(dcv->drone), WhoObj(explosive), nextState);
}

void __cdecl FakeAlertStatusSet(char status, DCVars_tag *dcv) {
    ShadowLog("alertstatus(%d,%d)", status, Who(dcv->drone));
}

bool __cdecl FakeObjectIsArmed(obj_tag *obj) {
    ShadowLog("objectisarmed(%d)", WhoObj(obj));
    return Yes();
}

short __cdecl FakeNearestNode(DCVars_tag *dcv) {
    ShadowLog("nearestnode(%d)", Who(dcv->drone));
    return world.nearestNode;
}

bool __cdecl FakeVerifyRouteTarget(obj_tag *mover, obj_tag *target, AITarget_tag *t, AIPoint_tag *point, float radius,
                                   AIRoute_tag *route) {
    ShadowLog("verifyroute(%d,%d,%ld,%ld,%08x,%ld)", WhoObj(mover), WhoObj(target), Off(t), Off(point), Bits(radius),
              Off(route));
    return Answer(5) != 0;
}

uchar __cdecl FakeRouteIsValid(AIRoute_tag *route) {
    ShadowLog("routeisvalid(%ld)", Off(route));
    return (uchar)Yes();
}

uint __cdecl FakeCalcRouteToObject(obj_tag *mover, obj_tag *target, AIRoute_tag *route, AITarget_tag *t,
                                   AIPoint_tag *point, float radius, uint flags) {
    ShadowLog("calcroute(%d,%d,%ld,%ld,%ld,%08x,%x)", WhoObj(mover), WhoObj(target), Off(route), Off(t), Off(point),
              Bits(radius), flags & 0xffff);
    return world.routeResult;
}

double __cdecl FakeGetRouteDistance(AIRoute_tag *route) {
    ShadowLog("routedistance(%ld)", Off(route));
    return world.routeDistance;
}

uint __cdecl FakeFollowRoute(AIRoute_tag *route, DCVars_tag *dcv) {
    ShadowLog("followroute(%ld,%d)", Off(route), Who(dcv->drone));
    return world.followResult;
}

bool __cdecl FakeStartRouteCreep(int unused, AIRoute_tag *route, DCVars_tag *dcv, char fromEnd) {
    ShadowLog("startcreep(%d,%ld,%d,%d)", unused, Off(route), Who(dcv->drone), fromEnd);
    return Yes();
}

void __cdecl FakeInvalidateRoute(AIRoute_tag *route) {
    ShadowLog("invalidateroute(%ld)", Off(route));
}

void __cdecl FakeSetupGoal(AITarget_tag *target, AIPoint_tag *point, CelPos_tag *celPos, float radius) {
    ShadowLog("setupgoal(%ld,%ld,%08x,%08x,%08x,%ld,%08x)", Off(target), Off(point), Bits(celPos->pos.x),
              Bits(celPos->pos.y), Bits(celPos->pos.z), Off(celPos->cel), Bits(radius));
}

bool __cdecl FakeSetupGoalToObj(AITarget_tag *target, AIPoint_tag *point, obj_tag *obj, float radius) {
    ShadowLog("setupgoaltoobj(%ld,%ld,%d,%08x)", Off(target), Off(point), WhoObj(obj), Bits(radius));
    return Yes();
}

bool __cdecl FakeFurthest(_VECTOR *from, _VECTOR *to, _VECTOR *furthest) {
    ShadowLog("furthest(%ld,%ld)", Off(from), Off(to));
    *furthest = world.furthest;
    return world.furthestOK != 0;
}

uint __cdecl FakeMoveToGoal(DCVars_tag *dcv, CelPos_tag *dest, float radius, ushort clearFlags) {
    ShadowLog("movetogoal(%d,%08x,%08x,%08x,%ld,%08x,%x)", Who(dcv->drone), Bits(dest->pos.x), Bits(dest->pos.y),
              Bits(dest->pos.z), Off(dest->cel), Bits(radius), clearFlags);
    return world.moveResult;
}

cel_tag *__cdecl FakeFindCel(_VECTOR *pos, void *w) {
    ShadowLog("findcel(%08x,%08x,%08x)", Bits(pos->x), Bits(pos->y), Bits(pos->z));
    return world.cel;
}

bool __cdecl FakeDoAnimation(Drone_tag *drone) {
    ShadowLog("doanimation(%d)", Who(drone));
    return Answer(5) != 0;
}

float __cdecl FakeNextNodeDistance(DCVars_tag *dcv) {
    ShadowLog("nextnodedistance(%d)", Who(dcv->drone));
    return world.nextNodeDistance;
}

double __cdecl FakeNextRouteAngle(DCVars_tag *dcv) {
    ShadowLog("nextrouteangle(%d)", Who(dcv->drone));
    return world.nextRouteAngle;
}

void InstallFakes() {
    ShadowHook_Install(0x0004e320, (const void *)&FakeSetState);
    ShadowHook_Install(0x000341c0, (const void *)&FakeCallAnim);
    ShadowHook_Install(0x00037530, (const void *)&FakeSpeech);
    ShadowHook_Install(0x000cbac0, (const void *)&FakeIsFinished);
    ShadowHook_Install(0x00037820, (const void *)&FakeTargetSeen);
    ShadowHook_Install(0x00038bd0, (const void *)&FakeCheckSurrender);
    ShadowHook_Install(0x00038f40, (const void *)&FakeAlertToObject);
    ShadowHook_Install(0x0003cd80, (const void *)&FakeSendAttackMessage);
    ShadowHook_Install(0x0003ace0, (const void *)&FakeSetAsAttacking);
    ShadowHook_Install(0x000397f0, (const void *)&FakeChangeToAttack);
    ShadowHook_Install(0x0009bdb0, (const void *)&FakeFailLabel);
    ShadowHook_Install(0x000b04d0, (const void *)&FakeOkToUpdate);
    ShadowHook_Install(0x000453e0, (const void *)&FakeMoveTestRelative);
    ShadowHook_Install(0x00066640, (const void *)&FakeAimingAtMe);
    ShadowHook_Install(0x000675f0, (const void *)&FakeFire);
    ShadowHook_Install(0x00066700, (const void *)&FakeAimTarget);
    ShadowHook_Install(0x00066c50, (const void *)&FakeCanThrowGrenade);
    ShadowHook_Install(0x00066b20, (const void *)&FakeChangeWeapon);
    ShadowHook_Install(0x00035e60, (const void *)&FakeFiringAnim);
    ShadowHook_Install(0x00043ba0, (const void *)&FakeTurnToObj);
    ShadowHook_Install(0x00043c60, (const void *)&FakeSetAngleToObj);
    ShadowHook_Install(0x00039f60, (const void *)&FakeSeenAndAttacking);
    ShadowHook_Install(0x0003e6a0, (const void *)&FakeHandleImpact);
    ShadowHook_Install(0x00038670, (const void *)&FakeConsiderExplosive);
    ShadowHook_Install(0x00031d30, (const void *)&FakeAlertStatusSet);
    ShadowHook_Install(0x00064be0, (const void *)&FakeObjectIsArmed);
    ShadowHook_Install(0x00044670, (const void *)&FakeNearestNode);
    ShadowHook_Install(0x00049bf0, (const void *)&FakeVerifyRouteTarget);
    ShadowHook_Install(0x000479d0, (const void *)&FakeRouteIsValid);
    ShadowHook_Install(0x0004adb0, (const void *)&FakeCalcRouteToObject);
    ShadowHook_Install(0x00047cd0, (const void *)&FakeGetRouteDistance);
    ShadowHook_Install(0x00046a80, (const void *)&FakeFollowRoute);
    ShadowHook_Install(0x00046220, (const void *)&FakeStartRouteCreep);
    ShadowHook_Install(0x000479f0, (const void *)&FakeInvalidateRoute);
    ShadowHook_Install(0x00049980, (const void *)&FakeSetupGoal);
    ShadowHook_Install(0x00049a20, (const void *)&FakeSetupGoalToObj);
    ShadowHook_Install(0x0004a7b0, (const void *)&FakeFurthest);
    ShadowHook_Install(0x00046f30, (const void *)&FakeMoveToGoal);
    ShadowHook_Install(0x00020c30, (const void *)&FakeFindCel);
    ShadowHook_Install(0x00037730, (const void *)&FakeDoAnimation);
    ShadowHook_Install(0x00044eb0, (const void *)&FakeNextNodeDistance);
    ShadowHook_Install(0x00044ec0, (const void *)&FakeNextRouteAngle);
}

// ---- the made-up world

const HASHCODE kLevels[] = {
    HT_Level_CastleExterior, HT_Level_CastleCourtyard, HT_Level_CastleIndoors1, HT_Level_CastleIndoors2,
    HT_Level_TowerA, HT_Level_TowerB, HT_Level_TowerC, HT_Level_PowerStationA1, HT_Level_PowerStationA2,
    HT_Level_Tower2A, HT_Level_Tower2B, HT_Level_Tower2C, HT_Level_EvilBase, HT_Level_EvilSilo, HT_Level_EvilBaseC,
    HT_Level_RefRoom, HT_Level_Tower2Elevator, HT_Level_HendersonA,
};

float RandomDistance() {
    static const float distances[] = {0.0f, 1.0f, 1.5f, 1.5000001f, 2.0f, 2.2f, 2.5f, 3.9f, 4.0f, 4.1f, 5.9f, 6.0f,
                                      6.1f, 10.0f, 30.0f, -0.0f};
    if (ShadowRand(20) == 0)
        return NAN;
    return Pick(distances);
}

void RandomObj(obj_tag *o, Drone_tag *d) {
    memset(o, 0, sizeof(*o));
    o->extraObjectData = d;
    o->objectType = (char)(ShadowRand(8) == 0 ? OBJECTTYPE_DEAD_DRONE : OBJECTTYPE_DRONE);
    o->position.x = ShadowRandFloat(-8.0f, 8.0f);
    o->position.y = ShadowRandFloat(-1.0f, 1.0f);
    o->position.z = ShadowRandFloat(-8.0f, 8.0f);
    o->rotation.y = ShadowRandFloat(-3.2f, 3.2f);
    o->flags = (char)ShadowRand(3);
    o->inCel = (cel_tag *)(uintptr_t)(ShadowRand(2) ? 0 : 0x1000);
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    memset(d, 0, sizeof(*d));
    RandomObj(o, d);
    d->gameObj = o;
    memset(&world.anims[i], 0, sizeof(AnimState));
    static const char weapons[] = {0, 1, 2, 0x18, 0x19, -1};
    world.anims[i].currentWeaponId = Pick(weapons);
    o->animState = ShadowRand(6) ? &world.anims[i] : NULL;

    static const short skinClasses[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 0xc, 0x10, 0x11, 0x13};
    d->skinClass = Pick(skinClasses);
    static const uchar dtypes[] = {0, 1, 2, 0x11, 0x19, 0x1e};
    d->dtype = Pick(dtypes);
    static const short animSets[] = {0, 1, 2, 3, 4, 6, 9, 10, 0x13, 20, 26};
    d->animSet = Pick(animSets);
    static const short animStates[] = {0, 0xa, 0xc, 0xd, 0x29, 0x2a, 0x5a, 0x5b, 5};
    d->animState = Pick(animStates);
    d->animMode = (uchar)ShadowRand(10);
    d->side = (uchar)(1 + ShadowRand(3));
    d->captain = (uchar)ShadowRand(2);
    d->secondWeapon = (char)(ShadowRand(4) - 1);
    d->firing = (uchar)ShadowRand(2);
    d->gunThroughWall = (uchar)ShadowRand(2);
    d->burstOver = (uchar)ShadowRand(2);
    d->unknown17 = (uchar)ShadowRand(2);
    d->modeChangeSwitchChannel = (uchar)(ShadowRand(3) == 0 ? 5 : 0);
    d->secondState = (short)(ShadowRand(2) ? 0 : DSTATE_Combat);
    d->initialState = (short)ShadowRand(250);
    d->returnState = (short)ShadowRand(250);
    d->health = ShadowRand(6) ? ShadowRandFloat(-5.0f, 50.0f) : (ShadowRand(2) ? 0.0f : NAN);
    d->distanceToTarget = RandomDistance();
    for (int r = 0; r < 5; r++)
        d->combatRanges[r] = RandomDistance();
    d->opponentAngles.y = ShadowRandFloat(-3.2f, 3.2f);
    d->aimOffset.y = ShadowRandFloat(0.0f, 1.5f);
    d->moveTarget.x = ShadowRandFloat(-8.0f, 8.0f);
    d->moveTarget.z = ShadowRandFloat(-8.0f, 8.0f);
    d->opponent = ShadowRand(5) ? &world.player : (ShadowRand(2) ? NULL : &world.objs[2]);
    d->targetSlot = (uchar)ShadowRand(8);
    static const uint seen[] = {0, 1000, 12000, 12340};
    d->opponentSeenFrame = Pick(seen) + ShadowRand(40);
    static const uint unseen[] = {0, 15, 16, 150, 179, 180, 181, 199, 200, 201, 240, 241};
    d->unseenFrames = Pick(unseen);
    d->sightFlags = ShadowRand(16);
    d->boundaryFlags = ShadowRand(0x20);
    d->bunchFlags = ShadowRand(0x20);
    d->unknown3d0 = 12000 + ShadowRand(500);
    d->clip = (short)(ShadowRand(3) - 1);
    d->clipSize = 30;
    d->moveQuad = (ushort)ShadowRand(5);
    d->speechChannel = (DYNAMICSOUNDS *)(uintptr_t)(ShadowRand(2) ? 0 : 0x5000);
    d->coverNode = ShadowRand(3) ? NULL : &world.nodes[ShadowRand(NODES)];
    d->stateScratch = ShadowRand(2) ? 0 : 0xffffffff;
    *(ushort *)&d->unknown824 = (ushort)ShadowRand(0x10000);
    d->route1.path = ShadowRand(5) ? &world.path : &world.otherPath;
    d->route1.targetFrame = 100 + ShadowRand(3);
    d->route1.arriveRadius = RandomDistance();
    d->activeRoute = &d->route1;

    d->flags = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    d->flags2 = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    for (int w = 0; w < 3; w++) {
        d->behaviour1[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
        d->behaviour2[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    }
    d->currentBehaviour = ShadowRand(2) ? d->behaviour1 : d->behaviour2;
    // A drone without an object only as a dead one (the cover test reads every live drone's object)
    if (i != 0 && ShadowRand(10) == 0) {
        d->gameObj = NULL;
        d->flags |= 0x200;
    }

    d->sm.id = 100 + i;
    static const uint states[] = {DSTATE_Combat, DSTATE_CombatOutOfRange, DSTATE_CombatNoSight, DSTATE_CombatWait,
                                  DSTATE_AimStandFire, DSTATE_AimCrouchFire, DSTATE_CombatNoMove, DSTATE_AimStand,
                                  DSTATE_AimCrouch, DSTATE_StepAimLeft, 0};
    d->sm.curState = Pick(states);
    d->sm.stateEnteredFrame = 12000 + ShadowRand(200);
    d->sm.scratch1c = ShadowRand(5);
    d->timerA.on = (uchar)ShadowRand(2);
    d->timerA.dueFrame = ShadowRand(5000);
    d->timerA.unknown8 = ShadowRand(5);
}

void RandomNode(int i) {
    CoverNode_tag *n = &world.nodes[i];
    memset(n, 0, sizeof(*n));
    n->type = (uchar)ShadowRand(3);
    n->unknown5 = (uchar)ShadowRand(12);
    n->unknown6 = (uchar)ShadowRand(12);
    n->flags = ShadowRand(0x200);
    n->cel = (cel_tag *)(uintptr_t)(0x2000 + i);
    n->occupant = ShadowRand(2) ? NULL : &world.objs[1];
    n->pos.x = ShadowRandFloat(-8.0f, 8.0f);
    n->pos.y = ShadowRandFloat(-1.0f, 1.0f);
    n->pos.z = ShadowRandFloat(-8.0f, 8.0f);
    n->rot.y = ShadowRand(10) ? ShadowRandFloat(-3.2f, 3.2f) : (ShadowRand(2) ? 0.0f : 1.5707964f);
    n->emitter.path = ShadowRand(6) ? &world.path : (ShadowRand(2) ? NULL : &world.otherPath);
    n->emitter.data = ShadowRand(6) ? world.flood[i < NODES ? i : 0] : NULL;
    for (int t = 0; t < 8; t++)
        n->usable[t] = ShadowRand(0x80);
    if (i < NODES)
        for (int k = 0; k < PATH_NODES; k++)
            world.flood[i][k] = (uchar)(ShadowRand(4) ? ShadowRand(12) : 0xff);
}

void RandomWorld() {
    world.answers = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    world.answerBias = 1 + ShadowRand(3);
    for (int i = 0; i < NODES + 1; i++)
        RandomNode(i);
    for (int i = 0; i < DRONES; i++)
        RandomDrone(i);
    // linked through next, as NDrone2List
    for (int i = 0; i < DRONES; i++) {
        world.drones[i].next = i + 1 < DRONES ? &world.drones[i + 1] : NULL;
        world.drones[i].prev = i > 0 ? &world.drones[i - 1] : NULL;
    }
    world.drones[1].opponent = ShadowRand(3) ? world.drones[0].opponent : NULL;
    RandomObj(&world.player, NULL);
    world.player.objectType = OBJECTTYPE_PLAYER;
    // Close to the cover nodes and the drone now and then
    if (ShadowRand(3) == 0)
        world.objs[1].position = world.nodes[ShadowRand(NODES)].pos;
    RandomObj(&world.explosive, NULL);
    world.explosive.objectType = OBJECTTYPE_BULLET;
    memset(&world.path, 0, sizeof(world.path));
    world.path.nodeCount = ShadowRand(PATH_NODES + 4);
    memset(&world.otherPath, 0, sizeof(world.otherPath));

    world.dcv.gameObj = &world.objs[0];
    world.dcv.drone = &world.drones[0];
    world.dcv.cel = NULL;
    world.dcv.aiStateMachine = &world.drones[0].sm;
    world.droneArg = ShadowRand(5) ? &world.drones[0] : &world.drones[1];
    world.gameObjArg = &world.objs[0];

    world.floatArg = RandomDistance();
    static const ushort kinds[] = {0, 1, 2, 3, 4, 0xffff};
    world.ushortArg = Pick(kinds);
    static const short variants[] = {0, 1, 2, 3, 4, -1, 0x7fff};
    world.shortArg = Pick(variants);
    world.charArg = (char)(ShadowRand(3) == 0 ? 0x40 : ShadowRand(2));
    world.firstArg = ShadowRand(0x100);
    world.lastArg = world.firstArg + ShadowRand(10);
    world.nodeArg = &world.nodes[ShadowRand(NODES)];
    world.objArg = ShadowRand(6) ? &world.player : NULL;
    memset(&world.posOut, 0x55, sizeof(world.posOut));
    memset(&world.rotOut, 0x55, sizeof(world.rotOut));
    world.rotWanted = ShadowRand(3) != 0;

    world.isFinished = ShadowRand(2);
    static const short modes[] = {0, DSTATE_Attack, DSTATE_DeleteMe, DSTATE_Combat, -1};
    world.attackMode = Pick(modes);
    world.nearestNode = (short)(ShadowRand(6) ? ShadowRand(PATH_NODES + 2) : -1);
    static const uint routes[] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13};
    world.routeResult = Pick(routes);
    world.followResult = Pick(routes);
    world.moveResult = Pick(routes);
    world.routeDistance = RandomDistance();
    world.nextNodeDistance = RandomDistance();
    static const double angles[] = {0.0, -0.0, 1.0, -1.0, 2.6, -2.6179938316345215, 2.7925267219543457, 2.79, -2.8,
                                    3.0};
    world.nextRouteAngle = Pick(angles);
    world.furthest.x = ShadowRandFloat(-8.0f, 8.0f);
    world.furthest.z = ShadowRandFloat(-8.0f, 8.0f);
    world.furthestOK = ShadowRand(3) != 0;
    world.cel = (cel_tag *)(uintptr_t)(ShadowRand(3) ? 0x3000 : 0);
    world.speech = (DYNAMICSOUNDS *)(uintptr_t)(ShadowRand(2) ? 0x6000 : 0);

    static const uint frames[] = {12100, 12345, 12400};
    GameState.NumFramesUnpaused = Pick(frames) + ShadowRand(50);
    GameState.CurrentLevelHashcode = Pick(kLevels);
    if (ShadowRand(2)) {
        FRAME_RATE_INT = 60;
        FRAME_RATE_DIV = 1.0f;
    } else {
        FRAME_RATE_INT = 50;
        FRAME_RATE_DIV = 1.2f;
    }
    NPCGlobals.NDrone2List = &world.drones[0];
    NPCGlobals.coverNodes = ShadowRand(10) ? world.nodes : NULL;
    NPCGlobals.numCoverNodes = ShadowRand(NODES + 1);
    NPCGlobals.playerTarget.builtFrame = 100 + ShadowRand(3);
    MPSettings.maybeDroneAIEnabled = ShadowRand(4) == 0;
    switch_BLIND_DRONES = ShadowRand(6) == 0;
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

void RandomMessage() {
    memset(&world.msg, 0, sizeof(world.msg));
    static const uint types[] = {0, 1, 2, 3, 3, 3, 3, 4, 5, 6, 7, 8, 9, 0xc, 0xc, 0x17, 0x18, 0x19, 0x1b, 0x21, 0x22,
                                 0x2e};
    world.msg.msgType = Pick(types);
    world.msg.sender = 101;
    world.msg.receiver = 100;
    world.msg.extraData = ShadowRand(4) ? (void *)&world.explosive : NULL;
}

// ---- running the two

enum Kind {
    K_DCV,              // (dcv)
    K_DCV_FLOAT,        // (dcv, floatArg)
    K_DCV_SHORT,        // (dcv, shortArg)
    K_DCV_CHAR,         // (dcv, charArg)
    K_DCV_NODE,         // (dcv, nodeArg)
    K_NODE_OK,          // (dcv, nodeArg, ushortArg, charArg)
    K_CORNER_POS,       // (nodeArg, ushortArg, &posOut, rot?, charArg)
    K_MOVE_TO_OBJECT,   // (dcv, objArg, floatArg, charArg)
    K_SFX,              // (firstArg, lastArg)
    K_DRONE,            // (droneArg)
    K_QUAD,             // (floatArg, ushortArg)
    K_STATE,            // (dcv, droneArg, gameObjArg, &msg)
    K_SIGHT_STATE,      // (dcv) in ESI
    K_ALERT_STATE,      // (dcv) in EAX
};

struct Case {
    const char *name;
    unsigned at;
    Kind kind;
    const void *ours;
    uint32_t mask;      // of the return register compared
};

const Case *current;

__declspec(naked) uint32_t __cdecl OriginalSightState(DCVars_tag *dcv) {
    _asm {
        push esi
        mov esi, [esp + 8]
        mov eax, 0x0003ea80
        call eax
        pop esi
        ret
    }
}

__declspec(naked) uint32_t __cdecl OriginalAlertState(DCVars_tag *dcv) {
    _asm {
        mov eax, [esp + 4]
        mov ecx, 0x0003ad70
        call ecx
        ret
    }
}

uint32_t Call(const void *f) {
    World &w = world;
    switch (current->kind) {
    case K_DCV:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *))(&w.dcv);
    case K_DCV_FLOAT:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, float))(&w.dcv, w.floatArg);
    case K_DCV_SHORT:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, short))(&w.dcv, w.shortArg);
    case K_DCV_CHAR:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, char))(&w.dcv, w.charArg);
    case K_DCV_NODE:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, CoverNode_tag *))(&w.dcv, w.nodeArg);
    case K_NODE_OK:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, CoverNode_tag *, ushort, char))(&w.dcv, w.nodeArg,
                                                                                            w.ushortArg, w.charArg);
    case K_CORNER_POS:
        return ORIGINAL(f, uint32_t(__cdecl *)(CoverNode_tag *, ushort, _VECTOR *, _VECTOR *, char))(
            w.nodeArg, w.ushortArg, &w.posOut, w.rotWanted ? &w.rotOut : NULL, w.charArg);
    case K_MOVE_TO_OBJECT:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, obj_tag *, float, char))(&w.dcv, w.objArg, w.floatArg,
                                                                                     w.charArg);
    case K_SFX:
        return ORIGINAL(f, uint32_t(__cdecl *)(uint, uint))(w.firstArg, w.lastArg);
    case K_DRONE:
        return ORIGINAL(f, uint32_t(__cdecl *)(Drone_tag *))(w.droneArg);
    case K_QUAD:
        return ORIGINAL(f, uint32_t(__cdecl *)(float, ushort))(w.floatArg, w.ushortArg);
    case K_STATE:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, Drone_tag *, obj_tag *, MsgObject *))(
            &w.dcv, w.droneArg, w.gameObjArg, &w.msg);
    default:
        return 0;
    }
}

uint32_t RunTheirs() {
    XbeOriginalScope o(current->at);
    if (current->kind == K_SIGHT_STATE)
        return OriginalSightState(&world.dcv) & current->mask;
    if (current->kind == K_ALERT_STATE)
        return OriginalAlertState(&world.dcv) & current->mask;
    return Call((const void *)(uintptr_t)current->at) & current->mask;
}

uint32_t RunOurs() {
    if (current->kind == K_SIGHT_STATE || current->kind == K_ALERT_STATE)
        return ORIGINAL(current->ours, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & current->mask;
    return Call(current->ours) & current->mask;
}

#define AL 0xffu
#define AX 0xffffu
#define ALL 0xffffffffu
#define CASE(name, at, kind, mask) {#name, at, kind, (const void *)&name, mask}

const Case kFirst[] = {
    CASE(DroneFunc_FirstAttack, 0x0003ec20, K_DCV, AX),
    {"DroneFunc_FirstSightState", 0x0003ea80, K_SIGHT_STATE, (const void *)&DroneFunc_FirstSightState, AX},
    {"DroneFunc_FirstAlertState", 0x0003ad70, K_ALERT_STATE, (const void *)&DroneFunc_FirstAlertState, AX},
    CASE(DroneFunc_AttackNoWeapon, 0x0003add0, K_DCV, AX),
    CASE(DroneFunc_ChooseSFX, 0x00036270, K_SFX, ALL),
    CASE(NDrone2_AttackTalk, 0x0003ba80, K_DCV, AL),
    CASE(NDrone2_SeenPlayerTalk, 0x0003bfe0, K_DCV, AL),
};

const Case kMoves[] = {
    CASE(NDrone2_HasOpponent, 0x0003a690, K_DCV, AL),
    CASE(NDrone2_CanAltAttack, 0x00043750, K_DCV_SHORT, AL),
    CASE(NDrone2_CanAimCrouch, 0x00043650, K_DCV, AL),
    CASE(NDrone2_CanStepLeft, 0x000455f0, K_DCV, AL),
    CASE(NDrone2_CanStepRight, 0x00045680, K_DCV, AL),
    CASE(NDrone2_CanStrafeLeft, 0x00045720, K_DCV, AL),
    CASE(NDrone2_CanStrafeRight, 0x000457c0, K_DCV, AL),
    CASE(NDrone2_CanStrafeDodgeLeft, 0x00045870, K_DCV, AL),
    CASE(NDrone2_CanStrafeDodgeRight, 0x00045900, K_DCV, AL),
    CASE(NDrone2_CanRollLeft, 0x00045a10, K_DCV, AL),
    CASE(NDrone2_CanRollRight, 0x00045aa0, K_DCV, AL),
    CASE(NDrone2_CanBackoff, 0x000459a0, K_DCV, AL),
    CASE(NDrone2_ChooseCombatMove, 0x00045e50, K_DCV, AX),
    CASE(NDrone2_EvasiveMove, 0x00045d30, K_DCV, AX),
};

const Case kRoutes[] = {
    CASE(NDrone2_InvalidateAttackRoute, 0x00043fa0, K_DCV, 0),
    CASE(NDrone2_MoveToObject, 0x00046d90, K_MOVE_TO_OBJECT, ALL),
    CASE(NDrone2_SetGoalToDroneWithLosToOpponent, 0x000475c0, K_DCV, AL),
    CASE(NDrone2_SetGoalToCloserPosition, 0x000476d0, K_DCV, AL),
    CASE(DroneFunc_CombatState, 0x0003ae20, K_DCV, AX),
};

const Case kCover[] = {
    CASE(DroneAnim_ForceCrouchCover, 0x00033870, K_DCV_NODE, AL),
    CASE(DroneAnim_CanUseCornerAsLowCover, 0x00033bf0, K_DCV_NODE, AL),
    CASE(DroneAnim_CanCrouchLean, 0x000338b0, K_DCV_NODE, AL),
    CASE(DroneAnim_CanCrouchStepOut, 0x00033990, K_DCV_NODE, AL),
    CASE(DroneAnim_CanStandLean, 0x00033ac0, K_DCV_NODE, AL),
    CASE(DroneAnim_CanStandStepOut, 0x00033b70, K_DCV_NODE, AL),
    CASE(NDrone2_CoverNodeOK, 0x00044310, K_NODE_OK, AX),
    CASE(NDrone2_GetPosForCornerCover, 0x00044490, K_CORNER_POS, AL),
    CASE(NDrone2_DisownCover, 0x00044210, K_DCV, 0),
    CASE(NDrone2_FindCover, 0x00046550, K_DCV_CHAR, AL),
    CASE(NDrone2_CoverAvailable, 0x00046a10, K_DCV_CHAR, AL),
};

const Case kWalk[] = {
    CASE(DroneAnim_InTransition, 0x000332e0, K_DRONE, AL),
    CASE(DroneAnim_GetMoveQuad, 0x00033c70, K_QUAD, ALL),
    CASE(DroneAnim_SetCombatMoveAnim, 0x000354c0, K_DCV_FLOAT, AL),
};

const Case kStates[] = {
    CASE(NDrone2_DSTATE_Attack, 0x000571b0, K_STATE, AL),
    CASE(NDrone2_DSTATE_Alerted1stEncounter, 0x00057310, K_STATE, AL),
    CASE(NDrone2_DSTATE_Combat, 0x00057340, K_STATE, AL),
    CASE(NDrone2_DSTATE_CombatNoMove, 0x000573f0, K_STATE, AL),
    CASE(NDrone2_DSTATE_CombatOutOfRange, 0x000576b0, K_STATE, AL),
    CASE(NDrone2_DSTATE_CombatNewSighting, 0x00057840, K_STATE, AL),
    CASE(NDrone2_DSTATE_CombatNoSight, 0x000578d0, K_STATE, AL),
    CASE(NDrone2_DSTATE_CombatTooClose, 0x000579f0, K_STATE, AL),
    CASE(NDrone2_DSTATE_CombatWait, 0x00057a30, K_STATE, AL),
    CASE(NDrone2_DSTATE_CombatNoRoute, 0x00057c80, K_STATE, AL),
    CASE(NDrone2_DSTATE_NoOpponent, 0x00058040, K_STATE, AL),
    CASE(NDrone2_DSTATE_Obstructed, 0x00058190, K_STATE, AL),
    CASE(NDrone2_DSTATE_DroneStuck, 0x00058f40, K_STATE, AL),
    CASE(NDrone2_DSTATE_DrawWeapon, 0x00059040, K_STATE, AL),
    CASE(NDrone2_DSTATE_AimStand, 0x000591d0, K_STATE, AL),
    CASE(NDrone2_DSTATE_AimStandFire, 0x00059390, K_STATE, AL),
    CASE(NDrone2_DSTATE_AimStandReload, 0x0004bf80, K_STATE, AL),
    CASE(NDrone2_DSTATE_AimStandDiscard, 0x0004c110, K_STATE, AL),
    CASE(NDrone2_DSTATE_Prone, 0x0004c200, K_STATE, AL),
    CASE(NDrone2_DSTATE_ProneFire, 0x00059700, K_STATE, AL),
    CASE(NDrone2_DSTATE_AimBackoff, 0x00059860, K_STATE, AL),
    CASE(NDrone2_DSTATE_CrouchCover, 0x00059a70, K_STATE, AL),
    CASE(NDrone2_DSTATE_AimCrouch, 0x00059c30, K_STATE, AL),
    CASE(NDrone2_DSTATE_AimCrouchFire, 0x00059d50, K_STATE, AL),
    CASE(NDrone2_DSTATE_AimCrouchReload, 0x0004c320, K_STATE, AL),
    CASE(NDrone2_DSTATE_AltAttack, 0x0004c470, K_STATE, AL),
    CASE(NDrone2_DSTATE_StepAimLeft, 0x0005a020, K_STATE, AL),
    CASE(NDrone2_DSTATE_StepAimRight, 0x0005a160, K_STATE, AL),
    CASE(NDrone2_DSTATE_StrafeAimLeft, 0x0005a2a0, K_STATE, AL),
    CASE(NDrone2_DSTATE_StrafeAimRight, 0x0005a480, K_STATE, AL),
    CASE(NDrone2_DSTATE_StrafeDodgeLeft, 0x0005a660, K_STATE, AL),
    CASE(NDrone2_DSTATE_StrafeDodgeRight, 0x0005a810, K_STATE, AL),
    CASE(NDrone2_DSTATE_RollLeftCrouch, 0x0004c5b0, K_STATE, AL),
    CASE(NDrone2_DSTATE_RollRightCrouch, 0x0004c6d0, K_STATE, AL),
};

// The quarter test also takes the angles at its boundaries and either side of them
void RandomQuadAngle() {
    static const float edges[] = {2.3561945f, 0.9250245f, 2.495821f, 0.7853982f, 2.2165682f, 0.0f, -0.0f, 3.1415927f};
    switch (ShadowRand(4)) {
    case 0: {
        float e = Pick(edges);
        world.floatArg = ShadowRand(2) ? e : -e;
        if (ShadowRand(2))
            world.floatArg = nextafterf(world.floatArg, ShadowRand(2) ? 10.0f : -10.0f);
        break;
    }
    case 1:
        world.floatArg = ShadowRand(10) ? ShadowRandFloat(-3.2f, 3.2f) : NAN;
        break;
    default:
        world.floatArg = ShadowRandFloat(-3.2f, 3.2f);
        break;
    }
}

int Test(const Case *cases, int count, int trials, int *differ) {
    int run = 0;
    for (int c = 0; c < count; c++) {
        current = &cases[c];
        for (int trial = 0; trial < trials; trial++) {
            RandomWorld();
            RandomMessage();
            if (current->kind == K_QUAD)
                RandomQuadAngle();
            run++;
            if (!ShadowCompare(regions, "dronecombat", current->name, trial, RunTheirs, RunOurs))
                (*differ)++;
        }
    }
    return run;
}

} // namespace

void DroneCombatShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], npc[sizeof(NPCGlobals_t)];
    static char channels[256];
    static uint32_t channelTimes[256];
    static uint32_t rand[2];
    uint32_t frameRate = FRAME_RATE_INT;
    float frameDiv = FRAME_RATE_DIV;
    uint32_t droneAI = MPSettings.maybeDroneAIEnabled;
    uint32_t blind = switch_BLIND_DRONES;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(channels, switch_channels, sizeof(channels));
    memcpy(channelTimes, switch_channels_time, sizeof(channelTimes));
    XbeOursRead(rand, RandWords.at, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(switch_channels, 256);
    regions.Add(switch_channels_time, sizeof(channelTimes));
    regions.Add(RandWords.game(), 8);
    regions.Add(&FRAME_RATE_INT, 4);
    regions.Add(&FRAME_RATE_DIV, 4);
    regions.Add(&MPSettings.maybeDroneAIEnabled, 4);
    regions.Add(&switch_BLIND_DRONES, 4);

    InstallFakes();
    NfWarnMuted++;

    struct Group {
        const char *what;
        const Case *cases;
        int count;
        int trials;
    };
    const Group groups[] = {
        {"first attack and talk", kFirst, sizeof(kFirst) / sizeof(kFirst[0]), 3000},
        {"combat moves", kMoves, sizeof(kMoves) / sizeof(kMoves[0]), 3000},
        {"going after the opponent", kRoutes, sizeof(kRoutes) / sizeof(kRoutes[0]), 4000},
        {"cover", kCover, sizeof(kCover) / sizeof(kCover[0]), 3000},
        {"combat walk anims", kWalk, sizeof(kWalk) / sizeof(kWalk[0]), 4000},
        {"combat and aiming states", kStates, sizeof(kStates) / sizeof(kStates[0]), 3000},
    };
    int cases[6], differ[6];
    for (int g = 0; g < 6; g++) {
        differ[g] = 0;
        cases[g] = Test(groups[g].cases, groups[g].count, groups[g].trials, &differ[g]);
    }

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(switch_channels, channels, sizeof(channels));
    memcpy(switch_channels_time, channelTimes, sizeof(channelTimes));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
    FRAME_RATE_INT = frameRate;
    FRAME_RATE_DIV = frameDiv;
    MPSettings.maybeDroneAIEnabled = droneAI;
    switch_BLIND_DRONES = blind;

    for (int g = 0; g < 6; g++)
        printf("[dronecombat] %s: %d cases, %d differ\n", groups[g].what, cases[g], differ[g]);
}
