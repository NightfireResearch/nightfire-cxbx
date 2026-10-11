#ifndef DRIVING_GAME_DEBRIS_H_
#define DRIVING_GAME_DEBRIS_H_

// ---------------------------------------------------------------------------------------------------------------
// RDebris (0x90 bytes, "RDebris", one: RenderManagers::InitDebris's): thrown pieces of 23 kinds, each kind an
// RDebrisType (0x1ad0 bytes, "RDebrisType") of up to 128 pieces that fly from where they were thrown under gravity,
// spinning about an axis of their own, and fade until their lifetime is over. Each kind draws its pieces as one
// textured triangle strip each (DebrisKinds, at 0x001c8c80: the strip, its texture coordinates and texture). The
// kind drawn with the texture "shel" (kShellCasings) is drawn in the view's frame by DrawShellCasings (for the
// player's weapon), the others by Draw. See Debris.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord4
#include "../eagl/GeoPrimState.h"
#include "../render/Materials.h"        // UVolatileMaterial

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
    uint32_t kind;                      // +0x1a0c its row of DebrisKinds
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

#define Debris (*(RDebris **)0x00202a80)        // RenderManagers::InitDebris's instance
extern uint32_t DebrisColours[RDebrisType::kMaxPieces];     // the pieces' colours as drawn

#endif // DRIVING_GAME_DEBRIS_H_
