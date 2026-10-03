#include "Streams.h"
#include "Platform.h"
#include "Stream.h"
#include "System.h"
#include "../../platform/RealPrint.h"
#include "SndGlobals.h"

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
// the release and frames callbacks are queued (PacketCallbacks) and delivered after the mix
// (SNDPKTPLAYI_flushcallbackdata -> SNDSTRMI_releasecallback, SNDSTRMI_framescallback), which is how a request's
// position - SNDSTRM_status/requeststatus, the game's "has this line finished" - advances.
//
// Each function is the original at its address, ported from the listing. Calls into the other modules (A: lists,
// critical section, server clients, SNDMEMI, the 64-bit sums; B, C; E: STREAM; F: the platform driver) are direct:
// devtools/SndStreamShadow.cpp puts its recording fakes in front of our functions and the originals alike. The
// callbacks this module registers are stored as the originals' addresses (0x0013bb20, 0x0013bb50, 0x0013bf10,
// 0x0013c320), as the originals store them: their entries jump here, and the server client list is searched by that
// value.
//
// Quirks kept: SNDSTRMI_create hands STREAM_buffersize / 3 to SNDSTRM_setgreedylevel with the STREAM pointer where
// a stream index belongs, so the level is never set (the call answers -8); SNDPKTPLAY_stop frees the per-channel
// stretch data without clearing the pointers; SNDSTRMI_parsedata writes the request id over the chunk's tag and the
// chunk's address just before its first channel's data (SNDSTRMI_releasecallback reads it back); the callback queue
// has no bound. What the originals leave uninitialised and nobody reads: SNDSTRMI_parseheader's user-data message
// +0xc (no client ever registers), SNDSTRMI_parsedata's packet +0 and +8, SNDPKTPLAY_start's patch header outside
// the channel count and render mode SNDI_validrendermode reads. The ports write 0 there.
//
// Data-dead (SND_UNTESTED): the old movie player's SNDSTRM_overheadtap, SNDSTRM_queuerequestid, SNDSTRM_createtap
// and what only they reach (SNDSTRMI_create's tap branch, SNDSTRMI_queue's memory and caller-handle requests).
//
// devtools/SndStreamShadow.cpp compares them with the originals (sound.md 9.3 step 6).
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- this module's globals
#define StreamArray ((SND::StreamState **)0x00244ba8)      // sndss: [NumStreams]
#define PacketPlayers ((SND::PacketPlayer **)0x002452e4)   // sndpps: [NumStreams]
#define PacketCallbackCount I32_AT(0x00244fe0)
#define PacketCallbacks ((SND::PacketCallback *)0x00244fe4)   // [PacketCallbackCount], no bound

// The chunk tags of an EA stream
enum ChunkTag : uint32_t {
    kChunkData = 0x6c444353,     // "SCDl"
    kChunkHeader = 0x6c484353,   // "SCHl"
    kChunkEnd = 0x6c454353,      // "SCEl"
};

// The sample representations (sndo.h SND_SR_*) SNDSTRMI_calcdatarate knows
enum SampleRep : uint8_t {
    kRepMicroTalk10 = 4,
    kRepS16 = 8,                 // 16-bit little-endian
    kRepEaXa = 0x0a,
    kRepLayer3 = 0x10,
    kRepXboxAdpcm = 0x14,
};

// An SCDl chunk as STREAM_get hands it out: the frames, then per channel the offset of its data past the offset
// table
struct DataChunk {
    uint32_t tag;                // +0x00 "SCDl"; the request id once submitted
    uint32_t size;               // +0x04
    uint32_t frames;             // +0x08 (bit 31 masked off)
    uint32_t offsets[6];         // +0x0c [channels], then the data
};

// The sizes SNDSTRM_overhead and SNDSTRMI_create lay the memory out by
constexpr int kStateSize = 0x138;
constexpr int kRequestSize = 0x28;
static_assert(sizeof(SND::StreamState) == kStateSize && sizeof(SND::StreamRequest) == kRequestSize);

// The entries this module registers, as the originals' addresses (their entries jump to the ports)
#define ServiceEntry ((SndServerClient)0x0013bf10)            // SNDSTRMI_service, as the server client list holds it
#define DestroyAllEntry ((SndRestoreHook)0x0013c320)          // SNDSTRMI_destroyall, the on-exit function
#define ReleaseEntry ((SND::PacketReleaseFn)0x0013bb20)       // SNDSTRMI_releasecallback
#define FramesEntry ((SND::PacketFramesFn)0x0013bb50)         // SNDSTRMI_framescallback

// ---- engine.core's dummyGetNullValue (not ours), at its address; the stack holds what it holds
#define DummyGetNullValue ((int (*)(...))0x000f7330)

// ---- SNDLINKI on the request lists: a request's first two words are the list's links
static_assert(offsetof(SND::StreamRequest, next) == offsetof(SND::LinkNode, next) &&
              offsetof(SND::StreamRequest, prev) == offsetof(SND::LinkNode, prev), "a request starts with the links");
inline SND::LinkNode *AsLinkNode(SND::StreamRequest *request) {
    return reinterpret_cast<SND::LinkNode *>(request);
}
inline void LinkPush(SND::LinkList *list, SND::StreamRequest *request) {
    SNDLINKI_push(list, AsLinkNode(request));
}
inline void LinkPushTail(SND::LinkList *list, SND::StreamRequest *request) {
    SNDLINKI_pushtail(list, AsLinkNode(request));
}
inline SND::StreamRequest *LinkPop(SND::LinkList *list) {
    return reinterpret_cast<SND::StreamRequest *>(SNDLINKI_pop(list));
}
inline void LinkRemove(SND::LinkList *list, SND::StreamRequest *request) {
    SNDLINKI_remove(list, AsLinkNode(request));
}

// STREAM_release with the EAX it leaves: SNDSTRMI_releasecallback and SNDSTRMI_parsedata return it, as the originals'
// tail calls do. No caller reads it (the shadow test's fake answers a value, and compares it).
inline int StreamReleaseEax(SND::StrmReader *stream, uint32_t *chunk) {
    return reinterpret_cast<int (*)(SND::StrmReader *, uint32_t *)>(&STREAM_release)(stream, chunk);
}

// The 31-bit frame count of a packet, sign-extended as SHL 1 / SAR 1 do
int32_t Frames31(uint32_t frames) {
    return int32_t(frames << 1) >> 1;
}

// The oldest queued request of a stream
SND::StreamRequest *Oldest(SND::StreamState *ss) {
    return (SND::StreamRequest *)ss->active.head;
}

// frames x 1000 / rate through the library's 64-bit helpers
uint32_t FramesToMs(uint32_t frames, uint32_t rate) {
    uint64_t product = iSNDmulu64(frames, 1000);
    return iSNDdivu64(uint32_t(product), uint32_t(product >> 32), rate);
}

// STREAM_gettable's bytes as milliseconds at the request's rate, the byte count capped at 4,000,000
uint32_t BufferedMs(SND::StrmReader *stream, uint32_t rate) {
    uint32_t bytes = STREAM_gettable(stream);
    if (bytes > 4000000)
        bytes = 4000000;
    return bytes * 1000 / rate;
}

// SNDPKTPLAYI_get's release step: hand the oldest slot back through the release callback (queued)
void ReleaseNext(SND::PacketPlayer *p, int player) {
    SND::PacketSlot *slot = &p->slot[p->releaseIndex];
    if (p->release != NULL) {
        SND::PacketCallback *c = &PacketCallbacks[PacketCallbackCount];
        c->release = 1;
        c->player = uint16_t(player);
        c->data = slot->channels[0];
        PacketCallbackCount++;
    }
    p->releaseIndex++;
    if (p->releaseIndex >= p->slots)
        p->releaseIndex = 0;
}

// The playing attributes and format become the last header's; the stretch data now belong to the playing copy
void TakeNextHeader(SND::StreamState *ss) {
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
    ss->opts.vol = int8_t(vol);
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
    SNDSYS_entercritical();
    SND::StreamState *ss;
    if (stream >= NumStreams || stream < 0 || (ss = StreamArray[stream]) == NULL) {
        SNDSYS_leavecritical();
        return -8;
    }
    if (ss->voice >= 0)
        SNDPKTPLAY_stop(ss->player);
    ss->voice = -1;
    if (ss->external == 0)
        STREAM_kill(ss->stream);
    if (ss->nextAttributes.stretchData[0] != NULL) {
        for (int i = 0; i < ss->nextFormat.channels; i++)
            SNDMEMI_free(ss->nextAttributes.stretchData[i]);
    }
    SND::StreamRequest *request;
    while ((request = LinkPop(&ss->active)) != NULL)
        LinkPush(&ss->freeRequests, request);
    ss->current = NULL;
    ss->state = 0;
    memclr(&ss->format, sizeof(ss->format));
    memclr(&ss->nextFormat, sizeof(ss->nextFormat));
    memclr(&ss->attributes, sizeof(ss->attributes));
    memclr(&ss->nextAttributes, sizeof(ss->nextAttributes));
    SNDSYS_leavecritical();
    return 0;
}

// FUNC_AT(0x0013c280)
int SNDSTRM_destroy(int stream) {
    SND::StreamState *ss;
    if (stream >= NumStreams || stream < 0 || (ss = StreamArray[stream]) == NULL)
        return -8;
    SNDSTRM_purge(stream);
    int n = NumStreams, live = 0;
    if (n > 0) {
        for (int i = 0; i < n; i++)
            if (StreamArray[i] != NULL)
                live++;
        if (live == 1) {   // the last one: no more servicing
            iSNDserverremoveclient(ServiceEntry);
            StreamExitHook = NULL;
        }
    }
    SNDPKTPLAY_destroy(ss->player);
    SND::StrmReader *s = ss->stream;
    uint8_t external = ss->external;
    StreamArray[stream] = NULL;
    if (external == 0)
        STREAM_destroy(s);
    return 0;
}

// FUNC_AT(0x0013c560)
int SNDSTRM_create(SND::PlayOpts *opts, int requests, int packets, void *memory, int size) {
    return SNDSTRMI_create(opts, requests, packets, memory, size, NULL, 0);
}

// FUNC_AT(0x0013c590)
int SNDSTRM_modifyhold(int id, int hold) {
    int result = -8;
    SNDSYS_entercritical();
    SND::StreamRequest *r = SNDSTRMI_getrequestptr(id);
    if (r != NULL) {
        r->hold = int16_t(hold);   // the original keeps only the low 16 bits, sign-extended
        result = 0;
    }
    SNDSYS_leavecritical();
    return result;
}

// The memory a tap stream needs (no STREAM of its own)
// FUNC_AT(0x0013c610)
int SNDSTRM_overheadtap(int requests, int packets) {
    SND_UNTESTED("SNDSTRM_overheadtap");
    int player = SNDPKTPLAY_overhead(packets);
    return player + requests * kRequestSize + kStateSize;
}

// The memory SNDSTRM_create needs: the record, the requests, the packet player and the STREAM
// FUNC_AT(0x0013c630)
int SNDSTRM_overhead(int requests, int packets) {
    int player = SNDPKTPLAY_overhead(packets);
    int own = player + requests * kRequestSize + kStateSize;
    return STREAM_overhead(requests + 2, 1, 1) + own;
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
    if (Oldest(ss) == r) {
        status->state = 2;
        rate = ss->format.sampleRate;
    } else {
        status->state = 1;
        rate = ss->nextFormat.sampleRate;
    }
    status->playedMs = FramesToMs(r->played, rate);
    status->remainingMs = FramesToMs(r->total - r->played, rate);
    status->outstandingMs = r->outstanding * 1000 / rate;
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
        status->id = Oldest(ss)->id;
        if (ss->format.sampleRate != 0) {
            uint32_t ms = uint32_t(SNDPKTPLAY_framesoutstanding(ss->player)) * 1000 / ss->format.sampleRate;
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
    ss->opts.azimuth = uint16_t(azimuth);
    ss->opts.elevation = uint16_t(elevation);
    SND3dpos(ss->voice, azimuth, elevation);
    return 0;
}

// FUNC_AT(0x0013c840)
int SNDSTRM_lowpass(int stream, int cutoff) {
    SND::StreamState *ss = SNDSTRMI_getstreamptr(stream);
    if (ss == NULL)
        return -8;
    ss->opts.lowpass = uint16_t(cutoff);   // the low-pass the voice starts with
    SNDCTRL_lowpass(ss->voice, cutoff);
    return 0;
}

// FUNC_AT(0x0013c880)
int SNDSTRM_pitchmult(int stream, int mult) {
    SND::StreamState *ss = SNDSTRMI_getstreamptr(stream);
    if (ss == NULL)
        return -8;
    ss->opts.pitchMult = uint16_t(mult);
    SNDpitchmult(ss->voice, mult);
    return 0;
}

// FUNC_AT(0x0013c8c0)
int SNDSTRM_vol(int stream, int vol) {
    SND::StreamState *ss = SNDSTRMI_getstreamptr(stream);
    if (ss == NULL)
        return -8;
    ss->opts.vol = int8_t(vol);
    SNDvol(ss->voice, vol);
    return 0;
}

// FUNC_AT(0x0013f9b0)
int SNDSTRM_setgreedylevel(int stream, int level) {
    SND::StreamState *ss = SNDSTRMI_getstreamptr(stream);
    if (ss == NULL)
        return -8;
    STREAM_setgreedylevel(ss->stream, level);
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
int SNDSTRM_createtap(SND::StrmReader *stream, SND::PlayOpts *opts, int requests, int packets, void *memory,
                      int size) {
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
        SNDCTRL_filteradd(ss->voice, int(uintptr_t(ss->filter)));
    ss->state = 1;
}

// Bytes per second of a format: rate x channels x the representation's bytes per frame in 1/256ths (Xbox ADPCM
// 0x90, EA-XA 0x88, MicroTalk 0x33, 16-bit 0x200; Layer 3 is 8 bytes a second per channel); 0 for the others.
// FUNC_AT(0x0013ba30)
int SNDSTRMI_calcdatarate(SND::StreamFormat *format) {
    uint32_t frames = format->sampleRate * format->channels;
    uint32_t perFrame;
    switch (format->sampleRep) {
    case kRepXboxAdpcm:
        perFrame = 0x90;
        break;
    case kRepEaXa:
        perFrame = 0x88;
        break;
    case kRepMicroTalk10:
        perFrame = 0x33;
        break;
    case kRepS16:
        perFrame = 0x200;
        break;
    case kRepLayer3:
        return format->channels * 8;
    default:
        perFrame = 0;
        break;
    }
    return int32_t(frames * perFrame) >> 8;   // IMUL: wraps; then an arithmetic shift
}

// FUNC_AT(0x0013baa0)
SND::StreamState* SNDSTRMI_getstreamptr(int stream) {
    if (stream < NumStreams && stream >= 0)
        return StreamArray[stream];
    return NULL;
}

// The request is finished: back to the free list
// FUNC_AT(0x0013bac0)
void SNDSTRMI_removerequest(int id) {
    SND::StreamState *ss = StreamArray[id & 0xff];
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
    uint32_t *chunk = reinterpret_cast<uint32_t **>(data)[-1];
    SND::StreamState *ss = StreamArray[chunk[0] & 0xff];
    return StreamReleaseEax(ss->stream, chunk);
}

// The mixer consumed frames: they count against the oldest requests in turn, each finished one is removed (at
// most 200 in one call; past that the oldest is removed whatever it holds)
// FUNC_AT(0x0013bb50)
void SNDSTRMI_framescallback(int player, uint32_t frames, SND::StreamState *ss) {
    (void)player;
    SND::StreamRequest *r = Oldest(ss);
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
            SND::StreamState *owner = StreamArray[id & 0xff];
            SND::StreamRequest *done = SNDSTRMI_getrequestptr(id);
            LinkRemove(&owner->active, done);
            LinkPush(&owner->freeRequests, done);
            if (owner->current == done)
                owner->current = NULL;
        }
        if (rest == 0)
            return;
        r = Oldest(ss);
        frames = rest;
        rest = 0;
        round++;
        if (round > 200)
            break;
    }
    SNDSTRMI_removerequest(r->id);
}

// An SCHl chunk: the next request's header. A header that differs from the playing one (format, attributes, or
// new per-channel stretch data) restarts the player - at once if nothing plays yet, else once the player has
// drained (state 2, SNDSTRMI_service).
// FUNC_AT(0x0013bc20)
int SNDSTRMI_parseheader(int stream, uint32_t *chunk) {
    SND::StreamState *ss = StreamArray[stream];
    if (ss->current == NULL)
        ss->current = Oldest(ss);
    else
        ss->current = ss->current->next;
    SND::StreamRequest *r = ss->current;
    SND::StreamLayout layout;
    SNDI_patchtohdr(0, (uint8_t *)(chunk + 2), &ss->nextFormat, &ss->nextAttributes, &layout);   // the PT tags at +8
    r->total = layout.frames;
    r->started = 0;

    // the header's user data (tag 0x14) to the user-data clients - none register. No bound on the entries, as in
    // the original: the first NULL one ends the walk.
    SND::Attributes &next = ss->nextAttributes;
    for (int k = 0; next.userData[k] != NULL; k++) {
        SND::UserDataInfo message;
        message.operation = 3;
        message.data = next.userData[k];
        message.size = next.userDataSize[k];
        message.handle = 0;
        message.request = r->id;
        next.userData[k] = NULL;
        next.userDataSize[k] = 0;
        for (int i = 0; i < NumUserDataClients; i++)
            UserDataClients[i](&message);
    }

    STREAM_release(ss->stream, chunk);
    r->rate = SNDSTRMI_calcdatarate(&ss->nextFormat);

    bool same = memcmp(&ss->format, &ss->nextFormat, sizeof(ss->format)) == 0 &&
                memcmp(&ss->attributes, &ss->nextAttributes, sizeof(ss->attributes)) == 0;
    if (!same || ss->nextAttributes.stretchData[0] != NULL) {
        if (ss->format.sampleRate != 0) {   // playing: restart once drained
            ss->state = 2;
            return 0;
        }
        TakeNextHeader(ss);
        ss->nextAttributes.stretchData[0] = NULL;
    }
    if (ss->state != 1) {   // SNDSTRMI_startstream, inlined (it sets the state twice)
        ss->voice = SNDPKTPLAY_start(ss->player, &ss->format, &ss->attributes, &ss->opts);
        if (ss->hasFilter != 0)
            SNDCTRL_filteradd(ss->voice, int(uintptr_t(ss->filter)));
        ss->state = 1;
        ss->state = 1;
    }
    return 0;
}

// An SCDl chunk: submitted as one packet; an empty chunk goes straight back.
// FUNC_AT(0x0013bdb0)
int SNDSTRMI_parsedata(SND::StreamState *ss, uint32_t *chunk) {
    DataChunk *data = reinterpret_cast<DataChunk *>(chunk);
    SND::Packet packet;
    packet.unused00 = 0;
    packet.unused08 = 0;
    packet.frames = data->frames & 0x7fffffff;
    int channels = ss->format.channels;
    uint8_t *base = (uint8_t *)&data->offsets[channels];
    for (int i = 0; i < channels; i++)
        packet.channels[i] = base + data->offsets[i];
    SND::StreamRequest *r = ss->current;
    if ((packet.frames & 0x7fffffff) == 0)
        return StreamReleaseEax(ss->stream, chunk);
    reinterpret_cast<uint32_t **>(packet.channels[0])[-1] = chunk;   // for SNDSTRMI_releasecallback
    data->tag = r->id;
    r->outstanding += packet.frames & 0x7fffffff;
    packet.frames = (uint32_t(r->started) << 31) | (packet.frames & 0x7fffffff);
    uint32_t result = SNDPKTPLAY_submit(ss->player, &packet);
    r->started = 1;
    return result;
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
    if (BufferedMs(ss->stream, r->rate) < uint32_t(r->hold) && STREAM_state(ss->stream) != 2) {
        if (ss->freeRequests.count > 0)
            return 1;
        if (STREAM_state(ss->stream) != 0)
            return 1;
    }
    r->hold = 0;
    return 0;
}

// The main-thread server client (registered while any stream exists): per stream, a pending restart, then up to
// as many chunks as the player has room for (10 before it plays). A header ends the stream's turn. The record is
// read again from sndss for each chunk handed on, as the original does.
// FUNC_AT(0x0013bf10)
void SNDSTRMI_service(void) {
    SNDSYS_entercritical();
    for (int i = 0; i < NumStreams; i++) {
        SND::StreamState *ss = StreamArray[i];
        if (ss == NULL || ss->active.count == 0)
            continue;
        if (ss->state == 2) {
            if (SNDPKTPLAY_framesoutstanding(ss->player) > 0)
                continue;
            TakeNextHeader(ss);
            ss->nextAttributes.stretchData[0] = NULL;
            SNDPKTPLAY_stop(ss->player);
            SNDSTRMI_startstream(ss);
        }
        if (SNDSTRMI_isheld(ss) != 0)
            continue;
        int n;
        if (ss->state == 1) {
            n = SNDPKTPLAY_submitspace(ss->player);
            if (n == 0)
                continue;
        } else {
            n = 10;
        }
        int got = 0;
        do {
            n--;
            uint32_t *chunk = STREAM_get(ss->stream);
            if (chunk == NULL) {
                if (got == 0)
                    break;
            } else if (chunk[0] == kChunkData) {
                SNDSTRMI_parsedata(StreamArray[i], chunk);
                got = 1;
            } else if (chunk[0] == kChunkHeader) {
                SNDSTRMI_parseheader(i, chunk);
                break;
            } else {
                STREAM_release(StreamArray[i]->stream, chunk);
                got = 1;
            }
        } while (n > 0);
    }
    SNDSYS_leavecritical();
}

// A request: type 0 a file (STREAM_queuefile), 1 memory (STREAM_queuemem), else the caller's STREAM request.
// Answers the request id, -1 if the STREAM refused it, -13 with no free request record.
// FUNC_AT(0x0013c060)
int SNDSTRMI_queue(int stream, int hold, const void *source, uint32_t arg, int type) {
    SND::StreamState *ss;
    if (stream >= NumStreams || stream < 0 || (ss = StreamArray[stream]) == NULL)
        return -8;
    if (ss->freeRequests.count == 0)
        return -13;
    uint32_t request;
    if (type == 0) {
        request = STREAM_queuefile(ss->stream, static_cast<const char *>(source), int(arg), kChunkEnd);
    } else if (type == 1) {
        SND_UNTESTED("SNDSTRMI_queue (memory)");
        request = STREAM_queuemem(ss->stream, static_cast<const uint32_t *>(source), 0, kChunkEnd);
    } else {
        SND_UNTESTED("SNDSTRMI_queue (caller's request)");
        request = arg;
    }
    if (request == 0)
        return -1;
    SNDSYS_entercritical();
    SND::StreamRequest *r = LinkPop(&ss->freeRequests);
    memclr(r, sizeof(*r));
    LinkPushTail(&ss->active, r);
    r->streamRequest = request;
    ss->generation += 0x100;
    if (ss->generation < 0)
        ss->generation = 0;
    r->id = ss->generation | stream;
    r->hold = hold;
    int id = r->id;
    SNDSYS_leavecritical();
    return id;
}

// The on-exit function
// FUNC_AT(0x0013c320)
int SNDSTRMI_destroyall(void) {
    for (int i = 0; i < NumStreams; i++)
        SNDSTRM_destroy(i);
    return 0;
}

// The memory is laid out as the record (0x138), the request records, the packet player, then the STREAM's ring in
// whatever is left. Answers the stream index, -9 with no free slot or STREAM, or the packet player's error.
// FUNC_AT(0x0013c350)
int SNDSTRMI_create(SND::PlayOpts *opts, int requests, int packets, void *memory, int size, SND::StrmReader *stream,
                    int tap) {
    int n = NumStreams, index = 0;
    if (n <= 0)
        return -9;
    while (StreamArray[index] != NULL) {
        index++;
        if (index >= n)
            return -9;
    }
    SND::StreamState *ss = (SND::StreamState *)memory;
    memclr(ss, sizeof(*ss));
    SND::StreamRequest *records = (SND::StreamRequest *)(ss + 1);
    uint8_t *playerMemory = (uint8_t *)(records + requests);
    int left = size + (-kStateSize - requests * kRequestSize);
    int playerSize = SNDPKTPLAY_overhead(packets);
    left -= playerSize;
    uint8_t *streamMemory = playerMemory + playerSize;
    SNDLINKI_init(&ss->active);
    SNDLINKI_init(&ss->freeRequests);
    for (int k = 0; k < requests; k++)
        LinkPush(&ss->freeRequests, &records[k]);
    ss->player = SNDPKTPLAY_create(ReleaseEntry, FramesEntry, ss, playerMemory, SNDPKTPLAY_overhead(packets));
    if (ss->player < 0)
        return ss->player;
    if (tap != 0) {
        SND_UNTESTED("SNDSTRMI_create (tap)");
        ss->stream = stream;
        ss->external = 1;
    } else {
        SND::StrmReader *s =
            STREAM_create(requests + 2, 1, 1, reinterpret_cast<SND::StrmInternal *>(streamMemory), left);
        if (s == NULL) {
            SNDPKTPLAY_destroy(ss->player);
            return -9;
        }
        ss->stream = s;
        ss->external = 0;
        int bytes = STREAM_buffersize(s);
        // the STREAM pointer where SNDSTRM_setgreedylevel wants a stream index: it answers -8 and sets nothing
        SNDSTRM_setgreedylevel(int(uintptr_t(ss->stream)), bytes / 3);
    }
    ss->generation = 0;
    ss->voice = -1;
    ss->opts = *opts;
    int live = 0;
    n = NumStreams;
    for (int i = 0; i < n; i++)
        if (StreamArray[i] != NULL)
            live++;
    if (live == 0) {   // the first one: service streams from now on
        iSNDserveraddclient(ServiceEntry);
        StreamExitHook = DestroyAllEntry;
    }
    StreamArray[index] = ss;
    ss->nextAttributes.stretchData[0] = NULL;
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
    SND::StreamRequest *r = Oldest(ss);
    while (r != NULL && r->id != id)
        r = r->next;
    return r;
}

// ---------------------------------------------------------------------------------------------------------------
// The packet player
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0013e8c0)
int SNDPKTPLAY_overhead(int packets) {
    int extra = DummyGetNullValue();
    return extra + packets * 32 + 0x7c;
}

// FUNC_AT(0x0013e8e0)
int SNDPKTPLAY_create(SND::PacketReleaseFn release, SND::PacketFramesFn framesDone, void *context, void *memory,
                      int size) {
    SNDSYS_entercritical();
    int n = NumStreams, index = 0;
    if (n > 0) {
        while (PacketPlayers[index] != NULL) {
            index++;
            if (index >= n)
                break;
        }
    }
    if (index >= n) {
        SNDSYS_leavecritical();
        return -9;
    }
    if (DummyGetNullValue(index, memory) < 0) {
        SNDSYS_leavecritical();
        return -6;
    }
    SND::PacketPlayer *p = (SND::PacketPlayer *)((uint8_t *)memory + DummyGetNullValue());
    int extra = DummyGetNullValue();
    p->slots = int16_t(uint32_t(size - extra - 0x7c) >> 5);   // an unsigned shift
    p->memory = memory;
    p->release = release;
    p->framesDone = framesDone;
    p->context = context;
    p->voice = -1;
    PacketPlayers[index] = p;
    SNDSYS_leavecritical();
    return index;
}

// Allocates the voice(s) for the format (priority 101) on the first render mode that has room and starts the
// platform's packet voice; answers the voice handle or the last error.
// FUNC_AT(0x0013e980)
int SNDPKTPLAY_start(int player, SND::StreamFormat *format, SND::Attributes *attributes, SND::PlayOpts *opts) {
    SND::PatchHeader header;   // what SNDI_validrendermode reads: the channel count and the render mode
    memset(&header, 0, sizeof(header));
    uint8_t channelCount = format->channels;
    header.channels = channelCount;
    int count = channelCount;
    int modeIndex = 0;
    SND::PacketPlayer *p = PacketPlayers[player];
    header.renderMode = attributes->renderMode;
    p->format = *format;
    p->serial = 0;
    p->queuedFrames = 0;
    p->takenFrames = 0;
    p->writeIndex = 0;
    p->releaseIndex = 0;
    p->waiting = 1;
    for (int i = 0; i < p->format.channels; i++) {
        p->count[i] = 0;
        p->readIndex[i] = 0;
        p->stretchData[i] = attributes->stretchData[i];
    }
    for (;;) {
        int mode = SNDI_validrendermode(&modeIndex, &header);
        if (mode == 0)
            break;
        int first, end, handle;
        SNDPLATFORM_getvoicerange(mode, &first, &end);
        int voice = SNDVOICEI_alloc(count, 0x65, &handle, first, end);
        if (voice < 0) {
            p->voice = -9;
            continue;
        }
        p->voice = handle;
        int channels = format->channels;
        int master = -1;
        if (channels > 0) {
            const int16_t *platformVoices = VoiceArray[voice].platformVoices;
            int highest = -1;
            for (int i = 0; i < channels; i++)
                if (platformVoices[i] > highest) {
                    highest = platformVoices[i];
                    master = i;
                }
        }
        p->master = int8_t(master);
        SND::Voice *v = &VoiceArray[voice];
        v->builtinAzimuth = 0;
        v->azimuth = opts->azimuth;
        v->bank = -1;   // a stream
        v->detune = attributes->detune;
        v->pitchMult = opts->pitchMult;
        v->fadeStep = 0;
        v->fade = opts->vol << 16;
        v->envStep = 0;
        v->envTicks = 0x7fffffff;
        v->env = 0x7f0000;
        v->builtinVol = attributes->vol;
        v->bend = opts->bend;
        v->envCount = 1;
        v->envCurrent = 0;
        v->envRelease = 0;
        v->builtinFxLevel = attributes->fxLevel;
        v->progVol = opts->progVol;
        v->fxLevel = opts->fxLevel;
        v->key = 0;
        v->keyLast = 0;
        v->envTable = NULL;
        v->volTable = NULL;
        v->bendTable = NULL;
        v->volLfo = NULL;
        v->bendRange = int16_t(int8_t(attributes->bendRange) * 100);   // the byte taken as signed
        v->pitchLfo = NULL;
        for (int i = 0; i < format->channels; i++)
            v->channelAzimuth[i] = attributes->azimuth[i];
        v->sustainEnd = -1;
        v->frames = 0;
        v->sampleRate = format->sampleRate;
        v->sampleRep = format->sampleRep;
        v->channels = format->channels;
        v->renderMode = uint16_t(mode);
        v->detuneLinear = 0;
        iSNDcalcpitch(voice);
        iSNDcalcvol(voice);
        SNDI_calcfxlevel(0, voice);
        int result = SNDPLATFORM_packetplay(player, voice, opts->timeMult, opts->distort, opts->lowpass,
                                            opts->highpass, format, attributes->stretchData);
        if (result >= 0)
            break;
        for (int k = 0; k < count; k++)   // the voice array re-read for each
            SNDVOICEI_free(VoiceArray[voice].platformVoices[k]);
        p->voice = result;
    }
    return p->voice;
}

// Queues a packet on every channel; -13 when the ring is full (one slot always stays empty). Answers the packet's
// serial.
// FUNC_AT(0x0013ecc0)
uint32_t SNDPKTPLAY_submit(int player, SND::Packet *packet) {
    SND::PacketPlayer *p = PacketPlayers[player];
    if (p->count[0] >= p->slots - 1)
        return uint32_t(-13);
    SND::PacketSlot *slot = &p->slot[p->writeIndex];
    slot->frames = (slot->frames & 0x80000000) | (packet->frames & 0x7fffffff);   // the two bit fields in turn
    slot->frames = (packet->frames & 0x80000000) | (slot->frames & 0x7fffffff);
    slot->serial = p->serial;
    for (int i = 0; i < p->format.channels; i++) {
        slot->channels[i] = packet->channels[i];
        p->count[i]++;
    }
    p->queuedFrames += packet->frames & 0x7fffffff;
    uint32_t serial = p->serial;
    p->serial = serial + 1;
    p->writeIndex++;
    p->waiting = 1;
    if (p->writeIndex >= p->slots)
        p->writeIndex = 0;
    return serial;
}

// FUNC_AT(0x0013eda0)
int SNDPKTPLAY_submitspace(int player) {
    SND::PacketPlayer *p = PacketPlayers[player];
    return p->slots - p->count[0] - 1;
}

// FUNC_AT(0x0013edc0)
int SNDPKTPLAY_framesoutstanding(int player) {
    SND::PacketPlayer *p = PacketPlayers[player];
    return p->takenFrames + p->queuedFrames;
}

// FUNC_AT(0x0013ede0)
int SNDPKTPLAY_destroy(int player) {
    DummyGetNullValue(player);
    PacketPlayers[player] = NULL;
    return 0;
}

// The unpacker takes the channel's next packet: its data (-1 if NULL), frames (31 bits) and bit 31 (sign-extended:
// 0 for a request's first packet, -1 after). With none queued, NULL - and on the master channel, once it has run
// dry, the oldest slot still held is released. The master's take moves its frames from queued to taken.
// FUNC_AT(0x0013ee00)
uint8_t* SNDPKTPLAYI_get(int player, int channel, int *frames, int *continued) {
    SND::PacketPlayer *p = PacketPlayers[player];
    if (p->count[channel] == 0) {
        if (channel == p->master && p->waiting == 0 && p->readIndex[channel] != p->releaseIndex)
            ReleaseNext(p, player);
        return NULL;
    }
    SND::PacketSlot *slot = &p->slot[p->readIndex[channel]];
    *frames = Frames31(slot->frames);
    *continued = int32_t(slot->frames) >> 31;
    if (channel == p->master) {
        if (p->readIndex[channel] != p->releaseIndex)
            ReleaseNext(p, player);
        p->queuedFrames -= Frames31(slot->frames);
        p->takenFrames += Frames31(slot->frames);
    }
    p->readIndex[channel]++;
    if (p->readIndex[channel] >= p->slots)
        p->readIndex[channel] = 0;
    p->count[channel]--;
    if (p->count[channel] == 0 && channel == p->master)
        p->waiting = 0;
    uint8_t *data = slot->channels[channel];
    if (data == NULL)
        data = reinterpret_cast<uint8_t *>(-1);
    return data;
}

// The unpacker consumed frames (counted on the master channel only): queued for the frames callback
// FUNC_AT(0x0013ef80)
void SNDPKTPLAYI_freeframes(int player, int channel, int frames) {
    SND::PacketPlayer *p = PacketPlayers[player];
    if (channel != p->master)
        return;
    p->takenFrames -= frames;
    if (p->framesDone != NULL) {
        SND::PacketCallback *c = &PacketCallbacks[PacketCallbackCount];
        c->release = 0;
        c->player = uint16_t(player);
        c->frames = frames;
        PacketCallbackCount++;
    }
}

// Delivers the queued callbacks (the SND thread, after each mix tick)
// FUNC_AT(0x0013efd0)
void SNDPKTPLAYI_flushcallbackdata(void) {
    for (int i = 0; i < PacketCallbackCount; i++) {
        SND::PacketCallback *c = &PacketCallbacks[i];
        int player = c->player;
        SND::PacketPlayer *p = PacketPlayers[player];
        if (c->release == 0)
            p->framesDone(player, c->frames, p->context);
        else
            p->release(c->data, p->context);
    }
    PacketCallbackCount = 0;
}

// FUNC_AT(0x0013f040)
int SNDPKTPLAY_stop(int player) {
    SND::PacketPlayer *p = PacketPlayers[player];
    SNDstop(p->voice);
    SNDPKTPLAYI_flushcallbackdata();
    p->voice = -1;
    for (int i = 0; i < p->format.channels; i++)
        if (p->stretchData[i] != NULL)
            SNDMEMI_free(p->stretchData[i]);
    return 0;
}

// The packet player whose voice is this voice index, -1 for none
// FUNC_AT(0x001457e0)
int SNDPKTPLAYI_voicetopackethandle(int voice) {
    for (int i = 0; i < NumStreams; i++) {
        SND::PacketPlayer *p = PacketPlayers[i];
        if (p != NULL && SNDVOICEI_get(p->voice) == voice)
            return i;
    }
    return -1;
}
