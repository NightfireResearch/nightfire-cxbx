#ifndef LOWLEVEL_H
#define LOWLEVEL_H

#include "../actionhelpers.h"

// Boot-time graphics setup after xboxInitGraphics: gamma, the widescreen switches, and the loading-screen dot
void Graphics_Init_LowLevel(void);

// The loading screen's progress dots: shown between LoadingScreenMakeVisible and LoadingScreenMakeInvisible,
// one frame drawn each time a blocking load polls
void ShowLoadProgressScreen(void);
void LoadingScreenMakeVisible(void);
void LoadingScreenMakeInvisible(void);

#endif // LOWLEVEL_H
