#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "AudioStreamShadow.h"
#include "FpControl.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <string>
#include <vector>

#include "../audio/Stream.h"
#include "../engine/UMemory.hpp"
#include "../sound/snd/Streams.h"
#include "../world/SoundMap.h"          // RefCounterMapBuyHead
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_AUDIOSTREAMSHADOW=1, from the first simulation tick.
//
// AStream and AStreamPriv (0x00121e80..0x00123870), ORIGINAL against PORT, the originals of the whole range swapped
// in for the original's runs (so an original reaches only originals):
//
//   - AStreamEntry's constructor on random names (upper case, '/', empty) and flags: the 0x44 bytes;
//   - the event queue (std::deque<AStreamEntry>): random sequences of push_back, pop_front, front and clear on two
//     queues built alike, and _Growmap on queues of every offset and size with random counts (both of its branches);
//     the queues compared after every step by their counts and every block's entry;
//   - the stream registry's tree (URefCounter<AStream>'s copies): random insert_unique of names that repeat in
//     other cases, erase at random nodes, erase(first, last) of random ranges and of everything, and both
//     destructors, on two maps of their own; compared as trees (shape, colours, names) and by the answers;
//   - AStream on synthetic streams (an AStream and AStreamPriv of the test's, a mix of its own, perturbed queues,
//     flags, files, requests, globals): Play, Next, Stop, DecodeError, FadeOut, Event, GetLatency, GetFile,
//     GetInternalVolume, the Is* flags and SetFilter; then AStreamPriv's constructor and destructor. The calls out
//     are recording fakes that answer from a script: SNDSYS_entercritical/leavecritical (which still take the
//     sound mutex), every SNDSTRM_* call made, TIMER_gettick, AMix::GetVolume, the C runtime's printf and the
//     three FILESYS calls. Compared: the AStream, the AStreamPriv (pointers by what they point at), the mix, the
//     four globals, the answer and the call log (function, arguments, file names by text, in order).
//
// Create, Get, Remove and AStream's constructor and destructor reach the live registries (mixes, sounds, streams)
// and are left to the lockstep runs.
//
// Mutations it catches: the low pass's 24000 in Play (SNDSTRM_lowpass's logged cutoff), SNDSTRM_vol and
// SNDSTRM_pitchmult called the other way round (the log's order), or _stricmp replaced by strcmp in the tree's
// insert_unique (names that repeat in another case).
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types: another test's of the same name must not merge with them

int g_cases, g_checks, g_differ, g_faults;
unsigned g_x87, g_sse;
DWORD g_mainThread;

void Check(bool same, const char *what, int index, const std::string *original = NULL,
           const std::string *port = NULL) {
    g_checks++;
    if (same)
        return;
    g_differ++;
    if (g_differ > 10)
        return;
    printf("[audiostream]   %s differs, case %d\n", what, index);
    if (original != NULL && port != NULL) {
        size_t at = 0;
        while (at < original->size() && at < port->size() && (*original)[at] == (*port)[at])
            at++;
        size_t from = at > 60 ? at - 60 : 0;
        printf("[audiostream]     original: ...%.160s\n", original->c_str() + from);
        printf("[audiostream]     port:     ...%.160s\n", port->c_str() + from);
    }
    fflush(stdout);
}

void Append(std::string &log, const char *format, ...) {
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    log += line;
}

void AppendBytes(std::string &log, const void *data, size_t bytes) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < bytes; i++)
        Append(log, "%02x", p[i]);
    log += ' ';
}

uint32_t g_random = 0x2545f491;
uint32_t Random(uint32_t below) {
    g_random = g_random * 1103515245u + 12345u;
    return below == 0 ? 0 : (g_random >> 8) % below;
}
float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Random(1 << 24)) * (1.0f / 16777216.0f);
}
uint32_t Bits(float f) {
    return std::bit_cast<uint32_t>(f);
}

template <class F>
bool Guarded(F &&run) {
#ifdef _MSC_VER
    __try {
        run();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        FpControlSetX87(g_x87);
        FpControlSetSse(g_sse);
        return false;
    }
#else
    run();
    return true;
#endif
}

// The originals of the whole stream range, for as long as the scope lives
struct Originals {
    Originals() { XbeOriginal_RestoreRange(0x00121e80, 0x00123870, true); }
    ~Originals() { XbeOriginal_RestoreRange(0x00121e80, 0x00123870, false); }
};

// ---- the originals

#define Orig_EntryConstruct ((AStreamEntry *(__fastcall *)(AStreamEntry *, int, const char *, float, bool, bool, bool, int))0x00121ea0)
#define Orig_QueuePopFront ((void (__fastcall *)(AStreamQueue *, int))0x001220c0)
#define Orig_QueueClear ((void (__fastcall *)(AStreamQueue *, int))0x001220f0)
#define Orig_QueueFront ((AStreamEntry *(__fastcall *)(AStreamQueue *, int))0x00122170)
#define Orig_QueueGrowMap ((void (__fastcall *)(AStreamQueue *, int, uint32_t))0x001230f0)
#define Orig_QueuePushBack ((void (__fastcall *)(AStreamQueue *, int, const AStreamEntry *))0x00123380)
#define Orig_TreeInsertUnique ((RefCounterInsertResult *(__fastcall *)(StreamRefTree *, int, RefCounterInsertResult *, const RefCounterValue *))0x001232a0)
#define Orig_TreeEraseAt ((RefCounterNode **(__fastcall *)(StreamRefTree *, int, RefCounterNode **, RefCounterNode *))0x00122b10)
#define Orig_TreeEraseRange ((RefCounterNode **(__fastcall *)(StreamRefTree *, int, RefCounterNode **, RefCounterNode *, RefCounterNode *))0x00123070)
#define Orig_TreeDestruct ((void (__fastcall *)(StreamRefTree *, int))0x001235a0)
#define Orig_TreeDestroyRange ((void (__fastcall *)(StreamRefTree *, int))0x00123560)
#define Orig_PrivConstruct ((AStreamPriv *(__fastcall *)(AStreamPriv *, int, const char *, const char *, unsigned int))0x00122190)
#define Orig_PrivDestruct ((void (__fastcall *)(AStreamPriv *, int))0x001222f0)
#define Orig_Play ((void (__fastcall *)(AStream *, int, ASoundPlayParams *))0x00122580)
#define Orig_Next ((void (__fastcall *)(AStream *, int))0x00122050)
#define Orig_Stop ((void (__fastcall *)(AStream *, int))0x00122430)
#define Orig_DecodeError ((void (__fastcall *)(AStream *, int, int))0x001224d0)
#define Orig_FadeOut ((void (__fastcall *)(AStream *, int))0x00121f90)
#define Orig_Event ((void (__fastcall *)(AStream *, int, const char *, float, bool, bool, bool))0x001234d0)
#define Orig_GetLatency ((double (__fastcall *)(AStream *, int))0x00121fc0)
#define Orig_GetInternalVolume ((float (__fastcall *)(AStream *, int))0x00122020)
#define Orig_GetFile ((const char *(__fastcall *)(AStream *, int))0x00122030)
#define Orig_SetFilter ((void (__fastcall *)(AStream *, int, float))0x00121f30)
#define Orig_IsOver ((bool (__fastcall *)(AStream *, int))0x00121f40)
#define Orig_IsNotFound ((bool (__fastcall *)(AStream *, int))0x00121f50)
#define Orig_IsPaused ((bool (__fastcall *)(AStream *, int))0x00121f60)
#define Orig_IsLoop ((bool (__fastcall *)(AStream *, int))0x00121f70)
#define Orig_IsEffect ((bool (__fastcall *)(AStream *, int))0x00121f80)

// The globals the stream code reads and writes
#define ShadowAudioFrames I32_AT(0x00243a68)
#define ShadowMissionOver BOOL8_AT(0x00243a77)
#define ShadowStreamHold I32_AT(0x00243a80)
#define ShadowStreamHoldTick I32_AT(0x00243a84)
#define ShadowSystem PTR_AT(0x00243b34)

struct Globals {
    int32_t frames;
    uint8_t missionOver;
    int32_t hold;
    int32_t holdTick;
};

Globals GlobalsSave() {
    Globals g = { ShadowAudioFrames, ShadowMissionOver, ShadowStreamHold, ShadowStreamHoldTick };
    return g;
}

void GlobalsLoad(const Globals &g) {
    ShadowAudioFrames = g.frames;
    ShadowMissionOver = g.missionOver;
    ShadowStreamHold = g.hold;
    ShadowStreamHoldTick = g.holdTick;
}

// ---- hooks: a jump written over an entry (and over the port a patched entry jumps to)

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[64];
int g_hookCount;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0])))
        return;
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    h.on = false;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
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

void HookInstall(uint32_t at, const void *to) {
    const uint8_t *entry = (const uint8_t *)(uintptr_t)at;
    uint32_t port = 0;
    if (entry[0] == 0xe9) {
        int32_t rel;
        memcpy(&rel, entry + 1, 4);
        port = at + 5 + uint32_t(rel);
    }
    HookOne(at, to);
    if (port != 0 && port != (uint32_t)(uintptr_t)to)
        HookOne(port, to);
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

// ---- the fakes: each logs its call and answers from the script

struct Script {
    SND::StreamStatus status;
    SND::RequestStatus request;
    int queueResult;
    int tick;
    double mixVolume;
    bool exists;
    bool bigAdded;
    int created;
};
Script g_script;
std::string g_log;

const char *Text(const char *s) {
    return s != NULL ? s : "(null)";
}

void FakeEnterCritical() {
    SNDI_mutexlock();
    g_log += "enter ";
}
void FakeLeaveCritical() {
    g_log += "leave ";
    SNDI_mutexunlock();
}
int FakeVol(int stream, int vol) {
    Append(g_log, "vol(%d,%d) ", stream, vol);
    return 0;
}
int FakePitchmult(int stream, int mult) {
    Append(g_log, "pitch(%d,%d) ", stream, mult);
    return 0;
}
int FakeLowpass(int stream, int cutoff) {
    Append(g_log, "lowpass(%d,%d) ", stream, cutoff);
    return 0;
}
int Fake3dpos(int stream, int azimuth, int elevation) {
    Append(g_log, "3dpos(%d,%d,%d) ", stream, azimuth, elevation);
    return 0;
}
int FakeAutovol(int stream, int time, int vol) {
    Append(g_log, "autovol(%d,%d,%d) ", stream, time, vol);
    return 0;
}
int FakePurge(int stream) {
    Append(g_log, "purge(%d) ", stream);
    return 0;
}
int FakeModifyhold(int id, int hold) {
    Append(g_log, "modifyhold(%d,%d) ", id, hold);
    return 0;
}
int FakeStatus(int stream, SND::StreamStatus *status) {
    Append(g_log, "status(%d) ", stream);
    *status = g_script.status;
    return 0;
}
int FakeRequeststatus(int id, SND::RequestStatus *status) {
    Append(g_log, "requeststatus(%d) ", id);
    *status = g_script.request;
    return 0;
}
int FakeQueuefile(int stream, int hold, const char *name, uint32_t offset) {
    Append(g_log, "queuefile(%d,%d,\"%s\",%u) ", stream, hold, Text(name), offset);
    return g_script.queueResult;
}
int FakeCreate(SND::PlayOpts *opts, int requests, int packets, void *, int size) {
    // pad06 is never written (SNDplaysetdef 0x0013c5c0, AStreamPriv's constructor 0x00122190): the caller's stack
    SND::PlayOpts copy = *opts;
    memset(copy.pad06, 0, sizeof(copy.pad06));
    Append(g_log, "create(%d,%d,%d,", requests, packets, size);
    AppendBytes(g_log, &copy, sizeof(copy));
    g_log += ") ";
    return g_script.created;
}
int FakeDestroy(int stream) {
    Append(g_log, "destroy(%d) ", stream);
    return 0;
}
int FakeGettick() {
    if (GetCurrentThreadId() != g_mainThread)
        return g_script.tick;
    g_script.tick += 7;
    Append(g_log, "tick=%d ", g_script.tick);
    return g_script.tick;
}
double __fastcall FakeGetVolume(AMix *mix, int) {
    Append(g_log, "getvolume(%s) ", mix->name);
    return g_script.mixVolume;
}
int FakePrintf(const char *format, ...) {
    Append(g_log, "printf(%s", Text(format));
    if (format != NULL && strncmp(format, "event ", 6) == 0) {
        va_list arguments;
        va_start(arguments, format);
        const char *name = va_arg(arguments, const char *);
        double fade = va_arg(arguments, double);
        const char *kind = va_arg(arguments, const char *);
        va_end(arguments);
        Append(g_log, ",\"%s\",%016llx,\"%s\"", Text(name), std::bit_cast<unsigned long long>(fade), Text(kind));
    }
    g_log += ") ";
    return 0;
}
bool FakeExists(const char *name, int priority) {
    Append(g_log, "exists(\"%s\",%d) ", Text(name), priority);
    return g_script.exists;
}
bool FakeAddBig(const char *name, int flags, int priority, int *id) {
    Append(g_log, "addbig(\"%s\",%d,%d) ", Text(name), flags, priority);
    *id = 0x77;
    return g_script.bigAdded;
}
bool FakeDelBig(int id, int priority) {
    Append(g_log, "delbig(%d,%d) ", id, priority);
    return true;
}

void HooksInstall() {
    HookInstall(0x0013b950, (const void *)&FakeEnterCritical);
    HookInstall(0x0013b970, (const void *)&FakeLeaveCritical);
    HookInstall(0x0013c8c0, (const void *)&FakeVol);
    HookInstall(0x0013c880, (const void *)&FakePitchmult);
    HookInstall(0x0013c840, (const void *)&FakeLowpass);
    HookInstall(0x0013c800, (const void *)&Fake3dpos);
    HookInstall(0x0013b990, (const void *)&FakeAutovol);
    HookInstall(0x0013c180, (const void *)&FakePurge);
    HookInstall(0x0013c590, (const void *)&FakeModifyhold);
    HookInstall(0x0013c740, (const void *)&FakeStatus);
    HookInstall(0x0013c660, (const void *)&FakeRequeststatus);
    HookInstall(0x0013c160, (const void *)&FakeQueuefile);
    HookInstall(0x0013c560, (const void *)&FakeCreate);
    HookInstall(0x0013c280, (const void *)&FakeDestroy);
    HookInstall(0x0010a6c0, (const void *)&FakeGettick);
    HookInstall(0x0011ca10, (const void *)&FakeGetVolume);
    HookInstall(0x00132192, (const void *)&FakePrintf);
    HookInstall(0x0010b870, (const void *)&FakeExists);
    HookInstall(0x0010b7d0, (const void *)&FakeAddBig);
    HookInstall(0x0010b830, (const void *)&FakeDelBig);
}

// =============================================================================================================
// AStreamEntry's constructor
// =============================================================================================================

const char *const kNames[] = {
    "M01_Intro", "music/level1", "SPEECH\\Q_Brief", "", "a", "Z", "nis/Out/END01", "x_y-z.9", "ALLCAPS/NAME",
};

void RandomName(char *out, size_t room) {
    if (Random(3) == 0) {
        strcpy(out, kNames[Random(sizeof(kNames) / sizeof(kNames[0]))]);
        return;
    }
    const char alphabet[] = "abcXYZ019_/\\.-AMZamz";
    size_t length = Random(uint32_t(room < 40 ? room - 1 : 40));
    for (size_t i = 0; i < length; i++)
        out[i] = alphabet[Random(sizeof(alphabet) - 1)];
    out[length] = '\0';
}

void TestEntries() {
    for (int i = 0; i < 200; i++, g_cases++) {
        char name[0x38];
        RandomName(name, sizeof(name));
        float fade = Random(4) == 0 ? -1.0f : Uniform(0.0f, 8.0f);
        bool loop = Random(2) != 0, effect = Random(2) != 0, held = Random(2) != 0;
        int tick = int(Random(1u << 30));
        AStreamEntry original, port;
        memset(&original, 0xcd, sizeof(original));
        memset(&port, 0xcd, sizeof(port));
        AStreamEntry *a = NULL;
        AStreamEntry *b = NULL;
        {
            Originals scope;
            Guarded([&] { a = Orig_EntryConstruct(&original, 0, name, fade, loop, effect, held, tick); });
        }
        Guarded([&] { b = port.Construct(name, fade, loop, effect, held, tick); });
        Check(memcmp(&original, &port, sizeof(original)) == 0, "AStreamEntry::Construct", i);
        Check((a == &original) == (b == &port), "AStreamEntry::Construct's answer", i);
    }
}

// =============================================================================================================
// The event queue
// =============================================================================================================

// An entry without the bytes Event's original leaves as its stack had them: the name's tail past its terminator
// and unknown3f (the port writes zero there)
void DumpEntry(std::string &log, const AStreamEntry *entry) {
    Append(log, "\"%.*s\" ", int(sizeof(entry->name)), entry->name);
    AppendBytes(log, &entry->fadeTime, offsetof(AStreamEntry, unknown3f) - offsetof(AStreamEntry, fadeTime));
    AppendBytes(log, &entry->eventTick, sizeof(entry->eventTick));
}

void DumpQueue(std::string &log, const AStreamQueue &queue) {
    Append(log, "size %u offset %u map %u [", queue.size, queue.offset, queue.mapSize);
    for (uint32_t i = 0; i < queue.mapSize; i++) {
        if (queue.map[i] == NULL)
            log += "- ";
        else
            DumpEntry(log, queue.map[i]);
    }
    log += "] ";
}

void RandomEntry(AStreamEntry *entry) {
    memset(entry, 0, sizeof(*entry));
    RandomName(entry->name, sizeof(entry->name));
    entry->fadeTime = Random(3) == 0 ? -1.0f : Uniform(0.0f, 4.0f);
    entry->loop = uint8_t(Random(2));
    entry->effect = uint8_t(Random(2));
    entry->held = uint8_t(Random(2));
    entry->unknown3f = uint8_t(Random(256));
    entry->eventTick = int(Random(100000));
}

void TestQueue() {
    for (int sequence = 0; sequence < 24; sequence++) {
        AStreamQueue original, port;
        memset(&original, 0, sizeof(original));
        memset(&port, 0, sizeof(port));
        for (int step = 0; step < 80; step++, g_cases++) {
            uint32_t op = Random(10);
            AStreamEntry entry;
            RandomEntry(&entry);
            bool clear = Random(4) == 0;
            uint32_t count = Random(3) == 0 ? Random(40) : 1 + Random(3);
            std::string a, b;
            const AStreamEntry *frontA = NULL;
            const AStreamEntry *frontB = NULL;
            {
                Originals scope;
                Guarded([&] {
                    if (op < 5)
                        Orig_QueuePushBack(&original, 0, &entry);
                    else if (op < 8)
                        Orig_QueuePopFront(&original, 0);
                    else if (op == 8 && original.size != 0)
                        frontA = Orig_QueueFront(&original, 0);
                    else if (op == 9 && clear)
                        Orig_QueueClear(&original, 0);
                    else if (op == 9)
                        Orig_QueueGrowMap(&original, 0, count);
                });
            }
            Guarded([&] {
                if (op < 5)
                    port.PushBack(entry);
                else if (op < 8)
                    port.PopFront();
                else if (op == 8 && port.size != 0)
                    frontB = port.Front();
                else if (op == 9 && clear)
                    port.Clear();
                else if (op == 9)
                    port.GrowMap(count);
            });
            DumpQueue(a, original);
            DumpQueue(b, port);
            if (frontA != NULL || frontB != NULL) {
                AppendBytes(a, frontA, frontA != NULL ? sizeof(AStreamEntry) : 0);
                AppendBytes(b, frontB, frontB != NULL ? sizeof(AStreamEntry) : 0);
            }
            Check(a == b, "the event queue", g_cases, &a, &b);
        }
        {
            Originals scope;
            Guarded([&] { Orig_QueueClear(&original, 0); });
        }
        Guarded([&] { port.Clear(); });
        std::string a, b;
        DumpQueue(a, original);
        DumpQueue(b, port);
        Check(a == b, "the event queue's clear", sequence, &a, &b);
    }

    // _Growmap alone, at every offset and size, with random counts
    for (int i = 0; i < 120; i++, g_cases++) {
        AStreamQueue original, port;
        memset(&original, 0, sizeof(original));
        memset(&port, 0, sizeof(port));
        uint32_t pushes = 1 + Random(30), pops = Random(pushes + 1), count = Random(2) ? 1 : Random(24);
        for (int side = 0; side < 2; side++) {
            AStreamQueue &q = side == 0 ? original : port;
            uint32_t seed = g_random;
            for (uint32_t k = 0; k < pushes; k++) {
                AStreamEntry entry;
                RandomEntry(&entry);
                q.PushBack(entry);
            }
            for (uint32_t k = 0; k < pops; k++)
                q.PopFront();
            if (side == 0)
                g_random = seed;
        }
        {
            Originals scope;
            Guarded([&] { Orig_QueueGrowMap(&original, 0, count); });
        }
        Guarded([&] { port.GrowMap(count); });
        std::string a, b;
        DumpQueue(a, original);
        DumpQueue(b, port);
        Check(a == b, "_Growmap", i, &a, &b);
        original.Clear();
        port.Clear();
    }
}

// =============================================================================================================
// The registry's tree
// =============================================================================================================

void TreeInit(StreamRefTree *tree) {
    memset(tree, 0, sizeof(*tree));
    tree->head = RefCounterMapBuyHead();
    tree->head->isNil = 1;
    tree->head->parent = tree->head;
    tree->head->left = tree->head;
    tree->head->right = tree->head;
    tree->size = 0;
}

void DumpNode(std::string &log, const RefCounterNode *node) {
    if (node->isNil) {
        log += ".";
        return;
    }
    Append(log, "(%s%c%d:%p ", node->value.name, node->color == kTreeBlack ? 'b' : 'r',
           node->value.entry.references, node->value.entry.object);
    DumpNode(log, node->left);
    DumpNode(log, node->right);
    log += ")";
}

void DumpTree(std::string &log, const StreamRefTree &tree) {
    if (tree.head == NULL) {
        Append(log, "no head, size %u", tree.size);
        return;
    }
    Append(log, "size %u head %d%d first %s last %s ", tree.size, tree.head->color, tree.head->isNil,
           tree.head->left == tree.head ? "-" : tree.head->left->value.name,
           tree.head->right == tree.head ? "-" : tree.head->right->value.name);
    DumpNode(log, tree.head->parent);
}

// The node at in-order position `index` (the head past the end)
RefCounterNode *NodeAt(const StreamRefTree &tree, uint32_t index) {
    RefCounterNode *node = tree.head->left;
    for (uint32_t i = 0; i < index && node != tree.head; i++) {
        if (!node->right->isNil) {
            node = node->right;
            while (!node->left->isNil)
                node = node->left;
        } else {
            RefCounterNode *parent = node->parent;
            while (!parent->isNil && node == parent->right) {
                node = parent;
                parent = parent->parent;
            }
            node = parent;
        }
    }
    return node;
}

uint32_t IndexOf(const StreamRefTree &tree, const RefCounterNode *node) {
    uint32_t index = 0;
    while (index <= tree.size && NodeAt(tree, index) != node)
        index++;
    return index;
}

const char *const kKeys[] = {
    "music", "MUSIC", "Music", "speech", "Speech", "nis", "NIS", "amb", "a", "A", "b", "zz", "ZZ", "m01", "M01", "_",
};

void TestTree() {
    for (int sequence = 0; sequence < 16; sequence++) {
        StreamRefTree original, port;
        TreeInit(&original);
        TreeInit(&port);
        for (int step = 0; step < 60; step++, g_cases++) {
            uint32_t op = Random(10);
            RefCounterValue value;
            memset(&value, 0, sizeof(value));
            if (Random(2))
                strcpy(value.name, kKeys[Random(sizeof(kKeys) / sizeof(kKeys[0]))]);
            else
                RandomName(value.name, 12);
            value.entry.references = int(Random(5));
            value.entry.object = (void *)(uintptr_t)(0x1000 + Random(0x1000) * 4);
            uint32_t first = Random(original.size + 1), last = first + Random(original.size + 1 - first);
            if (Random(8) == 0) {
                first = 0;
                last = original.size;
            }
            std::string a, b;
            {
                Originals scope;
                Guarded([&] {
                    if (op < 6) {
                        RefCounterInsertResult result = { NULL, false };
                        Orig_TreeInsertUnique(&original, 0, &result, &value);
                        Append(a, "insert %u %d ", IndexOf(original, result.node), result.inserted);
                    } else if (op < 9 && original.size != 0) {
                        RefCounterNode *next = NULL;
                        Orig_TreeEraseAt(&original, 0, &next, NodeAt(original, first % original.size));
                        Append(a, "erase %u ", IndexOf(original, next));
                    } else if (op == 9) {
                        RefCounterNode *next = NULL;
                        Orig_TreeEraseRange(&original, 0, &next, NodeAt(original, first), NodeAt(original, last));
                        Append(a, "range %u ", IndexOf(original, next));
                    }
                });
            }
            Guarded([&] {
                if (op < 6) {
                    RefCounterInsertResult result = { NULL, false };
                    port.InsertUnique(&result, &value);
                    Append(b, "insert %u %d ", IndexOf(port, result.node), result.inserted);
                } else if (op < 9 && port.size != 0) {
                    RefCounterNode *next = NULL;
                    port.EraseAt(&next, NodeAt(port, first % port.size));
                    Append(b, "erase %u ", IndexOf(port, next));
                } else if (op == 9) {
                    RefCounterNode *next = NULL;
                    port.EraseRange(&next, NodeAt(port, first), NodeAt(port, last));
                    Append(b, "range %u ", IndexOf(port, next));
                }
            });
            DumpTree(a, original);
            DumpTree(b, port);
            Check(a == b, "the registry's tree", g_cases, &a, &b);
        }
        bool range = sequence % 2 != 0;
        {
            Originals scope;
            Guarded([&] {
                if (range)
                    Orig_TreeDestroyRange(&original, 0);
                else
                    Orig_TreeDestruct(&original, 0);
            });
        }
        Guarded([&] {
            if (range)
                port.DestroyRange();
            else
                port.Destruct();
        });
        std::string a, b;
        DumpTree(a, original);
        DumpTree(b, port);
        Check(a == b, range ? "the tree's ~_Tree" : "the tree's destructor", sequence, &a, &b);
    }
}

// =============================================================================================================
// AStream on synthetic streams
// =============================================================================================================

constexpr size_t kMixOffset = 0x94;    // ABaseSound::mix: each side's own mix, compared as "its own or not"
constexpr size_t kPrivOffset = 0xc0;   // AStream::priv

struct alignas(16) StreamBytes {
    uint8_t bytes[sizeof(AStream)];
};
struct alignas(16) PrivBytes {
    uint8_t bytes[sizeof(AStreamPriv)];
};

// One synthetic stream: the AStream, its AStreamPriv and its mix, built from a seed
struct Synthetic {
    StreamBytes stream;
    PrivBytes priv;
    AMix mix;

    AStream *Stream() { return reinterpret_cast<AStream *>(stream.bytes); }
    AStreamPriv *Priv() { return reinterpret_cast<AStreamPriv *>(priv.bytes); }
};

struct Setup {
    uint32_t seed;
    Globals globals;
    Script script;
    ASoundPlayParams params;
};

const float kOdd[] = { 0.0f, 1.0f, -0.0f, 4.0f, 0.5f, -0.25f, 2.0f, 1.0e-6f };

float Perturbed(float lo, float hi) {
    return Random(5) == 0 ? kOdd[Random(sizeof(kOdd) / sizeof(kOdd[0]))] : Uniform(lo, hi);
}

void Build(Synthetic *s, uint32_t seed) {
    uint32_t saved = g_random;
    g_random = seed;
    for (size_t i = 0; i < sizeof(s->stream.bytes); i++)
        s->stream.bytes[i] = uint8_t(Random(256));
    memset(s->priv.bytes, 0, sizeof(s->priv.bytes));
    memset(&s->mix, 0, sizeof(s->mix));
    AStream *stream = s->Stream();
    AStreamPriv *priv = s->Priv();

    strcpy(s->mix.name, Random(3) == 0 ? "music" : (Random(2) ? "speech" : "MUSIC"));
    s->mix.unknown24 = Random(10);
    float sum = Uniform(0.0f, 3.0f);
    memcpy(&s->mix.unknown28, &sum, 4);
    s->mix.unknown2c = Perturbed(0.0f, 1.5f);

    stream->priv = priv;
    stream->mix = &s->mix;
    stream->volume = Perturbed(0.0f, 1.5f);
    stream->maxDistance = Random(3) == 0 ? 0.0f : Perturbed(-10.0f, 400.0f);
    stream->holdTimeout = int(Random(2) ? 10000 : Random(500));
    stream->heldTicks = int(Random(1000));
    stream->releaseTick = int(Random(1000));

    uint32_t pops = Random(4), pushes = Random(4);
    for (uint32_t k = 0; k < pushes + pops; k++) {
        AStreamEntry entry;
        RandomEntry(&entry);
        priv->queue.PushBack(entry);
    }
    for (uint32_t k = 0; k < pops; k++)
        priv->queue.PopFront();
    priv->status.requests = int(Random(4));
    priv->status.id = int(Random(1000));
    priv->status.bufferedMs = Random(5000);
    priv->request.state = int(Random(4));
    priv->request.playedMs = Random(60000);
    priv->request.remainingMs = Random(60000);
    priv->request.outstandingMs = Random(1000);
    if (Random(5) < 3) {
        priv->file = static_cast<char *>(OperatorNew(0x40));
        strcpy(priv->file, "|current.asf");
    }
    uint32_t filter = Random(6);
    priv->filter = filter == 0 ? 1.0f : filter == 1 ? -0.3f : filter == 2 ? 2.0f : Perturbed(-1.2f, 1.2f);
    priv->fade = Perturbed(0.0f, 3.0f);
    priv->volume = Perturbed(0.0f, 1.0f);
    priv->memorySize = Random(0x10000);
    uint32_t streamPick = Random(10);
    priv->stream = streamPick == 0 ? -1 : streamPick == 1 ? -2 : int(Random(16));
    priv->requestId = Random(5) < 2 ? -1 : int(Random(0x7ffff));
    priv->eventTick = int(Random(100000));
    priv->playTick = int(Random(100000));
    priv->bigAdded = int(Random(2));
    priv->bigId = int(Random(10));
    priv->over = uint8_t(Random(2));
    priv->notFound = uint8_t(Random(2));
    priv->paused = uint8_t(Random(2));
    priv->loop = uint8_t(Random(2));
    priv->effect = uint8_t(Random(2));
    priv->held = uint8_t(Random(3) == 0);
    priv->memory = (void *)0x12345678;
    strcpy(priv->filePrefix, "|");
    RandomName(priv->name, sizeof(priv->name));
    g_random = saved;
}

void Free(Synthetic *s) {
    AStreamPriv *priv = s->Priv();
    priv->queue.Clear();
    if (priv->file != NULL)
        OperatorDelete(priv->file);
    priv->file = NULL;
}

void DumpStream(std::string &log, Synthetic *s) {
    AStream *stream = s->Stream();
    AStreamPriv *priv = s->Priv();
    AppendBytes(log, s->stream.bytes, kMixOffset);
    Append(log, "mix %d ", stream->mix == &s->mix);
    AppendBytes(log, s->stream.bytes + kMixOffset + 4, kPrivOffset - kMixOffset - 4);
    AppendBytes(log, s->stream.bytes + kPrivOffset + 4, sizeof(AStream) - kPrivOffset - 4);
    Append(log, "priv %d ", stream->priv == priv);
    DumpQueue(log, priv->queue);
    AppendBytes(log, s->priv.bytes + offsetof(AStreamPriv, status), offsetof(AStreamPriv, file) - offsetof(AStreamPriv, status));
    Append(log, "file \"%s\" ", Text(priv->file));
    AppendBytes(log, s->priv.bytes + offsetof(AStreamPriv, filter), offsetof(AStreamPriv, memory) - offsetof(AStreamPriv, filter));
    Append(log, "memory %p ", priv->memory);
    AppendBytes(log, s->priv.bytes + offsetof(AStreamPriv, filePrefix), sizeof(AStreamPriv) - offsetof(AStreamPriv, filePrefix));
    AppendBytes(log, &s->mix, sizeof(s->mix));
    Globals g = GlobalsSave();
    Append(log, "globals %d %d %d %d ", g.frames, g.missionOver, g.hold, g.holdTick);
}

Setup RandomSetup() {
    Setup setup;
    setup.seed = Random(0xffffffffu) ^ (Random(0xffff) << 16);
    setup.globals.frames = int(Random(6));
    setup.globals.missionOver = uint8_t(Random(4) == 0);
    setup.globals.hold = int(Random(3));
    setup.globals.holdTick = int(Random(100000));
    memset(&setup.script, 0, sizeof(setup.script));
    setup.script.status.requests = Random(4) == 0 ? 0 : int(Random(4));
    setup.script.status.id = int(Random(1000));
    setup.script.status.bufferedMs = Random(5000);
    setup.script.request.state = int(Random(4));
    setup.script.request.playedMs = Random(60000);
    setup.script.request.remainingMs = Random(60000);
    setup.script.request.outstandingMs = Random(1000);
    setup.script.queueResult = Random(5) == 0 ? -int(Random(20)) - 1 : int(Random(0x10000));
    setup.script.tick = int(Random(100000));
    setup.script.mixVolume = Random(4) == 0 ? 1.0 : double(Uniform(0.0f, 1.3f)) / 3.0;
    setup.script.exists = Random(2) != 0;
    setup.script.bigAdded = Random(2) != 0;
    setup.script.created = int(Random(16));
    setup.params.volume = Perturbed(0.0f, 1.3f);
    setup.params.pitch = Perturbed(-0.5f, 5.0f);
    setup.params.azimuth = Perturbed(-1.0f, 1.0f);
    setup.params.unknown0c = 0.0f;
    setup.params.unknown10 = 0.0f;
    setup.params.unknown14 = 0;
    setup.params.listener = NULL;
    return setup;
}

enum Operation {
    kOpPlay,
    kOpNext,
    kOpStop,
    kOpDecodeError,
    kOpFadeOut,
    kOpEvent,
    kOpQueries,
    kOpCount,
};

const char *const kOperationNames[kOpCount] = {
    "AStream::Play", "AStream::Next", "AStream::Stop", "AStream::DecodeError", "AStream::FadeOut",
    "AStream::Event", "AStream's queries",
};

// One operation on a synthetic stream: its answers, the call log and the state after
std::string RunOperation(int op, const Setup &setup, bool original, int extra) {
    Synthetic *s = new Synthetic;
    Build(s, setup.seed);
    GlobalsLoad(setup.globals);
    g_script = setup.script;
    g_log.clear();
    ASoundPlayParams params = setup.params;
    AStream *stream = s->Stream();
    std::string answers;
    char eventName[0x38];
    uint32_t saved = g_random;
    g_random = uint32_t(extra);
    RandomName(eventName, sizeof(eventName));
    float eventFade = Random(3) == 0 ? -1.0f : Uniform(0.0f, 5.0f);
    bool loop = Random(2) != 0, effect = Random(2) != 0, held = Random(3) == 0;
    float filter = Perturbed(-2.0f, 2.0f);
    g_random = saved;

    auto run = [&] {
        switch (op) {
        case kOpPlay:
            if (original)
                Orig_Play(stream, 0, &params);
            else
                stream->Play(&params);
            break;
        case kOpNext:
            if (original)
                Orig_Next(stream, 0);
            else
                stream->Next();
            break;
        case kOpStop:
            if (original)
                Orig_Stop(stream, 0);
            else
                stream->Stop();
            break;
        case kOpDecodeError:
            if (original)
                Orig_DecodeError(stream, 0, -extra % 20);
            else
                stream->DecodeError(-extra % 20);
            break;
        case kOpFadeOut:
            if (original)
                Orig_FadeOut(stream, 0);
            else
                stream->FadeOut();
            break;
        case kOpEvent:
            if (original)
                Orig_Event(stream, 0, eventName, eventFade, loop, effect, held);
            else
                stream->Event(eventName, eventFade, loop, effect, held);
            break;
        case kOpQueries: {
            double latency = original ? Orig_GetLatency(stream, 0) : stream->GetLatency();
            float volume = original ? Orig_GetInternalVolume(stream, 0) : stream->GetInternalVolume();
            const char *file = original ? Orig_GetFile(stream, 0) : stream->GetFile();
            bool flags[5];
            flags[0] = original ? Orig_IsOver(stream, 0) : stream->IsOver();
            flags[1] = original ? Orig_IsNotFound(stream, 0) : stream->IsNotFound();
            flags[2] = original ? Orig_IsPaused(stream, 0) : stream->IsPaused();
            flags[3] = original ? Orig_IsLoop(stream, 0) : stream->IsLoop();
            flags[4] = original ? Orig_IsEffect(stream, 0) : stream->IsEffect();
            Append(answers, "latency %016llx volume %08x file \"%s\" flags %d%d%d%d%d ",
                   std::bit_cast<unsigned long long>(latency), Bits(volume), Text(file), flags[0], flags[1],
                   flags[2], flags[3], flags[4]);
            if (original)
                Orig_SetFilter(stream, 0, filter);
            else
                stream->SetFilter(filter);
            break;
        }
        }
    };
    bool ok;
    if (original) {
        Originals scope;
        ok = Guarded(run);
    } else {
        ok = Guarded(run);
    }
    std::string result = answers;
    Append(result, "%s| ", ok ? "" : "FAULT");
    result += g_log;
    result += "| ";
    DumpStream(result, s);
    Free(s);
    delete s;
    return result;
}

void TestStreams() {
    for (int i = 0; i < 1400; i++, g_cases++) {
        int op = i < 700 ? kOpPlay : int(Random(kOpCount));
        Setup setup = RandomSetup();
        int extra = int(Random(0x7fffffff));
        Globals live = GlobalsSave();
        std::string a = RunOperation(op, setup, true, extra);
        std::string b = RunOperation(op, setup, false, extra);
        GlobalsLoad(live);
        Check(a == b, kOperationNames[op], i, &a, &b);
    }
}

// ---- AStreamPriv's constructor and destructor

void DumpPriv(std::string &log, AStreamPriv *priv) {
    DumpQueue(log, priv->queue);
    PrivBytes copy;
    memcpy(copy.bytes, priv, sizeof(copy.bytes));
    AStreamPriv *c = reinterpret_cast<AStreamPriv *>(copy.bytes);
    c->queue.map = NULL;
    c->memory = c->memory != NULL ? (void *)1 : NULL;
    AppendBytes(log, copy.bytes, sizeof(copy.bytes));
}

void TestPriv() {
    void *liveSystem = ShadowSystem;
    for (int i = 0; i < 60; i++, g_cases++) {
        Setup setup = RandomSetup();
        char file[0x20];
        RandomName(file, 16);
        const char *extension = Random(3) == 0 ? "" : (Random(2) ? "mus" : "spe");
        unsigned int bufferSize = 0x400 + Random(0x2000);
        bool system = Random(4) != 0;
        uint32_t pushes = Random(4);
        std::string logs[2];
        for (int side = 0; side < 2; side++) {
            bool original = side == 0;
            PrivBytes *bytes = new PrivBytes;
            memset(bytes->bytes, 0xcd, sizeof(bytes->bytes));
            AStreamPriv *priv = reinterpret_cast<AStreamPriv *>(bytes->bytes);
            g_script = setup.script;
            g_log.clear();
            ShadowSystem = system ? liveSystem : NULL;
            AStreamPriv *answer = NULL;
            bool ok;
            if (original) {
                Originals scope;
                ok = Guarded([&] { answer = Orig_PrivConstruct(priv, 0, file, extension, bufferSize); });
            } else {
                ok = Guarded([&] { answer = priv->Construct(file, extension, bufferSize); });
            }
            ShadowSystem = liveSystem;
            std::string &log = logs[side];
            Append(log, "%s answer %d | ", ok ? "" : "FAULT", answer == priv);
            log += g_log;
            log += "| ";
            DumpPriv(log, priv);
            if (!ok) {
                delete bytes;
                continue;
            }
            uint32_t seed = g_random;
            for (uint32_t k = 0; k < pushes; k++) {
                AStreamEntry entry;
                RandomEntry(&entry);
                priv->queue.PushBack(entry);
            }
            g_random = seed;
            g_log.clear();
            if (original) {
                Originals scope;
                ok = Guarded([&] { Orig_PrivDestruct(priv, 0); });
            } else {
                ok = Guarded([&] { priv->Destruct(); });
            }
            Append(log, " || %s", ok ? "" : "FAULT ");
            log += g_log;
            log += "| ";
            DumpQueue(log, priv->queue);
            delete bytes;
        }
        Check(logs[0] == logs[1], "AStreamPriv's constructor and destructor", i, &logs[0], &logs[1]);
    }
}

}  // namespace

void AudioStreamShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_AUDIOSTREAMSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    static bool ran;
    if (ran)
        return;
    ran = true;
    FpControlGet(&g_x87, &g_sse);
    g_mainThread = GetCurrentThreadId();

    TestEntries();
    TestQueue();
    TestTree();
    HooksInstall();
    TestStreams();
    TestPriv();
    HooksRemove();

    printf("[audiostream] AStream, AStreamPriv, the event queue and the registry's tree: %d cases, %d checks, %d differ "
           "(%d calls faulted)\n", g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
