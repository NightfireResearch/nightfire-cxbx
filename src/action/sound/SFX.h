#ifndef SFX_H_
#define SFX_H_

#include "../actionhelpers.h"

bool SFXIsSFXPlaying(DYNAMICSOUNDS *param_1,int param_2);
void SFXStartMusic(int track, undefined4 param_2);
void __stdcall SFXStopMusic(void);
void SFXMusicSetVolume(int volume);
int __stdcall SFXMusicGetVolume(void);
void SFXFadeDown(undefined4 param_1);


#endif // SFX_H_