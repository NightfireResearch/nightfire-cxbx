#ifndef D3DSEAM_H_
#define D3DSEAM_H_

#include "../../actionhelpers.h"
#include "d3dhelpers.h"

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

// Forces surface level 0 of the given texture slot into existence via D3D8's own D3DTexture_GetSurfaceLevel2
// (result discarded - called purely for the side effect of materializing the surface). No-ops on an empty slot.
void d3dGetTextureSurfaceLevel0(int textureSlot);

// Enables/disables fog and sets the fog colour. Both share a "fog enabled" cache and a colour cache, and
// both mask the colour to 0 rather than the real cached value while a separate mode flag is set (preserved
// exactly from the original; the mode flag's own meaning hasn't been traced).
void d3dSetFogEnable(int enable);
void d3dSetFogColor(unsigned int colour);

// Thin wrapper around D3D8's own D3DDevice_SetRenderState_YuvEnable.
void d3dSetYuvEnable(int enable);

// Presents the frame via D3D8's own D3DDevice_Swap, and updates the frame-timing accumulator used elsewhere
// for FPS-style bookkeeping. Only two of the setTexture family are declared here (see d3dSeam.cpp) - the two
// distinctly-named, disambiguated d3dSetTextureStage0/1 (previously a single ambiguous "d3dSetTexture" name
// shared by two different functions in Ghidra - renamed there first so AUTOINJECT could target them safely).
void d3dSwap(void);
void d3dSetTextureStage0(int textureSlot);
void d3dSetTextureStage1(int textureSlot, int param2);

// Shader constants. colour is a packed 0xAARRGGBB value; near_/far_ feed a shared fog {1/(far-near),
// near/(far-near)} constant; intensity drives a character-lighting blend factor. d3dBeginFrame is the
// per-frame counterpart to d3dSwap - resets a few per-frame caches and no-ops if a frame is already pending.
void d3dSetColorConstant67(unsigned int colour);
void d3dSetFogNear(float near_);
void d3dSetFogFar(float far_);
void gfxSetCharacterLightIntensity(float intensity);
void d3dBeginFrame(void);

// Matrices and stream sources. d3dSetMatrix combines the given matrix with a cached "view" matrix (shader
// constant 0x60) plus a secondary basis constant (register 100); d3dSetProjectionMatrix additionally derives
// the fog Z-scale and depth-clip planes from the projection matrix; d3dSetWorldMatrix splits a world matrix
// into translation (via d3dSetMatrix) and rotation-only (shader constant 0) parts.
//
// maybeBuildAndSetModelViewProjectionMtx (constant 0x77) is deliberately NOT here yet - it passes its own
// local stack variable through maybeMultiplyMatrixChain's chain-node parameter, and that function's internal
// chain-walk semantics haven't been fully verified; faithfully reproducing it needs matching the original
// compiler's exact stack layout, which isn't reliable with plain C++ locals.
void d3dSetProjectionMatrix(D3DMATRIX *projMtx);
void d3dSetMatrix(D3DMATRIX *d3dMtx);
void d3dSetWorldMatrix(D3DMATRIX *worldMtx);
void d3dSetStreamSources(int baseIndex, int stream1Offset, float stream1Stride, int stream2Offset, float stream2Stride,
                          int stream3Offset, float stream3Stride, int stream4Offset, float stream4Stride,
                          int stream5Offset, float stream5Stride, int stream6Offset, float stream6Stride,
                          int stream7Offset, float stream7Stride, int stream8Offset, float stream8Stride);

// Binds a stream/index buffer pair by handle (no-ops if unchanged), and draws an ad-hoc non-indexed triangle
// list ("shard" - glass/debris fragments) directly from a caller-supplied vertex buffer.
void d3dBindBuffers(int streamBufferHandle, int indexBufferHandle);
void drawShard(void *data, int countTris);

#endif // D3DSEAM_H_
