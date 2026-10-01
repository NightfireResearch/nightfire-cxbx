These DLLs are FFmpeg 8.0.3 (https://ffmpeg.org), licensed under the LGPL 2.1 or later
(FFmpeg-COPYING.LGPLv2.1.txt). They are dynamically linked: the game loads them at startup.

Source: the third_party/ffmpeg submodule of https://github.com/NightfireResearch/nightfire-cxbx, which is FFmpeg's
own repository at commit 8ae0b34901ba60a802f183ee75a250a9fc3e09a5 (tag n8.0.3), unmodified.

Built by tools/fmv/build_ffmpeg.sh with the i686 mingw-w64 cross compiler and this configuration:

    configure --target-os=mingw32 --arch=x86 --cross-prefix=i686-w64-mingw32- --enable-cross-compile --enable-shared --disable-static --disable-programs --disable-doc --disable-network --disable-autodetect --disable-everything --enable-protocol=file --enable-demuxer=xmv --enable-decoder=wmv2,adpcm_ima_xbox,adpcm_ima_wav,eamad,adpcm_ea_r1 --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample --enable-w32threads --extra-ldflags=-static-libgcc

To rebuild: git submodule update --init third_party/ffmpeg, then sh tools/fmv/build_ffmpeg.sh.
