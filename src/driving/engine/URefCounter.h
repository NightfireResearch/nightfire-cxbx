#ifndef DRIVING_ENGINE_UREFCOUNTER_H_
#define DRIVING_ENGINE_UREFCOUNTER_H_

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// URefCounter<T>: the game's registry of shared, named objects - models, textures, CARP files, the audio
// framework's mixes, streams, faders, banks and engines. A std::map (the compiler's own, Dinkumware's red-black
// tree) from a name of up to 127 characters, compared without case, to the object and a count of the references
// to it. Each instance is a function-local static, made by the instantiation's Get.
//
// The game has a compiled copy of each method per instantiation, all the same code but for the tree's own
// compiled helpers (insert, erase), which differ by address. The template below is that code, written once; each
// instantiation's class gives every compiled copy its own entry, which calls it with the instantiation's helpers.
// See URefCounter.cpp.
// ---------------------------------------------------------------------------------------------------------------

// A tree node (0x98 bytes, allocated by the tree from the pools as "STL"). The head node is the tree's end: its
// parent is the root, its left the first node, its right the last, and it is the one node marked isNil.
struct RefCounterNode {
    RefCounterNode *left;       // +0x00
    RefCounterNode *parent;     // +0x04
    RefCounterNode *right;      // +0x08
    char name[0x80];            // +0x0c the key
    int32_t references;         // +0x8c
    void *object;               // +0x90
    uint8_t color;              // +0x94 1: black
    uint8_t isNil;              // +0x95
    uint8_t unknown96[2];
};
static_assert(sizeof(RefCounterNode) == 0x98, "a reference counter's tree node is 0x98 bytes");

// The map's mapped value, and the pair the tree's insert copies into a new node
struct RefCounterEntry {
    int32_t references;
    void *object;
};

struct RefCounterValue {
    char name[0x80];
    RefCounterEntry entry;
};
static_assert(sizeof(RefCounterValue) == 0x88, "a reference counter's value is 0x88 bytes");

// What the tree's insert_unique answers (through a pointer the caller supplies)
struct RefCounterInsertResult {
    RefCounterNode *node;
    bool inserted;
};

// Each instantiation's compiled tree helpers (__thiscall, called through __fastcall with a dummy EDX):
// insert_unique(value), erase(where), _Erase(subtree) and erase(first, last).
class URefCounterMap;
typedef RefCounterInsertResult *(__fastcall *RefCounterInsertFn)(URefCounterMap *map, int, RefCounterInsertResult *result,
                                                                 const RefCounterValue *value);
typedef RefCounterNode **(__fastcall *RefCounterEraseFn)(URefCounterMap *map, int, RefCounterNode **result,
                                                         RefCounterNode *where);
typedef void (__fastcall *RefCounterEraseTreeFn)(URefCounterMap *map, int, RefCounterNode *subtree);
typedef RefCounterNode **(__fastcall *RefCounterEraseRangeFn)(URefCounterMap *map, int, RefCounterNode **result,
                                                              RefCounterNode *first, RefCounterNode *last);
typedef void (*RefCounterDestroyFn)(void);   // a static's destructor, as atexit takes it

// The map, as every instantiation lays it out (0xc bytes), with the code that does not depend on T.
class URefCounterMap {
public:
    uint8_t compare;            // +0x00 the comparator (an empty object)
    RefCounterNode *head;       // +0x04
    uint32_t count;             // +0x08

    // The node of `name` (copied to a 128-byte key first, as the game does), or the head
    RefCounterNode* Find(const char *name);

    // The object registered as `name`, or NULL (0x00125270: one compiled copy serves every instantiation).
    void* GetReference(const char *name);

protected:
    void AddReference(const char *name, void *object, RefCounterInsertFn insert);
    bool RemoveReference(void *object, RefCounterEraseFn erase);
    void Destruct(RefCounterEraseTreeFn eraseTree, RefCounterEraseRangeFn eraseRange);
    // Get's body: constructs the static the first time (its guard's bit 0) and registers its destructor.
    void ConstructOnce(uint32_t *guard, RefCounterDestroyFn destroy);
};
static_assert(sizeof(URefCounterMap) == 0xc, "a reference counter is 0xc bytes");

template <class T>
class URefCounter : public URefCounterMap {
public:
    T* GetReference(const char *name) { return static_cast<T *>(URefCounterMap::GetReference(name)); }

protected:
    // Registers `object` as `name` if the name is new; either way one more reference to the name.
    void AddReference(const char *name, T *object, RefCounterInsertFn insert) {
        URefCounterMap::AddReference(name, object, insert);
    }
    // One reference fewer to every name `object` is registered under, until one reaches none: that one is erased,
    // and the answer is true.
    bool RemoveReference(T *object, RefCounterEraseFn erase) { return URefCounterMap::RemoveReference(object, erase); }
};

// The objects the instantiations count (layouts not needed here)
struct ActModelInfo;        // ActModelDatabase::ModelInfo
struct ActTextureInfo;      // ActTextureDatabase::TextureInfo
struct ActWeaponInfo;       // ActWeaponDatabase::WeaponInfo
struct RCARPFile;
class RTextureContext;
class AMix;
class AStream;
class AFader;
class ABank;
class AEngine;

// ---- the instantiations, one entry per compiled copy

class ModelInfoRefCounter : public URefCounter<ActModelInfo> {
public:
    void AddReference(const char *name, ActModelInfo *info);    // 0x00018030
    void Destruct();                                            // 0x000182b0
    static ModelInfoRefCounter* Get();                          // 0x00018340, the static at 0x001dd9e8
};

class TextureInfoRefCounter : public URefCounter<ActTextureInfo> {
public:
    void AddReference(const char *name, ActTextureInfo *info);  // 0x0001a5e0
    void Destruct();                                            // 0x0001a880
    static TextureInfoRefCounter* Get();                        // 0x0001a910, at 0x001dda04
};

class WeaponInfoRefCounter : public URefCounter<ActWeaponInfo> {
public:
    void AddReference(const char *name, ActWeaponInfo *info);   // 0x0001bbd0
    static WeaponInfoRefCounter* Get();                         // 0x0001bef0, at 0x001dda14
};

class CarpFileRefCounter : public URefCounter<RCARPFile> {
public:
    bool RemoveReference(RCARPFile *file);                      // 0x00090130
    void AddReference(const char *name, RCARPFile *file);       // 0x00090280
    static CarpFileRefCounter* Get();                           // 0x00090430, at 0x001f25f4
};

class TextureContextRefCounter : public URefCounter<RTextureContext> {
public:
    bool RemoveReference(RTextureContext *context);             // 0x00094d80
    void AddReference(const char *name, RTextureContext *context);   // 0x00094ed0
    static TextureContextRefCounter* Get();                     // 0x00095150, at 0x001f2a34
};

class MixRefCounter : public URefCounter<AMix> {
public:
    bool RemoveReference(AMix *mix);                            // 0x0011d310
    void AddReference(const char *name, AMix *mix);             // 0x0011d460
    static MixRefCounter* Get();                                // 0x0011d610, at 0x00243944
};

class StreamRefCounter : public URefCounter<AStream> {
public:
    bool RemoveReference(AStream *stream);                      // 0x00123230
    void AddReference(const char *name, AStream *stream);       // 0x001233f0
    static StreamRefCounter* Get();                             // 0x00123630, at 0x00243a88
};

class FaderRefCounter : public URefCounter<AFader> {
public:
    bool RemoveReference(AFader *fader);                        // 0x00125920
    void AddReference(const char *name, AFader *fader);         // 0x00125a70
    static FaderRefCounter* Get();                              // 0x00125c20, at 0x00243af8
};

class BankRefCounter : public URefCounter<ABank> {
public:
    void EraseSubtree(RefCounterNode *node);                    // 0x00125f50, the tree's _Erase
    bool RemoveReference(ABank *bank);                          // 0x00126580
    void AddReference(const char *name, ABank *bank);           // 0x001266d0
    void Destruct();                                            // 0x001267f0
    static BankRefCounter* Get();                               // 0x00126880, at 0x00243b08
};

class EngineRefCounter : public URefCounter<AEngine> {
public:
    bool RemoveReference(AEngine *engine);                      // 0x0012f5b0
    void AddReference(const char *name, AEngine *engine);       // 0x0012f700
    static EngineRefCounter* Get();                             // 0x0012f8b0, at 0x00243b74
};

#endif // DRIVING_ENGINE_UREFCOUNTER_H_
