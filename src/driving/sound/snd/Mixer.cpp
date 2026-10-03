#include "Mixer.h"
#include "SndGlobals.h"

#include <bit>
#include <math.h>
#include <stddef.h>
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

using SND::SFilterNode;
using SND::MixVoice;

// The 16-frame ramp's weights, 16/17 .. 1/17 (the original's .rdata 0x001a7344 down to 0x001a7308), and the
// steps' 1/17
constexpr float kRampWeight[16] = { 16.0f / 17, 15.0f / 17, 14.0f / 17, 13.0f / 17, 12.0f / 17, 11.0f / 17,
                                    10.0f / 17, 9.0f / 17,  8.0f / 17,  7.0f / 17,  6.0f / 17,  5.0f / 17,
                                    4.0f / 17,  3.0f / 17,  2.0f / 17,  1.0f / 17 };
constexpr uint32_t kRampWeightBits[16] = { 0x3f70f0f1, 0x3f61e1e2, 0x3f52d2d3, 0x3f43c3c4, 0x3f34b4b5, 0x3f25a5a6,
                                           0x3f169697, 0x3f078788, 0x3ef0f0f1, 0x3ed2d2d3, 0x3eb4b4b5, 0x3e969697,
                                           0x3e70f0f1, 0x3e34b4b5, 0x3df0f0f1, 0x3d70f0f1 };
constexpr bool RampWeightsExact() {
    for (int i = 0; i < 16; i++)
        if (std::bit_cast<uint32_t>(kRampWeight[i]) != kRampWeightBits[i])
            return false;
    return true;
}
static_assert(RampWeightsExact(), "the ramp weights are the original's");
constexpr float kOneSeventeenth = 1.0f / 17;
static_assert(std::bit_cast<uint32_t>(kOneSeventeenth) == 0x3d70f0f1, "the original's 0x001a7308");

// The functions MIX_create stores: the originals' addresses, as the original stores them (they jump to ours)
#define AllocThunk ((void *)0x00141860)               // SNDMEMI_allocthunk (CODA_New)
#define FreeThunk ((void *)0x00141870)                // SNDMEMI_freethunk (CODA_Delete)
#define MixcAt ((SND::MixFn)0x00143b40)               // mixc

// The unpacker inits (module H) MIXI_initunpack* puts in the table
#define UnpackfInit ((SND::SFilterUnpackInit)0x00146280)      // SFILTER_unpackfinit
#define UnpacklfInit ((SND::SFilterUnpackInit)0x001461a0)     // SFILTER_unpacklfinit
#define UnpackpfInit ((SND::SFilterUnpackInit)0x001460e0)     // SFILTER_unpackpfinit
#define UnpackXafInit ((SND::SFilterUnpackInit)0x00145f50)    // SFILTER_unpackxafinit
#define UnpackXalfInit ((SND::SFilterUnpackInit)0x00145e40)   // SFILTER_unpackxalfinit
#define UnpackXapfInit ((SND::SFilterUnpackInit)0x00145c10)   // SFILTER_unpackxapfinit
#define UnpackMtfInit ((SND::SFilterUnpackInit)0x00145a70)    // SFILTER_unpackmtfinit
#define UnpackMtpfInit ((SND::SFilterUnpackInit)0x00145970)   // SFILTER_unpackmtpfinit

// The originals called from here. memclr is not ours; module H's are, but take their own node types (Filters.h),
// so they are called at the originals' addresses, typed with the node head the mixer holds.
#define MemClear ((void (*)(void *, int))0x0013f600)                                // memclr
#define AddToFilterList ((int (*)(SFilterNode **, SFilterNode *))0x00144460)        // SFILTER_addtofilterlist
#define RemoveFromFilterList ((int (*)(SFilterNode **, SFilterNode *))0x001444a0)   // SFILTER_remove
#define CreateLPFRC ((int (*)(SFilterNode *))0x00143710)                            // SFILTER_createLPFRC
#define ModifyLPFRC ((void (*)(SFilterNode *, int *))0x00143740)                    // SFILTER_modifyLPFRC
#define CreateHPFFIR8 ((int (*)(SFilterNode *))0x001454d0)                          // SFILTER_createHPFFIR8
#define ModifyHPFFIR8 ((void (*)(SFilterNode *, int *))0x001455b0)                  // SFILTER_modifyHPFFIR8
#define RsfInit ((int (*)(SFilterNode *, int, int))0x00144920)                      // SFILTER_rsfinit
#define RsfSetPitch ((int (*)(SFilterNode *, int))0x00144700)                       // SFILTER_rsfsetpitch
#define TimeStretchInit ((int (*)(SFilterNode *, int, int))0x001443f0)              // SFILTER_timestretchinit
#define TimeStretchSetRatio ((int (*)(SFilterNode *, int))0x00144300)               // SFILTER_timestretchsetratio
#define Ft16Init ((int (*)(SFilterNode *))0x00144610)                               // SFILTER_ft16init

// FLD dword / FSTP dword with nothing in between
float ViaX87(float f) {
    volatile double d = f;
    return float(d);
}

// __ftol2's low dword: truncation, except that a value whose nearest integer has a zero low dword (and NaN or
// anything out of the 64-bit range) gives 0 - the routine's fix-up step is skipped then.
uint32_t Ftol2Low(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if ((uint32_t)nearest == 0)
        return 0;
    return (uint32_t)(long long)x;
}

// A low pass's design words: the cutoff (a fraction of the platform rate) << 7, the rate << 8, 0x100
void LowpassParams(int *params, float cutoff) {
    int rate = PlatformRate;
    params[1] = rate << 8;
    params[0] = int(Ftol2Low(double(rate) * cutoff) << 7);
    params[2] = 0x100;
}

}  // namespace

// The 16-frame ramp of a stopped voice's last sample to zero
// FUNC_AT(0x001413e0)
void MIXI_interpolateto0(float *gain, float *buffer) {
    for (uint32_t i = 0; i < 16; i++)
        buffer[i] = float(double(*gain) * kRampWeight[i] + buffer[i]);
    *gain = 0.0f;
}

// FUNC_AT(0x001414d0)
void MIXI_interpolatemix(float from, float to, float *in, float *out) {
    // Steps of (to - from) / 17: the first two as d and d + d, the rest as k * d (k = 3 .. 16)
    double step = (double(to) - from) * kOneSeventeenth;
    out[0] = float((from + step) * in[0] + out[0]);
    out[1] = float(((step + step) + from) * in[1] + out[1]);
    for (int i = 2; i < 16; i++)
        out[i] = float((double(i + 1) * step + from) * in[i] + out[i]);
}

// FUNC_AT(0x00141630)
void SNDMIX_setmasterlowpass(float cutoff) {
    int params[3] = { 0, 0, 0 };
    for (uint32_t i = 0; i < SndMix.counts.channels; i++) {
        if (cutoff < 1.0f) {
            if (SndMix.masterFilter[i] == NULL) {
                SFilterNode *node = static_cast<SFilterNode *>(SNDMEMI_alloc(0x28));
                SndMix.masterFilter[i] = node;
                CreateLPFRC(node);
                SndMix.masterFilter[i]->input = NULL;
            }
            LowpassParams(params, cutoff);
            ModifyLPFRC(SndMix.masterFilter[i], params);
        } else {
            SFilterNode *node = SndMix.masterFilter[i];
            if (node != NULL) {
                SNDMEMI_free(node);
                SndMix.masterFilter[i] = NULL;
            }
        }
    }
}

// The first 16 frames of a voice whose gains changed, ramped from the current gains to the targets. A gain that is
// unordered with its target (NaN) counts as changed.
// FUNC_AT(0x00141710)
int SNDMIXI_volramp(SND::MixVoice *voice) {
    SFilterNode *chain = voice->chain;
    int made = chain->process(chain, 16, SndMix.scratch[0], SndMix.scratch[1], 0);
    if (made <= 0) {
        voice->fx = voice->fxTarget;
        for (uint32_t i = 0; i < SndMix.counts.channels; i++)
            voice->dry[i] = voice->dryTarget[i];
        return 0;
    }
    voice->lastSample = SndMix.scratch[1][15];
    if (SndMix.reverbState != SND::kReverbOff) {
        if (!(voice->fx == voice->fxTarget)) {
            MIXI_interpolatemix(voice->fx, voice->fxTarget, SndMix.scratch[1], SndMix.fxSend);
            voice->fx = voice->fxTarget;
        } else if (!(voice->fxTarget == 0.0f)) {
            SndMix.mixFunc(16, voice->fxTarget, SndMix.scratch[1], SndMix.fxSend);
        }
    }
    for (uint32_t i = 0; i < SndMix.counts.channels; i++) {
        if (!(voice->dry[i] == voice->dryTarget[i])) {
            MIXI_interpolatemix(voice->dry[i], voice->dryTarget[i], SndMix.scratch[1], SndMix.accum[i]);
            voice->dry[i] = voice->dryTarget[i];
        } else if (!(voice->dryTarget[i] == 0.0f)) {
            SndMix.mixFunc(16, voice->dryTarget[i], SndMix.scratch[1], SndMix.accum[i]);
        }
    }
    return 16;
}

// FUNC_AT(0x00141880)
void MIX_destroy(void) {
    SNDSYS_entercritical();
    if (SndMix.voices != NULL) {
        SNDMEMI_free(SndMix.voices);
        SndMix.voices = NULL;
    }
    for (void *&raw : SndMix.scratchRaw) {
        if (raw != NULL) {
            SNDMEMI_free(raw);
            raw = NULL;
        }
    }
    for (uint32_t i = 0; i < SndMix.counts.channels; i++) {
        if (SndMix.accumRaw[i] != NULL) {
            SNDMEMI_free(SndMix.accumRaw[i]);
            SndMix.accumRaw[i] = NULL;
        }
    }
    SNDMIX_setmasterlowpass(2.0f);
    SNDSYS_leavecritical();
}

// A voice's chain: the unpacker for the sample representation (x bank, bank looping, packet) and, with
// time-stretch data, the time stretch. 'kind' is 1 for a bank sample, 0 for a packet voice.
// FUNC_AT(0x00141910)
void MIX_playinit(int voice, int sampleRep, int kind, const void *data, int p4, int stretchData, int p6, int p7,
                  int p8, int loop, int p10, int p11, int requester) {
    (void)p10;
    (void)p11;
    MixVoice *m = &SndMix.voices[voice];
    // The unpacker init's description of the sample; a bank unpacker's init writes its frame getter back (kept at
    // +0x44)
    SND::UnpackInfo info;
    info.flag = 1;
    info.getFrame = NULL;
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
        info.flag = 0;
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
    SND::SFilterUnpackInit init = SndMix.unpackerInit[index];
    if (init != NULL) {
        SFilterNode *node = static_cast<SFilterNode *>(SNDMEMI_alloc(SndMix.unpackerSize[index]));
        m->unpacker = node;
        info.data = data;
        info.unknown04 = p4;
        info.unknown08 = p6;
        info.frames = p7;
        info.loopStart = p8;
        info.loopEnd = loop;
        info.voice = voice;
        info.quality = MixQuality;
        m->unpacker->restore = NULL;
        m->unpacker->priority = 0xf0;
        m->unpacker->requester = uint8_t(requester);
        init(m->unpacker, &info);
        m->getFrame = info.getFrame;
        AddToFilterList(&m->chain, m->unpacker);
    }
    if (stretchData != 0) {
        int owner = voice;
        if (kind != 0)
            owner = -1;
        SFilterNode *node = static_cast<SFilterNode *>(SNDMEMI_alloc(0x1828));
        m->stretch = node;
        node->restore = NULL;
        m->stretch->priority = 200;
        TimeStretchInit(m->stretch, stretchData, owner);
        AddToFilterList(&m->chain, m->stretch);
    }
    m->state = SND::kMixVoiceInitialised;
}

// FUNC_AT(0x00141ad0)
void MIX_play(int voice) {
    MixVoice *m = &SndMix.voices[voice];
    m->lastSample = 0.0f;
    m->gainsChanged = 0;
    m->fx = m->fxTarget;
    for (uint32_t i = 0; i < SndMix.counts.channels; i++)
        m->dry[i] = m->dryTarget[i];
    m->state = SND::kMixVoicePlaying;
}

// The voice's last sample, at its current gains, is left to ramp to zero over the next slice; the chain is freed.
// FUNC_AT(0x00141b20)
void MIX_stop(int voice) {
    MixVoice *m = &SndMix.voices[voice];
    uint32_t channels = SndMix.counts.channels;
    SndMix.fxRampToZero = float(double(m->fx) * m->lastSample + SndMix.fxRampToZero);
    if (channels != 0) {
        for (uint32_t i = 0; i < SndMix.counts.channels; i++)
            SndMix.rampToZero[i] = float(double(m->dry[i]) * m->lastSample + SndMix.rampToZero[i]);
    }
    SFilterNode *next;
    do {
        SFilterNode *node = m->chain;
        if (node->restore != NULL)
            node->restore(node);
        node = m->chain;
        next = node->input;
        SNDMEMI_free(node);
        m->chain = next;
    } while (next != NULL);
    m->state = SND::kMixVoiceFree;
}

// FUNC_AT(0x00141bb0)
void SNDMIX_setdrygain(int voice, int speaker, float gain) {
    SndMix.voices[voice].dryTarget[speaker] = ViaX87(gain);
    SndMix.voices[voice].gainsChanged = 1;
}

// The send's level, x 1.5 for the standard reverb. Only send 0 is used; the original indexes the reverb state and
// the voice's fx target by it all the same.
// FUNC_AT(0x00141be0)
void MIX_setfxlevel(int voice, int send, float level) {
    bool standard = (&SndMix.reverbState)[send] == SND::kReverbStandard;
    float *slot = &(&SndMix.voices[voice].fxTarget)[send];
    if (standard)
        *slot = level * 1.5f;
    else
        *slot = ViaX87(level);
    SndMix.voices[voice].gainsChanged = 1;
}

// FUNC_AT(0x00141c20)
void MIX_create(const SND::MixCreateParams *params) {
    SND::CODASetNew(AllocThunk);
    SND::CODASetDelete(FreeThunk);
    SndMix.reverbState = SND::kReverbOff;
    SndMix.outputRate = params->rate;
    SndMix.counts = params->counts;
    SndMix.voiceFree = params->voiceFree;
    SNDSYS_entercritical();
    for (int i = 0; i < 2; i++) {
        void *raw = SNDMEMI_alloc(0x40bc);
        SndMix.scratchRaw[i] = raw;
        SndMix.scratch[i] = static_cast<float *>(raw) + 2;
        while (((uintptr_t)SndMix.scratch[i] & 0x3f) != 0)
            SndMix.scratch[i]++;
    }
    for (uint32_t i = 0; i < SndMix.counts.channels; i++) {
        void *raw = SNDMEMI_alloc(0x840);
        SndMix.accumRaw[i] = raw;
        SndMix.accum[i] = static_cast<float *>(raw);
        while (((uintptr_t)SndMix.accum[i] & 0x3f) != 0)
            SndMix.accum[i]++;
    }
    if (SndMix.counts.voices != 0) {
        void *list = SNDMEMI_alloc(SndMix.counts.voices * int(sizeof(MixVoice)));
        int bytes = SndMix.counts.voices * int(sizeof(MixVoice));
        SndMix.voices = static_cast<MixVoice *>(list);
        MemClear(list, bytes);
    }
    SNDSYS_leavecritical();
    MIXI_initunpack16();
    MIXI_initunpackxa();
    MIXI_initunpackmt();
    SndMix.mixFunc = MixcAt;
    SndMix.outputStep = 0x400;
    for (uint32_t i = 0; i < SndMix.counts.channels; i++) {
        SFilterNode *node = &SndMix.outputNodes[i];
        SndMix.outputList[i] = NULL;
        Ft16Init(node);
        node->priority = 0;
        AddToFilterList(&SndMix.outputList[i], node);
        MemClear(SndMix.accum[i], 0x800);
    }
}

// FUNC_AT(0x00141db0)
void MIX_audioslice(int16_t **outputs, int frames) {
    // (a ramp that is NaN counts as not yet done)
    if (SndMix.reverbState != SND::kReverbOff && !(SndMix.fxRampToZero == 0.0f))
        MIXI_interpolateto0(&SndMix.fxRampToZero, SndMix.fxSend);
    for (uint32_t i = 0; i < SndMix.counts.channels; i++) {
        MemClear(SndMix.accum[i], frames * 4);
        if (!(SndMix.rampToZero[i] == 0.0f))
            MIXI_interpolateto0(&SndMix.rampToZero[i], SndMix.accum[i]);
    }
    for (uint32_t v = 0; v < SndMix.counts.voices; v++) {
        MixVoice *m = &SndMix.voices[v];
        if (m->state != SND::kMixVoicePlaying)
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
        int made = m->chain->process(m->chain, remaining, SndMix.scratch[0], SndMix.scratch[1], 0);
        if (made < 0) {
            MIX_stop(int(v));
            SndMix.voiceFree(int(v));
            continue;
        }
        if (made == 0)
            continue;
        m->lastSample = SndMix.scratch[1][made - 1];
        for (uint32_t i = 0; i < SndMix.counts.channels; i++) {
            if (!(m->dryTarget[i] == 0.0f))
                SndMix.mixFunc(made, m->dryTarget[i], SndMix.scratch[1], SndMix.accum[i] + ramped);
        }
        if (SndMix.reverbState != SND::kReverbOff && !(m->fxTarget == 0.0f)) {
            SndMix.mixFunc(made, m->fxTarget, SndMix.scratch[1], SndMix.fxSend + ramped);
            SndMix.fxIdle = 0;
        }
    }
    SND::MixFxHook fxHook = SndMix.fxHook;
    if (fxHook != NULL)
        fxHook(frames);
    for (uint32_t i = 0; i < SndMix.counts.channels; i++) {
        SFilterNode *master = SndMix.masterFilter[i];
        float *accum = SndMix.accum[i];
        SFilterNode *output = SndMix.outputList[i];
        // (the output stage, SFILTER_ft24_32, writes 16-bit samples through its float pointer)
        if (master != NULL) {
            master->process(master, frames, accum, SndMix.scratch[0], 0);
            output->process(output, frames, SndMix.scratch[0], reinterpret_cast<float *>(outputs[i]), 0);
        } else {
            output->process(output, frames, accum, reinterpret_cast<float *>(outputs[i]), 0);
        }
    }
}

// FUNC_AT(0x00142050)
void MIX_audio(int16_t **outputs, int frames) {
    int16_t *rings[6];   // NUM output channels (6) pointers, advanced a slice at a time
    int channels = SndMix.counts.channels;
    for (int i = 0; i < channels; i++)
        rings[i] = outputs[i];
    for (int remaining = frames; remaining > 0; remaining -= 0x200) {
        MIX_audioslice(rings, remaining > 0x200 ? 0x200 : remaining);
        channels = SndMix.counts.channels;
        for (int i = 0; i < channels; i++)
            rings[i] = reinterpret_cast<int16_t *>(reinterpret_cast<uint8_t *>(rings[i]) + SndMix.outputStep);
    }
}

// FUNC_AT(0x001420c0)
void MIX_setpitch(int voice, int pitch) {
    MixVoice *m = &SndMix.voices[voice];
    if (pitch > 0x40000)
        pitch = 0x40000;
    int mode = 0;
    if (m->resampler == NULL) {
        SFilterNode *node = static_cast<SFilterNode *>(SNDMEMI_alloc(0x3c));
        m->resampler = node;
        if (SndMix.resamplerMode >= 50)
            mode = 1;
        node->restore = NULL;
        m->resampler->priority = 0xa0;
        RsfInit(m->resampler, MixQuality, mode);
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

// The unpacker tables (module H's inits), by MIX_playinit's index (unpackerInitFuncs' slot - 1): MicroTalk plain
// and packet
// FUNC_AT(0x00144630)
void MIXI_initunpackmt(void) {
    SndMix.unpackerInit[9] = UnpackMtfInit;      // slot 10
    SndMix.unpackerSize[9] = 0xd68;
    SndMix.unpackerInit[11] = UnpackMtpfInit;    // slot 12
    SndMix.unpackerSize[11] = 0xd74;
}

// EA-XA plain, looping, packet
// FUNC_AT(0x00144660)
void MIXI_initunpackxa(void) {
    SndMix.unpackerInit[6] = UnpackXafInit;      // slot 7
    SndMix.unpackerSize[6] = 0x2c;
    SndMix.unpackerSize[7] = 0x44;
    SndMix.unpackerInit[7] = UnpackXalfInit;     // slot 8
    SndMix.unpackerInit[8] = UnpackXapfInit;     // slot 9
    SndMix.unpackerSize[8] = 0x38;
}

// PCM16 plain, looping, packet
// FUNC_AT(0x001446a0)
void MIXI_initunpack16(void) {
    SndMix.unpackerInit[3] = UnpackfInit;        // slot 4
    SndMix.unpackerSize[3] = 0x2c;
    SndMix.unpackerInit[4] = UnpacklfInit;       // slot 5
    SndMix.unpackerSize[4] = 0x30;
    SndMix.unpackerInit[5] = UnpackpfInit;       // slot 6
    SndMix.unpackerSize[5] = 0x34;
}

// FUNC_AT(0x001446e0)
void SND::CODASetNew(void *allocate) {
    SndMix.codaNew = allocate;
}

// FUNC_AT(0x001446f0)
void SND::CODASetDelete(void *release) {
    SndMix.codaDelete = release;
}

// 'cutoff' is a fraction of the platform rate; 1 or more (or NaN) removes the low pass.
// FUNC_AT(0x00144af0)
void MIX_setlowpass(int voice, float cutoff) {
    bool below = cutoff < 1.0f;
    MixVoice *m = &SndMix.voices[voice];
    if (!below) {
        if (m->lowpass != NULL) {
            RemoveFromFilterList(&m->chain, m->lowpass);
            SNDMEMI_free(m->lowpass);
            m->lowpass = NULL;
        }
        return;
    }
    if (m->lowpass == NULL) {
        SFilterNode *node = static_cast<SFilterNode *>(SNDMEMI_alloc(0x28));
        m->lowpass = node;
        node->restore = NULL;
        m->lowpass->priority = 0x28;
        CreateLPFRC(m->lowpass);
        AddToFilterList(&m->chain, m->lowpass);
    }
    int params[3];
    LowpassParams(params, cutoff);
    ModifyLPFRC(m->lowpass, params);
}

// 'cutoff' in Hz; 0 or less removes the high pass.
// FUNC_AT(0x001464c0)
void MIX_sethighpass(int voice, int cutoff) {
    MixVoice *m = &SndMix.voices[voice];
    if (cutoff > 0) {
        if (m->highpass == NULL) {
            SFilterNode *node = static_cast<SFilterNode *>(SNDMEMI_alloc(0x58));
            m->highpass = node;
            node->restore = NULL;
            m->highpass->priority = 0x50;
            CreateHPFFIR8(m->highpass);
            AddToFilterList(&m->chain, m->highpass);
        }
        int params[2];
        params[1] = PlatformRate << 8;
        params[0] = cutoff << 8;
        ModifyHPFFIR8(m->highpass, params);
        return;
    }
    if (m->highpass != NULL) {
        RemoveFromFilterList(&m->chain, m->highpass);
        SNDMEMI_free(m->highpass);
        m->highpass = NULL;
    }
}

// FUNC_AT(0x00146570)
int MIX_settimemult(int voice, int ratio) {
    SFilterNode *stretch = SndMix.voices[voice].stretch;
    if (stretch == NULL)
        return 0;
    return TimeStretchSetRatio(stretch, ratio);   // a tail call in the original
}
