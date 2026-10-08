#include "SndCallTrace.h"

#include "../sound/snd/System.h"
#include "../sound/snd/Voices.h"
#include "../sound/snd/Banks.h"
#include "../sound/snd/Streams.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_SNDTRACE=1 logs every call the game makes into the sound library's API (docs/driving/sound.md 6.1):
// the SND* entries engine.audio calls, one line each, so that the sound side of two lockstep runs - a baseline
// and a port of the audio framework - can be compared with a plain diff, as the frame dumps compare the picture.
//
//     NIGHTFIRE_SNDTRACE_FILE=path        the log (default sndtrace.log); other threads' calls go to path.others
//     NIGHTFIRE_SNDTRACE_TICKS=first,last only those ticks (-1 is everything before the player's car appears)
//     NIGHTFIRE_SNDTRACE_TIMING=1         also the values the sound thread's timing decides (masked as ~)
//
// A line is
//
//     <tick> <function>(<arguments>) = <result>[ -> <what it wrote back>]
//
// with the tick counted from the first one the player's car exists in ("-" before), integers in hex, voice
// handles as v<n> - the voice the n-th successful SNDBANK_play started - and parameter blocks as their fields.
// Heap addresses are left out (the game's memory and the stream buffers are "mem").
//
// Which calls: the entries code outside the library calls (the AStream/AVoice/ABank/AFX/ASystem callers in the
// listing), plus the exit hook. Only the outermost call on a thread is logged: the library's calls to its own API
// (SNDSTRM_vol to SNDvol, SNDbankremove to SNDstop) are its business. SYNCTASK's SNDREAL_systemtask runs as often
// as the main loop's task scheduler does, so it is passed through unlogged, and what it calls with it.
//
// Threads: only the game thread's calls follow the simulation under NIGHTFIRE_LOCKSTEP. The game thread is the
// one ticks run on (the first to call the library until the first tick); every other thread's outermost calls -
// the 100 Hz server's SNDstop on a finished fade - go to path.others with their thread, the game tick they fell
// in, and every value shown, and are counted per entry.
//
// Timing: whether a voice has finished, and how far a stream has played, is the sound thread's doing. SNDover,
// the voice controls' results (the voice index, or an error once the server has reclaimed it) and the stream
// status blocks are masked unless NIGHTFIRE_SNDTRACE_TIMING=1. Voice handles carry the slot the voice landed in,
// which depends on what the server has freed, hence the v<n> numbering. Game logic that waits on a stream's end
// (sound.md 6.1) still makes a real difference between runs, which the log then shows.
//
// How: both ways into a port are moved to a wrapper that logs and calls the port. The original's entry, which
// the injection table made a jump to our port, is pointed at the wrapper (XbeOriginal_Redirect) - the game's own
// code and the stored addresses (SYNCTASK, the exit hooks) come in there. Our code calls the port directly, so
// every call or jump instruction in this module whose target is a port is retargeted to the wrapper; the
// wrappers themselves reach the port through a pointer, never by a direct call. Both happen at injection time,
// before any game thread exists.
// ---------------------------------------------------------------------------------------------------------------

namespace {

enum Api {
    kSysVectorToReal, kSysInit, kSysGetOpts, kSysSetOpts, kSysRestore, kSysExitHook, kSystemTask,
    kEnterCritical, kLeaveCritical,
    kBankAdd, kBankHeaderSize, kBankHeaderCopy, kBankRemove, kBankPatPresent, kBankPlay,
    kPlaySetDef, kStop, kVol, k3dPos, kPitchMult, kFxLevel, kOver, kFxInitBus, kFxMasterLevel,
    kStrmOverhead, kStrmCreate, kStrmDestroy, kStrmQueueFile, kStrmPurge, kStrmModifyHold, kStrmStatus,
    kStrmRequestStatus, kStrmVol, kStrmPitchMult, kStrmLowpass, kStrm3dPos, kStrmAutoVol,
    kApiCount
};

struct Entry {
    Api api;
    const char *name;
    uint32_t original;
    const void *wrapper;
    const void *port;
    int sites;           // instructions in our code retargeted from the port to the wrapper
    LONG otherCalls;     // outermost calls from threads other than the game's
    bool folded;         // the linker made another entry's port the same function: call sites left alone
};

extern Entry g_entries[kApiCount];

// The ports, as the wrappers call them: read at run time, so that no wrapper holds a direct call to a port.
const void *volatile g_ports[kApiCount];

template <class F> F Port(F, Api api) {
    return reinterpret_cast<F>(const_cast<void *>(g_ports[api]));
}

bool g_showTiming;
bool g_windowed;
int g_firstTick = INT_MIN;
int g_lastTick = INT_MAX;
int volatile g_tick = -1;              // ticks since the player's car appeared; -1 before
DWORD volatile g_gameThread;
FILE *g_log;
FILE *g_others;
CRITICAL_SECTION g_othersLock;
std::string g_pending;                 // the game thread's lines since the last tick
std::unordered_map<int, unsigned> g_voices;   // voice handle -> n of the SNDBANK_play that returned it
unsigned g_voiceCount;
thread_local int t_depth;              // entries in progress on this thread

#define PLAYER_CAR_HANDLE 0x00234e40   // -> -> the player's PBondCar

bool PlayerCarExists() {
    uint8_t **handle = *(uint8_t ***)PLAYER_CAR_HANDLE;
    return handle != NULL && *handle != NULL;
}

void Flush() {
    if (!g_pending.empty()) {
        fwrite(g_pending.data(), 1, g_pending.size(), g_log);
        g_pending.clear();
    }
    fflush(g_log);
}

uint32_t Fnv(const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

// One call into the library: whether it is the outermost on its thread, and the line it logs. Every method is a
// no-op unless the line is being written, so a wrapper reads as the call it logs.
class Call {
public:
    explicit Call(Api api);
    ~Call();

    bool top;            // not inside another sound entry on this thread
    bool game;           // on the game thread
    bool write;

    void Int(int value);
    void Hex(uint32_t value);
    void Voice(int handle);
    void Text(const char *text);
    void Name(const char *name);
    void Opts(const SND::PlayOpts *opts);
    void Bytes(const void *data, size_t size);
    void Return(int result);
    void ReturnTiming(int result);
    void ReturnVoice(int result);
    void Output();       // " -> ", what follows is written back
    bool Masked();       // a timing value is hidden: "~" written, true

private:
    void Put(const char *format, ...);
    void PutInt(int value);
    void Next();

    char text_[2048];
    int length_;
    int args_;
    bool closed_;
};

Call::Call(Api api) : top(t_depth++ == 0), game(false), write(false), length_(0), args_(0), closed_(false) {
    if (!top)
        return;
    DWORD thread = GetCurrentThreadId();
    if (g_gameThread == 0)
        InterlockedCompareExchange((LONG volatile *)&g_gameThread, (LONG)thread, 0);
    game = thread == g_gameThread;
    if (game) {
        write = !g_windowed || (g_tick >= g_firstTick && g_tick <= g_lastTick);
        if (!write)
            return;
        if (g_tick < 0)
            Put("- ");
        else
            Put("%d ", g_tick);
    } else {
        InterlockedIncrement(&g_entries[api].otherCalls);
        write = g_others != NULL;
        if (!write)
            return;
        Put("~%d t%lu ", g_tick, thread);
    }
    Put("%s(", g_entries[api].name);
}

Call::~Call() {
    if (write) {
        if (!closed_)
            Put(")");
        text_[length_++] = '\n';
        if (game) {
            g_pending.append(text_, length_);
            if (g_pending.size() > (1u << 20))
                Flush();
        } else {
            EnterCriticalSection(&g_othersLock);
            fwrite(text_, 1, length_, g_others);
            fflush(g_others);
            LeaveCriticalSection(&g_othersLock);
        }
    }
    t_depth--;
}

void Call::Put(const char *format, ...) {
    const int room = int(sizeof(text_)) - 1 - length_;   // one byte kept for the newline
    if (room <= 0)
        return;
    va_list list;
    va_start(list, format);
    int n = vsnprintf(text_ + length_, room, format, list);
    va_end(list);
    if (n > 0)
        length_ += n < room ? n : room - 1;
}

void Call::PutInt(int value) {
    if (value < 0)
        Put("-0x%x", 0u - unsigned(value));
    else
        Put("0x%x", unsigned(value));
}

void Call::Next() {
    if (args_++ > 0)
        Put(", ");
}

void Call::Int(int value) {
    if (!write)
        return;
    Next();
    PutInt(value);
}

void Call::Hex(uint32_t value) {
    if (!write)
        return;
    Next();
    Put("0x%x", value);
}

void Call::Voice(int handle) {
    if (!write)
        return;
    Next();
    if (game) {
        auto found = g_voices.find(handle);
        if (found != g_voices.end()) {
            Put("v%u", found->second);
            return;
        }
    }
    Put("h");
    PutInt(handle);
}

void Call::Text(const char *text) {
    if (!write)
        return;
    Next();
    if (text == NULL) {
        Put("null");
        return;
    }
    Put("\"");
    for (const char *c = text; *c != 0 && length_ < int(sizeof(text_)) - 8; c++) {
        if (*c >= 0x20 && *c < 0x7f && *c != '"' && *c != '\\')
            Put("%c", *c);
        else
            Put("\\x%02x", (uint8_t)*c);
    }
    Put("\"");
}

void Call::Name(const char *name) {
    if (!write)
        return;
    Next();
    Put("%s", name);
}

void Call::Opts(const SND::PlayOpts *opts) {
    if (!write)
        return;
    Next();
    if (opts == NULL) {
        Put("null");
        return;
    }
    Put("{vol ");
    PutInt(opts->vol);
    Put(" bend ");
    PutInt(opts->bend);
    Put(" key ");
    PutInt(opts->key);
    Put(" vel ");
    PutInt(opts->velocity);
    Put(" prog 0x%x fx ", opts->progVol);
    PutInt(opts->fxLevel);
    Put(" az 0x%x el 0x%x pitch 0x%x time 0x%x tempo 0x%x distort 0x%x lp 0x%x hp 0x%x}", opts->azimuth,
        opts->elevation, opts->pitchMult, opts->timeMult, opts->tempoMult, opts->distort, opts->lowpass,
        opts->highpass);
}

void Call::Bytes(const void *data, size_t size) {
    if (!write)
        return;
    Next();
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < size; i++)
        Put("%02x", p[i]);
}

void Call::Return(int result) {
    if (!write)
        return;
    Put(") = ");
    PutInt(result);
    closed_ = true;
}

void Call::ReturnTiming(int result) {
    if (!write)
        return;
    Put(") = ");
    closed_ = true;
    if (!Masked())
        PutInt(result);
}

void Call::ReturnVoice(int result) {
    if (!write)
        return;
    Put(") = ");
    closed_ = true;
    if (game && result >= 0)
        Put("v%u", g_voices[result]);
    else
        PutInt(result);
}

void Call::Output() {
    if (!write)
        return;
    if (!closed_)
        Put(")");
    closed_ = true;
    Put(" -> ");
    args_ = 0;
}

bool Call::Masked() {
    if (!write || !game || g_showTiming)
        return false;
    Put("~");
    return true;
}

// ---- the wrappers, one per entry, in the order of sound.md 6.1

int Traced_SNDSYS_vectortoreal(void) {
    Call call(kSysVectorToReal);
    int result = Port(&SNDSYS_vectortoreal, kSysVectorToReal)();
    call.Return(result);
    return result;
}

int Traced_SNDSYSI_init(void *memory, int size) {
    Call call(kSysInit);
    call.Name("mem");
    call.Int(size);
    int result = Port(&SNDSYSI_init, kSysInit)(memory, size);
    call.Return(result);
    return result;
}

// The caps and settings; the vectors are code addresses, ours or the original's
int Traced_SNDSYS_getopts(SND::SysOpts *opts) {
    Call call(kSysGetOpts);
    call.Name("out");
    int result = Port(&SNDSYS_getopts, kSysGetOpts)(opts);
    call.Return(result);
    call.Output();
    call.Bytes(opts, offsetof(SND::SysOpts, vectors));
    return result;
}

int Traced_SNDSYS_setops(const SND::SysOpts *opts) {
    Call call(kSysSetOpts);
    call.Bytes(opts, offsetof(SND::SysOpts, vectors));
    int result = Port(&SNDSYS_setops, kSysSetOpts)(opts);
    call.Return(result);
    return result;
}

int Traced_SNDSYS_restore(void) {
    Call call(kSysRestore);
    int result = Port(&SNDSYS_restore, kSysRestore)();
    call.Return(result);
    return result;
}

int Traced_SNDSYSI_exithook(void) {
    Call call(kSysExitHook);
    int result = Port(&SNDSYSI_exithook, kSysExitHook)();
    call.Return(result);
    return result;
}

// Not logged: what it calls is the library's own work, on the task scheduler's timing
int Traced_SNDREAL_systemtask(int argument, int ticksLate) {
    t_depth++;
    int result = Port(&SNDREAL_systemtask, kSystemTask)(argument, ticksLate);
    t_depth--;
    return result;
}

void Traced_SNDSYS_entercritical(void) {
    Call call(kEnterCritical);
    Port(&SNDSYS_entercritical, kEnterCritical)();
}

void Traced_SNDSYS_leavecritical(void) {
    Call call(kLeaveCritical);
    Port(&SNDSYS_leavecritical, kLeaveCritical)();
}

// The bank's file header and a hash of its header part; the memory's address is the game heap's
int Traced_SNDbankadd(int *bank, SND::BankHeader *data) {
    Call call(kBankAdd);
    call.Name("out");
    if (call.write) {
        uint32_t hashed = data->headerSize < (16u << 20) ? data->headerSize : 0;
        char fields[160];
        snprintf(fields, sizeof(fields), "{%.4s v0x%x n0x%x hs0x%x xs0x%x total0x%x fnv0x%08x}", data->magic,
                 data->version, data->patchCount, data->headerSize, data->extraSize, data->total,
                 Fnv(data, hashed));
        call.Name(fields);
    }
    int result = Port(&SNDbankadd, kBankAdd)(bank, data);
    call.Return(result);
    call.Output();
    call.Int(*bank);
    return result;
}

int Traced_SNDbankheadersize(int bank) {
    Call call(kBankHeaderSize);
    call.Int(bank);
    int result = Port(&SNDbankheadersize, kBankHeaderSize)(bank);
    call.Return(result);
    return result;
}

// A hash of what was moved: the bank's header and extra parts, as SNDbankheadersize counts them
int Traced_SNDbankheadercopy(SND::BankHeader *destination, int bank) {
    Call call(kBankHeaderCopy);
    call.Name("mem");
    call.Int(bank);
    int result = Port(&SNDbankheadercopy, kBankHeaderCopy)(destination, bank);
    call.Return(result);
    if (call.write && result == 0) {
        call.Output();
        call.Hex(Fnv(destination, destination->headerSize + destination->extraSize));
    }
    return result;
}

int Traced_SNDbankremove(int bank) {
    Call call(kBankRemove);
    call.Int(bank);
    int result = Port(&SNDbankremove, kBankRemove)(bank);
    call.Return(result);
    return result;
}

int Traced_SNDbankpatpresent(int bank, int patch) {
    Call call(kBankPatPresent);
    call.Int(bank);
    call.Int(patch);
    int result = Port(&SNDbankpatpresent, kBankPatPresent)(bank, patch);
    call.Return(result);
    return result;
}

int Traced_SNDBANK_play(int bank, int patch, SND::PlayOpts *opts) {
    Call call(kBankPlay);
    call.Int(bank);
    call.Int(patch);
    call.Opts(opts);
    int result = Port(&SNDBANK_play, kBankPlay)(bank, patch, opts);
    if (call.top && call.game && result >= 0)
        g_voices[result] = ++g_voiceCount;
    call.ReturnVoice(result);
    return result;
}

int Traced_SNDplaysetdef(SND::PlayOpts *opts) {
    Call call(kPlaySetDef);
    call.Name("out");
    int result = Port(&SNDplaysetdef, kPlaySetDef)(opts);
    call.Return(result);
    call.Output();
    call.Opts(opts);
    return result;
}

int Traced_SNDstop(int handle) {
    Call call(kStop);
    call.Voice(handle);
    int result = Port(&SNDstop, kStop)(handle);
    call.ReturnTiming(result);
    return result;
}

int Traced_SNDvol(int handle, int vol) {
    Call call(kVol);
    call.Voice(handle);
    call.Int(vol);
    int result = Port(&SNDvol, kVol)(handle, vol);
    call.ReturnTiming(result);
    return result;
}

int Traced_SND3dpos(int handle, int azimuth, int elevation) {
    Call call(k3dPos);
    call.Voice(handle);
    call.Int(azimuth);
    call.Int(elevation);
    int result = Port(&SND3dpos, k3dPos)(handle, azimuth, elevation);
    call.ReturnTiming(result);
    return result;
}

int Traced_SNDpitchmult(int handle, int mult) {
    Call call(kPitchMult);
    call.Voice(handle);
    call.Int(mult);
    int result = Port(&SNDpitchmult, kPitchMult)(handle, mult);
    call.ReturnTiming(result);
    return result;
}

int Traced_SNDfxlevel(int handle, int bus, int level) {
    Call call(kFxLevel);
    call.Voice(handle);
    call.Int(bus);
    call.Int(level);
    int result = Port(&SNDfxlevel, kFxLevel)(handle, bus, level);
    call.ReturnTiming(result);
    return result;
}

int Traced_SNDover(int handle) {
    Call call(kOver);
    call.Voice(handle);
    int result = Port(&SNDover, kOver)(handle);
    call.ReturnTiming(result);
    return result;
}

int Traced_SNDfxinitbus(int bus, int level, int mode, int delay, int feedback) {
    Call call(kFxInitBus);
    call.Int(bus);
    call.Int(level);
    call.Int(mode);
    call.Int(delay);
    call.Int(feedback);
    int result = Port(&SNDfxinitbus, kFxInitBus)(bus, level, mode, delay, feedback);
    call.Return(result);
    return result;
}

int Traced_SNDfxmasterlevel(int bus, int level) {
    Call call(kFxMasterLevel);
    call.Int(bus);
    call.Int(level);
    int result = Port(&SNDfxmasterlevel, kFxMasterLevel)(bus, level);
    call.Return(result);
    return result;
}

int Traced_SNDSTRM_overhead(int requests, int packets) {
    Call call(kStrmOverhead);
    call.Int(requests);
    call.Int(packets);
    int result = Port(&SNDSTRM_overhead, kStrmOverhead)(requests, packets);
    call.Return(result);
    return result;
}

int Traced_SNDSTRM_create(SND::PlayOpts *opts, int requests, int packets, void *memory, int size) {
    Call call(kStrmCreate);
    call.Opts(opts);
    call.Int(requests);
    call.Int(packets);
    call.Name("mem");
    call.Int(size);
    int result = Port(&SNDSTRM_create, kStrmCreate)(opts, requests, packets, memory, size);
    call.Return(result);
    return result;
}

int Traced_SNDSTRM_destroy(int stream) {
    Call call(kStrmDestroy);
    call.Int(stream);
    int result = Port(&SNDSTRM_destroy, kStrmDestroy)(stream);
    call.Return(result);
    return result;
}

int Traced_SNDSTRM_queuefile(int stream, int hold, const char *name, uint32_t offset) {
    Call call(kStrmQueueFile);
    call.Int(stream);
    call.Int(hold);
    call.Text(name);
    call.Hex(offset);
    int result = Port(&SNDSTRM_queuefile, kStrmQueueFile)(stream, hold, name, offset);
    call.Return(result);
    return result;
}

int Traced_SNDSTRM_purge(int stream) {
    Call call(kStrmPurge);
    call.Int(stream);
    int result = Port(&SNDSTRM_purge, kStrmPurge)(stream);
    call.Return(result);
    return result;
}

int Traced_SNDSTRM_modifyhold(int id, int hold) {
    Call call(kStrmModifyHold);
    call.Int(id);
    call.Int(hold);
    int result = Port(&SNDSTRM_modifyhold, kStrmModifyHold)(id, hold);
    call.Return(result);
    return result;
}

int Traced_SNDSTRM_status(int stream, SND::StreamStatus *status) {
    Call call(kStrmStatus);
    call.Int(stream);
    call.Name("out");
    int result = Port(&SNDSTRM_status, kStrmStatus)(stream, status);
    call.Return(result);
    call.Output();
    if (!call.Masked()) {
        call.Int(status->requests);
        call.Int(status->id);
        call.Hex(status->bufferedMs);
    }
    return result;
}

int Traced_SNDSTRM_requeststatus(int id, SND::RequestStatus *status) {
    Call call(kStrmRequestStatus);
    call.Int(id);
    call.Name("out");
    int result = Port(&SNDSTRM_requeststatus, kStrmRequestStatus)(id, status);
    call.Return(result);
    call.Output();
    if (!call.Masked()) {
        call.Int(status->state);
        call.Hex(status->playedMs);
        call.Hex(status->remainingMs);
        call.Hex(status->outstandingMs);
    }
    return result;
}

int Traced_SNDSTRM_vol(int stream, int vol) {
    Call call(kStrmVol);
    call.Int(stream);
    call.Int(vol);
    int result = Port(&SNDSTRM_vol, kStrmVol)(stream, vol);
    call.Return(result);
    return result;
}

int Traced_SNDSTRM_pitchmult(int stream, int mult) {
    Call call(kStrmPitchMult);
    call.Int(stream);
    call.Int(mult);
    int result = Port(&SNDSTRM_pitchmult, kStrmPitchMult)(stream, mult);
    call.Return(result);
    return result;
}

int Traced_SNDSTRM_lowpass(int stream, int cutoff) {
    Call call(kStrmLowpass);
    call.Int(stream);
    call.Int(cutoff);
    int result = Port(&SNDSTRM_lowpass, kStrmLowpass)(stream, cutoff);
    call.Return(result);
    return result;
}

int Traced_SNDSTRM_3dpos(int stream, int azimuth, int elevation) {
    Call call(kStrm3dPos);
    call.Int(stream);
    call.Int(azimuth);
    call.Int(elevation);
    int result = Port(&SNDSTRM_3dpos, kStrm3dPos)(stream, azimuth, elevation);
    call.Return(result);
    return result;
}

int Traced_SNDSTRM_autovol(int stream, int time, int vol) {
    Call call(kStrmAutoVol);
    call.Int(stream);
    call.Int(time);
    call.Int(vol);
    int result = Port(&SNDSTRM_autovol, kStrmAutoVol)(stream, time, vol);
    call.Return(result);
    return result;
}

#define ENTRY(api, function, original) \
    { api, #function, original, (const void *)&Traced_##function, (const void *)&function, 0, 0, false }

Entry g_entries[kApiCount] = {
    ENTRY(kSysVectorToReal, SNDSYS_vectortoreal, 0x0013d3f0),
    ENTRY(kSysInit, SNDSYSI_init, 0x0013d140),
    ENTRY(kSysGetOpts, SNDSYS_getopts, 0x0013d090),
    ENTRY(kSysSetOpts, SNDSYS_setops, 0x0013d0f0),
    ENTRY(kSysRestore, SNDSYS_restore, 0x0013d320),
    ENTRY(kSysExitHook, SNDSYSI_exithook, 0x001413d0),
    ENTRY(kSystemTask, SNDREAL_systemtask, 0x0013d3e0),
    ENTRY(kEnterCritical, SNDSYS_entercritical, 0x0013b950),
    ENTRY(kLeaveCritical, SNDSYS_leavecritical, 0x0013b970),
    ENTRY(kBankAdd, SNDbankadd, 0x0013cef0),
    ENTRY(kBankHeaderSize, SNDbankheadersize, 0x0013ce60),
    ENTRY(kBankHeaderCopy, SNDbankheadercopy, 0x0013ce20),
    ENTRY(kBankRemove, SNDbankremove, 0x0013cf60),
    ENTRY(kBankPatPresent, SNDbankpatpresent, 0x0013cf20),
    ENTRY(kBankPlay, SNDBANK_play, 0x0013cc20),
    ENTRY(kPlaySetDef, SNDplaysetdef, 0x0013c5c0),
    ENTRY(kStop, SNDstop, 0x0013c900),
    ENTRY(kVol, SNDvol, 0x0013cb40),
    ENTRY(k3dPos, SND3dpos, 0x0013c9f0),
    ENTRY(kPitchMult, SNDpitchmult, 0x0013caa0),
    ENTRY(kFxLevel, SNDfxlevel, 0x0013c960),
    ENTRY(kOver, SNDover, 0x0013cc00),
    ENTRY(kFxInitBus, SNDfxinitbus, 0x0013cd50),
    ENTRY(kFxMasterLevel, SNDfxmasterlevel, 0x0013ccc0),
    ENTRY(kStrmOverhead, SNDSTRM_overhead, 0x0013c630),
    ENTRY(kStrmCreate, SNDSTRM_create, 0x0013c560),
    ENTRY(kStrmDestroy, SNDSTRM_destroy, 0x0013c280),
    ENTRY(kStrmQueueFile, SNDSTRM_queuefile, 0x0013c160),
    ENTRY(kStrmPurge, SNDSTRM_purge, 0x0013c180),
    ENTRY(kStrmModifyHold, SNDSTRM_modifyhold, 0x0013c590),
    ENTRY(kStrmStatus, SNDSTRM_status, 0x0013c740),
    ENTRY(kStrmRequestStatus, SNDSTRM_requeststatus, 0x0013c660),
    ENTRY(kStrmVol, SNDSTRM_vol, 0x0013c8c0),
    ENTRY(kStrmPitchMult, SNDSTRM_pitchmult, 0x0013c880),
    ENTRY(kStrmLowpass, SNDSTRM_lowpass, 0x0013c840),
    ENTRY(kStrm3dPos, SNDSTRM_3dpos, 0x0013c800),
    ENTRY(kStrmAutoVol, SNDSTRM_autovol, 0x0013b990),
};

#undef ENTRY

// ---- installing

Entry *EntryForPort(uintptr_t target) {
    for (Entry &e : g_entries)
        if ((uintptr_t)e.port == target)
            return &e;
    return NULL;
}

// Every rel32 call, jump or conditional jump in this module's code whose target is a port now goes to its
// wrapper. A byte pattern, not a disassembly: an E8/E9 inside another instruction would have to hold the exact
// displacement of one of these few addresses to be taken for one.
int MoveCallSites() {
    HMODULE module = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&SndCallTrace_Install, &module))
        return 0;
    uint8_t *base = (uint8_t *)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    int moved = 0;
    for (int s = 0; s < nt->FileHeader.NumberOfSections; s++, section++) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
            continue;
        uint8_t *code = base + section->VirtualAddress;
        size_t size = section->Misc.VirtualSize;
        DWORD old;
        if (!VirtualProtect(code, size, PAGE_EXECUTE_READWRITE, &old))
            continue;
        for (size_t i = 0; i + 6 <= size; i++) {
            size_t length;
            if (code[i] == 0xe8 || code[i] == 0xe9)
                length = 5;
            else if (code[i] == 0x0f && (code[i + 1] & 0xf0) == 0x80)
                length = 6;
            else
                continue;
            uint8_t *next = code + i + length;
            int32_t displacement;
            memcpy(&displacement, next - 4, 4);
            Entry *e = EntryForPort((uintptr_t)next + displacement);
            if (e == NULL || e->folded)
                continue;
            displacement = (int32_t)((uintptr_t)e->wrapper - (uintptr_t)next);
            memcpy(next - 4, &displacement, 4);
            e->sites++;
            moved++;
            i += length - 1;
        }
        VirtualProtect(code, size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), code, size);
    }
    return moved;
}

void Shutdown() {
    Flush();
    if (g_others == NULL)
        return;
    EnterCriticalSection(&g_othersLock);
    for (const Entry &e : g_entries)
        if (e.otherCalls != 0)
            fprintf(g_others, "# %s: %ld calls from other threads\n", e.name, e.otherCalls);
    fflush(g_others);
    LeaveCriticalSection(&g_othersLock);
}

bool EnvironmentFlag(const char *name) {
    char text[16];
    return GetEnvironmentVariableA(name, text, sizeof(text)) != 0 && text[0] != '0';
}

}  // namespace

void SndCallTrace_Install(void) {
    if (!EnvironmentFlag("NIGHTFIRE_SNDTRACE"))
        return;

    for (int i = 0; i < kApiCount; i++)
        if (g_entries[i].api != i) {
            printf("[sndtrace] entry %d is %s, out of order - not installed\n", i, g_entries[i].name);
            return;
        }
    // Identical functions folded into one by the linker share a port: their originals still have their own
    // wrappers, but a call site in our code to the port could be either, so it is left alone.
    for (Entry &e : g_entries) {
        Entry *first = EntryForPort((uintptr_t)e.port);
        if (first != &e) {
            first->folded = true;
            e.folded = true;
            printf("[sndtrace] %s and %s are one function in this build: our own calls to it are not traced\n",
                   first->name, e.name);
        }
    }

    char text[MAX_PATH];
    if (GetEnvironmentVariableA("NIGHTFIRE_SNDTRACE_TICKS", text, sizeof(text)) &&
        sscanf(text, " %d , %d", &g_firstTick, &g_lastTick) == 2)
        g_windowed = true;
    g_showTiming = EnvironmentFlag("NIGHTFIRE_SNDTRACE_TIMING");

    char path[MAX_PATH] = "sndtrace.log";
    GetEnvironmentVariableA("NIGHTFIRE_SNDTRACE_FILE", path, sizeof(path));
    g_log = fopen(path, "w");
    if (g_log == NULL) {
        printf("[sndtrace] could not open %s\n", path);
        return;
    }
    char othersPath[MAX_PATH + 8];
    snprintf(othersPath, sizeof(othersPath), "%s.others", path);
    g_others = fopen(othersPath, "w");
    InitializeCriticalSection(&g_othersLock);
    for (int i = 0; i < kApiCount; i++)
        g_ports[i] = g_entries[i].port;

    int originals = 0;
    for (Entry &e : g_entries) {
        const uint8_t *at = (const uint8_t *)(uintptr_t)e.original;
        int32_t displacement;
        memcpy(&displacement, at + 1, 4);
        if (at[0] == 0xe9 && e.original + 5 + (uint32_t)displacement == (uint32_t)(uintptr_t)e.port &&
            XbeOriginal_Redirect(e.original, e.wrapper))
            originals++;
        else
            printf("[sndtrace] 0x%08x %s does not jump to our port: the game's calls to it are not traced\n",
                   e.original, e.name);
    }
    // An untraced function of the library folded into a traced one would have its calls from our code logged
    // under the traced name: its original's jump shows it.
    for (uint32_t at = 0x0013b7b0; at < 0x0014bff0; at++) {
        const uint8_t *code = (const uint8_t *)(uintptr_t)at;
        if (code[0] != 0xe9)
            continue;
        int32_t displacement;
        memcpy(&displacement, code + 1, 4);
        const Entry *e = EntryForPort(at + 5 + (uint32_t)displacement);
        if (e != NULL && !e->folded && e->original != at)
            printf("[sndtrace] 0x%08x jumps to %s's port too: our code's calls to it are logged as %s\n", at,
                   e->name, e->name);
    }
    int sites = MoveCallSites();

    atexit(Shutdown);
    printf("[sndtrace] %d sound entries traced to %s (%d original entries, %d call sites in our code)%s\n",
           int(kApiCount), path, originals, sites, g_windowed ? ", within the ticks asked for" : "");
}

void SndCallTrace_Tick(void) {
    if (g_log == NULL)
        return;
    DWORD thread = GetCurrentThreadId();
    if (thread != g_gameThread) {
        if (g_gameThread != 0)
            printf("[sndtrace] ticks run on thread %lu, the first sound call came from %lu\n", thread,
                   g_gameThread);
        g_gameThread = thread;
    }
    Flush();
    if (g_tick >= 0 || PlayerCarExists())
        g_tick = g_tick + 1;
}
