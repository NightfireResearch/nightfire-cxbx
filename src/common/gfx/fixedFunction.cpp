#include "fixedFunction.h"

#include "shaderBuilder.h"
#include "shaderSpirv.h"

#include <string.h>

// See the header.

namespace {

// D3DTEXTUREOP and D3DTA_* (d3d9types.h).
enum {
    TOP_DISABLE = 1, TOP_SELECTARG1, TOP_SELECTARG2, TOP_MODULATE, TOP_MODULATE2X, TOP_MODULATE4X, TOP_ADD,
    TOP_ADDSIGNED, TOP_ADDSIGNED2X, TOP_SUBTRACT, TOP_ADDSMOOTH, TOP_BLENDDIFFUSEALPHA, TOP_BLENDTEXTUREALPHA,
    TOP_BLENDFACTORALPHA, TOP_BLENDTEXTUREALPHAPM, TOP_BLENDCURRENTALPHA, TOP_PREMODULATE,
    TOP_MODULATEALPHA_ADDCOLOR, TOP_MODULATECOLOR_ADDALPHA, TOP_MODULATEINVALPHA_ADDCOLOR,
    TOP_MODULATEINVCOLOR_ADDALPHA, TOP_BUMPENVMAP, TOP_BUMPENVMAPLUMINANCE, TOP_DOTPRODUCT3, TOP_MULTIPLYADD, TOP_LERP,
};
enum { TA_DIFFUSE = 0, TA_CURRENT = 1, TA_TEXTURE = 2, TA_TFACTOR = 3, TA_SPECULAR = 4, TA_TEMP = 5,
       TA_SELECTMASK = 0x0F, TA_COMPLEMENT = 0x10, TA_ALPHAREPLICATE = 0x20 };

typedef ShaderValue Vec[4];

class StageProgram {
public:
    StageProgram() : b(ShaderBuilder::kPixelShader) {}
    bool Build(const FixedPixelKey &key, std::vector<uint32_t> *words, ShaderSpirvInfo *info, char *error, size_t errorSize);

private:
    ShaderValue Lit(float f) { return b.Literal(f); }
    void Argument(uint8_t arg, Vec out);
    ShaderValue Operation(uint8_t op, int c, const Vec a0, const Vec a1, const Vec a2);

    ShaderBuilder b;
    Vec diffuse, specular, current, texture, temp;
};

// An argument's four channels. TFACTOR is D3D9's default (white): the D3D9 backend never set D3DRS_TEXTUREFACTOR.
void StageProgram::Argument(uint8_t arg, Vec out) {
    const ShaderValue *source;
    Vec white = { Lit(1.0f), Lit(1.0f), Lit(1.0f), Lit(1.0f) };
    switch (arg & TA_SELECTMASK) {
        case TA_DIFFUSE: source = diffuse; break;
        case TA_CURRENT: source = current; break;
        case TA_TEXTURE: source = texture; break;
        case TA_SPECULAR: source = specular; break;
        case TA_TEMP: source = temp; break;
        default: source = white; break;
    }
    for (int c = 0; c < 4; c++) out[c] = source[(arg & TA_ALPHAREPLICATE) ? 3 : c];
    if (arg & TA_COMPLEMENT)
        for (int c = 0; c < 4; c++) out[c] = b.Sub(Lit(1.0f), out[c]);
}

// One channel of a stage's operation. a1 and a2 are COLORARG1/2 (or ALPHAARG1/2), a0 the third argument.
ShaderValue StageProgram::Operation(uint8_t op, int c, const Vec a0, const Vec a1, const Vec a2) {
    switch (op) {
        case TOP_SELECTARG1: return a1[c];
        case TOP_SELECTARG2: return a2[c];
        case TOP_MODULATE: return b.Mul(a1[c], a2[c]);
        case TOP_MODULATE2X: return b.Mul(b.Mul(a1[c], a2[c]), Lit(2.0f));
        case TOP_MODULATE4X: return b.Mul(b.Mul(a1[c], a2[c]), Lit(4.0f));
        case TOP_ADD: return b.Add(a1[c], a2[c]);
        case TOP_ADDSIGNED: return b.Sub(b.Add(a1[c], a2[c]), Lit(0.5f));
        case TOP_ADDSIGNED2X: return b.Mul(b.Sub(b.Add(a1[c], a2[c]), Lit(0.5f)), Lit(2.0f));
        case TOP_SUBTRACT: return b.Sub(a1[c], a2[c]);
        case TOP_ADDSMOOTH: return b.Sub(b.Add(a1[c], a2[c]), b.Mul(a1[c], a2[c]));
        case TOP_BLENDDIFFUSEALPHA: return b.Lerp(a2[c], a1[c], diffuse[3]);
        case TOP_BLENDTEXTUREALPHA: return b.Lerp(a2[c], a1[c], texture[3]);
        case TOP_BLENDFACTORALPHA: return a1[c];   // the factor's alpha is 1
        case TOP_BLENDTEXTUREALPHAPM: return b.Add(a1[c], b.Mul(a2[c], b.Sub(Lit(1.0f), texture[3])));
        case TOP_BLENDCURRENTALPHA: return b.Lerp(a2[c], a1[c], current[3]);
        case TOP_MODULATEALPHA_ADDCOLOR: return c == 3 ? a1[c] : b.Add(a1[c], b.Mul(a1[3], a2[c]));
        case TOP_MODULATECOLOR_ADDALPHA: return c == 3 ? a1[c] : b.Add(b.Mul(a1[c], a2[c]), a1[3]);
        case TOP_MODULATEINVALPHA_ADDCOLOR: return c == 3 ? a1[c] : b.Add(b.Mul(b.Sub(Lit(1.0f), a1[3]), a2[c]), a1[c]);
        case TOP_MODULATEINVCOLOR_ADDALPHA: return c == 3 ? a1[c] : b.Add(b.Mul(b.Sub(Lit(1.0f), a1[c]), a2[c]), a1[3]);
        case TOP_DOTPRODUCT3: {
            ShaderValue x[3], y[3];
            for (int k = 0; k < 3; k++) { x[k] = b.Sub(a1[k], Lit(0.5f)); y[k] = b.Sub(a2[k], Lit(0.5f)); }
            return b.Mul(b.Dp3(x, y), Lit(4.0f));
        }
        case TOP_MULTIPLYADD: return b.Add(a0[c], b.Mul(a1[c], a2[c]));
        case TOP_LERP: return b.Lerp(a2[c], a1[c], a0[c]);
        default: return a1[c];   // PREMODULATE and the bump modes: what they pass on unchanged
    }
}

bool StageProgram::Build(const FixedPixelKey &key, std::vector<uint32_t> *words, ShaderSpirvInfo *info, char *error,
                         size_t errorSize) {
    for (int c = 0; c < 4; c++) {
        diffuse[c] = b.Input(0, c);
        specular[c] = b.Input(1, c);
        current[c] = diffuse[c];
        temp[c] = Lit(0.0f);
    }
    for (int s = 0; s < key.stageCount && s < 4; s++) {
        const FixedStage &stage = key.stages[s];
        if (stage.colourOp == TOP_DISABLE || stage.colourOp == 0)
            break;
        ShaderValue coord[2];
        if (stage.projected) {
            ShaderValue q = b.Rcp(b.TexCoord(stage.texCoord, 3));
            for (int c = 0; c < 2; c++) coord[c] = b.Mul(b.TexCoord(stage.texCoord, c), q);
        } else {
            for (int c = 0; c < 2; c++) coord[c] = b.TexCoord(stage.texCoord, c);
        }
        b.Sample(s, false, coord, texture);
        Vec arg[3], alphaArg[3];
        for (int k = 0; k < 3; k++) {
            Argument(stage.colourArg[k], arg[k]);
            Argument(stage.alphaArg[k], alphaArg[k]);
        }
        Vec result;
        for (int c = 0; c < 3; c++)
            result[c] = b.Saturate(Operation(stage.colourOp, c, arg[0], arg[1], arg[2]));
        // ALPHAOP DISABLE passes the current alpha on
        bool alphaOn = stage.alphaOp != TOP_DISABLE && stage.alphaOp != 0;
        result[3] = alphaOn ? b.Saturate(Operation(stage.alphaOp, 3, alphaArg[0], alphaArg[1], alphaArg[2])) : current[3];
        if (stage.colourOp == TOP_DOTPRODUCT3 && stage.alphaOp == TOP_DOTPRODUCT3)
            result[3] = result[0];
        for (int c = 0; c < 4; c++) current[c] = result[c];
    }
    for (int c = 0; c < 4; c++) b.SetOutput(ShaderBuilder::kOutColour, c, current[c]);
    ShaderSpirvOptions options;
    memset(&options, 0, sizeof(options));
    options.hostPixelEnd = true;
    return b.AssembleSpirv(options, words, info, error, errorSize);
}

} // namespace

bool FixedFunction_PixelProgram(const FixedPixelKey &key, std::vector<uint32_t> *words, ShaderSpirvInfo *info,
                                char *error, size_t errorSize) {
    StageProgram *program = new StageProgram();   // the builder's tables are large for the stack
    bool ok = program->Build(key, words, info, error, errorSize);
    delete program;
    return ok;
}

// (x * 2/width - 1, 1 - y * 2/height, z, 1) / rhw: the screen position back in clip space, with 1/rhw as w so
// that the attributes interpolate as D3D9 interpolated them.
bool FixedFunction_PretransformedProgram(std::vector<uint32_t> *words, uint8_t inputKind[16], char *error,
                                         size_t errorSize) {
    enum { kPosition = 0, kColour = 3, kTexCoord = 9, kHostAdjust = 200 };
    ShaderBuilder *b = new ShaderBuilder(ShaderBuilder::kVertexShader);
    ShaderValue w = b->Rcp(b->Input(kPosition, 3));
    ShaderValue x = b->Sub(b->Mul(b->Input(kPosition, 0), b->Constant(kHostAdjust, 2)), b->Literal(1.0f));
    ShaderValue y = b->Sub(b->Literal(1.0f), b->Mul(b->Input(kPosition, 1), b->Constant(kHostAdjust, 3)));
    b->SetOutput(ShaderBuilder::kOutPosition, 0, b->Mul(x, w));
    b->SetOutput(ShaderBuilder::kOutPosition, 1, b->Mul(y, w));
    b->SetOutput(ShaderBuilder::kOutPosition, 2, b->Mul(b->Input(kPosition, 2), w));
    b->SetOutput(ShaderBuilder::kOutPosition, 3, w);
    for (int c = 0; c < 4; c++) {
        b->SetOutput(ShaderBuilder::kOutDiffuse, c, b->Input(kColour, c));
        b->SetOutput(ShaderBuilder::kOutTexCoord0, c, b->Input(kTexCoord, c));
    }
    ShaderSpirvOptions options;
    memset(&options, 0, sizeof(options));
    options.inputKind[kColour] = kSpirvColour;
    memcpy(inputKind, options.inputKind, 16);
    ShaderSpirvInfo info;
    bool ok = b->AssembleSpirv(options, words, &info, error, errorSize);
    delete b;
    return ok;
}

bool FixedFunction_ScreenProgram(std::vector<uint32_t> *words, uint8_t inputKind[16], char *error, size_t errorSize) {
    enum { kPosition = 0, kTexCoord = 9 };
    ShaderBuilder *b = new ShaderBuilder(ShaderBuilder::kVertexShader);
    for (int c = 0; c < 4; c++) {
        b->SetOutput(ShaderBuilder::kOutPosition, c, b->Input(kPosition, c));
        b->SetOutput(ShaderBuilder::kOutTexCoord0, c, c < 2 ? b->Input(kTexCoord, c) : b->Literal(c == 3 ? 1.0f : 0.0f));
    }
    ShaderSpirvOptions options;
    memset(&options, 0, sizeof(options));
    memcpy(inputKind, options.inputKind, 16);
    ShaderSpirvInfo info;
    bool ok = b->AssembleSpirv(options, words, &info, error, errorSize);
    delete b;
    return ok;
}
