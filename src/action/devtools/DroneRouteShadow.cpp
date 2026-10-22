// Shadow tests for the drones' routes (game/drone/NDrone2_Route.cpp) and the drone spawners
// (game/obj/DroneSpawner.cpp): each original and ours run on the same made-up drone, paths, AI points and spawners,
// with what needs a level going to logging fakes. Run at start with MenuShadowTests=on (MenuProbe.cpp), before any
// level exists.
//
// Faked, so not tested here: the feet (NDrone2_FeetPos), the node searches (NDrone2_NearestNode), the creep along a
// link (LinkCreep_Calc, LinkCreep_Dest), following and planning a route (AINetwork_FollowRoute,
// AINetwork_CalcRouteToPosition, AINetwork_GetRouteDistance, AINetwork_SetupGoalPosition), the steering at the
// waypoint (NDrone2_maybeCalcDistanceAndDirToTarget), NDrone2_CanReachGoalPosition (the move group's), the anims, the
// heap, creating, enabling and deleting drones and objects, the cels, the player, the player's view of a position
// (Drone_PositionVisible), the death channel and the speech. NDrone2_DistanceToEmitter runs for real on made-up
// emitters. Random draws are kept in step: both sides start from the same Rand_Random state, and it is compared
// afterwards.

#include "DroneRouteShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/viewer.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/NDrone2_Route.h"
#include "../game/obj/Drone_AIHints.h"
#include "../game/obj/DroneSpawner.h"
#include "../game/sp/SwitchChannels.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords (GameSpan<uint32_t>{0x0018cdf8})

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int kNodes = 12;
const int kLinks = kNodes + 4;
const int kCelNodes = 6;
const int kPoints = 8;
const int kTemplates = 4;
const int kSlots = 6;
const int kNewDrones = 3;

struct World {
    // The drone the route functions run for
    obj_tag obj;
    Drone_tag drone;
    DCVars_tag dcv;
    cel_tag cels[2];
    ushort route1Nodes[kNodes];
    ushort route2Nodes[kNodes];

    // One set of paths' nodes and links, shared by AIPaths[0] to [3]
    MAPNODE_tag fileNodes[kNodes];
    AINode_tag nodes[kNodes];
    AILink_tag links[kLinks];
    ushort nodeLinks[4 * kNodes];
    AINode_tag celNodes[kCelNodes];     // the cel's own list, onto the same file nodes
    char pathName[8];

    DroneAIPoint_tag points[kPoints];
    uchar emitterData[kPoints][kNodes];
    CelPos_tag dest;
    _VECTOR pos;

    // The spawners
    obj_tag spawnerObj;
    DroneSpawner_tag spawner;
    DroneSpawnerPlacement placement;
    obj_tag templateObjs[kTemplates];
    Drone_tag templates[kTemplates];
    DroneSpawnEntry_tag entries[kTemplates];
    obj_tag *slots[kSlots];
    obj_tag spawnedObjs[kSlots];
    Drone_tag spawned[kSlots];
    obj_tag newObjs[kNewDrones];
    obj_tag player;
    uint8_t pool[0x800];        // the fake Mem_Malloc's
    uint32_t poolUsed;

    // What the fakes answer
    short nearestNode;
    int creepOk;
    uint followStatus;
    uint calcStatus;
    double routeDistance;
    int canReach;
    int playerPresent;
    int visible;
    int visionTestsBump;
    int createOk;
    int newDrone;
};
World world;

ShadowRegions regions;
int cases, differ;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "droneroute", what, trial, theirs, ours))
        differ++;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

uint32_t DoubleBits(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    return (uint32_t)u ^ (uint32_t)(u >> 32);
}

int WhoObj(const obj_tag *obj) {
    if (obj == NULL)
        return -1;
    if (obj == &world.obj)
        return 0;
    if (obj == &world.spawnerObj)
        return 1;
    if (obj == &world.player)
        return 100;
    for (int i = 0; i < kTemplates; i++)
        if (obj == &world.templateObjs[i])
            return 10 + i;
    for (int i = 0; i < kSlots; i++)
        if (obj == &world.spawnedObjs[i])
            return 20 + i;
    for (int i = 0; i < kNewDrones; i++)
        if (obj == &world.newObjs[i])
            return 30 + i;
    if ((const uint8_t *)obj >= ShadowCreatePool && (const uint8_t *)obj < ShadowCreatePool + SHADOW_CREATE_POOL)
        return 200 + (int)((const uint8_t *)obj - ShadowCreatePool);
    return 999;
}

int WhoDrone(const Drone_tag *drone) {
    if (drone == NULL)
        return -1;
    if (drone == &world.drone)
        return 0;
    for (int i = 0; i < kTemplates; i++)
        if (drone == &world.templates[i])
            return 10 + i;
    for (int i = 0; i < kSlots; i++)
        if (drone == &world.spawned[i])
            return 20 + i;
    return 999;
}

int WhoRoute(const AIRoute_tag *route) {
    return route == &world.drone.route1 ? 1 : route == &world.drone.route2 ? 2 : route == NULL ? -1 : 9;
}

int WhoCel(const cel_tag *cel) {
    for (int i = 0; i < 2; i++)
        if (cel == &world.cels[i])
            return i;
    return cel == NULL ? -1 : 9;
}

int WhoPath(const AIPath_tag *path) {
    if (path >= &AIPaths[0] && path < &AIPaths[50])
        return (int)(path - &AIPaths[0]);
    return path == NULL ? -1 : 99;
}

int WhoPoint(const void *point) {
    for (int i = 0; i < kPoints; i++)
        if (point == &world.points[i])
            return i;
    return point == NULL ? -1 : 99;
}

void LogVector(const char *name, const _VECTOR *v) {
    ShadowLog("%s=%08x,%08x,%08x", name, Bits(v->x), Bits(v->y), Bits(v->z));
}

// ---- the fakes

bool __cdecl FakeFeetPos(obj_tag *obj, _VECTOR *feet) {
    ShadowLog("feet(%d)", WhoObj(obj));
    feet->x = obj->position.x;
    feet->y = obj->position.y - 0.875f;
    feet->z = obj->position.z;
    return true;
}

short __cdecl FakeNearestNode(CelPos_tag *pos, AIPath_tag *path, obj_tag *obj) {
    LogVector("nearest", &pos->pos);
    ShadowLog("(%d,%d,%d)", WhoCel(pos->cel), WhoPath(path), WhoObj(obj));
    return world.nearestNode;
}

bool __cdecl FakeCreepCalc(AIRoute_tag *route, char fromEnd) {
    ShadowLog("creepcalc(%d,%d,%d)", WhoRoute(route), fromEnd, route->count);
    return world.creepOk != 0;
}

bool __cdecl FakeCreepDest(AIRoute_tag *route, short offset, CelPos_tag *out) {
    ShadowLog("creepdest(%d,%d,%d)", WhoRoute(route), offset, out == &route->waypoint);
    out->pos.x = 1.5f;
    out->pos.y = -2.25f;
    out->pos.z = (float)route->count;
    out->cel = &world.cels[1];
    return true;
}

void __cdecl FakeSteerAt(DCVars_tag *dcv, CelPos_tag *target) {
    ShadowLog("steer(%d,%d)", dcv == &world.dcv, target == &world.drone.route1.waypoint ? 1
                                                : target == &world.drone.route2.waypoint ? 2 : 9);
    LogVector("at", &target->pos);
    dcv->drone->moveTarget = target->pos;
}

uint __cdecl FakeFollowRoute(AIRoute_tag *route, DCVars_tag *dcv) {
    ShadowLog("follow(%d,%d)", WhoRoute(route), dcv == &world.dcv);
    return world.followStatus;
}

uint __cdecl FakeCalcRouteToPosition(obj_tag *mover, CelPos_tag *dest, AIRoute_tag *route, AITarget_tag *target,
                                     AIPoint_tag *point, float radius, uint flags) {
    ShadowLog("calc(%d,%d,%d,%d,%d,%08x,%x)", WhoObj(mover), dest == &world.dest ? 0 : WhoPoint(dest),
              WhoRoute(route), target == &world.drone.goalTarget, point == &world.drone.aiPoint, Bits(radius),
              flags);
    route->start.pos.x = 3.0f;
    route->start.pos.y = (float)flags;
    route->start.pos.z = radius;
    route->start.cel = &world.cels[1];
    return world.calcStatus;
}

double __cdecl FakeRouteDistance(AIRoute_tag *route) {
    ShadowLog("distance(%d)", WhoRoute(route));
    return world.routeDistance;
}

void __cdecl FakeSetupGoal(AITarget_tag *target, AIPoint_tag *point, CelPos_tag *celPos, float radius) {
    ShadowLog("goal(%d,%d,%d,%08x)", target == &world.drone.goalTarget, point == &world.drone.aiPoint,
              WhoPoint((uint8_t *)celPos - offsetof(DroneAIPoint_tag, pos)), Bits(radius));
    point->radius = radius;
    point->set = 1;
}

bool __cdecl FakeCanReach(DCVars_tag *dcv) {
    ShadowLog("canreach(%d)", dcv == &world.dcv);
    return world.canReach != 0;
}

bool __cdecl FakeCallAnim(uint state, uint variant, short endState, uint endMsg, float speed, int unused,
                          DCVars_tag *dcv) {
    ShadowLog("anim(%x,%u,%d,%u,%08x,%d,%d)", state, variant, endState, endMsg, Bits(speed), unused,
              dcv == &world.dcv);
    return true;
}

bool __cdecl FakeStandIdle(DCVars_tag *dcv, char wait) {
    ShadowLog("standidle(%d,%d)", dcv == &world.dcv, wait);
    return true;
}

bool __cdecl FakeWalkIdle(DCVars_tag *dcv, char wait) {
    ShadowLog("walkidle(%d,%d)", dcv == &world.dcv, wait);
    return true;
}

void __cdecl FakeInitObject(obj_tag *obj) {
    ShadowLog("initobject(%d)", WhoObj(obj));
}

cel_tag *__cdecl FakeFindCel(_VECTOR *pos, void *w) {
    LogVector("findcel", pos);
    return &world.cels[0];
}

void *__cdecl FakeMalloc(size_t size, MallocFlags flags, uint32_t align) {
    ShadowLog("malloc(%u,%x,%u)", (unsigned)size, (unsigned)(ushort)flags, align);
    uint32_t at = (world.poolUsed + 3) & ~3u;
    if (at + size > sizeof(world.pool))
        return NULL;
    world.poolUsed = at + (uint32_t)size;
    return world.pool + at;
}

void __cdecl FakeFree(void **p) {
    ShadowLog("free(%d)", p != NULL && *p != NULL ? (int)((uint8_t *)*p - world.pool) : -1);
    if (p != NULL)
        *p = NULL;
}

void __cdecl FakeEnable(char enable, Drone_tag *drone) {
    ShadowLog("enable(%d,%d)", enable, WhoDrone(drone));
}

void __cdecl FakeDeleteObject(obj_tag *obj) {
    ShadowLog("delete(%d)", WhoObj(obj));
}

obj_tag *__cdecl FakeCreateFromDIVars(DIVars_tag *vars) {
    int entry = -1;
    for (int i = 0; i < kTemplates; i++)
        if (vars == &world.entries[i].vars)
            entry = i;
    ShadowLog("createdrone(%d)", entry);
    return world.newDrone >= 0 ? &world.newObjs[world.newDrone] : NULL;
}

void __cdecl FakePostLoadInit(obj_tag *obj) {
    ShadowLog("postload(%d)", WhoObj(obj));
}

obj_tag *__cdecl FakePlayer(void) {
    ShadowLog("player");
    return world.playerPresent ? &world.player : NULL;
}

bool __cdecl FakePositionVisible(_VECTOR *pos, cel_tag *cel) {
    LogVector("visible", pos);
    ShadowLog("(%d)", WhoCel(cel));
    NPCGlobals.visionTests += (ushort)world.visionTestsBump;
    return world.visible != 0;
}

bool __cdecl FakeDeathChannel(Drone_tag *drone) {
    ShadowLog("deathchannel(%d)", WhoDrone(drone));
    return true;
}

DYNAMICSOUNDS *__cdecl FakePlaySFX(DCVars_tag *dcv, uint sfx, _VECTOR *pos, char check, float scale) {
    ShadowLog("sfx(%d,%x,%d,%d,%08x)", dcv == NULL ? -1 : 1, sfx, pos == NULL ? -1 : 1, check, Bits(scale));
    return NULL;
}

void InstallFakes() {
    ShadowFakes_Install();
    ShadowHook_Install(0x00036dc0, (const void *)&FakeFeetPos);
    ShadowHook_Install(0x000445f0, (const void *)&FakeNearestNode);
    ShadowHook_Install(0x0004abb0, (const void *)&FakeCreepCalc);
    ShadowHook_Install(0x0004a2e0, (const void *)&FakeCreepDest);
    ShadowHook_Install(0x00046080, (const void *)&FakeSteerAt);
    ShadowHook_Install(0x0004b370, (const void *)&FakeFollowRoute);
    ShadowHook_Install(0x0004ae30, (const void *)&FakeCalcRouteToPosition);
    ShadowHook_Install(0x00047cd0, (const void *)&FakeRouteDistance);
    ShadowHook_Install(0x00049980, (const void *)&FakeSetupGoal);
    ShadowHook_Install(0x00047050, (const void *)&FakeCanReach);
    ShadowHook_Install(0x000341c0, (const void *)&FakeCallAnim);
    ShadowHook_Install(0x00035310, (const void *)&FakeStandIdle);
    ShadowHook_Install(0x00035410, (const void *)&FakeWalkIdle);
    ShadowHook_Install(0x0002d190, (const void *)&FakeInitObject);
    ShadowHook_Install(0x00020c30, (const void *)&FakeFindCel);
    ShadowHook_Install(0x00070ae0, (const void *)&FakeMalloc);
    ShadowHook_Install(0x000705f0, (const void *)&FakeFree);
    ShadowHook_Install(0x000376b0, (const void *)&FakeEnable);
    ShadowHook_Install(0x0002d080, (const void *)&FakeDeleteObject);
    ShadowHook_Install(0x0002fc50, (const void *)&FakeCreateFromDIVars);
    ShadowHook_Install(0x00042a90, (const void *)&FakePostLoadInit);
    ShadowHook_Install(0x00035f90, (const void *)&FakePlayer);
    ShadowHook_Install(0x000318c0, (const void *)&FakePositionVisible);
    ShadowHook_Install(0x00036090, (const void *)&FakeDeathChannel);
    ShadowHook_Install(0x00037340, (const void *)&FakePlaySFX);
}

// Every original of this group at once, so they reach each other rather than ours
struct OriginalRoutes {
    XbeOriginalScope obstructed{0x0003a680}, assign{0x00046ad0}, init{0x00044a20}, creep{0x00046220},
        closest{0x000462e0}, moveTo{0x00046f30}, follow{0x00046a80}, patrol{0x00046c20}, mission{0x00046d40},
        runTo{0x00047190}, goTo{0x000470b0}, alarm{0x000470f0}, create{0x00030530}, spawnerInit{0x00030670},
        spawn{0x000323e0}, control{0x000324d0}, spawnerDelete{0x00030800}, droneDelete{0x000308b0},
        resetEntries{0x00030860}, waveTalk{0x00030910};
};

// ---- the made-up world

float RandomFloat(float lo, float hi) {
    switch (ShadowRand(16)) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    default:
        return ShadowRandFloat(lo, hi);
    }
}

void RandomVector(_VECTOR *v, float range) {
    v->x = RandomFloat(-range, range);
    v->y = ShadowRandFloat(-range / 4, range / 4);
    v->z = RandomFloat(-range, range);
}

// Paths 0 to 3 over one ring of nodes, each node's links in either order, some nodes with one, three or no links,
// and now and then a link end moved
void RandomPaths() {
    memset(&AIPaths[0], 0, 4 * sizeof(AIPath_tag));
    uint n = 1 + ShadowRand(kNodes);
    static const uint pathFlags[] = {AIPATH_PATROL, AIPATH_MISSION, AIPATH_PATROL | 0x10, AIPATH_MISSION | 0x10, 0,
                                     AIPATH_NAV, AIPATH_PATROL | AIPATH_MISSION};
    for (int p = 0; p < 4; p++) {
        AIPath_tag *path = &AIPaths[p];
        path->index = (ushort)p;
        path->record = (uchar *)world.pathName;
        path->flags = pathFlags[ShadowRand(sizeof(pathFlags) / sizeof(pathFlags[0]))];
        path->nodeCount = ShadowRand(6) == 0 ? (uint)ShadowRand(n + 1) : n;
        path->runtimeNodes = world.nodes;
        path->linkCount = kLinks;
        path->links = world.links;
        path->nodeLinks = world.nodeLinks;
    }
    for (int i = 0; i < kLinks; i++) {
        AILink_tag *link = &world.links[i];
        memset(link, 0, sizeof(*link));
        link->index = (ushort)i;
        if (i < (int)n) {
            link->a = (ushort)i;
            link->b = (ushort)((i + 1) % n);
            if (ShadowRand(2)) {
                ushort t = link->a;
                link->a = link->b;
                link->b = t;
            }
        } else {
            link->a = (ushort)ShadowRand(n);
            link->b = (ushort)ShadowRand(n);
        }
        if (ShadowRand(10) == 0)
            link->a = (ushort)ShadowRand(n);
        link->length = ShadowRandFloat(0.5f, 5.0f);
    }
    for (int i = 0; i < kNodes; i++) {
        MAPNODE_tag *file = &world.fileNodes[i];
        memset(file, 0, sizeof(*file));
        file->index = (ushort)i;
        file->path = (ushort)ShadowRand(4);
        file->cel = &world.cels[ShadowRand(2)];
        RandomVector(&file->pos, 8.0f);
        AINode_tag *node = &world.nodes[i];
        memset(node, 0, sizeof(*node));
        node->fileNode = file;
        static const uchar counts[] = {2, 2, 2, 2, 2, 1, 1, 3, 0};
        node->linkCount = counts[ShadowRand(sizeof(counts))];
        node->firstLink = (ushort)(4 * i);
        ushort before = (ushort)((i + n - 1) % n), after = (ushort)(i % n);
        if (i >= (int)n)
            before = after = (ushort)ShadowRand(n);
        bool swap = ShadowRand(2) != 0;
        world.nodeLinks[4 * i] = swap ? after : before;
        world.nodeLinks[4 * i + 1] = swap ? before : after;
        world.nodeLinks[4 * i + 2] = (ushort)ShadowRand(kLinks);
        world.nodeLinks[4 * i + 3] = (ushort)ShadowRand(kLinks);
    }
    memcpy(world.pathName, "path_7", 7);
}

// The drone's cel holds nodes of the four paths, some within 0.3 m of it in plan
void RandomCelNodes() {
    world.cels[0].aiNodes = NULL;
    int count = (int)ShadowRand(kCelNodes + 1);
    for (int i = 0; i < count; i++) {
        AINode_tag *node = &world.celNodes[i];
        int which = (int)ShadowRand(kNodes);
        *node = world.nodes[which];
        node->fileNode = &world.fileNodes[which];
        node->nextInCel = world.cels[0].aiNodes;
        world.cels[0].aiNodes = node;
        _VECTOR *pos = &world.fileNodes[which].pos;
        switch (ShadowRand(5)) {
        case 0:
            pos->x = world.obj.position.x + ShadowRandFloat(-0.2f, 0.2f);
            pos->z = world.obj.position.z + ShadowRandFloat(-0.2f, 0.2f);
            break;
        case 1:
            pos->x = world.obj.position.x + 0.3f;
            pos->z = world.obj.position.z;
            break;
        case 2:
            pos->x = world.obj.position.x;
            pos->z = world.obj.position.z;
            break;
        case 3:
            if (ShadowRand(4) == 0)
                pos->x = NAN;
            break;
        }
    }
}

void RandomPoints() {
    static const ushort ids[] = {0, 1, 2, 8, 9, 3};
    for (int i = 0; i < kPoints; i++) {
        DroneAIPoint_tag *point = &world.points[i];
        memset(point, 0, sizeof(*point));
        point->active = (uchar)(ShadowRand(5) != 0);
        point->id = ids[ShadowRand(sizeof(ids) / sizeof(ids[0]))];
        switch (ShadowRand(4)) {
        case 0:
            point->group = 0;
            break;
        case 1:
            point->group = (uint)world.drone.group;
            break;
        default:
            point->group = ShadowRand(4);
            break;
        }
        point->radius = RandomFloat(0.0f, 3.0f);
        RandomVector(&point->pos, 10.0f);
        point->cel = ShadowRand(3) ? &world.cels[ShadowRand(2)] : NULL;
        point->rot.y = RandomFloat(-3.0f, 3.0f);
        point->emitter.path = ShadowRand(6) ? world.drone.route1.path : &AIPaths[3];
        point->emitter.data = ShadowRand(8) ? world.emitterData[i] : NULL;
        point->emitter.size = kNodes;
        static const uchar bytes[] = {0, 1, 7, 7, 30, 254, 255, 255};
        for (int k = 0; k < kNodes; k++)
            world.emitterData[i][k] = ShadowRand(3) ? bytes[ShadowRand(8)] : (uchar)ShadowRand(256);
    }
    NPCGlobals.numAIPoints = ShadowRand(6) == 0 ? 0 : ShadowRand(kPoints + 1);
    NPCGlobals.aiPoints = (DroneAIPoint_tag *)world.points;
}

void RandomDrone() {
    Drone_tag *d = &world.drone;
    obj_tag *o = &world.obj;
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    o->extraObjectData = d;
    o->objectType = OBJECTTYPE_DRONE;
    o->inCel = ShadowRand(8) ? &world.cels[0] : NULL;
    RandomVector(&o->position, 8.0f);
    d->gameObj = o;
    d->group = (int)ShadowRand(4);
    d->side = (uchar)ShadowRand(5);
    for (int w = 0; w < 3; w++)
        d->behaviour1[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    d->currentBehaviour = d->behaviour1;
    static const float alertness[] = {0.0f, 0.5f, 0.6599999f, 0.66f, 0.66000003f, 1.0f, NAN};
    d->alertness = alertness[ShadowRand(sizeof(alertness) / sizeof(alertness[0]))];
    d->route1.nodes = world.route1Nodes;
    d->route2.nodes = world.route2Nodes;
    d->route1.flags = (ushort)ShadowRand(0x100);
    d->route2.flags = (ushort)ShadowRand(0x100);
    d->route1.count = (ushort)ShadowRand(5);
    d->route2.count = (ushort)ShadowRand(5);
    d->route1.arriveRadius = RandomFloat(0.0f, 2.0f);
    d->route1.path = ShadowRand(6) ? &AIPaths[ShadowRand(3)] : NULL;
    d->route2.path = &AIPaths[0];
    d->missionPoint = ShadowRand(3) ? &world.points[ShadowRand(kPoints)] : NULL;
    d->unknown824 = (uchar)ShadowRand(256);
    d->unknown825 = (uchar)ShadowRand(256);
    d->sm.nextState = ShadowRand(3) == 0 ? DSTATE_OpenDoor : ShadowRand(0x90);
    for (int i = 0; i < kNodes; i++) {
        world.route1Nodes[i] = (ushort)ShadowRand(kNodes);
        world.route2Nodes[i] = (ushort)ShadowRand(kNodes);
    }

    world.dcv.gameObj = o;
    world.dcv.drone = d;
    world.dcv.cel = o->inCel;
    world.dcv.aiStateMachine = &d->sm;
}

void RandomRouteWorld() {
    memset(world.cels, 0, sizeof(world.cels));
    RandomDrone();
    RandomPaths();
    RandomCelNodes();
    RandomPoints();
    RandomVector(&world.dest.pos, 10.0f);
    world.dest.cel = ShadowRand(2) ? &world.cels[1] : NULL;
    RandomVector(&world.pos, 10.0f);

    world.nearestNode = ShadowRand(6) == 0 ? -1 : (short)ShadowRand(kNodes);
    world.creepOk = ShadowRand(3) != 0;
    static const uint statuses[] = {0, 0, 1, 2, 3, 4, 7, 7, 9, 10, 11, 12};
    world.followStatus = statuses[ShadowRand(sizeof(statuses) / sizeof(statuses[0]))];
    world.calcStatus = statuses[ShadowRand(sizeof(statuses) / sizeof(statuses[0]))];
    float arrive = world.drone.route1.arriveRadius;
    switch (ShadowRand(7)) {
    case 0:
        world.routeDistance = arrive;
        break;
    case 1:
        world.routeDistance = (double)arrive - 1e-12;   // below the radius only before rounding to float
        break;
    case 2:
        world.routeDistance = (double)arrive + 1e-12;
        break;
    case 3:
        world.routeDistance = NAN;
        break;
    case 4:
        world.routeDistance = -0.0;
        break;
    default:
        world.routeDistance = ShadowRandFloat(0.0f, 10.0f);
        break;
    }
    world.canReach = ShadowRand(2);

    GameState.NumFramesUnpaused = 1000 + ShadowRand(100);
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

void RandomSpawnerWorld() {
    RandomRouteWorld();
    memset(world.entries, 0, sizeof(world.entries));
    memset(world.slots, 0, sizeof(world.slots));
    memset(&world.player, 0, sizeof(world.player));
    world.poolUsed = 0;
    memset(world.pool, 0xcd, sizeof(world.pool));
    RandomVector(&world.player.position, 15.0f);
    if (ShadowRand(20) == 0)
        world.player.position.x = NAN;

    // The spawner, as DroneSpawner_Create and DroneSpawner_Init would leave it, then varied
    DroneSpawner_tag *s = &world.spawner;
    memset(&world.spawnerObj, 0, sizeof(world.spawnerObj));
    memset(s, 0, sizeof(*s));
    world.spawnerObj.extraObjectData = ShadowRand(20) ? s : NULL;
    world.spawnerObj.objectType = OBJECTTYPE_DRONE_SPAWNER;
    world.spawnerObj.flags = (char)(ShadowRand(10) == 0);
    s->kind = ShadowRand(5);
    s->group = ShadowRand(6) == 0 ? 0 : 1 + ShadowRand(30);
    s->entryCount = (ushort)ShadowRand(kTemplates + 1);
    s->maxAlive = (ushort)ShadowRand(kSlots + 1);
    static const ushort waves[] = {1, 2, 3, 32000};
    s->waves = waves[ShadowRand(4)];
    s->startChannel = (ushort)(ShadowRand(2) ? 0 : 1 + ShadowRand(8));
    s->stopChannel = (ushort)(ShadowRand(3) ? 0 : 1 + ShadowRand(8));
    s->doneChannel = (ushort)(ShadowRand(2) ? 0 : 1 + ShadowRand(8));
    s->killChannel = (ushort)(ShadowRand(3) ? 0 : 1 + ShadowRand(8));
    static const float distances[] = {0.0f, 2.0f, 10.0f, 10.0f, 25.0f, NAN};
    s->distance = distances[ShadowRand(sizeof(distances) / sizeof(distances[0]))];
    s->spawn = (uchar)ShadowRand(2);
    s->ready = (uchar)(ShadowRand(8) != 0);
    s->running = (uchar)ShadowRand(2);
    s->anywhere = (uchar)ShadowRand(2);
    s->killing = (uchar)(ShadowRand(4) == 0);
    s->waveSpawned = (ushort)ShadowRand(kSlots + 1);
    s->alive = (ushort)ShadowRand(kSlots);
    s->wave = (ushort)ShadowRand(4);
    s->entryCount2 = ShadowRand(10) == 0 ? (ushort)(0x8000 + ShadowRand(4)) : (ushort)ShadowRand(kTemplates + 2);
    s->spawned = ShadowRand(12) ? world.slots : NULL;
    s->entries = world.entries;
    for (int i = 0; i < kTemplates; i++) {
        DroneSpawnEntry_tag *e = &world.entries[i];
        e->spawned = ShadowRand(3) == 0 ? 1 + ShadowRand(2) : 0;
        e->cel = &world.cels[ShadowRand(2)];
        RandomVector(&e->vars.position, 15.0f);
        e->vars.keys.group = s->group;
    }

    // The drones it made, some gone, dead, dying or deleted
    for (int i = 0; i < kSlots; i++) {
        obj_tag *o = &world.spawnedObjs[i];
        Drone_tag *d = &world.spawned[i];
        memset(o, 0, sizeof(*o));
        memset(d, 0, sizeof(*d));
        o->objectType = ShadowRand(8) ? OBJECTTYPE_DRONE : OBJECTTYPE_DRONE_SPAWNER;
        o->flags = (char)(ShadowRand(8) == 0);
        o->drawnCountdown = (char)(ShadowRand(3) == 0 ? 1 + ShadowRand(2) : 0);
        o->extraObjectData = ShadowRand(12) ? d : NULL;
        d->gameObj = o;
        static const uint flags[] = {0x100, 0x100, 0x100, 0x300, 0x500, 0};
        d->flags = flags[ShadowRand(6)];
        static const float healths[] = {100.0f, 50.0f, 1.0f, 0.0f, -0.0f, -5.0f, NAN};
        d->health = healths[ShadowRand(7)];
        world.slots[i] = ShadowRand(3) ? o : NULL;
    }
    for (int i = 0; i < kNewDrones; i++)
        memset(&world.newObjs[i], 0, sizeof(world.newObjs[i]));

    // The templates' drones, in a group chain no longer than the group's count
    uint chain = ShadowRand(kTemplates + 1);
    for (int i = 0; i < kTemplates; i++) {
        obj_tag *o = &world.templateObjs[i];
        Drone_tag *d = &world.templates[i];
        memset(o, 0, sizeof(*o));
        memset(d, 0, sizeof(*d));
        o->extraObjectData = d;
        o->objectType = OBJECTTYPE_DRONE;
        o->inCel = &world.cels[ShadowRand(2)];
        d->gameObj = o;
        uint8_t *vars = (uint8_t *)&d->diVars;
        for (size_t k = 0; k < sizeof(d->diVars); k++)
            vars[k] = (uint8_t)ShadowRand(256);
        d->groupNext = i + 1 < (int)chain ? &world.templates[i + 1] : NULL;
    }
    memset(NPCGlobals.groups, 0, sizeof(NPCGlobals.groups));
    memset(NPCGlobals.spawners, 0, sizeof(NPCGlobals.spawners));
    memset(NPCGlobals.groupCounts, 0, sizeof(NPCGlobals.groupCounts));
    if (s->group < DRONE_GROUPS) {
        NPCGlobals.groups[s->group] = chain != 0 ? &world.templates[0] : NULL;
        switch (ShadowRand(6)) {
        case 0:
            NPCGlobals.groupCounts[s->group] = 0;
            break;
        case 1:
            NPCGlobals.groupCounts[s->group] = 0xffff;
            if (chain != 0)
                NPCGlobals.groups[s->group] = NULL;     // the chain must fit the entries
            break;
        default:
            NPCGlobals.groupCounts[s->group] = (ushort)(chain != 0 ? chain + ShadowRand(kTemplates + 1 - chain)
                                                                   : ShadowRand(3));
            break;
        }
        NPCGlobals.spawners[s->group] = &world.spawnerObj;
    }
    NPCGlobals.enabled = (uchar)(ShadowRand(10) != 0);
    NPCGlobals.visionTests = (ushort)ShadowRand(0x10000);
    Drone_bDisableSystem = (uchar)(ShadowRand(12) == 0);
    for (int c = 0; c < 10; c++) {
        switch_channels[c] = (char)(ShadowRand(3) == 0);
        switch_channels_time[c] = ShadowRand(5000);
    }

    // The placement DroneSpawner_Create reads
    memset(&world.placement, 0, sizeof(world.placement));
    world.placement.kind = ShadowRand(5);
    world.placement.group = ShadowRand(31);
    world.placement.maxAlive = ShadowRand(3) ? ShadowRand(8) : 0x10000 + ShadowRand(4);
    world.placement.waves = ShadowRand(3) ? ShadowRand(5) : 0x10000;
    world.placement.startChannel = ShadowRand(10);
    world.placement.stopChannel = ShadowRand(10);
    world.placement.doneChannel = ShadowRand(10);
    world.placement.killChannel = ShadowRand(10);
    static const uint placeDistances[] = {0, 0, 1, 10, 16777217, 0x80000000u, 0xffffffffu};
    world.placement.distance = placeDistances[ShadowRand(sizeof(placeDistances) / sizeof(placeDistances[0]))];

    static const HASHCODE levels[] = {HT_Level_Tower2A, HT_Level_Tower2B, HT_Level_Tower2C, HT_Level_EvilBaseC,
                                      HT_Level_Tower2Elevator, HT_Level_PowerStationA1, HT_Level_PowerStationA2,
                                      HT_Level_TowerC, HT_Level_EvilBase, HT_Level_Ravine, HT_Level_CastleIndoors1};
    GameState.CurrentLevelHashcode = levels[ShadowRand(sizeof(levels) / sizeof(levels[0]))];

    world.playerPresent = ShadowRand(5) != 0;
    world.visible = ShadowRand(3) == 0;
    world.visionTestsBump = ShadowRand(2);
    world.newDrone = ShadowRand(5) == 0 ? -1 : (int)ShadowRand(kNewDrones);
    ShadowFakes.createFails = ShadowRand(6) == 0;
}

// ---- the cases

char charArg;
uint uintArg;
ushort ushortArg;
float floatArg;
_VECTOR *vecArg;
cel_tag *celArg;
AINode_tag *nodeArg;
AIPath_tag *pathArg;
AIRoute_tag *routeArg;
CelPos_tag *celPosArg;
obj_tag *objArg;

void TestPaths() {
    for (int trial = 0; trial < 300; trial++) {
        RandomRouteWorld();
        Compare("NDrone2_ObstructedPatrolPath", trial,
                [] {
                    OriginalRoutes o;
                    return ORIGINAL(0x0003a680, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv);
                },
                [] { return (uint32_t)NDrone2_ObstructedPatrolPath(&world.dcv); });
    }
    for (int trial = 0; trial < 12000; trial++) {
        RandomRouteWorld();
        pathArg = &AIPaths[ShadowRand(4)];
        nodeArg = &world.nodes[ShadowRand(kNodes)];
        Compare("NDrone2_InitAIPath", trial,
                [] {
                    OriginalRoutes o;
                    return ORIGINAL(0x00044a20, uint32_t(__cdecl *)(DCVars_tag *, AIPath_tag *, AINode_tag *))(
                               &world.dcv, pathArg, nodeArg) & 0xff;
                },
                [] { return (uint32_t)NDrone2_InitAIPath(&world.dcv, pathArg, nodeArg); });
    }
    for (int trial = 0; trial < 3000; trial++) {
        RandomRouteWorld();
        routeArg = ShadowRand(2) ? &world.drone.route1 : &world.drone.route2;
        charArg = (char)ShadowRand(2);
        Compare("NDrone2_StartRouteCreep", trial,
                [] {
                    OriginalRoutes o;
                    return ORIGINAL(0x00046220, uint32_t(__cdecl *)(int, AIRoute_tag *, DCVars_tag *, char))(
                               0, routeArg, &world.dcv, charArg) & 0xff;
                },
                [] { return (uint32_t)NDrone2_StartRouteCreep(0, routeArg, &world.dcv, charArg); });
    }
    for (int trial = 0; trial < 12000; trial++) {
        RandomRouteWorld();
        static const uint masks[] = {AIPATH_PATROL, AIPATH_MISSION, AIPATH_PATROL | AIPATH_MISSION, 0};
        uintArg = masks[ShadowRand(4)];
        Compare("NDrone2_AssignAIPath", trial,
                [] {
                    OriginalRoutes o;
                    return ORIGINAL(0x00046ad0, uint32_t(__cdecl *)(DCVars_tag *, uint))(&world.dcv, uintArg) & 0xff;
                },
                [] { return (uint32_t)NDrone2_AssignAIPath(&world.dcv, uintArg); });
    }
    for (int trial = 0; trial < 6000; trial++) {
        RandomRouteWorld();
        Compare("NDrone2_UpdatePatrolRoute", trial,
                [] {
                    OriginalRoutes o;
                    return ORIGINAL(0x00046c20, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv);
                },
                [] { return NDrone2_UpdatePatrolRoute(&world.dcv); });
        Compare("NDrone2_UpdateMissionRoute", trial,
                [] {
                    OriginalRoutes o;
                    return ORIGINAL(0x00046d40, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv);
                },
                [] { return NDrone2_UpdateMissionRoute(&world.dcv); });
        routeArg = ShadowRand(2) ? &world.drone.route1 : &world.drone.route2;
        Compare("NDrone2_FollowRoute", trial,
                [] {
                    OriginalRoutes o;
                    return ORIGINAL(0x00046a80, uint32_t(__cdecl *)(AIRoute_tag *, DCVars_tag *))(routeArg,
                                                                                                  &world.dcv);
                },
                [] { return NDrone2_FollowRoute(routeArg, &world.dcv); });
    }
}

void TestGoingTo() {
    for (int trial = 0; trial < 12000; trial++) {
        RandomRouteWorld();
        celPosArg = ShadowRand(4) ? &world.dest : (CelPos_tag *)&world.points[ShadowRand(kPoints)].pos;
        floatArg = RandomFloat(0.0f, 3.0f);
        static const ushort clears[] = {0, 0, 1, 0x80, 0xffff, 0x1234};
        ushortArg = clears[ShadowRand(6)];
        Compare("NDrone2_MoveToGoalPosition", trial,
                [] {
                    OriginalRoutes o;
                    return ORIGINAL(0x00046f30, uint32_t(__cdecl *)(DCVars_tag *, CelPos_tag *, float, uint))(
                        &world.dcv, celPosArg, floatArg, ushortArg);
                },
                [] { return NDrone2_MoveToGoalPosition(&world.dcv, celPosArg, floatArg, ushortArg); });
    }
    for (int trial = 0; trial < 15000; trial++) {
        RandomRouteWorld();
        static const ushort ids[] = {0, 1, 2, 8, 9};
        ushortArg = ids[ShadowRand(5)];
        vecArg = ShadowRand(2) ? &world.pos : NULL;
        celArg = ShadowRand(2) ? &world.cels[1] : NULL;
        Compare("NDrone2_ClosestAIPoint", trial,
                [] {
                    OriginalRoutes o;
                    return (uint32_t)WhoPoint(ORIGINAL(0x000462e0, void *(__cdecl *)(DCVars_tag *, uint, _VECTOR *,
                                                                                      cel_tag *))(
                        &world.dcv, ushortArg, vecArg, celArg));
                },
                [] { return (uint32_t)WhoPoint(NDrone2_ClosestAIPoint(&world.dcv, ushortArg, vecArg, celArg)); });
    }
    for (int trial = 0; trial < 4000; trial++) {
        RandomRouteWorld();
        celPosArg = (CelPos_tag *)&world.points[ShadowRand(kPoints)].pos;
        floatArg = RandomFloat(0.0f, 3.0f);
        Compare("NDrone2_GoToPoint", trial,
                [] {
                    OriginalRoutes o;
                    return ORIGINAL(0x000470b0, uint32_t(__cdecl *)(DCVars_tag *, CelPos_tag *, float))(
                               &world.dcv, celPosArg, floatArg) & 0xff;
                },
                [] { return (uint32_t)NDrone2_GoToPoint(&world.dcv, celPosArg, floatArg); });
    }
    for (int trial = 0; trial < 12000; trial++) {
        RandomRouteWorld();
        Compare("NDrone2_FindRunToPoint", trial,
                [] {
                    OriginalRoutes o;
                    return ORIGINAL(0x00047190, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)NDrone2_FindRunToPoint(&world.dcv); });
        Compare("NDrone2_FindAlarmPoint", trial,
                [] {
                    OriginalRoutes o;
                    return ORIGINAL(0x000470f0, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)NDrone2_FindAlarmPoint(&world.dcv); });
    }
}

void TestSpawners() {
    for (int trial = 0; trial < 6000; trial++) {
        RandomSpawnerWorld();
        Compare("DroneSpawner_Create", trial,
                [] {
                    OriginalRoutes o;
                    ORIGINAL(0x00030530, void(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *))(
                        &world.pos, &world.dest.pos, (level_tag *)&world.placement);
                    return 0u;
                },
                [] {
                    DroneSpawner_Create(&world.pos, &world.dest.pos, (level_tag *)&world.placement);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 8000; trial++) {
        RandomSpawnerWorld();
        Compare("DroneSpawner_Init", trial,
                [] {
                    OriginalRoutes o;
                    ORIGINAL(0x00030670, void(__cdecl *)(obj_tag *))(&world.spawnerObj);
                    return 0u;
                },
                [] {
                    DroneSpawner_Init(&world.spawnerObj);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 3000; trial++) {
        RandomSpawnerWorld();
        if (ShadowRand(2))
            world.spawner.spawned = (obj_tag **)FakeMalloc(kSlots * sizeof(obj_tag *), 0x3404, 0);
        if (ShadowRand(2))
            world.spawner.entries = (DroneSpawnEntry_tag *)FakeMalloc(sizeof(world.entries), 0x3504, 0);
        else if (ShadowRand(2))
            world.spawner.entries = NULL;
        objArg = ShadowRand(10) ? &world.spawnerObj : NULL;
        Compare("DroneSpawner_Delete", trial,
                [] {
                    OriginalRoutes o;
                    ORIGINAL(0x00030800, void(__cdecl *)(obj_tag *))(objArg);
                    return 0u;
                },
                [] {
                    DroneSpawner_Delete(objArg);
                    return 0u;
                });
        objArg = ShadowRand(8) ? &world.spawnedObjs[ShadowRand(kSlots)] : NULL;
        Compare("DroneSpawner_DroneDelete", trial,
                [] {
                    OriginalRoutes o;
                    ORIGINAL(0x000308b0, void(__cdecl *)(obj_tag *, obj_tag *))(&world.spawnerObj, objArg);
                    return 0u;
                },
                [] {
                    DroneSpawner_DroneDelete(&world.spawnerObj, objArg);
                    return 0u;
                });
        // The original tests the slots but not the entries before it clears them: a spawner always has both
        if (world.spawner.entries == NULL)
            world.spawner.entries = world.entries;
        Compare("DroneSpawner_ResetEntries", trial,
                [] {
                    OriginalRoutes o;
                    ORIGINAL(0x00030860, void(__cdecl *)(obj_tag *))(&world.spawnerObj);
                    return 0u;
                },
                [] {
                    DroneSpawner_ResetEntries(&world.spawnerObj);
                    return 0u;
                });
        ushortArg = (ushort)ShadowRand(3);
        Compare("DroneSpawner_WaveTalk", trial,
                [] {
                    OriginalRoutes o;
                    // The original reads only the low word of its argument: the high one is garbage
                    ORIGINAL(0x00030910, void(__cdecl *)(uint))(0xabcd0000u | ushortArg);
                    return 0u;
                },
                [] {
                    DroneSpawner_WaveTalk(ushortArg);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 12000; trial++) {
        RandomSpawnerWorld();
        Compare("DroneSpawner_SpawnDrone", trial,
                [] {
                    OriginalRoutes o;
                    return (uint32_t)WhoObj(ORIGINAL(0x000323e0, obj_tag *(__cdecl *)(obj_tag *))(&world.spawnerObj));
                },
                [] { return (uint32_t)WhoObj(DroneSpawner_SpawnDrone(&world.spawnerObj)); });
    }
    for (int trial = 0; trial < 20000; trial++) {
        RandomSpawnerWorld();
        Compare("DroneSpawner_Control", trial,
                [] {
                    OriginalRoutes o;
                    ORIGINAL(0x000324d0, void(__cdecl *)(obj_tag *))(&world.spawnerObj);
                    return 0u;
                },
                [] {
                    DroneSpawner_Control(&world.spawnerObj);
                    return 0u;
                });
    }
}

} // namespace

void DroneRouteShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], npcGlobals[sizeof(NPCGlobals_t)], paths[sizeof(AIPaths)];
    static char channels[256];
    static uint32_t channelTimes[256];
    static uint32_t rand[2];
    uchar disabled = Drone_bDisableSystem;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(npcGlobals, &NPCGlobals, sizeof(npcGlobals));
    memcpy(paths, &AIPaths, sizeof(paths));
    memcpy(channels, &switch_channels, sizeof(channels));
    memcpy(channelTimes, &switch_channels_time, sizeof(channelTimes));
    XbeOursRead(rand, RandWords.at, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(&AIPaths, sizeof(AIPaths));
    regions.Add(&switch_channels, sizeof(channels));
    regions.Add(&switch_channels_time, sizeof(channelTimes));
    regions.Add(&Drone_bDisableSystem, 1);
    regions.Add(RandWords.game(), 8);
    ShadowFakes_AddRegions(regions);

    InstallFakes();
    NfWarnMuted++;
    cases = differ = 0;

    TestPaths();
    int pathCases = cases, pathDiffer = differ;
    TestGoingTo();
    int goCases = cases, goDiffer = differ;
    TestSpawners();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&NPCGlobals, npcGlobals, sizeof(npcGlobals));
    memcpy(&AIPaths, paths, sizeof(paths));
    memcpy(&switch_channels, channels, sizeof(channels));
    memcpy(&switch_channels_time, channelTimes, sizeof(channelTimes));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
    Drone_bDisableSystem = disabled;

    printf("[droneroute] patrol and mission paths: %d cases, %d differ\n", pathCases, pathDiffer);
    printf("[droneroute] going to positions and AI points: %d cases, %d differ\n", goCases - pathCases,
           goDiffer - pathDiffer);
    printf("[droneroute] spawners: %d cases, %d differ\n", cases - goCases, differ - goDiffer);
}
