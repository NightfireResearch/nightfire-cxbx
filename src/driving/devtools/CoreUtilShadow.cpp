#include "CoreUtilShadow.h"

#include "../engine/CoreContainers.h"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../engine/URefCounter.h"
#include "../engine/USingleton.h"
#include "../platform/FileSys.h"      // BIG_find, BIG_dirsize
#include "../platform/RealMemory.h"   // MEMCLASS_init, MEMCLASS_restore
#include "../platform/RefPack.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xboxPath.h"

#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_COREUTILSHADOW=1, at injection time (before the game has run: nothing here needs its state).
//
// Heap scenarios. Each runs twice in a scratch world - class 0 made afresh over the same 16 MB arena, the allocator
// table pointed at the MEM_ functions, the pools in use a fresh static FastPool, the object heap class 0 - once
// with the originals of the functions under test swapped back in, once with the ports. Every object lives at the
// same address in both runs, so the arena, the pools and the objects compare byte for byte, and so does a log of
// every result (pointers as offsets into the arena):
//   - UMemory: random FastAlloc/FastFree (both entries, every pool size and the big path), Alloc/Free, operator
//     new/delete/new[], the REAL callbacks, Size, AddFastBlocks, CarveFastBlock, then ReleaseFastBlocks; and a
//     NewClass/DeleteClass pair (the class record compared relative to its memory).
//   - URefCounter, per instantiation: random AddReference/RemoveReference/GetReference over names that differ in
//     case only, then the destructor where the instantiation has one. The key bytes after a name's terminator are
//     copied from the stack by the original, so they are cleared in live nodes before the arena is compared.
//   - The trees' insert_unique (AttributeExtension, resolvers, StateRef by its state's bytes), insert_multi, find on
//     hand-built trees, vector push_back, USimpleVec<RLightning::Segment>'s destructor (nested arrays), and
//     USingletonManager's Register/ResetAll/KillAll with recording singletons, USingleton's destructor.
// Data groups: every .crp, .gal and .loc file in the archives, copied fresh into one buffer for each side:
// Deserialize (original and port, the resolved images compared byte for byte), and on the file both unresolved and
// resolved, every lookup on every group (GetArray, DataEnd, GroupLocateTag, DataCountType, DataLocateFirst,
// DataLocateTag and 0x0008d6e0 through its register adaptor) for the tags present, perturbed tags and random ones,
// again with the groups' sorted flags flipped, and ProcessBreadthFirst with a recording processor that refuses
// some groups and data items.
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types: another test's of the same name must not merge with them

int g_cases, g_checks, g_differ, g_notRestored, g_faults;

void Report(const char *format, ...) {
    if (g_differ > 10)
        return;
    va_list arguments;
    va_start(arguments, format);
    printf("[coreutil]   ");
    vprintf(format, arguments);
    printf("\n");
    va_end(arguments);
}

void Append(std::string &log, const char *format, ...) {
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    log += line;
}

uint32_t g_random = 0x2545f491;
uint32_t Random(uint32_t below) {
    g_random = g_random * 1103515245u + 12345u;
    return below == 0 ? 0 : (g_random >> 8) % below;
}

// One call that may fault, counted rather than fatal
bool Guarded(void (*run)(void *), void *context) {
#ifdef _MSC_VER
    __try {
        run(context);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    run(context);
    return true;
#endif
}

// The originals of a list of addresses, swapped in for as long as the scope lives
struct OriginalsScope {
    const unsigned *addresses;
    int count;
    OriginalsScope(const unsigned *list, int n) : addresses(list), count(n) {
        for (int i = 0; i < count; i++)
            g_notRestored += !XbeOriginal_Restore(addresses[i], true);
    }
    ~OriginalsScope() {
        for (int i = 0; i < count; i++)
            XbeOriginal_Restore(addresses[i], false);
    }
};

// Two logs, line by line
void CompareLogs(const char *what, const std::string &original, const std::string &port) {
    size_t a = 0, b = 0;
    int line = 0;
    while (a < original.size() || b < port.size()) {
        size_t ea = original.find('\n', a), eb = port.find('\n', b);
        if (ea == std::string::npos)
            ea = original.size();
        if (eb == std::string::npos)
            eb = port.size();
        g_checks++;
        if (original.compare(a, ea - a, port, b, eb - b) != 0) {
            g_differ++;
            Report("%s, line %d: original \"%.*s\", port \"%.*s\"", what, line, (int)(ea - a > 160 ? 160 : ea - a),
                   original.c_str() + a, (int)(eb - b > 160 ? 160 : eb - b), port.c_str() + b);
        }
        a = ea + 1;
        b = eb + 1;
        line++;
    }
}

void CompareBytes(const char *what, const uint8_t *original, const uint8_t *port, size_t bytes) {
    g_checks++;
    if (memcmp(original, port, bytes) == 0)
        return;
    size_t at = 0;
    while (original[at] == port[at])
        at++;
    g_differ++;
    Report("%s: bytes differ from +0x%x (original %02x, port %02x)", what, (unsigned)at, original[at], port[at]);
}

// ===============================================================================================================
// The scratch world
// ===============================================================================================================

#define MemClassTable ((void **)0x00242ce0)
#define MemOutOfMemoryHook (*(void **)0x00242cd8)
#define MemDefaultClass (*(unsigned *)0x00242cdc)   // the class a request with flags 0 goes to
#define AllocatorTable (*(MemoryFunctions **)0x001d48c4)
#define PoolsInUse (*(FastPool **)0x001d48c8)
#define ObjectHeapClass (*(int32_t *)0x00242fc8)
#define PageSize (*(uint32_t *)0x00242fc4)
#define ClassMemory ((void **)0x00242fd0)

MemoryFunctions *const kMemFunctions = (MemoryFunctions *)0x001d4894;
const size_t kArenaBytes = 16 << 20;

uint8_t *g_arena;
FastPool g_pools;

struct SavedWorld {
    void *class0, *outOfMemory;
    MemoryFunctions *functions;
    FastPool *pools;
    int32_t objectHeap;
    uint32_t pageSize;
    unsigned defaultClass;
} g_saved;

void SaveWorld() {
    g_saved.class0 = MemClassTable[0];
    g_saved.outOfMemory = MemOutOfMemoryHook;
    g_saved.functions = AllocatorTable;
    g_saved.pools = PoolsInUse;
    g_saved.objectHeap = ObjectHeapClass;
    g_saved.pageSize = PageSize;
    g_saved.defaultClass = MemDefaultClass;
}

void RestoreWorld() {
    MemClassTable[0] = g_saved.class0;
    MemOutOfMemoryHook = g_saved.outOfMemory;
    AllocatorTable = g_saved.functions;
    PoolsInUse = g_saved.pools;
    ObjectHeapClass = g_saved.objectHeap;
    PageSize = g_saved.pageSize;
    MemDefaultClass = g_saved.defaultClass;
}

void BeginWorld() {
    memset(g_arena, 0, kArenaBytes);
    memset(&g_pools, 0, sizeof(g_pools));
    MemOutOfMemoryHook = NULL;
    MemDefaultClass = 0;   // flags 0 ("FastBlock", new[]) means class 0 (nothing in the game changes it)
    MEMCLASS_init(0, "SHADOW", g_arena, (int)kArenaBytes, 0x80, 0x80, 0, 0, 0, 0);
    AllocatorTable = kMemFunctions;
    PoolsInUse = &g_pools;
    ObjectHeapClass = 0;
}

void EndWorld() {
    MEMCLASS_restore(0);
}

// A pointer in the log: its offset into the arena (or "-" for NULL, "@" + its own value elsewhere)
std::string Where(const void *pointer) {
    char text[32];
    uintptr_t p = (uintptr_t)pointer;
    if (pointer == NULL)
        snprintf(text, sizeof(text), "-");
    else if (p >= (uintptr_t)g_arena && p < (uintptr_t)g_arena + kArenaBytes)
        snprintf(text, sizeof(text), "+%x", (unsigned)(p - (uintptr_t)g_arena));
    else
        snprintf(text, sizeof(text), "@%x", (unsigned)p);
    return text;
}

// What a scenario leaves behind
struct Snapshot {
    std::vector<uint8_t> arena;
    FastPool pools;
    std::vector<uint8_t> extra;   // the scenario's own objects
    std::string log;
    bool completed;
};

typedef void (*ScenarioFn)(bool original, std::string &log, std::vector<uint8_t> &extra);

struct ScenarioRun {
    ScenarioFn scenario;
    bool original;
    std::string *log;
    std::vector<uint8_t> *extra;
};

void RunScenarioBody(void *context) {
    ScenarioRun *run = (ScenarioRun *)context;
    run->scenario(run->original, *run->log, *run->extra);
}

// One side of a scenario in a fresh world (the originals listed swapped in for the original side)
void RunSide(ScenarioFn scenario, bool original, const unsigned *originals, int originalCount, Snapshot &out) {
    g_random = 0x2545f491;   // the same choices on both sides
    BeginWorld();
    ScenarioRun run = {scenario, original, &out.log, &out.extra};
    if (original) {
        OriginalsScope scope(originals, originalCount);
        out.completed = Guarded(RunScenarioBody, &run);
    } else {
        out.completed = Guarded(RunScenarioBody, &run);
    }
    out.arena.assign(g_arena, g_arena + kArenaBytes);
    out.pools = g_pools;
    EndWorld();
}

void Scenario(const char *what, ScenarioFn scenario, const unsigned *originals, int originalCount) {
    static Snapshot o, p;
    o = Snapshot();
    p = Snapshot();
    RunSide(scenario, true, originals, originalCount, o);
    RunSide(scenario, false, originals, originalCount, p);
    g_cases++;
    g_checks++;
    if (!o.completed || !p.completed) {
        g_differ++;
        Report("%s: faulted (original %s, port %s)", what, o.completed ? "ran" : "FAULTED", p.completed ? "ran" : "FAULTED");
    }
    CompareLogs(what, o.log, p.log);
    std::string label = std::string(what) + " arena";
    CompareBytes(label.c_str(), o.arena.data(), p.arena.data(), kArenaBytes);
    label = std::string(what) + " pools";
    CompareBytes(label.c_str(), (const uint8_t *)&o.pools, (const uint8_t *)&p.pools, sizeof(FastPool));
    label = std::string(what) + " objects";
    g_checks++;
    if (o.extra != p.extra) {
        g_differ++;
        Report("%s: differ", label.c_str());
    }
}

// ===============================================================================================================
// UMemory
// ===============================================================================================================

struct MemApi {
    void *(*fastAlloc)(unsigned int, const char *);
    void (*fastFree)(void *, unsigned int);
    void (*fastFreeThunk)(void *, unsigned int);
    void *(*alloc)(unsigned int, unsigned int, const char *);
    void (*free)(void *);
    void *(*operatorNew)(unsigned int);
    void (*operatorDelete)(void *);
    void *(*operatorNewArray)(unsigned int);
    void *(*realAlloc)(const char *, int, unsigned);
    bool (*realFree)(void *);
    size_t (*size)(void *);
    void (*addFastBlocks)(unsigned int);
    void (*releaseFastBlocks)();
    void (*carveFastBlock)(unsigned int);
    int (*newClass)(unsigned int, const char *, bool, unsigned int);
    void (*deleteClass)(int);
};

const MemApi kOriginalMem = {
    (void *(*)(unsigned int, const char *))0x00114750,
    (void (*)(void *, unsigned int))0x001147d0,
    (void (*)(void *, unsigned int))0x0001cba0,
    (void *(*)(unsigned int, unsigned int, const char *))0x00114470,
    (void (*)(void *))0x001144b0,
    (void *(*)(unsigned int))0x001146a0,
    (void (*)(void *))0x001146e0,
    (void *(*)(unsigned int))0x00114710,
    (void *(*)(const char *, int, unsigned))0x00114630,
    (bool (*)(void *))0x00114670,
    (size_t (*)(void *))0x001143f0,
    (void (*)(unsigned int))0x001144e0,
    (void (*)())0x00114540,
    (void (*)(unsigned int))0x001145b0,
    (int (*)(unsigned int, const char *, bool, unsigned int))0x00114400,
    (void (*)(int))0x001149a0,
};

const MemApi kPortMem = {
    UMemory::FastAlloc, UMemory::FastFree, UMemory::FastFreeThunk, UMemory::Alloc, UMemory::Free, OperatorNew,
    OperatorDelete, OperatorNewArray, UMemoryREALAllocCallback, UMemoryREALFreeCallback, UMemory::Size,
    UMemory::AddFastBlocks, UMemory::ReleaseFastBlocks, UMemory::CarveFastBlock, UMemory::NewClass,
    UMemory::DeleteClass,
};

const unsigned kMemOriginals[] = {0x00114400, 0x00114470, 0x001144b0, 0x001144e0, 0x00114540, 0x001145b0,
                                  0x00114630, 0x00114670, 0x001146a0, 0x001146e0, 0x00114710, 0x00114750,
                                  0x001147d0, 0x001149a0, 0x0001cba0, 0x001143f0};

enum BlockKind { kFast, kAlloc, kNew, kNewArray, kReal, kKinds };

bool AllZero(const uint8_t *data, unsigned int bytes) {
    for (unsigned int i = 0; i < bytes; i++)
        if (data[i] != 0)
            return false;
    return true;
}

void MemoryScenario(bool original, std::string &log, std::vector<uint8_t> &extra) {
    const MemApi &api = original ? kOriginalMem : kPortMem;
    const int kLive = 300;
    struct Live {
        uint8_t *block;
        unsigned int size;
        int kind;
    } live[kLive] = {};
    for (int op = 0; op < 6000; op++) {
        int slot = (int)Random(kLive);
        Live &l = live[slot];
        if (Random(400) == 0) {
            unsigned int count = 1 + Random(2);
            api.addFastBlocks(count);
            Append(log, "%d addblocks %u\n", op, count);
            continue;
        }
        if (Random(300) == 0) {
            unsigned int bucket = Random(64);
            api.carveFastBlock(bucket);
            Append(log, "%d carve %u\n", op, bucket);
            continue;
        }
        if (l.block == NULL) {
            int kind = (int)Random(kKinds);
            unsigned int size;
            if (kind == kFast)
                size = Random(10) == 0 ? 0x401 + Random(0x2000) : 1 + Random(0x400);
            else
                size = Random(8) == 0 ? Random(3) : Random(Random(6) == 0 ? 20000 : 1500);
            uint8_t *block = NULL;
            switch (kind) {
            case kFast: block = (uint8_t *)api.fastAlloc(size, "shadow fast"); break;
            case kAlloc: block = (uint8_t *)api.alloc(size, 0, "shadow alloc"); break;
            case kNew: block = (uint8_t *)api.operatorNew(size); break;
            case kNewArray: block = (uint8_t *)api.operatorNewArray(size); break;
            default: block = (uint8_t *)api.realAlloc("shadow real", (int)size, 0); break;
            }
            unsigned int usable = size == 0 && (kind == kNew || kind == kNewArray) ? 1 : size;
            bool zero = block != NULL && AllZero(block, usable);
            Append(log, "%d alloc kind %d size %u -> %s zero %d\n", op, kind, size, Where(block).c_str(), zero);
            if (block != NULL) {
                // a pool entry is filled over its whole size, so a fill of the wrong size on the way back shows
                unsigned int fill = kind == kFast && size <= 0x400 ? (((size - 1) >> 4) + 1) << 4 : usable;
                for (unsigned int i = 0; i < fill; i++)
                    block[i] = (uint8_t)(op * 7 + i);
                l.block = block;
                l.size = usable;
                l.kind = kind;
                if (kind != kFast && Random(4) == 0)
                    Append(log, "%d size %s = %u\n", op, Where(block).c_str(), (unsigned)api.size(block));
            }
        } else {
            switch (l.kind) {
            case kFast:
                if (Random(2) == 0)
                    api.fastFree(l.block, l.size);
                else
                    api.fastFreeThunk(l.block, l.size);
                break;
            case kAlloc: api.free(l.block); break;
            case kNew:
            case kNewArray: api.operatorDelete(l.block); break;
            default: Append(log, "%d realfree -> %d\n", op, api.realFree(l.block)); break;
            }
            Append(log, "%d free %s\n", op, Where(l.block).c_str());
            l.block = NULL;
        }
    }
    api.operatorDelete(NULL);
    api.releaseFastBlocks();
    Append(log, "released\n");

    // A class of its own: contiguous memory from the loader, so compared relative to it
    for (int i = 0; i < 3; i++) {
        bool locked = Random(2) != 0;
        unsigned int alignment = Random(2) != 0 ? 0x10 : 0x80;
        int number = api.newClass(0x20000, "shadow class", locked, alignment);
        Append(log, "newclass locked %d align %u -> %d\n", locked, alignment, number);
        if (number < 0 || number > 63)
            continue;
        uint8_t *memory = (uint8_t *)ClassMemory[number];
        uint8_t *record = (uint8_t *)MemClassTable[number];
        Append(log, "  memory %d record %d", memory != NULL, record != NULL);
        if (memory != NULL && record != NULL) {
            Append(log, " at +%x name \"%s\" align %d/%d tail %d flags %x locked %d", (unsigned)(record - memory),
                   (const char *)record, *(int *)(record + 0x28), *(int *)(record + 0x2c), *(int *)(record + 0x30),
                   *(unsigned *)(record + 0x34), record[0x38]);
        }
        Append(log, " pagesize %x\n", PageSize);
        api.deleteClass(number);
        Append(log, "  deleted: record %d memory %d\n", MemClassTable[number] != NULL, ClassMemory[number] != NULL);
    }
    (void)extra;
}

// ===============================================================================================================
// URefCounter
// ===============================================================================================================

typedef void (__fastcall *AddFn)(URefCounterMap *, int, const char *, void *);
typedef bool (__fastcall *RemoveFn)(URefCounterMap *, int, void *);
typedef void (__fastcall *DestructFn)(URefCounterMap *, int);
typedef void *(__fastcall *GetReferenceFn)(URefCounterMap *, int, const char *);

template <class C, class T, void (C::*F)(const char *, T *)>
void __fastcall PortAdd(URefCounterMap *map, int, const char *name, void *object) {
    (static_cast<C *>(map)->*F)(name, static_cast<T *>(object));
}

template <class C, class T, bool (C::*F)(T *)>
bool __fastcall PortRemove(URefCounterMap *map, int, void *object) {
    return (static_cast<C *>(map)->*F)(static_cast<T *>(object));
}

template <class C, void (C::*F)()>
void __fastcall PortDestruct(URefCounterMap *map, int) {
    (static_cast<C *>(map)->*F)();
}

void *__fastcall PortGetReference(URefCounterMap *map, int, const char *name) {
    return map->GetReference(name);
}

struct RefCase {
    const char *name;
    unsigned add, remove, destruct;   // the originals (0: the instantiation has no such copy)
    AddFn portAdd;
    RemoveFn portRemove;
    DestructFn portDestruct;
};

const RefCase kRefCases[] = {
    {"URefCounter<ModelInfo>", 0x00018030, 0, 0x000182b0,
     PortAdd<ModelInfoRefCounter, ActModelInfo, &ModelInfoRefCounter::AddReference>, NULL,
     PortDestruct<ModelInfoRefCounter, &ModelInfoRefCounter::Destruct>},
    {"URefCounter<TextureInfo>", 0x0001a5e0, 0, 0x0001a880,
     PortAdd<TextureInfoRefCounter, ActTextureInfo, &TextureInfoRefCounter::AddReference>, NULL,
     PortDestruct<TextureInfoRefCounter, &TextureInfoRefCounter::Destruct>},
    {"URefCounter<WeaponInfo>", 0x0001bbd0, 0, 0,
     PortAdd<WeaponInfoRefCounter, ActWeaponInfo, &WeaponInfoRefCounter::AddReference>, NULL, NULL},
    {"URefCounter<RCARPFile>", 0x00090280, 0x00090130, 0,
     PortAdd<CarpFileRefCounter, RCARPFile, &CarpFileRefCounter::AddReference>,
     PortRemove<CarpFileRefCounter, RCARPFile, &CarpFileRefCounter::RemoveReference>, NULL},
    {"URefCounter<RTextureContext>", 0x00094ed0, 0x00094d80, 0,
     PortAdd<TextureContextRefCounter, RTextureContext, &TextureContextRefCounter::AddReference>,
     PortRemove<TextureContextRefCounter, RTextureContext, &TextureContextRefCounter::RemoveReference>, NULL},
    {"URefCounter<AMix>", 0x0011d460, 0x0011d310, 0, PortAdd<MixRefCounter, AMix, &MixRefCounter::AddReference>,
     PortRemove<MixRefCounter, AMix, &MixRefCounter::RemoveReference>, NULL},
    {"URefCounter<AStream>", 0x001233f0, 0x00123230, 0,
     PortAdd<StreamRefCounter, AStream, &StreamRefCounter::AddReference>,
     PortRemove<StreamRefCounter, AStream, &StreamRefCounter::RemoveReference>, NULL},
    {"URefCounter<AFader>", 0x00125a70, 0x00125920, 0, PortAdd<FaderRefCounter, AFader, &FaderRefCounter::AddReference>,
     PortRemove<FaderRefCounter, AFader, &FaderRefCounter::RemoveReference>, NULL},
    {"URefCounter<ABank>", 0x001266d0, 0x00126580, 0x001267f0,
     PortAdd<BankRefCounter, ABank, &BankRefCounter::AddReference>,
     PortRemove<BankRefCounter, ABank, &BankRefCounter::RemoveReference>,
     PortDestruct<BankRefCounter, &BankRefCounter::Destruct>},
    {"URefCounter<AEngine>", 0x0012f700, 0x0012f5b0, 0,
     PortAdd<EngineRefCounter, AEngine, &EngineRefCounter::AddReference>,
     PortRemove<EngineRefCounter, AEngine, &EngineRefCounter::RemoveReference>, NULL},
};

#define RefCounterMap_BuyHeadNode ((RefCounterNode *(__fastcall *)(URefCounterMap *, int))0x00094070)

const RefCase *g_refCase;
URefCounterMap g_refMap;

const char *const kRefNames[] = {"alpha", "ALPHA", "Alpha", "beta", "Beta2", "gamma", "GAMMA", "delta", "e",
                                 "zeta_a_longer_name_for_the_key_buffer", "", "x1", "X1", "_under", "[bracket"};

// The bytes after a live node's terminator are stack garbage in the original's copy: cleared before comparing
void ClearKeyTails(RefCounterNode *node) {
    if (node == NULL || node->isNil)
        return;
    ClearKeyTails(node->left);
    ClearKeyTails(node->right);
    size_t length = strnlen(node->name, sizeof(node->name));
    if (length < sizeof(node->name))
        memset(node->name + length, 0, sizeof(node->name) - length);
}

void DumpRefTree(std::string &log, RefCounterNode *node) {
    if (node == NULL || node->isNil)
        return;
    DumpRefTree(log, node->left);
    Append(log, " [%s %s %d %s c%d]", Where(node).c_str(), node->name, node->references, Where(node->object).c_str(),
           node->color);
    DumpRefTree(log, node->right);
}

void RefScenario(bool original, std::string &log, std::vector<uint8_t> &extra) {
    const RefCase &c = *g_refCase;
    AddFn add = original ? (AddFn)c.add : c.portAdd;
    RemoveFn remove = c.remove == 0 ? NULL : original ? (RemoveFn)c.remove : c.portRemove;
    DestructFn destruct = c.destruct == 0 ? NULL : original ? (DestructFn)c.destruct : c.portDestruct;
    GetReferenceFn get = original ? (GetReferenceFn)0x00125270 : PortGetReference;

    URefCounterMap *map = &g_refMap;
    map->compare = 0;
    map->head = RefCounterMap_BuyHeadNode(map, 0);
    map->head->isNil = 1;
    map->head->parent = map->head;
    map->head->left = map->head;
    map->head->right = map->head;
    map->count = 0;
    const int kNames = sizeof(kRefNames) / sizeof(kRefNames[0]);
    for (int op = 0; op < 500; op++) {
        const char *name = kRefNames[Random(kNames)];
        void *object = (void *)(uintptr_t)(0x00700000 + 0x10 * Random(6));
        uint32_t what = Random(4);
        if (what < 2) {
            add(map, 0, name, object);
            Append(log, "%d add \"%s\" %s count %u\n", op, name, Where(object).c_str(), map->count);
        } else if (what == 2 && remove != NULL) {
            bool removed = remove(map, 0, object);
            Append(log, "%d remove %s -> %d count %u\n", op, Where(object).c_str(), removed, map->count);
        } else {
            void *found = get(map, 0, name);
            Append(log, "%d get \"%s\" -> %s\n", op, name, Where(found).c_str());
        }
    }
    Append(log, "tree");
    DumpRefTree(log, map->head->parent);
    Append(log, "\n");
    ClearKeyTails(map->head->parent);
    if (destruct != NULL) {
        destruct(map, 0);
        Append(log, "destructed: head %s count %u\n", Where(map->head).c_str(), map->count);
    }
    extra.assign((const uint8_t *)map, (const uint8_t *)map + sizeof(*map));
}

// ===============================================================================================================
// The containers
// ===============================================================================================================

template <class Value>
GameTreeNode<Value> *NewHead() {
    GameTreeNode<Value> *head = (GameTreeNode<Value> *)UMemory::FastAlloc(sizeof(GameTreeNode<Value>), "STL");
    head->left = head->parent = head->right = head;
    head->color = 1;
    head->isNil = 1;
    return head;
}

template <class Value>
void DumpTree(std::string &log, GameTreeNode<Value> *node) {
    if (node->isNil)
        return;
    DumpTree(log, node->left);
    Append(log, " %s:%x/%d", Where(node).c_str(), *(const uint32_t *)&node->value, node->color);
    DumpTree(log, node->right);
}

AttributeExtensionMap g_attributeMap;
ResolverMap g_resolverMap;
StateRefSet g_stateSet;
SimObjectMultimap g_simMap;
SimObjectVector g_simVector;
LightningSegmentVec g_segments;
USingletonManager g_singletonManager;

typedef AttributeExtensionInsert *(__fastcall *AttributeInsertFn)(GameTree<AttributeExtensionValue> *, int, AttributeExtensionInsert *, const AttributeExtensionValue *);
typedef ResolverInsert *(__fastcall *ResolverInsertFn)(GameTree<ResolverValue> *, int, ResolverInsert *, const ResolverValue *);
typedef StateRefInsert *(__fastcall *StateInsertFn)(GameTree<StateRefValue> *, int, StateRefInsert *, const StateRefValue *);
typedef SimObjectInsert *(__fastcall *SimInsertFn)(GameTree<SimObjectValue> *, int, SimObjectInsert *, const SimObjectValue *);

AttributeExtensionInsert *__fastcall PortAttributeInsert(GameTree<AttributeExtensionValue> *t, int, AttributeExtensionInsert *r, const AttributeExtensionValue *v) {
    return static_cast<AttributeExtensionMap *>(t)->InsertUnique(r, v);
}
ResolverInsert *__fastcall PortResolverInsert(GameTree<ResolverValue> *t, int, ResolverInsert *r, const ResolverValue *v) {
    return static_cast<ResolverMap *>(t)->InsertUnique(r, v);
}
StateRefInsert *__fastcall PortStateInsert(GameTree<StateRefValue> *t, int, StateRefInsert *r, const StateRefValue *v) {
    return static_cast<StateRefSet *>(t)->InsertUnique(r, v);
}
SimObjectInsert *__fastcall PortSimInsert(GameTree<SimObjectValue> *t, int, SimObjectInsert *r, const SimObjectValue *v) {
    return static_cast<SimObjectMultimap *>(t)->InsertMulti(r, v);
}

void TreeScenario(bool original, std::string &log, std::vector<uint8_t> &extra) {
    AttributeInsertFn attributeInsert = original ? (AttributeInsertFn)0x000552d0 : PortAttributeInsert;
    ResolverInsertFn resolverInsert = original ? (ResolverInsertFn)0x001198f0 : PortResolverInsert;
    StateInsertFn stateInsert = original ? (StateInsertFn)0x00092b40 : PortStateInsert;
    SimInsertFn simInsert = original ? (SimInsertFn)0x000b3930 : PortSimInsert;

    g_attributeMap = AttributeExtensionMap();
    g_attributeMap.head = NewHead<AttributeExtensionValue>();
    g_resolverMap = ResolverMap();
    g_resolverMap.head = NewHead<ResolverValue>();
    g_stateSet = StateRefSet();
    g_stateSet.head = NewHead<StateRefValue>();
    g_simMap = SimObjectMultimap();
    g_simMap.head = NewHead<SimObjectValue>();

    // 16 states of 0x4c bytes; contents from 6 patterns, so different states compare equal
    static uint8_t states[16][0x4c];
    for (int i = 0; i < 16; i++) {
        uint32_t pattern = Random(6);
        for (int b = 0; b < 0x4c; b++)
            states[i][b] = (uint8_t)(b == 0x4b - (int)pattern ? 0x80 + pattern : pattern * 3);
    }

    for (int op = 0; op < 400; op++) {
        switch (Random(4)) {
        case 0: {
            AttributeExtensionValue v;
            v.key = Random(64) * 0x01000193u;
            for (int b = 0; b < 0x14; b++)
                v.extension[b] = (uint8_t)Random(256);
            AttributeExtensionInsert r = {};
            AttributeExtensionInsert *answer = attributeInsert(&g_attributeMap, 0, &r, &v);
            Append(log, "%d attribute %x -> %s %s %d\n", op, v.key, Where(answer == &r ? NULL : answer).c_str(),
                   Where(r.node).c_str(), r.inserted);
            break;
        }
        case 1: {
            ResolverValue v = {Random(48), (void *)(uintptr_t)Random(1000)};
            ResolverInsert r = {};
            resolverInsert(&g_resolverMap, 0, &r, &v);
            Append(log, "%d resolver %x -> %s %d\n", op, v.key, Where(r.node).c_str(), r.inserted);
            break;
        }
        case 2: {
            StateRefValue v = {};
            v.state = states[Random(16)];
            v.unknown04[0] = (uint8_t)op;
            StateRefInsert r = {};
            stateInsert(&g_stateSet, 0, &r, &v);
            Append(log, "%d state %d -> %s %d\n", op, (int)(((const uint8_t *)v.state - &states[0][0]) / 0x4c),
                   Where(r.node).c_str(), r.inserted);
            break;
        }
        default: {
            SimObjectValue v = {Random(32), (void *)(uintptr_t)op};
            SimObjectInsert r = {};
            simInsert(&g_simMap, 0, &r, &v);
            Append(log, "%d sim %x -> %s %d\n", op, v.key, Where(r.node).c_str(), r.inserted);
            break;
        }
        }
    }
    Append(log, "attribute");
    DumpTree(log, g_attributeMap.head->parent);
    Append(log, "\nresolver");
    DumpTree(log, g_resolverMap.head->parent);
    Append(log, "\nstate");
    DumpTree(log, g_stateSet.head->parent);
    Append(log, "\nsim");
    DumpTree(log, g_simMap.head->parent);
    Append(log, "\n");
    const void *objects[] = {&g_attributeMap, &g_resolverMap, &g_stateSet, &g_simMap};
    for (const void *object : objects)
        extra.insert(extra.end(), (const uint8_t *)object, (const uint8_t *)object + 0xc);
}

// find, on hand-built trees (no allocation)
const unsigned kFindOriginals[] = {0x000a8ae0};
typedef CollisionInstanceNode **(__fastcall *FindFn)(GameTree<CollisionInstanceValue> *, int, CollisionInstanceNode **, const uint32_t *);
CollisionInstanceNode **__fastcall PortFind(GameTree<CollisionInstanceValue> *t, int, CollisionInstanceNode **r, const uint32_t *key) {
    return static_cast<CollisionInstanceMap *>(t)->Find(r, key);
}

CollisionInstanceNode g_findNodes[64];
CollisionInstanceNode g_findHead;

CollisionInstanceNode *BuildBalanced(int lo, int hi, CollisionInstanceNode *parent) {
    if (lo > hi)
        return &g_findHead;
    int mid = (lo + hi) / 2;
    CollisionInstanceNode *node = &g_findNodes[mid];
    node->parent = parent;
    node->left = BuildBalanced(lo, mid - 1, node);
    node->right = BuildBalanced(mid + 1, hi, node);
    return node;
}

void FindTest() {
    const int sizes[] = {0, 1, 2, 3, 7, 20, 64};
    for (int size : sizes) {
        memset(g_findNodes, 0, sizeof(g_findNodes));
        g_findHead = CollisionInstanceNode();
        g_findHead.isNil = 1;
        for (int i = 0; i < size; i++)
            g_findNodes[i].value.key = 0x10 * i + 0x8;
        CollisionInstanceNode *root = BuildBalanced(0, size - 1, &g_findHead);
        g_findHead.parent = root;
        g_findHead.left = size > 0 ? &g_findNodes[0] : &g_findHead;
        g_findHead.right = size > 0 ? &g_findNodes[size - 1] : &g_findHead;
        CollisionInstanceMap map;
        map.compare = 0;
        map.head = &g_findHead;
        map.size = size;
        g_cases++;
        for (uint32_t key = 0; key < (uint32_t)(0x10 * size + 0x20); key += 4) {
            CollisionInstanceNode *ro = NULL, *rp = NULL, **ao, **ap;
            {
                OriginalsScope scope(kFindOriginals, 1);
                ao = ((FindFn)0x000a8ae0)(&map, 0, &ro, &key);
            }
            ap = PortFind(&map, 0, &rp, &key);
            g_checks++;
            if (ro != rp || (ao == &ro) != (ap == &rp)) {
                g_differ++;
                Report("find in %d nodes, key %x: original %p, port %p", size, key, (void *)ro, (void *)rp);
            }
        }
    }
}

typedef void (__fastcall *PushBackFn)(GameVector<void *> *, int, void *const *);
void __fastcall PortPushBack(GameVector<void *> *v, int, void *const *value) {
    static_cast<SimObjectVector *>(v)->PushBack(value);
}

typedef void (__fastcall *SegmentsDestructFn)(USimpleVec<RLightningSegment> *, int);
void __fastcall PortSegmentsDestruct(USimpleVec<RLightningSegment> *v, int) {
    static_cast<LightningSegmentVec *>(v)->Destruct();
}

// A new[]'d array of RLightning::Segment (0x18 bytes: +8 a block, +0x10 a USimpleVec of segments), as the game's
// new[] lays it out: the count, then the elements
uint8_t *NewSegments(int count, int depth) {
    uint8_t *block = (uint8_t *)OperatorNewArray(4 + count * 0x18);
    *(int32_t *)block = count;
    uint8_t *segments = block + 4;
    for (int i = 0; i < count; i++) {
        uint8_t *segment = segments + i * 0x18;
        if (Random(2) != 0)
            *(void **)(segment + 8) = OperatorNew(8 + Random(40));
        if (depth > 0 && Random(2) != 0) {
            int inner = 1 + (int)Random(3);
            *(uint8_t **)(segment + 0x10) = NewSegments(inner, depth - 1);
            *(int32_t *)(segment + 0x14) = inner;
        }
    }
    return segments;
}

// Singletons that record their calls
std::string *g_singletonLog;
USingleton g_singletons[5];

void __fastcall RecordReset(USingleton *s, int) {
    Append(*g_singletonLog, "reset %d\n", (int)(s - g_singletons));
}
void __fastcall RecordKill(USingleton *s, int) {
    Append(*g_singletonLog, "kill %d\n", (int)(s - g_singletons));
}
const USingletonVtable kRecordingVtable = {NULL, RecordReset, RecordKill};

typedef void (__fastcall *ManagerFn)(USingletonManager *, int);
typedef void (__fastcall *RegisterFn)(USingletonManager *, int, USingleton *);
void __fastcall PortResetAll(USingletonManager *m, int) { m->ResetAll(); }
void __fastcall PortKillAll(USingletonManager *m, int) { m->KillAll(); }
void __fastcall PortRegister(USingletonManager *m, int, USingleton *s) { m->Register(s); }
void __fastcall PortSingletonDestruct(USingleton *s, int) { s->Destruct(); }

void MiscScenario(bool original, std::string &log, std::vector<uint8_t> &extra) {
    PushBackFn pushBack = original ? (PushBackFn)0x000b4980 : PortPushBack;
    g_simVector = SimObjectVector();
    for (int i = 0; i < 150; i++) {
        void *value = (void *)(uintptr_t)(0x1000 + i);
        pushBack(&g_simVector, 0, &value);
        Append(log, "push %d: %s %s %s\n", i, Where(g_simVector.first).c_str(), Where(g_simVector.last).c_str(),
               Where(g_simVector.end).c_str());
    }
    for (void **p = g_simVector.first; p != g_simVector.last; p++)
        Append(log, " %x", (unsigned)(uintptr_t)*p);
    Append(log, "\n");

    SegmentsDestructFn segmentsDestruct = original ? (SegmentsDestructFn)0x0009f870 : PortSegmentsDestruct;
    for (int round = 0; round < 6; round++) {
        g_segments = LightningSegmentVec();
        int count = round == 0 ? 0 : 1 + (int)Random(4);
        if (count != 0) {
            g_segments.data = (RLightningSegment *)NewSegments(count, 2);
            g_segments.count = count;
        }
        segmentsDestruct(&g_segments, 0);
        Append(log, "segments %d: %s %u\n", count, Where(g_segments.data).c_str(), g_segments.count);
    }

    ManagerFn resetAll = original ? (ManagerFn)0x0011b7a0 : PortResetAll;
    ManagerFn killAll = original ? (ManagerFn)0x0011b7d0 : PortKillAll;
    RegisterFn registerOne = original ? (RegisterFn)0x0011bb50 : PortRegister;
    g_singletonLog = &log;
    g_singletonManager = USingletonManager();
    for (int i = 0; i < 5; i++)
        g_singletons[i].vtable = &kRecordingVtable;
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < 5; i++) {
            registerOne(&g_singletonManager, 0, &g_singletons[(i * 3 + round) % 5]);
            Append(log, "register: %s %s %s\n", Where(g_singletonManager.singletons.first).c_str(),
                   Where(g_singletonManager.singletons.last).c_str(), Where(g_singletonManager.singletons.end).c_str());
        }
        resetAll(&g_singletonManager, 0);
        killAll(&g_singletonManager, 0);
        Append(log, "killed: %s %s %s\n", Where(g_singletonManager.singletons.first).c_str(),
               Where(g_singletonManager.singletons.last).c_str(), Where(g_singletonManager.singletons.end).c_str());
    }
    killAll(&g_singletonManager, 0);   // an empty list

    typedef void (__fastcall *SingletonDestructFn)(USingleton *, int);
    SingletonDestructFn singletonDestruct = original ? (SingletonDestructFn)0x0007d780 : PortSingletonDestruct;
    USingleton s;
    s.vtable = &kRecordingVtable;
    singletonDestruct(&s, 0);
    Append(log, "singleton vtable %x\n", (unsigned)(uintptr_t)s.vtable);

    extra.assign((const uint8_t *)&g_simVector, (const uint8_t *)&g_simVector + sizeof(g_simVector));
    extra.insert(extra.end(), (const uint8_t *)&g_singletonManager,
                 (const uint8_t *)&g_singletonManager + sizeof(g_singletonManager));
}

const unsigned kTreeOriginals[] = {0x000552d0, 0x001198f0, 0x00092b40, 0x000b3930};
const unsigned kMiscOriginals[] = {0x000b4980, 0x0009f870, 0x0011b7a0, 0x0011b7d0, 0x0011bb50, 0x0007d780};

// ===============================================================================================================
// UGroup
// ===============================================================================================================

struct GroupApi {
    UGroup *(__fastcall *getArray)(UGroup *, int);
    UData *(__fastcall *dataEnd)(UGroup *, int);
    UGroup *(__fastcall *groupLocateTag)(UGroup *, int, uint32_t);
    int (__fastcall *dataCountType)(UGroup *, int, uint32_t);
    UData *(__fastcall *dataLocateFirst)(UGroup *, int, uint32_t, int, uint32_t);
    UData *(__fastcall *dataLocateTag)(UGroup *, int, uint32_t);
    void (__fastcall *processBreadthFirst)(UGroup *, int, UGroup::Processor *);
    UGroup *(*deserialize)(void *, bool);
    bool (__fastcall *startGroup)(UGroup::Processor *, int, UGroup *);
    void *something;
};

UGroup *__fastcall PortGetArray(UGroup *g, int) { return g->GetArray(); }
UData *__fastcall PortDataEnd(UGroup *g, int) { return g->DataEnd(); }
UGroup *__fastcall PortGroupLocateTag(UGroup *g, int, uint32_t tag) { return g->GroupLocateTag(tag); }
int __fastcall PortDataCountType(UGroup *g, int, uint32_t type) { return g->DataCountType(type); }
UData *__fastcall PortDataLocateFirst(UGroup *g, int, uint32_t type, int first, uint32_t last) {
    return g->DataLocateFirst(type, first, last);
}
UData *__fastcall PortDataLocateTag(UGroup *g, int, uint32_t tag) { return g->DataLocateTag(tag); }
void __fastcall PortProcessBreadthFirst(UGroup *g, int, UGroup::Processor *p) { g->ProcessBreadthFirst(p); }
bool __fastcall PortStartGroup(UGroup::Processor *p, int, UGroup *g) { return p->StartGroup(g); }

const GroupApi kOriginalGroup = {
    (UGroup *(__fastcall *)(UGroup *, int))0x00117940,
    (UData *(__fastcall *)(UGroup *, int))0x0003db60,
    (UGroup *(__fastcall *)(UGroup *, int, uint32_t))0x00117950,
    (int (__fastcall *)(UGroup *, int, uint32_t))0x001179f0,
    (UData *(__fastcall *)(UGroup *, int, uint32_t, int, uint32_t))0x00117a60,
    (UData *(__fastcall *)(UGroup *, int, uint32_t))0x00117b50,
    (void (__fastcall *)(UGroup *, int, UGroup::Processor *))0x00117c10,
    (UGroup *(*)(void *, bool))0x00117d60,
    (bool (__fastcall *)(UGroup::Processor *, int, UGroup *))0x00117e40,
    (void *)0x0008d6e0,
};

const GroupApi kPortGroup = {
    PortGetArray, PortDataEnd, PortGroupLocateTag, PortDataCountType, PortDataLocateFirst, PortDataLocateTag,
    PortProcessBreadthFirst, UGroup::Deserialize, PortStartGroup, (void *)&UGroup::something,
};

const unsigned kGroupOriginals[] = {0x00117940, 0x0003db60, 0x00117950, 0x001179f0, 0x00117a60, 0x00117b50,
                                    0x00117c10, 0x00117cb0, 0x00117d60, 0x00117e40, 0x0008d6e0};

// 0x0008d6e0 or its adaptor: EAX the tag, ESI the group, the index on the stack
__declspec(naked) UData *CallLocateIndexed(void *function, UGroup *group, uint32_t tag, int index) {
    __asm {
        push esi
        push ebx
        mov esi, dword ptr [esp + 16]
        mov eax, dword ptr [esp + 20]
        push dword ptr [esp + 24]
        mov ecx, dword ptr [esp + 16]
        call ecx
        add esp, 4
        pop ebx
        pop esi
        ret
    }
}

uint8_t *g_groupBuffer;
size_t g_groupBufferBytes;

std::string At(const void *pointer) {
    char text[32];
    uintptr_t p = (uintptr_t)pointer;
    if (pointer == NULL)
        snprintf(text, sizeof(text), "-");
    else if (p >= (uintptr_t)g_groupBuffer && p < (uintptr_t)g_groupBuffer + g_groupBufferBytes)
        snprintf(text, sizeof(text), "+%x", (unsigned)(p - (uintptr_t)g_groupBuffer));
    else
        snprintf(text, sizeof(text), "@%x", (unsigned)p);
    return text;
}

struct RecordingProcessor : UGroup::Processor {
    std::string *log;
};

uint32_t Hash(const void *p) {
    uint32_t h = (uint32_t)((uintptr_t)p - (uintptr_t)g_groupBuffer) * 2654435761u;
    return h >> 16;
}

bool __fastcall RecordStart(UGroup::Processor *p, int, UGroup *group) {
    Append(*static_cast<RecordingProcessor *>(p)->log, " S%s", At(group).c_str());
    return Hash(group) % 5 != 0;
}
bool __fastcall RecordData(UGroup::Processor *p, int, UGroup *group, UData *data) {
    Append(*static_cast<RecordingProcessor *>(p)->log, " D%s/%s", At(group).c_str(), At(data).c_str());
    return Hash(data) % 7 != 0;
}
void __fastcall RecordEnd(UGroup::Processor *p, int, UGroup *group) {
    Append(*static_cast<RecordingProcessor *>(p)->log, " E%s", At(group).c_str());
}
const UGroup::ProcessorVtable kRecordingProcessor = {RecordStart, RecordData, RecordEnd};

int g_groupsVisited;

// Every lookup on one group (and its subgroups, walked with the port's GetArray on both sides)
void GroupLookups(const GroupApi &api, UGroup *group, std::string &log, int depth) {
    if (depth > 16 || g_groupsVisited > 4000)
        return;
    g_groupsVisited++;
    UGroup *items = group->GetArray();
    uint32_t groups = group->GroupCount();
    Append(log, "group %s tag %08x flags %x: array %s end %s\n", At(group).c_str(), group->tag, group->flags,
           At(api.getArray(group, 0)).c_str(), At(api.dataEnd(group, 0)).c_str());

    std::vector<uint32_t> tags;
    for (uint32_t i = 0; i < groups && i < 64; i++)
        tags.push_back(items[i].tag);
    for (int i = 0; i < 4; i++)
        tags.push_back(Random(2) ? Random(0xffffffff) : (tags.empty() ? 0 : tags[Random((uint32_t)tags.size())] + 1));
    tags.push_back(0);
    tags.push_back(0xffffffff);
    for (uint32_t tag : tags)
        Append(log, " g%08x=%s", tag, At(api.groupLocateTag(group, 0, tag)).c_str());
    Append(log, "\n");

    UData *data = items + groups;
    for (uint32_t i = 0; i < group->count && i < 96; i++) {
        uint32_t tag = data[i].tag;
        uint32_t type = data[i].MatchTag();
        uint32_t index = tag & 0xffff;
        Append(log, " d%08x=%s %s n%d f%s", tag, At(api.dataLocateTag(group, 0, tag)).c_str(),
               At(api.dataLocateTag(group, 0, tag + 1)).c_str(), api.dataCountType(group, 0, type),
               At(api.dataLocateFirst(group, 0, tag, -1, 0)).c_str());
        if ((data[i].flags & kDataIndexedTag) != 0) {
            Append(log, " %s %s %s %s", At(api.dataLocateFirst(group, 0, type, (int)index, index)).c_str(),
                   At(api.dataLocateFirst(group, 0, type, 0, 0xffff)).c_str(),
                   At(api.dataLocateFirst(group, 0, type, (int)index + 1, index + 5)).c_str(),
                   At(api.dataLocateFirst(group, 0, type, (int)index, index - 1)).c_str());
        }
        Append(log, " i%s %s %s %s\n", At(CallLocateIndexed(api.something, group, tag, -1)).c_str(),
               At(CallLocateIndexed(api.something, group, type, (int)index)).c_str(),
               At(CallLocateIndexed(api.something, group, type, (int)index + 1000)).c_str(),
               At(CallLocateIndexed(api.something, group, type, 0)).c_str());
    }
    uint32_t randomType = Random(0xffffffff);
    Append(log, " r%08x=%s %d %s\n", randomType, At(api.dataLocateTag(group, 0, randomType)).c_str(),
           api.dataCountType(group, 0, randomType), At(api.dataLocateFirst(group, 0, randomType, -1, 0)).c_str());

    // The binary searches on records that may not be sorted: the sorted flags flipped for a moment
    uint32_t flags = group->flags;
    group->flags = flags ^ (UGroup::kDataSorted | UGroup::kGroupsSorted);
    Append(log, " flipped:");
    for (uint32_t i = 0; i < groups && i < 8; i++)
        Append(log, " %s", At(api.groupLocateTag(group, 0, items[i].tag)).c_str());
    for (uint32_t i = 0; i < group->count && i < 8; i++)
        Append(log, " %s", At(api.dataLocateTag(group, 0, data[i].tag)).c_str());
    Append(log, "\n");
    group->flags = flags;

    for (uint32_t i = 0; i < groups; i++)
        GroupLookups(api, items + i, log, depth + 1);
}

struct GroupRun {
    const GroupApi *api;
    UGroup *root;
    std::string *log;
};

void GroupRunBody(void *context) {
    GroupRun *run = (GroupRun *)context;
    g_groupsVisited = 0;
    GroupLookups(*run->api, run->root, *run->log, 0);
    RecordingProcessor processor;
    processor.vtable = &kRecordingProcessor;
    processor.log = run->log;
    Append(*run->log, "process:");
    run->api->processBreadthFirst(run->root, 0, &processor);
    Append(*run->log, "\nstart %d\n", run->api->startGroup(&processor, 0, run->root));
}

// One side's lookups on the image in the buffer
void GroupSide(bool original, std::string &log) {
    g_random = 0x7f4a7c15;
    GroupRun run = {original ? &kOriginalGroup : &kPortGroup, (UGroup *)g_groupBuffer, &log};
    bool ok;
    if (original) {
        OriginalsScope scope(kGroupOriginals, sizeof(kGroupOriginals) / sizeof(kGroupOriginals[0]));
        ok = Guarded(GroupRunBody, &run);
    } else {
        ok = Guarded(GroupRunBody, &run);
    }
    if (!ok)
        Append(log, "FAULTED\n");
}

struct DeserializeRun {
    const GroupApi *api;
};

void DeserializeBody(void *context) {
    DeserializeRun *run = (DeserializeRun *)context;
    run->api->deserialize(g_groupBuffer, true);
}

void CheckGroupFile(const char *name, const std::vector<uint8_t> &file) {
    g_cases++;
    std::string label = std::string(name);
    std::string o, p;
    // unresolved
    memset(g_groupBuffer, 0, g_groupBufferBytes);
    memcpy(g_groupBuffer, file.data(), file.size());
    GroupSide(true, o);
    memcpy(g_groupBuffer, file.data(), file.size());
    GroupSide(false, p);
    CompareLogs((label + " unresolved").c_str(), o, p);

    // Deserialize, both ways, from the same image at the same address
    memcpy(g_groupBuffer, file.data(), file.size());
    DeserializeRun original = {&kOriginalGroup};
    bool okO;
    {
        OriginalsScope scope(kGroupOriginals, sizeof(kGroupOriginals) / sizeof(kGroupOriginals[0]));
        okO = Guarded(DeserializeBody, &original);
    }
    std::vector<uint8_t> resolved(g_groupBuffer, g_groupBuffer + file.size());
    memcpy(g_groupBuffer, file.data(), file.size());
    DeserializeRun port = {&kPortGroup};
    bool okP = Guarded(DeserializeBody, &port);
    g_checks++;
    if (!okO || !okP) {
        g_differ++;
        Report("%s: Deserialize faulted (original %d, port %d)", name, okO, okP);
    }
    CompareBytes((label + " resolved image").c_str(), resolved.data(), g_groupBuffer, file.size());

    // resolved (the port's image, the same bytes)
    o.clear();
    p.clear();
    std::vector<uint8_t> image(g_groupBuffer, g_groupBuffer + file.size());
    GroupSide(true, o);
    memcpy(g_groupBuffer, image.data(), image.size());
    GroupSide(false, p);
    CompareLogs((label + " resolved").c_str(), o, p);
}

bool HasExtension(const char *name, const char *extension) {
    size_t n = strlen(name), e = strlen(extension);
    return n > e && _stricmp(name + n - e, extension) == 0;
}

// Every data-group file of one archive
void CheckArchive(const char *hostPath, int *files) {
    HANDLE handle = CreateFileA(hostPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return;
    uint8_t header[16];
    DWORD got = 0;
    ReadFile(handle, header, 16, &got, NULL);
    int dirSize = got == 16 ? BIG_dirsize(header) : 0;
    std::vector<uint8_t> directory(dirSize > 16 ? dirSize : 16);
    SetFilePointer(handle, 0, NULL, FILE_BEGIN);
    ReadFile(handle, directory.data(), (DWORD)dirSize, &got, NULL);
    for (int i = 0;; i++) {
        int offset, size;
        const char *name = BIG_find(directory.data(), NULL, i, &offset, &size);
        if (name == NULL)
            break;
        if (!HasExtension(name, ".crp") && !HasExtension(name, ".gal") && !HasExtension(name, ".loc"))
            continue;
        std::vector<uint8_t> raw(size + 16);
        SetFilePointer(handle, offset, NULL, FILE_BEGIN);
        ReadFile(handle, raw.data(), (DWORD)size, &got, NULL);
        std::vector<uint8_t> file;
        unsigned unpacked = unpacksizez(raw.data());
        if (unpacked != 0) {
            file.assign(unpacked + 64, 0);
            UNPACK_unpack(raw.data(), file.data());
            file.resize(unpacked);
        } else {
            file.assign(raw.begin(), raw.begin() + size);
        }
        // a group as the game loads it: relative items
        if (file.size() < 16 || file.size() + 64 > g_groupBufferBytes || (((const UGroup *)file.data())->flags & kDataRelative) == 0)
            continue;
        CheckGroupFile(name, file);
        (*files)++;
    }
    CloseHandle(handle);
}

}   // namespace

void CoreUtilShadow_Run(void) {
    char setting[16];
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_COREUTILSHADOW", setting, sizeof(setting));
    if (length == 0 || length >= sizeof(setting) || atoi(setting) == 0)
        return;
    printf("[coreutil] the engine core's memory manager, reference counters, containers and data groups against "
           "the originals\n");
    fflush(stdout);

    g_arena = (uint8_t *)VirtualAlloc(NULL, kArenaBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_groupBufferBytes = 8 << 20;
    g_groupBuffer = (uint8_t *)VirtualAlloc(NULL, g_groupBufferBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    SaveWorld();

    Scenario("UMemory", MemoryScenario, kMemOriginals, sizeof(kMemOriginals) / sizeof(kMemOriginals[0]));
    for (const RefCase &c : kRefCases) {
        g_refCase = &c;
        unsigned originals[5];
        int n = 0;
        originals[n++] = c.add;
        originals[n++] = 0x00125270;   // GetReference, shared
        originals[n++] = 0x00125f50;   // ABank's _Erase (ours), reached from its destructor
        if (c.remove != 0)
            originals[n++] = c.remove;
        if (c.destruct != 0)
            originals[n++] = c.destruct;
        Scenario(c.name, RefScenario, originals, n);
    }
    Scenario("trees", TreeScenario, kTreeOriginals, sizeof(kTreeOriginals) / sizeof(kTreeOriginals[0]));
    Scenario("vector, USimpleVec, singletons", MiscScenario, kMiscOriginals,
             sizeof(kMiscOriginals) / sizeof(kMiscOriginals[0]));
    RestoreWorld();
    FindTest();

    int files = 0;
    char folder[MAX_PATH];
    if (Xbox_ResolvePath("D:\\driving", folder, sizeof(folder))) {
        char pattern[MAX_PATH];
        snprintf(pattern, sizeof(pattern), "%s\\*.viv", folder);
        WIN32_FIND_DATAA found;
        HANDLE search = FindFirstFileA(pattern, &found);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                char path[MAX_PATH];
                snprintf(path, sizeof(path), "%s\\%s", folder, found.cFileName);
                CheckArchive(path, &files);
            } while (FindNextFileA(search, &found));
            FindClose(search);
        }
    }
    if (files == 0) {
        g_differ++;
        printf("[coreutil]   no data-group files found under D:\\driving\n");
    }

    VirtualFree(g_arena, 0, MEM_RELEASE);
    VirtualFree(g_groupBuffer, 0, MEM_RELEASE);
    if (g_notRestored != 0) {
        g_differ++;
        printf("[coreutil]   %d originals could not be swapped back in (not patched?)\n", g_notRestored);
    }
    printf("[coreutil] UMemory, URefCounter, containers, UGroup (%d data-group files, %d faults): %d cases, %d checks, "
           "%d differ\n", files, g_faults, g_cases, g_checks, g_differ);
    fflush(stdout);
}
