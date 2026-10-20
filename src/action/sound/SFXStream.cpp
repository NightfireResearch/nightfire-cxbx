#include "SFX.h"
#include "../engine/FS.h"
#include "dsndSeam.h"

#include <stdio.h>

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

// ---- the stream's buffers

// The playback buffer's voice and the read buffers go; the psi layer's free of each read buffer is an empty
// function on the Xbox
// AUTOINJECT
void SFXReleaseStreamSystem(void) {
    if (!EnableReleaseAudioStreamSystem)
        return;
    if (Streams[0].StreamHandle != -1)
        dsndDeleteChannel(Streams[0].StreamHandle);
    if (Streams[0].StreamHandle2 != -1)
        dsndDeleteChannel(Streams[0].StreamHandle2);
    EnableReleaseAudioStreamSystem = 0;
}

// The tracks' list from MFXINFO.MXI, once, and the stream free
// AUTOINJECT
void SFXInitialiseAudioStreamSystem(void) {
    if (!HaveCalledOnce) {
        char filename[200];
        sprintf(filename, "%sMFXINFO.MXI", SFXFilePath);
        int *trackInfo = (int *)psiSFXLoadFile(filename);
        if (trackInfo == NULL)
            return;
        for (int i = 0; i < SFX_NUM_MUSIC_TRACKS; i++)
            MusicTrackInfo[i] = trackInfo[i];
        psiSFXFreeFileMem(trackInfo);
        HaveCalledOnce = 1;
    }
    AllStreamsPaused = 0;
    InitialiseStatus = STREAM_READY;
    DoInitialStreamBuffer = 1;
    Streams[0].Status = STREAM_FREE;
    Streams[0].IdNumber = 0;
    StreamMemCpyHandles[0].TransferStatus = 0;
}

// The stream's playback buffer and read buffers, after a reset: the music's in stereo at 44.1 kHz, a streamed
// sound's in mono at 22.05 kHz
// AUTOINJECT
void ES_InitialStreamBuffer(void) {
    EnableReleaseAudioStreamSystem = 1;
    if (!DoInitialStreamBuffer)
        return;
    DoInitialStreamBuffer = 0;
    for (int n = 0; n < SFX_NUM_STREAMS; n++) {
        StreamData *stream = &Streams[n];
        if (n == 0) {
            stream->StreamHandle = psiStreamCreatePlaybackBuffer(44100, 2, 0x4800);
            for (int i = 0; i < STREAM_READ_BUFFERS; i++)
                stream->Read[i].DataBuffer = (char *)psiAsyncCreateBuffer(0x4800);
        } else {
            stream->StreamHandle = psiStreamCreatePlaybackBuffer(22050, 1, 0x2400);
            for (int i = 0; i < 4; i++)
                stream->Read[i].DataBuffer = (char *)psiAsyncCreateBuffer(0x2400);
            stream->DataBuffer2 = (char *)psiAsyncCreateBuffer(0x2400);
        }
    }
}

// The music reads 8 file buffers ahead, a streamed sound 4. Not named on PS2; the name is ours.
// AUTOINJECT
void ES_SetFileBufferCount(StreamData *stream) {
    if (stream->MusicStream) {
        stream->FileBufferCount = 8;
        FileBufferMask = 7;
    } else {
        stream->FileBufferCount = 4;
        FileBufferMask = 3;
    }
}

// A read of the stream's file, in bytes of the playback buffer: software ADPCM is a quarter the size in the file
// AUTOINJECT
void ES_StreamAsyncReadFile(int fileHandle, int filePos, int byteSize, char *buffer, StreamData *stream) {
    if (stream->SoftwareAdpcm) {
        byteSize /= 4;
        filePos /= 4;
    }
    psiAsyncReadFile(fileHandle, stream->FileOffset + filePos, byteSize, buffer);
}

// The ADPCM state at the section the buffer being read starts, kept for when it is copied
// AUTOINJECT
void ES_GetStreamStates(StreamData *Stream) {
    StreamReadData *read = &Stream->Read[Stream->ReadBufferNumber];
    read->GetState[0] = Stream->MusicMarkerStart->State[0];
    read->GetState[1] = Stream->MusicMarkerStart->State[1];
    read->StateSet = 1;
    read->UseThisState = 0;
}

// AUTOINJECT
void ES_SetStreamStates(StreamData *Stream) {
    StreamReadData *read = &Stream->Read[Stream->TransferBufferNumber];
    if (!read->StateSet)
        return;
    read->State[0] = read->GetState[0];
    read->State[1] = read->GetState[1];
    read->StateSet = 0;
    read->UseThisState = 1;
}

// AUTOINJECT
void ES_StartStreamMemCpy(int streamHandle, int byteOffset, char *srcData, int byteSize, StreamData *Stream) {
    StreamMemCpyHandle *copy = &StreamMemCpyHandles[Stream->IdNumber];
    copy->StreamHandle = streamHandle;
    copy->StreamByteOffset = byteOffset;
    copy->SrcData = srcData;
    copy->SrcByteSize = byteSize;
    copy->TransferStatus = STREAM_COPYING;
    copy->TransferPos = 0;
    copy->TransferedData = 0;
    copy->Stream = Stream;
}

// Whether the play position has passed a marker: one that must wrap first counts once the position has gone back
// below it or wrapped. Passing it clears it.
// AUTOINJECT
int ES_CheckEndMarker(StreamData *Stream, TestMarker *marker) {
    bool wrapped = Stream->PlayBackPos < Stream->LastPlayBackPos;
    if (marker->EndMustToggle) {
        if (Stream->PlayBackPos < marker->EndHere)
            marker->EndMustToggle = 0;
        if (wrapped)
            marker->EndMustToggle = 0;
        wrapped = false;
    }
    if (!marker->EndMustToggle && (Stream->PlayBackPos > marker->EndHere || wrapped)) {
        marker->CheckMe = 0;
        return 1;
    }
    return 0;
}

// The next read buffer goes after the last in the playback buffer, wrapping at its end
// AUTOINJECT
void ES_HandleStreamBufferLoaded(StreamData *Stream) {
    int loaded = Stream->ReadBufferNumber;
    int next = (loaded + 1) & FileBufferMask;
    Stream->ReadBufferNumber = next;
    Stream->Read[next].ByteOffset = Stream->Read[loaded].ByteSize + Stream->Read[loaded].ByteOffset;
    Stream->FilePos = Stream->Read[loaded].FilePos;
    if (Stream->Read[next].ByteOffset == Stream->PlayBackBufferSize)
        Stream->Read[next].ByteOffset = 0;
}

// ---- starting and stopping the stream

// A stream for a track (or, below 0, a streamed sound), waiting for SFXInitialiseStreamUpdate; -1 if none is free.
// The streamed sounds' streams follow the music's: with one stream there are none.
// AUTOINJECT
int SFXInitialiseStream(int HashCode, int Is3d) {
    for (int n = HashCode < 0 ? 1 : 0; n < SFX_NUM_STREAMS; n++) {
        StreamData *stream = &Streams[n];
        if (stream->Status != STREAM_FREE)
            continue;
        StreamIntialiseDetails *waiting = &StreamWaitingIntialise[StreamWaitingIntialiseCount];
        waiting->HashCode = HashCode;
        waiting->ChannelNumber = n;
        stream->BeenStolenByInitialiseRef = StreamWaitingIntialiseCount;
        StreamWaitingIntialiseCount = (StreamWaitingIntialiseCount + 1) & (SFX_NUM_STREAM_REQUESTS - 1);
        stream->Is3d = Is3d;
        stream->Status = STREAM_WAITING;
        stream->KeyOn = 0;
        SFXInitialiseStreamUpdate();
        return n;
    }
    return -1;
}

// Silences and stops a stream, closing its file; one still opening only has its file closed
// AUTOINJECT
void SFXStopStream(StreamData *stream) {
    if (StreamIsActive(stream)) {
        psiSFX_UnPause(stream->StreamHandle);
        psiSFX_SetVolume(stream->StreamHandle, 0);
        psiAsyncCloseFile(stream->FileHandle);
        StreamMemCpyHandles[stream->IdNumber].TransferStatus = 0;
        psiStreamStop(stream->StreamHandle);
        if (stream->Channels != 1)
            psiSFXFreeFileMem(stream->StreamMarkerHeader);
        stream->Status = STREAM_STOPPED;
        return;
    }
    if (stream->Status == STREAM_OPENING) {
        if (!stream->MusicStream)
            psiAsyncCloseFile(stream->FileHandle);
        stream->Status = STREAM_STOPPED;
    }
}

// AUTOINJECT
void SFXStopMusic(void) {
    SFXStopStream(&Streams[0]);
    Streams[0].Status = STREAM_FREE;
}

// A track from one of its sections
// AUTOINJECT
void SFXStartMusic(int track, int section) {
    MusicStartSection = section;
    SFXActiveMarker = section;
    SFXLoadedMarker = section;
    SFXInitialiseStream(track, 0);
    SFXJumpMarker = -1;
    SFXJumpInstant = 0;
}
