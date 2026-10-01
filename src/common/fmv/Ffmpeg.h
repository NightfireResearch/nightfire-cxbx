#ifndef COMMON_FMV_FFMPEG_H_
#define COMMON_FMV_FFMPEG_H_

// FFmpeg for both engines' movie players (src/action/engine/Fmv.cpp, src/driving/engine/PlayMPC.cpp): the DLLs in
// third_party/ffmpeg-prebuilt, loaded at run time rather than linked, so no import library has to suit three
// toolchains. docs/fmv.md describes the build.

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
}

// The functions the players call, filled in by Ffmpeg_Require
struct FfmpegApi {
    decltype(&avformat_open_input)            open_input;
    decltype(&avformat_find_stream_info)      find_stream_info;
    decltype(&avformat_close_input)           close_input;
    decltype(&av_read_frame)                  read_frame;
    decltype(&avcodec_find_decoder)           find_decoder;
    decltype(&avcodec_alloc_context3)         alloc_context3;
    decltype(&avcodec_parameters_to_context)  parameters_to_context;
    decltype(&avcodec_open2)                  open2;
    decltype(&avcodec_send_packet)            send_packet;
    decltype(&avcodec_receive_frame)          receive_frame;
    decltype(&avcodec_free_context)           free_context;
    decltype(&av_packet_alloc)                packet_alloc;
    decltype(&av_packet_free)                 packet_free;
    decltype(&av_packet_unref)                packet_unref;
    decltype(&av_new_packet)                  new_packet;
    decltype(&av_frame_alloc)                 frame_alloc;
    decltype(&av_frame_free)                  frame_free;
    decltype(&av_log_set_level)               log_set_level;
    decltype(&av_channel_layout_default)      channel_layout_default;
};
extern FfmpegApi ff;

// Loads the DLLs, once; if they are missing, says so and exits the process. Call at startup.
void Ffmpeg_Require(void);

#endif // COMMON_FMV_FFMPEG_H_
