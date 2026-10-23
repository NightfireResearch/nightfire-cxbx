#include "SFX.h"
#include "../engine/FS.h"
#include "dsndSeam.h"

#include <stdio.h>
#include <string.h>

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
                stream->Read[i].DataBuffer = psiAsyncCreateBuffer(0x4800);
        } else {
            stream->StreamHandle = psiStreamCreatePlaybackBuffer(22050, 1, 0x2400);
            for (int i = 0; i < 4; i++)
                stream->Read[i].DataBuffer = psiAsyncCreateBuffer(0x2400);
            stream->DataBuffer2 = psiAsyncCreateBuffer(0x2400);
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

// ---- the markers

// File positions are whole ADPCM blocks: 36 bytes a channel
#define STREAM_BLOCK 72

static int WholeBlocks(int bytes) {
    return bytes / STREAM_BLOCK * STREAM_BLOCK;
}

// A read buffer's markers cleared (inline in the original)
static void ES_ClearEndMarker(StreamReadData *read) {
    read->JumpRequestCompleteMarker.EndMustToggle = 0;
    read->JumpRequestCompleteMarker.EndHere = -1;
    read->JumpRequestCompleteMarker.CheckMe = 0;
    read->CanJumpMarker.EndHere = -1;
    read->CanJumpMarker.EndMustToggle = 0;
    read->CanJumpMarker.CheckMe = 0;
    read->EndMarker.EndHere = -1;
    read->EndMarker.EndMustToggle = 0;
    read->EndMarker.CheckMe = 0;
}

// A marker at the end of a read buffer's data, which the play position must wrap to reach (inline in the original)
static void ES_SetupEndMarker(TestMarker *marker, StreamReadData *read) {
    marker->EndMustToggle = 1;
    marker->EndHere = read->ByteSize + read->ByteOffset;
    marker->CheckMe = 1;
}

// The next read: as much of the playback buffer as is left (all of it when restarting), up to a file buffer, cut at
// the first marker that ends, pauses, loops or takes the jump asked for. Once the stream is ending, silence.
// AUTOINJECT
void UpdateMusicMarkers(StreamData *stream, int restart) {
    if (stream->StartedPlaying) {
        StreamReadData *read = &stream->Read[stream->ReadBufferNumber];
        read->GetState[0] = 0;
        read->GetState[1] = 0;
        read->StateSet = 0;
        read->UseThisState = 0;
    }
    stream->Read[stream->ReadBufferNumber].ByteSize = 0;
    stream->Read[stream->ReadBufferNumber].NoDataToRead = 1;
    int readBuffer = stream->ReadBufferNumber;
    if (((readBuffer + 1) & FileBufferMask) == (uint32_t)stream->TransferBufferNumber)
        return;

    StreamReadData *read = &stream->Read[readBuffer];
    ES_ClearEndMarker(read);
    read->NoDataToRead = 0;
    int size = stream->PlayBackBufferSize;
    if (restart)
        read->ByteOffset = 0;
    else
        size -= read->ByteOffset;
    if (size > stream->BufferSize)
        size = stream->BufferSize;
    read->MarkerPassed = 0;
    read->ByteSize = size;
    read->SetToBlank = 0;
    if (stream->EndingSoon) {
        if (size > 0x2400)
            read->ByteSize = 0x2400;
        read->SetToBlank = 1;
        return;
    }

    while (stream->MusicMarker->Pos < stream->FilePos)
        stream->MusicMarker++;
    while (stream->MusicMarker->Pos < stream->FilePos + size) {
        if (stream->MusicMarker->MType != MARKER_LOOP_SILENT) {
            if (stream->HasJustLoadedJump && !stream->CanJumpMarker.CheckMe) {
                ES_SetupEndMarker(&read->CanJumpMarker, read);
                stream->HasJustLoadedJump = 0;
            }
            if (stream->HasJustRequestedLoadedJump) {
                stream->HasJustLoadedJump = stream->HasJustRequestedLoadedJump;
                stream->HasJustRequestedLoadedJump = 0;
            }
        }
        int type = stream->MusicMarker->MType;
        if (type == MARKER_END || type == MARKER_PAUSE) {
            read->ByteSize = WholeBlocks(stream->MusicMarker->Pos - stream->FilePos);
            if (stream->MusicMarker->MType == MARKER_PAUSE)
                stream->PauseNow = 1;
            ES_SetupEndMarker(&read->EndMarker, read);
            stream->EndingSoon = 1;
            return;
        }
        if (stream->StreamJumpTo > -1 && type != MARKER_LOOP_SILENT) {
            // The jump asked for, here
            SFXLoadedMarker = stream->StreamJumpTo;
            read->ByteSize = WholeBlocks(stream->MusicMarker->Pos - stream->FilePos);
            stream->MusicMarkerStart = &stream->BaseMusicMarkerStart[stream->StreamJumpTo];
            stream->MusicMarker = &stream->BaseMusicMarker[stream->MusicMarkerStart->MarkerPos];
            read->FilePos = WholeBlocks(stream->MusicMarker->Pos);
            stream->StreamJumpTo = -1;
            ES_SetupEndMarker(&read->JumpRequestCompleteMarker, read);
            ES_GetStreamStates(stream);
            stream->HasJustRequestedLoadedJump = 1;
            return;
        }
        if (type != MARKER_LOOP_SILENT) {
            read->MarkerPassed = 1;
            read->MarkerPassedByteSize = WholeBlocks(stream->MusicMarker->Pos - stream->FilePos);
        }
        type = stream->MusicMarker->MType;
        if (type == MARKER_LOOP || type == MARKER_LOOP_SILENT) {
            read->ByteSize = WholeBlocks(stream->MusicMarker->Pos - stream->FilePos);
            read->FilePos = WholeBlocks(stream->MusicMarker->LoopStart);
            stream->MusicMarker = &stream->BaseMusicMarker[stream->MusicMarker->LoopMarkerCount];
            stream->MusicMarkerStart = &stream->BaseMusicMarkerStart[stream->MusicMarker->Name];
            ES_GetStreamStates(stream);
            return;
        }
        stream->MusicMarker++;
    }
    read->FilePos = stream->FilePos + read->ByteSize;
}

// A jump that waits for a marker, taken now if a buffer already read passed one: the reads after it are dropped and
// it is read again to the marker, then from the section asked for
// AUTOINJECT
int ES_CheckFileBuffersForJumpMarkers(StreamData *Stream) {
    if (Stream->StreamJumpTo == -1 || Stream->Status != STREAM_READY)
        return 0;
    if (Stream->BaseMusicMarkerStart[Stream->StreamJumpTo].IsInstant)
        return 0;
    uint32_t transfer = Stream->TransferBufferNumber;
    uint32_t readBuffer = Stream->ReadBufferNumber;
    if (transfer == readBuffer)
        return 0;
    for (int i = 0; i < 7; i++) {
        transfer = (transfer + 1) & FileBufferMask;
        if (transfer == readBuffer)
            return 0;
        StreamReadData *read = &Stream->Read[transfer];
        if (!read->MarkerPassed)
            continue;
        Stream->ReadBufferNumber = transfer;
        ES_ClearEndMarker(read);
        read->ByteSize = read->MarkerPassedByteSize;
        SFXLoadedMarker = Stream->StreamJumpTo;
        Stream->MusicMarkerStart = &Stream->BaseMusicMarkerStart[Stream->StreamJumpTo];
        Stream->MusicMarker = &Stream->BaseMusicMarker[Stream->MusicMarkerStart->MarkerPos];
        read->FilePos = WholeBlocks(Stream->MusicMarker->Pos);
        Stream->StreamJumpTo = -1;
        ES_SetupEndMarker(&read->JumpRequestCompleteMarker, read);
        ES_GetStreamStates(Stream);
        ES_HandleStreamBufferLoaded(Stream);
        Stream->HasJustRequestedLoadedJump = 1;
        return 1;
    }
    return 0;
}

// A jump to the section asked for, now: the reads not yet copied are dropped and the section read from its start.
// It waits for a marker unless the section is instant or 'instant' says. Not named on PS2; the name is ours.
// AUTOINJECT
void ES_JumpToMusicMarker(StreamData *stream, int instant) {
    int jumpTo = stream->StreamJumpTo;
    // Read before the test of jumpTo: with none asked for, it is the word before the first start's
    MusicMarkerStartData *start = &stream->BaseMusicMarkerStart[jumpTo];
    if ((stream->Status == STREAM_READY || stream->Status == 8) && !stream->EndMarker.CheckMe && jumpTo > -1) {
        stream->MusicMarkerStart = start;
        if (start->IsInstant || instant) {
            SFXLoadedMarker = jumpTo;
            ES_SetFileBufferCount(stream);
            stream->ReadBufferNumber = (stream->TransferBufferNumber - 4) & FileBufferMask;
            stream->Status = STREAM_JUMPING;
            stream->StreamJumpTo = -1;
            stream->MusicMarker = &stream->BaseMusicMarker[stream->MusicMarkerStart->MarkerPos];
            stream->FilePos = WholeBlocks(stream->MusicMarkerStart->Marker.Pos);
            UpdateMusicMarkers(stream, 1);
            ES_GetStreamStates(stream);
            StreamReadData *read = &stream->Read[stream->ReadBufferNumber];
            ES_StreamAsyncReadFile(stream->FileHandle, stream->FilePos, read->ByteSize, read->DataBuffer, stream);
            stream->HasJustRequestedJump = 1;
        }
    }
    if (start->IsInstant || instant)
        stream->StreamJumpTo = -1;
}

// The music's ask for a section (SFXJumpMarker) given to the stream, unless it is the section read or playing, or
// a jump is still completing and the ask is not instant. A paused stream plays again and jumps at once.
// AUTOINJECT
void SFXJumpToMusicMarkerUpdate(void) {
    StreamData *stream = &Streams[0];
    if (!stream->MusicStream || SFXJumpMarker == -1)
        return;
    SFXLastJumpMarker = SFXJumpMarker;
    if (SFXLoadedMarker == SFXJumpMarker) {
        SFXJumpState = 3;
        return;
    }
    if (SFXActiveMarker == SFXJumpMarker) {
        SFXJumpState = 0;
        return;
    }
    if (stream->HasJustJumped && !SFXJumpInstant) {
        SFXJumpState = 1;
        return;
    }
    stream->StreamJumpTo = SFXJumpMarker;
    if (stream->Paused && !stream->GamePausedMe) {
        psiSFX_UnPause(stream->StreamHandle);
        stream->Paused = 0;
        stream->PauseNow = 0;
        ES_JumpToMusicMarker(stream, 1);
    } else {
        ES_JumpToMusicMarker(stream, SFXJumpInstant);
    }
    if (stream->StreamJumpTo > -1)
        stream->HasJustRequestedJump = 1;
}

// ---- the stream's frame

// On to the next stream asked for (inline in the original)
static void SFXInitialiseGetNextStream(void) {
    InitialiseStatus = STREAM_READY;
    StreamWaitingIntialiseMe = (StreamWaitingIntialiseMe + 1) & (SFX_NUM_STREAM_REQUESTS - 1);
}

// The next stream asked for: its stream starts afresh (keeping its buffers, voices and key-on), its marker file
// (a track's .SMF, or a streamed sound's part of STREAMS.BIN) is read, then its audio file opened, from the
// starting section for the music
// AUTOINJECT
void SFXInitialiseStreamUpdate(void) {
    if (StreamWaitingIntialiseCount == StreamWaitingIntialiseMe)
        return;

    if (InitialiseStatus == STREAM_READY) {
        StreamIntialiseDetails *waiting = &StreamWaitingIntialise[StreamWaitingIntialiseMe];
        int channel = waiting->ChannelNumber;
        InitialisingStream = &Streams[channel];
        InitialisingHashCode = waiting->HashCode;
        StreamData *stream = InitialisingStream;
        if (InitialisingHashCode >= 0)
            InitialisingHashCode = FindMusicTrackNumber(InitialisingHashCode);
        if (stream->BeenStolen) {
            stream->Is3d = waiting->Is3d;
            stream->Status = waiting->Status;
            stream->KeyOn = waiting->KeyOn;
        }

        char *dataBuffer2 = stream->DataBuffer2;
        char *dataBuffers[STREAM_READ_BUFFERS];
        for (int i = 0; i < STREAM_READ_BUFFERS; i++)
            dataBuffers[i] = stream->Read[i].DataBuffer;
        int is3d = stream->Is3d, keyOn = stream->KeyOn, reverbDepth = stream->ReverbDepth;
        int streamHandle = stream->StreamHandle, streamHandle2 = stream->StreamHandle2;
        memset(stream, 0, sizeof(StreamData));
        stream->Is3d = is3d;
        stream->KeyOn = keyOn;
        stream->ReverbDepth = reverbDepth;
        stream->DataBuffer2 = dataBuffer2;
        for (int i = 0; i < STREAM_READ_BUFFERS; i++)
            stream->Read[i].DataBuffer = dataBuffers[i];
        stream->StreamHandle = streamHandle;
        stream->StreamHandle2 = streamHandle2;
        stream->IdNumber = channel;
        stream->Status = STREAM_WAITING;
        stream->HashCode = InitialisingHashCode;
        stream->StreamJumpTo = -1;

        if (stream->HashCode < 0) {
            stream->Channels = 1;
            InitialisingHashCode = -1 - stream->HashCode;
            stream->Frequency = 22050;
            stream->MusicStream = 0;
            sprintf(StreamFileName, "%s%s/STREAMS/STREAMS.BIN", STRFilePath2, SFXLanguageName);
            sprintf(MarkerFileName, "%s%s/STREAMS/STREAMS.BIN", STRFilePath, SFXLanguageName);
        } else {
            stream->Channels = 2;
            stream->Frequency = 44100;
            stream->KeyOn = 1;
            stream->MusicStream = 1;
            sprintf(StreamFileName, "%sMFX_%d/MFX_%d.SSD", MFXFilePath2, stream->HashCode >> 4, stream->HashCode);
            sprintf(MarkerFileName, "%sMFX_%d/MFX_%d.SMF", MFXFilePath, stream->HashCode >> 4, stream->HashCode);
        }
        stream->BufferSize = stream->Channels * 0x2400;
        stream->PlayBackBufferSize = stream->Channels * 0x2400;

        if (!stream->MusicStream) {
            ES_InitialStreamBuffer();
            stream->FileHandle = psiAsyncOpenFile(MarkerFileName);
            if (stream->FileHandle == -1) {
                SFXInitialiseGetNextStream();
                return;
            }
            StreamLookupFileDetails *lookup = &StreamLookupFileData[InitialisingHashCode];
            psiAsyncReadFile(stream->FileHandle, lookup->MarkerOffset, lookup->MarkerSize, stream->DataBuffer2);
            stream->FileOffset = StreamLookupFileData[InitialisingHashCode].AudioOffset;
        } else {
            stream->StreamMarkerHeader = (StreamMarkerHeaderData *)psiSFXLoadFile(MarkerFileName);
            if (stream->StreamMarkerHeader == NULL) {
                SFXInitialiseGetNextStream();
                return;
            }
        }
        InitialiseStatus = STREAM_OPENING;
        stream->Status = STREAM_OPENING;
    }

    if (InitialiseStatus != STREAM_OPENING)
        return;
    StreamData *stream = InitialisingStream;
    if (stream->Status != STREAM_OPENING) {
        SFXInitialiseGetNextStream();
        return;
    }
    if (!stream->MusicStream) {
        if (!psiAsyncHasReadFinished(stream->FileHandle))
            return;
        stream->StreamMarkerHeader = (StreamMarkerHeaderData *)stream->DataBuffer2;
        psiAsyncCloseFile(stream->FileHandle);
    }
    stream->Read[0].ByteOffset = 0;
    stream->BaseMusicMarker =
        (MusicMarkerData *)((char *)stream->StreamMarkerHeader + stream->StreamMarkerHeader->MarkerOffset);
    stream->BaseMusicMarkerStart =
        (MusicMarkerStartData *)((char *)stream->StreamMarkerHeader + stream->StreamMarkerHeader->StartMarkerOffset);
    stream->MusicMarker = stream->BaseMusicMarker;
    stream->MusicMarkerStart = stream->BaseMusicMarkerStart;
    stream->FileHandle = psiAsyncOpenFile(StreamFileName);
    if (stream->FileHandle == -1) {
        SFXInitialiseGetNextStream();
        return;
    }
    if (stream->MusicStream) {
        SFXLoadedMarker = MusicStartSection;
        SFXActiveMarker = MusicStartSection;
        stream->MusicMarkerStart = &stream->BaseMusicMarkerStart[MusicStartSection];
        stream->MusicMarker = &stream->BaseMusicMarker[stream->MusicMarkerStart->MarkerPos];
        stream->FilePos = WholeBlocks(stream->MusicMarkerStart->Marker.Pos);
        ES_GetStreamStates(stream);
        ES_SetStreamStates(stream);
        stream->HasJustLoadedJump = 1;
        stream->HasJustRequestedLoadedJump = 0;
    } else {
        stream->FilePos = 0;
    }
    stream->BaseVolume = stream->StreamMarkerHeader->BaseVolume;
    stream->ShutDownVolume = 100;
    ES_MusicSetVolume();
    InitialisingStream->Status = STREAM_READY;
    InitialisingStream->FinishedInitialise = 1;
    SFXInitialiseGetNextStream();
}

// The copy into the playback buffer, a piece (up to a file buffer) a frame, waiting while the play position is in
// the piece. Its end is where the data ends; the last piece moves the copy on to the next read buffer. Part of
// SFXUpdateStreams in the original; the name is ours.
// Where software ADPCM would decode to: the game's four bytes. Nothing sets a stream's SoftwareAdpcm on the Xbox, so
// nothing copies from it.
// XBE_GLOBAL(0x00298a48, 0x4)
uint32_t SoftwareAdpcmBuffer;

static void ES_UpdateStreamMemCpy(void) {
    StreamMemCpyHandle *copy = &StreamMemCpyHandles[0];
    if (copy->TransferStatus != STREAM_COPYING)
        return;
    StreamData *stream = copy->Stream;
    ES_SetFileBufferCount(stream);
    int playPos = -1;
    if (stream->StartedPlaying)
        playPos = psiSFX_GetPlayPos(stream->StreamHandle);
    int size = copy->SrcByteSize - copy->TransferPos;
    if (size > 0x2400)
        size = 0x2400;
    if (playPos != -1) {
        int from = copy->StreamByteOffset + copy->TransferPos;
        if (from + size >= playPos && from <= playPos)
            return;
    }

    int transfer = stream->TransferBufferNumber;
    if (!stream->SoftwareAdpcm) {
        if (size != 0)
            psiStreamWrite(copy->StreamHandle, copy->TransferPos + copy->StreamByteOffset,
                           copy->SrcData + copy->TransferPos, size);
    } else {
        StreamReadData *read = &stream->Read[transfer];
        if (read->UseThisState) {
            stream->State[0] = read->State[0];
            stream->State[1] = read->State[1];
            read->UseThisState = 0;
        }
        // The decoder was never written for the Xbox: this copies the memory at its buffer's address
        if (size != 0)
            psiStreamWrite(copy->StreamHandle, copy->StreamByteOffset + copy->TransferPos, &SoftwareAdpcmBuffer, size);
    }
    stream->DataEndMarker.EndHere = copy->StreamByteOffset + copy->TransferPos + size;
    stream->DataEndMarker.EndMustToggle = 1;
    stream->DataEndMarker.CheckMe = 1;
    copy->TransferPos += size;
    if (copy->TransferPos == copy->SrcByteSize) {
        ES_SetStreamStates(stream);
        if (stream->SoftwareAdpcm && stream->Read[transfer].UseThisState) {
            stream->State[0] = stream->Read[transfer].State[0];
            stream->State[1] = stream->Read[transfer].State[1];
            stream->Read[transfer].UseThisState = 0;
        }
        copy->TransferStatus = 0;
        stream->TransferBufferNumber = (transfer + 1) & FileBufferMask;
    }
    if (!stream->AllowStartedPlaying)
        stream->AllowStartedPlaying = 1;
}

// A stream's read buffer copied over another, keeping the other's data buffer (inline in the original)
static void CopyReadBuffer(StreamReadData *to, const StreamReadData *from) {
    char *buffer = to->DataBuffer;
    *to = *from;
    to->DataBuffer = buffer;
}

// Once a frame: the stream's buffers, its jump and the next stream asked for; then the music stream's play
// position against its file buffers (a streamed sound that has not moved for 200 frames, or has run out, stops),
// its markers, the next read, the next copy, and a jump's read once loaded; then the copy itself
// AUTOINJECT
void SFXUpdateStreams(void) {
    ES_InitialStreamBuffer();
    SFXJumpToMusicMarkerUpdate();
    SFXInitialiseStreamUpdate();
    ES_MusicSetVolume();

    StreamData *stream = &Streams[0];
    if (!StreamIsActive(stream)) {
        ES_UpdateStreamMemCpy();
        return;
    }
    ES_SetFileBufferCount(stream);
    ES_CheckFileBuffersForJumpMarkers(stream);
    int readBuffer = stream->ReadBufferNumber;
    int transfer = stream->TransferBufferNumber;

    if (stream->StartedPlaying) {
        if (stream->LastFilePos == stream->FilePos && !stream->EndingSoon)
            stream->TimedOut++;
        else
            stream->TimedOut = 0;
        if (stream->TimedOut > 200 && !stream->MusicStream) {
            SFXStopStream(stream);
            ES_UpdateStreamMemCpy();
            return;
        }
        stream->LastPlayBackPos = stream->PlayBackPos;
        stream->PlayBackPos = psiSFX_GetPlayPos(stream->StreamHandle);
        int full = readBuffer < transfer ? stream->FileBufferCount + readBuffer : readBuffer;
        stream->LastFileBuffersFull = stream->FileBuffersFull;
        stream->FileBuffersFull = full - transfer;
        if (++stream->ShutDownCounter > 30) {
            if (transfer == readBuffer) {
                // Nothing more read: paused (the music) or stopped once the data runs out
                if (ES_CheckEndMarker(stream, &stream->DataEndMarker)) {
                    if (!stream->MusicStream) {
                        SFXStopStream(stream);
                        ES_UpdateStreamMemCpy();
                        return;
                    }
                    psiSamplePause(stream->StreamHandle);
                    stream->ShutDown = 1;
                }
            } else if (!stream->GamePausedMe && stream->ShutDown) {
                psiSFX_UnPause(stream->StreamHandle);
                stream->ShutDown = 0;
            }
        }
    }

    if (stream->CanJumpMarker.CheckMe && stream->SetCanJumpMarker &&
        ES_CheckEndMarker(stream, &stream->CanJumpMarker)) {
        stream->HasJustJumped = 0;
        stream->SetCanJumpMarker = 0;
    }
    int loaded = SFXLoadedMarker;
    bool checkTimer = true;
    if (stream->JumpRequestCompleteMarker.CheckMe) {
        if (!stream->SetJumpRequestCompleteMarker) {
            checkTimer = false;
        } else if (ES_CheckEndMarker(stream, &stream->JumpRequestCompleteMarker)) {
            SFXActiveMarker = loaded;
            stream->HasJustJumped = stream->HasJustRequestedJump;
            stream->HasJustRequestedJump = 0;
            stream->SetJumpRequestCompleteMarker = 0;
        }
    }
    // Or after 240 frames
    if (checkTimer && stream->SetJumpRequestCompleteMarker && stream->SetJumpRequestCompleteTimer-- == 1) {
        SFXActiveMarker = loaded;
        stream->HasJustJumped = stream->HasJustRequestedJump;
        stream->HasJustRequestedJump = 0;
        stream->SetJumpRequestCompleteMarker = 0;
    }

    if (stream->EndMarker.CheckMe && stream->SetEndMarker && ES_CheckEndMarker(stream, &stream->EndMarker)) {
        if (stream->PauseNow) {
            psiSamplePause(stream->StreamHandle);
            stream->Paused = 1;
            stream->PauseNow = 0;
        } else {
            SFXStopStream(stream);
        }
    } else {
        int status = stream->Status;
        if (status == STREAM_READY && !SFXFileAccessSuspended) {
            UpdateMusicMarkers(stream, 0);
            StreamReadData *read = &stream->Read[readBuffer];
            if (!read->NoDataToRead) {
                if (read->ByteSize != 0) {
                    if (read->SetToBlank)
                        psiStreamClear(read->ByteSize, read->DataBuffer);
                    else
                        ES_StreamAsyncReadFile(stream->FileHandle, stream->FilePos, read->ByteSize, read->DataBuffer,
                                               stream);
                }
                stream->Status = STREAM_READING;
            }
            status = stream->Status;
        }
        if (status == STREAM_READING) {
            if (psiAsyncHasReadFinished(stream->FileHandle) || stream->Read[readBuffer].ByteSize == 0) {
                ES_HandleStreamBufferLoaded(stream);
                readBuffer = stream->ReadBufferNumber;
                stream->Status = STREAM_READY;
            }
            status = stream->Status;
        }

        StreamMemCpyHandle *copy = &StreamMemCpyHandles[stream->IdNumber];
        if (copy->TransferStatus != STREAM_COPYING && status != STREAM_JUMPING && status != STREAM_JUMP_COPYING &&
            !(stream->AllowStartedPlaying && !stream->StartedPlaying)) {
            // The next read buffer to the playback buffer, and the markers in the one before it
            if (transfer != readBuffer) {
                StreamReadData *read = &stream->Read[transfer];
                ES_StartStreamMemCpy(stream->StreamHandle, read->ByteOffset, read->DataBuffer, read->ByteSize, stream);
            }
            StreamReadData *last = &stream->Read[(transfer - 1) & FileBufferMask];
            if (!stream->EndMarker.CheckMe && last->EndMarker.CheckMe) {
                stream->SetEndMarker = 1;
                stream->EndMarker = last->EndMarker;
            }
            if (!stream->CanJumpMarker.CheckMe && last->CanJumpMarker.CheckMe) {
                stream->SetCanJumpMarker = 1;
                stream->CanJumpMarker = last->CanJumpMarker;
            }
            if (!stream->JumpRequestCompleteMarker.CheckMe && last->JumpRequestCompleteMarker.CheckMe) {
                stream->SetJumpRequestCompleteTimer = 240;
                stream->SetJumpRequestCompleteMarker = 1;
                stream->JumpRequestCompleteMarker = last->JumpRequestCompleteMarker;
            }
        }

        if (status == STREAM_JUMPING) {
            if (!psiAsyncHasReadFinished(stream->FileHandle))
                goto keyOn;
            // The jump's section read: it goes into the playback buffer a little ahead of the play position
            stream->HasJustLoadedJump = 1;
            stream->HasJustRequestedLoadedJump = 0;
            int offset = WholeBlocks(stream->PlayBackPos / stream->Channels * 2 + 0x4000);
            if (offset >= stream->PlayBackBufferSize)
                offset -= stream->PlayBackBufferSize;
            StreamReadData *read = &stream->Read[readBuffer];
            read->ByteOffset = offset;
            StreamReadData *next = &stream->Read[(readBuffer + 1) & FileBufferMask];
            next->ByteSize = 0;
            if (read->ByteSize + offset > stream->PlayBackBufferSize) {
                // Split where the playback buffer wraps
                CopyReadBuffer(next, read);
                next->ByteSize = read->ByteSize - stream->PlayBackBufferSize + offset;
                next->ByteOffset = 0;
                read->ByteSize = read->ByteSize - next->ByteSize;
            }
            stream->TransferBufferNumber = readBuffer;
            StreamMemCpyHandles[stream->IdNumber].TransferStatus = 0;
            ES_SetStreamStates(stream);
            ES_StartStreamMemCpy(stream->StreamHandle, read->ByteOffset, read->DataBuffer, read->ByteSize, stream);
            stream->Status = STREAM_JUMP_COPYING;
            status = STREAM_JUMP_COPYING;
        }

        if (status == STREAM_JUMP_COPYING && StreamMemCpyHandles[stream->IdNumber].TransferStatus != STREAM_COPYING) {
            // Its first part copied: the part past the wrap next
            int nextBuffer = (readBuffer + 1) & FileBufferMask;
            stream->ReadBufferNumber = nextBuffer;
            StreamReadData *next = &stream->Read[nextBuffer];
            next->GetState[0] = 0;
            next->GetState[1] = 0;
            next->StateSet = 0;
            next->UseThisState = 0;
            if (next->ByteSize != 0) {
                int copied = stream->Read[readBuffer].ByteSize;
                if (stream->SoftwareAdpcm)
                    copied /= 4;
                ES_StartStreamMemCpy(stream->StreamHandle, next->ByteOffset,
                                     stream->Read[readBuffer].DataBuffer + copied, next->ByteSize, stream);
            } else {
                CopyReadBuffer(next, &stream->Read[readBuffer]);
            }
            SFXActiveMarker = SFXLoadedMarker;
            stream->HasJustJumped = stream->HasJustRequestedJump;
            stream->HasJustRequestedJump = 0;
            stream->Status = STREAM_READY;
            ES_HandleStreamBufferLoaded(stream);
            stream->SetJumpRequestCompleteTimer = 240;
        }
    }

keyOn:
    if (stream->AllowStartedPlaying && !stream->StartedPlaying && stream->KeyOn) {
        psiStreamPlay(stream->StreamHandle, stream->ReverbDepth);
        stream->StartedPlaying = 1;
    }
    ES_UpdateStreamMemCpy();
}
