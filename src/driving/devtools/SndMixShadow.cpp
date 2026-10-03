#include "SndMixShadow.h"

#include "../sound/snd/Mixer.h"
#include "../sound/snd/Reverb.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_SNDMIXSHADOW=1, at injection time on the loader's thread (nothing else runs, so swapping the
// original's bytes back is safe): module G of docs/driving/sound.md - the software mixer and reverb - against
// the originals, as section 9.3 step 5 asks. Every case is run twice from the same bytes: once with all 57 of
// G's entry points swapped back to the original (common/xbeOriginal.h), once with ours; the calls always go to
// the original addresses, so the second run also proves the jumps. Modules H (filters) and A (SNDMEMI) run
// underneath on both sides alike.
//
// 1. Pure functions, batched: findprime exhaustively for 3..120000 samples (rate x ms / 1000 from -100) plus
//    random rate/ms pairs over the full u16 range, FUN_001465a0, and the two series FUN_001465c0 / FUN_00146640
//    (doubles returned on the x87 stack, compared as 8 bytes).
// 2. Kernels on random buffers in a 256 KB world (its own SNDMEMI heap), compared as a whole after each run:
//    mixc (random lengths, alignments, in place), MIXI_interpolatemix/interpolateto0, MIXI_reverbblock (random
//    taps, in place as group 0 runs it), and every FX building block - tee, sum, allpass (init, modify, three
//    process calls with odd counts, restore), gain, resonator (through modify, and on raw random state including
//    the bypass), the two FIR-node modifies and process, and the six inits on garbage nodes - fed by fake
//    upstream nodes that write seeded noise (sometimes ending, sometimes returning 0 or -1).
// 3. The whole mixer, offline: from zeroed globals (0x00245978..0x00247600) and a private 1.5 MB heap, MIX_create,
//    the reverb (off, fxdefault, a three-group description, a mode with taps but no hook, and an fx2 network of
//    every node type), six voices whose unpacker is a fake init in the empty slot 1 (seeded noise at PCM scale, one
//    ending mid-run), with resampler, low and high pass on some, master low pass in one scenario; then 36
//    MIX_audio calls with gain ramps, fx sends coming and going, the 3000-slice idle cut-off, a stop, a removal of
//    filters and a replay in between; then MIX_restorereverb and MIX_destroy. Compared after every step (the
//    globals, the heap, the six rings) and byte for byte at the end.
//
// A difference where both sides hold a NaN is counted apart: the x87 and SSE pick NaN payloads differently.
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types

// G's entry points: 54 functions and the three Ghidra never made
const uint32_t kOurs[] = {
    0x001413e0, 0x001414d0, 0x00141630, 0x00141710, 0x00141880, 0x00141910, 0x00141ad0, 0x00141b20, 0x00141bb0,
    0x00141be0, 0x00141c20, 0x00141db0, 0x00142050, 0x001420c0, 0x00142ee0, 0x00142fc0, 0x001433a0, 0x001434b0,
    0x00143510, 0x00143780, 0x001437e0, 0x00143970, 0x00143b40, 0x00144630, 0x00144660, 0x001446a0, 0x001446e0,
    0x001446f0, 0x00144af0, 0x00144bc0, 0x00144ca0, 0x00144cc0, 0x00144d00, 0x00144e20, 0x00144e50, 0x00144f30,
    0x00144fc0, 0x00144ff0, 0x00145020, 0x001450b0, 0x00145160, 0x00145190, 0x001451b0, 0x00145390, 0x001453e0,
    0x00145410, 0x00145500, 0x001455f0, 0x00145640, 0x00145670, 0x00145760, 0x001464c0, 0x00146570, 0x001465a0,
    0x001465c0, 0x00146640, 0x0014a260 };
const int kNumOurs = (int)(sizeof(kOurs) / sizeof(kOurs[0]));

int g_patched;
int g_cases, g_checks, g_differ, g_nanOnly, g_faults, g_reports;

void UseOriginals(bool original) {
    for (int i = 0; i < kNumOurs; i++)
        XbeOriginal_Restore(kOurs[i], original);
}

void Report(const char *format, ...) {
    if (g_reports++ >= 12)
        return;
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    printf("[sndmixshadow]   %s\n", line);
}

inline uint8_t &U8(uint32_t address) { return *(uint8_t *)(uintptr_t)address; }
inline uint16_t &U16(uint32_t address) { return *(uint16_t *)(uintptr_t)address; }
inline uint32_t &U32(uint32_t address) { return *(uint32_t *)(uintptr_t)address; }

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
float Float(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}
bool IsNaNBits(uint32_t u) { return (u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) != 0; }

uint32_t Hash(const void *data, size_t size, uint32_t h = 2166136261u) {
    for (size_t i = 0; i < size; i++)
        h = (h ^ ((const uint8_t *)data)[i]) * 16777619u;
    return h;
}

// ---- random inputs

uint32_t g_rng = 0x2545f491u;
uint32_t Next() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
int Range(int lo, int hi) { return lo + (int)(Next() % (uint32_t)(hi - lo + 1)); }
float Unit() { return (float)(int32_t)Next() / 2147483648.0f; }   // -1 .. 1

// Audio-ish: mostly -1..1, PCM-scale, tiny and denormal, zeros, now and then a large value or (rarely) a NaN
float Sample() {
    uint32_t r = Next() % 1000;
    if (r < 600)
        return Unit();
    if (r < 850)
        return Unit() * 32768.0f;
    if (r < 900)
        return Unit() * 1e-30f;
    if (r < 930)
        return Float(Next() & 0x807fffffu);   // denormal
    if (r < 960)
        return (r & 1) ? 0.0f : -0.0f;
    if (r < 995)
        return Unit() * 1e20f;
    return Float(0x7fc00000u | (Next() & 0x803fffffu));   // NaN
}
float Gain() { return Unit() * 2.0f; }
void FillSamples(float *p, int n) {
    for (int i = 0; i < n; i++)
        p[i] = Sample();
}

// ---- the guarded call

typedef void (*Body)(void);

bool Guarded(Body body) {
#ifdef _MSC_VER
    __try {
        body();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        _fpreset();   // a fault inside x87 code leaves the register stack as it was
        return false;
    }
#else
    body();
    return true;
#endif
}

// NaN-only differences by test, for the detail line
struct NaNCount {
    char what[48];
    int cases;
} g_nanBy[24];

void CountNaNOnly(const char *what, int cases) {
    g_nanOnly += cases;
    for (int i = 0; i < 24; i++) {
        if (g_nanBy[i].what[0] == 0 || strncmp(g_nanBy[i].what, what, sizeof(g_nanBy[i].what) - 1) == 0) {
            if (g_nanBy[i].what[0] == 0)
                snprintf(g_nanBy[i].what, sizeof(g_nanBy[i].what), "%s", what);
            g_nanBy[i].cases += cases;
            return;
        }
    }
}

// Compares two images dword by dword; the number of differences (NaN-only ones counted apart)
int CompareImages(const char *what, int index, const uint8_t *o, const uint8_t *p, size_t size, uint32_t base) {
    int real = 0, nan = 0;
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t a, b;
        memcpy(&a, o + i, 4);
        memcpy(&b, p + i, 4);
        if (a == b)
            continue;
        if (IsNaNBits(a) && IsNaNBits(b)) {
            nan++;
            continue;
        }
        if (real++ == 0)
            Report("%s case %d: +0x%x (0x%08x) original %08x, ours %08x", what, index, (unsigned)i,
                   (unsigned)(base + i), a, b);
    }
    g_checks += (int)(size / 4);
    if (nan != 0 && real == 0)
        CountNaNOnly(what, 1);
    return real;
}

// ---- section 2's world: a heap and fixed slots, compared whole

const size_t kWorld = 0x40000;
const uint32_t kHeapSize = 0x30000;
const uint32_t kNodeA = 0x30000;     // 0x100
const uint32_t kUp1 = 0x30100;       // 0x80
const uint32_t kUp2 = 0x30180;       // 0x80
const uint32_t kResults = 0x30200;   // 0x200
const uint32_t kScratch = 0x30400;   // 1024 floats
const uint32_t kOut = 0x31400;       // 1024 floats
const uint32_t kIn = 0x32400;        // 1024 floats
const uint32_t kExtra = 0x33400;     // 0x3000 floats

uint8_t *g_world;
uint8_t *g_start;   // the bytes a case starts from
uint8_t *g_orig;    // the original's result

template <typename T> T *At(uint32_t offset) { return (T *)(g_world + offset); }

void HeapInit(void *base, uint32_t size) { ((void (*)(void *, uint32_t))0x0013f710)(base, size); }   // SNDMEMI_init
void *SndAlloc(int bytes) { return ((void *(*)(int))0x0013f780)(bytes); }                           // SNDMEMI_alloc

// A fake upstream (and, in section 3, the voices' unpacker): seeded noise into its fourth argument
struct FakeSource {
    SND::SFilterNode head;
    uint32_t seed;                   // +0x1c
    int32_t left;                    // +0x20 frames before the end (then -1); negative: endless
    float amplitude;                 // +0x24
    int32_t forced;                  // +0x28 1: return 0 (nothing made), 2: return -1 (the end)
    uint32_t calls;                  // +0x2c
    int32_t params[10];              // +0x30 the unpacker init's parameters
};
static_assert(sizeof(FakeSource) == 0x58, "the fake source's size is in the unpacker table");

int FakeProcess(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester) {
    (void)scratch;
    (void)requester;
    FakeSource *s = reinterpret_cast<FakeSource *>(node);
    s->calls++;
    if (s->forced == 1)
        return 0;
    if (s->forced == 2 || s->left == 0)
        return -1;
    int n = frames;
    if (s->left > 0 && n > s->left)
        n = s->left;
    for (int i = 0; i < n; i++) {
        s->seed = s->seed * 1664525u + 1013904223u;
        out[i] = (float)(int32_t)s->seed * (1.0f / 2147483648.0f) * s->amplitude;
    }
    if (s->left > 0)
        s->left -= n;
    return n;
}

void FakeSetup(FakeSource *s, bool ending) {
    memset(s, 0, sizeof(*s));
    s->head.process = FakeProcess;
    s->seed = Next();
    s->left = ending ? Range(0, 1500) : -1;
    s->amplitude = (Next() & 3) == 0 ? Sample() : Unit() * 30000.0f;
    uint32_t r = Next() % 30;
    if (r == 0)
        s->forced = 1;
    else if (r == 1)
        s->forced = 2;
}

// Fresh random contents in the slots, a fresh heap (its old bytes stay: both sides start from them)
void WorldReset() {
    for (uint32_t i = kNodeA; i < kExtra; i += 4) {
        float f = Sample();
        memcpy(g_world + i, &f, 4);
    }
    memset(g_world + kResults, 0, 0x200);
    HeapInit(g_world, kHeapSize);
}

// One case: the body from the same bytes with the originals, then with ours; the world compared
void Dual(const char *what, int index, Body body, uint32_t from, uint32_t to) {
    uint8_t *base = g_world + from;
    size_t size = to - from;
    memcpy(g_start, base, size);
    UseOriginals(true);
    bool okO = Guarded(body);
    UseOriginals(false);
    memcpy(g_orig, base, size);
    memcpy(base, g_start, size);
    bool okP = Guarded(body);
    g_cases++;
    if (!okO || !okP) {
        g_faults++;
        if (okO != okP) {
            g_differ++;
            Report("%s case %d: a fault on the %s side only", what, index, okO ? "port's" : "original's");
            return;
        }
    }
    if (CompareImages(what, index, g_orig, base, size, (uint32_t)(uintptr_t)base) != 0)
        g_differ++;
}

// ---- section 2's bodies (inputs in g_k, outputs in the world)

struct KernelInputs {
    int frames[4];
    int count;
    int requester;
    float a, b;
    int params[4];
    uint32_t inOffset, outOffset;
    bool inPlace;
    bool upstream;
} g_k;

typedef int (*ProcessAt)(void *, int, float *, float *, int);
typedef void (*ModifyAt)(void *, const int *);
typedef int (*InitAt)(void *);

void BodyMixc() {
    float *in = At<float>(kIn) + g_k.inOffset;
    float *out = g_k.inPlace ? in : At<float>(kOut) + g_k.outOffset;
    ((void (*)(int, float, const float *, float *))0x00143b40)(g_k.count, g_k.a, in, out);
}

void BodyInterpolateMix() {
    ((void (*)(float, float, float *, float *))0x001414d0)(g_k.a, g_k.b, At<float>(kIn), At<float>(kOut));
}

void BodyInterpolateTo0() {
    ((void (*)(float *, float *))0x001413e0)(At<float>(kResults), At<float>(kOut));
}

void BodyReverbBlock() {
    float *in = At<float>(kIn);
    float *out = g_k.inPlace ? in : At<float>(kOut);
    ((void (*)(void *, int, const float *, float *))0x00145760)(At<void>(kNodeA), g_k.count, in, out);
}

void RunProcess(uint32_t address, int calls) {
    int *results = At<int>(kResults);
    for (int i = 0; i < calls; i++)
        results[i] = ((ProcessAt)address)(At<void>(kNodeA), g_k.frames[i], At<float>(kScratch), At<float>(kOut),
                                          g_k.requester);
}

void BodyTee() {
    RunProcess(0x00144bc0, 4);
    ((void (*)(void *))0x00144ca0)(At<void>(kNodeA));
}

void BodySum() {
    RunProcess(0x00144d00, 3);
    ((void (*)(void *))0x00144ca0)(At<void>(kNodeA));
}

void BodyAllpass() {
    ((InitAt)0x00144ff0)(At<void>(kNodeA));
    At<SND::SFilterNode>(kNodeA)->input = g_k.upstream ? At<SND::SFilterNode>(kUp1) : NULL;
    At<SND::SFilterNode>(kNodeA)->requester = (uint8_t)g_k.requester;
    ((ModifyAt)0x00145020)(At<void>(kNodeA), g_k.params);
    RunProcess(0x00144f30, 4);
    ((void (*)(void *))0x00144fc0)(At<void>(kNodeA));
}

void BodyGain() {
    ((InitAt)0x00145160)(At<void>(kNodeA));
    At<SND::SFilterNode>(kNodeA)->input = g_k.upstream ? At<SND::SFilterNode>(kUp1) : NULL;
    ((ModifyAt)0x00145190)(At<void>(kNodeA), g_k.params);
    RunProcess(0x001450b0, 3);
}

void BodyResonator() {
    ((InitAt)0x001453e0)(At<void>(kNodeA));
    At<SND::SFilterNode>(kNodeA)->input = g_k.upstream ? At<SND::SFilterNode>(kUp1) : NULL;
    ((ModifyAt)0x00145410)(At<void>(kNodeA), g_k.params);
    RunProcess(0x00145390, 3);
}

void BodyResonatorRaw() {
    RunProcess(0x00145390, 2);
}

void BodyFir2() {
    ((InitAt)0x00145640)(At<void>(kNodeA));
    At<SND::SFilterNode>(kNodeA)->input = g_k.upstream ? At<SND::SFilterNode>(kUp1) : NULL;
    ((ModifyAt)0x00145670)(At<void>(kNodeA), g_k.params);
    RunProcess(0x001455f0, 2);
}

void BodyFir4() {
    ((InitAt)0x001454d0)(At<void>(kNodeA));   // module H's SFILTER_createHPFFIR8
    ((ModifyAt)0x00145500)(At<void>(kNodeA), g_k.params);
}

void BodyInits() {
    static const uint32_t inits[6] = { 0x00144cc0, 0x00144e20, 0x00144ff0, 0x00145160, 0x001453e0, 0x00145640 };
    int *results = At<int>(kResults);
    for (int i = 0; i < 6; i++)
        results[i] = ((InitAt)inits[i])(At<uint8_t>(kExtra) + i * 0x100);
}

void Kernels() {
    // mixc
    for (int c = 0; c < 3000; c++) {
        WorldReset();
        g_k.count = (Next() & 3) == 0 ? Range(1, 60) * 16 : Range(1, 1000);
        g_k.inOffset = (Next() & 1) ? 0 : Range(0, 3);
        g_k.outOffset = (Next() & 1) ? 0 : Range(0, 3);
        if (g_k.count > 1020)
            g_k.count = 1020;
        g_k.inPlace = (Next() % 10) == 0;
        g_k.a = (Next() % 20) == 0 ? Sample() : Gain();
        Dual("mixc", c, BodyMixc, kScratch, kExtra);
    }
    // the ramps
    for (int c = 0; c < 2000; c++) {
        WorldReset();
        g_k.a = (Next() & 7) == 0 ? Sample() : Gain();
        g_k.b = (Next() & 7) == 0 ? Sample() : Gain();
        Dual("MIXI_interpolatemix", c, BodyInterpolateMix, kResults, kExtra);
        WorldReset();
        float gain = (Next() & 7) == 0 ? Sample() : Gain();
        memcpy(At<float>(kResults), &gain, 4);
        Dual("MIXI_interpolateto0", c, BodyInterpolateTo0, kResults, kExtra);
    }
    // standard reverb taps
    for (int c = 0; c < 2000; c++) {
        WorldReset();
        SND::ReverbTap *tap = At<SND::ReverbTap>(kNodeA);
        tap->length = Range(1, 0x2f00);
        tap->buffer = At<float>(kExtra);
        FillSamples(tap->buffer, tap->length);
        tap->writePos = Range(1, tap->length);
        tap->readPos = Range(1, tap->length);
        tap->gain = Unit();
        tap->negGain = (Next() & 7) == 0 ? Unit() : -tap->gain;
        tap->lowpass = Sample();
        tap->lowpassKeep = (Unit() + 1.0f) * 0.5f;
        tap->lowpassInput = 1.0f - tap->lowpassKeep;
        int most = tap->writePos < tap->readPos ? tap->writePos : tap->readPos;
        if (most > 1024)
            most = 1024;
        g_k.count = (Next() % 50) == 0 ? 0 : Range(1, most);
        g_k.inPlace = (Next() & 1) != 0;
        Dual("MIXI_reverbblock", c, BodyReverbBlock, kNodeA, kWorld);
    }
    // the FX building blocks
    for (int c = 0; c < 600; c++) {
        // tee
        WorldReset();
        {
            SND::FxTeeNode *tee = At<SND::FxTeeNode>(kNodeA);
            memset(tee, 0, sizeof(*tee));
            tee->process = (SND::SFilterProcess)0x00144bc0;
            tee->input = At<SND::SFilterNode>(kUp1);
            tee->served = (uint16_t)(Next() & 1);
            tee->fetch = (uint16_t)(Next() & 1);
            tee->capacity = (Next() & 1) ? Range(0, 600) : 0;
            tee->cache = tee->capacity != 0 ? (float *)SndAlloc(tee->capacity * 4) : NULL;
            if (tee->cache != NULL)
                FillSamples(tee->cache, tee->capacity);
            FakeSetup(At<FakeSource>(kUp1), (Next() & 3) == 0);
            for (int i = 0; i < 4; i++)
                g_k.frames[i] = Range(1, 600);
            g_k.requester = Range(0, 6);
            Dual("tee 0x00144bc0", c, BodyTee, 0, (uint32_t)kWorld);
        }
        // sum
        WorldReset();
        {
            SND::FxSumNode *sum = At<SND::FxSumNode>(kNodeA);
            memset(sum, 0, sizeof(*sum));
            sum->input = At<SND::SFilterNode>(kUp1);
            sum->input2 = At<SND::SFilterNode>(kUp2);
            sum->capacity = (Next() & 1) ? Range(0, 600) : 0;
            sum->buffer = sum->capacity != 0 ? (float *)SndAlloc(sum->capacity * 4) : NULL;
            FakeSetup(At<FakeSource>(kUp1), (Next() & 3) == 0);
            FakeSetup(At<FakeSource>(kUp2), (Next() & 3) == 0);
            for (int i = 0; i < 3; i++)
                g_k.frames[i] = Range(1, 600);
            Dual("sum 0x00144d00", c, BodySum, 0, (uint32_t)kWorld);
        }
        // allpass
        WorldReset();
        {
            static const int rates[5] = { 8000, 22050, 32000, 44100, 48000 };
            FakeSetup(At<FakeSource>(kUp1), (Next() & 7) == 0);
            g_k.upstream = (Next() % 5) != 0;
            g_k.requester = Range(0, 6);
            g_k.params[0] = (Next() % 10) == 0 ? Range(-300, 300) : Range(0, 254);
            g_k.params[1] = rates[Next() % 5] << 8;
            g_k.params[2] = Range(0, 400) << 8;
            for (int i = 0; i < 4; i++)
                g_k.frames[i] = (Next() & 1) ? Range(1, 128) * 4 : Range(1, 509);
            Dual("allpass 0x00144f30", c, BodyAllpass, 0, (uint32_t)kWorld);
        }
        // gain
        WorldReset();
        {
            FakeSetup(At<FakeSource>(kUp1), (Next() & 7) == 0);
            g_k.upstream = (Next() % 4) != 0;
            g_k.requester = Range(0, 6);
            g_k.params[0] = (Next() & 1) ? Range(0, 512) : (int)Next();
            for (int i = 0; i < 3; i++)
                g_k.frames[i] = Range(1, 600);
            Dual("gain 0x001450b0", c, BodyGain, 0, (uint32_t)kWorld);
        }
        // resonator through its modify
        WorldReset();
        {
            static const int rates[5] = { 8000, 22050, 32000, 44100, 48000 };
            FakeSetup(At<FakeSource>(kUp1), (Next() & 7) == 0);
            g_k.upstream = (Next() % 4) != 0;
            g_k.requester = Range(0, 6);
            int rate = rates[Next() % 5];
            g_k.params[0] = ((Next() & 3) == 0 ? Range(0, 30000) : Range(20, rate / 2)) << 8;
            g_k.params[1] = rate << 8;
            g_k.params[2] = ((Next() & 3) == 0 ? Range(0, 30000) : Range(1, 2000)) << 8;
            g_k.params[3] = Range(0, 1024);
            for (int i = 0; i < 3; i++)
                g_k.frames[i] = Range(1, 600);
            Dual("resonator 0x00145410/0x00145390", c, BodyResonator, 0, (uint32_t)kWorld);
        }
        // resonator on raw state
        WorldReset();
        {
            SND::FxResonatorNode *node = At<SND::FxResonatorNode>(kNodeA);
            node->input = (Next() & 1) ? At<SND::SFilterNode>(kUp1) : NULL;
            node->requester = (uint8_t)Range(0, 6);
            node->rate = (Next() & 7) == 0 ? (int)Next() : Range(8000, 48000);
            node->frequency = (Next() & 7) == 0 ? Sample() : (float)Range(-100, 30000);
            node->bandwidth = (Next() & 7) == 0 ? Sample() : (float)Range(0, 40000);
            node->radius = (Next() & 7) == 0 ? Sample() : Unit();
            node->b1 = Unit();
            node->a0 = (Next() & 7) == 0 ? Sample() : Unit();
            node->y1 = Sample();
            node->y2 = Sample();
            FakeSetup(At<FakeSource>(kUp1), (Next() & 7) == 0);
            for (int i = 0; i < 2; i++)
                g_k.frames[i] = Range(1, 600);
            Dual("resonator raw 0x001451b0", c, BodyResonatorRaw, 0, (uint32_t)kWorld);
        }
        // the FIR node of type 2 (module H's FIR underneath)
        WorldReset();
        {
            static const int rates[5] = { 8000, 22050, 32000, 44100, 48000 };
            FakeSetup(At<FakeSource>(kUp1), (Next() & 7) == 0);
            g_k.upstream = (Next() % 4) != 0;
            g_k.requester = Range(0, 6);
            int rate = rates[Next() % 5];
            g_k.params[0] = Range(1, rate / 2) << 8;
            g_k.params[1] = rate << 8;
            for (int i = 0; i < 2; i++)
                g_k.frames[i] = Range(1, 600);
            Dual("fir 0x00145670/0x001455f0", c, BodyFir2, 0, (uint32_t)kWorld);
        }
        // the FIR node of type 4's modify
        WorldReset();
        {
            static const int rates[5] = { 8000, 22050, 32000, 44100, 48000 };
            int rate = rates[Next() % 5];
            g_k.params[0] = Range(1, rate / 2) << 8;
            g_k.params[1] = Range(1, rate / 2) << 8;
            g_k.params[2] = rate << 8;
            Dual("fir 0x00145500", c, BodyFir4, 0, (uint32_t)kWorld);
        }
        // the inits, on garbage nodes
        WorldReset();
        FillSamples(At<float>(kExtra), 0x600 / 4);
        Dual("inits", c, BodyInits, kResults, kExtra + 0x600);
    }
}

// ---- section 1: pure functions, all originals in one window, then all ports (through the jumps)

void PureFunctions() {
    std::vector<int> argsA, argsB, resultO, resultP;
    for (int n = -100; n <= 120000; n++) {
        argsA.push_back(n);
        argsB.push_back(1000);
    }
    for (int i = 0; i < 3000; i++) {
        static const int rates[6] = { 8000, 16000, 22050, 32000, 44100, 48000 };
        argsA.push_back((Next() & 7) == 0 ? Range(0, 65535) : rates[Next() % 6]);
        argsB.push_back((Next() & 1) ? Range(0, 2000) : Range(0, 65535));
    }
    typedef int (*Int2)(int, int);
    resultO.resize(argsA.size());
    resultP.resize(argsA.size());
    // findprime
    UseOriginals(true);
    for (size_t i = 0; i < argsA.size(); i++)
        resultO[i] = ((Int2)0x0014a260)(argsA[i], argsB[i]);
    UseOriginals(false);
    for (size_t i = 0; i < argsA.size(); i++)
        resultP[i] = ((Int2)0x0014a260)(argsA[i], argsB[i]);
    int bad = 0;
    for (size_t i = 0; i < argsA.size(); i++)
        if (resultO[i] != resultP[i] && bad++ == 0)
            Report("findprime(%d, %d): original %d, ours %d", argsA[i], argsB[i], resultO[i], resultP[i]);
    g_cases += (int)argsA.size();
    g_checks += (int)argsA.size();
    g_differ += bad;
    // FUN_001465a0 on the random pairs (milliseconds / 16 inside)
    size_t first = 120101;
    UseOriginals(true);
    for (size_t i = first; i < argsA.size(); i++)
        resultO[i] = ((Int2)0x001465a0)(argsA[i], argsB[i]);
    UseOriginals(false);
    for (size_t i = first; i < argsA.size(); i++)
        resultP[i] = ((Int2)0x001465a0)(argsA[i], argsB[i]);
    bad = 0;
    for (size_t i = first; i < argsA.size(); i++)
        if (resultO[i] != resultP[i] && bad++ == 0)
            Report("FUN_001465a0(%d, %d): original %d, ours %d", argsA[i], argsB[i], resultO[i], resultP[i]);
    g_cases += (int)(argsA.size() - first);
    g_checks += (int)(argsA.size() - first);
    g_differ += bad;
    // the two series
    std::vector<float> xs;
    for (int i = 0; i < 20000; i++) {
        uint32_t r = Next() % 100;
        xs.push_back(r < 70 ? Unit() * 1.5f : r < 90 ? Unit() * 100.0f : r < 97 ? Unit() * 10000.0f : Sample());
    }
    xs.push_back(Float(0xff800000u));   // -inf (not +inf: cos's reduction never ends)
    xs.push_back(Float(0x7fc00001u));
    xs.push_back(0.0f);
    xs.push_back(-0.0f);
    xs.push_back(6.2831854820251465f);
    typedef double (*Series)(float);
    static const uint32_t series[2] = { 0x001465c0, 0x00146640 };
    for (int s = 0; s < 2; s++) {
        std::vector<double> o(xs.size()), p(xs.size());
        bool cosine = s == 1;
        UseOriginals(true);
        for (size_t i = 0; i < xs.size(); i++)
            if (!(cosine && xs[i] > 20000.0f))
                o[i] = ((Series)series[s])(xs[i]);
        UseOriginals(false);
        for (size_t i = 0; i < xs.size(); i++)
            if (!(cosine && xs[i] > 20000.0f))
                p[i] = ((Series)series[s])(xs[i]);
        bad = 0;
        int nan = 0;
        for (size_t i = 0; i < xs.size(); i++) {
            if (cosine && xs[i] > 20000.0f)
                continue;
            if (memcmp(&o[i], &p[i], 8) == 0)
                continue;
            if (o[i] != o[i] && p[i] != p[i]) {
                nan++;
                continue;
            }
            if (bad++ == 0) {
                uint64_t a, b;
                memcpy(&a, &o[i], 8);
                memcpy(&b, &p[i], 8);
                Report("0x%08x(%08x): original %016llx, ours %016llx", series[s], Bits(xs[i]),
                       (unsigned long long)a, (unsigned long long)b);
            }
        }
        g_cases += (int)xs.size();
        g_checks += (int)xs.size();
        g_differ += bad;
        if (nan != 0)
            CountNaNOnly(s == 0 ? "FUN_001465c0" : "FUN_00146640", nan);
    }
}

// ---- section 3: the whole mixer

const uint32_t kGlobalsFrom = 0x00245978, kGlobalsTo = 0x00247600;
const size_t kMixWorld = 0x200000;
const uint32_t kMixHeap = 0x180000;
const uint32_t kMixDesc = 0x180000;       // the reverb description, 0x400
const uint32_t kMixLog = 0x180400;        // voice-free log: count, entries
const uint32_t kMixParams = 0x180600;     // MIX_create's argument
const uint32_t kMixRings = 0x181000;      // 6 x kRingFrames int16
const int kRingFrames = 0x8000;
const int kSteps = 36;

uint8_t *g_mix;
uint8_t *g_mixStart, *g_mixOrig;
uint8_t g_globalsStart[kGlobalsTo - kGlobalsFrom], g_globalsOrig[kGlobalsTo - kGlobalsFrom];

struct StepHash {
    uint32_t globals, heap, rings;
};
StepHash g_hashO[kSteps + 2], g_hashP[kSteps + 2];
StepHash *g_hash;

struct Scenario {
    const char *name;
    int reverb;                      // 0 none, else the description to build
    bool masterLowpass;
    int pitchShift;                  // varies which voice gets which resampler pitch
    bool oddCounts;
};
const Scenario kScenarios[] = {
    { "no reverb", 0, false, 0, true },
    { "fxdefault", 1, false, 1, true },
    { "three groups", 2, true, 0, true },
    { "taps, no hook", 3, false, 0, false },
    { "fx2 network", 4, false, 0, false },
};
const Scenario *g_scenario;

const int kVoices[6] = { 0, 3, 7, 12, 20, 31 };
float g_dry[6][6], g_fx[6];
int g_frames[kSteps];
uint32_t g_seeds[6];
float g_amplitude[6];

void FakeVoiceFree(int voice) {
    int32_t *log = (int32_t *)(g_mix + kMixLog);
    if (log[0] < 60)
        log[1 + log[0]] = voice;
    log[0]++;
}

void FakeUnpackerInit(SND::SFilterNode *node, SND::UnpackInfo *info) {
    int *params = reinterpret_cast<int *>(info);   // logged and seeded from as words
    FakeSource *s = reinterpret_cast<FakeSource *>(node);
    s->head.process = FakeProcess;
    s->head.input = NULL;
    s->head.input2 = NULL;
    s->head.output = NULL;
    s->head.output2 = NULL;
    s->head.flags = 0;
    s->seed = (uint32_t)params[0];
    s->left = params[1];
    s->amplitude = Float((uint32_t)params[2]);
    s->forced = 0;
    s->calls = 0;
    for (int i = 0; i < 10; i++)
        s->params[i] = params[i];
    params[9] = 0x5000 + params[7];   // a recognisable getFrame for the voice's +0x44 (never called)
}

void Hash3(int step) {
    StepHash &h = g_hash[step];
    h.globals = Hash((const void *)(uintptr_t)kGlobalsFrom, kGlobalsTo - kGlobalsFrom);
    h.heap = Hash(g_mix, kMixHeap);
    h.rings = Hash(g_mix + kMixRings, 6u * kRingFrames * 2u);
}

void SetDry(int v) {
    for (int s = 0; s < 6; s++)
        ((void (*)(int, int, float))0x00141bb0)(kVoices[v], s, g_dry[v][s]);
}

void StartVoice(int v) {
    // samplerep 9 (no unpacker of its own) with kind 2: the table's empty slot 1, where the fake init sits
    ((void (*)(int, int, int, int, int, int, int, int, int, int, int, int, int))0x00141910)(
        kVoices[v], 9, 2, (int)g_seeds[v], v == 2 ? 5000 : -1, 0, (int)Bits(g_amplitude[v]), 7, 8, 0, 0, 0, v);
    int voice = kVoices[v];
    if (v % 3 == 0) {
        static const int pitches[4] = { 0x10000, 0x8000, 0x18000, 0x50000 };
        ((void (*)(int, int))0x001420c0)(voice, pitches[(v / 3 + g_scenario->pitchShift) & 3]);
    }
    if (v & 1)
        ((void (*)(int, float))0x00144af0)(voice, 0.25f);
    if (v % 4 == 1)
        ((void (*)(int, int))0x001464c0)(voice, 300);
    ((int (*)(int, int))0x00146570)(voice, 0x1000);
    SetDry(v);
    ((void (*)(int, int, float))0x00141be0)(voice, 0, g_fx[v]);
    ((void (*)(int))0x00141ad0)(voice);
}

void BuildDescription(int which) {
    uint8_t *d = g_mix + kMixDesc;
    memset(d, 0, 0x400);
    if (which == 1) {
        memcpy(d, (const void *)(uintptr_t)0x001d98c0, 0x18);   // fxdefault
        return;
    }
    if (which == 2 || which == 3) {
        static const uint8_t taps[] = { 0x00, 0x2b, 0x50, 0x30, 0x00, 0x1d, 0xb0, 0x60, 0x00, 0x3f, 0x40, 0x10,
                                        0x00, 0x11, 0x60, 0x7f, 0x00, 0x57, 0xc0, 0x50 };
        d[2] = which == 2 ? 0 : 5;
        d[4] = 2;
        d[5] = 2;
        d[6] = 1;
        d[7] = 0x80;
        memcpy(d + 8, taps, sizeof(taps));
        return;
    }
    // fx2: configurations 0 (1 node), 1 (2 nodes, 1 link) and 2 (9 nodes, 9 links) - six channels pick 2
    d[2] = 10;
    d[8 + 3] = 1;
    d[12 + 3] = 2;
    d[16 + 3] = 9;
    d[0x24 + 3] = 1;
    d[0x28 + 3] = 9;
    static const uint8_t entries[] = {
        0x06, 0x00, 0x01, 0x00,                                 // configuration 0: a gain
        0x06, 0x00, 0x01, 0x00, 0x06, 0x00, 0x01, 0x00,         // configuration 1: two gains
        0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0,                     //   and a link
        0x00, 0x00, 0x00, 0x00,                                 // 0 source
        0x09, 0x00, 0x00, 0x00,                                 // 1 tee
        0x07, 0x00, 0x00, 0xb4, 0x00, 0xa0, 0x00, 0x00,         // 2 allpass, gain 180/254, 160 ms
        0x06, 0x01, 0x00, 0xc0,                                 // 3 gain 0.75, route 1
        0x05, 0x00, 0x03, 0x20, 0x00, 0xc8, 0x01, 0x00,         // 4 resonator 800 Hz, bandwidth 200, gain 1
        0x08, 0x00, 0x00, 0x00,                                 // 5 sum
        0x02, 0x00, 0x01, 0x90,                                 // 6 FIR kind 2, 400 Hz
        0x04, 0x00, 0x00, 0xc8, 0x0f, 0xa0, 0x00, 0x00,         // 7 FIR kind 4, 200 / 4000 Hz
        0x01, 0x02, 0x17, 0x70, 0x01, 0x00, 0x00, 0x00,         // 8 RC low pass 6000 Hz, route 2
    };
    memcpy(d + 0x38, entries, sizeof(entries));
    static const uint8_t links[9][2] = { { 0, 1 }, { 1, 2 }, { 1, 3 }, { 2, 4 }, { 3, 5 }, { 4, 5 },
                                         { 5, 6 }, { 6, 7 }, { 7, 8 } };
    uint8_t *l = d + 0x38 + sizeof(entries);
    for (int i = 0; i < 9; i++, l += 12) {
        l[3] = links[i][0];
        l[7] = links[i][1];
        l[8] = 1;
        l[9] = (i == 5) ? 2 : 1;   // the resonator into the sum's second input
    }
}

void BodyMixer() {
    int16_t *rings[6];
    int offset = 0;
    int step = 0;
    SND::MixCreateParams *params = (SND::MixCreateParams *)(g_mix + kMixParams);
    ((void (*)(const SND::MixCreateParams *))0x00141c20)(params);
    SndMix.unpackerInit[0] = FakeUnpackerInit;   // slot 1
    SndMix.unpackerSize[0] = sizeof(FakeSource);
    // SndMix.resamplerMode stays 0, as in the game: nothing ever writes it, and in the resampler's second mode
    // SFILTER_rsfinit leaves the kernel pointer (+0x24) unset - SFILTER_rsf then calls heap garbage, on both sides
    if (g_scenario->reverb != 0)
        ((void (*)(int, const uint8_t *))0x00143510)(48000, g_mix + kMixDesc);
    if (g_scenario->masterLowpass)
        ((void (*)(float))0x00141630)(0.45f);
    for (int v = 0; v < 6; v++)
        StartVoice(v);
    Hash3(step++);
    for (int k = 0; k < kSteps; k++) {
        if (k == 3) {
            for (int s = 0; s < 6; s++)
                g_dry[0][s] = g_dry[0][s] * 0.5f + 0.1f;
            SetDry(0);
            ((void (*)(int, int, float))0x00141be0)(kVoices[3], 0, 0.9f);
        }
        if (k == 5)
            ((void (*)(int))0x00141b20)(kVoices[4]);   // MIX_stop: its last sample ramps to zero
        if (k == 8) {
            ((void (*)(int, float))0x00144af0)(kVoices[1], 1.0f);   // low pass off
            ((void (*)(int, int))0x001464c0)(kVoices[1], 0);        // high pass off
            ((void (*)(int, int))0x001420c0)(kVoices[0], 0x12345);
            ((void (*)(int, float))0x00144af0)(kVoices[3], 0.6f);   // low pass changed
        }
        if (k == 10) {
            for (int v = 0; v < 6; v++)
                ((void (*)(int, int, float))0x00141be0)(kVoices[v], 0, 0.0f);
            SndMix.fxIdle = 2990;   // near the idle cut-off
        }
        if (k == 13)
            StartVoice(4);           // played again
        if (k == 16)
            for (int v = 0; v < 6; v++)
                ((void (*)(int, int, float))0x00141be0)(kVoices[v], 0, g_fx[v]);
        if (k == 20)
            ((void (*)(float))0x00141630)(g_scenario->masterLowpass ? 1.5f : 0.3f);
        for (int c = 0; c < 6; c++)
            rings[c] = (int16_t *)(g_mix + kMixRings) + c * kRingFrames + offset;
        ((void (*)(int16_t **, int))0x00142050)(rings, g_frames[k]);
        offset += g_frames[k];
        Hash3(step++);
    }
    ((void (*)(void))0x001434b0)();   // MIX_restorereverb
    ((void (*)(void))0x00141880)();   // MIX_destroy
}

void MixerScenario(int index) {
    const Scenario &s = kScenarios[index];
    g_scenario = &s;
    // the starting state: zeroed globals, a fresh heap, the description, quiet rings
    memset((void *)(uintptr_t)kGlobalsFrom, 0, kGlobalsTo - kGlobalsFrom);
    memset(g_mix, 0, kMixWorld);
    HeapInit(g_mix, kMixHeap);
    BuildDescription(s.reverb);
    SND::MixCreateParams *params = (SND::MixCreateParams *)(g_mix + kMixParams);
    params->rate = 48000;
    params->counts.voices = 32;
    params->counts.channels = 6;
    params->voiceFree = FakeVoiceFree;
    for (int v = 0; v < 6; v++) {
        g_seeds[v] = Next();
        g_amplitude[v] = v == 5 ? 60000.0f : (float)Range(2000, 30000);
        for (int c = 0; c < 6; c++)
            g_dry[v][c] = (Next() % 5) == 0 ? 0.0f : (Unit() + 1.0f) * 0.5f;
        g_fx[v] = (Next() % 4) == 0 ? 0.0f : (Unit() + 1.0f) * 0.5f;
    }
    float dry[6][6], fx[6];
    memcpy(dry, g_dry, sizeof(dry));
    memcpy(fx, g_fx, sizeof(fx));
    int total = 0;
    for (int k = 0; k < kSteps; k++) {
        int n = (Next() & 3) == 0 ? Range(1, 120) * 16 : 960;
        if (s.oddCounts && (k % 7) == 4)
            n = Range(1, 700);
        if (total + n > kRingFrames - 1100)
            n = 16;
        g_frames[k] = n;
        total += n;
    }
    memcpy(g_mixStart, g_mix, kMixWorld);
    memcpy(g_globalsStart, (const void *)(uintptr_t)kGlobalsFrom, sizeof(g_globalsStart));

    g_hash = g_hashO;
    memset(g_hashO, 0, sizeof(g_hashO));
    UseOriginals(true);
    bool okO = Guarded(BodyMixer);
    UseOriginals(false);
    memcpy(g_mixOrig, g_mix, kMixWorld);
    memcpy(g_globalsOrig, (const void *)(uintptr_t)kGlobalsFrom, sizeof(g_globalsOrig));

    memcpy(g_mix, g_mixStart, kMixWorld);
    memcpy((void *)(uintptr_t)kGlobalsFrom, g_globalsStart, sizeof(g_globalsStart));
    memcpy(g_dry, dry, sizeof(dry));
    memcpy(g_fx, fx, sizeof(fx));
    g_hash = g_hashP;
    memset(g_hashP, 0, sizeof(g_hashP));
    bool okP = Guarded(BodyMixer);

    g_cases++;
    char what[96];
    snprintf(what, sizeof(what), "mixer \"%s\"", s.name);
    if (!okO || !okP) {
        g_faults++;
        Report("%s: a fault on the %s", what, !okO && !okP ? "both sides" : okO ? "port's side" : "original's side");
    }
    for (int i = 0; i < kSteps + 1; i++) {
        const StepHash &o = g_hashO[i], &p = g_hashP[i];
        if (o.globals != p.globals || o.heap != p.heap || o.rings != p.rings) {
            Report("%s: first differs after %s %d (globals %s, heap %s, rings %s)", what, i == 0 ? "setup" : "MIX_audio",
                   i, o.globals != p.globals ? "differ" : "same", o.heap != p.heap ? "differ" : "same",
                   o.rings != p.rings ? "differ" : "same");
            break;
        }
    }
    int bad = CompareImages(what, 0, g_globalsOrig, (const uint8_t *)(uintptr_t)kGlobalsFrom,
                            sizeof(g_globalsOrig), kGlobalsFrom);
    bad += CompareImages(what, 1, g_mixOrig, g_mix, kMixWorld, (uint32_t)(uintptr_t)g_mix);
    if (bad != 0 || okO != okP)
        g_differ++;
}

}   // namespace

void SndMixShadow_Run(void) {
    const char *flag = getenv("NIGHTFIRE_SNDMIXSHADOW");
    if (flag == NULL || atoi(flag) == 0)
        return;
    // (the jump's bytes are captured by the first swap to the original, so that comes first)
    g_patched = 0;
    for (int i = 0; i < kNumOurs; i++)
        if (XbeOriginal_Restore(kOurs[i], true)) {
            XbeOriginal_Restore(kOurs[i], false);
            g_patched++;
        }
    g_world = (uint8_t *)VirtualAlloc(NULL, kWorld, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_mix = (uint8_t *)VirtualAlloc(NULL, kMixWorld, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_start = (uint8_t *)VirtualAlloc(NULL, kMixWorld * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_orig = (uint8_t *)VirtualAlloc(NULL, kMixWorld * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_world == NULL || g_mix == NULL || g_start == NULL || g_orig == NULL) {
        printf("[sndmixshadow] out of memory\n");
        fflush(stdout);
        return;
    }
    g_mixStart = g_start + kMixWorld;
    g_mixOrig = g_orig + kMixWorld;

    // the state this touches, put back at the end
    std::vector<uint8_t> savedGlobals((uint8_t *)(uintptr_t)kGlobalsFrom, (uint8_t *)(uintptr_t)kGlobalsTo);
    uint32_t savedHeap = U32(0x00244f6c);
    uint16_t savedRate = U16(0x00244cf2);
    uint8_t savedQuality = U8(0x00244ed7);
    uint8_t savedNesting = U8(0x00244ed3);
    U16(0x00244cf2) = 48000;
    U8(0x00244ed7) = 0;

    PureFunctions();
    Kernels();
    for (int i = 0; i < (int)(sizeof(kScenarios) / sizeof(kScenarios[0])); i++)
        MixerScenario(i);

    memcpy((void *)(uintptr_t)kGlobalsFrom, savedGlobals.data(), savedGlobals.size());
    U32(0x00244f6c) = savedHeap;
    U16(0x00244cf2) = savedRate;
    U8(0x00244ed7) = savedQuality;
    U8(0x00244ed3) = savedNesting;
    VirtualFree(g_world, 0, MEM_RELEASE);
    VirtualFree(g_mix, 0, MEM_RELEASE);
    VirtualFree(g_start, 0, MEM_RELEASE);
    VirtualFree(g_orig, 0, MEM_RELEASE);

    const char *precision = "";
#ifdef _MSC_VER
    unsigned int control = _control87(0, 0);
    precision = (control & _MCW_PC) == _PC_53 ? ", x87 at 53 bits" : (control & _MCW_PC) == _PC_64 ? ", x87 at 64 bits"
                                                                                                       : ", x87 at 24 bits";
#endif
    if (g_nanOnly != 0) {
        char by[600] = "";
        for (int i = 0; i < 24 && g_nanBy[i].what[0] != 0; i++)
            snprintf(by + strlen(by), sizeof(by) - strlen(by), "%s %s %d", i ? "," : "", g_nanBy[i].what,
                     g_nanBy[i].cases);
        printf("[sndmixshadow]   NaN-only cases by test:%s\n", by);
    }
    printf("[sndmixshadow] G mixer and reverb (%d of %d entry points patched%s): %d cases, %d checks, %d differ"
           " (%d NaN-only, %d faults)\n",
           g_patched, kNumOurs, precision, g_cases, g_checks, g_differ, g_nanOnly, g_faults);
    fflush(stdout);
}
