#include "SFX.h"

// The stream: the level's music (a track's .SSD file with its .SMF markers) or a streamed sound out of STREAMS.BIN,
// read a file buffer at a time and copied into a looping voice's playback buffer a piece a frame.

// The track's number in MFXINFO.MXI, from 1, or -1
// AUTOINJECT
int FindMusicTrackNumber(int MusicHashCode) {
    for (int i = 0; i < SFX_NUM_MUSIC_TRACKS; i++) {
        if (MusicTrackInfo[i] == MusicHashCode)
            return i + 1;
    }
    return -1;
}

// The music's volume through the ducker, the master volume and the fade, squared to 0 to 100
// AUTOINJECT
void ES_MusicSetVolume(void) {
    StreamData *stream = &Streams[0];
    if (!StreamIsActive(stream))
        return;
    int volume = stream->BaseVolume * DuckerCurrentVolume * MusicMasterVolume / 10000 * FadeMusicVolume / 10000 *
                 stream->ShutDownVolume / 100;
    psiSFX_SetVolume(stream->StreamHandle, volume * volume / 100);
}

// AUTOINJECT
void SFXMusicSetVolume(int Volume) {
    MusicMasterVolume = Volume;
    ES_MusicSetVolume();
}

// AUTOINJECT
int SFXMusicGetVolume(void) {
    return MusicMasterVolume;
}

// A stream's voice keyed on: at once, or once the stream it waits for is initialised
// AUTOINJECT
void ES_StreamKeyOn(int Handle, int ReverbDepth) {
    StreamData *stream = &Streams[Handle];
    if (stream->BeenStolen) {
        StreamIntialiseDetails *waiting = &StreamWaitingIntialise[stream->BeenStolenByInitialiseRef];
        waiting->KeyOn = 1;
        waiting->ReverbDepth = ReverbDepth;
        return;
    }
    stream->KeyOn = 1;
    stream->ReverbDepth = ReverbDepth;
}

// AUTOINJECT
void SFXPauseAllStreams(void) {
    AllStreamsPaused = 1;
    StreamData *stream = &Streams[0];
    if (StreamIsActive(stream) && stream->StartedPlaying) {
        psiSamplePause(stream->StreamHandle);
        stream->GamePausedMe = 1;
    }
}

// AUTOINJECT
void SFXUnPauseAllStreams(void) {
    AllStreamsPaused = 0;
    StreamData *stream = &Streams[0];
    if (StreamIsActive(stream) && stream->GamePausedMe == 1) {
        psiSFX_UnPause(stream->StreamHandle);
        stream->GamePausedMe = 0;
    }
}
