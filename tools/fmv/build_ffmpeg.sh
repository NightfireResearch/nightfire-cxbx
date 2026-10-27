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
# "sh tools/fmv/build_ffmpeg.sh windows-x86-gc" builds the i686 DLLs with the GameCube's movies too (HVQM4 video and
# its IMA ADPCM audio: docs/gc-data.md) into third_party/ffmpeg-prebuilt-gc, for the action engine's
# NIGHTFIRE_ACTION_DATA_GC build only; the default build keeps third_party/ffmpeg-prebuilt. It needs FFmpeg 8.1 (the
# audio decoder is new in 8.1; the submodule is 8.0), named by FFMPEG_SOURCE, and applies tools/fmv/patches (the HVQM4
# demuxer and video decoder, which FFmpeg does not have) to a copy of it:
#
#   git clone -c core.autocrlf=false --depth 1 --branch n8.1.3 https://git.ffmpeg.org/ffmpeg.git ../ffmpeg-8.1
#   FFMPEG_SOURCE=../ffmpeg-8.1 sh tools/fmv/build_ffmpeg.sh windows-x86-gc
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
SOURCE=$(cd "$SOURCE" && pwd)
if [ ! -f "$SOURCE/configure" ]; then
    echo "$SOURCE is empty: run git submodule update --init $SRC" >&2
    exit 1
fi
PATCHES=
if [ "$1" = windows-x86-gc ]; then
    case "$(cat "$SOURCE/RELEASE")" in
        8.1*) ;;
        *) echo "windows-x86-gc needs FFmpeg 8.1 in FFMPEG_SOURCE, not $(cat "$SOURCE/RELEASE")" >&2; exit 1 ;;
    esac
    PATCHES=tools/fmv/patches
fi
if [ "$SOURCE" != "$REPO/$SRC" ] && head -n 1 "$SOURCE/configure" | grep -q "$(printf '\r')"; then
    echo "$SOURCE has CRLF line endings: check it out with core.autocrlf=false" >&2
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
if [ "$1" = windows-x86-gc ]; then
    COMMON="$COMMON --enable-demuxer=hvqm4 --enable-decoder=hvqm4,adpcm_ima_hvqm4"
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
rm -rf build/ffmpeg-build build/ffmpeg build/ffmpeg-src
if [ -n "$PATCHES" ]; then
    cp -R "$FFSRC" build/ffmpeg-src
    for p in "$ROOT/$PATCHES"/*.patch; do patch -s -p1 -d build/ffmpeg-src < "$p"; done
    FFSRC="$ROOT/build/ffmpeg-src"
fi
mkdir -p build/ffmpeg-build
cd build/ffmpeg-build
HOSTCC=$(command -v gcc || command -v clang || command -v cc)
"$FFSRC/configure" --prefix="$ROOT/build/ffmpeg" --host-cc="$HOSTCC" $OPTIONS >/dev/null
make -j"${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}" >/dev/null
make install >/dev/null
'

if command -v $TRIPLE-gcc >/dev/null 2>&1 && command -v make >/dev/null 2>&1 && command -v nasm >/dev/null 2>&1 &&
   { [ -z "$PATCHES" ] || command -v patch >/dev/null 2>&1; }; then
    ROOT="$REPO" FFSRC="$SOURCE" PATCHES="$PATCHES" OPTIONS="$OPTIONS" sh -c "$BUILD"
else
    PACKAGES="make nasm patch"
    [ "$ARCH" = x86_64 ] && PACKAGES="$PACKAGES gcc-mingw-w64-x86-64"
    MOUNT=$(cygpath -m "$REPO" 2>/dev/null || echo "$REPO")
    FFMOUNT=$(cygpath -m "$SOURCE" 2>/dev/null || echo "$SOURCE")
    MSYS_NO_PATHCONV=1 docker run --rm -v "$MOUNT:/src" -v "$FFMOUNT:/ffmpeg:ro" -e ROOT=/src -e FFSRC=/ffmpeg \
        -e PATCHES="$PATCHES" -e OPTIONS="$OPTIONS" -e JOBS="$JOBS" nf-cross bash -c "
        apt-get update -qq >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $PACKAGES >/dev/null 2>&1
        $BUILD"
fi

OUT="$REPO/third_party/ffmpeg-prebuilt"
if [ "$1" = windows-x86-gc ]; then
    OUT="$REPO/third_party/ffmpeg-prebuilt-gc"
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
COMMIT=$(git -C "$SOURCE" rev-parse HEAD 2>/dev/null || echo unknown)
TAG=$(git -C "$SOURCE" describe --tags 2>/dev/null || echo "release $(cat "$SOURCE/RELEASE")")
ORIGIN="Source: the third_party/ffmpeg submodule of https://github.com/NightfireResearch/nightfire-cxbx, which is FFmpeg's
own repository at commit $COMMIT (tag $TAG), unmodified."
MORE='bin-x64 holds the same for x86-64 (--arch=x86_64 --cross-prefix=x86_64-w64-mingw32-, the rest as above), for the
64-bit standalone driving engine.

To rebuild: git submodule update --init third_party/ffmpeg, then sh tools/fmv/build_ffmpeg.sh, and
sh tools/fmv/build_ffmpeg.sh windows-x86_64 for bin-x64.'
if [ "$1" = windows-x86-gc ]; then
    ORIGIN="Source: FFmpeg's own repository (https://git.ffmpeg.org/ffmpeg.git) at commit $COMMIT (tag $TAG), modified by
the patches in tools/fmv/patches of https://github.com/NightfireResearch/nightfire-cxbx, which add an HVQM4 demuxer and
video decoder (Paul B Mahol's unmerged patches of July 2023, changed: each patch and file says what, by whom and
when) and build the adpcm_ima_hvqm4 decoder without --enable-gpl:
$(cd "$REPO/$PATCHES" && ls *.patch | sed 's/^/    /')"
    MORE="For the action engine's NIGHTFIRE_ACTION_DATA_GC build: third_party/ffmpeg-prebuilt's configuration with the
GameCube's movies added (HVQM4 video, its IMA ADPCM audio).

To rebuild: FFMPEG_SOURCE=<a checkout of FFmpeg n8.1.3> sh tools/fmv/build_ffmpeg.sh windows-x86-gc."
fi
cat > "$OUT/FFmpeg-README.txt" <<EOF
These DLLs are FFmpeg $(cat "$SOURCE/RELEASE") (https://ffmpeg.org), licensed under the LGPL 2.1 or later
(FFmpeg-COPYING.LGPLv2.1.txt). They are dynamically linked: the game loads them at startup.

$ORIGIN

Built by tools/fmv/build_ffmpeg.sh with the i686 mingw-w64 cross compiler and this configuration:

    configure $(echo $OPTIONS)

$MORE
EOF
ls -la "$OUT/bin"
