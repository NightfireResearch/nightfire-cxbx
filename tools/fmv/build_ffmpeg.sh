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
# "sh tools/fmv/build_ffmpeg.sh windows-x86-ps2" builds the i686 DLLs with the PS2's movies too (MPEG program streams,
# MPEG-2 video: docs/ps2-data.md) into third_party/ffmpeg-prebuilt-ps2, for the action engine's
# NIGHTFIRE_ACTION_DATA_PS2 build only; the default build keeps third_party/ffmpeg-prebuilt. FFMPEG_SOURCE names the
# submodule's checkout when this one has none (a worktree).
#
# "sh tools/fmv/build_ffmpeg.sh windows-x86_64" builds the same DLLs for x86-64, for the 64-bit standalone driving
# engine (docs/driving/64-bit.md), into third_party/ffmpeg-prebuilt/bin-x64 (committed too); the headers, licence
# and README stay the i686 build's, which they match.
#
# The Linux build (docs/driving/portable.md) needs the same release as shared libraries for the host, which are not
# committed: "sh tools/fmv/build_ffmpeg.sh linux-i386 <prefix>" builds them for 32-bit x86 Linux with gcc -m32, make
# and nasm into <prefix>/lib, for the linux-i386 preset's NF_FFMPEG_LIB_DIR; "linux-x86_64 <prefix>" builds them for
# x86-64 Linux, for the linux-x64 preset's; "macos-arm64 <prefix>" builds them on an Apple Silicon Mac with Apple's clang
# (no VideoToolbox: autodetection stays off), named by @rpath, for the macos-arm64 preset's. FFMPEG_SOURCE names
# another checkout of the submodule's commit.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SRC=third_party/ffmpeg

# Everything but the target, shared by every build
COMMON='--enable-shared --disable-static --disable-programs --disable-doc --disable-network --disable-autodetect
    --disable-everything --enable-protocol=file --enable-demuxer=xmv --enable-decoder=wmv2,adpcm_ima_xbox,adpcm_ima_wav,eamad,adpcm_ea_r1
    --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample'

if [ "$1" = linux-i386 ] || [ "$1" = linux-x86_64 ] || [ "$1" = macos-arm64 ]; then
    PREFIX=${2:?usage: build_ffmpeg.sh linux-i386|linux-x86_64|macos-arm64 <prefix>}
    SOURCE=${FFMPEG_SOURCE:-$REPO/$SRC}
    mkdir -p "$PREFIX/build"
    PREFIX=$(cd "$PREFIX" && pwd)
    cd "$PREFIX/build"
    if [ "$1" = linux-i386 ]; then
        # -mstackrealign: the game calls in with Windows' 4-byte stack alignment (cmake/xbox-abi-linux.cmake)
        "$SOURCE/configure" --prefix="$PREFIX" --arch=x86 --cc='gcc -m32' --extra-cflags=-mstackrealign \
            --extra-ldflags=-m32 $COMMON --enable-pthreads >/dev/null
    elif [ "$1" = macos-arm64 ]; then
        "$SOURCE/configure" --prefix="$PREFIX" --arch=aarch64 --cc=clang --extra-cflags=-mmacosx-version-min=15.0 \
            --extra-ldflags=-mmacosx-version-min=15.0 --install-name-dir=@rpath $COMMON --enable-pthreads >/dev/null
    else
        "$SOURCE/configure" --prefix="$PREFIX" --arch=x86_64 --cc=gcc $COMMON --enable-pthreads >/dev/null
    fi
    make -j"${JOBS:-4}" >/dev/null
    make install >/dev/null
    ls -la "$PREFIX/lib"
    exit 0
fi

SOURCE=${FFMPEG_SOURCE:-$REPO/$SRC}
if [ ! -f "$SOURCE/configure" ]; then
    echo "$SOURCE is empty: run git submodule update --init $SRC" >&2
    exit 1
fi
# A Windows checkout with core.autocrlf=true gives the scripts CRLF line endings, which configure cannot run with
if [ "$SOURCE" = "$REPO/$SRC" ] && head -n 1 "$REPO/$SRC/configure" | grep -q "$(printf '\r')"; then
    echo "Re-checking out $SRC with LF line endings"
    git -C "$REPO/$SRC" config core.autocrlf false
    git -C "$REPO/$SRC" rm -q --cached -r .
    git -C "$REPO/$SRC" reset -q --hard
fi

ARCH=x86
TRIPLE=i686-w64-mingw32
if [ "$1" = windows-x86-ps2 ]; then
    # (the raw MPEG video demuxer is what the program stream demuxer probes its video streams with)
    COMMON="$COMMON --enable-demuxer=mpegps,mpegvideo --enable-decoder=mpeg2video --enable-parser=mpegvideo"
fi
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
"$FFSRC/configure" --prefix="$ROOT/build/ffmpeg" --host-cc="$HOSTCC" $OPTIONS >/dev/null
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null
make install >/dev/null
'

if command -v $TRIPLE-gcc >/dev/null 2>&1 && command -v make >/dev/null 2>&1 && command -v nasm >/dev/null 2>&1; then
    ROOT="$REPO" FFSRC="$SOURCE" OPTIONS="$OPTIONS" sh -c "$BUILD"
else
    PACKAGES="make nasm"
    [ "$ARCH" = x86_64 ] && PACKAGES="$PACKAGES gcc-mingw-w64-x86-64"
    MOUNT=$(cygpath -m "$REPO" 2>/dev/null || echo "$REPO")
    FFMOUNT=$(cygpath -m "$SOURCE" 2>/dev/null || echo "$SOURCE")
    MSYS_NO_PATHCONV=1 docker run --rm -v "$MOUNT:/src" -v "$FFMOUNT:/ffmpeg:ro" -e ROOT=/src -e FFSRC=/ffmpeg \
        -e OPTIONS="$OPTIONS" nf-cross bash -c "
        apt-get update -qq >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $PACKAGES >/dev/null 2>&1
        $BUILD"
fi

OUT="$REPO/third_party/ffmpeg-prebuilt"
if [ "$1" = windows-x86-ps2 ]; then
    OUT="$REPO/third_party/ffmpeg-prebuilt-ps2"
fi
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
cp "$SOURCE/COPYING.LGPLv2.1" "$OUT/FFmpeg-COPYING.LGPLv2.1.txt"
MORE='bin-x64 holds the same for x86-64 (--arch=x86_64 --cross-prefix=x86_64-w64-mingw32-, the rest as above), for the
64-bit standalone driving engine.

To rebuild: git submodule update --init third_party/ffmpeg, then sh tools/fmv/build_ffmpeg.sh, and
sh tools/fmv/build_ffmpeg.sh windows-x86_64 for bin-x64.'
if [ "$1" = windows-x86-ps2 ]; then
    MORE="For the action engine's NIGHTFIRE_ACTION_DATA_PS2 build: third_party/ffmpeg-prebuilt's configuration with the PS2's
movies added (MPEG program streams, MPEG-2 video).

To rebuild: git submodule update --init third_party/ffmpeg, then sh tools/fmv/build_ffmpeg.sh windows-x86-ps2."
fi
cat > "$OUT/FFmpeg-README.txt" <<EOF
These DLLs are FFmpeg $(cat "$SOURCE/RELEASE") (https://ffmpeg.org), licensed under the LGPL 2.1 or later
(FFmpeg-COPYING.LGPLv2.1.txt). They are dynamically linked: the game loads them at startup.

Source: the third_party/ffmpeg submodule of https://github.com/NightfireResearch/nightfire-cxbx, which is FFmpeg's
own repository at commit $(git -C "$SOURCE" rev-parse HEAD) (tag $(git -C "$SOURCE" describe --tags)), unmodified.

Built by tools/fmv/build_ffmpeg.sh with the i686 mingw-w64 cross compiler and this configuration:

    configure $(echo $OPTIONS)

$MORE
EOF
ls -la "$OUT/bin"
