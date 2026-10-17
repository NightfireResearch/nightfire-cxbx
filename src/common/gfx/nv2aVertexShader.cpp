#include "nv2aVertexShader.h"

#include "shaderBuilder.h"
#include "shaderSpirv.h"

#include <stdio.h>
#include <string.h>

// See the header. The program is translated value by value: each register component the NV2A program reads
// is the value last written to it, so the result is the program's arithmetic with the registers gone.

void Nv2aVertexShader_Decode(const uint32_t *w, VshDecoded *d) {
    uint32_t b = w[0], c = w[1], e = w[2];
    d->a.swz[3] = b & 3; d->a.swz[2] = (b >> 2) & 3; d->a.swz[1] = (b >> 4) & 3; d->a.swz[0] = (b >> 6) & 3;
    d->a.negate = (b >> 8) & 1;
    d->input = (b >> 9) & 0xF;
    d->constIndex = (b >> 13) & 0xFF;
    d->mac = (b >> 21) & 0xF;
    d->ilu = (b >> 25) & 7;
    d->c.tempReg = (c & 3) << 2;
    d->c.swz[3] = (c >> 2) & 3; d->c.swz[2] = (c >> 4) & 3; d->c.swz[1] = (c >> 6) & 3; d->c.swz[0] = (c >> 8) & 3;
    d->c.negate = (c >> 10) & 1;
    d->b.mux = (c >> 11) & 3;
    d->b.tempReg = (c >> 13) & 0xF;
    d->b.swz[3] = (c >> 17) & 3; d->b.swz[2] = (c >> 19) & 3; d->b.swz[1] = (c >> 21) & 3; d->b.swz[0] = (c >> 23) & 3;
    d->b.negate = (c >> 25) & 1;
    d->a.mux = (c >> 26) & 3;
    d->a.tempReg = (c >> 28) & 0xF;
    d->final = e & 1;
    d->a0x = (e >> 1) & 1;
    d->outMux = (e >> 2) & 1;
    d->outAddress = (e >> 3) & 0xFF;
    d->outIsOutput = (e >> 11) & 1;
    d->outOMask = (e >> 12) & 0xF;
    d->outIluMask = (e >> 16) & 0xF;
    d->outTempReg = (e >> 20) & 0xF;
    d->outMacMask = (e >> 24) & 0xF;
    d->c.mux = (e >> 28) & 3;
    d->c.tempReg |= (e >> 30) & 3;
}

namespace {

typedef ShaderValue Vec[4];

// Output register addresses (the outAddress field) and the outputs they become.
enum { OUT_POS = 0, OUT_D0 = 3, OUT_D1 = 4, OUT_FOG = 5, OUT_PTS = 6, OUT_B0 = 7, OUT_B1 = 8, OUT_T0 = 9, OUT_COUNT = 13 };

// Constants the translation adds, beyond the game's 192 (see the header).
enum { C_HOST_ADJUST = 200, C_DEPTH_CLIP = 201, C_FOG = 202, C_TEX_SCALE01 = 203, C_TEX_SCALE23 = 204 };

// D3DDECLUSAGE_POSITION and _TEXCOORD: v0 is bound to the position, every other register to the texture
// coordinate of its own number.
enum { USAGE_POSITION = 0, USAGE_TEXCOORD = 5 };

class VertexTranslator {
public:
    VertexTranslator() : b(ShaderBuilder::kVertexShader), spirv(NULL), spirvInfo(NULL) {}
    bool Translate(const uint8_t *function, uint32_t normPacked, const uint8_t inputComponents[16],
                   std::vector<uint32_t> *bytecode, Nv2aVertexShaderInfo *info, char *error, size_t errorSize);

private:
    ShaderValue Lit(float f) { return b.Literal(f); }
    void ReadSource(const VshDecoded &d, const VshSource &s, Vec out);
    void Input(int reg, Vec out);
    ShaderValue SignedFraction(ShaderValue q);
    void UnpackNormPacked(int reg, Vec out);
    ShaderValue Rcc(ShaderValue x);
    void Epilogue();

    ShaderBuilder b;
    Vec R[13];           // R0-R11, and R12, which is oPos
    Vec out[OUT_COUNT];  // the output registers by address
    Vec v[16];
    bool inputBuilt[16];
    uint32_t normPacked;
    const uint8_t *components;
    ShaderValue a0;
    bool a0Set;
    uint32_t inputsUsed, outputsWritten;

public:
    const ShaderSpirvOptions *spirv;   // SPIR-V rather than vs_2_0 tokens, when set
    ShaderSpirvInfo *spirvInfo;
};

// Reading an input register is where its value is made: the declaration's components, with the NV2A's
// defaults (0, 0, 0, 1) for the rest, or NORMPACKED3 unpacked.
void VertexTranslator::Input(int reg, Vec result) {
    inputsUsed |= 1u << reg;
    if (!inputBuilt[reg]) {
        inputBuilt[reg] = true;
        int n = components[reg];
        if (n == 0) {
            // Not in the declaration: the NV2A reads the register's default. D3D9 leaves an input with no
            // element undefined, so it is not an input here at all. (Shader 129, the particles, takes its
            // positions' w from v3.w, which its declaration does not supply.)
            v[reg][0] = v[reg][1] = v[reg][2] = Lit(0.0f);
            v[reg][3] = Lit(1.0f);
        } else {
            b.DeclareInput(reg, reg == 0 ? USAGE_POSITION : USAGE_TEXCOORD, reg);
            if (normPacked & (1u << reg)) {
                UnpackNormPacked(reg, v[reg]);
            } else {
                // Components the declaration does not give get the defaults back, so that a neighbouring
                // vertex's bytes cannot arrive as this one's .y.
                for (int c = 0; c < 4; c++) v[reg][c] = c < n ? b.Input(reg, c) : Lit(c == 3 ? 1.0f : 0.0f);
            }
        }
    }
    for (int c = 0; c < 4; c++) result[c] = v[reg][c];
}

// The fraction of q with q's sign: what fmod(q * n, n) / n is.
ShaderValue VertexTranslator::SignedFraction(ShaderValue q) {
    ShaderValue f = b.Frc(b.Abs(q));
    return b.Lerp(ShaderBuilder::Negate(f), f, b.Sge(q, ShaderBuilder::Negate(q)));
}

// 11:11:10 signed normalised, arriving as four raw bytes (UBYTE4): the fields rebuilt with exact float maths.
//   x = lo % 2048, y = lo / 2048 + 32 * (b.z % 64), z = b.z / 64 + 4 * b.w (lo = b.x + 256 * b.y), each then
//   sign-extended and scaled to -1..1.
void VertexTranslator::UnpackNormPacked(int reg, Vec result) {
    ShaderValue bx = b.Input(reg, 0), by = b.Input(reg, 1), bz = b.Input(reg, 2), bw = b.Input(reg, 3);
    ShaderValue q = b.Mul(b.Mad(by, Lit(256.0f), bx), Lit(1.0f / 2048.0f));
    ShaderValue sx = SignedFraction(q);
    ShaderValue x = b.Add(b.Mul(sx, Lit(2048.0f)), b.Mul(b.Sge(sx, Lit(0.5f)), Lit(-2048.0f)));
    ShaderValue qz = b.Mul(bz, Lit(1.0f / 64.0f));
    ShaderValue y = b.Mad(SignedFraction(qz), Lit(2048.0f), b.Floor(q));
    y = b.Mad(b.Sge(y, Lit(1024.0f)), Lit(-2048.0f), y);
    ShaderValue z = b.Mad(bw, Lit(4.0f), b.Floor(qz));
    z = b.Mad(b.Sge(z, Lit(512.0f)), Lit(-1024.0f), z);
    result[0] = b.Mul(x, Lit(1.0f / 1023.0f));
    result[1] = b.Mul(y, Lit(1.0f / 1023.0f));
    result[2] = b.Mul(z, Lit(1.0f / 511.0f));
    result[3] = Lit(1.0f);
}

void VertexTranslator::ReadSource(const VshDecoded &d, const VshSource &s, Vec result) {
    Vec reg;
    switch (s.mux) {
        case VSH_PARAM_R:
            for (int c = 0; c < 4; c++) reg[c] = R[s.tempReg][c];
            break;
        case VSH_PARAM_V:
            Input(d.input, reg);
            break;
        case VSH_PARAM_C:
            for (int c = 0; c < 4; c++)
                reg[c] = (d.a0x && a0Set) ? b.RelativeConstant(d.constIndex, c, a0) : b.Constant(d.constIndex, c);
            break;
        default:
            for (int c = 0; c < 4; c++) reg[c] = Lit(0.0f);
            break;
    }
    for (int c = 0; c < 4; c++) result[c] = s.negate ? ShaderBuilder::Negate(reg[s.swz[c]]) : reg[s.swz[c]];
}

// RCC: the reciprocal, clamped away from zero and infinity with its sign kept.
ShaderValue VertexTranslator::Rcc(ShaderValue x) {
    ShaderValue r = b.Rcp(x);
    ShaderValue positive = b.Min(b.Max(r, Lit(5.42101e-20f)), Lit(1.884467e19f));
    ShaderValue negative = b.Min(b.Max(r, Lit(-1.884467e19f)), Lit(-5.42101e-20f));
    return b.Lerp(negative, positive, b.Sge(r, Lit(0.0f)));
}

// What the host needs after the program: the position back to clip space with depth from w, the fog factor
// from the fog coordinate, and linear textures' coordinates scaled to 0..1.
void VertexTranslator::Epilogue() {
    Vec &pos = R[12];
    ShaderValue w = pos[3];
    // xy back to clip space; z from w (the W-buffer depth) in the a + b/w form the rasteriser interpolates
    // exactly, mapped so the depth-clip planes land on D3D9's 0..1 z range (reversed when the float depth
    // buffer is in use) - which both clips where the NV2A clipped and keeps the depth ordering.
    b.BeginVector();
    for (int c = 0; c < 2; c++) {
        b.Lane(c);
        b.SetOutput(ShaderBuilder::kOutPosition, c, b.Mul(b.Add(pos[c], b.Constant(C_HOST_ADJUST, c)), w));
    }
    b.EndVector();
    ShaderValue zFromW = b.Mul(b.Constant(C_DEPTH_CLIP, 0), b.Sub(w, b.Constant(C_DEPTH_CLIP, 1)));
    ShaderValue z = b.Mad(b.Constant(C_DEPTH_CLIP, 3), b.Mad(pos[2], Lit(-2.0f), Lit(1.0f)), pos[2]);   // z, or 1 - z
    ShaderValue zFromShader = b.Mul(z, w);
    b.SetOutput(ShaderBuilder::kOutPosition, 2, b.Lerp(zFromW, zFromShader, b.Constant(C_DEPTH_CLIP, 2)));
    b.SetOutput(ShaderBuilder::kOutPosition, 3, w);

    for (int c = 0; c < 4; c++) {
        b.SetOutput(ShaderBuilder::kOutDiffuse, c, out[OUT_D0][c]);
        b.SetOutput(ShaderBuilder::kOutSpecular, c, out[OUT_D1][c]);
    }

    // Fog: on the Xbox the shader's oFog is the fog coordinate (the game emits its normalised near..far
    // distance) and the fog table mode turns it into the factor; D3D9 takes the factor straight from the
    // shader, so the table function is applied here, as selects since vs_2_0 does not branch.
    if (outputsWritten & (1u << OUT_FOG)) {
        const float log2e = 1.44269504088896340736f;
        ShaderValue d = out[OUT_FOG][0];
        ShaderValue start = b.Constant(C_FOG, 0), end = b.Constant(C_FOG, 1), density = b.Constant(C_FOG, 2);
        ShaderValue mode = b.Constant(C_FOG, 3);
        ShaderValue linear = b.Mul(b.Sub(end, d), b.Rcp(b.Max(b.Sub(end, start), Lit(1e-6f))));
        ShaderValue exp1 = b.Exp(b.Mul(b.Mul(ShaderBuilder::Negate(d), density), Lit(log2e)));
        ShaderValue exp2 = b.Exp(b.Mul(b.Mul(b.Mul(d, d), b.Mul(density, density)), Lit(-log2e)));
        ShaderValue is[4];
        for (int m = 1; m <= 3; m++) {
            ShaderValue t = b.Add(mode, Lit((float)-m));
            ShaderValue square = b.Mul(t, t);
            is[m] = b.Sge(ShaderBuilder::Negate(square), square);   // mode == m
        }
        ShaderValue f = b.Lerp(d, linear, is[3]);
        f = b.Lerp(f, exp2, is[2]);
        f = b.Lerp(f, exp1, is[1]);
        b.SetOutput(ShaderBuilder::kOutFog, 0, b.Saturate(f));
    }
    if (outputsWritten & (1u << OUT_PTS))
        b.SetOutput(ShaderBuilder::kOutPointSize, 0, out[OUT_PTS][0]);

    for (int t = 0; t < 4; t++) {
        int scale = t < 2 ? C_TEX_SCALE01 : C_TEX_SCALE23;
        int first = (t & 1) * 2;
        b.BeginVector();
        for (int c = 0; c < 2; c++) {
            b.Lane(c);
            b.SetOutput(ShaderBuilder::kOutTexCoord0 + t, c, b.Mul(out[OUT_T0 + t][c], b.Constant(scale, first + c)));
        }
        b.EndVector();
        b.SetOutput(ShaderBuilder::kOutTexCoord0 + t, 2, out[OUT_T0 + t][2]);
        b.SetOutput(ShaderBuilder::kOutTexCoord0 + t, 3, out[OUT_T0 + t][3]);
    }
}

bool VertexTranslator::Translate(const uint8_t *function, uint32_t normPackedInputs, const uint8_t inputComponents[16],
                                 std::vector<uint32_t> *bytecode, Nv2aVertexShaderInfo *info, char *error, size_t errorSize) {
    if (function[0] != 0x78 || function[1] != 0x20) {
        snprintf(error, errorSize, "not an NV2A vertex program (header %02x %02x)", function[0], function[1]);
        return false;
    }
    int count = function[2];
    info->instructionCount = count;
    const uint32_t *words = (const uint32_t *)(function + 8);
    normPacked = normPackedInputs;
    components = inputComponents;
    memset(inputBuilt, 0, sizeof(inputBuilt));
    inputsUsed = 0;
    outputsWritten = 1;
    a0Set = false;
    a0 = Lit(0.0f);

    // Temporaries start at zero; output registers at (0, 0, 0, 1) for the texture coordinates, whose w matters
    // because texture stages divide by q, and zero otherwise.
    for (int r = 0; r < 13; r++) for (int c = 0; c < 4; c++) R[r][c] = Lit(0.0f);
    for (int o = 0; o < OUT_COUNT; o++)
        for (int c = 0; c < 4; c++) out[o][c] = Lit(o >= OUT_T0 && c == 3 ? 1.0f : 0.0f);

    for (int i = 0; i < count; i++) {
        VshDecoded d;
        Nv2aVertexShader_Decode(words + i * 4, &d);
        if (d.mac == VSH_MAC_NOP && d.ilu == VSH_ILU_NOP)
            continue;
        const VshSource *sources[3] = { &d.a, &d.b, &d.c };
        for (int s = 0; s < 3; s++) {
            if (sources[s]->mux == VSH_PARAM_C && d.constIndex >= 192 && !d.a0x) {
                snprintf(error, errorSize, "instruction %d reads c%d, past the 192 constants", i, d.constIndex);
                return false;
            }
            if (sources[s]->mux == VSH_PARAM_R && sources[s]->tempReg > 12) {
                snprintf(error, errorSize, "instruction %d reads R%d", i, sources[s]->tempReg);
                return false;
            }
        }
        // Both units read their operands before either writes (they execute in parallel on the hardware).
        Vec A, B, C, M, I;
        ReadSource(d, d.a, A);
        ReadSource(d, d.b, B);
        ReadSource(d, d.c, C);
        b.BeginVector();   // the MAC operation is one vector operation (see ShaderBuilder::BeginVector)
        switch (d.mac) {
            case VSH_MAC_NOP: break;
            case VSH_MAC_MOV: case VSH_MAC_ARL: for (int c = 0; c < 4; c++) M[c] = A[c]; break;
            case VSH_MAC_MUL: for (int c = 0; c < 4; c++) { b.Lane(c); M[c] = b.Mul(A[c], B[c]); } break;
            case VSH_MAC_ADD: for (int c = 0; c < 4; c++) { b.Lane(c); M[c] = b.Add(A[c], C[c]); } break;
            case VSH_MAC_MAD: for (int c = 0; c < 4; c++) { b.Lane(c); M[c] = b.Add(b.Mul(A[c], B[c]), C[c]); } break;
            case VSH_MAC_DP3: M[0] = b.Dp3(A, B); M[1] = M[2] = M[3] = M[0]; break;
            case VSH_MAC_DPH: M[0] = b.Add(b.Dp3(A, B), B[3]); M[1] = M[2] = M[3] = M[0]; break;
            case VSH_MAC_DP4: M[0] = b.Dp4(A, B); M[1] = M[2] = M[3] = M[0]; break;
            case VSH_MAC_DST: b.Lane(1); M[0] = Lit(1.0f); M[1] = b.Mul(A[1], B[1]); M[2] = A[2]; M[3] = B[3]; break;
            case VSH_MAC_MIN: for (int c = 0; c < 4; c++) { b.Lane(c); M[c] = b.Min(A[c], B[c]); } break;
            case VSH_MAC_MAX: for (int c = 0; c < 4; c++) { b.Lane(c); M[c] = b.Max(A[c], B[c]); } break;
            case VSH_MAC_SLT: for (int c = 0; c < 4; c++) { b.Lane(c); M[c] = b.Slt(A[c], B[c]); } break;
            case VSH_MAC_SGE: for (int c = 0; c < 4; c++) { b.Lane(c); M[c] = b.Sge(A[c], B[c]); } break;
            default:
                snprintf(error, errorSize, "instruction %d: MAC operation %d", i, d.mac);
                return false;
        }
        b.EndVector();
        switch (d.ilu) {
            case VSH_ILU_NOP: break;
            case VSH_ILU_MOV: for (int c = 0; c < 4; c++) I[c] = C[c]; break;
            case VSH_ILU_RCP: I[0] = I[1] = I[2] = I[3] = b.Rcp(C[0]); break;
            case VSH_ILU_RCC: I[0] = I[1] = I[2] = I[3] = Rcc(C[0]); break;
            case VSH_ILU_RSQ: I[0] = I[1] = I[2] = I[3] = b.Rsq(b.Abs(C[0])); break;
            case VSH_ILU_EXP:
                I[0] = b.Exp(b.Floor(C[0])); I[1] = b.Frc(C[0]); I[2] = b.Exp(C[0]); I[3] = Lit(1.0f);
                break;
            case VSH_ILU_LOG: {
                ShaderValue x = b.Abs(C[0]), log = b.Log(x), exponent = b.Floor(log);
                I[0] = exponent; I[1] = b.Mul(b.Rcp(b.Exp(exponent)), x); I[2] = log; I[3] = Lit(1.0f);
                break;
            }
            case VSH_ILU_LIT: b.Lit(C[0], C[1], C[3], I); break;
            default:
                snprintf(error, errorSize, "instruction %d: ILU operation %d", i, d.ilu);
                return false;
        }
        if (d.mac == VSH_MAC_ARL) {
            a0 = b.Floor(M[0]);
            a0Set = true;
        } else if (d.mac != VSH_MAC_NOP) {
            if (d.outMacMask) {
                if (d.outTempReg > 11) {
                    snprintf(error, errorSize, "instruction %d writes R%d", i, d.outTempReg);
                    return false;
                }
                for (int c = 0; c < 4; c++) if (d.outMacMask & (8 >> c)) { R[d.outTempReg][c] = M[c]; b.Hold(M[c], d.outTempReg, c); }
            }
            if (d.outOMask && d.outMux == 0) {
                if (!d.outIsOutput || d.outAddress >= OUT_COUNT || d.outAddress == 1 || d.outAddress == 2) {
                    snprintf(error, errorSize, "instruction %d writes output address %d", i, d.outAddress);
                    return false;
                }
                Vec &o = d.outAddress == OUT_POS ? R[12] : out[d.outAddress];
                for (int c = 0; c < 4; c++) if (d.outOMask & (8 >> c)) { o[c] = M[c]; b.Hold(M[c], 16 + d.outAddress, c); }
                outputsWritten |= 1u << d.outAddress;
            }
        }
        if (d.ilu != VSH_ILU_NOP) {
            if (d.outIluMask) {
                int reg = (d.mac != VSH_MAC_NOP) ? 1 : d.outTempReg;   // paired ILU results land in R1
                if (reg > 11) {
                    snprintf(error, errorSize, "instruction %d writes R%d", i, reg);
                    return false;
                }
                for (int c = 0; c < 4; c++) if (d.outIluMask & (8 >> c)) { R[reg][c] = I[c]; b.Hold(I[c], reg, c); }
            }
            if (d.outOMask && d.outMux == 1) {
                if (!d.outIsOutput || d.outAddress >= OUT_COUNT || d.outAddress == 1 || d.outAddress == 2) {
                    snprintf(error, errorSize, "instruction %d writes output address %d", i, d.outAddress);
                    return false;
                }
                Vec &o = d.outAddress == OUT_POS ? R[12] : out[d.outAddress];
                for (int c = 0; c < 4; c++) if (d.outOMask & (8 >> c)) { o[c] = I[c]; b.Hold(I[c], 16 + d.outAddress, c); }
                outputsWritten |= 1u << d.outAddress;
            }
        }
        if (d.final)
            break;
    }

    Epilogue();
    info->inputsUsed = inputsUsed;
    info->outputsWritten = outputsWritten;
    if (spirv != NULL)
        return b.AssembleSpirv(*spirv, bytecode, spirvInfo, error, errorSize);
    return b.Assemble(bytecode, error, errorSize);
}

} // namespace

bool Nv2aVertexShader_Translate(const uint8_t *function, uint32_t normPackedInputs, const uint8_t inputComponents[16],
                                std::vector<uint32_t> *bytecode, Nv2aVertexShaderInfo *info, char *error, size_t errorSize) {
    VertexTranslator *t = new VertexTranslator();   // the builder's tables are large for the stack
    bool ok = t->Translate(function, normPackedInputs, inputComponents, bytecode, info, error, errorSize);
    delete t;
    return ok;
}

bool Nv2aVertexShader_TranslateSpirv(const uint8_t *function, uint32_t normPackedInputs, const uint8_t inputComponents[16],
                                     const ShaderSpirvOptions &options, std::vector<uint32_t> *words,
                                     Nv2aVertexShaderInfo *info, ShaderSpirvInfo *spirvInfo, char *error, size_t errorSize) {
    VertexTranslator *t = new VertexTranslator();
    t->spirv = &options;
    t->spirvInfo = spirvInfo;
    bool ok = t->Translate(function, normPackedInputs, inputComponents, words, info, error, errorSize);
    delete t;
    return ok;
}
