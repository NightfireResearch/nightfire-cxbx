// Shadow tests for what the drones see (game/drone/DroneVision.cpp, Drone_VisibilityForPosition in Drone_AIHints.cpp,
// Drone_PositionVisible in Drone.cpp): each original and ours run on the same made-up drones, with what needs a
// level going to logging fakes. Run at start with MenuShadowTests=on (MenuProbe.cpp), before any level exists.
//
// Faked, so not tested here: the rays and lines of sight themselves (AINetwork_TestRayCels, Collide_LineOfSight,
// Collide_RayIntersect), the skeleton (AnimGetBoneWorldTrans, AnimDatumGetWeaponInfo, DroneFunc_GetBonePos and both
// NDrone2_GetHeadPos), the AI volumes' box test, the camera's view test, the bot state functions, the reaction time,
// the state machine's messages, speech and the talk functions, the player stats and the music. MPDrone_MaybeIsDying-
// OrDead, which the standard fakes answer from tweakA, is pointed back at a copy of the original: the original sight
// loop has it inline where ours calls it.

#include "DroneVisionShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../engine/Collide.h"
#include "../engine/viewer.h"
#include "../game/obj/Player.h"
#include "../game/obj/Drone_AIHints.h"
#include "../game/drone/BOT.h"
#include "../game/drone/DroneVision.h"
#include "../game/drone/NDrone2.h"
#include "../game/mp/multiplayer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords (GameSpan<uint32_t>{0x0018cdf8})
#define switch_BLIND_DRONES U32_AT(XbeOurs(0x001df9a0))

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int kDrones = 6;
const int kVolumes = 3;

struct World {
    obj_tag objs[kDrones];
    Drone_tag drones[kDrones];
    AnimState anims[kDrones];
    BOT_vars_t botVars;
    obj_tag player;
    BLData playerData;
    AnimState playerAnim;
    obj_tag thing;                  // neither a drone nor the player
    obj_tag volumeObjs[kVolumes];
    DroneAIVolume_tag volumes[kVolumes];
    uint8_t cels[3][0x100];
    uint8_t viewer[0x400];
    uint8_t hitList[0x50];

    // Arguments and what the functions write through them
    int bone;
    uint flash;
    ushort sound;
    _VECTOR muzzle, barrel, pos;
    CelPos_tag from, to;
    char inView;

    // What the fakes answer
    uint losBits, rayCelBits, boxBits;
    int losCalls, rayCelCalls, boxCalls;
    int datumBone;
    uint datumFlash;
    ushort datumSound;
    int rayHits;
    short plrIndex;
    uint reaction;
    int missile;
    int talking;
    int inViewAnswer;
};
World world;

ShadowRegions regions;
int cases, differ, ulpAccepted;

// OpponentVisibility's FSIN is the CRT's sin in ours, and its x87 chain is SSE2 doubles: what the original returned
// and stored, for ours to be accepted when the double is within 2 ulps of it and the stored float within 1
double theirVisibility;
float theirStoredVisibility;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "dronevision", what, trial, theirs, ours))
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

float NaN() {
    uint32_t u = 0x7fc00000;
    float f;
    memcpy(&f, &u, 4);
    return f;
}

cel_tag *Cel(int i) {
    return (cel_tag *)world.cels[i];
}

viewer_tag *Viewer() {
    return (viewer_tag *)world.viewer;
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
    if (obj == &world.player)
        return 100;
    if (obj == &world.thing)
        return 101;
    for (int i = 0; i < kVolumes; i++)
        if (obj == &world.volumeObjs[i])
            return 110 + i;
    return obj == NULL ? -1 : 102;
}

int WhoCel(const cel_tag *cel) {
    for (int i = 0; i < 3; i++)
        if (cel == Cel(i))
            return i;
    return cel == NULL ? -1 : 9;
}

void LogVector(const char *name, const _VECTOR *v) {
    ShadowLog("%s=%08x,%08x,%08x", name, Bits(v->x), Bits(v->y), Bits(v->z));
}

// One value of ours, made the original's when it is within `ulps` of it
bool AcceptUlp(double *ours, double theirs, uint64_t ulps) {
    uint64_t a, b;
    memcpy(&a, ours, 8);
    memcpy(&b, &theirs, 8);
    if (a == b || (a - b > ulps && b - a > ulps))
        return false;
    *ours = theirs;
    return true;
}

bool AcceptUlp(float *ours, float theirs) {
    uint32_t a = Bits(*ours), b = Bits(theirs);
    if (a == b || (a - b != 1 && b - a != 1))
        return false;
    *ours = theirs;
    return true;
}

// ---- the fakes

bool __cdecl FakeRayCels(_VECTOR *from, cel_tag *fromCel, _VECTOR *to, cel_tag *toCel) {
    LogVector("raycels", from);
    LogVector("to", to);
    ShadowLog("(%d,%d)", WhoCel(fromCel), WhoCel(toCel));
    return (world.rayCelBits >> (world.rayCelCalls++ & 31)) & 1;
}

bool __cdecl FakeLineOfSight(_VECTOR *from, _VECTOR *to, cel_tag *cel, obj_tag *a, obj_tag *b, uint flags) {
    LogVector("los", from);
    LogVector("to", to);
    ShadowLog("(%d,%d,%d,%x)", WhoCel(cel), WhoObj(a), WhoObj(b), flags);
    return (world.losBits >> (world.losCalls++ & 31)) & 1;
}

bool __cdecl FakeBonePos(obj_tag *obj, uint bone, _VECTOR *pos) {
    ShadowLog("bonepos(%d,%x)", WhoObj(obj), bone);
    pos->x = obj->position.x + (float)(bone & 0xff) * 0.03125f;
    pos->y = obj->position.y + 1.25f;
    pos->z = obj->position.z - 0.375f;
    return true;
}

bool __cdecl FakeHeadPos(obj_tag *obj, _VECTOR *pos, float *yaw) {
    ShadowLog("headpos(%d,%d,%d)", WhoObj(obj), pos != NULL, yaw != NULL);
    if (pos != NULL) {
        pos->x = obj->position.x + 0.25f;
        pos->y = obj->position.y + 1.625f;
        pos->z = obj->position.z + 0.5f;
    }
    if (yaw != NULL)
        *yaw = obj->rotation.y + 0.125f;
    return obj->objectType == OBJECTTYPE_DRONE;
}

bool __cdecl FakeHeadPosOld(obj_tag *obj, _VECTOR *pos, float *yaw) {
    ShadowLog("headposold(%d,%d,%d)", WhoObj(obj), pos != NULL, yaw != NULL);
    if (pos != NULL) {
        pos->x = obj->position.x - 0.125f;
        pos->y = obj->position.y + 1.5f;
        pos->z = obj->position.z + 0.75f;
    }
    if (yaw != NULL)
        *yaw = obj->rotation.y - 0.25f;
    return true;
}

bool __cdecl FakeRay(_VECTOR *start, _VECTOR *end, cel_tag *cel, obj_tag *obj, obj_tag *p5, HITDATA_tag **hits, char p7,
                     uint flags, ushort p9) {
    LogVector("ray", start);
    LogVector("to", end);
    ShadowLog("(%d,%d,%p,%d,%x,%u)", WhoCel(cel), WhoObj(obj), p5, p7, flags, p9);
    *hits = world.rayHits ? (HITDATA_tag *)world.hitList : NULL;
    return world.rayHits != 0;
}

void __cdecl FakeFreeHits(HITDATA_tag **hits) {
    ShadowLog("freehits(%d)", *hits == (HITDATA_tag *)world.hitList);
}

// Writes the sound first: GunThroughWall's original hands its own argument slot for it
int __cdecl FakeDatum(obj_tag *obj, HASHCODE gfx, uint *flash, ushort *sound) {
    ShadowLog("datum(%d,%x,%d,%d)", WhoObj(obj), gfx, flash != NULL, sound != NULL);
    if (sound != NULL)
        *sound = world.datumSound;
    if (flash != NULL)
        *flash = world.datumFlash;
    return world.datumBone;
}

void __cdecl FakeBone(obj_tag *obj, uint which, int datum, _VECTOR *pos, _MATRIX *mtx) {
    ShadowLog("bone(%d,%x,%d)", WhoObj(obj), which, datum);
    float base = (float)(which & 0xff) * 0.125f;
    pos->x = base + 0.5f;
    pos->y = 1.25f;
    pos->z = obj->position.z - 2.0f;
    for (int k = 0; k < 15; k++)
        mtx->m[k] = base + (float)k * 0.25f - 1.5f;
}

short __cdecl FakePlr2Ind(obj_tag *obj) {
    ShadowLog("plr2ind(%d)", WhoObj(obj));
    return world.plrIndex;
}

void __cdecl FakeLogDetected(uint player) {
    ShadowLog("logdetected(%u)", player);
}

uint __cdecl FakeReaction(Drone_tag *drone) {
    ShadowLog("reaction(%d)", Who(drone));
    return world.reaction;
}

ushort __cdecl FakeWeaponChoice(Drone_tag *drone, obj_tag *opponent, char p3, char p4, char p5) {
    ShadowLog("weaponchoice(%d,%d,%d,%d,%d)", Who(drone), WhoObj(opponent), p3, p4, p5);
    return 0;
}

bool __cdecl FakeIsMissile(Drone_tag *drone, obj_tag *obj) {
    ShadowLog("ismissile(%d,%p)", Who(drone), obj);
    return world.missile != 0;
}

void __cdecl FakeCloseRange(Drone_tag *drone) {
    ShadowLog("closerange(%d)", Who(drone));
}

void __cdecl FakeDefaultRange(Drone_tag *drone) {
    ShadowLog("defaultrange(%d)", Who(drone));
}

void __cdecl FakeSendMsgSelf(uint msg, void *extra, uint delay, uint scope, DCVars_tag *dcv) {
    ShadowLog("msgself(%u,%p,%u,%u,%d,%d)", msg, extra, delay, scope, Who(dcv->drone), WhoObj(dcv->gameObj));
}

void __cdecl FakeMissionFail(Drone_tag *drone, uint reason) {
    ShadowLog("missionfail(%d,%u)", Who(drone), reason);
}

bool __cdecl FakeIsTalking(Drone_tag *drone) {
    ShadowLog("istalking(%d)", Who(drone));
    return world.talking != 0;
}

void __cdecl FakeSpeechSFXd(Drone_tag *drone, uint sfx, char a, char b, char c) {
    ShadowLog("speechd(%d,%x,%d,%d,%d)", Who(drone), sfx, a, b, c);
}

bool __cdecl FakeDroneMessage(obj_tag *obj, uint msg, void *extra, uint delay) {
    ShadowLog("message(%d,%u,%p,%u)", WhoObj(obj), msg, extra, delay);
    return true;
}

bool __cdecl FakeNewSightTalk(DCVars_tag *dcv) {
    ShadowLog("newsight(%d,%d)", Who(dcv->drone), WhoObj(dcv->gameObj));
    return true;
}

bool __cdecl FakeLostSightTalk(DCVars_tag *dcv) {
    ShadowLog("lostsight(%d,%d)", Who(dcv->drone), WhoObj(dcv->gameObj));
    return true;
}

bool __cdecl FakeInBox(_MATRIX *mtx, _VECTOR *half, _VECTOR *point, float slack) {
    ShadowLog("inbox(%08x,%08x)", Bits(half->x), Bits(slack));
    LogVector("point", point);
    return (world.boxBits >> (world.boxCalls++ & 31)) & 1;
}

bool __cdecl FakeInView(_VECTOR *point, float radius, viewer_tag *viewer) {
    LogVector("inview", point);
    ShadowLog("(%08x,%d)", Bits(radius), viewer == Viewer());
    return world.inViewAnswer != 0;
}

// MPDrone_MaybeIsDyingOrDead as the original has it (DroneVision_ProcessDroneSight's original inlines it)
bool __cdecl DroneDying(obj_tag *obj) {
    if (obj == NULL)
        return true;
    Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
    if ((drone->flags & 0x600) || !(drone->flags & 0x100))
        return true;
    if (drone->health <= 0.0f)
        return true;
    return obj->objectType == 0x11 || (obj->flags & 1);
}

void InstallFakes() {
    ShadowFakes_Install();
    ShadowHook_Install(0x0001a190, (const void *)&DroneDying);
    ShadowHook_Install(0x00048ac0, (const void *)&FakeRayCels);
    ShadowHook_Install(0x0002c5b0, (const void *)&FakeLineOfSight);
    ShadowHook_Install(0x00036e00, (const void *)&FakeBonePos);
    ShadowHook_Install(0x00036ba0, (const void *)&FakeHeadPos);
    ShadowHook_Install(0x00036c80, (const void *)&FakeHeadPosOld);
    ShadowHook_Install(0x0002c4e0, (const void *)&FakeRay);
    ShadowHook_Install(0x000294d0, (const void *)&FakeFreeHits);
    ShadowHook_Install(0x00013630, (const void *)&FakeDatum);
    ShadowHook_Install(0x00013070, (const void *)&FakeBone);
    ShadowHook_Install(0x0002d8a0, (const void *)&FakePlr2Ind);
    ShadowHook_Install(0x000b07e0, (const void *)&FakeLogDetected);
    ShadowHook_Install(0x0003a5d0, (const void *)&FakeReaction);
    ShadowHook_Install(0x0001d750, (const void *)&FakeWeaponChoice);
    ShadowHook_Install(0x0001c600, (const void *)&FakeIsMissile);
    ShadowHook_Install(0x0001c690, (const void *)&FakeCloseRange);
    ShadowHook_Install(0x0001c6c0, (const void *)&FakeDefaultRange);
    ShadowHook_Install(0x0004e890, (const void *)&FakeSendMsgSelf);
    ShadowHook_Install(0x00035fb0, (const void *)&FakeMissionFail);
    ShadowHook_Install(0x00036200, (const void *)&FakeIsTalking);
    ShadowHook_Install(0x0003cca0, (const void *)&FakeSpeechSFXd);
    ShadowHook_Install(0x00031830, (const void *)&FakeDroneMessage);
    ShadowHook_Install(0x0003c1e0, (const void *)&FakeNewSightTalk);
    ShadowHook_Install(0x0003c2b0, (const void *)&FakeLostSightTalk);
    ShadowHook_Install(0x00028bd0, (const void *)&FakeInBox);
    ShadowHook_Install(0x000dbc80, (const void *)&FakeInView);
}

// Every original of the group at once, so they reach each other rather than ours
struct OriginalVision {
    XbeOriginalScope weaponInfo{0x00066850}, clamp{0x000362d0}, losToObject{0x00064600}, canSeeFrom{0x00064750},
        canSeeObject{0x000648b0}, positionFrom{0x00064660}, canSeePosition{0x00064700}, gunWall{0x00065eb0},
        objectVisibility{0x000643c0}, opponentVisibility{0x00065510}, inView{0x000656a0}, opponentLOS{0x00066030},
        seekLOS{0x00066110}, logSeen{0x000654b0}, haveSight{0x00065730}, findDrone{0x00064580},
        droneAfter{0x000645c0}, droneSeesDrone{0x000658e0}, considerAlerted{0x00065950}, determine{0x00065cd0},
        findAlerted{0x00065d60}, processSight{0x000661f0}, volumeVisibility{0x00030470},
        positionVisible{0x000318c0},
        // Not ours: the original LogSeenOpponent keeps the drone in ECX across its call, which the original
        // NDrone2_ObjectIsArmed leaves alone and our port does not
        objectIsArmed{0x00064be0};
};

// The register-argument originals
__declspec(naked) double __cdecl OriginalOpponentVisibility(Drone_tag *drone) {
    _asm {
        push esi
        mov esi, [esp + 8]
        mov eax, 0x00065510
        call eax
        pop esi
        ret
    }
}

__declspec(naked) uint32_t __cdecl OriginalOpponentInView(Drone_tag *drone, float range) {
    _asm {
        mov eax, [esp + 4]
        push dword ptr [esp + 8]
        mov ecx, 0x000656a0
        call ecx
        add esp, 4
        ret
    }
}

__declspec(naked) uint32_t __cdecl OriginalSeekOpponentLOS(Drone_tag *drone) {
    _asm {
        push esi
        mov esi, [esp + 8]
        mov eax, 0x00066110
        call eax
        pop esi
        ret
    }
}

__declspec(naked) void __cdecl OriginalHaveOpponentSight(Drone_tag *drone) {
    _asm {
        push esi
        mov esi, [esp + 8]
        mov eax, 0x00065730
        call eax
        pop esi
        ret
    }
}

__declspec(naked) uint32_t __cdecl OriginalFindDrone(Drone_tag *from, uint id) {
    _asm {
        mov eax, [esp + 4]
        mov edx, [esp + 8]
        mov ecx, 0x00064580
        call ecx
        ret
    }
}

__declspec(naked) uint32_t __cdecl OriginalConsiderAlerted(Drone_tag *drone, Drone_tag *alerter) {
    _asm {
        push esi
        push edi
        mov esi, [esp + 12]
        mov edi, [esp + 16]
        mov eax, 0x00065950
        call eax
        pop edi
        pop esi
        ret
    }
}

// a in EDX, b in EAX
__declspec(naked) uint32_t __cdecl OriginalDetermineAlertCheck(Drone_tag *a, Drone_tag *b) {
    _asm {
        mov edx, [esp + 4]
        mov eax, [esp + 8]
        mov ecx, 0x00065cd0
        call ecx
        ret
    }
}

// ---- the made-up world

float RandomFloat(float lo, float hi) {
    switch (ShadowRand(20)) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    case 2:
        return NaN();
    default:
        return ShadowRandFloat(lo, hi);
    }
}

void RandomVector(_VECTOR *v, float range) {
    v->x = ShadowRandFloat(-range, range);
    v->y = ShadowRandFloat(-range / 4, range / 4);
    v->z = ShadowRandFloat(-range, range);
}

// A value or one of the edges the functions single out
float Pick(const float *edges, int n, float lo, float hi) {
    if (ShadowRand(2) == 0)
        return edges[ShadowRand(n)];
    return RandomFloat(lo, hi);
}

uint RandomWord() {
    return ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

const uint kFlags2Bits[] = {1, 2, 4, 8, 0x10, 0x20, 0x10000, 0x200000};

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    AnimState *anim = &world.anims[i];
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    memset(anim, 0, sizeof(*anim));

    static const char weapons[] = {0, 1, 2, 6, 0x46, 0x47, 0x6a, 0x71, 0x72, -1};
    anim->currentWeaponId = weapons[ShadowRand(sizeof(weapons))];
    anim->animObj.flags = ShadowRand(5) ? 1 : 0;
    o->animState = ShadowRand(12) ? anim : NULL;
    o->extraObjectData = d;
    o->objectType = ShadowRand(15) ? OBJECTTYPE_DRONE : 0x11;
    o->inCel = ShadowRand(10) ? Cel(ShadowRand(3)) : NULL;
    RandomVector(&o->position, 30.0f);
    o->rotation.y = RandomFloat(-4.0f, 4.0f);
    for (int k = 0; k < 15; k++)
        o->transformMatrix.m[k] = ShadowRandFloat(-1, 1);
    o->flags = (char)(ShadowRand(10) == 0);
    d->gameObj = o;

    static const float alertness[] = {0.0f, 0.5f, 0.999f, 1.0f, 2.0f};
    d->alertness = Pick(alertness, 5, 0.0f, 1.5f);
    static const float angles[] = {0.0f, -0.0f, 0.5f, 1.0f, 3.1415927f};
    d->viewAngle = Pick(angles, 5, 0.1f, 3.0f);
    static const float ranges[] = {0.0f, -1.0f, 5.0f, 20.0f, 40.0f};
    d->viewRange = Pick(ranges, 5, 1.0f, 50.0f);
    static const float distances[] = {0.0f, 1.99f, 2.0f, 2.01f, 10.0f, 49.9f, 50.0f, 50.1f};
    d->distanceToTarget = Pick(distances, 8, 0.0f, 60.0f);
    d->headYaw = RandomFloat(-4.0f, 4.0f);
    d->opponentAngles.y = RandomFloat(-4.0f, 4.0f);
    static const float healths[] = {-1.0f, 0.0f, -0.0f, 50.0f};
    d->health = ShadowRand(15) == 0 ? NaN() : healths[ShadowRand(4)];
    static const float visibilities[] = {0.0f, 0.69f, 0.7f, 0.71f, 1.0f};
    d->opponentVisibility = Pick(visibilities, 5, 0.0f, 1.0f);
    static const float scales[] = {1.0f, 1.0f, 0.5f, 0.0f};
    d->opponentVisibilityScale = Pick(scales, 4, 0.0f, 2.0f);

    uint flags = 0;
    if (ShadowRand(5))
        flags |= 0x100;
    if (ShadowRand(4))
        flags |= 0x2000;
    if (ShadowRand(4))
        flags |= 0x10000;
    if (ShadowRand(5) == 0)
        flags |= 0x20000;
    if (ShadowRand(8) == 0)
        flags |= ShadowRand(2) ? 0x200 : 0x400;
    d->flags = flags;
    uint flags2 = 0;
    if (ShadowRand(3))
        for (int k = ShadowRand(3); k >= 0; k--)
            flags2 |= kFlags2Bits[ShadowRand(8)];
    d->flags2 = flags2;
    static const uchar sides[] = {1, 1, 2, 3};
    d->side = sides[ShadowRand(4)];
    static const uchar dtypes[] = {DTYPE_Normal, DTYPE_HostageKiller, DTYPE_Hostage, DTYPE_Bot};
    d->dtype = dtypes[ShadowRand(4)];
    static const uint sight[] = {2, 4, 8, 0x40};
    for (int k = 0; k < 4; k++)
        if (ShadowRand(2))
            d->sightFlags |= sight[k];
    static const uint frames[] = {0, 1, 2, 49, 50, 51, 59, 60, 61, 99, 100, 101, 119, 120, 121};
    d->sightFrames = frames[ShadowRand(15)];
    d->unseenFrames = frames[ShadowRand(15)];
    d->viewBone = (ushort)ShadowRand(8);
    d->aiming = (uchar)ShadowRand(2);
    d->unknown1d = (uchar)ShadowRand(2);
    d->gunThroughWall = (uchar)ShadowRand(2);
    d->seenPlayerLogged = (uchar)(ShadowRand(4) == 0);
    d->unknown41 = (uchar)ShadowRand(2);
    for (int w = 0; w < 3; w++)
        d->behaviour1[w] = RandomWord();
    d->currentBehaviour = d->behaviour1;
    static const uint states[] = {DSTATE_PlayScript, DSTATE_EnemyMission, DSTATE_InterogateAssist,
                                  DSTATE_InterogateAssistWait, DSTATE_GoToGoalPosition,
                                  DSTATE_HostageGoToGoalPosition, DSTATE_EnemyRunToPoint, DSTATE_Idle,
                                  DSTATE_Attack, DSTATE_Patrol};
    d->sm.curState = ShadowRand(2) ? states[ShadowRand(7)] : states[7 + ShadowRand(3)];
    d->sm.id = (uint)i + 1;
    static const short skins[] = {5, 6, 4, 12};
    d->skinClass = skins[ShadowRand(4)];
    d->secondEyeFlags = ShadowRand(4) == 0 ? 0x30000000 : (ShadowRand(3) == 0 ? 0x10000000 : 0);
    RandomVector(&d->secondEyePos, 30.0f);
    d->secondEyeCel = Cel(ShadowRand(3));
    d->botVars = &world.botVars;
}

void RandomWorld() {
    for (int i = 0; i < kDrones; i++)
        RandomDrone(i);

    memset(&world.botVars, 0, sizeof(world.botVars));
    static const uchar botWeapons[] = {0x3b, 0x3b, 6};
    world.botVars.weapon = botWeapons[ShadowRand(3)];
    for (int p = 0; p < 10; p++)
        world.botVars.players[p].flags = (uchar)ShadowRand(16);

    memset(&world.player, 0, sizeof(world.player));
    memset(&world.playerData, 0, sizeof(world.playerData));
    memset(&world.playerAnim, 0, sizeof(world.playerAnim));
    world.player.objectType = OBJECTTYPE_PLAYER;
    world.player.extraObjectData = &world.playerData;
    world.player.animState = ShadowRand(5) ? &world.playerAnim : NULL;
    world.playerAnim.currentWeaponId = ShadowRand(3) ? 6 : 0;
    world.player.inCel = Cel(ShadowRand(3));
    RandomVector(&world.player.position, 30.0f);
    RandomVector(&world.player.rotation, 4.0f);
    world.playerData.playerNum = (char)ShadowRand(4);

    memset(&world.thing, 0, sizeof(world.thing));
    world.thing.objectType = 7;
    world.thing.inCel = Cel(ShadowRand(3));
    RandomVector(&world.thing.position, 30.0f);

    // The opponents: the player, another drone, something else or none
    for (int i = 0; i < kDrones; i++) {
        switch (ShadowRand(6)) {
        case 0:
            world.drones[i].opponent = NULL;
            break;
        case 1:
            world.drones[i].opponent = &world.objs[(i + 1) % kDrones];
            break;
        case 2:
            world.drones[i].opponent = &world.thing;
            break;
        default:
            world.drones[i].opponent = &world.player;
            break;
        }
    }

    // The AI volumes
    world.boxCalls = 0;
    world.boxBits = RandomWord();
    for (int v = 0; v < kVolumes; v++) {
        DroneAIVolume_tag *volume = &world.volumes[v];
        memset(volume, 0, sizeof(*volume));
        memset(&world.volumeObjs[v], 0, sizeof(world.volumeObjs[v]));
        volume->obj = &world.volumeObjs[v];
        volume->halfSize.x = (float)v + 1.0f;
        volume->visibility = ShadowRand(6) == 0 ? NaN() : ShadowRandFloat(0.0f, 1.5f);
        volume->type = (ushort)(ShadowRand(3) == 0);
        volume->node.next = v + 1 < kVolumes ? &world.volumes[v + 1].node : NULL;
    }
    AIBoxList.head = ShadowRand(6) ? &world.volumes[0].node : NULL;

    // The camera
    memset(world.viewer, 0, sizeof(world.viewer));
    RandomVector(&Viewer()->pos, 30.0f);
    Viewer()->someCel = Cel(ShadowRand(3));
    for (int v = 0; v < 11; v++)
        glb_viewer[v] = Viewer();
    glb_players[0] = &world.player;

    memset(world.cels, 0, sizeof(world.cels));
    for (int c = 0; c < 3; c++)
        Cel(c)->addedToDraw = ShadowRand(3) != 0;

    // The arguments and the fakes' answers
    world.bone = (int)ShadowRand(30);
    world.flash = RandomWord();
    world.sound = (ushort)ShadowRand(3);
    RandomVector(&world.muzzle, 30.0f);
    RandomVector(&world.barrel, 1.0f);
    RandomVector(&world.pos, 30.0f);
    world.from.pos = world.pos;
    world.from.cel = Cel(ShadowRand(3));
    RandomVector(&world.to.pos, 30.0f);
    world.to.cel = Cel(ShadowRand(3));
    world.inView = (char)ShadowRand(2);
    world.losBits = RandomWord();
    world.rayCelBits = ShadowRand(4) == 0 ? 0 : RandomWord();
    world.losCalls = world.rayCelCalls = 0;
    world.datumBone = ShadowRand(5) == 0 ? -1 : (int)ShadowRand(30);
    world.datumFlash = RandomWord();
    world.datumSound = (ushort)(ShadowRand(3) == 0 ? 0 : ShadowRand(0x600));
    world.rayHits = ShadowRand(2);
    static const short indexes[] = {-1, 0, 1, 3, 9};
    world.plrIndex = indexes[ShadowRand(5)];
    static const uint reactions[] = {0, 1, 2, 50, 60, 61, 1000};
    world.reaction = reactions[ShadowRand(7)];
    world.missile = ShadowRand(2);
    world.talking = ShadowRand(2);
    world.inViewAnswer = ShadowRand(4) != 0;

    // The drone list: some of the drones, in order, with a count that is now and then not the list's length
    int listed = 1 + ShadowRand(kDrones);
    for (int i = 0; i < kDrones; i++) {
        world.drones[i].prev = i > 0 ? &world.drones[i - 1] : NULL;
        world.drones[i].next = i + 1 < listed ? &world.drones[i + 1] : NULL;
    }
    // Ids now and then out of order
    if (ShadowRand(3) == 0) {
        uint a = ShadowRand(kDrones), b = ShadowRand(kDrones);
        uint t = world.drones[a].sm.id;
        world.drones[a].sm.id = world.drones[b].sm.id;
        world.drones[b].sm.id = t;
    }
    memset(&NPCGlobals, 0, sizeof(NPCGlobals));
    NPCGlobals.NDrone2List = ShadowRand(20) ? &world.drones[0] : NULL;
    NPCGlobals.NDrone2Count = (ushort)(ShadowRand(5) == 0 ? ShadowRand(kDrones + 2) : listed);
    static const uint cursorIds[] = {0, 0, 1, 2, 3, 4, 5, 6, 99};
    NPCGlobals.alertCursor[0] = cursorIds[ShadowRand(9)];
    NPCGlobals.alertCursor[1] = cursorIds[ShadowRand(9)];
    NPCGlobals.sightCursor = cursorIds[ShadowRand(9)];
    NPCGlobals.visionTests = (ushort)(ShadowRand(3) == 0 ? 0xffff - ShadowRand(3) : ShadowRand(1000));
    NPCGlobals.unknown1390 = (uchar)(ShadowRand(10) == 0);
    NPCGlobals.unknown1392 = (uchar)ShadowRand(2);
    NPCGlobals.unknown174 = ShadowRand(4) == 0;
    NPCGlobals.newSightSaid = (uchar)ShadowRand(2);
    NPCGlobals.lostSightSaid = (uchar)ShadowRand(2);

    // The frames: now and then past 2^31, where the originals convert them unsigned
    uint now = ShadowRand(4) == 0 ? 0x80000000u + ShadowRand(1000) : 100 + ShadowRand(100000);
    GameState.NumFramesUnpaused = now;
    NPCGlobals.postLoadFrame = ShadowRand(8) == 0 ? now - ShadowRand(20) : now - 15 - ShadowRand(1000);
    NPCGlobals.enabledFrame = ShadowRand(8) == 0 ? now - ShadowRand(20) : now - 15 - ShadowRand(1000);
    for (int i = 0; i < kDrones; i++) {
        uint frame;
        switch (ShadowRand(4)) {
        case 0:
            frame = now - 60;
            break;
        case 1:
            frame = now - 72;
            break;
        case 2:
            frame = 0x80000000u + ShadowRand(1000);
            break;
        default:
            frame = now - ShadowRand(200);
            break;
        }
        world.drones[i].alertedFrame = frame;
    }
    static const HASHCODE levels[] = {HT_Level_HendersonA, HT_Level_HendersonD, HT_Level_CastleIndoors1,
                                      HT_Level_TowerA, HT_Level_TowerB, HT_Level_PowerStationA1,
                                      HT_Level_PowerStationA2, HT_Level_EvilBase, HT_Level_SpaceStationD};
    GameState.CurrentLevelHashcode = levels[ShadowRand(sizeof(levels) / sizeof(levels[0]))];
    if (ShadowRand(2)) {
        FRAME_RATE_INT = 60;
        _FRAME_RATE = 60.0f;
        FRAME_RATE_DIV = 1.0f;
    } else {
        FRAME_RATE_INT = 50;
        _FRAME_RATE = 50.0f;
        FRAME_RATE_DIV = 1.2f;
    }
    MPSettings.maybeDroneAIEnabled = ShadowRand(4) == 0;
    static const MultiplayerGameMode modes[] = {GM_PROTECTION, GM_DEMOLITION, (MultiplayerGameMode)0};
    MPSettings.GameMode = modes[ShadowRand(3)];
    switch_BLIND_DRONES = ShadowRand(10) == 0;
    RandWords[0] = RandomWord();
    RandWords[1] = RandomWord();
}

// ---- the cases

Drone_tag *droneArg, *otherArg;
obj_tag *objArg;
uint uintArg, maskArg;
float floatArg;
cel_tag *celArg;
_VECTOR *vectorArg;
char *charPtrArg;
ushort *soundArg;

void TestLinesOfSight() {
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        droneArg = &world.drones[ShadowRand(kDrones)];
        soundArg = ShadowRand(5) ? &world.sound : NULL;
        Compare("DroneWeap_GetWeaponInfo", trial,
                [] {
                    OriginalVision o;
                    return ORIGINAL(0x00066850, uint32_t(__cdecl *)(Drone_tag *, int *, uint *, ushort *, _VECTOR *,
                                                                    _VECTOR *))(droneArg, &world.bone, &world.flash,
                                                                                soundArg, &world.muzzle,
                                                                                &world.barrel) & 0xff;
                },
                [] {
                    return (uint32_t)DroneWeap_GetWeaponInfo(droneArg, &world.bone, &world.flash, soundArg,
                                                             &world.muzzle, &world.barrel);
                });
        Compare("DroneFunc_ClampPosToCollisionCylinder", trial,
                [] {
                    OriginalVision o;
                    ORIGINAL(0x000362d0, void(__cdecl *)(obj_tag *, _VECTOR *))(droneArg->gameObj, &world.pos);
                    return 0u;
                },
                [] {
                    DroneFunc_ClampPosToCollisionCylinder(droneArg->gameObj, &world.pos);
                    return 0u;
                });
        Compare("DroneVision_GunThroughWall", trial,
                [] {
                    OriginalVision o;
                    return ORIGINAL(0x00065eb0, uint32_t(__cdecl *)(Drone_tag *))(droneArg) & 0xff;
                },
                [] { return (uint32_t)DroneVision_GunThroughWall(droneArg); });
    }
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        droneArg = &world.drones[ShadowRand(kDrones)];
        switch (ShadowRand(4)) {
        case 0:
            objArg = &world.player;
            break;
        case 1:
            objArg = &world.thing;
            break;
        default:
            objArg = &world.objs[ShadowRand(kDrones)];
            break;
        }
        uintArg = ShadowRand(3) == 0 ? 0x80000000u | ShadowRand(40) : ShadowRand(40);
        maskArg = ShadowRand(3) == 0 ? 0x500 : (ShadowRand(5) == 0 ? 0xffff0000u : 0);
        celArg = ShadowRand(6) ? Cel(ShadowRand(3)) : NULL;
        Compare("DroneVision_LineOfSightToObject", trial,
                [] {
                    OriginalVision o;
                    return ORIGINAL(0x00064600, uint32_t(__cdecl *)(CelPos_tag *, CelPos_tag *, obj_tag *, obj_tag *,
                                                                    char, uint))(&world.from, &world.to, objArg,
                                                                                 droneArg->gameObj, world.inView,
                                                                                 maskArg) & 0xff;
                },
                [] {
                    return (uint32_t)DroneVision_LineOfSightToObject(&world.from, &world.to, objArg, droneArg->gameObj,
                                                                     world.inView, maskArg);
                });
        Compare("DroneVision_CanSeeObjectFrom", trial,
                [] {
                    OriginalVision o;
                    return ORIGINAL(0x00064750, uint32_t(__cdecl *)(Drone_tag *, obj_tag *, uint, _VECTOR *, cel_tag *,
                                                                    uint))(droneArg, objArg, uintArg, &world.pos,
                                                                           celArg, maskArg) & 0xff;
                },
                [] {
                    return (uint32_t)DroneVision_CanSeeObjectFrom(droneArg, objArg, uintArg, &world.pos, celArg,
                                                                  maskArg);
                });
        Compare("NDrone2_CanSeeObject", trial,
                [] {
                    OriginalVision o;
                    return ORIGINAL(0x000648b0, uint32_t(__cdecl *)(Drone_tag *, obj_tag *, uint, uint))(
                               droneArg, objArg, uintArg, maskArg) & 0xff;
                },
                [] { return (uint32_t)NDrone2_CanSeeObject(droneArg, objArg, uintArg, maskArg); });
        Compare("DroneVision_CanSeePositionFrom", trial,
                [] {
                    OriginalVision o;
                    return ORIGINAL(0x00064660, uint32_t(__cdecl *)(obj_tag *, _VECTOR *, cel_tag *, obj_tag *,
                                                                    _VECTOR *, cel_tag *))(
                               droneArg->gameObj, &world.pos, celArg, objArg, &world.muzzle, world.to.cel) & 0xff;
                },
                [] {
                    return (uint32_t)DroneVision_CanSeePositionFrom(droneArg->gameObj, &world.pos, celArg, objArg,
                                                                    &world.muzzle, world.to.cel);
                });
        Compare("DroneVision_CanSeePosition", trial,
                [] {
                    OriginalVision o;
                    return ORIGINAL(0x00064700, uint32_t(__cdecl *)(obj_tag *, _VECTOR *, cel_tag *, obj_tag *))(
                               droneArg->gameObj, &world.pos, celArg, objArg) & 0xff;
                },
                [] { return (uint32_t)DroneVision_CanSeePosition(droneArg->gameObj, &world.pos, celArg, objArg); });
        Compare("Drone_PositionVisible", trial,
                [] {
                    OriginalVision o;
                    return ORIGINAL(0x000318c0, uint32_t(__cdecl *)(_VECTOR *, cel_tag *))(&world.pos, celArg) & 0xff;
                },
                [] { return (uint32_t)Drone_PositionVisible(&world.pos, celArg); });
    }
}

void TestSeeing() {
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        droneArg = ShadowRand(30) ? &world.drones[ShadowRand(kDrones)] : NULL;
        switch (ShadowRand(5)) {
        case 0:
            objArg = &world.player;
            break;
        case 1:
            objArg = &world.thing;
            break;
        case 2:
            objArg = NULL;
            break;
        default:
            objArg = &world.objs[ShadowRand(kDrones)];
            break;
        }
        Compare("DroneVision_ObjectVisibility", trial,
                [] {
                    OriginalVision o;
                    return Bits(ORIGINAL(0x000643c0, float(__cdecl *)(Drone_tag *, obj_tag *))(droneArg, objArg));
                },
                [] { return Bits(DroneVision_ObjectVisibility(droneArg, objArg)); });
        droneArg = &world.drones[ShadowRand(kDrones)];
        Compare("DroneVision_OpponentVisibility", trial,
                [] {
                    OriginalVision o;
                    theirVisibility = OriginalOpponentVisibility(droneArg);
                    theirStoredVisibility = droneArg->opponentVisibility;
                    return DoubleBits(theirVisibility);
                },
                [] {
                    double visibility = DroneVision_OpponentVisibility(droneArg);
                    if (AcceptUlp(&visibility, theirVisibility, 2) | AcceptUlp(&droneArg->opponentVisibility,
                                                                            theirStoredVisibility))
                        ulpAccepted++;
                    return DoubleBits(visibility);
                });
        static const float ranges[] = {0.0f, -0.0f, 10.0f, 30.0f};
        floatArg = Pick(ranges, 4, -5.0f, 60.0f);
        Compare("DroneVision_OpponentInView", trial,
                [] {
                    OriginalVision o;
                    return OriginalOpponentInView(droneArg, floatArg) & 0xff;
                },
                [] { return (uint32_t)DroneVision_OpponentInView(droneArg, floatArg); });
    }
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        droneArg = &world.drones[ShadowRand(kDrones)];
        Compare("DroneVision_OpponentLOS", trial,
                [] {
                    OriginalVision o;
                    return ORIGINAL(0x00066030, uint32_t(__cdecl *)(Drone_tag *))(droneArg) & 0xff;
                },
                [] { return (uint32_t)DroneVision_OpponentLOS(droneArg); });
        Compare("DroneVision_SeekOpponentLOS", trial,
                [] {
                    OriginalVision o;
                    return OriginalSeekOpponentLOS(droneArg) & 0xff;
                },
                [] { return (uint32_t)DroneVision_SeekOpponentLOS(droneArg); });
        Compare("DroneVision_LogSeenOpponent", trial,
                [] {
                    OriginalVision o;
                    ORIGINAL(0x000654b0, void(__cdecl *)(Drone_tag *))(droneArg);
                    return 0u;
                },
                [] {
                    DroneVision_LogSeenOpponent(droneArg);
                    return 0u;
                });
        if (trial % 4 == 0)
            droneArg->sightFrames = 2;
        Compare("DroneVision_HaveOpponentSight", trial,
                [] {
                    OriginalVision o;
                    OriginalHaveOpponentSight(droneArg);
                    return 0u;
                },
                [] {
                    DroneVision_HaveOpponentSight(droneArg);
                    return 0u;
                });
        uintArg = ShadowRand(3) == 0;
        Compare("Drone_VisibilityForPosition", trial,
                [] {
                    OriginalVision o;
                    return Bits(ORIGINAL(0x00030470, float(__cdecl *)(_VECTOR *, uint))(&world.pos, uintArg));
                },
                [] { return Bits(Drone_VisibilityForPosition(&world.pos, (ushort)uintArg)); });
    }
}

void TestAlerts() {
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        droneArg = ShadowRand(4) == 0 ? NULL : &world.drones[ShadowRand(kDrones)];
        static const uint ids[] = {0, 1, 2, 3, 4, 5, 6, 7, 99};
        uintArg = ids[ShadowRand(9)];
        Compare("DroneVision_FindDrone", trial,
                [] {
                    OriginalVision o;
                    return OriginalFindDrone(droneArg, uintArg);
                },
                [] { return (uint32_t)(uintptr_t)DroneVision_FindDrone(droneArg, uintArg); });
        Compare("DroneVision_DroneAfter", trial,
                [] {
                    OriginalVision o;
                    return (uint32_t)(uintptr_t)ORIGINAL(0x000645c0, Drone_tag *(__fastcall *)(Drone_tag *, uint))(
                        droneArg, uintArg);
                },
                [] { return (uint32_t)(uintptr_t)DroneVision_DroneAfter(droneArg, uintArg); });
    }
    for (int trial = 0; trial < 12000; trial++) {
        RandomWorld();
        int a = ShadowRand(kDrones);
        int b = (a + 1 + ShadowRand(kDrones - 1)) % kDrones;
        droneArg = &world.drones[a];
        otherArg = &world.drones[b];
        // Near each other more often than not, so the property chain gets as far as the line of sight
        if (ShadowRand(3)) {
            otherArg->gameObj->position = droneArg->gameObj->position;
            otherArg->gameObj->position.x += ShadowRandFloat(-8.0f, 8.0f);
        }
        charPtrArg = ShadowRand(5) ? &world.inView : NULL;
        Compare("DroneVision_DroneCanSeeDrone", trial,
                [] {
                    OriginalVision o;
                    return ORIGINAL(0x000658e0, uint32_t(__cdecl *)(Drone_tag *, Drone_tag *, char *))(
                               droneArg, otherArg, charPtrArg) & 0xff;
                },
                [] { return (uint32_t)DroneVision_DroneCanSeeDrone(droneArg, otherArg, charPtrArg); });
        Compare("DroneVision_ConsiderAlerted", trial,
                [] {
                    OriginalVision o;
                    return OriginalConsiderAlerted(droneArg, otherArg) & 0xff;
                },
                [] { return (uint32_t)DroneVision_ConsiderAlerted(droneArg, otherArg); });
        Compare("DroneVision_DetermineAlertCheck", trial,
                [] {
                    OriginalVision o;
                    return OriginalDetermineAlertCheck(droneArg, otherArg) & 0xff;
                },
                [] { return (uint32_t)DroneVision_DetermineAlertCheck(droneArg, otherArg); });
    }
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        // Drones that know something and drones open to it, close together, so that pairs alert
        for (int i = 0; i < kDrones; i++) {
            Drone_tag *d = &world.drones[i];
            if (ShadowRand(3)) {
                d->gameObj->position = world.drones[0].gameObj->position;
                d->gameObj->position.z += ShadowRandFloat(-6.0f, 6.0f);
            }
            if (ShadowRand(2))
                d->flags = (d->flags | 0x2100) & ~0x20600;
            // Half the frames: drones that all see round them, open to every alert, so that five pairs are in view
            // and the search stops
            if (trial & 1) {
                d->gameObj->objectType = OBJECTTYPE_DRONE;
                d->gameObj->flags = 0;
                d->health = 50.0f;
                d->viewAngle = 0.0f;
                d->viewRange = 40.0f;
                d->side = 1;
                d->dtype = DTYPE_Normal;
                d->sm.curState = DSTATE_Idle;
                d->alertedFrame = GameState.NumFramesUnpaused - 200;
                d->behaviour1[0] = d->behaviour1[1] = d->behaviour1[2] = 0xffffffffu;
            }
        }
        world.losBits = ShadowRand(3) ? 0xffffffffu : world.losBits;
        world.rayCelBits = 0xffffffffu;
        Compare("DroneVision_FindAlertedDrones", trial,
                [] {
                    OriginalVision o;
                    ORIGINAL(0x00065d60, void(__cdecl *)(void))();
                    return 0u;
                },
                [] {
                    DroneVision_FindAlertedDrones();
                    return 0u;
                });
    }
}

void TestEachFrame() {
    for (int trial = 0; trial < 10000; trial++) {
        RandomWorld();
        if (ShadowRand(2))
            for (int i = 0; i < kDrones; i++)
                world.drones[i].flags = (world.drones[i].flags | 0x10100) & ~0x600;
        // A count with no list would have both run off NULL
        if (NPCGlobals.NDrone2List == NULL)
            NPCGlobals.NDrone2Count = 0;
        Compare("DroneVision_ProcessDroneSight", trial,
                [] {
                    OriginalVision o;
                    ORIGINAL(0x000661f0, void(__cdecl *)(void))();
                    return 0u;
                },
                [] {
                    DroneVision_ProcessDroneSight();
                    return 0u;
                });
    }
}

} // namespace

void DroneVisionShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], mpSettings[sizeof(MPSettings_t)], npc[sizeof(NPCGlobals_t)];
    static uint8_t boxList[sizeof(LLISTINFO_tag)];
    static viewer_tag *viewers[11];
    static obj_tag *players[4];
    static uint32_t rand[2];
    uint frameRateInt = FRAME_RATE_INT;
    float frameRate = _FRAME_RATE;
    float frameDiv = FRAME_RATE_DIV;
    uint blind = switch_BLIND_DRONES;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(mpSettings, &MPSettings, sizeof(mpSettings));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(boxList, &AIBoxList, sizeof(boxList));
    memcpy(viewers, &glb_viewer, sizeof(viewers));
    memcpy(players, &glb_players, sizeof(players));
    XbeOursRead(rand, RandWords.at, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&MPSettings, sizeof(MPSettings_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(&AIBoxList, sizeof(LLISTINFO_tag));
    regions.Add(&glb_viewer, sizeof(viewers));
    regions.Add(&glb_players, sizeof(players));
    regions.Add(RandWords.game(), 8);
    regions.Add(&FRAME_RATE_INT, 4);
    regions.Add(&_FRAME_RATE, 4);
    regions.Add(&FRAME_RATE_DIV, 4);
    regions.Add(&switch_BLIND_DRONES, 4);
    regions.Add(Drone_View_Bones, 7 * 4);
    ShadowFakes_AddRegions(regions);

    InstallFakes();
    NfWarnMuted++;
    cases = differ = ulpAccepted = 0;

    TestLinesOfSight();
    int sightCases = cases, sightDiffer = differ;
    TestSeeing();
    int seeingCases = cases, seeingDiffer = differ;
    TestAlerts();
    int alertCases = cases, alertDiffer = differ;
    TestEachFrame();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&MPSettings, mpSettings, sizeof(mpSettings));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(&AIBoxList, boxList, sizeof(boxList));
    memcpy(&glb_viewer, viewers, sizeof(viewers));
    memcpy(&glb_players, players, sizeof(players));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
    FRAME_RATE_INT = frameRateInt;
    _FRAME_RATE = frameRate;
    FRAME_RATE_DIV = frameDiv;
    switch_BLIND_DRONES = blind;

    printf("[dronevision] lines of sight: %d cases, %d differ\n", sightCases, sightDiffer);
    printf("[dronevision] how well drones see: %d cases, %d differ (%d OpponentVisibility results up to 2 ulps apart)\n",
           seeingCases - sightCases, seeingDiffer - sightDiffer, ulpAccepted);
    printf("[dronevision] drones alerting drones: %d cases, %d differ\n", alertCases - seeingCases,
           alertDiffer - seeingDiffer);
    printf("[dronevision] each frame: %d cases, %d differ\n", cases - alertCases, differ - alertDiffer);
}
