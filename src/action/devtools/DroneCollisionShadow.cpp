// Shadow tests for a drone's collision and tracking (game/drone/DroneCollision.cpp, game/drone/DroneTracking.cpp, and
// AnimGetCollData, AnimObjectIsAiming, AnimObjectIsHeadTracking and AnimObjectSetAiming in engine/Anim.cpp): each
// original and ours run on the same made-up drones, with what needs a level going to logging fakes. Run at start
// with MenuShadowTests=on (MenuProbe.cpp), before any level exists.
//
// Faked, so not tested here: the ray under the feet (Collide_RayIntersect answers from a made-up hit list), the hit
// list's freeing, the movement taken from what the feet stand on (PlayerOrDrone_FeetOnPoint_DoCollision), the
// skeleton (AnimGetBoneWorldTrans, psiBuildMatrixPalette: the bone matrices are made up), the AI network's bounds
// push, the head tracking anim (AnimObjectSetHeadTracking) and the water ripple. NDrone2_DealWithObjHit runs for
// real, on hits whose objects end it early. Random draws are kept in step: both sides start from the same
// Rand_Random state, and it is compared afterwards.

#include "DroneCollisionShadow.h"
#include "AllOriginal.h"
#include "../game/obj/Player.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../engine/Collide.h"
#include "../engine/viewer.h"
#include "../game/drone/BOT.h"
#include "../game/drone/DroneCollision.h"
#include "../game/drone/DroneTracking.h"
#include "../game/drone/NDrone2.h"
#include "../game/mp/multiplayer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords (GameSpan<uint32_t>{0x0018cdf8})
// AnimGetCollData's boxes (16 of them for either table)
#define HitList2Block ((uint8_t *)(size_t)XbeOurs(0x001d6bf0))
const int kHitList2Bytes = 16 * sizeof(COLLJOINT_tag);

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int kDrones = 3;
const int kBones = 64;

struct World {
    obj_tag objs[kDrones];
    Drone_tag drones[kDrones];
    AnimState anims[kDrones];
    obj_tag player;
    obj_tag other;              // objectType ROTOR: NDrone2_DealWithObjHit's default
    obj_tag ladder;             // ends NDrone2_DealWithObjHit at once
    cel_tag cels[2];
    HITDATA_tag rayHits[3];     // what Collide_RayIntersect answers
    HITDATA_tag objHits[3];     // the object's hit list
    BOT_vars_t botVars;
    sAnimSkin_tag skin;
    uchar skinBones[kBones];
    _MATRIX bones[kBones];
    sAnimScript_tag script;
    HITTEST_tag test;
    DCVars_tag dcv;

    // Arguments and what the fakes answer
    ushort count;
    float slope, height;
    int rayHitCount;
    _VECTOR push;
};
World world;

ShadowRegions regions;
int cases, differ;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "dronecollision", what, trial, theirs, ours))
        differ++;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

int WhoObj(const obj_tag *obj) {
    for (int i = 0; i < kDrones; i++)
        if (obj == &world.objs[i])
            return i;
    if (obj == &world.player)
        return 100;
    if (obj == &world.other)
        return 101;
    if (obj == &world.ladder)
        return 102;
    return obj == NULL ? -1 : 999;
}

int WhoHit(const HITDATA_tag *hit) {
    for (int i = 0; i < 3; i++) {
        if (hit == &world.rayHits[i])
            return i;
        if (hit == &world.objHits[i])
            return 10 + i;
    }
    return hit == NULL ? -1 : 999;
}

int WhoCel(const cel_tag *cel) {
    for (int i = 0; i < 2; i++)
        if (cel == &world.cels[i])
            return i;
    return cel == NULL ? -1 : 999;
}

void LogVector(const char *name, const _VECTOR *v) {
    ShadowLog("%s=%08x,%08x,%08x", name, Bits(v->x), Bits(v->y), Bits(v->z));
}

// ---- the fakes

void __cdecl FakeBone(obj_tag *obj, uint which, int datum, _VECTOR *pos, _MATRIX *mtx) {
    ShadowLog("bone(%d,%x,%d,%d)", WhoObj(obj), which, datum, mtx != NULL);
    float base = (float)(which & 0xff) * 0.0625f;
    pos->x = obj->position.x + base;
    pos->y = obj->position.y + 1.5f - base;
    pos->z = obj->position.z - base * 0.5f;
}

void __cdecl FakePalette(obj_tag *obj, AnimObj *animObj, char build) {
    ShadowLog("palette(%d,%d,%d)", WhoObj(obj), animObj == &obj->animState->animObj, build);
}

bool __cdecl FakeRay(_VECTOR *start, _VECTOR *end, cel_tag *cel, obj_tag *obj, obj_tag *p5, HITDATA_tag **hits, char p7,
                     uint flags, ushort p9) {
    LogVector("ray", start);
    LogVector("to", end);
    ShadowLog("(%d,%d,%p,%d,%x,%u)", WhoCel(cel), WhoObj(obj), p5, p7, flags, p9);
    *hits = world.rayHitCount ? &world.rayHits[0] : NULL;
    return world.rayHitCount != 0;
}

void __cdecl FakeFreeHits(HITDATA_tag **hits) {
    ShadowLog("freehits(%d)", WhoHit(*hits));
    *hits = NULL;
}

bool __cdecl FakeBoundsPush(_VECTOR *pos, cel_tag *cel, float radius, _VECTOR *push, uint flags) {
    LogVector("boundspush", pos);
    ShadowLog("(%d,%08x,%x)", WhoCel(cel), Bits(radius), flags);
    *push = world.push;
    return true;
}

void __cdecl FakeHeadTracking(obj_tag *obj, void *target, float range, float rate, char randomDelay, char targetIsObj,
                              uchar bone) {
    ShadowLog("headtracking(%d,%d,%08x,%08x,%d,%d,%u)", WhoObj(obj), WhoObj((obj_tag *)target), Bits(range), Bits(rate),
              randomDelay, targetIsObj, bone);
}

void __cdecl FakeWaterRipple(HITDATA_tag *hit) {
    ShadowLog("ripple(%d)", WhoHit(hit));
}

} // namespace

// PlayerOrDrone_FeetOnPoint_DoCollision takes the object in EAX and the hit in ECX. Outside the namespace so the
// naked fake can call it by name.
void __cdecl DroneCollisionShadow_LogFeetCollision(obj_tag *obj, HITDATA_tag *hit) {
    ShadowLog("feetcollision(%d,%d)", WhoObj(obj), WhoHit(hit));
}

static void __declspec(naked) DroneCollisionShadow_FakeFeetCollision(void) {
    _asm {
        push ecx
        push eax
        call DroneCollisionShadow_LogFeetCollision
        add esp, 8
        ret
    }
}

namespace {

void InstallFakes() {
    ShadowFakes_Install();
    ShadowHook_Install(0x00013070, (const void *)&FakeBone);
    ShadowHook_Install(0x000dd290, (const void *)&FakePalette);
    ShadowHook_Install(0x0002c4e0, (const void *)&FakeRay);
    ShadowHook_Install(0x000294d0, (const void *)&FakeFreeHits);
    ShadowHook_Install(0x00048ee0, (const void *)&FakeBoundsPush);
    ShadowHook_Install(0x000119a0, (const void *)&FakeHeadTracking);
    ShadowHook_Install(0x00068f20, (const void *)&FakeWaterRipple);
    ShadowHook_Install(0x000ab390, (const void *)&DroneCollisionShadow_FakeFeetCollision);
    // our Drone_FeetOnPoint calls the port's core by name
    ShadowHook_Install((unsigned)(uintptr_t)&PlayerOrDrone_FeetOnPoint_DoCollision,
                       (const void *)&DroneCollisionShadow_LogFeetCollision);
}

// Every original of this file at once, so they reach each other rather than ours
struct Originals {
    XbeOriginalScope collData{0x000136b0}, isAiming{0x000122b0}, isHeadTracking{0x00011a80}, setAiming{0x00012220},
        nearPlayer{0x0003b7f0}, doCollision{0x0003cce0}, doGravity{0x0003cd50}, fall{0x0003ef30},
        collision{0x0003f350}, feet{0x000369d0}, handler{0x0003eff0}, aimTest{0x0003ab90}, doAiming{0x0003ab00},
        headTracking{0x0003a850}, nearest{0x0003a780}, behaviour{0x0003a940}, tracking{0x0003ac10};
};

// ---- the made-up world

// The values the compares single out, NaN and -0 among them
float Pick(const float *values, int n) {
    return values[ShadowRand(n)];
}

float SpecialOr(float lo, float hi) {
    switch (ShadowRand(20)) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    case 2:
        return NAN;
    default:
        return ShadowRandFloat(lo, hi);
    }
}

void RandomVector(_VECTOR *v, float range) {
    v->x = SpecialOr(-range, range);
    v->y = SpecialOr(-range, range);
    v->z = SpecialOr(-range, range);
}

obj_tag *RandomObj() {
    switch (ShadowRand(6)) {
    case 0:
        return NULL;
    case 1:
        return &world.player;
    case 2:
        return &world.other;
    default:
        return &world.objs[ShadowRand(kDrones)];
    }
}

void RandomHit(HITDATA_tag *hit, const obj_tag *obj, bool forRay) {
    memset(hit, 0, sizeof(*hit));
    // The feet are rootHeight below the position: put the hit around them, either side of 0.2 m
    static const float drops[] = {0.0f, 0.1f, 0.19f, 0.2f, 0.21f, 0.5f, -0.3f};
    float feet = obj->position.y - world.drones[0].rootHeight;
    hit->hitPosition.x = SpecialOr(-5.0f, 5.0f);
    hit->hitPosition.y = ShadowRand(10) == 0 ? NAN : feet - Pick(drops, 7);
    hit->hitPosition.z = SpecialOr(-5.0f, 5.0f);
    if (!forRay && ShadowRand(2)) {
        // Around the capsule's ends, for the contact flags
        static const float offs[] = {0.0f, -0.0f, 0.05f, -0.05f, -0.1f, -0.15f, -0.2f, -0.25f, 0.3f};
        const HITTEST_tag &capsule = world.drones[0].collision;
        hit->hitPosition.y = (ShadowRand(2) ? capsule.rayEnd.y : capsule.rayStart.y) + Pick(offs, 9);
    }
    hit->surfaceNormal.b = SpecialOr(-1.0f, 1.0f);
    hit->materialType = (char)(ShadowRand(8) == 0 ? 13 + ShadowRand(2) : ShadowRand(64));
    hit->flags = (char)ShadowRand(4);
    hit->hitBoneIdx = (short)(ShadowRand(8) - 1);
    hit->hitCel = ShadowRand(3) ? NULL : &world.cels[ShadowRand(2)];
    if (forRay) {
        hit->hitObj = RandomObj();
    } else {
        static obj_tag *const safe[] = {NULL, &world.other, &world.ladder, &world.objs[1]};
        hit->hitObj = safe[ShadowRand(4)];
    }
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    AnimState *anim = &world.anims[i];
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    memset(anim, 0, sizeof(*anim));

    o->objectType = OBJECTTYPE_DRONE;
    o->extraObjectData = d;
    o->animState = anim;
    o->inCel = ShadowRand(10) == 0 ? NULL : &world.cels[ShadowRand(2)];
    RandomVector(&o->position, 10.0f);
    for (int k = 0; k < 15; k++)
        o->transformMatrix.m[k] = ShadowRandFloat(-1.0f, 1.0f);
    o->drawnCountdown = (char)(ShadowRand(3) == 0 ? ShadowRand(3) : 0);
    o->transformFlags = (ushort)ShadowRand(0x100);
    o->effectFlags = ShadowRand(4) == 0 ? 0x40 : 0;
    d->gameObj = o;

    anim->animFlags = (char)ShadowRand(0x100);
    anim->animObj.flags = ShadowRand(0x400) | (ShadowRand(4) == 0 ? 0x3 : 0);
    anim->animObj.skin = &world.skin;
    anim->animObj.boneMtxs = world.bones;
    switch (ShadowRand(3)) {
    case 0:
        anim->animObj.aimTarget = NULL;
        break;
    case 1:
        anim->animObj.aimTarget = &d->targetPos;
        break;
    default:
        anim->animObj.aimTarget = RandomObj();
        break;
    }

    static const uint flagBits[] = {0x4, 0x20, 0x20, 0x40, 0x80, 0x80, 0x200, 0x4000, 0x4000000, 0x80000000};
    for (int k = 0; k < 10; k++)
        if (ShadowRand(2))
            d->flags |= flagBits[k];
    static const short skins[] = {0, 5, 0xc, 0x11, 0x13};
    d->skinClass = skins[ShadowRand(5)];
    d->unknown434 = ShadowRand(5) == 0 ? (void *)&world.script : NULL;
    d->unknown46c = ShadowRand(3) == 0 ? 0 : ShadowRand(8);
    static const float distances[] = {0.0f, -0.0f, 1.9f, 2.0f, 2.1f, 10.0f, NAN};
    for (int k = 0; k < 4; k++)
        d->playerDistance[k] = Pick(distances, 7);
    d->flies = (uchar)(ShadowRand(4) == 0);
    d->dtype = (uchar)(ShadowRand(3) == 0 ? DTYPE_Astronaut : ShadowRand(10));
    static const float roots[] = {0.3f, 0.39f, 0.4f, 0.41f, 0.55f, 1.0f, 1.2f, -0.0f, NAN};
    d->rootHeight = Pick(roots, 9);
    d->smallCollision = (uchar)(ShadowRand(4) == 0);
    RandomVector(&d->velocity, 3.0f);
    d->boundsPushFlags = ShadowRand(4);
    RandomVector(&d->collision.push, 0.5f);
    RandomVector(&d->collision.rayStart, 10.0f);
    RandomVector(&d->collision.rayEnd, 10.0f);
    d->collision.found = (ushort)ShadowRand(2);
    static const float alerts[] = {0.0f, 0.5f, 0.66f, 0.6600001f, 0.9f, NAN};
    d->alertness = Pick(alerts, 6);
    d->headMode = (short)(ShadowRand(2) ? 2 : ShadowRand(3));
    d->headTarget = RandomObj();
    d->headTrackUntil = ShadowRand(3) == 0 ? 0 : 990 + ShadowRand(20);
    d->sightFlags = ShadowRand(16);
    d->side = (uchar)ShadowRand(5);
    d->opponent = RandomObj();
    RandomVector(&d->targetPos, 10.0f);
    static const uchar aims[] = {0, 1, 2, 3, 3, 0xd};
    d->aimMode = aims[ShadowRand(6)];
    d->aimByEvent = (uchar)ShadowRand(2);
    d->aimEvent = (uchar)ShadowRand(3);
    d->animScript = ShadowRand(4) == 0 ? NULL : &world.script;
    d->aiming = (uchar)ShadowRand(2);
    for (int w = 0; w < 3; w++)
        d->behaviour1[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    d->currentBehaviour = d->behaviour1;
    d->botVars = &world.botVars;
    d->sm.id = i + 1;
}

// Either side of the 0.3 m NDrone2_Fall allows
const float kShearOffsets[] = {0.2f, 0.3f, 0.31f};

void RandomWorld() {
    for (int i = 0; i < kDrones; i++)
        RandomDrone(i);
    for (int i = 0; i + 1 < kDrones; i++)
        world.drones[i].next = &world.drones[i + 1];
    // Some drones share a cel with drone 0, near it
    for (int i = 1; i < kDrones; i++) {
        if (ShadowRand(2)) {
            world.objs[i].inCel = world.objs[0].inCel;
            world.objs[i].position.x = world.objs[0].position.x + ShadowRandFloat(-31.0f, 31.0f);
            world.objs[i].position.z = world.objs[0].position.z + ShadowRandFloat(-31.0f, 31.0f);
        }
    }

    memset(&world.player, 0, sizeof(world.player));
    world.player.objectType = OBJECTTYPE_PLAYER;
    RandomVector(&world.player.position, 10.0f);
    world.player.effectFlags = ShadowRand(4) == 0 ? 0x40 : 0;
    memset(&world.other, 0, sizeof(world.other));
    world.other.objectType = OBJECTTYPE_ROTOR;
    RandomVector(&world.other.position, 10.0f);
    world.other.effectFlags = ShadowRand(4) == 0 ? 0x40 : 0;
    memset(&world.ladder, 0, sizeof(world.ladder));
    world.ladder.objectType = OBJECTTYPE_LADDER;

    for (int c = 0; c < 2; c++) {
        memset(&world.cels[c], 0, sizeof(cel_tag));
        static const float shears[] = {-1000.0f, 0.0f, 5.0f, NAN};
        world.cels[c].shearHeight = Pick(shears, 4);
        if (ShadowRand(2))
            world.cels[c].shearHeight = world.objs[0].position.y + Pick(kShearOffsets, 3);
        world.cels[c].flags84 = (uchar)(ShadowRand(3) == 0 ? 0x40 : 0);
    }

    world.rayHitCount = ShadowRand(4);
    for (int k = 0; k < 3; k++) {
        RandomHit(&world.rayHits[k], &world.objs[0], true);
        world.rayHits[k].next = k + 1 < world.rayHitCount ? &world.rayHits[k + 1] : NULL;
    }
    int objHitCount = ShadowRand(4);
    for (int k = 0; k < 3; k++) {
        RandomHit(&world.objHits[k], &world.objs[0], false);
        world.objHits[k].next = k + 1 < objHitCount ? &world.objHits[k + 1] : NULL;
    }
    world.objs[0].hitList = objHitCount ? &world.objHits[0] : NULL;

    memset(&world.botVars, 0, sizeof(world.botVars));
    world.botVars.flags = ShadowRand(4);

    memset(&world.skin, 0, sizeof(world.skin));
    world.skin.hashcode = ShadowRand(2) ? HT_Skin_SkinAstronaut : HT_Skin_000077;
    world.skin.bones = world.skinBones;
    world.skin.skinScale.x = SpecialOr(0.5f, 1.5f);
    world.skin.skinScale.y = SpecialOr(0.5f, 1.5f);
    world.skin.skinScale.z = SpecialOr(0.5f, 1.5f);
    for (int b = 0; b < kBones; b++) {
        world.skinBones[b] = (uchar)(ShadowRand(kBones) | (ShadowRand(2) << 7));
        for (int k = 0; k < 15; k++)
            world.bones[b].m[k] = SpecialOr(-3.0f, 3.0f);
    }

    memset(&world.script, 0, sizeof(world.script));
    static const short frameCounts[] = {0, 1, 2, 10, 11, -4, 300};
    world.script.numFrames = frameCounts[ShadowRand(7)];
    static const float frames[] = {0.0f, 1.0f, 4.99f, 5.0f, 5.5f, 6.0f, -1.5f, 150.0f, NAN, 3.0e10f};
    world.script.curFrame = Pick(frames, 10);

    memset(&world.test, 0, sizeof(world.test));
    RandomVector(&world.test.rayEnd, 10.0f);
    world.test.found = (ushort)ShadowRand(2);
    RandomVector(&world.push, 0.5f);
    world.count = 0xcccc;
    world.slope = 7.0f;
    world.height = 7.0f;

    world.dcv.gameObj = &world.objs[0];
    world.dcv.drone = &world.drones[0];
    world.dcv.cel = world.objs[0].inCel;
    world.dcv.aiStateMachine = &world.drones[0].sm;

    for (int k = 0; k < 4; k++)
        glb_players[k] = ShadowRand(3) == 0 ? NULL : (k == 0 ? &world.player : &world.other);
    GameState.NumFramesUnpaused = 1000;
    GameState.CurrentLevelHashcode = ShadowRand(3) == 0 ? HT_Level_Tower2Elevator : HT_Level_HendersonA;
    MPSettings.dronesAreBots = ShadowRand(4) == 0;
    NPCGlobals.enabled = (uchar)(ShadowRand(8) != 0);
    NPCGlobals.NDrone2List = ShadowRand(8) == 0 ? NULL : &world.drones[0];
    NPCGlobals.dummyPlayerActive = (uchar)(ShadowRand(5) == 0);
    NPCGlobals.dummyPlayer = &world.other;
    Drone_bDisableSystem = (uchar)(ShadowRand(8) == 0);
    if (ShadowRand(2)) {
        FRAME_RATE_INT = 60;
        REC_FRAME_RATE = 1.0f / 60.0f;
        FRAME_RATE_DIV = 1.0f;
    } else {
        FRAME_RATE_INT = 50;
        REC_FRAME_RATE = 1.0f / 50.0f;
        FRAME_RATE_DIV = 1.2f;
    }
    GRAVITY_VECTOR.x = 0.0f;
    GRAVITY_VECTOR.y = ShadowRand(4) == 0 ? SpecialOr(-20.0f, 0.0f) : -9.8f;
    GRAVITY_VECTOR.z = 0.0f;
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

// ---- the cases

obj_tag *objArg;
const void *targetArg;
float rangeArg, rateArg;
float *slopeArg, *heightArg;
char charArg;
uchar ucharArg;

void TestAnim() {
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        switch (ShadowRand(6)) {
        case 0:
            objArg = NULL;
            break;
        case 1:
            objArg = &world.ladder;     // no animState
            break;
        default:
            objArg = &world.objs[0];
            break;
        }
        Compare("AnimObjectIsAiming", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x000122b0, uint32_t(__cdecl *)(obj_tag *))(objArg) & 0xff;
                },
                [] { return (uint32_t)AnimObjectIsAiming(objArg); });
        Compare("AnimObjectIsHeadTracking", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x00011a80, uint32_t(__cdecl *)(obj_tag *))(objArg) & 0xff;
                },
                [] { return (uint32_t)AnimObjectIsHeadTracking(objArg); });
        targetArg = ShadowRand(3) == 0 ? NULL : (const void *)&world.drones[0].targetPos;
        static const float ranges[] = {0.0f, -0.0f, 3.0f, 100.0f, -2.0f, NAN, INFINITY};
        rangeArg = Pick(ranges, 7);
        rateArg = SpecialOr(0.0f, 1.0f);
        charArg = (char)ShadowRand(3);
        ucharArg = (uchar)ShadowRand(256);
        Compare("AnimObjectSetAiming", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00012220, void(__cdecl *)(obj_tag *, const void *, float, float, char, uchar))(
                        objArg, targetArg, rangeArg, rateArg, charArg, ucharArg);
                    return 0u;
                },
                [] {
                    AnimObjectSetAiming(objArg, targetArg, rangeArg, rateArg, charArg, ucharArg);
                    return 0u;
                });
        Compare("AnimGetCollData", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x000136b0, uint32_t(__cdecl *)(obj_tag *, ushort *))(objArg, &world.count);
                },
                [] { return (uint32_t)(uintptr_t)AnimGetCollData(objArg, &world.count); });
    }
}

void TestCollision() {
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        Compare("DroneFunc_NearPlayerCollision", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x0003b7f0, uint32_t(__cdecl *)(Drone_tag *))(&world.drones[0]) & 0xff;
                },
                [] { return (uint32_t)DroneFunc_NearPlayerCollision(&world.drones[0]); });
        Compare("NDrone2_DoCollision", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x0003cce0, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)NDrone2_DoCollision(&world.dcv); });
        Compare("NDrone2_DoGravity", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x0003cd50, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)NDrone2_DoGravity(&world.dcv); });
        Compare("NDrone2_Fall", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x0003ef30, void(__cdecl *)(obj_tag *))(&world.objs[0]);
                    return 0u;
                },
                [] {
                    NDrone2_Fall(&world.objs[0]);
                    return 0u;
                });
        Compare("NDrone2_Collision", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x0003f350, void(__cdecl *)(obj_tag *))(&world.objs[0]);
                    return 0u;
                },
                [] {
                    NDrone2_Collision(&world.objs[0]);
                    return 0u;
                });
        objArg = ShadowRand(20) == 0 ? NULL : &world.objs[0];
        slopeArg = ShadowRand(3) ? &world.slope : NULL;
        heightArg = ShadowRand(3) ? &world.height : NULL;
        Compare("Drone_FeetOnPoint", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x000369d0, uint32_t(__cdecl *)(obj_tag *, HITTEST_tag *, float *, int, float *))(
                        objArg, &world.test, slopeArg, 0, heightArg);
                },
                [] { return (uint32_t)(uintptr_t)Drone_FeetOnPoint(objArg, &world.test, slopeArg, 0, heightArg); });
        Compare("Drone_CollisionHandler", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x0003eff0, void(__cdecl *)(obj_tag *))(&world.objs[0]);
                    return 0u;
                },
                [] {
                    Drone_CollisionHandler(&world.objs[0]);
                    return 0u;
                });
    }
}

void TestTracking() {
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        Compare("NDrone2_AimTest", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x0003ab90, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)NDrone2_AimTest(&world.dcv); });
        charArg = (char)ShadowRand(3);
        Compare("NDrone2_DoAiming", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x0003ab00, void(__cdecl *)(char, DCVars_tag *))(charArg, &world.dcv);
                    return 0u;
                },
                [] {
                    NDrone2_DoAiming(charArg, &world.dcv);
                    return 0u;
                });
        objArg = RandomObj();
        Compare("NDrone2_DoHeadTracking", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x0003a850, void(__cdecl *)(char, obj_tag *, DCVars_tag *))(charArg, objArg, &world.dcv);
                    return 0u;
                },
                [] {
                    NDrone2_DoHeadTracking(charArg, objArg, &world.dcv);
                    return 0u;
                });
        Compare("NDrone2_HeadTrackNearest", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x0003a780, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv);
                },
                [] { return (uint32_t)(uintptr_t)NDrone2_HeadTrackNearest(&world.dcv); });
        Compare("NDrone2_DoHeadTrackingBehaviour", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x0003a940, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    NDrone2_DoHeadTrackingBehaviour(&world.dcv);
                    return 0u;
                });
        Compare("NDrone2_DoTracking", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x0003ac10, void(__cdecl *)(char, DCVars_tag *))(charArg, &world.dcv);
                    return 0u;
                },
                [] {
                    NDrone2_DoTracking(charArg, &world.dcv);
                    return 0u;
                });
    }
}

} // namespace

void DroneCollisionShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], mpSettings[sizeof(MPSettings_t)], npc[sizeof(NPCGlobals_t)];
    static uint8_t hitList2[kHitList2Bytes];
    static obj_tag *players[4];
    static uint32_t rand[2];
    static _VECTOR gravity;
    uint32_t frameRateInt = FRAME_RATE_INT;
    float recFrameRate = REC_FRAME_RATE;
    float frameDiv = FRAME_RATE_DIV;
    uchar disable = Drone_bDisableSystem;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(mpSettings, &MPSettings, sizeof(mpSettings));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(hitList2, HitList2Block, sizeof(hitList2));
    memcpy(players, glb_players, sizeof(players));
    XbeOursRead(rand, RandWords.at, sizeof(rand));
    gravity = GRAVITY_VECTOR;

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&MPSettings, sizeof(MPSettings_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(HitList2Block, kHitList2Bytes);
    regions.Add(glb_players, sizeof(players));
    regions.Add(RandWords.game(), 8);
    regions.Add(&FRAME_RATE_INT, 4);
    regions.Add(&REC_FRAME_RATE, 4);
    regions.Add(&FRAME_RATE_DIV, 4);
    regions.Add(&GRAVITY_VECTOR, sizeof(_VECTOR));
    regions.Add(&Drone_bDisableSystem, 1);
    ShadowFakes_AddRegions(regions);

    InstallFakes();
    NfWarnMuted++;
    cases = differ = 0;

    TestAnim();
    int animCases = cases, animDiffer = differ;
    TestCollision();
    int collisionCases = cases, collisionDiffer = differ;
    TestTracking();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&MPSettings, mpSettings, sizeof(mpSettings));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(HitList2Block, hitList2, sizeof(hitList2));
    memcpy(glb_players, players, sizeof(players));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
    GRAVITY_VECTOR = gravity;
    FRAME_RATE_INT = frameRateInt;
    REC_FRAME_RATE = recFrameRate;
    FRAME_RATE_DIV = frameDiv;
    Drone_bDisableSystem = disable;

    printf("[dronecollision] joint boxes and aiming flags: %d cases, %d differ\n", animCases, animDiffer);
    printf("[dronecollision] capsule, floor and falling: %d cases, %d differ\n", collisionCases - animCases,
           collisionDiffer - animDiffer);
    printf("[dronecollision] head tracking and aiming: %d cases, %d differ\n", cases - collisionCases,
           differ - collisionDiffer);
}
