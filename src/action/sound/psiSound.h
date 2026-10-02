#ifndef PSISOUND_H_
#define PSISOUND_H_

#include <stdint.h>
#include "../actionhelpers.h"

// Eurocom's psiSFX layer (0x000e09c0-0x000e0f00): what the game's sound code (SFX*, ES_*) calls, a thin layer
// over the dsnd* voices in dsndSeam.cpp. Streams are voices too: a looping buffer the stream code refills.

// A sample bank entry, as LoadSoundBank leaves it
typedef struct SampleHeaderData {
    int Flags;              // 0x00 bit 0: loops
    void *Address;          // 0x04 the sample data
    int Size;               // 0x08
    int Frequency;          // 0x0c
    int RealSize;           // 0x10
    int NumberOfChannels;   // 0x14
    int BitsPerSample;      // 0x18
    char *psi_SampleHeader; // 0x1c
    int LoopOffset;         // 0x20 where the loop starts, when bit 0 is set
} SampleHeaderData;

// A playing sound's parameters, as the SFX code keeps them
typedef struct psiSFX {
    int Handle;             // 0x00 the voice
    int InnerRadius;        // 0x04
    int OuterRadius;        // 0x08
    int Volume;             // 0x0c 0..100
    int Frequency;          // 0x10
    int Is3d;               // 0x14
    int Positioned;         // 0x18 3D only: placed in the world (0 = follows the listener)
    int IsStreamed;         // 0x1c
    int Pan;                // 0x20 2D only
    _VECTOR *pos;           // 0x24
    _VECTOR *vel;           // 0x28
    void *Specials;         // 0x2c
    int TrackingType;       // 0x30
} psiSFX;

uint32_t psiSFXStop(uint32_t voice);
void psiSFXFreeFileMem(void *data);
int psiSFXInit(void);
uint32_t psiRequestVoiceHandle(SampleHeaderData *sample, int is3d);
void psiSFX_MarkInactive(uint32_t voice);
void psiSFX_UnPause(uint32_t voice);
int64_t psiSFXGetTimer(void);
void *psiSFXLoadFile(char *fileName);
void psiSetReverb(int depth);
int psiSampleKeyOn(uint32_t voice, int reverb);
void psiSFXSetupListener(_VECTOR *pos, _VECTOR *vel, _VECTOR *dir, _VECTOR *up, _VECTOR *norm);
bool psiIsSamplePlaying(uint32_t voice);
int psiAsyncCreateBuffer(int size);
int psiStreamCreatePlaybackBuffer(int frequency, int channels, int byteSize);
void dsndDeleteChannel(uint32_t voice);
void maybePsiStreamPlay(uint32_t stream, int reverbDepth);
void psiStreamStop(uint32_t stream);
void psiSamplePause(uint32_t voice);
void psiSFX_SetVolume(uint32_t voice, int volume);
void psiStreamWrite(uint32_t voice, int offset, const void *src, uint32_t length);
uint32_t psiSFX_GetPlayPos(uint32_t voice);
void psiStreamClear(uint32_t size, void *dest);
void psiUpdateSound(psiSFX *sfx);
void __cdecl psiDebugPrint(const char *format, ...);
void psiInitialiseSound(psiSFX *sfx);

#endif // PSISOUND_H_
