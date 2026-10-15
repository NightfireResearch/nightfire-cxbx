#include "AISplinePath.h"

#include <string.h>

#include "Simulation.h"                 // fgSimulation
#include "../../helpers.h"
#include "../render/PathEngine.h"       // RPathEngine, RPathHandle

// ---------------------------------------------------------------------------------------------------------------
// AISplinePath (0x00035680-0x000357b0; the CARP::Instance matrix setter before it, 0x00035610, is Carp.cpp's),
// ported from the listing.
// ---------------------------------------------------------------------------------------------------------------


namespace {

const MATRIX4 kIdentity = { {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
} };

constexpr uint8_t kSplinePathProcAnimType = 0xfa;
constexpr float kSplinePathSpeed = 0.5f;

} // namespace


// FUNC_AT(0x00035680)
AISplinePath* AISplinePath::Construct(CARP::AISpline *spline) {
    instance.SetMatrix(&kIdentity);
    procAnim.damage = 0;
    procAnim.flags = kPathAnimRunning;
    procAnim.unknown06 = 0;
    instance.flags = kWorldInstanceProcAnim;
    instance.procAnimIndex = 0;
    instance.procAnimType = 0xff;
    instance.articleDesc.value = 0;
    instance.unknown2c = 0;
    instance.packedDimensions = 0;
    procAnim.type = kPathAnimType;
    procAnim.procAnimType = kSplinePathProcAnimType;
    procAnim.parameter = 0;
    procAnim.article = 0;
    procAnim.sceneObj = NULL;
    procAnim.path = PtrFrom32<CARP::PathInfo>(spline->path.value);
    procAnim.master = NULL;
    procAnim.startTime = 0.0f;
    procAnim.speed = kSplinePathSpeed;
    procAnim.untransformed = 0;
    procAnim.unknown1D[0] = 0;
    procAnim.unknown1D[1] = 0;
    procAnim.unknown1D[2] = 0;
    path = RPathEngine::CreatePathHandle(&instance, &procAnim, float(fgSimulation.stepCount));
    path->userData = spline->unknown44;
    spline->GetApplyTransform(&placement.mtx[0][0]);
    return this;
}

// FUNC_AT(0x00035730)
void AISplinePath::Destruct() {
    RPathEngine::DestroyPathHandle(path);
}

// FUNC_AT(0x00035740)
void AISplinePath::Reset(CARP::AISpline *spline) {
    procAnim.path = PtrFrom32<CARP::PathInfo>(spline->path.value);
    path->Init(float(fgSimulation.stepCount));
    path->userData = spline->unknown44;
    spline->GetApplyTransform(&placement.mtx[0][0]);
}

// FUNC_AT(0x00035780)
void AISplinePath::SetNext(CARP::AISpline *spline) {
    path->SetNextPath(PtrFrom32<CARP::PathInfo>(spline->path.value));
}
