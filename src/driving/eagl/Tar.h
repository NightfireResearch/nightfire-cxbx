#ifndef DRIVING_EAGL_TAR_H_
#define DRIVING_EAGL_TAR_H_

// EAGL::TAR, the texture attribute record: a texture binding (sampler state, palette, bump-env matrix) over a
// shared image record that owns the D3D texture, and the texture code around it - creation from a SHAPE, the
// commit to D3D, the bind (Use), the loader's TAR constructor (docs/driving/eagl.md 2.6, 4.4, 7.3, 7.5, 8.3).
// See Tar.cpp. In namespace EAGL, as in Ghidra. The RUNTIME_ALLOC property parsers of the same range are in
// RuntimeAlloc.cpp.

#include <stdint.h>

class DynamicLoader;

namespace EAGL {

struct TAR;

// EAGL::TARPrivate::SharedData: the image a TAR draws, shared by every TAR made from the same SHAPE. It lives in the
// SHAPE's 'i' attachment when the file has room for it (magic "USED"), else in a long-name "EAGL" block, else it is
// allocated (bit 23 of +0x10).
struct TARSharedData {               // 0x34
    uint8_t magic[4];                // +0x00 "USED" in use, "DGK!" once released
    int32_t refCount;                // +0x04 TARs on it
    int32_t width;                   // +0x08
    int32_t height;                  // +0x0c
    uint32_t bits;                   // +0x10 0..5 mip levels (signed), 6..13 depth, 22 set by Init, 23 allocated by
                                     //       EAGL (freed with it), 24 the SHAPE's 0x2000 flag (linear: keep address modes)
    uint32_t format;                 // +0x14 D3DFORMAT
    void *atlas;                     // +0x18 the atlas list LoadAtlas copied from the TAR
    uint8_t *shape;                  // +0x1c the SHAPE image (TAR::GetShape)
    uint8_t *pixels;                 // +0x20 the image's pixels
    void *contiguous;                // +0x24 the copy path's physical copy (never made now: see TAR::Commit)
    uint32_t *texture;               // +0x28 the D3D texture header (0x14; D3D-created ones have common bit 24)
    uint32_t flags;                  // +0x2c bit 0: the SHAPE header was allocated here (render targets)
    uint8_t yuv;                     // +0x30 YUY2: D3DRS_YUVENABLE while bound
    uint8_t pad31[3];

    TARSharedData* Init();                                                   // 0x000eb840 (invented)
    void Release();                                                          // 0x000eb880 (invented)
};
static_assert(sizeof(TARSharedData) == 0x34, "a TAR's shared data is 0x34 bytes");

// The Xbox extension's methods work through the TAR's +0x48 field: their `this` is the field's address, the field
// points at the TAR (itself). Opcode 15 calls SetStage that way.
struct TARExtension {
    TAR *tar;                        // +0x00

    TARExtension* Construct(TAR *owner);                                     // 0x000ebd40
    void DumpState();                                                        // 0x000ebd50 (empty)
    bool SetStage(uint32_t stage);                                           // 0x000ebd60
    bool GetStage(uint32_t *stage) const;                                    // 0x000ebd70
    bool SetMaxAnisotropy(uint32_t anisotropy);                              // 0x000ebd80
    bool GetMaxAnisotropy(uint32_t *anisotropy) const;                       // 0x000ebd90
    bool SetBumpEnvMatrix(float m00, float m01, float m10, float m11);       // 0x000ebda0
    bool GetBumpEnvMatrix(float *matrix) const;                              // 0x000ebdd0
    void SetAtlas(void *atlas);                                              // 0x000eb3a0 (invented)
    void* GetAtlas() const;                                                  // 0x000eb3e0 (invented)
    void SwapShape(uint8_t *shape);                                          // 0x000ec420 (invented)
    void Share(TAR *other);                                                  // 0x000ec530 (invented)
};
static_assert(sizeof(TARExtension) == 4, "the extension handle is one pointer");

struct TAR {                         // 0x4c ("EAGL::TAR new"); arrays of them are 0x50 apart
    uint32_t stage;                  // +0x00 texture stage
    uint32_t address0;               // +0x04 set with the others, never sent
    uint32_t addressU;               // +0x08 D3DTSS_ADDRESSU (1 wrap, 3 clamp)
    uint32_t addressV;               // +0x0c
    uint32_t addressW;               // +0x10
    uint32_t filter;                 // +0x14 D3DTSS_MAGFILTER and MINFILTER
    float lodBias;                   // +0x18 D3DTSS_MIPMAPLODBIAS
    uint32_t mipFilter;              // +0x1c D3DTSS_MIPFILTER
    uint8_t committed;               // +0x20
    uint8_t pad21[3];
    uint32_t maxAnisotropy;          // +0x24 D3DTSS_MAXANISOTROPY
    float bumpEnv[4];                // +0x28 BUMPENVMAT00, 01, 10, 11
    void *palette;                   // +0x38 D3DPalette
    uint8_t *clut;                   // +0x3c the clut's colours
    TARSharedData *data;             // +0x40
    void *atlas;                     // +0x44 {x, y, image} records, 0xc apart, from +4 (LoadAtlas)
    TAR *extension;                  // +0x48 itself

    TAR* InitFields();                                                       // 0x000eb7a0 (invented)
    TAR* Construct(uint8_t *shape);                                          // 0x000eca20
    TAR* ConstructShared();                                                  // 0x000ebee0 (invented)
    TAR* ConstructCopy(const TAR *other);                                    // 0x000ec3b0
    void Destruct();                                                         // 0x000ecab0
    bool SwapClut(uint8_t *clutShape);                                       // 0x000eb220
    bool SwapShape(uint8_t *shape);                                          // 0x000ecbc0
    uint8_t* GetShape() const;                                               // 0x000eb790
    int32_t GetRefCount() const;                                             // 0x000eb390 (invented)
    void Use();                                                              // 0x000eb3f0 (candidate)
    bool ReturnsFalse1();                                                    // 0x000eb770 (unreferenced)
    bool ReturnsFalse2();                                                    // 0x000eb780 (unreferenced)
    void ReleasePalette();                                                   // 0x000eb7f0 (invented)
    void DumpState();                                                        // 0x000eb900 (empty)
    void LoadAtlas();                                                        // 0x000eb910 (invented, no caller)
    uint32_t Commit();                                                       // 0x000eba80 (invented)
    void Create(uint8_t *shape);                                             // 0x000ec000 (invented)
};
static_assert(sizeof(TAR) == 0x4c, "a TAR is 0x4c bytes");

// EAGLInternal::Property's folded constructor (Ghidra: RMissileStreak::RMissileStreak) and element destructor.
struct TARProperty {                 // 0xc
    const char *name;
    int32_t count;
    const char **values;

    TARProperty* Construct();                                                // 0x000ed310
    void Destruct();                                                         // 0x000ed320
};
static_assert(sizeof(TARProperty) == 0xc, "a property is 0xc bytes");

struct TARProperties {               // 0x10, RuntimeAlloc.h's Properties
    int32_t count;
    TARProperty *list;
    int32_t length;
    char *buffer;

    void Destruct();                                                         // 0x000edde0
};
static_assert(sizeof(TARProperties) == 0x10, "a property list is 0x10 bytes");

}  // namespace EAGL

uint32_t EAGL_TextureFormatFromShape(const uint8_t *shape);                  // 0x000eb070 (invented)
uint8_t* EAGL_FindClut(uint8_t *shape);                                      // 0x000eb200 (invented)
EAGL::TARSharedData* EAGL_FindSharedData(uint8_t *shape);                    // 0x000ebe00 (invented)
EAGL::TAR* EAGL_TARFromSurface(void *surface);                               // 0x000ec610 (invented)
EAGL::TAR* EAGL_TARRenderTarget(int32_t width, int32_t height, int32_t depth, int32_t mode);  // 0x000ec6e0
EAGL::TAR* EAGL_TARDepthSurface(int32_t width, int32_t height, int32_t depth);               // 0x000ec8a0
void EAGL_TARConstructor(void *object, DynamicLoader *loader);               // 0x000ed070 EAGLInternal::TARConstructor
void EAGL_TARDestructor(void *object);                                       // 0x000ed2b0 EAGLInternal::TARDestructor
void EAGL_TARDestructor2(void *object);                                      // 0x000ed2c0 (the same, unreferenced)
void EAGL_ShapeCountEax();                                                   // 0x000ede20 (EAX = SHPX file)
void EAGL_ShapeImageEax();                                                   // 0x000ede30 (ECX = SHPX, EAX = index)
int EAGL_AddSymbolAnswerZero(const char *name, void *value);                 // 0x000ede40 (invented)

#endif // DRIVING_EAGL_TAR_H_
