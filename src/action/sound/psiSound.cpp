#include "psiSound.h"
#include "dsndSeam.h"
#include "../memory.h"
#include "../game.h"   // timestamp

#include <string.h>
#include "../engine/FS.h"

// ---------------------------------------------------------------------------------------------------------------
// Eurocom's psiSFX layer (0x000e09c0-0x000e0f00), the platform half of the game's sound code: SFX*, ES_* and the
// stream code call it, and it drives the 64 voices of dsndSeam.cpp. Four of its functions are the psi layer's own
// copies of dsnd* entry points, compiled twice (dsndMarkInactive, psiSampleUnPause, dsndBufferSetVolume,
// psiStreamGetPlayPos, and dsndWriteVoiceData at 0x000e0dd0): those forward to the one implementation.
//
// Coordinates arrive in the game's handedness and go to DirectSound's with z negated.
// ---------------------------------------------------------------------------------------------------------------

// The reverb send. psiSetReverb sets the level (half the environment's depth); each voice started with reverb on
// gets it, and psiSetReverb re-sends it to those voices when the environment changes.
// XBE_GLOBAL(0x002ae550, 0x4)
static int ReverbLevel;
// XBE_GLOBAL(0x002ae558, 0x40)
static uint8_t VoiceHasReverb[64];

// A voice that failed to allocate is 0xffffffff, and the original stored its flag one byte before the table, in
// padding nothing reads (0x002ae557). Here that byte would belong to something else, so such voices are skipped;
// the dsnd* calls ignore them anyway.
static void SetVoiceReverb(uint32_t voice, int on) {
    if (voice < 64)
        VoiceHasReverb[voice] = on != 0;
    dsndSetI3DL2Source(voice, (float)(on ? ReverbLevel : 0));
}

// The original goes through FS_AllocateAndLoadBlocking; the file layer comes later

// AUTOINJECT
uint32_t psiSFXStop(uint32_t voice) {
    dsndSamplePause(voice, 0);
    return 0;
}

// AUTOINJECT
void psiSFXFreeFileMem(void *data) {
    Mem_Free(&data);
}

// Nothing to set up: SFXInitialise checks for success
// FUNC_AT(000e09f0)
int psiSFXInit(void) {
    return 1;
}

// AUTOINJECT
uint32_t psiRequestVoiceHandle(SampleHeaderData *sample, int is3d) {
    bool loops = (sample->Flags & 1) != 0;
    uint32_t voice = dsndGetVoice(sample->Address, sample->Size, sample->Frequency, sample->NumberOfChannels, loops,
                                  is3d != 0);
    dsndSetLoopRegion(voice, loops ? sample->LoopOffset : 0);
    dsndMarkActive(voice);
    return voice;
}

// FUNC_AT(000e0a60)
void psiSFX_MarkInactive(uint32_t voice) {
    dsndMarkInactive(voice);
}

// FUNC_AT(000e0a70)
void psiSFX_UnPause(uint32_t voice) {
    psiSampleUnPause(voice);
}

// The time in milliseconds, truncated
// AUTOINJECT
int64_t psiSFXGetTimer(void) {
    return (int64_t)timestamp();
}

// AUTOINJECT
void* psiSFXLoadFile(char *fileName) {
    return FS_AllocateAndLoadBlocking(fileName, 0x1204, NULL);
}

// AUTOINJECT
void psiSetReverb(int depth) {
    ReverbLevel = depth / 2;
    for (uint32_t voice = 0; voice < 64; voice++)
        dsndSetI3DL2Source(voice, (float)(VoiceHasReverb[voice] ? ReverbLevel : 0));
}

// Starts a voice, with the reverb send on or off
// AUTOINJECT
int psiSampleKeyOn(uint32_t voice, int reverb) {
    SetVoiceReverb(voice, reverb);
    psiSampleUnPause(voice);
    return 1;
}

// AUTOINJECT
void psiSFXSetupListener(_VECTOR *pos, _VECTOR *vel, _VECTOR *dir, _VECTOR *up, _VECTOR *norm) {
    (void)vel; (void)norm;
    SetListenerPosition(pos->x, pos->y, -pos->z);
    SetListenerOrientation(dir->x, dir->y, -dir->z, up->x, up->y, -up->z);
    SetListenerVelocity();
}

// AUTOINJECT
bool psiIsSamplePlaying(uint32_t voice) {
    return dsndIsPlaying(voice);
}

// A stream's read buffer: 2 KB of slack either side, as the reads are rounded out to whole 2 KB sectors
// AUTOINJECT
int psiAsyncCreateBuffer(int size) {
    return (int)(uintptr_t)allocateXboxSpecialMemory(size + 0x1000, 4) + 0x800;
}

// A stream's playback buffer: a looping 2D voice over a buffer of its own, at full volume
// AUTOINJECT
int psiStreamCreatePlaybackBuffer(int frequency, int channels, int byteSize) {
    void *data = allocateXboxSpecialMemory(byteSize, 4);
    if (data != NULL) {
        uint32_t voice = dsndGetVoice(data, byteSize, frequency, channels, true, 0);
        if (voice != 0xffffffffu) {
            dsndMarkActive(voice);
            dsndBufferSetVolume(voice, 100);
            return (int)voice;
        }
        FreeMemory(data);
    }
    return -1;
}

// AUTOINJECT
void dsndDeleteChannel(uint32_t voice) {
    dsndSamplePause(voice, 0);
    FreeMemory(dsndGetData(voice));
    dsndMarkInactive(voice);
}

// AUTOINJECT
void maybePsiStreamPlay(uint32_t stream, int reverbDepth) {
    SetVoiceReverb(stream, reverbDepth);
    psiSampleUnPause(stream);
}

// FUNC_AT(000e0da0)
void psiStreamStop(uint32_t stream) {
    dsndSamplePause(stream, 0);
}

// AUTOINJECT
void psiSamplePause(uint32_t voice) {
    dsndSamplePause(voice, 1);
}

// FUNC_AT(000e0dc0)
void psiSFX_SetVolume(uint32_t voice, int volume) {
    dsndBufferSetVolume(voice, volume);
}

// FUNC_AT(000e0dd0)
void psiStreamWrite(uint32_t voice, int offset, const void *src, uint32_t length) {
    dsndWriteVoiceData(voice, offset, src, length);
}

// FUNC_AT(000e0de0)
uint32_t psiSFX_GetPlayPos(uint32_t voice) {
    return psiStreamGetPlayPos(voice);
}

// Silence for a stream buffer
// FUNC_AT(000e0df0)
void psiStreamClear(uint32_t size, void *dest) {
    memset(dest, 0, size);
}

// AUTOINJECT
void psiUpdateSound(psiSFX *sfx) {
    if (sfx->Is3d != 0) {
        _VECTOR *pos = sfx->pos;
        dsndSetPosition(sfx->Handle, pos->x, pos->y, -pos->z);
        dsndClearVelocity(sfx->Handle);
        dsndSetDistances(sfx->Handle, (float)sfx->InnerRadius, (float)sfx->OuterRadius);
        dsndSetFrequency(sfx->Handle, sfx->Frequency);
        dsndBufferSetVolume(sfx->Handle, sfx->Volume);
        return;
    }
    dsndSetFrequency(sfx->Handle, sfx->Frequency);
    dsndSetPan(sfx->Handle, sfx->Pan, 0);
    dsndBufferSetVolume(sfx->Handle, sfx->Volume);
}

// The debug print the retail build compiled to nothing: game code still calls it, and keeps its address in data
// FUNC_AT(000e0ec0)
void __cdecl psiDebugPrint(const char *format, ...) {
    (void)format;
}

// AUTOINJECT
void psiInitialiseSound(psiSFX *sfx) {
    if (sfx->Is3d != 0)
        maybeXboxSFXCalculate3D(sfx->Handle, sfx->Positioned != 0);
    psiUpdateSound(sfx);
}
