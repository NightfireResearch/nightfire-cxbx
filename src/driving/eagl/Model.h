#ifndef DRIVING_EAGL_MODEL_H_
#define DRIVING_EAGL_MODEL_H_

// EAGL::Model and EAGL::DynamicModel (docs/driving/eagl.md 2.7, 4.3), with everything else in the address range
// 0x000e8b40..0x000eb220 and EAGLInternal::ModelSetScale (0x000f0e40). See Model.cpp. In namespace EAGL, as in
// Ghidra. Invented names are marked; the rest are Ghidra's (Model::Patc is Patch, the name cut short).

#include <stddef.h>
#include <stdint.h>

#include "Tar.h"

namespace EAGL {

struct GeoPrim;   // {RenderMethod* +0x00; parameter block +0x04 ...} (2.8)
struct DrawArray;

// A model's morph weights (invented name; e8b50..e8bc0 work on its first three words, MorphModel on the rest).
struct MorphWeights {
    uint32_t count;                  // +0x00 the accessors accept an index up to and including it
    void **pointers;                 // +0x04
    float *weights;                  // +0x08
    uint32_t unknown0c;              // +0x0c
    uint32_t groupCount;             // +0x10 number of morph groups
    uint32_t groups[1];              // +0x14 per group: its name, then its sub-blocks (MorphModel)

    void* GetPointer(uint32_t index);                                        // 0x000e8b50
    void SetWeight(uint32_t index, float weight);                            // 0x000e8b70
    float GetWeight(uint32_t index);                                         // 0x000e8b90
    float* GetWeights();                                                     // 0x000e8bb0
    MorphWeights* Construct();                                               // 0x000e8bc0
};
static_assert(offsetof(MorphWeights, groups) == 0x14, "the morph groups follow at +0x14");

// A morph record (Model +0xb8's data): where one morph target's data goes and its rest copy.
struct MorphRecord {                 // 0x10
    int32_t id;                      // +0x00
    int16_t elementSize;             // +0x04
    int16_t strideFlags;             // +0x06 the base stride * 2; bit 0: one copy per variation
    uint32_t variationStride;        // +0x08
    uint32_t target;                 // +0x0c the data's address; the rest copy is variationStride before it
};
static_assert(sizeof(MorphRecord) == 0x10, "a morph record is 16 bytes");

// What Model::GetTARList fills in (invented name)
struct TARList {
    int32_t variation;               // +0x00 the model's variation, or -1 when the name is not found
    int32_t count;                   // +0x04
    TAR **tars;                      // +0x08 each the first of an array of variations
};

// Model::DrawInstances' argument (invented name)
struct InstanceStream {              // 0x10
    int16_t unknown00;               // +0x00
    int16_t stride;                  // +0x02 bytes per instance
    uint8_t *source;                 // +0x04 per-instance data, or 0
    void *destination;               // +0x08 where the instance's slice is copied before it is drawn
    uint32_t unknown0c;              // +0x0c
};
struct InstanceEnable {              // 4 per instance
    uint8_t enable;                  // +0x00
    uint8_t unknown01;               // +0x01
    int16_t variation;               // +0x02
};
struct InstanceData {
    uint32_t unknown00;              // +0x00
    uint32_t unknown04;              // +0x04
    const InstanceEnable *enable;    // +0x08 one per instance
    const float *matrices;           // +0x0c one 4x4 model matrix per instance
    int32_t streamCount;             // +0x10
    InstanceStream *streams;         // +0x14
};

// One geometry block's entry in a model's enable table
struct GeometryEnable {              // 4
    uint16_t unknown00;              // +0x00
    uint16_t enable;                 // +0x02 0: the block is skipped
};

struct Model {                       // 0xd8; built by the loader from a "Model" symbol (2.10)
    uint32_t unknown00;              // +0x00 (docs: the name; GetChild compares +0xb4)
    uint32_t unknown04;              // +0x04
    uint32_t unknown08;              // +0x08
    float matrix[16];                // +0x0c model matrix
    float scale[4];                  // +0x4c } given to ModelSetScale, which is empty on the Xbox
    float offset[4];                 // +0x5c }
    uint8_t unknown6c[0x20];         // +0x6c
    float sphereCentre[3];           // +0x8c bounding sphere, in model space
    float sphereRadius;              // +0x98
    int32_t geometryCount;           // +0x9c
    union {                          // +0xa0
        const char **geometryNames;  //       the geometry names
        GeometryEnable defaultEnable;  //     a model built by Construct: its one enable entry {0, 1}
    };
    Model *sameMatrixChildren;       // +0xa4 drawn with the parent's matrix (Ghidra: numChildren)
    Model *ownMatrixChildren;        // +0xa8 drawn with their own matrix (Ghidra: children)
    Model *relativeChildren;         // +0xac drawn with their matrix times the parent's
    Model *next;                     // +0xb0 next sibling in one of those lists
    const char *name;                // +0xb4
    uint8_t *morphData;              // +0xb8 a word, then {name, n, n MorphRecords} ... 0
    uint32_t *tarList;               // +0xbc {name, count, TAR* x count} ... 0
    int32_t variation;               // +0xc0 copied to CurrentVariation (Ghidra: tarList)
    GeometryEnable *enableTable;     // +0xc4 one per geometry block
    int32_t on;                      // +0xc8
    uint32_t *drawList;              // +0xcc entries {0xffff, GeoPrim*} and skippable blocks (Ghidra: optimized)
    uint32_t *patch;                 // +0xd0 {count, {address, value} x count}, applied by Patch before drawing
    MorphWeights *morphWeights;      // +0xd4

    const char* GetName();                                                   // 0x000e8be0 (invented)
    Model* GetChild(const char *childName);                                  // 0x000e8bf0
    bool AddSameMatrixChild(Model *child);                                   // 0x000e8d00 (invented)
    bool AddRelativeChild(Model *child);                                     // 0x000e8d30 (invented)
    bool AddOwnMatrixChild(Model *child);                                    // 0x000e8d60 (invented)
    Model* RemoveChild(const char *childName);                               // 0x000e8d90 (invented)
    uint8_t* GetGeometry(const char *geometryName);                          // 0x000e8f60
    float* GetModelMatrix();                                                 // 0x000e8fe0 (invented)
    void SetModelMatrix(const float *m);                                     // 0x000e8ff0
    void SwapShapes(uint8_t *shapes);                                        // 0x000e90f0 (invented)
    TARList* GetTARList(TARList *out, const char *listName);                 // 0x000e9170 (returns a TARList)
    void SetTextureList(const TARList *list, uint8_t *shape);                // 0x000e9210 (Ghidra: SetTexture)
    void DrawInstances(const InstanceData *instances, int first, int count); // 0x000e9260
    void Optimize();                                                         // 0x000e9410
    bool IsOn();                                                             // 0x000e94e0 (invented)
    void TurnOn();                                                           // 0x000e94f0 (invented)
    void TurnOff();                                                          // 0x000e9500 (invented)
    void Patch(const uint32_t *patchList);                                   // 0x000e9510 (Ghidra: Patc)
    Model* ConstructCopy(const Model *other);                                // 0x000e9540 (invented)
    Model* Construct();                                                      // 0x000e9600 (invented)
    void Destruct();                                                         // 0x000e96a0 (invented)
    void SetMorphWeights(MorphWeights *weights);                             // 0x000e96b0 (invented)
    void ClearMorphModel();                                                  // 0x000e96c0
    void SetTexture(const char *listName, uint8_t *shape);                   // 0x000ea1e0
    void Draw(const float *parentMatrix);                                    // 0x000ea210
    void MorphModel();                                                       // 0x000ea480
    void Call(const char *callName);                                         // 0x000ea7b0
};
static_assert(sizeof(Model) == 0xd8, "a Model is 0xd8 bytes");
static_assert(offsetof(Model, geometryNames) == 0xa0 && offsetof(Model, morphWeights) == 0xd4, "Model layout");

struct DynamicModel {                // 0x58; the game builds these for effects, decals, materials
    float matrix[16];                // +0x00 model matrix
    uint32_t unknown40;              // +0x40
    int32_t capacity;                // +0x44
    int32_t count;                   // +0x48
    GeoPrim **geoPrims;              // +0x4c
    DrawArray **drawArrays;          // +0x50 one per GeoPrim, filled only by the dead vertex-array API
    uint32_t unknown54;              // +0x54

    DynamicModel* Construct();                                               // 0x000e9760
    void Reserve(int newCapacity);                                           // 0x000e97b0 (invented)
    int AddGeoPrim(GeoPrim *geoPrim);                                        // 0x000e9890
    void DrawNoTransform();                                                  // 0x000e9990
    void Draw();                                                             // 0x000e99e0
    float* GetModelMatrix();                                                 // 0x000e9a70 (PS2 name)
    void SetModelMatrix(const float *m);                                     // 0x000e9a80
    bool SetPrimitiveType(int index, int type);                              // 0x000e9aa0
    int GetIndexFromName(int index, const char *varName);                    // 0x000e9b40
    bool SetVarByName(int index, const char *varName, void *data, int size); // 0x000e9be0 (Ghidra: SetVar)
    bool SetVar(int index, int var, void *data, int size);                   // 0x000e9c90
    bool SetStreamByName(int index, const char *varName, void *data, int size); // 0x000e9d40 (Ghidra: SetStream)
    bool SetStream(int index, int var, void *data, int size);                // 0x000e9df0
    bool SetParamName(int index, const char *paramName);                     // 0x000e9ea0
    bool Lock(int index);                                                    // 0x000e9f40
    bool Unlock(int index);                                                  // 0x000e9fe0
    bool SetNumVerts(int index, int verts);                                  // 0x000ea080
    void Destruct();                                                         // 0x000ea720
};
static_assert(sizeof(DynamicModel) == 0x58, "a DynamicModel is 0x58 bytes");

// Unreferenced helpers of unknown classes in the range (invented names)
struct ModelHeaderBits {
    uint8_t bytes[8];
    bool TestLowBits6();                                                     // 0x000ea120
};
struct ModelNameEntry {              // 0x10
    const char *name;                // +0x00
    uint32_t unknown04[3];           // +0x04
};
struct ModelNameTable {              // {begin, count} over the entries
    ModelNameEntry *begin;
    int32_t count;
    ModelNameEntry** Find(ModelNameEntry **out, const char *prefix);         // 0x000ea870 (returns an iterator)
    ModelNameEntry** FindLast(ModelNameEntry **out, const char *prefix);     // 0x000ea8d0 (returns an iterator)
};
struct ModelShapeImage : ShapeImage {
    uint8_t* Data0();                                                        // 0x000eb020
    uint8_t* Data1();                                                        // 0x000eb040
};

// Free functions in the range (0x000eb070 and 0x000eb200, the texture format and the clut attachment, are
// ported in Tar.cpp)
uint8_t* ModelFindShape(uint8_t *shapes, const char *imageName);             // 0x000e9010 (invented)
void __stdcall ModelCopyBytes(void *destination, const void *source, int bytes); // 0x000ea130 (invented)
void ModelMorphAddBytes(uint8_t *base, const uint8_t *source, int components, int count, int baseStride,
                        float weight, int baseOffset, int sourceStride);     // 0x000ea940 (invented)
void ModelMorphAddShorts(uint8_t *base, const uint8_t *source, int components, int count, int baseStride,
                         float weight, int baseOffset, int sourceStride);    // 0x000eaa90 (invented)
void ModelMorphAddFloats(uint8_t *base, const uint8_t *source, int components, int count, int baseStride,
                         float weight, int baseOffset, int sourceStride);    // 0x000eabd0 (invented)
void __stdcall ModelIgnore4a(uint32_t unused);                               // 0x000eaf80 (invented)
void __stdcall ModelIgnore4b(uint32_t unused);                               // 0x000eafa0 (invented)
uint32_t __stdcall ModelReturnZero4(uint32_t unused);                        // 0x000eaff0 (invented)

// Register-argument code (out-of-line copies of XDK inlines, all unreferenced): naked, registers as the original.
void ModelSetRenderStateRegs();      // 0x000eaca0 ESI = state index, EDI = value
void ModelSetTextureStateRegs();     // 0x000eaea0 EAX = state, ECX = stage, EDX = value
void ModelCreatePaletteRegs();       // 0x000eaf20 EAX = size, [esp+8] = out; RET 8
void ModelRegisterRegs();            // 0x000eaf40 ECX = resource, EAX = base
void ModelGet2DSurfaceDescRegs();    // 0x000eaf50 EDX, ECX, EAX
void ModelGetSurfaceLevelRegs();     // 0x000eaf60 ECX = texture, EAX = level, [esp+4] = out; RET 4
void ModelReleaseRegsA();            // 0x000eaf90 EAX = resource
void ModelReleaseRegsB();            // 0x000eafb0 EAX = resource
void ModelSurfaceGetDescRegs();      // 0x000eafc0 ECX = surface, EAX = desc
void ModelLockRectRegs();            // 0x000eafd0 [esp+4] = surface, EDX, ECX, EAX; RET 4
void ModelWordAt4Regs();             // 0x000eb000 EAX = object
void ModelWordAt6Regs();             // 0x000eb010 EAX = object
void ModelShapeEntryRegs();          // 0x000eb060 ECX = shapes, EAX = index

}  // namespace EAGL

namespace EAGLInternal {
void ModelDestructor(void *object);                                          // 0x000e8b40 (empty)
void ModelConstructor(EAGL::Model *model);                                   // 0x000ea1d0
void ModelSetScale(const float *scale, const float *offset);                 // 0x000f0e40 (empty)
}  // namespace EAGLInternal

// 0x000e8b20 (EAX = object, returns +8) and 0x000e8b30 (ECX = shapes, EAX = index): Ghidra functions reading EAX,
// which FUNC_AT's ABI check refuses, so they are tagged AUTOLTCG under Ghidra's names. Unreferenced.
void FUN_000e8b20();
void FUN_000e8b30();

#endif // DRIVING_EAGL_MODEL_H_
