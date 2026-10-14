#ifndef DRIVING_GFX_D3D8SHADERS_H_
#define DRIVING_GFX_D3D8SHADERS_H_

// The Xbox D3D8's shader formats, for the programs EAGL carries (EaglFont.cpp, RenderMethod.cpp): vertex shader
// declarations (D3DVSD tokens), vertex shader functions (NV2A microcode, assembled here from the XDK's vertex
// shader assembly) and pixel shader definitions (D3DPIXELSHADERDEF, the register combiners' values). Names and
// encodings are the XDK's (d3d8types.h); the graphics backend translates the words to HLSL at run time.

#include <array>
#include <stddef.h>
#include <stdint.h>

// ---- Vertex shader declarations

enum : uint32_t {
    D3DVSDT_FLOAT2 = 0x22,
    D3DVSDT_FLOAT3 = 0x32,
    D3DVSDT_D3DCOLOR = 0x40,
};

enum : uint32_t {
    D3DVSDE_POSITION = 0,
    D3DVSDE_DIFFUSE = 3,
    D3DVSDE_TEXCOORD0 = 9,
};

constexpr uint32_t D3DVSD_STREAM(uint32_t stream) { return 0x20000000 | stream; }
constexpr uint32_t D3DVSD_REG(uint32_t reg, uint32_t type) { return 0x40000000 | type << 16 | reg; }
constexpr uint32_t D3DVSD_END() { return 0xffffffff; }

// ---- Vertex shader functions

// A program is written in the XDK's assembly language with these, inside a function that says
// `using namespace Vsh;`: `Dp4(oPos.x(), v(0), c(0))` for "dp4 oPos.x, v0, c0".
namespace Vsh {

// The NV2A's instruction set: a MAC operation and an ILU operation, which one instruction may pair
enum Mac : uint32_t { kMacNop, kMacMov, kMacMul, kMacAdd, kMacMad, kMacDp3, kMacDph, kMacDp4, kMacDst, kMacMin,
                      kMacMax, kMacSlt, kMacSge, kMacArl };
enum Ilu : uint32_t { kIluNop, kIluMov, kIluRcp, kIluRcc, kIluRsq, kIluExp, kIluLog, kIluLit };

// A source operand: a temporary (r), an input (v) or a constant (c), swizzled
struct Source {
    enum Mux : uint32_t { kTemporary = 1, kInput = 2, kConstant = 3 };
    uint32_t mux;
    uint32_t index;
    uint32_t swizzle[4];             // the component each of x, y, z and w reads: 0 x .. 3 w

    constexpr Source Replicate(uint32_t component) const {
        return { mux, index, { component, component, component, component } };
    }
    constexpr Source x() const { return Replicate(0); }
    constexpr Source w() const { return Replicate(3); }
};

constexpr Source r(uint32_t n) { return { Source::kTemporary, n, { 0, 1, 2, 3 } }; }
constexpr Source v(uint32_t n) { return { Source::kInput, n, { 0, 1, 2, 3 } }; }
constexpr Source c(int n) { return { Source::kConstant, uint32_t(n + 96), { 0, 1, 2, 3 } }; }   // c-96..c95

// A destination: an output register, or the temporary the ILU writes, with a write mask (x 8, y 4, z 2, w 1)
struct Destination {
    bool output;
    uint32_t index;
    uint32_t mask;

    constexpr Destination Masked(uint32_t components) const { return { output, index, components }; }
    constexpr Destination x() const { return Masked(8); }
    constexpr Destination y() const { return Masked(4); }
    constexpr Destination z() const { return Masked(2); }
    constexpr Destination w() const { return Masked(1); }
    constexpr Destination xyz() const { return Masked(14); }
};

constexpr Destination oPos = { true, 0, 15 };
constexpr Destination oD0 = { true, 3, 15 };
constexpr Destination oFog = { true, 5, 15 };
constexpr Destination oT0 = { true, 9, 15 };
constexpr Destination r1 = { false, 1, 15 };

struct Instruction {
    Mac mac;
    Ilu ilu;
    Destination macOut;              // an output register
    Destination iluOut;              // r1
    Source a, b, c;                  // the MAC reads a, b and c; the ILU reads c
};

constexpr Source kUnused = v(0);     // an operand the operation does not read

constexpr Instruction Mov(Destination out, Source a) { return { kMacMov, kIluNop, out, {}, a, kUnused, kUnused }; }
constexpr Instruction Mul(Destination out, Source a, Source b) { return { kMacMul, kIluNop, out, {}, a, b, kUnused }; }
constexpr Instruction Mad(Destination out, Source a, Source b, Source c) { return { kMacMad, kIluNop, out, {}, a, b, c }; }
constexpr Instruction Dp4(Destination out, Source a, Source b) { return { kMacDp4, kIluNop, out, {}, a, b, kUnused }; }
constexpr Instruction Rcc(Destination out, Source c) { return { kMacNop, kIluRcc, {}, out, kUnused, kUnused, c }; }
// "mul ... + rcc ...": a MAC operation and an ILU operation in one instruction
constexpr Instruction operator+(Instruction mac, Instruction ilu) {
    return { mac.mac, ilu.ilu, mac.macOut, ilu.iluOut, mac.a, mac.b, ilu.c };
}

// The instruction's three meaningful words
constexpr std::array<uint32_t, 3> Encode(const Instruction &in, bool final) {
    uint32_t input = 0, constant = 0;
    for (const Source *s : { &in.a, &in.b, &in.c }) {
        if (s->mux == Source::kInput && s->index > input)
            input = s->index;
        if (s->mux == Source::kConstant)
            constant = s->index;
    }
    auto temp = [](const Source &s) { return s.mux == Source::kTemporary ? s.index : 0u; };
    auto swizzle = [](const Source &s, int shift) {
        return s.swizzle[0] << shift | s.swizzle[1] << (shift - 2) | s.swizzle[2] << (shift - 4) |
               s.swizzle[3] << (shift - 6);
    };
    uint32_t word1 = in.ilu << 25 | in.mac << 21 | constant << 13 | input << 9 | swizzle(in.a, 6);
    uint32_t word2 = temp(in.a) << 28 | in.a.mux << 26 | swizzle(in.b, 23) | temp(in.b) << 13 | in.b.mux << 11 |
                     swizzle(in.c, 8) | temp(in.c) >> 2;
    uint32_t word3 = (temp(in.c) & 3) << 30 | in.c.mux << 28 | 7u << 20 | in.iluOut.mask << 16 |
                     in.macOut.mask << 12 | 1u << 11 | in.macOut.index << 3 | (final ? 1u : 0u);
    return { word1, word2, word3 };
}

// An XDK vertex shader function: the header {0x78, 0x20, instruction count, 0} and a zero word, then each
// instruction as its three words and an unused zero. Words may leave off the last instruction's unused word.
template <size_t Words, size_t N>
constexpr std::array<uint32_t, Words> Function(const Instruction (&program)[N]) {
    static_assert(Words == 2 + 4 * N || Words == 1 + 4 * N, "a header and four words an instruction");
    std::array<uint32_t, Words> out{};
    out[0] = 0x2078 | uint32_t(N) << 16;
    for (size_t i = 0; i < N; i++) {
        std::array<uint32_t, 3> words = Encode(program[i], i == N - 1);
        for (size_t k = 0; k < 3; k++)
            out[2 + 4 * i + k] = words[k];
    }
    return out;
}

}  // namespace Vsh

// ---- Pixel shader definitions

// D3DPIXELSHADERDEF: the register combiners' state, eight general stages and the final combiner
struct D3DPixelShaderDef {           // 0xf0
    uint32_t alphaInputs[8];         // +0x00
    uint32_t finalCombinerInputsABCD;   // +0x20
    uint32_t finalCombinerInputsEFG;    // +0x24
    uint32_t constant0[8];           // +0x28
    uint32_t constant1[8];           // +0x48
    uint32_t alphaOutputs[8];        // +0x68
    uint32_t rgbInputs[8];           // +0x88
    uint32_t compareMode;            // +0xa8
    uint32_t finalCombinerConstant0; // +0xac
    uint32_t finalCombinerConstant1; // +0xb0
    uint32_t rgbOutputs[8];          // +0xb4
    uint32_t combinerCount;          // +0xd4
    uint32_t textureModes;           // +0xd8
    uint32_t dotMapping;             // +0xdc
    uint32_t inputTexture;           // +0xe0
    uint32_t c0Mapping;              // +0xe4
    uint32_t c1Mapping;              // +0xe8
    uint32_t finalCombinerConstants; // +0xec
};
static_assert(sizeof(D3DPixelShaderDef) == 0xf0, "D3DPIXELSHADERDEF is 60 dwords");
static_assert(offsetof(D3DPixelShaderDef, alphaOutputs) == 0x68 && offsetof(D3DPixelShaderDef, rgbInputs) == 0x88 &&
              offsetof(D3DPixelShaderDef, rgbOutputs) == 0xb4 && offsetof(D3DPixelShaderDef, combinerCount) == 0xd4,
              "D3DPIXELSHADERDEF layout");

enum : uint32_t {
    PS_REGISTER_ZERO = 0x0,
    PS_REGISTER_DISCARD = 0x0,
    PS_REGISTER_C0 = 0x1,
    PS_REGISTER_C1 = 0x2,
    PS_REGISTER_FOG = 0x3,
    PS_REGISTER_V0 = 0x4,
    PS_REGISTER_V1 = 0x5,
    PS_REGISTER_T0 = 0x8,
    PS_REGISTER_T1 = 0x9,
    PS_REGISTER_T2 = 0xa,
    PS_REGISTER_T3 = 0xb,
    PS_REGISTER_R0 = 0xc,
    PS_REGISTER_R1 = 0xd,
    PS_REGISTER_ONE = 0x20,          // zero, unsigned-inverted
};

enum : uint32_t {
    PS_CHANNEL_RGB = 0x00,
    PS_CHANNEL_ALPHA = 0x10,
};

enum : uint32_t {
    PS_INPUTMAPPING_UNSIGNED_IDENTITY = 0x00,
    PS_INPUTMAPPING_UNSIGNED_INVERT = 0x20,
    PS_INPUTMAPPING_SIGNED_IDENTITY = 0xc0,
};

enum : uint32_t { PS_COMBINEROUTPUT_IDENTITY = 0x00 };

enum : uint32_t {
    PS_COMBINERCOUNT_MUX_MSB = 0x0001,
    PS_COMBINERCOUNT_UNIQUE_C0 = 0x0010,
    PS_COMBINERCOUNT_UNIQUE_C1 = 0x0100,
};

enum : uint32_t {
    PS_TEXTUREMODES_NONE = 0x00,
    PS_TEXTUREMODES_PROJECT2D = 0x01,
};

enum : uint32_t { PS_GLOBALFLAGS_TEXMODE_ADJUST = 0x0001 };

constexpr uint32_t PS_COMBINERINPUTS(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return a << 24 | b << 16 | c << 8 | d;
}
constexpr uint32_t PS_COMBINEROUTPUTS(uint32_t ab, uint32_t cd, uint32_t sum, uint32_t flags) {
    return flags << 12 | sum << 8 | ab << 4 | cd;
}
constexpr uint32_t PS_COMBINERCOUNT(uint32_t count, uint32_t flags) { return flags << 8 | count; }
constexpr uint32_t PS_TEXTUREMODES(uint32_t t0, uint32_t t1, uint32_t t2, uint32_t t3) {
    return t3 << 15 | t2 << 10 | t1 << 5 | t0;
}
// Which D3D constant each stage's C0 (or C1) is, a nibble a stage; 0xf none
constexpr uint32_t PS_CONSTANTMAPPING(uint32_t s0, uint32_t s1, uint32_t s2, uint32_t s3, uint32_t s4, uint32_t s5,
                                      uint32_t s6, uint32_t s7) {
    return s0 | s1 << 4 | s2 << 8 | s3 << 12 | s4 << 16 | s5 << 20 | s6 << 24 | s7 << 28;
}
constexpr uint32_t PS_FINALCOMBINERCONSTANTS(uint32_t c0, uint32_t c1, uint32_t flags) {
    return flags << 8 | c1 << 4 | c0;
}

#endif // DRIVING_GFX_D3D8SHADERS_H_
