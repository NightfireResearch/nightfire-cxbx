#ifndef COMMON_GFX_NV2AVERTEXSHADER_H_
#define COMMON_GFX_NV2AVERTEXSHADER_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NV2A vertex programs, translated to vs_2_0 bytecode.
//
// The game's vertex shaders are NV2A vertex programs (the XDK's D3DVS binary: an 8-byte header {0x78, 0x20,
// instruction count, 0} then 16 bytes per instruction, three meaningful words and one unused), decoded with the
// field layout documented by Erik Abair's nv2a-vsh (Unlicense). vs_2_0 rather than 3_0 because D3D9 refuses to
// pair a shader-model-3 vertex shader with the fixed-function pixel pipeline, which draws whatever the pixel
// shader translator cannot.
//
// Three things differ from the hardware. First, an NV2A program ends by doing its own perspective divide and
// viewport transform (MUL oPos.xyz, R12, c[58] + RCC R1.x, R12.w; MAD oPos.xyz, R12, R1.x, c[59]), with c58/
// c59 written by the Xbox D3D runtime from the viewport. D3D9 wants clip-space positions, so c58 is pinned to
// (1,1,1,1) and c59 to zero - making the program's own epilogue produce plain NDC - and the translated shader
// multiplies xy back up by w on output. Depth is taken from w rather than the program's z: the game W-buffers
// (ZEnable(2)) with a projection scaled so w is the 24-bit depth, and clips w against SetDepthClipPlanes, so
// the emitted z is (w - near) / (far - near), which reproduces both the clipping and the depth order. Second,
// Xbox's NORMPACKED3 vertex type (11:11:10 signed normalised) has no D3D9 declaration type; it is declared as
// UBYTE4 and unpacked with float arithmetic in the prologue. Third, oFog is a fog coordinate on the NV2A and a
// factor on D3D9, so the render-state fog table function is applied in the shader epilogue.
//
// The constants the backend adds after the game's 192:
//   c200 .xy D3D9's half-pixel offset in NDC
//   c201 .xy the W-buffer depth mapping; .z 1 to use the shader's own z instead; .w 1 to reverse it
//   c202 fog .x start, .y end, .z density, .w table mode (0 none, 1 exp, 2 exp2, 3 linear)
//   c203 1/size for linear textures on coordinate sets 0 (.xy) and 1 (.zw), else 1; c204 the same for 2 and 3
// ---------------------------------------------------------------------------------------------------------------

enum { VSH_MAC_NOP, VSH_MAC_MOV, VSH_MAC_MUL, VSH_MAC_ADD, VSH_MAC_MAD, VSH_MAC_DP3, VSH_MAC_DPH, VSH_MAC_DP4,
       VSH_MAC_DST, VSH_MAC_MIN, VSH_MAC_MAX, VSH_MAC_SLT, VSH_MAC_SGE, VSH_MAC_ARL };
enum { VSH_ILU_NOP, VSH_ILU_MOV, VSH_ILU_RCP, VSH_ILU_RCC, VSH_ILU_RSQ, VSH_ILU_EXP, VSH_ILU_LOG, VSH_ILU_LIT };
enum { VSH_PARAM_R = 1, VSH_PARAM_V = 2, VSH_PARAM_C = 3 };

struct VshSource { int mux, tempReg, swz[4]; bool negate; };
struct VshDecoded {
    int mac, ilu, input, constIndex;
    bool a0x, final, outIsOutput;
    int outMux, outAddress, outOMask, outIluMask, outTempReg, outMacMask;
    VshSource a, b, c;
};

// One instruction's three words.
void Nv2aVertexShader_Decode(const uint32_t *words, VshDecoded *d);

struct Nv2aVertexShaderInfo {
    int instructionCount;
    uint32_t inputsUsed;       // bit r: the program reads v<r>
    uint32_t outputsWritten;   // bit a: the program writes output address a (0 oPos, 3 oD0, 5 oFog, 9 oT0...)
};

// Translates one XDK vertex shader function to vs_2_0 tokens. normPackedInputs: bit r set where register r
// is NORMPACKED3 (declared as UBYTE4); inputComponents[r]: the components the declaration gives register r,
// 0 where it gives none. False, with the reason in error, if the program cannot be translated.
bool Nv2aVertexShader_Translate(const uint8_t *function, uint32_t normPackedInputs, const uint8_t inputComponents[16],
                                std::vector<uint32_t> *bytecode, Nv2aVertexShaderInfo *info, char *error, size_t errorSize);

// The same translation as a SPIR-V module (shaderSpirv.h), with options.inputKind saying how each input arrives.
struct ShaderSpirvOptions;
struct ShaderSpirvInfo;
bool Nv2aVertexShader_TranslateSpirv(const uint8_t *function, uint32_t normPackedInputs, const uint8_t inputComponents[16],
                                     const ShaderSpirvOptions &options, std::vector<uint32_t> *words,
                                     Nv2aVertexShaderInfo *info, ShaderSpirvInfo *spirvInfo, char *error, size_t errorSize);

#endif // COMMON_GFX_NV2AVERTEXSHADER_H_
