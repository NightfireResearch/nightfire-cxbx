#!/bin/sh
# Rebuilds the minimal FFmpeg the FMV player (src/action/engine/Fmv.cpp) needs, from the third_party/ffmpeg submodule,
# into third_party/ffmpeg-prebuilt: 32-bit Windows DLLs with just the XMV demuxer, the WMV2 video decoder and the
# Xbox IMA ADPCM audio decoder, their headers, FFmpeg's licence and a note of how they were built. LGPL (no GPL parts),
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

# The configuration, also written into the README beside the DLLs
OPTIONS='--target-os=mingw32 --arch=x86 --cross-prefix=i686-w64-mingw32- --enable-cross-compile
    --enable-shared --disable-static --disable-programs --disable-doc --disable-network --disable-autodetect
    --disable-everything --enable-protocol=file --enable-demuxer=xmv --enable-decoder=wmv2,adpcm_ima_xbox,adpcm_ima_wav
    --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample
    --enable-w32threads --extra-ldflags=-static-libgcc'

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

if command -v i686-w64-mingw32-gcc >/dev/null 2>&1 && command -v make >/dev/null 2>&1 && command -v nasm >/dev/null 2>&1; then
    ROOT="$REPO" OPTIONS="$OPTIONS" sh -c "$BUILD"
else
    MOUNT=$(cygpath -m "$REPO" 2>/dev/null || echo "$REPO")
    MSYS_NO_PATHCONV=1 docker run --rm -v "$MOUNT:/src" -e ROOT=/src -e OPTIONS="$OPTIONS" nf-cross bash -c "
        apt-get update -qq >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq make nasm >/dev/null 2>&1
        $BUILD"
fi

# What the build and the game need: the DLLs, the headers, the licence and how they were made
OUT="$REPO/third_party/ffmpeg-prebuilt"
rm -rf "$OUT"
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

To rebuild: git submodule update --init third_party/ffmpeg, then sh tools/fmv/build_ffmpeg.sh.
EOF
ls -la "$OUT/bin"
