#include "shaderCheck.h"

#include "shaderHlsl.h"
#include "../nv2aPixelShader.h"
#include "../nv2aVertexShader.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// See the header.

namespace {

const int kTrials = 384;
bool g_showCode = false;   // both disassemblies in every report, not only where they differ

struct Report {
    char *buf; size_t size, len;
    void printf(const char *fmt, ...) {
        if (size == 0) return;
        va_list ap; va_start(ap, fmt);
        int n = vsnprintf(buf + len, size - len, fmt, ap);
        va_end(ap);
        if (n > 0) len = (len + (size_t)n < size) ? len + (size_t)n : size - 1;
    }
};

uint32_t Bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
float Float(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

struct Random {
    uint32_t s;
    uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float Uniform(float lo, float hi) { return lo + (hi - lo) * (float)(Next() >> 8) / 16777216.0f; }
    // Mostly ordinary values, often small integers and halves, so that equality selects and clamps are taken.
    float Value() {
        static const float special[] = { 0.0f, 1.0f, 2.0f, 3.0f, -1.0f, 0.5f, -0.5f, 0.25f, 4.0f };
        uint32_t k = Next() % 10;
        if (k < 3) return special[Next() % (sizeof(special) / sizeof(special[0]))];
        if (k < 4) return (float)(Next() % 256);
        return Uniform(-3.0f, 3.0f);
    }
};

// Output slots: vertex shaders oPos, oFog, oPts, oD0, oD1, oT0-oT7; pixel shaders oC0.
enum { SLOT_POS = 0, SLOT_FOG = 1, SLOT_PTS = 2, SLOT_D0 = 3, SLOT_T0 = 5, SLOT_COUNT = 13 };

struct Inputs {
    float semantic[32][4];   // vertex inputs by usage * 16 + index (POSITION 0, TEXCOORD 5*16+n); pixel: v0, v1
    float tex[8][4];         // pixel shader texture coordinates
    float constant[256][4];
};

struct Outputs {
    float value[SLOT_COUNT][4];
    uint8_t written[SLOT_COUNT];
};

class Machine {
public:
    bool Run(const uint32_t *code, const Inputs &in, Outputs *out, std::string *error);

private:
    bool Read(const uint32_t *&p, float v[4]);
    float r[32][4];
    float defs[256][4];
    bool defined[256];
    int semanticOf[16];
    int samplerType[16];
    int a0[4];
    bool pixel;
    const Inputs *in;
    std::string *error;
};

int RegisterType(uint32_t t) { return (int)(((t >> 28) & 7) | ((t >> 8) & 0x18)); }

bool Machine::Read(const uint32_t *&p, float v[4]) {
    uint32_t t = *p++;
    int type = RegisterType(t), num = (int)(t & 0x7FF);
    float base[4] = { 0, 0, 0, 0 };
    switch (type) {
        case 0: memcpy(base, r[num & 31], 16); break;
        case 1:
            if (pixel) memcpy(base, in->semantic[num & 31], 16);
            else if (semanticOf[num & 15] >= 0) memcpy(base, in->semantic[semanticOf[num & 15]], 16);
            break;
        case 2: {
            int index = num;
            if (t & (1u << 13)) {
                uint32_t a = pixel ? 0 : *p++;
                index += a0[(a >> 16) & 3];
                if (index < 0 || index >= 192) break;   // outside the game's constants: zero for both programs
            }
            if (index < 256) memcpy(base, defined[index] ? defs[index] : in->constant[index], 16);
            break;
        }
        case 3:
            if (pixel) memcpy(base, in->tex[num & 7], 16);
            else for (int c = 0; c < 4; c++) base[c] = (float)a0[c];
            break;
        default:
            *error = "unexpected source register type " + std::to_string(type);
            return false;
    }
    int swizzle = (int)((t >> 16) & 0xFF), modifier = (int)((t >> 24) & 0xF);
    for (int c = 0; c < 4; c++) {
        float x = base[(swizzle >> (2 * c)) & 3];
        switch (modifier) {
            case 0: break;
            case 1: x = -x; break;
            case 11: x = fabsf(x); break;
            case 12: x = -fabsf(x); break;
            default: *error = "source modifier " + std::to_string(modifier); return false;
        }
        v[c] = x;
    }
    return true;
}

// The texture a sampler reads: the bits of the coordinates it uses, hashed to an 8-bit colour.
void Sample(int sampler, int type, const float coord[4], float out[4]) {
    uint32_t h = 2166136261u ^ (uint32_t)sampler;
    int used = type == 3 ? 3 : 2;
    for (int c = 0; c < used; c++) { h = (h ^ Bits(coord[c])) * 16777619u; h ^= h >> 15; }
    for (int c = 0; c < 4; c++) { h = h * 2654435761u + 0x9e3779b9u; h ^= h >> 13; out[c] = (float)((h >> 8) & 0xFF) / 255.0f; }
}

float Dot(const float *a, const float *b, int n) {
    float s = a[0] * b[0];
    for (int i = 1; i < n; i++) {
        float p = a[i] * b[i];
        s = s + p;
    }
    return s;
}

bool Machine::Run(const uint32_t *code, const Inputs &inputs, Outputs *out, std::string *err) {
    in = &inputs;
    error = err;
    memset(r, 0, sizeof(r));
    memset(defined, 0, sizeof(defined));
    memset(a0, 0, sizeof(a0));
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < 16; i++) { semanticOf[i] = -1; samplerType[i] = 2; }
    pixel = (code[0] & 0xFFFF0000u) == 0xFFFF0000u;
    const uint32_t *p = code + 1;
    for (int steps = 0; steps < 4096; steps++) {
        uint32_t tok = *p;
        int op = (int)(tok & 0xFFFF);
        if (op == 0xFFFF) return true;
        if (op == 0xFFFE) { p += 1 + ((tok >> 16) & 0x7FFF); continue; }
        int length = (int)((tok >> 24) & 0xF);
        const uint32_t *next = p + 1 + length;
        p++;
        if (op == 81) {   // def
            int reg = (int)(p[0] & 0x7FF);
            for (int c = 0; c < 4; c++) defs[reg][c] = Float(p[1 + c]);
            defined[reg] = true;
            p = next;
            continue;
        }
        if (op == 31) {   // dcl
            uint32_t usage = p[0], reg = p[1];
            int type = RegisterType(reg), num = (int)(reg & 0x7FF);
            if (!pixel && type == 1) semanticOf[num & 15] = (int)((usage & 0x1F) * 16 + ((usage >> 16) & 0xF)) & 31;
            if (pixel && type == 10) samplerType[num & 15] = (int)((usage >> 27) & 0xF);
            p = next;
            continue;
        }
        if (op == 0) { p = next; continue; }
        uint32_t dest = *p++;
        float s[3][4];
        int srcCount = 0;
        while (p < next && srcCount < 3) {
            if (op == 66 && srcCount == 1) { s[1][0] = (float)(*p & 0x7FF); p++; srcCount++; break; }   // the sampler
            if (!Read(p, s[srcCount])) return false;
            srcCount++;
        }
        float d[4] = { 0, 0, 0, 0 };
        switch (op) {
            case 1: memcpy(d, s[0], 16); break;
            case 2: for (int c = 0; c < 4; c++) d[c] = s[0][c] + s[1][c]; break;
            case 3: for (int c = 0; c < 4; c++) d[c] = s[0][c] - s[1][c]; break;
            case 4: for (int c = 0; c < 4; c++) d[c] = fmaf(s[0][c], s[1][c], s[2][c]); break;
            case 5: for (int c = 0; c < 4; c++) d[c] = s[0][c] * s[1][c]; break;
            case 6: for (int c = 0; c < 4; c++) d[c] = 1.0f / s[0][0]; break;
            case 7: for (int c = 0; c < 4; c++) d[c] = 1.0f / sqrtf(fabsf(s[0][0])); break;
            case 8: for (int c = 0; c < 4; c++) d[c] = Dot(s[0], s[1], 3); break;
            case 9: for (int c = 0; c < 4; c++) d[c] = Dot(s[0], s[1], 4); break;
            case 10: for (int c = 0; c < 4; c++) d[c] = s[0][c] < s[1][c] ? s[0][c] : s[1][c]; break;
            case 11: for (int c = 0; c < 4; c++) d[c] = s[0][c] >= s[1][c] ? s[0][c] : s[1][c]; break;
            case 12: for (int c = 0; c < 4; c++) d[c] = s[0][c] < s[1][c] ? 1.0f : 0.0f; break;
            case 13: for (int c = 0; c < 4; c++) d[c] = s[0][c] >= s[1][c] ? 1.0f : 0.0f; break;
            case 14: for (int c = 0; c < 4; c++) d[c] = exp2f(s[0][0]); break;
            case 15: for (int c = 0; c < 4; c++) d[c] = log2f(fabsf(s[0][0])); break;
            case 16: {   // lit
                float power = s[0][3] < -128.0f ? -128.0f : s[0][3] > 128.0f ? 128.0f : s[0][3];
                d[0] = 1.0f; d[1] = 0.0f; d[2] = 0.0f; d[3] = 1.0f;
                if (s[0][0] > 0.0f) { d[1] = s[0][0]; if (s[0][1] > 0.0f) d[2] = powf(s[0][1], power); }
                break;
            }
            case 17: d[0] = 1.0f; d[1] = s[0][1] * s[1][1]; d[2] = s[0][2]; d[3] = s[1][3]; break;   // dst
            case 18: for (int c = 0; c < 4; c++) { float diff = s[1][c] - s[2][c]; d[c] = fmaf(s[0][c], diff, s[2][c]); } break;
            case 19: for (int c = 0; c < 4; c++) d[c] = s[0][c] - floorf(s[0][c]); break;
            case 32: for (int c = 0; c < 4; c++) d[c] = powf(fabsf(s[0][0]), s[1][0]); break;
            case 35: for (int c = 0; c < 4; c++) d[c] = fabsf(s[0][c]); break;
            case 36: {   // nrm: dp3, rsq, mul
                float len = Dot(s[0], s[0], 3), k = 1.0f / sqrtf(fabsf(len));
                for (int c = 0; c < 4; c++) d[c] = s[0][c] * k;
                break;
            }
            case 46: for (int c = 0; c < 4; c++) a0[c] = (int)floorf(s[0][c] + 0.5f); p = next; continue;   // mova
            case 66: Sample((int)s[1][0], samplerType[(int)s[1][0] & 15], s[0], d); break;
            case 88: for (int c = 0; c < 4; c++) d[c] = s[0][c] >= 0.0f ? s[1][c] : s[2][c]; break;
            case 90: { float a = s[0][0] * s[1][0], b = s[0][1] * s[1][1]; float t = a + b; for (int c = 0; c < 4; c++) d[c] = t + s[2][2]; break; }
            default:
                *error = "opcode " + std::to_string(op);
                return false;
        }
        if (dest & (1u << 20))
            for (int c = 0; c < 4; c++) d[c] = d[c] < 0.0f ? 0.0f : d[c] > 1.0f ? 1.0f : d[c];   // _sat (a NaN stays)
        int type = RegisterType(dest), num = (int)(dest & 0x7FF), mask = (int)((dest >> 16) & 0xF);
        float *target = NULL;
        int slot = -1;
        switch (type) {
            case 0: target = r[num & 31]; break;
            case 4: slot = num == 0 ? SLOT_POS : num == 1 ? SLOT_FOG : SLOT_PTS; break;
            case 5: slot = SLOT_D0 + (num & 1); break;
            case 6: slot = SLOT_T0 + (num & 7); break;
            case 8: slot = 0; break;
            default: *error = "unexpected destination register type " + std::to_string(type); return false;
        }
        if (slot == SLOT_FOG || slot == SLOT_PTS) mask &= 1;   // scalar registers: .x is the value
        for (int c = 0; c < 4; c++) {
            if (!(mask & (1 << c))) continue;
            if (target) target[c] = d[c];
            else { out->value[slot][c] = d[c]; out->written[slot] |= (uint8_t)(1 << c); }
        }
        p = next;
    }
    *error = "no end token";
    return false;
}

const char *SlotName(int slot, bool pixel) {
    static const char *names[SLOT_COUNT] = { "oPos", "oFog", "oPts", "oD0", "oD1", "oT0", "oT1", "oT2", "oT3", "oT4", "oT5", "oT6", "oT7" };
    return pixel ? "oC0" : names[slot];
}

size_t TokenCount(const uint32_t *code) {
    size_t n = 1;
    while (true) {
        uint32_t tok = code[n];
        if ((tok & 0xFFFF) == 0xFFFF) return n + 1;
        if ((tok & 0xFFFF) == 0xFFFE) n += 1 + ((tok >> 16) & 0x7FFF);
        else n += 1 + ((tok >> 24) & 0xF);
    }
}

void Disassemble(Report *r, const char *title, const uint32_t *code) {
    ID3DBlob *text = NULL;
    if (SUCCEEDED(D3DDisassemble(code, TokenCount(code) * 4, 0, NULL, &text))) {
        r->printf("---- %s\n%s", title, (const char *)text->GetBufferPointer());
        text->Release();
    } else {
        r->printf("---- %s: does not disassemble\n", title);
    }
}

// The HLSL compiled as the backend compiled it: the lowest of the profiles that takes it.
bool Compile(const char *hlsl, const char *const *profiles, int count, std::vector<uint32_t> *code, std::string *message) {
    HRESULT hr = E_FAIL;
    ID3DBlob *blob = NULL, *errors = NULL;
    for (int i = 0; i < count && FAILED(hr); i++) {
        if (errors) { errors->Release(); errors = NULL; }
        hr = D3DCompile(hlsl, strlen(hlsl), NULL, NULL, NULL, "main", profiles[i], D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
    }
    if (FAILED(hr)) {
        *message = errors ? (const char *)errors->GetBufferPointer() : "(no message)";
        if (errors) errors->Release();
        return false;
    }
    if (errors) errors->Release();
    const uint32_t *words = (const uint32_t *)blob->GetBufferPointer();
    code->assign(words, words + blob->GetBufferSize() / 4);
    blob->Release();
    return true;
}


// ---------------------------------------------------------------------------------------------------------------
// Where two programs part: each run symbolically, every register component an expression over the inputs, so
// that a differing output can be shown as the first operation at which the two expressions differ. Only
// exact rewrites are applied (signs out of products, operand order of commutative operations, x*1, x+0), so
// what is shown is a real difference in the arithmetic.
// ---------------------------------------------------------------------------------------------------------------

enum SymOp {
    S_INPUT, S_TEX, S_CONST, S_CONSTREL, S_LITERAL, S_A0,
    S_ADD, S_MUL, S_MAD, S_DP3, S_DP4, S_MIN, S_MAX, S_SLT, S_SGE, S_RCP, S_RSQ, S_FRC, S_ABS, S_EXP, S_LOG, S_LIT,
    S_CMP, S_SAMPLE, S_POW, S_DP2ADD,
};
const char *const kSymNames[] = { "v", "t", "c", "c[a0]", "lit", "a0", "add", "mul", "mad", "dp3", "dp4", "min", "max",
                                  "slt", "sge", "rcp", "rsq", "frc", "abs", "exp", "log", "lit", "cmp", "texld", "pow", "dp2add" };

struct SymRef { int id; bool neg; };
struct SymExpr { int op; int a, b; int argCount; SymRef args[8]; };   // a, b: leaf identity or channel

class Symbolic {
public:
    std::vector<SymExpr> exprs;
    SymRef Make(int op, int a, int b, int argCount, const SymRef *args) {
        SymExpr e;
        memset(&e, 0, sizeof(e));
        e.op = op; e.a = a; e.b = b; e.argCount = argCount;
        for (int i = 0; i < argCount; i++) e.args[i] = args[i];
        for (size_t i = 0; i < exprs.size(); i++) {
            const SymExpr &x = exprs[i];
            bool same = x.op == e.op && x.a == e.a && x.b == e.b && x.argCount == e.argCount;
            for (int k = 0; same && k < e.argCount; k++) same = x.args[k].id == e.args[k].id && x.args[k].neg == e.args[k].neg;
            if (same) { SymRef r = { (int)i, false }; return r; }
        }
        exprs.push_back(e);
        SymRef r = { (int)exprs.size() - 1, false };
        return r;
    }
    SymRef Literal(float f) {
        bool neg = f < 0.0f || (f == 0.0f && signbit(f));
        SymRef r = Make(S_LITERAL, (int)Bits(neg ? -f : f), 0, 0, NULL);
        r.neg = neg;
        return r;
    }
    bool IsLiteral(SymRef r, float v) const {
        if (exprs[r.id].op != S_LITERAL) return false;
        float f = Float((uint32_t)exprs[r.id].a);
        return (r.neg ? -f : f) == v;
    }
    static SymRef Neg(SymRef r) { r.neg = !r.neg; return r; }
    static bool Less(SymRef x, SymRef y) { return x.id != y.id ? x.id < y.id : x.neg < y.neg; }
    SymRef Add(SymRef x, SymRef y) {
        if (IsLiteral(x, 0.0f)) return y;
        if (IsLiteral(y, 0.0f)) return x;
        if (x.id == y.id && x.neg == y.neg) return Mul(x, Literal(2.0f));   // exactly x * 2
        if (Less(y, x)) { SymRef t = x; x = y; y = t; }
        SymRef args[2] = { x, y };
        return Make(S_ADD, 0, 0, 2, args);
    }
    SymRef Mul(SymRef x, SymRef y) {
        if (IsLiteral(x, 0.0f) || IsLiteral(y, 0.0f)) return Literal(0.0f);
        if (IsLiteral(x, 1.0f)) return y;
        if (IsLiteral(y, 1.0f)) return x;
        if (IsLiteral(x, -1.0f)) return Neg(y);
        if (IsLiteral(y, -1.0f)) return Neg(x);
        bool neg = x.neg != y.neg;
        x.neg = y.neg = false;
        if (Less(y, x)) { SymRef t = x; x = y; y = t; }
        SymRef args[2] = { x, y };
        SymRef r = Make(S_MUL, 0, 0, 2, args);
        r.neg = neg;
        return r;
    }
    SymRef Mad(SymRef x, SymRef y, SymRef z) {
        if (IsLiteral(x, 0.0f) || IsLiteral(y, 0.0f)) return z;   // exact for finite operands
        if (IsLiteral(x, 1.0f)) return Add(y, z);
        if (IsLiteral(y, 1.0f)) return Add(x, z);
        if (IsLiteral(z, 0.0f)) return Mul(x, y);
        bool neg = x.neg != y.neg;   // -(x*y) + z = -(x*y - z)
        x.neg = y.neg = false;
        if (neg) z = Neg(z);
        if (Less(y, x)) { SymRef t = x; x = y; y = t; }
        SymRef args[3] = { x, y, z };
        SymRef r = Make(S_MAD, 0, 0, 3, args);
        r.neg = neg;
        return r;
    }
    SymRef Op(int op, int argCount, const SymRef *args, int channel = 0) {
        SymRef a[8];
        for (int i = 0; i < argCount; i++) a[i] = args[i];
        if ((op == S_MIN || op == S_MAX) && Less(a[1], a[0])) { SymRef t = a[0]; a[0] = a[1]; a[1] = t; }
        if (op == S_ABS) a[0].neg = false;
        return Make(op, channel, 0, argCount, a);
    }
    void Print(std::string *out, SymRef r, int depth) const {
        const SymExpr &e = exprs[r.id];
        if (r.neg) *out += "-";
        char buf[64];
        switch (e.op) {
            case S_INPUT: snprintf(buf, sizeof(buf), "v%d.%c", e.a, "xyzw"[e.b]); *out += buf; return;
            case S_TEX: snprintf(buf, sizeof(buf), "t%d.%c", e.a, "xyzw"[e.b]); *out += buf; return;
            case S_CONST: snprintf(buf, sizeof(buf), "c%d.%c", e.a, "xyzw"[e.b]); *out += buf; return;
            case S_LITERAL: snprintf(buf, sizeof(buf), "%.9g", Float((uint32_t)e.a)); *out += buf; return;
            default: break;
        }
        *out += kSymNames[e.op];
        if (e.op == S_CONSTREL) { snprintf(buf, sizeof(buf), "%d.%c", e.a, "xyzw"[e.b]); *out += buf; }
        if (e.op == S_LIT || e.op == S_SAMPLE) { snprintf(buf, sizeof(buf), ".%c", "xyzw"[e.a]); *out += buf; }
        if (depth <= 0) { *out += "(..)"; return; }
        *out += "(";
        for (int i = 0; i < e.argCount; i++) {
            if (i) *out += ", ";
            Print(out, e.args[i], depth - 1);
        }
        *out += ")";
    }
};

class SymMachine {
public:
    bool Run(const uint32_t *code, Symbolic *s, SymRef out[SLOT_COUNT][4], uint8_t written[SLOT_COUNT]);
private:
    bool Read(const uint32_t *&p, SymRef v[4]);
    Symbolic *sym;
    SymRef r[32][4], a0[4];
    bool defined[256];
    float defs[256][4];
    int semanticOf[16];
    bool cube[16];
    bool pixel;
};

bool SymMachine::Read(const uint32_t *&p, SymRef v[4]) {
    uint32_t t = *p++;
    int type = RegisterType(t), num = (int)(t & 0x7FF);
    SymRef base[4];
    for (int c = 0; c < 4; c++) {
        switch (type) {
            case 0: base[c] = r[num & 31][c]; break;
            case 1: base[c] = sym->Make(S_INPUT, pixel ? num : semanticOf[num & 15], c, 0, NULL); break;
            case 2:
                if (t & (1u << 13)) {
                    uint32_t a = pixel ? 0 : p[0];
                    SymRef args[1] = { a0[(a >> 16) & 3] };
                    base[c] = sym->Make(S_CONSTREL, num, c, 1, args);
                } else if (defined[num & 255]) {
                    base[c] = sym->Literal(defs[num & 255][c]);
                } else {
                    base[c] = sym->Make(S_CONST, num, c, 0, NULL);
                }
                break;
            case 3:
                base[c] = pixel ? sym->Make(S_TEX, num, c, 0, NULL) : a0[c];
                break;
            default: return false;
        }
    }
    if (type == 2 && (t & (1u << 13)) && !pixel) p++;
    int swizzle = (int)((t >> 16) & 0xFF), modifier = (int)((t >> 24) & 0xF);
    for (int c = 0; c < 4; c++) {
        SymRef x = base[(swizzle >> (2 * c)) & 3];
        if (modifier == 1) x = Symbolic::Neg(x);
        else if (modifier == 11) x = sym->Op(S_ABS, 1, &x);
        else if (modifier == 12) x = Symbolic::Neg(sym->Op(S_ABS, 1, &x));
        v[c] = x;
    }
    return true;
}

bool SymMachine::Run(const uint32_t *code, Symbolic *s, SymRef out[SLOT_COUNT][4], uint8_t written[SLOT_COUNT]) {
    sym = s;
    SymRef zero = s->Literal(0.0f);
    for (int i = 0; i < 32; i++) for (int c = 0; c < 4; c++) r[i][c] = zero;
    for (int c = 0; c < 4; c++) a0[c] = zero;
    memset(defined, 0, sizeof(defined));
    memset(written, 0, SLOT_COUNT);
    for (int i = 0; i < 16; i++) { semanticOf[i] = -1; cube[i] = false; }
    pixel = (code[0] & 0xFFFF0000u) == 0xFFFF0000u;
    const uint32_t *p = code + 1;
    for (int steps = 0; steps < 4096; steps++) {
        uint32_t tok = *p;
        int op = (int)(tok & 0xFFFF);
        if (op == 0xFFFF) return true;
        if (op == 0xFFFE) { p += 1 + ((tok >> 16) & 0x7FFF); continue; }
        const uint32_t *next = p + 1 + ((tok >> 24) & 0xF);
        p++;
        if (op == 81) {
            int reg = (int)(p[0] & 0x7FF);
            for (int c = 0; c < 4; c++) defs[reg][c] = Float(p[1 + c]);
            defined[reg] = true;
            p = next;
            continue;
        }
        if (op == 31) {
            int type = RegisterType(p[1]), num = (int)(p[1] & 0x7FF);
            if (!pixel && type == 1) semanticOf[num & 15] = (int)((p[0] & 0x1F) * 16 + ((p[0] >> 16) & 0xF)) & 31;
            if (pixel && type == 10) cube[num & 15] = ((p[0] >> 27) & 0xF) == 3;
            p = next;
            continue;
        }
        uint32_t dest = *p++;
        SymRef src[3][4];
        int sampler = 0, n = 0;
        while (p < next && n < 3) {
            if (op == 66 && n == 1) { sampler = (int)(*p & 0x7FF); p++; break; }
            if (!Read(p, src[n])) return false;
            n++;
        }
        SymRef d[4];
        for (int c = 0; c < 4; c++) {
            SymRef *x = src[0], *y = src[1], *z = src[2];
            switch (op) {
                case 1: d[c] = x[c]; break;
                case 2: d[c] = s->Add(x[c], y[c]); break;
                case 4: d[c] = s->Mad(x[c], y[c], z[c]); break;
                case 5: d[c] = s->Mul(x[c], y[c]); break;
                case 6: d[c] = s->Op(S_RCP, 1, &x[0]); break;
                case 7: { SymRef a = s->Op(S_ABS, 1, &x[0]); d[c] = s->Op(S_RSQ, 1, &a); break; }
                case 8: { SymRef a[6] = { x[0], x[1], x[2], y[0], y[1], y[2] }; d[c] = s->Op(S_DP3, 6, a); break; }
                case 9: { SymRef a[8] = { x[0], x[1], x[2], x[3], y[0], y[1], y[2], y[3] }; d[c] = s->Op(S_DP4, 8, a); break; }
                case 10: { SymRef a[2] = { x[c], y[c] }; d[c] = s->Op(S_MIN, 2, a); break; }
                case 11: { SymRef a[2] = { x[c], y[c] }; d[c] = s->Op(S_MAX, 2, a); break; }
                case 12: { SymRef a[2] = { x[c], y[c] }; d[c] = s->Op(S_SLT, 2, a); break; }
                case 13: { SymRef a[2] = { x[c], y[c] }; d[c] = s->Op(S_SGE, 2, a); break; }
                case 14: d[c] = s->Op(S_EXP, 1, &x[0]); break;
                case 15: { SymRef a = s->Op(S_ABS, 1, &x[0]); d[c] = s->Op(S_LOG, 1, &a); break; }
                case 16: { SymRef a[3] = { x[0], x[1], x[3] }; d[c] = s->Op(S_LIT, 3, a, c); break; }
                case 18: d[c] = s->Mad(x[c], s->Add(y[c], Symbolic::Neg(z[c])), z[c]); break;   // lrp
                case 19: d[c] = s->Op(S_FRC, 1, &x[c]); break;
                case 35: d[c] = s->Op(S_ABS, 1, &x[c]); break;
                case 36: {   // nrm
                    SymRef a[6] = { x[0], x[1], x[2], x[0], x[1], x[2] };
                    SymRef dp = s->Op(S_DP3, 6, a), ab = s->Op(S_ABS, 1, &dp);
                    d[c] = s->Mul(x[c], s->Op(S_RSQ, 1, &ab));
                    break;
                }
                case 46: d[c] = x[c]; break;
                case 66: { SymRef a[3] = { x[0], x[1], cube[sampler & 15] ? x[2] : zero }; d[c] = s->Op(S_SAMPLE, 3, a, c); break; }
                case 88: { SymRef a[3] = { x[c], y[c], z[c] }; d[c] = s->Op(S_CMP, 3, a); break; }
                default: return false;
            }
        }
        if (op == 46) { for (int c = 0; c < 4; c++) a0[c] = d[c]; p = next; continue; }
        int type = RegisterType(dest), num = (int)(dest & 0x7FF), mask = (int)((dest >> 16) & 0xF);
        if (dest & (1u << 20)) {
            SymRef lo = s->Literal(0.0f), hi = s->Literal(1.0f);
            for (int c = 0; c < 4; c++) { SymRef a[2] = { d[c], lo }; SymRef m = s->Op(S_MAX, 2, a); SymRef b2[2] = { m, hi }; d[c] = s->Op(S_MIN, 2, b2); }
        }
        int slot = -1;
        switch (type) {
            case 0: break;
            case 4: slot = num == 0 ? SLOT_POS : num == 1 ? SLOT_FOG : SLOT_PTS; break;
            case 5: slot = SLOT_D0 + (num & 1); break;
            case 6: slot = SLOT_T0 + (num & 7); break;
            case 8: slot = 0; break;
            default: return false;
        }
        if (slot == SLOT_FOG || slot == SLOT_PTS) mask &= 1;
        for (int c = 0; c < 4; c++) {
            if (!(mask & (1 << c))) continue;
            if (type == 0) r[num & 31][c] = d[c];
            else { out[slot][c] = d[c]; written[slot] |= (uint8_t)(1 << c); }
        }
        p = next;
    }
    return false;
}

// The first operation at which two expressions part, followed down while all but one operand agree.
void Divergence(const Symbolic &s, SymRef a, SymRef b, std::string *text) {
    for (int guard = 0; guard < 64; guard++) {
        const SymExpr &x = s.exprs[a.id], &y = s.exprs[b.id];
        if (a.neg != b.neg || x.op != y.op || x.argCount != y.argCount || x.a != y.a || x.b != y.b || x.argCount == 0) break;
        // Operands match up by position, or for an operation whose operands commute, by value.
        bool commutes = x.op == S_ADD || x.op == S_MUL || x.op == S_MIN || x.op == S_MAX || x.op == S_MAD;
        int n = x.op == S_MAD ? 2 : x.argCount;
        bool usedA[8] = {}, usedB[8] = {};
        for (int i = 0; i < x.argCount; i++)
            for (int j = 0; j < y.argCount; j++) {
                if (usedB[j] || (!commutes && i != j) || (commutes && (i < n) != (j < n)) || (i >= n && i != j)) continue;
                if (x.args[i].id == y.args[j].id && x.args[i].neg == y.args[j].neg) { usedA[i] = usedB[j] = true; break; }
            }
        // Down the first operand that differs, to the smallest difference.
        int differA = -1, differB = -1, count = 0;
        for (int i = 0; i < x.argCount; i++) if (!usedA[i]) { if (differA < 0) differA = i; count++; }
        for (int j = 0; j < y.argCount; j++) if (!usedB[j] && differB < 0) differB = j;
        if (count == 0) break;
        a = x.args[differA];
        b = y.args[differB];
    }
    *text += "      ours:      ";
    s.Print(text, a, 6);
    *text += "\n      reference: ";
    s.Print(text, b, 6);
    *text += "\n";
}

} // namespace

void ShaderCheck_ShowCode(bool show) { g_showCode = show; }

void ShaderCheck_Count(const uint32_t *code, int *arithmetic, int *texture) {
    *arithmetic = *texture = 0;
    for (size_t n = 1;;) {
        uint32_t tok = code[n];
        int op = (int)(tok & 0xFFFF);
        if (op == 0xFFFF) return;
        if (op == 0xFFFE) { n += 1 + ((tok >> 16) & 0x7FFF); continue; }
        if (op == 66) (*texture)++;
        else if (op != 31 && op != 81 && op != 0) (*arithmetic)++;
        n += 1 + ((tok >> 24) & 0xF);
    }
}

int ShaderCheck_Compare(const uint32_t *ours, const uint32_t *reference, char *reportText, size_t reportSize) {
    Report report = { reportText, reportSize, 0 };
    if (reportSize) reportText[0] = 0;
    bool pixel = (ours[0] & 0xFFFF0000u) == 0xFFFF0000u;
    Random rng = { 0x12345678u };
    int differing[SLOT_COUNT][4];
    int firstTrial[SLOT_COUNT][4];
    float example[SLOT_COUNT][4][2];
    memset(differing, 0, sizeof(differing));
    Inputs *in = new Inputs;
    Machine a, b;
    Outputs oa, ob;
    int result = 0;
    for (int trial = 0; trial < kTrials; trial++) {
        bool bytes = trial % 4 == 1;   // raw bytes, as a UBYTE4 input delivers them
        for (int i = 0; i < 32; i++) for (int c = 0; c < 4; c++)
            in->semantic[i][c] = pixel ? rng.Uniform(0.0f, 1.0f) : bytes ? (float)(rng.Next() % 256) : rng.Value();
        for (int i = 0; i < 8; i++) for (int c = 0; c < 4; c++) in->tex[i][c] = c == 3 ? rng.Uniform(0.5f, 2.0f) : rng.Value();
        for (int i = 0; i < 256; i++) for (int c = 0; c < 4; c++) in->constant[i][c] = rng.Value();
        std::string ea, eb;
        if (!a.Run(ours, *in, &oa, &ea) || !b.Run(reference, *in, &ob, &eb)) {
            report.printf("cannot run: %s%s%s\n", ea.c_str(), ea.empty() || eb.empty() ? "" : "; ", eb.c_str());
            delete in;
            return -1;
        }
        for (int s = 0; s < SLOT_COUNT; s++) {
            for (int c = 0; c < 4; c++) {
                bool wa = (oa.written[s] >> c) & 1, wb = (ob.written[s] >> c) & 1;
                if (!wa && !wb) continue;
                float x = oa.value[s][c], y = ob.value[s][c];
                bool same = wa && wb && (Bits(x) == Bits(y) || (x != x && y != y) || (x == 0.0f && y == 0.0f));
                if (same) continue;
                if (differing[s][c]++ == 0) { firstTrial[s][c] = trial; example[s][c][0] = wa ? x : NAN; example[s][c][1] = wb ? y : NAN; }
            }
        }
    }
    Symbolic *sym = new Symbolic;
    SymMachine sa, sb;
    SymRef xa[SLOT_COUNT][4], xb[SLOT_COUNT][4];
    uint8_t wa[SLOT_COUNT], wb[SLOT_COUNT];
    bool symbolic = sa.Run(ours, sym, xa, wa) && sb.Run(reference, sym, xb, wb);
    int shown = 0;
    for (int s = 0; s < SLOT_COUNT; s++) {
        for (int c = 0; c < 4; c++) {
            if (!differing[s][c]) continue;
            result++;
            report.printf("  %s.%c differs in %d of %d trials (first, trial %d: ours %.9g, reference %.9g)\n", SlotName(s, pixel),
                          "xyzw"[c], differing[s][c], kTrials, firstTrial[s][c], example[s][c][0], example[s][c][1]);
            if (symbolic && shown < 4 && ((wa[s] & wb[s]) >> c) & 1) {
                std::string text;
                Divergence(*sym, xa[s][c], xb[s][c], &text);
                report.printf("%s", text.c_str());
                shown++;
            }
        }
    }
    delete sym;
    delete in;
    return result;
}

bool ShaderCheck_Vertex(const uint8_t *function, uint32_t normPackedInputs, const uint8_t inputComponents[16],
                        char *reportText, size_t reportSize) {
    Report report = { reportText, reportSize, 0 };
    std::vector<uint32_t> ours, reference;
    Nv2aVertexShaderInfo info;
    char error[256];
    bool built = Nv2aVertexShader_Translate(function, normPackedInputs, inputComponents, &ours, &info, error, sizeof(error));
    static char hlsl[65536];
    uint32_t inputsUsed, outputsWritten;
    int count;
    std::string message = "translation failed";
    static const char *const profiles[2] = { "vs_2_0", "vs_2_a" };
    bool compiled = ShaderHlsl_Vertex(function, &normPackedInputs, inputComponents, hlsl, sizeof(hlsl), &inputsUsed, &outputsWritten, &count) &&
                    Compile(hlsl, profiles, 2, &reference, &message);
    if (!built || !compiled) {
        report.printf("%s: ours %s, HLSL %s\n", built == compiled ? "same" : "DIFFERENT", built ? "builds" : error,
                      compiled ? "compiles" : message.c_str());
        return built == compiled;
    }
    char details[4096];
    int differing = ShaderCheck_Compare(ours.data(), reference.data(), details, sizeof(details));
    int ourCount, refCount, tex;
    ShaderCheck_Count(ours.data(), &ourCount, &tex);
    ShaderCheck_Count(reference.data(), &refCount, &tex);
    report.printf("%s: %d instructions against the compiler's %d%s\n%s", differing == 0 ? "same" : "DIFFERENT", ourCount, refCount,
                  differing > 0 ? ", outputs differ:" : "", details);
    if (differing != 0 || g_showCode) {
        Disassemble(&report, "ours", ours.data());
        Disassemble(&report, "compiled HLSL", reference.data());
        if (g_showCode) report.printf("---- HLSL\n%s", hlsl);
    }
    return differing == 0;
}

bool ShaderCheck_Pixel(const uint32_t def[60], char *reportText, size_t reportSize) {
    Report report = { reportText, reportSize, 0 };
    std::vector<uint32_t> ours, reference;
    Nv2aPixelShaderInfo info;
    char error[256];
    bool built = Nv2aPixelShader_Translate(def, &ours, &info, error, sizeof(error));
    static char hlsl[32768];
    std::string message = "translation failed";
    static const char *const profiles[3] = { "ps_2_0", "ps_2_b", "ps_2_a" };
    bool compiled = ShaderHlsl_Pixel(def, hlsl, sizeof(hlsl), &info) && Compile(hlsl, profiles, 3, &reference, &message);
    if (!built || !compiled) {
        report.printf("%s: ours %s, HLSL %s\n", built == compiled ? "same" : "DIFFERENT", built ? "builds" : error,
                      compiled ? "compiles" : message.c_str());
        return built == compiled;
    }
    char details[4096];
    int differing = ShaderCheck_Compare(ours.data(), reference.data(), details, sizeof(details));
    int ourCount, refCount, ourTex, refTex;
    ShaderCheck_Count(ours.data(), &ourCount, &ourTex);
    ShaderCheck_Count(reference.data(), &refCount, &refTex);
    report.printf("%s: %d+%d instructions against the compiler's %d+%d%s\n%s", differing == 0 ? "same" : "DIFFERENT", ourCount, ourTex,
                  refCount, refTex, differing > 0 ? ", outputs differ:" : "", details);
    if (differing != 0 || g_showCode) {
        Disassemble(&report, "ours", ours.data());
        Disassemble(&report, "compiled HLSL", reference.data());
        if (g_showCode) report.printf("---- HLSL\n%s", hlsl);
    }
    return differing == 0;
}
