// Shadow tests for the starting, idle, alert, patrol, run-to-point and small drone states (game/drone/NDrone2_States.cpp):
// each original handler and ours run on the same made-up drones and messages, every message type their jump tables
// handle and some they do not. Run at start with MenuShadowTests=on (MenuProbe.cpp), before any level exists.
//
// What would need a level goes to logging fakes that answer from the test's settings: the state change
// (Drone_SM_SetState: so the requested state and its parameter are compared, and its answer is made up to see which
// paths pass it on), sight (DroneVision_EnemyLookForOpponent, _EnemyAlerts), impacts (DroneFunc_HandleImpact,
// NDrone2_BulletImpact), routes (NDrone2_AssignAIPath, which also sets route2.count; NDrone2_FindRunToPoint;
// NDrone2_UpdatePatrolRoute), talk (NDrone2_PatrolTalk), the music count (NDrone2_SeenAndAttacking), the forced
// attack and explosives (DroneFunc_DoForcedAttack, _ConsiderExplosive), the anims (DroneAnim_CallAnim,
// _SetStandIdleAnim) and NDrone2_Enable. Those are other functions' tests; here only what the states pass them and
// do with their answers is checked. behaviour_util_*, NDrone2_SetIdleTimeOut, Drone_AlertStatusSet and Rand_Rand run
// for real: the random state (Rand_Random's words at 0x0018cdf8) is compared afterwards.
//
// Returns are compared in AL only: the handlers return bool, and their one caller (Drone_SM_RouteMsgDCV, through
// NDrone2_ProcessStateMachine) tests AL; the original's paths that pass Drone_SM_SetState's answer on leave its upper
// bytes in EAX, ours do not.
//
// JustStand's FSIN and FCOS are the CRT's sin and cos in ours: its two positions may differ by 1 ulp, which is
// accepted (and counted).

#include "DroneStateShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Collide.h"
#include "../game/drone/Behaviour.h"
#include "../game/drone/DroneTables.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/NDrone2_States.h"
#include "../game/sp/SwitchChannels.h"

#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords ((uint32_t *)0x0018cdf8)

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int DRONES = 2;

struct World {
    obj_tag objs[DRONES];
    Drone_tag drones[DRONES];
    HITDATA_tag hit;
    DCVars_tag dcv;
    MsgObject msg;
    Drone_tag *droneArg;    // the handler's own drone and object arguments: usually the dcv's, not always
    obj_tag *objArg;

    // What the fakes answer
    uint32_t setStateResult;
    uint32_t lookResult;
    uint32_t alertResult;
    uint32_t runPointResult;
    ushort routeCount;
};
World world;

ShadowRegions regions;
int cases, differ, ulpAccepted;

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
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

int WhoDCV(const DCVars_tag *dcv) {
    return dcv == &world.dcv ? 0 : 99;
}

// ---- the fakes

uint32_t __cdecl FakeSetState(StateMachineInfo_tag *sm, uint state, int param) {
    ShadowLog("setstate(%d,%d,%d)", WhoSM(sm), (int)state, param);
    return world.setStateResult;
}

void __cdecl FakeEnable(char enable, Drone_tag *drone) {
    ShadowLog("enable(%d,%d)", enable, Who(drone));
}

uint32_t __cdecl FakeLook(DCVars_tag *dcv) {
    ShadowLog("look(%d)", WhoDCV(dcv));
    return world.lookResult;
}

uint32_t __cdecl FakeAlerts(DCVars_tag *dcv, MsgObject *msg) {
    ShadowLog("alerts(%d,%d)", WhoDCV(dcv), msg == &world.msg);
    return world.alertResult;
}

uint32_t __cdecl FakeHandleImpact(DCVars_tag *dcv, MsgObject *msg, uint nextState, char flag) {
    ShadowLog("handleimpact(%d,%d,%u,%d)", WhoDCV(dcv), msg == &world.msg, nextState, flag);
    return 0x5a5a5a01;
}

uint32_t __cdecl FakeBulletImpact(DCVars_tag *dcv, HITDATA_tag *hit, uint nextState, char flag) {
    ShadowLog("bulletimpact(%d,%p,%u,%d)", WhoDCV(dcv), hit, nextState, flag);
    return 0x5a5a5a01;
}

uint32_t __cdecl FakeAssignAIPath(DCVars_tag *dcv, uint pathType) {
    ShadowLog("assignaipath(%d,%u)", WhoDCV(dcv), pathType);
    dcv->drone->route2.count = world.routeCount;
    return 0x5a5a5a5a;
}

uint32_t __cdecl FakeFindRunToPoint(DCVars_tag *dcv) {
    ShadowLog("findruntopoint(%d)", WhoDCV(dcv));
    return world.runPointResult;
}

uint32_t __cdecl FakeUpdatePatrolRoute(DCVars_tag *dcv) {
    ShadowLog("updatepatrolroute(%d)", WhoDCV(dcv));
    return 7;
}

uint32_t __cdecl FakePatrolTalk(DCVars_tag *dcv) {
    ShadowLog("patroltalk(%d)", WhoDCV(dcv));
    return 1;
}

void __cdecl FakeSeenAndAttacking(DCVars_tag *dcv) {
    ShadowLog("seenandattacking(%d)", WhoDCV(dcv));
}

uint32_t __cdecl FakeForcedAttack(DCVars_tag *dcv) {
    ShadowLog("forcedattack(%d)", WhoDCV(dcv));
    return 0;
}

void __cdecl FakeConsiderExplosive(DCVars_tag *dcv, obj_tag *explosive, short nextState) {
    ShadowLog("considerexplosive(%d,%p,%d)", WhoDCV(dcv), explosive, nextState);
}

uint32_t __cdecl FakeCallAnim(uint state, uint variant, short endState, uint endMsg, float speed, int unused,
                              DCVars_tag *dcv) {
    ShadowLog("callanim(%u,%u,%d,%u,%08x,%d,%d)", state, variant, endState, endMsg, Bits(speed), unused, WhoDCV(dcv));
    return 1;
}

uint32_t __cdecl FakeStandIdle(DCVars_tag *dcv, char wait) {
    ShadowLog("standidle(%d,%d)", WhoDCV(dcv), wait);
    return 1;
}

void InstallFakes() {
    ShadowHook_Install(0x0004e320, (const void *)&FakeSetState);
    ShadowHook_Install(0x000376b0, (const void *)&FakeEnable);
    ShadowHook_Install(0x00065360, (const void *)&FakeLook);
    ShadowHook_Install(0x00065220, (const void *)&FakeAlerts);
    ShadowHook_Install(0x0003e6a0, (const void *)&FakeHandleImpact);
    ShadowHook_Install(0x0003e220, (const void *)&FakeBulletImpact);
    ShadowHook_Install(0x00046ad0, (const void *)&FakeAssignAIPath);
    ShadowHook_Install(0x00047190, (const void *)&FakeFindRunToPoint);
    ShadowHook_Install(0x00046c20, (const void *)&FakeUpdatePatrolRoute);
    ShadowHook_Install(0x0003b9b0, (const void *)&FakePatrolTalk);
    ShadowHook_Install(0x00039f60, (const void *)&FakeSeenAndAttacking);
    ShadowHook_Install(0x0003b5d0, (const void *)&FakeForcedAttack);
    ShadowHook_Install(0x00038670, (const void *)&FakeConsiderExplosive);
    ShadowHook_Install(0x000341c0, (const void *)&FakeCallAnim);
    ShadowHook_Install(0x00035310, (const void *)&FakeStandIdle);
}

// ---- the made-up world

const HASHCODE kLevels[] = {
    HT_Level_HendersonA, HT_Level_SpaceStationD, HT_Level_SpaceStationD, HT_Level_RefRoom, HT_Level_Tower2A,
    HT_Level_Tower2B, HT_Level_Tower2C, HT_Level_TowerA, HT_Level_CastleIndoors1,
};

const uint kStates[] = {
    0, DSTATE_Idle, DSTATE_Alert, DSTATE_Patrol, DSTATE_Attack, DSTATE_JustStand, DSTATE_BotInit, 0x7fff, 0xffff,
};

float RandomAlertness() {
    static const float values[] = {0.0f, -0.0f, 0.5f, 0.66f, 0.65999997f, 0.66000003f, 0.9f, 1.0f, 0.99999994f, 1.5f};
    if (ShadowRand(16) == 0) {
        uint32_t nan = 0x7fc00000;
        float f;
        memcpy(&f, &nan, 4);
        return f;
    }
    return values[ShadowRand(sizeof(values) / sizeof(values[0]))];
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    d->gameObj = o;
    o->extraObjectData = d;
    o->objectType = OBJECTTYPE_DRONE;
    o->position.x = ShadowRandFloat(-50.0f, 50.0f);
    o->position.y = ShadowRandFloat(-5.0f, 5.0f);
    o->position.z = ShadowRandFloat(-50.0f, 50.0f);
    o->flags = (char)ShadowRand(4);

    static const uchar dtypes[] = {DTYPE_Normal, DTYPE_Guard, DTYPE_HostageKiller, DTYPE_HostageKiller,
                                   DTYPE_TruckDriver, DTYPE_TruckDriver, DTYPE_Bot, DTYPE_Bot, DTYPE_Civilian};
    d->dtype = dtypes[ShadowRand(sizeof(dtypes))];
    d->side = (uchar)(ShadowRand(4));
    static const float healths[] = {-1.0f, 0.0f, -0.0f, 50.0f, 0.5f};
    d->health = healths[ShadowRand(5)];
    d->skinClass = (short)(ShadowRand(2) ? 0x14 : ShadowRand(22));
    static const uchar channels[] = {0, 0, 5, 0x61, 0xff};
    d->waitSwitchChannel = channels[ShadowRand(5)];
    d->waitSwitchInitial = (uchar)ShadowRand(2);
    d->playScript = (HASHCODE)(ShadowRand(2) ? 0 : 0x6000168 + ShadowRand(3));
    static const short initials[] = {DSTATE_Idle, DSTATE_InitPatrol, DSTATE_Attack, -1, 0x7fff, 0};
    d->initialState = initials[ShadowRand(6)];
    d->returnState = (short)ShadowRand(200);
    d->stateScratch = ShadowRand(2) ? kStates[ShadowRand(sizeof(kStates) / 4)] : 0x80000000u + ShadowRand(5);
    d->alertness = RandomAlertness();
    d->angleToSomePoint = ShadowRandFloat(-7.0f, 7.0f);
    d->unknown38 = (uchar)ShadowRand(3);
    d->route2.count = (ushort)(ShadowRand(2) ? 0 : 1 + ShadowRand(5));
    d->alertStatus = (uchar)ShadowRand(3);
    d->prevAlertStatus = (uchar)ShadowRand(3);
    d->alertStatusChangedFrame = ShadowRand(1000);
    d->stateTimeoutFrame = ShadowRand(1000);

    for (int w = 0; w < 3; w++) {
        d->behaviour1[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
        d->behaviour2[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    }
    d->currentBehaviour = ShadowRand(2) ? d->behaviour1 : d->behaviour2;

    static const uint flagBits[] = {0x10, 0x100, 0x200, 0x2000, 0x10000, 0x20000, 0x4000000, 0x8000000};
    for (int k = 0; k < 8; k++)
        if (ShadowRand(2))
            d->flags |= flagBits[k];

    d->sm.id = 100 + i;
    d->sm.curState = kStates[ShadowRand(sizeof(kStates) / 4)];
    d->sm.scratch1c = ShadowRand(5);
}

void RandomMessage(uint type) {
    MsgObject *msg = &world.msg;
    memset(msg, 0, sizeof(*msg));
    msg->msgType = type;
    msg->sender = 100 + ShadowRand(3);
    msg->createdFrame = GameState.NumFramesUnpaused;
    switch (ShadowRand(5)) {
    case 0:
        msg->extraData = NULL;
        break;
    case 1:
        msg->extraData = &world.objs[1];
        break;
    case 2:
        msg->extraData = &world.hit;
        break;
    case 3:
        msg->extraData = (void *)(uintptr_t)kStates[ShadowRand(sizeof(kStates) / 4)];
        break;
    default:
        msg->extraData = (void *)(uintptr_t)(0x80000000u + ShadowRand(300));
        break;
    }
}

void RandomWorld(uint msgType) {
    for (int i = 0; i < DRONES; i++)
        RandomDrone(i);
    memset(&world.hit, 0, sizeof(world.hit));
    world.dcv.drone = &world.drones[0];
    world.dcv.gameObj = &world.objs[0];
    world.dcv.cel = NULL;
    world.dcv.aiStateMachine = &world.drones[0].sm;
    // The handlers read some fields through dcv and some through their own drone argument
    world.droneArg = ShadowRand(5) ? &world.drones[0] : &world.drones[1];
    world.objArg = ShadowRand(5) ? &world.objs[0] : &world.objs[1];

    static const uint32_t setStates[] = {0, 1, 0x100, 0x12345601};
    world.setStateResult = setStates[ShadowRand(4)];
    static const uint32_t sights[] = {0, 0, 0xdead0000, DSTATE_Attack, 0xbeef0000 | DSTATE_Alert, 0xffff, 0x8000};
    world.lookResult = sights[ShadowRand(7)];
    world.alertResult = sights[ShadowRand(7)];
    world.runPointResult = setStates[ShadowRand(4)];
    world.routeCount = (ushort)(ShadowRand(2) ? 0 : 1 + ShadowRand(3));

    GameState.CurrentLevelHashcode = kLevels[ShadowRand(sizeof(kLevels) / sizeof(kLevels[0]))];
    static const uint32_t frames[] = {0, 1000, 123457, 0x7fffffff, 0x80000000u, 0xfffffff0u};
    GameState.NumFramesUnpaused = frames[ShadowRand(6)] + ShadowRand(1000);
    FRAME_RATE_INT = ShadowRand(2) ? 60 : 50;
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    for (int ch = 0; ch < 256; ch++)
        switch_channels[ch] = (char)(ShadowRand(2));
    RandomMessage(msgType);
}

// ---- the handlers

typedef bool (*OurHandler)(DCVars_tag *, Drone_tag *, obj_tag *, MsgObject *);
typedef uint32_t(__cdecl *TheirHandler)(DCVars_tag *, Drone_tag *, obj_tag *, MsgObject *);

struct Handler {
    const char *name;
    unsigned at;
    OurHandler ours;
    int trials;     // per message type; more for the ones the demos do not reach
};

const Handler kHandlers[] = {
    {"NDrone2_DSTATE_Global", 0x0004e950, NDrone2_DSTATE_Global, 40},
    {"NDrone2_DSTATE_WaitSwitch", 0x0004eaa0, NDrone2_DSTATE_WaitSwitch, 40},
    {"NDrone2_DSTATE_Disabled", 0x0004ebc0, NDrone2_DSTATE_Disabled, 80},
    {"NDrone2_DSTATE_Idle", 0x0004f100, NDrone2_DSTATE_Idle, 60},
    {"NDrone2_DSTATE_Alert", 0x0004f440, NDrone2_DSTATE_Alert, 60},
    {"NDrone2_DSTATE_InitPatrol", 0x0004f6f0, NDrone2_DSTATE_InitPatrol, 40},
    {"NDrone2_DSTATE_Patrol", 0x0004f790, NDrone2_DSTATE_Patrol, 60},
    {"NDrone2_DSTATE_EnemyRunToPoint", 0x0005b270, NDrone2_DSTATE_EnemyRunToPoint, 60},
    {"NDrone2_DSTATE_DeleteMe", 0x0004d840, NDrone2_DSTATE_DeleteMe, 20},
    {"NDrone2_DSTATE_JustStand", 0x0004d880, NDrone2_DSTATE_JustStand, 80},
    {"NDrone2_DSTATE_WaitForever", 0x0004dbe0, NDrone2_DSTATE_WaitForever, 20},
};

const Handler *current;
_VECTOR theirPositions[DRONES];

uint32_t RunTheirs() {
    uint32_t result;
    {
        XbeOriginalScope o(current->at);
        result = ORIGINAL(current->at, TheirHandler)(&world.dcv, world.droneArg, world.objArg, &world.msg);
    }
    for (int i = 0; i < DRONES; i++)
        theirPositions[i] = world.objs[i].position;
    return result & 0xff;
}

// One float that is ours, accepted as the original's when it is within 1 ulp of it
void AcceptUlp(float *ours, float theirs) {
    uint32_t a = Bits(*ours), b = Bits(theirs);
    if (a != b && (a - b == 1 || b - a == 1)) {
        *ours = theirs;
        ulpAccepted++;
    }
}

uint32_t RunOurs() {
    uint32_t result = current->ours(&world.dcv, world.droneArg, world.objArg, &world.msg);
    if (current->ours == NDrone2_DSTATE_JustStand) {
        for (int i = 0; i < DRONES; i++) {
            AcceptUlp(&world.objs[i].position.x, theirPositions[i].x);
            AcceptUlp(&world.objs[i].position.z, theirPositions[i].z);
        }
    }
    return result;
}

void TestHandlers() {
    // Every type up to 0x22 (beyond each jump table's top), and a few more
    static const uint kExtraTypes[] = {0x2e, 0x40, 0xffffffffu};
    const int numTypes = 0x23 + sizeof(kExtraTypes) / sizeof(kExtraTypes[0]);
    for (size_t h = 0; h < sizeof(kHandlers) / sizeof(kHandlers[0]); h++) {
        current = &kHandlers[h];
        int trial = 0;
        for (int t = 0; t < numTypes; t++) {
            uint type = t < 0x23 ? (uint)t : kExtraTypes[t - 0x23];
            for (int k = 0; k < current->trials; k++, trial++) {
                RandomWorld(type);
                cases++;
                if (!ShadowCompare(regions, "dronestate", current->name, trial, RunTheirs, RunOurs))
                    differ++;
            }
        }
    }
}

} // namespace

void DroneStateShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)];
    static char channels[256];
    static uint32_t rand[2];
    uint32_t frameRate = FRAME_RATE_INT;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(channels, switch_channels, sizeof(channels));
    memcpy(rand, RandWords, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(switch_channels, 256);
    regions.Add(RandWords, 8);
    regions.Add(&FRAME_RATE_INT, 4);

    InstallFakes();
    NfWarnMuted++;
    cases = differ = ulpAccepted = 0;

    TestHandlers();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(switch_channels, channels, sizeof(channels));
    memcpy(RandWords, rand, sizeof(rand));
    FRAME_RATE_INT = frameRate;

    printf("[dronestate] NDrone2_DSTATE_* (11 handlers): %d cases, %d differ (%d JustStand positions 1 ulp apart)\n",
           cases, differ, ulpAccepted);
}
