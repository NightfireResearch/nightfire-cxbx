#ifndef DRIVING_GFX_D3D8_H_
#define DRIVING_GFX_D3D8_H_

// The Xbox D3D8 and XGRAPHC entry points our code calls, as the graphics seam implements them (d3dSeam.cpp). Each is
// the function the seam patches over the original's entry point, so calling it directly is calling exactly what a
// call to the original's address reached: a 5-byte jump to the same function. Names, argument order and conventions
// are the Xbox library's (__stdcall; the register-argument entry points __fastcall); the original's address is
// beside each, for reading the listing.
//
// Resources are the Xbox D3D8 headers below - every D3D8 object starts with them; the caller's own wider view of an
// object (EAGL's SurfaceTexture, the seam's vertex buffer) converts to them.

#include <stddef.h>
#include <stdint.h>

// ---- Xbox D3D8 structures

// D3DResource: the header every D3D8 object starts with.
struct D3DResource {                 // 0x0c
    uint32_t common;                 // +0x00 the type, reference count and flags
    uint32_t data;                   // +0x04 the data's address
    uint32_t lock;                   // +0x08
};
static_assert(sizeof(D3DResource) == 0x0c, "a D3D8 resource header is 3 dwords");

// D3DPixelContainer: the header every D3D8 surface and texture starts with.
struct D3DPixelContainer : D3DResource {   // 0x14
    uint32_t format;                 // +0x0c
    uint32_t size;                   // +0x10
};
static_assert(sizeof(D3DPixelContainer) == 0x14, "a D3DPixelContainer is 0x14 bytes");

// D3DPushBuffer: the resource header, Size and AllocationSize.
struct D3DPushBuffer : D3DResource {   // 0x14
    uint32_t size;                   // +0x0c
    uint32_t allocationSize;         // +0x10
};
static_assert(sizeof(D3DPushBuffer) == 0x14, "a D3D8 push buffer header is 5 dwords");

// D3DSURFACE_DESC as the Xbox's D3D8 has it: no Pool, and Size where the PC version has Pool.
struct D3DSurfaceDesc {
    uint32_t format;                 // +0x00
    uint32_t type;                   // +0x04
    uint32_t usage;                  // +0x08
    uint32_t size;                   // +0x0c
    uint32_t multiSampleType;        // +0x10
    uint32_t width;                  // +0x14
    uint32_t height;                 // +0x18
};
static_assert(sizeof(D3DSurfaceDesc) == 0x1c, "D3DSURFACE_DESC is 0x1c bytes");

struct D3DLockedRect {               // D3DLOCKED_RECT
    int32_t pitch;
    void *bits;
};

struct D3DRect {                     // RECT / D3DRECT
    int32_t left, top, right, bottom;
};

struct D3DPoint {                    // POINT
    int32_t x, y;
};

struct D3DViewport8 {                // D3DVIEWPORT8
    uint32_t x;                      // +0x00
    uint32_t y;                      // +0x04
    uint32_t width;                  // +0x08
    uint32_t height;                 // +0x0c
    float minZ;                      // +0x10
    float maxZ;                      // +0x14
};
static_assert(sizeof(D3DViewport8) == 0x18, "D3DVIEWPORT8 is 0x18 bytes");

struct D3DTile {                     // D3DTILE
    uint32_t flags;                  // +0x00
    void *memory;                    // +0x04
    uint32_t size;                   // +0x08
    uint32_t pitch;                  // +0x0c
    uint32_t zStartTag;              // +0x10
    uint32_t zOffset;                // +0x14
};
static_assert(sizeof(D3DTile) == 0x18, "D3DTILE is 0x18 bytes");

struct D3DGammaRamp {                // D3DGAMMARAMP
    uint8_t red[256], green[256], blue[256];
};

// ---- the device

// presentationParameters: D3DPRESENT_PARAMETERS (EAGL's PresentParameters, RenderContext.h, is that and one word more).
int32_t __stdcall Direct3D_CreateDevice(uint32_t adapter, uint32_t deviceType, void *focusWindow,
                                        uint32_t behaviourFlags, void *presentationParameters,
                                        void **returnedDevice);                                  // 0x00169480
int32_t __stdcall D3DDevice_Reset(void *presentationParameters);                                // 0x00166230
int32_t __stdcall D3DDevice_PersistDisplay();                                                   // 0x00166eb0
void __stdcall D3D_SetPushBufferSize(uint32_t pushBufferSize, uint32_t kickOffSize);           // 0x00169460
uint32_t __stdcall D3D_ReturnsTrue(uint32_t unused);                                            // 0x00169450
void __stdcall D3DDevice_Clear(uint32_t count, const D3DRect *rects, uint32_t flags, uint32_t colour, float z,
                               uint32_t stencil);                                               // 0x00168c90
void __stdcall D3DDevice_Swap(uint32_t flags);                                                  // 0x00169fb0
D3DPixelContainer *__stdcall D3DDevice_GetBackBuffer2(int32_t index);                           // 0x001662e0
D3DPixelContainer *__stdcall D3DDevice_GetDepthStencilSurface2();                               // 0x001666b0
void __stdcall D3DDevice_SetRenderTarget(D3DPixelContainer *renderTarget,
                                         D3DPixelContainer *depthStencil);                      // 0x00165dc0
void __stdcall D3DDevice_SetViewport(const D3DViewport8 *viewport);                             // 0x001666d0
void __stdcall D3DDevice_SetScreenSpaceOffset(float x, float y);                                // 0x00167030
void __stdcall D3DDevice_SetShaderConstantMode(uint32_t mode);                                  // 0x0016ab30
void __stdcall D3DDevice_GetGammaRamp(D3DGammaRamp *ramp);                                      // 0x00165e70
void __stdcall D3DDevice_SetGammaRamp(uint32_t flags, const D3DGammaRamp *ramp);                // 0x00165de0
void __stdcall D3DDevice_GetTile(uint32_t index, D3DTile *tile);                                // 0x00166060
void __stdcall D3DDevice_SetTile(uint32_t index, const D3DTile *tile);                          // 0x00166d00
void __stdcall D3DDevice_SetFlickerFilter(uint32_t filter);                                     // 0x00166090
void __stdcall D3DDevice_SetSoftDisplayFilter(uint32_t enable);                                 // 0x001660e0
void __stdcall D3DDevice_CopyRects(D3DPixelContainer *source, const D3DRect *rects, uint32_t count,
                                   D3DPixelContainer *destination, const D3DPoint *points);     // 0x001663e0

// ---- render states

// The simple states: the NV2A method in ECX and the value in EDX, written straight into the push buffer.
void __fastcall D3DDevice_SetRenderState_Simple(uint32_t method, uint32_t value);              // 0x001673e0

// The states with an entry point of their own, all taking the value as their one argument.
void __stdcall D3DDevice_SetRenderState_PSTextureModes(uint32_t value);                         // 0x001673b0
void __stdcall D3DDevice_SetRenderState_VertexBlend(uint32_t value);                            // 0x00167bf0
void __stdcall D3DDevice_SetRenderState_FogColor(uint32_t colour);                              // 0x00167760
void __stdcall D3DDevice_SetRenderState_FillMode(uint32_t fillMode);                            // 0x00167ad0
void __stdcall D3DDevice_SetRenderState_BackFillMode(uint32_t value);                           // 0x00167b20
void __stdcall D3DDevice_SetRenderState_TwoSidedLighting(uint32_t value);                       // 0x00167b80
void __stdcall D3DDevice_SetRenderState_NormalizeNormals(uint32_t value);                       // 0x00167860
void __stdcall D3DDevice_SetRenderState_ZEnable(uint32_t value);                                // 0x001687f0
void __stdcall D3DDevice_SetRenderState_StencilEnable(uint32_t value);                          // 0x00168880
void __stdcall D3DDevice_SetRenderState_StencilFail(uint32_t value);                            // 0x00168910
void __stdcall D3DDevice_SetRenderState_FrontFace(uint32_t value);                              // 0x00167820
void __stdcall D3DDevice_SetRenderState_CullMode(uint32_t cullMode);                            // 0x001677b0
void __stdcall D3DDevice_SetRenderState_TextureFactor(uint32_t value);                          // 0x001678a0
void __stdcall D3DDevice_SetRenderState_ZBias(uint32_t value);                                  // 0x001679f0
void __stdcall D3DDevice_SetRenderState_LogicOp(uint32_t value);                                // 0x00167a70
void __stdcall D3DDevice_SetRenderState_EdgeAntiAlias(uint32_t value);                          // 0x001676e0
void __stdcall D3DDevice_SetRenderState_MultiSampleAntiAlias(uint32_t value);                   // 0x00168b70
void __stdcall D3DDevice_SetRenderState_MultiSampleMask(uint32_t value);                        // 0x00168bf0
void __stdcall D3DDevice_SetRenderState_MultiSampleMode(uint32_t value);                        // 0x00168af0
void __stdcall D3DDevice_SetRenderState_MultiSampleRenderTargetMode(uint32_t value);            // 0x00168b30
void __stdcall D3DDevice_SetRenderState_ShadowFunc(uint32_t value);                             // 0x00167720
void __stdcall D3DDevice_SetRenderState_LineWidth(uint32_t value);                              // 0x00167900
void __stdcall D3DDevice_SetRenderState_SampleAlpha(uint32_t value);                            // 0x00168c40
void __stdcall D3DDevice_SetRenderState_Dxt1NoiseEnable(uint32_t value);                        // 0x00167970
void __stdcall D3DDevice_SetRenderState_YuvEnable(uint32_t enable);                             // 0x00168980
void __stdcall D3DDevice_SetRenderState_OcclusionCullEnable(uint32_t value);                    // 0x001689b0
void __stdcall D3DDevice_SetRenderState_StencilCullEnable(uint32_t value);                      // 0x00168a20
void __stdcall D3DDevice_SetRenderState_RopZCmpAlwaysRead(uint32_t value);                      // 0x00168a90
void __stdcall D3DDevice_SetRenderState_RopZRead(uint32_t value);                               // 0x00168ab0
void __stdcall D3DDevice_SetRenderState_DoNotCullUncompressed(uint32_t value);                  // 0x00168ad0

// ---- texture stage states

void __stdcall D3DDevice_SetTextureState_TexCoordIndex(uint32_t stage, uint32_t value);         // 0x00167c40
void __stdcall D3DDevice_SetTextureState_BorderColor(uint32_t stage, uint32_t colour);          // 0x00167dc0
void __stdcall D3DDevice_SetTextureState_ColorKeyColor(uint32_t stage, uint32_t value);         // 0x00167e00
void __stdcall D3DDevice_SetTextureState_BumpEnv(uint32_t stage, uint32_t type, uint32_t value);   // 0x00167d50

// ---- resources

void __stdcall D3DResource_Register(D3DResource *resource, void *base);                          // 0x001693a0
uint32_t __stdcall D3DResource_AddRef(D3DResource *resource);                                    // 0x001691f0
uint32_t __stdcall D3DResource_Release(D3DResource *resource);                                   // 0x00169230
uint32_t __stdcall D3DResource_IsBusy(D3DResource *resource);                                    // 0x00169310
void __stdcall D3DResource_BlockUntilNotBusy(D3DResource *resource);                             // 0x001693d0

// ---- textures, surfaces and palettes

D3DPixelContainer *__stdcall D3DDevice_CreateTexture2(uint32_t width, uint32_t height, uint32_t depth,
                                                      uint32_t levels, uint32_t usage, uint32_t format,
                                                      uint32_t resourceType);                     // 0x00167260
D3DPixelContainer *__stdcall D3D_CreateStandAloneSurface(uint32_t width, uint32_t height, uint32_t levels,
                                                         uint32_t format);                        // 0x00167100
D3DPixelContainer *__stdcall D3DTexture_GetSurfaceLevel2(D3DPixelContainer *texture, uint32_t level);   // 0x00167330
int32_t __stdcall D3DTexture_LockRect(D3DPixelContainer *texture, uint32_t level, D3DLockedRect *locked,
                                      const D3DRect *rect, uint32_t flags);                       // 0x00167380
void __stdcall D3DSurface_GetDesc(D3DPixelContainer *surface, D3DSurfaceDesc *desc);              // 0x00167220
int32_t __stdcall D3DSurface_LockRect(D3DPixelContainer *surface, D3DLockedRect *locked, const D3DRect *rect,
                                      uint32_t flags);                                            // 0x00167240
// The middle argument is the mip level. The generated entry table calls it Get2DSurfaceDesc (D3D::Get2DSurfaceDesc).
void __stdcall D3D_Get2DSurfaceDesc(D3DPixelContainer *surface, uint32_t level, D3DSurfaceDesc *desc);   // 0x00167320
void __stdcall D3DDevice_SetTexture(uint32_t stage, D3DPixelContainer *texture);                 // 0x00166830
D3DResource *__stdcall D3DDevice_CreatePalette2(uint32_t size);                                  // 0x0016b510
uint32_t *__stdcall D3DPalette_Lock2(D3DResource *palette, uint32_t flags);                      // 0x0016b570
void __stdcall D3DDevice_SetPalette(uint32_t stage, D3DResource *palette);                       // 0x001669e0

// ---- shaders

int32_t __stdcall D3DDevice_CreateVertexShader(const void *declaration, const void *function, uint32_t *handle,
                                               uint32_t usage);                                  // 0x0016a650
void __stdcall D3DDevice_SetVertexShader(uint32_t handle);                                      // 0x0016ad90
void __stdcall D3DDevice_DeleteVertexShader(uint32_t handle);                                   // 0x0016ac70
// The constant setters: the first register in ECX, the values' address in EDX; NotInline takes the number of dwords
// on the stack.
void __fastcall D3DDevice_SetVertexShaderConstant1(int reg, const void *constants);             // 0x0016a790
void __fastcall D3DDevice_SetVertexShaderConstant4(int reg, const void *constants);             // 0x0016a7f0
void __fastcall D3DDevice_SetVertexShaderConstantNotInline(int reg, const void *constants,
                                                           uint32_t countDwords);               // 0x0016a980
int32_t __stdcall D3DDevice_CreatePixelShader(const void *definition, uint32_t *handle);        // 0x0016aef0
void __stdcall D3DDevice_SetPixelShader(uint32_t handle);                                       // 0x0016af60
void __stdcall D3DDevice_SetPixelShaderConstant(uint32_t reg, const void *values, uint32_t count);   // 0x0016b160
void __stdcall D3DDevice_DeletePixelShader(uint32_t handle);                                    // 0x0016af40

// ---- vertex and index buffers, drawing

D3DResource *__stdcall D3DDevice_CreateVertexBuffer2(uint32_t length);                           // 0x0016b470
uint8_t *__stdcall D3DVertexBuffer_Lock2(D3DResource *buffer, uint32_t flags);                   // 0x0016b4c0
D3DResource *__stdcall D3DDevice_CreateIndexBuffer2(uint32_t length);                            // 0x0016b430
void __stdcall D3DDevice_SetStreamSource(uint32_t streamNumber, D3DResource *vertexBuffer, uint32_t stride);   // 0x0016a9c0
void __stdcall D3DDevice_SetIndices(D3DResource *indexBuffer, uint32_t baseVertexIndex);         // 0x00166a70
void __stdcall D3DDevice_DrawVertices(uint32_t primitiveType, uint32_t startVertex, uint32_t vertexCount);   // 0x0016b620
void __stdcall D3DDevice_DrawIndexedVertices(uint32_t primitiveType, uint32_t vertexCount,
                                             const uint16_t *indexData);                         // 0x0016b6c0
void __stdcall D3DDevice_RunPushBuffer(D3DPushBuffer *pushBuffer, void *fixup);                  // 0x0016baa0

// Immediate mode: a vertex's colour and texture coordinate, then its position, which completes it.
void __stdcall D3DDevice_Begin(uint32_t primitiveType);                                         // 0x0016ba20
void __stdcall D3DDevice_End();                                                                 // 0x0016ba60
void __stdcall D3DDevice_SetVertexDataColor(uint32_t reg, uint32_t colour);                     // 0x0016b9d0
void __stdcall D3DDevice_SetVertexData2f(uint32_t reg, float a, float b);                       // 0x0016b930
void __stdcall D3DDevice_SetVertexData4f(uint32_t reg, float a, float b, float c, float d);     // 0x0016b970

// ---- fences and visibility tests

uint32_t __stdcall D3DDevice_InsertFence();                                                     // 0x00166140
void __stdcall D3DDevice_BlockOnFence(uint32_t fence);                                          // 0x00165fc0
uint32_t __stdcall D3DDevice_IsBusy();                                                          // 0x00166b00
void __stdcall D3DDevice_MakeSpace();                                                           // 0x0016cda0
void __stdcall D3DDevice_BeginVisibilityTest();                                                 // 0x00166b40
void __stdcall D3DDevice_EndVisibilityTest(uint32_t index);                                     // 0x00166be0
int32_t __stdcall D3DDevice_GetVisibilityTestResult(uint32_t index, uint32_t *result, uint64_t *timeStamp);   // 0x00165fd0

// ---- XGRAPHC and D3DX

void __stdcall XGSetTextureHeader(uint32_t width, uint32_t height, uint32_t levels, uint32_t usage, uint32_t format,
                                  uint32_t pool, D3DPixelContainer *texture, uint32_t data, uint32_t pitch);   // 0x0017a8ac
void __stdcall XGSetVertexBufferHeader(uint32_t length, uint32_t usage, uint32_t fvf, uint32_t pool,
                                       D3DResource *buffer, uint32_t data);                       // 0x0017a8d6
uint32_t __stdcall XGBytesPerPixelFromFormat(uint32_t format);                                    // 0x00178fb8
uint32_t __stdcall XGIsSwizzledFormat(uint32_t format);                                           // 0x00178f74
void __stdcall XGSwizzleRect(const void *source, uint32_t pitch, const D3DRect *rect, void *destination,
                             uint32_t width, uint32_t height, const D3DPoint *point,
                             uint32_t bytesPerPixel);                                             // 0x0017982a
void __stdcall XGUnswizzleRect(const void *source, uint32_t width, uint32_t height, uint32_t depth,
                               void *destination, uint32_t pitch, const D3DRect *rect,
                               uint32_t bytesPerPixel);                                           // 0x00179f7b
int32_t __stdcall XGWriteSurfaceToFile(D3DPixelContainer *surface, const char *path);              // 0x0017a8ee
int32_t __stdcall D3DXLoadSurfaceFromMemory(D3DPixelContainer *destSurface, const void *destPalette,
                                            const D3DRect *destRect, const void *source, uint32_t sourceFormat,
                                            uint32_t sourcePitch, const void *sourcePalette,
                                            const D3DRect *sourceRect, uint32_t filter,
                                            uint32_t colourKey);                                  // 0x0015d3bd

#endif // DRIVING_GFX_D3D8_H_
