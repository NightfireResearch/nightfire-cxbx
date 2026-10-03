#ifndef DRIVING_SOUND_SND_STREAMS_H_
#define DRIVING_SOUND_SND_STREAMS_H_

// EA's sound library, module D: streams on the SND side (docs/driving/sound.md 2.5, 3.5, 4.4) - the SNDSTRM API the
// game's AStream calls, the per-stream request queue and SCHl/SCDl chunk parser (SNDSTRMI), and the packet player
// the mixer's unpacker pulls from (SNDPKTPLAY/SNDPKTPLAYI). See Streams.cpp. SNDI_patchtohdr is Banks.cpp's.

#include "SndUntested.h"   // first: System.h keeps its own guarded copy for now
#include "Banks.h"
#include "Voices.h"
#include "System.h"

#include <stddef.h>
#include <stdint.h>

namespace SND {

struct StrmReader;   // Stream.h

// SNDLINKLIST (LinkList) is System.h's.

// A queued request of a stream (0x28 each, after the SNDSTRMI record in the memory AStreamPriv supplies)
struct StreamRequest {
    StreamRequest *next;         // +0x00 } the SNDLINKI node
    StreamRequest *prev;         // +0x04 }
    uint32_t streamRequest;      // +0x08 STREAM_queuefile's handle (or the caller's, SNDSTRM_queuerequestid)
    int32_t id;                  // +0x0c generation | stream index (the handle the game holds)
    uint32_t rate;               // +0x10 bytes per second (SNDSTRMI_calcdatarate), 0 until its header
    uint32_t played;             // +0x14 frames the mixer has consumed
    uint32_t total;              // +0x18 frames in the stream (tag 0x85)
    uint32_t outstanding;        // +0x1c frames submitted to the packet player, not yet consumed
    int32_t hold;                // +0x20 ms of data to buffer before starting; < 0 holds until released
    uint8_t started;             // +0x24 its first data chunk has been submitted
    uint8_t pad25[3];
};
static_assert(sizeof(StreamRequest) == 0x28, "a stream request is 0x28 bytes");

// SNDSTRMI (0x138): sndss[32] at 0x00244ba8 (NUM_STREAMS at 0x00244d0f)
struct StreamState {
    StrmReader *stream;          // +0x00 the STREAM (STREAM_create's, or the caller's for a tap)
    int32_t voice;               // +0x04 SNDPKTPLAY_start's voice handle, -1 none
    int32_t player;              // +0x08 the packet player
    int32_t generation;          // +0x0c request id generation, += 0x100 per request
    uint8_t state;               // +0x10 0 idle, 1 playing, 2 restart once the player has drained
    uint8_t external;            // +0x11 1: the STREAM is the caller's (SNDSTRM_createtap)
    uint8_t pad12[2];
    StreamFormat format;         // +0x14 the playing format
    StreamFormat nextFormat;     // +0x18 the last header's
    Attributes attributes;       // +0x1c the playing attributes
    Attributes nextAttributes;   // +0x84 the last header's (its stretch data are owned here until copied)
    PlayOpts opts;               // +0xec
    uint8_t filter[0x10];        // +0x104 handed to SNDCTRL_filteradd when hasFilter
    int32_t hasFilter;           // +0x114 (nothing in this module sets it)
    int32_t pad118;              // +0x118
    LinkList active;             // +0x11c queued requests, oldest first
    LinkList freeRequests;       // +0x128
    StreamRequest *current;      // +0x134 the request whose chunks are being parsed
};
static_assert(sizeof(StreamState) == 0x138, "SNDSTRMI is 0x138 bytes");

// A packet as SNDSTRMI_parsedata builds it on its stack for SNDPKTPLAY_submit (0x24)
struct Packet {
    uint32_t unused00;           // +0x00 never read
    uint32_t frames;             // +0x04 bit 31: not the request's first packet
    uint32_t unused08;           // +0x08 never read
    uint8_t *channels[6];        // +0x0c per channel: its data in the chunk
};
static_assert(sizeof(Packet) == 0x24, "a packet is 0x24 bytes");

// A packet in the player's ring (0x20)
struct PacketSlot {
    uint32_t serial;             // +0x00 the player's packet counter at submission
    uint32_t frames;             // +0x04 as submitted (bit 31 included)
    uint8_t *channels[6];        // +0x08
};
static_assert(sizeof(PacketSlot) == 0x20, "a packet slot is 0x20 bytes");

typedef int (*PacketReleaseFn)(uint8_t *data, void *context);              // SNDSTRMI_releasecallback
typedef void (*PacketFramesFn)(int player, uint32_t frames, void *context);  // SNDSTRMI_framescallback

// SNDPKTPLAY (0x5c + 0x20 per slot): sndpps at 0x002452e4, in memory its creator supplies
struct PacketPlayer {
    int32_t voice;               // +0x00 voice handle, -1 none (or SNDPKTPLAY_start's error)
    uint32_t serial;             // +0x04 packets submitted
    int16_t readIndex[6];        // +0x08 per channel: the next slot it takes
    int16_t count[6];            // +0x14 per channel: slots submitted, not yet taken
    int16_t slots;               // +0x20
    int8_t master;               // +0x22 the channel whose consumption counts (its voice is the highest)
    uint8_t waiting;             // +0x23 set by start and submit, cleared when the master runs dry
    int16_t releaseIndex;        // +0x24 the next slot to hand back (release callback)
    int16_t writeIndex;          // +0x26
    int32_t queuedFrames;        // +0x28 submitted, not taken by the master
    int32_t takenFrames;         // +0x2c taken by the master, not reported consumed
    void *memory;                // +0x30
    PacketReleaseFn release;     // +0x34
    PacketFramesFn framesDone;   // +0x38
    void *context;               // +0x3c
    StreamFormat format;         // +0x40
    uint8_t *stretchData[6];     // +0x44 the attributes' per-channel time-stretch data (freed by SNDPKTPLAY_stop)
    PacketSlot slot[1];          // +0x5c [slots]
};
static_assert(offsetof(PacketPlayer, slot) == 0x5c, "the packet slots start at +0x5c");

// A queued packet callback: count at 0x00244fe0, entries at 0x00244fe4 (96 of them before sndpps)
struct PacketCallback {
    uint16_t release;            // +0x00 1: the release callback, 0: the frames callback
    uint16_t player;             // +0x02
    union {                      // +0x04
        uint8_t *data;           //       release: the slot's first channel
        uint32_t frames;         //       frames: the frames consumed
    };
};
static_assert(sizeof(PacketCallback) == 8, "a packet callback is 8 bytes");

// SNDSTRM_status's answer
struct StreamStatus {
    int32_t requests;            // +0x00 queued requests
    int32_t id;                  // +0x04 the oldest request's id
    uint32_t bufferedMs;         // +0x08
};

// SNDSTRM_requeststatus's answer
struct RequestStatus {
    int32_t state;               // +0x00 0 not started, 1 queued, 2 playing, 3 finished (no such request)
    uint32_t playedMs;           // +0x04
    uint32_t remainingMs;        // +0x08
    uint32_t outstandingMs;      // +0x0c
};

}  // namespace SND

// The API (stream index in; 0 or a negative error out)
int SNDSTRM_autovol(int stream, int time, int vol);                                         // 0x0013b990
int SNDSTRM_queuefile(int stream, int hold, const char *name, uint32_t offset);              // 0x0013c160
int SNDSTRM_purge(int stream);                                                               // 0x0013c180
int SNDSTRM_destroy(int stream);                                                             // 0x0013c280
int SNDSTRM_create(SND::PlayOpts *opts, int requests, int packets, void *memory, int size);  // 0x0013c560
int SNDSTRM_modifyhold(int id, int hold);                                                    // 0x0013c590
int SNDSTRM_overheadtap(int requests, int packets);                                          // 0x0013c610 (dead)
int SNDSTRM_overhead(int requests, int packets);                                             // 0x0013c630
int SNDSTRM_requeststatus(int id, SND::RequestStatus *status);                               // 0x0013c660
int SNDSTRM_status(int stream, SND::StreamStatus *status);                                   // 0x0013c740
int SNDSTRM_3dpos(int stream, int azimuth, int elevation);                                   // 0x0013c800
int SNDSTRM_lowpass(int stream, int cutoff);                                                 // 0x0013c840
int SNDSTRM_pitchmult(int stream, int mult);                                                 // 0x0013c880
int SNDSTRM_vol(int stream, int vol);                                                        // 0x0013c8c0
int SNDSTRM_setgreedylevel(int stream, int level);                                           // 0x0013f9b0
int SNDSTRM_queuerequestid(int stream, int hold, uint32_t request);                          // 0x00150360 (dead)
int SNDSTRM_createtap(SND::StrmReader *stream, SND::PlayOpts *opts, int requests, int packets, void *memory,
                      int size);                                                             // 0x00150380 (dead)

// The stream internals
void SNDSTRMI_startstream(SND::StreamState *ss);                                             // 0x0013b9e0
int SNDSTRMI_calcdatarate(SND::StreamFormat *format);                                        // 0x0013ba30
SND::StreamState* SNDSTRMI_getstreamptr(int stream);                                         // 0x0013baa0
void SNDSTRMI_removerequest(int id);                                                         // 0x0013bac0
int SNDSTRMI_releasecallback(uint8_t *data, void *context);                                  // 0x0013bb20
void SNDSTRMI_framescallback(int player, uint32_t frames, SND::StreamState *ss);             // 0x0013bb50
int SNDSTRMI_parseheader(int stream, uint32_t *chunk);                                       // 0x0013bc20
int SNDSTRMI_parsedata(SND::StreamState *ss, uint32_t *chunk);                               // 0x0013bdb0
int SNDSTRMI_isheld(SND::StreamState *ss);                                                   // 0x0013be80
void SNDSTRMI_service(void);                                                                 // 0x0013bf10
int SNDSTRMI_queue(int stream, int hold, const void *source, uint32_t arg, int type);        // 0x0013c060
int SNDSTRMI_destroyall(void);                                                               // 0x0013c320
int SNDSTRMI_create(SND::PlayOpts *opts, int requests, int packets, void *memory, int size, SND::StrmReader *stream,
                    int tap);                                                                // 0x0013c350
SND::StreamRequest* SNDSTRMI_getrequestptr(int id);                                          // 0x0013f180

// The packet player
int SNDPKTPLAY_overhead(int packets);                                                        // 0x0013e8c0
int SNDPKTPLAY_create(SND::PacketReleaseFn release, SND::PacketFramesFn framesDone, void *context, void *memory,
                      int size);                                                             // 0x0013e8e0
int SNDPKTPLAY_start(int player, SND::StreamFormat *format, SND::Attributes *attributes,
                     SND::PlayOpts *opts);                                                   // 0x0013e980
uint32_t SNDPKTPLAY_submit(int player, SND::Packet *packet);                                 // 0x0013ecc0
int SNDPKTPLAY_submitspace(int player);                                                      // 0x0013eda0
int SNDPKTPLAY_framesoutstanding(int player);                                                // 0x0013edc0
int SNDPKTPLAY_destroy(int player);                                                          // 0x0013ede0
uint8_t* SNDPKTPLAYI_get(int player, int channel, int *frames, int *continued);              // 0x0013ee00
void SNDPKTPLAYI_freeframes(int player, int channel, int frames);                            // 0x0013ef80
void SNDPKTPLAYI_flushcallbackdata(void);                                                    // 0x0013efd0
int SNDPKTPLAY_stop(int player);                                                             // 0x0013f040
int SNDPKTPLAYI_voicetopackethandle(int voice);                                              // 0x001457e0

#endif // DRIVING_SOUND_SND_STREAMS_H_
