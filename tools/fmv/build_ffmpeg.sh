#!/bin/sh
# Builds the minimal FFmpeg the FMV player (src/action/engine/Fmv.cpp) uses: 32-bit Windows DLLs with just the XMV
# demuxer, the WMV2 video decoder and the Xbox IMA ADPCM audio decoder, LGPL (no GPL parts), no other dependencies.
# Output: third_party/ffmpeg/{bin,include} (gitignored). CMake copies the DLLs beside the executables.
#
#   sh tools/fmv/build_ffmpeg.sh
#
# Runs in the nf-cross image (it has the i686 mingw-w64 compilers), installing make and nasm into a throwaway
# container. The source is cloned once into build/ffmpeg-src.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
TAG=n8.0.3   # 8.0 has the Xbox IMA ADPCM decoder (adpcm_ima_xbox); 7.1 decodes it as adpcm_ima_wav
mkdir -p "$REPO/build" "$REPO/third_party"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(cygpath -m "$REPO" 2>/dev/null || echo "$REPO"):/src" nf-cross bash -c "
set -e
apt-get update -qq >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq make nasm >/dev/null 2>&1
if [ -d /src/build/ffmpeg-src ] && [ \"\$(git -C /src/build/ffmpeg-src describe --tags 2>/dev/null)\" != $TAG ]; then
    rm -rf /src/build/ffmpeg-src /src/third_party/ffmpeg
fi
if [ ! -d /src/build/ffmpeg-src ]; then
    git clone -q --depth 1 --branch $TAG https://git.ffmpeg.org/ffmpeg.git /src/build/ffmpeg-src
fi
cd /src/build/ffmpeg-src
make distclean >/dev/null 2>&1 || true
./configure --prefix=/src/third_party/ffmpeg \
    --target-os=mingw32 --arch=x86 --cross-prefix=i686-w64-mingw32- --enable-cross-compile \
    --enable-shared --disable-static --disable-programs --disable-doc --disable-network --disable-autodetect \
    --disable-everything --enable-protocol=file --enable-demuxer=xmv --enable-decoder=wmv2,adpcm_ima_xbox,adpcm_ima_wav \
    --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample \
    --enable-w32threads --extra-ldflags='-static-libgcc' --host-cc=clang >/dev/null
make -j\$(nproc) >/dev/null
make install >/dev/null
i686-w64-mingw32-objdump -p /src/third_party/ffmpeg/bin/avcodec-*.dll | grep 'DLL Name'
"
ls -la "$REPO/third_party/ffmpeg/bin/"*.dll
