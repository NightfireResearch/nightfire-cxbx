#ifndef DRIVING_AUDIO_STREAM_H_
#define DRIVING_AUDIO_STREAM_H_

// ---------------------------------------------------------------------------------------------------------------
// AStream: a streamed sound (music, speech) - an ABaseSound whose AStreamPriv owns one SND stream (SNDSTRM_create
// over memory of its own) and a queue of stream events. AStream::Event queues a file; AStream::Play, the sound's
// per-frame update, sets the stream's volume, pitch, low pass and azimuth, reads its status, and when the playing
// request is done (or the next event's fade time has run out) queues the next event's file. Streams are named
// objects (URefCounter<AStream>): Create makes one, Get finds it, Remove deletes it. See Stream.cpp and
// docs/driving/sound.md 3.5 for the SND side.
//
// The event queue is the game's compiled std::deque<AStreamEntry> (one entry per block: an entry is over 8 bytes),
// and the stream registry's map the compiled std::map of URefCounter<AStream> (engine/URefCounter.h), whose tree
// code is here.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "Sound.h"                      // ABaseSound, ASoundPlayParams
#include "Mix.h"                        // RefCounterTree
#include "../engine/URefCounter.h"
#include "../sound/snd/Streams.h"       // SND::StreamStatus, SND::RequestStatus

struct AVoiceMapNode;

// A queued stream event (0x44 bytes; the type's and fields' names are ours)
struct AStreamEntry {
    char name[0x38];            // +0x00 the file's name, lower case, '/' made '\'
    float fadeTime;             // +0x38 seconds the playing stream fades out over before this one starts; below
                                //       zero, it plays to its end
    uint8_t loop;               // +0x3c
    uint8_t effect;             // +0x3d
    uint8_t held;               // +0x3e queued held (SNDSTRM's hold -1) until the hold is released
    uint8_t unknown3f;
    int32_t eventTick;          // +0x40 the stream's last Play tick when the event was queued

    AStreamEntry* Construct(const char *name, float fadeTime, bool loop, bool effect, bool held,
                            int eventTick);                                     // 0x00121ea0
};
static_assert(sizeof(AStreamEntry) == 0x44, "a stream event is 0x44 bytes");

// std::deque<AStreamEntry>: a map of blocks of one entry each, used as a ring from `offset`
struct AStreamQueue {
    uint8_t allocator;          // +0x00
    uint8_t unknown01[3];
    AStreamEntry **map;         // +0x04
    uint32_t mapSize;           // +0x08
    uint32_t offset;            // +0x0c
    uint32_t size;              // +0x10

    bool Empty() const { return size == 0; }

    void PopFront();                                                            // 0x001220c0
    void Clear();               // every entry, every block and the map freed   0x001220f0
    void Destruct();                                                            // 0x00122160
    AStreamEntry* Front();                                                      // 0x00122170
    void ThrowTooLong();                                                        // 0x00122a90
    void GrowMap(uint32_t count);                                               // 0x001230f0
    void PushBack(const AStreamEntry &entry);                                   // 0x00123380

private:
    void PopBack() {
        if (size != 0 && --size == 0)
            offset = 0;
    }
};
static_assert(sizeof(AStreamQueue) == 0x14, "a deque is 0x14 bytes");

// The stream's own state (0x128 bytes, "AStreamPriv" from the pools; the field names are ours)
class AStreamPriv {
public:
    AStreamQueue queue;             // +0x00
    SND::StreamStatus status;       // +0x14 SNDSTRM_status's, each Play
    SND::RequestStatus request;     // +0x20 SNDSTRM_requeststatus's for the playing request, each Play
    char *file;                     // +0x30 the playing request's file name (64 bytes from operator new); NULL
                                    //       when none
    float filter;                   // +0x34 SetFilter's: a low pass of |filter| x 24000 Hz below 1
    float fade;                     // +0x38 seconds faded so far
    float volume;                   // +0x3c the volume the last Play set (GetInternalVolume)
    uint32_t memorySize;            // +0x40 SNDSTRM_overhead(4, 15) + the caller's buffer size
    int32_t stream;                 // +0x44 SNDSTRM_create's stream; -1 none
    int32_t requestId;              // +0x48 SNDSTRM_queuefile's request; -1 none
    int32_t eventTick;              // +0x4c the playing event's eventTick (GetLatency counts from it)
    int32_t playTick;               // +0x50 TIMER_gettick at the last Play
    int32_t bigAdded;               // +0x54 FILESYS_addbigsync's answer
    int32_t bigId;                  // +0x58
    uint8_t over;                   // +0x5c
    uint8_t notFound;               // +0x5d
    uint8_t paused;                 // +0x5e
    uint8_t loop;                   // +0x5f
    uint8_t effect;                 // +0x60
    uint8_t held;                   // +0x61
    uint8_t unknown62[2];
    void *memory;                   // +0x64 the SND stream's memory (UMemory::Alloc)
    char filePrefix[0x80];          // +0x68 "|": file names are inside the big file
    char name[0x40];                // +0xe8 the playing event's name, its first letter upper case (GetFile)

    AStreamPriv* Construct(const char *file, const char *extension, unsigned int bufferSize);   // 0x00122190
    void Destruct();                                                                           // 0x001222f0
};
static_assert(sizeof(AStreamPriv) == 0x128, "AStreamPriv is 0x128 bytes");
static_assert(offsetof(AStreamPriv, status) == 0x14, "AStreamPriv::status");
static_assert(offsetof(AStreamPriv, request) == 0x20, "AStreamPriv::request");
static_assert(offsetof(AStreamPriv, file) == 0x30, "AStreamPriv::file");
static_assert(offsetof(AStreamPriv, stream) == 0x44, "AStreamPriv::stream");
static_assert(offsetof(AStreamPriv, over) == 0x5c, "AStreamPriv::over");
static_assert(offsetof(AStreamPriv, memory) == 0x64, "AStreamPriv::memory");
static_assert(offsetof(AStreamPriv, name) == 0xe8, "AStreamPriv::name");

class AStream : public ABaseSound {
public:
    AStreamPriv *priv;          // +0xc0
    int32_t holdTimeout;        // +0xc4 ten seconds of timer ticks: a held request not released by then is skipped
    int32_t heldTicks;          // +0xc8 how long the held request has waited
    int32_t releaseTick;        // +0xcc TIMER_gettick when the held request was released (or skipped); 0 when
                                //       queued

    // Names ours (holdTimeout, heldTicks, releaseTick, and the hold state's globals in Stream.cpp).

    AStream* Construct(const char *name, const char *file, const char *extension,
                       unsigned int bufferSize);                                // 0x00122370
    void Destruct();                                                            // 0x00122450
    AStream* Delete(unsigned int flags);                                        // 0x00122550

    // The directory stream files are found in (copied).
    static void SetPath(const char *path);                                      // 0x00121e80

    void SetFilter(float filter);                                               // 0x00121f30
    bool IsOver();                                                              // 0x00121f40
    bool IsNotFound();                                                          // 0x00121f50
    bool IsPaused();                                                            // 0x00121f60
    bool IsLoop();                                                              // 0x00121f70
    bool IsEffect();                                                            // 0x00121f80
    // Fades the stream to silence over three seconds.
    void FadeOut();                                                             // 0x00121f90
    // Seconds since the playing event was queued, less the seconds played; 0 for a loop or with nothing playing.
    double GetLatency();                                                        // 0x00121fc0
    float GetInternalVolume();                                                  // 0x00122020
    // The playing event's name, or "".
    const char* GetFile();                                                      // 0x00122030
    // Ends the playing request; the queue's next event starts at the next Play.
    void Next();                                                                // 0x00122050
    // Empties the queue and ends the playing request.
    void Stop();                                                                // 0x00122430
    // A request that failed (SNDSTRM_queuefile's negative answer): the queue emptied, the request ended.
    void DecodeError(int error);                                                // 0x001224d0
    void Play(ASoundPlayParams *params);                                        // 0x00122580
    // Queues the file `name`.
    void Event(const char *name, float fadeTime, bool loop, bool effect, bool held);   // 0x001234d0

    // A stream named `name` (its mix too) over "<path><file>.<extension>", the extension by the name: "music"
    // music's, "speech" and "nis" speech's, anything else "viv".
    static AStream* Create(const char *name, const char *file, unsigned int bufferSize);   // 0x001236c0
    static AStream* Get(const char *name);                                      // 0x001237d0
    // One reference fewer to the stream `name`, and the stream deleted.
    static void Remove(const char *name);                                       // 0x001237f0
};
static_assert(sizeof(AStream) == 0xd0, "AStream is 0xd0 bytes");

// URefCounter<AStream>'s tree (StreamRefCounter's map, at 0x00243a88): its compiled copies of RefCounterTree's code
// (Mix.h).
class StreamRefTree : public RefCounterTree {
public:
    void EraseSubtree(RefCounterNode *node);                                    // 0x00122500
    RefCounterNode** EraseAt(RefCounterNode **result, RefCounterNode *where);   // 0x00122b10
    RefCounterNode** InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where,
                              const RefCounterValue *value);                    // 0x00122e80
    RefCounterNode** EraseRange(RefCounterNode **result, RefCounterNode *first,
                                RefCounterNode *last);                          // 0x00123070
    RefCounterInsertResult* InsertUnique(RefCounterInsertResult *result,
                                         const RefCounterValue *value);         // 0x001232a0
    // The map's destructor, which only an exception's unwinding reaches.
    void DestroyRange();                                                        // 0x00123560
    // The static's destructor (at exit).
    void Destruct();                                                            // 0x001235a0
};
static_assert(sizeof(StreamRefTree) == 0xc, "a map is 12 bytes");

// The rightmost node under `node`, for the maps with 0x20-byte nodes (the voice debug map's, the collision
// manager's window map, ...): the linker kept one copy, here.
AVoiceMapNode* AVoiceMapMax(AVoiceMapNode *node);                               // 0x00123850

#endif // DRIVING_AUDIO_STREAM_H_
