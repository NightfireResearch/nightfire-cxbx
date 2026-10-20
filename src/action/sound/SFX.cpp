#include "SFX.h"
#include "../assets.h"

#include <stdio.h>

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
    if (voice->IsStream) {
        StreamData *stream = &Streams[voice->Handle];
        return stream->Status != STREAM_FREE && stream->Status != STREAM_WAITING &&
               stream->Status != STREAM_STOPPED && stream->FinishedInitialise;
    }
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
