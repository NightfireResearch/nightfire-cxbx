#include "ProcAnim.h"

#include <bit>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../../helpers.h"
#include "../platform/RealMath.h"
#include "../world/World.h"               // ProcAnimState, kWorldInstanceProcAnim
#include "AnimEngine.h"                   // RAnimEngine::EvaluateInstance
#include "../render/Renderer.h"

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// The proc-anim functions (ProcAnim.h). Each starts from the instance's own matrix, its rows' fourth words cleared,
// except the billboards, which build theirs, and ProcAnimXFormCopy.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's globals
#define ProcAnimFunctions ((ProcAnimFunction *)0x001c41c0)    // [256], by CARP::Instance::procAnimType
#define ProcAnimParams (*(float **)0x001c41b0)
#define ProcAnimParamCount U32_AT(0x001c41b4)
#define ProcAnimXForms (*(MATRIX4 **)0x001c41b8)
#define ProcAnimXFormCount U32_AT(0x001c41bc)
// The lists' defaults: a zero parameter and a zero transform the globals point at
#define DefaultProcAnimParam ((float *)0x00191938)
#define DefaultProcAnimXForm ((MATRIX4 *)0x00191940)
#define SimStep FLOAT_AT(0x001c465c)                           // "GAME::SimStep": the tick, as a float

namespace {

constexpr float kWiden = 1.0001f;
constexpr float kSpinRate = 1.0f / 256.0f;     // degrees a tick per unit of the parameter
constexpr float kNearVertical = 0.99f;
constexpr float kSwingRate = 1.0f / 65535.0f;  // turns of the swing's cycle a tick per unit, over a tick's length
constexpr float kHalf = 0.5f;
constexpr float kSwingTurns = 0.1f;
constexpr float kSwayAmplitude = 1.0f / 65536.0f;
constexpr float kSineRate = 0.16f;
constexpr float kCosineRate = 0.33f;
constexpr float kTiltX = 6.0f;
constexpr float kTiltZ = -60.0f;
constexpr float kSwayHeight = 85.0f;
constexpr float kScaleBand = 0.075f;
constexpr float kScaleSlope = 13.333333f;
constexpr float kScalePeak = 3.0f;
constexpr float kScaleY = 0.25f;
constexpr uint32_t kDimensionMask = 0x3ff;     // CARP::Instance::packedDimensions' x
static_assert(std::bit_cast<uint32_t>(kWiden) == 0x3f800347 && std::bit_cast<uint32_t>(kSpinRate) == 0x3b800000 &&
              std::bit_cast<uint32_t>(kNearVertical) == 0x3f7d70a4 &&
              std::bit_cast<uint32_t>(kSwingRate) == 0x37800080 && std::bit_cast<uint32_t>(kSwingTurns) == 0x3dcccccd &&
              std::bit_cast<uint32_t>(kSwayAmplitude) == 0x37800000 &&
              std::bit_cast<uint32_t>(kSineRate) == 0x3e23d70a && std::bit_cast<uint32_t>(kScaleBand) == 0x3d99999a &&
              std::bit_cast<uint32_t>(kScaleSlope) == 0x41555555, "the original's constants");

const Coord4 kAxisX = { 1.0f, 0.0f, 0.0f, 0.0f };
const Coord4 kAxisY = { 0.0f, 1.0f, 0.0f, 0.0f };
const Coord4 kAxisZ = { 0.0f, 0.0f, 1.0f, 0.0f };

void ProcAnimUntested(const char *what) {
    printf("[anim] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define PROCANIM_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            ProcAnimUntested(what); \
        } \
    } while (0)

// The instance's matrix, its rows' fourth words cleared
void CopyInstanceMatrix(const CARP::Instance *instance, MATRIX4 *out) {
    *out = instance->Matrix();
    out->mtx[0][3] = 0.0f;
    out->mtx[1][3] = 0.0f;
    out->mtx[2][3] = 0.0f;
    out->mtx[3][3] = 1.0f;
}

// The function's parameter: its state's, or the instance's procAnimIndex
uint16_t Parameter(const CARP::Instance *instance, ProcAnimState *state) {
    return (instance->flags & kWorldInstanceProcAnim) ? state->parameter : instance->procAnimIndex;
}

// The parameter list's entry, the first when out of range (the PS2 build's GetProcAnimParameter)
float GetProcAnimParameter(uint32_t index) {
    return index < ProcAnimParamCount ? ProcAnimParams[index] : ProcAnimParams[0];
}

// The transform list's entry `number` (from 1), the first when out of range
const MATRIX4 *GetProcAnimXForm(uint32_t number) {
    uint32_t index = number - 1;
    if (index >= ProcAnimXFormCount)
        index = 0;
    return &ProcAnimXForms[index];
}

// out = the rotation of `degrees` about the axis, then the instance's matrix
void RotateInstance(CARP::Instance *instance, float degrees, const Coord4 *axis, MATRIX4 *out) {
    CopyInstanceMatrix(instance, out);
    alignas(16) MATRIX4 rotation;
    BuildRotate(&rotation, degrees, axis->x, axis->y, axis->z);
    VU0_MATRIX4_mult(out, &rotation, out);
}

// Spun by the parameter, signed
bool Spin(CARP::Instance *instance, ProcAnimState *state, const Coord4 *axis, MATRIX4 *out) {
    int16_t rate = Parameter(instance, state);    // read signed
    RotateInstance(instance, (float)((double)rate * SimStep * kSpinRate), axis, out);
    return true;
}

// Turned by the parameter list's entry
bool ParamRotate(CARP::Instance *instance, ProcAnimState *state, const Coord4 *axis, MATRIX4 *out) {
    RotateInstance(instance, GetProcAnimParameter(Parameter(instance, state)), axis, out);
    return true;
}

} // namespace

// FUNC_AT(0x0008a4f0)
bool GetInstanceMatrix(CARP::Instance *instance, const Coord4 *offset, MATRIX4 *out, ProcAnimState *states) {
    return ProcAnimFunctions[instance->procAnimType](instance, &states[instance->procAnimIndex], offset, out);
}

// FUNC_AT(0x0008a520)
void SetProcAnimXFormList(MATRIX4 *xforms, uint32_t count) {
    if (xforms != NULL) {
        ProcAnimXForms = xforms;
        ProcAnimXFormCount = count;
    } else {
        ProcAnimXForms = DefaultProcAnimXForm;
        ProcAnimXFormCount = 1;
    }
}

// FUNC_AT(0x0008a550)
void SetProcAnimParamList(float *params, uint32_t count) {
    if (params != NULL) {
        ProcAnimParams = params;
        ProcAnimParamCount = count;
    } else {
        ProcAnimParams = DefaultProcAnimParam;
        ProcAnimParamCount = 1;
    }
}

// FUNC_AT(0x0008a580)
bool ProcAnimStatic(CARP::Instance *instance, ProcAnimState *, const Coord4 *, MATRIX4 *out) {
    CopyInstanceMatrix(instance, out);
    return true;
}

// FUNC_AT(0x0008a5b0)
bool ProcAnimWidened(CARP::Instance *instance, ProcAnimState *, const Coord4 *, MATRIX4 *out) {
    PROCANIM_UNTESTED("ProcAnimWidened");
    *out = instance->Matrix();
    out->mtx[0][0] *= kWiden;
    out->mtx[0][1] *= kWiden;
    out->mtx[0][2] *= kWiden;
    out->mtx[0][3] = 0.0f;
    out->mtx[1][3] = 0.0f;
    out->mtx[2][3] = 0.0f;
    out->mtx[3][3] = 1.0f;
    out->mtx[2][0] *= kWiden;
    out->mtx[2][1] *= kWiden;
    out->mtx[2][2] *= kWiden;
    return true;
}

// FUNC_AT(0x0008a620)
bool QuaternionStreamAnimation(CARP::Instance *instance, ProcAnimState *, const Coord4 *, MATRIX4 *out) {
    RAnimEngine::EvaluateInstance(instance, out);
    return true;
}

// FUNC_AT(0x0008a640)
bool ProcAnimSpinX(CARP::Instance *instance, ProcAnimState *state, const Coord4 *, MATRIX4 *out) {
    PROCANIM_UNTESTED("ProcAnimSpinX");
    return Spin(instance, state, &kAxisX, out);
}

// FUNC_AT(0x0008a6d0)
bool ProcAnimSpinY(CARP::Instance *instance, ProcAnimState *state, const Coord4 *, MATRIX4 *out) {
    return Spin(instance, state, &kAxisY, out);
}

// FUNC_AT(0x0008a760)
bool ProcAnimSpinZ(CARP::Instance *instance, ProcAnimState *state, const Coord4 *, MATRIX4 *out) {
    return Spin(instance, state, &kAxisZ, out);
}

// FUNC_AT(0x0008a7f0)
bool ProcAnimParamRotateX(CARP::Instance *instance, ProcAnimState *state, const Coord4 *, MATRIX4 *out) {
    PROCANIM_UNTESTED("ProcAnimParamRotateX");
    return ParamRotate(instance, state, &kAxisX, out);
}

// FUNC_AT(0x0008a890)
bool ProcAnimParamRotateY(CARP::Instance *instance, ProcAnimState *state, const Coord4 *, MATRIX4 *out) {
    PROCANIM_UNTESTED("ProcAnimParamRotateY");
    return ParamRotate(instance, state, &kAxisY, out);
}

// FUNC_AT(0x0008a930)
bool ProcAnimParamRotateZ(CARP::Instance *instance, ProcAnimState *state, const Coord4 *, MATRIX4 *out) {
    return ParamRotate(instance, state, &kAxisZ, out);
}

// FUNC_AT(0x0008a9d0)
bool ProcAnimXFormTransform(CARP::Instance *instance, ProcAnimState *, const Coord4 *, MATRIX4 *out) {
    CopyInstanceMatrix(instance, out);
    VU0_MATRIX4_mult(out, GetProcAnimXForm(instance->procAnimIndex), out);
    return true;
}

// FUNC_AT(0x0008aa20)
bool ProcAnimXFormCopy(CARP::Instance *instance, ProcAnimState *, const Coord4 *, MATRIX4 *out) {
    PROCANIM_UNTESTED("ProcAnimXFormCopy");
    *out = *GetProcAnimXForm(instance->procAnimIndex);
    return true;
}

// Row 2 from the camera to the instance's position plus `offset`, row 0 across it in the ground plane, row 1 up
// from both; straight up or down, rows 0 and 1 are x (or -x) and z.
// FUNC_AT(0x0008aa50)
bool ProcAnimBillboard(CARP::Instance *instance, ProcAnimState *, const Coord4 *offset, MATRIX4 *out) {
    PROCANIM_UNTESTED("ProcAnimBillboard");
    const Coord3 *position = reinterpret_cast<const Coord3 *>(instance->position);
    alignas(16) Coord4 placed = { 0.0f, 0.0f, 0.0f, 1.0f };
    VU0_v3add(position, offset, &placed);
    alignas(16) Coord4 forward = { 0.0f, 0.0f, 0.0f, 0.0f };
    alignas(16) Coord4 camera = fgRenderer->cameraPosition;
    VU0_v4sub(&placed, &camera, &forward);
    VU0_v4unitxyz(&forward, &forward);

    alignas(16) Coord4 right = { 0.0f, 0.0f, 0.0f, 0.0f };
    alignas(16) Coord4 up;
    if (forward.y > kNearVertical) {
        right = { 1.0f, 0.0f, 0.0f, 0.0f };
        up = { 0.0f, 0.0f, 1.0f, 0.0f };
    } else if (forward.y < -kNearVertical) {
        right = { -1.0f, 0.0f, 0.0f, 0.0f };
        up = { 0.0f, 0.0f, 1.0f, 0.0f };
    } else {
        up = { 0.0f, 1.0f, 0.0f, 0.0f };
        VU0_v4unitcrossprodxyz(&up, &forward, &right);
        VU0_v4unitcrossprodxyz(&forward, &right, &up);
    }
    *MatrixRow(out, 0) = right;
    *MatrixRow(out, 1) = up;
    *MatrixRow(out, 2) = forward;
    *MatrixRow(out, 3) = { 0.0f, 0.0f, 0.0f, 1.0f };
    out->mtx[3][0] = position->x;
    out->mtx[3][1] = position->y;
    out->mtx[3][2] = position->z;
    return true;
}

// Upright, row 2 from the camera in the ground plane, turned about it by a triangle wave of the parameter's rate
// over time: (2 phase - 0.5) * 0.1 turns, the phase rising and falling between 0 and 0.5.
// FUNC_AT(0x0008ad40)
bool ProcAnimSwingBillboard(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out) {
    uint16_t rate = Parameter(instance, state);
    double cycle = (double)rate * TickSeconds * SimStep * kSwingRate;
    double fraction = (double)(float)cycle - floor(cycle);
    float phase = (float)fraction;
    if (fraction > kHalf)
        phase = (float)(1.0 - phase);

    const Coord3 *position = reinterpret_cast<const Coord3 *>(instance->position);
    alignas(16) Coord4 placed;           // its w is never set; only x, y and z are used
    VU0_v3add(position, offset, &placed);
    alignas(16) Coord4 camera = fgRenderer->cameraPosition;
    alignas(16) Coord4 toward;
    VU0_v4sub(&placed, &camera, &toward);
    toward.y = 0.0f;
    toward.w = 0.0f;
    VU0_v4unitxyz(&toward, &toward);

    alignas(16) MATRIX4 frame;
    *MatrixRow(&frame, 1) = { 0.0f, 1.0f, 0.0f, 0.0f };
    *MatrixRow(&frame, 2) = toward;
    *MatrixRow(&frame, 3) = { 0.0f, 0.0f, 0.0f, 1.0f };
    frame.mtx[0][3] = 0.0f;
    VU0_v4unitcrossprodxyz(MatrixRow(&frame, 1), MatrixRow(&frame, 2), MatrixRow(&frame, 0));

    alignas(16) MATRIX4 swing;
    VU0_MATRIX4setzrot(&swing, (float)(((double)phase + phase - kHalf) * kSwingTurns));
    VU0_MATRIX4_mult(out, &swing, &frame);
    out->mtx[3][0] = position->x;
    out->mtx[3][1] = position->y;
    out->mtx[3][2] = position->z;
    return true;
}

// Two waves of the time over the instance's size in x, a sine at 0.16 turns and a cosine at 0.33, their amplitude
// the parameter in 65536ths: the cosine tilts row 1 along x, the sine along z, and their sum raises the position.
// The position is read after it is cleared (VU0_v4Init), so every instance of a size sways alike.
// FUNC_AT(0x0008af60)
bool ProcAnimSway(CARP::Instance *instance, ProcAnimState *state, const Coord4 *, MATRIX4 *out) {
    PROCANIM_UNTESTED("ProcAnimSway");
    CopyInstanceMatrix(instance, out);
    alignas(16) Coord4 position;
    Coord4 *translation = MatrixRow(out, 3);
    VU0_v4copy(translation, &position);
    VU0_v4Init(translation);

    uint16_t parameter = Parameter(instance, state);
    float amplitude = (float)((double)parameter * kSwayAmplitude);
    double inverseSize = 1.0 / (double)int32_t(instance->packedDimensions & kDimensionMask);
    double time = ((double)translation->z + translation->x + SimStep) * inverseSize;
    float sineCycle = (float)(kSineRate * time);
    float cosineCycle = (float)(time * kCosineRate);
    float sine = (float)(SineTurns((float)((double)sineCycle - floor((double)sineCycle))) * amplitude);
    float cosine = (float)(CosineTurns((float)((double)cosineCycle - floor((double)cosineCycle))) * amplitude);

    alignas(16) MATRIX4 tilt;
    VU0_MATRIX4Init(&tilt);
    alignas(16) Coord4 up = { cosine * kTiltX, 1.0f, sine * kTiltZ, 0.0f };
    VU0_v4unit(&up, MatrixRow(&tilt, 1));
    VU0_v4unitcrossprodxyz(MatrixRow(&tilt, 1), MatrixRow(&tilt, 2), MatrixRow(&tilt, 0));
    VU0_v4unitcrossprodxyz(MatrixRow(&tilt, 0), MatrixRow(&tilt, 1), MatrixRow(&tilt, 2));
    VU0_MATRIX4_mult(out, &tilt, out);

    position.y = (float)(((double)cosine + sine) * kSwayHeight + position.y);
    VU0_v4copy(&position, translation);
    return true;
}

// FUNC_AT(0x0008b110)
bool ProcAnimSpinY2(CARP::Instance *instance, ProcAnimState *state, const Coord4 *, MATRIX4 *out) {
    return Spin(instance, state, &kAxisY, out);
}

// FUNC_AT(0x0008b1a0)
bool ProcAnimByState(CARP::Instance *instance, ProcAnimState *state, const Coord4 *offset, MATRIX4 *out) {
    if (state->sceneObj != NULL)
        return false;
    return ProcAnimFunctions[state->procAnimType](instance, state, offset, out);
}

// Row 1 scaled by 0.25, or up to three times that while the instance's x, as (x + 1) / 2, is within 0.075 of the
// first parameter; then the first transform applied.
// FUNC_AT(0x0008b1c0)
bool ProcAnimParamScale(CARP::Instance *instance, ProcAnimState *, const Coord4 *, MATRIX4 *out) {
    PROCANIM_UNTESTED("ProcAnimParamScale");
    CopyInstanceMatrix(instance, out);
    float x = (float)(((double)out->mtx[3][0] + 1.0) * kHalf);
    double target = ProcAnimParams[0];
    double scale = 1.0;
    if (x > target - kScaleBand && x < target + kScaleBand) {
        double distance = x - target;
        if (distance < 0.0)
            distance = -distance;
        scale = kScalePeak - distance * kScaleSlope;
    }
    alignas(16) MATRIX4 scaling;
    BuildScale(&scaling, 1.0f, (float)(scale * kScaleY), 1.0f);
    VU0_MATRIX4_mult(out, &scaling, out);
    VU0_MATRIX4_mult(out, out, ProcAnimXForms);
    return true;
}
