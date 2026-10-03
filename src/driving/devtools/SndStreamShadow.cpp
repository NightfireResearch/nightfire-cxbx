#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // as the build defines it (CMakeLists.txt)
#endif

#include "SndStreamShadow.h"
#include "FpControl.h"

#include "../sound/snd/Streams.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xboxPath.h"

#include <windows.h>
#include <float.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_SNDSTREAMSHADOW=1 (2: about four times the cases), at injection time on the loader's thread before the
// game runs: EA's stream code on the SND side (sound/snd/Streams.cpp, docs/driving/sound.md 4.4) against the
// originals, as sound.md 9.3 step 6 asks, as far as it can run without the game.
//
// Every case runs in one workspace (stream records, request records, packet players, a stand-in voice array, an
// arena standing in for the sound heap, the fake STREAMs and the chunk memory they hand out) plus the library's
// globals the code touches (0x00244ba8..0x00244fc0: sndss, NUM_STREAMS, the voice array pointer, the on-exit
// function; 0x00244fe0..0x00245330: the packet callback queue and sndpps). The case is set up, snapshotted, run
// with the 43 entry points swapped back to the originals (common/xbeOriginal.h), saved, put back to the
// snapshot, run with ours, and the two compared word by word, together with every call the fakes saw (arguments
// and order, hashed, the first 8192 kept for the report) and whether a side faulted (SEH: counted, not fatal). The
// cases always call the entry points by their original addresses, so the original side runs the originals
// throughout and ours runs ours (internal calls included).
//
// Everything the stream code calls outside module D is replaced for the run by five-byte jumps to recording
// fakes (put back after): the critical section, SNDMEMI_alloc/free, the server client list, the twelve STREAM
// entries (a synchronous fake STREAM: queuefile copies a disc stream's chunks into the chunk memory and STREAM_get
// hands them out, so many per tick; gettable/state answer scripted values), SNDPLATFORM_getvoicerange/packetplay,
// and the voice manager functions (SNDVOICEI_alloc/free/get, iSNDcalcpitch/calcvol, SNDI_calcfxlevel,
// SNDI_validrendermode, SNDstop, SNDvol, SND3dpos, SNDpitchmult, SNDautovol, SNDCTRL_filteradd, SNDCTRL_lowpass) -
// those are hooked both at the original's address and at our port's entry, since our stream code calls the ports
// directly. Scripted answers are a function of the case and of how many times that fake was called, so both
// sides see the same answers as long as they make the same calls. SNDLINKI_*, iSNDmulu64/divu64, memclr,
// dummyGetNullValue and SNDI_patchtohdr (Banks.cpp's port, the same code on both sides) run for real.
//
// The cases:
// - header: every SCHl chunk of every .mus and .spe (842 streams, all languages) through SNDSTRMI_parseheader, on
//   a stream that is idle, playing the same header, playing another format, or waiting to restart, with zero to
//   three requests ahead; plus a perturbed copy of each (channels 1..6, rate, sample representation, frames,
//   inserted 0x98 blob, 0x9c azimuth and 0x14 user-data tags) for the restart, blob and user-data paths.
// - data: a sample of the SCDl chunks (every 64th, every 16th at level 2) through SNDSTRMI_parsedata into a packet
//   player at a random fill level (full rings included), some with zero frames or bit 31 set.
// - api: random request records and players for SNDSTRM_requeststatus (the 64-bit sums) and status, getrequestptr,
//   getstreamptr, isheld, voicetopackethandle, submitspace, framesoutstanding, overhead/overheadtap, modifyhold,
//   vol/pitchmult/lowpass/3dpos/autovol/setgreedylevel, on valid and invalid handles.
// - packet: random sequences of SNDPKTPLAY_submit/start/stop, SNDPKTPLAYI_get/freeframes/flushcallbackdata on a
//   small ring (2..8 slots, 1..6 channels) with fake callbacks.
// - service: one to three streams in arbitrary states (idle, playing, waiting to restart, held) whose STREAM holds a
//   run of chunks cut from a disc stream at a random chunk (data before any header only on a stream that has a
//   format: see SetupService), serviced, mixed and
//   flushed for one to six ticks.
// - scenario: one to three streams made with SNDSTRM_create (or createtap), disc streams of up to 256 KB queued on
//   them over 150..500 ticks of SNDSTRMI_service, a mixer stand-in taking packets on every channel with
//   SNDPKTPLAYI_get and reporting them with freeframes (occasionally in two parts), flushcallbackdata each tick
//   (so the frames and release callbacks run, requests finish and headers restart players), and status,
//   requeststatus, modifyhold, holds, purges, the setters and destroy/destroyall along the way.
// - calcdatarate: every sample representation 0..255 against rates and channel counts.
//
// Not covered: the real STREAM, FILESYS and their threads (the fakes are synchronous), the real voice manager
// and platform driver underneath SNDPKTPLAY_start (their calls are compared, not their effects), and the two
// threads touching a stream in game (main-thread service vs the SND thread's get/freeframes/flush) - here they
// interleave in one fixed order per tick.
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types

using namespace SND;

// ---- the entry points under test (all swapped back for the original side)

const unsigned kOurs[] = {
    0x0013b990, 0x0013b9e0, 0x0013ba30, 0x0013baa0, 0x0013bac0, 0x0013bb20, 0x0013bb50, 0x0013bc20, 0x0013bdb0,
    0x0013be80, 0x0013bf10, 0x0013c060, 0x0013c160, 0x0013c180, 0x0013c280, 0x0013c320, 0x0013c350, 0x0013c560,
    0x0013c590, 0x0013c610, 0x0013c630, 0x0013c660, 0x0013c740, 0x0013c800, 0x0013c840, 0x0013c880, 0x0013c8c0,
    0x0013f180, 0x0013f9b0, 0x00150360, 0x00150380, 0x0013e8c0, 0x0013e8e0, 0x0013e980, 0x0013ecc0, 0x0013eda0,
    0x0013edc0, 0x0013ede0, 0x0013ee00, 0x0013ef80, 0x0013efd0, 0x0013f040, 0x001457e0, 0 };

void Originals(bool original) {
    for (const unsigned *a = kOurs; *a != 0; a++)
        XbeOriginal_Restore(*a, original);
}

// ---- the entry points, always called at the original's address

inline int Overhead(int r, int p) { return ((int (*)(int, int))0x0013c630u)(r, p); }
inline int OverheadTap(int r, int p) { return ((int (*)(int, int))0x0013c610u)(r, p); }
inline int Create(PlayOpts *o, int r, int p, void *m, int s) {
    return ((int (*)(PlayOpts *, int, int, void *, int))0x0013c560u)(o, r, p, m, s);
}
inline int CreateTap(void *st, PlayOpts *o, int r, int p, void *m, int s) {
    return ((int (*)(void *, PlayOpts *, int, int, void *, int))0x00150380u)(st, o, r, p, m, s);
}
inline int QueueFile(int s, int h, const char *n, uint32_t o) {
    return ((int (*)(int, int, const char *, uint32_t))0x0013c160u)(s, h, n, o);
}
inline int QueueRequestId(int s, int h, uint32_t r) { return ((int (*)(int, int, uint32_t))0x00150360u)(s, h, r); }
inline int Queue(int s, int h, const void *src, uint32_t a, int t) {
    return ((int (*)(int, int, const void *, uint32_t, int))0x0013c060u)(s, h, src, a, t);
}
inline void Service() { ((void (*)(void))0x0013bf10u)(); }
inline int Purge(int s) { return ((int (*)(int))0x0013c180u)(s); }
inline int Destroy(int s) { return ((int (*)(int))0x0013c280u)(s); }
inline int DestroyAll() { return ((int (*)(void))0x0013c320u)(); }
inline int ModifyHold(int id, int h) { return ((int (*)(int, int))0x0013c590u)(id, h); }
inline int RequestStatus_(int id, RequestStatus *s) { return ((int (*)(int, RequestStatus *))0x0013c660u)(id, s); }
inline int Status(int s, StreamStatus *st) { return ((int (*)(int, StreamStatus *))0x0013c740u)(s, st); }
inline int Vol(int s, int v) { return ((int (*)(int, int))0x0013c8c0u)(s, v); }
inline int Pitch(int s, int v) { return ((int (*)(int, int))0x0013c880u)(s, v); }
inline int Lowpass(int s, int v) { return ((int (*)(int, int))0x0013c840u)(s, v); }
inline int Pos3d(int s, int a, int e) { return ((int (*)(int, int, int))0x0013c800u)(s, a, e); }
inline int AutoVol(int s, int t, int v) { return ((int (*)(int, int, int))0x0013b990u)(s, t, v); }
inline int Greedy(int s, int l) { return ((int (*)(int, int))0x0013f9b0u)(s, l); }
inline int CalcDataRate(StreamFormat *f, bool original) {
    if (original)
        return ((int (*)(StreamFormat *))0x0013ba30u)(f);
    return SNDSTRMI_calcdatarate(f);
}
inline StreamState *GetStreamPtr(int s) { return ((StreamState * (*)(int))0x0013baa0u)(s); }
inline StreamRequest *GetRequestPtr(int id) { return ((StreamRequest * (*)(int))0x0013f180u)(id); }
inline int IsHeld(StreamState *ss) { return ((int (*)(StreamState *))0x0013be80u)(ss); }
inline int ParseHeader(int s, uint32_t *c) { return ((int (*)(int, uint32_t *))0x0013bc20u)(s, c); }
inline int ParseData(StreamState *ss, uint32_t *c) { return ((int (*)(StreamState *, uint32_t *))0x0013bdb0u)(ss, c); }
inline uint32_t Submit(int p, Packet *k) { return ((uint32_t (*)(int, Packet *))0x0013ecc0u)(p, k); }
inline int SubmitSpace(int p) { return ((int (*)(int))0x0013eda0u)(p); }
inline int FramesOutstanding(int p) { return ((int (*)(int))0x0013edc0u)(p); }
inline int Start(int p, StreamFormat *f, Attributes *a, PlayOpts *o) {
    return ((int (*)(int, StreamFormat *, Attributes *, PlayOpts *))0x0013e980u)(p, f, a, o);
}
inline int Stop(int p) { return ((int (*)(int))0x0013f040u)(p); }
inline uint8_t *Get(int p, int c, int *f, int *k) { return ((uint8_t * (*)(int, int, int *, int *))0x0013ee00u)(p, c, f, k); }
inline void FreeFrames(int p, int c, int f) { ((void (*)(int, int, int))0x0013ef80u)(p, c, f); }
inline void Flush() { ((void (*)(void))0x0013efd0u)(); }
inline int VoiceToPacket(int v) { return ((int (*)(int))0x001457e0u)(v); }

// ---- random numbers (the cases' own)

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

bool Chance(int percent) {
    return (int)(Next() % 100) < percent;
}

uint32_t Mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// ---- the workspace

const int kStreams = 3;
const int kStreamBytes = 0x1000;
const int kMaxPending = 4096;
const int kChunkBytes = 2 << 20;
const int kSmallChunkBytes = 0x2000;   // what the header, data, api and packet cases use of the chunk memory
const uint32_t kStrmMagic = 0x4d525453u;

struct FakeStream {
    uint32_t magic;
    int32_t head, tail;
    uint32_t pending[kMaxPending];   // chunk offsets in Workspace::chunks
};

struct Workspace {
    uint32_t caseSeed;
    uint32_t ordinal[32];            // per fake: calls so far (the scripted answers' input)
    int32_t budget;                  // STREAM_get answers left this tick
    int32_t nextFile;                // the disc stream the next STREAM_queuefile/queuemem delivers
    int32_t streamsCreated;
    uint32_t handleSerial;
    uint32_t arenaUsed;
    uint32_t chunkUsed;
    int32_t streamIndex;             // the header and data cases' arguments
    uint32_t *chunk;
    StreamState *ss;
    int32_t player;
    RequestStatus rs;
    StreamStatus st;
    Packet packet;
    StreamFormat format;
    Attributes attributes;
    PlayOpts opts[kStreams + 1];
    char name[32];
    FakeStream fake[kStreams + 1];   // the last one: a tap's STREAM
    alignas(16) uint8_t streamMemory[kStreams][kStreamBytes];
    alignas(16) uint8_t voices[224 * 0x88];
    alignas(16) uint8_t arena[0x4000];
    alignas(16) uint8_t chunks[kChunkBytes];
};

Workspace *W, *g_snapshot, *g_resultOriginal;
size_t g_extent;   // bytes of the workspace this case uses (snapshotted and compared)

// ---- the globals the code touches

const uint32_t kG1 = 0x00244ba8u, kG1Size = 0x00244fc0u - 0x00244ba8u;
const uint32_t kG2 = 0x00244fe0u, kG2Size = 0x00245330u - 0x00244fe0u;
uint8_t g_savedG1[kG1Size], g_savedG2[kG2Size];
uint8_t g_snapG1[kG1Size], g_snapG2[kG2Size], g_resG1[kG1Size], g_resG2[kG2Size];

inline PacketPlayer *&PlayerAt(int i) { return ((PacketPlayer **)(uintptr_t)0x002452e4u)[i]; }
inline StreamState *&StreamAt(int i) { return ((StreamState **)(uintptr_t)0x00244ba8u)[i]; }

void GlobalsBase() {
    memcpy((void *)(uintptr_t)kG1, g_savedG1, kG1Size);
    memcpy((void *)(uintptr_t)kG2, g_savedG2, kG2Size);
    memset((void *)(uintptr_t)0x00244ba8u, 0, 32 * 4);            // sndss
    *(uint8_t *)(uintptr_t)0x00244d0fu = 16;                      // NUM_STREAMS
    *(int8_t *)(uintptr_t)0x00244ed6u = 0;                        // no user-data clients
    *(uint32_t *)(uintptr_t)0x00244f38u = 0;                      // SNDSTRM_on_exit_func
    *(uint8_t **)(uintptr_t)0x00244f3cu = W->voices;              // the voice array
    memset((void *)(uintptr_t)0x00244fe0u, 0, 0x00245324u - 0x00244fe0u);   // callback queue, sndpps
}

// ---- the call log (per side)

const int kKeep = 8192;
struct Entry {
    uint32_t w[6];
};
int g_side = -1;   // -1: setup (not logged)
uint32_t g_hash[2], g_logCount[2];
Entry *g_log[2];

// What the scenarios did (counted on our side): packets taken, chunks released, voices started, requests finished
bool g_counting;
uint32_t g_taken, g_released, g_starts, g_finished, g_stops;

void Log(uint32_t tag, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0, uint32_t e = 0) {
    if (g_side < 0)
        return;
    if (g_counting && g_side == 1) {
        g_taken += tag == 0x400 && c != 0;
        g_released += tag == 0x19;
        g_starts += tag == 0x21;
        g_finished += tag == 0x505 && b == 3;
        g_stops += tag == 0x30;
    }
    uint32_t w[6] = { tag, a, b, c, d, e };
    uint32_t h = g_hash[g_side];
    for (int i = 0; i < 6; i++)
        for (int k = 0; k < 4; k++) {
            h ^= (w[i] >> (8 * k)) & 0xff;
            h *= 16777619u;
        }
    g_hash[g_side] = h;
    if (g_logCount[g_side] < (uint32_t)kKeep)
        memcpy(g_log[g_side][g_logCount[g_side]].w, w, sizeof(w));
    g_logCount[g_side]++;
}

uint32_t P(const void *p) {
    return (uint32_t)(uintptr_t)p;
}

// A scripted answer: a function of the case and of how many times this fake was called
uint32_t Script(int kind) {
    return Mix(W->caseSeed ^ ((uint32_t)kind * 0x9e3779b9u) ^ (W->ordinal[kind]++ * 0x85ebca6bu));
}

// ---- the fakes

enum {
    S_MEMALLOC, S_STREAMCREATE, S_GETTABLE, S_STATE, S_ALLOC, S_PACKETPLAY, S_VOICEGET, S_COUNT
};

void FakeEnter() { Log(0x01); }
void FakeLeave() { Log(0x02); }

void *FakeMemAlloc(int32_t size) {
    Log(0x03, (uint32_t)size);
    uint32_t s = ((uint32_t)size + 15u) & ~15u;
    if (size <= 0 || W->arenaUsed + s > sizeof(W->arena))
        return NULL;
    void *p = W->arena + W->arenaUsed;
    W->arenaUsed += s;
    return p;
}
void FakeMemFree(void *p) { Log(0x04, P(p)); }
void FakeAddClient(uint32_t fn) { Log(0x05, fn); }
void FakeRemoveClient(uint32_t fn) { Log(0x06, fn); }

FakeStream *Fake(void *s) {
    for (int i = 0; i <= kStreams; i++)
        if (s == &W->fake[i] && W->fake[i].magic == kStrmMagic)
            return &W->fake[i];
    return NULL;
}

void *FakeStreamCreate(int requests, int a, int b, void *memory, int size) {
    Log(0x10, (uint32_t)requests, (uint32_t)a, (uint32_t)b, P(memory), (uint32_t)size);
    if (Script(S_STREAMCREATE) % 12 == 0 || W->streamsCreated >= kStreams)
        return NULL;
    FakeStream *f = &W->fake[W->streamsCreated++];
    f->magic = kStrmMagic;
    f->head = f->tail = 0;
    return f;
}
int FakeStreamOverhead(int requests, int a, int b) {
    Log(0x11, (uint32_t)requests, (uint32_t)a, (uint32_t)b);
    return 0x40 + requests * 0x40;
}

struct DiscStream {
    uint8_t *data;
    uint32_t size;
    int channels;
};
const int kMaxPlay = 48;
DiscStream g_play[kMaxPlay];
int g_playCount;

uint32_t FakeQueue(void *s) {
    FakeStream *f = Fake(s);
    int k = W->nextFile;
    if (f == NULL || k < 0 || k >= g_playCount)
        return 0;
    const DiscStream &d = g_play[k];
    if (W->chunkUsed + d.size > (uint32_t)kChunkBytes)
        return 0;
    uint32_t base = W->chunkUsed;
    memcpy(W->chunks + base, d.data, d.size);
    W->chunkUsed = (base + d.size + 15u) & ~15u;
    for (uint32_t at = 0; at + 8 <= d.size && f->tail < kMaxPending;) {
        uint32_t length = *(uint32_t *)(d.data + at + 4);
        f->pending[f->tail++] = base + at;
        at += length;
    }
    return 0x5000u + ++W->handleSerial;
}
uint32_t FakeQueueFile(void *s, const void *name, uint32_t offset, uint32_t tag) {
    Log(0x12, P(s), P(name), offset, tag);
    return FakeQueue(s);
}
uint32_t FakeQueueMem(void *s, const void *memory, uint32_t a3, uint32_t tag) {
    Log(0x13, P(s), P(memory), a3, tag);
    return FakeQueue(s);
}
uint32_t *FakeGet(void *s) {
    Log(0x14, P(s));
    FakeStream *f = Fake(s);
    if (f == NULL || W->budget <= 0 || f->head == f->tail)
        return NULL;
    W->budget--;
    return (uint32_t *)(W->chunks + f->pending[f->head++]);
}
uint32_t FakeGetTable(void *s) {
    Log(0x15, P(s));
    uint32_t x = Script(S_GETTABLE);
    switch (x % 8) {
    case 0:
        return 0;
    case 1:
        return 4000000u + (x >> 8) % 3000000u;   // past the 4,000,000 cap
    case 2:
        return x;
    default:
        return (x >> 4) % 400000u;
    }
}
int FakeState(void *s) {
    Log(0x16, P(s));
    return (int)(Script(S_STATE) % 4);
}
int FakeBufferSize(void *s) {
    Log(0x17, P(s));
    return 0x7ffe;
}
int FakeSetGreedy(void *s, int level) {
    Log(0x18, P(s), (uint32_t)level);
    return 0;
}
int FakeRelease(void *s, void *chunk) {
    Log(0x19, P(s), P(chunk));
    return 0x77;
}
void FakeKill(void *s) {
    Log(0x1a, P(s));
    FakeStream *f = Fake(s);
    if (f != NULL)
        f->head = f->tail = 0;
}
void FakeDestroy(void *s) { Log(0x1b, P(s)); }

void FakeGetVoiceRange(int mode, int *first, int *end) {
    Log(0x20, (uint32_t)mode);
    *first = mode == 0x24 ? 192 : 0;
    *end = mode == 0x24 ? 224 : 192;
}
int FakePacketPlay(int player, int voice, int a, int b, int c, int d, void *format, void *blobs) {
    Log(0x21, (uint32_t)player, (uint32_t)voice, (uint32_t)a, (uint32_t)b, (uint32_t)c);
    Log(0x22, (uint32_t)d, P(format), P(blobs));
    return Script(S_PACKETPLAY) % 5 == 0 ? -3 : 0;
}
int FakeStop(int h) { Log(0x30, (uint32_t)h); return 0; }
int FakeVol(int h, int v) { Log(0x31, (uint32_t)h, (uint32_t)v); return 0; }
int Fake3dpos(int h, int a, int e) { Log(0x32, (uint32_t)h, (uint32_t)a, (uint32_t)e); return 0; }
int FakePitch(int h, int m) { Log(0x33, (uint32_t)h, (uint32_t)m); return 0; }
int FakeAutovol(int h, int t, int v) { Log(0x34, (uint32_t)h, (uint32_t)t, (uint32_t)v); return 0; }
int FakeFilterAdd(int h, int f) { Log(0x35, (uint32_t)h, (uint32_t)f); return 0; }
int FakeLowpass(int h, int c) { Log(0x36, (uint32_t)h, (uint32_t)c); return 0; }
int FakeAlloc(int count, int priority, int *handle, int first, int end) {
    Log(0x37, (uint32_t)count, (uint32_t)priority, (uint32_t)first, (uint32_t)end);
    uint32_t x = Script(S_ALLOC);
    if (x % 6 == 0)
        return -1;
    int voice = first + (int)((x >> 4) % 8);
    Voice *v = (Voice *)(W->voices + voice * 0x88);
    for (int i = 0; i < count && i < 6; i++)   // the platform voices in a shuffled order: the master varies
        v->platformVoices[i] = (int16_t)(voice + (int)((x >> (8 + 3 * i)) % 6));
    *handle = (int)(x & 0x7f00u) | voice;
    return voice;
}
void FakeFree(int v) { Log(0x38, (uint32_t)v); }
int FakeVoiceGet(int h) {
    Log(0x39, (uint32_t)h);
    return Script(S_VOICEGET) % 4 == 0 ? -1 : (h & 0xff);
}
void FakeCalcPitch(int v) { Log(0x3a, (uint32_t)v); }
void FakeCalcVol(int v) { Log(0x3b, (uint32_t)v); }
void FakeCalcFx(int bus, int v) { Log(0x3c, (uint32_t)bus, (uint32_t)v); }
int FakeValidMode(int *index, PatchHeader *h) {
    Log(0x3d, (uint32_t)*index, (uint32_t)(uint8_t)h->channels, h->renderMode);
    static const int modes[2] = { 0x420, 0x24 };
    if (*index < 0 || *index >= 2)
        return 0;
    return modes[(*index)++];
}

// the packet case's callbacks
int FakeReleaseCallback(uint8_t *data, void *context) {
    Log(0x40, P(data), P(context));
    return 0;
}
void FakeFramesCallback(int player, uint32_t frames, void *context) {
    Log(0x41, (uint32_t)player, frames, P(context));
}

// ---- five-byte jumps over the functions the fakes stand in for

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[64];
int g_hookCount;

void HookInstall(uint32_t at, const void *to) {
    if (g_hookCount >= 64)
        return;
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

// A ported callee: its original's address and our port's entry
void HookBoth(uint32_t at, const void *ours, const void *to) {
    HookInstall(at, to);
    if ((uint32_t)(uintptr_t)ours != at)
        HookInstall((uint32_t)(uintptr_t)ours, to);
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

void InstallFakes() {
    HookInstall(0x0013b950u, (void *)&FakeEnter);             // SNDSYS_entercritical
    HookInstall(0x0013b970u, (void *)&FakeLeave);             // SNDSYS_leavecritical
    HookInstall(0x0013f780u, (void *)&FakeMemAlloc);          // SNDMEMI_alloc
    HookInstall(0x0013f880u, (void *)&FakeMemFree);           // SNDMEMI_free
    HookInstall(0x0013f900u, (void *)&FakeAddClient);         // iSNDserveraddclient
    HookInstall(0x0013f920u, (void *)&FakeRemoveClient);      // iSNDserverremoveclient
    HookInstall(0x0014b0c0u, (void *)&FakeStreamCreate);      // STREAM_create
    HookInstall(0x0014b090u, (void *)&FakeStreamOverhead);    // STREAM_overhead
    HookInstall(0x0014b3b0u, (void *)&FakeQueueFile);         // STREAM_queuefile
    HookInstall(0x0014b470u, (void *)&FakeQueueMem);          // STREAM_queuemem
    HookInstall(0x0014b520u, (void *)&FakeGet);               // STREAM_get
    HookInstall(0x0014b5d0u, (void *)&FakeGetTable);          // STREAM_gettable
    HookInstall(0x0014b5f0u, (void *)&FakeState);             // STREAM_state
    HookInstall(0x0014b640u, (void *)&FakeBufferSize);        // STREAM_buffersize
    HookInstall(0x0014b9a0u, (void *)&FakeSetGreedy);         // STREAM_setgreedylevel
    HookInstall(0x0014b9f0u, (void *)&FakeRelease);           // STREAM_release
    HookInstall(0x0014bcb0u, (void *)&FakeKill);              // STREAM_kill
    HookInstall(0x0014be60u, (void *)&FakeDestroy);           // STREAM_destroy
    HookInstall(0x0013d900u, (void *)&FakeGetVoiceRange);     // SNDPLATFORM_getvoicerange
    HookInstall(0x001424c0u, (void *)&FakePacketPlay);        // SNDPLATFORM_packetplay
    HookBoth(0x0013c900u, (void *)&SNDstop, (void *)&FakeStop);
    HookBoth(0x0013cb40u, (void *)&SNDvol, (void *)&FakeVol);
    HookBoth(0x0013c9f0u, (void *)&SND3dpos, (void *)&Fake3dpos);
    HookBoth(0x0013caa0u, (void *)&SNDpitchmult, (void *)&FakePitch);
    HookBoth(0x0013e7d0u, (void *)&SNDautovol, (void *)&FakeAutovol);
    HookBoth(0x0013e860u, (void *)&SNDCTRL_filteradd, (void *)&FakeFilterAdd);
    HookBoth(0x0013fa80u, (void *)&SNDCTRL_lowpass, (void *)&FakeLowpass);
    HookBoth(0x0013fb70u, (void *)&SNDVOICEI_alloc, (void *)&FakeAlloc);
    HookBoth(0x0013fef0u, (void *)&SNDVOICEI_free, (void *)&FakeFree);
    HookBoth(0x00140020u, (void *)&SNDVOICEI_get, (void *)&FakeVoiceGet);
    HookBoth(0x0013e700u, (void *)&iSNDcalcpitch, (void *)&FakeCalcPitch);
    HookBoth(0x0013e5d0u, (void *)&iSNDcalcvol, (void *)&FakeCalcVol);
    HookBoth(0x001401b0u, (void *)&SNDI_calcfxlevel, (void *)&FakeCalcFx);
    HookBoth(0x00142830u, (void *)&SNDI_validrendermode, (void *)&FakeValidMode);
}

// ---- building state for a case (setup: our own code, not the library's)

void ListPushTail(LinkList *list, StreamRequest *r) {
    r->next = NULL;
    r->prev = (StreamRequest *)list->tail;
    if (list->tail != NULL)
        ((StreamRequest *)list->tail)->next = r;
    else
        list->head = (LinkNode *)r;
    list->tail = (LinkNode *)r;
    list->count++;
}

void RandomBytes(void *p, size_t n) {
    for (size_t i = 0; i < n; i++)
        ((uint8_t *)p)[i] = (uint8_t)Next();
}

void DefaultOpts(PlayOpts *o) {
    memset(o, 0, sizeof(*o));
    o->vol = 0x7f;
    o->bend = 0x40;
    o->key = 0x3c;
    o->velocity = 0x7f;
    o->progVol = 0x7f;
    o->fxLevel = 0x7f;
    o->pitchMult = 0x1000;
    o->timeMult = 0x1000;
    o->tempoMult = 0x1000;
    o->lowpass = 0xffff;
}

void RandomOpts(PlayOpts *o) {
    DefaultOpts(o);
    if (Chance(50)) {
        o->vol = (int8_t)Range(-128, 127);
        o->bend = (int8_t)Range(0, 127);
        o->progVol = (uint8_t)Range(0, 255);
        o->fxLevel = (int8_t)Range(0, 127);
        o->azimuth = (uint16_t)Next();
        o->pitchMult = (uint16_t)Range(0, 0x3000);
        o->timeMult = (uint16_t)Next();
        o->distort = (uint16_t)Next();
        o->lowpass = (uint16_t)Next();
        o->highpass = (uint16_t)Next();
    }
}

// A stream record in streamMemory[slot] with its requests and packet player, registered as sndss[index]
StreamState *MakeStream(int slot, int index, int player, int requests, int active, int packets, int channels) {
    uint8_t *m = W->streamMemory[slot];
    StreamState *ss = (StreamState *)m;
    StreamRequest *req = (StreamRequest *)(m + 0x138);
    PacketPlayer *p = (PacketPlayer *)(m + 0x138 + requests * 0x28);
    memset(m, 0, (size_t)(0x138 + requests * 0x28 + 0x5c + packets * 0x20));
    W->fake[slot].magic = kStrmMagic;
    ss->stream = &W->fake[slot];
    ss->voice = Chance(70) ? (int)(Next() & 0x7fff) : -1;
    ss->player = player;
    ss->generation = (int32_t)((Next() & 0x7fff) << 8);
    RandomOpts(&ss->opts);
    ss->hasFilter = Chance(20) ? 1 : 0;
    RandomBytes(ss->filter, sizeof(ss->filter));
    for (int i = 0; i < requests; i++) {
        StreamRequest *r = &req[i];
        if (i < active) {
            ListPushTail(&ss->active, r);
            r->streamRequest = 0x5000u + (uint32_t)i;
            r->id = (int32_t)(((uint32_t)(ss->generation + 0x100 * (i + 1)) & 0x7fffff00u) | (uint32_t)index);
            r->rate = Chance(80) ? (uint32_t)Range(1, 200000) : 0;
            r->total = (uint32_t)Range(0, 2000000);
            r->played = Chance(50) ? (uint32_t)Range(0, (int)r->total) : Next();
            r->outstanding = Chance(70) ? (uint32_t)Range(0, 50000) : Next();
            r->hold = Chance(50) ? 0 : (Chance(80) ? Range(1, 5000) : Range(-3, -1));
            r->started = (uint8_t)(Chance(70) ? 1 : 0);
        } else {
            ListPushTail(&ss->freeRequests, r);
        }
    }
    p->voice = ss->voice;
    p->slots = (int16_t)packets;
    p->memory = p;
    p->release = (PacketReleaseFn)(uintptr_t)0x0013bb20u;
    p->framesDone = (PacketFramesFn)(uintptr_t)0x0013bb50u;
    p->context = ss;
    p->format.sampleRate = 48000;
    p->format.channels = (uint8_t)channels;
    p->format.sampleRep = 10;
    p->master = (int8_t)Range(0, channels - 1);
    p->queuedFrames = Range(0, 20000);
    p->takenFrames = Chance(50) ? 0 : Range(0, 5000);
    StreamAt(index) = ss;
    PlayerAt(player) = p;
    return ss;
}

// ---- disc data

uint32_t BE32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
uint32_t LE32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// The channel count of an SCHl chunk's PT header (tag 0x82), 1 if it has none
int HeaderChannels(const uint8_t *chunk, uint32_t length) {
    for (uint32_t k = 12; k + 2 < length; k++)
        if (chunk[k] == 0x82 && chunk[k + 1] == 0x01)
            return chunk[k + 2] >= 1 && chunk[k + 2] <= 6 ? chunk[k + 2] : 1;
    return 1;
}

// The current item of the header and data cases
const uint8_t *g_item;
uint32_t g_itemSize;
int g_itemChannels;
bool g_perturb;

// ---- the case kinds

// A PT header copy: walks the tags (id, length byte or 0xff + 4-byte length, value; 0xfc padding; 0xfd/0xfe
// markers; 0xff end) and changes values in place or inserts tags. out has room for 64 more bytes.
uint32_t PerturbHeader(const uint8_t *in, uint32_t length, uint8_t *out) {
    memcpy(out, in, length);
    uint32_t end = length, pos = 12, marker = 0;
    int edits = Range(1, 3);
    for (int e = 0; e < edits; e++) {
        int what = Range(0, 6);
        // find the tag positions afresh after each edit
        uint32_t at82 = 0, at84 = 0, at85 = 0, atA0 = 0;
        marker = 0;
        pos = 12;
        while (pos < end) {
            uint8_t id = out[pos];
            if (id == 0xff)
                break;
            if (id == 0xfc || id == 0xfd || id == 0xfe) {
                if (id == 0xfd && marker == 0)
                    marker = pos;
                pos++;
                continue;
            }
            if (pos + 1 >= end)
                break;
            uint32_t len = out[pos + 1], head = 2;
            if (len == 0xff) {
                if (pos + 6 > end)
                    break;
                len = BE32(out + pos + 2);
                head = 6;
            }
            if (id == 0x82 && len == 1)
                at82 = pos + head;
            if (id == 0x84 && len >= 1 && len <= 4)
                at84 = pos + head;
            if (id == 0x85 && len >= 1 && len <= 4)
                at85 = pos + head;
            if (id == 0xa0 && len == 1)
                atA0 = pos + head;
            pos += head + len;
        }
        uint8_t insert[24];
        uint32_t insertLength = 0, insertAt = 0;
        switch (what) {
        case 0:
            if (at82)
                out[at82] = (uint8_t)(Chance(70) ? Range(1, 2) : Range(1, 6));
            break;
        case 1:
            if (at84)
                out[at84 + 1] ^= (uint8_t)Next();
            break;
        case 2:
            if (atA0) {
                static const uint8_t reps[] = { 10, 20, 4, 8, 16, 7, 9, 0x40, 0x14, 0 };
                out[atA0] = Chance(80) ? reps[Range(0, 9)] : (uint8_t)Next();
            }
            break;
        case 3:
            if (at85)
                out[at85] ^= (uint8_t)Next();
            break;
        case 4:   // a blob (0x98 .. 0x9b, 0xa4, 0xa5)
            if (marker) {
                static const uint8_t ids[] = { 0x98, 0x99, 0x9a, 0x9b, 0xa4, 0xa5 };
                insert[0] = ids[Range(0, 5)];
                insert[1] = (uint8_t)Range(1, 16);
                RandomBytes(insert + 2, insert[1]);
                insertLength = 2u + insert[1];
                insertAt = marker + 1;
            }
            break;
        case 5:   // a per-channel azimuth offset
            if (marker) {
                insert[0] = (uint8_t)(0x9c + Range(0, 1));
                insert[1] = 2;
                RandomBytes(insert + 2, 2);
                insertLength = 4;
                insertAt = marker + 1;
            }
            break;
        case 6:   // user data
            insert[0] = 0x14;
            insert[1] = (uint8_t)Range(1, 8);
            RandomBytes(insert + 2, insert[1]);
            insertLength = 2u + insert[1];
            insertAt = 12;
            break;
        }
        if (insertLength != 0 && end + insertLength <= length + 64) {
            memmove(out + insertAt + insertLength, out + insertAt, end - insertAt);
            memcpy(out + insertAt, insert, insertLength);
            end += insertLength;
        }
    }
    *(uint32_t *)(out + 4) = end;
    return end;
}

void SetupHeader(int index) {
    (void)index;
    int requests = Range(1, 5), active = Range(1, requests);
    int channels = Range(1, 2);
    W->streamIndex = Range(0, 15);
    W->player = Range(0, 15);
    StreamState *ss = MakeStream(0, W->streamIndex, W->player, requests, active, 15, channels);
    // which request the header belongs to: the next after current
    if (Chance(50) || active == 1)
        ss->current = NULL;
    else
        ss->current = (StreamRequest *)((uint8_t *)ss + 0x138 + 0x28 * Range(0, active - 2));
    uint8_t *chunk = W->chunks;
    uint32_t length = g_itemSize;
    if (g_perturb)
        length = PerturbHeader(g_item, g_itemSize, chunk);
    else
        memcpy(chunk, g_item, length);
    W->chunk = (uint32_t *)chunk;
    W->chunkUsed = (length + 15u) & ~15u;
    switch (Range(0, 3)) {
    case 0:   // idle
        ss->state = 0;
        break;
    case 1: { // playing this very header: what our SNDI_patchtohdr makes of it (no blobs: the disc has none)
        StreamLayout layout;
        SNDI_patchtohdr(0, (uint8_t *)(W->chunk + 2), &ss->format, &ss->attributes, &layout);
        ss->state = 1;
        break;
    }
    case 2:   // playing another format
        ss->format.sampleRate = (uint16_t)(Chance(50) ? 48000 : Range(1, 65535));
        ss->format.channels = (uint8_t)Range(1, 2);
        ss->format.sampleRep = 10;
        RandomBytes(&ss->attributes, Chance(50) ? 8 : sizeof(ss->attributes));
        for (int i = 0; i < 6; i++)
            ss->attributes.stretchData[i] = NULL;
        ss->state = (uint8_t)(Chance(80) ? 1 : 0);
        break;
    default:  // already waiting to restart
        ss->format.sampleRate = 48000;
        ss->format.channels = (uint8_t)Range(1, 2);
        ss->format.sampleRep = 10;
        ss->state = 2;
        break;
    }
    W->arenaUsed = (W->arenaUsed + 15u) & ~15u;
}

void RunHeader() {
    int r = ParseHeader(W->streamIndex, W->chunk);
    Log(0x100, (uint32_t)r);
}

void SetupData(int index) {
    (void)index;
    int channels = g_itemChannels;
    int requests = Range(1, 4), active = Range(1, requests), packets = Range(2, 15);
    W->streamIndex = Range(0, 15);
    W->player = Range(0, 15);
    StreamState *ss = MakeStream(0, W->streamIndex, W->player, requests, active, packets, channels);
    ss->format.sampleRate = 48000;
    ss->format.channels = (uint8_t)channels;
    ss->format.sampleRep = 10;
    ss->state = 1;
    ss->current = (StreamRequest *)((uint8_t *)ss + 0x138 + 0x28 * Range(0, active - 1));
    PacketPlayer *p = PlayerAt(W->player);
    for (int c = 0; c < channels; c++) {
        p->count[c] = (int16_t)(Chance(15) ? packets - 1 : Range(0, packets - 1));
        p->readIndex[c] = (int16_t)Range(0, packets - 1);
    }
    p->writeIndex = (int16_t)Range(0, packets - 1);
    p->serial = Next();
    p->waiting = (uint8_t)Range(0, 1);
    uint32_t length = g_itemSize < (uint32_t)kSmallChunkBytes ? g_itemSize : (uint32_t)kSmallChunkBytes;
    memcpy(W->chunks, g_item, length);
    W->chunk = (uint32_t *)W->chunks;
    if (Chance(4))
        W->chunk[2] = 0;
    else if (Chance(4))
        W->chunk[2] |= 0x80000000u;
    else if (Chance(2))
        W->chunk[2] = 0x80000000u;
    W->ss = ss;
}

void RunData() {
    int r = ParseData(W->ss, W->chunk);
    Log(0x101, (uint32_t)r);
}

void SetupApi(int index) {
    (void)index;
    for (int s = 0; s < kStreams; s++) {
        int requests = Range(1, 5), active = Range(0, requests);
        int index_ = Range(0, 15);
        while (StreamAt(index_) != NULL)
            index_ = (index_ + 1) & 15;
        int player = Range(0, 15);
        while (PlayerAt(player) != NULL)
            player = (player + 1) & 15;
        StreamState *ss = MakeStream(s, index_, player, requests, active, Range(2, 15), Range(1, 2));
        ss->format.sampleRate = (uint16_t)(Chance(10) ? 0 : Range(1, 65535));
        ss->nextFormat.sampleRate = (uint16_t)Range(1, 65535);
        ss->format.channels = ss->nextFormat.channels = (uint8_t)Range(1, 2);
        ss->current = active > 0 && Chance(70) ? (StreamRequest *)((uint8_t *)ss + 0x138 + 0x28 * Range(0, active - 1))
                                               : NULL;
        ss->freeRequests.count = Chance(50) ? ss->freeRequests.count : Range(-1, 2);
    }
}

// a request id: one that exists, one of a valid stream that does not, or junk
int PickId() {
    int s = Range(0, kStreams - 1);
    StreamState *ss = (StreamState *)W->streamMemory[s];
    int r = Range(0, 9);
    if (r < 6 && ss->active.count > 0) {
        StreamRequest *q = (StreamRequest *)ss->active.head;
        for (int k = Range(0, ss->active.count - 1); k > 0 && q->next != NULL; k--)
            q = q->next;
        return q->id;
    }
    if (r < 8 && ss->active.head != NULL)
        return (int)((Next() & 0x7fff00u) | (uint32_t)(((StreamRequest *)ss->active.head)->id & 0xff));
    return (int)Next() >> Range(0, 24);
}

int PickStream() {
    int r = Range(0, 9);
    if (r < 7)
        for (int k = 0, i = Range(0, 15); k < 16; k++, i = (i + 1) & 15)
            if (StreamAt(i) != NULL)
                return i;
    return Range(-3, 20);
}

void RunApi() {
    g_seed = W->caseSeed;
    for (int op = 0; op < 24; op++) {
        int r = Range(0, 15);
        switch (r) {
        case 0:
        case 1: {
            int id = PickId();
            StreamState *ss = GetStreamPtr(id & 0xff);
            bool rateZero = id >= 0 && ss != NULL && (ss->format.sampleRate == 0 || ss->nextFormat.sampleRate == 0);
            if (rateZero)   // the original divides by it: both would fault
                break;
            int v = RequestStatus_(id, &W->rs);
            Log(0x200, (uint32_t)v, (uint32_t)W->rs.state, W->rs.playedMs, W->rs.remainingMs, W->rs.outstandingMs);
            break;
        }
        case 2:
        case 3: {
            int v = Status(PickStream(), &W->st);
            Log(0x201, (uint32_t)v, (uint32_t)W->st.requests, (uint32_t)W->st.id, W->st.bufferedMs);
            break;
        }
        case 4:
            Log(0x202, P(GetRequestPtr(PickId())));
            break;
        case 5:
            Log(0x203, P(GetStreamPtr(Range(-5, 40))));
            break;
        case 6: {
            int s = Range(0, kStreams - 1);
            StreamState *ss = (StreamState *)W->streamMemory[s];
            Log(0x204, (uint32_t)IsHeld(ss));
            break;
        }
        case 7:
            Log(0x205, (uint32_t)VoiceToPacket(Range(-2, 260)));
            break;
        case 8: {
            int s = Range(0, kStreams - 1);
            StreamState *ss = (StreamState *)W->streamMemory[s];
            Log(0x206, (uint32_t)SubmitSpace(ss->player), (uint32_t)FramesOutstanding(ss->player));
            break;
        }
        case 9: {
            int a = Range(-2, 40), b = Range(-2, 64);
            Log(0x207, (uint32_t)Overhead(a, b), (uint32_t)OverheadTap(a, b));
            break;
        }
        case 10:
            Log(0x208, (uint32_t)ModifyHold(PickId(), (int)Next()));
            break;
        case 11:
            Log(0x209, (uint32_t)Vol(PickStream(), (int)Next()), (uint32_t)Pitch(PickStream(), (int)Next()));
            break;
        case 12:
            Log(0x20a, (uint32_t)Lowpass(PickStream(), (int)Next()),
                (uint32_t)Pos3d(PickStream(), (int)Next(), (int)Next()));
            break;
        case 13:
            Log(0x20b, (uint32_t)AutoVol(PickStream(), (int)Next() >> Range(0, 31), (int)Next()));
            break;
        case 14:
            Log(0x20c, (uint32_t)Greedy(PickStream(), (int)Next()));
            break;
        default:
            break;
        }
    }
}

void SetupPacket(int index) {
    (void)index;
    PacketPlayer *p = (PacketPlayer *)W->streamMemory[0];
    int slots = Range(2, 8), channels = Range(1, 6);
    memset(p, 0, (size_t)(0x5c + slots * 0x20));
    p->voice = Chance(70) ? (int)(Next() & 0x7fff) : -1;
    p->slots = (int16_t)slots;
    p->memory = p;
    p->release = Chance(90) ? &FakeReleaseCallback : NULL;
    p->framesDone = Chance(90) ? &FakeFramesCallback : NULL;
    p->context = W;
    p->format.sampleRate = 48000;
    p->format.channels = (uint8_t)channels;
    p->format.sampleRep = 10;
    p->master = (int8_t)Range(0, channels - 1);
    p->waiting = (uint8_t)Range(0, 1);
    for (int c = 0; c < channels; c++)
        p->stretchData[c] = Chance(30) ? W->arena + 16 * c : NULL;
    W->player = Range(0, 15);
    PlayerAt(W->player) = p;
    if (Chance(50))   // another player, for voicetopackethandle
        PlayerAt((W->player + Range(1, 15)) & 15) = (PacketPlayer *)W->streamMemory[1];
    ((PacketPlayer *)W->streamMemory[1])->voice = (int)(Next() & 0x7fff);
    W->format.sampleRate = (uint16_t)Range(1, 65535);
    W->format.channels = (uint8_t)channels;
    W->format.sampleRep = (uint8_t)Range(0, 30);
    RandomBytes(&W->attributes, sizeof(W->attributes));
    for (int i = 0; i < 6; i++)
        W->attributes.stretchData[i] = Chance(30) ? W->arena + 0x100 + 16 * i : NULL;
    W->attributes.renderMode = Chance(50) ? 0x24 : (uint16_t)Next();
    RandomOpts(&W->opts[0]);
}

void RunPacket() {
    g_seed = W->caseSeed;
    PacketPlayer *p = (PacketPlayer *)W->streamMemory[0];
    int player = W->player, channels = p->format.channels;
    for (int op = 0; op < 80; op++) {
        int r = Range(0, 99);
        if (r < 35) {
            W->packet.unused00 = Next();
            W->packet.unused08 = Next();
            W->packet.frames = Chance(10) ? Next() : (uint32_t)Range(0, 4000) | (Chance(50) ? 0x80000000u : 0);
            for (int c = 0; c < 6; c++)
                W->packet.channels[c] = Chance(5) ? NULL : W->chunks + Range(0, 4000);
            Log(0x300, Submit(player, &W->packet));
        } else if (r < 65) {
            int frames = 0x1234, continued = 0x5678;
            uint8_t *data = Get(player, Range(0, channels - 1), &frames, &continued);
            Log(0x301, P(data), (uint32_t)frames, (uint32_t)continued);
        } else if (r < 80) {
            FreeFrames(player, Range(0, channels - 1), Range(-100, 3000));
        } else if (r < 88) {
            Flush();
        } else if (r < 93) {
            Log(0x302, (uint32_t)SubmitSpace(player), (uint32_t)FramesOutstanding(player));
        } else if (r < 95) {
            Log(0x303, (uint32_t)Stop(player));
        } else if (r < 98) {
            Log(0x304, (uint32_t)Start(player, &W->format, &W->attributes, &W->opts[0]));
        } else {
            Log(0x305, (uint32_t)VoiceToPacket(Range(-1, 255)));
        }
    }
    Log(0x306, (uint32_t)*(int32_t *)(uintptr_t)0x00244fe0u);
}

int PickHold() {
    int r = Range(0, 99);
    if (r < 60)
        return 0;
    if (r < 90)
        return Range(10, 3000);
    if (r < 95)
        return -1;
    return Range(-100000, 100000);
}

// Streams in arbitrary states (idle, playing, waiting to restart; held or not) whose STREAM holds a run of chunks
// cut from a disc stream at a random chunk, serviced for a few ticks. A run with data before its first header goes
// only to a stream that has a format: SNDSTRMI_parsedata fills the packet's channel pointers for the stream's
// format.channels and stores the chunk's address before channels[0], and SNDPKTPLAY_submit copies the player's
// format.channels of them - on a stream with no header parsed (channels 0) that is uninitialised stack, which the
// original and ours do not share.
const uint32_t kServiceChunkBytes = 0x20000;

void SetupService(int index) {
    (void)index;
    int n = Range(1, kStreams);
    for (int s = 0; s < n; s++) {
        const DiscStream &d = g_play[Range(0, g_playCount - 1)];
        // the chunk run: from a random chunk of the stream, at most a share of the chunk memory
        uint32_t start = 0, at = 0, skip = (uint32_t)Range(0, 60);
        for (uint32_t k = 0; k < skip && at + 8 <= d.size; k++) {
            start = at;
            at += *(uint32_t *)(d.data + at + 4);
        }
        if (at + 8 > d.size)
            at = start;
        uint32_t room = kServiceChunkBytes / kStreams, length = 0;
        bool header = false, dataFirst = false;
        while (at + length + 8 <= d.size) {
            uint32_t next = *(uint32_t *)(d.data + at + length + 4);
            if (length + next > room)
                break;
            uint32_t tag = *(uint32_t *)(d.data + at + length);
            dataFirst |= !header && tag == 0x6c444353u;
            header |= tag == 0x6c484353u;
            length += next;
        }
        // a header moves the current request on: keep one after it (the original would fault on none)
        int requests = Range(header ? 2 : 1, 5), active = Range(header ? 2 : 1, requests), packets = Range(2, 15);
        int streamIndex = Range(0, 15);
        while (StreamAt(streamIndex) != NULL)
            streamIndex = (streamIndex + 1) & 15;
        int player = Range(0, 15);
        while (PlayerAt(player) != NULL)
            player = (player + 1) & 15;
        StreamState *ss = MakeStream(s, streamIndex, player, requests, active, packets, d.channels);
        ss->state = (uint8_t)Range(0, 2);
        if (ss->state != 0 || dataFirst || Chance(50)) {   // the format the player was started with
            ss->format.sampleRate = 48000;
            ss->format.channels = (uint8_t)d.channels;
            ss->format.sampleRep = 10;
        }
        if (Chance(30) || (ss->state == 2 && dataFirst))   // a restart starts the player with nextFormat
            ss->nextFormat = ss->format;
        ss->current = (StreamRequest *)((uint8_t *)ss + 0x138 + 0x28 * Range(0, header ? active - 2 : active - 1));
        for (StreamRequest *r = (StreamRequest *)ss->active.head; r != NULL; r = r->next) {
            r->played = 0;
            r->outstanding = 0;
            r->total = 0x7fffffff;
        }
        PacketPlayer *p = PlayerAt(player);
        if (Chance(50)) {
            p->queuedFrames = 0;
            p->takenFrames = 0;
        }
        FakeStream *f = &W->fake[s];
        uint32_t base = W->chunkUsed;
        memcpy(W->chunks + base, d.data + at, length);
        W->chunkUsed = (base + length + 15u) & ~15u;
        for (uint32_t o = 0; o + 8 <= length && f->tail < kMaxPending; o += *(uint32_t *)(W->chunks + base + o + 4))
            f->pending[f->tail++] = base + o;
    }
}

void Mixer();

void RunService() {
    g_seed = W->caseSeed;
    int ticks = Range(1, 6);
    for (int t = 0; t < ticks; t++) {
        W->budget = Range(0, 14);
        Service();
        Mixer();
        Flush();
    }
}

void SetupScenario(int index) {
    (void)index;
    strcpy(W->name, "driving\\mis01en.spe");
}

void Mixer() {
    for (int p = 0; p < 16; p++) {
        PacketPlayer *pp = PlayerAt(p);
        if (pp == NULL || pp->voice < 0)
            continue;
        int n = pp->format.channels;
        if (n > 6)
            n = 6;
        int takes = Range(0, 2);
        for (int k = 0; k < takes; k++)
            for (int c = 0; c < n; c++) {
                int frames = 0, continued = 0;
                uint8_t *data = Get(p, c, &frames, &continued);
                Log(0x400, (uint32_t)p, (uint32_t)c, P(data), (uint32_t)frames, (uint32_t)continued);
                if (data == NULL)
                    continue;
                if (Chance(10)) {
                    int part = Range(0, frames > 0 ? frames : 0);
                    FreeFrames(p, c, part);
                    FreeFrames(p, c, frames - part);
                } else {
                    FreeFrames(p, c, frames);
                }
            }
    }
}

void RunScenario() {
    g_seed = W->caseSeed;
    int n = Range(1, kStreams), handle[kStreams];
    int ids[256], idCount = 0;
    for (int s = 0; s < n; s++) {
        RandomOpts(&W->opts[s]);
        int requests = Range(2, 5), packets = Range(3, 15);
        int size = Overhead(requests, packets);
        Log(0x500, (uint32_t)size);
        if (size > kStreamBytes)
            size = kStreamBytes;
        if (s == kStreams - 1 && Chance(25)) {
            W->fake[kStreams].magic = kStrmMagic;
            Log(0x501, (uint32_t)OverheadTap(requests, packets));
            handle[s] = CreateTap(&W->fake[kStreams], &W->opts[s], requests, packets, W->streamMemory[s], size);
        } else {
            handle[s] = Create(&W->opts[s], requests, packets, W->streamMemory[s], size);
        }
        Log(0x502, (uint32_t)handle[s]);
    }
    int ticks = Range(150, 500);
    for (int t = 0; t < ticks; t++) {
        for (int s = 0; s < n; s++) {
            if (handle[s] < 0 || !(t == 0 || Chance(5)))
                continue;
            W->nextFile = Range(0, g_playCount - 1);
            int id;
            int kind = Range(0, 99);
            if (kind < 96) {
                id = QueueFile(handle[s], PickHold(), W->name, (uint32_t)Range(0, 100000));
            } else if (kind < 98) {
                id = Queue(handle[s], PickHold(), W->chunks, 0, 1);
            } else {
                id = QueueRequestId(handle[s], PickHold(), Chance(80) ? Next() : 0);
            }
            Log(0x503, (uint32_t)id);
            if (id >= 0 && idCount < 256)
                ids[idCount++] = id;
        }
        W->budget = Range(0, 12);
        Service();
        Mixer();
        Flush();
        if (Chance(10)) {
            int s = Chance(90) ? handle[Range(0, n - 1)] : Range(-2, 17);
            int v = Status(s, &W->st);
            Log(0x504, (uint32_t)v, (uint32_t)W->st.requests, (uint32_t)W->st.id, W->st.bufferedMs);
        }
        if (Chance(10) && idCount > 0) {
            int id = Chance(90) ? ids[Range(0, idCount - 1)] : (int)Next();
            int v = RequestStatus_(id, &W->rs);
            Log(0x505, (uint32_t)v, (uint32_t)W->rs.state, W->rs.playedMs, W->rs.remainingMs, W->rs.outstandingMs);
        }
        if (Chance(3) && idCount > 0)
            Log(0x506, (uint32_t)ModifyHold(ids[Range(0, idCount - 1)], Chance(70) ? 0 : PickHold()));
        if (Chance(3)) {
            int s = handle[Range(0, n - 1)];
            switch (Range(0, 5)) {
            case 0:
                Log(0x507, (uint32_t)Vol(s, Range(0, 127)));
                break;
            case 1:
                Log(0x508, (uint32_t)Pitch(s, Range(0, 0x2000)));
                break;
            case 2:
                Log(0x509, (uint32_t)Lowpass(s, Range(0, 0xffff)));
                break;
            case 3:
                Log(0x50a, (uint32_t)Pos3d(s, Range(0, 0xffff), Range(-0x4000, 0x4000)));
                break;
            case 4:
                Log(0x50b, (uint32_t)AutoVol(s, Range(-100, 5000), Range(0, 127)));
                break;
            default:
                Log(0x50c, (uint32_t)Greedy(s, Range(0, 100000)));
                break;
            }
        }
        if (Chance(2))
            Log(0x50d, (uint32_t)VoiceToPacket(Range(185, 230)));
        if (Chance(1)) {
            int s = Range(0, n - 1);
            Log(0x50e, (uint32_t)Purge(handle[s]));
            // a tap's STREAM is its owner's: purge leaves it alone, so the owner empties it (as the old movie
            // player did) - else its chunks would reach a stream with no request, which faults on both sides
            if (handle[s] >= 0 && ((StreamState *)W->streamMemory[s])->external != 0)
                W->fake[kStreams].head = W->fake[kStreams].tail;
        }
    }
    if (Chance(50)) {
        Log(0x50f, (uint32_t)DestroyAll());
    } else {
        for (int s = 0; s < n; s++)
            Log(0x510, (uint32_t)Destroy(handle[s]));
        Log(0x511, (uint32_t)Destroy(Range(-2, 20)));
    }
}

// ---- running a case

typedef void (*SetupFn)(int);
typedef void (*RunFn)();

struct Kind {
    const char *name;
    SetupFn setup;
    RunFn run;
    uint32_t chunkBytes;   // how much of the chunk memory it uses (snapshotted and compared)
    int ran, differ, faults;
};

Kind g_kinds[] = {
    { "header", SetupHeader, RunHeader, kSmallChunkBytes, 0, 0, 0 },
    { "data", SetupData, RunData, kSmallChunkBytes, 0, 0, 0 },
    { "api", SetupApi, RunApi, kSmallChunkBytes, 0, 0, 0 },
    { "packet", SetupPacket, RunPacket, kSmallChunkBytes, 0, 0, 0 },
    { "service", SetupService, RunService, kServiceChunkBytes, 0, 0, 0 },
    { "scenario", SetupScenario, RunScenario, kChunkBytes, 0, 0, 0 },
};
enum { K_HEADER, K_DATA, K_API, K_PACKET, K_SERVICE, K_SCENARIO };

unsigned g_x87, g_sse;

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

bool Guarded(RunFn run) {
#ifdef _MSC_VER
    __try {
        run();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        return false;
    }
#else
    run();
    return true;
#endif
}

bool GuardedSetup(SetupFn setup, int index) {
#ifdef _MSC_VER
    __try {
        setup(index);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        return false;
    }
#else
    setup(index);
    return true;
#endif
}

int g_cases, g_checks, g_differ, g_details, g_faultsBoth, g_setupFaults;

void Detail(const char *format, ...) {
    if (g_details++ >= 10)
        return;
    char line[400];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    printf("[sndstreamshadow]   %s\n", line);
}

const char *Region(size_t offset) {
    struct R {
        size_t at;
        const char *name;
    };
    static const R regions[] = {
        { 0, "case state" }, { offsetof(Workspace, rs), "outputs" }, { offsetof(Workspace, fake), "fake STREAMs" },
        { offsetof(Workspace, streamMemory), "stream memory" }, { offsetof(Workspace, voices), "voices" },
        { offsetof(Workspace, arena), "arena" }, { offsetof(Workspace, chunks), "chunks" } };
    const char *name = "?";
    for (const R &r : regions)
        if (offset >= r.at)
            name = r.name;
    return name;
}

void RunCase(int kindIndex, int index) {
    Kind &kind = g_kinds[kindIndex];
    g_seed = 0x9e3779b9u ^ ((uint32_t)kindIndex * 0x01000193u) ^ ((uint32_t)index * 0x85ebca6bu) ^
             ((uint32_t)g_cases * 0x27d4eb2fu);
    if (g_seed == 0)
        g_seed = 1;
    g_extent = offsetof(Workspace, chunks) + (size_t)kind.chunkBytes;
    memset(W, 0, g_extent);
    W->caseSeed = Next() | 1u;
    W->nextFile = -1;
    GlobalsBase();
    g_side = -1;
    if (!GuardedSetup(kind.setup, index)) {
        g_setupFaults++;
        return;
    }
    memcpy(g_snapshot, W, g_extent);
    memcpy(g_snapG1, (void *)(uintptr_t)kG1, kG1Size);
    memcpy(g_snapG2, (void *)(uintptr_t)kG2, kG2Size);

    g_side = 0;
    g_hash[0] = 2166136261u;
    g_logCount[0] = 0;
    Originals(true);
    bool okOriginal = Guarded(kind.run);
    Originals(false);
    memcpy(g_resultOriginal, W, g_extent);
    memcpy(g_resG1, (void *)(uintptr_t)kG1, kG1Size);
    memcpy(g_resG2, (void *)(uintptr_t)kG2, kG2Size);

    memcpy(W, g_snapshot, g_extent);
    memcpy((void *)(uintptr_t)kG1, g_snapG1, kG1Size);
    memcpy((void *)(uintptr_t)kG2, g_snapG2, kG2Size);
    g_side = 1;
    g_hash[1] = 2166136261u;
    g_logCount[1] = 0;
    g_counting = kindIndex == K_SCENARIO;
    bool okOurs = Guarded(kind.run);
    g_counting = false;
    g_side = -1;

    g_cases++;
    kind.ran++;
    g_checks += 5;
    bool differ = false;
    if (okOriginal != okOurs) {
        differ = true;
        Detail("%s case %d: the original %s, ours %s", kind.name, index, okOriginal ? "ran" : "faulted",
               okOurs ? "ran" : "faulted");
    } else if (!okOriginal) {
        g_faultsBoth++;
        kind.faults++;
        uint32_t last = g_logCount[0] < (uint32_t)kKeep ? g_logCount[0] : (uint32_t)kKeep;
        const uint32_t *x = last != 0 ? g_log[0][last - 1].w : NULL;
        if (x != NULL)
            Detail("%s case %d faulted on both sides after %u calls, the last %x(%x %x %x %x %x)", kind.name, index,
                   g_logCount[0], x[0], x[1], x[2], x[3], x[4], x[5]);
    }
    const uint32_t *a = (const uint32_t *)g_resultOriginal, *b = (const uint32_t *)W;
    size_t words = g_extent / 4, first = (size_t)-1;
    int differing = 0;
    for (size_t j = 0; j < words; j++)
        if (a[j] != b[j]) {
            if (first == (size_t)-1)
                first = j;
            differing++;
        }
    if (differing != 0) {
        differ = true;
        Detail("%s case %d: %d workspace words differ, first %s +0x%x: original %08x, ours %08x", kind.name, index,
               differing, Region(first * 4), (unsigned)(first * 4), a[first], b[first]);
    }
    for (int g = 0; g < 2; g++) {
        const uint8_t *o = g == 0 ? g_resG1 : g_resG2, *n = (const uint8_t *)(uintptr_t)(g == 0 ? kG1 : kG2);
        uint32_t size = g == 0 ? kG1Size : kG2Size;
        for (uint32_t k = 0; k < size; k++)
            if (o[k] != n[k]) {
                differ = true;
                Detail("%s case %d: global 0x%08x differs: original %02x, ours %02x", kind.name, index,
                       (g == 0 ? kG1 : kG2) + k, o[k], n[k]);
                break;
            }
    }
    if (g_hash[0] != g_hash[1] || g_logCount[0] != g_logCount[1]) {
        differ = true;
        uint32_t m = g_logCount[0] < g_logCount[1] ? g_logCount[0] : g_logCount[1];
        if (m > (uint32_t)kKeep)
            m = kKeep;
        uint32_t k = 0;
        while (k < m && memcmp(&g_log[0][k], &g_log[1][k], sizeof(Entry)) == 0)
            k++;
        if (k < m) {
            const uint32_t *x = g_log[0][k].w, *y = g_log[1][k].w;
            Detail("%s case %d: the calls differ at call %u of %u/%u: original %x(%x %x %x %x %x), ours %x(%x %x %x %x "
                   "%x)", kind.name, index, k, g_logCount[0], g_logCount[1], x[0], x[1], x[2], x[3], x[4], x[5],
                   y[0], y[1], y[2], y[3], y[4], y[5]);
        } else {
            Detail("%s case %d: the calls differ (%u calls / %u calls)", kind.name, index, g_logCount[0],
                   g_logCount[1]);
        }
    }
    if (differ) {
        g_differ++;
        kind.differ++;
    }
}

// ---- calcdatarate: a pure function of the format

int g_rateCases, g_rateDiffer;

void TestCalcDataRate() {
    static const int rates[] = { 0, 1, 8000, 11025, 22050, 24000, 32000, 44100, 48000, 65535 };
    static const int channels[] = { 0, 1, 2, 3, 6, 255 };
    g_seed = 0x51ed2701u;
    for (int rep = 0; rep < 256; rep++)
        for (int c : channels)
            for (int k = 0; k < 11; k++) {
                StreamFormat f;
                f.sampleRate = (uint16_t)(k < 10 ? rates[k] : (int)(Next() & 0xffff));
                f.channels = (uint8_t)c;
                f.sampleRep = (uint8_t)rep;
                Originals(true);
                int a = CalcDataRate(&f, true);
                Originals(false);
                int b = CalcDataRate(&f, false);
                g_rateCases++;
                if (a != b) {
                    g_rateDiffer++;
                    Detail("calcdatarate rate %u channels %u rep %u: original %d, ours %d", f.sampleRate, f.channels,
                           f.sampleRep, a, b);
                }
            }
}

// ---- the disc: every archive's streams

int g_headers, g_dataChunks, g_files, g_level = 1;

void ReadArchive(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 16) {
        fclose(f);
        return;
    }
    uint8_t *file = (uint8_t *)malloc((size_t)size + 64);
    if (file == NULL || fread(file, 1, (size_t)size, f) != (size_t)size || memcmp(file, "BIGF", 4) != 0) {
        fclose(f);
        free(file);
        return;
    }
    fclose(f);
    g_files++;
    uint32_t entries = BE32(file + 8), p = 16;
    int stride = g_level >= 2 ? 16 : 64, picked = 0;
    for (uint32_t e = 0; e < entries && p + 8 < (uint32_t)size; e++) {
        uint32_t offset = BE32(file + p), length = BE32(file + p + 4);
        p += 8;
        while (p < (uint32_t)size && file[p] != 0)
            p++;
        p++;
        if ((uint64_t)offset + length > (uint64_t)size)
            continue;
        uint32_t at = offset, end = offset + length, streamStart = 0;
        int channels = 1;
        while (at + 8 <= end) {
            uint32_t chunkLength = LE32(file + at + 4);
            if (chunkLength < 8 || at + chunkLength > end)
                break;
            uint32_t tag = LE32(file + at);
            if (tag == 0x6c484353u) {   // SCHl
                streamStart = at;
                channels = HeaderChannels(file + at, chunkLength);
                g_item = file + at;
                g_itemSize = chunkLength;
                g_perturb = false;
                RunCase(K_HEADER, g_headers);
                g_perturb = true;
                RunCase(K_HEADER, g_headers);
                g_headers++;
            } else if (tag == 0x6c444353u) {   // SCDl
                if (g_dataChunks++ % stride == 0) {
                    g_item = file + at;
                    g_itemSize = chunkLength;
                    g_itemChannels = channels;
                    RunCase(K_DATA, g_dataChunks);
                }
            } else if (tag == 0x6c454353u) {   // SCEl: a whole stream, kept for the scenarios if small
                uint32_t streamSize = at + chunkLength - streamStart;
                if (streamStart != 0 && streamSize <= 256u * 1024u && g_playCount < kMaxPlay &&
                    picked < (g_level >= 2 ? 3 : 2) && ((Next() & 3) == 0 || channels == 2)) {
                    uint8_t *copy = (uint8_t *)malloc(streamSize);
                    if (copy != NULL) {
                        memcpy(copy, file + streamStart, streamSize);
                        g_play[g_playCount].data = copy;
                        g_play[g_playCount].size = streamSize;
                        g_play[g_playCount].channels = channels;
                        g_playCount++;
                        picked++;
                    }
                }
                streamStart = 0;
            }
            at += chunkLength;
        }
    }
    free(file);
}

}   // namespace

void SndStreamShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_SNDSTREAMSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    g_level = atoi(env);
    FpControlGet(&g_x87, &g_sse);

    W = (Workspace *)VirtualAlloc(NULL, sizeof(Workspace), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_snapshot = (Workspace *)VirtualAlloc(NULL, sizeof(Workspace), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_resultOriginal = (Workspace *)VirtualAlloc(NULL, sizeof(Workspace), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_log[0] = (Entry *)malloc(sizeof(Entry) * kKeep);
    g_log[1] = (Entry *)malloc(sizeof(Entry) * kKeep);
    if (W == NULL || g_snapshot == NULL || g_resultOriginal == NULL || g_log[0] == NULL || g_log[1] == NULL) {
        printf("[sndstreamshadow] no memory\n");
        fflush(stdout);
        return;
    }

    int replaced = 0, total = 0;
    for (const unsigned *a = kOurs; *a != 0; a++) {
        total++;
        if (XbeOriginal_Restore(*a, true)) {
            XbeOriginal_Restore(*a, false);
            replaced++;
        }
    }

    memcpy(g_savedG1, (void *)(uintptr_t)kG1, kG1Size);
    memcpy(g_savedG2, (void *)(uintptr_t)kG2, kG2Size);
    InstallFakes();

    TestCalcDataRate();

    char folder[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
    if (Xbox_ResolvePath("D:\\driving", folder, sizeof(folder))) {
        static const char *const kinds[] = { "*.mus", "*.spe" };
        for (const char *k : kinds) {
            snprintf(pattern, sizeof(pattern), "%s\\%s", folder, k);
            WIN32_FIND_DATAA found;
            HANDLE search = FindFirstFileA(pattern, &found);
            if (search == INVALID_HANDLE_VALUE)
                continue;
            do {
                snprintf(path, sizeof(path), "%s\\%s", folder, found.cFileName);
                ReadArchive(path);
            } while (FindNextFileA(search, &found));
            FindClose(search);
        }
    }

    int apiCases = g_level >= 2 ? 4000 : 1000, packetCases = g_level >= 2 ? 2000 : 500;
    for (int i = 0; i < apiCases; i++)
        RunCase(K_API, i);
    for (int i = 0; i < packetCases; i++)
        RunCase(K_PACKET, i);
    int services = g_playCount == 0 ? 0 : (g_level >= 2 ? 4000 : 1000);
    for (int i = 0; i < services; i++)
        RunCase(K_SERVICE, i);
    int scenarios = g_playCount == 0 ? 0 : (g_level >= 2 ? 256 : 64);
    for (int i = 0; i < scenarios; i++)
        RunCase(K_SCENARIO, i);

    HooksRemove();
    memcpy((void *)(uintptr_t)kG1, g_savedG1, kG1Size);
    memcpy((void *)(uintptr_t)kG2, g_savedG2, kG2Size);
    VirtualFree(W, 0, MEM_RELEASE);
    VirtualFree(g_snapshot, 0, MEM_RELEASE);
    VirtualFree(g_resultOriginal, 0, MEM_RELEASE);
    free(g_log[0]);
    free(g_log[1]);
    int playCount = g_playCount;
    for (int i = 0; i < g_playCount; i++)
        free(g_play[i].data);
    g_playCount = 0;
    W = NULL;

    int kindCount = (int)(sizeof(g_kinds) / sizeof(g_kinds[0]));
    char perKind[400];
    int at = 0;
    for (int k = 0; k < kindCount; k++)
        at += snprintf(perKind + at, sizeof(perKind) - (size_t)at, "%s%s %d/%d/%d", k ? ", " : "", g_kinds[k].name,
                       g_kinds[k].ran, g_kinds[k].differ, g_kinds[k].faults);
    printf("[sndstreamshadow] SND streams: %d cases, %d checks, %d differ (cases/differ/faulted on both: %s; "
           "calcdatarate %d cases, %d differ; %d archives, %d headers, %d data chunks, %d streams for scenarios; "
           "%d setup faults; scenario traffic: %u packets taken, %u chunks released, %u voice starts, %u player "
           "stops, %u finished requests seen; %d of %d entry points replaced)\n",
           g_cases + g_rateCases, g_checks + g_rateCases, g_differ + g_rateDiffer, perKind, g_rateCases, g_rateDiffer,
           g_files, g_headers, g_dataChunks, playCount, g_setupFaults, g_taken, g_released, g_starts, g_stops,
           g_finished, replaced, total);
    fflush(stdout);
}
