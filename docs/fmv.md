# FMV playback

The action engine's movies (the attract loop, the menu backdrops, the briefings) are 25 `.xmv` files in
`eurocom/25_fps/`, 452 MB: WMV2 video at 640x480 and 25 fps, with Xbox IMA ADPCM audio, either one stereo track or
four (one per language). The game plays them through five functions of its own (0x000e8a00-0x000e8cf0). Originally
those drove Microsoft's XMV decoder, which sits in the XBE's `XMV` section: 160 KB of WMV2 decoding with about 2,700
hand-written MMX instructions. It was the last XDK library the game ran, and the only thing that still reached
DirectSound.

`src/action/engine/Fmv.cpp` is those five functions, reimplemented on FFmpeg. With them, nothing reaches the XMV
library or DirectSound. There is no fallback to the original decoder.

## FFmpeg

FFmpeg is the `third_party/ffmpeg` submodule, pinned at release 8.0.3. 8.0 is the first release with a dedicated Xbox
IMA ADPCM decoder. `tools/fmv/build_ffmpeg.sh` builds it into `build/ffmpeg`:

```
git submodule update --init third_party/ffmpeg
sh tools/fmv/build_ffmpeg.sh
```

It produces three 32-bit Windows DLLs (`avutil-60`, `avcodec-62`, `avformat-62`, about 2.2 MB together). They are
cut down to the XMV demuxer and the WMV2 and Xbox ADPCM decoders, are LGPL only with no GPL parts, and depend only on
`kernel32` and `msvcrt`. The script builds natively where the i686 mingw-w64 compiler, `make` and `nasm` are present
(CI, Linux, macOS), and in the `nf-cross` Docker image otherwise (Windows). It also switches the submodule to LF line
endings if a Windows checkout gave its scripts CRLF ones.

CMake requires `build/ffmpeg` (or `NF_FFMPEG_DIR`). It copies the DLLs, FFmpeg's licence and a note of where the
source is next to the executables. CI builds FFmpeg once per submodule commit in its own job (cached) and hands it to
the Windows, macOS and Linux builds, so every artifact is complete: the only thing a player supplies is the game
disc. That is how the LGPL is met: dynamic linking, the licence shipped alongside, and the exact source and
configuration in this repository.

The game loads the DLLs at startup (`Fmv_Init`). If they are missing it says so and exits.

## How it plays

The player keeps the originals' contract. Their callers are ours (`psiStartBackgroundMovie`,
`maybeStartBackgroundMovie`, `psiStopBackgroundMovie` in `game.cpp`), plus three original shutdown and launch
functions that call the cleanup.

- **The file:** `d:\eurocom\25_fps\` (`30_fps\` on a PAL-60 TV, as in the original), through our path mapping.
- **The audio track:** chosen by language as the original chooses it. A movie with fewer tracks plays the first.
- **The picture:** decoded into two of the game's own YUY2 textures (`RegisterTexture` format 9), converted from
  FFmpeg's 4:2:0. The original's draw calls are kept unchanged, so the backend's YUY2 upload does the rest.
- **The clock:** a frame is shown once the audio played reaches its timestamp. Frames that fall behind are skipped.
  Without audio, or after the audio has run out, the wall clock takes over.
- **The audio:** goes to an XAudio2 voice of its own on the game's device, about half a second ahead, with the volume
  through the game's volume table. The movie volume (0x1b52e0) is ours.

Checked against the original decoder on the boot movies, the pre-menu movie (four language tracks), the looping
menu backdrop and the letterboxed attract movie. Frames match to within rounding (a mean of under 1 level in 255), and
the game moves on at each movie's end as before. FMV subtitles are script-player scripts on the game's own clock and
are unaffected.

## The driving engine

The driving engine's movies are a different case, so nothing here applies to them yet. They are 12 EA Madcow `.mad`
files with EA ADPCM audio (an EA SCHl stream), packed inside `driving/misc.viv` (an EA BIG archive). They are decoded
by EA's own player code inside `Driving.xbe`'s `.text`; there is no XDK movie library. FFmpeg decodes both formats
(the `ea` demuxer, the `eamad` and `adpcm_ea` decoders), so the same build could be extended. That still needs the
driving engine's movie functions mapped, and the archive read in memory, first.
