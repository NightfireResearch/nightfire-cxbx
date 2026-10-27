These DLLs are FFmpeg 8.1.3 (https://ffmpeg.org), licensed under the LGPL 2.1 or later
(FFmpeg-COPYING.LGPLv2.1.txt). They are dynamically linked: the game loads them at startup.

Source: FFmpeg's own repository (https://git.ffmpeg.org/ffmpeg.git) at commit 1041abdc962f4cc4f394aa8de9dc5236c0c3b9e7 (tag n8.1.3), modified by
the patches in tools/fmv/patches of https://github.com/NightfireResearch/nightfire-cxbx, which add an HVQM4 demuxer and
video decoder (Paul B Mahol's unmerged patches of July 2023, changed: each patch and file says what, by whom and
when) and build the adpcm_ima_hvqm4 decoder without --enable-gpl:
    0001-avformat-add-hvqm4-demuxer.patch
    0002-avcodec-add-HVQM4-video-decoder.patch
    0003-configure-build-adpcm_ima_hvqm4-without-enable-gpl.patch

Built by tools/fmv/build_ffmpeg.sh with the i686 mingw-w64 cross compiler and this configuration:

    configure --target-os=mingw32 --arch=x86 --cross-prefix=i686-w64-mingw32- --enable-cross-compile --enable-shared --disable-static --disable-programs --disable-doc --disable-network --disable-autodetect --disable-everything --enable-protocol=file --enable-demuxer=xmv --enable-decoder=wmv2,adpcm_ima_xbox,adpcm_ima_wav,eamad,adpcm_ea_r1 --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample --enable-demuxer=hvqm4 --enable-decoder=hvqm4,adpcm_ima_hvqm4 --enable-w32threads --extra-ldflags=-static-libgcc

For the action engine's NIGHTFIRE_ACTION_DATA_GC build: third_party/ffmpeg-prebuilt's configuration with the
GameCube's movies added (HVQM4 video, its IMA ADPCM audio).

To rebuild: FFMPEG_SOURCE=<a checkout of FFmpeg n8.1.3> sh tools/fmv/build_ffmpeg.sh windows-x86-gc.
