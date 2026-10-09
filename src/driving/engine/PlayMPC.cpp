#include "PlayMPC.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>

#include "../../common/fmv/Ffmpeg.h"
#include "../../common/fmv/FmvAudio.h"
#include "../../common/gfx/d3d9Backend.h"
#include "../../common/xboxPath.h"
#include "../eagl/RenderContext.h"
#include "../eagl/View.h"
#include "../platform/Pad.hpp"
#include "../sound/xaudio2Driving.h"

// ---------------------------------------------------------------------------------------------------------------
// The driving engine's movies, on FFmpeg - the counterpart of the action engine's engine/Fmv.cpp.
//
// The level intros and outros and the scripted movies are EA Madcow files (12 movies in four languages) inside
// driving\misc.viv, an EA BIG archive: MAD video at 640x480 and 25 fps, and 5.1 EA-XA audio at 48 kHz, in EA's
// chunked format. The original played them through EA's own player library (RCMP, its MAD codec and an MMX colour
// converter) over EA's file and sound-stream layers; PlayMPC's Init and Play, replaced here, are the only way in
// to all of that, so none of it is reached any more. docs/driving-fmv.md maps the original.
//
// FFmpeg's EA demuxer refuses six-channel audio, so the chunks are walked here and only the decoders are FFmpeg's:
// eamad for the MADk/MADm/MADe chunks (the whole chunk is the packet) and adpcm_ea_r1 for the SCDl chunks (the
// chunk after its 8-byte header), set up from the SCHl header.
//
// What stays the original's is the frame around the picture: the render context's BeginFrame/EndFrame, the
// viewport's BeginView/ClearViewPort/EndView, and the subtitle callback with the frame number in between - so the
// subtitles, which the caller loads and draws, come out as before. The picture itself goes to the backend as a quad
// of our own (D3D9_DrawMovieFrame), converted from FFmpeg's 4:2:0 to 32-bit colour rather than EA's dithered 16-bit.
// The audio is a 5.1 voice of its own, folded to stereo with the gains the game's own mixer output gets.
// ---------------------------------------------------------------------------------------------------------------

#define MOVIE_FRAME_MS_DEFAULT 40       // 25 fps, what every movie's MADk header says

// Chunk tags (four characters, little-endian)
#define TAG(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define TAG_SCHl TAG('S', 'C', 'H', 'l')
#define TAG_SCDl TAG('S', 'C', 'D', 'l')
#define TAG_SCEl TAG('S', 'C', 'E', 'l')
#define TAG_MADk TAG('M', 'A', 'D', 'k')
#define TAG_MADm TAG('M', 'A', 'D', 'm')
#define TAG_MADe TAG('M', 'A', 'D', 'e')

static struct Movie {
    PlayMPC *owner;                     // the PlayMPC that Init opened it for
    FILE *file;
    long base, size, pos;               // the movie's bytes within the file (an archive, or a loose file)
    bool fileEnded;

    AVCodecContext *video, *audio;
    AVPacket *packet;
    AVFrame *frame;
    std::deque<AVPacket *> videoPackets;   // read ahead of the picture while keeping the audio fed
    bool videoEnded;
    bool audioHeaderSeen;               // the SCHl chunk has been read (it follows the first video chunk)
    bool started;                       // the clock is running: an audio header now would be too late
    int frameMs;                        // from the MADk header
    int framesDecoded;                  // the number of the next frame out of the decoder
    double nextFrameTime;               // seconds: when the frame after the one shown is due

    uint32_t *rgb;                      // the frame shown, X8R8G8B8
    int width, height;

    FmvAudio sound;
} M;

// ---------------------------------------------------------------------------------------------------------------
// Finding the file: loose first, as EA's file system looks, then inside the archives in driving\
// ---------------------------------------------------------------------------------------------------------------

static uint32_t BigEndian32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// Looks `name` up in a BIG archive's directory: "BIGF", the total size, the entry count and the directory's size
// (big-endian), then per entry its offset and size (big-endian) and its name
static bool FindInBig(const char *bigPath, const char *name, long *base, long *size) {
    FILE *f = fopen(bigPath, "rb");
    if (f == NULL)
        return false;
    uint8_t header[16];
    bool found = false;
    if (fread(header, 1, 16, f) == 16 && memcmp(header, "BIGF", 4) == 0) {
        uint32_t count = BigEndian32(header + 8), dirSize = BigEndian32(header + 12);
        uint8_t *dir = (dirSize > 16 && dirSize < (16u << 20)) ? (uint8_t *)malloc(dirSize) : NULL;
        if (dir != NULL && fread(dir, 1, dirSize - 16, f) == dirSize - 16) {
            const uint8_t *p = dir, *end = dir + dirSize - 16;
            for (uint32_t i = 0; i < count && p + 9 <= end && !found; i++) {
                uint32_t offset = BigEndian32(p), length = BigEndian32(p + 4);
                const char *entry = (const char *)p + 8;
                size_t n = strnlen(entry, (size_t)(end - p - 8));
                if (_stricmp(entry, name) == 0) {
                    *base = (long)offset;
                    *size = (long)length;
                    found = true;
                }
                p += 8 + n + 1;
            }
        }
        free(dir);
    }
    fclose(f);
    return found;
}

static FILE *OpenMovieFile(const char *path, long *base, long *size) {
    // The path is relative to the disc (it may carry the drive)
    const char *rel = path;
    if ((rel[0] == 'd' || rel[0] == 'D') && rel[1] == ':')
        rel += 2;
    while (*rel == '\\' || *rel == '/')
        rel++;

    char xboxPath[MAX_PATH], hostPath[MAX_PATH];
    snprintf(xboxPath, sizeof(xboxPath), "d:\\%s", rel);
    if (Xbox_ResolvePath(xboxPath, hostPath, sizeof(hostPath))) {
        FILE *f = fopen(hostPath, "rb");
        if (f != NULL) {
            fseek(f, 0, SEEK_END);
            *base = 0;
            *size = ftell(f);
            return f;
        }
    }

    char pattern[MAX_PATH];
    if (!Xbox_ResolvePath("d:\\driving\\*.viv", pattern, sizeof(pattern)))
        return NULL;
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
        return NULL;
    FILE *result = NULL;
    do {
        char archive[MAX_PATH];
        snprintf(xboxPath, sizeof(xboxPath), "d:\\driving\\%s", found.cFileName);
        if (Xbox_ResolvePath(xboxPath, archive, sizeof(archive)) && FindInBig(archive, rel, base, size))
            result = fopen(archive, "rb");
    } while (result == NULL && FindNextFileA(search, &found));
    FindClose(search);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------
// The chunks
// ---------------------------------------------------------------------------------------------------------------

static void CloseMovie(void) {
    M.sound.Close();
    for (AVPacket *p : M.videoPackets)
        ff.packet_free(&p);
    M.videoPackets.clear();
    if (M.video != NULL) ff.free_context(&M.video);
    if (M.audio != NULL) ff.free_context(&M.audio);
    if (M.packet != NULL) ff.packet_free(&M.packet);
    if (M.frame != NULL) ff.frame_free(&M.frame);
    if (M.file != NULL) fclose(M.file);
    free(M.rgb);
    M.file = NULL;
    M.rgb = NULL;
    M.owner = NULL;
    M.fileEnded = M.videoEnded = M.audioHeaderSeen = M.started = false;
    M.framesDecoded = M.width = M.height = 0;
}

static AVCodecContext *OpenDecoder(enum AVCodecID id, int channels, int sampleRate) {
    const AVCodec *codec = ff.find_decoder(id);
    if (codec == NULL)
        return NULL;
    AVCodecContext *ctx = ff.alloc_context3(codec);
    if (ctx == NULL)
        return NULL;
    if (channels > 0) {
        ff.channel_layout_default(&ctx->ch_layout, channels);
        ctx->sample_rate = sampleRate;
    }
    if (ff.open2(ctx, codec, NULL) < 0)
        ff.free_context(&ctx);
    return ctx;
}

// The audio header: "PT", the platform (7 here) and a zero, then byte tags, each with a length-prefixed big-endian
// value. 0xFD opens the audio subheader, in which 0x82 is the channel count and 0x84 the sample rate; 0x8A closes it
// and 0xFF ends the header. The movies' say 6 channels, 48000 Hz, revision2 10 (EA-XA R1).
static void ParseAudioHeader(const uint8_t *p, const uint8_t *end, int *channels, int *sampleRate) {
    *channels = 1;
    *sampleRate = 22050;
    if (end - p < 4 || p[0] != 'P')
        return;
    p += 4;
    bool inSub = false;
    while (p < end) {
        uint8_t tag = *p++;
        if (!inSub && tag == 0xFD) { inSub = true; continue; }
        if (tag == 0xFF) return;
        if (inSub && tag == 0x8A) inSub = false;
        if (p >= end) return;
        uint8_t length = *p++;
        uint32_t value = 0;
        for (uint8_t i = 0; i < length && p < end; i++)
            value = value << 8 | *p++;
        if (inSub && tag == 0x82) *channels = (int)value;
        if (inSub && tag == 0x84) *sampleRate = (int)value;
    }
}

static void DecodeAudio(AVPacket *packet) {
    if (M.audio == NULL || ff.send_packet(M.audio, packet) < 0)
        return;
    while (ff.receive_frame(M.audio, M.frame) == 0)
        M.sound.Queue(M.frame);
}

static void EndOfAudio(void) {
    DecodeAudio(NULL);
    M.sound.Finish();
}

// Reads and handles one chunk. False at the end of the movie.
static bool ReadChunk(void) {
    if (M.fileEnded)
        return false;
    uint8_t header[8];
    if (M.pos + 8 > M.size || fseek(M.file, M.base + M.pos, SEEK_SET) != 0 || fread(header, 1, 8, M.file) != 8) {
        M.fileEnded = true;
        EndOfAudio();
        return false;
    }
    uint32_t tag = header[0] | header[1] << 8 | header[2] << 16 | (uint32_t)header[3] << 24;
    uint32_t length = header[4] | header[5] << 8 | header[6] << 16 | (uint32_t)header[7] << 24;
    if (length < 8 || M.pos + (long)length > M.size) {
        M.fileEnded = true;
        EndOfAudio();
        return false;
    }

    if (tag == TAG_MADk || tag == TAG_MADm || tag == TAG_MADe) {
        AVPacket *p = ff.packet_alloc();
        if (p != NULL && ff.new_packet(p, (int)length) == 0) {
            memcpy(p->data, header, 8);
            if (fread(p->data + 8, 1, length - 8, M.file) == length - 8) {
                if (tag == TAG_MADk && length >= 16) {
                    int ms = p->data[14] | p->data[15] << 8;
                    if (ms > 0 && ms < 1000)
                        M.frameMs = ms;
                }
                M.videoPackets.push_back(p);
                p = NULL;
            }
        }
        if (p != NULL)
            ff.packet_free(&p);
    } else if (tag == TAG_SCHl && !M.audioHeaderSeen && !M.started) {
        M.audioHeaderSeen = true;
        uint8_t *body = (uint8_t *)malloc(length - 8);
        if (body != NULL && fread(body, 1, length - 8, M.file) == length - 8) {
            int channels, sampleRate;
            ParseAudioHeader(body, body + length - 8, &channels, &sampleRate);
            M.audio = OpenDecoder(AV_CODEC_ID_ADPCM_EA_R1, channels, sampleRate);
            float fold[12], matrix[12];
            DrivingAudio_StereoFold(fold);
            for (int s = 0; s < 6; s++) {      // XAudio2 wants each output's row of input levels
                matrix[0 * 6 + s] = fold[s * 2];
                matrix[1 * 6 + s] = fold[s * 2 + 1];
            }
            bool fiveOne = channels == 6;
            if (M.audio == NULL ||
                !M.sound.Open(DrivingAudio_GetDevice(), channels, sampleRate, fiveOne ? matrix : NULL, 2)) {
                printf("[fmv] no audio for this movie (%d channels at %d Hz)\n", channels, sampleRate);
                if (M.audio != NULL)
                    ff.free_context(&M.audio);
            }
        }
        free(body);
    } else if (tag == TAG_SCDl && M.audio != NULL) {
        if (ff.new_packet(M.packet, (int)length - 8) == 0) {
            if (fread(M.packet->data, 1, length - 8, M.file) == length - 8)
                DecodeAudio(M.packet);
            ff.packet_unref(M.packet);
        }
    } else if (tag == TAG_SCEl) {
        EndOfAudio();
    }
    M.pos += (long)length;
    return true;
}

static void KeepAudioFed(void) {
    while (M.sound.WantsMore() && ReadChunk()) {
    }
}

// Decodes the next frame into M.frame. False when there are no more.
static bool DecodeVideoFrame(void) {
    for (;;) {
        int r = ff.receive_frame(M.video, M.frame);
        if (r == 0) {
            M.framesDecoded++;
            return true;
        }
        if (r == AVERROR_EOF) {
            M.videoEnded = true;
            return false;
        }
        while (M.videoPackets.empty() && ReadChunk()) {
        }
        if (M.videoPackets.empty()) {
            ff.send_packet(M.video, NULL);   // drain what is left
            continue;
        }
        AVPacket *p = M.videoPackets.front();
        M.videoPackets.pop_front();
        ff.send_packet(M.video, p);
        ff.packet_free(&p);
    }
}

static uint8_t Clamp(int v) {
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

// 4:2:0 planar (BT.601, video range) to X8R8G8B8
static void ConvertFrame(const AVFrame *f) {
    if (M.rgb == NULL || M.width != f->width || M.height != f->height) {
        free(M.rgb);
        M.width = f->width;
        M.height = f->height;
        M.rgb = (uint32_t *)malloc((size_t)M.width * M.height * 4);
        if (M.rgb == NULL)
            return;
    }
    for (int y = 0; y < M.height; y++) {
        const uint8_t *Y = f->data[0] + y * f->linesize[0];
        const uint8_t *U = f->data[1] + (y >> 1) * f->linesize[1];
        const uint8_t *V = f->data[2] + (y >> 1) * f->linesize[2];
        uint32_t *out = M.rgb + (size_t)y * M.width;
        for (int x = 0; x < M.width; x++) {
            int c = 298 * (Y[x] - 16), d = U[x >> 1] - 128, e = V[x >> 1] - 128;
            out[x] = 0xff000000u | (uint32_t)Clamp((c + 409 * e + 128) >> 8) << 16 |
                     (uint32_t)Clamp((c - 100 * d - 208 * e + 128) >> 8) << 8 | Clamp((c + 516 * d + 128) >> 8);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// PlayMPC
// ---------------------------------------------------------------------------------------------------------------

// EA's player library's settings (RCMP_SYSTEM at 0x001dcd10): the allocator and the flags its allocations take,
// which SetREALDefaults (0x0014df10) set to MEM_allocalign and MEM_free. Nothing reads them now that the player is
// FFmpeg; they are kept as the original left them.
#define RcmpAllocate     (*(uint32_t *)0x001dcd14u)
#define RcmpFree         (*(uint32_t *)0x001dcd18u)
#define RcmpAllocFlags   (*(int32_t *)0x001dcd1cu)
#define MovieGlobal244798 (*(int32_t *)0x00244798u)   // set by the original Play, cleared here; never read

// FUNC_AT(0x00130780)
PlayMPC* PlayMPC::Construct(void *device_, EAGL::RenderContext *context_, int32_t padPort_, int32_t allocFlags) {
    device = device_;
    context = context_;
    unknown08 = 0;
    frameShown = 0;
    skipped = 0;
    player = NULL;
    padPort = padPort_;
    RcmpAllocate = 0x00114340;   // MEM_allocalign
    RcmpFree = 0x00113f20;       // MEM_free
    RcmpAllocFlags = allocFlags;
    return this;
}

// The original destroyed and freed EA's player object here; ours never makes one.
// FUNC_AT(0x00130b50)
void PlayMPC::Destruct() {
    MovieGlobal244798 = 0;
    player = NULL;
}

// AUTOINJECT
void PlayMPC::Init(const char *path, uint32_t volume, bool mode) {
    (void)mode;
    if (path == NULL)
        return;                         // as the original: nothing is touched
    CloseMovie();
    this->player = NULL;
    this->volume = (int32_t)volume;

    M.file = OpenMovieFile(path, &M.base, &M.size);
    if (M.file == NULL) {
        printf("[fmv] cannot find the movie %s\n", path);
        return;
    }
    M.pos = 0;
    M.frameMs = MOVIE_FRAME_MS_DEFAULT;
    M.video = OpenDecoder(AV_CODEC_ID_MAD, 0, 0);
    M.packet = ff.packet_alloc();
    M.frame = ff.frame_alloc();
    if (M.video == NULL || M.packet == NULL || M.frame == NULL) {
        printf("[fmv] cannot set up the decoders for %s\n", path);
        CloseMovie();
        return;
    }
    M.owner = this;
    printf("[fmv] %s\n", path);
}

// AUTOINJECT
void PlayMPC::Play(EAGL::ViewPort *viewport, bool widescreen, SubtitleCallback subtitles) {
    if (M.owner != this || M.file == NULL)
        return;                         // as the original, with no player: nothing to play

    // The first frame, and the audio from there. The audio header comes after the first video chunk, so read on
    // until it has been seen (or a few frames have been buffered without one) before the clock starts.
    while (!M.audioHeaderSeen && M.videoPackets.size() < 16 && ReadChunk()) {
    }
    KeepAudioFed();
    if (!DecodeVideoFrame()) {
        CloseMovie();
        return;
    }
    ConvertFrame(M.frame);
    M.sound.SetVolume((float)this->volume / 127.0f);   // EA's 0-127 (the curve EA's mixer applies is not mapped)
    this->frameShown++;
    this->unknown08 = 0;
    M.nextFrameTime = M.frameMs / 1000.0;

    // Placement, as the original: the whole screen, or with `widescreen` the picture stretched by 4/3 and moved up
    // a sixth of the screen, so that the film inside the 4:3 frame fills a 16:9 one
    float width, height;
    this->context->GetSize(&width, &height);
    viewport->SetShape(0.0f, 0.0f, width, height, 0.01f, 1.0f);
    float top = widescreen ? height * -0.16666669f : 0.0f;
    float bottom = top + (widescreen ? height * 1.3333334f : height);

    M.sound.Start();
    M.started = true;
    for (;;) {
        this->context->BeginFrame();
        viewport->BeginView();
        viewport->ClearViewPort(7);
        if (M.rgb != NULL)
            D3D9_DrawMovieFrame(M.rgb, (uint32_t)M.width, (uint32_t)M.height, (uint32_t)M.width * 4,
                                0.0f, top, width, bottom);
        if (subtitles != NULL)
            subtitles(this->frameShown);
        viewport->EndView();
        this->context->EndFrame();

        // Until the next frame is due, keeping EA's tasks and the audio going
        while (M.sound.Clock() < M.nextFrameTime) {
            KeepAudioFed();
            SYNCTASK_run(0);
            THREAD_yield(0);
        }

        PAD_update();
        PadData *pad = PAD_getdataptr(this->padPort);
        if (pad != NULL && (pad->analogPressed[0] != 0 || pad->analogPressed[1] != 0 || (pad->buttonsPressed & 0x10) != 0))
            this->skipped = 1;

        // Every frame now due is decoded; only the last is shown. As the original, the movie ends with its frames
        // (a failed decode leaves M.frame empty, so there is nothing more to show either way).
        KeepAudioFed();
        double now = M.sound.Clock();
        bool ended = false;
        for (;;) {
            if (!DecodeVideoFrame()) {
                ended = true;
                break;
            }
            M.nextFrameTime = M.framesDecoded * (M.frameMs / 1000.0);
            if (now < M.nextFrameTime)
                break;
        }
        if (ended || this->skipped) {
            printf("[fmv] %s at frame %d\n", this->skipped ? "skipped" : "finished", this->frameShown);
            break;
        }
        ConvertFrame(M.frame);
        this->frameShown = M.framesDecoded - 1;
    }
    CloseMovie();
}
