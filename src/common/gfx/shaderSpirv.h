#ifndef COMMON_GFX_SHADERSPIRV_H_
#define COMMON_GFX_SHADERSPIRV_H_

#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// SPIR-V modules from ShaderBuilder programs, for SDL's GPU API (ShaderBuilder::AssembleSpirv, shaderSpirv.cpp).
//
// SPIR-V is SSA, so the lowering is direct: each node of the program is one instruction, with none of the D3D9
// lowering's registers, read ports or phases. Nothing is reassociated or contracted. Every add, multiply and divide
// is decorated NoContraction, and a product feeds a sum as one Fma exactly where Assemble writes a mad
// (ShaderBuilder::Fuse), so the driver rounds where the vs_2_0/ps_2_0 programs round; a dp is its products summed
// from the first. Min, max and the comparisons are selects on ordered comparisons, as the builder folds literals.
//
// Every program has the same interface, so that any vertex program goes with any pixel program:
// - vertex inputs at the location of their register;
// - vertex outputs and pixel inputs by location: 0 the diffuse colour, 1 the specular, 2 to 5 texture coordinates
//   0 to 3, 6 the fog factor. A vertex program writes all seven, the colours clamped to 0..1 as D3D9 clamps oD0
//   and oD1, and 1 (no fog) where the program writes no fog;
// - the constants as one uniform block of float4s, a vertex program's at set 1, binding 0 (kSpirvVertexConstants
//   of them: the game's 192 and the host's c200-c204, nv2aVertexShader.h) and a pixel program's at set 3, binding 0
//   (kSpirvPixelConstants); samplers at set 2, binding = sampler number. These are SDL's sets for SPIR-V.
// ---------------------------------------------------------------------------------------------------------------

enum { kSpirvVertexConstants = 208, kSpirvPixelConstants = 32 };

// The pixel constants the host's end of a program reads (ShaderSpirvOptions::hostPixelEnd):
//   kSpirvFogColour   .rgb the fog colour
//   kSpirvPixelState  .x 1 to blend in the fog, .y the alpha test's function (D3DCMP_*: 8, always, for none),
//                     .z its reference (0..255), .w 1 to write 1 everywhere (a visibility test's mask)
enum { kSpirvFogColour = 30, kSpirvPixelState = 31 };

// How a vertex input register arrives from SDL's vertex element format.
enum SpirvInputKind {
    kSpirvFloat,      // a float format, or a normalised one
    kSpirvSigned,     // SHORT2/SHORT4: integers, read as floats
    kSpirvUnsigned,   // UBYTE4: integers, read as floats
    kSpirvColour,     // UBYTE4_NORM holding a D3DCOLOR: B, G, R, A in memory, read as R, G, B, A
};

struct ShaderSpirvOptions {
    uint8_t inputKind[16];   // vertex programs: SpirvInputKind per input register
    // Vertex programs: each point drawn as two triangles, a point sprite - its attributes an instance's, its corner
    // the vertex index (0 to 5), its size the program's point size in pixels (c200.zw: 2 / the target's size), and
    // every texture coordinate the sprite's (0, 0) to (1, 1).
    bool pointSprite;
    // Pixel programs: D3D9's fog blend and alpha test after the program, and the visibility mask (kSpirvPixelState).
    bool hostPixelEnd;
};

struct ShaderSpirvInfo {
    uint32_t samplers;       // bit s: the program samples sampler s
    uint32_t cubeSamplers;   // bit s: as a cube map
    uint32_t inputs;         // bit r: a vertex program reads input register r
};

#endif // COMMON_GFX_SHADERSPIRV_H_
