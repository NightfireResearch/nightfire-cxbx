#include "dsndStream.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include "xadpcm.h"
#include "xaudio2Backend.h"
#include "../engine/XboxStartup.h"

// See dsndStream.h for why this exists at all.

// ---------------------------------------------------------------------------------------------------------------
// The Xbox side of the interface
//
// Layouts taken from the XBE's own use of them rather than from a header. XMEDIAPACKET is six dwords, which is
// what DSOUND's own packet-attach code copies; DSSTREAMDESC is what maybeSFXCreateStreamForVideo fills in.
// ---------------------------------------------------------------------------------------------------------------

struct XMediaPacket {
    void     *pvBuffer;
    uint32_t  dwMaxSize;
    uint32_t *pdwCompletedSize;
    uint32_t *pdwStatus;
    void     *pContext;          // passed back to the callback; the decoder puts a timestamp here
    uint32_t  reserved;
};

typedef void (__stdcall *StreamCallbackFn)(void *streamContext, void *packetContext, uint32_t status);

struct DSStreamDesc {
    uint32_t          dwFlags;
    uint32_t          dwMaxAttachedPackets;
    WAVEFORMATEX     *lpwfxFormat;
    StreamCallbackFn  lpfnCallback;
    void             *lpvContext;
    void             *lpMixBins;
};

// The decoder polls for PENDING to decide whether a stream still owes it anything, so these values have to be
// exactly the ones it compares against.
#define XMP_STATUS_SUCCESS 0x00000000u
#define XMP_STATUS_PENDING 0x8000000Au
#define XMP_STATUS_FLUSHED 0x80004004u

#define DS_OK          0x00000000u
#define DSERR_GENERIC  0x80004005u

#define WAVE_FORMAT_XBOX_ADPCM 0x0069

// Pause modes, as IDirectSoundStream_Pause takes them.
#define DSSTREAMPAUSE_RESUME         0
#define DSSTREAMPAUSE_PAUSE          1
#define DSSTREAMPAUSE_SYNCHPLAYBACK  2

static void StreamLog(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
}

// ---------------------------------------------------------------------------------------------------------------
// A stream
// ---------------------------------------------------------------------------------------------------------------

struct NativeStream;

// One packet the decoder has handed us and not yet had back. The decoded audio is owned here, because
// XAudio2 reads from the buffer for as long as it is queued and the decoder reuses its own memory
// immediately.
struct PendingPacket {
    PendingPacket *next;
    uint32_t      *pdwCompletedSize;
    uint32_t      *pdwStatus;
    void          *packetContext;
    uint32_t       sourceBytes;
    int16_t       *pcm;
    volatile LONG  finished;      // set from XAudio2's thread, read from the game's
};

// XAudio2 reports buffer completion on its own audio thread. Nothing is done there beyond flagging the
// packet, so that everything the game can observe - the status word, the callback - happens on the game's
// thread inside DirectSoundDoWork, which is where the decoder expects progress.
//
// One shared instance serves every stream, because the only thing it needs is the packet that finished and
// XAudio2 hands that back as the buffer's context. It must not be a member of the stream struct: the streams
// are allocated with calloc, which does not run a constructor, so a class with a vtable placed inside one
// would be called through a null vtable pointer - which is what XAudio2 faulted on the first time this ran.
struct StreamVoiceCallback : public IXAudio2VoiceCallback {
    void __stdcall OnBufferEnd(void *bufferContext) override {
        if (bufferContext != NULL)
            InterlockedExchange(&((PendingPacket *)bufferContext)->finished, 1);
    }
    void __stdcall OnVoiceProcessingPassStart(UINT32) override {}
    void __stdcall OnVoiceProcessingPassEnd() override {}
    void __stdcall OnStreamEnd() override {}
    void __stdcall OnBufferStart(void *) override {}
    void __stdcall OnLoopEnd(void *) override {}
    void __stdcall OnVoiceError(void *, HRESULT) override {}
};

static StreamVoiceCallback g_voiceCallback;

struct NativeStream {
    // The first two words are the object's vtables, because that is where the XBE's own stream object keeps
    // them and the decoder calls straight through the first one.
    void *vtable;
    void *secondaryVtable;

    LONG  refCount;
    IXAudio2SourceVoice *voice;

    int      channels;
    uint32_t sampleRate;
    bool     adpcm;

    StreamCallbackFn callback;
    void            *callbackContext;

    CRITICAL_SECTION lock;
    PendingPacket   *head;      // oldest first, so packets complete in submission order
    PendingPacket   *tail;

    bool waitingForSynch;       // paused by Pause(SYNCHPLAYBACK), started by IDirectSound_SynchPlayback
};

// Every live stream, so that DoWork and SynchPlayback can reach them all. There are only ever a handful (one
// per audio track of the movie being played), so a fixed table is plenty and avoids allocating.
#define MAX_STREAMS 8
static NativeStream *g_streams[MAX_STREAMS];
static CRITICAL_SECTION g_streamsLock;
static bool g_initialised = false;

static void EnsureInitialised(void) {
    if (!g_initialised) {
        InitializeCriticalSection(&g_streamsLock);
        memset(g_streams, 0, sizeof(g_streams));
        g_initialised = true;
    }
}

static void RegisterStream(NativeStream *stream) {
    EnterCriticalSection(&g_streamsLock);
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (g_streams[i] == NULL) { g_streams[i] = stream; break; }
    }
    LeaveCriticalSection(&g_streamsLock);
}

static void UnregisterStream(NativeStream *stream) {
    EnterCriticalSection(&g_streamsLock);
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (g_streams[i] == stream) { g_streams[i] = NULL; break; }
    }
    LeaveCriticalSection(&g_streamsLock);
}

// ---------------------------------------------------------------------------------------------------------------
// Packet completion
// ---------------------------------------------------------------------------------------------------------------

// Retires every packet at the head of the queue whose audio has played out. Order matters: the decoder
// measures its audio/video sync offset from when a packet completes, so completing out of order would skew
// it. Done under the stream's lock, with the callback invoked outside it - the callback is the decoder's and
// has no business running while a lock of ours is held.
static void CompleteFinishedPackets(NativeStream *stream) {
    for (;;) {
        PendingPacket *packet = NULL;

        EnterCriticalSection(&stream->lock);
        if (stream->head != NULL && stream->head->finished) {
            packet = stream->head;
            stream->head = packet->next;
            if (stream->head == NULL)
                stream->tail = NULL;
        }
        LeaveCriticalSection(&stream->lock);

        if (packet == NULL)
            return;

        if (packet->pdwCompletedSize != NULL)
            *packet->pdwCompletedSize = packet->sourceBytes;
        if (packet->pdwStatus != NULL)
            *packet->pdwStatus = XMP_STATUS_SUCCESS;

        if (stream->callback != NULL)
            stream->callback(stream->callbackContext, packet->packetContext, XMP_STATUS_SUCCESS);

        free(packet->pcm);
        free(packet);
    }
}

// Abandons everything queued, which is what closing a stream mid-movie has to do. The packets are reported as
// flushed rather than successful so the decoder does not treat them as played.
static void DiscardPackets(NativeStream *stream) {
    if (stream->voice != NULL) {
        stream->voice->Stop(0, 0);
        stream->voice->FlushSourceBuffers();
    }

    for (;;) {
        PendingPacket *packet = NULL;

        EnterCriticalSection(&stream->lock);
        if (stream->head != NULL) {
            packet = stream->head;
            stream->head = packet->next;
            if (stream->head == NULL)
                stream->tail = NULL;
        }
        LeaveCriticalSection(&stream->lock);

        if (packet == NULL)
            return;

        if (packet->pdwCompletedSize != NULL)
            *packet->pdwCompletedSize = 0;
        if (packet->pdwStatus != NULL)
            *packet->pdwStatus = XMP_STATUS_FLUSHED;
        if (stream->callback != NULL)
            stream->callback(stream->callbackContext, packet->packetContext, XMP_STATUS_FLUSHED);

        free(packet->pcm);
        free(packet);
    }
}

void DSoundStream_DoWork(void) {
    if (!g_initialised)
        return;

    EnterCriticalSection(&g_streamsLock);
    NativeStream *snapshot[MAX_STREAMS];
    memcpy(snapshot, g_streams, sizeof(snapshot));
    LeaveCriticalSection(&g_streamsLock);

    for (int i = 0; i < MAX_STREAMS; i++) {
        if (snapshot[i] != NULL)
            CompleteFinishedPackets(snapshot[i]);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The stream's methods, as the decoder reaches them through the object's vtable
// ---------------------------------------------------------------------------------------------------------------

static void DestroyStream(NativeStream *stream) {
    UnregisterStream(stream);
    DiscardPackets(stream);

    if (stream->voice != NULL) {
        stream->voice->DestroyVoice();
        stream->voice = NULL;
    }
    DeleteCriticalSection(&stream->lock);
    free(stream);
}

static ULONG __stdcall Stream_AddRef(NativeStream *stream) {
    return (ULONG)InterlockedIncrement(&stream->refCount);
}

static ULONG __stdcall Stream_Release(NativeStream *stream) {
    LONG remaining = InterlockedDecrement(&stream->refCount);
    if (remaining <= 0) {
        DestroyStream(stream);
        return 0;
    }
    return (ULONG)remaining;
}

// Vtable slot 4. Takes a packet, turns it into audio and queues it, and leaves it pending until XAudio2 says
// it has been played - which is what keeps the movie's video in step with its sound.
//
// Three parameters, not two. CDirectSoundStream_Process ends RET 0xc and only ever reads the first two, so
// the third is there and ignored. Declaring it with two was worth one debugging cycle: the callee then popped
// four bytes fewer than the caller pushed, and the decoder returned into a corrupted frame rather than
// failing anywhere near here. The plan's standing warning about checking RET immediates applies to vtable
// slots exactly as it does to exported entry points.
static uint32_t __stdcall Stream_Process(NativeStream *stream, XMediaPacket *packet, void *unused) {
    (void)unused;
    if (stream == NULL || packet == NULL)
        return DSERR_GENERIC;

    // Claim it straight away. If anything below fails the packet is completed rather than left pending, or
    // the decoder would wait for it forever.
    if (packet->pdwStatus != NULL)
        *packet->pdwStatus = XMP_STATUS_PENDING;
    if (packet->pdwCompletedSize != NULL)
        *packet->pdwCompletedSize = 0;

    uint32_t sourceBytes = packet->dwMaxSize;
    if (stream->voice == NULL || packet->pvBuffer == NULL || sourceBytes == 0) {
        if (packet->pdwCompletedSize != NULL)
            *packet->pdwCompletedSize = sourceBytes;
        if (packet->pdwStatus != NULL)
            *packet->pdwStatus = XMP_STATUS_SUCCESS;
        if (stream->callback != NULL)
            stream->callback(stream->callbackContext, packet->pContext, XMP_STATUS_SUCCESS);
        return DS_OK;
    }

    size_t valueCount;
    int16_t *pcm;
    if (stream->adpcm) {
        valueCount = XAdpcm_DecodedValueCount(sourceBytes, stream->channels);
        pcm = (int16_t *)malloc(valueCount * sizeof(int16_t));
        if (pcm != NULL)
            valueCount = XAdpcm_Decode(packet->pvBuffer, sourceBytes, stream->channels, pcm, valueCount);
    } else {
        valueCount = sourceBytes / sizeof(int16_t);
        pcm = (int16_t *)malloc(valueCount * sizeof(int16_t));
        if (pcm != NULL)
            memcpy(pcm, packet->pvBuffer, valueCount * sizeof(int16_t));
    }

    PendingPacket *pending = (pcm != NULL) ? (PendingPacket *)calloc(1, sizeof(PendingPacket)) : NULL;
    if (pending == NULL) {
        free(pcm);
        if (packet->pdwCompletedSize != NULL)
            *packet->pdwCompletedSize = sourceBytes;
        if (packet->pdwStatus != NULL)
            *packet->pdwStatus = XMP_STATUS_SUCCESS;
        if (stream->callback != NULL)
            stream->callback(stream->callbackContext, packet->pContext, XMP_STATUS_SUCCESS);
        return DS_OK;
    }

    pending->pdwCompletedSize = packet->pdwCompletedSize;
    pending->pdwStatus = packet->pdwStatus;
    pending->packetContext = packet->pContext;
    pending->sourceBytes = sourceBytes;
    pending->pcm = pcm;

    // Queued before submitting, so that a buffer which finishes immediately still finds its record.
    EnterCriticalSection(&stream->lock);
    if (stream->tail != NULL)
        stream->tail->next = pending;
    else
        stream->head = pending;
    stream->tail = pending;
    LeaveCriticalSection(&stream->lock);

    XAUDIO2_BUFFER buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.AudioBytes = (UINT32)(valueCount * sizeof(int16_t));
    buffer.pAudioData = (const BYTE *)pcm;
    buffer.pContext = pending;

    HRESULT hr = stream->voice->SubmitSourceBuffer(&buffer, NULL);
    if (FAILED(hr)) {
        StreamLog("[dsndStream] SubmitSourceBuffer failed: 0x%08lx\n", hr);
        InterlockedExchange(&pending->finished, 1);   // let DoWork retire it rather than stranding it
    }
    return DS_OK;
}

// Vtable slot 5. The decoder calls this when a stream has no more data coming - at the end of a movie, and
// whenever it stops feeding one of the tracks. It means "that was the last packet", not "throw away what is
// queued", so whatever is still playing is left to play out. Release does the real teardown.
static uint32_t __stdcall Stream_Discontinuity(NativeStream *stream) {
    (void)stream;
    return DS_OK;
}

// Anything else on the interface. The decoder only uses AddRef, Release, Process and Discontinuity, and the
// game's own stream calls come in through the exported entry points rather than the vtable, so reaching here
// means something does that this has not seen. It says so and stops, rather than returning and unbalancing
// the stack by an unknown amount - the same trade the loader's kernel stubs make, and for the same reason.
static void __cdecl Stream_UnknownSlot(unsigned slot) {
    StreamLog("\n[dsndStream] a DirectSound stream method this does not implement was called: vtable slot %u.\n"
              "[dsndStream] Implement it in src/action/sound/dsndStream.cpp.\n\n", slot);
    if (IsDebuggerPresent())
        DebugBreak();
    ExitProcess(1);
}

#define UNKNOWN_SLOT(n) static void __stdcall Stream_Slot##n(void) { Stream_UnknownSlot(n); }
UNKNOWN_SLOT(2)  UNKNOWN_SLOT(3)  UNKNOWN_SLOT(6)  UNKNOWN_SLOT(7)
UNKNOWN_SLOT(8)  UNKNOWN_SLOT(9)  UNKNOWN_SLOT(10) UNKNOWN_SLOT(11)
UNKNOWN_SLOT(12) UNKNOWN_SLOT(13) UNKNOWN_SLOT(14) UNKNOWN_SLOT(15)

// Laid out exactly like the XBE's own stream vtable, so a decoder call on any slot lands on the matching
// function here. Sixteen entries, as read out of the image at 0x0016264c.
static void *g_streamVtable[16] = {
    (void *)Stream_AddRef,          // 0
    (void *)Stream_Release,         // 1
    (void *)Stream_Slot2,
    (void *)Stream_Slot3,
    (void *)Stream_Process,         // 4
    (void *)Stream_Discontinuity,   // 5
    (void *)Stream_Slot6,
    (void *)Stream_Slot7,
    (void *)Stream_Slot8,
    (void *)Stream_Slot9,
    (void *)Stream_Slot10,
    (void *)Stream_Slot11,
    (void *)Stream_Slot12,
    (void *)Stream_Slot13,
    (void *)Stream_Slot14,
    (void *)Stream_Slot15,
};

// ---------------------------------------------------------------------------------------------------------------
// The DSOUND entry points the decoder calls directly
// ---------------------------------------------------------------------------------------------------------------

static uint32_t __stdcall Hook_DirectSoundCreateStream(DSStreamDesc *pdssd, NativeStream **ppStream) {
    EnsureInitialised();

    if (pdssd == NULL || ppStream == NULL || pdssd->lpwfxFormat == NULL)
        return DSERR_GENERIC;

    const WAVEFORMATEX *format = pdssd->lpwfxFormat;

    NativeStream *stream = (NativeStream *)calloc(1, sizeof(NativeStream));
    if (stream == NULL)
        return DSERR_GENERIC;

    stream->vtable = g_streamVtable;
    stream->secondaryVtable = NULL;
    stream->refCount = 1;
    stream->channels = (format->nChannels > 0) ? format->nChannels : 1;
    stream->sampleRate = (format->nSamplesPerSec > 0) ? format->nSamplesPerSec : 22050;
    stream->adpcm = (format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM);
    stream->callback = pdssd->lpfnCallback;
    stream->callbackContext = pdssd->lpvContext;
    InitializeCriticalSection(&stream->lock);

    // Whatever the source format, what is queued is 16-bit PCM: ADPCM is decoded here, and the only other
    // format the decoder uses is already 16-bit.
    IXAudio2 *xaudio = XA2_GetDevice();
    if (xaudio != NULL) {
        WAVEFORMATEX wfx;
        memset(&wfx, 0, sizeof(wfx));
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = (WORD)stream->channels;
        wfx.nSamplesPerSec = stream->sampleRate;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = (WORD)(wfx.nChannels * wfx.wBitsPerSample / 8);
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

        HRESULT hr = xaudio->CreateSourceVoice(&stream->voice, &wfx, 0, 2.0f, &g_voiceCallback);
        if (FAILED(hr)) {
            StreamLog("[dsndStream] CreateSourceVoice failed: 0x%08lx - this stream will be silent\n", hr);
            stream->voice = NULL;
        } else {
            stream->voice->Start(0, 0);
        }
    }

    RegisterStream(stream);
    *ppStream = stream;

    StreamLog("[dsndStream] stream %p: %d channel %s at %u Hz, %u packets\n",
              stream, stream->channels, stream->adpcm ? "ADPCM" : "PCM",
              stream->sampleRate, pdssd->dwMaxAttachedPackets);
    return DS_OK;
}

// Millibels, as every DirectSound volume is: 0 is unattenuated and -10000 is silence.
static uint32_t __stdcall Hook_IDirectSoundStream_SetVolume(NativeStream *stream, int32_t millibels) {
    if (stream != NULL && stream->voice != NULL) {
        float amplitude = (millibels <= -10000) ? 0.0f : powf(10.0f, (float)millibels / 2000.0f);
        stream->voice->SetVolume(amplitude);
    }
    return DS_OK;
}

// The 5.1 mixbin routing the game asks for. The backend's output is stereo and its own buffers already
// collapse mixbins onto that, so there is nothing here to honour yet.
static uint32_t __stdcall Hook_IDirectSoundStream_SetMixBins(NativeStream *stream, void *pMixBins) {
    (void)stream;
    (void)pMixBins;
    return DS_OK;
}

static uint32_t __stdcall Hook_IDirectSoundStream_Pause(NativeStream *stream, uint32_t mode) {
    if (stream == NULL || stream->voice == NULL)
        return DS_OK;

    switch (mode) {
    case DSSTREAMPAUSE_RESUME:
        stream->waitingForSynch = false;
        stream->voice->Start(0, 0);
        break;
    case DSSTREAMPAUSE_PAUSE:
        stream->waitingForSynch = false;
        stream->voice->Stop(0, 0);
        break;
    case DSSTREAMPAUSE_SYNCHPLAYBACK:
        // Hold until every stream is ready, so that a movie's tracks start together.
        stream->waitingForSynch = true;
        stream->voice->Stop(0, 0);
        break;
    default:
        break;
    }
    return DS_OK;
}

// The decoder creates a DirectSound object purely to call SynchPlayback on it, then releases it. There is
// nothing to create, so this hands back a token that the two hooks below recognise and ignore.
static void *g_synchToken = (void *)&g_synchToken;

static uint32_t __stdcall Hook_DirectSoundCreate(void *lpGuid, void **ppDS, void *pUnknown) {
    (void)lpGuid;
    (void)pUnknown;   // three parameters, not the two Ghidra reports - RET 0xc; see dsndSeam.cpp
    if (ppDS != NULL)
        *ppDS = g_synchToken;
    return DS_OK;
}

static uint32_t __stdcall Hook_IDirectSound_SynchPlayback(void *thisPtr) {
    (void)thisPtr;
    EnsureInitialised();

    EnterCriticalSection(&g_streamsLock);
    for (int i = 0; i < MAX_STREAMS; i++) {
        NativeStream *stream = g_streams[i];
        if (stream != NULL && stream->waitingForSynch) {
            stream->waitingForSynch = false;
            if (stream->voice != NULL)
                stream->voice->Start(0, 0);
        }
    }
    LeaveCriticalSection(&g_streamsLock);
    return DS_OK;
}

static uint32_t __stdcall Hook_IDirectSound_Release(void *thisPtr) {
    (void)thisPtr;
    return 0;
}

static void __stdcall Hook_DirectSoundDoWork(void) {
    DSoundStream_DoWork();
}

// ---------------------------------------------------------------------------------------------------------------
// Installing the hooks
// ---------------------------------------------------------------------------------------------------------------

bool DSoundStream_RunningStandalone(void) {
    // One definition of the rule, in the startup layer that owns the question. If CXBX is hosting this
    // process then the XBE's DSOUND has already been replaced by CXBX's own patches and nothing here should
    // touch it; if it is not, the XBE's real DSOUND - hardware registers and all - is what these entry points
    // would otherwise reach.
    return Xbox_RunningStandalone();
}

static void WriteJump(uint32_t address, void *target) {
    uint8_t *site = (uint8_t *)address;
    DWORD previous = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) {
        StreamLog("[dsndStream] could not make 0x%08x writable (error %lu)\n", address, GetLastError());
        return;
    }
    site[0] = 0xE9;                                                   // jmp rel32
    *(int32_t *)(site + 1) = (int32_t)((uint8_t *)target - (site + 5));
}

// Each of these was checked against the RET immediate of the entry point it replaces, which is the only
// reliable source for an Xbox library's parameter count:
//
//     DirectSoundCreateStream        RET 8     2 parameters
//     IDirectSoundStream_SetVolume   RET 8     2
//     IDirectSoundStream_SetMixBins  RET 8     2
//     IDirectSoundStream_Pause       RET 8     2
//     DirectSoundDoWork              RET       0
//     DirectSoundCreate              RET 0xc   3  (Ghidra says 2)
//     IDirectSound_SynchPlayback     RET 4     1
//     IDirectSound_Release           RET 4     1
//
// and, on the stream's own vtable: AddRef, Release and Discontinuity RET 4, Process RET 0xc.
void DSoundStream_InstallHooks(void) {
    if (!DSoundStream_RunningStandalone())
        return;

    EnsureInitialised();

    // These are patched at the DSOUND entry points themselves, not through AUTOINJECT, because they have to
    // be conditional: under CXBX the same addresses carry CXBX's patches and overwriting them would take FMV
    // audio away from the host that is handling it.
    WriteJump(0x001148ff, (void *)Hook_DirectSoundCreateStream);
    WriteJump(0x001134ee, (void *)Hook_IDirectSoundStream_SetVolume);
    WriteJump(0x001134f3, (void *)Hook_IDirectSoundStream_SetMixBins);
    WriteJump(0x001134f8, (void *)Hook_IDirectSoundStream_Pause);
    WriteJump(0x001134fd, (void *)Hook_DirectSoundDoWork);
    WriteJump(0x001148b8, (void *)Hook_DirectSoundCreate);
    WriteJump(0x001133b2, (void *)Hook_IDirectSound_SynchPlayback);
    WriteJump(0x00112733, (void *)Hook_IDirectSound_Release);

    StreamLog("[dsndStream] DirectSound stream entry points hooked (no CXBX in this process)\n");
}
