#ifndef DRIVING_DEVTOOLS_RENDERSHADOW_H_
#define DRIVING_DEVTOOLS_RENDERSHADOW_H_

// NIGHTFIRE_RENDERSHADOW=1: world/Render.cpp's culling (the tree walks, the culling's walk over the visible
// instances, CopyDrawPasses) and its std::sort against the originals. See RenderShadow.cpp. Run from the first
// simulation tick, once the track and fgRender exist.
void RenderShadow_Run(void);

#endif // DRIVING_DEVTOOLS_RENDERSHADOW_H_
