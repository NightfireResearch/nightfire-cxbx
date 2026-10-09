#ifndef DRIVING_RENDER_MATERIALS_H_
#define DRIVING_RENDER_MATERIALS_H_

// ---------------------------------------------------------------------------------------------------------------
// The game's simple materials: GeoPrimStates for vertices the caller builds every frame. Each class has a ring of
// sixteen draw requests - a GeoPrim (the material's render method and its parameters) and a DynamicModel holding
// it - made by Init. The caller points the current request's vertex arrays at its data, then calls Draw, which
// fills in the counts and the material, draws the request's model and moves the ring on. USimpleMaterial draws
// coloured vertices ("MatDebug"); USimpleTexturedMaterial and UVolatileMaterial ("MatVolatileTexture") textured
// ones. See Materials.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../eagl/GeoPrimState.h"
#include "../eagl/EaglGlobals.h"       // EaglMalloc
#include "../eagl/RenderMethod.h"      // GeoPrimParam

struct MATRIX4;

// The primitive types Draw takes (D3D's numbering on the Xbox)
enum DrawPrimitive : uint32_t {
    kTriangleList = 5,
    kTriangleStrip = 6,
    kQuadList = 8,
};

// The alpha blend modes (GeoPrimState::SetAlphaBlendMode) Draw::SetNormalBlendMode and SetAdditiveBlendMode set
enum AlphaBlendMode : uint32_t {
    kBlendNormal = 1,
    kBlendAdditive = 2,
};

// USimpleMaterial's GeoPrim (0x2c bytes, "EAGL::GeoPrim new"; the name is ours)
struct SimpleGeoPrim {
    EAGL::RenderMethod *method;         // +0x00 a child of "MatDebug"
    EAGL::GeoPrimParam state;                 // +0x04 the material
    EAGL::GeoPrimParam vertexCount;           // +0x0c the material's vertexCount
    EAGL::GeoPrimParam modelViewProjection;   // +0x14 EAGL::ViewPort::gpModelViewProjectionMatrix
    EAGL::GeoPrimParam positions;             // +0x1c Vec4 each
    EAGL::GeoPrimParam colours;               // +0x24 0xAARRGGBB each

    SimpleGeoPrim* Construct();         // (the name is ours)                                  // 0x0011be40
};
static_assert(sizeof(SimpleGeoPrim) == 0x2c, "SimpleGeoPrim is 0x2c bytes");

// The textured materials' GeoPrim (0x3c bytes; the name is ours). UVolatileMaterial makes its own; the one
// USimpleTexturedMaterial makes, the decals' and the missile camera's are made by Construct, over a child of the
// render method at 0x00242de8.
struct TexturedGeoPrim {
    EAGL::RenderMethod *method;         // +0x00
    EAGL::GeoPrimParam state;                 // +0x04 the material
    EAGL::GeoPrimParam texture;               // +0x0c an EAGL::TAR
    EAGL::GeoPrimParam vertexCount;           // +0x14 the material's vertexCount
    EAGL::GeoPrimParam modelViewProjection;   // +0x1c
    EAGL::GeoPrimParam positions;             // +0x24 Vec4 each
    EAGL::GeoPrimParam colours;               // +0x2c 0xAARRGGBB each
    EAGL::GeoPrimParam texCoords;             // +0x34 Vec4 each (u, v used)

    TexturedGeoPrim* Construct();                                                               // 0x0009ac40
    // UVolatileMaterial's: a child of "MatVolatileTexture" (the name is ours)
    TexturedGeoPrim* ConstructVolatile();                                                       // 0x0011c380
    EAGL::GeoPrim* AsGeoPrim() { return reinterpret_cast<EAGL::GeoPrim *>(this); }
};
static_assert(sizeof(TexturedGeoPrim) == 0x3c, "TexturedGeoPrim is 0x3c bytes");

// The three material classes share their layout (0x60 bytes: a GeoPrimState and the vertex count its draws send)
// and their destructor; the other two derive from this one here for that, the game's own hierarchy is not known.
class USimpleMaterial : public EAGL::GeoPrimState {
public:
    uint32_t unknown4C;
    uint32_t vertexCount;               // +0x50 Draw's count
    uint32_t unknown54[3];

    USimpleMaterial* Construct();                                                               // 0x0011bb60
    // The materials' destructor (all three; a jump to GeoPrimState's)
    void Destruct();                                                                            // 0x0011bbe0
    // Draws the current request: `count` vertices of `primitive`, under `transform` (NULL: the model's own)
    void Draw(int primitive, int count, MATRIX4 *transform);                                    // 0x0011bbf0
    static void Init();                                                                         // 0x0011bee0
    static void Shutdown();                                                                     // 0x0011c100
};
static_assert(sizeof(USimpleMaterial) == 0x60, "a simple material is 0x60 bytes");

class USimpleTexturedMaterial : public USimpleMaterial {
public:
    USimpleTexturedMaterial* Construct();                                                       // 0x0011bcd0
    void Draw(int primitive, int count, MATRIX4 *transform);                                    // 0x0011bd50
    static void Init();                                                                         // 0x0011bfe0
    static void Shutdown();                                                                     // 0x0011c180
};
static_assert(sizeof(USimpleTexturedMaterial) == 0x60, "a simple textured material is 0x60 bytes");

class UVolatileMaterial : public USimpleMaterial {
public:
    UVolatileMaterial* Construct();                                                             // 0x0011c210
    void Draw(int primitive, int count, MATRIX4 *transform);                                    // 0x0011c290
    static void Init();                                                                         // 0x0011c430
    static void Shutdown();                                                                     // 0x0011c540
};
static_assert(sizeof(UVolatileMaterial) == 0x60, "a volatile material is 0x60 bytes");

// A GeoPrim's render method: a child of `parent` ("EAGL::Rendermethod new"; the game inlines it in every GeoPrim
// constructor)
inline EAGL::RenderMethod *NewChildRenderMethod(EAGL::RenderMethod *parent) {
    void *memory = EaglMalloc(sizeof(EAGL::RenderMethod), "EAGL::Rendermethod new");
    return memory != NULL ? static_cast<EAGL::RenderMethod *>(memory)->ConstructChild(parent) : NULL;
}

void InitAllSimpleMaterials();                                                                  // 0x0011c0f0
void ShutdownAllSimpleMaterials();                                                              // 0x0011c200

// The rings: each class's current request, its requests and their models
constexpr int kMaterialRequests = 16;
#define SimpleRequestIndex (*(uint32_t *)0x00243708)
#define SimpleRequests ((SimpleGeoPrim **)0x00243758)
#define SimpleModels ((EAGL::DynamicModel **)0x00243798)
#define TexturedRequestIndex (*(uint32_t *)0x00243750)
#define TexturedRequests ((TexturedGeoPrim **)0x00243710)
#define TexturedModels ((EAGL::DynamicModel **)0x002437d8)
#define VolatileRequestIndex (*(uint32_t *)0x00243830)
#define VolatileRequests ((TexturedGeoPrim **)0x00243878)
#define VolatileModels ((EAGL::DynamicModel **)0x00243838)

#endif // DRIVING_RENDER_MATERIALS_H_
