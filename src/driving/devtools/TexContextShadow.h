#ifndef DRIVING_DEVTOOLS_TEXCONTEXTSHADOW_H_
#define DRIVING_DEVTOOLS_TEXCONTEXTSHADOW_H_

// NIGHTFIRE_TEXCONTEXTSHADOW=1: the R_D ports (render/StateManager.cpp, TextureContext.cpp, SkeletalObj.cpp,
// TimeData.cpp and the trees under them) against the originals. See TexContextShadow.cpp. Run from the first
// simulation tick, once the track's texture contexts are loaded.
void TexContextShadow_Run(void);

#endif // DRIVING_DEVTOOLS_TEXCONTEXTSHADOW_H_
