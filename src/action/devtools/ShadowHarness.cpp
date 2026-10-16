// Shared pieces for the game-object shadow tests: see ShadowHarness.h.

#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"

#include <windows.h>

#include <map>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// ---- regions

void ShadowRegions::Save() {
    Capture(saved);
}

void ShadowRegions::Restore() {
    size_t at = 0;
    for (const Region &r : regions) {
        memcpy(r.at, saved.data() + at, r.size);
        at += r.size;
    }
}

void ShadowRegions::Capture(std::vector<uint8_t> &out) const {
    out.clear();
    for (const Region &r : regions)
        out.insert(out.end(), r.at, r.at + r.size);
}

bool ShadowRegions::Describe(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, int *region,
                             size_t *offset) const {
    size_t at = 0;
    for (size_t i = 0; i < regions.size(); i++) {
        for (size_t j = 0; j < regions[i].size; j++) {
            if (a[at + j] != b[at + j]) {
                *region = (int)i;
                *offset = j;
                return true;
            }
        }
        at += regions[i].size;
    }
    return false;
}

// ---- hooks

namespace {

struct Hook {
    unsigned at;
    uint8_t saved[5];
};
Hook hooks[96];
int hookCount;

void HookOne(unsigned at, const void *to) {
    if (hookCount == (int)(sizeof(hooks) / sizeof(hooks[0]))) {
        printf("[shadow] too many hooks\n");
        return;
    }
    Hook &h = hooks[hookCount];
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    h.at = at;
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = (int32_t)((uint32_t)(uintptr_t)to - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    hookCount++;
}

} // namespace

void ShadowHook_Install(unsigned at, const void *fake) {
    unsigned port = XbeOriginal_Target(at);
    // A jump at the entry that the patch records cannot follow: our C++ calling the port by name would run it,
    // not the fake. Said, so a suite cannot quietly test less than it means to.
    if (port == 0 && at < 0x00400000u && *(const uint8_t *)(uintptr_t)at == 0xe9)
        printf("[shadow] 0x%08x has a jump the patch records cannot follow: calls to our port there are not faked\n",
               at);
    HookOne(at, fake);
    if (port != 0 && port != (unsigned)(uintptr_t)fake)
        HookOne(port, fake);
}

void ShadowHook_RemoveAll(void) {
    for (int i = hookCount; i-- > 0;) {
        Hook &h = hooks[i];
        DWORD old;
        VirtualProtect((void *)(uintptr_t)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)h.at, h.saved, 5);
        VirtualProtect((void *)(uintptr_t)h.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)h.at, 5);
    }
    hookCount = 0;
}

// ---- the log

namespace {
std::string logText;
}

void ShadowLog_Clear(void) {
    logText.clear();
}

void ShadowLog(const char *format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    logText += line;
    logText += ' ';
}

const std::string &ShadowLog_Text(void) {
    return logText;
}

// ---- random numbers

namespace {
uint32_t seed = 0x2b2b2b;
}

uint32_t ShadowRand(uint32_t n) {
    seed = seed * 1103515245u + 12345u;
    return n ? (seed >> 8) % n : 0;
}

float ShadowRandFloat(float lo, float hi) {
    return lo + (hi - lo) * (float)ShadowRand(0x10000) / 65535.0f;
}

// ---- comparing

namespace {
uint32_t createUsed;   // control_create_object's pool
}

bool ShadowCompare(ShadowRegions &regions, const char *tag, const char *what, int trial, uint32_t (*theirs)(void),
                   uint32_t (*ours)(void)) {
    static std::vector<uint8_t> theirState, ourState;
    createUsed = 0;   // each case's objects come from an empty pool
    regions.Save();

    ShadowLog_Clear();
    uint32_t theirResult = theirs();
    regions.Capture(theirState);
    std::string theirLog = ShadowLog_Text();

    regions.Restore();
    ShadowLog_Clear();
    uint32_t ourResult = ours();
    regions.Capture(ourState);
    const std::string &ourLog = ShadowLog_Text();

    bool same = true;
    static std::map<std::string, int> reports;
    int &reported = reports[std::string(tag) + what];
    int region;
    size_t offset;
    if (theirResult != ourResult) {
        same = false;
        if (reported++ < 3)
            printf("[%s] %s case %d: returns 0x%x, the original 0x%x\n", tag, what, trial, ourResult, theirResult);
    }
    if (regions.Describe(theirState, ourState, &region, &offset)) {
        same = false;
        if (reported++ < 3)
            printf("[%s] %s case %d: memory differs in region %d at +0x%x\n", tag, what, trial, region,
                   (unsigned)offset);
    }
    if (theirLog != ourLog) {
        same = false;
        if (reported++ < 3)
            printf("[%s] %s case %d: calls differ\n  ours:     %s\n  original: %s\n", tag, what, trial,
                   ourLog.c_str(), theirLog.c_str());
    }
    return same;
}

// ---- the fakes

ShadowFakeSettings ShadowFakes;
uint8_t ShadowCreatePool[SHADOW_CREATE_POOL];

namespace {

char labelText[64];

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

void LogVector(const char *name, const _VECTOR *v) {
    if (v == NULL)
        ShadowLog("%s=null", name);
    else
        ShadowLog("%s=%08x,%08x,%08x", name, Bits(v->x), Bits(v->y), Bits(v->z));
}

obj_tag *__cdecl FakeCreate(int size, _VECTOR *pos, _VECTOR *rot, quaternion_tag *quat) {
    ShadowLog("create(%d)", size);
    LogVector("pos", pos);
    LogVector("rot", rot);
    if (ShadowFakes.createFails)
        return NULL;
    uint32_t total = (sizeof(obj_tag) + size + 15) & ~15u;
    if (createUsed + total > SHADOW_CREATE_POOL)
        return NULL;
    obj_tag *obj = (obj_tag *)(ShadowCreatePool + createUsed);
    createUsed += total;
    memset(obj, 0, total);
    if (pos != NULL)
        obj->position = *pos;
    if (rot != NULL)
        obj->rotation = *rot;
    obj->extraObjectData = obj + 1;
    return obj;
}

bool __cdecl FakeLinkToRoom(obj_tag *obj, char flags, void *world) {
    ShadowLog("link(%p,%d,%p)", obj, flags, world);
    return true;
}

// A pointer as itself, or "stack" for one into the caller's stack, where the original's frame and ours differ
const void *Where(const void *p) {
    char here;
    ptrdiff_t distance = (const char *)p - &here;
    return distance > -0x10000 && distance < 0x10000 ? (const void *)1 : p;
}

void MadeUpMatrix(float frame, _MATRIX *mtx) {
    float angle = frame * 0.37f;
    float c = cosf(angle), s = sinf(angle);
    float m[15] = {c, 0.0f, -s, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, s, 0.0f, c, 0.0f, frame, 0.5f, frame * 2.0f};
    memcpy(mtx->m, m, sizeof(m));
}

void __cdecl FakeSplineInterp(void *path, ushort first, ushort last, float frame, _MATRIX *mtx,
                              quaternion_tag *quat, uint mode) {
    ShadowLog("spline(%p,%u,%u,%08x,%p,%p,%u)", path, first, last, Bits(frame), Where(mtx), quat, mode);
    if (mtx != NULL)
        MadeUpMatrix(frame, mtx);
}

void __cdecl FakeKeyFrameInterp(void *path, uint first, uint last, float frame, _MATRIX *mtx) {
    ShadowLog("keyframe(%p,%u,%u,%08x,%p)", path, (ushort)first, (ushort)last, Bits(frame), Where(mtx));
    if (mtx != NULL)
        MadeUpMatrix(frame, mtx);
}

void *__cdecl FakeSoundPlay3D(uint sfx, _VECTOR *pos, float volume, float outer, float inner, ushort pitch,
                              uint p7, int p8) {
    ShadowLog("sound3d(%x,%08x,%08x,%08x,%x,%x,%x)", sfx, Bits(volume), Bits(outer), Bits(inner), pitch, p7, p8);
    LogVector("at", pos);
    return NULL;
}

void *__cdecl FakeSoundPlayExt(uint sfx, float volume, ushort pitch, uint p4) {
    ShadowLog("sound(%x,%08x,%x,%x)", sfx, Bits(volume), pitch, p4);
    return NULL;
}

void __cdecl FakeTextAddMsg(char player, char p2, int kind, char *text, int sprite, short frames) {
    ShadowLog("text(%d,%d,%d,%p,%d,%d)", player, p2, kind, text, sprite, frames);
}

const char *__cdecl FakeTxtBindLabel(uint label, uint p2) {
    ShadowLog("label(%x,%x)", label, p2);
    snprintf(labelText, sizeof(labelText), "label %x", label);
    return labelText;
}

void __cdecl FakePushState(int state, float time, uint colour) {
    ShadowLog("pushstate(%d,%08x,%x)", state, Bits(time), colour);
}

void __cdecl FakeMusicEvent(uint event, uint value) {
    ShadowLog("music(%u,%u)", event, value);
}

void __cdecl FakePauseGame(uint8_t pause) {
    ShadowLog("pause(%u)", pause);
}

void __cdecl FakeLevelToLoad(uint level, uint8_t warm, uint8_t skipFmv) {
    ShadowLog("leveltoload(%x,%u,%u)", level, warm, skipFmv);
}

void __cdecl FakeRamSave(void *bloke) {
    ShadowLog("ramsave(%p)", bloke);
}

bool __cdecl FakeLListAdd(void *list, void *node) {
    ShadowLog("llistadd(%p,%p)", list, node);
    return true;
}

void __cdecl FakeBuildWorldSph(obj_tag *obj) {
    ShadowLog("worldsph(%p)", obj);
}

bool __cdecl FakeStraddleTest(obj_tag *obj) {
    ShadowLog("straddle(%p)", obj);
    return false;
}

bool __cdecl FakeNextLOD(obj_tag *obj) {
    ShadowLog("nextlod(%p)", obj);
    return true;
}

void __cdecl FakeExplodeCreate(obj_tag *obj, _VECTOR *pos, _VECTOR *up, float scale, float range, uint script,
                               float strength, uint8_t h, uint8_t i, uint8_t j, obj_tag *shooter, ushort material) {
    ShadowLog("explode(%p,%p,%08x,%08x,%x,%08x,%u,%u,%u,%p,%u)", obj, up, Bits(scale), Bits(range), script,
              Bits(strength), h, i, j, shooter, material);
    LogVector("at", pos);
}

void __cdecl FakeSetUpdator(ushort num, obj_tag *obj, void *func) {
    ShadowLog("setupdator(%u,%p,%p)", num, obj, func);
}

void __cdecl FakePlayerEnable(obj_tag *player, _MATRIX *mtx, int checkAttach) {
    ShadowLog("playerenable(%p,%p,%d)", player, mtx, checkAttach);
}

void __cdecl FakePlayerDisable(obj_tag *player, char freeze) {
    ShadowLog("playerdisable(%p,%d)", player, freeze);
}

void __cdecl FakeHUDEnable(void *hud, int pane, char enable, ushort state) {
    ShadowLog("hud(%p,%d,%d,%u)", hud, pane, enable, state);
}

void __cdecl FakeSetAction(short player, int action, uint8_t value) {
    ShadowLog("setaction(%d,%d,%u)", player, action, value);
}

ushort __cdecl FakeAction(short player, int action, uint8_t flags) {
    ShadowLog("action(%d,%d,%u)", player, action, flags);
    return ShadowFakes.inputAction;
}

float __cdecl FakeGetDamage(void *hits, obj_tag **list, ushort *count, ushort max) {
    ShadowLog("damage(%p,%p,%p,%u)", hits, list, count, max);
    return ShadowFakes.damage;
}

bool __cdecl FakeDroneDying(obj_tag *drone) {
    ShadowLog("dying(%p)", drone);
    return (drone->tweakA & 1) != 0;
}

obj_tag *__cdecl FakeFirstObject(void) {
    return ShadowFakes.firstObject;
}

unsigned long long __cdecl FakeTime(void) {
    return ShadowFakes.time;
}

uint32_t __cdecl FakeMissionStatus(void) {
    return ShadowFakes.missionStatus;
}

} // namespace

void ShadowFakes_Install(void) {
    ShadowHook_Install(0x0002dc10, (const void *)&FakeCreate);
    ShadowHook_Install(0x00021110, (const void *)&FakeLinkToRoom);
    ShadowHook_Install(0x000d8df0, (const void *)&FakeSplineInterp);
    ShadowHook_Install(0x000d8ac0, (const void *)&FakeKeyFrameInterp);
    ShadowHook_Install(0x000cb8c0, (const void *)&FakeSoundPlay3D);
    ShadowHook_Install(0x000cb940, (const void *)&FakeSoundPlayExt);
    ShadowHook_Install(0x000d1d70, (const void *)&FakeTextAddMsg);
    ShadowHook_Install(0x0006d460, (const void *)&FakeTxtBindLabel);
    ShadowHook_Install(0x0006abf0, (const void *)&FakePushState);
    ShadowHook_Install(0x000cbce0, (const void *)&FakeMusicEvent);
    ShadowHook_Install(0x0006af70, (const void *)&FakePauseGame);
    ShadowHook_Install(0x000bddf0, (const void *)&FakeLevelToLoad);
    ShadowHook_Install(0x000abdf0, (const void *)&FakeRamSave);
    ShadowHook_Install(0x0006e4d0, (const void *)&FakeLListAdd);
    ShadowHook_Install(0x0002db20, (const void *)&FakeBuildWorldSph);
    ShadowHook_Install(0x0002d2a0, (const void *)&FakeStraddleTest);
    ShadowHook_Install(0x0002da50, (const void *)&FakeNextLOD);
    ShadowHook_Install(0x000690a0, (const void *)&FakeExplodeCreate);
    ShadowHook_Install(0x00025160, (const void *)&FakeSetUpdator);
    ShadowHook_Install(0x000abbd0, (const void *)&FakePlayerEnable);
    ShadowHook_Install(0x000abd70, (const void *)&FakePlayerDisable);
    ShadowHook_Install(0x000b1a60, (const void *)&FakeHUDEnable);
    ShadowHook_Install(0x0006cb10, (const void *)&FakeSetAction);
    ShadowHook_Install(0x0006c9a0, (const void *)&FakeAction);
    ShadowHook_Install(0x0002b350, (const void *)&FakeGetDamage);
    ShadowHook_Install(0x0001a190, (const void *)&FakeDroneDying);
    ShadowHook_Install(0x0002cfc0, (const void *)&FakeFirstObject);
    ShadowHook_Install(0x000dffb0, (const void *)&FakeTime);
    ShadowHook_Install(0x0009be00, (const void *)&FakeMissionStatus);
}

void ShadowFakes_AddRegions(ShadowRegions &regions) {
    regions.Add(ShadowCreatePool, sizeof(ShadowCreatePool));
    regions.Add(&createUsed, sizeof(createUsed));
}
