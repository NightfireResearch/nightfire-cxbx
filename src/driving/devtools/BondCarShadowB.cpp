#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "BondCarShadowB.h"
#include "FpControl.h"

#include "../camera/Camera.h"           // AISplinePath
#include "../camera/CameraIniLoader.h"  // fgCameraTables
#include "../camera/PlayerCamera.h"     // RPlayerCamera
#include "../game/BondCar.h"
#include "../game/BondCarBasics.h"
#include "../game/BondCarModes.h"
#include "../physics/RigidBody.h"
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../render/PathEngine.h"       // RPathHandle
#include "../render/RenderHigh.h"       // fgRenderHigh
#include "../world/CollisionManager.h"
#include "../world/CollisionTypes.h"    // MatrixRow
#include "../world/WorldPos.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bit>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_BONDCARSHADOWB=1, from the first simulation tick: package V_B's PBondCar methods against the originals
// (each original's entry bytes swapped back in for its run, common/xbeOriginal.h), on every live PBondCar in place:
// the car, its rigid body and body info, its CarPhysics record, the first bytes of its audio object, the camera's
// mode and the collision manager's barrier mask are taken before each case and put back after each run and at
// the end.
//
//   - ProcessSubmarinePhysics with the controls, class, reverse timer, rocket boost (around its end, with an audio
//     object), roll and bob state, the body's velocities and orientation, its hit points and the interior camera
//     view perturbed, and the body moved into each soft zone.
//   - ProcessSplinePhysics on the cars whose AI has a spline path, with the wheel spin angles, wheel radius,
//     suspension tuning and the snowmobile/sub flags perturbed.
//   - DebugObject with the physics record's ratios and friction limits perturbed.
//   - GetCarColourVariation for every car name and a spread of colours.
//   - Every accessor with the car's fields from +0x74 on filled at random (and the suspension zero, -0 or NaN),
//     results and the car compared.
// The destructor is not run here (it frees the car's objects and its body's slot): in game, as cars are removed.
//
// NIGHTFIRE_BONDCARSHADOWB=3 skips all that and compares every live call of the submarine and spline physics
// instead (BondCarShadowB_Tick, below), printing the first differing calls with their inputs.
//
// One mutation this catches: the bob offset added back to the height rounded first (bob for bobOffset in
// ProcessSubmarinePhysics) changes the body's position.y in its last bit on most submarine cases.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kBondCarVtable = 0x0018f580;
const int kOwners = 0x40;
const size_t kAudioBytes = 0x160;
const int kSoftZones = 19;
const uint32_t kSoftZoneTable = 0x001c37b0;

typedef void (__fastcall *CarFn)(PBondCar *, int);
typedef void *(__fastcall *PtrGetFn)(PBondCar *, int);
typedef int (__fastcall *IntGetFn)(PBondCar *, int);
typedef uint8_t (__fastcall *ByteGetFn)(PBondCar *, int);
typedef float (__fastcall *FloatGetFn)(PBondCar *, int);
typedef void *(__fastcall *PtrIndexFn)(PBondCar *, int, int);
typedef int (__fastcall *IntIndexFn)(PBondCar *, int, int);
typedef uint8_t (__fastcall *ByteIndexFn)(PBondCar *, int, int);
typedef float (__fastcall *FloatIndexFn)(PBondCar *, int, int);
typedef void (__fastcall *SetFn)(PBondCar *, int, uint32_t);
typedef void (__fastcall *SetFloatFn)(PBondCar *, int, float);
typedef uint32_t (*ColourFn)(const char *, uint32_t);
typedef AISplinePath *(__fastcall *SplinePathFn)(AIGroundVehicle *, int);

#define ShadowSim ((void *)0x00233ff0)
#define Shadow_GetSplinePath ((SplinePathFn)0x000359d0)

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10)
        printf("[bondcarB]   %s #%d: %s\n", what, index, detail);
}

void CheckBytes(const char *what, int index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    char detail[96];
    snprintf(detail, sizeof(detail), "byte 0x%x of 0x%x: original %02x, port %02x", unsigned(at), unsigned(bytes),
             x[at], y[at]);
    Differ(what, index, detail);
}

// Cases and differing cases by the method tested
struct Kind {
    const char *what;
    int cases, differ;
};
std::vector<Kind> g_kinds;

void Tally(const char *what, bool differed) {
    for (Kind &kind : g_kinds) {
        if (strcmp(kind.what, what) == 0) {
            kind.cases++;
            kind.differ += differed;
            return;
        }
    }
    g_kinds.push_back({ what, 1, int(differed) });
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context, bool original);

bool Guarded(CaseFn run, void *context, bool original) {
#ifdef _MSC_VER
    __try {
        run(context, original);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        return false;
    }
#else
    run(context, original);
    return true;
#endif
}

// ---- the state a call runs on

struct CarState {
    alignas(16) uint8_t car[sizeof(PBondCar)];
    alignas(16) RigidBody body;
    alignas(16) RigidBodyInfo info;
    CarPhysics physics;
    uint8_t audio[kAudioBytes];
    uint32_t barrierMask;
    int32_t cameraMode;
};

RPlayerCamera *Camera() {
    return fgRenderHigh != NULL ? fgRenderHigh->views[0].camera : NULL;
}

RigidBody *BodyOf(PBondCar *car) {
    return Simulation_GetRigidBody(ShadowSim, 0, car->rigidBodySlot);
}

// The objects the state covers, taken from the car once before its cases (the cases overwrite its pointers)
struct LiveCar {
    PBondCar *car;
    RigidBody *body;
    RigidBodyInfo *info;
    CarPhysics *physics;
    void *audio;
};
LiveCar g_live;

void Bind(PBondCar *car) {
    g_live.car = car;
    g_live.body = BodyOf(car);
    g_live.info = g_live.body->info;
    g_live.physics = car->physics;
    g_live.audio = car->audio;
}

void TakeState(CarState *state) {
    memcpy(state->car, g_live.car, sizeof(PBondCar));
    state->body = *g_live.body;
    state->info = *g_live.info;
    state->physics = *g_live.physics;
    if (g_live.audio != NULL)
        memcpy(state->audio, g_live.audio, kAudioBytes);
    state->barrierMask = fgCollisionMgr->barrierMask;
    state->cameraMode = Camera() != NULL ? Camera()->cameraMode : 0;
}

void PutState(const CarState *state) {
    memcpy(g_live.car, state->car, sizeof(PBondCar));
    *g_live.body = state->body;
    *g_live.info = state->info;
    *g_live.physics = state->physics;
    if (g_live.audio != NULL)
        memcpy(g_live.audio, state->audio, kAudioBytes);
    fgCollisionMgr->barrierMask = state->barrierMask;
    if (Camera() != NULL)
        Camera()->cameraMode = state->cameraMode;
}

CarState g_saved, g_start, g_result[2];

// The original (its entry bytes swapped in), then the port, each from the car's current state; the car is left
// as it was
template <class F>
void Both(uint32_t at, F &&call) {
    typedef typename std::remove_reference<F>::type Call;
    CaseFn thunk = [](void *context, bool original) { (*static_cast<Call *>(context))(original); };
    TakeState(&g_start);
    for (int run = 0; run < 2; run++) {
        const bool original = run == 0;
        PutState(&g_start);
        if (original)
            XbeOriginal_Restore(at, true);
        Guarded(thunk, &call, original);
        if (original)
            XbeOriginal_Restore(at, false);
        TakeState(&g_result[run]);
    }
    PutState(&g_start);
}

// Whether any check differed
bool CompareRuns(const char *what, int index) {
    g_cases++;
    const int differBefore = g_differ;
    char name[64];
    snprintf(name, sizeof(name), "%s car", what);
    CheckBytes(name, index, g_result[0].car, g_result[1].car, sizeof(PBondCar));
    snprintf(name, sizeof(name), "%s body", what);
    CheckBytes(name, index, &g_result[0].body, &g_result[1].body, sizeof(RigidBody));
    snprintf(name, sizeof(name), "%s info", what);
    CheckBytes(name, index, &g_result[0].info, &g_result[1].info, sizeof(RigidBodyInfo));
    snprintf(name, sizeof(name), "%s physics", what);
    CheckBytes(name, index, &g_result[0].physics, &g_result[1].physics, sizeof(CarPhysics));
    snprintf(name, sizeof(name), "%s audio", what);
    CheckBytes(name, index, g_result[0].audio, g_result[1].audio, kAudioBytes);
    snprintf(name, sizeof(name), "%s barrier mask", what);
    CheckBytes(name, index, &g_result[0].barrierMask, &g_result[1].barrierMask, sizeof(uint32_t));
    return g_differ != differBefore;
}

// ---- inputs

uint32_t g_random = 0x6b1d39a5;

uint32_t Random() {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return g_random;
}

int RandomInt(int n) { return n <= 0 ? 0 : int(Random() % uint32_t(n)); }
float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Random() >> 8) * (1.0f / 16777216.0f); }

void RandomVector(Coord3 *v, float size) {
    *v = { Uniform(-size, size), Uniform(-size, size), Uniform(-size, size) };
}

void RandomRotation(MATRIX4 *m, bool nearUpright) {
    for (;;) {
        Coord4 q = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f) };
        if (nearUpright) {
            q.x *= 0.15f;
            q.z *= 0.15f;
        }
        float length = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (length > 0.1f) {
            q = { q.x / length, q.y / length, q.z / length, q.w / length };
            VU0_quattom4(m, &q);
            return;
        }
    }
}

std::vector<PBondCar *> g_cars;

// A camera mode whose view is from inside the car; -1 none
int InteriorMode() {
    if (Camera() == NULL || fgCameraTables.modes == NULL)
        return -1;
    for (int i = 0; i < fgCameraTables.modeCount; i++)
        if (fgCameraTables.modes[i].interiorView)
            return i;
    return -1;
}

// ---- ProcessSubmarinePhysics

void TestSubmarine() {
    static const float kSpecial[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f };
    const int interiorMode = InteriorMode();
    const Coord4 *zones = reinterpret_cast<const Coord4 *>(kSoftZoneTable);   // 5 vectors each
    int index = 0;
    for (PBondCar *car : g_cars) {
        Bind(car);
        RigidBody *body = g_live.body;
        const int boostEnd = car->attributes.LookupInt("BOOST_TIME", NULL) * 3;
        const bool ownHitPoints = car->hitPointLoc == &car->hitPoints;
        for (int repeat = 0; repeat < 48; repeat++, index++) {
            TakeState(&g_saved);
            if (repeat > 0) {
                BondCarControl &control = car->control;
                control.steering = RandomInt(6) ? Uniform(-1.0f, 1.0f) : kSpecial[RandomInt(5)];
                control.steeringVertical = RandomInt(6) ? Uniform(-1.0f, 1.0f) : kSpecial[RandomInt(5)];
                control.gas = RandomInt(3) ? Uniform(0.0f, 1.0f) : 0.0f;
                control.brake = RandomInt(2) ? Uniform(0.0f, 1.0f) : 0.0f;
                control.handBrake = uint8_t(RandomInt(3) == 0);
                static const int kClasses[] = { 0, 0, 1, 2, 3 };
                car->carClass = kClasses[RandomInt(5)];
                static const int8_t kTimers[] = { 0, 14, 15, 30, -56 };
                car->reverseTimer = kTimers[RandomInt(5)];
                car->rocketBoost = 0;
                if (RandomInt(3) == 0)
                    car->rocketBoost = car->audio != NULL ? boostEnd + RandomInt(3) : boostEnd + 2 + RandomInt(5);
                car->againstWall = uint8_t(RandomInt(4) == 0);
                static const int8_t kRolls[] = { 0, 0, 10, -10, 45, -45, 90, -90, 127, -128 };
                car->rollSub = kRolls[RandomInt(10)];
                static const int8_t kDirections[] = { 0, 0, 1, -1 };
                car->forceRollDirection = kDirections[RandomInt(4)];
                car->subRolling = uint8_t(RandomInt(2));
                car->unknown408 = uint8_t(RandomInt(2));
                car->bobAngle = RandomInt(3) ? Uniform(0.0f, 1.0f) : 0.999f;
                car->unknown3E8 = RandomInt(3) ? Uniform(0.0f, 1.0f) : 0.998f;
                car->bob = Uniform(-0.2f, 0.2f);
                RandomVector(&body->velocity, RandomInt(2) ? 3.0f : 40.0f);
                RandomVector(&body->angularVelocity, 2.0f);
                RandomVector(&body->angularMomentum, 2000.0f);
                if (RandomInt(2))
                    RandomRotation(&body->info->orientation, RandomInt(2) != 0);
                if (ownHitPoints && RandomInt(4) == 0)
                    car->hitPoints = RandomInt(2) ? 0.0f : -5.0f;
                if (interiorMode >= 0 && RandomInt(3) == 0)
                    Camera()->cameraMode = interiorMode;
                if (RandomInt(3) == 0) {
                    // into a soft zone: its corners' centre, some way along its normal
                    const Coord4 *zone = zones + 5 * RandomInt(kSoftZones);
                    float depth = zone[4].w * Uniform(-0.1f, 1.1f);
                    body->position.x = (zone[0].x + zone[1].x + zone[2].x + zone[3].x) * 0.25f + zone[4].x * depth;
                    body->position.y = (zone[0].y + zone[1].y + zone[2].y + zone[3].y) * 0.25f + zone[4].y * depth;
                    body->position.z = (zone[0].z + zone[1].z + zone[2].z + zone[3].z) * 0.25f + zone[4].z * depth;
                    RandomVector(&body->force, RandomInt(2) ? 100.0f : 5000.0f);
                }
            }
            Both(0x000644c0, [&](bool original) {
                if (original)
                    ((CarFn)0x000644c0)(car, 0);
                else
                    car->ProcessSubmarinePhysics();
            });
            Tally("ProcessSubmarinePhysics", CompareRuns("ProcessSubmarinePhysics", index));
            PutState(&g_saved);
        }
    }
}

// ---- ProcessSplinePhysics

int g_splineCars = 0;

bool HasSplinePath(PBondCar *car) {
    if (car->aiGroundVehicle == NULL || car->physics->subPhysics != 0)
        return false;
    AISplinePath *spline = Shadow_GetSplinePath(car->aiGroundVehicle, 0);
    return spline != NULL && spline->path != NULL;
}

void TestSpline() {
    int index = 0;
    for (PBondCar *car : g_cars) {
        if (!HasSplinePath(car))
            continue;
        Bind(car);
        g_splineCars++;
        for (int repeat = 0; repeat < 32; repeat++, index++) {
            TakeState(&g_saved);
            if (repeat > 0) {
                car->wheelSpinAngle[0] = Uniform(-1.5f, 1.5f);
                car->wheelSpinAngle[1] = RandomInt(3) ? Uniform(-1.5f, 1.5f) : 0.9999f;
                CarPhysics *physics = car->physics;
                if (RandomInt(3) == 0)
                    physics->wheelRadius = RandomInt(2) ? 0.0f : Uniform(0.001f, 0.05f);
                if (RandomInt(3) == 0)
                    physics->springRestLength = Uniform(-1.0f, 1.0f);
                if (RandomInt(3) == 0)
                    physics->springCompressionLimit = Uniform(-0.2f, 0.6f);
                if (RandomInt(6) == 0)
                    physics->isSnowmobile = 1;
                for (int i = 0; i < kCarWheels; i++)
                    car->wheelSlip[i] = Uniform(0.0f, 1.0f);
            }
            Both(0x00065740, [&](bool original) {
                if (original)
                    ((CarFn)0x00065740)(car, 0);
                else
                    car->ProcessSplinePhysics();
            });
            Tally("ProcessSplinePhysics", CompareRuns("ProcessSplinePhysics", index));
            PutState(&g_saved);
        }
    }
}

// ---- DebugObject

void TestDebugObject() {
    static const float kSpecial[] = { 0.0f, -0.0f, 1.0f, 0.5f };
    int index = 0;
    for (PBondCar *car : g_cars) {
        Bind(car);
        for (int repeat = 0; repeat < 16; repeat++, index++) {
            TakeState(&g_saved);
            if (repeat > 0) {
                CarPhysics *physics = car->physics;
                physics->accelFrwRatio = RandomInt(4) ? Uniform(0.0f, 1.0f) : kSpecial[RandomInt(4)];
                physics->coastFrwRatio = RandomInt(4) ? Uniform(0.0f, 1.0f) : kSpecial[RandomInt(4)];
                physics->brakeFrwRatio = RandomInt(4) ? Uniform(0.0f, 1.0f) : kSpecial[RandomInt(4)];
                physics->frictionLimitFront = Uniform(0.0f, 50.0f);
                physics->frictionLimitRear = Uniform(0.0f, 50.0f);
            }
            Both(0x00065ba0, [&](bool original) {
                if (original)
                    ((CarFn)0x00065ba0)(car, 0);
                else
                    car->DebugObject();
            });
            Tally("DebugObject", CompareRuns("DebugObject", index));
            PutState(&g_saved);
        }
    }
}

// ---- GetCarColourVariation

void TestColourVariation() {
    static const uint32_t kColours[] = { 0, 1, 2, 3, 4, 5, 7, 11, 255, 0x7fffffff, 0xffffffff };
    const uint32_t count = PVehicle::GetNameCount();
    const char *const *names = PVehicle::GetCarNames();
    int index = 0;
    for (uint32_t n = 0; n < count; n++) {
        for (uint32_t colour : kColours) {
            uint32_t result[2] = { 0xdeadbeef, 0xdeadbeef };
            for (int run = 0; run < 2; run++) {
                if (run == 0) {
                    XbeOriginal_Restore(0x00065bf0, true);
                    result[0] = ((ColourFn)0x00065bf0)(names[n], colour);
                    XbeOriginal_Restore(0x00065bf0, false);
                } else {
                    result[1] = GetCarColourVariation(names[n], colour);
                }
            }
            g_cases++;
            const int differBefore = g_differ;
            CheckBytes("GetCarColourVariation", index++, &result[0], &result[1], sizeof(uint32_t));
            Tally("GetCarColourVariation", g_differ != differBefore);
        }
    }
}

// ---- the accessors

// The car's own fields filled at random (nothing an accessor reads is dereferenced)
void ScrambleCar(PBondCar *car) {
    uint8_t *bytes = reinterpret_cast<uint8_t *>(car);
    for (size_t at = offsetof(PBondCar, tyreTracks); at < sizeof(PBondCar); at++)
        bytes[at] = uint8_t(Random());
    static const uint32_t kSuspension[] = { 0x00000000, 0x80000000, 0x7fc00000, 0x3f000000 };
    for (int i = 0; i < kCarWheels; i++)
        if (RandomInt(2))
            memcpy(&car->suspensionCompression[i], &kSuspension[RandomInt(4)], sizeof(float));
    for (int i = 0; i < kCarWheels; i++)
        if (RandomInt(2))
            car->tyreDamagePoints[i] = 0;
    if (RandomInt(2))
        car->reversing = uint8_t(RandomInt(2));
}

uint64_t g_value[2];

template <class F>
void Accessor(PBondCar *car, const char *what, uint32_t at, int index, F &&call) {
    TakeState(&g_saved);
    ScrambleCar(car);
    g_value[0] = g_value[1] = 0xdeadbeefdeadbeefull;
    Both(at, [&](bool original) { g_value[original ? 0 : 1] = call(original); });
    const bool differed = CompareRuns(what, index);
    char name[64];
    snprintf(name, sizeof(name), "%s result", what);
    const int differBefore = g_differ;
    CheckBytes(name, index, &g_value[0], &g_value[1], sizeof(uint64_t));
    Tally(what, differed || g_differ != differBefore);
    PutState(&g_saved);
}

uint64_t Bits(float f) { return std::bit_cast<uint32_t>(f); }
uint64_t Bits(const void *p) { return uintptr_t(p); }

void TestAccessors() {
    int index = 0;
    for (PBondCar *car : g_cars) {
        Bind(car);
        for (int repeat = 0; repeat < 8; repeat++, index++) {
            const int wheel = RandomInt(kCarWheels);
            const uint32_t word = Random();
            const float value = Uniform(-2.0f, 2.0f);
            const bool flag = RandomInt(2) != 0;
            BondCarControl control;

#define GET_PTR(name, at) Accessor(car, #name, at, index, [&](bool o) { \
                return o ? Bits(((PtrGetFn)(at))(car, 0)) : Bits(car->name()); })
#define GET_INT(name, at) Accessor(car, #name, at, index, [&](bool o) { \
                return uint64_t(uint32_t(o ? ((IntGetFn)(at))(car, 0) : int(car->name()))); })
#define GET_BYTE(name, at) Accessor(car, #name, at, index, [&](bool o) { \
                return uint64_t(o ? ((ByteGetFn)(at))(car, 0) : uint8_t(car->name())); })
#define GET_FLOAT(name, at) Accessor(car, #name, at, index, [&](bool o) { \
                return o ? Bits(((FloatGetFn)(at))(car, 0)) : Bits(car->name()); })
#define INDEX_PTR(name, at) Accessor(car, #name, at, index, [&](bool o) { \
                return o ? Bits(((PtrIndexFn)(at))(car, 0, wheel)) : Bits(car->name(wheel)); })
#define INDEX_INT(name, at) Accessor(car, #name, at, index, [&](bool o) { \
                return uint64_t(uint32_t(o ? ((IntIndexFn)(at))(car, 0, wheel) : int(car->name(wheel)))); })
#define INDEX_BYTE(name, at) Accessor(car, #name, at, index, [&](bool o) { \
                return uint64_t(o ? ((ByteIndexFn)(at))(car, 0, wheel) : uint8_t(car->name(int8_t(wheel)))); })
#define INDEX_FLOAT(name, at) Accessor(car, #name, at, index, [&](bool o) { \
                return o ? Bits(((FloatIndexFn)(at))(car, 0, wheel)) : Bits(car->name(wheel)); })
#define SET(name, at, type, arg) Accessor(car, #name, at, index, [&](bool o) { \
                if (o) ((SetFn)(at))(car, 0, uint32_t(uintptr_t(arg))); else car->name((type)(uintptr_t)(arg)); \
                return uint64_t(0); })
#define SET_FLOAT(name, at) Accessor(car, #name, at, index, [&](bool o) { \
                if (o) ((SetFloatFn)(at))(car, 0, value); else car->name(value); return uint64_t(0); })
#define SET_FLAG(name, at) Accessor(car, #name, at, index, [&](bool o) { \
                if (o) ((SetFn)(at))(car, 0, flag); else car->name(flag); return uint64_t(0); })

            GET_PTR(GetAudio, 0x00065d50);
            SET(SetAudio, 0x00065d60, ABaseSound *, word);
            SET(SetAIGroundVehicle, 0x00065d70, AIGroundVehicle *, word);
            GET_PTR(GetAIGroundVehiclePtr, 0x00065d80);
            GET_INT(GetResetAvailable, 0x00065d90);
            GET_PTR(GetCarType, 0x00065da0);
            SET(SetCarClass, 0x00065db0, int, word);
            GET_INT(GetCarClass, 0x00065dc0);
            GET_INT(GetCarColour, 0x00065dd0);
            SET_FLOAT(SetCarControlSteering, 0x00065de0);
            SET_FLOAT(SetCarControlSteeringVertical, 0x00065df0);
            SET_FLOAT(SetCarControlStrafeHorizontal, 0x00065e00);
            SET_FLOAT(SetCarControlStrafeVertical, 0x00065e10);
            SET_FLOAT(SetCarControlGas, 0x00065e20);
            SET_FLOAT(SetCarControlBrake, 0x00065e30);
            SET_FLAG(SetCarControlHandBrake, 0x00065e40);
            GET_BYTE(GetCarControlHandBrake, 0x00065e50);
            SET_FLAG(SetCarControlFirePrimary, 0x00065e60);
            Accessor(car, "LockCarControlForever", 0x00065e70, index, [&](bool o) {
                if (o)
                    ((CarFn)0x00065e70)(car, 0);
                else
                    car->LockCarControlForever();
                return uint64_t(0);
            });
            SET_FLOAT(SetTargetGas, 0x00065ea0);
            SET_FLOAT(SetTargetBrake, 0x00065eb0);
            Accessor(car, "GetCarControl", 0x00065ec0, index, [&](bool o) {
                memset(&control, 0xcd, sizeof(control));
                void *result = o ? ((PtrIndexFn)0x00065ec0)(car, 0, int(uintptr_t(&control)))
                                 : car->GetCarControl(&control);
                uint64_t sum = uintptr_t(result);
                const uint32_t *words = reinterpret_cast<const uint32_t *>(&control);
                for (int i = 0; i < 8; i++)
                    sum = sum * 1000003u + words[i];
                return sum;
            });
            GET_BYTE(EmpActive, 0x00065ee0);
            GET_FLOAT(GetCarSpeed, 0x00065ef0);
            INDEX_BYTE(IsTyreShredded, 0x00065f00);
            INDEX_PTR(GetTyreTrackPtr, 0x00065f20);
            SET_FLAG(SetScoreable, 0x00065f30);
            GET_INT(InShock, 0x00065f40);
            SET_FLAG(SetOilSlick, 0x00065f50);
            GET_INT(GetNumWheelsOnGround, 0x00065f60);
            GET_BYTE(GetDamageByPlayerTimer, 0x00065f70);
            Accessor(car, "GetCarWheelSpinAngle", 0x00065f80, index, [&](bool o) {
                return o ? Bits(((FloatIndexFn)0x00065f80)(car, 0, wheel))
                         : Bits(car->GetCarWheelSpinAngle(int8_t(wheel)));
            });
            GET_FLOAT(GetCarSteer, 0x00065f90);
            INDEX_FLOAT(GetSuspensionCompression, 0x00065fa0);
            GET_INT(IsReversing, 0x00065fb0);
            SET_FLAG(SetAgainstWallFlag, 0x00065fc0);
            INDEX_FLOAT(GetWheelRoadHeight, 0x00065fd0);
            INDEX_INT(GetWheelRoadSurface, 0x00065ff0);
            INDEX_PTR(GetWheelRoadNormal, 0x00066010);
            SET(SetShieldPointLoc, 0x00066020, float *, word);
            Accessor(car, "IsWheelOnGround", 0x00066030, index, [&](bool o) {
                return uint64_t(uint32_t(o ? ((IntIndexFn)0x00066030)(car, 0, wheel)
                                           : car->IsWheelOnGround(int8_t(wheel))));
            });

#undef GET_PTR
#undef GET_INT
#undef GET_BYTE
#undef GET_FLOAT
#undef INDEX_PTR
#undef INDEX_INT
#undef INDEX_BYTE
#undef INDEX_FLOAT
#undef SET
#undef SET_FLOAT
#undef SET_FLAG
        }
    }
}

// ---- NIGHTFIRE_BONDCARSHADOWB=3: live
//
// The port's ProcessSubmarinePhysics and ProcessSplinePhysics entries jump here; each call runs the original and
// then the port on the car's state exactly as the game hands it over, compares, and leaves the port's result (so
// the game goes on as the port build does).

#define LiveSimStepCount (*(const int32_t *)0x00234e34)

struct LiveHook {
    uint32_t at;            // the port's entry
    uint8_t saved[5];
    bool on;
};
LiveHook g_subHook, g_splineHook;
bool g_liveOn = false, g_liveDone = false;
int g_liveTicks = 0, g_liveTickLimit = 600;
int g_liveCalls = 0, g_liveDiffer = 0, g_liveReported = 0;

void Patch(LiveHook *hook, const void *to) {
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)hook->at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(hook->saved, (void *)(uintptr_t)hook->at, 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t rel = int32_t(uint32_t(uintptr_t(to)) - (hook->at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)hook->at, jump, 5);
    VirtualProtect((void *)(uintptr_t)hook->at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)hook->at, 5);
    hook->on = true;
}

void Unpatch(LiveHook *hook) {
    if (!hook->on)
        return;
    DWORD old;
    VirtualProtect((void *)(uintptr_t)hook->at, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)(uintptr_t)hook->at, hook->saved, 5);
    VirtualProtect((void *)(uintptr_t)hook->at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)hook->at, 5);
    hook->on = false;
}

void PrintFloat(const char *name, float f) {
    printf(" %s=%.9g(%08x)", name, f, std::bit_cast<uint32_t>(f));
}

void PrintVector(const char *name, const void *v) {
    const float *f = static_cast<const float *>(v);
    printf("[bondcarB-live]     %s", name);
    for (int i = 0; i < 4; i++)
        printf(" %.9g(%08x)", f[i], std::bit_cast<uint32_t>(f[i]));
    printf("\n");
}

// Every differing 32-bit word of a region, the first 12
void PrintRegion(const char *region, const void *a, const void *b, size_t bytes) {
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    int shown = 0, count = 0;
    for (size_t at = 0; at + 4 <= bytes; at += 4) {
        if (memcmp(x + at, y + at, 4) == 0)
            continue;
        count++;
        if (shown++ >= 12)
            continue;
        uint32_t u, v;
        memcpy(&u, x + at, 4);
        memcpy(&v, y + at, 4);
        printf("[bondcarB-live]     %s +0x%03x: original %08x (%.9g), port %08x (%.9g)\n", region, unsigned(at), u,
               std::bit_cast<float>(u), v, std::bit_cast<float>(v));
    }
    if (count > 12)
        printf("[bondcarB-live]     %s: %d words differ in all\n", region, count);
}

// The inputs the submarine and spline physics read, from the state the call started with
void PrintInputs(const CarState *state) {
    const PBondCar *car = reinterpret_cast<const PBondCar *>(state->car);
    const BondCarControl &c = car->control;
    printf("[bondcarB-live]   inputs:");
    PrintFloat("steering", c.steering);
    PrintFloat("steeringVertical", c.steeringVertical);
    PrintFloat("gas", c.gas);
    PrintFloat("brake", c.brake);
    printf(" handBrake=%d class=%d subPhysics=%d\n", c.handBrake, car->carClass, int(state->physics.subPhysics));
    printf("[bondcarB-live]     reverseTimer=%d rocketBoost=%d rollSub=%d subRolling=%d forceRollDirection=%d "
           "unknown408=%d againstWall=%d cameraMode=%d interior=%d", car->reverseTimer, car->rocketBoost,
           car->rollSub, car->subRolling, car->forceRollDirection, car->unknown408, car->againstWall,
           state->cameraMode,
           fgCameraTables.modes != NULL ? fgCameraTables.modes[state->cameraMode].interiorView : -1);
    PrintFloat("bob", car->bob);
    PrintFloat("bobAngle", car->bobAngle);
    PrintFloat("unknown3E8", car->unknown3E8);
    PrintFloat("carSpeed", car->carSpeed);
    PrintFloat("hitPoints", car->hitPointLoc != NULL ? *car->hitPointLoc : 1.0f);
    printf("\n");
    PrintVector("position", &state->body.position);
    PrintVector("velocity", &state->body.velocity);
    PrintVector("angularVelocity", &state->body.angularVelocity);
    PrintVector("angularMomentum", &state->body.angularMomentum);
    PrintVector("force", &state->body.force);
    for (int row = 0; row < 4; row++) {
        char name[24];
        snprintf(name, sizeof(name), "orientation[%d]", row);
        PrintVector(name, MatrixRow(&state->info.orientation, row));
    }
    printf("[bondcarB-live]     mass=%.9g(%08x) wheelSpinAngle %.9g %.9g wheelRadius=%.9g\n", state->body.mass,
           std::bit_cast<uint32_t>(state->body.mass), car->wheelSpinAngle[0], car->wheelSpinAngle[1],
           state->physics.wheelRadius);
}

bool SameState(const CarState *a, const CarState *b) {
    return memcmp(a->car, b->car, sizeof(PBondCar)) == 0 && memcmp(&a->body, &b->body, sizeof(RigidBody)) == 0 &&
           memcmp(&a->info, &b->info, sizeof(RigidBodyInfo)) == 0 &&
           memcmp(&a->physics, &b->physics, sizeof(CarPhysics)) == 0 && memcmp(a->audio, b->audio, kAudioBytes) == 0;
}

struct OriginalCall { CarFn fn; PBondCar *car; };

bool RunOriginal(uint32_t original, PBondCar *car) {
    XbeOriginal_Restore(original, true);
    OriginalCall call = { (CarFn)(uintptr_t)original, car };
    const bool ran = Guarded([](void *context, bool) {
        OriginalCall *c = static_cast<OriginalCall *>(context);
        c->fn(c->car, 0);
    }, &call, true);
    XbeOriginal_Restore(original, false);
    return ran;
}

void PrintWheelPos(const char *who, const WWorldPos *w) {
    printf("[bondcarB-live]       %s face valid=%d", who, w->valid);
    for (int k = 0; k < 3; k++)
        printf(" (%.6g %.6g %.6g %08x)", w->face.corner[k].x, w->face.corner[k].y, w->face.corner[k].z,
               w->face.corner[k].flags);
    printf("\n");
}

// The spline physics' wheels as the start, the original and the port left them, and the points the listing puts
// them at (the path's frame times the spline's placement, each lever rotated by it, plus the path's position)
void PrintSplineWheels() {
    const PBondCar *start = reinterpret_cast<const PBondCar *>(g_start.car);
    const PBondCar *runs[2] = { reinterpret_cast<const PBondCar *>(g_result[0].car),
                                reinterpret_cast<const PBondCar *>(g_result[1].car) };
    alignas(16) Coord4 expected[kCarWheels] = {};
    if (HasSplinePath(g_live.car)) {
        AISplinePath *spline = Shadow_GetSplinePath(g_live.car->aiGroundVehicle, 0);
        alignas(16) MATRIX4 orientation;
        spline->path->GetOrientMat(&orientation);
        const Coord3 *pathPosition = spline->path->GetPosition();
        printf("[bondcarB-live]     path position %.9g %.9g %.9g\n", pathPosition->x, pathPosition->y,
               pathPosition->z);
        // the listing copies the path's position into the frame's row 3 (its stack slots overlap) before the
        // multiply, so the wheels sit at the placed position
        orientation.mtx[3][0] = pathPosition->x;
        orientation.mtx[3][1] = pathPosition->y;
        orientation.mtx[3][2] = pathPosition->z;
        VU0_MATRIX4_mult(&orientation, &orientation, &spline->placement);
        const Coord4 *position = MatrixRow(&orientation, 3);
        for (int row = 0; row < 4; row++) {
            char name[32];
            snprintf(name, sizeof(name), "path*placement[%d]", row);
            PrintVector(name, MatrixRow(&orientation, row));
        }
        for (int i = 0; i < kCarWheels; i++) {
            VU0_MATRIX4_vect3rotate(&g_start.info.levers[i], &orientation, &expected[i]);
            expected[i].x += position->x;
            expected[i].y += position->y;
            expected[i].z += position->z;
        }
    }
    for (int i = 0; i < kCarWheels; i++) {
        printf("[bondcarB-live]     wheel %d: lever %.6g %.6g %.6g, listing's point %.9g %.9g %.9g\n", i,
               g_start.info.levers[i].x, g_start.info.levers[i].y, g_start.info.levers[i].z, expected[i].x,
               expected[i].y, expected[i].z);
        for (int run = 0; run < 2; run++)
            printf("[bondcarB-live]       %s point %.9g %.9g %.9g, road height %.9g, compression %.9g\n",
                   run == 0 ? "original" : "port", runs[run]->wheelPos[i].x, runs[run]->wheelPos[i].y,
                   runs[run]->wheelPos[i].z, runs[run]->wheelRoadNormal[i].w, runs[run]->suspensionCompression[i]);
        PrintWheelPos("start   ", &start->wheels[i]);
        PrintWheelPos("original", &runs[0]->wheels[i]);
        PrintWheelPos("port    ", &runs[1]->wheels[i]);
    }
}

// Each side run once more from the start: a run that does not repeat itself reads something the state does not
// cover (stack, or memory outside the car, body, info and physics record)
void PrintRepeats(uint32_t original, PBondCar *car, void (*port)(PBondCar *)) {
    static CarState again;
    PutState(&g_start);
    RunOriginal(original, car);
    TakeState(&again);
    const bool originalRepeats = SameState(&again, &g_result[0]);
    PutState(&g_start);
    port(car);
    TakeState(&again);
    const bool portRepeats = SameState(&again, &g_result[1]);
    PutState(&g_result[1]);
    printf("[bondcarB-live]     run again from the start: the original %s, the port %s\n",
           originalRepeats ? "repeats" : "DIFFERS FROM ITSELF", portRepeats ? "repeats" : "DIFFERS FROM ITSELF");
}

void LiveCompare(PBondCar *car, uint32_t original, LiveHook *hook, const char *what, void (*port)(PBondCar *)) {
    Bind(car);
    TakeState(&g_start);
    const bool ran = RunOriginal(original, car);
    TakeState(&g_result[0]);
    PutState(&g_start);

    Unpatch(hook);
    port(car);   // the caller puts the jump back
    TakeState(&g_result[1]);

    g_liveCalls++;
    const bool same = ran && SameState(&g_result[0], &g_result[1]);
    if (same)
        return;
    g_liveDiffer++;
    if (g_liveReported++ < 20)
        printf("[bondcarB-live] %s differs: tick %d (shadow tick %d), car %p%s\n", what, LiveSimStepCount,
               g_liveTicks, (void *)car, ran ? "" : " - the original faulted");
    if (g_liveReported <= 3) {
        PrintInputs(&g_start);
        PrintRegion("car", g_result[0].car, g_result[1].car, sizeof(PBondCar));
        PrintRegion("body", &g_result[0].body, &g_result[1].body, sizeof(RigidBody));
        PrintRegion("info", &g_result[0].info, &g_result[1].info, sizeof(RigidBodyInfo));
        PrintRegion("physics", &g_result[0].physics, &g_result[1].physics, sizeof(CarPhysics));
        PrintRegion("audio", g_result[0].audio, g_result[1].audio, kAudioBytes);
        if (original == 0x00065740)
            PrintSplineWheels();
        PrintRepeats(original, car, port);
    }
    fflush(stdout);
}

void __fastcall LiveSubmarine(PBondCar *car, int) {
    LiveCompare(car, 0x000644c0, &g_subHook, "ProcessSubmarinePhysics",
                [](PBondCar *c) { c->ProcessSubmarinePhysics(); });
    Patch(&g_subHook, (const void *)&LiveSubmarine);
}

void __fastcall LiveSpline(PBondCar *car, int) {
    LiveCompare(car, 0x00065740, &g_splineHook, "ProcessSplinePhysics",
                [](PBondCar *c) { c->ProcessSplinePhysics(); });
    Patch(&g_splineHook, (const void *)&LiveSpline);
}

// The live PBondCars with a body, its info and a physics record
void GatherCars() {
    g_cars.clear();
    for (int i = 0; i < kOwners; i++) {
        PhysicsObject *owner = PhysicsObjects[i];
        if (owner == NULL || uint32_t(uintptr_t(owner->vtable)) != kBondCarVtable)
            continue;
        PBondCar *car = static_cast<PBondCar *>(static_cast<PVehicle *>(owner));
        RigidBody *body = BodyOf(car);
        if (body != NULL && body->info != NULL && car->physics != NULL)
            g_cars.push_back(car);
    }
}

// NIGHTFIRE_BONDCARSHADOWB=1: the cars at the first tick had no spline path (the AI hands one over a tick or two
// later), so the spline cases wait, each tick, for a car that has one - for 600 ticks
bool g_splinePending = false;
int g_splineWaited = 0;

void TestSplineOnceOneExists() {
    if (fgCollisionMgr == NULL)
        return;
    GatherCars();
    bool any = false;
    for (PBondCar *car : g_cars)
        any = any || HasSplinePath(car);
    if (!any) {
        if (++g_splineWaited >= g_liveTickLimit) {
            g_splinePending = false;
            printf("[bondcarB] no car took a spline path in %d ticks - spline cases not run\n", g_splineWaited);
            fflush(stdout);
        }
        return;
    }
    g_splinePending = false;
    FpControlGet(&g_x87, &g_sse);
    const int cases = g_cases, checks = g_checks, differ = g_differ, faults = g_faults;
    g_details = 0;
    TestSpline();
    ResetFpu();
    for (const Kind &kind : g_kinds)
        if (strcmp(kind.what, "ProcessSplinePhysics") == 0)
            printf("[bondcarB]   ProcessSplinePhysics: %d of %d cases differ\n", kind.differ, kind.cases);
    printf("[bondcarB] spline cases at tick %d (%d cars, %d on spline paths): %d cases, %d checks, %d differ%s\n",
           LiveSimStepCount, int(g_cars.size()), g_splineCars, g_cases - cases, g_checks - checks,
           g_differ - differ, g_faults != faults ? " (with faults)" : "");
    fflush(stdout);
}

int Mode() {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_BONDCARSHADOWB", value, sizeof(value));
    return length == 0 || length >= sizeof(value) ? 0 : atoi(value);
}

}  // namespace

void BondCarShadowB_Tick(void) {
    if (g_splinePending)
        TestSplineOnceOneExists();
    if (g_liveDone)
        return;
    if (!g_liveOn) {
        if (Mode() != 3) {
            g_liveDone = true;
            return;
        }
        char value[16] = "";
        DWORD length = GetEnvironmentVariableA("NIGHTFIRE_BONDCARSHADOWB_TICKS", value, sizeof(value));
        if (length != 0 && length < sizeof(value) && atoi(value) > 0)
            g_liveTickLimit = atoi(value);
        FpControlGet(&g_x87, &g_sse);
        g_subHook.at = uint32_t(XbeAddress(&PBondCar::ProcessSubmarinePhysics));
        g_splineHook.at = uint32_t(XbeAddress(&PBondCar::ProcessSplinePhysics));
        Patch(&g_subHook, (const void *)&LiveSubmarine);
        Patch(&g_splineHook, (const void *)&LiveSpline);
        g_liveOn = true;
        printf("[bondcarB-live] comparing every submarine and spline physics call for %d ticks from tick %d\n",
               g_liveTickLimit, LiveSimStepCount);
        fflush(stdout);
    }
    if (++g_liveTicks < g_liveTickLimit)
        return;
    Unpatch(&g_subHook);
    Unpatch(&g_splineHook);
    g_liveDone = true;
    printf("[bondcarB-live] %d ticks to tick %d: %d calls, %d differ\n", g_liveTicks, LiveSimStepCount, g_liveCalls,
           g_liveDiffer);
    fflush(stdout);
}

void BondCarShadowB_Run(void) {
    if (Mode() == 0 || Mode() == 3)   // 3: the live comparison only (BondCarShadowB_Tick)
        return;
    g_kinds.clear();
    GatherCars();
    if (g_cars.empty() || fgCollisionMgr == NULL) {
        printf("[bondcarB] no live cars - nothing tested\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);

    TestSubmarine();
    TestSpline();
    TestDebugObject();
    TestColourVariation();
    TestAccessors();
    ResetFpu();

    for (const Kind &kind : g_kinds)
        if (kind.differ != 0)
            printf("[bondcarB]   %s: %d of %d cases differ\n", kind.what, kind.differ, kind.cases);
    printf("[bondcarB] PBondCar part 2 vs originals (%d cars, %d on spline paths): %d cases, %d checks, %d differ%s\n",
           int(g_cars.size()), g_splineCars, g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_splineCars == 0) {
        g_splinePending = true;
        printf("[bondcarB] no car has a spline path yet: the spline cases run on the first tick one does\n");
    }
    fflush(stdout);
}
