#ifndef COMMON_GFX_FIXEDFUNCTION_H_
#define COMMON_GFX_FIXEDFUNCTION_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>

struct ShaderSpirvInfo;

// ---------------------------------------------------------------------------------------------------------------
// What D3D9's fixed-function pipeline did for the D3D9 backend, as programs for a backend that has none (the SDL GPU
// backend, sdlGpuBackend.cpp): ShaderBuilder programs written as SPIR-V with shaderSpirv.h's interface.
//
// - The pretransformed vertices (D3DFVF_XYZRHW): the immediate path, the screen-space quads, the movie frames.
//   Inputs: register 0 x, y in the target's pixels, z, rhw (float4); register 3 a D3DCOLOR; register 9 a texture
//   coordinate (float4). c200.zw is 2 / the target's size. D3D10's pixel centres, so no half-pixel offset.
// - The texture stages: COLOROP, ALPHAOP and their arguments, per stage, with D3D9's meanings for the numbers (the
//   D3D9 backend handed the Xbox's values to D3D9 as they were, so this reads them as D3D9 read them).
// - A full-screen pass's vertices (FXAA): register 0 a clip-space position, register 9 its texture coordinate.
// ---------------------------------------------------------------------------------------------------------------

struct FixedStage {
    uint8_t colourOp, colourArg[3];   // D3DTOP_*, D3DTA_* (0 for COLORARG0/ALPHAARG0 when unused)
    uint8_t alphaOp, alphaArg[3];
    uint8_t texCoord;                 // the texture coordinate set the stage reads
    uint8_t projected;                // 1: the coordinate is divided by its q (D3DTTFF_PROJECTED)
};

struct FixedPixelKey {
    uint8_t stageCount;   // the host stages in use, from 0; host stage s samples sampler s
    uint8_t pad[3];
    FixedStage stages[4];
};

// The texture stages as a pixel program, with the host's end (fog, alpha test) after it.
bool FixedFunction_PixelProgram(const FixedPixelKey &key, std::vector<uint32_t> *words, ShaderSpirvInfo *info,
                                char *error, size_t errorSize);

// The pretransformed vertex program; inputKind gets the kinds its inputs need (shaderSpirv.h).
bool FixedFunction_PretransformedProgram(std::vector<uint32_t> *words, uint8_t inputKind[16], char *error, size_t errorSize);

// A full-screen pass's vertex program.
bool FixedFunction_ScreenProgram(std::vector<uint32_t> *words, uint8_t inputKind[16], char *error, size_t errorSize);

#endif // COMMON_GFX_FIXEDFUNCTION_H_
