#ifndef DRIVING_GAME_DEBRIS_H_
#define DRIVING_GAME_DEBRIS_H_

// ---------------------------------------------------------------------------------------------------------------
// RDebris (0x90 bytes, "RDebris", one: RenderManagers::InitDebris's): thrown pieces of 23 kinds, each kind an
// RDebrisType (0x1ad0 bytes, "RDebrisType") of up to 128 pieces that fly from where they were thrown under gravity,
// spinning about an axis of their own, and fade until their lifetime is over. Each kind draws its pieces as one
// textured triangle strip each (DebrisStrips.kinds: the strip, its texture coordinates and texture). The
// kind drawn with the texture "shel" (kShellCasings) is drawn in the view's frame by DrawShellCasings (for the
// player's weapon), the others by Draw. See Debris.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord4
#include "../eagl/GeoPrimState.h"
#include "../render/Materials.h"        // UVolatileMaterial
#include "WeaponEffects.h"               // EffectUV

namespace EAGL {
struct TAR;
}
class RSceneObj;
struct ArticleEffect;

class RDebrisType {
public:
    static constexpr uint32_t kMaxPieces = 128;

    Coord4 position[kMaxPieces];        // +0x0000 where each piece was thrown from
    Coord4 velocity[kMaxPieces];        // +0x0800 xyz its velocity in units a tick; w the tick it was thrown
    Coord4 spin[kMaxPieces];            // +0x1000 xyz a random axis; w its turn rate
    uint32_t colour[kMaxPieces];        // +0x1800 0xAARRGGBB
    float gravity;                      // +0x1a00 the last Add's
    uint32_t lifetime;                  // +0x1a04 the last Add's, in ticks
    uint32_t count;                     // +0x1a08
    uint32_t kind;                      // +0x1a0c its row of DebrisStrips.kinds
    EAGL::TAR *texture;                 // +0x1a10
    uint8_t unknown1A14[0xc];
    EAGL::GeoPrimState state;           // +0x1a20 constructed, not used
    uint8_t unknown1A6C[4];
    UVolatileMaterial material;         // +0x1a70

    // The constructor (inlined in RDebris's)
    void Construct() {
        count = 0;
        state.Construct();
        material.Construct();
    }
    // The material and texture of `kind`
    void Init(uint32_t kind);                                                                   // 0x000a8f30
    // A piece, unless there are kMaxPieces; its axis and turn rate random (the renderer's sequence, four draws)
    void Add(const Coord4 *position, const Coord4 *velocity, uint32_t colour, uint32_t tick, float gravity,
             uint32_t lifetime);                                                                // 0x000a8fa0
    // The pieces past the lifetime dropped, the others drawn
    void Draw();                                                                                // 0x000a92a0
};
static_assert(sizeof(RDebrisType) == 0x1ad0, "RDebrisType is 0x1ad0 bytes");
static_assert(offsetof(RDebrisType, colour) == 0x1800 && offsetof(RDebrisType, count) == 0x1a08 &&
              offsetof(RDebrisType, texture) == 0x1a10 && offsetof(RDebrisType, state) == 0x1a20 &&
              offsetof(RDebrisType, material) == 0x1a70, "RDebrisType layout");

class RDebris {
public:
    static constexpr int kKinds = 23;
    static constexpr int kShellCasings = 4;     // the kind with the texture "shel"
    static constexpr int kMaxShells = 4;

    RDebrisType *types[kKinds];         // +0x00
    // Shell casings DrawShellCasings throws, each from an object's effect: nothing in the game queues any, so the
    // count stays 0 (the names are ours)
    uint32_t shellCount;                // +0x5c
    uint32_t shellTick[kMaxShells];     // +0x60
    ArticleEffect *shellEffect[kMaxShells];     // +0x70
    RSceneObj *shellObject[kMaxShells];         // +0x80

    RDebris* Construct();               // every kind made, and the strips scaled              // 0x000a91b0
    void Reset();                                                                               // 0x000a8f10
    // A piece of `kind`, thrown now (none for a lifetime of 0)
    void Add(int kind, const Coord4 *position, const Coord4 *velocity, uint32_t colour, float gravity,
             uint32_t lifetime);                                                                // 0x000a9260
    void Draw();                        // every kind but kShellCasings                         // 0x000a9660
    // FUN_000a9680 (the name ours, after its caller ActWeapon::RenderShellCasings): the queued shell casings
    // thrown, then kShellCasings drawn in the view's frame
    void DrawShellCasings();                                                                    // 0x000a9680
};
static_assert(sizeof(RDebris) == 0x90, "RDebris is 0x90 bytes");
static_assert(offsetof(RDebris, shellCount) == 0x5c && offsetof(RDebris, shellObject) == 0x80, "RDebris layout");

// A kind of debris (20 bytes; the names are ours): its piece's strip
struct DebrisKind {
    const Coord4 *positions;
    const EffectUV *texCoords;
    const uint8_t *list;                // a zero-ended list of rising vertex numbers (empty for most); not read
    uint32_t vertexCount;
    uint32_t texture;                   // a texture name ("shel" ...)
};
static_assert(sizeof(DebrisKind) == 20, "a debris kind is 20 bytes");

// The kinds' strips and the kinds, in the XBE's order (0x32ac bytes; the names are ours, each array by the first
// kind that uses it). Kinds 16, 19 and 22 use other kinds' arrays. scaledA (kind 4's) and scaledB (kind 22's) start
// as copies of DebrisVectorsA and vectorsB (kind 16's), and ScaleDebrisTables (render/SkyWater.cpp) scales them.
struct DebrisTables {
    Coord4 positions0[30];          // +0x0000
    Coord4 vectorsB[30];            // +0x01e0
    Coord4 scaledB[30];             // +0x03c0
    EffectUV texCoords0[30];        // +0x05a0
    Coord4 positions1[30];          // +0x0690
    EffectUV texCoords1[30];        // +0x0870
    Coord4 positions2[28];          // +0x0960
    EffectUV texCoords2[28];        // +0x0b20
    Coord4 positions3[8];           // +0x0c00
    EffectUV texCoords3[8];         // +0x0c80
    uint8_t list3[16];             // +0x0cc0
    Coord4 positions7[8];           // +0x0cd0
    EffectUV texCoords7[8];         // +0x0d50
    uint8_t list7[16];             // +0x0d90
    Coord4 positions8[8];           // +0x0da0
    EffectUV texCoords8[8];         // +0x0e20
    uint8_t list8[16];             // +0x0e60
    Coord4 scaledA[26];             // +0x0e70
    EffectUV texCoords4[26];        // +0x1010
    uint8_t list4[16];             // +0x10e0
    Coord4 positions5[30];          // +0x10f0
    EffectUV texCoords5[30];        // +0x12d0
    Coord4 positions6[30];          // +0x13c0
    uint8_t list5[16];             // +0x15a0
    EffectUV texCoords6[30];        // +0x15b0
    Coord4 positions9[30];          // +0x16a0
    EffectUV texCoords9[30];        // +0x1880
    uint8_t list9[16];             // +0x1970
    Coord4 positions10[24];         // +0x1980
    EffectUV texCoords10[24];       // +0x1b00
    uint8_t list10[16];            // +0x1bc0
    Coord4 positions11[24];         // +0x1bd0
    EffectUV texCoords11[24];       // +0x1d50
    uint8_t list11[16];            // +0x1e10
    Coord4 positions12[29];         // +0x1e20
    EffectUV texCoords12[29];       // +0x1ff0
    uint8_t padding12[8];           // +0x20d8 (each list starts on 16 bytes)
    uint8_t list12[16];            // +0x20e0
    Coord4 positions13[28];         // +0x20f0
    EffectUV texCoords13[28];       // +0x22b0
    uint8_t list13[16];            // +0x2390
    Coord4 positions14[30];         // +0x23a0
    EffectUV texCoords14[30];       // +0x2580
    uint8_t list14[16];            // +0x2670
    Coord4 positions15[24];         // +0x2680
    EffectUV texCoords15[24];       // +0x2800
    uint8_t list15[16];            // +0x28c0
    Coord4 positions17[24];         // +0x28d0
    EffectUV texCoords17[24];       // +0x2a50
    Coord4 positions18[24];         // +0x2b10
    EffectUV texCoords18[24];       // +0x2c90
    Coord4 positions20[20];         // +0x2d50
    EffectUV texCoords20[20];       // +0x2e90
    Coord4 positions21[18];         // +0x2f30
    EffectUV texCoords21[18];       // +0x3050
    DebrisKind kinds[RDebris::kKinds];  // +0x30e0
};
static_assert(sizeof(DebrisTables) == 0x32ac, "the debris tables are 0x32ac bytes");
static_assert(offsetof(DebrisTables, vectorsB) == 0x1e0 && offsetof(DebrisTables, scaledB) == 0x3c0 &&
              offsetof(DebrisTables, scaledA) == 0xe70 && offsetof(DebrisTables, list12) == 0x20e0 &&
              offsetof(DebrisTables, kinds) == 0x30e0, "DebrisTables layout");
constexpr int kDebrisScaledB = 22;      // the kind whose strip is scaledB

extern RDebris *Debris;   // RenderManagers::InitDebris's instance
extern DebrisTables DebrisStrips;
extern uint8_t DebrisEmptyLists[2][16];     // the empty lists most kinds point at
extern uint32_t DebrisColours[RDebrisType::kMaxPieces];     // the pieces' colours as drawn
extern const MATRIX4 *ViewMatrixPointer;

#endif // DRIVING_GAME_DEBRIS_H_
