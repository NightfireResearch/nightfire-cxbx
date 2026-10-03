#include "Loader.h"

#include "Realgraph.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGL's dynamic loader (docs/driving/eagl.md 3.1). Every model, skeleton, animation bank and render-method library
// on the disc is a MIPS ELF object used as a data container: the loader parses its sections, rewrites "__Class:::
// Name" symbols as "Name\0\x7fClass", hashes the symbols into a 256-bucket table linked into a global list, applies
// the relocations (R_MIPS_32, _26, HI16/LO16) - resolving undefined symbols from the other loaded objects, the
// game's resolver, the global name pool, or by building a RUNTIME_ALLOC:: object through its type's constructor -
// and then runs the constructor registered for each symbol's class. Around it, the name pools: an open-addressed
// hash table (SymbolPool), and two of them per constructor pool (constructors, destructors).
//
// Each function is the original at the same address, ported from it; the state stays where the original keeps it
// (the pools at 0x0023fb8c, 0x0023fbb8 and 0x0023fbe0, the list of loaded objects at 0x0023fb88), allocations go
// through EAGL's allocator hooks with the original's name strings, and diagnostics through the original PrintMessage.
// devtools/LoaderShadow.cpp loads every object on the disc with both and compares the results.
// ---------------------------------------------------------------------------------------------------------------

#define EaglMalloc   (*(void *(**)(uint32_t size, const char *name))0x001caf68u)
#define EaglFree     (*(void (**)(void *data, uint32_t size))0x001caf6cu)
#define PoolSize     (*(uint32_t *)0x001cdc50u)                  // a new pool's table size (256)
#define LoadedTables (*(HashTable **)0x0023fb88u)
#define GlobalPool   ((SymbolPool *)0x0023fb8cu)
#define RuntimePool  ((RuntimeAllocConstructorPool *)0x0023fbb8u)
#define CtorPool     ((ConstructorPool *)0x0023fbe0u)

// EAGL's PrintMessage (0x000f42b0, not ours yet): level, format, arguments.
#define PrintMessage ((int (*)(int level, const char *format, ...))0x000f42b0u)

// ---- SymbolPool

// FUNC_AT(0x000f3c70)
SymbolPool* SymbolPool::Construct() {
    count = 0;
    size = PoolSize;
    table = NULL;
    resolvers = NULL;
    return this;
}

static uint32_t PoolHash(const char *name) {
    uint32_t h = 0;
    for (; *name != 0; name++)
        h ^= (h << 5) ^ (uint32_t)(int32_t)(signed char)*name;
    return h;
}

// AUTOINJECT
uint32_t SymbolPool::HashFunction(const char *name) {
    return PoolHash(name) % size;
}

// Into the first free slot from the name's; the table doubles (and everything is inserted again) when full.
// AUTOINJECT
void SymbolPool::Insert(const char *name, SymbolEntry *entry) {
    if (table == NULL) {
        size = PoolSize;
        table = (SymbolEntry **)EaglMalloc(PoolSize << 2, (const char *)0x001cdc54u);
        for (uint32_t i = 0; i < size; i++)
            table[i] = NULL;
    }
    uint32_t i = PoolHash(name) % size;
    while (table[i] != NULL) {
        if (++i >= size)
            i = 0;
    }
    table[i] = entry;
    uint32_t oldSize = size;
    if (oldSize <= ++count) {
        SymbolEntry **old = table;
        size = oldSize * 2;
        table = (SymbolEntry **)EaglMalloc(oldSize << 3, (const char *)0x001cdc74u);
        for (uint32_t j = 0; j < size; j++)
            table[j] = NULL;
        count = 0;
        for (int j = 0; j < (int)oldSize; j++)
            Insert(old[j]->name, old[j]);
        if (old != NULL)
            EaglFree(old, oldSize);   // the original passes the entry count as the size
    }
}

// A copy of the name with the value, inserted; answers the copy's name.
// AUTOINJECT
void* SymbolPool::AddSymbol(const char *name, void *value) {
    size_t n = strlen(name);
    SymbolEntry *entry = (SymbolEntry *)EaglMalloc((uint32_t)(n + 5), (const char *)0x001cdc94u);
    entry->value = value;
    memcpy(entry->name, name, n + 1);
    Insert(name, entry);
    return entry->name;
}

// Frees the entry and empties its slot (later entries are found anyway: a search looks at every slot).
// AUTOINJECT
void SymbolPool::RemoveSymbol(const char *name) {
    if (table == NULL)
        return;
    uint32_t i = HashFunction(name);
    for (uint32_t probes = 0;; probes++) {
        SymbolEntry *e = table[i];
        if (e != NULL && strcmp(name, e->name) == 0) {
            EaglFree(e, (uint32_t)strlen(e->name) + 5);
            table[i] = NULL;
            count--;
            return;
        }
        if (size < probes)
            return;
        if (++i >= size)
            i = 0;
    }
}

// The value for a name; a miss walks the whole table. (The fallback resolvers at +0x10 are only consulted when the
// slot that matched has gone empty under it, which cannot happen; they are kept as the original has them.)
// AUTOINJECT
void* SymbolPool::Search(const char *name, bool *found) {
    SymbolEntry **t = table;
    if (t == NULL) {
        *found = false;
        return NULL;
    }
    uint32_t i = HashFunction(name);
    for (uint32_t probes = 0;; probes++) {
        SymbolEntry *e = t[i];
        if (e != NULL && strcmp(name, e->name) == 0) {
            SymbolEntry *again = t[i];
            if (again != NULL) {
                *found = true;
                return again->value;
            }
            *found = false;
            void *r = NULL;
            for (SymbolResolver *s = resolvers; s != NULL && r == NULL; s = s->next)
                r = s->lookup(name, found);
            return r;
        }
        if (size < probes) {
            *found = false;
            return NULL;
        }
        if (++i >= size)
            i = 0;
    }
}

// AUTOINJECT
void SymbolPool::Empty() {
    if (table != NULL) {
        for (uint32_t i = 0; i < size; i++) {
            SymbolEntry *e = table[i];
            if (e != NULL)
                EaglFree(e, (uint32_t)strlen(e->name) + 5);
            table[i] = NULL;
        }
    }
    count = 0;
    while (resolvers != NULL) {
        SymbolResolver *r = resolvers;
        resolvers = r->next;
        EaglFree(r, 0xc);
    }
    resolvers = NULL;
}

// FUNC_AT(0x000f41d0)
void SymbolPool::Destruct() {
    Empty();
    if (table != NULL) {
        EaglFree(table, size << 2);
        table = NULL;
    }
}

// ---- the constructor pools: each a pair of SymbolPools, constructors at +0, destructors at +0x14

// FUNC_AT(0x000e6390)
ConstructorPool* ConstructorPool::Construct() {
    constructors.Construct();
    destructors.Construct();
    return this;
}

// The body's two destructions and then the members' own, as compiled.
// FUNC_AT(0x000f3bf0)
void ConstructorPool::Destruct() {
    constructors.Destruct();
    destructors.Destruct();
    destructors.Destruct();
    constructors.Destruct();
}

// FUNC_AT(0x000f3a40)
void ConstructorPool::AddType(const char *name, void *constructor, void *destructor) {
    constructors.AddSymbol(name, constructor);
    destructors.AddSymbol(name, destructor);
}

// FUNC_AT(0x000f3a20)
void ConstructorPool::RemoveType(const char *name) {
    constructors.RemoveSymbol(name);
    destructors.RemoveSymbol(name);
}

// FUNC_AT(0x000f3a70)
void* ConstructorPool::FindConstructor(const char *name) {
    bool found;
    void *r = constructors.Search(name, &found);
    return found ? r : NULL;
}

// FUNC_AT(0x000f3a90)
void* ConstructorPool::FindDestructor(const char *name) {
    bool found;
    void *r = destructors.Search(name, &found);
    return found ? r : NULL;
}

// FUNC_AT(0x000f3ab0)
void RuntimeAllocConstructorPool::Destruct() {
    constructors.Destruct();
    destructors.Destruct();
    destructors.Destruct();
    constructors.Destruct();
}

// FUNC_AT(0x000f3b40)
void RuntimeAllocConstructorPool::AddType(const char *name, void *constructor, void *destructor) {
    constructors.AddSymbol(name, constructor);
    destructors.AddSymbol(name, destructor);
}

// FUNC_AT(0x000f3b20)
void RuntimeAllocConstructorPool::RemoveType(const char *name) {
    constructors.RemoveSymbol(name);
    destructors.RemoveSymbol(name);
}

// FUNC_AT(0x000f3b70)
void* RuntimeAllocConstructorPool::FindConstructor(const char *name) {
    bool found;
    void *r = constructors.Search(name, &found);
    return found ? r : NULL;
}

// FUNC_AT(0x000f3b90)
void* RuntimeAllocConstructorPool::FindDestructor(const char *name) {
    bool found;
    void *r = destructors.Search(name, &found);
    return found ? r : NULL;
}

// FUNC_AT(0x000f3bb0)
void RuntimeAllocConstructorPool::EmptyBoth() {
    constructors.Empty();
    destructors.Empty();
}

// FUNC_AT(0x000f3bd0)
void RuntimeAllocConstructorPool::EmptyBoth2() {
    constructors.Empty();
    destructors.Empty();
}

// The matrices EAGL registers by name, for render methods to address (EAGL::ViewPort::gp*Matrix).
// FUNC_AT(0x000f39c0)
void EAGL_SymbolInit() {
    DynamicLoader::RegisterVar((const char *)0x001cdb74u, (void *)0x0023fa10u);
    DynamicLoader::RegisterVar((const char *)0x001cdb98u, (void *)0x0023f950u);
    DynamicLoader::RegisterVar((const char *)0x001cdbb8u, (void *)0x0023fa50u);
    DynamicLoader::RegisterVar((const char *)0x001cdbd8u, (void *)0x0023f990u);
    DynamicLoader::RegisterVar((const char *)0x001cdbfcu, (void *)0x0023fa90u);
    DynamicLoader::RegisterVar((const char *)0x001cdc24u, (void *)0x0023f9d0u);
}

// ---- a loaded object's symbol table

struct ElfSymbol {          // Elf32_Sym
    uint32_t name;
    uint32_t value;
    uint32_t size;
    uint8_t info;
    uint8_t other;          // 1: value is absolute; 2: resolved from outside (the loader's marks)
    uint16_t shndx;
};

struct ElfSection {         // Elf32_Shdr; offset, link and info become pointers as the loader goes
    uint32_t name, type, flags, addr;
    uint32_t offset;
    uint32_t size;
    uint32_t link, info, align, entsize;
};

struct HashTable {          // 0x428, "EAGL::HashPointer new"
    HashTable *next;        // +0x000 the list of loaded objects
    HashTable *previous;    // +0x004
    const char *strings;    // +0x008 .strtab
    uint8_t resolved;       // +0x00c
    uint8_t pad[3];
    int symbolCount;        // +0x010
    ElfSymbol *symbols;     // +0x014 .symtab
    ElfSection *sections;   // +0x018
    uint8_t *header;        // +0x01c the ELF header
    int32_t buckets[256];   // +0x020
    int32_t *chain;         // +0x420
    void *(*resolver)(const char *name, char *found);   // +0x424
};
static_assert(sizeof(HashTable) == 0x428, "a HashTable is 0x428 bytes");

template <typename T> static inline T At(const void *p, int offset) {
    T v;
    memcpy(&v, (const uint8_t *)p + offset, sizeof(T));
    return v;
}

// The ELF hash of a symbol name, to 8 bits (0x000e51d0: name in EDX).
static uint32_t ElfHash(const char *name) {
    uint32_t h = 0;
    for (; *name != 0; name++) {
        h = h * 0x10 + (uint32_t)(int32_t)(signed char)*name;
        uint32_t g = h & 0xf0000000;
        if (g != 0)
            h ^= g >> 0x18;
        h &= ~g;
    }
    return h & 0xff;
}

static inline uint32_t SectionBase(const HashTable *t, uint32_t index) {
    return t->sections[index].offset;
}

// ---- DynamicLoader

// FUNC_AT(0x000e62f0)
DynamicLoader* DynamicLoader::Construct(void *elfData, uint32_t size, void *resolver) {
    constructorCount = 0;
    destructors = NULL;
    runtimeAllocs = NULL;
    second = NULL;
    elf = (uint8_t *)elfData;
    elfSize = size;
    Initialize(resolver);
    Resolve();
    return this;
}

// A .dat with its .rel: offsets past the first part's end resolve into the second.
// FUNC_AT(0x000e6330)
DynamicLoader* DynamicLoader::ConstructSplit(void *elfData, uint32_t size, void *secondPart, char deferResolve,
                                             void *resolver) {
    elf = (uint8_t *)elfData;
    second = (uint8_t *)secondPart;
    constructorCount = 0;
    destructors = NULL;
    runtimeAllocs = NULL;
    elfSize = size;
    Initialize(resolver);
    if (deferResolve == 0)
        Resolve();
    return this;
}

// FUNC_AT(0x000e6380)
void DynamicLoader::Destruct() {
    Release();
    RunDestructors();
}

// A file offset to memory: in the first part, the second part, or (just past the end, without a second) the end.
static inline uint32_t FileAddress(const DynamicLoader *l, uint32_t offset) {
    if (offset < l->elfSize)
        return (uint32_t)(uintptr_t)l->elf + offset;
    if (l->second != NULL)
        return (uint32_t)(uintptr_t)l->second + (offset - l->elfSize);
    if (offset <= l->elfSize)
        return (uint32_t)(uintptr_t)l->elf + offset;
    return 0;
}

// Sections parsed (their offsets, links and infos made pointers in place), names rewritten, symbols hashed.
// AUTOINJECT
void DynamicLoader::Initialize(void *resolver) {
    HashTable t;
    memset(&t, 0, sizeof(t));
    t.header = elf;
    ElfSection *sections = (ElfSection *)(uintptr_t)FileAddress(this, At<uint32_t>(elf, 0x20));
    t.sections = sections;
    int sectionCount = At<uint16_t>(elf, 0x30);
    for (int i = 0; i < sectionCount; i++)
        sections[i].offset = FileAddress(this, sections[i].offset);
    const char *sectionNames = (const char *)(uintptr_t)sections[At<uint16_t>(elf, 0x32)].offset;
    for (int i = 0; i < sectionCount; i++) {
        ElfSection *s = &sections[i];
        uint32_t type = s->type;
        const char *name = sectionNames + s->name;
        if (type < 0x70000006) {
            if (type == 0x70000005)
                continue;
            switch (type) {
            case 0: case 1:
                break;
            case 2:   // SHT_SYMTAB
                s->link = SectionBase(&t, s->link);
                if (strcmp(".symtab", name) == 0) {
                    t.symbols = (ElfSymbol *)(uintptr_t)s->offset;
                    t.symbolCount = (int32_t)s->size / 16;
                }
                break;
            case 3:   // SHT_STRTAB: "__Class:::Name" -> "Name\0\x7fClass"
                if (strcmp(".strtab", name) == 0) {
                    char *p = (char *)(uintptr_t)s->offset;
                    t.strings = p;
                    for (int left = (int32_t)s->size; left > 0;) {
                        size_t length = strlen(p);
                        if (p[0] == '_' && p[1] == '_') {
                            char *separator = strstr(p + 2, ":::");
                            if (separator != NULL) {
                                char className[128];
                                *separator = 0;
                                strcpy(className, p + 2);
                                const char *after = separator + 3;
                                size_t n = strlen(after) + 1;
                                memmove(p, after, n);
                                p[n] = 0x7f;
                                strcpy(p + n + 1, className);
                            }
                        }
                        left -= (int)length + 1;
                        p += length + 1;
                    }
                }
                break;
            case 4:
                if (s->size != 0)
                    PrintMessage(0, (const char *)0x001cb590u);
                break;
            case 5:
                PrintMessage(0, (const char *)0x001cb5ccu);
                break;
            case 6:
                PrintMessage(0, (const char *)0x001cb5fcu);
                break;
            case 8:
                if (s->size != 0)
                    PrintMessage(0, (const char *)0x001cb62cu);
                break;
            case 9:   // SHT_REL: the section relocated, and the symbol table
                s->info = SectionBase(&t, s->info);
                s->link = SectionBase(&t, s->link);
                break;
            case 10:
                PrintMessage(0, (const char *)0x001cb668u);
                break;
            case 11:
                PrintMessage(0, (const char *)0x001cb698u);
                break;
            default:
                PrintMessage(0, (const char *)0x001cb6d0u, type, i);
                break;
            }
        } else if (type != 0x70000006 && (type < 0x7ffff420 || type > 0x7ffff421)) {
            PrintMessage(0, (const char *)0x001cb6d0u, type, i);
        }
    }
    if (sectionCount == 0 || t.symbols == NULL)
        PrintMessage(0, (const char *)0x001cb714u);
    t.resolver = (void *(*)(const char *, char *))resolver;
    for (int i = 0; i < 256; i++)
        t.buckets[i] = -1;
    t.chain = (int32_t *)EaglMalloc((uint32_t)(t.symbolCount * 4), (const char *)0x001cb748u);
    HashTable *table = (HashTable *)EaglMalloc(0x428, (const char *)0x001cb920u);
    if (table != NULL) {   // the constructor's work, overwritten at once
        table->next = NULL;
        table->strings = NULL;
        table->resolved = 0;
        table->symbolCount = 0;
        table->symbols = NULL;
        table->sections = NULL;
        table->header = NULL;
        table->chain = NULL;
        table->resolver = NULL;
    }
    for (int i = 0; i < t.symbolCount; i++) {
        uint32_t h = ElfHash(t.strings + t.symbols[i].name);
        t.chain[i] = t.buckets[h];
        t.buckets[h] = i;
    }
    memcpy(table, &t, sizeof(t));
    table->next = LoadedTables;
    if (LoadedTables != NULL)
        LoadedTables->previous = table;
    table->previous = NULL;
    this->table = table;
    LoadedTables = table;
}

// A symbol in another loaded object, by name, if it is defined there (0x000e5af0).
// FUNC_AT(0x000e5af0)
void* EAGL_LookupInObject(HashTable *t, const char *name) {
    ElfSymbol *symbols = t->symbols;
    for (uint32_t j = (uint32_t)t->buckets[ElfHash(name)]; j != 0xffffffff; j = (uint32_t)t->chain[j]) {
        if ((uint32_t)t->symbolCount < j)
            PrintMessage(0, (const char *)0x001cb8b0u, j, t->symbolCount);
        ElfSymbol *s = &symbols[j];
        if (strcmp(name, t->strings + s->name) == 0 && s->shndx != 0 && s->shndx < At<uint16_t>(t->header, 0x30))
            return (void *)(uintptr_t)(SectionBase(t, s->shndx) + symbols[j].value);
    }
    return NULL;
}

struct RuntimeAllocRecord {   // "EAGL::DynamicLoader::RuntimeAllocDestructorEntry new"
    void (*destructor)(void *object, void *context);
    void *object;
    void *context;
    RuntimeAllocRecord *next;
};

typedef void *(*RuntimeAllocConstructor)(const char *properties, DynamicLoader *loader, void **context,
                                         char *destroy);

// Every relocation applied, then the constructors run. Once.
// AUTOINJECT
void DynamicLoader::Resolve() {
    HashTable *t = table;
    if (t == NULL || t->resolved != 0)
        return;
    ElfSection *sections = t->sections;
    uint8_t *header = t->header;
    t->resolved = 1;
    for (int si = 0; si < At<uint16_t>(header, 0x30); si++) {
        ElfSection *sec = &sections[si];
        if (sec->type != 9)
            continue;
        uint32_t target = sec->info;
        uint32_t *rel = (uint32_t *)(uintptr_t)sec->offset;
        ElfSymbol *symbols = (ElfSymbol *)(uintptr_t)sec->link;
        for (int count = (int32_t)sec->size / 8; count != 0; count--, rel += 2) {
            ElfSymbol *sym = &symbols[rel[1] >> 8];
            uint32_t S = 0;
            bool understood = false;
            while ((sym->info & 0xf) < 4) {
                if (sym->shndx != 0 && sym->shndx < At<uint16_t>(header, 0x30)) {
                    S = SectionBase(t, sym->shndx) + sym->value;
                    understood = true;
                    break;
                }
                sym->other = 2;
                const char *name = t->strings + sym->name;
                void *address = NULL;
                bool resolved = false;
                for (HashTable *o = LoadedTables; o != NULL; o = o->next) {
                    if (o != t && (address = EAGL_LookupInObject(o, name)) != NULL) {
                        resolved = true;
                        break;
                    }
                }
                if (!resolved) {
                    char found = 0;
                    if (t->resolver != NULL) {
                        address = t->resolver(name, &found);
                        resolved = found != 0;
                    }
                    if (!resolved) {
                        bool inPool;
                        address = GlobalPool->Search(name, &inPool);
                        resolved = inPool;
                    }
                }
                if (!resolved) {
                    const char *className = name + strlen(name);
                    if (className[1] == 0x7f)
                        className += 2;
                    const char *prefix = (const char *)0x001a0a54u;   // "RUNTIME_ALLOC::"
                    size_t prefixLength = strlen(prefix);
                    RuntimeAllocConstructor ctor = NULL;
                    if (strncmp(prefix, name, prefixLength) == 0)
                        ctor = (RuntimeAllocConstructor)RuntimePool->FindConstructor(className);
                    if (ctor == NULL) {
                        PrintMessage(0, (const char *)0x001cb228u, name);
                        S = sym->value;   // unresolved: the value as it stands
                        understood = true;
                        break;
                    }
                    // No object on the disc has a RUNTIME_ALLOC symbol, so the shipped game never gets here (this
                    // path is only shadow-tested on perturbed objects), nor into the property parsers it calls.
                    static bool warned;
                    if (!warned) {
                        warned = true;
                        printf("[eagl] WARNING: RUNTIME_ALLOC symbol %s - a path no shipped data reaches; its "
                               "constructors are untested\n", name);
                    }
                    void *dtor = RuntimePool->FindDestructor(className);
                    char destroy = 0;
                    void *context;
                    address = ctor(name + prefixLength, this, &context, &destroy);
                    if (destroy != 0 && address != NULL) {
                        RuntimeAllocRecord *r = (RuntimeAllocRecord *)EaglMalloc(0x10, (const char *)0x001cb8e8u);
                        if (r != NULL) {
                            r->destructor = (void (*)(void *, void *))dtor;
                            r->object = address;
                            r->context = context;
                            r->next = NULL;
                        }
                        r->next = (RuntimeAllocRecord *)runtimeAllocs;
                        runtimeAllocs = r;
                    }
                }
                sym->shndx = 1;
                sym->value = (uint32_t)(uintptr_t)address - SectionBase(t, 1);
            }
            if (!understood)
                PrintMessage(0, (const char *)0x001cb3e8u, t->strings + sym->name, sym->info & 0xf);
            uint32_t *P = (uint32_t *)(uintptr_t)(rel[0] + target);
            uint32_t A = *P;
            switch ((uint8_t)rel[1]) {
            case 0: case 1: case 3:
                break;
            case 2:   // R_MIPS_32
                *P = A + S;
                break;
            case 4: { // R_MIPS_26
                uint32_t v = S + (A & 0x3ffffff) * 4;
                uint32_t pc = (uint32_t)(uintptr_t)(P + 1);
                if (((pc ^ v) & 0xfc000000) != 0)
                    PrintMessage(0, (const char *)0x001cb438u, pc, v);
                *P = *P ^ (((v >> 2) ^ *P) & 0x3ffffff);
                break;
            }
            case 5:   // R_MIPS_HI16 (no carry from the low half), then as LO16
                S >>= 16;
                *(uint16_t *)P = (uint16_t)(*(uint16_t *)P + (uint16_t)S);
                break;
            case 6:   // R_MIPS_LO16
                *(uint16_t *)P = (uint16_t)(*(uint16_t *)P + (uint16_t)S);
                break;
            case 7: case 12:
                PrintMessage(0, (const char *)0x001cb4a0u);
                break;
            case 8:
                PrintMessage(0, (const char *)0x001cb4dcu);
                break;
            case 9:
                PrintMessage(0, (const char *)0x001cb514u);
                break;
            default:
                PrintMessage(0, (const char *)0x001cb548u, (uint32_t)(uint8_t)rel[1]);
                break;
            }
        }
    }
    RunConstructors();
}

// For every symbol of a class with a registered constructor: construct it, and keep its destructor.
// AUTOINJECT
void DynamicLoader::RunConstructors() {
    int symbols = table != NULL ? table->symbolCount : 0;
    int n = 0;
    LoaderSymbol s;
    for (int i = 0; i < symbols; i++) {
        GetSymbol(&s, i);
        if ((uint8_t)s.defined != 0 && CtorPool->FindConstructor(s.className) != NULL)
            n++;
    }
    if (n > 0) {
        destructors = (void **)EaglMalloc((uint32_t)(n * 8), (const char *)0x001cb204u);
        n = 0;
        for (int i = 0; i < symbols; i++) {
            GetSymbol(&s, i);
            if ((uint8_t)s.defined == 0)
                continue;
            PoolConstructor ctor = (PoolConstructor)CtorPool->FindConstructor(s.className);
            if (ctor == NULL)
                continue;
            void *dtor = CtorPool->FindDestructor(s.className);
            ctor(s.address, this);
            destructors[n * 2] = dtor;
            destructors[n * 2 + 1] = s.address;
            n++;
        }
    }
    constructorCount = n;
}

// The destructors in reverse, then the RUNTIME_ALLOC objects'.
// AUTOINJECT
void DynamicLoader::RunDestructors() {
    if (destructors != NULL) {
        for (int i = constructorCount - 1; i >= 0; i--)
            ((void (*)(void *))destructors[i * 2])(destructors[i * 2 + 1]);
        EaglFree(destructors, (uint32_t)constructorCount << 3);
        destructors = NULL;
    }
    RuntimeAllocRecord *r = (RuntimeAllocRecord *)runtimeAllocs;
    while (r != NULL) {
        RuntimeAllocRecord *next = r->next;
        r->destructor(r->object, r->context);
        EaglFree(r, 0x10);
        r = next;
    }
    runtimeAllocs = NULL;
}

// Out of the list of loaded objects, and the table freed.
// AUTOINJECT
void DynamicLoader::Release() {
    HashTable *t = table;
    if (t == NULL)
        return;
    if (t->previous == NULL)
        LoadedTables = t->next;
    else
        t->previous->next = t->next;
    if (t->next != NULL)
        t->next->previous = t->previous;
    if (t->chain != NULL)
        EaglFree(t->chain, (uint32_t)t->symbolCount << 2);
    EaglFree(t, 0x428);
    table = NULL;
}

// A symbol's address by name and class ("" for none).
// AUTOINJECT
bool DynamicLoader::GetAddr(const char *className, const char *name, void **out) {
    *out = NULL;
    HashTable *t = table;
    if (t == NULL)
        return false;
    ElfSymbol *symbols = t->symbols;
    for (uint32_t i = (uint32_t)t->buckets[ElfHash(name)]; i != 0xffffffff; i = (uint32_t)t->chain[i]) {
        if ((uint32_t)t->symbolCount < i) {
            PrintMessage(0, (const char *)0x001cb860u, i, t->symbolCount);
            continue;
        }
        ElfSymbol *s = &symbols[i];
        const char *symbolName = t->strings + s->name;
        const char *symbolClass = symbolName + strlen(name) + 1;
        symbolClass = *symbolClass == 0x7f ? symbolClass + 1 : symbolClass - 1;
        if (strcmp(name, symbolName) != 0 || strcmp(className, symbolClass) != 0)
            continue;
        if (s->other == 1) {
            *out = (void *)(uintptr_t)symbols[i].value;
            return true;
        }
        if (s->shndx != 0 && s->shndx < At<uint16_t>(t->header, 0x30)) {
            *out = (void *)(uintptr_t)(SectionBase(t, s->shndx) + symbols[i].value);
            return true;
        }
    }
    return false;
}

// Symbol number index (name, class, address, and whether it was defined here). Without a table only the name is
// written by the original, the rest being whatever its stack held; 0s here.
// AUTOINJECT
LoaderSymbol* DynamicLoader::GetSymbol(LoaderSymbol *out, int index) {
    HashTable *t = table;
    if (t == NULL || index < 0 || t->symbolCount <= index) {
        out->name = NULL;
        out->className = NULL;
        out->address = NULL;
        out->defined = 0;
        return out;
    }
    ElfSymbol *s = &t->symbols[index];
    const char *name = t->strings + s->name;
    const char *end = name + strlen(name);
    out->name = name;
    out->className = end[1] == 0x7f ? end + 2 : end;
    out->defined = s->other != 2;
    uint32_t address = 0;
    if (s->other == 1)
        address = s->value;
    else if (s->shndx != 0 && s->shndx < At<uint16_t>(t->header, 0x30))
        address = SectionBase(t, s->shndx) + s->value;
    out->address = (void *)(uintptr_t)address;
    return out;
}

// The next symbol, from *index, whose class is name.
// AUTOINJECT
bool DynamicLoader::GetNextSymbol(const char *name, int *index, LoaderSymbol *out) {
    for (;;) {
        int count = table != NULL ? table->symbolCount : 0;
        int i = *index;
        if (count <= i)
            return false;
        LoaderSymbol s;
        GetSymbol(&s, i);
        if (strcmp(name, s.className) == 0) {
            *out = s;
            *index = *index + 1;
            return true;
        }
        *index = i + 1;
    }
}

// AUTOINJECT
bool DynamicLoader::GetNextAddr(const char *name, int *index, void **out) {
    LoaderSymbol s;
    s.address = NULL;
    if (GetNextSymbol(name, index, &s)) {
        *out = s.address;
        return true;
    }
    return false;
}

// FUNC_AT(0x000e5ae0)
void* DynamicLoader::GetElfData() {
    return elf;
}

// AUTOINJECT
void DynamicLoader::RegisterVar(const char *name, void *value) {
    GlobalPool->AddSymbol(name, value);
}

// AUTOINJECT
void DynamicLoader::UnRegisterVar(const char *name) {
    GlobalPool->RemoveSymbol(name);
}

// AUTOINJECT
void* DynamicLoader::GetRegisteredVar(const char *name, bool *found) {
    return GlobalPool->Search(name, found);
}

// Every image of a SHPX file into the global pool as "shape_" + its name (its long name if that is at most four
// characters, else its four-character directory name with trailing spaces cut), unless the name is taken.
// AUTOINJECT
void DynamicLoader::RegisterShapes(uint8_t *shapes) {
    char name[11];
    memcpy(name, "shape_", 7);
    int count = At<int32_t>(shapes, 8);
    for (int i = 0; i < count; i++) {
        uint8_t *image = shapes + At<int32_t>(shapes, 0x14 + i * 8);
        const char *longName = SHAPE_longname(image);
        if (longName != NULL && strlen(longName) <= 4) {
            strncpy(name + 6, longName, 4);
        } else {
            SHAPE_name(shapes, i, (uint32_t *)(name + 6));
            for (char *p = name + 9; p >= name && *p == ' '; p--)
                *p = 0;
        }
        name[10] = 0;
        bool found;
        GlobalPool->Search(name, &found);
        if (!found)
            GlobalPool->AddSymbol(name, image);
    }
}

// Each image's name taken out again if it still points at this file's image. Always by its directory name, padded
// back out with spaces - so a name RegisterShapes took from the long name, or cut, is never found and stays.
// AUTOINJECT
void DynamicLoader::UnRegisterShapes(uint8_t *shapes) {
    char name[16];
    memcpy(name, "shape_", 7);
    for (int i = 0; i < At<int32_t>(shapes, 8); i++) {
        SHAPE_name(shapes, i, (uint32_t *)(name + 6));
        name[10] = 0;
        for (char *p = name + 9; *p == 0 || isspace((unsigned char)*p); p--)
            *p = ' ';
        int offset = At<int32_t>(shapes, 0x14 + i * 8);
        bool found;
        void *value = GlobalPool->Search(name, &found);
        if (found && value == shapes + offset)
            GlobalPool->RemoveSymbol(name);
    }
}
