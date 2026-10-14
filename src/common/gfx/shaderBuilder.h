#ifndef COMMON_GFX_SHADERBUILDER_H_
#define COMMON_GFX_SHADERBUILDER_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// Direct3D 9 shader bytecode (vs_2_0 / ps_2_0) from scalar expressions, with no HLSL compiler.
//
// The NV2A translators (nv2aVertexShader.cpp, nv2aPixelShader.cpp) describe a program as values: each one a
// scalar - an input, a constant, a literal, or an operation on other values - and say which values the
// program outputs. Assemble then picks instructions, packs values into the components of temporary registers,
// places literals in def'd constants, and writes the tokens.
//
// The programs these replace were HLSL compiled by Microsoft's compiler, and the frames the game draws are
// compared bit for bit, so the builder rounds where that compiler rounded: a multiply feeding an add is
// fused into a mad (the left operand first, as the compiler does), a lerp is a mad of the difference
// (an lrp when the difference stays an add), and folds are only the exact ones (x*1, x+0, x*0, literal
// arithmetic). Instruction order, register allocation and swizzles cannot change a result and are the
// builder's own. docs/architecture/shader-bytecode.md has the rules and how they were checked.
// ---------------------------------------------------------------------------------------------------------------

// A scalar value: a node of the program and whether it is read negated.
struct ShaderValue {
    uint16_t node;
    bool negate;
};

class ShaderBuilder {
public:
    enum Profile { kVertexShader, kPixelShader };
    // vs_2_0 output registers (RASTOUT, ATTROUT and TEXCRDOUT in the token encoding).
    enum Output { kOutPosition, kOutFog, kOutPointSize, kOutDiffuse, kOutSpecular, kOutTexCoord0, kOutColour = 16 };

    explicit ShaderBuilder(Profile profile);

    // One operation of the source over a vector: between BeginVector and EndVector, each lane's expression
    // follows a Lane(lane) call, and the n-th node a lane makes belongs to the same vector operation as the n-th of
    // every other lane. Outside, every node is an operation of its own. The compiler fuses a multiply into a
    // sum per vector instruction, so this is what decides which lanes' products fuse (see Assemble).
    void BeginVector();
    void Lane(int lane);
    void EndVector();

    // Leaves.
    ShaderValue Literal(float value);
    ShaderValue Input(int reg, int component);          // v#: a vertex input, or a pixel shader's colour
    ShaderValue TexCoord(int reg, int component);       // t#: a pixel shader's texture coordinate
    ShaderValue Constant(int reg, int component);       // c#
    ShaderValue RelativeConstant(int base, int component, ShaderValue address);   // c[a0.x + base]; address integral

    // Arithmetic. Each is one instruction of the profile, with the folds the header describes.
    static ShaderValue Negate(ShaderValue a) { a.negate = !a.negate; return a; }
    ShaderValue Add(ShaderValue a, ShaderValue b);
    ShaderValue Sub(ShaderValue a, ShaderValue b) { return Add(a, Negate(b)); }
    ShaderValue Mul(ShaderValue a, ShaderValue b);
    ShaderValue Mad(ShaderValue a, ShaderValue b, ShaderValue c);
    ShaderValue Dp3(const ShaderValue a[3], const ShaderValue b[3]);
    ShaderValue Dp4(const ShaderValue a[4], const ShaderValue b[4]);
    ShaderValue Min(ShaderValue a, ShaderValue b);
    ShaderValue Max(ShaderValue a, ShaderValue b);
    ShaderValue Slt(ShaderValue a, ShaderValue b);       // vertex shaders: a < b ? 1 : 0
    ShaderValue Sge(ShaderValue a, ShaderValue b);       // vertex shaders: a >= b ? 1 : 0
    ShaderValue Cmp(ShaderValue a, ShaderValue b, ShaderValue c);   // pixel shaders: a >= 0 ? b : c
    ShaderValue Rcp(ShaderValue a);
    ShaderValue Rsq(ShaderValue a);
    ShaderValue Frc(ShaderValue a);
    ShaderValue Floor(ShaderValue a);                    // a - frc(a), whose add never fuses (HLSL's floor)
    ShaderValue Abs(ShaderValue a);
    ShaderValue Exp(ShaderValue a);                      // 2^a, full precision
    ShaderValue Log(ShaderValue a);                      // log2 |a|, full precision
    ShaderValue Saturate(ShaderValue a);                 // pixel shaders: the _sat modifier; vertex: max, min
    ShaderValue Lerp(ShaderValue a, ShaderValue b, ShaderValue t);  // a + t * (b - a)
    void Lit(ShaderValue x, ShaderValue y, ShaderValue w, ShaderValue out[4]);
    // Pixel shaders: texld from a sampler at (u, v) or, for a cube map, (u, v, w).
    void Sample(int sampler, bool cube, const ShaderValue *coord, ShaderValue out[4]);

    // Says that a value is written to lane lane of the source program's register variable. The compiler keeps
    // a variable in one temporary, and the order of a dp's terms follows the components it reads, so the
    // values of one variable are kept together as it kept them.
    void Hold(ShaderValue value, int variable, int lane);

    // A vertex shader input register and the declaration usage it is bound to.
    void DeclareInput(int reg, int usage, int usageIndex);
    // One component of an output register: an Output for vertex shaders (kOutTexCoord0 + n for oTn), and
    // kOutColour for a pixel shader's oC0, which must be given all four components.
    void SetOutput(int output, int component, ShaderValue value);

    // Writes the program. False, with the reason in error, if it does not fit the profile.
    bool Assemble(std::vector<uint32_t> *tokens, char *error, size_t errorSize);

    // A node of the program: a leaf (a register component or a literal) or an operation on other nodes.
    struct Node {
        uint8_t op;          // NodeOp, shaderBuilder.cpp
        uint8_t component;   // leaves: the register component; lit, texld: the result channel
        uint8_t argCount;
        int16_t reg;         // leaves: the register; a relative constant: the base; texld: the sampler
        uint32_t bits;       // a literal's float bits; texld: 1 for a cube map; add: 1 for a floor's
        ShaderValue args[8];
        float lo, hi;        // the range the value is known to lie in (not part of its identity)
        uint32_t operation;  // the source operation that made it: lanes of one vector operation share it
        int8_t lane;         // the vector lane it was made for, or -1: where a pixel shader keeps it
        int16_t variable;    // the source register it was first written to, or -1: values of one share a temporary
    };

private:
    ShaderValue Make(int op, int component, int reg, uint32_t bits, int argCount, const ShaderValue *args);
    bool IsLiteral(ShaderValue v, float *value) const;
    void Range(ShaderValue v, float *lo, float *hi) const;
    void ComputeRange(Node *n) const;
    bool IsLiteral(ShaderValue v, float value) const;
    ShaderValue Fold(int op, float a, float b, float c);

    Profile m_profile;
    uint32_t m_nextOperation, m_vectorBase;
    int m_laneCalls;   // Make calls in the current lane, or -1 outside a vector operation
    int m_lane;
    std::vector<Node> m_nodes;
    std::vector<uint32_t> m_hash;   // open-addressed: node index + 1, 0 = empty
    struct InputDecl { int reg, usage, usageIndex; };
    std::vector<InputDecl> m_inputs;
    struct OutputSlot { int output, component; ShaderValue value; };
    std::vector<OutputSlot> m_outputs;
};

#endif // COMMON_GFX_SHADERBUILDER_H_
