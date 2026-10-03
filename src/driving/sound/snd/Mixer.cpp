#include "Mixer.h"

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
// EA's software mixer (docs/driving/sound.md 2.4, 3.6, 4.7), all on the SND thread except the voice setters
// (SNDPLATFORM_* under SoundMutex). Each slice (at most 512 frames): clear the six float accumulators and finish
// the ramps to zero a stopped voice left; per playing voice ramp to changed gains over the first 16 frames, pull
// the rest from the voice's filter chain into scratch B and mixc it into each accumulator and the FX send; run
// the FX hook (unpackerInitFuncs[0]); then per speaker the optional master low pass and the output node into the
// ring. The filters themselves (module H) and the decoders (I) are called at their addresses.
//
// x87 code is double arithmetic in the original's order with a float store per FSTP; a float only moved by FLD
// and FSTP goes through double too (an SNaN comes out quiet, as on the x87), one moved by MOV is copied as bits.
// mixc is SSE: the same intrinsics in the same lanes and order.
// ---------------------------------------------------------------------------------------------------------------

namespace {

using SND::MixFilterNode;
using SND::MixVoice;

inline uint8_t &U8(uint32_t address) { return *(uint8_t *)(uintptr_t)address; }
inline uint16_t &U16(uint32_t address) { return *(uint16_t *)(uintptr_t)address; }
inline uint32_t &U32(uint32_t address) { return *(uint32_t *)(uintptr_t)address; }
inline float &F32(uint32_t address) { return *(float *)(uintptr_t)address; }
inline double K(uint32_t address) { return (double)*(const float *)(uintptr_t)address; }

// Globals (sound.md 2.4)
const uint32_t kPlatformRate = 0x00244cf2;       // u16, 48000
const uint32_t kQuality = 0x00244ed7;            // u8, handed to the resampler and the unpackers
const uint32_t kOutputRate = 0x00245990;
const uint32_t kNumVoices = 0x00245994;          // u8 (the dword is MIX_create's counts)
const uint32_t kNumChannels = 0x00245995;        // u8
const uint32_t kVoiceFreeFunc = 0x00245998;
const uint32_t kOutputStep = 0x0024599c;         // u16, bytes a ring advances per slice
const uint32_t kResamplerMode = 0x0024599e;      // u8, >= 50: the resampler's second mode
const uint32_t kReverbState = 0x0024599f;        // u8: 0 off, 1 fx2, 2 standard
const uint32_t kUnpackerInit = 0x002459a4;       // [32], slot 0 the FX hook
const uint32_t kUnpackerSize = 0x00245a28;       // [32]
const uint32_t kScratchRaw = 0x00245ab0;         // [2]
const uint32_t kScratchA = 0x00245ab8;
const uint32_t kScratchB = 0x00245abc;
const uint32_t kAccumRaw = 0x00245ac0;           // [6]
const uint32_t kAccum = 0x00245ad8;              // [6] 64-byte aligned
const uint32_t kRampToZero = 0x00245af0;         // [6] floats
const uint32_t kFxRampToZero = 0x00245b08;
const uint32_t kOutputList = 0x00245b0c;         // [6] filter list heads (the output nodes)
const uint32_t kMasterFilter = 0x00245b24;       // [6]
const uint32_t kOutputNodes = 0x00245b3c;        // [6] x 0x1c
const uint32_t kMixList = 0x00245be4;
const uint32_t kMixFunc = 0x00245be8;
const uint32_t kFxSend = 0x00245dd0;             // sndfx, 512 floats
const uint32_t kFxIdle = 0x002475d0;             // slices since a voice last fed the FX send
const uint32_t kCODANew = 0x002475f4;
const uint32_t kCODADelete = 0x002475f8;

const uint32_t kOne = 0x00189de8;                // 1.0f
const uint32_t kZero = 0x00189dec;               // 0.0f

inline uint32_t NumChannels() { return U8(kNumChannels); }
inline MixVoice *Voices() { return (MixVoice *)(uintptr_t)U32(kMixList); }
inline float *ScratchA() { return (float *)(uintptr_t)U32(kScratchA); }
inline float *ScratchB() { return (float *)(uintptr_t)U32(kScratchB); }
inline float *Accum(uint32_t i) { return (float *)(uintptr_t)U32(kAccum + i * 4); }
inline void MixCall(int count, float gain, const float *in, float *out) {
    ((void (*)(int, float, const float *, float *))(uintptr_t)U32(kMixFunc))(count, gain, in, out);
}

inline void CopyBits(float *to, const float *from) { memcpy(to, from, 4); }

// FLD dword / FSTP dword with nothing in between
inline float ViaX87(float f) {
    volatile double d = f;
    return (float)d;
}

// The x87 FCOMP/FNSTSW/TEST AH,0x44/JNP pair: true when the two compare equal (ordered)
inline bool Equal(float a, float b) { return (double)a == (double)b; }

// __ftol2's low dword: truncation, except that a value whose nearest integer has a zero low dword (and NaN or
// anything out of the 64-bit range) gives 0 - the routine's fix-up step is skipped then.
inline uint32_t Ftol2Low(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if ((uint32_t)nearest == 0)
        return 0;
    return (uint32_t)(long long)x;
}

inline void *Alloc(int bytes) { return ((void *(*)(int))0x0013f780)(bytes); }            // SNDMEMI_alloc
inline void Free(void *block) { ((void (*)(void *))0x0013f880)(block); }                  // SNDMEMI_free
inline void MemClear(void *p, int bytes) { ((void (*)(void *, int))0x0013f600)(p, bytes); }   // memclr
inline void EnterCritical() { ((void (*)(void))0x0013b950)(); }                           // SNDSYS_entercritical
inline void LeaveCritical() { ((void (*)(void))0x0013b970)(); }                           // SNDSYS_leavecritical

// Module H
inline void AddToFilterList(MixFilterNode **list, void *node) {
    ((int (*)(MixFilterNode **, void *))0x00144460)(list, node);           // SFILTER_addtofilterlist
}
inline void RemoveFromFilterList(MixFilterNode **list, void *node) {
    ((int (*)(MixFilterNode **, void *))0x001444a0)(list, node);           // SFILTER_remove
}
inline void CreateLPFRC(void *node) { ((int (*)(void *))0x00143710)(node); }
inline void ModifyLPFRC(void *node, int *params) { ((void (*)(void *, int *))0x00143740)(node, params); }
inline void CreateHPFFIR8(void *node) { ((int (*)(void *))0x001454d0)(node); }
inline void ModifyHPFFIR8(void *node, int *params) { ((void (*)(void *, int *))0x001455b0)(node, params); }
inline void RsfInit(void *node, int quality, int mode) { ((int (*)(void *, int, int))0x00144920)(node, quality, mode); }
inline void RsfSetPitch(void *node, int pitch) { ((int (*)(void *, int))0x00144700)(node, pitch); }
inline void TimeStretchInit(void *node, int data, int voice) {
    ((int (*)(void *, int, int))0x001443f0)(node, data, voice);
}
inline int TimeStretchSetRatio(void *node, int ratio) { return ((int (*)(void *, int))0x00144300)(node, ratio); }
inline void Ft16Init(void *node) { ((int (*)(void *))0x00144610)(node); }

typedef int (*UnpackerInitFn)(void *node, int *params);

}  // namespace

// The 16-frame ramp's weights, 16/17 .. 1/17
// FUNC_AT(0x001413e0)
void MIXI_interpolateto0(float *gain, float *buffer) {
    for (uint32_t i = 0; i < 16; i++)
        buffer[i] = (float)((double)*gain * K(0x001a7344 - i * 4) + (double)buffer[i]);
    U32((uint32_t)(uintptr_t)gain) = 0;
}

// FUNC_AT(0x001414d0)
void MIXI_interpolatemix(float from, float to, float *in, float *out) {
    // Steps of (to - from) / 17: the first two as d and d + d, the rest as k * d with k from .rdata
    static const uint32_t kMultipliers[14] = { 0x0018a9f4, 0x00189ed4, 0x00189ff4, 0x0018a5dc, 0x0018adfc,
                                               0x0018b5dc, 0x0018b838, 0x00189fec, 0x001a7348, 0x0018f544,
                                               0x0018a5d8, 0x00191b58, 0x0018b560, 0x00191634 };   // 3 .. 16
    double step = ((double)to - (double)from) * K(0x001a7308);
    out[0] = (float)(((double)from + step) * (double)in[0] + (double)out[0]);
    out[1] = (float)(((step + step) + (double)from) * (double)in[1] + (double)out[1]);
    for (uint32_t i = 2; i < 16; i++)
        out[i] = (float)((K(kMultipliers[i - 2]) * step + (double)from) * (double)in[i] + (double)out[i]);
}

// FUNC_AT(0x00141630)
void SNDMIX_setmasterlowpass(float cutoff) {
    int params[3] = { 0, 0, 0 };   // the low pass design: cutoff << 7, rate << 8, 0x100
    for (uint32_t i = 0; i < NumChannels(); i++) {
        uint32_t slot = kMasterFilter + i * 4;
        if ((double)cutoff < K(kOne)) {
            if (U32(slot) == 0) {
                void *node = Alloc(0x28);
                U32(slot) = (uint32_t)(uintptr_t)node;
                CreateLPFRC(node);
                ((MixFilterNode *)(uintptr_t)U32(slot))->upstream = NULL;
            }
            int rate = U16(kPlatformRate);
            params[1] = rate << 8;
            params[0] = (int)(Ftol2Low((double)rate * (double)cutoff) << 7);
            params[2] = 0x100;
            ModifyLPFRC((void *)(uintptr_t)U32(slot), params);
        } else {
            void *node = (void *)(uintptr_t)U32(slot);
            if (node != NULL) {
                Free(node);
                U32(slot) = 0;
            }
        }
    }
}

// The first 16 frames of a voice whose gains changed, ramped from the current gains to the targets.
// FUNC_AT(0x00141710)
int SNDMIXI_volramp(SND::MixVoice *voice) {
    MixFilterNode *chain = voice->chain;
    int made = chain->process(chain, 16, ScratchA(), ScratchB(), 0);
    if (made <= 0) {
        CopyBits(&voice->fx, &voice->fxTarget);
        for (uint32_t i = 0; i < NumChannels(); i++)
            CopyBits(&voice->dry[i], &voice->dryTarget[i]);
        return 0;
    }
    CopyBits(&voice->lastSample, &ScratchB()[15]);
    if (U8(kReverbState) != 0) {
        if (!Equal(voice->fx, voice->fxTarget)) {
            MIXI_interpolatemix(voice->fx, voice->fxTarget, ScratchB(), (float *)(uintptr_t)kFxSend);
            CopyBits(&voice->fx, &voice->fxTarget);
        } else if (!((double)voice->fxTarget == K(kZero))) {
            MixCall(16, voice->fxTarget, ScratchB(), (float *)(uintptr_t)kFxSend);
        }
    }
    for (uint32_t i = 0; i < NumChannels(); i++) {
        if (!Equal(voice->dry[i], voice->dryTarget[i])) {
            MIXI_interpolatemix(voice->dry[i], voice->dryTarget[i], ScratchB(), Accum(i));
            CopyBits(&voice->dry[i], &voice->dryTarget[i]);
        } else if (!((double)voice->dryTarget[i] == K(kZero))) {
            MixCall(16, voice->dryTarget[i], ScratchB(), Accum(i));
        }
    }
    return 16;
}

// FUNC_AT(0x00141880)
void MIX_destroy(void) {
    EnterCritical();
    if (U32(kMixList) != 0) {
        Free((void *)(uintptr_t)U32(kMixList));
        U32(kMixList) = 0;
    }
    for (uint32_t slot = kScratchRaw; slot < kScratchA; slot += 4) {
        if (U32(slot) != 0) {
            Free((void *)(uintptr_t)U32(slot));
            U32(slot) = 0;
        }
    }
    for (uint32_t i = 0; i < NumChannels(); i++) {
        if (U32(kAccumRaw + i * 4) != 0) {
            Free((void *)(uintptr_t)U32(kAccumRaw + i * 4));
            U32(kAccumRaw + i * 4) = 0;
        }
    }
    SNDMIX_setmasterlowpass(2.0f);
    LeaveCritical();
}

// A voice's chain: the unpacker for the sample representation (x bank, bank looping, packet) and, with
// time-stretch data, the time stretch. 'kind' is 1 for a bank sample, 0 for a packet voice.
// FUNC_AT(0x00141910)
void MIX_playinit(int voice, int sampleRep, int kind, int p3, int p4, int stretchData, int p6, int p7, int p8,
                  int loop, int p10, int p11, int requester) {
    (void)p10;
    (void)p11;
    MixVoice *m = &Voices()[voice];
    // The unpacker init's parameter block; it may write the last word back (kept at +0x44)
    int params[10];
    params[6] = 1;
    params[9] = 0;
    m->chain = NULL;
    m->unpacker = NULL;
    m->resampler = NULL;
    m->lowpass = NULL;
    m->highpass = NULL;
    m->stretch = NULL;
    int index = 0;
    if (sampleRep == 8)
        index = 3;
    else if (sampleRep == 7)
        index = 3;
    else if (sampleRep == 9) {
        index = 0;
        params[6] = 0;
    } else if (sampleRep == 10)
        index = 6;
    else if (sampleRep == 4)
        index = 9;
    else if (sampleRep == 14)
        index = 12;
    else if (sampleRep == 15)
        index = 15;
    else if (sampleRep == 16)
        index = 18;
    else if (sampleRep == 0x40)
        index = 21;
    if (kind == 1) {
        if (loop > 0)
            index++;
    } else if (kind == 0) {
        index += 2;
    }
    UnpackerInitFn init = (UnpackerInitFn)(uintptr_t)U32(kUnpackerInit + 4 + index * 4);
    if (init != NULL) {
        MixFilterNode *node = (MixFilterNode *)Alloc((int)U32(kUnpackerSize + 4 + index * 4));
        m->unpacker = node;
        params[0] = p3;
        params[1] = p4;
        params[2] = p6;
        params[3] = p7;
        params[4] = p8;
        params[5] = loop;
        params[7] = voice;
        params[8] = U8(kQuality);
        m->unpacker->restore = NULL;
        m->unpacker->priority = 0xf0;
        m->unpacker->requester = (uint8_t)requester;
        init(m->unpacker, params);
        m->field44 = (uint32_t)params[9];
        AddToFilterList(&m->chain, m->unpacker);
    }
    if (stretchData != 0) {
        int owner = voice;
        if (kind != 0)
            owner = -1;
        MixFilterNode *node = (MixFilterNode *)Alloc(0x1828);
        m->stretch = node;
        node->restore = NULL;
        m->stretch->priority = 200;
        TimeStretchInit(m->stretch, stretchData, owner);
        AddToFilterList(&m->chain, m->stretch);
    }
    m->state = 1;
}

// FUNC_AT(0x00141ad0)
void MIX_play(int voice) {
    MixVoice *m = &Voices()[voice];
    U32((uint32_t)(uintptr_t)&m->lastSample) = 0;
    m->gainsChanged = 0;
    CopyBits(&m->fx, &m->fxTarget);
    for (uint32_t i = 0; i < NumChannels(); i++)
        CopyBits(&m->dry[i], &m->dryTarget[i]);
    m->state = 2;
}

// The voice's last sample, at its current gains, is left to ramp to zero over the next slice; the chain is freed.
// FUNC_AT(0x00141b20)
void MIX_stop(int voice) {
    MixVoice *m = &Voices()[voice];
    uint32_t channels = NumChannels();
    F32(kFxRampToZero) = (float)((double)m->fx * (double)m->lastSample + (double)F32(kFxRampToZero));
    if (channels != 0) {
        for (uint32_t i = 0; i < NumChannels(); i++) {
            uint32_t ramp = kRampToZero + i * 4;
            F32(ramp) = (float)((double)m->dry[i] * (double)m->lastSample + (double)F32(ramp));
        }
    }
    MixFilterNode *next;
    do {
        MixFilterNode *node = m->chain;
        if (node->restore != NULL)
            node->restore(node);
        node = m->chain;
        next = node->upstream;
        Free(node);
        m->chain = next;
    } while (next != NULL);
    m->state = 0;
}

// FUNC_AT(0x00141bb0)
void SNDMIX_setdrygain(int voice, int speaker, float gain) {
    float *slot = (float *)(uintptr_t)(U32(kMixList) + (uint32_t)(voice * 24 + speaker) * 4 + 0x1c);
    *slot = ViaX87(gain);
    Voices()[voice].gainsChanged = 1;
}

// FUNC_AT(0x00141be0)
void MIX_setfxlevel(int voice, int send, float level) {
    bool standard = U8(kReverbState + (uint32_t)send) == 2;
    float *slot = (float *)(uintptr_t)(U32(kMixList) + (uint32_t)(voice * 24 + send) * 4 + 0x38);
    if (standard)
        *slot = (float)((double)level * K(0x0018a8e8));   // x 1.5
    else
        *slot = ViaX87(level);
    Voices()[voice].gainsChanged = 1;
}

// FUNC_AT(0x00141c20)
void MIX_create(const SND::MixCreateParams *params) {
    SND::CODASetNew((void *)0x00141860);        // the thunks to SNDMEMI_alloc / SNDMEMI_free
    SND::CODASetDelete((void *)0x00141870);
    U8(kReverbState) = 0;
    U32(kOutputRate) = params->rate;
    U32(kNumVoices) = params->counts;
    U32(kVoiceFreeFunc) = (uint32_t)(uintptr_t)params->voiceFree;
    EnterCritical();
    for (uint32_t slot = kScratchA; slot < kAccumRaw; slot += 4) {
        uint32_t raw = (uint32_t)(uintptr_t)Alloc(0x40bc);
        U32(slot - 8) = raw;
        U32(slot) = raw + 8;
        while ((U32(slot) & 0x3f) != 0)
            U32(slot) = U32(slot) + 4;
    }
    for (uint32_t i = 0; i < NumChannels(); i++) {
        uint32_t raw = (uint32_t)(uintptr_t)Alloc(0x840);
        U32(kAccumRaw + i * 4) = raw;
        U32(kAccum + i * 4) = raw;
        while ((U32(kAccum + i * 4) & 0x3f) != 0)
            U32(kAccum + i * 4) = U32(kAccum + i * 4) + 4;
    }
    if (U8(kNumVoices) != 0) {
        void *list = Alloc((int)(U8(kNumVoices) * 0x60u));
        int bytes = (int)(U8(kNumVoices) * 0x60u);
        U32(kMixList) = (uint32_t)(uintptr_t)list;
        MemClear(list, bytes);
    }
    LeaveCritical();
    MIXI_initunpack16();
    MIXI_initunpackxa();
    MIXI_initunpackmt();
    U32(kMixFunc) = 0x00143b40;                 // mixc, by its original address
    U16(kOutputStep) = 0x400;
    for (uint32_t i = 0; i < NumChannels(); i++) {
        MixFilterNode *node = (MixFilterNode *)(uintptr_t)(kOutputNodes + i * 0x1c);
        U32(kOutputList + i * 4) = 0;
        Ft16Init(node);
        node->priority = 0;
        AddToFilterList((MixFilterNode **)(uintptr_t)(kOutputList + i * 4), node);
        MemClear(Accum(i), 0x800);
    }
}

// FUNC_AT(0x00141db0)
void MIX_audioslice(int16_t **outputs, int frames) {
    if (U8(kReverbState) != 0 && !Equal(F32(kFxRampToZero), F32(kZero)))
        MIXI_interpolateto0(&F32(kFxRampToZero), (float *)(uintptr_t)kFxSend);
    for (uint32_t i = 0; i < NumChannels(); i++) {
        MemClear(Accum(i), frames * 4);
        if (!Equal(F32(kRampToZero + i * 4), F32(kZero)))
            MIXI_interpolateto0(&F32(kRampToZero + i * 4), Accum(i));
    }
    for (uint32_t v = 0; v < U8(kNumVoices); v++) {
        MixVoice *m = (MixVoice *)(uintptr_t)(U32(kMixList) + v * 0x60);
        if (m->state != 2)
            continue;
        int ramped, remaining;
        if (m->gainsChanged != 0) {
            ramped = SNDMIXI_volramp(m);
            remaining = frames - ramped;
            m->gainsChanged = 0;
        } else {
            ramped = 0;
            remaining = frames;
        }
        if (remaining == 0)
            continue;
        int made = m->chain->process(m->chain, remaining, ScratchA(), ScratchB(), 0);
        if (made < 0) {
            MIX_stop((int)v);
            ((void (*)(int))(uintptr_t)U32(kVoiceFreeFunc))((int)v);
            continue;
        }
        if (made == 0)
            continue;
        CopyBits(&m->lastSample, &ScratchB()[made - 1]);
        for (uint32_t i = 0; i < NumChannels(); i++) {
            if (!Equal(m->dryTarget[i], F32(kZero)))
                MixCall(made, m->dryTarget[i], ScratchB(), Accum(i) + ramped);
        }
        if (U8(kReverbState) != 0 && !Equal(m->fxTarget, F32(kZero))) {
            MixCall(made, m->fxTarget, ScratchB(), (float *)(uintptr_t)kFxSend + ramped);
            U32(kFxIdle) = 0;
        }
    }
    void (*fxHook)(int) = (void (*)(int))(uintptr_t)U32(kUnpackerInit);
    if (fxHook != NULL)
        fxHook(frames);
    for (uint32_t i = 0; i < NumChannels(); i++) {
        MixFilterNode *master = (MixFilterNode *)(uintptr_t)U32(kMasterFilter + i * 4);
        float *accum = Accum(i);
        MixFilterNode *output = (MixFilterNode *)(uintptr_t)U32(kOutputList + i * 4);
        if (master != NULL) {
            master->process(master, frames, accum, ScratchA(), 0);
            output->process(output, frames, ScratchA(), (float *)outputs[i], 0);
        } else {
            output->process(output, frames, accum, (float *)outputs[i], 0);
        }
    }
}

// FUNC_AT(0x00142050)
void MIX_audio(int16_t **outputs, int frames) {
    int16_t *rings[6];   // NUM output channels (6) pointers, advanced a slice at a time
    int channels = (int)NumChannels();
    for (int i = 0; i < channels; i++)
        rings[i] = outputs[i];
    for (int remaining = frames; remaining > 0; remaining -= 0x200) {
        MIX_audioslice(rings, remaining > 0x200 ? 0x200 : remaining);
        channels = (int)NumChannels();
        for (int i = 0; i < channels; i++)
            rings[i] = (int16_t *)((uint8_t *)rings[i] + U16(kOutputStep));
    }
}

// FUNC_AT(0x001420c0)
void MIX_setpitch(int voice, int pitch) {
    MixVoice *m = (MixVoice *)(uintptr_t)(U32(kMixList) + (uint32_t)voice * 0x60);
    if (pitch > 0x40000)
        pitch = 0x40000;
    int mode = 0;
    if (m->resampler == NULL) {
        MixFilterNode *node = (MixFilterNode *)Alloc(0x3c);
        m->resampler = node;
        if (U8(kResamplerMode) >= 0x32)
            mode = 1;
        node->restore = NULL;
        m->resampler->priority = 0xa0;
        RsfInit(m->resampler, U8(kQuality), mode);
        AddToFilterList(&m->chain, m->resampler);
    }
    RsfSetPitch(m->resampler, pitch);
}

// out[i] += in[i] x gain, from the end: scalar until the count is a multiple of 16 with both buffers 16-byte
// aligned, then 16 at a time. As in the original a count of 0 is not checked for.
// FUNC_AT(0x00143b40)
void mixc(int count, float gain, const float *in, float *out) {
    __m128 g = _mm_set_ss(gain);
    g = _mm_shuffle_ps(g, g, 0);
    int n = count;
    for (;;) {
        if ((n & 0xf) == 0 && ((uintptr_t)in & 0xf) == 0 && ((uintptr_t)out & 0xf) == 0)
            break;
        __m128 v = _mm_mul_ss(_mm_load_ss(in + n - 1), g);
        v = _mm_add_ss(v, _mm_load_ss(out + n - 1));
        _mm_store_ss(out + n - 1, v);
        n -= 1;
        if (n == 0)
            return;
    }
    do {
        const float *i = in + n - 16;
        float *o = out + n - 16;
        __m128 a = _mm_load_ps(i), b = _mm_load_ps(i + 4), c = _mm_load_ps(i + 8), d = _mm_load_ps(i + 12);
        a = _mm_mul_ps(a, g);
        b = _mm_mul_ps(b, g);
        c = _mm_mul_ps(c, g);
        d = _mm_mul_ps(d, g);
        a = _mm_add_ps(a, _mm_load_ps(o));
        b = _mm_add_ps(b, _mm_load_ps(o + 4));
        c = _mm_add_ps(c, _mm_load_ps(o + 8));
        d = _mm_add_ps(d, _mm_load_ps(o + 12));
        _mm_store_ps(o, a);
        _mm_store_ps(o + 4, b);
        _mm_store_ps(o + 8, c);
        _mm_store_ps(o + 12, d);
        n -= 16;
    } while (n > 0);
}

// The unpacker tables (module H's inits): MicroTalk plain and packet
// FUNC_AT(0x00144630)
void MIXI_initunpackmt(void) {
    U32(0x002459cc) = 0x00145a70;   // slot 10: SFILTER_unpackmtfinit
    U32(0x00245a50) = 0xd68;
    U32(0x002459d4) = 0x00145970;   // slot 12: SFILTER_unpackmtpfinit
    U32(0x00245a58) = 0xd74;
}

// EA-XA plain, looping, packet
// FUNC_AT(0x00144660)
void MIXI_initunpackxa(void) {
    U32(0x002459c0) = 0x00145f50;   // slot 7: SFILTER_unpackxafinit
    U32(0x00245a44) = 0x2c;
    U32(0x00245a48) = 0x44;
    U32(0x002459c4) = 0x00145e40;   // slot 8: SFILTER_unpackxalfinit
    U32(0x002459c8) = 0x00145c10;   // slot 9: SFILTER_unpackxapfinit
    U32(0x00245a4c) = 0x38;
}

// PCM16 plain, looping, packet
// FUNC_AT(0x001446a0)
void MIXI_initunpack16(void) {
    U32(0x002459b4) = 0x00146280;   // slot 4: SFILTER_unpackfinit
    U32(0x00245a38) = 0x2c;
    U32(0x002459b8) = 0x001461a0;   // slot 5: SFILTER_unpacklfinit
    U32(0x00245a3c) = 0x30;
    U32(0x002459bc) = 0x001460e0;   // slot 6: SFILTER_unpackpfinit
    U32(0x00245a40) = 0x34;
}

// FUNC_AT(0x001446e0)
void SND::CODASetNew(void *allocate) {
    U32(kCODANew) = (uint32_t)(uintptr_t)allocate;
}

// FUNC_AT(0x001446f0)
void SND::CODASetDelete(void *release) {
    U32(kCODADelete) = (uint32_t)(uintptr_t)release;
}

// 'cutoff' is a fraction of the platform rate; 1 or more (or NaN) removes the low pass.
// FUNC_AT(0x00144af0)
void MIX_setlowpass(int voice, float cutoff) {
    bool below = (double)cutoff < K(kOne);
    MixVoice *m = (MixVoice *)(uintptr_t)(U32(kMixList) + (uint32_t)voice * 0x60);
    if (!below) {
        if (m->lowpass != NULL) {
            RemoveFromFilterList(&m->chain, m->lowpass);
            Free(m->lowpass);
            m->lowpass = NULL;
        }
        return;
    }
    if (m->lowpass == NULL) {
        MixFilterNode *node = (MixFilterNode *)Alloc(0x28);
        m->lowpass = node;
        node->restore = NULL;
        m->lowpass->priority = 0x28;
        CreateLPFRC(m->lowpass);
        AddToFilterList(&m->chain, m->lowpass);
    }
    int params[3];
    int rate = U16(kPlatformRate);
    params[1] = rate << 8;
    params[0] = (int)(Ftol2Low((double)rate * (double)cutoff) << 7);
    params[2] = 0x100;
    ModifyLPFRC(m->lowpass, params);
}

// 'cutoff' in Hz; 0 or less removes the high pass.
// FUNC_AT(0x001464c0)
void MIX_sethighpass(int voice, int cutoff) {
    MixVoice *m = (MixVoice *)(uintptr_t)(U32(kMixList) + (uint32_t)voice * 0x60);
    if (cutoff > 0) {
        if (m->highpass == NULL) {
            MixFilterNode *node = (MixFilterNode *)Alloc(0x58);
            m->highpass = node;
            node->restore = NULL;
            m->highpass->priority = 0x50;
            CreateHPFFIR8(m->highpass);
            AddToFilterList(&m->chain, m->highpass);
        }
        int params[2];
        params[1] = (int)U16(kPlatformRate) << 8;
        params[0] = cutoff << 8;
        ModifyHPFFIR8(m->highpass, params);
        return;
    }
    if (m->highpass != NULL) {
        RemoveFromFilterList(&m->chain, m->highpass);
        Free(m->highpass);
        m->highpass = NULL;
    }
}

// FUNC_AT(0x00146570)
int MIX_settimemult(int voice, int ratio) {
    MixFilterNode *stretch = ((MixVoice *)(uintptr_t)(U32(kMixList) + (uint32_t)voice * 0x60))->stretch;
    if (stretch == NULL)
        return 0;
    return TimeStretchSetRatio(stretch, ratio);   // a tail call in the original
}
