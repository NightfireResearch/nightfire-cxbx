// fmvaudiocheck: the movies' sound as the driving engine's player decodes it (src/driving/engine/PlayMPC.cpp),
// without the game.
//
//   fmvaudiocheck [-s seconds] <archive.viv>...
//
// Every .mad in each BIG archive: its audio header, then the first seconds (30 by default) of its SCDl chunks
// through the EA-XA R1 decoder opened as PlayMPC opens it. Prints what the decoder hands back (sample format,
// channel count and layout), each decoded channel's RMS, zero crossings per second and share of its energy below
// 150 Hz (nearly all of it, for the LFE), and for a movie also seen in another language's folder, each channel's
// RMS of the difference from that one (the voices differ, the music and effects do not). Then the left and right
// RMS of the stereo fold FmvAudio::Queue applies - once with the six channels taken in FFmpeg's default 5.1 order
// (FL FR C LFE BL BR) and once in EA's (FL C FR BL BR C), the order PlayMPC folds them in.

#include "../../common/fmv/Ffmpeg.h"

#include <math.h>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

enum { FL, FR, C, LFE, BL, BR };

// DrivingAudio_StereoFold's gains (MixBinToStereo): each speaker's left and right level
const float kFold[6][2] = {
    { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.7071068f, 0.7071068f }, { 0.7071068f, 0.7071068f },
    { 0.7071068f, 0.0f }, { 0.0f, 0.7071068f },
};

const float kLowPass = 0.0194f;       // 1 - e^(-2 pi 150 / 48000)

const int kDefaultOrder[6] = { FL, FR, C, LFE, BL, BR };
const int kEaOrder[6] = { FL, C, FR, BL, BR, C };   // PlayMPC's kChannelSpeakers

struct Movie {
    std::string name;
    uint32_t offset, size;
};

uint32_t BigEndian32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

std::vector<Movie> ListMovies(FILE *f) {
    std::vector<Movie> movies;
    uint8_t header[16];
    if (fread(header, 1, 16, f) != 16 || memcmp(header, "BIGF", 4) != 0)
        return movies;
    uint32_t count = BigEndian32(header + 8), dirSize = BigEndian32(header + 12);
    std::vector<uint8_t> dir(dirSize > 16 ? dirSize - 16 : 0);
    if (fread(dir.data(), 1, dir.size(), f) != dir.size())
        return movies;
    const uint8_t *p = dir.data(), *end = p + dir.size();
    for (uint32_t i = 0; i < count && p + 9 <= end; i++) {
        const char *entry = (const char *)p + 8;
        size_t n = strnlen(entry, (size_t)(end - p - 8));
        if (n > 4 && _stricmp(entry + n - 4, ".mad") == 0)
            movies.push_back({ std::string(entry, n), BigEndian32(p), BigEndian32(p + 4) });
        p += 8 + n + 1;
    }
    return movies;
}

// PlayMPC's ParseAudioHeader
void ParseAudioHeader(const uint8_t *p, const uint8_t *end, int *channels, int *sampleRate) {
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

struct Levels {
    double input[8] = {};
    double low[8] = {};                // through a one-pole low-pass at about 150 Hz
    float lowState[8] = {};
    long crossings[8] = {};
    int16_t last[8] = {};
    double output[2][2] = {};          // [order][left, right]
    long frames = 0;
    std::vector<int16_t> samples[8];
};

std::map<std::string, Levels> g_firstLanguage;   // by the movie's file name, without its folder

// One decoded frame: each channel's energy and crossings, then FmvAudio::Queue's fold in both orders
void Measure(const AVFrame *frame, int channels, Levels *levels) {
    bool planar = frame->format == AV_SAMPLE_FMT_S16P;
    int n = frame->ch_layout.nb_channels < channels ? frame->ch_layout.nb_channels : channels;
    for (int i = 0; i < frame->nb_samples; i++) {
        float in[8] = {};
        for (int c = 0; c < n; c++) {
            int16_t s = planar ? ((const int16_t *)frame->extended_data[c])[i]
                               : ((const int16_t *)frame->extended_data[0])[i * frame->ch_layout.nb_channels + c];
            in[c] = s;
            levels->input[c] += (double)s * s;
            levels->lowState[c] += kLowPass * (s - levels->lowState[c]);
            levels->low[c] += (double)levels->lowState[c] * levels->lowState[c];
            if ((s < 0) != (levels->last[c] < 0))
                levels->crossings[c]++;
            levels->last[c] = s;
            levels->samples[c].push_back(s);
        }
        if (channels == 6) {
            for (int order = 0; order < 2; order++) {
                const int *speakers = order == 0 ? kDefaultOrder : kEaOrder;
                for (int o = 0; o < 2; o++) {
                    float value = 0.0f;
                    for (int c = 0; c < 6; c++)
                        value += kFold[speakers[c]][o] * in[c];
                    value = value > 32767.0f ? 32767.0f : value < -32768.0f ? -32768.0f : value;
                    levels->output[order][o] += (double)value * value;
                }
            }
        }
    }
    levels->frames += frame->nb_samples;
}

double Rms(double sum, long frames) {
    return frames > 0 ? sqrt(sum / frames) : 0.0;
}

void CheckMovie(FILE *f, const Movie &movie, double seconds) {
    printf("%s\n", movie.name.c_str());
    AVCodecContext *audio = NULL;
    AVPacket *packet = ff.packet_alloc();
    AVFrame *frame = ff.frame_alloc();
    int channels = 0, sampleRate = 0;
    bool described = false;
    Levels levels;
    uint32_t pos = 0;
    while (pos + 8 <= movie.size) {
        uint8_t header[8];
        if (fseek(f, (long)(movie.offset + pos), SEEK_SET) != 0 || fread(header, 1, 8, f) != 8)
            break;
        uint32_t length = header[4] | header[5] << 8 | header[6] << 16 | (uint32_t)header[7] << 24;
        if (length < 8 || pos + length > movie.size)
            break;
        if (memcmp(header, "SCHl", 4) == 0 && audio == NULL) {
            std::vector<uint8_t> body(length - 8);
            if (fread(body.data(), 1, body.size(), f) != body.size())
                break;
            ParseAudioHeader(body.data(), body.data() + body.size(), &channels, &sampleRate);
            const AVCodec *codec = ff.find_decoder(AV_CODEC_ID_ADPCM_EA_R1);
            audio = codec != NULL ? ff.alloc_context3(codec) : NULL;
            if (audio != NULL) {
                ff.channel_layout_default(&audio->ch_layout, channels);
                audio->sample_rate = sampleRate;
                if (ff.open2(audio, codec, NULL) < 0)
                    ff.free_context(&audio);
            }
            printf("  header: %d channels at %d Hz%s\n", channels, sampleRate, audio == NULL ? ", no decoder" : "");
            if (audio == NULL)
                break;
        } else if (memcmp(header, "SCDl", 4) == 0 && audio != NULL) {
            if (ff.new_packet(packet, (int)length - 8) == 0) {
                if (fread(packet->data, 1, length - 8, f) == length - 8 && ff.send_packet(audio, packet) >= 0) {
                    while (ff.receive_frame(audio, frame) == 0) {
                        if (!described) {
                            const char *format = frame->format == AV_SAMPLE_FMT_S16P ? "s16p"
                                               : frame->format == AV_SAMPLE_FMT_S16 ? "s16" : "other";
                            printf("  decoded: format %s (%d), %d channels, layout order %d mask 0x%llx\n", format,
                                   frame->format, frame->ch_layout.nb_channels, frame->ch_layout.order,
                                   (unsigned long long)frame->ch_layout.u.mask);
                            described = true;
                        }
                        Measure(frame, channels, &levels);
                    }
                }
                ff.packet_unref(packet);
            }
            if (levels.frames >= (long)(seconds * sampleRate))
                break;
        }
        pos += length;
    }
    std::string file = movie.name.substr(movie.name.find_last_of('\\') + 1);
    if (levels.frames > 0) {
        double duration = (double)levels.frames / sampleRate;
        auto other = g_firstLanguage.find(file);
        printf("  %.1f s; per channel RMS, crossings/s, share below 150 Hz%s:\n", duration,
               other != g_firstLanguage.end() ? ", RMS of the difference from the first language's" : "");
        for (int c = 0; c < channels && c < 8; c++) {
            printf("    %d: %6.0f %6.0f %4.0f%%", c, Rms(levels.input[c], levels.frames), levels.crossings[c] / duration,
                   levels.input[c] > 0.0 ? 100.0 * levels.low[c] / levels.input[c] : 0.0);
            if (other != g_firstLanguage.end()) {
                const std::vector<int16_t> &a = levels.samples[c], &b = other->second.samples[c];
                size_t n = a.size() < b.size() ? a.size() : b.size();
                double sum = 0.0;
                for (size_t i = 0; i < n; i++)
                    sum += (double)(a[i] - b[i]) * (a[i] - b[i]);
                printf(" %6.0f", Rms(sum, (long)n));
            }
            printf("\n");
        }
        if (channels == 6) {
            printf("  fold, FFmpeg's order: L %.0f R %.0f\n", Rms(levels.output[0][0], levels.frames),
                   Rms(levels.output[0][1], levels.frames));
            printf("  fold, EA's order:     L %.0f R %.0f\n", Rms(levels.output[1][0], levels.frames),
                   Rms(levels.output[1][1], levels.frames));
        }
        if (channels == 6 && other != g_firstLanguage.end()) {
            // the difference from the first language (the voices), folded in both orders
            size_t n = levels.samples[0].size() < other->second.samples[0].size() ? levels.samples[0].size()
                                                                                  : other->second.samples[0].size();
            double sums[2][2] = {};
            for (size_t i = 0; i < n; i++) {
                for (int order = 0; order < 2; order++) {
                    const int *speakers = order == 0 ? kDefaultOrder : kEaOrder;
                    for (int o = 0; o < 2; o++) {
                        float value = 0.0f;
                        for (int c = 0; c < 6; c++)
                            value += kFold[speakers[c]][o] * (levels.samples[c][i] - other->second.samples[c][i]);
                        sums[order][o] += (double)value * value;
                    }
                }
            }
            printf("  the difference folded, FFmpeg's order: L %.0f R %.0f; EA's order: L %.0f R %.0f\n",
                   Rms(sums[0][0], (long)n), Rms(sums[0][1], (long)n), Rms(sums[1][0], (long)n),
                   Rms(sums[1][1], (long)n));
        }
    }
    if (g_firstLanguage.find(file) == g_firstLanguage.end())
        g_firstLanguage[file] = std::move(levels);
    if (audio != NULL)
        ff.free_context(&audio);
    ff.frame_free(&frame);
    ff.packet_free(&packet);
}

} // namespace

int main(int argc, char **argv) {
    Ffmpeg_Require();
    double seconds = 30.0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            seconds = atof(argv[++i]);
            continue;
        }
        FILE *f = fopen(argv[i], "rb");
        if (f == NULL) {
            printf("cannot open %s\n", argv[i]);
            continue;
        }
        for (const Movie &movie : ListMovies(f))
            CheckMovie(f, movie, seconds);
        fclose(f);
    }
    return 0;
}
