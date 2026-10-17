#ifndef DRIVING_DEVTOOLS_GAMEFRONTENDSHADOWC_H_
#define DRIVING_DEVTOOLS_GAMEFRONTENDSHADOWC_H_

// NIGHTFIRE_GAMEFRONTENDSHADOWC=1: the galleries, the loading screen and the pause menu's figure (game/Gallery.cpp,
// EffectsGallery.cpp, LoadingScreen.cpp, Girl.cpp) against the originals, on the live HUD and effects galleries and
// synthetic state, every draw, allocation and sound call recorded by fakes, once, from the first simulation tick.
// See GameFrontEndShadowC.cpp.
void GameFrontEndShadowC_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEFRONTENDSHADOWC_H_
