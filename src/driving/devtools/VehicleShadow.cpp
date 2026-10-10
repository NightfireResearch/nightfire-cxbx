#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "VehicleShadow.h"
#include "FpControl.h"

#include "../../common/xbeOriginal.h"
#include "../../helpers.h"
#include "../data/AttributeSystem.h"
#include "../engine/ActionQueue.hpp"
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../game/Helicopter.h"
#include "../game/Vehicle.h"
#include "../physics/SimpleRigidBody.h"
#include "../physics/Simulation.h"
#include "../render/RSceneObj.hpp"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_VEHICLESHADOW=1: the ports of PVehicle, its car name map, PhysicsData's constructor and PHelicopter
// against the originals (their entry bytes swapped back in for each original call, common/xbeOriginal.h), on
// identical inputs, compared.
//
//   - The car attributes of every car name: GetNamedAttribs, CarPhysicsAttrib, RenderNameAttrib,
//     NumColoursAttrib (the answers; the first side to look a structure or a file name up makes it, the second
//     answers the same). InitCarPhysics on random bytes. MissionEditorSwitch on random masks, both ways.
//   - The car names: GetNameCount, GetCarNames, NameToIndex of every name and its upper and lower case forms.
//     BuildCarNames and Shutdown, each side building its own list and map (the live ones put back after): the
//     count, the names, the map's every node (key text, index, colour, shape).
//   - CarNameMap on maps of our own: random insert, erase and erase-range sequences of the car names and names of
//     our own, then Destroy; the answers and the map after every step.
//   - PhysicsData's constructor on the first "smackable" names.
//   - PHelicopter on copies - of each live helicopter, and of one built on the player's car's render object and a
//     free simple body slot - with copies of its render object (a vtable of our own recording TriggerFX and
//     UpdatePosition), sound, AI and an action queue of our own: GetDamageZones; GetControllerInput on random
//     actions; Simulate with random controls, eased values, body orientation and velocity, both blink parities,
//     the sleep step passed or not, and the controls read from the queue; ApplyDamage on segments through and past
//     the body's box with random amounts, splits, kinds, hit points, scale, zones and sources. Recording fakes
//     stand in front of the animation stimuli, the mission manager, the AI's DisableTargetBeacon and
//     ForceVehicleToSleep. Compared: the copies' bytes, the live body, the random generator, the answer and the
//     calls each side made.
// Everything written is put back: the live body slot, the random generator, the step count, the car name globals.
//
// One mutation this catches: ApplyDamage spreading a fifth of the damage as a quarter (kSpreadScale 0.25) changes
// the neighbouring zones of every kind 3 hit; Simulate easing roll by 0.5 rather than 0.05 changes the body's
// orientation in every flying case.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kRanges[2][2] = {
    { 0x0006d8f0, 0x0006ed40 },     // PHelicopter, PhysicsData's constructor
    { 0x0006f810, 0x00071930 },     // PVehicle, the car name map
};

struct OriginalWindow {
    OriginalWindow() {
        for (const uint32_t *range : kRanges)
            XbeOriginal_RestoreRange(range[0], range[1], true);
    }
    ~OriginalWindow() {
        for (const uint32_t *range : kRanges)
            XbeOriginal_RestoreRange(range[0], range[1], false);
    }
};

// ---- the originals

typedef AttributeSet *(*GetNamedAttribsFn)(AttributeSet *, const char *);
typedef CarPhysics *(*CarPhysicsAttribFn)(AttributeSet *);
typedef const char *(*RenderNameAttribFn)(AttributeSet *);
typedef uint32_t (*NumColoursAttribFn)(AttributeSet *);
typedef void (*InitCarPhysicsFn)(const char *, const char *, const char *, uint32_t, void *);
typedef void (__fastcall *MissionEditorSwitchFn)(PVehicle *, int, bool, int);
typedef void (*VoidFn)();
typedef uint32_t (*GetNameCountFn)();
typedef int (*NameToIndexFn)(const char *);
typedef const char *const *(*GetCarNamesFn)();
typedef TreeInsertResult *(__fastcall *InsertUniqueFn)(CarNameMap *, int, TreeInsertResult *, const TreePair *);
typedef TreeNode **(__fastcall *EraseAtFn)(CarNameMap *, int, TreeNode **, TreeNode *);
typedef TreeNode **(__fastcall *EraseRangeFn)(CarNameMap *, int, TreeNode **, TreeNode *, TreeNode *);
typedef void (__fastcall *MapVoidFn)(CarNameMap *, int);
typedef PhysicsData *(__fastcall *PhysicsDataConstructFn)(PhysicsData *, int, const char *);
typedef void (__fastcall *HeliVoidFn)(PHelicopter *, int);
typedef DamageZone *(__fastcall *HeliZonesFn)(PHelicopter *, int, uint32_t *);
typedef int (__fastcall *HeliDamageFn)(PHelicopter *, int, const Coord3 *, const Coord3 *, float, float, int,
                                       const uint32_t *);

#define Orig_GetNamedAttribs ((GetNamedAttribsFn)0x0006f810)
#define Orig_CarPhysicsAttrib ((CarPhysicsAttribFn)0x0006f840)
#define Orig_RenderNameAttrib ((RenderNameAttribFn)0x0006f860)
#define Orig_NumColoursAttrib ((NumColoursAttribFn)0x0006f8c0)
#define Orig_InitCarPhysics ((InitCarPhysicsFn)0x0006f8e0)
#define Orig_MissionEditorSwitch ((MissionEditorSwitchFn)0x00071000)
#define Orig_BuildCarNames ((VoidFn)0x00071730)
#define Orig_Shutdown ((VoidFn)0x00071900)
#define Orig_GetNameCount ((GetNameCountFn)0x00071880)
#define Orig_NameToIndex ((NameToIndexFn)0x000718a0)
#define Orig_GetCarNames ((GetCarNamesFn)0x000718e0)
#define Orig_InsertUnique ((InsertUniqueFn)0x00071560)
#define Orig_EraseAt ((EraseAtFn)0x00071290)
#define Orig_EraseRange ((EraseRangeFn)0x00071630)
#define Orig_Destroy ((MapVoidFn)0x000716f0)
#define Orig_PhysicsDataConstruct ((PhysicsDataConstructFn)0x0006ec80)
#define Orig_HeliGetControllerInput ((HeliVoidFn)0x0006dc30)
#define Orig_HeliGetDamageZones ((HeliZonesFn)0x0006dd10)
#define Orig_HeliSimulate ((HeliVoidFn)0x0006dd30)
#define Orig_HeliApplyDamage ((HeliDamageFn)0x0006e330)

// ---- the game's state the tests read, write and restore

#define ShadowSim ((void *)0x00233ff0)
#define ShadowRandom (*(SimRandom **)0x00233ff0)
#define ShadowStepCount I32_AT(0x00234e34)
#define ShadowMissilesFirst (*(PhysicsObject ***)0x00234e80)
#define ShadowMissilesLast (*(PhysicsObject ***)0x00234e84)
#define ShadowHelicoptersFirst (*(PhysicsObject ***)0x00234ee0)
#define ShadowHelicoptersLast (*(PhysicsObject ***)0x00234ee4)
#define ShadowSimpleOwners ((PhysicsObject **)0x002343a0)
const int kSimpleBodies = 96;
const uint32_t kRenderBytes = 0x370;    // RVehicle
const uint32_t kSoundBytes = 0x310;     // AHelicopter
const uint32_t kAIBytes = 0x140;        // AIHelicopter

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[vehicle]   %s #%d: %s\n", what, index, detail);
}

void CheckBytes(const char *what, int index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    char detail[96];
    snprintf(detail, sizeof(detail), "byte %u of %u: original %02x, port %02x", unsigned(at), unsigned(bytes), x[at],
             y[at]);
    Differ(what, index, detail);
}

template <class T> void Check(const char *what, int index, const T &a, const T &b) {
    CheckBytes(what, index, &a, &b, sizeof(T));
}

void CheckString(const char *what, int index, const char *a, const char *b) {
    g_checks++;
    if (a == b || (a != NULL && b != NULL && strcmp(a, b) == 0))
        return;
    char detail[96];
    snprintf(detail, sizeof(detail), "original \"%.30s\", port \"%.30s\"", a ? a : "(null)", b ? b : "(null)");
    Differ(what, index, detail);
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context, bool original);

bool Guarded(CaseFn run, void *context, bool original) {
#ifdef _MSC_VER
    __try {
        run(context, original);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        return false;
    }
#else
    run(context, original);
    return true;
#endif
}

// ---- random inputs

uint32_t g_seed = 0x5eed1234;

uint32_t RandomWord() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

int RandomInt(int n) { return int(RandomWord() % uint32_t(n)); }

float RandomFloat(float lo, float hi) { return lo + (hi - lo) * float(RandomWord() & 0xffff) / 65535.0f; }

// ---- the calls the code under test makes into code it must not reach for real: recorded

struct Call {
    uint32_t function, a, b, c, d, e;
};

std::vector<Call> g_calls[2];
int g_side = 0;

void Record(uint32_t function, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0, uint32_t e = 0) {
    Call call = { function, a, b, c, d, e };
    g_calls[g_side].push_back(call);
}

uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }

uint32_t FloatBits(float f) {
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return bits;
}

// The byte and word arguments are compared at their width: the callers leave the rest of the register as it was
void __fastcall FakeProcessStimuli(Handle *handle, int, uint32_t stimulus, uint32_t tick, int mode) {
    Record(0x00077e00, Word(handle), stimulus & 0xff, tick, uint32_t(mode));
}
void __fastcall FakeProcessStimuliZones(Handle *handle, int, uint32_t stimulus, uint32_t zones, uint32_t tick,
                                        int mode) {
    Record(0x00077e60, Word(handle), stimulus & 0xff, zones & 0xffff, tick, uint32_t(mode));
}
void __fastcall FakeIncShotsHit(void *manager, int, uint32_t hit) { Record(0x000b6720, Word(manager), hit & 0xff); }
void __fastcall FakeIncKills(void *manager, int, int kills) { Record(0x000b6770, Word(manager), uint32_t(kills)); }
void __fastcall FakeProgrammerDefinedEvent(void *manager, int, int event, const char *text) {
    Record(0x000b72f0, Word(manager), uint32_t(event), uint32_t(strlen(text)), uint8_t(text[0]));
}
void __fastcall FakeDisableTargetBeacon(void *ai, int) { Record(0x000318d0, Word(ai)); }
void __fastcall FakeForceVehicleToSleep(void *controller, int, void *vehicle) {
    Record(0x000289f0, Word(controller), Word(vehicle));
}
void __fastcall FakeUpdatePosition(void *object, int, int flag) { Record(0x10e, Word(object), uint32_t(flag)); }
void __fastcall FakeTriggerFX(void *object, int, int type, uint32_t which, uint32_t unused3, uint32_t unused4,
                              uint32_t unused5, float intensity) {
    Record(0x10f, Word(object), uint32_t(type), which, unused3, unused4 ^ unused5 ^ FloatBits(intensity));
}

void *g_fakeRenderVtable[32];

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[24];
int g_hookCount = 0;

void HookOne(uint32_t at, const void *to) {
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    DWORD old;
    h.on = VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old) != 0;
    if (!h.on)
        return;
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t rel = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
}

// The original's entry, and the port its jump leads to (ported callers call the port directly)
void HookInstall(uint32_t at, const void *to) {
    const uint8_t *entry = (const uint8_t *)(uintptr_t)at;
    uint32_t port = 0;
    if (entry[0] == 0xe9) {
        int32_t rel;
        memcpy(&rel, entry + 1, 4);
        port = at + 5 + uint32_t(rel);
    }
    HookOne(at, to);
    if (port != 0 && port != (uint32_t)(uintptr_t)to)
        HookOne(port, to);
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
        Hook &h = g_hooks[i];
        if (!h.on)
            continue;
        DWORD old;
        VirtualProtect((void *)(uintptr_t)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)h.at, h.saved, 5);
        VirtualProtect((void *)(uintptr_t)h.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)h.at, 5);
    }
    g_hookCount = 0;
}

struct Fakes {
    Fakes() {
        HookInstall(0x00077e00, (const void *)&FakeProcessStimuli);
        HookInstall(0x00077e60, (const void *)&FakeProcessStimuliZones);
        HookInstall(0x000b6720, (const void *)&FakeIncShotsHit);
        HookInstall(0x000b6770, (const void *)&FakeIncKills);
        HookInstall(0x000b72f0, (const void *)&FakeProgrammerDefinedEvent);
        HookInstall(0x000318d0, (const void *)&FakeDisableTargetBeacon);
        HookInstall(0x000289f0, (const void *)&FakeForceVehicleToSleep);
    }
    ~Fakes() { HooksRemove(); }
};

// The original (inside the window), then the port, each with its own call record; false if either faulted
template <class F>
bool Both(F &&run) {
    typedef typename std::remove_reference<F>::type Run;
    CaseFn thunk = [](void *context, bool original) { (*static_cast<Run *>(context))(original); };
    g_calls[0].clear();
    g_calls[1].clear();
    bool ok;
    {
        OriginalWindow window;
        g_side = 0;
        ok = Guarded(thunk, &run, true);
    }
    g_side = 1;
    ok = Guarded(thunk, &run, false) && ok;
    g_side = 0;
    return ok;
}

void CheckCalls(const char *what, int index) {
    g_checks++;
    if (g_calls[0].size() != g_calls[1].size()) {
        char detail[64];
        snprintf(detail, sizeof(detail), "original made %u calls, port %u", unsigned(g_calls[0].size()),
                 unsigned(g_calls[1].size()));
        Differ(what, index, detail);
        return;
    }
    for (size_t i = 0; i < g_calls[0].size(); i++) {
        if (memcmp(&g_calls[0][i], &g_calls[1][i], sizeof(Call)) != 0) {
            const Call &a = g_calls[0][i], &b = g_calls[1][i];
            char detail[128];
            snprintf(detail, sizeof(detail), "call %u: original %x(%x, %x, %x), port %x(%x, %x, %x)", unsigned(i),
                     a.function, a.b, a.c, a.d, b.function, b.b, b.c, b.d);
            Differ(what, index, detail);
            return;
        }
    }
}

// ---- maps compared by content: every node's key text, index and colour, in pre-order with the nil leaves

uint32_t TextHash(const char *text) {
    uint32_t hash = 2166136261u;
    for (; *text != 0; text++)
        hash = (hash ^ uint8_t(*text)) * 16777619u;
    return hash;
}

int NodeIndex(TreeNode *node) { return node->value.index; }

void Serialize(TreeNode *node, std::vector<uint32_t> *out, int depth) {
    if (node->isNil || depth > 64) {
        out->push_back(0xffffffffu);
        return;
    }
    out->push_back(TextHash(node->value.name));
    out->push_back(uint32_t(NodeIndex(node)));
    out->push_back(node->color);
    Serialize(node->left, out, depth + 1);
    Serialize(node->right, out, depth + 1);
}

std::vector<uint32_t> MapContents(CarNameMap *map) {
    std::vector<uint32_t> out;
    out.push_back(map->size);
    if (map->head == NULL)
        return out;
    Serialize(map->head->parent, &out, 0);
    out.push_back(map->head->left->isNil ? 0 : TextHash(map->head->left->value.name));
    out.push_back(map->head->right->isNil ? 0 : TextHash(map->head->right->value.name));
    return out;
}

void CheckMaps(const char *what, int index, CarNameMap *a, CarNameMap *b) {
    std::vector<uint32_t> x = MapContents(a), y = MapContents(b);
    g_checks++;
    if (x == y)
        return;
    char detail[64];
    snprintf(detail, sizeof(detail), "sizes %u and %u, %u and %u words", a->size, b->size, unsigned(x.size()),
             unsigned(y.size()));
    Differ(what, index, detail);
}

CarNameMap *NewMap() {
    CarNameMap *map = static_cast<CarNameMap *>(OperatorNew(sizeof(CarNameMap)));
    map->allocator = 0;
    map->Init();
    return map;
}

// The n-th node in order (the head past the end)
TreeNode *NthNode(CarNameMap *map, int n) {
    TreeNode *node = map->head->left;
    while (n-- > 0 && node != map->head)
        node = TreeNext(node);
    return node;
}

int NodePosition(CarNameMap *map, TreeNode *node) {
    int n = 0;
    for (TreeNode *at = map->head->left; at != map->head && at != node; at = TreeNext(at))
        n++;
    return n;
}

// ---- PVehicle's statics

void TestAttributes() {
    uint32_t count = PVehicle::GetNameCount();
    const char *const *names = PVehicle::GetCarNames();
    for (uint32_t i = 0; i < count; i++) {
        g_cases++;
        AttributeSet sets[2] = {};
        AttributeSet *answers[2] = {};
        CarPhysics *physics[2] = {};
        const char *files[2] = {};
        uint32_t colours[2] = {};
        Both([&](bool original) {
            int side = original ? 0 : 1;
            if (original) {
                answers[side] = Orig_GetNamedAttribs(&sets[side], names[i]);
                physics[side] = Orig_CarPhysicsAttrib(&sets[side]);
                files[side] = Orig_RenderNameAttrib(&sets[side]);
                colours[side] = Orig_NumColoursAttrib(&sets[side]);
            } else {
                answers[side] = PVehicle::GetNamedAttribs(&sets[side], names[i]);
                physics[side] = PVehicle::CarPhysicsAttrib(&sets[side]);
                files[side] = PVehicle::RenderNameAttrib(&sets[side]);
                colours[side] = PVehicle::NumColoursAttrib(&sets[side]);
            }
        });
        Check("GetNamedAttribs", int(i), sets[0].collection, sets[1].collection);
        Check("GetNamedAttribs answer", int(i), answers[0] == &sets[0], answers[1] == &sets[1]);
        Check("CarPhysicsAttrib", int(i), physics[0], physics[1]);
        CheckString("RenderNameAttrib", int(i), files[0], files[1]);
        Check("RenderNameAttrib pointer", int(i), files[0], files[1]);
        Check("NumColoursAttrib", int(i), colours[0], colours[1]);
        sets[0].Destruct();
        sets[1].Destruct();
    }

    for (int i = 0; i < 64; i++) {
        g_cases++;
        uint8_t blocks[2][sizeof(CarPhysics)];
        for (uint8_t &byte : blocks[0])
            byte = uint8_t(RandomWord());
        memcpy(blocks[1], blocks[0], sizeof(CarPhysics));
        Both([&](bool original) {
            if (original)
                Orig_InitCarPhysics("pvehicle", "x", "CarPhysics", 0, blocks[0]);
            else
                InitCarPhysics("pvehicle", "x", "CarPhysics", 0, blocks[1]);
        });
        Check("InitCarPhysics", i, blocks[0], blocks[1]);
    }

    for (int i = 0; i < 256; i++) {
        g_cases++;
        alignas(4) uint8_t vehicles[2][sizeof(PVehicle)];
        for (uint8_t &byte : vehicles[0])
            byte = uint8_t(RandomWord());
        memcpy(vehicles[1], vehicles[0], sizeof(PVehicle));
        bool on = RandomInt(2) != 0;
        int which = RandomInt(32);
        Both([&](bool original) {
            if (original)
                Orig_MissionEditorSwitch(reinterpret_cast<PVehicle *>(vehicles[0]), 0, on, which);
            else
                reinterpret_cast<PVehicle *>(vehicles[1])->MissionEditorSwitch(on, which);
        });
        Check("MissionEditorSwitch", i, vehicles[0], vehicles[1]);
    }

    // PhysicsData's constructor on the first "smackable" names
    const char *name = NULL;
    for (int i = 0; i < 16; i++) {
        name = AttributeSystemInstance->GetClassNextName("smackable", name);
        if (name == NULL)
            break;
        g_cases++;
        alignas(4) uint8_t data[2][sizeof(PhysicsData)];
        memset(data, 0xcd, sizeof(data));
        Both([&](bool original) {
            if (original)
                Orig_PhysicsDataConstruct(reinterpret_cast<PhysicsData *>(data[0]), 0, name);
            else
                reinterpret_cast<PhysicsData *>(data[1])->Construct(name);
        });
        Check("PhysicsData::PhysicsData", i, data[0], data[1]);
        reinterpret_cast<PhysicsData *>(data[0])->attributes.Destruct();
        reinterpret_cast<PhysicsData *>(data[1])->attributes.Destruct();
    }
}

void TestCarNames() {
    uint32_t counts[2] = {};
    const char *const *lists[2] = {};
    Both([&](bool original) {
        int side = original ? 0 : 1;
        counts[side] = original ? Orig_GetNameCount() : PVehicle::GetNameCount();
        lists[side] = original ? Orig_GetCarNames() : PVehicle::GetCarNames();
    });
    g_cases++;
    Check("GetNameCount", 0, counts[0], counts[1]);
    Check("GetCarNames", 0, lists[0], lists[1]);

    for (uint32_t i = 0; i < counts[1]; i++) {
        const char *name = lists[1][i];
        char forms[3][64];
        snprintf(forms[0], sizeof(forms[0]), "%s", name);
        snprintf(forms[1], sizeof(forms[1]), "%s", name);
        snprintf(forms[2], sizeof(forms[2]), "%s", name);
        for (char *c = forms[1]; *c != 0; c++)
            if (*c >= 'a' && *c <= 'z')
                *c = char(*c - 'a' + 'A');
        for (char *c = forms[2]; *c != 0; c++)
            if (*c >= 'A' && *c <= 'Z')
                *c = char(*c - 'A' + 'a');
        for (int form = 0; form < 3; form++) {
            g_cases++;
            int indices[2] = {};
            Both([&](bool original) {
                indices[original ? 0 : 1] =
                    original ? Orig_NameToIndex(forms[form]) : PVehicle::NameToIndex(forms[form]);
            });
            Check("NameToIndex", int(i * 3 + form), indices[0], indices[1]);
        }
    }

    // Each side builds its own list and map, then frees them with its own Shutdown
    CarNameMap *savedMap = fgCarNameMap;
    const char **savedNames = fgCarNames;
    uint32_t savedCount = fgCarNameCount;
    CarNameMap *maps[2] = {};
    const char **names[2] = {};
    uint32_t built[2] = {};
    Both([&](bool original) {
        int side = original ? 0 : 1;
        if (original)
            Orig_BuildCarNames();
        else
            PVehicle::BuildCarNames();
        maps[side] = fgCarNameMap;
        names[side] = fgCarNames;
        built[side] = fgCarNameCount;
    });
    g_cases++;
    Check("BuildCarNames count", 0, built[0], built[1]);
    if (maps[0] != NULL && maps[1] != NULL && built[0] == built[1]) {
        for (uint32_t i = 0; i < built[0]; i++)
            CheckString("BuildCarNames name", int(i), names[0][i], names[1][i]);
        CheckMaps("BuildCarNames map", 0, maps[0], maps[1]);
    }
    Both([&](bool original) {
        int side = original ? 0 : 1;
        fgCarNameMap = maps[side];
        fgCarNames = names[side];
        fgCarNameCount = built[side];
        if (original)
            Orig_Shutdown();
        else
            PVehicle::Shutdown();
    });
    fgCarNameMap = savedMap;
    fgCarNames = savedNames;
    fgCarNameCount = savedCount;
}

void TestNameMaps() {
    static const char *const kOwnNames[] = {
        "Alpha", "bravo", "CHARLIE", "delta", "Echo", "foxtrot", "golf", "HOTEL", "india", "juliet", "kilo", "Lima",
        "mike", "november", "OSCAR", "papa", "quebec", "romeo", "Sierra", "tango", "uniform", "victor", "whiskey",
        "xray", "yankee", "zulu", "a", "B", "ab", "AB", "zz", "",
    };
    std::vector<const char *> keys(kOwnNames, kOwnNames + sizeof(kOwnNames) / sizeof(kOwnNames[0]));
    const char *const *carNames = PVehicle::GetCarNames();
    for (uint32_t i = 0; i < PVehicle::GetNameCount(); i++)
        keys.push_back(carNames[i]);

    for (int run = 0; run < 24; run++) {
        CarNameMap *maps[2] = { NewMap(), NewMap() };
        for (int step = 0; step < 96; step++) {
            g_cases++;
            int what = RandomInt(10);
            const char *key = keys[size_t(RandomInt(int(keys.size())))];
            int value = RandomInt(1000);
            int first = RandomInt(int(maps[1]->size) + 1), count = RandomInt(4);
            bool whole = (RandomWord() & 1) != 0;
            TreeInsertResult results[2] = {};
            int positions[2] = {};
            Both([&](bool original) {
                int side = original ? 0 : 1;
                CarNameMap *map = maps[side];
                if (what < 6) {
                    TreePair pair = {};
                    pair.name = key;
                    *reinterpret_cast<int *>(&pair.ns) = value;
                    if (original)
                        Orig_InsertUnique(map, 0, &results[side], &pair);
                    else
                        map->InsertUnique(&results[side], &pair);
                    positions[side] = NodePosition(map, results[side].where);
                } else if (what < 9 && map->size != 0) {
                    TreeNode *where = NthNode(map, first % int(map->size));
                    TreeNode *next = NULL;
                    if (original)
                        Orig_EraseAt(map, 0, &next, where);
                    else
                        map->EraseAt(&next, where);
                    positions[side] = NodePosition(map, next);
                } else {
                    TreeNode *from = NthNode(map, first), *to = NthNode(map, first + count);
                    if (whole) {
                        from = map->head->left;
                        to = map->head;
                    }
                    TreeNode *next = NULL;
                    if (original)
                        Orig_EraseRange(map, 0, &next, from, to);
                    else
                        map->EraseRange(&next, from, to);
                    positions[side] = NodePosition(map, next);
                }
            });
            Check("CarNameMap answer", step, positions[0], positions[1]);
            Check("CarNameMap inserted", step, results[0].inserted, results[1].inserted);
            CheckMaps("CarNameMap", run * 100 + step, maps[0], maps[1]);
        }
        Both([&](bool original) {
            if (original)
                Orig_Destroy(maps[0], 0);
            else
                maps[1]->Destroy();
        });
        g_cases++;
        Check("CarNameMap::Destroy", run, maps[0]->head == NULL && maps[0]->size == 0,
              maps[1]->head == NULL && maps[1]->size == 0);
        OperatorDelete(maps[0]);
        OperatorDelete(maps[1]);
    }
}

// ---- PHelicopter

// One case's state: the copies the helicopter points at live here, so restoring the block restores them all
struct HeliState {
    alignas(16) PHelicopter heli;
    float hitPoints;
    alignas(16) uint8_t render[kRenderBytes];
    alignas(16) uint8_t sound[kSoundBytes];
    alignas(16) uint8_t ai[kAIBytes];
    alignas(16) ActionQueue queue;
    alignas(16) SimpleRigidBody body;   // the live body slot's contents
    SimRandom random;
    int32_t stepCount;
};

HeliState g_work, g_before, g_after[2];
SimpleRigidBody *g_body = NULL;

void Load(const HeliState &state) {
    memcpy(&g_work, &state, sizeof(HeliState));
    *g_body = state.body;
    *ShadowRandom = state.random;
    ShadowStepCount = state.stepCount;
}

void Save(HeliState *state) {
    memcpy(state, &g_work, sizeof(HeliState));
    state->body = *g_body;
    state->random = *ShadowRandom;
    state->stepCount = ShadowStepCount;
}

void RandomUnit(float *v, int n) {
    double length = 0.0;
    do {
        length = 0.0;
        for (int i = 0; i < n; i++) {
            v[i] = RandomFloat(-1.0f, 1.0f);
            length += double(v[i]) * v[i];
        }
    } while (length < 0.01);
    for (int i = 0; i < n; i++)
        v[i] = float(v[i] / sqrt(length));
}

void FillQueue(ActionQueue *queue) {
    memset(queue, 0, sizeof(ActionQueue));
    queue->ring.capacity = 200;
    queue->ring.head = RandomInt(200);
    queue->ring.count = RandomInt(12);
    queue->ring.tail = (queue->ring.head + queue->ring.count - 1) % 200;
    for (int i = 0; i < 200; i++) {
        queue->ring.items[i].action = 0x48 + RandomInt(9);
        queue->ring.items[i].source = RandomInt(4);
        float value = RandomFloat(-1.0f, 1.0f);
        if (RandomInt(3) == 0)
            value *= 0.25f;
        queue->ring.items[i].value = value;
    }
}

void RandomiseHeli(HeliState *state, bool randomBody) {
    PHelicopter &heli = state->heli;
    heli.controlGas = RandomFloat(-1.2f, 1.2f);
    heli.controlStrafe = RandomFloat(-1.2f, 1.2f);
    heli.controlSteer = RandomInt(4) == 0 ? 0.0f : RandomFloat(-1.0f, 1.0f);
    heli.controlAltitude = RandomFloat(-1.0f, 1.0f);
    if (RandomInt(5) == 0)
        heli.controlGas = 0.0f;
    heli.roll = RandomFloat(-1.0f, 1.0f);
    heli.unknown74 = RandomFloat(-1.0f, 1.0f);
    heli.unknownA8 = RandomInt(6) == 0 ? 0 : 1;
    heli.heliClass = RandomInt(4) == 0 ? 0 : 1;
    heli.sleepStep = RandomInt(3) == 0 ? state->stepCount - RandomInt(2) : 0;
    heli.damageScale = RandomInt(3) == 0 ? -1.0f : RandomFloat(0.01f, 1.0f);
    heli.scoreable = RandomInt(2) != 0;
    for (DamageZone &zone : heli.damageZones) {
        zone.unknown00 = RandomInt(3) == 0 ? 0.0f : RandomFloat(0.0f, 1.2f);
        zone.unknown04 = RandomInt(3) == 0 ? 0.0f : RandomFloat(0.0f, 1.2f);
    }
    static const float kHitPoints[] = { -1.0f, 0.0f, 0.5f, 5.0f, 50.0f, 500.0f };
    state->hitPoints = kHitPoints[RandomInt(6)];
    state->stepCount ^= RandomInt(2) << 5;
    FillQueue(&state->queue);
    if (randomBody) {
        RandomUnit(&state->body.orientation.x, 4);
        state->body.velocity.x = RandomFloat(-30.0f, 30.0f);
        state->body.velocity.y = RandomFloat(-10.0f, 10.0f);
        state->body.velocity.z = RandomFloat(-30.0f, 30.0f);
        if (RandomInt(8) == 0)
            state->body.velocity = Coord3{0.0f, 0.0f, 0.0f};
    }
}

void TestHelicopter(PHelicopter *live, PhysicsObject *renderSource, int slot, const char *what) {
    RSceneObj *render = live != NULL ? live->renderObject : renderSource->renderObject;
    if (render == NULL)
        return;
    g_body = Simulation_GetSimpleRigidBody(ShadowSim, 0, slot);
    SimpleRigidBody savedBody = *g_body;
    SimRandom savedRandom = *ShadowRandom;
    int32_t savedStep = ShadowStepCount;

    // The helicopter and the copies it points at
    memset(&g_work, 0, sizeof(g_work));
    if (live != NULL) {
        memcpy(&g_work.heli, live, sizeof(PHelicopter));
        if (live->audioObject != NULL)
            memcpy(g_work.sound, live->audioObject, kSoundBytes);
        if (live->ai != NULL)
            memcpy(g_work.ai, live->ai, kAIBytes);
        g_work.hitPoints = live->hitPointLoc != NULL ? *live->hitPointLoc : 100.0f;
    } else {
        g_work.heli.vtable = reinterpret_cast<void **>(kPHelicopterVtable);
        g_work.heli.type = 10;
        g_work.heli.flags = PhysicsObject::kSimpleBody;
        g_work.heli.rigidBodySlot = int16_t(slot);
        g_work.heli.heliClass = 1;
        g_work.heli.damageScale = -1.0f;
        g_work.heli.scoreable = true;
        g_work.hitPoints = 100.0f;
        g_work.body = savedBody;
        g_work.body.flags = SimpleRigidBody::kMoves | SimpleRigidBody::kTouchesTriggers;
        g_work.body.bodyType = kSimpleHelicopter;
        render->GetPosition(&g_work.body.position);
    }
    if (live != NULL)
        g_work.body = savedBody;
    memcpy(g_work.render, render, kRenderBytes);
    void **realVtable = *reinterpret_cast<void ***>(render);
    for (int i = 0; i < 32; i++)
        g_fakeRenderVtable[i] = realVtable[i];
    g_fakeRenderVtable[14] = (void *)&FakeUpdatePosition;
    g_fakeRenderVtable[15] = (void *)&FakeTriggerFX;
    *reinterpret_cast<void ***>(g_work.render) = g_fakeRenderVtable;
    g_work.heli.renderObject = reinterpret_cast<RSceneObj *>(g_work.render);
    g_work.heli.audioObject = reinterpret_cast<ABaseSound *>(g_work.sound);
    g_work.heli.ai = reinterpret_cast<HelicopterAI *>(g_work.ai);
    g_work.heli.hitPointLoc = &g_work.hitPoints;
    g_work.heli.actionQueue = &g_work.queue;
    g_work.random = savedRandom;
    g_work.stepCount = savedStep;
    HeliState base;
    memcpy(&base, &g_work, sizeof(HeliState));

    // A target that some missiles have, so the kill's shot count finds them
    WTargetable *missileTarget = NULL;
    for (PhysicsObject **m = ShadowMissilesFirst; m != NULL && m != ShadowMissilesLast; m++)
        if (*m != NULL)
            missileTarget = *reinterpret_cast<WTargetable **>(reinterpret_cast<uint8_t *>(*m) + 0x6c);
    static uint8_t dummyTarget[0x40];
    uint32_t playerSig = 0;
    PhysicsObject *player = Simulation_GetPlayerObject(ShadowSim, 0);
    if (player != NULL)
        player->GetSig(&playerSig);

    Fakes fakes;
    for (int i = 0; i < 6; i++) {
        g_cases++;
        memcpy(&g_before, &base, sizeof(HeliState));
        uint32_t counts[2] = {};
        DamageZone *zones[2] = {};
        Load(g_before);
        Both([&](bool original) {
            int side = original ? 0 : 1;
            Load(g_before);
            zones[side] = original ? Orig_HeliGetDamageZones(&g_work.heli, 0, &counts[side])
                                   : g_work.heli.GetDamageZones(&counts[side]);
        });
        Check("PHelicopter::GetDamageZones", i, counts[0], counts[1]);
        Check("PHelicopter::GetDamageZones answer", i, zones[0], zones[1]);
    }

    for (int i = 0; i < 64; i++) {
        g_cases++;
        memcpy(&g_before, &base, sizeof(HeliState));
        FillQueue(&g_before.queue);
        Both([&](bool original) {
            Load(g_before);
            if (original)
                Orig_HeliGetControllerInput(&g_work.heli, 0);
            else
                g_work.heli.GetControllerInput();
            Save(&g_after[original ? 0 : 1]);
        });
        CheckBytes("PHelicopter::GetControllerInput", i, &g_after[0], &g_after[1], sizeof(HeliState));
    }

    for (int i = 0; i < 400; i++) {
        g_cases++;
        memcpy(&g_before, &base, sizeof(HeliState));
        RandomiseHeli(&g_before, true);
        Both([&](bool original) {
            Load(g_before);
            if (original)
                Orig_HeliSimulate(&g_work.heli, 0);
            else
                g_work.heli.Simulate();
            Save(&g_after[original ? 0 : 1]);
        });
        CheckBytes("PHelicopter::Simulate", i, &g_after[0], &g_after[1], sizeof(HeliState));
        CheckCalls("PHelicopter::Simulate calls", i);
    }

    for (int i = 0; i < 800; i++) {
        g_cases++;
        memcpy(&g_before, &base, sizeof(HeliState));
        RandomiseHeli(&g_before, true);
        HelicopterAI *ai = reinterpret_cast<HelicopterAI *>(g_before.ai);
        int target = RandomInt(3);
        ai->targetBeacon = target == 0 ? NULL : target == 1 && missileTarget != NULL
                                                    ? missileTarget
                                                    : reinterpret_cast<WTargetable *>(dummyTarget);

        // A segment through the box (or now and then past it)
        Coord4 halfExtents;
        reinterpret_cast<RSceneObj *>(g_before.render)->GetBoundingDimensions(&halfExtents);
        float reach = float(fabs(halfExtents.x) + fabs(halfExtents.y) + fabs(halfExtents.z)) * 2.0f + 1.0f;
        float direction[3], offset[3];
        RandomUnit(direction, 3);
        RandomUnit(offset, 3);
        float miss = RandomInt(6) == 0 ? reach * 3.0f : RandomFloat(0.0f, reach * 0.2f);
        const Coord3 &centre = g_before.body.position;
        Coord3 from = { centre.x + direction[0] * reach + offset[0] * miss,
                        centre.y + direction[1] * reach + offset[1] * miss,
                        centre.z + direction[2] * reach + offset[2] * miss };
        Coord3 to = { centre.x - direction[0] * reach + offset[0] * miss,
                      centre.y - direction[1] * reach + offset[1] * miss,
                      centre.z - direction[2] * reach + offset[2] * miss };
        float amount = RandomInt(4) == 0 ? RandomFloat(0.0f, 5.0f) : RandomFloat(0.0f, 120.0f);
        float split = RandomInt(4) == 0 ? 0.0f : RandomFloat(0.0f, 1.0f);
        int kind = RandomInt(4);
        uint32_t sources[3] = { playerSig, 0, RandomWord() };
        uint32_t source = sources[RandomInt(3)];
        int answers[2] = {};
        Both([&](bool original) {
            int side = original ? 0 : 1;
            Load(g_before);
            answers[side] = original ? Orig_HeliApplyDamage(&g_work.heli, 0, &from, &to, amount, split, kind, &source)
                                     : g_work.heli.ApplyDamage(&from, &to, amount, split, kind, &source);
            Save(&g_after[side]);
        });
        Check("PHelicopter::ApplyDamage answer", i, answers[0], answers[1]);
        CheckBytes("PHelicopter::ApplyDamage", i, &g_after[0], &g_after[1], sizeof(HeliState));
        CheckCalls("PHelicopter::ApplyDamage calls", i);
    }

    *g_body = savedBody;
    *ShadowRandom = savedRandom;
    ShadowStepCount = savedStep;
    (void)what;
}

int TestHelicopters(bool includeTemplate) {
    int helicopters = 0;
    for (PhysicsObject **h = ShadowHelicoptersFirst; h != NULL && h != ShadowHelicoptersLast; h++) {
        PHelicopter *heli = static_cast<PHelicopter *>(*h);
        if (heli == NULL || heli->renderObject == NULL || !(heli->flags & PhysicsObject::kSimpleBody))
            continue;
        TestHelicopter(heli, NULL, heli->rigidBodySlot, "live");
        helicopters++;
    }
    if (includeTemplate) {
        PhysicsObject *player = Simulation_GetPlayerObject(ShadowSim, 0);
        int freeSlot = -1;
        for (int i = kSimpleBodies; i-- > 0;)
            if (ShadowSimpleOwners[i] == NULL && freeSlot < 0)
                freeSlot = i;
        if (player != NULL && player->renderObject != NULL && freeSlot >= 0)
            TestHelicopter(NULL, player, freeSlot, "template");
    }
    return helicopters;
}

bool Enabled() {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_VEHICLESHADOW", value, sizeof(value));
    return length != 0 && length < sizeof(value) && atoi(value) != 0;
}

void Report(const char *what, int helicopters) {
    printf("[vehicle] %s: %d live helicopters; %d cases, %d checks, %d differ%s\n", what, helicopters, g_cases,
           g_checks, g_differ, g_faults != 0 ? " (faults counted)" : "");
    if (g_faults != 0)
        printf("[vehicle]   %d calls faulted\n", g_faults);
    fflush(stdout);
}

bool g_waitingForHelicopters = false;

} // namespace

void VehicleShadow_Run(void) {
    if (!Enabled())
        return;
    FpControlGet(&g_x87, &g_sse);
    g_cases = g_checks = g_differ = g_details = g_faults = 0;
    TestAttributes();
    TestCarNames();
    TestNameMaps();
    int helicopters = TestHelicopters(true);
    g_waitingForHelicopters = helicopters == 0;
    Report("PVehicle, car names, PhysicsData, PHelicopter", helicopters);
}

void VehicleShadow_Tick(void) {
    if (!g_waitingForHelicopters)
        return;
    if (ShadowHelicoptersFirst == ShadowHelicoptersLast)
        return;
    g_waitingForHelicopters = false;
    FpControlGet(&g_x87, &g_sse);
    g_cases = g_checks = g_differ = g_details = g_faults = 0;
    int helicopters = TestHelicopters(false);
    Report("PHelicopter (live)", helicopters);
}
