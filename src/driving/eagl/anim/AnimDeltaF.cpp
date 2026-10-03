#include "AnimDeltaF.h"
#include "Skeleton.h"
#include "../EaglGlobals.h"
#include "../../platform/X87.h"
#include "../../../helpers.h"

#include <bit>
#include <xmmintrin.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// FnDeltaF1 and FnDeltaF3 (docs/driving/eagl.md 3.4), most of the disc's channels. The data keeps its keys in
// blocks of 2^shift: each block a u16 base value per value, then a row of u8 deltas per key after the first. A
// key is reached from the cached one by adding the rows on (or, going back, taking them off, unless the global at
// 0x001ceb4c disables that and the block is decoded again from its base); between keys the next key's values,
// kept in a second buffer that becomes the first when time reaches it, are interpolated. Constant values follow
// the blocks. The masked variant skips values whose bone (output index / 12) is not in the 256-bit mask and starts
// again whenever the mask changes. Each function is the original at the same address; x87 in double in the
// original's order with a float store per store (a single operation on floats written in float: the same bits).
// ---------------------------------------------------------------------------------------------------------------

namespace {

#define VtDeltaF1 ((const void *)0x001a1270)
#define VtDeltaF3 ((const void *)0x001a12e8)

// The dequantisation's scales (the original's .rdata floats at 0x001a1260 / 0x001a12d8 and 0x001a1258 / 0x001a12d0)
constexpr float kOneOver65535 = 1.0f / 65535;
constexpr float kOneOver255 = 1.0f / 255;
static_assert(std::bit_cast<uint32_t>(kOneOver65535) == 0x37800080, "the original's 1/65535");
static_assert(std::bit_cast<uint32_t>(kOneOver255) == 0x3b808081, "the original's 1/255");

// Where the key blocks start: after the header and the range records.
template <typename Range>
const uint8_t* Blocks(const DeltaFData *d) {
    return reinterpret_cast<const uint8_t *>(reinterpret_cast<const Range *>(d + 1) + d->count);
}

// The key below the time: from the cached key, back or forward through the key times.
int FindKey(int frame, const DeltaFData *d, int cached) {
    if (d->times == NULL)
        return frame < 0 ? 0 : (frame >= d->keys ? d->keys - 1 : frame);
    if (frame < d->times[0])
        return 0;
    int i = cached >= 1 ? cached - 1 : 0;
    if (d->times[i] > frame) {
        while (i > 0 && d->times[i] > frame)
            i--;
    } else {
        int last = d->keys - 2;
        while (i < last && d->times[i + 1] <= frame)
            i++;
    }
    return i + 1;
}

// How far between key k and the next the time is: false on the key itself (an unordered compare counts as
// between, as the original's !(==) does).
bool Fraction(const DeltaFData *d, int k, int frame, float time, float *s) {
    if (d->times == NULL) {
        float frameTime = float(frame);
        if (!(time == frameTime)) {
            *s = time - frameTime;
            return true;
        }
    } else if (k == 0) {
        if (!(time == 0.0f)) {
            *s = time / d->times[0];
            return true;
        }
    } else {
        double previous = d->times[k - 1];
        if (!(time == previous)) {
            *s = float((time - previous) / (d->times[k] - previous));
            return true;
        }
    }
    return false;
}

// The constants after the blocks: their u16 output indexes, then their values on a 4-byte boundary.
void WriteConstants(const DeltaFData *d, const uint8_t *blocks, int blockSize, int unitMask, float *sqt) {
    int constants = d->constants;
    if (constants <= 0)
        return;
    int n = d->count;
    uintptr_t p = reinterpret_cast<uintptr_t>(blocks + blockSize * (d->keys >> d->shift));
    int rem = d->keys & unitMask;
    if (rem > 0)
        p = (p + (rem + 1) * n + 1) & ~uintptr_t(1);
    if (n == 0)
        p = (p + 1) & ~uintptr_t(1);
    const uint16_t *index = reinterpret_cast<const uint16_t *>(p);
    const float *value = reinterpret_cast<const float *>((p + constants * 2 + 3) & ~uintptr_t(3));
    for (int c = 0; c < constants; c++)
        sqt[index[c]] = value[c];
}

bool EvalF1Core(FnDeltaF *self, float time, float *sqt, const BoneMask *mask) {
    const DeltaFData *d = reinterpret_cast<const DeltaFData *>(self->anim);
    int n = d->count;
    int shift = d->shift;
    int frame = Truncate(time);
    int k = FindKey(frame, d, self->key);
    int unitMask = 0x7fffffff >> (31 - shift);
    int block = k >> shift, offset = k & unitMask;
    int cached = self->key;
    int cachedBlock = cached >> shift;
    int blockSize = ((((1 << shift) + 1) * n) + 1) & ~1;
    const uint8_t *blocks = Blocks<DeltaF1Range>(d);
    const uint8_t *bp = blocks + blockSize * block;
    const DeltaF1Dequant *P = self->dequant1;
    uint8_t bone[256];
    if (mask != NULL)
        for (int i = 0; i < n; i++)
            bone[i] = uint8_t(d->index[i] / 12);
    auto skip = [&](int i) { return mask != NULL && !mask->Has(bone[i]); };
    bool restart = k < cached && ReverseDeltaSumEnabled == 0;
    if (k == self->nextKey) {
        float *t = self->values;
        self->values = self->next;
        self->next = t;
        self->key = self->nextKey;
        self->nextKey = -1;
    } else {
        int start;
        if (cached != -1 && block == cachedBlock && offset != 0 && !restart) {
            start = cached & unitMask;
        } else {
            const uint16_t *base = reinterpret_cast<const uint16_t *>(bp);
            for (int i = 0; i < n; i++) {
                if (skip(i))
                    continue;
                self->values[i] = float(base[i] * double(P[i].scale) + P[i].min);
            }
            start = 0;
        }
        if (start < offset) {
            const uint8_t *row = bp + (start + 2) * n;
            for (int f = start; f < offset; f++, row += n)
                for (int i = 0; i < n; i++) {
                    if (skip(i))
                        continue;
                    self->values[i] = float(row[i] * double(P[i].deltaScale) + P[i].deltaOffset + self->values[i]);
                }
        } else if (start > offset) {
            const uint8_t *row = bp + (start + 1) * n;
            for (int f = start; f > offset; f--, row -= n)
                for (int i = 0; i < n; i++) {
                    if (skip(i))
                        continue;
                    self->values[i] =
                        float(self->values[i] - (row[i] * double(P[i].deltaScale) + P[i].deltaOffset));
                }
        }
    }
    self->key = k;

    float s = 1.0f;
    bool interpolate = Fraction(d, k, frame, time, &s);
    const uint16_t *index = d->index;
    if (!interpolate || k >= d->keys - 1) {
        for (int i = 0; i < n; i++)
            if (!skip(i))
                sqt[index[i]] = self->values[i];
    } else {
        int nb = (k + 1) >> shift;
        const uint8_t *np = blocks + blockSize * nb;
        float *v = self->values;
        if (k + 1 == self->nextKey) {
            for (int i = 0; i < n; i++) {
                if (skip(i))
                    continue;
                sqt[index[i]] = float((double(self->next[i]) - v[i]) * s + v[i]);
            }
        } else if (nb != block) {
            const uint16_t *base = reinterpret_cast<const uint16_t *>(np);
            for (int i = 0; i < n; i++) {
                if (skip(i))
                    continue;
                self->next[i] = float(base[i] * double(P[i].scale) + P[i].min);
                sqt[index[i]] = float((double(self->next[i]) - v[i]) * s + v[i]);
            }
            self->nextKey = k + 1;
        } else {
            const uint8_t *row = np + (offset + 2) * n;
            for (int i = 0; i < n; i++) {
                if (skip(i))
                    continue;
                double delta = row[i] * double(P[i].deltaScale) + P[i].deltaOffset;
                self->next[i] = float(delta + v[i]);
                sqt[index[i]] = float(delta * s + v[i]);
            }
            self->nextKey = k + 1;
        }
    }
    WriteConstants(d, blocks, blockSize, unitMask, sqt);
    return true;
}

}  // namespace

// FUNC_AT(0x000ff270)
int AnimTruncateF1(float value) {
    return Truncate(value);
}

// FUNC_AT(0x00100970)
int AnimTruncateF3(float value) {
    return Truncate(value);
}

// ---- FnDeltaF1

// FUNC_AT(0x000fe280)
FnDeltaF* FnDeltaF::ConstructF1() {
    FnAnimMemoryMap::Construct();
    valuesBlock = NULL;
    values = NULL;
    nextBlock = NULL;
    next = NULL;
    vtable = VtDeltaF1;
    key = -1;
    nextKey = -1;
    type = kDeltaF1;
    return this;
}

// FUNC_AT(0x000fe2b0)
void FnDeltaF::EvalF1(float, float time, float *out) {
    AnimVCall<bool>(this, kSlotEvalSQT, time, out, (void *)NULL);
}

// FUNC_AT(0x000feaa0)
bool FnDeltaF::EvalWeightsF1(float time, float *weights) {
    AnimVCall<void>(this, kSlotEval, time, time, weights);
    return true;
}

// FUNC_AT(0x000feac0)
bool FnDeltaF::EvalVel2DF1(float time, float *velocity) {
    AnimVCall<void>(this, kSlotEval, time, time, velocity);
    return true;
}

// The value buffers and each value's dequantisation: {min, scale / 65535, offset * scale / 65535 * 2 - scale,
// (delta scale * scale / 65535 * 2) / 255}, rounded to float where the original stores.
// FUNC_AT(0x000feae0)
void FnDeltaF::InitBuffersF1() {
    const DeltaFData *d = reinterpret_cast<const DeltaFData *>(anim);
    int n = d->count;
    if (n == 0)
        return;
    values = valuesBlock = static_cast<float *>(AnimPool_NewBlock(n * sizeof(float)));
    next = nextBlock = static_cast<float *>(AnimPool_NewBlock(n * sizeof(float)));
    dequant1 = static_cast<DeltaF1Dequant *>(AnimPool_NewBlock(n * sizeof(DeltaF1Dequant)));
    const DeltaF1Range *ranges = reinterpret_cast<const DeltaF1Range *>(d + 1);
    for (int i = 0; i < n; i++) {
        const DeltaF1Range &range = ranges[i];
        DeltaF1Dequant &q = dequant1[i];
        q.min = range.min;
        q.scale = kOneOver65535 * range.scale;
        double offset = range.deltaOffset * double(range.scale) * kOneOver65535;
        q.deltaOffset = float(offset + offset - range.scale);
        double deltaScale = range.deltaScale * double(range.scale) * kOneOver65535;
        q.deltaScale = float(deltaScale + deltaScale);
        q.deltaScale = kOneOver255 * q.deltaScale;
    }
}

// FUNC_AT(0x000febd0)
bool FnDeltaF::EvalSQTF1(float time, float *sqt, void *m) {
    if (values == NULL)
        InitBuffersF1();
    if (m != NULL)
        return EvalSQTMaskF1(time, sqt, m);
    if (mask != NULL) {
        key = -1;
        nextKey = -1;
        mask = NULL;
    }
    return EvalF1Core(this, time, sqt, NULL);
}

// FUNC_AT(0x000fe2d0)
bool FnDeltaF::EvalSQTMaskF1(float time, float *sqt, void *m) {
    if (m != mask) {
        key = -1;
        nextKey = -1;
        mask = m;
    }
    return EvalF1Core(this, time, sqt, static_cast<const BoneMask *>(m));
}

// FUNC_AT(0x000ff210)
void FnDeltaF::DestructF1() {
    vtable = VtDeltaF1;
    if (values != NULL) {
        AnimPool_FreeBlock(valuesBlock);
        AnimPool_FreeBlock(nextBlock);
        AnimPool_FreeBlock(dequant1);
    }
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x000ff1e0)
FnDeltaF* FnDeltaF::ScalarDeleteF1(unsigned flags) {
    DestructF1();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// ---- both

// FUNC_AT(0x00100880)
void FnDeltaF::SetAnimMemoryMap(uint8_t *data) {
    anim = data;
    key = -1;
    nextKey = -1;
}

// The last key time plus one, or the key count (16-bit, as the original).
// FUNC_AT(0x001008a0)
bool FnDeltaF::GetLength(float *length) {
    const DeltaFData *d = reinterpret_cast<const DeltaFData *>(anim);
    uint16_t frames = d->times == NULL ? d->keys : uint16_t(d->times[d->keys - 2] + 1);
    *length = float(frames);
    return true;
}

// ---- FnDeltaF3's object functions

// FUNC_AT(0x00100910)
void FnDeltaF::DestructF3() {
    vtable = VtDeltaF3;
    if (values != NULL) {
        AnimPool_FreeBlock(valuesBlock);
        AnimPool_FreeBlock(nextBlock);
        AnimPool_FreeBlock(dequant3);
    }
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x001008e0)
FnDeltaF* FnDeltaF::ScalarDeleteF3(unsigned flags) {
    DestructF3();
    if (flags & 1)
        EaglFree(this, sizeof(*this));
    return this;
}

// ---- FnDeltaF3: the same over 3-vectors (values padded to 4 floats). Its z delta is rounded to float before it
// is added.

// The constants after the blocks (their u16 output indexes, then their vectors on a 4-byte boundary).
// FUNC_AT(0x00100800)
uint8_t* DeltaF3Header::ConstantsPointer() {
    int n = count;
    int blockSize = ((((1 << shift) + 1) * n) * 3 + 1) & ~1;
    uintptr_t p = reinterpret_cast<uintptr_t>(Blocks<DeltaF3Range>(this)) + blockSize * (keys >> shift);
    int rem = keys & (0x7fffffff >> (31 - shift));
    if (rem > 0)
        p = (p + 3 * (rem + 1) * n + 1) & ~uintptr_t(1);
    if (n == 0)
        p = (p + 1) & ~uintptr_t(1);
    return reinterpret_cast<uint8_t *>(p);
}

namespace {

bool EvalF3Core(FnDeltaF *self, float time, float *sqt, const BoneMask *mask) {
    const DeltaFData *d = reinterpret_cast<const DeltaFData *>(self->anim);
    int n = d->count;
    int shift = d->shift;
    int frame = Truncate(time);
    int k = FindKey(frame, d, self->key);
    int unitMask = 0x7fffffff >> (31 - shift);
    int block = k >> shift, offset = k & unitMask;
    int cached = self->key;
    int cachedBlock = cached >> shift;
    int blockSize = ((((1 << shift) + 1) * n) * 3 + 1) & ~1;
    int stride = 3 * n;
    const uint8_t *blocks = Blocks<DeltaF3Range>(d);
    const uint8_t *bp = blocks + blockSize * block;
    const DeltaF3Dequant *P = self->dequant3;
    uint8_t bone[256];
    if (mask != NULL)
        for (int i = 0; i < n; i++)
            bone[i] = uint8_t(d->index[i] / 12);
    auto skip = [&](int i) { return mask != NULL && !mask->Has(bone[i]); };
    bool restart = k < cached && ReverseDeltaSumEnabled == 0;
    if (k == self->nextKey) {
        float *t = self->values;
        self->values = self->next;
        self->next = t;
        self->key = self->nextKey;
        self->nextKey = -1;
    } else {
        int start;
        if (cached != -1 && block == cachedBlock && offset != 0 && !restart) {
            start = cached & unitMask;
        } else {
            const uint16_t *base = reinterpret_cast<const uint16_t *>(bp);
            for (int i = 0; i < n; i++) {
                if (skip(i))
                    continue;
                float *v = self->values + i * 4;
                for (int c = 0; c < 3; c++)
                    v[c] = float(base[i * 3 + c] * double(P[i].scale[c]) + P[i].min[c]);
            }
            start = 0;
        }
        if (start < offset) {
            const uint8_t *row = bp + (start + 2) * stride;
            for (int f = start; f < offset; f++, row += stride)
                for (int i = 0; i < n; i++) {
                    if (skip(i))
                        continue;
                    float *v = self->values + i * 4;
                    const DeltaF3Dequant &q = P[i];
                    double dx = row[i * 3] * double(q.deltaScale[0]) + q.deltaOffset[0];
                    double dy = row[i * 3 + 1] * double(q.deltaScale[1]) + q.deltaOffset[1];
                    float dz = float(row[i * 3 + 2] * double(q.deltaScale[2]) + q.deltaOffset[2]);
                    v[0] = float(dx + v[0]);
                    v[1] = float(dy + v[1]);
                    v[2] = dz + v[2];
                }
        } else if (start > offset) {
            const uint8_t *row = bp + (start + 1) * stride;
            for (int f = start; f > offset; f--, row -= stride)
                for (int i = 0; i < n; i++) {
                    if (skip(i))
                        continue;
                    float *v = self->values + i * 4;
                    const DeltaF3Dequant &q = P[i];
                    double dx = row[i * 3] * double(q.deltaScale[0]) + q.deltaOffset[0];
                    double dy = row[i * 3 + 1] * double(q.deltaScale[1]) + q.deltaOffset[1];
                    float dz = float(row[i * 3 + 2] * double(q.deltaScale[2]) + q.deltaOffset[2]);
                    v[0] = float(v[0] - dx);
                    v[1] = float(v[1] - dy);
                    v[2] = v[2] - dz;
                }
        }
    }
    self->key = k;

    float s = 1.0f;
    bool interpolate = Fraction(d, k, frame, time, &s);
    const uint16_t *index = d->index;
    if (!interpolate || k >= d->keys - 1) {
        for (int i = 0; i < n; i++)
            if (!skip(i))
                for (int c = 0; c < 3; c++)
                    sqt[index[i] + c] = self->values[i * 4 + c];
    } else {
        int nb = (k + 1) >> shift;
        const uint8_t *np = blocks + blockSize * nb;
        if (k + 1 == self->nextKey) {
            for (int i = 0; i < n; i++) {
                if (skip(i))
                    continue;
                float *v = self->values + i * 4, *w = self->next + i * 4;
                for (int c = 0; c < 3; c++)
                    sqt[index[i] + c] = float((double(w[c]) - v[c]) * s + v[c]);
            }
        } else if (nb != block) {
            const uint16_t *base = reinterpret_cast<const uint16_t *>(np);
            for (int i = 0; i < n; i++) {
                if (skip(i))
                    continue;
                float *v = self->values + i * 4, *w = self->next + i * 4;
                for (int c = 0; c < 3; c++)
                    w[c] = float(base[i * 3 + c] * double(P[i].scale[c]) + P[i].min[c]);
                for (int c = 0; c < 3; c++)
                    sqt[index[i] + c] = float((double(w[c]) - v[c]) * s + v[c]);
            }
            self->nextKey = k + 1;
        } else {
            const uint8_t *row = np + (offset + 2) * stride;
            for (int i = 0; i < n; i++) {
                if (skip(i))
                    continue;
                float *v = self->values + i * 4, *w = self->next + i * 4;
                const DeltaF3Dequant &q = P[i];
                double dx = row[i * 3] * double(q.deltaScale[0]) + q.deltaOffset[0];
                double dy = row[i * 3 + 1] * double(q.deltaScale[1]) + q.deltaOffset[1];
                float dz = float(row[i * 3 + 2] * double(q.deltaScale[2]) + q.deltaOffset[2]);
                w[0] = float(dx + v[0]);
                w[1] = float(dy + v[1]);
                w[2] = dz + v[2];
                sqt[index[i]] = float(dx * s + v[0]);
                sqt[index[i] + 1] = float(dy * s + v[1]);
                sqt[index[i] + 2] = float(double(dz) * s + v[2]);
            }
            self->nextKey = k + 1;
        }
    }
    int constants = d->constants;
    if (constants > 0) {
        uintptr_t p = reinterpret_cast<uintptr_t>(reinterpret_cast<DeltaF3Header *>(self->anim)->ConstantsPointer());
        const uint16_t *constantIndex = reinterpret_cast<const uint16_t *>(p);
        const float *value = reinterpret_cast<const float *>((p + constants * 2 + 3) & ~uintptr_t(3));
        for (int c = 0; c < constants; c++, value += 3)
            for (int j = 0; j < 3; j++)
                sqt[constantIndex[c] + j] = value[j];
    }
    return true;
}

}  // namespace

// FUNC_AT(0x000ff280)
FnDeltaF* FnDeltaF::ConstructF3() {
    FnAnimMemoryMap::Construct();
    valuesBlock = NULL;
    values = NULL;
    nextBlock = NULL;
    next = NULL;
    vtable = VtDeltaF3;
    key = -1;
    nextKey = -1;
    type = kDeltaF3;
    return this;
}

// FUNC_AT(0x000ff2b0)
void FnDeltaF::EvalF3(float, float time, float *out) {
    AnimVCall<bool>(this, kSlotEvalSQT, time, out, (void *)NULL);
}

// FUNC_AT(0x000ffce0)
bool FnDeltaF::EvalWeightsF3(float time, float *weights) {
    AnimVCall<void>(this, kSlotEval, time, time, weights);
    return true;
}

// FUNC_AT(0x000ffd00)
bool FnDeltaF::EvalVel2DF3(float time, float *velocity) {
    AnimVCall<void>(this, kSlotEval, time, time, velocity);
    return true;
}

// As the F1 dequantisation per component, x's products as (offset * scale) / 65535, y's and z's as
// (offset / 65535) * scale - the original's orders.
// FUNC_AT(0x000ffd20)
void FnDeltaF::InitBuffersF3() {
    const DeltaFData *d = reinterpret_cast<const DeltaFData *>(anim);
    int n = d->count;
    if (n == 0)
        return;
    values = valuesBlock = static_cast<float *>(AnimPool_NewBlock(n * 4 * sizeof(float)));
    next = nextBlock = static_cast<float *>(AnimPool_NewBlock(n * 4 * sizeof(float)));
    dequant3 = static_cast<DeltaF3Dequant *>(AnimPool_NewBlock(n * sizeof(DeltaF3Dequant)));
    const DeltaF3Range *ranges = reinterpret_cast<const DeltaF3Range *>(d + 1);
    const double k1 = kOneOver65535;
    for (int i = 0; i < n; i++) {
        const DeltaF3Range &range = ranges[i];
        DeltaF3Dequant &q = dequant3[i];
        for (int c = 0; c < 3; c++) {
            double scale = range.scale[c];
            double offset = range.deltaOffset[c];
            double deltaScale = range.deltaScale[c];
            q.min[c] = range.min[c];
            q.scale[c] = kOneOver65535 * range.scale[c];
            double o = c == 0 ? offset * scale * k1 : offset * k1 * scale;
            q.deltaOffset[c] = float(o + o - scale);
            double ds = c == 0 ? deltaScale * scale * k1 : deltaScale * k1 * scale;
            q.deltaScale[c] = float(ds + ds);
            q.deltaScale[c] = kOneOver255 * q.deltaScale[c];
        }
    }
}

// FUNC_AT(0x000fff10)
bool FnDeltaF::EvalSQTF3(float time, float *sqt, void *m) {
    if (values == NULL)
        InitBuffersF3();
    if (m != NULL)
        return EvalSQTMaskF3(time, sqt, m);
    if (mask != NULL) {
        key = -1;
        nextKey = -1;
        mask = NULL;
    }
    return EvalF3Core(this, time, sqt, NULL);
}

// FUNC_AT(0x000ff2d0)
bool FnDeltaF::EvalSQTMaskF3(float time, float *sqt, void *m) {
    if (m != mask) {
        key = -1;
        nextKey = -1;
        mask = m;
    }
    return EvalF3Core(this, time, sqt, static_cast<const BoneMask *>(m));
}
