// Shadow tests for the drones' alerts, hearing and speech (game/drone/DroneVision_Alerts.cpp; NDrone2_PlaySFX,
// NDrone2_SpeechSFX, NDrone2_SpeechSFXd, DroneFunc_NewSightTalk and DroneFunc_LostSightTalk in NDrone2_Talk.cpp):
// each original and ours run on the same made-up drones, with what needs a level going to logging fakes. Run at start
// with MenuShadowTests=on (MenuProbe.cpp), before any level exists.
//
// Faked, so not tested here: the line of sight (NDrone2_CanSeeObject), what a drone hears (Sound_Alertness) and
// where (build_FindCel), the sounds themselves (Sound_Play3D, Sound_IsFinnished), the facial scripts
// (hashtable_getitem, NDrone2_PlayFacialAnim), which line DroneFunc_ChooseSFX picks, the target table
// (DroneFunc_TargetSeen), the message queue (Drone_SM_SendMsgSelf) and the bots' answers (BOT_reactToDroneAlertMsg,
// MP_areObjectsOnSameTeam). MPDrone_IsDyingOrDead, which NDrone2_NearArmedDrone's original has inline, is
// answered by a copy of its test. Drone_SfxInfo entries are made up and put back afterwards. Random draws are kept in
// step: both sides start from the same Rand_Random state, and it is compared afterwards.

#include "DroneAlertShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../sound/Sound.h"
#include "../game/drone/DroneVision_Alerts.h"
#include "../game/drone/NDrone2.h"
#include "../game/mp/multiplayer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords (GameSpan<uint32_t>{0x0018cdf8})
// Drone_SfxInfo (NDrone2_Talk.cpp): 0x15a entries of 0x20 bytes
#define SfxInfoTable ((uint8_t *)(size_t)XbeOurs(0x00174b28))
const int kSfxInfoBytes = 0x15a * 0x20;
#define switch_BLIND_DRONES U32_AT(XbeOurs(0x001df9a0))

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

// One Drone_SfxInfo entry, as NDrone2_Talk.cpp lays it out
struct SfxEntry {
    uint sfx;
    uint facialAnim;
    uint lastFrame;
    uint interval;
    float volume;
    float radiusOuter;
    float radiusInner;
    short pitchMin;
    short pitchMax;
};
static_assert(sizeof(SfxEntry) == 0x20, "SfxEntry is 0x20 bytes");
#define SfxEntries ((SfxEntry *)SfxInfoTable)

const int kDrones = 4;

struct World {
    obj_tag objs[kDrones];
    Drone_tag drones[kDrones];
    AnimState anims[kDrones];
    obj_tag player, dummy;
    DCVars_tag dcv;
    MsgObject msg;
    DroneAlert_tag alert;
    DYNAMICSOUNDS heard;        // what Sound_Alertness hands back
    DYNAMICSOUNDS handle;       // what Sound_Play3D hands back
    DYNAMICSOUNDS speech;       // a line being said
    uint8_t cels[2][0x40];
    uint8_t facial[0x40];       // what hashtable_getitem finds
    _VECTOR at;

    // What the fakes answer
    int canSee;
    int finished;
    int chooseZero;
    uint chooseOffset;
    float alertness;
    float level;
    int soundNull;
    int celFound;
    int botReact;
    int sameTeam;
    int scriptLoaded;
    int facialStarts;
    int playOk;
};
World world;

ShadowRegions regions;
int cases, differ;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "dronealert", what, trial, theirs, ours))
        differ++;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

float NaN() {
    uint32_t u = 0x7fc00000;
    float f;
    memcpy(&f, &u, 4);
    return f;
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
    if (obj == &world.dummy)
        return 101;
    return obj == NULL ? -1 : 102;
}

int WhoSound(const DYNAMICSOUNDS *sound) {
    if (sound == &world.heard)
        return 1;
    if (sound == &world.handle)
        return 2;
    if (sound == &world.speech)
        return 3;
    return sound == NULL ? 0 : 9;
}

void LogVector(const char *name, const _VECTOR *v) {
    ShadowLog("%s=%08x,%08x,%08x", name, Bits(v->x), Bits(v->y), Bits(v->z));
}

bool ValidVector(const _VECTOR *v) {
    const uint8_t *p = (const uint8_t *)v;
    return p >= (const uint8_t *)&world && p < (const uint8_t *)(&world + 1);
}

// ---- the fakes

bool __cdecl FakeCanSee(Drone_tag *drone, obj_tag *obj, uint bone, uint mask) {
    ShadowLog("cansee(%d,%d,%u,%u)", Who(drone), WhoObj(obj), bone, mask);
    return world.canSee != 0;
}

bool __cdecl FakeFinished(DYNAMICSOUNDS *sound) {
    ShadowLog("finished(%d)", WhoSound(sound));
    return world.finished != 0;
}

uint __cdecl FakeChooseSfx(uint first, uint last) {
    ShadowLog("choose(%x,%x)", first, last);
    return world.chooseZero ? 0 : first + world.chooseOffset % (last - first + 1);
}

void __cdecl FakeTargetSeen(Drone_tag *drone, _VECTOR *pos) {
    ShadowLog("targetseen(%d)", Who(drone));
    LogVector("at", pos);
}

float __cdecl FakeSoundAlertness(_VECTOR *pos, float *level, DYNAMICSOUNDS **sound) {
    LogVector("alertness", pos);
    *level = world.level;
    *sound = world.soundNull ? NULL : &world.heard;
    return world.alertness;
}

cel_tag *__cdecl FakeFindCel(_VECTOR *pos, world_tag *w) {
    bool valid = ValidVector(pos);
    ShadowLog("findcel(%d,%p)", valid, w);
    if (valid)
        LogVector("at", pos);
    return valid && world.celFound ? (cel_tag *)world.cels[1] : NULL;
}

void __cdecl FakeSendMsgSelf(uint msgType, void *extraData, uint delay, uint scope, DCVars_tag *dcv) {
    ShadowLog("sendself(%x,%p,%u,%u,%d)", msgType, extraData, delay, scope, Who(dcv->drone));
}

bool __cdecl FakeBotReact(uint msgType, Drone_tag *drone, void *extra) {
    ShadowLog("botreact(%x,%d,%p)", msgType, Who(drone), extra);
    return world.botReact != 0;
}

bool __cdecl FakeSameTeam(obj_tag *a, obj_tag *b) {
    ShadowLog("sameteam(%d,%d)", WhoObj(a), WhoObj(b));
    return world.sameTeam != 0;
}

void *__cdecl FakeHashtableGet(HASHCODE hashcode) {
    ShadowLog("hashtable(%x)", (uint)hashcode);
    return world.scriptLoaded ? world.facial : NULL;
}

void __cdecl FakePlayFacialAnim(DCVars_tag *dcv, HASHCODE script) {
    ShadowLog("facial(%d,%x)", Who(dcv->drone), (uint)script);
    dcv->drone->facialAnim = world.facialStarts ? world.facial : NULL;
}

void *__cdecl FakePlay3D(uint sfx, _VECTOR *pos, float volume, float outer, float inner, ushort pitch, uint id,
                         int limited) {
    ShadowLog("sound3d(%x,%08x,%08x,%08x,%x,%x,%x)", sfx, Bits(volume), Bits(outer), Bits(inner), pitch, id, limited);
    LogVector("at", pos);
    return world.playOk ? &world.handle : NULL;
}

// MPDrone_IsDyingOrDead's own test, without a log: NDrone2_NearArmedDrone's original has it inline
bool __cdecl FakeDying(obj_tag *obj) {
    if (obj == NULL)
        return true;
    Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
    if ((drone->flags & (DRONE_FLAG_DEAD | DRONE_FLAG_DYING)) || !(drone->flags & DRONE_FLAG_ENABLED))
        return true;
    if (drone->health <= 0.0f)
        return true;
    return obj->objectType == OBJECTTYPE_DEAD_DRONE || (obj->flags & 1);
}

void InstallFakes() {
    ShadowFakes_Install();
    ShadowHook_Install(0x000648b0, (const void *)&FakeCanSee);
    ShadowHook_Install(0x000cbac0, (const void *)&FakeFinished);
    ShadowHook_Install(0x00036270, (const void *)&FakeChooseSfx);
    ShadowHook_Install(0x00037820, (const void *)&FakeTargetSeen);
    ShadowHook_Install(0x000cbb10, (const void *)&FakeSoundAlertness);
    ShadowHook_Install(0x00020c30, (const void *)&FakeFindCel);
    ShadowHook_Install(0x0004e890, (const void *)&FakeSendMsgSelf);
    ShadowHook_Install(0x0001a260, (const void *)&FakeBotReact);
    ShadowHook_Install(0x0009df20, (const void *)&FakeSameTeam);
    ShadowHook_Install(0x0006b500, (const void *)&FakeHashtableGet);
    ShadowHook_Install(0x000374e0, (const void *)&FakePlayFacialAnim);
    ShadowHook_Install(0x000cb8c0, (const void *)&FakePlay3D);
    ShadowHook_Install(0x0001a190, (const void *)&FakeDying);
}

// Every original of the group at once, so they reach each other rather than ours
struct OriginalAlerts {
    XbeOriginalScope begin{0x000644c0}, end{0x000644e0}, setAssist{0x00064510}, endAssist{0x00064520},
        testAssist{0x00064550}, nearArmed{0x00064c10}, notNearArmed{0x00064dc0}, alertSound{0x00064f20},
        alarm{0x000650a0}, shout11{0x00065120}, shout12{0x00065160}, shout13{0x000651a0}, shout15{0x000651e0},
        enemyAlerts{0x00065220}, lookFor{0x00065360}, newSight{0x0003c1e0}, lostSight{0x0003c2b0},
        hearing{0x0003b410}, playSfx{0x00037340}, speech{0x00037530}, speechD{0x0003cca0};
};

// The register-argument originals
__declspec(naked) uint32_t __cdecl OriginalAlertSound(DCVars_tag *dcv) {
    _asm {
        push esi
        mov esi, [esp + 8]
        mov eax, 0x00064f20
        call eax
        pop esi
        ret
    }
}

__declspec(naked) uint32_t __cdecl OriginalGlobalAlarm(DCVars_tag *dcv) {
    _asm {
        mov edx, [esp + 4]
        mov eax, 0x000650a0
        call eax
        ret
    }
}

// A shout: the DCVars in EAX, the alert in ESI
__declspec(naked) uint32_t __cdecl OriginalShout(unsigned at, DCVars_tag *dcv, void *alert) {
    _asm {
        push esi
        mov esi, [esp + 16]
        mov eax, [esp + 12]
        mov ecx, [esp + 8]
        call ecx
        pop esi
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

// The values a compare singles out, and some others
float Pick(const float *edges, int count, float lo, float hi) {
    if (ShadowRand(3) == 0)
        return edges[ShadowRand(count)];
    return RandomFloat(lo, hi);
}

void RandomVector(_VECTOR *v, float range) {
    v->x = RandomFloat(-range, range);
    v->y = ShadowRandFloat(-range / 4, range / 4);
    v->z = RandomFloat(-range, range);
}

uint RandomFrame() {
    switch (ShadowRand(8)) {
    case 0:
        return 0x7fffff00 + ShadowRand(0x200);
    case 1:
        return 0xffffff00 + ShadowRand(0x100);
    default:
        return 1000 + ShadowRand(2000);
    }
}

const char kWeapons[] = {0, 1, 2, 0x20, 0x46, 0x47, 0x48, 0x69, 0x6a, 0x71, 0x72, -1};
const uint kFlagBits[] = {0x200, 0x400, 0x4000, 0x800, 0x400000, 0x800000, 0x4000000, 0x20000, 0x8, 0x10000000};
const uint kFlag2Bits[] = {0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x10000, 0x200000};
const float kAlertness[] = {0.0f, 0.66f, 0.7f, 1.0f, 0.6599999f, 0.7000001f, 0.9999999f};
const float kDistances[] = {0.0f, 2.0f, 49.99999f, 50.0f, 50.00001f, 80.0f};

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    AnimState *anim = &world.anims[i];
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    memset(anim, 0, sizeof(*anim));

    anim->currentWeaponId = kWeapons[ShadowRand(sizeof(kWeapons))];
    o->animState = ShadowRand(10) ? anim : NULL;
    o->extraObjectData = d;
    o->objectType = ShadowRand(10) ? OBJECTTYPE_DRONE : OBJECTTYPE_DEAD_DRONE;
    o->flags = (char)(ShadowRand(10) == 0);
    o->inCel = (cel_tag *)world.cels[ShadowRand(2)];
    RandomVector(&o->position, 20.0f);
    d->gameObj = (i > 0 && ShadowRand(12) == 0) ? NULL : o;

    d->flags = ShadowRand(6) ? 0x100 : 0;
    for (uint k = 0; k < sizeof(kFlagBits) / sizeof(kFlagBits[0]); k++)
        if (ShadowRand(6) == 0)
            d->flags |= kFlagBits[k];
    for (uint k = 0; k < sizeof(kFlag2Bits) / sizeof(kFlag2Bits[0]); k++)
        if (ShadowRand(4) == 0)
            d->knowledge |= kFlag2Bits[k];
    static const float healths[] = {-1.0f, 0.0f, -0.0f, 50.0f, 100.0f, 100.0f};
    d->health = ShadowRand(15) == 0 ? NaN() : healths[ShadowRand(6)];
    static const uchar sides[] = {1, 1, 2, 3};
    d->side = sides[ShadowRand(4)];
    static const uchar dtypes[] = {DTYPE_Normal, DTYPE_Guard, DTYPE_Hostage, DTYPE_RunToPoint, DTYPE_Civilian,
                                   DTYPE_AlarmRaiser, DTYPE_PartyGirl, DTYPE_CivilianGuard, DTYPE_CivDoorGuard,
                                   DTYPE_Bot};
    d->dtype = dtypes[ShadowRand(sizeof(dtypes))];
    static const short skins[] = {0, 3, 4, 5, 6, 7, 12, -5};
    d->skinClass = skins[ShadowRand(sizeof(skins) / sizeof(skins[0]))];
    d->distanceToTarget = Pick(kDistances, 6, 0.0f, 80.0f);
    d->alertness = Pick(kAlertness, 7, 0.0f, 1.2f);
    static const float steps[] = {0.2f, 0.2000001f, 0.1999999f};
    d->soundAlertStep = Pick(steps, 3, 0.0f, 0.5f);
    d->soundAlertness = RandomFloat(0.0f, 40.0f);
    static const uint states[] = {DSTATE_PlayScript, DSTATE_PlayScript + 0x10000, DSTATE_EnemyMission,
                                  DSTATE_EnemyRunToPoint, DSTATE_GoToGoalPosition, DSTATE_Idle, DSTATE_Patrol};
    d->sm.curState = states[ShadowRand(sizeof(states) / sizeof(states[0]))];
    d->sm.id = 1 + i;
    for (int w = 0; w < 3; w++)
        d->behaviour1[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    d->currentBehaviour = d->behaviour1;
    d->sightFlags = ShadowRand(16);
    d->returnPosKept = (uchar)(ShadowRand(4) == 0);
    d->explosiveSeen = (uchar)(ShadowRand(4) == 0);
    d->returnState = (short)ShadowRand(200);
    d->speechChannel = ShadowRand(2) ? &world.speech : NULL;
    d->facialAnim = ShadowRand(3) == 0 ? world.facial : NULL;
    d->facialAnimPlaying = (uchar)(ShadowRand(4) == 0);
    d->unknown37 = (uchar)(ShadowRand(4) == 0);
    d->heardFrame = RandomFrame();
    static const float levels[] = {0.0f, 0.5f, 1.0f};
    d->heardLevel = Pick(levels, 3, 0.0f, 2.0f);
    d->sfxFrame = RandomFrame();
}

const uint kSfxNumbers[] = {0, 1, 2, 3, 0x8b, 0xd0, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0x100, 0x159};

void RandomSfx(uint n, uint now) {
    SfxEntry *e = &SfxEntries[n];
    e->sfx = ShadowRand(6) ? 0x80 + ShadowRand(0x400) : 0;
    e->facialAnim = ShadowRand(3) == 0 ? 0x06000100 + ShadowRand(16) : 0;
    e->lastFrame = ShadowRand(4) == 0 ? RandomFrame() : now - ShadowRand(400);
    static const uint intervals[] = {0, 30, 300, 0x80000000};
    e->interval = intervals[ShadowRand(4)];
    e->volume = ShadowRandFloat(0.0f, 100.0f);
    e->radiusOuter = ShadowRandFloat(1.0f, 30.0f);
    e->radiusInner = ShadowRandFloat(1.0f, 20.0f);
    switch (ShadowRand(4)) {
    case 0:
        e->pitchMin = e->pitchMax = 0;
        break;
    case 1:
        e->pitchMin = -100;
        e->pitchMax = 100;
        break;
    case 2:
        e->pitchMin = 0;
        e->pitchMax = 50;
        break;
    default:
        e->pitchMin = 20;
        e->pitchMax = 0;
        break;
    }
}

void RandomWorld() {
    for (int i = 0; i < kDrones; i++)
        RandomDrone(i);
    // The list, from one of the first two
    int first = ShadowRand(2);
    NPCGlobals.NDrone2List = &world.drones[first];
    for (int i = first; i < kDrones - 1; i++)
        world.drones[i].next = &world.drones[i + 1];
    world.drones[kDrones - 1].next = NULL;

    memset(&world.player, 0, sizeof(world.player));
    memset(&world.dummy, 0, sizeof(world.dummy));
    world.player.objectType = OBJECTTYPE_PLAYER;
    world.player.inCel = (cel_tag *)world.cels[ShadowRand(2)];
    RandomVector(&world.player.position, 20.0f);
    world.dummy.inCel = (cel_tag *)world.cels[ShadowRand(2)];
    glb_players[0] = &world.player;
    NPCGlobals.dummyPlayer = &world.dummy;
    NPCGlobals.dummyPlayerActive = (uchar)(ShadowRand(6) == 0);

    for (int i = 0; i < kDrones; i++) {
        Drone_tag *d = &world.drones[i];
        switch (ShadowRand(4)) {
        case 0:
            d->opponent = NULL;
            break;
        case 1:
            d->opponent = &world.objs[(i + 1) % kDrones];
            break;
        default:
            d->opponent = NDrone2_Player();
            break;
        }
        d->nearArmedDrone = ShadowRand(3) == 0 ? &world.objs[ShadowRand(kDrones)] : NULL;
        d->alertedBy = ShadowRand(3) ? &world.drones[ShadowRand(kDrones)] : NULL;
    }

    world.dcv.drone = &world.drones[ShadowRand(4) == 0 ? 1 : 0];
    world.dcv.gameObj = &world.objs[world.dcv.drone == &world.drones[0] ? 0 : 1];
    world.dcv.drone->gameObj = world.dcv.gameObj;
    world.dcv.cel = world.dcv.gameObj->inCel;
    world.dcv.aiStateMachine = &world.dcv.drone->sm;

    memset(&world.alert, 0, sizeof(world.alert));
    static obj_tag *const senders[] = {NULL, &world.objs[1], &world.objs[2], &world.player};
    world.alert.sender = senders[ShadowRand(4)];
    memset(&world.msg, 0, sizeof(world.msg));
    static const uint msgs[] = {0, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1d, 0x1e, 0x1f, 0xffffffff};
    world.msg.msgType = msgs[ShadowRand(sizeof(msgs) / sizeof(msgs[0]))];
    world.msg.extraData = ShadowRand(5) ? &world.alert : NULL;

    memset(&world.heard, 0, sizeof(world.heard));
    RandomVector(&world.heard.location, 20.0f);
    world.heard.alertness = RandomFloat(0.0f, 1.0f);
    RandomVector(&world.at, 20.0f);

    world.canSee = ShadowRand(2);
    world.finished = ShadowRand(2);
    world.chooseZero = ShadowRand(8) == 0;
    world.chooseOffset = ShadowRand(5);
    world.alertness = ShadowRand(4) == 0 ? 0.0f : RandomFloat(0.0f, 50.0f);
    static const float heardLevels[] = {0.0f, 0.5f, 1.0f};
    world.level = Pick(heardLevels, 3, 0.0f, 2.0f);
    world.soundNull = ShadowRand(12) == 0;
    world.celFound = ShadowRand(5) != 0;
    world.botReact = ShadowRand(2);
    world.sameTeam = ShadowRand(2);
    world.scriptLoaded = ShadowRand(3) != 0;
    world.facialStarts = ShadowRand(2);
    world.playOk = ShadowRand(3) != 0;

    uint now = RandomFrame();
    GameState.NumFramesUnpaused = now;
    static const HASHCODE levels[] = {HT_Level_PowerStationA1, HT_Level_PowerStationA2, HT_Level_EvilBase,
                                      HT_Level_CastleIndoors1, HT_Level_TowerA, HT_Level_TowerB, HT_Level_HendersonA,
                                      HT_Level_TowerC, HT_Level_Cut_Level1};
    GameState.CurrentLevelHashcode = levels[ShadowRand(sizeof(levels) / sizeof(levels[0]))];
    NPCGlobals.postLoadFrame = ShadowRand(5) == 0 ? RandomFrame() : now - ShadowRand(60);
    NPCGlobals.sfxLogNext = ShadowRand(5) == 0 ? 15 : ShadowRand(16);
    NPCGlobals.interogating = (uchar)ShadowRand(2);
    MPSettings.dronesAreBots = ShadowRand(5) == 0;
    switch_BLIND_DRONES = ShadowRand(8) == 0;
    switch (ShadowRand(20)) {
    case 0:
        FRAME_RATE_DIV = NaN();
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        FRAME_RATE_DIV = 1.2f;
        break;
    default:
        FRAME_RATE_DIV = 1.0f;
        break;
    }
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);

    for (uint k = 0; k < sizeof(kSfxNumbers) / sizeof(kSfxNumbers[0]); k++)
        RandomSfx(kSfxNumbers[k], now);
}

// ---- the cases

char charA, charB, charC;
uint sfxArg;
float floatArg;
_VECTOR *posArg;

void TestInterogation() {
    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        Compare("DroneVision_BeginInterogate", trial,
                [] {
                    OriginalAlerts o;
                    ORIGINAL(0x000644c0, void(__cdecl *)(Drone_tag *))(world.dcv.drone);
                    return 0u;
                },
                [] {
                    DroneVision_BeginInterogate(world.dcv.drone);
                    return 0u;
                });
        Compare("DroneVision_EndInterogate", trial,
                [] {
                    OriginalAlerts o;
                    ORIGINAL(0x000644e0, void(__cdecl *)(Drone_tag *))(world.dcv.drone);
                    return 0u;
                },
                [] {
                    DroneVision_EndInterogate(world.dcv.drone);
                    return 0u;
                });
        Compare("DroneVision_SetInterogateAssist", trial,
                [] {
                    OriginalAlerts o;
                    ORIGINAL(0x00064510, void(__cdecl *)(Drone_tag *))(world.dcv.drone);
                    return 0u;
                },
                [] {
                    DroneVision_SetInterogateAssist(world.dcv.drone);
                    return 0u;
                });
        Compare("DroneVision_EndInterogateAssist", trial,
                [] {
                    OriginalAlerts o;
                    ORIGINAL(0x00064520, void(__cdecl *)(Drone_tag *))(world.dcv.drone);
                    return 0u;
                },
                [] {
                    DroneVision_EndInterogateAssist(world.dcv.drone);
                    return 0u;
                });
        Compare("DroneVision_TestEndInterogationAssist", trial,
                [] {
                    OriginalAlerts o;
                    return ORIGINAL(0x00064550, uint32_t(__cdecl *)(Drone_tag *))(world.dcv.drone) & 0xff;
                },
                [] { return (uint32_t)DroneVision_TestEndInterogationAssist(world.dcv.drone); });
    }
}

float RandomRadius() {
    static const float radii[] = {0.0f, 5.0f, 10.0f, 20.0f, 40.0f};
    return Pick(radii, 5, 0.0f, 40.0f);
}

void TestArmed() {
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        floatArg = RandomRadius();
        charA = (char)ShadowRand(2);
        Compare("NDrone2_NearArmedDrone", trial,
                [] {
                    OriginalAlerts o;
                    return ORIGINAL(0x00064c10, uint32_t(__cdecl *)(DCVars_tag *, float, char))(&world.dcv, floatArg,
                                                                                               charA) & 0xff;
                },
                [] { return (uint32_t)NDrone2_NearArmedDrone(&world.dcv, floatArg, charA); });
        Compare("NDrone2_NotNearArmedDrone", trial,
                [] {
                    OriginalAlerts o;
                    return ORIGINAL(0x00064dc0, uint32_t(__cdecl *)(DCVars_tag *, float))(&world.dcv, floatArg) & 0xff;
                },
                [] { return (uint32_t)NDrone2_NotNearArmedDrone(&world.dcv, floatArg); });
    }
}

void TestAlerts() {
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        Compare("DroneVision_AlertSound", trial,
                [] {
                    OriginalAlerts o;
                    return OriginalAlertSound(&world.dcv) & 0xffff;
                },
                [] { return (uint32_t)DroneVision_AlertSound(&world.dcv); });
        Compare("DroneVision_GlobalAlarm", trial,
                [] {
                    OriginalAlerts o;
                    return OriginalGlobalAlarm(&world.dcv) & 0xffff;
                },
                [] { return (uint32_t)DroneVision_GlobalAlarm(&world.dcv); });
    }
    for (int trial = 0; trial < 2000; trial++) {
        RandomWorld();
        Compare("DroneVision_ShoutMsg11", trial,
                [] {
                    OriginalAlerts o;
                    return OriginalShout(0x00065120, &world.dcv, world.msg.extraData) & 0xffff;
                },
                [] { return (uint32_t)DroneVision_ShoutMsg11(&world.dcv); });
        Compare("DroneVision_ShoutMsg12", trial,
                [] {
                    OriginalAlerts o;
                    return OriginalShout(0x00065160, &world.dcv, world.msg.extraData) & 0xffff;
                },
                [] { return (uint32_t)DroneVision_ShoutMsg12(&world.dcv); });
        Compare("DroneVision_ShoutMsg13", trial,
                [] {
                    OriginalAlerts o;
                    return OriginalShout(0x000651a0, &world.dcv, world.msg.extraData) & 0xffff;
                },
                [] { return (uint32_t)DroneVision_ShoutMsg13(&world.dcv); });
        Compare("DroneVision_ShoutDroneAlert", trial,
                [] {
                    OriginalAlerts o;
                    return OriginalShout(0x000651e0, &world.dcv, world.msg.extraData) & 0xffff;
                },
                [] { return (uint32_t)DroneVision_ShoutDroneAlert(&world.dcv); });
    }
    for (int trial = 0; trial < 10000; trial++) {
        RandomWorld();
        if (ShadowRand(20) == 0)
            world.dcv.drone = NULL;
        Compare("DroneVision_EnemyAlerts", trial,
                [] {
                    OriginalAlerts o;
                    return ORIGINAL(0x00065220, uint32_t(__cdecl *)(DCVars_tag *, MsgObject *))(&world.dcv,
                                                                                               &world.msg) & 0xffff;
                },
                [] { return (uint32_t)DroneVision_EnemyAlerts(&world.dcv, &world.msg); });
    }
    for (int trial = 0; trial < 10000; trial++) {
        RandomWorld();
        Compare("DroneVision_EnemyLookForOpponent", trial,
                [] {
                    OriginalAlerts o;
                    return ORIGINAL(0x00065360, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xffff;
                },
                [] { return (uint32_t)DroneVision_EnemyLookForOpponent(&world.dcv); });
    }
}

void TestHearing() {
    for (int trial = 0; trial < 10000; trial++) {
        RandomWorld();
        Compare("DroneFunc_HandleSoundAlerts", trial,
                [] {
                    OriginalAlerts o;
                    ORIGINAL(0x0003b410, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneFunc_HandleSoundAlerts(&world.dcv);
                    return 0u;
                });
    }
}

void TestSpeech() {
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        sfxArg = kSfxNumbers[ShadowRand(sizeof(kSfxNumbers) / sizeof(kSfxNumbers[0]))];
        posArg = ShadowRand(3) ? &world.at : NULL;
        charA = (char)ShadowRand(2);
        static const float scales[] = {1.0f, 2.0f, 0.5f};
        floatArg = Pick(scales, 3, 0.0f, 4.0f);
        Compare("NDrone2_PlaySFX", trial,
                [] {
                    OriginalAlerts o;
                    return ORIGINAL(0x00037340, uint32_t(__cdecl *)(DCVars_tag *, uint, _VECTOR *, char, float))(
                        &world.dcv, sfxArg, posArg, charA, floatArg);
                },
                [] { return (uint32_t)(uintptr_t)NDrone2_PlaySFX(&world.dcv, sfxArg, posArg, charA, floatArg); });
    }
    for (int trial = 0; trial < 10000; trial++) {
        RandomWorld();
        sfxArg = kSfxNumbers[ShadowRand(sizeof(kSfxNumbers) / sizeof(kSfxNumbers[0]))];
        charA = (char)ShadowRand(2);
        charB = (char)ShadowRand(2);
        charC = (char)ShadowRand(2);
        Compare("NDrone2_SpeechSFX", trial,
                [] {
                    OriginalAlerts o;
                    return ORIGINAL(0x00037530, uint32_t(__cdecl *)(DCVars_tag *, uint, char, char, char))(
                        &world.dcv, sfxArg, charA, charB, charC);
                },
                [] { return (uint32_t)(uintptr_t)NDrone2_SpeechSFX(&world.dcv, sfxArg, charA, charB, charC); });
        // The DCVars it builds must be found: the original's are stack otherwise
        world.dcv.gameObj->objectType = ShadowRand(4) ? OBJECTTYPE_DRONE : OBJECTTYPE_DEAD_DRONE;
        Compare("NDrone2_SpeechSFXd", trial,
                [] {
                    OriginalAlerts o;
                    ORIGINAL(0x0003cca0, void(__cdecl *)(Drone_tag *, uint, char, char, char))(world.dcv.drone, sfxArg,
                                                                                              charA, charB, charC);
                    return 0u;
                },
                [] {
                    NDrone2_SpeechSFXd(world.dcv.drone, sfxArg, charA, charB, charC);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 8000; trial++) {
        RandomWorld();
        if (ShadowRand(10) == 0) {
            glb_players[0] = NULL;
            NPCGlobals.dummyPlayerActive = 0;
        }
        Compare("DroneFunc_NewSightTalk", trial,
                [] {
                    OriginalAlerts o;
                    return ORIGINAL(0x0003c1e0, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)DroneFunc_NewSightTalk(&world.dcv); });
        // Not with no player: both read his cel
        glb_players[0] = &world.player;
        Compare("DroneFunc_LostSightTalk", trial,
                [] {
                    OriginalAlerts o;
                    return ORIGINAL(0x0003c2b0, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)DroneFunc_LostSightTalk(&world.dcv); });
    }
}

} // namespace

void DroneAlertShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], mpSettings[sizeof(MPSettings_t)], npc[sizeof(NPCGlobals_t)];
    static uint8_t sfxInfo[kSfxInfoBytes];
    static obj_tag *players[4];
    static uint32_t rand[2];
    uint blind = switch_BLIND_DRONES;
    float frameDiv = FRAME_RATE_DIV;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(mpSettings, &MPSettings, sizeof(mpSettings));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(sfxInfo, SfxInfoTable, sizeof(sfxInfo));
    memcpy(players, &glb_players, sizeof(players));
    XbeOursRead(rand, RandWords.at, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&MPSettings, sizeof(MPSettings_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(SfxInfoTable, kSfxInfoBytes);
    regions.Add(&glb_players, sizeof(players));
    regions.Add(RandWords.game(), 8);
    regions.Add(&switch_BLIND_DRONES, 4);
    regions.Add(&FRAME_RATE_DIV, 4);
    ShadowFakes_AddRegions(regions);

    InstallFakes();
    NfWarnMuted++;
    cases = differ = 0;

    TestInterogation();
    int interCases = cases, interDiffer = differ;
    TestArmed();
    int armedCases = cases, armedDiffer = differ;
    TestAlerts();
    int alertCases = cases, alertDiffer = differ;
    TestHearing();
    int hearCases = cases, hearDiffer = differ;
    TestSpeech();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&MPSettings, mpSettings, sizeof(mpSettings));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(SfxInfoTable, sfxInfo, sizeof(sfxInfo));
    memcpy(&glb_players, players, sizeof(players));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
    switch_BLIND_DRONES = blind;
    FRAME_RATE_DIV = frameDiv;

    printf("[dronealert] interrogation: %d cases, %d differ\n", interCases, interDiffer);
    printf("[dronealert] armed drones near: %d cases, %d differ\n", armedCases - interCases, armedDiffer - interDiffer);
    printf("[dronealert] alerts and sight: %d cases, %d differ\n", alertCases - armedCases, alertDiffer - armedDiffer);
    printf("[dronealert] hearing: %d cases, %d differ\n", hearCases - alertCases, hearDiffer - alertDiffer);
    printf("[dronealert] speech: %d cases, %d differ\n", cases - hearCases, differ - hearDiffer);
}
