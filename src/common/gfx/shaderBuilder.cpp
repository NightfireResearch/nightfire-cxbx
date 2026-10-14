#include "shaderBuilder.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>

// See the header. The program is a graph of scalar nodes, hash-consed so that equal expressions are one node.
// Assemble lowers it in the order the nodes were made, which is the source program's order, packing the
// lanes of one source instruction back into one vector instruction where their operands allow.

enum NodeOp {
    // leaves: read straight from a register, never computed
    kLiteral, kInput, kTexCoord, kConstant, kRelative,
    // instructions
    kAdd, kMul, kMad, kDp3, kDp4, kMin, kMax, kSlt, kSge, kCmp, kRcp, kRsq, kFrc, kAbs, kExp, kLog, kSat, kLit, kTex,
    kNrm,   // a component of x * rsq(dot(x, x)), which the compiler makes an nrm
    kLrp,   // only made by Assemble, from a mad of a difference
};

static bool IsLeaf(int op) { return op <= kRelative; }
static bool IsLiteralOp(int op) { return op == kLiteral; }

static uint32_t FloatBits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float BitsFloat(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

ShaderBuilder::ShaderBuilder(Profile profile)
    : m_profile(profile), m_nextOperation(1), m_vectorBase(0), m_laneCalls(-1), m_lane(-1) {
    m_nodes.reserve(1024);
    m_hash.assign(4096, 0);
}

static uint32_t HashNode(const ShaderBuilder::Node &n) {
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix(n.op); mix(n.component); mix((uint16_t)n.reg); mix(n.bits); mix(n.argCount);
    for (int i = 0; i < n.argCount; i++) mix(n.args[i].node * 2u + n.args[i].negate);
    return h;
}

static bool SameNode(const ShaderBuilder::Node &a, const ShaderBuilder::Node &b) {
    if (a.op != b.op || a.component != b.component || a.reg != b.reg || a.bits != b.bits || a.argCount != b.argCount)
        return false;
    for (int i = 0; i < a.argCount; i++)
        if (a.args[i].node != b.args[i].node || a.args[i].negate != b.args[i].negate) return false;
    return true;
}

ShaderValue ShaderBuilder::Make(int op, int component, int reg, uint32_t bits, int argCount, const ShaderValue *args) {
    Node n;
    memset(&n, 0, sizeof(n));
    n.op = (uint8_t)op;
    n.component = (uint8_t)component;
    n.reg = (int16_t)reg;
    n.bits = bits;
    n.argCount = (uint8_t)argCount;
    for (int i = 0; i < argCount; i++) n.args[i] = args[i];
    n.operation = m_laneCalls >= 0 ? m_vectorBase + (uint32_t)m_laneCalls++ : m_nextOperation++;
    n.lane = (int8_t)(m_laneCalls >= 0 ? m_lane : -1);
    n.variable = -1;
    if (m_nodes.size() * 2 >= m_hash.size()) {   // grow and rehash
        m_hash.assign(m_hash.size() * 2, 0);
        for (size_t i = 0; i < m_nodes.size(); i++) {
            size_t slot = HashNode(m_nodes[i]) & (m_hash.size() - 1);
            while (m_hash[slot] != 0) slot = (slot + 1) & (m_hash.size() - 1);
            m_hash[slot] = (uint32_t)i + 1;
        }
    }
    size_t slot = HashNode(n) & (m_hash.size() - 1);
    while (m_hash[slot] != 0) {
        if (SameNode(m_nodes[m_hash[slot] - 1], n)) {
            ShaderValue v = { (uint16_t)(m_hash[slot] - 1), false };
            return v;
        }
        slot = (slot + 1) & (m_hash.size() - 1);
    }
    ComputeRange(&n);
    m_nodes.push_back(n);
    m_hash[slot] = (uint32_t)m_nodes.size();
    ShaderValue v = { (uint16_t)(m_nodes.size() - 1), false };
    return v;
}

// Value ranges, as the HLSL compiler tracks them: it drops a saturate, max or min that cannot change its
// operand, which matters beyond the instruction saved when the operand is a product that can then fuse into
// a mad. A pixel shader's colour inputs are 0..1; registers otherwise unknown.
void ShaderBuilder::Range(ShaderValue v, float *lo, float *hi) const {
    const Node &n = m_nodes[v.node];
    if (v.negate) { *lo = -n.hi; *hi = -n.lo; }
    else { *lo = n.lo; *hi = n.hi; }
}

static void ProductRange(float alo, float ahi, float blo, float bhi, float *lo, float *hi) {
    float p[4] = { alo * blo, alo * bhi, ahi * blo, ahi * bhi };
    *lo = INFINITY; *hi = -INFINITY;
    for (int i = 0; i < 4; i++) {
        if (p[i] != p[i]) { *lo = -INFINITY; *hi = INFINITY; return; }   // 0 * infinity
        if (p[i] < *lo) *lo = p[i];
        if (p[i] > *hi) *hi = p[i];
    }
}

void ShaderBuilder::ComputeRange(Node *n) const {
    float lo[8], hi[8];
    for (int i = 0; i < n->argCount; i++) Range(n->args[i], &lo[i], &hi[i]);
    n->lo = -INFINITY;
    n->hi = INFINITY;
    switch (n->op) {
        case kLiteral: n->lo = n->hi = BitsFloat(n->bits); break;
        case kInput: if (m_profile == kPixelShader) { n->lo = 0.0f; n->hi = 1.0f; } break;
        case kAdd: n->lo = lo[0] + lo[1]; n->hi = hi[0] + hi[1]; break;
        case kMul:
            if (n->args[0].node == n->args[1].node && n->args[0].negate == n->args[1].negate) {   // a square
                float m = fmaxf(lo[0] * lo[0], hi[0] * hi[0]);
                n->lo = (lo[0] > 0.0f) ? lo[0] * lo[0] : (hi[0] < 0.0f) ? hi[0] * hi[0] : 0.0f;
                n->hi = m;
            } else {
                ProductRange(lo[0], hi[0], lo[1], hi[1], &n->lo, &n->hi);
            }
            break;
        case kMad: {
            float plo, phi;
            ProductRange(lo[0], hi[0], lo[1], hi[1], &plo, &phi);
            n->lo = plo + lo[2]; n->hi = phi + hi[2];
            break;
        }
        case kDp3: case kDp4: {
            int terms = n->op == kDp3 ? 3 : 4;
            n->lo = n->hi = 0.0f;
            for (int i = 0; i < terms; i++) {
                float plo, phi;
                ProductRange(lo[i], hi[i], lo[terms + i], hi[terms + i], &plo, &phi);
                n->lo += plo; n->hi += phi;
            }
            break;
        }
        case kMin: n->lo = fminf(lo[0], lo[1]); n->hi = fminf(hi[0], hi[1]); break;
        case kMax: n->lo = fmaxf(lo[0], lo[1]); n->hi = fmaxf(hi[0], hi[1]); break;
        case kSlt: case kSge: case kSat: n->lo = 0.0f; n->hi = 1.0f; break;
        case kCmp: n->lo = fminf(lo[1], lo[2]); n->hi = fmaxf(hi[1], hi[2]); break;
        case kFrc: n->lo = 0.0f; n->hi = 1.0f; break;
        case kAbs: n->lo = (lo[0] > 0.0f) ? lo[0] : (hi[0] < 0.0f) ? -hi[0] : 0.0f; n->hi = fmaxf(-lo[0], hi[0]); break;
        case kExp: case kRsq: n->lo = 0.0f; break;
        default: break;
    }
    if (n->lo != n->lo || n->hi != n->hi) { n->lo = -INFINITY; n->hi = INFINITY; }
}

void ShaderBuilder::BeginVector() {
    m_vectorBase = m_nextOperation;
    m_nextOperation += 256;
    m_laneCalls = 0;
}

void ShaderBuilder::Lane(int lane) { m_laneCalls = 0; m_lane = lane; }
void ShaderBuilder::EndVector() { m_laneCalls = -1; m_lane = -1; }

bool ShaderBuilder::IsLiteral(ShaderValue v, float *value) const {
    const Node &n = m_nodes[v.node];
    if (n.op != kLiteral) return false;
    *value = v.negate ? -BitsFloat(n.bits) : BitsFloat(n.bits);
    return true;
}

bool ShaderBuilder::IsLiteral(ShaderValue v, float value) const {
    float f;
    return IsLiteral(v, &f) && f == value;
}

ShaderValue ShaderBuilder::Literal(float value) {
    // Negative literals are the positive one negated, so that a literal and its negation share a def slot.
    bool negative = signbit(value) != 0 && value != 0.0f;
    ShaderValue v = Make(kLiteral, 0, 0, FloatBits(negative ? -value : value), 0, NULL);
    v.negate = negative;
    return v;
}

ShaderValue ShaderBuilder::Input(int reg, int component) { return Make(kInput, component, reg, 0, 0, NULL); }
ShaderValue ShaderBuilder::TexCoord(int reg, int component) { return Make(kTexCoord, component, reg, 0, 0, NULL); }
ShaderValue ShaderBuilder::Constant(int reg, int component) { return Make(kConstant, component, reg, 0, 0, NULL); }
ShaderValue ShaderBuilder::RelativeConstant(int base, int component, ShaderValue address) {
    return Make(kRelative, component, base, 0, 1, &address);
}

ShaderValue ShaderBuilder::Add(ShaderValue a, ShaderValue b) {
    float x, y;
    if (IsLiteral(a, &x) && IsLiteral(b, &y)) return Literal(x + y);
    if (a.node == b.node && a.negate != b.negate) return Literal(0.0f);   // x - x, as the compiler folds it
    // (p + q) - p is q, as the compiler folds it too.
    for (int s = 0; s < 2; s++) {
        ShaderValue sum = s ? b : a, other = s ? a : b;
        if (m_nodes[sum.node].op != kAdd || m_nodes[sum.node].bits != 0) continue;
        for (int k = 0; k < 2; k++) {
            ShaderValue p = m_nodes[sum.node].args[k], q = m_nodes[sum.node].args[1 - k];
            if (sum.negate) { p = Negate(p); q = Negate(q); }
            if (p.node == other.node && p.negate != other.negate) return q;
        }
    }
    // A lane of a vector operation stays an add with zero, as it does for the compiler, which keeps the
    // vector whole: whether its products fuse depends on every lane (see Assemble).
    if (m_laneCalls < 0 && IsLiteral(a, 0.0f)) return b;
    if (m_laneCalls < 0 && IsLiteral(b, 0.0f)) return a;
    // a*b + (1-a)*c is a lerp from c to b by a.
    for (int s = 0; s < 2; s++) {
        ShaderValue p = s ? b : a, q = s ? a : b;
        if (p.negate || q.negate || m_nodes[p.node].op != kMul || m_nodes[q.node].op != kMul) continue;
        for (int i = 0; i < 2; i++) {
            ShaderValue t = m_nodes[p.node].args[i], from = m_nodes[p.node].args[1 - i];
            for (int j = 0; j < 2; j++) {
                ShaderValue inverse = m_nodes[q.node].args[j], to = m_nodes[q.node].args[1 - j];
                const Node &d = m_nodes[inverse.node];
                if (inverse.negate || d.op != kAdd) continue;
                for (int k = 0; k < 2; k++)
                    if (IsLiteral(d.args[k], 1.0f) && d.args[1 - k].node == t.node && d.args[1 - k].negate != t.negate)
                        return Lerp(to, from, t);
            }
        }
    }
    ShaderValue args[2] = { a, b };
    return Make(kAdd, 0, 0, 0, 2, args);
}

ShaderValue ShaderBuilder::Mul(ShaderValue a, ShaderValue b) {
    float x, y;
    if (IsLiteral(a, &x) && IsLiteral(b, &y)) return Literal(x * y);
    if (IsLiteral(a, 0.0f) || IsLiteral(b, 0.0f)) return Literal(0.0f);
    if (IsLiteral(a, 1.0f)) return b;
    if (IsLiteral(b, 1.0f)) return a;
    if (IsLiteral(a, -1.0f)) return Negate(b);
    if (IsLiteral(b, -1.0f)) return Negate(a);
    bool negate = a.negate != b.negate;   // signs come out of the product, so that -a*b and a*-b are one node
    a.negate = b.negate = false;
    // A component of a vector times the reciprocal square root of the vector's dot product with itself is
    // a component of nrm: the compiler finds the pattern, and its product then never fuses into a mad.
    for (int s = 0; s < 2; s++) {
        ShaderValue x = s ? b : a, r = s ? a : b;
        if (m_nodes[r.node].op != kRsq) continue;
        ShaderValue d = m_nodes[r.node].args[0];
        if (!d.negate && m_nodes[d.node].op == kAbs) d = m_nodes[d.node].args[0];
        const Node &dot = m_nodes[d.node];
        if (dot.op != kDp3) continue;
        bool square = true;
        for (int i = 0; i < 3; i++)
            if (dot.args[i].node != dot.args[3 + i].node || dot.args[i].negate != dot.args[3 + i].negate) square = false;
        if (!square) continue;
        for (int i = 0; i < 3; i++) {
            if (dot.args[i].node != x.node) continue;
            // The reciprocal square root rides along as a fourth operand, for an nrm that Assemble turns back
            // into a mul.
            ShaderValue args[4] = { dot.args[0], dot.args[1], dot.args[2], r };
            args[3].negate = false;
            ShaderValue v = Make(kNrm, i, 0, 0, 4, args);
            v.negate = negate != dot.args[i].negate;
            return v;
        }
    }
    ShaderValue args[2] = { a, b };
    ShaderValue v = Make(kMul, 0, 0, 0, 2, args);
    v.negate = negate;
    return v;
}

ShaderValue ShaderBuilder::Mad(ShaderValue a, ShaderValue b, ShaderValue c) {
    float x, y, z;
    if (IsLiteral(a, &x) && IsLiteral(b, &y) && IsLiteral(c, &z)) return Literal(fmaf(x, y, z));
    if (IsLiteral(a, 0.0f) || IsLiteral(b, 0.0f)) return c;
    if (IsLiteral(a, 1.0f)) return Add(b, c);
    if (IsLiteral(b, 1.0f)) return Add(a, c);
    if (IsLiteral(a, -1.0f)) return Add(Negate(b), c);
    if (IsLiteral(b, -1.0f)) return Add(Negate(a), c);
    if (IsLiteral(c, 0.0f)) return Mul(a, b);
    if (a.negate && b.negate) { a.negate = b.negate = false; }
    ShaderValue args[3] = { a, b, c };
    return Make(kMad, 0, 0, 0, 3, args);
}

// A dot product with at most one term that is not a literal zero is that term's product; otherwise every
// term, zeros included, stays in the dp.
static int LiveTerms(const ShaderValue *a, const ShaderValue *b, int n, bool (*zero)(const void *, ShaderValue),
                     const void *self, int *last) {
    int live = 0;
    for (int i = 0; i < n; i++)
        if (!zero(self, a[i]) && !zero(self, b[i])) { live++; *last = i; }
    return live;
}

ShaderValue ShaderBuilder::Dp3(const ShaderValue a[3], const ShaderValue b[3]) {
    auto zero = [](const void *self, ShaderValue v) { return ((const ShaderBuilder *)self)->IsLiteral(v, 0.0f); };
    int last = 0, live = LiveTerms(a, b, 3, zero, this, &last);
    if (live == 0) return Literal(0.0f);
    if (live == 1) return Mul(a[last], b[last]);
    ShaderValue args[6] = { a[0], a[1], a[2], b[0], b[1], b[2] };
    return Make(kDp3, 0, 0, 0, 6, args);
}

ShaderValue ShaderBuilder::Dp4(const ShaderValue a[4], const ShaderValue b[4]) {
    auto zero = [](const void *self, ShaderValue v) { return ((const ShaderBuilder *)self)->IsLiteral(v, 0.0f); };
    int last = 0, live = LiveTerms(a, b, 4, zero, this, &last);
    if (live == 0) return Literal(0.0f);
    if (live == 1) return Mul(a[last], b[last]);
    ShaderValue args[8] = { a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3] };
    return Make(kDp4, 0, 0, 0, 8, args);
}

ShaderValue ShaderBuilder::Min(ShaderValue a, ShaderValue b) {
    float x, y;
    if (IsLiteral(a, &x) && IsLiteral(b, &y)) return Literal(x < y ? x : y);
    float alo, ahi, blo, bhi;   // one operand never above the other: the min is that one
    Range(a, &alo, &ahi);
    Range(b, &blo, &bhi);
    if (ahi <= blo) return a;
    if (bhi <= alo) return b;
    ShaderValue args[2] = { a, b };
    return Make(kMin, 0, 0, 0, 2, args);
}

ShaderValue ShaderBuilder::Max(ShaderValue a, ShaderValue b) {
    float x, y;
    if (IsLiteral(a, &x) && IsLiteral(b, &y)) return Literal(x >= y ? x : y);
    float alo, ahi, blo, bhi;
    Range(a, &alo, &ahi);
    Range(b, &blo, &bhi);
    if (alo >= bhi) return a;
    if (blo >= ahi) return b;
    ShaderValue args[2] = { a, b };
    return Make(kMax, 0, 0, 0, 2, args);
}

ShaderValue ShaderBuilder::Slt(ShaderValue a, ShaderValue b) {
    float x, y;
    if (IsLiteral(a, &x) && IsLiteral(b, &y)) return Literal(x < y ? 1.0f : 0.0f);
    ShaderValue args[2] = { a, b };
    return Make(kSlt, 0, 0, 0, 2, args);
}

ShaderValue ShaderBuilder::Sge(ShaderValue a, ShaderValue b) {
    float x, y;
    if (IsLiteral(a, &x) && IsLiteral(b, &y)) return Literal(x >= y ? 1.0f : 0.0f);
    ShaderValue args[2] = { a, b };
    return Make(kSge, 0, 0, 0, 2, args);
}

ShaderValue ShaderBuilder::Cmp(ShaderValue a, ShaderValue b, ShaderValue c) {
    float x;
    if (IsLiteral(a, &x)) return x >= 0.0f ? b : c;
    ShaderValue args[3] = { a, b, c };
    return Make(kCmp, 0, 0, 0, 3, args);
}

ShaderValue ShaderBuilder::Rcp(ShaderValue a) {
    float x;
    if (IsLiteral(a, &x)) return Literal(1.0f / x);
    return Make(kRcp, 0, 0, 0, 1, &a);
}

ShaderValue ShaderBuilder::Rsq(ShaderValue a) {
    float x;
    if (IsLiteral(a, &x)) return Literal(1.0f / sqrtf(fabsf(x)));
    return Make(kRsq, 0, 0, 0, 1, &a);
}

ShaderValue ShaderBuilder::Frc(ShaderValue a) {
    float x;
    if (IsLiteral(a, &x)) return Literal(x - floorf(x));
    return Make(kFrc, 0, 0, 0, 1, &a);
}

// HLSL's floor is an intrinsic the compiler expands after it has fused products into mads, so its add is
// marked (bits 1) to stay an add.
ShaderValue ShaderBuilder::Floor(ShaderValue a) {
    float x;
    if (IsLiteral(a, &x)) return Literal(floorf(x));
    ShaderValue args[2] = { Negate(Frc(a)), a };
    return Make(kAdd, 0, 0, 1, 2, args);
}

ShaderValue ShaderBuilder::Abs(ShaderValue a) {
    float x;
    if (IsLiteral(a, &x)) return Literal(fabsf(x));
    a.negate = false;
    if (m_nodes[a.node].op == kAbs) return a;
    return Make(kAbs, 0, 0, 0, 1, &a);
}

ShaderValue ShaderBuilder::Exp(ShaderValue a) {
    float x;
    if (IsLiteral(a, &x)) return Literal(exp2f(x));
    return Make(kExp, 0, 0, 0, 1, &a);
}

ShaderValue ShaderBuilder::Log(ShaderValue a) {
    float x;
    if (IsLiteral(a, &x)) return Literal(log2f(fabsf(x)));
    return Make(kLog, 0, 0, 0, 1, &a);
}

ShaderValue ShaderBuilder::Saturate(ShaderValue a) {
    if (m_profile == kVertexShader)
        return Min(Max(a, Literal(0.0f)), Literal(1.0f));
    float x;
    if (IsLiteral(a, &x)) return Literal(x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x);
    float lo, hi;
    Range(a, &lo, &hi);
    if (lo >= 0.0f && hi <= 1.0f) return a;
    return Make(kSat, 0, 0, 0, 1, &a);
}

ShaderValue ShaderBuilder::Lerp(ShaderValue a, ShaderValue b, ShaderValue t) {
    if (IsLiteral(t, 0.0f)) return a;
    if (IsLiteral(t, 1.0f)) return b;
    return Mad(t, Sub(b, a), a);
}

void ShaderBuilder::Lit(ShaderValue x, ShaderValue y, ShaderValue w, ShaderValue out[4]) {
    ShaderValue args[3] = { x, y, w };
    for (int c = 0; c < 4; c++) out[c] = Make(kLit, c, 0, 0, 3, args);
}

void ShaderBuilder::Sample(int sampler, bool cube, const ShaderValue *coord, ShaderValue out[4]) {
    for (int c = 0; c < 4; c++) out[c] = Make(kTex, c, sampler, cube ? 1 : 0, cube ? 3 : 2, coord);
}

void ShaderBuilder::Hold(ShaderValue value, int variable, int lane) {
    Node &n = m_nodes[value.node];
    if (n.variable < 0 && !IsLeaf(n.op)) {
        n.variable = (int16_t)variable;
        n.lane = (int8_t)lane;
    }
}

void ShaderBuilder::DeclareInput(int reg, int usage, int usageIndex) {
    InputDecl d = { reg, usage, usageIndex };
    m_inputs.push_back(d);
}

void ShaderBuilder::SetOutput(int output, int component, ShaderValue value) {
    for (size_t i = 0; i < m_outputs.size(); i++)
        if (m_outputs[i].output == output && m_outputs[i].component == component) { m_outputs[i].value = value; return; }
    OutputSlot o = { output, component, value };
    m_outputs.push_back(o);
}



// ---------------------------------------------------------------------------------------------------------------
// Lowering
//
// Values live in components of the twelve temporary registers from the instruction that computes them to
// their last use. An instruction's operands each come from one register, so the lanes of a lane-wise
// instruction (add, mul, mad...) must find their operands together: in any components for a vertex shader,
// which swizzles freely, but for a pixel shader (ps_2_0 swizzles are the identity or one component
// replicated) each lane in its own component. Where they are not, movs gather them into a temporary first,
// and the copies stay there for later instructions that read the same values together until the
// components are needed. The same moves happen when an instruction would read more constant or input
// registers than the profile has read ports.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// d3d9types.h: register types, opcodes, and the fields of instruction and parameter tokens.
enum {
    REG_TEMP = 0, REG_INPUT = 1, REG_CONST = 2, REG_ADDR = 3, REG_TEXTURE = 3, REG_RASTOUT = 4, REG_ATTROUT = 5,
    REG_TEXCRDOUT = 6, REG_COLOROUT = 8, REG_SAMPLER = 10,
};
enum {
    OP_MOV = 1, OP_ADD = 2, OP_MAD = 4, OP_MUL = 5, OP_RCP = 6, OP_RSQ = 7, OP_DP3 = 8, OP_DP4 = 9, OP_MIN = 10,
    OP_MAX = 11, OP_SLT = 12, OP_SGE = 13, OP_EXP = 14, OP_LOG = 15, OP_LIT = 16, OP_LRP = 18, OP_FRC = 19,
    OP_DCL = 31, OP_ABS = 35, OP_NRM = 36, OP_MOVA = 46, OP_TEX = 66, OP_DEF = 81, OP_CMP = 88,
};
const uint32_t kParam = 0x80000000u;
const uint32_t kRelativeAddressing = 1u << 13;
const uint32_t kSaturate = 1u << 20;
const uint32_t kNegateSource = 1u << 24;
const uint32_t kSampler2D = 2u << 27, kSamplerCube = 3u << 27;

uint32_t RegisterBits(int type, int index) {
    return kParam | ((uint32_t)(type & 7) << 28) | ((uint32_t)(type & 0x18) << 8) | (uint32_t)index;
}

int Opcode(int op) {
    switch (op) {
        case kAdd: return OP_ADD; case kMul: return OP_MUL; case kMad: return OP_MAD; case kDp3: return OP_DP3;
        case kDp4: return OP_DP4; case kMin: return OP_MIN; case kMax: return OP_MAX; case kSlt: return OP_SLT;
        case kSge: return OP_SGE; case kCmp: return OP_CMP; case kRcp: return OP_RCP; case kRsq: return OP_RSQ;
        case kFrc: return OP_FRC; case kAbs: return OP_ABS; case kExp: return OP_EXP; case kLog: return OP_LOG;
        case kLit: return OP_LIT; case kTex: return OP_TEX; case kNrm: return OP_NRM; case kLrp: return OP_LRP;
        default: return OP_MOV;   // kSat: mov_sat
    }
}

// Instructions whose lanes are independent, so that several values can share one under a write mask.
bool IsLaneOp(int op) {
    switch (op) {
        case kAdd: case kMul: case kMad: case kMin: case kMax: case kSlt: case kSge: case kCmp: case kFrc:
        case kAbs: case kSat: case kLrp: return true;
        default: return false;
    }
}

// Instructions that compute several channels from one vector operand: one instruction writes all of them.
bool IsChannelOp(int op) { return op == kLit || op == kTex || op == kNrm; }

// A pixel shader declares an input's components as .x, .xy, .xyz or .xyzw: the runtime refuses any other mask.
uint8_t PrefixMask(uint8_t mask) {
    return (uint8_t)(mask & 8 ? 0xF : mask & 4 ? 0x7 : mask & 2 ? 0x3 : 0x1);
}

int LowestLane(uint8_t mask) {
    for (int l = 0; l < 4; l++) if (mask & (1 << l)) return l;
    return 0;
}

const int kNone = -1;
const int kGathered = -2;   // a temporary component holding a moved operand until its instruction is written
const int kMaxTemps = 12;

struct Place {
    int file;      // a REG_ type, or kNone for a literal not yet given a def'd register
    int reg;
    int address;   // a relative constant's address node, else kNone
};

struct Source {
    Place place;
    uint8_t swizzle[4];
    bool negate;
};

struct OutputUse { int output, component; };

class Lowering {
public:
    typedef ShaderBuilder::Node Node;
    struct OutputValue { int output, component; ShaderValue value; };
    struct InputDecl { int reg, usage, usageIndex; };

    Lowering(const std::vector<Node> &nodes, bool pixel) : m_nodes(nodes), m_pixel(pixel) {}
    bool Run(const std::vector<OutputValue> &outputs, const std::vector<InputDecl> &inputs,
             std::vector<uint32_t> *tokens, char *error, size_t errorSize);

private:
    // A node as it is lowered: a mad of a difference may become an lrp, an instruction may take a _sat.
    struct Work { int op; bool sat; int argCount; ShaderValue args[8]; };

    bool Fail(const char *what, int n = 0);
    float LiteralValue(ShaderValue v) const;
    void Use(ShaderValue v, int delta);
    void Release(int node);
    bool InTemp(int node, int reg, int *comp, int preferred) const;
    bool Evictable(int reg, int comp) const;
    int FindTemp(const int *comps, int count, bool exact, int *chosen, bool evictPinned);
    bool Compact();
    bool Locate(ShaderValue v, Place *place, int *comp) const;
    int LaneOf(int node) const;
    int NewDefRegister() const;
    bool PlaceLiterals(int slotCount, const float (*values)[4], const uint8_t *masks, const bool *single,
                       int *defReg, uint8_t (*comps)[4]);
    bool LiteralSource(float value, Source *out);
    bool Resolve(const ShaderValue *want, uint8_t mask, bool texCoord, Source *out);
    bool Gather(const ShaderValue *want, uint8_t mask, bool texCoord, Source *out);
    bool MoveToTemp(Source *src, uint8_t mask);
    void SetAddress(int node);
    void EmitInstruction(int opcode, bool sat, uint32_t dest, const Source *src, int srcCount);
    uint32_t OutputDest(int output, uint32_t writeMask) const;
    bool CanJoin(const int *members, int count, int candidate) const;
    bool EmitGroup(const int *members, int count);
    void WriteOutputs(int node);
    void EndInstruction();
    void ModelCompilerComponent(int node, const int *members, int count);

    const std::vector<Node> &m_nodes;
    bool m_pixel;
    std::vector<Work> m_work;
    std::vector<int> m_uses;
    std::vector<bool> m_live, m_done;
    std::vector<std::vector<OutputUse> > m_outputUses;
    int m_temp[kMaxTemps][4];   // the node a component holds (a value or a copy of one), kNone or kGathered
    uint32_t m_pinned;          // registers the instruction being written reads
    uint32_t m_avoid;           // registers its result may not go to (an lrp or nrm reading them)
    // Dependent texture reads (pixel shaders): the runtime counts how deep each register's contents are in
    // texture reads - per whole register, a partial write keeping the deeper of old and new - and refuses a
    // texld past ps_2_0's limit. So a value goes to a register no deeper than itself where there is one.
    std::vector<int> m_depth;   // per node: texture reads under it
    int m_registerDepth[kMaxTemps];
    int m_wantDepth;            // the depth of what is being placed, or a large number for no preference
    int m_variableTemp[64];     // the temporary each source register variable was given
    int m_compilerLanes[64][4]; // per source register variable, the node in each component of the compiler's temporary
    std::vector<int> m_compilerComponent;   // per node, its component there, or kNone
    struct Def { int reg; float value[4]; uint8_t used; };
    std::vector<Def> m_defs;
    bool m_constRead[256];
    bool m_relative;
    int m_address;   // the node a0.x holds, or kNone
    std::vector<uint32_t> m_code;
    uint8_t m_inputMask[16], m_texMask[8];
    uint32_t m_samplerType[16];
    int m_arithmetic, m_texture;
    char m_error[160];
    bool m_failed;
};

bool Lowering::Fail(const char *what, int n) {
    if (!m_failed) snprintf(m_error, sizeof(m_error), what, n);
    m_failed = true;
    return false;
}

float Lowering::LiteralValue(ShaderValue v) const {
    float f = BitsFloat(m_nodes[v.node].bits);
    return v.negate ? -f : f;
}

void Lowering::Use(ShaderValue v, int delta) {
    m_uses[v.node] += delta;
    if (m_nodes[v.node].op == kRelative)
        m_uses[m_nodes[v.node].args[0].node] += delta;
}

// A value nothing reads any more gives up every component holding it.
void Lowering::Release(int node) {
    if (m_uses[node] > 0)
        return;
    for (int r = 0; r < kMaxTemps; r++)
        for (int c = 0; c < 4; c++)
            if (m_temp[r][c] == node) m_temp[r][c] = kNone;
}

// Where the compiler keeps a value of a source register: in the register's temporary, in the value's own lane
// unless a value of the register made earlier and still needed - if only by this instruction - holds it, else
// in the lowest component free.
void Lowering::ModelCompilerComponent(int node, const int *members, int count) {
    int v = m_nodes[node].variable, lane = m_nodes[node].lane;
    if (v < 0 || v >= 64 || lane < 0) return;
    bool taken[4];
    for (int c = 0; c < 4; c++) {
        int holder = m_compilerLanes[v][c];
        taken[c] = holder >= 0 && holder != node && m_uses[holder] > 0;
        for (int i = 0; i < count && holder >= 0 && !taken[c]; i++)
            for (int k = 0; k < m_work[members[i]].argCount; k++)
                if (m_work[members[i]].args[k].node == holder) taken[c] = true;
    }
    int c = taken[lane] ? kNone : lane;
    for (int k = 0; k < 4 && c == kNone; k++) if (!taken[k]) c = k;
    if (c == kNone) return;
    m_compilerLanes[v][c] = node;
    m_compilerComponent[node] = c;
}

void Lowering::EndInstruction() {
    for (int r = 0; r < kMaxTemps; r++)
        for (int c = 0; c < 4; c++)
            if (m_temp[r][c] == kGathered) m_temp[r][c] = kNone;
    m_pinned = 0;
}

// Whether register reg holds node, and in which component (the preferred one if it is there).
bool Lowering::InTemp(int node, int reg, int *comp, int preferred) const {
    if (preferred >= 0 && m_temp[reg][preferred] == node) { *comp = preferred; return true; }
    for (int c = 0; c < 4; c++)
        if (m_temp[reg][c] == node) { *comp = c; return true; }
    return false;
}

// A component that can be taken without losing anything: a copy of a leaf, which can be read from its own
// register again, or a value held somewhere else too.
bool Lowering::Evictable(int reg, int comp) const {
    int node = m_temp[reg][comp];
    if (node < 0 || (m_pinned & (1u << reg))) return false;
    if (IsLeaf(m_work[node].op)) return true;
    for (int r = 0; r < kMaxTemps; r++)
        for (int c = 0; c < 4; c++)
            if ((r != reg || c != comp) && m_temp[r][c] == node) return true;
    return false;
}

// A temporary register with the given components free; or, unless exact, with count free components of any
// kind. Copies are evicted where nothing else will do. The components used come back in chosen.
int Lowering::FindTemp(const int *comps, int count, bool exact, int *chosen, bool evictPinned) {
    uint32_t pinned = m_pinned;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1 && evictPinned) m_pinned = m_avoid;
        auto usable = [&](int r, int c) { return m_temp[r][c] == kNone || (pass == 1 && Evictable(r, c)); };
        int best = kNone, bestCost = 1 << 20;
        int bestChosen[4];
        for (int r = 0; r < kMaxTemps; r++) {
            if (m_avoid & (1u << r)) continue;
            bool fits = true;
            int cost = 0;
            for (int i = 0; i < count && fits; i++) {
                if (comps[i] < 0 || !usable(r, comps[i])) fits = false;
                else if (m_temp[r][comps[i]] != kNone) cost++;
                for (int j = 0; j < i; j++) if (comps[j] == comps[i]) fits = false;
            }
            int got[4];
            if (fits) {
                for (int i = 0; i < count; i++) got[i] = comps[i];
            } else if (!exact) {
                int n = 0;
                cost = 0;
                for (int c = 0; c < 4 && n < count; c++) if (m_temp[r][c] == kNone) got[n++] = c;
                for (int c = 0; c < 4 && n < count; c++) if (m_temp[r][c] != kNone && usable(r, c)) { got[n++] = c; cost++; }
                fits = n == count;
                cost += 4;   // a register where the components fit as they are is better
            }
            if (fits) {   // then the fullest register: scalars pack together, whole registers stay free
                int freeLeft = 0;
                for (int c = 0; c < 4; c++) if (m_temp[r][c] == kNone) freeLeft++;
                cost = cost * 8 + freeLeft - count;
                if (m_pixel && m_registerDepth[r] > m_wantDepth) cost += 64;
            }
            if (fits && cost < bestCost) {
                best = r;
                bestCost = cost;
                memcpy(bestChosen, got, sizeof(got));
            }
        }
        if (best != kNone) {
            for (int i = 0; i < count; i++) {
                chosen[i] = bestChosen[i];
                m_temp[best][chosen[i]] = kNone;   // an evicted copy goes
            }
            m_pinned = pinned;
            return best;
        }
    }
    m_pinned = pinned;
    if (Compact())
        return FindTemp(comps, count, exact, chosen, evictPinned);
    return kNone;
}

// Empties the emptiest register that can be emptied by moving its values into other registers' free
// components (copies just go), so that a value or operand needing a whole register finds one.
bool Lowering::Compact() {
    int order[kMaxTemps], n = 0;
    for (int r = 0; r < kMaxTemps; r++) {
        if (m_pinned & (1u << r)) continue;
        int used = 0;
        for (int c = 0; c < 4; c++) if (m_temp[r][c] != kNone) used++;
        if (used == 0 || used == 4) continue;
        order[n++] = r;
    }
    for (int i = 0; i < n; i++)   // fewest values first
        for (int j = i + 1; j < n; j++) {
            int ui = 0, uj = 0;
            for (int c = 0; c < 4; c++) { ui += m_temp[order[i]][c] != kNone; uj += m_temp[order[j]][c] != kNone; }
            if (uj < ui) { int t = order[i]; order[i] = order[j]; order[j] = t; }
        }
    for (int i = 0; i < n; i++) {
        int r = order[i];
        // Plan: each value not held elsewhere needs a free component in another register.
        int moveTo[4][2], moves = 0;
        int taken[kMaxTemps][4];
        memcpy(taken, m_temp, sizeof(taken));
        bool possible = true;
        for (int c = 0; c < 4 && possible; c++) {
            int node = m_temp[r][c];
            if (node < 0) { if (node == kGathered) possible = false; continue; }
            if (Evictable(r, c)) continue;
            bool placed = false;
            for (int q = 0; q < kMaxTemps && !placed; q++) {
                if (q == r || (m_pinned & (1u << q))) continue;
                for (int d = 0; d < 4 && !placed; d++)
                    if (taken[q][d] == kNone) { taken[q][d] = node; moveTo[moves][0] = q; moveTo[moves][1] = d; placed = true; }
            }
            if (!placed) possible = false;
            else moves++;
        }
        if (!possible) continue;
        int m = 0;
        for (int c = 0; c < 4; c++) {
            int node = m_temp[r][c];
            if (node < 0) continue;
            if (!Evictable(r, c)) {
                Source s;
                s.place.file = REG_TEMP; s.place.reg = r; s.place.address = kNone;
                s.negate = false;
                memset(s.swizzle, c, 4);
                int q = moveTo[m][0], d = moveTo[m][1];
                m++;
                EmitInstruction(OP_MOV, false, RegisterBits(REG_TEMP, q) | (1u << (16 + d)), &s, 1);
                m_temp[q][d] = node;
            }
            m_temp[r][c] = kNone;
        }
        return true;
    }
    return false;
}

// Where a value is read from, if it is somewhere already. A literal is found with file kNone.
bool Lowering::Locate(ShaderValue v, Place *place, int *comp) const {
    const Node &n = m_nodes[v.node];
    place->address = kNone;
    place->reg = n.reg;
    *comp = n.component;
    switch (n.op) {
        case kLiteral: place->file = kNone; place->reg = 0; *comp = 0; return true;
        case kInput: place->file = REG_INPUT; return true;
        case kTexCoord: place->file = REG_TEXTURE; return true;
        case kConstant: place->file = REG_CONST; return true;
        case kRelative: place->file = REG_CONST; place->address = n.args[0].node; return true;
        default:
            for (int r = 0; r < kMaxTemps; r++)
                if (InTemp(v.node, r, comp, -1)) { place->file = REG_TEMP; place->reg = r; return true; }
            return false;
    }
}

// The lane a lane-wise node was made for, else the component its first operand that is not a literal sits in,
// or kNone.
int Lowering::LaneOf(int node) const {
    if (m_nodes[node].lane >= 0) return m_nodes[node].lane;
    for (int k = 0; k < m_work[node].argCount; k++) {
        Place p;
        int c;
        if (Locate(m_work[node].args[k], &p, &c) && p.file != kNone) return c;
    }
    return kNone;
}

// The lowest constant register the program does not read: past c191 when it reads relative to a0, since
// those reads could land anywhere in the game's 192, and never the backend's own c200-c204.
int Lowering::NewDefRegister() const {
    int limit = m_pixel ? 32 : 256;
    for (int r = m_relative ? 192 : 0; r < limit; r++) {
        if (m_constRead[r] || (!m_pixel && r >= 200 && r <= 204)) continue;
        bool taken = false;
        for (size_t i = 0; i < m_defs.size(); i++) if (m_defs[i].reg == r) taken = true;
        if (!taken) return r;
    }
    return kNone;
}

// Places the literal operands of one instruction in one def'd register. Per slot, the value each lane in its
// mask reads: one value replicated (single), or - in a pixel shader - each in its own lane's component.
bool Lowering::PlaceLiterals(int slotCount, const float (*values)[4], const uint8_t *masks, const bool *single,
                             int *defReg, uint8_t (*comps)[4]) {
    for (size_t d = 0; d <= m_defs.size(); d++) {
        Def trial;
        if (d == m_defs.size()) {
            trial.reg = NewDefRegister();
            if (trial.reg == kNone) return false;
            trial.used = 0;
            memset(trial.value, 0, sizeof(trial.value));
        } else {
            trial = m_defs[d];
        }
        bool fits = true;
        for (int s = 0; s < slotCount && fits; s++) {
            for (int lane = 0; lane < 4 && fits; lane++) {
                if (!(masks[s] & (1 << lane))) continue;
                uint32_t bits = FloatBits(values[s][lane]);
                int found = kNone;
                if (single[s] || !m_pixel) {
                    for (int c = 0; c < 4 && found == kNone; c++)
                        if ((trial.used & (1 << c)) && FloatBits(trial.value[c]) == bits) found = c;
                    for (int c = 0; c < 4 && found == kNone; c++)
                        if (!(trial.used & (1 << c))) found = c;
                } else if (!(trial.used & (1 << lane)) || FloatBits(trial.value[lane]) == bits) {
                    found = lane;
                }
                if (found == kNone) { fits = false; break; }
                trial.value[found] = values[s][lane];
                trial.used |= (uint8_t)(1 << found);
                comps[s][lane] = (uint8_t)found;
            }
        }
        if (!fits) continue;
        if (d == m_defs.size()) m_defs.push_back(trial); else m_defs[d] = trial;
        *defReg = trial.reg;
        return true;
    }
    return false;
}

bool Lowering::LiteralSource(float value, Source *out) {
    float values[1][4] = { { value, value, value, value } };
    uint8_t mask = 1, comps[1][4];
    bool single = true;
    int reg;
    if (!PlaceLiterals(1, values, &mask, &single, &reg, comps)) return Fail("out of constant registers");
    out->place.file = REG_CONST; out->place.reg = reg; out->place.address = kNone;
    out->negate = false;
    memset(out->swizzle, comps[0][0], 4);
    return true;
}

void Lowering::SetAddress(int node) {
    if (m_address == node)
        return;
    m_address = node;   // first, so that the mova's own source does not ask again
    Source s;
    ShaderValue v = { (uint16_t)node, false };
    int comp = 0;
    if (IsLiteralOp(m_nodes[node].op)) {
        LiteralSource(LiteralValue(v), &s);
    } else {
        Locate(v, &s.place, &comp);
        s.negate = false;
        memset(s.swizzle, comp, 4);
    }
    EmitInstruction(OP_MOVA, false, RegisterBits(REG_ADDR, 0) | (1u << 16), &s, 1);
}

void Lowering::EmitInstruction(int opcode, bool sat, uint32_t dest, const Source *src, int srcCount) {
    for (int i = 0; i < srcCount; i++)
        if (src[i].place.address != kNone) SetAddress(src[i].place.address);
    uint32_t params[12];
    int n = 0;
    params[n++] = dest | (sat ? kSaturate : 0);
    for (int i = 0; i < srcCount; i++) {
        const Source &s = src[i];
        uint32_t swizzle = s.swizzle[0] | (s.swizzle[1] << 2) | (s.swizzle[2] << 4) | (s.swizzle[3] << 6);
        uint32_t t = RegisterBits(s.place.file, s.place.reg) | (swizzle << 16) | (s.negate ? kNegateSource : 0);
        if (s.place.address != kNone) {
            params[n++] = t | kRelativeAddressing;
            params[n++] = RegisterBits(REG_ADDR, 0);   // a0.x
        } else {
            params[n++] = t;
        }
        if (m_pixel && s.place.file == REG_INPUT)
            for (int l = 0; l < 4; l++) m_inputMask[s.place.reg & 15] |= (uint8_t)(1 << s.swizzle[l]);
        if (m_pixel && s.place.file == REG_TEXTURE)
            for (int l = 0; l < 4; l++) m_texMask[s.place.reg & 7] |= (uint8_t)(opcode == OP_TEX ? 0xF : 1 << s.swizzle[l]);
    }
    if (m_pixel && ((dest >> 28) & 7) == REG_TEMP && ((dest >> 11) & 3) == 0) {
        int depth = 0, reg = (int)(dest & 0x7FF);
        for (int i = 0; i < srcCount; i++)
            if (src[i].place.file == REG_TEMP && m_registerDepth[src[i].place.reg] > depth) depth = m_registerDepth[src[i].place.reg];
        if (opcode == OP_TEX) depth++;
        if (((dest >> 16) & 0xF) == 0xF || depth > m_registerDepth[reg]) m_registerDepth[reg] = depth;
    }
    m_code.push_back((uint32_t)opcode | ((uint32_t)n << 24));
    m_code.insert(m_code.end(), params, params + n);
    if (opcode == OP_TEX) m_texture++; else m_arithmetic++;
}

// Copies the wanted lane values into a temporary, each in its own lane's component. What is copied
// unnegated stays as a copy for later reads.
bool Lowering::Gather(const ShaderValue *want, uint8_t mask, bool texCoord, Source *out) {
    int lanes[4], count = 0, chosen[4];
    for (int l = 0; l < 4; l++) if (mask & (1 << l)) lanes[count++] = l;
    m_wantDepth = 0;
    for (int l = 0; l < 4; l++) if ((mask & (1 << l)) && m_depth[want[l].node] > m_wantDepth) m_wantDepth = m_depth[want[l].node];
    int reg = FindTemp(lanes, count, m_pixel || texCoord, chosen, false);   // a vertex shader can swizzle them back
    m_wantDepth = 1 << 20;
    if (reg == kNone) return Fail("out of temporary registers gathering an operand");
    m_pinned |= 1u << reg;
    int at[4] = { 0, 1, 2, 3 };   // the component each lane goes to
    for (int i = 0; i < count; i++) at[lanes[i]] = chosen[i];
    // A lane whose value is negated where the others are not is copied negated, and so is no copy.
    int plain = 0, negated = 0;
    for (int l = 0; l < 4; l++) if (mask & (1 << l)) (want[l].negate ? negated : plain)++;
    bool negateAll = negated > plain;
    for (int l = 0; l < 4; l++) {
        if (!(mask & (1 << l))) continue;
        Source s;
        int comp;
        bool flip = want[l].negate != negateAll;
        if (IsLiteralOp(m_nodes[want[l].node].op)) {
            if (!LiteralSource(BitsFloat(m_nodes[want[l].node].bits) * (flip ? -1.0f : 1.0f), &s)) return false;
        } else if (Locate(want[l], &s.place, &comp)) {
            s.negate = flip;
            memset(s.swizzle, comp, 4);
        } else {
            return Fail("internal: an operand is used before it is computed");
        }
        EmitInstruction(OP_MOV, false, RegisterBits(REG_TEMP, reg) | (1u << (16 + at[l])), &s, 1);
        m_temp[reg][at[l]] = flip ? kGathered : want[l].node;
    }
    out->place.file = REG_TEMP; out->place.reg = reg; out->place.address = kNone;
    out->negate = negateAll;
    int last = at[lanes[0]];
    for (int l = 0; l < 4; l++) {
        if (mask & (1 << l)) last = at[l];
        out->swizzle[l] = (uint8_t)((mask & (1 << l)) ? at[l] : (m_pixel || texCoord) ? l : last);
    }
    return true;
}

// Moves an operand that is over a read-port limit into a temporary, lanes in place.
bool Lowering::MoveToTemp(Source *src, uint8_t mask) {
    int lanes[4], count = 0, chosen[4];
    for (int l = 0; l < 4; l++) if (mask & (1 << l)) lanes[count++] = l;
    int reg = FindTemp(lanes, count, true, chosen, false);
    if (reg == kNone) return Fail("out of temporary registers for a read-port move");
    Source from = *src;
    from.negate = false;
    EmitInstruction(OP_MOV, false, RegisterBits(REG_TEMP, reg) | ((uint32_t)mask << 16), &from, 1);
    for (int i = 0; i < count; i++) m_temp[reg][lanes[i]] = kGathered;
    m_pinned |= 1u << reg;
    src->place.file = REG_TEMP; src->place.reg = reg; src->place.address = kNone;
    for (int l = 0; l < 4; l++) src->swizzle[l] = (uint8_t)l;
    return true;
}

// One source operand: the value each lane in mask reads. Finds a register holding them all, with a swizzle
// the profile allows - a temporary (where they were computed or copied before) or the leaves' own register -
// or gathers them. All-literal operands come back with place.file kNone, for the caller to place together
// with the instruction's other literals.
bool Lowering::Resolve(const ShaderValue *want, uint8_t mask, bool texCoord, Source *out) {
    bool negate = want[LowestLane(mask)].negate, sameSign = true, literals = true;
    for (int l = 0; l < 4; l++) {
        if (!(mask & (1 << l))) continue;
        if (want[l].negate != negate) sameSign = false;
        if (!IsLiteralOp(m_nodes[want[l].node].op)) literals = false;
    }
    if (literals && !texCoord) {
        out->place.file = kNone; out->place.reg = 0; out->place.address = kNone;
        out->negate = false;
        for (int l = 0; l < 4; l++) out->swizzle[l] = (uint8_t)l;
        return true;
    }
    // The swizzle a set of lane components makes, if the profile allows it.
    auto legal = [&](const int *comp) {
        int first = comp[LowestLane(mask)];
        bool replicate = true, identity = true;
        for (int l = 0; l < 4; l++) {
            if (!(mask & (1 << l))) continue;
            if (comp[l] != first) replicate = false;
            if (comp[l] != l) identity = false;
        }
        if (texCoord) return identity;
        return !m_pixel || replicate || identity;
    };
    auto fill = [&](const int *comp, const Place &place) {
        out->place = place;
        out->negate = negate;
        int first = comp[LowestLane(mask)], last = first;
        bool replicate = true;
        for (int l = 0; l < 4; l++) if ((mask & (1 << l)) && comp[l] != first) replicate = false;
        for (int l = 0; l < 4; l++) {
            if (mask & (1 << l)) last = comp[l];
            out->swizzle[l] = (uint8_t)((mask & (1 << l)) ? comp[l] : (texCoord || (m_pixel && !replicate)) ? l : last);
        }
        if (place.file == REG_TEMP) m_pinned |= 1u << place.reg;
    };
    if (sameSign) {
        // A temporary holding every lane's value.
        for (int r = 0; r < kMaxTemps; r++) {
            int comp[4] = { 0, 0, 0, 0 };
            bool all = true;
            for (int l = 0; l < 4 && all; l++)
                if (mask & (1 << l)) all = InTemp(want[l].node, r, &comp[l], l);
            if (all && legal(comp)) {
                Place p = { REG_TEMP, r, kNone };
                fill(comp, p);
                return true;
            }
        }
        // The leaves' own register.
        Place common = { kNone, 0, kNone };
        int comp[4] = { 0, 0, 0, 0 };
        bool together = true, first = true;
        for (int l = 0; l < 4 && together; l++) {
            if (!(mask & (1 << l))) continue;
            Place p;
            if (!IsLeaf(m_nodes[want[l].node].op) || !Locate(want[l], &p, &comp[l]) || p.file == kNone) { together = false; break; }
            if (first) { common = p; first = false; }
            else if (p.file != common.file || p.reg != common.reg || p.address != common.address) together = false;
        }
        bool fileOk = !texCoord || common.file == REG_TEXTURE;
        if (together && fileOk && legal(comp)) {
            fill(comp, common);
            return true;
        }
    }
    return Gather(want, mask, texCoord, out);
}

uint32_t Lowering::OutputDest(int output, uint32_t writeMask) const {
    switch (output) {
        case ShaderBuilder::kOutPosition: return RegisterBits(REG_RASTOUT, 0) | (writeMask << 16);
        case ShaderBuilder::kOutFog: return RegisterBits(REG_RASTOUT, 1) | 0xF0000u;   // scalar registers, written whole
        case ShaderBuilder::kOutPointSize: return RegisterBits(REG_RASTOUT, 2) | 0xF0000u;
        case ShaderBuilder::kOutDiffuse: return RegisterBits(REG_ATTROUT, 0) | (writeMask << 16);
        case ShaderBuilder::kOutSpecular: return RegisterBits(REG_ATTROUT, 1) | (writeMask << 16);
        default: return RegisterBits(REG_TEXCRDOUT, output - ShaderBuilder::kOutTexCoord0) | (writeMask << 16);
    }
}

// The output moves for a value just computed into a temporary (vertex shaders; a pixel shader's colour is
// written at the end).
void Lowering::WriteOutputs(int node) {
    if (m_pixel || m_outputUses[node].empty())
        return;
    ShaderValue v = { (uint16_t)node, false };
    Source s;
    int comp;
    if (!Locate(v, &s.place, &comp)) return;
    s.negate = false;
    memset(s.swizzle, comp, 4);
    for (size_t i = 0; i < m_outputUses[node].size(); i++) {
        EmitInstruction(OP_MOV, false, OutputDest(m_outputUses[node][i].output, 1u << m_outputUses[node][i].component), &s, 1);
        m_uses[node]--;
    }
    m_outputUses[node].clear();
    Release(node);
}

// Whether candidate can share members' instruction: the same operation, and each operand slot of each
// member read from one register (or all literals) - in a pixel shader every member in its own lane.
bool Lowering::CanJoin(const int *members, int count, int candidate) const {
    const Work &a = m_work[members[0]], &b = m_work[candidate];
    if (a.op != b.op || a.sat != b.sat || a.argCount != b.argCount)
        return false;
    for (int k = 0; k < a.argCount; k++) {
        Place pa, pb;
        int ca, cb;
        if (!Locate(a.args[k], &pa, &ca) || !Locate(b.args[k], &pb, &cb)) return false;
        if ((pa.file == kNone) != (pb.file == kNone)) return false;
        if (pa.file != kNone && (pa.file != pb.file || pa.reg != pb.reg || pa.address != pb.address ||
                                 a.args[k].negate != b.args[k].negate)) return false;
    }
    if (!m_pixel)
        return true;
    int lanes[5];
    for (int i = 0; i < count; i++) lanes[i] = LaneOf(members[i]);
    lanes[count] = LaneOf(candidate);
    for (int i = 0; i <= count; i++) {
        if (lanes[i] == kNone) return false;
        for (int j = 0; j < i; j++) if (lanes[j] == lanes[i]) return false;
    }
    for (int k = 0; k < b.argCount; k++) {
        bool shared = true;
        for (int i = 0; i < count; i++)
            if (m_work[members[i]].args[k].node != b.args[k].node) shared = false;
        if (shared) continue;
        for (int i = 0; i <= count; i++) {
            const Work &w = m_work[i < count ? members[i] : candidate];
            Place p;
            int c;
            Locate(w.args[k], &p, &c);
            if (p.file != kNone && c != lanes[i]) return false;
        }
    }
    return true;
}

bool Lowering::EmitGroup(const int *members, int count) {
    const Work &w = m_work[members[0]];
    int op = w.op;
    bool laneOp = IsLaneOp(op);
    m_pinned = 0;

    // Lane i of a lane-wise instruction holds members[i]: the component its operands are in where the
    // members' differ, so that a pixel shader reads them unswizzled.
    int lane[4] = { 0, 1, 2, 3 };
    if (laneOp) {
        bool distinct = true;
        for (int i = 0; i < count; i++) {
            lane[i] = LaneOf(members[i]);
            if (lane[i] == kNone) distinct = false;
            for (int j = 0; j < i; j++) if (lane[j] == lane[i]) distinct = false;
        }
        if (!distinct) for (int i = 0; i < count; i++) lane[i] = i;
    }

    // want[k][l]: the value lane l of operand k reads.
    ShaderValue want[4][4];
    uint8_t mask[4] = { 0, 0, 0, 0 };
    bool texCoord[4] = { false, false, false, false };
    int srcCount = 1;
    memset(want, 0, sizeof(want));
    if (laneOp) {
        srcCount = w.argCount;
        for (int k = 0; k < srcCount; k++)
            for (int i = 0; i < count; i++) { want[k][lane[i]] = m_work[members[i]].args[k]; mask[k] |= (uint8_t)(1 << lane[i]); }
    } else if (op == kDp3 || op == kDp4) {
        int n = op == kDp3 ? 3 : 4;
        srcCount = 2;
        for (int k = 0; k < 2; k++)
            for (int l = 0; l < n; l++) { want[k][l] = w.args[k * n + l]; mask[k] |= (uint8_t)(1 << l); }
    } else if (op == kLit) {
        want[0][0] = w.args[0]; want[0][1] = w.args[1]; want[0][3] = w.args[2];
        mask[0] = 0xB;
    } else if (op == kTex || op == kNrm) {
        for (int l = 0; l < w.argCount; l++) { want[0][l] = w.args[l]; mask[0] |= (uint8_t)(1 << l); }
        texCoord[0] = op == kTex;
    } else {   // rcp, rsq, exp, log: one component, replicated
        want[0][0] = w.args[0];
        mask[0] = 1;
    }

    Source src[4];
    for (int k = 0; k < srcCount; k++)
        if (!Resolve(want[k], mask[k], texCoord[k], &src[k])) return false;
    // The compiler reads a dp's terms in the order of the components they sit in, whatever their order in
    // the source - which changes how the sum rounds - so where all the terms are values of one source register,
    // they are put in the order of the components the compiler gave them (see ModelCompilerComponent).
    if ((op == kDp3 || op == kDp4) && !m_pixel) {
        int n = op == kDp3 ? 3 : 4, key = kNone, component[4];
        for (int k = 0; k < 2 && key == kNone; k++) {
            bool modelled = true;
            for (int l = 0; l < n && modelled; l++) {
                int node = want[k][l].node;
                component[l] = m_compilerComponent[node];
                modelled = component[l] >= 0 && m_nodes[node].variable == m_nodes[want[k][0].node].variable;
            }
            if (modelled) key = k;
        }
        if (key != kNone) {
            int order[4] = { 0, 1, 2, 3 };
            for (int i = 1; i < n; i++)
                for (int j = i; j > 0 && component[order[j]] < component[order[j - 1]]; j--) {
                    int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
                }
            for (int k = 0; k < 2; k++) {
                uint8_t sw[4];
                memcpy(sw, src[k].swizzle, 4);
                for (int i = 0; i < n; i++) src[k].swizzle[i] = sw[order[i]];
            }
        }
    }

    // Literal operands share one def'd register where they fit in one.
    int slots[4], slotCount = 0;
    float values[4][4];
    uint8_t masks[4];
    bool single[4];
    for (int k = 0; k < srcCount; k++) {
        if (src[k].place.file != kNone) continue;
        slots[slotCount] = k;
        masks[slotCount] = mask[k];
        single[slotCount] = true;
        for (int l = 0; l < 4; l++) {
            if (!(mask[k] & (1 << l))) continue;
            values[slotCount][l] = LiteralValue(want[k][l]);
            if (FloatBits(values[slotCount][l]) != FloatBits(values[slotCount][LowestLane(mask[k])])) single[slotCount] = false;
        }
        slotCount++;
    }
    if (slotCount > 0) {
        int defReg = kNone;
        uint8_t comps[4][4];
        bool together = PlaceLiterals(slotCount, values, masks, single, &defReg, comps);
        for (int s = 0; s < slotCount; s++) {
            if (!together && !PlaceLiterals(1, &values[s], &masks[s], &single[s], &defReg, &comps[s]))
                return Fail("out of constant registers");
            Source &t = src[slots[s]];
            t.place.file = REG_CONST; t.place.reg = defReg; t.place.address = kNone;
            t.negate = false;
            int last = comps[s][LowestLane(masks[s])];
            for (int l = 0; l < 4; l++) {
                if (masks[s] & (1 << l)) last = comps[s][l];
                t.swizzle[l] = (uint8_t)((masks[s] & (1 << l)) ? comps[s][l] : (m_pixel && !single[s]) ? l : last);
            }
        }
    }

    // Read ports: per instruction a vertex shader reads one constant and one input register, a pixel shader
    // two constants, one colour and one texture-coordinate register.
    Place seen[3][4];
    int seenCount[3] = { 0, 0, 0 };
    for (int k = 0; k < srcCount; k++) {
        const Place &p = src[k].place;
        int kind = p.file == REG_CONST ? 0 : p.file == REG_INPUT ? 1 : (m_pixel && p.file == REG_TEXTURE) ? 2 : kNone;
        if (kind == kNone) continue;
        int limit = 1;
        bool known = false;
        for (int i = 0; i < seenCount[kind]; i++)
            if (seen[kind][i].reg == p.reg && seen[kind][i].address == p.address) known = true;
        if (known) continue;
        if (seenCount[kind] < limit) { seen[kind][seenCount[kind]++] = p; continue; }
        if (!MoveToTemp(&src[k], mask[k])) return false;
    }

    // The operands are read: values whose last use this was give up their components.
    for (int i = 0; i < count; i++)
        for (int k = 0; k < m_work[members[i]].argCount; k++) Use(m_work[members[i]].args[k], -1);
    for (int i = 0; i < count; i++)
        for (int k = 0; k < m_work[members[i]].argCount; k++) {
            int a = m_work[members[i]].args[k].node;
            Release(a);
            if (m_nodes[a].op == kRelative) Release(m_nodes[a].args[0].node);
        }
    for (int r = 0; r < kMaxTemps; r++)
        for (int c = 0; c < 4; c++)
            if (m_temp[r][c] == kGathered) m_temp[r][c] = kNone;

    // lrp and nrm are macros that write part of their result before reading the rest of their operands, so
    // the D3D9 runtime refuses one whose destination is a register it reads.
    m_avoid = 0;
    if (op == kLrp || op == kNrm)
        for (int k = 0; k < srcCount; k++)
            if (src[k].place.file == REG_TEMP) m_avoid |= 1u << src[k].place.reg;

    m_wantDepth = 0;
    for (int i = 0; i < count; i++) if (m_depth[members[i]] > m_wantDepth) m_wantDepth = m_depth[members[i]];

    // The destination: straight into the output register when that is all the result is for (vertex shaders),
    // else a temporary. destComp[i] is where members[i] goes.
    int destComp[4], destReg = kNone;
    uint32_t dest = 0, writeMask = 0;
    bool direct = !m_pixel && !IsChannelOp(op);
    for (int i = 0; i < count && direct; i++) {
        int node = members[i];
        if (m_outputUses[node].size() != 1 || m_uses[node] != 1 || m_outputUses[node][0].output != m_outputUses[members[0]][0].output)
            direct = false;
        else
            destComp[i] = m_outputUses[node][0].component;
    }
    if (direct && count > 1 && (m_outputUses[members[0]][0].output == ShaderBuilder::kOutFog || m_outputUses[members[0]][0].output == ShaderBuilder::kOutPointSize))
        direct = false;
    if (direct) {
        for (int i = 0; i < count; i++) writeMask |= 1u << destComp[i];
        dest = OutputDest(m_outputUses[members[0]][0].output, writeMask);
        for (int i = 0; i < count; i++) { m_uses[members[i]]--; m_outputUses[members[i]].clear(); }
    } else {
        if (op == kTex) {
            int all[4] = { 0, 1, 2, 3 };
            destReg = FindTemp(all, 4, true, destComp, true);
            for (int i = 0; i < count; i++) destComp[i] = m_nodes[members[i]].component;
        } else if (IsChannelOp(op)) {
            int channels[4];
            for (int i = 0; i < count; i++) channels[i] = m_nodes[members[i]].component;
            destReg = FindTemp(channels, count, true, destComp, true);
        } else {
            // A lane-wise result goes in its lanes; a single result (a scalar instruction writes it to any
            // component) where its source was. Values of one source register go where the compiler put them:
            // that register's temporary, in their own lanes, else in the lowest free ones.
            int pref = src[0].swizzle[0];
            int *wanted = laneOp ? lane : &pref;
            int variable = m_nodes[members[0]].variable;
            for (int i = 1; i < count; i++) if (m_nodes[members[i]].variable != variable) variable = kNone;
            if (!m_pixel && variable >= 0 && variable < 64 && m_variableTemp[variable] != kNone &&
                !(m_avoid & (1u << m_variableTemp[variable]))) {
                int r = m_variableTemp[variable], n = 0;
                bool own = true;
                for (int i = 0; i < count; i++) {
                    int l = m_nodes[members[i]].lane;
                    if (l < 0 || m_temp[r][l] != kNone) own = false;
                    for (int j = 0; j < i; j++) if (m_nodes[members[j]].lane == l) own = false;
                }
                if (own) {
                    for (int i = 0; i < count; i++) destComp[i] = m_nodes[members[i]].lane;
                    destReg = r;
                } else {
                    for (int c = 0; c < 4 && n < count; c++) if (m_temp[r][c] == kNone) destComp[n++] = c;
                    if (n == count) destReg = r;
                }
            }
            if (destReg == kNone)
                destReg = FindTemp(wanted, laneOp ? count : 1, m_pixel && laneOp && count > 1, destComp, true);
            if (destReg != kNone && !m_pixel && variable >= 0 && variable < 64 && m_variableTemp[variable] == kNone)
                m_variableTemp[variable] = destReg;
        }
        m_avoid = 0;
        m_wantDepth = 1 << 20;
        if (destReg == kNone) return Fail("out of temporary registers");
        for (int i = 0; i < count; i++) writeMask |= 1u << destComp[i];
        if (op == kTex) writeMask = 0xF;
        dest = RegisterBits(REG_TEMP, destReg) | (writeMask << 16);
    }

    // Lane-wise operands follow their members to the components chosen.
    if (laneOp) {
        for (int k = 0; k < srcCount; k++) {
            uint8_t sw[4];
            bool replicate = true;
            for (int i = 0; i < count; i++) if (src[k].swizzle[lane[i]] != src[k].swizzle[lane[0]]) replicate = false;
            int last = src[k].swizzle[lane[0]];
            for (int c = 0; c < 4; c++) sw[c] = (uint8_t)last;
            for (int i = 0; i < count; i++) sw[destComp[i]] = src[k].swizzle[lane[i]];
            for (int c = 0; c < 4; c++) {
                if (writeMask & (1u << c)) last = sw[c];
                else sw[c] = (uint8_t)(m_pixel && !replicate ? c : last);
            }
            memcpy(src[k].swizzle, sw, 4);
        }
    }

    if (op == kTex) {
        Source &s = src[srcCount++];
        s.place.file = REG_SAMPLER; s.place.reg = m_nodes[members[0]].reg; s.place.address = kNone;
        s.negate = false;
        for (int l = 0; l < 4; l++) s.swizzle[l] = (uint8_t)l;
        m_samplerType[m_nodes[members[0]].reg & 15] = m_nodes[members[0]].bits ? kSamplerCube : kSampler2D;
    }
    EmitInstruction(Opcode(op), w.sat || op == kSat, dest, src, srcCount);
    m_pinned = 0;

    for (int i = 0; i < count; i++) {
        int node = members[i];
        m_done[node] = true;
        ModelCompilerComponent(node, members, count);
        if (!direct) {
            m_temp[destReg][destComp[i]] = node;
            WriteOutputs(node);
        }
        Release(node);
    }
    return !m_failed;
}

bool Lowering::Run(const std::vector<OutputValue> &outputs, const std::vector<InputDecl> &inputs,
                   std::vector<uint32_t> *tokens, char *error, size_t errorSize) {
    size_t n = m_nodes.size();
    m_failed = false;
    m_error[0] = 0;
    m_work.resize(n);
    m_uses.assign(n, 0);
    m_live.assign(n, false);
    m_done.assign(n, false);
    m_outputUses.assign(n, std::vector<OutputUse>());
    for (int r = 0; r < kMaxTemps; r++) for (int c = 0; c < 4; c++) m_temp[r][c] = kNone;
    m_pinned = 0;
    m_avoid = 0;
    m_wantDepth = 1 << 20;
    for (int r = 0; r < kMaxTemps; r++) m_registerDepth[r] = 0;
    for (int v = 0; v < 64; v++) m_variableTemp[v] = kNone;
    for (int v = 0; v < 64; v++) for (int c = 0; c < 4; c++) m_compilerLanes[v][c] = kNone;
    m_compilerComponent.assign(n, kNone);
    memset(m_constRead, 0, sizeof(m_constRead));
    m_relative = false;
    m_address = kNone;
    memset(m_inputMask, 0, sizeof(m_inputMask));
    memset(m_texMask, 0, sizeof(m_texMask));
    memset(m_samplerType, 0, sizeof(m_samplerType));
    m_arithmetic = m_texture = 0;
    for (size_t i = 0; i < n; i++) {
        m_work[i].op = m_nodes[i].op;
        m_work[i].sat = false;
        m_work[i].argCount = m_nodes[i].op == kNrm ? 3 : m_nodes[i].argCount;
        memcpy(m_work[i].args, m_nodes[i].args, sizeof(m_work[i].args));
    }

    // Live nodes, from the outputs back.
    for (size_t i = 0; i < outputs.size(); i++) m_live[outputs[i].value.node] = true;
    for (size_t i = n; i-- > 0;)
        if (m_live[i])
            for (int k = 0; k < m_nodes[i].argCount; k++) m_live[m_nodes[i].args[k].node] = true;

    // A product feeding a sum fuses into a mad as the compiler fuses it, per vector instruction: the live lanes
    // of one vector add whose operands come from the same registers are one instruction, and it is a mad when
    // one operand of every lane is a product of one vector multiply - the left operand's when both are.
    {
        // Where an add's operand comes from: a leaf's register, a literal, or a temporary.
        auto origin = [this](ShaderValue v) -> uint64_t {
            const Node &x = m_nodes[v.node];
            if (IsLiteralOp(x.op) || !IsLeaf(x.op)) return 2;   // literals are moved into the temporary's lanes
            uint64_t address = x.op == kRelative ? (uint64_t)x.args[0].node + 1 : 0;
            return ((uint64_t)x.op << 56) | ((uint64_t)(uint16_t)x.reg << 40) | address << 8 | 3;
        };
        std::vector<int> adds;
        for (size_t i = 0; i < n; i++) if (m_live[i] && m_nodes[i].op == kAdd && m_nodes[i].bits == 0) adds.push_back((int)i);
        // A product the compiler packs into one instruction with another operation's product - the same first
        // operand, the second from the same constant register - is no longer one multiply it can fuse; with
        // both operands of a sum products, it then fuses the other.
        auto packedWithAnother = [&](int product) {
            const Node &p = m_nodes[product];
            for (size_t i = 0; i < n; i++) {
                const Node &q = m_nodes[i];
                if (!m_live[i] || (int)i == product || q.op != kMul || q.operation == p.operation) continue;
                if (q.args[0].node != p.args[0].node || q.args[0].negate != p.args[0].negate) continue;
                const Node &b = m_nodes[p.args[1].node], &c = m_nodes[q.args[1].node];
                if (b.op == kConstant && c.op == kConstant && b.reg == c.reg) return true;
            }
            return false;
        };
        // The vector operation that reads a value, if one operation reads all its uses (0 if several do, or an
        // output does): an add whose lanes all feed one vector operation stays one instruction.
        std::vector<uint32_t> reader(n, 0xFFFFFFFFu);
        for (size_t i = 0; i < outputs.size(); i++) reader[outputs[i].value.node] = 0;
        for (size_t i = 0; i < n; i++) {
            if (!m_live[i]) continue;
            for (int k = 0; k < m_nodes[i].argCount; k++) {
                uint32_t &r = reader[m_nodes[i].args[k].node];
                r = (r == 0xFFFFFFFFu || r == m_nodes[i].operation) ? m_nodes[i].operation : 0;
            }
        }
        std::vector<bool> grouped(n, false);
        for (size_t a = 0; a < adds.size(); a++) {
            int first = adds[a];
            if (grouped[first]) continue;
            bool together = true;   // every lane read by the same one vector operation
            for (size_t b = a; b < adds.size(); b++)
                if (m_nodes[adds[b]].operation == m_nodes[first].operation)
                    together = together && reader[adds[b]] != 0 && reader[adds[b]] != 0xFFFFFFFFu && reader[adds[b]] == reader[first];
            std::vector<int> lanes;
            for (size_t b = a; b < adds.size(); b++) {
                int x = adds[b];
                if (grouped[x] || m_nodes[x].operation != m_nodes[first].operation) continue;
                if (!together && (origin(m_nodes[x].args[0]) != origin(m_nodes[first].args[0]) ||
                                  origin(m_nodes[x].args[1]) != origin(m_nodes[first].args[1]))) continue;
                lanes.push_back(x);
                grouped[x] = true;
            }
            for (int s = 0; s < 2; s++) {
                bool fuse = true;
                uint32_t multiply = 0;
                for (size_t l = 0; l < lanes.size() && fuse; l++) {
                    const Node &product = m_nodes[m_nodes[lanes[l]].args[s].node];
                    if (product.op != kMul) fuse = false;
                    else if (l == 0) multiply = product.operation;
                    else if (product.operation != multiply) fuse = false;
                    if (fuse && s == 0 && packedWithAnother(m_nodes[lanes[l]].args[s].node)) fuse = false;
                }
                if (!fuse) continue;
                for (size_t l = 0; l < lanes.size(); l++) {
                    const Node &sum = m_nodes[lanes[l]];
                    ShaderValue p = sum.args[s];
                    Work &w = m_work[lanes[l]];
                    w.op = kMad;
                    w.argCount = 3;
                    w.args[0] = m_nodes[p.node].args[0];
                    if (p.negate) w.args[0] = ShaderBuilder::Negate(w.args[0]);
                    w.args[1] = m_nodes[p.node].args[1];
                    w.args[2] = sum.args[1 - s];
                }
                break;
            }
        }
    }

    std::vector<int> uses(n, 0);
    for (size_t i = 0; i < n; i++) m_live[i] = false;
    for (size_t i = 0; i < outputs.size(); i++) { m_live[outputs[i].value.node] = true; uses[outputs[i].value.node]++; }
    for (size_t i = n; i-- > 0;)
        if (m_live[i])
            for (int k = 0; k < m_work[i].argCount; k++) { m_live[m_work[i].args[k].node] = true; uses[m_work[i].args[k].node]++; }

    // mad(t, b - a, a) is lrp(t, b, a) when the difference is used nowhere else; in a pixel shader, a
    // saturate of a value used nowhere else is that instruction's _sat.
    for (size_t i = 0; i < n; i++) {
        if (!m_live[i]) continue;
        Work &w = m_work[i];
        if (w.op == kMad && !w.args[1].negate && m_work[w.args[1].node].op == kAdd && uses[w.args[1].node] == 1) {
            const Work &d = m_work[w.args[1].node];
            ShaderValue a = w.args[2];
            for (int s = 0; s < 2; s++) {
                if (d.args[1 - s].node == a.node && d.args[1 - s].negate != a.negate) {
                    ShaderValue t = w.args[0], b = d.args[s];
                    w.op = kLrp;
                    w.args[0] = t; w.args[1] = b; w.args[2] = a;
                    break;
                }
            }
        }
        if (w.op == kSat && m_pixel && !w.args[0].negate) {
            int x = w.args[0].node;
            if (!IsLeaf(m_work[x].op) && !IsChannelOp(m_work[x].op) && m_work[x].op != kSat && uses[x] == 1) {
                w = m_work[x];
                w.sat = true;
            }
        }
    }

    // Live nodes of what is lowered (an absorbed difference or saturated value drops out), and their uses.
    auto countUses = [&]() {
        for (size_t i = 0; i < n; i++) { m_live[i] = false; m_uses[i] = 0; m_outputUses[i].clear(); }
        for (size_t i = 0; i < outputs.size(); i++) m_live[outputs[i].value.node] = true;
        for (size_t i = n; i-- > 0;)
            if (m_live[i])
                for (int k = 0; k < m_work[i].argCount; k++) m_live[m_work[i].args[k].node] = true;
        for (size_t i = 0; i < n; i++) {
            if (!m_live[i]) continue;
            for (int k = 0; k < m_work[i].argCount; k++) Use(m_work[i].args[k], +1);
            if (m_nodes[i].op == kConstant) m_constRead[m_nodes[i].reg & 255] = true;
            if (m_nodes[i].op == kRelative) m_relative = true;
        }
        for (size_t i = 0; i < outputs.size(); i++) {
            Use(outputs[i].value, +1);
            if (!m_pixel && !IsLeaf(m_nodes[outputs[i].value.node].op)) {
                OutputUse u = { outputs[i].output, outputs[i].component };
                m_outputUses[outputs[i].value.node].push_back(u);
            }
        }
    };
    countUses();

    // The compiler makes an nrm only of a vector that sits in .xyz of its temporary. Where its model of the
    // components (ModelCompilerComponent, run over the program here) has the vector elsewhere, the nrm is
    // the dp3, rsq and mul it stands for.
    if (!m_pixel) {
        std::vector<int> uses = m_uses;
        for (size_t i = 0; i < n; i++) {
            if (!m_live[i] || IsLeaf(m_work[i].op)) continue;
            for (int k = 0; k < m_work[i].argCount; k++) Use(m_work[i].args[k], -1);
            int self[1] = { (int)i };
            ModelCompilerComponent((int)i, self, 1);
            m_uses[i] -= (int)m_outputUses[i].size();
        }
        bool changed = false;
        for (size_t i = 0; i < n; i++) {
            if (!m_live[i] || m_work[i].op != kNrm) continue;
            const Node &x = m_nodes[i];
            bool inOrder = true;
            for (int l = 0; l < 3; l++) {
                int c = m_compilerComponent[x.args[l].node];
                if (c >= 0 && c != l && m_nodes[x.args[l].node].variable == m_nodes[x.args[0].node].variable) inOrder = false;
            }
            if (inOrder) continue;
            m_work[i].op = kMul;
            m_work[i].argCount = 2;
            m_work[i].args[0] = x.args[x.component];
            m_work[i].args[1] = x.args[3];
            changed = true;
        }
        m_uses = uses;
        for (int v = 0; v < 64; v++) for (int c = 0; c < 4; c++) m_compilerLanes[v][c] = kNone;
        m_compilerComponent.assign(n, kNone);
        if (changed) countUses();
    }

    m_depth.assign(n, 0);
    for (size_t i = 0; i < n; i++) {
        for (int k = 0; k < m_work[i].argCount; k++)
            if (m_depth[m_work[i].args[k].node] > m_depth[i]) m_depth[i] = m_depth[m_work[i].args[k].node];
        if (m_work[i].op == kTex) m_depth[i]++;
    }

    // The instructions in program order, each taking along later nodes that can share it.
    std::vector<int> order;
    for (size_t i = 0; i < n; i++) if (m_live[i] && !IsLeaf(m_work[i].op)) order.push_back((int)i);
    // A pixel shader reads its textures in phases, as the compiler wrote them: the coordinates and texlds of
    // one depth of dependent reads, then the arithmetic on their results. The runtime counts a phase for every
    // texld that follows arithmetic, and refuses a fourth.
    if (m_pixel) {
        std::vector<bool> feedsTexture(n, false);
        for (size_t i = n; i-- > 0;) {
            if (!m_live[i] || (m_work[i].op != kTex && !feedsTexture[i])) continue;
            for (int k = 0; k < m_work[i].argCount; k++) {
                int a = m_work[i].args[k].node;
                if (m_depth[a] == m_depth[i] - (m_work[i].op == kTex ? 1 : 0)) feedsTexture[a] = true;
            }
        }
        std::vector<int> phase(n, 0);
        for (size_t k = 0; k < order.size(); k++) {
            int i = order[k];
            phase[i] = m_work[i].op == kTex ? 2 * m_depth[i] - 1 : feedsTexture[i] ? 2 * m_depth[i] : 2 * m_depth[i] + 2;
        }
        std::stable_sort(order.begin(), order.end(), [&phase](int a, int b) { return phase[a] < phase[b]; });
    }
    for (size_t oi = 0; oi < order.size() && !m_failed; oi++) {
        int first = order[oi];
        if (m_done[first]) continue;
        int members[4] = { first, 0, 0, 0 };
        int count = 1;
        if (IsChannelOp(m_work[first].op)) {
            for (size_t j = oi + 1; j < order.size() && count < 4; j++) {
                const Node &a = m_nodes[first], &b = m_nodes[order[j]];
                bool same = !m_done[order[j]] && b.op == a.op && b.reg == a.reg && b.bits == a.bits && b.argCount == a.argCount;
                for (int k = 0; same && k < a.argCount; k++)
                    same = a.args[k].node == b.args[k].node && a.args[k].negate == b.args[k].negate;
                if (same) members[count++] = order[j];
            }
        } else if (IsLaneOp(m_work[first].op)) {
            for (size_t j = oi + 1; j < order.size() && j < oi + 24 && count < 4; j++) {
                int c = order[j];
                if (m_done[c]) continue;
                bool ready = true;
                for (int k = 0; k < m_work[c].argCount && ready; k++) {
                    int a = m_work[c].args[k].node;
                    if (m_nodes[a].op == kRelative) a = m_nodes[a].args[0].node;
                    if (!IsLeaf(m_work[a].op) && !m_done[a]) ready = false;
                }
                if (ready && CanJoin(members, count, c)) members[count++] = c;
            }
        }
        EmitGroup(members, count);
        EndInstruction();
    }

    // What is left to write: outputs that are leaves (vertex shaders), and a pixel shader's colour, which goes
    // to oC0 whole from one register.
    for (size_t i = 0; i < outputs.size() && !m_failed && !m_pixel; i++) {
        ShaderValue v = outputs[i].value;
        if (!IsLeaf(m_nodes[v.node].op)) continue;
        Source s;
        if (IsLiteralOp(m_nodes[v.node].op)) {
            if (!LiteralSource(LiteralValue(v), &s)) break;
        } else {
            int comp;
            Locate(v, &s.place, &comp);
            s.negate = v.negate;
            memset(s.swizzle, comp, 4);
        }
        EmitInstruction(OP_MOV, false, OutputDest(outputs[i].output, 1u << outputs[i].component), &s, 1);
    }
    if (!m_failed && m_pixel) {
        ShaderValue colour[4];
        uint8_t have = 0;
        memset(colour, 0, sizeof(colour));
        for (size_t i = 0; i < outputs.size(); i++)
            if (outputs[i].output == ShaderBuilder::kOutColour) { colour[outputs[i].component] = outputs[i].value; have |= (uint8_t)(1 << outputs[i].component); }
        Source s;
        if (have != 0xF) {
            Fail("a pixel shader must output all four colour components");
        } else if (Resolve(colour, 0xF, false, &s)) {
            if (s.place.file == kNone) {
                float values[1][4];
                bool single = true;
                for (int l = 0; l < 4; l++) {
                    values[0][l] = LiteralValue(colour[l]);
                    if (FloatBits(values[0][l]) != FloatBits(values[0][0])) single = false;
                }
                uint8_t mask = 0xF, comps[1][4];
                int reg = 0;
                if (!PlaceLiterals(1, values, &mask, &single, &reg, comps)) Fail("out of constant registers");
                s.place.file = REG_CONST; s.place.reg = reg; s.place.address = kNone; s.negate = false;
                for (int l = 0; l < 4; l++) s.swizzle[l] = comps[0][single ? 0 : l];
            }
            bool whole = s.place.file == REG_TEMP && !s.negate && s.swizzle[0] == 0 && s.swizzle[1] == 1 && s.swizzle[2] == 2 && s.swizzle[3] == 3;
            if (!whole && !m_failed) {
                bool negate = s.negate;
                if (MoveToTemp(&s, 0xF) && negate) {   // MoveToTemp leaves the sign to the reader; apply it in a second move
                    Source neg = s;
                    neg.negate = true;
                    EmitInstruction(OP_MOV, false, RegisterBits(REG_TEMP, s.place.reg) | 0xF0000u, &neg, 1);
                }
                s.negate = false;
            }
            if (!m_failed) EmitInstruction(OP_MOV, false, RegisterBits(REG_COLOROUT, 0) | 0xF0000u, &s, 1);
        }
    }
    if (!m_failed && !m_pixel && m_arithmetic > 256) Fail("%d instructions, over vs_2_0's 256", m_arithmetic);
    if (!m_failed && m_pixel && m_arithmetic > 64) Fail("%d arithmetic instructions, over ps_2_0's 64", m_arithmetic);
    if (!m_failed && m_pixel && m_texture > 32) Fail("%d texture instructions, over ps_2_0's 32", m_texture);
    if (m_failed) {
        snprintf(error, errorSize, "%s", m_error);
        return false;
    }

    // Version, defs, declarations, instructions, end.
    tokens->clear();
    tokens->push_back(m_pixel ? 0xFFFF0200u : 0xFFFE0200u);
    for (size_t i = 0; i < m_defs.size(); i++) {
        tokens->push_back((uint32_t)OP_DEF | (5u << 24));
        tokens->push_back(RegisterBits(REG_CONST, m_defs[i].reg) | 0xF0000u);
        for (int c = 0; c < 4; c++) tokens->push_back(FloatBits(m_defs[i].value[c]));
    }
    if (!m_pixel) {
        for (size_t i = 0; i < inputs.size(); i++) {
            bool read = false;
            for (size_t k = 0; k < n && !read; k++) read = m_live[k] && m_nodes[k].op == kInput && m_nodes[k].reg == inputs[i].reg;
            if (!read) continue;
            tokens->push_back((uint32_t)OP_DCL | (2u << 24));
            tokens->push_back(kParam | (uint32_t)inputs[i].usage | ((uint32_t)inputs[i].usageIndex << 16));
            tokens->push_back(RegisterBits(REG_INPUT, inputs[i].reg) | 0xF0000u);
        }
    } else {
        for (int r = 0; r < 2; r++) {
            if (!m_inputMask[r]) continue;
            tokens->push_back((uint32_t)OP_DCL | (2u << 24));
            tokens->push_back(kParam);
            tokens->push_back(RegisterBits(REG_INPUT, r) | ((uint32_t)PrefixMask(m_inputMask[r]) << 16));
        }
        for (int r = 0; r < 8; r++) {
            if (!m_texMask[r]) continue;
            tokens->push_back((uint32_t)OP_DCL | (2u << 24));
            tokens->push_back(kParam);
            tokens->push_back(RegisterBits(REG_TEXTURE, r) | ((uint32_t)PrefixMask(m_texMask[r]) << 16));
        }
        for (int s = 0; s < 16; s++) {
            if (!m_samplerType[s]) continue;
            tokens->push_back((uint32_t)OP_DCL | (2u << 24));
            tokens->push_back(kParam | m_samplerType[s]);
            tokens->push_back(RegisterBits(REG_SAMPLER, s) | 0xF0000u);
        }
    }
    tokens->insert(tokens->end(), m_code.begin(), m_code.end());
    tokens->push_back(0x0000FFFFu);
    return true;
}

} // namespace

bool ShaderBuilder::Assemble(std::vector<uint32_t> *tokens, char *error, size_t errorSize) {
    std::vector<Lowering::OutputValue> outputs;
    for (size_t i = 0; i < m_outputs.size(); i++) {
        Lowering::OutputValue o = { m_outputs[i].output, m_outputs[i].component, m_outputs[i].value };
        outputs.push_back(o);
    }
    std::vector<Lowering::InputDecl> inputs;
    for (size_t i = 0; i < m_inputs.size(); i++) {
        Lowering::InputDecl d = { m_inputs[i].reg, m_inputs[i].usage, m_inputs[i].usageIndex };
        inputs.push_back(d);
    }
    Lowering lowering(m_nodes, m_profile == kPixelShader);
    return lowering.Run(outputs, inputs, tokens, error, errorSize);
}
