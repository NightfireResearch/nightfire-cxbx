#ifndef DRIVING_DEVTOOLS_RENDERFXSHADOW_H_
#define DRIVING_DEVTOOLS_RENDERFXSHADOW_H_

// NIGHTFIRE_RENDERFXSHADOW=1: the lightning, the shadow map's queries and camera, the glare types' texture
// coordinates, the broken windows' batch and pane walk, the debris tables and the scorch marks' ground normal
// (render/Lightning.cpp, ShadowMap.cpp, RGlareManager.cpp, SkyWater.cpp, PostProcessing.cpp) against the originals.
// Call once the track is loaded (the first simulation tick); returns at once unless the variable is set.
void RenderFxShadow_Run(void);

#endif // DRIVING_DEVTOOLS_RENDERFXSHADOW_H_
