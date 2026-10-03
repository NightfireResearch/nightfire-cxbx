#include "LoaderShadow.h"

#include "../eagl/EaglGlobals.h"
#include "../eagl/Loader.h"
#include "../platform/FileSys.h"
#include "../platform/RefPack.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xboxPath.h"

#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_LDSHADOW=1, at injection time: eagl/Loader.cpp against the originals.
//
// Both sides run on the same addresses: every object is copied fresh into the same buffer for each side, and
// EAGL's allocator hooks are pointed at an arena that starts over for each side, so the symbol tables, chains and
// destructor records land at the same addresses too, and everything compares byte for byte - the relocated
// images, the arena, the loaders, and a log of every allocation (size and name), free, message (PrintMessage's
// hook), resolver call, and constructor and destructor callback.
//
// - Pools: scripted runs of AddSymbol/RemoveSymbol/Search/Empty on a SymbolPool (at the default size and at a
//   tiny one, so it grows again and again), the two ConstructorPools, and RegisterShapes/UnRegisterShapes on every
//   image container (.xsh) in the archives.
// - Objects: per archive, misc.viv's .o files and then the archive's own (.o, and .dat with its .rel) are loaded
//   and kept loaded, so lookups across objects happen. A discovery load with the port first finds every class and
//   every symbol resolved from outside; the test then registers a recording constructor and destructor for every
//   class (in both pools), puts a third of the outside names in the global pool and answers another third from a
//   resolver (given to three objects in four), and some RUNTIME_ALLOC objects come back NULL or without a
//   destructor. Some split objects defer Resolve. (Every .rel on the disc is a copy of its .dat, so the game never
//   reaches the second part; every other split object here is cut at its section table, the rest found there.)
//   Then every symbol is read back (GetSymbol, GetAddr, the lookup other objects use, GetNextSymbol/GetNextAddr per
//   class) and the objects destroyed in reverse.
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types: another test's of the same name must not merge with them

static int g_checks, g_failures;
static int g_counts[5];   // over the original side's logs: constructions, runtime ones, resolver calls, messages,
                          // relocated objects

static int CountLines(const std::string &log, const char *prefix) {
    int n = 0;
    size_t length = strlen(prefix);
    for (size_t at = 0; at < log.size();) {
        if (log.compare(at, length, prefix) == 0)
            n++;
        size_t next = log.find('\n', at);
        if (next == std::string::npos)
            break;
        at = next + 1;
    }
    return n;
}

static void Progress(const char *what, const char *detail) {
    if (getenv("NIGHTFIRE_LDSHADOW_TRACE") != NULL) {
        printf("[ldshadow] ... %s %s (heap %s)\n", what, detail,
               HeapValidate(GetProcessHeap(), 0, NULL) ? "valid" : "CORRUPT");
        fflush(stdout);
    }
}

static void Report(const char *what, const char *detail) {
    if (g_failures++ < 30)
        printf("[ldshadow] %s: %s\n", what, detail);
}

static const unsigned kLoader[] = {
    0x000e5220, 0x000e52a0, 0x000e57e0, 0x000e5a10, 0x000e5ae0, 0x000e5af0, 0x000e5bc0, 0x000e5c80, 0x000e5cc0,
    0x000e5ce0, 0x000e5cf0, 0x000e5d10, 0x000e5d70, 0x000e5e80, 0x000e62f0, 0x000e6330, 0x000e6380, 0x000e6390,
    0x000ede60, 0x000edf50, 0x000f39c0, 0x000f3a20, 0x000f3a40, 0x000f3a70, 0x000f3a90, 0x000f3ab0, 0x000f3b20,
    0x000f3b40, 0x000f3b70, 0x000f3b90, 0x000f3bb0, 0x000f3bd0, 0x000f3bf0, 0x000f3c70, 0x000f3c90, 0x000f3cc0,
    0x000f3df0, 0x000f3e50, 0x000f3fa0, 0x000f40a0, 0x000f41d0 };
static const int kLoaderCount = sizeof(kLoader) / sizeof(kLoader[0]);

struct Originals {   // the originals in play for one side
    bool on;
    explicit Originals(bool original) : on(original) {
        if (on)
            for (int i = 0; i < kLoaderCount; i++)
                XbeOriginal_Restore(kLoader[i], true);
    }
    ~Originals() {
        if (on)
            for (int i = 0; i < kLoaderCount; i++)
                XbeOriginal_Restore(kLoader[i], false);
    }
};

// ---- the log, the arena and the hooks

static std::string *g_log;

static void Logf(const char *format, ...) {
    if (g_log == NULL)
        return;
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_log->append(line);
    g_log->push_back('\n');
}

static const size_t kArena = 48u << 20;
static uint8_t *g_arena;
static size_t g_arenaUsed;
static bool g_inArena;

static void ArenaReset() {
    memset(g_arena, 0xcd, g_arenaUsed > 0 ? g_arenaUsed : kArena);
    g_arenaUsed = 0;
}

static void *ShadowMalloc(uint32_t size, const char *name) {
    if (!g_inArena)
        return malloc(size != 0 ? size : 1);
    size_t at = (g_arenaUsed + 15) & ~(size_t)15;
    if (at + size > kArena) {
        Report("arena", "out of space");
        return NULL;
    }
    g_arenaUsed = at + size;
    Logf("malloc %u \"%s\" -> +%x", size, name != NULL ? name : "(null)", (unsigned)at);
    return g_arena + at;
}

static void ShadowFree(void *data, uint32_t size) {
    uint8_t *p = (uint8_t *)data;
    if (g_inArena && p >= g_arena && p < g_arena + kArena) {
        Logf("free +%x %u", (unsigned)(p - g_arena), size);
        return;
    }
    Logf("free outside the arena %u", size);
    free(data);
}

static int ShadowPrint(const char *format, va_list args) {
    char line[512];
    vsnprintf(line, sizeof(line), format, args);
    Logf("message %s", line);
    return 0;
}

#define PrintHook       (*(void **)0x00240268u)
#define PoolSize        (*(uint32_t *)0x001cdc50u)
#define LoadedTables    (*(void **)0x0023fb88u)

static uint32_t Hash(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s != 0; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

// ---- the originals, called as __fastcall with a dummy EDX where they are thiscall

typedef SymbolPool *(__fastcall *PoolConstructFn)(SymbolPool *, int);
typedef void (__fastcall *PoolVoidFn)(void *, int);
typedef void *(__fastcall *PoolAddFn)(SymbolPool *, int, const char *, void *);
typedef void (__fastcall *PoolRemoveFn)(SymbolPool *, int, const char *);
typedef void *(__fastcall *PoolSearchFn)(SymbolPool *, int, const char *, bool *);
typedef uint32_t (__fastcall *PoolHashFn)(SymbolPool *, int, const char *);
typedef void (__fastcall *TypeAddFn)(void *, int, const char *, void *, void *);
typedef void (__fastcall *TypeRemoveFn)(void *, int, const char *);
typedef void *(__fastcall *TypeFindFn)(void *, int, const char *);
typedef void (*ShapesFn)(uint8_t *);
typedef DynamicLoader *(__fastcall *LoadFn)(DynamicLoader *, int, void *, uint32_t, void *);
typedef DynamicLoader *(__fastcall *LoadSplitFn)(DynamicLoader *, int, void *, uint32_t, void *, int, void *);
typedef void (__fastcall *LoaderVoidFn)(DynamicLoader *, int);
typedef void *(__fastcall *ElfDataFn)(DynamicLoader *, int);
typedef LoaderSymbol *(__fastcall *GetSymbolFn)(DynamicLoader *, int, LoaderSymbol *, int);
typedef bool (__fastcall *GetAddrFn)(DynamicLoader *, int, const char *, const char *, void **);
typedef bool (__fastcall *GetNextSymbolFn)(DynamicLoader *, int, const char *, int *, LoaderSymbol *);
typedef bool (__fastcall *GetNextAddrFn)(DynamicLoader *, int, const char *, int *, void **);
typedef void *(*LookupFn)(HashTable *, const char *);

// ---- comparing two sides

static void CompareLogs(const char *what, const std::string &o, const std::string &p) {
    g_checks++;
    if (o == p)
        return;
    size_t i = 0, line = 0, start = 0;
    while (i < o.size() && i < p.size() && o[i] == p[i]) {
        if (o[i] == '\n') {
            line++;
            start = i + 1;
        }
        i++;
    }
    size_t eo = o.find('\n', start), ep = p.find('\n', start);
    std::string lo = o.substr(start, eo == std::string::npos ? std::string::npos : eo - start);
    std::string lp = p.substr(start, ep == std::string::npos ? std::string::npos : ep - start);
    char detail[640];
    snprintf(detail, sizeof(detail), "line %u: original \"%.250s\" / ours \"%.250s\"", (unsigned)line, lo.c_str(),
             lp.c_str());
    Report(what, detail);
}

static void CompareBytes(const char *what, const std::vector<uint8_t> &o, const std::vector<uint8_t> &p) {
    g_checks++;
    if (o == p)
        return;
    char detail[200];
    if (o.size() != p.size()) {
        snprintf(detail, sizeof(detail), "%u bytes / %u", (unsigned)o.size(), (unsigned)p.size());
    } else {
        size_t i = 0;
        while (o[i] == p[i])
            i++;
        snprintf(detail, sizeof(detail), "first difference at +%x: %02x / %02x", (unsigned)i, o[i], p[i]);
    }
    Report(what, detail);
}

// ---- pools

static void DumpPool(const SymbolPool *pool) {
    Logf("pool count %u size %u table %p resolvers %p", pool->count, pool->size, (void *)pool->table,
         (void *)pool->resolvers);
    if (pool->table == NULL)
        return;
    for (uint32_t i = 0; i < pool->size; i++) {
        SymbolEntry *e = pool->table[i];
        if (e != NULL)
            Logf("  [%u] %p \"%s\" %p", i, (void *)e, e->name, e->value);
    }
}

static void PoolScript(bool original, uint32_t initialSize, std::string *log) {
    g_log = log;
    ArenaReset();
    uint32_t savedSize = PoolSize;
    PoolSize = initialSize;
    SymbolPool pool;
    memset(&pool, 0xee, sizeof(pool));
    uint32_t seed = 12345;
    char name[32];
    Progress(original ? "pool original" : "pool ours", "");
    {
        Originals scope(original);
        if (original)
            ((PoolConstructFn)0x000f3c70)(&pool, 0);
        else
            pool.Construct();
        for (int op = 0; op < 6000; op++) {
            seed = seed * 1103515245u + 12345u;
            uint32_t r = seed >> 8;
            uint32_t which = r % 10;
            if ((r >> 4) % 61 == 0)
                name[0] = 0;
            else
                snprintf(name, sizeof(name), "sym%u", (r >> 4) % 700);
            if (which < 5) {
                void *value = (void *)(uintptr_t)(r & 0xfffff0);
                void *copy = original ? ((PoolAddFn)0x000f3df0)(&pool, 0, name, value) : pool.AddSymbol(name, value);
                Logf("add %s -> %s", name, (const char *)copy);
            } else if (which < 7) {
                if (original)
                    ((PoolRemoveFn)0x000f3e50)(&pool, 0, name);
                else
                    pool.RemoveSymbol(name);
                Logf("remove %s", name);
            } else {
                bool found = true;
                void *v = original ? ((PoolSearchFn)0x000f3fa0)(&pool, 0, name, &found) : pool.Search(name, &found);
                Logf("search %s -> %d %p", name, (int)found, v);
                if (pool.size != 0 && pool.table != NULL) {
                    uint32_t h = original ? ((PoolHashFn)0x000f3c90)(&pool, 0, name) : pool.HashFunction(name);
                    Logf("hash %u", h);
                }
            }
            if (op % 1000 == 999) {
                DumpPool(&pool);
                Progress("pool", "1000 more");
            }
            if (op == 3000) {
                if (original)
                    ((PoolVoidFn)0x000f40a0)(&pool, 0);
                else
                    pool.Empty();
                DumpPool(&pool);
            }
        }
        if (original)
            ((PoolVoidFn)0x000f41d0)(&pool, 0);
        else
            pool.Destruct();
        DumpPool(&pool);
    }
    PoolSize = savedSize;
    g_log = NULL;
}

static void DumpTypes(const SymbolPool *pools) {
    DumpPool(&pools[0]);
    DumpPool(&pools[1]);
}

template <typename Pool>
static void TypesScript(bool original, bool runtime, std::string *log) {
    g_log = log;
    ArenaReset();
    Pool pool;
    memset(&pool, 0xee, sizeof(pool));
    char name[32];
    {
        Originals scope(original);
        // both kinds start as two SymbolPools (the runtime pool has no constructor of its own)
        if (original)
            ((PoolVoidFn)0x000e6390)(&pool, 0);
        else
            ((ConstructorPool *)&pool)->Construct();
        unsigned add = runtime ? 0x000f3b40 : 0x000f3a40, remove = runtime ? 0x000f3b20 : 0x000f3a20;
        unsigned findC = runtime ? 0x000f3b70 : 0x000f3a70, findD = runtime ? 0x000f3b90 : 0x000f3a90;
        Progress(original ? "types original" : "types ours", runtime ? "runtime: add" : "add");
        for (int i = 0; i < 300; i++) {
            snprintf(name, sizeof(name), "type%d", i % 220);
            void *c = (void *)(uintptr_t)(0x1000 + i * 16), *d = (void *)(uintptr_t)(0x900000 + i * 16);
            if (original)
                ((TypeAddFn)add)(&pool, 0, name, c, d);
            else
                pool.AddType(name, c, d);
        }
        Progress("types", "remove");
        for (int i = 0; i < 220; i += 3) {
            snprintf(name, sizeof(name), "type%d", i);
            if (original)
                ((TypeRemoveFn)remove)(&pool, 0, name);
            else
                pool.RemoveType(name);
        }
        Progress("types", "find");
        for (int i = 0; i < 240; i++) {
            snprintf(name, sizeof(name), "type%d", i);
            void *c = original ? ((TypeFindFn)findC)(&pool, 0, name) : pool.FindConstructor(name);
            void *d = original ? ((TypeFindFn)findD)(&pool, 0, name) : pool.FindDestructor(name);
            Logf("find %s %p %p", name, c, d);
        }
        DumpTypes((SymbolPool *)&pool);
        Progress("types", "empty / destruct");
        if (runtime) {
            RuntimeAllocConstructorPool *rp = (RuntimeAllocConstructorPool *)&pool;
            if (original)
                ((PoolVoidFn)0x000f3bb0)(&pool, 0);
            else
                rp->EmptyBoth();
            DumpTypes((SymbolPool *)&pool);
            if (original)
                ((TypeAddFn)add)(&pool, 0, "again", (void *)1, (void *)2);
            else
                pool.AddType("again", (void *)1, (void *)2);
            if (original)
                ((PoolVoidFn)0x000f3bd0)(&pool, 0);
            else
                rp->EmptyBoth2();
            DumpTypes((SymbolPool *)&pool);
        }
        if (original)
            ((PoolVoidFn)(runtime ? 0x000f3ab0 : 0x000f3bf0))(&pool, 0);
        else
            pool.Destruct();
        DumpTypes((SymbolPool *)&pool);
    }
    g_log = NULL;
}

// ---- reading the archives

struct Entry {
    std::string archive, name;
    std::vector<uint8_t> data;   // the file (unpacked), then zeros
    size_t size;
};

static const size_t kShapePad = 0x1000;

static bool HasExt(const std::string &name, const char *ext) {
    size_t n = strlen(ext);
    return name.size() > n && _stricmp(name.c_str() + name.size() - n, ext) == 0;
}

static void ReadArchive(const char *hostPath, const char *archive, std::vector<Entry> *out) {
    HANDLE file = CreateFileA(hostPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    uint8_t header[16];
    DWORD got = 0;
    ReadFile(file, header, 16, &got, NULL);
    int dirSize = got == 16 ? BIG_dirsize(header) : 0;
    std::vector<uint8_t> dir(dirSize > 16 ? dirSize : 16);
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    ReadFile(file, dir.data(), (DWORD)dirSize, &got, NULL);
    for (int i = 0;; i++) {
        int offset, size;
        const char *name = BIG_find(dir.data(), NULL, i, &offset, &size);
        if (name == NULL)
            break;
        std::string n = name;
        bool shape = HasExt(n, ".xsh");
        if (!HasExt(n, ".o") && !HasExt(n, ".dat") && !HasExt(n, ".rel") && !shape)
            continue;
        std::vector<uint8_t> raw(size + 16);
        SetFilePointer(file, offset, NULL, FILE_BEGIN);
        ReadFile(file, raw.data(), (DWORD)size, &got, NULL);
        Entry e;
        e.archive = archive;
        e.name = n;
        size_t pad = shape ? kShapePad : 0;
        unsigned unpacked = unpacksizez(raw.data());
        if (unpacked != 0) {
            e.data.assign(pad + unpacked + 64, 0);
            UNPACK_unpack(raw.data(), e.data.data() + pad);
            e.size = unpacked;
        } else {
            e.data.assign(pad, 0);
            e.data.insert(e.data.end(), raw.begin(), raw.begin() + size);
            e.data.resize(e.data.size() + 64, 0);
            e.size = (size_t)size;
        }
        out->push_back(e);
    }
    CloseHandle(file);
}

static bool IsElf(const Entry &e) {
    return e.size >= 0x34 && memcmp(e.data.data(), "\x7f" "ELF", 4) == 0;
}

// The disc's objects only use R_MIPS_32, section types 0-3 and 9, and nothing builds a RUNTIME_ALLOC object - so,
// the same for both sides, each object is perturbed once before it is checked: a quarter of its undefined names
// (those long enough) are renamed RUNTIME_ALLOC::... in place (the same length, so the string table is otherwise
// untouched), one relocation in eleven gets another type, one symbol in 23 a type the loader does not understand,
// one section of type 1 another type (each of the ones the loader reports or skips, in turn), and one object in
// nine has its .symtab renamed so it is not found.
static void Perturb(Entry *e, uint32_t salt) {
    static const uint8_t kRelocations[] = { 0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 4, 5, 6, 4, 5, 6 };
    static const uint32_t kSections[] = { 4, 5, 6, 7, 8, 10, 11, 0x70000005, 0x70000006, 0x7ffff41f, 0x7ffff420,
                                          0x7ffff421, 0x7ffff422, 0x12345 };
    uint8_t *d = e->data.data();
    uint32_t shoff = *(uint32_t *)(d + 0x20);
    int shnum = *(uint16_t *)(d + 0x30);
    int shstr = *(uint16_t *)(d + 0x32);
    if (shoff + (size_t)shnum * 40 > e->size || shstr >= shnum)
        return;
    uint32_t *sections = (uint32_t *)(d + shoff);
    bool retyped = false;
    for (int i = 0; i < shnum; i++) {
        uint32_t *s = sections + i * 10;
        if (s[4] + s[5] > e->size)
            continue;
        if (s[1] == 1 && !retyped) {
            s[1] = kSections[salt % (sizeof(kSections) / sizeof(kSections[0]))];
            retyped = true;
        } else if (s[1] == 9) {
            for (uint32_t k = 0; k < s[5] / 8; k++)
                if ((salt + k) % 11 == 0)
                    d[s[4] + k * 8 + 4] = kRelocations[(k / 11) % sizeof(kRelocations)];
        } else if (s[1] == 2 && s[6] < (uint32_t)shnum) {
            const uint32_t *strtab = sections + s[6] * 10;
            if (strtab[4] + strtab[5] > e->size)
                continue;
            for (uint32_t k = 1; k < s[5] / 16; k++) {
                uint8_t *sym = d + s[4] + k * 16;
                if (k % 23 == 5)
                    sym[12] = (uint8_t)((sym[12] & 0xf0) | (4 + k % 3));
                uint32_t nameAt = *(const uint32_t *)sym;
                if (*(const uint16_t *)(sym + 14) != 0 || nameAt >= strtab[5])
                    continue;
                char *name = (char *)d + strtab[4] + nameAt;
                char *bare = strstr(name, ":::");
                bare = bare != NULL ? bare + 3 : name;
                if (strlen(bare) >= 16 && Hash(bare) % 4 == 0)
                    memcpy(bare, "RUNTIME_ALLOC::", 15);
            }
            if (salt % 9 == 8) {
                char *name = (char *)d + sections[shstr * 10 + 4] + s[0];
                if (strcmp(name, ".symtab") == 0)
                    name[6] = 'X';
            }
        }
    }
}

// ---- RegisterShapes

static void ShapesScript(bool original, std::vector<Entry *> &shapes, std::string *log) {
    g_log = log;
    ArenaReset();
    uint8_t saved[sizeof(SymbolPool)];
    memcpy(saved, &GlobalPool, sizeof(SymbolPool));
    {
        Originals scope(original);
        for (Entry *e : shapes) {
            uint8_t *file = e->data.data() + kShapePad;
            if (e->size < 0x10 || memcmp(file, "SHPX", 4) != 0)
                continue;
            memset(&GlobalPool, 0, sizeof(SymbolPool));
            GlobalPool.Construct();
            Logf("%s", e->name.c_str());
            for (int pass = 0; pass < 2; pass++) {   // the second time every name is taken
                if (original)
                    ((ShapesFn)0x000ede60)(file);
                else
                    DynamicLoader::RegisterShapes(file);
            }
            DumpPool(&GlobalPool);
            if (original)
                ((ShapesFn)0x000edf50)(file);
            else
                DynamicLoader::UnRegisterShapes(file);
            DumpPool(&GlobalPool);
        }
    }
    memcpy(&GlobalPool, saved, sizeof(SymbolPool));
    g_log = NULL;
}

// ---- objects: the recording callbacks

static uint8_t g_resolved[4096], g_registered[4096], g_runtimeObjects[0x10000];
static int g_runtimeCount;

static void *RecordConstructor(void *data, void *loader) {
    Logf("construct %p for %p", data, loader);
    return data;
}

static void RecordDestructor(void *data) {
    Logf("destruct %p", data);
}

static void *RecordRuntimeConstructor(const char *properties, void *loader, void **context, char *destroy) {
    uint32_t h = Hash(properties);
    Logf("runtime construct \"%s\" for %p", properties, loader);
    *context = (void *)(uintptr_t)(h & 0xffff);
    *destroy = (char)((h % 3) != 0);
    if (h % 7 == 0)
        return NULL;
    return &g_runtimeObjects[(g_runtimeCount++ * 16) % sizeof(g_runtimeObjects)];
}

static void RecordRuntimeDestructor(void *object, void *context) {
    Logf("runtime destruct %p %p", object, context);
}

static void *RecordResolver(const char *name, char *found) {
    uint32_t h = Hash(name);
    Logf("resolver \"%s\"", name);
    if (strncmp(name, "RUNTIME_ALLOC::", 15) != 0 && h % 3 == 0) {
        *found = 1;
        return &g_resolved[(h % 1024) * 4];
    }
    *found = 0;
    return NULL;
}

// ---- objects: one group

struct Object {
    Entry *main, *rel;
    std::vector<uint8_t> image, second;   // where both sides load it
    uint32_t size;          // the size given: a split object is cut at its section table every other time
    size_t secondOffset;    // and its second part starts that far into the copy (the same bytes as the .dat)
    void *resolver;
    bool defer;
};

static void LoadGroup(bool original, std::vector<Object> &objects, std::vector<DynamicLoader> &loaders) {
    for (size_t k = 0; k < objects.size(); k++) {
        Object &o = objects[k];
        memcpy(o.image.data(), o.main->data.data(), o.image.size());
        if (o.rel != NULL)
            memcpy(o.second.data(), o.rel->data.data(), o.second.size());
        DynamicLoader *l = &loaders[k];
        memset(l, 0xee, sizeof(*l));
        Logf("load %s%s%s", o.main->name.c_str(), o.rel != NULL ? " + rel" : "", o.defer ? " deferred" : "");
        if (o.rel == NULL) {
            if (original)
                ((LoadFn)0x000e62f0)(l, 0, o.image.data(), o.size, o.resolver);
            else
                l->Construct(o.image.data(), o.size, o.resolver);
        } else {
            if (original)
                ((LoadSplitFn)0x000e6330)(l, 0, o.image.data(), o.size, o.second.data() + o.secondOffset,
                                          o.defer ? 1 : 0, o.resolver);
            else
                l->ConstructSplit(o.image.data(), o.size, o.second.data() + o.secondOffset, o.defer ? 1 : 0, o.resolver);
        }
    }
    // the deferred ones, then every one again (already resolved: nothing happens)
    for (int pass = 0; pass < 2; pass++)
        for (size_t k = 0; k < objects.size(); k++) {
            if (pass == 0 && !objects[k].defer)
                continue;
            if (original)
                ((LoaderVoidFn)0x000e5e80)(&loaders[k], 0);
            else
                loaders[k].Resolve();
        }
}

static void ReadBack(bool original, std::vector<DynamicLoader> &loaders) {
    for (size_t k = 0; k < loaders.size(); k++) {
        DynamicLoader *l = &loaders[k];
        void *elf = original ? ((ElfDataFn)0x000e5ae0)(l, 0) : l->GetElfData();
        Logf("object %u elf %p", (unsigned)k, elf);
        std::set<std::string> classes;
        for (int i = -1;; i++) {
            LoaderSymbol s;
            memset(&s, 0, sizeof(s));
            LoaderSymbol *r = original ? ((GetSymbolFn)0x000e5a10)(l, 0, &s, i) : l->GetSymbol(&s, i);
            if (r != &s)
                Report("GetSymbol", "does not answer its output");
            if (s.name == NULL) {
                if (i >= 0)
                    break;
                continue;
            }
            Logf("symbol %d \"%s\" \"%s\" %p %d", i, s.name, s.className, s.address, (int)(uint8_t)s.defined);
            classes.insert(s.className);
            void *a = (void *)1;
            bool ok = original ? ((GetAddrFn)0x000e57e0)(l, 0, s.className, s.name, &a)
                               : l->GetAddr(s.className, s.name, &a);
            void *found = original ? ((LookupFn)0x000e5af0)(l->table, s.name) : EAGL_LookupInObject(l->table, s.name);
            Logf("  addr %d %p lookup %p", (int)ok, a, found);
        }
        void *a = (void *)1;
        bool ok = original ? ((GetAddrFn)0x000e57e0)(l, 0, "", "no such symbol", &a)
                           : l->GetAddr("", "no such symbol", &a);
        Logf("missing %d %p", (int)ok, a);
        for (const std::string &c : classes) {
            int index = 0;
            void *addr;
            while (original ? ((GetNextAddrFn)0x000e5c80)(l, 0, c.c_str(), &index, &addr)
                            : l->GetNextAddr(c.c_str(), &index, &addr))
                Logf("next addr \"%s\" %d %p", c.c_str(), index, addr);
            index = 0;
            LoaderSymbol s;
            while (original ? ((GetNextSymbolFn)0x000e5bc0)(l, 0, c.c_str(), &index, &s)
                            : l->GetNextSymbol(c.c_str(), &index, &s))
                Logf("next symbol %d \"%s\" %p", index, s.name, s.address);
        }
    }
}

struct Snapshot {
    std::string log;
    std::vector<std::vector<uint8_t>> images;
    std::vector<uint8_t> arena, loaders;
    void *loadedAfter;
};

static void GroupSide(bool original, std::vector<Object> &objects, std::vector<DynamicLoader> &loaders,
                      Snapshot *out) {
    g_log = &out->log;
    ArenaReset();
    g_runtimeCount = 0;
    LoadedTables = NULL;
    {
        Originals scope(original);
        LoadGroup(original, objects, loaders);
        // the snapshot: each object's own bytes, the arena (the copy's padding masked: the original copies stack
        // garbage into a symbol table's bytes +0x0d..+0x0f), the loaders
        for (Object &o : objects) {
            out->images.push_back(o.image);
            if (o.rel != NULL)
                out->images.push_back(o.second);
        }
        out->arena.assign(g_arena, g_arena + g_arenaUsed);
        for (DynamicLoader &l : loaders) {
            uint8_t *t = (uint8_t *)l.table;
            if (t >= g_arena && t + 0x10 <= g_arena + g_arenaUsed)
                memset(out->arena.data() + (t - g_arena) + 0xd, 0, 3);
        }
        out->loaders.assign((uint8_t *)loaders.data(), (uint8_t *)(loaders.data() + loaders.size()));
        Logf("loaded list %p", LoadedTables);
        ReadBack(original, loaders);
        for (size_t k = loaders.size(); k-- > 0;) {
            Logf("destroy %u", (unsigned)k);
            if (original)
                ((LoaderVoidFn)0x000e6380)(&loaders[k], 0);
            else
                loaders[k].Destruct();
        }
        out->loadedAfter = LoadedTables;
        Logf("loaded list after %p", LoadedTables);
    }
    g_log = NULL;
}

// The port, alone, with empty pools: every class, and every name looked up outside its object.
static void Discover(std::vector<Object> &objects, std::set<std::string> *classes, std::set<std::string> *outside) {
    std::vector<DynamicLoader> loaders(objects.size());
    LoadedTables = NULL;
    for (size_t k = 0; k < objects.size(); k++) {
        Object &o = objects[k];
        memcpy(o.image.data(), o.main->data.data(), o.image.size());
        if (o.rel != NULL) {
            memcpy(o.second.data(), o.rel->data.data(), o.second.size());
            loaders[k].ConstructSplit(o.image.data(), o.size, o.second.data() + o.secondOffset, 0, NULL);
        } else {
            loaders[k].Construct(o.image.data(), o.size, NULL);
        }
        for (int i = 0;; i++) {
            LoaderSymbol s;
            if (loaders[k].GetSymbol(&s, i)->name == NULL)
                break;
            classes->insert(s.className);
            if ((uint8_t)s.defined == 0)
                outside->insert(s.name);
        }
    }
    for (size_t k = loaders.size(); k-- > 0;)
        loaders[k].Destruct();
}

static void CheckGroup(const char *label, std::vector<Object> &objects) {
    // the pools: empty for the discovery, then a recorder for every class and a third of the outside names
    memset(&GlobalPool, 0, sizeof(SymbolPool));
    memset(&TheRuntimeAllocPool, 0, sizeof(RuntimeAllocConstructorPool));
    memset(&TheConstructorPool, 0, sizeof(ConstructorPool));
    GlobalPool.Construct();
    TheConstructorPool.Construct();
    ((ConstructorPool *)&TheRuntimeAllocPool)->Construct();
    std::set<std::string> classes, outside;
    Progress(label, "discovery");
    Discover(objects, &classes, &outside);
    Progress(label, "registering");
    for (const std::string &c : classes) {
        TheConstructorPool.AddType(c.c_str(), (void *)RecordConstructor, (void *)RecordDestructor);
        TheRuntimeAllocPool.AddType(c.c_str(), (void *)RecordRuntimeConstructor, (void *)RecordRuntimeDestructor);
    }
    for (const std::string &n : outside) {
        uint32_t h = Hash(n.c_str());
        if (strncmp(n.c_str(), "RUNTIME_ALLOC::", 15) != 0 && h % 3 == 1)
            GlobalPool.AddSymbol(n.c_str(), &g_registered[(h % 1024) * 4]);
    }
    Snapshot o, p;
    std::vector<DynamicLoader> loaders(objects.size());   // the same for both sides
    g_inArena = true;
    Progress(label, "original");
    GroupSide(true, objects, loaders, &o);
    Progress(label, "ours");
    GroupSide(false, objects, loaders, &p);
    Progress(label, "comparing");
    g_inArena = false;
    char what[200];
    const char *kinds[] = { "construct ", "runtime construct ", "resolver ", "message ", "load " };
    for (int i = 0; i < 5; i++)
        g_counts[i] += CountLines(o.log, kinds[i]);
    snprintf(what, sizeof(what), "%s: the log", label);
    CompareLogs(what, o.log, p.log);
    for (size_t i = 0; i < o.images.size() && i < p.images.size(); i++) {
        snprintf(what, sizeof(what), "%s: image %u", label, (unsigned)i);
        CompareBytes(what, o.images[i], p.images[i]);
    }
    snprintf(what, sizeof(what), "%s: the arena", label);
    CompareBytes(what, o.arena, p.arena);
    snprintf(what, sizeof(what), "%s: the loaders", label);
    CompareBytes(what, o.loaders, p.loaders);
    g_checks++;
    if (o.loadedAfter != NULL || p.loadedAfter != NULL)
        Report(label, "objects left in the loaded list");
    TheConstructorPool.Destruct();
    ((ConstructorPool *)&TheRuntimeAllocPool)->Destruct();
    GlobalPool.Destruct();
}

static Object MakeObject(Entry *main, Entry *rel, size_t index) {
    Object o;
    o.main = main;
    o.rel = rel;
    o.image.assign(main->data.size(), 0);
    if (rel != NULL)
        o.second.assign(rel->data.size(), 0);
    o.size = (uint32_t)main->size;
    o.secondOffset = 0;
    uint32_t sections = *(const uint32_t *)(main->data.data() + 0x20);
    if (rel != NULL && index % 2 == 0 && sections > 0x34 && sections < o.size && rel->size == main->size) {
        o.size = sections & ~3u;
        o.secondOffset = o.size;
    }
    o.resolver = index % 4 == 3 ? NULL : (void *)RecordResolver;
    o.defer = rel != NULL && index % 5 == 4;
    return o;
}

static void ReadFolder(const char *folder, std::vector<std::string> *archives) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*.viv", folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
        return;
    do
        archives->push_back(found.cFileName);
    while (FindNextFileA(search, &found));
    FindClose(search);
}

static std::string Stem(const std::string &name) {
    std::string stem = name.substr(0, name.rfind('.'));
    for (char &c : stem)
        c = (char)tolower((unsigned char)c);
    return stem;
}

// One archive: its image containers through RegisterShapes, then its objects (after misc.viv's .o files).
static void CheckArchive(const char *archive, std::vector<Entry> &entries, const std::vector<Entry *> &common,
                         int *objectCount, int *groups, int *shapeCount) {
    std::vector<Entry *> shapes;
    for (Entry &e : entries)
        if (HasExt(e.name, ".xsh"))
            shapes.push_back(&e);
    if (!shapes.empty()) {
        Progress(archive, "shapes");
        std::string o, p;
        g_inArena = true;
        ShapesScript(true, shapes, &o);
        ShapesScript(false, shapes, &p);
        g_inArena = false;
        char what[200];
        snprintf(what, sizeof(what), "%s: RegisterShapes", archive);
        CompareLogs(what, o, p);
        *shapeCount += (int)shapes.size();
    }
    std::map<std::string, Entry *> rels;
    for (Entry &e : entries)
        if (HasExt(e.name, ".rel"))
            rels[Stem(e.name)] = &e;
    std::vector<Object> objects;
    bool misc = _stricmp(archive, "misc.viv") == 0;
    if (!misc)
        for (Entry *e : common)
            objects.push_back(MakeObject(e, NULL, objects.size()));
    size_t own = 0;
    for (Entry &e : entries) {
        if (!IsElf(e))
            continue;
        if (HasExt(e.name, ".o")) {
            objects.push_back(MakeObject(&e, NULL, objects.size()));
            own++;
        } else if (HasExt(e.name, ".dat")) {
            auto rel = rels.find(Stem(e.name));
            objects.push_back(MakeObject(&e, rel != rels.end() ? rel->second : NULL, objects.size()));
            own++;
        }
    }
    if (own == 0)
        return;
    if (!misc)   // misc.viv's own were perturbed once, as the common objects
        for (size_t k = common.size(); k < objects.size(); k++)
            Perturb(objects[k].main, (uint32_t)k);
    CheckGroup(archive, objects);
    *objectCount += (int)own;
    (*groups)++;
}

}   // namespace

void LoaderShadow_Run(void) {
    if (getenv("NIGHTFIRE_LDSHADOW") == NULL)
        return;
    char folder[MAX_PATH], path[MAX_PATH];
    if (!Xbox_ResolvePath("D:\\driving", folder, sizeof(folder)))
        return;
    std::vector<std::string> archives;
    ReadFolder(folder, &archives);
    g_arena = (uint8_t *)VirtualAlloc(NULL, kArena, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_arena == NULL)
        return;
    g_arenaUsed = kArena;

    // the game's state, put back at the end
    EaglMallocFn savedMalloc = EaglMalloc;
    EaglFreeFn savedFree = EaglFree;
    void *savedPrint = PrintHook, *savedLoaded = LoadedTables;
    uint8_t savedPools[0x14 + 0x28 + 0x28];
    memcpy(savedPools, &GlobalPool, 0x14);
    memcpy(savedPools + 0x14, &TheRuntimeAllocPool, 0x28);
    memcpy(savedPools + 0x3c, &TheConstructorPool, 0x28);
    EaglMalloc = ShadowMalloc;
    EaglFree = ShadowFree;
    PrintHook = (void *)ShadowPrint;

    Progress("pools", "");
    g_inArena = true;
    const uint32_t sizes[] = { PoolSize, 3 };
    for (uint32_t size : sizes) {
        std::string o, p;
        PoolScript(true, size, &o);
        PoolScript(false, size, &p);
        CompareLogs(size == 3 ? "SymbolPool (size 3)" : "SymbolPool", o, p);
    }
    Progress("constructor pools", "");
    {
        std::string o, p;
        TypesScript<ConstructorPool>(true, false, &o);
        TypesScript<ConstructorPool>(false, false, &p);
        CompareLogs("ConstructorPool", o, p);
        o.clear();
        p.clear();
        TypesScript<RuntimeAllocConstructorPool>(true, true, &o);
        TypesScript<RuntimeAllocConstructorPool>(false, true, &p);
        CompareLogs("RuntimeAllocConstructorPool", o, p);
    }
    g_inArena = false;

    Progress("reading", "misc.viv");
    // misc.viv first: its .o files go in front of every other archive's objects
    std::vector<Entry> miscEntries;
    snprintf(path, sizeof(path), "%s\\misc.viv", folder);
    ReadArchive(path, "misc.viv", &miscEntries);
    std::vector<Entry *> common;
    for (Entry &e : miscEntries)
        if (HasExt(e.name, ".o") && IsElf(e))
            common.push_back(&e);
    for (size_t k = 0; k < common.size(); k++)
        Perturb(common[k], (uint32_t)k);
    int objectCount = 0, groups = 0, shapeCount = 0;
    CheckArchive("misc.viv", miscEntries, common, &objectCount, &groups, &shapeCount);
    for (const std::string &archive : archives) {
        if (_stricmp(archive.c_str(), "misc.viv") == 0)
            continue;
        std::vector<Entry> entries;
        snprintf(path, sizeof(path), "%s\\%s", folder, archive.c_str());
        ReadArchive(path, archive.c_str(), &entries);
        CheckArchive(archive.c_str(), entries, common, &objectCount, &groups, &shapeCount);
    }

    EaglMalloc = savedMalloc;
    EaglFree = savedFree;
    PrintHook = savedPrint;
    LoadedTables = savedLoaded;
    memcpy(&GlobalPool, savedPools, 0x14);
    memcpy(&TheRuntimeAllocPool, savedPools + 0x14, 0x28);
    memcpy(&TheConstructorPool, savedPools + 0x3c, 0x28);
    VirtualFree(g_arena, 0, MEM_RELEASE);
    printf("[ldshadow] %d objects in %d archives (%d loads: %d constructions, %d RUNTIME_ALLOC, %d resolver calls, "
           "%d messages), %d image containers, %d checks: %s\n", objectCount, groups, g_counts[4], g_counts[0],
           g_counts[1], g_counts[2], g_counts[3], shapeCount, g_checks,
           g_failures == 0 ? "the same as the original" : "FAILED");
    fflush(stdout);
}
