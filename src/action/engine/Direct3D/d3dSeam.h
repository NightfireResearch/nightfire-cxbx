#ifndef D3DSEAM_H_
#define D3DSEAM_H_

#include "../../actionhelpers.h"

// "Thin seam" reimplementation of Eurocom's own small wrapper functions that sit directly on top of the
// statically-linked D3D8 library CXBX still hooks via its own OOVPA pattern matching. See d3dSeam.cpp's
// top-of-file comment for the full rationale - every function declared here still calls through to the
// same, completely untouched D3D8:: functions the original did, so CXBX's own D3D8 emulation keeps working
// exactly as it does today. This is deliberately NOT yet the full DX9 switch.

// Every texture the game creates funnels through this one function, which creates/registers a texture
// resource with D3D8 and returns its slot index into Gfx's 2048-entry texture table (0 on failure). "data"
// is a pointer to the pixel data buffer to associate with the new texture (see the caller in
// Graphics_Init_LowLevel for an example - it passes a freshly-allocated buffer, not existing pixel data).
int RegisterTexture(unsigned int width, unsigned int height, int formatType, unsigned int levels, void *data, int param_6);

// These three all hardcode one specific NV2A render state apiece and share a dirty-flag cache + "is the
// device ready yet" guard, funnelled through D3D8's generic (and, unusually, custom-register-convention -
// see D3D_SetRenderStateSimple in the .cpp) D3DDevice_SetRenderState_Simple.
void d3dSetRenderState(int enableDepthTest);   // NV097_SET_DEPTH_FUNC: 0 -> ALWAYS, nonzero -> LEQUAL
void d3dSetRenderState1(int alphaRef);         // NV097_SET_ALPHA_REF
void d3dSetRenderState2(int zWriteEnable);     // NV097_SET_DEPTH_MASK

// Thin wrapper around D3D8's own D3DDevice_SetRenderState_CullMode (confirmed plain __stdcall, no dirty-
// flag cache unlike the three above).
void d3dSetCullMode(int cullEnabled);

// Builds a gamma-ramp table from a brightness/gamma/contrast-style triplet and uploads it via D3D8's own
// D3DDevice_SetGammaRamp (no-op if the device isn't ready yet).
void ConfigureGammaRamp(float gamma, float brightness, float contrast);

// Releases a D3D8 resource (texture, vertex buffer, etc.) via D3D8's own D3DResource_Release. NULL-safe,
// matching the original.
void D3DResourceRelease(void *resource);

// Sets up the render viewport (clamping width/height to a minimum of 2, matching the original) and uploads
// it via D3D8's own D3DDevice_SetViewport if the device is ready.
void d3dSetupViewportDimensions(unsigned int viewportX, unsigned int viewportY, unsigned int viewportWidth, unsigned int viewportHeight);

// Clears the target and/or Z/stencil buffer via D3D8's own D3DDevice_Clear. colour is a raw 0xRRGGBB value
// (alpha byte masked off); z is always cleared to 1.0f and stencil to 0, matching the original exactly.
void d3dClear(unsigned int colour, bool clearTarget, bool clearZStencil);

#endif // D3DSEAM_H_
