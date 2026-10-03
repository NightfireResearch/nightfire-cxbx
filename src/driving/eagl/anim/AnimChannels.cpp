#include "AnimChannels.h"
#include "AnimDecode.h"
#include "../../platform/X87.h"

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
// x87 in double in the original's order with a float store per store (a single operation on floats written in
// float: the same bits), comparisons as the original's FCOMP flag tests decide them (unordered included, a negated
// comparison where an unordered result must count as true), truncation by CVTTSS2SI.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000fb350)
int AnimTruncate(float value) {
    return Truncate(value);
}

// ---- raw events: fired as time passes them; going back in time fires the rest and starts again

// A handler's slot 0 gets (time, the event, data).
static inline void Fire(void **handlers, RawEvent *e, float time, void *data) {
    void *h = handlers[e->id];
    if (h != NULL)
        AnimVCall<void>(h, 0, time, e, data);
}

// FUNC_AT(0x000fb170)
void RawEventData::Eval(float previous, float time, int32_t *index, float *lastTime, void **handlers, void *data) {
    if (previous < *lastTime) {   // went back: to the first event after 'previous'
        int i = *index;
        if (i >= 0) {
            while (!(events[i].time <= previous)) {
                if (--i < 0)
                    break;
            }
        }
        *index = i + 1;
    }
    int j = *index;
    while (j < count && !(events[j].time > previous))
        j++;
    *index = j;
    if (previous == time) {
        if (j >= count) {
            j = count - 1;
            *index = j;
        }
        int i = *index;
        if (i >= 0) {
            while (!(events[i].time < time)) {
                if (--i < 0)
                    break;
            }
        }
        *index = i + 1;
    } else if (time < previous) {   // wrapped: the events after 'previous', then from the start
        for (int k = j; k < count; k++)
            Fire(handlers, &events[k], time, data);
        *index = 0;
    }
    j = *index;
    while (j < count && events[j].time <= time) {
        Fire(handlers, &events[j], time, data);
        j++;
    }
    *index = j;
    if (j >= count)
        *index = count - 1;
    *lastTime = time;
}

// ---- DeltaQuat

// The frame below the time decoded; between frames, one more frame's deltas scaled by the fraction (a lerp).
static void DeltaQuatEval(FnDeltaChan *c, float time, float *out) {
    int t0 = Truncate(time);
    DeltaChanData *data = reinterpret_cast<DeltaChanData *>(c->anim);
    DeltaCompressedData *info = data->info;
    int frames = data->frames;
    int f = t0;
    if (t0 >= frames)
        f = frames - 1;
    else if (t0 < 0)
        f = 0;
    info->DecompressValues(0, info->count, c->frame, f, c->values, c->values);
    c->frame = f;
    float tf = float(t0);
    int count = info->count;
    if (time != tf && t0 + 1 < data->frames) {   // !(==): unordered counts as between frames
        float s = time - tf;
        info->DecompressValuesIndexed(0, count, t0, t0 + 1, c->values, out, 4, data->index, s);
        return;
    }
    for (int i = 0; i < count; i += 4)
        memcpy(out + data->index[i / 4], c->values + i, 4 * sizeof(float));
}

// FUNC_AT(0x000fb670)
void FnDeltaChan::EvalQuat(float, float time, float *out) {
    DeltaQuatEval(this, time, out);
}

// FUNC_AT(0x000fb780)
bool FnDeltaChan::EvalSQTQuat(float time, float *sqt, void *) {
    DeltaQuatEval(this, time, sqt);
    return true;
}

// ---- KeyQuat

static inline KeyChanData *Keyed(uint8_t *anim) {
    return reinterpret_cast<KeyChanData *>(anim);
}

// The key after the time (0 before the first), searched from the last one used.
// FUNC_AT(0x000fb960)
int FnKeyDeltaChan::FindLowerKey(float time) {
    int t = Truncate(time);
    uint16_t *times = Keyed(anim)->times;
    int last = Keyed(anim)->keys - 2;
    if (times[0] > time)
        return 0;
    int i = key >= 1 ? key - 1 : 0;
    if (times[i] > t) {
        while (i > 0 && times[i] > t)
            i--;
        return i + 1;
    }
    if (i >= last)
        return i + 1;
    for (;;) {
        if (times[i + 1] > t)
            return i + 1;
        if (++i >= last)
            return i + 1;
    }
}

// FUNC_AT(0x000fbe10)
bool FnKeyDeltaChan::GetLength(float *length) {
    KeyChanData *data = Keyed(anim);
    *length = float(data->times[data->keys - 2] + 1);
    return true;
}

// FUNC_AT(0x000fbb40)
void FnKeyDeltaChan::EvalQuat(float, float time, float *out) {
    AnimVCall<bool>(this, kSlotEvalSQT, time, out, (void *)NULL);
}

// The key below the time decoded; between keys each quaternion slerps to the next key's (decoded into a
// temporary). On a key, after the last key or before 0, a copy.
// FUNC_AT(0x000fbb60)
bool FnKeyDeltaChan::EvalSQTQuat(float time, float *sqt, void *) {
    KeyChanData *data = Keyed(anim);
    int keys = data->keys;
    DeltaCompressedData *info = data->info;
    int count = info->count;
    uint16_t *times = data->times;
    int k = FindLowerKey(time);
    info->DecompressValues(0, info->count, key, k, values, values);
    key = k;
    int previousTime = k != 0 ? times[k - 1] : 0;
    float pf = float(previousTime);
    bool copy = pf == time;
    if (!copy && k == keys - 1 && times[keys - 2] < time)
        copy = true;
    if (!copy && k == 0 && time < 0.0f)
        copy = true;
    if (copy) {
        for (int i = 0; i < count; i += 4)
            memcpy(sqt + data->index[i / 4], values + i, 4 * sizeof(float));
        return true;
    }
    int span = times[k] - previousTime;
    float s = float((double(time) - pf) / span);
    for (int i = 0; i < count; i += 4) {
        float next[4];
        info->DecompressValues(i, 4, k, k + 1, values + i, next);
        EAGL_VU0_fastqslerp(s, values + i, next, sqt + data->index[i / 4]);
    }
    return true;
}

// The shorter way round (a negative dot flips the second), then normalised. 'out' may be q1 (or q0): each
// component is read before it is written.
// FUNC_AT(0x000fbd10)
void EAGL_VU0_fastqslerp(float t, const float *q0, const float *q1, float *out) {
    double dot = double(q0[0]) * q1[0] + double(q0[2]) * q1[2] + double(q0[1]) * q1[1] + double(q1[3]) * q0[3];
    if (dot > 0.0) {
        for (int i = 0; i < 4; i++)
            out[i] = float((double(q1[i]) - q0[i]) * t + q0[i]);
    } else {
        for (int i = 0; i < 3; i++)
            out[i] = float(q0[i] - (double(q0[i]) + q1[i]) * t);
        out[3] = float(q0[3] - (double(q1[3]) + q0[3]) * t);
    }
    double x = out[0], y = out[1], z = out[2], w = out[3];
    double inverse = 1.0 / sqrt(x * x + y * y + z * z + w * w);
    for (int i = 0; i < 4; i++)
        out[i] = float(inverse * out[i]);
}
