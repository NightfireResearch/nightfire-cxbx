#include "SFX.h"
#include "../assets.h"
#include "../math/X87Ops.h"
#include "music.h"
#include "Sound.h"
#include "../game.h"
#include "../game/mp/multiplayer.h"
#include "../ui/Menu.h"

#include <stdio.h>

// XBE_GLOBAL(0x00182b20, 0xc)
_VECTOR NormVector = {.x = 1.0f};
// XBE_GLOBAL(0x00182b2c, 0xc)
_VECTOR DirVector = {.y = 1.0f};
// XBE_GLOBAL(0x00182b38, 0xc)
_VECTOR UpVector = {.z = 1.0f};
// XBE_GLOBAL(0x00182b44, 0xc)
_VECTOR ListenerNorm = {.x = -1000.0f};
// XBE_GLOBAL(0x00182b50, 0xc)
_VECTOR ListenerDir = {.y = 1000.0f};
// XBE_GLOBAL(0x00182b5c, 0xc)
_VECTOR ListenerUp = {.z = 1000.0f};

// XBE_GLOBAL(0x00279510, 0x1518)
SoundBankHeader SoundBankData[SFX_NUM_BANKS];
// XBE_GLOBAL(0x0027aa28, 0x4b0)
ES_VoiceHandleDetails ES_VoiceHandleData[SFX_NUM_VOICE_HANDLES];
// XBE_GLOBAL(0x0027aee0, 0x30)
_VECTOR ListenerPositions[SFX_NUM_LISTENERS];
// XBE_GLOBAL(0x00286ce8, 0x11300)
SFXItem SFXItems[SFX_NUM_ITEMS];
// XBE_GLOBAL(0x00298448, 0x600)
StreamIntialiseDetails StreamWaitingIntialise[SFX_NUM_STREAM_REQUESTS];
// XBE_GLOBAL(0x00298a50, 0x20)
StreamMemCpyHandle StreamMemCpyHandles[SFX_NUM_STREAMS];
// XBE_GLOBAL(0x00298a70, 0x3fc)
StreamData Streams[SFX_NUM_STREAMS];
// XBE_GLOBAL(0x00298e74, 0x4b0)
SFXRequest SFXRequestedActiveData[SFX_NUM_ITEMS];
// XBE_GLOBAL(0x0029937c, 0xc)
_VECTOR ZeroPos;
// XBE_GLOBAL(0x00299484, 0x320)
SFXActive SFXActiveData[SFX_NUM_ITEMS];
// XBE_GLOBAL(0x002997b4, 0xc)
_VECTOR ListenerPos;
// XBE_GLOBAL(0x002997c0, 0xc)
_VECTOR ListenerVel;
// XBE_GLOBAL(0x00299a10, 0xc)
_VECTOR SFXZeroPoint;

// XBE_GLOBAL(0x00182750, 0xc8)
char MFXFilePath[200] = "music\\";
// XBE_GLOBAL(0x00182818, 0xc8)
char MFXFilePath2[200] = "music\\";
// XBE_GLOBAL(0x001828e0, 0xc8)
char STRFilePath[200] = "streams\\";
// XBE_GLOBAL(0x001829a8, 0xc8)
char STRFilePath2[200] = "streams\\";

// XBE_GLOBAL(0x00182748, 0x4)
int32_t ES_MuteMusic = 1;
// XBE_GLOBAL(0x0018274c, 0x4)
int32_t ES_MuteSFX = 1;
// XBE_GLOBAL(0x00182a70, 0x14)
char SFXLanguageName[20] = "ENGLISH";
// XBE_GLOBAL(0x00182a84, 0x4)
int32_t SFXModeNumber = 1;
// XBE_GLOBAL(0x00182a88, 0x4)
int32_t MusicMasterVolume = 75;
// XBE_GLOBAL(0x00182a8c, 0x4)
int32_t DuckerDuration = 1;
// XBE_GLOBAL(0x00182a90, 0x4)
int32_t DuckerTargetVolume = 100;
// XBE_GLOBAL(0x00182a94, 0x4)
int32_t DuckerCurrentVolume = 100;
// XBE_GLOBAL(0x00182a98, 0x4)
int32_t SFXVolume = 100;
// XBE_GLOBAL(0x00182a9c, 0x4)
int32_t FadeVolume = 10000;
// XBE_GLOBAL(0x00182aa0, 0x4)
int32_t FadeMusicVolume = 10000;
// XBE_GLOBAL(0x00182aa4, 0x4)
int32_t FadeRate = 30;
// XBE_GLOBAL(0x00182aa8, 0x4)
int32_t FadeMusic = 1;
// XBE_GLOBAL(0x00182aac, 0x4)
int32_t SFXNewIndoorsVolume = 100;
// XBE_GLOBAL(0x00182ab0, 0x4)
int32_t SFXIndoorsVolume = 100;
// XBE_GLOBAL(0x00182b14, 0x4)
uint32_t sfx_randomseed1 = 12341234;
// XBE_GLOBAL(0x00182b18, 0x4)
uint32_t sfx_randomseed2 = 13123123;
// XBE_GLOBAL(0x00182b1c, 0x4)
int32_t SFXListenerCount = 1;
// XBE_GLOBAL(0x00182b68, 0x4)
uint32_t FileBufferMask = 7;
// XBE_GLOBAL(0x00182b6c, 0x4)
int32_t SFXJumpMarker = -1;
// XBE_GLOBAL(0x00182b74, 0x4)
int32_t InitialiseStatus = 2;
// XBE_GLOBAL(0x00182b78, 0x4)
int32_t SFXJumpState = -1;

// XBE_GLOBAL(0x0027aedc, 0x4)
SFXItem *SFXFreeList;
// XBE_GLOBAL(0x00286a90, 0x258)
int MusicTrackInfo[SFX_NUM_MUSIC_TRACKS];
// XBE_GLOBAL(0x002981e8, 0x4)
int32_t SFXKeyOnFrameDelay;
// XBE_GLOBAL(0x002981f0, 0x258)
int SBInfoList[SFX_NUM_BANKS];
// XBE_GLOBAL(0x00298a4c, 0x4)
SFXItem *BaseSFXActive;
// XBE_GLOBAL(0x00299328, 0x4)
int32_t SfxNisMusicTrigger;
// XBE_GLOBAL(0x0029932c, 0x4)
int32_t NisPlaying;
// XBE_GLOBAL(0x00299330, 0x4)
int32_t NisQuitten;
// XBE_GLOBAL(0x00299378, 0x4)
uint32_t SFXPaused;
// XBE_GLOBAL(0x00299388, 0xc8)
char SFXFilePath[200];
// XBE_GLOBAL(0x00299450, 0x4)
int32_t SFXLanguage;
// XBE_GLOBAL(0x00299454, 0x4)
int32_t SFXFileAccessSuspendedToggle;
// XBE_GLOBAL(0x00299458, 0x4)
int32_t SFXStartLoad;
// XBE_GLOBAL(0x0029945c, 0x4)
int32_t DuckerStartTime;
// XBE_GLOBAL(0x00299460, 0x4)
int32_t AllStreamsPaused;
// XBE_GLOBAL(0x00299464, 0x4)
int32_t FadeStatus;
// XBE_GLOBAL(0x00299468, 0x4)
int32_t SFXNewReverb;
// XBE_GLOBAL(0x0029946c, 0x4)
int32_t SFXReverb;
// XBE_GLOBAL(0x00299470, 0x4)
int32_t SFXUnderWater;
// XBE_GLOBAL(0x00299474, 0x4)
int32_t FadeStartTimer;
// XBE_GLOBAL(0x00299478, 0x4)
int32_t SFXFileAccessSuspended;
// XBE_GLOBAL(0x00299480, 0x4)
int32_t SFXActiveCount;
// XBE_GLOBAL(0x002997a4, 0x4)
int32_t SFXLoadedMarker;
// XBE_GLOBAL(0x002997ac, 0x4)
int32_t SFXActiveMarker;
// XBE_GLOBAL(0x002997cc, 0x4)
StreamLookupFileDetails *StreamLookupFileData;
// XBE_GLOBAL(0x002997d0, 0x4)
int32_t EnableReleaseAudioStreamSystem;
// XBE_GLOBAL(0x002997d4, 0x4)
int32_t DoInitialStreamBuffer;
// XBE_GLOBAL(0x002997d8, 0x4)
int32_t SFXJumpInstant;
// XBE_GLOBAL(0x002997dc, 0x4)
int32_t MusicStartSection;
// XBE_GLOBAL(0x002997e0, 0x4)
int32_t SFXLastJumpMarker;
// XBE_GLOBAL(0x002997e4, 0x4)
int32_t StreamWaitingIntialiseMe;
// XBE_GLOBAL(0x002997e8, 0x4)
int32_t StreamWaitingIntialiseCount;
// XBE_GLOBAL(0x002997ec, 0x4)
int32_t HaveCalledOnce;
// XBE_GLOBAL(0x002997f0, 0x100)
char MarkerFileName[256];
// XBE_GLOBAL(0x002998f0, 0x100)
char StreamFileName[256];
// XBE_GLOBAL(0x002999f0, 0x4)
int32_t InitialisingHashCode;
// XBE_GLOBAL(0x002999f4, 0x4)
StreamData *InitialisingStream;
// XBE_GLOBAL(0x00299a0c, 0x4)
int32_t SFXInitialised;

// The sounds: each SFXStart3D takes an SFXItem from the free list and starts its samples on voices of the psi layer;
// SFXUpdate runs them once a frame, moving, mixing, cueing the next sample and freeing those that end.

// ---- what SFXIsSFXPlaying knows of before SFXUpdate has run

// Remembers a sound just asked for, for five frames. Not named on PS2 (nightfire-port's remember_request); the name
// is ours.
// AUTOINJECT
void SFXRememberRequest(int hashCode, DYNAMICSOUNDS *handle) {
    if (handle == NULL && hashCode == -1)
        return;
    for (int i = 0; i < SFX_NUM_ITEMS; i++) {
        if (SFXRequestedActiveData[i].Life == 0) {
            SFXRequestedActiveData[i].Life = 5;
            SFXRequestedActiveData[i].HashCode = hashCode;
            SFXRequestedActiveData[i].Handle = handle;
            return;
        }
    }
}

// Not named on PS2; the name is ours.
// AUTOINJECT
void SFXAgeRequests(void) {
    for (int i = 0; i < SFX_NUM_ITEMS; i++) {
        if (SFXRequestedActiveData[i].Life > 0)
            SFXRequestedActiveData[i].Life--;
    }
}

// Whether a sound is playing or was just asked for: a handle of NULL and a number of -1 match any
// AUTOINJECT
int SFXIsSFXPlaying(DYNAMICSOUNDS *handle, int hashCode) {
    for (int i = 0; i < SFX_NUM_ITEMS; i++) {
        SFXRequest *request = &SFXRequestedActiveData[i];
        if (request->Life > 0 && (request->Handle == handle || handle == NULL) &&
            (request->HashCode == hashCode || hashCode == -1))
            return 1;
    }
    if (SFXActiveCount == 0)
        return 0;
    if (handle == NULL && hashCode == -1)
        return 1;
    for (int i = 0; i < SFXActiveCount; i++) {
        if ((SFXActiveData[i].Handle == handle || handle == NULL) &&
            (SFXActiveData[i].HashCode == hashCode || hashCode == -1))
            return 1;
    }
    return 0;
}

// ---- the environment, the fade and the volumes

// The reverb depth (0 to 100) and whether the place is outdoors, from the listener's cel
// AUTOINJECT
void SFXSetEnvironment(int environment, int outdoors) {
    if (environment == SFX_ENVIRONMENT_UNDERWATER) {
        SFXUnderWater = 1;
        environment = 0;
    } else {
        SFXUnderWater = 0;
        if (environment < 0)
            environment = 0;
        else if (environment > 100)
            environment = 100;
    }
    SFXNewReverb = environment;
    SFXNewIndoorsVolume = outdoors ? 50 : 100;
}

// Moves the reverb and the indoors volume towards the environment's, a step a frame
// AUTOINJECT
void SFXUpdateEnvironment(void) {
    int step = SFXNewReverb - SFXReverb;
    if (step > 16)
        step = 16;
    else if (step < -4)
        step = -4;
    SFXReverb += step;
    psiSetReverb(SFXReverb);

    step = SFXNewIndoorsVolume - SFXIndoorsVolume;
    if (step > 4)
        step = 4;
    else if (step < -4)
        step = -4;
    SFXIndoorsVolume += step;
}

// The fade, by the time since it started: down over 2 seconds, up from nothing
// AUTOINJECT
void SFXFadeUpdate(void) {
    if (FadeStatus == 0)
        return;
    int step = 10000 / (FadeRate * FadeStatus);
    if (step == 0)
        step = FadeStatus;
    FadeVolume += step;

    if (FadeStatus == -1)
        FadeVolume = (FadeStartTimer - (int)psiSFXGetTimer() + 2000) * 5;
    else
        FadeVolume = ((int)psiSFXGetTimer() - FadeStartTimer) * 5;

    if (FadeVolume < 0) {
        FadeVolume = 0;
        FadeStatus = 0;
    } else if (FadeVolume > 10000) {
        FadeVolume = 10000;
        FadeStatus = 0;
    }
    if (FadeMusic)
        FadeMusicVolume = FadeVolume;
}

// AUTOINJECT
void SFXFadeDown(int music) {
    if (FadeStatus == -1 || FadeVolume == 0)
        return;
    FadeStatus = -1;
    FadeMusic = music;
    FadeStartTimer = (int)psiSFXGetTimer();
}

// AUTOINJECT
void SFXSetVolume(int volume) {
    SFXVolume = volume;
    if (volume < 0)
        SFXVolume = 0;
    else if (volume > 100)
        SFXVolume = 100;
}

// AUTOINJECT
int SFXGetVolume(void) {
    return SFXVolume;
}

// The sounds heard as well under water as above it
static const Action_SFX UnderWaterSFX[] = {
    SFX_BOND_MOMENT_BM_CAS_BEAM, SFX_BOND_MOMENT_BM_CAS_TRUCK, SFX_BOND_MOMENT_BM_CAS_WALL,
    SFX_BOND_MOMENT_BM_CAS_ZIP, SFX_BOND_MOMENT_BM_STAB3, SFX_BOND_MOMENT_BM_TENSE, SFX_BOND_MOMENT_LONG_01,
    SFX_BOND_MOMENT_LONG_02, SFX_BOND_MOMENT_MEDIUM_01, SFX_BOND_MOMENT_SHORT_01, SFX_CHR_BOND_BREATHING_LOOP,
    SFX_CHR_BOND_DIE, SFX_CHR_BOND_JETPACK_OFF, SFX_CHR_BOND_JETPACK_ON_LOOP, SFX_CHR_BOND_PAIN_01,
    SFX_WEAPON_SHELL_WATER, SFX_IMPACT_DEEPWATER, SFX_FOOTSTEP_SHALWATER, SFX_ENV_WATER_SMALL_STREAM_LOOP,
    SFX_BODY_HIT_DEEPWATER, SFX_BODY_HIT_SHALWATER, SFX_ENV_UNDERWATER_AMBIENCE_LOOP_01, SFX_IMPACT_SHALWATER,
};

// AUTOINJECT
int IsUnderWaterSFX(SFXItem *Sfx) {
    for (int i = 0; i < ARRAY_SIZE(UnderWaterSFX); i++) {
        if (Sfx->HashCode == UnderWaterSFX[i])
            return 1;
    }
    return 0;
}

// A voice's volume (0 to 100) through the mixer: the music's or the sounds' master volume, the indoors volume, the
// water, the fade, the sound's own fade and master volume, and the ducker for those that do not duck themselves.
// The result is squared to 0 to 100.
// AUTOINJECT
int ES_CombineVolumes(int MyVolume, SFXItem *Sfx) {
    int volume;
    if (Sfx->HashCode == SFX_MUSIC_FRONT_END_LOOP_01 || Sfx->HashCode == SFX_MUSIC_FRONT_END_LOOP_02) {
        volume = ES_MuteMusic * MusicMasterVolume * MyVolume / 100;
    } else {
        volume = ES_MuteSFX * SFXVolume * MyVolume / 100 * Sfx->VolumeOverRide / 100;
        if (Sfx->SFXData->Outdoors)
            volume = SFXIndoorsVolume * volume / 100;
        if (SFXUnderWater && !IsUnderWaterSFX(Sfx))
            volume = volume * 50 / 100;
    }
    volume = FadeVolume * volume / 10000 * Sfx->FadeAndKillVolume / 100 * Sfx->SFXData->MasterVolume / 100;
    if (Sfx->SFXData->Ducker == 0)
        volume = (100 - (100 - DuckerCurrentVolume) / 3) * volume / 100;
    return volume * volume / 100;
}

// Moves the sounds a handle (NULL for any) started as a number (-1 for any)
// AUTOINJECT
void SFXSetPositionAndVelocity(_VECTOR *pos, _VECTOR *vel, DYNAMICSOUNDS *handle, int hashCode) {
    for (SFXItem *Sfx = BaseSFXActive; Sfx != NULL; Sfx = Sfx->Next) {
        if ((Sfx->Handle == handle || handle == NULL) && (Sfx->HashCode == hashCode || hashCode == -1)) {
            Sfx->pos = *pos;
            Sfx->RealPos = *pos;
            Sfx->vel = *vel;
        }
    }
}

// ---- the paths and the language

// AUTOINJECT
void SFXSetFilePath(char *path) {
    sprintf(MFXFilePath2, "%s%s", path, "MUSIC\\");
    sprintf(MFXFilePath, "%s%s", path, "MUSIC\\");
    sprintf(STRFilePath2, "%s", path);
    sprintf(STRFilePath, "%s", path);
    sprintf(SFXFilePath, "%s", path);
}

// AUTOINJECT
void UpdateLanguageName(void) {
    switch (SFXLanguage) {
    case 0:
        sprintf(SFXLanguageName, "%s", "ENGLISH");
        break;
    case 1:
        sprintf(SFXLanguageName, "%s", "FRENCH");
        break;
    case 2:
        sprintf(SFXLanguageName, "%s", "GERMAN");
        break;
    case 3:
        sprintf(SFXLanguageName, "%s", "SPANISH");
        break;
    case 4:
        sprintf(SFXLanguageName, "%s", "JAPANESE");
        break;
    }
}

// AUTOINJECT
int GetSFXLanguage(void) {
    return SFXLanguage;
}

// AUTOINJECT
void SFXSetLanguage(int language) {
    SFXLanguage = language;
    UpdateLanguageName();
}

// The psi layer's half of this is an empty function on the Xbox
// AUTOINJECT
void SFXSetMode(int mode) {
    SFXModeNumber = mode;
}

// AUTOINJECT
int SFXGetMode(void) {
    return SFXModeNumber;
}

// ---- the listeners

// With one listener it is the psi layer's too. With more, each listener only places the sounds (ES_GetMyPos), and
// the psi layer's listener stays where SFXSetNumberOfListeners put it.
// AUTOINJECT
void SFXSetupListener(_VECTOR *pos, _VECTOR *vel, _VECTOR *dir, _VECTOR *up, _VECTOR *norm, int ListenerNumber) {
    if (SFXListenerCount == 1) {
        psiSFXSetupListener(pos, vel, dir, up, norm);
        ListenerPos = *pos;
        ListenerVel = *vel;
        ListenerNorm = *norm;
        ListenerDir = *dir;
        ListenerUp = *up;
        ListenerPositions[0] = *pos;
        return;
    }
    ListenerNorm = NormVector;
    ListenerDir = DirVector;
    ListenerUp = UpVector;
    ListenerPositions[ListenerNumber] = *pos;
}

// AUTOINJECT
void SFXSetNumberOfListeners(int ListenerCount) {
    if (ListenerCount > 1)
        psiSFXSetupListener(&ZeroPos, &ZeroPos, &DirVector, &UpVector, &NormVector);
    SFXListenerCount = ListenerCount;
}

// ---- the file access a cut scene's streaming suspends

// AUTOINJECT
void SFXSuspendFileAccess(void) {
    SFXFileAccessSuspended = 1;
    SFXUpdate();
}

// AUTOINJECT
void SFXUnSuspendFileAccess(void) {
    SFXFileAccessSuspended = 0;
}

// ---- the voices

// AUTOINJECT
int ES_FindFreeVoiceHandle(void) {
    for (int i = 0; i < SFX_NUM_VOICE_HANDLES; i++) {
        if (ES_VoiceHandleData[i].Status == 0) {
            ES_VoiceHandleData[i].Status = 1;
            return i;
        }
    }
    return -1;
}

// AUTOINJECT
void ES_UnlockVoiceHandle(int voiceIdx) {
    if (voiceIdx == -1)
        return;
    ES_VoiceHandleDetails *voice = &ES_VoiceHandleData[voiceIdx];
    if (voice->IsStream) {
        voice->Status = 0;
        Streams[voice->Handle].Status = STREAM_FREE;
        return;
    }
    psiSFX_MarkInactive(voice->Handle);
    voice->Status = 0;
}

// Whether a voice has finished: one not yet keyed on has not
// AUTOINJECT
int HasSampleEnded(int voiceIdx, SFXItem *Sfx) {
    if (voiceIdx == -1)
        return 1;
    if (Sfx->KeyOnFrameDelay > 0)
        return 0;
    ES_VoiceHandleDetails *voice = &ES_VoiceHandleData[voiceIdx];
    if (voice->IsStream)
        return Streams[voice->Handle].Status == STREAM_STOPPED;
    return !psiIsSamplePlaying(voice->Handle);
}

// A voice for a bank's sample, or for a stream (a FileRef below 0)
// AUTOINJECT
int ES_RequestVoiceHandle(SampleHeaderData *sampleHeader, int fileRefOrHashcode, int is3d, int unused) {
    if (fileRefOrHashcode < 0) {
        int stream = SFXInitialiseStream(fileRefOrHashcode, is3d);
        if (stream == -1)
            return -1;
        int voiceIdx = ES_FindFreeVoiceHandle();
        if (voiceIdx == -1)
            return -1;
        ES_VoiceHandleData[voiceIdx].IsStream = 1;
        ES_VoiceHandleData[voiceIdx].Handle = stream;
        return voiceIdx;
    }
    uint32_t voice = psiRequestVoiceHandle(&sampleHeader[fileRefOrHashcode], is3d);
    if (voice == 0xffffffffu)
        return -1;
    int voiceIdx = ES_FindFreeVoiceHandle();
    if (voiceIdx == -1)
        return -1;
    ES_VoiceHandleData[voiceIdx].IsStream = 0;
    ES_VoiceHandleData[voiceIdx].Handle = voice;
    return voiceIdx;
}

// Whether a voice is playing, or a stream ready to
// AUTOINJECT
int ES_SFXIsReady(int voiceIdx) {
    if (voiceIdx == -1)
        return 0;
    ES_VoiceHandleDetails *voice = &ES_VoiceHandleData[voiceIdx];
    if (voice->IsStream)
        return StreamIsActive(&Streams[voice->Handle]);
    return psiIsSamplePlaying(voice->Handle);
}

// AUTOINJECT
void ES_PauseSfx(SFXItem *Sfx) {
    if (Sfx->Paused)
        return;
    for (int i = 0; i < SFX_MAX_VOICES; i++) {
        int voiceIdx = Sfx->VoiceHandle[i];
        if (voiceIdx == -1 || !ES_SFXIsReady(voiceIdx))
            continue;
        ES_VoiceHandleDetails *voice = &ES_VoiceHandleData[voiceIdx];
        if (voice->IsStream)
            psiSamplePause(Streams[voice->Handle].StreamHandle);
        else
            psiSamplePause(voice->Handle);
    }
    Sfx->Paused = 1;
}

// AUTOINJECT
void SFXPause(void) {
    SFXPaused = 1;
    for (SFXItem *Sfx = BaseSFXActive; Sfx != NULL; Sfx = Sfx->Next)
        ES_PauseSfx(Sfx);
}

// AUTOINJECT
void ES_UnPauseSfx(SFXItem *Sfx) {
    if (!Sfx->Paused)
        return;
    for (int i = 0; i < SFX_MAX_VOICES; i++) {
        int voiceIdx = Sfx->VoiceHandle[i];
        if (voiceIdx == -1 || !ES_SFXIsReady(voiceIdx))
            continue;
        ES_VoiceHandleDetails *voice = &ES_VoiceHandleData[voiceIdx];
        if (voice->IsStream)
            psiSFX_UnPause(Streams[voice->Handle].StreamHandle);
        else
            psiSFX_UnPause(voice->Handle);
    }
    Sfx->Paused = 0;
}

// AUTOINJECT
void SFXUnPause(void) {
    SFXPaused = 0;
    for (SFXItem *Sfx = BaseSFXActive; Sfx != NULL; Sfx = Sfx->Next)
        ES_UnPauseSfx(Sfx);
}

// Stops a sound's voices and puts it back in the free list
// AUTOINJECT
void KillSFX(SFXItem *Sfx) {
    for (int i = 0; i < SFX_MAX_VOICES; i++) {
        int voiceIdx = Sfx->VoiceHandle[i];
        if (voiceIdx == -1)
            continue;
        ES_VoiceHandleDetails *voice = &ES_VoiceHandleData[voiceIdx];
        if (voice->IsStream)
            SFXStopStream(&Streams[voice->Handle]);
        else
            psiSFXStop(voice->Handle);
        ES_UnlockVoiceHandle(Sfx->VoiceHandle[i]);
    }

    SFXItem *next = Sfx->Next;
    if (Sfx->Last != NULL)
        Sfx->Last->Next = next;
    else
        BaseSFXActive = next;
    if (next != NULL)
        next->Last = Sfx->Last;
    Sfx->Last = NULL;
    Sfx->Next = SFXFreeList;
    SFXFreeList = Sfx;
}

// Stops the sounds a handle (NULL for any) started as a number (-1 for any)
// AUTOINJECT
void SFXRemove(DYNAMICSOUNDS *handle, int hashCode) {
    SFXItem *next;
    for (SFXItem *Sfx = BaseSFXActive; Sfx != NULL; Sfx = next) {
        next = Sfx->Next;
        if ((Sfx->Handle == handle || handle == NULL) && (Sfx->HashCode == hashCode || hashCode == -1))
            KillSFX(Sfx);
    }
}

// What the psi layer is given for one of a sound's voices
// AUTOINJECT
void ES_FillpsiSFXData(psiSFX *SfxData, SFXItem *Sfx, int WhichVoice) {
    SfxData->TrackingType = Sfx->SFXData->TrackingType;
    ES_VoiceHandleDetails *voice = &ES_VoiceHandleData[Sfx->VoiceHandle[WhichVoice]];
    SfxData->Handle = voice->IsStream ? Streams[voice->Handle].StreamHandle : voice->Handle;
    SfxData->InnerRadius = Sfx->InnerRadius;
    SfxData->OuterRadius = Sfx->OuterRadius;
    int volume = ES_CombineVolumes(Sfx->BaseVolume[WhichVoice], Sfx);
    SfxData->Volume = volume;
    SfxData->Specials = &Sfx->Specials;
    SfxData->Frequency = Sfx->Pitch[WhichVoice];
    SfxData->Pan = Sfx->Pan[WhichVoice];
    if (Sfx->SFXData->TrackingType == SFX_TRACK_2D) {
        SfxData->Is3d = 0;
        SfxData->Positioned = 0;
    } else {
        SfxData->Is3d = 1;
        SfxData->Positioned = 1;
    }
    SfxData->IsStreamed = voice->IsStream;
    if (SfxData->IsStreamed)
        SfxData->Volume = Streams[voice->Handle].ShutDownVolume * volume / 100;
    SfxData->vel = &Sfx->vel;
    SfxData->pos = &Sfx->pos;
}

// ---- the ducker

// Moves the music's ducking towards its target, quicker while a ducking sound lasts; after it the target is full
// volume again
// AUTOINJECT
void ES_UpdateDucker(void) {
    int elapsed = (int)psiSFXGetTimer() - DuckerStartTime;
    // As the original tests it: a time more than 2^30 before the start is not over
    bool over = elapsed >= -0x40000000 && elapsed > DuckerDuration;
    int difference = (DuckerTargetVolume - DuckerCurrentVolume) * 1024;
    if (difference != 0) {
        int step = (over ? difference / 15 : difference / 6) >> 10;
        if (step == 0)
            step = difference >= 0 ? 1 : -1;
        DuckerCurrentVolume += step;
        if (step != 0)
            ES_MusicSetVolume();
    }
    if (over)
        DuckerTargetVolume = 100;
}

// ---- starting the sounds' samples

// The layer's random numbers: the top half of the first of two words
static uint32_t SFXRandom(void) {
    sfx_randomseed1 += sfx_randomseed2;
    sfx_randomseed2 += (uint32_t)((int32_t)sfx_randomseed1 >> 31) + sfx_randomseed1;
    return sfx_randomseed1 >> 16;
}

// A random spread of up to 'range' either way (1024 the whole of it)
static int SFXRandomOffset(int range) {
    return ((int)(SFXRandom() % 0x801) - 0x400) * range >> 10;
}

// The frames before a sound's next sample
// AUTOINJECT
int SetCueTimer(SFXParameters *SFXData) {
    int range = SFXData->MaxDelay - SFXData->MinDelay;
    if (range <= 0)
        return SFXData->MinDelay;
    return SFXData->MinDelay + SFXRandom() % range;
}

// The voices the other sounds hold. Not named on PS2; the name is ours.
// AUTOINJECT
int SFXCountVoicesInUse(SFXItem *except) {
    int count = 0;
    for (SFXItem *Sfx = BaseSFXActive; Sfx != NULL; Sfx = Sfx->Next) {
        if (Sfx == except)
            continue;
        for (int i = 0; i < SFX_MAX_VOICES; i++) {
            if (Sfx->VoiceHandle[i] != -1)
                count++;
        }
    }
    return count;
}

// The square of the distance to the nearest listener, in hundreds (inline in the original). The x87 keeps each
// distance unrounded until it is the nearest.
static float NearestListenerSq(const SFXItem *Sfx) {
    float nearest = 0.0f;
    for (int i = 0; i < SFXListenerCount; i++) {
        const _VECTOR *listener = &ListenerPositions[i];
        Extended dx = (Extended(listener->x) - Extended(Sfx->RealPos.x)) * Extended(0.01f);
        Extended dy = (Extended(listener->y) - Extended(Sfx->RealPos.y)) * Extended(0.01f);
        Extended dz = (Extended(listener->z) - Extended(Sfx->RealPos.z)) * Extended(0.01f);
        Extended distanceSq = dz * dz + dy * dy + dx * dx;
        if (i == 0 || distanceSq < Extended(nearest))
            nearest = (float)distanceSq;
    }
    return nearest;
}

// Where a sound plays from: in front of the listener for one that follows it, and with more than one listener (or
// for one placed by the nearest) as far in front of the first as the nearest listener is from it
// AUTOINJECT
void ES_GetMyPos(SFXItem *Sfx) {
    SFXParameters *SFXData = Sfx->SFXData;
    if (SFXData->TrackingType == SFX_TRACK_LISTENER) {
        Sfx->InnerRadius = 2;
        Sfx->OuterRadius = 3;
        Sfx->pos = ListenerPos;
        Sfx->pos.x = ListenerDir.x + Sfx->pos.x;
        Sfx->pos.y = ListenerDir.y + Sfx->pos.y;
        Sfx->pos.z = ListenerDir.z + Sfx->pos.z;
        Sfx->vel = ZeroPos;
    }
    if (SFXListenerCount > 1 || SFXData->TrackingType == SFX_TRACK_NEAREST) {
        float nearestSq = NearestListenerSq(Sfx);
        Sfx->pos = ListenerPos;
        Extended distance = FSqrt(Extended(nearestSq)) * Extended(100.0f);
        Sfx->pos.x = (float)(Extended(ListenerDir.x) * distance + Extended(Sfx->pos.x));
        Sfx->pos.y = (float)(Extended(ListenerDir.y) * distance + Extended(Sfx->pos.y));
        Sfx->pos.z = (float)(Extended(ListenerDir.z) * distance + Extended(Sfx->pos.z));
    }
}

// The distance to the nearest listener, in tens. Not named on PS2; the name is ours.
// AUTOINJECT
int ES_GetNearestListenerDistance(SFXItem *Sfx) {
    return Ftol(FSqrt(Extended(NearestListenerSq(Sfx))) * Extended(0.1f));
}

// A voice keyed on, a stream's or the psi layer's (inline in the original)
static void SampleKeyOn(int voiceIdx, int reverbSend) {
    if (voiceIdx == -1)
        return;
    ES_VoiceHandleDetails *voice = &ES_VoiceHandleData[voiceIdx];
    if (voice->IsStream)
        ES_StreamKeyOn(voice->Handle, reverbSend);
    else
        psiSampleKeyOn(voice->Handle, reverbSend);
}

// One of a sound's samples on a voice, with its random pitch, volume and pan; keyed on unless it waits for frames
// AUTOINJECT
int StartSample(SFXSamplePoolFiles *sfxNum, int SoundBankSlot, SFXItem *Sfx, int WhichVoice) {
    SampleHeaderData *sampleHeader = SoundBankData[SoundBankSlot].SampleHeader;
    int voiceIdx = ES_RequestVoiceHandle(sampleHeader, sfxNum->FileRef, Sfx->SFXData->TrackingType != SFX_TRACK_2D, 0);
    Sfx->VoiceHandle[WhichVoice] = voiceIdx;
    if (voiceIdx == -1)
        return 0;

    int frequency;
    if (sfxNum->FileRef < 0) {
        frequency = 0x759;
        Sfx->IsStreamed = 1;
    } else {
        frequency = sampleHeader[sfxNum->FileRef].Frequency;
        Sfx->IsStreamed = 0;
    }
    int pitch = frequency + (SFXRandomOffset(sfxNum->RandomPitchOffset) + sfxNum->PitchOffset) * frequency / 0x3000;
    int volume = SFXRandomOffset(sfxNum->RandomVolumeOffset) + sfxNum->BaseVolume;
    if (volume < 0)
        volume = 0;
    else if (volume > 100)
        volume = 100;
    int pan = SFXRandomOffset(sfxNum->RandomPan) + sfxNum->Pan;
    if (pan < -100)
        pan = -100;
    else if (pan > 100)
        pan = 100;
    Sfx->BaseVolume[WhichVoice] = volume;
    Sfx->Pan[WhichVoice] = pan;
    Sfx->Pitch[WhichVoice] = pitch;

    Sfx->pos = Sfx->RealPos;
    ES_GetMyPos(Sfx);
    psiSFX SfxData;
    ES_FillpsiSFXData(&SfxData, Sfx, WhichVoice);
    psiInitialiseSound(&SfxData);
    if (Sfx->KeyOnFrameDelay == 0)
        SampleKeyOn(Sfx->VoiceHandle[WhichVoice], Sfx->SFXData->ReverbSend);
    return 1;
}

// Starts a sound's first sample, or cues its samples. A 3D sound too far off for its priority, or one over the
// voices or its own limit that may not stop others, does not start. 0 if it did not.
// AUTOINJECT
int maybeDXSoundPlay(SFXItem *Sfx) {
    SFXSamplePoolFiles *pool = (SFXSamplePoolFiles *)(Sfx->SFXData + 1);
    for (int i = 0; i < SFX_MAX_VOICES; i++)
        Sfx->VoiceHandle[i] = -1;
    if (SFXStartLoad > 2000)
        SFXStartLoad = 2000;
    Sfx->DeleteMe = 0;
    Sfx->IsStreamed = 0;
    // Fewer voices the more sounds have started lately
    int voiceBudget = 45 - SFXStartLoad / 400;
    int voicesInUse = SFXCountVoicesInUse(Sfx);

    if (Sfx->SFXData->TrackingType == SFX_TRACK_3D && Sfx->SFXData->Priority < 100) {
        int priority = Sfx->SFXData->Priority;
        int distance = ES_GetNearestListenerDistance(Sfx);
        if (distance > Sfx->OuterRadius) {
            Sfx->DeleteMe = 1;
            return 0;
        }
        int range = Sfx->OuterRadius - Sfx->InnerRadius;
        if (range < 1)
            range = 1;
        int loudness = 100 - (distance - Sfx->InnerRadius) * 100 / range;
        if (loudness < 0)
            loudness = 0;
        else if (loudness > 100)
            loudness = 100;
        if (priority * loudness < voicesInUse * voiceBudget * 2000 / (voiceBudget * voiceBudget)) {
            Sfx->DeleteMe = 1;
            return 0;
        }
    }

    if (voicesInUse > voiceBudget) {
        // Marks as many of the lowest priority sounds as there are voices over
        for (int over = SFXCountVoicesInUse(Sfx) - voiceBudget; over > 0; over--) {
            SFXItem *lowest = NULL;
            int priority = 10000;
            for (SFXItem *other = BaseSFXActive; other != NULL; other = other->Next) {
                if (!other->DeleteMe && !other->IsStreamed && other->SFXData->TrackingType != SFX_TRACK_LISTENER &&
                    other->SFXData->Priority < priority) {
                    priority = other->SFXData->Priority;
                    lowest = other;
                }
            }
            if (lowest != NULL)
                lowest->DeleteMe = 1;
        }
        if (Sfx->SFXData->MaxReject) {
            Sfx->DeleteMe = 1;
            return 0;
        }
        if (Sfx->DeleteMe)
            return 0;
    }

    int maxVoices = Sfx->SFXData->MaxVoices;
    if (maxVoices != 0) {
        int playing = 0;
        for (SFXItem *other = BaseSFXActive; other != NULL; other = other->Next) {
            if (Sfx->HashCode == other->HashCode && Sfx != other)
                playing++;
        }
        if (playing >= maxVoices) {
            if (Sfx->SFXData->MaxReject) {
                Sfx->DeleteMe = 1;
                return 0;
            }
            // The oldest of them goes
            SFXItem *oldest = NULL;
            int age = -1;
            for (SFXItem *other = BaseSFXActive; other != NULL; other = other->Next) {
                if (Sfx->HashCode == other->HashCode && Sfx != other && other->Counter > age) {
                    age = other->Counter;
                    oldest = other;
                }
            }
            oldest->DeleteMe = 1;
        }
    }

    SFXStartLoad += 100;
    SFXParameters *SFXData = Sfx->SFXData;
    Sfx->CuePos = 0;
    if (SFXData->Ducker != 0 && (SFXData->TrackingType == SFX_TRACK_2D || SFXData->TrackingType == SFX_TRACK_LISTENER)) {
        int length = SFXData->DuckerLength;
        int target = 100 - SFXData->Ducker;
        DuckerStartTime = (int)psiSFXGetTimer();
        DuckerDuration = length;
        DuckerTargetVolume = target;
    }

    SFXData = Sfx->SFXData;
    if (!SFXData->MultiSample) {
        uint32_t pick = SFXData->SampleCount > 0 ? SFXRandom() % (uint32_t)SFXData->SampleCount : 0;
        if (!StartSample(&pool[pick], Sfx->SoundBankSlot, Sfx, 0)) {
            Sfx->DeleteMe = 1;
            return 0;
        }
        Sfx->CueSampleWaiting[0] = SetCueTimer(Sfx->SFXData);
        return 1;
    }

    if (SFXData->Polyphonic) {
        // All of them, each after its own delay (SFXUpdate starts them)
        for (int i = 0; i < Sfx->SFXData->SampleCount; i++) {
            Sfx->CueSampleWaiting[i] = SetCueTimer(Sfx->SFXData);
            Sfx->CuedSamples[i] = &pool[i];
        }
        return 1;
    }

    // One after another, from the last cued
    Sfx->CuePos = SFXData->SampleCount;
    Sfx->CueSampleWaiting[0] = 0;
    if (SFXData->Shuffled) {
        int taken[SFX_MAX_CUED] = {0};
        int left = SFXData->SampleCount;
        for (int i = 0; i < Sfx->SFXData->SampleCount; i++, left--) {
            int skip = left > 0 ? SFXRandom() % (uint32_t)left : 0;
            int slot = 0;
            do {
                if (skip > 0)
                    slot++;
                while (taken[slot])
                    slot++;
            } while (skip-- > 0);
            taken[slot] = 1;
            Sfx->CuedSamples[i] = &pool[slot];
        }
    } else {
        SFXSamplePoolFiles *sample = &pool[Sfx->CuePos - 1];
        for (int i = 0; i < Sfx->SFXData->SampleCount; i++)
            Sfx->CuedSamples[i] = sample--;
    }
    Sfx->CuePos--;
    StartSample(Sfx->CuedSamples[Sfx->CuePos], Sfx->SoundBankSlot, Sfx, 0);
    Sfx->CueSampleWaiting[0] = SetCueTimer(Sfx->SFXData);
    return 1;
}

// A sound's last sample has ended: it goes, or a looping one starts again once its delay is over (and is updated
// again this frame if it has not been twice)
// AUTOINJECT
void SFXUpdateEnding(SFXItem *Sfx, SFXItem **next) {
    if (!Sfx->SFXData->Loop) {
        Sfx->DeleteMe = 1;
        return;
    }
    if (Sfx->CueSampleWaiting[0]-- > 0)
        return;
    if (maybeDXSoundPlay(Sfx) && Sfx->UpdatedCounter < 2)
        *next = Sfx;
}

// Starts a sound by its number, at pos (or wherever the SFXItem was), for a handle. The radii given (-1 for the
// sound's own) are kept; the volume is not used. 0 if it started, 1 if no bank has it or it did not start, 2 if
// there is no free SFXItem, 3 for the cut scenes' music trigger. A streamed sound already waiting for its key-on
// is keyed on rather than started again.
// AUTOINJECT
uint32_t SFXStart3D(int HashCode, _VECTOR *pos, _VECTOR *vel, DYNAMICSOUNDS *Handle, int VolumeOverRide,
                    int InnerRadius, int OuterRadius) {
    int keyOnFrameDelay = SFXKeyOnFrameDelay;
    SFXKeyOnFrameDelay = 0;
    if (HashCode == SFX_NIS_MUSIC_TRIGGER) {
        SfxNisMusicTrigger = 1;
        return 3;
    }
    SFXRememberRequest(HashCode, Handle);

    for (int bank = 0; bank < SFX_NUM_BANKS; bank++) {
        SoundBankHeader *soundBank = &SoundBankData[bank];
        if (!soundBank->Loaded)
            continue;
        for (int i = 0; i < soundBank->SFXCount; i++) {
            SFXHeader *header = &soundBank->SFXHeaderData[i];
            if (header->HashCode != HashCode)
                continue;

            SFXParameters *SFXData = (SFXParameters *)((char *)soundBank->SFXBuffer + header->Offset);
            SFXItem *first = BaseSFXActive;
            if (SFXData->SampleCount == 1) {
                SFXSamplePoolFiles *sample = (SFXSamplePoolFiles *)(SFXData + 1);
                if (sample->FileRef < 0) {
                    if (keyOnFrameDelay == 0) {
                        for (SFXItem *Sfx = first; Sfx != NULL; Sfx = Sfx->Next) {
                            if (Sfx->HashCode != HashCode || Sfx->KeyOnFrameDelay == 0)
                                continue;
                            Sfx->KeyOnFrameDelay = 0;
                            if (Sfx->VoiceHandle[0] == -1) {
                                Sfx->DeleteMe = 1;
                                continue;
                            }
                            Sfx->StartedWhenNisPlaying = NisPlaying;
                            Sfx->StartedWhenPaused = SFXPaused;
                            SampleKeyOn(Sfx->VoiceHandle[0], Sfx->SFXData->ReverbSend);
                            return 0;
                        }
                    } else {
                        for (SFXItem *Sfx = first; Sfx != NULL; Sfx = Sfx->Next) {
                            if (Sfx->HashCode == HashCode && Sfx->KeyOnFrameDelay != 0)
                                return 0;
                        }
                    }
                } else if (keyOnFrameDelay != 0) {
                    return 0;
                }
            }

            SFXItem *Sfx = SFXFreeList;
            if (Sfx == NULL)
                return 2;
            if (first != NULL)
                first->Last = Sfx;
            SFXFreeList = Sfx->Next;
            Sfx->Next = first;
            Sfx->Last = NULL;
            Sfx->StartedWhenNisPlaying = NisPlaying;
            Sfx->Handle = Handle;
            Sfx->StartedWhenPaused = SFXPaused;
            BaseSFXActive = Sfx;
            Sfx->FadeAndKillVolume = 100;
            Sfx->FadeAndKill = 0;
            if (pos != NULL) {
                Sfx->pos = *pos;
                Sfx->RealPos = *pos;
            }
            if (vel != NULL)
                Sfx->vel = *vel;
            Sfx->VolumeOverRide = 100;
            Sfx->InnerRadius = InnerRadius == -1 ? SFXData->InnerRadiusReal : InnerRadius;
            Sfx->OuterRadius = OuterRadius == -1 ? SFXData->OuterRadiusReal : OuterRadius;
            Sfx->SFXData = SFXData;
            Sfx->HashCode = HashCode;
            Sfx->SoundBankSlot = bank;
            Sfx->Counter = 0;
            Sfx->Paused = 0;
            Sfx->KeyOnFrameDelay = keyOnFrameDelay;
            return !maybeDXSoundPlay(Sfx);
        }
    }
    return 1;
}

// AUTOINJECT
uint32_t SFXStart(int HashCode, DYNAMICSOUNDS *Handle) {
    return SFXStart3D(HashCode, &SFXZeroPoint, &SFXZeroPoint, Handle, -1, -1, -1);
}

// ---- the frame

// Once a frame: the environment, the ducker, the music and the fade, the stream; then each sound unpaused is aged,
// faded, has its ended samples freed and the next cued, and is moved and mixed; those paused in cut scenes are
// paused or let go; the sounds marked go, and the list is copied for SFXIsSFXPlaying. Ghidra gives the name to
// the dsnd layer's update at 0x000e19c0 too.
// FUNC_AT(0x000cad40)
void SFXUpdate(void) {
    if (SFXStartLoad > 0) {
        SFXStartLoad = SFXStartLoad * 9 / 10;
        if (SFXStartLoad < 0)
            SFXStartLoad = 0;
    }
    if (SFXFileAccessSuspended)
        SFXFileAccessSuspendedToggle = 1 - SFXFileAccessSuspendedToggle;

    SFXItem *first = BaseSFXActive;
    SFXUpdateEnvironment();
    ES_UpdateDucker();
    SFXAgeRequests();
    UpdateMusicalEvents();
    SFXFadeUpdate();
    if (NisQuitten)
        DuckerDuration = 0;
    for (SFXItem *Sfx = first; Sfx != NULL; Sfx = Sfx->Next) {
        if (NisQuitten && Sfx->StartedWhenNisPlaying)
            Sfx->FadeAndKill = 1;
        Sfx->UpdatedCounter = 0;
    }
    SFXUpdateStreams();

    SFXItem *next;
    for (SFXItem *Sfx = BaseSFXActive; Sfx != NULL; Sfx = next) {
        next = Sfx->Next;
        if (!Sfx->Paused) {
            if (Sfx->FadeAndKill) {
                Sfx->FadeAndKillVolume -= 5;
                if (Sfx->FadeAndKillVolume < 0) {
                    Sfx->FadeAndKillVolume = 0;
                    Sfx->DeleteMe = 1;
                }
            }
            Sfx->UpdatedCounter++;
            Sfx->Counter++;
            if (Sfx->KeyOnFrameDelay != 0 && --Sfx->KeyOnFrameDelay == 0)
                Sfx->DeleteMe = 1;

            SFXParameters *SFXData = Sfx->SFXData;
            if (!SFXData->MultiSample) {
                if (HasSampleEnded(Sfx->VoiceHandle[0], Sfx)) {
                    ES_UnlockVoiceHandle(Sfx->VoiceHandle[0]);
                    Sfx->VoiceHandle[0] = -1;
                    SFXUpdateEnding(Sfx, &next);
                }
            } else if (!SFXData->Polyphonic) {
                if (HasSampleEnded(Sfx->VoiceHandle[0], Sfx)) {
                    ES_UnlockVoiceHandle(Sfx->VoiceHandle[0]);
                    Sfx->VoiceHandle[0] = -1;
                    if (Sfx->CuePos == 0) {
                        SFXUpdateEnding(Sfx, &next);
                    } else if (Sfx->CueSampleWaiting[0]-- <= 0) {
                        Sfx->CuePos--;
                        StartSample(Sfx->CuedSamples[Sfx->CuePos], Sfx->SoundBankSlot, Sfx, 0);
                        Sfx->CueSampleWaiting[0] = SetCueTimer(Sfx->SFXData);
                    }
                }
            } else {
                // Each sample starts when its wait reaches 0 and is freed when it ends; once all have, the sound ends
                bool allEnded = true;
                for (int i = 0; i < Sfx->SFXData->SampleCount; i++) {
                    int waiting = Sfx->CueSampleWaiting[i];
                    if (waiting == 0) {
                        StartSample(Sfx->CuedSamples[i], Sfx->SoundBankSlot, Sfx, i);
                        Sfx->CueSampleWaiting[i] = -1;
                        allEnded = false;
                    } else if (waiting > 0) {
                        allEnded = false;
                        Sfx->CueSampleWaiting[i] = waiting - 1;
                    } else if (HasSampleEnded(Sfx->VoiceHandle[i], Sfx)) {
                        ES_UnlockVoiceHandle(Sfx->VoiceHandle[i]);
                        Sfx->VoiceHandle[i] = -1;
                    } else {
                        allEnded = false;
                    }
                }
                if (allEnded)
                    SFXUpdateEnding(Sfx, &next);
            }

            Sfx->pos = Sfx->RealPos;
            ES_GetMyPos(Sfx);
            for (int i = 0; i < SFX_MAX_VOICES; i++) {
                if (Sfx->VoiceHandle[i] != -1) {
                    psiSFX SfxData;
                    ES_FillpsiSFXData(&SfxData, Sfx, i);
                    psiUpdateSound(&SfxData);
                }
            }
        }

        if (Sfx->SFXData->PauseInNis && !SFXPaused) {
            if (NisPlaying || SFXFileAccessSuspended)
                ES_PauseSfx(Sfx);
            else if (!SFXFileAccessSuspendedToggle)
                ES_UnPauseSfx(Sfx);
        }
    }

    for (SFXItem *Sfx = BaseSFXActive; Sfx != NULL; Sfx = next) {
        next = Sfx->Next;
        if (Sfx->DeleteMe)
            KillSFX(Sfx);
    }

    SFXActiveCount = 0;
    for (SFXItem *Sfx = BaseSFXActive; Sfx != NULL; Sfx = Sfx->Next) {
        SFXActiveData[SFXActiveCount].Handle = Sfx->Handle;
        SFXActiveData[SFXActiveCount].HashCode = Sfx->HashCode;
        SFXActiveCount++;
    }
}

// ---- the banks

// The free list of all the SFXItems, in order. Not named on PS2; the name is ours.
// AUTOINJECT
void SFXInitialiseItems(void) {
    BaseSFXActive = NULL;
    SFXFreeList = &SFXItems[0];
    for (int i = 0; i < SFX_NUM_ITEMS; i++) {
        SFXItems[i].Next = i == SFX_NUM_ITEMS - 1 ? NULL : &SFXItems[i + 1];
        SFXItems[i].Last = i > 0 ? &SFXItems[i - 1] : NULL;
    }
}

// The SBInfoList slot of a bank, or -1 (inline in the original)
static int FindSoundBankSlot(int index) {
    for (int slot = 0; slot < SFX_NUM_BANKS; slot++) {
        if (SBInfoList[slot] == index)
            return slot;
    }
    return -1;
}

// Loads a bank, by its number in SBINFO.SBI, from the language's SB_<slot / 8> folder
// AUTOINJECT
void LoadSoundBank(int index) {
    int slot = FindSoundBankSlot(index);
    if (slot == -1)
        return;
    SoundBankHeader *bank = &SoundBankData[slot];
    if (bank->Loaded)
        return;

    char sfxFilename[200], shfFilename[200], sbfFilename[200], shgFilename[200];
    sprintf(sfxFilename, "%s%s/SB_%d/SB_%d.SFX", SFXFilePath, SFXLanguageName, slot / 8, slot);
    sprintf(shfFilename, "%s%s/SB_%d/SB_%d.SHF", SFXFilePath, SFXLanguageName, slot / 8, slot);
    sprintf(sbfFilename, "%s%s/SB_%d/SB_%d.SBF", SFXFilePath, SFXLanguageName, slot / 8, slot);
    sprintf(shgFilename, "%s%s/SB_%d/SB_%d.SHG", SFXFilePath, SFXLanguageName, slot / 8, slot);

    int *sfxFile = (int *)psiSFXLoadFile(sfxFilename);
    bank->SFXBuffer = sfxFile;
    if (sfxFile == NULL)
        return;
    bank->SFXHeaderData = (SFXHeader *)(sfxFile + 1);
    bank->SFXCount = sfxFile[0];

    int *shfFile = (int *)psiSFXLoadFile(shfFilename);
    bank->SampleHeaderBuffer = shfFile;
    if (shfFile == NULL) {
        psiSFXFreeFileMem(bank->SFXBuffer);
        return;
    }
    bank->SampleHeader = (SampleHeaderData *)(shfFile + 1);
    bank->SampleCount = shfFile[0];

    if (bank->SampleCount != 0) {
        bank->psi_SampleHeaderBuffer = NULL;
        char *samples = (char *)psiSFXLoadFile(sbfFilename);
        if (samples == NULL) {
            psiSFXFreeFileMem(bank->SFXBuffer);
            psiSFXFreeFileMem(bank->SampleHeaderBuffer);
            if (bank->psi_SampleHeaderBuffer != NULL)
                psiSFXFreeFileMem(bank->psi_SampleHeaderBuffer);
            return;
        }
        // The samples one after another in the .SBF file
        SampleHeaderData *sample = bank->SampleHeader;
        for (int i = 0; i < bank->SampleCount; i++, sample++) {
            sample->Address = samples;
            sample->psi_SampleHeader = (char *)((uintptr_t)sample->psi_SampleHeader + (uintptr_t)bank->psi_SampleHeaderBuffer);
            samples += sample->Size;
        }
    }
    bank->Loaded = 1;
    bank->HashCode = index;
}

// Stops the bank's sounds and frees it. Not named on PS2; the name is ours.
// AUTOINJECT
void SFXUnloadSoundBank(int index) {
    int slot = FindSoundBankSlot(index);
    if (slot == -1)
        return;
    SoundBankHeader *bank = &SoundBankData[slot];
    if (!bank->Loaded)
        return;
    SFXItem *next;
    for (SFXItem *Sfx = BaseSFXActive; Sfx != NULL; Sfx = next) {
        next = Sfx->Next;
        if (Sfx->SoundBankSlot == slot)
            KillSFX(Sfx);
    }
    psiSFXFreeFileMem(bank->SFXBuffer);
    psiSFXFreeFileMem(bank->SampleHeaderBuffer);
    if (bank->psi_SampleHeaderBuffer != NULL)
        psiSFXFreeFileMem(bank->psi_SampleHeaderBuffer);
    bank->Loaded = 0;
}

// Stops every sound and the stream, frees the banks, and puts the mixer back as it starts
// AUTOINJECT
void SFXReset(void) {
    SFXItem *next;
    for (SFXItem *Sfx = BaseSFXActive; Sfx != NULL; Sfx = next) {
        next = Sfx->Next;
        KillSFX(Sfx);
    }
    for (int slot = 0; slot < SFX_NUM_BANKS; slot++) {
        if (SoundBankData[slot].Loaded)
            SFXUnloadSoundBank(SoundBankData[slot].HashCode);
    }
    StreamData *stream = &Streams[0];
    if (StreamIsActive(stream))
        SFXStopStream(stream);
    stream->Status = STREAM_FREE;
    SFXReleaseStreamSystem();
    SFXInitialiseAudioStreamSystem();

    FadeVolume = 10000;
    FadeMusicVolume = 10000;
    DuckerDuration = 1;
    DuckerTargetVolume = 100;
    DuckerCurrentVolume = 100;
    DuckerStartTime = 0;
    SFXStartLoad = 0;
    SFXFileAccessSuspendedToggle = 0;
    FadeStatus = 0;
    FadeRate = 120;
    SFXPaused = 0;
    for (int i = 0; i < SFX_NUM_VOICE_HANDLES; i++)
        ES_VoiceHandleData[i].Status = 0;
    ES_MuteMusic = 1;
    ES_MuteSFX = 1;
    SFXNewReverb = 0;
    SFXReverb = 0;
    psiSetReverb(0);
    SFXNewIndoorsVolume = 100;
    SFXIndoorsVolume = 100;
    SFXUnderWater = 0;
    for (int i = 0; i < SFX_NUM_ITEMS; i++)
        SFXRequestedActiveData[i].Life = 0;
}

// Once: the language, the paths, and the list of banks from SBINFO.SBI
// AUTOINJECT
void SFXInitialise(char *path) {
    if (SFXInitialised)
        return;
    UpdateLanguageName();
    if (path != NULL)
        SFXSetFilePath(path);
    psiSFXInit();
    SFXInitialised = 1;

    char filename[200];
    sprintf(filename, "%sSBINFO.SBI", SFXFilePath);
    int *info = (int *)psiSFXLoadFile(filename);
    if (info == NULL)
        return;
    for (int slot = 0; slot < SFX_NUM_BANKS; slot++) {
        SoundBankData[slot].Loaded = 0;
        SBInfoList[slot] = info[slot];
    }
    psiSFXFreeFileMem(info);
    SFXInitialiseItems();
    SFXReset();
}

// The level's bank, and the music's start. The front end's two banks take turns.
// AUTOINJECT
void SFXLoadCorrectBank(void) {
    SFXReset();
    SFXSetNumberOfListeners(MPSettings.isMultiplayer ? MPSettings.numPlayers : 1);
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_HendersonA: LoadSoundBank(1); break;
    case HT_Level_HendersonB: LoadSoundBank(2); break;
    case HT_Level_HendersonC: LoadSoundBank(3); break;
    case HT_Level_HendersonD: LoadSoundBank(8); break;
    case HT_Level_CastleExterior: LoadSoundBank(5); break;
    case HT_Level_CastleCourtyard: LoadSoundBank(4); break;
    case HT_Level_CastleIndoors1: LoadSoundBank(6); break;
    case HT_Level_CastleIndoors2: LoadSoundBank(7); break;
    case HT_Level_TowerA: LoadSoundBank(0x17); break;
    case HT_Level_TowerB: LoadSoundBank(0x18); break;
    case HT_Level_TowerC: LoadSoundBank(0x19); break;
    case HT_Level_PowerStationA1: LoadSoundBank(0x1a); break;
    case HT_Level_PowerStationA2: LoadSoundBank(0x37); break;
    case HT_Level_Tower2A: LoadSoundBank(0x1e); break;
    case HT_Level_Tower2B: LoadSoundBank(0x1f); break;
    case HT_Level_Tower2C: LoadSoundBank(0x20); break;
    case HT_Level_Tower2Elevator: LoadSoundBank(0x21); break;
    case HT_Level_EvilBase: LoadSoundBank(0x27); break;
    case HT_Level_EvilSilo: LoadSoundBank(0x28); break;
    case HT_Level_EvilBaseC: LoadSoundBank(0x2b); break;
    case (HASHCODE)0x700001a:
    case HT_Level_SpaceStationD: LoadSoundBank(0x25); break;
    case HT_Level_SpaceStation:
    case HT_Level_Facility:
    case HT_Level_Atlantis:
    case HT_Level_SkyRail:
    case HT_Level_SubPen:
    case HT_Level_StealthShip:
    case HT_Level_FortKnox:
    case HT_Level_MissileSilo:
    case HT_Level_SnowBlind:
    case HT_Level_Ravine:
    case (HASHCODE)0x700004c: LoadSoundBank(0x16); break;
    case HT_Level_RefRoom: LoadSoundBank(0xb); break;
    case HT_Level_Menu_Pre:
        LoadSoundBank(ls.relatedToMainMenuSound ? 0x38 : 0xc);
        ls.relatedToMainMenuSound = 1 - ls.relatedToMainMenuSound;
        break;
    default:
        break;
    }
    SoundInit = 1;
    MusicEventList[MUSICEV_LEVEL] = GameState.CurrentLevelHashcode;
    MusicEventList[MUSICEV_NEW_LEVEL] = 1;
}
