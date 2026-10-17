#ifndef DRIVING_DEVTOOLS_SNDOWNED_H_
#define DRIVING_DEVTOOLS_SNDOWNED_H_

// ---------------------------------------------------------------------------------------------------------------
// The sound library's globals the port owns, beside the original's copies of them at the game's addresses - for
// the shadow tests that build the library's state at the game's addresses (the original's copy), run an original
// there, and compare the bytes it leaves with what the port leaves.
//
//   SndOwned_ToOriginal()   the port's variables into the original's copies: once before a test builds its state
//                           (the original's copies then hold the live state), and after each port run (the bytes
//                           at the game's addresses are then the port's results, for the comparison);
//   SndOwned_ToPort()       the original's copies into the port's variables: before each side's run (the
//                           original's side calls the port's functions of the other modules), and once after the
//                           test has put the live state back at the game's addresses;
//   SndOwned_Take(), SndOwned_CarryWrites()
//                           around the original's run: what those port functions wrote in the port's variables
//                           carried into the original's copies, where the original's results are compared.
//
// Not here: SoundMutex (a live CRITICAL_SECTION each side keeps its own of; the tests fake the lock), and the
// constant tables (ListenerPresets, DefaultReverb), the same on both sides.
// ---------------------------------------------------------------------------------------------------------------

#include "../sound/snd/Banks.h"
#include "../sound/snd/Filters.h"
#include "../sound/snd/Platform.h"
#include "../sound/snd/SndGlobals.h"
#include "../sound/snd/Stream.h"
#include "../sound/snd/Streams.h"
#include "../sound/snd/System.h"
#include "../sound/snd/Voices.h"

#include <stdint.h>
#include <string.h>

struct SndOwnedGlobal {
    uint32_t original;          // the original's copy
    void *port;                 // the port's variable
    uint32_t size;
};

constexpr SndOwnedGlobal kSndOwned[] = {
    // System.cpp
    { 0x00244c28, &CapsResult, sizeof(CapsResult) },
    { 0x00244c2c, &CapsRead, sizeof(CapsRead) },
    { 0x00244c30, &SysTaskAdded, sizeof(SysTaskAdded) },
    { 0x00244cc8, &SndOptions, sizeof(SndOptions) },
    { 0x00244de8, &SndSavedSet, sizeof(SndSavedSet) },
    { 0x00244ed0, &SystemInited, sizeof(SystemInited) },
    { 0x00244ed1, &MasterVolume, sizeof(MasterVolume) },
    { 0x00244ed3, &CriticalNesting, sizeof(CriticalNesting) },
    { 0x00244ed4, &Num100HzClients, sizeof(Num100HzClients) },
    { 0x00244ed5, &NumServerClients, sizeof(NumServerClients) },
    { 0x00244ed6, &NumUserDataClients, sizeof(NumUserDataClients) },
    { 0x00244ed7, &MixQuality, sizeof(MixQuality) },
    { 0x00244ed8, &NumVoices, sizeof(NumVoices) },
    { 0x00244edc, &SndTick, sizeof(SndTick) },
    { 0x00244ee0, ServerClients100Hz, sizeof(ServerClients100Hz) },
    { 0x00244ef8, ServerClients, sizeof(ServerClients) },
    { 0x00244f10, UserDataClients, sizeof(UserDataClients) },
    { 0x00244f20, &RestoreHook20, sizeof(RestoreHook20) },
    { 0x00244f24, &RestoreHook24, sizeof(RestoreHook24) },
    { 0x00244f28, &RestoreHook28, sizeof(RestoreHook28) },
    { 0x00244f2c, &BankExitHook, sizeof(BankExitHook) },
    { 0x00244f30, &RestoreHook30, sizeof(RestoreHook30) },
    { 0x00244f34, &RestoreHook34, sizeof(RestoreHook34) },
    { 0x00244f38, &StreamExitHook, sizeof(StreamExitHook) },
    { 0x00244f3c, &VoiceArray, sizeof(VoiceArray) },
    { 0x00244f40, &BankArray, sizeof(BankArray) },
    { 0x00244f44, FxBuses, sizeof(FxBuses) },
    { 0x00244f70, ChannelAzimuth, sizeof(ChannelAzimuth) },
    { 0x00244f6c, &SndHeap, sizeof(SndHeap) },
    { 0x00245978, RandomState, sizeof(RandomState) },
    { 0x001d9d48, SndAuthor, sizeof(SndAuthor) },
    // Platform.cpp
    { 0x00244c38, &NextDeadline, sizeof(NextDeadline) },
    { 0x00244c3c, &IsRunning, sizeof(IsRunning) },
    { 0x00244c3d, &KeepRunning, sizeof(KeepRunning) },
    { 0x00244c40, &SetFxFunc, sizeof(SetFxFunc) },
    { 0x00244c44, &BufferNodes, sizeof(BufferNodes) },
    { 0x00244c48, BufferLists, sizeof(BufferLists) },
    { 0x00244c78, PoolSizes, sizeof(PoolSizes) },
    { 0x00244c80, &PlatformVoices, sizeof(PlatformVoices) },
    { 0x00244c84, &Device, sizeof(Device) },
    { 0x00244c88, Rings, sizeof(Rings) },
    { 0x00244ca0, RingMemory, sizeof(RingMemory) },
    { 0x00244cb8, &RingFrames, sizeof(RingFrames) },
    { 0x00244cbc, &MixLead, sizeof(MixLead) },
    { 0x00244cc0, &MixPosition, sizeof(MixPosition) },
    { 0x00244fb8, (void *)&PreFrameCallback, sizeof(PreFrameCallback) },
    { 0x00244fbc, (void *)&PostFrameCallback, sizeof(PostFrameCallback) },
    // Streams.cpp, Stream.cpp
    { 0x00244ba8, StreamArray, sizeof(StreamArray) },
    { 0x00244fe0, &PacketCallbackCount, sizeof(PacketCallbackCount) },
    { 0x00244fe4, PacketCallbacks, sizeof(PacketCallbacks) },
    { 0x002452e4, PacketPlayers, sizeof(PacketPlayers) },
    { 0x002475fc, &RequestGeneration, sizeof(RequestGeneration) },
    // Voices.cpp, Banks.cpp, Filters.cpp
    { 0x00245364, AllocScratch, sizeof(AllocScratch) },
    { 0x00245370, &HandleGeneration, sizeof(HandleGeneration) },
    { 0x00245374, &NextKey, sizeof(NextKey) },
    { 0x00245378, SpeakerGains, sizeof(SpeakerGains) },
    { 0x001da4e0, &KernelScratch, sizeof(KernelScratch) },
};

inline void SndOwned_ToOriginal() {
    for (const SndOwnedGlobal &g : kSndOwned)
        memcpy((void *)(uintptr_t)g.original, g.port, g.size);
}

inline void SndOwned_ToPort() {
    for (const SndOwnedGlobal &g : kSndOwned)
        memcpy(g.port, (const void *)(uintptr_t)g.original, g.size);
}

constexpr uint32_t SndOwnedBytes() {
    uint32_t bytes = 0;
    for (const SndOwnedGlobal &g : kSndOwned)
        bytes += g.size;
    return bytes;
}

struct SndOwnedCopy {
    uint8_t bytes[SndOwnedBytes()];
};

// The port's variables as they are
inline void SndOwned_Take(SndOwnedCopy *copy) {
    uint8_t *at = copy->bytes;
    for (const SndOwnedGlobal &g : kSndOwned) {
        memcpy(at, g.port, g.size);
        at += g.size;
    }
}

// Each of the port's variables that changed since `before`, into the original's copy
inline void SndOwned_CarryWrites(const SndOwnedCopy &before) {
    const uint8_t *at = before.bytes;
    for (const SndOwnedGlobal &g : kSndOwned) {
        if (memcmp(at, g.port, g.size) != 0)
            memcpy((void *)(uintptr_t)g.original, g.port, g.size);
        at += g.size;
    }
}

// The port's byte for a byte of the original's copy (NULL: not an owned global)
inline void *SndOwned_Port(uint32_t original) {
    for (const SndOwnedGlobal &g : kSndOwned)
        if (original - g.original < g.size)
            return static_cast<uint8_t *>(g.port) + (original - g.original);
    return NULL;
}

// A global's byte as one side has it: the original's copy, or the port's variable
inline void *SndOwned_At(uint32_t original, bool port) {
    return port ? SndOwned_Port(original) : (void *)(uintptr_t)original;
}

#endif  // DRIVING_DEVTOOLS_SNDOWNED_H_
