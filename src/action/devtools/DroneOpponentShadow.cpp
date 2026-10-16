// Shadow tests for the drones' opponents (game/drone/NDrone2_Opponent.cpp, and Drone_BuildDynamicAwarePoints,
// Drone_ProcessOpponents and Drone_UpdateDummyPlayer in Drone.cpp): each original and ours run on the same made-up
// drones, players and objects, with what needs a level going to logging fakes. Run at start with MenuShadowTests=on
// (MenuProbe.cpp), before any level exists.
//
// Faked, so not tested here: the lines of sight (NDrone2_CanSeeObject, which also counts NPCGlobals.visionTests when
// the fake is told to), the skeleton (AnimGetBoneWorldTrans, NDrone2_GetHeadPos), the AI volumes
// (Drone_VisibilityForPosition), the AI links (AINetwork_SetLinksFlagInCircle), the messages
// (Drone_SM_BroadcastMsg, MP_sendBotMessage) and the bot code (BOT_handleOpponentHistory,
// BOTSTATE_increaseDistraction, BOT_getAggressionMul, MP_getObjectTeam and the objectives). The rest runs for real:
// Drone_AddDynamicAwarePoint, the vector maths, behaviour_util_getProperty and Rand_Rand, whose state both sides
// start from and is compared afterwards.

#include "DroneOpponentShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../game/obj/bullet.h"
#include "../game/obj/Player.h"
#include "../game/drone/BOT.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/NDrone2_Opponent.h"
#include "../game/mp/multiplayer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords ((uint32_t *)0x0018cdf8)

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int DRONES = 4;
const int COPTERS = 3;
const int BULLETS = 40;
const int WEAPONS = 24;

struct World {
    obj_tag objs[DRONES];
    Drone_tag drones[DRONES];
    AnimState anims[DRONES];
    BOT_vars_t bots[DRONES];
    obj_tag player;
    BLData playerData;
    AnimState playerAnim;
    obj_tag dummy;
    obj_tag copters[COPTERS];
    obj_tag goal;
    obj_tag gfx;
    obj_tag bullets[BULLETS];
    BU_tag bulletData[BULLETS];
    weapon_definition_tag weapons[WEAPONS];
    uint8_t cel[0x40];
    DCVars_tag dcv;
    _VECTOR vec;
    _VECTOR rot;

    // What the fakes answer
    uint32_t seeMask;
    int seeCounts;
    int okToUpdate;
    int team;
    int goalAvailable;
    double aggression;
    float visibility;
};
World world;

ShadowRegions regions;
int cases, differ;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "droneopponent", what, trial, theirs, ours))
        differ++;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

float FromBits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

int Who(const Drone_tag *drone) {
    for (int i = 0; i < DRONES; i++)
        if (drone == &world.drones[i])
            return i;
    return drone == NULL ? -1 : 99;
}

int WhoObj(const obj_tag *obj) {
    for (int i = 0; i < DRONES; i++)
        if (obj == &world.objs[i])
            return i;
    for (int i = 0; i < COPTERS; i++)
        if (obj == &world.copters[i])
            return 10 + i;
    for (int i = 0; i < BULLETS; i++)
        if (obj == &world.bullets[i])
            return 20 + i;
    if (obj == &world.player)
        return 100;
    if (obj == &world.dummy)
        return 101;
    if (obj == &world.goal)
        return 102;
    if (obj == &world.gfx)
        return 103;
    return obj == NULL ? -1 : 999;
}

void LogVector(const char *name, const _VECTOR *v) {
    ShadowLog("%s=%08x,%08x,%08x", name, Bits(v->x), Bits(v->y), Bits(v->z));
}

// ---- the fakes

bool __cdecl FakeCanSee(Drone_tag *drone, obj_tag *obj, uint bone, uint mask) {
    ShadowLog("cansee(%d,%d,%u,%u)", Who(drone), WhoObj(obj), bone, mask);
    if (world.seeCounts)
        NPCGlobals.visionTests++;
    int bit = (Who(drone) * 7 + WhoObj(obj) + (int)bone) & 31;
    return (world.seeMask >> bit) & 1;
}

void __cdecl FakeBone(obj_tag *obj, uint which, int datum, _VECTOR *pos, _MATRIX *mtx) {
    ShadowLog("bone(%d,%x,%d,%d,%d)", WhoObj(obj), which, datum, pos != NULL, mtx != NULL);
    if (pos == NULL)
        return;
    pos->x = obj->position.x + (float)(which & 0xff) * 0.125f;
    pos->y = obj->position.y + 1.25f;
    pos->z = obj->position.z - 0.5f;
}

bool __cdecl FakeHeadPos(obj_tag *obj, _VECTOR *pos, float *yaw) {
    ShadowLog("headpos(%d,%d,%d)", WhoObj(obj), pos != NULL, yaw != NULL);
    if (pos != NULL) {
        pos->x = obj->position.x;
        pos->y = obj->position.y + 1.625f;
        pos->z = obj->position.z + 0.0625f;
    }
    if (yaw != NULL)
        *yaw = obj->rotation.y;
    return obj->objectType == OBJECTTYPE_DRONE;
}

void __cdecl FakeBotMessage(obj_tag *obj, uint msg, uintptr_t param, uint param2) {
    if (msg == 0x45)
        ShadowLog("botmsg(%d,%x,obj %d,%u)", WhoObj(obj), msg, WhoObj((obj_tag *)param), param2);
    else
        ShadowLog("botmsg(%d,%x,%u,%u)", WhoObj(obj), msg, (uint)param, param2);
}

obj_tag *__cdecl FakeOpponentHistory(Drone_tag *drone, obj_tag *obj) {
    ShadowLog("history(%d,%d)", Who(drone), WhoObj(obj));
    return obj;
}

void __cdecl FakeDistraction(Drone_tag *drone, float amount) {
    ShadowLog("distraction(%d,%08x)", Who(drone), Bits(amount));
}

obj_tag *__cdecl FakeProtection(void) {
    ShadowLog("protection");
    return world.goalAvailable ? &world.goal : NULL;
}

obj_tag *__cdecl FakeDemolition(void) {
    ShadowLog("demolition");
    return world.goalAvailable ? &world.goal : NULL;
}

MPTeam __cdecl FakeTeam(obj_tag *obj) {
    ShadowLog("team(%d)", WhoObj(obj));
    return (MPTeam)world.team;
}

double __cdecl FakeAggression(Drone_tag *drone) {
    ShadowLog("aggression(%d)", Who(drone));
    return world.aggression;
}

bool __cdecl FakeOkToUpdate(void) {
    ShadowLog("oktoupdate");
    return world.okToUpdate != 0;
}

void __cdecl FakeBroadcast(uint msg, void *extra, uint delay, uint sender) {
    ShadowLog("broadcast(%x,%d,%u,%u)", msg, extra == &NPCGlobals.alert, delay, sender);
}

void __cdecl FakeSetLinks(_VECTOR *centre, cel_tag *cel, float radius, ushort set, ushort clear) {
    LogVector("links", centre);
    ShadowLog("(%d,%08x,%x,%x)", cel == (cel_tag *)world.cel, Bits(radius), set, clear);
}

float __cdecl FakeVisibility(_VECTOR *pos, ushort type) {
    LogVector("visibility", pos);
    ShadowLog("(%u)", type);
    return world.visibility + pos->x * 0.0625f;
}

void InstallFakes() {
    ShadowFakes_Install();
    ShadowHook_Install(0x000648b0, (const void *)&FakeCanSee);
    ShadowHook_Install(0x00013070, (const void *)&FakeBone);
    ShadowHook_Install(0x00036ba0, (const void *)&FakeHeadPos);
    ShadowHook_Install(0x0009e3e0, (const void *)&FakeBotMessage);
    ShadowHook_Install(0x0001a990, (const void *)&FakeOpponentHistory);
    ShadowHook_Install(0x0001c6f0, (const void *)&FakeDistraction);
    ShadowHook_Install(0x000a1600, (const void *)&FakeProtection);
    ShadowHook_Install(0x000a1610, (const void *)&FakeDemolition);
    ShadowHook_Install(0x0009df90, (const void *)&FakeTeam);
    ShadowHook_Install(0x0001ab70, (const void *)&FakeAggression);
    ShadowHook_Install(0x000b04d0, (const void *)&FakeOkToUpdate);
    ShadowHook_Install(0x0004e900, (const void *)&FakeBroadcast);
    ShadowHook_Install(0x0004a620, (const void *)&FakeSetLinks);
    ShadowHook_Install(0x00030470, (const void *)&FakeVisibility);
}

// Every original of this group at once, so they reach each other rather than ours
struct OriginalOpponents {
    XbeOriginalScope allocate{0x00037790}, seen{0x00037820}, setOpponent{0x00037a10}, aimPos{0x000378f0},
        bonePos{0x00037870}, angles{0x00037bc0}, find{0x00037c10}, setupAlert{0x00044e30},
        alertToObject{0x00038f40}, alertToPosition{0x00038ea0}, awarePoints{0x00030e70},
        processOpponents{0x000313e0}, dummyPlayer{0x00030950}, clearTargets{0x00031110},
        updateTargets{0x00031190};
};

// ---- the made-up world

const uint32_t kNaN = 0x7fc00000;
const uint32_t kSignallingNaN = 0x7f800001;

float RandomFloat(float lo, float hi) {
    switch (ShadowRand(24)) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    case 2:
        return FromBits(kNaN);
    default:
        return ShadowRandFloat(lo, hi);
    }
}

void RandomVector(_VECTOR *v, float range) {
    v->x = RandomFloat(-range, range);
    v->y = ShadowRandFloat(-range / 4, range / 4);
    v->z = RandomFloat(-range, range);
}

uint32_t RandomWord() {
    return ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

// One of the made-up objects a drone may have as its opponent, or NULL
obj_tag *RandomOpponent() {
    switch (ShadowRand(9)) {
    case 0:
        return NULL;
    case 1:
    case 2:
        return &world.player;
    case 3:
        return &world.dummy;
    case 4:
        return &world.copters[ShadowRand(COPTERS)];
    case 5:
        return &world.goal;
    case 6:
        return &world.gfx;
    default:
        return &world.objs[ShadowRand(DRONES)];
    }
}

void RandomBot(BOT_vars_t *bot) {
    memset(bot, 0, sizeof(*bot));
    bot->preferredOpponent = ShadowRand(3) == 0 ? 0xff : (uchar)ShadowRand(10);
    bot->stats.traitFlags = (uchar)ShadowRand(16);
    static const uchar personalities[] = {0, 2, 3, 5};
    bot->stats.personality = personalities[ShadowRand(4)];
    bot->botIndex = (short)ShadowRand(10);
    bot->atObjective = (uchar)(ShadowRand(5) == 0);
    bot->beingGuarded = (uchar)(ShadowRand(4) == 0);
    static const float facings[] = {-10.0f, 10.0f, -9.999999f, 9.999999f, 45.0f, -45.0f, 0.0f};
    for (int i = 0; i < 10; i++) {
        BOT_playerInfo_t *info = &bot->players[i];
        info->flags = (uchar)ShadowRand(16);
        if (ShadowRand(2))
            info->flags |= BOT_PLAYER_PRESENT;
        info->distanceSq = RandomFloat(0.0f, 2500.0f);
        info->facing = ShadowRand(3) == 0 ? facings[ShadowRand(7)] : RandomFloat(-40.0f, 40.0f);
        info->activeTime = ShadowRandFloat(0.0f, 100.0f);
    }
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    AnimState *anim = &world.anims[i];
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    memset(anim, 0, sizeof(*anim));

    anim->animObj.flags = ShadowRand(3) ? ANIMOBJ_FLAG_1 : 0;
    o->animState = ShadowRand(6) ? anim : NULL;
    o->extraObjectData = d;
    o->objectType = ShadowRand(8) ? OBJECTTYPE_DRONE : OBJECTTYPE_DEAD_DRONE;
    o->inCel = (cel_tag *)world.cel;
    RandomVector(&o->position, 20.0f);
    RandomVector(&o->lastPosition, 20.0f);
    o->rotation.x = ShadowRandFloat(-1.0f, 1.0f);
    o->rotation.y = RandomFloat(-4.0f, 4.0f);
    o->tweakA = (uchar)(ShadowRand(5) == 0);
    o->flags = (char)(ShadowRand(8) == 0);
    o->curState = (ushort)ShadowRand(5);
    o->subState = (ushort)ShadowRand(6);
    d->gameObj = ShadowRand(12) ? o : NULL;
    d->next = i + 1 < DRONES ? &world.drones[i + 1] : NULL;

    static const uchar sides[] = {1, 1, 2, 2, 3};
    d->side = sides[ShadowRand(5)];
    static const uint flagSets[] = {0x100, 0x100, 0x100100, 0x100100, 0x300, 0x500, 0, 0x100000};
    d->flags = flagSets[ShadowRand(8)];
    static const float healths[] = {-1.0f, 0.0f, -0.0f, 50.0f, 100.0f, 100.0f};
    d->health = ShadowRand(16) == 0 ? FromBits(kNaN) : healths[ShadowRand(6)];
    d->accuracy = (uchar)ShadowRand(9);
    d->opponent = RandomOpponent();
    d->targetSlot = ShadowRand(4) == 0 ? NO_TARGET_SLOT : (uchar)ShadowRand(MAX_DRONE_TARGETS);
    d->firstSeenFrame = ShadowRand(3) == 0 ? 0 : 500 + ShadowRand(400);
    static const uint unseen[] = {0, 4, 5, 6, 7, 15, 300, 1199, 1200, 1201, 2400, 0x80000000u};
    d->unseenFrames = unseen[ShadowRand(sizeof(unseen) / sizeof(unseen[0]))];
    d->sightFlags = ShadowRand(16);
    d->viewBone = (ushort)ShadowRand(8);
    static const float views[] = {0.0f, 0.5f, 1.0471976f, 1.5707964f, 3.1415927f};
    d->viewAngle = ShadowRand(5) == 0 ? RandomFloat(0.0f, 3.2f) : views[ShadowRand(5)];
    d->viewRange = RandomFloat(5.0f, 60.0f);
    d->distanceToTarget = RandomFloat(0.0f, 40.0f);
    RandomVector(&d->aimOffset, 1.0f);
    RandomVector(&d->targetPos, 10.0f);
    RandomVector(&d->opponentSeenPos, 10.0f);
    d->positionVisibility = ShadowRandFloat(0.0f, 1.0f);
    d->opponentVisibilityScale = ShadowRandFloat(0.0f, 1.0f);
    for (int w = 0; w < 3; w++)
        d->behaviour1[w] = RandomWord();
    d->currentBehaviour = d->behaviour1;
    d->sm.id = 100 + i;
    RandomBot(&world.bots[i]);
    d->botVars = &world.bots[i];
}

void RandomObject(obj_tag *o, char type) {
    memset(o, 0, sizeof(*o));
    o->objectType = type;
    o->inCel = (cel_tag *)world.cel;
    RandomVector(&o->position, 20.0f);
    RandomVector(&o->lastPosition, 20.0f);
    RandomVector(&o->rotation, 3.0f);
    o->curState = (ushort)ShadowRand(6);
    o->subState = (ushort)ShadowRand(3);
}

// The weapons Drone_BuildDynamicAwarePoints singles out, and their neighbours
const short kWeapons[WEAPONS] = {0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x33, 0x34, 0x35, 0x36,
                                 0x37, 0x38, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x6c, 0x6d};

void RandomBullets() {
    for (int i = 0; i < WEAPONS; i++) {
        memset(&world.weapons[i], 0, sizeof(world.weapons[i]));
        world.weapons[i].weaponVariantNum = kWeapons[i];
    }
    static const float fuses[] = {0.0f, 179.0f, 179.99998f, 180.0f, 181.0f, 400.0f, -1.0f};
    for (int i = 0; i < BULLETS; i++) {
        obj_tag *o = &world.bullets[i];
        BU_tag *bullet = &world.bulletData[i];
        RandomObject(o, ShadowRand(3) ? OBJECTTYPE_BULLET : OBJECTTYPE_OCCLUDE);
        if (ShadowRand(10) == 0)
            o->objectType = OBJECTTYPE_EXPLODE;
        memset(bullet, 0, sizeof(*bullet));
        o->extraObjectData = ShadowRand(15) ? bullet : NULL;
        bullet->wpnDef = ShadowRand(15) ? &world.weapons[ShadowRand(WEAPONS)] : NULL;
        bullet->fuseTimer = ShadowRand(12) == 0 ? FromBits(kNaN) : fuses[ShadowRand(7)];
        o->curState = (ushort)(ShadowRand(3) == 0 ? 3 : ShadowRand(5));
    }
}

// control_first_object's list: the copters, the drones, the player, the others, and as many bullets as asked,
// in a made-up order
void BuildObjectList(int bullets) {
    obj_tag *list[DRONES + COPTERS + BULLETS + 4];
    int n = 0;
    for (int i = 0; i < COPTERS; i++)
        list[n++] = &world.copters[i];
    for (int i = 0; i < DRONES; i++)
        list[n++] = &world.objs[i];
    list[n++] = &world.player;
    list[n++] = &world.goal;
    list[n++] = &world.gfx;
    for (int i = 0; i < bullets; i++)
        list[n++] = &world.bullets[i];
    for (int i = n - 1; i > 0; i--) {
        int j = (int)ShadowRand(i + 1);
        obj_tag *t = list[i];
        list[i] = list[j];
        list[j] = t;
    }
    int kept = ShadowRand(5) == 0 ? (int)ShadowRand(n + 1) : n;
    for (int i = 0; i < kept; i++)
        list[i]->nextObject = i + 1 < kept ? list[i + 1] : NULL;
    ShadowFakes.firstObject = kept ? list[0] : NULL;
}

void RandomWorld(bool bots) {
    memset(&NPCGlobals, 0, sizeof(NPCGlobals));
    memset(&MPGame, 0, sizeof(MPGame));
    for (int i = 0; i < DRONES; i++)
        RandomDrone(i);
    NPCGlobals.NDrone2List = &world.drones[ShadowRand(5) == 0 ? 1 : 0];

    memset(&world.player, 0, sizeof(world.player));
    memset(&world.playerData, 0, sizeof(world.playerData));
    memset(&world.playerAnim, 0, sizeof(world.playerAnim));
    static const char playerTypes[] = {OBJECTTYPE_PLAYER, OBJECTTYPE_PLAYER, OBJECTTYPE_PLAYER, OBJECTTYPE_DEAD_PLAYER,
                                       OBJECTTYPE_GFX};
    world.player.objectType = playerTypes[ShadowRand(5)];
    world.player.extraObjectData = &world.playerData;
    world.player.animState = ShadowRand(4) ? &world.playerAnim : NULL;
    world.playerAnim.animObj.flags = ShadowRand(3) ? ANIMOBJ_FLAG_1 : 0;
    world.player.inCel = (cel_tag *)world.cel;
    RandomVector(&world.player.position, 20.0f);
    RandomVector(&world.player.lastPosition, 20.0f);
    world.player.rotation.y = RandomFloat(-4.0f, 4.0f);
    world.player.subState = (ushort)(ShadowRand(3) == 0 ? 4 : ShadowRand(6));
    static const float healths[] = {-1.0f, 0.0f, -0.0f, 100.0f, 100.0f};
    world.playerData.health = ShadowRand(16) == 0 ? FromBits(kNaN) : healths[ShadowRand(5)];

    RandomObject(&world.dummy, OBJECTTYPE_PLAYER);
    world.dummy.extraObjectData = &world.playerData;
    for (int i = 0; i < COPTERS; i++)
        RandomObject(&world.copters[i], OBJECTTYPE_COPTER);
    RandomObject(&world.goal, OBJECTTYPE_MPOBJECT);
    if (ShadowRand(3) == 0) {
        // Near a drone, for the objective's tests
        world.goal.position = world.objs[0].position;
        world.goal.position.x += ShadowRandFloat(-4.5f, 4.5f);
        world.goal.position.z += ShadowRandFloat(-4.5f, 4.5f);
    }
    RandomObject(&world.gfx, OBJECTTYPE_GFX);
    RandomBullets();
    BuildObjectList(ShadowRand(3) == 0 ? BULLETS : (int)ShadowRand(6));

    glb_players[0] = &world.player;
    NPCGlobals.dummyPlayerActive = (uchar)ShadowRand(3);
    NPCGlobals.dummyPlayer = ShadowRand(4) ? &world.dummy : NULL;
    NPCGlobals.visionTests = (ushort)(ShadowRand(4) == 0 ? 0xffff : ShadowRand(100));
    for (int i = 0; i < MAX_DRONE_TARGETS; i++) {
        DroneTarget_tag *target = &NPCGlobals.targets[i];
        static const uint flags[] = {0, 1, 2, 3, 1, 0x101};
        target->flags = flags[ShadowRand(6)];
        target->obj = ShadowRand(4) ? RandomOpponent() : NULL;
        target->seenFrame = ShadowRand(2000);
        RandomVector(&target->pos, 10.0f);
    }
    for (int i = 0; i < 10; i++) {
        // Players 4 to 9 are bots: drones with bot state
        if (i < 4)
            MPGame.players[i].playerObj = ShadowRand(3) == 0 ? NULL : RandomOpponent();
        else
            MPGame.players[i].playerObj = ShadowRand(3) == 0 ? NULL : &world.objs[ShadowRand(DRONES)];
        MPGame.players[i].unknown26 = (ushort)(ShadowRand(3) == 0 ? ShadowRand(0x40) : 0);
    }

    world.dcv.drone = &world.drones[0];
    world.dcv.gameObj = &world.objs[0];
    world.dcv.cel = (cel_tag *)world.cel;
    world.dcv.aiStateMachine = &world.drones[0].sm;
    world.drones[0].gameObj = &world.objs[0];
    RandomVector(&world.vec, 10.0f);

    world.seeMask = RandomWord();
    if (ShadowRand(4) == 0)
        world.seeMask = 0;
    world.seeCounts = ShadowRand(2);
    world.okToUpdate = ShadowRand(5) != 0;
    world.team = (int)ShadowRand(3);
    world.goalAvailable = ShadowRand(4) != 0;
    static const double aggressions[] = {1.0, 0.5, 1.000000001, 2.0, 0.0};
    world.aggression = aggressions[ShadowRand(5)];
    world.visibility = ShadowRandFloat(0.0f, 1.0f);

    GameState.NumFramesUnpaused = 1000 + ShadowRand(100);
    if (ShadowRand(2)) {
        _FRAME_RATE = 60.0f;
        FRAME_RATE_DIV = 1.0f;
    } else {
        _FRAME_RATE = 50.0f;
        FRAME_RATE_DIV = 1.2f;
    }
    RandWords[0] = RandomWord();
    RandWords[1] = RandomWord();
    MPSettings.maybeDroneAIEnabled = bots;
    static const MultiplayerGameMode modes[] = {GM_ARENA, GM_PROTECTION, GM_DEMOLITION, GM_CTF};
    MPSettings.GameMode = modes[ShadowRand(4)];
}

// ---- the cases

Drone_tag *droneArg;
obj_tag *objArg;
_VECTOR *vecArg;
char charArg;
uint uintArg;
float floatArgs[3];

void TestTargets() {
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld(ShadowRand(5) == 0);
        droneArg = &world.drones[ShadowRand(DRONES)];
        objArg = RandomOpponent();
        Compare("DroneFunc_AllocateTargetID", trial,
                [] {
                    OriginalOpponents o;
                    return ORIGINAL(0x00037790, uint32_t(__cdecl *)(Drone_tag *, obj_tag *))(droneArg, objArg) & 0xff;
                },
                [] { return (uint32_t)DroneFunc_AllocateTargetID(droneArg, objArg); });
        Compare("DroneFunc_TargetSeen", trial,
                [] {
                    OriginalOpponents o;
                    ORIGINAL(0x00037820, void(__cdecl *)(Drone_tag *, _VECTOR *))(droneArg, &world.vec);
                    return 0u;
                },
                [] {
                    DroneFunc_TargetSeen(droneArg, &world.vec);
                    return 0u;
                });
        Compare("DroneFunc_UpdateTargets", trial,
                [] {
                    OriginalOpponents o;
                    ORIGINAL(0x00031190, void(__cdecl *)(void))();
                    return 0u;
                },
                [] {
                    DroneFunc_UpdateTargets();
                    return 0u;
                });
        Compare("DroneFunc_ClearTargets", trial,
                [] {
                    OriginalOpponents o;
                    ORIGINAL(0x00031110, void(__cdecl *)(void))();
                    return 0u;
                },
                [] {
                    DroneFunc_ClearTargets();
                    return 0u;
                });
    }
}

void TestSetOpponent() {
    for (int trial = 0; trial < 12000; trial++) {
        RandomWorld(ShadowRand(3) == 0);
        droneArg = &world.drones[ShadowRand(DRONES)];
        objArg = RandomOpponent();
        if (ShadowRand(3) == 0)
            objArg = droneArg->opponent;   // the same again, for the last-seen position
        Compare("NDrone2_SetOpponent", trial,
                [] {
                    OriginalOpponents o;
                    ORIGINAL(0x00037a10, void(__cdecl *)(Drone_tag *, obj_tag *))(droneArg, objArg);
                    return 0u;
                },
                [] {
                    NDrone2_SetOpponent(droneArg, objArg);
                    return 0u;
                });
        Compare("NDrone2_SetOpponentAimPos", trial,
                [] {
                    OriginalOpponents o;
                    ORIGINAL(0x000378f0, void(__cdecl *)(Drone_tag *))(droneArg);
                    return 0u;
                },
                [] {
                    NDrone2_SetOpponentAimPos(droneArg);
                    return 0u;
                });
        if (objArg != NULL) {
            uintArg = ShadowRand(3) ? ShadowRand(0x40) : 0x7fffffff;
            vecArg = ShadowRand(5) ? &world.vec : NULL;
            Compare("NDrone2_OpponentBonePos", trial,
                    [] {
                        OriginalOpponents o;
                        return ORIGINAL(0x00037870, uint32_t(__cdecl *)(obj_tag *, uint, _VECTOR *))(objArg, uintArg,
                                                                                                    vecArg) & 0xff;
                    },
                    [] { return (uint32_t)NDrone2_OpponentBonePos(objArg, uintArg, vecArg); });
            vecArg = ShadowRand(6) ? &world.rot : NULL;
            Compare("NDrone2_AnglesToObj", trial,
                    [] {
                        OriginalOpponents o;
                        return ORIGINAL(0x00037bc0, uint32_t(__cdecl *)(Drone_tag *, obj_tag *, _VECTOR *))(
                                   &world.drones[0], objArg, vecArg) & 0xff;
                    },
                    [] { return (uint32_t)NDrone2_AnglesToObj(&world.drones[0], objArg, vecArg); });
        }
    }
}

uint32_t OriginalFindOpponent() {
    OriginalOpponents o;
    return ORIGINAL(0x00037c10, uint32_t(__cdecl *)(Drone_tag *))(droneArg);
}

uint32_t OurFindOpponent() {
    return (uint32_t)(uintptr_t)NDrone2_FindOpponent(droneArg);
}

void TestFindOpponent() {
    for (int trial = 0; trial < 20000; trial++) {
        RandomWorld(false);
        droneArg = &world.drones[ShadowRand(DRONES)];
        if (ShadowRand(3) == 0)
            droneArg->side = 2;
        if (ShadowRand(3) == 0)
            droneArg->opponentCursor = &world.drones[ShadowRand(DRONES)];
        droneArg->gameObj = &world.objs[Who(droneArg)];
        if (ShadowRand(3) == 0) {
            // A helicopter at the edges of an ally's pitches
            obj_tag *copter = &world.copters[ShadowRand(COPTERS)];
            copter->curState = (ushort)(1 + ShadowRand(3));
            copter->position = droneArg->gameObj->position;
            copter->position.z += 10.0f;
            static const float heights[] = {1.75f, 1.76f, 1.7f, -5.77f, -5.78f, -5.76f, 0.0f, 3.0f, -8.0f};
            copter->position.y += heights[ShadowRand(9)];
        }
        Compare("NDrone2_FindOpponent", trial, OriginalFindOpponent, OurFindOpponent);
    }
}

void TestFindBotOpponent() {
    for (int trial = 0; trial < 30000; trial++) {
        RandomWorld(true);
        droneArg = &world.drones[ShadowRand(DRONES)];
        droneArg->gameObj = &world.objs[Who(droneArg)];
        if (ShadowRand(2)) {
            // Every player present, in sight, in range, so the costs decide
            for (int i = 0; i < 10; i++) {
                BOT_playerInfo_t *info = &droneArg->botVars->players[i];
                info->flags |= BOT_PLAYER_PRESENT | BOT_PLAYER_IN_SIGHT;
                if (ShadowRand(2))
                    info->flags &= ~BOT_PLAYER_TEAMMATE;
                info->distanceSq = ShadowRandFloat(1.0f, 400.0f);
            }
            droneArg->viewRange = 40.0f;
        }
        Compare("NDrone2_FindOpponent (bots)", trial, OriginalFindOpponent, OurFindOpponent);
    }
}

void TestAlerts() {
    static const uint32_t params[] = {0, 0x80000000, 0x41a00000, 0x3f428f5c, kNaN, kSignallingNaN, 0xbf800000};
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld(ShadowRand(5) == 0);
        for (int k = 0; k < 3; k++)
            floatArgs[k] = FromBits(params[ShadowRand(7)]);
        uintArg = 0x10 + ShadowRand(8);
        objArg = ShadowRand(6) == 0 ? NULL : RandomOpponent();
        Compare("NDrone2_SetupAlertTarget", trial,
                [] {
                    OriginalOpponents o;
                    ORIGINAL(0x00044e30, void(__cdecl *)(void))();
                    return 0u;
                },
                [] {
                    NDrone2_SetupAlertTarget();
                    return 0u;
                });
        Compare("NDrone2_DroneAlertToObject", trial,
                [] {
                    OriginalOpponents o;
                    ORIGINAL(0x00038f40, void(__cdecl *)(DCVars_tag *, uint, obj_tag *, float, float, float))(
                        &world.dcv, uintArg, objArg, floatArgs[0], floatArgs[1], floatArgs[2]);
                    return 0u;
                },
                [] {
                    NDrone2_DroneAlertToObject(&world.dcv, uintArg, objArg, floatArgs[0], floatArgs[1], floatArgs[2]);
                    return 0u;
                });
        Compare("NDrone2_DroneAlertToPosition", trial,
                [] {
                    OriginalOpponents o;
                    ORIGINAL(0x00038ea0, void(__cdecl *)(obj_tag *, uint, _VECTOR *, cel_tag *, float, float, float))(
                        objArg, uintArg, &world.vec, (cel_tag *)world.cel, floatArgs[0], floatArgs[1], floatArgs[2]);
                    return 0u;
                },
                [] {
                    NDrone2_DroneAlertToPosition(objArg, uintArg, &world.vec, (cel_tag *)world.cel, floatArgs[0],
                                                 floatArgs[1], floatArgs[2]);
                    return 0u;
                });
    }
}

void TestEachFrame() {
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld(ShadowRand(5) == 0);
        Compare("Drone_BuildDynamicAwarePoints", trial,
                [] {
                    OriginalOpponents o;
                    ORIGINAL(0x00030e70, void(__cdecl *)(void))();
                    return 0u;
                },
                [] {
                    Drone_BuildDynamicAwarePoints();
                    return 0u;
                });
        if (ShadowRand(6) == 0) {
            glb_players[0] = NULL;
            NPCGlobals.dummyPlayerActive = 0;
        }
        // Drone_GetOpponentInfo (ours, which our Drone_ProcessOpponents inlines) runs for real: every drone needs
        // its object
        for (int i = 0; i < DRONES; i++)
            if (world.drones[i].gameObj == NULL)
                world.drones[i].gameObj = &world.objs[i];
        Compare("Drone_ProcessOpponents", trial,
                [] {
                    OriginalOpponents o;
                    ORIGINAL(0x000313e0, void(__cdecl *)(void))();
                    return 0u;
                },
                [] {
                    Drone_ProcessOpponents();
                    return 0u;
                });
        glb_players[0] = &world.player;
        charArg = (char)ShadowRand(3);
        Compare("Drone_UpdateDummyPlayer", trial,
                [] {
                    OriginalOpponents o;
                    ORIGINAL(0x00030950, void(__cdecl *)(char))(charArg);
                    return 0u;
                },
                [] {
                    Drone_UpdateDummyPlayer(charArg);
                    return 0u;
                });
    }
}

} // namespace

void DroneOpponentShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], mpSettings[sizeof(MPSettings_t)], npc[sizeof(NPCGlobals_t)],
        mpGame[sizeof(MPGameStruct)];
    static obj_tag *players[4];
    static uint32_t rand[2];
    float frameRate = _FRAME_RATE;
    float frameDiv = FRAME_RATE_DIV;
    obj_tag *firstObject = ShadowFakes.firstObject;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(mpSettings, &MPSettings, sizeof(mpSettings));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(mpGame, &MPGame, sizeof(mpGame));
    memcpy(players, &glb_players, sizeof(players));
    memcpy(rand, RandWords, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&MPSettings, sizeof(MPSettings_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(&MPGame, sizeof(MPGameStruct));
    regions.Add(&glb_players, sizeof(players));
    regions.Add(RandWords, 8);
    regions.Add(&_FRAME_RATE, 4);
    regions.Add(&FRAME_RATE_DIV, 4);
    ShadowFakes_AddRegions(regions);

    InstallFakes();
    NfWarnMuted++;
    cases = differ = 0;

    TestTargets();
    int targetCases = cases, targetDiffer = differ;
    TestSetOpponent();
    int setCases = cases, setDiffer = differ;
    TestFindOpponent();
    int findCases = cases, findDiffer = differ;
    TestFindBotOpponent();
    int botCases = cases, botDiffer = differ;
    TestAlerts();
    int alertCases = cases, alertDiffer = differ;
    TestEachFrame();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&MPSettings, mpSettings, sizeof(mpSettings));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(&MPGame, mpGame, sizeof(mpGame));
    memcpy(&glb_players, players, sizeof(players));
    memcpy(RandWords, rand, sizeof(rand));
    _FRAME_RATE = frameRate;
    FRAME_RATE_DIV = frameDiv;
    ShadowFakes.firstObject = firstObject;

    printf("[droneopponent] targets: %d cases, %d differ\n", targetCases, targetDiffer);
    printf("[droneopponent] setting the opponent and its aim: %d cases, %d differ\n", setCases - targetCases,
           setDiffer - targetDiffer);
    printf("[droneopponent] finding the opponent: %d cases, %d differ\n", findCases - setCases, findDiffer - setDiffer);
    printf("[droneopponent] finding the opponent, bots: %d cases, %d differ\n", botCases - findCases,
           botDiffer - findDiffer);
    printf("[droneopponent] alerts: %d cases, %d differ\n", alertCases - botCases, alertDiffer - botDiffer);
    printf("[droneopponent] aware points, opponent visibility, the dummy player: %d cases, %d differ\n",
           cases - alertCases, differ - alertDiffer);
}
