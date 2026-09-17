#ifndef D3D9BACKEND_H_
#define D3D9BACKEND_H_

#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// The native Direct3D 9 backend for the D3D8 seam (d3dSeam.cpp). Selected by "GraphicsBackend=d3d9" in
// settings.ini; the default ("cxbx") leaves every D3D8 entry point going to CXBX's HLE exactly as before.
//
// In d3d9 mode the seam never calls a D3D8 library function: each entry-point macro in d3dSeam.cpp dispatches
// to the D3D9_* function attached to it here, or - for entry points this backend doesn't implement yet - to
// D3D9_BackendMissing, which counts the call and does nothing. The missing-call table is printed periodically
// so the bring-up can be driven by what the game actually asks for.
//
// The D3D9 device is created on CXBX's own render window ("CxbxRender", the child of the launcher's window
// that CXBX creates at startup). Because the game never calls Direct3D_CreateDevice in this mode, CXBX never
// creates its own device, so the window is ours.
//
// Signatures below deliberately mirror the D3D8 entry-point typedefs in d3dSeam.cpp (same parameter types),
// since the dispatch wrapper deduces the backend function's signature from the D3D8 one.
// ---------------------------------------------------------------------------------------------------------------

enum { GFX_BACKEND_CXBX = 0, GFX_BACKEND_D3D9 = 1 };
extern int g_gfxBackend;

void D3D9_BackendMissing(const char *entryPoint);

// Device
uint32_t D3D9_CreateDevice(uint32_t adapter, uint32_t deviceType, void *hFocusWindow, uint32_t behaviorFlags,
                           void *pPresentationParameters, void **ppDevice);
uint32_t D3D9_ArePushBuffersSupported(uint32_t unused);
void D3D9_SetPushBufferSize(uint32_t pushBufferSize, uint32_t kickOffSize);
void D3D9_Clear(uint32_t rectCount, void *pRects, uint32_t flags, uint32_t colour, float z, uint32_t stencil);
void D3D9_Swap(uint32_t type);
void D3D9_SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height, float minZ, float maxZ);
void D3D9_SetGammaRamp(uint32_t flags, void *pRamp);

// Shaders
uint32_t D3D9_CreateVertexShader(const void *pDeclaration, const void *pFunction, void **pHandle, uint32_t usage);

// Resources / surfaces (Xbox-layout surface objects, so the seam's own hooks keep working on them)
uint32_t *D3D9_GetBackBuffer2(int32_t backBufferIndex);
void *D3D9_GetSurfaceLevel2(void *pTexture, uint32_t level);
void *D3D9_GetRenderTarget2(void);
void *D3D9_GetDepthStencilSurface2(void);
uint32_t D3D9_ResourceRelease(void *pResource);
void D3D9_BlockUntilNotBusy(void *pResource);

#endif // D3D9BACKEND_H_
