#include "nv2aPixelShader.h"

#include "shaderBuilder.h"

#include <stdio.h>
#include <stdlib.h>
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

void Nv2aPixelShader_Analyse(const uint32_t def[60], Nv2aPixelShaderInfo *info) {
    memset(info, 0, sizeof(*info));
    uint32_t stages = def[DEF_COMBINER_COUNT] & 0xF;
    if (stages > 8) stages = 8;
    info->stageCount = stages;
    for (int s = 0; s < 4; s++) {
        info->textureMode[s] = (uint8_t)((def[DEF_TEXTURE_MODES] >> (5 * s)) & 0x1F);
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
    info->readsFog = ProgramReadsRegister(def, stages, REG_FOG);
    bool finalUsesFogBlend = ((finalAbcd >> 24) & 0xF) == REG_FOG && ((finalAbcd >> 8) & 0xF) == REG_FOG;
    info->fogByHost = (finalAbcd == 0 && finalEfg == 0) || finalUsesFogBlend;
}

namespace {

typedef ShaderValue Vec[4];

class PixelTranslator {
public:
    PixelTranslator() : b(ShaderBuilder::kPixelShader) {}
    bool Translate(const uint32_t def[60], std::vector<uint32_t> *bytecode, Nv2aPixelShaderInfo *info,
                   char *error, size_t errorSize);

private:
    ShaderValue Lit(float f) { return b.Literal(f); }
    Vec *Register(uint32_t reg, int stage, bool finalCombiner, Vec scratch);
    ShaderValue Input(uint32_t byte, bool alphaHalf, int lane, int stage, bool finalCombiner);
    ShaderValue OutputOp(uint32_t op, ShaderValue x);
    void Texture(int s, uint32_t mode, uint32_t src);

    ShaderBuilder b;
    Vec v0, v1, fog, t[4], r0, r1, efprod, v1r0sum;
};

// The register an input byte names. Stage constants are indexed by stage; in the final combiner they are the
// final combiner's own pair. The final combiner's two extra registers read as zero in the stages.
Vec *PixelTranslator::Register(uint32_t reg, int stage, bool finalCombiner, Vec scratch) {
    int k = -1;
    switch (reg) {
        case REG_C0: k = finalCombiner ? NV2A_PS_K_FINAL_C0 : NV2A_PS_K_C0 + stage; break;
        case REG_C1: k = finalCombiner ? NV2A_PS_K_FINAL_C1 : NV2A_PS_K_C1 + stage; break;
        case REG_FOG: return &fog;
        case REG_V0: return &v0;
        case REG_V1: return &v1;
        case REG_T0: case REG_T1: case REG_T2: case REG_T3: return &t[reg - REG_T0];
        case REG_R0: return &r0;
        case REG_R1: return &r1;
        case REG_V1R0_SUM: if (finalCombiner) return &v1r0sum; break;
        case REG_EF_PROD: if (finalCombiner) return &efprod; break;
        default: break;
    }
    for (int c = 0; c < 4; c++) scratch[c] = k >= 0 ? b.Constant(k, c) : Lit(0.0f);
    return (Vec *)scratch;
}

// One input byte: register, channel and input mapping, for one lane. RGB inputs read the register's lane (or
// its alpha, replicated); alpha inputs read its alpha (or its blue).
ShaderValue PixelTranslator::Input(uint32_t byte, bool alphaHalf, int lane, int stage, bool finalCombiner) {
    Vec scratch;
    Vec &reg = *Register(byte & 0xF, stage, finalCombiner, scratch);
    bool channelBit = (byte & 0x10) != 0;
    ShaderValue x = alphaHalf ? reg[channelBit ? 3 : 2] : reg[channelBit ? 3 : lane];
    switch ((byte >> 5) & 7) {
        case 0: return b.Max(x, Lit(0.0f));                                     // PS_INPUTMAPPING_UNSIGNED_IDENTITY
        case 1: return b.Sub(Lit(1.0f), b.Saturate(x));                         // UNSIGNED_INVERT
        case 2: return b.Sub(b.Mul(Lit(2.0f), b.Max(x, Lit(0.0f))), Lit(1.0f)); // EXPAND_NORMAL
        case 3: return b.Sub(Lit(1.0f), b.Mul(Lit(2.0f), b.Max(x, Lit(0.0f)))); // EXPAND_NEGATE
        case 4: return b.Sub(b.Max(x, Lit(0.0f)), Lit(0.5f));                   // HALFBIAS_NORMAL
        case 5: return b.Sub(Lit(0.5f), b.Max(x, Lit(0.0f)));                   // HALFBIAS_NEGATE
        case 6: return x;                                                       // SIGNED_IDENTITY
        default: return ShaderBuilder::Negate(x);                               // SIGNED_NEGATE
    }
}

// The output scale and bias of a stage (PS_COMBINEROUTPUT_* op field), then the clamp every combiner output gets.
ShaderValue PixelTranslator::OutputOp(uint32_t op, ShaderValue x) {
    switch (op) {
        case 1: x = b.Sub(x, Lit(0.5f)); break;                      // BIAS
        case 2: x = b.Mul(x, Lit(2.0f)); break;                      // SHIFTLEFT_1
        case 3: x = b.Mul(b.Sub(x, Lit(0.5f)), Lit(2.0f)); break;    // SHIFTLEFT_1_BIAS
        case 4: x = b.Mul(x, Lit(4.0f)); break;                      // SHIFTLEFT_2
        case 5: x = b.Mul(b.Sub(x, Lit(0.5f)), Lit(4.0f)); break;    // SHIFTLEFT_2_BIAS
        case 6: x = b.Mul(x, Lit(0.5f)); break;                      // SHIFTRIGHT_1
        case 7: x = b.Mul(b.Sub(x, Lit(0.5f)), Lit(0.5f)); break;    // SHIFTRIGHT_1_BIAS
        default: break;
    }
    return b.Min(b.Max(x, Lit(-1.0f)), Lit(1.0f));
}

// One texture stage. The bump and dependent modes read an earlier stage's result, so stages go in order.
void PixelTranslator::Texture(int s, uint32_t mode, uint32_t src) {
    ShaderValue tc[4], coord[3];
    for (int c = 0; c < 4; c++) tc[c] = b.TexCoord(s, c);
    switch (mode) {
        case TEX_NONE:
            return;
        case TEX_PASSTHRU:
            for (int c = 0; c < 4; c++) t[s][c] = tc[c];
            return;
        case TEX_CUBEMAP:
            b.Sample(s, true, tc, t[s]);
            break;
        case TEX_BUMPENVMAP:
        case TEX_BUMPENVMAP_LUM: {
            // The source stage's red and green, through the 2x2 bump matrix, perturb this stage's coordinates.
            ShaderValue q = b.Rcp(tc[3]);
            b.BeginVector();
            for (int c = 0; c < 2; c++) {
                b.Lane(c);
                ShaderValue dot = b.Add(b.Mul(b.Constant(NV2A_PS_K_BUMPMAT + s, 2 * c), t[src][0]),
                                        b.Mul(b.Constant(NV2A_PS_K_BUMPMAT + s, 2 * c + 1), t[src][1]));
                coord[c] = b.Add(b.Mul(tc[c], q), dot);
            }
            b.EndVector();
            b.Sample(s, false, coord, t[s]);
            if (mode == TEX_BUMPENVMAP_LUM) {
                int k = s == 3 ? NV2A_PS_K_BUMPLUM + 1 : NV2A_PS_K_BUMPLUM;
                int first = s == 2 ? 2 : 0;
                ShaderValue lum = b.Saturate(b.Mad(t[src][2], b.Constant(k, first), b.Constant(k, first + 1)));
                for (int c = 0; c < 3; c++) t[s][c] = b.Mul(t[s][c], lum);
            }
            break;
        }
        case TEX_DPNDNT_AR:
            coord[0] = t[src][3]; coord[1] = t[src][0];
            b.Sample(s, false, coord, t[s]);
            break;
        case TEX_DPNDNT_GB:
            coord[0] = t[src][1]; coord[1] = t[src][2];
            b.Sample(s, false, coord, t[s]);
            break;
        default: {   // PROJECT2D, PROJECT3D, and the modes not done yet, which sample as 2D so that something shows
            ShaderValue q = b.Rcp(tc[3]);
            b.BeginVector();
            for (int c = 0; c < 2; c++) { b.Lane(c); coord[c] = b.Mul(tc[c], q); }
            b.EndVector();
            b.Sample(s, false, coord, t[s]);
            break;
        }
    }
    // X_D3DTSS_COLORSIGN: channels the game declared signed are expanded from the unsigned texture.
    b.BeginVector();
    for (int c = 0; c < 4; c++) {
        b.Lane(c);
        t[s][c] = b.Lerp(t[s][c], b.Sub(b.Mul(t[s][c], Lit(2.0f)), Lit(1.0f)), b.Constant(NV2A_PS_K_SIGN + s, c));
    }
    b.EndVector();
}

bool PixelTranslator::Translate(const uint32_t def[60], std::vector<uint32_t> *bytecode, Nv2aPixelShaderInfo *info,
                                char *error, size_t errorSize) {
    Nv2aPixelShader_Analyse(def, info);
    uint32_t stages = info->stageCount;
    // PS_COMBINERCOUNT_MUX_MSB is the only mux test done: the LSB variant needs 8-bit arithmetic no game here uses.

    for (int c = 0; c < 4; c++) {
        v0[c] = b.Input(0, c);
        v1[c] = b.Input(1, c);
        fog[c] = c < 3 ? b.Constant(NV2A_PS_K_FOG, c) : Lit(1.0f);
        for (int s = 0; s < 4; s++) t[s][c] = Lit(0.0f);
    }
    for (int s = 0; s < 4; s++)
        Texture(s, info->textureMode[s], InputStageFor(def[DEF_INPUT_TEXTURE], s));
    // r0 and r1 start black, with the alpha of t0 and t1 (Cxbx's translator, checked against the XDK samples).
    for (int c = 0; c < 3; c++) r0[c] = r1[c] = Lit(0.0f);
    r0[3] = t[0][3];
    r1[3] = t[1][3];

    for (uint32_t s = 0; s < stages; s++) {
        uint32_t rgbIn = def[DEF_RGB_INPUTS + s], rgbOut = def[DEF_RGB_OUTPUTS + s];
        uint32_t alphaIn = def[DEF_ALPHA_INPUTS + s], alphaOut = def[DEF_ALPHA_OUTPUTS + s];
        uint32_t rgbFlags = rgbOut >> 12, alphaFlags = alphaOut >> 12;
        // Every input is read before anything is written: the two halves run in parallel on the hardware.
        Vec in[4];
        ShaderValue alpha[4];
        for (int k = 0; k < 4; k++) {
            b.BeginVector();
            for (int c = 0; c < 3; c++) { b.Lane(c); in[k][c] = Input((rgbIn >> (24 - 8 * k)) & 0xFF, false, c, (int)s, false); }
            b.EndVector();
            alpha[k] = Input((alphaIn >> (24 - 8 * k)) & 0xFF, true, 3, (int)s, false);
        }
        ShaderValue muxCondition = b.Sub(r0[3], Lit(0.5f));   // r0.a >= 0.5
        Vec AB, CD, SUM;
        ShaderValue dotAB = b.Dp3(in[0], in[1]), dotCD = b.Dp3(in[2], in[3]);
        b.BeginVector();
        for (int c = 0; c < 3; c++) {
            b.Lane(c);
            AB[c] = (rgbFlags & 2) ? dotAB : b.Mul(in[0][c], in[1][c]);
            CD[c] = (rgbFlags & 1) ? dotCD : b.Mul(in[2][c], in[3][c]);
            SUM[c] = (rgbFlags & 4) ? b.Cmp(muxCondition, CD[c], AB[c]) : b.Add(AB[c], CD[c]);
        }
        b.EndVector();
        ShaderValue ab = b.Mul(alpha[0], alpha[1]), cd = b.Mul(alpha[2], alpha[3]);
        ShaderValue sum = (alphaFlags & 4) ? b.Cmp(muxCondition, cd, ab) : b.Add(ab, cd);
        uint32_t rgbOp = (rgbFlags >> 3) & 7, alphaOp = (alphaFlags >> 3) & 7;
        Vec AB2, CD2, SUM2;
        b.BeginVector();
        for (int c = 0; c < 3; c++) {
            b.Lane(c);
            AB2[c] = OutputOp(rgbOp, AB[c]);
            CD2[c] = OutputOp(rgbOp, CD[c]);
            SUM2[c] = OutputOp(rgbOp, SUM[c]);
        }
        b.EndVector();
        ShaderValue ab2 = OutputOp(alphaOp, ab), cd2 = OutputOp(alphaOp, cd), sum2 = OutputOp(alphaOp, sum);
        // Alpha writes first, then RGB, then RGB's blue-to-alpha, which takes precedence over an alpha write.
        Vec scratch, *d;
        auto dest = [&](uint32_t reg) -> Vec * {
            switch (reg) {
                case REG_V0: case REG_V1: case REG_T0: case REG_T1: case REG_T2: case REG_T3: case REG_R0: case REG_R1:
                    return Register(reg, (int)s, false, scratch);
                default: return NULL;   // zero and the constants discard
            }
        };
        if ((d = dest((alphaOut >> 4) & 0xF)) != NULL) (*d)[3] = ab2;
        if ((d = dest(alphaOut & 0xF)) != NULL) (*d)[3] = cd2;
        if ((d = dest((alphaOut >> 8) & 0xF)) != NULL) (*d)[3] = sum2;
        if ((d = dest((rgbOut >> 4) & 0xF)) != NULL) {
            for (int c = 0; c < 3; c++) (*d)[c] = AB2[c];
            if (rgbFlags & 0x80) (*d)[3] = AB2[2];
        }
        if ((d = dest(rgbOut & 0xF)) != NULL) {
            for (int c = 0; c < 3; c++) (*d)[c] = CD2[c];
            if (rgbFlags & 0x40) (*d)[3] = CD2[2];
        }
        if ((d = dest((rgbOut >> 8) & 0xF)) != NULL)
            for (int c = 0; c < 3; c++) (*d)[c] = SUM2[c];
    }

    // The final combiner: A*B + (1-A)*C + D in RGB, G in alpha.
    uint32_t finalAbcd = def[DEF_FINAL_ABCD], finalEfg = def[DEF_FINAL_EFG];
    ShaderValue colour[4];
    if (finalAbcd == 0 && finalEfg == 0) {
        b.BeginVector();
        for (int c = 0; c < 4; c++) { b.Lane(c); colour[c] = b.Saturate(r0[c]); }
        b.EndVector();
    } else {
        uint32_t e = (finalEfg >> 24) & 0xF, f = (finalEfg >> 16) & 0xF;
        if (e == REG_V1R0_SUM || e == REG_EF_PROD || f == REG_V1R0_SUM || f == REG_EF_PROD) {
            snprintf(error, errorSize, "the final combiner's E or F reads V1R0_SUM or EF_PROD");
            return false;
        }
        uint32_t flags = finalEfg & 0xFF;
        b.BeginVector();
        for (int c = 0; c < 3; c++) {
            b.Lane(c);
            efprod[c] = b.Mul(Input((finalEfg >> 24) & 0xFF, false, c, 0, true), Input((finalEfg >> 16) & 0xFF, false, c, 0, true));
            ShaderValue x = (flags & 0x40) ? b.Sub(Lit(1.0f), v1[c]) : v1[c];
            ShaderValue y = (flags & 0x20) ? b.Sub(Lit(1.0f), r0[c]) : r0[c];
            v1r0sum[c] = b.Add(x, y);
            if (flags & 0x80) v1r0sum[c] = b.Saturate(v1r0sum[c]);
        }
        b.EndVector();
        efprod[3] = v1r0sum[3] = Lit(0.0f);
        bool fogBlend = ((finalAbcd >> 24) & 0xF) == REG_FOG && ((finalAbcd >> 8) & 0xF) == REG_FOG;
        b.BeginVector();
        for (int c = 0; c < 3; c++) {
            b.Lane(c);
            ShaderValue A = Input((finalAbcd >> 24) & 0xFF, false, c, 0, true);
            ShaderValue B = Input((finalAbcd >> 16) & 0xFF, false, c, 0, true);
            ShaderValue C = Input((finalAbcd >> 8) & 0xFF, false, c, 0, true);
            ShaderValue D = Input(finalAbcd & 0xFF, false, c, 0, true);
            // With the standard fog blend (fog.a * B + (1 - fog.a) * fog.rgb) the host's fog does the blend.
            colour[c] = b.Saturate(fogBlend ? b.Add(B, D) : b.Add(b.Add(b.Mul(A, B), b.Mul(b.Sub(Lit(1.0f), A), C)), D));
        }
        b.EndVector();
        colour[3] = b.Saturate(Input((finalEfg >> 8) & 0xFF, true, 3, 0, true));
    }
    for (int c = 0; c < 4; c++) b.SetOutput(ShaderBuilder::kOutColour, c, colour[c]);
    return b.Assemble(bytecode, error, errorSize);
}

} // namespace

bool Nv2aPixelShader_Translate(const uint32_t def[60], std::vector<uint32_t> *bytecode, Nv2aPixelShaderInfo *info,
                               char *error, size_t errorSize) {
    PixelTranslator *t = new PixelTranslator();
    bool ok = t->Translate(def, bytecode, info, error, errorSize);
    delete t;
    return ok;
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

void Nv2aPixelShader_LoadFactors(const uint32_t def[60], float factors[NV2A_PS_FACTOR_COUNT][4]) {
    // PS_COMBINERCOUNT_UNIQUE_C0/C1 (the bits set) give each stage its own; clear, every stage uses stage 0's.
    bool uniqueC0 = (def[DEF_COMBINER_COUNT] & 0x1000) != 0, uniqueC1 = (def[DEF_COMBINER_COUNT] & 0x10000) != 0;
    for (int s = 0; s < 8; s++) {
        UnpackColour(def[DEF_C0 + (uniqueC0 ? s : 0)], factors[s]);
        UnpackColour(def[DEF_C1 + (uniqueC1 ? s : 0)], factors[8 + s]);
    }
    UnpackColour(def[DEF_FINAL_C0], factors[16]);
    UnpackColour(def[DEF_FINAL_C1], factors[17]);
}

void Nv2aPixelShader_SetConstant(const uint32_t def[60], uint32_t reg, const float value[4], float factors[NV2A_PS_FACTOR_COUNT][4]) {
    // Packed to a byte per channel, as the original does before it writes the register.
    float packed[4];
    for (int c = 0; c < 4; c++) {
        float v = value[c] < 0.0f ? 0.0f : value[c] > 1.0f ? 1.0f : value[c];
        packed[c] = (float)(int)(v * 255.0f + 0.5f) / 255.0f;
    }
    for (int s = 0; s < 8; s++) {
        if (((def[DEF_C0_MAPPING] >> (4 * s)) & 0xF) == reg) memcpy(factors[s], packed, 16);
        if (((def[DEF_C1_MAPPING] >> (4 * s)) & 0xF) == reg) memcpy(factors[8 + s], packed, 16);
    }
    if ((def[DEF_FINAL_CONSTANTS] & 0xF) == reg) memcpy(factors[16], packed, 16);
    if (((def[DEF_FINAL_CONSTANTS] >> 4) & 0xF) == reg) memcpy(factors[17], packed, 16);
}

void Nv2aPixelShader_BuildConstants(const float factors[NV2A_PS_FACTOR_COUNT][4], uint32_t fogColour,
                                    const uint32_t bumpEnv[4][6], const uint32_t colourSign[4],
                                    float out[NV2A_PS_K_COUNT][4]) {
    memset(out, 0, sizeof(float) * 4 * NV2A_PS_K_COUNT);
    memcpy(out[NV2A_PS_K_C0], factors, sizeof(float) * 4 * NV2A_PS_FACTOR_COUNT);   // C0s, C1s, then the final pair
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
