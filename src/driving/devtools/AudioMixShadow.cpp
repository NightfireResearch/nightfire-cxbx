#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "AudioMixShadow.h"
#include "FpControl.h"

#include <windows.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <string>
#include <vector>

#include "../audio/Fader.h"
#include "../audio/Mix.h"
#include "../audio/Sound.h"             // ABaseSound::GetMixedVolume
#include "../engine/UMemory.hpp"
#include "../world/SoundMap.h"          // RefCounterMapBuyHead
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of AMix, URefCounter<AMix>'s and URefCounter<AFader>'s trees, AFX, AListener and the parts of AFader
// that do not drive a stream, run once from the first simulation tick when NIGHTFIRE_AUDIOMIXSHADOW is set (the
// sound manager has made its mixes, faders and streams by then). Each case runs the ORIGINAL (its entry and those
// of the originals it calls swapped back) and then the PORT on the same state, put back between the two, and
// compares:
//
//   - mixes: private chains of up to three mixes (masters), random volumes, targets, steps, presets and
//     transitions, put through random sequences of SetTransition (volumes outside 0..1, step counts below 1),
//     Revert, GetVolume, GetPresetVolume and a sound's GetMixedVolume: the chain's bytes and every answer's bits;
//   - Reset over the live mixes Load listed, the live presets perturbed: every live mix's bytes;
//   - the registry: Add, Get, SetMaster, ClearMaster and Remove on names of the test's own (made and freed
//     again), each mix's bytes (its master as a name) and reference counts after each step;
//   - the trees: the same random inserts (names differing only in case among them), erases, range erases (whole
//     and partial) and iterator decrements on a private tree of each kind, then its destructor: the answers, the
//     size, and the tree's shape and colours;
//   - AFX: Update with random requests, current modes, bus settings, levels, frame counts and the modes' mixes
//     perturbed; SetMode, GetCurrentModeName, IsAllVoices, Pause and Resume. The sound library's SNDfxmasterlevel
//     and SNDfxinitbus are replaced by recording fakes (no voice is touched): AFX's state, the live mixes (each
//     GetVolume advances a transition) and the calls are compared;
//   - AListener's two constructors on random cameras, over a patterned buffer;
//   - AFader: Priv::SetSecondary, both SetSecondary, SetFilter and Call911 on copies of a fader's state with random
//     flags and names; Get on the live names; Create (on a stand-in stream) and Remove on a name of the test's own.
//
// AFader::Priv::Event and Update, AMix::Load and Clear, and AFX::Init/Shutdown drive live streams and the registry:
// they are checked in game.
//
// A mutation the test sees: SetTransition without its clamp of the step count to one (the step of a count of 0 or
// less), or AFX's fade-in rate 0.3 changed (the level and SNDfxmasterlevel's argument).
//
// One summary line: [audiomixshadow] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

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
        printf("[audiomixshadow] DIFF %s, case %ld\n", what, index);
        fflush(stdout);
    }
}

uint32_t g_seed = 0x3c6ef372;
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
uint64_t Bits(double d) {
    return std::bit_cast<uint64_t>(d);
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

// The originals' entries swapped back for as long as it lives.
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

// Runs `run(original, log)` for the original (inside the window) and then the port, with `reset` before each,
// into the two logs.
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

void AppendBytes(std::string &log, const void *data, size_t size) {
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; i++)
        Append(log, "%02x", bytes[i]);
    log += '\n';
}

// ---- the registry, read

RefCounterNode *Next(RefCounterNode *node) {
    if (node->isNil)
        return node;
    if (!node->right->isNil) {
        node = node->right;
        while (!node->left->isNil)
            node = node->left;
        return node;
    }
    RefCounterNode *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

RefCounterNode *FindName(URefCounterMap *map, const char *name) {
    for (RefCounterNode *node = map->head->left; node != map->head; node = Next(node))
        if (_stricmp(node->value.name, name) == 0)
            return node;
    return NULL;
}

// Every live mix: the registry's, the listed ones and their masters.
std::vector<AMix *> LiveMixes() {
    std::vector<AMix *> mixes;
    auto add = [&](AMix *mix) {
        while (mix != NULL) {
            bool known = false;
            for (AMix *other : mixes)
                known = known || other == mix;
            if (known)
                return;
            mixes.push_back(mix);
            mix = mix->master;
        }
    };
    MixRefCounter *map = MixRefCounter::Get();
    for (RefCounterNode *node = map->head->left; node != map->head; node = Next(node))
        add(static_cast<AMix *>(node->value.entry.object));
    MixList &list = *(MixList *)0x00243954;
    if (list.head != NULL)
        for (PointerListNode *node = list.head->next; node != list.head; node = node->next)
            add(static_cast<AMix *>(node->value));
    return mixes;
}

struct MixSnapshot {
    std::vector<AMix *> mixes;
    std::vector<AMix> saved;
    void Take() {
        mixes = LiveMixes();
        saved.clear();
        for (AMix *mix : mixes)
            saved.push_back(*mix);
    }
    void Put() const {
        for (size_t i = 0; i < mixes.size(); i++)
            *mixes[i] = saved[i];
    }
    void Dump(std::string &log) const {
        for (AMix *mix : mixes)
            AppendBytes(log, mix, sizeof(AMix));
    }
};

// ===============================================================================================================
// 1. Mix chains
// ===============================================================================================================

typedef void (__fastcall *SetTransitionFn)(AMix *, int, float, int);
typedef void (__fastcall *RevertFn)(AMix *, int, int);
typedef double (__fastcall *GetVolumeFn)(AMix *, int);
typedef float (__fastcall *GetPresetVolumeFn)(AMix *, int);
typedef double (__fastcall *MixedVolumeFn)(ABaseSound *, int);

void __fastcall PortSetTransition(AMix *mix, int, float to, int steps) { mix->SetTransition(to, steps); }
void __fastcall PortRevert(AMix *mix, int, int steps) { mix->Revert(steps); }
double __fastcall PortGetVolume(AMix *mix, int) { return mix->GetVolume(); }
float __fastcall PortGetPresetVolume(AMix *mix, int) { return mix->GetPresetVolume(); }
double __fastcall PortMixedVolume(ABaseSound *sound, int) { return sound->GetMixedVolume(); }

struct Chain {
    AMix mixes[3];
    ABaseSound sound;
};

float RandomVolume() {
    switch (Random(6)) {
    case 0: return 0.0f;
    case 1: return 1.0f;
    case 2: return Uniform(-0.5f, 1.5f);
    default: return Uniform(0.0f, 1.0f);
    }
}

void RandomMix(AMix *mix) {
    memset(mix, 0, sizeof(*mix));
    mix->volume = RandomVolume();
    mix->previous = RandomVolume();
    mix->unknown0c = RandomVolume();
    mix->presetVolume = Random(4) == 0 ? -1.0f : RandomVolume();
    mix->target = RandomVolume();
    switch (Random(4)) {
    case 0: mix->step = 0.0f; break;
    case 1: mix->step = Uniform(-0.05f, 0.05f); break;
    case 2: mix->step = Uniform(-1.0f, 1.0f); break;
    default: mix->step = mix->target - mix->volume; break;   // ends at the target exactly
    }
    mix->transitioning = Random(3) != 0;
    strcpy(mix->name, "shadow");
}

void TestChains() {
    static const unsigned kEntries[] = { 0x0011c980, 0x0011c9e0, 0x0011ca10, 0x0011ca90, 0x0011dab0 };
    for (int c = 0; c < 3000; c++) {
        g_cases++;
        Chain start;
        memset(&start, 0, sizeof(start));
        for (int i = 0; i < 3; i++)
            RandomMix(&start.mixes[i]);
        start.sound.volume = RandomVolume();
        int depth = int(Random(3));
        int soundMix = int(Random(3));
        uint32_t seed = Random();

        // The pointers are into the chain run on, the same storage for both runs.
        Chain chain;
        auto reset = [&] {
            chain = start;
            for (int i = 0; i < 3; i++)
                chain.mixes[i].master = i < depth ? &chain.mixes[i + 1] : NULL;
            chain.sound.mix = &chain.mixes[soundMix];
            g_seed = seed;
        };
        std::string original, port;
        Both(kEntries, 5, reset, [&](bool orig, std::string &log) {
            int ops = 1 + int(Random(6));
            for (int op = 0; op < ops; op++) {
                AMix *mix = &chain.mixes[Random(3)];
                switch (Random(5)) {
                case 0: {
                    float to = RandomVolume();
                    int steps = int(Random(64)) - 3;
                    (orig ? (SetTransitionFn)0x0011c980 : PortSetTransition)(mix, 0, to, steps);
                    break;
                }
                case 1:
                    (orig ? (RevertFn)0x0011c9e0 : PortRevert)(mix, 0, int(Random(40)) - 3);
                    break;
                case 2: {
                    int calls = 1 + int(Random(8));
                    for (int k = 0; k < calls; k++)
                        Append(log, "v%016llx ", (unsigned long long)Bits((orig ? (GetVolumeFn)0x0011ca10 : PortGetVolume)(mix, 0)));
                    break;
                }
                case 3:
                    Append(log, "p%08x ", Bits((orig ? (GetPresetVolumeFn)0x0011ca90 : PortGetPresetVolume)(mix, 0)));
                    break;
                default:
                    Append(log, "s%016llx ", (unsigned long long)Bits((orig ? (MixedVolumeFn)0x0011dab0 : PortMixedVolume)(&chain.sound, 0)));
                    break;
                }
            }
            log += '\n';
            AppendBytes(log, &chain, sizeof(chain));
        }, original, port);
        Check(original == port, "mix chain", c);
    }
}

// ===============================================================================================================
// 2. Reset over the live mixes
// ===============================================================================================================

typedef void (*ResetFn)(int);

void TestReset() {
    static const unsigned kEntries[] = { 0x0011cb80, 0x0011c980 };
    MixSnapshot live;
    live.Take();
    static const int kSteps[] = { -5, 0, 1, 2, 7, 30, 1000 };
    for (int c = 0; c < 40; c++) {
        g_cases++;
        int steps = kSteps[c % 7];
        uint32_t seed = Random();
        bool perturb = c >= 7;
        std::string original, port;
        Both(kEntries, 2, [&] {
            live.Put();
            g_seed = seed;
            if (perturb)
                for (AMix *mix : live.mixes) {
                    mix->volume = RandomVolume();
                    mix->presetVolume = Random(4) == 0 ? -1.0f : RandomVolume();
                }
        }, [&](bool orig, std::string &log) {
            (orig ? (ResetFn)0x0011cb80 : AMix::Reset)(steps);
            live.Dump(log);
        }, original, port);
        Check(original == port, "AMix::Reset", c);
    }
    live.Put();
}

// ===============================================================================================================
// 3. The registry
// ===============================================================================================================

typedef AMix *(*AddFn)(const char *);
typedef void (__fastcall *RemoveFn)(AMix *, int);
typedef void (__fastcall *ClearMasterFn)(AMix *, int);
typedef void (__fastcall *SetMasterFn)(AMix *, int, const char *);

void __fastcall PortRemove(AMix *mix, int) { mix->Remove(); }
void __fastcall PortClearMaster(AMix *mix, int) { mix->ClearMaster(); }
void __fastcall PortSetMaster(AMix *mix, int, const char *name) { mix->SetMaster(name); }

const char *const kShadowMixes[] = { "AudioMixShadowA", "AudioMixShadowB", "AudioMixShadowC" };

void DumpShadowMixes(std::string &log) {
    for (const char *name : kShadowMixes) {
        RefCounterNode *node = FindName(MixRefCounter::Get(), name);
        if (node == NULL) {
            Append(log, "%s: none\n", name);
            continue;
        }
        AMix copy = *static_cast<AMix *>(node->value.entry.object);
        AMix *master = copy.master;
        copy.master = NULL;
        RefCounterNode *b = FindName(MixRefCounter::Get(), "AudioMixShadowB");
        RefCounterNode *c = FindName(MixRefCounter::Get(), "AudioMixShadowC");
        const char *masterName = master == NULL ? "none"
                                 : b != NULL && master == b->value.entry.object ? "B"
                                 : c != NULL && master == c->value.entry.object ? "C"
                                 : "other";
        Append(log, "%s: %d refs, master %s, ", name, node->value.entry.references, masterName);
        AppendBytes(log, &copy, sizeof(copy));
    }
}

void TestRegistry() {
    static const unsigned kEntries[] = { 0x0011d6a0, 0x0011d720, 0x0011d750, 0x0011da20, 0x0011da60 };
    for (const char *name : kShadowMixes)
        if (FindName(MixRefCounter::Get(), name) != NULL)
            return;
    g_cases++;
    std::string original, port;
    Both(kEntries, 5, [] {}, [&](bool orig, std::string &log) {
        AddFn add = orig ? (AddFn)0x0011d6a0 : AMix::Add;
        AddFn get = orig ? (AddFn)0x0011d720 : AMix::Get;
        RemoveFn remove = orig ? (RemoveFn)0x0011d750 : PortRemove;
        ClearMasterFn clearMaster = orig ? (ClearMasterFn)0x0011da20 : PortClearMaster;
        SetMasterFn setMaster = orig ? (SetMasterFn)0x0011da60 : PortSetMaster;
        AMix *a = add("AudioMixShadowA");
        Append(log, "again %d\n", add("audiomixshadowa") == a);
        AMix *b = get("AudioMixShadowB");
        Append(log, "get %d %d\n", get("AUDIOMIXSHADOWA") == a, b != NULL);
        DumpShadowMixes(log);
        setMaster(a, 0, "AudioMixShadowB");
        DumpShadowMixes(log);
        setMaster(a, 0, "AudioMixShadowC");
        DumpShadowMixes(log);
        clearMaster(a, 0);
        DumpShadowMixes(log);
        setMaster(a, 0, "AudioMixShadowB");
        remove(a, 0);
        DumpShadowMixes(log);
        remove(a, 0);
        DumpShadowMixes(log);
        remove(b, 0);
        DumpShadowMixes(log);
    }, original, port);
    Check(original == port, "the registry", 0);
}

// ===============================================================================================================
// 4. The trees
// ===============================================================================================================

typedef RefCounterInsertResult *(__fastcall *InsertUniqueFn)(RefCounterTree *, int, RefCounterInsertResult *, const RefCounterValue *);
typedef RefCounterNode **(__fastcall *EraseAtFn)(RefCounterTree *, int, RefCounterNode **, RefCounterNode *);
typedef RefCounterNode **(__fastcall *EraseRangeFn)(RefCounterTree *, int, RefCounterNode **, RefCounterNode *, RefCounterNode *);
typedef void (__fastcall *DestructFn)(RefCounterTree *, int);
typedef void (__fastcall *DecrementFn)(RefCounterIterator *, int);

template <class T>
RefCounterInsertResult *__fastcall PortInsertUnique(RefCounterTree *tree, int, RefCounterInsertResult *result, const RefCounterValue *value) {
    return static_cast<T *>(tree)->InsertUnique(result, value);
}
template <class T>
RefCounterNode **__fastcall PortEraseAt(RefCounterTree *tree, int, RefCounterNode **result, RefCounterNode *where) {
    return static_cast<T *>(tree)->EraseAt(result, where);
}
template <class T>
RefCounterNode **__fastcall PortEraseRange(RefCounterTree *tree, int, RefCounterNode **result, RefCounterNode *first, RefCounterNode *last) {
    return static_cast<T *>(tree)->EraseRange(result, first, last);
}
template <class T>
void __fastcall PortDestruct(RefCounterTree *tree, int) {
    static_cast<T *>(tree)->Destruct();
}
void __fastcall PortDecrement(RefCounterIterator *iterator, int) {
    iterator->Decrement();
}

struct TreeKind {
    const char *name;
    unsigned insertUnique, eraseAt, eraseRange, destruct;
    InsertUniqueFn portInsertUnique;
    EraseAtFn portEraseAt;
    EraseRangeFn portEraseRange;
    DestructFn portDestruct;
    unsigned entries[8];
};

const TreeKind kTrees[] = {
    {"URefCounter<AMix>'s tree", 0x0011d380, 0x0011cd30, 0x0011d290, 0x0011d580, PortInsertUnique<MixRefTree>,
     PortEraseAt<MixRefTree>, PortEraseRange<MixRefTree>, PortDestruct<MixRefTree>,
     { 0x0011caa0, 0x0011cb20, 0x0011cc30, 0x0011cd30, 0x0011d0a0, 0x0011d290, 0x0011d380, 0x0011d580 }},
    {"URefCounter<AFader>'s tree", 0x00125990, 0x00125340, 0x001258a0, 0x00125b90, PortInsertUnique<FaderRefTree>,
     PortEraseAt<FaderRefTree>, PortEraseRange<FaderRefTree>, PortDestruct<FaderRefTree>,
     { 0x0011caa0, 0x0011cb20, 0x001252f0, 0x00125340, 0x001256b0, 0x001258a0, 0x00125990, 0x00125b90 }},
};

char g_names[48][24];

const char *NodeName(const RefCounterTree &tree, const RefCounterNode *node) {
    return node == tree.head ? "<end>" : node->value.name;
}

RefCounterNode *Nth(const RefCounterTree &tree, uint32_t n) {
    RefCounterNode *node = tree.head->left;
    while (n-- > 0 && node != tree.head)
        node = Next(node);
    return node;
}

void DumpShape(std::string &log, const RefCounterTree &tree, const RefCounterNode *node) {
    if (node->isNil) {
        log += '.';
        return;
    }
    Append(log, "(%s:%d:%d ", node->value.name, node->color, node->value.entry.references);
    DumpShape(log, tree, node->left);
    log += ' ';
    DumpShape(log, tree, node->right);
    log += ')';
}

void TreeScenario(const TreeKind &kind, bool orig, std::string &log) {
    InsertUniqueFn insert = orig ? (InsertUniqueFn)kind.insertUnique : kind.portInsertUnique;
    EraseAtFn eraseAt = orig ? (EraseAtFn)kind.eraseAt : kind.portEraseAt;
    EraseRangeFn eraseRange = orig ? (EraseRangeFn)kind.eraseRange : kind.portEraseRange;
    DestructFn destruct = orig ? (DestructFn)kind.destruct : kind.portDestruct;
    DecrementFn decrement = orig ? (DecrementFn)0x0011caa0 : PortDecrement;

    RefCounterTree tree;
    tree.allocator = 0;
    tree.head = RefCounterMapBuyHead();
    tree.head->isNil = 1;
    tree.head->parent = tree.head;
    tree.head->left = tree.head;
    tree.head->right = tree.head;
    tree.size = 0;
    for (int op = 0; op < 700; op++) {
        uint32_t what = Random(10);
        if (what < 5 || tree.size == 0) {
            RefCounterValue value;
            memset(&value, 0, sizeof(value));
            strcpy(value.name, g_names[Random(48)]);
            value.entry.references = int32_t(Random(4));
            value.entry.object = (void *)(uintptr_t)(0x00700000 + 0x10 * Random(8));
            RefCounterInsertResult result;
            insert(&tree, 0, &result, &value);
            Append(log, "+%s>%s/%d/%u ", value.name, NodeName(tree, result.node), result.inserted, tree.size);
        } else if (what < 7) {
            RefCounterNode *next;
            eraseAt(&tree, 0, &next, Nth(tree, Random(tree.size)));
            Append(log, "-%s/%u ", NodeName(tree, next), tree.size);
        } else if (what < 8) {
            RefCounterNode *first, *last;
            if (Random(6) == 0) {
                first = tree.head->left;
                last = tree.head;
            } else {
                uint32_t a = Random(tree.size + 1), b = Random(tree.size + 1);
                if (a > b) {
                    uint32_t swap = a;
                    a = b;
                    b = swap;
                }
                first = Nth(tree, a);
                last = Nth(tree, b);
            }
            RefCounterNode *after;
            eraseRange(&tree, 0, &after, first, last);
            Append(log, "~%s/%u ", NodeName(tree, after), tree.size);
        } else {
            RefCounterIterator it = { Nth(tree, Random(tree.size + 1)) };
            decrement(&it, 0);
            Append(log, "<%s ", NodeName(tree, it.node));
        }
        if (op % 50 == 49) {
            log += '\n';
            DumpShape(log, tree, tree.head->parent);
            Append(log, " L%s R%s\n", NodeName(tree, tree.head->left), NodeName(tree, tree.head->right));
        }
    }
    DumpShape(log, tree, tree.head->parent);
    destruct(&tree, 0);
    Append(log, "\nend: head %d size %u\n", tree.head == NULL, tree.size);
}

void TestTrees() {
    static const char *const kStems[] = { "speech", "Music", "ambience", "x", "Fade Effects", "0:Fader0" };
    for (int i = 0; i < 48; i++) {
        snprintf(g_names[i], sizeof(g_names[i]), "%s%d", kStems[i % 6], i / 12);
        if (i % 4 == 1)
            for (char *c = g_names[i]; *c; c++)
                *c = char(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
    }
    for (const TreeKind &kind : kTrees) {
        for (int c = 0; c < 6; c++) {
            g_cases++;
            uint32_t seed = Random();
            std::string original, port;
            Both(kind.entries, 8, [&] { g_seed = seed; }, [&](bool orig, std::string &log) {
                TreeScenario(kind, orig, log);
            }, original, port);
            Check(original == port, kind.name, c);
        }
    }
}

// ===============================================================================================================
// 5. AFX
// ===============================================================================================================

#define ShadowFx (*(FxState *)0x001d80f0)
#define ShadowFxModes ((const FxMode *)0x001d8190)
#define ShadowFxMixes ((AMix **)0x00243ab8)
#define ShadowFxLevel FLOAT_AT(0x00243af0)
#define ShadowFxPaused BOOL8_AT(0x00243af4)
#define ShadowFxReady BOOL8_AT(0x00243aec)
#define ShadowFrames I32_AT(0x00243a68)
#define ShadowSystem PTR_AT(0x00243b34)

// ---- hooks over the sound library's entries (ours: the original's jump and the port it leads to)

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[8];
int g_hookCount;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0])))
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

int FakeMasterLevel(int bus, int level) {
    if (g_calls != NULL)
        Append(*g_calls, "SNDfxmasterlevel(%d, %d) ", bus, level);
    return 0;
}

int FakeInitBus(int bus, int level, int mode, int delay, int feedback) {
    if (g_calls != NULL)
        Append(*g_calls, "SNDfxinitbus(%d, %d, %d, %d, %d) ", bus, level, mode, delay, feedback);
    return 0;
}

struct FxSnapshot {
    FxState fx;
    float level;
    uint8_t paused, ready;
    int32_t frames;
    void Take() {
        fx = ShadowFx;
        level = ShadowFxLevel;
        paused = ShadowFxPaused;
        ready = ShadowFxReady;
        frames = ShadowFrames;
    }
    void Put() const {
        ShadowFx = fx;
        ShadowFxLevel = level;
        ShadowFxPaused = paused;
        ShadowFxReady = ready;
        ShadowFrames = frames;
    }
};

void DumpFx(std::string &log) {
    AppendBytes(log, &ShadowFx, sizeof(FxState));
    Append(log, "level %08x paused %d\n", Bits(ShadowFxLevel), ShadowFxPaused);
}

typedef void (*VoidFn)(void);
typedef void (*SetModeFn)(int, int, int);
typedef const char *(*NameFn)(void);
typedef bool (*BoolFn)(void);

void PerturbFx() {
    for (int i = 0; i < 2; i++) {
        ShadowFx.requests[i].mode = int32_t(Random(AFX::kModeCount));
        ShadowFx.requests[i].priority = int32_t(Random(5)) - 1;
    }
    ShadowFx.currentMode = Random(4) == 0 ? -1 : int32_t(Random(AFX::kModeCount));
    uint32_t bus = Random(4);
    ShadowFx.busReverb = bus == 0 ? -1 : ShadowFxModes[Random(AFX::kModeCount)].reverb;
    switch (Random(4)) {
    case 0: ShadowFxLevel = 0.0f; break;
    case 1: ShadowFxLevel = Uniform(0.0f, 0.05f); break;
    default: ShadowFxLevel = Uniform(0.0f, 1.2f); break;
    }
    ShadowFxPaused = Random(10) == 0;
    ShadowFrames = int32_t(Random(5));
    if (ShadowFxReady) {
        for (int i = 0; i < AFX::kModeCount; i++) {
            AMix *mix = ShadowFxMixes[i];
            if (mix == NULL || Random(2) == 0)
                continue;
            mix->volume = RandomVolume();
            mix->target = RandomVolume();
            mix->step = Uniform(-0.2f, 0.2f);
            mix->transitioning = Random(2) != 0;
        }
    }
}

void TestFx() {
    if (!ShadowFxReady)
        return;
    MixSnapshot live;
    live.Take();
    FxSnapshot fx;
    fx.Take();
    HookInstall(0x0013ccc0, (const void *)&FakeMasterLevel);
    HookInstall(0x0013cd50, (const void *)&FakeInitBus);

    static const unsigned kUpdate[] = { 0x001248b0, 0x0011ca10 };
    for (int c = 0; c < 3000; c++) {
        g_cases++;
        uint32_t seed = Random();
        std::string original, port;
        Both(kUpdate, 2, [&] {
            live.Put();
            fx.Put();
            g_seed = seed;
            PerturbFx();
        }, [&](bool orig, std::string &log) {
            g_calls = &log;
            (orig ? (VoidFn)0x001248b0 : AFX::Update)();
            g_calls = NULL;
            log += '\n';
            DumpFx(log);
            live.Dump(log);
        }, original, port);
        Check(original == port, "AFX::Update", c);
    }

    static const unsigned kSmall[] = { 0x001247d0, 0x00124810, 0x00124830, 0x00124850, 0x00124870 };
    for (int c = 0; c < 500; c++) {
        g_cases++;
        uint32_t seed = Random();
        std::string original, port;
        Both(kSmall, 5, [&] {
            fx.Put();
            g_seed = seed;
            PerturbFx();
        }, [&](bool orig, std::string &log) {
            g_calls = &log;
            for (int op = 0; op < 4; op++) {
                switch (Random(5)) {
                case 0: {
                    int mode = int(Random(AFX::kModeCount)), slot = int(Random(2)), priority = int(Random(5)) - 1;
                    (orig ? (SetModeFn)0x001247d0 : AFX::SetMode)(mode, slot, priority);
                    break;
                }
                case 1:
                    Append(log, "name %s ", (orig ? (NameFn)0x00124810 : AFX::GetCurrentModeName)());
                    break;
                case 2:
                    Append(log, "all %d ", (orig ? (BoolFn)0x00124830 : AFX::IsAllVoices)());
                    break;
                case 3:
                    (orig ? (VoidFn)0x00124850 : AFX::Pause)();
                    break;
                default:
                    (orig ? (VoidFn)0x00124870 : AFX::Resume)();
                    break;
                }
            }
            g_calls = NULL;
            log += '\n';
            DumpFx(log);
        }, original, port);
        Check(original == port, "AFX's small methods", c);
    }

    HooksRemove();
    live.Put();
    fx.Put();
}

// ===============================================================================================================
// 6. AListener
// ===============================================================================================================

typedef AListener *(__fastcall *ListenerFn)(AListener *, int, int);
typedef AListener *(__fastcall *ListenerCameraFn)(AListener *, int, const MATRIX4 *, const Coord3 *, int);

AListener *__fastcall PortListener(AListener *listener, int, int unknown) { return listener->Construct(unknown); }
AListener *__fastcall PortListenerCamera(AListener *listener, int, const MATRIX4 *camera, const Coord3 *velocity, int unknown) {
    return listener->Construct(camera, velocity, unknown);
}

void TestListener() {
    static const unsigned kEntries[] = { 0x00124ae0, 0x00124b50 };
    alignas(16) uint8_t buffer[sizeof(AListener)];
    AListener *listener = reinterpret_cast<AListener *>(buffer);
    for (int c = 0; c < 500; c++) {
        g_cases++;
        MATRIX4 camera;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                camera.mtx[i][j] = i == 3 ? Uniform(-3000.0f, 3000.0f) : Uniform(-1.0f, 1.0f);
        Coord3 velocity = { Uniform(-80.0f, 80.0f), Uniform(-80.0f, 80.0f), Uniform(-80.0f, 80.0f) };
        int unknown = int(Random(4)) - 1;
        bool withCamera = c % 4 != 0;
        std::string original, port;
        Both(kEntries, 2, [&] { memset(buffer, 0xcd, sizeof(buffer)); }, [&](bool orig, std::string &log) {
            AListener *answer = withCamera
                ? (orig ? (ListenerCameraFn)0x00124b50 : PortListenerCamera)(listener, 0, &camera, &velocity, unknown)
                : (orig ? (ListenerFn)0x00124ae0 : PortListener)(listener, 0, unknown);
            Append(log, "this %d\n", answer == listener);
            AppendBytes(log, buffer, sizeof(buffer));
        }, original, port);
        Check(original == port, withCamera ? "AListener(camera)" : "AListener()", c);
    }
}

// ===============================================================================================================
// 7. AFader
// ===============================================================================================================

typedef void (__fastcall *PrivSetSecondaryFn)(AFader::Priv *, int, const char *);
typedef void (__fastcall *SetSecondaryNameFn)(AFader *, int, const char *);
typedef void (__fastcall *SetSecondaryOnFn)(AFader *, int, bool);
typedef void (__fastcall *SetFilterFn)(AFader *, int, float);
typedef void (__fastcall *FaderFn)(AFader *, int);
typedef AFader *(*FaderGetFn)(const char *);
typedef AFader *(*FaderCreateFn)(const char *, AStream *, AMix *);
typedef void (*FaderRemoveFn)(const char *);

void __fastcall PortPrivSetSecondary(AFader::Priv *priv, int, const char *name) { priv->SetSecondary(name); }
void __fastcall PortSetSecondaryName(AFader *fader, int, const char *name) { fader->SetSecondary(name); }
void __fastcall PortSetSecondaryOn(AFader *fader, int, bool on) { fader->SetSecondary(on); }
void __fastcall PortSetFilter(AFader *fader, int, float filter) { fader->SetFilter(filter); }
void __fastcall PortCall911(AFader *fader, int) { fader->Call911(); }

const char *const kSecondaries[] = { "Off", "off", "city", "CITY", "Cit", "ambience1", "" };

void TestFader() {
    static const unsigned kEntries[] = { 0x00124e30, 0x00125200, 0x00125210, 0x00125220, 0x00125230 };
    for (int c = 0; c < 1500; c++) {
        g_cases++;
        AFader::Priv start;
        memset(&start, 0, sizeof(start));
        start.fade = Uniform(0.0f, 1.0f);
        strcpy(start.secondary, kSecondaries[Random(7)]);
        start.stream = (AStream *)(uintptr_t)0x00700000;
        start.filter = Uniform(0.0f, 1.0f);
        start.unknown28 = uint8_t(Random(2));
        start.unknown29 = uint8_t(Random(2));
        start.fadingOut = uint8_t(Random(2));
        start.unknown2b = uint8_t(Random(2));
        start.unknown2c = Random(5) != 0;
        start.secondaryOn = uint8_t(Random(2));
        uint32_t seed = Random();
        AFader::Priv priv;
        AFader fader = { &priv };
        std::string original, port;
        Both(kEntries, 5, [&] { priv = start; g_seed = seed; }, [&](bool orig, std::string &log) {
            for (int op = 0; op < 3; op++) {
                const char *name = kSecondaries[Random(7)];
                switch (Random(5)) {
                case 0: (orig ? (PrivSetSecondaryFn)0x00124e30 : PortPrivSetSecondary)(&priv, 0, name); break;
                case 1: (orig ? (SetSecondaryNameFn)0x00125200 : PortSetSecondaryName)(&fader, 0, name); break;
                case 2: (orig ? (SetSecondaryOnFn)0x00125210 : PortSetSecondaryOn)(&fader, 0, Random(2) != 0); break;
                case 3: (orig ? (SetFilterFn)0x00125220 : PortSetFilter)(&fader, 0, Uniform(-1.0f, 1.0f)); break;
                default: (orig ? (FaderFn)0x00125230 : PortCall911)(&fader, 0); break;
                }
            }
            AppendBytes(log, &priv, sizeof(priv));
        }, original, port);
        Check(original == port, "AFader's state", c);
    }

    // Get on the live faders and an unknown name
    {
        static const unsigned kGet[] = { 0x00125d40 };
        static const char *const kNames[] = { "SpeechVsAmbience", "speechvsambience", "AudioMixShadowFader" };
        for (int i = 0; i < 3; i++) {
            g_cases++;
            std::string original, port;
            Both(kGet, 1, [] {}, [&](bool orig, std::string &log) {
                Append(log, "%p", (void *)(orig ? (FaderGetFn)0x00125d40 : AFader::Get)(kNames[i]));
            }, original, port);
            Check(original == port, "AFader::Get", i);
        }
    }

    // Create and Remove on a name of the test's own (the constructor's mixes exist: Get only reads)
    if (FindName(FaderRefCounter::Get(), "AudioMixShadowFader") == NULL &&
        FindName(MixRefCounter::Get(), "0:Fade Effects") != NULL && FindName(MixRefCounter::Get(), "0:Fade Music") != NULL) {
        static const unsigned kCreate[] = { 0x00125cb0, 0x00125180, 0x00124c40, 0x00125d60 };
        g_cases++;
        AMix *scaled = static_cast<AMix *>(FindName(MixRefCounter::Get(), "0:Fade Music")->value.entry.object);
        std::string original, port;
        Both(kCreate, 4, [] {}, [&](bool orig, std::string &log) {
            AFader *fader = (orig ? (FaderCreateFn)0x00125cb0 : AFader::Create)("AudioMixShadowFader",
                                                                                (AStream *)(uintptr_t)0x00700000, scaled);
            RefCounterNode *node = FindName(FaderRefCounter::Get(), "AudioMixShadowFader");
            Append(log, "registered %d refs %d\n", node != NULL && node->value.entry.object == fader,
                   node != NULL ? node->value.entry.references : -1);
            if (fader != NULL && fader->priv != NULL)
                AppendBytes(log, fader->priv, sizeof(AFader::Priv));
            (orig ? (FaderRemoveFn)0x00125d60 : AFader::Remove)("AudioMixShadowFader");
            Append(log, "removed %d\n", FindName(FaderRefCounter::Get(), "AudioMixShadowFader") == NULL);
        }, original, port);
        Check(original == port, "AFader::Create/Remove", 0);
    }
}

}  // namespace

void AudioMixShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_AUDIOMIXSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);
    TestChains();
    TestReset();
    TestRegistry();
    TestTrees();
    TestFx();
    TestListener();
    TestFader();
    printf("[audiomixshadow] AMix, its tree, AFX, AListener, AFader: %ld cases, %ld checks, %ld differ%s\n", g_cases,
           g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    fflush(stdout);
}
