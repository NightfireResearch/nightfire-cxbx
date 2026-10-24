// The PS2's movies (docs/ps2-data.md): Fmv.h's five functions over MOVIES\25_FPS\<LANGUAGE>\<hash>.PSS, in place of
// engine/Fmv.cpp's XMV player. Built only with NIGHTFIRE_ACTION_DATA_PS2, with the FFmpeg that has the MPEG program
// stream demuxer and the MPEG-2 decoder (third_party/ffmpeg-prebuilt-ps2).
//
// A PSS file is an MPEG-2 program stream: MPEG-2 video (512 wide, as the PS2 draws it), and the sound in private stream
// 1, which FFmpeg passes by. That is read here: each packet's payload after 4 bytes of its own, together an "SShd" header
// (format, rate, channels, interleave) and "SSbd" data: 16-bit PCM, the channels in turns of `interleave` bytes. The
// picture is scaled to the 640x480 YUY2 texture the game draws, and the timing follows the sound, as Fmv.cpp's does.

#include "../engine/Fmv.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <deque>
#include <vector>

#include "../actionhelpers.h"
#include "../memory.h"
#include "../game.h"
#include "../engine/Direct3D/d3dSeam.h"
#include "../engine/Direct3D/GraphicsSystem.h"
#include "../sound/xaudio2Backend.h"
#include "../sound/dsndSeam.h"
#include "../sound/SFX.h"                   // SFXLanguageName
#include "../../common/gfx/d3d9Backend.h"
#include "../../common/xboxPath.h"
#include "../../common/fmv/Ffmpeg.h"
#include "../../common/fmv/FmvAudio.h"

#define FMV_WIDTH            640
#define FMV_HEIGHT           480
#define FMV_FRAME_BYTES      0x12c000        // as Fmv.cpp allocates (twice a YUY2 frame)
#define FMV_FRAME_ALIGN      0x80

#define PSS_PRIVATE_STREAM_1 0xbd
#define PSS_SOUND_PCM16      1                // SShd's format: 16-bit little-endian PCM
#define PSS_SOUND_CHUNK      4096             // frames queued at a time

static struct {
    AVFormatContext *format;
    AVCodecContext *video;
    int videoStream;
    AVPacket *packet;
    AVFrame *frame;
    bool demuxEnded, videoEnded;

    void *frameMemory[2];
    int texture[2];
    int displayIndex;
    bool haveFrame;
    double nextFrameTime;
    double timeBase;

    std::vector<int16_t> sound;     // the whole track, interleaved
    int soundChannels;
    size_t soundAt;                 // frames queued so far
    FmvAudio audio_;

    bool isPlaying;
} M;

static int MovieVolume = 100;      // as Fmv.cpp's

static void ApplyVolume(void) {
    int32_t millibels = DSound_VolumeMillibels(MovieVolume);
    M.audio_.SetVolume(millibels <= -10000 ? 0.0f : powf(10.0f, (float)millibels / 2000.0f));
}

// ---- the sound: private stream 1 out of the program stream

static uint32_t Le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// The sound of a PSS file read whole: false if it has none this can play
static bool ReadPssSound(const std::vector<uint8_t> &file, std::vector<int16_t> *pcm, int *channels, int *rate) {
    std::vector<uint8_t> stream;
    size_t p = 0;
    while (p + 6 <= file.size()) {
        if (file[p] != 0 || file[p + 1] != 0 || file[p + 2] != 1) {
            p++;
            continue;
        }
        uint8_t id = file[p + 3];
        if (id == 0xba) {                           // a pack header (MPEG-2): 14 bytes and its stuffing
            if (p + 14 > file.size())
                break;
            p += 14 + (file[p + 13] & 7);
            continue;
        }
        if (id == 0xb9)                             // the end
            break;
        size_t length = (size_t)file[p + 4] << 8 | file[p + 5];
        if (id == PSS_PRIVATE_STREAM_1 && p + 9 <= file.size()) {
            size_t payload = p + 9 + file[p + 8] + 4;   // after the PES header and the payload's own 4 bytes
            size_t end = p + 6 + length;
            if (end <= file.size() && payload < end)
                stream.insert(stream.end(), file.begin() + payload, file.begin() + end);
        }
        p += 6 + length;
    }
    if (stream.size() < 8 || memcmp(stream.data(), "SShd", 4) != 0)
        return false;
    uint32_t headerBytes = Le32(&stream[4]);
    if (8 + headerBytes + 8 > stream.size())
        return false;
    const uint8_t *h = &stream[8];
    uint32_t format = Le32(h), hz = Le32(h + 4), count = Le32(h + 8), interleave = Le32(h + 12);
    const uint8_t *body = h + headerBytes;
    if (memcmp(body, "SSbd", 4) != 0 || format != PSS_SOUND_PCM16 || count < 1 || count > 2 || interleave == 0 ||
        interleave % 2 != 0)
        return false;
    size_t bytes = stream.size() - (size_t)(body + 8 - stream.data());
    const uint8_t *data = body + 8;
    size_t turn = (size_t)interleave * count;
    size_t frames = bytes / turn * (interleave / 2);
    pcm->resize(frames * count);
    for (size_t t = 0; t < bytes / turn; t++) {
        for (uint32_t c = 0; c < count; c++) {
            const uint8_t *in = data + t * turn + c * interleave;
            for (uint32_t i = 0; i < interleave / 2; i++)
                (*pcm)[(t * (interleave / 2) + i) * count + c] = (int16_t)(in[i * 2] | in[i * 2 + 1] << 8);
        }
    }
    *channels = (int)count;
    *rate = (int)hz;
    return true;
}

static void KeepAudioFed(void) {
    size_t frames = M.sound.size() / (M.soundChannels > 0 ? M.soundChannels : 1);
    while (M.audio_.IsOpen() && M.audio_.WantsMore() && M.soundAt < frames) {
        size_t n = frames - M.soundAt < PSS_SOUND_CHUNK ? frames - M.soundAt : PSS_SOUND_CHUNK;
        AVFrame chunk;
        memset(&chunk, 0, sizeof(chunk));
        uint8_t *planes[1] = { (uint8_t *)&M.sound[M.soundAt * M.soundChannels] };
        chunk.format = AV_SAMPLE_FMT_S16;
        chunk.nb_samples = (int)n;
        chunk.ch_layout.nb_channels = M.soundChannels;
        chunk.extended_data = planes;
        M.audio_.Queue(&chunk);
        M.soundAt += n;
        if (M.soundAt == frames)
            M.audio_.Finish();
    }
}

// ---- the picture

static bool ReadPacket(void) {
    if (M.demuxEnded)
        return false;
    for (;;) {
        if (ff.read_frame(M.format, M.packet) < 0) {
            M.demuxEnded = true;
            return false;
        }
        if (M.packet->stream_index == M.videoStream)
            return true;
        ff.packet_unref(M.packet);
    }
}

static bool DecodeVideoFrame(void) {
    for (;;) {
        int r = ff.receive_frame(M.video, M.frame);
        if (r == 0)
            return true;
        if (r == AVERROR_EOF) {
            M.videoEnded = true;
            return false;
        }
        if (!ReadPacket()) {
            ff.send_packet(M.video, NULL);
            continue;
        }
        ff.send_packet(M.video, M.packet);
        ff.packet_unref(M.packet);
    }
}

// 4:2:0 planar of any size to the game's 640x480 YUY2, each texel the nearest of the movie's
static void ConvertFrame(const AVFrame *frame, uint8_t *out) {
    int width = frame->width, height = frame->height;
    for (int y = 0; y < FMV_HEIGHT; y++) {
        int sy = y * height / FMV_HEIGHT;
        const uint8_t *Y = frame->data[0] + sy * frame->linesize[0];
        const uint8_t *U = frame->data[1] + (sy >> 1) * frame->linesize[1];
        const uint8_t *V = frame->data[2] + (sy >> 1) * frame->linesize[2];
        uint8_t *row = out + y * FMV_WIDTH * 2;
        for (int x = 0; x < FMV_WIDTH; x += 2) {
            int sx0 = x * width / FMV_WIDTH, sx1 = (x + 1) * width / FMV_WIDTH;
            row[x * 2 + 0] = Y[sx0];
            row[x * 2 + 1] = U[sx0 >> 1];
            row[x * 2 + 2] = Y[sx1];
            row[x * 2 + 3] = V[sx0 >> 1];
        }
    }
}

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

// ---- the five

void BackgroundMovieCleanup(void) {
    M.audio_.Close();
    if (M.video != NULL) ff.free_context(&M.video);
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
    std::vector<int16_t>().swap(M.sound);
    M.soundAt = 0;
    M.videoStream = -1;
    M.demuxEnded = M.videoEnded = M.haveFrame = M.isPlaying = false;
}

// The movie's file on the PS2's disc: <hash>.PSS in the language's folder, else the English one
static bool FindMovie(const char *filename, char *hostPath, size_t size) {
    char name[64];
    size_t n = 0;
    for (; filename[n] != '\0' && filename[n] != '.' && n + 1 < sizeof(name); n++)
        name[n] = (char)toupper((unsigned char)filename[n]);
    name[n] = '\0';
    const char *languages[] = { SFXLanguageName, "ENGLISH" };
    for (const char *language : languages) {
        char discPath[200];
        snprintf(discPath, sizeof(discPath), "d:\\MOVIES\\25_FPS\\%s\\%s.PSS", language, name);
        if (Xbox_ResolvePath(discPath, hostPath, size)) {
            FILE *f = fopen(hostPath, "rb");
            if (f != NULL) {
                fclose(f);
                return true;
            }
        }
    }
    return false;
}

void BackgroundMoviePlayFile(char *filename) {
    BackgroundMovieCleanup();

    char hostPath[300];
    if (!FindMovie(filename, hostPath, sizeof(hostPath))) {
        printf("[fmv] no PSS for %s\n", filename);
        return;
    }
    for (int i = 0; i < 2; i++) {
        M.frameMemory[i] = Mem_Malloc(FMV_FRAME_BYTES, MALLOC_XBOX, FMV_FRAME_ALIGN);
        M.texture[i] = M.frameMemory[i] ? RegisterTexture(FMV_WIDTH, FMV_HEIGHT, TEXFMT_YUY2, 1, M.frameMemory[i], 0)
                                        : 0;
        if (M.frameMemory[i] == NULL || M.texture[i] == 0) {
            BackgroundMovieCleanup();
            return;
        }
        memset(M.frameMemory[i], 0, FMV_FRAME_BYTES);
    }
    if (ff.open_input(&M.format, hostPath, NULL, NULL) < 0) {
        printf("[fmv] cannot open %s\n", hostPath);
        BackgroundMovieCleanup();
        return;
    }
    ff.find_stream_info(M.format, NULL);
    M.videoStream = -1;
    for (unsigned i = 0; i < M.format->nb_streams; i++) {
        if (M.format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && M.videoStream < 0)
            M.videoStream = (int)i;
    }
    const AVCodec *codec = M.videoStream >= 0 ? ff.find_decoder(M.format->streams[M.videoStream]->codecpar->codec_id)
                                              : NULL;
    if (codec != NULL) {
        M.video = ff.alloc_context3(codec);
        if (M.video == NULL || ff.parameters_to_context(M.video, M.format->streams[M.videoStream]->codecpar) < 0 ||
            ff.open2(M.video, codec, NULL) < 0)
            ff.free_context(&M.video);
    }
    if (M.video == NULL) {
        printf("[fmv] %s: no video stream it can decode\n", hostPath);
        BackgroundMovieCleanup();
        return;
    }
    AVRational tb = M.format->streams[M.videoStream]->time_base;
    M.timeBase = (double)tb.num / tb.den;
    M.packet = ff.packet_alloc();
    M.frame = ff.frame_alloc();

    // The sound, from the file read whole
    std::vector<uint8_t> file;
    if (FILE *f = fopen(hostPath, "rb")) {
        fseek(f, 0, SEEK_END);
        file.resize((size_t)ftell(f));
        fseek(f, 0, SEEK_SET);
        if (fread(file.data(), 1, file.size(), f) != file.size())
            file.clear();
        fclose(f);
    }
    int rate = 0;
    if (ReadPssSound(file, &M.sound, &M.soundChannels, &rate) &&
        M.audio_.Open(XA2_GetDevice(), M.soundChannels, rate, NULL, 0))
        ApplyVolume();
    else
        M.audio_.Finish();                 // no sound: the clock is the wall clock

    M.nextFrameTime = 0.0;
    KeepAudioFed();
    if (!DecodeVideoFrame()) {
        BackgroundMovieCleanup();
        return;
    }
    PresentFrame();
    M.audio_.Start();
    M.isPlaying = true;
}

bool __stdcall BackgroundMovieUpdate(void) {
    if (M.format == NULL)
        return false;

    KeepAudioFed();
    double now = M.audio_.Clock();
    bool due = false;
    while (!M.videoEnded && now >= M.nextFrameTime && DecodeVideoFrame()) {
        due = true;
        int64_t pts = M.frame->best_effort_timestamp;
        double duration = M.frame->duration > 0 ? M.frame->duration * M.timeBase : 1.0 / 25.0;
        double t = pts == AV_NOPTS_VALUE ? M.nextFrameTime : pts * M.timeBase;
        if (now < t + duration)
            break;
        M.nextFrameTime = t + duration;
    }
    if (due && M.frame->data[0] != NULL)
        PresentFrame();

    // The draw, as Fmv.cpp's (the original's)
    resetRenderState(1);
    d3dSetYuvEnable(1);
    d3dSetDeferredTextureState(0, 0);
    d3dSetTextureStage0(M.texture[M.displayIndex]);
    uint32_t tint = 0xff808080u;
    float tintBits;
    memcpy(&tintBits, &tint, sizeof(tintBits));
    immediateModePushItem(0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 0.0f, 640.0f, 480.0f, tintBits);
    immediateModeFlush();
    d3dSetTextureStage0(0);
    d3dSetDeferredTextureState(1, 1);
    d3dSetYuvEnable(0);

    if (M.videoEnded && M.audio_.PlayedOut())
        M.isPlaying = false;
    return M.isPlaying;
}

bool __stdcall BackgroundMovieIsPlaying(void) {
    return M.isPlaying;
}

void BackgroundMovieSetVolume(int volume) {
    MovieVolume = volume;
    ApplyVolume();
}

void Fmv_Init(void) {
    Ffmpeg_Require();
}
