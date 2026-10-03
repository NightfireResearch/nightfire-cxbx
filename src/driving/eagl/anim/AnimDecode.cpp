#include "AnimDecode.h"

#include <string.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// EAGLAnim's shared helpers (docs/driving/eagl.md 3.4, 4.12): delta-compressed values, scratch buffers, attribute
// blocks. Each function is the original at the same address. Values decode frame by frame from a start (the dof's
// start value at key 0, or the previous key's values) by adding q * range per frame, where q is a 4, 8 or 16-bit
// unsigned delta: the plain form as (q * range + current) + min, the indexed form as ((q * range) + min) * scale +
// current - the original's orders, kept (MW's source has another). x87 in double, a float store per store.
// ---------------------------------------------------------------------------------------------------------------

#define EaglMalloc   (*(void *(**)(uint32_t size, const char *name))0x001caf68u)
#define EaglFree     (*(void (**)(void *data, uint32_t size))0x001caf6cu)
#define Scratch      ((ScratchBuffer *)0x00241ba0u)

#define DeltaCount(d)   (*(uint16_t *)(d))
#define DeltaBits(d)    (*((uint8_t *)(d) + 2))
#define DeltaDofs(d)    ((DofInfo *)((uint8_t *)(d) + 4))
#define DeltaStream(d)  ((uint8_t *)(d) + 4 + DeltaCount(d) * 12)

static inline float Step(float current, int q, const DofInfo &dof) {
    return (float)(((double)q * (double)dof.range + (double)current) + (double)dof.min);
}

static inline float StepScaled(float current, int q, const DofInfo &dof, float scale) {
    return (float)(((double)q * (double)dof.range + (double)dof.min) * (double)scale + (double)current);
}

// From the previous key's values ('source' copied to 'out') when going forward, otherwise from the start values;
// then a frame of deltas per key up to 'key'. The deltas are read 'count' at a time from where 'first' starts in
// the first frame, each frame following on (the callers decode whole frames).
// FUNC_AT(0x001069d0)
void DeltaCompressedData::DecompressValues(int first, int count, int previousKey, int key, float *source,
                                           float *out) {
    if (key == previousKey)
        return;
    DofInfo *dofs = DeltaDofs(this) + first;
    int from;
    if (key >= previousKey && key != 0 && previousKey != -1) {
        if (out != source)
            memcpy(out, source, (size_t)count * 4);
        from = previousKey;
    } else {
        for (int i = 0; i < count; i++)
            out[i] = dofs[i].start;
        from = 0;
    }
    if (key == 0)
        return;
    int n = DeltaCount(this);
    uint8_t *stream = DeltaStream(this);
    switch (DeltaBits(this)) {
    case 16: {
        const uint16_t *q = (const uint16_t *)stream + (n * from + first);
        for (int f = from; f < key; f++)
            for (int i = 0; i < count; i++)
                out[i] = Step(out[i], *q++, dofs[i]);
        break;
    }
    case 8: {
        const uint8_t *q = stream + (n * from + first);
        for (int f = from; f < key; f++)
            for (int i = 0; i < count; i++)
                out[i] = Step(out[i], *q++, dofs[i]);
        break;
    }
    case 4: {
        const uint8_t *q = stream + ((n + 1) >> 1) * from + first;
        int last = count - 1;
        if ((first & 1) == 0) {
            // low nibble first; an odd count leaves the last byte's high nibble unused
            for (int f = from; f < key; f++) {
                int i = 0;
                if (last > 0) {
                    do {
                        uint8_t b = *q++;
                        out[i] = Step(out[i], b & 0xf, dofs[i]);
                        out[i + 1] = Step(out[i + 1], b >> 4, dofs[i + 1]);
                        i += 2;
                    } while (i < last);
                }
                if (count & 1) {
                    uint8_t b = *q++;
                    out[i] = Step(out[i], b & 0xf, dofs[i]);
                }
            }
        } else {
            // an odd first value starts in a high nibble (and a single value decodes nothing, as in the original)
            for (int f = from; f < key; f++) {
                int i = 0;
                if (last > 0) {
                    uint8_t b = *q++;
                    out[0] = Step(out[0], b >> 4, dofs[0]);
                    i = 1;
                    if (last > 1) {
                        do {
                            b = *q++;
                            out[i] = Step(out[i], b & 0xf, dofs[i]);
                            out[i + 1] = Step(out[i + 1], b >> 4, dofs[i + 1]);
                            i += 2;
                        } while (i < last);
                    }
                }
                if (last & 1) {
                    uint8_t b = *q++;
                    out[i] = Step(out[i], b & 0xf, dofs[i]);
                }
            }
        }
        break;
    }
    }
}

// The same for values scattered in groups: group g's 'groupSize' values go to out[index[g]...], the deltas scaled
// (an interpolation toward the next key). Here the stream starts at the frame, not at 'first'.
// FUNC_AT(0x00106df0)
void DeltaCompressedData::DecompressValuesIndexed(int first, int count, int previousKey, int key, float *source,
                                                  float *out, int groupSize, uint16_t *index, float scale) {
    if (key == previousKey)
        return;
    DofInfo *dofs = DeltaDofs(this) + first;
    int groups = count / groupSize;
    int from;
    if (key != 0 && previousKey != -1 && key >= previousKey) {
        int s = 0;
        for (int g = 0; g < groups; g++)
            for (int j = 0; j < groupSize; j++)
                out[index[g] + j] = source[s++];
        from = previousKey;
    } else {
        DofInfo *d = dofs;
        for (int g = 0; g < groups; g++)
            for (int j = 0; j < groupSize; j++)
                out[index[g] + j] = (d++)->start;
        from = 0;
    }
    if (key == 0)
        return;
    int n = DeltaCount(this);
    uint8_t *stream = DeltaStream(this);
    switch (DeltaBits(this)) {
    case 16: {
        const uint16_t *q = (const uint16_t *)stream + n * from;
        for (int f = from; f < key; f++) {
            DofInfo *d = dofs;
            for (int g = 0; g < groups; g++)
                for (int j = 0; j < groupSize; j++, d++)
                    out[index[g] + j] = StepScaled(out[index[g] + j], *q++, *d, scale);
        }
        break;
    }
    case 8: {
        const uint8_t *q = stream + n * from;
        for (int f = from; f < key; f++) {
            DofInfo *d = dofs;
            for (int g = 0; g < groups; g++)
                for (int j = 0; j < groupSize; j++, d++)
                    out[index[g] + j] = StepScaled(out[index[g] + j], *q++, *d, scale);
        }
        break;
    }
    case 4: {
        const uint8_t *q = stream + ((n + 1) / 2) * from;
        bool low = (first & ~1) == 0;   // as the original decides it
        for (int f = from; f < key; f++) {
            DofInfo *d = dofs;
            for (int g = 0; g < groups; g++) {
                for (int j = 0; j < groupSize; j++, d++) {
                    int v;
                    if (low) {
                        v = *q & 0xf;
                    } else {
                        v = *q >> 4;
                        q++;
                    }
                    low = !low;
                    out[index[g] + j] = StepScaled(out[index[g] + j], v, *d, scale);
                }
            }
            if (!low) {   // each frame starts on a byte
                q++;
                low = true;
            }
        }
        break;
    }
    }
}

// One value at a key, from its value at the previous key.
// FUNC_AT(0x00107370)
double DeltaCompressedData::DecompressValue(int value, int previousKey, int key, float previous) {
    float result;   // the original decodes into its 'key' argument's slot: unchanged (the key's bits) if no step
    memcpy(&result, &key, 4);
    DecompressValues(value, 1, previousKey, key, &previous, &result);
    return (double)result;
}

// ---- ScratchBuffer

// FUNC_AT(0x00106710)
void ScratchBuffer::AllocateBuffer(uint32_t bytes) {
    users++;
    if (bytes <= size)
        return;
    if (buffer != NULL)
        EaglFree(buffer, size);
    size = bytes;
    buffer = EaglMalloc(bytes, (const char *)0x001cec80u);   // "ScratchBuffer::mBuffer"
}

// FUNC_AT(0x00106750)
void ScratchBuffer::FreeBuffer() {
    users--;
    if (buffer != NULL && users <= 0) {
        EaglFree(buffer, size);
        buffer = NULL;
    }
}

// FUNC_AT(0x00106780)
ScratchBuffer* ScratchBuffer_GetScratchBuffer(int index) {
    return &Scratch[index];
}

// (The pointers are not cleared.)
// FUNC_AT(0x00106790)
void ScratchBuffer_FreeScratchBuffers() {
    for (int i = 0; i < 3; i++)
        if (Scratch[i].buffer != NULL)
            EaglFree(Scratch[i].buffer, Scratch[i].size);
}

// ---- AttributeBlock

// FUNC_AT(0x001067c0)
AttributeEntry* AttributeBlock::FindAttribute(uint16_t id) {
    int lo = 0, hi = *(int32_t *)this - 1;
    AttributeEntry *entries = (AttributeEntry *)((uint8_t *)this + 4);
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (id > entries[mid].id)
            lo = mid + 1;
        else if (id < entries[mid].id)
            hi = mid - 1;
        else
            return &entries[mid];
    }
    return NULL;
}

// FUNC_AT(0x00106800)
bool AttributeBlock::GetAttribute(uint16_t id, uint32_t *out) {
    AttributeEntry *e = FindAttribute(id);
    if (e == NULL)
        return false;
    *out = e->value;
    return true;
}

static bool Copy(AttributeBlock *block, uint16_t id, void *out) {
    AttributeEntry *e = block->FindAttribute(id);
    if (e == NULL)
        return false;
    memcpy(out, (const void *)(uintptr_t)e->value, e->size);
    return true;
}

// FUNC_AT(0x00106830)
bool AttributeBlock::CopyAttribute(uint16_t id, void *out) {
    return Copy(this, id, out);
}

// FUNC_AT(0x00106880)
bool AttributeBlock::CopyAttribute2(uint16_t id, void *out) {
    return Copy(this, id, out);
}

// Up to four bytes are the value itself; more are behind the pointer.
// FUNC_AT(0x001068d0)
bool AttributeBlock::GetAttributeValue(uint16_t id, void *out) {
    AttributeEntry *e = FindAttribute(id);
    if (e == NULL)
        return false;
    if (e->size > 4)
        memcpy(out, (const void *)(uintptr_t)e->value, e->size);
    else
        *(uint32_t *)out = e->value;
    return true;
}

// FUNC_AT(0x00106930)
bool AttributeBlock::CopyAttribute3(uint16_t id, void *out) {
    return Copy(this, id, out);
}

// FUNC_AT(0x00106980)
bool AttributeBlock::CopyAttribute4(uint16_t id, void *out) {
    return Copy(this, id, out);
}
