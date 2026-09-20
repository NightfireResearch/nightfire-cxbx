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

// Where the XBE's statically linked D3D8 keeps the state the backend reads back at draw time: the deferred
// texture stage states (four stages of 32 dwords) and the render states. Both addresses belong to the XBE
// being run, so the engine sets them before the device is created - see the note in d3d9Backend.cpp.
extern uint32_t g_xboxTextureStateTable;
extern uint32_t g_xboxRenderStateTable;

// The action engine's overlay quad table, whose slots carry a vertex count rather than a byte size. Left
// empty by an engine that has no such table.
extern uint32_t g_overlayTableBase;
extern uint32_t g_overlayTableEnd;
// True when the game writes into memory the GPU reads with no signal to the backend - EAGL does: its dynamic
// vertex buffer is triple-buffered and refilled per draw, and its linear textures (the video window, the
// things it draws on the CPU) are written in place. Every draw then streams the vertex range it reads
// through a ring buffer instead of using a cached host copy (PrepareShaderDraw), and a linear texture is
// re-uploaded on its first bind in each frame (GetHostTexture).
extern bool g_streamsVolatile;

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

// Render state
void D3D9_SetRenderStateSimple(uint32_t nv2aMethod, uint32_t value); // the 11 NV097 methods the game uses
void D3D9_SetCullMode(int xboxCullMode);
void D3D9_SetFogColor(uint32_t colour);
void D3D9_SetZBias(int zBias);
void D3D9_SetZEnable(uint32_t value);
void D3D9_SetNormalizeNormals(uint32_t value);
void D3D9_SetVertexBlend(uint32_t value);
void D3D9_SetShaderConstantMode(uint32_t value);
void D3D9_SetYuvEnable(uint32_t enable);
void D3D9_SetDepthClipPlanes(uint32_t param1, uint32_t param2, uint32_t param3);
void D3D9_SetTextureBorderColor(uint32_t stage, uint32_t colour);

// Pixel shaders: NV2A register combiner definitions, translated (see nv2aPixelShader.h). Handles are tagged
// indices, not pointers; the game never looks inside one.
uint32_t D3D9_CreatePixelShader(const void *definition, uint32_t *handleOut);
void D3D9_SetPixelShader(uint32_t handle);
void D3D9_SetPixelShaderConstant(uint32_t reg, const float *values, uint32_t count);
void D3D9_DeletePixelShader(uint32_t handle);

// Shaders and constants
uint32_t D3D9_CreateVertexShader(const void *pDeclaration, const void *pFunction, void **pHandle, uint32_t usage);
void D3D9_SetVertexShader(void *handle);
void D3D9_SetVertexShaderConstant1(uint32_t constantIndex, float *pConstants);
void D3D9_SetVertexShaderConstant4(uint32_t constantIndex, void *pMatrix);
void D3D9_SetVertexShaderConstantNotInline(uint32_t constantIndex, void *pData, uint32_t countDwords);

// Textures and geometry
void D3D9_XGSetTextureHeader(uint32_t width, uint32_t height, uint32_t levels, uint32_t usage, int format,
                             uint32_t pool, void *pTexture, uint32_t data, uint32_t pitch);
void D3D9_ResourceRegister(void *pResource, uint32_t data);
void D3D9_NotifyTextureModified(void *pTextureOrSurface); // CPU wrote into the pixel data (decoder, intro effect)
void D3D9_SetTexture(uint32_t stage, void *pTexture);

// The palette bound to a texture stage: 256 (or fewer) A8R8G8B8 entries, or null for none. Paletted textures
// are expanded through it at upload, since D3D9 has no equivalent of the NV2A's palette hardware.
void D3D9_SetPalette(uint32_t stage, const void *entries);
void D3D9_SetStreamSource(int streamNumber, void *vertexBuffer, int stride);
void D3D9_SetIndices(void *pIndexBuffer, uint32_t baseVertexIndex);
void D3D9_DrawVerticesUP(uint32_t primitiveType, uint32_t vertexCount, void *pVertexData, uint32_t stride);

// Immediate mode: D3DDevice_Begin, a run of SetVertexData* calls, then End. The register numbers are the
// Xbox's vertex attribute slots - 3 is the diffuse colour, 9 is the first texture coordinate, and writing
// the position completes a vertex. See the immediate-mode section of d3d9Backend.cpp.
void D3D9_ImmediateBegin(uint32_t primitiveType);
void D3D9_ImmediateColour(uint32_t reg, uint32_t colour);
void D3D9_ImmediateTexCoord(uint32_t reg, float u, float v);
void D3D9_ImmediateVertex(uint32_t reg, float x, float y, float z, float w);
void D3D9_ImmediateEnd(void);
void D3D9_DrawIndexedVertices(uint32_t primitiveType, uint32_t vertexCount, const void *pIndexData);
void D3D9_DrawVertices(uint32_t primitiveType, uint32_t startVertex, uint32_t vertexCount);
void D3D9_SetRenderTarget(void *pRenderTarget, void *pDepthStencil);

// Resources / surfaces (Xbox-layout surface objects, so the seam's own hooks keep working on them)
uint32_t *D3D9_GetBackBuffer2(int32_t backBufferIndex);
void *D3D9_GetSurfaceLevel2(void *pTexture, uint32_t level);
void *D3D9_GetRenderTarget2(void);
void *D3D9_GetDepthStencilSurface2(void);
uint32_t D3D9_ResourceRelease(void *pResource);
// The geometry behind a surface object, for the D3D8 entry points that report it (D3DSurface_GetDesc). The
// format is an Xbox X_D3DFMT_* value, because that is what the callers feed back into XGSetTextureHeader.
void D3D9_GetSurfaceDesc(void *pSurface, uint32_t *format, uint32_t *width, uint32_t *height);

// True for the backbuffer, render target and depth stand-ins the backend hands out. They have no pixels a
// CPU can read or write - there is no Xbox-side framebuffer here - so a caller that wants to lock one has to
// be given something else.
bool D3D9_IsStandInSurface(const void *pSurface);
// Copies the backbuffer's pixels, as X8R8G8B8 rows of the given pitch, into memory the game is about to
// read - what a lock of the backbuffer means. Returns false if nothing could be read.
bool D3D9_ReadBackBuffer(void *destination, uint32_t pitch, uint32_t width, uint32_t height);
void D3D9_BlockUntilNotBusy(void *pResource);

// Visibility tests: the NV2A counts the pixels that pass the depth test between Begin and End(index), and the
// game asks for the count later. Occlusion queries, one per index. GetResult returns 0 with the count when it
// is ready, or a failing HRESULT (D3DERR_TESTINCOMPLETE) until it is - the game spins on that.
void D3D9_BeginVisibilityTest(void);
void D3D9_EndVisibilityTest(uint32_t index);
uint32_t D3D9_GetVisibilityTestResult(uint32_t index, uint32_t *result, uint64_t *timeStamp);

#endif // D3D9BACKEND_H_
