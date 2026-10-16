// Shadow tests for how the drones move (game/drone/DroneMove.cpp): each original and ours run on the same made-up
// drones, with what needs a level going to logging fakes. Run at start with MenuShadowTests=on (MenuProbe.cpp),
// before any level exists.
//
// Faked, so not tested here: the walk tests (NDrone2_MoveTest, the relative step test 0x453e0), the feet
// (NDrone2_FeetPos), the nav node search (NDrone2_NearestNode), the safety flood and its node (AINetwork_InitEmitter2,
// AINetwork_EmitPath, AINetwork_Emitter_GetNodeAtDistance), the routes (NDrone2_MoveToGoalPosition,
// AINetwork_SetupGoalPosition, AINetwork_ResetRoute, AINetwork_InvalidateRoute, AINetwork_GetRouteDistanceBetweenNodes,
// AINetwork_FurthestPosition), build_FindCel, the AI boxes (Drone_PointInAnyAIBox), the player (NDrone2_Player),
// state changes and anims. The vector helpers are the real ones. Random draws are kept in step: both sides start
// from the same Rand_Random state, and it is compared afterwards.
//
// NDrone2_Steer's obstacle record keeps its flags across obstacles, and the original never clears them first: it
// reads stack garbage. Its callers here (NDrone2_Steer, NDrone2_Move) run with the stack below them zeroed, as ours
// starts it, and our Vec_AngleDifference, which NDrone2_Move calls first, zeroes it again for them.

#include "DroneMoveShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/AINetwork.h"
#include "../engine/Collide.h"
#include "../game/drone/BOT.h"
#include "../game/drone/DroneMove.h"
#include "../game/drone/NDrone2.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/build.h"
#include "../math/math.h"

#include <bit>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords ((uint32_t *)0x0018cdf8)

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int kDrones = 4;
const int kPlayers = 3;
const int kNodes = 8;

struct World {
    obj_tag objs[kDrones];
    Drone_tag drones[kDrones];
    obj_tag players[kPlayers];
    BOT_vars_t botVars;
    AIRoute_tag route;
    AIPath_tag path;
    AIPathNodeRecord nodes[kNodes];
    ushort routeNodes[kNodes];
    MAPNODE_tag node;
    HITDATA_tag hit;
    uint8_t cel[0x40];
    uint8_t cel2[0x40];
    DCVars_tag dcv;

    // Arguments for the direct tests
    ObstacleEval_tag eval;
    SteerBody obstacle;
    _VECTOR vecs[6];
    float out[2];
    obj_tag *objArg;
    float floats[4];

    // What the fakes answer
    int player;             // NDrone2_Player: -1 for none
    uint moveResult;
    int emitterOk;
    uint emitResult;
    short nearestNode;
    int nodeOk;
    int nodeNull;
    int furthestOk;
    int inBox;
    uint moveTestBits;      // two bits a call
    uint moveTestCalls;
    uint relativeBits;      // one bit a call
    uint relativeCalls;
    float routeDistance;
};
World world;

ShadowRegions regions;
int cases, differ;

// DroneMove_NextRouteAngle's acos is the XBE's x87 __CIacos in the original and the host CRT's in ours: their doubles
// may be an ulp or two apart, which is accepted (and counted) rather than called a difference.
double theirAngle, ourAngle;
int acosUlps;

void CompareAngles(int trial) {
    uint64_t a = std::bit_cast<uint64_t>(theirAngle), b = std::bit_cast<uint64_t>(ourAngle);
    if (a == b)
        return;
    int64_t apart = (int64_t)(a - b);
    if (isfinite(theirAngle) && (a >> 63) == (b >> 63) && apart >= -2 && apart <= 2) {
        acosUlps++;
        return;
    }
    differ++;
    printf("[dronemove] DroneMove_NextRouteAngle case %d: returns %016llx, the original %016llx\n", trial,
           (unsigned long long)b, (unsigned long long)a);
}

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "dronemove", what, trial, theirs, ours))
        differ++;
}

uint32_t Bits(float f) {
    return std::bit_cast<uint32_t>(f);
}

uint32_t DoubleBits(double d) {
    uint64_t u = std::bit_cast<uint64_t>(d);
    return (uint32_t)u ^ (uint32_t)(u >> 32);
}

int Who(const Drone_tag *drone) {
    for (int i = 0; i < kDrones; i++)
        if (drone == &world.drones[i])
            return i;
    return drone == NULL ? -1 : 9;
}

int WhoObj(const obj_tag *obj) {
    for (int i = 0; i < kDrones; i++)
        if (obj == &world.objs[i])
            return i;
    for (int i = 0; i < kPlayers; i++)
        if (obj == &world.players[i])
            return 100 + i;
    return obj == NULL ? -1 : 102;
}

int WhoCel(const void *cel) {
    if (cel == world.cel)
        return 1;
    if (cel == world.cel2)
        return 2;
    return cel == NULL ? 0 : 9;
}

void LogVector(const char *name, const _VECTOR *v) {
    ShadowLog("%s=%08x,%08x,%08x", name, Bits(v->x), Bits(v->y), Bits(v->z));
}

// ---- the fakes

obj_tag *__cdecl FakePlayer(void) {
    ShadowLog("player");
    return world.player < 0 ? NULL : &world.players[world.player];
}

uint __cdecl FakeMoveToGoal(DCVars_tag *dcv, _VECTOR *pos, float radius, ushort flags) {
    LogVector("movetogoal", pos);
    ShadowLog("(%d,%08x,%u)", dcv == &world.dcv, Bits(radius), flags);
    return world.moveResult;
}

// The original pushes a sixth argument, which ours does not: not read
bool __cdecl FakeInitEmitter(obj_tag *obj, _VECTOR *pos, cel_tag *cel, AIEmitter_tag *emitter, AIPath_tag *path) {
    LogVector("initemitter", pos);
    ShadowLog("(%d,%d,%d,%d)", WhoObj(obj), WhoCel(cel), emitter == &NPCGlobals.safetyEmitter, path == &world.path);
    return world.emitterOk != 0;
}

uint __cdecl FakeEmitPath(AIEmitter_tag *emitter, float maxCost) {
    ShadowLog("emitpath(%d,%08x)", emitter == &NPCGlobals.safetyEmitter, Bits(maxCost));
    return world.emitResult;
}

short __cdecl FakeNearestNode(CelPos_tag *pos, AIPath_tag *path, obj_tag *obj) {
    LogVector("nearestnode", &pos->pos);
    ShadowLog("(%d,%d,%d)", WhoCel(pos->cel), path == &world.path, WhoObj(obj));
    return world.nearestNode;
}

bool __cdecl FakeNodeAtDistance(short node, float distance, AIEmitter_tag *emitter, MAPNODE_tag **found) {
    ShadowLog("nodeatdistance(%d,%08x,%d,%d)", node, Bits(distance), emitter == &NPCGlobals.safetyEmitter,
              *found == NULL);
    *found = world.nodeNull ? NULL : &world.node;
    return world.nodeOk != 0;
}

// The original pushes a fifth argument, which ours does not: not read
void __cdecl FakeSetupGoal(AITarget_tag *target, AIPoint_tag *point, CelPos_tag *celPos, float radius) {
    LogVector("setupgoal", &celPos->pos);
    ShadowLog("(%d,%d,%d,%08x)", target == &world.drones[0].goalTarget, point == &world.drones[0].aiPoint,
              WhoCel(celPos->cel), Bits(radius));
}

void __cdecl FakeResetRoute(AIRoute_tag *route, obj_tag *mover, obj_tag *targetObj) {
    ShadowLog("resetroute(%d,%d,%d)", route == &world.drones[0].route1, WhoObj(mover), WhoObj(targetObj));
}

void __cdecl FakeInvalidateRoute(AIRoute_tag *route) {
    ShadowLog("invalidateroute(%d)", route == &world.drones[0].route1);
}

cel_tag *__cdecl FakeFindCel(_VECTOR *pos, world_tag *where) {
    LogVector("findcel", pos);
    return (cel_tag *)world.cel2;
}

bool __cdecl FakeFurthest(_VECTOR *from, _VECTOR *to, _VECTOR *furthest) {
    LogVector("furthest", from);
    LogVector("to", to);
    furthest->x = (from->x + to->x) * 0.5f;
    furthest->y = to->y;
    furthest->z = (from->z + to->z) * 0.5f;
    return world.furthestOk != 0;
}

bool __cdecl FakeInBox(_VECTOR *pos, ushort type) {
    LogVector("inbox", pos);
    ShadowLog("(%u)", type);
    return world.inBox != 0;
}

bool __cdecl FakeSetState(StateMachineInfo_tag *sm, uint next, int param) {
    ShadowLog("setstate(%d,%u,%d)", sm == &world.drones[0].sm, next, param);
    return true;
}

bool __cdecl FakeCallAnim(uint state, uint variant, short endState, uint endMsg, float speed, int unused,
                          DCVars_tag *dcv) {
    ShadowLog("callanim(%u,%u,%d,%u,%08x,%d,%d)", state, variant, endState, endMsg, Bits(speed), unused,
              dcv == &world.dcv);
    return true;
}

bool __cdecl FakeMoveRelative(_VECTOR *offset, Drone_tag *drone, obj_tag *obj, char unscaled, int p5) {
    LogVector("moverel", offset);
    ShadowLog("(%d,%d,%d,%d)", Who(drone), WhoObj(obj), unscaled, p5);
    return (world.relativeBits >> (world.relativeCalls++ & 31)) & 1;
}

bool __cdecl FakeFeetPos(obj_tag *obj, _VECTOR *feet) {
    ShadowLog("feet(%d)", WhoObj(obj));
    feet->x = obj->position.x;
    feet->y = obj->position.y - 1.25f;
    feet->z = obj->position.z;
    return true;
}

char __cdecl FakeMoveTest(_VECTOR *from, cel_tag *fromCel, _VECTOR *to, cel_tag *toCel, obj_tag *objA, obj_tag *objB,
                          _VECTOR *hit, char strict, char checkBlocked) {
    LogVector("movetest", from);
    LogVector("to", to);
    ShadowLog("(%d,%d,%d,%d,%d,%d,%d)", WhoCel(fromCel), WhoCel(toCel), WhoObj(objA), WhoObj(objB), hit == NULL,
              strict, checkBlocked);
    return (char)((world.moveTestBits >> ((world.moveTestCalls++ * 2) & 31)) & 3);
}

double __cdecl FakeRouteDistance(AIRoute_tag *route, short from, short to) {
    ShadowLog("routedistance(%d,%d,%d)", route == &world.drones[0].route2, from, to);
    return (double)world.routeDistance + to;
}

void InstallFakes() {
    ShadowFakes_Install();
    ShadowHook_Install(0x00035f90, (const void *)&FakePlayer);
    ShadowHook_Install(0x00046f30, (const void *)&FakeMoveToGoal);
    ShadowHook_Install(0x0004a900, (const void *)&FakeInitEmitter);
    ShadowHook_Install(0x00049d70, (const void *)&FakeEmitPath);
    ShadowHook_Install(0x000445f0, (const void *)&FakeNearestNode);
    ShadowHook_Install(0x0004a070, (const void *)&FakeNodeAtDistance);
    ShadowHook_Install(0x00049980, (const void *)&FakeSetupGoal);
    ShadowHook_Install(0x000482c0, (const void *)&FakeResetRoute);
    ShadowHook_Install(0x000479f0, (const void *)&FakeInvalidateRoute);
    ShadowHook_Install(0x00020c30, (const void *)&FakeFindCel);
    ShadowHook_Install(0x0004a7b0, (const void *)&FakeFurthest);
    ShadowHook_Install(0x000304d0, (const void *)&FakeInBox);
    ShadowHook_Install(0x0004e320, (const void *)&FakeSetState);
    ShadowHook_Install(0x000341c0, (const void *)&FakeCallAnim);
    ShadowHook_Install(0x000453e0, (const void *)&FakeMoveRelative);
    ShadowHook_Install(0x00036dc0, (const void *)&FakeFeetPos);
    ShadowHook_Install(0x00044030, (const void *)&FakeMoveTest);
    ShadowHook_Install(0x00047bb0, (const void *)&FakeRouteDistance);
}

// Every original of the group at once, so they reach each other rather than ours
struct OriginalMoves {
    XbeOriginalScope move{0x00043400}, steer{0x00043070}, evaluate{0x00042cc0}, sweep{0x00042bb0},
        quadratic{0x00042b20}, noBunching{0x000437f0}, boundry{0x000454e0}, turnToObj{0x00043ba0},
        routeAngle{0x00044ec0}, nextNode{0x00044eb0}, missionPath{0x00044cb0}, nearestDCV{0x00044670},
        canReach{0x00047050}, safetyPos{0x00047750}, safetyObj{0x000479a0}, astronautCombat{0x00045040},
        astronautAnim{0x00044f70}, astronautHit{0x00045230}, astronautDeath{0x000452c0};
};

// The original NDrone2_Steer and NDrone2_Move, with the stack they will use zeroed first (see the top)
__declspec(naked) void __cdecl OriginalSteer(DCVars_tag *dcv, float angle) {
    _asm {
        push edi
        lea edi, [esp - 0x800]
        mov ecx, 0x200
        xor eax, eax
        cld
        rep stosd
        pop edi
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        mov eax, 0x00043070
        call eax
        add esp, 8
        ret
    }
}

__declspec(naked) void __cdecl OriginalMove(DCVars_tag *dcv) {
    _asm {
        push edi
        lea edi, [esp - 0x800]
        mov ecx, 0x200
        xor eax, eax
        cld
        rep stosd
        pop edi
        push dword ptr [esp + 4]
        mov eax, 0x00043400
        call eax
        add esp, 4
        ret
    }
}

// The original NDrone2_Move calls our Vec_AngleDifference just before NDrone2_Steer, so Steer's uninitialised
// obstacle flags are whatever ours left on the stack, which depends on how it was compiled. For the originals, ours
// zeroes the stack below it again on the way out.
const unsigned kVec_AngleDifference = 0x000d4cb0;

__declspec(naked) float __cdecl AngleDifferenceZeroingStack(float angle1, float angle2) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        call Vec_AngleDifference
        add esp, 8
        push edi
        lea edi, [esp - 0x800]
        mov ecx, 0x200
        xor eax, eax
        cld
        rep stosd
        pop edi
        ret
    }
}

struct ZeroStackAfterAngles {
    unsigned ours = XbeOriginal_Target(kVec_AngleDifference);
    ZeroStackAfterAngles() { XbeOriginal_Redirect(kVec_AngleDifference, (const void *)&AngleDifferenceZeroingStack); }
    ~ZeroStackAfterAngles() { XbeOriginal_Redirect(kVec_AngleDifference, (const void *)(uintptr_t)ours); }
};

// The register-argument original: the obstacle drone in EAX, the test in ESI
__declspec(naked) uint32_t __cdecl OriginalEvaluateObstacle(Drone_tag *drone, Drone_tag *other,
                                                            ObstacleEval_tag *eval) {
    _asm {
        push esi
        mov eax, [esp + 12]
        mov esi, [esp + 16]
        push dword ptr [esp + 8]
        mov ecx, 0x00042cc0
        call ecx
        add esp, 4
        pop esi
        ret
    }
}

// ---- the made-up world

const float kNaN = std::bit_cast<float>(0x7fc00000u);

float RandomFloat(float lo, float hi) {
    switch (ShadowRand(24)) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    case 2:
        return kNaN;
    default:
        return ShadowRandFloat(lo, hi);
    }
}

void RandomVector(_VECTOR *v, float range) {
    v->x = RandomFloat(-range, range);
    v->y = ShadowRandFloat(-range / 4, range / 4);
    v->z = RandomFloat(-range, range);
}

// Near p: within the 3 m and the 0.4 m the functions single out, or exactly 3 m off
void Near(_VECTOR *v, const _VECTOR *p) {
    switch (ShadowRand(6)) {
    case 0:
        *v = *p;
        v->x += 3.0f;
        break;
    case 1:
        *v = *p;
        v->z += ShadowRandFloat(-0.8f, 0.8f);
        break;
    default:
        v->x = p->x + ShadowRandFloat(-4.0f, 4.0f);
        v->y = p->y + ShadowRandFloat(-0.5f, 0.5f);
        v->z = p->z + ShadowRandFloat(-4.0f, 4.0f);
        break;
    }
}

float RandomSpeed() {
    static const float edges[] = {0.0f, 0.004f, 0.005f, 0.0051f, 0.02f, 0.1f};
    if (ShadowRand(2))
        return edges[ShadowRand(sizeof(edges) / sizeof(edges[0]))];
    return RandomFloat(0.0f, 0.2f);
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    o->extraObjectData = d;
    o->objectType = ShadowRand(10) == 0 ? OBJECTTYPE_DEAD_DRONE : OBJECTTYPE_DRONE;
    o->flags = (char)(ShadowRand(10) == 0);
    o->inCel = (cel_tag *)world.cel;
    if (i == 0)
        RandomVector(&o->position, 10.0f);
    else
        Near(&o->position, &world.objs[0].position);
    o->rotation.y = RandomFloat(-4.0f, 4.0f);
    d->gameObj = (i > 0 && ShadowRand(10) == 0) ? NULL : o;

    static const uint flags[] = {0x100, 0x100, 0x101, 0x110, 0x300, 0x500, 0, 0x140};
    d->flags = flags[ShadowRand(sizeof(flags) / sizeof(flags[0]))];
    static const float healths[] = {-1.0f, 0.0f, -0.0f, 50.0f, 100.0f, 100.0f};
    d->health = ShadowRand(12) == 0 ? kNaN : healths[ShadowRand(6)];
    d->sm.id = ShadowRand(6);
    d->skinClass = (short)(ShadowRand(6) == 0 ? 0xb : ShadowRand(20));
    d->animMoveSpeed = ShadowRand(5) == 0 ? 0.0f : RandomFloat(0.0f, 3.0f);
    d->moved = RandomSpeed();
    d->turnRate = RandomFloat(0.0f, 1.0f);
    d->moveRotation.y = RandomFloat(-4.0f, 4.0f);
    d->angleToSomePoint = RandomFloat(-4.0f, 4.0f);
    d->flies = (uchar)(ShadowRand(3) == 0);
    d->floatsToMoveTarget = (uchar)ShadowRand(2);
    d->floatAcceleration = RandomFloat(0.0f, 4.0f);
    d->unknown3f0 = ShadowRand(5) == 0 ? 0x8000000 : 0;
    static const uint animFlags[] = {0, 0, 8, 0x10, 0x20, 0x38, 0x80, 0x200};
    d->unknown46c = animFlags[ShadowRand(sizeof(animFlags) / sizeof(animFlags[0]))];
    d->unknown290 = ShadowRand(4) == 0 ? 0x1e : 0;
    RandomVector(&d->pushOffset, 0.2f);
    RandomVector(&d->prevPushOffset, 0.2f);
    RandomVector(&d->velocity, 5.0f);
    Near(&d->moveTarget, &o->position);
    d->moveTargetDistance = RandomFloat(0.0f, 20.0f);
    d->nearestDroneDist = RandomFloat(0.0f, 4.0f);
    d->unknown28c = (Drone_tag *)(uintptr_t)ShadowRand(0x10000);
    RandomVector(&d->opponentDelta, 3.0f);
    if (ShadowRand(10) == 0)
        d->opponentDelta = {0.0f, 0.0f, 0.0f};
    static const uint sight[] = {0, 4, 8, 0xc, 0xffffffff};
    d->sightFlags = sight[ShadowRand(5)];
    d->stateScratch = 900 + ShadowRand(200);
    d->stateScratch2 = ShadowRand(250);
    d->botVars = &world.botVars;
    d->explosiveSeen = (uchar)(ShadowRand(4) != 0);
    RandomVector(&d->explosivePos, 10.0f);
    d->explosiveRadius = RandomFloat(0.0f, 20.0f);
    d->explosiveState = (short)(ShadowRand(2) ? 0 : DSTATE_HideFromScaryObject);
    d->sm.resumeState = ShadowRand(3) == 0 ? 0x12345 + ShadowRand(5) : DSTATE_Idle + ShadowRand(100);
    d->aiPoint.radius = RandomFloat(0.0f, 3.0f);
    RandomVector(&d->aiPoint.pos, 10.0f);
}

void RandomRoute() {
    memset(&world.route, 0, sizeof(world.route));
    memset(&world.path, 0, sizeof(world.path));
    memset(world.nodes, 0, sizeof(world.nodes));
    for (int i = 0; i < kNodes; i++) {
        Near(&world.nodes[i].node.pos, &world.players[0].position);
        world.nodes[i].node.cel = ShadowRand(2) ? (cel_tag *)world.cel : (cel_tag *)world.cel2;
        world.routeNodes[i] = (ushort)ShadowRand(kNodes);
    }
    world.path.nodeCount = kNodes;
    world.path.fileNodes = world.nodes;
    RandomVector(&world.route.creep.segmentStart, 10.0f);
    if (ShadowRand(4) == 0) {
        // Along the line from moveTarget, for the near-parallel branch
        Vec_Subtract(&world.objs[0].position, &world.drones[0].moveTarget, &world.route.creep.segmentEnd);
        Vec_Add2(&world.route.creep.segmentEnd, &world.route.creep.segmentStart, &world.route.creep.segmentEnd);
        world.route.creep.segmentEnd.x += ShadowRandFloat(-0.01f, 0.01f);
    } else {
        RandomVector(&world.route.creep.segmentEnd, 10.0f);
    }

    Drone_tag *d = &world.drones[0];
    d->activeRoute = &world.route;
    d->route1.path = &world.path;
    d->route2.path = &world.path;
    d->route2.nodes = world.routeNodes;
    d->route2.count = (ushort)ShadowRand(kNodes + 1);
    d->route2.current = (short)(ShadowRand(5) == 0 ? -1 : ShadowRand(kNodes));
    NPCGlobals.safetyEmitter.path = ShadowRand(5) == 0 ? NULL : &world.path;
}

void RandomWorld() {
    for (int i = 0; i < kPlayers; i++) {
        obj_tag *p = &world.players[i];
        memset(p, 0, sizeof(*p));
        p->objectType = OBJECTTYPE_PLAYER;
        RandomVector(&p->position, 10.0f);
        p->rotation.y = RandomFloat(-4.0f, 4.0f);
    }
    for (int i = 0; i < kDrones; i++)
        RandomDrone(i);
    for (int i = 0; i < kPlayers; i++)
        if (ShadowRand(2))
            Near(&world.players[i].position, &world.objs[0].position);
    memset(&world.botVars, 0, sizeof(world.botVars));
    world.botVars.flags = ShadowRand(3) == 0 ? 2 : 4;

    // The drone list: drone 0 and some of the others, in some order
    Drone_tag *list = NULL;
    bool listed[kDrones] = {};
    int count = 1 + ShadowRand(kDrones);
    for (int i = count - 1; i >= 0; i--) {
        int which = (i + ShadowRand(2)) % kDrones;
        if (listed[which])
            continue;
        listed[which] = true;
        world.drones[which].next = list;
        list = &world.drones[which];
    }
    if (ShadowRand(8) == 0)
        list = NULL;
    NPCGlobals.NDrone2List = list;

    Drone_tag *d = &world.drones[0];
    switch (ShadowRand(4)) {
    case 0:
        d->opponent = NULL;
        break;
    case 1:
        d->opponent = &world.objs[1];
        break;
    default:
        d->opponent = &world.players[0];
        break;
    }
    int nearest = 1 + ShadowRand(kDrones - 1);
    d->nearestDrone = ShadowRand(4) == 0 ? NULL : &world.drones[nearest];
    if (d->nearestDrone != NULL)
        d->nearestDrone->gameObj = &world.objs[nearest];    // NoBunching does not check it
    memset(&world.hit, 0, sizeof(world.hit));
    RandomVector(&world.hit.maybehitDirection, 1.0f);
    d->lastHit = ShadowRand(2) ? &world.hit : NULL;
    memset(&world.node, 0, sizeof(world.node));
    RandomVector(&world.node.pos, 10.0f);
    world.node.cel = (cel_tag *)world.cel2;
    RandomRoute();

    world.dcv.drone = d;
    world.dcv.gameObj = &world.objs[0];
    world.dcv.cel = (cel_tag *)world.cel;
    world.dcv.aiStateMachine = &d->sm;

    world.player = (int)ShadowRand(kPlayers + 1) - 1;
    static const uint moveResults[] = {0, 1, 2, 3, 4, 9, 10, 11, 12, 13, 0x100, 0xffffffff};
    world.moveResult = moveResults[ShadowRand(sizeof(moveResults) / sizeof(moveResults[0]))];
    world.emitterOk = ShadowRand(4) != 0;
    static const uint emitResults[] = {0, 1, 1, 0x100};
    world.emitResult = emitResults[ShadowRand(4)];
    world.nearestNode = (short)(ShadowRand(4) == 0 ? -1 : ShadowRand(kNodes));
    world.nodeOk = ShadowRand(4) != 0;
    world.nodeNull = ShadowRand(4) == 0;
    world.furthestOk = ShadowRand(2);
    world.inBox = ShadowRand(2);
    world.moveTestBits = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    world.moveTestCalls = 0;
    world.relativeBits = ShadowRand(0x10000);
    world.relativeCalls = 0;
    world.routeDistance = RandomFloat(-20.0f, 20.0f);

    GameState.NumFramesUnpaused = 1000 + ShadowRand(100);
    if (ShadowRand(2)) {
        FRAME_RATE_INT = 60;
        FRAME_RATE_DIV = 1.0f;
        REC_FRAME_RATE = 1.0f / 60.0f;
    } else {
        FRAME_RATE_INT = 50;
        FRAME_RATE_DIV = 1.2f;
        REC_FRAME_RATE = 0.02f;
    }
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    MPSettings.maybeDroneAIEnabled = ShadowRand(4) == 0;
    MPSettings.numPlayers = ShadowRand(kPlayers + 1);
    for (int i = 0; i < kPlayers; i++)
        MPGame.players[i].playerObj = ShadowRand(4) == 0 ? NULL : &world.players[i];
}

// A test of one obstacle for NDrone2_EvaluateObstacle, on the world's own record
void RandomEval() {
    memset(&world.eval, 0, sizeof(world.eval));
    memset(&world.obstacle, 0, sizeof(world.obstacle));
    RandomVector(&world.vecs[0], 5.0f);
    Near(&world.vecs[1], &world.vecs[0]);
    world.vecs[2].y = RandomFloat(-4.0f, 4.0f);
    world.vecs[3].y = RandomFloat(-4.0f, 4.0f);
    SteerBody *self = &world.eval.self;
    self->flags = ShadowRand(4);
    self->pos = &world.vecs[0];
    self->rot = &world.vecs[2];
    self->speed = RandomSpeed();
    self->radius = ShadowRand(2) ? 0.404f : RandomFloat(0.0f, 1.0f);
    self->vel.x = (float)(sin(world.vecs[2].y) * self->speed);
    self->vel.z = (float)(cos(world.vecs[2].y) * self->speed);
    if (ShadowRand(6) == 0)
        RandomVector(&self->vel, 1.0f);
    world.obstacle.flags = ShadowRand(3) == 0 ? 2 : ShadowRand(2);
    world.obstacle.pos = &world.vecs[1];
    world.obstacle.rot = &world.vecs[3];
    world.obstacle.speed = RandomSpeed();
    world.obstacle.radius = ShadowRand(2) ? 0.475f : RandomFloat(0.0f, 1.0f);
    world.eval.other = &world.obstacle;
}

// ---- the cases

float floatA, floatB;

void TestSteering() {
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        Compare("NDrone2_Move", trial,
                [] {
                    OriginalMoves o;
                    ZeroStackAfterAngles zeroed;
                    OriginalMove(&world.dcv);
                    return 0u;
                },
                [] {
                    NDrone2_Move(&world.dcv);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        floatA = RandomFloat(-3.5f, 3.5f);
        Compare("NDrone2_Steer", trial,
                [] {
                    OriginalMoves o;
                    OriginalSteer(&world.dcv, floatA);
                    return 0u;
                },
                [] {
                    NDrone2_Steer(&world.dcv, floatA);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 12000; trial++) {
        RandomWorld();
        RandomEval();
        world.objArg = ShadowRand(3) == 0 ? NULL : &world.objs[1];
        Compare("NDrone2_EvaluateObstacle", trial,
                [] {
                    OriginalMoves o;
                    Drone_tag *other = world.objArg == NULL ? NULL : &world.drones[1];
                    return OriginalEvaluateObstacle(&world.drones[0], other, &world.eval) & 0xff;
                },
                [] {
                    Drone_tag *other = world.objArg == NULL ? NULL : &world.drones[1];
                    return (uint32_t)_NDrone2_EvaluateObstacle(&world.drones[0], other, &world.eval);
                });
    }
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        RandomVector(&world.vecs[0], 3.0f);
        Near(&world.vecs[2], &world.vecs[0]);
        world.vecs[1] = world.vecs[0];
        world.vecs[1].x += RandomFloat(-0.2f, 0.2f);
        world.vecs[1].z += RandomFloat(-0.2f, 0.2f);
        if (ShadowRand(5) == 0) {
            // Moving together: no closing speed, a discriminant of 0
            world.vecs[3] = world.vecs[2];
            Vec_Subtract(&world.vecs[1], &world.vecs[0], &world.vecs[4]);
            Vec_Add2(&world.vecs[3], &world.vecs[4], &world.vecs[3]);
        } else {
            world.vecs[3] = world.vecs[2];
            world.vecs[3].x += RandomFloat(-0.2f, 0.2f);
            world.vecs[3].z += RandomFloat(-0.2f, 0.2f);
        }
        world.floats[0] = ShadowRand(2) ? 0.404f : RandomFloat(0.0f, 1.0f);
        world.floats[1] = ShadowRand(2) ? 0.475f : RandomFloat(0.0f, 1.0f);
        world.out[0] = RandomFloat(-1.0f, 1.0f);
        world.out[1] = RandomFloat(-1.0f, 1.0f);
        Compare("SphereSphereSweep", trial,
                [] {
                    OriginalMoves o;
                    return ORIGINAL(0x00042bb0, uint32_t(__cdecl *)(float, _VECTOR *, _VECTOR *, float, _VECTOR *,
                                                                    _VECTOR *, float *, float *))(
                               world.floats[0], &world.vecs[0], &world.vecs[1], world.floats[1], &world.vecs[2],
                               &world.vecs[3], &world.out[0], &world.out[1]) & 0xff;
                },
                [] {
                    return (uint32_t)SphereSphereSweep(world.floats[0], &world.vecs[0], &world.vecs[1],
                                                       world.floats[1], &world.vecs[2], &world.vecs[3],
                                                       &world.out[0], &world.out[1]);
                });
    }
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        switch (ShadowRand(4)) {
        case 0: {
            // A repeated root: b^2 = 4ac
            float a = ShadowRandFloat(0.5f, 4.0f);
            float r = ShadowRandFloat(-2.0f, 2.0f);
            world.floats[0] = 1.0f;
            world.floats[1] = 2.0f * r;
            world.floats[2] = r * r;
            if (ShadowRand(2)) {
                world.floats[0] = 4.0f;
                world.floats[1] = 4.0f * a;
                world.floats[2] = a * a;
            }
            break;
        }
        default:
            world.floats[0] = RandomFloat(-1.0f, 1.0f);
            world.floats[1] = RandomFloat(-2.0f, 2.0f);
            world.floats[2] = RandomFloat(-1.0f, 1.0f);
            break;
        }
        world.out[0] = RandomFloat(-1.0f, 1.0f);
        world.out[1] = RandomFloat(-1.0f, 1.0f);
        Compare("SolveQuadratic", trial,
                [] {
                    OriginalMoves o;
                    return ORIGINAL(0x00042b20, uint32_t(__cdecl *)(float, float, float, float *, float *))(
                               world.floats[0], world.floats[1], world.floats[2], &world.out[0], &world.out[1]) &
                           0xff;
                },
                [] {
                    return (uint32_t)SolveQuadratic(world.floats[0], world.floats[1], world.floats[2], &world.out[0],
                                                    &world.out[1]);
                });
    }
}

void TestTurningAndRoutes() {
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        Compare("DroneMove_NoBunching", trial,
                [] {
                    OriginalMoves o;
                    return ORIGINAL(0x000437f0, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)DroneMove_NoBunching(&world.dcv); });
        Compare("DroneMove_SetBoundryFlags", trial,
                [] {
                    OriginalMoves o;
                    ORIGINAL(0x000454e0, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneMove_SetBoundryFlags(&world.dcv);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        world.objArg = ShadowRand(6) == 0 ? NULL : (ShadowRand(2) ? &world.players[0] : &world.objs[1]);
        floatA = RandomFloat(-1.0f, 1.0f);
        static const float tolerances[] = {0.0f, 0.1f, 0.5f, 3.2f};
        floatB = ShadowRand(10) == 0 ? kNaN : tolerances[ShadowRand(4)];
        Compare("DroneMove_TurnToObj", trial,
                [] {
                    OriginalMoves o;
                    ORIGINAL(0x00043ba0, void(__cdecl *)(DCVars_tag *, obj_tag *, float, float))(
                        &world.dcv, world.objArg, floatA, floatB);
                    return 0u;
                },
                [] {
                    DroneMove_TurnToObj(&world.dcv, world.objArg, floatA, floatB);
                    return 0u;
                });
        Compare("DroneMove_NextRouteAngle", trial,
                [] {
                    OriginalMoves o;
                    theirAngle = ORIGINAL(0x00044ec0, double(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    ourAngle = DroneMove_NextRouteAngle(&world.dcv);
                    return 0u;
                });
        CompareAngles(trial);
        Compare("DroneMove_DistanceToNextRouteNode", trial,
                [] {
                    OriginalMoves o;
                    return Bits(ORIGINAL(0x00044eb0, float(__cdecl *)(DCVars_tag *))(&world.dcv));
                },
                [] { return Bits(DroneMove_DistanceToNextRouteNode(&world.dcv)); });
    }
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        world.objArg = ShadowRand(2) ? &world.players[0] : &world.objs[1];
        static const float radii[] = {0.0f, 1.0f, 5.0f, 5.0f, 30.0f};
        floatA = ShadowRand(12) == 0 ? kNaN : radii[ShadowRand(5)];
        Compare("DroneMove_ObjectMissionPathDistance", trial,
                [] {
                    OriginalMoves o;
                    return DoubleBits(ORIGINAL(0x00044cb0, double(__cdecl *)(DCVars_tag *, obj_tag *, float))(
                        &world.dcv, world.objArg, floatA));
                },
                [] { return DoubleBits(DroneMove_ObjectMissionPathDistance(&world.dcv, world.objArg, floatA)); });
        Compare("NDrone2_NearestNodeDCV", trial,
                [] {
                    OriginalMoves o;
                    return ORIGINAL(0x00044670, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xffff;
                },
                [] { return (uint32_t)(ushort)NDrone2_NearestNodeDCV(&world.dcv); });
        Compare("NDrone2_CanReachGoalPosition", trial,
                [] {
                    OriginalMoves o;
                    return ORIGINAL(0x00047050, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)NDrone2_CanReachGoalPosition(&world.dcv); });
    }
}

void TestSafety() {
    for (int trial = 0; trial < 10000; trial++) {
        RandomWorld();
        world.objArg = ShadowRand(3) == 0 ? NULL : &world.players[0];
        floatA = ShadowRand(12) == 0 ? kNaN : RandomFloat(0.0f, 20.0f);
        RandomVector(&world.vecs[5], 10.0f);
        Compare("DroneMove_FindSafetyFromScaryPosition", trial,
                [] {
                    OriginalMoves o;
                    return ORIGINAL(0x00047750, uint32_t(__cdecl *)(DCVars_tag *, _VECTOR *, obj_tag *, float))(
                               &world.dcv, &world.vecs[5], world.objArg, floatA) & 0xff;
                },
                [] {
                    return (uint32_t)DroneMove_FindSafetyFromScaryPosition(&world.dcv, &world.vecs[5], world.objArg,
                                                                           floatA);
                });
        Compare("DroneMove_FindSafetyFromScaryObject", trial,
                [] {
                    OriginalMoves o;
                    return ORIGINAL(0x000479a0, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)DroneMove_FindSafetyFromScaryObject(&world.dcv); });
    }
}

void TestZeroGravity() {
    for (int trial = 0; trial < 10000; trial++) {
        RandomWorld();
        Drone_tag *d = &world.drones[0];
        if (ShadowRand(4) == 0) {
            // Exactly 3 m from where it floats to
            d->moveTarget = world.objs[0].position;
            d->moveTarget.z += 3.0f;
        }
        static const uint frames[] = {0, 100, 150, 200};
        d->stateScratch2 = frames[ShadowRand(4)];
        Compare("DroneMove_AstronautCombat", trial,
                [] {
                    OriginalMoves o;
                    ORIGINAL(0x00045040, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneMove_AstronautCombat(&world.dcv);
                    return 0u;
                });
        Compare("DroneMove_AstronautAnim", trial,
                [] {
                    OriginalMoves o;
                    ORIGINAL(0x00044f70, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneMove_AstronautAnim(&world.dcv);
                    return 0u;
                });
        Compare("DroneMove_AstronautHitMove", trial,
                [] {
                    OriginalMoves o;
                    ORIGINAL(0x00045230, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneMove_AstronautHitMove(&world.dcv);
                    return 0u;
                });
        Compare("DroneMove_AstronautDeathMove", trial,
                [] {
                    OriginalMoves o;
                    ORIGINAL(0x000452c0, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneMove_AstronautDeathMove(&world.dcv);
                    return 0u;
                });
    }
}

} // namespace

void DroneMoveShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], mpSettings[sizeof(MPSettings_t)], npc[sizeof(NPCGlobals_t)];
    static uint8_t mpPlayers[sizeof(MPGame.players)];
    static uint32_t rand[2];
    uint frameInt = FRAME_RATE_INT;
    float frameDiv = FRAME_RATE_DIV;
    float frameRec = REC_FRAME_RATE;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(mpSettings, &MPSettings, sizeof(mpSettings));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(mpPlayers, &MPGame.players, sizeof(mpPlayers));
    memcpy(rand, RandWords, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&MPSettings, sizeof(MPSettings_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(&MPGame.players, sizeof(MPGame.players));
    regions.Add(RandWords, 8);
    regions.Add(&FRAME_RATE_INT, 4);
    regions.Add(&FRAME_RATE_DIV, 4);
    regions.Add(&REC_FRAME_RATE, 4);
    ShadowFakes_AddRegions(regions);

    InstallFakes();
    NfWarnMuted++;
    cases = differ = acosUlps = 0;

    TestSteering();
    int steerCases = cases, steerDiffer = differ;
    TestTurningAndRoutes();
    int routeCases = cases, routeDiffer = differ;
    TestSafety();
    int safetyCases = cases, safetyDiffer = differ;
    TestZeroGravity();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&MPSettings, mpSettings, sizeof(mpSettings));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(&MPGame.players, mpPlayers, sizeof(mpPlayers));
    memcpy(RandWords, rand, sizeof(rand));
    FRAME_RATE_INT = frameInt;
    FRAME_RATE_DIV = frameDiv;
    REC_FRAME_RATE = frameRec;

    printf("[dronemove] walking and steering: %d cases, %d differ\n", steerCases, steerDiffer);
    printf("[dronemove] turning and routes: %d cases, %d differ (%d NextRouteAngle results 1-2 ulp apart)\n",
           routeCases - steerCases, routeDiffer - steerDiffer, acosUlps);
    printf("[dronemove] running from danger: %d cases, %d differ\n", safetyCases - routeCases,
           safetyDiffer - routeDiffer);
    printf("[dronemove] zero gravity: %d cases, %d differ\n", cases - safetyCases, differ - safetyDiffer);
}
