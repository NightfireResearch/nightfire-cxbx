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

namespace EAGL {
struct GeoPrim;
}

// REmp's GeoPrim (0x7c) over a child of the render method at 0x00242e1c: the model, model-view and projection
// matrices, "GAME::FishEyeParams", eight "GAME::PositionalLights" and "GAME::SimStep". The name is ours.
struct EmpGeoPrim {
    EAGL::RenderMethod *method;         // +0x00
    EAGL::GeoPrimParam params[15];      // +0x04

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
static_assert(sizeof(RDecalManager) == 0x2a0, "RDecalManager is 0x2a0 bytes");
static_assert(offsetof(RDecalManager, types) == 0x10 && offsetof(RDecalManager, nextDecal) == 0x294,
              "RDecalManager layout");

// REmp's strip round a cylinder: 17 pairs of vertices a sixteenth of a turn apart, each pair (radiusA, zA) then
// (radiusB, zB) - x = cos * radius, y = sin * radius + 0.5 - with texture coordinates ((x + 1) / 2, (z + 1) / 2)
typedef float CylinderUV[2];
void CylinderSection(Coord3 *positions, CylinderUV *uvs, float radiusB, float radiusA, float zA,
                     float zB);                                                         // 0x0009b470

#define TheDecalManager (*(RDecalManager **)0x001fe820)

#endif // DRIVING_RENDER_DECALS_H_
