#include "Fmv.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <deque>

#include "../actionhelpers.h"
#include "../memory.h"
#include "../game.h"
#include "Direct3D/d3dSeam.h"
#include "Direct3D/GraphicsSystem.h"
#include "../sound/xaudio2Backend.h"
#include "../sound/dsndSeam.h"
#include "../../common/gfx/d3d9Backend.h"
#include "../../common/xboxPath.h"
#include "../../common/fmv/Ffmpeg.h"
#include "../../common/fmv/FmvAudio.h"

// ---------------------------------------------------------------------------------------------------------------
// FMV playback through FFmpeg.
//
// The game plays its movies (the attract loop, the menu backdrops, the mission briefings) through five functions
// of its own, all still original, over Microsoft's XMV decoder library: 160 KB of WMV2 decoding with hand-written
// MMX, the last XDK library the game ran. These are the five functions, so the library is no longer reached at all.
//
// The movies are WMV2 video (640x480, 25 fps) with Xbox IMA ADPCM audio - one stereo track, or four (one per
// language). FFmpeg's xmv demuxer and wmv2/adpcm_ima_xbox decoders read them exactly. FFmpeg is a minimal LGPL
// build (third_party/ffmpeg-prebuilt), loaded at startup (Fmv_Init, through common/fmv/Ffmpeg.cpp).
//
// What the game expects of the five, from the originals (0x000e8a00-0x000e8cf0):
//   - BackgroundMoviePlayFile(name): opens d:\eurocom\25_fps\<name> (30_fps on a PAL-60 TV), insists on 640x480,
//     picks the audio track by language (2 -> 1, 3 -> 2, 6 -> 3, else 0, and 0 if there are fewer), decodes the
//     first frame before returning, and plays the audio from there.
//   - maybeDecodeMpgAudio(), every frame: decodes the next frame if one is due and draws the current one as a
//     640x480 quad from a YUY2 texture (double-buffered); false once the movie has ended.
//   - maybeBackgroundMovieIsPlaying(): whether it has not ended yet.
//   - BackgroundMovieSetVolume(0..100): through the game's volume table.
//   - maybeBackgroundMovieCleanup(): stops and frees everything; called freely, also with nothing playing.
// The textures stay the game's kind (RegisterTexture format 9, Xbox YUY2, in memory from the game's allocator), so
// the backend's existing YUY2 upload and the drawing calls are exactly the original's.
//
// Timing follows the audio: a frame is due once the samples played reach its timestamp (the wall clock stands in
// when a movie has no audio). The original decoder did the same against its DirectSound stream.
// ---------------------------------------------------------------------------------------------------------------

#define FMV_WIDTH            640
#define FMV_HEIGHT           480
#define FMV_FRAME_BYTES      0x12c000        // what the original allocates per frame (twice a YUY2 frame)
#define FMV_FRAME_ALIGN      0x80
#define FMV_FORMAT_YUY2      9               // RegisterTexture's format type for Xbox YUY2

// ---------------------------------------------------------------------------------------------------------------
// The player
// ---------------------------------------------------------------------------------------------------------------

static struct {
    AVFormatContext *format;
    AVCodecContext *video;
    AVCodecContext *audio;
    int videoStream, audioStream;
    AVPacket *packet;
    AVFrame *frame;
    std::deque<AVPacket *> videoPackets;     // read ahead of the video while keeping the audio fed
    bool demuxEnded;                         // no more packets in the file
    bool videoEnded;                         // and the video decoder has given up its last frame

    void *frameMemory[2];
    int texture[2];
    int displayIndex;                        // the texture drawn; the other is decoded into
    bool haveFrame;
    double nextFrameTime;                    // seconds: the timestamp of the next frame to show
    double timeBase;                         // seconds per video timestamp unit

    FmvAudio audio_;                         // the voice and the clock (common/fmv/FmvAudio.cpp)

    bool isPlaying;
} M;

// The movie volume, 0..100, kept across movies (only the original BackgroundMovieSetVolume and
// BackgroundMoviePlayFile used it). The originals' own state, the BackgroundMovie block, is replaced by M.
// XBE_GLOBAL(0x001b52e0, 0x4)
static int MovieVolume = 100;

// Declared for the generated stub in game.cpp
int Language_Get(void);

static void ApplyVolume(void) {
    int32_t millibels = DSound_VolumeMillibels(MovieVolume);
    M.audio_.SetVolume(millibels <= -10000 ? 0.0f : powf(10.0f, (float)millibels / 2000.0f));
}

static void DecodeAudioPacket(AVPacket *packet) {
    if (ff.send_packet(M.audio, packet) < 0)
        return;
    while (ff.receive_frame(M.audio, M.frame) == 0)
        M.audio_.Queue(M.frame);
}

// Reads one packet: audio goes straight to the voice, video is kept for when a frame is due. False at the end.
static bool ReadPacket(void) {
    if (M.demuxEnded)
        return false;
    if (ff.read_frame(M.format, M.packet) < 0) {
        M.demuxEnded = true;
        if (M.audio != NULL)
            DecodeAudioPacket(NULL);       // drain
        M.audio_.Finish();
        return false;
    }
    if (M.packet->stream_index == M.videoStream) {
        AVPacket *keep = ff.packet_alloc();
        *keep = *M.packet;                 // takes over the reference
        memset(M.packet, 0, sizeof(*M.packet));
        M.videoPackets.push_back(keep);
        return true;
    }
    if (M.audio != NULL && M.packet->stream_index == M.audioStream)
        DecodeAudioPacket(M.packet);
    ff.packet_unref(M.packet);
    return true;
}

static void KeepAudioFed(void) {
    while (M.audio_.WantsMore() && ReadPacket()) {
    }
}

// 4:2:0 planar to the game's YUY2 (Y0 U Y1 V), straight into the texture's memory
static void ConvertFrame(const AVFrame *frame, uint8_t *out) {
    for (int y = 0; y < FMV_HEIGHT; y++) {
        const uint8_t *Y = frame->data[0] + y * frame->linesize[0];
        const uint8_t *U = frame->data[1] + (y >> 1) * frame->linesize[1];
        const uint8_t *V = frame->data[2] + (y >> 1) * frame->linesize[2];
        uint8_t *row = out + y * FMV_WIDTH * 2;
        for (int x = 0; x < FMV_WIDTH; x += 2) {
            row[x * 2 + 0] = Y[x];
            row[x * 2 + 1] = U[x >> 1];
            row[x * 2 + 2] = Y[x + 1];
            row[x * 2 + 3] = V[x >> 1];
        }
    }
}

// Decodes the next video frame into M.frame. False when there are no more.
static bool DecodeVideoFrame(void) {
    for (;;) {
        int r = ff.receive_frame(M.video, M.frame);
        if (r == 0)
            return true;
        if (r == AVERROR_EOF) {
            M.videoEnded = true;
            return false;
        }
        // needs input
        while (M.videoPackets.empty() && ReadPacket()) {
        }
        if (M.videoPackets.empty()) {
            ff.send_packet(M.video, NULL);     // drain what is left
            continue;
        }
        AVPacket *p = M.videoPackets.front();
        M.videoPackets.pop_front();
        ff.send_packet(M.video, p);
        ff.packet_free(&p);
    }
}

// Shows a decoded frame: into the texture not being drawn, which becomes the one drawn
static void PresentFrame(void) {
    int back = M.haveFrame ? 1 - M.displayIndex : M.displayIndex;
    ConvertFrame(M.frame, (uint8_t *)M.frameMemory[back]);
    D3D9_NotifyTextureModified(&Gfx.textures[M.texture[back]]);
    M.displayIndex = back;
    M.haveFrame = true;
    int64_t pts = M.frame->best_effort_timestamp;
    double t = pts == AV_NOPTS_VALUE ? M.nextFrameTime : pts * M.timeBase;
    double duration = M.frame->duration > 0 ? M.frame->duration * M.timeBase : 1.0 / 25.0;
    M.nextFrameTime = t + duration;
}

static bool OpenDecoder(int stream, AVCodecContext **out) {
    AVCodecParameters *params = M.format->streams[stream]->codecpar;
    const AVCodec *codec = ff.find_decoder(params->codec_id);
    if (codec == NULL)
        return false;
    AVCodecContext *ctx = ff.alloc_context3(codec);
    if (ctx == NULL || ff.parameters_to_context(ctx, params) < 0 || ff.open2(ctx, codec, NULL) < 0) {
        ff.free_context(&ctx);
        return false;
    }
    *out = ctx;
    return true;
}

// AUTOINJECT
void maybeBackgroundMovieCleanup(void) {
    M.audio_.Close();
    for (AVPacket *p : M.videoPackets)
        ff.packet_free(&p);
    M.videoPackets.clear();
    if (M.video != NULL) ff.free_context(&M.video);
    if (M.audio != NULL) ff.free_context(&M.audio);
    if (M.format != NULL) ff.close_input(&M.format);
    if (M.packet != NULL) ff.packet_free(&M.packet);
    if (M.frame != NULL) ff.frame_free(&M.frame);
    for (int i = 0; i < 2; i++) {
        if (M.texture[i] != 0)
            ReleaseTexture(M.texture[i]);
        if (M.frameMemory[i] != NULL)
            Mem_Free(&M.frameMemory[i]);
        M.texture[i] = 0;
        M.frameMemory[i] = NULL;
    }
    M.audioStream = M.videoStream = -1;
    M.demuxEnded = M.videoEnded = M.haveFrame = M.isPlaying = false;
}

// AUTOINJECT
void BackgroundMoviePlayFile(char *filename) {
    maybeBackgroundMovieCleanup();

    for (int i = 0; i < 2; i++) {
        M.frameMemory[i] = Mem_Malloc(FMV_FRAME_BYTES, (MallocFlags)0x1204, FMV_FRAME_ALIGN);
        M.texture[i] = M.frameMemory[i] ? RegisterTexture(FMV_WIDTH, FMV_HEIGHT, FMV_FORMAT_YUY2, 1, M.frameMemory[i], 0) : 0;
        if (M.frameMemory[i] == NULL || M.texture[i] == 0) {
            maybeBackgroundMovieCleanup();
            return;
        }
        memset(M.frameMemory[i], 0, FMV_FRAME_BYTES);
    }

    char xboxPath[300], hostPath[MAX_PATH];
    snprintf(xboxPath, sizeof(xboxPath), "d:\\eurocom\\%s%s", Graphics_IsPalI() ? "30_fps\\" : "25_fps\\", filename);
    if (!Xbox_ResolvePath(xboxPath, hostPath, sizeof(hostPath)) || ff.open_input(&M.format, hostPath, NULL, NULL) < 0) {
        printf("[fmv] cannot open %s\n", xboxPath);
        maybeBackgroundMovieCleanup();
        return;
    }
    ff.find_stream_info(M.format, NULL);

    int audioTracks = 0, wantTrack;
    switch (Language_Get()) {
        case 2: wantTrack = 1; break;
        case 3: wantTrack = 2; break;
        case 6: wantTrack = 3; break;
        default: wantTrack = 0; break;
    }
    M.videoStream = M.audioStream = -1;
    int firstAudio = -1;
    for (unsigned i = 0; i < M.format->nb_streams; i++) {
        AVCodecParameters *p = M.format->streams[i]->codecpar;
        if (p->codec_type == AVMEDIA_TYPE_VIDEO && M.videoStream < 0)
            M.videoStream = (int)i;
        if (p->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (firstAudio < 0)
                firstAudio = (int)i;
            if (audioTracks == wantTrack)
                M.audioStream = (int)i;
            audioTracks++;
        }
    }
    if (M.audioStream < 0)
        M.audioStream = firstAudio;      // fewer tracks than the language wants: the first

    AVCodecParameters *vp = M.videoStream >= 0 ? M.format->streams[M.videoStream]->codecpar : NULL;
    if (vp == NULL || vp->width != FMV_WIDTH || vp->height != FMV_HEIGHT || !OpenDecoder(M.videoStream, &M.video)) {
        printf("[fmv] %s: no 640x480 video stream it can decode\n", xboxPath);
        maybeBackgroundMovieCleanup();
        return;
    }
    AVRational tb = M.format->streams[M.videoStream]->time_base;
    M.timeBase = (double)tb.num / tb.den;
    M.packet = ff.packet_alloc();
    M.frame = ff.frame_alloc();

    if (M.audioStream >= 0 && OpenDecoder(M.audioStream, &M.audio)) {
        if (M.audio_.Open(XA2_GetDevice(), M.audio->ch_layout.nb_channels, M.audio->sample_rate, NULL, 0))
            ApplyVolume();
        else
            ff.free_context(&M.audio);
    }
    if (M.audio == NULL)
        M.audio_.Finish();                 // no audio: the clock is the wall clock

    // The first frame before returning, as the original does; the audio starts with it
    M.nextFrameTime = 0.0;
    KeepAudioFed();
    if (!DecodeVideoFrame()) {
        maybeBackgroundMovieCleanup();
        return;
    }
    PresentFrame();
    M.audio_.Start();
    M.isPlaying = true;
}

// AUTOINJECT
bool __stdcall maybeDecodeMpgAudio(void) {
    if (M.format == NULL)
        return false;

    KeepAudioFed();
    // Every frame now due is decoded; only the last of them is shown
    double now = M.audio_.Clock();
    bool due = false;
    while (!M.videoEnded && now >= M.nextFrameTime && DecodeVideoFrame()) {
        due = true;
        int64_t pts = M.frame->best_effort_timestamp;
        double duration = M.frame->duration > 0 ? M.frame->duration * M.timeBase : 1.0 / 25.0;
        double t = pts == AV_NOPTS_VALUE ? M.nextFrameTime : pts * M.timeBase;
        if (now < t + duration)
            break;                         // this one is current
        M.nextFrameTime = t + duration;    // already past: skip it
    }
    if (due && M.frame->data[0] != NULL)
        PresentFrame();

    // The draw, exactly the original's
    maybeResetRenderState(1);
    d3dSetYuvEnable(1);
    d3dSetDeferredTextureState(0, 0);
    d3dSetTextureStage0(M.texture[M.displayIndex]);
    uint32_t tint = 0xff808080u;
    float tintBits;
    memcpy(&tintBits, &tint, sizeof(tintBits));
    maybeImmediateModePushItem(0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 0.0f, 640.0f, 480.0f, tintBits);
    maybeImmediateModeFlush();
    d3dSetTextureStage0(0);
    d3dSetDeferredTextureState(1, 1);
    d3dSetYuvEnable(0);

    // Ended once the last frame has been shown and the audio has played out
    if (M.videoEnded && M.audio_.PlayedOut())
        M.isPlaying = false;
    return M.isPlaying;
}

// AUTOINJECT
bool __stdcall maybeBackgroundMovieIsPlaying(void) {
    return M.isPlaying;
}

// AUTOINJECT
void BackgroundMovieSetVolume(int volume) {
    MovieVolume = volume;
    ApplyVolume();
}

void Fmv_Init(void) {
    Ffmpeg_Require();
}
