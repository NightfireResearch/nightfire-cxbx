#ifndef DRIVING_ANIM_MODEL_H_
#define DRIVING_ANIM_MODEL_H_

// ActModel, ActModelDatabase and ActTextureDatabase: the actors' models (EAGL models loaded from a character's
// object files, up to three, with the TARs their skins are drawn with) and the textures swapped onto them, each
// shared by name through a URefCounter; and the code of those two URefCounters' trees. See Model.cpp.

#include <stddef.h>
#include <stdint.h>

#include "Character.h"                 // ActCharacterInfo
#include "../eagl/Realgraph.h"          // ShapeFile
#include "../data/StdStreams.h"         // GameStd::LogicError
#include "../engine/URefCounter.h"      // RefCounterTree

class DynamicLoader;
namespace EAGL {
struct Model;
struct TAR;
}  // namespace EAGL

// The TARs of a model's skin, by their symbols' names.
enum ActModelTar {
    kTarB = 0,                        // "tar_bbbb"
    kTarH = 1,                        // "tar_hhhh"
    kTarT = 2,                        // "tar_tttt"
    kTarCount
};

class ActModel {                      // 0x4c ("ActModel")
public:
    enum { kMaxModels = 3 };

    EAGL::Model *models[kMaxModels];  // +0x00 the "Model" symbols name1..name3 (types C and A: name1 in all three)
    void *files[kMaxModels];          // +0x0c the object files
    DynamicLoader *loaders[kMaxModels];   // +0x18
    EAGL::TAR *tars[kMaxModels][kTarCount];   // +0x24
    int32_t count;                    // +0x48 objects loaded: 1 or 3

    ActModel* Construct(ActCharacterInfo *info);                                // 0x000177b0
    void Load(ActCharacterInfo *info);                                          // 0x00017590
    void UnLoad();                                                              // 0x000177f0
    // Model `model`'s TARs from its object's "EAGL::TAR" symbols.
    void FindTars(int model, DynamicLoader *loader);                            // 0x00017480
    // Model `model`'s TAR `tar` draws the image of `texture` (nothing when texture is NULL).
    void SetTexture(int model, int tar, EAGL::TAR *texture);                    // 0x00017450
};
static_assert(sizeof(ActModel) == 0x4c, "an ActModel is 0x4c bytes");

// ActModelDatabase::ModelInfo (operator new)
struct ActModelInfo {                 // 0x18
    ActModel *model;                  // +0x00
    char name[0x14];                  // +0x04 the character's name
};
static_assert(sizeof(ActModelInfo) == 0x18, "a ModelInfo is 0x18 bytes");

class ActModelDatabase {              // 0xb0 ("ActModelDatabase")
public:
    enum { kMaxModels = 40 };

    ModelInfoRefCounter *refs;        // +0x00
    ActModelInfo *models[kMaxModels]; // +0x04 in the order loaded
    int32_t count;                    // +0xa4
    uint8_t unknownA8[8];

    ActModelDatabase* Construct();                                              // 0x000183d0
    void Destruct();                                                            // 0x00018110
    // Loads the character's model, unless its name is loaded already.
    void LoadModel(ActCharacterInfo *info);                                     // 0x000181a0
    ActModel* UseModel(ActCharacterInfo *info);                                 // 0x00017840
};
static_assert(offsetof(ActModelDatabase, count) == 0xa4, "the model database's count is at +0xa4");
static_assert(sizeof(ActModelDatabase) == 0xb0, "an ActModelDatabase is 0xb0 bytes");

// ActTextureDatabase::TextureInfo (operator new)
struct ActTextureInfo {               // 0x28
    EAGL::TAR *tar;                   // +0x00 over the file's first image
    ShapeFile *file;                  // +0x04
    char name[0x20];                  // +0x08
};
static_assert(sizeof(ActTextureInfo) == 0x28, "a TextureInfo is 0x28 bytes");

class ActTextureDatabase {            // 0xac ("ActTextureDatabase")
public:
    enum { kMaxTextures = 40 };

    TextureInfoRefCounter *refs;      // +0x00
    uint8_t *shapes;                  // +0x04 data\Render\Actors.xsh, its images registered with EAGL
    ActTextureInfo *textures[kMaxTextures];   // +0x08 in the order loaded
    int32_t count;                    // +0xa8

    ActTextureDatabase* Construct();                                            // 0x0001a9a0
    void Destruct();                                                            // 0x0001a6c0
    // Loads the shape file `path` as the texture `name`, unless the name is loaded already.
    void LoadTexture(const char *name, const char *path);                       // 0x0001a770
    EAGL::TAR* UseTexture(const char *name);                                    // 0x00019e30
};
static_assert(offsetof(ActTextureDatabase, count) == 0xa8, "the texture database's count is at +0xa8");
static_assert(sizeof(ActTextureDatabase) == 0xac, "an ActTextureDatabase is 0xac bytes");

// ---- the two URefCounters' trees: their compiled copies of RefCounterTree's code (engine/URefCounter.h)

class ModelInfoRefTree : public RefCounterTree {
public:
    void EraseSubtree(RefCounterNode *node);                                    // 0x00017860
    RefCounterNode** EraseAt(RefCounterNode **result, RefCounterNode *where);   // 0x000178e0
    RefCounterNode** InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where,
                              const RefCounterValue *value);                    // 0x00017c70
    RefCounterNode** EraseRange(RefCounterNode **result, RefCounterNode *first,
                                RefCounterNode *last);                          // 0x00017e60
    RefCounterInsertResult* InsertUnique(RefCounterInsertResult *result,
                                         const RefCounterValue *value);         // 0x00017f50
    // The destructor's erase and free, without its subtree erase (only an exception unwind calls it).
    void DestroyRange();                                                        // 0x00018270
};

class TextureInfoRefTree : public RefCounterTree {
public:
    void EraseSubtree(RefCounterNode *node);                                    // 0x00019e60
    RefCounterNode** EraseAt(RefCounterNode **result, RefCounterNode *where);   // 0x00019eb0
    RefCounterNode** InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where,
                              const RefCounterValue *value);                    // 0x0001a220
    RefCounterNode** EraseRange(RefCounterNode **result, RefCounterNode *first,
                                RefCounterNode *last);                          // 0x0001a410
    RefCounterInsertResult* InsertUnique(RefCounterInsertResult *result,
                                         const RefCounterValue *value);         // 0x0001a500
    void DestroyRange();                                                        // 0x0001a840
};

// std::out_of_range, the trees' "invalid map/set<T> iterator" (0x28 bytes: logic_error's, the vtable at
// 0x0018a29c).
struct OutOfRangeError : GameStd::LogicError {
    OutOfRangeError* ConstructCopy(const OutOfRangeError *other);               // 0x00017c50
    void Destruct();                                                            // 0x000178d0
    OutOfRangeError* Delete(unsigned flags);                                    // 0x000178b0
};
static_assert(sizeof(OutOfRangeError) == 0x28, "an out_of_range is 0x28 bytes");

#endif // DRIVING_ANIM_MODEL_H_
