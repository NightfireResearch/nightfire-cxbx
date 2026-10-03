#ifndef DRIVING_EAGL_LOADER_H_
#define DRIVING_EAGL_LOADER_H_

// EAGL's dynamic loader: the ELF objects every model, skeleton, animation bank and render-method library is loaded
// as (relocated, symbols hashed, constructors run by type), and the name pools around it. See Loader.cpp and
// docs/driving/eagl.md 2.10, 3.1, 4.1. Ghidra's names (EAGL::DynamicLoader, EAGL::SymbolPool, ...), the
// namespace left off as elsewhere; constructors and destructors as Construct/Destruct.

#include <stdint.h>

struct SymbolEntry {        // EAGL::SymbolEntry, FILE_malloc'd strlen(name) + 5
    void *value;
    char name[1];
};

struct SymbolResolver {     // a fallback lookup chained at SymbolPool +0x10 (0xc bytes)
    void *(*lookup)(const char *name, bool *found);
    void *unused;
    SymbolResolver *next;
};

class SymbolPool {          // 0x14: an open-addressed hash table of names
public:
    uint32_t count;
    uint32_t size;
    SymbolEntry **table;
    uint32_t unknown0c;
    SymbolResolver *resolvers;

    SymbolPool *Construct();
    void Destruct();
    uint32_t HashFunction(const char *name);
    void Insert(const char *name, SymbolEntry *entry);
    void *AddSymbol(const char *name, void *value);
    void RemoveSymbol(const char *name);
    void *Search(const char *name, bool *found);
    void Empty();
};
static_assert(sizeof(SymbolPool) == 0x14, "SymbolPool is 0x14 bytes");

typedef void *(*PoolConstructor)(void *data, void *loader);

class ConstructorPool {     // 0x28: constructors and destructors by type name
public:
    SymbolPool constructors;
    SymbolPool destructors;

    ConstructorPool *Construct();                                // 0x000e6390, the static initialiser's
    void Destruct();
    void AddType(const char *name, void *constructor, void *destructor);
    void RemoveType(const char *name);
    void *FindConstructor(const char *name);
    void *FindDestructor(const char *name);
};

class RuntimeAllocConstructorPool {   // the same, for RUNTIME_ALLOC:: symbols
public:
    SymbolPool constructors;
    SymbolPool destructors;

    void Destruct();
    void AddType(const char *name, void *constructor, void *destructor);
    void RemoveType(const char *name);
    void *FindConstructor(const char *name);
    void *FindDestructor(const char *name);
    void EmptyBoth();     // 0x000f3bb0 (invented; nothing calls it)
    void EmptyBoth2();    // 0x000f3bd0, the same again
};

struct HashTable;           // a loaded object's symbol table (0x428), Loader.cpp

struct LoaderSymbol {       // what GetSymbol answers
    const char *name;
    const char *className;  // after the name's 0x7f marker, or the name's own terminator
    void *address;
    uint32_t defined;       // low byte: the symbol was not resolved from outside
};

struct LoaderDestructor {   // a constructed symbol's destructor, called when the object is unloaded
    void (*destructor)(void *object);
    void *object;
};

struct RuntimeAllocRecord {   // "EAGL::DynamicLoader::RuntimeAllocDestructorEntry new" (0x10)
    void (*destructor)(void *object, void *context);
    void *object;
    void *context;
    RuntimeAllocRecord *next;
};

class DynamicLoader {       // 0x1c
public:
    HashTable *table;       // +0x00
    int constructorCount;   // +0x04
    LoaderDestructor *destructors;      // +0x08
    RuntimeAllocRecord *runtimeAllocs;  // +0x0c RUNTIME_ALLOC destructor records, a list
    uint8_t *elf;           // +0x10
    uint32_t elfSize;       // +0x14
    uint8_t *second;        // +0x18 the .rel half of a split object

    DynamicLoader *Construct(void *elf, uint32_t size, void *resolver);
    DynamicLoader *ConstructSplit(void *elf, uint32_t size, void *second, char deferResolve, void *resolver);
    void Destruct();
    void Initialize(void *resolver);
    void Resolve();
    void RunConstructors();
    void RunDestructors();
    void Release();
    bool GetAddr(const char *className, const char *name, void **out);
    LoaderSymbol* GetSymbol(LoaderSymbol *out, int index);      // returned by value: out, answered
    bool GetNextSymbol(const char *name, int *index, LoaderSymbol *out);
    bool GetNextAddr(const char *name, int *index, void **out);
    void *GetElfData();                                          // 0x000e5ae0
    static void RegisterVar(const char *name, void *value);
    static void UnRegisterVar(const char *name);
    static void *GetRegisteredVar(const char *name, bool *found);
    static void RegisterShapes(uint8_t *shapes);
    static void UnRegisterShapes(uint8_t *shapes);
};
static_assert(sizeof(DynamicLoader) == 0x1c, "DynamicLoader is 0x1c bytes");

void *EAGL_LookupInObject(HashTable *table, const char *name);   // 0x000e5af0 (invented)
void EAGL_SymbolInit();

#endif // DRIVING_EAGL_LOADER_H_
