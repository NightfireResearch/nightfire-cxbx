#include "Loader.h"

#include "EaglGlobals.h"
#include "Profiler.h"
#include "Realgraph.h"
#include "../../helpers.h"

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
// through EAGL's allocator hooks under the original's allocation names, and diagnostics through PrintMessage.
// devtools/LoaderShadow.cpp loads every object on the disc with both and compares the results.
// ---------------------------------------------------------------------------------------------------------------

// (The allocator hooks, the pools and EAGL::ViewPort's matrices, registered by name, are EaglGlobals.h's.)
#define DefaultPoolSize U32_AT(0x001cdc50)                      // a new pool's table size (256)
#define LoadedTables (*(HashTable **)0x0023fb88)

using EAGL::PrintMessage;

// ---- SymbolPool

// FUNC_AT(0x000f3c70)
SymbolPool* SymbolPool::Construct() {
    count = 0;
    size = DefaultPoolSize;
    table = NULL;
    resolvers = NULL;
    return this;
}

static uint32_t PoolHash(const char *name) {
    uint32_t h = 0;
    for (; *name != 0; name++)
        h ^= (h << 5) ^ (signed char)*name;   // the character sign-extended
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
        size = DefaultPoolSize;
        table = (SymbolEntry **)EaglMalloc(DefaultPoolSize << 2, "EAGL::SymbolPool::mpSymbolTable");
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
        table = (SymbolEntry **)EaglMalloc(oldSize << 3, "EAGL::SymbolPool::mpSymbolTable");
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
    SymbolEntry *entry = (SymbolEntry *)EaglMalloc(n + 5, "EAGL::SymbolEntry");
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
            EaglFree(e, strlen(e->name) + 5);
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
                EaglFree(e, strlen(e->name) + 5);
            table[i] = NULL;
        }
    }
    count = 0;
    while (resolvers != NULL) {
        SymbolResolver *r = resolvers;
        resolvers = r->next;
        EaglFree(r, sizeof(SymbolResolver));
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

// The matrices EAGL registers by name, for render methods to address.
// FUNC_AT(0x000f39c0)
void EAGL_SymbolInit() {
    DynamicLoader::RegisterVar("EAGL::ViewPort::gpProjectionMatrix", ProjectionMatrix);
    DynamicLoader::RegisterVar("EAGL::ViewPort::gpViewMatrix", ViewMatrix);
    DynamicLoader::RegisterVar("EAGL::ViewPort::gpModelMatrix", ModelMatrix);
    DynamicLoader::RegisterVar("EAGL::ViewPort::gpModelViewMatrix", ModelViewMatrix);
    DynamicLoader::RegisterVar("EAGL::ViewPort::gpViewProjectionMatrix", ViewProjectionMatrix);
    DynamicLoader::RegisterVar("EAGL::ViewPort::gpModelViewProjectionMatrix", ModelViewProjectionMatrix);
}

// ---- a loaded object's symbol table

namespace {

struct ElfHeader {          // Elf32_Ehdr
    uint8_t ident[16];      // +0x00
    uint16_t type;          // +0x10
    uint16_t machine;       // +0x12
    uint32_t version;       // +0x14
    uint32_t entry;         // +0x18
    uint32_t phoff;         // +0x1c
    uint32_t shoff;         // +0x20 the section table's file offset
    uint32_t flags;         // +0x24
    uint16_t ehsize;        // +0x28
    uint16_t phentsize;     // +0x2a
    uint16_t phnum;         // +0x2c
    uint16_t shentsize;     // +0x2e
    uint16_t shnum;         // +0x30 sections
    uint16_t shstrndx;      // +0x32 the section holding the section names
};
static_assert(sizeof(ElfHeader) == 0x34, "an ELF header is 0x34 bytes");

enum ElfSectionType : uint32_t {
    kShtNull = 0, kShtProgBits = 1, kShtSymTab = 2, kShtStrTab = 3, kShtRela = 4, kShtHash = 5, kShtDynamic = 6,
    kShtNoBits = 8, kShtRel = 9, kShtShLib = 10, kShtDynSym = 11,
    kShtMipsDebug = 0x70000005, kShtMipsRegInfo = 0x70000006, kShtDvpOverlayTable = 0x7ffff420,
    kShtDvpOverlay = 0x7ffff421,
};

enum MipsRelocationType : uint8_t {
    kRMipsNone = 0, kRMips16 = 1, kRMips32 = 2, kRMipsRel32 = 3, kRMips26 = 4, kRMipsHi16 = 5, kRMipsLo16 = 6,
    kRMipsGpRel16 = 7, kRMipsLiteral = 8, kRMipsGot16 = 9, kRMipsGpRel32 = 12,
};

// The loader's marks in a symbol's st_other
enum : uint8_t { kSymbolAbsolute = 1, kSymbolExternal = 2 };

const char kClassMarker = 0x7f;   // "Name\0\x7fClass"

struct ElfSymbol {          // Elf32_Sym
    uint32_t name;
    uint32_t value;
    uint32_t size;
    uint8_t info;           // low four bits the type: below 4 (no type, object, function, section) understood
    uint8_t other;          // kSymbolAbsolute: value is absolute; kSymbolExternal: resolved from outside
    uint16_t shndx;
};

struct ElfSection {         // Elf32_Shdr; offset, link and info become addresses as the loader goes
    uint32_t name, type, flags, addr;
    uint32_t offset;
    uint32_t size;
    uint32_t link, info, align, entsize;
};

struct ElfRel {             // Elf32_Rel
    uint32_t offset;        // in the section relocated
    uint32_t info;          // the symbol in bits 8..31, the MipsRelocationType in the low byte
};

}  // namespace

typedef void *(*LoaderResolver)(const char *name, char *found);

struct HashTable {          // 0x428, "EAGL::HashPointer new"
    HashTable *next;        // +0x000 the list of loaded objects
    HashTable *previous;    // +0x004
    const char *strings;    // +0x008 .strtab
    uint8_t resolved;       // +0x00c
    uint8_t pad[3];
    int symbolCount;        // +0x010
    ElfSymbol *symbols;     // +0x014 .symtab
    ElfSection *sections;   // +0x018
    ElfHeader *header;      // +0x01c
    int32_t buckets[256];   // +0x020
    int32_t *chain;         // +0x420
    LoaderResolver resolver;   // +0x424
};
static_assert(sizeof(HashTable) == 0x428, "a HashTable is 0x428 bytes");

// The ELF hash of a symbol name, to 8 bits (0x000e51d0: name in EDX).
static uint32_t ElfHash(const char *name) {
    uint32_t h = 0;
    for (; *name != 0; name++) {
        h = h * 0x10 + (signed char)*name;   // the character sign-extended
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

// A symbol defined in one of the object's own sections
static inline bool InSection(const HashTable *t, const ElfSymbol *s) {
    return s->shndx != 0 && s->shndx < t->header->shnum;
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
        return (uint32_t)l->elf + offset;
    if (l->second != NULL)
        return (uint32_t)l->second + (offset - l->elfSize);
    if (offset <= l->elfSize)
        return (uint32_t)l->elf + offset;
    return 0;
}

// Sections parsed (their offsets, links and infos made addresses in place), names rewritten, symbols hashed.
// AUTOINJECT
void DynamicLoader::Initialize(void *resolver) {
    HashTable t = {};
    ElfHeader *header = (ElfHeader *)elf;
    t.header = header;
    ElfSection *sections = (ElfSection *)FileAddress(this, header->shoff);
    t.sections = sections;
    int sectionCount = header->shnum;
    for (int i = 0; i < sectionCount; i++)
        sections[i].offset = FileAddress(this, sections[i].offset);
    const char *sectionNames = (const char *)sections[header->shstrndx].offset;
    for (int i = 0; i < sectionCount; i++) {
        ElfSection *s = &sections[i];
        uint32_t type = s->type;
        const char *name = sectionNames + s->name;
        switch (type) {
        case kShtNull: case kShtProgBits:
        case kShtMipsDebug: case kShtMipsRegInfo: case kShtDvpOverlayTable: case kShtDvpOverlay:
            break;
        case kShtSymTab:
            s->link = SectionBase(&t, s->link);
            if (strcmp(".symtab", name) == 0) {
                t.symbols = (ElfSymbol *)s->offset;
                t.symbolCount = (int32_t)s->size / 16;
            }
            break;
        case kShtStrTab:   // "__Class:::Name" -> "Name\0\x7fClass"
            if (strcmp(".strtab", name) == 0) {
                char *p = (char *)s->offset;
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
                            p[n] = kClassMarker;
                            strcpy(p + n + 1, className);
                        }
                    }
                    left -= (int)length + 1;
                    p += length + 1;
                }
            }
            break;
        case kShtRela:
            if (s->size != 0)
                PrintMessage(0, "dlopen: non empty SHT_RELA section seen. Not supported.\n");
            break;
        case kShtHash:
            PrintMessage(0, "dlopen: SHT_HASH section seen. Not supported.\n");
            break;
        case kShtDynamic:   // the original's message says SHT_HASH here too
            PrintMessage(0, "dlopen: SHT_HASH section seen. Not supported.\n");
            break;
        case kShtNoBits:
            if (s->size != 0)
                PrintMessage(0, "dlopen: SHT_NOBITS section with data seen. Not supported.\n");
            break;
        case kShtRel:   // the section relocated, and the symbol table
            s->info = SectionBase(&t, s->info);
            s->link = SectionBase(&t, s->link);
            break;
        case kShtShLib:
            PrintMessage(0, "dlopen: SHT_SHLIB section seen. Not supported.\n");
            break;
        case kShtDynSym:
            PrintMessage(0, "dlopen: SHT_DYNSYM section seen. Not supported.\n");
            break;
        default:
            PrintMessage(0, "Unknown section type %d (section %d). Aborting dynamic loading.\n", type, i);
            break;
        }
    }
    if (sectionCount == 0 || t.symbols == NULL)
        PrintMessage(0, "dlopen: Failed to find a symbol table. Aborting\n");
    t.resolver = (LoaderResolver)resolver;
    for (int i = 0; i < 256; i++)
        t.buckets[i] = -1;
    t.chain = (int32_t *)EaglMalloc(t.symbolCount * 4, "EAGL::dynamic symbols");
    HashTable *table = (HashTable *)EaglMalloc(sizeof(HashTable), "EAGL::HashPointer new");
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
    *table = t;
    table->next = LoadedTables;
    if (LoadedTables != NULL)
        LoadedTables->previous = table;
    table->previous = NULL;
    this->table = table;
    LoadedTables = table;
}

// A symbol in another loaded object, by name, if it is defined there.
// FUNC_AT(0x000e5af0)
void* EAGL_LookupInObject(HashTable *t, const char *name) {
    ElfSymbol *symbols = t->symbols;
    for (uint32_t j = t->buckets[ElfHash(name)]; j != 0xffffffff; j = t->chain[j]) {
        if ((uint32_t)t->symbolCount < j)
            PrintMessage(0, "dlsym: Internal error. Bad j value %d (chain size %d)!\n", j, t->symbolCount);
        ElfSymbol *s = &symbols[j];
        if (strcmp(name, t->strings + s->name) == 0 && InSection(t, s))
            return (void *)(SectionBase(t, s->shndx) + s->value);
    }
    return NULL;
}

typedef void *(*RuntimeAllocConstructor)(const char *properties, DynamicLoader *loader, void **context,
                                         char *destroy);
typedef void (*RuntimeAllocDestructor)(void *object, void *context);

// Every relocation applied, then the constructors run. Once.
// AUTOINJECT
void DynamicLoader::Resolve() {
    HashTable *t = table;
    if (t == NULL || t->resolved != 0)
        return;
    ElfSection *sections = t->sections;
    const ElfHeader *header = t->header;
    t->resolved = 1;
    for (int si = 0; si < header->shnum; si++) {
        ElfSection *sec = &sections[si];
        if (sec->type != kShtRel)
            continue;
        uint32_t target = sec->info;
        ElfRel *rel = (ElfRel *)sec->offset;
        ElfSymbol *symbols = (ElfSymbol *)sec->link;
        for (int count = (int32_t)sec->size / 8; count != 0; count--, rel++) {
            ElfSymbol *sym = &symbols[rel->info >> 8];
            uint32_t S = 0;
            bool understood = false;
            while ((sym->info & 0xf) < 4) {
                if (sym->shndx != 0 && sym->shndx < header->shnum) {
                    S = SectionBase(t, sym->shndx) + sym->value;
                    understood = true;
                    break;
                }
                sym->other = kSymbolExternal;
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
                        address = GlobalPool.Search(name, &inPool);
                        resolved = inPool;
                    }
                }
                if (!resolved) {
                    const char *className = name + strlen(name);
                    if (className[1] == kClassMarker)
                        className += 2;
                    const char *prefix = "RUNTIME_ALLOC::";
                    size_t prefixLength = strlen(prefix);
                    RuntimeAllocConstructor ctor = NULL;
                    if (strncmp(prefix, name, prefixLength) == 0)
                        ctor = (RuntimeAllocConstructor)TheRuntimeAllocPool.FindConstructor(className);
                    if (ctor == NULL) {
                        PrintMessage(0,
                                     "ERROR: DynamicLoader::Resolve - Failed to resolve undefined relocation symbol "
                                     "%s.\n"
                                     "Possible causes:\n"
                                     "    - You forgot to add the symbol to the global symbol pool before loading "
                                     "this ELF.\n"
                                     "    - You forgot to provide an address if using an address callback "
                                     "function.\n"
                                     "    - You forgot to load another ELF that contains the symbol before loading "
                                     "this ELF.\n"
                                     "      ie. forgot to load eaglrm.o before a model ELF.\n"
                                     "    - There is a problem with the ELF file\n",
                                     name);
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
                    void *dtor = TheRuntimeAllocPool.FindDestructor(className);
                    char destroy = 0;
                    void *context;
                    address = ctor(name + prefixLength, this, &context, &destroy);
                    if (destroy != 0 && address != NULL) {
                        RuntimeAllocRecord *r = (RuntimeAllocRecord *)EaglMalloc(
                            sizeof(RuntimeAllocRecord), "EAGL::DynamicLoader::RuntimeAllocDestructorEntry new");
                        if (r != NULL) {
                            r->destructor = (RuntimeAllocDestructor)dtor;
                            r->object = address;
                            r->context = context;
                            r->next = NULL;
                        }
                        r->next = runtimeAllocs;
                        runtimeAllocs = r;
                    }
                }
                sym->shndx = 1;
                sym->value = (uint32_t)address - SectionBase(t, 1);
            }
            if (!understood)
                PrintMessage(0, "dlopen: Relocation to a symbol type I don't understand! (symbol %s type %d)\n",
                             t->strings + sym->name, sym->info & 0xf);
            uint32_t *P = (uint32_t *)(rel->offset + target);
            uint32_t A = *P;
            uint8_t type = (uint8_t)rel->info;
            switch (type) {
            case kRMipsNone: case kRMips16: case kRMipsRel32:
                break;
            case kRMips32:
                *P = A + S;
                break;
            case kRMips26: {
                uint32_t v = S + (A & 0x3ffffff) * 4;
                uint32_t pc = (uint32_t)(P + 1);
                if (((pc ^ v) & 0xfc000000) != 0)
                    PrintMessage(0, "dlopen: Result of patching jmp instruction is outside of range of possible "
                                    "jump. (%x vs. %x) Aborting.\n", pc, v);
                *P = *P ^ (((v >> 2) ^ *P) & 0x3ffffff);
                break;
            }
            case kRMipsHi16:   // no carry from the low half; then as LO16
                S >>= 16;
                *(uint16_t *)P += (uint16_t)S;
                break;
            case kRMipsLo16:
                *(uint16_t *)P += (uint16_t)S;
                break;
            case kRMipsGpRel16: case kRMipsGpRel32:
                PrintMessage(0, "dlopen: Cannot deal with GP relative relocations. Aborting\n");
                break;
            case kRMipsLiteral:
                PrintMessage(0, "dlopen: Cannot deal with MIPS_LITERAL relocation yet\n");
                break;
            case kRMipsGot16:
                PrintMessage(0, "dlopen: Cannot deal with MIPS_GOT16 relocation yet\n");
                break;
            default:
                PrintMessage(0, "dlopen: Cannot deal with relocation type %d yet\n", type);
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
        if ((uint8_t)s.defined != 0 && TheConstructorPool.FindConstructor(s.className) != NULL)
            n++;
    }
    if (n > 0) {
        destructors = (LoaderDestructor *)EaglMalloc(n * sizeof(LoaderDestructor), "EAGL::dynamic destructor list");
        n = 0;
        for (int i = 0; i < symbols; i++) {
            GetSymbol(&s, i);
            if ((uint8_t)s.defined == 0)
                continue;
            PoolConstructor ctor = (PoolConstructor)TheConstructorPool.FindConstructor(s.className);
            if (ctor == NULL)
                continue;
            void *dtor = TheConstructorPool.FindDestructor(s.className);
            ctor(s.address, this);
            destructors[n].destructor = (void (*)(void *))dtor;
            destructors[n].object = s.address;
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
            destructors[i].destructor(destructors[i].object);
        EaglFree(destructors, (uint32_t)constructorCount << 3);
        destructors = NULL;
    }
    RuntimeAllocRecord *r = runtimeAllocs;
    while (r != NULL) {
        RuntimeAllocRecord *next = r->next;
        r->destructor(r->object, r->context);
        EaglFree(r, sizeof(RuntimeAllocRecord));
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
    EaglFree(t, sizeof(HashTable));
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
    for (uint32_t i = t->buckets[ElfHash(name)]; i != 0xffffffff; i = t->chain[i]) {
        if ((uint32_t)t->symbolCount < i) {
            PrintMessage(0, "DynamicLoader::GetAddr() -- INTERNAL ERROR: Bad i value %d (chain size %d)!\n", i,
                         t->symbolCount);
            continue;
        }
        ElfSymbol *s = &symbols[i];
        const char *symbolName = t->strings + s->name;
        const char *symbolClass = symbolName + strlen(name) + 1;
        symbolClass = *symbolClass == kClassMarker ? symbolClass + 1 : symbolClass - 1;
        if (strcmp(name, symbolName) != 0 || strcmp(className, symbolClass) != 0)
            continue;
        if (s->other == kSymbolAbsolute) {
            *out = (void *)s->value;
            return true;
        }
        if (InSection(t, s)) {
            *out = (void *)(SectionBase(t, s->shndx) + s->value);
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
    out->className = end[1] == kClassMarker ? end + 2 : end;
    out->defined = s->other != kSymbolExternal;
    uint32_t address = 0;
    if (s->other == kSymbolAbsolute)
        address = s->value;
    else if (InSection(t, s))
        address = SectionBase(t, s->shndx) + s->value;
    out->address = (void *)address;
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
    GlobalPool.AddSymbol(name, value);
}

// AUTOINJECT
void DynamicLoader::UnRegisterVar(const char *name) {
    GlobalPool.RemoveSymbol(name);
}

// AUTOINJECT
void* DynamicLoader::GetRegisteredVar(const char *name, bool *found) {
    return GlobalPool.Search(name, found);
}

// Every image of a SHPX file into the global pool as "shape_" + its name (its long name if that is at most four
// characters, else its four-character directory name with trailing spaces cut), unless the name is taken.
// AUTOINJECT
void DynamicLoader::RegisterShapes(uint8_t *shapes) {
    const ShapeFile *file = (const ShapeFile *)shapes;
    char name[11];
    memcpy(name, "shape_", 7);
    int count = file->count;
    for (int i = 0; i < count; i++) {
        uint8_t *image = shapes + file->entries[i].offset;
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
        GlobalPool.Search(name, &found);
        if (!found)
            GlobalPool.AddSymbol(name, image);
    }
}

// Each image's name taken out again if it still points at this file's image. Always by its directory name, padded
// back out with spaces - so a name RegisterShapes took from the long name, or cut, is never found and stays.
// AUTOINJECT
void DynamicLoader::UnRegisterShapes(uint8_t *shapes) {
    const ShapeFile *file = (const ShapeFile *)shapes;
    char name[16];
    memcpy(name, "shape_", 7);
    for (int i = 0; i < file->count; i++) {
        SHAPE_name(shapes, i, (uint32_t *)(name + 6));
        name[10] = 0;
        for (char *p = name + 9; *p == 0 || isspace((unsigned char)*p); p--)
            *p = ' ';
        int offset = file->entries[i].offset;
        bool found;
        void *value = GlobalPool.Search(name, &found);
        if (found && value == shapes + offset)
            GlobalPool.RemoveSymbol(name);
    }
}
