// Shadow tests for the player (Player.cpp, PlayerTraverse.cpp) and the placements and cameras that go with it: each
// function, the original and ours, runs on the same made-up player, cels, wires, walls, hits and globals, with every
// callee that needs a level (anims, sounds, the HUD, the camera, collision queries, the weapon's functions, the other
// objects' activation) going to a logging fake. Our other player ports run for real underneath both. Run at start
// with MenuShadowTests=on (MenuProbe.cpp), before any level exists.
//
// What cannot be tested here: what the faked callees would do in a level. The register-argument originals are called
// with their registers set (CallRegs), and so are our adaptors, so the adaptors are tested too. Returns are compared
// in full for the int ones (Player_InWater, Player_ChangeSubState), in AL for the byte ones, and as the value in ST0
// for the float ones.

#include "PlayerShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../input.h"
#include "../engine/Anim.h"
#include "../engine/Camera.h"
#include "../engine/viewer.h"
#include "../game/drone/BOT.h"
#include "../game/drone/Drone.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/bullet.h"
#include "../game/obj/CamSubject.h"
#include "../game/obj/CreepWall.h"
#include "../game/obj/Grapple.h"
#include "../game/obj/Light.h"
#include "../game/obj/Player.h"
#include "../game/obj/ThirdCam.h"
#include "../game/obj/ThirdIcon.h"
#include "../game/obj/Wire.h"
#include "../game/sp/SwitchChannels.h"
#include "../ui/HUD.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// The game's RNG state (Rand_Random)
#define RandWords ((uint32_t *)0x0018cdf8)
#define GameWireAnim U32_AT(0x00278e54)
#define GameCheatInfo ((uint8_t *)0x001f65dc)
#define GameSurfaced U8_AT(0x00276760)
#define GameScanMode U8_AT(0x001fec49)
#define GameBenchmark U8_AT(0x002adf32)
#define GameAssassin ((uint8_t *)0x00261788)

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))
#ifndef COUNT
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#endif

const int HITS = 4;
const int LIGHTS = 3;
const int STREAM = 128;

struct World {
    obj_tag player;
    uint8_t bl[0x900];
    uint8_t anim[0x100];
    uint8_t newAnim[0x100];     // what AnimObjectNew gives a new player
    uint8_t viewer[sizeof(viewer_tag)];
    uint8_t cel[sizeof(cel_tag)];
    HUDINFO_tag hud;
    uint8_t celgl[sizeof(celglist_tag)];
    obj_tag weapon;
    obj_tag muzzleFlash;
    obj_tag rope;
    obj_tag line;               // a wire, creep wall or ladder
    uint8_t lineData[8];
    obj_tag grapple;
    obj_tag thirdCam;
    THIRDCAM thirdCamData;
    obj_tag device;             // a remote-controlled device
    obj_tag bulletObj;
    BU_tag bullet;
    weapon_definition_tag bulletWeapon; // a variant whose base is one the code looks for
    obj_tag shooter;
    Drone_tag drone;
    BOT_vars_t bot;
    obj_tag hurt;
    obj_tag door;
    obj_tag pickup;
    obj_tag glow;
    obj_tag objs[2];            // the object list for control_first_object
    uint8_t objData[2][8];
    HITDATA_tag hits[HITS];
    sAnimScript_tag script;
    light_tag lights[LIGHTS];
    light_tag *lightList[LIGHTS];
    char text[160];
    char cels[4];
    _VECTOR vectorArg;
    _VECTOR gravityArg;
    _MATRIX matrixArg;
    char placement[0xb0];
    uint32_t wireAnim;          // WireAnim, ours and the game's, moved in and out around each run
    uint32_t args[16];          // the cases' own arguments, drawn with the world
    uint32_t stream[STREAM];    // what the fakes answer, in turn
    uint32_t streamPos;
};
World world;

ShadowRegions regions;
int cases, differ;

BLData *Bl() { return (BLData *)world.bl; }
AnimState *Anim() { return (AnimState *)world.anim; }
viewer_tag *Viewer() { return (viewer_tag *)world.viewer; }
cel_tag *Cel() { return (cel_tag *)world.cel; }

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

uint64_t Bits64(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

// A pointer, as an offset into the world when it is in it, or "stack"
uint32_t W(const void *p) {
    if (p == NULL)
        return 0xffffffffu;
    uintptr_t at = (uintptr_t)p, base = (uintptr_t)&world;
    if (at >= base && at < base + sizeof(world))
        return (uint32_t)(at - base);
    char here;
    ptrdiff_t distance = (const char *)p - &here;
    if (distance > -0x10000 && distance < 0x10000)
        return 0x7fffffffu;
    return 0x80000000u | (uint32_t)at;
}

uint32_t Next(uint32_t n) {
    uint32_t v = world.stream[world.streamPos++ % STREAM];
    return n ? v % n : v;
}

float NextFloat(const float *values, int count) {
    return values[Next(count)];
}

void LogVector(const char *name, const _VECTOR *v) {
    if (v == NULL)
        ShadowLog("%s=null", name);
    else
        ShadowLog("%s=%08x,%08x,%08x", name, Bits(v->x), Bits(v->y), Bits(v->z));
}

// ---- the fakes

void __cdecl FakeAnimSetDeleteAll(obj_tag *obj) { ShadowLog("animsetdeleteall(%x)", W(obj)); }
sAnimScript_tag *__cdecl FakeAnimScriptAdd(obj_tag *obj, HASHCODE hc) {
    ShadowLog("animscriptadd(%x,%x)", W(obj), (uint)hc);
    return Next(4) ? &world.script : NULL;
}
sAnimScript_tag *__cdecl FakeAppendBlend(obj_tag *obj, HASHCODE hc) {
    ShadowLog("appendblend(%x,%x)", W(obj), (uint)hc);
    return &world.script;
}
int __cdecl FakeScriptIsStopped(obj_tag *obj) {
    ShadowLog("isstopped(%x)", W(obj));
    return Next(2);
}
void __cdecl FakeAnimSetUpdate(obj_tag *obj, void *set, float a, float b, float c, float d, float e) {
    ShadowLog("animsetupdate(%x,%x,%08x,%08x,%08x,%08x,%08x)", W(obj), W(set), Bits(a), Bits(b), Bits(c), Bits(d),
              Bits(e));
}
void __cdecl FakeAnimInitNormal(obj_tag *obj) { ShadowLog("initnormal(%x)", W(obj)); }
void __cdecl FakeAnimInitCrouch(obj_tag *obj) { ShadowLog("initcrouch(%x)", W(obj)); }
void __cdecl FakeAnimCrouch2Stand(obj_tag *obj) { ShadowLog("crouch2stand(%x)", W(obj)); }
void __cdecl FakeAnimStand2Crouch(obj_tag *obj) { ShadowLog("stand2crouch(%x)", W(obj)); }
bool __cdecl FakeAnimObjectUpdate(obj_tag *obj) {
    ShadowLog("animobjectupdate(%x)", W(obj));
    return true;
}
void __cdecl FakeBoneWorldTrans(obj_tag *obj, uint which, int datum, _VECTOR *pos, _MATRIX *mtx) {
    ShadowLog("bone(%x,%x,%d,%x,%x)", W(obj), which, datum, W(pos), W(mtx));
    float k = (float)(which & 0xff) * 0.0625f;
    if (pos != NULL) {
        pos->x = obj->position.x + k;
        pos->y = obj->position.y + 1.25f + k;
        pos->z = obj->position.z - k;
    }
    if (mtx != NULL)
        memset(mtx, 0, sizeof(*mtx));
}
void __cdecl FakeFrameTransRaw(HASHCODE hc, int frame, _VECTOR *out) {
    ShadowLog("frametrans(%x,%d,%x)", (uint)hc, frame, W(out));
    out->x = frame * 0.001f + 0.08f;
    out->y = frame * -0.002f;
    out->z = frame * 0.003f - 0.1f;
}
int __cdecl FakeAnimObjectNew(obj_tag *obj, HASHCODE skin) {
    ShadowLog("animobjectnew(%x,%x)", W(obj), (uint)skin);
    obj->animState = (AnimState *)world.newAnim;
    return 1;
}
void __cdecl FakeAnimDebugCreate(void) { ShadowLog("animdebugcreate"); }
void __cdecl FakeSetAiming(obj_tag *obj, const void *target, float range, float rate, char atBone, uchar bone) {
    ShadowLog("setaiming(%x,%x,%08x,%08x,%d,%u)", W(obj), W(target), Bits(range), Bits(rate), atBone, bone);
}
void __cdecl FakeStopAllWithId(uint sfx) { ShadowLog("stopall(%x)", sfx); }
void __cdecl FakeSoundStop(void *handle) { ShadowLog("soundstop(%x)", W(handle)); }
void *__cdecl FakeSoundPlay3D(uint sfx, _VECTOR *pos, float volume, float outer, float inner, ushort pitch, uint p7,
                              int p8) {
    ShadowLog("sound3d(%x,%x,%08x,%08x,%08x,%x,%x,%x)", sfx, W(pos), Bits(volume), Bits(outer), Bits(inner), pitch, p7,
              p8);
    return Next(2) ? (void *)&world.cels[2] : NULL;
}
bool __cdecl FakeOkToUpdate(void) {
    ShadowLog("oktoupdate");
    return Next(4) != 0;
}
void __cdecl FakeLogHealth(float health, uint player) { ShadowLog("loghealth(%08x,%u)", Bits(health), player); }
void __cdecl FakeLogPlayer(uint player) { ShadowLog("logplayer(%u)", player); }
void __cdecl FakeRumble(ushort player, int time, int intensity) {
    ShadowLog("rumble(%u,%d,%d)", player, time, intensity);
}
ushort __cdecl FakeAction(short player, int action, uchar flags) {
    ShadowLog("action(%d,%d,%u)", player, action, flags);
    static const ushort answers[] = {0, 0, 0, 1, 1, 0x100};
    return answers[Next(6)];
}
float __cdecl FakeActionf(short player, int action, uchar flags) {
    ShadowLog("actionf(%d,%d,%u)", player, action, flags);
    static const float answers[] = {0.0f, 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 0.49999997f, 0.95f, -0.94999999f,
                                    0.04f, 0.05f, -0.3f, 0.7f, 0.0001f, 0.25f};
    return NextFloat(answers, 16);
}
uint __cdecl FakeGameFlowState(void) {
    ShadowLog("gameflow");
    return Next(4);
}
void __cdecl FakeObjCall(obj_tag *obj) { ShadowLog("objcall(%x)", W(obj)); }
void __cdecl FakeObjCall2(obj_tag *obj, obj_tag *other) { ShadowLog("objcall2(%x,%x)", W(obj), W(other)); }
bool __cdecl FakeDoorActivate(obj_tag *obj, obj_tag *user, char touched) {
    ShadowLog("door(%x,%x,%d)", W(obj), W(user), touched);
    return Next(2) != 0;
}
obj_tag *__cdecl FakePickup(_MATRIX *mtx, int weapon, short count, int p4) {
    ShadowLog("pickup(%x,%x,%d,%d)", W(mtx), weapon & 0xffff, count, p4);
    return Next(2) ? &world.pickup : NULL;
}
void __cdecl FakeHudCall(BLData *bl, obj_tag *obj) { ShadowLog("hudcall(%x,%x)", W(bl), W(obj)); }
void __cdecl FakeHudDisableAll(BLData *bl) { ShadowLog("huddisableall(%x)", W(bl)); }
ushort __cdecl FakeHudState(HUDINFO_tag *hud, int pane) {
    ShadowLog("hudstate(%x,%d)", W(hud), pane);
    return (ushort)Next(2);
}
void __cdecl FakeHudEnable(HUDINFO_tag *hud, int pane, char enable, ushort state) {
    ShadowLog("hudenable(%x,%d,%d,%u)", W(hud), pane, enable, state);
}
void __cdecl FakeSetCamMode(BLData *bl, ushort mode) { ShadowLog("setcammode(%x,%u)", W(bl), mode); }
char *__cdecl FakeStringFromHeap(uchar lock) {
    ShadowLog("stringfromheap(%u)", lock);
    return world.text;
}
void __cdecl FakeFreeHitList(HITDATA_tag **list) {
    ShadowLog("freehitlist(%x,%x)", W(list), W(*list));
    *list = NULL;
}
bool __cdecl FakeQuitOrRestarted(void) {
    ShadowLog("quitorrestarted");
    return Next(4) == 0;
}
short __cdecl FakePlr2Ind(obj_tag *obj) {
    ShadowLog("plr2ind(%x)", W(obj));
    static const short answers[] = {0, 1, 4, 5, -1, 0};
    return answers[Next(6)];
}
bool __cdecl FakeReSpawn(obj_tag *obj, ushort idx) {
    ShadowLog("respawn(%x,%u)", W(obj), idx);
    return true;
}
light_tag **__cdecl FakeClosestLights(_VECTOR *pos, uint mask, ushort *count) {
    ShadowLog("closestlights(%x,%x)", W(pos), mask);
    *count = (ushort)Next(LIGHTS + 1);
    return world.lightList;
}
bool __cdecl FakeLineOfSight(_VECTOR *a, _VECTOR *b, cel_tag *cel, obj_tag *obj, obj_tag *ignore, uint mask) {
    ShadowLog("los(%x,%x,%x,%x,%x)", W(cel), W(obj), W(ignore), mask, 0);
    LogVector("a", a);
    LogVector("b", b);
    return Next(2) != 0;
}
bool __cdecl FakeRayIntersect(_VECTOR *a, _VECTOR *b, cel_tag *cel, obj_tag *obj, obj_tag *p5, HITDATA_tag **list,
                              char p7, uint mask, ushort p9) {
    ShadowLog("ray(%x,%x,%x,%x,%d,%x,%u)", W(cel), W(obj), W(p5), W(list), p7, mask, p9);
    LogVector("a", a);
    LogVector("b", b);
    if (Next(2)) {
        *list = &world.hits[0];
        return true;
    }
    *list = NULL;
    return false;
}
bool __cdecl FakeSphereIntersect(_VECTOR *centre, float radius, cel_tag *cel, obj_tag *obj, obj_tag *ignore,
                                 HITDATA_tag **list, char p7, char p8, ushort flags, int p10) {
    ShadowLog("sphere(%08x,%x,%x,%x,%d,%d,%x,%d)", Bits(radius), W(cel), W(obj), W(ignore), p7, p8, flags, p10);
    LogVector("at", centre);
    if (Next(3) == 0)
        return false;
    *list = Next(4) ? &world.hits[0] : NULL;
    return true;
}
cel_tag *__cdecl FakeFindCel(_VECTOR *pos, void *w) {
    ShadowLog("findcel(%x)", W(w));
    LogVector("at", pos);
    return (cel_tag *)world.cel;
}
bool __cdecl FakePointOnFloor(cel_tag *cel, obj_tag *obj, _VECTOR *pos, float distance, _VECTOR *dir) {
    ShadowLog("pointonfloor(%x,%x,%x,%08x,%x)", W(cel), W(obj), W(pos), Bits(distance), W(dir));
    pos->y = pos->y - 1.5f;
    return Next(2) != 0;
}
void __cdecl FakeUnlink(obj_tag *obj) { ShadowLog("unlink(%x)", W(obj)); }
void __cdecl FakeCameraSet(viewer_tag *viewer, _VECTOR *pos, _VECTOR *rot) {
    ShadowLog("cameraset(%x,%x,%x)", W(viewer), W(pos), W(rot));
    LogVector("pos", pos);
    LogVector("rot", rot);
}
void __cdecl FakeCameraEnable(ushort num, char a, uchar b, _VECTOR *c) {
    ShadowLog("cameraenable(%u,%d,%u,%x)", num, a, b, W(c));
}
bool __cdecl FakeSetFade(ushort num, float frames, uint colour, uchar mode) {
    ShadowLog("setfade(%u,%08x,%x,%u)", num, Bits(frames), colour, mode);
    return true;
}
void __cdecl FakeCalcViewAngles(ushort num, float fov) { ShadowLog("viewangles(%u,%08x)", num, Bits(fov)); }
void __cdecl FakeApplySwing(viewer_tag *viewer, float sway) { ShadowLog("swing(%x,%08x)", W(viewer), Bits(sway)); }
void __cdecl FakePlace(_VECTOR *pos, viewer_tag *viewer, float scale) {
    ShadowLog("place(%x,%x,%08x)", W(pos), W(viewer), Bits(scale));
    LogVector("at", pos);
}
uint __cdecl FakeMenuCreate(uint a, uint b, short c, uchar d, uint e, char f, uint g) {
    ShadowLog("menucreate(%x,%x,%d,%u,%x,%d,%u)", a, b, c, d, e, f, g);
    return Next(8);
}
bool __cdecl FakeIsPaused(short player) {
    ShadowLog("ispaused(%d)", player);
    return Next(5) == 0;
}
void __cdecl FakeBreath(_VECTOR *pos, _VECTOR *dir, cel_tag *cel, int seed) {
    ShadowLog("breath(%x,%d)", W(cel), seed);
    LogVector("at", pos);
    LogVector("dir", dir);
}
void __cdecl FakeRipple(HITDATA_tag *hit) { ShadowLog("ripple(%x)", W(hit)); }
ushort __cdecl FakeHurtType(obj_tag *obj) {
    ShadowLog("hurttype(%x)", W(obj));
    return (ushort)Next(9);
}
float __cdecl FakeHurtDamage(obj_tag *obj) {
    ShadowLog("hurtdamage(%x)", W(obj));
    static const float answers[] = {0.0f, 5.0f, 12.5f, 150.0f};
    return NextFloat(answers, 4);
}
bool __cdecl FakeModPlayerHitDamage(obj_tag *player, HITDATA_tag *hit) {
    ShadowLog("modplayerhit(%x,%x)", W(player), W(hit));
    return Next(4) != 0;
}
void __cdecl FakeWeaponFiring(obj_tag *player, char p2) { ShadowLog("weaponfiring(%x,%d)", W(player), p2); }
void __cdecl FakeMuzzleFlash(obj_tag *player, _VECTOR *muzzle) { ShadowLog("muzzleflash(%x,%x)", W(player), W(muzzle)); }
obj_tag *__cdecl FakeBodyGlow(obj_tag *obj) {
    ShadowLog("bodyglow(%x)", W(obj));
    return &world.glow;
}
obj_tag *__cdecl FakeCreateObjEx(ushort a, _VECTOR *b, _VECTOR *c, _MATRIX *d, celglist_tag *e, obj_tag *f, char g,
                                 ushort h, float i, ushort j, uchar k, uchar l, uchar m) {
    ShadowLog("createobjex(%u,%x,%x,%x,%x,%x,%d,%u,%08x,%x,%u,%u,%u)", a, W(b), W(c), W(d), W(e), W(f), g, h, Bits(i), j,
              k, l, m);
    return &world.rope;
}
celglist_tag *__cdecl FakeCelglist(HASHCODE hc) {
    ShadowLog("celglist(%x)", (uint)hc);
    return (celglist_tag *)world.celgl;
}
obj_tag *__cdecl FakeFeetOnPoint(obj_tag *obj, HITTEST_tag *test, float *normal, char dying) {
    ShadowLog("feet(%x,%x,%x,%d)", W(obj), W(test), W(normal), dying);
    static const float normals[] = {1.0f, 0.5f, 0.49999997f, 0.2f};
    *normal = NextFloat(normals, 4);
    if (Next(2))
        obj->animState->animFlags |= ANIM_CONTACT_FLOOR;
    return NULL;
}
void __cdecl FakeOtherViews(obj_tag *obj, char player) { ShadowLog("otherviewsonly(%x,%d)", W(obj), player); }
void __cdecl FakeInitWeapon(BLData *bl, obj_tag *obj, void *p3) { ShadowLog("initweapon(%x,%x)", W(bl), W(obj)); }

// The register-argument originals Player_Update calls: Player_Swim (EAX data, ESI player), Player_ClampSomeAngles
// (ECX data, the player on the stack) and Player_UpdateBlurAndFade (ESI data)
void __cdecl LogRegs(const char *what, uint32_t a, uint32_t b) { ShadowLog("%s(%x,%x)", what, W((void *)a), W((void *)b)); }
void __cdecl LogSwim(uint32_t data, uint32_t player) { LogRegs("swim", data, player); }
void __cdecl LogClamp(uint32_t data, uint32_t player) { LogRegs("clampangles", data, player); }
void __cdecl LogBlur(uint32_t data, uint32_t unused) { LogRegs("blurandfade", data, unused); }
__declspec(naked) void FakeSwim(void) {
    _asm {
        push esi
        push eax
        call LogSwim
        add esp, 8
        ret
    }
}
__declspec(naked) void FakeClampSomeAngles(void) {
    _asm {
        push dword ptr [esp + 4]
        push ecx
        call LogClamp
        add esp, 8
        ret
    }
}
__declspec(naked) void FakeBlurAndFade(void) {
    _asm {
        push 0
        push esi
        call LogBlur
        add esp, 8
        ret
    }
}

void InstallFakes(void) {
    ShadowFakes_Install();
    ShadowHook_Install(0x00017820, (const void *)&FakeAnimSetDeleteAll);
    ShadowHook_Install(0x00017220, (const void *)&FakeAnimScriptAdd);
    ShadowHook_Install(0x00015ca0, (const void *)&FakeAppendBlend);
    ShadowHook_Install(0x00012780, (const void *)&FakeScriptIsStopped);
    ShadowHook_Install(0x00018bb0, (const void *)&FakeAnimSetUpdate);
    ShadowHook_Install(0x000a91e0, (const void *)&FakeAnimInitNormal);
    ShadowHook_Install(0x000a9350, (const void *)&FakeAnimInitCrouch);
    ShadowHook_Install(0x000a94c0, (const void *)&FakeAnimCrouch2Stand);
    ShadowHook_Install(0x000a9590, (const void *)&FakeAnimStand2Crouch);
    ShadowHook_Install(0x00018690, (const void *)&FakeAnimObjectUpdate);
    ShadowHook_Install(0x00013070, (const void *)&FakeBoneWorldTrans);
    ShadowHook_Install(0x00015e40, (const void *)&FakeFrameTransRaw);
    ShadowHook_Install(0x00011440, (const void *)&FakeAnimObjectNew);
    ShadowHook_Install(0x00019180, (const void *)&FakeAnimDebugCreate);
    ShadowHook_Install(0x00012220, (const void *)&FakeSetAiming);
    ShadowHook_Install(0x000cb9b0, (const void *)&FakeStopAllWithId);
    ShadowHook_Install(0x000cb9a0, (const void *)&FakeSoundStop);
    ShadowHook_Install(0x000cb8c0, (const void *)&FakeSoundPlay3D);
    ShadowHook_Install(0x000b04d0, (const void *)&FakeOkToUpdate);
    ShadowHook_Install(0x000b0600, (const void *)&FakeLogHealth);
    ShadowHook_Install(0x000b05c0, (const void *)&FakeLogPlayer);
    ShadowHook_Install(0x000b0590, (const void *)&FakeLogPlayer);
    ShadowHook_Install(0x0006cc00, (const void *)&FakeRumble);
    ShadowHook_Install(0x0006c9a0, (const void *)&FakeAction);
    ShadowHook_Install(0x0006ca20, (const void *)&FakeActionf);
    ShadowHook_Install(0x0006aa70, (const void *)&FakeGameFlowState);
    ShadowHook_Install(0x00026b60, (const void *)&FakeObjCall);     // Car_PlayerHasDied
    ShadowHook_Install(0x00026f10, (const void *)&FakeObjCall);     // GunImp_PlayerHasDied
    ShadowHook_Install(0x0009a960, (const void *)&FakeObjCall);     // GT_PlayerHasDied
    ShadowHook_Install(0x000a1fc0, (const void *)&FakeObjCall);     // MP_PlayerKilled
    ShadowHook_Install(0x000a7f10, (const void *)&FakePickup);
    ShadowHook_Install(0x000b6190, (const void *)&FakeHudCall);     // HUD_Update
    ShadowHook_Install(0x000b6aa0, (const void *)&FakeHudCall);     // HUD_Init
    ShadowHook_Install(0x000b1a30, (const void *)&FakeHudDisableAll);
    ShadowHook_Install(0x000b1a90, (const void *)&FakeHudState);
    ShadowHook_Install(0x000b1a60, (const void *)&FakeHudEnable);
    ShadowHook_Install(0x000abef0, (const void *)&FakeSetCamMode);
    ShadowHook_Install(0x0006d140, (const void *)&FakeStringFromHeap);
    ShadowHook_Install(0x000294d0, (const void *)&FakeFreeHitList);
    ShadowHook_Install(0x00092c30, (const void *)&FakeQuitOrRestarted);
    ShadowHook_Install(0x0002d8a0, (const void *)&FakePlr2Ind);
    ShadowHook_Install(0x0009f0b0, (const void *)&FakeReSpawn);
    ShadowHook_Install(0x0006d8f0, (const void *)&FakeClosestLights);
    ShadowHook_Install(0x0002c5b0, (const void *)&FakeLineOfSight);
    ShadowHook_Install(0x0002c4e0, (const void *)&FakeRayIntersect);
    ShadowHook_Install(0x0002c610, (const void *)&FakeSphereIntersect);
    ShadowHook_Install(0x00020c30, (const void *)&FakeFindCel);
    ShadowHook_Install(0x00020eb0, (const void *)&FakePointOnFloor);
    ShadowHook_Install(0x0002d050, (const void *)&FakeUnlink);
    ShadowHook_Install(0x000240c0, (const void *)&FakeCameraSet);
    ShadowHook_Install(0x00024150, (const void *)&FakeCameraEnable);
    ShadowHook_Install(0x000243b0, (const void *)&FakeSetFade);
    ShadowHook_Install(0x00025190, (const void *)&FakeCalcViewAngles);
    ShadowHook_Install(0x00024490, (const void *)&FakeApplySwing);
    ShadowHook_Install(0x000245a0, (const void *)&FakePlace);
    ShadowHook_Install(0x00024610, (const void *)&FakePlace);
    ShadowHook_Install(0x00093960, (const void *)&FakeMenuCreate);
    ShadowHook_Install(0x0006afb0, (const void *)&FakeIsPaused);
    ShadowHook_Install(0x00068db0, (const void *)&FakeBreath);
    ShadowHook_Install(0x00068f20, (const void *)&FakeRipple);
    ShadowHook_Install(0x0006c850, (const void *)&FakeHurtType);
    ShadowHook_Install(0x0006c820, (const void *)&FakeHurtDamage);
    ShadowHook_Install(0x00031eb0, (const void *)&FakeModPlayerHitDamage);
    ShadowHook_Install(0x000b7c00, (const void *)&FakeObjCall2);    // Player_PositionGun
    ShadowHook_Install(0x000b8940, (const void *)&FakeObjCall);     // Player_LaserPointer
    ShadowHook_Install(0x000b9fc0, (const void *)&FakeMuzzleFlash);
    ShadowHook_Install(0x000b7f20, (const void *)&FakeObjCall);     // Draw_MuzzleFlash
    ShadowHook_Install(0x000b8cb0, (const void *)&FakeObjCall);     // Player_SetWatch
    ShadowHook_Install(0x000bd740, (const void *)&FakeObjCall);     // Player_WeaponRecoil
    ShadowHook_Install(0x000ba8f0, (const void *)&FakeObjCall);     // Player_Weapon
    ShadowHook_Install(0x000bb300, (const void *)&FakeWeaponFiring);
    ShadowHook_Install(0x000bc1e0, (const void *)&FakeInitWeapon);
    ShadowHook_Install(0x000ce4b0, (const void *)&FakeBodyGlow);
    ShadowHook_Install(0x0002e320, (const void *)&FakeCreateObjEx);
    ShadowHook_Install(0x0006b610, (const void *)&FakeCelglist);
    ShadowHook_Install(0x000ab450, (const void *)&FakeFeetOnPoint);
    ShadowHook_Install(0x000ae930, (const void *)&FakeObjCall2);    // PlayerCollLadder (the third argument unlogged)
    ShadowHook_Install(0x000c4000, (const void *)&FakeObjCall2);    // SP_Activate
    ShadowHook_Install(0x0002f1f0, (const void *)&FakeDoorActivate);
    ShadowHook_Install(0x000cb310, (const void *)&FakeObjCall);     // SS_Activate
    ShadowHook_Install(0x000d2ab0, (const void *)&FakeObjCall2);    // Trigger_Activate
    ShadowHook_Install(0x000d05e0, (const void *)&FakeObjCall);     // PCQWorm_Activate
    ShadowHook_Install(0x000d0230, (const void *)&FakeObjCall2);    // Lock_Activate
    ShadowHook_Install(0x000d12c0, (const void *)&FakeObjCall2);    // Monitor_Activate
    ShadowHook_Install(0x000268e0, (const void *)&FakeObjCall2);    // Car_Activate
    ShadowHook_Install(0x00026d00, (const void *)&FakeObjCall2);    // GunImp_Activate
    ShadowHook_Install(0x0009a750, (const void *)&FakeObjCall);     // GT_Activate
    ShadowHook_Install(0x000db3a0, (const void *)&FakeOtherViews);
    ShadowHook_Install(0x000db3d0, (const void *)&FakeObjCall);     // View_SetDrawInNoViews
    ShadowHook_Install(0x000aa590, (const void *)&FakeSwim);
    ShadowHook_Install(0x000ab180, (const void *)&FakeClampSomeAngles);
    ShadowHook_Install(0x000ab0b0, (const void *)&FakeBlurAndFade);
}

// ---- the made-up world

float Pick(const float *values, int count) {
    if (ShadowRand(24) == 0)
        return ShadowRand(2) ? NAN : -0.0f;
    return values[ShadowRand(count)];
}
#define PICK(...) ([]() { static const float v[] = {__VA_ARGS__}; return Pick(v, (int)(sizeof(v) / sizeof(v[0]))); }())

void RandomVector(_VECTOR *v, float range) {
    v->x = ShadowRandFloat(-range, range);
    v->y = ShadowRandFloat(-range, range);
    v->z = ShadowRandFloat(-range, range);
}

// An orthonormal matrix turned by yaw and pitch, at pos
void RandomMatrix(_MATRIX *m, const _VECTOR *pos) {
    float yaw = ShadowRandFloat(-3.2f, 3.2f), pitch = ShadowRandFloat(-1.5f, 1.5f);
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    float rows[15] = {cy, 0.0f, -sy, 0.0f, sy * sp, cp, cy * sp, 0.0f, sy * cp, -sp, cy * cp, 0.0f,
                      pos->x, pos->y, pos->z};
    memcpy(m->m, rows, sizeof(rows));
}

void RandomObj(obj_tag *o, int type, void *data) {
    memset(o, 0, sizeof(*o));
    o->objectType = (char)type;
    o->extraObjectData = data;
    RandomVector(&o->position, 6.0f);
    o->lastPosition = o->position;
    if (ShadowRand(3) == 0)
        RandomVector(&o->lastPosition, 6.0f);
    o->centrePoint = o->position;
    o->rotation.x = ShadowRandFloat(-0.5f, 0.5f);
    o->rotation.y = ShadowRandFloat(-3.2f, 3.2f);
    o->rotation.z = ShadowRand(3) ? 0.0f : ShadowRandFloat(-0.5f, 0.5f);
    o->radius = PICK(0.5f, 2.0f, 4.0f, 6.5f, 10.0f);
    RandomMatrix(&o->transformMatrix, &o->position);
    o->flags = (char)ShadowRand(2);
    o->subState = (ushort)ShadowRand(3);
}

void RandomHits(void) {
    obj_tag *targets[] = {NULL, &world.bulletObj, &world.hurt, &world.line, &world.door, &world.player,
                          &world.bulletObj};
    for (int i = 0; i < HITS; i++) {
        HITDATA_tag *h = &world.hits[i];
        memset(h, 0, sizeof(*h));
        h->next = (i + 1 < HITS && ShadowRand(3)) ? &world.hits[i + 1] : NULL;
        h->dmgAmt = PICK(0.0f, 1.0f, 10.0f, 35.5f, 120.0f);
        RandomVector(&h->hitPosition, 6.0f);
        if (ShadowRand(2))
            h->hitPosition.y = world.player.position.y + ShadowRandFloat(-1.0f, 1.0f);
        RandomVector(&h->maybehitDirection, 1.0f);
        static const char materials[] = {0, 0xd, 0xe, 0x4d, 0x0e, 3};
        h->materialType = materials[ShadowRand(6)];
        h->flags = (char)ShadowRand(4);
        static const short bones[] = {0, -1, 1, 2, 4, 5, 6, 20, 21, 32, 35, 49, 56, 57};
        h->hitBoneIdx = bones[ShadowRand(14)];
        h->hitObj = targets[ShadowRand(7)];
        h->hitCel = (cel_tag *)world.cel;
    }
}

void RandomGlobals(void) {
    static const HASHCODE levels[] = {HT_Level_CastleIndoors1, HT_Level_Tower2B, (HASHCODE)0x07000001,
                                      (HASHCODE)0x07000004, (HASHCODE)0x07000007, (HASHCODE)0x0700001b,
                                      HT_Level_HendersonA, (HASHCODE)0x07000013};
    GameState.CurrentLevelHashcode = levels[ShadowRand(8)];
    GameState.difficultyModifier = ShadowRand(5);
    GameState.NumFramesUnpaused = ShadowRand(0x7fff);
    GameState.maybePaused = ShadowRand(5) == 0;
    MPSettings.isMultiplayer = ShadowRand(3) == 0;
    MPSettings.maybeDroneAIEnabled = ShadowRand(3) == 0;
    MPSettings.LocationDamageEnabled = ShadowRand(2);
    MPSettings.TripleDamageModifierProfessionalMode = ShadowRand(3) == 0;
    static const MultiplayerGameMode modes[] = {GM_QUICK, GM_TOPAGENT, GM_ASSASSIN, GM_TEAMARENA};
    MPSettings.GameMode = modes[ShadowRand(4)];
    MPSettings.MaxPoints = ShadowRand(4);
    MPSettings.maybeIsTeamGame = ShadowRand(2);
    MPSettings.FriendlyFire = ShadowRand(2);
    for (int i = 0; i < 10; i++) {
        snprintf(MPSettings.Player[i].Name, sizeof(MPSettings.Player[i].Name), "agent%d", i);
        MPSettings.Player[i].TeamId = (MPTeam)ShadowRand(2);
        MPSettings.Player[i].SkinNum = ShadowRand(29);
        MPSettings.Player[i].HealthModifier = ShadowRand(5) * 25 - 50;
        MPGame.players[i].deaths = ShadowRand(5);
        static const short injurers[] = {-2, -1, 0, 1, 4};
        MPGame.players[i].maybeIdxOfLastInjurer = injurers[ShadowRand(5)];
        MPGame.players[i].friendlyFireLabelTimer = (short)(ShadowRand(2) * ShadowRand(100));
    }
    for (int i = 0; i < 256; i++)
        switch_channels[i] = (char)(ShadowRand(3) == 0);
    memset(GameCheatInfo, 0, 0xc);
    GameCheatInfo[0] = ShadowRand(6) == 0;
    GameSurfaced = (uint8_t)ShadowRand(2);
    GameScanMode = ShadowRand(10) == 0;
    GameBenchmark = ShadowRand(12) == 0;
    GRAVITY_VECTOR.x = 0.0f;
    GRAVITY_VECTOR.y = ShadowRand(4) ? -9.8f : 0.0f;
    GRAVITY_VECTOR.z = 0.0f;
    if (ShadowRand(2)) {
        FRAME_RATE_INT = 60;
        _FRAME_RATE = 60.0f;
        FRAME_RATE_DIV = 1.0f;
        FRAME_RATE_MUL = 1.0f;
        REC_FRAME_RATE = 0.016666668f;
    }
    else {
        FRAME_RATE_INT = 50;
        _FRAME_RATE = 50.0f;
        FRAME_RATE_DIV = 0.8333333f;
        FRAME_RATE_MUL = 1.2f;
        REC_FRAME_RATE = 0.02f;
    }
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    PlayerInputs[0].crouchToggle = (undefined)ShadowRand(2);
    PlayerInputs[0].levelViewOnPace = (undefined)ShadowRand(2);
    PlayerInputs[0].hudVisibility = (undefined)ShadowRand(2);
    obj_tag **assassin = (obj_tag **)GameAssassin;
    assassin[0] = ShadowRand(2) ? &world.player : NULL;     // AssassinTarget
    assassin[1] = ShadowRand(2) ? &world.shooter : NULL;    // CurrentAssassinObjId
    static const uint32_t wireAnims[] = {0x5e5, 0x5ec, 0x5ed, 0x5ee, 0x5ea, 0xb5, 0xb4, 0x96};
    world.wireAnim = ShadowRand(3) ? 0x06000000 + wireAnims[ShadowRand(8)] : 0;
}

void RandomWorld(void) {
    for (int i = 0; i < 16; i++)
        world.args[i] = ShadowRand(0x10000);
    for (int i = 0; i < STREAM; i++)
        world.stream[i] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    world.streamPos = 0;
    RandomGlobals();

    // The player
    BLData *bl = Bl();
    AnimState *anim = Anim();
    memset(world.bl, 0, sizeof(world.bl));
    memset(world.anim, 0, sizeof(world.anim));
    memset(world.newAnim, 0, sizeof(world.newAnim));
    RandomObj(&world.player, ShadowRand(6) ? OBJECTTYPE_PLAYER : OBJECTTYPE_DEAD_PLAYER, bl);
    world.player.animState = anim;
    world.player.inCel = ShadowRand(5) ? (cel_tag *)world.cel : NULL;
    static const ushort subStates[] = {0, 0, 1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    world.player.subState = subStates[ShadowRand(19)];
    world.player.curState = (ushort)(ShadowRand(5) ? 1 : ShadowRand(5));
    world.player.hitList = ShadowRand(3) ? &world.hits[0] : NULL;
    world.player.ambientR = (uchar)ShadowRand(256);
    world.player.ambientG = (uchar)ShadowRand(256);
    world.player.ambientB = (uchar)ShadowRand(256);
    world.player.effectFlags = ShadowRand(0x10000);
    world.player.transformFlags = (ushort)ShadowRand(0x80);
    world.player.creationTimeFrames = (int)ShadowRand(0x7fff);

    bl->playerNum = 0;
    bl->hudInfo = &world.hud;
    bl->weaponObject = &world.weapon;
    bl->muzzleFlashObj = &world.muzzleFlash;
    bl->attachedToSpecialMovementItem = &world.line;
    bl->rope = ShadowRand(5) ? &world.rope : NULL;
    bl->grappleObj = ShadowRand(3) ? &world.grapple : NULL;
    // Every original but Player_Update's remote control reads the device unchecked
    bl->remoteControlDevice = (world.player.subState != MovementType_RemoteControl || ShadowRand(2)) ? &world.device
                                                                                                    : NULL;
    RandomVector(&bl->lastPosition, 6.0f);
    RandomVector(&bl->movement, 0.3f);
    RandomVector(&bl->lastMovement, 0.3f);
    RandomVector(&bl->rotationDelta, 0.2f);
    RandomVector(&bl->lastRotationDelta, 0.2f);
    RandomVector(&bl->field_0x3c, 20.0f);
    if (ShadowRand(3) == 0)
        bl->field_0x3c.y = PICK(-50.0f, -45.0f, 0.0f, 50.0f, 45.000004f);
    RandomVector(&bl->muzzle, 6.0f);
    RandomVector(&bl->grapplePoint, 6.0f);
    if (ShadowRand(3) == 0)
        bl->grapplePoint = world.player.position;
    bl->grappleSpeed = PICK(0.0f, 0.4f, 0.47f, 1.48f, 1.5f);
    static const short grappleStates[] = {0, 1, 1, 2, 3, 4};
    bl->grappleState = grappleStates[ShadowRand(6)];
    bl->grappleWait = (short)ShadowRand(3);
    bl->movementDisabled = ShadowRand(4) == 0;
    bl->floorNormalY = PICK(1.0f, 0.5f, 0.49999997f, 0.1f);
    bl->aimX = PICK(0.0f, 0.2f, -0.296f, 0.29599997f, 0.3f, -0.31f);
    bl->aimY = PICK(0.0f, 0.39999998f, -0.4f, 0.41f, 0.1f);
    bl->aimTurnX = PICK(0.0f, 0.1f);
    bl->aimTurnY = PICK(0.0f, -0.1f);
    bl->aimAutoLevelState = (char)ShadowRand(4);
    bl->walkClass = (char)ShadowRand(3);
    bl->health = PICK(100.0f, 0.0f, -5.0f, 1.0f, 0.99999994f, 600.0f, 33.0f);
    bl->armor = PICK(0.0f, 10.0f, 50.0f, 0.5f);
    bl->pitchFromHorizontal = PICK(0.0f, 0.5f, -0.99f, 1.0f);
    bl->pitchAutoLevelTarget = PICK(0.0f, 0.3f);
    bl->zoom = PICK(1.0f, 2.0f, 4.5f);
    bl->scopeAccelX = PICK(0.015f, 0.03f, 0.06f, -0.02f);
    bl->scopeAccelY = PICK(0.011f, 0.03f);
    bl->aimAccelX = PICK(0.05f, 0.1f, 0.2f, -0.05f);
    bl->aimAccelY = PICK(0.06f, 0.15f);
    bl->turnAccelState = PICK(0.04f, 0.08f);
    bl->lookAccel = PICK(0.01f, 0.02f, 0.031f);
    bl->headSwimOffset = PICK(0.7f, 0.0f, 0.35f);
    bl->headCrouchOffset = PICK(0.0f, 0.45f, 0.2f);
    bl->air = PICK(100.0f, 50.0f, 0.5f, 1.0f, -2.0f, 101.0f);
    // Not a NaN: the sum of two NaNs in Player_MonitorAir takes its sign from the x87's rule (X87Ops.h)
    static const float underwaters[] = {0.0f, 0.5f, 1.0f, -0.25f};
    bl->underwater = underwaters[ShadowRand(4)];
    bl->waterFlags = ShadowRand(0x20);
    bl->waterSound = ShadowRand(2) ? (DYNAMICSOUNDS *)&world.cels[3] : NULL;
    bl->ropeFatigue = PICK(0.0f, 50.0f, 99.99f, 100.0f, 100.5f);
    bl->alpha = PICK(0.0f, 0.3f, 0.5f, 1.0f, 1.2f);
    bl->turnKick = PICK(0.0f, 0.1f, -0.1f);
    bl->visibility = 0.5f;
    bl->flashTime = PICK(0.0f, 2.0f);
    bl->fallFrames = PICK(0.0f, 30.0f, 60.0f, 70.0f, 70.5f, 200.0f);
    bl->stepDistance = 3.0f;
    bl->groundBits = (ushort)(ShadowRand(3) ? ShadowRand(16) : 0x8000);
    bl->previousSubState = (short)subStates[ShadowRand(19)];
    // Player_Update lets go of the device and goes back to the substate before, where Player_Collision would read
    // the device unchecked on an emplacement
    if (world.player.subState == MovementType_RemoteControl && bl->previousSubState == MovementType_Emplacement)
        bl->previousSubState = MovementType_Walk;
    bl->remoteControlTimer = (short)(ShadowRand(3) ? 0 : ShadowRand(3));
    bl->active = (char)(ShadowRand(5) ? 1 : ShadowRand(3));
    bl->frozen = ShadowRand(6) == 0;
    bl->jumpState = (char)ShadowRand(4);
    bl->traverseStep = (char)(subStates[ShadowRand(19)] % 13);
    bl->camMode = (char)ShadowRand(3);
    bl->traverseWait = (char)(ShadowRand(3) ? 0 : ShadowRand(5));
    bl->jumpWait = (char)(ShadowRand(2) ? 0 : ShadowRand(5));
    bl->field_0x8e3 = (char)ShadowRand(2);
    bl->shotFired = (char)ShadowRand(2);
    static const uchar icons[] = {THIRDICON_GRAPPLE, THIRDICON_WIRE, THIRDICON_STANDING, THIRDICON_NONE};
    bl->thirdIcon = icons[ShadowRand(4)];
    bl->animWait = (char)(ShadowRand(2) ? 0 : ShadowRand(4));
    bl->hitDirectionFade = (uchar)ShadowRand(256);
    bl->muzzleFlashFrames = (short)ShadowRand(3);
    bl->field_0x8cc = (short)(ShadowRand(3) - 1);
    bl->animSet = (sAnimSet_tag *)&world.cels[1];
    static const int gadgets[] = {0x33, 0x45, 0x4a, 0x4c, 0x4e, 0x4f};
    for (int gadget : gadgets)
        bl->weaponStats[gadget].clipOrCooldown = (short)(weapon_data[gadget].clipSize - (short)ShadowRand(3));

    anim->animFlags = (char)ShadowRand(256);
    static const char weapons[] = {0x01, 0x47, 0x50, 0x51, 0x1a, 0x10, 0x30, 0x4e};
    anim->currentWeaponId = weapons[ShadowRand(8)];
    anim->switchingToWeaponId = weapons[ShadowRand(8)];
    anim->prevHeldWeaponId = weapons[ShadowRand(8)];
    anim->inWater = (char)ShadowRand(4);
    static const char floors[] = {0, 0xd, 0xe, 3};
    anim->floor.materialType = floors[ShadowRand(4)];
    anim->floor.hitObj = ShadowRand(2) ? &world.door : NULL;
    anim->animObj.rootHeight = PICK(0.3f, 0.55f, 0.6f, 1.2f);

    // The view and the cel
    memset(world.viewer, 0, sizeof(world.viewer));
    viewer_tag *viewer = Viewer();
    viewer->idx = 0;
    RandomVector(&viewer->pos, 6.0f);
    RandomVector(&viewer->someVec, 3.0f);
    viewer->fovRadians = PICK(1.0471976f, 0.21f, 3.13f, 0.2f);
    RandomMatrix(&viewer->viewMatrix, &viewer->pos);
    RandomMatrix(&viewer->worldMatrix, &viewer->pos);
    viewer->trackPitch = PICK(0.5f, 2.0f);
    viewer->trackYaw = PICK(0.0f, 1.0f);
    viewer->trackDistance = PICK(4.0f, 0.0f, 2.5f);
    // Short of wrapping, and never NaN: normaliseToPlusMinusPi's fmodf needs the C runtime started, which it is not yet
    static const float shearAngles[] = {0.0f, 3.1f, -1.0f, -3.1f, -0.0f};
    viewer->shearAngle = shearAngles[ShadowRand(5)];
    memset(world.cel, 0, sizeof(world.cel));
    Cel()->shearHeight = world.player.position.y + PICK(-2.0f, 0.2f, 0.3f, 0.30000001f, 1.0f, 3.0f);
    Cel()->musicEvent = (uchar)(ShadowRand(3) ? 0 : 7);
    memset(&world.hud, 0, sizeof(world.hud));
    world.hud.pane[Blood].enabled = ShadowRand(2) != 0;
    memset(world.celgl, 0, sizeof(world.celgl));
    ((celglist_tag *)world.celgl)->applyFlagsToObject = 0x40 | ShadowRand(16);

    // The things about
    RandomObj(&world.weapon, OBJECTTYPE_GFX, NULL);
    world.weapon.curState = (ushort)(ShadowRand(3) ? 0 : 9);
    RandomObj(&world.muzzleFlash, OBJECTTYPE_GFX, NULL);
    RandomObj(&world.rope, OBJECTTYPE_GFX, NULL);
    static const int lineTypes[] = {OBJECTTYPE_THIRDCAM_OR_WIRE, OBJECTTYPE_THIRDCAM_OR_WIRE, OBJECTTYPE_CREEPWALL,
                                    OBJECTTYPE_LADDER};
    RandomObj(&world.line, lineTypes[ShadowRand(4)], world.lineData);
    if (ShadowRand(3) == 0)
        world.line.position = world.player.position;
    world.line.centrePoint = world.line.position;
    if (ShadowRand(2))
        world.line.centrePoint.y = world.player.position.y + ShadowRandFloat(-1.0f, 2.0f);
    WIRE *wire = (WIRE *)world.lineData;
    wire->type = (short)ShadowRand(4);
    wire->thirdCam = (ushort)(ShadowRand(3) ? 0 : 1);
    wire->field_4 = (short)ShadowRand(3);
    wire->channel = (short)(ShadowRand(2) ? 0 : 9);
    RandomObj(&world.grapple, OBJECTTYPE_GRAPPLE, NULL);
    RandomObj(&world.thirdCam, OBJECTTYPE_GFX, &world.thirdCamData);
    world.thirdCamData.id = 1;
    world.thirdCamData.path = &world.cels[0];
    world.thirdCamData.lastKey = (ushort)ShadowRand(9);
    world.thirdCamData.show = (uchar)ShadowRand(2);
    ThirdPersonCameras[1] = &world.thirdCam;
    RandomObj(&world.device, ShadowRand(2) ? OBJECTTYPE_GUNTURRET : OBJECTTYPE_GUNTURRET2, NULL);
    RandomObj(&world.bulletObj, OBJECTTYPE_BULLET, &world.bullet);
    memset(&world.bullet, 0, sizeof(world.bullet));
    world.bullet.firedByObj = ShadowRand(4) ? &world.shooter : NULL;
    static const int bulletWeapons[] = {0x01, 0x64, 0x65, 0x66, 0x10, 0x1a, 0x54, 0x63};
    world.bullet.wpnDef = &weapon_data[bulletWeapons[ShadowRand(8)]];
    if (ShadowRand(2)) {
        world.bulletWeapon = *world.bullet.wpnDef;
        world.bulletWeapon.weaponBaseNum = (short)bulletWeapons[ShadowRand(8)];
        world.bullet.wpnDef = &world.bulletWeapon;
    }
    RandomObj(&world.shooter, OBJECTTYPE_DRONE, &world.drone);
    memset(&world.drone, 0, sizeof(world.drone));
    world.drone.botVars = &world.bot;
    world.bot.stats.traitFlags = (uchar)ShadowRand(8);
    RandomObj(&world.hurt, OBJECTTYPE_HURT, NULL);
    static const int usable[] = {OBJECTTYPE_DOOR, OBJECTTYPE_TRIGGER, OBJECTTYPE_SIMPLESCRIPT, OBJECTTYPE_CAR,
                                 OBJECTTYPE_GUNTURRET, OBJECTTYPE_MONITOR, OBJECTTYPE_LOCK, OBJECTTYPE_GUNTURRET2,
                                 OBJECTTYPE_QWORM, 0};
    RandomObj(&world.door, usable[ShadowRand(10)], NULL);
    world.door.scriptPlayer = ShadowRand(3) ? NULL : &world.cels[0];
    RandomObj(&world.pickup, OBJECTTYPE_PICKUP, NULL);
    RandomObj(&world.glow, OBJECTTYPE_GFX, NULL);
    for (int i = 0; i < 2; i++) {
        RandomObj(&world.objs[i], ShadowRand(2) ? OBJECTTYPE_THIRDCAM_OR_WIRE : OBJECTTYPE_CREEPWALL, world.objData[i]);
        world.objs[i].nextObject = i == 0 ? &world.objs[1] : (ShadowRand(2) ? &world.line : NULL);
        memset(world.objData[i], 0, 8);
    }
    ShadowFakes.firstObject = ShadowRand(4) ? &world.objs[0] : NULL;
    RandomHits();

    memset(&world.script, 0, sizeof(world.script));
    world.script.speed = PICK(1.0f, -1.0f, 0.5f);
    world.script.flags = ShadowRand(0x10000) | 0x80000000u;
    for (int i = 0; i < LIGHTS; i++) {
        memset(&world.lights[i], 0, sizeof(world.lights[i]));
        RandomVector(&world.lights[i].position, 6.0f);
        world.lights[i].brightness = PICK(1.0f, 5.0f, 12.0f);
        world.lights[i].clr_r = (uchar)ShadowRand(256);
        world.lights[i].clr_g = (uchar)ShadowRand(256);
        world.lights[i].clr_b = (uchar)ShadowRand(256);
        world.lightList[i] = &world.lights[i];
    }
    memset(world.text, 0, sizeof(world.text));
    RandomVector(&world.vectorArg, 6.0f);
    world.gravityArg = GRAVITY_VECTOR;
    RandomMatrix(&world.matrixArg, &world.vectorArg);
    memset(world.placement, 0, sizeof(world.placement));
    uint32_t *params = (uint32_t *)(world.placement + 0x2c);
    params[0] = ShadowRand(3) ? ShadowRand(3) : 9;
    params[1] = ShadowRand(4);
    params[2] = ShadowRand(3) ? 0 : 9;
    params[3] = ShadowRand(4);
    *(ushort *)(world.placement + 2) = (ushort)ShadowRand(5);
    *(void **)(world.placement + 8) = &world.cels[0];
}

// ---- calling: registers set for the register-argument functions, and the wire anim moved in and out

struct Regs {
    uint32_t eax, ebx, ecx, edx, esi, edi;
};

__declspec(naked) uint32_t __cdecl CallRegs(unsigned at, const Regs *r, int argc, const uint32_t *args) {
    _asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ecx, [ebp + 16]
        mov edx, [ebp + 20]
    pushing:
        test ecx, ecx
        jz pushed
        push dword ptr [edx + ecx * 4 - 4]
        dec ecx
        jmp pushing
    pushed:
        mov eax, [ebp + 12]
        mov ebx, [eax + 4]
        mov ecx, [eax + 8]
        mov edx, [eax + 12]
        mov esi, [eax + 16]
        mov edi, [eax + 20]
        mov eax, [eax]
        call dword ptr [ebp + 8]
        mov ecx, [ebp + 16]
        lea esp, [esp + ecx * 4]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

uint32_t P(const void *p) { return (uint32_t)(uintptr_t)p; }

// The cases' arguments: the same draws for the original's run and ours
uint32_t argPos;
uint32_t Arg(uint32_t n) {
    uint32_t v = world.args[argPos++ % 16];
    return n ? v % n : v;
}
float ArgPick(const float *values, int count) {
    if (Arg(24) == 0)
        return Arg(2) ? NAN : -0.0f;
    return values[Arg(count)];
}
#define APICK(...) ([]() { static const float v[] = {__VA_ARGS__}; return ArgPick(v, (int)(sizeof(v) / sizeof(v[0]))); }())

typedef uint32_t (*Runner)(bool original);

struct Case {
    const char *name;
    Runner run;
};

const Case *current;

// The rows' fourth floats of the matrices the cases change: the originals leave stack there (RotMatrix does not write
// them, and Mat_CopyRot copies them), so they are not compared
void ClearMatrixPads(void) {
    _MATRIX *matrices[] = {&world.player.transformMatrix, &world.rope.transformMatrix, &world.line.transformMatrix,
                           &Viewer()->viewMatrix, &world.matrixArg, &Bl()->zeroGUpright};
    for (_MATRIX *m : matrices) {
        m->m[3] = 0.0f;
        m->m[7] = 0.0f;
        m->m[11] = 0.0f;
    }
}

// The wire anim: the original's copy or ours, whichever the run changed (an original can reach our ports underneath)
void WireAnimIn(void) {
    GameWireAnim = world.wireAnim;
    WireAnim = (HASHCODE)world.wireAnim;
}
void WireAnimOut(void) {
    world.wireAnim = GameWireAnim != world.wireAnim ? GameWireAnim : (uint32_t)WireAnim;
}

uint32_t RunTheirs() {
    argPos = 0;
    WireAnimIn();
    uint32_t result = current->run(true);
    WireAnimOut();
    ClearMatrixPads();
    return result;
}

uint32_t RunOurs() {
    argPos = 0;
    WireAnimIn();
    uint32_t result = current->run(false);
    WireAnimOut();
    ClearMatrixPads();
    return result;
}

// A register-argument function: the original at its address, ours by its adaptor
uint32_t Regs6(bool original, unsigned at, const void *ours, Regs r, int argc, const uint32_t *args) {
    if (original) {
        XbeOriginalScope o(at);
        return CallRegs(at, &r, argc, args);
    }
    return CallRegs((unsigned)(uintptr_t)ours, &r, argc, args);
}

// A cdecl function: the original at its address, ours by name
#define CDECL_RUN(at, ours, type, ...)                                                                                \
    (original ? ([&]() { XbeOriginalScope o(at); return ORIGINAL(at, type)(__VA_ARGS__); }()) : ours(__VA_ARGS__))

obj_tag *Pl() { return &world.player; }

// ---- the cases

uint32_t RunSetHealth(bool original) {
    float h = APICK(50.0f, -1.0f, 0.0f, 1e-30f);
    CDECL_RUN(0x000ac050, Player_SetHealth, void (__cdecl *)(BLData *, float), Bl(), h);
    return 0;
}
uint32_t RunSetFlashBang(bool original) {
    BLData *bl = Arg(5) ? Bl() : NULL;
    CDECL_RUN(0x000abdc0, Player_SetFlashBang, void (__cdecl *)(BLData *, float, uchar), bl, 3.5f, 0x80);
    return 0;
}
uint32_t RunClearInertia(bool original) {
    CDECL_RUN(0x000ab240, Player_ClearInertia, void (__cdecl *)(obj_tag *), Pl());
    CDECL_RUN(0x000b91c0, Player_ClearAimInertia, void (__cdecl *)(obj_tag *), Pl());
    return 0;
}
uint32_t RunGetHeadPos(bool original) {
    _MATRIX *m = Arg(2) ? &world.matrixArg : NULL;
    CDECL_RUN(0x000a89a0, Player_GetHeadPos, void (__cdecl *)(_VECTOR *, obj_tag *, _MATRIX *), &world.vectorArg,
              Pl(), m);
    return 0;
}
uint32_t RunInWater(bool original) {
    Regs r = {};
    r.esi = P(Pl());
    r.edi = P(Bl());
    return Regs6(original, 0x000ab280, (const void *)&Player_InWater, r, 0, NULL);
}
uint32_t RunInShadow(bool original) {
    CDECL_RUN(0x000aafb0, Player_InShadow, void (__cdecl *)(BLData *, obj_tag *), Bl(), Pl());
    return 0;
}
uint32_t RunHandlePain(bool original) {
    static const short locations[] = {-1, 5, 20, 33, 49, 56, 0};
    static const ushort types[] = {0, 1, 4, 5, 6, 7, 8, 0x1234};
    Regs r = {};
    r.esi = P(Bl());
    r.edi = 0xabcd0000u | (ushort)locations[Arg(7)];
    r.ebx = 0x55aa0000u | types[Arg(8)];
    float damage = APICK(10.0f, 0.0f, -1.0f, 150.0f, 2.0f, 6.0f);
    uint32_t args[2] = {P(Pl()), Bits(damage)};
    Regs6(original, 0x000ac0a0, (const void *)&Player_HandlePain, r, 2, args);
    return 0;
}
uint32_t RunCheckForDeath(bool original) {
    uint type = Arg(3) ? OBJECTTYPE_PLAYER : OBJECTTYPE_DEAD_PLAYER;
    return (uint32_t)CDECL_RUN(0x000ac390, Player_CheckForDeath, bool (__cdecl *)(obj_tag *, uint), Pl(), type);
}
uint32_t RunHandleDeath(bool original) {
    Regs r = {};
    r.eax = P(Pl());
    Regs6(original, 0x000ac690, (const void *)&Player_HandleDeath, r, 0, NULL);
    return 0;
}
uint32_t RunMonitorAir(bool original) {
    Regs r = {};
    r.eax = P(Pl());
    r.ecx = P(Bl());
    Regs6(original, 0x000acf50, (const void *)&Player_MonitorAir, r, 0, NULL);
    return 0;
}
uint32_t RunChangeSubState(bool original) {
    static const ushort states[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 13, 14, 15, 16, 17, 0xffff};
    ushort state = states[Arg(17)];
    return (uint32_t)CDECL_RUN(0x000a9720, Player_ChangeSubState, int (__cdecl *)(obj_tag *, ushort), Pl(), state);
}
uint32_t RunAnimSetInitDead(bool original) {
    CDECL_RUN(0x000a96a0, PlayerAnimSetInitDead, void (__cdecl *)(obj_tag *), Pl());
    return 0;
}
uint32_t RunStandAtNewPosition(bool original) {
    static const ushort states[] = {0, 3, 4, 8};
    ushort state = states[Arg(4)];
    CDECL_RUN(0x000ac7e0, Player_StandAtNewPosition, void (__cdecl *)(obj_tag *, _VECTOR *, _VECTOR *, ushort), Pl(),
              &world.vectorArg, &world.line.rotation, state);
    return 0;
}
uint32_t RunSSWalk(bool original) {
    Regs r = {};
    r.eax = P(Pl());
    r.ecx = P(Bl());
    Regs6(original, 0x000ad210, (const void *)&Player_SSWalk, r, 0, NULL);
    return 0;
}
uint32_t RunSSCrouch(bool original) {
    Regs r = {};
    r.eax = P(Pl());
    r.ecx = P(Bl());
    Regs6(original, 0x000ad340, (const void *)&Player_SSCrouch, r, 0, NULL);
    return 0;
}
uint32_t RunHandleJump(bool original) {
    Regs r = {};
    r.eax = P(Bl());
    r.ecx = P(Pl());
    uint32_t args[1] = {P(&world.gravityArg)};
    Regs6(original, 0x000a9990, (const void *)&Player_HandleJump, r, 1, args);
    return 0;
}
uint32_t RunWalkClass(bool original) {
    float speed = APICK(0.0f, 0.2f, 0.20000002f, -0.5f, 0.8f, 0.79999995f, -1.0f);
    CDECL_RUN(0x000a9160, Player_UpdateWalkClass, void (__cdecl *)(obj_tag *, float, float), Pl(), speed, 0.0f);
    return 0;
}
uint32_t RunZeroG(bool original) {
    Regs r = {};
    r.esi = P(Bl());
    uint32_t args[1] = {P(Pl())};
    Regs6(original, 0x000aa6f0, (const void *)&Player_ZeroG, r, 1, args);
    return 0;
}
// The first Concat multiplies the matrix by RotMatrixY's, whose translation is never written: the original leaves
// stack in the matrix's translation x and z (Player_Update puts the position there afterwards), so they are not
// compared
uint32_t RunScanMode(bool original) {
    Regs r = {};
    r.eax = P(Bl());
    r.ebx = P(Pl());
    Regs6(original, 0x000aac40, (const void *)&Player_SSScanMode, r, 0, NULL);
    Mat_Position(world.player.transformMatrix)->x = 0.0f;
    Mat_Position(world.player.transformMatrix)->z = 0.0f;
    return 0;
}

// AccelFunc0 and its flagged twin return in ST0: compared as the double they leave
uint64_t accelResult;
uint32_t RunAccel(bool original) {
    float input = APICK(0.0f, 0.04f, 0.05f, 0.06f, 0.5f, -0.95f, 0.95f, 1.0f, -1.0f);
    float speed = APICK(0.015f, 0.03f, 0.045f, 0.06f, -0.05f, 0.2f, 0.0f);
    float base = APICK(0.015f, 0.05f, 0.04f);
    float mul = APICK(3.0f, 2.0f, 1.0f, 0.5f);
    float steps = APICK(120.0f, 1.0f, 30.0f);
    double result;
    if (Arg(2)) {
        result = CDECL_RUN(0x000b7420, (ORIGINAL((uintptr_t)&AccelFunc0, double (__cdecl *)(float, float *, float, float,
                                                                                           float, float, float))),
                           double (__cdecl *)(float, float *, float, float, float, float, float), input, &speed, base,
                           mul, steps, 0.05f, 0.95f);
    }
    else {
        int full = (int)Arg(2);
        result = CDECL_RUN(0x000b7520, AccelFunc0Flag,
                           double (__cdecl *)(float, float *, float, float, float, float, int), input, &speed, base,
                           mul, steps, 0.05f, full);
    }
    accelResult = Bits64(result);
    return (uint32_t)accelResult ^ (uint32_t)(accelResult >> 32);
}
uint32_t RunAiming(bool original) {
    CDECL_RUN(0x000b7610, Player_Aiming, void (__cdecl *)(obj_tag *), Pl());
    return 0;
}
uint32_t RunCollision(bool original) {
    CDECL_RUN(0x000ab5d0, Player_Collision, void (__cdecl *)(obj_tag *), Pl());
    return 0;
}
uint32_t RunCollisionHandler(bool original) {
    CDECL_RUN(0x000ae1c0, Player_CollisionHandler, void (__cdecl *)(obj_tag *), Pl());
    return 0;
}
uint32_t RunDealWithObjHit(bool original) {
    float r = CDECL_RUN(0x000add00, Player_DealWithObjHit, float (__cdecl *)(obj_tag *, BLData *, HITDATA_tag *), Pl(),
                        Bl(), &world.hits[Arg(HITS)]);
    return Bits(r);
}
uint32_t RunActivate(bool original) {
    Regs r = {};
    r.eax = P(Bl());
    r.edi = P(Pl());
    return Regs6(original, 0x000b86b0, (const void *)&Player_Activate, r, 0, NULL) & 0xff;
}
uint32_t RunRegisterBulletHit(bool original) {
    ushort index = (ushort)Arg(3);
    obj_tag *targets[] = {NULL, &world.bulletObj, &world.shooter, &world.player, &world.device, &world.door};
    obj_tag *obj = targets[Arg(6)];
    return (uint32_t)CDECL_RUN(0x0009d700, MP_RegisterBulletHit, bool (__cdecl *)(ushort, obj_tag *, float), index, obj,
                               50.0f);
}
uint32_t RunInit(bool original) {
    glb_players[0] = Arg(6) ? NULL : &world.door;
    ushort num = 0;
    CDECL_RUN(0x000acab0, Player_Init, obj_tag *(__cdecl *)(ushort, _VECTOR *, _VECTOR *, level_tag *), num,
              &world.vectorArg, &world.line.rotation, (level_tag *)world.placement);
    return 0;
}
// Flying, the translation's x and z are left as RunScanMode says
uint32_t RunUpdate(bool original) {
    CDECL_RUN(0x000ad520, Player_Update, void (__cdecl *)(obj_tag *), Pl());
    if (world.player.subState == MovementType_ScanMode) {
        Mat_Position(world.player.transformMatrix)->x = 0.0f;
        Mat_Position(world.player.transformMatrix)->z = 0.0f;
    }
    return 0;
}
uint32_t RunClimb(bool original) {
    CDECL_RUN(0x000aeb30, Player_Climb, void (__cdecl *)(BLData *, obj_tag *), Bl(), Pl());
    return 0;
}
uint32_t RunLeaveLadder(bool original) {
    CDECL_RUN(0x000aeac0, Player_LeaveLadder, void (__cdecl *)(obj_tag *, BLData *), Pl(), Bl());
    return 0;
}
uint32_t RunInitGrapple(bool original) {
    Regs r = {};
    r.esi = P(Bl());
    uint32_t args[1] = {P(Pl())};
    Regs6(original, 0x000aa130, (const void *)&Player_InitGrapple, r, 1, args);
    return 0;
}
uint32_t RunSetGrapplePoint(bool original) {
    CDECL_RUN(0x000aa2a0, Player_SetGrapplePoint, void (__cdecl *)(BLData *, _VECTOR *, obj_tag *), Bl(),
              &world.vectorArg, &world.grapple);
    return 0;
}
uint32_t RunSSGrapple(bool original) {
    Regs r = {};
    r.eax = P(Bl());
    r.edi = P(Pl());
    Regs6(original, 0x000aa340, (const void *)&Player_SSGrapple, r, 0, NULL);
    return 0;
}
uint32_t RunGrappleSetRope(bool original) {
    Regs r = {};
    r.edi = P(Pl());
    Regs6(original, 0x000aa190, (const void *)&Player_GrappleSetRope, r, 0, NULL);
    return 0;
}
uint32_t RunCollWire(bool original) {
    CDECL_RUN(0x000af5a0, Player_CollWire, void (__cdecl *)(obj_tag *, BLData *, obj_tag *), Pl(), Bl(), &world.line);
    return 0;
}
uint32_t RunWire(bool original) {
    CDECL_RUN(0x000afb50, Player_Wire, void (__cdecl *)(obj_tag *), Pl());
    return 0;
}
uint32_t RunHang(bool original) {
    if (Arg(2))
        CDECL_RUN(0x000afa80, Player_HangByHandA, void (__cdecl *)(obj_tag *, _VECTOR *), Pl(), &world.vectorArg);
    else
        CDECL_RUN(0x000afaf0, Player_HangByHandB, void (__cdecl *)(obj_tag *, _VECTOR *), Pl(), &world.vectorArg);
    return 0;
}
uint32_t RunZipline(bool original) {
    CDECL_RUN(0x000b0180, Player_Zipline, void (__cdecl *)(obj_tag *), Pl());
    return 0;
}
uint32_t RunCollCreepWall(bool original) {
    CDECL_RUN(0x000aeda0, Player_CollCreepWall, void (__cdecl *)(obj_tag *, BLData *, obj_tag *), Pl(), Bl(),
              &world.line);
    return 0;
}
uint32_t RunCreep(bool original) {
    CDECL_RUN(0x000aefd0, Player_Creep, void (__cdecl *)(BLData *, obj_tag *), Bl(), Pl());
    return 0;
}

// The placements' create functions: the object comes from the create fake's pool (ShadowCreatePool)
#define CREATE_RUN(at, ours)                                                                                          \
    uint32_t Run##ours(bool original) {                                                                               \
        ShadowFakes.createFails = Arg(8) == 0;                                                                 \
        obj_tag *o = CDECL_RUN(at, ours, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *),     \
                               &world.vectorArg, &world.line.rotation, (level_tag *)world.placement,                  \
                               (celglist_tag *)world.celgl);                                                          \
        ShadowFakes.createFails = false;                                                                              \
        return o == NULL ? 0 : (uint32_t)((uint8_t *)o - ShadowCreatePool) + 1;                                       \
    }
CREATE_RUN(0x000b03b0, Wire_Create)
CREATE_RUN(0x000af3c0, CreepWall_Create)
CREATE_RUN(0x000b0440, Grapple_Create)
CREATE_RUN(0x000af440, ThirdIcon_Create)
CREATE_RUN(0x00025700, CamSubject_Create)

uint32_t RunThirdCam_Create(bool original) {
    obj_tag *o = CDECL_RUN(0x000253c0, ThirdCam_Create,
                           obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *), &world.vectorArg,
                           &world.line.rotation, (level_tag *)world.placement, (celglist_tag *)world.celgl);
    return o == NULL ? 0 : (uint32_t)((uint8_t *)o - ShadowCreatePool) + 1;
}

// The update functions run on the line object, made into an icon or camera subject
uint32_t RunThirdIconUpdate(bool original) {
    world.line.extraObjectData = world.placement;
    THIRDICON *data = (THIRDICON *)world.placement;
    static const uint icons[] = {2, 3, 6, 6, 7};
    data->icon = icons[Arg(5)];
    data->onChannel = (ushort)Arg(12);
    data->offChannel = (ushort)Arg(12);
    data->wait = (char)(Arg(3) - 1);
    world.line.curState = (ushort)Arg(2);
    world.line.hitList = &world.hits[0];
    CDECL_RUN(0x000af4d0, ThirdIcon_Update, void (__cdecl *)(obj_tag *), &world.line);
    return 0;
}
uint32_t RunCamSubjectUpdate(bool original) {
    world.line.hitList = &world.hits[0];
    world.line.subState = (ushort)Arg(16);
    CDECL_RUN(0x00025760, CamSubject_Update, void (__cdecl *)(obj_tag *), &world.line);
    return 0;
}
uint32_t RunThirdCamGetPos(bool original) {
    float along = APICK(0.0f, 0.5f, 1.0f, 0.33f);
    CDECL_RUN(0x00025410, ThirdCam_GetPos, void (__cdecl *)(obj_tag *, float, _MATRIX *), &world.thirdCam, along,
              &world.matrixArg);
    return 0;
}
uint32_t RunCameraTracking(bool original) {
    if (Arg(6) == 0)
        Bl()->attachedToSpecialMovementItem = NULL;
    CDECL_RUN(0x00025810, camera_tracking, void (__cdecl *)(obj_tag *, viewer_tag *), Pl(), Viewer());
    return 0;
}
uint32_t RunCameraShear(bool original) {
    viewer_tag *viewer = Arg(8) ? Viewer() : NULL;
    CDECL_RUN(0x00025610, Camera_Shear, void (__cdecl *)(viewer_tag *), viewer);
    return 0;
}

void Test(const char *group, const Case *list, int count, int trials, void (*fixups)(void) = NULL) {
    int groupCases = cases, groupDiffer = differ;
    for (int c = 0; c < count; c++) {
        current = &list[c];
        for (int trial = 0; trial < trials; trial++) {
            RandomWorld();
            if (fixups != NULL)
                fixups();
            cases++;
            if (!ShadowCompare(regions, "player", current->name, trial, RunTheirs, RunOurs))
                differ++;
        }
    }
    printf("[player] %s: %d cases, %d differ\n", group, cases - groupCases, differ - groupDiffer);
}

#define C(name, run) {name, run}

const Case kHealth[] = {
    C("Player_SetHealth", RunSetHealth),
    C("Player_SetFlashBang", RunSetFlashBang),
    C("Player_ClearInertia/ClearAimInertia", RunClearInertia),
    C("Player_GetHeadPos", RunGetHeadPos),
    C("Player_InWater", RunInWater),
    C("Player_InShadow", RunInShadow),
    C("Player_HandlePain", RunHandlePain),
    C("Player_CheckForDeath", RunCheckForDeath),
    C("Player_HandleDeath", RunHandleDeath),
    C("Player_MonitorAir", RunMonitorAir),
};

const Case kSubstates[] = {
    C("Player_ChangeSubState", RunChangeSubState),
    C("PlayerAnimSetInitDead", RunAnimSetInitDead),
    C("Player_StandAtNewPosition", RunStandAtNewPosition),
    C("Player_SSWalk", RunSSWalk),
    C("Player_SSCrouch", RunSSCrouch),
    C("Player_HandleJump", RunHandleJump),
    C("Player_UpdateWalkClass", RunWalkClass),
    C("Player_ZeroG", RunZeroG),
    C("Player_SSScanMode", RunScanMode),
    C("AccelFunc0/AccelFunc0Flag", RunAccel),
    C("Player_Aiming", RunAiming),
};

const Case kCollision[] = {
    C("Player_Collision", RunCollision),
    C("Player_CollisionHandler", RunCollisionHandler),
    C("Player_DealWithObjHit", RunDealWithObjHit),
    C("Player_Activate", RunActivate),
    C("MP_RegisterBulletHit", RunRegisterBulletHit),
};

const Case kTraverse[] = {
    C("Player_Climb", RunClimb),
    C("Player_LeaveLadder", RunLeaveLadder),
    C("Player_InitGrapple", RunInitGrapple),
    C("Player_SetGrapplePoint", RunSetGrapplePoint),
    C("Player_SSGrapple", RunSSGrapple),
    C("Player_GrappleSetRope", RunGrappleSetRope),
    C("Player_CollWire", RunCollWire),
    C("Player_Wire", RunWire),
    C("Player_HangByHandA/B", RunHang),
    C("Player_Zipline", RunZipline),
    C("Player_CollCreepWall", RunCollCreepWall),
    C("Player_Creep", RunCreep),
};

const Case kObjects[] = {
    C("Wire_Create", RunWire_Create),
    C("CreepWall_Create", RunCreepWall_Create),
    C("Grapple_Create", RunGrapple_Create),
    C("ThirdIcon_Create", RunThirdIcon_Create),
    C("CamSubject_Create", RunCamSubject_Create),
    C("ThirdCam_Create", RunThirdCam_Create),
    C("ThirdIcon_Update", RunThirdIconUpdate),
    C("CamSubject_Update", RunCamSubjectUpdate),
    C("ThirdCam_GetPos", RunThirdCamGetPos),
    C("camera_tracking", RunCameraTracking),
    C("Camera_Shear", RunCameraShear),
};

const Case kInitUpdate[] = {
    C("Player_Init", RunInit),
    C("Player_Update", RunUpdate),
};

} // namespace

void PlayerShadow_Run(void) {
    // Everything the cases change, kept to put back
    static uint8_t gameState[sizeof(GameState_t)], mpSettings[sizeof(MPSettings_t)], mpGame[sizeof(MPGameStruct)];
    static char channels[256];
    static uint8_t cheats[0xc], inputs[0x560], weapons[sizeof(weapon_data)], thirdCams[0x40], assassin[8];
    static uint32_t rand[2], frameRates[6];
    static void *viewers[11], *players[4], *blokes[4];
    static _VECTOR gravity;
    static uint8_t surfaced, scanMode, bench, blur;
    static uint32_t wireAnim, gameWireAnim;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(mpSettings, &MPSettings, sizeof(mpSettings));
    memcpy(mpGame, &MPGame, sizeof(mpGame));
    memcpy(channels, switch_channels, sizeof(channels));
    memcpy(cheats, GameCheatInfo, sizeof(cheats));
    memcpy(inputs, PlayerInputs, sizeof(inputs));
    memcpy(weapons, weapon_data, sizeof(weapons));
    memcpy(thirdCams, ThirdPersonCameras, sizeof(thirdCams));
    memcpy(assassin, GameAssassin, sizeof(assassin));
    memcpy(rand, RandWords, sizeof(rand));
    memcpy(frameRates, (void *)0x0017c0f0, sizeof(frameRates));
    memcpy(viewers, glb_viewer, sizeof(viewers));
    memcpy(players, glb_players, sizeof(players));
    memcpy(blokes, glb_blokes, sizeof(blokes));
    gravity = GRAVITY_VECTOR;
    surfaced = GameSurfaced;
    scanMode = GameScanMode;
    bench = GameBenchmark;
    blur = U8_AT(0x001f667c);
    wireAnim = (uint32_t)WireAnim;
    gameWireAnim = GameWireAnim;

    glb_viewer[0] = Viewer();
    glb_players[0] = Pl();
    glb_blokes[0] = Bl();

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&MPSettings, sizeof(MPSettings_t));
    regions.Add(&MPGame, sizeof(MPGameStruct));
    regions.Add(switch_channels, 256);
    regions.Add(GameCheatInfo, 0xc);
    regions.Add(PlayerInputs, 0x560);
    regions.Add(weapon_data, sizeof(weapon_data));
    regions.Add(ThirdPersonCameras, 0x40);
    regions.Add(GameAssassin, 8);
    regions.Add(RandWords, 8);
    regions.Add((void *)0x0017c0f0, sizeof(frameRates));
    regions.Add(glb_viewer, sizeof(viewers));
    regions.Add(glb_players, sizeof(players));
    regions.Add(glb_blokes, sizeof(blokes));
    regions.Add(&GRAVITY_VECTOR, sizeof(_VECTOR));
    regions.Add(&GameSurfaced, 1);
    regions.Add(&GameScanMode, 1);
    regions.Add(&GameBenchmark, 1);
    regions.Add((void *)0x001f667c, 1);
    ShadowFakes_AddRegions(regions);
    NfWarnMuted++;
    cases = differ = 0;
    const int trials = 3000;

    InstallFakes();
    Test("health, pain, death and air", kHealth, COUNT(kHealth), trials);
    Test("substates, walking, jumping, zero G, flying and aiming", kSubstates, COUNT(kSubstates), trials);
    Test("collision, hits and using things", kCollision, COUNT(kCollision), trials);
    Test("ladders, the grapple, wires, ziplines and creep walls", kTraverse, COUNT(kTraverse), trials);
    Test("the placements, icons and third-person cameras", kObjects, COUNT(kObjects), trials);
    Test("Player_Init and Player_Update", kInitUpdate, COUNT(kInitUpdate), trials);
    ShadowHook_RemoveAll();

    NfWarnMuted--;
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&MPSettings, mpSettings, sizeof(mpSettings));
    memcpy(&MPGame, mpGame, sizeof(mpGame));
    memcpy(switch_channels, channels, sizeof(channels));
    memcpy(GameCheatInfo, cheats, sizeof(cheats));
    memcpy(PlayerInputs, inputs, sizeof(inputs));
    memcpy(weapon_data, weapons, sizeof(weapons));
    memcpy(ThirdPersonCameras, thirdCams, sizeof(thirdCams));
    memcpy(GameAssassin, assassin, sizeof(assassin));
    memcpy(RandWords, rand, sizeof(rand));
    memcpy((void *)0x0017c0f0, frameRates, sizeof(frameRates));
    memcpy(glb_viewer, viewers, sizeof(viewers));
    memcpy(glb_players, players, sizeof(players));
    memcpy(glb_blokes, blokes, sizeof(blokes));
    GRAVITY_VECTOR = gravity;
    GameSurfaced = surfaced;
    GameScanMode = scanMode;
    GameBenchmark = bench;
    U8_AT(0x001f667c) = blur;
    WireAnim = (HASHCODE)wireAnim;
    GameWireAnim = gameWireAnim;
}
