#include "shaderSpirv.h"
#include "shaderBuilder.h"

#include <initializer_list>
#include <map>
#include <stdio.h>
#include <string.h>
#include <vector>

// See shaderSpirv.h. The numbers are the SPIR-V 1.0 specification's and GLSL.std.450's.

using namespace ShaderOp;

namespace {

enum {
    OpExtInstImport = 11, OpExtInst = 12, OpMemoryModel = 14, OpEntryPoint = 15, OpExecutionMode = 16,
    OpCapability = 17, OpTypeVoid = 19, OpTypeBool = 20, OpTypeInt = 21, OpTypeFloat = 22, OpTypeVector = 23,
    OpTypeImage = 25, OpTypeSampledImage = 27, OpTypeArray = 28, OpTypeStruct = 30, OpTypePointer = 32,
    OpTypeFunction = 33, OpConstant = 43, OpFunction = 54, OpFunctionEnd = 56, OpVariable = 59, OpLoad = 61,
    OpStore = 62, OpAccessChain = 65, OpDecorate = 71, OpMemberDecorate = 72, OpVectorShuffle = 79,
    OpCompositeConstruct = 80, OpCompositeExtract = 81, OpImageSampleImplicitLod = 87, OpConvertFToS = 110,
    OpConvertSToF = 111, OpConvertUToF = 112, OpFNegate = 127, OpIAdd = 128, OpFAdd = 129, OpFSub = 131,
    OpFMul = 133, OpFDiv = 136, OpLogicalOr = 166, OpLogicalAnd = 167, OpSelect = 169, OpIEqual = 170,
    OpULessThan = 176, OpFOrdEqual = 180, OpFOrdNotEqual = 182, OpFOrdLessThan = 184, OpFOrdGreaterThan = 186,
    OpFOrdLessThanEqual = 188, OpFOrdGreaterThanEqual = 190, OpShiftRightLogical = 194, OpBitwiseAnd = 199,
    OpSelectionMerge = 247, OpLabel = 248, OpBranchConditional = 250, OpKill = 252, OpReturn = 253,
};
enum { DecorationBlock = 2, DecorationArrayStride = 6, DecorationBuiltIn = 11, DecorationLocation = 30,
       DecorationBinding = 33, DecorationDescriptorSet = 34, DecorationOffset = 35, DecorationNoContraction = 42 };
enum { BuiltInPosition = 0, BuiltInVertexIndex = 42 };
enum { StorageUniformConstant = 0, StorageInput = 1, StorageUniform = 2, StorageOutput = 3 };
enum { GlslFAbs = 4, GlslFloor = 8, GlslFract = 10, GlslPow = 26, GlslExp2 = 29, GlslLog2 = 30,
       GlslInverseSqrt = 32, GlslSClamp = 45, GlslFma = 50, GlslNClamp = 81 };

// Vertex outputs and pixel inputs by location (shaderSpirv.h).
enum { kLocDiffuse = 0, kLocSpecular = 1, kLocTexCoord0 = 2, kLocFog = 6 };

// The host constant a point sprite reads its pixel size from (nv2aVertexShader.h's c200; .zw).
const uint32_t kHostAdjust = 200;
// D3D9's point sizes: POINTSIZE_MIN's default and the largest the hardware here reports.
const float kPointSizeMin = 1.0f, kPointSizeMax = 8192.0f;

// The module's sections, filled in any order and written in the order SPIR-V wants them.
class Module {
public:
    Module() : m_bound(1) { m_glsl = NewId(); }

    uint32_t NewId() { return m_bound++; }

    static void Emit(std::vector<uint32_t> &section, uint32_t op, std::initializer_list<uint32_t> operands) {
        section.push_back(((uint32_t)(operands.size() + 1) << 16) | op);
        section.insert(section.end(), operands.begin(), operands.end());
    }
    // An instruction of the function with a result type and id.
    uint32_t Code(uint32_t op, uint32_t type, std::initializer_list<uint32_t> operands) {
        uint32_t id = NewId();
        m_code.push_back(((uint32_t)(operands.size() + 3) << 16) | op);
        m_code.push_back(type);
        m_code.push_back(id);
        m_code.insert(m_code.end(), operands.begin(), operands.end());
        return id;
    }
    uint32_t Exact(uint32_t op, uint32_t type, std::initializer_list<uint32_t> operands) {
        uint32_t id = Code(op, type, operands);
        Decorate(id, DecorationNoContraction);
        return id;
    }
    uint32_t Glsl(uint32_t instruction, uint32_t type, std::initializer_list<uint32_t> operands) {
        uint32_t id = NewId();
        m_code.push_back(((uint32_t)(operands.size() + 5) << 16) | OpExtInst);
        m_code.push_back(type);
        m_code.push_back(id);
        m_code.push_back(m_glsl);
        m_code.push_back(instruction);
        m_code.insert(m_code.end(), operands.begin(), operands.end());
        return id;
    }
    void Statement(uint32_t op, std::initializer_list<uint32_t> operands) { Emit(m_code, op, operands); }
    uint32_t Label() {
        uint32_t id = NewId();
        Statement(OpLabel, { id });
        return id;
    }

    uint32_t Type(uint32_t op, std::initializer_list<uint32_t> operands) {
        std::vector<uint32_t> key(1, op);
        key.insert(key.end(), operands.begin(), operands.end());
        std::map<std::vector<uint32_t>, uint32_t>::iterator found = m_types.find(key);
        if (found != m_types.end())
            return found->second;
        uint32_t id = NewId();
        m_globals.push_back(((uint32_t)(operands.size() + 2) << 16) | op);
        m_globals.push_back(id);
        m_globals.insert(m_globals.end(), operands.begin(), operands.end());
        m_types[key] = id;
        return id;
    }
    uint32_t Void() { return Type(OpTypeVoid, {}); }
    uint32_t Bool() { return Type(OpTypeBool, {}); }
    uint32_t Float() { return Type(OpTypeFloat, { 32 }); }
    uint32_t Int() { return Type(OpTypeInt, { 32, 1 }); }
    uint32_t Uint() { return Type(OpTypeInt, { 32, 0 }); }
    uint32_t Vector(uint32_t component, uint32_t count) { return Type(OpTypeVector, { component, count }); }
    uint32_t Vec4() { return Vector(Float(), 4); }
    uint32_t Pointer(uint32_t storage, uint32_t type) { return Type(OpTypePointer, { storage, type }); }

    uint32_t Constant(uint32_t type, uint32_t bits) {
        std::pair<uint32_t, uint32_t> key(type, bits);
        std::map<std::pair<uint32_t, uint32_t>, uint32_t>::iterator found = m_constants.find(key);
        if (found != m_constants.end())
            return found->second;
        uint32_t id = NewId();
        Emit(m_globals, OpConstant, { type, id, bits });
        m_constants[key] = id;
        return id;
    }
    uint32_t FloatConstant(float value) {
        uint32_t bits;
        memcpy(&bits, &value, 4);
        return Constant(Float(), bits);
    }
    uint32_t IntConstant(int32_t value) { return Constant(Int(), (uint32_t)value); }
    uint32_t UintConstant(uint32_t value) { return Constant(Uint(), value); }

    uint32_t Variable(uint32_t storage, uint32_t type) {
        uint32_t id = NewId();
        Emit(m_globals, OpVariable, { Pointer(storage, type), id, storage });
        if (storage == StorageInput || storage == StorageOutput)
            m_interface.push_back(id);
        return id;
    }
    void Decorate(uint32_t id, uint32_t decoration) { Emit(m_annotations, OpDecorate, { id, decoration }); }
    void Decorate(uint32_t id, uint32_t decoration, uint32_t value) { Emit(m_annotations, OpDecorate, { id, decoration, value }); }
    void MemberDecorate(uint32_t type, uint32_t member, uint32_t decoration, uint32_t value) {
        Emit(m_annotations, OpMemberDecorate, { type, member, decoration, value });
    }

    // The function's first two instructions, written at the start of its code.
    void BeginMain() {
        m_main = NewId();
        uint32_t voidType = Void();
        uint32_t functionType = Type(OpTypeFunction, { voidType });
        Emit(m_code, OpFunction, { voidType, m_main, 0, functionType });
        Label();
    }
    void EndMain() {
        Statement(OpReturn, {});
        Statement(OpFunctionEnd, {});
    }

    void Write(bool pixel, std::vector<uint32_t> *words) const {
        words->clear();
        words->push_back(0x07230203u);
        words->push_back(0x00010000u);   // SPIR-V 1.0
        words->push_back(0);             // no registered generator
        words->push_back(m_bound);
        words->push_back(0);
        Emit(*words, OpCapability, { 1 });   // Shader
        words->push_back((6u << 16) | OpExtInstImport);
        words->push_back(m_glsl);
        uint32_t name[4] = { 0, 0, 0, 0 };
        memcpy(name, "GLSL.std.450", 12);   // and its terminator in the last word's zero
        words->insert(words->end(), name, name + 4);
        Emit(*words, OpMemoryModel, { 0, 1 });   // Logical, GLSL450
        words->push_back(((uint32_t)(5 + m_interface.size()) << 16) | OpEntryPoint);
        words->push_back(pixel ? 4 : 0);         // Fragment, Vertex
        words->push_back(m_main);
        words->push_back(0x6e69616du);           // "main"
        words->push_back(0);
        words->insert(words->end(), m_interface.begin(), m_interface.end());
        if (pixel)
            Emit(*words, OpExecutionMode, { m_main, 7 });   // OriginUpperLeft
        words->insert(words->end(), m_annotations.begin(), m_annotations.end());
        words->insert(words->end(), m_globals.begin(), m_globals.end());
        words->insert(words->end(), m_code.begin(), m_code.end());
    }

private:
    uint32_t m_bound, m_glsl, m_main;
    std::vector<uint32_t> m_annotations, m_globals, m_code, m_interface;
    std::map<std::vector<uint32_t>, uint32_t> m_types;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> m_constants;
};

class SpirvLowering {
public:
    struct OutputValue { int output, component; ShaderValue value; };

    SpirvLowering(const std::vector<ShaderBuilder::Node> &nodes, const std::vector<ShaderBuilder::FusedNode> &fused,
                  bool pixel, const ShaderSpirvOptions &options, ShaderSpirvInfo *info)
        : m_nodes(nodes), m_fused(fused), m_pixel(pixel), m_options(options), m_info(info), m_constants(0),
          m_failed(false) {
        m_id.assign(nodes.size(), 0);
        m_negated.assign(nodes.size(), 0);
        memset(m_inputVector, 0, sizeof(m_inputVector));
        memset(m_texCoordVector, 0, sizeof(m_texCoordVector));
        memset(m_samplerVariable, 0, sizeof(m_samplerVariable));
        memset(info, 0, sizeof(*info));
        m_error[0] = 0;
    }

    bool Run(const std::vector<OutputValue> &outputs, std::vector<uint32_t> *words, char *error, size_t errorSize);

private:
    uint32_t Node(int node);
    uint32_t Value(ShaderValue v);
    uint32_t Compute(int node);
    uint32_t Lit(int node);
    uint32_t InputComponent(int reg, int component);
    uint32_t TexCoordComponent(int reg, int component);
    uint32_t ConstantRegister(uint32_t index);
    uint32_t Sample(int node);
    uint32_t Fail(const char *what, int n);

    uint32_t F(float value) { return m.FloatConstant(value); }
    uint32_t Select(uint32_t condition, uint32_t a, uint32_t b) { return m.Code(OpSelect, m.Float(), { condition, a, b }); }
    uint32_t Compare(uint32_t op, uint32_t a, uint32_t b) { return m.Code(op, m.Bool(), { a, b }); }
    uint32_t Add(uint32_t a, uint32_t b) { return m.Exact(OpFAdd, m.Float(), { a, b }); }
    uint32_t Sub(uint32_t a, uint32_t b) { return m.Exact(OpFSub, m.Float(), { a, b }); }
    uint32_t Mul(uint32_t a, uint32_t b) { return m.Exact(OpFMul, m.Float(), { a, b }); }
    uint32_t Extract(uint32_t vector, uint32_t component) { return m.Code(OpCompositeExtract, m.Float(), { vector, component }); }
    uint32_t Saturate(uint32_t a) { return m.Glsl(GlslNClamp, m.Float(), { a, F(0.0f), F(1.0f) }); }
    uint32_t Output(uint32_t location, uint32_t type);

    void VertexOutputs(const std::vector<OutputValue> &outputs);
    void PixelOutputs(const std::vector<OutputValue> &outputs);

    const std::vector<ShaderBuilder::Node> &m_nodes;
    const std::vector<ShaderBuilder::FusedNode> &m_fused;
    bool m_pixel;
    const ShaderSpirvOptions &m_options;
    ShaderSpirvInfo *m_info;
    Module m;
    std::vector<uint32_t> m_id, m_negated;
    uint32_t m_inputVector[16];     // a vertex input register or a pixel colour, loaded as a float4
    uint32_t m_texCoordVector[8];
    uint32_t m_constants;           // the uniform block's variable
    std::map<uint32_t, uint32_t> m_constantRegister;
    std::map<std::vector<uint32_t>, uint32_t> m_samples;
    uint32_t m_samplerVariable[16];
    bool m_failed;
    char m_error[160];
};

uint32_t SpirvLowering::Fail(const char *what, int n) {
    if (!m_failed)
        snprintf(m_error, sizeof(m_error), what, n);
    m_failed = true;
    return F(0.0f);
}

uint32_t SpirvLowering::Node(int node) {
    if (m_id[node] == 0)
        m_id[node] = Compute(node);
    return m_id[node];
}

uint32_t SpirvLowering::Value(ShaderValue v) {
    const ShaderBuilder::Node &n = m_nodes[v.node];
    if (!v.negate)
        return Node(v.node);
    if (n.op == kLiteral)
        return m.Constant(m.Float(), n.bits ^ 0x80000000u);
    if (m_negated[v.node] == 0)
        m_negated[v.node] = m.Code(OpFNegate, m.Float(), { Node(v.node) });
    return m_negated[v.node];
}

uint32_t SpirvLowering::InputComponent(int reg, int component) {
    if (reg < 0 || reg >= 16)
        return Fail("input register %d", reg);
    if (m_inputVector[reg] == 0) {
        uint32_t vec4 = m.Vec4();
        if (m_pixel) {
            if (reg > 1)
                return Fail("pixel colour input %d", reg);
            uint32_t variable = m.Variable(StorageInput, vec4);
            m.Decorate(variable, DecorationLocation, reg == 0 ? kLocDiffuse : kLocSpecular);
            m_inputVector[reg] = m.Code(OpLoad, vec4, { variable });
        } else {
            int kind = m_options.inputKind[reg];
            uint32_t type = kind == kSpirvSigned ? m.Vector(m.Int(), 4) : kind == kSpirvUnsigned ? m.Vector(m.Uint(), 4) : vec4;
            uint32_t variable = m.Variable(StorageInput, type);
            m.Decorate(variable, DecorationLocation, (uint32_t)reg);
            uint32_t loaded = m.Code(OpLoad, type, { variable });
            if (kind == kSpirvSigned)
                loaded = m.Code(OpConvertSToF, vec4, { loaded });
            else if (kind == kSpirvUnsigned)
                loaded = m.Code(OpConvertUToF, vec4, { loaded });
            else if (kind == kSpirvColour)
                loaded = m.Code(OpVectorShuffle, vec4, { loaded, loaded, 2, 1, 0, 3 });
            m_inputVector[reg] = loaded;
            m_info->inputs |= 1u << reg;
        }
    }
    return Extract(m_inputVector[reg], (uint32_t)component);
}

uint32_t SpirvLowering::TexCoordComponent(int reg, int component) {
    if (!m_pixel || reg < 0 || reg >= 4)
        return Fail("texture coordinate register %d", reg);
    if (m_texCoordVector[reg] == 0) {
        uint32_t variable = m.Variable(StorageInput, m.Vec4());
        m.Decorate(variable, DecorationLocation, kLocTexCoord0 + (uint32_t)reg);
        m_texCoordVector[reg] = m.Code(OpLoad, m.Vec4(), { variable });
    }
    return Extract(m_texCoordVector[reg], (uint32_t)component);
}

// A constant register as a float4. index is an int's id: a constant's for a register named in the program, a
// computed one's for a relative read.
uint32_t SpirvLowering::ConstantRegister(uint32_t index) {
    std::map<uint32_t, uint32_t>::iterator found = m_constantRegister.find(index);
    if (found != m_constantRegister.end())
        return found->second;
    uint32_t vec4 = m.Vec4();
    if (m_constants == 0) {
        uint32_t count = m_pixel ? kSpirvPixelConstants : kSpirvVertexConstants;
        uint32_t array = m.Type(OpTypeArray, { vec4, m.UintConstant(count) });
        m.Decorate(array, DecorationArrayStride, 16);
        uint32_t block = m.Type(OpTypeStruct, { array });
        m.Decorate(block, DecorationBlock);
        m.MemberDecorate(block, 0, DecorationOffset, 0);
        m_constants = m.Variable(StorageUniform, block);
        m.Decorate(m_constants, DecorationDescriptorSet, m_pixel ? 3 : 1);
        m.Decorate(m_constants, DecorationBinding, 0);
    }
    uint32_t pointer = m.Code(OpAccessChain, m.Pointer(StorageUniform, vec4), { m_constants, m.IntConstant(0), index });
    uint32_t value = m.Code(OpLoad, vec4, { pointer });
    m_constantRegister[index] = value;
    return value;
}

// One channel of a texld. The channels of one read share one sample.
uint32_t SpirvLowering::Sample(int node) {
    const ShaderBuilder::Node &n = m_nodes[node];
    int sampler = n.reg;
    bool cube = n.bits != 0;
    if (!m_pixel || sampler < 0 || sampler >= 16)
        return Fail("texture read from sampler %d", sampler);
    std::vector<uint32_t> key;
    key.push_back((uint32_t)sampler);
    key.push_back(cube ? 1 : 0);
    uint32_t coord[3];
    for (int k = 0; k < n.argCount; k++) {
        coord[k] = Value(n.args[k]);
        key.push_back(coord[k]);
    }
    uint32_t sampled;
    std::map<std::vector<uint32_t>, uint32_t>::iterator found = m_samples.find(key);
    if (found != m_samples.end()) {
        sampled = found->second;
    } else {
        uint32_t image = m.Type(OpTypeImage, { m.Float(), cube ? 3u : 1u, 0, 0, 0, 1, 0 });   // Cube or 2D, sampled
        uint32_t sampledImage = m.Type(OpTypeSampledImage, { image });
        if (m_samplerVariable[sampler] == 0) {
            m_samplerVariable[sampler] = m.Variable(StorageUniformConstant, sampledImage);
            m.Decorate(m_samplerVariable[sampler], DecorationDescriptorSet, 2);
            m.Decorate(m_samplerVariable[sampler], DecorationBinding, (uint32_t)sampler);
            m_info->samplers |= 1u << sampler;
            if (cube)
                m_info->cubeSamplers |= 1u << sampler;
        } else if (((m_info->cubeSamplers >> sampler) & 1) != (cube ? 1u : 0u)) {
            return Fail("sampler %d read both as a cube map and as 2D", sampler);
        }
        uint32_t loaded = m.Code(OpLoad, sampledImage, { m_samplerVariable[sampler] });
        uint32_t coordinate = cube ? m.Code(OpCompositeConstruct, m.Vector(m.Float(), 3), { coord[0], coord[1], coord[2] })
                                   : m.Code(OpCompositeConstruct, m.Vector(m.Float(), 2), { coord[0], coord[1] });
        sampled = m.Code(OpImageSampleImplicitLod, m.Vec4(), { loaded, coordinate });
        m_samples[key] = sampled;
    }
    return Extract(sampled, n.component);
}

// D3D9's lit: (1, x > 0 ? x : 0, x > 0 && y > 0 ? y ^ w : 0, 1), the power held inside (-128, 128).
uint32_t SpirvLowering::Lit(int node) {
    const ShaderBuilder::Node &n = m_nodes[node];
    if (n.component == 0 || n.component == 3)
        return F(1.0f);
    uint32_t x = Value(n.args[0]);
    uint32_t xPositive = Compare(OpFOrdGreaterThan, x, F(0.0f));
    if (n.component == 1)
        return Select(xPositive, x, F(0.0f));
    uint32_t y = Value(n.args[1]), w = Value(n.args[2]);
    uint32_t power = m.Glsl(GlslNClamp, m.Float(), { w, F(-127.9961f), F(127.9961f) });
    uint32_t lit = m.Code(OpLogicalAnd, m.Bool(), { xPositive, Compare(OpFOrdGreaterThan, y, F(0.0f)) });
    return Select(lit, m.Glsl(GlslPow, m.Float(), { y, power }), F(0.0f));
}

uint32_t SpirvLowering::Compute(int node) {
    const ShaderBuilder::Node &n = m_nodes[node];
    const ShaderBuilder::FusedNode &f = m_fused[node];
    switch (f.op) {
        case kLiteral:
            return m.Constant(m.Float(), n.bits);
        case kInput:
            return InputComponent(n.reg, n.component);
        case kTexCoord:
            return TexCoordComponent(n.reg, n.component);
        case kConstant:
            if (n.reg < 0 || n.reg >= (m_pixel ? kSpirvPixelConstants : kSpirvVertexConstants))
                return Fail("constant register %d", n.reg);
            return Extract(ConstantRegister(m.IntConstant(n.reg)), n.component);
        case kRelative: {
            // c[a0.x + base]: the address is integral; out of range reads clamp to the block
            uint32_t address = m.Code(OpConvertFToS, m.Int(), { Value(n.args[0]) });
            uint32_t index = m.Code(OpIAdd, m.Int(), { address, m.IntConstant(n.reg) });
            uint32_t last = (m_pixel ? kSpirvPixelConstants : kSpirvVertexConstants) - 1;
            index = m.Glsl(GlslSClamp, m.Int(), { index, m.IntConstant(0), m.IntConstant((int32_t)last) });
            return Extract(ConstantRegister(index), n.component);
        }
        case kAdd: {
            ShaderValue a = f.args[0], b = f.args[1];
            if (b.negate && !a.negate)
                return Sub(Node(a.node), Node(b.node));
            if (a.negate && !b.negate)
                return Sub(Node(b.node), Node(a.node));
            return Add(Value(a), Value(b));
        }
        case kMul:
            return Mul(Value(f.args[0]), Value(f.args[1]));
        case kMad: {
            uint32_t id = m.Glsl(GlslFma, m.Float(), { Value(f.args[0]), Value(f.args[1]), Value(f.args[2]) });
            m.Decorate(id, DecorationNoContraction);
            return id;
        }
        case kLrp: {
            uint32_t id = m.Glsl(GlslFma, m.Float(), { Value(f.args[0]), Sub(Value(f.args[1]), Value(f.args[2])),
                                                       Value(f.args[2]) });
            m.Decorate(id, DecorationNoContraction);
            return id;
        }
        case kDp3:
        case kDp4: {
            int terms = f.op == kDp3 ? 3 : 4;
            uint32_t sum = Mul(Value(f.args[0]), Value(f.args[terms]));
            for (int i = 1; i < terms; i++)
                sum = Add(sum, Mul(Value(f.args[i]), Value(f.args[terms + i])));
            return sum;
        }
        case kMin: {
            uint32_t a = Value(f.args[0]), b = Value(f.args[1]);
            return Select(Compare(OpFOrdLessThan, a, b), a, b);
        }
        case kMax: {
            uint32_t a = Value(f.args[0]), b = Value(f.args[1]);
            return Select(Compare(OpFOrdGreaterThanEqual, a, b), a, b);
        }
        case kSlt:
            return Select(Compare(OpFOrdLessThan, Value(f.args[0]), Value(f.args[1])), F(1.0f), F(0.0f));
        case kSge:
            return Select(Compare(OpFOrdGreaterThanEqual, Value(f.args[0]), Value(f.args[1])), F(1.0f), F(0.0f));
        case kCmp:
            return Select(Compare(OpFOrdGreaterThanEqual, Value(f.args[0]), F(0.0f)), Value(f.args[1]), Value(f.args[2]));
        case kRcp:
            return m.Exact(OpFDiv, m.Float(), { F(1.0f), Value(f.args[0]) });
        case kRsq:
            return m.Glsl(GlslInverseSqrt, m.Float(), { m.Glsl(GlslFAbs, m.Float(), { Value(f.args[0]) }) });
        case kFrc:
            return m.Glsl(GlslFract, m.Float(), { Value(f.args[0]) });
        case kAbs:
            return m.Glsl(GlslFAbs, m.Float(), { Value(f.args[0]) });
        case kExp:
            return m.Glsl(GlslExp2, m.Float(), { Value(f.args[0]) });
        case kLog:
            return m.Glsl(GlslLog2, m.Float(), { m.Glsl(GlslFAbs, m.Float(), { Value(f.args[0]) }) });
        case kSat:
            return Saturate(Value(f.args[0]));
        case kLit:
            return Lit(node);
        case kTex:
            return Sample(node);
        case kNrm:   // the component's product with the reciprocal square root, which rides along as the fourth operand
            return Mul(Value(n.args[n.component]), Value(n.args[3]));
        default:
            return Fail("node operation %d", f.op);
    }
}

uint32_t SpirvLowering::Output(uint32_t location, uint32_t type) {
    uint32_t variable = m.Variable(StorageOutput, type);
    m.Decorate(variable, DecorationLocation, location);
    return variable;
}

// The value each output component was given (outputs set more than once keep the last, as SetOutput does).
static bool FindOutput(const std::vector<SpirvLowering::OutputValue> &outputs, int output, int component,
                       ShaderValue *value) {
    for (size_t i = 0; i < outputs.size(); i++) {
        if (outputs[i].output == output && outputs[i].component == component) {
            *value = outputs[i].value;
            return true;
        }
    }
    return false;
}

void SpirvLowering::VertexOutputs(const std::vector<OutputValue> &outputs) {
    // Every component's value first, in output order, then the stores.
    uint32_t position[4], colour[2][4], texCoord[4][4], fog, pointSize;
    for (int c = 0; c < 4; c++) {
        ShaderValue v;
        position[c] = FindOutput(outputs, ShaderBuilder::kOutPosition, c, &v) ? Value(v) : F(0.0f);
        for (int k = 0; k < 2; k++)
            colour[k][c] = FindOutput(outputs, ShaderBuilder::kOutDiffuse + k, c, &v) ? Saturate(Value(v)) : F(0.0f);
        for (int t = 0; t < 4; t++)
            texCoord[t][c] = FindOutput(outputs, ShaderBuilder::kOutTexCoord0 + t, c, &v) ? Value(v) : F(c == 3 ? 1.0f : 0.0f);
    }
    ShaderValue v;
    fog = FindOutput(outputs, ShaderBuilder::kOutFog, 0, &v) ? Value(v) : F(1.0f);
    pointSize = FindOutput(outputs, ShaderBuilder::kOutPointSize, 0, &v) ? Value(v) : F(kPointSizeMin);

    if (m_options.pointSprite) {
        // Corner q of the quad (0 top left, 1 top right, 2 bottom left, 3 bottom right) for vertex i of the two
        // triangles (0, 1, 2) and (2, 1, 3).
        uint32_t indexVariable = m.Variable(StorageInput, m.Int());
        m.Decorate(indexVariable, DecorationBuiltIn, BuiltInVertexIndex);
        uint32_t i = m.Code(OpLoad, m.Int(), { indexVariable });
        uint32_t q = m.Code(OpSelect, m.Int(), { m.Code(OpIEqual, m.Bool(), { i, m.IntConstant(3) }), m.IntConstant(2), i });
        q = m.Code(OpSelect, m.Int(), { m.Code(OpIEqual, m.Bool(), { i, m.IntConstant(4) }), m.IntConstant(1), q });
        q = m.Code(OpSelect, m.Int(), { m.Code(OpIEqual, m.Bool(), { i, m.IntConstant(5) }), m.IntConstant(3), q });
        uint32_t right = m.Code(OpConvertSToF, m.Float(), { m.Code(OpBitwiseAnd, m.Int(), { q, m.IntConstant(1) }) });
        uint32_t down = m.Code(OpConvertSToF, m.Float(),
                               { m.Code(OpShiftRightLogical, m.Int(), { q, m.IntConstant(1) }) });
        uint32_t size = m.Glsl(GlslNClamp, m.Float(), { pointSize, F(kPointSizeMin), F(kPointSizeMax) });
        uint32_t adjust = ConstantRegister(m.IntConstant((int32_t)kHostAdjust));
        uint32_t w = position[3];
        // x += (right - 0.5) * size * 2/width * w; y += (0.5 - down) * size * 2/height * w (clip space is y up)
        position[0] = Add(position[0], Mul(Mul(Mul(Sub(right, F(0.5f)), size), Extract(adjust, 2)), w));
        position[1] = Add(position[1], Mul(Mul(Mul(Sub(F(0.5f), down), size), Extract(adjust, 3)), w));
        for (int t = 0; t < 4; t++) {
            texCoord[t][0] = right;
            texCoord[t][1] = down;
            texCoord[t][2] = F(0.0f);
            texCoord[t][3] = F(1.0f);
        }
    }

    uint32_t vec4 = m.Vec4();
    uint32_t positionVariable = m.Variable(StorageOutput, vec4);
    m.Decorate(positionVariable, DecorationBuiltIn, BuiltInPosition);
    m.Statement(OpStore, { positionVariable, m.Code(OpCompositeConstruct, vec4, { position[0], position[1], position[2], position[3] }) });
    for (int k = 0; k < 2; k++)
        m.Statement(OpStore, { Output(kLocDiffuse + k, vec4),
                               m.Code(OpCompositeConstruct, vec4, { colour[k][0], colour[k][1], colour[k][2], colour[k][3] }) });
    for (int t = 0; t < 4; t++)
        m.Statement(OpStore, { Output(kLocTexCoord0 + t, vec4),
                               m.Code(OpCompositeConstruct, vec4, { texCoord[t][0], texCoord[t][1], texCoord[t][2], texCoord[t][3] }) });
    m.Statement(OpStore, { Output(kLocFog, m.Float()), fog });
}

void SpirvLowering::PixelOutputs(const std::vector<OutputValue> &outputs) {
    uint32_t colour[4];
    for (int c = 0; c < 4; c++) {
        ShaderValue v;
        if (!FindOutput(outputs, ShaderBuilder::kOutColour, c, &v)) {
            Fail("a pixel program must write all four colour components", 0);
            colour[c] = F(0.0f);
        } else {
            colour[c] = Value(v);
        }
    }
    uint32_t vec4 = m.Vec4();
    uint32_t target = Output(0, vec4);
    if (!m_options.hostPixelEnd) {
        m.Statement(OpStore, { target, m.Code(OpCompositeConstruct, vec4, { colour[0], colour[1], colour[2], colour[3] }) });
        return;
    }

    uint32_t state = ConstantRegister(m.IntConstant(kSpirvPixelState));
    uint32_t fogColour = ConstantRegister(m.IntConstant(kSpirvFogColour));
    // Fog: colour * f + fog colour * (1 - f), with f the vertex program's factor
    uint32_t fogVariable = m.Variable(StorageInput, m.Float());
    m.Decorate(fogVariable, DecorationLocation, kLocFog);
    uint32_t f = Saturate(m.Code(OpLoad, m.Float(), { fogVariable }));
    uint32_t fogOn = Compare(OpFOrdGreaterThan, Extract(state, 0), F(0.5f));
    uint32_t inverse = Sub(F(1.0f), f);
    for (int c = 0; c < 3; c++)
        colour[c] = Select(fogOn, Add(Mul(colour[c], f), Mul(Extract(fogColour, (uint32_t)c), inverse)), colour[c]);
    // The alpha test, on the alpha as the 8 bits a render target holds
    uint32_t alpha = m.Glsl(GlslFloor, m.Float(), { Add(Mul(Saturate(colour[3]), F(255.0f)), F(0.5f)) });
    uint32_t function = Extract(state, 1), reference = Extract(state, 2);
    static const uint32_t kComparisons[6] = { OpFOrdLessThan, OpFOrdEqual, OpFOrdLessThanEqual, OpFOrdGreaterThan,
                                              OpFOrdNotEqual, OpFOrdGreaterThanEqual };   // D3DCMP 2..7
    uint32_t pass = m.Code(OpFOrdNotEqual, m.Bool(), { function, F(1.0f) });   // NEVER fails, ALWAYS and the rest pass
    for (int k = 0; k < 6; k++) {
        uint32_t isThis = Compare(OpFOrdEqual, function, F((float)(k + 2)));
        pass = m.Code(OpSelect, m.Bool(), { isThis, Compare(kComparisons[k], alpha, reference), pass });
    }
    // A visibility test's mask
    uint32_t mask = Compare(OpFOrdGreaterThan, Extract(state, 3), F(0.5f));
    for (int c = 0; c < 4; c++)
        colour[c] = Select(mask, F(1.0f), colour[c]);
    uint32_t result = m.Code(OpCompositeConstruct, vec4, { colour[0], colour[1], colour[2], colour[3] });

    uint32_t merge = m.NewId(), kill = m.NewId();
    m.Statement(OpSelectionMerge, { merge, 0 });
    m.Statement(OpBranchConditional, { pass, merge, kill });
    m.Statement(OpLabel, { kill });
    m.Statement(OpKill, {});
    m.Statement(OpLabel, { merge });
    m.Statement(OpStore, { target, result });
}

bool SpirvLowering::Run(const std::vector<OutputValue> &outputs, std::vector<uint32_t> *words, char *error,
                        size_t errorSize) {
    m.BeginMain();
    // Live nodes, from the outputs back, in the order they were made (their operands always come first).
    size_t n = m_nodes.size();
    std::vector<bool> live(n, false);
    for (size_t i = 0; i < outputs.size(); i++) live[outputs[i].value.node] = true;
    for (size_t i = n; i-- > 0;) {
        if (!live[i]) continue;
        for (int k = 0; k < m_fused[i].argCount; k++) live[m_fused[i].args[k].node] = true;
        if (m_fused[i].op == kNrm) live[m_nodes[i].args[3].node] = true;
    }
    for (size_t i = 0; i < n && !m_failed; i++)
        if (live[i] && m_nodes[i].op > kRelative)
            Node((int)i);
    if (m_pixel)
        PixelOutputs(outputs);
    else
        VertexOutputs(outputs);
    m.EndMain();
    if (m_failed) {
        snprintf(error, errorSize, "%s", m_error);
        return false;
    }
    m.Write(m_pixel, words);
    return true;
}

}   // namespace

bool ShaderBuilder::AssembleSpirv(const ShaderSpirvOptions &options, std::vector<uint32_t> *words, ShaderSpirvInfo *info,
                                  char *error, size_t errorSize) const {
    std::vector<FusedNode> fused;
    Fuse(&fused);
    std::vector<SpirvLowering::OutputValue> outputs;
    for (size_t i = 0; i < m_outputs.size(); i++) {
        SpirvLowering::OutputValue o = { m_outputs[i].output, m_outputs[i].component, m_outputs[i].value };
        outputs.push_back(o);
    }
    SpirvLowering lowering(m_nodes, fused, m_profile == kPixelShader, options, info);
    return lowering.Run(outputs, words, error, errorSize);
}
