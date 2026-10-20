#ifndef SFX_H_
#define SFX_H_

#include "../actionhelpers.h"
#include "../../common/xbeClass.h"
#include "psiSound.h"

// Eurocom's sound layer (0x000c4b70-0x000cbdf0): the game's sounds by number, each an SFXItem playing up to ten
// voices from the loaded banks, and the streams (one, the music or a streamed sound) with their markers. SFX.cpp
// has the sounds and banks, SFXStream.cpp the streams. Both sit on the psi layer (psiSound.cpp).

#pragma pack(push, 1)

// A bank's sound: its number, and where its SFXParameters are in the bank's .SFX file
typedef struct SFXHeader {
    int HashCode;
    int Offset;
} SFXHeader;

// One sample a sound can play, with its random spreads (1024 = the whole offset either way)
typedef struct SFXSamplePoolFiles {
    int FileRef;                // the bank's sample; below 0, a stream
    int PitchOffset;            // 0x3000 = an octave
    int RandomPitchOffset;
    int BaseVolume;
    int RandomVolumeOffset;
    int Pan;
    int RandomPan;
} SFXSamplePoolFiles;

// A sound's settings, as the bank's .SFX file has them. SampleCount SFXSamplePoolFiles follow.
typedef struct SFXParameters {
    int ReverbSend;
    int TrackingType;           // SFXTrackingType
    int InnerRadiusReal;
    int OuterRadiusReal;
    int MaxVoices;              // 0x10 - of this sound at once; 0 for any number
    int Priority;
    int Group;
    short MaxReject;            // 0x1c - a sound over its limits does not start, rather than stopping another
    short Action2;
    short IgnoreAge;
    short Ducker;               // 0x22 - how far it ducks the music
    int DuckerLength;
    int MasterVolume;           // 0x28
    int MinDelay;               // 0x2c - frames between samples
    int MaxDelay;
    short MultiSample;          // 0x34 - plays its samples one after another (or together if Polyphonic)
    short RandomPick;
    short Shuffled;
    short Loop;
    short Polyphonic;
    char Outdoors;              // 0x3e - takes the indoors volume
    char PauseInNis;            // 0x3f
    int SampleCount;            // 0x40
} SFXParameters;

typedef struct psiSFXSpecials {
    int Volume3D;
    int Pan1;
    int Pan2;
    int Pan3;
} psiSFXSpecials;

#define SFX_MAX_VOICES 10
#define SFX_MAX_CUED 50

// A sound playing: in BaseSFXActive's list, or free in SFXFreeList's
typedef struct SFXItem {
    struct SFXItem *Next;
    struct SFXItem *Last;
    int HashCode;               // 0x08 - the sound's number
    int Counter;                // 0x0c - frames since it started
    int StreamHandle;
    int VoiceHandle[SFX_MAX_VOICES];    // 0x14 - ES_VoiceHandleData indices, -1 for none
    int BaseVolume[SFX_MAX_VOICES];     // 0x3c
    int Volume3D;
    int Pitch[SFX_MAX_VOICES];          // 0x68
    int Pan[SFX_MAX_VOICES];            // 0x90
    int CuePos;                         // 0xb8 - cued samples left
    int CueSampleWaiting[SFX_MAX_CUED]; // 0xbc - frames before each
    SFXSamplePoolFiles *CuedSamples[SFX_MAX_CUED];  // 0x184
    SFXParameters *SFXData;     // 0x24c
    int SoundBankSlot;          // 0x250
    int UpdatedCounter;         // 0x254 - times SFXUpdate has updated it this frame
    _VECTOR RealPos;            // 0x258 - where it was put
    _VECTOR pos;                // 0x264 - where it plays from (ES_GetMyPos)
    _VECTOR vel;                // 0x270
    DYNAMICSOUNDS *Handle;      // 0x27c - the caller's
    int VolumeOverRide;         // 0x280
    int OuterRadius;
    int InnerRadius;
    int DeleteMe;               // 0x28c
    int Paused;                 // 0x290
    int StartedWhenPaused;
    int StartedWhenNisPlaying;  // 0x298
    int FadeAndKill;            // 0x29c
    int FadeAndKillVolume;      // 0x2a0
    psiSFXSpecials Specials;    // 0x2a4
    int KeyOnFrameDelay;        // 0x2b4 - frames before it is keyed on; it is dropped if it is not by then
    int FirstTransferComplete;
    int IsStreamed;             // 0x2bc
} SFXItem;

// A loaded bank: SB_<n>.SFX (the sounds), .SHF (the sample headers) and .SBF (the samples)
typedef struct SoundBankHeader {
    int *SampleHeaderBuffer;    // the .SHF file: a count, then the headers
    int SampleCount;
    SampleHeaderData *SampleHeader;
    int *SFXBuffer;             // 0x0c - the .SFX file: a count, then the SFXHeaders; the sounds after them
    int SFXCount;               // 0x10
    SFXHeader *SFXHeaderData;
    int Loaded;                 // 0x18
    int HashCode;               // 0x1c - its SBInfoList number
    char *psi_SampleHeaderBuffer;   // 0x20
} SoundBankHeader;

// A voice the sounds hold: a psi voice, or a stream
typedef struct ES_VoiceHandleDetails {
    int IsStream;
    int Handle;                 // the psi voice, or the Streams index
    int Status;                 // 0 free
} ES_VoiceHandleDetails;

// A sound asked for this frame or the last few, so SFXIsSFXPlaying says yes before it is in the list
typedef struct SFXRequest {
    int HashCode;
    DYNAMICSOUNDS *Handle;
    int Life;                   // frames left
} SFXRequest;

// SFXUpdate's copy of the list at the end of the frame
typedef struct SFXActive {
    int HashCode;
    DYNAMICSOUNDS *Handle;
} SFXActive;

// ---- the streams

// A position in the playback buffer the stream has to pass
typedef struct TestMarker {
    int CheckMe;
    int EndMustToggle;          // the play position must wrap first
    int EndHere;
} TestMarker;

// A marker in a music track's .SMF file
typedef struct MusicMarkerData {
    int Name;                   // the section it belongs to
    int Pos;                    // in the .SSD file
    int MType;                  // MusicMarkerType
    int Flags;
    int Extra;
    int LoopStart;
    int MarkerCount;
    int LoopMarkerCount;
} MusicMarkerData;

// A section's start
typedef struct MusicMarkerStartData {
    MusicMarkerData Marker;
    int MarkerPos;              // 0x20 - its first marker
    int IsInstant;              // 0x24 - a jump to it need not wait for a marker
    int InstantBuffer;
    int State[2];               // 0x2c - the ADPCM decoder's state there
} MusicMarkerStartData;

typedef struct StreamMarkerHeaderData {
    int StartMarkerCount;
    int MarkerCount;
    int StartMarkerOffset;      // from the header
    int MarkerOffset;
    int BaseVolume;
} StreamMarkerHeaderData;

// The streamed sounds' places in STREAMS.BIN
typedef struct StreamLookupFileDetails {
    int MarkerOffset;
    int MarkerSize;
    int AudioOffset;
    int AudioSize;
} StreamLookupFileDetails;

// A read into one of the stream's file buffers
typedef struct StreamReadData {
    char *DataBuffer;
    int FilePos;
    int ByteOffset;             // 0x08 - where it goes in the playback buffer
    int ByteSize;
    int SetToBlank;             // 0x10 - silence rather than the file
    int NoDataToRead;
    int State[2];               // 0x18
    int GetState[2];            // 0x20
    int StateSet;               // 0x28
    int UseThisState;
    TestMarker EndMarker;       // 0x30
    TestMarker CanJumpMarker;   // 0x3c
    TestMarker JumpRequestCompleteMarker;   // 0x48
    int MarkerPassed;           // 0x54
    int MarkerPassedByteSize;
} StreamReadData;

#define STREAM_READ_BUFFERS 8

typedef struct StreamData {
    int IdNumber;
    int HashCode;               // the track; below 0, -1 - the streamed sound's number
    int Status;                 // 0x08 - StreamStatus
    int FileHandle;
    int StreamHandle;           // 0x10 - the psi voice
    int StreamHandle2;
    int Channels;
    int BufferToggle;
    int PauseNow;               // 0x20
    int Paused;
    int GamePausedMe;           // 0x28
    int BaseVolume;
    int Test1;
    int MusicStream;            // 0x34
    int SoftwareAdpcm;
    int Frequency;
    int FinishedInitialise;     // 0x40
    int FileOffset;
    int StartedPlaying;         // 0x48
    int AllowStartedPlaying;
    int KeyOn;                  // 0x50
    int ReverbDepth;
    int Is3d;
    int BeenStolen;             // 0x5c
    int BeenStolenByInitialiseRef;
    int SetJumpRequestCompleteTimer;    // 0x64
    int SetEndMarker;
    int SetCanJumpMarker;
    int SetJumpRequestCompleteMarker;   // 0x70
    TestMarker EndMarker;       // 0x74
    TestMarker CanJumpMarker;   // 0x80
    TestMarker JumpRequestCompleteMarker;   // 0x8c
    int EndingSoon;             // 0x98
    TestMarker DataEndMarker;   // 0x9c
    int FilePos;                // 0xa8
    int LastFilePos;
    int TimedOut;               // 0xb0
    char *DataBuffer2;
    int BufferSize;
    int PlayBackBufferSize;
    int PlayBackPos;            // 0xc0
    int LastPlayBackPos;
    StreamReadData Read[STREAM_READ_BUFFERS];   // 0xc8
    int FileBufferCount;        // 0x3a8
    int LastFileBuffersFull;
    int FileBuffersFull;
    int ReadBufferNumber;       // 0x3b4
    int TransferBufferNumber;   // 0x3b8
    int State[2];               // 0x3bc
    int ShutDown;               // 0x3c4
    int ShutDownVolume;
    int ShutDownCounter;
    StreamMarkerHeaderData *StreamMarkerHeader; // 0x3d0
    MusicMarkerData *MusicMarker;
    MusicMarkerData *JumpMusicMarker;
    MusicMarkerStartData *MusicMarkerStart;     // 0x3dc
    MusicMarkerData *BaseMusicMarker;
    MusicMarkerStartData *BaseMusicMarkerStart; // 0x3e4
    int StreamJumpTo;           // 0x3e8 - the section to jump to, -1 for none
    int HasJustJumped;
    int HasJustRequestedJump;   // 0x3f0
    int HasJustLoadedJump;
    int HasJustRequestedLoadedJump;
} StreamData;

// A copy into the stream's playback buffer, a piece a frame
typedef struct StreamMemCpyHandle {
    int StreamHandle;
    int StreamByteOffset;
    char *SrcData;
    int SrcByteSize;
    int TransferPos;
    int TransferedData;
    int TransferStatus;         // 0x18 - STREAM_COPYING while it runs
    StreamData *Stream;
} StreamMemCpyHandle;

// A stream asked for, waiting for SFXInitialiseStreamUpdate
typedef struct StreamIntialiseDetails {
    int HashCode;
    int ChannelNumber;
    int Is3d;
    int Status;
    int KeyOn;
    int ReverbDepth;
} StreamIntialiseDetails;

#pragma pack(pop)

static_assert(sizeof(SFXParameters) == 0x44, "SFXParameters is 0x44 bytes");
static_assert(sizeof(SFXSamplePoolFiles) == 0x1c, "SFXSamplePoolFiles is 0x1c bytes");
static_assert(offsetof(SFXParameters, PauseInNis) == 0x3f, "Bad offset of SFXParameters.PauseInNis");
LAYOUT_32(sizeof(SFXItem) == 0x2c0, "SFXItem is 0x2c0 bytes");
LAYOUT_32(offsetof(SFXItem, SFXData) == 0x24c, "Bad offset of SFXItem.SFXData");
LAYOUT_32(offsetof(SFXItem, Handle) == 0x27c, "Bad offset of SFXItem.Handle");
LAYOUT_32(offsetof(SFXItem, KeyOnFrameDelay) == 0x2b4, "Bad offset of SFXItem.KeyOnFrameDelay");
LAYOUT_32(sizeof(SoundBankHeader) == 0x24, "SoundBankHeader is 0x24 bytes");
static_assert(sizeof(MusicMarkerStartData) == 0x34, "MusicMarkerStartData is 0x34 bytes");
LAYOUT_32(sizeof(StreamReadData) == 0x5c, "StreamReadData is 0x5c bytes");
LAYOUT_32(sizeof(StreamData) == 0x3fc, "StreamData is 0x3fc bytes");
LAYOUT_32(offsetof(StreamData, Read) == 0xc8, "Bad offset of StreamData.Read");
LAYOUT_32(offsetof(StreamData, FileBufferCount) == 0x3a8, "Bad offset of StreamData.FileBufferCount");
LAYOUT_32(offsetof(StreamData, StreamJumpTo) == 0x3e8, "Bad offset of StreamData.StreamJumpTo");
LAYOUT_32(sizeof(StreamMemCpyHandle) == 0x20, "StreamMemCpyHandle is 0x20 bytes");

// SFXParameters.TrackingType
enum SFXTrackingType {
    SFX_TRACK_2D = 0,
    SFX_TRACK_NEAREST = 1,      // placed from the nearest listener, in front of the first
    SFX_TRACK_3D = 2,
    SFX_TRACK_LISTENER = 3,     // just in front of the listener
};

// StreamData.Status
enum StreamStatus {
    STREAM_FREE = 0,
    STREAM_WAITING = 1,         // asked for, not yet initialised
    STREAM_READY = 2,
    STREAM_OPENING = 3,
    STREAM_READING = 4,
    STREAM_JUMPING = 5,
    STREAM_JUMP_COPYING = 9,
    STREAM_STOPPED = 10,
};

// StreamMemCpyHandle.TransferStatus
#define STREAM_COPYING 7

// MusicMarkerData.MType
enum MusicMarkerType {
    MARKER_PAUSE = 5,
    MARKER_LOOP = 6,
    MARKER_LOOP_SILENT = 7,
    MARKER_END = 9,
};

#define SFX_NUM_ITEMS 100
#define SFX_NUM_BANKS 150
#define SFX_NUM_VOICE_HANDLES 100
#define SFX_NUM_LISTENERS 4
#define SFX_NUM_STREAMS 1
#define SFX_NUM_STREAM_REQUESTS 64
#define SFX_NUM_MUSIC_TRACKS 150

// ---- the layer's state, at its addresses in the game

#define ES_MuteMusic I32_AT(0x00182748)        // 1 plays the music, 0 mutes it
#define ES_MuteSFX I32_AT(0x0018274c)
#define MFXFilePath (*(char(*)[200])0x00182750)
#define MFXFilePath2 (*(char(*)[200])0x00182818)
#define STRFilePath (*(char(*)[200])0x001828e0)
#define STRFilePath2 (*(char(*)[200])0x001829a8)
#define SFXLanguageName (*(char(*)[20])0x00182a70)
#define SFXModeNumber I32_AT(0x00182a84)
#define MusicMasterVolume I32_AT(0x00182a88)
#define DuckerDuration I32_AT(0x00182a8c)
#define DuckerTargetVolume I32_AT(0x00182a90)
#define DuckerCurrentVolume I32_AT(0x00182a94)
#define SFXVolume I32_AT(0x00182a98)
#define FadeVolume I32_AT(0x00182a9c)          // 0 to 10000
#define FadeMusicVolume I32_AT(0x00182aa0)
#define FadeRate I32_AT(0x00182aa4)
#define FadeMusic I32_AT(0x00182aa8)           // the fade takes the music too
#define SFXNewIndoorsVolume I32_AT(0x00182aac)
#define SFXIndoorsVolume I32_AT(0x00182ab0)
#define sfx_randomseed1 U32_AT(0x00182b14)
#define sfx_randomseed2 U32_AT(0x00182b18)
#define SFXListenerCount I32_AT(0x00182b1c)
#define NormVector (*(_VECTOR *)0x00182b20)
#define DirVector (*(_VECTOR *)0x00182b2c)
#define UpVector (*(_VECTOR *)0x00182b38)
#define ListenerNorm (*(_VECTOR *)0x00182b44)
#define ListenerDir (*(_VECTOR *)0x00182b50)
#define ListenerUp (*(_VECTOR *)0x00182b5c)
#define FileBufferMask U32_AT(0x00182b68)      // the stream's file buffers less one
#define SFXJumpMarker I32_AT(0x00182b6c)       // the section the music asks for, -1 for none
#define InitialiseStatus I32_AT(0x00182b74)    // SFXInitialiseStreamUpdate's: STREAM_READY or STREAM_OPENING

#define SoundBankData (*(SoundBankHeader(*)[SFX_NUM_BANKS])0x00279510)
#define ES_VoiceHandleData (*(ES_VoiceHandleDetails(*)[SFX_NUM_VOICE_HANDLES])0x0027aa28)
#define SFXFreeList (*(SFXItem **)0x0027aedc)
#define ListenerPositions (*(_VECTOR(*)[SFX_NUM_LISTENERS])0x0027aee0)
#define MusicTrackInfo (*(int(*)[SFX_NUM_MUSIC_TRACKS])0x00286a90)
#define SFXItems (*(SFXItem(*)[SFX_NUM_ITEMS])0x00286ce8)
#define StreamWaitingIntialise (*(StreamIntialiseDetails(*)[SFX_NUM_STREAM_REQUESTS])0x00298448)
#define SFXKeyOnFrameDelay I32_AT(0x002981e8)  // for the next SFXStart3D
#define SBInfoList (*(int(*)[SFX_NUM_BANKS])0x002981f0)
#define BaseSFXActive (*(SFXItem **)0x00298a4c)
#define StreamMemCpyHandles (*(StreamMemCpyHandle(*)[SFX_NUM_STREAMS])0x00298a50)
#define Streams (*(StreamData(*)[SFX_NUM_STREAMS])0x00298a70)
#define SFXRequestedActiveData (*(SFXRequest(*)[SFX_NUM_ITEMS])0x00298e74)
#define SfxNisMusicTrigger I32_AT(0x00299328)
#define NisPlaying I32_AT(0x0029932c)
#define NisQuitten I32_AT(0x00299330)          // counts down 5 frames from a cut scene ending
#define SFXPaused U32_AT(0x00299378)
#define ZeroPos (*(_VECTOR *)0x0029937c)
#define SFXFilePath (*(char(*)[200])0x00299388)
#define SFXLanguage I32_AT(0x00299450)
#define SFXFileAccessSuspendedToggle I32_AT(0x00299454)
#define AllStreamsPaused I32_AT(0x00299460)
#define SFXStartLoad I32_AT(0x00299458)        // 100 a sound started, decaying (the name is ours)
#define DuckerStartTime I32_AT(0x0029945c)
#define FadeStatus I32_AT(0x00299464)          // -1 fading down, 1 up, 0 not fading
#define SFXNewReverb I32_AT(0x00299468)
#define SFXReverb I32_AT(0x0029946c)
#define SFXUnderWater I32_AT(0x00299470)
#define FadeStartTimer I32_AT(0x00299474)
#define SFXFileAccessSuspended I32_AT(0x00299478)
#define SFXActiveCount I32_AT(0x00299480)
#define SFXActiveData (*(SFXActive(*)[SFX_NUM_ITEMS])0x00299484)
#define SFXLoadedMarker I32_AT(0x002997a4)     // the section the stream has read to (the name is ours)
#define SFXActiveMarker I32_AT(0x002997ac)     // the section playing
#define ListenerPos (*(_VECTOR *)0x002997b4)
#define ListenerVel (*(_VECTOR *)0x002997c0)
#define EnableReleaseAudioStreamSystem I32_AT(0x002997d0)
#define DoInitialStreamBuffer I32_AT(0x002997d4)
#define SFXJumpInstant I32_AT(0x002997d8)
#define MusicStartSection I32_AT(0x002997dc)
#define StreamWaitingIntialiseCount I32_AT(0x002997e8)
#define HaveCalledOnce I32_AT(0x002997ec)      // MFXINFO.MXI is read
#define SFXInitialised I32_AT(0x00299a0c)
#define SFXZeroPoint (*(_VECTOR *)0x00299a10)

// Whether a stream is playing, or able to
inline bool StreamIsActive(const StreamData *stream) {
    return stream->Status != STREAM_FREE && stream->Status != STREAM_WAITING && stream->Status != STREAM_STOPPED &&
           stream->FinishedInitialise;
}

// SFXSetEnvironment's environment for under water
#define SFX_ENVIRONMENT_UNDERWATER (-0x6768)

// ---- the sounds (SFX.cpp)

void SFXRememberRequest(int hashCode, DYNAMICSOUNDS *handle);
void SFXAgeRequests(void);
int SFXIsSFXPlaying(DYNAMICSOUNDS *handle, int hashCode);
void SFXSetEnvironment(int environment, int outdoors);
void SFXUpdateEnvironment(void);
void SFXFadeUpdate(void);
void SFXFadeDown(int music);
void SFXSetVolume(int volume);
int SFXGetVolume(void);
int IsUnderWaterSFX(SFXItem *Sfx);
int ES_CombineVolumes(int MyVolume, SFXItem *Sfx);
void SFXSetPositionAndVelocity(_VECTOR *pos, _VECTOR *vel, DYNAMICSOUNDS *handle, int hashCode);
void SFXSetFilePath(char *path);
void UpdateLanguageName(void);
int GetSFXLanguage(void);
void SFXSetLanguage(int language);
void SFXSetMode(int mode);
int SFXGetMode(void);
void SFXSetupListener(_VECTOR *pos, _VECTOR *vel, _VECTOR *dir, _VECTOR *up, _VECTOR *norm, int ListenerNumber);
void SFXSetNumberOfListeners(int ListenerCount);
int ES_FindFreeVoiceHandle(void);
void ES_UnlockVoiceHandle(int voiceIdx);
int HasSampleEnded(int voiceIdx, SFXItem *Sfx);
int ES_RequestVoiceHandle(SampleHeaderData *sampleHeader, int fileRefOrHashcode, int is3d, int unused);
int ES_SFXIsReady(int voiceIdx);
void ES_PauseSfx(SFXItem *Sfx);
void SFXPause(void);
void ES_UnPauseSfx(SFXItem *Sfx);
void SFXUnPause(void);
void KillSFX(SFXItem *Sfx);
void SFXRemove(DYNAMICSOUNDS *handle, int hashCode);
void ES_FillpsiSFXData(psiSFX *SfxData, SFXItem *Sfx, int WhichVoice);
void ES_UpdateDucker(void);
int SetCueTimer(SFXParameters *SFXData);
int SFXCountVoicesInUse(SFXItem *except);
void ES_GetMyPos(SFXItem *Sfx);
int ES_GetNearestListenerDistance(SFXItem *Sfx);
int StartSample(SFXSamplePoolFiles *sfxNum, int SoundBankSlot, SFXItem *Sfx, int WhichVoice);
int maybeDXSoundPlay(SFXItem *Sfx);
void SFXUpdateEnding(SFXItem *Sfx, SFXItem **next);
uint32_t SFXStart3D(int HashCode, _VECTOR *pos, _VECTOR *vel, DYNAMICSOUNDS *Handle, int VolumeOverRide,
                    int InnerRadius, int OuterRadius);
uint32_t SFXStart(int HashCode, DYNAMICSOUNDS *Handle);
void SFXUpdate(void);
void SFXInitialiseItems(void);
void LoadSoundBank(int index);
void SFXUnloadSoundBank(int index);
void SFXReset(void);
void SFXInitialise(char *path);
void SFXLoadCorrectBank(void);
void SFXSuspendFileAccess(void);
void SFXUnSuspendFileAccess(void);

// ---- the streams (SFXStream.cpp)

int FindMusicTrackNumber(int MusicHashCode);
void ES_MusicSetVolume(void);
void SFXMusicSetVolume(int Volume);
int SFXMusicGetVolume(void);
void ES_StreamKeyOn(int Handle, int ReverbDepth);
void SFXPauseAllStreams(void);
void SFXUnPauseAllStreams(void);
void SFXReleaseStreamSystem(void);
void SFXInitialiseAudioStreamSystem(void);
void ES_InitialStreamBuffer(void);
void ES_SetFileBufferCount(StreamData *stream);
void ES_StreamAsyncReadFile(int fileHandle, int filePos, int byteSize, char *buffer, StreamData *stream);
void ES_GetStreamStates(StreamData *Stream);
void ES_SetStreamStates(StreamData *Stream);
void ES_StartStreamMemCpy(int streamHandle, int byteOffset, char *srcData, int byteSize, StreamData *Stream);
int ES_CheckEndMarker(StreamData *Stream, TestMarker *marker);
void ES_HandleStreamBufferLoaded(StreamData *Stream);
int SFXInitialiseStream(int HashCode, int Is3d);
void SFXStopStream(StreamData *stream);
void SFXStopMusic(void);
void SFXStartMusic(int track, int section);

// The originals still called by address
#define SFXUpdateStreams ((void (__cdecl *)(void))0x000c9030)
#define SFXInitialiseStreamUpdate ((void (__cdecl *)(void))0x000c63c0)

#endif // SFX_H_
