#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "ActModelShadow.h"
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

#include "../anim/Manager.h"
#include "../anim/Model.h"
#include "../anim/Poser.h"                // ActPoseMatrices
#include "../anim/Skeleton.h"
#include "../eagl/Loader.h"
#include "../eagl/anim/EventTarget.h"
#include "../engine/UMemory.hpp"
#include "../render/Lights.h"
#include "../world/SoundMap.h"          // RefCounterMapBuyHead
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of the actor manager, models, skeletons and textures (anim/Manager.cpp, Model.cpp, Skeleton.cpp),
// run once from the first simulation tick when NIGHTFIRE_ACTMODELSHADOW is set (ActManager::StartUp has made the
// databases, the skeletons, the texture database and the lights by then). Each case runs the ORIGINAL (its entry,
// and those of the originals it calls, swapped back) and then the PORT on the same state, put back between the
// two, and compares:
//
//   - the trees of URefCounter<ModelInfo> and URefCounter<TextureInfo>: random inserts (names differing only in
//     case among them), erases, range erases, RemoveReference by object, the shared helpers (++iterator,
//     lower_bound, _Max, _Rrotate) on a private tree of each kind, then DestroyRange: the answers, the size, and
//     the tree's shape and colours;
//   - the live skeletons: GetBoneIndex on every "Bone" symbol and some names that are not, GetNumBones, both
//     GetStillPose, BlendBones on perturbed still poses (random t, special values among them): every output bit;
//   - BlendQ, BlendQT, the translation lerp at 0x00019870 and MakeCoord4 on random and special floats;
//   - ActSkeleton's constructor and destructor on the nine skeleton files, with the shared animation buffer absent,
//     too small and big enough: the bone indices, the skeleton's place in its file, its still pose, and what
//     happened to the buffer and its registration (the live buffer is put back afterwards);
//   - ActPoserMatrices' copy constructor and destructor on random matrices and counts;
//   - SetIRMode in every combination of its flags and argument over random light blocks: the scene's lights, both
//     saved blocks and the flags (all put back);
//   - SymbolResolver on the live event names (only names it already knows: a new one would be added) and names
//     without the "event." prefix; UseModel and UseTexture on every live model and texture.
//
// ActManager's StartUp/ShutDown/Init/Destruct, the databases' constructors, destructors and loads, ActModel's
// Load/UnLoad/FindTars/SetTexture, the cross-fade record and the helpers at 0x0001aa40/0x0001aa60 build or change
// the live system and are checked in game (lockstep runs).
//
// A mutation the test sees: BlendBones lerping the translation only when allTranslations (not for the root bone:
// half the cases pass false, and the root's translations differ), or LowerBound stopping at an equal name.
//
// One summary line: [actmodelshadow] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

namespace {

#define ShadowActManager (*(ActManager **)0x001dd9cc)
#define ShadowEventResolver (*(EventTarget ***)0x001dd9d0)   // ActEventResolver: its EventTarget first
#define ShadowNormalLight (*(LightBlock **)0x001dd9d4)
#define ShadowIRLight (*(LightBlock **)0x001dd9d8)
#define ShadowIRLightReady U8_AT(0x001dd9dc)
#define ShadowIRModeOn U8_AT(0x001dd9dd)
#define ShadowLighting (*(uint8_t **)0x001ec260)               // the scene's LightBlock at +0xec
#define ShadowAnimationBuffer (*(MATRIX4 **)0x001dd9fc)
#define ShadowAnimationBufferBones I32_AT(0x001dda00)
#define ShadowSkeletonFiles ((const char (*)[0x5a])0x001b4ae8)

const char kBufferName[] = "EAGLAnimationBuffer";

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
        printf("[actmodelshadow] DIFF %s, case %ld\n", what, index);
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
// Mostly ordinary values, sometimes one an x87 chain treats specially.
float RandomFloat(float lo, float hi) {
    static const uint32_t kSpecial[] = {0x80000000, 0x00000001, 0x7f800000, 0xff800000, 0x7fc00000, 0x7f7fffff,
                                        0x3f800000, 0x00000000, 0x33d6bf95};
    if (Random(24) == 0)
        return std::bit_cast<float>(kSpecial[Random(sizeof(kSpecial) / sizeof(kSpecial[0]))]);
    return Uniform(lo, hi);
}

void Append(std::string &log, const char *format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    log += line;
}

void AppendBytes(std::string &log, const void *data, size_t size) {
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; i++)
        Append(log, "%02x", bytes[i]);
    log += '\n';
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

// Runs `run(original, log)` for the original (inside the window) and then the port, with `reset` before each.
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

// ===============================================================================================================
// 1. The trees
// ===============================================================================================================

typedef RefCounterInsertResult *(__fastcall *InsertUniqueFn)(RefCounterTree *, int, RefCounterInsertResult *, const RefCounterValue *);
typedef RefCounterNode **(__fastcall *EraseAtFn)(RefCounterTree *, int, RefCounterNode **, RefCounterNode *);
typedef RefCounterNode **(__fastcall *EraseRangeFn)(RefCounterTree *, int, RefCounterNode **, RefCounterNode *, RefCounterNode *);
typedef bool (__fastcall *RemoveFn)(RefCounterTree *, int, void *);
typedef void (__fastcall *DestroyRangeFn)(RefCounterTree *, int);
typedef void (__fastcall *IncrementFn)(RefCounterIterator *, int);
typedef RefCounterNode *(__fastcall *LowerBoundFn)(RefCounterTree *, int, const char *);
typedef void (__fastcall *RrotateFn)(RefCounterTree *, int, RefCounterNode *);
typedef RefCounterNode *(*MaxFn)(RefCounterNode *);

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
void __fastcall PortDestroyRange(RefCounterTree *tree, int) {
    static_cast<T *>(tree)->DestroyRange();
}
bool __fastcall PortRemoveModel(RefCounterTree *tree, int, void *object) {
    return static_cast<ModelInfoRefCounter *>(static_cast<URefCounterMap *>(tree))->RemoveReference(static_cast<ActModelInfo *>(object));
}
bool __fastcall PortRemoveTexture(RefCounterTree *tree, int, void *object) {
    return static_cast<TextureInfoRefCounter *>(static_cast<URefCounterMap *>(tree))->RemoveReference(static_cast<ActTextureInfo *>(object));
}
void __fastcall PortIncrement(RefCounterIterator *iterator, int) {
    iterator->Increment();
}
RefCounterNode *__fastcall PortLowerBound(RefCounterTree *tree, int, const char *key) {
    return tree->LowerBound(key);
}
void __fastcall PortRrotate(RefCounterTree *tree, int, RefCounterNode *where) {
    tree->Rrotate(where);
}

struct TreeKind {
    const char *name;
    unsigned insertUnique, eraseAt, eraseRange, remove, destroyRange;
    InsertUniqueFn portInsertUnique;
    EraseAtFn portEraseAt;
    EraseRangeFn portEraseRange;
    RemoveFn portRemove;
    DestroyRangeFn portDestroyRange;
    unsigned entries[13];
};

// Each kind's compiled copies, and the shared helpers (RefCounterTree's, engine/URefCounter.cpp)
const TreeKind kTrees[] = {
    {"URefCounter<ModelInfo>'s tree", 0x00017f50, 0x000178e0, 0x00017e60, 0x00017ee0, 0x00018270,
     PortInsertUnique<ModelInfoRefTree>, PortEraseAt<ModelInfoRefTree>, PortEraseRange<ModelInfoRefTree>,
     PortRemoveModel, PortDestroyRange<ModelInfoRefTree>,
     {0x00017860, 0x000178e0, 0x00017c70, 0x00017e60, 0x00017ee0, 0x00017f50, 0x00018270, 0x00017560, 0x00019d20,
      0x00019d80, 0x00019de0, 0x0011caa0, 0x0011cb20}},
    {"URefCounter<TextureInfo>'s tree", 0x0001a500, 0x00019eb0, 0x0001a410, 0x0001a490, 0x0001a840,
     PortInsertUnique<TextureInfoRefTree>, PortEraseAt<TextureInfoRefTree>, PortEraseRange<TextureInfoRefTree>,
     PortRemoveTexture, PortDestroyRange<TextureInfoRefTree>,
     {0x00019e60, 0x00019eb0, 0x0001a220, 0x0001a410, 0x0001a490, 0x0001a500, 0x0001a840, 0x00017560, 0x00019d20,
      0x00019d80, 0x00019de0, 0x0011caa0, 0x0011cb20}},
};

char g_names[48][24];

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

const char *NodeName(const RefCounterTree &tree, const RefCounterNode *node) {
    return node == tree.head ? "<end>" : node->value.name;
}

RefCounterNode *Nth(const RefCounterTree &tree, uint32_t n) {
    RefCounterNode *node = tree.head->left;
    while (n-- > 0 && node != tree.head)
        node = Next(node);
    return node;
}

void DumpShape(std::string &log, const RefCounterNode *node) {
    if (node->isNil) {
        log += '.';
        return;
    }
    Append(log, "(%s:%d:%d:%x ", node->value.name, node->color, node->value.entry.references,
           unsigned(uintptr_t(node->value.entry.object)));
    DumpShape(log, node->left);
    log += ' ';
    DumpShape(log, node->right);
    log += ')';
}

void *FakeObject() {
    return reinterpret_cast<void *>(uintptr_t(0x00700000 + 0x10 * Random(8)));
}

void TreeScenario(const TreeKind &kind, bool orig, std::string &log) {
    InsertUniqueFn insert = orig ? (InsertUniqueFn)kind.insertUnique : kind.portInsertUnique;
    EraseAtFn eraseAt = orig ? (EraseAtFn)kind.eraseAt : kind.portEraseAt;
    EraseRangeFn eraseRange = orig ? (EraseRangeFn)kind.eraseRange : kind.portEraseRange;
    RemoveFn remove = orig ? (RemoveFn)kind.remove : kind.portRemove;
    DestroyRangeFn destroyRange = orig ? (DestroyRangeFn)kind.destroyRange : kind.portDestroyRange;
    IncrementFn increment = orig ? (IncrementFn)0x00019d80 : PortIncrement;
    LowerBoundFn lowerBound = orig ? (LowerBoundFn)0x00019de0 : PortLowerBound;
    RrotateFn rrotate = orig ? (RrotateFn)0x00019d20 : PortRrotate;
    MaxFn maximum = orig ? (MaxFn)0x00017560 : RefCounterTree::Max;

    RefCounterTree tree;
    tree.allocator = 0;
    tree.head = RefCounterMapBuyHead();
    tree.head->isNil = 1;
    tree.head->parent = tree.head;
    tree.head->left = tree.head;
    tree.head->right = tree.head;
    tree.size = 0;
    for (int op = 0; op < 700; op++) {
        uint32_t what = Random(20);
        if (what < 9 || tree.size == 0) {
            RefCounterValue value;
            memset(&value, 0, sizeof(value));
            strcpy(value.name, g_names[Random(48)]);
            value.entry.references = int32_t(Random(4));
            value.entry.object = FakeObject();
            RefCounterInsertResult result;
            insert(&tree, 0, &result, &value);
            Append(log, "+%s>%s/%d/%u ", value.name, NodeName(tree, result.node), result.inserted, tree.size);
        } else if (what < 12) {
            RefCounterNode *next;
            eraseAt(&tree, 0, &next, Nth(tree, Random(tree.size)));
            Append(log, "-%s/%u ", NodeName(tree, next), tree.size);
        } else if (what < 13) {
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
        } else if (what < 15) {
            bool removed = remove(&tree, 0, FakeObject());
            Append(log, "r%d/%u ", removed, tree.size);
        } else if (what < 16) {
            RefCounterIterator it = {Nth(tree, Random(tree.size + 1))};
            increment(&it, 0);
            Append(log, ">%s ", NodeName(tree, it.node));
        } else if (what < 18) {
            const char *key = g_names[Random(48)];
            Append(log, "lb%s>%s ", key, NodeName(tree, lowerBound(&tree, 0, key)));
        } else if (what < 19) {
            RefCounterNode *node = Nth(tree, Random(tree.size));
            Append(log, "max>%s ", NodeName(tree, maximum(node)));
        } else {
            RefCounterNode *node = Nth(tree, Random(tree.size));
            if (!node->left->isNil) {
                rrotate(&tree, 0, node);
                Append(log, "rr%s ", node->value.name);
            }
        }
        if (op % 50 == 49) {
            log += '\n';
            DumpShape(log, tree.head->parent);
            Append(log, " L%s R%s\n", NodeName(tree, tree.head->left), NodeName(tree, tree.head->right));
        }
    }
    DumpShape(log, tree.head->parent);
    destroyRange(&tree, 0);
    Append(log, "\nend: head %d size %u\n", tree.head == NULL, tree.size);
}

void TestTrees() {
    static const char *const kStems[] = {"hhhh", "Bond", "tar_bbbb", "q", "Henchman Two", "skel"};
    for (int i = 0; i < 48; i++) {
        snprintf(g_names[i], sizeof(g_names[i]), "%s%d", kStems[i % 6], i / 12);
        if (i % 4 == 1)
            for (char *c = g_names[i]; *c; c++)
                *c = char(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
    }
    for (const TreeKind &kind : kTrees) {
        for (int c = 0; c < 8; c++) {
            g_cases++;
            uint32_t seed = Random();
            std::string original, port;
            Both(kind.entries, 13, [&] { g_seed = seed; }, [&](bool orig, std::string &log) {
                TreeScenario(kind, orig, log);
            }, original, port);
            Check(original == port, kind.name, c);
        }
    }
}

// ===============================================================================================================
// 2. The live skeletons
// ===============================================================================================================

typedef int (__fastcall *GetBoneIndexFn)(ActSkeleton *, int, const char *);
typedef int (__fastcall *GetNumBonesFn)(ActSkeleton *, int);
typedef void (__fastcall *GetStillPoseIntoFn)(ActSkeleton *, int, float *);
typedef void (__fastcall *GetStillPoseFn)(ActSkeleton *, int);
typedef void (__fastcall *BlendBonesFn)(ActSkeleton *, int, float, const float *, const float *, float *, bool);

std::vector<ActSkeleton *> LiveSkeletons() {
    std::vector<ActSkeleton *> list;
    ActManager *manager = ShadowActManager;
    if (manager == NULL || manager->skeletons == NULL)
        return list;
    for (ActSkeleton *skeleton : manager->skeletons->skeletons)
        if (skeleton != NULL && skeleton->skeleton != NULL)
            list.push_back(skeleton);
    return list;
}

void RandomPose(const ActSkeleton *skeleton, float *pose) {
    int count = skeleton->skeleton->count;
    for (int i = 0; i < count * 12; i++)
        pose[i] = skeleton->stillPose[i];
    for (int bone = 0; bone < count; bone++) {
        float *b = pose + bone * 12;
        for (int i = 4; i < 8; i++)
            b[i] = RandomFloat(-1.0f, 1.0f);
        for (int i = 8; i < 11; i++)
            b[i] = b[i] + RandomFloat(-2.0f, 2.0f);
    }
}

void TestSkeletons() {
    static const unsigned kEntries[] = {0x000197f0, 0x00019820, 0x00019830, 0x00019ac0, 0x00019ad0, 0x00019870};
    std::vector<ActSkeleton *> skeletons = LiveSkeletons();
    for (size_t s = 0; s < skeletons.size(); s++) {
        ActSkeleton *skeleton = skeletons[s];
        int count = skeleton->skeleton->count;
        size_t poseBytes = size_t(count) * 12 * sizeof(float);

        // The bone names: every "Bone" symbol, and some that are not
        std::vector<std::string> names = {"Root.Root", "Root.Weapon", "Root.Clip", "Root.RWeapon", "", "root.root",
                                          "Root.Root ", "NotABone"};
        int index = 0;
        LoaderSymbol symbol;
        while (skeleton->loader->GetNextSymbol("Bone", &index, &symbol))
            names.push_back(symbol.name);
        g_cases++;
        std::string original, port;
        Both(kEntries, 1, [] {}, [&](bool orig, std::string &log) {
            for (const std::string &name : names) {
                const char *text = name.c_str();
                int bone = orig ? ((GetBoneIndexFn)0x000197f0)(skeleton, 0, text) : skeleton->GetBoneIndex(text);
                Append(log, "%s=%d ", text, bone);
            }
            Append(log, "n%d", orig ? ((GetNumBonesFn)0x00019ac0)(skeleton, 0) : skeleton->GetNumBones());
        }, original, port);
        Check(original == port, "ActSkeleton::GetBoneIndex/GetNumBones", long(s));

        // Both GetStillPose
        std::vector<uint8_t> saved(poseBytes);
        memcpy(saved.data(), skeleton->stillPose, poseBytes);
        std::vector<float> into(count * 12 + 4);
        g_cases++;
        Both(kEntries, 6, [&] {
            memset(skeleton->stillPose, 0xcd, poseBytes);
            memset(into.data(), 0xab, into.size() * sizeof(float));
        }, [&](bool orig, std::string &log) {
            if (orig) {
                ((GetStillPoseIntoFn)0x00019830)(skeleton, 0, into.data());
                ((GetStillPoseFn)0x00019820)(skeleton, 0);
            } else {
                skeleton->GetStillPose(into.data());
                skeleton->GetStillPose();
            }
            AppendBytes(log, into.data(), into.size() * sizeof(float));
            AppendBytes(log, skeleton->stillPose, poseBytes);
        }, original, port);
        memcpy(skeleton->stillPose, saved.data(), poseBytes);
        Check(original == port, "ActSkeleton::GetStillPose", long(s));

        // BlendBones
        std::vector<float> from(count * 12), to(count * 12), out(count * 12 + 4);
        for (int c = 0; c < 40; c++) {
            RandomPose(skeleton, from.data());
            RandomPose(skeleton, to.data());
            float t = RandomFloat(-0.5f, 1.5f);
            bool all = Random(2) == 0;
            g_cases++;
            Both(kEntries, 6, [&] { memset(out.data(), 0xab, out.size() * sizeof(float)); },
                 [&](bool orig, std::string &log) {
                if (orig)
                    ((BlendBonesFn)0x00019ad0)(skeleton, 0, t, from.data(), to.data(), out.data(), all);
                else
                    skeleton->BlendBones(t, from.data(), to.data(), out.data(), all);
                AppendBytes(log, out.data(), out.size() * sizeof(float));
            }, original, port);
            Check(original == port, "ActSkeleton::BlendBones", c);
        }
    }
}

// ===============================================================================================================
// 3. The blends and MakeCoord4
// ===============================================================================================================

typedef void (*BlendQFn)(float *, const float *, float);
typedef void (*BlendQTFn)(float *, float *, const float *, const float *, float);
typedef void (*LerpFn)(float, const float *, const float *, float *);
typedef Coord4 *(*MakeCoord4Fn)(Coord4 *, const Coord3 *, float);

void TestBlends() {
    static const unsigned kEntries[] = {0x00019850, 0x00019b80, 0x00019870, 0x0001a9f0};
    for (int c = 0; c < 4000; c++) {
        float q[4], target[4], t0[4], t1[4], lerped[4];
        for (int i = 0; i < 4; i++) {
            q[i] = RandomFloat(-1.0f, 1.0f);
            target[i] = RandomFloat(-1.0f, 1.0f);
            t0[i] = RandomFloat(-100.0f, 100.0f);
            t1[i] = RandomFloat(-100.0f, 100.0f);
        }
        float t = RandomFloat(-0.5f, 1.5f);
        float w = RandomFloat(-10.0f, 10.0f);
        float qa[4], ta[4];
        g_cases++;
        std::string original, port;
        Both(kEntries, 4, [&] {
            memcpy(qa, q, sizeof(q));
            memcpy(ta, t0, sizeof(t0));
            memset(lerped, 0xab, sizeof(lerped));
        }, [&](bool orig, std::string &log) {
            float q2[4];
            memcpy(q2, q, sizeof(q));
            Coord4 made;
            memset(&made, 0xab, sizeof(made));
            Coord3 v = {t1[0], t1[1], t1[2]};
            if (orig) {
                ((BlendQTFn)0x00019b80)(qa, ta, target, t1, t);
                ((BlendQFn)0x00019850)(q2, target, t);
                ((LerpFn)0x00019870)(t, t0, t1, lerped);
                Append(log, "%d ", ((MakeCoord4Fn)0x0001a9f0)(&made, &v, w) == &made);
            } else {
                ActSkeleton::BlendQT((Coord4 *)qa, (Coord4 *)ta, (const Coord4 *)target, (const Coord4 *)t1, t);
                ActSkeleton::BlendQ((Coord4 *)q2, (const Coord4 *)target, t);
                LerpTranslation(t, t0, t1, lerped);
                Append(log, "%d ", MakeCoord4(&made, &v, w) == &made);
            }
            AppendBytes(log, qa, sizeof(qa));
            AppendBytes(log, ta, sizeof(ta));
            AppendBytes(log, q2, sizeof(q2));
            AppendBytes(log, lerped, sizeof(lerped));
            AppendBytes(log, &made, sizeof(made));
        }, original, port);
        Check(original == port, "BlendQ/BlendQT/lerp/MakeCoord4", c);
    }
}

// ===============================================================================================================
// 4. ActSkeleton's constructor and destructor
// ===============================================================================================================

typedef ActSkeleton *(__fastcall *SkeletonConstructFn)(ActSkeleton *, int, const char *);
typedef void (__fastcall *SkeletonDestructFn)(ActSkeleton *, int);

void TestSkeletonLoads() {
    static const unsigned kEntries[] = {0x000198b0, 0x00019c60, 0x000197f0, 0x00016ec0};
    MATRIX4 *liveBuffer = ShadowAnimationBuffer;
    int liveBones = ShadowAnimationBufferBones;
    if (liveBuffer == NULL)
        return;
    for (int file = 0; file < ActSkeletonDatabase::kSkeletonCount; file++) {
        for (int state = 0; state < 3; state++) {   // the buffer as it is, too small, absent
            MATRIX4 *scratch = NULL;
            g_cases++;
            std::string original, port;
            Both(kEntries, 4, [&] {
                DynamicLoader::UnRegisterVar(kBufferName);
                scratch = NULL;
                if (state == 0) {
                    ShadowAnimationBuffer = liveBuffer;
                    ShadowAnimationBufferBones = liveBones;
                    DynamicLoader::RegisterVar(kBufferName, liveBuffer);
                } else if (state == 1) {
                    scratch = static_cast<MATRIX4 *>(OperatorNewArray(sizeof(MATRIX4)));
                    ShadowAnimationBuffer = scratch;
                    ShadowAnimationBufferBones = 0;
                    DynamicLoader::RegisterVar(kBufferName, scratch);
                } else {
                    ShadowAnimationBuffer = NULL;
                    ShadowAnimationBufferBones = 0;
                }
            }, [&](bool orig, std::string &log) {
                ActSkeleton *skeleton = static_cast<ActSkeleton *>(UMemory::FastAlloc(sizeof(ActSkeleton), "ActSkeleton"));
                memset(skeleton, 0xab, sizeof(ActSkeleton));
                const char *path = ShadowSkeletonFiles[file];
                if (orig)
                    ((SkeletonConstructFn)0x000198b0)(skeleton, 0, path);
                else
                    skeleton->Construct(path);
                bool found = false;
                void *registered = DynamicLoader::GetRegisteredVar(kBufferName, &found);
                int count = skeleton->skeleton->count;
                Append(log, "bones %d root %d weapon %d clip %d at %d buffer %d/%d/%d registered %d/%d\n", count,
                       skeleton->rootBone, skeleton->weaponBones[0], skeleton->weaponBones[1],
                       int(reinterpret_cast<uint8_t *>(skeleton->skeleton) - static_cast<uint8_t *>(skeleton->file)),
                       ShadowAnimationBuffer == liveBuffer, ShadowAnimationBuffer == scratch,
                       ShadowAnimationBufferBones, found, registered == ShadowAnimationBuffer);
                AppendBytes(log, skeleton->stillPose, size_t(count) * 12 * sizeof(float));
                Append(log, "buffers %d %d\n", skeleton->pose != NULL, skeleton->matrices != NULL);
                if (orig)
                    ((SkeletonDestructFn)0x00019c60)(skeleton, 0);
                else
                    skeleton->Destruct();
                UMemory::FastFree(skeleton, sizeof(ActSkeleton));
                // A buffer the constructor made is freed here (it freed the scratch one itself)
                if (ShadowAnimationBuffer != liveBuffer && ShadowAnimationBuffer != NULL)
                    OperatorDelete(ShadowAnimationBuffer);
                ShadowAnimationBuffer = NULL;
            }, original, port);
            Check(original == port, "ActSkeleton::ActSkeleton/~ActSkeleton", file * 3 + state);
        }
    }
    DynamicLoader::UnRegisterVar(kBufferName);
    DynamicLoader::RegisterVar(kBufferName, liveBuffer);
    ShadowAnimationBuffer = liveBuffer;
    ShadowAnimationBufferBones = liveBones;
}

// ===============================================================================================================
// 5. ActPoserMatrices
// ===============================================================================================================

typedef ActPoseMatrices *(__fastcall *MatricesCopyFn)(ActPoseMatrices *, int, const ActPoseMatrices *);
typedef void (__fastcall *MatricesDestructFn)(ActPoseMatrices *, int);

void TestMatrices() {
    static const unsigned kEntries[] = {0x00018410, 0x00018400};
    for (int c = 0; c < 200; c++) {
        ActPoseMatrices source;
        for (size_t i = 0; i < sizeof(source.matrices) / sizeof(float); i++)
            reinterpret_cast<float *>(source.matrices)[i] = RandomFloat(-10.0f, 10.0f);
        source.current = NULL;
        source.buffer = NULL;
        source.count = int32_t(Random(64));
        g_cases++;
        std::string original, port;
        Both(kEntries, 2, [] {}, [&](bool orig, std::string &log) {
            ActPoseMatrices copy;
            memset(&copy, 0xab, sizeof(copy));
            if (orig)
                ((MatricesCopyFn)0x00018410)(&copy, 0, &source);
            else
                copy.ConstructCopy(&source);
            AppendBytes(log, copy.matrices, sizeof(copy.matrices));
            Append(log, "%d %d %d\n", copy.count, copy.current == copy.buffer, copy.buffer != NULL);
            if (orig)
                ((MatricesDestructFn)0x00018400)(&copy, 0);
            else
                copy.Destruct();
        }, original, port);
        Check(original == port, "ActPoserMatrices copy", c);
    }
}

// ===============================================================================================================
// 6. SetIRMode
// ===============================================================================================================

typedef void (*SetIRModeFn)(bool);

void TestIRMode() {
    static const unsigned kEntries[] = {0x00016ed0};
    LightBlock *normal = ShadowNormalLight, *ir = ShadowIRLight;
    uint8_t *lighting = ShadowLighting;
    if (normal == NULL || ir == NULL || lighting == NULL)
        return;
    LightBlock *scene = reinterpret_cast<LightBlock *>(lighting + 0xec);
    LightBlock savedScene = *scene, savedNormal = *normal, savedIR = *ir;
    uint8_t savedReady = ShadowIRLightReady, savedMode = ShadowIRModeOn;
    for (int c = 0; c < 64; c++) {
        LightBlock blocks[3];
        for (LightBlock &block : blocks)
            for (size_t i = 0; i < sizeof(LightBlock) / sizeof(float); i++)
                reinterpret_cast<float *>(&block)[i] = RandomFloat(-2.0f, 2.0f);
        uint8_t ready = uint8_t(c & 1), mode = uint8_t((c >> 1) & 1);
        bool on = ((c >> 2) & 1) != 0;
        g_cases++;
        std::string original, port;
        Both(kEntries, 1, [&] {
            *scene = blocks[0];
            *normal = blocks[1];
            *ir = blocks[2];
            ShadowIRLightReady = ready;
            ShadowIRModeOn = mode;
        }, [&](bool orig, std::string &log) {
            (orig ? (SetIRModeFn)0x00016ed0 : ActManager::SetIRMode)(on);
            AppendBytes(log, scene, sizeof(LightBlock));
            AppendBytes(log, normal, sizeof(LightBlock));
            AppendBytes(log, ir, sizeof(LightBlock));
            Append(log, "%d %d\n", ShadowIRLightReady, ShadowIRModeOn);
        }, original, port);
        Check(original == port, "ActManager::SetIRMode", c);
    }
    *scene = savedScene;
    *normal = savedNormal;
    *ir = savedIR;
    ShadowIRLightReady = savedReady;
    ShadowIRModeOn = savedMode;
}

// ===============================================================================================================
// 7. SymbolResolver, UseModel, UseTexture
// ===============================================================================================================

typedef int (*SymbolResolverFn)(const char *, bool *);
typedef ActModel *(__fastcall *UseModelFn)(ActModelDatabase *, int, ActCharacterInfo *);
typedef EAGL::TAR *(__fastcall *UseTextureFn)(ActTextureDatabase *, int, const char *);

void TestLookups() {
    static const unsigned kEntries[] = {0x00017010, 0x00017840, 0x00019e30};
    std::vector<std::string> names = {"event", "Event.x", "", "events.a", "notevent.a"};
    EventTarget **resolver = ShadowEventResolver;
    if (resolver != NULL && *resolver != NULL && (*resolver)->names != NULL)
        for (int i = 0; i < (*resolver)->count; i++)
            if ((*resolver)->names[i] != NULL)
                names.push_back(std::string("event.") + (*resolver)->names[i]);
    g_cases++;
    std::string original, port;
    Both(kEntries, 3, [] {}, [&](bool orig, std::string &log) {
        for (const std::string &name : names) {
            bool answer = false;
            int id = (orig ? (SymbolResolverFn)0x00017010 : ActManager::SymbolResolver)(name.c_str(), &answer);
            Append(log, "%s=%d/%d ", name.c_str(), id, answer);
        }
    }, original, port);
    Check(original == port, "ActManager::SymbolResolver", 0);

    ActManager *manager = ShadowActManager;
    if (manager == NULL)
        return;
    if (ActModelDatabase *models = manager->models) {
        g_cases++;
        Both(kEntries, 3, [] {}, [&](bool orig, std::string &log) {
            for (int i = 0; i < models->count; i++) {
                ActCharacterInfo info;
                memset(&info, 0, sizeof(info));
                info.name = models->models[i]->name;
                ActModel *model = orig ? ((UseModelFn)0x00017840)(models, 0, &info) : models->UseModel(&info);
                Append(log, "%s=%d ", info.name, model == models->models[i]->model);
            }
        }, original, port);
        Check(original == port, "ActModelDatabase::UseModel", 0);
    }
    if (ActTextureDatabase *textures = manager->textures) {
        g_cases++;
        Both(kEntries, 3, [] {}, [&](bool orig, std::string &log) {
            for (int i = 0; i < textures->count; i++) {
                const char *name = textures->textures[i]->name;
                EAGL::TAR *tar = orig ? ((UseTextureFn)0x00019e30)(textures, 0, name) : textures->UseTexture(name);
                Append(log, "%s=%d ", name, tar == textures->textures[i]->tar);
            }
        }, original, port);
        Check(original == port, "ActTextureDatabase::UseTexture", 0);
    }
}

}  // namespace

void ActModelShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_ACTMODELSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);
    TestTrees();
    TestSkeletons();
    TestBlends();
    TestSkeletonLoads();
    TestMatrices();
    TestIRMode();
    TestLookups();
    printf("[actmodelshadow] ActManager, ActModel(Database), ActSkeleton(Database), ActTextureDatabase, their trees: "
           "%ld cases, %ld checks, %ld differ%s\n", g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    fflush(stdout);
}
