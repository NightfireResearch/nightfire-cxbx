#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "GeoPrimShadow.h"
#include "FpControl.h"

#include "../eagl/D3D8State.h"
#include "../eagl/EaglGlobals.h"
#include "../eagl/GeoPrimState.h"
#include "../eagl/RenderContext.h"
#include "../eagl/RenderMethod.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_GEOPRIMSHADOW=1: eagl/GeoPrimState.cpp against the originals (0x000eec70-0x000ef4b0 swapped back in
// for each original run, common/xbeOriginal.h), on identical inputs.
//
//   - Every setter and getter, both constructors, the extension's constructor, DumpState and the destructor, on
//     objects of random bytes with random and boundary arguments: the object's bytes, the getter's output (and
//     the bytes beside it) and the answer compared.
//   - Apply on random states (field values drawn from small pools, so consecutive states share some and the cache
//     both hits and misses), in sequences from three starting points - the live cache, a cache invalidated as
//     EndFrame does, a random cache - and on the font driver's state. After each step: the answer, the apply
//     cache, the polygon offset cache, the render context extension's shadows, the per-stage textures, D3D8's
//     dirty flags and render-state table, the current RenderContext's bytes, and a log of the D3D8 calls made.
//   - Apply on the states the game draws with: from the first tick, render-method opcode 16 goes through a
//     collector that tests each state it has not seen before (by value) from the live cache, from an invalidated
//     cache and after up to four states seen earlier, then puts everything back and runs the real handler. A
//     summary line follows new states by a second or so; the collector stops at 2048 states.
//
// The D3D8 calls Apply makes (SetTexture, SetRenderState_FillMode, _Simple, _CullMode) go to recording fakes for
// the duration of each test, over both the XBE entries the original calls and the seam functions the port calls
// directly, so nothing reaches the backend; everything Apply writes is put back after each side. A seam function
// that only returns (the fill mode's) is not hooked on either side: a jump does not fit in it.
//
// One mutation this catches: SetAlphaBlendMode's modes 3-5 taken in address order (3 reverse subtract, 4 alpha
// over zero, 5 destination colour) instead of the jump table's (3 alpha over zero, 4 destination colour,
// 5 reverse subtract) differs in the object's +0x30..+0x38 on every case with mode 3, 4 or 5.
// ---------------------------------------------------------------------------------------------------------------

namespace {

using EAGL::GeoPrimState;
using EAGL::GeoPrimStateExtension;

void OriginalWindow(bool original) {
    XbeOriginal_RestoreRange(0x000eec70, 0x000ef4b0, original);
}

#define FontState (*(const GeoPrimState *)0x002400f8)       // EaglFont's state
#define OpcodeSlot16 (*(void (**)())0x001ce740)               // the render-method opcode table's entry 16
#define InterpParam (*(EAGL::GeoPrimParam **)0x002401c0)      // the interpreter's parameter cursor
#define InterpPacket (*(const EAGL::ParamPacket **)0x002401c4)   // and its packet cursor

const uint32_t kOpcode16 = 0x000f6320;
const int kRenderStates = 128;

// ---- the originals, thiscall through __fastcall (EDX unused)

typedef bool (__fastcall *SetFn)(void *, int, uint32_t);
typedef bool (__fastcall *SetFloatFn)(void *, int, float);
typedef bool (__fastcall *GetFn)(const void *, int, void *);
typedef bool (__fastcall *Set3Fn)(void *, int, uint32_t, uint32_t, uint32_t);
typedef bool (__fastcall *Get3Fn)(const void *, int, void *, void *, void *);
typedef void *(__fastcall *ConstructFn)(void *, int);
typedef void *(__fastcall *CopyFn)(void *, int, const void *);
typedef void (__fastcall *VoidFn)(void *, int);
typedef bool (__fastcall *ApplyFn)(const void *, int);

#define Orig_Apply ((ApplyFn)0x000ef050)
#define Orig_ExtensionConstruct ((ConstructFn)0x000eeea0)
#define Orig_DumpState ((VoidFn)0x000eef10)
#define Orig_Construct ((ConstructFn)0x000ef480)
#define Orig_Destruct ((VoidFn)0x000ef490)
#define Orig_ConstructCopy ((CopyFn)0x000ef4a0)
#define Orig_SetAlphaBlend ((Set3Fn)0x000eef80)
#define Orig_GetAlphaBlend ((Get3Fn)0x000eefa0)

// ---- the ports of the one-argument setters and getters

enum ArgKind { kArgWord, kArgBool, kArgMode, kArgFloat };

#define PORT_SET(cls, name, type) \
    bool Port##name(GeoPrimState *s, uint32_t v) { return static_cast<EAGL::cls *>(s)->name(type(v)); }
#define PORT_GET(cls, name, type) \
    bool Port##name(GeoPrimState *s, void *out) { return static_cast<EAGL::cls *>(s)->name((type *)out); }

bool PortSetZOffset(GeoPrimState *s, uint32_t v) {
    float f;
    memcpy(&f, &v, 4);
    return static_cast<GeoPrimStateExtension *>(s)->SetZOffset(f);
}
bool PortSetZSlopeScale(GeoPrimState *s, uint32_t v) {
    float f;
    memcpy(&f, &v, 4);
    return static_cast<GeoPrimStateExtension *>(s)->SetZSlopeScale(f);
}

PORT_SET(GeoPrimState, SetPrimitiveType, uint32_t)
PORT_SET(GeoPrimState, SetShading, uint32_t)
PORT_SET(GeoPrimState, SetCullEnable, bool)
PORT_SET(GeoPrimState, SetDepthTestMethod, uint32_t)
PORT_SET(GeoPrimState, SetAlphaBlendMode, uint32_t)
PORT_SET(GeoPrimState, SetAlphaTestEnable, bool)
PORT_SET(GeoPrimState, SetAlphaCompareValue, uint32_t)
PORT_SET(GeoPrimState, SetAlphaTestMethod, uint32_t)
PORT_SET(GeoPrimState, SetTextureEnable, bool)
PORT_SET(GeoPrimState, SetTextureCoordType, uint32_t)
PORT_SET(GeoPrimState, SetTransparencyMethod, uint32_t)
PORT_SET(GeoPrimState, SetChromaColour, uint32_t)
PORT_SET(GeoPrimStateExtension, SetCullDirection, uint32_t)
PORT_SET(GeoPrimStateExtension, SetFillMode, uint32_t)
PORT_SET(GeoPrimStateExtension, SetBlendOperation, uint32_t)
PORT_SET(GeoPrimStateExtension, SetBlendColour, uint32_t)
PORT_SET(GeoPrimStateExtension, SetZWritesEnable, bool)

PORT_GET(GeoPrimState, GetPrimitiveType, uint32_t)
PORT_GET(GeoPrimState, GetShading, uint32_t)
PORT_GET(GeoPrimState, GetCullEnable, bool)
PORT_GET(GeoPrimState, GetDepthTestMethod, uint32_t)
PORT_GET(GeoPrimState, GetAlphaBlendMode, uint32_t)
PORT_GET(GeoPrimState, GetAlphaTestEnable, bool)
PORT_GET(GeoPrimState, GetAlphaCompareValue, uint32_t)
PORT_GET(GeoPrimState, GetAlphaTestMethod, uint32_t)
PORT_GET(GeoPrimState, GetTextureEnable, bool)
PORT_GET(GeoPrimState, GetTextureCoordType, uint32_t)
PORT_GET(GeoPrimState, GetTransparencyMethod, uint32_t)
PORT_GET(GeoPrimState, GetChromaColour, uint32_t)
PORT_GET(GeoPrimStateExtension, GetCullDirection, uint32_t)
PORT_GET(GeoPrimStateExtension, GetFillMode, uint32_t)
PORT_GET(GeoPrimStateExtension, GetBlendOperation, uint32_t)
PORT_GET(GeoPrimStateExtension, GetZOffset, float)
PORT_GET(GeoPrimStateExtension, GetZSlopeScale, float)
PORT_GET(GeoPrimStateExtension, GetBlendColour, uint32_t)
PORT_GET(GeoPrimStateExtension, GetZWritesEnable, bool)

struct Setter {
    const char *name;
    uint32_t original;
    bool (*port)(GeoPrimState *, uint32_t);
    ArgKind arg;
};

#define SETTER(name, address, arg) { #name, address, Port##name, arg }
const Setter kSetters[] = {
    SETTER(SetPrimitiveType, 0x000eec70, kArgWord),
    SETTER(SetShading, 0x000eec90, kArgWord),
    SETTER(SetCullEnable, 0x000eecb0, kArgBool),
    SETTER(SetDepthTestMethod, 0x000eecd0, kArgWord),
    SETTER(SetAlphaBlendMode, 0x000eecf0, kArgMode),
    SETTER(SetAlphaTestEnable, 0x000eedc0, kArgBool),
    SETTER(SetAlphaCompareValue, 0x000eede0, kArgWord),
    SETTER(SetAlphaTestMethod, 0x000eee00, kArgWord),
    SETTER(SetTextureEnable, 0x000eee20, kArgBool),
    SETTER(SetTextureCoordType, 0x000eee40, kArgWord),
    SETTER(SetTransparencyMethod, 0x000eee60, kArgWord),
    SETTER(SetChromaColour, 0x000eee80, kArgWord),
    SETTER(SetCullDirection, 0x000eef20, kArgWord),
    SETTER(SetFillMode, 0x000eef40, kArgWord),
    SETTER(SetBlendOperation, 0x000eef60, kArgWord),
    SETTER(SetZOffset, 0x000eefc0, kArgFloat),
    SETTER(SetZSlopeScale, 0x000eefe0, kArgFloat),
    SETTER(SetBlendColour, 0x000ef000, kArgWord),
    SETTER(SetZWritesEnable, 0x000ef020, kArgBool),
};

struct Getter {
    const char *name;
    uint32_t original;
    bool (*port)(GeoPrimState *, void *);
};

#define GETTER(name, address) { #name, address, Port##name }
const Getter kGetters[] = {
    GETTER(GetPrimitiveType, 0x000eec80),
    GETTER(GetShading, 0x000eeca0),
    GETTER(GetCullEnable, 0x000eecc0),
    GETTER(GetDepthTestMethod, 0x000eece0),
    GETTER(GetAlphaBlendMode, 0x000eedb0),
    GETTER(GetAlphaTestEnable, 0x000eedd0),
    GETTER(GetAlphaCompareValue, 0x000eedf0),
    GETTER(GetAlphaTestMethod, 0x000eee10),
    GETTER(GetTextureEnable, 0x000eee30),
    GETTER(GetTextureCoordType, 0x000eee50),
    GETTER(GetTransparencyMethod, 0x000eee70),
    GETTER(GetChromaColour, 0x000eee90),
    GETTER(GetCullDirection, 0x000eef30),
    GETTER(GetFillMode, 0x000eef50),
    GETTER(GetBlendOperation, 0x000eef70),
    GETTER(GetZOffset, 0x000eefd0),
    GETTER(GetZSlopeScale, 0x000eeff0),
    GETTER(GetBlendColour, 0x000ef010),
    GETTER(GetZWritesEnable, 0x000ef030),
};

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 12)
        printf("[geoprim]   %s #%d: %s\n", what, index, detail);
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

void CheckU32(const char *what, int index, uint32_t a, uint32_t b) {
    g_checks++;
    if (a == b)
        return;
    char detail[64];
    snprintf(detail, sizeof(detail), "original %08x, port %08x", a, b);
    Differ(what, index, detail);
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

// ---- random inputs (a generator of our own: the game's is not touched)

uint32_t g_seed = 0x6e0b57a7;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return (g_seed >> 8) ^ (g_seed << 24);
}

uint32_t Pick(const uint32_t *pool, int count) {
    return pool[Next() % count];
}

// A float's bits with no signalling NaN, which a store through the FPU would quieten on one side only
uint32_t QuietFloat(uint32_t bits) {
    if ((bits & 0x7f800000) == 0x7f800000 && (bits & 0x007fffff) != 0)
        bits |= 0x00400000;
    return bits;
}

uint32_t FloatArg() {
    static const uint32_t kPool[] = { 0x00000000, 0x80000000, 0x3f800000, 0x40200000, 0xbf000000, 0x7fc00000,
                                      0xffc00001, 0x7f800000, 0x00000001 };
    return (Next() & 1) != 0 ? Pick(kPool, 9) : QuietFloat(Next());
}

uint32_t Arg(ArgKind kind) {
    switch (kind) {
    case kArgBool:
        return Next() & 1;
    case kArgMode:
        return (Next() & 3) != 0 ? Next() % 8 : Next();
    case kArgFloat:
        return FloatArg();
    default:
        return (Next() & 1) != 0 ? Next() % 8 : Next();
    }
}

void RandomObject(GeoPrimState *s) {
    uint8_t *bytes = reinterpret_cast<uint8_t *>(s);
    for (size_t i = 0; i < sizeof(*s); i++)
        bytes[i] = uint8_t(Next());
    uint32_t bits;
    memcpy(&bits, &s->zSlopeScale, 4);
    bits = QuietFloat(bits);
    memcpy(&s->zSlopeScale, &bits, 4);
    memcpy(&bits, &s->zOffset, 4);
    bits = QuietFloat(bits);
    memcpy(&s->zOffset, &bits, 4);
}

// ---- a run guarded against faults; the original inside the window

typedef void (*CaseFn)(void *context, bool original);

bool Guarded(CaseFn run, void *context, bool original) {
    if (original)
        OriginalWindow(true);
    bool ok = true;
#ifdef _MSC_VER
    __try {
        run(context, original);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        ok = false;
    }
#else
    run(context, original);
#endif
    if (original)
        OriginalWindow(false);
    return ok;
}

// ---- the setters, getters and constructors

struct ObjectCase {
    GeoPrimState object;
    GeoPrimState source;             // ConstructCopy's
    uint8_t out[3][8];               // the getters' outputs, with bytes beside them
    uint32_t args[3];
    int which;                       // a setter or getter index
    int kind;
    uint32_t answer;
};

enum { kSetter, kGetter, kSetAlphaBlend, kGetAlphaBlend, kExtensionConstruct, kConstruct, kConstructCopy,
       kDumpState, kDestruct, kObjectKinds };

void RunObject(void *context, bool original) {
    ObjectCase *c = static_cast<ObjectCase *>(context);
    GeoPrimState *s = &c->object;
    GeoPrimStateExtension *x = static_cast<GeoPrimStateExtension *>(s);
    switch (c->kind) {
    case kSetter: {
        const Setter &f = kSetters[c->which];
        if (!original)
            c->answer = f.port(s, c->args[0]);
        else if (f.arg == kArgFloat) {
            float v;
            memcpy(&v, &c->args[0], 4);
            c->answer = ((SetFloatFn)f.original)(s, 0, v);
        } else {
            c->answer = ((SetFn)f.original)(s, 0, c->args[0]);
        }
        break;
    }
    case kGetter: {
        const Getter &f = kGetters[c->which];
        c->answer = original ? ((GetFn)f.original)(s, 0, c->out[0]) : f.port(s, c->out[0]);
        break;
    }
    case kSetAlphaBlend:
        c->answer = original ? Orig_SetAlphaBlend(s, 0, c->args[0], c->args[1], c->args[2])
                             : x->SetAlphaBlend(c->args[0], c->args[1], c->args[2]);
        break;
    case kGetAlphaBlend:
        c->answer = original ? Orig_GetAlphaBlend(s, 0, c->out[0], c->out[1], c->out[2])
                             : x->GetAlphaBlend((uint32_t *)c->out[0], (uint32_t *)c->out[1], (uint32_t *)c->out[2]);
        break;
    // The constructors answer 'this': kept as an offset from the object, which differs between the sides
    case kExtensionConstruct:
        c->answer = uint32_t(uintptr_t(original ? Orig_ExtensionConstruct(s, 0) : x->Construct()) - uintptr_t(s));
        break;
    case kConstruct:
        c->answer = uint32_t(uintptr_t(original ? Orig_Construct(s, 0) : s->Construct()) - uintptr_t(s));
        break;
    case kConstructCopy:
        c->answer = uint32_t(uintptr_t(original ? Orig_ConstructCopy(s, 0, &c->source) : s->ConstructCopy(&c->source)) -
                             uintptr_t(s));
        break;
    case kDumpState:
        if (original)
            Orig_DumpState(s, 0);
        else
            x->DumpState();
        break;
    case kDestruct:
        if (original)
            Orig_Destruct(s, 0);
        else
            s->Destruct();
        break;
    }
}

ObjectCase g_object[2];

void TestObjects() {
    const int kRounds = 200;
    int index = 0;
    for (int kind = 0; kind < kObjectKinds; kind++) {
        int functions = kind == kSetter ? int(sizeof(kSetters) / sizeof(kSetters[0]))
                      : kind == kGetter ? int(sizeof(kGetters) / sizeof(kGetters[0]))
                      : 1;
        for (int which = 0; which < functions; which++) {
            for (int round = 0; round < kRounds; round++, index++) {
                ObjectCase &c = g_object[0];
                memset(&c, 0, sizeof(c));
                c.kind = kind;
                c.which = which;
                RandomObject(&c.object);
                RandomObject(&c.source);
                for (int i = 0; i < 3; i++)
                    for (int b = 0; b < 8; b++)
                        c.out[i][b] = uint8_t(Next());
                if (kind == kSetter)
                    c.args[0] = Arg(kSetters[which].arg);
                for (int i = kind == kSetter ? 1 : 0; i < 3; i++)
                    c.args[i] = Arg(kArgWord);
                g_object[1] = c;
                g_cases++;
                if (!Guarded(RunObject, &g_object[0], true) || !Guarded(RunObject, &g_object[1], false))
                    continue;
                const char *what = kind == kSetter ? kSetters[which].name
                                 : kind == kGetter ? kGetters[which].name
                                 : kind == kSetAlphaBlend ? "SetAlphaBlend"
                                 : kind == kGetAlphaBlend ? "GetAlphaBlend"
                                 : kind == kExtensionConstruct ? "GeoPrimStateExtension::Construct"
                                 : kind == kConstruct ? "Construct"
                                 : kind == kConstructCopy ? "ConstructCopy"
                                 : kind == kDumpState ? "DumpState" : "Destruct";
                CheckBytes(what, index, &g_object[0].object, &g_object[1].object, sizeof(GeoPrimState));
                CheckBytes(what, index, g_object[0].out, g_object[1].out, sizeof(g_object[0].out));
                uint32_t mask = kind >= kExtensionConstruct ? 0xffffffff : 0xff;   // bools answer in AL
                if (kind != kDumpState && kind != kDestruct)
                    CheckU32(what, index, g_object[0].answer & mask, g_object[1].answer & mask);
            }
        }
    }
}

// ---- recording fakes for the D3D8 calls Apply makes

struct Call {
    uint32_t what, a, b;
};
const int kMaxCalls = 64;
Call g_calls[kMaxCalls];
int g_callCount;

void Record(uint32_t what, uint32_t a, uint32_t b) {
    if (g_callCount < kMaxCalls) {
        g_calls[g_callCount].what = what;
        g_calls[g_callCount].a = a;
        g_calls[g_callCount].b = b;
    }
    g_callCount++;
}

void __stdcall FakeSetTexture(uint32_t stage, D3DPixelContainer *texture) {
    Record(1, stage, uint32_t(uintptr_t(texture)));
}
void __stdcall FakeFillMode(uint32_t mode) {
    Record(2, mode, 0);
}
void __fastcall FakeSimple(uint32_t method, uint32_t value) {
    Record(3, method, value);
}
void __stdcall FakeCullMode(uint32_t mode) {
    Record(4, mode, 0);
}

// ---- hooks: a jump written over an entry

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[16];
int g_hookCount;

bool Hooked(uint32_t at) {
    for (int i = 0; i < g_hookCount; i++)
        if (g_hooks[i].at == at)
            return true;
    return false;
}

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0])) || Hooked(at))
        return;
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    h.on = false;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = (int32_t)((uint32_t)(uintptr_t)to - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    h.on = true;
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
        h.on = false;
    }
    g_hookCount = 0;
}

// A seam function that only returns (the fill mode's ignores its value) is too short for a jump: five bytes
// written over it would land on whatever the linker put next. Its calls have no effect, so they are left to it on
// both sides and the fill mode is compared through the cache.
bool OnlyReturns(const void *function) {
    uint8_t first = *static_cast<const uint8_t *>(function);
    return first == 0xc3 || first == 0xc2;
}

// The XBE entry the original calls and the seam function the port calls directly, both to the fake
void HookPair(uint32_t entry, const void *seam, const void *fake) {
    if (OnlyReturns(seam))
        return;
    HookOne(entry, fake);
    HookOne((uint32_t)(uintptr_t)seam, fake);
}

void HooksInstall() {
    HookPair(0x00166830, (const void *)D3DDevice_SetTexture, (const void *)FakeSetTexture);
    HookPair(0x00167ad0, (const void *)D3DDevice_SetRenderState_FillMode, (const void *)FakeFillMode);
    HookPair(0x001673e0, (const void *)D3DDevice_SetRenderState_Simple, (const void *)FakeSimple);
    HookPair(0x001677b0, (const void *)D3DDevice_SetRenderState_CullMode, (const void *)FakeCullMode);
}

// ---- everything Apply writes

struct Snapshot {
    GeoPrimApplyCache cache;
    float zSlopeScale, zOffset;
    ExtensionShadows shadows;
    D3DPixelContainer *stageTextures[4];
    uint32_t dirtyFlags;
    uint32_t renderStates[kRenderStates];
    uint8_t renderContext[sizeof(EAGL::RenderContext)];
};

void Take(Snapshot *s) {
    memset(s, 0, sizeof(*s));
    s->cache = ApplyCache;
    s->zSlopeScale = ApplyCacheZSlopeScale;
    s->zOffset = ApplyCacheZOffset;
    s->shadows = Shadows;
    memcpy(s->stageTextures, StageTexture, sizeof(s->stageTextures));
    s->dirtyFlags = D3DDirtyFlags;
    memcpy(s->renderStates, D3DRenderState, sizeof(s->renderStates));
    if (CurrentRenderContext != NULL)
        memcpy(s->renderContext, CurrentRenderContext, sizeof(s->renderContext));
}

void Put(const Snapshot *s) {
    ApplyCache = s->cache;
    ApplyCacheZSlopeScale = s->zSlopeScale;
    ApplyCacheZOffset = s->zOffset;
    Shadows = s->shadows;
    memcpy(StageTexture, s->stageTextures, sizeof(s->stageTextures));
    D3DDirtyFlags = s->dirtyFlags;
    memcpy(D3DRenderState, s->renderStates, sizeof(s->renderStates));
    if (CurrentRenderContext != NULL)
        memcpy(CurrentRenderContext, s->renderContext, sizeof(s->renderContext));
}

Snapshot g_base, g_pre, g_afterOriginal, g_afterPort;
Call g_originalCalls[kMaxCalls];
int g_originalCallCount;

// Word by word, naming the first word that differs
void CheckWords(const char *what, int index, const char *part, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint32_t *x = static_cast<const uint32_t *>(a), *y = static_cast<const uint32_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    char detail[128];
    snprintf(detail, sizeof(detail), "%s word %u (+0x%02x): original %08x, port %08x", part, unsigned(at),
             unsigned(at * 4), x[at], y[at]);
    Differ(what, index, detail);
}

void CheckSnapshots(const char *what, int index) {
    const Snapshot &a = g_afterOriginal, &b = g_afterPort;
    CheckWords(what, index, "apply cache", &a.cache, &b.cache, sizeof(a.cache));
    CheckWords(what, index, "z slope scale, z offset cache", &a.zSlopeScale, &b.zSlopeScale, 8);
    CheckWords(what, index, "extension shadows", &a.shadows, &b.shadows, sizeof(a.shadows));
    CheckWords(what, index, "stage textures", a.stageTextures, b.stageTextures, sizeof(a.stageTextures));
    CheckWords(what, index, "D3D8 dirty flags", &a.dirtyFlags, &b.dirtyFlags, 4);
    CheckWords(what, index, "D3D8 render-state slot", a.renderStates, b.renderStates, sizeof(a.renderStates));
    CheckWords(what, index, "render context", a.renderContext, b.renderContext, sizeof(a.renderContext));
}

const char *CallName(uint32_t what) {
    static const char *const kNames[] = { "?", "SetTexture", "SetRenderState_FillMode", "SetRenderState_Simple",
                                          "SetRenderState_CullMode" };
    return what < 5 ? kNames[what] : "?";
}

void CheckCalls(const char *what, int index) {
    g_checks++;
    int n = g_callCount < g_originalCallCount ? g_callCount : g_originalCallCount;
    if (n > kMaxCalls)
        n = kMaxCalls;
    int at = 0;
    while (at < n && memcmp(&g_originalCalls[at], &g_calls[at], sizeof(Call)) == 0)
        at++;
    if (at == n && g_callCount == g_originalCallCount)
        return;
    char detail[192];
    if (at < n) {
        const Call &x = g_originalCalls[at], &y = g_calls[at];
        snprintf(detail, sizeof(detail), "D3D calls (%d, %d): call %d original %s(%x, %x), port %s(%x, %x)",
                 g_originalCallCount, g_callCount, at, CallName(x.what), x.a, x.b, CallName(y.what), y.a, y.b);
    } else {
        const Call &extra = g_originalCallCount > g_callCount ? g_originalCalls[at] : g_calls[at];
        snprintf(detail, sizeof(detail), "D3D calls: original %d, port %d; the first extra (%s): %s(%x, %x)",
                 g_originalCallCount, g_callCount, g_originalCallCount > g_callCount ? "original" : "port",
                 at < kMaxCalls ? CallName(extra.what) : "?", at < kMaxCalls ? extra.a : 0,
                 at < kMaxCalls ? extra.b : 0);
    }
    Differ(what, index, detail);
}

struct ApplyCase {
    GeoPrimState state;
    bool answer;
};

void RunApply(void *context, bool original) {
    ApplyCase *c = static_cast<ApplyCase *>(context);
    c->answer = original ? Orig_Apply(&c->state, 0) : c->state.Apply();
}

ApplyCase g_apply[2];

// One state applied by both sides from the state as it is now; the port's result is left in place
void ApplyBoth(const GeoPrimState *state, const char *what, int index) {
    g_cases++;
    g_apply[0].state = *state;
    g_apply[1].state = *state;
    Take(&g_pre);
    g_callCount = 0;
    bool ok = Guarded(RunApply, &g_apply[0], true);
    Take(&g_afterOriginal);
    memcpy(g_originalCalls, g_calls, sizeof(g_calls));
    g_originalCallCount = g_callCount;
    Put(&g_pre);
    g_callCount = 0;
    ok = Guarded(RunApply, &g_apply[1], false) && ok;
    Take(&g_afterPort);
    if (!ok)
        return;
    CheckU32(what, index, g_apply[0].answer, g_apply[1].answer);   // the answer
    CheckSnapshots(what, index);
    CheckCalls(what, index);
    CheckWords(what, index, "the state", &g_apply[0].state, &g_apply[1].state, sizeof(GeoPrimState));
}

// ---- random states and caches

const uint32_t kShadings[] = { 0, 1, 2, 3, 0xffffffff };
const uint32_t kBytes[] = { 0, 1, 0x80 };
const uint32_t kCullDirections[] = { 0x900, 0x901 };
const uint32_t kCompares[] = { 0x200, 0x201, 0x202, 0x203, 0x204, 0x205, 0x206, 0x207 };
const uint32_t kBlendModes[] = { 0, 1, 2, 3, 4, 5, 6 };
const uint32_t kFactors[] = { 0, 1, 0x302, 0x303, 0x306 };
const uint32_t kOperations[] = { 0x8006, 0x800b };
const uint32_t kTransparencies[] = { 0, 1, 2 };
const uint32_t kFillModes[] = { 0x1b00, 0x1b01, 0x1b02 };
const uint32_t kOffsets[] = { 0x00000000, 0x80000000, 0x3f800000, 0xc0000000, 0x7fc00000 };
const uint32_t kZWrites[] = { 0, 1, 0xffffffff, 2, 0x100 };
const uint32_t kColours[] = { 0, 0xffffffff, 0x80808080 };

#define PICK(pool) Pick(pool, int(sizeof(pool) / sizeof(pool[0])))

uint32_t Maybe(uint32_t value) {
    return (Next() & 15) == 0 ? Next() : value;
}

void RandomState(GeoPrimState *s) {
    RandomObject(s);
    s->primitiveType = Next() % 8;
    s->shading = Maybe(PICK(kShadings));
    s->cullEnable = uint8_t(PICK(kBytes));
    s->cullDirection = PICK(kCullDirections);
    s->depthTestMethod = PICK(kCompares);
    s->alphaBlendMode = PICK(kBlendModes);
    s->blendSource = PICK(kFactors);
    s->blendDestination = PICK(kFactors);
    s->blendOperation = PICK(kOperations);
    if ((Next() & 1) != 0)
        s->SetAlphaBlendMode(s->alphaBlendMode);
    s->alphaTestEnable = uint8_t(PICK(kBytes));
    s->alphaCompareValue = Maybe(Next() & 3);
    s->alphaTestMethod = PICK(kCompares);
    s->textureEnable = uint8_t(PICK(kBytes));
    s->transparencyMethod = Maybe(PICK(kTransparencies));
    s->fillMode = PICK(kFillModes);
    uint32_t bits = (Next() & 7) == 0 ? QuietFloat(Next()) : PICK(kOffsets);
    memcpy(&s->zSlopeScale, &bits, 4);
    bits = (Next() & 7) == 0 ? QuietFloat(Next()) : PICK(kOffsets);
    memcpy(&s->zOffset, &bits, 4);
    s->blendColour = Maybe(PICK(kColours));
    s->zWritesEnable = PICK(kZWrites);
    if (s->zWritesEnable == 0xffffffff && CurrentRenderContext == NULL)
        s->zWritesEnable = 1;
}

// The cache as EndFrame leaves it (every word invalid), and the polygon offsets something else
void InvalidateCache() {
    memset(&ApplyCache, 0xff, sizeof(ApplyCache));
    uint32_t bits = PICK(kOffsets);
    memcpy(&ApplyCacheZSlopeScale, &bits, 4);
    bits = PICK(kOffsets);
    memcpy(&ApplyCacheZOffset, &bits, 4);
}

// A cache built from a random state's fields, so the next states partly hit it
void RandomCache() {
    GeoPrimState s;
    RandomState(&s);
    GeoPrimApplyCache &c = ApplyCache;
    c.shading = s.shading;
    c.cullEnable = s.cullEnable;
    c.cullDirection = s.cullDirection;
    c.depthTestMethod = s.depthTestMethod;
    c.alphaBlendMode = s.alphaBlendMode;
    c.alphaTestEnable = s.alphaTestEnable;
    c.alphaCompareValue = s.alphaCompareValue;
    c.alphaTestMethod = s.alphaTestMethod;
    c.textureEnable = s.textureEnable;
    c.transparencyMethod = s.transparencyMethod;
    c.fillMode = s.fillMode;
    c.blendOperation = s.blendOperation;
    c.blendSource = s.blendSource;
    c.blendDestination = s.blendDestination;
    c.blendColour = s.blendColour;
    c.zWritesEnable = s.zWritesEnable;
    ApplyCacheZSlopeScale = s.zSlopeScale;
    ApplyCacheZOffset = s.zOffset;
    Shadows.zWritesEnable = uint8_t(PICK(kBytes));
}

// Textures bound or not; the fakes never look at them
void RandomStageTextures() {
    for (int stage = 0; stage < 4; stage++)
        StageTexture[stage] = (Next() & 1) != 0 ? (D3DPixelContainer *)(uintptr_t)(0x7e570000 + stage * 0x10) : NULL;
}

void TestApply() {
    const int kSequences = 96, kSteps = 24;
    Take(&g_base);
    HooksInstall();
    for (int sequence = 0; sequence < kSequences; sequence++) {
        Put(&g_base);
        switch (sequence % 3) {
        case 1:
            InvalidateCache();
            break;
        case 2:
            RandomCache();
            break;
        }
        RandomStageTextures();
        if (sequence % 8 == 0)
            ApplyBoth(&FontState, "Apply (font state)", sequence * kSteps);
        for (int step = 0; step < kSteps; step++) {
            GeoPrimState state;
            RandomState(&state);
            if ((Next() & 7) == 0)
                RandomStageTextures();
            ApplyBoth(&state, "Apply", sequence * kSteps + step);
        }
    }
    HooksRemove();
    Put(&g_base);
}

// ---- the states the game draws with, collected at opcode 16

const int kMaxLive = 2048, kLiveTableSize = 4096;
GeoPrimState g_live[kMaxLive];
int16_t g_liveTable[kLiveTableSize];          // index + 1 into g_live, open addressing
int g_liveCount = 0;
int g_liveReported = 0;
int g_liveDiffer = 0;
DWORD g_lastReport = 0;
void (*g_handler16)() = NULL;
bool g_collecting = false;

uint32_t Hash(const GeoPrimState *s) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(s);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(*s); i++)
        h = (h ^ bytes[i]) * 16777619u;
    return h;
}

// True the first time a state's bytes are seen
bool Remember(const GeoPrimState *s) {
    uint32_t slot = Hash(s) % kLiveTableSize;
    while (g_liveTable[slot] != 0) {
        if (memcmp(&g_live[g_liveTable[slot] - 1], s, sizeof(*s)) == 0)
            return false;
        slot = (slot + 1) % kLiveTableSize;
    }
    g_live[g_liveCount] = *s;
    g_liveTable[slot] = int16_t(++g_liveCount);
    return true;
}

void Report(bool last) {
    printf("[geoprim] live states: %d seen, %d differ - all tests so far: %d cases, %d checks, %d differ%s%s\n",
           g_liveCount, g_liveDiffer, g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "",
           last ? " - the collector's limit, stopped" : "");
    fflush(stdout);
    g_liveReported = g_liveCount;
    g_lastReport = GetTickCount();
}

void TestLive(const GeoPrimState *state) {
    int before = g_differ;
    int index = g_liveCount - 1;
    Take(&g_base);
    HooksInstall();
    ApplyBoth(state, "live Apply (live cache)", index);
    Put(&g_base);
    InvalidateCache();
    ApplyBoth(state, "live Apply (invalidated cache)", index);
    for (int i = 0; i < 4 && g_liveCount > 1; i++) {
        Put(&g_base);
        ApplyBoth(&g_live[Next() % (g_liveCount - 1)], "live Apply (earlier state)", index);
        ApplyBoth(state, "live Apply (after an earlier state)", index);
    }
    HooksRemove();
    Put(&g_base);
    if (g_differ != before)
        g_liveDiffer++;
}

void SetSlot16(void (*handler)()) {
    DWORD old;
    if (!VirtualProtect(&OpcodeSlot16, 4, PAGE_EXECUTE_READWRITE, &old))
        return;
    OpcodeSlot16 = handler;
    VirtualProtect(&OpcodeSlot16, 4, old, &old);
}

void CollectOpcode16() {
    const GeoPrimState *state = reinterpret_cast<const GeoPrimState *>(
        InterpParam->data + InterpPacket->variationStride * CurrentVariation);
    if (g_collecting && Remember(state)) {
        TestLive(state);
        if (g_liveCount == kMaxLive) {
            g_collecting = false;
            SetSlot16(g_handler16);
            Report(true);
        }
    }
    if (g_liveReported != g_liveCount && GetTickCount() - g_lastReport >= 1000)
        Report(false);
    g_handler16();
}

void StartCollecting() {
    void (*handler)() = OpcodeSlot16;
    uint32_t at = (uint32_t)(uintptr_t)handler;
    if (at != kOpcode16) {
        printf("[geoprim] opcode 16's handler is %08x, not %08x - the live states not collected\n", at, kOpcode16);
        return;
    }
    g_handler16 = handler;
    g_collecting = true;
    g_lastReport = GetTickCount();
    SetSlot16(CollectOpcode16);
}

}  // namespace

void GeoPrimShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GEOPRIMSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);
    TestObjects();
    TestApply();
    ResetFpu();
    printf("[geoprim] setters, getters, constructors, Apply sequences vs originals: %d cases, %d checks, %d differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[geoprim]   %d calls faulted\n", g_faults);
    fflush(stdout);
    StartCollecting();
}
