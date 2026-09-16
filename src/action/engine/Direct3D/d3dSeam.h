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
//
// KNOWN ISSUE, confirmed pre-existing (not introduced by this seam): repeatedly reloading the identical
// level exhausts this table, because psiCreateMapTextures registers every one of a level's textures on
// entry (no dedup) and nothing anywhere in the compiled binary releases them again on exit. Confirmed via
// a controlled A/B test - logging every RegisterTexture call across an identical repeated-reload sequence
// on both this seam and a clean pre-seam baseline commit produced byte-for-byte identical registration
// logs; the baseline fails under the same test too, just less visibly (silently missing geometry rather
// than an explicit diagnostic). Real hardware likely never hits this in normal play. See the diagnostic
// printed from this function's own table-full path for more.
int RegisterTexture(unsigned int width, unsigned int height, int formatType, unsigned int levels, void *data, int param_6);

// Frees a texture slot returned by RegisterTexture (no-ops if its refcount is still nonzero).
void ReleaseTexture(int textureSlot);

// Forces refCount to 1 on an already-registered texture slot, so ReleaseTexture's own gate permanently
// refuses to free it - used for critical/fallback textures (error-screen font, boot-time init) that must
// never disappear.
void d3dMarkTexturePermanent(int textureSlot);

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
// maybeBuildAndSetModelViewProjectionMtx (constant 0x77) builds an inverse-MVP-style matrix from a rigid
// transform and a base matrix - like d3dSetMatrix, it computes its combine steps directly (Multiply4x4RowMajor)
// rather than through maybeMultiplyMatrixChain, since the original's own call sequence aliases dest with base
// and/or chain in all three of its multiply steps.
void d3dSetProjectionMatrix(D3DMATRIX *projMtx);
void d3dSetMatrix(D3DMATRIX *d3dMtx);
void d3dSetWorldMatrix(D3DMATRIX *worldMtx);
void maybeBuildAndSetModelViewProjectionMtx(D3DMATRIX *rigidTransform, D3DMATRIX *base);
void d3dSetStreamSources(int baseIndex, int stream1Offset, float stream1Stride, int stream2Offset, float stream2Stride,
                          int stream3Offset, float stream3Stride, int stream4Offset, float stream4Stride,
                          int stream5Offset, float stream5Stride, int stream6Offset, float stream6Stride,
                          int stream7Offset, float stream7Stride, int stream8Offset, float stream8Stride);

// Binds a stream/index buffer pair by handle (no-ops if unchanged), and draws an ad-hoc non-indexed triangle
// list ("shard" - glass/debris fragments) directly from a caller-supplied vertex buffer.
void d3dBindBuffers(int streamBufferHandle, int indexBufferHandle);
void drawShard(void *data, int countTris);

// Allocates an index-buffer slot (0 on failure) and returns its handle, matching RegisterTexture's own
// free-slot-scan pattern. No D3D8 calls involved - purely bookkeeping consumed later by d3dBindBuffers.
int d3dCreateIndexBuffer(int indexCount, unsigned int data);

// Allocates either one vertex-buffer slot (streamCount <= 0) or a run of `streamCount` consecutive slots (a
// multi-stream set), returning the starting handle (0 on failure). Shares its slot table with
// d3dSetStreamSources/d3dBindBuffers.
int d3dCreateVertexBuffers(unsigned int vtxCnt, unsigned int data, int nonSwizzled, unsigned int streamCount);

// A render-target push/pop "stack" (single level): a nonzero textureSlot pushes that texture's surface as the
// render target; 0 pops back to what was saved. NOTE: the original passes its parameter in ESI, not on the
// stack (confirmed via raw disassembly) - this is a __declspec(naked) entry trampoline (see its own comment
// in d3dSeam.cpp), so it must only ever be reached via the AUTOLTCG hook from the original's own callers, who
// already know to set ESI. Do NOT call this directly from new C++ code with a normal argument - it won't work.
void d3dRenderTargetSetup(void);

// Allocates a slot (0 on failure) in a small, separate 256-slot table used for reticle/crosshair-style
// textured-quad drawing (FUN_000e5350, not yet reimplemented).
int d3dRegisterOverlayBuffer(void *data, unsigned int vertexCount);

// Frees a slot registered by d3dRegisterOverlayBuffer (no-op for slot 0).
void d3dReleaseOverlayBuffer(int overlaySlot);

// Draws a small textured overlay quad (reticle/crosshair-style) previously registered via
// d3dRegisterOverlayBuffer, bound to texture stage 3.
void d3dDrawOverlayQuad(int overlaySlot, float sizeParam, int textureSlot, float param4, int param5);

// Resets render-target/texture/stream bindings to a clean default state. Has zero xrefs anywhere in the
// original binary - possibly dead code - but implemented anyway since it's simple and low-risk.
void d3dResetRenderTargetAndBuffers(void);

// Transforms up to 64 pending immediate-mode quads into a vertex buffer and draws them in one
// D3DDevice_DrawVerticesUP call. See its own comment in d3dSeam.cpp.
void maybeImmediateModeFlush(void);

// Appends a 9-float item (position rect, UV rect, packed colour as the raw bits of param9) to the immediate-
// mode buffer, flushing first (via maybeImmediateModeFlush) if it's full.
void maybeImmediateModePushItem(float param1, float param2, float param3, float param4, float param5,
                                 float param6, float param7, float param8, float param9);

// Standard D3D-style perspective projection matrix builder.
void createProjectionMatrix(D3DMATRIX *mtxOut, float aspect, float fov, float param4, float nearDist, float farDist);

// Resets some per-slot transform-related cache state (4 fixed slots) - untraced overall purpose.
void d3dResetTransformCaches(void);

// A fog-mode state machine (param 0/1/2) that also re-sends the current fog colour.
void d3dSetupRenderStatesAndFog(int param1);

// Resets a batch of cached render state to known defaults (texture/transform/fog/depth/cull/alpha/blend).
void maybeResetRenderState(char param1);

// Calls maybeResetRenderState(1) plus unbinds texture stage 0 and stream/index buffers.
void maybeD3dShutdown(void);

// Caches a pair of "deferred texture state" values, pushing them into two D3D8-internal globals once changed.
void d3dSetDeferredTextureState(int param1, int param2);

// Binds a texture (by slot, to stage 0) together with a clamp-to-border colour; textureSlot 0 unbinds and
// restores the plain deferred-texture-state pair saved beforehand.
void d3dSetTextureWithBorderColor(int textureSlot, int borderColour);

// Rebuilds the cached view matrix (Gfx_ViewMatrixCache) from a rigid transform - see its own comment in
// d3dSeam.cpp. Used directly by d3dBeginEndAuxRenderPass; not otherwise called from anywhere else (yet).
void d3dSetViewMatrixFromRigidTransform(D3DMATRIX *rigidTransform);

// A single-level "auxiliary render pass" push/pop (character-shadow rendering is the likely use, going by the
// call pattern) - begin != 0 pushes (saves matrices, pushes the render target given by an externally-maintained
// global, clears it, sets up rigidTransform/projMtx as the new view/projection); begin == 0 pops (restores the
// saved matrices/render target/viewport). Returns an externally-maintained status value either way - see its
// own comment in d3dSeam.cpp.
unsigned int d3dBeginEndAuxRenderPass(char begin, D3DMATRIX *rigidTransform, D3DMATRIX *projMtx);

// One-time-per-call D3D8 device setup - resets caches, establishes baseline render state, projection matrix,
// viewport, and various opaque D3D8-internal registers. See its own comment in d3dSeam.cpp for the details.
void d3dSetup(void);

// Closes an aux render pass and runs a two-pass box blur on a character shadow texture. See its own comment
// in d3dSeam.cpp. Only ever called from maybe_psiDrawShadow (not yet reimplemented).
void psiBlurCharacterShadow(void);

// Captures the current backbuffer into a round-robin "history" texture (3 slots) and draws it back as a
// blurIntensity-tinted screen-covering quad - a motion-blur/afterimage effect. See its own comment in
// d3dSeam.cpp for the (non-obvious) details of how it reaches real backbuffer pixel data.
void psiBlurScreen(int blurIntensity);

// One-time setup (xboxInitGraphics only) of the two permanent render-target textures psiBlurCharacterShadow/
// d3dBeginEndAuxRenderPass consume (Gfx_AuxRenderPassResult, Gfx_ShadowBlurTargetB). See its own comment in
// d3dSeam.cpp.
void d3dInitShadowBlurTextures(void);

#endif // D3DSEAM_H_
