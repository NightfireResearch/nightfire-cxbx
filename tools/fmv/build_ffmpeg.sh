#!/bin/sh
# Builds the minimal FFmpeg the FMV player (src/action/engine/Fmv.cpp) needs, from the third_party/ffmpeg submodule:
# 32-bit Windows DLLs with just the XMV demuxer, the WMV2 video decoder and the Xbox IMA ADPCM audio decoder. LGPL
# (no GPL parts), no other dependencies. Output: build/ffmpeg/{bin,include} plus the licence; CMake copies the DLLs and
# the licence beside the executables. CI runs this too, so releases carry the DLLs.
#
#   git submodule update --init third_party/ffmpeg
#   sh tools/fmv/build_ffmpeg.sh
#
# Builds natively where the i686 mingw-w64 compiler, make and nasm are installed (CI, Linux, macOS); otherwise (Windows)
# in the nf-cross Docker image, installing make and nasm into a throwaway container.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SRC=third_party/ffmpeg

if [ ! -f "$REPO/$SRC/configure" ]; then
    echo "$SRC is empty: run git submodule update --init $SRC" >&2
    exit 1
fi
# A Windows checkout with core.autocrlf=true gives the scripts CRLF line endings, which configure cannot run with
if head -n 1 "$REPO/$SRC/configure" | grep -q "$(printf '\r')"; then
    echo "Re-checking out $SRC with LF line endings"
    git -C "$REPO/$SRC" config core.autocrlf false
    git -C "$REPO/$SRC" rm -q --cached -r .
    git -C "$REPO/$SRC" reset -q --hard
fi

BUILD='
set -e
cd "$ROOT"
rm -rf build/ffmpeg-build build/ffmpeg
mkdir -p build/ffmpeg-build
cd build/ffmpeg-build
HOSTCC=$(command -v gcc || command -v clang || command -v cc)
"$ROOT/third_party/ffmpeg/configure" --prefix="$ROOT/build/ffmpeg" \
    --target-os=mingw32 --arch=x86 --cross-prefix=i686-w64-mingw32- --enable-cross-compile --host-cc="$HOSTCC" \
    --enable-shared --disable-static --disable-programs --disable-doc --disable-network --disable-autodetect \
    --disable-everything --enable-protocol=file --enable-demuxer=xmv --enable-decoder=wmv2,adpcm_ima_xbox,adpcm_ima_wav \
    --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample \
    --enable-w32threads --extra-ldflags=-static-libgcc >/dev/null
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null
make install >/dev/null
cp "$ROOT/third_party/ffmpeg/COPYING.LGPLv2.1" "$ROOT/build/ffmpeg/FFmpeg-COPYING.LGPLv2.1.txt"
printf "These DLLs are FFmpeg %s (https://ffmpeg.org), LGPL 2.1 or later, built from the third_party/ffmpeg\nsubmodule of this project by tools/fmv/build_ffmpeg.sh, which gives the configuration.\n" \
    "$(cat "$ROOT/third_party/ffmpeg/RELEASE")" > "$ROOT/build/ffmpeg/FFmpeg-README.txt"
'

if command -v i686-w64-mingw32-gcc >/dev/null 2>&1 && command -v make >/dev/null 2>&1 && command -v nasm >/dev/null 2>&1; then
    ROOT="$REPO" sh -c "$BUILD"
else
    MOUNT=$(cygpath -m "$REPO" 2>/dev/null || echo "$REPO")
    MSYS_NO_PATHCONV=1 docker run --rm -v "$MOUNT:/src" -e ROOT=/src nf-cross bash -c "
        apt-get update -qq >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq make nasm >/dev/null 2>&1
        $BUILD"
fi
ls "$REPO"/build/ffmpeg/bin/*.dll
