#include "SFX.h"

// The stream: the level's music (a track's .SSD file with its .SMF markers) or a streamed sound out of STREAMS.BIN,
// read a file buffer at a time and copied into a looping voice's playback buffer a piece a frame.

// Whether it is playing, or able to
static bool StreamIsActive(const StreamData *stream) {
    return stream->Status != STREAM_FREE && stream->Status != STREAM_WAITING && stream->Status != STREAM_STOPPED &&
           stream->FinishedInitialise;
}

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
