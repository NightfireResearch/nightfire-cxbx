#ifndef DRIVING_EAGL_VIEW_H_
#define DRIVING_EAGL_VIEW_H_

// EAGL's device, its texture render contexts, and the viewports of both kinds of context (docs/driving/eagl.md 2.1,
// 2.3, 2.4, 3.2, 4.2, 8.7). See View.cpp. In namespace EAGL: the game-side overlay in render/RenderState.hpp has a
// global ViewPort and RenderContext of its own.
//
// EAGL::RenderContext itself is RenderContext.h's (0x000e6610..0x000e8900); its viewport-list methods
// (0x000ee010..0x000ee190) are defined in View.cpp.

#include <stdint.h>

#include "RenderContext.h"

namespace EAGL {

struct Device;
struct TAR;
struct TextureRenderContext;
struct ViewPort;

// The ViewPort's extension at +0x00: its one word is the ViewPort.
struct ViewPortExtension {
    ViewPort *viewPort;

    ViewPortExtension* Construct(ViewPort *owner);                           // 0x000e4b90
    float GetFov();                                                          // 0x000e4b60 (invented; no callers)
    float GetAspect();                                                       // 0x000e4b70 (invented; no callers)
    void Project(int count, const float *points, float *out);                // 0x000e4d80 (invented; no callers)
};

// EAGLInternal::ViewPortPrivate, at ViewPort +0x10 (it starts with the D3DVIEWPORT8). Its methods address the
// ViewPort's fields from there: Object() is the ViewPort.
struct ViewPortPrivate {
    ViewPort* Object();

    ViewPortPrivate* Construct(ViewPort *owner);                             // 0x000e4bb0
    void ReBegin();                                                          // 0x000e4eb0 (no callers)
};

// D3DVIEWPORT8
struct D3DViewport8 {
    uint32_t x;                          // +0x00
    uint32_t y;                          // +0x04
    uint32_t width;                      // +0x08
    uint32_t height;                     // +0x0c
    float minZ;                          // +0x10
    float maxZ;                          // +0x14
};
static_assert(sizeof(D3DViewport8) == 0x18, "D3DVIEWPORT8 is 0x18 bytes");

struct ViewPort {                        // 0x1a0
    ViewPort *extension;                 // +0x000 ViewPortExtension: this
    ViewPort *previous;                  // +0x004 the view this one nested over; 0 none, 1 nothing to restore
    uint32_t enableModelSphereCull;      // +0x008
    uint32_t unknown00c;                 // +0x00c
    D3DViewport8 viewport;               // +0x010 (ViewPortPrivate starts here)
    RenderContext *renderContext;        // +0x028 or
    TextureRenderContext *textureRenderContext;  // +0x02c
    uint32_t projectionType;             // +0x030 0 perspective, 1 orthographic
    uint32_t backgroundColour;           // +0x034
    uint32_t unknown038[2];              // +0x038
    float projection[16];                // +0x040
    float view[16];                      // +0x080
    float viewProjection[16];            // +0x0c0
    uint8_t unknown100[0x18];            // +0x100
    int32_t shape[4];                    // +0x118 x, y, width, height as SetShape got them (truncated)
    float shapeMinZ;                     // +0x128
    float shapeMaxZ;                     // +0x12c
    float perspective[4];                // +0x130 fov (degrees), aspect, near, far
    float projectionScale[2];            // +0x140
    float projectionOffset[2];           // +0x148
    float projectionOffsetCopy[2];       // +0x150
    float planes[8];                     // +0x158 four frustum planes as (tan, sin) pairs
    float guardBand[4];                  // +0x178
    uint8_t active;                      // +0x188
    uint8_t pad189[3];
    ViewPort *next;                      // +0x18c the context's list
    ViewPort *linked;                    // +0x190 (ViewPortPrivate +0x180) this
    uint8_t unknown194[0xc];             // +0x194

    // The extension and the private part are views of this object, at +0x00 and +0x10
    ViewPortExtension* Extension() { return reinterpret_cast<ViewPortExtension *>(this); }
    ViewPortPrivate* Private() { return reinterpret_cast<ViewPortPrivate *>(&viewport); }

    ViewPort* Construct(RenderContext *context);                             // 0x000f37d0
    ViewPort* ConstructForTexture(TextureRenderContext *context);            // 0x000f3860
    void Destruct();                                                         // 0x000e4ba0 (empty)
    void DestructThunk();                                                    // 0x000f3910 (jumps to Destruct)
    void SetShape(float x, float y, float width, float height, float minZ, float maxZ);       // 0x000e4340
    void GetShape(float *x, float *y, float *width, float *height, float *minZ, float *maxZ); // 0x000e4680
    void SetPerspective(float fov, float aspect, float nearZ, float farZ);   // 0x000e46d0 (candidate name)
    void SetOrthographicScreenSpace(float height, float nearZ, float farZ);  // 0x000e4870 (candidate name)
    void SetOrthographic(float nearZ, float farZ);                           // 0x000e4900
    void EndView();                                                          // 0x000e49a0
    void ClearViewPort(uint32_t flags);                                      // 0x000e49d0
    bool IsSphereInView(const float *centre, float radius);                  // 0x000e4a10
    void SetGuardBandScale(float scale);                                     // 0x000e4b50 (candidate; empty)
    ViewPort* GetExtension();                                                // 0x000e4b80 (invented; no callers)
    void BeginView();                                                        // 0x000e4be0
    void SetViewMatrix(const float *matrix);                                 // 0x000e4ef0
    void SetEnableModelSphereCull(uint32_t enable);                          // 0x000f38f0 (no callers)
    uint32_t GetEnableModelSphereCull();                                     // 0x000f3900
    void GetPerspective(float *fov, float *aspect, float *nearZ, float *farZ);  // 0x000f3920 (no callers)
    uint32_t GetProjectionType();                                            // 0x000f3960 (no callers)
    void SetBackgroundColour(uint32_t colour);                               // 0x000f3970
    void GetBackgroundColour(uint32_t *colour);                              // 0x000f3980 (no callers)
    float* GetViewMatrix();                                                  // 0x000f3990
    float* GetProjectionMatrix();                                            // 0x000f39a0 (no callers)
    float* GetViewProjectionMatrix();                                        // 0x000f39b0
};
static_assert(sizeof(ViewPort) == 0x1a0, "a ViewPort is 0x1a0 bytes");

inline ViewPort* ViewPortPrivate::Object() {
    return reinterpret_cast<ViewPort *>(reinterpret_cast<uint8_t *>(this) - 0x10);
}

struct TextureRenderContextExtension;
struct TextureRenderContextPrivate;

// EAGL::TextureRenderContext (0xd4): a render-to-texture context.
struct TextureRenderContext {
    TextureRenderContext *extension;     // +0x00 TextureRenderContextExtension: this
    TextureRenderContext *privateOwner;  // +0x04 TextureRenderContextPrivate: this
    uint32_t unknown08[2];               // +0x08
    int32_t width;                       // +0x10
    int32_t height;                      // +0x14
    uint32_t unknown18;                  // +0x18
    int32_t colourFormat;                // +0x1c bits 6..13 of the texture's format word, signed
    int32_t depthFormat;                 // +0x20 the same of the depth texture's
    ViewPort *currentViewPort;           // +0x24
    ViewPort *viewPorts;                 // +0x28 list, linked through ViewPort::next
    uint32_t unknown2c;                  // +0x2c
    TextureRenderContext *next;          // +0x30 the Device's list
    SurfaceTexture *texture;             // +0x34 D3D8 texture rendered into (the colour TAR's)
    SurfaceTexture *depthSurface;        // +0x38 the depth TAR's texture, the depth buffer
    uint8_t unknown3c[0x48];             // +0x3c
    uint32_t inFrame;                    // +0x84
    uint8_t unknown88[0x48];             // +0x88
    Device *device;                      // +0xd0

    // The extension and the private part are views of this object, at +0x00 and +0x04
    TextureRenderContextExtension* Extension() { return reinterpret_cast<TextureRenderContextExtension *>(this); }
    TextureRenderContextPrivate* Private() { return reinterpret_cast<TextureRenderContextPrivate *>(&privateOwner); }

    TextureRenderContext* Construct(Device *owner);                          // 0x000f3450
    void Destruct();                                                         // 0x000f35a0
    ViewPort* NewViewPort();                                                 // 0x000f34b0
    ViewPort* GetCurrentViewPort();                                          // 0x000f3520
    void DeleteViewPort(ViewPort *viewPort);                                 // 0x000f3540
    void BeginFrame();                                                       // 0x000f3600
    void EndFrame();                                                         // 0x000f3620
    void GetSize(float *width, float *height);                               // 0x000f3640
    int32_t GetColourFormat();                                               // 0x000f3660 (invented; no callers)
    int32_t GetDepthFormat();                                                // 0x000f3670 (invented; no callers)
    bool UnsupportedF3680(uint32_t value);                                   // 0x000f3680 .. 0x000f36d0: six
    bool UnsupportedF3690(uint32_t value);                                   // setters the texture context
    bool UnsupportedF36A0(uint32_t value);                                   // does not keep (false; no
    bool UnsupportedF36B0(uint32_t value);                                   // callers)
    bool UnsupportedF36C0(uint32_t value);
    bool UnsupportedF36D0(uint32_t value);
    uint32_t SetupFrameBuffers(const TAR *colourTexture, const TAR *depthTexture);  // 0x000f36e0
};
static_assert(sizeof(TextureRenderContext) == 0xd4, "a TextureRenderContext is 0xd4 bytes");

struct TextureRenderContextExtension {
    TextureRenderContext *owner;

    TextureRenderContextExtension* Construct(TextureRenderContext *context); // 0x000f3750
    void Destruct();                                                         // 0x000f3760 (empty)
};

// EAGLInternal::TextureRenderContextPrivate, at TextureRenderContext +0x04.
struct TextureRenderContextPrivate {
    TextureRenderContext *owner;

    // The object this is the private part of: the one 4 bytes below, as the constructor addresses it
    TextureRenderContext* Object() {
        return reinterpret_cast<TextureRenderContext *>(reinterpret_cast<uint8_t *>(this) - 4);
    }

    TextureRenderContextPrivate* Construct(TextureRenderContext *context);   // 0x000f3790
    void SetCurrentViewPort(ViewPort *viewPort);                             // 0x000f3530
};

// Two unreferenced constructors that zero four and three words (0x000f3770, 0x000f3780; class unknown).
struct ZeroedWords4 {
    uint32_t words[4];
    ZeroedWords4* Construct();                                               // 0x000f3770
};
struct ZeroedWords3 {
    uint32_t words[3];
    ZeroedWords3* Construct();                                               // 0x000f3780
};

struct DeviceExtension;
struct DevicePrivate;

// EAGL::Device (0x1c); one, in RRenderer.
struct Device {
    Device *extension;                   // +0x00 DeviceExtension: this
    uint32_t privatePart;                // +0x04 DevicePrivate starts here (0)
    uint32_t unknown08[2];               // +0x08
    RenderContext *renderContexts;       // +0x10
    TextureRenderContext *textureRenderContexts;  // +0x14
    uint8_t initialised;                 // +0x18
    uint8_t pad19[3];

    // The extension and the private part are views of this object, at +0x00 and +0x04
    DeviceExtension* Extension() { return reinterpret_cast<DeviceExtension *>(this); }
    DevicePrivate* Private() { return reinterpret_cast<DevicePrivate *>(&privatePart); }

    Device* Construct();                                                     // 0x000e50c0
    void Destruct();                                                         // 0x000e50e0
    bool Init();                                                             // 0x000e4f70
    RenderContext* NewRenderContext();                                       // 0x000e8900
    void DeleteRenderContext(RenderContext *context);                        // 0x000e8a50
    RenderContext* GetCurrentRenderContext();                                // 0x000e89e0
    TextureRenderContext* GetCurrentTextureRenderContext();                  // 0x000e89f0
    static void SetNewOverride(void *allocator);                             // 0x000e8a20
    static void SetDeleteOverride(void *deallocator);                        // 0x000e8a30
    static Device* Get();                                                    // 0x000e8a40
};
static_assert(sizeof(Device) == 0x1c, "a Device is 0x1c bytes");

// EAGL::DeviceExtension, the Device itself (its first word is the Device).
struct DeviceExtension {
    Device *device;

    DeviceExtension* Construct(Device *owner);                               // 0x000e50a0 (invented; no callers)
    void Destruct();                                                         // 0x000e50b0 (empty; no callers)
    TextureRenderContext* NewTextureRenderContext();                         // 0x000e8970
    void DeleteTextureRenderContext(TextureRenderContext *context);          // 0x000e8ad0 (invented)
};

// EAGLInternal::DevicePrivate, at Device +0x04. The setters keep the current contexts in globals.
struct DevicePrivate {
    // The Device this is the private part of: the one 4 bytes below
    Device* Object() { return reinterpret_cast<Device *>(reinterpret_cast<uint8_t *>(this) - 4); }

    DevicePrivate* Construct();                                              // 0x000e5080 (invented; no callers)
    void Destruct();                                                         // 0x000e5090 (empty; no callers)
    void SetCurrentRenderContext(RenderContext *context);                    // 0x000e8a00
    void SetCurrentTextureRenderContext(TextureRenderContext *context);      // 0x000e8a10
};

}  // namespace EAGL

// EAGL's default allocator pair (what 0x001caf68 / 0x001caf6c hold until RRenderer overrides them).
void *eagl_alloc(uint32_t size, const char *name);                          // 0x000e4f50
void eagl_free(void *pointer, uint32_t size);                               // 0x000e4f60

// Two empty functions after Device::~Device (0x000e51b0, 0x000e51c0; no callers, names invented).
void EAGL_EmptyE51B0();
void EAGL_EmptyE51C0();

// The RenderContext accessors 0x000ee130..0x000ee180 (no callers): five take register arguments (EAX = index and
// ECX = the context, or a pointer in EAX) and touch nothing but EAX, so they are naked adapters under Ghidra's
// names (AUTOLTCG: the injection table's ABI check refuses an EAX argument under FUNC_AT). 0x000ee160 reads only
// ECX and is RenderContext::OffsetSelf.
void FUN_000ee130();                    // EAX = [ECX + EAX * 4 + 0x20]
void FUN_000ee140();                    // [ECX + EAX * 4 + 0x20] = the float argument (caller pops), AL = 1
void FUN_000ee150();                    // ST0 = [ECX + EAX * 4 + 0x20]
void FUN_000ee170();                    // EAX = (short) [EAX + 4]
void FUN_000ee180();                    // EAX = (short) [EAX + 6]

#endif // DRIVING_EAGL_VIEW_H_
