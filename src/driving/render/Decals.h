#ifndef DRIVING_RENDER_DECALS_H_
#define DRIVING_RENDER_DECALS_H_

// ---------------------------------------------------------------------------------------------------------------
// RDecalManager (0x2a0 bytes, one instance at 0x001fe820, a USingleton): a ring of 168 quads laid on surfaces
// (RScorchWorld's scorch marks), each of one of the eight types data\render\decal.dat describes, drawn through
// eight game-built GeoPrims of 21 quads each. Also the GeoPrims built here for other effects (the decals', the
// missile camera's and USimpleTexturedMaterial's, and REmp's) and REmp's cylinder strip. See Decals.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../eagl/RenderMethod.h"       // GeoPrimParam
#include "Materials.h"                   // TexturedGeoPrim
#include "../../common/xbeClass.h"       // LAYOUT_32

namespace EAGL {
struct GeoPrim;
}

// The parameters of REmp's GeoPrims (EmpGeoPrim's params: the render method's packets)
enum EmpGeoPrimParam {
    kEmpParamState = 0,
    kEmpParamTexture = 1,
    kEmpParamVertexCount = 2,           // a vertex count record
    kEmpParamIndices = 3,
    kEmpParamModelMatrix = 4,           // "EAGL::ViewPort::gpModelMatrix"
    kEmpParamModelViewMatrix = 5,       // "EAGL::ViewPort::gpModelViewMatrix"
    kEmpParamProjectionMatrix = 6,      // "EAGL::ViewPort::gpProjectionMatrix"
    kEmpParamFishEye = 7,               // "GAME::FishEyeParams"; REmp::Init points it at the reflection's warpage
    kEmpParamLights = 8,                // "GAME::PositionalLights"; REmp::Init points it at the light manager's
    kEmpParamConstants = 9,
    kEmpParam10 = 10,
    kEmpParamSimStep = 11,              // "GAME::SimStep"
    kEmpParamPositions = 12,
    kEmpParamTexCoords = 13,
    kEmpParamColours = 14,
    kEmpParamCount = 15,
};

// REmp's GeoPrim (0x7c) over a child of the render method at 0x00242e1c: the model, model-view and projection
// matrices, "GAME::FishEyeParams", eight "GAME::PositionalLights" and "GAME::SimStep". The name is ours.
struct EmpGeoPrim {
    EAGL::RenderMethod *method;         // +0x00
    EAGL::GeoPrimParam params[kEmpParamCount];  // +0x04 by EmpGeoPrimParam

    EmpGeoPrim* Construct();            // FUN_0009b550                                  // 0x0009b550
};
static_assert(sizeof(EmpGeoPrim) == 0x7c, "REmp's GeoPrim is 0x7c bytes");

// One of decal.dat's eight records (0x50)
struct DecalType {
    Coord4 uvs[4];                      // +0x00 the corners' texture coordinates
    uint32_t unknown40;
    uint32_t delay;                     // +0x44 draws before the quad shows (its alpha 0 until then); 0 counts as 1
    uint8_t alpha;                      // +0x48 its alpha, a quarter of it (AddDecal shifts it up two)
    uint8_t unknown49[7];
};
static_assert(sizeof(DecalType) == 0x50, "a decal type is 0x50 bytes");

class RDecalManager {
public:
    const void *vtable;                 // +0x000 0x00192838
    uint8_t unknown004[0xc];
    DecalType types[8];                 // +0x010 decal.dat
    uint32_t unknown290;                // +0x290 (Ghidra: numDecals)
    int32_t nextDecal;                  // +0x294 the ring's next slot
    uint8_t unknown298[8];

    RDecalManager* Construct();                                                         // 0x0009acf0
    void Destruct();                                                                    // 0x0009b3c0
    RDecalManager* Delete(unsigned flags);  // the scalar deleting destructor, vtable slot 0  // 0x0009b450
    static void Kill();                     // vtable slot 2                             // 0x0009af50
    // Every quad cleared and the GeoPrims pointed at the arrays again; vtable slot 1
    void Reset();                                                                       // 0x0009af70
    // A quad centred on `centre`, `width` along normal x axis and `height` along (normal x axis) x axis, into the
    // next slot
    void AddDecal(const Coord4 *centre, const Coord4 *axis, const Coord4 *normal, int type, float width,
                  float height);                                                        // 0x0009b070
    // Counts the delays down, then draws every quad, depth writes off
    void DrawDecals();                                                                  // 0x0009b2d0
};
LAYOUT_32(sizeof(RDecalManager) == 0x2a0, "RDecalManager is 0x2a0 bytes");
LAYOUT_32(offsetof(RDecalManager, types) == 0x10 && offsetof(RDecalManager, nextDecal) == 0x294,
          "RDecalManager layout");

// REmp's strip round a cylinder: 17 pairs of vertices a sixteenth of a turn apart, each pair (radiusA, zA) then
// (radiusB, zB) - x = cos * radius, y = sin * radius + 0.5 - with texture coordinates ((x + 1) / 2, (z + 1) / 2)
typedef float CylinderUV[2];
void CylinderSection(Coord3 *positions, CylinderUV *uvs, float radiusB, float radiusA, float zA,
                     float zB);                                                         // 0x0009b470

extern RDecalManager *TheDecalManager;

// The quads: eight GeoPrims of 21, six vertices each (two triangles), and per quad its alpha and delay
extern TexturedGeoPrim *DecalGeoPrims[8];
extern uint8_t DecalAlpha[8 * 21];
extern uint8_t DecalDelay[8 * 21];
extern Coord4 DecalPositions[8 * 21 * 6];
extern Coord4 DecalUVs[8 * 21 * 6];
extern uint32_t DecalColours[8 * 21 * 6];
extern uint32_t DecalVertexCount[4];        // the GeoPrims' vertex count record

// The decals' render state and the render methods their GeoPrims inherit (Decals.cpp)
extern EAGL::GeoPrimState DecalState;
extern const std::array<uint32_t, 34> TexturedParentVSFunction;
extern const D3DPixelShaderDef TexturedParentPSDefinition;
extern const void *TexturedParentVSFunctions[1];
extern const uint8_t *TexturedParentPSDefinitions[1];
extern uint32_t TexturedParentPackets[37];
extern const uint32_t TexturedParentVSDeclaration[7];
extern const char *TexturedParentParamNames[8];
extern const std::array<uint32_t, 202> EmpParentVSFunction;
extern const D3DPixelShaderDef EmpParentPSDefinition;
extern const void *EmpParentVSFunctions[1];
extern const uint8_t *EmpParentPSDefinitions[1];
extern uint32_t EmpParentPackets[65];
extern const uint32_t EmpParentVSDeclaration[7];
extern const char *EmpParentParamNames[16];
extern EAGL::RenderMethod TexturedParentMethod;
extern EAGL::RenderMethod EmpParentMethod;

#endif // DRIVING_RENDER_DECALS_H_
