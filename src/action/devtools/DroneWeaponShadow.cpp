// Shadow tests for the drones' weapons (game/drone/DroneWeapon.cpp, DroneAnim_PlayFiringAnim, NDrone2_BondIsFacingMe,
// NDrone2_SetAngleToObj, NDrone2_CanMoveToRelativePos): each original and ours run on the same made-up drones, with
// what needs a level going to logging fakes. Run at start with MenuShadowTests=on (MenuProbe.cpp), before any level
// exists.
//
// Faked, so not tested here: where the muzzle and barrel are (DroneWeap_GetWeaponInfo), the skeleton
// (AnimGetBoneWorldTrans, AnimDatumGetWeaponInfo), the bullets, casings, lights, pickups and sounds, the ray
// DroneWeap_DropPathClear casts, the walk test behind NDrone2_CanMoveToRelativePos, the bot weapon functions and
// whether the opponent is armed. Random draws are kept in step: both sides start from the same Rand_Random state, and
// it is compared afterwards.

#include "DroneWeaponShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../engine/Collide.h"
#include "../game/weapon_stats.h"
#include "../game/obj/bullet.h"
#include "../game/obj/Player.h"
#include "../game/drone/BOT.h"
#include "../game/drone/DroneAnim.h"
#include "../game/drone/DroneWeapon.h"
#include "../game/drone/NDrone2.h"
#include "../game/mp/multiplayer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords (GameSpan<uint32_t>{0x0018cdf8})
// The DroneFiring_* tuning (DroneWeapon.cpp), 0x164094 to 0x1640d8
#define DroneFiringBlock (GameSpan<float>{0x00164094})
const int kDroneFiringFloats = 0x12;

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

struct World {
    obj_tag objs[2];
    Drone_tag drones[2];
    AnimState anims[2];
    BOT_vars_t botVars;
    obj_tag player;
    BLData playerData;
    obj_tag bulletObj;
    BU_tag bullet;
    weapon_definition_tag wpn;
    obj_tag pickupObj;
    HITDATA_tag hit;
    uint8_t hitList[0x50];
    uint8_t cel[0x40];
    sAnimScript_tag script;     // what AnimScriptAdd answers
    DCVars_tag dcv;

    // What the fakes answer
    int weaponInfoOk;
    int bone;
    uint flash;
    ushort sound;
    _VECTOR muzzle, barrel;
    short ammo;
    int tooClose;
    int deployOk;
    int okToUpdate;
    int armed;
    int canMove;
    int rayHits;
    int datumBone;
    int pickupOk;
};
World world;

ShadowRegions regions;
int cases, differ;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "droneweapon", what, trial, theirs, ours))
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

int Who(const Drone_tag *drone) {
    for (int i = 0; i < 2; i++)
        if (drone == &world.drones[i])
            return i;
    return drone == NULL ? -1 : 9;
}

int WhoObj(const obj_tag *obj) {
    for (int i = 0; i < 2; i++)
        if (obj == &world.objs[i])
            return i;
    if (obj == &world.player)
        return 100;
    if (obj == NULL)
        return -1;
    if ((const uint8_t *)obj >= ShadowCreatePool && (const uint8_t *)obj < ShadowCreatePool + SHADOW_CREATE_POOL)
        return 200 + (int)((const uint8_t *)obj - ShadowCreatePool);
    return 102;
}

int WeaponIndex(const weapon_definition_tag *wpn) {
    if (wpn >= &weapon_data[0] && wpn < &weapon_data[115])
        return (int)(wpn - &weapon_data[0]);
    return wpn == &world.wpn ? 1000 : -1;
}

void LogVector(const char *name, const _VECTOR *v) {
    ShadowLog("%s=%08x,%08x,%08x", name, Bits(v->x), Bits(v->y), Bits(v->z));
}

// ---- the fakes

bool __cdecl FakeWeaponInfo(Drone_tag *drone, int *bone, uint *flash, ushort *sound, _VECTOR *muzzle, _VECTOR *barrel) {
    ShadowLog("weaponinfo(%d)", Who(drone));
    *bone = world.bone;
    *flash = world.flash;
    *sound = world.sound;
    *muzzle = world.muzzle;
    *barrel = world.barrel;
    return world.weaponInfoOk != 0;
}

void __cdecl FakeBone(obj_tag *obj, uint which, int datum, _VECTOR *pos, _MATRIX *mtx) {
    ShadowLog("bone(%d,%x,%d)", WhoObj(obj), which, datum);
    float base = (float)(which & 0xff) * 0.125f;
    pos->x = base + 0.5f;
    pos->y = 1.25f;
    pos->z = obj->position.z - 2.0f;
    for (int k = 0; k < 15; k++)
        mtx->m[k] = base + (float)k * 0.25f;
}

int __cdecl FakeDatum(obj_tag *obj, HASHCODE gfx, HASHCODE flash, ushort *sound) {
    ShadowLog("datum(%d,%x,%x,%p)", WhoObj(obj), gfx, flash, sound);
    return world.datumBone;
}

void *__cdecl FakeLight(_VECTOR *pos, uchar r, uchar g, uchar b, float brightness, ushort channel, short life,
                        uchar type, float p9, ushort p10, ushort p11, ushort p12, int sound) {
    LogVector("light", pos);
    ShadowLog("(%u,%u,%u,%08x,%u,%d,%u,%08x,%u,%u,%u,%d)", r, g, b, Bits(brightness), channel, life, type, Bits(p9),
              p10, p11, p12, sound);
    return NULL;
}

obj_tag *__cdecl FakeBullet(short player, obj_tag *a, obj_tag *b, weapon_definition_tag *wpn, _VECTOR *pos,
                            _VECTOR *dir, ushort sound) {
    ShadowLog("bullet(%d,%d,%d,%d,%x)", player, WhoObj(a), WhoObj(b), WeaponIndex(wpn), sound);
    LogVector("pos", pos);
    LogVector("dir", dir);
    return NULL;
}

void __cdecl FakeCasing(obj_tag *obj) {
    ShadowLog("casing(%d)", WhoObj(obj));
}

void __cdecl FakeGfx(obj_tag *obj, HASHCODE hashcode) {
    ShadowLog("gfx(%d,%x)", WhoObj(obj), hashcode);
}

void __cdecl FakeLinkCel(obj_tag *obj, cel_tag *cel) {
    ShadowLog("linkcel(%d,%p)", WhoObj(obj), cel);
}

obj_tag *__cdecl FakePickup(_MATRIX *mtx, short weapon, short count, int flag) {
    ShadowLog("pickup(%08x,%08x,%08x,%08x,%d,%d,%d)", Bits(mtx->m[0]), Bits(mtx->m[12]), Bits(mtx->m[13]),
              Bits(mtx->m[14]), weapon, count, flag);
    return world.pickupOk ? &world.pickupObj : NULL;
}

bool __cdecl FakeRay(_VECTOR *start, _VECTOR *end, cel_tag *cel, obj_tag *obj, obj_tag *p5, HITDATA_tag **hits, char p7,
                     uint flags, ushort p9) {
    LogVector("ray", start);
    LogVector("to", end);
    ShadowLog("(%p,%d,%p,%d,%x,%u)", cel, WhoObj(obj), p5, p7, flags, p9);
    *hits = world.rayHits ? (HITDATA_tag *)world.hitList : NULL;
    return world.rayHits != 0;
}

void __cdecl FakeFreeHits(HITDATA_tag **hits) {
    ShadowLog("freehits(%d)", *hits == (HITDATA_tag *)world.hitList);
}

bool __cdecl FakeOkToUpdate(void) {
    ShadowLog("oktoupdate");
    return world.okToUpdate != 0;
}

short __cdecl FakeAmmo(Drone_tag *drone, short weapon) {
    ShadowLog("ammo(%d,%d)", Who(drone), weapon);
    return world.ammo;
}

bool __cdecl FakeTooClose(Drone_tag *drone, short weapon, float distance) {
    ShadowLog("tooclose(%d,%d,%08x)", Who(drone), weapon, Bits(distance));
    return world.tooClose != 0;
}

void __cdecl FakeDecrRounds(Drone_tag *drone, short rounds) {
    ShadowLog("decrrounds(%d,%d)", Who(drone), rounds);
}

uint __cdecl FakeChangeBotWeapon(BOT_vars_t *bot, uint weapon, char p3, char p4) {
    ShadowLog("botchangeweapon(%d,%u,%d,%d)", bot == &world.botVars, weapon, p3, p4);
    return 0;
}

obj_tag *__cdecl FakeDeploy(_VECTOR *pos, _MATRIX *mtx, obj_tag *owner) {
    LogVector("deploy", pos);
    ShadowLog("(%d,%d)", mtx == &owner->transformMatrix, WhoObj(owner));
    return world.deployOk ? &world.pickupObj : NULL;
}

void __cdecl FakeBotTargetting(DCVars_tag *dcv) {
    ShadowLog("bottargetting(%d)", Who(dcv->drone));
}

void __cdecl FakeAimPos(Drone_tag *drone) {
    ShadowLog("aimpos(%d)", Who(drone));
}

bool __cdecl FakeArmed(obj_tag *obj) {
    ShadowLog("armed(%d)", WhoObj(obj));
    return world.armed != 0;
}

bool __cdecl FakeMoveRelative(_VECTOR *offset, Drone_tag *drone, obj_tag *obj, char unscaled, int p5) {
    LogVector("moverel", offset);
    ShadowLog("(%d,%d,%d,%d)", Who(drone), WhoObj(obj), unscaled, p5);
    return world.canMove != 0;
}

// DroneAnim_SetDAnimNow runs for real on both sides (ours is inlined into our DroneAnim_PlayFiringAnim, so a fake
// on it would only be reached by the original); its script comes from here.
bool __cdecl FakeDoAnimation(Drone_tag *drone) {
    ShadowLog("doanimation(%d)", Who(drone));
    return false;
}

sAnimScript_tag *__cdecl FakeScriptAdd(obj_tag *obj, HASHCODE hashcode) {
    ShadowLog("scriptadd(%x)", hashcode);
    memset(&world.script, 0, sizeof(world.script));
    world.script.numFrames = 20;
    return &world.script;
}

void InstallFakes() {
    ShadowFakes_Install();
    ShadowHook_Install(0x00066850, (const void *)&FakeWeaponInfo);
    ShadowHook_Install(0x00013070, (const void *)&FakeBone);
    ShadowHook_Install(0x00013630, (const void *)&FakeDatum);
    ShadowHook_Install(0x0006d530, (const void *)&FakeLight);
    ShadowHook_Install(0x000211f0, (const void *)&FakeBullet);
    ShadowHook_Install(0x00023080, (const void *)&FakeCasing);
    ShadowHook_Install(0x0006b5e0, (const void *)&FakeGfx);
    ShadowHook_Install(0x0002d200, (const void *)&FakeLinkCel);
    ShadowHook_Install(0x000a7f10, (const void *)&FakePickup);
    ShadowHook_Install(0x0002c4e0, (const void *)&FakeRay);
    ShadowHook_Install(0x000294d0, (const void *)&FakeFreeHits);
    ShadowHook_Install(0x000b04d0, (const void *)&FakeOkToUpdate);
    ShadowHook_Install(0x0001e4d0, (const void *)&FakeAmmo);
    ShadowHook_Install(0x0001f740, (const void *)&FakeTooClose);
    ShadowHook_Install(0x0001e9a0, (const void *)&FakeDecrRounds);
    ShadowHook_Install(0x0001e3f0, (const void *)&FakeChangeBotWeapon);
    ShadowHook_Install(0x0009a9b0, (const void *)&FakeDeploy);
    ShadowHook_Install(0x0001aee0, (const void *)&FakeBotTargetting);
    ShadowHook_Install(0x000378f0, (const void *)&FakeAimPos);
    ShadowHook_Install(0x00064be0, (const void *)&FakeArmed);
    ShadowHook_Install(0x000453e0, (const void *)&FakeMoveRelative);
    ShadowHook_Install(0x00037730, (const void *)&FakeDoAnimation);
    ShadowHook_Install(0x00017220, (const void *)&FakeScriptAdd);
}

// Every original of this file at once, so they reach each other rather than ours
struct OriginalWeapons {
    XbeOriginalScope handleFiring{0x00067e10}, doFiring{0x00067d80}, fire{0x000675f0}, nextBullet{0x00067400},
        nextBurst{0x00067c00}, burstDelay{0x000672d0}, ready{0x00066920}, fireWeapon{0x000677f0},
        accuracy{0x00066f90}, targetting{0x00067190}, aim{0x000666a0}, fireDelay{0x00066e90},
        burstLength{0x00066f10}, manageBurst{0x00067590}, aimTarget{0x00066700}, aimingAtMe{0x00066640},
        hasWeapon{0x00066610}, facingMe{0x00043ee0}, setAngle{0x00043c60}, canMove{0x00045490},
        canThrow{0x00066c50}, throwGrenade{0x00066cd0}, tertiary{0x00067bf0}, closeCombat{0x00066500},
        changeWeapon{0x00066b20}, dropWeapon{0x00067690}, dropPath{0x00067640}, modBullet{0x00031de0},
        modPlayerHit{0x00031eb0}, backshot{0x00031da0}, firingAnim{0x00035e60};
};

// The register-argument originals
__declspec(naked) uint32_t __cdecl OriginalDoFiring(DCVars_tag *dcv) {
    _asm {
        push edi
        mov edi, [esp + 8]
        mov eax, 0x00067d80
        call eax
        pop edi
        ret
    }
}

__declspec(naked) void __cdecl OriginalNextBulletTime(DCVars_tag *dcv, char newBurst, char start) {
    _asm {
        push edi
        mov edi, [esp + 8]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        mov eax, 0x00067400
        call eax
        add esp, 8
        pop edi
        ret
    }
}

__declspec(naked) uint32_t __cdecl OriginalManageBurstCount(DCVars_tag *dcv) {
    _asm {
        mov eax, [esp + 4]
        mov ecx, 0x00067590
        call ecx
        ret
    }
}

__declspec(naked) uint32_t __cdecl OriginalBurstDelay(Drone_tag *drone) {
    _asm {
        push esi
        mov esi, [esp + 8]
        mov eax, 0x000672d0
        call eax
        pop esi
        ret
    }
}

// ---- the made-up world

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
    v->x = RandomFloat(-range, range);
    v->y = ShadowRandFloat(-range / 4, range / 4);
    v->z = RandomFloat(-range, range);
}

// The distances the functions single out, and some others
float RandomDistance() {
    static const float edges[] = {0.5f, 1.75f, 2.0f, 2.5f, 3.0f, 4.0f, 8.0f, 10.0f, 18.0f, 20.0f, 30.0f, 40.0f};
    if (ShadowRand(3) == 0)
        return edges[ShadowRand(sizeof(edges) / sizeof(edges[0]))];
    return RandomFloat(0.0f, 45.0f);
}

const char kWeapons[] = {0, 1, 2, 6, 10, 14, 15, 16, 18, 24, 25, 26, 32, 42, 54, 70};

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    AnimState *anim = &world.anims[i];
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    memset(anim, 0, sizeof(*anim));

    anim->currentWeaponId = kWeapons[ShadowRand(sizeof(kWeapons))];
    if (ShadowRand(20) == 0)
        anim->currentWeaponId = -1;
    anim->animObj.flags = ShadowRand(2) ? 0x1000 : 0;
    o->animState = anim;
    o->extraObjectData = d;
    o->objectType = OBJECTTYPE_DRONE;
    o->inCel = (cel_tag *)world.cel;
    RandomVector(&o->position, 10.0f);
    o->rotation.y = ShadowRandFloat(-4.0f, 4.0f);
    for (int k = 0; k < 15; k++)
        o->transformMatrix.m[k] = ShadowRandFloat(-1, 1);
    o->creationTimeFrames = ShadowRand(4) == 0 ? -(int)ShadowRand(5000) : (int)ShadowRand(100000);
    o->tweakA = (uchar)ShadowRand(256);
    o->flags = (char)(ShadowRand(6) == 0);
    d->gameObj = o;

    d->firing = (uchar)ShadowRand(2);
    d->oneBurst = (uchar)ShadowRand(2);
    d->burstOver = (uchar)ShadowRand(2);
    d->firesByAnim = (uchar)(ShadowRand(3) == 0);
    d->readyToFire = (uchar)ShadowRand(2);
    d->fired = (uchar)ShadowRand(2);
    d->unknown41 = (uchar)(ShadowRand(3) == 0);
    d->unknown21 = (uchar)ShadowRand(2);
    d->inPlayersSights = (uchar)ShadowRand(2);
    static const float healths[] = {-1.0f, 0.0f, -0.0f, 50.0f, 100.0f};
    d->health = healths[ShadowRand(5)];
    d->accuracy = (uchar)ShadowRand(12);
    d->aggression = (uchar)ShadowRand(6);
    static const uchar dtypes[] = {DTYPE_Normal, DTYPE_Guard, DTYPE_Sniper, DTYPE_Ninja, DTYPE_SniperAlert, DTYPE_Bot};
    d->dtype = dtypes[ShadowRand(sizeof(dtypes))];
    static const short animSets[] = {0, 4, 5, 6, 7, 0xc, 0x12, 0x13};
    d->animSet = animSets[ShadowRand(sizeof(animSets) / sizeof(animSets[0]))];
    d->skinClass = (short)(ShadowRand(4) == 0 ? 0xc : ShadowRand(20));
    d->damageScale = ShadowRandFloat(0.25f, 3.0f);
    d->distanceToTarget = RandomDistance();
    RandomVector(&d->opponentLastPos, 10.0f);
    RandomVector(&d->opponentDelta, 3.0f);
    d->opponentAngles.x = RandomFloat(-1.0f, 1.0f);
    d->opponentAngles.y = RandomFloat(-4.0f, 4.0f);
    d->opponentAngles.z = RandomFloat(-1.0f, 1.0f);
    d->opponentFacing = RandomFloat(-3.5f, 3.5f);
    d->opponentBearing = RandomFloat(-3.5f, 3.5f);
    d->targetMoving = (uchar)ShadowRand(2);
    d->targetStartedMoving = (uchar)ShadowRand(2);
    d->targetStartedSeen = (uchar)ShadowRand(2);
    d->targetStopped = (uchar)ShadowRand(2);
    d->targetStoppedSeen = (uchar)ShadowRand(2);
    RandomVector(&d->targetPos, 10.0f);
    RandomVector(&d->aimError, 1.0f);
    RandomVector(&d->aimOffset, 1.0f);
    d->targetChangeFrame = 900 + ShadowRand(250);
    d->sightFlags = ShadowRand(16);
    d->sightFrames = ShadowRand(300);
    static const uint unseen[] = {0, 15, 16, 40};
    d->unseenFrames = unseen[ShadowRand(4)];
    for (int w = 0; w < 3; w++)
        d->behaviour1[w] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    d->currentBehaviour = d->behaviour1;
    d->flags = ShadowRand(4) == 0 ? 0x10 : 0x100;
    d->unknown3f0 = ShadowRand(5) == 0 ? 0x8000000 : 0;
    static const uchar modes[] = {0, 1, 2, 2, 4};
    d->animMode = modes[ShadowRand(5)];
    d->unknown434 = ShadowRand(5) == 0 ? (void *)world.cel : NULL;
    static const short animStates[] = {0, 10, 0x1e, 0x1f, 0x24};
    d->animState = animStates[ShadowRand(5)];
    d->animCall.state = (ushort)(ShadowRand(4) == 0 ? 0x10 : 0);
    d->unknown46c = ShadowRand(4) == 0 ? 0x100 : 0;
    d->clip = (short)ShadowRand(20);
    d->clipSize = (short)ShadowRand(40);
    static const short bursts[] = {0, 0, 1, 2, 3, 5};
    d->burstShots = bursts[ShadowRand(6)];
    d->lastShotFrame = 900 + ShadowRand(200);
    d->nextShotFrame = 950 + ShadowRand(200);
    static const ushort grenadeWeapons[] = {0, 42, 53};
    d->grenadeWeapon = grenadeWeapons[ShadowRand(3)];
    d->grenades = (ushort)ShadowRand(3);
    d->botVars = &world.botVars;
    d->sm.curState = ShadowRand(3) == 0 ? DSTATE_BotAttackReload + ShadowRand(2) : DSTATE_BotAttack + ShadowRand(18);
}

void RandomWorld() {
    for (int i = 0; i < 2; i++)
        RandomDrone(i);
    memset(&world.botVars, 0, sizeof(world.botVars));
    static const uchar botWeapons[] = {1, 6, Weap_Ronin};
    world.botVars.weapon = botWeapons[ShadowRand(3)];
    world.botVars.flags = ShadowRand(3) == 0 ? 2 : 0;

    memset(&world.player, 0, sizeof(world.player));
    memset(&world.playerData, 0, sizeof(world.playerData));
    world.player.objectType = ShadowRand(5) == 0 ? OBJECTTYPE_DEAD_PLAYER : OBJECTTYPE_PLAYER;
    world.player.extraObjectData = &world.playerData;
    RandomVector(&world.player.position, 10.0f);
    world.player.rotation.y = ShadowRandFloat(-4.0f, 4.0f);
    static const float healths[] = {-1.0f, 0.0f, 100.0f, 100.0f};
    world.playerData.health = healths[ShadowRand(4)];
    if (ShadowRand(2))
        RandomVector(&world.playerData.movement, 0.2f);

    switch (ShadowRand(5)) {
    case 0:
        world.drones[0].opponent = NULL;
        break;
    case 1:
        world.drones[0].opponent = &world.objs[1];
        break;
    default:
        world.drones[0].opponent = &world.player;
        break;
    }

    memset(&world.bulletObj, 0, sizeof(world.bulletObj));
    memset(&world.bullet, 0, sizeof(world.bullet));
    memset(&world.wpn, 0, sizeof(world.wpn));
    world.bulletObj.objectType = ShadowRand(6) ? OBJECTTYPE_BULLET : OBJECTTYPE_DRONE;
    world.bulletObj.flags = (char)(ShadowRand(6) == 0);
    world.bulletObj.extraObjectData = &world.bullet;
    world.bullet.wpnDef = &world.wpn;
    static const short variants[] = {1, 6, 98, 99, 100, 101, 102, 103};
    world.wpn.weaponVariantNum = variants[ShadowRand(sizeof(variants) / sizeof(variants[0]))];
    switch (ShadowRand(5)) {
    case 0:
        world.bullet.firedByObj = NULL;
        break;
    case 1:
        world.bullet.firedByObj = &world.player;
        break;
    default:
        world.bullet.firedByObj = &world.objs[0];
        break;
    }

    memset(&world.hit, 0, sizeof(world.hit));
    static const float damages[] = {0.0f, -0.0f, 5.0f, 12.5f, 40.0f};
    world.hit.dmgAmt = damages[ShadowRand(5)];
    RandomVector(&world.hit.maybehitDirection, 1.0f);
    world.hit.hitObj = ShadowRand(8) ? &world.bulletObj : NULL;
    memset(&world.pickupObj, 0, sizeof(world.pickupObj));
    world.pickupObj.effectFlags = ShadowRand(4);

    world.weaponInfoOk = ShadowRand(6) != 0;
    world.bone = ShadowRand(30);
    world.flash = ShadowRand(3) ? 0x02000100 + ShadowRand(16) : 0;
    static const ushort sounds[] = {0x5ef, 0x10, 0x123, 0xffff};
    world.sound = sounds[ShadowRand(4)];
    RandomVector(&world.muzzle, 10.0f);
    RandomVector(&world.barrel, 1.0f);
    if (ShadowRand(4) == 0) {
        // Along the line to the target, for the close-aim branches
        Vec_Subtract(&world.drones[0].targetPos, &world.muzzle, &world.barrel);
        world.barrel.x += ShadowRandFloat(-0.2f, 0.2f);
    }
    world.ammo = (short)(ShadowRand(4) - 1);
    world.tooClose = ShadowRand(3) == 0;
    world.deployOk = ShadowRand(2);
    world.okToUpdate = ShadowRand(5) != 0;
    world.armed = ShadowRand(3) != 0;
    world.canMove = ShadowRand(2);
    world.rayHits = ShadowRand(3) == 0;
    world.datumBone = ShadowRand(4) == 0 ? -1 : (int)ShadowRand(30);
    world.pickupOk = ShadowRand(2);
    ShadowFakes.createFails = ShadowRand(6) == 0;

    world.dcv.drone = &world.drones[0];
    world.dcv.gameObj = &world.objs[0];
    world.dcv.cel = (cel_tag *)world.cel;
    world.dcv.aiStateMachine = &world.drones[0].sm;

    GameState.NumFramesUnpaused = 1000 + ShadowRand(100);
    static const HASHCODE levels[] = {HT_Level_HendersonA, HT_Level_TowerC, HT_Level_PowerStationA1,
                                      HT_Level_PowerStationA2, HT_Level_SpaceStationD, HT_Level_CastleExterior};
    GameState.CurrentLevelHashcode = levels[ShadowRand(sizeof(levels) / sizeof(levels[0]))];
    GameState.difficultyModifier = ShadowRand(6);
    if (ShadowRand(2)) {
        _FRAME_RATE = 60.0f;
        FRAME_RATE_DIV = 1.0f;
    } else {
        _FRAME_RATE = 50.0f;
        FRAME_RATE_DIV = 1.2f;
    }
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    MPSettings.maybeDroneAIEnabled = ShadowRand(5) == 0;

    // The standard tuning (Player.cpp), varied; now and then a negative delay, for the unsigned conversions
    static const float tuning[kDroneFiringFloats] = {15.0f, 45.0f, 60.0f, 4.0f, 30.0f, 0.5f, 0.7f, 1.0f, 2.0f,
                                                     2.0f, 0.25f, 0.75f, 2.0f, 1.0f, 3.0f, 2.0f, 2.0f, 2.0f};
    for (int k = 0; k < kDroneFiringFloats; k++)
        DroneFiringBlock[k] = tuning[k] * (ShadowRand(3) ? 1.0f : ShadowRandFloat(0.5f, 1.5f));
    if (ShadowRand(10) == 0)
        DroneFiringBlock[1] = -DroneFiringBlock[1];
    if (ShadowRand(10) == 0)
        DroneFiringBlock[0] = -DroneFiringBlock[0];
}

// ---- the cases

char charA, charB;
int intArg;
float floatArg, x3, y3, z3;
obj_tag *objArg;
Drone_tag *droneArg;
HITDATA_tag *hitArg;

void TestFiring() {
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        Compare("DroneWeap_HandleFiring", trial,
                [] {
                    OriginalWeapons o;
                    ORIGINAL(0x00067e10, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneWeap_HandleFiring(&world.dcv);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 4000; trial++) {
        RandomWorld();
        Compare("DroneWeap_DoFiring", trial,
                [] {
                    OriginalWeapons o;
                    return OriginalDoFiring(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)_DroneWeap_DoFiring(&world.dcv); });
    }
    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        charA = (char)ShadowRand(2);
        Compare("DroneWeap_Fire", trial,
                [] {
                    OriginalWeapons o;
                    ORIGINAL(0x000675f0, void(__cdecl *)(char, DCVars_tag *))(charA, &world.dcv);
                    return 0u;
                },
                [] {
                    DroneWeap_Fire(charA, &world.dcv);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        charA = (char)ShadowRand(2);
        charB = (char)ShadowRand(2);
        Compare("DroneWeap_NextBulletTime", trial,
                [] {
                    OriginalWeapons o;
                    OriginalNextBulletTime(&world.dcv, charA, charB);
                    return 0u;
                },
                [] {
                    _DroneWeap_NextBulletTime(&world.dcv, charA, charB);
                    return 0u;
                });
        Compare("DroneWeap_NextBulletTimeNewBurst", trial,
                [] {
                    OriginalWeapons o;
                    ORIGINAL(0x00067c00, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneWeap_NextBulletTimeNewBurst(&world.dcv);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        Compare("DroneWeap_ManageBurstCount", trial,
                [] {
                    OriginalWeapons o;
                    return OriginalManageBurstCount(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)_DroneWeap_ManageBurstCount(&world.dcv); });
        Compare("DroneWeap_FireDelay", trial,
                [] {
                    OriginalWeapons o;
                    return ORIGINAL(0x00066e90, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xffff;
                },
                [] { return (uint32_t)(ushort)DroneWeap_FireDelay(&world.dcv); });
        Compare("DroneWeap_BurstLength", trial,
                [] {
                    OriginalWeapons o;
                    return ORIGINAL(0x00066f10, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xffff;
                },
                [] { return (uint32_t)(ushort)DroneWeap_BurstLength(&world.dcv); });
        Compare("DroneWeap_AimAtOpponent", trial,
                [] {
                    OriginalWeapons o;
                    ORIGINAL(0x000666a0, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneWeap_AimAtOpponent(&world.dcv);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        Compare("DroneWeap_BurstDelay", trial,
                [] {
                    OriginalWeapons o;
                    return OriginalBurstDelay(&world.drones[0]);
                },
                [] { return (uint32_t)_DroneWeap_BurstDelay(&world.drones[0]); });
    }
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        Compare("DroneWeap_Ready2Fire", trial,
                [] {
                    OriginalWeapons o;
                    return ORIGINAL(0x00066920, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)DroneWeap_Ready2Fire(&world.dcv); });
    }
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        if (trial % 3 == 0)
            world.drones[0].dtype = DTYPE_Bot;
        Compare("DroneWeap_FireWeapon", trial,
                [] {
                    OriginalWeapons o;
                    ORIGINAL(0x000677f0, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneWeap_FireWeapon(&world.dcv);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 6000; trial++) {
        RandomWorld();
        Compare("DroneWeap_DoBulletAccuracy", trial,
                [] {
                    OriginalWeapons o;
                    ORIGINAL(0x00066f90, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneWeap_DoBulletAccuracy(&world.dcv);
                    return 0u;
                });
        Compare("DroneWeap_DoOpponentTargetting", trial,
                [] {
                    OriginalWeapons o;
                    ORIGINAL(0x00067190, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneWeap_DoOpponentTargetting(&world.dcv);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        world.drones[0].firesByAnim = (uchar)ShadowRand(2);
        world.drones[0].readyToFire = 1;
        Compare("DroneAnim_PlayFiringAnim", trial,
                [] {
                    OriginalWeapons o;
                    return ORIGINAL(0x00035e60, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)DroneAnim_PlayFiringAnim(&world.dcv); });
    }
}

void TestOpponent() {
    for (int trial = 0; trial < 5000; trial++) {
        RandomWorld();
        charA = (char)ShadowRand(2);
        charB = (char)ShadowRand(2);
        Compare("DroneWeap_AimTarget_IsOpponent", trial,
                [] {
                    OriginalWeapons o;
                    return ORIGINAL(0x00066700, uint32_t(__cdecl *)(char, char, DCVars_tag *))(charA, charB,
                                                                                                &world.dcv) & 0xff;
                },
                [] { return (uint32_t)DroneWeap_AimTarget_IsOpponent(charA, charB, &world.dcv); });
    }
    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        floatArg = ShadowRand(4) == 0 ? 0.0f : ShadowRandFloat(1.0f, 120.0f);
        Compare("DroneWeap_OpponentIsAimingAtMe", trial,
                [] {
                    OriginalWeapons o;
                    return ORIGINAL(0x00066640, uint32_t(__cdecl *)(DCVars_tag *, float))(&world.dcv, floatArg) &
                           0xff;
                },
                [] { return (uint32_t)DroneWeap_OpponentIsAimingAtMe(&world.dcv, floatArg); });
        Compare("DroneWeap_OpponentHasWeapon", trial,
                [] {
                    OriginalWeapons o;
                    return ORIGINAL(0x00066610, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)DroneWeap_OpponentHasWeapon(&world.dcv); });
        Compare("NDrone2_BondIsFacingMe", trial,
                [] {
                    OriginalWeapons o;
                    return ORIGINAL(0x00043ee0, uint32_t(__cdecl *)(DCVars_tag *, float))(&world.dcv, floatArg) &
                           0xff;
                },
                [] { return (uint32_t)NDrone2_BondIsFacingMe(&world.dcv, floatArg); });
    }
    for (int trial = 0; trial < 4000; trial++) {
        RandomWorld();
        objArg = ShadowRand(6) == 0 ? NULL : (ShadowRand(2) ? &world.player : &world.objs[1]);
        floatArg = RandomFloat(-1.0f, 1.0f);
        charA = (char)ShadowRand(2);
        Compare("NDrone2_SetAngleToObj", trial,
                [] {
                    OriginalWeapons o;
                    ORIGINAL(0x00043c60, void(__cdecl *)(DCVars_tag *, obj_tag *, float, char))(&world.dcv, objArg,
                                                                                               floatArg, charA);
                    return 0u;
                },
                [] {
                    NDrone2_SetAngleToObj(&world.dcv, objArg, floatArg, charA);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 2000; trial++) {
        RandomWorld();
        droneArg = ShadowRand(5) == 0 ? NULL : &world.drones[0];
        x3 = RandomFloat(-5.0f, 5.0f);
        y3 = RandomFloat(-1.0f, 1.0f);
        z3 = RandomFloat(-5.0f, 5.0f);
        Compare("NDrone2_CanMoveToRelativePos", trial,
                [] {
                    OriginalWeapons o;
                    return ORIGINAL(0x00045490, uint32_t(__cdecl *)(Drone_tag *, obj_tag *, float, float, float))(
                               droneArg, &world.objs[0], x3, y3, z3) & 0xff;
                },
                [] { return (uint32_t)NDrone2_CanMoveToRelativePos(droneArg, &world.objs[0], x3, y3, z3); });
    }
}

void TestGrenadesAndHands() {
    for (int trial = 0; trial < 4000; trial++) {
        RandomWorld();
        Compare("DroneWeap_CanThrowGrenade", trial,
                [] {
                    OriginalWeapons o;
                    return ORIGINAL(0x00066c50, uint32_t(__cdecl *)(DCVars_tag *))(&world.dcv) & 0xff;
                },
                [] { return (uint32_t)DroneWeap_CanThrowGrenade(&world.dcv); });
        Compare("DroneWeap_ThrowGrenade", trial,
                [] {
                    OriginalWeapons o;
                    ORIGINAL(0x00066cd0, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneWeap_ThrowGrenade(&world.dcv);
                    return 0u;
                });
        Compare("DroneWeap_PlaceTertiary", trial,
                [] {
                    OriginalWeapons o;
                    ORIGINAL(0x00067bf0, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneWeap_PlaceTertiary(&world.dcv);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 3000; trial++) {
        RandomWorld();
        intArg = (int)ShadowRand(4);
        Compare("DroneWeap_CloseCombatImpact", trial,
                [] {
                    OriginalWeapons o;
                    ORIGINAL(0x00066500, void(__cdecl *)(DCVars_tag *, int))(&world.dcv, intArg);
                    return 0u;
                },
                [] {
                    DroneWeap_CloseCombatImpact(&world.dcv, intArg);
                    return 0u;
                });
        static const int changes[] = {0, 1, 2, 6, 18, 24, 25, 26, 54, 55};
        intArg = changes[ShadowRand(sizeof(changes) / sizeof(changes[0]))];
        Compare("DroneWeap_ChangeWeapon", trial,
                [] {
                    OriginalWeapons o;
                    ORIGINAL(0x00066b20, void(__cdecl *)(DCVars_tag *, int))(&world.dcv, intArg);
                    return 0u;
                },
                [] {
                    DroneWeap_ChangeWeapon(&world.dcv, intArg);
                    return 0u;
                });
    }
    for (int trial = 0; trial < 4000; trial++) {
        RandomWorld();
        if (ShadowRand(10) == 0)
            world.objs[0].animState = NULL;
        Compare("DroneWeap_DropWeapon", trial,
                [] {
                    OriginalWeapons o;
                    ORIGINAL(0x00067690, void(__cdecl *)(DCVars_tag *))(&world.dcv);
                    return 0u;
                },
                [] {
                    DroneWeap_DropWeapon(&world.dcv);
                    return 0u;
                });
        Compare("DroneWeap_DropPathClear", trial,
                [] {
                    OriginalWeapons o;
                    return ORIGINAL(0x00067640, uint32_t(__cdecl *)(Drone_tag *, _VECTOR *))(&world.drones[0],
                                                                                            &world.muzzle) & 0xff;
                },
                [] { return (uint32_t)DroneWeap_DropPathClear(&world.drones[0], &world.muzzle); });
    }
}

void TestPlayerDamage() {
    for (int trial = 0; trial < 5000; trial++) {
        RandomWorld();
        world.hit.hitObj = &world.bulletObj;
        Compare("Drone_MayDealBackshotDamage", trial,
                [] {
                    OriginalWeapons o;
                    return (uint32_t)ORIGINAL(0x00031da0, bool(__fastcall *)(Drone_tag *))(&world.drones[0]);
                },
                [] { return (uint32_t)Drone_MayDealBackshotDamage(&world.drones[0]); });
        Compare("Drone_ModBulletDamage", trial,
                [] {
                    OriginalWeapons o;
                    return DoubleBits(ORIGINAL(0x00031de0, double(__cdecl *)(Drone_tag *, obj_tag *, HITDATA_tag *))(
                        &world.drones[0], &world.player, &world.hit));
                },
                [] { return DoubleBits(Drone_ModBulletDamage(&world.drones[0], &world.player, &world.hit)); });
    }
    for (int trial = 0; trial < 4000; trial++) {
        RandomWorld();
        objArg = ShadowRand(8) == 0 ? NULL : &world.player;
        hitArg = ShadowRand(10) ? &world.hit : NULL;
        Compare("Drone_ModPlayerHitDamage", trial,
                [] {
                    OriginalWeapons o;
                    return ORIGINAL(0x00031eb0, uint32_t(__cdecl *)(obj_tag *, HITDATA_tag *))(objArg, hitArg) &
                           0xff;
                },
                [] { return (uint32_t)Drone_ModPlayerHitDamage(objArg, hitArg); });
    }
}

} // namespace

void DroneWeaponShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], mpSettings[sizeof(MPSettings_t)];
    static float firing[kDroneFiringFloats];
    static uint32_t rand[2];
    float frameRate = _FRAME_RATE;
    float frameDiv = FRAME_RATE_DIV;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(mpSettings, &MPSettings, sizeof(mpSettings));
    XbeOursRead(firing, DroneFiringBlock.at, sizeof(firing));
    XbeOursRead(rand, RandWords.at, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&MPSettings, sizeof(MPSettings_t));
    regions.Add(DroneFiringBlock.game(), sizeof(firing));
    regions.Add(RandWords.game(), 8);
    regions.Add(&_FRAME_RATE, 4);
    regions.Add(&FRAME_RATE_DIV, 4);
    ShadowFakes_AddRegions(regions);

    InstallFakes();
    NfWarnMuted++;
    cases = differ = 0;

    TestFiring();
    int firingCases = cases, firingDiffer = differ;
    TestOpponent();
    int opponentCases = cases, opponentDiffer = differ;
    TestGrenadesAndHands();
    int handCases = cases, handDiffer = differ;
    TestPlayerDamage();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&MPSettings, mpSettings, sizeof(mpSettings));
    XbeOursWrite(DroneFiringBlock.at, firing, sizeof(firing));
    XbeOursWrite(RandWords.at, rand, sizeof(rand));
    _FRAME_RATE = frameRate;
    FRAME_RATE_DIV = frameDiv;

    printf("[droneweapon] firing: %d cases, %d differ\n", firingCases, firingDiffer);
    printf("[droneweapon] aiming at the opponent: %d cases, %d differ\n", opponentCases - firingCases,
           opponentDiffer - firingDiffer);
    printf("[droneweapon] grenades, close combat, the weapon in hand: %d cases, %d differ\n", handCases - opponentCases,
           handDiffer - opponentDiffer);
    printf("[droneweapon] damage to the player: %d cases, %d differ\n", cases - handCases, differ - handDiffer);
}
