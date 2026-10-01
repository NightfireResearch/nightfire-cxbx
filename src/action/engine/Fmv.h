#ifndef FMV_H_
#define FMV_H_

// FMV playback through FFmpeg, in place of the XMV decoder linked into the XBE. See Fmv.cpp and docs/fmv.md.

// Loads the FFmpeg DLLs; stops the game with a message if they are missing. Once, at startup.
void Fmv_Init(void);

// The game's five movie functions (0x000e8a00-0x000e8cf0)
void BackgroundMoviePlayFile(char *filename);   // d:\eurocom\25_fps\<filename>; the first frame is ready on return
bool __stdcall maybeDecodeMpgAudio(void);       // every frame: the next frame if due, then draws; false once ended
bool __stdcall maybeBackgroundMovieIsPlaying(void);
void BackgroundMovieSetVolume(int volume);      // 0..100, through the game's volume table
void maybeBackgroundMovieCleanup(void);         // stops and frees everything; fine with nothing playing

#endif // FMV_H_
