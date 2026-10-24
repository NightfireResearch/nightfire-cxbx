// Shadow tests for parcel 9's package C: the civilian guard, scared and hiding states (NDrone2_Civilian.cpp), the
// alarm, enemy mission, ninja, space drake and astronaut states (NDrone2_Special.cpp), the impact, knock-out, taser,
// stun, smoke and surrender states and their helpers (NDrone2_Reactions.cpp), and NDrone2_LogDroneDeath and the
// special death (NDrone2_Death.cpp). Each handler, the original and ours, gets the same message on the same made-up
// drone, with every function it calls that needs a level (state changes, anims, routes, sight, speech, impacts,
// weapons, alerts, moves) going to a logging fake. Run at start with MenuShadowTests=on (MenuProbe.cpp), before any
// level exists.
//
// What cannot be tested here: what the faked callees would do in a level. The maths helpers (Vec_Dist2D,
// Vec_Dist3D, Vec_AngleDifference, Vec_SqDist3D), the random numbers, the behaviour properties, NDrone2_Player,
// NDrone2_ObjectIsArmed and DroneFunc_SendHurtMessage run for real. The handlers' returns are compared in AL, as
// their only caller (NDrone2_ProcessStateMachine) reads them.

#include "DroneReactShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../game/drone/Behaviour.h"
#include "../game/drone/DroneTables.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/NDrone2_Civilian.h"
#include "../game/drone/NDrone2_Reactions.h"
#include "../game/drone/NDrone2_Special.h"
#include "../game/mp/multiplayer.h"
#include "../game/sp/SwitchChannels.h"
#include "../math/math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords (GameSpan<uint32_t>{0x0018cdf8})
#define switch_BLIND_DRONES U32_AT(XbeOurs(0x001df9a0))

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int DRONES = 2;

struct World {
    obj_tag objs[DRONES];
    Drone_tag drones[DRONES];
    obj_tag player;
    obj_tag explosive;
    AnimState animStates[3];
    DCVars_tag dcv;
    MsgObject msg;
    Drone_tag *droneArg;        // the handler's drone and gameObj arguments: usually the dcv's
    obj_tag *gameObjArg;
    float floatArg;             // DroneFunc_AnyoneNear's radius

    // What the fakes answer
    int setStateResult;
    ushort pathCount;
    int armed;
    ushort alertResult;
    ushort lookResult;
    uint missionResult;
    short attackState;
    int civilian;
    int findPoint;
    short firstAttack;
    uint guardZone;
    uint sfx;
    uint recoverTime;
    int inRange;
    uint canMoveMask;           // bit 0: +3 m aside, 1: -3 m aside, 2: 5 m back, 3: 5 m forward
    uint moveResult;
    float hitHealth;
    int anyoneNear;
    int unsurrender;
    int canSee;
    int droneNear;
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

bool __cdecl FakeCallAnim(uint state, uint variant, short endState, uint endMsg, float speed, int unused,
                          DCVars_tag *dcv) {
    ShadowLog("callanim(%x,%u,%d,%u,%08x,%d,%d)", state, variant, endState, endMsg, Bits(speed), unused,
              Who(dcv->drone));
    return true;
}

bool __cdecl FakeStandIdle(DCVars_tag *dcv, char wait) {
    ShadowLog("standidle(%d,%d)", Who(dcv->drone), wait);
    return true;
}

bool __cdecl FakeNearArmed(DCVars_tag *dcv) {
    ShadowLog("neararmed(%d)", Who(dcv->drone));
    return world.armed != 0;
}

bool __cdecl FakeBondTalk(DCVars_tag *dcv) {
    ShadowLog("bondtalk(%d)", Who(dcv->drone));
    return true;
}

void __cdecl FakeAlertToObject(DCVars_tag *dcv, uint msgType, obj_tag *obj, float a, float b, float c) {
    ShadowLog("alerttoobject(%d,%x,%d,%08x,%08x,%08x)", Who(dcv->drone), msgType, WhoObj(obj), Bits(a), Bits(b),
              Bits(c));
}

void __cdecl FakeAlertToPosition(obj_tag *sender, uint msgType, _VECTOR *pos, cel_tag *cel, float a, float b,
                                 float c) {
    ShadowLog("alerttoposition(%d,%x,%08x,%p,%08x,%08x,%08x)", WhoObj(sender), msgType, pos ? Bits(pos->x) : 0, cel,
              Bits(a), Bits(b), Bits(c));
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

ushort __cdecl FakeEnemyLook(DCVars_tag *dcv) {
    ShadowLog("enemylook(%d)", Who(dcv->drone));
    return world.lookResult;
}

bool __cdecl FakeSetDronePartner(uchar dtype, DCVars_tag *dcv) {
    ShadowLog("setdronepartner(%d,%d)", dtype, Who(dcv->drone));
    return true;
}

uint __cdecl FakeDoorGuardHandler(DCVars_tag *dcv) {
    ShadowLog("doorguard(%d)", Who(dcv->drone));
    return world.guardZone;
}

void __cdecl FakeSetAngleToObj(DCVars_tag *dcv, obj_tag *obj, float offset, char force) {
    ShadowLog("setangle(%d,%d,%08x,%d)", Who(dcv->drone), WhoObj(obj), Bits(offset), force);
}

uint __cdecl FakeChooseSFX(uint first, uint last) {
    ShadowLog("choosesfx(%x,%x)", first, last);
    return world.sfx;
}

DYNAMICSOUNDS *__cdecl FakeSpeechSFX(DCVars_tag *dcv, uint sfx, char force, char face, char needFace) {
    ShadowLog("speech(%d,%x,%d,%d,%d)", Who(dcv->drone), sfx, force, face, needFace);
    return NULL;
}

void __cdecl FakeFailReason(Drone_tag *drone, uint reason) {
    ShadowLog("failreason(%d,%u)", Who(drone), reason);
}

void __cdecl FakeAlertStatusSet(char status, DCVars_tag *dcv) {
    ShadowLog("alertstatus(%d,%d)", status, Who(dcv->drone));
}

bool __cdecl FakeFindAlarmPoint(DCVars_tag *dcv) {
    ShadowLog("findalarm(%d)", Who(dcv->drone));
    return world.findPoint != 0;
}

bool __cdecl FakeFindRunToPoint(DCVars_tag *dcv) {
    ShadowLog("findruntopoint(%d)", Who(dcv->drone));
    return world.findPoint != 0;
}

void __cdecl FakeSeenAndAttacking(DCVars_tag *dcv) {
    ShadowLog("seen(%d)", Who(dcv->drone));
}

bool __cdecl FakeIsCivilian(DCVars_tag *dcv) {
    ShadowLog("iscivilian(%d)", Who(dcv->drone));
    return world.civilian != 0;
}

short __cdecl FakeFirstAttack(DCVars_tag *dcv) {
    ShadowLog("firstattack(%d)", Who(dcv->drone));
    return world.firstAttack;
}

void __cdecl FakeScaredTalk(DCVars_tag *dcv) {
    ShadowLog("scaredtalk(%d)", Who(dcv->drone));
}

uint __cdecl FakeMissionRoute(DCVars_tag *dcv) {
    ShadowLog("missionroute(%d)", Who(dcv->drone));
    return world.missionResult;
}

void __cdecl FakeInvalidateRoute(AIRoute_tag *route) {
    ShadowLog("invalidateroute(%d)", route == &world.drones[0].route2 ? 0 : route == &world.drones[1].route2 ? 1 : 9);
}

short __cdecl FakeChangeToAttack(Drone_tag *drone) {
    ShadowLog("changetoattack(%d)", Who(drone));
    return world.attackState;
}

void __cdecl FakeAnimDatum(obj_tag *obj, int datum, HASHCODE entity, uchar frames) {
    ShadowLog("animdatum(%d,%d,%x,%d)", WhoObj(obj), datum, (uint)entity, frames);
}

void __cdecl FakeDoHitEffects(DCVars_tag *dcv, HITDATA_tag *hit, float scale) {
    ShadowLog("hiteffects(%d,%d,%08x)", Who(dcv->drone), WhoObj((obj_tag *)hit), Bits(scale));
    dcv->drone->health = world.hitHealth;
}

bool __cdecl FakePainTalk(DCVars_tag *dcv) {
    ShadowLog("paintalk(%d)", Who(dcv->drone));
    return true;
}

bool __cdecl FakeExplosiveImpact(DCVars_tag *dcv, DroneImpact_tag *impact, uint nextState, char anim) {
    ShadowLog("explosiveimpact(%d,%d,%u,%d)", Who(dcv->drone), WhoObj((obj_tag *)impact), nextState, anim);
    return true;
}

bool __cdecl FakePunchImpact(DCVars_tag *dcv, HITDATA_tag *hit, uint nextState, char anim) {
    ShadowLog("punchimpact(%d,%d,%u,%d)", Who(dcv->drone), WhoObj((obj_tag *)hit), nextState, anim);
    return true;
}

bool __cdecl FakeBulletImpact(DCVars_tag *dcv, HITDATA_tag *hit, uint nextState, char anim) {
    ShadowLog("bulletimpact(%d,%d,%u,%d)", Who(dcv->drone), WhoObj((obj_tag *)hit), nextState, anim);
    return true;
}

bool __cdecl FakeInRange(DCVars_tag *dcv, float range) {
    ShadowLog("inrange(%d,%08x)", Who(dcv->drone), Bits(range));
    return world.inRange != 0;
}

bool __cdecl FakeCanMove(Drone_tag *drone, obj_tag *obj, float x, float y, float z) {
    ShadowLog("canmove(%d,%d,%08x,%08x,%08x)", Who(drone), WhoObj(obj), Bits(x), Bits(y), Bits(z));
    int bit = x > 0.0f ? 0 : x < 0.0f ? 1 : z < 0.0f ? 2 : 3;
    return (world.canMoveMask >> bit) & 1;
}

uint __cdecl FakeMoveToObject(DCVars_tag *dcv, obj_tag *obj, float radius, char follow) {
    ShadowLog("movetoobject(%d,%d,%08x,%d)", Who(dcv->drone), WhoObj(obj), Bits(radius), follow);
    return world.moveResult;
}

void __cdecl FakeFire(char restart, DCVars_tag *dcv) {
    ShadowLog("fire(%d,%d)", restart, Who(dcv->drone));
}

void __cdecl FakeInvalidateAttackRoute(DCVars_tag *dcv) {
    ShadowLog("invalidateattack(%d)", Who(dcv->drone));
}

void __cdecl FakeSetIdleTimeOut(DCVars_tag *dcv, int minSeconds, uint randSeconds) {
    ShadowLog("idletimeout(%d,%d,%u)", Who(dcv->drone), minSeconds, randSeconds);
}

bool __cdecl FakeAssignAIPath(DCVars_tag *dcv, uint pathType) {
    ShadowLog("assignpath(%d,%u)", Who(dcv->drone), pathType);
    dcv->drone->route2.count = world.pathCount;
    return true;
}

void __cdecl FakeAstroHit(DCVars_tag *dcv) {
    ShadowLog("astrohit(%d)", Who(dcv->drone));
}

void __cdecl FakeAstroDeath(DCVars_tag *dcv) {
    ShadowLog("astrodeath(%d)", Who(dcv->drone));
}

void __cdecl FakeAstroCombat(DCVars_tag *dcv) {
    ShadowLog("astrocombat(%d)", Who(dcv->drone));
}

void __cdecl FakeAstroAnim(DCVars_tag *dcv) {
    ShadowLog("astroanim(%d)", Who(dcv->drone));
}

bool __cdecl FakeDeathChannel(Drone_tag *drone) {
    ShadowLog("deathchannel(%d)", Who(drone));
    return true;
}

void __cdecl FakeLogDroneDeath(DCVars_tag *dcv) {
    ShadowLog("logdeath(%d)", Who(dcv->drone));
}

bool __cdecl FakeDeadDrone(DCVars_tag *dcv) {
    ShadowLog("deaddrone(%d)", Who(dcv->drone));
    return false;
}

void __cdecl FakeDropWeapon(DCVars_tag *dcv) {
    ShadowLog("dropweapon(%d)", Who(dcv->drone));
}

void __cdecl FakeChangeWeapon(DCVars_tag *dcv, int weapon) {
    ShadowLog("changeweapon(%d,%d)", Who(dcv->drone), weapon);
}

void __cdecl FakeLogDisabled(uint player) {
    ShadowLog("logdisabled(%u)", player);
}

void __cdecl FakeLogSurrender(uint player) {
    ShadowLog("logsurrender(%u)", player);
}

void __cdecl FakeLogDispatched(uint player) {
    ShadowLog("logdispatched(%u)", player);
}

uint __cdecl FakeRecoverTime(DCVars_tag *dcv, HITDATA_tag *hit) {
    ShadowLog("recovertime(%d,%d)", Who(dcv->drone), WhoObj((obj_tag *)hit));
    return world.recoverTime;
}

bool __cdecl FakeAnyoneNear(DCVars_tag *dcv, float radius) {
    ShadowLog("anyonenear(%d,%08x)", Who(dcv->drone), Bits(radius));
    return world.anyoneNear != 0;
}

bool __cdecl FakeCheckUnsurrender(DCVars_tag *dcv) {
    ShadowLog("checkunsurrender(%d)", Who(dcv->drone));
    return world.unsurrender != 0;
}

bool __cdecl FakeCanSee(Drone_tag *drone, obj_tag *obj, uint bone, uint mask) {
    ShadowLog("cansee(%d,%d,%u,%u)", Who(drone), WhoObj(obj), bone, mask);
    return world.canSee != 0;
}

bool __cdecl FakeDroneNearPos(_VECTOR *pos, float radius, obj_tag *except) {
    ShadowLog("dronenearpos(%08x,%08x,%d)", Bits(pos->x), Bits(radius), WhoObj(except));
    return world.droneNear != 0;
}

void __cdecl FakeInitDeath(DCVars_tag *dcv) {
    ShadowLog("initdeath(%d)", Who(dcv->drone));
}

bool __cdecl FakeSetScript(HASHCODE script, ushort animState, ushort nextRow, uchar blend, short endState,
                           uint endMsg, uint flags, DCVars_tag *dcv) {
    ShadowLog("setscript(%x,%x,%u,%u,%d,%u,%u,%d)", (uint)script, animState, nextRow, blend, endState, endMsg, flags,
              Who(dcv->drone));
    return script != 0;
}

void __cdecl FakeSlide(_VECTOR *pos, float speed, char turn, DCVars_tag *dcv) {
    ShadowLog("slide(%08x,%08x,%d,%d)", Bits(pos->x), Bits(speed), turn, Who(dcv->drone));
}

DYNAMICSOUNDS *__cdecl FakePlay3D(int sfx, _VECTOR *pos, float volume, float outer, float inner, int pitch, int a,
                                  int b) {
    ShadowLog("play3d(%x,%08x,%08x,%08x,%08x,%d,%d,%d)", sfx, Bits(pos->x), Bits(volume), Bits(outer), Bits(inner),
              (short)pitch, a, b);
    return NULL;
}

// The hooks each group needs (ShadowHarness has room for 96 entries, a port's counting twice)
struct HookSpec {
    unsigned at;
    const void *fake;
};

const HookSpec kCivilianHooks[] = {
    {0x0004e320, (const void *)&FakeSetState},       {0x000341c0, (const void *)&FakeCallAnim},
    {0x00035310, (const void *)&FakeStandIdle},      {0x00064d40, (const void *)&FakeNearArmed},
    {0x0003cf50, (const void *)&FakeBondTalk},       {0x00038f40, (const void *)&FakeAlertToObject},
    {0x0003e6a0, (const void *)&FakeHandleImpact},   {0x00038670, (const void *)&FakeConsiderExplosive},
    {0x0003b5d0, (const void *)&FakeForcedAttack},   {0x00065220, (const void *)&FakeEnemyAlerts},
    {0x00065360, (const void *)&FakeEnemyLook},      {0x00037000, (const void *)&FakeSetDronePartner},
    {0x000371d0, (const void *)&FakeDoorGuardHandler}, {0x00043c60, (const void *)&FakeSetAngleToObj},
    {0x00036270, (const void *)&FakeChooseSFX},      {0x00037530, (const void *)&FakeSpeechSFX},
    {0x00035fb0, (const void *)&FakeFailReason},     {0x00031d30, (const void *)&FakeAlertStatusSet},
    {0x000470f0, (const void *)&FakeFindAlarmPoint}, {0x00039f60, (const void *)&FakeSeenAndAttacking},
    {0x00039f50, (const void *)&FakeIsCivilian},     {0x00047190, (const void *)&FakeFindRunToPoint},
    {0x0003ec20, (const void *)&FakeFirstAttack},    {0x0003c6a0, (const void *)&FakeScaredTalk},
    {0x00046d40, (const void *)&FakeMissionRoute},   {0x000479f0, (const void *)&FakeInvalidateRoute},
    {0x000397f0, (const void *)&FakeChangeToAttack},
};

const HookSpec kNinjaHooks[] = {
    {0x0004e320, (const void *)&FakeSetState},       {0x000341c0, (const void *)&FakeCallAnim},
    {0x000135d0, (const void *)&FakeAnimDatum},      {0x0003d900, (const void *)&FakeDoHitEffects},
    {0x0003bd80, (const void *)&FakePainTalk},       {0x0003e3f0, (const void *)&FakeExplosiveImpact},
    {0x00035f50, (const void *)&FakeInRange},        {0x00045490, (const void *)&FakeCanMove},
    {0x00046d90, (const void *)&FakeMoveToObject},   {0x000675f0, (const void *)&FakeFire},
    {0x00043fa0, (const void *)&FakeInvalidateAttackRoute}, {0x00039f60, (const void *)&FakeSeenAndAttacking},
    {0x00043c60, (const void *)&FakeSetAngleToObj},
};

const HookSpec kAstronautHooks[] = {
    {0x0004e320, (const void *)&FakeSetState},       {0x000341c0, (const void *)&FakeCallAnim},
    {0x00035310, (const void *)&FakeStandIdle},      {0x00038e70, (const void *)&FakeSetIdleTimeOut},
    {0x0003e6a0, (const void *)&FakeHandleImpact},   {0x00046ad0, (const void *)&FakeAssignAIPath},
    {0x000675f0, (const void *)&FakeFire},           {0x00039f60, (const void *)&FakeSeenAndAttacking},
    {0x00043c60, (const void *)&FakeSetAngleToObj},  {0x00046d40, (const void *)&FakeMissionRoute},
    {0x00045230, (const void *)&FakeAstroHit},       {0x000452c0, (const void *)&FakeAstroDeath},
    {0x00045040, (const void *)&FakeAstroCombat},    {0x00044f70, (const void *)&FakeAstroAnim},
    {0x00031d30, (const void *)&FakeAlertStatusSet}, {0x00036090, (const void *)&FakeDeathChannel},
    {0x0003a1d0, (const void *)&FakeLogDroneDeath},  {0x00038db0, (const void *)&FakeDeadDrone},
};

const HookSpec kReactionHooks[] = {
    {0x0004e320, (const void *)&FakeSetState},       {0x000341c0, (const void *)&FakeCallAnim},
    {0x0003e6a0, (const void *)&FakeHandleImpact},   {0x0003e010, (const void *)&FakePunchImpact},
    {0x0003e220, (const void *)&FakeBulletImpact},   {0x0003e3f0, (const void *)&FakeExplosiveImpact},
    {0x00039f60, (const void *)&FakeSeenAndAttacking}, {0x00031d30, (const void *)&FakeAlertStatusSet},
    {0x00067690, (const void *)&FakeDropWeapon},     {0x00066b20, (const void *)&FakeChangeWeapon},
    {0x000b0670, (const void *)&FakeLogDisabled},    {0x000b06b0, (const void *)&FakeLogSurrender},
    {0x00036090, (const void *)&FakeDeathChannel},   {0x0003a4d0, (const void *)&FakeRecoverTime},
    {0x00039f50, (const void *)&FakeIsCivilian},     {0x00035fb0, (const void *)&FakeFailReason},
    {0x00038ea0, (const void *)&FakeAlertToPosition}, {0x00038f40, (const void *)&FakeAlertToObject},
    {0x00039990, (const void *)&FakeAnyoneNear},     {0x0003bd80, (const void *)&FakePainTalk},
    {0x00038ca0, (const void *)&FakeCheckUnsurrender}, {0x00043c60, (const void *)&FakeSetAngleToObj},
    {0x00038670, (const void *)&FakeConsiderExplosive},
};

const HookSpec kDeathHooks[] = {
    {0x0004e320, (const void *)&FakeSetState},       {0x00031d30, (const void *)&FakeAlertStatusSet},
    {0x0003de00, (const void *)&FakeInitDeath},      {0x00036090, (const void *)&FakeDeathChannel},
    {0x00034680, (const void *)&FakeSetScript},      {0x00043b20, (const void *)&FakeSlide},
    {0x00067690, (const void *)&FakeDropWeapon},     {0x000cb8c0, (const void *)&FakePlay3D},
};

const HookSpec kHelperHooks[] = {
    {0x00039870, (const void *)&FakeDroneNearPos},   {0x000648b0, (const void *)&FakeCanSee},
    {0x000b0630, (const void *)&FakeLogDispatched},
};

void InstallHooks(const HookSpec *hooks, int count) {
    ShadowHook_RemoveAll();
    for (int i = 0; i < count; i++)
        ShadowHook_Install(hooks[i].at, hooks[i].fake);
}

// ---- the made-up world

const HASHCODE kLevels[] = {
    HT_Level_HendersonA, HT_Level_CastleCourtyard, HT_Level_CastleIndoors1, HT_Level_TowerA, HT_Level_TowerB,
    HT_Level_TowerC, HT_Level_Tower2B, HT_Level_EvilBase,
};

template <typename T, size_t N> T Pick(const T (&values)[N]) {
    return values[ShadowRand(N)];
}

// Around the boundaries the states test (0, 2, 5, 6, 7, 12 m)
float RandomDistance() {
    static const float distances[] = {-1.0f,     0.0f,  1.0f,       2.0f, 2.0000002f, 3.0f, 5.0f, 5.0000005f, 6.0f,
                                      6.0000005f, 6.9999995f, 7.0f, 9.0f, 12.0f,      12.000001f, 30.0f};
    if (ShadowRand(16) == 0)
        return NAN;
    return Pick(distances);
}

float RandomAlertness() {
    static const float alertnesses[] = {0.0f, 0.5f, 0.65999997f, 0.66f, 0.66000003f, 1.0f};
    if (ShadowRand(12) == 0)
        return NAN;
    return Pick(alertnesses);
}

AnimState *RandomAnimState(bool allowNull) {
    if (allowNull && ShadowRand(4) == 0)
        return NULL;
    return &world.animStates[ShadowRand(3)];
}

void RandomObj(obj_tag *o, Drone_tag *d) {
    memset(o, 0, sizeof(*o));
    o->extraObjectData = d;
    o->objectType = OBJECTTYPE_DRONE;
    o->position.x = ShadowRandFloat(-3.0f, 3.0f);
    o->position.y = ShadowRandFloat(-4.0f, 4.0f);
    o->position.z = ShadowRandFloat(-3.0f, 3.0f);
    o->rotation.y = ShadowRandFloat(-3.2f, 3.2f);
    o->effectFlags = ShadowRand(4) << 4;
    o->tweakA = (uchar)ShadowRand(256);
    o->animState = RandomAnimState(true);
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    memset(d, 0, sizeof(*d));
    RandomObj(o, d);
    d->gameObj = o;

    static const short skinClasses[] = {0, 3, 13, 16, 0x13, 18};
    d->skinClass = Pick(skinClasses);
    static const uchar dtypes[] = {0, 2, 5, 7, 0xc, 0x11, 0x16, 0x1d};
    d->dtype = Pick(dtypes);
    d->dtype2 = Pick(dtypes);
    static const short animSets[] = {0, 3, 4, 5, 6, 9, 10, -5};
    d->animSet = Pick(animSets);
    static const short animStates[] = {0x31, 0x32, 0x2f, 0};
    d->animState = Pick(animStates);
    d->side = (uchar)(1 + ShadowRand(3));
    d->secondWeapon = (char)(ShadowRand(4) - 1);
    d->firing = (uchar)ShadowRand(2);
    d->burstOver = (uchar)ShadowRand(2);
    d->gunThroughWall = (uchar)ShadowRand(2);
    d->runningToAlarm = (uchar)ShadowRand(2);
    d->alertness = RandomAlertness();
    d->distanceToTarget = RandomDistance();
    for (int r = 0; r < 5; r++)
        d->combatRanges[r] = ShadowRandFloat(0.0f, 10.0f);
    static const float healths[] = {50.0f, 0.0f, 10.0f};
    d->health = Pick(healths);
    static const float facings[] = {0.0f, -0.0f, 1.5707963f, HALF_PI, -HALF_PI, 3.0f, NAN};
    d->opponentFacing = Pick(facings);
    d->opponent = ShadowRand(4) ? &world.player : NULL;
    static obj_tag *const attackers[] = {NULL, &world.player, &world.objs[1], &world.explosive};
    d->lastAttacker = Pick(attackers);
    static const uint sights[] = {0, 4, 0x46, 2};
    d->sightFlags = Pick(sights);
    static const uint aimed[] = {0, 1, 30, 31, 200};
    d->inPlayersSightsFrames = Pick(aimed);
    static const uint zones[] = {0, 1, 2, 3, 0xff};
    d->guardZone = Pick(zones);
    static const uint scratches[] = {0, 1, 2, 4, 0x24, 0x31, 0x32, 0xff};
    d->stateScratch = Pick(scratches);
    d->stateScratch2 = Pick(scratches);
    d->stateScratch3 = ShadowRand(1000);
    static const HASHCODE scripts[] = {(HASHCODE)0, HT_Script_DeathScriptIcyQuip, HT_Script_000300};
    d->deathScript = Pick(scripts);
    d->deathSwitchChannel = (uchar)ShadowRand(256);
    d->secondState = (short)(ShadowRand(3) == 0 ? 0 : ShadowRand(250));
    d->modeChangeSwitchChannel = (uchar)(ShadowRand(3) == 0 ? ShadowRand(256) : 0);
    d->initialState = (short)ShadowRand(250);
    d->returnState = (short)ShadowRand(250);
    d->route2.count = (ushort)(ShadowRand(2) ? 0 : 1 + ShadowRand(20));
    static const uchar statuses[] = {0, 1, 9, 0xc, 3};
    d->route2.status = Pick(statuses);
    d->diVars.rotation.y = ShadowRandFloat(-3.0f, 3.0f);
    d->aiPointRot.y = ShadowRandFloat(-3.0f, 3.0f);
    // The special death's tests: the script's start near the drone (1 m in plan) and its facing (90 degrees)
    static const float offsets[] = {0.5f, 0.99f, 1.0f, 2.0f};
    static const float turns[] = {0.0f, 1.5f, HALF_PI, -1.6f, 3.0f};
    d->scriptedMovePos = o->position;
    d->scriptedMovePos.x += Pick(offsets);
    d->scriptedMoveRot.y = o->rotation.y + Pick(turns);

    d->flags = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    d->knowledge = ShadowRand(0x10000);
    for (int w = 0; w < 3; w++) {
        d->behaviour1[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
        d->behaviour2[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    }
    d->currentBehaviour = ShadowRand(2) ? d->behaviour1 : d->behaviour2;
    behaviour_util_setProperty(DRONE_PROP_HURT_ALERTS, d->currentBehaviour, ShadowRand(2));
    behaviour_util_setProperty(DRONE_PROP_SIGHTING_ALERTS, d->currentBehaviour, ShadowRand(2));
    behaviour_util_setProperty(DRONE_PROP_LOOKS_AROUND, d->currentBehaviour, ShadowRand(2));

    d->sm.id = 100 + i;
    d->sm.curState = ShadowRand(250);
    d->sm.prevState = ShadowRand(250);
    d->sm.resumeState = ShadowRand(3) ? ShadowRand(250) : 0x18005;
    d->sm.scratch1c = ShadowRand(5);
    d->timerA.on = (uchar)ShadowRand(2);
    d->timerA.dueFrame = ShadowRand(5000);
    d->timerA.data = ShadowRand(5);
}

void RandomWorld(bool playerAlways) {
    for (int i = 0; i < DRONES; i++)
        RandomDrone(i);
    RandomObj(&world.player, NULL);
    world.player.objectType = OBJECTTYPE_PLAYER;
    world.player.animState = RandomAnimState(false);
    // Within reach of the dcv's drone now and then
    if (ShadowRand(2)) {
        world.player.position = world.objs[0].position;
        static const float across[] = {0.5f, 1.9f, 2.0f, 2.1f, 3.9f, 4.0f};
        static const float up[] = {0.0f, 2.9f, 3.0f, -3.0f, 3.1f};
        world.player.position.x += Pick(across);
        world.player.position.y += Pick(up);
    }
    RandomObj(&world.explosive, NULL);
    world.explosive.objectType = OBJECTTYPE_BULLET;
    for (int i = 0; i < 3; i++) {
        memset(&world.animStates[i], 0, sizeof(AnimState));
        static const char weapons[] = {0, 1, 2, 0x47, 0x6a, -1};
        world.animStates[i].currentWeaponId = Pick(weapons);
    }

    world.dcv.gameObj = &world.objs[0];
    world.dcv.drone = &world.drones[0];
    world.dcv.cel = NULL;
    world.dcv.aiStateMachine = &world.drones[0].sm;
    // Now and then a handler is given the other drone or object than the dcv's, to tell which one it reads
    world.droneArg = ShadowRand(5) ? &world.drones[0] : &world.drones[1];
    world.gameObjArg = ShadowRand(5) ? &world.objs[0] : &world.objs[1];
    static const float radii[] = {2.0f, 0.5f, 1.0f, 3.0f};
    world.floatArg = Pick(radii);

    world.setStateResult = ShadowRand(2);
    world.pathCount = (ushort)(ShadowRand(2) ? 0 : 1 + ShadowRand(9));
    world.armed = ShadowRand(2);
    static const ushort alerts[] = {0, 0, DSTATE_Attack, DSTATE_SeenOpponent, 0xffff, 0x8001};
    world.alertResult = Pick(alerts);
    world.lookResult = Pick(alerts);
    static const uint routes[] = {0, 1, 2, 3, 3, 4, 9, 10, 11, 0x103};
    world.missionResult = Pick(routes);
    world.moveResult = Pick(routes);
    static const short attacks[] = {0, DSTATE_Attack, DSTATE_Combat, -1, -0x7ff0};
    world.attackState = Pick(attacks);
    world.firstAttack = Pick(attacks);
    world.civilian = ShadowRand(2);
    world.findPoint = ShadowRand(2);
    static const uint zones[] = {0, 1, 2, 3, 4, 0xffffffff};
    world.guardZone = Pick(zones);
    world.sfx = 0x40 + ShadowRand(0x60);
    world.recoverTime = ShadowRand(3) ? ShadowRand(500) : 0xfffffff0u;
    world.inRange = ShadowRand(2);
    world.canMoveMask = ShadowRand(16);
    static const float hitHealths[] = {10.0f, 0.0f, -0.0f, -5.0f, 0.1f};
    world.hitHealth = ShadowRand(10) ? Pick(hitHealths) : NAN;
    world.anyoneNear = ShadowRand(2);
    world.unsurrender = ShadowRand(2);
    world.canSee = ShadowRand(2);
    world.droneNear = ShadowRand(2);

    static const uint frames[] = {1000, 12345, 0x7ffffff0, 0x80000010, 0xfffffff0};
    GameState.NumFramesUnpaused = Pick(frames) + ShadowRand(50);
    GameState.CurrentLevelHashcode = Pick(kLevels);
    if (ShadowRand(2)) {
        FRAME_RATE_INT = 60;
        FRAME_RATE_DIV = 1.0f;
    } else {
        FRAME_RATE_INT = 50;
        FRAME_RATE_DIV = 1.2f;
    }
    NPCGlobals.deadFrames = ShadowRand(2) ? 600 : 0xfffffff0u + ShadowRand(32);
    NPCGlobals.dummyPlayerActive = (uchar)(ShadowRand(4) == 0);
    NPCGlobals.dummyPlayer = &world.objs[1];
    static obj_tag *const players[] = {NULL, &world.player, &world.objs[1], &world.explosive};
    for (int i = 0; i < 4; i++)
        glb_players[i] = Pick(players);
    if (playerAlways)
        glb_players[0] = &world.player;
    MPSettings.dronesAreBots = ShadowRand(3) == 0;
    switch_BLIND_DRONES = ShadowRand(2);
    switch_channels[SWITCH_SPACE_DRAKE] = (char)ShadowRand(2);
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
    // Most of each handler is in its enter and update cases
    case 2:
        world.msg.msgType = DRONE_MSG_Enter;
        break;
    case 3:
        world.msg.msgType = DRONE_MSG_Update;
        break;
    default:
        world.msg.msgType = ShadowRand(0x24);
        break;
    }
    world.msg.sender = 100 + ShadowRand(DRONES);
    world.msg.receiver = 100;
    world.msg.createdFrame = GameState.NumFramesUnpaused;
    world.msg.handleOnFrame = GameState.NumFramesUnpaused;
    static void *const extras[] = {NULL, (void *)1, (void *)2, (void *)3, &world.explosive};
    world.msg.extraData = Pick(extras);
}

// ---- the handlers

struct Handler {
    const char *name;
    unsigned at;
    DroneStateFunc ours;
};

const Handler kCivilian[] = {
    {"NDrone2_DSTATE_CivilianGuard", 0x00053430, NDrone2_DSTATE_CivilianGuard},
    {"NDrone2_DSTATE_CivilianDoorGuard", 0x000536c0, NDrone2_DSTATE_CivilianDoorGuard},
    {"NDrone2_DSTATE_CivilianScared", 0x000508c0, NDrone2_DSTATE_CivilianScared},
    {"NDrone2_DSTATE_CivilianHiding", 0x0004b470, NDrone2_DSTATE_CivilianHiding},
    {"NDrone2_DSTATE_RunToAlarm", 0x0005b8f0, NDrone2_DSTATE_RunToAlarm},
    {"NDrone2_DSTATE_PressAlarm", 0x0004c890, NDrone2_DSTATE_PressAlarm},
    {"NDrone2_DSTATE_DonePressAlarm", 0x0005b990, NDrone2_DSTATE_DonePressAlarm},
    {"NDrone2_DSTATE_EnemyMission", 0x00051700, NDrone2_DSTATE_EnemyMission},
};

const Handler kNinja[] = {
    {"NDrone2_DSTATE_NinjaStand", 0x0005dc10, NDrone2_DSTATE_NinjaStand},
    {"NDrone2_DSTATE_NinjaAttack", 0x0005ddc0, NDrone2_DSTATE_NinjaAttack},
    {"NDrone2_DSTATE_NinjaAttackLongRange", 0x0005e220, NDrone2_DSTATE_NinjaAttackLongRange},
    {"NDrone2_DSTATE_NinjaAttackMidRange", 0x0005e4a0, NDrone2_DSTATE_NinjaAttackMidRange},
    {"NDrone2_DSTATE_NinjaAttackShortRange", 0x0005e830, NDrone2_DSTATE_NinjaAttackShortRange},
    {"NDrone2_DSTATE_NinjaGetCloseToPlayer", 0x0005ecb0, NDrone2_DSTATE_NinjaGetCloseToPlayer},
    {"NDrone2_DSTATE_NinjaSword", 0x0005f2c0, NDrone2_DSTATE_NinjaSword},
    {"NDrone2_DSTATE_NinjaSomersault", 0x0005f440, NDrone2_DSTATE_NinjaSomersault},
    {"NDrone2_DSTATE_NinjaBackflip", 0x0005f5c0, NDrone2_DSTATE_NinjaBackflip},
    {"NDrone2_DSTATE_NinjaSideflip", 0x0005eee0, NDrone2_DSTATE_NinjaSideflip},
    {"NDrone2_DSTATE_NinjaSideflipLeft", 0x0005efc0, NDrone2_DSTATE_NinjaSideflipLeft},
    {"NDrone2_DSTATE_NinjaSideflipRight", 0x0005f140, NDrone2_DSTATE_NinjaSideflipRight},
    {"NDrone2_DSTATE_NinjaStandFire", 0x0005f730, NDrone2_DSTATE_NinjaStandFire},
    {"NDrone2_DSTATE_NinjaNoRoute", 0x0005f9c0, NDrone2_DSTATE_NinjaNoRoute},
};

const Handler kAstronaut[] = {
    {"NDrone2_DSTATE_SpaceDrake", 0x00060230, NDrone2_DSTATE_SpaceDrake},
    {"NDrone2_DSTATE_AstronautLaunch", 0x0005fd70, NDrone2_DSTATE_AstronautLaunch},
    {"NDrone2_DSTATE_AstronautHit", 0x00060030, NDrone2_DSTATE_AstronautHit},
    {"NDrone2_DSTATE_AstronautDeath", 0x00060140, NDrone2_DSTATE_AstronautDeath},
    {"NDrone2_DSTATE_AstronautCombat", 0x0004dc00, NDrone2_DSTATE_AstronautCombat},
    {"NDrone2_DSTATE_AstronautCombatMove", 0x0005fef0, NDrone2_DSTATE_AstronautCombatMove},
};

const Handler kReactions[] = {
    {"NDrone2_DSTATE_PunchImpact", 0x0004bcd0, NDrone2_DSTATE_PunchImpact},
    {"NDrone2_DSTATE_ExplosiveImpact", 0x0004bdb0, NDrone2_DSTATE_ExplosiveImpact},
    {"NDrone2_DSTATE_BulletImpact", 0x000570e0, NDrone2_DSTATE_BulletImpact},
    {"NDrone2_DSTATE_KnockedOut_Anim", 0x00055830, NDrone2_DSTATE_KnockedOut_Anim},
    {"NDrone2_DSTATE_Knocked_Out", 0x00055940, NDrone2_DSTATE_Knocked_Out},
    {"NDrone2_DSTATE_Taser", 0x000559b0, NDrone2_DSTATE_Taser},
    {"NDrone2_DSTATE_Stunned", 0x00055c50, NDrone2_DSTATE_Stunned},
    {"NDrone2_DSTATE_Stunned_Recover", 0x00055e90, NDrone2_DSTATE_Stunned_Recover},
    {"NDrone2_DSTATE_StunGrenadeImpact", 0x00055fc0, NDrone2_DSTATE_StunGrenadeImpact},
    {"NDrone2_DSTATE_StunGrenadeLoop", 0x000560c0, NDrone2_DSTATE_StunGrenadeLoop},
    {"NDrone2_DSTATE_StunGrenadeRecover", 0x000562a0, NDrone2_DSTATE_StunGrenadeRecover},
    {"NDrone2_DSTATE_StunDartImpact", 0x000563d0, NDrone2_DSTATE_StunDartImpact},
    {"NDrone2_DSTATE_StunDartLoop", 0x000564e0, NDrone2_DSTATE_StunDartLoop},
    {"NDrone2_DSTATE_StunDartRecover", 0x000566c0, NDrone2_DSTATE_StunDartRecover},
    {"NDrone2_DSTATE_SmokedOut", 0x0004d640, NDrone2_DSTATE_SmokedOut},
    {"NDrone2_DSTATE_SmokedOut_Loop", 0x0005dab0, NDrone2_DSTATE_SmokedOut_Loop},
    {"NDrone2_DSTATE_SmokedOut_Recover", 0x0004d730, NDrone2_DSTATE_SmokedOut_Recover},
    {"NDrone2_DSTATE_Surrender_Anim", 0x000554a0, NDrone2_DSTATE_Surrender_Anim},
    {"NDrone2_DSTATE_Surrendered", 0x000556f0, NDrone2_DSTATE_Surrendered},
    {"NDrone2_DSTATE_Unsurrender_Anim", 0x0004b9e0, NDrone2_DSTATE_Unsurrender_Anim},
};

const Handler kDeath[] = {
    {"NDrone2_DSTATE_SpecialDeath_Anim", 0x00056bd0, NDrone2_DSTATE_SpecialDeath_Anim},
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

// cases and differs of the group run
void TestHandlers(const Handler *handlers, int count, int trials, bool playerAlways, int *groupCases,
                  int *groupDiffer) {
    int before = cases, beforeDiffer = differ;
    for (int h = 0; h < count; h++) {
        current = &handlers[h];
        for (int trial = 0; trial < trials; trial++) {
            RandomWorld(playerAlways);
            RandomMessage();
            cases++;
            if (!ShadowCompare(regions, "dronereact", current->name, trial, RunTheirs, RunOurs))
                differ++;
        }
    }
    *groupCases = cases - before;
    *groupDiffer = differ - beforeDiffer;
}

// The helpers, theirs and ours
uint32_t AnyoneNearTheirs() {
    XbeOriginalScope o(0x00039990);
    return ORIGINAL(0x00039990, uint32_t(__cdecl *)(DCVars_tag *, float))(&world.dcv, world.floatArg) & 0xff;
}
uint32_t AnyoneNearOurs() {
    return (uint32_t)DroneFunc_AnyoneNear(&world.dcv, world.floatArg);
}
uint32_t UnsurrenderTheirs() {
    XbeOriginalScope o(0x00038ca0);
    return ORIGINAL(0x00038ca0, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
}
uint32_t UnsurrenderOurs() {
    return (uint32_t)NDrone2_CheckUnsurrender(&world.dcv);
}
uint32_t LogDeathTheirs() {
    XbeOriginalScope o(0x0003a1d0);
    ORIGINAL(0x0003a1d0, void(__cdecl *)(DCVars_tag *))(&world.dcv);
    return 0;
}
uint32_t LogDeathOurs() {
    NDrone2_LogDroneDeath(&world.dcv);
    return 0;
}

struct HelperTest {
    const char *name;
    uint32_t (*theirs)(void);
    uint32_t (*ours)(void);
};

const HelperTest kHelpers[] = {
    {"DroneFunc_AnyoneNear", AnyoneNearTheirs, AnyoneNearOurs},
    {"NDrone2_CheckUnsurrender", UnsurrenderTheirs, UnsurrenderOurs},
    {"NDrone2_LogDroneDeath", LogDeathTheirs, LogDeathOurs},
};

} // namespace

void DroneReactShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], npc[sizeof(NPCGlobals_t)], mp[sizeof(MPSettings_t)];
    static char channels[256];
    static uint32_t rand[2];
    static obj_tag *players[4];
    uint32_t frameRate = FRAME_RATE_INT;
    float frameDiv = FRAME_RATE_DIV;
    uint32_t blind = switch_BLIND_DRONES;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(mp, &MPSettings, sizeof(mp));
    memcpy(channels, switch_channels, sizeof(channels));
    XbeOursRead(rand, RandWords.at, sizeof(rand));
    memcpy(players, glb_players, sizeof(players));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(&MPSettings, sizeof(MPSettings_t));
    regions.Add(switch_channels, 256);
    regions.Add(RandWords.game(), 8);
    regions.Add(glb_players, sizeof(players));
    regions.Add(&FRAME_RATE_INT, 4);
    regions.Add(&FRAME_RATE_DIV, 4);
    regions.Add(&switch_BLIND_DRONES, 4);

    NfWarnMuted++;
    cases = differ = 0;

    int civilianCases, civilianDiffer;
    InstallHooks(kCivilianHooks, sizeof(kCivilianHooks) / sizeof(kCivilianHooks[0]));
    TestHandlers(kCivilian, sizeof(kCivilian) / sizeof(kCivilian[0]), 6000, false, &civilianCases, &civilianDiffer);

    int ninjaCases, ninjaDiffer;
    InstallHooks(kNinjaHooks, sizeof(kNinjaHooks) / sizeof(kNinjaHooks[0]));
    TestHandlers(kNinja, sizeof(kNinja) / sizeof(kNinja[0]), 6000, false, &ninjaCases, &ninjaDiffer);

    int astronautCases, astronautDiffer;
    InstallHooks(kAstronautHooks, sizeof(kAstronautHooks) / sizeof(kAstronautHooks[0]));
    TestHandlers(kAstronaut, sizeof(kAstronaut) / sizeof(kAstronaut[0]), 4000, false, &astronautCases,
                 &astronautDiffer);

    int reactionCases, reactionDiffer;
    InstallHooks(kReactionHooks, sizeof(kReactionHooks) / sizeof(kReactionHooks[0]));
    TestHandlers(kReactions, sizeof(kReactions) / sizeof(kReactions[0]), 4000, false, &reactionCases,
                 &reactionDiffer);

    // The original reads the player's position without checking for one
    int deathCases, deathDiffer;
    InstallHooks(kDeathHooks, sizeof(kDeathHooks) / sizeof(kDeathHooks[0]));
    TestHandlers(kDeath, sizeof(kDeath) / sizeof(kDeath[0]), 8000, true, &deathCases, &deathDiffer);

    int helperCases = 0, helperDiffer = 0;
    InstallHooks(kHelperHooks, sizeof(kHelperHooks) / sizeof(kHelperHooks[0]));
    for (const HelperTest &test : kHelpers) {
        for (int trial = 0; trial < 6000; trial++) {
            RandomWorld(false);
            // NDrone2_CheckUnsurrender reads the opponent's weapon without checking for an anim state
            world.player.animState = &world.animStates[ShadowRand(3)];
            helperCases++;
            if (!ShadowCompare(regions, "dronereact", test.name, trial, test.theirs, test.ours))
                helperDiffer++;
        }
    }

    ShadowHook_RemoveAll();
    NfWarnMuted--;
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(&MPSettings, mp, sizeof(mp));
    memcpy(switch_channels, channels, sizeof(channels));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
    memcpy(glb_players, players, sizeof(players));
    FRAME_RATE_INT = frameRate;
    FRAME_RATE_DIV = frameDiv;
    switch_BLIND_DRONES = blind;

    printf("[dronereact] civilian guard, scared, hiding, alarm and enemy mission states: %d cases, %d differ\n",
           civilianCases, civilianDiffer);
    printf("[dronereact] ninja states: %d cases, %d differ\n", ninjaCases, ninjaDiffer);
    printf("[dronereact] space drake and astronaut states: %d cases, %d differ\n", astronautCases, astronautDiffer);
    printf("[dronereact] impact, knock-out, taser, stun, smoke and surrender states: %d cases, %d differ\n",
           reactionCases, reactionDiffer);
    printf("[dronereact] special death: %d cases, %d differ\n", deathCases, deathDiffer);
    printf("[dronereact] AnyoneNear, CheckUnsurrender, LogDroneDeath: %d cases, %d differ\n", helperCases,
           helperDiffer);
}
