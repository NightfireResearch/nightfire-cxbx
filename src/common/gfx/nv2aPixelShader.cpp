#include "nv2aPixelShader.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// See the header for what this is. The definition's words, by index (the XDK's D3DPIXELSHADERDEF):
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

bool Nv2aPixelShader_Translate(const uint32_t def[60], char *hlsl, size_t hlslSize, Nv2aPixelShaderInfo *info) {
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

    Writer w = { hlsl, hlslSize, 0, false };
    w.printf("// NV2A register combiner program, %u stage%s\n", stages, stages == 1 ? "" : "s");
    w.printf("sampler s0 : register(s0);\nsampler s1 : register(s1);\nsampler s2 : register(s2);\nsampler s3 : register(s3);\n");
    w.printf("float4 k[%d] : register(c0);\n", NV2A_PS_K_COUNT);
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
    return !w.overflow;
}

static void UnpackColour(uint32_t argb, float out[4]) {
    out[0] = ((argb >> 16) & 0xFF) / 255.0f;
    out[1] = ((argb >> 8) & 0xFF) / 255.0f;
    out[2] = (argb & 0xFF) / 255.0f;
    out[3] = ((argb >> 24) & 0xFF) / 255.0f;
}

static float FloatBits(uint32_t bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

void Nv2aPixelShader_BuildConstants(const uint32_t def[60], const float d3dConstants[16][4], uint32_t fogColour,
                                    const uint32_t bumpEnv[4][6], const uint32_t colourSign[4],
                                    float out[NV2A_PS_K_COUNT][4]) {
    memset(out, 0, sizeof(float) * 4 * NV2A_PS_K_COUNT);
    // A stage's constant is the literal in the definition unless its mapping nibble names a D3D constant, in
    // which case SetPixelShaderConstant's value stands in - that is what the original does with the mapping
    // (driving 0x0016b160). Unless the count word says the stages share stage 0's (PS_COMBINERCOUNT_SAME_C0,
    // the bit clear), in which case they all do.
    bool uniqueC0 = (def[DEF_COMBINER_COUNT] & 0x1000) != 0, uniqueC1 = (def[DEF_COMBINER_COUNT] & 0x10000) != 0;
    for (int s = 0; s < 8; s++) {
        int src0 = uniqueC0 ? s : 0, src1 = uniqueC1 ? s : 0;
        uint32_t m0 = (def[DEF_C0_MAPPING] >> (4 * src0)) & 0xF, m1 = (def[DEF_C1_MAPPING] >> (4 * src1)) & 0xF;
        if (m0 != 0xF) memcpy(out[NV2A_PS_K_C0 + s], d3dConstants[m0], 16); else UnpackColour(def[DEF_C0 + src0], out[NV2A_PS_K_C0 + s]);
        if (m1 != 0xF) memcpy(out[NV2A_PS_K_C1 + s], d3dConstants[m1], 16); else UnpackColour(def[DEF_C1 + src1], out[NV2A_PS_K_C1 + s]);
    }
    uint32_t f0 = def[DEF_FINAL_CONSTANTS] & 0xF, f1 = (def[DEF_FINAL_CONSTANTS] >> 4) & 0xF;
    if (f0 != 0xF) memcpy(out[NV2A_PS_K_FINAL_C0], d3dConstants[f0], 16); else UnpackColour(def[DEF_FINAL_C0], out[NV2A_PS_K_FINAL_C0]);
    if (f1 != 0xF) memcpy(out[NV2A_PS_K_FINAL_C1], d3dConstants[f1], 16); else UnpackColour(def[DEF_FINAL_C1], out[NV2A_PS_K_FINAL_C1]);
    UnpackColour(fogColour, out[NV2A_PS_K_FOG]);
    for (int s = 0; s < 4; s++) {
        for (int m = 0; m < 4; m++)
            out[NV2A_PS_K_BUMPMAT + s][m] = FloatBits(bumpEnv[s][m]);
        // X_D3DTSIGN_ASIGNED = 1, RSIGNED = 2, GSIGNED = 4, BSIGNED = 8.
        out[NV2A_PS_K_SIGN + s][0] = (colourSign[s] & 2) ? 1.0f : 0.0f;
        out[NV2A_PS_K_SIGN + s][1] = (colourSign[s] & 4) ? 1.0f : 0.0f;
        out[NV2A_PS_K_SIGN + s][2] = (colourSign[s] & 8) ? 1.0f : 0.0f;
        out[NV2A_PS_K_SIGN + s][3] = (colourSign[s] & 1) ? 1.0f : 0.0f;
    }
    out[NV2A_PS_K_BUMPLUM][0] = FloatBits(bumpEnv[1][4]); out[NV2A_PS_K_BUMPLUM][1] = FloatBits(bumpEnv[1][5]);
    out[NV2A_PS_K_BUMPLUM][2] = FloatBits(bumpEnv[2][4]); out[NV2A_PS_K_BUMPLUM][3] = FloatBits(bumpEnv[2][5]);
    out[NV2A_PS_K_BUMPLUM + 1][0] = FloatBits(bumpEnv[3][4]); out[NV2A_PS_K_BUMPLUM + 1][1] = FloatBits(bumpEnv[3][5]);
}
