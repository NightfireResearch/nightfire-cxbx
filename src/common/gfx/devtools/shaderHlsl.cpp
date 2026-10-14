#include "shaderHlsl.h"

#include "../nv2aPixelShader.h"
#include "../nv2aVertexShader.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The HLSL the translators produced before they wrote bytecode themselves, kept as it was for the shader check
// (shaderCheck.cpp): compiled with D3DCompile, it is the reference the bytecode is compared against.

namespace {
struct HlslWriter {
    char *buf; size_t size, len; bool overflow;
    void printf(const char *fmt, ...) {
        va_list ap; va_start(ap, fmt);
        int n = vsnprintf(buf + len, size > len ? size - len : 0, fmt, ap);
        va_end(ap);
        if (n < 0 || (size_t)n >= size - len) { overflow = true; len = size - 1; }
        else len += (size_t)n;
    }
};
}

static const char *VshOutputName(int address) {
    switch (address) {
        case 0: return "oPos"; case 3: return "oD0"; case 4: return "oD1"; case 5: return "oFog"; case 6: return "oPts";
        case 7: return "oB0"; case 8: return "oB1"; case 9: return "oT0"; case 10: return "oT1"; case 11: return "oT2"; case 12: return "oT3";
        default: return NULL;
    }
}

// Writes "R3.xyzz" / "-(c[a0 + 96].xyzw)" / "v3.zzzz" for one source operand.
static void EmitVshSource(HlslWriter *w, const VshDecoded *d, const VshSource *s, uint32_t *inputsUsed) {
    static const char swzName[4] = { 'x', 'y', 'z', 'w' };
    if (s->negate) w->printf("-(");
    switch (s->mux) {
        case VSH_PARAM_R: if (s->tempReg == 12) w->printf("oPos"); else w->printf("R%d", s->tempReg); break;
        case VSH_PARAM_V: w->printf("v%d", d->input); *inputsUsed |= 1u << d->input; break;
        case VSH_PARAM_C: if (d->a0x) w->printf("c[a0 + %d]", d->constIndex); else w->printf("c[%d]", d->constIndex); break;
        default: w->printf("float4(0,0,0,0)"); break;
    }
    w->printf(".%c%c%c%c", swzName[s->swz[0]], swzName[s->swz[1]], swzName[s->swz[2]], swzName[s->swz[3]]);
    if (s->negate) w->printf(")");
}

static void VshMaskString(int vshMask, char out[5]) { // NV2A mask bits: 8 = x, 4 = y, 2 = z, 1 = w
    int n = 0;
    if (vshMask & 8) out[n++] = 'x';
    if (vshMask & 4) out[n++] = 'y';
    if (vshMask & 2) out[n++] = 'z';
    if (vshMask & 1) out[n++] = 'w';
    out[n] = 0;
}

// Translates one XDK vertex shader function blob to HLSL. Returns false if something isn't handled.
// inputsUsed/outputsWritten come back as register bitmasks (v0..v15 / the oX output addresses).
bool ShaderHlsl_Vertex(const uint8_t *function, const uint32_t *normPackedInputs,
                               const uint8_t *inputComponents, char *out, size_t outSize,
                               uint32_t *inputsUsed, uint32_t *outputsWritten, int *instructionCountOut) {
    if (function[0] != 0x78 || function[1] != 0x20)
        return false;
    int count = function[2];
    *instructionCountOut = count;
    const uint32_t *words = (const uint32_t*)(function + 8);

    // Body first (so we know which inputs/outputs the header needs), into the second half of the buffer.
    char *body = out + outSize / 2;
    HlslWriter w = { body, outSize / 2, 0, false };
    *inputsUsed = 0; *outputsWritten = 1;
    for (int i = 0; i < count; i++) {
        VshDecoded d;
        Nv2aVertexShader_Decode(words + i * 4, &d);
        if (d.mac == VSH_MAC_NOP && d.ilu == VSH_ILU_NOP)
            continue;
        w.printf("    { // %d\n", i);
        w.printf("        float4 A = "); EmitVshSource(&w, &d, &d.a, inputsUsed); w.printf(";\n");
        w.printf("        float4 B = "); EmitVshSource(&w, &d, &d.b, inputsUsed); w.printf(";\n");
        w.printf("        float4 C = "); EmitVshSource(&w, &d, &d.c, inputsUsed); w.printf(";\n");
        // Both units read their operands before either writes (they execute in parallel on the hardware).
        if (d.mac != VSH_MAC_NOP) {
            w.printf("        float4 M = ");
            switch (d.mac) {
                case VSH_MAC_MOV: w.printf("A"); break;
                case VSH_MAC_MUL: w.printf("A * B"); break;
                case VSH_MAC_ADD: w.printf("A + C"); break;
                case VSH_MAC_MAD: w.printf("A * B + C"); break;
                case VSH_MAC_DP3: w.printf("dot(A.xyz, B.xyz).xxxx"); break;
                case VSH_MAC_DPH: w.printf("(dot(A.xyz, B.xyz) + B.w).xxxx"); break;
                case VSH_MAC_DP4: w.printf("dot(A, B).xxxx"); break;
                case VSH_MAC_DST: w.printf("float4(1.0, A.y * B.y, A.z, B.w)"); break;
                case VSH_MAC_MIN: w.printf("min(A, B)"); break;
                case VSH_MAC_MAX: w.printf("max(A, B)"); break;
                case VSH_MAC_SLT: w.printf("(float4)(A < B)"); break;
                case VSH_MAC_SGE: w.printf("(float4)(A >= B)"); break;
                case VSH_MAC_ARL: w.printf("A"); break;
                default: return false;
            }
            w.printf(";\n");
        }
        if (d.ilu != VSH_ILU_NOP) {
            w.printf("        float4 I = ");
            switch (d.ilu) {
                case VSH_ILU_MOV: w.printf("C"); break;
                case VSH_ILU_RCP: w.printf("(1.0 / C.x).xxxx"); break;
                case VSH_ILU_RCC: w.printf("((1.0 / C.x) >= 0 ? clamp(1.0 / C.x, 5.42101e-20, 1.884467e19) : clamp(1.0 / C.x, -1.884467e19, -5.42101e-20)).xxxx"); break;
                case VSH_ILU_RSQ: w.printf("(1.0 / sqrt(abs(C.x))).xxxx"); break;
                case VSH_ILU_EXP: w.printf("float4(exp2(floor(C.x)), frac(C.x), exp2(C.x), 1.0)"); break;
                case VSH_ILU_LOG: w.printf("float4(floor(log2(abs(C.x))), abs(C.x) / exp2(floor(log2(abs(C.x)))), log2(abs(C.x)), 1.0)"); break;
                case VSH_ILU_LIT: w.printf("lit(C.x, C.y, C.w)"); break;
                default: return false;
            }
            w.printf(";\n");
        }
        char mask[5];
        if (d.mac == VSH_MAC_ARL) {
            w.printf("        a0 = (int)floor(M.x);\n");
        } else if (d.mac != VSH_MAC_NOP) {
            if (d.outMacMask) { VshMaskString(d.outMacMask, mask); w.printf("        R%d.%s = M.%s;\n", d.outTempReg, mask, mask); }
            if (d.outOMask && d.outMux == 0) {
                const char *name = d.outIsOutput ? VshOutputName(d.outAddress) : NULL;
                if (name == NULL) return false;
                VshMaskString(d.outOMask, mask); w.printf("        %s.%s = M.%s;\n", name, mask, mask);
                *outputsWritten |= 1u << d.outAddress;
            }
        }
        if (d.ilu != VSH_ILU_NOP) {
            if (d.outIluMask) {
                int reg = (d.mac != VSH_MAC_NOP) ? 1 : d.outTempReg; // paired ILU results land in R1
                VshMaskString(d.outIluMask, mask); w.printf("        R%d.%s = I.%s;\n", reg, mask, mask);
            }
            if (d.outOMask && d.outMux == 1) {
                const char *name = d.outIsOutput ? VshOutputName(d.outAddress) : NULL;
                if (name == NULL) return false;
                VshMaskString(d.outOMask, mask); w.printf("        %s.%s = I.%s;\n", name, mask, mask);
                *outputsWritten |= 1u << d.outAddress;
            }
        }
        w.printf("    }\n");
        if (d.final) break;
    }
    if (w.overflow) return false;

    HlslWriter h = { out, outSize / 2, 0, false };
    h.printf("float4 c[192] : register(c0);\n");
    h.printf("float4 hostAdjust : register(c200); // .xy = D3D9's half-pixel offset in NDC\n");
    h.printf("float4 depthClip : register(c201);  // .xy: the W-buffer depth mapping; .z: use the shader's own z instead; .w: reverse it\n");
    h.printf("float4 fogParams : register(c202);  // .x = start, .y = end, .z = density, .w = table mode (0 none, 1 exp, 2 exp2, 3 linear)\n");
    h.printf("float4 texScale01 : register(c203); // 1/size for linear textures on coordinate sets 0 (.xy) and 1 (.zw), else 1\n");
    h.printf("float4 texScale23 : register(c204); // same for sets 2 and 3\n");
    h.printf("struct VS_IN {\n");
    for (int r = 0; r < 16; r++)
        if ((*inputsUsed & (1u << r)) && inputComponents[r] != 0)
            h.printf("    float4 v%d : %s%d;\n", r, r == 0 ? "POSITION" : "TEXCOORD", r == 0 ? 0 : r);
    h.printf("};\nstruct VS_OUT {\n    float4 oPos : POSITION;\n    float4 oD0 : COLOR0;\n    float4 oD1 : COLOR1;\n");
    if (*outputsWritten & (1u << 5)) h.printf("    float oFog : FOG;\n");
    if (*outputsWritten & (1u << 6)) h.printf("    float oPts : PSIZE;\n");
    h.printf("    float4 oT0 : TEXCOORD0;\n    float4 oT1 : TEXCOORD1;\n    float4 oT2 : TEXCOORD2;\n    float4 oT3 : TEXCOORD3;\n};\n");
    h.printf("VS_OUT main(VS_IN vin) {\n");
    for (int r = 0; r < 16; r++) {
        if (!(*inputsUsed & (1u << r))) continue;
        if (inputComponents[r] == 0) {
            // Not in the declaration: the NV2A reads the register's default, (0, 0, 0, 1). D3D9 leaves an input
            // with no element undefined, so it is not an input here at all. (Shader 129, the particles, takes
            // its positions' w from v3.w, which its declaration does not supply.)
            h.printf("    float4 v%d = float4(0, 0, 0, 1);\n", r);
        } else if (*normPackedInputs & (1u << r)) {
            // 11:11:10 signed normalised, arriving as four raw bytes (UBYTE4): rebuild the fields with exact float maths.
            h.printf("    float4 v%d;\n    {\n        float4 b = vin.v%d;\n        float lo = b.x + 256.0 * b.y;\n", r, r);
            h.printf("        float x = fmod(lo, 2048.0);\n        float y = floor(lo / 2048.0) + 32.0 * fmod(b.z, 64.0);\n        float z = floor(b.z / 64.0) + 4.0 * b.w;\n");
            h.printf("        x = (x >= 1024.0) ? x - 2048.0 : x;\n        y = (y >= 1024.0) ? y - 2048.0 : y;\n        z = (z >= 512.0) ? z - 1024.0 : z;\n");
            h.printf("        v%d = float4(x / 1023.0, y / 1023.0, z / 511.0, 1.0);\n    }\n", r);
        } else if (inputComponents[r] < 4) {
            // The declaration gave fewer components than the host type reads: put the NV2A's
            // defaults back, so a neighbouring vertex's bytes cannot arrive as this one's .y.
            static const char *shapes[3] = { "vin.v%d.x, 0, 0, 1", "vin.v%d.xy, 0, 1", "vin.v%d.xyz, 1" };
            char shape[48];
            snprintf(shape, sizeof(shape), shapes[inputComponents[r] - 1], r);
            h.printf("    float4 v%d = float4(%s);\n", r, shape);
        } else {
            h.printf("    float4 v%d = vin.v%d;\n", r, r);
        }
    }
    h.printf("    float4 R0 = 0, R1 = 0, R2 = 0, R3 = 0, R4 = 0, R5 = 0, R6 = 0, R7 = 0, R8 = 0, R9 = 0, R10 = 0, R11 = 0;\n");
    // Output registers start as (0,0,0,1) on the hardware; the w matters because texture stages divide by q.
    h.printf("    float4 oPos = 0, oD0 = 0, oD1 = 0, oFog = 0, oPts = 0, oB0 = 0, oB1 = 0;\n");
    h.printf("    float4 oT0 = float4(0,0,0,1), oT1 = float4(0,0,0,1), oT2 = float4(0,0,0,1), oT3 = float4(0,0,0,1);\n");
    h.printf("    int a0 = 0;\n");
    if (h.overflow || h.len + w.len + 512 > outSize / 2) return false;
    memmove(out + h.len, body, w.len);
    h.len += w.len;
    h.printf("    VS_OUT o;\n");
    // Position: xy back to clip space; z from w (the W-buffer depth) in the a + b/w form the rasteriser
    // interpolates exactly, mapped so the depth-clip planes land on D3D9's 0..1 z range (reversed when the
    // float depth buffer is in use) - which both clips where the NV2A clipped and keeps the depth ordering.
    h.printf("    float zFromW = depthClip.x * (oPos.w - depthClip.y);\n");
    h.printf("    float zFromShader = lerp(oPos.z, 1.0 - oPos.z, depthClip.w) * oPos.w;\n");
    h.printf("    o.oPos = float4((oPos.xy + hostAdjust.xy) * oPos.w, lerp(zFromW, zFromShader, depthClip.z), oPos.w);\n");
    h.printf("    o.oD0 = oD0;\n    o.oD1 = oD1;\n");
    // Fog: on the Xbox the shader's oFog is the fog coordinate (the game emits its normalised near..far
    // distance) and the fog table mode turns it into the factor; D3D9 takes the factor straight from the
    // shader, so apply the table function here. Written as arithmetic selects, since vs_2_0 can't branch.
    if (*outputsWritten & (1u << 5)) {
        h.printf("    {\n        float d = oFog.x;\n");
        h.printf("        float fLinear = (fogParams.y - d) / max(fogParams.y - fogParams.x, 1e-6);\n");
        h.printf("        float fExp = exp(-d * fogParams.z);\n");
        h.printf("        float fExp2 = exp(-(d * d * fogParams.z * fogParams.z));\n");
        h.printf("        float f = (fogParams.w == 1.0) ? fExp : (fogParams.w == 2.0) ? fExp2 : (fogParams.w == 3.0) ? fLinear : d;\n");
        h.printf("        o.oFog = saturate(f);\n    }\n");
    }
    if (*outputsWritten & (1u << 6)) h.printf("    o.oPts = oPts.x;\n");
    h.printf("    o.oT0 = oT0 * float4(texScale01.xy, 1, 1);\n    o.oT1 = oT1 * float4(texScale01.zw, 1, 1);\n");
    h.printf("    o.oT2 = oT2 * float4(texScale23.xy, 1, 1);\n    o.oT3 = oT3 * float4(texScale23.zw, 1, 1);\n    return o;\n}\n");
    return !h.overflow;
}

// The definition's words, by index (the XDK's D3DPIXELSHADERDEF):
enum {
    DEF_ALPHA_INPUTS = 0,      // [8]
    DEF_FINAL_ABCD = 8,
    DEF_FINAL_EFG = 9,
    DEF_C0 = 10,               // [8]
    DEF_C1 = 18,               // [8]
    DEF_ALPHA_OUTPUTS = 26,    // [8]
    DEF_RGB_INPUTS = 34,       // [8]
    DEF_COMPARE_MODE = 42,
    DEF_FINAL_C0 = 43,
    DEF_FINAL_C1 = 44,
    DEF_RGB_OUTPUTS = 45,      // [8]
    DEF_COMBINER_COUNT = 53,
    DEF_TEXTURE_MODES = 54,
    DEF_DOT_MAPPING = 55,
    DEF_INPUT_TEXTURE = 56,
    DEF_C0_MAPPING = 57,
    DEF_C1_MAPPING = 58,
    DEF_FINAL_CONSTANTS = 59,
};

// PS_REGISTER_*: the low nibble of an input byte.
enum { REG_ZERO = 0, REG_C0 = 1, REG_C1 = 2, REG_FOG = 3, REG_V0 = 4, REG_V1 = 5, REG_T0 = 8, REG_T1 = 9,
       REG_T2 = 10, REG_T3 = 11, REG_R0 = 12, REG_R1 = 13, REG_V1R0_SUM = 14, REG_EF_PROD = 15 };

// PS_TEXTUREMODES_*.
enum { TEX_NONE = 0, TEX_PROJECT2D = 1, TEX_PROJECT3D = 2, TEX_CUBEMAP = 3, TEX_PASSTHRU = 4, TEX_CLIPPLANE = 5,
       TEX_BUMPENVMAP = 6, TEX_BUMPENVMAP_LUM = 7, TEX_DPNDNT_AR = 15, TEX_DPNDNT_GB = 16 };

namespace {
struct Writer {
    char *buf; size_t size, len; bool overflow;
    void printf(const char *fmt, ...) {
        va_list ap; va_start(ap, fmt);
        int n = vsnprintf(buf + len, size > len ? size - len : 0, fmt, ap);
        va_end(ap);
        if (n < 0 || (size_t)n >= size - len) { overflow = true; len = size - 1; }
        else len += (size_t)n;
    }
};
}

// The name of a register as the program sees it. Stage constants are indexed by stage; in the final
// combiner they are the final combiner's own pair.
static const char *RegisterName(uint32_t reg, int stage, bool finalCombiner, char *scratch, size_t scratchSize) {
    switch (reg) {
        case REG_ZERO: return "float4(0, 0, 0, 0)";
        case REG_C0: snprintf(scratch, scratchSize, "k[%d]", finalCombiner ? NV2A_PS_K_FINAL_C0 : NV2A_PS_K_C0 + stage); return scratch;
        case REG_C1: snprintf(scratch, scratchSize, "k[%d]", finalCombiner ? NV2A_PS_K_FINAL_C1 : NV2A_PS_K_C1 + stage); return scratch;
        case REG_FOG: return "fog";
        case REG_V0: return "v0";
        case REG_V1: return "v1";
        case REG_T0: return "t0";
        case REG_T1: return "t1";
        case REG_T2: return "t2";
        case REG_T3: return "t3";
        case REG_R0: return "r0";
        case REG_R1: return "r1";
        case REG_V1R0_SUM: return finalCombiner ? "v1r0sum" : "float4(0, 0, 0, 0)";
        case REG_EF_PROD: return finalCombiner ? "efprod" : "float4(0, 0, 0, 0)";
        default: return "float4(0, 0, 0, 0)";
    }
}

// One input byte: register, channel and input mapping, as an expression. RGB inputs are float3 (the register's
// rgb, or its alpha replicated); alpha inputs are float (the register's alpha, or its blue).
static void EmitInput(Writer *w, uint32_t byte, bool alphaHalf, int stage, bool finalCombiner) {
    char scratch[32];
    const char *reg = RegisterName(byte & 0xF, stage, finalCombiner, scratch, sizeof(scratch));
    bool channelBit = (byte & 0x10) != 0;
    const char *swizzle = alphaHalf ? (channelBit ? ".a" : ".b") : (channelBit ? ".aaa" : ".rgb");
    char x[64];
    snprintf(x, sizeof(x), "%s%s", reg, swizzle);
    switch ((byte >> 5) & 7) {
        case 0: w->printf("max(%s, 0)", x); break;              // PS_INPUTMAPPING_UNSIGNED_IDENTITY
        case 1: w->printf("(1 - saturate(%s))", x); break;      // UNSIGNED_INVERT
        case 2: w->printf("(2 * max(%s, 0) - 1)", x); break;    // EXPAND_NORMAL
        case 3: w->printf("(1 - 2 * max(%s, 0))", x); break;    // EXPAND_NEGATE
        case 4: w->printf("(max(%s, 0) - 0.5)", x); break;      // HALFBIAS_NORMAL
        case 5: w->printf("(0.5 - max(%s, 0))", x); break;      // HALFBIAS_NEGATE
        case 6: w->printf("%s", x); break;                      // SIGNED_IDENTITY
        default: w->printf("(-%s)", x); break;                  // SIGNED_NEGATE
    }
}

// The output scale and bias of a stage (PS_COMBINEROUTPUT_* op field), applied to a value, then the clamp
// every combiner output gets.
static void EmitOutputOp(Writer *w, uint32_t op, const char *value) {
    switch (op) {
        case 0: w->printf("clamp(%s, -1, 1)", value); break;
        case 1: w->printf("clamp(%s - 0.5, -1, 1)", value); break;          // BIAS
        case 2: w->printf("clamp(%s * 2, -1, 1)", value); break;            // SHIFTLEFT_1
        case 3: w->printf("clamp((%s - 0.5) * 2, -1, 1)", value); break;    // SHIFTLEFT_1_BIAS
        case 4: w->printf("clamp(%s * 4, -1, 1)", value); break;            // SHIFTLEFT_2
        case 5: w->printf("clamp((%s - 0.5) * 4, -1, 1)", value); break;    // SHIFTLEFT_2_BIAS
        case 6: w->printf("clamp(%s * 0.5, -1, 1)", value); break;          // SHIFTRIGHT_1
        default: w->printf("clamp((%s - 0.5) * 0.5, -1, 1)", value); break; // SHIFTRIGHT_1_BIAS
    }
}

// Where a combiner output goes. Only the writable registers are; the rest (zero, the constants) discard.
static const char *DestinationName(uint32_t reg) {
    switch (reg) {
        case REG_V0: return "v0"; case REG_V1: return "v1";
        case REG_T0: return "t0"; case REG_T1: return "t1"; case REG_T2: return "t2"; case REG_T3: return "t3";
        case REG_R0: return "r0"; case REG_R1: return "r1";
        default: return NULL;
    }
}

static bool ProgramReadsRegister(const uint32_t def[60], uint32_t stages, uint32_t reg) {
    for (uint32_t i = 0; i < stages; i++) {
        uint32_t words[2] = { def[DEF_RGB_INPUTS + i], def[DEF_ALPHA_INPUTS + i] };
        for (int k = 0; k < 2; k++)
            for (int shift = 0; shift < 32; shift += 8)
                if (((words[k] >> shift) & 0xF) == reg) return true;
    }
    uint32_t abcd = def[DEF_FINAL_ABCD], efg = def[DEF_FINAL_EFG];
    for (int shift = 0; shift < 32; shift += 8)
        if (((abcd >> shift) & 0xF) == reg) return true;
    for (int shift = 8; shift < 32; shift += 8)
        if (((efg >> shift) & 0xF) == reg) return true;
    return false;
}

// The texture stage another stage's mode takes its input from (PS_INPUTTEXTURE).
static uint32_t InputStageFor(uint32_t inputTexture, int stage) {
    switch (stage) {
        case 1: return 0;
        case 2: return (inputTexture >> 16) & 3;
        case 3: return (inputTexture >> 20) & 3;
        default: return 0;
    }
}

// The combiner constants a program reads, declared one register each: "k[18]" in the generated body becomes
// k18, bound to c18. Declaring the whole k[] array instead reserves 29 of ps_2_x's 32 float constant registers
// and leaves three for the literals the compiler adds. Microsoft's compiler packs those four to a register
// and fits; Wine's (vkd3d) does not, spills into c32, and CreatePixelShader then refuses the program -
// "Shader using float constant 32 which is not supported" - so most surfaces drew with no shader at all.
// Declaring only what is read leaves the rest free for the literals. The registers are the same as before,
// so the upload of all NV2A_PS_K_COUNT constants from c0 is unchanged.
static bool DeclareConstants(const char *body, char *out, size_t outSize) {
    uint32_t used = 0;
    for (const char *p = strstr(body, "k["); p != NULL; p = strstr(p + 2, "k[")) {
        long n = strtol(p + 2, NULL, 10);
        if (n >= 0 && n < NV2A_PS_K_COUNT)
            used |= 1u << n;
    }

    Writer w = { out, outSize, 0, false };
    for (int n = 0; n < NV2A_PS_K_COUNT; n++)
        if (used & (1u << n))
            w.printf("float4 k%d : register(c%d);\n", n, n);
    const char *p = body;
    for (const char *at = strstr(p, "k["); at != NULL; at = strstr(p, "k[")) {
        char *end;
        long n = strtol(at + 2, &end, 10);
        w.printf("%.*sk%ld", (int)(at - p), p, n);
        p = (*end == ']') ? end + 1 : end;
    }
    w.printf("%s", p);
    return !w.overflow;
}

bool ShaderHlsl_Pixel(const uint32_t def[60], char *hlsl, size_t hlslSize, Nv2aPixelShaderInfo *info) {
    memset(info, 0, sizeof(*info));
    uint32_t stages = def[DEF_COMBINER_COUNT] & 0xF;
    if (stages > 8) stages = 8;
    info->stageCount = stages;
    bool muxOnMsb = (def[DEF_COMBINER_COUNT] & 0x100) != 0;
    (void)muxOnMsb;   // the LSB variant needs 8-bit arithmetic no game here uses; the MSB test stands in

    for (int s = 0; s < 4; s++) {
        info->textureMode[s] = (uint8_t)((def[DEF_TEXTURE_MODES] >> (5 * s)) & 0x1F);
        info->samplesStage[s] = false;
        switch (info->textureMode[s]) {
            case TEX_NONE: case TEX_PASSTHRU: break;
            case TEX_PROJECT2D: case TEX_PROJECT3D: case TEX_CUBEMAP: case TEX_BUMPENVMAP: case TEX_BUMPENVMAP_LUM:
            case TEX_DPNDNT_AR: case TEX_DPNDNT_GB:
                info->samplesStage[s] = true; break;
            default:
                info->samplesStage[s] = true;
                if (info->unsupportedMode == 0) info->unsupportedMode = info->textureMode[s];
                break;
        }
    }

    // Fog. The program cannot read the fog factor in ps_2_x, and the host applies fog after the program
    // anyway; so where the final combiner is the standard blend (A = fog.a, C = fog.rgb, B = the colour) or is
    // the default, the program leaves fog out and the host's fog does exactly that blend. Anywhere else the
    // fog register reads as its colour with a factor of one, which is what the first case reduces to.
    uint32_t finalAbcd = def[DEF_FINAL_ABCD], finalEfg = def[DEF_FINAL_EFG];
    bool defaultFinal = (finalAbcd == 0 && finalEfg == 0);
    info->readsFog = ProgramReadsRegister(def, stages, REG_FOG);
    bool finalUsesFogBlend = ((finalAbcd >> 24) & 0xF) == REG_FOG && ((finalAbcd >> 8) & 0xF) == REG_FOG;
    info->fogByHost = defaultFinal || finalUsesFogBlend;

    // The body first, with the constants as k[N]; DeclareConstants then writes it out with their declarations.
    char *body = (char *)malloc(hlslSize);
    if (body == NULL)
        return false;
    Writer w = { body, hlslSize, 0, false };
    w.printf("// NV2A register combiner program, %u stage%s\n", stages, stages == 1 ? "" : "s");
    w.printf("sampler s0 : register(s0);\nsampler s1 : register(s1);\nsampler s2 : register(s2);\nsampler s3 : register(s3);\n");
    w.printf("struct PS_IN { float4 v0 : COLOR0; float4 v1 : COLOR1; float4 tc0 : TEXCOORD0; float4 tc1 : TEXCOORD1; float4 tc2 : TEXCOORD2; float4 tc3 : TEXCOORD3; };\n");
    w.printf("float4 main(PS_IN i) : COLOR {\n");
    w.printf("    float4 v0 = i.v0, v1 = i.v1;\n");
    w.printf("    float4 fog = float4(k[%d].rgb, 1);\n", NV2A_PS_K_FOG);
    w.printf("    float4 t0 = 0, t1 = 0, t2 = 0, t3 = 0;\n");

    // Textures, in stage order, since the bump and dependent modes read an earlier stage's result.
    for (int s = 0; s < 4; s++) {
        uint32_t mode = info->textureMode[s];
        uint32_t src = InputStageFor(def[DEF_INPUT_TEXTURE], s);
        switch (mode) {
            case TEX_NONE:
                break;
            case TEX_PASSTHRU:
                w.printf("    t%d = i.tc%d;\n", s, s);
                break;
            case TEX_CUBEMAP:
                w.printf("    t%d = texCUBE(s%d, i.tc%d.xyz);\n", s, s, s);
                break;
            case TEX_BUMPENVMAP:
            case TEX_BUMPENVMAP_LUM:
                // The source stage's red and green, through the 2x2 bump matrix, perturb this stage's coordinates.
                w.printf("    t%d = tex2D(s%d, i.tc%d.xy / i.tc%d.w + float2(dot(k[%d].xy, t%u.xy), dot(k[%d].zw, t%u.xy)));\n",
                         s, s, s, s, NV2A_PS_K_BUMPMAT + s, src, NV2A_PS_K_BUMPMAT + s, src);
                if (mode == TEX_BUMPENVMAP_LUM) {
                    const char *lum = (s == 1) ? "k[23].x, k[23].y" : (s == 2) ? "k[23].z, k[23].w" : "k[24].x, k[24].y";
                    w.printf("    { float2 lum = float2(%s); t%d.rgb *= saturate(t%u.b * lum.x + lum.y); }\n", lum, s, src);
                }
                break;
            case TEX_DPNDNT_AR:
                w.printf("    t%d = tex2D(s%d, float2(t%u.a, t%u.r));\n", s, s, src, src);
                break;
            case TEX_DPNDNT_GB:
                w.printf("    t%d = tex2D(s%d, float2(t%u.g, t%u.b));\n", s, s, src, src);
                break;
            default:   // PROJECT2D, PROJECT3D, and the modes not done yet, which sample as 2D so that something shows
                w.printf("    t%d = tex2D(s%d, i.tc%d.xy / i.tc%d.w);\n", s, s, s, s);
                break;
        }
        // X_D3DTSS_COLORSIGN: channels the game declared signed are expanded from the unsigned texture.
        if (mode != TEX_NONE && mode != TEX_PASSTHRU)
            w.printf("    t%d = lerp(t%d, t%d * 2 - 1, k[%d]);\n", s, s, s, NV2A_PS_K_SIGN + s);
    }
    // r0 and r1 start black, with the alpha of t0 and t1 (Cxbx's translator, checked against the XDK samples).
    w.printf("    float4 r0 = float4(0, 0, 0, t0.a), r1 = float4(0, 0, 0, t1.a);\n");

    for (uint32_t s = 0; s < stages; s++) {
        uint32_t rgbIn = def[DEF_RGB_INPUTS + s], rgbOut = def[DEF_RGB_OUTPUTS + s];
        uint32_t alphaIn = def[DEF_ALPHA_INPUTS + s], alphaOut = def[DEF_ALPHA_OUTPUTS + s];
        w.printf("    { // stage %u\n", s);
        // Every input is read before anything is written: the two halves run in parallel on the hardware.
        static const char *names[4] = { "A", "B", "C", "D" };
        for (int k = 0; k < 4; k++) {
            w.printf("        float3 %s = ", names[k]); EmitInput(&w, (rgbIn >> (24 - 8 * k)) & 0xFF, false, (int)s, false); w.printf(";\n");
        }
        static const char *alphaNames[4] = { "a", "b", "c", "d" };
        for (int k = 0; k < 4; k++) {
            w.printf("        float %s = ", alphaNames[k]); EmitInput(&w, (alphaIn >> (24 - 8 * k)) & 0xFF, true, (int)s, false); w.printf(";\n");
        }
        uint32_t rgbFlags = rgbOut >> 12, alphaFlags = alphaOut >> 12;
        w.printf("        float3 AB = %s;\n", (rgbFlags & 2) ? "dot(A, B)" : "A * B");
        w.printf("        float3 CD = %s;\n", (rgbFlags & 1) ? "dot(C, D)" : "C * D");
        w.printf("        float3 SUM = %s;\n", (rgbFlags & 4) ? "(r0.a >= 0.5) ? CD : AB" : "AB + CD");
        w.printf("        float ab = a * b, cd = c * d;\n");
        w.printf("        float sum = %s;\n", (alphaFlags & 4) ? "(r0.a >= 0.5) ? cd : ab" : "ab + cd");
        uint32_t rgbOp = (rgbFlags >> 3) & 7, alphaOp = (alphaFlags >> 3) & 7;
        w.printf("        float3 AB2 = "); EmitOutputOp(&w, rgbOp, "AB"); w.printf(";\n");
        w.printf("        float3 CD2 = "); EmitOutputOp(&w, rgbOp, "CD"); w.printf(";\n");
        w.printf("        float3 SUM2 = "); EmitOutputOp(&w, rgbOp, "SUM"); w.printf(";\n");
        w.printf("        float ab2 = "); EmitOutputOp(&w, alphaOp, "ab"); w.printf(";\n");
        w.printf("        float cd2 = "); EmitOutputOp(&w, alphaOp, "cd"); w.printf(";\n");
        w.printf("        float sum2 = "); EmitOutputOp(&w, alphaOp, "sum"); w.printf(";\n");
        // Alpha writes first, then RGB, then RGB's blue-to-alpha, which takes precedence over an alpha write.
        const char *d;
        if ((d = DestinationName((alphaOut >> 4) & 0xF)) != NULL) w.printf("        %s.a = ab2;\n", d);
        if ((d = DestinationName(alphaOut & 0xF)) != NULL) w.printf("        %s.a = cd2;\n", d);
        if ((d = DestinationName((alphaOut >> 8) & 0xF)) != NULL) w.printf("        %s.a = sum2;\n", d);
        if ((d = DestinationName((rgbOut >> 4) & 0xF)) != NULL) {
            w.printf("        %s.rgb = AB2;\n", d);
            if (rgbFlags & 0x80) w.printf("        %s.a = AB2.b;\n", d);
        }
        if ((d = DestinationName(rgbOut & 0xF)) != NULL) {
            w.printf("        %s.rgb = CD2;\n", d);
            if (rgbFlags & 0x40) w.printf("        %s.a = CD2.b;\n", d);
        }
        if ((d = DestinationName((rgbOut >> 8) & 0xF)) != NULL) w.printf("        %s.rgb = SUM2;\n", d);
        w.printf("    }\n");
    }

    // The final combiner.
    if (defaultFinal) {
        w.printf("    return saturate(r0);\n}\n");
    } else {
        uint32_t flags = finalEfg & 0xFF;
        w.printf("    float3 efprod = "); EmitInput(&w, (finalEfg >> 24) & 0xFF, false, 0, true);
        w.printf(" * "); EmitInput(&w, (finalEfg >> 16) & 0xFF, false, 0, true); w.printf(";\n");
        w.printf("    float3 v1r0sum = %s + %s;\n", (flags & 0x40) ? "(1 - v1.rgb)" : "v1.rgb", (flags & 0x20) ? "(1 - r0.rgb)" : "r0.rgb");
        if (flags & 0x80) w.printf("    v1r0sum = saturate(v1r0sum);\n");
        w.printf("    float3 FA = "); EmitInput(&w, (finalAbcd >> 24) & 0xFF, false, 0, true); w.printf(";\n");
        w.printf("    float3 FB = "); EmitInput(&w, (finalAbcd >> 16) & 0xFF, false, 0, true); w.printf(";\n");
        w.printf("    float3 FC = "); EmitInput(&w, (finalAbcd >> 8) & 0xFF, false, 0, true); w.printf(";\n");
        w.printf("    float3 FD = "); EmitInput(&w, finalAbcd & 0xFF, false, 0, true); w.printf(";\n");
        w.printf("    float FG = "); EmitInput(&w, (finalEfg >> 8) & 0xFF, true, 0, true); w.printf(";\n");
        if (finalUsesFogBlend)
            w.printf("    return saturate(float4(FB + FD, FG)); // fog.a * B + (1 - fog.a) * fog.rgb: the host's fog does the blend\n}\n");
        else
            w.printf("    return saturate(float4(FA * FB + (1 - FA) * FC + FD, FG));\n}\n");
    }
    bool ok = !w.overflow && DeclareConstants(body, hlsl, hlslSize);
    free(body);
    return ok;
}

