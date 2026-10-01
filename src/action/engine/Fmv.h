#ifndef FMV_H_
#define FMV_H_

// FMV playback through FFmpeg, in place of the XMV decoder linked into the XBE. See Fmv.cpp.
//
// Replaces the game's five movie functions (BackgroundMoviePlayFile, maybeDecodeMpgAudio, maybeBackgroundMovieCleanup,
// maybeBackgroundMovieIsPlaying, BackgroundMovieSetVolume) at their own addresses, but only when the FFmpeg DLLs
// load and the game runs standalone on the D3D9 backend; otherwise the original decoder stays in charge. Call once
// at startup, after the DirectSound stream hooks.
void Fmv_InstallHooks(void);

#endif // FMV_H_
