// Shadow tests for the drone animation system (game/drone/DroneAnim.cpp's rows, calls and script callbacks, and
// AnimObjectSetHeadTracking, AnimScriptCallOnEvent, AnimScriptCallOnStop and AnimScriptSetAnimRotation in
// engine/Anim.cpp): each original and ours run on the same made-up drones, with what needs a level going to logging
// fakes. Run at start with MenuShadowTests=on (MenuProbe.cpp), before any level exists.
//
// Faked, so not tested here: the scripts themselves (AnimScriptAdd and AnimScriptAppendBlend hand out made-up
// scripts, AnimScriptDelete only logs), whether the drone animates (NDrone2_DoAnimation), the bones
// (AnimGetBoneWorldTrans), the opponent's angles (Drone_GetOpponentInfo), the end action (DroneAnim_SetEndAIState),
// messages, the weapons and the datums. The register-argument helpers (DroneAnim_CanSetAnimCall, CallFullyComplete,
// GetDAnimForCall, GetDAnimForAnimState, TakeCall, SkipAnim, BaseCallState, SetDAnimVeryInternal) are tested through
// their callers: the original side runs the originals throughout, ours the C++ bodies.

#include "DroneAnimShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../game/drone/DroneAnim.h"
#include "../game/drone/NDrone2.h"
#include "../game/mp/multiplayer.h"
#include "../math/math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords (GameSpan<uint32_t>{0x0018cdf8})

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int kDrones = 2;
const int kScripts = 4;     // 0 the drone's, 1 one blending out, 2 AnimScriptAdd's, 3 AnimScriptAppendBlend's

struct World {
    obj_tag objs[kDrones];
    Drone_tag drones[kDrones];
    AnimState anims[kDrones];
    sAnimScript_tag scripts[kScripts];
    obj_tag other;              // not a drone
    DCVars_tag dcv;
    _VECTOR delta;
    _VECTOR target;

    // What the fakes answer
    int doAnimation;
};
World world;

ShadowRegions regions;
int cases, differ;

void SameCallbacks();

// Both sides' scripts then hold the original's callback addresses (SameCallbacks)
uint32_t (*theirFn)(void);
uint32_t (*ourFn)(void);

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    theirFn = theirs;
    ourFn = ours;
    if (!ShadowCompare(regions, "droneanim", what, trial,
                       [] {
                           uint32_t result = theirFn();
                           SameCallbacks();
                           return result;
                       },
                       [] {
                           uint32_t result = ourFn();
                           SameCallbacks();
                           return result;
                       }))
        differ++;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

int WhoObj(const void *obj) {
    for (int i = 0; i < kDrones; i++)
        if (obj == &world.objs[i])
            return i;
    if (obj == &world.other)
        return 100;
    if (obj == &world.target)
        return 101;
    return obj == NULL ? -1 : 999;
}

int WhoDrone(const Drone_tag *drone) {
    for (int i = 0; i < kDrones; i++)
        if (drone == &world.drones[i])
            return i;
    return drone == NULL ? -1 : 999;
}

int WhoScript(const void *p) {
    for (int i = 0; i < kScripts; i++)
        if (p == &world.scripts[i])
            return i;
    for (int i = 0; i < kDrones; i++) {
        if (p == &world.drones[i].animScript)
            return 10 + i;
        if (p == &world.drones[i].blendScript)
            return 20 + i;
    }
    return p == NULL ? -1 : 999;
}

// ---- the fakes

bool __cdecl FakeDoAnimation(Drone_tag *drone) {
    ShadowLog("doanimation(%d)", WhoDrone(drone));
    return world.doAnimation != 0;
}

sAnimScript_tag *__cdecl FakeScriptAdd(obj_tag *obj, HASHCODE hashcode) {
    ShadowLog("scriptadd(%d,%x)", WhoObj(obj), hashcode);
    return &world.scripts[2];
}

sAnimScript_tag *__cdecl FakeAppendBlend(obj_tag *obj, HASHCODE hashcode) {
    ShadowLog("appendblend(%d,%x)", WhoObj(obj), hashcode);
    return &world.scripts[3];
}

void __cdecl FakeScriptDelete(obj_tag *obj, sAnimScript_tag **slot) {
    ShadowLog("scriptdelete(%d,%d,%d)", WhoObj(obj), WhoScript(slot), WhoScript(*slot));
}

void __cdecl FakeBone(obj_tag *obj, uint which, int datum, _VECTOR *pos, _MATRIX *mtx) {
    ShadowLog("bone(%d,%x,%d)", WhoObj(obj), which, datum);
    float angle = obj->rotation.y * 0.5f + 0.3f;
    float c = cosf(angle), s = sinf(angle);
    float m[15] = {c, 0.0f, -s, 0.0f, 1.0f, 0.0f, s, 0.0f, c, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 3.0f};
    memcpy(mtx->m, m, sizeof(m));
    pos->x = 1.0f;
    pos->y = 2.0f;
    pos->z = 3.0f;
}

void __cdecl FakeOpponentInfo(Drone_tag *drone, obj_tag *obj) {
    ShadowLog("opponentinfo(%d,%d)", WhoDrone(drone), WhoObj(obj));
}

void __cdecl FakeSetEndAIState(Drone_tag *drone, short state, uint msg) {
    ShadowLog("endaistate(%d,%d,%x)", WhoDrone(drone), state, msg);
}

void __cdecl FakeSendMsg(uint msg, uint scope, uint sender, int receiver) {
    ShadowLog("sendmsg(%x,%x,%x,%x)", msg, scope, sender, receiver);
}

void __cdecl FakeThrowGrenade(DCVars_tag *dcv) {
    ShadowLog("grenade(%d)", WhoDrone(dcv->drone));
}

void __cdecl FakeChangeWeapon(DCVars_tag *dcv, int weapon) {
    ShadowLog("changeweapon(%d,%d)", WhoDrone(dcv->drone), weapon);
}

void __cdecl FakeCloseCombat(DCVars_tag *dcv, int kind) {
    ShadowLog("closecombat(%d,%d)", WhoDrone(dcv->drone), kind);
}

void __cdecl FakeDatum(obj_tag *obj, int datum, HASHCODE entity, uchar frames) {
    ShadowLog("datum(%d,%d,%x,%u)", WhoObj(obj), datum, entity, frames);
}

// Our callbacks given to a script, as the original's addresses, so that both sides' scripts compare equal
void SameCallbacks() {
    for (int i = 0; i < kScripts; i++) {
        sAnimScript_tag *s = &world.scripts[i];
        if (s->onEvent == &DroneAnim_EventFunc)
            s->onEvent = (AnimScriptEventFunc)(uintptr_t)0x00033590;
        if (s->onStop == &DroneAnim_AnimEndCallback)
            s->onStop = (AnimScriptStopFunc)(uintptr_t)0x00033300;
    }
}

// The rows' functions the drones are given
void __cdecl FakeAnimFunc(Drone_tag *drone, ushort phase) {
    ShadowLog("animfunc(%d,%u)", WhoDrone(drone), phase);
}

void __cdecl FakeStateFunc(Drone_tag *drone, ushort phase) {
    ShadowLog("statefunc(%d,%u)", WhoDrone(drone), phase);
}

void InstallFakes() {
    ShadowFakes_Install();
    ShadowHook_Install(0x00037730, (const void *)&FakeDoAnimation);
    ShadowHook_Install(0x00017220, (const void *)&FakeScriptAdd);
    ShadowHook_Install(0x00015ca0, (const void *)&FakeAppendBlend);
    ShadowHook_Install(0x00015b40, (const void *)&FakeScriptDelete);
    ShadowHook_Install(0x00013070, (const void *)&FakeBone);
    ShadowHook_Install(0x00031260, (const void *)&FakeOpponentInfo);
    ShadowHook_Install(0x00032e70, (const void *)&FakeSetEndAIState);
    ShadowHook_Install(0x000317e0, (const void *)&FakeSendMsg);
    ShadowHook_Install(0x00066cd0, (const void *)&FakeThrowGrenade);
    ShadowHook_Install(0x00066b20, (const void *)&FakeChangeWeapon);
    ShadowHook_Install(0x00066500, (const void *)&FakeCloseCombat);
    ShadowHook_Install(0x000135d0, (const void *)&FakeDatum);
}

// Every original of the package at once, so they reach each other rather than ours
struct Originals {
    XbeOriginalScope onEvent{0x00012860}, onStop{0x00012870}, rotation{0x00012a90}, headTracking{0x000119a0},
        setAnimD{0x00035e10}, internal{0x00034550}, htAnim{0x00033f20}, snap{0x00033150}, handler{0x00035cb0},
        canSet{0x00032dd0}, complete{0x00033e60}, event{0x00033590}, endCallback{0x00033300},
        freeScript{0x00033120}, now{0x00035dc0}, baseCall{0x00032ed0}, veryInternal{0x00032f30},
        skip{0x00032fe0}, forState{0x00033040}, onStart{0x000331d0}, onEnd{0x00033240}, preTrans{0x00033320},
        forCall{0x00033de0}, endAnim{0x00033ea0}, takeCall{0x000344f0}, run{0x00035ec0}, animInfo{0x00035ef0},
        stateInfo{0x00035f20};
};

// ---- the made-up world

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

DroneAnimFunc RandomFunc() {
    switch (ShadowRand(5)) {
    case 0:
        return &FakeAnimFunc;
    case 1:
        return &FakeStateFunc;
    case 2:
        return (DroneAnimFunc)(uintptr_t)0x00035ec0;     // DAnimFunc_Run
    default:
        return NULL;
    }
}

void RandomScript(sAnimScript_tag *s) {
    memset(s, 0, sizeof(*s));
    static const short frameCounts[] = {0, 1, 10, 15, 16, 20, 300, -4};
    s->numFrames = frameCounts[ShadowRand(8)];
    static const float frames[] = {0.0f, 1.0f, 9.5f, 10.0f, 15.0f, 19.0f, 20.0f, 25.0f, NAN};
    s->curFrame = Pick(frames, 9);
    s->speed = SpecialOr(-2.0f, 2.0f);
    s->flags = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    s->fadeTime = SpecialOr(0.0f, 30.0f);
    s->fadeLen = SpecialOr(0.0f, 30.0f);
    s->rotBaseYaw = SpecialOr(-3.0f, 3.0f);
    s->onEvent = ShadowRand(2) ? NULL : (AnimScriptEventFunc)(uintptr_t)0x12345678;
    s->onStop = ShadowRand(2) ? NULL : (AnimScriptStopFunc)(uintptr_t)0x23456789;
    s->ownerSlot = ShadowRand(2) ? NULL : (sAnimScript_tag **)&world.drones[1].animScript;
}

// Anim states the code singles out, and any
ushort RandomAnimState() {
    static const ushort states[] = {0, 0xc, 0xd, 0x29, 0x2a, 0x24, 0x40, 0x76, 0x77, 0xffff, 10, 0x21, 0x5c};
    return ShadowRand(3) ? states[ShadowRand(13)] : (ushort)ShadowRand(NUM_DRONE_ANIM_STATES);
}

ushort RandomRow() {
    static const ushort rows[] = {0, 1, 0x5c, 0xdc, 0xf4, 0x1a5, 0x11a, 0x82, 0x7d, 0x32};
    return ShadowRand(2) ? rows[ShadowRand(10)] : (ushort)ShadowRand(NUM_DRONE_ANIM_ROWS);
}

ushort RandomCall() {
    static const ushort calls[] = {0, 10, 0x40, 0x41, 0x44, 0x45, 0x50, 0x6f, 0x24, 0x21, 0x0b};
    return ShadowRand(2) ? calls[ShadowRand(11)] : (ushort)ShadowRand(NUM_DRONE_ANIM_STATES);
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    AnimState *anim = &world.anims[i];
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    memset(anim, 0, sizeof(*anim));

    o->objectType = ShadowRand(10) == 0 ? OBJECTTYPE_DEAD_DRONE : OBJECTTYPE_DRONE;
    o->extraObjectData = d;
    o->animState = anim;
    RandomVector(&o->position, 10.0f);
    RandomVector(&o->rotation, 3.0f);
    for (int k = 0; k < 15; k++)
        o->transformMatrix.m[k] = ShadowRandFloat(-1.0f, 1.0f);
    o->drawnCountdown = (char)(ShadowRand(2) ? 0 : 2);
    d->gameObj = o;
    d->sm.id = 7 + i;

    anim->animObj.flags = ShadowRand(0x400);
    static const float roots[] = {0.0f, -0.0f, 0.1f, 0.21f, 0.22f, 0.2200001f, 1.0f, 1.3f, NAN};
    anim->animObj.rootHeight = Pick(roots, 9);
    anim->animObj.headTrackDelay = (uchar)ShadowRand(256);

    static const short animSets[] = {0, 1, 2, 4, 5, 6, 0xc, 0xe, 0x12, 0x13, 26};
    d->animSet = animSets[ShadowRand(11)];
    d->animState = (short)RandomAnimState();
    d->animRow = RandomRow();
    d->animMode = (uchar)ShadowRand(11);
    d->animPriority = (uchar)ShadowRand(256);
    d->animCall.state = RandomCall();
    d->animCall.variant = (ushort)(ShadowRand(3) ? ShadowRand(4) : ShadowRand(0x10000));
    d->animCall.fromState = ShadowRand(2) ? (ushort)d->animState : RandomAnimState();
    d->animCall.pending = (uchar)ShadowRand(2);
    d->animCall.frame = ShadowRand(2000);
    d->animCallFlag = (uchar)ShadowRand(2);
    d->animPlaying = (uchar)ShadowRand(2);
    d->animOnce = (uchar)ShadowRand(2);
    d->animCallTaken = (uchar)ShadowRand(2);
    d->animNextRow = ShadowRand(3) == 0 ? 0 : (ShadowRand(4) == 0 ? d->animRow : RandomRow());
    d->animEndBlend = (uchar)ShadowRand(3) == 0 ? 0 : (uchar)ShadowRand(40);
    static const uint animFlags[] = {0, 1, 4, 5, 0x80, 0x100, 0x101, 0x200, 0x384, 0x105};
    d->animFlags = animFlags[ShadowRand(10)];
    d->prevAnimFlags = animFlags[ShadowRand(10)];
    d->animHashcode = (HASHCODE)(HT_Script_HASHCODE_BASE + ShadowRand(3) * 0x123);
    d->animSpeed = SpecialOr(0.5f, 1.5f);
    d->animMoveSpeed = SpecialOr(0.0f, 2.0f);
    d->animCallSpeed = SpecialOr(0.0f, 2.0f);
    d->animEndState = (short)(ShadowRand(3) ? 0 : ShadowRand(70) - 2);
    d->animEndMsg = ShadowRand(3) ? 0 : ShadowRand(0x30);
    d->animScript = ShadowRand(5) == 0 ? NULL : &world.scripts[0];
    d->blendScript = ShadowRand(3) == 0 ? &world.scripts[1] : NULL;
    d->animFunc = RandomFunc();
    d->animStateFunc = RandomFunc();
    static const float snaps[] = {0.0f, -0.0f, PI, -HALF_PI, 0.3f, NAN};
    d->pendingTurn = Pick(snaps, 6);
    d->targetRot.y = SpecialOr(-3.0f, 3.0f);
    static const float healths[] = {0.0f, -0.0f, -5.0f, 50.0f, NAN};
    d->health = Pick(healths, 5);
    d->secondWeapon = (char)(ShadowRand(5) - 1);
    d->hostagePartner = ShadowRand(2) ? &world.objs[1 - i] : NULL;
    d->heavyHit = (uchar)ShadowRand(2);
    d->flags = ShadowRand(0x10000) | (ShadowRand(2) << 31);
    d->aimMode = (uchar)ShadowRand(16);
    d->animMoveState = (uchar)ShadowRand(16);

    static const float heights[] = {0.0f, 0.2f, 0.35f, 1.0f, NAN};
    d->rootHeight = Pick(heights, 5);
    d->prevRootHeight = SpecialOr(0.0f, 1.0f);
    d->playingDeathScript = (uchar)(ShadowRand(4) == 0);
    d->dtype = (uchar)(ShadowRand(4) == 0 ? DTYPE_Bot : ShadowRand(30));
    d->speed = (uchar)ShadowRand(4);
    d->standingStill = (uchar)(ShadowRand(5) == 0);
    d->flies = (uchar)(ShadowRand(5) == 0);
    d->touchingPlayer = (uchar)(ShadowRand(3) == 0);
    RandomVector(&d->contactDir, 1.0f);
    d->door = ShadowRand(4) == 0 ? &world.other : NULL;
    d->boundaryFlags = ShadowRand(0x20);
    RandomVector(&d->scriptedMovePos, 5.0f);
}

void RandomWorld() {
    for (int i = 0; i < kDrones; i++)
        RandomDrone(i);
    for (int i = 0; i < kScripts; i++)
        RandomScript(&world.scripts[i]);
    memset(&world.other, 0, sizeof(world.other));
    world.other.objectType = OBJECTTYPE_ROTOR;
    RandomVector(&world.delta, 0.3f);
    RandomVector(&world.target, 5.0f);
    world.doAnimation = ShadowRand(3) != 0;

    world.dcv.gameObj = &world.objs[0];
    world.dcv.drone = &world.drones[0];
    world.dcv.cel = NULL;
    world.dcv.aiStateMachine = &world.drones[0].sm;

    GameState.NumFramesUnpaused = 1000 + ShadowRand(10);
    static const HASHCODE levels[] = {HT_Level_TowerA, HT_Level_TowerB, HT_Level_TowerC, HT_Level_HendersonA};
    GameState.CurrentLevelHashcode = levels[ShadowRand(4)];
    MPSettings.dronesAreBots = ShadowRand(4) == 0;
    FRAME_RATE_INT = ShadowRand(2) ? 60 : 50;
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

// ---- the cases

obj_tag *objArg;
const void *targetArg;
sAnimScript_tag *scriptArg;
float rangeArg, rateArg, speedArg;
char charArg, char2Arg;
uchar ucharArg;
ushort rowArg, blendArg, frameArg, eventArg;
uint flagsArg;
HASHCODE hashArg;

// AnimObjectSetHeadTracking and the script setters
void TestScripts() {
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        switch (ShadowRand(6)) {
        case 0:
            objArg = NULL;
            break;
        case 1:
            objArg = &world.other;      // no animState
            break;
        default:
            objArg = &world.objs[0];
            break;
        }
        switch (ShadowRand(3)) {
        case 0:
            targetArg = NULL;
            break;
        case 1:
            targetArg = &world.target;
            break;
        default:
            targetArg = &world.objs[1];
            break;
        }
        static const float ranges[] = {0.0f, -0.0f, 3.0f, 100.0f, -2.0f, NAN, INFINITY};
        rangeArg = Pick(ranges, 7);
        rateArg = SpecialOr(0.0f, 1.0f);
        charArg = (char)ShadowRand(3);
        char2Arg = (char)ShadowRand(3);
        ucharArg = (uchar)ShadowRand(256);
        Compare("AnimObjectSetHeadTracking", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x000119a0, void(__cdecl *)(obj_tag *, const void *, float, float, char, char, uchar))(
                        objArg, targetArg, rangeArg, rateArg, charArg, char2Arg, ucharArg);
                    return 0u;
                },
                [] {
                    AnimObjectSetHeadTracking(objArg, targetArg, rangeArg, rateArg, charArg, char2Arg, ucharArg);
                    return 0u;
                });
        scriptArg = &world.scripts[ShadowRand(kScripts)];
        Compare("AnimScriptCallOnEvent", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00012860, void(__cdecl *)(sAnimScript_tag *, AnimScriptEventFunc))(
                        scriptArg, &DroneAnim_EventFunc);
                    return 0u;
                },
                [] {
                    AnimScriptCallOnEvent(scriptArg, &DroneAnim_EventFunc);
                    return 0u;
                });
        Compare("AnimScriptCallOnStop", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00012870, void(__cdecl *)(sAnimScript_tag *, AnimScriptStopFunc))(scriptArg, NULL);
                    return 0u;
                },
                [] {
                    AnimScriptCallOnStop(scriptArg, NULL);
                    return 0u;
                });
        Compare("AnimScriptSetAnimRotation", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00012a90, void(__cdecl *)(sAnimScript_tag *, char))(scriptArg, charArg);
                    return 0u;
                },
                [] {
                    AnimScriptSetAnimRotation(scriptArg, charArg);
                    return 0u;
                });
    }
}

// Rows and calls
void TestRows() {
    for (int trial = 0; trial < 12000; trial++) {
        RandomWorld();
        Compare("DroneAnim_CallHandler", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00035cb0, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneAnim_CallHandler(&world.dcv);
                    return 0u;
                });
        rowArg = RandomRow();
        static const ushort blends[] = {0, 8, 30, 0x1234};
        blendArg = blends[ShadowRand(4)];
        ucharArg = (uchar)ShadowRand(3);
        Compare("DroneAnim_SetDAnimInternal", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x00034550, uint32_t(__cdecl *)(ushort, ushort, uchar, Drone_tag *))(
                        rowArg, blendArg, ucharArg, &world.drones[0]) & 0xff;
                },
                [] { return (uint32_t)DroneAnim_SetDAnimInternal(rowArg, blendArg, ucharArg, &world.drones[0]); });
        Compare("DroneAnim_SetDAnimNow", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x00035dc0, uint32_t(__cdecl *)(ushort, ushort, uchar, DCVars_tag *))(
                        rowArg, blendArg, ucharArg, &world.dcv) & 0xff;
                },
                [] { return (uint32_t)DroneAnim_SetDAnimNow(rowArg, blendArg, ucharArg, &world.dcv); });
        Compare("DroneAnim_SetAnimD", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00035e10, void(__cdecl *)(ushort, ushort, Drone_tag *))(rowArg, blendArg,
                                                                                        &world.drones[1]);
                    return 0u;
                },
                [] {
                    DroneAnim_SetAnimD(rowArg, blendArg, &world.drones[1]);
                    return 0u;
                });
        hashArg = (HASHCODE)(HT_Script_HASHCODE_BASE + ShadowRand(0x800));
        static const uint htFlags[] = {0, 1, 4, 0x100, 0x101};
        flagsArg = htFlags[ShadowRand(5)];
        static const ushort frames[] = {0, 1, 5, 999, 998};
        frameArg = frames[ShadowRand(5)];
        static const float speeds[] = {1.0f, 0.5f, 1.5f, 0.0f, NAN};
        speedArg = Pick(speeds, 5);
        Compare("DroneAnim_SetHTAnim", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00033f20, void(__cdecl *)(HASHCODE, uint, ushort, Drone_tag *, ushort, float))(
                        hashArg, flagsArg, frameArg, &world.drones[0], blendArg, speedArg);
                    return 0u;
                },
                [] {
                    DroneAnim_SetHTAnim(hashArg, flagsArg, frameArg, &world.drones[0], blendArg, speedArg);
                    return 0u;
                });
        Compare("DroneAnim_SetEndAnim", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x00033ea0, uint32_t(__cdecl *)(Drone_tag *, uchar))(&world.drones[0],
                                                                                         ucharArg) & 0xff;
                },
                [] { return (uint32_t)DroneAnim_SetEndAnim(&world.drones[0], ucharArg); });
        flagsArg = ShadowRand(0x400);
        ucharArg = (uchar)ShadowRand(256);
        Compare("DroneAnim_OnEndAnim", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x00033240, uint32_t(__cdecl *)(Drone_tag *, uint, uchar))(
                        &world.drones[0], flagsArg, ucharArg) & 0xff;
                },
                [] { return (uint32_t)DroneAnim_OnEndAnim(&world.drones[0], flagsArg, ucharArg); });
        if (world.drones[0].animScript != NULL) {
            Compare("DroneAnim_OnStartAnim", trial,
                    [] {
                        Originals o;
                        ORIGINAL(0x000331d0, void(__cdecl *)(Drone_tag *))(&world.drones[0]);
                        return 0u;
                    },
                    [] {
                        DroneAnim_OnStartAnim(&world.drones[0]);
                        return 0u;
                    });
            rowArg = (ushort)ShadowRand(3);
            Compare("DAnimFunc_Run", trial,
                    [] {
                        Originals o;
                        ORIGINAL(0x00035ec0, void(__cdecl *)(Drone_tag *, ushort))(&world.drones[0], rowArg);
                        return 0u;
                    },
                    [] {
                        DAnimFunc_Run(&world.drones[0], rowArg);
                        return 0u;
                    });
        }
        Compare("DroneAnim_FreeScript", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00033120, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneAnim_FreeScript(&world.dcv);
                    return 0u;
                });
        rowArg = ShadowRand(2) ? (ushort)ShadowRand(0x10000) : (ShadowRand(2) ? 0xdc : 10);
        Compare("DroneData_GetAnimFuncInfo", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x00035ef0, uint32_t(__cdecl *)(ushort))(rowArg);
                },
                [] { return (uint32_t)(uintptr_t)DroneData_GetAnimFuncInfo(rowArg); });
        Compare("DroneData_GetDAStateFuncInfo", trial,
                [] {
                    Originals o;
                    return ORIGINAL(0x00035f20, uint32_t(__cdecl *)(ushort))(rowArg);
                },
                [] { return (uint32_t)(uintptr_t)DroneData_GetDAStateFuncInfo(rowArg); });
    }
}

// The script callbacks, the snap turn and the root's move
void TestCallbacks() {
    for (int trial = 0; trial < 12000; trial++) {
        RandomWorld();
        world.objs[0].objectType = OBJECTTYPE_DRONE;    // DroneAnim_EventFunc trusts Drone_DCVfromOBJ
        eventArg = (ushort)(ShadowRand(10) == 0 ? ShadowRand(0x10000) : ShadowRand(0x14));
        switch (ShadowRand(4)) {
        case 0:
            scriptArg = &world.scripts[1];
            break;
        case 1:
            scriptArg = NULL;
            break;
        default:
            scriptArg = world.drones[0].animScript;
            break;
        }
        Compare("DroneAnim_EventFunc", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00033590, void(__cdecl *)(obj_tag *, AnimObj *, sAnimScript_tag *, ushort))(
                        &world.objs[0], &world.anims[0].animObj, scriptArg, eventArg);
                    return 0u;
                },
                [] {
                    DroneAnim_EventFunc(&world.objs[0], &world.anims[0].animObj, scriptArg, eventArg);
                    return 0u;
                });
        Compare("DroneAnim_AnimEndCallback", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00033300, void(__cdecl *)(obj_tag *, AnimObj *, sAnimScript_tag *))(
                        &world.objs[0], &world.anims[0].animObj, scriptArg);
                    return 0u;
                },
                [] {
                    DroneAnim_AnimEndCallback(&world.objs[0], &world.anims[0].animObj, scriptArg);
                    return 0u;
                });
        Drone_tag *snapDrone = ShadowRand(20) == 0 ? NULL : &world.drones[0];
        static Drone_tag *snapArg;
        snapArg = snapDrone;
        Compare("DroneAnim_SnapRotate", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00033150, void(__cdecl *)(Drone_tag *))(snapArg);
                    return 0u;
                },
                [] {
                    DroneAnim_SnapRotate(snapArg);
                    return 0u;
                });
        objArg = ShadowRand(10) == 0 ? &world.other : &world.objs[0];
        Compare("DroneAnim_PreTransCallback", trial,
                [] {
                    Originals o;
                    ORIGINAL(0x00033320, void(__cdecl *)(obj_tag *, AnimObj *, _VECTOR *))(
                        objArg, &world.anims[0].animObj, &world.delta);
                    return 0u;
                },
                [] {
                    DroneAnim_PreTransCallback(objArg, &world.anims[0].animObj, &world.delta);
                    return 0u;
                });
    }
}

} // namespace

void DroneAnimShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], mpSettings[sizeof(MPSettings_t)];
    static uint32_t rand[2];
    uint32_t frameRateInt = FRAME_RATE_INT;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(mpSettings, &MPSettings, sizeof(mpSettings));
    XbeOursRead(rand, RandWords.at, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&MPSettings, sizeof(MPSettings_t));
    regions.Add(RandWords.game(), 8);
    regions.Add(&FRAME_RATE_INT, 4);
    ShadowFakes_AddRegions(regions);

    InstallFakes();
    NfWarnMuted++;
    cases = differ = 0;

    TestScripts();
    int scriptCases = cases, scriptDiffer = differ;
    TestRows();
    int rowCases = cases, rowDiffer = differ;
    TestCallbacks();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&MPSettings, mpSettings, sizeof(mpSettings));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
    FRAME_RATE_INT = frameRateInt;

    printf("[droneanim] head tracking and script setters: %d cases, %d differ\n", scriptCases, scriptDiffer);
    printf("[droneanim] rows and calls: %d cases, %d differ\n", rowCases - scriptCases, rowDiffer - scriptDiffer);
    printf("[droneanim] events, snap turn and root motion: %d cases, %d differ\n", cases - rowCases,
           differ - rowDiffer);
}
