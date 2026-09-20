#ifndef COMMON_GFX_NV2APIXELSHADER_H_
#define COMMON_GFX_NV2APIXELSHADER_H_

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// NV2A register combiner programs, translated to HLSL.
//
// An Xbox pixel shader is not a program but a set of register values: the 240-byte D3DPIXELSHADERDEF the game
// hands to D3DDevice_CreatePixelShader, which the console's D3D8 writes straight into the NV2A's combiner
// registers when the shader is set (D3DDevice_SetPixelShader, driving 0x0016af60, copies it word for word
// into the push buffer). Eight general combiner stages, each computing A*B and C*D and their sum or a mux on
// the RGB and alpha halves of the registers, and a final combiner that blends A*B + (1-A)*C + D. This file
// turns one of those into a ps_2_x program that does the same arithmetic. The layout and the meaning of every
// field are the XDK's (d3d8types.h, the PS_* enumerations); the semantics of the mappings and the register
// initialisation were checked against Cxbx-Reloaded's translator, which has been tested against the XDK
// samples, and against what the games here actually use (tools/nv2a_psh_dump.py reads a dump of them).
//
// What the draw-time code has to do for a translated program is the other half of the contract: bind the
// textures the program samples to samplers 0..3 by Xbox stage, and hand it the constant block that
// Nv2aPixelShader_BuildConstants fills from the definition, the D3D pixel shader constants, the fog colour
// and the per-stage bump-environment and colour-sign state.
// ---------------------------------------------------------------------------------------------------------------

struct Nv2aPixelShaderInfo {
    uint32_t stageCount;
    uint8_t textureMode[4];   // PS_TEXTUREMODES_* per stage
    bool samplesStage[4];     // the stage reads its texture, so a texture must be bound to that sampler
    bool readsFog;            // the program reads the fog register anywhere
    bool fogByHost;           // fog is the host's to apply after the program (default final combiner, or the
                              // standard fog blend in the final combiner, which the program then leaves out)
    uint32_t unsupportedMode; // a texture mode this translator does not do, or 0
};

// The constant block, as float4 registers c0.. in the program:
enum {
    NV2A_PS_K_C0 = 0,          // 8: each stage's C0
    NV2A_PS_K_C1 = 8,          // 8: each stage's C1
    NV2A_PS_K_FINAL_C0 = 16,   // the final combiner's C0 and C1
    NV2A_PS_K_FINAL_C1 = 17,
    NV2A_PS_K_FOG = 18,        // .rgb the fog colour
    NV2A_PS_K_BUMPMAT = 19,    // 4: per stage, (m00, m01, m10, m11)
    NV2A_PS_K_BUMPLUM = 23,    // 2: luminance (scale, offset) for stages 1 and 2 in .xy/.zw, stage 3 in the next .xy
    NV2A_PS_K_SIGN = 25,       // 4: per stage, (r, g, b, a) = 1 where the texture channel is signed
    NV2A_PS_K_COUNT = 29
};

// Translates a definition to HLSL. False if the program could not be expressed (the info says why).
bool Nv2aPixelShader_Translate(const uint32_t def[60], char *hlsl, size_t hlslSize, Nv2aPixelShaderInfo *info);

// Fills the constant block for a draw. d3dConstants are the 16 float4s D3DDevice_SetPixelShaderConstant wrote;
// bumpEnv is X_D3DTSS_BUMPENVMAT00..BUMPENVLOFFSET (six words) per stage, as float bit patterns; colourSign is
// X_D3DTSS_COLORSIGN per stage.
void Nv2aPixelShader_BuildConstants(const uint32_t def[60], const float d3dConstants[16][4], uint32_t fogColour,
                                    const uint32_t bumpEnv[4][6], const uint32_t colourSign[4],
                                    float out[NV2A_PS_K_COUNT][4]);

#endif // COMMON_GFX_NV2APIXELSHADER_H_
