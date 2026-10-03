#ifndef DRIVING_EAGL_RENDERCONTEXT_H_
#define DRIVING_EAGL_RENDERCONTEXT_H_

// EAGL::RenderContext, its Xbox extension and its private part (docs/driving/eagl.md 2.2, 3.2, 4.2, 8.1, 8.2):
// the frame, the frame buffers and every render-context setting the game changes. See RenderContext.cpp. In
// namespace EAGL, as in Ghidra: the game-side overlay in render/RenderState.hpp has a RenderContext of its own.
//
// One object, three views of it: the RenderContextExtension is the object's first word (which points back at the
// object), the RenderContextPrivate starts at +4 (its first word points back at the object too). The extension's
// methods are called with the object's address and reach the fields through that first word. The viewport-list
// methods (0x000ee010..0x000ee160) are View.cpp's.

#include <stdint.h>

namespace EAGL {

struct Device;
struct RenderContextExtension;
struct RenderContextPrivate;
struct TAR;
struct ViewPort;

// D3D8's D3DPixelContainer: the header every D3D8 surface and texture starts with.
struct D3DPixelContainer {               // 0x14
    uint32_t common;                     // +0x00
    uint32_t data;                       // +0x04 the pixels' address
    uint32_t lock;                       // +0x08
    uint32_t format;                     // +0x0c
    uint32_t size;                       // +0x10
};
static_assert(sizeof(D3DPixelContainer) == 0x14, "a D3DPixelContainer is 0x14 bytes");

// The 0x14-byte texture header EAGL lays over a surface ("D3DTexture"); its constructor clears it.
struct SurfaceTexture : D3DPixelContainer {
    SurfaceTexture* Construct();                                             // 0x000e85e0 (unreferenced)
};
static_assert(sizeof(SurfaceTexture) == 0x14, "the texture header is 0x14 bytes");

// D3DPRESENT_PARAMETERS as the Xbox's D3D8 has it, plus the word after it SetupFrameBuffers clears with it.
struct PresentParameters {               // 0x44
    uint32_t backBufferWidth;            // +0x00
    uint32_t backBufferHeight;           // +0x04
    uint32_t backBufferFormat;           // +0x08
    uint32_t backBufferCount;            // +0x0c
    uint32_t multiSampleType;            // +0x10
    uint32_t swapEffect;                 // +0x14
    uint32_t deviceWindow;               // +0x18
    uint32_t windowed;                   // +0x1c
    uint32_t enableAutoDepthStencil;     // +0x20
    uint32_t autoDepthStencilFormat;     // +0x24
    uint32_t flags;                      // +0x28 0x10 widescreen, 0x30/0x50/0xb0 the HD modes, 0x100
    uint32_t refreshRate;                // +0x2c 60 with PAL60
    uint32_t presentationInterval;       // +0x30 0x80000000 immediate when not synced to the VBL
    uint32_t bufferSurfaces[3];          // +0x34
    uint32_t depthStencilSurface;        // +0x40
};
static_assert(sizeof(PresentParameters) == 0x44, "SetupFrameBuffers clears 0x11 words");

struct RenderContext {                   // 0x14c, "EAGL::RenderContext"
    RenderContextExtension *extension;   // +0x000 this object (the extension's back pointer)
    RenderContext *privateOwner;         // +0x004 this object (the RenderContextPrivate's back pointer)
    uint32_t backBufferFormat;           // +0x008 5 or 6 (SetBackBufferDepth)
    uint32_t depthFormat;                // +0x00c 0x2a or 0x2c (SetZBufferDepth)
    uint32_t multiSampleType;            // +0x010 0x11 = none: EndFrame skips its state restore
    int32_t width;                       // +0x014 SetSize, truncated
    int32_t height;                      // +0x018
    int32_t frontBufferDepth;            // +0x01c
    int32_t backBufferDepth;             // +0x020
    int32_t zBufferDepth;                // +0x024 0: no depth buffer
    uint8_t pad028[4];
    uint8_t syncToVBL;                   // +0x02c
    uint8_t pad02d[3];
    uint32_t unknown030;                 // +0x030 kept, never sent (SetField30)
    uint8_t ditherEnable;                // +0x034 sent as method 0x40310, shadowed at 0x0023ff10
    uint8_t pad035[3];
    uint32_t zEnable;                    // +0x038 SetRenderState_ZEnable in SetupFrameBuffers
    uint32_t stencilZFail;               // +0x03c method 0x40374
    uint32_t stencilZPass;               // +0x040 method 0x40378
    uint32_t stencilFail;                // +0x044 SetRenderState_StencilFail
    uint32_t stencilFunc;                // +0x048 method 0x40364
    uint32_t stencilRef;                 // +0x04c method 0x40368
    uint32_t stencilMask;                // +0x050 method 0x4036c
    uint32_t stencilWriteMask;           // +0x054 method 0x40360
    uint8_t stencilEnable;               // +0x058
    uint8_t zWritesEnable;               // +0x059 method 0x4035c
    uint8_t pad05a[2];
    uint32_t colourWriteMask;            // +0x05c the render mask, method 0x40358
    uint8_t multiSampleAntiAlias;        // +0x060
    uint8_t fogEnable;                   // +0x061
    uint8_t pad062[2];
    uint32_t fogTableMode;               // +0x064 D3D8 render-state slot 93
    uint32_t fogStart;                   // +0x068 slot 94 (float bits)
    uint32_t fogEnd;                     // +0x06c slot 95
    uint32_t fogDensity;                 // +0x070 slot 96
    uint32_t fogColour;                  // +0x074
    uint8_t wideScreen;                  // +0x078 present flag 0x10
    uint8_t presentFlag100;              // +0x079 present flag 0x100 (640 wide only)
    uint8_t pad07a[2];
    uint32_t shadowFunc;                 // +0x07c
    uint32_t pushBufferSize;             // +0x080 D3D_SetPushBufferSize, before the device exists
    uint32_t kickOffSize;                // +0x084
    float screenSpaceOffsetX;            // +0x088
    float screenSpaceOffsetY;            // +0x08c
    uint32_t pointSize;                  // +0x090 slot 116
    uint32_t pointSizeMin;               // +0x094 slot 117
    uint32_t pointSizeMax;               // +0x098 slot 123
    uint32_t pointScaleA;                // +0x09c slot 120
    uint32_t pointScaleB;                // +0x0a0 slot 121
    uint32_t pointScaleC;                // +0x0a4 slot 122
    uint8_t pointSpriteEnable;           // +0x0a8 slot 118
    uint8_t pointScaleEnable;            // +0x0a9 slot 119
    uint8_t softDisplayFilter;           // +0x0aa
    uint8_t pad0ab;
    int32_t flickerFilter;               // +0x0ac 0..5
    uint8_t pal60;                       // +0x0b0 XGetVideoFlags & 0x40
    uint8_t pad0b1[3];
    int32_t swapInterval;                // +0x0b4 1..3
    PresentParameters present;           // +0x0b8
    int32_t currentWidth;                // +0x0fc what SetupFrameBuffers last set up (GetSize reads these)
    int32_t currentHeight;               // +0x100
    int32_t currentFrontBufferDepth;     // +0x104
    int32_t currentBackBufferDepth;      // +0x108
    int32_t currentZBufferDepth;         // +0x10c
    D3DPixelContainer *frontBuffer;      // +0x110 D3D surface, GetBackBuffer2(-1)
    D3DPixelContainer *backBuffer;       // +0x114 GetBackBuffer2(0), the render target
    D3DPixelContainer *depthSurface;     // +0x118
    D3DPixelContainer *copyTexture;      // +0x11c CopyBackBuffer's texture
    SurfaceTexture *frontAlias;          // +0x120 texture headers over the three surfaces,
    SurfaceTexture *backAlias;           // +0x124 their data pointers refreshed every EndFrame
    SurfaceTexture *depthAlias;          // +0x128
    TAR *backTar;                        // +0x12c TARs over them (CopyBackBuffer also fills this one)
    TAR *frontTar;                       // +0x130
    TAR *depthTar;                       // +0x134
    ViewPort *currentViewPort;           // +0x138
    ViewPort *viewPorts;                 // +0x13c list, linked through ViewPort::next
    RenderContext *next;                 // +0x140 the Device's list
    uint32_t unknown144;                 // +0x144
    Device *device;                      // +0x148

    // The extension and the private part are views of this object, at +0x00 and +0x04
    RenderContextExtension* Extension() { return reinterpret_cast<RenderContextExtension *>(this); }
    RenderContextPrivate* Private() { return reinterpret_cast<RenderContextPrivate *>(&privateOwner); }

    RenderContext* Construct(Device *device);                                // 0x000e8740
    void Destruct();                                                         // 0x000e8600
    void BeginFrame();                                                       // 0x000e6610
    void* EndFrame();                                                        // 0x000e6640 (returns Device::Get())
    void SetSize(float width, float height);                                 // 0x000e6a60
    void GetSize(float *width, float *height);                               // 0x000e6a80
    void SetFrontBufferDepth(int depth);                                     // 0x000e6aa0 (empty)
    int GetFrontBufferDepth();                                               // 0x000e6ab0 (unreferenced)
    void SetBackBufferDepth(int depth);                                      // 0x000e6ac0
    int GetBackBufferDepth();                                                // 0x000e6b00 (unreferenced)
    void SetZBufferDepth(int depth);                                         // 0x000e6b10
    int GetZBufferDepth();                                                   // 0x000e6b50 (unreferenced)
    void SetSyncToVBL(uint8_t sync);                                         // 0x000e6b60
    uint8_t GetSyncToVBL();                                                  // 0x000e6bd0 (unreferenced)
    bool SetZEnable(uint32_t enable);                                        // 0x000e6be0 (unreferenced)
    bool GetZEnable(uint32_t *enable);                                       // 0x000e6bf0 (unreferenced)
    uint32_t SetupFrameBuffers();                                            // 0x000e6c00
    bool SetDitherEnable(uint8_t enable);                                    // 0x000e73a0 (unreferenced)
    bool GetDitherEnable(uint8_t *enable);                                   // 0x000e73e0 (unreferenced)
    bool SetZWritesEnable(uint8_t enable);                                   // 0x000e73f0
    bool GetZWritesEnable(uint8_t *enable);                                  // 0x000e7430
    bool SetField30(uint32_t value);                                         // 0x000e7440 (unreferenced)
    bool GetField30(uint32_t *value);                                        // 0x000e7450 (unreferenced)
    bool SetSwapInterval(int interval);                                      // 0x000e7460 (unreferenced)
    bool GetSwapInterval(int *interval);                                     // 0x000e74c0 (unreferenced)

    // The viewport list (View.cpp)
    ViewPort* NewViewPort();                                                 // 0x000ee010
    ViewPort* GetCurrentViewPort();                                          // 0x000ee080
    void DeleteViewPort(ViewPort *viewPort);                                 // 0x000ee0a0
    uint8_t* OffsetSelf();                                                   // 0x000ee160 (invented; no callers)
};
static_assert(sizeof(RenderContext) == 0x14c, "a RenderContext is 0x14c bytes");

// The Xbox extension: the object's first word. Every method reaches the object through it.
struct RenderContextExtension {
    RenderContext *context;              // +0x00

    RenderContextExtension* Construct(RenderContext *owner);                // 0x000e8560 (unreferenced)
    bool SetMultiSampleType(uint32_t type);                                  // 0x000e74e0
    bool GetMultiSampleType(uint32_t *type);                                 // 0x000e7510
    bool SetStencilZFail(uint32_t op);                                       // 0x000e7520
    bool GetStencilZFail(uint32_t *op);                                      // 0x000e7560 (unreferenced)
    bool SetStencilZPass(uint32_t op);                                       // 0x000e7570
    bool GetStencilZPass(uint32_t *op);                                      // 0x000e75b0 (unreferenced)
    bool SetStencilFail(uint32_t op);                                        // 0x000e75c0
    bool GetStencilFail(uint32_t *op);                                       // 0x000e75f0 (unreferenced)
    bool SetStencilFunc(uint32_t func);                                      // 0x000e7600
    bool GetStencilFunc(uint32_t *func);                                     // 0x000e7640 (unreferenced)
    bool SetStencilRef(uint32_t ref);                                        // 0x000e7650
    bool GetStencilRef(uint32_t *ref);                                       // 0x000e7690 (unreferenced)
    bool SetStencilMask(uint32_t mask);                                      // 0x000e76a0
    bool GetStencilMask(uint32_t *mask);                                     // 0x000e76e0 (unreferenced)
    bool SetStencilWriteMask(uint32_t mask);                                 // 0x000e76f0
    bool GetStencilWriteMask(uint32_t *mask);                                // 0x000e7730 (unreferenced)
    bool SetGamma(float red, float green, float blue);                       // 0x000e7740 (unreferenced)
    bool SetGammaRamp(const uint8_t *ramp);                                  // 0x000e7820 (unreferenced)
    bool SetStencilEnable(uint8_t enable);                                   // 0x000e7890
    bool GetStencilEnable(uint8_t *enable);                                  // 0x000e78c0 (unreferenced)
    bool Screenshot(const char *path);                                       // 0x000e78d0 (unreferenced)
    bool SetRenderMask(uint32_t mask);                                       // 0x000e7900
    bool GetRenderMask(uint32_t *mask);                                      // 0x000e7940 (unreferenced)
    bool SetFogEnable(uint8_t enable);                                       // 0x000e7950 (Ghidra: changeFogState)
    bool GetFogEnable(uint8_t *enable);                                      // 0x000e7990 (unreferenced)
    bool SetFogTableMode(uint32_t mode);                                     // 0x000e79a0
    bool GetFogTableMode(uint32_t *mode);                                    // 0x000e79d0 (unreferenced)
    bool SetFogStart(uint32_t start);                                        // 0x000e79e0 (float bits)
    bool GetFogStart(uint32_t *start);                                       // 0x000e7a10 (unreferenced)
    bool SetFogEnd(uint32_t end);                                            // 0x000e7a20
    bool GetFogEnd(uint32_t *end);                                           // 0x000e7a50 (unreferenced)
    bool SetFogDensity(uint32_t density);                                    // 0x000e7a60
    bool GetFogDensity(uint32_t *density);                                   // 0x000e7a90 (unreferenced)
    bool SetFogColour(uint32_t colour);                                      // 0x000e7aa0
    bool GetFogColour(uint32_t *colour);                                     // 0x000e7ac0 (unreferenced)
    bool SetWideScreen(uint8_t enable);                                      // 0x000e7ad0
    bool GetWideScreen(uint8_t *enable);                                     // 0x000e7b10 (unreferenced)
    bool SetPresentFlag100(uint8_t enable);                                  // 0x000e7b20
    bool GetPresentFlag100(uint8_t *enable);                                 // 0x000e7b60 (unreferenced)
    bool SetSoftDisplayFilter(uint8_t enable);                               // 0x000e7b70 (unreferenced)
    bool GetSoftDisplayFilter(uint8_t *enable);                              // 0x000e7ba0 (unreferenced)
    bool SetFlickerFilter(int level);                                        // 0x000e7bc0 (unreferenced)
    bool GetFlickerFilter(int *level);                                       // 0x000e7c10 (unreferenced)
    bool SetShadowFunc(uint32_t func);                                       // 0x000e7c30 (unreferenced)
    bool GetShadowFunc(uint32_t *func);                                      // 0x000e7c50 (unreferenced)
    bool BeginVisibilityTest();                                              // 0x000e7c60
    bool EndVisibilityTest(uint32_t index);                                  // 0x000e7c80
    bool GetVisibilityTestResult(uint32_t index, uint32_t *result);          // 0x000e7ca0
    bool ReadBackBuffer(void *destination);                                  // 0x000e7d00 (unreferenced)
    bool SetGlobal23ff0c(uint32_t value);                                    // 0x000e7d70 (TAR LOD bias override)
    bool GetGlobal23ff0c(float *value);                                      // 0x000e7d80 (unreferenced)
    bool SetGlobal1cb938(uint32_t value);                                    // 0x000e7da0 (unreferenced; filter)
    bool GetGlobal1cb938(uint32_t *value);                                   // 0x000e7db0 (unreferenced)
    bool SetMultiSampleAntiAlias(uint8_t enable);                            // 0x000e7dd0 (unreferenced)
    bool GetMultiSampleAntiAlias(uint8_t *enable);                           // 0x000e7e00 (unreferenced)
    bool SetPushBufferSize(uint32_t size, uint32_t kickOffSize);             // 0x000e7e10 (unreferenced)
    bool GetPushBufferSize(uint32_t *size, uint32_t *kickOffSize);           // 0x000e7e40 (unreferenced)
    bool SetScreenSpaceOffset(float x, float y);                             // 0x000e7e70 (unreferenced)
    bool GetScreenSpaceOffset(float *x, float *y);                           // 0x000e7eb0 (EAGLFont)
    bool SetPointSize(uint32_t size);                                        // 0x000e7ee0 (unreferenced)
    bool GetPointSize(uint32_t *size);                                       // 0x000e7f20 (unreferenced)
    bool SetPointSizeMin(uint32_t size);                                     // 0x000e7f40 (unreferenced)
    bool GetPointSizeMin(uint32_t *size);                                    // 0x000e7f80 (unreferenced)
    bool SetPointSizeMax(uint32_t size);                                     // 0x000e7fa0 (unreferenced)
    bool GetPointSizeMax(uint32_t *size);                                    // 0x000e7fe0 (unreferenced)
    bool SetPointScale(uint32_t a, uint32_t b, uint32_t c);                  // 0x000e8000 (unreferenced)
    bool GetPointScale(uint32_t *a, uint32_t *b, uint32_t *c);               // 0x000e8060 (unreferenced)
    bool SetPointSpriteEnable(uint8_t enable);                               // 0x000e8090 (unreferenced)
    bool GetPointSpriteEnable(uint8_t *enable);                              // 0x000e80d0 (unreferenced)
    bool SetPointScaleEnable(uint8_t enable);                                // 0x000e80f0 (unreferenced)
    bool GetPointScaleEnable(uint8_t *enable);                               // 0x000e8130 (unreferenced)
    uint8_t QueryPal60();                                                    // 0x000e8150
    bool IsDeviceCreated(uint32_t unused);                                   // 0x000e8190 (unreferenced)
    TAR* CopyBackBuffer();                                                   // 0x000e81a0 (unreferenced)
    TAR* GetFrontBuffer();                                                   // 0x000e8270
    TAR* GetBackBuffer();                                                    // 0x000e8350
    TAR* GetDepthBuffer();                                                   // 0x000e8430 (unreferenced)
    void ReleaseCopyTexture();                                               // 0x000e85b0
};

// The private part, at +4 of the object (EAGLInternal::RenderContextPrivate): its constructor (which the
// RenderContext constructor has inlined) and the viewport setter (View.cpp).
struct RenderContextPrivate {
    RenderContext *owner;                // +0x00 (object +0x004)

    // The object this is the private part of: the one 4 bytes below, as the constructor addresses it
    RenderContext* Object() { return reinterpret_cast<RenderContext *>(reinterpret_cast<uint8_t *>(this) - 4); }

    RenderContextPrivate* Construct(RenderContext *owner);                   // 0x000e86f0 (unreferenced)
    void SetCurrentViewPort(ViewPort *viewPort);                             // 0x000ee090
};

}  // namespace EAGL

#endif // DRIVING_EAGL_RENDERCONTEXT_H_
