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

// An identity matrix of the low level drawing (LowLevel.cpp), also the matrix models are drawn with
extern _MATRIX LowLevelIdentityMatrix;

#endif // LOWLEVEL_H
