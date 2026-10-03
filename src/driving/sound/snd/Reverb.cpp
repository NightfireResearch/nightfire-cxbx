#include "Reverb.h"
#include "Banks.h"
#include "System.h"

#include <bit>
#include <stddef.h>
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

using SND::SFilterNode;
using SND::ReverbTap;

// The description tables in the original's .rdata, read where they are: an unknown node type reads past the ten
#define NodeSizes ((const uint32_t *)0x001d9e88)       // [type] bytes per node
#define EntrySizes ((const uint32_t *)0x001da498)      // [type] bytes per node entry
#define RouteTable ((const uint8_t *)0x001d9e85)       // [(channels + configuration x 6) x 6 + route]: the routing slot
#define MixRows ((const int8_t (*)[6])0x001d9f88)      // per configuration and channel: the other speakers fed, or -1

// The functions the tables hold: the originals' addresses, as the original stores them (they jump to ours)
#define FxInitAt ((SND::MixFxInitFn)0x00143780)                  // SNDMIXI_fxinit
#define Fx2HookAt ((SND::MixFxHook)0x001437e0)                   // FUN_001437e0
#define FxAddAt ((SND::MixFxHook)0x00143970)                     // SNDMIXI_fxadd
#define TeeProcessAt ((SND::SFilterProcess)0x00144bc0)          // FUN_00144bc0
#define BufferRestoreAt ((SND::SFilterRestore)0x00144ca0)       // FUN_00144ca0
#define SumProcessAt ((SND::SFilterProcess)0x00144d00)          // FUN_00144d00
#define AllpassProcessAt ((SND::SFilterProcess)0x00144f30)      // FUN_00144f30
#define AllpassRestoreAt ((SND::SFilterRestore)0x00144fc0)      // FUN_00144fc0
#define GainProcessAt ((SND::SFilterProcess)0x001450b0)         // FUN_001450b0
#define ResonatorProcessAt ((SND::SFilterProcess)0x00145390)    // FUN_00145390
#define FirProcessAt ((SND::SFilterProcess)0x001455f0)          // FUN_001455f0

// The originals called from here. memclr is not ours; module H's node functions are, but take their own node
// types (Filters.h), so they are called at the originals' addresses, typed with the node head the network holds.
#define MemClear ((void (*)(void *, int))0x0013f600)                                  // memclr
#define CreateLPFRC ((int (*)(SFilterNode *))0x00143710)                              // SFILTER_createLPFRC
#define ModifyLPFRC ((void (*)(SFilterNode *, int *))0x00143740)                      // SFILTER_modifyLPFRC
#define CreateHPFFIR8 ((int (*)(SFilterNode *))0x001454d0)                            // SFILTER_createHPFFIR8
#define ModifyHPFFIR8 ((void (*)(SFilterNode *, int *))0x001455b0)                    // SFILTER_modifyHPFFIR8
#define CreateSource ((int (*)(SFilterNode *))0x00145700)                             // SFILTER_createSOURCE
#define InitSource ((void (*)(SFilterNode *, float *))0x001456e0)                     // SFILTER_initSOURCE
#define Connect ((int (*)(SFilterNode *, SFilterNode *, int, int))0x001444f0)         // SFILTER_connect

constexpr float kOneOver127 = 1.0f / 127;
constexpr float kOneOver256 = 1.0f / 256;
constexpr float kOneOver350 = 1.0f / 350;
constexpr float kDenormalGuard = 1e-20f;            // added before the fx2 blocks' feedback
constexpr float kReverbGuard = 1e-30f;              // the standard taps'
constexpr float kTwoOverPi = 0x1.45f306p-1f;
constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;
static_assert(std::bit_cast<uint32_t>(kOneOver127) == 0x3c010204, "the original's 0x001a296c");
static_assert(std::bit_cast<uint32_t>(kOneOver350) == 0x3b3b3ee7, "the original's 0x001a7550");
static_assert(std::bit_cast<uint32_t>(kDenormalGuard) == 0x1e3ce508, "the original's 0x001a7564");
static_assert(std::bit_cast<uint32_t>(kReverbGuard) == 0x0da24260, "the original's 0x001da4c0");
static_assert(std::bit_cast<uint32_t>(kTwoOverPi) == 0x3f22f983, "the original's 0x001a7568");
static_assert(std::bit_cast<uint32_t>(kPi) == 0x40490fdb, "the original's 0x001a756c");
static_assert(std::bit_cast<uint32_t>(kTwoPi) == 0x40c90fdb, "the original's 0x0019320c");

// sqrt(1 + x)'s series: each term is the last x x the next ratio (the binomial's (1/2 - n) / (n + 1), the last one
// rounded to -0.7857 by EA)
constexpr float kSqrtRatio[7] = { 0.5f, -0.25f, -0.5f, -0.625f, -0.7f, -0.75f, -0.7857f };
static_assert(std::bit_cast<uint32_t>(kSqrtRatio[4]) == 0xbf333333, "the original's 0x0018b924");
static_assert(std::bit_cast<uint32_t>(kSqrtRatio[6]) == 0xbf4923a3, "the original's 0x001a7570");

// cos's series: 1/2!, 1/4! .. 1/12! (EA's 1/12! is one ulp above the nearest float)
constexpr float kCosTerm[6] = { 0.5f, 1.0f / 24, 1.0f / 720, 1.0f / 40320, 1.0f / 3628800, 0x1.1eed9p-29f };
static_assert(std::bit_cast<uint32_t>(kCosTerm[1]) == 0x3d2aaaab, "the original's 0x00190330");
static_assert(std::bit_cast<uint32_t>(kCosTerm[2]) == 0x3ab60b61, "the original's 0x001a7584");
static_assert(std::bit_cast<uint32_t>(kCosTerm[3]) == 0x37d00d01, "the original's 0x001a7580");
static_assert(std::bit_cast<uint32_t>(kCosTerm[4]) == 0x3493f27e, "the original's 0x001a757c");
static_assert(std::bit_cast<uint32_t>(kCosTerm[5]) == 0x310f76c8, "the original's 0x001a7578");

// REP MOVSD then REP MOVSB, forwards
void ForwardCopy(void *to, const void *from, uint32_t bytes) {
    uint32_t *d = static_cast<uint32_t *>(to);
    const uint32_t *s = static_cast<const uint32_t *>(from);
    for (uint32_t n = bytes >> 2; n != 0; n--)
        *d++ = *s++;
    uint8_t *db = reinterpret_cast<uint8_t *>(d);
    const uint8_t *sb = reinterpret_cast<const uint8_t *>(s);
    for (uint32_t n = bytes & 3; n != 0; n--)
        *db++ = *sb++;
}

// The common head of the fx2 blocks' inits
void ClearLinks(SFilterNode *node) {
    node->input = NULL;
    node->input2 = NULL;
    node->output = NULL;
    node->output2 = NULL;
    node->flags = 0;
}

}  // namespace

// FUNC_AT(0x00142ee0)
void SNDMIXI_restorefx2(void) {
    int count = SNDI_getb(SndMix.fx2Desc->nodeCounts[SndMix.fx2ConfigCopy - 1], 4);
    if (count != 0) {
        for (int i = 0; i < count; i++) {
            SFilterNode *node = SndMix.fx2Nodes[i];
            if (node->restore != NULL)
                node->restore(node);
            if (SndMix.fx2Nodes[i] != NULL) {
                SNDMEMI_free(SndMix.fx2Nodes[i]);
                SndMix.fx2Nodes[i] = NULL;
            }
        }
    }
    if (SndMix.fx2Nodes != NULL) {
        SNDMEMI_free(SndMix.fx2Nodes);
        SndMix.fx2Nodes = NULL;
    }
    SFilterNode **routing = SndMix.fx2Routing;
    for (int i = 0; i < 6; i++) {
        if (routing[i] != NULL) {
            routing[i] = NULL;
            routing = SndMix.fx2Routing;
        }
    }
    if (routing != NULL) {
        SNDMEMI_free(routing);
        SndMix.fx2Routing = NULL;
    }
    SndMix.fx2Entries = NULL;
    SndMix.fx2Connections = NULL;
    SndMix.fx2ConnCursor = NULL;
    SndMix.fx2EntryCursor = NULL;
    SndMix.fx2Active = 0;
}

// Builds the selected configuration's nodes and connections. 'rate' is the platform rate. A node entry is its
// type (SND::Fx2NodeType), its route (0: none) and big-endian u16 parameters from +2.
// FUNC_AT(0x00142fc0)
void SNDMIXI_initfx(int rate) {
    SndMix.fx2Desc = &SndMix.fx2Header;
    void *routing = SNDMEMI_alloc(0x18);
    int config = SndMix.fx2Config - 1;
    SndMix.fx2Routing = static_cast<SFilterNode **>(routing);
    SndMix.fx2EntryCursor = SndMix.fx2Entries;
    SndMix.fx2ConnCursor = SndMix.fx2Connections;
    int nodeCount = SNDI_getb(SndMix.fx2Desc->nodeCounts[config], 4);
    void *nodes = SNDMEMI_alloc(nodeCount * 4);
    const uint8_t *entry = SndMix.fx2EntryCursor;
    SndMix.fx2Nodes = static_cast<SFilterNode **>(nodes);
    for (int i = 0; i < nodeCount; i++) {
        SndMix.fx2Nodes[i] = static_cast<SFilterNode *>(SNDMEMI_alloc(NodeSizes[entry[0]]));
        int params[4];
        MemClear(params, 0x10);
        SFilterNode *node = SndMix.fx2Nodes[i];
        switch (entry[0]) {
        case SND::kFx2Source:
            CreateSource(node);
            break;
        case SND::kFx2LowpassRC:   // cutoff, rate, the third design word
            params[0] = SNDI_getb(entry + 4, 2) << 8;
            params[1] = rate << 8;
            params[2] = SNDI_getb(entry + 6, 2);
            CreateLPFRC(node);
            ModifyLPFRC(node, params);
            break;
        case SND::kFx2FirLowpass:   // corner, rate
            params[0] = SNDI_getb(entry + 2, 2) << 8;
            params[1] = rate << 8;
            FUN_00145640(reinterpret_cast<SND::SFilterFIR8 *>(node));
            FUN_00145670(reinterpret_cast<SND::SFilterFIR8 *>(node), params);
            break;
        case SND::kFx2FirHighpass:   // cutoff, rate
            params[0] = SNDI_getb(entry + 2, 2) << 8;
            params[1] = rate << 8;
            CreateHPFFIR8(node);
            ModifyHPFFIR8(node, params);
            break;
        case SND::kFx2FirBandpass:   // the two corners, rate
            params[0] = SNDI_getb(entry + 4, 2) << 8;
            params[1] = SNDI_getb(entry + 6, 2) << 8;
            params[2] = rate << 8;
            CreateHPFFIR8(node);
            FUN_00145500(reinterpret_cast<SND::SFilterFIR8 *>(node), params);
            break;
        case SND::kFx2Resonator:   // frequency, rate, bandwidth, gain (x 256)
            params[0] = SNDI_getb(entry + 2, 2) << 8;
            params[1] = rate << 8;
            params[2] = SNDI_getb(entry + 4, 2) << 8;
            params[3] = SNDI_getb(entry + 6, 2);
            FUN_001453e0(static_cast<SND::FxResonatorNode *>(node));
            FUN_00145410(static_cast<SND::FxResonatorNode *>(node), params);
            break;
        case SND::kFx2Gain:   // gain (x 256)
            params[0] = SNDI_getb(entry + 2, 2);
            FUN_00145160(static_cast<SND::FxGainNode *>(node));
            FUN_00145190(static_cast<SND::FxGainNode *>(node), params);
            break;
        case SND::kFx2Allpass:   // gain (x 254), rate, milliseconds
            params[0] = SNDI_getb(entry + 2, 2);
            params[1] = rate << 8;
            params[2] = SNDI_getb(entry + 4, 2) << 8;
            FUN_00144ff0(static_cast<SND::FxAllpassNode *>(node));
            FUN_00145020(static_cast<SND::FxAllpassNode *>(node), params);
            break;
        case SND::kFx2Sum:
            FUN_00144e20(static_cast<SND::FxSumNode *>(node));
            break;
        case SND::kFx2Tee:
            FUN_00144cc0(static_cast<SND::FxTeeNode *>(node));
            break;
        default:
            break;
        }
        uint8_t route = entry[1];
        if (route != 0) {
            int row = (SndMix.counts.channels + SndMix.fx2Config * 6) * 6;
            uint8_t slot = RouteTable[route + row];
            SndMix.fx2Routing[slot] = SndMix.fx2Nodes[i];
        }
        entry += EntrySizes[entry[0]];
    }
    const SND::Fx2Connection *connection = SndMix.fx2ConnCursor;
    for (int i = 0; i < SNDI_getb(SndMix.fx2Desc->connCounts[config], 4); i++) {
        int input = connection->input;
        int output = connection->output;
        SFilterNode *to = SndMix.fx2Nodes[SNDI_getb(connection->to, 4)];
        SFilterNode *from = SndMix.fx2Nodes[SNDI_getb(connection->from, 4)];
        Connect(from, to, output, input);
        connection++;
    }
}

// Picks the configuration for the output channels (the highest with nodes, at most the channel count) and finds
// its entries, then builds it.
// FUNC_AT(0x001433a0)
void SNDMIXI_initfx2(int rate, const uint8_t *description) {
    int config = SndMix.counts.channels;
    SndMix.fx2Active = 1;
    memcpy(&SndMix.fx2Header, description, sizeof(SND::Fx2Header));   // the original copies 14 dwords
    config--;
    const uint8_t *count = SndMix.fx2Header.nodeCounts[config];
    const uint8_t *entry = description + sizeof(SND::Fx2Header);
    int configs;
    if (SNDI_getb(count, 4) != 0) {
        configs = SndMix.counts.channels;
    } else {
        if (SNDI_getb(count, 4) == 0) {
            do {
                count -= 4;   // the configuration below
                config--;
            } while (SNDI_getb(count, 4) == 0);
        }
        configs = config + 1;
    }
    SndMix.fx2Config = configs;
    SndMix.fx2ConfigCopy = configs;
    // Skip the configurations below: their node entries, then their connections
    for (int below = 0; below < config; below++) {
        int nodes = SNDI_getb(SndMix.fx2Header.nodeCounts[below], 4);
        if (nodes > 0) {
            do
                entry += EntrySizes[entry[0]];
            while (--nodes != 0);
        }
        int links = SNDI_getb(SndMix.fx2Header.connCounts[below], 4);
        entry += links * 12;   // x sizeof(Fx2Connection)
    }
    SndMix.fx2Entries = entry;
    int nodes = SNDI_getb(SndMix.fx2Header.nodeCounts[config], 4);
    if (nodes > 0) {
        do
            entry += EntrySizes[entry[0]];
        while (--nodes != 0);
    }
    SndMix.fx2Connections = reinterpret_cast<const SND::Fx2Connection *>(entry);
    SNDMIXI_initfx(rate);
}

// FUNC_AT(0x001434b0)
void MIX_restorereverb(void) {
    uint32_t fx2 = SndMix.fx2Active;
    SndMix.fxInit = NULL;
    SndMix.fxHook = NULL;
    if (fx2 != 0)
        SNDMIXI_restorefx2();
    for (ReverbTap &tap : SndMix.taps) {
        if (tap.buffer != NULL) {
            SNDMEMI_free(tap.buffer);
            tap.buffer = NULL;
        }
    }
    SndMix.reverbState = SND::kReverbOff;
}

// The description: see SND::ReverbTapDesc. One of more than 11 taps runs past SndMix.taps into the fx2 state after
// them, as the original's does.
// FUNC_AT(0x00143510)
void MIX_initreverb(int rate, const uint8_t *description) {
    MIX_restorereverb();
    SndMix.fxInit = FxInitAt;
    uint8_t mode = description[2];
    if (mode != 0) {
        if (mode == 10) {
            SNDMIXI_initfx2(rate, description);
            SndMix.fxHook = Fx2HookAt;
            SndMix.reverbState = SND::kReverbFx2;
            return;
        }
    } else {
        SndMix.fxHook = FxAddAt;
    }
    const SND::ReverbTapDesc *tapDescs = reinterpret_cast<const SND::ReverbTapDesc *>(description + 8);
    int tap = 0;
    for (int group = 0; group < 3; group++) {
        SndMix.tapCounts[group] = description[4 + group];
        if (SndMix.tapCounts[group] <= 0)
            continue;
        ReverbTap *t = &SndMix.taps[tap];
        const SND::ReverbTapDesc *field = &tapDescs[tap];
        for (int j = 0; j < SndMix.tapCounts[group]; j++, t++, field++) {
            double feedback = double(field->feedback) * kOneOver127;
            t->lowpassKeep = float(feedback);
            t->lowpassInput = float(1.0 - feedback);
            int damping = description[7] + 100;
            t->lowpassKeep = float(double(damping) * t->lowpassKeep * kOneOver350);
            damping = description[7] + 100;
            t->lowpassInput = float(double(damping) * t->lowpassInput * kOneOver350);
            double gain = double(field->gain) * kOneOver127;
            t->gain = float(gain);
            t->negGain = float(-gain);
            int milliseconds = SNDI_getb(field->milliseconds, 2);
            int length = findprime(rate, milliseconds);
            void *buffer = SNDMEMI_alloc(length * 4);
            t->buffer = static_cast<float *>(buffer);
            MemClear(buffer, length * 4);
            tap++;
            t->writePos = 0;
            t->lowpass = 0.0f;
            t->length = length;
            t->readPos = length;
        }
    }
    SNDMIXI_fxinit(description);
    SndMix.reverbState = SND::kReverbStandard;
}

// FUNC_AT(0x00143780)
void SNDMIXI_fxinit(const uint8_t *description) {
    SndMix.fxIdle = 3000;
    MemClear(SndMix.fxSend, sizeof(SndMix.fxSend));
    if (description[2] != 10)
        return;
    // The SOURCE node's index: entries walked with a stride of 20 x the entry size, as the original does
    const uint8_t *entry = SndMix.fx2EntryCursor;
    uint8_t type = entry[0];
    int index = 0;
    SndMix.fx2Source = 0;
    if (type == SND::kFx2Source)
        return;
    do {
        entry += EntrySizes[type] * 20;
        type = entry[0];
        index++;
    } while (type != SND::kFx2Source);
    SndMix.fx2Source = index;
}

// The fx2 hook (never made a function by Ghidra; installed by MIX_initreverb): each routed node pulled into its
// channel and the other channels its mix row names
// FUNC_AT(0x001437e0)
void FUN_001437e0(int frames) {
    int done[6] = { 0, 0, 0, 0, 0, 0 };
    float *scratchA = SndMix.scratch[0];
    float *scratchB = SndMix.scratch[1];
    int idle = SndMix.fxIdle + 1;
    SndMix.fxIdle = idle;
    if (idle >= 3000)
        return;
    int nodes = SNDI_getb(SndMix.fx2Desc->nodeCounts[SndMix.fx2Config - 1], 4);
    if (nodes != 0) {
        InitSource(SndMix.fx2Nodes[SndMix.fx2Source], SndMix.fxSend);
        for (uint32_t i = 0; i < SndMix.counts.channels; i++) {
            if (done[i] != 0)
                continue;
            SFilterNode *node = SndMix.fx2Routing[i + 1];
            if (node == NULL)
                continue;
            node->process(node, frames, scratchA, scratchB, int(i) + 1);
            SndMix.mixFunc(frames, 1.0f, scratchB, SndMix.accum[i]);
            uint32_t channels = SndMix.counts.channels;
            const int8_t *row = MixRows[i + (channels + SndMix.fx2Config * 6 - 7) * 6];
            done[i] = 1;
            for (uint32_t j = 0; j < SndMix.counts.channels; j++) {
                int8_t speaker = row[j];
                if (speaker >= 0) {
                    SndMix.mixFunc(frames, 1.0f, scratchB, SndMix.accum[speaker]);
                    done[speaker] = 1;
                }
            }
        }
    }
    MemClear(SndMix.fxSend, frames * 4);
}

// The standard reverb's hook: group 0 in place on sndfx; group 1 writes its first tap into fxGroup1 and runs the
// rest there; group 2 likewise from sndfx into fxGroup2. Then each speaker but the LFE (5) takes fxGroup2 if it is
// odd and group 1 has taps, else fxGroup1 if group 2 has taps, else sndfx - crossed, as the original has it.
// FUNC_AT(0x00143970)
void SNDMIXI_fxadd(int frames) {
    int idle = SndMix.fxIdle + 1;
    float *in = SndMix.fxSend;
    float *out = SndMix.fxSend;
    int tap = 0;
    SndMix.fxIdle = idle;
    if (idle >= 3000)
        return;
    for (int group = 0; group < 3; group++) {
        if (SndMix.tapCounts[group] <= 0)
            continue;
        ReverbTap *t = &SndMix.taps[tap];
        int j = 0;
        do {
            if (group == 1) {
                if (j == 0)
                    out = SndMix.fxGroup1;
                else if (j == 1)
                    in = SndMix.fxGroup1;
            } else if (group == 2) {
                if (j == 0) {
                    in = SndMix.fxSend;
                    out = SndMix.fxGroup2;
                } else if (j == 1) {
                    in = SndMix.fxGroup2;
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
        } while (j < SndMix.tapCounts[group]);
    }
    for (uint32_t i = 0; i < SndMix.counts.channels; i++) {
        if (i == 5)
            continue;
        const float *source;
        if ((i & 1) != 0 && SndMix.tapCounts[1] != 0)
            source = SndMix.fxGroup2;
        else if (SndMix.tapCounts[2] != 0)
            source = SndMix.fxGroup1;
        else
            source = SndMix.fxSend;
        SndMix.mixFunc(frames, 1.0f, source, SndMix.accum[i]);
    }
    MemClear(SndMix.fxSend, frames * 4);
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
    uint32_t bytes = uint32_t(frames) * 4;
    if (node->capacity < frames) {
        if (node->cache != NULL)
            SNDMEMI_free(node->cache);
        node->cache = static_cast<float *>(SNDMEMI_alloc(int(bytes)));
        node->capacity = frames;
    }
    if (node->fetch != 0) {
        SFilterNode *up = node->input;
        int made = up->process(up, frames, out, scratch, 1);
        if (made <= 0)
            return made;
        ForwardCopy(out, scratch, bytes);
        ForwardCopy(node->cache, scratch, uint32_t(frames) * 4);
        node->fetch = 0;
        return frames;
    }
    ForwardCopy(out, node->cache, bytes);
    node->served = 1;
    return frames;
}

// The restore of types 8 and 9 (never made a function by Ghidra): frees the buffer at +0x1c (the sum's buffer,
// the tee's cache)
// FUNC_AT(0x00144ca0)
void FUN_00144ca0(SND::FxSumNode *node) {
    void *buffer = node->buffer;
    if (buffer != NULL)
        SNDMEMI_free(buffer);
}

// FUNC_AT(0x00144cc0)
int FUN_00144cc0(SND::FxTeeNode *node) {
    ClearLinks(node);
    node->restore = BufferRestoreAt;
    node->process = TeeProcessAt;
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
            SNDMEMI_free(node->buffer);
        node->capacity = frames;
        node->buffer = static_cast<float *>(SNDMEMI_alloc(frames * 4));
    }
    SFilterNode *up = node->input;
    int made = up->process(up, frames, scratch, out, 1);
    if (made <= 0)
        return made;
    SFilterNode *up2 = node->input2;
    made = up2->process(up2, frames, scratch, node->buffer, 2);
    if (made <= 0)
        return made;
    for (int i = 0; i < frames; i++)
        out[i] = node->buffer[i] + out[i];
    return frames;
}

// FUNC_AT(0x00144e20)
int FUN_00144e20(SND::FxSumNode *node) {
    ClearLinks(node);
    node->restore = BufferRestoreAt;
    node->process = SumProcessAt;
    node->buffer = NULL;
    node->capacity = 0;
    return 0;
}

// The allpass kernel, four samples a step (a count that is not a multiple of 4 runs over to the next 4):
// v = g x d + in + 1e-20; out = -g x v + d; d = v. The third and fourth v are rounded to float before their
// output is formed, the first two are not (the original keeps them on the x87 stack).
// FUNC_AT(0x00144e50)
void FUN_00144e50(SND::FxAllpassNode *node, int count, const float *in, float *out) {
    float *end = out + count;
    float *d = node->buffer + node->position;
    if (!(out < end))
        return;
    do {
        double v0 = double(node->gain) * d[0] + in[0];
        v0 = v0 + kDenormalGuard;
        double v1 = double(node->gain) * d[1] + in[1];
        v1 = v1 + kDenormalGuard;
        double v2 = double(node->gain) * d[2] + in[2];
        v2 = v2 + kDenormalGuard;
        float f2 = float(v2);
        double v3 = double(d[3]) * node->gain + in[3];
        v3 = v3 + kDenormalGuard;
        float f3 = float(v3);
        out[0] = float(v0 * node->negGain + d[0]);
        out[1] = float(v1 * node->negGain + d[1]);
        out[2] = float(double(f2) * node->negGain + d[2]);
        out[3] = float(double(f3) * node->negGain + d[3]);
        d[2] = f2;
        d[0] = float(v0);
        d[3] = f3;
        d[1] = float(v1);
        out += 4;
        in += 4;
        d += 4;
    } while (out < end);
}

// fx2 type 7: pulls input into 'scratch', runs the allpass into 'out' in runs up to the delay's wrap
// FUNC_AT(0x00144f30)
int FUN_00144f30(SND::FxAllpassNode *node, int frames, float *scratch, float *out, int requester) {
    (void)requester;
    SFilterNode *up = node->input;
    if (up != NULL) {
        int made = up->process(up, frames, out, scratch, node->requester);
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
        SNDMEMI_free(node->memory);
        node->memory = NULL;
        node->buffer = NULL;
    }
}

// FUNC_AT(0x00144ff0)
int FUN_00144ff0(SND::FxAllpassNode *node) {
    ClearLinks(node);
    node->restore = AllpassRestoreAt;
    node->process = AllpassProcessAt;
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
        SNDMEMI_free(node->memory);
        node->memory = NULL;
        node->buffer = NULL;
    }
    double gain = double(half) * kOneOver127;
    node->gain = float(gain);
    node->negGain = float(-gain);
    int length = FUN_001465a0(rate, milliseconds);
    node->length = length;
    void *memory = SNDMEMI_alloc(length * 4 + 0x10);
    node->memory = memory;
    float *buffer = reinterpret_cast<float *>((reinterpret_cast<uintptr_t>(memory) + 0xf) >> 4 << 4);   // 16-aligned
    node->buffer = buffer;
    MemClear(buffer, length * 4);
    node->position = 0;
}

// fx2 type 6: out = in x gain
// FUNC_AT(0x001450b0)
int FUN_001450b0(SND::FxGainNode *node, int frames, float *scratch, float *out, int requester) {
    (void)requester;
    SFilterNode *up = node->input;
    if (up != NULL) {
        int made = up->process(up, frames, out, scratch, node->requester);
        if (made <= 0)
            return made;
    }
    for (int i = 0; i < frames; i++)
        out[i] = scratch[i] * node->gain;
    return frames;
}

// FUNC_AT(0x00145160)
int FUN_00145160(SND::FxGainNode *node) {
    ClearLinks(node);
    node->restore = NULL;
    node->process = GainProcessAt;
    node->gain = 0.0f;
    return 0;
}

// FUNC_AT(0x00145190)
void FUN_00145190(SND::FxGainNode *node, const int *params) {
    node->gain = float(double(params[0]) * kOneOver256);
}

// The resonator: y = (in + 1e-20) a0 + 2 b1 r y1 - r^2 y2, run only while rate x 2/pi > bandwidth and
// 0 < frequency < rate / 2 (NaN counts as outside); otherwise the input is copied.
// FUNC_AT(0x001451b0)
void FUN_001451b0(SND::FxResonatorNode *node, int count, const float *in, float *out) {
    int rate = node->rate;
    if (!(double(rate) * kTwoOverPi > node->bandwidth) || !(node->frequency > 0.0f) ||
        !(double(rate >> 1) > node->frequency)) {
        for (int i = 0; i < count; i++)
            out[i] = in[i];
        return;
    }
    for (int i = 0; i < count; i++) {
        double t = (double(in[i]) + kDenormalGuard) * node->a0;
        double br = double(node->b1) * node->radius;
        double p = (br + br) * node->y1;
        double y = (t + p) - (double(node->y2) * node->radius) * node->radius;
        out[i] = float(y);
        node->y2 = node->y1;
        node->y1 = out[i];
    }
}

// fx2 type 5: runs the resonator over what upstream made
// FUNC_AT(0x00145390)
int FUN_00145390(SND::FxResonatorNode *node, int frames, float *scratch, float *out, int requester) {
    (void)requester;
    int made = frames;
    SFilterNode *up = node->input;
    if (up != NULL) {
        made = up->process(up, frames, out, scratch, node->requester);
        if (made <= 0)
            return made;
    }
    FUN_001451b0(node, made, scratch, out);
    return made;
}

// FUNC_AT(0x001453e0)
int FUN_001453e0(SND::FxResonatorNode *node) {
    ClearLinks(node);
    node->restore = NULL;
    node->process = ResonatorProcessAt;
    node->unknown34 = 0.0f;
    node->y1 = 0.0f;
    node->y2 = 0.0f;
    return 0;
}

// params: frequency << 8, rate << 8, bandwidth << 8, gain x 256
// FUNC_AT(0x00145410)
void FUN_00145410(SND::FxResonatorNode *node, const int *params) {
    double gain = double(params[3]) * kOneOver256;
    int frequency = params[0] >> 8;
    int bandwidth = params[2] >> 8;
    int rate = params[1] >> 8;
    float gainF = float(gain);
    double f = frequency;
    node->frequency = float(f);
    double q = bandwidth;
    node->rate = rate;
    node->bandwidth = float(q);
    double r = 1.0 - (q * kPi) / rate;
    node->radius = float(r);
    float rr = float(r * r);
    double w = (f * kTwoPi) / rate;
    double c = FUN_00146640(float(w));
    double b = c * ((double(node->radius) + node->radius) / (double(rr) + 1.0));
    node->b1 = float(b);
    double e = FUN_001465c0(float(-(b * b)));
    double a = e * (1.0 - rr);
    a = a * gainF;
    node->a0 = float(a);
}

// fx2 type 4 (module H's FIR node): two corners / rate, then the FIR design of kind 4
// FUNC_AT(0x00145500)
void FUN_00145500(SND::SFilterFIR8 *node, const int *params) {
    int rate = params[2] >> 8;
    int low = params[0] >> 7;
    int high = params[1] >> 7;
    double r = rate;
    node->fir.cutoff = float(double(low) / r);
    node->fir.cutoff2 = float(double(high) / r);
    FUN_00146930(&node->fir, 4);   // a tail call in the original
}

// fx2 type 2: module H's FIR over what upstream made
// FUNC_AT(0x001455f0)
int FUN_001455f0(SND::SFilterFIR8 *node, int frames, float *scratch, float *out, int requester) {
    (void)requester;
    SFilterNode *up = node->node.input;
    if (up != NULL) {
        int made = up->process(up, frames, out, scratch, node->node.requester);
        if (made <= 0)
            return made;
    }
    FUN_001466e0(&node->fir, frames, scratch, out);
    return frames;
}

// FUNC_AT(0x00145640)
int FUN_00145640(SND::SFilterFIR8 *node) {
    ClearLinks(&node->node);
    node->node.restore = NULL;
    node->node.process = FirProcessAt;
    FUN_001466c0(&node->fir);
    return 0;
}

// params: corner << 8, rate << 8; the FIR design of kind 2
// FUNC_AT(0x00145670)
void FUN_00145670(SND::SFilterFIR8 *node, const int *params) {
    int corner = params[0] >> 8;
    int rate = params[1] >> 8;
    node->fir.cutoff2 = float((double(corner) + corner) / rate);
    FUN_00146930(&node->fir, 2);   // a tail call in the original
}

// One run of a standard tap, at most up to either position's wrap (positions count down; the caller wraps them):
//   a = -g in + delayed;  out = a;  v = g a + in + 1e-30;  lowpass = keep x lowpass + input x v;  write lowpass
// a and v stay double, as on the x87 stack. Hand-written in the original, preserving every register; it runs
// one sample even for a count of 0.
// FUNC_AT(0x00145760)
void MIXI_reverbblock(SND::ReverbTap *tap, int count, const float *in, float *out) {
    float *write = tap->buffer + tap->writePos;
    const float *read = tap->buffer + tap->readPos;
    int i = 0;
    do {
        double a = double(in[i]) * tap->negGain;
        double kept = double(tap->lowpass) * tap->lowpassKeep;
        a = a + read[-1];
        double v = a * tap->gain + in[i];
        write--;
        read--;
        v = v + kReverbGuard;
        double input = tap->lowpassInput;
        out[i] = float(a);
        *write = float(v);
        double lp = kept + input * v;
        *write = float(lp);
        tap->lowpass = float(lp);
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
    double v = x;
    double a = v * kSqrtRatio[0];
    double b = (v * a) * kSqrtRatio[1];
    double c = (v * b) * kSqrtRatio[2];
    double d = (v * c) * kSqrtRatio[3];
    double e = (v * d) * kSqrtRatio[4];
    double f = (v * e) * kSqrtRatio[5];
    double g = (v * f) * kSqrtRatio[6];
    double sum = g + f;
    sum = sum + e;
    sum = sum + d;
    sum = sum + c;
    sum = sum + b;
    sum = a + sum;
    return sum + 1.0;
}

// cos(x) by its series to x^12 after taking 2 pi off while x > 2 pi, returned on the x87 stack (double)
// FUNC_AT(0x00146640)
double FUN_00146640(float x) {
    double v = x;
    if (v > kTwoPi) {
        do
            v = v - kTwoPi;
        while (v > kTwoPi);
    }
    double s = v * v;
    double s2 = s * s;
    double s3 = s2 * s;
    double s4 = s2 * s2;
    double t = 1.0 - s * kCosTerm[0];
    t = t + s2 * kCosTerm[1];
    t = t - s3 * kCosTerm[2];
    t = t + s4 * kCosTerm[3];
    t = t - (s4 * s) * kCosTerm[4];
    t = t + (s3 * s3) * kCosTerm[5];
    return t;
}

// The first prime (by trial division) at or above rate x milliseconds / 1000, at least 3. The products wrap
// as the original's 32-bit IMULs do.
// FUNC_AT(0x0014a260)
int findprime(int rate, int milliseconds) {
    int n = int(uint32_t(rate) * uint32_t(milliseconds)) / 1000;
    if (n < 3)
        n = 3;
    for (;;) {
        int c = 0;
        int limit;
        for (;;) {
            int square = int(uint32_t(c) * uint32_t(c));
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
