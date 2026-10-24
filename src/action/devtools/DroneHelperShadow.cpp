// Shadow tests for the drones' small helpers: making a drone and the dummy player, the aware points, the ninja's
// eyes, the talk helpers, surrender, the alarm message, the hit logs, the cover and combat-move tests. Each original
// and ours run on the same made-up drones, with every callee that needs a level (object creation, anims, sight,
// speech, alerts) going to a logging fake. Run at start with MenuShadowTests=on (MenuProbe.cpp), before any level
// exists; NIGHTFIRE_SHADOW_ONLY=dronehelpers runs them alone.
//
// Bool and char returns are compared in AL and short ones in AX: no original caller reads more (narrow_returns.py).

#include "DroneHelperShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../game/drone/DroneModes.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/NDrone2_Combat.h"
#include "../game/drone/NDrone2_Damage.h"
#include "../game/drone/NDrone2_Special.h"
#include "../game/obj/Drone_AIHints.h"
#include "../game/obj/DroneSpawner.h"
#include "../sound/Sound.h"
#include "../math/math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords (GameSpan<uint32_t>{0x0018cdf8})

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int DRONES = 3;

struct World {
    obj_tag objs[DRONES];
    Drone_tag drones[DRONES];
    AnimState anims[DRONES];
    sAnimSkin_tag skins[2];
    sAnimScript_tag script;
    DYNAMICSOUNDS sound;
    obj_tag player;
    CoverNode_tag node;
    DIVars_tag diVars;
    cel_tag *cel;
    DCVars_tag dcv;

    // Arguments
    obj_tag *objArg;
    Drone_tag *droneArg;
    float floatArg;
    uint uintArg;
    uint firstArg, lastArg;
    uchar ucharArg;
    char charArg;
    _VECTOR posArg;
    CelPos_tag celPosArg;
    CoverNode_tag *nodeArg;
    Drone_tag *droneOrNull;
    _VECTOR outPos, outRot;
    int rotWanted;

    // What the fakes answer: a sequence drawn per call, the same for both runs (the world is restored)
    uint32_t answers;
    uint answerBias;
    int createObjFails;
    int facialFails;
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

// A pointer as an offset into the world, NPCGlobals or the create pool, the same for both runs
int Who(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    if (p == NULL)
        return -1;
    if (b >= (const uint8_t *)&world && b < (const uint8_t *)&world + sizeof(world))
        return (int)(b - (const uint8_t *)&world);
    if (b >= (const uint8_t *)&NPCGlobals && b < (const uint8_t *)&NPCGlobals + sizeof(NPCGlobals))
        return 0x1000000 + (int)(b - (const uint8_t *)&NPCGlobals);
    if (b >= ShadowCreatePool && b < ShadowCreatePool + SHADOW_CREATE_POOL)
        return 0x2000000 + (int)(b - ShadowCreatePool);
    return 0x7fffffff;
}

void LogVector(const char *name, const _VECTOR *v) {
    ShadowLog("%s=%08x,%08x,%08x", name, Bits(v->x), Bits(v->y), Bits(v->z));
}

// ---- the fakes

void __cdecl FakeInitObject(obj_tag *obj) {
    ShadowLog("initobj(%x)", Who(obj));
}
cel_tag *__cdecl FakeFindCel(_VECTOR *pos, world_tag *w) {
    ShadowLog("findcel(%x,%p)", Who(pos), w);
    LogVector("at", pos);
    return world.cel;
}
obj_tag *__cdecl FakeCreateObj(DIVars_tag *diVars) {
    ShadowLog("createobj(%x)", Who(diVars));
    if (world.createObjFails)
        return NULL;
    return &world.objs[1];
}
sAnimScript_tag *__cdecl FakeFacialAppend(obj_tag *obj, HASHCODE hc) {
    ShadowLog("facial(%x,%x)", Who(obj), (uint)hc);
    return world.facialFails ? NULL : &world.script;
}
bool __cdecl FakeCanSeeObject(Drone_tag *drone, obj_tag *obj, uint bone, uint mask) {
    ShadowLog("cansee(%x,%x,%u,%x)", Who(drone), Who(obj), bone, mask & 0xffff);
    return Yes();
}
bool __cdecl FakeOpponentHasWeapon(DCVars_tag *dcv) {
    ShadowLog("hasweapon(%x)", Who(dcv));
    return Yes();
}
int __cdecl FakeBondIsFacingMe(DCVars_tag *dcv, float degrees) {
    ShadowLog("facing(%x,%08x)", Who(dcv), Bits(degrees));
    return Yes();
}
void __cdecl FakeAlertToObject(DCVars_tag *dcv, uint msgType, obj_tag *obj, float a, float b, float c) {
    ShadowLog("alertobj(%x,%x,%x,%08x,%08x,%08x)", Who(dcv), msgType, Who(obj), Bits(a), Bits(b), Bits(c));
}
bool __cdecl FakeDroneNearPos(_VECTOR *pos, float radius, obj_tag *except) {
    ShadowLog("nearpos(%x,%08x,%x)", Who(pos), Bits(radius), Who(except));
    return Yes();
}
void __cdecl FakeLogShotHitEnemy(uint player) {
    ShadowLog("loghit(%u)", player);
}
void __cdecl FakeBoneWorldTrans(obj_tag *obj, uint which, int datum, _VECTOR *pos, _MATRIX *mtx) {
    ShadowLog("bone(%x,%x,%d,%x,%x)", Who(obj), which, datum, Who(pos), Who(mtx));
    pos->x = (float)which;
    pos->y = (float)which * 0.5f;
    pos->z = -(float)which;
}
void __cdecl FakeSetEntityGfx(obj_tag *obj, HASHCODE hc) {
    ShadowLog("gfx(%x,%x)", Who(obj), (uint)hc);
}
void __cdecl FakeLinkToCel(obj_tag *obj, cel_tag *cel) {
    ShadowLog("linkcel(%x,%p)", Who(obj), cel);
}
DYNAMICSOUNDS *__cdecl FakeSpeech(DCVars_tag *dcv, uint sfx, char force, char face, char needFace) {
    ShadowLog("speech(%x,%x,%d,%d,%d)", Who(dcv), sfx, force, face, needFace);
    return NULL;
}
uint32_t __cdecl FakeCanDoAnimState(DCVars_tag *dcv, short state, short variant) {
    ShadowLog("candoanim(%x,%x,%x)", Who(dcv), state & 0xffff, variant & 0xffff);
    return Yes();
}
// The side moves' and cover anims' tests, each told apart by its address
template <unsigned AT> uint32_t __cdecl FakeDcvTest(DCVars_tag *dcv) {
    ShadowLog("test%x(%x)", AT, Who(dcv));
    return Yes();
}
template <unsigned AT> uint32_t __cdecl FakeNodeTest(DCVars_tag *dcv, CoverNode_tag *node) {
    ShadowLog("test%x(%x,%x)", AT, Who(dcv), Who(node));
    return Yes();
}
uint32_t __cdecl FakeCornerPos(CoverNode_tag *node, ushort kind, _VECTOR *pos, _VECTOR *rot, char step) {
    ShadowLog("cornerpos(%x,%x,%x,%x,%d)", Who(node), kind, Who(pos), Who(rot), step);
    return Yes();
}
uint32_t __cdecl FakeFeetPos(obj_tag *obj, _VECTOR *feet) {
    ShadowLog("feet(%x,%x)", Who(obj), Who(feet));
    if (obj == NULL)
        return 0;
    feet->x = obj->position.x;
    feet->y = obj->position.y - 1.1f;
    feet->z = obj->position.z;
    return 1;
}
cel_tag *__cdecl FakeDroneFindCel(_VECTOR *pos) {
    ShadowLog("dronecel(%x)", Who(pos));
    LogVector("at", pos);
    return world.cel;
}
uint32_t __cdecl FakeMoveTest(_VECTOR *from, cel_tag *fromCel, _VECTOR *to, cel_tag *toCel, obj_tag *objA,
                              obj_tag *objB, _VECTOR *hit, char strict, char checkBlocked) {
    ShadowLog("movetest(%x,%p,%x,%p,%x,%x,%x,%d,%d)", Who(from), fromCel, Who(to), toCel, Who(objA), Who(objB),
              Who(hit), strict, checkBlocked);
    LogVector("from", from);
    LogVector("to", to);
    return Answer(3);
}
float __cdecl FakeSpeedMul(Drone_tag *drone) {
    ShadowLog("speedmul(%x)", Who(drone));
    return 0.5f + 0.25f * Answer(4);
}

void InstallFakes() {
    ShadowFakes_InstallCreate();
    ShadowHook_Install(0x0002d190, (const void *)&FakeInitObject);
    ShadowHook_Install(0x00020c30, (const void *)&FakeFindCel);
    ShadowHook_Install(0x0003f940, (const void *)&FakeCreateObj);
    ShadowHook_Install(0x00017160, (const void *)&FakeFacialAppend);
    ShadowHook_Install(0x000648b0, (const void *)&FakeCanSeeObject);
    ShadowHook_Install(0x00066610, (const void *)&FakeOpponentHasWeapon);
    ShadowHook_Install(0x00043ee0, (const void *)&FakeBondIsFacingMe);
    ShadowHook_Install(0x00038f40, (const void *)&FakeAlertToObject);
    ShadowHook_Install(0x00039870, (const void *)&FakeDroneNearPos);
    ShadowHook_Install(0x000b06f0, (const void *)&FakeLogShotHitEnemy);
    ShadowHook_Install(0x00013070, (const void *)&FakeBoneWorldTrans);
    ShadowHook_Install(0x0006b5e0, (const void *)&FakeSetEntityGfx);
    ShadowHook_Install(0x0002d200, (const void *)&FakeLinkToCel);
    ShadowHook_Install(0x00037530, (const void *)&FakeSpeech);
}

// For the combat moves, cover and walking: what they call is faked; their own neighbours run for real
void InstallMoveFakes() {
    ShadowHook_Install(0x000330c0, (const void *)&FakeCanDoAnimState);
    ShadowHook_Install(0x00045a10, (const void *)&FakeDcvTest<0x45a10>);
    ShadowHook_Install(0x00045aa0, (const void *)&FakeDcvTest<0x45aa0>);
    ShadowHook_Install(0x00045720, (const void *)&FakeDcvTest<0x45720>);
    ShadowHook_Install(0x000457c0, (const void *)&FakeDcvTest<0x457c0>);
    ShadowHook_Install(0x00045870, (const void *)&FakeDcvTest<0x45870>);
    ShadowHook_Install(0x00045900, (const void *)&FakeDcvTest<0x45900>);
    ShadowHook_Install(0x000455f0, (const void *)&FakeDcvTest<0x455f0>);
    ShadowHook_Install(0x00045680, (const void *)&FakeDcvTest<0x45680>);
    ShadowHook_Install(0x00043650, (const void *)&FakeDcvTest<0x43650>);
    ShadowHook_Install(0x00033870, (const void *)&FakeNodeTest<0x33870>);
    ShadowHook_Install(0x000338b0, (const void *)&FakeNodeTest<0x338b0>);
    ShadowHook_Install(0x00033990, (const void *)&FakeNodeTest<0x33990>);
    ShadowHook_Install(0x00033ac0, (const void *)&FakeNodeTest<0x33ac0>);
    ShadowHook_Install(0x00033b70, (const void *)&FakeNodeTest<0x33b70>);
    ShadowHook_Install(0x00033bf0, (const void *)&FakeNodeTest<0x33bf0>);
    ShadowHook_Install(0x00044490, (const void *)&FakeCornerPos);
    ShadowHook_Install(0x00036dc0, (const void *)&FakeFeetPos);
    ShadowHook_Install(0x00036b80, (const void *)&FakeDroneFindCel);
    ShadowHook_Install(0x00044030, (const void *)&FakeMoveTest);
    ShadowHook_Install(0x0001ac30, (const void *)&FakeSpeedMul);
}

// ---- the made-up world

float RandomFloat() {
    static const float edges[] = {0.0f, -0.0f, 0.66f, 0.65999997f, 2.0f, 1.9999999f, 2.0943952f, -2.0943952f,
                                  2.0943954f, -2.0943954f, 1.0f, NAN, -NAN, PI, -PI};
    switch (ShadowRand(3)) {
    case 0:
        return Pick(edges);
    case 1:
        return ShadowRandFloat(-4.0f, 4.0f);
    default:
        return ShadowRandFloat(0.0f, 3.0f);
    }
}

void RandomBytes(void *at, size_t n) {
    uint8_t *b = (uint8_t *)at;
    for (size_t i = 0; i < n; i++)
        *(uint8_t *)(size_t)XbeOurs((unsigned)(size_t)(b + i)) = (uint8_t)ShadowRand(256);
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    memset(&world.anims[i], 0, sizeof(world.anims[i]));
    o->extraObjectData = ShadowRand(8) ? d : NULL;
    o->animState = ShadowRand(6) ? &world.anims[i] : NULL;
    o->position.x = ShadowRandFloat(-10.0f, 10.0f);
    o->position.y = ShadowRandFloat(-2.0f, 2.0f);
    o->position.z = ShadowRandFloat(-10.0f, 10.0f);
    o->inCel = (cel_tag *)(uintptr_t)(0x1000 + i);
    world.anims[i].currentWeaponId = (char)ShadowRand(256);
    world.anims[i].animObj.skin = &world.skins[ShadowRand(2)];
    d->gameObj = o;
    d->sm.id = ShadowRand(1000);
    d->skinHashcode = (HASHCODE)(HT_Skin_HASHCODE_BASE + ShadowRand(0x100));
    d->side = (uchar)ShadowRand(4);
    d->flags = (ShadowRand(2) ? 0x200000 : 0) | ShadowRand(0x100000);
    d->knowledge = ShadowRand(0x40);
    d->alertness = RandomFloat();
    d->distanceToTarget = RandomFloat();
    d->opponentBearing = RandomFloat();
    d->health = RandomFloat();
    d->opponent = ShadowRand(5) ? &world.player : NULL;
    static obj_tag *const attackers[] = {NULL, &world.player, &world.objs[2]};
    d->lastAttacker = Pick(attackers);
    if (ShadowRand(3) == 0)
        d->lastAttacker = d->opponent;
    d->speechChannel = ShadowRand(2) ? &world.sound : NULL;
    for (int w = 0; w < 3; w++)
        d->behaviour1[w] = ShadowRand(4) ? ShadowRand(0x10000) | (ShadowRand(0x10000) << 16) : 0;
    d->currentBehaviour = d->behaviour1;
    d->facialAnimDone = (uchar)ShadowRand(3);
    d->facialAnimPlaying = (uchar)ShadowRand(3);
    d->unknown37 = (uchar)ShadowRand(3);
    d->dtype = ShadowRand(2) ? DTYPE_Bot : (uchar)ShadowRand(30);
    d->flags |= ShadowRand(3) ? 0 : 0x10;
    d->coverNode = &world.node;
    d->coverFlags = (ushort)ShadowRand(0x10000);
    d->targetSlot = (uchar)ShadowRand(MAX_DRONE_TARGETS);
    d->moveTarget.x = o->position.x + ShadowRandFloat(-1.0f, 1.0f);
    d->moveTarget.y = o->position.y + ShadowRandFloat(-1.0f, 1.0f);
    d->moveTarget.z = o->position.z + ShadowRandFloat(-1.0f, 1.0f);
    d->moveTargetRadius = RandomFloat();
    if (ShadowRand(4) == 0) {
        // exactly 5 m away in plan, at a radius of 5 now and then
        o->position.x = (float)ShadowRand(8);
        o->position.z = (float)ShadowRand(8);
        d->moveTarget.x = o->position.x + 3.0f;
        d->moveTarget.z = o->position.z - 4.0f;
        if (ShadowRand(2))
            d->moveTargetRadius = 5.0f;
    }
    d->moveTargetDistance = RandomFloat();
    float *m = (float *)&o->transformMatrix;
    for (size_t k = 0; k < sizeof(o->transformMatrix) / sizeof(float); k++)
        m[k] = ShadowRandFloat(-1.0f, 1.0f);
}

void RandomWorld() {
    world.answers = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    world.answerBias = 1 + ShadowRand(3);
    for (int i = 0; i < DRONES; i++)
        RandomDrone(i);
    for (int i = 0; i < DRONES; i++) {
        world.drones[i].next = i + 1 < DRONES ? &world.drones[i + 1] : NULL;
        world.drones[i].prev = i > 0 ? &world.drones[i - 1] : NULL;
    }
    memset(&world.player, 0, sizeof(world.player));
    world.player.objectType = OBJECTTYPE_PLAYER;
    world.skins[0].hashcode = HT_Skin_Ninja;
    world.skins[1].hashcode = (HASHCODE)(HT_Skin_HASHCODE_BASE + ShadowRand(0x30));
    memset(&world.script, 0, sizeof(world.script));
    world.script.flags = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    world.script.playMode = (uchar)ShadowRand(4);
    memset(&world.sound, 0, sizeof(world.sound));
    world.sound.sfxId = ShadowRand(2) ? ShadowRand(0x400) : ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    memset(&world.node, 0, sizeof(world.node));
    world.node.type = (uchar)ShadowRand(3);
    world.node.flags = ShadowRand(0x200);
    world.node.cel = (cel_tag *)(uintptr_t)(0x3000 + ShadowRand(4));
    world.node.occupant = NULL;
    for (int t = 0; t < MAX_DRONE_TARGETS; t++)
        world.node.usable[t] = ShadowRand(0x20);
    world.nodeArg = ShadowRand(8) ? &world.node : NULL;
    world.charArg = (char)(ShadowRand(4) == 0 ? 0x40 + ShadowRand(4) : ShadowRand(4));
    world.celPosArg.pos.x = ShadowRandFloat(-10.0f, 10.0f);
    world.celPosArg.pos.y = ShadowRandFloat(-2.0f, 2.0f);
    world.celPosArg.pos.z = ShadowRandFloat(-10.0f, 10.0f);
    world.celPosArg.cel = ShadowRand(2) ? (cel_tag *)(uintptr_t)0x4000 : NULL;
    world.droneOrNull = ShadowRand(5) ? &world.drones[ShadowRand(DRONES)] : NULL;
    memset(&world.outPos, 0x55, sizeof(world.outPos));
    memset(&world.outRot, 0x55, sizeof(world.outRot));
    world.rotWanted = ShadowRand(2);
    RandomBytes(&world.diVars, sizeof(world.diVars));
    world.cel = (cel_tag *)(uintptr_t)(0x2000 + ShadowRand(4));

    world.objs[1].extraObjectData = &world.drones[1];
    world.objs[1].animState = &world.anims[1];
    world.objs[2].animState = &world.anims[2];
    world.dcv.gameObj = &world.objs[0];
    world.dcv.drone = &world.drones[0];
    world.dcv.cel = NULL;
    world.dcv.aiStateMachine = &world.drones[0].sm;
    world.objArg = &world.objs[ShadowRand(DRONES)];
    world.droneArg = &world.drones[ShadowRand(DRONES)];
    world.floatArg = RandomFloat();
    world.uintArg = ShadowRand(0x15a);
    world.firstArg = ShadowRand(0x100);
    world.lastArg = world.firstArg + ShadowRand(10);
    world.ucharArg = (uchar)ShadowRand(256);
    world.posArg.x = ShadowRandFloat(-10.0f, 10.0f);
    world.posArg.y = ShadowRandFloat(-10.0f, 10.0f);
    world.posArg.z = ShadowRandFloat(-10.0f, 10.0f);
    world.createObjFails = ShadowRand(6) == 0;
    world.facialFails = ShadowRand(4) == 0;
    ShadowFakes.createFails = ShadowRand(6) == 0;

    NPCGlobals.numAwarePoints = ShadowRand(2) ? ShadowRand(MAX_AWARE_POINTS + 3) : MAX_AWARE_POINTS - ShadowRand(3);
    NPCGlobals.dummyPlayerActive = (uchar)ShadowRand(2);
    NPCGlobals.dummyPlayer = NULL;
    GameState.NumFramesUnpaused = ShadowRand(100000);
    FRAME_RATE_INT = ShadowRand(2) ? 60 : 50;
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

// ---- running the two

enum Kind {
    K_VOID,             // ()
    K_DIVARS,           // (&diVars)
    K_OBJ,              // (objArg)
    K_AWARE,            // (ucharArg, objArg, &posArg, cel, floatArg)
    K_DCV,              // (dcv)
    K_DCV_FLOAT,        // (dcv, floatArg)
    K_DCV_NODE,         // (dcv, &node)
    K_DCV_UINT,         // (dcv, uintArg)
    K_DRONE,            // (droneArg)
    K_DRONE_UINT,       // (droneArg, uintArg)
    K_SFX_ID,           // (droneArg, firstArg, lastArg)
    K_DCV_NODE_OR_NULL, // (dcv, nodeArg)
    K_DCV_CHAR,         // (dcv, charArg)
    K_COVER_POS,        // (&node, uintArg, &outPos, &outRot or NULL, charArg)
    K_WALK,             // (droneOrNull, objArg, &celPosArg, charArg)
    K_RELATIVE,         // (&posArg, droneOrNull, objArg, unscaled, charArg); unscaled without a drone
    K_DCV_CELPOS,       // (dcv, &celPosArg)
};

struct Case {
    const char *name;
    unsigned at;
    Kind kind;
    const void *ours;
    uint32_t mask;      // of the return register compared
};

const Case *current;

uint32_t Call(const void *f) {
    World &w = world;
    switch (current->kind) {
    case K_VOID:
        return ORIGINAL(f, uint32_t(__cdecl *)(void))();
    case K_DIVARS:
        return ORIGINAL(f, uint32_t(__cdecl *)(DIVars_tag *))(&w.diVars);
    case K_OBJ:
        return ORIGINAL(f, uint32_t(__cdecl *)(obj_tag *))(w.objArg);
    case K_AWARE:
        return ORIGINAL(f, uint32_t(__cdecl *)(uchar, obj_tag *, _VECTOR *, cel_tag *, float))(
            w.ucharArg, w.objArg, &w.posArg, w.cel, w.floatArg);
    case K_DCV:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *))(&w.dcv);
    case K_DCV_FLOAT:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, float))(&w.dcv, w.floatArg);
    case K_DCV_NODE:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, CoverNode_tag *))(&w.dcv, &w.node);
    case K_DCV_UINT:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, uint))(&w.dcv, w.uintArg);
    case K_DRONE:
        return ORIGINAL(f, uint32_t(__cdecl *)(Drone_tag *))(w.droneArg);
    case K_DRONE_UINT:
        return ORIGINAL(f, uint32_t(__cdecl *)(Drone_tag *, uint))(w.droneArg, w.uintArg);
    case K_SFX_ID:
        return ORIGINAL(f, uint32_t(__cdecl *)(Drone_tag *, uint, uint))(w.droneArg, w.firstArg, w.lastArg);
    case K_DCV_NODE_OR_NULL:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, CoverNode_tag *))(&w.dcv, w.nodeArg);
    case K_DCV_CHAR:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, char))(&w.dcv, w.charArg);
    case K_COVER_POS:
        return ORIGINAL(f, uint32_t(__cdecl *)(CoverNode_tag *, uint, _VECTOR *, _VECTOR *, char))(
            &w.node, w.uintArg % MAX_DRONE_TARGETS, &w.outPos, w.rotWanted ? &w.outRot : NULL, w.charArg);
    case K_WALK:
        return ORIGINAL(f, uint32_t(__cdecl *)(Drone_tag *, obj_tag *, CelPos_tag *, char))(w.droneOrNull, w.objArg,
                                                                                          &w.celPosArg, w.charArg);
    case K_RELATIVE:
        return ORIGINAL(f, uint32_t(__cdecl *)(_VECTOR *, Drone_tag *, obj_tag *, char, char))(
            &w.posArg, w.droneOrNull, w.objArg, w.droneOrNull == NULL ? 1 : w.ucharArg & 1, w.charArg);
    case K_DCV_CELPOS:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, CelPos_tag *))(&w.dcv, &w.celPosArg);
    default:
        return 0;
    }
}

uint32_t RunTheirs() {
    XbeOriginalScope o(current->at);
    return Call((const void *)(uintptr_t)current->at) & current->mask;
}

uint32_t RunOurs() {
    return Call(current->ours) & current->mask;
}

#define AL 0xffu
#define AX 0xffffu
#define ALL 0xffffffffu
#define CASE(name, at, kind, mask) {#name, at, kind, (const void *)&name, mask}

const Case kMaking[] = {
    CASE(DroneSpawner_CreateBlank, 0x0002fb90, K_VOID, 0),
    CASE(NDrone2_CreateFromDIVars, 0x0002fc50, K_DIVARS, ALL),
    CASE(Drone_GetSkin, 0x00030a10, K_OBJ, ALL),
    CASE(Drone_AddDynamicAwarePoint, 0x00030e10, K_AWARE, 0),
    CASE(NDrone2_CreateNinjaEyes, 0x0003b6c0, K_OBJ, 0),
    CASE(NDrone2_init_DMODE_Normal, 0x000408a0, K_DRONE, 0),
};

const Case kTalk[] = {
    CASE(Drone_IsSayingSfx, 0x00036230, K_DRONE_UINT, AL),
    CASE(DroneFunc_ChooseSFXByID, 0x00036290, K_SFX_ID, ALL),
    CASE(NDrone2_PlayFacialAnim, 0x000374e0, K_DCV_UINT, 0),
    CASE(DroneFunc_InjuredSFX, 0x0003b830, K_DCV_UINT, AL),
};

const Case kCombat[] = {
    CASE(DroneAnim_CanStandAtCover, 0x00033a40, K_DCV_NODE, AL),
    CASE(DroneFunc_InRangeOfTarget, 0x00035f50, K_DCV_FLOAT, AL),
    CASE(DroneFunc_WeaponInHand, 0x00035f70, K_DCV, AL),
    CASE(NDrone2_CheckSurrender, 0x00038bd0, K_DCV, AL),
    CASE(NDrone2_LogHitByOpponent, 0x0003a240, K_DCV, 0),
    CASE(NDrone2_LogHitByOpponentOrAlive, 0x0003a270, K_DCV, 0),
    CASE(NDrone2_SendAttackMessage, 0x0003cd80, K_DCV, 0),
    CASE(NDrone2_NearDrone, 0x0003cdf0, K_DCV_FLOAT, AL),
};

const Case kMoves[] = {
    CASE(NDrone2_StepAllowed, 0x00043510, K_DCV, AL),
    CASE(NDrone2_StrafeAllowed, 0x00043560, K_DCV, AL),
    CASE(NDrone2_StrafeDodgeAllowed, 0x000435b0, K_DCV, AL),
    CASE(NDrone2_RollAllowed, 0x00043600, K_DCV, AL),
    CASE(NDrone2_CanCrouchWalk, 0x000436b0, K_DCV, AL),
    CASE(NDrone2_CanMove8, 0x00043700, K_DCV, AL),
    CASE(NDrone2_ChooseAimStand, 0x000437b0, K_VOID, AX),
    CASE(NDrone2_ChooseAimCrouch, 0x000437d0, K_DCV, AX),
    CASE(NDrone2_ChooseRoll, 0x00045b30, K_DCV, AX),
    CASE(NDrone2_ChooseStrafe, 0x00045bc0, K_DCV, AX),
    CASE(NDrone2_ChooseStrafeDodge, 0x00045c30, K_DCV, AX),
    CASE(NDrone2_ChooseStep, 0x00045ca0, K_DCV, AX),
};

const Case kCover[] = {
    CASE(NDrone2_CornerCoverAnimOK, 0x00044250, K_DCV_NODE_OR_NULL, AL),
    CASE(NDrone2_LowCoverOK, 0x000442e0, K_DCV_NODE, AL),
    CASE(NDrone2_GetCoverPosForTarget, 0x000445a0, K_COVER_POS, AL),
    CASE(NDrone2_TakeCover, 0x000464a0, K_DCV_CHAR, 0),
};

const Case kWalking[] = {
    CASE(NDrone2_AtDest, 0x00043fd0, K_DCV, AL),
    CASE(NDrone2_CanWalkStraightTo, 0x00045340, K_WALK, AL),
    CASE(NDrone2_MoveTestRelative, 0x000453e0, K_RELATIVE, AL),
    CASE(NDrone2_SetMoveTarget, 0x00046080, K_DCV_CELPOS, 0),
};

int Test(const Case *cases, int count, int trials, int *differ) {
    int run = 0;
    for (int c = 0; c < count; c++) {
        current = &cases[c];
        for (int trial = 0; trial < trials; trial++) {
            RandomWorld();
            if (current->at == 0x0003b6c0)
                world.objArg = &world.objs[1 + ShadowRand(2)];
            if (current->at == 0x00035f70 && ShadowRand(10) == 0)
                world.dcv.gameObj = NULL;
            run++;
            if (!ShadowCompare(regions, "dronehelpers", current->name, trial, RunTheirs, RunOurs))
                (*differ)++;
        }
    }
    return run;
}

} // namespace

void DroneHelperShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], npc[sizeof(NPCGlobals_t)];
    static uint32_t rand[2];
    uint32_t frameRate = FRAME_RATE_INT;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    XbeOursRead(rand, RandWords.at, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(RandWords.game(), 8);
    regions.Add(&FRAME_RATE_INT, 4);
    regions.Add((void *)0x001ee420, 12);    // NDrone2_CanWalkStraightTo's target
    ShadowFakes_AddRegions(regions);

    InstallFakes();
    NfWarnMuted++;

    struct Group {
        const char *what;
        const Case *cases;
        int count;
        int trials;
    };
    const Group groups[] = {
        {"making drones and effects", kMaking, sizeof(kMaking) / sizeof(kMaking[0]), 3000},
        {"talk helpers", kTalk, sizeof(kTalk) / sizeof(kTalk[0]), 3000},
        {"combat helpers", kCombat, sizeof(kCombat) / sizeof(kCombat[0]), 3000},
        {"combat moves", kMoves, sizeof(kMoves) / sizeof(kMoves[0]), 3000},
        {"cover", kCover, sizeof(kCover) / sizeof(kCover[0]), 3000},
        {"walking", kWalking, sizeof(kWalking) / sizeof(kWalking[0]), 3000},
    };
    const int n = sizeof(groups) / sizeof(groups[0]);
    int cases[n], differ[n];
    for (int g = 0; g < n; g++) {
        if (groups[g].cases == kMoves)
            InstallMoveFakes();
        differ[g] = 0;
        cases[g] = Test(groups[g].cases, groups[g].count, groups[g].trials, &differ[g]);
    }

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    ShadowFakes.createFails = false;
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
    FRAME_RATE_INT = frameRate;

    for (int g = 0; g < n; g++)
        printf("[dronehelpers] %s: %d cases, %d differ\n", groups[g].what, cases[g], differ[g]);
}
