// Shadow tests for the civilian states (NDrone2_Civilian.cpp) and the death states (NDrone2_Death.cpp): each handler,
// the original and ours, gets the same message on the same made-up drone, with every function it calls that needs a
// level (state changes, paths and routes, sight, anims, speech and sounds, impacts, alerts, the death helpers) going
// to a logging fake. Run at start with MenuShadowTests=on (MenuProbe.cpp), before any level exists.
//
// What cannot be tested here: what the faked callees would do in a level (the routes and paths really assigned, the
// anims played, the state the machine changes to), and Vec_Dist2D only runs on made-up positions. The handlers'
// returns are compared in AL: their only caller, NDrone2_ProcessStateMachine, reads no more (narrow_returns.py), and
// the originals leave the rest of EAX as whatever the last load or call put there.

#include "DroneCivilianShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../game/drone/Behaviour.h"
#include "../game/drone/DroneTables.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/NDrone2_Civilian.h"
#include "../game/sp/SwitchChannels.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords (GameSpan<uint32_t>{0x0018cdf8})

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int DRONES = 2;

struct World {
    obj_tag objs[DRONES];
    Drone_tag drones[DRONES];
    obj_tag player;
    obj_tag explosive;
    DroneImpact_tag impact;
    DCVars_tag dcv;
    MsgObject msg;
    Drone_tag *droneArg;        // the handler's drone and gameObj arguments: usually the dcv's
    obj_tag *gameObjArg;

    // What the fakes answer
    int setStateResult;
    ushort pathCounts[2];       // route2.count after each NDrone2_AssignAIPath, in turn
    int pathCalls;
    int armed;
    int playerPresent;
    ushort alertResult;
    int missionResult;
    short attackState;
    int withinAngle;
};
World world;

ShadowRegions regions;
int cases, differ;

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

bool __cdecl FakeSetState(StateMachineInfo_tag *sm, DSTATE next, int param) {
    ShadowLog("setstate(%d,%d,%d)", WhoSM(sm), (int)next, param);
    return world.setStateResult != 0;
}

void __cdecl FakeAssignAIPath(DCVars_tag *dcv, uint pathType) {
    ShadowLog("assignpath(%d,%u)", Who(dcv->drone), pathType);
    dcv->drone->route2.count = world.pathCounts[world.pathCalls++ & 1];
}

bool __cdecl FakeStandIdle(DCVars_tag *dcv, char wait) {
    ShadowLog("standidle(%d,%d)", Who(dcv->drone), wait);
    return true;
}

bool __cdecl FakeCallAnim(uint state, uint variant, short endState, uint endMsg, float speed, int unused,
                          DCVars_tag *dcv) {
    ShadowLog("callanim(%x,%u,%d,%u,%08x,%d,%d)", state, variant, endState, endMsg, Bits(speed), unused,
              Who(dcv->drone));
    return true;
}

// The original takes a radius after dcv that it does not read; ours takes none
bool __cdecl FakeNearArmed(DCVars_tag *dcv) {
    ShadowLog("neararmed(%d)", Who(dcv->drone));
    return world.armed != 0;
}

obj_tag *__cdecl FakePlayer(void) {
    ShadowLog("player");
    return world.playerPresent ? &world.player : NULL;
}

void __cdecl FakeAlertToObject(DCVars_tag *dcv, uint msgType, obj_tag *obj, float radiusA, float radiusB,
                               float alertness) {
    ShadowLog("alerttoobject(%d,%x,%d,%08x,%08x,%08x)", Who(dcv->drone), msgType, WhoObj(obj), Bits(radiusA),
              Bits(radiusB), Bits(alertness));
}

bool __cdecl FakeBondTalk(DCVars_tag *dcv) {
    ShadowLog("bondtalk(%d)", Who(dcv->drone));
    return true;
}

bool __cdecl FakeHandleImpact(DCVars_tag *dcv, MsgObject *msg, uint nextState, char flag) {
    ShadowLog("handleimpact(%d,%x,%u,%d)", Who(dcv->drone), msg->msgType, nextState, flag);
    return true;
}

void __cdecl FakeConsiderExplosive(DCVars_tag *dcv, obj_tag *explosive, short nextState) {
    ShadowLog("considerexplosive(%d,%d,%d)", Who(dcv->drone), WhoObj(explosive), nextState);
}

bool __cdecl FakeForcedAttack(DCVars_tag *dcv) {
    ShadowLog("forcedattack(%d)", Who(dcv->drone));
    return true;
}

ushort __cdecl FakeEnemyAlerts(DCVars_tag *dcv, MsgObject *msg) {
    ShadowLog("enemyalerts(%d,%x)", Who(dcv->drone), msg->msgType);
    return world.alertResult;
}

void __cdecl FakePatrolRoute(DCVars_tag *dcv) {
    ShadowLog("patrolroute(%d)", Who(dcv->drone));
}

int __cdecl FakeMissionRoute(DCVars_tag *dcv) {
    ShadowLog("missionroute(%d)", Who(dcv->drone));
    return world.missionResult;
}

DYNAMICSOUNDS *__cdecl FakePlaySFX(DCVars_tag *dcv, uint sfx, _VECTOR *pos, char wait, float volume) {
    ShadowLog("playsfx(%d,%x,%p,%d,%08x)", Who(dcv->drone), sfx, pos, wait, Bits(volume));
    return NULL;
}

bool __cdecl FakeReFindMissionPath(DCVars_tag *dcv) {
    ShadowLog("refindmissionpath(%d)", Who(dcv->drone));
    return true;
}

short __cdecl FakeChangeToAttack(Drone_tag *drone) {
    ShadowLog("changetoattack(%d)", Who(drone));
    return world.attackState;
}

bool __cdecl FakeWithinAngle(DCVars_tag *dcv, float angle) {
    ShadowLog("withinangle(%d,%08x)", Who(dcv->drone), Bits(angle));
    return world.withinAngle != 0;
}

void __cdecl FakeFailReason(Drone_tag *drone, uint reason) {
    ShadowLog("failreason(%d,%u)", Who(drone), reason);
}

void __cdecl FakeInitDeath(DCVars_tag *dcv) {
    ShadowLog("initdeath(%d)", Who(dcv->drone));
}

bool __cdecl FakeLocationDeath(DCVars_tag *dcv, short endState) {
    ShadowLog("locationdeath(%d,%d)", Who(dcv->drone), endState);
    return true;
}

void __cdecl FakeDropWeapon(DCVars_tag *dcv) {
    ShadowLog("dropweapon(%d)", Who(dcv->drone));
}

bool __cdecl FakeDeathChannel(Drone_tag *drone) {
    ShadowLog("deathchannel(%d)", Who(drone));
    return true;
}

void __cdecl FakeEnable(char enable, Drone_tag *drone) {
    ShadowLog("enable(%d,%d)", enable, Who(drone));
}

// NDrone2_SetAsDead, DroneFunc_DeadDrone, NDrone2_FadeOut and NDrone2_ExplosionDir run for real: ours share the
// death states' file and the compiler inlines them there, so a fake would only ever see the original's calls.
void InstallFakes() {
    ShadowHook_Install(0x0004e320, (const void *)&FakeSetState);
    ShadowHook_Install(0x00046ad0, (const void *)&FakeAssignAIPath);
    ShadowHook_Install(0x00035310, (const void *)&FakeStandIdle);
    ShadowHook_Install(0x000341c0, (const void *)&FakeCallAnim);
    ShadowHook_Install(0x00064d40, (const void *)&FakeNearArmed);
    ShadowHook_Install(0x00035f90, (const void *)&FakePlayer);
    ShadowHook_Install(0x00038f40, (const void *)&FakeAlertToObject);
    ShadowHook_Install(0x0003cf50, (const void *)&FakeBondTalk);
    ShadowHook_Install(0x0003e6a0, (const void *)&FakeHandleImpact);
    ShadowHook_Install(0x00038670, (const void *)&FakeConsiderExplosive);
    ShadowHook_Install(0x0003b5d0, (const void *)&FakeForcedAttack);
    ShadowHook_Install(0x00065220, (const void *)&FakeEnemyAlerts);
    ShadowHook_Install(0x00046c20, (const void *)&FakePatrolRoute);
    ShadowHook_Install(0x00046d40, (const void *)&FakeMissionRoute);
    ShadowHook_Install(0x00037340, (const void *)&FakePlaySFX);
    ShadowHook_Install(0x00047290, (const void *)&FakeReFindMissionPath);
    ShadowHook_Install(0x000397f0, (const void *)&FakeChangeToAttack);
    ShadowHook_Install(0x00043f20, (const void *)&FakeWithinAngle);
    ShadowHook_Install(0x00035fb0, (const void *)&FakeFailReason);
    ShadowHook_Install(0x0003de00, (const void *)&FakeInitDeath);
    ShadowHook_Install(0x00034b90, (const void *)&FakeLocationDeath);
    ShadowHook_Install(0x00067690, (const void *)&FakeDropWeapon);
    ShadowHook_Install(0x00036090, (const void *)&FakeDeathChannel);
    ShadowHook_Install(0x000376b0, (const void *)&FakeEnable);
}

// ---- the made-up world

const HASHCODE kLevels[] = {
    HT_Level_HendersonA, HT_Level_HendersonB, HT_Level_CastleIndoors1, HT_Level_TowerA, HT_Level_Tower2B,
    HT_Level_Tower2Elevator, HT_Level_EvilBase,
};

float RandomAlertness() {
    static const float alertnesses[] = {0.0f, 0.5f, 0.65999997f, 0.66f, 0.66000003f, 1.0f};
    if (ShadowRand(12) == 0)
        return NAN;
    return alertnesses[ShadowRand(sizeof(alertnesses) / sizeof(alertnesses[0]))];
}

void RandomObj(obj_tag *o, Drone_tag *d) {
    memset(o, 0, sizeof(*o));
    o->extraObjectData = d;
    o->objectType = OBJECTTYPE_DRONE;
    o->position.x = ShadowRandFloat(-4.0f, 4.0f);
    o->position.y = ShadowRandFloat(-1.0f, 1.0f);
    o->position.z = ShadowRandFloat(-4.0f, 4.0f);
    o->effectFlags = ShadowRand(4) << 4;
    o->flags = (char)ShadowRand(4);
    o->drawnCountdown = (char)ShadowRand(3);
    o->tweakA = (uchar)ShadowRand(256);
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    memset(d, 0, sizeof(*d));
    RandomObj(o, d);
    d->gameObj = o;

    static const short skinClasses[] = {0, 13, 15, 16, 18, 19, 3, 12};
    d->skinClass = skinClasses[ShadowRand(sizeof(skinClasses) / sizeof(skinClasses[0]))];
    static const uchar dtypes[] = {0, 9, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e};
    d->dtype = dtypes[ShadowRand(sizeof(dtypes))];
    static const short animSets[] = {0, 3, 4, 9, 10};
    d->animSet = animSets[ShadowRand(sizeof(animSets) / sizeof(animSets[0]))];
    static const uchar channels[] = {0, 5, 0x29, 0x2a, 0x88, 99};
    d->deathSwitchChannel = channels[ShadowRand(sizeof(channels))];
    d->modeChangeSwitchChannel = (uchar)(ShadowRand(3) == 0 ? ShadowRand(256) : 0);
    d->secondState = (short)(ShadowRand(3) == 0 ? 0 : ShadowRand(250));
    d->initialState = (short)ShadowRand(250);
    d->unknown15 = (uchar)ShadowRand(2);
    d->firing = (uchar)ShadowRand(2);
    d->headShot = (uchar)ShadowRand(2);
    d->side = (uchar)(1 + ShadowRand(3));
    d->deathScript = (HASHCODE)(ShadowRand(4) == 0 ? HT_Script_DeathScriptIcyQuip : 0);
    d->alertness = RandomAlertness();
    d->armedPersonRadius = ShadowRandFloat(0.0f, 30.0f);
    d->fadeAlpha = ShadowRand(4) ? ShadowRandFloat(0.0f, 255.0f) : (ShadowRand(2) ? 0.0f : 5.0f);
    static const uint scratches[] = {0, 0, 1, 0xff, 7};
    d->stateScratch = scratches[ShadowRand(sizeof(scratches) / sizeof(scratches[0]))];
    d->stateScratch2 = ShadowRand(1000);
    d->alertStatus = (uchar)ShadowRand(4);
    d->prevAlertStatus = (uchar)ShadowRand(4);

    d->route2.count = (ushort)(ShadowRand(2) ? 0 : 1 + ShadowRand(20));
    static const uchar statuses[] = {0, 1, 9, 0xc, 3};
    d->route2.status = statuses[ShadowRand(sizeof(statuses))];

    d->flags = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    d->flags2 = ShadowRand(0x10000);
    for (int w = 0; w < 3; w++) {
        d->behaviour1[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
        d->behaviour2[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    }
    d->currentBehaviour = ShadowRand(2) ? d->behaviour1 : d->behaviour2;
    // The properties these states test, often either way
    behaviour_util_setProperty(DRONE_PROP_1D, d->currentBehaviour, ShadowRand(2));
    behaviour_util_setProperty(DRONE_PROP_31, d->currentBehaviour, ShadowRand(2));
    behaviour_util_setProperty(DRONE_PROP_33, d->currentBehaviour, ShadowRand(2));
    behaviour_util_setProperty(DRONE_PROP_52, d->currentBehaviour, ShadowRand(2));

    d->sm.id = 100 + i;
    static const uint prevStates[] = {DSTATE_Fade, DSTATE_Fade, DSTATE_Dead, DSTATE_FadeFast, DSTATE_Idle};
    d->sm.curState = ShadowRand(250);
    d->sm.prevState = prevStates[ShadowRand(sizeof(prevStates) / sizeof(prevStates[0]))];
    d->sm.scratch1c = ShadowRand(5);
    d->sm.stateParam = ShadowRand(4) ? (int)(uintptr_t)&world.impact : 0;
    d->timerA.on = (uchar)ShadowRand(2);
    d->timerA.dueFrame = ShadowRand(5000);
    d->timerA.unknown8 = ShadowRand(5);
    d->timerB.on = (uchar)ShadowRand(2);
    d->timerB.dueFrame = ShadowRand(5000);
    d->timerB.unknown8 = ShadowRand(5);
}

void RandomWorld() {
    for (int i = 0; i < DRONES; i++)
        RandomDrone(i);
    RandomObj(&world.player, NULL);
    world.player.objectType = OBJECTTYPE_PLAYER;
    // Around the 3 m the waiting states test, from the dcv's drone
    if (ShadowRand(3) == 0) {
        world.player.position = world.objs[0].position;
        world.player.position.x += ShadowRand(2) ? 3.0f : 2.9999998f;
    }
    RandomObj(&world.explosive, NULL);
    world.explosive.objectType = OBJECTTYPE_BULLET;
    memset(&world.impact, 0, sizeof(world.impact));
    world.impact.position.x = ShadowRandFloat(-10.0f, 10.0f);
    world.impact.position.z = ShadowRandFloat(-10.0f, 10.0f);

    world.dcv.gameObj = &world.objs[0];
    world.dcv.drone = &world.drones[0];
    world.dcv.cel = NULL;
    world.dcv.aiStateMachine = &world.drones[0].sm;
    // Now and then a handler is given the other drone or object than the dcv's, to tell which one it reads
    world.droneArg = ShadowRand(5) ? &world.drones[0] : &world.drones[1];
    world.gameObjArg = ShadowRand(5) ? &world.objs[0] : &world.objs[1];

    world.setStateResult = ShadowRand(2);
    world.pathCounts[0] = (ushort)(ShadowRand(2) ? 0 : 1 + ShadowRand(9));
    world.pathCounts[1] = (ushort)(ShadowRand(2) ? 0 : 1 + ShadowRand(9));
    world.pathCalls = 0;
    world.armed = ShadowRand(2);
    world.playerPresent = ShadowRand(6) != 0;
    static const ushort alerts[] = {0, 0, DSTATE_Attack, DSTATE_SeenOpponent, 0xffff, 0x8001};
    world.alertResult = alerts[ShadowRand(sizeof(alerts) / sizeof(alerts[0]))];
    static const int routes[] = {0, 1, 2, 3, 3, 4, 0x103};
    world.missionResult = routes[ShadowRand(sizeof(routes) / sizeof(routes[0]))];
    static const short attacks[] = {0, DSTATE_Attack, DSTATE_Combat, -1, -0x7ff0};
    world.attackState = attacks[ShadowRand(sizeof(attacks) / sizeof(attacks[0]))];
    world.withinAngle = ShadowRand(3) != 0;

    static const uint frames[] = {1000, 12345, 0x7ffffff0, 0x80000010, 0xfffffff0};
    GameState.NumFramesUnpaused = frames[ShadowRand(sizeof(frames) / sizeof(frames[0]))] + ShadowRand(50);
    GameState.CurrentLevelHashcode = kLevels[ShadowRand(sizeof(kLevels) / sizeof(kLevels[0]))];
    if (ShadowRand(2)) {
        FRAME_RATE_INT = 60;
        FRAME_RATE_DIV = 1.0f;
    } else {
        FRAME_RATE_INT = 50;
        FRAME_RATE_DIV = 1.2f;
    }
    NPCGlobals.unknown14c = ShadowRand(4);
    NPCGlobals.unknown150 = ShadowRand(6);
    NPCGlobals.unknown15c = 100 + ShadowRand(2);
    NPCGlobals.deadFrames = ShadowRand(2) ? 600 : 0xfffffff0u + ShadowRand(32);
    switch_channels[0x35] = (char)ShadowRand(2);
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

void RandomMessage() {
    memset(&world.msg, 0, sizeof(world.msg));
    switch (ShadowRand(10)) {
    case 0:
        world.msg.msgType = ShadowRand(2) ? DRONE_MSG_BotStateChanged : 0x40;
        break;
    case 1:
        world.msg.msgType = ShadowRand(2) ? 0xffffffffu : 0x80000003u;
        break;
    default:
        world.msg.msgType = ShadowRand(0x25);
        break;
    }
    world.msg.sender = 100 + ShadowRand(DRONES);
    world.msg.receiver = 100;
    world.msg.createdFrame = GameState.NumFramesUnpaused;
    world.msg.handleOnFrame = GameState.NumFramesUnpaused;
    world.msg.extraData = ShadowRand(4) ? (void *)&world.explosive : NULL;
}

// ---- the handlers

struct Handler {
    const char *name;
    unsigned at;
    DroneStateFunc ours;
};

const Handler kCivilian[] = {
    {"NDrone2_DSTATE_CivilianInit", 0x00050530, NDrone2_DSTATE_CivilianInit},
    {"NDrone2_DSTATE_Civilian", 0x00050600, NDrone2_DSTATE_Civilian},
    {"NDrone2_DSTATE_CivilianPatrol", 0x00050a20, NDrone2_DSTATE_CivilianPatrol},
    {"NDrone2_DSTATE_ReturnToPatrolPath", 0x00050ca0, NDrone2_DSTATE_ReturnToPatrolPath},
    {"NDrone2_DSTATE_CivilianMission", 0x00050ce0, NDrone2_DSTATE_CivilianMission},
    {"NDrone2_DSTATE_CivilianMissionWait", 0x00051050, NDrone2_DSTATE_CivilianMissionWait},
    {"NDrone2_DSTATE_AllyLeadInit", 0x000519f0, NDrone2_DSTATE_AllyLeadInit},
    {"NDrone2_DSTATE_PartyGirlInit", 0x00053110, NDrone2_DSTATE_PartyGirlInit},
    {"NDrone2_DSTATE_PartyGirl", 0x00053190, NDrone2_DSTATE_PartyGirl},
};

const Handler kDeath[] = {
    {"NDrone2_DSTATE_Death_Anim", 0x000567f0, NDrone2_DSTATE_Death_Anim},
    {"NDrone2_DSTATE_DeathByExplosion", 0x00056a30, NDrone2_DSTATE_DeathByExplosion},
    {"NDrone2_DSTATE_Dead", 0x00056f00, NDrone2_DSTATE_Dead},
    {"NDrone2_DSTATE_Fade", 0x0004bb20, NDrone2_DSTATE_Fade},
    {"NDrone2_DSTATE_FadeFast", 0x0004bbf0, NDrone2_DSTATE_FadeFast},
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

void TestHandlers(const Handler *handlers, int count, int trials) {
    for (int h = 0; h < count; h++) {
        current = &handlers[h];
        for (int trial = 0; trial < trials; trial++) {
            RandomWorld();
            RandomMessage();
            cases++;
            if (!ShadowCompare(regions, "dronestate2", current->name, trial, RunTheirs, RunOurs))
                differ++;
        }
    }
}

} // namespace

void DroneCivilianShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], npc[sizeof(NPCGlobals_t)];
    static char channels[256];
    static uint32_t rand[2];
    uint32_t frameRate = FRAME_RATE_INT;
    float frameDiv = FRAME_RATE_DIV;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(channels, switch_channels, sizeof(channels));
    XbeOursRead(rand, RandWords.at, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(switch_channels, 256);
    regions.Add(RandWords.game(), 8);
    regions.Add(&FRAME_RATE_INT, 4);
    regions.Add(&FRAME_RATE_DIV, 4);

    InstallFakes();
    NfWarnMuted++;

    cases = differ = 0;
    TestHandlers(kCivilian, sizeof(kCivilian) / sizeof(kCivilian[0]), 4000);
    int civilianCases = cases, civilianDiffer = differ;
    TestHandlers(kDeath, sizeof(kDeath) / sizeof(kDeath[0]), 4000);

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(switch_channels, channels, sizeof(channels));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
    FRAME_RATE_INT = frameRate;
    FRAME_RATE_DIV = frameDiv;

    printf("[dronestate2] civilian, party girl and AllyLeadInit states: %d cases, %d differ\n", civilianCases,
           civilianDiffer);
    printf("[dronestate2] death and fade states: %d cases, %d differ\n", cases - civilianCases,
           differ - civilianDiffer);
}
