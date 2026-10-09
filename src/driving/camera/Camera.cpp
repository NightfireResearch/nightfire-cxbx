#pragma fp_contract(off)

#include "Camera.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>

#include "../../helpers.h"
#include "../eagl/View.h"
#include "../engine/CoreContainers.h"   // GameVector
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"       // RigidVehicle
#include "../platform/RealMath.h"
#include "../render/Renderer.h"

// ---------------------------------------------------------------------------------------------------------------
// RCamera (0x00078430-0x00078550, 0x00096820, 0x00096980) and RViewCamera (0x00096830-0x000970a0, 0x0008bf20),
// ported from the listing. The x87 arithmetic is in double in the original's order, rounded where it stores a
// float. A view's shape is its extents scaled to the screen, its level-of-detail multiplier its field of view
// times its width.
// ---------------------------------------------------------------------------------------------------------------

using EAGL::ViewPort;

// ---- globals
#define RCameraVtable ((void **)0x00190350)
#define RViewCameraVtable ((void **)0x00192444)
#define ResolutionStamp I32_AT(0x001f2d78)              // changes with the screen's resolution (names ours)
#define ViewWidth I32_AT(0x001f2d7c)                    // the screen's size in pixels
#define ViewHeight I32_AT(0x001f2d80)
#define AspectScale FLOAT_AT(0x001c47a0)                // scales the aspect ratio (RRenderHigh::RearrangeSplitScreens's)
#define SimCars (*(GameVector<RigidVehicle *> *)0x00234e3c)   // the Simulation's cars, the player's first
#define SimStepCount I32_AT(0x00234e34)
// ApplyPerspectiveFunction's sway (names ours): on, and the period in steps and amplitude of each part
#define PerspectiveSway BOOL8_AT(0x001c47b8)            // 1
#define FovSwayPeriod I32_AT(0x001c47b4)                // 500
#define FovSwayAmplitude FLOAT_AT(0x001c47b0)           // 0.95 degrees
#define AspectSwayPeriod I32_AT(0x001c47ac)             // 720
#define AspectSwayAmplitude FLOAT_AT(0x001c47a8)        // 0.05

namespace {

constexpr float kDefaultFieldOfView = 33.0f;
constexpr float kDefaultNearZ = 0.17f;
constexpr float kDefaultFarZ = 3000.0f;
constexpr float kShapeMinZ = 0.01f;
constexpr float kShapeMaxZ = 1.0f;
constexpr float kLodFovScale = 1.0f / 33.0f;
constexpr float kLodScale = 0.0025f;
constexpr float kAspect4x3 = 1.33333325f;           // one unit below the nearest float to 4/3
constexpr float kAspect16x9 = 16.0f / 9.0f;
constexpr float kWidescreenFovScale = 1.25f;
constexpr float kTwoPi = 6.283185f;
constexpr float kDeviceNearZ = 0.1f;
constexpr float kDeviceFarZ = 10.0f;
constexpr uint32_t kClearAll = 1 | 2 | 4;           // ViewPort::ClearViewPort's three flags
static_assert(std::bit_cast<uint32_t>(kDefaultNearZ) == 0x3e2e147b && std::bit_cast<uint32_t>(kShapeMinZ) == 0x3c23d70a &&
              std::bit_cast<uint32_t>(kLodFovScale) == 0x3cf83e10 && std::bit_cast<uint32_t>(kLodScale) == 0x3b23d70a &&
              std::bit_cast<uint32_t>(kAspect4x3) == 0x3faaaaaa && std::bit_cast<uint32_t>(kAspect16x9) == 0x3fe38e39 &&
              std::bit_cast<uint32_t>(kTwoPi) == 0x40c90fda && std::bit_cast<uint32_t>(kDeviceNearZ) == 0x3dcccccd,
              "the original's constants");

// value + amplitude * FSIN(radians), on the x87 as the original keeps it (FSIN's result is not rounded)
__declspec(naked) float AddSine(double radians, float amplitude, float value) {
    __asm {
        fld qword ptr [esp + 4]
        fsin
        fmul dword ptr [esp + 12]
        fadd dword ptr [esp + 16]
        ret
    }
}

} // namespace

// ---- RCamera

// FUNC_AT(0x00078430)
RCamera* RCamera::Construct() {
    vtable = RCameraVtable;
    VU0_MATRIX4Init(&matrix);
    VU0_MATRIX4Init(&inverse);
    VU0_v4Init(&unknown50);
    matrixChanged = 0;
    active = 1;
    fieldOfView = kDefaultFieldOfView;
    return this;
}

// FUNC_AT(0x00096980)
RCamera* RCamera::ConstructCopy(const RCamera *other) {
    vtable = RCameraVtable;
    matrix = other->matrix;
    unknown50 = other->unknown50;
    matrixChanged = other->matrixChanged;
    inverse = other->inverse;
    active = other->active;
    fieldOfView = other->fieldOfView;
    return this;
}

// FUNC_AT(0x0008b330)
RCamera* RCamera::Assign(const RCamera *other) {
    matrix = other->matrix;
    unknown50 = other->unknown50;
    matrixChanged = other->matrixChanged;
    inverse = other->inverse;
    active = other->active;
    fieldOfView = other->fieldOfView;
    return this;
}

// FUNC_AT(0x00096820)
void RCamera::Destruct() {
    vtable = RCameraVtable;
}

// FUNC_AT(0x00078470)
RCamera* RCamera::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RCamera));
    return this;
}

// FUNC_AT(0x000784a0)
void RCamera::CreateMatrix4Inv() {
    if (!matrixChanged)
        return;
    matrixChanged = 0;
    VU0_MATRIX4_3x3transpose(&matrix, &inverse);
    Coord4 position;
    VU0_v4scale(MatrixRow(&matrix, 3), -1.0f, &position);
    VU0_MATRIX4_vect3rotate(&position, &inverse, MatrixRow(&inverse, 3));
}

// FUNC_AT(0x00078500)
void RCamera::ConvertToRHCS() {
    matrixChanged = 1;
    matrix.mtx[2][0] = -matrix.mtx[2][0];
    matrix.mtx[2][1] = -matrix.mtx[2][1];
    matrix.mtx[2][2] = -matrix.mtx[2][2];
}

// FUNC_AT(0x00078520)
void RCamera::SetMatrix4(const MATRIX4 *frame) {
    MatrixCopy(frame, &matrix);
    matrixChanged = 1;
}

// FUNC_AT(0x00078540)
void RCamera::SetActive(bool active) {
    this->active = active;
}

// ---- RViewCamera

// FUNC_AT(0x00096ae0)
RViewCamera* RViewCamera::Construct(RCamera *camera) {
    vtable = RViewCameraVtable;
    transformMode = kWorldTransform;
    resolutionStamp = ResolutionStamp;
    xMin = 0.0f;
    yMin = 0.0f;
    nearZ = kDefaultNearZ;
    xMax = 1.0f;
    yMax = 1.0f;
    viewId = -1;
    player = 0;
    farZ = kDefaultFarZ;
    fillColour = 0;
    active = 0;
    zBufferRangeA = 0xffffff;
    zBufferRangeB = 0;
    viewPort = fgRenderer->renderContext->NewViewPort();
    if (camera == NULL) {
        RCamera *made = static_cast<RCamera *>(UMemory::FastAlloc(sizeof(RCamera), "RCamera"));
        camera = made != NULL ? made->Construct() : NULL;
    }
    this->camera = camera;
    camera->fieldOfView = kDefaultFieldOfView;
    RefreshLODMultiplier();
    UpdateForResolution();
    return this;
}

// FUNC_AT(0x00096830)
void RViewCamera::Destruct() {
    vtable = RViewCameraVtable;
    fgRenderer->renderContext->DeleteViewPort(viewPort);
}

// FUNC_AT(0x0008bf20)
void RViewCamera::DestructThunk() {
    Destruct();
}

// FUNC_AT(0x00096c60)
RViewCamera* RViewCamera::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RViewCamera));
    return this;
}

// FUNC_AT(0x00096850)
void RViewCamera::SetGuardBandSize(float size) {
    guardBandSize = size;
    viewPort->SetGuardBandScale(size);
}

// FUNC_AT(0x00096860)
void RViewCamera::UpdateForResolution() {
    double width = ViewWidth;
    double height = ViewHeight;
    viewPort->SetShape((float)(width * xMin), (float)(height * yMin), (float)(((double)xMax - xMin) * width),
                       (float)(((double)yMax - yMin) * height), kShapeMinZ, kShapeMaxZ);
}

// FUNC_AT(0x000968b0)
void RViewCamera::RefreshLODMultiplier() {
    lodMultiplier = (float)((double)camera->fieldOfView * kLodFovScale * ((double)xMax - xMin) * kLodScale);
}

// FUNC_AT(0x000968e0)
void RViewCamera::EndView() {
    viewPort->EndView();
    active = 0;
    fgRenderer->EndView();
}

// The range is kept but not used; the shape is made again as UpdateForResolution makes it.
// FUNC_AT(0x00096900)
void RViewCamera::SetZBufferRange(uint32_t a, uint32_t b) {
    zBufferRangeA = a;
    zBufferRangeB = b;
    UpdateForResolution();
}

// FUNC_AT(0x00096960)
double RViewCamera::AspectRatio() {
    if (fgRenderer->widescreen)
        return (double)AspectScale * kAspect16x9;
    return (double)AspectScale * kAspect4x3;
}

// An orthographic view of the unit square: (0, 0) top left, (1, 1) bottom right.
// FUNC_AT(0x000969f0)
void RViewCamera::SetViewPortToUnitTransformMode(ViewPort *viewPort, float nearZ, float farZ) {
    viewPort->SetOrthographicScreenSpace(1.0f, nearZ, farZ);
    MATRIX4 view;
    MATRIX4 scale;
    BuildTranslate(&view, -1.0f, 1.0f, 0.0f);
    BuildScaleXYZ(&scale, 2.0f, -2.0f, -1.0f);
    VU0_MATRIX4_mult(&view, &scale, &view);
    viewPort->SetViewMatrix(view.mtx[0]);
}

// FUNC_AT(0x00096a70)
void RViewCamera::SetDeviceTransformMode() {
    if (active)
        viewPort->EndView();
    MATRIX4 view;
    BuildScaleXYZ(&view, 1.0f, 1.0f, -1.0f);
    viewPort->SetViewMatrix(view.mtx[0]);
    viewPort->SetOrthographic(kDeviceNearZ, kDeviceFarZ);
    if (active)
        viewPort->BeginView();
}

// FUNC_AT(0x00096ca0)
void RViewCamera::SetFillColour(uint32_t colour) {
    fillColour = colour;
    viewPort->SetBackgroundColour(colour);
}

// The level of detail takes the width as stored after the sum's rounding; the shape's height the sum unrounded.
// FUNC_AT(0x00096cc0)
void RViewCamera::SetExtents(float x, float y, float width, float height) {
    xMin = x;
    double right = (double)x + width;
    xMax = (float)right;
    yMin = y;
    double bottom = (double)y + height;
    yMax = (float)bottom;
    float viewWidth = (float)(right - x);
    lodMultiplier = (float)((double)camera->fieldOfView * kLodFovScale * viewWidth * kLodScale);
    float screenWidth = (float)ViewWidth;     // FILD, rounded to a float
    float screenHeight = (float)ViewHeight;
    viewPort->SetShape(screenWidth * x, screenHeight * y, viewWidth * screenWidth,
                       (float)((bottom - y) * screenHeight), kShapeMinZ, kShapeMaxZ);
}

// FUNC_AT(0x00096d70)
void RViewCamera::SetExtents(const RViewCamera *other) {
    xMin = other->xMin;
    yMin = other->yMin;
    nearZ = other->nearZ;
    xMax = other->xMax;
    yMax = other->yMax;
    farZ = other->farZ;
    RefreshLODMultiplier();
    UpdateForResolution();
}

// FUNC_AT(0x00096e20)
void ApplyPerspectiveFunction(float *aspect, float *fieldOfView) {
    if (!PerspectiveSway || SimCars.first[0]->GetPhysics()->unknownBC == 0)
        return;
    int step = SimStepCount % FovSwayPeriod;
    *fieldOfView = AddSine((double)step * kTwoPi / FovSwayPeriod, FovSwayAmplitude, *fieldOfView);
    step = SimStepCount % AspectSwayPeriod;
    *aspect = AddSine((double)step * kTwoPi / AspectSwayPeriod, AspectSwayAmplitude, *aspect);
}

// A perspective view through a copy of the camera turned right-handed.
// FUNC_AT(0x00096eb0)
void RViewCamera::SetWorldTransformMode() {
    if (active)
        viewPort->EndView();
    RCamera view;
    view.ConstructCopy(camera);
    view.ConvertToRHCS();
    view.CreateMatrix4Inv();
    viewPort->SetViewMatrix(view.inverse.mtx[0]);
    float fieldOfView = (float)((double)camera->fieldOfView * 2 * fgRenderer->fieldOfViewScale);
    float aspect;
    if (fgRenderer->widescreen) {
        fieldOfView = fieldOfView * kWidescreenFovScale;
        aspect = AspectScale * kAspect16x9;
    } else {
        aspect = AspectScale * kAspect4x3;
    }
    ApplyPerspectiveFunction(&aspect, &fieldOfView);
    viewPort->SetPerspective(fieldOfView, aspect, nearZ, farZ);
    if (active)
        viewPort->BeginView();
}

// FUNC_AT(0x00096fc0)
void RViewCamera::SetRenderCamera() {
    fgRenderer->SetCurrentView(this);
    int stamp = ResolutionStamp;
    if (resolutionStamp != stamp) {
        resolutionStamp = stamp;
        UpdateForResolution();
    }
    if (transformMode == kDeviceTransform)
        SetDeviceTransformMode();
    else if (transformMode == kTransformMode1)
        SetDeviceTransformMode();
    else
        SetWorldTransformMode();
    active = 1;
    fgRenderer->cameraPosition = *MatrixRow(&camera->matrix, 3);
    fgRenderer->cameraPosition.w = 1.0f;
    viewPort->BeginView();
    if (fillColour != 0)
        viewPort->ClearViewPort(kClearAll);
}

// FUNC_AT(0x00097060)
void RViewCamera::Render() {
    PreRenderVirtual();
    SetRenderCamera();
    DoRenderVirtual();
    EndView();
    PostRenderVirtual();
}
