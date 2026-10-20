// Shadow tests for parcel 14's package A (game/obj/GT.cpp, Ripples.cpp, LeafGen.cpp, Env.cpp, OneSided.cpp,
// Effect.cpp's particles, Pickup.cpp, Copter.cpp, Mine.cpp, Shooter.cpp, Apocalypse.cpp, Emitter.cpp, GunImp.cpp,
// RainBox.cpp, Cloud.cpp, Lightning.cpp, Corona.cpp, Flicker.cpp, Light.cpp, and Check_Target, Player_EquipKey and
// Intersect_ConeSphere): each function, the original and ours, on the same made-up world - objects of the kinds
// these read with their extra data, cels, models, scripts, hits, helicopter paths, an emitter's definition, the
// weather's globals - with zeros of both signs, NaNs and boundary values among the numbers. What needs the game
// running (objects made, rooms, sounds, scripts, bullets, explosions, text, the players' and bots' own code,
// collision queries, the heap) goes to a logging fake; the package's functions, ours, run for real underneath both.
// Run at start with MenuShadowTests=on (MenuProbe.cpp); NIGHTFIRE_SHADOW_ONLY=objectport runs it alone.
//
// The register-argument originals are called with their registers set (CallRegs), and so are our adaptors. A bool is
// compared on AL (tools/narrow_returns.py checks no caller reads more).

#include "ObjectPortShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/Anim.h"
#include "../engine/Collide.h"
#include "../engine/celglist.h"
#include "../engine/viewer.h"
#include "../game/view.h"
#include "../game/weapon_stats.h"
#include "../game/drone/BOT.h"
#include "../game/drone/Drone.h"
#include "../game/drone/NDrone2.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/DynamicObject.h"
#include "../game/obj/Apocalypse.h"
#include "../game/obj/Cloud.h"
#include "../game/obj/Copter.h"
#include "../game/obj/Corona.h"
#include "../game/obj/Creature.h"
#include "../game/obj/Sub.h"
#include "../game/obj/Effect.h"
#include "../game/obj/Emitter.h"
#include "../game/obj/Env.h"
#include "../game/obj/Flicker.h"
#include "../game/obj/GT.h"
#include "../game/obj/Gas.h"
#include "../game/obj/GunImp.h"
#include "../game/obj/LeafGen.h"
#include "../game/obj/Light.h"
#include "../game/obj/Lightning.h"
#include "../game/obj/Mine.h"
#include "../game/obj/OneSided.h"
#include "../game/obj/Pickup.h"
#include "../game/obj/Player.h"
#include "../game/obj/PlayerWeapon.h"
#include "../game/obj/RainBox.h"
#include "../game/obj/Ripples.h"
#include "../game/obj/Shooter.h"
#include "../game/obj/bullet.h"
#include "../game/obj/object.h"
#include "../game/sp/SwitchChannels.h"
#include "../game/Autoaim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

namespace {

#define LightList (*(DLISTINFO_tag *)0x00215700)
#define ORIGINAL(at, type) ((type)(uintptr_t)(at))
#ifndef COUNT
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#endif

const int OBJS = 12;
const int MADE = 14;
const int EXTRA = 0xa00;
const int MADE_EXTRA = 0x600;
const int HITS = 6;
const int PATHS = 4;
const int KEYS = 5;
const int HEAP = 0x1000;

struct Radiators {
    uint count;
    AMBIENT_RADIATOR lights[4];
};

struct World {
    obj_tag objs[OBJS];
    uint8_t extra[OBJS][EXTRA];
    obj_tag made[MADE];
    uint8_t madeExtra[MADE][MADE_EXTRA];
    int madeCount;
    cel_tag cels[3];
    ENV_tag envs[3];
    celglist_tag glists[4];
    SCRIPTINFO scripts[2];
    HITDATA_tag hits[HITS];
    AnimState anims[3];
    sAnimScript_tag animScript;
    BOT_vars_t bot;
    BOT_stats_t stats;
    viewer_tag viewers[2];
    COPTERPATH paths[PATHS];
    EMITTER_DEF emitterDef;
    KEYED_CLR keys[KEYS + 1];
    int texture;
    Radiators radiators;
    light_tag lights[2];
    light_tag lightPool[8];
    world_tag level;
    weapon_definition_tag weapons[2];
    uint8_t placement[0x60];
    _VECTOR v[6];
    _MATRIX mtx[2];
    char text[256];
    char doorMsg[16];
    uint8_t heap[HEAP];
    uint32_t heapUsed;
    uint8_t sounds[4];
    uint32_t answer;            // the fakes' coin
    int hitCount;               // how many hits the collision queries hand back
};
World world;

int cases, differ, nanChoices;

// The cases' random numbers: each run of a case starts from the same point, so the original and ours are given the
// same arguments, and the fakes the same answers
uint32_t gen, caseSeed;

uint32_t R(uint32_t n) {
    gen = gen * 1664525u + 1013904223u;
    uint32_t v = gen ^ (gen >> 16);
    return n ? v % n : v;
}

float RF(float lo, float hi) {
    return lo + (hi - lo) * (float)R(0x10000) / 65535.0f;
}

// Every NaN as one: which of two NaNs an operation gives is the x87's or SSE2's choice (the regions' comparison allows
// it in memory too)
uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    if ((u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) != 0)
        return 0x7fc0ffffu;
    return u;
}

// A pointer as the logs and the results show it: an offset in the world, the stack as one value, one of our ports or
// an original as the original's entry, else its address
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
    unsigned entry = XbeOriginal_EntryOf((unsigned)at);
    if (entry != 0)
        return 0x80000000u | entry;
    return 0x80000000u | (uint32_t)at;
}

void LogVector(const char *name, const _VECTOR *v) {
    if (v == NULL)
        ShadowLog("%s=null", name);
    else
        ShadowLog("%s=%x:%08x,%08x,%08x", name, W(v), Bits(v->x), Bits(v->y), Bits(v->z));
}

void LogMatrix(const char *name, const _MATRIX *m) {
    if (m == NULL) {
        ShadowLog("%s=null", name);
        return;
    }
    ShadowLog("%s=%x:", name, W(m));
    for (int i = 0; i < 15; i++)
        ShadowLog("%08x,", Bits(m->m[i]));
}

// Mostly in range, now and then an edge: zeros of both signs, a NaN, an infinity, a huge or tiny value
float Value(float lo, float hi) {
    switch (R(48)) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return NAN;
    case 3: return R(2) ? INFINITY : -INFINITY;
    case 4: return R(2) ? 1e30f : -1e-30f;
    case 5: return (float)(int)RF(lo, hi);
    default: return RF(lo, hi);
    }
}

float Plain(float lo, float hi) {
    return RF(lo, hi);
}

void RandomVector(_VECTOR *v, float range) {
    v->x = Value(-range, range);
    v->y = Value(-range, range);
    v->z = Value(-range, range);
}

void PlainVector(_VECTOR *v, float range) {
    v->x = Plain(-range, range);
    v->y = Plain(-range, range);
    v->z = Plain(-range, range);
}

void RandomMatrix(_MATRIX *m, float range) {
    _VECTOR rot;
    PlainVector(&rot, 3.2f);
    RotMatrix(&rot, m);
    m->m[3] = m->m[7] = m->m[11] = 0.0f;
    PlainVector((_VECTOR *)&m->m[12], range);
    if (R(16) == 0)
        m->m[R(15)] = Value(-2.0f, 2.0f);
}

void UnitVectorLike(_VECTOR *v) {
    PlainVector(v, 1.0f);
    Vec_Normalise(v, v);
    if (R(12) == 0)
        RandomVector(v, 1.0f);
}

obj_tag *AnyObj(void) {
    return &world.objs[R(OBJS)];
}

obj_tag *AnyObjOrNull(void) {
    return R(5) == 0 ? NULL : AnyObj();
}

// ---- the fakes

bool sureCreate, sureItem;
char labels[4][16];
int nextLabel;

obj_tag *NewObject(int size, _VECTOR *pos, _VECTOR *rot) {
    if (!sureCreate && R(24) == 0)
        return NULL;
    int i = world.madeCount < MADE - 1 ? world.madeCount++ : MADE - 1;
    obj_tag *obj = &world.made[i];
    memset(obj, 0, sizeof(obj_tag));
    memset(world.madeExtra[i], 0, sizeof(world.madeExtra[i]));
    (void)size;
    if (pos != NULL)
        obj->position = *pos;
    if (rot != NULL)
        obj->rotation = *rot;
    obj->inCel = &world.cels[R(3)];
    obj->extraObjectData = world.madeExtra[i];
    obj->radius = Plain(0.1f, 3.0f);
    obj->centrePoint = obj->position;
    return obj;
}

obj_tag *__cdecl FakeCreate(int size, _VECTOR *pos, _VECTOR *rot, quaternion_tag *quat) {
    ShadowLog("create(%d,%x)", size, W(quat));
    LogVector("pos", pos);
    LogVector("rot", rot);
    return NewObject(size, pos, rot);
}

obj_tag *__cdecl FakeCreateEx(ushort size, _VECTOR *pos, _VECTOR *rot, _MATRIX *mtx, celglist_tag *glist, obj_tag *from,
                              char a, ushort b, float c, ushort d, uchar e, uchar f, uchar g) {
    ShadowLog("createex(%d,%x,%x,%d,%d,%08x,%d,%d,%d,%d)", size, W(glist), W(from), a, b, Bits(c), d, e, f, g);
    LogVector("pos", pos);
    LogVector("rot", rot);
    LogMatrix("mtx", mtx);
    obj_tag *obj = NewObject(size, pos, rot);
    if (obj != NULL) {
        if (mtx != NULL)
            obj->transformMatrix = *mtx;
        obj->objGraphics = glist;
    }
    return obj;
}

bool __cdecl FakeLinkToRoom(obj_tag *obj, char flags, world_tag *w) {
    ShadowLog("room(%x,%d,%x)", W(obj), flags, W(w));
    return R(2) != 0;
}

void __cdecl FakeLinkToCel(obj_tag *obj, cel_tag *cel) {
    ShadowLog("cel(%x,%x)", W(obj), W(cel));
}

obj_tag *firstObject;

obj_tag *__cdecl FakeFirstObject(void) {
    return firstObject;
}

void __cdecl FakeSetGfx(obj_tag *obj, HASHCODE hash) {
    ShadowLog("gfx(%x,%x)", W(obj), hash);
}

celglist_tag *__cdecl FakeGlist(HASHCODE hash) {
    ShadowLog("glist(%x)", hash);
    return R(6) == 0 ? NULL : &world.glists[R(4)];
}

void *__cdecl FakeGetItem(HASHCODE hash) {
    ShadowLog("item(%x)", hash);
    if (!sureItem && R(8) == 0)
        return NULL;
    return R(2) ? (void *)&world.emitterDef : (void *)&world.texture;
}

DYNAMICSOUNDS *__cdecl FakeSound3D(uint sfx, _VECTOR *pos, float volume, float outer, float inner, ushort pitch,
                                   uint p7, int p8) {
    ShadowLog("sound3d(%x,%08x,%08x,%08x,%x,%x,%x)", sfx, Bits(volume), Bits(outer), Bits(inner), pitch, p7, p8);
    LogVector("at", pos);
    return R(4) == 0 ? NULL : (DYNAMICSOUNDS *)&world.sounds[R(4)];
}

void __cdecl FakeSoundStop(DYNAMICSOUNDS *handle) {
    ShadowLog("stop(%x)", W(handle));
}

void __cdecl FakeMusic(uint event, uint value) {
    ShadowLog("music(%d,%d)", event, value);
}

light_tag *__cdecl FakeLightCreate(_VECTOR *pos, uchar r, uchar g, uchar b, float brightness, ushort channel,
                                   short life, uchar type, float p9, ushort p10, ushort p11, ushort p12, int sound) {
    ShadowLog("light(%d,%d,%d,%08x,%d,%d,%d,%08x,%d,%d,%d,%d)", r, g, b, Bits(brightness), channel, life, type,
              Bits(p9), p10, p11, p12, sound);
    LogVector("at", pos);
    return &world.lights[0];
}

obj_tag *__cdecl FakeEmitter(HASHCODE hc, _VECTOR *pos, _VECTOR *dir, float life, uint on, uint off, uint alive,
                             float period) {
    ShadowLog("emitter(%x,%08x,%d,%d,%d,%08x)", hc, Bits(life), on, off, alive, Bits(period));
    LogVector("pos", pos);
    LogVector("dir", dir);
    return R(2) ? AnyObj() : NULL;
}

void __cdecl FakeExplode(obj_tag *obj, _VECTOR *pos, _VECTOR *dir, float scale, float radius, uint script,
                         float damage, uchar a, uchar b, uchar c, obj_tag *from, ushort material) {
    ShadowLog("explode(%x,%08x,%08x,%x,%08x,%d,%d,%d,%x,%d)", W(obj), Bits(scale), Bits(radius), script, Bits(damage),
              a, b, c, W(from), material);
    LogVector("pos", pos);
    LogVector("dir", dir);
}

void __cdecl FakePropagate(_VECTOR *pos, float radius, cel_tag *cel, float damage, obj_tag *from) {
    ShadowLog("propagate(%08x,%x,%08x,%x)", Bits(radius), W(cel), Bits(damage), W(from));
    LogVector("pos", pos);
}

obj_tag *__cdecl FakeBullet(short player, obj_tag *from, obj_tag *owner, weapon_definition_tag *weapon, _VECTOR *pos,
                            _VECTOR *dir, ushort sound) {
    ShadowLog("bullet(%d,%x,%x,%x,%x)", player, W(from), W(owner), W(weapon), sound);
    LogVector("pos", pos);
    LogVector("dir", dir);
    // the sub's torpedo is used unchecked
    return sureCreate ? &world.objs[OBJS - 1] : NULL;
}

SCRIPTINFO *__cdecl FakeScriptLoad(HASHCODE hash, _VECTOR *pos, _VECTOR *rot, uint *buf, void *player,
                                   ScriptPlayerCallback *callback, void *context) {
    ShadowLog("scriptload(%x,%x,%x,%x,%x)", hash, W(buf), W(player), W((const void *)callback), W(context));
    LogVector("pos", pos);
    LogVector("rot", rot);
    return R(8) == 0 ? NULL : &world.scripts[R(2)];
}

bool __cdecl FakeScriptPlay(SCRIPTINFO *script, char loop) {
    ShadowLog("play(%x,%d)", W(script), loop);
    return true;
}

void __cdecl FakeScriptUpdate(SCRIPTINFO *script) {
    ShadowLog("update(%x)", W(script));
}

obj_tag *__cdecl FakeScriptGetObj(SCRIPTINFO *script, ushort stream) {
    ShadowLog("getobj(%x,%d)", W(script), stream);
    return R(4) == 0 ? NULL : AnyObj();
}

void __cdecl FakeScriptSetPosRot(SCRIPTINFO *script, _MATRIX *mtx) {
    ShadowLog("setposrot(%x)", W(script));
    LogMatrix("m", mtx);
}

void __cdecl FakeScriptFree(SCRIPTINFO *script) {
    ShadowLog("free(%x)", W(script));
}

obj_tag *__cdecl FakeSPCreate(_VECTOR *pos, _VECTOR *rot, HASHCODE a, HASHCODE b, short mode, char once, obj_tag *owner,
                              ScriptPlayerCallback *callback, void *context, short mask) {
    ShadowLog("spcreate(%x,%x,%d,%d,%x,%x,%x,%d)", a, b, mode, once, W(owner), W((const void *)callback), W(context),
              mask);
    LogVector("pos", pos);
    LogVector("rot", rot);
    return AnyObjOrNull();
}

void __cdecl FakeSPHide(obj_tag *obj, char hide) {
    ShadowLog("sphide(%x,%d)", W(obj), hide);
}

void __cdecl FakeSPSetPosRot(obj_tag *obj, _MATRIX *mtx) {
    ShadowLog("spsetposrot(%x)", W(obj));
    LogMatrix("m", mtx);
}

void __cdecl FakeSPDelete(obj_tag *obj) {
    ShadowLog("spdelete(%x)", W(obj));
}

void __cdecl FakeSpline(void *path, ushort first, ushort last, float frame, _MATRIX *mtx, quaternion_tag *q, uint mode) {
    ShadowLog("spline(%x,%d,%d,%08x,%x,%d)", W(path), first, last, Bits(frame), W(q), mode);
    if (mtx != NULL)
        RandomMatrix(mtx, 50.0f);
}

void ChooseHits(HITDATA_tag **list) {
    int n = R(world.hitCount + 1);
    *list = n == 0 ? NULL : &world.hits[R(HITS - n + 1)];
    for (int i = 0; i < HITS; i++)
        world.hits[i].next = i + 1 < HITS ? &world.hits[i + 1] : NULL;
    if (n != 0)
        (*list)[n - 1].next = NULL;
}

bool __cdecl FakeRay(_VECTOR *from, _VECTOR *to, cel_tag *cel, obj_tag *a, obj_tag *b, HITDATA_tag **list, char only,
                     ushort mask, ushort flags) {
    ShadowLog("ray(%x,%x,%x,%x,%d,%x,%x)", W(cel), W(a), W(b), W(list), only, mask, flags);
    LogVector("from", from);
    LogVector("to", to);
    ChooseHits(list);
    return *list != NULL;
}

float __cdecl FakePlayerHurt(obj_tag *player, float damage, _VECTOR *from) {
    ShadowLog("hurt(%x,%08x,%x)", W(player), Bits(damage), W(from));
    return damage;
}

void __cdecl FakeDeleteObject(obj_tag *obj) {
    ShadowLog("delete(%x)", W(obj));
}

bool __cdecl FakeLightDelete(light_tag *light) {
    ShadowLog("lightdelete(%x)", W(light));
    return true;
}

// AnimScriptPeekSeq's: a sequence whose first root translation is fixed
uint8_t peekSeq[0x100];

void *__cdecl FakePeekSeq(HASHCODE script) {
    ShadowLog("peekseq(%x)", (uint)script);
    float y = 1.75f;
    memcpy(peekSeq + 0x1c, &y, 4);
    return peekSeq;
}

void __cdecl FakeAnimSetDeleteAll(obj_tag *obj) {
    ShadowLog("animsetdeleteall(%x)", W(obj));
}

void __cdecl FakeBoneTrans(obj_tag *obj, uint which, int datum, _VECTOR *pos, _MATRIX *mtx) {
    ShadowLog("bone(%x,%u,%d)", W(obj), which, datum);
    if (pos != NULL) {
        *pos = obj->position;
        pos->x += (float)which;
    }
    if (mtx != NULL)
        *mtx = obj->transformMatrix;
}

bool __cdecl FakeSphere(_VECTOR *pos, float radius, cel_tag *cel, obj_tag *a, obj_tag *b, HITDATA_tag **list,
                        char only, char oneSided, ushort mask, ushort flags) {
    ShadowLog("sphere(%08x,%x,%x,%x,%d,%d,%x,%x)", Bits(radius), W(cel), W(a), W(b), only, oneSided, mask, flags);
    LogVector("pos", pos);
    ChooseHits(list);
    if (R(2) && pos != NULL)
        pos->y = pos->y + Plain(-0.5f, 0.5f);
    return *list != NULL;
}

bool __cdecl FakeSight(_VECTOR *a, _VECTOR *b, cel_tag *cel, obj_tag *o1, obj_tag *o2, uint flags) {
    ShadowLog("sight(%x,%x,%x,%x)", W(cel), W(o1), W(o2), flags);
    LogVector("a", a);
    LogVector("b", b);
    return R(3) != 0;
}

void __cdecl FakeFreeHits(HITDATA_tag **list) {
    ShadowLog("freehits(%x)", W(*list));
    *list = NULL;
}

double __cdecl FakeDamage(HITDATA_tag *hits, obj_tag **list, ushort *count, ushort max) {
    ShadowLog("damage(%x,%x,%x,%d)", W(hits), W(list), W(count), max);
    return R(3) == 0 ? 0.0 : Value(0.0f, 40.0f);
}

void __cdecl FakeFilter(HITDATA_tag **list, ushort mask) {
    ShadowLog("filter(%x,%x)", W(*list), mask);
}

bool __cdecl FakePlace(_VECTOR *pos, _MATRIX *mtx, float radius, float extent, obj_tag *obj) {
    ShadowLog("place(%08x,%08x,%x)", Bits(radius), Bits(extent), W(obj));
    LogVector("pos", pos);
    LogMatrix("m", mtx);
    return R(3) != 0;
}

cel_tag *__cdecl FakeFindCel(_VECTOR *pos, world_tag *w) {
    ShadowLog("findcel(%x)", W(w));
    LogVector("pos", pos);
    return R(4) == 0 ? NULL : &world.cels[R(3)];
}

bool __cdecl FakeOnFloor(cel_tag *cel, obj_tag *obj, _VECTOR *pos, float distance, _VECTOR *dir) {
    ShadowLog("floor(%x,%x,%08x,%x)", W(cel), W(obj), Bits(distance), W(dir));
    LogVector("pos", pos);
    if (pos != NULL && R(2))
        pos->y = pos->y - Plain(0.0f, distance);
    return R(2) != 0;
}

void __cdecl FakeObj1(obj_tag *obj) {
    ShadowLog("obj1(%x)", W(obj));
}

void __cdecl FakeReferenced(obj_tag *obj, char referenced) {
    ShadowLog("referenced(%x,%d)", W(obj), referenced);
}

void __cdecl FakeInherit(obj_tag *obj, obj_tag *from, float k) {
    ShadowLog("inherit(%x,%x,%08x)", W(obj), W(from), Bits(k));
    if (R(2))
        obj->position.y = obj->position.y + 0.25f;
}

short __cdecl FakePlr2Ind(obj_tag *obj) {
    ShadowLog("plr2ind(%x)", W(obj));
    return (short)R(4);
}

int __cdecl FakeChangeSubState(obj_tag *player, ushort state) {
    ShadowLog("substate(%x,%d)", W(player), state);
    return (int)R(17);
}

void __cdecl FakeSetCamMode(BLData *blData, ushort mode) {
    ShadowLog("cammode(%x,%d)", W(blData), mode);
}

void __cdecl FakePlayerEnable(obj_tag *player, _MATRIX *mtx, int check) {
    ShadowLog("enable(%x,%x,%d)", W(player), W(mtx), check);
}

void __cdecl FakePlayerDisable(obj_tag *player, char freeze) {
    ShadowLog("disable(%x,%d)", W(player), freeze);
}

void __cdecl FakeWeaponNone(obj_tag *player) {
    ShadowLog("weaponnone(%x)", W(player));
}

bool __cdecl FakeEquipWeapon(BLData *blData, short weapon, short amount) {
    ShadowLog("equipweapon(%x,%d,%d)", W(blData), weapon, amount);
    return R(3) != 0;
}

uint __cdecl FakeEquipAmmo(BLData *blData, short weapon, short amount) {
    ShadowLog("equipammo(%x,%d,%d)", W(blData), weapon, amount);
    return R(3) == 0 ? 0 : R(0x18000);
}

void __cdecl FakeFlashBang(BLData *blData, float time, uchar blur) {
    ShadowLog("flashbang(%x,%08x,%d)", W(blData), Bits(time), blur);
}

void __cdecl FakeHeadPos(_VECTOR *out, obj_tag *player, _MATRIX *mtx) {
    ShadowLog("headpos(%x,%x)", W(player), W(mtx));
    PlainVector(out, 30.0f);
}

ushort __cdecl FakeAction(short player, int action, uint8_t flags) {
    ShadowLog("action(%d,%d,%d)", player, action, flags);
    return R(3) == 0 ? (ushort)R(3) : 0;
}

float __cdecl FakeActionf(short player, int action, uint8_t flags) {
    ShadowLog("actionf(%d,%d,%d)", player, action, flags);
    return R(3) == 0 ? 0.0f : Value(-1.0f, 1.0f);
}

void __cdecl FakeSetAction(short player, int action, uint8_t value) {
    ShadowLog("setaction(%d,%d,%d)", player, action, value);
}

void __cdecl FakeRumble(ushort player, int time, int intensity) {
    ShadowLog("rumble(%d,%d,%d)", player, time, intensity);
}

void __cdecl FakeText(char player, char p2, int kind, char *text, int sprite, short frames) {
    ShadowLog("text(%d,%d,%d,%x,%.40s,%d,%d)", player, p2, kind, W(text), text != NULL ? text : "", sprite, frames);
}

const char *__cdecl FakeBindLabel(uint label, uint p2) {
    ShadowLog("label(%x,%x)", label, p2);
    char *out = labels[nextLabel++ & 3];
    snprintf(out, 16, "L%x", label);
    return out;
}

char *__cdecl FakeStringHeap(uchar which) {
    ShadowLog("heap(%d)", which);
    return world.text;
}

uint __cdecl FakeLanguage(void) {
    return R(2);
}

char *__cdecl FakeDoorMsg(obj_tag *door) {
    ShadowLog("doormsg(%x)", W(door));
    return R(2) ? world.doorMsg : NULL;
}

ushort __cdecl FakeTriggerType(obj_tag *obj) {
    ShadowLog("triggertype(%x)", W(obj));
    return R(2) ? 10 : (ushort)R(12);
}

bool __cdecl FakeTriggerLocked(obj_tag *obj) {
    ShadowLog("locked(%x)", W(obj));
    return R(2) != 0;
}

uint __cdecl FakeTeam(obj_tag *obj) {
    ShadowLog("team(%x)", W(obj));
    return R(3);
}

bool __cdecl FakeOnTeam(obj_tag *obj, uint team) {
    ShadowLog("onteam(%x,%d)", W(obj), team);
    return R(2) != 0;
}

void __cdecl FakeMPPickup(obj_tag *obj) {
    ShadowLog("mppickup(%x)", W(obj));
}

void *__cdecl FakeGetMPPickup(PICKUPINFO *info) {
    ShadowLog("getmppickup(%x)", W(info));
    return &world.heap[0];
}

void __cdecl FakeBotAmmo(Drone_tag *drone, short weapon, short amount) {
    ShadowLog("botammo(%x,%d,%d)", W(drone), weapon, amount);
}

bool __cdecl FakeBotWeapon(Drone_tag *drone, short weapon, short amount) {
    ShadowLog("botweapon(%x,%d,%d)", W(drone), weapon, amount);
    return R(3) != 0;
}

bool __cdecl FakeBotLoaded(Drone_tag *drone, short weapon) {
    ShadowLog("botloaded(%x,%d)", W(drone), weapon);
    return R(3) != 0;
}

void __cdecl FakeBotChoice(Drone_tag *drone, short weapon, int ammo, int p4) {
    ShadowLog("botchoice(%x,%d,%d,%d)", W(drone), weapon, ammo, p4);
}

void __cdecl FakeBotVisit(Drone_tag *drone, void *pickup) {
    ShadowLog("botvisit(%x,%x)", W(drone), W(pickup));
}

BOT_stats_t *__cdecl FakeBotStats(uint id) {
    ShadowLog("botstats(%d)", id);
    return &world.stats;
}

uint __cdecl FakeUpgrade(uint weapon) {
    ShadowLog("upgrade(%d)", weapon);
    return R(4) == 0 ? R(0x60) : weapon;
}

uint __cdecl FakeUpgradeMP(short player, uint weapon) {
    ShadowLog("upgrademp(%d,%d)", player, weapon);
    return R(4) == 0 ? R(0x60) : weapon;
}

void __cdecl FakeStat(uint player) {
    ShadowLog("stat(%d)", player);
}

bool __cdecl FakeGoldMedal(void) {
    return R(2) != 0;
}

int __cdecl FakeAnimNew(obj_tag *obj, HASHCODE skin) {
    ShadowLog("animnew(%x,%x)", W(obj), skin);
    return 1;
}

sAnimScript_tag *__cdecl FakeAnimScriptAdd(obj_tag *obj, HASHCODE script) {
    ShadowLog("animscript(%x,%x)", W(obj), script);
    return &world.animScript;
}

int __cdecl FakeAnimUpdate(obj_tag *obj) {
    ShadowLog("animupdate(%x)", W(obj));
    return 0;
}

void *__cdecl FakeMalloc(size_t size, MallocFlags flags, uint32_t align) {
    ShadowLog("malloc(%d,%x,%d)", (int)size, flags, align);
    if (world.heapUsed + size > HEAP)
        return NULL;
    void *p = &world.heap[world.heapUsed];
    world.heapUsed += (uint32_t)((size + 3) & ~3u);
    memset(p, 0xcd, size);
    return p;
}

bool __cdecl FakeLListAdd(LLISTINFO_tag *list, LLNODE_tag *node) {
    ShadowLog("lladd(%x,%x)", W(list), W(node));
    return true;
}

bool __cdecl FakeLListInit(LLISTINFO_tag *list, ushort size, ushort count) {
    ShadowLog("llinit(%x,%d,%d)", W(list), size, count);
    return true;
}

LLNODE_tag *__cdecl FakeLListRemove(LLISTINFO_tag *list, LLNODE_tag *node) {
    ShadowLog("llremove(%x,%x)", W(list), W(node));
    return node;
}

void __cdecl FakeSkyObj(ushort slot, celglist_tag *glist, _VECTOR *pos, _VECTOR *rot, obj_tag *obj, char a, ushort b,
                        ushort c, char d) {
    ShadowLog("skyobj(%d,%x,%x,%x,%x,%d,%d,%d,%d)", slot, W(glist), W(pos), W(rot), W(obj), a, b, c, d);
}

void __cdecl FakeParticleList(const float *positions, int count, uint32_t colour, int texture, const uint32_t *colours,
                              char blend, float size) {
    ShadowLog("particles(%x,%d,%x,%x,%x,%d,%08x)", W(positions), count, colour, texture, W(colours), blend, Bits(size));
}

void __cdecl FakeDrawMatrix(celglist_tag *glist, _MATRIX *mtx) {
    ShadowLog("draw(%x)", W(glist));
    LogMatrix("m", mtx);
}

void __cdecl FakeTweak(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    ShadowLog("tweak(%d,%d,%d,%d)", a, r, g, b);
}

bool __cdecl FakePaused(short player) {
    return R(5) == 0;
}

void __cdecl FakeFractal(obj_tag **segments, short first, short last, _VECTOR *a, _VECTOR *b, char root) {
    ShadowLog("fractal(%x,%d,%d,%x,%x,%d)", W(segments), first, last, W(a), W(b), root);
}

void __cdecl FakeBeam(obj_tag *obj, _VECTOR *a, _VECTOR *b) {
    ShadowLog("beam(%x)", W(obj));
    LogVector("a", a);
    LogVector("b", b);
}

bool __cdecl FakeDying(obj_tag *obj) {
    ShadowLog("dying(%x)", W(obj));
    return (obj->tweakA & 1) != 0;
}

void __cdecl FakeNothing(void) {
}

void InstallFakes(bool realLights) {
    ShadowHook_Install(0x0002dc10, (const void *)&FakeCreate);
    ShadowHook_Install(0x0002e320, (const void *)&FakeCreateEx);
    ShadowHook_Install(0x00021110, (const void *)&FakeLinkToRoom);
    ShadowHook_Install(0x0002d200, (const void *)&FakeLinkToCel);
    ShadowHook_Install(0x0002cfc0, (const void *)&FakeFirstObject);
    ShadowHook_Install(0x0006b5e0, (const void *)&FakeSetGfx);
    ShadowHook_Install(0x0006b610, (const void *)&FakeGlist);
    ShadowHook_Install(0x0006b500, (const void *)&FakeGetItem);
    ShadowHook_Install(0x000cb8c0, (const void *)&FakeSound3D);
    ShadowHook_Install(0x000cb9a0, (const void *)&FakeSoundStop);
    ShadowHook_Install(0x000cbce0, (const void *)&FakeMusic);
    if (!realLights)
        ShadowHook_Install(0x0006d530, (const void *)&FakeLightCreate);
    if (!realLights)
        ShadowHook_Install(0x00068290, (const void *)&FakeEmitter);
    ShadowHook_Install(0x000690a0, (const void *)&FakeExplode);
    ShadowHook_Install(0x00069390, (const void *)&FakePropagate);
    ShadowHook_Install(0x000211f0, (const void *)&FakeBullet);
    ShadowHook_Install(0x000c1550, (const void *)&FakeScriptLoad);
    ShadowHook_Install(0x000c2660, (const void *)&FakeScriptPlay);
    ShadowHook_Install(0x000c36d0, (const void *)&FakeScriptUpdate);
    ShadowHook_Install(0x000c2700, (const void *)&FakeScriptGetObj);
    ShadowHook_Install(0x000c3dc0, (const void *)&FakeScriptSetPosRot);
    ShadowHook_Install(0x000c2500, (const void *)&FakeScriptFree);
    ShadowHook_Install(0x000c4650, (const void *)&FakeSPCreate);
    ShadowHook_Install(0x000c44b0, (const void *)&FakeSPHide);
    ShadowHook_Install(0x000c4230, (const void *)&FakeSPSetPosRot);
    ShadowHook_Install(0x000c3f40, (const void *)&FakeSPDelete);
    ShadowHook_Install(0x000d8df0, (const void *)&FakeSpline);
    ShadowHook_Install(0x0002c4e0, (const void *)&FakeRay);
    ShadowHook_Install(0x000ae690, (const void *)&FakePlayerHurt);
    ShadowHook_Install(0x0006d610, (const void *)&FakeLightDelete);
    ShadowHook_Install(0x000125b0, (const void *)&FakePeekSeq);
    ShadowHook_Install(0x00017820, (const void *)&FakeAnimSetDeleteAll);
    ShadowHook_Install(0x00013070, (const void *)&FakeBoneTrans);
    ShadowHook_Install(0x0002d080, (const void *)&FakeDeleteObject);
    ShadowHook_Install(0x0002c610, (const void *)&FakeSphere);
    ShadowHook_Install(0x0002c5b0, (const void *)&FakeSight);
    ShadowHook_Install(0x000294d0, (const void *)&FakeFreeHits);
    ShadowHook_Install(0x0002b350, (const void *)&FakeDamage);
    ShadowHook_Install(0x0002b3b0, (const void *)&FakeFilter);
    ShadowHook_Install(0x0002c8e0, (const void *)&FakePlace);
    ShadowHook_Install(0x00020c30, (const void *)&FakeFindCel);
    ShadowHook_Install(0x00020eb0, (const void *)&FakeOnFloor);
    ShadowHook_Install(0x0002db20, (const void *)&FakeObj1);
    ShadowHook_Install(0x0002db00, (const void *)&FakeReferenced);
    ShadowHook_Install(0x0002d6a0, (const void *)&FakeInherit);
    ShadowHook_Install(0x0002d8a0, (const void *)&FakePlr2Ind);
    ShadowHook_Install(0x000a9720, (const void *)&FakeChangeSubState);
    ShadowHook_Install(0x000abef0, (const void *)&FakeSetCamMode);
    ShadowHook_Install(0x000abbd0, (const void *)&FakePlayerEnable);
    ShadowHook_Install(0x000abd70, (const void *)&FakePlayerDisable);
    ShadowHook_Install(0x000b9b90, (const void *)&FakeWeaponNone);
    ShadowHook_Install(0x000ba5e0, (const void *)&FakeEquipWeapon);
    ShadowHook_Install(0x000ba790, (const void *)&FakeEquipAmmo);
    ShadowHook_Install(0x000abdc0, (const void *)&FakeFlashBang);
    ShadowHook_Install(0x000a89a0, (const void *)&FakeHeadPos);
    ShadowHook_Install(0x0006c9a0, (const void *)&FakeAction);
    ShadowHook_Install(0x0006ca20, (const void *)&FakeActionf);
    ShadowHook_Install(0x0006cb10, (const void *)&FakeSetAction);
    ShadowHook_Install(0x0006cc00, (const void *)&FakeRumble);
    ShadowHook_Install(0x000d1d70, (const void *)&FakeText);
    ShadowHook_Install(0x0006d460, (const void *)&FakeBindLabel);
    ShadowHook_Install(0x0006d140, (const void *)&FakeStringHeap);
    ShadowHook_Install(0x0006d130, (const void *)&FakeLanguage);
    ShadowHook_Install(0x0002f5b0, (const void *)&FakeDoorMsg);
    ShadowHook_Install(0x000d2ce0, (const void *)&FakeTriggerType);
    ShadowHook_Install(0x000d2d00, (const void *)&FakeTriggerLocked);
    ShadowHook_Install(0x0009df90, (const void *)&FakeTeam);
    ShadowHook_Install(0x0009dfd0, (const void *)&FakeOnTeam);
    ShadowHook_Install(0x000a1310, (const void *)&FakeMPPickup);
    ShadowHook_Install(0x0009de20, (const void *)&FakeMPPickup);
    ShadowHook_Install(0x0009e870, (const void *)&FakeGetMPPickup);
    ShadowHook_Install(0x0001e820, (const void *)&FakeBotAmmo);
    ShadowHook_Install(0x0001e6f0, (const void *)&FakeBotWeapon);
    ShadowHook_Install(0x0001f410, (const void *)&FakeBotLoaded);
    ShadowHook_Install(0x0001e470, (const void *)&FakeBotChoice);
    ShadowHook_Install(0x0001c550, (const void *)&FakeBotVisit);
    ShadowHook_Install(0x0001a250, (const void *)&FakeBotStats);
    ShadowHook_Install(0x000b9bf0, (const void *)&FakeUpgrade);
    ShadowHook_Install(0x000b9d40, (const void *)&FakeUpgradeMP);
    ShadowHook_Install(0x000b0820, (const void *)&FakeStat);
    ShadowHook_Install(0x000b06f0, (const void *)&FakeStat);
    ShadowHook_Install(0x000b07a0, (const void *)&FakeStat);
    ShadowHook_Install(0x000b0840, (const void *)&FakeGoldMedal);
    ShadowHook_Install(0x00011440, (const void *)&FakeAnimNew);
    ShadowHook_Install(0x00017220, (const void *)&FakeAnimScriptAdd);
    ShadowHook_Install(0x00018690, (const void *)&FakeAnimUpdate);
    ShadowHook_Install(0x00070ae0, (const void *)&FakeMalloc);
    if (!realLights)
        ShadowHook_Install(0x0006e4d0, (const void *)&FakeLListAdd);
    ShadowHook_Install(0x0006ea10, (const void *)&FakeLListInit);
    ShadowHook_Install(0x0006e550, (const void *)&FakeLListRemove);
    ShadowHook_Install(0x000d9c60, (const void *)&FakeSkyObj);
    ShadowHook_Install(0x000dcac0, (const void *)&FakeParticleList);
    ShadowHook_Install(0x000dde50, (const void *)&FakeDrawMatrix);
    ShadowHook_Install(0x000ddf00, (const void *)&FakeTweak);
    ShadowHook_Install(0x0006afb0, (const void *)&FakePaused);
    ShadowHook_Install(0x000b82e0, (const void *)&FakeFractal);
    ShadowHook_Install(0x000b81b0, (const void *)&FakeBeam);
    ShadowHook_Install(0x0001a190, (const void *)&FakeDying);
    ShadowHook_Install(0x000e0ec0, (const void *)&FakeNothing);
}

// ---- the world

const uchar kTypes[] = {OBJECTTYPE_DRONE, OBJECTTYPE_PLAYER, OBJECTTYPE_BULLET, OBJECTTYPE_DEAD_DRONE,
                        OBJECTTYPE_RIGIDBODY, OBJECTTYPE_DOOR, OBJECTTYPE_TRIGGER, OBJECTTYPE_GUNTURRET,
                        OBJECTTYPE_GUNTURRET2, OBJECTTYPE_COPTER, OBJECTTYPE_PICKUP, OBJECTTYPE_LIGHTNING,
                        OBJECTTYPE_EMITTER, OBJECTTYPE_GFX, 4, 9, OBJECTTYPE_CAR};

// An object's extra data, by its type, with its pointers into the world
void RandomExtra(obj_tag *obj) {
    uint8_t *extra = (uint8_t *)obj->extraObjectData;
    for (int i = 0; i < EXTRA; i += 4) {
        float f = R(6) == 0 ? Value(-100.0f, 100.0f) : Plain(-100.0f, 100.0f);
        memcpy(extra + i, &f, 4);
    }
    switch ((uchar)obj->objectType) {
    case OBJECTTYPE_PLAYER: {
        BLData *bl = (BLData *)extra;
        bl->playerNum = (char)R(4);
        bl->weaponObject = AnyObj();
        bl->remoteControlDevice = AnyObjOrNull();
        bl->health = R(4) == 0 ? -1.0f : Value(0.0f, 100.0f);
        bl->armor = Value(0.0f, 60.0f);
        bl->active = (char)R(2);
        bl->ambientBoost = (char)R(2);
        bl->previousSubState = (short)R(17);
        for (int i = 0; i < 114; i++)
            bl->weaponStats[i].enabled = (char)R(2);
        break;
    }
    case OBJECTTYPE_DRONE:
    case OBJECTTYPE_DEAD_DRONE: {
        Drone_tag *drone = (Drone_tag *)extra;
        drone->gameObj = obj;
        drone->botVars = &world.bot;
        drone->flags = R(0x800);
        drone->health = R(4) == 0 ? -1.0f : Value(-30.0f, 100.0f);
        drone->side = (uchar)R(4);
        drone->next = NULL;
        break;
    }
    case OBJECTTYPE_BULLET: {
        BU_tag *bullet = (BU_tag *)extra;
        bullet->firedByObj = AnyObjOrNull();
        bullet->wpnDef = R(3) == 0 ? &world.weapons[R(2)] : &weapon_data[R(0x60)];
        break;
    }
    case OBJECTTYPE_GUNTURRET: {
        GUNTURRET *turret = (GUNTURRET *)extra;
        turret->body = &world.scripts[0];
        turret->base = &world.scripts[1];
        turret->owner = &world.objs[0];
        turret->target = AnyObjOrNull();
        turret->muzzleFlash = AnyObjOrNull();
        turret->barrel = AnyObj();
        turret->part = AnyObjOrNull();
        turret->kind = (short)R(2);
        turret->enableChannel = R(2) ? 0 : (ushort)R(8);
        turret->disableChannel = R(2) ? 0 : (ushort)R(8);
        turret->ammo = (short)(R(4) == 0 ? R(3) : 100);
        turret->controlled = (uchar)R(2);
        turret->health = R(5) == 0 ? -1.0f : Value(0.0f, 100.0f);
        turret->timer = Value(0.0f, 3.0f);
        turret->fireTimer = Value(-0.5f, 1.0f);
        for (int i = 0; i < 15; i++)
            turret->aim.m[i] = turret->rest.m[i] = 0.0f;
        RandomMatrix(&turret->aim, 30.0f);
        RandomMatrix(&turret->rest, 30.0f);
        break;
    }
    case OBJECTTYPE_GUNTURRET2: {
        GUNIMP *gunImp = (GUNIMP *)extra;
        gunImp->gun = AnyObj();
        gunImp->user = &world.objs[0];
        // Its player
        obj->subState = 0;
        gunImp->firingSound = R(2) ? NULL : (DYNAMICSOUNDS *)&world.sounds[1];
        gunImp->gunType = (uchar)R(12);
        gunImp->overheated = (char)R(2);
        RandomMatrix(&gunImp->placement, 30.0f);
        break;
    }
    case OBJECTTYPE_COPTER: {
        COPTER *copter = (COPTER *)extra;
        copter->obj = obj;
        copter->path = R(6) == 0 ? NULL : &world.paths[R(PATHS)];
        copter->exitPath = &world.paths[R(PATHS)];
        for (int i = 0; i < 9; i++)
            copter->parts[i] = AnyObjOrNull();
        if (copter->gun != NULL && copter->flash == NULL)
            copter->flash = AnyObj();
        copter->scriptPlayer = AnyObj();
        copter->enableChannel = R(2) ? 0 : (ushort)R(8);
        copter->doneChannel = R(2) ? 0 : (ushort)R(8);
        copter->channel = R(4) == 0 ? 100 : (R(2) ? 0 : (ushort)R(8));
        copter->delay = (ushort)R(3);
        copter->paths = (ushort)R(4);
        copter->node.next = NULL;
        copter->damage = Value(-100.0f, 400.0f);
        copter->maxDamage = Value(100.0f, 300.0f);
        break;
    }
    case OBJECTTYPE_LIGHTNING: {
        LIGHTNING_tag *bolt = (LIGHTNING_tag *)extra;
        bool made = R(3) != 0;
        for (int i = 0; i < 8; i++)
            bolt->segments[i] = made ? AnyObj() : NULL;
        bolt->pair = (short)R(3);
        break;
    }
    case OBJECTTYPE_EMITTER: {
        EMITTER_tag *emitter = (EMITTER_tag *)extra;
        ushort count = world.emitterDef.count;
        emitter->vel = (EMITTER_VEC *)(emitter + 1);
        emitter->pos = emitter->vel + count;
        emitter->life = (float *)(emitter->pos + count);
        emitter->maxLife = emitter->life + count;
        emitter->colours = (EMITTER_COLOUR *)(emitter->maxLife + count);
        emitter->def = &world.emitterDef;
        for (int i = 0; i < count; i++) {
            emitter->life[i] = R(4) == 0 ? 99999.0f : Value(0.0f, 60.0f);
            emitter->maxLife[i] = Value(0.0f, 60.0f);
            memset(&emitter->colours[i], R(256), 4);
        }
        emitter->onChannel = R(2) ? 0 : R(8);
        emitter->offChannel = R(2) ? 0 : R(8);
        emitter->aliveChannel = R(2) ? 0 : R(8);
        emitter->lifeLeft = Value(-2.0f, 3.0f);
        emitter->period = R(2) ? 0.0f : Value(0.0f, 2.0f);
        emitter->timer = Value(0.0f, 2.0f);
        emitter->phaseOn = (uchar)R(2);
        emitter->finishedFrames = (uchar)(R(3) == 0 ? R(3) : 0);
        emitter->active = (uchar)R(2);
        emitter->stopping = (uchar)R(2);
        emitter->finished = (uchar)R(2);
        break;
    }
    case OBJECTTYPE_PICKUP: {
        PICKUPINFO *info = (PICKUPINFO *)extra;
        info->state = (ushort)R(3);
        info->kind = (ushort)R(8);
        info->item = (ushort)(R(4) == 0 ? 10 + 3 * R(2) : R(0x60));
        info->count = (short)R(30);
        info->channel = R(2) ? 0 : (ushort)R(8);
        info->sound = (ushort)R(0x200);
        info->respawn = R(3) == 0 ? 0xffff : (ushort)R(3);
        info->lifetime = (ushort)R(3);
        info->label = (Action_TranslatedText)(R(2) ? 0 : 0x02000123);
        info->spin = (ushort)R(2);
        break;
    }
    }
}

void RandomObject(int i) {
    obj_tag *obj = &world.objs[i];
    memset(obj, 0, sizeof(obj_tag));
    obj->objectType = i == 0 ? OBJECTTYPE_PLAYER : (char)kTypes[R(COUNT(kTypes))];
    if (i == 1 && R(2))
        obj->objectType = OBJECTTYPE_PLAYER;
    RandomVector(&obj->position, 40.0f);
    if (R(3))
        PlainVector(&obj->position, 40.0f);
    obj->lastPosition = obj->position;
    PlainVector(&obj->rotation, 3.2f);
    RandomMatrix(&obj->transformMatrix, 40.0f);
    PlainVector(&obj->centrePoint, 40.0f);
    obj->radius = Value(0.1f, 3.0f);
    obj->inCel = R(5) == 0 ? NULL : &world.cels[R(3)];
    obj->objGraphics = R(5) == 0 ? NULL : &world.glists[R(4)];
    obj->animState = &world.anims[R(3)];
    obj->extraObjectData = world.extra[i];
    obj->hitList = R(2) ? NULL : &world.hits[R(HITS)];
    obj->curState = (ushort)R(9);
    obj->subState = (ushort)R(17);
    obj->flags = (char)(R(5) == 0);
    obj->effectFlags = (int)R(0x40000) * 7;
    obj->creationTimeFrames = (int)R(5000);
    obj->scale = Plain(0.5f, 2.0f);
    obj->tweakA = (uchar)R(256);
    obj->tweakR = (uchar)R(256);
    obj->tweakG = (uchar)R(256);
    obj->tweakB = (uchar)R(256);
    obj->ambientR = (uchar)R(256);
    obj->ambientG = (uchar)R(256);
    obj->ambientB = (uchar)R(256);
    obj->nextObject = i + 1 < OBJS ? &world.objs[i + 1] : NULL;
    RandomExtra(obj);
}

// The drones on NDrone2List and the helicopters on CopterList, by their types now
void LinkLists(void) {
    Drone_tag *last = NULL;
    NPCGlobals.NDrone2List = NULL;
    COPTER *lastCopter = NULL;
    CopterList.head = NULL;
    for (int i = 0; i < OBJS; i++) {
        obj_tag *obj = &world.objs[i];
        if (obj->objectType == OBJECTTYPE_DRONE || obj->objectType == OBJECTTYPE_DEAD_DRONE) {
            Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
            drone->next = NULL;
            if (last == NULL)
                NPCGlobals.NDrone2List = drone;
            else
                last->next = drone;
            last = drone;
        }
        if (obj->objectType == OBJECTTYPE_COPTER) {
            COPTER *copter = (COPTER *)obj->extraObjectData;
            copter->node.next = NULL;
            if (lastCopter == NULL)
                CopterList.head = (LLNODE_tag *)copter;
            else
                lastCopter->node.next = (LLNODE_tag *)copter;
            lastCopter = copter;
        }
    }
}

void RandomWorld(void) {
    memset(&world, 0, sizeof(world));
    world.hitCount = (int)R(4);
    for (int i = 0; i < 3; i++) {
        cel_tag *cel = &world.cels[i];
        cel->env = R(3) == 0 ? NULL : &world.envs[i];
        cel->addedToDraw = (uchar)R(3);
        cel->bitsFromPlacementTag = R(2) ? CEL_NO_SKY : 0;
        cel->shearHeight = Value(-10.0f, 10.0f);
        cel->someSwitchChannel = (uchar)R(8);
        for (int j = 0; j < 2; j++) {
            cel->ambientR[j] = (uchar)R(256);
            cel->ambientG[j] = (uchar)R(256);
            cel->ambientB[j] = (uchar)R(256);
        }
        cel->nextCel = i + 1 < 3 ? &world.cels[i + 1] : NULL;
        PlainVector(&cel->sphere, 20.0f);
        ENV_tag *env = &world.envs[i];
        PlainVector(&env->windDir, 1.0f);
        env->windStrength = Value(0.0f, 3.0f);
        env->windMax = R(4) ? Value(0.0f, 3.0f) : 0.0f;
        env->windGust = Value(-1.5f, 1.5f);
        env->windSpread = Value(-1.0f, 1.0f);
        env->windRising = (uchar)R(2);
        env->dropType = (uchar)R(3);
    }
    for (int i = 0; i < 4; i++) {
        celglist_tag *glist = &world.glists[i];
        PlainVector(&glist->extentMin, 3.0f);
        PlainVector(&glist->extentMax, 3.0f);
        if (R(10) == 0)
            glist->extentMin.y = Value(-3.0f, 3.0f);
        glist->applyFlagsToObject = R(0x10000) * 3;
    }
    for (int i = 0; i < 2; i++) {
        SCRIPTINFO *script = &world.scripts[i];
        RandomMatrix(&script->maybeMatrix, 30.0f);
        script->waitFrames = R(2) ? 0 : (int)R(3) - 1;
        script->numElements = (ushort)R(5);
    }
    for (int i = 0; i < HITS; i++) {
        HITDATA_tag *hit = &world.hits[i];
        hit->dmgAmt = Value(-5.0f, 30.0f);
        hit->dist = Value(0.0f, 10.0f);
        PlainVector(&hit->hitPosition, 40.0f);
        PlainVector(&hit->maybehitDirection, 1.0f);
        PlainVector(&hit->surfaceNormal.normal, 1.0f);
        hit->hitObj = AnyObjOrNull();
        hit->next = i + 1 < HITS ? &world.hits[i + 1] : NULL;
    }
    for (int i = 0; i < 3; i++) {
        world.anims[i].currentWeaponId = (char)R(0x50);
        world.anims[i].switchingToWeaponId = (char)R(0x50);
    }
    world.animScript.numFrames = (short)R(200);
    world.animScript.flags = R(0) & 0xffff00ff;
    world.bot.playerIndex = (short)R(10);
    for (int i = 0; i < 114; i++)
        world.bot.weapons[i].held = (uchar)R(2);
    world.bot.armour = (uchar)R(60);
    world.bot.stats.health = (ushort)R(300);
    world.stats.isBad = (uchar)R(2);
    for (int i = 0; i < 2; i++) {
        viewer_tag *viewer = &world.viewers[i];
        viewer->enabled = (uchar)R(2);
        viewer->draw.skyVisible = (uchar)R(4) != 0;
        viewer->someCel = R(2) ? NULL : &world.cels[R(3)];
        PlainVector(&viewer->pos, 30.0f);
        viewer->nightVisionRelated = (ushort)R(3);
        RandomMatrix(&viewer->mtx, 10.0f);
    }
    for (int i = 0; i < PATHS; i++) {
        COPTERPATH *path = &world.paths[i];
        path->node.next = i + 1 < PATHS ? (LLNODE_tag *)&world.paths[i + 1] : NULL;
        path->spline = &world.heap[i * 8];
        path->speed = Value(0.0f, 2.0f);
        path->frames = (ushort)(R(5) == 0 ? 0 : R(60));
        path->group = (ushort)R(2);
        path->start = (ushort)R(30);
        path->index = (ushort)R(4);
        path->kind = (ushort)R(3);
    }
    EMITTER_DEF *def = &world.emitterDef;
    def->keys = world.keys;
    def->texture = (int)R(100);
    def->model = R(2) ? NULL : &world.glists[0];
    def->lifeBase = Value(1.0f, 40.0f);
    def->lifeRange = Value(0.0f, 20.0f);
    def->gravity = Value(-1.0f, 1.0f);
    def->speedBase = Value(0.0f, 1.0f);
    def->speedRange = Value(0.0f, 1.0f);
    def->elevationBase = Value(-3.0f, 3.0f);
    def->elevationRange = Value(0.0f, 3.0f);
    def->azimuthBase = Value(-3.0f, 3.0f);
    def->azimuthRange = Value(0.0f, 6.0f);
    def->size = Value(0.0f, 1.0f);
    def->keyCount = (int)R(KEYS) + (R(8) == 0 ? 0 : 1);
    def->count = (ushort)R(12);
    def->perFrame = (ushort)R(5);
    def->additive = (ushort)R(2);
    float t = 0.0f;
    for (int i = 0; i <= KEYS; i++) {
        world.keys[i].r = Value(0.0f, 255.0f);
        world.keys[i].g = Value(0.0f, 255.0f);
        world.keys[i].b = Value(0.0f, 255.0f);
        world.keys[i].a = Value(0.0f, 255.0f);
        t += Plain(0.0f, 0.4f);
        world.keys[i].time = R(12) == 0 ? Value(0.0f, 1.0f) : t;
    }
    world.radiators.count = R(5);
    for (int i = 0; i < 4; i++) {
        AMBIENT_RADIATOR *r = &world.radiators.lights[i];
        PlainVector(&r->pos, 30.0f);
        r->radiusSq = Value(0.0f, 100.0f);
        r->r = Value(0.0f, 1.0f);
        r->g = Value(0.0f, 1.0f);
        r->b = Value(0.0f, 1.0f);
    }
    for (int i = 0; i < 2; i++) {
        light_tag *light = &world.lights[i];
        light->enabled = (uchar)R(2);
        light->lightType = (uchar)R(4);
        light->brightness = Value(0.0f, 20.0f);
        light->maybeParentObject = AnyObj();
        PlainVector(&light->position, 30.0f);
        light->node.next = i == 0 ? (LLNODE_tag *)&world.lights[1] : NULL;
    }
    // The lights' free list
    for (int i = 0; i < 8; i++) {
        world.lightPool[i].node.prev = i > 0 ? &world.lightPool[i - 1].node : NULL;
        world.lightPool[i].node.next = i < 7 ? &world.lightPool[i + 1].node : NULL;
    }
    LightList.freeList.head = R(6) == 0 ? NULL : &world.lightPool[0].node;
    LightList.freeList.tail = LightList.freeList.head == NULL ? NULL : &world.lightPool[7].node;
    LightList.freeList.count = LightList.freeList.head == NULL ? 0 : 8;
    LightList.activeList.head = LightList.activeList.tail = NULL;
    LightList.activeList.count = 0;
    for (int i = 0; i < 0x60; i += 4) {
        uint32_t value = R(3) == 0 ? R(0) : R(12);
        memcpy(&world.placement[i], &value, 4);
    }
    for (int i = 0; i < 6; i++)
        RandomVector(&world.v[i], 30.0f);
    for (int i = 0; i < 2; i++)
        RandomMatrix(&world.mtx[i], 30.0f);
    strcpy(world.doorMsg, "door");
    world.answer = R(0);
    world.weapons[0].weaponVariantNum = 0x4a;
    world.weapons[1].weaponVariantNum = R(2) ? 0x4c : 0x10;

    for (int i = 0; i < OBJS; i++)
        RandomObject(i);
    firstObject = R(8) == 0 ? NULL : &world.objs[R(4)];

    // The game's globals the package reads
    for (int i = 0; i < 4; i++)
        glb_players[i] = i == 0 ? (R(8) ? &world.objs[0] : NULL) : (R(2) ? &world.objs[i] : NULL);
    for (int i = 0; i < 4; i++)
        glb_blokes[i] = (BLData *)world.extra[0];
    for (int i = 0; i < 11; i++)
        glb_viewer[i] = &world.viewers[i == 4];
    world.level.firstCel = &world.cels[0];
    glb_world = &world.level;
    GameState.NumFramesUnpaused = R(4) == 0 ? R(64) : R(0);
    GameState.CurrentLevelHashcode = (HASHCODE)(0x07000000 + R(0x14));
    GameState.NextLevelHashcode = (HASHCODE)(R(4) == 0 ? 0x07000043 : 0x07000001);
    GameState.LoadTimeStart = R(100000);
    GameState.difficultyModifier = R(6);
    MPSettings.isMultiplayer = R(3) == 0;
    MPSettings.maybeDroneAIEnabled = R(3) == 0;
    MPSettings.FriendlyFire = R(2);
    MPSettings.GunEmplacementsEnabled = R(2);
    MPSettings.TripleDamageModifierProfessionalMode = R(2);
    MPSettings.weaponSet = (WeaponSet)R(11);
    MPSettings.GameMode = (MultiplayerGameMode)(R(3) == 0 ? 0x20000001 : (R(2) ? 0x20000080 : 0x20000040));
    for (int i = 0; i < 10; i++) {
        MPSettings.Player[i].TeamId = (MPTeam)R(3);
        MPSettings.Player[i].SkinNum = R(29);
        MPGame.players[i].playerObj = R(3) == 0 ? NULL : AnyObj();
    }
    for (int i = 0; i < 256; i++)
        switch_channels[i] = (char)(R(3) == 0);
    ScriptCam = (HASHCODE)(R(4) == 0 ? 1 : 0);
    GlobalEnv = R(4) == 0 ? NULL : &world.envs[R(3)];

    LinkLists();
    CopterPaths.head = R(5) == 0 ? NULL : (LLNODE_tag *)&world.paths[0];
    DiffMod = Plain(1.0f, 4.0f);
}

// ---- calling

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
uint32_t F(float f) { return Bits(f); }

typedef uint32_t (*Runner)(bool original);
struct Case {
    const char *name;
    Runner run;
};
const Case *current;

// The stack below, as zeros, the same for both
__declspec(noinline) void ZeroStack(void) {
    volatile uint32_t below[0x1000];
    for (int i = 0; i < 0x1000; i++)
        below[i] = 0;
}

uint32_t RunCase(bool original) {
    gen = caseSeed;
    sureCreate = false;
    sureItem = false;
    nextLabel = 0;
    ZeroStack();
    return current->run(original);
}

uint32_t RegsRun(bool original, unsigned at, const void *ours, Regs r, int argc, const uint32_t *args) {
    if (original) {
        XbeOriginalScope o(at);
        ZeroStack();
        return CallRegs(at, &r, argc, args);
    }
    return CallRegs((unsigned)(uintptr_t)ours, &r, argc, args);
}

#define CDECL_RUN(at, ours, type, ...)                                                                                \
    (original ? ([&]() { XbeOriginalScope o(at); ZeroStack(); return ORIGINAL(at, type)(__VA_ARGS__); }()) \
              : ours(__VA_ARGS__))

#define CDECL_VOID(at, ours, type, ...)                                                                               \
    do {                                                                                                              \
        if (original) {                                                                                               \
            XbeOriginalScope o(at);                                                                                   \
            ZeroStack();                                                                                              \
            ORIGINAL(at, type)(__VA_ARGS__);                                                                          \
        } else {                                                                                                      \
            ours(__VA_ARGS__);                                                                                        \
        }                                                                                                             \
    } while (0)

// ---- the regions and the comparison (two NaNs the same: the x87 and SSE2 choose differently between two)

struct Area {
    uint8_t *at;
    size_t size;
};
Area areas[40];
int areaCount;
ShadowRegions regions;

void AddArea(void *at, size_t size) {
    regions.Add(at, size);
    areas[areaCount].at = (uint8_t *)at;
    areas[areaCount++].size = size;
}

bool IsNaNWord(const uint8_t *p) {
    uint32_t u;
    memcpy(&u, p, 4);
    return (u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) != 0;
}

bool CompareCase(const char *what, int trial) {
    static std::vector<uint8_t> theirState, ourState;
    regions.Save();
    ShadowLog_Clear();
    uint32_t theirResult = RunCase(true);
    regions.Capture(theirState);
    std::string theirLog = ShadowLog_Text();
    regions.Restore();
    ShadowLog_Clear();
    uint32_t ourResult = RunCase(false);
    regions.Capture(ourState);
    const std::string &ourLog = ShadowLog_Text();

    static std::map<std::string, int> reports;
    int &reported = reports[what];
    bool same = true;
    if (theirResult != ourResult) {
        same = false;
        if (reported++ < 3)
            printf("[objectport] %s case %d: returns 0x%x, the original 0x%x\n", what, trial, ourResult, theirResult);
    }
    size_t at = 0;
    bool nans = false, memorySame = true;
    bool identical = theirState.size() == ourState.size() &&
                     memcmp(theirState.data(), ourState.data(), theirState.size()) == 0;
    for (int a = 0; a < areaCount && memorySame && !identical; a++) {
        for (size_t j = 0; j < areas[a].size; j++) {
            if (theirState[at + j] == ourState[at + j])
                continue;
            size_t window = j >= 3 ? j - 3 : 0;
            for (; window <= j && window + 4 <= areas[a].size; window++) {
                if (IsNaNWord(&theirState[at + window]) && IsNaNWord(&ourState[at + window]))
                    break;
            }
            if (window <= j && window + 4 <= areas[a].size) {
                nans = true;
                j = window + 3;
                continue;
            }
            size_t word = j & ~(size_t)3;
            memorySame = false;
            if (reported++ < 3) {
                uint32_t theirs = 0, ours = 0;
                if (word + 4 <= areas[a].size) {
                    memcpy(&theirs, &theirState[at + word], 4);
                    memcpy(&ours, &ourState[at + word], 4);
                }
                printf("[objectport] %s case %d: memory differs in region %d at +0x%x: 0x%08x, the original 0x%08x\n",
                       what, trial, a, (unsigned)word, ours, theirs);
            }
            break;
        }
        at += areas[a].size;
    }
    same = same && memorySame;
    if (theirLog != ourLog) {
        same = false;
        if (reported++ < 3) {
            size_t d = 0;
            while (d < theirLog.size() && d < ourLog.size() && theirLog[d] == ourLog[d])
                d++;
            size_t from = d > 120 ? d - 120 : 0;
            printf("[objectport] %s case %d: calls differ at %d\n  ours:     %.400s\n  original: %.400s\n", what, trial,
                   (int)d, ourLog.c_str() + from, theirLog.c_str() + from);
        }
    }
    if (same && nans)
        nanChoices++;
    return same;
}

void Test(const char *group, const Case *list, int count, int trials) {
    int groupCases = cases, groupDiffer = differ;
    for (int c = 0; c < count; c++) {
        current = &list[c];
        for (int trial = 0; trial < trials; trial++) {
            gen = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
            RandomWorld();
            caseSeed = gen;
            cases++;
            if (!CompareCase(current->name, trial))
                differ++;
        }
    }
    printf("[objectport] %s: %d cases, %d differ\n", group, cases - groupCases, differ - groupDiffer);
}

#define C(name, run) {name, run}

// The object a case works on, made of a type
obj_tag *Subject(char type) {
    obj_tag *obj = &world.objs[2 + R(OBJS - 2)];
    obj->objectType = type;
    RandomExtra(obj);
    LinkLists();
    return obj;
}

level_tag *Placement(void) {
    return (level_tag *)world.placement;
}

// ---- scenery, hazards and weather

uint32_t RunRipplesCreate(bool original) {
    _VECTOR *pos = &world.v[0], *rot = &world.v[1];
    celglist_tag *glist = &world.glists[R(4)];
    obj_tag *obj = CDECL_RUN(0x000ce200, Ripples_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *),
                             pos, rot, Placement(), glist);
    return W(obj);
}

uint32_t RunBubbleUpdate(bool original) {
    obj_tag *obj = Subject(6);
    BUBBLE_tag *bubble = (BUBBLE_tag *)obj->extraObjectData;
    obj->inCel = R(5) == 0 ? NULL : &world.cels[R(3)];
    obj->objGraphics = &world.glists[R(4)];
    bubble->framesLeft = (short)(R(3) == 0 ? R(3) : R(100));
    if (obj->inCel != NULL && R(2))
        obj->inCel->someMax.y = obj->position.y + obj->objGraphics->extentMax.y + (R(2) ? 0.5f : -0.5f);
    CDECL_VOID(0x000cd0b0, Bubble_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunRipplesUpdate(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_RIPPLES);
    RIPPLES_tag *ripples = (RIPPLES_tag *)obj->extraObjectData;
    ripples->radius = (short)(R(4) == 0 ? R(0) : R(20));
    ripples->rate = (short)(R(4) == 0 ? R(0) : R(4));
    CDECL_VOID(0x000ce280, Ripples_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunLeafGenCreate(bool original) {
    obj_tag *obj = CDECL_RUN(0x000ce180, LeafGen_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *),
                             &world.v[0], &world.v[1], Placement(), &world.glists[R(4)]);
    return W(obj);
}

uint32_t RunLeafGenUpdate(bool original) {
    obj_tag *obj = R(8) == 0 ? NULL : Subject(OBJECTTYPE_LEAFGEN);
    CDECL_VOID(0x000ce1d0, LeafGen_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunLeafCreate(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_LEAFGEN);
    ushort count = (ushort)R(4);
    CDECL_VOID(0x000cdef0, Leaf_Create, void (__cdecl *)(obj_tag *, ushort), obj, count);
    return 0;
}

uint32_t RunLeafUpdate(bool original) {
    obj_tag *obj = R(8) == 0 ? NULL : Subject(OBJECTTYPE_LEAF);
    if (obj != NULL) {
        LEAF_tag *leaf = (LEAF_tag *)obj->extraObjectData;
        leaf->life = (short)((int)R(8) - 2);
        leaf->fade = (uchar)(R(2) ? 0 : R(256));
    }
    CDECL_VOID(0x000ce0a0, Leaf_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunEnvBreath(bool original) {
    sureCreate = true;
    cel_tag *cel = R(6) == 0 ? NULL : &world.cels[R(3)];
    int created = (int)R(200);
    CDECL_VOID(0x00068db0, Env_Breath, void (__cdecl *)(_VECTOR *, _VECTOR *, cel_tag *, int), &world.v[0], &world.v[1],
               cel, created);
    return 0;
}

uint32_t RunTreeCreate(bool original) {
    uint32_t script = R(2) ? 0x0600008c : R(0);
    memcpy(&world.placement[0x30], &script, 4);
    sureCreate = true;
    obj_tag *obj = CDECL_RUN(0x00068fc0, Env_Tree_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *),
                             &world.v[0], &world.v[1], Placement());
    return W(obj);
}

uint32_t RunTreeUpdate(bool original) {
    CDECL_VOID(0x00069090, Env_Tree_Update, void (__cdecl *)(obj_tag *), AnyObj());
    return 0;
}

uint32_t RunOneSidedCreate(bool original) {
    obj_tag *obj = CDECL_RUN(0x000db3e0, OneSided_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, celglist_tag *),
                             &world.v[0], &world.v[1], &world.glists[R(4)]);
    return W(obj);
}

uint32_t RunOneSidedUpdate(bool original) {
    CDECL_VOID(0x000db430, OneSided_Update, void (__cdecl *)(obj_tag *), AnyObj());
    return 0;
}

uint32_t RunParticlesCreate(bool original) {
    plane_equ_tag plane = {{{0.0f, -1.0f, 0.0f}}, 0.0f};
    CDECL_VOID(0x0006feb0, Particles_Create, void (__cdecl *)(int, _VECTOR *, plane_equ_tag *), (int)R(8), &world.v[0],
               &plane);
    return 0;
}

uint32_t RunParticlesUpdate(bool original) {
    obj_tag *obj = R(8) == 0 ? NULL : AnyObj();
    CDECL_VOID(0x0006ff40, Particles_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunEnvUpdate(bool original) {
    if (R(8) == 0)
        glb_world = NULL;
    DropType = (uchar)R(4) == 3 ? 0xff : (uchar)R(3);
    CDECL_VOID(0x00068bf0, Env_Update, void (__cdecl *)(void));
    return 0;
}

uint32_t RunEnvReset(bool original) {
    CDECL_VOID(0x00068f80, Env_Reset, void (__cdecl *)(void));
    return 0;
}

// Rain boxes on RainBoxRoot, and the drops' state
void RandomWeather(void) {
    obj_tag *last = NULL;
    RainBoxRoot = NULL;
    for (int i = 4; i < 4 + (int)R(4); i++) {
        obj_tag *box = &world.objs[i];
        box->objGraphics = &world.glists[R(4)];
        box->maybeParent = NULL;
        RAINBOX_tag *rain = (RAINBOX_tag *)box->extraObjectData;
        for (int k = 0; k < 3; k++) {
            rain->min[k] = Plain(-20.0f, 5.0f);
            rain->max[k] = rain->min[k] + Plain(0.0f, 30.0f);
        }
        rain->active = (uchar)R(3) != 0;
        if (last == NULL)
            RainBoxRoot = box;
        else
            last->maybeParent = box;
        last = box;
    }
    DropType = R(5) == 0 ? 0xff : (uchar)R(3);
    CurrentMax = (short)(R(3) == 0 ? R(0x1001) : R(80));
    if (R(10) == 0)
        CurrentMax = 1000 + (short)R(1100);
    Added = (short)(R(4) == 0 ? 999 : R(25));
    InitBodge = (uchar)(R(6) == 0);
    PlainVector(&CubeCentre, 20.0f);
    WindVector.x = Value(-0.012f, 0.012f);
    WindVector.y = R(3) == 0 ? 0.0f : Value(-1.0f, 0.0f);
    WindVector.z = Value(-0.012f, 0.012f);
    for (int i = 0; i < 32; i++)
        PlainVector(&RandTable1[i], 0.1f);
    int n = CurrentMax < MAX_DROPS ? CurrentMax : MAX_DROPS;
    for (int i = 0; i < n + 8 && i < MAX_DROPS; i++) {
        DropState[i] = (uchar)(R(3) != 0);
        PlainVector(&DropVector[i].pos, 25.0f);
        DropVector[i].end = Plain(-20.0f, 20.0f);
    }
}

uint32_t RunInitDrops(bool original) {
    uchar type = (uchar)(R(4) == 3 ? 0xff : R(3));
    CDECL_VOID(0x000a3180, InitDrops, void (__cdecl *)(uchar), type);
    return 0;
}

uint32_t RunAddDrop(bool original) {
    RandomWeather();
    short index = (short)R(40);
    int first = (int)R(3) * 7;
    CDECL_VOID(0x000a3350, AddDrop, void (__cdecl *)(short, int, uchar *, DROP_tag *), index, first, DropState,
               DropVector);
    return 0;
}

uint32_t RunUpdateDrops(bool original) {
    RandomWeather();
    CDECL_VOID(0x000a4300, UpdateDrops, void (__cdecl *)(void));
    return 0;
}

uint32_t RunDrawDrops(bool original) {
    RandomWeather();
    sureItem = true;
    CDECL_VOID(0x000a3560, DrawDrops, void (__cdecl *)(void));
    return 0;
}

uint32_t RunRainBoxClip(bool original) {
    RandomWeather();
    ushort active = CDECL_RUN(0x000a3ab0, RainBox_Clip, ushort (__cdecl *)(void));
    return active;
}

uint32_t RunRainBoxCreate(bool original) {
    RandomWeather();
    obj_tag *obj = CDECL_RUN(0x000a3a40, RainBox_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *),
                             &world.v[0], &world.v[1], Placement(), &world.glists[R(4)]);
    return W(obj);
}

uint32_t RunInitRainBox(bool original) {
    RandomWeather();
    CDECL_VOID(0x000a3a30, InitRainBox, void (__cdecl *)(void));
    return 0;
}

uint32_t RunCloudCreate(bool original) {
    sureCreate = true;
    obj_tag *obj = CDECL_RUN(0x000a36f0, Cloud_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *),
                             &world.v[0], &world.v[1], Placement(), &world.glists[R(4)]);
    return W(obj);
}

uint32_t RunCloudUpdate(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_CLOUD);
    CLOUD_tag *cloud = (CLOUD_tag *)obj->extraObjectData;
    cloud->wrap = 1 + (int)R(500);
    cloud->height = (int)R(100) - 50;
    CDECL_VOID(0x000a37a0, cloud_update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunLightningCreate(bool original) {
    sureCreate = true;
    obj_tag *obj = CDECL_RUN(0x000a3820, Lightning_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *),
                             &world.v[0], &world.v[1], Placement());
    return W(obj);
}

uint32_t RunLightningUpdate(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_LIGHTNING);
    LightningFlash = (uchar)R(2);
    LightningOverhead = (uchar)R(2);
    CDECL_VOID(0x000a3950, Lightning_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunCoronaCreate(bool original) {
    obj_tag *obj = CDECL_RUN(0x000a3e40, Corona_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *),
                             &world.v[0], &world.v[1], Placement(), &world.glists[R(4)]);
    return W(obj);
}

uint32_t RunCoronaUpdate(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_CORONA);
    static const uchar alphas[] = {0, 10, 16, 17, 239, 240, 250, 255};
    obj->curState = (ushort)R(2);
    obj->drawnCountdown = (char)R(3);
    obj->tweakA = alphas[R(COUNT(alphas))];
    CDECL_VOID(0x000a3eb0, Corona_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunSunreflectionUpdate(bool original) {
    obj_tag *obj = Subject(0);
    static const float scales[] = {1.0f, 1.9f, 2.0f, 2.2f, 95.0f, 100.0f, 101.0f, 0.0f};
    obj->scale = scales[R(COUNT(scales))];
    obj->curState = (ushort)R(2);
    CDECL_VOID(0x000a3f60, Sunreflection_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunDynamicObjectUpdate(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_DYNAMICOBJECT);
    DYNAMICOBJECT_tag *data = (DYNAMICOBJECT_tag *)obj->extraObjectData;
    data->showChannel = (short)(R(2) ? 0 : R(256));
    data->hideChannel = (short)(R(2) ? 0 : R(256));
    data->hideOnly = (uchar)R(2);
    data->fadeDistance = (short)(R(3) == 0 ? 0 : R(30));
    CDECL_VOID(0x000a4090, DynamicObject_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

// ---- creatures

void WaterCels(void) {
    for (int i = 0; i < 3; i++) {
        if (R(2))
            world.cels[i].bitsFromPlacementTag = (world.cels[i].bitsFromPlacementTag & 0xffff0000u) | 0xc023;
    }
}

obj_tag *RandomCreature(void) {
    obj_tag *obj = Subject(OBJECTTYPE_CREATURE);
    Creature *c = (Creature *)obj->extraObjectData;
    memset(c, 0, sizeof(*c));
    for (int i = 0; i < CREATURE_WAYPOINTS; i++) {
        PlainVector(&c->waypoints[i].pos, 20.0f);
        c->waypoints[i].quat.q[0] = 1.0f;
        c->waypoints[i].quat.q[1] = Plain(-0.3f, 0.3f);
    }
    PlainVector(&c->home, 20.0f);
    c->splineMode = R(2) ? 0 : 2;
    c->gameObj = obj;
    c->range = R(4) ? Plain(1.0f, 15.0f) : 0.0f;
    c->t = R(4) ? Plain(0.0f, 5.5f) : 5.0f;
    c->speed = Plain(0.0f, 0.2f);
    c->speedRange = Plain(0.0f, 2.0f);
    c->size = Plain(0.1f, 2.0f);
    c->kind = (ushort)R(3);
    c->state = (short)(R(6) ? R(5) : R(0x10000));
    c->seeks = (char)R(2);
    c->hurts = (char)R(2);
    // a fish's cel is read unchecked
    obj->inCel = R(5) || c->kind == CREATURE_FISH ? &world.cels[R(3)] : NULL;
    obj->hitList = NULL;
    ChooseHits(&obj->hitList);
    WaterCels();
    return obj;
}

uint32_t RunCreatureCreate(bool original) {
    CreaturePlacement *keys = (CreaturePlacement *)Placement();
    keys->kind = R(3);
    keys->range = R(4) ? R(20) : 0xfffffff0u;
    keys->speed = R(200);
    keys->seeks = R(2);
    keys->hurts = R(2);
    WaterCels();
    obj_tag *obj = CDECL_RUN(0x0002e8c0, Creature_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, celglist_tag *, level_tag *),
                             &world.v[0], &world.v[1], &world.glists[R(4)], Placement());
    return W(obj);
}

uint32_t RunCreatureWaypoints(bool original) {
    obj_tag *obj = RandomCreature();
    CDECL_VOID(0x0002e600, Creature_CalcWaypoints, void (__cdecl *)(obj_tag *, Creature *), obj,
               (Creature *)obj->extraObjectData);
    return 0;
}

uint32_t RunCreatureUpdate(bool original) {
    obj_tag *obj = R(10) ? RandomCreature() : NULL;
    CDECL_VOID(0x0002eb40, Creature_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunCreatureDelete(bool original) {
    obj_tag *obj = RandomCreature();
    CDECL_VOID(0x0002e410, Creature_Delete, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunCreatureInit(bool original) {
    CDECL_VOID(0x0002e3f0, Creature_Init, void (__cdecl *)(void));
    return 0;
}

uint32_t RunCreatureHurt(bool original) {
    obj_tag *obj = RandomCreature();
    Regs r = {0, 0, 0, 0, 0, P(obj)};
    RegsRun(original, 0x0002e440, (const void *)&Creature_HurtPlayers, r, 0, NULL);
    return 0;
}

uint32_t RunCreaturePoint(bool original) {
    obj_tag *obj = RandomCreature();
    Creature *c = (Creature *)obj->extraObjectData;
    _VECTOR *angles = &world.v[2], *dir = &world.v[3];
    PlainVector(angles, 3.2f);
    PlainVector(dir, 1.0f);
    float distance = R(3) ? Plain(0.0f, 10.0f) : 0.0f;
    uint32_t args[3] = {P(&c->waypoints[2].pos), Bits(distance), Bits(c->size)};
    Regs r = {0, P(&c->waypoints[1].pos), P(angles), 0, P(dir), P(obj)};
    RegsRun(original, 0x0002e480, (const void *)&Creature_CalcPoint, r, 3, args);
    return 0;
}

uint32_t RunCreatureSeek(bool original) {
    obj_tag *obj = RandomCreature();
    Creature *c = (Creature *)obj->extraObjectData;
    if (glb_players[0] != NULL && R(2)) {   // close by, now and then
        glb_players[0]->position = obj->position;
        glb_players[0]->position.x += Plain(-3.0f, 3.0f);
    }
    Regs r = {P(c), 0, 0, P(obj), 0, P(&world.v[3])};
    return RegsRun(original, 0x0002e580, (const void *)&Creature_SeekPlayer, r, 0, NULL) & 0xff;
}

uint32_t RunCreatureMove(bool original) {
    obj_tag *obj = RandomCreature();
    Regs r = {P(obj), 0, 0, 0, P(obj->extraObjectData), 0};
    RegsRun(original, 0x0002ea90, (const void *)&Creature_Move, r, 0, NULL);
    return 0;
}

const Case kCreatures[] = {
    C("Creature_Init", RunCreatureInit),
    C("Creature_Create", RunCreatureCreate),
    C("Creature_Delete", RunCreatureDelete),
    C("Creature_CalcWaypoints", RunCreatureWaypoints),
    C("Creature_Update", RunCreatureUpdate),
    C("Creature_HurtPlayers", RunCreatureHurt),
    C("Creature_CalcPoint", RunCreaturePoint),
    C("Creature_SeekPlayer", RunCreatureSeek),
    C("Creature_Move", RunCreatureMove),
};

// ---- the sub

obj_tag *RandomSub(void) {
    obj_tag *obj = Subject(OBJECTTYPE_SUB);
    SubInfo *sub = (SubInfo *)obj->extraObjectData;
    memset(sub, 0, sizeof(*sub));
    sub->path = &world.paths[0];
    sub->lastKey = (ushort)(R(4) ? R(KEYS) : 0);
    sub->t = R(4) ? Plain(0.0f, (float)KEYS) : (float)sub->lastKey;
    sub->speed = Plain(0.0f, 0.3f);
    sub->sinkY = R(3) ? Plain(-10.0f, 10.0f) : Mat_Position(obj->transformMatrix)->y + Plain(-0.05f, 0.05f);
    sub->health = R(4) ? Plain(-5.0f, 300.0f) : 0.0f;
    sub->height = Plain(0.5f, 3.0f);
    sub->seabedGap = R(4) ? -3.0f : Plain(-5.0f, 5.0f);
    sub->missileTime = R(3) ? Plain(0.0f, 10.2f) : 10.0f;
    sub->laserTime = R(3) ? Plain(0.0f, 2.0f) : 1.5f;
    sub->torpedoTime = R(3) ? Plain(0.0f, 5.2f) : 5.0f;
    sub->channel = (ushort)(R(2) ? 0 : R(256));
    sub->lastHitFrame = R(3) ? (int)GameState.NumFramesUnpaused : (int)R(100);
    sub->beam = R(2) ? &world.objs[1] : NULL;
    sub->light = R(4) ? &world.lights[1] : NULL;
    obj->curState = (ushort)(R(5) ? R(3) : R(10));
    if (glb_players[0] == NULL)   // read unchecked once any player is close; in a level there always is one
        glb_players[0] = &world.objs[0];
    if (R(2)) {   // close by, now and then
        glb_players[0]->position = obj->position;
        glb_players[0]->position.z += Plain(-6.0f, 6.0f);
    }
    return obj;
}

uint32_t RunSubCreate(bool original) {
    level_tag *keys = Placement();
    keys->keys[0] = R(2) ? 0 : R(256);
    keys->spline = &world.paths[0];
    keys->splinePoints = (ushort)R(KEYS + 1);
    keys->splineLength = (ushort)(R(8) ? 1 + R(400) : 0);
    sureCreate = true;
    obj_tag *obj = CDECL_RUN(0x000cef20, Sub_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *),
                             &world.v[0], &world.v[1], keys);
    sureCreate = false;
    return W(obj);
}

uint32_t RunSubUpdate(bool original) {
    obj_tag *obj = R(10) ? RandomSub() : NULL;
    if (obj != NULL && obj->curState == 0 && ((SubInfo *)obj->extraObjectData)->light == NULL)
        ((SubInfo *)obj->extraObjectData)->light = &world.lights[1];   // read unchecked afloat
    sureCreate = true;
    CDECL_VOID(0x000cf5c0, Sub_Update, void (__cdecl *)(obj_tag *), obj);
    sureCreate = false;
    return 0;
}

uint32_t RunSubDamage(bool original) {
    obj_tag *obj = RandomSub();
    float damage = R(3) ? Plain(0.0f, 100.0f) : 300.0f;
    CDECL_VOID(0x000cf6a0, Sub_ApplyDamage, void (__cdecl *)(obj_tag *, float), obj, damage);
    return 0;
}

uint32_t RunSubFollow(bool original) {
    obj_tag *obj = RandomSub();
    Regs r = {0, 0, 0, 0, P(obj->extraObjectData), P(obj)};
    RegsRun(original, 0x000cf050, (const void *)&Sub_Follow, r, 0, NULL);
    return 0;
}

uint32_t RunSubRam(bool original) {
    obj_tag *obj = RandomSub();
    uint32_t args[1] = {P(obj)};
    Regs r = {P(obj->extraObjectData), 0, 0, 0, 0, 0};
    RegsRun(original, 0x000cf0c0, (const void *)&Sub_Ram, r, 1, args);
    return 0;
}

uint32_t RunSubLaser(bool original) {
    obj_tag *obj = RandomSub();
    Regs r = {0, P(obj), 0, 0, P(obj->extraObjectData), 0};
    RegsRun(original, 0x000cf190, (const void *)&Sub_Laser, r, 0, NULL);
    return 0;
}

uint32_t RunSubMissiles(bool original) {
    obj_tag *obj = RandomSub();
    Regs r = {0, 0, 0, 0, P(obj), P(obj->extraObjectData)};
    RegsRun(original, 0x000cf2f0, (const void *)&Sub_FireMissiles, r, 0, NULL);
    return 0;
}

uint32_t RunSubTorpedoes(bool original) {
    obj_tag *obj = RandomSub();
    Regs r = {P(obj), 0, 0, 0, 0, P(obj->extraObjectData)};
    sureCreate = true;
    RegsRun(original, 0x000cf3c0, (const void *)&Sub_DeployTorpedoes, r, 0, NULL);
    sureCreate = false;
    return 0;
}

const Case kSubs[] = {
    C("Sub_Create", RunSubCreate),
    C("Sub_Update", RunSubUpdate),
    C("Sub_ApplyDamage", RunSubDamage),
    C("Sub_Follow", RunSubFollow),
    C("Sub_Ram", RunSubRam),
    C("Sub_Laser", RunSubLaser),
    C("Sub_FireMissiles", RunSubMissiles),
    C("Sub_DeployTorpedoes", RunSubTorpedoes),
};

uint32_t RunApocalypseCreate(bool original) {
    obj_tag *obj = CDECL_RUN(0x000a4180, Apocalypse_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *),
                             &world.v[0], &world.v[1], Placement(), &world.glists[R(4)]);
    return W(obj);
}

uint32_t RunApocalypseUpdate(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_APOCALYPSE);
    APOCALYPSE_tag *quake = (APOCALYPSE_tag *)obj->extraObjectData;
    quake->channel = (short)R(8);
    quake->rumble = (short)(R(2) ? 0 : R(100));
    quake->shake = (uchar)R(2);
    quake->flash = (uchar)R(2);
    quake->running = (uchar)R(2);
    quake->timeLeft = Value(-1.0f, 600.0f);
    quake->length = Value(0.0f, 600.0f);
    CDECL_VOID(0x000a4200, Apocalypse_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunMineCreate(bool original) {
    obj_tag *obj = CDECL_RUN(0x00069790, Mine_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *),
                             &world.v[0], &world.v[1], Placement(), &world.glists[R(4)]);
    return W(obj);
}

uint32_t RunMineUpdate(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_MINE);
    CDECL_VOID(0x00069870, Mine_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunShooterCreate(bool original) {
    obj_tag *obj = CDECL_RUN(0x00069910, Shooter_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *),
                             &world.v[0], &world.v[1], Placement(), &world.glists[R(4)]);
    return W(obj);
}

uint32_t RunShooterUpdate(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_SHOOTER);
    obj->curState = (ushort)R(8);
    obj->subState = (ushort)(R(4) == 0 ? 0x3b : R(0x60));
    CDECL_VOID(0x00069970, Shooter_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunFlickerCreate(bool original) {
    obj_tag *obj = CDECL_RUN(0x000cda40, Flicker_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *, celglist_tag *),
                             &world.v[0], &world.v[1], Placement(), &world.glists[R(4)]);
    return W(obj);
}

uint32_t RunFlickerUpdate(bool original) {
    obj_tag *obj = R(10) == 0 ? NULL : Subject(OBJECTTYPE_FLICKER);
    if (obj != NULL) {
        FLICKER_tag *flicker = (FLICKER_tag *)obj->extraObjectData;
        flicker->light = R(2) ? 0 : (uint)(uintptr_t)&world.lights[R(2)];
        flicker->sparks = (short)R(2);
        flicker->channel = R(2) ? 0 : R(8);
        PlainVector(&flicker->up, 1.0f);
    }
    CDECL_VOID(0x000cdae0, Flicker_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

const Case kScenery[] = {
    C("Ripples_Create", RunRipplesCreate),
    C("Ripples_Update", RunRipplesUpdate),
    C("Bubble_Update", RunBubbleUpdate),
    C("LeafGen_Create", RunLeafGenCreate),
    C("LeafGen_Update", RunLeafGenUpdate),
    C("Leaf_Create", RunLeafCreate),
    C("Leaf_Update", RunLeafUpdate),
    C("Env_Breath", RunEnvBreath),
    C("Env_Tree_Create", RunTreeCreate),
    C("Env_Tree_Update", RunTreeUpdate),
    C("OneSided_Create", RunOneSidedCreate),
    C("OneSided_Update", RunOneSidedUpdate),
    C("Particles_Create", RunParticlesCreate),
    C("Particles_Update", RunParticlesUpdate),
    C("Cloud_Create", RunCloudCreate),
    C("cloud_update", RunCloudUpdate),
    C("Lightning_Create", RunLightningCreate),
    C("Lightning_Update", RunLightningUpdate),
    C("Corona_Create", RunCoronaCreate),
    C("Corona_Update", RunCoronaUpdate),
    C("Sunreflection_Update", RunSunreflectionUpdate),
    C("DynamicObject_Update", RunDynamicObjectUpdate),
    C("Apocalypse_Create", RunApocalypseCreate),
    C("Apocalypse_Update", RunApocalypseUpdate),
    C("Mine_Create", RunMineCreate),
    C("Mine_Update", RunMineUpdate),
    C("Shooter_Create", RunShooterCreate),
    C("Shooter_Update", RunShooterUpdate),
    C("Flicker_Create", RunFlickerCreate),
    C("Flicker_Update", RunFlickerUpdate),
};

const Case kWeather[] = {
    C("Env_Update", RunEnvUpdate),
    C("Env_Reset", RunEnvReset),
    C("InitDrops", RunInitDrops),
    C("AddDrop", RunAddDrop),
    C("UpdateDrops", RunUpdateDrops),
    C("DrawDrops", RunDrawDrops),
    C("RainBox_Clip", RunRainBoxClip),
    C("RainBox_Create", RunRainBoxCreate),
    C("InitRainBox", RunInitRainBox),
};

// ---- lights and emitters

uint32_t RunClrStep(bool original) {
    float from = R(3) == 0 ? Value(0.0f, 255.0f) : (float)R(256);
    float to = R(3) == 0 ? Value(0.0f, 255.0f) : (float)R(256);
    float share = Value(-0.5f, 1.5f);
    double step = CDECL_RUN(0x0006b0a0, ClrStep, double (__cdecl *)(float, float, float), from, to, share);
    return F((float)step) ^ (uint32_t)(Bits((float)(step * 4096.0)) << 1);
}

uint32_t RunAmbient(bool original) {
    CDECL_VOID(0x0006d790, Lights_CalcAmbientLight, void (__cdecl *)(obj_tag *), AnyObj());
    return 0;
}

uint32_t RunClosest(bool original) {
    LightList.activeList.head = R(6) == 0 ? NULL : (LLNODE_tag *)&world.lights[R(2)];
    ushort count = 0;
    _VECTOR *sphere = &AnyObj()->centrePoint;
    uint mask = R(0x400);
    light_tag **list = CDECL_RUN(0x0006d8f0, Lights_CalcClosestLights, light_tag **(__cdecl *)(_VECTOR *, uint, ushort *),
                                 sphere, mask, R(8) == 0 ? NULL : &count);
    return W(list) ^ ((uint32_t)count << 24);
}

uint32_t RunRadiators(bool original) {
    CDECL_VOID(0x0006e3b0, Light_SetAmbientRadiators, void (__cdecl *)(AMBIENT_RADIATORS *),
               (AMBIENT_RADIATORS *)&world.radiators);
    return 0;
}

uint32_t RunColour(bool original) {
    KEYED_CLR out;
    float t = Value(-0.2f, 1.2f);
    int count = world.emitterDef.keyCount;
    CDECL_VOID(0x00067ea0, ColourGetValue, void (__cdecl *)(KEYED_CLR *, float, KEYED_CLR *, int), &out, t, world.keys,
               count);
    ShadowLog("clr(%08x,%08x,%08x,%08x,%08x)", F(t), F(out.r), F(out.g), F(out.b), F(out.a));
    return 0;
}

uint32_t RunEmitterCreate(bool original) {
    sureCreate = true;
    HASHCODE hc = (HASHCODE)(R(8) == 0 ? 0 : 0x0c000001);
    _VECTOR dir;
    PlainVector(&dir, 1.0f);
    Vec_Normalise(&dir, &dir);
    obj_tag *obj = CDECL_RUN(0x00068290, Emitter_Create,
                             obj_tag *(__cdecl *)(HASHCODE, _VECTOR *, _VECTOR *, float, uint, uint, uint, float), hc,
                             &world.v[0], &dir, R(3) == 0 ? 0.0f : Value(-1.0f, 100.0f), R(2) ? 0 : R(8),
                             R(2) ? 0 : R(8), R(2) ? 0 : R(8), Value(0.0f, 3.0f));
    return W(obj);
}

uint32_t RunEmitterPlist(bool original) {
    sureCreate = true;
    uint32_t life = R(4) == 0 ? R(0) | 0x80000000u : R(3);
    memcpy(&world.placement[0x30], &life, 4);
    obj_tag *obj = CDECL_RUN(0x00068470, Emitter_CreatePlist, obj_tag *(__cdecl *)(_MATRIX *, level_tag *),
                             &world.mtx[0], Placement());
    return W(obj);
}

uint32_t RunEmitterUpdate(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_EMITTER);
    CDECL_VOID(0x00068510, Emitter_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunEmitterDraw(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_EMITTER);
    CDECL_VOID(0x00068930, Emitter_Draw, void (__cdecl *)(obj_tag *, viewer_tag *), obj, &world.viewers[0]);
    return 0;
}

const Case kLights[] = {
    C("ClrStep", RunClrStep),
    C("Lights_CalcAmbientLight", RunAmbient),
    C("Lights_CalcClosestLights", RunClosest),
    C("Light_SetAmbientRadiators", RunRadiators),
    C("ColourGetValue", RunColour),
    C("Emitter_Create", RunEmitterCreate),
    C("Emitter_CreatePlist", RunEmitterPlist),
    C("Emitter_Update", RunEmitterUpdate),
    C("Emitter_Draw", RunEmitterDraw),
};

// ---- pickups, the player's aim and keys

uint32_t RunPickupCreate(bool original) {
    ushort kind = (ushort)R(13);
    ushort item = R(10) == 0 ? 0xffff : (R(4) == 0 ? 0x2c : (ushort)R(0x60));
    celglist_tag *glist = R(8) == 0 ? NULL : &world.glists[R(4)];
    _VECTOR *pos = R(3) == 0 ? NULL : &world.v[0];
    _MATRIX *mtx = pos == NULL || R(2) ? &world.mtx[0] : NULL;
    obj_tag *obj = CDECL_RUN(0x000a7380, Pickup_Create,
                             obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, _MATRIX *, celglist_tag *, ushort, ushort, uint,
                                                  uint, uint, char, ushort, ushort, uint),
                             pos, R(2) ? &world.v[1] : NULL, mtx, glist, kind, item, R(30), R(2) ? 0 : R(8),
                             R(3) == 0 ? 0 : R(0x200), (char)R(2), (ushort)R(4), (ushort)R(100), R(2) ? 0 : 0x02000444);
    return W(obj);
}

uint32_t RunPickupInherit(bool original) {
    obj_tag *pickup = Subject(OBJECTTYPE_PICKUP);
    obj_tag *from = AnyObjOrNull();
    Regs r = {P(pickup), 0, P(from), 0, 0, 0};
    return RegsRun(original, 0x000a76a0, (const void *)&Pickup_CanInheritPosition, r, 0, NULL) & 0xff;
}

uint32_t RunPickupUpdate(bool original) {
    obj_tag *obj = R(10) == 0 ? NULL : Subject(OBJECTTYPE_PICKUP);
    if (obj != NULL) {
        // Not checked
        if (obj->objGraphics == NULL)
            obj->objGraphics = &world.glists[0];
        for (int i = 0; i < 4; i++)
            if (glb_players[i] != NULL && R(2)) {
                glb_players[i]->curState = 1;
                // Near the edge of its reach
                if (R(2)) {
                    glb_players[i]->position = obj->centrePoint;
                    glb_players[i]->position.x += obj->radius + Value(0.5f, 1.7f);
                }
            }
    }
    CDECL_VOID(0x000a84d0, Pickup_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunPickupHandler(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_PICKUP);
    PICKUPINFO *info = (PICKUPINFO *)obj->extraObjectData;
    obj_tag *by = R(3) == 0 ? &world.objs[1] : &world.objs[0];
    if (by == &world.objs[1]) {
        by->objectType = R(2) ? OBJECTTYPE_DRONE : OBJECTTYPE_PLAYER;
        RandomExtra(by);
        LinkLists();
    }
    uint32_t args[2] = {P(obj), P(by)};
    Regs r = {0, 0, 0, 0, 0, P(info)};
    RegsRun(original, 0x000a7710, (const void *)&Pickup_Handler, r, 2, args);
    return 0;
}

uint32_t RunRandomSet(bool original) {
    CDECL_VOID(0x000a8030, Pickup_MakeRandomWeaponSet, void (__stdcall *)(void));
    return 0;
}

uint32_t RunFromSet(bool original) {
    ushort kind = (ushort)(7 + R(5));
    obj_tag *obj = CDECL_RUN(0x000a80b0, Pickup_CreateFromSet, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, ushort),
                             &world.v[0], &world.v[1], kind);
    return W(obj);
}

uint32_t RunCreateSimple(bool original) {
    static const short weapons[] = {1, 0x45, 0x47, 0x50, 0x51, 0x53, 0x6a, 0x6e, 0, -3, 0x72, 0x71, 0x1a, 0x1b};
    short weapon = R(2) ? weapons[R(COUNT(weapons))] : (short)R(0x72);
    short count = (short)(R(5) == 0 ? 0 : R(40));
    obj_tag *obj = CDECL_RUN(0x000a7f10, Pickup_CreateSimple, obj_tag *(__cdecl *)(_MATRIX *, short, short, ushort),
                             &world.mtx[0], weapon, count, (ushort)R(2));
    return W(obj);
}

uint32_t RunEquipKey(bool original) {
    CDECL_VOID(0x000b9b00, Player_EquipKey, void (__cdecl *)(BLData *, ushort), (BLData *)world.extra[0],
               (ushort)R(0x34));
    return 0;
}

uint32_t RunCheckTarget(bool original) {
    obj_tag *obj = R(10) == 0 ? NULL : &world.objs[0];
    if (obj != NULL) {
        // The aim's ray hits one of these
        obj_tag *hit = &world.objs[3 + R(OBJS - 3)];
        static const char kinds[] = {OBJECTTYPE_PLAYER, OBJECTTYPE_DRONE, OBJECTTYPE_DOOR, OBJECTTYPE_TRIGGER, 9};
        hit->objectType = kinds[R(COUNT(kinds))];
        RandomExtra(hit);
        LinkLists();
        for (int i = 0; i < HITS; i++)
            if (R(2))
                world.hits[i].hitObj = hit;
        if (R(4) == 0)
            GameState.CurrentLevelHashcode = (HASHCODE)(R(2) ? 0x0700004a : 0x07000009);
        U32_AT(0x00279158) = R(3) == 0 ? R(5) : 0;
        U32_AT(0x00279154) = R(5);
    }
    CDECL_VOID(0x000b9580, Check_Target, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

const Case kPickups[] = {
    C("Pickup_Create", RunPickupCreate),
    C("Pickup_CanInheritPosition", RunPickupInherit),
    C("Pickup_Update", RunPickupUpdate),
    C("Pickup_Handler", RunPickupHandler),
    C("Pickup_MakeRandomWeaponSet", RunRandomSet),
    C("Pickup_CreateFromSet", RunFromSet),
    C("Pickup_CreateSimple", RunCreateSimple),
    C("Player_EquipKey", RunEquipKey),
    C("Check_Target", RunCheckTarget),
};

// ---- helicopters

uint32_t RunConeSphere(bool original) {
    _VECTOR apex, dir;
    float sphere[4];
    PlainVector(&apex, 20.0f);
    UnitVectorLike(&dir);
    PlainVector((_VECTOR *)sphere, 20.0f);
    sphere[3] = Value(0.0f, 8.0f);
    float angle = Plain(0.0f, 1.6f);
    float c = R(8) == 0 ? Value(-1.0f, 1.0f) : cosf(angle), s = R(8) == 0 ? Value(-1.0f, 1.0f) : sinf(angle);
    bool hit = CDECL_RUN(0x0002b260, Intersect_ConeSphere, bool (__cdecl *)(_VECTOR *, _VECTOR *, float, float, _VECTOR *),
                         &apex, &dir, c, s, (_VECTOR *)sphere);
    return hit;
}

uint32_t RunCopterCreate(bool original) {
    uint32_t kind = R(3) == 0 ? 1 : 0;
    memcpy(&world.placement[0x2c], &kind, 4);
    *(ushort *)&world.placement[2] = (ushort)R(40);
    *(ushort *)&world.placement[4] = (ushort)(1 + R(400));
    obj_tag *obj = CDECL_RUN(0x0006ba90, Copter_Create, obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, level_tag *),
                             &world.v[0], &world.v[1], Placement());
    return W(obj);
}

uint32_t RunCopterInit(bool original) {
    CDECL_VOID(0x0006b810, Copter_Init, void (__cdecl *)(void));
    return 0;
}

uint32_t RunCopterRegister(bool original) {
    obj_tag *copterObj = Subject(OBJECTTYPE_COPTER);
    void *context = R(6) == 0 ? NULL : copterObj->extraObjectData;
    CDECL_VOID(0x0006b900, Copter_Register, void (__cdecl *)(obj_tag *, ushort, void *), AnyObj(), (ushort)R(11),
               context);
    return 0;
}

uint32_t RunCopterCone(bool original) {
    CDECL_VOID(0x000f5460, Copter_InitConeCos, void (__cdecl *)(void));
    CDECL_VOID(0x000f5470, Copter_InitConeSin, void (__cdecl *)(void));
    return 0;
}

uint32_t RunCopterGetBody(bool original) {
    COPTER *copter = (COPTER *)Subject(OBJECTTYPE_COPTER)->extraObjectData;
    obj_tag *body = CDECL_RUN(0x0006b7e0, Copter_GetBody, obj_tag *(__cdecl *)(COPTER *), copter);
    return W(body);
}

uint32_t RunCopterDelete(bool original) {
    CDECL_VOID(0x0006bbf0, Copter_Delete, void (__cdecl *)(obj_tag *), Subject(OBJECTTYPE_COPTER));
    return 0;
}

uint32_t RunCopterPath(bool original) {
    uint32_t length = R(3) == 0 ? 0 : (R(8) == 0 ? R(0) | 0x80000000u : 1 + R(500));
    memcpy(&world.placement[0x54], &length, 4);
    *(ushort *)&world.placement[2] = (ushort)R(40);
    *(ushort *)&world.placement[4] = (ushort)(1 + R(400));
    Regs r = {0, P(Placement()), 0, 0, 0, 0};
    return W((void *)(uintptr_t)RegsRun(original, 0x0006b930, (const void *)&Copter_CreatePath, r, 0, NULL));
}

uint32_t RunCopterNextPath(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_COPTER);
    COPTER *copter = (COPTER *)obj->extraObjectData;
    if (copter->path == NULL)
        copter->path = &world.paths[0];
    Regs r = {0, 0, 0, 0, P(copter), 0};
    return W((void *)(uintptr_t)RegsRun(original, 0x0006b9f0, (const void *)&Copter_NextPath, r, 0, NULL));
}

uint32_t RunCopterMove(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_COPTER);
    uint32_t args[2] = {P(obj), R(2)};
    Regs r = {P(obj->extraObjectData), 0, 0, 0, 0, 0};
    return RegsRun(original, 0x0006bc20, (const void *)&Copter_Move, r, 2, args) & 0xff;
}

uint32_t RunCopterFireGun(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_COPTER);
    if (glb_players[0] == NULL)
        glb_players[0] = &world.objs[0];
    COPTER *copter = (COPTER *)obj->extraObjectData;
    if (copter->gun != NULL && copter->gun->objGraphics == NULL)
        copter->gun->objGraphics = &world.glists[0];
    if (copter->gun != NULL)
        copter->gun->curState = (ushort)(R(2) ? R(8) : 5);
    uint32_t args[1] = {P(obj)};
    Regs r = {P(copter), 0, 0, 0, 0, 0};
    return RegsRun(original, 0x0006bcf0, (const void *)&Copter_FireGun, r, 1, args) & 0xff;
}

uint32_t RunCopterFireMissile(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_COPTER);
    if (glb_players[0] == NULL)
        glb_players[0] = &world.objs[0];
    COPTER *copter = (COPTER *)obj->extraObjectData;
    for (int i = 0; i < 2; i++)
        if (copter->launchers[i] != NULL && copter->launchers[i]->objGraphics == NULL)
            copter->launchers[i]->objGraphics = &world.glists[1];
    if (R(2))
        GameState.NumFramesUnpaused = 60 * R(10);
    if (R(2))
        GameState.CurrentLevelHashcode = (HASHCODE)0x07000008;
    uint32_t args[1] = {P(obj)};
    Regs r = {P(copter), 0, 0, 0, 0, 0};
    return RegsRun(original, 0x0006be70, (const void *)&Copter_FireMissile, r, 1, args) & 0xff;
}

uint32_t RunCopterSees(bool original) {
    obj_tag *obj = AnyObj();
    obj_tag *player = AnyObjOrNull();
    uint32_t args[1] = {P(obj)};
    Regs r = {0, 0, 0, 0, 0, P(player)};
    return RegsRun(original, 0x0006b830, (const void *)&Copter_SeesPlayer, r, 1, args) & 0xff;
}

uint32_t RunCopterUpdate(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_COPTER);
    obj->curState = (ushort)R(8);
    obj->subState = (ushort)(R(3) == 0 ? 400 : R(20));
    COPTER *copter = (COPTER *)obj->extraObjectData;
    if (copter->path == NULL)
        copter->path = &world.paths[1];
    if (copter->smoke != NULL)
        copter->smoke->inCel = &world.cels[0];
    if (glb_players[0] == NULL && R(2))
        glb_players[0] = &world.objs[0];
    if (copter->gun != NULL && copter->gun->objGraphics == NULL)
        copter->gun->objGraphics = &world.glists[0];
    for (int i = 0; i < 2; i++)
        if (copter->launchers[i] != NULL && copter->launchers[i]->objGraphics == NULL)
            copter->launchers[i]->objGraphics = &world.glists[1];
    if (glb_players[0] == NULL)
        glb_players[0] = &world.objs[0];
    CDECL_VOID(0x0006bfb0, Copter_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

const Case kCopters[] = {
    C("Intersect_ConeSphere", RunConeSphere),
    C("Copter_Create", RunCopterCreate),
    C("Copter_Init", RunCopterInit),
    C("Copter_Register", RunCopterRegister),
    C("Copter_InitCone", RunCopterCone),
    C("Copter_Delete", RunCopterDelete),
    C("Copter_GetBody", RunCopterGetBody),
    C("Copter_CreatePath", RunCopterPath),
    C("Copter_NextPath", RunCopterNextPath),
    C("Copter_Move", RunCopterMove),
    C("Copter_FireGun", RunCopterFireGun),
    C("Copter_FireMissile", RunCopterFireMissile),
    C("Copter_SeesPlayer", RunCopterSees),
    C("Copter_Update", RunCopterUpdate),
};

// ---- gun turrets

obj_tag *Turret(void) {
    obj_tag *obj = Subject(OBJECTTYPE_GUNTURRET);
    GUNTURRET *turret = (GUNTURRET *)obj->extraObjectData;
    turret->owner = &world.objs[R(3) == 0 ? 1 : 0];
    if (turret->owner->objectType != OBJECTTYPE_PLAYER) {
        turret->owner->objectType = OBJECTTYPE_PLAYER;
        RandomExtra(turret->owner);
    }
    LinkLists();
    return obj;
}

uint32_t RunGTCreate(bool original) {
    uint32_t keys[2] = {R(6) == 0 ? 0u : 0x06000045u, R(6) == 0 ? 0u : 0x06000044u};
    memcpy(&world.placement[0x34], keys, 8);
    uint32_t kind = R(2);
    memcpy(&world.placement[0x2c], &kind, 4);
    sureCreate = R(4) != 0;
    obj_tag *obj = CDECL_RUN(0x0009a200, GT_Create,
                             obj_tag *(__cdecl *)(_VECTOR *, _VECTOR *, quaternion_tag *, level_tag *, celglist_tag *, obj_tag *),
                             &world.v[0], &world.v[1], NULL, Placement(), &world.glists[R(4)], AnyObjOrNull());
    return W(obj);
}

uint32_t RunGTDeploy(bool original) {
    obj_tag *obj = CDECL_RUN(0x0009a9b0, GT_DeployMiniGun, obj_tag *(__cdecl *)(_VECTOR *, _MATRIX *, obj_tag *),
                             &world.v[0], &world.mtx[0], AnyObj());
    return W(obj);
}

uint32_t RunGTRegister(bool original) {
    obj_tag *obj = Turret();
    void *context = R(6) == 0 ? NULL : obj->extraObjectData;
    CDECL_VOID(0x00099d70, GT_Register, void (__cdecl *)(obj_tag *, ushort, void *), AnyObj(), (ushort)R(5), context);
    return 0;
}

uint32_t RunGTTrack(bool original) {
    obj_tag *obj = Turret();
    obj_tag *target = CDECL_RUN(0x00099da0, GT_Track, obj_tag *(__cdecl *)(GUNTURRET *, obj_tag *),
                                (GUNTURRET *)obj->extraObjectData, obj);
    return W(target);
}

uint32_t RunGTInterpolate(bool original) {
    _MATRIX towards = world.mtx[0], from = world.mtx[1];
    Regs r = {P(&towards), 0, P(&from), 0, 0, 0};
    RegsRun(original, 0x0009a530, (const void *)&GT_maybeInterpolateAimDir, r, 0, NULL);
    uint32_t sum = 0;
    for (int i = 0; i < 15; i++)
        sum = sum * 31 + Bits(towards.m[i]);
    return sum;
}

uint32_t RunGTAimAt(bool original) {
    obj_tag *obj = Turret();
    uint32_t args[1] = {P(&world.v[2])};
    Regs r = {P(obj->extraObjectData), 0, 0, 0, 0, 0};
    RegsRun(original, 0x0009a5b0, (const void *)&GT_AimAt, r, 1, args);
    return 0;
}

uint32_t RunGTRandMove(bool original) {
    obj_tag *obj = Turret();
    uint32_t args[1] = {P(obj)};
    Regs r = {0, 0, 0, 0, P(obj->extraObjectData), 0};
    RegsRun(original, 0x0009a680, (const void *)&GT_RandMove, r, 1, args);
    return 0;
}

uint32_t RunGTUpdate(bool original) {
    obj_tag *obj = Turret();
    obj->curState = (ushort)(R(5) == 0 ? R(9) : (R(2) ? 4 : 7));
    obj->subState = (ushort)R(8);
    CDECL_VOID(0x0009aa90, GT_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunGTLoseControl(bool original) {
    obj_tag *obj = Turret();
    CDECL_VOID(0x0009a830, GT_LoseControl, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunGTTakeControl(bool original) {
    obj_tag *obj = Turret();
    obj->curState = (ushort)(R(2) ? 4 : R(8));
    bool taken = CDECL_RUN(0x0009a790, GT_TakeControl, bool (__cdecl *)(obj_tag *, obj_tag *), obj, &world.objs[0]);
    return taken;
}

uint32_t RunGTDelete(bool original) {
    CDECL_VOID(0x0009a650, GT_Delete, void (__cdecl *)(obj_tag *), Turret());
    return 0;
}

uint32_t RunGTDisable(bool original) {
    obj_tag *obj = Turret();
    obj->curState = (ushort)(R(2) ? 4 : R(8));
    CDECL_VOID(0x0009a6f0, GT_Disable, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunGTActivate(bool original) {
    obj_tag *obj = Turret();
    obj->curState = (ushort)(R(8) == 0 ? 0xffff : R(9));
    CDECL_VOID(0x0009a750, GT_Activate, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

uint32_t RunGTPlayerHasDied(bool original) {
    obj_tag *obj = Turret();
    obj->curState = (ushort)(R(2) ? 7 : R(8));
    firstObject = R(6) == 0 ? NULL : &world.objs[0];
    CDECL_VOID(0x0009a960, GT_PlayerHasDied, void (__cdecl *)(obj_tag *), R(2) ? &world.objs[0] : &world.objs[1]);
    return 0;
}

const Case kTurrets[] = {
    C("GT_Create", RunGTCreate),
    C("GT_DeployMiniGun", RunGTDeploy),
    C("GT_Register", RunGTRegister),
    C("GT_Track", RunGTTrack),
    C("GT_maybeInterpolateAimDir", RunGTInterpolate),
    C("GT_AimAt", RunGTAimAt),
    C("GT_RandMove", RunGTRandMove),
    C("GT_Update", RunGTUpdate),
    C("GT_LoseControl", RunGTLoseControl),
    C("GT_TakeControl", RunGTTakeControl),
    C("GT_Delete", RunGTDelete),
    C("GT_Disable", RunGTDisable),
    C("GT_Activate", RunGTActivate),
    C("GT_PlayerHasDied", RunGTPlayerHasDied),
};

// ---- gun emplacements

uint32_t RunGunImpCreate(bool original) {
    sureCreate = true;
    quaternion_tag quat;
    for (int i = 0; i < 4; i++)
        quat.q[i] = Plain(-1.0f, 1.0f);
    obj_tag *obj = CDECL_RUN(0x00026ba0, GunImp_Create, obj_tag *(__cdecl *)(_VECTOR *, quaternion_tag *, celglist_tag *),
                             &world.v[0], &quat, &world.glists[R(4)]);
    return W(obj);
}

uint32_t RunGunImpDied(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_GUNTURRET2);
    ((GUNIMP *)obj->extraObjectData)->user = R(2) ? &world.objs[0] : &world.objs[1];
    obj->curState = (ushort)R(2);
    obj->subState = (ushort)R(4);
    glb_players[0] = &world.objs[0];
    glb_players[obj->subState] = &world.objs[0];
    firstObject = R(6) == 0 ? NULL : &world.objs[0];
    CDECL_VOID(0x00026f10, GunImp_PlayerHasDied, void (__cdecl *)(obj_tag *), &world.objs[0]);
    return 0;
}

uint32_t RunGunImpUpdate(bool original) {
    obj_tag *obj = Subject(OBJECTTYPE_GUNTURRET2);
    GUNIMP *gunImp = (GUNIMP *)obj->extraObjectData;
    gunImp->user = &world.objs[0];
    obj->curState = (ushort)(R(5) == 0 ? R(3) : 1);
    obj->subState = (ushort)R(4);
    glb_players[obj->subState] = &world.objs[0];
    gunImp->heat = Value(-5.0f, 210.0f);
    gunImp->refireTimer = R(2) ? 0.0f : Value(-1.0f, 10.0f);
    gunImp->pitchTarget = Value(-2.0f, 1.0f);
    sureCreate = true;
    CDECL_VOID(0x00027fc0, GunImp_Update, void (__cdecl *)(obj_tag *), obj);
    return 0;
}

const Case kGunImps[] = {
    C("GunImp_Create", RunGunImpCreate),
    C("GunImp_PlayerHasDied", RunGunImpDied),
    C("GunImp_Update", RunGunImpUpdate),
};

void Group(const char *name, const Case *list, int count, int trials, bool realLights = false) {
    ShadowHook_RemoveAll();
    InstallFakes(realLights);
    Test(name, list, count, trials);
}

} // namespace

void ObjectPortShadow_Run(void) {
    struct Saved {
        void *at;
        size_t size;
        std::vector<uint8_t> bytes;
    };
    static const struct {
        uint32_t at;
        size_t size;
    } kGlobals[] = {
        {0x001f6580, sizeof(GameState_t)},  // GameState
        {0x0025fe38, sizeof(MPSettings_t)}, // MPSettings
        {0x00262738, sizeof(MPGameStruct)}, // MPGame
        {0x001df138, 256},                  // switch_channels
        {0x001df428, 1024},                 // switch_channels_time
        {0x0018cdf8, 8},                    // the random words
        {0x001f6654, 16},                   // glb_players
        {0x002774b8, 16},                   // glb_blokes
        {0x001f661c, 44},                   // glb_viewer
        {0x001f6674, 8},                    // glb_world, ScriptCam
        {0x001f64ac, 4},                    // GlobalEnv
        {0x001f6668, 12},                   // the sun
        {0x0029d7a0, 0x444},                // the sky's models
        {0x0029e81a, 3},                    // the lightning's flags
        {0x002637e0, 0x274b2c - 0x2637e0},  // the drops
        {0x001fe6a8, 0x22},                 // CopterList, CopterPaths, the cone, the attack mode
        {0x0017c118, 4},                    // DiffMod
        {0x0025fc20, 0x200},                // TargetList
        {0x0017e5d8, 0xa8},                 // PickupMatrix, UseableGuns
        {0x001f6448, 0x62},                 // the emitters' statics
        {0x00215700, 0x18 + 0x78 + 0x18},   // LightList, ClosestLights
        {0x0027911c, 4},                    // FlashMod
        {0x00279154, 8},                    // the door and load trigger messages
        {0x001e5634, 4},                    // NPCGlobals.NDrone2List
    };
    std::vector<Saved> saved;
    for (const auto &g : kGlobals) {
        Saved s;
        s.at = (void *)(uintptr_t)g.at;
        s.size = g.size;
        s.bytes.assign((uint8_t *)s.at, (uint8_t *)s.at + s.size);
        saved.push_back(s);
    }

    regions.Clear();
    areaCount = 0;
    AddArea(&world, sizeof(world));
    for (const auto &g : kGlobals)
        AddArea((void *)(uintptr_t)g.at, g.size);
    NfWarnMuted++;
    cases = differ = nanChoices = 0;

    Group("scenery and hazards", kScenery, COUNT(kScenery), 2500);
    Group("creatures", kCreatures, COUNT(kCreatures), 3000);
    Group("the sub", kSubs, COUNT(kSubs), 3000);
    Group("the weather", kWeather, COUNT(kWeather), 1500);
    Group("lights and emitters", kLights, COUNT(kLights), 3000, true);
    Group("pickups and the aim", kPickups, COUNT(kPickups), 3000);
    Group("helicopters", kCopters, COUNT(kCopters), 3000);
    Group("gun turrets", kTurrets, COUNT(kTurrets), 3000);
    Group("gun emplacements", kGunImps, COUNT(kGunImps), 4000);
    ShadowHook_RemoveAll();
    if (nanChoices != 0)
        printf("[objectport] %d cases matched with NaNs the x87 and SSE2 chose differently\n", nanChoices);

    NfWarnMuted--;
    for (auto &s : saved)
        memcpy(s.at, s.bytes.data(), s.size);
}
