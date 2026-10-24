// Shadow tests for the drone state machine (DroneSM.cpp), the anim calls (DroneAnim.cpp), and the talk, death,
// explosive and helper functions (NDrone2*.cpp, Drone.cpp): each original and ours run on the same made-up drones,
// with the calls that need a level (speech, sounds, the skeleton, sight, pickups, the mission) going to logging fakes.
// Run at start with MenuShadowTests=on (MenuProbe.cpp), before any level exists.
//
// The drones' state machines run through a fake processFunction that logs every message and asks for state changes
// from a script, so the dispatcher's change loop, the bots' extra messages and the delayed queue all run. Random draws
// are kept in step: both sides start from the same Rand_Random state, and it is compared afterwards.

#include "DroneAIShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../engine/Collide.h"
#include "../game/obj/bullet.h"
#include "../game/drone/BOT.h"
#include "../game/drone/Behaviour.h"
#include "../game/drone/DroneAnim.h"
#include "../game/drone/DroneTables.h"
#include "../game/drone/NDrone2.h"
#include "../game/mp/multiplayer.h"
#include "../game/sp/SwitchChannels.h"
#include "../sound/music.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords (GameSpan<uint32_t>{0x0018cdf8})
// Drone_SfxInfo's sfx field, per speech number (what Drone_IsSayingSfx compares)
#define SfxInfoSound(n) U32_AT(XbeOurs(0x00174b28 + (n) * 0x20))

const int DRONES = 6;

struct World {
    obj_tag objs[DRONES];
    Drone_tag drones[DRONES];
    AnimState anims[DRONES];
    BOT_vars_t botVars;
    obj_tag player;
    AnimState playerAnim;
    obj_tag explosive;
    BU_tag bullet;
    weapon_definition_tag wpn;
    obj_tag switchObj;
    HITDATA_tag hit;
    DroneImpact_tag impact;
    uint8_t sound[0x40];
    uint8_t cel[0x40];
    uint8_t face[0x10];
    DCVars_tag dcv;
    MsgObject msg;
    _VECTOR posOut;     // where the functions under test write
    float yawOut;

    // What the fakes answer
    int speechResult;
    int soundFinished;
    float visibility;
    int opponentArmed;
    ushort switchChannel;
    int botValid;
    uint seeBias;

    // The fake processFunction's script
    int chain;
    uint handledMask;
    uint nextStates[8];
    int queueDuring;
};
World world;

ShadowRegions regions;
int cases, differ;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "droneai", what, trial, theirs, ours))
        differ++;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

int Who(const Drone_tag *drone) {
    for (int i = 0; i < DRONES; i++)
        if (drone == &world.drones[i])
            return i;
    return -1;
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

// ---- the fakes

bool __cdecl FakeProcess(DCVars_tag *dcv, uint state, MsgObject *msg) {
    int who = Who(dcv->drone);
    if (msg->msgType == DRONE_MSG_Enter || msg->msgType == DRONE_MSG_Exit) {
        // The original leaves the rest of these uninitialised
        ShadowLog("proc(%d,%u,%u,%u,%d,%p)", who, state, msg->msgType, msg->sender, msg->receiver, msg->extraData);
    } else if (msg->msgType == DRONE_MSG_BotStateChanged) {
        // and the frames of this one
        ShadowLog("proc(%d,%u,%u,%u,%u,%d,%p)", who, state, msg->msgType, msg->scope, msg->sender, msg->receiver,
                  msg->extraData);
    } else {
        ShadowLog("proc(%d,%u,%u,%u,%u,%d,%u,%u,%p)", who, state, msg->msgType, msg->scope, msg->sender, msg->receiver,
                  msg->createdFrame, msg->handleOnFrame, msg->extraData);
    }
    ShadowLog("cel(%p,%d)", dcv->cel, WhoObj(dcv->gameObj));
    if (msg->msgType == DRONE_MSG_Enter && world.chain > 0) {
        world.chain--;
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)world.nextStates[world.chain & 7], world.chain);
    }
    if (msg->msgType == 0x40 && world.queueDuring) {
        world.queueDuring = 0;
        Drone_SM_SendMsgSelf(0x41, (void *)0x1234, 3, 0, dcv);
    }
    if (msg->msgType == DRONE_MSG_Update && (world.handledMask & 0x100) && world.chain > 0) {
        world.chain--;
        Drone_SM_SetState(dcv->aiStateMachine, (DSTATE)world.nextStates[(world.chain + 3) & 7], 0);
    }
    return ((state * 31 + msg->msgType) & world.handledMask & 0xff) != 0;
}

undefined4 __cdecl FakeValidate(int *dcv, uint state) {
    ShadowLog("validate(%d,%u)", Who(((DCVars_tag *)dcv)->drone), state);
    return world.botValid;
}

int __cdecl FakeSpeech(DCVars_tag *dcv, uint sfx, char force, char face, char needFace) {
    ShadowLog("speech(%d,%u,%d,%d,%d)", Who(dcv->drone), sfx, force, face, needFace);
    return world.speechResult;
}

bool __cdecl FakeFinished(void *handle) {
    ShadowLog("finished(%p)", handle);
    return world.soundFinished != 0;
}

void __cdecl FakeHeadTracking(obj_tag *obj, void *target, float range, float rate, char randomDelay, char targetIsObj,
                              uchar bone) {
    ShadowLog("headtracking(%d,%p,%08x,%08x,%d,%d,%d)", WhoObj(obj), target, Bits(range), Bits(rate), randomDelay,
              targetIsObj, bone);
}

void __cdecl FakeBone(obj_tag *obj, uint which, int datum, _VECTOR *pos, _MATRIX *mtx) {
    ShadowLog("bone(%d,%x,%d,%d,%d)", WhoObj(obj), which, datum, pos != NULL, mtx != NULL);
    if (pos != NULL) {
        pos->x = (float)(which & 0xff) + 0.25f;
        pos->y = 1.5f;
        pos->z = obj->position.z - 2.0f;
    }
}

bool __cdecl FakeCanSee(Drone_tag *drone, obj_tag *obj, uint bone, ushort mask) {
    ShadowLog("cansee(%d,%d,%u,%x)", Who(drone), WhoObj(obj), bone, mask);
    return ((Who(drone) * 7 + WhoObj(obj) + bone + mask + world.seeBias) & 1) != 0;
}

float __cdecl FakeVisibility(Drone_tag *drone, obj_tag *obj) {
    ShadowLog("visibility(%d,%d)", Who(drone), WhoObj(obj));
    return world.visibility;
}

bool __cdecl FakeOpponentArmed(DCVars_tag *dcv) {
    ShadowLog("opparmed(%d)", Who(dcv->drone));
    return world.opponentArmed != 0;
}

void __cdecl FakeDropWeapon(DCVars_tag *dcv) {
    ShadowLog("dropweapon(%d)", Who(dcv->drone));
}

void __cdecl FakeLogDeath(DCVars_tag *dcv) {
    ShadowLog("logdeath(%d)", Who(dcv->drone));
}

obj_tag *__cdecl FakePickup(_MATRIX *mtx, short weapon, short count, _VECTOR *pos) {
    ShadowLog("pickup(%p,%d,%d,%p)", mtx, weapon, count, pos);
    return NULL;
}

void __cdecl FakeFailLabel(uint label) {
    ShadowLog("faillabel(%x)", label);
}

obj_tag *__cdecl FakeNearestSwitch(_VECTOR *pos) {
    ShadowLog("nearestswitch(%08x,%08x,%08x)", Bits(pos->x), Bits(pos->y), Bits(pos->z));
    return &world.switchObj;
}

ushort __cdecl FakeSwitchChannel(obj_tag *obj) {
    ShadowLog("switchchannel(%p)", obj);
    return world.switchChannel;
}

void __cdecl FakeMusic(uint event, uint value) {
    ShadowLog("music(%u,%u)", event, value);
}

void __cdecl FakeFellOut(obj_tag *obj) {
    ShadowLog("botfellout(%d)", WhoObj(obj));
}

void __cdecl FakeEnable(char enable, Drone_tag *drone) {
    ShadowLog("enable(%d,%d)", enable, Who(drone));
}

void InstallFakes() {
    ShadowHook_Install(0x0001a2d0, (const void *)&FakeValidate);
    ShadowHook_Install(0x00037530, (const void *)&FakeSpeech);
    ShadowHook_Install(0x000cbac0, (const void *)&FakeFinished);
    ShadowHook_Install(0x000119a0, (const void *)&FakeHeadTracking);
    ShadowHook_Install(0x00013070, (const void *)&FakeBone);
    ShadowHook_Install(0x000648b0, (const void *)&FakeCanSee);
    ShadowHook_Install(0x000643c0, (const void *)&FakeVisibility);
    ShadowHook_Install(0x00066610, (const void *)&FakeOpponentArmed);
    ShadowHook_Install(0x00067690, (const void *)&FakeDropWeapon);
    ShadowHook_Install(0x0003a1d0, (const void *)&FakeLogDeath);
    ShadowHook_Install(0x000a7f10, (const void *)&FakePickup);
    ShadowHook_Install(0x0009bdb0, (const void *)&FakeFailLabel);
    ShadowHook_Install(0x000cfed0, (const void *)&FakeNearestSwitch);
    ShadowHook_Install(0x000cff30, (const void *)&FakeSwitchChannel);
    ShadowHook_Install(0x000cbce0, (const void *)&FakeMusic);
    ShadowHook_Install(0x0001adc0, (const void *)&FakeFellOut);
    ShadowHook_Install(0x000376b0, (const void *)&FakeEnable);
}

// ---- the made-up world

const HASHCODE kLevels[] = {
    HT_Level_HendersonA, HT_Level_HendersonB, HT_Level_CastleExterior, HT_Level_CastleIndoors1,
    HT_Level_CastleIndoors2, HT_Level_TowerA, HT_Level_TowerB, HT_Level_TowerC, HT_Level_PowerStationA1,
    HT_Level_PowerStationA2, HT_Level_Tower2A, HT_Level_EvilBase, HT_Level_EvilSilo, HT_Level_SpaceStationD,
    HT_Level_Tower2Elevator, HT_Level_Cut_Level1, HT_Level_SkyRail,
};

// States that the functions single out, and some others
const uint kStates[] = {
    DSTATE_Global, DSTATE_WaitSwitch, DSTATE_PlayScript, DSTATE_Idle, DSTATE_Patrol, DSTATE_HostageDead,
    DSTATE_AllyGoToGoalPosition, DSTATE_GrenadeThrow, DSTATE_PartyGirl, DSTATE_Death_Anim, DSTATE_SpecialDeath_Anim,
    DSTATE_Dead, DSTATE_Fade, DSTATE_FadeFast, DSTATE_Taser, DSTATE_Stunned, DSTATE_PunchImpact,
    DSTATE_ExplosiveImpact, DSTATE_BulletImpact, DSTATE_Attack, DSTATE_GoToGoalPosition, DSTATE_AimStand,
    DSTATE_AimStandFire, DSTATE_RunAwayFromObject, DSTATE_HideFromScaryObject, DSTATE_RecoverFromScaryObject,
    DSTATE_BotGlobal, DSTATE_BotAttack, DSTATE_BotAttackFire, DSTATE_BotAttackNoOpponent, DSTATE_BotAttackReload,
    DSTATE_BotAttackUnarmed, DSTATE_BotImpactBullet, DSTATE_BotDead, DSTATE_BotIdle, 7, 33, 120, 222, 260,
};
const int kNumStates = sizeof(kStates) / sizeof(kStates[0]);

uint RandomState() {
    return kStates[ShadowRand(kNumStates)];
}

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
    v->x = ShadowRandFloat(-range, range);
    v->y = ShadowRandFloat(-range / 4, range / 4);
    v->z = ShadowRandFloat(-range, range);
}

void RandomAnim(AnimState *anim) {
    memset(anim, 0, sizeof(*anim));
    anim->animFlags = (char)ShadowRand(4);
    static const char weapons[] = {0, 1, 2, 0x20, 0x46, 0x47, 0x48, 0x69, 0x6a, 0x71, 0x72, -5, 0x7f};
    anim->currentWeaponId = weapons[ShadowRand(sizeof(weapons))];
    anim->animObj.flags = ShadowRand(4);
    anim->animObj.headTrackSpeed = ShadowRandFloat(0, 1);
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    RandomAnim(&world.anims[i]);

    d->prev = i > 0 ? &world.drones[i - 1] : NULL;
    d->next = i + 1 < DRONES ? &world.drones[i + 1] : NULL;
    d->gameObj = o;
    o->extraObjectData = d;
    o->animState = i == 0 || ShadowRand(8) ? &world.anims[i] : NULL;   // the dcv's drone always animates
    o->objectType = ShadowRand(6) ? OBJECTTYPE_DRONE : OBJECTTYPE_DEAD_DRONE;
    o->inCel = (cel_tag *)world.cel;
    RandomVector(&o->position, 8.0f);
    o->rotation.y = ShadowRandFloat(-7.0f, 7.0f);
    o->curState = (ushort)ShadowRand(250);
    o->flags = (char)(ShadowRand(5) == 0);
    o->drawnCountdown = (char)ShadowRand(3);
    o->tweakA = (uchar)ShadowRand(256);
    o->effectFlags = ShadowRand(4) << 4;
    for (int k = 0; k < 15; k++)
        o->transformMatrix.m[k] = ShadowRandFloat(-1, 1);

    d->female = (uchar)ShadowRand(2);
    d->runningToAlarm = (uchar)(ShadowRand(4) == 0);
    d->captain = (uchar)ShadowRand(2);
    d->inPlayersSights = (uchar)ShadowRand(2);
    d->explosiveSeen = (uchar)(ShadowRand(4) == 0);
    d->headShot = (uchar)ShadowRand(2);
    d->firing = (uchar)ShadowRand(2);
    d->oneBurst = (uchar)ShadowRand(2);
    d->side = (uchar)(1 + ShadowRand(3));
    static const float healths[] = {-1.0f, 0.0f, -0.0f, 50.0f, 0.5f};
    d->health = healths[ShadowRand(5)];
    d->skinClass = (short)ShadowRand(22);
    d->animSet = (short)ShadowRand(NUM_DRONE_ANIM_SETS);
    static const uchar dtypes[] = {0, 1, 2, 12, 12, 19, 22, 13, 30, 30, 5};
    d->dtype = dtypes[ShadowRand(sizeof(dtypes))];
    static const uint skins[] = {HT_Skin_PartyGirl01, HT_Skin_PartyGirl02, HT_Skin_PartyGirl03, HT_Skin_Dominique,
                                 HT_Skin_Drake_Astronaut};
    d->skinHashcode = (HASHCODE)skins[ShadowRand(5)];
    d->distanceToTarget = RandomFloat(0.0f, 6.0f);
    d->opponentBearing = RandomFloat(-3.5f, 3.5f);
    d->sightFlags = ShadowRand(16);
    d->opponentSeenFrame = 900 + ShadowRand(400);
    d->sightFrames = ShadowRand(3);
    d->hitDamageScale = 0.5f;

    d->sm.id = 100 + i;
    d->sm.curState = RandomState();
    d->sm.prevState = RandomState();
    d->sm.nextState = RandomState();
    d->sm.resumeState = RandomState();
    d->sm.stateEnteredFrame = ShadowRand(1000);
    d->sm.scratch1c = ShadowRand(5);
    d->sm.processFunction = FakeProcess;

    for (int w = 0; w < 3; w++) {
        d->behaviour1[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
        d->behaviour2[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    }
    d->currentBehaviour = ShadowRand(2) ? d->behaviour1 : d->behaviour2;

    static const uint flagBits[] = {0x10, 0x20, 0x80, 0x100, 0x200, 0x400, 0x2000, 0x8000, 0x10000, 0x20000,
                                    0x100000, 0x1000000, 0x2000000};
    for (int k = 0; k < 13; k++)
        if (ShadowRand(3) == 0)
            d->flags |= flagBits[k];
    if (ShadowRand(2))
        d->flags |= DRONE_FLAG_ENABLED;

    d->speechChannel = ShadowRand(3) == 0 ? (DYNAMICSOUNDS *)world.sound : NULL;
    d->facialAnim = ShadowRand(3) == 0 ? world.face : NULL;
    switch (ShadowRand(4)) {
    case 0:
        d->lastAttacker = &world.player;
        break;
    case 1:
        d->lastAttacker = &world.objs[ShadowRand(DRONES)];
        break;
    default:
        d->lastAttacker = NULL;
        break;
    }
    d->lastHit = ShadowRand(3) ? &world.hit : NULL;
    d->deathSwitchChannel = (uchar)(ShadowRand(3) == 0 ? 0 : (ShadowRand(2) ? 99 : ShadowRand(256)));
    RandomVector(&d->headPos, 5.0f);
    d->headYaw = ShadowRandFloat(-3, 3);

    d->animState = (short)(ShadowRand(5) == 0 ? 0x24 : ShadowRand(0x7a));
    d->animRow = (ushort)ShadowRand(NUM_DRONE_ANIM_ROWS);
    d->animCall.state = (ushort)ShadowRand(0x7a);
    d->animCall.variant = (ushort)ShadowRand(4);
    d->animCall.pending = (uchar)ShadowRand(2);
    d->animEndState = (short)(ShadowRand(2) ? 0 : ShadowRand(250));
    d->animEndMsg = ShadowRand(2) ? 0 : ShadowRand(0x30);
    d->animCallSpeed = ShadowRand(2) ? 0.0f : 1.5f;
    d->animMoveState = (uchar)ShadowRand(2);
    d->idleAnimFrame = 900 + ShadowRand(300);

    static const uint scratches[] = {0xff, 0xff, 0, 0, 1, 2, 3, 4, 5, 6, 7, 9};
    d->stateScratch = scratches[ShadowRand(sizeof(scratches) / 4)];
    d->stateScratch2 = 900 + ShadowRand(250);
    d->stateScratch3 = ShadowRand(2);
    static const uint steps[] = {0, 1, 2, 3, 99};
    d->talkStep = steps[ShadowRand(5)];
    d->nextTalkFrame = 900 + ShadowRand(250);
    static const float fades[] = {-1.0f, 0.0f, 0.3f, 5.0f, 100.0f, 255.0f, 300.0f};
    d->fadeAlpha = fades[ShadowRand(7)];
    d->botVars = &world.botVars;
}

void RandomWorld() {
    for (int i = 0; i < DRONES; i++)
        RandomDrone(i);
    // Bots only in the state machine tests: elsewhere BOT_validateStateChange's redirect would be read
    memset(&world.botVars, 0, sizeof(world.botVars));

    memset(&world.player, 0, sizeof(world.player));
    world.player.objectType = OBJECTTYPE_PLAYER;
    world.player.animState = &world.playerAnim;
    RandomAnim(&world.playerAnim);
    RandomVector(&world.player.position, 6.0f);
    world.player.rotation.y = ShadowRandFloat(-3, 3);

    memset(&world.explosive, 0, sizeof(world.explosive));
    memset(&world.bullet, 0, sizeof(world.bullet));
    memset(&world.wpn, 0, sizeof(world.wpn));
    static const ObjectType types[] = {OBJECTTYPE_BULLET, OBJECTTYPE_BULLET, OBJECTTYPE_BULLET, OBJECTTYPE_OCCLUDE,
                                       OBJECTTYPE_PICKUP};
    world.explosive.objectType = (char)types[ShadowRand(5)];
    world.explosive.extraObjectData = &world.bullet;
    world.explosive.inCel = (cel_tag *)world.cel;
    RandomVector(&world.explosive.position, 6.0f);
    world.bullet.wpnDef = ShadowRand(6) ? &world.wpn : NULL;
    static const short variants[] = {0x2a, 0x2b, 0x2c, 0x2f, 0x30, 0x34, 0x35, 0x36, 0x37, 0x3b, 0x40, 0x41, 0x6c,
                                     0x6b};
    world.wpn.weaponVariantNum = variants[ShadowRand(sizeof(variants) / 2)];
    world.bullet.fuseTimer = ShadowRandFloat(0, 200);

    memset(&world.hit, 0, sizeof(world.hit));
    static const short bones[] = {4, 5, 3, 0};
    world.hit.hitBoneIdx = bones[ShadowRand(4)];
    RandomVector(&world.hit.hitDirection, 1.0f);
    RandomVector(&world.impact.position, 6.0f);

    memset(world.sound, 0, sizeof(world.sound));
    static const uint talked[] = {1, 3, 4, 9, 0xcd, 0xd2, 0x95, 0x30};
    U32_AT(world.sound + 0x24) = SfxInfoSound(talked[ShadowRand(8)]) | (ShadowRand(2) << 20);

    world.speechResult = ShadowRand(3) ? 1 : 0;
    world.soundFinished = ShadowRand(2);
    static const float visibilities[] = {0.0f, 0.69f, 0.7f, 1.0f};
    world.visibility = visibilities[ShadowRand(4)];
    world.opponentArmed = ShadowRand(2);
    static const ushort channels[] = {0, 0x61, 5, 0x98};
    world.switchChannel = channels[ShadowRand(4)];
    world.botValid = ShadowRand(3) != 0;
    world.seeBias = ShadowRand(2);
    world.chain = ShadowRand(5);
    world.handledMask = ShadowRand(0x200);
    for (int k = 0; k < 8; k++)
        world.nextStates[k] = RandomState();
    world.queueDuring = ShadowRand(2);

    Drone_SM_Init();
    world.dcv.drone = &world.drones[0];
    world.dcv.gameObj = &world.objs[0];
    world.dcv.cel = (cel_tag *)world.cel;
    world.dcv.aiStateMachine = &world.drones[0].sm;

    GameState.NumFramesUnpaused = 1000 + ShadowRand(100);
    GameState.CurrentLevelHashcode = kLevels[ShadowRand(sizeof(kLevels) / sizeof(kLevels[0]))];
    if (ShadowRand(2)) {
        FRAME_RATE_INT = 60;
        FRAME_RATE_DIV = 1.0f;
    } else {
        FRAME_RATE_INT = 50;
        FRAME_RATE_DIV = 1.2f;
    }
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    MPSettings.dronesAreBots = ShadowRand(6) == 0;
    Drone_bDisableSystem = (uchar)(ShadowRand(10) == 0);

    NPCGlobals.NDrone2List = &world.drones[0];
    NPCGlobals.dummyPlayerActive = (uchar)(ShadowRand(4) == 0);
    NPCGlobals.dummyPlayer = &world.objs[ShadowRand(DRONES)];
    NPCGlobals.maxDeadDrones = ShadowRand(4);
    NPCGlobals.deadDrones = ShadowRand(6);
    NPCGlobals.oldestDeadId = 100 + ShadowRand(3);
    NPCGlobals.nextPatrolTalkFrame = 1000 + ShadowRand(100);
    NPCGlobals.lastPowerstationChannel = 0x34 + ShadowRand(10);
    NPCGlobals.interogating = (uchar)ShadowRand(2);
    NPCGlobals.numAwarePoints = ShadowRand(8);
    for (int k = 0; k < 8; k++) {
        DroneAwarePoint *p = &NPCGlobals.awarePoints[k];
        memset(p, 0, sizeof(*p));
        p->type = (uchar)ShadowRand(4);
        p->obj = ShadowRand(2) ? &world.explosive : &world.objs[ShadowRand(DRONES)];
        RandomVector(&p->pos, 8.0f);
        p->radius = ShadowRandFloat(0, 9);
    }
    glb_players[0] = ShadowRand(8) ? &world.player : NULL;

    for (int ch = 0x34; ch < 0x54; ch++)
        switch_channels[ch] = (char)(ShadowRand(3) == 0);
    switch_channels[0x98] = (char)ShadowRand(2);
    switch_channels[0x4a] = (char)(ShadowRand(3) == 0);

    MusicVars.combatThreshold = (ushort)ShadowRand(4);
    MusicVars.seenSeconds = ShadowRand(4);
    MusicVars.closeRange = ShadowRandFloat(0, 5);
    MusicVars.combat = (ushort)ShadowRand(2);
    MusicVars.attackers = (ushort)ShadowRand(3);
    MusicVars.nearestOpponent = ShadowRandFloat(0, 6);
    MusicVars.nearestSeenOpponent = ShadowRandFloat(0, 6);
    MusicVars.lastSeenFrame = 900 + ShadowRand(400);
    MusicVars.countingStarted = ShadowRand(2);
    int counted = ShadowRand(5);
    I32_AT(XbeOurs(0x001ee418)) = counted;
    for (int k = 0; k < 100; k++)
        ((Drone_tag **)XbeOurs(0x001ee288))[k] = k < counted ? &world.drones[ShadowRand(DRONES)] : NULL;
    I32_AT(XbeOurs(0x001ee41c)) = ShadowRand(3) - 1;
}

// The drones not set up for the bots are left as non-bots
void NoBots() {
    for (int i = 0; i < DRONES; i++)
        if (world.drones[i].dtype == DTYPE_Bot)
            world.drones[i].dtype = DTYPE_Normal;
}

// ---- calling the originals

__declspec(naked) void __cdecl OriginalRouteMsgDCV(DCVars_tag *dcv, MsgObject *msg) {
    _asm {
        push esi
        mov esi, [esp + 8]
        push dword ptr [esp + 12]
        mov eax, 0x0004e410
        call eax
        add esp, 4
        pop esi
        ret
    }
}

__declspec(naked) void __cdecl OriginalHandleTalking(DCVars_tag *dcv) {
    _asm {
        push edi
        mov edi, [esp + 8]
        mov eax, 0x0003c430
        call eax
        pop edi
        ret
    }
}

// Every state machine original at once, so they reach each other rather than ours
struct OriginalStateMachine {
    XbeOriginalScope init{0x0004e1e0}, delayed{0x00064380}, route{0x0004e630}, dispatch{0x0004e410},
        queue{0x0004e3c0};
};

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

// ---- the state machine

MsgObject queued[12];
int numQueued, rounds;

void RandomMessage(MsgObject *msg) {
    msg->msgType = ShadowRand(3) == 0 ? DRONE_MSG_Update : (ShadowRand(2) ? 0x40 : ShadowRand(0x30));
    static const uint scopes[] = {0, 0, 0, DSTATE_BotGlobal};
    msg->scope = ShadowRand(4) ? scopes[ShadowRand(4)] : RandomState();
    msg->sender = 100 + ShadowRand(DRONES);
    switch (ShadowRand(5)) {
    case 0:
    case 1:
        msg->receiver = 0;
        break;
    case 2:
        msg->receiver = -1 - (int)ShadowRand(3);
        break;
    case 3:
        msg->receiver = 200;
        break;
    default:
        msg->receiver = 100 + ShadowRand(DRONES);
        break;
    }
    msg->createdFrame = GameState.NumFramesUnpaused;
    msg->handleOnFrame = GameState.NumFramesUnpaused + (ShadowRand(2) ? 0 : ShadowRand(6));
    msg->extraData = (void *)(uintptr_t)(0x5000 + ShadowRand(100));
}

void RandomBots() {
    for (int i = 0; i < 10; i++) {
        MPGame.players[i].playerObj = i >= 4 && ShadowRand(3) ? &world.objs[ShadowRand(DRONES)] : NULL;
        MPSettings.Player[i].TeamId = (MPTeam)ShadowRand(4);
    }
    world.botVars.redirectState = (ushort)(ShadowRand(2) ? 0 : RandomState());
}

uint32_t RunStateMachine(bool original) {
    // The same messages, a few frames of them, routed and then delivered from the queue
    for (int r = 0; r < rounds; r++) {
        for (int k = 0; k < numQueued; k++) {
            MsgObject msg = queued[k];
            msg.createdFrame += r;
            msg.handleOnFrame += r;
            if (original)
                ORIGINAL(0x0004e630, void(__cdecl *)(MsgObject *))(&msg);
            else
                Drone_SM_RouteMsg(&msg);
        }
        GameState.NumFramesUnpaused++;
        if (original)
            ORIGINAL(0x00064380, void(__cdecl *)(void))();
        else
            Drone_SM_SendDelayedMsgs();
    }
    return 0;
}

void TestStateMachine() {
    for (int trial = 0; trial < 50; trial++) {
        RandomWorld();
        for (size_t i = 0; i < 0x8010; i++)
            GameSpan<uint8_t>{0x001ee438}[i] = (uint8_t)ShadowRand(256);
        Compare("Drone_SM_Init", trial,
                [] {
                    OriginalStateMachine o;
                    return (uint32_t)(ORIGINAL(0x0004e1e0, uint(__cdecl *)(void))() & 0xff);
                },
                [] { return (uint32_t)Drone_SM_Init(); });
    }

    for (int trial = 0; trial < 1500; trial++) {
        RandomWorld();
        RandomBots();
        Drone_bDisableSystem = 0;
        Drone_SM_Init();
        // Some already waiting
        for (int k = ShadowRand(6); k > 0; k--) {
            MsgObject msg;
            RandomMessage(&msg);
            msg.handleOnFrame = GameState.NumFramesUnpaused + 1 + ShadowRand(5);
            Drone_SM_QueueMsg(&msg);
        }
        numQueued = 1 + ShadowRand(trial % 10 == 0 ? 12 : 4);
        for (int k = 0; k < numQueued; k++)
            RandomMessage(&queued[k]);
        rounds = 1 + ShadowRand(6);
        Compare("Drone_SM_RouteMsg", trial,
                [] {
                    OriginalStateMachine o;
                    return RunStateMachine(true);
                },
                [] { return RunStateMachine(false); });
    }

    for (int trial = 0; trial < 1500; trial++) {
        RandomWorld();
        RandomBots();
        Drone_SM_Init();
        int who = ShadowRand(DRONES);
        world.dcv.drone = &world.drones[who];
        world.dcv.gameObj = &world.objs[who];
        world.dcv.aiStateMachine = &world.drones[who].sm;
        world.drones[who].sm.changePending = (uchar)ShadowRand(2);
        world.drones[who].sm.processFunction = ShadowRand(10) ? FakeProcess : NULL;
        RandomMessage(&world.msg);
        if (ShadowRand(2))
            world.msg.scope = ShadowRand(2) ? world.drones[who].sm.curState : 0;
        Compare("Drone_SM_RouteMsgDCV", trial,
                [] {
                    OriginalStateMachine o;
                    OriginalRouteMsgDCV(&world.dcv, &world.msg);
                    return 0u;
                },
                [] {
                    Drone_SM_RouteMsgDCV(&world.dcv, &world.msg);
                    return 0u;
                });
    }
}

// ---- anims

short animState, animVariant, endState;
uint callState, callVariant, endMsg;
float speed;
char waitArg;
Drone_tag *droneArg;

void TestAnims() {
    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        NoBots();
        animState = (short)(ShadowRand(10) == 0 ? -1 - ShadowRand(5) : ShadowRand(0x7c));
        animVariant = (short)ShadowRand(5);
        Compare("DroneAnim_CanDoAnimState", trial,
                [] {
                    XbeOriginalScope o(0x000330c0);
                    return (uint32_t)(ORIGINAL(0x000330c0, uint(__cdecl *)(DCVars_tag *, short, short))(
                                          &world.dcv, animState, animVariant) & 0xff);
                },
                [] { return (uint32_t)DroneAnim_CanDoAnimState(&world.dcv, animState, animVariant); });
    }

    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        NoBots();
        Drone_tag *d = &world.drones[0];
        switch (ShadowRand(5)) {
        case 0:
            callState = 0x3d;
            break;
        case 1:
            callState = d->animState;
            break;
        case 2:
            callState = d->animCall.state;
            break;
        default:
            callState = ShadowRand(0x7c);
            break;
        }
        callVariant = ShadowRand(3) ? d->animCall.variant : (ShadowRand(2) ? 3 : ShadowRand(5));
        endState = ShadowRand(2) ? d->animEndState : (short)(ShadowRand(2) ? 0 : RandomState());
        endMsg = ShadowRand(2) ? d->animEndMsg : (ShadowRand(2) ? 0 : 0x40);
        speed = ShadowRand(2) ? d->animCallSpeed : 2.0f;
        Compare("DroneAnim_CallAnim", trial,
                [] {
                    XbeOriginalScope o(0x000341c0);
                    return (uint32_t)(ORIGINAL(0x000341c0,
                                               uint(__cdecl *)(uint, uint, short, uint, float, int, DCVars_tag *))(
                                          callState, callVariant, endState, endMsg, speed, 0, &world.dcv) & 0xff);
                },
                [] {
                    return (uint32_t)DroneAnim_CallAnim(callState, callVariant, endState, endMsg, speed, 0,
                                                        &world.dcv);
                });
    }

    for (int trial = 0; trial < 2000; trial++) {
        RandomWorld();
        NoBots();
        droneArg = ShadowRand(10) ? &world.drones[0] : NULL;
        Compare("DroneAnim_CurrentMoveState", trial,
                [] {
                    XbeOriginalScope o(0x000332b0);
                    return (uint32_t)(ORIGINAL(0x000332b0, uchar(__cdecl *)(Drone_tag *))(droneArg));
                },
                [] { return (uint32_t)DroneAnim_CurrentMoveState(droneArg); });
    }

    for (int trial = 0; trial < 4000; trial++) {
        RandomWorld();
        NoBots();
        static const uint idleStates[] = {DSTATE_Idle, DSTATE_PartyGirl, DSTATE_Patrol, DSTATE_Attack};
        world.drones[0].sm.curState = idleStates[ShadowRand(4)];
        if (ShadowRand(4) == 0)
            world.drones[0].skinClass = SKINCLASS_ROOK;
        if (ShadowRand(4) == 0)
            GameState.CurrentLevelHashcode = HT_Level_EvilBase;
        waitArg = (char)ShadowRand(2);
        Compare("DroneAnim_SetStandIdleAnim", trial,
                [] {
                    XbeOriginalScope o(0x00035310);
                    return (uint32_t)(ORIGINAL(0x00035310, uint(__cdecl *)(DCVars_tag *, char))(&world.dcv, waitArg) &
                                      0xff);
                },
                [] { return (uint32_t)DroneAnim_SetStandIdleAnim(&world.dcv, waitArg); });
        Compare("DroneAnim_SetWalkIdleAnim", trial,
                [] {
                    XbeOriginalScope o(0x00035410);
                    return (uint32_t)(ORIGINAL(0x00035410, uint(__cdecl *)(DCVars_tag *, char))(&world.dcv, waitArg) &
                                      0xff);
                },
                [] { return (uint32_t)DroneAnim_SetWalkIdleAnim(&world.dcv, waitArg); });
    }

    for (int trial = 0; trial < 4000; trial++) {
        RandomWorld();
        NoBots();
        glb_players[0] = &world.player;
        endState = (short)(ShadowRand(2) ? 0 : RandomState());
        Compare("DroneAnim_LocationDeathAnim", trial,
                [] {
                    XbeOriginalScope o(0x00034b90);
                    return (uint32_t)(ORIGINAL(0x00034b90, uint(__cdecl *)(DCVars_tag *, short))(&world.dcv, endState) &
                                      0xff);
                },
                [] { return (uint32_t)DroneAnim_LocationDeathAnim(&world.dcv, endState); });
    }
}

// ---- helpers

obj_tag *objArg;
_VECTOR *posArg;
float *yawArg;
float floatArg;
uint uintArg, uintArg2, uintArg3;
int intArg;

void PickObject() {
    switch (ShadowRand(5)) {
    case 0:
        objArg = &world.player;
        break;
    case 1:
        objArg = &world.explosive;
        break;
    default:
        objArg = &world.objs[ShadowRand(DRONES)];
        break;
    }
    posArg = ShadowRand(5) ? &world.posOut : NULL;
    yawArg = ShadowRand(2) ? &world.yawOut : NULL;
    world.posOut.x = world.posOut.y = world.posOut.z = 0.0f;
    world.yawOut = 0.0f;
}

void TestHelpers() {
    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        NoBots();
        PickObject();
        if (ShadowRand(8) == 0)
            objArg = NULL;
        Compare("Drone_AlertStatus", trial,
                [] {
                    XbeOriginalScope o(0x00031d70);
                    return (uint32_t)(uintptr_t)ORIGINAL(0x00031d70, uchar *(__cdecl *)(obj_tag *))(objArg);
                },
                [] { return (uint32_t)(uintptr_t)Drone_AlertStatus(objArg); });
        Compare("NDrone2_ObjectIsArmed", trial,
                [] {
                    XbeOriginalScope o(0x00064be0);
                    return (uint32_t)(ORIGINAL(0x00064be0, uint(__cdecl *)(obj_tag *))(objArg) & 0xff);
                },
                [] { return (uint32_t)NDrone2_ObjectIsArmed(objArg); });
        if (objArg == NULL)
            continue;
        if (posArg == NULL && objArg->objectType != OBJECTTYPE_DRONE)
            posArg = &world.posOut;   // both read it unchecked
        Compare("NDrone2_GetHeadPos", trial,
                [] {
                    XbeOriginalScope o(0x00036ba0);
                    return (uint32_t)(ORIGINAL(0x00036ba0, uint(__cdecl *)(obj_tag *, _VECTOR *, float *))(
                                          objArg, posArg, yawArg) & 0xff);
                },
                [] { return (uint32_t)NDrone2_GetHeadPos(objArg, posArg, yawArg); });
        Compare("NDrone2_GetHeadPosOld", trial,
                [] {
                    XbeOriginalScope o(0x00036c80);
                    return (uint32_t)(ORIGINAL(0x00036c80, uint(__cdecl *)(obj_tag *, _VECTOR *, float *))(
                                          objArg, posArg, yawArg) & 0xff);
                },
                [] { return (uint32_t)NDrone2_GetHeadPosOld(objArg, posArg, yawArg); });
        uintArg = ShadowRand(30);
        Compare("DroneFunc_GetBonePos", trial,
                [] {
                    XbeOriginalScope o(0x00036e00);
                    return (uint32_t)(ORIGINAL(0x00036e00, uint(__cdecl *)(obj_tag *, uint, _VECTOR *))(
                                          objArg, uintArg, posArg) & 0xff);
                },
                [] { return (uint32_t)DroneFunc_GetBonePos(objArg, uintArg, posArg); });
    }

    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        NoBots();
        floatArg = RandomFloat(0.0f, 3.5f);
        intArg = ShadowRand(4) == 0 ? -(int)ShadowRand(100) : (int)ShadowRand(600);
        PickObject();
        if (ShadowRand(4) == 0)
            objArg = NULL;
        Compare("NDrone2_NearArmedPerson", trial,
                [] {
                    XbeOriginalScope o(0x00064d40);
                    return (uint32_t)(ORIGINAL(0x00064d40, uint(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff);
                },
                [] { return (uint32_t)NDrone2_NearArmedPerson(&world.dcv); });
        Compare("NDrone2_BondWithinAngle", trial,
                [] {
                    XbeOriginalScope o(0x00043f20);
                    return (uint32_t)(ORIGINAL(0x00043f20, uint(__cdecl *)(DCVars_tag *, float))(&world.dcv, floatArg) &
                                      0xff);
                },
                [] { return (uint32_t)NDrone2_BondWithinAngle(&world.dcv, floatArg); });
        if (world.objs[0].animState == NULL)
            world.objs[0].animState = &world.anims[0];
        Compare("NDrone2_HeadTrackObj", trial,
                [] {
                    XbeOriginalScope o(0x0003a6c0);
                    return (uint32_t)(ORIGINAL(0x0003a6c0, uint(__cdecl *)(obj_tag *, int, float, DCVars_tag *))(
                                          objArg, intArg, floatArg, &world.dcv) & 0xff);
                },
                [] { return (uint32_t)NDrone2_HeadTrackObj(objArg, intArg, floatArg, &world.dcv); });
    }

    for (int trial = 0; trial < 2000; trial++) {
        RandomWorld();
        NoBots();
        static const uint msgs[] = {DRONE_MSG_ImpactStunGrenade, DRONE_MSG_ImpactStunGrenade, 0x40, 0x15};
        uintArg = msgs[ShadowRand(4)];
        PickObject();
        floatArg = ShadowRandFloat(0, 12);
        uintArg2 = ShadowRand(3) ? 0 : 3;
        Compare("Drone_MessageObjVicinity", trial,
                [] {
                    XbeOriginalScope o(0x00032970);
                    ORIGINAL(0x00032970, void(__cdecl *)(uint, obj_tag *, float, void *, uint))(
                        uintArg, objArg, floatArg, (void *)0x777, uintArg2);
                    return 0u;
                },
                [] {
                    Drone_MessageObjVicinity(uintArg, objArg, floatArg, (void *)0x777, uintArg2);
                    return 0u;
                });
        objArg = &world.objs[ShadowRand(DRONES)];
        if (ShadowRand(4) == 0)
            objArg->extraObjectData = NULL;
        Compare("Drone_FellOutMap", trial,
                [] {
                    XbeOriginalScope o(0x00031970);
                    ORIGINAL(0x00031970, void(__cdecl *)(obj_tag *))(objArg);
                    return 0u;
                },
                [] {
                    Drone_FellOutMap(objArg);
                    return 0u;
                });
    }
}

// ---- talk

void TestTalk() {
    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        NoBots();
        Compare("Drone_IsTalking", trial,
                [] {
                    XbeOriginalScope o(0x00036200);
                    return (uint32_t)(ORIGINAL(0x00036200, uint(__cdecl *)(Drone_tag *))(&world.drones[0]) & 0xff);
                },
                [] { return (uint32_t)Drone_IsTalking(&world.drones[0]); });
        if (ShadowRand(3) == 0)
            GameState.CurrentLevelHashcode = ShadowRand(2) ? HT_Level_PowerStationA1 : HT_Level_PowerStationA2;
        Compare("NDrone2_DeathTalk", trial,
                [] {
                    XbeOriginalScope o(0x0003be90);
                    return (uint32_t)(ORIGINAL(0x0003be90, uint(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff);
                },
                [] { return (uint32_t)NDrone2_DeathTalk(&world.dcv); });
    }

    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        NoBots();
        uintArg = 0x80 + ShadowRand(20);
        intArg = ShadowRand(300);
        uintArg2 = ShadowRand(600);
        waitArg = (char)ShadowRand(2);
        Compare("DroneFunc_DoBondTalk", trial,
                [] {
                    XbeOriginalScope o(0x0003ce10);
                    return (uint32_t)(ORIGINAL(0x0003ce10, uint(__cdecl *)(DCVars_tag *, uint, int, uint, char))(
                                          &world.dcv, uintArg, intArg, uintArg2, waitArg) & 0xff);
                },
                [] {
                    return (uint32_t)DroneFunc_DoBondTalk(&world.dcv, uintArg, intArg, uintArg2, waitArg);
                });
    }

    // Aimed at the guests: mostly in CastleIndoors1, close, facing, unarmed and drawn, so each skin class talks
    for (int trial = 0; trial < 12000; trial++) {
        RandomWorld();
        NoBots();
        if (ShadowRand(8)) {
            GameState.CurrentLevelHashcode = HT_Level_CastleIndoors1;
            world.objs[0].drawnCountdown = 1;
            world.drones[0].distanceToTarget = ShadowRandFloat(0, 3.2f);
            world.drones[0].opponentBearing = ShadowRandFloat(-1.7f, 1.7f);
            world.anims[0].currentWeaponId = 0;
            static const short classes[] = {9, 13, 14, 16, 17, 18, 18, 13, 10};
            world.drones[0].skinClass = classes[ShadowRand(9)];
            NPCGlobals.dummyPlayerActive = 0;
            glb_players[0] = &world.player;
            if (world.objs[0].animState == NULL)
                world.objs[0].animState = &world.anims[0];
        }
        Compare("NDrone2_BondTalk", trial,
                [] {
                    XbeOriginalScope o(0x0003cf50);
                    return (uint32_t)(ORIGINAL(0x0003cf50, uint(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff);
                },
                [] { return (uint32_t)NDrone2_BondTalk(&world.dcv); });
    }

    for (int trial = 0; trial < 2000; trial++) {
        RandomWorld();
        NoBots();
        for (int i = 0; i < DRONES; i++)
            if (ShadowRand(2))
                world.drones[i].dtype = DTYPE_Mayhew;
        if (ShadowRand(6) == 0)
            world.drones[ShadowRand(DRONES)].gameObj = NULL;
        uintArg = 0x9a + ShadowRand(5);
        Compare("Drone_MayhewTalk", trial,
                [] {
                    XbeOriginalScope o(0x0003c380);
                    ORIGINAL(0x0003c380, void(__cdecl *)(uint))(uintArg);
                    return 0u;
                },
                [] {
                    Drone_MayhewTalk(uintArg);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 4000; trial++) {
        RandomWorld();
        NoBots();
        if (ShadowRand(2))
            world.drones[0].skinClass = SKINCLASS_BLACK_OPS;
        Compare("NDrone2_PatrolTalk", trial,
                [] {
                    XbeOriginalScope o(0x0003b9b0);
                    return (uint32_t)(ORIGINAL(0x0003b9b0, uint(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff);
                },
                [] { return (uint32_t)NDrone2_PatrolTalk(&world.dcv); });
        if (ShadowRand(3))
            GameState.CurrentLevelHashcode = ShadowRand(2) ? HT_Level_PowerStationA1 : HT_Level_PowerStationA2;
        Compare("NDrone2_PowerstationTalk", trial,
                [] {
                    XbeOriginalScope o(0x0003b880);
                    return (uint32_t)(ORIGINAL(0x0003b880, uint(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff);
                },
                [] { return (uint32_t)NDrone2_PowerstationTalk(&world.dcv); });
        if (ShadowRand(3) == 0) {
            GameState.CurrentLevelHashcode = HT_Level_CastleIndoors2;
            world.drones[0].skinClass = SKINCLASS_ZOE;
            NPCGlobals.dummyPlayerActive = 0;
            glb_players[0] = &world.player;
            world.player.position = world.objs[0].position;
            world.player.position.x += ShadowRandFloat(-3.5f, 3.5f);
        }
        Compare("NDrone2_HandleTalking", trial,
                [] {
                    XbeOriginalScope o(0x0003c430);
                    OriginalHandleTalking(&world.dcv);
                    return 0u;
                },
                [] {
                    NDrone2_HandleTalking(&world.dcv);
                    return 0u;
                });
    }
}

// ---- death

DroneImpact_tag *impactArg;

void TestDeath() {
    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        NoBots();
        uintArg = ShadowRand(20);
        Compare("DroneFunc_SetMissionFailReason", trial,
                [] {
                    XbeOriginalScope o(0x00035fb0);
                    ORIGINAL(0x00035fb0, void(__cdecl *)(Drone_tag *, uint))(&world.drones[0], uintArg);
                    return 0u;
                },
                [] {
                    DroneFunc_SetMissionFailReason(&world.drones[0], uintArg);
                    return 0u;
                });
        Compare("NDrone2_SetAsDead", trial,
                [] {
                    XbeOriginalScope o(0x00038df0);
                    ORIGINAL(0x00038df0, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    NDrone2_SetAsDead(&world.dcv);
                    return 0u;
                });
        Compare("NDrone2_FadeOut", trial,
                [] {
                    XbeOriginalScope o(0x00038d30);
                    return (uint32_t)(ORIGINAL(0x00038d30, uint(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff);
                },
                [] { return (uint32_t)NDrone2_FadeOut(&world.dcv); });
        Compare("DroneFunc_DeadDrone", trial,
                [] {
                    XbeOriginalScope o(0x00038db0);
                    return (uint32_t)(ORIGINAL(0x00038db0, uint(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff);
                },
                [] { return (uint32_t)DroneFunc_DeadDrone(&world.dcv); });
        Compare("DroneFunc_SetDeathChannel", trial,
                [] {
                    XbeOriginalScope o(0x00036090);
                    return (uint32_t)(ORIGINAL(0x00036090, uint(__cdecl *)(Drone_tag *))(&world.drones[0]) & 0xff);
                },
                [] { return (uint32_t)DroneFunc_SetDeathChannel(&world.drones[0]); });
    }

    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        NoBots();
        // The properties OnInitDeath tests, set often
        behaviour_util_setProperty(DRONE_PROP_DEATH_FAILS_MISSION, world.drones[0].currentBehaviour, ShadowRand(2));
        behaviour_util_setProperty(DRONE_PROP_PLAYER_KILL_FAILS_MISSION, world.drones[0].currentBehaviour, ShadowRand(2));
        Compare("DroneFunc_OnInitDeath", trial,
                [] {
                    XbeOriginalScope o(0x0003de00);
                    ORIGINAL(0x0003de00, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneFunc_OnInitDeath(&world.dcv);
                    return 0u;
                });
        impactArg = ShadowRand(6) ? &world.impact : NULL;
        Compare("NDrone2_ExplosionDir", trial,
                [] {
                    XbeOriginalScope o(0x0003a2b0);
                    return ORIGINAL(0x0003a2b0, uint(__cdecl *)(DCVars_tag *, DroneImpact_tag *))(&world.dcv,
                                                                                                 impactArg);
                },
                [] { return NDrone2_ExplosionDir(&world.dcv, impactArg); });
    }
}

// ---- attack and explosives

short nextStateArg;

void TestExplosives() {
    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        NoBots();
        if (ShadowRand(3) == 0)
            GameState.CurrentLevelHashcode = HT_Level_CastleIndoors1;
        Compare("DroneFunc_DoForcedAttack", trial,
                [] {
                    XbeOriginalScope o(0x0003b5d0);
                    return (uint32_t)(ORIGINAL(0x0003b5d0, uint(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff);
                },
                [] { return (uint32_t)DroneFunc_DoForcedAttack(&world.dcv); });
    }

    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        NoBots();
        world.drones[0].explosiveSeen = (uchar)(ShadowRand(6) == 0);
        static const short nexts[] = {0, 0, 0, DSTATE_RunAwayFromObject, DSTATE_HideFromScaryObject, DSTATE_Attack,
                                      DSTATE_Idle, -3};
        nextStateArg = nexts[ShadowRand(8)];
        Compare("DroneFunc_ConsiderExplosive", trial,
                [] {
                    XbeOriginalScope o(0x00038670);
                    ORIGINAL(0x00038670, void(__cdecl *)(DCVars_tag *, obj_tag *, short))(&world.dcv, &world.explosive,
                                                                                         nextStateArg);
                    return 0u;
                },
                [] {
                    DroneFunc_ConsiderExplosive(&world.dcv, &world.explosive, nextStateArg);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 4000; trial++) {
        RandomWorld();
        NoBots();
        world.drones[0].explosiveSeen = (uchar)(ShadowRand(6) == 0);
        Compare("DroneFunc_HandleExplosives", trial,
                [] {
                    XbeOriginalScope o(0x00038a90);
                    ORIGINAL(0x00038a90, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneFunc_HandleExplosives(&world.dcv);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 4000; trial++) {
        RandomWorld();
        NoBots();
        if (ShadowRand(4) == 0)
            GameState.CurrentLevelHashcode = HT_Level_PowerStationA1;
        Compare("NDrone2_SeenAndAttacking", trial,
                [] {
                    XbeOriginalScope o(0x00039f60);
                    ORIGINAL(0x00039f60, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    NDrone2_SeenAndAttacking(&world.dcv);
                    return 0u;
                });
    }
}

} // namespace

void DroneAIShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], npc[sizeof(NPCGlobals_t)], lists[0x8010], music[0x1c0];
    static uint8_t mpSettings[sizeof(MPSettings_t)], mpGame[sizeof(MPGameStruct)];
    static char channels[256];
    static uint32_t times[256], rand[2];
    static obj_tag *players[4];
    uint32_t frameRate = FRAME_RATE_INT;
    float frameDiv = FRAME_RATE_DIV;
    uchar disabled = Drone_bDisableSystem;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(lists, (void *)(size_t)XbeOurs(0x001ee438), sizeof(lists));
    memcpy(music, (void *)XbeOurs(0x001ee260), sizeof(music));
    memcpy(mpSettings, &MPSettings, sizeof(mpSettings));
    memcpy(mpGame, &MPGame, sizeof(mpGame));
    memcpy(channels, switch_channels, sizeof(channels));
    memcpy(times, switch_channels_time, sizeof(times));
    XbeOursRead(rand, RandWords.at, sizeof(rand));
    memcpy(players, glb_players, sizeof(players));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add((void *)0x001ee438, sizeof(lists));
    regions.Add((void *)XbeOurs(0x001ee260), sizeof(music));
    regions.Add(&MPSettings, sizeof(MPSettings_t));
    regions.Add(&MPGame, sizeof(MPGameStruct));
    regions.Add(switch_channels, 256);
    regions.Add(switch_channels_time, 1024);
    regions.Add(RandWords.game(), 8);
    regions.Add(glb_players, sizeof(players));
    regions.Add(&FRAME_RATE_INT, 4);
    regions.Add(&FRAME_RATE_DIV, 4);
    regions.Add(&Drone_bDisableSystem, 1);

    InstallFakes();
    NfWarnMuted++;
    cases = differ = 0;

    TestStateMachine();
    int smCases = cases, smDiffer = differ;
    TestAnims();
    TestHelpers();
    TestTalk();
    TestDeath();
    TestExplosives();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy((void *)(size_t)XbeOurs(0x001ee438), lists, sizeof(lists));
    memcpy((void *)XbeOurs(0x001ee260), music, sizeof(music));
    memcpy(&MPSettings, mpSettings, sizeof(mpSettings));
    memcpy(&MPGame, mpGame, sizeof(mpGame));
    memcpy(switch_channels, channels, sizeof(channels));
    memcpy(switch_channels_time, times, sizeof(times));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
    memcpy(glb_players, players, sizeof(players));
    FRAME_RATE_INT = frameRate;
    FRAME_RATE_DIV = frameDiv;
    Drone_bDisableSystem = disabled;

    printf("[droneai] Drone_SM_*: %d cases, %d differ\n", smCases, smDiffer);
    printf("[droneai] DroneAnim_*, talk, death, explosives, helpers: %d cases, %d differ\n", cases - smCases,
           differ - smDiffer);
}
