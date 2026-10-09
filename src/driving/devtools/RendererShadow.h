#ifndef DRIVING_DEVTOOLS_RENDERERSHADOW_H_
#define DRIVING_DEVTOOLS_RENDERERSHADOW_H_

// NIGHTFIRE_RENDERERSHADOW=1: the renderer core (render/Renderer.cpp, Draw.cpp, Fog.cpp, Materials.cpp) against the
// originals - the pure maths, the fog, the simple draws and batches, the materials' draws, the draw group's vector,
// the instance draws and RRenderSharedData, with the calls they make into EAGL and the other render packages
// recorded by fakes. See RendererShadow.cpp. Run from the first simulation tick, once the track is loaded.
void RendererShadow_Run(void);

#endif // DRIVING_DEVTOOLS_RENDERERSHADOW_H_
