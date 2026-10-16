// Shadow tests for what hits a drone (NDrone2_Damage.cpp), the hurt volumes' Hurt_GetDamage and Hurt_GetType,
// DroneAnim_LocationImpactAnim and the talk on being hit (NDrone2_Talk.cpp): each original and ours run on the same
// made-up drones. Run at start with MenuShadowTests=on (MenuProbe.cpp), before any level exists.
//
// The originals run together (every one of these swapped back at once), so an original reaches the other originals
// and ours reach ours. What needs a level or the rest of the AI goes to logging fakes: speech and sounds, the smoke
// (Gas_Init), the ripples, the bots' pain and hits, the alerts to other drones, messages and state changes
// (Drone_Message, Drone_SM_SetState), Drone_AlertStatusSet, the mission's fail label, the player's stats, which player
// an object is (Control_Plr2Ind), the game flow and whether a drone is armed. DroneAnim_CallAnim runs for real (the
// original LocationImpactAnim has it inlined), with DroneAnim_SetEndAIState faked. Not tested here: what the faked
// functions themselves do, and the impact anims' scripts (only the anim call's fields are compared).
//
// Random draws are kept in step: both sides start from the same Rand_Random state, and it is compared afterwards.

#include "DroneDamageShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../engine/Collide.h"
#include "../game/obj/bullet.h"
#include "../game/obj/Hurt.h"
#include "../game/drone/BOT.h"
#include "../game/drone/Behaviour.h"
#include "../game/drone/DroneAnim.h"
#include "../game/drone/DroneTables.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/NDrone2_Damage.h"
#include "../game/mp/multiplayer.h"
#include "../game/sp/SwitchChannels.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

// The game's RNG state (Rand_Random)
#define RandWords ((uint32_t *)0x0018cdf8)
// The Assassin mode's two objects (MP_IsAssasin, MP_IsTarget)
#define AssassinGlobals ((obj_tag **)0x00261788)
// The drone damage and armour tuning (NDrone2_HitDamage), 11 floats
#define DroneTuning ((float *)0x00164068)

const int DRONES = 4;

struct World {
    obj_tag objs[DRONES];
    Drone_tag drones[DRONES];
    AnimState anims[DRONES];
    BOT_vars_t bots[2];
    obj_tag player;
    AnimState playerAnim;
    obj_tag shotObj;
    BU_tag bullet;
    weapon_definition_tag wpn;
    obj_tag hurtObj;
    Hurt_tag hurt;
    obj_tag otherObj;
    obj_tag gasObj;
    HITDATA_tag hit;
    DroneImpact_tag impact;
    uint8_t sound[0x40];
    uint8_t cel[0x40];
    DCVars_tag dcv;
    MsgObject msg;

    // What the fakes answer
    int speechResult;
    int soundFinished;
    int disallowed;
    int armed;
    int setStateResult;
    int messageResult;
    short selfIndex;
    short shooterIndex;
    uint gameFlow;
};
World world;

ShadowRegions regions;
int cases, differ;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "dronedamage", what, trial, theirs, ours))
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
    return drone == NULL ? -1 : 99;
}

int WhoSm(const StateMachineInfo_tag *sm) {
    for (int i = 0; i < DRONES; i++)
        if (sm == &world.drones[i].sm)
            return i;
    return sm == NULL ? -1 : 99;
}

int WhoObj(const obj_tag *obj) {
    for (int i = 0; i < DRONES; i++)
        if (obj == &world.objs[i])
            return i;
    if (obj == &world.player)
        return 100;
    if (obj == &world.shotObj)
        return 101;
    if (obj == &world.hurtObj)
        return 102;
    if (obj == &world.otherObj)
        return 103;
    if (obj == &world.gasObj)
        return 104;
    return obj == NULL ? -1 : 199;
}

int WhoData(const void *p) {
    if (p == &world.hit)
        return 1;
    if (p == &world.impact)
        return 2;
    return p == NULL ? 0 : 3;
}

// ---- the fakes

int __cdecl FakeSpeech(DCVars_tag *dcv, uint sfx, char force, char face, char needFace) {
    ShadowLog("speech(%d,%u,%d,%d,%d)", Who(dcv->drone), sfx, force, face, needFace);
    return world.speechResult;
}

bool __cdecl FakeFinished(void *handle) {
    ShadowLog("finished(%p)", handle);
    return world.soundFinished != 0;
}

bool __cdecl FakeDisallowed(Drone_tag *drone, uint sfx) {
    ShadowLog("disallowed(%d,%u)", Who(drone), sfx);
    return ((sfx + world.disallowed) & 3) == 0;
}

DYNAMICSOUNDS *__cdecl FakePlay3D(Action_SFX sfx, _VECTOR *pos, float volume, float outer, float inner, ushort pitch,
                                  uint param7, int limited) {
    ShadowLog("play3d(%d,%08x,%08x,%08x,%08x,%08x,%08x,%u,%u,%d)", sfx, Bits(pos->x), Bits(pos->y), Bits(pos->z),
              Bits(volume), Bits(outer), Bits(inner), pitch, param7, limited);
    return NULL;
}

void __cdecl FakeRipple(HITDATA_tag *hit) {
    ShadowLog("ripple(%d)", WhoData(hit));
}

obj_tag *__cdecl FakeGas(cel_tag *cel, _VECTOR *pos, _VECTOR *rot, _VECTOR *wind, float a, float b, HASHCODE gfx,
                         uint type, uint colourStart, uint colourEnd, ushort lifetime) {
    ShadowLog("gas(%p,%08x,%08x,%08x,%p,%p,%08x,%08x,%x,%u,%x,%x,%u)", cel, Bits(pos->x), Bits(pos->y), Bits(pos->z), rot,
              wind, Bits(a), Bits(b), gfx, type, colourStart, colourEnd, lifetime);
    return &world.gasObj;
}

void __cdecl FakeBotSound(DCVars_tag *dcv, int sound) {
    ShadowLog("botsound(%d,%d)", Who(dcv->drone), sound);
}

void __cdecl FakeBotPain(DCVars_tag *dcv, float damage, ushort unknown, short location) {
    ShadowLog("botpain(%d,%08x,%u,%d)", Who(dcv->drone), Bits(damage), unknown, location);
}

bool __cdecl FakeRegisterHit(uint player, obj_tag *obj, float health) {
    ShadowLog("registerhit(%u,%d,%08x)", player, WhoObj(obj), Bits(health));
    return true;
}

void __cdecl FakeAlertToPosition(obj_tag *obj, uint msgType, _VECTOR *pos, cel_tag *cel, int a, int b, int c) {
    ShadowLog("alerttopos(%d,%x,%08x,%08x,%08x,%p,%d,%d,%d)", WhoObj(obj), msgType, Bits(pos->x), Bits(pos->y),
              Bits(pos->z), cel, a, b, c);
}

void __cdecl FakeFailLabel(uint label) {
    ShadowLog("faillabel(%x)", label);
}

void __cdecl FakeLogShot(uint player) {
    ShadowLog("logshot(%u)", player);
}

short __cdecl FakePlr2Ind(obj_tag *obj) {
    ShadowLog("plr2ind(%d)", WhoObj(obj));
    if (obj == &world.objs[0])
        return world.selfIndex;
    if (obj == &world.objs[1])
        return world.shooterIndex;
    if (obj == &world.player)
        return 0;
    return -1;
}

uint __stdcall FakeGameFlow(void) {
    ShadowLog("gameflow()");
    return world.gameFlow;
}

bool __cdecl FakeArmed(obj_tag *obj) {
    ShadowLog("armed(%d)", WhoObj(obj));
    return world.armed != 0;
}

void __cdecl FakeAlertStatus(char status, DCVars_tag *dcv) {
    ShadowLog("alertstatus(%d,%d)", status, Who(dcv->drone));
}

bool __cdecl FakeSetState(StateMachineInfo_tag *sm, uint state, int param) {
    ShadowLog("setstate(%d,%u,%d)", WhoSm(sm), state, WhoData((void *)(intptr_t)param));
    return world.setStateResult != 0;
}

bool __cdecl FakeMessage(obj_tag *obj, uint msgType, void *extraData, uint delay) {
    ShadowLog("message(%d,%x,%d,%u)", WhoObj(obj), msgType, WhoData(extraData), delay);
    return world.messageResult != 0;
}

void __cdecl FakeSetEndAIState(Drone_tag *drone, short state, uint msgType) {
    ShadowLog("setendaistate(%d,%d,%u)", Who(drone), state, msgType);
}

void InstallFakes() {
    ShadowHook_Install(0x00037530, (const void *)&FakeSpeech);
    ShadowHook_Install(0x000cbac0, (const void *)&FakeFinished);
    ShadowHook_Install(0x00036230, (const void *)&FakeDisallowed);
    ShadowHook_Install(0x000cb8c0, (const void *)&FakePlay3D);
    ShadowHook_Install(0x00068f20, (const void *)&FakeRipple);
    ShadowHook_Install(0x000cd140, (const void *)&FakeGas);
    ShadowHook_Install(0x0001aca0, (const void *)&FakeBotSound);
    ShadowHook_Install(0x0001b6b0, (const void *)&FakeBotPain);
    ShadowHook_Install(0x0009d700, (const void *)&FakeRegisterHit);
    ShadowHook_Install(0x00038ea0, (const void *)&FakeAlertToPosition);
    ShadowHook_Install(0x0009bdb0, (const void *)&FakeFailLabel);
    ShadowHook_Install(0x000b06f0, (const void *)&FakeLogShot);
    ShadowHook_Install(0x0002d8a0, (const void *)&FakePlr2Ind);
    ShadowHook_Install(0x0006aa70, (const void *)&FakeGameFlow);
    ShadowHook_Install(0x00064be0, (const void *)&FakeArmed);
    ShadowHook_Install(0x00031d30, (const void *)&FakeAlertStatus);
    ShadowHook_Install(0x0004e320, (const void *)&FakeSetState);
    ShadowHook_Install(0x00031830, (const void *)&FakeMessage);
    ShadowHook_Install(0x00032e70, (const void *)&FakeSetEndAIState);
}

// Every original under test at once, so they reach each other rather than ours
struct OriginalDamage {
    XbeOriginalScope hurtDamage{0x0006c820}, hurtType{0x0006c850}, dealWithObjHit{0x000364f0},
        bulletHit{0x00031f20}, hitDamage{0x0003d670}, doHitEffects{0x0003d900}, sendHurt{0x0003a370},
        punch{0x0003e010}, bullet{0x0003e220}, explosive{0x0003e3f0}, handleImpact{0x0003e6a0},
        impactAnim{0x00035000}, hitTalk{0x0003bc90}, painTalk{0x0003bd80}, injuredTalk{0x0003c7f0};
};

// NDrone2_DealWithObjHit takes the drone in EAX: target is the original or our adaptor
__declspec(naked) void __cdecl CallDealWithObjHit(unsigned target, Drone_tag *drone, obj_tag *obj, HITDATA_tag *hit) {
    _asm {
        mov eax, [esp + 8]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        call dword ptr [esp + 12]
        add esp, 8
        ret
    }
}

// ---- the made-up world

const HASHCODE kLevels[] = {
    HT_Level_HendersonA, HT_Level_HendersonD, HT_Level_CastleExterior, HT_Level_CastleIndoors1, HT_Level_TowerA,
    HT_Level_TowerB, HT_Level_TowerC, HT_Level_PowerStationA1, HT_Level_PowerStationA2, HT_Level_Tower2A,
    HT_Level_Tower2B, HT_Level_Tower2C, HT_Level_Tower2Elevator, HT_Level_EvilBase, HT_Level_EvilSilo,
    HT_Level_EvilBaseC, HT_Level_SpaceStationD, HT_Level_Cut_Level1,
};

// States the functions single out, the impact states and their neighbours, and some others
const uint kStates[] = {
    0, 0, DSTATE_Idle, 0x52, DSTATE_PunchImpact, DSTATE_ExplosiveImpact, DSTATE_BulletImpact, DSTATE_Attack,
    DSTATE_AmbushWait, DSTATE_UnderCoverIdle, DSTATE_Taser, DSTATE_Combat, DSTATE_BotDeathAnim, DSTATE_BotDead,
    DSTATE_BotIdle, DSTATE_BotImpactBullet, 300,
};

uint RandomState() {
    return kStates[ShadowRand(sizeof(kStates) / sizeof(kStates[0]))];
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

uint32_t RandomWord() {
    return ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    memset(&world.anims[i], 0, sizeof(world.anims[i]));
    world.anims[i].animFlags = (char)ShadowRand(0x20);

    d->prev = i > 0 ? &world.drones[i - 1] : NULL;
    d->next = i + 1 < DRONES ? &world.drones[i + 1] : NULL;
    d->gameObj = o;
    o->extraObjectData = d;
    o->animState = &world.anims[i];
    o->objectType = ShadowRand(8) ? OBJECTTYPE_DRONE : OBJECTTYPE_DEAD_DRONE;
    o->inCel = (cel_tag *)world.cel;
    o->flags = (char)(ShadowRand(6) == 0);
    RandomVector(&o->position, 8.0f);
    o->rotation.y = ShadowRandFloat(-4.0f, 4.0f);
    for (int k = 0; k < 15; k++)
        o->transformMatrix.m[k] = ShadowRandFloat(-1, 1);
    o->transformMatrix.m[0] += 2.0f;
    o->transformMatrix.m[5] += 2.0f;
    o->transformMatrix.m[10] += 2.0f;

    d->vulnerable = (uchar)(ShadowRand(4) != 0);
    d->impactImmune = (uchar)(ShadowRand(8) == 0);
    d->unknown15 = (uchar)ShadowRand(2);
    d->unknown16 = (uchar)ShadowRand(2);
    d->side = (uchar)ShadowRand(5);
    static const float healths[] = {-1.0f, 0.0f, -0.0f, 0.4f, 3.5f, 20.0f, 50.0f, 100.0f, 160.0f};
    d->health = healths[ShadowRand(sizeof(healths) / 4)];
    static const float starts[] = {100.0f, 50.0f, 10.0f, 200.0f};
    d->startHealth = starts[ShadowRand(4)];
    d->armour = (uchar)ShadowRand(16);
    static const uchar dtypes[] = {DTYPE_Normal, DTYPE_Guard, DTYPE_Sniper, DTYPE_Civilian, DTYPE_Ninja, DTYPE_Ambush,
                                   DTYPE_Abseil27, DTYPE_Abseil28, DTYPE_Astronaut, DTYPE_Bot};
    d->dtype = dtypes[ShadowRand(sizeof(dtypes))];
    d->skinClass = (short)(ShadowRand(4) == 0 ? (ShadowRand(2) ? 12 : 16) : ShadowRand(22));
    d->animSet = (short)(ShadowRand(3) == 0 ? 5 : ShadowRand(NUM_DRONE_ANIM_SETS));
    d->skinHashcode = (HASHCODE)(ShadowRand(5) == 0 ? 0x50000ba : 0x5000020);
    static const float scales[] = {1.0f, 0.5f, 2.5f};
    d->hitDamageScale = scales[ShadowRand(3)];
    d->lastDamage = RandomFloat(0.0f, 80.0f);
    static const ushort stuns[] = {0, 0, 1, 2, 7, 40, 199, 200, 201, 320, 0xffff};
    d->stunFrames = stuns[ShadowRand(sizeof(stuns) / 2)];
    d->hitsTaken = (ushort)ShadowRand(5);
    d->unknown3d0 = GameState.NumFramesUnpaused - ShadowRand(400);
    d->impactAnimFrame = GameState.NumFramesUnpaused - ShadowRand(300);
    d->speechChannel = ShadowRand(3) == 0 ? (DYNAMICSOUNDS *)world.sound : NULL;

    d->sm.id = 100 + i;
    d->sm.curState = RandomState();
    d->sm.prevState = RandomState();
    d->sm.resumeState = RandomState();

    for (int w = 0; w < 3; w++) {
        d->behaviour1[w] = RandomWord();
        d->behaviour2[w] = RandomWord();
    }
    d->currentBehaviour = ShadowRand(2) ? d->behaviour1 : d->behaviour2;
    static const uint flagBits[] = {8, 0x100, 0x200, 0x2000000, 0x4000000};
    for (int k = 0; k < 5; k++)
        if (ShadowRand(3) == 0)
            d->flags |= flagBits[k];
    d->flags2 = ShadowRand(8);
    d->usingSecondBehaviour = (uchar)ShadowRand(2);
    d->secondState = (short)RandomState();
    d->initialState = (short)RandomState();
    d->dtype2 = (uchar)ShadowRand(30);
    static const uint modes[] = {100, 100, 0, 5};
    d->diVars.keys.mode = modes[ShadowRand(4)];
    d->lastHit = &world.hit;
    d->botVars = &world.bots[i & 1];
    static obj_tag *const attackers[] = {&world.player, &world.objs[1], NULL};
    d->lastAttacker = attackers[ShadowRand(3)];
    d->opponent = attackers[ShadowRand(3)];

    d->animState = (short)(ShadowRand(5) == 0 ? DANIM_IMPACT : ShadowRand(0x77));
    d->animRow = (ushort)ShadowRand(NUM_DRONE_ANIM_ROWS);
    d->animCall.state = (ushort)(ShadowRand(4) == 0 ? DANIM_IMPACT : ShadowRand(0x77));
    d->animCall.variant = (ushort)ShadowRand(14);
    d->animEndState = (short)(ShadowRand(2) ? 0 : RandomState());
    d->animEndMsg = 0;
    d->animCallSpeed = 0.0f;
}

void RandomWorld() {
    GameState.NumFramesUnpaused = 1000 + ShadowRand(100);
    for (int i = 0; i < DRONES; i++)
        RandomDrone(i);
    for (int b = 0; b < 2; b++) {
        memset(&world.bots[b], 0, sizeof(world.bots[b]));
        world.bots[b].stats.traitFlags = (uchar)ShadowRand(8);
        world.bots[b].playerIndex = (short)ShadowRand(10);
    }

    memset(&world.player, 0, sizeof(world.player));
    world.player.objectType = OBJECTTYPE_PLAYER;
    world.player.animState = &world.playerAnim;
    memset(&world.playerAnim, 0, sizeof(world.playerAnim));
    world.playerAnim.animFlags = (char)ShadowRand(4);
    RandomVector(&world.player.position, 6.0f);

    memset(&world.otherObj, 0, sizeof(world.otherObj));
    static const char others[] = {0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x28, OBJECTTYPE_GUNTURRET, 1, 0x50, 0x1c, 0x27};
    world.otherObj.objectType = others[ShadowRand(sizeof(others))];
    world.otherObj.extraObjectData = &world.hurt;
    memset(&world.gasObj, 0, sizeof(world.gasObj));
    world.gasObj.effectFlags = ShadowRand(4);

    memset(&world.hurtObj, 0, sizeof(world.hurtObj));
    world.hurtObj.objectType = ShadowRand(6) ? OBJECTTYPE_HURT : OBJECTTYPE_TRIGGER;
    world.hurtObj.extraObjectData = &world.hurt;
    memset(&world.hurt, 0, sizeof(world.hurt));
    world.hurt.damage = RandomFloat(0.0f, 60.0f);
    world.hurt.type = (uchar)ShadowRand(256);

    memset(&world.shotObj, 0, sizeof(world.shotObj));
    world.shotObj.objectType = OBJECTTYPE_BULLET;
    world.shotObj.extraObjectData = ShadowRand(10) ? &world.bullet : NULL;
    memset(&world.bullet, 0, sizeof(world.bullet));
    memset(&world.wpn, 0, sizeof(world.wpn));
    world.bullet.wpnDef = &world.wpn;
    static obj_tag *const shooters[] = {NULL, &world.player, &world.objs[1], &world.objs[2], &world.otherObj};
    world.bullet.firedByObj = shooters[ShadowRand(5)];
    static const short weapons[] = {1,    0x1b, 0x1c, 0x2a, 0x2b, 0x2c, 0x2e, 0x32, 0x33, 0x34, 0x37, 0x3a, 0x43, 0x44,
                                    0x46, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x5f, 0x64, 0x65, 0x66, 0x6e, 0x20, 0x10, 0x60};
    world.wpn.weaponVariantNum = weapons[ShadowRand(sizeof(weapons) / 2)];
    static const float damages[] = {0.0f, 10.0f, -1.0f, 25.0f};
    world.wpn.damage = damages[ShadowRand(4)];
    static const uint impactFlags[] = {0, 0, 0x80, 0x100, 0x200};
    world.wpn.impactFlags = impactFlags[ShadowRand(5)];

    memset(&world.hit, 0, sizeof(world.hit));
    world.hit.materialType = (char)(ShadowRand(3) == 0 ? 13 + ShadowRand(2) + (ShadowRand(2) << 6) : ShadowRand(256));
    static const short bones[] = {-1, 0,    1,    2,    3,    4,    5,    5,    6,    0x12, 0x13, 0x15, 0x16, 0x14,
                                  0x17, 0x1e, 0x1f, 0x20, 0x21, 0x23, 0x24, 0x27, 0x31, 0x33, 0x34, 0x35, 0x37, 0x38,
                                  0x39};
    world.hit.hitBoneIdx = bones[ShadowRand(sizeof(bones) / 2)];
    static const float hitDamages[] = {0.0f, -0.0f, -1.0f, 0.3f, 0.5f, 0.6f, 12.0f, 50.0f};
    world.hit.dmgAmt = hitDamages[ShadowRand(sizeof(hitDamages) / 4)];
    RandomVector(&world.hit.maybehitDirection, 1.0f);
    RandomVector(&world.hit.hitPosition, 6.0f);
    static obj_tag *const hitObjs[] = {NULL,           &world.shotObj, &world.shotObj, &world.shotObj, &world.shotObj,
                                       &world.player,  &world.hurtObj, &world.objs[1], &world.otherObj};
    world.hit.hitObj = hitObjs[ShadowRand(sizeof(hitObjs) / sizeof(hitObjs[0]))];

    memset(&world.impact, 0, sizeof(world.impact));
    static const float impactDamages[] = {0.0f, -1.0f, 5.0f, 40.0f, 200.0f};
    world.impact.damage = impactDamages[ShadowRand(5)];
    RandomVector(&world.impact.position, 6.0f);
    static obj_tag *const sources[] = {&world.objs[0], &world.objs[1], NULL};
    world.impact.unknown14 = sources[ShadowRand(3)];

    memset(world.sound, 0, sizeof(world.sound));

    world.speechResult = ShadowRand(3) ? 1 : 0;
    world.soundFinished = ShadowRand(2);
    world.disallowed = ShadowRand(4);
    world.armed = ShadowRand(2);
    world.setStateResult = ShadowRand(2);
    world.messageResult = ShadowRand(2);
    world.selfIndex = (short)(ShadowRand(8) - 1);
    world.shooterIndex = (short)(ShadowRand(8) - 1);
    static const uint flows[] = {2, 2, 5};
    world.gameFlow = flows[ShadowRand(3)];

    world.dcv.drone = &world.drones[0];
    world.dcv.gameObj = &world.objs[0];
    world.dcv.cel = (cel_tag *)world.cel;
    world.dcv.aiStateMachine = &world.drones[0].sm;

    GameState.CurrentLevelHashcode = kLevels[ShadowRand(sizeof(kLevels) / sizeof(kLevels[0]))];
    GameState.difficultyModifier = ShadowRand(6);
    if (ShadowRand(2)) {
        FRAME_RATE_INT = 60;
        FRAME_RATE_MUL = 1.0f;
    } else {
        FRAME_RATE_INT = 50;
        FRAME_RATE_MUL = 1.2f;
    }
    RandWords[0] = RandomWord();
    RandWords[1] = RandomWord();
    MPSettings.maybeDroneAIEnabled = ShadowRand(3) == 0;
    MPSettings.FriendlyFire = ShadowRand(2);
    MPSettings.GameMode = ShadowRand(2) ? GM_ASSASSIN : (MultiplayerGameMode)0;
    for (int p = 0; p < 10; p++)
        MPSettings.Player[p].TeamId = (MPTeam)ShadowRand(3);
    static obj_tag *const assassins[] = {&world.objs[0], &world.objs[1], NULL};
    AssassinGlobals[0] = assassins[ShadowRand(3)];
    AssassinGlobals[1] = assassins[ShadowRand(3)];

    static const float tunings[] = {1.5f, 1.0f, 0.8f, 10.0f, 0.75f, 0.25f, 0.5f, 0.333f, 2.0f};
    for (int k = 0; k < 11; k++)
        DroneTuning[k] = tunings[ShadowRand(sizeof(tunings) / 4)];

    NPCGlobals.NDrone2List = &world.drones[0];
    NPCGlobals.impactAnimFrame = GameState.NumFramesUnpaused - ShadowRand(300);
    glb_players[0] = &world.player;
}

// The properties the functions test, set often
void RandomProperties() {
    uint *words = world.drones[0].currentBehaviour;
    behaviour_util_setProperty(DRONE_PROP_32, words, ShadowRand(2));
    behaviour_util_setProperty(DRONE_PROP_1C, words, ShadowRand(3) == 0);
    behaviour_util_setProperty(DRONE_PROP_34, words, ShadowRand(3) == 0);
    behaviour_util_setProperty(DRONE_PROP_36, words, ShadowRand(3) == 0);
    behaviour_util_setProperty(DRONE_PROP_10, words, ShadowRand(2));
}

// The arguments the tests hand both sides
uint nextArg;
char animArg;
float scaleArg, damageArg;
int locationArg, weaponArg;
char forceArg;
HITDATA_tag *hitArg;
obj_tag *objArg;

uint RandomNext() {
    return ShadowRand(4) == 0 ? world.drones[0].sm.curState : RandomState();
}

// ---- the suites

void TestHurt() {
    for (int trial = 0; trial < 2000; trial++) {
        RandomWorld();
        static obj_tag *const objs[] = {NULL, &world.hurtObj, &world.hurtObj, &world.otherObj, &world.objs[0]};
        objArg = objs[ShadowRand(5)];
        Compare("Hurt_GetDamage", trial,
                [] {
                    OriginalDamage o;
                    return Bits(ORIGINAL(0x0006c820, float(__cdecl *)(obj_tag *))(objArg));
                },
                [] { return Bits(Hurt_GetDamage(objArg)); });
        // The original leaves the rest of EAX as it was
        Compare("Hurt_GetType", trial,
                [] {
                    OriginalDamage o;
                    return ORIGINAL(0x0006c850, uint(__cdecl *)(obj_tag *))(objArg) & 0xffff;
                },
                [] { return (uint32_t)Hurt_GetType(objArg); });
    }
}

void TestImpactAnim() {
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        Compare("DroneAnim_LocationImpactAnim", trial,
                [] {
                    OriginalDamage o;
                    return ORIGINAL(0x00035000, uint(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)DroneAnim_LocationImpactAnim(&world.dcv); });
    }
}

void TestTalk() {
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        MPSettings.maybeDroneAIEnabled = ShadowRand(4) == 0;
        if (ShadowRand(3) == 0)
            GameState.CurrentLevelHashcode = ShadowRand(2) ? HT_Level_EvilBase : HT_Level_EvilSilo;
        Compare("NDrone2_HitTalkExtra", trial,
                [] {
                    OriginalDamage o;
                    return ORIGINAL(0x0003bc90, uint(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)NDrone2_HitTalkExtra(&world.dcv); });
        Compare("NDrone2_PainTalk", trial,
                [] {
                    OriginalDamage o;
                    return ORIGINAL(0x0003bd80, uint(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)NDrone2_PainTalk(&world.dcv); });
        MPSettings.maybeDroneAIEnabled = ShadowRand(4) != 0;
        Compare("DroneFunc_InjuredTalk", trial,
                [] {
                    OriginalDamage o;
                    ORIGINAL(0x0003c7f0, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneFunc_InjuredTalk(&world.dcv);
                    return 0u;
                });
    }
}

void TestDamage() {
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        static const float damages[] = {0.0f, 1.0f, 7.5f, 30.0f, 1e-40f, -2.0f};
        damageArg = damages[ShadowRand(6)];
        static const int locations[] = {-5, -1, 0, 1, 4, 5, 5, 5, 6, 0x14, 0x15, 0x16, 0x17, 0x20, 0x23, 0x27,
                                        0x28, 0x30, 0x31, 0x34, 0x38, 0x39, 0x100};
        locationArg = locations[ShadowRand(sizeof(locations) / 4)];
        static const int weapons[] = {-1, 1, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x20, 0x10001};
        weaponArg = weapons[ShadowRand(sizeof(weapons) / 4)];
        forceArg = (char)(ShadowRand(3) == 0 ? (ShadowRand(2) ? 1 : 0x80) : 0);
        Compare("NDrone2_HitDamage", trial,
                [] {
                    OriginalDamage o;
                    ORIGINAL(0x0003d670, void(__cdecl *)(DCVars_tag *, float, int, int, char))(
                        &world.dcv, damageArg, locationArg, weaponArg, forceArg);
                    return 0u;
                },
                [] {
                    NDrone2_HitDamage(&world.dcv, damageArg, locationArg, weaponArg, forceArg);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        // (Any other object's data would be read as a bullet: the player has none)
        world.hit.hitObj = ShadowRand(8) ? &world.shotObj : (ShadowRand(2) ? &world.player : NULL);
        if (MPSettings.maybeDroneAIEnabled && ShadowRand(2))
            world.bullet.firedByObj = &world.objs[1];
        // Without a weapon, the original reads its flags for the sound, and with bots on its id for a shot someone
        // fired: keep to shots it does neither with
        if (ShadowRand(8) == 0 && !(world.hit.dmgAmt > 0.5f) &&
            !(MPSettings.maybeDroneAIEnabled && world.bullet.firedByObj != NULL))
            world.bullet.wpnDef = NULL;
        if (ShadowRand(2))
            AssassinGlobals[1] = world.bullet.firedByObj;
        hitArg = ShadowRand(10) ? &world.hit : NULL;
        static const float scales[] = {1.0f, 0.5f, 3.0f};
        scaleArg = scales[ShadowRand(3)];
        Compare("NDrone2_DoHitEffects", trial,
                [] {
                    OriginalDamage o;
                    ORIGINAL(0x0003d900, void(__cdecl *)(DCVars_tag *, HITDATA_tag *, float))(&world.dcv, hitArg,
                                                                                            scaleArg);
                    return 0u;
                },
                [] {
                    NDrone2_DoHitEffects(&world.dcv, hitArg, scaleArg);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        RandomProperties();
        Compare("DroneFunc_SendHurtMessage", trial,
                [] {
                    OriginalDamage o;
                    ORIGINAL(0x0003a370, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneFunc_SendHurtMessage(&world.dcv);
                    return 0u;
                });
    }
}

// A shot the impacts can take: from the bullet, with a weapon
void ShotHit() {
    world.hit.hitObj = &world.shotObj;
    world.shotObj.extraObjectData = &world.bullet;
}

void TestImpacts() {
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        RandomProperties();
        ShotHit();
        nextArg = RandomNext();
        animArg = (char)ShadowRand(2);
        Compare("NDrone2_PunchImpact", trial,
                [] {
                    OriginalDamage o;
                    return ORIGINAL(0x0003e010, uint(__cdecl *)(DCVars_tag *, HITDATA_tag *, uint, char))(
                               &world.dcv, &world.hit, nextArg, animArg) & 0xff;
                },
                [] { return (uint32_t)NDrone2_PunchImpact(&world.dcv, &world.hit, nextArg, animArg); });
    }

    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        RandomProperties();
        ShotHit();
        nextArg = RandomNext();
        animArg = (char)ShadowRand(2);
        Compare("NDrone2_BulletImpact", trial,
                [] {
                    OriginalDamage o;
                    return ORIGINAL(0x0003e220, uint(__cdecl *)(DCVars_tag *, HITDATA_tag *, uint, char))(
                               &world.dcv, &world.hit, nextArg, animArg) & 0xff;
                },
                [] { return (uint32_t)NDrone2_BulletImpact(&world.dcv, &world.hit, nextArg, animArg); });
    }

    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        RandomProperties();
        nextArg = RandomNext();
        animArg = (char)ShadowRand(2);
        Compare("NDrone2_ExplosiveImpact", trial,
                [] {
                    OriginalDamage o;
                    return ORIGINAL(0x0003e3f0, uint(__cdecl *)(DCVars_tag *, DroneImpact_tag *, uint, char))(
                               &world.dcv, &world.impact, nextArg, animArg) & 0xff;
                },
                [] { return (uint32_t)NDrone2_ExplosiveImpact(&world.dcv, &world.impact, nextArg, animArg); });
    }

    for (int trial = 0; trial < 12000; trial++) {
        RandomWorld();
        RandomProperties();
        ShotHit();
        static const uint types[] = {DRONE_MSG_ImpactPunch, DRONE_MSG_ImpactTaser, DRONE_MSG_ImpactTaser,
                                     DRONE_MSG_ImpactBullet, DRONE_MSG_ImpactBullet, DRONE_MSG_ImpactExplosive,
                                     DRONE_MSG_ImpactSmoke, DRONE_MSG_ImpactStunGrenade, DRONE_MSG_ImpactStunDart, 0,
                                     5, 0xa, 0x16, 0x1a};
        memset(&world.msg, 0, sizeof(world.msg));
        world.msg.msgType = types[ShadowRand(sizeof(types) / 4)];
        world.msg.extraData = world.msg.msgType == DRONE_MSG_ImpactExplosive ? (void *)&world.impact : &world.hit;
        if (ShadowRand(3) == 0)
            GameState.CurrentLevelHashcode = ShadowRand(2) ? HT_Level_TowerA : HT_Level_TowerB;
        nextArg = RandomNext();
        animArg = (char)ShadowRand(2);
        Compare("DroneFunc_HandleImpact", trial,
                [] {
                    OriginalDamage o;
                    return ORIGINAL(0x0003e6a0, uint(__cdecl *)(DCVars_tag *, MsgObject *, uint, char))(
                               &world.dcv, &world.msg, nextArg, animArg) & 0xff;
                },
                [] { return (uint32_t)DroneFunc_HandleImpact(&world.dcv, &world.msg, nextArg, animArg); });
    }
}

void TestObjHits() {
    for (int trial = 0; trial < 12000; trial++) {
        RandomWorld();
        hitArg = ShadowRand(12) ? &world.hit : NULL;
        if (ShadowRand(4) == 0)
            world.drones[0].dtype = DTYPE_Astronaut;
        if (ShadowRand(4) == 0)
            world.drones[0].sm.curState = ShadowRand(2) ? DSTATE_BotDeathAnim : DSTATE_BotIdle;
        Compare("NDrone2_DealWithObjHit", trial,
                [] {
                    OriginalDamage o;
                    CallDealWithObjHit(0x000364f0, &world.drones[0], &world.objs[0], hitArg);
                    return 0u;
                },
                [] {
                    CallDealWithObjHit((unsigned)(uintptr_t)&NDrone2_DealWithObjHit, &world.drones[0], &world.objs[0],
                                       hitArg);
                    return 0u;
                });
    }

    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        static obj_tag *const objs[] = {NULL, &world.objs[0], &world.objs[0], &world.objs[0], &world.player};
        objArg = objs[ShadowRand(5)];
        if (objArg == &world.player)
            world.player.extraObjectData = ShadowRand(2) ? &world.drones[3] : NULL;
        Compare("Drone_BulletHit", trial,
                [] {
                    OriginalDamage o;
                    ORIGINAL(0x00031f20, void(__cdecl *)(obj_tag *, BU_tag *, HITDATA_tag *))(objArg, &world.bullet,
                                                                                            &world.hit);
                    return 0u;
                },
                [] {
                    Drone_BulletHit(objArg, &world.bullet, &world.hit);
                    return 0u;
                });
    }
}

} // namespace

void DroneDamageShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], npc[sizeof(NPCGlobals_t)], mpSettings[sizeof(MPSettings_t)];
    static char channels[256];
    static uint32_t times[256], rand[2];
    static obj_tag *players[4], *assassins[2];
    static float tuning[11];
    uint32_t frameRate = FRAME_RATE_INT;
    float frameMul = FRAME_RATE_MUL;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(mpSettings, &MPSettings, sizeof(mpSettings));
    memcpy(channels, switch_channels, sizeof(channels));
    memcpy(times, switch_channels_time, sizeof(times));
    memcpy(rand, RandWords, sizeof(rand));
    memcpy(players, glb_players, sizeof(players));
    memcpy(assassins, AssassinGlobals, sizeof(assassins));
    memcpy(tuning, DroneTuning, sizeof(tuning));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(&MPSettings, sizeof(MPSettings_t));
    regions.Add(switch_channels, 256);
    regions.Add(switch_channels_time, 1024);
    regions.Add(RandWords, 8);
    regions.Add(glb_players, sizeof(players));
    regions.Add(AssassinGlobals, sizeof(assassins));
    regions.Add(DroneTuning, sizeof(tuning));
    regions.Add(&FRAME_RATE_INT, 4);
    regions.Add(&FRAME_RATE_MUL, 4);

    InstallFakes();
    NfWarnMuted++;

    struct Suite {
        const char *what;
        void (*run)(void);
    };
    static const Suite suites[] = {
        {"Hurt_GetDamage, Hurt_GetType", TestHurt},
        {"DroneAnim_LocationImpactAnim", TestImpactAnim},
        {"NDrone2_HitTalkExtra, NDrone2_PainTalk, DroneFunc_InjuredTalk", TestTalk},
        {"NDrone2_HitDamage, NDrone2_DoHitEffects, DroneFunc_SendHurtMessage", TestDamage},
        {"NDrone2_PunchImpact, BulletImpact, ExplosiveImpact, DroneFunc_HandleImpact", TestImpacts},
        {"NDrone2_DealWithObjHit, Drone_BulletHit", TestObjHits},
    };
    const int numSuites = sizeof(suites) / sizeof(suites[0]);
    int suiteCases[numSuites], suiteDiffer[numSuites];
    for (int s = 0; s < numSuites; s++) {
        cases = differ = 0;
        suites[s].run();
        suiteCases[s] = cases;
        suiteDiffer[s] = differ;
    }

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(&MPSettings, mpSettings, sizeof(mpSettings));
    memcpy(switch_channels, channels, sizeof(channels));
    memcpy(switch_channels_time, times, sizeof(times));
    memcpy(RandWords, rand, sizeof(rand));
    memcpy(glb_players, players, sizeof(players));
    memcpy(AssassinGlobals, assassins, sizeof(assassins));
    memcpy(DroneTuning, tuning, sizeof(tuning));
    FRAME_RATE_INT = frameRate;
    FRAME_RATE_MUL = frameMul;

    for (int s = 0; s < numSuites; s++)
        printf("[dronedamage] %s: %d cases, %d differ\n", suites[s].what, suiteCases[s], suiteDiffer[s]);
}
