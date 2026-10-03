#include "View.h"

#include "Loader.h"
#include "Transform.h"
#include "../platform/RealMath.h"

#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGL's device, its texture render contexts, the RenderContext's viewport list, and the viewports
// (docs/driving/eagl.md 2.1, 2.3, 2.4, 3.2, 4.2, 8.7). Each function is the original at the same address, ported
// from its listing: the x87 arithmetic in double in the original's order with a float rounding at every store
// (SetShape's guard-band maths, IsSphereInView's plane tests - which decide culling - and the projection
// adjustments), the frustum planes of SetPerspective in the original x87 instructions because they use FPTAN,
// FPATAN and FSIN unrounded (8.7), and the D3D8 calls through the seam by their original addresses, in order.
//
// Views nest: BeginView remembers the view that was current (ViewPort +0x04) and ends it; EndView re-begins it.
// The "end this view" sequence is inlined in several originals (EndViewOf below).
// ---------------------------------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

using EAGL::ViewPort;
using EAGL::TextureRenderContext;
using EAGL::RenderContextViews;
using EAGL::Device;

namespace {

inline uint32_t &U32(uint32_t address) {
    return *(uint32_t *)(uintptr_t)address;
}

inline float F32(uint32_t address) {
    return *(const float *)(uintptr_t)address;
}

inline uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

inline float FromBits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

// __ftol2: truncates ST0 (the full double) to 64 bits; the caller keeps EAX.
inline int32_t Ftol(double d) {
    return (int32_t)(int64_t)d;
}

// Constants in .rdata
const uint32_t kZero = 0x00189dec, kOne = 0x00189de8, kHalf = 0x00189eb0, kOneOver180 = 0x0018a138,
               kPi = 0x001a09b0;   // and -1 at 0x0018a134 (PerspectivePlanes)
const uint32_t kOneBits = 0x3f800000;

// Globals
const uint32_t kCurrentDevice = 0x0023fb60, kCurrentRenderContext = 0x0023fb64,
               kCurrentTextureRenderContext = 0x0023fb68;
const uint32_t kMalloc = 0x001caf68, kFree = 0x001caf6c;
const uint32_t kViewMatrix = 0x0023f950, kProjectionMatrix = 0x0023fa10, kViewProjectionMatrix = 0x0023fa90;
const uint32_t kIdentityPointer = 0x001cdb70;   // -> 0x001cdb30, an identity MATRIX4
const uint32_t kConstructorPool = 0x0023fbe0, kRuntimeAllocConstructorPool = 0x0023fbb8;
const uint32_t kReturnsTrueAnswer = 0x0023ff14;

inline void *EaglMalloc(uint32_t size, uint32_t name) {
    return ((void *(__cdecl *)(uint32_t, const char *))U32(kMalloc))(size, (const char *)(uintptr_t)name);
}

inline void EaglFree(void *pointer, uint32_t size) {
    ((void (__cdecl *)(void *, uint32_t))U32(kFree))(pointer, size);
}

// EAGL::RenderContext's own methods (another module): called by address.
inline void RenderContextGetSize(RenderContextViews *context, float *width, float *height) {
    ((void (__fastcall *)(void *, int, float *, float *))0x000e6a80)(context, 0, width, height);
}

inline RenderContextViews *RenderContextConstruct(void *memory, Device *device) {
    return ((RenderContextViews *(__fastcall *)(void *, int, Device *))0x000e8740)(memory, 0, device);
}

inline void RenderContextDestruct(RenderContextViews *context) {
    ((void (__fastcall *)(void *, int))0x000e8600)(context, 0);
}

// D3D8 entry points (the seam), by their original addresses
inline void D3DSetRenderTarget(void *target, void *depth) {
    ((void (__stdcall *)(void *, void *))0x00165dc0)(target, depth);
}

inline void D3DSetViewport(const void *viewport) {
    ((void (__stdcall *)(const void *))0x001666d0)(viewport);
}

inline void *D3DTextureGetSurfaceLevel2(void *texture, uint32_t level) {
    return ((void *(__stdcall *)(void *, uint32_t))0x00167330)(texture, level);
}

inline void D3DResourceRelease(void *resource) {
    ((uint32_t (__stdcall *)(void *))0x00169230)(resource);
}

inline void D3DClear(uint32_t count, const void *rects, uint32_t flags, uint32_t colour, float z, uint32_t stencil) {
    ((void (__stdcall *)(uint32_t, const void *, uint32_t, uint32_t, float, uint32_t))0x00168c90)(
        count, rects, flags, colour, z, stencil);
}

inline uint32_t D3DReturnsTrue(uint32_t value) {
    return ((uint32_t (__stdcall *)(uint32_t))0x00169450)(value);   // D3D_ReturnsTrue (docs 2.13)
}

// The inlined "end this view": it is no longer active, and the view it nested over is begun again.
inline void EndViewOf(ViewPort *view) {
    ViewPort *previous = view->previous;
    view->active = 0;
    if (previous != NULL && previous != (ViewPort *)1)
        previous->BeginView();
    view->previous = NULL;
}

// The projection matrix set to identity before a D3DX builder overwrites it, in the two store orders the
// originals use.
inline void IdentityZerosFirst(float *m) {
    uint32_t *w = (uint32_t *)m;
    w[14] = 0; w[13] = 0; w[12] = 0; w[11] = 0; w[9] = 0; w[8] = 0;
    w[7] = 0; w[6] = 0; w[4] = 0; w[3] = 0; w[2] = 0; w[1] = 0;
    w[15] = kOneBits; w[10] = kOneBits; w[5] = kOneBits; w[0] = kOneBits;
}

inline void IdentityOnesFirst(float *m) {
    uint32_t *w = (uint32_t *)m;
    w[15] = kOneBits; w[10] = kOneBits; w[5] = kOneBits; w[0] = kOneBits;
    w[14] = 0; w[13] = 0; w[12] = 0; w[11] = 0; w[9] = 0; w[8] = 0;
    w[7] = 0; w[6] = 0; w[4] = 0; w[3] = 0; w[2] = 0; w[1] = 0;
}

// SetPerspective's frustum planes (0x000e478e..0x000e486b), the original instructions: the half field of view's
// tangent, the four guard-band-adjusted angles through FPATAN, then each plane's tangent and sine, all unrounded
// on the x87 stack except where the original stores a float. ESI is the ViewPort as in the original; the
// original's float temporaries (stored over its fov, near and far argument slots) are three locals here.
__declspec(naked) void __stdcall PerspectivePlanes(ViewPort *view, float fov, float aspect) {
    __asm {
        push esi
        sub esp, 0xc
        // [esp] angle 0, [esp + 4] angle 1, [esp + 8] angle 2; view [esp + 0x14], fov [esp + 0x18],
        // aspect [esp + 0x1c]
        mov esi, dword ptr [esp + 0x14]
        mov eax, 0x001a09b0         // pi
        mov ecx, 0x0018a138         // 1/180
        mov edx, 0x00189eb0         // 0.5
        fld dword ptr [eax]
        fmul dword ptr [ecx]
        fmul dword ptr [esp + 0x18]
        fmul dword ptr [edx]
        fptan
        fstp st(0)
        fld st(0)
        fmul dword ptr [esi + 0x178]
        fchs
        fld1
        fpatan
        fstp dword ptr [esp]
        fld st(0)
        fmul dword ptr [esi + 0x17c]
        fld1
        fpatan
        fstp dword ptr [esp + 4]
        fld dword ptr [esi + 0x180]
        fdiv dword ptr [esp + 0x1c]
        fmul st, st(1)
        fld1
        fpatan
        fstp dword ptr [esp + 8]
        mov ecx, 0x0018a134         // -1
        fld dword ptr [ecx]
        fdiv dword ptr [esp + 0x1c]
        fmul dword ptr [esi + 0x184]
        fmulp st(1), st
        fld1
        fpatan
        fld dword ptr [esp]
        fptan
        fstp st(0)
        fstp dword ptr [esi + 0x158]
        fld dword ptr [eax]
        fmul dword ptr [edx]
        fld dword ptr [esp]
        fsubr st, st(1)
        fsin
        fstp dword ptr [esi + 0x15c]
        fld dword ptr [esp + 4]
        fptan
        fstp st(0)
        fstp dword ptr [esi + 0x160]
        fld dword ptr [esp + 4]
        fsubr st, st(1)
        fsin
        fstp dword ptr [esi + 0x164]
        fld dword ptr [esp + 8]
        fptan
        fstp st(0)
        fstp dword ptr [esi + 0x168]
        fld dword ptr [esp + 8]
        fsubr st, st(1)
        fsin
        fstp dword ptr [esi + 0x16c]
        fld st(1)
        fptan
        fstp st(0)
        fstp dword ptr [esi + 0x170]
        // FSUBRP ST(1), ST(0) (DE E1): ST1 = ST0 - ST1 = pi/2 - angle 3, popped. Emitted as bytes, since
        // assemblers disagree on which mnemonic that encoding is.
        _emit 0xde
        _emit 0xe1
        fsin
        fstp dword ptr [esi + 0x174]
        add esp, 0xc
        pop esi
        ret 0xc
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// ViewPort
// ---------------------------------------------------------------------------------------------------------------

// The rectangle (x, y, width, height, truncated), clamped to the context's size, becomes the D3D viewport. When
// the clamped rectangle is not the one asked for, the projection is scaled and offset so the picture is the
// asked-for rectangle's, cut, and the guard-band factors say how far each edge moved; otherwise those are 1 and 0.
// Ghidra: unnamed (the overlay calls it SetRect).
// FUNC_AT(0x000e4340)
void EAGL::ViewPort::SetShape(float x, float y, float width, float height, float minZ, float maxZ) {
    shape[0] = Ftol(x);
    shape[1] = Ftol(y);
    shape[2] = Ftol(width);
    shape[3] = Ftol(height);
    shapeMaxZ = FromBits(Bits(maxZ));
    shapeMinZ = FromBits(Bits(minZ));
    float contextWidth, contextHeight;
    if (renderContext != NULL)
        RenderContextGetSize(renderContext, &contextWidth, &contextHeight);
    else
        textureRenderContext->GetSize(&contextWidth, &contextHeight);
    float left = FromBits(Bits(x));
    float top = FromBits(Bits(y));
    float right = (float)((double)x + (double)width);
    double bottom = (double)y + (double)height;                // kept on the x87 stack
    if ((double)x < (double)F32(kZero))
        left = FromBits(0);
    if ((double)y < (double)F32(kZero))
        top = FromBits(0);
    if ((double)right < (double)F32(kZero))
        right = FromBits(0);
    if (bottom < (double)F32(kZero))
        bottom = (double)F32(kZero);
    if ((double)left > (double)contextWidth)
        left = contextWidth;
    if ((double)top > (double)contextHeight)
        top = contextHeight;
    if ((double)right > (double)contextWidth)
        right = contextWidth;
    if (bottom > (double)contextHeight)
        bottom = (double)contextHeight;
    viewportX = (uint32_t)Ftol(left);
    viewportY = (uint32_t)Ftol(top);
    double clampedWidth = (double)right - (double)left;
    float widthF = (float)clampedWidth;                        // over contextHeight's slot
    int32_t w = Ftol(clampedWidth);
    viewportWidth = (uint32_t)w;
    double clampedHeight = bottom - (double)top;
    float heightF = (float)clampedHeight;                      // over contextWidth's slot
    int32_t h = Ftol(clampedHeight);
    viewportMaxZ = FromBits(Bits(maxZ));
    viewportHeight = (uint32_t)h;
    viewportMinZ = FromBits(Bits(minZ));
    // FCOMP + TEST AH,0x44 + JP: equal and ordered falls through
    bool unchanged = (double)widthF == (double)width && (double)heightF == (double)height;
    if (!unchanged) {
        if ((uint32_t)h * (uint32_t)w != 0) {
            double scaleX = (double)F32(kOne) / (double)widthF;                 // kept on the stack
            float scaleY = (float)((double)F32(kOne) / (double)heightF);         // stored
            projectionScale[0] = (float)((double)width * scaleX);
            projectionScale[1] = (float)((double)scaleY * (double)height);
            double t = (double)width * (double)F32(kHalf);
            float halfWidth = (float)t;
            float centreX = (float)(t + (double)x);
            double offsetX = (double)centreX - ((double)widthF * (double)F32(kHalf) + (double)left);
            t = (double)height * (double)F32(kHalf);
            float halfHeight = (float)t;
            float centreY = (float)(t + (double)y);
            float offsetY = (float)-((double)centreY - ((double)heightF * (double)F32(kHalf) + (double)top));
            double o = scaleX * offsetX;
            o = o + o;
            projectionOffsetCopy[0] = (float)o;
            projectionOffset[0] = (float)o;
            o = (double)offsetY * (double)scaleY;
            o = o + o;
            projectionOffsetCopy[1] = (float)o;
            projectionOffset[1] = (float)o;
            guardBand[0] = (float)(((double)centreX - (double)left) / (double)halfWidth);
            guardBand[1] = (float)(((double)right - (double)centreX) / (double)halfWidth);
            guardBand[2] = (float)(((double)centreY - (double)top) / (double)halfHeight);
            guardBand[3] = (float)((bottom - (double)centreY) / (double)halfHeight);
            return;
        }
        viewportX = 0;
        viewportY = 0;
        viewportWidth = 1;
        viewportHeight = 1;
    }
    uint32_t *o = (uint32_t *)projectionOffset;
    uint32_t *c = (uint32_t *)projectionOffsetCopy;
    uint32_t *s = (uint32_t *)projectionScale;
    uint32_t *g = (uint32_t *)guardBand;
    o[1] = 0;
    o[0] = 0;
    c[1] = 0;
    c[0] = 0;
    s[1] = kOneBits;
    s[0] = kOneBits;
    g[0] = kOneBits;
    g[1] = kOneBits;
    g[2] = kOneBits;
    g[3] = kOneBits;
}

// FUNC_AT(0x000e4680)
void EAGL::ViewPort::GetShape(float *x, float *y, float *width, float *height, float *minZ, float *maxZ) {
    *x = (float)shape[0];
    *y = (float)shape[1];
    *width = (float)shape[2];
    *height = (float)shape[3];
    *(uint32_t *)minZ = Bits(shapeMinZ);
    *(uint32_t *)maxZ = Bits(shapeMaxZ);
}

// A perspective projection: D3DXMatrixPerspectiveFovRH with the field of view in degrees and an aspect of 1, then
// SetShape's scale and offset, the aspect applied to the y scale, and the frustum planes IsSphereInView tests.
// FUNC_AT(0x000e46d0)
void EAGL::ViewPort::SetPerspective(float fov, float aspect, float nearZ, float farZ) {
    float fovY = (float)((double)F32(kPi) * (double)F32(kOneOver180) * (double)fov);
    IdentityZerosFirst(projection);
    D3DXMatrixPerspectiveFovRH(projection, fovY, FromBits(kOneBits), nearZ, farZ);
    projection[0] = (float)((double)projectionScale[0] * (double)projection[0]);
    perspective[0] = FromBits(Bits(fov));
    double y = (double)projectionScale[1] * (double)projection[5];
    perspective[1] = FromBits(Bits(aspect));
    perspective[2] = FromBits(Bits(nearZ));
    perspective[3] = FromBits(Bits(farZ));
    y = y * (double)aspect;
    projectionType = 0;
    projection[5] = (float)y;
    projection[8] = (float)-(double)projectionOffset[0];
    projection[9] = (float)-(double)projectionOffset[1];
    PerspectivePlanes(this, fov, aspect);
}

// An orthographic projection 2 wide and 2 * height high (D3DXMatrixOrthoRH). Ghidra: unnamed.
// FUNC_AT(0x000e4870)
void EAGL::ViewPort::SetOrthographicScreenSpace(float height, float nearZ, float farZ) {
    float h = (float)((double)height + (double)height);
    IdentityOnesFirst(projection);
    D3DXMatrixOrthoRH(projection, 2.0f, h, nearZ, farZ);
    *(uint32_t *)&perspective[0] = 0;
    perspective[1] = FromBits(Bits(height));
    perspective[2] = FromBits(Bits(nearZ));
    perspective[3] = FromBits(Bits(farZ));
    projectionType = 1;
}

// An orthographic projection over the shape's pixels (0..width, 0..height). The context's size is asked for and
// not used.
// FUNC_AT(0x000e4900)
void EAGL::ViewPort::SetOrthographic(float nearZ, float farZ) {
    IdentityOnesFirst(projection);
    float contextWidth, contextHeight;
    if (renderContext != NULL)
        RenderContextGetSize(renderContext, &contextWidth, &contextHeight);
    else
        textureRenderContext->GetSize(&contextWidth, &contextHeight);
    D3DXMatrixOrthoOffCenterRH(projection, FromBits(0), (float)shape[2], (float)shape[3], FromBits(0), nearZ, farZ);
    projectionType = 1;
}

// FUNC_AT(0x000e49a0)
void EAGL::ViewPort::EndView() {
    EndViewOf(this);
}

// Clears what the flags ask for: 1 colour (D3DCLEAR_TARGET, 0xf0 on the Xbox), 2 Z, 4 stencil, to the background
// colour, Z 1 and stencil 0.
// FUNC_AT(0x000e49d0)
void EAGL::ViewPort::ClearViewPort(uint32_t flags) {
    uint8_t f = (uint8_t)flags;
    uint32_t clear = 0;
    if (f & 1)
        clear = 0xf0;
    if (f & 2)
        clear |= 1;
    if (f & 4)
        clear |= 2;
    D3DClear(0, NULL, clear, backgroundColour, FromBits(kOneBits), 0);
}

// The sphere (centre in world space) against the near and far planes and, in perspective, the four side planes.
// Every comparison is the original's: x > r is "FCOMP r; TEST AH,0x41; JE" (false when unordered), a test against
// 0 "TEST AH,0x41; JNE" (x > 0) or "TEST AH,5; JP" (x < 0).
// FUNC_AT(0x000e4a10)
bool EAGL::ViewPort::IsSphereInView(const float *centre, float radius) {
    alignas(16) Transform viewMatrix;
    viewMatrix.BuildMatrix((const float *)(uintptr_t)kViewMatrix);
    float p[3];
    viewMatrix.TransformPoint(centre, p);
    double r = (double)radius;
    if ((double)p[2] + (double)perspective[2] > r)
        return false;
    if (-((double)p[2] + (double)perspective[3]) > r)
        return false;
    if (projectionType != 0)
        return true;
    double zero = (double)F32(kZero);
    double a = (double)p[2] * (double)planes[2] + (double)p[0];
    if (a > zero) {
        if (a * (double)planes[3] > r)
            return false;
    } else {
        a = (double)p[2] * (double)planes[0] + (double)p[0];
        if (a < zero) {
            if (-(a * (double)planes[1]) > r)
                return false;
        }
    }
    double b = (double)p[2] * (double)planes[4] + (double)p[1];
    if (b > zero)
        return !(b * (double)planes[5] > r);
    b = (double)p[2] * (double)planes[6] + (double)p[1];
    if (b < zero)
        return !(-(b * (double)planes[7]) > r);
    return true;
}

// Candidate SetGuardBandScale (RViewCamera::SetGuardBandSize calls it): empty in this build.
// FUNC_AT(0x000e4b50)
void EAGL::ViewPort::SetGuardBandScale(float scale) {
    (void)scale;
}

// FUNC_AT(0x000e4b60)
float EAGL::ViewPortExtension::GetFov() {
    return viewPort->perspective[0];
}

// FUNC_AT(0x000e4b70)
float EAGL::ViewPortExtension::GetAspect() {
    return viewPort->perspective[1];
}

// FUNC_AT(0x000e4b80)
EAGL::ViewPort* EAGL::ViewPort::GetExtension() {
    return this;
}

// FUNC_AT(0x000e4b90)
EAGL::ViewPortExtension* EAGL::ViewPortExtension::Construct(ViewPort *owner) {
    viewPort = owner;
    return this;
}

// ViewPort::~ViewPort, everything folded away (DeleteViewPort calls it through the thunk at 0x000f3910).
// FUNC_AT(0x000e4ba0)
void EAGL::ViewPort::Destruct() {
}

// FUNC_AT(0x000e4bb0)
EAGL::ViewPortPrivate* EAGL::ViewPortPrivate::Construct(ViewPort *owner) {
    *(uint32_t *)(bytes + 0x18) = 0;      // ViewPort +0x28 render context
    *(uint32_t *)(bytes + 0x1c) = 0;      // +0x2c texture render context
    *(uint32_t *)(bytes + 0x24) = 0;      // +0x34 background colour
    bytes[0x178] = 0;                     // +0x188 active
    *(uint32_t *)(bytes + 0x17c) = 0;     // +0x18c next
    *(ViewPort **)(bytes + 0x180) = owner;  // +0x190 linked
    return this;
}

// Makes this the context's view: the view that was current is remembered and ended (for a RenderContext its own
// "previous" is put back afterwards; for a TextureRenderContext it is left 0 - the original's asymmetry, kept),
// then the render target, the D3D viewport, and view-projection = view * projection, copied with the view and
// projection into the registered globals render methods read.
// FUNC_AT(0x000e4be0)
void EAGL::ViewPort::BeginView() {
    if (renderContext != NULL) {
        if (renderContext->GetCurrentViewPort() != NULL && renderContext->GetCurrentViewPort()->active != 0) {
            ViewPort *current = renderContext->GetCurrentViewPort();
            previous = current;
            ViewPort *itsPrevious = current->previous;
            EndViewOf(current);
            previous->previous = itsPrevious;
        }
    } else {
        if (textureRenderContext->GetCurrentViewPort() != NULL &&
            textureRenderContext->GetCurrentViewPort()->active != 0) {
            ViewPort *current = textureRenderContext->GetCurrentViewPort();
            previous = current;
            EndViewOf(current);
        }
    }
    if (previous == NULL)
        previous = (ViewPort *)1;
    if (renderContext != NULL) {
        ((RenderContextPrivateViews *)&renderContext->privateOwner)->SetCurrentViewPort(this);
        D3DSetRenderTarget(renderContext->renderTarget, renderContext->depthSurface);
    } else {
        ((TextureRenderContextPrivate *)&textureRenderContext->privateOwner)->SetCurrentViewPort(this);
        void *surface = D3DTextureGetSurfaceLevel2(textureRenderContext->texture, 0);
        D3DSetRenderTarget(surface, textureRenderContext->depthSurface);
        D3DResourceRelease(surface);
    }
    D3DSetViewport(&viewportX);
    active = 1;
    alignas(16) Transform viewProjectionMatrix;
    viewProjectionMatrix.BuildMatrix(view);
    float *projectionMatrix = projection;
    viewProjectionMatrix.AppendMatrix(projectionMatrix);
    memcpy(viewProjection, viewProjectionMatrix.m, 64);
    memcpy((void *)(uintptr_t)kViewMatrix, view, 64);
    memcpy((void *)(uintptr_t)kProjectionMatrix, projectionMatrix, 64);
    memcpy((void *)(uintptr_t)kViewProjectionMatrix, viewProjectionMatrix.m, 64);
}

// Projects count points (12-byte strides) to the screen with this view (D3DXVec3Project, identity world), begun
// for the purpose when it was not active. No callers.
// FUNC_AT(0x000e4d80)
void EAGL::ViewPortExtension::Project(int count, const float *points, float *out) {
    bool began = false;
    if (viewPort->active == 0) {
        began = true;
        viewPort->BeginView();
    }
    if (count != 0) {
        int left = count;
        do {
            ViewPort *v = viewPort;
            alignas(16) float world[16];
            IdentityZerosFirst(world);
            D3DXVec3Project(out, points, &v->viewportX, v->projection, v->view, world);
            points += 3;
            out += 3;
        } while (--left != 0);
    }
    if (began)
        EndViewOf(viewPort);
}

// Ends and re-begins the view. No callers.
// FUNC_AT(0x000e4eb0)
void EAGL::ViewPortPrivate::ReBegin() {
    EndViewOf(*(ViewPort **)(bytes + 0x180));
    (*(ViewPort **)(bytes + 0x180))->BeginView();
}

// FUNC_AT(0x000e4ef0)
void EAGL::ViewPort::SetViewMatrix(const float *matrix) {
    uint8_t wasActive = active;
    memcpy(view, matrix, 64);
    if (wasActive != 0) {
        EndViewOf(linked);
        linked->BeginView();
    }
}

// FUNC_AT(0x000f37d0)
EAGL::ViewPort* EAGL::ViewPort::Construct(RenderContextViews *context) {
    ((ViewPortExtension *)this)->Construct(this);
    ((ViewPortPrivate *)&viewportX)->Construct(this);
    const float *identity = *(const float **)(uintptr_t)kIdentityPointer;
    previous = NULL;
    enableModelSphereCull = 0;
    memcpy(projection, identity, 64);
    memcpy(view, identity, 64);
    memcpy(viewProjection, identity, 64);
    textureRenderContext = NULL;
    renderContext = context;
    return this;
}

// ViewPort::ViewPort for a TextureRenderContext (TextureRenderContext::NewViewPort's). Ghidra: unnamed.
// FUNC_AT(0x000f3860)
EAGL::ViewPort* EAGL::ViewPort::ConstructForTexture(TextureRenderContext *context) {
    ((ViewPortExtension *)this)->Construct(this);
    ((ViewPortPrivate *)&viewportX)->Construct(this);
    const float *identity = *(const float **)(uintptr_t)kIdentityPointer;
    previous = NULL;
    enableModelSphereCull = 0;
    memcpy(projection, identity, 64);
    memcpy(view, identity, 64);
    memcpy(viewProjection, identity, 64);
    renderContext = NULL;
    textureRenderContext = context;
    return this;
}

// FUNC_AT(0x000f38f0)
void EAGL::ViewPort::SetEnableModelSphereCull(uint32_t enable) {
    enableModelSphereCull = enable;
}

// FUNC_AT(0x000f3900)
uint32_t EAGL::ViewPort::GetEnableModelSphereCull() {
    return enableModelSphereCull;
}

// thunk_FUN_000e4ba0
// FUNC_AT(0x000f3910)
void EAGL::ViewPort::DestructThunk() {
    Destruct();
}

// FUNC_AT(0x000f3920)
void EAGL::ViewPort::GetPerspective(float *fov, float *aspect, float *nearZ, float *farZ) {
    *(uint32_t *)fov = Bits(perspective[0]);
    *(uint32_t *)aspect = Bits(perspective[1]);
    *(uint32_t *)nearZ = Bits(perspective[2]);
    *(uint32_t *)farZ = Bits(perspective[3]);
}

// FUNC_AT(0x000f3960)
uint32_t EAGL::ViewPort::GetProjectionType() {
    return projectionType;
}

// FUNC_AT(0x000f3970)
void EAGL::ViewPort::SetBackgroundColour(uint32_t colour) {
    backgroundColour = colour;
}

// FUNC_AT(0x000f3980)
void EAGL::ViewPort::GetBackgroundColour(uint32_t *colour) {
    *colour = backgroundColour;
}

// The view matrix (+0x80; docs 4.2 says +0x40, the listing says +0x80). Ghidra: unnamed.
// FUNC_AT(0x000f3990)
float* EAGL::ViewPort::GetViewMatrix() {
    return view;
}

// FUNC_AT(0x000f39a0)
float* EAGL::ViewPort::GetProjectionMatrix() {
    return projection;
}

// FUNC_AT(0x000f39b0)
float* EAGL::ViewPort::GetViewProjectionMatrix() {
    return viewProjection;
}

// ---------------------------------------------------------------------------------------------------------------
// RenderContext's viewports
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000ee010)
EAGL::ViewPort* EAGL::RenderContextViews::NewViewPort() {
    void *memory = EaglMalloc(0x1a0, 0x001ccfb4);   // "EAGL::Viewport new"
    ViewPort *v = memory != NULL ? ((ViewPort *)memory)->Construct(this) : NULL;
    v->next = viewPorts;                            // the original stores through a failed allocation too
    viewPorts = v;
    return v;
}

// FUNC_AT(0x000ee080)
EAGL::ViewPort* EAGL::RenderContextViews::GetCurrentViewPort() {
    return currentViewPort;
}

// FUNC_AT(0x000ee090)
void EAGL::RenderContextPrivateViews::SetCurrentViewPort(ViewPort *viewPort) {
    owner->currentViewPort = viewPort;
}

// Unlinks, destroys and frees the viewport. One not in the list walks off the end and faults, as the original.
// FUNC_AT(0x000ee0a0)
void EAGL::RenderContextViews::DeleteViewPort(ViewPort *viewPort) {
    ViewPort *victim = viewPorts;
    if (viewPort == victim) {
        viewPorts = victim->next;
    } else {
        ViewPort *p = victim;
        while (p != NULL && p->next != viewPort)
            p = p->next;
        victim = p->next;
        p->next = victim->next;
    }
    victim->DestructThunk();
    EaglFree(victim, 0x1a0);
    if (viewPort == currentViewPort)
        currentViewPort = NULL;
}

// FUNC_AT(0x000ee160)
uint8_t* EAGL::RenderContextViews::OffsetSelf() {
    return (uint8_t *)this + *(uint32_t *)((uint8_t *)this + 0x1c);
}

namespace {

uint32_t __cdecl RenderContextWordAt(const uint8_t *self, int index) {
    return *(const uint32_t *)(self + index * 4 + 0x20);
}

uint8_t __cdecl RenderContextSetFloatAt(uint8_t *self, int index, uint32_t valueBits) {
    *(uint32_t *)(self + index * 4 + 0x20) = valueBits;   // FLD/FSTP of a float: the same bits
    return 1;
}

float __cdecl RenderContextFloatAt(const uint8_t *self, int index) {
    return *(const float *)(self + index * 4 + 0x20);
}

int32_t __cdecl ShortAt(const uint8_t *p, int offset) {
    return (int32_t)*(const int16_t *)(p + offset);
}

}  // namespace

// AUTOLTCG
__declspec(naked) void FUN_000ee130() {
    __asm {
        push ecx
        push edx
        push eax
        push ecx
        call RenderContextWordAt
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// The original leaves EAX's upper bytes (the index) under AL = 1.
// AUTOLTCG
__declspec(naked) void FUN_000ee140() {
    __asm {
        push ecx
        push edx
        push eax
        push dword ptr [esp + 0x10]
        push eax
        push ecx
        call RenderContextSetFloatAt
        add esp, 0xc
        mov ecx, eax
        pop eax
        mov al, cl
        pop edx
        pop ecx
        ret
    }
}

// AUTOLTCG
__declspec(naked) void FUN_000ee150() {
    __asm {
        push ecx
        push edx
        push eax
        push ecx
        call RenderContextFloatAt
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// AUTOLTCG
__declspec(naked) void FUN_000ee170() {
    __asm {
        push ecx
        push edx
        push 4
        push eax
        call ShortAt
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// AUTOLTCG
__declspec(naked) void FUN_000ee180() {
    __asm {
        push ecx
        push edx
        push 6
        push eax
        call ShortAt
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// ---------------------------------------------------------------------------------------------------------------
// TextureRenderContext
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000f3450)
EAGL::TextureRenderContext* EAGL::TextureRenderContext::Construct(Device *owner) {
    ((TextureRenderContextExtension *)this)->Construct(this);
    ((TextureRenderContextPrivate *)&privateOwner)->Construct(this);
    device = owner;
    return this;
}

// FUNC_AT(0x000f34b0)
EAGL::ViewPort* EAGL::TextureRenderContext::NewViewPort() {
    void *memory = EaglMalloc(0x1a0, 0x001ccfb4);   // "EAGL::Viewport new"
    ViewPort *v = memory != NULL ? ((ViewPort *)memory)->ConstructForTexture(this) : NULL;
    v->next = viewPorts;
    viewPorts = v;
    return v;
}

// FUNC_AT(0x000f3520)
EAGL::ViewPort* EAGL::TextureRenderContext::GetCurrentViewPort() {
    return currentViewPort;
}

// FUNC_AT(0x000f3530)
void EAGL::TextureRenderContextPrivate::SetCurrentViewPort(ViewPort *viewPort) {
    owner->currentViewPort = viewPort;
}

// As RenderContext's, but the current viewport is not cleared when it is the one deleted.
// FUNC_AT(0x000f3540)
void EAGL::TextureRenderContext::DeleteViewPort(ViewPort *viewPort) {
    ViewPort *victim = viewPorts;
    if (viewPort == victim) {
        viewPorts = victim->next;
    } else {
        ViewPort *p = victim;
        while (p != NULL && p->next != viewPort)
            p = p->next;
        victim = p->next;
        p->next = victim->next;
    }
    victim->DestructThunk();
    EaglFree(victim, 0x1a0);
}

// FUNC_AT(0x000f35a0)
void EAGL::TextureRenderContext::Destruct() {
    while (viewPorts != NULL)
        DeleteViewPort(viewPorts);
    ((TextureRenderContextExtension *)this)->Destruct();
}

// FUNC_AT(0x000f3600)
void EAGL::TextureRenderContext::BeginFrame() {
    ((DevicePrivate *)&Device::Get()->privatePart)->SetCurrentTextureRenderContext(this);
    inFrame = 1;
}

// FUNC_AT(0x000f3620)
void EAGL::TextureRenderContext::EndFrame() {
    ((DevicePrivate *)&Device::Get()->privatePart)->SetCurrentTextureRenderContext(NULL);
    inFrame = 0;
}

// The size as kept: integers (FILD).
// FUNC_AT(0x000f3640)
void EAGL::TextureRenderContext::GetSize(float *w, float *h) {
    *w = (float)width;
    *h = (float)height;
}

// FUNC_AT(0x000f3660)
int32_t EAGL::TextureRenderContext::GetColourFormat() {
    return colourFormat;
}

// FUNC_AT(0x000f3670)
int32_t EAGL::TextureRenderContext::GetDepthFormat() {
    return depthFormat;
}

// FUNC_AT(0x000f3680)
bool EAGL::TextureRenderContext::UnsupportedF3680(uint32_t value) {
    (void)value;
    return false;
}

// FUNC_AT(0x000f3690)
bool EAGL::TextureRenderContext::UnsupportedF3690(uint32_t value) {
    (void)value;
    return false;
}

// FUNC_AT(0x000f36a0)
bool EAGL::TextureRenderContext::UnsupportedF36A0(uint32_t value) {
    (void)value;
    return false;
}

// FUNC_AT(0x000f36b0)
bool EAGL::TextureRenderContext::UnsupportedF36B0(uint32_t value) {
    (void)value;
    return false;
}

// FUNC_AT(0x000f36c0)
bool EAGL::TextureRenderContext::UnsupportedF36C0(uint32_t value) {
    (void)value;
    return false;
}

// FUNC_AT(0x000f36d0)
bool EAGL::TextureRenderContext::UnsupportedF36D0(uint32_t value) {
    (void)value;
    return false;
}

// Takes the size, surface and format from the colour texture's header (+0x40 of the object given) and the depth
// surface and format from the depth texture's, if there is one.
// FUNC_AT(0x000f36e0)
uint32_t EAGL::TextureRenderContext::SetupFrameBuffers(const void *colourTexture, const void *depthTexture) {
    const uint8_t *colour = *(const uint8_t *const *)((const uint8_t *)colourTexture + 0x40);
    const uint8_t *depth = NULL;
    if (depthTexture != NULL)
        depth = *(const uint8_t *const *)((const uint8_t *)depthTexture + 0x40);
    height = *(const int32_t *)(colour + 0xc);
    width = *(const int32_t *)(colour + 8);
    texture = *(void *const *)(colour + 0x28);
    colour = *(const uint8_t *const *)((const uint8_t *)colourTexture + 0x40);
    colourFormat = (int32_t)(*(const uint32_t *)(colour + 0x10) << 0x12) >> 0x18;
    depthSurface = NULL;
    if (depthTexture != NULL) {
        depthSurface = *(void *const *)(depth + 0x28);
        const uint8_t *d = *(const uint8_t *const *)((const uint8_t *)depthTexture + 0x40);
        depthFormat = (int32_t)(*(const uint32_t *)(d + 0x10) << 0x12) >> 0x18;
    }
    return 1;
}

// FUNC_AT(0x000f3750)
EAGL::TextureRenderContextExtension* EAGL::TextureRenderContextExtension::Construct(TextureRenderContext *context) {
    owner = context;
    return this;
}

// FUNC_AT(0x000f3760)
void EAGL::TextureRenderContextExtension::Destruct() {
}

// FUNC_AT(0x000f3770)
EAGL::ZeroedWords4* EAGL::ZeroedWords4::Construct() {
    words[0] = 0;
    words[1] = 0;
    words[2] = 0;
    words[3] = 0;
    return this;
}

// FUNC_AT(0x000f3780)
EAGL::ZeroedWords3* EAGL::ZeroedWords3::Construct() {
    words[0] = 0;
    words[1] = 0;
    words[2] = 0;
    return this;
}

// At TextureRenderContext +0x04: the context's +0x08..+0x30 and +0x84 cleared.
// FUNC_AT(0x000f3790)
EAGL::TextureRenderContextPrivate* EAGL::TextureRenderContextPrivate::Construct(TextureRenderContext *context) {
    uint32_t *w = (uint32_t *)this;
    owner = context;
    for (int i = 1; i <= 0xb; i++)
        w[i] = 0;
    w[0x20] = 0;
    return this;
}

// ---------------------------------------------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000e4f50)
void *eagl_alloc(uint32_t size, const char *name) {
    (void)name;
    return ((void *(__cdecl *)(uint32_t))0x001340e3)(size);   // _malloc
}

// FUNC_AT(0x000e4f60)
void eagl_free(void *pointer, uint32_t size) {
    (void)size;
    ((void (__cdecl *)(void *))0x001331dc)(pointer);          // free
}

// Registers the load-time types in the ConstructorPool, the property-built ones in the RuntimeAllocConstructorPool,
// the viewport matrices and the render-method constants by name (docs 2.1). The constructors and destructors are
// registered by their original addresses, as the original stores them.
// FUNC_AT(0x000e4f70)
bool EAGL::Device::Init() {
    initialised = 1;
    U32(kCurrentDevice) = (uint32_t)(uintptr_t)this;
    ConstructorPool *pool = (ConstructorPool *)(uintptr_t)kConstructorPool;
    RuntimeAllocConstructorPool *runtimePool = (RuntimeAllocConstructorPool *)(uintptr_t)kRuntimeAllocConstructorPool;
    pool->AddType((const char *)0x001cb0b8u, (void *)0x000ed070u, (void *)0x000ed2b0u);   // "EAGL::TAR"
    pool->AddType((const char *)0x001cb0c4u, (void *)0x000f11c0u, (void *)0x000f1390u);   // "RenderMethod"
    pool->AddType((const char *)0x001cb0d4u, (void *)0x000ea1d0u, (void *)0x000e8b40u);   // "Model"
    pool->AddType((const char *)0x001cb0dcu, (void *)0x000f0ee0u, (void *)0x000f0f10u);   // "VertexBuffer"
    runtimePool->AddType((const char *)0x001cb0ecu, (void *)0x000ef4c0u, (void *)0x000ef780u);  // "EAGL::GeoPrimState"
    runtimePool->AddType((const char *)0x001cb100u, (void *)0x000ecbe0u, (void *)0x000ed2d0u);  // "EAGL::TAR"
    EAGL_SymbolInit();
    DynamicLoader::RegisterVar((const char *)0x001cb10cu, (void *)0x001cdab0u);   // gReflectionConstants
    DynamicLoader::RegisterVar((const char *)0x001cb140u, (void *)0x001cdac0u);   // gSkinningConstants
    DynamicLoader::RegisterVar((const char *)0x001cb174u, (void *)0x001cdad0u);   // gZeroOneTwoThree
    DynamicLoader::RegisterVar((const char *)0x001cb1a4u, (void *)0x001cdae0u);   // gShadowColour
    DynamicLoader::RegisterVar((const char *)0x001cb1d0u, (void *)0x001cdaf0u);   // gEnvMapConstants
    uint32_t answer = D3DReturnsTrue(0);
    U32(kReturnsTrueAnswer) = answer;
    return answer != 0;
}

// FUNC_AT(0x000e5080)
EAGL::DevicePrivate* EAGL::DevicePrivate::Construct() {
    words[0] = 0;
    words[3] = 0;
    words[4] = 0;
    *(uint8_t *)&words[5] = 0;
    return this;
}

// FUNC_AT(0x000e5090)
void EAGL::DevicePrivate::Destruct() {
}

// FUNC_AT(0x000e50a0)
EAGL::DeviceExtension* EAGL::DeviceExtension::Construct(Device *owner) {
    device = owner;
    return this;
}

// FUNC_AT(0x000e50b0)
void EAGL::DeviceExtension::Destruct() {
}

// FUNC_AT(0x000e50c0)
EAGL::Device* EAGL::Device::Construct() {
    extension = this;
    privatePart = 0;
    renderContexts = NULL;
    textureRenderContexts = NULL;
    initialised = 0;
    U32(kCurrentDevice) = (uint32_t)(uintptr_t)this;
    privatePart = 0;
    renderContexts = NULL;
    return this;
}

// FUNC_AT(0x000e50e0)
void EAGL::Device::Destruct() {
    while (renderContexts != NULL)
        DeleteRenderContext(renderContexts);
    while (textureRenderContexts != NULL)
        ((DeviceExtension *)this)->DeleteTextureRenderContext(textureRenderContexts);
    DynamicLoader::UnRegisterVar((const char *)0x001caf70u);   // gReflectionConstants
    DynamicLoader::UnRegisterVar((const char *)0x001cafa4u);   // gSkinningConstants
    DynamicLoader::UnRegisterVar((const char *)0x001cafd8u);   // gZeroOneTwoThree
    DynamicLoader::UnRegisterVar((const char *)0x001cb008u);   // gShadowColour
    DynamicLoader::UnRegisterVar((const char *)0x001cb034u);   // gEnvMapConstants
    ConstructorPool *pool = (ConstructorPool *)(uintptr_t)kConstructorPool;
    RuntimeAllocConstructorPool *runtimePool = (RuntimeAllocConstructorPool *)(uintptr_t)kRuntimeAllocConstructorPool;
    pool->RemoveType((const char *)0x001cb064u);          // "EAGL::TAR"
    pool->RemoveType((const char *)0x001cb070u);          // "RenderMethod"
    pool->RemoveType((const char *)0x001cb080u);          // "Model"
    pool->RemoveType((const char *)0x001cb088u);          // "VertexBuffer"
    runtimePool->RemoveType((const char *)0x001cb098u);   // "EAGL::TAR"
    runtimePool->RemoveType((const char *)0x001cb0a4u);   // "EAGL::GeoPrimState"
}

// FUNC_AT(0x000e51b0)
void EAGL_EmptyE51B0() {
}

// FUNC_AT(0x000e51c0)
void EAGL_EmptyE51C0() {
}

// A new RenderContext, put at the head of the list and made current.
// FUNC_AT(0x000e8900)
EAGL::RenderContextViews* EAGL::Device::NewRenderContext() {
    void *memory = EaglMalloc(0x14c, 0x001cb9ac);   // "RenderContext new"
    RenderContextViews *context = memory != NULL ? RenderContextConstruct(memory, this) : NULL;
    context->next = renderContexts;
    renderContexts = context;
    U32(kCurrentRenderContext) = (uint32_t)(uintptr_t)context;
    return context;
}

// FUNC_AT(0x000e8970)
EAGL::TextureRenderContext* EAGL::DeviceExtension::NewTextureRenderContext() {
    void *memory = EaglMalloc(0xd4, 0x001cb9c0);    // "TextureRenderContext new"
    TextureRenderContext *context = memory != NULL ? ((TextureRenderContext *)memory)->Construct(device) : NULL;
    context->next = device->textureRenderContexts;
    device->textureRenderContexts = context;
    return context;
}

// FUNC_AT(0x000e89e0)
EAGL::RenderContextViews* EAGL::Device::GetCurrentRenderContext() {
    return (RenderContextViews *)(uintptr_t)U32(kCurrentRenderContext);
}

// FUNC_AT(0x000e89f0)
EAGL::TextureRenderContext* EAGL::Device::GetCurrentTextureRenderContext() {
    return (TextureRenderContext *)(uintptr_t)U32(kCurrentTextureRenderContext);
}

// FUNC_AT(0x000e8a00)
void EAGL::DevicePrivate::SetCurrentRenderContext(RenderContextViews *context) {
    U32(kCurrentRenderContext) = (uint32_t)(uintptr_t)context;
}

// FUNC_AT(0x000e8a10)
void EAGL::DevicePrivate::SetCurrentTextureRenderContext(TextureRenderContext *context) {
    U32(kCurrentTextureRenderContext) = (uint32_t)(uintptr_t)context;
}

// FUNC_AT(0x000e8a20)
void EAGL::Device::SetNewOverride(void *allocator) {
    U32(kMalloc) = (uint32_t)(uintptr_t)allocator;
}

// FUNC_AT(0x000e8a30)
void EAGL::Device::SetDeleteOverride(void *deallocator) {
    U32(kFree) = (uint32_t)(uintptr_t)deallocator;
}

// FUNC_AT(0x000e8a40)
EAGL::Device* EAGL::Device::Get() {
    return (Device *)(uintptr_t)U32(kCurrentDevice);
}

// Unlinks, destroys and frees the context; the current one becomes the list's head if it was this.
// FUNC_AT(0x000e8a50)
void EAGL::Device::DeleteRenderContext(RenderContextViews *context) {
    RenderContextViews *victim = renderContexts;
    if (context == victim) {
        renderContexts = victim->next;
    } else {
        RenderContextViews *p = victim;
        while (p != NULL && p->next != context)
            p = p->next;
        victim = p->next;
        p->next = victim->next;
    }
    RenderContextDestruct(victim);
    EaglFree(victim, 0x14c);
    if (U32(kCurrentRenderContext) == (uint32_t)(uintptr_t)context)
        U32(kCurrentRenderContext) = (uint32_t)(uintptr_t)renderContexts;
}

// The texture contexts' DeleteRenderContext (Device::~Device's); the current one is left as it is.
// FUNC_AT(0x000e8ad0)
void EAGL::DeviceExtension::DeleteTextureRenderContext(TextureRenderContext *context) {
    Device *d = device;
    TextureRenderContext *victim = d->textureRenderContexts;
    if (context == victim) {
        d->textureRenderContexts = victim->next;
    } else {
        TextureRenderContext *p = victim;
        while (p != NULL && p->next != context)
            p = p->next;
        victim = p->next;
        p->next = victim->next;
    }
    victim->Destruct();
    EaglFree(victim, 0xd4);
}
