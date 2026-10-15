#include "Ffmpeg.h"

#include <stdio.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef HMODULE Library;
#define OpenLibrary(name) LoadLibraryA(name)
#define LibrarySymbol(library, name) ((void *)GetProcAddress(library, name))
#define AVUTIL "avutil-60.dll"
#define AVCODEC "avcodec-62.dll"
#define AVFORMAT "avformat-62.dll"
#else
#include <dlfcn.h>
#include <stdlib.h>

// Beside the executable (its run path), or wherever the system's loader finds them
typedef void *Library;
#define OpenLibrary(name) dlopen(name, RTLD_NOW)
#define LibrarySymbol(library, name) dlsym(library, name)
#define AVUTIL "libavutil.so.60"
#define AVCODEC "libavcodec.so.62"
#define AVFORMAT "libavformat.so.62"
#endif

FfmpegApi ff;

static bool Load(void) {
    Library avutil = OpenLibrary(AVUTIL);
    Library avcodec = OpenLibrary(AVCODEC);
    Library avformat = OpenLibrary(AVFORMAT);
    if (avutil == NULL || avcodec == NULL || avformat == NULL)
        return false;
    bool ok = true;
#define FF_LOAD(module, field, name) \
    if ((*(void **)&ff.field = LibrarySymbol(module, name)) == NULL) { printf("[ffmpeg] %s missing\n", name); ok = false; }
    FF_LOAD(avformat, open_input, "avformat_open_input");
    FF_LOAD(avformat, find_stream_info, "avformat_find_stream_info");
    FF_LOAD(avformat, close_input, "avformat_close_input");
    FF_LOAD(avformat, read_frame, "av_read_frame");
    FF_LOAD(avcodec, find_decoder, "avcodec_find_decoder");
    FF_LOAD(avcodec, alloc_context3, "avcodec_alloc_context3");
    FF_LOAD(avcodec, parameters_to_context, "avcodec_parameters_to_context");
    FF_LOAD(avcodec, open2, "avcodec_open2");
    FF_LOAD(avcodec, send_packet, "avcodec_send_packet");
    FF_LOAD(avcodec, receive_frame, "avcodec_receive_frame");
    FF_LOAD(avcodec, free_context, "avcodec_free_context");
    FF_LOAD(avcodec, packet_alloc, "av_packet_alloc");
    FF_LOAD(avcodec, packet_free, "av_packet_free");
    FF_LOAD(avcodec, packet_unref, "av_packet_unref");
    FF_LOAD(avcodec, new_packet, "av_new_packet");
    FF_LOAD(avutil, frame_alloc, "av_frame_alloc");
    FF_LOAD(avutil, frame_free, "av_frame_free");
    FF_LOAD(avutil, log_set_level, "av_log_set_level");
    FF_LOAD(avutil, channel_layout_default, "av_channel_layout_default");
#undef FF_LOAD
    if (ok)
        ff.log_set_level(AV_LOG_ERROR);
    return ok;
}

void Ffmpeg_Require(void) {
    static bool loaded = false;
    if (loaded)
        return;
    if (Load()) {
        loaded = true;
        return;
    }
    printf("[ffmpeg] the FFmpeg libraries (" AVUTIL ", " AVCODEC ", " AVFORMAT ") are missing or incomplete\n");
#ifdef _WIN32
    MessageBoxA(NULL, "The FFmpeg DLLs (avutil-60.dll, avcodec-62.dll, avformat-62.dll) are missing from the game's "
                "folder, or are not the version it was built with. They come with every build, from "
                "third_party/ffmpeg-prebuilt.",
                "Nightfire", MB_ICONERROR | MB_OK);
    ExitProcess(1);
#else
    exit(1);
#endif
}
