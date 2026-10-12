#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameAudioShadowD.h"
#include "FpControl.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <string>
#include <vector>

#include "../audio/Mix.h"
#include "../audio/Sound.h"
#include "../audio/SoundManager.h"
#include "../audio/Stream.h"
#include "../data/DebugItem.h"
#include "../data/StdLocale.h"
#include "../eagl/EaglFont.h"
#include "../engine/GameLoop.h"
#include "../game/DebugAudio.h"
#include "../game/Simulation.h"
#include "../render/Renderer.h"
#include "../world/Grid.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of package GA_D, run once from the first simulation tick when NIGHTFIRE_GAMEAUDIOSHADOWD is set.
// Each case runs the ORIGINAL (its entry, and those of the package's originals it calls, swapped back) and then the
// PORT on the same state, put back between the two, and compares:
//
//   - the locale helpers: a facet's _Incref and _Decref on counts round 0 and the full count, ~locale (the facet's
//     deleting destructor a recording fake), ios_base::getloc, the facet's destructors (flags 0);
//   - the targets' list: construction, push_backs (_Incsize), erase over random ranges, destruction - the values in
//     order, the size and the answers as node positions;
//   - the debug items' methods on stand-in items with a recording vtable: Increment, Decrement and Activate (the
//     calls they make), SubMenuNamespace, Name, IsHidden, IsIndexed, GetIndexer, the destructors (flags 0);
//   - DAudio::UpdateTargets over the live sounds, each sound's range and last volumes perturbed: the list built;
//   - DAudio::DrawTargets, InfoDraw, MixerDraw and Draw with every drawing call (Draw::DrawBox, FONT_drawtextfa,
//     FONT_getrectx, GHud::dbDrawTarget), the streams' latency and the four pauses replaced by recording fakes on
//     both sides: the calls and their arguments, the font's colour, the mixer's selection and scroll, the mixes
//     (perturbed: group, level, scale; the editing mode on and off, the selection and scroll random).
//
// A mutation the test sees: MixerDraw without its clamp of the alpha to 0xff (the levels are perturbed up to 5, so
// a swatch's DrawBox colour differs), or UpdateTargets without its test of the last volumes (perturbed sounds no
// view hears get listed).
//
// One summary line: [gameaudioshadowd] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

using namespace GameStd;

namespace {

long g_cases, g_checks, g_differ, g_faults;
int g_reported;
unsigned g_x87, g_sse;

void Check(bool same, const char *what, long index) {
    g_checks++;
    if (same)
        return;
    g_differ++;
    if (g_reported < 10) {
        g_reported++;
        printf("[gameaudioshadowd] DIFF %s, case %ld\n", what, index);
        fflush(stdout);
    }
}

uint32_t g_seed = 0x2545f491;
uint32_t Random() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}
uint32_t Random(uint32_t n) {
    return n == 0 ? 0 : Random() % n;
}
float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Random() & 0xffff) * (1.0f / 65536.0f);
}

uint32_t Bits(float f) {
    return std::bit_cast<uint32_t>(f);
}

void Append(std::string &log, const char *format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    log += line;
}

template <class F>
bool Guarded(const F &call) {
#ifdef _MSC_VER
    __try {
        call();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        FpControlSetX87(g_x87);
        FpControlSetSse(g_sse);
        return false;
    }
#else
    call();
#endif
    return true;
}

struct Originals {
    const unsigned *entries;
    int count;
    Originals(const unsigned *list, int n) : entries(list), count(n) {
        for (int i = 0; i < count; i++)
            XbeOriginal_Restore(entries[i], true);
    }
    ~Originals() {
        for (int i = 0; i < count; i++)
            XbeOriginal_Restore(entries[i], false);
    }
};

// Runs `run(original, log)` for the original (inside the window) and then the port, `reset` before each.
template <class Reset, class Run>
void Both(const unsigned *entries, int count, const Reset &reset, const Run &run, std::string &original,
          std::string &port) {
    reset();
    {
        Originals window(entries, count);
        original.clear();
        if (!Guarded([&] { run(true, original); }))
            original += "<fault>";
    }
    reset();
    port.clear();
    if (!Guarded([&] { run(false, port); }))
        port += "<fault>";
}

#define ENTRIES(list) list, int(sizeof(list) / sizeof(list[0]))

// ---- hooks: a jump over an entry, and over our port's entry behind it

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[32];       // nine fakes, each over the entry and over our port behind it
int g_hookCount;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0]))) {
        printf("[gameaudioshadowd]   no room for a hook at 0x%08x\n", at);
        return;
    }
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
        h.on = false;
    }
    g_hookCount = 0;
}

std::string *g_calls;

// ---------------------------------------------------------------------------------------------------------------
// The locale helpers

#define Orig_Incref ((void (__fastcall *)(Facet *, int))0x000378c0)
#define Orig_Decref ((Facet *(__fastcall *)(Facet *, int))0x000378f0)
#define Orig_FacetDestruct ((void (__fastcall *)(Facet *, int))0x00037930)
#define Orig_LocaleDestruct ((void (__fastcall *)(Locale *, int))0x00037940)
#define Orig_FacetDelete ((Facet *(__fastcall *)(Facet *, int, unsigned))0x00037960)
#define Orig_GetLoc ((Locale *(__fastcall *)(IosBase *, int, Locale *))0x00037980)

const unsigned kLocaleEntries[] = { 0x000378c0, 0x000378f0, 0x00037930, 0x00037940, 0x00037960, 0x00037980 };

Facet *g_facet;

Facet *__fastcall FakeFacetDelete(Facet *self, int, unsigned flags) {
    if (g_calls != NULL)
        Append(*g_calls, "delete(%d,%u) ", self == g_facet, flags);
    return self;
}
const void *g_fakeFacetVtable[1] = { (const void *)&FakeFacetDelete };

void TestLocale() {
    static const uint32_t kCounts[] = { 0, 1, 2, 7, 0x7fffffff, 0xfffffffe, 0xffffffff };
    Facet facet;
    g_facet = &facet;
    for (int i = 0; i < 40; i++) {
        uint32_t refs = i < 7 ? kCounts[i] : Random();
        std::string original, port;
        g_cases++;
        auto reset = [&] {
            facet.vtable = (const void *const *)g_fakeFacetVtable;
            facet.refs = refs;
        };
        Both(ENTRIES(kLocaleEntries), reset, [&](bool orig, std::string &log) {
            g_calls = &log;
            if (orig) Orig_Incref(&facet, 0); else facet.Incref();
            Append(log, "inc %08x ", facet.refs);
            facet.refs = refs;
            Facet *dead = orig ? Orig_Decref(&facet, 0) : facet.Decref();
            Append(log, "dec %08x %d ", facet.refs, dead == &facet ? 1 : dead == NULL ? 0 : 2);
            facet.refs = refs;
            Locale locale = { &facet };
            if (orig) Orig_LocaleDestruct(&locale, 0); else locale.Destruct();
            Append(log, "~locale %08x ", facet.refs);
            Locale empty = { NULL };
            if (orig) Orig_LocaleDestruct(&empty, 0); else empty.Destruct();
            facet.refs = refs;
            IosBase ios;
            memset(&ios, 0, sizeof(ios));
            Locale streamLocale = { &facet };
            ios.locale = &streamLocale;
            Locale copy = { NULL };
            Locale *answer = orig ? Orig_GetLoc(&ios, 0, &copy) : ios.GetLoc(&copy);
            Append(log, "getloc %d %d %08x ", answer == &copy, copy.impl == &facet, facet.refs);
            if (orig) Orig_FacetDestruct(&facet, 0); else facet.Destruct();
            Append(log, "dtor %p ", (const void *)facet.vtable);
            facet.vtable = (const void *const *)g_fakeFacetVtable;
            Facet *deleted = orig ? Orig_FacetDelete(&facet, 0, 0) : facet.Delete(0);
            Append(log, "delete %d %p", deleted == &facet, (const void *)facet.vtable);
            g_calls = NULL;
        }, original, port);
        Check(original == port, "locale helpers", i);
    }
    g_facet = NULL;
}

// ---------------------------------------------------------------------------------------------------------------
// The targets' list

#define Orig_ListConstruct ((DAudioTargetList *(__fastcall *)(DAudioTargetList *, int))0x00037640)
#define Orig_ListDestruct ((void (__fastcall *)(DAudioTargetList *, int))0x00037660)
#define Orig_ListErase ((DAudioTargetNode **(__fastcall *)(DAudioTargetList *, int, DAudioTargetNode **, DAudioTargetNode *, DAudioTargetNode *))0x00037160)
#define Orig_ListIncSize ((void (__fastcall *)(DAudioTargetList *, int, uint32_t))0x000376a0)

const unsigned kListEntries[] = { 0x00037640, 0x00037660, 0x00037160, 0x000376a0 };

int Position(const DAudioTargetList &list, const DAudioTargetNode *node) {
    if (node == list.head)
        return -1;
    int i = 0;
    for (const DAudioTargetNode *at = list.head->next; at != list.head; at = at->next, i++)
        if (at == node)
            return i;
    return -2;
}

void AppendList(std::string &log, const DAudioTargetList &list) {
    Append(log, "[%u:", list.size);
    for (const DAudioTargetNode *node = list.head->next; node != list.head; node = node->next)
        Append(log, " %08x/%08x/%08x/%s", Bits(node->value.screen.x), Bits(node->value.screen.y),
               Bits(node->value.distance), node->value.name);
    log += "] ";
}

DAudioTarget RandomTarget() {
    DAudioTarget target;
    target.screen.x = Uniform(-50.0f, 700.0f);
    target.screen.y = Uniform(-50.0f, 500.0f);
    target.distance = Uniform(0.0f, 300.0f);
    snprintf(target.name, sizeof(target.name), "snd%u", Random(1000));
    return target;
}

void TestList() {
    for (int i = 0; i < 40; i++) {
        int count = int(Random(9));
        std::vector<DAudioTarget> targets;
        for (int n = 0; n < count; n++)
            targets.push_back(RandomTarget());
        int first = int(Random(count + 1));
        int last = first + int(Random(count - first + 1));
        std::string original, port;
        g_cases++;
        Both(ENTRIES(kListEntries), [] {}, [&](bool orig, std::string &log) {
            DAudioTargetList list;
            memset(&list, 0, sizeof(list));
            DAudioTargetList *self = orig ? Orig_ListConstruct(&list, 0) : list.Construct();
            Append(log, "ctor %d %u ", self == &list, list.size);
            for (const DAudioTarget &target : targets) {
                DAudioTargetNode *head = list.head;
                DAudioTargetNode *node = (DAudioTargetNode *)reinterpret_cast<WGridMoverList *>(&list)->BuyNode(
                    (WGridMoverNode *)head, (WGridMoverNode *)head->prev, (const WGridMover *)&target);
                if (orig) Orig_ListIncSize(&list, 0, 1); else list.IncreaseSize(1);
                head->prev = node;
                node->prev->next = node;
            }
            AppendList(log, list);
            DAudioTargetNode *from = list.head->next;
            for (int n = 0; n < first; n++)
                from = from->next;
            DAudioTargetNode *to = from;
            for (int n = first; n < last; n++)
                to = to->next;
            DAudioTargetNode *result = NULL;
            DAudioTargetNode **answer = orig ? Orig_ListErase(&list, 0, &result, from, to) : list.Erase(&result, from, to);
            Append(log, "erase %d %d ", answer == &result, Position(list, result));
            AppendList(log, list);
            if (orig) Orig_ListDestruct(&list, 0); else list.Destruct();
            Append(log, "dtor %d %u", list.head == NULL, list.size);
        }, original, port);
        Check(original == port, "targets' list", i);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The debug items

const unsigned kItemEntries[] = { 0x000379e0, 0x00037a90, 0x000379f0, 0x00037a00, 0x00037a20, 0x00037a30,
                                  0x00037a60, 0x00037a80, 0x00037ac0, 0x00037ab0 };

#define Orig_ItemDestruct ((void (__fastcall *)(DebugData *, int))0x000379e0)
#define Orig_ItemDelete ((DebugData *(__fastcall *)(DebugData *, int, unsigned))0x00037a90)
#define Orig_Increment ((void (__fastcall *)(DebugData *, int))0x000379f0)
#define Orig_Decrement ((void (__fastcall *)(DebugData *, int))0x00037a00)
#define Orig_Activate ((void (__fastcall *)(DebugData *, int))0x00037a20)
#define Orig_SubMenuNamespace ((CARP::CountedString *(__fastcall *)(DebugData *, int, CARP::CountedString *))0x00037a30)
#define Orig_Name ((char *(__fastcall *)(DebugData *, int))0x00037a60)
#define Orig_IsHidden ((int (__fastcall *)(DebugData *, int))0x00037a80)
#define Orig_IsIndexed ((int (__fastcall *)(DebugData *, int))0x00037ac0)
#define Orig_GetIndexer ((DebugData *(__fastcall *)(DebugData *, int))0x00037ab0)

DebugData *g_item;

void __fastcall FakeIncrease(DebugData *self, int, float scale) {
    if (g_calls != NULL)
        Append(*g_calls, "increase(%d,%08x) ", self == g_item, Bits(scale));
}
void __fastcall FakeDecrease(DebugData *self, int, float scale) {
    if (g_calls != NULL)
        Append(*g_calls, "decrease(%d,%08x) ", self == g_item, Bits(scale));
}
void __fastcall FakeIncrement(DebugData *self, int) {
    if (g_calls != NULL)
        Append(*g_calls, "increment(%d) ", self == g_item);
}
const void *g_fakeItemVtable[22] = {
    NULL, (const void *)&FakeIncrease, (const void *)&FakeDecrease, NULL, NULL, (const void *)&FakeIncrement,
};

void TestItems() {
    DebugData item, other;
    g_item = &item;
    for (int i = 0; i < 30; i++) {
        char name[48];
        int length = int(Random(40));
        for (int n = 0; n < length; n++)
            name[n] = char('a' + Random(26));
        name[length] = 0;
        int nameLength = int(Random(length + 1));
        uint32_t flags = Random();
        bool indexed = Random(2) != 0;
        std::string original, port;
        g_cases++;
        auto reset = [&] {
            memset(&item, 0, sizeof(item));
            item.vtable = (const void *const *)g_fakeItemVtable;
            item.name.text = name;
            item.name.length = nameLength;
            item.flags = flags;
            item.indexer = indexed ? &other : NULL;
        };
        Both(ENTRIES(kItemEntries), reset, [&](bool orig, std::string &log) {
            DebugData *self = &item;
            g_calls = &log;
            if (orig) Orig_Increment(&item, 0); else self->Increment();
            if (orig) Orig_Decrement(&item, 0); else self->Decrement();
            if (orig) Orig_Activate(&item, 0); else self->Activate();
            CARP::CountedString text = { name, 99 };
            CARP::CountedString *answer = orig ? Orig_SubMenuNamespace(&item, 0, &text) : self->SubMenuNamespace(&text);
            Append(log, "ns %d '%s' %d ", answer == &text, text.text, text.length);
            char *itemName = orig ? Orig_Name(&item, 0) : self->Name();
            Append(log, "name '%s' ", itemName);
            Append(log, "hidden %d ", orig ? Orig_IsHidden(&item, 0) : self->IsHidden());
            Append(log, "indexed %d ", orig ? Orig_IsIndexed(&item, 0) : self->IsIndexed());
            DebugData *indexer = orig ? Orig_GetIndexer(&item, 0) : self->GetIndexer();
            Append(log, "indexer %d ", indexer == &other ? 1 : indexer == NULL ? 0 : 2);
            if (orig) Orig_ItemDestruct(&item, 0); else self->Destruct();
            Append(log, "dtor %p ", (const void *)item.vtable);
            item.vtable = (const void *const *)g_fakeItemVtable;
            DebugData *deleted = orig ? Orig_ItemDelete(&item, 0, 0) : (DebugData *)self->Delete(0);
            Append(log, "delete %d %p", deleted == &item, (const void *)item.vtable);
            g_calls = NULL;
        }, original, port);
        Check(original == port, "debug items", i);
    }
    g_item = NULL;
}

// ---------------------------------------------------------------------------------------------------------------
// DAudio

#define Orig_UpdateTargets ((void (*)())0x00037750)
#define Orig_DrawTargets ((void (*)())0x00036c90)
#define Orig_MixerDraw ((void (*)())0x00036d10)
#define Orig_InfoDraw ((void (*)())0x000371b0)
#define Orig_Draw ((void (*)())0x00037620)

#define GameTargets (*(DAudioTargetList *)0x001e2278)   // the game's copy of DAudioTargets
#define GameMixerSelected I32_AT(0x001e2268)        // the game's copies of our globals
#define GameMixerScrollTop I32_AT(0x001e226c)
#define GameMixerEditing BOOL8_AT(0x001e2270)
#define GameMixerVisible BOOL8_AT(0x001e4760)
#define GameInfoVisible BOOL8_AT(0x001e4761)       // the game's copy of gDAudio
#define MixesList (*(MixList *)0x00243954)

const unsigned kUpdateEntries[] = { 0x00037750, 0x00037160, 0x000376a0 };
const unsigned kDrawTargetsEntries[] = { 0x00036c90 };
const unsigned kMixerEntries[] = { 0x00036d10 };
const unsigned kInfoEntries[] = { 0x000371b0 };
const unsigned kDrawEntries[] = { 0x00037620 };

// The list each side builds and draws: the original's is the game's copy, made for the test
DAudioTargetList &Targets(bool orig) {
    return orig ? GameTargets : DAudioTargets;
}

void ClearTargets() {
    DAudioTargetNode *ignored;
    for (int side = 0; side < 2; side++)
        Targets(side != 0).Erase(&ignored, Targets(side != 0).Begin(), Targets(side != 0).head);
}

struct SoundState {
    ABaseSound *sound;
    float maxDistance;
    float lastVolumes[kSoundViewCount];
};

std::vector<SoundState> LiveSounds() {
    std::vector<SoundState> sounds;
    ASoundList *list = fgSoundList;
    if (list == NULL || list->head == NULL)
        return sounds;
    for (PointerListNode *node = list->Begin(); node != list->head; node = node->next) {
        SoundState state;
        state.sound = (ABaseSound *)node->value;
        state.maxDistance = state.sound->maxDistance;
        memcpy(state.lastVolumes, state.sound->lastVolumes, sizeof(state.lastVolumes));
        sounds.push_back(state);
    }
    return sounds;
}

void TestUpdateTargets() {
    if (DAudioTargets.head == NULL)
        return;
    std::vector<SoundState> live = LiveSounds();
    for (int i = 0; i < 24; i++) {
        std::vector<SoundState> cased = live;
        if (i > 0) {
            for (SoundState &state : cased) {
                switch (Random(6)) {
                case 0: state.maxDistance = 0.0f; break;
                case 1: state.maxDistance = -1.0f; break;
                case 2: state.maxDistance = NAN; break;
                case 3: state.maxDistance = 1.0e6f; break;
                default: state.maxDistance *= Uniform(0.0f, 2.0f); break;
                }
                for (float &volume : state.lastVolumes) {
                    switch (Random(4)) {
                    case 0: volume = 0.0f; break;
                    case 1: volume = -0.5f; break;
                    case 2: volume = Uniform(0.0f, 1.0f); break;
                    default: break;
                    }
                }
            }
        }
        auto reset = [&] {
            ClearTargets();
            for (const SoundState &state : cased) {
                state.sound->maxDistance = state.maxDistance;
                memcpy(state.sound->lastVolumes, state.lastVolumes, sizeof(state.lastVolumes));
            }
        };
        std::string original, port;
        g_cases++;
        Both(ENTRIES(kUpdateEntries), reset, [&](bool orig, std::string &log) {
            if (orig) Orig_UpdateTargets(); else DAudio::UpdateTargets();
            AppendList(log, Targets(orig));
        }, original, port);
        Check(original == port, "DAudio::UpdateTargets", i);
    }
    for (const SoundState &state : live) {
        state.sound->maxDistance = state.maxDistance;
        memcpy(state.sound->lastVolumes, state.lastVolumes, sizeof(state.lastVolumes));
    }
    ClearTargets();
}

// ---- the drawing fakes

void FakeDrawBox(float x, float y, float width, float height, uint32_t colour) {
    if (g_calls != NULL)
        Append(*g_calls, "box %08x %08x %08x %08x %08x\n", Bits(x), Bits(y), Bits(width), Bits(height), colour);
}

void FakeDrawText(const uint8_t *font, float x, float y, const char *format, ...) {
    if (g_calls == NULL)
        return;
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    Append(*g_calls, "text %d %08x %08x '%s' '%s'\n", font == (const uint8_t *)fgRenderer->debugFont, Bits(x),
           Bits(y), format, text);
}

void FakeGetRect(const uint8_t *font, const uint8_t *text, float *x, float *y, float *width, float *height) {
    if (g_calls != NULL)
        Append(*g_calls, "rect %d '%s' %d %d %d\n", font == (const uint8_t *)fgRenderer->debugFont,
               (const char *)text, x != NULL, y != NULL, height != NULL);
    if (width != NULL)
        *width = float(strlen((const char *)text)) * 7.3f;
}

void __fastcall FakeDrawTarget(void *hud, int, const ScreenPos *screen, const char *text, float size,
                               uint32_t colour, const char *label) {
    (void)hud;
    if (g_calls != NULL)
        Append(*g_calls, "target %08x %08x '%s' %08x %08x '%s'\n", Bits(screen->x), Bits(screen->y), text,
               Bits(size), colour, label);
}

float g_latency[2];
AStream *g_speech;

double __fastcall FakeLatency(AStream *stream, int) {
    if (g_calls != NULL)
        Append(*g_calls, "latency\n");
    return g_latency[stream == g_speech];
}

void __fastcall FakePauseSim(void *, int) {
    if (g_calls != NULL)
        Append(*g_calls, "pausesim\n");
}
void __fastcall FakeMissionStop(void *, int) {
    if (g_calls != NULL)
        Append(*g_calls, "missionstop\n");
}
void FakeFeedbackPause() {
    if (g_calls != NULL)
        Append(*g_calls, "feedbackpause\n");
}
void FakeSoundPause() {
    if (g_calls != NULL)
        Append(*g_calls, "soundpause\n");
}
void FakeInfoDraw() {
    if (g_calls != NULL)
        Append(*g_calls, "infodraw\n");
}
void FakeMixerDraw() {
    if (g_calls != NULL)
        Append(*g_calls, "mixerdraw\n");
}

void InstallDrawingFakes() {
    HookInstall(0x000760e0, (const void *)&FakeDrawBox);
    HookInstall(0x001073a0, (const void *)&FakeDrawText);
    HookInstall(0x00107b30, (const void *)&FakeGetRect);
    HookInstall(0x000da8e0, (const void *)&FakeDrawTarget);
    HookInstall(0x00121fc0, (const void *)&FakeLatency);
    HookInstall(0x000b25e0, (const void *)&FakePauseSim);
    HookInstall(0x000b63a0, (const void *)&FakeMissionStop);
    HookInstall(0x0004fa00, (const void *)&FakeFeedbackPause);
    HookInstall(0x00121070, (const void *)&FakeSoundPause);
}

// Every mix the pages can reach: the listed ones, the effect modes', and their masters
struct MixCopy {
    AMix *mix;
    uint8_t bytes[sizeof(AMix)];
};

std::vector<MixCopy> SnapshotMixes() {
    std::vector<MixCopy> copies;
    auto add = [&](AMix *mix) {
        for (; mix != NULL; mix = mix->master) {
            bool known = false;
            for (const MixCopy &copy : copies)
                known = known || copy.mix == mix;
            if (known)
                return;
            MixCopy copy;
            copy.mix = mix;
            memcpy(copy.bytes, mix, sizeof(AMix));
            copies.push_back(copy);
        }
    };
    if (MixesList.head != NULL)
        for (PointerListNode *node = MixesList.Begin(); node != MixesList.head; node = node->next)
            add((AMix *)node->value);
    for (int i = 0; i < AFX::kModeCount; i++)
        add(FxMixes[i]);
    return copies;
}

void RestoreMixes(const std::vector<MixCopy> &copies) {
    for (const MixCopy &copy : copies)
        memcpy(copy.mix, copy.bytes, sizeof(AMix));
}

void AppendMixes(std::string &log, const std::vector<MixCopy> &copies) {
    for (const MixCopy &copy : copies)
        Append(log, "mix %08x %08x %d\n", Bits(copy.mix->volume), Bits(copy.mix->unknown28), copy.mix->unknown20);
}

void TestDrawing() {
    std::vector<MixCopy> mixes = SnapshotMixes();
    FNTXFont *font = fgRenderer->debugFont;
    uint32_t fontColour = font->colour;
    int32_t simState = fgSimulation.simState;
    g_speech = AStream::Get("Speech");

    // DrawTargets over a list of random targets
    for (int i = 0; i < 6; i++) {
        ClearTargets();
        int count = int(Random(6));
        for (int n = 0; n < count; n++) {
            DAudioTarget target = RandomTarget();
            for (int side = 0; side < 2; side++) {
                DAudioTargetList &list = Targets(side != 0);
                DAudioTargetNode *head = list.head;
                DAudioTargetNode *node = (DAudioTargetNode *)reinterpret_cast<WGridMoverList *>(&list)->BuyNode(
                    (WGridMoverNode *)head, (WGridMoverNode *)head->prev, (const WGridMover *)&target);
                list.size++;
                head->prev = node;
                node->prev->next = node;
            }
        }
        std::string original, port;
        g_cases++;
        Both(ENTRIES(kDrawTargetsEntries), [] {}, [&](bool orig, std::string &log) {
            g_calls = &log;
            if (orig) Orig_DrawTargets(); else DAudio::DrawTargets();
            g_calls = NULL;
        }, original, port);
        Check(original == port, "DAudio::DrawTargets", i);
    }
    ClearTargets();

    // InfoDraw, the latency random
    for (int i = 0; i < 6; i++) {
        g_latency[0] = Uniform(-1.0f, 3.0f);
        g_latency[1] = Uniform(-1.0f, 3.0f);
        uint32_t startColour = Random();
        std::string original, port;
        g_cases++;
        Both(ENTRIES(kInfoEntries), [&] {
            RestoreMixes(mixes);
            font->colour = startColour;
        }, [&](bool orig, std::string &log) {
            g_calls = &log;
            if (orig) Orig_InfoDraw(); else DAudio::InfoDraw();
            g_calls = NULL;
            Append(log, "colour %08x\n", font->colour);
            AppendMixes(log, mixes);
        }, original, port);
        Check(original == port, "DAudio::InfoDraw", i);
    }

    // MixerDraw over perturbed mixes
    for (int i = 0; i < 30; i++) {
        std::vector<MixCopy> cased = mixes;
        for (MixCopy &copy : cased) {
            AMix *mix = (AMix *)copy.bytes;
            mix->unknown20 = int32_t(Random(9)) - 2;          // -2..6
            switch (Random(4)) {
            case 0: mix->unknown28 = 0.0f; break;
            case 1: mix->unknown28 = -Uniform(0.0f, 1.0f); break;
            default: mix->unknown28 = Uniform(0.0f, 5.0f); break;
            }
            mix->unknown2c = Random(3) == 0 ? -Uniform(0.0f, 1.0f) : Uniform(0.0f, 2.0f);
        }
        bool editing = Random(2) != 0;
        int32_t selected = int32_t(Random(40)) - 4;
        int32_t scrollTop = int32_t(Random(40)) - 4;
        int32_t state = Random(3) == 0 ? 3 : 0;
        std::string original, port;
        g_cases++;
        Both(ENTRIES(kMixerEntries), [&] {
            RestoreMixes(cased);
            font->colour = fontColour;
            fgSimulation.simState = state;
            GameMixerSelected = DAudioMixerSelected = selected;
            GameMixerScrollTop = DAudioMixerScrollTop = scrollTop;
            GameMixerEditing = DAudioMixerEditing = editing;
        }, [&](bool orig, std::string &log) {
            g_calls = &log;
            if (orig) Orig_MixerDraw(); else DAudio::MixerDraw();
            g_calls = NULL;
            Append(log, "colour %08x selected %d scroll %d\n", font->colour,
                   orig ? GameMixerSelected : DAudioMixerSelected, orig ? GameMixerScrollTop : DAudioMixerScrollTop);
            AppendMixes(log, mixes);
        }, original, port);
        Check(original == port, "DAudio::MixerDraw", i);
    }

    RestoreMixes(mixes);
    font->colour = fontColour;
    fgSimulation.simState = simState;
    GameMixerSelected = DAudioMixerSelected = 0;
    GameMixerScrollTop = DAudioMixerScrollTop = 0;
    GameMixerEditing = DAudioMixerEditing = false;
}

void TestDraw() {
    bool info = gDAudio;
    HookInstall(0x000371b0, (const void *)&FakeInfoDraw);
    HookInstall(0x00036d10, (const void *)&FakeMixerDraw);
    for (int i = 0; i < 4; i++) {
        std::string original, port;
        g_cases++;
        Both(ENTRIES(kDrawEntries), [&] {
            GameInfoVisible = gDAudio = (i & 1) != 0;
            GameMixerVisible = DAudioMixerVisible = (i & 2) != 0;
        }, [&](bool orig, std::string &log) {
            g_calls = &log;
            if (orig) Orig_Draw(); else DAudio::Draw();
            g_calls = NULL;
        }, original, port);
        Check(original == port, "DAudio::Draw", i);
    }
    HooksRemove();
    GameInfoVisible = gDAudio = info;
    GameMixerVisible = DAudioMixerVisible = false;
}

} // namespace

void GameAudioShadowD_Run(void) {
    const char *env = getenv("NIGHTFIRE_GAMEAUDIOSHADOWD");
    if (env == NULL || atoi(env) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);
    TestLocale();
    TestList();
    TestItems();
    GameTargets.Construct();        // the static initialisers construct only ours
    TestUpdateTargets();
    TestDraw();
    InstallDrawingFakes();
    TestDrawing();
    HooksRemove();
    GameTargets.Destruct();
    printf("[gameaudioshadowd] locale helpers, targets' list, debug items, DAudio: %ld cases, %ld checks, %ld "
           "differ%s\n", g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    fflush(stdout);
}
