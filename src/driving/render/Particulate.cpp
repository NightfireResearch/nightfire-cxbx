#pragma fp_contract(off)

#include "Particulate.h"

#include "Renderer.h"                   // fgRenderer
#include "RSceneObj.hpp"
#include "../camera/Camera.h"           // RViewCamera, RCamera
#include "../data/DebugVariables.h"
#include "../eagl/D3D8State.h"
#include "../engine/SimRandom.h"        // Noise
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../../helpers.h"

#include <bit>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// RParticulate. See Particulate.h.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals

#define Sim ((void *)0x00233ff0)                    // the Simulation
#define SimState I32_AT(0x00234e24)
#define SimStepCount I32_AT(0x00234e34)
#define PlayerPhysicsObject (*(PhysicsObject **)PTR_AT(0x00234e40))
#define OffOnNames ((const char *const *)0x001b6638)    // "Off", "On"

const USingletonVtable *const kRParticulateVtable = (const USingletonVtable *)0x00192e54;
const USingletonVtable *const kUSingletonVtable = (const USingletonVtable *)0x0018beb0;
typedef RParticulate *(__fastcall *DeletingDestructor)(RParticulate *particulate, int, unsigned flags);

constexpr int32_t kSimPaused = 3;
constexpr int kPointList = 1;                       // Draw's primitive
constexpr int kBatch = 512;                         // points a draw
constexpr uint32_t kPointDirty = 0x100;             // D3D8's dirty flag for the point states
constexpr float kPointSize = 3.0f;
constexpr float kPointScaleA = 10.0f;
constexpr float kRandom16 = 1.0f / 65536.0f;        // a 16-bit draw times this: 0 to 1
constexpr uint32_t kMaxChannel = 255;

constexpr float kDefaultRed = 0.3f;
constexpr float kDefaultGreen = 0.4f;
constexpr float kDefaultBlue = 0.55f;
constexpr float kDefaultDriftAmplitude = 0.094f;
constexpr float kDefaultDriftFrequency = 0.012f;
constexpr float kDefaultDriftSpeed = 0.055f;
constexpr float kDefaultShadeAngle = 0.1f;
constexpr float kDefaultShadeStrength = 0.7f;
static_assert(std::bit_cast<uint32_t>(kDefaultRed) == 0x3e99999a && std::bit_cast<uint32_t>(kDefaultGreen) == 0x3ecccccd &&
              std::bit_cast<uint32_t>(kDefaultBlue) == 0x3f0ccccd &&
              std::bit_cast<uint32_t>(kDefaultDriftAmplitude) == 0x3dc08312 &&
              std::bit_cast<uint32_t>(kDefaultDriftFrequency) == 0x3c449ba6 &&
              std::bit_cast<uint32_t>(kDefaultDriftSpeed) == 0x3d6147ae &&
              std::bit_cast<uint32_t>(kDefaultShadeAngle) == 0x3dcccccd &&
              std::bit_cast<uint32_t>(kDefaultShadeStrength) == 0x3f333333, "the original's constants");

// A point anywhere within `range` of `centre` in each axis: three draws, the last for x and the first for z.
static void Scatter(Coord4 *position, float range, const Coord4 *centre) {
    double span = double(range) + range;
    uint32_t first = RandomShort();
    uint32_t second = RandomShort();
    uint32_t third = RandomShort();
    position->x = float(double(third) * span * kRandom16 - range + centre->x);
    position->y = float(double(second) * span * kRandom16 - range + centre->y);
    position->z = float(double(first) * span * kRandom16 - range + centre->z);
    position->w = 1.0f;
}

static uint32_t Argb(uint32_t alpha, uint32_t red, uint32_t green, uint32_t blue) {
    return alpha << 24 | red << 16 | green << 8 | blue;
}

static uint32_t ClampChannel(uint32_t value) {
    return value > kMaxChannel ? kMaxChannel : value;
}

// FUNC_AT(0x000a3f30)
RParticulate* RParticulate::Construct() {
    vtable = kRParticulateVtable;
    memset(colours, 0, sizeof(colours));
    material.Construct();
    _init();
    enabled = false;
    return this;
}

// FUNC_AT(0x000a3240)
void RParticulate::Destruct() {
    vtable = kRParticulateVtable;
    material.Destruct();
    vtable = kUSingletonVtable;
}

// FUNC_AT(0x000a3fa0)
RParticulate* RParticulate::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RParticulate));
    return this;
}

// FUNC_AT(0x000a3290)
void RParticulate::Kill() {
    RParticulate *particulate = fgParticulate;
    if (particulate != NULL)
        reinterpret_cast<DeletingDestructor>(particulate->vtable->slot0)(particulate, 0, 1);
}

// FUNC_AT(0x000a32b0)
void RParticulate::_init() {
    colour.x = kDefaultRed;
    colour.y = kDefaultGreen;
    colour.z = kDefaultBlue;
    colour.w = 0.0f;
    range = 70.0f;
    alpha = 25.0f;
    driftAmplitude = kDefaultDriftAmplitude;
    driftFrequency = kDefaultDriftFrequency;
    driftSpeed = kDefaultDriftSpeed;
    shadeAngle = kDefaultShadeAngle;
    shadeStrength = kDefaultShadeStrength;
    fullBatches = 0;
    lastBatchSize = kParticulateCount;
    const Coord4 origin = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < kParticulateCount; i++) {
        Scatter(&positions[i], range, &origin);
        colours[i] = Argb(Ftol(alpha), Ftol(double(colour.x) * 255.0f), Ftol(double(colour.y) * 255.0f),
                          Ftol(double(colour.z) * 255.0f));
    }
}

// FUNC_AT(0x000a3210)
void RParticulate::LoadAttributes() {
    dbattrib_bool("Enable Particulate", &enabled, 0, 1, 0, -1.0f, OffOnNames);
}

// FUNC_AT(0x000a3fd0)
void RParticulate::Update() {
    if (SimState == kSimPaused || !enabled)
        return;
    float alphaPerDistance = alpha / range;
    float inverseShadeAngle = 1.0f / shadeAngle;
    Coord4 eye = fgRenderer->cameraPosition;
    const float *viewAxis = fgRenderer->currentView->camera->matrix.mtx[2];
    const Coord3 *at = PlayerPhysicsObject->GetPosition();
    Coord4 player = { at->x, at->y, at->z, 1.0f };
    const float *carAxis = Simulation_GetRigidBody(Sim, 0, PlayerPhysicsObject->rigidBodySlot)->info->orientation.mtx[2];
    Coord4 heading = { carAxis[0], carAxis[1], carAxis[2], 1.0f };
    Coord4 centre;
    VU0_v4scaleadd4(viewAxis, range, &eye, &centre);

    // The drift: each axis's noise at a phase moved on by the step count and by its own last value.
    if (driftAmplitude > 0.0f) {
        drift.x = float(Noise::Noise1(float((double(SimStepCount) * driftSpeed + drift.x) * driftFrequency)) * driftAmplitude);
        drift.y = float(Noise::Noise1(float((double(SimStepCount) * driftSpeed + drift.y) * driftFrequency)) * driftAmplitude);
        drift.z = float(Noise::Noise1(float((double(SimStepCount) * driftSpeed + drift.z) * driftFrequency)) * driftAmplitude);
    }

    // A shaded point leaves its channels behind for the points after it.
    uint32_t red = ClampChannel(Ftol(double(colour.x) * 255.0f));
    uint32_t green = ClampChannel(Ftol(double(colour.y) * 255.0f));
    uint32_t blue = ClampChannel(Ftol(double(colour.z) * 255.0f));
    for (int i = 0; i < kParticulateCount; i++) {
        Coord4 *position = &positions[i];
        if (driftAmplitude > 0.0f)
            VU0_v3add(position, &drift, position);
        if (vec3distance(&centre, position) > range)
            Scatter(position, range, &centre);

        // Faded out by twice the range from the camera
        double fade = double(range) + range - vec3distance(&eye, position);
        if (fade < 0.0)
            fade = 0.0f;
        fade *= alphaPerDistance;
        float alphaValue = fade > 255.0f ? 255.0f : float(fade);
        uint32_t alphaByte = RoundToInt(alphaValue);
        colours[i] = Argb(alphaByte, red, green, blue);

        if (shadeAngle > 0.0f) {
            Coord4 away;
            VU0_v4sub4(position, &player, &away);
            VU0_v4unitxyz(&away, &away);
            double facing = 1.0f - double(v3dotprod(&heading, &away));
            if (facing < shadeAngle) {
                // Red from the unrounded shade, green and blue from it rounded to a float
                double shade = (1.0f - facing * inverseShadeAngle) * shadeStrength;
                float shadeRounded = float(shade);
                red = ClampChannel(RoundToInt(float((shade + colour.x) * 255.0f)));
                green = ClampChannel(RoundToInt(float((double(shadeRounded) + colour.y) * 255.0f)));
                blue = ClampChannel(RoundToInt(float((double(shadeRounded) + colour.z) * 255.0f)));
                colours[i] = Argb(alphaByte, red, green, blue);
            }
        }
    }
}

// FUNC_AT(0x000a3e60)
void RParticulate::Draw() {
    if (!enabled)
        return;
    D3DRenderState[kRsPointSize] = std::bit_cast<uint32_t>(kPointSize);
    D3DDirtyFlags |= kPointDirty;
    D3DRenderState[kRsPointScaleA] = std::bit_cast<uint32_t>(kPointScaleA);
    for (uint32_t batch = 0; batch <= fullBatches; batch++) {
        SimpleRequests[SimpleRequestIndex]->positions.SetData(&positions[batch * kBatch]);
        SimpleRequests[SimpleRequestIndex]->colours.SetData(&colours[batch * kBatch]);
        material.Draw(kPointList, batch == fullBatches ? lastBatchSize : kBatch, NULL);
    }
}
