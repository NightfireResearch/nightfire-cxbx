#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#pragma fp_contract(off)

#include "Stream.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <bit>

#include "Mix.h"
#include "SoundManager.h"               // FramesSinceAudioUpdate, ASoundManager_fgMissionOver, ASystem_fgSystem
#include "Voice.h"                      // AVoiceMapNode
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "../data/Tree.h"               // TreeThrow
#include "../engine/CoreFoundation.h"   // GameEmptyString
#include "../engine/InputConfig.h"      // BuildFileName
#include "../engine/UMemory.hpp"
#include "../platform/FileSys.h"
#include "../platform/RealSystem.h"     // TIMER_gettick, TIMER_getfrequency
#include "../platform/X87.h"
#include "../sound/snd/Streams.h"
#include "../sound/snd/System.h"        // SNDSYS_entercritical
#include "../sound/snd/Voices.h"        // SNDplaysetdef
#include "../world/CollisionQueries.h"  // PointerUninitializedCopy

// ---------------------------------------------------------------------------------------------------------------
// AStream and AStreamPriv (see Stream.h), with the compiled code of their event queue (std::deque<AStreamEntry>)
// and of the stream registry's map. Every SND call is made where the original makes it, under SNDSYS_entercritical
// where it takes it.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address

#define PointerUninitializedFill ((void (*)(AStreamEntry **, uint32_t, AStreamEntry *const *))0x000b2ec0)

// The C runtime's: case-insensitive comparison (the registry's order depends on it) and printf (its formatting).
#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)
#define CRT_printf ((int (*)(const char *, ...))0x00132192)

// ---- globals

#define StreamPath ((char *)0x001d8070)                // char[0x80], "./" until SetPath
#define MusicExtension ((const char *)0x001d7fc8)      // char[8], "mus"
#define SpeechExtension ((const char *)0x001d7fd0)     // char[8], "spe"
#define StreamHold I32_AT(0x00243a80)                  // StreamHoldState (name ours)
#define StreamHoldTick I32_AT(0x00243a84)              // TIMER_gettick when the held request was queued

// A held request: queued with SNDSTRM's hold -1; EReleaseStream releases it.
enum StreamHoldState {
    kHoldNone = 0,
    kHoldWaiting = 1,
    kHoldReleased = 2,
};

constexpr uint32_t kAStreamVtable = 0x001a2910;
constexpr int kStreamRequests = 4;               // SNDSTRM_create's request and packet counts
constexpr int kStreamPackets = 15;
constexpr int kFilePriority = 100;               // FILESYS_existssync's and the big file's priority
constexpr unsigned int kStreamMemoryFlags = 0x400;
constexpr int kStreamHoldMs = 250;               // SNDSTRM_queuefile's hold: ms buffered before it starts
constexpr int kFadeOutMs = 3000;
constexpr uint32_t kQueueMaxSize = 0x03c3c3c3;   // the deque's max_size(): 0xffffffff / sizeof(AStreamEntry)
constexpr float kFrameSeconds = 1.0f / 60.0f;
constexpr float kMsToSeconds = 0.001f;
static_assert(std::bit_cast<uint32_t>(kFrameSeconds) == 0x3c888889, "the original's 1/60");
static_assert(std::bit_cast<uint32_t>(kMsToSeconds) == 0x3a83126f, "the original's 0.001");

// Dead code the disc never reaches, said once
static void StreamUntested(const char *what) {
    printf("[audio] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check it against "
           "the original.\n", what);
    fflush(stdout);
}

#define STREAM_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            StreamUntested(what); \
        } \
    } while (0)

// =============================================================================================================
// The event queue: std::deque<AStreamEntry>
// =============================================================================================================

// FUNC_AT(0x00121ea0)
AStreamEntry* AStreamEntry::Construct(const char *file, float fade, bool loops, bool effects, bool isHeld,
                                      int tick) {
    fadeTime = fade;
    loop = loops;
    effect = effects;
    held = isHeld;
    eventTick = tick;
    strcpy(name, file);
    for (size_t i = 0; i < strlen(name); i++) {
        if (name[i] == '/')
            name[i] = '\\';
        else if (name[i] >= 'A' && name[i] <= 'Z')
            name[i] += 'a' - 'A';
    }
    return this;
}

// FUNC_AT(0x001220c0)
void AStreamQueue::PopFront() {
    if (size == 0)
        return;
    if (mapSize <= ++offset)
        offset = 0;
    if (--size == 0)
        offset = 0;
}

// FUNC_AT(0x001220f0)
void AStreamQueue::Clear() {
    while (size != 0)
        PopBack();
    for (uint32_t block = mapSize; block > 0;) {
        block--;
        if (map[block] != NULL)
            UMemory::FastFree(map[block], sizeof(AStreamEntry));
    }
    if (map != NULL)
        UMemory::FastFree(map, mapSize * sizeof(AStreamEntry *));
    mapSize = 0;
    map = NULL;
}

// The deque's destructor; nothing calls it.
// FUNC_AT(0x00122160)
void AStreamQueue::Destruct() {
    STREAM_UNTESTED("std::deque<AStreamEntry>'s destructor");
    Clear();
}

// FUNC_AT(0x00122170)
AStreamEntry* AStreamQueue::Front() {
    uint32_t block = offset;
    if (mapSize <= block)
        block -= mapSize;
    return map[block];
}

// FUNC_AT(0x00122a90)
void AStreamQueue::ThrowTooLong() {
    TreeThrow("deque<T> too long", kLengthErrorVtable, kLengthErrorThrowInfo);
}

static AStreamEntry **CopyBlocks(AStreamEntry **first, AStreamEntry **last, AStreamEntry **dest) {
    return reinterpret_cast<AStreamEntry **>(PointerUninitializedCopy(reinterpret_cast<void **>(first),
                                                                      reinterpret_cast<void **>(last),
                                                                      reinterpret_cast<void **>(dest)));
}

// _Growmap: a map `count` (or half again, at least 8) blocks longer, the ring's blocks moved so it stays in order
// from `offset`.
// FUNC_AT(0x001230f0)
void AStreamQueue::GrowMap(uint32_t count) {
    if (kQueueMaxSize - mapSize < count)
        ThrowTooLong();
    uint32_t increment = mapSize / 2;
    if (increment < 8)
        increment = 8;
    if (count < increment && mapSize <= kQueueMaxSize - increment)
        count = increment;

    uint32_t first = offset;
    AStreamEntry **newMap = static_cast<AStreamEntry **>(
        UMemory::FastAlloc((mapSize + count) * sizeof(AStreamEntry *), "STL"));
    AStreamEntry **next = CopyBlocks(map + first, map + mapSize, newMap + first);
    AStreamEntry *const none = NULL;
    if (first <= count) {
        next = CopyBlocks(map, map + first, next);
        PointerUninitializedFill(next, count - first, &none);
        PointerUninitializedFill(newMap, first, &none);
    } else {
        CopyBlocks(map, map + count, next);
        next = CopyBlocks(map + count, map + first, newMap);
        PointerUninitializedFill(next, count, &none);
    }
    if (map != NULL)
        UMemory::FastFree(map, mapSize * sizeof(AStreamEntry *));
    map = newMap;
    mapSize += count;
}

// FUNC_AT(0x00123380)
void AStreamQueue::PushBack(const AStreamEntry &entry) {
    if (mapSize <= size + 1)
        GrowMap(1);
    uint32_t block = offset + size;
    if (mapSize <= block)
        block -= mapSize;
    if (map[block] == NULL)
        map[block] = static_cast<AStreamEntry *>(UMemory::FastAlloc(sizeof(AStreamEntry), "STL"));
    if (map[block] != NULL)
        *map[block] = entry;
    size++;
}

// =============================================================================================================
// AStreamPriv
// =============================================================================================================

// FUNC_AT(0x00122190)
AStreamPriv* AStreamPriv::Construct(const char *fileName, const char *extension, unsigned int bufferSize) {
    queue.map = NULL;
    queue.mapSize = 0;
    queue.offset = 0;
    queue.size = 0;
    file = NULL;
    filter = 1.0f;
    fade = 0.0f;
    eventTick = 0;
    playTick = 0;
    bigAdded = 0;
    over = 1;
    notFound = 0;
    paused = 0;
    loop = 0;
    effect = 0;
    held = 0;
    memory = NULL;

    char path[0x40];
    BuildFileName(path, 0, StreamPath, fileName, extension);
    if (FILESYS_existssync(path, kFilePriority))
        bigAdded = FILESYS_addbigsync(path, 0, kFilePriority, &bigId);
    strcpy(filePrefix, "|");
    memorySize = SNDSTRM_overhead(kStreamRequests, kStreamPackets) + bufferSize;
    char memoryName[0x40];
    memory = UMemory::Alloc(memorySize, kStreamMemoryFlags, BuildFileName(memoryName, 0, fileName, " buffer", ""));

    SND::PlayOpts opts;
    SNDplaysetdef(&opts);
    if (ASystem_fgSystem != NULL) {
        int created = SNDSTRM_create(&opts, kStreamRequests, kStreamPackets, memory, memorySize);
        requestId = -1;
        stream = created;
    } else {
        requestId = -1;
        stream = -1;
    }
    return this;
}

// FUNC_AT(0x001222f0)
void AStreamPriv::Destruct() {
    if (stream > -1)
        SNDSTRM_destroy(stream);
    if (memory != NULL)
        UMemory::Free(memory);
    if (bigAdded)
        FILESYS_delbigsync(bigId, kFilePriority);
    queue.Clear();
}

// =============================================================================================================
// AStream
// =============================================================================================================

// FUNC_AT(0x00122370)
AStream* AStream::Construct(const char *name, const char *file, const char *extension, unsigned int bufferSize) {
    ABaseSound::Construct(name, kSoundViewsActive);
    vtable = kAStreamVtable;
    holdTimeout = TIMER_getfrequency() * 10;
    heldTicks = 0;
    releaseTick = 0;
    AStreamPriv *memory = static_cast<AStreamPriv *>(UMemory::FastAlloc(sizeof(AStreamPriv), "AStreamPriv"));
    priv = memory != NULL ? memory->Construct(file, extension, bufferSize) : NULL;
    return this;
}

// FUNC_AT(0x00122450)
void AStream::Destruct() {
    vtable = kAStreamVtable;
    priv->queue.Clear();
    Next();
    AStreamPriv *own = priv;
    if (own != NULL) {
        own->Destruct();
        UMemory::FastFree(own, sizeof(AStreamPriv));
    }
    ABaseSound::Destruct();
}

// FUNC_AT(0x00122550)
AStream* AStream::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        ABaseSound::OperatorDelete(this, sizeof(AStream));
    return this;
}

// FUNC_AT(0x00121e80)
void AStream::SetPath(const char *path) {
    strcpy(StreamPath, path);
}

// FUNC_AT(0x00121f30)
void AStream::SetFilter(float to) {
    priv->filter = to;
}

// FUNC_AT(0x00121f40)
bool AStream::IsOver() {
    return priv->over;
}

// FUNC_AT(0x00121f50)
bool AStream::IsNotFound() {
    return priv->notFound;
}

// FUNC_AT(0x00121f60)
bool AStream::IsPaused() {
    return priv->paused;
}

// FUNC_AT(0x00121f70)
bool AStream::IsLoop() {
    return priv->loop;
}

// FUNC_AT(0x00121f80)
bool AStream::IsEffect() {
    return priv->effect;
}

// FUNC_AT(0x00121f90)
void AStream::FadeOut() {
    AStreamPriv *p = priv;
    SNDSYS_entercritical();
    SNDSTRM_autovol(p->stream, kFadeOutMs, 0);
    SNDSYS_leavecritical();
}

// FUNC_AT(0x00121fc0)
double AStream::GetLatency() {
    if (priv->loop || priv->requestId == -1)
        return 0.0;
    AStreamPriv *p = priv;
    double latency = 1.0 / TIMER_getfrequency() * (p->playTick - p->eventTick);
    if (p->request.state == 2)
        latency -= int32_t(p->request.playedMs) * double(kMsToSeconds);
    return latency;
}

// FUNC_AT(0x00122020)
float AStream::GetInternalVolume() {
    return priv->volume;
}

// FUNC_AT(0x00122030)
const char* AStream::GetFile() {
    if (priv->file == NULL)
        return GameEmptyString;
    return priv->name;
}

// FUNC_AT(0x00122050)
void AStream::Next() {
    AStreamPriv *p = priv;
    if (p->stream > -1) {
        if (StreamHold == kHoldWaiting) {
            SNDSTRM_modifyhold(p->requestId, 0);
            StreamHold = kHoldNone;
        }
        SNDSTRM_purge(p->stream);
    }
    p->requestId = -1;
    if (p->file != NULL) {
        ::OperatorDelete(p->file);
        p->file = NULL;
    }
    if (priv->queue.Empty())
        priv->over = 1;
}

// FUNC_AT(0x00122430)
void AStream::Stop() {
    priv->queue.Clear();
    Next();
}

// The error is not used (the original adds 19 to it and drops it).
// FUNC_AT(0x001224d0)
void AStream::DecodeError(int) {
    priv->queue.Clear();
    Next();
}

// FUNC_AT(0x00122580)
void AStream::Play(ASoundPlayParams *params) {
    AStreamPriv *p = priv;
    if (p->stream == -1)
        return;
    p->playTick = TIMER_gettick();
    if (ASoundManager_fgMissionOver && CRT_stricmp(mix->name, "music") == 0)
        return;

    p->volume = float(mix->GetVolume() * volume * params->volume);

    // The next event's fade: the playing request fades out over its time, then ends.
    if (p->requestId > -1 && !p->queue.Empty()) {
        const AStreamEntry &next = *p->queue.Front();
        if (next.fadeTime >= 0.0f) {
            double fade = FramesSinceAudioUpdate * double(kFrameSeconds) + p->fade;   // compared unrounded
            p->fade = float(fade);
            if (fade >= next.fadeTime) {
                if (p->stream > -1)
                    SNDSTRM_purge(p->stream);
                p->requestId = -1;
                if (p->file != NULL) {
                    ::OperatorDelete(p->file);
                    p->file = NULL;
                }
            } else {
                p->volume = float((1.0 - fade / next.fadeTime) * p->volume);
            }
        }
    }

    if (priv->file != NULL && p->volume > 0.0f) {
        mix->unknown24++;
        mix->unknown28 = float(double(p->volume) + p->volume + mix->unknown28);
    }
    p->volume = mix->unknown2c * p->volume;

    int vol;
    int pitch;
    if (p->volume < 0.0f) {
        vol = 0;
        pitch = 0;
    } else {
        vol = p->volume > 1.0f ? 127 : RoundToInt(p->volume * 127.0f);
        if (vol == 0) {
            pitch = 0;
        } else if (params->pitch < 0.0f) {
            pitch = 0;
        } else {
            pitch = params->pitch > 4.0f ? 0x4000 : RoundToInt(params->pitch * 4096.0f);
        }
    }
    priv->paused = pitch == 0;
    int azimuth = RoundToInt(params->azimuth * 65536.0f) & 0xffff;
    if (pitch == 0)
        vol = 0;
    float filter = fabsf(priv->filter);
    int cutoff = filter >= 1.0f ? 0xffff : RoundToInt(filter * 24000.0f);

    SNDSYS_entercritical();
    SNDSTRM_vol(p->stream, vol);
    SNDSTRM_pitchmult(p->stream, pitch);
    SNDSTRM_lowpass(p->stream, cutoff);
    if (maxDistance > 0.0f)
        SNDSTRM_3dpos(p->stream, azimuth, 0);
    SNDSTRM_status(p->stream, &p->status);
    if (p->requestId > -1)
        SNDSTRM_requeststatus(p->requestId, &p->request);
    SNDSYS_leavecritical();

    if (p->requestId > -1) {
        // A request is playing.
        int requests = p->status.requests;
        if (requests == 0) {
            if (p->file != NULL) {
                ::OperatorDelete(p->file);
                p->file = NULL;
                p->requestId = -1;
            }
            return;
        }
        if (p->held == 1) {
            switch (StreamHold) {
            case kHoldWaiting:
                heldTicks = TIMER_gettick() - StreamHoldTick;
                if (heldTicks > holdTimeout) {
                    Next();
                    releaseTick = TIMER_gettick();
                }
                break;
            case kHoldReleased:
                SNDSTRM_modifyhold(p->requestId, 0);
                p->eventTick = p->playTick;
                releaseTick = TIMER_gettick();
                StreamHold = kHoldNone;
                break;
            }
            return;
        }
        if (p->loop && requests < 2)
            p->requestId = SNDSTRM_queuefile(p->stream, kStreamHoldMs, p->file, 0);
        return;
    }

    // Nothing playing: the next event's file.
    if (p->file != NULL)
        return;
    if (p->queue.Empty()) {
        p->over = 1;
        return;
    }
    const AStreamEntry &next = *p->queue.Front();
    strcpy(p->name, next.name);
    if (p->name[0] >= 'a' && p->name[0] <= 'z')
        p->name[0] -= 'a' - 'A';
    char *fileName = static_cast<char *>(::OperatorNew(0x40));
    if (fileName != NULL)
        BuildFileName(fileName, 0, p->filePrefix, next.name, "asf");
    p->file = fileName;
    p->eventTick = next.eventTick;
    p->loop = next.loop;
    p->effect = next.effect;
    p->fade = 0.0f;
    p->held = next.held;
    if (next.held == 1) {
        p->requestId = SNDSTRM_queuefile(p->stream, -1, fileName, 0);
        StreamHold = kHoldWaiting;
        StreamHoldTick = TIMER_gettick();
        releaseTick = 0;
    } else {
        p->requestId = SNDSTRM_queuefile(p->stream, kStreamHoldMs, fileName, 0);
        releaseTick = 0;
    }
    if (p->requestId < 0)
        DecodeError(p->requestId);
    p->queue.PopFront();
    if (!p->loop && !p->queue.Empty()) {
        ::OperatorDelete(p->file);
        p->file = NULL;
        p->requestId = -1;
    }
}

// FUNC_AT(0x001234d0)
void AStream::Event(const char *name, float fadeTime, bool loop, bool effect, bool held) {
    CRT_printf("event %s, %.1f, %s\n", name, double(fadeTime), loop ? "loop" : "one-shot");
    AStreamEntry entry = {};   // the original leaves its stack in the name's tail and in unknown3f
    priv->queue.PushBack(*entry.Construct(name, fadeTime, loop, effect, held, priv->playTick));
    priv->over = 0;
    priv->notFound = 0;
}

// FUNC_AT(0x001236c0)
AStream* AStream::Create(const char *name, const char *file, unsigned int bufferSize) {
    StreamRefCounter::Get()->GetReference(name);   // the answer is not used
    char extension[8];
    if (CRT_stricmp(name, "music") == 0)
        strcpy(extension, MusicExtension);
    else if (CRT_stricmp(name, "speech") == 0 || CRT_stricmp(name, "nis") == 0)
        strcpy(extension, SpeechExtension);
    else
        strcpy(extension, "viv");

    AStream *memory = static_cast<AStream *>(ABaseSound::OperatorNew(sizeof(AStream), "AStream"));
    AStream *stream = memory != NULL ? memory->Construct(name, file, extension, bufferSize) : NULL;
    stream->maxDistance = 0.0f;
    stream->maxDistanceSq = 0.0f;
    StreamRefCounter::Get()->AddReference(name, stream);
    return stream;
}

// FUNC_AT(0x001237d0)
AStream* AStream::Get(const char *name) {
    return StreamRefCounter::Get()->GetReference(name);
}

// FUNC_AT(0x001237f0)
void AStream::Remove(const char *name) {
    AStream *stream = StreamRefCounter::Get()->GetReference(name);
    StreamRefCounter::Get()->RemoveReference(stream);
    if (stream != NULL)
        (stream->*XbeVirtual<decltype(&AStream::Delete)>(stream, 0))(1);
}

// =============================================================================================================
// URefCounter<AStream>'s tree
// =============================================================================================================

// FUNC_AT(0x00122500)
void StreamRefTree::EraseSubtree(RefCounterNode *node) {
    RefCounterTree::EraseSubtree(node);
}

// FUNC_AT(0x00122b10)
RefCounterNode** StreamRefTree::EraseAt(RefCounterNode **result, RefCounterNode *where) {
    return RefCounterTree::EraseAt(result, where);
}

// FUNC_AT(0x00122e80)
RefCounterNode** StreamRefTree::InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where,
                                     const RefCounterValue *value) {
    return RefCounterTree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x00123070)
RefCounterNode** StreamRefTree::EraseRange(RefCounterNode **result, RefCounterNode *first, RefCounterNode *last) {
    return RefCounterTree::EraseRange(result, first, last);
}

// FUNC_AT(0x001232a0)
RefCounterInsertResult* StreamRefTree::InsertUnique(RefCounterInsertResult *result, const RefCounterValue *value) {
    return RefCounterTree::InsertUnique(result, value);
}

// FUNC_AT(0x00123560)
void StreamRefTree::DestroyRange() {
    STREAM_UNTESTED("std::map<AStream>'s destructor (an exception's unwinding)");
    RefCounterTree::DestroyRange();
}

// FUNC_AT(0x001235a0)
void StreamRefTree::Destruct() {
    RefCounterTree::Destroy();
}

// =============================================================================================================
// The maps with 0x20-byte nodes
// =============================================================================================================

// FUNC_AT(0x00123850)
AVoiceMapNode* AVoiceMapMax(AVoiceMapNode *node) {
    while (!node->right->isNil)
        node = node->right;
    return node;
}
