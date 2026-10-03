#include "Streams.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EA's sound library, module D (docs/driving/sound.md 2.5, 3.5, 4.4): streams on the SND side.
//
// AStreamPriv makes a stream (SNDSTRM_create: the SNDSTRMI record, its request records, a packet player and a STREAM
// ring in one block of memory it supplies) and AStream queues files on it (SNDSTRM_queuefile -> STREAM_queuefile;
// the FILESYS worker fills the ring). Every main-thread tick SNDSTRMI_service pulls chunks with STREAM_get: an SCHl
// header goes to SNDSTRMI_parseheader (SNDI_patchtohdr; a new format restarts the player once it has drained), an
// SCDl chunk to SNDSTRMI_parsedata -> SNDPKTPLAY_submit, anything else back with STREAM_release. On the SND thread
// the mixer's unpacker takes the packets (SNDPKTPLAYI_get) and reports what it consumed (SNDPKTPLAYI_freeframes);
// the release and frames callbacks are queued (0x00244fe0) and delivered after the mix
// (SNDPKTPLAYI_flushcallbackdata -> SNDSTRMI_releasecallback, SNDSTRMI_framescallback), which is how a request's
// position - SNDSTRM_status/requeststatus, the game's "has this line finished" - advances.
//
// Each function is the original at its address, ported from the listing. Calls into ported modules (B, C) go to our
// functions; into modules not ported here (A: lists, critical section, server clients, SNDMEMI, the 64-bit sums;
// E: STREAM; F: the platform driver) to the originals' addresses. The callbacks this module registers are stored as
// the originals' addresses (0x0013bb20, 0x0013bb50, 0x0013bf10, 0x0013c320), as the originals store them: their
// entries jump here, and the server client list is searched by that value.
//
// Quirks kept: SNDSTRMI_create hands STREAM_buffersize / 3 to SNDSTRM_setgreedylevel with the STREAM pointer where
// a stream index belongs, so the level is never set (the call answers -8); SNDPKTPLAY_stop frees the per-channel
// blobs without clearing them; SNDSTRMI_parsedata writes the request id over the chunk's tag and the chunk's address
// just before its first channel's data (SNDSTRMI_releasecallback reads it back); the callback queue has no bound.
// What the originals leave uninitialised and nobody reads: SNDSTRMI_parseheader's user-data message +0xc (no client
// ever registers), SNDSTRMI_parsedata's packet +0 and +8, SNDPKTPLAY_start's patch header outside the channel count
// and render mode SNDI_validrendermode reads. The ports write 0 there.
//
// Data-dead (SND_UNTESTED): the old movie player's SNDSTRM_overheadtap, SNDSTRM_queuerequestid, SNDSTRM_createtap
// and what only they reach (SNDSTRMI_create's tap branch, SNDSTRMI_queue's memory and caller-handle requests).
//
// devtools/SndStreamShadow.cpp compares them with the originals (sound.md 9.3 step 6).
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kTagData = 0x6c444353u;     // "SCDl"
const uint32_t kTagHeader = 0x6c484353u;   // "SCHl"
const uint32_t kTagEnd = 0x6c454353u;      // "SCEl"

const uint32_t kServiceAddress = 0x0013bf10u;     // SNDSTRMI_service, as the server client list holds it
const uint32_t kDestroyAllAddress = 0x0013c320u;  // SNDSTRMI_destroyall, the on-exit function
const uint32_t kReleaseAddress = 0x0013bb20u;     // SNDSTRMI_releasecallback
const uint32_t kFramesAddress = 0x0013bb50u;      // SNDSTRMI_framescallback

inline uint8_t NumStreams() {
    return *(uint8_t *)(uintptr_t)0x00244d0fu;
}
inline SND::StreamState *&StreamAt(int index) {    // sndss
    return ((SND::StreamState **)(uintptr_t)0x00244ba8u)[index];
}
inline SND::PacketPlayer *&PlayerAt(int index) {   // sndpps
    return ((SND::PacketPlayer **)(uintptr_t)0x002452e4u)[index];
}
inline int32_t &CallbackCount() {
    return *(int32_t *)(uintptr_t)0x00244fe0u;
}
inline SND::PacketCallback *CallbackAt(int index) {
    return (SND::PacketCallback *)(uintptr_t)0x00244fe4u + index;
}
inline SND::Voice *VoiceAt(int index) {           // sndvoicei_buffer, re-read at every use as the original does
    return (SND::Voice *)(*(uint8_t **)(uintptr_t)0x00244f3cu + index * 0x88);
}
inline uint32_t &StreamOnExit() {                 // SNDSTRM_on_exit_func
    return *(uint32_t *)(uintptr_t)0x00244f38u;
}
inline int8_t UserDataClientCount() {
    return *(int8_t *)(uintptr_t)0x00244ed6u;
}
typedef void (*UserDataClientFn)(void *message);
inline UserDataClientFn UserDataClient(int index) {
    return ((UserDataClientFn *)(uintptr_t)0x00244f10u)[index];
}

// The 31-bit frame count of a packet, sign-extended as SHL 1 / SAR 1 do
inline int32_t Frames31(uint32_t frames) {
    return (int32_t)(frames << 1) >> 1;
}

// ---- the originals called from here (other modules)

inline void EnterCritical() {
    ((void (*)(void))0x0013b950u)();                       // SNDSYS_entercritical
}
inline void LeaveCritical() {
    ((void (*)(void))0x0013b970u)();                       // SNDSYS_leavecritical
}
inline void LinkInit(SND::LinkList *list) {
    ((void (*)(SND::LinkList *))0x0013f0a0u)(list);        // SNDLINKI_init
}
inline void LinkPush(SND::LinkList *list, void *node) {
    ((void (*)(SND::LinkList *, void *))0x0013f0b0u)(list, node);   // SNDLINKI_push
}
inline void LinkPushTail(SND::LinkList *list, void *node) {
    ((void (*)(SND::LinkList *, void *))0x0013f0e0u)(list, node);   // SNDLINKI_pushtail
}
inline void *LinkPop(SND::LinkList *list) {
    return ((void *(*)(SND::LinkList *))0x0013f110u)(list);         // SNDLINKI_pop
}
inline void LinkRemove(SND::LinkList *list, void *node) {
    ((void (*)(SND::LinkList *, void *))0x0013f140u)(list, node);   // SNDLINKI_remove
}
inline void MemClear(void *p, int size) {
    ((void (*)(void *, int))0x0013f600u)(p, size);         // memclr
}
inline void MemFree(void *p) {
    ((void (*)(void *))0x0013f880u)(p);                    // SNDMEMI_free
}
inline void ServerAddClient(uint32_t fn) {
    ((void (*)(uint32_t))0x0013f900u)(fn);                 // iSNDserveraddclient
}
inline void ServerRemoveClient(uint32_t fn) {
    ((void (*)(uint32_t))0x0013f920u)(fn);                 // iSNDserverremoveclient
}
inline uint64_t MulU64(uint32_t a, uint32_t b) {
    return ((uint64_t (*)(uint32_t, uint32_t))0x0013f9e0u)(a, b);   // iSNDmulu64 (EDX:EAX)
}
inline uint32_t DivU64(uint64_t a, uint32_t b) {
    return ((uint32_t (*)(uint32_t, uint32_t, uint32_t))0x0013fa50u)((uint32_t)a, (uint32_t)(a >> 32), b);   // iSNDdivu64
}
inline int NullValue() {
    return ((int (*)(void))0x000f7330u)();                 // dummyGetNullValue (the stack holds what it holds)
}
inline int NullValue1(int a) {
    return ((int (*)(int))0x000f7330u)(a);
}
inline int NullValue2(int a, void *b) {
    return ((int (*)(int, void *))0x000f7330u)(a, b);
}
inline void GetVoiceRange(int mode, int *first, int *end) {
    ((void (*)(int, int *, int *))0x0013d900u)(mode, first, end);   // SNDPLATFORM_getvoicerange
}
inline int PacketPlay(int player, int voice, int timeMult, int lowpass, int opt14, int opt16,
                      SND::StreamFormat *format, uint8_t **blobs) {
    return ((int (*)(int, int, int, int, int, int, SND::StreamFormat *, uint8_t **))0x001424c0u)(
        player, voice, timeMult, lowpass, opt14, opt16, format, blobs);   // SNDPLATFORM_packetplay
}

// STREAM (module E)
inline void *StreamCreate(int requests, int a2, int a3, void *memory, int size) {
    return ((void *(*)(int, int, int, void *, int))0x0014b0c0u)(requests, a2, a3, memory, size);   // STREAM_create
}
inline int StreamOverhead(int requests, int a2, int a3) {
    return ((int (*)(int, int, int))0x0014b090u)(requests, a2, a3);     // STREAM_overhead
}
inline uint32_t StreamQueueFile(void *stream, const void *name, uint32_t offset, uint32_t tag) {
    return ((uint32_t (*)(void *, const void *, uint32_t, uint32_t))0x0014b3b0u)(stream, name, offset, tag);
}
inline uint32_t StreamQueueMem(void *stream, const void *memory, uint32_t a3, uint32_t tag) {
    return ((uint32_t (*)(void *, const void *, uint32_t, uint32_t))0x0014b470u)(stream, memory, a3, tag);
}
inline uint32_t *StreamGet(void *stream) {
    return ((uint32_t *(*)(void *))0x0014b520u)(stream);  // STREAM_get
}
inline uint32_t StreamGetTable(void *stream) {
    return ((uint32_t (*)(void *))0x0014b5d0u)(stream);   // STREAM_gettable: bytes buffered
}
inline int StreamState_(void *stream) {
    return ((int (*)(void *))0x0014b5f0u)(stream);        // STREAM_state
}
inline int StreamBufferSize(void *stream) {
    return ((int (*)(void *))0x0014b640u)(stream);        // STREAM_buffersize
}
inline int StreamSetGreedyLevel(void *stream, int level) {
    return ((int (*)(void *, int))0x0014b9a0u)(stream, level);   // STREAM_setgreedylevel
}
inline int StreamRelease(void *stream, void *chunk) {
    return ((int (*)(void *, void *))0x0014b9f0u)(stream, chunk);   // STREAM_release
}
inline void StreamKill(void *stream) {
    ((void (*)(void *))0x0014bcb0u)(stream);              // STREAM_kill
}
inline void StreamDestroy(void *stream) {
    ((void (*)(void *))0x0014be60u)(stream);              // STREAM_destroy
}

// STREAM_gettable's bytes as milliseconds at the request's rate, the byte count capped at 4,000,000
inline uint32_t BufferedMs(void *stream, uint32_t rate) {
    uint32_t bytes = StreamGetTable(stream);
    if (bytes > 4000000u)
        bytes = 4000000u;
    return bytes * 1000u / rate;
}

// SNDPKTPLAYI_get's release step: hand the oldest slot back through the release callback (queued)
inline void ReleaseNext(SND::PacketPlayer *p, int player) {
    SND::PacketSlot *slot = &p->slot[p->releaseIndex];
    if (p->release != NULL) {
        SND::PacketCallback *c = CallbackAt(CallbackCount());
        c->release = 1;
        c->player = (uint16_t)player;
        c->value = (uint32_t)(uintptr_t)slot->channels[0];
        CallbackCount()++;
    }
    p->releaseIndex = (int16_t)((uint16_t)p->releaseIndex + 1);
    if (p->releaseIndex >= p->slots)
        p->releaseIndex = 0;
}

// The playing attributes and format become the last header's; the blobs now belong to the playing copy
inline void TakeNextHeader(SND::StreamState *ss) {
    ss->format = ss->nextFormat;
    ss->attributes = ss->nextAttributes;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The API
// ---------------------------------------------------------------------------------------------------------------

// A fade of the stream's voice to vol over time (ms; the server ticks every 10)
// FUNC_AT(0x0013b990)
int SNDSTRM_autovol(int stream, int time, int vol) {
    SND::StreamState *ss = SNDSTRMI_getstreamptr(stream);
    if (ss == NULL)
        return -8;
    ss->opts.vol = (int8_t)vol;
    SNDautovol(ss->voice, time / 10, vol);
    return 0;
}

// FUNC_AT(0x0013c160)
int SNDSTRM_queuefile(int stream, int hold, const char *name, uint32_t offset) {
    return SNDSTRMI_queue(stream, hold, name, offset, 0);
}

// Stops the stream and drops every request (STREAM_kill empties the ring)
// FUNC_AT(0x0013c180)
int SNDSTRM_purge(int stream) {
    EnterCritical();
    SND::StreamState *ss;
    if (stream >= (int)NumStreams() || stream < 0 || (ss = StreamAt(stream)) == NULL) {
        LeaveCritical();
        return -8;
    }
    if (ss->voice >= 0)
        SNDPKTPLAY_stop(ss->player);
    ss->voice = -1;
    if (ss->external == 0)
        StreamKill(ss->stream);
    if (ss->nextAttributes.blobs[0] != NULL) {
        uint8_t **blob = &ss->nextAttributes.blobs[0];
        for (int i = 0; i < (int)ss->nextFormat.channels; i++, blob++)
            MemFree(*blob);
    }
    void *request;
    while ((request = LinkPop(&ss->active)) != NULL)
        LinkPush(&ss->freeRequests, request);
    ss->current = NULL;
    ss->state = 0;
    MemClear(&ss->format, 4);
    MemClear(&ss->nextFormat, 4);
    MemClear(&ss->attributes, 0x68);
    MemClear(&ss->nextAttributes, 0x68);
    LeaveCritical();
    return 0;
}

// FUNC_AT(0x0013c280)
int SNDSTRM_destroy(int stream) {
    SND::StreamState *ss;
    if (stream >= (int)NumStreams() || stream < 0 || (ss = StreamAt(stream)) == NULL)
        return -8;
    SNDSTRM_purge(stream);
    int n = (int)NumStreams(), live = 0;
    if (n > 0) {
        for (int i = 0; i < n; i++)
            if (StreamAt(i) != NULL)
                live++;
        if (live == 1) {   // the last one: no more servicing
            ServerRemoveClient(kServiceAddress);
            StreamOnExit() = 0;
        }
    }
    SNDPKTPLAY_destroy(ss->player);
    void *s = ss->stream;
    int external = (int8_t)ss->external;
    StreamAt(stream) = NULL;
    if (external == 0)
        StreamDestroy(s);
    return 0;
}

// FUNC_AT(0x0013c560)
int SNDSTRM_create(SND::PlayOpts *opts, int requests, int packets, void *memory, int size) {
    return SNDSTRMI_create(opts, requests, packets, memory, size, NULL, 0);
}

// FUNC_AT(0x0013c590)
int SNDSTRM_modifyhold(int id, int hold) {
    int result = -8;
    EnterCritical();
    SND::StreamRequest *r = SNDSTRMI_getrequestptr(id);
    if (r != NULL) {
        r->hold = (int16_t)hold;
        result = 0;
    }
    LeaveCritical();
    return result;
}

// The memory a tap stream needs (no STREAM of its own)
// FUNC_AT(0x0013c610)
int SNDSTRM_overheadtap(int requests, int packets) {
    SND_UNTESTED("SNDSTRM_overheadtap");
    int player = SNDPKTPLAY_overhead(packets);
    return player + requests * 0x28 + 0x138;
}

// The memory SNDSTRM_create needs: the record, the requests, the packet player and the STREAM
// FUNC_AT(0x0013c630)
int SNDSTRM_overhead(int requests, int packets) {
    int player = SNDPKTPLAY_overhead(packets);
    int own = player + requests * 0x28 + 0x138;
    return StreamOverhead(requests + 2, 1, 1) + own;
}

// FUNC_AT(0x0013c660)
int SNDSTRM_requeststatus(int id, SND::RequestStatus *status) {
    status->state = 0;
    status->playedMs = 0;
    status->remainingMs = 0;
    status->outstandingMs = 0;
    if (id < 0)
        return -8;
    SND::StreamState *ss = SNDSTRMI_getstreamptr(id & 0xff);
    if (ss == NULL)
        return -8;
    SND::StreamRequest *r = SNDSTRMI_getrequestptr(id);
    if (r == NULL) {
        status->state = 3;
        return 0;
    }
    if (r->started == 0) {
        status->state = 0;
        return 0;
    }
    uint32_t rate;
    if ((SND::StreamRequest *)ss->active.head == r) {
        status->state = 2;
        rate = ss->format.sampleRate;
    } else {
        status->state = 1;
        rate = ss->nextFormat.sampleRate;
    }
    status->playedMs = DivU64(MulU64(r->played, 1000), rate);
    status->remainingMs = DivU64(MulU64(r->total - r->played, 1000), rate);
    status->outstandingMs = r->outstanding * 1000u / rate;
    return 0;
}

// FUNC_AT(0x0013c740)
int SNDSTRM_status(int stream, SND::StreamStatus *status) {
    status->bufferedMs = 0;
    status->id = 0;
    status->requests = 0;
    SND::StreamState *ss = SNDSTRMI_getstreamptr(stream);
    if (ss == NULL)
        return -8;
    status->requests = ss->active.count;
    if (ss->active.count != 0) {
        status->id = ((SND::StreamRequest *)ss->active.head)->id;
        if (ss->format.sampleRate != 0) {
            uint32_t ms = (uint32_t)SNDPKTPLAY_framesoutstanding(ss->player) * 1000u / ss->format.sampleRate;
            status->bufferedMs = ms;
            if (ms == 0) {
                SND::StreamRequest *r = SNDSTRMI_getrequestptr(status->id);
                if (r->rate != 0)
                    status->bufferedMs = BufferedMs(ss->stream, r->rate);
            }
        }
    }
    return 0;
}

// FUNC_AT(0x0013c800)
int SNDSTRM_3dpos(int stream, int azimuth, int elevation) {
    SND::StreamState *ss = SNDSTRMI_getstreamptr(stream);
    if (ss == NULL)
        return -8;
    ss->opts.azimuth = (uint16_t)azimuth;
    ss->opts.elevation = (uint16_t)elevation;
    SND3dpos(ss->voice, azimuth, elevation);
    return 0;
}

// FUNC_AT(0x0013c840)
int SNDSTRM_lowpass(int stream, int cutoff) {
    SND::StreamState *ss = SNDSTRMI_getstreamptr(stream);
    if (ss == NULL)
        return -8;
    ss->opts.opt14 = (uint16_t)cutoff;   // +0x14: the low-pass the voice starts with
    SNDCTRL_lowpass(ss->voice, cutoff);
    return 0;
}

// FUNC_AT(0x0013c880)
int SNDSTRM_pitchmult(int stream, int mult) {
    SND::StreamState *ss = SNDSTRMI_getstreamptr(stream);
    if (ss == NULL)
        return -8;
    ss->opts.pitchMult = (uint16_t)mult;
    SNDpitchmult(ss->voice, mult);
    return 0;
}

// FUNC_AT(0x0013c8c0)
int SNDSTRM_vol(int stream, int vol) {
    SND::StreamState *ss = SNDSTRMI_getstreamptr(stream);
    if (ss == NULL)
        return -8;
    ss->opts.vol = (int8_t)vol;
    SNDvol(ss->voice, vol);
    return 0;
}

// FUNC_AT(0x0013f9b0)
int SNDSTRM_setgreedylevel(int stream, int level) {
    SND::StreamState *ss = SNDSTRMI_getstreamptr(stream);
    if (ss == NULL)
        return -8;
    StreamSetGreedyLevel(ss->stream, level);
    return 0;
}

// A request whose STREAM request the caller made (the old movie player)
// FUNC_AT(0x00150360)
int SNDSTRM_queuerequestid(int stream, int hold, uint32_t request) {
    SND_UNTESTED("SNDSTRM_queuerequestid");
    return SNDSTRMI_queue(stream, hold, NULL, request, 2);
}

// A stream on the caller's STREAM (the old movie player)
// FUNC_AT(0x00150380)
int SNDSTRM_createtap(void *stream, SND::PlayOpts *opts, int requests, int packets, void *memory, int size) {
    SND_UNTESTED("SNDSTRM_createtap");
    return SNDSTRMI_create(opts, requests, packets, memory, size, stream, 1);
}

// ---------------------------------------------------------------------------------------------------------------
// The stream internals
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0013b9e0)
void SNDSTRMI_startstream(SND::StreamState *ss) {
    ss->voice = SNDPKTPLAY_start(ss->player, &ss->format, &ss->attributes, &ss->opts);
    if (ss->hasFilter != 0)
        SNDCTRL_filteradd(ss->voice, (int)(uintptr_t)ss->filter);
    ss->state = 1;
}

// Bytes per second of a format: rate x channels x the representation's bytes per frame in 1/256ths (Xbox ADPCM
// 0x90, EA-XA 0x88, MicroTalk 0x33, 16-bit 0x200; 0x10 is 8 bytes a second per channel); 0 for the others.
// FUNC_AT(0x0013ba30)
int SNDSTRMI_calcdatarate(SND::StreamFormat *format) {
    uint32_t frames = (uint32_t)format->sampleRate * (uint32_t)format->channels;   // IMUL: wraps
    uint32_t perFrame;
    switch (format->sampleRep) {
    case 0x14:
        perFrame = 0x90;
        break;
    case 0x0a:
        perFrame = 0x88;
        break;
    case 4:
        perFrame = 0x33;
        break;
    case 8:
        perFrame = 0x200;
        break;
    case 0x10:
        return (int)format->channels * 8;
    default:
        perFrame = 0;
        break;
    }
    return (int32_t)(frames * perFrame) >> 8;
}

// FUNC_AT(0x0013baa0)
SND::StreamState* SNDSTRMI_getstreamptr(int stream) {
    if (stream < (int)NumStreams() && stream >= 0)
        return StreamAt(stream);
    return NULL;
}

// The request is finished: back to the free list
// FUNC_AT(0x0013bac0)
void SNDSTRMI_removerequest(int id) {
    SND::StreamState *ss = StreamAt(id & 0xff);
    SND::StreamRequest *r = SNDSTRMI_getrequestptr(id);
    LinkRemove(&ss->active, r);
    LinkPush(&ss->freeRequests, r);
    if (ss->current == r)
        ss->current = NULL;
}

// A packet's slot is free again: its chunk goes back to the STREAM. data is the packet's first channel, just after
// which SNDSTRMI_parsedata wrote the chunk's address; the chunk's first word holds the request id.
// FUNC_AT(0x0013bb20)
int SNDSTRMI_releasecallback(uint8_t *data, void *context) {
    (void)context;
    uint32_t *chunk = *(uint32_t **)(data - 4);
    SND::StreamState *ss = StreamAt(chunk[0] & 0xff);
    return StreamRelease(ss->stream, chunk);
}

// The mixer consumed frames: they count against the oldest requests in turn, each finished one is removed (at
// most 200 in one call; past that the oldest is removed whatever it holds)
// FUNC_AT(0x0013bb50)
void SNDSTRMI_framescallback(int player, uint32_t frames, SND::StreamState *ss) {
    (void)player;
    SND::StreamRequest *r = (SND::StreamRequest *)ss->active.head;
    uint32_t rest = 0;
    int round = 1;
    for (;;) {
        if (frames > r->outstanding) {
            rest = frames - r->outstanding;
            frames -= rest;
        }
        r->played += frames;
        r->outstanding -= frames;
        if (r->played >= r->total) {   // SNDSTRMI_removerequest, inlined
            int id = r->id;
            SND::StreamState *owner = StreamAt(id & 0xff);
            SND::StreamRequest *done = SNDSTRMI_getrequestptr(id);
            LinkRemove(&owner->active, done);
            LinkPush(&owner->freeRequests, done);
            if (owner->current == done)
                owner->current = NULL;
        }
        if (rest == 0)
            return;
        r = (SND::StreamRequest *)ss->active.head;
        frames = rest;
        rest = 0;
        round++;
        if (round > 200)
            break;
    }
    SNDSTRMI_removerequest(r->id);
}

// An SCHl chunk: the next request's header. A header that differs from the playing one (format, attributes, or
// new per-channel blobs) restarts the player - at once if nothing plays yet, else once the player has drained
// (state 2, SNDSTRMI_service).
// FUNC_AT(0x0013bc20)
int SNDSTRMI_parseheader(int stream, uint32_t *chunk) {
    SND::StreamState *ss = StreamAt(stream);
    if (ss->current == NULL)
        ss->current = (SND::StreamRequest *)ss->active.head;
    else
        ss->current = ss->current->next;
    SND::StreamRequest *r = ss->current;
    SND::StreamLayout layout;
    SNDI_patchtohdr(0, (uint8_t *)(chunk + 2), &ss->nextFormat, &ss->nextAttributes, &layout);
    r->total = (uint32_t)layout.frames;
    r->started = 0;

    // the header's user data (tag 0x14) to the user-data clients - none register
    uint8_t **data = &ss->nextAttributes.userData[0];
    while (*data != NULL) {
        struct {
            int32_t operation;   // 3
            uint8_t *data;
            int32_t size;
            int32_t unused;
            int32_t id;
        } message;
        message.operation = 3;
        message.data = data[0];
        message.size = *(int32_t *)(data + 4);   // userDataSize, 4 entries on
        message.unused = 0;
        message.id = r->id;
        data[0] = NULL;
        *(int32_t *)(data + 4) = 0;
        for (int i = 0; i < (int)UserDataClientCount(); i++)
            UserDataClient(i)(&message);
        data++;
    }

    StreamRelease(ss->stream, chunk);
    r->rate = (uint32_t)SNDSTRMI_calcdatarate(&ss->nextFormat);

    bool same = *(uint32_t *)&ss->format == *(uint32_t *)&ss->nextFormat;
    if (same) {
        const uint32_t *a = (const uint32_t *)&ss->attributes, *b = (const uint32_t *)&ss->nextAttributes;
        for (int i = 0; i < 0x1a; i++)
            if (a[i] != b[i]) {
                same = false;
                break;
            }
    }
    if (!same || ss->nextAttributes.blobs[0] != NULL) {
        if (ss->format.sampleRate != 0) {   // playing: restart once drained
            ss->state = 2;
            return 0;
        }
        TakeNextHeader(ss);
        ss->nextAttributes.blobs[0] = NULL;
    }
    if (ss->state != 1) {   // SNDSTRMI_startstream, inlined (it sets the state twice)
        ss->voice = SNDPKTPLAY_start(ss->player, &ss->format, &ss->attributes, &ss->opts);
        if (ss->hasFilter != 0)
            SNDCTRL_filteradd(ss->voice, (int)(uintptr_t)ss->filter);
        ss->state = 1;
        ss->state = 1;
    }
    return 0;
}

// An SCDl chunk: frames, then per channel the offset of its data past the offset table. Submitted as one packet;
// an empty chunk goes straight back.
// FUNC_AT(0x0013bdb0)
int SNDSTRMI_parsedata(SND::StreamState *ss, uint32_t *chunk) {
    SND::Packet packet;
    packet.unused00 = 0;
    packet.unused08 = 0;
    packet.frames = chunk[2] & 0x7fffffffu;
    int channels = ss->format.channels;
    uint8_t *base = (uint8_t *)(chunk + 3) + channels * 4;
    for (int i = 0; i < channels; i++)
        packet.channels[i] = base + chunk[3 + i];
    SND::StreamRequest *r = ss->current;
    if ((packet.frames & 0x7fffffffu) == 0)
        return StreamRelease(ss->stream, chunk);
    ((uint32_t **)packet.channels[0])[-1] = chunk;   // for SNDSTRMI_releasecallback
    chunk[0] = (uint32_t)r->id;
    r->outstanding += packet.frames & 0x7fffffffu;
    packet.frames = ((uint32_t)r->started << 31) | (packet.frames & 0x7fffffffu);
    uint32_t result = SNDPKTPLAY_submit(ss->player, &packet);
    r->started = 1;
    return (int)result;
}

// Whether to wait for more data before submitting: the request's hold (ms of buffer) is not reached yet and the
// STREAM is still filling. A hold below 0 waits until changed. A reached hold is cleared.
// FUNC_AT(0x0013be80)
int SNDSTRMI_isheld(SND::StreamState *ss) {
    SND::StreamRequest *r = ss->current;
    if (r == NULL || r->rate == 0)
        return 0;
    if (r->hold < 0)
        return 1;
    if (r->hold == 0)
        return 0;
    if (BufferedMs(ss->stream, r->rate) < (uint32_t)r->hold && StreamState_(ss->stream) != 2) {
        if (ss->freeRequests.count > 0)
            return 1;
        if (StreamState_(ss->stream) != 0)
            return 1;
    }
    r->hold = 0;
    return 0;
}

// The main-thread server client (registered while any stream exists): per stream, a pending restart, then up to
// as many chunks as the player has room for (10 before it plays). A header ends the stream's turn.
// FUNC_AT(0x0013bf10)
void SNDSTRMI_service(void) {
    int i = 0;
    EnterCritical();
    if (NumStreams() != 0) {
        SND::StreamState **slot = &StreamAt(0);
        do {
            SND::StreamState *ss = *slot;
            if (ss == NULL || ss->active.count == 0)
                goto next;
            if (ss->state == 2) {
                if (SNDPKTPLAY_framesoutstanding(ss->player) > 0)
                    goto next;
                TakeNextHeader(ss);
                ss->nextAttributes.blobs[0] = NULL;
                SNDPKTPLAY_stop(ss->player);
                SNDSTRMI_startstream(ss);
            }
            if (SNDSTRMI_isheld(ss) != 0)
                goto next;
            {
                int n;
                if (ss->state == 1) {
                    n = SNDPKTPLAY_submitspace(ss->player);
                    if (n == 0)
                        goto next;
                } else {
                    n = 10;
                }
                int got = 0;
                do {
                    n--;
                    uint32_t *chunk = StreamGet(ss->stream);
                    if (chunk == NULL) {
                        if (got == 0)
                            break;
                    } else if (chunk[0] == kTagData) {
                        SNDSTRMI_parsedata(*slot, chunk);
                        got = 1;
                    } else if (chunk[0] == kTagHeader) {
                        SNDSTRMI_parseheader(i, chunk);
                        break;
                    } else {
                        StreamRelease((*slot)->stream, chunk);
                        got = 1;
                    }
                } while (n > 0);
            }
        next:
            i++;
            slot++;
        } while (i < (int)NumStreams());
    }
    LeaveCritical();
}

// A request: type 0 a file (STREAM_queuefile), 1 memory (STREAM_queuemem), else the caller's STREAM request.
// Answers the request id, -1 if the STREAM refused it, -13 with no free request record.
// FUNC_AT(0x0013c060)
int SNDSTRMI_queue(int stream, int hold, const void *source, uint32_t arg, int type) {
    SND::StreamState *ss;
    if (stream >= (int)NumStreams() || stream < 0 || (ss = StreamAt(stream)) == NULL)
        return -8;
    if (ss->freeRequests.count == 0)
        return -13;
    uint32_t request;
    if (type == 0) {
        request = StreamQueueFile(ss->stream, source, arg, kTagEnd);
    } else if (type == 1) {
        SND_UNTESTED("SNDSTRMI_queue (memory)");
        request = StreamQueueMem(ss->stream, source, 0, kTagEnd);
    } else {
        SND_UNTESTED("SNDSTRMI_queue (caller's request)");
        request = arg;
    }
    if (request == 0)
        return -1;
    EnterCritical();
    SND::StreamRequest *r = (SND::StreamRequest *)LinkPop(&ss->freeRequests);
    MemClear(r, 0x28);
    LinkPushTail(&ss->active, r);
    r->streamRequest = request;
    ss->generation += 0x100;
    if (ss->generation < 0)
        ss->generation = 0;
    r->id = ss->generation | stream;
    r->hold = hold;
    int id = r->id;
    LeaveCritical();
    return id;
}

// The on-exit function
// FUNC_AT(0x0013c320)
int SNDSTRMI_destroyall(void) {
    if (NumStreams() != 0) {
        int i = 0;
        do {
            SNDSTRM_destroy(i);
            i++;
        } while (i < (int)NumStreams());
    }
    return 0;
}

// The memory is laid out as the record (0x138), the request records, the packet player, then the STREAM's ring in
// whatever is left. Answers the stream index, -9 with no free slot or STREAM, or the packet player's error.
// FUNC_AT(0x0013c350)
int SNDSTRMI_create(SND::PlayOpts *opts, int requests, int packets, void *memory, int size, void *stream,
                    int tap) {
    int n = (int)NumStreams(), index = 0;
    if (n <= 0)
        return -9;
    while (StreamAt(index) != NULL) {
        index++;
        if (index >= n)
            return -9;
    }
    SND::StreamState *ss = (SND::StreamState *)memory;
    MemClear(ss, 0x138);
    uint8_t *requestMemory = (uint8_t *)memory + 0x138;
    uint8_t *playerMemory = requestMemory + requests * 0x28;
    int left = size + (-0x138 - requests * 0x28);
    int playerSize = SNDPKTPLAY_overhead(packets);
    left -= playerSize;
    uint8_t *streamMemory = playerMemory + playerSize;
    LinkInit(&ss->active);
    LinkInit(&ss->freeRequests);
    if (requests > 0) {
        uint8_t *node = requestMemory;
        for (int k = requests; k != 0; k--) {
            LinkPush(&ss->freeRequests, node);
            node += 0x28;
        }
    }
    ss->player = SNDPKTPLAY_create((SND::PacketReleaseFn)(uintptr_t)kReleaseAddress,
                                   (SND::PacketFramesFn)(uintptr_t)kFramesAddress, ss, playerMemory,
                                   SNDPKTPLAY_overhead(packets));
    if (ss->player < 0)
        return ss->player;
    if (tap != 0) {
        SND_UNTESTED("SNDSTRMI_create (tap)");
        ss->stream = stream;
        ss->external = 1;
    } else {
        void *s = StreamCreate(requests + 2, 1, 1, streamMemory, left);
        if (s == NULL) {
            SNDPKTPLAY_destroy(ss->player);
            return -9;
        }
        ss->stream = s;
        ss->external = 0;
        int bytes = StreamBufferSize(s);
        // the STREAM pointer where SNDSTRM_setgreedylevel wants a stream index: it answers -8 and sets nothing
        SNDSTRM_setgreedylevel((int)(intptr_t)ss->stream, bytes / 3);
    }
    ss->generation = 0;
    ss->voice = -1;
    ss->opts = *opts;
    int live = 0;
    n = (int)NumStreams();
    for (int i = 0; i < n; i++)
        if (StreamAt(i) != NULL)
            live++;
    if (live == 0) {   // the first one: service streams from now on
        ServerAddClient(kServiceAddress);
        StreamOnExit() = kDestroyAllAddress;
    }
    StreamAt(index) = ss;
    ss->nextAttributes.blobs[0] = NULL;
    SNDSTRM_purge(index);
    return index;
}

// FUNC_AT(0x0013f180)
SND::StreamRequest* SNDSTRMI_getrequestptr(int id) {
    if (id < 0)
        return NULL;
    SND::StreamState *ss = SNDSTRMI_getstreamptr(id & 0xff);
    if (ss == NULL)
        return NULL;
    SND::StreamRequest *r = (SND::StreamRequest *)ss->active.head;
    while (r != NULL && r->id != id)
        r = r->next;
    return r;
}

// ---------------------------------------------------------------------------------------------------------------
// The packet player
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0013e8c0)
int SNDPKTPLAY_overhead(int packets) {
    int extra = NullValue();
    return extra + packets * 32 + 0x7c;
}

// FUNC_AT(0x0013e8e0)
int SNDPKTPLAY_create(SND::PacketReleaseFn release, SND::PacketFramesFn framesDone, void *context, void *memory,
                      int size) {
    EnterCritical();
    int n = (int)NumStreams(), index = 0;
    if (n > 0) {
        while (PlayerAt(index) != NULL) {
            index++;
            if (index >= n)
                break;
        }
    }
    if (index >= n) {
        LeaveCritical();
        return -9;
    }
    if (NullValue2(index, memory) < 0) {
        LeaveCritical();
        return -6;
    }
    SND::PacketPlayer *p = (SND::PacketPlayer *)((uint8_t *)memory + NullValue());
    int extra = NullValue();
    p->slots = (int16_t)((uint32_t)(size - extra - 0x7c) >> 5);
    p->memory = memory;
    p->release = release;
    p->framesDone = framesDone;
    p->context = context;
    p->voice = -1;
    PlayerAt(index) = p;
    LeaveCritical();
    return index;
}

// Allocates the voice(s) for the format (priority 101) on the first render mode that has room and starts the
// platform's packet voice; answers the voice handle or the last error.
// FUNC_AT(0x0013e980)
int SNDPKTPLAY_start(int player, SND::StreamFormat *format, SND::Attributes *attributes, SND::PlayOpts *opts) {
    SND::PatchHeader header;   // what SNDI_validrendermode reads: the channel count and the render mode
    memset(&header, 0, sizeof(header));
    uint8_t channelCount = format->channels;
    header.channels = (int8_t)channelCount;
    int count = channelCount;
    int modeIndex = 0;
    SND::PacketPlayer *p = PlayerAt(player);
    header.renderMode = attributes->renderMode;
    p->format = *format;
    p->serial = 0;
    p->queuedFrames = 0;
    p->takenFrames = 0;
    p->writeIndex = 0;
    p->releaseIndex = 0;
    p->waiting = 1;
    for (int i = 0; i < (int)p->format.channels; i++) {
        p->count[i] = 0;
        p->readIndex[i] = 0;
        p->blobs[i] = attributes->blobs[i];
    }
    for (;;) {
        int mode = SNDI_validrendermode(&modeIndex, &header);
        if (mode == 0)
            break;
        int first, end, handle;
        GetVoiceRange(mode, &first, &end);
        int voice = SNDVOICEI_alloc(count, 0x65, &handle, first, end);
        if (voice < 0) {
            p->voice = -9;
            continue;
        }
        p->voice = handle;
        int channels = format->channels;
        int master = -1;
        if (channels > 0) {
            const int16_t *platformVoice = VoiceAt(voice)->platformVoices;
            int highest = -1;
            for (int i = 0; i < channels; i++, platformVoice++)
                if (*platformVoice > highest) {
                    highest = *platformVoice;
                    master = i;
                }
        }
        p->master = (int8_t)master;
        SND::Voice *v = VoiceAt(voice);
        v->builtinAzimuth = 0;
        v->azimuth = opts->azimuth;
        v->bank = (int16_t)0xffff;
        v->detune = (int16_t)attributes->a00;
        v->pitchMult = opts->pitchMult;
        v->fadeStep = 0;
        v->fade = (int32_t)((uint32_t)(int32_t)opts->vol << 16);
        v->envStep = 0;
        v->envTicks = 0x7fffffff;
        v->env = 0x7f0000;
        v->builtinVol = (int8_t)attributes->vol;
        v->bend = opts->pan;
        v->envCount = 1;
        v->envCurrent = 0;
        v->envRelease = 0;
        v->dry = (int8_t)attributes->fxLevel;
        v->progVol = opts->progVol;
        v->fxLevel = opts->fxLevel;
        v->key = 0;
        v->keyLast = 0;
        v->envTable = NULL;
        v->volTable = NULL;
        v->bendTable = NULL;
        v->volLfo = NULL;
        v->bendRange = (int16_t)((int8_t)attributes->bendRange * 100);
        v->pitchLfo = NULL;
        uint16_t *azimuth = v->channelAzimuth;
        for (int i = 0; i < (int)format->channels; i++)
            azimuth[i] = attributes->azimuth[i];
        v->sustainEnd = -1;
        v->frames = 0;
        v->sampleRate = format->sampleRate;
        v->sampleRep = format->sampleRep;
        v->channels = (int8_t)format->channels;
        v->renderMode = (uint16_t)mode;
        v->detuneLinear = 0;
        iSNDcalcpitch(voice);
        iSNDcalcvol(voice);
        SNDI_calcfxlevel(0, voice);
        int result = PacketPlay(player, voice, opts->timeMult, opts->lowpass, opts->opt14, opts->opt16, format,
                                attributes->blobs);
        if (result >= 0)
            break;
        for (int k = 0; k < count; k++)   // the voice buffer re-read for each
            SNDVOICEI_free(VoiceAt(voice)->platformVoices[k]);
        p->voice = result;
    }
    return p->voice;
}

// Queues a packet on every channel; -13 when the ring is full (one slot always stays empty). Answers the packet's
// serial.
// FUNC_AT(0x0013ecc0)
uint32_t SNDPKTPLAY_submit(int player, SND::Packet *packet) {
    SND::PacketPlayer *p = PlayerAt(player);
    if (p->count[0] >= p->slots - 1)
        return (uint32_t)-13;
    SND::PacketSlot *slot = &p->slot[p->writeIndex];
    slot->frames = (slot->frames & 0x80000000u) | (packet->frames & 0x7fffffffu);   // the two bit fields in turn
    slot->frames = (packet->frames & 0x80000000u) | (slot->frames & 0x7fffffffu);
    slot->serial = p->serial;
    int16_t *count = p->count;
    for (int i = 0; i < (int)p->format.channels; i++, count++) {
        slot->channels[i] = packet->channels[i];
        *count = (int16_t)((uint16_t)*count + 1);
    }
    p->queuedFrames += (int32_t)(packet->frames & 0x7fffffffu);
    uint32_t serial = p->serial;
    p->serial = serial + 1;
    p->writeIndex = (int16_t)((uint16_t)p->writeIndex + 1);
    p->waiting = 1;
    if (p->writeIndex >= p->slots)
        p->writeIndex = 0;
    return serial;
}

// FUNC_AT(0x0013eda0)
int SNDPKTPLAY_submitspace(int player) {
    SND::PacketPlayer *p = PlayerAt(player);
    return (int)p->slots - (int)p->count[0] - 1;
}

// FUNC_AT(0x0013edc0)
int SNDPKTPLAY_framesoutstanding(int player) {
    SND::PacketPlayer *p = PlayerAt(player);
    return p->takenFrames + p->queuedFrames;
}

// FUNC_AT(0x0013ede0)
int SNDPKTPLAY_destroy(int player) {
    NullValue1(player);
    PlayerAt(player) = NULL;
    return 0;
}

// The unpacker takes the channel's next packet: its data (-1 if NULL), frames (31 bits) and bit 31 (sign-extended:
// 0 for a request's first packet, -1 after). With none queued, NULL - and on the master channel, once it has run
// dry, the oldest slot still held is released. The master's take moves its frames from queued to taken.
// FUNC_AT(0x0013ee00)
uint8_t* SNDPKTPLAYI_get(int player, int channel, int *frames, int *continued) {
    SND::PacketPlayer *p = PlayerAt(player);
    if (p->count[channel] == 0) {
        if (channel == p->master && p->waiting == 0 && p->readIndex[channel] != p->releaseIndex)
            ReleaseNext(p, player);
        return NULL;
    }
    SND::PacketSlot *slot = &p->slot[p->readIndex[channel]];
    *frames = Frames31(slot->frames);
    *continued = (int32_t)slot->frames >> 31;
    if (channel == p->master) {
        if (p->readIndex[channel] != p->releaseIndex)
            ReleaseNext(p, player);
        p->queuedFrames -= Frames31(slot->frames);
        p->takenFrames += Frames31(slot->frames);
    }
    p->readIndex[channel] = (int16_t)((uint16_t)p->readIndex[channel] + 1);
    if (p->readIndex[channel] >= p->slots)
        p->readIndex[channel] = 0;
    p->count[channel] = (int16_t)((uint16_t)p->count[channel] - 1);
    if (p->count[channel] == 0 && channel == p->master)
        p->waiting = 0;
    uint8_t *data = slot->channels[channel];
    if (data == NULL)
        data = (uint8_t *)(intptr_t)-1;
    return data;
}

// The unpacker consumed frames (counted on the master channel only): queued for the frames callback
// FUNC_AT(0x0013ef80)
void SNDPKTPLAYI_freeframes(int player, int channel, int frames) {
    SND::PacketPlayer *p = PlayerAt(player);
    if (channel != p->master)
        return;
    p->takenFrames -= frames;
    if (p->framesDone != NULL) {
        SND::PacketCallback *c = CallbackAt(CallbackCount());
        c->release = 0;
        c->player = (uint16_t)player;
        c->value = (uint32_t)frames;
        CallbackCount()++;
    }
}

// Delivers the queued callbacks (the SND thread, after each mix tick)
// FUNC_AT(0x0013efd0)
void SNDPKTPLAYI_flushcallbackdata(void) {
    for (int i = 0; i < CallbackCount(); i++) {
        SND::PacketCallback *c = CallbackAt(i);
        int player = c->player;
        SND::PacketPlayer *p = PlayerAt(player);
        if (c->release == 0)
            p->framesDone(player, c->value, p->context);
        else
            p->release((uint8_t *)(uintptr_t)c->value, p->context);
    }
    CallbackCount() = 0;
}

// FUNC_AT(0x0013f040)
int SNDPKTPLAY_stop(int player) {
    SND::PacketPlayer *p = PlayerAt(player);
    SNDstop(p->voice);
    SNDPKTPLAYI_flushcallbackdata();
    p->voice = -1;
    for (int i = 0; i < (int)p->format.channels; i++)
        if (p->blobs[i] != NULL)
            MemFree(p->blobs[i]);
    return 0;
}

// The packet player whose voice is this voice index, -1 for none
// FUNC_AT(0x001457e0)
int SNDPKTPLAYI_voicetopackethandle(int voice) {
    if (NumStreams() != 0) {
        int i = 0;
        do {
            SND::PacketPlayer *p = PlayerAt(i);
            if (p != NULL && SNDVOICEI_get(p->voice) == voice)
                return i;
            i++;
        } while (i < (int)NumStreams());
    }
    return -1;
}
