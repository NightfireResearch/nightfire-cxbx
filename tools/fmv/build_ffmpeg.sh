#!/bin/sh
# Rebuilds the minimal FFmpeg the FMV player (src/action/engine/Fmv.cpp) needs, from the third_party/ffmpeg submodule,
# into third_party/ffmpeg-prebuilt: 32-bit Windows DLLs with just what the movies need - the action engine's XMV
# demuxer, WMV2 video and Xbox IMA ADPCM audio, and the driving engine's EA Madcow video and EA-XA R1 audio (its
# player walks the chunks itself) - their headers, FFmpeg's licence and a note of how they were built. LGPL (no GPL parts),
# no other dependencies.
#
# The result is committed, so a normal build never runs this (CI did, and an FFmpeg build takes over ten minutes
# there). Run it after moving the submodule to another FFmpeg release or changing the configuration below, and commit
# third_party/ffmpeg-prebuilt with the submodule:
#
#   git submodule update --init third_party/ffmpeg
#   sh tools/fmv/build_ffmpeg.sh
#
# Builds natively where the i686 mingw-w64 compiler, make and nasm are installed (Linux, macOS); otherwise (Windows)
# in the nf-cross Docker image, installing make and nasm into a throwaway container.
#
# "sh tools/fmv/build_ffmpeg.sh windows-x86_64" builds the same DLLs for x86-64, for the 64-bit standalone driving
# engine (docs/driving/64-bit.md), into third_party/ffmpeg-prebuilt/bin-x64 (committed too); the headers, licence
# and README stay the i686 build's, which they match.
#
# The Linux build (docs/driving/portable.md) needs the same release as shared libraries for the host, which are not
# committed: "sh tools/fmv/build_ffmpeg.sh linux-i386 <prefix>" builds them for 32-bit x86 Linux with gcc -m32, make
# and nasm into <prefix>/lib, for the linux-i386 preset's NF_FFMPEG_LIB_DIR. FFMPEG_SOURCE names another checkout of
# the submodule's commit.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SRC=third_party/ffmpeg

# Everything but the target, shared by every build
COMMON='--enable-shared --disable-static --disable-programs --disable-doc --disable-network --disable-autodetect
    --disable-everything --enable-protocol=file --enable-demuxer=xmv --enable-decoder=wmv2,adpcm_ima_xbox,adpcm_ima_wav,eamad,adpcm_ea_r1
    --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample'

if [ "$1" = linux-i386 ]; then
    PREFIX=${2:?usage: build_ffmpeg.sh linux-i386 <prefix>}
    SOURCE=${FFMPEG_SOURCE:-$REPO/$SRC}
    mkdir -p "$PREFIX/build"
    PREFIX=$(cd "$PREFIX" && pwd)
    cd "$PREFIX/build"
    # -mstackrealign: the game calls in with Windows' 4-byte stack alignment (cmake/xbox-abi-linux.cmake)
    "$SOURCE/configure" --prefix="$PREFIX" --arch=x86 --cc='gcc -m32' --extra-cflags=-mstackrealign \
        --extra-ldflags=-m32 $COMMON --enable-pthreads >/dev/null
    make -j"${JOBS:-4}" >/dev/null
    make install >/dev/null
    ls -la "$PREFIX/lib"
    exit 0
fi

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

ARCH=x86
TRIPLE=i686-w64-mingw32
if [ "$1" = windows-x86_64 ]; then
    ARCH=x86_64
    TRIPLE=x86_64-w64-mingw32
fi

# The configuration, also written into the README beside the DLLs
OPTIONS="--target-os=mingw32 --arch=$ARCH --cross-prefix=$TRIPLE- --enable-cross-compile
    $COMMON
    --enable-w32threads --extra-ldflags=-static-libgcc"

BUILD='
set -e
cd "$ROOT"
rm -rf build/ffmpeg-build build/ffmpeg
mkdir -p build/ffmpeg-build
cd build/ffmpeg-build
HOSTCC=$(command -v gcc || command -v clang || command -v cc)
"$ROOT/third_party/ffmpeg/configure" --prefix="$ROOT/build/ffmpeg" --host-cc="$HOSTCC" $OPTIONS >/dev/null
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null
make install >/dev/null
'

if command -v $TRIPLE-gcc >/dev/null 2>&1 && command -v make >/dev/null 2>&1 && command -v nasm >/dev/null 2>&1; then
    ROOT="$REPO" OPTIONS="$OPTIONS" sh -c "$BUILD"
else
    PACKAGES="make nasm"
    [ "$ARCH" = x86_64 ] && PACKAGES="$PACKAGES gcc-mingw-w64-x86-64"
    MOUNT=$(cygpath -m "$REPO" 2>/dev/null || echo "$REPO")
    MSYS_NO_PATHCONV=1 docker run --rm -v "$MOUNT:/src" -e ROOT=/src -e OPTIONS="$OPTIONS" nf-cross bash -c "
        apt-get update -qq >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $PACKAGES >/dev/null 2>&1
        $BUILD"
fi

OUT="$REPO/third_party/ffmpeg-prebuilt"
if [ "$ARCH" = x86_64 ]; then
    rm -rf "$OUT/bin-x64"
    mkdir -p "$OUT/bin-x64"
    cp "$REPO"/build/ffmpeg/bin/*.dll "$OUT/bin-x64/"
    ls -la "$OUT/bin-x64"
    exit 0
fi

# What the build and the game need: the DLLs, the headers, the licence and how they were made
rm -rf "$OUT/bin" "$OUT/include"
mkdir -p "$OUT/bin"
cp "$REPO"/build/ffmpeg/bin/*.dll "$OUT/bin/"
cp -r "$REPO/build/ffmpeg/include" "$OUT/include"
cp "$REPO/$SRC/COPYING.LGPLv2.1" "$OUT/FFmpeg-COPYING.LGPLv2.1.txt"
cat > "$OUT/FFmpeg-README.txt" <<EOF
These DLLs are FFmpeg $(cat "$REPO/$SRC/RELEASE") (https://ffmpeg.org), licensed under the LGPL 2.1 or later
(FFmpeg-COPYING.LGPLv2.1.txt). They are dynamically linked: the game loads them at startup.

Source: the third_party/ffmpeg submodule of https://github.com/NightfireResearch/nightfire-cxbx, which is FFmpeg's
own repository at commit $(git -C "$REPO/$SRC" rev-parse HEAD) (tag $(git -C "$REPO/$SRC" describe --tags)), unmodified.

Built by tools/fmv/build_ffmpeg.sh with the i686 mingw-w64 cross compiler and this configuration:

    configure $(echo $OPTIONS)

bin-x64 holds the same for x86-64 (--arch=x86_64 --cross-prefix=x86_64-w64-mingw32-, the rest as above), for the
64-bit standalone driving engine.

To rebuild: git submodule update --init third_party/ffmpeg, then sh tools/fmv/build_ffmpeg.sh, and
sh tools/fmv/build_ffmpeg.sh windows-x86_64 for bin-x64.
EOF
ls -la "$OUT/bin"
