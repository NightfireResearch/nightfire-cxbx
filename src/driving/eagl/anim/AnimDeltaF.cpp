#include "AnimDeltaF.h"

#include <string.h>
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
// original's order with a float store per store.
// ---------------------------------------------------------------------------------------------------------------

#define EaglFree     (*(void (**)(void *data, uint32_t size))0x001caf6cu)
#define FreeBySize   ((void **)0x00241520u)
#define VtDeltaF1    ((const void *)0x001a1270u)
#define VtDeltaF3    ((const void *)0x001a12e8u)
#define ReverseDeltaSumEnabled (*(uint8_t *)0x001ceb4cu)   // 0: going back decodes the block again
#define DequantK1    (*(const float *)0x001a1260u)
#define DequantK2    (*(const float *)0x001a1258u)

static inline int Truncate(float f) {
    return _mm_cvtt_ss2si(_mm_set_ss(f));
}

static inline void FreeSized(void *block) {
    uint32_t sizeClass = ((uint32_t *)block)[-1];
    *(void **)block = FreeBySize[sizeClass];
    FreeBySize[sizeClass] = block;
}

static inline void CopyBits(float *to, const void *from) {
    memcpy(to, from, 4);
}

// FUNC_AT(0x000ff270)
int AnimTruncateF1(float value) {
    return Truncate(value);
}

// FUNC_AT(0x00100970)
int AnimTruncateF3(float value) {
    return Truncate(value);
}

// ---- the F1 data: u16 type, u16 checksum, u16 output index*, u16 key times* (or none: a key a frame), u16 keys,
// u16 values, u8 shift, u8 constants, u16, {u32 min, float scale, u16 delta offset, u16 delta scale}[values], blocks

struct DeltaF1Data {
    uint16_t type, checksum;
    uint16_t *index;
    uint16_t *times;
    uint16_t keys;
    uint16_t count;
    uint8_t shift;
    uint8_t constants;
    uint16_t pad;
};

static inline bool InMask(const uint32_t *mask, int bone) {
    return (mask[bone >> 5] & (1u << (bone & 31))) != 0;
}

// The key below the time: from the cached key, back or forward through the key times.
static int FindKey(int frame, const DeltaF1Data *d, int cached) {
    if (d->times == NULL)
        return frame < 0 ? 0 : (frame >= d->keys ? d->keys - 1 : frame);
    if (frame < (int)d->times[0])
        return 0;
    int i = cached >= 1 ? cached - 1 : 0;
    if ((int)d->times[i] > frame) {
        while (i > 0 && (int)d->times[i] > frame)
            i--;
    } else {
        int last = d->keys - 2;
        while (i < last && (int)d->times[i + 1] <= frame)
            i++;
    }
    return i + 1;
}

static void WriteConstants(const DeltaF1Data *d, uint8_t *blocks, int blockSize, int unitMask, float *sqt) {
    int constants = d->constants;
    if (constants <= 0)
        return;
    int n = d->count;
    uintptr_t p = (uintptr_t)(blocks + blockSize * (d->keys >> d->shift));
    int rem = d->keys & unitMask;
    if (rem > 0)
        p = (p + (uintptr_t)((rem + 1) * n) + 1) & ~(uintptr_t)1;
    if (n == 0)
        p = (p + 1) & ~(uintptr_t)1;
    const uint16_t *index = (const uint16_t *)p;
    const uint32_t *value = (const uint32_t *)((p + (uintptr_t)constants * 2 + 3) & ~(uintptr_t)3);
    for (int c = 0; c < constants; c++)
        CopyBits(&sqt[index[c]], &value[c]);
}

static bool EvalF1Core(FnDeltaF *self, float time, float *sqt, const uint32_t *mask) {
    const DeltaF1Data *d = (const DeltaF1Data *)self->anim;
    int n = d->count;
    int shift = d->shift;
    int frame = Truncate(time);
    int k = FindKey(frame, d, self->key);
    int unitMask = (int)(0x7fffffffu >> (31 - shift));
    int block = k >> shift, offset = k & unitMask;
    int cached = self->key;
    int cachedBlock = cached >> shift;
    int blockSize = ((((1 << shift) + 1) * n) + 1) & ~1;
    uint8_t *blocks = (uint8_t *)d + n * 12 + 0x14;
    uint8_t *bp = blocks + blockSize * block;
    const float *P = self->dequant;
    uint8_t bone[256];
    if (mask != NULL)
        for (int i = 0; i < n; i++)
            bone[i] = (uint8_t)(d->index[i] / 12);
#define SKIP(i) (mask != NULL && !InMask(mask, bone[i]))
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
            const uint16_t *base = (const uint16_t *)bp;
            for (int i = 0; i < n; i++) {
                if (SKIP(i))
                    continue;
                self->values[i] = (float)((double)(int)base[i] * (double)P[i * 4 + 1] + (double)P[i * 4]);
            }
            start = 0;
        }
        if (start < offset) {
            const uint8_t *row = bp + (start + 2) * n;
            for (int f = start; f < offset; f++, row += n)
                for (int i = 0; i < n; i++) {
                    if (SKIP(i))
                        continue;
                    self->values[i] = (float)(((double)(int)row[i] * (double)P[i * 4 + 3] + (double)P[i * 4 + 2]) +
                                              (double)self->values[i]);
                }
        } else if (start > offset) {
            const uint8_t *row = bp + (start + 1) * n;
            for (int f = start; f > offset; f--, row -= n)
                for (int i = 0; i < n; i++) {
                    if (SKIP(i))
                        continue;
                    self->values[i] = (float)((double)self->values[i] -
                                              ((double)(int)row[i] * (double)P[i * 4 + 3] + (double)P[i * 4 + 2]));
                }
        }
    }
    self->key = k;

    bool interpolate;
    float s = 1.0f;
    if (d->times == NULL) {
        double tf = (double)frame;
        interpolate = !((double)time == tf);
        if (interpolate)
            s = (float)((double)time - tf);
    } else if (k == 0) {
        interpolate = !((double)time == 0.0);
        if (interpolate)
            s = (float)((double)time / (double)(int)d->times[0]);
    } else {
        double p = (double)(int)d->times[k - 1];
        interpolate = !((double)time == p);
        if (interpolate)
            s = (float)(((double)time - p) / ((double)(int)d->times[k] - p));
    }
    const uint16_t *index = d->index;
    if (!interpolate || k >= d->keys - 1) {
        for (int i = 0; i < n; i++)
            if (!SKIP(i))
                CopyBits(&sqt[index[i]], &self->values[i]);
    } else {
        int nb = (k + 1) >> shift;
        uint8_t *np = blocks + blockSize * nb;
        float *v = self->values;
        if (k + 1 == self->nextKey) {
            for (int i = 0; i < n; i++) {
                if (SKIP(i))
                    continue;
                sqt[index[i]] = (float)(((double)self->next[i] - (double)v[i]) * (double)s + (double)v[i]);
            }
        } else if (nb != block) {
            const uint16_t *base = (const uint16_t *)np;
            for (int i = 0; i < n; i++) {
                if (SKIP(i))
                    continue;
                self->next[i] = (float)((double)(int)base[i] * (double)P[i * 4 + 1] + (double)P[i * 4]);
                sqt[index[i]] = (float)(((double)self->next[i] - (double)v[i]) * (double)s + (double)v[i]);
            }
            self->nextKey = k + 1;
        } else {
            const uint8_t *row = np + (offset + 2) * n;
            for (int i = 0; i < n; i++) {
                if (SKIP(i))
                    continue;
                double delta = (double)(int)row[i] * (double)P[i * 4 + 3] + (double)P[i * 4 + 2];
                self->next[i] = (float)(delta + (double)v[i]);
                sqt[index[i]] = (float)(delta * (double)s + (double)v[i]);
            }
            self->nextKey = k + 1;
        }
    }
#undef SKIP
    WriteConstants(d, blocks, blockSize, unitMask, sqt);
    return true;
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
void FnDeltaF::EvalF1(float previous, float time, float *out) {
    (void)previous;
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

// The value buffers and each value's dequantisation: {min, scale * K1, offset * scale * K1 * 2 - scale,
// (delta scale * scale * K1 * 2) * K2}.
// FUNC_AT(0x000feae0)
void FnDeltaF::InitBuffersF1() {
    uint8_t *d = anim;
    int n = *(uint16_t *)(d + 0xe);
    if (n == 0)
        return;
    values = valuesBlock = (float *)AnimPool_NewBlock((uint32_t)n * 4);
    next = nextBlock = (float *)AnimPool_NewBlock((uint32_t)n * 4);
    dequant = (float *)AnimPool_NewBlock((uint32_t)n * 16);
    const uint8_t *e = d + 0x14;
    for (int i = 0; i < n; i++, e += 12) {
        float *P = dequant + i * 4;
        double scale = (double)*(const float *)(e + 4);
        memcpy(&P[0], e, 4);
        memcpy(&P[1], e + 4, 4);
        P[1] = (float)((double)DequantK1 * (double)P[1]);
        double offset = (double)(int)*(const uint16_t *)(e + 8) * scale * (double)DequantK1;
        P[2] = (float)((offset + offset) - scale);
        double deltaScale = (double)(int)*(const uint16_t *)(e + 10) * scale * (double)DequantK1;
        P[3] = (float)(deltaScale + deltaScale);
        P[3] = (float)((double)DequantK2 * (double)P[3]);
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
    return EvalF1Core(this, time, sqt, (const uint32_t *)m);
}

// FUNC_AT(0x000ff210)
void FnDeltaF::DestructF1() {
    vtable = VtDeltaF1;
    if (values != NULL) {
        FreeSized(valuesBlock);
        FreeSized(nextBlock);
        FreeSized(dequant);
    }
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x000ff1e0)
FnDeltaF* FnDeltaF::ScalarDeleteF1(unsigned flags) {
    DestructF1();
    if (flags & 1)
        EaglFree(this, 0x30);
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
    uint16_t *times = *(uint16_t **)(anim + 8);
    uint16_t keys = *(uint16_t *)(anim + 0xc);
    uint16_t n = times == NULL ? keys : (uint16_t)(times[keys - 2] + 1);
    *length = (float)(int)n;
    return true;
}

// ---- FnDeltaF3's object functions

// FUNC_AT(0x00100910)
void FnDeltaF::DestructF3() {
    vtable = VtDeltaF3;
    if (values != NULL) {
        FreeSized(valuesBlock);
        FreeSized(nextBlock);
        FreeSized(dequant);
    }
    FnAnimMemoryMap::Destruct();
}

// FUNC_AT(0x001008e0)
FnDeltaF* FnDeltaF::ScalarDeleteF3(unsigned flags) {
    DestructF3();
    if (flags & 1)
        EaglFree(this, 0x30);
    return this;
}

// ---- FnDeltaF3: the same over 3-vectors (values padded to 4 floats, 12 dequantisation floats a vector:
// min xyz, scale xyz, delta offset xyz, delta scale xyz). Its z delta is rounded to float before it is added.

#define DequantF3K1  (*(const float *)0x001a12d8u)
#define DequantF3K2  (*(const float *)0x001a12d0u)

// The constants after the blocks (their u16 output indexes, then their vectors on a 4-byte boundary).
// FUNC_AT(0x00100800)
uint8_t* DeltaF3Header::ConstantsPointer() {
    const DeltaF1Data *d = (const DeltaF1Data *)this;
    int n = d->count, shift = d->shift, keys = d->keys;
    int blockSize = ((((1 << shift) + 1) * n) * 3 + 1) & ~1;
    uintptr_t p = (uintptr_t)this + (uintptr_t)(blockSize * (keys >> shift)) + (uintptr_t)(n * 36) + 0x14;
    int rem = keys & (int)(0x7fffffffu >> (31 - shift));
    if (rem > 0)
        p = (p + (uintptr_t)(3 * (rem + 1) * n) + 1) & ~(uintptr_t)1;
    if (n == 0)
        p = (p + 1) & ~(uintptr_t)1;
    return (uint8_t *)p;
}

static bool EvalF3Core(FnDeltaF *self, float time, float *sqt, const uint32_t *mask) {
    const DeltaF1Data *d = (const DeltaF1Data *)self->anim;
    int n = d->count;
    int shift = d->shift;
    int frame = Truncate(time);
    int k = FindKey(frame, d, self->key);
    int unitMask = (int)(0x7fffffffu >> (31 - shift));
    int block = k >> shift, offset = k & unitMask;
    int cached = self->key;
    int cachedBlock = cached >> shift;
    int blockSize = ((((1 << shift) + 1) * n) * 3 + 1) & ~1;
    int stride = 3 * n;
    uint8_t *blocks = (uint8_t *)d + n * 36 + 0x14;
    uint8_t *bp = blocks + blockSize * block;
    const float *P = self->dequant;
    uint8_t bone[256];
    if (mask != NULL)
        for (int i = 0; i < n; i++)
            bone[i] = (uint8_t)(d->index[i] / 12);
#define SKIP(i) (mask != NULL && !InMask(mask, bone[i]))
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
            const uint16_t *base = (const uint16_t *)bp;
            for (int i = 0; i < n; i++) {
                if (SKIP(i))
                    continue;
                float *v = self->values + i * 4;
                const float *q = P + i * 12;
                for (int c = 0; c < 3; c++)
                    v[c] = (float)((double)(int)base[i * 3 + c] * (double)q[3 + c] + (double)q[c]);
            }
            start = 0;
        }
        if (start < offset) {
            const uint8_t *row = bp + (start + 2) * stride;
            for (int f = start; f < offset; f++, row += stride)
                for (int i = 0; i < n; i++) {
                    if (SKIP(i))
                        continue;
                    float *v = self->values + i * 4;
                    const float *q = P + i * 12;
                    double dx = (double)(int)row[i * 3] * (double)q[9] + (double)q[6];
                    double dy = (double)(int)row[i * 3 + 1] * (double)q[10] + (double)q[7];
                    float dz = (float)((double)(int)row[i * 3 + 2] * (double)q[11] + (double)q[8]);
                    v[0] = (float)(dx + (double)v[0]);
                    v[1] = (float)(dy + (double)v[1]);
                    v[2] = (float)((double)dz + (double)v[2]);
                }
        } else if (start > offset) {
            const uint8_t *row = bp + (start + 1) * stride;
            for (int f = start; f > offset; f--, row -= stride)
                for (int i = 0; i < n; i++) {
                    if (SKIP(i))
                        continue;
                    float *v = self->values + i * 4;
                    const float *q = P + i * 12;
                    double dx = (double)(int)row[i * 3] * (double)q[9] + (double)q[6];
                    double dy = (double)(int)row[i * 3 + 1] * (double)q[10] + (double)q[7];
                    float dz = (float)((double)(int)row[i * 3 + 2] * (double)q[11] + (double)q[8]);
                    v[0] = (float)((double)v[0] - dx);
                    v[1] = (float)((double)v[1] - dy);
                    v[2] = (float)((double)v[2] - (double)dz);
                }
        }
    }
    self->key = k;

    bool interpolate;
    float s = 1.0f;
    if (d->times == NULL) {
        double tf = (double)frame;
        interpolate = !((double)time == tf);
        if (interpolate)
            s = (float)((double)time - tf);
    } else if (k == 0) {
        interpolate = !((double)time == 0.0);
        if (interpolate)
            s = (float)((double)time / (double)(int)d->times[0]);
    } else {
        double p = (double)(int)d->times[k - 1];
        interpolate = !((double)time == p);
        if (interpolate)
            s = (float)(((double)time - p) / ((double)(int)d->times[k] - p));
    }
    const uint16_t *index = d->index;
    if (!interpolate || k >= d->keys - 1) {
        for (int i = 0; i < n; i++)
            if (!SKIP(i))
                for (int c = 0; c < 3; c++)
                    CopyBits(&sqt[index[i] + c], &self->values[i * 4 + c]);
    } else {
        int nb = (k + 1) >> shift;
        uint8_t *np = blocks + blockSize * nb;
        if (k + 1 == self->nextKey) {
            for (int i = 0; i < n; i++) {
                if (SKIP(i))
                    continue;
                float *v = self->values + i * 4, *w = self->next + i * 4;
                for (int c = 0; c < 3; c++)
                    sqt[index[i] + c] = (float)(((double)w[c] - (double)v[c]) * (double)s + (double)v[c]);
            }
        } else if (nb != block) {
            const uint16_t *base = (const uint16_t *)np;
            for (int i = 0; i < n; i++) {
                if (SKIP(i))
                    continue;
                float *v = self->values + i * 4, *w = self->next + i * 4;
                const float *q = P + i * 12;
                for (int c = 0; c < 3; c++)
                    w[c] = (float)((double)(int)base[i * 3 + c] * (double)q[3 + c] + (double)q[c]);
                for (int c = 0; c < 3; c++)
                    sqt[index[i] + c] = (float)(((double)w[c] - (double)v[c]) * (double)s + (double)v[c]);
            }
            self->nextKey = k + 1;
        } else {
            const uint8_t *row = np + (offset + 2) * stride;
            for (int i = 0; i < n; i++) {
                if (SKIP(i))
                    continue;
                float *v = self->values + i * 4, *w = self->next + i * 4;
                const float *q = P + i * 12;
                double dx = (double)(int)row[i * 3] * (double)q[9] + (double)q[6];
                double dy = (double)(int)row[i * 3 + 1] * (double)q[10] + (double)q[7];
                float dz = (float)((double)(int)row[i * 3 + 2] * (double)q[11] + (double)q[8]);
                w[0] = (float)(dx + (double)v[0]);
                w[1] = (float)(dy + (double)v[1]);
                w[2] = (float)((double)dz + (double)v[2]);
                sqt[index[i]] = (float)(dx * (double)s + (double)v[0]);
                sqt[index[i] + 1] = (float)(dy * (double)s + (double)v[1]);
                sqt[index[i] + 2] = (float)((double)dz * (double)s + (double)v[2]);
            }
            self->nextKey = k + 1;
        }
    }
#undef SKIP
    int constants = d->constants;
    if (constants > 0) {
        const uint16_t *ci = (const uint16_t *)((DeltaF3Header *)self->anim)->ConstantsPointer();
        uintptr_t p = (uintptr_t)((DeltaF3Header *)self->anim)->ConstantsPointer();
        const uint32_t *cv = (const uint32_t *)((p + (uintptr_t)constants * 2 + 3) & ~(uintptr_t)3);
        for (int c = 0; c < constants; c++, cv += 3)
            for (int j = 0; j < 3; j++)
                CopyBits(&sqt[ci[c] + j], &cv[j]);
    }
    return true;
}

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
void FnDeltaF::EvalF3(float previous, float time, float *out) {
    (void)previous;
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

// As the F1 dequantisation per component, x as (offset * scale) * K1, y and z as (offset * K1) * scale.
// FUNC_AT(0x000ffd20)
void FnDeltaF::InitBuffersF3() {
    uint8_t *d = anim;
    int n = *(uint16_t *)(d + 0xe);
    if (n == 0)
        return;
    values = valuesBlock = (float *)AnimPool_NewBlock((uint32_t)n * 16);
    next = nextBlock = (float *)AnimPool_NewBlock((uint32_t)n * 16);
    dequant = (float *)AnimPool_NewBlock((uint32_t)n * 48);
    const uint8_t *e = d + 0x14;
    double K1 = DequantF3K1, K2 = DequantF3K2;
    for (int i = 0; i < n; i++, e += 0x24) {
        float *P = dequant + i * 12;
        for (int c = 0; c < 3; c++) {
            double scale = (double)*(const float *)(e + 0xc + c * 4);
            double offset = (double)(int)*(const uint16_t *)(e + 0x18 + c * 2);
            double deltaScale = (double)(int)*(const uint16_t *)(e + 0x1e + c * 2);
            memcpy(&P[c], e + c * 4, 4);
            memcpy(&P[3 + c], e + 0xc + c * 4, 4);
            P[3 + c] = (float)(K1 * (double)P[3 + c]);
            double o = c == 0 ? offset * scale * K1 : offset * K1 * scale;
            P[6 + c] = (float)((o + o) - scale);
            double q = c == 0 ? deltaScale * scale * K1 : deltaScale * K1 * scale;
            P[9 + c] = (float)(q + q);
            P[9 + c] = (float)(K2 * (double)P[9 + c]);
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
    return EvalF3Core(this, time, sqt, (const uint32_t *)m);
}
