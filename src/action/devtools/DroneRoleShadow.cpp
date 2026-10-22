// Shadow tests for the scripted roles' states and helpers (NDrone2_Roles.cpp, NDrone2_Hostage.cpp): each handler, the
// original and ours, gets the same message on the same made-up drones, with every function it calls that needs a
// level (state changes, anims, routes and paths, sight, speech and sounds, impacts, alerts, cover) going to a logging
// fake. The helpers (NDrone2_ReFindMissionPath, NDrone2_FindAmbushCover, the hostage pairing and talk, and
// NDrone2_NearArmedBond) run the same way on made-up routes, cover nodes and drone lists. Run at start with
// MenuShadowTests=on (MenuProbe.cpp), before any level exists.
//
// What cannot be tested here: what the faked callees would do in a level. The ported helpers our handlers call
// (NDrone2_NearArmedBond, NDrone2_SetupHostageKiller and the rest) run for real in both, the original's calls
// reaching our ports; they are compared on their own in the helpers' group. The handlers' returns are compared in
// AL, which is all their only caller, NDrone2_ProcessStateMachine, reads (narrow_returns.py); so are the helpers'.
//
// The fakes answer from one stream of numbers kept in the world, so the original and ours get the same answers as
// long as they make the same calls.

#include "DroneRoleShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../game/drone/Behaviour.h"
#include "../game/drone/DroneTables.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/NDrone2_Hostage.h"
#include "../game/drone/NDrone2_Roles.h"
#include "../game/obj/Drone_AIHints.h"
#include "../game/sp/SwitchChannels.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords (GameSpan<uint32_t>{0x0018cdf8})

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int DRONES = 3;
const int COVER = 8;
const int NODES = 8;
const int STREAM = 64;

struct World {
    obj_tag objs[DRONES];
    Drone_tag drones[DRONES];
    obj_tag player;
    obj_tag explosive;
    AnimState anims[DRONES + 1];
    sAnimScript_tag scripts[2];
    CoverNode_tag cover[COVER];
    ushort routeNodes[NODES];
    AIPathNodeRecord fileNodes[NODES];
    AIPath_tag path;
    AIPath_tag navPath;
    char cels[4];
    DCVars_tag dcv;
    MsgObject msg;
    Drone_tag *droneArg;        // the handler's drone and gameObj arguments: usually the dcv's
    obj_tag *gameObjArg;
    bool playerThere;           // the player is there or not for the whole case, as in a level
    uint32_t stream[STREAM];    // what the fakes answer, in turn
    uint32_t streamPos;
};
World world;

ShadowRegions regions;
int cases, differ;

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

uint64_t Bits64(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

// A pointer, as an offset into the world when it is in it
uint32_t W(const void *p) {
    if (p == NULL)
        return 0xffffffffu;
    uintptr_t at = (uintptr_t)p, base = (uintptr_t)&world;
    if (at >= base && at < base + sizeof(world))
        return (uint32_t)(at - base);
    return 0x80000000u | (uint32_t)at;
}

uint32_t Next(uint32_t n) {
    uint32_t v = world.stream[world.streamPos++ % STREAM];
    return n ? v % n : v;
}

// ---- the fakes

bool __cdecl FakeSetState(StateMachineInfo_tag *sm, DSTATE next, int param) {
    ShadowLog("setstate(%x,%d,%d)", W(sm), (int)next, param);
    return Next(2) != 0;
}

bool __cdecl FakeCallAnim(uint state, uint variant, short endState, uint endMsg, float speed, int unused,
                          DCVars_tag *dcv) {
    ShadowLog("callanim(%x,%u,%d,%u,%08x,%d,%x)", state, variant, endState, endMsg, Bits(speed), unused, W(dcv));
    return true;
}

bool __cdecl FakeStandIdle(DCVars_tag *dcv, char wait) {
    ShadowLog("standidle(%x,%d)", W(dcv), wait);
    return true;
}

bool __cdecl FakeSetDAnimNow(ushort row, ushort blend, uchar callFlag, DCVars_tag *dcv) {
    ShadowLog("setdanimnow(%x,%u,%u,%x)", row, blend, callFlag, W(dcv));
    return true;
}

bool __cdecl FakeSetScript(HASHCODE script, ushort animState, ushort nextRow, uchar blend, short endState,
                           uint endMsg, uint flags, DCVars_tag *dcv) {
    ShadowLog("setscript(%x,%x,%x,%u,%d,%u,%u,%x)", (uint)script, animState, nextRow, blend, endState, endMsg, flags,
              W(dcv));
    return true;
}

bool __cdecl FakeCanDoAnimState(DCVars_tag *dcv, short state, short variant) {
    ShadowLog("candoanim(%x,%d,%d)", W(dcv), state, variant);
    return Next(2) != 0;
}

bool __cdecl FakeHandleImpact(DCVars_tag *dcv, MsgObject *msg, uint nextState, char anim) {
    ShadowLog("handleimpact(%x,%x,%u,%d)", W(dcv), W(msg), nextState, anim);
    return true;
}

void __cdecl FakeConsiderExplosive(DCVars_tag *dcv, obj_tag *explosive, short nextState) {
    ShadowLog("considerexplosive(%x,%x,%d)", W(dcv), W(explosive), nextState);
}

bool __cdecl FakeForcedAttack(DCVars_tag *dcv) {
    ShadowLog("forcedattack(%x)", W(dcv));
    return true;
}

ushort AlertAnswer() {
    static const ushort answers[] = {0, 0, DSTATE_Attack, DSTATE_SeenOpponent, 0xffff, 0x8001};
    return answers[Next(sizeof(answers) / sizeof(answers[0]))];
}

ushort __cdecl FakeEnemyAlerts(DCVars_tag *dcv, MsgObject *msg) {
    ShadowLog("enemyalerts(%x,%x)", W(dcv), W(msg));
    return AlertAnswer();
}

ushort __cdecl FakeLookForOpponent(DCVars_tag *dcv) {
    ShadowLog("lookforopponent(%x)", W(dcv));
    return AlertAnswer();
}

void __cdecl FakeSetAngleToObj(DCVars_tag *dcv, obj_tag *obj, float offset, char force) {
    ShadowLog("setangle(%x,%x,%08x,%d)", W(dcv), W(obj), Bits(offset), force);
}

void __cdecl FakeSeenAndAttacking(DCVars_tag *dcv) {
    ShadowLog("seenandattacking(%x)", W(dcv));
}

void __cdecl FakeAlertStatusSet(char status, DCVars_tag *dcv) {
    ShadowLog("alertstatus(%d,%x)", status, W(dcv));
}

obj_tag *__cdecl FakePlayer(void) {
    ShadowLog("player");
    return world.playerThere ? &world.player : NULL;
}

// Never NULL, for the helpers that read its position
obj_tag *__cdecl FakePlayerPresent(void) {
    ShadowLog("player");
    return &world.player;
}

DYNAMICSOUNDS *__cdecl FakeSpeechSFX(DCVars_tag *dcv, uint sfx, char force, char face, char needFace) {
    ShadowLog("speech(%x,%x,%d,%d,%d)", W(dcv), sfx, force, face, needFace);
    return Next(2) ? (DYNAMICSOUNDS *)&world.cels[1] : NULL;
}

DYNAMICSOUNDS *__cdecl FakePlaySFX(DCVars_tag *dcv, uint sfx, _VECTOR *pos, char check, float scale) {
    ShadowLog("playsfx(%x,%x,%x,%d,%08x)", W(dcv), sfx, W(pos), check, Bits(scale));
    return NULL;
}

void __cdecl FakeAssignAIPath(DCVars_tag *dcv, uint pathType) {
    ShadowLog("assignpath(%x,%u)", W(dcv), pathType);
}

uint __cdecl FakeMissionRoute(DCVars_tag *dcv) {
    ShadowLog("missionroute(%x)", W(dcv));
    static const uint answers[] = {0, 1, 2, 3, 3, 9, 0xa, 0xc, 4};
    return answers[Next(sizeof(answers) / sizeof(answers[0]))];
}

bool __cdecl FakeOpponentHasWeapon(DCVars_tag *dcv) {
    ShadowLog("opponenthasweapon(%x)", W(dcv));
    return Next(2) != 0;
}

bool __cdecl FakeObjectIsArmed(obj_tag *obj) {
    ShadowLog("objectisarmed(%x)", W(obj));
    return Next(2) != 0;
}

bool __cdecl FakeTalk(DCVars_tag *dcv) {
    ShadowLog("talk(%x)", W(dcv));
    return Next(2) != 0;
}

void __cdecl FakeFire(char restart, DCVars_tag *dcv) {
    ShadowLog("fire(%d,%x)", restart, W(dcv));
}

void __cdecl FakeLogDeath(DCVars_tag *dcv) {
    ShadowLog("logdeath(%x)", W(dcv));
}

void __cdecl FakeDropWeapon(DCVars_tag *dcv) {
    ShadowLog("dropweapon(%x)", W(dcv));
}

void __cdecl FakeEnable(char enable, Drone_tag *drone) {
    ShadowLog("enable(%d,%x)", enable, W(drone));
}

bool __cdecl FakeDeathChannel(Drone_tag *drone) {
    ShadowLog("deathchannel(%x)", W(drone));
    return true;
}

bool __cdecl FakeFindSafety(DCVars_tag *dcv) {
    ShadowLog("findsafety(%x)", W(dcv));
    return Next(2) != 0;
}

double __cdecl FakeDistanceToAIPoint(DCVars_tag *dcv, AIPoint_tag *point) {
    ShadowLog("distancetoaipoint(%x,%x)", W(dcv), W(point));
    return 2.5;
}

uint __cdecl FakeMoveToGoal(DCVars_tag *dcv, CelPos_tag *dest, float radius, ushort clearFlags) {
    ShadowLog("movetogoal(%x,%x,%08x,%x)", W(dcv), W(dest), Bits(radius), clearFlags);
    return Next(15);
}

bool __cdecl FakeReFindMissionPath(DCVars_tag *dcv) {
    ShadowLog("refindmissionpath(%x)", W(dcv));
    return Next(2) != 0;
}

void __cdecl FakeDcvCall(DCVars_tag *dcv) {
    ShadowLog("dcvcall(%x)", W(dcv));
}

bool __cdecl FakeDoorIsOpen(DCVars_tag *dcv) {
    ShadowLog("doorisopen(%x)", W(dcv));
    return Next(2) != 0;
}

void __cdecl FakeFailReason(Drone_tag *drone, uint reason) {
    ShadowLog("failreason(%x,%u)", W(drone), reason);
}

void __cdecl FakeInvalidateRoute(AIRoute_tag *route) {
    ShadowLog("invalidateroute(%x)", W(route));
}

// Ours takes no fifth argument (the original pushes one it does not read)
bool __cdecl FakeGoalToObj(AITarget_tag *target, AIPoint_tag *point, obj_tag *obj, float radius) {
    ShadowLog("goaltoobj(%x,%x,%x,%08x)", W(target), W(point), W(obj), Bits(radius));
    return true;
}

void __cdecl FakeSetupGoal(AITarget_tag *target, AIPoint_tag *point, CelPos_tag *celPos, float radius) {
    ShadowLog("setupgoal(%x,%x,%08x,%08x,%08x,%x,%08x)", W(target), W(point), Bits(celPos->pos.x),
              Bits(celPos->pos.y), Bits(celPos->pos.z), W(celPos->cel), Bits(radius));
}

bool __cdecl FakeCanReachGoal(DCVars_tag *dcv) {
    ShadowLog("canreachgoal(%x)", W(dcv));
    return Next(2) != 0;
}

void __cdecl FakeDroneCall(Drone_tag *drone) {
    ShadowLog("dronecall(%x)", W(drone));
}

bool __cdecl FakeTestEndAssist(Drone_tag *drone) {
    ShadowLog("testendassist(%x)", W(drone));
    return Next(2) != 0;
}

char __cdecl FakeHeldWeapon(DCVars_tag *dcv) {
    ShadowLog("heldweapon(%x)", W(dcv));
    static const char answers[] = {0, 1, 2, 3, -1};
    return answers[Next(sizeof(answers))];
}

bool __cdecl FakeIsCivilian(DCVars_tag *dcv) {
    ShadowLog("iscivilian(%x)", W(dcv));
    return Next(4) == 0;
}

bool __cdecl FakeNearArmedDrone(DCVars_tag *dcv, float radius, char check) {
    ShadowLog("neararmeddrone(%x,%08x,%d)", W(dcv), Bits(radius), check);
    return Next(3) == 0;
}

bool __cdecl FakeNotNearArmedDrone(DCVars_tag *dcv, float radius) {
    ShadowLog("notneararmeddrone(%x,%08x)", W(dcv), Bits(radius));
    return Next(2) != 0;
}

bool __cdecl FakeFindRunToPoint(DCVars_tag *dcv) {
    ShadowLog("findruntopoint(%x)", W(dcv));
    return Next(2) != 0;
}

bool __cdecl FakeWithinAngle(DCVars_tag *dcv, float angle) {
    ShadowLog("withinangle(%x,%08x)", W(dcv), Bits(angle));
    return Next(3) != 0;
}

double __cdecl FakePathDistance(DCVars_tag *dcv, obj_tag *obj, float radius) {
    ShadowLog("pathdistance(%x,%x,%08x)", W(dcv), W(obj), Bits(radius));
    static const double answers[] = {0.0, 0.0, -0.0, -9.5, -9.0, -7.0, -6.9, 0.5, 3.0, -0.25, NAN};
    return answers[Next(sizeof(answers) / sizeof(answers[0]))];
}

void __cdecl FakeMoveToRouteNode(DCVars_tag *dcv, int node, AIRoute_tag *route) {
    ShadowLog("movetoroutenode(%x,%d,%x)", W(dcv), node, W(route));
}

uint __cdecl FakeMoveToObject(DCVars_tag *dcv, obj_tag *obj, float radius, char run) {
    ShadowLog("movetoobject(%x,%x,%08x,%d)", W(dcv), W(obj), Bits(radius), run);
    return Next(13);
}

bool __cdecl FakeCoverNodeOK(DCVars_tag *dcv, CoverNode_tag *node) {
    ShadowLog("covernodeok(%x,%x)", W(dcv), W(node));
    return Next(4) != 0;
}

// Moves the position by the side and a stream number, so the corners' distances come out either way
void __cdecl FakeCornerCover(CoverNode_tag *node, int side, _VECTOR *pos, _VECTOR *rot, int unused) {
    ShadowLog("cornercover(%x,%d,%x,%x,%d)", W(node), side, W(pos), W(rot), unused);
    pos->x = node->pos.x + (float)(int)Next(5) - 2.0f;
    pos->y = node->pos.y;
    pos->z = node->pos.z + (float)side * 0.5f;
    if (rot != NULL)
        rot->y = (float)side;
}

void __cdecl FakeTakeCover(DCVars_tag *dcv, uint side) {
    ShadowLog("takecover(%x,%u)", W(dcv), side);
}

bool __cdecl FakeSoundFinished(DYNAMICSOUNDS *handle) {
    ShadowLog("soundfinished(%x)", W(handle));
    return Next(2) != 0;
}

bool __cdecl FakeMessage(obj_tag *obj, uint msgType, void *extraData, uint delay) {
    ShadowLog("message(%x,%x,%x,%u)", W(obj), msgType, W(extraData), delay);
    return true;
}

void __cdecl FakeSendMsg(uint msgType, uint scope, uint sender, int receiver) {
    ShadowLog("sendmsg(%x,%u,%u,%d)", msgType, scope, sender, receiver);
}

void __cdecl FakeSendMsgSelf(uint msgType, void *extraData, uint delay, uint scope, DCVars_tag *dcv) {
    ShadowLog("sendmsgself(%x,%x,%u,%u,%x)", msgType, W(extraData), delay, scope, W(dcv));
}

void __cdecl FakeMusic(int event, int param) {
    ShadowLog("music(%d,%d)", event, param);
}

bool __cdecl FakeNearArmedPerson(DCVars_tag *dcv) {
    ShadowLog("neararmedperson(%x)", W(dcv));
    return Next(2) != 0;
}

bool __cdecl FakeCanWalkStraight(Drone_tag *drone, obj_tag *obj, CelPos_tag *pos, char strict) {
    ShadowLog("canwalkstraight(%x,%x,%08x,%08x,%x,%d)", W(drone), W(obj), Bits(pos->pos.x), Bits(pos->pos.z),
              W(pos->cel), strict);
    if (pos->cel == NULL)
        pos->cel = (cel_tag *)&world.cels[2];
    return Next(3) == 0;
}

bool __cdecl FakeLinkCreepCalc(AIRoute_tag *route, char fromEnd) {
    ShadowLog("linkcreepcalc(%x,%d,%d)", W(route), fromEnd, route->current);
    return Next(2) != 0;
}

bool __cdecl FakeLinkCreepDest(AIRoute_tag *route, short offset, CelPos_tag *out) {
    ShadowLog("linkcreepdest(%x,%d,%x)", W(route), offset, W(out));
    out->pos.x = (float)route->current;
    out->cel = (cel_tag *)&world.cels[3];
    return true;
}

void __cdecl FakeCalcDistanceAndDir(DCVars_tag *dcv, CelPos_tag *target) {
    ShadowLog("calcdistanceanddir(%x,%x)", W(dcv), W(target));
}

AIPath_tag *__cdecl FakeNavPath(_VECTOR *pos, cel_tag *cel, char moveTest) {
    ShadowLog("navpath(%x,%x,%d)", W(pos), W(cel), moveTest);
    return &world.navPath;
}

void __cdecl FakeResetRoute(AIRoute_tag *route, obj_tag *mover, obj_tag *targetObj) {
    ShadowLog("resetroute(%x,%x,%x)", W(route), W(mover), W(targetObj));
}

// Every group's fakes: state changes, anims, impacts, alerts, facing, the player, speech
void InstallCommon(bool playerAlwaysThere) {
    ShadowHook_Install(0x0004e320, (const void *)&FakeSetState);
    ShadowHook_Install(0x000341c0, (const void *)&FakeCallAnim);
    ShadowHook_Install(0x00035310, (const void *)&FakeStandIdle);
    ShadowHook_Install(0x00035dc0, (const void *)&FakeSetDAnimNow);
    ShadowHook_Install(0x0003e6a0, (const void *)&FakeHandleImpact);
    ShadowHook_Install(0x00038670, (const void *)&FakeConsiderExplosive);
    ShadowHook_Install(0x0003b5d0, (const void *)&FakeForcedAttack);
    ShadowHook_Install(0x00065220, (const void *)&FakeEnemyAlerts);
    ShadowHook_Install(0x00065360, (const void *)&FakeLookForOpponent);
    ShadowHook_Install(0x00043c60, (const void *)&FakeSetAngleToObj);
    ShadowHook_Install(0x00039f60, (const void *)&FakeSeenAndAttacking);
    ShadowHook_Install(0x00031d30, (const void *)&FakeAlertStatusSet);
    ShadowHook_Install(0x00035f90, playerAlwaysThere ? (const void *)&FakePlayerPresent : (const void *)&FakePlayer);
    ShadowHook_Install(0x00037530, (const void *)&FakeSpeechSFX);
    ShadowHook_Install(0x00037340, (const void *)&FakePlaySFX);
}

// ---- the made-up world

const HASHCODE kLevels[] = {
    HT_Level_HendersonA, HT_Level_HendersonB, HT_Level_CastleExterior, HT_Level_CastleIndoors1, HT_Level_TowerA,
    HT_Level_TowerB, HT_Level_Tower2Elevator, HT_Level_EvilBaseC, HT_Level_EvilBase,
};

float Pick(const float *values, int count) {
    if (ShadowRand(16) == 0)
        return NAN;
    return values[ShadowRand(count)];
}

void RandomObj(obj_tag *o, Drone_tag *d, AnimState *anim) {
    memset(o, 0, sizeof(*o));
    o->extraObjectData = d;
    o->objectType = d != NULL ? OBJECTTYPE_DRONE : OBJECTTYPE_PLAYER;
    o->position.x = ShadowRandFloat(-4.0f, 4.0f);
    o->position.y = ShadowRandFloat(-3.0f, 3.0f);
    o->position.z = ShadowRandFloat(-4.0f, 4.0f);
    o->rotation.y = ShadowRandFloat(-3.0f, 3.0f);
    o->inCel = ShadowRand(3) ? (cel_tag *)&world.cels[0] : NULL;
    o->effectFlags = ShadowRand(4) << 4;
    o->flags = (char)ShadowRand(4);
    o->drawnCountdown = (char)ShadowRand(3);
    o->animState = anim;
    if (anim != NULL) {
        memset(anim, 0, sizeof(*anim));
        anim->animFlags = (char)(ShadowRand(2) ? ANIM_CONTACT_FLOOR : 0) | (char)ShadowRand(4);
    }
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    memset(d, 0, sizeof(*d));
    RandomObj(o, d, &world.anims[i]);
    d->gameObj = o;
    d->next = i + 1 < DRONES ? &world.drones[i + 1] : NULL;
    d->prev = i > 0 ? &world.drones[i - 1] : NULL;

    static const uchar dtypes[] = {0, 4, 5, 5, 9, 0xc, 0x1b, 0x1c};
    d->dtype = dtypes[ShadowRand(sizeof(dtypes))];
    d->dtypeBase = (uchar)ShadowRand(8);
    static const short skinClasses[] = {0, 9, 13, 19};
    d->skinClass = skinClasses[ShadowRand(sizeof(skinClasses) / sizeof(skinClasses[0]))];
    d->animSet = (short)ShadowRand(10);
    d->savedAnimSet = (short)ShadowRand(10);
    d->flags = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    d->flags2 = ShadowRand(0x10000);
    d->sightFlags = ShadowRand(0x20);
    static const float alertnesses[] = {0.0f, 0.5f, 0.65999997f, 0.66f, 0.66000003f, 1.0f};
    d->alertness = Pick(alertnesses, 6);
    static const float distances[] = {0.5f, 3.9999998f, 4.0f, 4.5f, 5.9999995f, 6.0f, 8.0f, 12.0f};
    d->distanceToTarget = Pick(distances, 8);
    d->interrogateNearest = ShadowRand(3) ? d->distanceToTarget + ShadowRandFloat(-5.0f, 1.0f) : 4.0f;
    d->interrogateDistance = ShadowRandFloat(0.0f, 10.0f);
    d->viewAngle = 1.0f;
    d->unknown33 = (uchar)ShadowRand(2);
    d->gunThroughWall = (uchar)ShadowRand(2);
    d->firing = (uchar)ShadowRand(2);
    d->flies = (uchar)ShadowRand(2);
    d->explosiveSeen = (uchar)ShadowRand(2);
    d->explosiveTimeout = ShadowRand(3) ? 12000 + ShadowRand(100) : 0xfffffff0u;
    d->moveTarget.y = ShadowRandFloat(-3.0f, 3.0f);
    d->opponentLastPos.y = ShadowRandFloat(-3.0f, 3.0f);
    d->velocity.x = ShadowRandFloat(-1.0f, 1.0f);
    d->velocity.y = ShadowRandFloat(-1.0f, 1.0f);
    d->velocity.z = ShadowRandFloat(-1.0f, 1.0f);
    d->floatAcceleration = ShadowRandFloat(0.0f, 1.0f);
    d->playScript = (HASHCODE)(ShadowRand(2) ? 0x0600021f : 0);
    d->deathScript = (HASHCODE)(ShadowRand(3) ? 0 : 0x06000220);
    d->chatSwitchChannel = (uchar)(ShadowRand(2) ? 0x19 : 0x11);
    d->modeChangeSwitchChannel = (uchar)(ShadowRand(3) == 0 ? ShadowRand(256) : 0);
    d->secondState = (short)(ShadowRand(3) == 0 ? 0 : ShadowRand(250));
    d->initialState = (short)ShadowRand(250);
    d->returnState = (short)(ShadowRand(2) ? DSTATE_RunToAlarm : ShadowRand(250));
    d->health = ShadowRand(4) ? ShadowRandFloat(-10.0f, 100.0f) : (ShadowRand(2) ? 0.0f : NAN);
    d->unknown3ca = (ushort)ShadowRand(4);
    d->speechChannel = ShadowRand(2) ? (DYNAMICSOUNDS *)&world.cels[1] : NULL;
    static const uint scratches[] = {0, 0, 1, 2, 3, 4, 5, 6, 7, 0x19, 0x99, 300};
    d->stateScratch = scratches[ShadowRand(sizeof(scratches) / sizeof(scratches[0]))];
    d->stateScratch2 = ShadowRand(2);
    d->animScript = ShadowRand(3) ? &world.scripts[0] : NULL;
    d->unknown430 = ShadowRand(2) ? &world.scripts[1] : NULL;
    d->coverNode = ShadowRand(2) ? &world.cover[ShadowRand(COVER)] : NULL;
    d->route2.count = (ushort)ShadowRand(NODES + 1);
    static const uchar statuses[] = {0, 1, 9, 0xc, 3};
    d->route2.status = statuses[ShadowRand(sizeof(statuses))];
    static const float remainings[] = {10.0f, 15.0f, 15.000001f, 20.0f, 24.999998f, 25.0f, 30.0f};
    d->route2.remaining = Pick(remainings, 7);
    d->route2.nodes = world.routeNodes;
    d->route2.path = &world.path;
    d->route2.current = (short)ShadowRand(NODES);

    for (int w = 0; w < 3; w++) {
        d->behaviour1[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
        d->behaviour2[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    }
    d->currentBehaviour = ShadowRand(2) ? d->behaviour1 : d->behaviour2;
    // The properties these states test, either way
    static const uint props[] = {DRONE_PROP_0F, DRONE_PROP_10, DRONE_PROP_1D, DRONE_PROP_3C,
                                 DRONE_PROP_4C, DRONE_PROP_51, DRONE_PROP_53};
    for (uint p : props)
        behaviour_util_setProperty(p, d->currentBehaviour, ShadowRand(3) != 0);

    d->sm.id = 100 + i;
    static const uint curStates[] = {DSTATE_WaitSwitch, DSTATE_Idle, DSTATE_AllyLead, DSTATE_Hostage, 200};
    d->sm.curState = curStates[ShadowRand(sizeof(curStates) / sizeof(curStates[0]))];
    d->sm.prevState = ShadowRand(250);
    d->sm.scratch1c = ShadowRand(3);
    d->sm.scratch20 = ShadowRand(0x10000);
    d->sm.stateParam = ShadowRand(2) ? 0 : (int)ShadowRand(256);
    d->timerA.on = (uchar)ShadowRand(2);
    d->timerA.dueFrame = ShadowRand(5000);
    d->timerA.unknown8 = ShadowRand(5);
}

void RandomWorld() {
    for (int i = 0; i < DRONES; i++)
        RandomDrone(i);
    // The pairs a hostage killer and its hostage make, or none
    for (int i = 0; i < DRONES; i++)
        world.drones[i].unknown230 = ShadowRand(3) ? &world.objs[ShadowRand(DRONES)] : NULL;
    for (int i = 0; i < DRONES; i++) {
        world.drones[i].opponent = ShadowRand(5) ? &world.player : (ShadowRand(2) ? &world.objs[2] : NULL);
        world.drones[i].alertedBy = ShadowRand(3) ? &world.drones[ShadowRand(DRONES)] : NULL;
    }
    RandomObj(&world.player, NULL, &world.anims[DRONES]);
    // Around the distances the states test, from the dcv's drone
    if (ShadowRand(2)) {
        static const float gaps[] = {2.9999998f, 3.0f, 3.4999998f, 3.5f, 3.9999998f, 4.0f, 4.9999995f, 5.0f,
                                     7.9999995f, 8.0f};
        world.player.position = world.objs[0].position;
        world.player.position.x += gaps[ShadowRand(sizeof(gaps) / sizeof(gaps[0]))];
    }
    RandomObj(&world.explosive, NULL, NULL);
    world.explosive.objectType = OBJECTTYPE_BULLET;

    for (int s = 0; s < 2; s++) {
        memset(&world.scripts[s], 0, sizeof(world.scripts[s]));
        world.scripts[s].numFrames = (short)(ShadowRand(300));
        world.scripts[s].curFrame = ShadowRandFloat(0.0f, 100.0f);
        world.scripts[s].speed = 1.0f;
    }
    memset(world.cover, 0, sizeof(world.cover));
    for (int c = 0; c < COVER; c++) {
        CoverNode_tag *n = &world.cover[c];
        n->type = (uchar)(ShadowRand(4) == 0 ? 2 : ShadowRand(2));
        n->flags = ShadowRand(3) == 0 ? 0x10 : ShadowRand(4);
        n->cel = (cel_tag *)&world.cels[ShadowRand(3)];
        n->occupant = ShadowRand(2) ? &world.objs[1] : NULL;
        n->pos.x = world.objs[0].position.x + ShadowRandFloat(-2.5f, 2.5f);
        n->pos.y = world.objs[0].position.y;
        n->pos.z = world.objs[0].position.z + ShadowRandFloat(-1.0f, 1.0f);
    }
    memset(&world.path, 0, sizeof(world.path));
    memset(&world.navPath, 0, sizeof(world.navPath));
    world.path.fileNodes = world.fileNodes;
    for (int n = 0; n < NODES; n++) {
        world.routeNodes[n] = (ushort)ShadowRand(NODES);
        memset(&world.fileNodes[n], 0, sizeof(world.fileNodes[n]));
        world.fileNodes[n].node.cel = ShadowRand(3) ? (cel_tag *)&world.cels[ShadowRand(2)] : NULL;
        world.fileNodes[n].node.pos.x = world.objs[0].position.x + ShadowRandFloat(-25.0f, 25.0f);
        world.fileNodes[n].node.pos.y = ShadowRandFloat(-2.0f, 2.0f);
        world.fileNodes[n].node.pos.z = world.objs[0].position.z + ShadowRandFloat(-25.0f, 25.0f);
    }

    world.dcv.gameObj = &world.objs[0];
    world.dcv.drone = &world.drones[0];
    world.dcv.cel = NULL;
    world.dcv.aiStateMachine = &world.drones[0].sm;
    // Now and then a handler is given another drone or object than the dcv's, to tell which one it reads
    world.droneArg = ShadowRand(5) ? &world.drones[0] : &world.drones[1];
    // NDrone2_DSTATE_Interogate tests the given drone's opponent and then copies the dcv drone's opponent's position;
    // the game never lets the two drones differ, and the original reads through NULL when only the dcv drone has none
    if (world.dcv.drone->opponent == NULL)
        world.droneArg->opponent = NULL;
    world.gameObjArg = ShadowRand(5) ? &world.objs[0] : &world.objs[1];

    for (int s = 0; s < STREAM; s++)
        world.stream[s] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    world.streamPos = 0;
    world.playerThere = ShadowRand(8) != 0;

    static const uint frames[] = {1000, 12345, 0x7ffffff0, 0x80000010, 0xfffffff0};
    GameState.NumFramesUnpaused = frames[ShadowRand(sizeof(frames) / sizeof(frames[0]))] + ShadowRand(50);
    GameState.CurrentLevelHashcode = kLevels[ShadowRand(sizeof(kLevels) / sizeof(kLevels[0]))];
    if (ShadowRand(2)) {
        FRAME_RATE_INT = 60;
        FRAME_RATE_DIV = 1.0f;
        FRAME_RATE_MUL = 1.0f;
    } else {
        FRAME_RATE_INT = 50;
        FRAME_RATE_DIV = 1.2f;
        FRAME_RATE_MUL = 0.8333333f;
    }
    NPCGlobals.NDrone2List = &world.drones[0];
    NPCGlobals.coverNodes = ShadowRand(8) ? world.cover : NULL;
    NPCGlobals.numCoverNodes = ShadowRand(COVER);
    NPCGlobals.prevSeenAttacking = ShadowRand(2);
    NPCGlobals.deadFrames = ShadowRand(2) ? 600 : 0xfffffff0u + ShadowRand(32);
    NPCGlobals.hostagesSaved = ShadowRand(3);
    for (int c = 0; c < 256; c++)
        switch_channels[c] = (char)(ShadowRand(3) == 0);
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

void RandomMessage() {
    // the messages most states act on
    static const uint common[] = {DRONE_MSG_Enter, DRONE_MSG_Update, DRONE_MSG_TimerA, DRONE_MSG_Exit,
                                  DRONE_MSG_ScriptEnd};
    memset(&world.msg, 0, sizeof(world.msg));
    switch (ShadowRand(10)) {
    case 0:
        world.msg.msgType = ShadowRand(2) ? DRONE_MSG_BotStateChanged : 0x40;
        break;
    case 1:
        world.msg.msgType = ShadowRand(2) ? 0xffffffffu : 0x80000003u;
        break;
    case 2:
    case 3:
        world.msg.msgType = common[ShadowRand(5)];
        break;
    default:
        world.msg.msgType = ShadowRand(0x25);
        break;
    }
    world.msg.sender = 100 + ShadowRand(DRONES);
    world.msg.receiver = 100;
    world.msg.createdFrame = GameState.NumFramesUnpaused;
    world.msg.handleOnFrame = GameState.NumFramesUnpaused;
    // Timer A's message carries the timer's data (Tester1)
    world.msg.extraData = ShadowRand(3) ? (void *)&world.explosive : (void *)(uintptr_t)ShadowRand(4);
}

// The hostage pairs as the game keeps them: where a handler reads the partner without testing it, it has one
void HostageFixups() {
    uint m = world.msg.msgType;
    if (m == DRONE_MSG_AttackNow || m == DRONE_MSG_ScriptEnd || world.drones[0].unknown3ca == 2)
        if (world.drones[0].unknown230 == NULL)
            world.drones[0].unknown230 = &world.objs[1];
}

// The interrogation's steps run on Timer A, one per message, each level with its own script
void InterrogationFixups() {
    if (ShadowRand(3) == 0) {
        world.msg.msgType = DRONE_MSG_TimerA;
        world.drones[0].stateScratch = ShadowRand(7);
    }
}

// ---- the handlers

struct Handler {
    const char *name;
    unsigned at;
    DroneStateFunc ours;
};

const Handler *current;

uint32_t RunTheirs() {
    XbeOriginalScope o(current->at);
    return ORIGINAL(current->at, uint32_t(__cdecl *)(DCVars_tag *, Drone_tag *, obj_tag *, MsgObject *))(
               &world.dcv, world.droneArg, world.gameObjArg, &world.msg) & 0xff;
}

uint32_t RunOurs() {
    return (uint32_t)current->ours(&world.dcv, world.droneArg, world.gameObjArg, &world.msg);
}

void TestHandlers(const char *group, const Handler *handlers, int count, int trials, void (*fixups)(void) = NULL) {
    int groupCases = cases, groupDiffer = differ;
    for (int h = 0; h < count; h++) {
        current = &handlers[h];
        for (int trial = 0; trial < trials; trial++) {
            RandomWorld();
            RandomMessage();
            if (fixups != NULL)
                fixups();
            cases++;
            if (!ShadowCompare(regions, "dronerole", current->name, trial, RunTheirs, RunOurs))
                differ++;
        }
    }
    printf("[dronerole] %s: %d cases, %d differ\n", group, cases - groupCases, differ - groupDiffer);
}

#define H(name, at) {#name, at, name}

const Handler kTruck[] = {
    H(NDrone2_DSTATE_TruckDriverInit, 0x00053c00),
    H(NDrone2_DSTATE_TruckDriverInitAlert, 0x00053e10),
    H(NDrone2_DSTATE_TruckDriverIdle, 0x00053f80),
    H(NDrone2_DSTATE_TruckDriverMission, 0x00054130),
};

const Handler kAbseil[] = {
    H(NDrone2_DSTATE_AbseilInit, 0x0005bf10),
    H(NDrone2_DSTATE_AbseilSlide, 0x0005c070),
    H(NDrone2_DSTATE_AbseilHang, 0x0005c260),
    H(NDrone2_DSTATE_AbseilStepOff, 0x0004caf0),
    H(NDrone2_DSTATE_AbseilDeath, 0x0004cbf0),
};

const Handler kScripted[] = {
    H(NDrone2_DSTATE_RunAwayFromObject, 0x0005b3d0),
    H(NDrone2_DSTATE_HideFromScaryObject, 0x0005b6a0),
    H(NDrone2_DSTATE_RecoverFromScaryObject, 0x0005b860),
    H(NDrone2_DSTATE_ElevatorJumper, 0x0005bdd0),
    H(NDrone2_DSTATE_HoldItRightThere, 0x0004c7f0),
    H(NDrone2_DSTATE_FailMission, 0x0005fc40),
    H(NDrone2_DSTATE_StandBlind, 0x0004b590),
    H(NDrone2_DSTATE_HangUp, 0x0004db90),
};

const Handler kInterrogation[] = {
    H(NDrone2_DSTATE_InterogateAssist, 0x00054840),
    H(NDrone2_DSTATE_InterogateAssistWait, 0x00054990),
    H(NDrone2_DSTATE_Interogator, 0x00054b80),
    H(NDrone2_DSTATE_Interogate, 0x00054bb0),
    H(NDrone2_DSTATE_InterogateWalk, 0x00055240),
    H(NDrone2_DSTATE_CivilianChallenge, 0x0004b840),
};

const Handler kAllies[] = {
    H(NDrone2_DSTATE_KikoMission, 0x000513c0),
    H(NDrone2_DSTATE_KikoMissionRun, 0x000515b0),
    H(NDrone2_DSTATE_AllyLead, 0x00051bc0),
    H(NDrone2_DSTATE_AllyLeadPlayerInWay, 0x00051ec0),
    H(NDrone2_DSTATE_AllyLeadHide, 0x00052050),
    H(NDrone2_DSTATE_AllyLeadWait, 0x000521e0),
    H(NDrone2_DSTATE_AllyLeadMissionWait, 0x000523a0),
    H(NDrone2_DSTATE_AllyLeadBondCombat, 0x000524e0),
    H(NDrone2_DSTATE_AllyFollowInit, 0x00052660),
    H(NDrone2_DSTATE_AllyFollow, 0x00052760),
    H(NDrone2_DSTATE_AllyFollowWait, 0x000528c0),
    H(NDrone2_DSTATE_AllyFollowDone, 0x0004b5c0),
};

const Handler kOthers[] = {
    H(NDrone2_DSTATE_CastleChatGuard1, 0x000543c0),
    H(NDrone2_DSTATE_AmbushInit, 0x00054650),
    H(NDrone2_DSTATE_AmbushWait, 0x000546b0),
    H(NDrone2_DSTATE_Tester1, 0x0004d950),
    H(NDrone2_DSTATE_Tester2, 0x0005fc80),
    H(NDrone2_DSTATE_Tester3, 0x0004daf0),
    H(NDrone2_DSTATE_Tester4, 0x0005fcd0),
};

const Handler kHostage[] = {
    H(NDrone2_DSTATE_HostageKiller, 0x0004fa40),
    H(NDrone2_DSTATE_HostageKillerAttack, 0x0004fcc0),
    H(NDrone2_DSTATE_Hostage, 0x0004fea0),
    H(NDrone2_DSTATE_HostageDie, 0x0004b420),
    H(NDrone2_DSTATE_HostageSaved, 0x00050000),
    H(NDrone2_DSTATE_HostageIdle, 0x00050150),
    H(NDrone2_DSTATE_HostageHide, 0x000502e0),
    H(NDrone2_DSTATE_HostageDead, 0x00050440),
};

#define COUNT(a) (int)(sizeof(a) / sizeof(a[0]))

// ---- the helpers, each with its own arguments

struct Helper {
    const char *name;
    unsigned at;
    uint32_t (*theirs)(void);
    uint32_t (*ours)(void);
};

float bondRadius;

const Helper kHelpers[] = {
    {"NDrone2_ReFindMissionPath", 0x00047290,
     [] {
         XbeOriginalScope o(0x00047290);
         return ORIGINAL(0x00047290, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
     },
     [] { return (uint32_t)NDrone2_ReFindMissionPath(&world.dcv); }},
    {"NDrone2_FindAmbushCover", 0x000467e0,
     [] {
         XbeOriginalScope o(0x000467e0);
         return ORIGINAL(0x000467e0, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
     },
     [] { return (uint32_t)NDrone2_FindAmbushCover(&world.dcv); }},
    {"NDrone2_SetupHostageExecute", 0x00039a30,
     [] {
         XbeOriginalScope o(0x00039a30);
         ORIGINAL(0x00039a30, void(__cdecl *)(DCVars_tag *))(&world.dcv);
         return 0u;
     },
     [] {
         NDrone2_SetupHostageExecute(&world.dcv);
         return 0u;
     }},
    {"NDrone2_SetupHostageKiller", 0x00039b10,
     [] {
         XbeOriginalScope o(0x00039b10);
         return ORIGINAL(0x00039b10, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
     },
     [] { return (uint32_t)NDrone2_SetupHostageKiller(&world.dcv); }},
    {"NDrone2_MonitorTalkToMissionObj", 0x00039cc0,
     [] {
         XbeOriginalScope o(0x00039cc0);
         return ORIGINAL(0x00039cc0, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
     },
     [] { return (uint32_t)NDrone2_MonitorTalkToMissionObj(&world.dcv); }},
    {"NDrone2_TalkToMissionObj", 0x00039da0,
     [] {
         XbeOriginalScope o(0x00039da0);
         return ORIGINAL(0x00039da0, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
     },
     [] { return (uint32_t)NDrone2_TalkToMissionObj(&world.dcv); }},
    {"NDrone2_NearArmedBond", 0x00065f90,
     [] {
         XbeOriginalScope o(0x00065f90);
         return ORIGINAL(0x00065f90, uint32_t(__cdecl *)(DCVars_tag *, float))(&world.dcv, bondRadius) & 0xff;
     },
     [] { return (uint32_t)NDrone2_NearArmedBond(&world.dcv, bondRadius); }},
};

void TestHelpers(int trials) {
    int groupCases = cases, groupDiffer = differ;
    for (const Helper &h : kHelpers) {
        for (int trial = 0; trial < trials; trial++) {
            RandomWorld();
            // A position on a node now and then, so equal distances come up
            if (ShadowRand(4) == 0)
                world.objs[0].position = world.fileNodes[ShadowRand(NODES)].node.pos;
            if (world.drones[0].unknown3ca == 2 && world.drones[0].unknown230 == NULL)
                world.drones[0].unknown230 = &world.objs[1];
            static const float radii[] = {4.0f, 6.0f, 6.0000005f, NAN};
            bondRadius = radii[ShadowRand(4)];
            cases++;
            if (!ShadowCompare(regions, "dronerole", h.name, trial, h.theirs, h.ours))
                differ++;
        }
    }
    printf("[dronerole] ReFindMissionPath, FindAmbushCover, the hostage helpers and NearArmedBond: %d cases, "
           "%d differ\n",
           cases - groupCases, differ - groupDiffer);
}

} // namespace

void DroneRoleShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], npc[sizeof(NPCGlobals_t)];
    static char channels[256];
    static uint32_t channelTimes[256];
    static uint32_t rand[2];
    uint32_t frameRate = FRAME_RATE_INT;
    float frameDiv = FRAME_RATE_DIV, frameMul = FRAME_RATE_MUL;
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
    regions.Add(&FRAME_RATE_MUL, 4);
    NfWarnMuted++;
    cases = differ = 0;
    const int trials = 2000;

    InstallCommon(false);
    ShadowHook_Install(0x00034680, (const void *)&FakeSetScript);
    ShadowHook_Install(0x00046ad0, (const void *)&FakeAssignAIPath);
    ShadowHook_Install(0x00046d40, (const void *)&FakeMissionRoute);
    ShadowHook_Install(0x00066610, (const void *)&FakeOpponentHasWeapon);
    ShadowHook_Install(0x00064be0, (const void *)&FakeObjectIsArmed);
    TestHandlers("truck driver states", kTruck, COUNT(kTruck), trials);
    ShadowHook_RemoveAll();

    InstallCommon(false);
    ShadowHook_Install(0x0003ba80, (const void *)&FakeTalk);
    ShadowHook_Install(0x000675f0, (const void *)&FakeFire);
    ShadowHook_Install(0x0003a1d0, (const void *)&FakeLogDeath);
    ShadowHook_Install(0x00067690, (const void *)&FakeDropWeapon);
    ShadowHook_Install(0x000376b0, (const void *)&FakeEnable);
    ShadowHook_Install(0x00036090, (const void *)&FakeDeathChannel);
    TestHandlers("abseil states", kAbseil, COUNT(kAbseil), trials);
    ShadowHook_RemoveAll();

    InstallCommon(false);
    ShadowHook_Install(0x0003be90, (const void *)&FakeTalk);
    ShadowHook_Install(0x000479a0, (const void *)&FakeFindSafety);
    ShadowHook_Install(0x00044e70, (const void *)&FakeDistanceToAIPoint);
    ShadowHook_Install(0x00046f30, (const void *)&FakeMoveToGoal);
    ShadowHook_Install(0x000330c0, (const void *)&FakeCanDoAnimState);
    ShadowHook_Install(0x00047290, (const void *)&FakeReFindMissionPath);
    ShadowHook_Install(0x00039df0, (const void *)&FakeDcvCall);
    ShadowHook_Install(0x00039ec0, (const void *)&FakeDcvCall);
    ShadowHook_Install(0x00039f10, (const void *)&FakeDoorIsOpen);
    ShadowHook_Install(0x00035fb0, (const void *)&FakeFailReason);
    TestHandlers("scary object, elevator, hold, fail, blind and hang up states", kScripted, COUNT(kScripted), trials);
    ShadowHook_RemoveAll();

    InstallCommon(false);
    ShadowHook_Install(0x000479f0, (const void *)&FakeInvalidateRoute);
    ShadowHook_Install(0x00049a20, (const void *)&FakeGoalToObj);
    ShadowHook_Install(0x00047050, (const void *)&FakeCanReachGoal);
    ShadowHook_Install(0x00064510, (const void *)&FakeDroneCall);
    ShadowHook_Install(0x00064520, (const void *)&FakeDroneCall);
    ShadowHook_Install(0x000644c0, (const void *)&FakeDroneCall);
    ShadowHook_Install(0x000644e0, (const void *)&FakeDroneCall);
    ShadowHook_Install(0x00064550, (const void *)&FakeTestEndAssist);
    ShadowHook_Install(0x00035f70, (const void *)&FakeHeldWeapon);
    ShadowHook_Install(0x00039f50, (const void *)&FakeIsCivilian);
    ShadowHook_Install(0x00066610, (const void *)&FakeOpponentHasWeapon);
    TestHandlers("interrogation and civilian challenge states", kInterrogation, COUNT(kInterrogation), trials,
                 InterrogationFixups);
    ShadowHook_RemoveAll();

    InstallCommon(false);
    ShadowHook_Install(0x00046d40, (const void *)&FakeMissionRoute);
    ShadowHook_Install(0x00064c10, (const void *)&FakeNearArmedDrone);
    ShadowHook_Install(0x00064dc0, (const void *)&FakeNotNearArmedDrone);
    ShadowHook_Install(0x00047190, (const void *)&FakeFindRunToPoint);
    ShadowHook_Install(0x00043f20, (const void *)&FakeWithinAngle);
    ShadowHook_Install(0x00044cb0, (const void *)&FakePathDistance);
    ShadowHook_Install(0x00047290, (const void *)&FakeReFindMissionPath);
    ShadowHook_Install(0x00046160, (const void *)&FakeMoveToRouteNode);
    ShadowHook_Install(0x00046d90, (const void *)&FakeMoveToObject);
    TestHandlers("Kiko and ally states", kAllies, COUNT(kAllies), trials);
    ShadowHook_RemoveAll();

    InstallCommon(false);
    ShadowHook_Install(0x00034680, (const void *)&FakeSetScript);
    ShadowHook_Install(0x00044250, (const void *)&FakeCoverNodeOK);
    ShadowHook_Install(0x00044490, (const void *)&FakeCornerCover);
    ShadowHook_Install(0x000464a0, (const void *)&FakeTakeCover);
    ShadowHook_Install(0x000479f0, (const void *)&FakeInvalidateRoute);
    ShadowHook_Install(0x00049980, (const void *)&FakeSetupGoal);
    TestHandlers("castle chat guard, ambush and test states", kOthers, COUNT(kOthers), trials);
    ShadowHook_RemoveAll();

    InstallCommon(false);
    ShadowHook_Install(0x000cbac0, (const void *)&FakeSoundFinished);
    ShadowHook_Install(0x00031830, (const void *)&FakeMessage);
    ShadowHook_Install(0x000317e0, (const void *)&FakeSendMsg);
    ShadowHook_Install(0x0004e890, (const void *)&FakeSendMsgSelf);
    ShadowHook_Install(0x000cbce0, (const void *)&FakeMusic);
    ShadowHook_Install(0x00039c10, (const void *)&FakeDcvCall);
    ShadowHook_Install(0x00064d40, (const void *)&FakeNearArmedPerson);
    ShadowHook_Install(0x00047190, (const void *)&FakeFindRunToPoint);
    ShadowHook_Install(0x00064c10, (const void *)&FakeNearArmedDrone);
    ShadowHook_Install(0x00038df0, (const void *)&FakeDcvCall);
    ShadowHook_Install(0x00038db0, (const void *)&FakeTalk);
    ShadowHook_Install(0x00035fb0, (const void *)&FakeFailReason);
    ShadowHook_Install(0x0003a1d0, (const void *)&FakeLogDeath);
    TestHandlers("hostage states", kHostage, COUNT(kHostage), trials, HostageFixups);
    ShadowHook_RemoveAll();

    InstallCommon(true);
    ShadowHook_Install(0x00045340, (const void *)&FakeCanWalkStraight);
    ShadowHook_Install(0x0004abb0, (const void *)&FakeLinkCreepCalc);
    ShadowHook_Install(0x0004a2e0, (const void *)&FakeLinkCreepDest);
    ShadowHook_Install(0x00046080, (const void *)&FakeCalcDistanceAndDir);
    ShadowHook_Install(0x000479f0, (const void *)&FakeInvalidateRoute);
    ShadowHook_Install(0x00049980, (const void *)&FakeSetupGoal);
    ShadowHook_Install(0x00046f30, (const void *)&FakeMoveToGoal);
    ShadowHook_Install(0x00049650, (const void *)&FakeNavPath);
    ShadowHook_Install(0x000482c0, (const void *)&FakeResetRoute);
    ShadowHook_Install(0x00047050, (const void *)&FakeCanReachGoal);
    ShadowHook_Install(0x00044250, (const void *)&FakeCoverNodeOK);
    ShadowHook_Install(0x00044490, (const void *)&FakeCornerCover);
    ShadowHook_Install(0x000464a0, (const void *)&FakeTakeCover);
    ShadowHook_Install(0x000cbac0, (const void *)&FakeSoundFinished);
    ShadowHook_Install(0x00031830, (const void *)&FakeMessage);
    ShadowHook_Install(0x00066610, (const void *)&FakeOpponentHasWeapon);
    ShadowHook_Install(0x00064be0, (const void *)&FakeObjectIsArmed);
    TestHelpers(trials * 2);
    ShadowHook_RemoveAll();

    NfWarnMuted--;
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(switch_channels, channels, sizeof(channels));
    memcpy(switch_channels_time, channelTimes, sizeof(channelTimes));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
    FRAME_RATE_INT = frameRate;
    FRAME_RATE_DIV = frameDiv;
    FRAME_RATE_MUL = frameMul;
}
