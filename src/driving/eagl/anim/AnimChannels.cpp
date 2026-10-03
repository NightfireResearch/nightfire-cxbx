#include "AnimChannels.h"
#include "AnimDecode.h"

#include <math.h>
#include <string.h>
#include <xmmintrin.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// The live channel types besides DeltaF1/F3 (docs/driving/eagl.md 3.4): every anim on the disc is a compound of
// raw event, DeltaQuat, KeyQuat, DeltaF3 and DeltaF1 channels. Each function is the original at the same address;
// x87 in double in the original's order with a float store per store, comparisons as the original's FCOMP flag
// tests decide them (unordered included), truncation by CVTTSS2SI.
// ---------------------------------------------------------------------------------------------------------------

static inline int Truncate(float f) {
    return _mm_cvtt_ss2si(_mm_set_ss(f));
}

// FUNC_AT(0x000fb350)
int AnimTruncate(float value) {
    return Truncate(value);
}

// ---- raw events: fired as time passes them; going back in time fires the rest and starts again

struct RawEvent {
    uint32_t id;
    float time;
    uint32_t data[2];
};

static inline void Fire(void **handlers, RawEvent *e, float time, void *data) {
    void *h = handlers[e->id];
    if (h != NULL)
        ((void (__fastcall *)(void *, int, float, RawEvent *, void *))(*(void ***)h)[0])(h, 0, time, e, data);
}

// FUNC_AT(0x000fb170)
void RawEventData::Eval(float previous, float time, int32_t *index, float *lastTime, void **handlers, void *data) {
    int count = *(int32_t *)((uint8_t *)this + 4);
    RawEvent *events = (RawEvent *)((uint8_t *)this + 8);
    double p = previous, t = time;
    if (p < (double)*lastTime) {   // went back: to the first event after 'previous'
        int i = *index;
        if (i >= 0) {
            while (!((double)events[i].time < p || (double)events[i].time == p)) {
                if (--i < 0)
                    break;
            }
        }
        *index = i + 1;
    }
    int j = *index;
    while (j < count && !((double)events[j].time > p))
        j++;
    *index = j;
    if (p == t) {
        if (j >= count) {
            j = count - 1;
            *index = j;
        }
        int i = *index;
        if (i >= 0) {
            while (!((double)events[i].time < t)) {
                if (--i < 0)
                    break;
            }
        }
        *index = i + 1;
    } else if (t < p) {   // wrapped: the events after 'previous', then from the start
        for (int k = j; k < count; k++)
            Fire(handlers, &events[k], time, data);
        *index = 0;
    }
    j = *index;
    while (j < count && ((double)events[j].time < t || (double)events[j].time == t)) {
        Fire(handlers, &events[j], time, data);
        j++;
    }
    *index = j;
    if (j >= count)
        *index = count - 1;
    *lastTime = time;
}

// ---- DeltaQuat: u16 type, u16 checksum, DeltaCompressedData* at +4, u16 frames at +8, u16 index[] at +0xa

#define DeltaInfo(anim) (*(DeltaCompressedData **)((anim) + 4))
#define InfoCount(info) (*(uint16_t *)(info))

// The frame below the time decoded; between frames, one more frame's deltas scaled by the fraction (a lerp).
static void DeltaQuatEval(FnDeltaChan *c, float time, float *out) {
    int t0 = Truncate(time);
    uint8_t *a = c->anim;
    DeltaCompressedData *info = DeltaInfo(a);
    int frames = *(uint16_t *)(a + 8);
    int f = t0;
    if (t0 >= frames)
        f = frames - 1;
    else if (t0 < 0)
        f = 0;
    info->DecompressValues(0, InfoCount(info), c->frame, f, c->values, c->values);
    c->frame = f;
    float tf = (float)t0;
    a = c->anim;
    info = DeltaInfo(a);
    int count = InfoCount(info);
    uint16_t *index = (uint16_t *)(a + 0xa);
    if (!((double)time == (double)tf) && t0 + 1 < (int)*(uint16_t *)(a + 8)) {
        float s = (float)((double)time - (double)tf);
        info->DecompressValuesIndexed(0, count, t0, t0 + 1, c->values, out, 4, index, s);
        return;
    }
    for (int i = 0; i < count; i += 4)
        memcpy(out + index[i / 4], c->values + i, 16);
}

// FUNC_AT(0x000fb670)
void FnDeltaChan::EvalQuat(float previous, float time, float *out) {
    (void)previous;
    DeltaQuatEval(this, time, out);
}

// FUNC_AT(0x000fb780)
bool FnDeltaChan::EvalSQTQuat(float time, float *sqt, void *mask) {
    (void)mask;
    DeltaQuatEval(this, time, sqt);
    return true;
}

// ---- KeyQuat: u16 type, u16 checksum, DeltaCompressedData* at +4, u16 key times* at +8, u16 keys at +0xc,
// u16 index[] at +0xe

#define KeyTimes(anim) (*(uint16_t **)((anim) + 8))
#define KeyCount(anim) (*(uint16_t *)((anim) + 0xc))

// The key after the time (0 before the first), searched from the last one used.
// FUNC_AT(0x000fb960)
int FnKeyDeltaChan::FindLowerKey(float time) {
    int t = Truncate(time);
    uint16_t *times = KeyTimes(anim);
    int last = KeyCount(anim) - 2;
    double first = (double)(int)times[0];
    if (!(first < (double)time || first == (double)time || time != time))
        return 0;
    int i = key >= 1 ? key - 1 : 0;
    if ((int)times[i] > t) {
        while (i > 0 && (int)times[i] > t)
            i--;
        return i + 1;
    }
    if (i >= last)
        return i + 1;
    for (;;) {
        if ((int)times[i + 1] > t)
            return i + 1;
        if (++i >= last)
            return i + 1;
    }
}

// FUNC_AT(0x000fbe10)
bool FnKeyDeltaChan::GetLength(float *length) {
    *length = (float)((int)KeyTimes(anim)[KeyCount(anim) - 2] + 1);
    return true;
}

// FUNC_AT(0x000fbb40)
void FnKeyDeltaChan::EvalQuat(float previous, float time, float *out) {
    (void)previous;
    AnimVCall<bool>(this, kSlotEvalSQT, time, out, (void *)NULL);
}

// The key below the time decoded; between keys each quaternion slerps to the next key's (decoded into a
// temporary). On a key, after the last key or before 0, a copy.
// FUNC_AT(0x000fbb60)
bool FnKeyDeltaChan::EvalSQTQuat(float time, float *sqt, void *mask) {
    (void)mask;
    uint8_t *a = anim;
    int keys = KeyCount(a);
    DeltaCompressedData *info = DeltaInfo(a);
    int count = InfoCount(info);
    uint16_t *times = KeyTimes(a);
    uint16_t *index = (uint16_t *)(a + 0xe);
    int k = FindLowerKey(time);
    info = DeltaInfo(anim);
    info->DecompressValues(0, InfoCount(info), key, k, values, values);
    key = k;
    int previousTime = k != 0 ? times[k - 1] : 0;
    float pf = (float)previousTime;
    bool copy = (double)pf == (double)time;
    if (!copy && k == keys - 1 && (double)(int)times[keys - 2] < (double)time)
        copy = true;
    if (!copy && k == 0 && (double)time < 0.0)
        copy = true;
    if (copy) {
        for (int i = 0; i < count; i += 4)
            memcpy(sqt + index[i / 4], values + i, 16);
        return true;
    }
    int span = (int)times[k] - previousTime;
    float s = (float)(((double)time - (double)pf) / (double)span);
    for (int i = 0; i < count; i += 4) {
        float next[4];
        info->DecompressValues(i, 4, k, k + 1, values + i, next);
        EAGL_VU0_fastqslerp(s, values + i, next, sqt + index[i / 4]);
    }
    return true;
}

// The shorter way round (a negative dot flips the second), then normalised.
// FUNC_AT(0x000fbd10)
void EAGL_VU0_fastqslerp(float t, const float *q0, const float *q1, float *out) {
    double dot = (double)q0[0] * (double)q1[0];
    dot = dot + (double)q0[2] * (double)q1[2];
    dot = dot + (double)q0[1] * (double)q1[1];
    dot = dot + (double)q1[3] * (double)q0[3];
    if (dot > 0.0) {
        for (int i = 0; i < 4; i++)
            out[i] = (float)(((double)q1[i] - (double)q0[i]) * (double)t + (double)q0[i]);
    } else {
        for (int i = 0; i < 3; i++)
            out[i] = (float)((double)q0[i] - ((double)q0[i] + (double)q1[i]) * (double)t);
        out[3] = (float)((double)q0[3] - ((double)q1[3] + (double)q0[3]) * (double)t);
    }
    double x = out[0], y = out[1], z = out[2], w = out[3];
    double sum = x * x + y * y;
    sum = sum + z * z;
    sum = sum + w * w;
    double inv = 1.0 / sqrt(sum);
    for (int i = 0; i < 4; i++)
        out[i] = (float)(inv * (double)out[i]);
}
