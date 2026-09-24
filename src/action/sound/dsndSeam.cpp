#include "dsndSeam.h"
#include "../engine/XboxSettings.h" // Settings_GetAudioBackend
#include "xaudio2Backend.h"          // the native backend the entry-point wrappers can dispatch to

#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <type_traits>

// ---------------------------------------------------------------------------------------------------------------
// The audio seam. See dsndSeam.h for what this is and why; docs/cxbx-removal-plan.md section 3 for where it sits
// in the overall CXBX removal. Everything below is a reimplementation of Eurocom's own audio wrapper layer
// (0x000e0f00-0x000e1e40), still calling the untouched DSOUND library through the entry-point macros further
// down, so behaviour in CXBX mode is unchanged.
//
// Addresses in comments are for the EU default.xbe, matching tools/functions_action.json - Ghidra names every
// function in the range, but three are injected by address rather than by name (see the FUNC_AT comments).
// ---------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------
// TEMPORARY tracing of every DSOUND entry point this seam calls - the inventory pass whose log is the spec for
// the eventual native backend (plan step 3.3.2). Set DSNDSEAM_TRACE to 0 to compile it all out (every DSOUND
// macro then collapses back to a plain forwarding call). Modelled directly on d3dSeam.cpp's tracing, which see.
//
// Two things are recorded, both to dsndSeam_trace.log in the working directory:
//  - a running table per entry point: call count plus the set of distinct "key" values seen (the first argument
//    that is neither a pointer nor a float - i.e. the volume, frequency, headroom, size, loop point... rather
//    than the buffer object every one of these takes first), dumped every DSNDSEAM_TRACE_SUMMARY_EVERY frames
//    and on every level reset;
//  - a full per-call listing (all arguments) for the first DSNDSEAM_TRACE_DETAIL_FRAMES frames after boot and
//    after every level reset, so the log stays bounded no matter how long the game runs.
//
// "Frame" here is one dsndUpdateVoices (0x000e19c0) call, which the game makes once per rendered frame.
// ---------------------------------------------------------------------------------------------------------------
#ifndef DSNDSEAM_TRACE
#define DSNDSEAM_TRACE 0
#endif
#define DSNDSEAM_TRACE_DETAIL_FRAMES 8
#define DSNDSEAM_TRACE_DETAIL_DELAY  600  // frames after a level reset before the detailed window opens (past the loading screen)
#define DSNDSEAM_TRACE_SUMMARY_EVERY 1000

#if DSNDSEAM_TRACE

struct DSoundSeamTraceEntry {
    const char *name;
    uint32_t calls;
    uint32_t distinctCount;
    uint32_t distinct[24];
    bool overflow;
};
static DSoundSeamTraceEntry g_dsndTraceEntries[64];
static int g_dsndTraceEntryCount = 0;
static uint32_t g_dsndTraceFrame = 0;
static int g_dsndTraceDetailFramesLeft = DSNDSEAM_TRACE_DETAIL_FRAMES;
static int g_dsndTraceDetailDelay = 0;
static FILE *g_dsndTraceFile = NULL;

static FILE *DSoundSeamTraceFile(void) {
    if (g_dsndTraceFile == NULL)
        g_dsndTraceFile = fopen("dsndSeam_trace.log", "w");
    return g_dsndTraceFile;
}

static DSoundSeamTraceEntry *DSoundSeamTraceFind(const char *name) {
    for (int i = 0; i < g_dsndTraceEntryCount; i++) {
        if (g_dsndTraceEntries[i].name == name || strcmp(g_dsndTraceEntries[i].name, name) == 0)
            return &g_dsndTraceEntries[i];
    }
    if (g_dsndTraceEntryCount >= (int)(sizeof(g_dsndTraceEntries) / sizeof(g_dsndTraceEntries[0])))
        return NULL;
    DSoundSeamTraceEntry *e = &g_dsndTraceEntries[g_dsndTraceEntryCount++];
    memset(e, 0, sizeof(*e));
    e->name = name;
    return e;
}

static void DSoundSeamTraceRecord(const char *name, uint32_t key, const char *detail) {
    DSoundSeamTraceEntry *e = DSoundSeamTraceFind(name);
    if (e != NULL) {
        e->calls++;
        bool seen = false;
        for (uint32_t i = 0; i < e->distinctCount; i++) {
            if (e->distinct[i] == key) { seen = true; break; }
        }
        if (!seen) {
            if (e->distinctCount < sizeof(e->distinct) / sizeof(e->distinct[0]))
                e->distinct[e->distinctCount++] = key;
            else
                e->overflow = true;
        }
    }
    if (detail != NULL) {
        FILE *f = DSoundSeamTraceFile();
        if (f != NULL)
            fprintf(f, "F%u %s%s\n", g_dsndTraceFrame, name, detail);
    }
}

static void DSoundSeamTraceDumpSummary(const char *reason) {
    FILE *f = DSoundSeamTraceFile();
    if (f == NULL)
        return;
    fprintf(f, "\n==== summary at frame %u (%s): %d entry points seen ====\n", g_dsndTraceFrame, reason, g_dsndTraceEntryCount);
    for (int i = 0; i < g_dsndTraceEntryCount; i++) {
        const DSoundSeamTraceEntry *e = &g_dsndTraceEntries[i];
        fprintf(f, "  %-48s calls=%-9u keys={", e->name, e->calls);
        for (uint32_t k = 0; k < e->distinctCount; k++)
            fprintf(f, "%s0x%x", k ? ", " : "", e->distinct[k]);
        fprintf(f, "%s}\n", e->overflow ? ", ..." : "");
    }
    fprintf(f, "====\n\n");
    fflush(f);
}

static bool DSoundSeamTraceWantDetail(void) {
    return g_dsndTraceDetailFramesLeft > 0;
}

// Called from dsndUpdateVoices (frame boundary) and maybeSoundShutdown (level reset).
static void DSoundSeamTraceEndFrame(void) {
    g_dsndTraceFrame++;
    if (g_dsndTraceDetailDelay > 0) {
        if (--g_dsndTraceDetailDelay == 0) {
            g_dsndTraceDetailFramesLeft = DSNDSEAM_TRACE_DETAIL_FRAMES;
            if (g_dsndTraceFile != NULL)
                fprintf(g_dsndTraceFile, "==== detailed listing window opens at frame %u ====\n", g_dsndTraceFrame);
        }
    }
    if (g_dsndTraceDetailFramesLeft > 0) {
        g_dsndTraceDetailFramesLeft--;
        if (g_dsndTraceFile != NULL)
            fflush(g_dsndTraceFile);
    }
    if (g_dsndTraceFrame % DSNDSEAM_TRACE_SUMMARY_EVERY == 0)
        DSoundSeamTraceDumpSummary("periodic");
}

static void DSoundSeamTraceLevelReset(void) {
    DSoundSeamTraceDumpSummary("level reset");
    FILE *f = DSoundSeamTraceFile();
    if (f != NULL)
        fprintf(f, "==== level reset at frame %u - detailed listing window scheduled %d frames from now ====\n",
                g_dsndTraceFrame, DSNDSEAM_TRACE_DETAIL_DELAY);
    g_dsndTraceDetailDelay = DSNDSEAM_TRACE_DETAIL_DELAY;
}

// One argument, formatted by type: floats as floats, everything else (pointers, handles, volumes) as hex.
template<typename T>
static void DSoundSeamTraceAppendArg(char *buf, size_t cap, size_t *pos, T value) {
    if (*pos + 24 >= cap)
        return;
    int n;
    if constexpr (std::is_floating_point_v<T>)
        n = snprintf(buf + *pos, cap - *pos, "%g, ", (double)value);
    else if constexpr (std::is_pointer_v<T>)
        n = snprintf(buf + *pos, cap - *pos, "0x%x, ", (unsigned)(uintptr_t)value);
    else
        n = snprintf(buf + *pos, cap - *pos, "0x%x, ", (unsigned)value);
    if (n > 0)
        *pos += (size_t)n;
}

// The key is the first argument that is neither a pointer nor a float: for almost every DSOUND entry point the
// first argument is the buffer/stream/device object, which would just overflow the distinct-value set.
static inline uint32_t DSoundSeamTraceKey(void) { return 0; }
template<typename T, typename... Rest>
static inline uint32_t DSoundSeamTraceKey(T first, Rest... rest) {
    if constexpr (std::is_floating_point_v<T> || std::is_pointer_v<T>) return DSoundSeamTraceKey(rest...);
    else return (uint32_t)first;
}

template<typename... A>
static void DSoundSeamTraceCall(const char *name, A... args) {
    uint32_t key = DSoundSeamTraceKey(args...);
    if (!DSoundSeamTraceWantDetail()) {
        DSoundSeamTraceRecord(name, key, NULL);
        return;
    }
    char detail[256];
    size_t pos = 0;
    detail[pos++] = '(';
    (DSoundSeamTraceAppendArg(detail, sizeof(detail), &pos, args), ...);
    if (pos >= 3 && detail[pos - 2] == ',')
        pos -= 2;
    detail[pos++] = ')';
    detail[pos] = '\0';
    DSoundSeamTraceRecord(name, key, detail);
}

#else // !DSNDSEAM_TRACE

static inline void DSoundSeamTraceEndFrame(void) {}
static inline void DSoundSeamTraceLevelReset(void) {}
template<typename... A> static inline void DSoundSeamTraceCall(const char *, A...) {}

#endif // DSNDSEAM_TRACE

// ---------------------------------------------------------------------------------------------------------------
// Entry-point dispatch. Every DSOUND entry point the seam calls goes through one of these wrappers (via the
// macros below): it records the call when tracing is on, then either forwards to the DSOUND library (cxbx mode)
// or to the attached native backend function. An entry point with no backend function attached is counted as
// missing and does nothing. Every public DSOUND entry point in this XBE is plain __stdcall (verified: each one
// ends in a RET whose immediate matches its parameter count), so - unlike the D3D seam, which needed a
// __fastcall variant too - one wrapper type is enough.
// ---------------------------------------------------------------------------------------------------------------

int g_audioBackend = AUDIO_BACKEND_CXBX;

// Backend messages go to the console and to dsnd_backend.log in the working directory.
static void DSoundLog(const char *fmt, ...) {
    static FILE *logFile = NULL;
    static bool opened = false;
    if (!opened) { opened = true; logFile = fopen("dsnd_backend.log", "w"); }
    va_list ap;
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    if (logFile != NULL) { va_start(ap, fmt); vfprintf(logFile, fmt, ap); va_end(ap); fflush(logFile); }
}

struct DSoundMissingEntry { const char *name; uint32_t count; };
static DSoundMissingEntry g_dsndMissing[64];
static int g_dsndMissingCount = 0;

void DSound_BackendMissing(const char *entryPoint) {
    for (int i = 0; i < g_dsndMissingCount; i++) {
        if (g_dsndMissing[i].name == entryPoint || strcmp(g_dsndMissing[i].name, entryPoint) == 0) {
            g_dsndMissing[i].count++;
            return;
        }
    }
    if (g_dsndMissingCount < (int)(sizeof(g_dsndMissing) / sizeof(g_dsndMissing[0]))) {
        g_dsndMissing[g_dsndMissingCount].name = entryPoint;
        g_dsndMissing[g_dsndMissingCount].count = 1;
        g_dsndMissingCount++;
        DSoundLog("[dsnd] not implemented yet: %s\n", entryPoint);
    }
}

static void DSoundPrintMissingSummary(void) {
    if (g_dsndMissingCount == 0)
        return;
    DSoundLog("[dsnd] entry points still unimplemented (calls so far):");
    for (int i = 0; i < g_dsndMissingCount; i++)
        DSoundLog(" %s=%u", g_dsndMissing[i].name, g_dsndMissing[i].count);
    DSoundLog("\n");
}

template<typename R, typename... A> struct DSoundSeamTracedCall {
    const char *name;
    R(__stdcall *fn)(A...);
    R(*backend)(A...);
    R operator()(A... args) const {
        DSoundSeamTraceCall(name, args...);
        if (g_audioBackend != AUDIO_BACKEND_CXBX) {
            if (backend != NULL)
                return backend(args...);
            DSound_BackendMissing(name);
            return R();
        }
        return fn(args...);
    }
};
template<typename R, typename... A>
static inline DSoundSeamTracedCall<R, A...> DSoundSeamTraced(const char *name, R(__stdcall *fn)(A...)) {
    return { name, fn, NULL };
}
template<typename R, typename... A>
static inline DSoundSeamTracedCall<R, A...> DSoundSeamTraced(const char *name, R(__stdcall *fn)(A...), R(*backend)(A...)) {
    return { name, fn, backend };
}

// A few entry points have to keep going to the DSOUND library even in native mode, because the objects they
// act on are not ours to own. The XMV decoder creates its streams by calling DirectSoundCreateStream directly
// rather than through anything this seam replaced, so CXBX's emulation both owns those stream objects and -
// while CXBX is still hosting the process - still plays them. That is why FMV audio is audible in native mode
// at all. Routing the stream setters to a native backend, or dropping them, would leave the stream playing at
// whatever volume and mixbin routing CXBX happened to default to, with the game's own calls going nowhere.
//
// This is a bridge, not an end state: when the stream entry points are hooked at their own addresses the
// streams become ours and these go back through the normal dispatch.
template<typename R, typename... A> struct DSoundSeamPassThroughCall {
    const char *name;
    R(__stdcall *fn)(A...);
    R operator()(A... args) const {
        DSoundSeamTraceCall(name, args...);
        return fn(args...);
    }
};
template<typename R, typename... A>
static inline DSoundSeamPassThroughCall<R, A...> DSoundSeamPassThrough(const char *name, R(__stdcall *fn)(A...)) {
    return { name, fn };
}

// ---------------------------------------------------------------------------------------------------------------
// The DSOUND entry points. These are the public (I)DirectSound* wrappers at the top of the library, not its
// internals: everything they call at 0x00112xxx-0x00119xxx (the CDirectSound*/CMcpx* classes) is inside the
// library and stops existing once a native backend takes over, exactly like the D3D8 library did.
//
// All confirmed __stdcall. Parameter types are the real Xbox DirectSound ones, which for the 3D setters means
// float rather than the int Ghidra inferred - confirmed from the call sites (SFXUpdate FSTPs its computed
// positions straight into the argument slots; see the raw disassembly at 0x000e19de-0x000e1a16).
// ---------------------------------------------------------------------------------------------------------------

#define WAVE_FORMAT_XBOX_ADPCM 0x0069u
#define DSBCAPS_CTRL3D         0x00000010u

// DS3D_APPLY
#define DS3D_IMMEDIATE 0u
#define DS3D_DEFERRED  1u

// DSMIXBIN indices
enum {
    DSMIXBIN_FRONT_LEFT = 0, DSMIXBIN_FRONT_RIGHT = 1, DSMIXBIN_FRONT_CENTER = 2,
    DSMIXBIN_LOW_FREQUENCY = 3, DSMIXBIN_BACK_LEFT = 4, DSMIXBIN_BACK_RIGHT = 5,
    DSMIXBIN_XTLK_FRONT_LEFT = 6, DSMIXBIN_XTLK_FRONT_RIGHT = 7,
    DSMIXBIN_XTLK_BACK_LEFT = 8, DSMIXBIN_XTLK_BACK_RIGHT = 9,
    DSMIXBIN_I3DL2 = 10,
};

// DSBSTATUS, as reported by IDirectSoundBuffer_GetStatus
#define DSBSTATUS_PLAYING 0x00000001u

// THREE parameters, not the two Ghidra reports: the function is RET 0xc, it reads its ppDS out of [EBP+0xc]
// (i.e. the second argument), and the original call site at 0x000e1af3 pushes three dwords. Getting this wrong
// is silent and fatal - the callee pops 4 bytes more than the caller pushed, so xboxInitSound's own epilogue
// popped the wrong registers and returned to garbage instead of to main. Every other entry point below was
// re-checked the same way (RET immediate read out of the image against the typedef's parameter count) and was
// already right; do the same for anything added here later rather than trusting the reported prototype.
typedef void(__stdcall *DirectSoundCreateFn)(void *lpGuid, DSoundObject **ppDS, void *pUnknown);
#define DirectSoundCreate (DSoundSeamTraced("DirectSoundCreate", (DirectSoundCreateFn)0x001148b8u, XA2_DirectSoundCreate))

typedef void(__stdcall *DirectSoundUseFullHRTFFn)(void);
#define DirectSoundUseFullHRTF (DSoundSeamTraced("DirectSoundUseFullHRTF", (DirectSoundUseFullHRTFFn)0x0011275fu, XA2_DirectSoundUseFullHRTF))

typedef void(__stdcall *DirectSoundDoWorkFn)(void);
#define DirectSoundDoWork (DSoundSeamTraced("DirectSoundDoWork", (DirectSoundDoWorkFn)0x001134fdu, XA2_DirectSoundDoWork))

typedef void(__stdcall *IDirectSound_DownloadEffectsImageFn)(DSoundObject *thisPtr, const void *pvImageBuffer,
                                                             uint32_t dwImageSize, void *pImageLoc, void **ppImageDesc);
#define IDirectSound_DownloadEffectsImage (DSoundSeamTraced("IDirectSound_DownloadEffectsImage", (IDirectSound_DownloadEffectsImageFn)0x0011338bu, XA2_IDirectSound_DownloadEffectsImage))

typedef void(__stdcall *IDirectSound_CreateSoundBufferFn)(DSoundObject *thisPtr, DSBUFFERDESC_Xbox *pdsbd,
                                                          uint32_t *ppBuffer, uint32_t *ppUnknown);
#define IDirectSound_CreateSoundBuffer (DSoundSeamTraced("IDirectSound_CreateSoundBuffer", (IDirectSound_CreateSoundBufferFn)0x001146feu, XA2_IDirectSound_CreateSoundBuffer))

typedef void(__stdcall *IDirectSound_SetPositionFn)(DSoundObject *thisPtr, float x, float y, float z, uint32_t dwApply);
#define IDirectSound_SetPosition (DSoundSeamTraced("IDirectSound_SetPosition", (IDirectSound_SetPositionFn)0x0011437eu, XA2_IDirectSound_SetPosition))

typedef void(__stdcall *IDirectSound_SetVelocityFn)(DSoundObject *thisPtr, float x, float y, float z, uint32_t dwApply);
#define IDirectSound_SetVelocity (DSoundSeamTraced("IDirectSound_SetVelocity", (IDirectSound_SetVelocityFn)0x001143b3u, XA2_IDirectSound_SetVelocity))

typedef void(__stdcall *IDirectSound_SetOrientationFn)(DSoundObject *thisPtr, float xFront, float yFront, float zFront,
                                                       float xTop, float yTop, float zTop, uint32_t dwApply);
#define IDirectSound_SetOrientation (DSoundSeamTraced("IDirectSound_SetOrientation", (IDirectSound_SetOrientationFn)0x00114334u, XA2_IDirectSound_SetOrientation))

typedef void(__stdcall *IDirectSound_CommitDeferredSettingsFn)(DSoundObject *thisPtr);
#define IDirectSound_CommitDeferredSettings (DSoundSeamTraced("IDirectSound_CommitDeferredSettings", (IDirectSound_CommitDeferredSettingsFn)0x00113c13u, XA2_IDirectSound_CommitDeferredSettings))

typedef void(__stdcall *IDirectSoundBuffer_SetBufferDataFn)(DSoundBuffer *thisPtr, void *pvBufferData, uint32_t dwBufferBytes);
#define IDirectSoundBuffer_SetBufferData (DSoundSeamTraced("IDirectSoundBuffer_SetBufferData", (IDirectSoundBuffer_SetBufferDataFn)0x001143e8u, XA2_IDirectSoundBuffer_SetBufferData))

typedef void(__stdcall *IDirectSoundBuffer_SetFrequencyFn)(DSoundBuffer *thisPtr, uint32_t dwFrequency);
#define IDirectSoundBuffer_SetFrequency (DSoundSeamTraced("IDirectSoundBuffer_SetFrequency", (IDirectSoundBuffer_SetFrequencyFn)0x00113c2bu, XA2_IDirectSoundBuffer_SetFrequency))

typedef void(__stdcall *IDirectSoundBuffer_SetLoopRegionFn)(DSoundBuffer *thisPtr, uint32_t dwLoopStart, uint32_t dwLoopLength);
#define IDirectSoundBuffer_SetLoopRegion (DSoundSeamTraced("IDirectSoundBuffer_SetLoopRegion", (IDirectSoundBuffer_SetLoopRegionFn)0x00113476u, XA2_IDirectSoundBuffer_SetLoopRegion))

typedef void(__stdcall *IDirectSoundBuffer_SetCurrentPositionFn)(DSoundBuffer *thisPtr, uint32_t dwPlayCursor);
#define IDirectSoundBuffer_SetCurrentPosition (DSoundSeamTraced("IDirectSoundBuffer_SetCurrentPosition", (IDirectSoundBuffer_SetCurrentPositionFn)0x001134d2u, XA2_IDirectSoundBuffer_SetCurrentPosition))

typedef void(__stdcall *IDirectSoundBuffer_GetCurrentPositionFn)(DSoundBuffer *thisPtr, uint32_t *pdwPlayCursor, uint32_t *pdwWriteCursor);
#define IDirectSoundBuffer_GetCurrentPosition (DSoundSeamTraced("IDirectSoundBuffer_GetCurrentPosition", (IDirectSoundBuffer_GetCurrentPositionFn)0x001134b2u, XA2_IDirectSoundBuffer_GetCurrentPosition))

typedef void(__stdcall *IDirectSoundBuffer_GetStatusFn)(DSoundBuffer *thisPtr, uint32_t *pdwStatus);
#define IDirectSoundBuffer_GetStatus (DSoundSeamTraced("IDirectSoundBuffer_GetStatus", (IDirectSoundBuffer_GetStatusFn)0x00113496u, XA2_IDirectSoundBuffer_GetStatus))

typedef void(__stdcall *IDirectSoundBuffer_PlayFn)(DSoundBuffer *thisPtr, uint32_t dwReserved1, uint32_t dwReserved2, uint32_t dwFlags);
#define IDirectSoundBuffer_Play (DSoundSeamTraced("IDirectSoundBuffer_Play", (IDirectSoundBuffer_PlayFn)0x0011343au, XA2_IDirectSoundBuffer_Play))

typedef void(__stdcall *IDirectSoundBuffer_StopFn)(DSoundBuffer *thisPtr);
#define IDirectSoundBuffer_Stop (DSoundSeamTraced("IDirectSoundBuffer_Stop", (IDirectSoundBuffer_StopFn)0x0011345eu, XA2_IDirectSoundBuffer_Stop))

typedef void(__stdcall *IDirectSoundBuffer_SetVolumeFn)(DSoundBuffer *thisPtr, int32_t lVolume);
#define IDirectSoundBuffer_SetVolume (DSoundSeamTraced("IDirectSoundBuffer_SetVolume", (IDirectSoundBuffer_SetVolumeFn)0x001133cau, XA2_IDirectSoundBuffer_SetVolume))

typedef void(__stdcall *IDirectSoundBuffer_SetHeadroomFn)(DSoundBuffer *thisPtr, uint32_t dwHeadroom);
#define IDirectSoundBuffer_SetHeadroom (DSoundSeamTraced("IDirectSoundBuffer_SetHeadroom", (IDirectSoundBuffer_SetHeadroomFn)0x001133e6u, XA2_IDirectSoundBuffer_SetHeadroom))

typedef void(__stdcall *IDirectSoundBuffer_SetMixBinsFn)(DSoundBuffer *thisPtr, DSMIXBINS_Xbox *pMixBins);
#define IDirectSoundBuffer_SetMixBins (DSoundSeamTraced("IDirectSoundBuffer_SetMixBins", (IDirectSoundBuffer_SetMixBinsFn)0x00113402u, XA2_IDirectSoundBuffer_SetMixBins))

// The library exports two SetMixBinVolumes entry points; this is the 8-byte-argument one (a DSMIXBINS, same
// shape as SetMixBins), which is the only one the game uses (from dsndSetPan).
typedef void(__stdcall *IDirectSoundBuffer_SetMixBinVolumesFn)(DSoundBuffer *thisPtr, DSMIXBINS_Xbox *pMixBins);
#define IDirectSoundBuffer_SetMixBinVolumes (DSoundSeamTraced("IDirectSoundBuffer_SetMixBinVolumes_8", (IDirectSoundBuffer_SetMixBinVolumesFn)0x0011341eu, XA2_IDirectSoundBuffer_SetMixBinVolumes))

typedef void(__stdcall *IDirectSoundBuffer_SetMinDistanceFn)(DSoundBuffer *thisPtr, float flMinDistance, uint32_t dwApply);
#define IDirectSoundBuffer_SetMinDistance (DSoundSeamTraced("IDirectSoundBuffer_SetMinDistance", (IDirectSoundBuffer_SetMinDistanceFn)0x00113c6bu, XA2_IDirectSoundBuffer_SetMinDistance))

typedef void(__stdcall *IDirectSoundBuffer_SetMaxDistanceFn)(DSoundBuffer *thisPtr, float flMaxDistance, uint32_t dwApply);
#define IDirectSoundBuffer_SetMaxDistance (DSoundSeamTraced("IDirectSoundBuffer_SetMaxDistance", (IDirectSoundBuffer_SetMaxDistanceFn)0x00113c47u, XA2_IDirectSoundBuffer_SetMaxDistance))

typedef void(__stdcall *IDirectSoundBuffer_SetPositionFn)(DSoundBuffer *thisPtr, float x, float y, float z, uint32_t dwApply);
#define IDirectSoundBuffer_SetPosition (DSoundSeamTraced("IDirectSoundBuffer_SetPosition", (IDirectSoundBuffer_SetPositionFn)0x00113c8fu, XA2_IDirectSoundBuffer_SetPosition))

typedef void(__stdcall *IDirectSoundBuffer_SetVelocityFn)(DSoundBuffer *thisPtr, float x, float y, float z, uint32_t dwApply);
#define IDirectSoundBuffer_SetVelocity (DSoundSeamTraced("IDirectSoundBuffer_SetVelocity", (IDirectSoundBuffer_SetVelocityFn)0x00113cc4u, XA2_IDirectSoundBuffer_SetVelocity))

typedef void(__stdcall *IDirectSoundBuffer_SetRolloffCurveFn)(DSoundBuffer *thisPtr, const float *pflPoints,
                                                              uint32_t dwPointCount, uint32_t dwApply);
#define IDirectSoundBuffer_SetRolloffCurve (DSoundSeamTraced("IDirectSoundBuffer_SetRolloffCurve", (IDirectSoundBuffer_SetRolloffCurveFn)0x00113cf9u, XA2_IDirectSoundBuffer_SetRolloffCurve))

typedef void(__stdcall *IDirectSoundBuffer_SetI3DL2SourceFn)(DSoundBuffer *thisPtr, DSI3DL2BUFFER_Xbox *pds3db, uint32_t dwApply);
#define IDirectSoundBuffer_SetI3DL2Source (DSoundSeamTraced("IDirectSoundBuffer_SetI3DL2Source", (IDirectSoundBuffer_SetI3DL2SourceFn)0x00113d1du, XA2_IDirectSoundBuffer_SetI3DL2Source))

typedef void(__stdcall *IDirectSoundStream_SetVolumeFn)(DSoundStream *pStream, int32_t lVolume);
#define IDirectSoundStream_SetVolume (DSoundSeamPassThrough("IDirectSoundStream_SetVolume", (IDirectSoundStream_SetVolumeFn)0x001134eeu))

typedef void(__stdcall *IDirectSoundStream_SetMixBinsFn)(DSoundStream *pStream, DSMIXBINS_Xbox *pMixBins);
#define IDirectSoundStream_SetMixBins (DSoundSeamPassThrough("IDirectSoundStream_SetMixBins", (IDirectSoundStream_SetMixBinsFn)0x001134f3u))

// ---------------------------------------------------------------------------------------------------------------
// The game's own audio state, at its fixed address - "AudioSystem" in Ghidra, one 5312-byte struct. Mirrored
// here field for field rather than accessed through offsets, the same way the D3D seam mirrors Gfx.
// ---------------------------------------------------------------------------------------------------------------

#pragma pack(push, 1)

// "directSoundMaybeVoices" in Ghidra. One of these per voice slot; 64 slots.
struct XboxVoice {
    DSoundBuffer *buffer;            // +0x00 whichever of the three pre-made buffers for this slot is bound
    DSI3DL2BUFFER_Xbox i3dl2source;  // +0x04 reverb send parameters, only lRoom is ever set
    void *data;                      // +0x28 the game's own sample data - non-NULL means "slot in use"
    uint32_t size;                   // +0x2c
    uint32_t frequency;              // +0x30
    uint32_t numChannels;            // +0x34
    uint8_t loop;                    // +0x38
    uint8_t is3d;                    // +0x39
    uint8_t positioned;              // +0x3a field_0x3a: 0 = the per-frame update parks this voice on the listener
    uint8_t pad3b;                   // +0x3b
    uint32_t flags;                  // +0x3c see VOICE_* below
};
static_assert(sizeof(XboxVoice) == 0x40, "XboxVoice must be 0x40 bytes");

struct XboxAudioSystem {
    DSoundObject *directSoundInst;        // +0x000
    void *effectImageDesc;                // +0x004
    void *effectImageLocation;            // +0x008
    uint32_t maybeInitialised;            // +0x00c
    DSoundBuffer *buffersMono2D[64];      // +0x010
    DSoundBuffer *buffersStereo2D[64];    // +0x110
    DSoundBuffer *buffers3D[64];          // +0x210 mono, DSBCAPS_CTRL3D
    float listenerPosition[3];            // +0x310
    float listenerOrientation[3];         // +0x31c unit front vector
    XboxVoice voices[64];                 // +0x328
    uint32_t activeVoiceCount;            // +0x1328 recomputed by dsndUpdateVoices, informational
    int32_t volumeTable[101];             // +0x132c 0..100 -> hundredths of a dB, -10000..0
};
static_assert(sizeof(XboxAudioSystem) == 5312, "XboxAudioSystem must be 5312 bytes");

#pragma pack(pop)

#define AudioSys (*(XboxAudioSystem *)0x002ae598)

// XboxVoice::flags. Set by the dsnd* entry points, acted on (and cleared) by dsndUpdateVoices.
enum {
    VOICE_REQUEST_PLAY = 0x01, // start playing at the next dsndUpdateVoices
    VOICE_REQUEST_STOP = 0x02, // stop at the next dsndUpdateVoices
    VOICE_KEEP_ALIVE   = 0x04, // dsndMarkActive: do not free the slot when playback ends
    VOICE_PLAYING      = 0x08, // mirror of DSBSTATUS_PLAYING, refreshed every dsndUpdateVoices
    VOICE_STARTED      = 0x10, // Play has been issued at least once
    VOICE_PAUSED       = 0x20, // stopped by dsndSamplePause rather than really stopped
    VOICE_IN_USE       = 0x40, // what dsndIsPlaying reports; cleared once the voice is neither playing nor paused
};

// The I3DL2 reverb DSP program, and the 3D rolloff curve, both constant data in the XBE image. Referenced at
// their addresses rather than copied, matching how the D3D seam reaches the game's own shader/vertex data.
#define EffectsImage      ((const void *)0x00194840)
#define EffectsImageSize  0x6168u
#define RolloffCurve      ((const float *)0x0019a9a8) // { 1.0, 0.5, 0.25, 0.125, 0.0 }
#define RolloffCurvePoints 5u

// The original's own float constants for the volume table (0x0015d31c, 0x0015d3f8). Kept as floats because the
// original loads them as floats into the x87 stack, so the product is not a plain double 1500*log10(i/100).
static const float kVolumeTableHundredth = 0.01f;
static const float kVolumeTableScale     = 1500.0f;

// Xbox ADPCM: 4 bits per sample, 64 samples per block, so 36 bytes per block per channel.
#define ADPCM_BLOCK_BYTES_PER_CHANNEL 36
#define ADPCM_SAMPLES_PER_BLOCK       64
#define SAMPLE_RATE                   44032u

#define VOICE_COUNT 64

// ---------------------------------------------------------------------------------------------------------------
// Buffer creation
// ---------------------------------------------------------------------------------------------------------------

// Fills in the Xbox ADPCM wave format the game uses for every one of its sound buffers.
static void FillAdpcmFormat(XBOXADPCMWAVEFORMAT_Xbox *fmt, int numChannels) {
    fmt->wfx.wFormatTag = WAVE_FORMAT_XBOX_ADPCM;
    fmt->wfx.nChannels = (uint16_t)numChannels;
    fmt->wfx.nSamplesPerSec = SAMPLE_RATE;
    fmt->wfx.nBlockAlign = (uint16_t)(numChannels * ADPCM_BLOCK_BYTES_PER_CHANNEL);
    fmt->wfx.nAvgBytesPerSec = ((uint32_t)fmt->wfx.nBlockAlign * SAMPLE_RATE) >> 6;
    fmt->wfx.wBitsPerSample = 4;
    fmt->wfx.cbSize = 2;
    fmt->wSamplesPerBlock = ADPCM_SAMPLES_PER_BLOCK;
}

// The real body of dsndCreateSoundBufferWithSomeDefaultSettings (0x000e0f00), reached through the naked
// trampoline below. Note the original ignores ECX entirely - it only ever reads EDX and one stack argument.
uint32_t dsndCreateSoundBuffer(int numChannels, char is3d) {
    XBOXADPCMWAVEFORMAT_Xbox waveFormat;
    DSBUFFERDESC_Xbox bufferDesc;

    FillAdpcmFormat(&waveFormat, numChannels);

    memset(&bufferDesc, 0, sizeof(bufferDesc));
    bufferDesc.dwSize = sizeof(bufferDesc);
    bufferDesc.dwFlags |= (numChannels == 1 && is3d != 0) ? DSBCAPS_CTRL3D : 0u;
    bufferDesc.lpwfxFormat = &waveFormat.wfx;

    uint32_t bufferObj = 0;
    IDirectSound_CreateSoundBuffer(AudioSys.directSoundInst, &bufferDesc, &bufferObj, NULL);
    return bufferObj;
}

// The original takes its channel count in EDX and its 3D flag on the stack, and still leaves the stack cleanup
// to the caller (plain RET) - confirmed via raw disassembly of both the function and its one call site in
// xboxCreateSoundBuffers (0x000e1170: "PUSH EBX / MOV EDX,EBP / CALL 0x000e0f00 / ... / ADD ESP,0x4"). Ghidra
// reports it as __fastcall, which would be a callee-cleans RET 4 if compiled as such - hence a hand-written
// entry trampoline rather than an AUTOINJECT of a __fastcall function. Same pattern as d3dRenderTargetSetup.
//
// AUTOLTCG
void __declspec(naked) dsndCreateSoundBufferWithSomeDefaultSettings(void) {
    _asm {
        push ebp
        mov  ebp, esp
        movzx eax, byte ptr [ebp + 8] // the single stack argument: is3d
        push eax
        push edx                      // numChannels
        call dsndCreateSoundBuffer
        add  esp, 8
        pop  ebp
        ret                           // plain RET: the caller cleans up its own pushed argument
    }
}

// 192 buffers: per voice slot, one mono 2D, one stereo 2D and one mono 3D, all Xbox ADPCM at 44032 Hz. They are
// created once at boot and then re-bound to different sample data for the lifetime of the process - nothing ever
// releases them. The 2D ones get the six 5.1 mixbins with the centre channel muted and 6 dB of headroom; the 3D
// ones get the four cross-talk bins plus front centre plus the I3DL2 (reverb) bin, no headroom, and the game's
// own 5-point rolloff curve.
//
// AUTOINJECT
void __cdecl xboxCreateSoundBuffers(void) {
    for (int slot = 0; slot < VOICE_COUNT; slot++) {
        XBOXADPCMWAVEFORMAT_Xbox format2D;
        DSBUFFERDESC_Xbox desc2D;
        memset(&desc2D, 0, sizeof(desc2D));
        FillAdpcmFormat(&format2D, 1);
        desc2D.dwSize = sizeof(desc2D);
        desc2D.lpwfxFormat = &format2D.wfx;

        DSoundBuffer *buffer = NULL;
        IDirectSound_CreateSoundBuffer(AudioSys.directSoundInst, &desc2D, (uint32_t *)&buffer, NULL);
        AudioSys.buffersMono2D[slot] = buffer;

        AudioSys.buffersStereo2D[slot] = (DSoundBuffer *)dsndCreateSoundBuffer(2, 0);

        XBOXADPCMWAVEFORMAT_Xbox format3D;
        DSBUFFERDESC_Xbox desc3D;
        memset(&desc3D, 0, sizeof(desc3D));
        FillAdpcmFormat(&format3D, 1);
        desc3D.dwSize = sizeof(desc3D);
        desc3D.dwFlags |= DSBCAPS_CTRL3D;
        desc3D.lpwfxFormat = &format3D.wfx;

        buffer = NULL;
        IDirectSound_CreateSoundBuffer(AudioSys.directSoundInst, &desc3D, (uint32_t *)&buffer, NULL);
        AudioSys.buffers3D[slot] = buffer;
        IDirectSoundBuffer_SetRolloffCurve(buffer, RolloffCurve, RolloffCurvePoints, DS3D_IMMEDIATE);

        // XTLK + I3DL2 are what an Xbox 3D voice needs; front centre is Eurocom's own addition.
        DSMIXBINVOLUMEPAIR_Xbox pairs3D[6] = {
            { DSMIXBIN_XTLK_FRONT_LEFT,  0 },
            { DSMIXBIN_XTLK_BACK_LEFT,   0 },
            { DSMIXBIN_XTLK_FRONT_RIGHT, 0 },
            { DSMIXBIN_XTLK_BACK_RIGHT,  0 },
            { DSMIXBIN_FRONT_CENTER,     0 },
            { DSMIXBIN_I3DL2,            0 },
        };
        DSMIXBINS_Xbox mixBins3D = { 6, pairs3D };
        IDirectSoundBuffer_SetMixBins(AudioSys.buffers3D[slot], &mixBins3D);

        DSMIXBINVOLUMEPAIR_Xbox pairs2D[6] = {
            { DSMIXBIN_FRONT_LEFT,     0 },
            { DSMIXBIN_FRONT_RIGHT,    0 },
            { DSMIXBIN_FRONT_CENTER,   -10000 }, // muted: the 2D path pans through dsndSetPan instead
            { DSMIXBIN_LOW_FREQUENCY,  0 },
            { DSMIXBIN_BACK_LEFT,      0 },
            { DSMIXBIN_BACK_RIGHT,     0 },
        };
        DSMIXBINS_Xbox mixBins2D = { 6, pairs2D };
        IDirectSoundBuffer_SetMixBins(AudioSys.buffersMono2D[slot], &mixBins2D);
        IDirectSoundBuffer_SetMixBins(AudioSys.buffersStereo2D[slot], &mixBins2D);

        // Headroom is extra attenuation: trueVolume = mixbinVolume + 3DVolume + volume - headroom.
        IDirectSoundBuffer_SetHeadroom(AudioSys.buffers3D[slot], 0);
        IDirectSoundBuffer_SetHeadroom(AudioSys.buffersMono2D[slot], 600);
        IDirectSoundBuffer_SetHeadroom(AudioSys.buffersStereo2D[slot], 600);
    }
}

// One DirectSound object, full HRTF, the I3DL2 reverb DSP image, 192 buffers, and the volume table. Called once
// from Game_Main (src/action/main.cpp).
//
// The volume table maps a 0..100 game volume to hundredths of a dB: entry i is 1500*log10(i/100), clamped to
// -10000..0, with 0 and 100 written directly. The decompile of the x87 sequence reads as a multiply by
// log10(2), which is Ghidra mis-modelling FYL2X; the raw disassembly at 0x000e1b50 is FILD / FMUL 0.01 / FLDLG2
// / FXCH / FYL2X / FMUL 1500.0, i.e. a plain base-10 log.
//
// AUTOINJECT
void xboxInitSound(void) {
    // Decided once, before the first DSOUND entry point is touched - same place and reason as the graphics
    // backend switch in xboxInitGraphics.
    g_audioBackend = Settings_GetAudioBackend();
    printf("[dsndSeam] audio backend: %s\n", g_audioBackend == AUDIO_BACKEND_XAUDIO2 ? "xaudio2 (native)" : "cxbx (DSOUND HLE)");

    memset(&AudioSys, 0, sizeof(AudioSys));

    DirectSoundCreate(NULL, &AudioSys.directSoundInst, NULL);
    DirectSoundUseFullHRTF();

    AudioSys.effectImageLocation = NULL;
    AudioSys.maybeInitialised = 1;
    IDirectSound_DownloadEffectsImage(AudioSys.directSoundInst, EffectsImage, EffectsImageSize,
                                      &AudioSys.effectImageLocation, &AudioSys.effectImageDesc);

    xboxCreateSoundBuffers();

    AudioSys.volumeTable[0] = -10000;
    AudioSys.volumeTable[100] = 0;
    for (int i = 1; i < 100; i++) {
        int value = (int)((double)kVolumeTableScale * log10((double)i * (double)kVolumeTableHundredth));
        AudioSys.volumeTable[i] = value;
        if (value < -10000)
            AudioSys.volumeTable[i] = -10000;
        if (AudioSys.volumeTable[i] > 0)
            AudioSys.volumeTable[i] = 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The 3D listener
// ---------------------------------------------------------------------------------------------------------------

// AUTOINJECT
void __cdecl SetListenerPosition(float x, float y, float z) {
    AudioSys.listenerPosition[0] = x;
    AudioSys.listenerPosition[2] = z;
    AudioSys.listenerPosition[1] = y;
    IDirectSound_SetPosition(AudioSys.directSoundInst, x, y, z, DS3D_DEFERRED);
}

// The game never uses Doppler, so this only ever zeroes the listener velocity (deferred, like the rest of the
// 3D path). Called from psiSFXSetupListener (0x000e0baf), alongside the position/orientation setters.
//
// AUTOINJECT
void __cdecl SetListenerVelocity(void) {
    IDirectSound_SetVelocity(AudioSys.directSoundInst, 0.0f, 0.0f, 0.0f, DS3D_DEFERRED);
}

// AUTOINJECT
void __cdecl SetListenerOrientation(float xDir, float yDir, float zDir, float xUp, float yUp, float zUp) {
    float length = sqrtf(zDir * zDir + yDir * yDir + xDir * xDir);
    if (length <= 0.0f) {
        // Degenerate front vector: fall back to +Z, exactly as the original does (note it leaves zDir itself
        // alone and uses the cached component in the call below).
        xDir = 0.0f;
        yDir = 0.0f;
        AudioSys.listenerOrientation[2] = 1.0f;
    } else {
        float scale = 1.0f / length;
        xDir = xDir * scale;
        yDir = yDir * scale;
        AudioSys.listenerOrientation[2] = scale * zDir;
    }
    AudioSys.listenerOrientation[0] = xDir;
    AudioSys.listenerOrientation[1] = yDir;
    IDirectSound_SetOrientation(AudioSys.directSoundInst, xDir, yDir, AudioSys.listenerOrientation[2],
                                xUp, yUp, zUp, DS3D_DEFERRED);
}

// ---------------------------------------------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------------------------------------------

// True if the slot index is in range and the slot is bound to sample data. Every per-voice entry point below
// starts with this test, matching the original.
static inline bool VoiceInUse(uint32_t channel) {
    return channel < VOICE_COUNT && AudioSys.voices[channel].data != NULL;
}
static inline bool Voice3DInUse(uint32_t channel) {
    return VoiceInUse(channel) && AudioSys.voices[channel].is3d != 0;
}

// Picks the first free voice slot, binds the pre-made buffer that matches the requested channel count and 2D/3D
// mode, and points it at the caller's sample data. SetBufferData makes the buffer *reference* that memory - it
// is not copied - which is why maybeSoundShutdown has to unbind every buffer before a level's sound bank is
// freed. Returns the slot index, or -1.
//
// AUTOINJECT
int __cdecl dsndGetVoice(void *data, int size, uint32_t frequency, int numChannels, bool loop, char is3d) {
    if (data == NULL || size <= 0)
        return -1;

    if (numChannels != 1)
        is3d = false; // only the mono buffers are DSBCAPS_CTRL3D

    int channel = -1;
    for (int i = 0; i < VOICE_COUNT; i++) {
        if (AudioSys.voices[i].data == NULL) {
            channel = i;
            break;
        }
    }
    if (channel < 0)
        return -1;

    XboxVoice *voice = &AudioSys.voices[channel];
    memset(&voice->i3dl2source, 0, sizeof(voice->i3dl2source));
    voice->data = data;
    voice->size = (uint32_t)size;
    voice->frequency = frequency;
    voice->numChannels = (uint32_t)numChannels;
    voice->loop = loop ? 1 : 0;
    voice->is3d = (is3d != 0) ? 1 : 0;

    if (voice->is3d == 0) {
        voice->buffer = (numChannels == 1) ? AudioSys.buffersMono2D[channel] : AudioSys.buffersStereo2D[channel];
    } else {
        voice->buffer = AudioSys.buffers3D[channel];
        dsndSetI3DL2Source((uint32_t)channel, 0.0f);
        dsndSetDistances((uint32_t)channel, 10.0f, 100.0f);
        maybeXboxSFXCalculate3D((uint32_t)channel, 1); // inlined in the original, same guard and effect
    }

    IDirectSoundBuffer_SetBufferData(voice->buffer, voice->data, voice->size);
    IDirectSoundBuffer_SetFrequency(voice->buffer, voice->frequency);
    IDirectSoundBuffer_SetLoopRegion(voice->buffer, 0, 0);
    IDirectSoundBuffer_SetCurrentPosition(voice->buffer, 0);
    voice->flags |= VOICE_IN_USE;
    return channel;
}

// AUTOINJECT
bool __cdecl dsndIsPlaying(uint32_t channel) {
    if (channel >= VOICE_COUNT)
        return false;
    return (AudioSys.voices[channel].flags & VOICE_IN_USE) != 0;
}

// AUTOINJECT
void __cdecl dsndMarkActive(uint32_t channel) {
    if (VoiceInUse(channel))
        AudioSys.voices[channel].flags |= VOICE_KEEP_ALIVE;
}

// AUTOINJECT
void __cdecl dsndMarkInactive(uint32_t channel) {
    if (VoiceInUse(channel))
        AudioSys.voices[channel].flags &= ~(uint32_t)VOICE_KEEP_ALIVE;
}

// AUTOINJECT
void __cdecl psiSampleUnPause(uint32_t channel) {
    if (VoiceInUse(channel)) {
        uint32_t *flags = &AudioSys.voices[channel].flags;
        *flags = (*flags & ~(uint32_t)(VOICE_REQUEST_STOP | VOICE_PAUSED)) | VOICE_REQUEST_PLAY | VOICE_PLAYING;
    }
}

// FUN_000e1400: request a stop at the next dsndUpdateVoices. A nonzero "pause" only takes effect on a voice that is
// actually playing or has a play request pending; it sets VOICE_PAUSED, which keeps the slot allocated (see the
// VOICE_PLAYING|VOICE_PAUSED test in dsndUpdateVoices) so psiSampleUnPause can resume it. Anything else - including
// pause != 0 on an already-stopped voice - clears VOICE_IN_USE, so the slot is released.
//
// FUNC_AT(000e1400)
void __cdecl dsndSamplePause(uint32_t channel, uint8_t pause) {
    if (!VoiceInUse(channel))
        return;

    uint32_t *flags = &AudioSys.voices[channel].flags;
    uint32_t paused;
    if ((*flags & (VOICE_REQUEST_PLAY | VOICE_PLAYING)) == 0) {
        paused = 0;
        *flags &= ~(uint32_t)VOICE_IN_USE;
    } else {
        paused = pause;
        if (pause == 0)
            *flags &= ~(uint32_t)VOICE_IN_USE;
    }
    *flags = ((paused & 1) << 5) | (*flags & ~(uint32_t)(VOICE_REQUEST_PLAY | VOICE_PAUSED)) | VOICE_REQUEST_STOP;
}

// volume is the game's 0..100 scale. The "% 101" is the original's own (only) bounds handling - it is a signed
// remainder, so a negative volume still indexes backwards out of the table, exactly as before.
//
// AUTOINJECT
void __cdecl dsndBufferSetVolume(uint32_t channel, int volume) {
    if (VoiceInUse(channel))
        IDirectSoundBuffer_SetVolume(AudioSys.voices[channel].buffer, AudioSys.volumeTable[volume % 101]);
}

// AUTOINJECT
void __cdecl dsndSetFrequency(uint32_t channel, int frequency) {
    if (VoiceInUse(channel))
        IDirectSoundBuffer_SetFrequency(AudioSys.voices[channel].buffer, (uint32_t)frequency);
}

// The loop point has to land on an ADPCM block boundary (36 bytes per channel), and is dropped to 0 if it is
// past the end of the sample. The loop length is always 0, i.e. "to the end of the buffer".
//
// AUTOINJECT
void __cdecl dsndSetLoopRegion(uint32_t channel, int loopStart) {
    if (!VoiceInUse(channel))
        return;
    const XboxVoice *voice = &AudioSys.voices[channel];
    int blockAlign = (voice->numChannels == 2) ? 2 * ADPCM_BLOCK_BYTES_PER_CHANNEL : ADPCM_BLOCK_BYTES_PER_CHANNEL;
    int aligned = loopStart - loopStart % blockAlign;
    if (aligned >= (int)voice->size)
        aligned = 0;
    IDirectSoundBuffer_SetLoopRegion(voice->buffer, (uint32_t)aligned, 0);
}

// 2D panning, done by writing the six 5.1 mixbin volumes directly. leftRight and frontBack are each -100..100:
// positive leftRight attenuates the left pair, negative attenuates the right pair; positive frontBack attenuates
// the back pair, negative attenuates the front pair. The centre channel stays muted and the LFE unattenuated,
// matching the buffer's own mixbin setup.
//
// AUTOINJECT
void __cdecl dsndSetPan(uint32_t channel, int leftRight, int frontBack) {
    if (channel >= VOICE_COUNT)
        return;
    if (AudioSys.voices[channel].data == NULL || AudioSys.voices[channel].is3d != 0)
        return;

    int left = 100;
    if (leftRight > 0)
        left = 100 - leftRight;
    int right = (leftRight < 0) ? leftRight + 100 : 100;

    // Note the order: the back volumes are derived from the front ones *before* those are attenuated in turn.
    int backLeft = left;
    if (frontBack > 0)
        backLeft = ((100 - frontBack) * left) / 100;
    if (frontBack < 0)
        left = ((frontBack + 100) * left) / 100;

    int backRight = right;
    if (frontBack > 0)
        backRight = ((100 - frontBack) * right) / 100;
    if (frontBack < 0)
        right = ((frontBack + 100) * right) / 100;

    DSMIXBINVOLUMEPAIR_Xbox pairs[6] = {
        { DSMIXBIN_FRONT_LEFT,    AudioSys.volumeTable[left % 101] },
        { DSMIXBIN_FRONT_RIGHT,   AudioSys.volumeTable[right % 101] },
        { DSMIXBIN_FRONT_CENTER,  -10000 },
        { DSMIXBIN_LOW_FREQUENCY, 0 },
        { DSMIXBIN_BACK_LEFT,     AudioSys.volumeTable[backLeft % 101] },
        { DSMIXBIN_BACK_RIGHT,    AudioSys.volumeTable[backRight % 101] },
    };
    DSMIXBINS_Xbox mixBins = { 6, pairs };
    IDirectSoundBuffer_SetMixBinVolumes(AudioSys.voices[channel].buffer, &mixBins);
}

// AUTOINJECT
void *__cdecl dsndGetData(uint32_t channel) {
    if (channel >= VOICE_COUNT)
        return NULL;
    return AudioSys.voices[channel].data;
}

// AUTOINJECT
uint32_t __cdecl psiStreamGetPlayPos(uint32_t channel) {
    if (!VoiceInUse(channel))
        return 0;
    uint32_t playCursor = 0;
    IDirectSoundBuffer_GetCurrentPosition(AudioSys.voices[channel].buffer, &playCursor, NULL);
    return playCursor;
}

// FUN_000e18a0: writes new sample data straight into the memory the voice's buffer is already referencing (the
// streaming path - the buffer keeps playing while this overwrites the part it has passed).
//
// FUNC_AT(000e18a0)
void __cdecl dsndWriteVoiceData(uint32_t channel, int offset, const void *src, uint32_t length) {
    if (!VoiceInUse(channel) || src == NULL || (int)length <= 0)
        return;
    void *data = AudioSys.voices[channel].data;
    memcpy((char *)data + offset, src, length);

    // A native backend has already converted this buffer's sample data into something it can play, so it has
    // to be told when the game edits the ADPCM underneath it. Nothing here goes through DirectSound, so this
    // is the only point at which that is visible. In cxbx mode the DSOUND buffer references the game's memory
    // directly and there is nothing to do.
    if (g_audioBackend != AUDIO_BACKEND_CXBX)
        XA2_NotifyBufferDataWritten(data, (uint32_t)offset, length);
}

// ---------------------------------------------------------------------------------------------------------------
// Per-voice 3D state
// ---------------------------------------------------------------------------------------------------------------

// AUTOINJECT
void __cdecl dsndSetPosition(uint32_t channel, float x, float y, float z) {
    if (Voice3DInUse(channel))
        IDirectSoundBuffer_SetPosition(AudioSys.voices[channel].buffer, x, y, z, DS3D_DEFERRED);
}

// AUTOINJECT
void __cdecl dsndClearVelocity(uint32_t channel) {
    if (Voice3DInUse(channel))
        IDirectSoundBuffer_SetVelocity(AudioSys.voices[channel].buffer, 0.0f, 0.0f, 0.0f, DS3D_DEFERRED);
}

// The max distance is set twice on purpose (the original does the same): once to a huge value, so that the new
// min distance can never be rejected for exceeding the old max, then to the real value.
//
// AUTOINJECT
void __cdecl dsndSetDistances(uint32_t channel, float minDist, float maxDist) {
    if (!Voice3DInUse(channel))
        return;
    if (minDist < 0.001f)
        minDist = 0.001f;
    if (maxDist <= minDist)
        maxDist = minDist + 0.001f;
    DSoundBuffer *buffer = AudioSys.voices[channel].buffer;
    IDirectSoundBuffer_SetMaxDistance(buffer, 999999.0f, DS3D_DEFERRED);
    IDirectSoundBuffer_SetMinDistance(buffer, minDist, DS3D_DEFERRED);
    IDirectSoundBuffer_SetMaxDistance(buffer, maxDist, DS3D_DEFERRED);
}

// The reverb (I3DL2) send level for one voice, as a 0..100 game volume. Note the original indexes the volume
// table with the truncated float and no bounds check at all - kept as-is; the callers only ever pass 0..100.
//
// AUTOINJECT
void __cdecl dsndSetI3DL2Source(uint32_t channel, float volume) {
    if (!Voice3DInUse(channel))
        return;
    XboxVoice *voice = &AudioSys.voices[channel];
    voice->i3dl2source.lRoom = AudioSys.volumeTable[(int)volume];
    IDirectSoundBuffer_SetI3DL2Source(voice->buffer, &voice->i3dl2source, DS3D_DEFERRED);
}

// Marks a 3D voice as having its own world position (positioned != 0) or as following the listener
// (positioned == 0, which is what dsndUpdateVoices's first loop implements). Called from psiInitialiseSound.
//
// AUTOINJECT
void __cdecl maybeXboxSFXCalculate3D(uint32_t channel, uint8_t positioned) {
    if (Voice3DInUse(channel))
        AudioSys.voices[channel].positioned = positioned;
}

// ---------------------------------------------------------------------------------------------------------------
// Streams
// ---------------------------------------------------------------------------------------------------------------

// AUTOINJECT
void __cdecl BackgroundMovieSetupMix(DSoundStream *stream) {
    if (stream == NULL)
        return;
    DSMIXBINVOLUMEPAIR_Xbox pairs[6] = {
        { DSMIXBIN_FRONT_LEFT,    0 },
        { DSMIXBIN_FRONT_RIGHT,   0 },
        { DSMIXBIN_FRONT_CENTER,  -10000 },
        { DSMIXBIN_LOW_FREQUENCY, 0 },
        { DSMIXBIN_BACK_LEFT,     0 },
        { DSMIXBIN_BACK_RIGHT,    0 },
    };
    DSMIXBINS_Xbox mixBins = { 6, pairs };
    IDirectSoundStream_SetMixBins(stream, &mixBins);
}

// AUTOINJECT
void __cdecl dsndStreamSetVolume(DSoundStream *stream, int volume) {
    if (stream != NULL)
        IDirectSoundStream_SetVolume(stream, AudioSys.volumeTable[volume % 101]);
}

// ---------------------------------------------------------------------------------------------------------------
// The per-frame update
// ---------------------------------------------------------------------------------------------------------------

// Three passes over the voice table, in the original's order:
//  1. every 3D voice that has no world position of its own is parked one unit in front of the listener
//     (listenerPosition + listenerOrientation, the orientation being a unit vector), then the deferred 3D
//     settings from this frame are committed and DSOUND's own work queue is serviced;
//  2. pending play/stop requests are issued and cleared;
//  3. each voice's playing flag is refreshed from the hardware, and a voice that is neither playing nor paused
//     loses VOICE_IN_USE - and, unless something called dsndMarkActive on it, has its slot freed.
//
// FUNC_AT(000e19c0)
void dsndUpdateVoices(void) {
    for (int i = 0; i < VOICE_COUNT; i++) {
        XboxVoice *voice = &AudioSys.voices[i];
        if (voice->data != NULL && voice->is3d != 0 && voice->positioned == 0) {
            IDirectSoundBuffer_SetPosition(voice->buffer,
                                           AudioSys.listenerOrientation[0] + AudioSys.listenerPosition[0],
                                           AudioSys.listenerOrientation[1] + AudioSys.listenerPosition[1],
                                           AudioSys.listenerOrientation[2] + AudioSys.listenerPosition[2],
                                           DS3D_DEFERRED);
        }
    }

    IDirectSound_CommitDeferredSettings(AudioSys.directSoundInst);
    DirectSoundDoWork();

    AudioSys.activeVoiceCount = 0;
    for (int i = 0; i < VOICE_COUNT; i++) {
        XboxVoice *voice = &AudioSys.voices[i];
        if (voice->data == NULL) {
            voice->flags = 0;
            continue;
        }
        AudioSys.activeVoiceCount++;
        if ((voice->flags & VOICE_REQUEST_STOP) != 0) {
            IDirectSoundBuffer_Stop(voice->buffer);
        } else if ((voice->flags & VOICE_REQUEST_PLAY) != 0) {
            IDirectSoundBuffer_Play(voice->buffer, 0, 0, voice->loop != 0 ? 1 : 0);
            voice->flags |= VOICE_STARTED;
        }
        voice->flags &= ~(uint32_t)(VOICE_REQUEST_PLAY | VOICE_REQUEST_STOP);
    }

    for (int i = 0; i < VOICE_COUNT; i++) {
        XboxVoice *voice = &AudioSys.voices[i];
        if (voice->data == NULL)
            continue;
        uint32_t status = 0;
        IDirectSoundBuffer_GetStatus(voice->buffer, &status);
        voice->flags = (voice->flags & ~(uint32_t)VOICE_PLAYING) |
                       (((status & DSBSTATUS_PLAYING) != 0) ? VOICE_PLAYING : 0u);
        if ((voice->flags & (VOICE_PLAYING | VOICE_PAUSED)) != 0)
            continue;
        voice->flags &= ~(uint32_t)VOICE_IN_USE;
        if ((voice->flags & VOICE_KEEP_ALIVE) == 0) {
            voice->data = NULL;
            voice->buffer = NULL;
        }
    }

    DSoundSeamTraceEndFrame();
}

// Level change / relaunch. The important part is dropping every buffer's reference to the game's sample data:
// SetBufferData does not copy, so a buffer left pointing at a sound bank that is about to be freed would have
// the hardware reading freed memory. Requests a stop on every live voice, runs one final dsndUpdateVoices to actually
// issue those stops, then clears the whole voice table.
//
// AUTOINJECT
void __cdecl maybeSoundShutdown(void) {
    AudioSys.listenerPosition[0] = 0.0f;
    AudioSys.listenerPosition[1] = 0.0f;
    AudioSys.listenerPosition[2] = 0.0f;
    IDirectSound_SetPosition(AudioSys.directSoundInst, 0.0f, 0.0f, 0.0f, DS3D_DEFERRED);
    IDirectSound_SetVelocity(AudioSys.directSoundInst, 0.0f, 0.0f, 0.0f, DS3D_DEFERRED);
    SetListenerOrientation(0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f);

    for (int i = 0; i < VOICE_COUNT; i++) {
        if (AudioSys.voices[i].data != NULL) {
            uint32_t *flags = &AudioSys.voices[i].flags;
            *flags = (*flags & ~(uint32_t)(VOICE_REQUEST_PLAY | VOICE_PAUSED | VOICE_IN_USE)) | VOICE_REQUEST_STOP;
        }
        if (AudioSys.buffersMono2D[i] != NULL)
            IDirectSoundBuffer_SetBufferData(AudioSys.buffersMono2D[i], NULL, 0);
        if (AudioSys.buffersStereo2D[i] != NULL)
            IDirectSoundBuffer_SetBufferData(AudioSys.buffersStereo2D[i], NULL, 0);
        if (AudioSys.buffers3D[i] != NULL)
            IDirectSoundBuffer_SetBufferData(AudioSys.buffers3D[i], NULL, 0);
    }

    dsndUpdateVoices();

    memset(AudioSys.voices, 0, sizeof(AudioSys.voices));

    DSoundSeamTraceLevelReset();
    if (g_audioBackend != AUDIO_BACKEND_CXBX)
        DSoundPrintMissingSummary();
}
