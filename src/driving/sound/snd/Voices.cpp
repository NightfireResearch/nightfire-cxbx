#include "Voices.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EA's sound library, module B (docs/driving/sound.md 3.3, 4.2): the logical voices - allocation with priority
// stealing, the per-sound voice iterator, the volume and pitch arithmetic (integer only, the divisions by 127 and
// 0x1f417f in the compiler's multiply-high form, reproduced here as it computes them) - and the control API the
// game's AVoice/AFX call, plus the azimuth -> speaker gain tables and the render-mode choice.
//
// Each function is the original at its address, ported from the listing. Calls into modules not ported here (the
// platform driver, the system module) go to the originals' addresses. The library's globals stay where they are.
//
// devtools/SndTagShadow.cpp compares the pure ones with the originals (sound.md 9.3 step 3); the stateful ones are
// exact ports checked in game.
// ---------------------------------------------------------------------------------------------------------------

namespace {

inline uint8_t &U8(uint32_t address) {
    return *(uint8_t *)(uintptr_t)address;
}

inline int16_t &S16(uint32_t address) {
    return *(int16_t *)(uintptr_t)address;
}

inline uint16_t &U16(uint32_t address) {
    return *(uint16_t *)(uintptr_t)address;
}

inline uint32_t &U32(uint32_t address) {
    return *(uint32_t *)(uintptr_t)address;
}

// The voice array (sndvoicei_buffer) and its size
inline SND::Voice *Voices() {
    return *(SND::Voice **)(uintptr_t)0x00244f3cu;
}
inline int NumVoices() {
    return S16(0x00244ed8);
}
inline SND::Voice *VoiceAt(int index) {   // index may be out of range where the original's is
    return (SND::Voice *)((uint8_t *)Voices() + index * 0x88);
}
// Fields the original indexes by bus or channel past their declared size
inline int8_t &VoiceByte(SND::Voice *v, int offset) {
    return *(int8_t *)((uint8_t *)v + offset);
}
inline int16_t &VoiceShort(SND::Voice *v, int offset) {
    return *(int16_t *)((uint8_t *)v + offset);
}

const uint32_t kMasterVolume = 0x00244ed1;     // int8
const uint32_t kTick = 0x00244edc;             // the 100 Hz server's tick counter
const uint32_t kAllocScratch = 0x00245364;     // SNDVOICEI_alloc's chosen voices (int16 each)
const uint32_t kGeneration = 0x00245370;       // handle generation, += 0x100 per allocation
const uint32_t kStealEqual = 0x00244d04;       // steal at equal priority
const uint32_t kOutputMode = 0x00244d10;       // speakers: 1, 2 ... 6
const uint32_t kModeCount = 0x00244d15;        // configured render modes
const uint32_t kModes = 0x00244d18;            // uint16 each: 0x420, 0x24
const uint32_t kSpeakerAngles = 0x00244d34;    // uint16 [n] at + 12 x n, per output mode n
const uint32_t kSpeakerGains = 0x00245378;     // 256 azimuths x 6 bytes
const uint32_t kPanAzimuth = 0x001d9d88;       // uint16 per pan 0..127
const uint32_t kDetuneUp = 0x001d97c0;         // uint8 fractions of an octave, 0..255
const uint32_t kDetuneDown = 0x001d98c0;       // uint8, indexed -255..-1
const uint32_t kActiveBuffers = 0x00244c48;    // two SNDLINKLISTs (0xc each) of hardware buffer nodes

// x / 2048383 (0x1f417f) as the compiler computes it: multiply-high by 0x4186143d, >> 19, + the sign bit
inline int32_t Div2048383(int32_t x) {
    int32_t hi = (int32_t)(((int64_t)x * 0x4186143dLL) >> 32);
    hi >>= 19;
    return hi + (int32_t)((uint32_t)hi >> 31);
}

// x / 127 as the compiler computes it: multiply-high by 0x81020409, + x, >> 6, + the sign bit
inline int32_t Div127(int32_t x) {
    int32_t hi = (int32_t)(((int64_t)x * (int64_t)(int32_t)0x81020409u) >> 32);
    hi = (int32_t)((uint32_t)hi + (uint32_t)x);
    hi >>= 6;
    return hi + (int32_t)((uint32_t)hi >> 31);
}

// x / 1200, unsigned: multiply-high by 0x1b4e81b5, >> 7
inline uint32_t DivU1200(uint32_t x) {
    return (uint32_t)(((uint64_t)x * 0x1b4e81b5u) >> 32) >> 7;
}

inline int32_t Mul(int32_t a, int32_t b) {   // IMUL r32: wraps
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

// ---- the originals called from here (other modules)

inline void PlatformStop(int voice) {
    ((void (*)(int))0x0013de50)(voice);                    // SNDPLATFORM_stop
}
inline void PlatformSetVol(int voice) {
    ((void (*)(int))0x0013df50)(voice);                    // SNDPLATFORM_setvol
}
inline void PlatformSet3dPos(int voice) {
    ((void (*)(int))0x0013e0c0)(voice);                    // SNDPLATFORM_set3dpos
}
inline void PlatformSetPitch(int voice) {
    ((void (*)(int))0x0013e320)(voice);                    // SNDPLATFORM_setpitch
}
inline void PlatformSetFxLevel(int voice, int bus) {
    ((void (*)(int, int))0x00140070)(voice, bus);          // SNDPLATFORM_setfxlevel
}
inline void PlatformFilterAdd(int voice, int filter) {
    ((void (*)(int, int))0x001427d0)(voice, filter);       // SNDPLATFORM_filteradd
}
inline void PlatformLowpass(int voice, int cutoff) {
    ((void (*)(int, int))0x001429f0)(voice, cutoff);       // SNDPLATFORM_lowpass
}
inline void PlatformFxInit(int bus, int path) {
    ((void (*)(int, int))0x00140a80)(bus, path);           // SNDPLATFORM_fxinit
}
inline void ServicePacketVoice(void *node) {
    ((void (*)(void *))0x00142150)(node);                  // FUN_00142150, the hardware packet voice
}
inline void EnterCritical() {
    ((void (*)(void))0x0013b950)();                        // SNDSYS_entercritical
}
inline void LeaveCritical() {
    ((void (*)(void))0x0013b970)();                        // SNDSYS_leavecritical
}
inline uint32_t BufferGetStatus(void *buffer, uint32_t *status) {
    return ((uint32_t (__stdcall *)(void *, uint32_t *))0x0017b690)(buffer, status);   // IDirectSoundBuffer_GetStatus
}

// One computed speaker gain: 2g - g^2 / 127, in bytes as the original's SHL CL / SUB CL
inline uint8_t Gain(int32_t g) {
    int32_t d = Div127(Mul(g, g));
    return (uint8_t)((uint8_t)((uint32_t)g << 1) - (uint8_t)d);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The control API
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0013c5c0)
int SNDplaysetdef(SND::PlayOpts *opts) {
    opts->pitchMult = 0x1000;
    opts->timeMult = 0x1000;
    opts->opt10 = 0x1000;
    opts->key = 0x3c;
    opts->lowpass = 0;
    opts->opt14 = 0xffff;
    opts->opt16 = 0;
    opts->velocity = 0x7f;
    opts->vol = 0x7f;
    opts->progVol = 0x7f;
    opts->pan = 0x40;
    opts->fxLevel = 0x7f;
    opts->azimuth = 0;
    opts->elevation = 0;
    return 0;
}

// FUNC_AT(0x0013c900)
int SNDstop(int handle) {
    int index = SNDVOICEI_get(handle);
    if (index >= 0) {
        int voice = -1;
        if (iSNDpatchkey(index, &voice) != 0) {
            do {
                PlatformStop(voice);
            } while (iSNDpatchkey(index, &voice) != 0);
        }
    }
    return index;
}

// FUNC_AT(0x0013c960)
int SNDfxlevel(int handle, int bus, int level) {
    int index = SNDVOICEI_get(handle);
    if (index >= 0) {
        int voice = -1;
        if (iSNDpatchkey(index, &voice) != 0) {
            do {
                VoiceByte(VoiceAt(voice), 0x5d + bus) = (int8_t)level;
                SNDI_calcfxlevel(bus, voice);
                PlatformSetFxLevel(voice, bus);
            } while (iSNDpatchkey(index, &voice) != 0);
        }
    }
    return index;
}

// FUNC_AT(0x0013c9f0)
int SND3dpos(int handle, int azimuth, int elevation) {
    if (elevation > 0x3fff)
        elevation = 0x3fff;
    else if (elevation < -0x4000)
        elevation = -0x4000;
    int index = SNDVOICEI_get(handle);
    if (index >= 0) {
        int voice = -1;
        if (iSNDpatchkey(index, &voice) != 0) {
            do {
                SND::Voice *v = VoiceAt(voice);
                v->azimuth = (uint16_t)(v->builtinAzimuth + (uint16_t)azimuth);
                v->elevation = (int16_t)elevation;
                PlatformSet3dPos(voice);
            } while (iSNDpatchkey(index, &voice) != 0);
        }
    }
    return index;
}

// The first voice already at the multiplier ends it with 0, not the index.
// FUNC_AT(0x0013caa0)
int SNDpitchmult(int handle, int mult) {
    int index = SNDVOICEI_get(handle);
    if (index >= 0) {
        int voice = -1;
        if (iSNDpatchkey(index, &voice) != 0) {
            do {
                SND::Voice *v = VoiceAt(voice);
                if ((int)v->pitchMult == mult)
                    return 0;
                v->pitchMult = (uint16_t)mult;
                iSNDcalcpitch(voice);
                PlatformSetPitch(voice);
            } while (iSNDpatchkey(index, &voice) != 0);
        }
    }
    return index;
}

// Cancels any fade; the first voice already at the volume ends it with 0.
// FUNC_AT(0x0013cb40)
int SNDvol(int handle, int vol) {
    int index = SNDVOICEI_get(handle);
    if (index >= 0) {
        int voice = -1;
        if (iSNDpatchkey(index, &voice) != 0) {
            int32_t target = (int32_t)((uint32_t)vol << 16);
            do {
                SND::Voice *v = VoiceAt(voice);
                int32_t fade = v->fade;
                v->fadeStep = 0;
                if (fade == target)
                    return 0;
                v->fade = target;
                iSNDcalcvol(voice);
                PlatformSetVol(voice);
                if (v->fxSend > 0)
                    PlatformSetFxLevel(v->platformVoices[0], 0);
            } while (iSNDpatchkey(index, &voice) != 0);
        }
    }
    return index;
}

// FUNC_AT(0x0013cc00)
int SNDover(int handle) {
    return SNDVOICEI_get(handle) < 0 ? 1 : 0;
}

// FUNC_AT(0x0013cc80)
SND::FxBus* SNDCTRLI_getfxbus(int bus, int renderMode) {
    int location = renderMode & 0x71c;
    if (location == 4)
        return (SND::FxBus *)(uintptr_t)(0x00244f44u + (uint32_t)(bus * 0x14));
    if (location == 0x400)
        return (SND::FxBus *)(uintptr_t)(0x00244f58u + (uint32_t)(bus * 0x14));
    return NULL;
}

// bus: the bus index in the low bits, 0x10 / 0x180 the paths (none given: all). Re-sends every voice's level.
// FUNC_AT(0x0013ccc0)
int SNDfxmasterlevel(int bus, int level) {
    int index = bus & (int)0xfffffe07;
    if ((bus & 0x1f8) == 0)
        bus |= 0x1f8;
    if ((bus & 0x10) != 0)
        U8(0x00244f46u + (uint32_t)(index * 0x14)) = (uint8_t)level;
    if ((bus & 0x180) != 0)
        U8(0x00244f5au + (uint32_t)(index * 0x14)) = (uint8_t)level;
    EnterCritical();
    for (int i = 0; i < NumVoices(); i++) {
        SND::Voice *v = VoiceAt(i);
        SNDfxlevel(v->handle, index, VoiceByte(v, 0x5d + index));
    }
    LeaveCritical();
    return 0;
}

// FUNC_AT(0x0013cd50)
int SNDfxinitbus(int bus, int level, int mode, int delay, int feedback) {
    if ((bus & 0x1f8) == 0)
        bus |= 0x1f8;
    int paths = 0;
    if ((bus & 0x10) != 0)
        paths = 4;
    if ((bus & 0x100) != 0)
        paths |= 0x400;
    int index = bus & (int)0xfffffe07;
    int path = 4;
    while (paths != 0) {
        if ((paths & path) != 0) {
            SND::FxBus *fx;
            int location = path & 0x71c;
            if (location == 4)
                fx = (SND::FxBus *)(uintptr_t)(0x00244f44u + (uint32_t)(index * 0x14));
            else if (location == 0x400)
                fx = (SND::FxBus *)(uintptr_t)(0x00244f58u + (uint32_t)(index * 0x14));
            else
                fx = NULL;
            fx->mode = (uint16_t)mode;
            fx->delay = delay;
            fx->feedback = feedback;
            PlatformFxInit(index, path);
            paths &= ~path;
        }
        path <<= 1;
    }
    if (mode == 0)
        SNDfxmasterlevel(bus, 0);
    else
        SNDfxmasterlevel(bus, level);
    return 0;
}

// A fade to vol (0..127) over ticks (at least 1) of the 100 Hz server.
// FUNC_AT(0x0013e7d0)
int SNDautovol(int handle, int ticks, int vol) {
    int index = SNDVOICEI_get(handle);
    if (index >= 0) {
        if (ticks <= 0)
            ticks = 1;
        int voice = -1;
        if (iSNDpatchkey(index, &voice) != 0) {
            int32_t target = (int32_t)((uint32_t)vol << 16);
            do {
                SND::Voice *v = VoiceAt(voice);
                int32_t step = (int32_t)((uint32_t)target - (uint32_t)v->fade) / ticks;
                v->fadeTarget = target;
                v->fadeStep = step;
            } while (iSNDpatchkey(index, &voice) != 0);
        }
    }
    return index;
}

// FUNC_AT(0x0013e860)
int SNDCTRL_filteradd(int handle, int filter) {
    int index = SNDVOICEI_get(handle);
    if (index >= 0) {
        int voice = -1;
        if (iSNDpatchkey(index, &voice) != 0) {
            do {
                PlatformFilterAdd(voice, filter);
            } while (iSNDpatchkey(index, &voice) != 0);
        }
    }
    return index;
}

// FUNC_AT(0x0013fa80)
int SNDCTRL_lowpass(int handle, int cutoff) {
    int index = SNDVOICEI_get(handle);
    if (index >= 0) {
        int voice = -1;
        if (iSNDpatchkey(index, &voice) != 0) {
            do {
                PlatformLowpass(voice, cutoff);
            } while (iSNDpatchkey(index, &voice) != 0);
        }
    }
    return index;
}

// ---------------------------------------------------------------------------------------------------------------
// The voice manager
// ---------------------------------------------------------------------------------------------------------------

// The 100 Hz server's reclaim: every master voice on the two active hardware lists - a bank voice whose buffer has
// stopped playing is collected (then stopped), a packet voice gets its ring refilled.
// FUNC_AT(0x0013e530)
void iSNDserve(void) {
    uint16_t collected[256];
    int count = 0;
    for (uint32_t list = kActiveBuffers; list < kActiveBuffers + 0x18; list += 0xc) {
        for (uint8_t *node = *(uint8_t **)(uintptr_t)list; node != NULL; node = *(uint8_t **)node) {
            int voice = *(uint16_t *)(node + 0xc);
            if (VoiceAt(voice)->master >= 0)
                continue;
            if (*(int32_t *)(node + 0x20) < 0) {
                uint32_t status;
                BufferGetStatus(*(void **)(node + 8), &status);
                if ((status & 1) == 0)
                    collected[count++] = *(uint16_t *)(node + 0xc);
            } else {
                ServicePacketVoice(node);
            }
        }
    }
    for (int i = 0; i < count; i++)
        PlatformStop(collected[i]);
}

// fade x envelope x built-in x master / 0x1f417f, then the volume LFO and the volume scaling table
// FUNC_AT(0x0013e5d0)
void iSNDcalcvol(int voice) {
    SND::Voice *v = VoiceAt(voice);
    int32_t p = Mul(VoiceShort(v, 0x3a), VoiceShort(v, 0x42));
    p = Mul(p, v->builtinVol);
    p = Mul(p, (int8_t)U8(kMasterVolume));
    int32_t vol = Div2048383(p);
    v->vol = (int8_t)vol;
    if (v->volLfo != NULL) {
        int32_t t = Mul(v->volLfo[v->volLfoPos], (int8_t)vol);
        v->vol = (int8_t)Div127(t);
    }
    if (v->volTable != NULL)
        v->vol = v->volTable[v->vol];
}

// Cents to a 4.12 multiplier: whole octaves by shifting, the rest from two tables of 256ths of an octave.
// FUNC_AT(0x0013e660)
int iSNDdetunetolinear(int cents) {
    int32_t c = cents;
    int32_t linear = 0x1000;
    if (c >= 1200) {
        uint32_t octaves = DivU1200((uint32_t)c);
        c += Mul((int32_t)octaves, -1200);
        uint32_t u = (uint32_t)linear;
        for (; octaves != 0 && u != 0; octaves--)   // once zero it stays zero
            u <<= 1;
        linear = (int32_t)u;
    }
    if (c <= -1200) {
        uint32_t octaves = DivU1200((uint32_t)-1200 - (uint32_t)c) + 1;
        c += Mul((int32_t)octaves, 1200);
        for (; octaves != 0 && linear != 0; octaves--)
            linear >>= 1;
    }
    c = Mul(c, 0x369d) >> 16;
    if (c < -255)
        c = -255;
    else if (c >= 0)
        return Mul(*(uint8_t *)(uintptr_t)(kDetuneUp + (uint32_t)c) + 0x100, linear) >> 8;
    return Mul(*(uint8_t *)(uintptr_t)(kDetuneDown + (uint32_t)c) + 0x100, linear) >> 9;
}

// detune + bend + pitch LFO -> the cached 4.12 detune, x the programmed multiplier
// FUNC_AT(0x0013e700)
void iSNDcalcpitch(int voice) {
    SND::Voice *v = VoiceAt(voice);
    if (v->detuneLinear == 0) {
        int16_t range = v->bendRange;
        int32_t cents = v->detune;
        if (range != 0) {
            int32_t bend;
            if (v->bendTable != NULL)
                bend = v->bendTable[v->bend];
            else
                bend = v->bend;
            cents += Mul(bend - 0x40, range) >> 6;
        }
        if (v->pitchLfo != NULL)
            cents += Mul(v->pitchLfo[v->pitchLfoPos] - 0x40, v->pitchLfoDepth) >> 6;
        v->detuneLinear = (uint16_t)iSNDdetunetolinear(cents);
    }
    v->pitch = (uint16_t)(Mul(v->pitchMult, v->detuneLinear) >> 12);
}

// The voices of the sound 'voice' is the master of, one per call: *iterator starts at -1. A sound of one voice
// (key 0) yields only itself.
// FUNC_AT(0x0013fae0)
int iSNDpatchkey(int voice, int *iterator) {
    uint32_t key = VoiceAt(voice)->key;
    if (key == 0) {
        if (*iterator >= 0)
            return 0;
        *iterator = voice;
        return 1;
    }
    *iterator = *iterator + 1;
    while (*iterator < NumVoices()) {
        SND::Voice *v = VoiceAt(*iterator);
        if (v->key == key && v->inUse == 1 && v->handle >= 0)
            return 1;
        *iterator = *iterator + 1;
    }
    return 0;
}

// count voices in [first, end): free ones oldest first, then the lowest priority below the caller's (equal too if
// 0x00244d04), oldest first; streams (101) are never stolen. The chosen are sorted, the lowest is the master, a
// stolen one is SNDstopped. The master's index, or -9.
// FUNC_AT(0x0013fb70)
int SNDVOICEI_alloc(int count, int priority, int *handle, int first, int end) {
    int16_t *chosen = (int16_t *)(uintptr_t)kAllocScratch;
    int n = 0;
    if (count > 0)
        for (uint32_t i = 0; i < (uint32_t)count; i++)
            chosen[i] = -1;
    int32_t generation = (int32_t)(U32(kGeneration) + 0x100);
    U32(kGeneration) = (uint32_t)generation;
    if (generation < 0)
        U32(kGeneration) = 0;

    if (count > 0) {
        int16_t *next = chosen;
        int k = count;
        do {
            int best = -1;
            uint32_t bestTick = 0xffffffffu;
            for (int c = first; c < end; c++) {
                SND::Voice *v = VoiceAt(c);
                if (v->inUse != 0)
                    continue;
                bool taken = false;
                if (next > chosen) {
                    for (int e = 0; e < n; e++)
                        if (chosen[e] == c) {
                            taken = true;
                            break;
                        }
                }
                if (taken)
                    continue;
                if (v->tick < bestTick) {
                    bestTick = v->tick;
                    best = c;
                }
            }
            if (best >= 0) {
                *next++ = (int16_t)best;
                n++;
            }
        } while (--k != 0);
    }

    int round = n;
    if (n < count) {
        int16_t *next = &chosen[n];
        do {
            int threshold = priority;
            if (U8(kStealEqual) == 0)
                threshold--;
            int best = -1;
            uint32_t bestTick = 0xffffffffu;
            for (int c = first; c < end; c++) {
                SND::Voice *v = VoiceAt(c);
                bool taken = false;
                if (next > chosen) {
                    for (int e = 0; e < n; e++)
                        if (chosen[e] == c) {
                            taken = true;
                            break;
                        }
                }
                if (taken)
                    continue;
                uint8_t p = v->priority;
                if (p >= 0x65)
                    continue;
                if ((int)p < threshold) {
                    bestTick = v->tick;
                    threshold = p;
                    best = c;
                } else if ((int)p == threshold && v->tick < bestTick) {
                    bestTick = v->tick;
                    best = c;
                }
            }
            if (best >= 0) {
                *next++ = (int16_t)best;
                n++;
                if (n >= count)
                    break;
            }
            round++;
        } while (round < count);
    }

    if (n != count)
        return -9;
    int last = count - 1;
    bool sorted;
    do {
        sorted = true;
        if (last <= 0)
            break;
        for (int e = 0; e < last; e++) {
            int16_t a = chosen[e], b = chosen[e + 1];
            if (a > b) {
                chosen[e] = b;
                chosen[e + 1] = a;
                sorted = false;
            }
        }
    } while (!sorted);

    *handle = (int32_t)chosen[0] | (int32_t)U32(kGeneration);
    int master = chosen[0];
    for (int e = 0; e < n; e++) {
        SND::Voice *v = VoiceAt(chosen[e]);
        if (v->inUse != 0) {
            int h = v->handle;
            if (h < 0)
                h = VoiceAt(v->master)->handle;
            SNDstop(h);
        }
        v->inUse = 1;
        v->tick = U32(kTick);
        v->priority = (uint8_t)priority;
    }
    SND::Voice *m = VoiceAt(chosen[0]);
    m->handle = *handle;
    m->platformVoices[0] = chosen[0];
    m->master = -1;
    for (int e = 1; e < n; e++) {
        VoiceShort(VoiceAt(chosen[0]), 4 + e * 2) = chosen[e];
        VoiceAt(chosen[e])->handle = -1;
        VoiceAt(chosen[e])->master = chosen[0];
    }
    return master;
}

// A voice of a multi-timbre sound (key != 0) whose sound still has others playing is left for the last: the one
// marked keyLast goes to state 2 and is freed with the second-to-last.
// FUNC_AT(0x0013fef0)
void SNDVOICEI_free(int voice) {
    SND::Voice *self = VoiceAt(voice);
    uint32_t key = self->key;
    if (key != 0) {
        int playing = 0, last = -1;
        int count = NumVoices();
        for (int i = 0; i < count; i++) {
            SND::Voice *v = VoiceAt(i);
            if (v->key != key || v->handle < 0 || v->inUse == 0)
                continue;
            playing++;
            if (v->keyLast != 0)
                last = i;
        }
        if (playing != 1) {
            SND::Voice *l = VoiceAt(last);
            uint8_t state = l->inUse;
            if (state == 2 && voice != last && playing == 2) {
                self->inUse = 0;
                self->key = 0;
                self->keyLast = 0;
                self->tick = U32(kTick);
                l->inUse = 0;
                l->key = 0;
                l->keyLast = 0;
                l->tick = U32(kTick);
                return;
            }
            if (state == 1 && voice == last) {
                l->inUse = 2;
                return;
            }
        }
    }
    self->inUse = 0;
    self->key = 0;
    self->keyLast = 0;
    self->tick = U32(kTick);
}

// handle -> voice index, or -8 if the voice was freed or reused since
// FUNC_AT(0x00140020)
int SNDVOICEI_get(int handle) {
    if (handle < 0)
        return -8;
    int index = handle & 0xff;
    if (index >= NumVoices())
        return -8;
    SND::Voice *v = VoiceAt(index);
    if (v->inUse == 0 || v->handle != handle)
        return -8;
    return index;
}

// FUNC_AT(0x001401b0)
void SNDI_calcfxlevel(int bus, int voice) {
    SND::Voice *v = VoiceAt(voice);
    SND::FxBus *fx = SNDCTRLI_getfxbus(bus, v->renderMode);
    int32_t level = Mul(Mul(VoiceByte(v, 0x5d + bus), VoiceByte(v, 0x5c + bus)), fx->level);
    VoiceShort(v, 0x5e + bus * 2) = (int16_t)(level >> 6);
}

// The azimuth -> speaker gain table: 256 azimuths (in 256ths of a turn), a byte per speaker (0..127), between the
// two speakers either side by 2g - g^2/127 of the linear split. One speaker: 127.
// FUNC_AT(0x00141070)
void SNDI_precalcaztospkrvol(void) {
    for (int k = 0, row = 0; row < 0x600; k++, row += 6) {
        int32_t a = k << 8;
        uint8_t mode = U8(kOutputMode);
        uint8_t *gains = (uint8_t *)(uintptr_t)(kSpeakerGains + (uint32_t)row);
        if (mode == 1) {
            gains[0] = 0x7f;
            continue;
        }
        int n = mode;
        if (n > 0)
            memset(gains, 0, (size_t)n);
        uint32_t angles = kSpeakerAngles + (uint32_t)(n * 12);
        int32_t first = U16(angles);
        int32_t below, above;   // distance from the speaker below, to the speaker above
        if (a <= first) {
            above = first - a;
            below = a - (int32_t)U16(angles + (uint32_t)(n * 2 - 2)) + 0x10000;
        } else {
            int32_t lastAngle = U16(angles + (uint32_t)(n * 2 - 2));
            if (a < lastAngle) {
                for (int j = 0; j < n; j++) {
                    int32_t lo = U16(angles + (uint32_t)(j * 2));
                    if (a < lo)
                        continue;
                    int32_t hi = U16(angles + (uint32_t)(j * 2 + 2));
                    if (a > hi)
                        continue;
                    int32_t up = hi - a, down = a - lo;
                    int32_t sum = up + down;
                    int32_t g = Mul(up, 0x7f) / sum;
                    uint8_t b = Gain(g);
                    g = Mul(down, 0x7f) / sum;
                    gains[j] = b;
                    gains[j + 1] = Gain(g);
                }
                continue;
            }
            above = first - a + 0x10000;
            below = a - lastAngle;
        }
        int32_t sum = below + above;
        int32_t g = Mul(below, 0x7f) / sum;
        uint8_t b = Gain(g);
        g = Mul(above, 0x7f) / sum;
        gains[0] = b;
        gains[n - 1] = Gain(g);
    }
}

// The speaker gains of an azimuth: the table's byte x 0x102 per speaker
// FUNC_AT(0x00141230)
void SNDI_aztospkrvol(int azimuth, int16_t *gains) {
    uint32_t row = ((uint32_t)azimuth >> 8) & 0xff;
    for (int i = 0; i < (int)U8(kOutputMode); i++)
        gains[i] = (int16_t)(*(int8_t *)(uintptr_t)(kSpeakerGains + row * 6 + (uint32_t)i) * 0x102);
}

// The next configured render mode (from *index on) a patch can play in: location bits (& 0x71c) and type bits
// (& 0xe0) must meet the patch's where it has any; more than one channel needs bit 0x80 clear. 0: none left.
// FUNC_AT(0x00142830)
int SNDI_validrendermode(int *index, SND::PatchHeader *header) {
    int count = U8(kModeCount);
    if (*index >= count)
        return 0;
    do {
        uint32_t mode = U16(kModes + (uint32_t)(*index * 2));
        uint32_t patch = header->renderMode;
        uint32_t result = mode & 0x71c;
        bool ok = true;
        if ((patch & 0x71c) != 0) {
            result = patch & result;
            if (result == 0)
                ok = false;
        }
        if (ok) {
            uint32_t type = mode & 0xe0;
            if ((patch & 0xe0) == 0) {
                result |= type;
            } else {
                uint32_t both = patch & type;
                result |= both;
                if (both == 0)
                    ok = false;
            }
        }
        if (ok && header->channels != 1 && (int8_t)result < 0)
            ok = false;
        if (ok && result != 0) {
            *index = *index + 1;
            return (int)result;
        }
        *index = *index + 1;
    } while (*index < (int)U8(kModeCount));
    return 0;
}

// FUNC_AT(0x00142970)
int SND_attrsetdef(SND::Attributes *attributes) {
    attributes->priority = 0;
    attributes->a00 = 0;
    attributes->vol = 0x7f;
    attributes->pan = 0x40;
    attributes->fxLevel = 0;
    attributes->bendRange = 0;
    attributes->tag80 = 2;
    attributes->renderMode = 0;
    for (int i = 0; i < 4; i++) {
        attributes->userData[i] = NULL;
        attributes->userDataSize[i] = 0;
    }
    for (int i = 0; i < 6; i++) {
        attributes->azimuth[i] = 0;
        attributes->blobs[i] = NULL;
    }
    return 0;
}

// FUNC_AT(0x00142e90)
int SNDI_pantoazimuth(int pan) {
    return *(uint16_t *)(uintptr_t)(kPanAzimuth + (uint32_t)(pan * 2));
}
