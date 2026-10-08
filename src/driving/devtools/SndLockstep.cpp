#include "SndLockstep.h"

#include "../sound/snd/Platform.h"
#include "../sound/snd/Streams.h"
#include "../sound/snd/System.h"
#include "../platform/RealSystem.h"
#include "../../helpers.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#include <unordered_map>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_SNDLOCKSTEP=1, only with NIGHTFIRE_LOCKSTEP=1 (devtools/Teleport.cpp: one simulation tick per frame).
//
// Lockstep makes the simulation a function of its ticks, but the sound library runs on real time: its driver thread
// (SNDDRV_thread, docs/driving/sound.md 3.1) wakes every 10 ms of the host's clock, asks DirectSound whether each
// voice is still playing (answered by XAudio2 as the device plays) and mixes as far as the device has read; the
// main thread's service (SNDSTRMI_service) runs from SYNCTASK as often as the timer says; the FILESYS worker
// fills the stream rings as fast as the disc allows; and the audio framework reads TIMER_gettick. All of it
// reaches game logic (SNDover, stream status, voice slots), so two runs of one build part within a few ticks.
//
// This mode puts every one of those on the simulation's clock:
//
//  - The driver thread runs no steps of its own (SndLockstep_DriverThread replaces its loop). At the start of each
//    game loop that simulates (Scheduler.cpp's RunTick, before the schedules), SndLockstep_Tick hands it as many
//    10 ms steps as one timer tick lasts (1000 / the timer's rate ms: two at 50 Hz; at 60 Hz 1, 2, 2 by a
//    remainder carried over) and waits until it has run them. A step is the thread's own: SoundMutex, the frame
//    callbacks, SNDSYSI_100hzserver, dsndMixProcess, SNDPKTPLAYI_flushcallbackdata - on the driver thread, so
//    anything that tells threads apart sees what it did.
//  - The DirectSound seam answers GetStatus and GetCurrentPosition from playback simulated here, one 10 ms step at
//    a time before each driver step: a playing buffer advances by its frequency x 10 ms in sample frames (integers,
//    with the remainder kept), a one-shot stops at its data's end, a looping one wraps in its loop region. Stop
//    keeps the position and Play resumes from it, as the XAudio2 backend does. The device still plays what it is
//    given; it just no longer decides anything the game sees.
//  - SNDREAL_systemtask does nothing when SYNCTASK calls it; SndLockstep_Tick calls it once, after the steps,
//    through the original's address (so NIGHTFIRE_SNDTRACE's wrapper sees it as it would).
//  - A sound stream's FILESYS ops (the open, close and read whose callbacks are STREAM's) are waited for when
//    FILESYS_callbackop is asked to call back, and the callback - and the chain of ops it starts, read after read
//    until the ring is full - runs there on the caller's thread before the caller goes on, whatever the disc's
//    timing. No stream callback ever runs on the worker. (Nothing else uses FILESYS asynchronously once the game
//    loop runs, so the worker has nothing else in hand while the caller waits.)
//  - TIMER_gettick counts lockstep ticks from the first one: the real count then, plus one per tick, the same
//    value for everything that reads it during a tick. If the simulation stops ticking for over a second (a load,
//    a movie inside the loop) it follows the real clock again from there, so a wait on it cannot hang.
//
// Before the first tick (the loads) nothing on the sound side moves: no driver steps, no service, playback
// frozen, file ops already synchronous - so every run reaches the first tick with the same sound state.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the driver's globals (Platform.cpp, sound.md 2.6)
typedef void (*FrameCallback)(void);
#define IsRunning U8_AT(0x00244c3c)                    // SNDDRV_isRunning
#define KeepRunning U8_AT(0x00244c3d)                  // SNDDRV_shouldContinueRunning
#define PreFrameCallback (*(FrameCallback volatile *)0x00244fb8)    // SNDDRVPreFrameCb
#define PostFrameCallback (*(FrameCallback volatile *)0x00244fbc)   // SNDDRVPostFrameCb
#define SysTaskAdded U32_AT(0x00244c30)                // systaskadded: SNDREAL_systemtask is a SYNCTASK
#define RealTimerTicks (*(volatile int *)0x00242428u)  // what TIMER_gettick answers otherwise

// SNDREAL_systemtask by the original's address, as SYNCTASK holds it
#define SystemTask ((int (*)(int, int))0x0013d3e0)

// STREAM's FILESYS callbacks, as it hands them over (Stream.cpp)
#define StreamOpenDone ((FsCallback)0x0014aee0)
#define StreamCloseDone ((FsCallback)0x0014af10)
#define StreamReadDone ((FsCallback)0x0014b660)

constexpr int kStepMs = 10;
constexpr uint32_t kStatusPlaying = 1;             // DSBSTATUS_PLAYING
constexpr uint32_t kStatusLooping = 4;             // DSBSTATUS_LOOPING
constexpr uint16_t kFormatXboxAdpcm = 0x69;        // 64 frames to a block of 36 bytes a channel
constexpr DWORD kFileOpPatienceMs = 10000;

// ---- the mode's state
volatile LONG g_once;                  // 0 not read, 1 being read, 2 read
bool g_enabled;
CRITICAL_SECTION g_playbackLock;
HANDLE g_stepGranted;                  // one count per step handed to the driver thread
HANDLE g_stepDone;                     // one count per step it has run
volatile LONG g_driverAlive;

// The game thread's
int g_stepRemainder;                   // in steps x timer rate: a tick is 100 / rate steps
bool g_ownService;                     // SndLockstep_Tick is calling SNDREAL_systemtask
unsigned g_ticks;

// TIMER_gettick (any thread reads these)
volatile bool g_ticking;               // the first lockstep tick has happened
volatile int g_virtualTick;            // TIMER_gettick at the last lockstep tick
volatile int g_realAtTick;             // the real count then

void Init() {
    if (g_once == 2)
        return;
    if (InterlockedCompareExchange(&g_once, 1, 0) != 0) {
        while (g_once != 2)
            Sleep(0);
        return;
    }
    char text[16] = "";
    bool lockstep = GetEnvironmentVariableA("NIGHTFIRE_LOCKSTEP", text, sizeof(text)) != 0 && text[0] != '0';
    text[0] = 0;
    bool sound = GetEnvironmentVariableA("NIGHTFIRE_SNDLOCKSTEP", text, sizeof(text)) != 0 && text[0] != '0';
    if (sound && !lockstep)
        printf("[sndlockstep] NIGHTFIRE_SNDLOCKSTEP needs NIGHTFIRE_LOCKSTEP=1 - off\n");
    g_enabled = sound && lockstep;
    if (g_enabled) {
        InitializeCriticalSection(&g_playbackLock);
        g_stepGranted = CreateSemaphoreA(NULL, 0, 0x7fffffff, NULL);
        g_stepDone = CreateSemaphoreA(NULL, 0, 0x7fffffff, NULL);
        printf("[sndlockstep] the sound driver, stream reads and TIMER_gettick follow the simulation's ticks\n");
    }
    fflush(stdout);
    InterlockedExchange(&g_once, 2);
}

// ---- simulated playback

struct Playback {
    uint32_t sampleRate;
    uint16_t formatTag;
    uint16_t channels;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    uint32_t bytes;            // the data's
    uint32_t frequency;        // SetFrequency's; the sample rate until then
    uint32_t loopStart;        // bytes
    uint32_t loopLength;       // bytes, 0 for the whole buffer
    bool playing;
    bool looping;
    uint32_t frame;            // the play position in sample frames
    uint32_t thousandths;      // and the part of a frame beyond it
};

std::unordered_map<const IDirectSoundBuffer *, Playback> g_buffers;

uint32_t AdpcmBlockBytes(const Playback &p) {
    return p.blockAlign != 0 ? p.blockAlign : 36u * (p.channels != 0 ? p.channels : 1);
}

uint32_t FrameBytes(const Playback &p) {
    if (p.blockAlign != 0)
        return p.blockAlign;
    uint32_t bytes = (p.bitsPerSample != 0 ? p.bitsPerSample : 16) / 8 * (p.channels != 0 ? p.channels : 1);
    return bytes != 0 ? bytes : 1;
}

uint32_t FramesOf(const Playback &p, uint32_t bytes) {
    if (p.formatTag == kFormatXboxAdpcm)
        return bytes / AdpcmBlockBytes(p) * 64;
    return bytes / FrameBytes(p);
}

uint32_t BytesOf(const Playback &p, uint32_t frames) {
    if (p.formatTag == kFormatXboxAdpcm)
        return frames / 64 * AdpcmBlockBytes(p);
    return frames * FrameBytes(p);
}

// One step of playback for every playing buffer; the backend's frequency ratio is clamped to 4.
void AdvancePlayback() {
    EnterCriticalSection(&g_playbackLock);
    for (auto &entry : g_buffers) {
        Playback &p = entry.second;
        if (!p.playing)
            continue;
        uint32_t hz = p.frequency;
        if (hz > p.sampleRate * 4)
            hz = p.sampleRate * 4;
        uint32_t thousandths = p.thousandths + hz * kStepMs;
        p.frame += thousandths / 1000;
        p.thousandths = thousandths % 1000;
        uint32_t end = FramesOf(p, p.bytes);
        if (!p.looping) {
            if (p.frame >= end) {
                p.frame = end;
                p.thousandths = 0;
                p.playing = false;
            }
            continue;
        }
        uint32_t loopStart = 0;
        uint32_t loopEnd = end;
        if (p.loopLength != 0) {
            loopStart = FramesOf(p, p.loopStart);
            uint32_t regionEnd = FramesOf(p, p.loopStart + p.loopLength);
            if (regionEnd < loopEnd)
                loopEnd = regionEnd;
        }
        if (loopEnd <= loopStart) {
            p.frame = loopStart;
        } else if (p.frame >= loopEnd) {
            p.frame = loopStart + (p.frame - loopStart) % (loopEnd - loopStart);
        }
    }
    LeaveCriticalSection(&g_playbackLock);
}

template <class F> void WithBuffer(const IDirectSoundBuffer *buffer, F change) {
    if (!SndLockstep_Enabled() || buffer == NULL)
        return;
    EnterCriticalSection(&g_playbackLock);
    auto found = g_buffers.find(buffer);
    if (found != g_buffers.end())
        change(found->second);
    LeaveCriticalSection(&g_playbackLock);
}

// ---- the driver thread's step, as SNDDRV_thread runs it

void RunDriverStep() {
    SNDI_mutexlock();
    AdvancePlayback();
    FrameCallback pre = PreFrameCallback;
    if (pre != NULL)
        pre();
    SNDSYSI_100hzserver();
    dsndMixProcess();
    SNDPKTPLAYI_flushcallbackdata();
    FrameCallback post = PostFrameCallback;
    if (post != NULL)
        post();
    SNDI_mutexunlock();
}

bool IsStreamCallback(FsCallback callback) {
    return callback == StreamOpenDone || callback == StreamCloseDone || callback == StreamReadDone;
}

// A stream's chain of file ops, run on the thread that started it (SndLockstep_FileCallback)
struct FileOp {
    unsigned handle;
    FsCallback callback;
};
constexpr unsigned kChainDepth = 16;
thread_local bool t_running;           // this thread is running a chain
thread_local bool t_passThrough;       // the next FILESYS_callbackop on this thread is RunWhenDone's
thread_local FileOp t_chain[kChainDepth];
thread_local unsigned t_next, t_queued;

// The op finished (FILESYS_opstatus no longer 0, pending), then its callback run by FILESYS_callbackop itself. A
// worker that never gets to it (it cannot here, but if it did) leaves the op to call back from the worker.
void RunWhenDone(unsigned handle, FsCallback callback) {
    DWORD start = GetTickCount();
    for (int spins = 0; FILESYS_opstatus(handle) == 0; spins++) {
        if (spins < 1000) {
            SwitchToThread();
            continue;
        }
        if (GetTickCount() - start > kFileOpPatienceMs) {
            printf("[sndlockstep] stream file op %08x still not done after %lu ms - left to the worker\n", handle,
                   kFileOpPatienceMs);
            fflush(stdout);
            break;
        }
        Sleep(1);
    }
    t_passThrough = true;
    FILESYS_callbackop(handle, callback);
    t_passThrough = false;
}

}  // namespace

bool SndLockstep_Enabled(void) {
    Init();
    return g_enabled;
}

void SndLockstep_Tick(void) {
    if (!SndLockstep_Enabled())
        return;

    int realTick = RealTimerTicks;
    int virtualTick = g_ticking ? SndLockstep_TimerTick(realTick) + 1 : realTick;
    g_realAtTick = realTick;
    g_virtualTick = virtualTick;
    g_ticking = true;

    int rate = TIMER_getfrequency();
    if (rate <= 0)
        rate = 50;
    g_stepRemainder += 1000 / kStepMs;
    int steps = g_stepRemainder / rate;
    g_stepRemainder %= rate;
    if (g_driverAlive && KeepRunning != 0) {
        for (int i = 0; i < steps; i++) {
            ReleaseSemaphore(g_stepGranted, 1, NULL);
            while (WaitForSingleObject(g_stepDone, 100) == WAIT_TIMEOUT) {
                if (!g_driverAlive)
                    break;
            }
        }
    }

    if (SysTaskAdded != 0) {
        g_ownService = true;
        SystemTask(0, 0);
        g_ownService = false;
    }

    if (g_ticks++ == 0) {
        printf("[sndlockstep] first tick: %d driver steps a tick at %d Hz\n", steps, rate);
        fflush(stdout);
    }
}

uint32_t SndLockstep_DriverThread(void) {
    Init();
    InterlockedExchange(&g_driverAlive, 1);
    volatile uint8_t &keepRunning = KeepRunning;   // cleared by SNDPLATFORM_restore on the game thread
    while (keepRunning != 0) {
        if (WaitForSingleObject(g_stepGranted, 1) != WAIT_OBJECT_0)
            continue;
        RunDriverStep();
        ReleaseSemaphore(g_stepDone, 1, NULL);
    }
    InterlockedExchange(&g_driverAlive, 0);
    static_cast<volatile uint8_t &>(IsRunning) = 0;
    return 0;
}

// The first call inside SndLockstep_Tick's own is the one it means; any other (SYNCTASK's) is deferred.
bool SndLockstep_DefersService(void) {
    if (!SndLockstep_Enabled())
        return false;
    if (g_ownService) {
        g_ownService = false;
        return false;
    }
    return true;
}

// Each op is waited for with no FILESYS lock held, then FILESYS_callbackop runs its callback at once. A callback
// that starts the next op (a read finished, so read on) does it from inside FILESYS_callbackop, which holds the
// worker's queue lock while it calls back: waiting there would starve the worker, so that op is only noted, and run
// the same way once the callback has returned - the chain the worker would have run, run here, in its order.
bool SndLockstep_FileCallback(unsigned handle, FsCallback callback) {
    if (!SndLockstep_Enabled() || !IsStreamCallback(callback))
        return false;
    if (t_passThrough) {
        t_passThrough = false;
        return false;
    }
    if (t_running) {
        if (t_queued - t_next == kChainDepth) {
            printf("[sndlockstep] more than %u stream file ops waiting - op %08x left to the worker\n", kChainDepth,
                   handle);
            fflush(stdout);
            return false;
        }
        t_chain[t_queued++ % kChainDepth] = { handle, callback };
        return true;
    }
    t_running = true;
    t_next = t_queued = 0;
    RunWhenDone(handle, callback);
    while (t_next != t_queued) {
        FileOp op = t_chain[t_next++ % kChainDepth];
        RunWhenDone(op.handle, op.callback);
    }
    t_running = false;
    return true;
}

int SndLockstep_TimerTick(int realTick) {
    if (!g_ticking)
        return realTick;
    int stalled = realTick - g_realAtTick - TIMER_getfrequency();
    return stalled > 0 ? g_virtualTick + stalled : g_virtualTick;
}

void SndLockstep_BufferCreated(const IDirectSoundBuffer *buffer, uint32_t sampleRate, uint16_t formatTag,
                               uint16_t channels, uint16_t blockAlign, uint16_t bitsPerSample, uint32_t bytes) {
    if (!SndLockstep_Enabled() || buffer == NULL)
        return;
    Playback p = {};
    p.sampleRate = sampleRate;
    p.formatTag = formatTag;
    p.channels = channels;
    p.blockAlign = blockAlign;
    p.bitsPerSample = bitsPerSample;
    p.bytes = bytes;
    p.frequency = sampleRate;
    EnterCriticalSection(&g_playbackLock);
    g_buffers[buffer] = p;
    LeaveCriticalSection(&g_playbackLock);
}

void SndLockstep_BufferData(const IDirectSoundBuffer *buffer, uint32_t bytes) {
    WithBuffer(buffer, [&](Playback &p) { p.bytes = bytes; });
}

void SndLockstep_BufferPlay(const IDirectSoundBuffer *buffer, bool looping) {
    WithBuffer(buffer, [&](Playback &p) {
        p.playing = true;
        p.looping = looping;
        if (p.frame >= FramesOf(p, p.bytes)) {
            p.frame = 0;
            p.thousandths = 0;
        }
    });
}

void SndLockstep_BufferStop(const IDirectSoundBuffer *buffer) {
    WithBuffer(buffer, [&](Playback &p) { p.playing = false; });
}

void SndLockstep_BufferPosition(const IDirectSoundBuffer *buffer, uint32_t bytes) {
    WithBuffer(buffer, [&](Playback &p) {
        p.frame = FramesOf(p, bytes);
        p.thousandths = 0;
    });
}

void SndLockstep_BufferLoopRegion(const IDirectSoundBuffer *buffer, uint32_t startBytes, uint32_t lengthBytes) {
    WithBuffer(buffer, [&](Playback &p) {
        p.loopStart = startBytes;
        p.loopLength = lengthBytes;
    });
}

void SndLockstep_BufferFrequency(const IDirectSoundBuffer *buffer, uint32_t hz) {
    WithBuffer(buffer, [&](Playback &p) {
        if (hz != 0)
            p.frequency = hz;
    });
}

void SndLockstep_BufferReleased(const IDirectSoundBuffer *buffer) {
    if (!SndLockstep_Enabled() || buffer == NULL)
        return;
    EnterCriticalSection(&g_playbackLock);
    g_buffers.erase(buffer);
    LeaveCriticalSection(&g_playbackLock);
}

bool SndLockstep_BufferStatus(const IDirectSoundBuffer *buffer, uint32_t *status) {
    if (!SndLockstep_Enabled())
        return false;
    uint32_t answer = 0;
    WithBuffer(buffer, [&](Playback &p) {
        if (p.playing)
            answer = kStatusPlaying | (p.looping ? kStatusLooping : 0);
    });
    if (status != NULL)
        *status = answer;
    return true;
}

// Nothing is written ahead of the simulated play cursor, so the write cursor is the same place.
bool SndLockstep_BufferCursors(const IDirectSoundBuffer *buffer, uint32_t *playCursor, uint32_t *writeCursor) {
    if (!SndLockstep_Enabled())
        return false;
    uint32_t position = 0;
    WithBuffer(buffer, [&](Playback &p) { position = BytesOf(p, p.frame); });
    if (playCursor != NULL)
        *playCursor = position;
    if (writeCursor != NULL)
        *writeCursor = position;
    return true;
}
