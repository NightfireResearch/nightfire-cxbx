#include "Reverb.h"

#include <string.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// The mixer's software reverb (docs/driving/sound.md 3.6, 4.7), on the FX send sndfx (0x00245dd0, 512 floats).
//
// Standard (SNDDRV_setfx -> MIX_initreverb with a description whose byte 2 is 0): up to three groups of
// allpass taps with prime delay lengths (findprime of rate x milliseconds); the hook SNDMIXI_fxadd runs them
// over the send each slice - group 0 in place on sndfx, groups 1 and 2 through the two buffers after it - and
// adds the result to the five speakers (not the LFE). A description byte 2 of anything but 0 and 10 builds the
// taps but installs no hook.
//
// fx2 (byte 2 = 10): the description is a node network per output configuration - node entries (type, route,
// big-endian parameters) and connections - built by SNDMIXI_initfx2/initfx from the building blocks below and
// the filters of module H; the never-made hook 0x001437e0 pulls each routed node and adds it to its speakers.
//
// Both hooks stop working after 3000 slices with nothing sent (0x002475d0, reset by MIX_audioslice).
// x87 code: double arithmetic in the original's order, a float store per FSTP; values the original keeps on the
// x87 stack stay double, values it stores and reloads are rounded.
// ---------------------------------------------------------------------------------------------------------------

namespace {

using SND::MixFilterNode;
using SND::ReverbTap;

inline uint8_t &U8(uint32_t address) { return *(uint8_t *)(uintptr_t)address; }
inline uint32_t &U32(uint32_t address) { return *(uint32_t *)(uintptr_t)address; }
inline double K(uint32_t address) { return (double)*(const float *)(uintptr_t)address; }

const uint32_t kNumChannels = 0x00245995;        // u8
const uint32_t kReverbState = 0x0024599f;        // u8: 0 off, 1 fx2, 2 standard
const uint32_t kFxInitFunc = 0x002459a0;
const uint32_t kFxHook = 0x002459a4;             // unpackerInitFuncs[0]
const uint32_t kScratchA = 0x00245ab8;
const uint32_t kScratchB = 0x00245abc;
const uint32_t kAccum = 0x00245ad8;              // [6]
const uint32_t kMixFunc = 0x00245be8;
const uint32_t kTapCounts = 0x00245bf0;          // [3]
const uint32_t kFx2Connections = 0x00245bfc;     // the selected configuration's connection entries
const uint32_t kTaps = 0x00245c00;               // [11] x 0x24
const uint32_t kTapsEnd = 0x00245d98;
const uint32_t kFx2Entries = 0x00245d8c;         // the selected configuration's node entries
const uint32_t kFx2Header = 0x00245d90;          // the description's first 14 dwords, copied
const uint32_t kFx2NodeCounts = 0x00245d98;      // [6] big-endian, per configuration (in the copy)
const uint32_t kFx2ConnCounts = 0x00245db0;      // [6]
const uint32_t kFx2Active = 0x00245dc8;
const uint32_t kFxSend = 0x00245dd0;             // sndfx
const uint32_t kFxGroup1 = 0x002465d0;           // 512 floats each
const uint32_t kFxGroup2 = 0x00246dd0;
const uint32_t kFxIdle = 0x002475d0;
const uint32_t kFx2Nodes = 0x002475d4;           // node pointer array
const uint32_t kFx2Desc = 0x002475d8;            // = kFx2Header
const uint32_t kFx2Routing = 0x002475dc;         // [6] node pointers by route
const uint32_t kFx2Source = 0x002475e0;          // index of the SOURCE node
const uint32_t kFx2Config = 0x002475e4;          // configuration + 1
const uint32_t kFx2ConnCursor = 0x002475e8;
const uint32_t kFx2EntryCursor = 0x002475ec;
const uint32_t kFx2ConfigCopy = 0x002475f0;

const uint32_t kNodeSizes = 0x001d9e88;          // [10] by node type
const uint32_t kEntrySizes = 0x001da498;         // [10] bytes per node entry
const uint32_t kRouteTable = 0x001d9e85;
const uint32_t kMixRows = 0x001d9f88;

const uint32_t kOne = 0x00189de8;                // 1.0f
const uint32_t kZero = 0x00189dec;               // 0.0f
const uint32_t kOver127 = 0x001a296c;            // 1/127
const uint32_t kOver256 = 0x00190274;            // 1/256
const uint32_t kDenormalGuard = 0x001a7564;      // 1e-20
const uint32_t kReverbGuard = 0x001da4c0;        // 1e-30

inline uint32_t NumChannels() { return U8(kNumChannels); }
inline float *Accum(uint32_t i) { return (float *)(uintptr_t)U32(kAccum + i * 4); }
inline MixFilterNode **Nodes() { return (MixFilterNode **)(uintptr_t)U32(kFx2Nodes); }
inline MixFilterNode **Routing() { return (MixFilterNode **)(uintptr_t)U32(kFx2Routing); }
inline const uint8_t *Desc() { return (const uint8_t *)(uintptr_t)U32(kFx2Desc); }
inline uint32_t EntrySize(uint8_t type) { return U32(kEntrySizes + type * 4u); }
inline void MixCall(int count, float gain, const float *in, float *out) {
    ((void (*)(int, float, const float *, float *))(uintptr_t)U32(kMixFunc))(count, gain, in, out);
}

inline void *Alloc(int bytes) { return ((void *(*)(int))0x0013f780)(bytes); }            // SNDMEMI_alloc
inline void Free(void *block) { ((void (*)(void *))0x0013f880)(block); }                  // SNDMEMI_free
inline void MemClear(void *p, int bytes) { ((void (*)(void *, int))0x0013f600)(p, bytes); }   // memclr
inline int GetB(const void *p, int bytes) { return ((int (*)(const void *, int))0x00144a90)(p, bytes); }   // SNDI_getb

// Module H
inline void CreateLPFRC(void *node) { ((int (*)(void *))0x00143710)(node); }
inline void ModifyLPFRC(void *node, int *params) { ((void (*)(void *, int *))0x00143740)(node, params); }
inline void CreateHPFFIR8(void *node) { ((int (*)(void *))0x001454d0)(node); }
inline void ModifyHPFFIR8(void *node, int *params) { ((void (*)(void *, int *))0x001455b0)(node, params); }
inline void CreateSource(void *node) { ((int (*)(void *))0x00145700)(node); }
inline void InitSource(void *node, float *buffer) { ((void (*)(void *, float *))0x001456e0)(node, buffer); }
inline void Connect(void *from, void *to, int output, int input) {
    ((int (*)(void *, void *, int, int))0x001444f0)(from, to, output, input);   // SFILTER_connect
}
inline void FirClear(void *state) { ((void (*)(void *))0x001466c0)(state); }
inline void FirRun(void *state, int frames, float *in, float *out) {
    ((void (*)(void *, int, float *, float *))0x001466e0)(state, frames, in, out);
}
inline void FirDesign(void *state, int kind) { ((void (*)(void *, int))0x00146930)(state, kind); }

inline float &NodeF32(void *node, uint32_t offset) { return *(float *)((uint8_t *)node + offset); }

// REP MOVSD then REP MOVSB, forwards
inline void ForwardCopy(void *to, const void *from, uint32_t bytes) {
    uint32_t *d = (uint32_t *)to;
    const uint32_t *s = (const uint32_t *)from;
    for (uint32_t n = bytes >> 2; n != 0; n--)
        *d++ = *s++;
    uint8_t *db = (uint8_t *)d;
    const uint8_t *sb = (const uint8_t *)s;
    for (uint32_t n = bytes & 3; n != 0; n--)
        *db++ = *sb++;
}

}  // namespace

// FUNC_AT(0x00142ee0)
void SNDMIXI_restorefx2(void) {
    int count = GetB(Desc() + U32(kFx2ConfigCopy) * 4 + 4, 4);
    if (count != 0) {
        for (int i = 0; i < count; i++) {
            MixFilterNode *node = Nodes()[i];
            if (node->restore != NULL)
                node->restore(node);
            if (Nodes()[i] != NULL) {
                Free(Nodes()[i]);
                Nodes()[i] = NULL;
            }
        }
    }
    if (U32(kFx2Nodes) != 0) {
        Free((void *)(uintptr_t)U32(kFx2Nodes));
        U32(kFx2Nodes) = 0;
    }
    uint32_t routing = U32(kFx2Routing);
    for (uint32_t offset = 0; offset < 0x18; offset += 4) {
        if (U32(routing + offset) != 0) {
            U32(routing + offset) = 0;
            routing = U32(kFx2Routing);
        }
    }
    if (routing != 0) {
        Free((void *)(uintptr_t)routing);
        U32(kFx2Routing) = 0;
    }
    U32(kFx2Entries) = 0;
    U32(kFx2Connections) = 0;
    U32(kFx2ConnCursor) = 0;
    U32(kFx2EntryCursor) = 0;
    U32(kFx2Active) = 0;
}

// Builds the selected configuration's nodes and connections. 'rate' is the platform rate.
// FUNC_AT(0x00142fc0)
void SNDMIXI_initfx(int rate) {
    U32(kFx2Desc) = kFx2Header;
    void *routing = Alloc(0x18);
    int config = (int)U32(kFx2Config) - 1;
    U32(kFx2Routing) = (uint32_t)(uintptr_t)routing;
    U32(kFx2EntryCursor) = U32(kFx2Entries);
    U32(kFx2ConnCursor) = U32(kFx2Connections);
    int nodeCount = GetB(Desc() + config * 4 + 8, 4);
    void *nodes = Alloc(nodeCount * 4);
    const uint8_t *entry = (const uint8_t *)(uintptr_t)U32(kFx2EntryCursor);
    U32(kFx2Nodes) = (uint32_t)(uintptr_t)nodes;
    for (int i = 0; i < nodeCount; i++) {
        Nodes()[i] = (MixFilterNode *)Alloc((int)U32(kNodeSizes + entry[0] * 4u));
        int params[4];
        MemClear(params, 0x10);
        void *node = Nodes()[i];
        switch (entry[0]) {
        case 0:   // the source: reads sndfx
            CreateSource(node);
            break;
        case 1:   // RC low pass: cutoff, rate, the third design word
            params[0] = GetB(entry + 4, 2) << 8;
            params[1] = rate << 8;
            params[2] = GetB(entry + 6, 2);
            CreateLPFRC(node);
            ModifyLPFRC(node, params);
            break;
        case 2:
            params[0] = GetB(entry + 2, 2) << 8;
            params[1] = rate << 8;
            FUN_00145640((MixFilterNode *)node);
            FUN_00145670((MixFilterNode *)node, params);
            break;
        case 3:   // FIR high pass
            params[0] = GetB(entry + 2, 2) << 8;
            params[1] = rate << 8;
            CreateHPFFIR8(node);
            ModifyHPFFIR8(node, params);
            break;
        case 4:   // the FIR with two corners
            params[0] = GetB(entry + 4, 2) << 8;
            params[1] = GetB(entry + 6, 2) << 8;
            params[2] = rate << 8;
            CreateHPFFIR8(node);
            FUN_00145500((MixFilterNode *)node, params);
            break;
        case 5:   // resonator: frequency, rate, bandwidth, gain (x 256)
            params[0] = GetB(entry + 2, 2) << 8;
            params[1] = rate << 8;
            params[2] = GetB(entry + 4, 2) << 8;
            params[3] = GetB(entry + 6, 2);
            FUN_001453e0((SND::FxResonatorNode *)node);
            FUN_00145410((SND::FxResonatorNode *)node, params);
            break;
        case 6:   // gain (x 256)
            params[0] = GetB(entry + 2, 2);
            FUN_00145160((SND::FxGainNode *)node);
            FUN_00145190((SND::FxGainNode *)node, params);
            break;
        case 7:   // allpass: gain (x 254), rate, milliseconds
            params[0] = GetB(entry + 2, 2);
            params[1] = rate << 8;
            params[2] = GetB(entry + 4, 2) << 8;
            FUN_00144ff0((SND::FxAllpassNode *)node);
            FUN_00145020((SND::FxAllpassNode *)node, params);
            break;
        case 8:
            FUN_00144e20((SND::FxSumNode *)node);
            break;
        case 9:
            FUN_00144cc0((SND::FxTeeNode *)node);
            break;
        default:
            break;
        }
        uint8_t route = entry[1];
        if (route != 0) {
            uint32_t row = (NumChannels() + U32(kFx2Config) * 6) * 6;
            uint8_t slot = U8(kRouteTable + route + row);
            Routing()[slot] = Nodes()[i];
        }
        entry += EntrySize(entry[0]);
    }
    const uint8_t *connection = (const uint8_t *)(uintptr_t)U32(kFx2ConnCursor);
    for (int i = 0; i < GetB(Desc() + config * 4 + 0x20, 4); i++) {
        int input = connection[9];
        int output = connection[8];
        MixFilterNode *to = Nodes()[GetB(connection + 4, 4)];
        MixFilterNode *from = Nodes()[GetB(connection, 4)];
        Connect(from, to, output, input);
        connection += 12;
    }
}

// Picks the configuration for the output channels (the highest with nodes, at most the channel count) and finds
// its entries, then builds it.
// FUNC_AT(0x001433a0)
void SNDMIXI_initfx2(int rate, const uint8_t *description) {
    int config = (int)NumChannels();
    U32(kFx2Active) = 1;
    for (uint32_t i = 0; i < 14; i++)
        U32(kFx2Header + i * 4) = ((const uint32_t *)description)[i];
    config--;
    uint32_t count = kFx2NodeCounts + (uint32_t)config * 4;
    const uint8_t *entry = description + 0x38;
    int configs;
    if (GetB((const void *)(uintptr_t)count, 4) != 0) {
        configs = (int)NumChannels();
    } else {
        if (GetB((const void *)(uintptr_t)count, 4) == 0) {
            do {
                count -= 4;
                config--;
            } while (GetB((const void *)(uintptr_t)count, 4) == 0);
        }
        configs = config + 1;
    }
    U32(kFx2Config) = (uint32_t)configs;
    U32(kFx2ConfigCopy) = (uint32_t)configs;
    if (config > 0) {
        uint32_t connections = kFx2ConnCounts;
        for (int left = config; left != 0; left--) {
            int nodes = GetB((const void *)(uintptr_t)(connections - 0x18), 4);
            if (nodes > 0) {
                do
                    entry += EntrySize(entry[0]);
                while (--nodes != 0);
            }
            int links = GetB((const void *)(uintptr_t)connections, 4);
            connections += 4;
            entry += links * 12;
        }
    }
    U32(kFx2Entries) = (uint32_t)(uintptr_t)entry;
    int nodes = GetB((const void *)(uintptr_t)(kFx2NodeCounts + (uint32_t)config * 4), 4);
    if (nodes > 0) {
        do
            entry += EntrySize(entry[0]);
        while (--nodes != 0);
    }
    U32(kFx2Connections) = (uint32_t)(uintptr_t)entry;
    SNDMIXI_initfx(rate);
}

// FUNC_AT(0x001434b0)
void MIX_restorereverb(void) {
    uint32_t fx2 = U32(kFx2Active);
    U32(kFxInitFunc) = 0;
    U32(kFxHook) = 0;
    if (fx2 != 0)
        SNDMIXI_restorefx2();
    for (uint32_t slot = kTaps + 0x0c; slot < kTapsEnd; slot += 0x24) {
        if (U32(slot) != 0) {
            Free((void *)(uintptr_t)U32(slot));
            U32(slot) = 0;
        }
    }
    U8(kReverbState) = 0;
}

// The description (sound.md 3.6): byte 2 the mode, bytes 4..6 the three groups' tap counts, byte 7 the damping,
// then per tap a big-endian u16 delay in milliseconds, the allpass gain and the feedback (signed, / 127).
// FUNC_AT(0x00143510)
void MIX_initreverb(int rate, const uint8_t *description) {
    MIX_restorereverb();
    U32(kFxInitFunc) = 0x00143780;   // SNDMIXI_fxinit
    uint8_t mode = description[2];
    if (mode != 0) {
        if (mode == 10) {
            SNDMIXI_initfx2(rate, description);
            U32(kFxHook) = 0x001437e0;
            U8(kReverbState) = 1;
            return;
        }
    } else {
        U32(kFxHook) = 0x00143970;   // SNDMIXI_fxadd
    }
    int tap = 0;
    const uint8_t *groupCount = description + 4;
    for (uint32_t group = kTapCounts; group < kFx2Connections; group += 4, groupCount++) {
        U32(group) = groupCount[0];
        if ((int)U32(group) <= 0)
            continue;
        ReverbTap *t = (ReverbTap *)(uintptr_t)(kTaps + (uint32_t)tap * 0x24);
        const uint8_t *field = description + tap * 4 + 0xa;
        for (int j = 0; j < (int)U32(group); j++, t++, field += 4) {
            double feedback = (double)(int)(int8_t)field[1] * K(kOver127);
            t->lowpassKeep = (float)feedback;
            t->lowpassInput = (float)(K(kOne) - feedback);
            int damping = description[7] + 100;
            t->lowpassKeep = (float)((double)damping * (double)t->lowpassKeep * K(0x001a7550));   // / 350
            damping = description[7] + 100;
            t->lowpassInput = (float)((double)damping * (double)t->lowpassInput * K(0x001a7550));
            double gain = (double)(int)(int8_t)field[0] * K(kOver127);
            t->gain = (float)gain;
            t->negGain = (float)(-gain);
            int milliseconds = GetB(field - 2, 2);
            int length = findprime(rate, milliseconds);
            void *buffer = Alloc(length * 4);
            t->buffer = (float *)buffer;
            MemClear(buffer, length * 4);
            tap++;
            t->writePos = 0;
            *(uint32_t *)&t->lowpass = 0;
            t->length = length;
            t->readPos = length;
        }
    }
    SNDMIXI_fxinit(description);
    U8(kReverbState) = 2;
}

// FUNC_AT(0x00143780)
void SNDMIXI_fxinit(const uint8_t *description) {
    U32(kFxIdle) = 3000;
    MemClear((void *)(uintptr_t)kFxSend, 0x800);
    if (description[2] != 10)
        return;
    // The SOURCE node's index: entries walked with a stride of 20 x the entry size, as the original does
    const uint8_t *entry = (const uint8_t *)(uintptr_t)U32(kFx2EntryCursor);
    uint8_t type = entry[0];
    int index = 0;
    U32(kFx2Source) = 0;
    if (type == 0)
        return;
    do {
        entry += EntrySize(type) * 20;
        type = entry[0];
        index++;
    } while (type != 0);
    U32(kFx2Source) = (uint32_t)index;
}

// The fx2 hook (never made a function by Ghidra; installed by MIX_initreverb)
// FUNC_AT(0x001437e0)
void FUN_001437e0(int frames) {
    int done[6] = { 0, 0, 0, 0, 0, 0 };
    float *scratchA = (float *)(uintptr_t)U32(kScratchA);
    float *scratchB = (float *)(uintptr_t)U32(kScratchB);
    int idle = (int)U32(kFxIdle) + 1;
    U32(kFxIdle) = (uint32_t)idle;
    if (idle >= 3000)
        return;
    int nodes = GetB(Desc() + U32(kFx2Config) * 4 + 4, 4);
    if (nodes != 0) {
        InitSource(Nodes()[U32(kFx2Source)], (float *)(uintptr_t)kFxSend);
        for (uint32_t i = 0; i < NumChannels(); i++) {
            if (done[i] != 0)
                continue;
            MixFilterNode *node = Routing()[i + 1];
            if (node == NULL)
                continue;
            node->process(node, frames, scratchA, scratchB, (int)i + 1);
            MixCall(frames, 1.0f, scratchB, Accum(i));
            uint32_t channels = NumChannels();
            const int8_t *row = (const int8_t *)(uintptr_t)(
                kMixRows + (i + (channels + U32(kFx2Config) * 6 - 7) * 6) * 6);
            done[i] = 1;
            for (uint32_t j = 0; j < NumChannels(); j++) {
                int8_t speaker = row[j];
                if (speaker >= 0) {
                    MixCall(frames, 1.0f, scratchB, Accum((uint32_t)speaker));
                    done[speaker] = 1;
                }
            }
        }
    }
    MemClear((void *)(uintptr_t)kFxSend, frames * 4);
}

// The standard reverb's hook: group 0 in place on sndfx; group 1 writes its first tap into the second buffer and
// runs the rest there; group 2 likewise into the third. Odd speakers take group 1 when it exists, the others
// group 2, else sndfx; the LFE none.
// FUNC_AT(0x00143970)
void SNDMIXI_fxadd(int frames) {
    int idle = (int)U32(kFxIdle) + 1;
    float *in = (float *)(uintptr_t)kFxSend;
    float *out = (float *)(uintptr_t)kFxSend;
    int tap = 0;
    U32(kFxIdle) = (uint32_t)idle;
    if (idle >= 3000)
        return;
    for (uint32_t group = 0; group < 3; group++) {
        if ((int)U32(kTapCounts + group * 4) <= 0)
            continue;
        ReverbTap *t = (ReverbTap *)(uintptr_t)(kTaps + (uint32_t)tap * 0x24);
        int j = 0;
        do {
            if (group == 1) {
                if (j == 0)
                    out = (float *)(uintptr_t)kFxGroup1;
                else if (j == 1)
                    in = (float *)(uintptr_t)kFxGroup1;
            } else if (group == 2) {
                if (j == 0) {
                    in = (float *)(uintptr_t)kFxSend;
                    out = (float *)(uintptr_t)kFxGroup2;
                } else if (j == 1) {
                    in = (float *)(uintptr_t)kFxGroup2;
                }
            }
            int left = frames;
            if (frames > 0) {
                do {
                    if (t->writePos <= 0)
                        t->writePos = t->length;
                    if (t->readPos <= 0)
                        t->readPos = t->length;
                    int n = left;
                    if (t->writePos < left)
                        n = t->writePos;
                    if (t->readPos < n)
                        n = t->readPos;
                    int offset = frames - left;
                    MIXI_reverbblock(t, n, in + offset, out + offset);
                    left -= n;
                    t->writePos -= n;
                    t->readPos -= n;
                } while (left > 0);
            }
            tap++;
            t++;
            j++;
        } while (j < (int)U32(kTapCounts + group * 4));
    }
    for (uint32_t i = 0; i < NumChannels(); i++) {
        if (i == 5)
            continue;
        const float *source;
        if ((i & 1) != 0 && U32(kTapCounts + 4) != 0)
            source = (const float *)(uintptr_t)kFxGroup2;
        else if (U32(kTapCounts + 8) != 0)
            source = (const float *)(uintptr_t)kFxGroup1;
        else
            source = (const float *)(uintptr_t)kFxSend;
        MixCall(frames, 1.0f, source, Accum(i));
    }
    MemClear((void *)(uintptr_t)kFxSend, frames * 4);
}

// fx2 type 9, the tee: the first reader of a slice pulls upstream (into its scratch) and gets a copy in 'out';
// the cache keeps it for the second, which gets the cache and makes the next call pull again.
// FUNC_AT(0x00144bc0)
int FUN_00144bc0(SND::FxTeeNode *node, int frames, float *scratch, float *out, int requester) {
    (void)requester;
    if (node->served != 0) {
        node->served = 0;
        node->fetch = 1;
    }
    uint32_t bytes = (uint32_t)frames * 4;
    if (node->capacity < frames) {
        if (node->cache != NULL)
            Free(node->cache);
        node->cache = (float *)Alloc((int)bytes);
        node->capacity = frames;
    }
    if (node->fetch != 0) {
        MixFilterNode *up = node->head.upstream;
        int made = up->process(up, frames, out, scratch, 1);
        if (made <= 0)
            return made;
        ForwardCopy(out, scratch, bytes);
        ForwardCopy(node->cache, scratch, (uint32_t)frames * 4);
        node->fetch = 0;
        return frames;
    }
    ForwardCopy(out, node->cache, bytes);
    node->served = 1;
    return frames;
}

// The restore of types 8 and 9 (never made a function by Ghidra): frees the buffer at +0x1c
// FUNC_AT(0x00144ca0)
void FUN_00144ca0(void *node) {
    void *buffer = *(void **)((uint8_t *)node + 0x1c);
    if (buffer != NULL)
        Free(buffer);
}

// FUNC_AT(0x00144cc0)
int FUN_00144cc0(SND::FxTeeNode *node) {
    node->head.upstream = NULL;
    node->head.upstream2 = NULL;
    node->head.downstream = NULL;
    node->head.field14 = 0;
    node->head.field1b = 0;
    node->head.restore = (SND::MixRestoreFn)(uintptr_t)0x00144ca0;
    node->head.process = (SND::MixProcessFn)(uintptr_t)0x00144bc0;
    node->capacity = 0;
    node->cache = NULL;
    node->served = 0;
    node->fetch = 1;
    return 0;
}

// fx2 type 8, the sum of input 1 (into 'out') and input 2 (into the node's buffer) (never made a function)
// FUNC_AT(0x00144d00)
int FUN_00144d00(SND::FxSumNode *node, int frames, float *scratch, float *out, int requester) {
    (void)requester;
    if (frames > node->capacity) {
        if (node->buffer != NULL)
            Free(node->buffer);
        node->capacity = frames;
        node->buffer = (float *)Alloc(frames * 4);
    }
    MixFilterNode *up = node->head.upstream;
    int made = up->process(up, frames, scratch, out, 1);
    if (made <= 0)
        return made;
    MixFilterNode *up2 = node->head.upstream2;
    made = up2->process(up2, frames, scratch, node->buffer, 2);
    if (made <= 0)
        return made;
    for (int i = 0; i < frames; i++)
        out[i] = (float)((double)node->buffer[i] + (double)out[i]);
    return frames;
}

// FUNC_AT(0x00144e20)
int FUN_00144e20(SND::FxSumNode *node) {
    node->head.upstream = NULL;
    node->head.upstream2 = NULL;
    node->head.downstream = NULL;
    node->head.field14 = 0;
    node->head.field1b = 0;
    node->head.restore = (SND::MixRestoreFn)(uintptr_t)0x00144ca0;
    node->head.process = (SND::MixProcessFn)(uintptr_t)0x00144d00;
    node->buffer = NULL;
    node->capacity = 0;
    return 0;
}

// The allpass kernel, four samples a step (a count that is not a multiple of 4 runs over to the next 4):
// v = g x d + in + 1e-20; out = -g x v + d; d = v. The third and fourth v are rounded to float before their
// output is formed, the first two are not.
// FUNC_AT(0x00144e50)
void FUN_00144e50(SND::FxAllpassNode *node, int count, const float *in, float *out) {
    float *end = out + count;
    float *d = node->buffer + node->position;
    if (!(out < end))
        return;
    const double guard = K(kDenormalGuard);
    do {
        double v0 = (double)node->gain * (double)d[0] + (double)in[0];
        v0 = v0 + guard;
        double v1 = (double)node->gain * (double)d[1] + (double)in[1];
        v1 = v1 + guard;
        double v2 = (double)node->gain * (double)d[2] + (double)in[2];
        v2 = v2 + guard;
        float f2 = (float)v2;
        double v3 = (double)d[3] * (double)node->gain + (double)in[3];
        v3 = v3 + guard;
        float f3 = (float)v3;
        out[0] = (float)(v0 * (double)node->negGain + (double)d[0]);
        out[1] = (float)(v1 * (double)node->negGain + (double)d[1]);
        out[2] = (float)((double)f2 * (double)node->negGain + (double)d[2]);
        out[3] = (float)((double)f3 * (double)node->negGain + (double)d[3]);
        d[2] = f2;
        d[0] = (float)v0;
        d[3] = f3;
        d[1] = (float)v1;
        out += 4;
        in += 4;
        d += 4;
    } while (out < end);
}

// fx2 type 7: pulls input into 'scratch', runs the allpass into 'out' in runs up to the delay's wrap
// FUNC_AT(0x00144f30)
int FUN_00144f30(SND::FxAllpassNode *node, int frames, float *scratch, float *out, int requester) {
    (void)requester;
    MixFilterNode *up = node->head.upstream;
    if (up != NULL) {
        int made = up->process(up, frames, out, scratch, node->head.requester);
        if (made <= 0)
            return made;
    }
    if (frames <= 0)
        return frames;
    float *o = out;
    const float *i = scratch;
    int left = frames;
    do {
        int length = node->length;
        if (node->position >= length)
            node->position = 0;
        int n = length - node->position;
        if (n > left)
            n = left;
        FUN_00144e50(node, n, i, o);
        node->position += n;
        o += n;
        left -= n;
        i += n;
    } while (left > 0);
    return frames;
}

// FUNC_AT(0x00144fc0)
void FUN_00144fc0(SND::FxAllpassNode *node) {
    if (node->memory != NULL) {
        Free(node->memory);
        node->memory = NULL;
        node->buffer = NULL;
    }
}

// FUNC_AT(0x00144ff0)
int FUN_00144ff0(SND::FxAllpassNode *node) {
    node->head.upstream = NULL;
    node->head.upstream2 = NULL;
    node->head.downstream = NULL;
    node->head.field14 = 0;
    node->head.field1b = 0;
    node->head.restore = (SND::MixRestoreFn)(uintptr_t)0x00144fc0;
    node->head.process = (SND::MixProcessFn)(uintptr_t)0x00144f30;
    node->memory = NULL;
    node->buffer = NULL;
    return 0;
}

// params: gain x 254 (halved, / 127), rate << 8, milliseconds << 8
// FUNC_AT(0x00145020)
void FUN_00145020(SND::FxAllpassNode *node, const int *params) {
    int half = params[0] >> 1;
    int rate = params[1] >> 8;
    int milliseconds = params[2] >> 8;
    if (node->memory != NULL) {
        Free(node->memory);
        node->memory = NULL;
        node->buffer = NULL;
    }
    double gain = (double)half * K(kOver127);
    node->gain = (float)gain;
    node->negGain = (float)(-gain);
    int length = FUN_001465a0(rate, milliseconds);
    node->length = length;
    void *memory = Alloc(length * 4 + 0x10);
    node->memory = memory;
    float *buffer = (float *)(uintptr_t)((((uint32_t)(uintptr_t)memory + 0xf) >> 4) << 4);
    node->buffer = buffer;
    MemClear(buffer, length * 4);
    node->position = 0;
}

// fx2 type 6: out = in x gain
// FUNC_AT(0x001450b0)
int FUN_001450b0(SND::FxGainNode *node, int frames, float *scratch, float *out, int requester) {
    (void)requester;
    MixFilterNode *up = node->head.upstream;
    if (up != NULL) {
        int made = up->process(up, frames, out, scratch, node->head.requester);
        if (made <= 0)
            return made;
    }
    for (int i = 0; i < frames; i++)
        out[i] = (float)((double)scratch[i] * (double)node->gain);
    return frames;
}

// FUNC_AT(0x00145160)
int FUN_00145160(SND::FxGainNode *node) {
    node->head.upstream = NULL;
    node->head.upstream2 = NULL;
    node->head.downstream = NULL;
    node->head.field14 = 0;
    node->head.field1b = 0;
    node->head.restore = NULL;
    node->head.process = (SND::MixProcessFn)(uintptr_t)0x001450b0;
    *(uint32_t *)&node->gain = 0;
    return 0;
}

// FUNC_AT(0x00145190)
void FUN_00145190(SND::FxGainNode *node, const int *params) {
    node->gain = (float)((double)params[0] * K(kOver256));
}

// The resonator: y = (in + 1e-20) a0 + 2 b1 r y1 - r^2 y2, run only while rate x 2/pi > bandwidth and
// 0 < frequency < rate / 2; otherwise the input is copied.
// FUNC_AT(0x001451b0)
void FUN_001451b0(SND::FxResonatorNode *node, int count, const float *in, float *out) {
    int rate = node->rate;
    if (!((double)rate * K(0x001a7568) > (double)node->bandwidth) || !((double)node->frequency > K(kZero)) ||
        !((double)(rate >> 1) > (double)node->frequency)) {
        for (int i = 0; i < count; i++)
            memcpy(&out[i], &in[i], 4);
        return;
    }
    const double guard = K(kDenormalGuard);
    for (int i = 0; i < count; i++) {
        double t = ((double)in[i] + guard) * (double)node->a0;
        double br = (double)node->b1 * (double)node->radius;
        double p = (br + br) * (double)node->y1;
        double y = (t + p) - ((double)node->y2 * (double)node->radius) * (double)node->radius;
        out[i] = (float)y;
        memcpy(&node->y2, &node->y1, 4);
        memcpy(&node->y1, &out[i], 4);
    }
}

// fx2 type 5: runs the resonator over what upstream made
// FUNC_AT(0x00145390)
int FUN_00145390(SND::FxResonatorNode *node, int frames, float *scratch, float *out, int requester) {
    (void)requester;
    int made = frames;
    MixFilterNode *up = node->head.upstream;
    if (up != NULL) {
        made = up->process(up, frames, out, scratch, node->head.requester);
        if (made <= 0)
            return made;
    }
    FUN_001451b0(node, made, scratch, out);
    return made;
}

// FUNC_AT(0x001453e0)
int FUN_001453e0(SND::FxResonatorNode *node) {
    node->head.upstream = NULL;
    node->head.upstream2 = NULL;
    node->head.downstream = NULL;
    node->head.field14 = 0;
    node->head.field1b = 0;
    node->head.restore = NULL;
    node->head.process = (SND::MixProcessFn)(uintptr_t)0x00145390;
    *(uint32_t *)&node->field34 = 0;
    *(uint32_t *)&node->y1 = 0;
    *(uint32_t *)&node->y2 = 0;
    return 0;
}

// params: frequency << 8, rate << 8, bandwidth << 8, gain x 256
// FUNC_AT(0x00145410)
void FUN_00145410(SND::FxResonatorNode *node, const int *params) {
    double gain = (double)params[3] * K(kOver256);
    int frequency = params[0] >> 8;
    int bandwidth = params[2] >> 8;
    int rate = params[1] >> 8;
    float gainF = (float)gain;
    double f = (double)frequency;
    node->frequency = (float)f;
    double q = (double)bandwidth;
    node->rate = rate;
    node->bandwidth = (float)q;
    double r = K(kOne) - (q * K(0x001a756c)) / (double)rate;   // pi
    node->radius = (float)r;
    float rr = (float)(r * r);
    double w = (f * K(0x0019320c)) / (double)rate;              // 2 pi
    double c = FUN_00146640((float)w);
    double b = c * (((double)node->radius + (double)node->radius) / ((double)rr + K(kOne)));
    node->b1 = (float)b;
    double e = FUN_001465c0((float)(-(b * b)));
    double a = e * (K(kOne) - (double)rr);
    a = a * (double)gainF;
    node->a0 = (float)a;
}

// fx2 type 4 (module H's FIR node): two corners / rate, then the FIR design of kind 4
// FUNC_AT(0x00145500)
void FUN_00145500(SND::MixFilterNode *node, const int *params) {
    int rate = params[2] >> 8;
    int low = params[0] >> 7;
    int high = params[1] >> 7;
    double r = (double)rate;
    NodeF32(node, 0x50) = (float)((double)low / r);
    NodeF32(node, 0x54) = (float)((double)high / r);
    FirDesign((uint8_t *)node + 0x1c, 4);   // a tail call in the original
}

// fx2 type 2: module H's FIR over what upstream made
// FUNC_AT(0x001455f0)
int FUN_001455f0(SND::MixFilterNode *node, int frames, float *scratch, float *out, int requester) {
    (void)requester;
    MixFilterNode *up = node->upstream;
    if (up != NULL) {
        int made = up->process(up, frames, out, scratch, node->requester);
        if (made <= 0)
            return made;
    }
    FirRun((uint8_t *)node + 0x1c, frames, scratch, out);
    return frames;
}

// FUNC_AT(0x00145640)
int FUN_00145640(SND::MixFilterNode *node) {
    node->upstream = NULL;
    node->upstream2 = NULL;
    node->downstream = NULL;
    node->field14 = 0;
    node->field1b = 0;
    node->restore = NULL;
    node->process = (SND::MixProcessFn)(uintptr_t)0x001455f0;
    FirClear((uint8_t *)node + 0x1c);
    return 0;
}

// params: corner << 8, rate << 8; the FIR design of kind 2
// FUNC_AT(0x00145670)
void FUN_00145670(SND::MixFilterNode *node, const int *params) {
    int corner = params[0] >> 8;
    int rate = params[1] >> 8;
    NodeF32(node, 0x54) = (float)(((double)corner + (double)corner) / (double)rate);
    FirDesign((uint8_t *)node + 0x1c, 2);   // a tail call in the original
}

// One run of a standard tap, at most up to either position's wrap (positions count down; the caller wraps them):
//   a = -g in + delayed;  out = a;  v = g a + in + 1e-30;  lowpass = keep x lowpass + input x v;  write lowpass
// a and v stay double, as on the x87 stack. Hand-written in the original, preserving every register; it runs
// one sample even for a count of 0.
// FUNC_AT(0x00145760)
void MIXI_reverbblock(SND::ReverbTap *tap, int count, const float *in, float *out) {
    float *write = tap->buffer + tap->writePos;
    const float *read = tap->buffer + tap->readPos;
    const double guard = K(kReverbGuard);
    int i = 0;
    do {
        double a = (double)in[i] * (double)tap->negGain;
        double kept = (double)tap->lowpass * (double)tap->lowpassKeep;
        a = a + (double)read[-1];
        double v = a * (double)tap->gain + (double)in[i];
        write--;
        read--;
        v = v + guard;
        double input = (double)tap->lowpassInput;
        out[i] = (float)a;
        *write = (float)v;
        double lp = kept + input * v;
        *write = (float)lp;
        tap->lowpass = (float)lp;
        i++;
    } while (i < count);
}

// A delay length in samples, a multiple of 16: findprime(rate, milliseconds / 16) x 16
// FUNC_AT(0x001465a0)
int FUN_001465a0(int rate, int milliseconds) {
    return findprime(rate, milliseconds / 16) << 4;
}

// sqrt(1 + x) by its series to x^7, returned on the x87 stack (double)
// FUNC_AT(0x001465c0)
double FUN_001465c0(float x) {
    double v = (double)x;
    double a = v * K(0x00189eb0);
    double b = (v * a) * K(0x0018ac0c);
    double c = (v * b) * K(0x0018a5e4);
    double d = (v * c) * K(0x001a7574);
    double e = (v * d) * K(0x0018b924);
    double f = (v * e) * K(0x0018b590);
    double g = (v * f) * K(0x001a7570);
    double sum = g + f;
    sum = sum + e;
    sum = sum + d;
    sum = sum + c;
    sum = sum + b;
    sum = a + sum;
    return sum + K(kOne);
}

// cos(x) by its series to x^12 after taking 2 pi off while x > 2 pi, returned on the x87 stack (double)
// FUNC_AT(0x00146640)
double FUN_00146640(float x) {
    double v = (double)x;
    if (v > K(0x0019320c)) {
        do
            v = v - K(0x0019320c);
        while (v > K(0x0019320c));
    }
    double s = v * v;
    double s2 = s * s;
    double s3 = s2 * s;
    double s4 = s2 * s2;
    double t = K(kOne) - s * K(0x00189eb0);
    t = t + s2 * K(0x00190330);
    t = t - s3 * K(0x001a7584);
    t = t + s4 * K(0x001a7580);
    t = t - (s4 * s) * K(0x001a757c);
    t = t + (s3 * s3) * K(0x001a7578);
    return t;
}

// The first prime (by trial division) at or above rate x milliseconds / 1000, at least 3. The products wrap
// as the original's 32-bit IMULs do.
// FUNC_AT(0x0014a260)
int findprime(int rate, int milliseconds) {
    int n = (int)((uint32_t)rate * (uint32_t)milliseconds) / 1000;
    if (n < 3)
        n = 3;
    for (;;) {
        int c = 0;
        int limit;
        for (;;) {
            int square = (int)((uint32_t)c * (uint32_t)c);
            c++;
            if (square == n) {
                limit = c;
                break;
            }
            if (square > n) {
                limit = c - 1;
                break;
            }
        }
        if (limit < 2)
            continue;
        int d = 2;
        bool composite = false;
        for (;;) {
            if (n % d == 0) {
                composite = true;
                break;
            }
            if (d == limit)
                return n;
            d++;
            if (d > limit)
                break;
        }
        if (composite)
            n++;
    }
}
