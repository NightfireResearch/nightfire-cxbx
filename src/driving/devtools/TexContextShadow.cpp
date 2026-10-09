#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "TexContextShadow.h"
#include "FpControl.h"

#include "../anim/AnimEngine.h"           // TickSeconds
#include "../eagl/Realgraph.h"            // SHAPE_locatez
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"
#include "../render/RenderTree.h"
#include "../render/SkeletalObj.h"
#include "../render/StateManager.h"
#include "../render/TextureContext.h"
#include "../render/TimeData.h"
#include "../world/SoundMap.h"            // BuyMapHead, RefCounterMapBuyHead
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_TEXCONTEXTSHADOW=1, from the first simulation tick: the R_D ports against the originals (the package's
// entry bytes swapped back in for each original call, common/xbeOriginal.h, so an original calls the other
// originals and a port the other ports), on identical inputs, compared byte for byte (floats: equal bits, or both
// NaN). Nothing here reaches D3D: no TAR is made, no file loaded.
//
//   - The state handlers, each on random states with the words of every table, junk, numbers, colours and commas;
//     FUN_000914f0 (the naked adaptor, EDX kept) on tables, missing names, a null name and a null table.
//   - ParseFirstStateTag on random state strings: leading junk, tags and values past their caps, no '=', no ';',
//     empty pairs, a null text; the answer (as an offset), the state and *handled.
//   - RStateManager: the original constructor beside ours, then batches of state strings (repeats among them)
//     through GetGeoPrimState and the namespace thunk: the slot each answers, the count, the states and the set's
//     whole tree (shape, colours, slots, strings); the destructors after.
//   - RTexList on scratch maps: insert_unique (repeated keys), find, lower_bound, erase at an iterator and over a
//     range, the destructor; the trees compared after each phase.
//   - RTextureContext over the live contexts' files: FindWithoutOwning, Find and NameLookup ("NAME", "NAME::FLGS",
//     shapes the file has) on fresh lists, the answers and the lists compared. GetContext for keys -2..9.
//   - URefCounter<RTextureContext>'s tree on scratch maps (names differing in case), the three node makers.
//   - UpdateSpringMassSystem on synthetic bodies and random springs (starting or not, both limit signs, zero mass,
//     released, bouncing), the time step perturbed; AllocateSkeleton sequences.
//   - RTimeData::Init and Update, the step count perturbed (past 2^31 too); the globals put back.
// Not covered (lockstep runs): the contexts' constructor, Unload and destructor, FindOrCreateTexture (TARs), the
// manager's NewContext/KillContext/destructor, RSkeletalObj's constructor, PostLoad and UpdatePosition.
//
// One mutation this catches: the spring's pull towards its limit applied for a negative limit too changes the
// angle of every negative-limit case; KeyLess comparing the flags before the name shows in the texture lists.
// ---------------------------------------------------------------------------------------------------------------

namespace {

void OriginalWindow(bool original) {
    XbeOriginal_RestoreRange(0x00090b90, 0x000953e0, original);
}

// ---- the originals

typedef bool (*HandlerFn)(const char *, const char *, EAGL::GeoPrimState *, int);
typedef const char *(__fastcall *ParseFn)(RStateManager *, int, const char *, EAGL::GeoPrimState *, int, bool *);
typedef RStateManager *(__fastcall *ManagerConstructFn)(RStateManager *, int);
typedef void (__fastcall *ManagerDestructFn)(RStateManager *, int);
typedef EAGL::GeoPrimState *(__fastcall *GetStateFn)(RStateManager *, int, const char *);
typedef void *(__fastcall *NameLookupFn)(void *, int, const char *, int *);
typedef TexListInsert *(__fastcall *TexInsertFn)(RTexList *, int, TexListInsert *, const TexListValue *);
typedef TexListNode **(__fastcall *TexFindFn)(RTexList *, int, TexListNode **, const TexListKey *);
typedef TexListNode *(__fastcall *TexBoundFn)(RTexList *, int, const TexListKey *);
typedef TexListNode **(__fastcall *TexEraseFn)(RTexList *, int, TexListNode **, TexListNode *);
typedef TexListNode **(__fastcall *TexEraseRangeFn)(RTexList *, int, TexListNode **, TexListNode *, TexListNode *);
typedef void (__fastcall *TexDestructFn)(RTexList *, int);
typedef uint8_t *(__fastcall *ContextFindFn)(RTextureContext *, int, uint32_t, uint32_t);
typedef uint8_t *(__fastcall *ContextLookupFn)(RTextureContext *, int, const char *, int *);
typedef RTextureContext *(*GetContextFn)(int32_t);
typedef RefCounterInsertResult *(__fastcall *RefInsertFn)(URefCounterMap *, int, RefCounterInsertResult *,
                                                          const RefCounterValue *);
typedef RefCounterNode **(__fastcall *RefEraseFn)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *);
typedef RefCounterNode **(__fastcall *RefEraseRangeFn)(URefCounterMap *, int, RefCounterNode **, RefCounterNode *,
                                                       RefCounterNode *);
typedef void (__fastcall *RefDestructFn)(URefCounterMap *, int);
typedef RefCounterNode *(__fastcall *RefBuyNodeFn)(void *, int, RefCounterNode *, RefCounterNode *,
                                                   RefCounterNode *, const RefCounterValue *, int);
typedef TextureContextNode *(__fastcall *MapBuyNodeFn)(void *, int, TextureContextNode *, TextureContextNode *,
                                                       TextureContextNode *, const TextureContextEntry *, int);
typedef StateRefNode *(__fastcall *StateBuyNodeFn)(void *, int, StateRefNode *, StateRefNode *, StateRefNode *,
                                                   const StateRefValue *, int);
typedef bool (__fastcall *SpringFn)(RSkeletalObj *, int, RigidBody *, CARP::Instance *, const Coord3 *,
                                    const Coord3 *, float, float, float, float *, float *);
typedef void (__fastcall *AllocateFn)(RSkeletalObj *, int, uint32_t);
typedef void (*TimeFn)(void);

#define Orig_Parse ((ParseFn)0x00092280)
#define Orig_ManagerConstruct ((ManagerConstructFn)0x00092f50)
#define Orig_ManagerDestruct ((ManagerDestructFn)0x000930a0)
#define Orig_GetGeoPrimState ((GetStateFn)0x00092d20)
#define Orig_StateNameLookup ((NameLookupFn)0x00092f30)
#define Orig_TexInsert ((TexInsertFn)0x000938d0)
#define Orig_TexFind ((TexFindFn)0x000932b0)
#define Orig_TexLowerBound ((TexBoundFn)0x000931c0)
#define Orig_TexErase ((TexEraseFn)0x00093600)
#define Orig_TexEraseRange ((TexEraseRangeFn)0x000939d0)
#define Orig_TexDestruct ((TexDestructFn)0x00093d90)
#define Orig_FindWithoutOwning ((ContextFindFn)0x00093b20)
#define Orig_ContextFind ((ContextFindFn)0x00093c70)
#define Orig_ContextNameLookup ((ContextLookupFn)0x00093cd0)
#define Orig_GetContext ((GetContextFn)0x000940c0)
#define Orig_RefInsert ((RefInsertFn)0x00094df0)
#define Orig_RefErase ((RefEraseFn)0x000946b0)
#define Orig_RefEraseRange ((RefEraseRangeFn)0x00094c10)
#define Orig_RefDestruct ((RefDestructFn)0x00094ff0)
#define Orig_RefBuyNode ((RefBuyNodeFn)0x00093f60)
#define Orig_MapBuyNode ((MapBuyNodeFn)0x00093f10)
#define Orig_StateBuyNode ((StateBuyNodeFn)0x00093260)
#define Orig_Spring ((SpringFn)0x00090cf0)
#define Orig_AllocateSkeleton ((AllocateFn)0x00090c80)
#define Orig_TimeInit ((TimeFn)0x00095380)
#define Orig_TimeUpdate ((TimeFn)0x000953a0)

#define ShadowSimTimeStep FLOAT_AT(0x00234e30)
#define ShadowSimStepCount U32_AT(0x00234e34)

// FUN_000914f0's convention: EDX the name, the table on the stack. Answers the result, or (the second) EDX after.
__declspec(naked) const void *CallWithNameInEdx(const void *, const char *, const void *) {
    __asm {
        mov edx, [esp + 8]
        push dword ptr [esp + 12]
        call dword ptr [esp + 8]
        add esp, 4
        ret
    }
}

__declspec(naked) const void *EdxAfterCall(const void *, const char *, const void *) {
    __asm {
        mov edx, [esp + 8]
        push dword ptr [esp + 12]
        call dword ptr [esp + 8]
        add esp, 4
        mov eax, edx
        ret
    }
}

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0, g_nanOnly = 0;
unsigned int g_x87 = 0, g_sse = 0;

// How many differ under each label, printed after the summary (the detail lines show only the first ten)
struct Tally {
    const char *what;
    int count;
};
Tally g_tallies[48];
int g_tallyCount = 0;

void Count(const char *what) {
    for (int i = 0; i < g_tallyCount; i++) {
        if (strcmp(g_tallies[i].what, what) == 0) {
            g_tallies[i].count++;
            return;
        }
    }
    if (g_tallyCount < int(sizeof(g_tallies) / sizeof(g_tallies[0])))
        g_tallies[g_tallyCount++] = { what, 1 };
}

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    Count(what);
    if (g_details++ < 10)
        printf("[texctx]   %s #%d: %s\n", what, index, detail);
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

void CheckFloats(const char *what, int index, const float *a, const float *b, int count) {
    for (int i = 0; i < count; i++) {
        g_checks++;
        if (memcmp(&a[i], &b[i], sizeof(float)) == 0)
            continue;
        if (a[i] != a[i] && b[i] != b[i]) {
            g_nanOnly++;
            continue;
        }
        char detail[96];
        snprintf(detail, sizeof(detail), "float %d of %d: original %.9g, port %.9g", i, count, a[i], b[i]);
        Differ(what, index, detail);
    }
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

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

// ---- random inputs (our own generator)

uint32_t g_seed = 0x7e4c0a17;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

uint32_t Below(uint32_t n) {
    return Next() % n;
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f;
}

// ---- trees compared by shape, colour and value

template <class Node, class Same>
void CompareSubtree(const char *what, int index, const Node *a, const Node *b, Same same) {
    g_checks++;
    if (a->isNil != b->isNil) {
        Differ(what, index, "a node against nil");
        return;
    }
    if (a->isNil)
        return;
    CheckU32(what, index, a->color, b->color);
    if (!same(a->value, b->value))
        Differ(what, index, "values differ");
    CompareSubtree(what, index, a->left, b->left, same);
    CompareSubtree(what, index, a->right, b->right, same);
}

template <class Tree, class Same>
void CompareTrees(const char *what, int index, const Tree *a, const Tree *b, Same same) {
    CheckU32(what, index, a->size, b->size);
    CompareSubtree(what, index, a->head->parent, b->head->parent, same);
    g_checks++;
    if ((a->head->left == a->head) != (b->head->left == b->head) ||
        (a->head->left != a->head && !same(a->head->left->value, b->head->left->value)) ||
        (a->head->right != a->head && !same(a->head->right->value, b->head->right->value)))
        Differ(what, index, "the head's links");
}

// The test's trees start zeroed: the three bytes after the allocator's are padding nothing writes, and the whole
// object is compared once destroyed.
template <class Tree>
void ConstructTree(Tree *tree) {
    memset(tree, 0, sizeof(*tree));
    RenderTree::Construct(tree, BuyMapHead<typename Tree::Node>());
}

// =============================================================================================================
// The state handlers and the word lookup
// =============================================================================================================

struct HandlerEntry {
    uint32_t original;
    HandlerFn port;
};

const HandlerEntry kHandlers[] = {
    {0x00091560, StateTagPrimType}, {0x00091680, StateTagCull}, {0x00091700, StateTagTexture},
    {0x00091780, StateTagTexCoord}, {0x00091800, StateTagDepthTest}, {0x00091900, StateTagAlphaTestMethod},
    {0x00091a00, StateTagTransparency}, {0x00091a90, StateTagAlphaBlendMode}, {0x00091b70, StateTagAlphaTest},
    {0x00091bf0, StateTagTexAlpha}, {0x00091cf0, StateTagGlareGen}, {0x00091d20, StateTagFillMode},
    {0x00091db0, StateTagAlphaBlendXbox}, {0x00092030, StateTagZSlope}, {0x00092080, StateTagZBias},
    {0x00092140, StateTagChroma}, {0x00092170, StateTagModify},
};

const char *const kWords[] = {
    "pointlist", "linelist", "lineloop", "trilist", "tristrip", "trifan", "quadlist", "quadstrip", "polygon",
    "sprite", "off", "on", "stq", "uv", "never", "always", "notequal", "less", "lequal", "equal", "gequal",
    "greater", "opaque", "alpha", "chroma", "blend", "add", "atten", "mod", "sub", "custom", "noA", "deepA", "1bitA",
    "point", "wireframe", "solid", "revsub", "min", "max", "adds", "revsubs", "Zero", "One", "SrcCol", "chromemap",
    "glossmap", "envmap", "specmap", "watermap", "glassdmgmap", "bumpmap", "shadowmap", "glaregen", "", "ON", "Off",
    "x", "1.5", "-0.25", "3e2", "0", "0x00ff00ff", "0x8", "0xg", "add,add,add", "add,sub", "One,Zero,add",
    "max,max", ",add,", "trilist ", " on",
};
const int kWordCount = sizeof(kWords) / sizeof(kWords[0]);

struct HandlerCase {
    HandlerFn function;
    const char *value;
    int pass;
    EAGL::GeoPrimState state;
    bool result;
};

void RunHandler(void *context, bool) {
    HandlerCase *c = static_cast<HandlerCase *>(context);
    c->result = c->function("tag", c->value, &c->state, c->pass);
}

void TestHandlers() {
    for (int h = 0; h < int(sizeof(kHandlers) / sizeof(kHandlers[0])); h++) {
        for (int n = 0; n < 160; n++) {
            HandlerCase a, b;
            memset(&a, 0, sizeof(a));
            uint8_t *bytes = reinterpret_cast<uint8_t *>(&a.state);
            for (size_t i = 0; i < sizeof(a.state); i++)
                bytes[i] = uint8_t(Next());
            if (Below(2))
                a.state.alphaTestEnable = uint8_t(Below(2));
            a.value = n < kWordCount ? kWords[n] : kWords[Below(kWordCount)];
            a.pass = int(Below(2));
            b = a;
            a.function = reinterpret_cast<HandlerFn>(kHandlers[h].original);
            b.function = kHandlers[h].port;
            g_cases++;
            if (!Guarded(RunHandler, &a, true) || !Guarded(RunHandler, &b, false))
                continue;
            CheckBytes("handler state", h * 1000 + n, &a.state, &b.state, sizeof(a.state));
            CheckU32("handler answer", h * 1000 + n, a.result, b.result);
        }
    }
}

struct FindCase {
    const char *name;
    const StateKeyword *table;
    const void *result;
    const void *edx;
};

void RunFind(void *context, bool original) {
    FindCase *c = static_cast<FindCase *>(context);
    const void *function = original ? reinterpret_cast<const void *>(0x000914f0)
                                    : reinterpret_cast<const void *>(&FUN_000914f0);
    c->result = CallWithNameInEdx(function, c->name, c->table);
    c->edx = EdxAfterCall(function, c->name, c->table);
}

void TestFind() {
    static const StateKeyword table[] = { {"off", 0}, {"on", 1}, {"add", 2}, {"adds", 3}, {"", 4}, {NULL, 5} };
    static const StateKeyword empty[] = { {NULL, 0} };
    const char *names[] = { "off", "on", "add", "adds", "", "ad", "addss", "OFF", "x", NULL };
    for (int t = 0; t < 3; t++) {
        for (int n = 0; n < int(sizeof(names) / sizeof(names[0])); n++) {
            FindCase a = { names[n], t == 0 ? table : t == 1 ? empty : NULL, NULL, NULL };
            FindCase b = a;
            g_cases++;
            if (!Guarded(RunFind, &a, true) || !Guarded(RunFind, &b, false))
                continue;
            CheckU32("word lookup", t * 100 + n, uint32_t(uintptr_t(a.result)), uint32_t(uintptr_t(b.result)));
            CheckU32("word lookup EDX", t * 100 + n, uint32_t(uintptr_t(a.edx)), uint32_t(uintptr_t(b.edx)));
        }
    }
}

// =============================================================================================================
// ParseFirstStateTag and the manager
// =============================================================================================================

const char *const kTags[] = {
    "modify", "primtype", "shading", "cull", "texture", "texcoord", "depthtest", "alphatestmethod", "alphabm",
    "transparency", "chroma", "alphatest", "texalpha", "fillmode", "alphablendxbox", "zslope", "zbias", "bogus",
    "Cull", "alphatestmethodalphatestmethodalphatestmethod",
};
const int kTagCount = sizeof(kTags) / sizeof(kTags[0]);

// A random state string of `pairs` pairs into `out`
void MakeStateString(char *out, size_t size, int pairs) {
    out[0] = '\0';
    for (int p = 0; p < pairs; p++) {
        char pair[160];
        const char *prefix = Below(4) == 0 ? (Below(2) ? " ;" : "--") : "";
        const char *tag = kTags[Below(kTagCount)];
        const char *value = kWords[Below(kWordCount)];
        switch (Below(10)) {
        case 0:
            snprintf(pair, sizeof(pair), "%s%s", prefix, tag);                   // no '='
            break;
        case 1:
            snprintf(pair, sizeof(pair), "%s%s=%s%s%s", prefix, tag, value, value, "0123456789012345678901234567890123456789012345678901234567890123");
            break;
        case 2:
            snprintf(pair, sizeof(pair), "%s=%s", prefix, value);                 // no tag
            break;
        default:
            snprintf(pair, sizeof(pair), "%s%s=%s", prefix, tag, value);
            break;
        }
        if (strlen(out) + strlen(pair) + 2 >= size)
            break;
        strcat(out, pair);
        if (p + 1 < pairs || Below(2))
            strcat(out, ";");
    }
}

struct ParseCase {
    const char *text;
    int pass;
    EAGL::GeoPrimState state;
    uint8_t handled;
    const char *answer;
};

void RunParse(void *context, bool original) {
    ParseCase *c = static_cast<ParseCase *>(context);
    bool *handled = reinterpret_cast<bool *>(&c->handled);
    RStateManager unused;               // ParseFirstStateTag reads nothing of its object
    memset(&unused, 0, sizeof(unused));
    c->answer = original ? Orig_Parse(&unused, 0, c->text, &c->state, c->pass, handled)
                         : unused.ParseFirstStateTag(c->text, &c->state, c->pass, handled);
}

void TestParse() {
    static char texts[400][600];
    for (int n = 0; n < 400; n++) {
        MakeStateString(texts[n], sizeof(texts[n]), 1 + int(Below(4)));
        for (const char *at = n == 0 ? NULL : texts[n]; ; ) {
            ParseCase a;
            memset(&a, 0, sizeof(a));
            a.text = at;
            a.pass = int(Below(2));
            a.state.Construct();
            a.handled = 0x5a;
            ParseCase b = a;
            g_cases++;
            if (!Guarded(RunParse, &a, true) || !Guarded(RunParse, &b, false))
                break;
            CheckU32("parse answer", n, a.answer == NULL ? 0xffffffffu : uint32_t(a.answer - a.text),
                     b.answer == NULL ? 0xffffffffu : uint32_t(b.answer - b.text));
            CheckBytes("parse state", n, &a.state, &b.state, sizeof(a.state));
            CheckU32("parse handled", n, a.handled, b.handled);
            if (a.answer == NULL || *a.answer == '\0' || a.answer != NULL && b.answer != NULL &&
                                                            a.answer - a.text != b.answer - b.text)
                break;
            at = a.answer;
        }
    }
}

struct ManagerCase {
    RStateManager *manager;
    const char *text;
    EAGL::GeoPrimState *state;
    void *looked;
    int size;
    bool constructing, destructing, lookup;
};

void RunManager(void *context, bool original) {
    ManagerCase *c = static_cast<ManagerCase *>(context);
    if (c->constructing) {
        if (original)
            Orig_ManagerConstruct(c->manager, 0);
        else
            c->manager->Construct();
    } else if (c->destructing) {
        if (original)
            Orig_ManagerDestruct(c->manager, 0);
        else
            c->manager->Destruct();
    } else if (c->lookup) {
        c->looked = original ? Orig_StateNameLookup(&c->manager->symbols, 0, c->text, &c->size)
                             : c->manager->symbols.NameLookup(c->text, &c->size);
    } else {
        c->state = original ? Orig_GetGeoPrimState(c->manager, 0, c->text) : c->manager->GetGeoPrimState(c->text);
    }
}

void TestManager() {
    static char texts[24][400];
    for (int batch = 0; batch < 12; batch++) {
        RStateManager managerA, managerB;
        memset(&managerA, 0, sizeof(managerA));
        memset(&managerB, 0, sizeof(managerB));
        ManagerCase a = { &managerA, NULL, NULL, NULL, 0, true, false, false };
        ManagerCase b = a;
        b.manager = &managerB;
        g_cases++;
        if (!Guarded(RunManager, &a, true) || !Guarded(RunManager, &b, false))
            continue;
        CheckBytes("manager vtables", batch, &managerA, &managerB, 12);
        const int kStrings = 20;
        for (int s = 0; s < kStrings; s++) {
            if (s > 0 && Below(4) == 0)
                strcpy(texts[s], texts[Below(s)]);
            else
                MakeStateString(texts[s], sizeof(texts[s]), 1 + int(Below(3)));
        }
        for (int s = 0; s < kStrings + 6; s++) {
            const char *text = s < kStrings ? texts[s] : texts[Below(kStrings)];
            if (s == kStrings + 5)
                text = NULL;
            a = { &managerA, text, NULL, NULL, 0, false, false, s % 5 == 4 };
            b = a;
            b.manager = &managerB;
            g_cases++;
            if (!Guarded(RunManager, &a, true) || !Guarded(RunManager, &b, false))
                continue;
            EAGL::GeoPrimState *stateA = a.lookup ? static_cast<EAGL::GeoPrimState *>(a.looked) : a.state;
            EAGL::GeoPrimState *stateB = b.lookup ? static_cast<EAGL::GeoPrimState *>(b.looked) : b.state;
            CheckU32("state slot", batch * 100 + s, uint32_t(stateA - managerA.states),
                     uint32_t(stateB - managerB.states));
            CheckU32("state count", batch * 100 + s, managerA.count, managerB.count);
            CheckU32("lookup size", batch * 100 + s, a.size, b.size);
            CheckBytes("state", batch * 100 + s, stateA, stateB, sizeof(EAGL::GeoPrimState));
        }
        CheckBytes("states", batch, managerA.states, managerB.states, managerA.count * sizeof(EAGL::GeoPrimState));
        const EAGL::GeoPrimState *baseA = managerA.states, *baseB = managerB.states;
        CompareTrees("state set", batch, managerA.lookup, managerB.lookup,
                     [&](const StateRefValue &x, const StateRefValue &y) {
                         return static_cast<const EAGL::GeoPrimState *>(x.state) - baseA ==
                                    static_cast<const EAGL::GeoPrimState *>(y.state) - baseB &&
                                memcmp(x.name, y.name, sizeof(x.name)) == 0;
                     });
        a = { &managerA, NULL, NULL, NULL, 0, false, true, false };
        b = a;
        b.manager = &managerB;
        Guarded(RunManager, &a, true);
        Guarded(RunManager, &b, false);
        CheckBytes("destroyed manager", batch, &managerA, &managerB, 12);
    }
}

// =============================================================================================================
// RTexList, RTextureContext, the manager's queries
// =============================================================================================================

// The padding after `used` is left out: CreateNewTextureElement's original builds the entry on its stack without
// writing those three bytes (0x00093ac2..0x00093ae6), so the node's copy holds whatever the stack had there.
bool SameTexEntry(const TexListValue &x, const TexListValue &y) {
    return memcmp(&x.key, &y.key, sizeof(x.key)) == 0 && x.shape == y.shape && x.tar == y.tar && x.used == y.used;
}

struct TexCase {
    RTexList *list;
    int op;                     // 0 insert, 1 find, 2 lower_bound, 3 erase, 4 erase range, 5 destruct
    TexListValue value;
    TexListNode *where, *last;
    TexListInsert inserted;
    TexListNode *answer;
};

void RunTex(void *context, bool original) {
    TexCase *c = static_cast<TexCase *>(context);
    switch (c->op) {
    case 0:
        if (original)
            Orig_TexInsert(c->list, 0, &c->inserted, &c->value);
        else
            c->list->InsertUnique(&c->inserted, &c->value);
        break;
    case 1:
        if (original)
            Orig_TexFind(c->list, 0, &c->answer, &c->value.key);
        else
            c->list->Find(&c->answer, &c->value.key);
        break;
    case 2:
        c->answer = original ? Orig_TexLowerBound(c->list, 0, &c->value.key) : c->list->LowerBound(&c->value.key);
        break;
    case 3:
        if (original)
            Orig_TexErase(c->list, 0, &c->answer, c->where);
        else
            c->list->EraseAt(&c->answer, c->where);
        break;
    case 4:
        if (original)
            Orig_TexEraseRange(c->list, 0, &c->answer, c->where, c->last);
        else
            c->list->EraseRange(&c->answer, c->where, c->last);
        break;
    default:
        if (original)
            Orig_TexDestruct(c->list, 0);
        else
            c->list->Destruct();
        break;
    }
}

// The node's key as a comparable word (the end: all ones)
uint64_t KeyOf(const RTexList *list, const TexListNode *node) {
    return node == list->head ? ~uint64_t(0) : uint64_t(node->value.key.name) << 32 | node->value.key.flags;
}

TexListKey RandomTexKey() {
    TexListKey key = { 0x41414141u + Below(6), Below(3) };
    return key;
}

void TestTexList() {
    for (int round = 0; round < 8; round++) {
        RTexList listA, listB;
        ConstructTree(&listA);
        ConstructTree(&listB);
        int index = round * 1000;
        for (int n = 0; n < 30; n++, index++) {
            TexCase a;
            memset(&a, 0, sizeof(a));
            a.list = &listA;
            a.op = 0;
            a.value.key = RandomTexKey();
            a.value.shape = reinterpret_cast<uint8_t *>(uintptr_t(Next()));
            a.value.used = uint8_t(Below(2));
            TexCase b = a;
            b.list = &listB;
            g_cases++;
            if (!Guarded(RunTex, &a, true) || !Guarded(RunTex, &b, false))
                continue;
            CheckU32("texlist inserted", index, a.inserted.inserted, b.inserted.inserted);
            g_checks++;
            if (KeyOf(&listA, a.inserted.node) != KeyOf(&listB, b.inserted.node))
                Differ("texlist insert node", index, "keys differ");
        }
        CompareTrees("texlist after inserts", round, &listA, &listB, SameTexEntry);
        for (uint32_t name = 0x41414140u; name < 0x41414148u; name++) {
            for (uint32_t flags = 0; flags < 4; flags++, index++) {
                for (int op = 1; op <= 2; op++) {
                    TexCase a;
                    memset(&a, 0, sizeof(a));
                    a.list = &listA;
                    a.op = op;
                    a.value.key.name = name;
                    a.value.key.flags = flags;
                    TexCase b = a;
                    b.list = &listB;
                    g_cases++;
                    if (!Guarded(RunTex, &a, true) || !Guarded(RunTex, &b, false))
                        continue;
                    g_checks++;
                    if (KeyOf(&listA, a.answer) != KeyOf(&listB, b.answer))
                        Differ(op == 1 ? "texlist find" : "texlist lower_bound", index, "keys differ");
                }
            }
        }
        // Erase about half, one at a time, then a range from the second node to the last
        for (int n = 0; n < 8 && listA.size > 0; n++, index++) {
            TexListKey key = RandomTexKey();
            TexListNode *whereA, *whereB;
            listA.Find(&whereA, &key);
            listB.Find(&whereB, &key);
            if (whereA == listA.head || whereB == listB.head)
                continue;
            TexCase a;
            memset(&a, 0, sizeof(a));
            a.list = &listA;
            a.op = 3;
            a.where = whereA;
            TexCase b = a;
            b.list = &listB;
            b.where = whereB;
            g_cases++;
            if (!Guarded(RunTex, &a, true) || !Guarded(RunTex, &b, false))
                continue;
            g_checks++;
            if (KeyOf(&listA, a.answer) != KeyOf(&listB, b.answer))
                Differ("texlist erase next", index, "keys differ");
        }
        CompareTrees("texlist after erases", round, &listA, &listB, SameTexEntry);
        if (listA.size > 2 && round % 2 == 0) {
            TexCase a;
            memset(&a, 0, sizeof(a));
            a.list = &listA;
            a.op = 4;
            a.where = RenderTree::Next(listA.head->left);
            a.last = listA.head->right;
            TexCase b = a;
            b.list = &listB;
            b.where = RenderTree::Next(listB.head->left);
            b.last = listB.head->right;
            g_cases++;
            if (Guarded(RunTex, &a, true) && Guarded(RunTex, &b, false)) {
                g_checks++;
                if (KeyOf(&listA, a.answer) != KeyOf(&listB, b.answer))
                    Differ("texlist erase range", round, "keys differ");
                CompareTrees("texlist after the range", round, &listA, &listB, SameTexEntry);
            }
        }
        TexCase a;
        memset(&a, 0, sizeof(a));
        a.list = &listA;
        a.op = 5;
        TexCase b = a;
        b.list = &listB;
        g_cases++;
        Guarded(RunTex, &a, true);
        Guarded(RunTex, &b, false);
        CheckBytes("texlist destroyed", round, &listA, &listB, sizeof(listA));
    }
}

struct ContextCase {
    RTextureContext *context;
    int op;                     // 0 FindWithoutOwning, 1 Find, 2 NameLookup
    uint32_t name, flags;
    const char *symbol;
    uint8_t *answer;
};

void RunContext(void *context, bool original) {
    ContextCase *c = static_cast<ContextCase *>(context);
    int size = 0;
    switch (c->op) {
    case 0:
        c->answer = original ? Orig_FindWithoutOwning(c->context, 0, c->name, c->flags)
                             : c->context->FindWithoutOwning(c->name, c->flags);
        break;
    case 1:
        c->answer = original ? Orig_ContextFind(c->context, 0, c->name, c->flags) : c->context->Find(c->name, c->flags);
        break;
    default:
        c->answer = original ? Orig_ContextNameLookup(c->context, 0, c->symbol, &size)
                             : c->context->NameLookup(c->symbol, &size);
        break;
    }
}

RTexList *NewList() {
    RTexList *list = static_cast<RTexList *>(UMemory::FastAlloc(sizeof(RTexList), "RTexList"));
    ConstructTree(list);
    return list;
}

void FreeList(RTexList *list) {
    list->Destruct();
    UMemory::FastFree(list, sizeof(RTexList));
}

bool HasShape(uint8_t *shapes, uint32_t name) {
    char text[5];
    memcpy(text, &name, 4);
    text[4] = '\0';
    return SHAPE_locatez(shapes, text) != NULL;
}

void TestContexts() {
    RTextureContextManager *manager = TheTextureContextManager;
    if (manager == NULL || manager->contexts == NULL) {
        printf("[texctx] no texture context manager - the contexts skipped\n");
        return;
    }
    int tested = 0;
    for (TextureContextNode *node = manager->contexts->head->left; node != manager->contexts->head && tested < 4;
         node = RenderTree::Next(node)) {
        RTextureContext *live = node->value.context;
        if (live == NULL || live->shapes == NULL || live->textures == NULL)
            continue;
        tested++;
        // Names: the live list's, some changed by a character, some random
        uint32_t names[48];
        int nameCount = 0;
        for (TexListNode *entry = live->textures->head->left; entry != live->textures->head && nameCount < 32;
             entry = RenderTree::Next(entry))
            names[nameCount++] = entry->value.key.name;
        while (nameCount < 48) {
            uint32_t name = nameCount % 3 == 0 && nameCount > 0 ? names[Below(nameCount)] ^ 0x01000000u
                                                                : 0x41414141u + Next() % 0x1a1a1a1au;
            names[nameCount++] = name;
        }
        RTextureContext contextA = { live->vtable, 0, live->shapes, NewList() };
        RTextureContext contextB = { live->vtable, 0, live->shapes, NewList() };
        for (int n = 0; n < 160; n++) {
            ContextCase a;
            memset(&a, 0, sizeof(a));
            a.context = &contextA;
            a.op = int(Below(3));
            a.name = names[Below(nameCount)];
            a.flags = Below(4) == 0 ? 0x3a3a3a3au + Below(3) : 0;
            char symbol[16];
            if (a.op == 2) {
                if (!HasShape(live->shapes, a.name))
                    a.op = 0;
                memcpy(symbol, &a.name, 4);
                symbol[4] = '\0';
                if (a.flags != 0) {
                    symbol[4] = ':';
                    symbol[5] = ':';
                    memcpy(symbol + 6, &a.flags, 4);
                    symbol[10] = '\0';
                }
                a.symbol = symbol;
            }
            ContextCase b = a;
            b.context = &contextB;
            g_cases++;
            if (!Guarded(RunContext, &a, true) || !Guarded(RunContext, &b, false))
                continue;
            CheckU32("context answer", tested * 1000 + n, uint32_t(uintptr_t(a.answer)), uint32_t(uintptr_t(b.answer)));
        }
        CompareTrees("context list", tested, contextA.textures, contextB.textures, SameTexEntry);
        FreeList(contextA.textures);
        FreeList(contextB.textures);
    }
    for (int32_t key = -2; key <= 9; key++) {
        RTextureContext *original = NULL, *port = NULL;
        OriginalWindow(true);
        original = Orig_GetContext(key);
        OriginalWindow(false);
        port = RTextureContextManager::GetContext(key);
        g_cases++;
        CheckU32("GetContext", key, uint32_t(uintptr_t(original)), uint32_t(uintptr_t(port)));
    }
}

// =============================================================================================================
// The reference counter's tree and the node makers
// =============================================================================================================

bool SameRefEntry(const RefCounterValue &x, const RefCounterValue &y) {
    return memcmp(&x, &y, sizeof(x)) == 0;
}

struct RefCase {
    URefCounterMap *map;
    int op;                     // 0 insert, 1 erase, 2 erase range, 3 destruct
    RefCounterValue value;
    RefCounterNode *where, *last, *answer;
    RefCounterInsertResult inserted;
};

void RunRef(void *context, bool original) {
    RefCase *c = static_cast<RefCase *>(context);
    TextureContextRefTree *tree = static_cast<TextureContextRefTree *>(c->map);
    switch (c->op) {
    case 0:
        if (original)
            Orig_RefInsert(c->map, 0, &c->inserted, &c->value);
        else
            tree->InsertUnique(&c->inserted, &c->value);
        break;
    case 1:
        if (original)
            Orig_RefErase(c->map, 0, &c->answer, c->where);
        else
            tree->EraseAt(&c->answer, c->where);
        break;
    case 2:
        if (original)
            Orig_RefEraseRange(c->map, 0, &c->answer, c->where, c->last);
        else
            tree->EraseRange(&c->answer, c->where, c->last);
        break;
    default:
        if (original)
            Orig_RefDestruct(c->map, 0);
        else
            tree->Destruct();
        break;
    }
}

const char *const kRefNames[] = { "data\\a.xsh", "DATA\\A.XSH", "data\\b.xsh", "Data\\c.xsh", "e", "E", "f", "zz", "Zy" };

void MakeRefMap(URefCounterMap *map) {
    memset(map, 0, sizeof(*map));       // the padding after the allocator's byte too: the map is compared whole
    map->allocator = 0;
    map->head = RefCounterMapBuyHead();
    map->head->isNil = 1;
    map->head->parent = map->head->left = map->head->right = map->head;
    map->size = 0;
}

const char *NameOf(const URefCounterMap *map, const RefCounterNode *node) {
    return node == map->head ? "(end)" : node->value.name;
}

void TestRefTree() {
    for (int round = 0; round < 6; round++) {
        URefCounterMap mapA, mapB;
        MakeRefMap(&mapA);
        MakeRefMap(&mapB);
        for (int n = 0; n < 24; n++) {
            RefCase a;
            memset(&a, 0, sizeof(a));
            a.map = &mapA;
            strcpy(a.value.name, kRefNames[Below(sizeof(kRefNames) / sizeof(kRefNames[0]))]);
            a.value.entry.references = int32_t(Below(5));
            a.value.entry.object = reinterpret_cast<void *>(uintptr_t(Next()));
            RefCase b = a;
            b.map = &mapB;
            g_cases++;
            if (!Guarded(RunRef, &a, true) || !Guarded(RunRef, &b, false))
                continue;
            CheckU32("refs inserted", round * 100 + n, a.inserted.inserted, b.inserted.inserted);
            g_checks++;
            if (strcmp(NameOf(&mapA, a.inserted.node), NameOf(&mapB, b.inserted.node)) != 0)
                Differ("refs insert node", round * 100 + n, "names differ");
        }
        CompareTrees("refs after inserts", round, &mapA, &mapB, SameRefEntry);
        while (mapA.size > 2 && mapB.size > 2) {
            RefCounterNode *whereA = mapA.head->left, *whereB = mapB.head->left;
            for (uint32_t skip = Below(mapA.size); skip > 0; skip--) {
                whereA = RenderTree::Next(whereA);
                whereB = RenderTree::Next(whereB);
            }
            RefCase a;
            memset(&a, 0, sizeof(a));
            a.map = &mapA;
            a.op = 1;
            a.where = whereA;
            RefCase b = a;
            b.map = &mapB;
            b.where = whereB;
            g_cases++;
            if (!Guarded(RunRef, &a, true) || !Guarded(RunRef, &b, false))
                break;
            g_checks++;
            if (strcmp(NameOf(&mapA, a.answer), NameOf(&mapB, b.answer)) != 0)
                Differ("refs erase next", round, "names differ");
            CompareTrees("refs after an erase", round, &mapA, &mapB, SameRefEntry);
            if (round % 2 == 1)
                break;
        }
        if (round % 2 == 1 && mapA.size > 1) {
            RefCase a;
            memset(&a, 0, sizeof(a));
            a.map = &mapA;
            a.op = 2;
            a.where = mapA.head->left;
            a.last = mapA.head->right;
            RefCase b = a;
            b.map = &mapB;
            b.where = mapB.head->left;
            b.last = mapB.head->right;
            g_cases++;
            if (Guarded(RunRef, &a, true) && Guarded(RunRef, &b, false))
                CompareTrees("refs after the range", round, &mapA, &mapB, SameRefEntry);
        }
        RefCase a;
        memset(&a, 0, sizeof(a));
        a.map = &mapA;
        a.op = 3;
        RefCase b = a;
        b.map = &mapB;
        g_cases++;
        Guarded(RunRef, &a, true);
        Guarded(RunRef, &b, false);
        CheckBytes("refs destroyed", round, &mapA, &mapB, sizeof(mapA));
    }
}

struct BuyCase {
    int kind;
    void *node;
};

void RunBuy(void *context, bool original) {
    BuyCase *c = static_cast<BuyCase *>(context);
    RefCounterNode *refLink = reinterpret_cast<RefCounterNode *>(uintptr_t(0x11110000));
    TextureContextNode *mapLink = reinterpret_cast<TextureContextNode *>(uintptr_t(0x22220000));
    StateRefNode *stateLink = reinterpret_cast<StateRefNode *>(uintptr_t(0x33330000));
    static RefCounterValue refValue;
    strcpy(refValue.name, "shadow");
    refValue.entry.references = 7;
    refValue.entry.object = &refValue;
    const TextureContextEntry mapValue = { -3, reinterpret_cast<RTextureContext *>(uintptr_t(0x44440000)) };
    StateRefValue stateValue;
    stateValue.state = reinterpret_cast<EAGL::GeoPrimState *>(&stateValue);
    for (size_t i = 0; i < sizeof(stateValue.name); i++)
        stateValue.name[i] = char(i * 7 + 1);
    // the node makers read nothing of their objects
    TextureContextRefTree refTreeObject;
    TextureContextMap mapObject;
    StateRefTree setObject;
    memset(&refTreeObject, 0, sizeof(refTreeObject));
    memset(&mapObject, 0, sizeof(mapObject));
    memset(&setObject, 0, sizeof(setObject));
    TextureContextRefTree *refTree = &refTreeObject;
    TextureContextMap *map = &mapObject;
    StateRefTree *set = &setObject;
    switch (c->kind) {
    case 0:
        c->node = original ? Orig_RefBuyNode(refTree, 0, refLink, refLink + 1, refLink + 2, &refValue, 1)
                           : refTree->BuyNode(refLink, refLink + 1, refLink + 2, &refValue, 1);
        break;
    case 1:
        c->node = original ? Orig_MapBuyNode(map, 0, mapLink, mapLink + 1, mapLink + 2, &mapValue, 0)
                           : map->BuyNode(mapLink, mapLink + 1, mapLink + 2, &mapValue, 0);
        break;
    default:
        c->node = original ? Orig_StateBuyNode(set, 0, stateLink, stateLink + 1, stateLink + 2, &stateValue, 1)
                           : set->BuyNode(stateLink, stateLink + 1, stateLink + 2, &stateValue, 1);
        break;
    }
}

void TestNodeMakers() {
    // Compared up to the isNil byte: the node's last two bytes are padding neither maker writes (the pool's)
    const size_t sizes[3] = { sizeof(RefCounterNode), sizeof(TextureContextNode), sizeof(StateRefNode) };
    const size_t written[3] = { offsetof(RefCounterNode, isNil) + 1, offsetof(TextureContextNode, isNil) + 1,
                                offsetof(StateRefNode, isNil) + 1 };
    for (int kind = 0; kind < 3; kind++) {
        BuyCase a = { kind, NULL }, b = a;
        g_cases++;
        if (!Guarded(RunBuy, &a, true) || !Guarded(RunBuy, &b, false))
            continue;
        if (kind == 2) {
            // the value's state pointer is each run's own stack copy
            memset(static_cast<uint8_t *>(a.node) + 0xc, 0, 4);
            memset(static_cast<uint8_t *>(b.node) + 0xc, 0, 4);
        }
        CheckBytes("node maker", kind, a.node, b.node, written[kind]);
        UMemory::FastFree(a.node, unsigned(sizes[kind]));
        UMemory::FastFree(b.node, unsigned(sizes[kind]));
    }
}

// =============================================================================================================
// RSkeletalObj
// =============================================================================================================

struct SpringCase {
    RSkeletalObj *object;
    RigidBody *body;
    Coord3 point, axis;
    float threshold, mass, limit, angle, angularVelocity;
    bool result;
};

void RunSpring(void *context, bool original) {
    SpringCase *c = static_cast<SpringCase *>(context);
    c->result = original ? Orig_Spring(c->object, 0, c->body, NULL, &c->point, &c->axis, c->threshold, c->mass,
                                       c->limit, &c->angle, &c->angularVelocity)
                         : c->object->UpdateSpringMassSystem(c->body, NULL, &c->point, &c->axis, c->threshold,
                                                             c->mass, c->limit, &c->angle, &c->angularVelocity);
}

void TestSprings() {
    static RigidBodyInfo info;
    static RigidBody body;
    alignas(16) static uint8_t objectA[sizeof(RSkeletalObj)], objectB[sizeof(RSkeletalObj)];
    RSkeletalObj *a = reinterpret_cast<RSkeletalObj *>(objectA);
    RSkeletalObj *b = reinterpret_cast<RSkeletalObj *>(objectB);
    float savedStep = ShadowSimTimeStep;
    memset(&body, 0, sizeof(body));
    body.info = &info;
    for (int n = 0; n < 2000; n++) {
        float *m = &info.orientation.mtx[0][0];
        for (int i = 0; i < 16; i++)
            m[i] = Uniform(-1.0f, 1.0f);
        body.velocity = { Uniform(-40, 40), Uniform(-10, 10), Uniform(-40, 40) };
        body.angularVelocity = { Uniform(-3, 3), Uniform(-3, 3), Uniform(-3, 3) };
        ShadowSimTimeStep = Below(3) == 0 ? Uniform(0.001f, 0.1f) : Below(2) ? 1.0f / 60.0f : 1.0f / 50.0f;
        SpringCase c;
        memset(&c, 0, sizeof(c));
        c.body = &body;
        c.point = { Uniform(-2, 2), Uniform(-2, 2), Uniform(-2, 2) };
        c.axis = Below(40) == 0 ? Coord3{ 0, 0, 0 } : Coord3{ Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1) };
        c.threshold = Uniform(-5, 50);
        c.mass = Below(30) == 0 ? 0.0f : Uniform(0.1f, 20.0f);
        c.limit = Below(5) == 0 ? 0.0f : Uniform(-0.4f, 0.4f);
        c.angle = Below(4) == 0 ? c.limit * Uniform(0.9f, 1.1f) : Uniform(-0.5f, 0.5f);
        c.angularVelocity = Uniform(-2, 2);
        memset(objectA, 0, sizeof(objectA));
        Coord4 last = { Uniform(-30, 30), Uniform(-30, 30), Uniform(-30, 30), Below(4) == 0 ? -1.0f : 1.0f };
        Coord4 now = { Uniform(-30, 30), Uniform(-30, 30), Uniform(-30, 30), 1.0f };
        a->springPrevPos = last;
        a->springPos = now;
        memcpy(objectB, objectA, sizeof(objectA));
        SpringCase ca = c, cb = c;
        ca.object = a;
        cb.object = b;
        g_cases++;
        if (!Guarded(RunSpring, &ca, true) || !Guarded(RunSpring, &cb, false))
            continue;
        CheckU32("spring answer", n, ca.result, cb.result);
        CheckFloats("spring angle", n, &ca.angle, &cb.angle, 1);
        CheckFloats("spring angular velocity", n, &ca.angularVelocity, &cb.angularVelocity, 1);
        CheckFloats("spring velocities", n, &a->springPrevPos.x, &b->springPrevPos.x, 8);
    }
    ShadowSimTimeStep = savedStep;
}

struct AllocateCase {
    RSkeletalObj *object;
    uint32_t count;
};

void RunAllocate(void *context, bool original) {
    AllocateCase *c = static_cast<AllocateCase *>(context);
    if (original)
        Orig_AllocateSkeleton(c->object, 0, c->count);
    else
        c->object->AllocateSkeleton(c->count);
}

void TestAllocateSkeleton() {
    alignas(16) static uint8_t objectA[sizeof(RSkeletalObj)], objectB[sizeof(RSkeletalObj)];
    RSkeletalObj *a = reinterpret_cast<RSkeletalObj *>(objectA);
    RSkeletalObj *b = reinterpret_cast<RSkeletalObj *>(objectB);
    memset(objectA, 0, sizeof(objectA));
    memset(objectB, 0, sizeof(objectB));
    const uint32_t counts[] = { 3, 0, 7, 1, 40, 2, 0 };
    for (int n = 0; n < int(sizeof(counts) / sizeof(counts[0])); n++) {
        AllocateCase ca = { a, counts[n] }, cb = { b, counts[n] };
        g_cases++;
        if (!Guarded(RunAllocate, &ca, true) || !Guarded(RunAllocate, &cb, false))
            break;
        CheckU32("bones count", n, a->numBones, b->numBones);
        CheckU32("bones null", n, a->bones == NULL, b->bones == NULL);
        if (a->bones != NULL && b->bones != NULL)
            CheckBytes("bones", n, a->bones, b->bones, a->numBones * sizeof(MATRIX4));
    }
}

// =============================================================================================================
// RTimeData
// =============================================================================================================

struct TimeState {
    float videoModeRate, tickSeconds, simStep;
    uint32_t gameTick;
    float caustic[4];
};

TimeState CaptureTime() {
    TimeState s;
    s.videoModeRate = VideoModeRate;
    s.tickSeconds = TickSeconds;
    s.simStep = SimStep;
    s.gameTick = GameTick;
    memcpy(s.caustic, CausticControl, sizeof(s.caustic));
    return s;
}

void PutTime(const TimeState &s) {
    VideoModeRate = s.videoModeRate;
    TickSeconds = s.tickSeconds;
    SimStep = s.simStep;
    GameTick = s.gameTick;
    memcpy(CausticControl, s.caustic, sizeof(s.caustic));
}

struct TimeCase {
    bool init;
};

void RunTime(void *context, bool original) {
    TimeCase *c = static_cast<TimeCase *>(context);
    if (c->init)
        original ? Orig_TimeInit() : RTimeData::Init();
    else
        original ? Orig_TimeUpdate() : RTimeData::Update();
}

void TestTime() {
    TimeState saved = CaptureTime();
    uint32_t savedCount = ShadowSimStepCount;
    const uint32_t counts[] = { 0, 1, 12345, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu };
    for (int n = 0; n < 40; n++) {
        TimeCase c = { n == 0 };
        TimeState start = saved;
        start.caustic[0] = Uniform(-1, 1);
        ShadowSimStepCount = n < 7 ? counts[n] : Next() * 977u;
        PutTime(start);
        g_cases++;
        if (!Guarded(RunTime, &c, true))
            continue;
        TimeState original = CaptureTime();
        PutTime(start);
        if (!Guarded(RunTime, &c, false))
            continue;
        TimeState port = CaptureTime();
        CheckBytes("time data", n, &original, &port, sizeof(original));
    }
    ShadowSimStepCount = savedCount;
    PutTime(saved);
}

}  // namespace

void TexContextShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_TEXCONTEXTSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);
    TestHandlers();
    TestFind();
    TestParse();
    TestManager();
    TestTexList();
    TestContexts();
    TestRefTree();
    TestNodeMakers();
    TestSprings();
    TestAllocateSkeleton();
    TestTime();
    ResetFpu();
    printf("[texctx] state handlers, parser, state manager, texture lists and contexts, ref tree, springs, bones, "
           "time data vs originals: %d cases, %d checks, %d differ%s\n", g_cases, g_checks, g_differ,
           g_faults != 0 ? " (with faults)" : "");
    if (g_nanOnly != 0)
        printf("[texctx]   %d results both NaN with different bits\n", g_nanOnly);
    if (g_faults != 0)
        printf("[texctx]   %d calls faulted\n", g_faults);
    for (int i = 0; i < g_tallyCount; i++)
        printf("[texctx]   %s: %d differ\n", g_tallies[i].what, g_tallies[i].count);
    fflush(stdout);
}
