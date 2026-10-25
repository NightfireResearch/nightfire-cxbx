#include "View.h"
#include "../../common/math/Libm.h"

#include "Loader.h"
#include "Tar.h"
#include "Transform.h"
#include "D3D8State.h"
#include "EaglGlobals.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../../helpers.h"
#include "../platform/CRuntime.h"
#include "Model.h"                   // ModelConstructor, ModelDestructor
#include "RenderMethod.h"            // the render method and vertex buffer constructors
#include "RuntimeAlloc.h"            // the property-built constructors

#include <bit>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// The render methods' constant vectors, registered by name: gReflectionConstants, gSkinningConstants,
// gZeroOneTwoThree, gShadowColour, gEnvMapConstants
// XBE_GLOBAL(0x001cdab0, 0x50)
alignas(16) float RenderMethodConstants[5][4] = {
    { 1.0f, -1.0f, 1.0f, 1.0f },
    { 1.0f, 2.0f, 3.0f, 4.0f },
    { 0.0f, 1.0f, 2.0f, 3.0f },
    { 1.0f, 0.01f, 0.01f, 0.01f },
    { 1.0f, 0.25f, 0.5f, 0.75f },
};

// The matrices render methods read by name (EaglGlobals.h)
// XBE_GLOBAL(0x0023f950, 0x40)
alignas(16) float ViewMatrix[16];
// XBE_GLOBAL(0x0023f990, 0x40)
alignas(16) float ModelViewMatrix[16];
// XBE_GLOBAL(0x0023f9d0, 0x40)
alignas(16) float ModelViewProjectionMatrix[16];
// XBE_GLOBAL(0x0023fa10, 0x40)
alignas(16) float ProjectionMatrix[16];
// XBE_GLOBAL(0x0023fa50, 0x40)
alignas(16) float ModelMatrix[16];
// XBE_GLOBAL(0x0023fa90, 0x40)
alignas(16) float ViewProjectionMatrix[16];

// ---------------------------------------------------------------------------------------------------------------
// EAGL's device, its texture render contexts, the RenderContext's viewport list, and the viewports
// (docs/driving/eagl.md 2.1, 2.3, 2.4, 3.2, 4.2, 8.7). Each function is the original at the same address, ported
// from its listing: the x87 arithmetic in double in the original's order with a float rounding at every store
// (SetShape's guard-band maths, IsSphereInView's plane tests - which decide culling - and the projection
// adjustments), the frustum planes of SetPerspective in the original x87 instructions because they use FPTAN,
// FPATAN and FSIN unrounded (8.7), and the D3D8 calls through the seam (../gfx/D3D8.h), in order.
//
// Views nest: BeginView remembers the view that was current (ViewPort::previous) and ends it; EndView re-begins
// it. The "end this view" sequence is inlined in several originals (EndViewOf below).
// ---------------------------------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

using EAGL::Device;
using EAGL::RenderContext;
using EAGL::TextureRenderContext;
using EAGL::ViewPort;

// ---- globals

// (EAGL's allocator pair is eagl_alloc / eagl_free until RRenderer overrides it through Device::SetNewOverride;
// it, the registered matrices and the pools are EaglGlobals.h's.)

// XBE_GLOBAL(0x0023fb60, 4)
Device *CurrentDevice;
// XBE_GLOBAL(0x0023fb64, 4)
RenderContext *CurrentRenderContext;
// XBE_GLOBAL(0x0023fb68, 4)
TextureRenderContext *CurrentTextureRenderContext;
// XBE_GLOBAL(0x0023ff14, 4)
static uint32_t ReturnsTrueAnswer;

namespace {

const float kIdentity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

constexpr float kPi = 3.14159265f;
static_assert(std::bit_cast<uint32_t>(kPi) == 0x40490fdb, "pi as the original's float");
constexpr float kOneOver180 = 1.0f / 180.0f;
static_assert(std::bit_cast<uint32_t>(kOneOver180) == 0x3bb60b61, "1/180 as the original's float");
const float kHalf = 0.5f;
const float kMinusOne = -1.0f;

// The inlined "end this view": it is no longer active, and the view it nested over is begun again.
void EndViewOf(ViewPort *view) {
    ViewPort *previous = view->previous;
    view->active = 0;
    if (previous != NULL && previous != reinterpret_cast<ViewPort *>(1))
        previous->BeginView();
    view->previous = NULL;
}

// The projection matrix set to identity before a D3DX builder overwrites it, in the two store orders the
// originals use.
void IdentityZerosFirst(float *m) {
    m[14] = 0.0f; m[13] = 0.0f; m[12] = 0.0f; m[11] = 0.0f; m[9] = 0.0f; m[8] = 0.0f;
    m[7] = 0.0f; m[6] = 0.0f; m[4] = 0.0f; m[3] = 0.0f; m[2] = 0.0f; m[1] = 0.0f;
    m[15] = 1.0f; m[10] = 1.0f; m[5] = 1.0f; m[0] = 1.0f;
}

void IdentityOnesFirst(float *m) {
    m[15] = 1.0f; m[10] = 1.0f; m[5] = 1.0f; m[0] = 1.0f;
    m[14] = 0.0f; m[13] = 0.0f; m[12] = 0.0f; m[11] = 0.0f; m[9] = 0.0f; m[8] = 0.0f;
    m[7] = 0.0f; m[6] = 0.0f; m[4] = 0.0f; m[3] = 0.0f; m[2] = 0.0f; m[1] = 0.0f;
}

#if NIGHTFIRE_X87
// SetPerspective's frustum planes (0x000e478e..0x000e486b), the original instructions: the half field of view's
// tangent, the four guard-band-adjusted angles through FPATAN, then each plane's tangent and sine, all unrounded
// on the x87 stack except where the original stores a float. ESI is the ViewPort as in the original; the
// original's float temporaries (stored over its fov, near and far argument slots) are three locals here. The
// constants are ours (the original reads its .rdata copies through EAX, ECX and EDX).
__declspec(naked) void __stdcall PerspectivePlanes(ViewPort *view, float fov, float aspect) {
    __asm {
        push esi
        sub esp, 0xc
        // [esp] angle 0, [esp + 4] angle 1, [esp + 8] angle 2; view [esp + 0x14], fov [esp + 0x18],
        // aspect [esp + 0x1c]
        mov esi, dword ptr [esp + 0x14]
        fld dword ptr [kPi]
        fmul dword ptr [kOneOver180]
        fmul dword ptr [esp + 0x18]
        fmul dword ptr [kHalf]
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
        fld dword ptr [kMinusOne]
        fdiv dword ptr [esp + 0x1c]
        fmul dword ptr [esi + 0x184]
        fmulp st(1), st
        fld1
        fpatan
        fld dword ptr [esp]
        fptan
        fstp st(0)
        fstp dword ptr [esi + 0x158]
        fld dword ptr [kPi]
        fmul dword ptr [kHalf]
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
#else
// SetPerspective's frustum planes (0x000e478e..0x000e486b) in double: the half field of view's tangent, the four
// guard-band-adjusted angles (the first three stored as floats, the last kept), then each plane's tangent and the
// sine of its complement.
void __stdcall PerspectivePlanes(ViewPort *view, float fov, float aspect) {
    double halfTan = Libm::Tan(double(kPi) * kOneOver180 * fov * kHalf);
    float angle[3];
    angle[0] = float(Libm::Atan2(-(halfTan * view->guardBand[0]), 1.0));
    angle[1] = float(Libm::Atan2(halfTan * view->guardBand[1], 1.0));
    angle[2] = float(Libm::Atan2(double(view->guardBand[2]) / aspect * halfTan, 1.0));
    double angle3 = Libm::Atan2(halfTan * (double(kMinusOne) / aspect * view->guardBand[3]), 1.0);
    double quarterTurn = double(kPi) * kHalf;
    for (int i = 0; i < 3; i++) {
        view->planes[i * 2] = float(Libm::Tan(double(angle[i])));
        view->planes[i * 2 + 1] = float(Libm::Sin(quarterTurn - angle[i]));
    }
    view->planes[6] = float(Libm::Tan(angle3));
    view->planes[7] = float(Libm::Sin(quarterTurn - angle3));
}
#endif

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// ViewPort
// ---------------------------------------------------------------------------------------------------------------

// The rectangle (x, y, width, height, truncated), clamped to the context's size, becomes the D3D viewport. When
// the clamped rectangle is not the one asked for, the projection is scaled and offset so the picture is the
// asked-for rectangle's, cut, and the guard-band factors say how far each edge moved; otherwise those are 1 and 0.
// Ghidra: unnamed.
// FUNC_AT(0x000e4340)
void EAGL::ViewPort::SetShape(float x, float y, float width, float height, float minZ, float maxZ) {
    shape[0] = Ftol(x);
    shape[1] = Ftol(y);
    shape[2] = Ftol(width);
    shape[3] = Ftol(height);
    shapeMaxZ = maxZ;
    shapeMinZ = minZ;
    float contextWidth, contextHeight;
    if (renderContext != NULL)
        renderContext->GetSize(&contextWidth, &contextHeight);
    else
        textureRenderContext->GetSize(&contextWidth, &contextHeight);
    float left = x;
    float top = y;
    float right = x + width;
    double bottom = double(y) + height;                        // kept on the x87 stack
    if (x < 0.0f)
        left = 0.0f;
    if (y < 0.0f)
        top = 0.0f;
    if (right < 0.0f)
        right = 0.0f;
    if (bottom < 0.0)
        bottom = 0.0;
    if (left > contextWidth)
        left = contextWidth;
    if (top > contextHeight)
        top = contextHeight;
    if (right > contextWidth)
        right = contextWidth;
    if (bottom > contextHeight)
        bottom = contextHeight;
    viewport.x = Ftol(left);
    viewport.y = Ftol(top);
    double clampedWidth = double(right) - left;                // rounded to a float and truncated, both
    float widthF = float(clampedWidth);                        // over contextHeight's slot
    uint32_t w = Ftol(clampedWidth);
    viewport.width = w;
    double clampedHeight = bottom - top;
    float heightF = float(clampedHeight);                      // over contextWidth's slot
    uint32_t h = Ftol(clampedHeight);
    viewport.maxZ = maxZ;
    viewport.height = h;
    viewport.minZ = minZ;
    // FCOMP + TEST AH,0x44 + JP: equal and ordered falls through
    bool unchanged = widthF == width && heightF == height;
    if (!unchanged) {
        if (h * w != 0) {
            double scaleX = 1.0 / widthF;                                       // kept on the stack
            float scaleY = 1.0f / heightF;                                      // stored
            projectionScale[0] = float(width * scaleX);
            projectionScale[1] = scaleY * height;
            double t = double(width) * 0.5;
            float halfWidth = float(t);
            float centreX = float(t + x);
            double offsetX = double(centreX) - (double(widthF) * 0.5 + left);
            t = double(height) * 0.5;
            float halfHeight = float(t);
            float centreY = float(t + y);
            float offsetY = float(-(double(centreY) - (double(heightF) * 0.5 + top)));
            double o = scaleX * offsetX;
            o = o + o;
            projectionOffsetCopy[0] = float(o);
            projectionOffset[0] = float(o);
            o = double(offsetY) * scaleY;
            o = o + o;
            projectionOffsetCopy[1] = float(o);
            projectionOffset[1] = float(o);
            guardBand[0] = float((double(centreX) - left) / halfWidth);
            guardBand[1] = float((double(right) - centreX) / halfWidth);
            guardBand[2] = float((double(centreY) - top) / halfHeight);
            guardBand[3] = float((bottom - centreY) / halfHeight);
            return;
        }
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = 1;
        viewport.height = 1;
    }
    projectionOffset[1] = 0.0f;
    projectionOffset[0] = 0.0f;
    projectionOffsetCopy[1] = 0.0f;
    projectionOffsetCopy[0] = 0.0f;
    projectionScale[1] = 1.0f;
    projectionScale[0] = 1.0f;
    guardBand[0] = 1.0f;
    guardBand[1] = 1.0f;
    guardBand[2] = 1.0f;
    guardBand[3] = 1.0f;
}

// FUNC_AT(0x000e4680)
void EAGL::ViewPort::GetShape(float *x, float *y, float *width, float *height, float *minZ, float *maxZ) {
    *x = float(shape[0]);
    *y = float(shape[1]);
    *width = float(shape[2]);
    *height = float(shape[3]);
    *minZ = shapeMinZ;
    *maxZ = shapeMaxZ;
}

// A perspective projection: D3DXMatrixPerspectiveFovRH with the field of view in degrees and an aspect of 1, then
// SetShape's scale and offset, the aspect applied to the y scale, and the frustum planes IsSphereInView tests.
// FUNC_AT(0x000e46d0)
void EAGL::ViewPort::SetPerspective(float fov, float aspect, float nearZ, float farZ) {
    float fovY = float(double(kPi) * kOneOver180 * fov);
    IdentityZerosFirst(projection);
    D3DXMatrixPerspectiveFovRH(projection, fovY, 1.0f, nearZ, farZ);
    projection[0] = projectionScale[0] * projection[0];
    perspective[0] = fov;
    double y = double(projectionScale[1]) * projection[5];
    perspective[1] = aspect;
    perspective[2] = nearZ;
    perspective[3] = farZ;
    y = y * aspect;
    projectionType = 0;
    projection[5] = float(y);
    projection[8] = -projectionOffset[0];
    projection[9] = -projectionOffset[1];
    PerspectivePlanes(this, fov, aspect);
}

// An orthographic projection 2 wide and 2 * height high (D3DXMatrixOrthoRH). Ghidra: unnamed.
// FUNC_AT(0x000e4870)
void EAGL::ViewPort::SetOrthographicScreenSpace(float height, float nearZ, float farZ) {
    float h = height + height;
    IdentityOnesFirst(projection);
    D3DXMatrixOrthoRH(projection, 2.0f, h, nearZ, farZ);
    perspective[0] = 0.0f;
    perspective[1] = height;
    perspective[2] = nearZ;
    perspective[3] = farZ;
    projectionType = 1;
}

// An orthographic projection over the shape's pixels (0..width, 0..height). The context's size is asked for and
// not used.
// FUNC_AT(0x000e4900)
void EAGL::ViewPort::SetOrthographic(float nearZ, float farZ) {
    IdentityOnesFirst(projection);
    float contextWidth, contextHeight;
    if (renderContext != NULL)
        renderContext->GetSize(&contextWidth, &contextHeight);
    else
        textureRenderContext->GetSize(&contextWidth, &contextHeight);
    D3DXMatrixOrthoOffCenterRH(projection, 0.0f, float(shape[2]), float(shape[3]), 0.0f, nearZ, farZ);
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
    uint32_t clear = 0;
    if (flags & 1)
        clear = 0xf0;
    if (flags & 2)
        clear |= 1;
    if (flags & 4)
        clear |= 2;
    D3DDevice_Clear(0, NULL, clear, backgroundColour, 1.0f, 0);
}

// The sphere (centre in world space) against the near and far planes and, in perspective, the four side planes.
// Every comparison is the original's: x > r is "FCOMP r; TEST AH,0x41; JE" (false when unordered), a test against
// 0 "TEST AH,0x41; JNE" (x > 0) or "TEST AH,5; JP" (x < 0). The sums and products stay in double, unrounded, as on
// the x87 stack.
// FUNC_AT(0x000e4a10)
bool EAGL::ViewPort::IsSphereInView(const float *centre, float radius) {
    alignas(16) Transform viewMatrix;
    viewMatrix.BuildMatrix(ViewMatrix);
    float p[3];
    viewMatrix.TransformPoint(centre, p);
    double r = radius;
    if (double(p[2]) + perspective[2] > r)
        return false;
    if (-(double(p[2]) + perspective[3]) > r)
        return false;
    if (projectionType != 0)
        return true;
    double a = double(p[2]) * planes[2] + p[0];
    if (a > 0.0) {
        if (a * planes[3] > r)
            return false;
    } else {
        a = double(p[2]) * planes[0] + p[0];
        if (a < 0.0) {
            if (-(a * planes[1]) > r)
                return false;
        }
    }
    double b = double(p[2]) * planes[4] + p[1];
    if (b > 0.0)
        return !(b * planes[5] > r);
    b = double(p[2]) * planes[6] + p[1];
    if (b < 0.0)
        return !(-(b * planes[7]) > r);
    return true;
}

// Candidate SetGuardBandScale (RViewCamera::SetGuardBandSize calls it): empty in this build.
// FUNC_AT(0x000e4b50)
void EAGL::ViewPort::SetGuardBandScale(float) {
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
    ViewPort *view = Object();
    view->renderContext = NULL;
    view->textureRenderContext = NULL;
    view->backgroundColour = 0;
    view->active = 0;
    view->next = NULL;
    view->linked = owner;
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
        previous = reinterpret_cast<ViewPort *>(1);
    if (renderContext != NULL) {
        renderContext->Private()->SetCurrentViewPort(this);
        D3DDevice_SetRenderTarget(renderContext->backBuffer, renderContext->depthSurface);
    } else {
        textureRenderContext->Private()->SetCurrentViewPort(this);
        D3DPixelContainer *surface = D3DTexture_GetSurfaceLevel2(textureRenderContext->texture, 0);
        D3DDevice_SetRenderTarget(surface, textureRenderContext->depthSurface);
        D3DResource_Release(surface);
    }
    D3DDevice_SetViewport(&viewport);
    active = 1;
    alignas(16) Transform viewProjectionMatrix;
    viewProjectionMatrix.BuildMatrix(view);
    viewProjectionMatrix.AppendMatrix(projection);
    memcpy(viewProjection, viewProjectionMatrix.m, sizeof(viewProjection));
    memcpy(ViewMatrix, view, sizeof(view));
    memcpy(ProjectionMatrix, projection, sizeof(projection));
    memcpy(ViewProjectionMatrix, viewProjectionMatrix.m, sizeof(viewProjectionMatrix.m));
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
            D3DXVec3Project(out, points, &v->viewport, v->projection, v->view, world);
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
    EndViewOf(Object()->linked);
    Object()->linked->BeginView();   // read again, as the original
}

// FUNC_AT(0x000e4ef0)
void EAGL::ViewPort::SetViewMatrix(const float *matrix) {
    uint8_t wasActive = active;
    memcpy(view, matrix, sizeof(view));
    if (wasActive != 0) {
        EndViewOf(linked);
        linked->BeginView();
    }
}

// FUNC_AT(0x000f37d0)
EAGL::ViewPort* EAGL::ViewPort::Construct(RenderContext *context) {
    Extension()->Construct(this);
    Private()->Construct(this);
    previous = NULL;
    enableModelSphereCull = 0;
    memcpy(projection, kIdentity, sizeof(projection));
    memcpy(view, kIdentity, sizeof(view));
    memcpy(viewProjection, kIdentity, sizeof(viewProjection));
    textureRenderContext = NULL;
    renderContext = context;
    return this;
}

// ViewPort::ViewPort for a TextureRenderContext (TextureRenderContext::NewViewPort's). Ghidra: unnamed.
// FUNC_AT(0x000f3860)
EAGL::ViewPort* EAGL::ViewPort::ConstructForTexture(TextureRenderContext *context) {
    Extension()->Construct(this);
    Private()->Construct(this);
    previous = NULL;
    enableModelSphereCull = 0;
    memcpy(projection, kIdentity, sizeof(projection));
    memcpy(view, kIdentity, sizeof(view));
    memcpy(viewProjection, kIdentity, sizeof(viewProjection));
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
    *fov = perspective[0];
    *aspect = perspective[1];
    *nearZ = perspective[2];
    *farZ = perspective[3];
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
EAGL::ViewPort* EAGL::RenderContext::NewViewPort() {
    void *memory = EaglMalloc(sizeof(ViewPort), "EAGL::Viewport new");
    ViewPort *v = memory != NULL ? static_cast<ViewPort *>(memory)->Construct(this) : NULL;
    v->next = viewPorts;                            // the original stores through a failed allocation too
    viewPorts = v;
    return v;
}

// FUNC_AT(0x000ee080)
EAGL::ViewPort* EAGL::RenderContext::GetCurrentViewPort() {
    return currentViewPort;
}

// FUNC_AT(0x000ee090)
void EAGL::RenderContextPrivate::SetCurrentViewPort(ViewPort *viewPort) {
    owner->currentViewPort = viewPort;
}

// Unlinks, destroys and frees the viewport. One not in the list walks off the end and faults, as the original.
// FUNC_AT(0x000ee0a0)
void EAGL::RenderContext::DeleteViewPort(ViewPort *viewPort) {
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
    EaglFree(victim, sizeof(ViewPort));
    if (viewPort == currentViewPort)
        currentViewPort = NULL;
}

// The word at +0x1c (frontBufferDepth) added to the object's address. No callers.
// FUNC_AT(0x000ee160)
uint8_t* EAGL::RenderContext::OffsetSelf() {
    return reinterpret_cast<uint8_t *>(this) + uint32_t(frontBufferDepth);
}

// The RenderContext's words from +0x20 by index, and a pointer's shorts
uint32_t __cdecl RenderContextWordAt(const uint8_t *self, int index) {
    return *reinterpret_cast<const uint32_t *>(self + index * 4 + 0x20);
}

uint8_t __cdecl RenderContextSetFloatAt(uint8_t *self, int index, uint32_t valueBits) {
    *reinterpret_cast<uint32_t *>(self + index * 4 + 0x20) = valueBits;   // FLD/FSTP of a float: the same bits
    return 1;
}

float __cdecl RenderContextFloatAt(const uint8_t *self, int index) {
    return *reinterpret_cast<const float *>(self + index * 4 + 0x20);
}

int32_t __cdecl ShortAt(const uint8_t *p, int offset) {
    return *reinterpret_cast<const int16_t *>(p + offset);
}

// ---------------------------------------------------------------------------------------------------------------
// TextureRenderContext
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000f3450)
EAGL::TextureRenderContext* EAGL::TextureRenderContext::Construct(Device *owner) {
    Extension()->Construct(this);
    Private()->Construct(this);
    device = owner;
    return this;
}

// FUNC_AT(0x000f34b0)
EAGL::ViewPort* EAGL::TextureRenderContext::NewViewPort() {
    void *memory = EaglMalloc(sizeof(ViewPort), "EAGL::Viewport new");
    ViewPort *v = memory != NULL ? static_cast<ViewPort *>(memory)->ConstructForTexture(this) : NULL;
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
    EaglFree(victim, sizeof(ViewPort));
}

// FUNC_AT(0x000f35a0)
void EAGL::TextureRenderContext::Destruct() {
    while (viewPorts != NULL)
        DeleteViewPort(viewPorts);
    Extension()->Destruct();
}

// FUNC_AT(0x000f3600)
void EAGL::TextureRenderContext::BeginFrame() {
    Device::Get()->Private()->SetCurrentTextureRenderContext(this);
    inFrame = 1;
}

// FUNC_AT(0x000f3620)
void EAGL::TextureRenderContext::EndFrame() {
    Device::Get()->Private()->SetCurrentTextureRenderContext(NULL);
    inFrame = 0;
}

// The size as kept: integers (FILD).
// FUNC_AT(0x000f3640)
void EAGL::TextureRenderContext::GetSize(float *w, float *h) {
    *w = float(width);
    *h = float(height);
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
bool EAGL::TextureRenderContext::UnsupportedF3680(uint32_t) {
    return false;
}

// FUNC_AT(0x000f3690)
bool EAGL::TextureRenderContext::UnsupportedF3690(uint32_t) {
    return false;
}

// FUNC_AT(0x000f36a0)
bool EAGL::TextureRenderContext::UnsupportedF36A0(uint32_t) {
    return false;
}

// FUNC_AT(0x000f36b0)
bool EAGL::TextureRenderContext::UnsupportedF36B0(uint32_t) {
    return false;
}

// FUNC_AT(0x000f36c0)
bool EAGL::TextureRenderContext::UnsupportedF36C0(uint32_t) {
    return false;
}

// FUNC_AT(0x000f36d0)
bool EAGL::TextureRenderContext::UnsupportedF36D0(uint32_t) {
    return false;
}

// Takes the size, texture and format from the colour TAR's shared data and the depth texture and format from the
// depth TAR's, if there is one. The formats are the shared data's bits 6..13 taken signed (the original's
// SHL 0x12, SAR 0x18); the colour TAR's data pointer is read again for its format, as the original.
// FUNC_AT(0x000f36e0)
uint32_t EAGL::TextureRenderContext::SetupFrameBuffers(const TAR *colourTexture, const TAR *depthTexture) {
    const TARSharedData *colour = colourTexture->data;
    const TARSharedData *depth = NULL;
    if (depthTexture != NULL)
        depth = depthTexture->data;
    height = colour->height;
    width = colour->width;
    texture = colour->texture;
    colourFormat = int8_t(colourTexture->data->depth);
    depthSurface = NULL;
    if (depthTexture != NULL) {
        depthSurface = depth->texture;
        depthFormat = int8_t(depthTexture->data->depth);
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
    TextureRenderContext *object = Object();
    owner = context;
    object->unknown08[0] = 0;
    object->unknown08[1] = 0;
    object->width = 0;
    object->height = 0;
    object->unknown18 = 0;
    object->colourFormat = 0;
    object->depthFormat = 0;
    object->currentViewPort = NULL;
    object->viewPorts = NULL;
    object->unknown2c = 0;
    object->next = NULL;
    object->inFrame = 0;
    return this;
}

// ---------------------------------------------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------------------------------------------

// The allocator hooks (EaglGlobals.h), EAGL's own pair until RRenderer overrides them
// XBE_GLOBAL(0x001caf68, 4)
EaglMallocFn EaglMalloc = eagl_alloc;
// XBE_GLOBAL(0x001caf6c, 4)
EaglFreeFn EaglFree = eagl_free;

// FUNC_AT(0x000e4f50)
void *eagl_alloc(uint32_t size, const char *) {
    return Crt::malloc(size);
}

// FUNC_AT(0x000e4f60)
void eagl_free(void *pointer, uint32_t) {
    Crt::free(pointer);
}

// Registers the load-time types in the ConstructorPool, the property-built ones in the RuntimeAllocConstructorPool,
// the viewport matrices and the render-method constants by name (docs 2.1).
// FUNC_AT(0x000e4f70)
bool EAGL::Device::Init() {
    initialised = 1;
    CurrentDevice = this;
    TheConstructorPool.AddType("EAGL::TAR", reinterpret_cast<void *>(EAGL_TARConstructor),
                               reinterpret_cast<void *>(EAGL_TARDestructor));
    TheConstructorPool.AddType("RenderMethod", reinterpret_cast<void *>(EAGL_RenderMethodConstructor),
                               reinterpret_cast<void *>(EAGL_RenderMethodDestructor));
    TheConstructorPool.AddType("Model", reinterpret_cast<void *>(EAGLInternal::ModelConstructor),
                               reinterpret_cast<void *>(EAGLInternal::ModelDestructor));
    TheConstructorPool.AddType("VertexBuffer", reinterpret_cast<void *>(EAGL_VertexBufferConstructor),
                               reinterpret_cast<void *>(EAGL_VertexBufferDestructor));
    TheRuntimeAllocPool.AddType("EAGL::GeoPrimState", reinterpret_cast<void *>(RuntimeAllocGeoPrimStateConstructor),
                                reinterpret_cast<void *>(RuntimeAllocGeoPrimStateDestructor));
    TheRuntimeAllocPool.AddType("EAGL::TAR", reinterpret_cast<void *>(RuntimeAllocTARConstructor),
                                reinterpret_cast<void *>(RuntimeAllocTARDestructor));
    EAGL_SymbolInit();
    DynamicLoader::RegisterVar("&EAGL::RenderMethodConstants::gReflectionConstants", RenderMethodConstants[0]);
    DynamicLoader::RegisterVar("&EAGL::RenderMethodConstants::gSkinningConstants", RenderMethodConstants[1]);
    DynamicLoader::RegisterVar("&EAGL::RenderMethodConstants::gZeroOneTwoThree", RenderMethodConstants[2]);
    DynamicLoader::RegisterVar("&EAGL::RenderMethodConstants::gShadowColour", RenderMethodConstants[3]);
    DynamicLoader::RegisterVar("&EAGL::RenderMethodConstants::gEnvMapConstants", RenderMethodConstants[4]);
    uint32_t answer = D3D_ReturnsTrue(0);
    ReturnsTrueAnswer = answer;
    return answer != 0;
}

// FUNC_AT(0x000e5080)
EAGL::DevicePrivate* EAGL::DevicePrivate::Construct() {
    Device *object = Object();
    object->privatePart = 0;
    object->renderContexts = NULL;
    object->textureRenderContexts = NULL;
    object->initialised = 0;
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
    CurrentDevice = this;
    privatePart = 0;
    renderContexts = NULL;
    return this;
}

// FUNC_AT(0x000e50e0)
void EAGL::Device::Destruct() {
    while (renderContexts != NULL)
        DeleteRenderContext(renderContexts);
    while (textureRenderContexts != NULL)
        Extension()->DeleteTextureRenderContext(textureRenderContexts);
    DynamicLoader::UnRegisterVar("&EAGL::RenderMethodConstants::gReflectionConstants");
    DynamicLoader::UnRegisterVar("&EAGL::RenderMethodConstants::gSkinningConstants");
    DynamicLoader::UnRegisterVar("&EAGL::RenderMethodConstants::gZeroOneTwoThree");
    DynamicLoader::UnRegisterVar("&EAGL::RenderMethodConstants::gShadowColour");
    DynamicLoader::UnRegisterVar("&EAGL::RenderMethodConstants::gEnvMapConstants");
    TheConstructorPool.RemoveType("EAGL::TAR");
    TheConstructorPool.RemoveType("RenderMethod");
    TheConstructorPool.RemoveType("Model");
    TheConstructorPool.RemoveType("VertexBuffer");
    TheRuntimeAllocPool.RemoveType("EAGL::TAR");
    TheRuntimeAllocPool.RemoveType("EAGL::GeoPrimState");
}

// FUNC_AT(0x000e51b0)
void EAGL_EmptyE51B0() {
}

// FUNC_AT(0x000e51c0)
void EAGL_EmptyE51C0() {
}

// A new RenderContext, put at the head of the list and made current.
// FUNC_AT(0x000e8900)
EAGL::RenderContext* EAGL::Device::NewRenderContext() {
    void *memory = EaglMalloc(sizeof(RenderContext), "RenderContext new");
    RenderContext *context = memory != NULL ? static_cast<RenderContext *>(memory)->Construct(this) : NULL;
    context->next = renderContexts;
    renderContexts = context;
    CurrentRenderContext = context;
    return context;
}

// FUNC_AT(0x000e8970)
EAGL::TextureRenderContext* EAGL::DeviceExtension::NewTextureRenderContext() {
    void *memory = EaglMalloc(sizeof(TextureRenderContext), "TextureRenderContext new");
    TextureRenderContext *context = memory != NULL ? static_cast<TextureRenderContext *>(memory)->Construct(device)
                                                   : NULL;
    context->next = device->textureRenderContexts;
    device->textureRenderContexts = context;
    return context;
}

// FUNC_AT(0x000e89e0)
EAGL::RenderContext* EAGL::Device::GetCurrentRenderContext() {
    return CurrentRenderContext;
}

// FUNC_AT(0x000e89f0)
EAGL::TextureRenderContext* EAGL::Device::GetCurrentTextureRenderContext() {
    return CurrentTextureRenderContext;
}

// FUNC_AT(0x000e8a00)
void EAGL::DevicePrivate::SetCurrentRenderContext(RenderContext *context) {
    CurrentRenderContext = context;
}

// FUNC_AT(0x000e8a10)
void EAGL::DevicePrivate::SetCurrentTextureRenderContext(TextureRenderContext *context) {
    CurrentTextureRenderContext = context;
}

// FUNC_AT(0x000e8a20)
void EAGL::Device::SetNewOverride(void *allocator) {
    EaglMalloc = reinterpret_cast<EaglMallocFn>(allocator);
}

// FUNC_AT(0x000e8a30)
void EAGL::Device::SetDeleteOverride(void *deallocator) {
    EaglFree = reinterpret_cast<EaglFreeFn>(deallocator);
}

// FUNC_AT(0x000e8a40)
EAGL::Device* EAGL::Device::Get() {
    return CurrentDevice;
}

// Unlinks, destroys and frees the context; the current one becomes the list's head if it was this.
// FUNC_AT(0x000e8a50)
void EAGL::Device::DeleteRenderContext(RenderContext *context) {
    RenderContext *victim = renderContexts;
    if (context == victim) {
        renderContexts = victim->next;
    } else {
        RenderContext *p = victim;
        while (p != NULL && p->next != context)
            p = p->next;
        victim = p->next;
        p->next = victim->next;
    }
    victim->Destruct();
    EaglFree(victim, sizeof(RenderContext));
    if (CurrentRenderContext == context)
        CurrentRenderContext = renderContexts;
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
    EaglFree(victim, sizeof(TextureRenderContext));
}
