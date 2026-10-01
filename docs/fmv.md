# FMV playback

The action engine's movies (the attract loop, the menu backdrops, the briefings) are 25 `.xmv` files in
`eurocom/25_fps/`, 452 MB: WMV2 video at 640x480 and 25 fps, with Xbox IMA ADPCM audio, either one stereo track or
four (one per language). The game plays them through five functions of its own (0x000e8a00-0x000e8cf0) over
Microsoft's XMV decoder, which sits in the XBE's `XMV` section: 160 KB of WMV2 decoding with about 2,700 hand-written MMX
instructions, the last XDK library the game ran.

`src/action/engine/Fmv.cpp` replaces those five functions with a player built on FFmpeg. With it, nothing reaches the
XMV library any more. DirectSound was already out of the picture: the decoder's stream calls were answered by
`sound/dsndStream.cpp`.

## Building FFmpeg

```
sh tools/fmv/build_ffmpeg.sh
```

This builds FFmpeg 8.0.3 in the `nf-cross` Docker image as 32-bit Windows DLLs (`avutil-60`, `avcodec-62`,
`avformat-62`, about 2.2 MB together). It is cut down to the XMV demuxer and the WMV2 and Xbox ADPCM decoders, and it
is LGPL only, with no GPL parts and no external dependencies. The output goes to `third_party/ffmpeg/`, which is
gitignored. CMake picks it up, defines `NF_HAVE_FFMPEG` and copies the DLLs next to the executables. Re-run
`cmake . -A Win32` after building it for the first time.

The DLLs are loaded at run time, and the five functions are replaced only when they load, the game runs standalone and
the graphics backend is D3D9. Otherwise the log says so and the XBE's own decoder plays the movies, as before.

## How it plays

The player keeps the originals' contract, and those functions' callers are ours (`psiStartBackgroundMovie`,
`maybeStartBackgroundMovie`, `psiStopBackgroundMovie` in `game.cpp`):

- **The file:** `d:\eurocom\25_fps\` (`30_fps\` on a PAL-60 TV), through our path mapping.
- **The audio track:** chosen by language as the original chooses it. A movie with fewer tracks plays the first.
- **The picture:** decoded into two of the game's own YUY2 textures (`RegisterTexture` format 9), converted from
  FFmpeg's 4:2:0. The original's draw calls are kept unchanged, so the backend's existing YUY2 upload does the rest.
- **The clock:** a frame is shown once the audio played reaches its timestamp. Frames that fall behind are skipped.
  Without audio, or after the audio has run out, the wall clock takes over.
- **The audio:** goes to an XAudio2 voice of its own on the game's device, about half a second ahead, with the volume
  through the game's volume table.

Checked against the original decoder on the boot movies, the pre-menu movie (four language tracks), the looping
menu backdrop and the letterboxed attract movie. Frames match to within rounding (a mean of under 1 level in 255), and
the game moves on at each movie's end as before.
