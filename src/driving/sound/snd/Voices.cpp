#include "Voices.h"
#include "Platform.h"
#include "System.h"
#include "../DirectSound.h"
#include "SndGlobals.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EA's sound library, module B (docs/driving/sound.md 3.3, 4.2): the logical voices - allocation with priority
// stealing, the per-sound voice iterator, the volume and pitch arithmetic (integer only, the divisions by 127 and
// 0x1f417f in the compiler's multiply-high form, reproduced here as it computes them) - and the control API the
// game's AVoice/AFX call, plus the azimuth -> speaker gain tables and the render-mode choice.
//
// Each function is the original at its address, ported from the listing. Calls into the other modules (the
// platform driver, the system module, DirectSound) are direct. The library's globals stay where they are.
//
// devtools/SndTagShadow.cpp compares the pure ones with the originals (sound.md 9.3 step 3); the stateful ones are
// exact ports checked in game.
// ---------------------------------------------------------------------------------------------------------------

namespace {

#define AllocScratch ((int16_t *)0x00245364)          // SNDVOICEI_alloc's chosen voices
#define Generation I32_AT(0x00245370)                  // the handle generation, += 0x100 per allocation
#define SpeakerGains ((uint8_t (*)[6])0x00245378)     // [azimuth / 256][speaker]: SNDI_precalcaztospkrvol's table

// The pan -> azimuth table (128 entries in the original's .rdata: a quarter turn left, 0xc000, through 0 to a quarter
// turn right, on a sine). Read where it is: SNDI_pantoazimuth reads past it for a pan over 127, as the original's
// does.
#define PanAzimuth ((const uint16_t *)0x001d9d88)

// 2^(i/256) - 1 in 256ths, i = 0..255 (0x001d97c0 in the original's .rdata). The original reads the fractions
// below an octave through a second base 256 bytes on (0x001d98c0, indexed -255..-1): the same bytes.
const uint8_t kOctaveFraction[256] = {
    0, 1, 2, 2, 3, 4, 4, 5, 6, 7, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 14, 15, 16, 17, 17, 18, 19, 20, 20, 21, 22, 23,
    23, 24, 25, 26, 26, 27, 28, 29, 30, 30, 31, 32, 33, 33, 34, 35, 36, 37, 37, 38, 39, 40, 41, 41, 42, 43, 44, 45,
    45, 46, 47, 48, 49, 50, 50, 51, 52, 53, 54, 55, 55, 56, 57, 58, 59, 60, 61, 61, 62, 63, 64, 65, 66, 67, 67, 68,
    69, 70, 71, 72, 73, 74, 75, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 86, 87, 88, 89, 90, 91, 92, 93, 94,
    95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117,
    119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 142,
    143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 154, 155, 156, 157, 158, 159, 160, 161, 163, 164, 165, 166, 167,
    168, 169, 171, 172, 173, 174, 175, 176, 178, 179, 180, 181, 182, 183, 185, 186, 187, 188, 189, 191, 192, 193, 194,
    196, 197, 198, 199, 200, 202, 203, 204, 205, 207, 208, 209, 210, 212, 213, 214, 216, 217, 218, 219, 221, 222, 223,
    225, 226, 227, 229, 230, 231, 232, 234, 235, 236, 238, 239, 240, 242, 243, 245, 246, 247, 249, 250, 251, 253, 254,
    255,
};

// The per-bus voice fields, indexed by bus past the one declared (bus 1's are the next fields' bytes), as the
// original indexes them
int8_t &BuiltinFxLevel(SND::Voice *v, int bus) {
    return (&v->builtinFxLevel)[bus];
}
int8_t &FxLevel(SND::Voice *v, int bus) {
    return (&v->fxLevel)[bus];
}
int16_t &FxSend(SND::Voice *v, int bus) {
    return (&v->fxSend)[bus];
}

// x / 2048383 (0x1f417f) as the compiler computes it: multiply-high by 0x4186143d, >> 19, + the sign bit
int32_t Div2048383(int32_t x) {
    int32_t hi = int32_t((int64_t(x) * 0x4186143d) >> 32);
    hi >>= 19;
    return hi + int32_t(uint32_t(hi) >> 31);
}

// x / 127 as the compiler computes it: multiply-high by 0x81020409, + x, >> 6, + the sign bit
int32_t Div127(int32_t x) {
    int32_t hi = int32_t((int64_t(x) * -0x7efdfbf7) >> 32);   // 0x81020409 as a signed multiplier
    hi = int32_t(uint32_t(hi) + uint32_t(x));
    hi >>= 6;
    return hi + int32_t(uint32_t(hi) >> 31);
}

// x / 1200, unsigned: multiply-high by 0x1b4e81b5, >> 7
uint32_t DivU1200(uint32_t x) {
    return uint32_t((uint64_t(x) * 0x1b4e81b5) >> 32) >> 7;
}

int32_t Mul(int32_t a, int32_t b) {   // IMUL r32: wraps
    return int32_t(uint32_t(a) * uint32_t(b));
}

// One computed speaker gain: 2g - g^2 / 127, in bytes as the original's SHL CL / SUB CL
uint8_t Gain(int32_t g) {
    return uint8_t(uint8_t(uint32_t(g) << 1) - uint8_t(Div127(Mul(g, g))));
}


}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The control API
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0013c5c0)
int SNDplaysetdef(SND::PlayOpts *opts) {
    opts->pitchMult = 0x1000;
    opts->timeMult = 0x1000;
    opts->tempoMult = 0x1000;
    opts->key = 0x3c;
    opts->distort = 0;
    opts->lowpass = 0xffff;
    opts->highpass = 0;
    opts->velocity = 0x7f;
    opts->vol = 0x7f;
    opts->progVol = 0x7f;
    opts->bend = 0x40;
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
                SNDPLATFORM_stop(voice);
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
                FxLevel(&VoiceArray[voice], bus) = level;
                SNDI_calcfxlevel(bus, voice);
                SNDPLATFORM_setfxlevel(voice, bus);
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
                SND::Voice *v = &VoiceArray[voice];
                v->azimuth = v->builtinAzimuth + azimuth;
                v->elevation = elevation;
                SNDPLATFORM_set3dpos(voice);
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
                SND::Voice *v = &VoiceArray[voice];
                if (v->pitchMult == mult)
                    return 0;
                v->pitchMult = mult;
                iSNDcalcpitch(voice);
                SNDPLATFORM_setpitch(voice);
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
            int32_t target = uint32_t(vol) << 16;
            do {
                SND::Voice *v = &VoiceArray[voice];
                int32_t fade = v->fade;
                v->fadeStep = 0;
                if (fade == target)
                    return 0;
                v->fade = target;
                iSNDcalcvol(voice);
                SNDPLATFORM_setvol(voice);
                if (v->fxSend > 0)
                    SNDPLATFORM_setfxlevel(v->platformVoices[0], 0);
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
        return &FxBusMainCpu[bus];
    if (location == 0x400)
        return &FxBusHardware[bus];
    return NULL;
}

// bus: the bus index in the low bits, 0x10 / 0x180 the paths (none given: all). Re-sends every voice's level.
// FUNC_AT(0x0013ccc0)
int SNDfxmasterlevel(int bus, int level) {
    int index = bus & ~0x1f8;
    if ((bus & 0x1f8) == 0)
        bus |= 0x1f8;
    if ((bus & 0x10) != 0)
        FxBusMainCpu[index].level = level;
    if ((bus & 0x180) != 0)
        FxBusHardware[index].level = level;
    SNDSYS_entercritical();
    for (int i = 0; i < NumVoices; i++) {
        SND::Voice *v = &VoiceArray[i];
        SNDfxlevel(v->handle, index, FxLevel(v, index));
    }
    SNDSYS_leavecritical();
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
    int index = bus & ~0x1f8;
    int path = 4;
    while (paths != 0) {
        if ((paths & path) != 0) {
            SND::FxBus *fx;   // SNDCTRLI_getfxbus, inlined
            int location = path & 0x71c;
            if (location == 4)
                fx = &FxBusMainCpu[index];
            else if (location == 0x400)
                fx = &FxBusHardware[index];
            else
                fx = NULL;
            fx->mode = mode;
            fx->delay = delay;
            fx->feedback = feedback;
            SNDPLATFORM_fxinit(index, path);
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
            int32_t target = uint32_t(vol) << 16;
            do {
                SND::Voice *v = &VoiceArray[voice];
                int32_t step = int32_t(uint32_t(target) - uint32_t(v->fade)) / ticks;
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
                SNDPLATFORM_filteradd(voice, filter);
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
                SNDPLATFORM_lowpass(voice, cutoff);
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
    for (SND::BufferList *list = &ActiveBufferLists[0]; list < &ActiveBufferLists[2]; list++) {
        for (SND::BufferNode *node = list->first; node != NULL; node = node->next) {
            int voice = uint16_t(node->platformVoice);   // the original reads the word unsigned
            if (VoiceArray[voice].master >= 0)
                continue;
            if (node->player < 0) {
                uint32_t status;
                IDirectSoundBuffer_GetStatus(node->buffer, &status);
                if ((status & 1) == 0)
                    collected[count++] = node->platformVoice;
            } else {
                FUN_00142150(node);   // refill the packet voice's ring
            }
        }
    }
    for (int i = 0; i < count; i++)
        SNDPLATFORM_stop(collected[i]);
}

// fade x envelope x built-in x master / 0x1f417f, then the volume LFO and the volume scaling table
// FUNC_AT(0x0013e5d0)
void iSNDcalcvol(int voice) {
    SND::Voice *v = &VoiceArray[voice];
    int32_t p = Mul(int16_t(v->fade >> 16), int16_t(v->env >> 16));   // the 16.16 values' high halves
    p = Mul(p, v->builtinVol);
    p = Mul(p, MasterVolume);
    int32_t vol = Div2048383(p);
    v->vol = vol;
    if (v->volLfo != NULL) {
        int32_t t = Mul(v->volLfo[v->volLfoPos], int8_t(vol));
        v->vol = Div127(t);
    }
    if (v->volTable != NULL)
        v->vol = v->volTable[v->vol];
}

// Cents to a 4.12 multiplier: whole octaves by shifting, the rest from the table of 256ths of an octave.
// FUNC_AT(0x0013e660)
int iSNDdetunetolinear(int cents) {
    int32_t c = cents;
    int32_t linear = 0x1000;
    if (c >= 1200) {
        uint32_t octaves = DivU1200(c);
        c += Mul(octaves, -1200);
        uint32_t u = linear;
        for (; octaves != 0 && u != 0; octaves--)   // once zero it stays zero
            u <<= 1;
        linear = u;
    }
    if (c <= -1200) {
        uint32_t octaves = DivU1200(uint32_t(-1200) - uint32_t(c)) + 1;
        c += Mul(octaves, 1200);
        for (; octaves != 0 && linear != 0; octaves--)
            linear >>= 1;
    }
    c = Mul(c, 0x369d) >> 16;
    if (c < -255)
        c = -255;
    else if (c >= 0)
        return Mul(kOctaveFraction[c] + 0x100, linear) >> 8;
    return Mul(kOctaveFraction[256 + c] + 0x100, linear) >> 9;
}

// detune + bend + pitch LFO -> the cached 4.12 detune, x the programmed multiplier
// FUNC_AT(0x0013e700)
void iSNDcalcpitch(int voice) {
    SND::Voice *v = &VoiceArray[voice];
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
        v->detuneLinear = iSNDdetunetolinear(cents);
    }
    v->pitch = Mul(v->pitchMult, v->detuneLinear) >> 12;
}

// The voices of the sound 'voice' is the master of, one per call: *iterator starts at -1. A sound of one voice
// (key 0) yields only itself.
// FUNC_AT(0x0013fae0)
int iSNDpatchkey(int voice, int *iterator) {
    uint32_t key = VoiceArray[voice].key;
    if (key == 0) {
        if (*iterator >= 0)
            return 0;
        *iterator = voice;
        return 1;
    }
    *iterator = *iterator + 1;
    while (*iterator < NumVoices) {
        SND::Voice *v = &VoiceArray[*iterator];
        if (v->key == key && v->inUse == 1 && v->handle >= 0)
            return 1;
        *iterator = *iterator + 1;
    }
    return 0;
}

// count voices in [first, end): free ones oldest first, then the lowest priority below the caller's (equal too if
// the options say so), oldest first; streams (101) are never stolen. The chosen are sorted, the lowest is the
// master, a stolen one is SNDstopped. The master's index, or -9.
// FUNC_AT(0x0013fb70)
int SNDVOICEI_alloc(int count, int priority, int *handle, int first, int end) {
    int16_t *chosen = AllocScratch;
    int n = 0;
    if (count > 0)
        for (uint32_t i = 0; i < uint32_t(count); i++)
            chosen[i] = -1;
    int32_t generation = int32_t(uint32_t(Generation) + 0x100);
    Generation = generation;
    if (generation < 0)
        Generation = 0;

    if (count > 0) {
        int16_t *next = chosen;
        int k = count;
        do {
            int best = -1;
            uint32_t bestTick = 0xffffffff;
            for (int c = first; c < end; c++) {
                SND::Voice *v = &VoiceArray[c];
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
                *next++ = best;
                n++;
            }
        } while (--k != 0);
    }

    int round = n;
    if (n < count) {
        int16_t *next = &chosen[n];
        do {
            int threshold = priority;
            if (SndOptions.set.stealEqualPriority == 0)
                threshold--;
            int best = -1;
            uint32_t bestTick = 0xffffffff;
            for (int c = first; c < end; c++) {
                SND::Voice *v = &VoiceArray[c];
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
                if (p < threshold) {
                    bestTick = v->tick;
                    threshold = p;
                    best = c;
                } else if (p == threshold && v->tick < bestTick) {
                    bestTick = v->tick;
                    best = c;
                }
            }
            if (best >= 0) {
                *next++ = best;
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

    *handle = chosen[0] | Generation;
    int master = chosen[0];
    for (int e = 0; e < n; e++) {
        SND::Voice *v = &VoiceArray[chosen[e]];
        if (v->inUse != 0) {
            int h = v->handle;
            if (h < 0)
                h = VoiceArray[v->master].handle;
            SNDstop(h);
        }
        v->inUse = 1;
        v->tick = SndTick;
        v->priority = priority;
    }
    SND::Voice *m = &VoiceArray[chosen[0]];
    m->handle = *handle;
    m->platformVoices[0] = chosen[0];
    m->master = -1;
    for (int e = 1; e < n; e++) {
        VoiceArray[chosen[0]].platformVoices[e] = chosen[e];   // past six channels, on into the next fields
        VoiceArray[chosen[e]].handle = -1;
        VoiceArray[chosen[e]].master = chosen[0];
    }
    return master;
}

// A voice of a multi-timbre sound (key != 0) whose sound still has others playing is left for the last: the one
// marked keyLast goes to state 2 and is freed with the second-to-last.
// FUNC_AT(0x0013fef0)
void SNDVOICEI_free(int voice) {
    SND::Voice *self = &VoiceArray[voice];
    uint32_t key = self->key;
    if (key != 0) {
        int playing = 0, last = -1;
        int count = NumVoices;
        for (int i = 0; i < count; i++) {
            SND::Voice *v = &VoiceArray[i];
            if (v->key != key || v->handle < 0 || v->inUse == 0)
                continue;
            playing++;
            if (v->keyLast != 0)
                last = i;
        }
        if (playing != 1) {
            SND::Voice *l = &VoiceArray[last];
            uint8_t state = l->inUse;
            if (state == 2 && voice != last && playing == 2) {
                self->inUse = 0;
                self->key = 0;
                self->keyLast = 0;
                self->tick = SndTick;
                l->inUse = 0;
                l->key = 0;
                l->keyLast = 0;
                l->tick = SndTick;
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
    self->tick = SndTick;
}

// handle -> voice index, or -8 if the voice was freed or reused since
// FUNC_AT(0x00140020)
int SNDVOICEI_get(int handle) {
    if (handle < 0)
        return -8;
    int index = handle & 0xff;
    if (index >= NumVoices)
        return -8;
    SND::Voice *v = &VoiceArray[index];
    if (v->inUse == 0 || v->handle != handle)
        return -8;
    return index;
}

// FUNC_AT(0x001401b0)
void SNDI_calcfxlevel(int bus, int voice) {
    SND::Voice *v = &VoiceArray[voice];
    SND::FxBus *fx = SNDCTRLI_getfxbus(bus, v->renderMode);
    int32_t level = Mul(Mul(FxLevel(v, bus), BuiltinFxLevel(v, bus)), fx->level);
    FxSend(v, bus) = level >> 6;
}

// The azimuth -> speaker gain table: 256 azimuths (in 256ths of a turn), a byte per speaker (0..127), between the
// two speakers either side by 2g - g^2/127 of the linear split. One speaker: 127.
// FUNC_AT(0x00141070)
void SNDI_precalcaztospkrvol(void) {
    for (int k = 0; k < 256; k++) {
        int32_t a = k << 8;
        uint8_t mode = OutputMode;
        uint8_t *gains = SpeakerGains[k];
        if (mode == 1) {
            gains[0] = 0x7f;
            continue;
        }
        int n = mode;
        if (n > 0)
            memset(gains, 0, n);
        // the azimuths of n speakers (the options' row n, read as the original reads it whatever n is)
        const uint16_t *angles = SndOptions.set.speakerAzimuth[0] + n * 6;
        int32_t first = angles[0];
        int32_t below, above;   // distance from the speaker below, to the speaker above
        if (a <= first) {
            above = first - a;
            below = a - angles[n - 1] + 0x10000;
        } else {
            int32_t lastAngle = angles[n - 1];
            if (a < lastAngle) {
                for (int j = 0; j < n; j++) {
                    int32_t lo = angles[j];
                    if (a < lo)
                        continue;
                    int32_t hi = angles[j + 1];
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
    uint32_t row = (uint32_t(azimuth) >> 8) & 0xff;
    for (int i = 0; i < OutputMode; i++)
        gains[i] = int8_t(SpeakerGains[row][i]) * 0x102;   // the byte signed
}

// The next configured render mode (from *index on) a patch can play in: location bits (& 0x71c) and type bits
// (& 0xe0) must meet the patch's where it has any; more than one channel needs bit 0x80 clear. 0: none left.
// FUNC_AT(0x00142830)
int SNDI_validrendermode(int *index, SND::PatchHeader *header) {
    int count = RenderModeCount;
    if (*index >= count)
        return 0;
    do {
        uint32_t mode = RenderModes[*index];
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
        if (ok && header->channels != 1 && int8_t(result) < 0)
            ok = false;
        if (ok && result != 0) {
            *index = *index + 1;
            return result;
        }
        *index = *index + 1;
    } while (*index < RenderModeCount);
    return 0;
}

// FUNC_AT(0x00142970)
int SND_attrsetdef(SND::Attributes *attributes) {
    attributes->priority = 0;
    attributes->detune = 0;
    attributes->vol = 0x7f;
    attributes->pan = 0x40;
    attributes->fxLevel = 0;
    attributes->bendRange = 0;
    attributes->platformVersion = 2;
    attributes->renderMode = 0;
    for (int i = 0; i < 4; i++) {
        attributes->userData[i] = NULL;
        attributes->userDataSize[i] = 0;
    }
    for (int i = 0; i < 6; i++) {
        attributes->azimuth[i] = 0;
        attributes->stretchData[i] = NULL;
    }
    return 0;
}

// FUNC_AT(0x00142e90)
int SNDI_pantoazimuth(int pan) {
    return PanAzimuth[pan];
}
