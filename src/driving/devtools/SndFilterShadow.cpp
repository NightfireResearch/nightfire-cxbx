#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // as the build defines it (CMakeLists.txt)
#endif

#include "SndFilterShadow.h"
#include "FpControl.h"

#include "../sound/snd/Filters.h"
#include "../sound/snd/FiltersUnused.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xboxPath.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_SNDFILTERSHADOW=1, at injection time on the loader's thread before the game runs: EA's SFILTER graph
// (sound/snd/Filters.cpp and FiltersUnused.cpp, docs/driving/sound.md 4.8) against the originals, as sound.md 9.3
// step 4 asks - one node at a time behind a fake upstream node that replays a signal, outputs and node state
// compared bit for bit.
//
// Every case runs in one workspace (nodes, buffers, the fake upstream node and packet player, an arena standing in
// for the sound heap and the decoders' allocator): set up, snapshotted, run with the case's functions swapped back
// to the originals (common/xbeOriginal.h), saved, put back to the snapshot, run with ours, and the two workspaces
// compared word by word - with the resampler kernel's scratch globals, a hash of every call the fakes saw (frames,
// requester, buffers, in order) and whether a side faulted (SEH: a fault is counted, not fatal). Same addresses on
// both sides, so stored pointers compare too.
//
// Signals: white noise (+-1 and +-32768), a sine sweep, full-scale square waves (+-32768, and +-40000 for the
// clip), exact half-integers (the output stage's rounding), and disc speech - the first stream of
// driving\mis01en.spe decoded by the ORIGINAL EA-XA decoder (module I's range swapped back for it), whose packets
// also feed SFILTER_unpackxapf through a fake SNDPKTPLAYI_get. Resampler pitches 0x4000..0x20000 (pass-through
// 0x10000 included), low-pass cutoffs 0..24000 Hz, high-pass cutoffs 0..24000 Hz; the designs (FUN_00146930 in
// modes 2, 3 and 4, the sine FUN_0014a2e0) compared on their coefficient output; the list and connection
// functions on random graphs; SFILTER_add with each init. The data-dead unpackers run on synthetic data too: PCM16
// buffers and packets, the disc's EA-XA blocks as a bank sample (looping and not), random bytes for MicroTalk (the
// decoders only need not to fault), a synthetic tag-0x98 blob for the time stretch - their first-run warnings
// print during this test.
//
// The packet player, SNDDRV voice lookups, SNDMEMI_alloc and SNDPKTPLAY_framesoutstanding are replaced for the
// run by five-byte jumps to fakes (put back after), CODA_New/CODA_Delete pointed at the arena.
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types

using namespace SND;

// ---- what each kind of case swaps back to the original (0-terminated)

const unsigned kAllOurs[] = {
    0x00144a30, 0x00144460, 0x001444a0, 0x001444f0, 0x001436c0, 0x00143710, 0x00143740, 0x00145720, 0x00145560,
    0x001454d0, 0x001455b0, 0x001466c0, 0x001466e0, 0x00146930, 0x0014a2e0, 0x00144710, 0x00144920, 0x00144700,
    0x001462b0, 0x001456b0, 0x001456e0, 0x00145700, 0x00144590, 0x00144610, 0x00145ab0, 0x00145bf0, 0x00145c10,
    0x00145ed0, 0x00145f40, 0x00145f50, 0x00145cc0, 0x00145e40, 0x00145830, 0x00145970, 0x001459c0, 0x00145a70,
    0x00146000, 0x001460e0, 0x00146130, 0x001461a0, 0x001461e0, 0x001461f0, 0x00146280, 0x00144340, 0x001443f0,
    0x00144300, 0x001441a0, 0x001440b0, 0x00143be0, 0x00143e10, 0x00143f20, 0x00144050, 0 };

const unsigned kSine[] = { 0x0014a2e0, 0 };
const unsigned kDesign[] = { 0x00146930, 0x0014a2e0, 0 };
const unsigned kFir[] = { 0x001466e0, 0 };
const unsigned kLpfState[] = { 0x00145720, 0 };
const unsigned kLpfNode[] = { 0x001436c0, 0x00143710, 0x00143740, 0x00145720, 0 };
const unsigned kHpfNode[] = { 0x00145560, 0x001454d0, 0x001455b0, 0x001466c0, 0x001466e0, 0x00146930, 0x0014a2e0, 0 };
const unsigned kRsfNode[] = { 0x00144710, 0x00144920, 0x00144700, 0x001462b0, 0 };
const unsigned kKernel[] = { 0x001462b0, 0 };
const unsigned kOutput[] = { 0x00144590, 0x00144610, 0 };
const unsigned kSource[] = { 0x001456b0, 0x001456e0, 0x00145700, 0 };
const unsigned kGraph[] = { 0x00144460, 0x001444a0, 0x001444f0, 0 };
const unsigned kAdd[] = { 0x00144a30, 0x00144460, 0x00144920, 0x00143710, 0x001454d0, 0x001466c0, 0x00144610,
                          0x001456e0, 0x00145700, 0 };
const unsigned kXapf[] = { 0x00145ab0, 0x00145bf0, 0x00145c10, 0 };
const unsigned kXaf[] = { 0x00145ed0, 0x00145f40, 0x00145f50, 0x00145bf0, 0 };
const unsigned kXalf[] = { 0x00145cc0, 0x00145e40, 0x00145f40, 0x00145bf0, 0 };
const unsigned kPf[] = { 0x00146000, 0x001460e0, 0 };
const unsigned kLf[] = { 0x00146130, 0x001461a0, 0x001461e0, 0 };
const unsigned kF[] = { 0x001461f0, 0x00146280, 0x001461e0, 0 };
const unsigned kMtpf[] = { 0x00145830, 0x00145970, 0 };
const unsigned kMtf[] = { 0x001459c0, 0x00145a70, 0x001461e0, 0 };
const unsigned kStretch[] = { 0x00144340, 0x001443f0, 0x00144300, 0x001441a0, 0x001440b0, 0x00143be0, 0x00143e10,
                              0x00143f20, 0x00144050, 0 };

void Originals(const unsigned *list, bool original) {
    for (; *list != 0; list++)
        XbeOriginal_Restore(*list, original);
}

// ---- random numbers

uint32_t g_seed = 1;

uint32_t Next() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

int Range(int lo, int hi) {   // inclusive
    return lo + (int)(Next() % (uint32_t)(hi - lo + 1));
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * (float)(Next() & 0xffffff) / 16777216.0f;
}

bool Chance(int percent) {
    return (int)(Next() % 100) < percent;
}

// ---- signals

const int kSignalLength = 1 << 16;
enum { SIG_NOISE1, SIG_NOISE16, SIG_LOUD, SIG_SWEEP, SIG_SQUARE, SIG_SQUARE_OVER, SIG_HALVES, SIG_SPEECH, SIG_COUNT };
float *g_signal[SIG_COUNT];
int g_speechFrames;

// ---- packets (the fake packet player's)

struct Packet {
    const uint8_t *data;
    int frames;
};
const int kMaxPackets = 64;
Packet g_xa[kMaxPackets];   // EA-XA: two history shorts, then 15-byte blocks
int g_xaCount;
bool g_xaFromDisc;
Packet g_pcm[16];           // PCM16
Packet g_mt[16];            // random bytes for MicroTalk
uint8_t *g_file;

// ---- the workspace

const int kStride = 1104;   // floats per block (a multiple of four: each block's buffers stay 16-byte aligned)
const int kBlocks = 8;

struct FakeUp {             // the fake upstream node
    SFilterNode node;
    uint32_t signal;
    uint32_t position;
    int32_t left;           // frames it can still give
    int32_t endValue;       // its answer once empty
    uint32_t partial;       // answers what it had rather than what was asked, when it runs out
    uint32_t calls;
};

struct FakePlayer {
    uint32_t kind;          // 0 EA-XA, 1 PCM16, 2 MicroTalk
    uint32_t next, count;
    uint32_t calls;
    int32_t freed;
    int32_t outstanding;
};

struct Workspace {
    float scratch[kBlocks * kStride];
    float out[kBlocks * kStride];
    uint8_t node[0x4000];
    FakeUp up;
    FakePlayer player;
    SFilterNode graph[16];
    SFilterNode *head;
    uint32_t pad0[3];
    uint8_t mix[32 * 0x60];
    UnpackInfo info;
    SFilterDesc desc[6];
    int32_t params[4];
    uint8_t blob[4096];
    float src[4096];
    int16_t pcm[8192];
    double dres[64];
    int32_t results[64];
    uint32_t arenaUsed;
    uint32_t pad1[3];
    uint8_t arena[0x8000];
};

Workspace *W, *g_snapshot, *g_resultOriginal;

const uint32_t kKernelGlobals = 0x001da4e0u, kKernelGlobalsSize = 0x50;
uint8_t g_globalsSnapshot[kKernelGlobalsSize], g_globalsOriginal[kKernelGlobalsSize];

// ---- the per-side call log

int g_side;
uint32_t g_hash[2], g_logCount[2];

void Log(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) {
    uint32_t v[4] = { a, b, c, d };
    uint32_t h = g_hash[g_side];
    for (int i = 0; i < 16; i++)
        h = (h ^ ((const uint8_t *)v)[i]) * 16777619u;
    g_hash[g_side] = h;
    g_logCount[g_side]++;
}

uint32_t P(const void *p) {
    return (uint32_t)(uintptr_t)p;
}

// ---- the fakes (cdecl, called by the original and by ours alike)

void *ArenaAlloc(uint32_t size) {
    uint32_t at = (W->arenaUsed + 15u) & ~15u;
    if (size > sizeof(W->arena) || at + size > sizeof(W->arena))
        return NULL;
    W->arenaUsed = at + size;
    return W->arena + at;
}

int FakeProcess(SFilterNode *node, int frames, float *a3, float *a4, int requester) {
    FakeUp *u = (FakeUp *)node;
    u->calls++;
    Log(0x1001, (uint32_t)frames, (uint32_t)requester, P(a3) ^ (P(a4) << 1));
    if (u->left <= 0)
        return u->endValue;
    int n = frames < u->left ? frames : u->left;
    uint8_t *lo = (uint8_t *)W, *hi = (uint8_t *)W + sizeof(Workspace);
    if ((uint8_t *)a4 < lo || (uint8_t *)a4 >= hi)
        n = 0;
    else if (n > (int)((hi - (uint8_t *)a4) / 4))
        n = (int)((hi - (uint8_t *)a4) / 4);
    const float *s = g_signal[u->signal];
    for (int i = 0; i < n; i++)
        a4[i] = s[(u->position + (uint32_t)i) & (kSignalLength - 1)];
    if (n > 0)
        u->position += (uint32_t)n;
    if (frames > u->left) {
        int answer = u->partial ? u->left : frames;
        u->left = 0;
        return answer;
    }
    u->left -= frames;
    return frames;
}

void *FakeGetPacket(int player, int channel, int *frames, int *other) {
    FakePlayer *p = &W->player;
    p->calls++;
    Log(0x1002, (uint32_t)player, (uint32_t)channel, p->next);
    if (p->next >= p->count)
        return NULL;
    const Packet *list = p->kind == 0 ? g_xa : p->kind == 1 ? g_pcm : g_mt;
    const Packet &packet = list[p->next++];
    *frames = packet.frames;
    *other = 0x55;
    return (void *)packet.data;
}

void FakeFreeFrames(int player, int channel, int frames) {
    Log(0x1003, (uint32_t)player, (uint32_t)channel, (uint32_t)frames);
    W->player.freed += frames;
}

int FakeGetMasterVoice(int voice) {
    Log(0x1004, (uint32_t)voice);
    return voice + 7;
}

int FakeVoiceToPacketHandle(int voice) {
    Log(0x1005, (uint32_t)voice);
    return voice ^ 0x5a;
}

int FakeGetSampleChan(int voice) {
    Log(0x1006, (uint32_t)voice);
    return voice & 1;
}

void *FakeCodaNew(uint32_t size) {
    void *p = ArenaAlloc(size);
    Log(0x1007, size, P(p));
    return p;
}

void FakeCodaDelete(void *p) {
    Log(0x1008, P(p));
}

void *FakeSndMemAlloc(uint32_t size) {
    void *p = ArenaAlloc(size);
    Log(0x1009, size, P(p));
    return p;
}

int FakeFramesOutstanding(int player) {
    Log(0x100a, (uint32_t)player);
    return W->player.outstanding;
}

// ---- five-byte jumps over the originals the fakes stand in for

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[8];
int g_hookCount;

void HookInstall(uint32_t at, const void *to) {
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old)) {
        h.on = false;
        return;
    }
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = (int32_t)((uint32_t)(uintptr_t)to - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    h.on = true;
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
        Hook &h = g_hooks[i];
        if (!h.on)
            continue;
        DWORD old;
        VirtualProtect((void *)(uintptr_t)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)h.at, h.saved, 5);
        VirtualProtect((void *)(uintptr_t)h.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)h.at, 5);
        h.on = false;
    }
    g_hookCount = 0;
}

// ---- function types and the pick between the original's address and ours

typedef int (*ProcessFn)(SFilterNode *, int, float *, float *, int);
typedef void (*NodeFn)(void *);
typedef int (*IntNodeFn)(void *);
typedef void (*Init2Fn)(void *, void *);
typedef void (*Init3Fn)(void *, uint32_t, int);
typedef void (*ModifyFn)(void *, const int *);
typedef void (*FirFn)(void *, int, const float *, float *);
typedef void (*DesignFn)(void *, int);
typedef double (*SineFn)(float);
typedef void (*KernelFn)(int, const float *, float *, int *, uint32_t *, int, uint32_t);
typedef void *(*ListFn)(void *, void *);
typedef int (*ConnectFn)(void *, void *, int, int);
typedef void *(*AddFn)(int, uint32_t, const void *);
typedef void (*SetPitchFn)(void *, uint32_t);
typedef int (*Ft24Fn)(void *, int, float *, int16_t *, int);
typedef int (*StretchInitFn)(void *, const uint8_t *, int);
typedef void (*StretchRatioFn)(void *, int);

#define PICK(type, address, port) (original ? (type)(uintptr_t)(address) : (type)(port))

// ---- the case parameters (outside the workspace, read-only during a run)

struct Params {
    int blocks;
    int frames[kBlocks];
    int requester;
    int mode;
    int withUp;
    int noKernel;
    uint32_t pitch, pitch2;
    int changeAt;
    float x[64];
    int count, stepInt;
    uint32_t stepFraction;
    int ops[64][5];
    int opCount;
    int voice[6];
    uint32_t arg[6];
    int stretchVoice, ratio;
    int sharedScratch;
} g_p;

void FillBuffers() {
    const float *noise = g_signal[SIG_NOISE1];
    for (int i = 0; i < kBlocks * kStride; i++) {
        W->scratch[i] = noise[(i * 7 + 3) & (kSignalLength - 1)];
        W->out[i] = noise[(i * 13 + 5) & (kSignalLength - 1)];
    }
}

void SetupUp(int signal, int left) {
    W->up.node.process = &FakeProcess;
    W->up.signal = (uint32_t)signal;
    W->up.position = Next() & (kSignalLength - 1);
    W->up.left = left;
    W->up.endValue = Chance(50) ? 0 : -1;
    W->up.partial = Chance(30) ? 1 : 0;
}

int PickSignal() {
    static const int kinds[] = { SIG_NOISE1, SIG_NOISE16, SIG_SWEEP, SIG_SQUARE, SIG_SPEECH, SIG_SPEECH, SIG_LOUD,
                                 SIG_HALVES };
    int s = kinds[Next() % 8];
    return s == SIG_SPEECH && g_speechFrames == 0 ? SIG_NOISE16 : s;
}

void RandomFrames(int max) {
    static const int special[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 28, 29, 64, 100 };
    g_p.blocks = Range(1, kBlocks);
    for (int b = 0; b < kBlocks; b++) {
        int f = Chance(25) ? special[Next() % 16] : Range(1, max);
        if (Chance(10))
            f = max;
        g_p.frames[b] = f > max ? max : f;
    }
}

void RunBlocks(ProcessFn fn, SFilterNode *node) {
    for (int b = 0; b < g_p.blocks; b++) {
        float *scratch = g_p.sharedScratch ? W->scratch : W->scratch + b * kStride;
        W->results[b] = fn(node, g_p.frames[b], scratch, W->out + b * kStride, g_p.requester);
    }
}

// ---- the kinds of case

void SetupSine(int i) {
    for (int j = 0; j < 64; j++) {
        uint32_t r = Next() % 6;
        float x;
        if (r == 0)
            x = Uniform(-10.0f, 30.0f);
        else if (r == 1)
            x = (float)Range(0, 16) * 0.7853981852531433f * Uniform(0.0f, 1.0f);
        else if (r == 2)
            x = Uniform(0.0f, 7.0f);
        else if (r == 3)
            x = (float)Range(0, 8) * 6.2831854820251465f;
        else if (r == 4)
            x = Uniform(-2000.0f, 2000.0f);
        else
            x = Uniform(-1e-3f, 1e-3f);
        g_p.x[j] = x;
    }
    (void)i;
}

void RunSine(bool original) {
    SineFn fn = PICK(SineFn, 0x0014a2e0, &FUN_0014a2e0);
    for (int j = 0; j < 64; j++)
        W->dres[j] = fn(g_p.x[j]);
}

float RandomCutoff() {
    uint32_t r = Next() % 8;
    if (r == 0)
        return 0.0f;
    if (r == 1)
        return Uniform(0.7f, 1.3f);
    if (r == 2)
        return (float)Range(0, 24000) / 24000.0f;
    if (r == 3)
        return 0.5f;
    return Uniform(0.0f, 1.0f);
}

void SetupDesign(int i) {
    static const int modes[] = { 2, 3, 4, 2, 3, 4, 3, 4, 0, 5 };
    FirState *s = (FirState *)W->node;
    for (int k = 0; k < 8; k++)
        s->history[k] = Uniform(-1.0f, 1.0f);
    for (int k = 0; k < 5; k++)
        s->coef[k] = Uniform(-1.0f, 1.0f);
    s->cutoff = RandomCutoff();
    s->cutoff2 = RandomCutoff();
    g_p.mode = modes[i % 10];
}

void RunDesign(bool original) {
    PICK(DesignFn, 0x00146930, &FUN_00146930)(W->node, g_p.mode);
}

void SetupFir(int i) {
    (void)i;
    FirState *s = (FirState *)W->node;
    int signal = PickSignal();
    for (int k = 0; k < 8; k++)
        s->history[k] = g_signal[signal][Next() & (kSignalLength - 1)];
    for (int k = 0; k < 5; k++)
        s->coef[k] = Uniform(-0.6f, 0.6f);
    RandomFrames(512);
    for (int k = 0; k < kBlocks * kStride; k++)
        W->scratch[k] = g_signal[signal][(k + 977) & (kSignalLength - 1)];
}

void RunFir(bool original) {
    FirFn fn = PICK(FirFn, 0x001466e0, &FUN_001466e0);
    for (int b = 0; b < g_p.blocks; b++)
        fn(W->node, g_p.frames[b], W->scratch + b * kStride, W->out + b * kStride);
}

void SetupLpfState(int i) {
    float *s = (float *)W->node;
    s[0] = Uniform(-1.0f, 1.0f);
    s[1] = Uniform(0.0f, 1.0f);
    s[2] = Uniform(0.0f, 1.0f);
    RandomFrames(512);
    if (i % 7 == 0)
        g_p.frames[0] = -1;
    int signal = PickSignal();
    for (int k = 0; k < kBlocks * kStride; k++)
        W->scratch[k] = g_signal[signal][(k + 31) & (kSignalLength - 1)];
}

void RunLpfState(bool original) {
    FirFn fn = PICK(FirFn, 0x00145720, &FUN_00145720);
    for (int b = 0; b < g_p.blocks; b++)
        fn(W->node, g_p.frames[b], W->scratch + b * kStride, W->out + b * kStride);
}

void SetupChain(int i, int maxFrames) {
    g_p.withUp = Chance(85);
    g_p.requester = Range(0, 3);
    SetupUp(PickSignal(), Chance(70) ? 1 << 20 : Range(0, 1500));
    RandomFrames(maxFrames);
    if (!g_p.withUp) {
        int signal = PickSignal();
        for (int k = 0; k < kBlocks * kStride; k++)
            W->scratch[k] = g_signal[signal][(k + 11) & (kSignalLength - 1)];
    }
    (void)i;
}

void SetupLpfNode(int i) {
    SetupChain(i, 512);
    W->params[0] = Chance(50) ? (i % 25) * 1000 : Range(0, 24000);
    W->params[1] = Chance(80) ? 48000 : Range(8000, 48000);
    W->params[2] = Chance(60) ? 256 : Range(0, 512);
}

void RunLpfNode(bool original) {
    SFilterLPFRC *node = (SFilterLPFRC *)W->node;
    PICK(IntNodeFn, 0x00143710, &SFILTER_createLPFRC)(node);
    PICK(ModifyFn, 0x00143740, &SFILTER_modifyLPFRC)(node, W->params);
    if (g_p.withUp)
        node->node.input = &W->up.node;
    RunBlocks(PICK(ProcessFn, 0x001436c0, &SFILTER_lpfRC), &node->node);
}

void SetupHpfNode(int i) {
    SetupChain(i, 512);
    int cutoff = Chance(50) ? (i % 25) * 1000 : Range(0, 24000);
    W->params[0] = cutoff << 7;
    W->params[1] = 48000 << 8;
    if (Chance(10)) {
        W->params[0] = (int)Next();
        W->params[1] = (int)(Next() | 0x10000);
    }
}

void RunHpfNode(bool original) {
    SFilterFIR8 *node = (SFilterFIR8 *)W->node;
    PICK(IntNodeFn, 0x001454d0, &SFILTER_createHPFFIR8)(node);
    PICK(ModifyFn, 0x001455b0, &SFILTER_modifyHPFFIR8)(node, W->params);
    if (g_p.withUp)
        node->node.input = &W->up.node;
    RunBlocks(PICK(ProcessFn, 0x00145560, &SFILTER_hpfFIR8), &node->node);
}

uint32_t RandomPitch(int i) {
    static const uint32_t pitches[] = { 0x4000, 0x6000, 0x8000, 0xa000, 0xc000, 0xe000, 0xffff, 0x10000, 0x10000,
                                        0x10001, 0x12345, 0x14000, 0x18000, 0x1c000, 0x1ffff, 0x20000 };
    if (i % 3 == 0)
        return pitches[(i / 3) % 16];
    return (uint32_t)Range(0x4000, 0x20000);
}

void SetupRsfNode(int i) {
    SetupChain(i, 512);
    g_p.pitch = RandomPitch(i);
    g_p.pitch2 = RandomPitch(i + 1);
    g_p.changeAt = Chance(30) ? Range(1, kBlocks - 1) : -1;
    g_p.noKernel = Chance(5);
    ((SFilterRSF *)W->node)->kernel = (RsfKernel)0x001462b0;
}

void RunRsfNode(bool original) {
    SFilterRSF *node = (SFilterRSF *)W->node;
    PICK(Init3Fn, 0x00144920, &SFILTER_rsfinit)(node, 0, g_p.noKernel);
    SetPitchFn setPitch = PICK(SetPitchFn, 0x00144700, &SFILTER_rsfsetpitch);
    setPitch(node, g_p.pitch);
    if (g_p.withUp)
        node->node.input = &W->up.node;
    ProcessFn fn = PICK(ProcessFn, 0x00144710, &SFILTER_rsf);
    for (int b = 0; b < g_p.blocks; b++) {
        if (b == g_p.changeAt)
            setPitch(node, g_p.pitch2);
        W->results[b] = fn(&node->node, g_p.frames[b], W->scratch + b * kStride, W->out + b * kStride,
                           g_p.requester);
    }
}

void SetupKernel(int i) {
    static const int counts[] = { 0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 24, 64, 100, 255, 256, 511, 512, -1, -3, -7, -8 };
    g_p.count = i % 2 ? counts[(i / 2) % 21] : Range(0, 512);
    g_p.stepInt = Range(0, 2);
    g_p.stepFraction = Next();
    if (Chance(20))
        g_p.stepFraction &= 0xffff0000u;
    W->results[0] = Range(0, 3);
    W->results[1] = (int32_t)Next();
    int signal = PickSignal();
    for (int k = 0; k < 4096; k++)
        W->src[k] = g_signal[signal][(k + 1234) & (kSignalLength - 1)];
}

void RunKernel(bool original) {
    PICK(KernelFn, 0x001462b0, &FUN_001462b0)(g_p.count, W->src, W->out, &W->results[0], (uint32_t *)&W->results[1],
                                              g_p.stepInt, g_p.stepFraction);
}

void SetupOutput(int i) {
    static const int signals[] = { SIG_SQUARE, SIG_SQUARE_OVER, SIG_LOUD, SIG_HALVES, SIG_NOISE16, SIG_SPEECH,
                                   SIG_NOISE1 };
    SetupChain(i, 512);
    int signal = signals[i % 7];
    if (signal == SIG_SPEECH && g_speechFrames == 0)
        signal = SIG_LOUD;
    W->up.signal = (uint32_t)signal;
    if (!g_p.withUp)
        for (int k = 0; k < kBlocks * kStride; k++)
            W->scratch[k] = g_signal[signal][(k + 77) & (kSignalLength - 1)];
}

void RunOutput(bool original) {
    SFilterNode *node = (SFilterNode *)W->node;
    PICK(NodeFn, 0x00144610, &SFILTER_ft16init)(node);
    if (g_p.withUp)
        node->input = &W->up.node;
    Ft24Fn fn = PICK(Ft24Fn, 0x00144590, &SFILTER_ft24_32);
    for (int b = 0; b < g_p.blocks; b++)
        W->results[b] = fn(node, g_p.frames[b], W->scratch + b * kStride, (int16_t *)(W->out + b * kStride),
                           g_p.requester);
}

void SetupSource(int i) {
    (void)i;
    RandomFrames(500);
    int signal = PickSignal();
    for (int k = 0; k < 4096; k++)
        W->src[k] = g_signal[signal][(k + 5) & (kSignalLength - 1)];
}

void RunSource(bool original) {
    SFilterSource *node = (SFilterSource *)W->node;
    PICK(IntNodeFn, 0x00145700, &SFILTER_createSOURCE)(node);
    PICK(Init2Fn, 0x001456e0, &SFILTER_initSOURCE)(node, W->src);
    RunBlocks(PICK(ProcessFn, 0x001456b0, &SFILTER_src), &node->node);
}

// Nodes 0..7 go in and out of the list (only nodes not in it are added: a node added twice would make a cycle
// the list walk never leaves); nodes 8..15 are connected, apart, since connect writes the same links.
void SetupGraph(int i) {
    (void)i;
    for (int k = 0; k < 16; k++)
        W->graph[k].priority = (uint16_t)(Range(0, 5) * 40);
    bool in[8] = {};
    int count = 0;
    g_p.opCount = 0;
    while (g_p.opCount < 64) {
        int *op = g_p.ops[g_p.opCount];
        uint32_t r = Next() % 10;
        if (r < 4 && count < 8) {
            int k = Range(0, 7);
            while (in[k])
                k = (k + 1) % 8;
            op[0] = 0;
            op[1] = k;
            in[k] = true;
            count++;
        } else if (r < 7) {
            op[0] = 2;
            op[1] = Range(8, 15);
            op[2] = Range(8, 15);
            op[3] = Range(0, 3);
            op[4] = Range(0, 3);
        } else if (count > 0) {
            op[0] = 1;
            op[1] = Range(0, 7);
            if (in[op[1]]) {
                in[op[1]] = false;
                count--;
            }
        } else {
            continue;
        }
        g_p.opCount++;
    }
}

void RunGraph(bool original) {
    ListFn add = PICK(ListFn, 0x00144460, &SFILTER_addtofilterlist);
    ListFn remove = PICK(ListFn, 0x001444a0, &SFILTER_remove);
    ConnectFn connect = PICK(ConnectFn, 0x001444f0, &SFILTER_connect);
    for (int k = 0; k < g_p.opCount; k++) {
        const int *op = g_p.ops[k];
        if (op[0] == 0)
            W->results[k] = (int32_t)P(add(&W->head, &W->graph[op[1]]));
        else if (op[0] == 1)
            W->results[k] = (int32_t)P(remove(&W->head, &W->graph[op[1]]));
        else
            W->results[k] = connect(&W->graph[op[1]], &W->graph[op[2]], op[3], op[4]);
    }
}

void SetupAdd(int i) {
    (void)i;
    static const uint32_t inits[][2] = { { 0x00144920, 0x3c }, { 0x00143710, 0x28 }, { 0x001454d0, 0x58 },
                                         { 0x00144610, 0x1c }, { 0x001456e0, 0x20 }, { 0x00145700, 0x20 } };
    *(uint8_t **)0x00245be4u = W->mix;   // MixList, put back at the end of the run
    for (int k = 0; k < 6; k++) {
        const uint32_t *init = inits[Next() % 6];
        SFilterDesc &d = W->desc[k];
        d.init = (SFilterInit)(uintptr_t)init[0];
        d.size = init[1];
        d.param = init[0] == 0x001456e0 ? P(W->src) : Next();
        d.priority = (uint16_t)Range(0, 300);
        d.process = (SFilterProcess)(uintptr_t)Next();   // never called: only copied into the node
        d.restore = (SFilterRestore)(uintptr_t)Next();
        g_p.voice[k] = Range(0, 3);
        g_p.arg[k] = Chance(90) ? 0 : 1;
    }
}

void RunAdd(bool original) {
    AddFn fn = PICK(AddFn, 0x00144a30, &SFILTER_add);
    for (int k = 0; k < 6; k++)
        W->results[k] = (int32_t)P(fn(g_p.voice[k], g_p.arg[k], &W->desc[k]));
}

void SetupPlayer(uint32_t kind, int available) {
    W->player.kind = kind;
    W->player.next = available > 1 ? (uint32_t)Range(0, available - 1) : 0;
    W->player.count = W->player.next + (uint32_t)Range(1, 4);
    if (W->player.count > (uint32_t)available)
        W->player.count = (uint32_t)available;
    W->player.outstanding = Range(0, 3000);
}

void SetupXapf(int i) {
    (void)i;
    W->info.voice = Range(0, 31);
    W->info.frames = Range(0, 5000);
    SetupPlayer(0, g_xaCount);
    RandomFrames(1024);
    g_p.requester = Range(0, 2);
}

void RunXapf(bool original) {
    SFilterXAPF *node = (SFilterXAPF *)W->node;
    PICK(Init2Fn, 0x00145c10, &SFILTER_unpackxapfinit)(node, &W->info);
    RunBlocks(PICK(ProcessFn, 0x00145ab0, &SFILTER_unpackxapf), &node->node);
    PICK(NodeFn, 0x00145bf0, &SFILTER_unpackxapfrestore)(node);
}

void SetupBankXa(int i) {
    (void)i;
    const Packet &p = g_xa[Next() % (uint32_t)g_xaCount];
    W->info.data = p.data + 4;
    W->info.frames = Range(1, p.frames);
    W->info.loopStart = Range(0, W->info.frames > 2 ? W->info.frames - 2 : 0);
    W->info.loopEnd = Range(W->info.loopStart + 1, W->info.frames > W->info.loopStart + 1 ? W->info.frames - 1
                                                                                           : W->info.loopStart + 1);
    RandomFrames(1024);
    for (int b = 0; b < kBlocks; b++)
        if (g_p.frames[b] == 0)
            g_p.frames[b] = 1;   // xalf decodes on with nothing asked: the original's own loop, not a test
}

void RunXaf(bool original) {
    SFilterXAF *node = (SFilterXAF *)W->node;
    PICK(Init2Fn, 0x00145f50, &SFILTER_unpackxafinit)(node, &W->info);
    RunBlocks(PICK(ProcessFn, 0x00145ed0, &SFILTER_unpackxaf), &node->node);
    W->results[20] = W->info.getFrame(&node->node);
    PICK(NodeFn, 0x00145bf0, &SFILTER_unpackxapfrestore)(node);
}

void RunXalf(bool original) {
    SFilterXALF *node = (SFilterXALF *)W->node;
    PICK(Init2Fn, 0x00145e40, &SFILTER_unpackxalfinit)(node, &W->info);
    RunBlocks(PICK(ProcessFn, 0x00145cc0, &SFILTER_unpackxalf), &node->node);
    W->results[20] = W->info.getFrame(&node->node);
    PICK(NodeFn, 0x00145bf0, &SFILTER_unpackxapfrestore)(node);
}

void SetupPf(int i) {
    (void)i;
    W->info.voice = Range(0, 31);
    W->info.flag = Chance(80) ? 1 : (Next() & 0xffffff00u);
    SetupPlayer(1, 16);
    RandomFrames(1024);
}

void RunPf(bool original) {
    SFilterPF *node = (SFilterPF *)W->node;
    PICK(Init2Fn, 0x001460e0, &SFILTER_unpackpfinit)(node, &W->info);
    RunBlocks(PICK(ProcessFn, 0x00146000, &SFILTER_unpackpf), &node->node);
}

void SetupPcmBank(int i) {
    (void)i;
    int signal = PickSignal();
    for (int k = 0; k < 8192; k++) {
        float f = g_signal[signal][(k + 99) & (kSignalLength - 1)];
        if (f > 32767.0f)
            f = 32767.0f;
        if (f < -32768.0f)
            f = -32768.0f;
        W->pcm[k] = (int16_t)(f >= -1.0f && f <= 1.0f ? f * 32767.0f : f);
    }
    W->info.data = W->pcm;
    W->info.frames = Range(0, 8192);
    W->info.loopStart = Range(0, 4000);
    W->info.loopEnd = Range(W->info.loopStart, 8000);
    W->info.flag = Chance(85) ? 1 : 0;
    RandomFrames(1024);
}

void RunLf(bool original) {
    SFilterLF *node = (SFilterLF *)W->node;
    PICK(Init2Fn, 0x001461a0, &SFILTER_unpacklfinit)(node, &W->info);
    RunBlocks(PICK(ProcessFn, 0x00146130, &SFILTER_unpacklf), &node->node);
    W->results[20] = W->info.getFrame(&node->node);
}

void RunF(bool original) {
    SFilterF *node = (SFilterF *)W->node;
    PICK(Init2Fn, 0x00146280, &SFILTER_unpackfinit)(node, &W->info);
    RunBlocks(PICK(ProcessFn, 0x001461f0, &SFILTER_unpackf), &node->node);
    W->results[20] = W->info.getFrame(&node->node);
}

void SetupMtpf(int i) {
    (void)i;
    W->info.voice = Range(0, 31);
    SetupPlayer(2, 16);
    RandomFrames(1024);
}

void RunMtpf(bool original) {
    SFilterNode *node = (SFilterNode *)W->node;
    PICK(Init2Fn, 0x00145970, &SFILTER_unpackmtpfinit)(node, &W->info);
    RunBlocks(PICK(ProcessFn, 0x00145830, &SFILTER_unpackmtpf), node);
}

void SetupMtf(int i) {
    (void)i;
    W->info.data = g_mt[Next() % 16].data;
    W->info.frames = Range(1, 4000);
    RandomFrames(1024);
}

void RunMtf(bool original) {
    SFilterNode *node = (SFilterNode *)W->node;
    PICK(Init2Fn, 0x00145a70, &SFILTER_unpackmtfinit)(node, &W->info);
    RunBlocks(PICK(ProcessFn, 0x001459c0, &SFILTER_unpackmtf), node);
    W->results[20] = W->info.getFrame(node);
}

void SetupStretch(int i) {
    SetupChain(i, 512);
    g_p.withUp = 1;
    g_p.sharedScratch = 1;
    W->blob[0] = 0x98;
    int half = Range(4, 120);
    W->blob[1] = (uint8_t)half;
    uint32_t total = Chance(5) ? 0 : (uint32_t)Range(100, 30000);
    W->blob[2] = (uint8_t)(total >> 24);
    W->blob[3] = (uint8_t)(total >> 16);
    W->blob[4] = (uint8_t)(total >> 8);
    W->blob[5] = (uint8_t)total;
    for (int k = 6; k < 4096; k++)   // offsets up to half a window: larger ones make the copies negative
        W->blob[k] = (uint8_t)Range(0, half);
    g_p.stretchVoice = Chance(70) ? -1 : Range(0, 31);
    g_p.ratio = Chance(80) ? Range(0x800, 0x2000) : Range(0, 0x3000);
    W->player.outstanding = Chance(80) ? 100000 : Range(0, 3000);
}

void RunStretch(bool original) {
    SFilterStretch *node = (SFilterStretch *)W->node;
    PICK(StretchInitFn, 0x001443f0, &SFILTER_timestretchinit)(node, W->blob, g_p.stretchVoice);
    PICK(StretchRatioFn, 0x00144300, &SFILTER_timestretchsetratio)(node, g_p.ratio);
    node->node.input = &W->up.node;
    RunBlocks(PICK(ProcessFn, 0x00144340, &SFILTER_timestretch), &node->node);
}

typedef void (*SetupFn)(int);
typedef void (*RunFn)(bool);

struct Kind {
    const char *name;
    const unsigned *originals;
    SetupFn setup;
    RunFn run;
    int cases;
    bool needsXa;
    int ran, differ, faults;
};

Kind g_kinds[] = {
    { "sine FUN_0014a2e0", kSine, SetupSine, RunSine, 200, false },
    { "FIR design FUN_00146930", kDesign, SetupDesign, RunDesign, 800, false },
    { "FIR FUN_001466e0", kFir, SetupFir, RunFir, 400, false },
    { "one-pole FUN_00145720", kLpfState, SetupLpfState, RunLpfState, 200, false },
    { "SFILTER_lpfRC", kLpfNode, SetupLpfNode, RunLpfNode, 500, false },
    { "SFILTER_hpfFIR8", kHpfNode, SetupHpfNode, RunHpfNode, 500, false },
    { "SFILTER_rsf", kRsfNode, SetupRsfNode, RunRsfNode, 900, false },
    { "rsf kernel FUN_001462b0", kKernel, SetupKernel, RunKernel, 600, false },
    { "SFILTER_ft24_32", kOutput, SetupOutput, RunOutput, 350, false },
    { "SOURCE", kSource, SetupSource, RunSource, 100, false },
    { "list and connect", kGraph, SetupGraph, RunGraph, 300, false },
    { "SFILTER_add", kAdd, SetupAdd, RunAdd, 150, false },
    { "SFILTER_unpackxapf", kXapf, SetupXapf, RunXapf, 250, true },
    { "SFILTER_unpackxaf", kXaf, SetupBankXa, RunXaf, 100, true },
    { "SFILTER_unpackxalf", kXalf, SetupBankXa, RunXalf, 150, true },
    { "SFILTER_unpackpf", kPf, SetupPf, RunPf, 100, false },
    { "SFILTER_unpacklf", kLf, SetupPcmBank, RunLf, 100, false },
    { "SFILTER_unpackf", kF, SetupPcmBank, RunF, 100, false },
    { "SFILTER_unpackmtpf", kMtpf, SetupMtpf, RunMtpf, 40, false },
    { "SFILTER_unpackmtf", kMtf, SetupMtf, RunMtf, 40, false },
    { "time stretch", kStretch, SetupStretch, RunStretch, 400, false },
};

// ---- running a side

unsigned g_x87, g_sse;

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

bool Guarded(RunFn run, bool original) {
#ifdef _MSC_VER
    __try {
        run(original);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        return false;
    }
#else
    run(original);
    return true;
#endif
}

int g_cases, g_checks, g_differ, g_details, g_nanOnly, g_faultsBoth;

bool IsNaNBits(uint32_t u) {
    return (u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) != 0;
}

const char *Region(size_t offset) {
    struct R {
        size_t at;
        const char *name;
    };
    static const R regions[] = {
        { offsetof(Workspace, scratch), "scratch" }, { offsetof(Workspace, out), "out" },
        { offsetof(Workspace, node), "node" }, { offsetof(Workspace, up), "upstream" },
        { offsetof(Workspace, player), "player" }, { offsetof(Workspace, graph), "graph" },
        { offsetof(Workspace, head), "head" }, { offsetof(Workspace, mix), "mix" },
        { offsetof(Workspace, info), "info" }, { offsetof(Workspace, desc), "desc" },
        { offsetof(Workspace, params), "params" }, { offsetof(Workspace, blob), "blob" },
        { offsetof(Workspace, src), "src" }, { offsetof(Workspace, pcm), "pcm" },
        { offsetof(Workspace, dres), "double results" }, { offsetof(Workspace, results), "results" },
        { offsetof(Workspace, arenaUsed), "arena used" }, { offsetof(Workspace, arena), "arena" } };
    const char *name = "?";
    for (const R &r : regions)
        if (offset >= r.at)
            name = r.name;
    return name;
}

void Detail(const char *format, ...) {
    if (g_details++ >= 10)
        return;
    char line[400];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    printf("[sndfiltershadow]   %s\n", line);
}

void RunCase(Kind &kind, int index, int kindIndex) {
    g_seed = 0x9e3779b9u ^ ((uint32_t)kindIndex * 0x01000193u) ^ ((uint32_t)index * 0x85ebca6bu);
    if (g_seed == 0)
        g_seed = 1;
    memset(W, 0, sizeof(Workspace));
    memset(&g_p, 0, sizeof(g_p));
    FillBuffers();
    kind.setup(index);
    memcpy(g_snapshot, W, sizeof(Workspace));
    memcpy(g_globalsSnapshot, (void *)(uintptr_t)kKernelGlobals, kKernelGlobalsSize);

    g_side = 0;
    g_hash[0] = 2166136261u;
    g_logCount[0] = 0;
    Originals(kind.originals, true);
    bool okOriginal = Guarded(kind.run, true);
    Originals(kind.originals, false);
    memcpy(g_resultOriginal, W, sizeof(Workspace));
    memcpy(g_globalsOriginal, (void *)(uintptr_t)kKernelGlobals, kKernelGlobalsSize);

    memcpy(W, g_snapshot, sizeof(Workspace));
    memcpy((void *)(uintptr_t)kKernelGlobals, g_globalsSnapshot, kKernelGlobalsSize);
    g_side = 1;
    g_hash[1] = 2166136261u;
    g_logCount[1] = 0;
    bool okOurs = Guarded(kind.run, false);

    g_cases++;
    kind.ran++;
    g_checks += 4;
    bool differ = false;
    if (okOriginal != okOurs) {
        differ = true;
        Detail("%s case %d: the original %s, ours %s", kind.name, index, okOriginal ? "ran" : "faulted",
               okOurs ? "ran" : "faulted");
    } else if (!okOriginal) {
        g_faultsBoth++;
        kind.faults++;
    }
    const uint32_t *a = (const uint32_t *)g_resultOriginal, *b = (const uint32_t *)W;
    size_t words = sizeof(Workspace) / 4, first = (size_t)-1;
    int differing = 0, nanWords = 0;
    for (size_t j = 0; j < words; j++) {
        if (a[j] == b[j])
            continue;
        if (IsNaNBits(a[j]) && IsNaNBits(b[j])) {
            nanWords++;
            continue;
        }
        if (first == (size_t)-1)
            first = j;
        differing++;
    }
    if (differing != 0) {
        differ = true;
        Detail("%s case %d: %d words differ, first %s +0x%x: original %08x, ours %08x", kind.name, index, differing,
               Region(first * 4), (unsigned)(first * 4), a[first], b[first]);
    }
    if (memcmp(g_globalsOriginal, (void *)(uintptr_t)kKernelGlobals, kKernelGlobalsSize) != 0) {
        differ = true;
        Detail("%s case %d: the kernel's scratch globals differ", kind.name, index);
    }
    if (g_hash[0] != g_hash[1] || g_logCount[0] != g_logCount[1]) {
        differ = true;
        Detail("%s case %d: the calls differ (%u calls, hash %08x / %u calls, hash %08x)", kind.name, index,
               g_logCount[0], g_hash[0], g_logCount[1], g_hash[1]);
    }
    if (differ) {
        g_differ++;
        kind.differ++;
    } else if (nanWords != 0) {
        g_nanOnly++;
    }
}

// ---- disc speech, decoded by the original EA-XA decoder

uint32_t BE32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

uint32_t LE32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// The first stream of the archive: its SCDl chunks' first channel (two history shorts, then the blocks)
void LoadSpeechPackets() {
    char path[MAX_PATH];
    if (!Xbox_ResolvePath("D:\\driving\\mis01en.spe", path, sizeof(path)))
        return;
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return;
    uint8_t header[32];
    if (fread(header, 1, sizeof(header), f) != sizeof(header) || memcmp(header, "BIGF", 4) != 0) {
        fclose(f);
        return;
    }
    uint32_t offset = BE32(header + 16), size = BE32(header + 20);
    if (size > (16u << 20))
        size = 16u << 20;
    g_file = (uint8_t *)malloc(size + 16);
    if (g_file == NULL || fseek(f, (long)offset, SEEK_SET) != 0 || fread(g_file, 1, size, f) != size) {
        fclose(f);
        free(g_file);
        g_file = NULL;
        return;
    }
    fclose(f);
    int channels = 1;
    uint32_t at = 0;
    while (at + 8 <= size && g_xaCount < kMaxPackets) {
        uint32_t length = LE32(g_file + at + 4);
        if (length < 8 || at + length > size)
            break;
        if (memcmp(g_file + at, "SCHl", 4) == 0) {
            for (uint32_t k = at + 8; k + 2 < at + length; k++)
                if (g_file[k] == 0x82 && g_file[k + 1] == 0x01) {
                    channels = g_file[k + 2] >= 1 && g_file[k + 2] <= 6 ? g_file[k + 2] : 1;
                    break;
                }
        } else if (memcmp(g_file + at, "SCDl", 4) == 0) {
            uint32_t frames = LE32(g_file + at + 8);
            uint32_t data = at + 12 + 4u * (uint32_t)channels + LE32(g_file + at + 12);
            if (frames > 0 && frames < 100000 && data + 4 + (frames + 27) / 28 * 15 <= at + length) {
                g_xa[g_xaCount].data = g_file + data;
                g_xa[g_xaCount].frames = (int)frames;
                g_xaCount++;
            }
        } else if (memcmp(g_file + at, "SCEl", 4) == 0) {
            break;
        }
        at += length;
    }
    g_xaFromDisc = g_xaCount > 0;
}

void DecodeSpeech() {
    alignas(16) static uint8_t decoder[0xc0];
    float *out = g_signal[SIG_SPEECH];
    int position = 0;
    XbeOriginal_RestoreRange(0x00149d80, 0x0014a1e0, true);
    ((void *(__fastcall *)(void *, int))0x00149e70u)(decoder, 0);
    for (int k = 0; k < g_xaCount && position < kSignalLength; k++) {
        const uint8_t *p = g_xa[k].data;
        int frames = g_xa[k].frames;
        if (frames > kSignalLength - position)
            frames = kSignalLength - position;
        float state[2] = { (float)(int16_t)(p[0] | (p[1] << 8)), (float)(int16_t)(p[2] | (p[3] << 8)) };
        ((void (__fastcall *)(void *, int, float *))0x0014a1c0u)(decoder, 0, state);
        ((int (__fastcall *)(void *, int, const void *, int, int))0x00149e90u)(decoder, 0, p + 4, frames * 4, frames);
        float *to = out + position;
        int got = ((int (__fastcall *)(void *, int, float **, int))0x00149ec0u)(decoder, 0, &to, frames);
        if (got <= 0)
            break;
        position += got;
        if (got < frames) {   // left in its partial-block buffer: drain it so the next Feed is accepted
            to = out + position;
            got = ((int (__fastcall *)(void *, int, float **, int))0x00149ec0u)(decoder, 0, &to, frames - got);
            position += got > 0 ? got : 0;
        }
    }
    XbeOriginal_RestoreRange(0x00149d80, 0x0014a1e0, false);
    g_speechFrames = position;
    for (int i = position; i < kSignalLength; i++)   // the rest repeats what was decoded
        out[i] = position > 0 ? out[i % position] : 0.0f;
}

bool DecodeSpeechGuarded() {
#ifdef _MSC_VER
    __try {
        DecodeSpeech();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        XbeOriginal_RestoreRange(0x00149d80, 0x0014a1e0, false);
        g_speechFrames = 0;
        return false;
    }
#else
    DecodeSpeech();
    return true;
#endif
}

// Random EA-XA when the disc is not there: any bytes are blocks
uint8_t g_syntheticXa[16][32768];

void MakeSignals() {
    for (int s = 0; s < SIG_COUNT; s++)
        g_signal[s] = (float *)VirtualAlloc(NULL, kSignalLength * sizeof(float), MEM_COMMIT | MEM_RESERVE,
                                            PAGE_READWRITE);
    g_seed = 0x2545f491u;
    double phase = 0.0;
    for (int i = 0; i < kSignalLength; i++) {
        g_signal[SIG_NOISE1][i] = Uniform(-1.0f, 1.0f);
        g_signal[SIG_NOISE16][i] = Uniform(-32768.0f, 32767.0f);
        g_signal[SIG_LOUD][i] = Uniform(-50000.0f, 50000.0f);
        double t = (double)i / kSignalLength;
        phase += 2.0 * 3.14159265358979 * (20.0 + 23980.0 * t) / 48000.0;   // 20 Hz .. 24 kHz
        g_signal[SIG_SWEEP][i] = (float)(30000.0 * sin(phase));
        int period = 8 + (i >> 12);
        g_signal[SIG_SQUARE][i] = (i / period) & 1 ? 32767.0f : -32768.0f;
        g_signal[SIG_SQUARE_OVER][i] = (i / period) & 1 ? 40000.0f : -40000.0f;
        g_signal[SIG_HALVES][i] = (float)Range(-40000, 40000) + 0.5f;
        g_signal[SIG_SPEECH][i] = 0.0f;
    }
    // PCM16 packets
    static int16_t pcm[16][2048];
    for (int k = 0; k < 16; k++) {
        int frames = Range(100, 2000);
        for (int j = 0; j < 2048; j++)
            pcm[k][j] = (int16_t)Range(-32768, 32767);
        g_pcm[k].data = (const uint8_t *)pcm[k];
        g_pcm[k].frames = frames;
    }
    // MicroTalk-ish packets: random bytes, the first byte picking the raw (0) or the header path
    static uint8_t mt[16][16384];
    for (int k = 0; k < 16; k++) {
        for (int j = 0; j < 16384; j++)
            mt[k][j] = (uint8_t)Next();
        mt[k][0] = k % 2 ? 0 : 1;
        g_mt[k].data = mt[k];
        g_mt[k].frames = Range(100, 1500);
    }
}

}   // namespace

void SndFilterShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_SNDFILTERSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    W = (Workspace *)VirtualAlloc(NULL, sizeof(Workspace), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_snapshot = (Workspace *)VirtualAlloc(NULL, sizeof(Workspace), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_resultOriginal = (Workspace *)VirtualAlloc(NULL, sizeof(Workspace), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (W == NULL || g_snapshot == NULL || g_resultOriginal == NULL) {
        printf("[sndfiltershadow] no memory\n");
        fflush(stdout);
        return;
    }
    MakeSignals();
    LoadSpeechPackets();
    if (g_xaCount > 0)
        DecodeSpeechGuarded();
    if (g_xaCount == 0) {
        for (int k = 0; k < 16; k++) {
            for (int j = 0; j < 32768; j++)
                g_syntheticXa[k][j] = (uint8_t)Next();
            g_xa[k].data = g_syntheticXa[k];
            g_xa[k].frames = Range(500, 32768 / 15 * 28 - 100);
        }
        g_xaCount = 16;
    }

    int replaced = 0, total = 0;
    for (const unsigned *a = kAllOurs; *a != 0; a++) {
        total++;
        if (XbeOriginal_Restore(*a, true)) {
            XbeOriginal_Restore(*a, false);
            replaced++;
        }
    }

    // the fakes, and the globals the run borrows
    uint32_t savedNew = *(uint32_t *)0x002475f4u, savedDelete = *(uint32_t *)0x002475f8u;
    uint8_t *savedMixList = *(uint8_t **)0x00245be4u;
    uint8_t savedKernelGlobals[kKernelGlobalsSize];
    memcpy(savedKernelGlobals, (void *)(uintptr_t)kKernelGlobals, kKernelGlobalsSize);
    *(uint32_t *)0x002475f4u = P((void *)&FakeCodaNew);
    *(uint32_t *)0x002475f8u = P((void *)&FakeCodaDelete);
    HookInstall(0x0013ee00, (void *)&FakeGetPacket);           // SNDPKTPLAYI_get
    HookInstall(0x0013ef80, (void *)&FakeFreeFrames);          // SNDPKTPLAYI_freeframes
    HookInstall(0x00142420, (void *)&FakeGetMasterVoice);      // SNDDRV_getmastervoice
    HookInstall(0x001457e0, (void *)&FakeVoiceToPacketHandle); // SNDPKTPLAYI_voicetopackethandle
    HookInstall(0x00142460, (void *)&FakeGetSampleChan);       // SNDDRV_getsamplechan
    HookInstall(0x0013f780, (void *)&FakeSndMemAlloc);         // SNDMEMI_alloc
    HookInstall(0x0013edc0, (void *)&FakeFramesOutstanding);   // SNDPKTPLAY_framesoutstanding

    int kindCount = (int)(sizeof(g_kinds) / sizeof(g_kinds[0]));
    for (int k = 0; k < kindCount; k++) {
        Kind &kind = g_kinds[k];
        for (int i = 0; i < kind.cases; i++)
            RunCase(kind, i, k);
    }

    HooksRemove();
    *(uint32_t *)0x002475f4u = savedNew;
    *(uint32_t *)0x002475f8u = savedDelete;
    *(uint8_t **)0x00245be4u = savedMixList;
    memcpy((void *)(uintptr_t)kKernelGlobals, savedKernelGlobals, kKernelGlobalsSize);
    VirtualFree(W, 0, MEM_RELEASE);
    VirtualFree(g_snapshot, 0, MEM_RELEASE);
    VirtualFree(g_resultOriginal, 0, MEM_RELEASE);
    for (int s = 0; s < SIG_COUNT; s++)
        VirtualFree(g_signal[s], 0, MEM_RELEASE);
    free(g_file);
    g_file = NULL;
    W = NULL;

    for (int k = 0; k < kindCount; k++)
        if (g_kinds[k].differ != 0)
            Detail("%s: %d of %d cases differ", g_kinds[k].name, g_kinds[k].differ, g_kinds[k].ran);
    printf("[sndfiltershadow] SFILTER graph: %d cases, %d checks, %d differ (%d faulted on both sides, %d NaN-only; "
           "speech %d frames from %d %s packets; %d of %d entry points replaced; x87 control word %04x)\n",
           g_cases, g_checks, g_differ, g_faultsBoth, g_nanOnly, g_speechFrames, g_xaCount,
           g_xaFromDisc ? "disc" : "synthetic", replaced, total, g_x87 & 0xffffu);
    fflush(stdout);
}
