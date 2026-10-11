#ifndef DRIVING_DEVTOOLS_BONDCARORIGTUNING_H_
#define DRIVING_DEVTOOLS_BONDCARORIGTUNING_H_

// The originals' copy of the cars' tuning (game/BondCarState.h's BondCar_* globals): the block at 0x001c36cc the
// original InitializeBondCarGlobals loads and the original car code reads, and BondCar_FarDistanceSq at 0x001c36c0.
// The bond car shadows snapshot each side's tuning as a block and lend ours to the originals before they run.

#include <stddef.h>
#include <stdint.h>

#include "../game/BondCarState.h"

struct BondCarTuning {                  // 0xdc, at 0x001c36cc
    float enableRollStopsThreshold;     // +0x00
    float baseFrictionMass;
    float shredDrag;
    float padDeadZone;
    float twoWheelAngle;                // +0x10
    float twoWheelScale;
    float twoWheelLimit;
    float twoWheelOppositeScale;
    float twoWheelTensorScale;          // +0x20
    float skidAudioScale;
    float rollingResistance;
    float minButtonValue;
    float wheelSpinExtraRpm;            // +0x30
    uint32_t unknown34;                 // not ours: the game's
    float damageScaleCollision;
    int32_t empLifetime;
    float tyreDamageRadius;             // +0x40
    float maxWheelSpinRate;
    float maxWheelSpinRateAI;
    int32_t postBrakeAccelCount;
    float postBrakeAccelScale;          // +0x50
    float postBrakeAccelMin;
    uint32_t unknown58;                 // not ours: the game's
    float friction[kCarSurfaces];       // +0x5c
    float lateralLoss[kCarSurfaces];    // +0x9c
};
static_assert(offsetof(BondCarTuning, friction) == 0x5c && sizeof(BondCarTuning) == 0xdc,
              "the tuning block is 0x001c36cc-0x001c37a8");

#define OrigBondCarTuning (*(BondCarTuning *)0x001c36cc)
#define OrigBondCar_FarDistanceSq (*(float *)0x001c36c0)

// Our tuning as a block (the two words that are not ours from the game's)
inline BondCarTuning OurBondCarTuning() {
    BondCarTuning t = OrigBondCarTuning;
    t.enableRollStopsThreshold = BondCar_ENABLE_ROLL_STOPS_THRESHOLD;
    t.baseFrictionMass = BondCar_BASE_FRICTION_MASS;
    t.shredDrag = BondCar_SHRED_DRAG;
    t.padDeadZone = BondCar_PAD_DEAD_ZONE;
    t.twoWheelAngle = BondCar_TWO_WHEEL_ANGLE;
    t.twoWheelScale = BondCar_TWO_WHEEL_SCALE;
    t.twoWheelLimit = BondCar_TWO_WHEEL_LIMIT;
    t.twoWheelOppositeScale = BondCar_TWO_WHEEL_OPPOSITE_SCALE;
    t.twoWheelTensorScale = BondCar_TWO_WHEEL_TENSOR_SCALE;
    t.skidAudioScale = BondCar_SKID_AUDIO_SCALE;
    t.rollingResistance = BondCar_ROLLING_RESISTANCE;
    t.minButtonValue = BondCar_MIN_BUTTON_VALUE;
    t.wheelSpinExtraRpm = BondCar_WHEEL_SPIN_EXTRA_RPM;
    t.damageScaleCollision = BondCar_DAMAGE_SCALE_COLLISION;
    t.empLifetime = BondCar_EMP_LIFETIME;
    t.tyreDamageRadius = BondCar_TYRE_DAMAGE_RADIUS;
    t.maxWheelSpinRate = BondCar_MAX_WHEEL_SPIN_RATE;
    t.maxWheelSpinRateAI = BondCar_MAX_WHEEL_SPIN_RATE_AI;
    t.postBrakeAccelCount = BondCar_POST_BRAKE_ACCEL_COUNT;
    t.postBrakeAccelScale = BondCar_POST_BRAKE_ACCEL_SCALE;
    t.postBrakeAccelMin = BondCar_POST_BRAKE_ACCEL_MIN;
    for (int i = 0; i < kCarSurfaces; i++) {
        t.friction[i] = BondCar_friction[i];
        t.lateralLoss[i] = BondCar_lateralLoss[i];
    }
    return t;
}

inline void SetOurBondCarTuning(const BondCarTuning &t) {
    BondCar_ENABLE_ROLL_STOPS_THRESHOLD = t.enableRollStopsThreshold;
    BondCar_BASE_FRICTION_MASS = t.baseFrictionMass;
    BondCar_SHRED_DRAG = t.shredDrag;
    BondCar_PAD_DEAD_ZONE = t.padDeadZone;
    BondCar_TWO_WHEEL_ANGLE = t.twoWheelAngle;
    BondCar_TWO_WHEEL_SCALE = t.twoWheelScale;
    BondCar_TWO_WHEEL_LIMIT = t.twoWheelLimit;
    BondCar_TWO_WHEEL_OPPOSITE_SCALE = t.twoWheelOppositeScale;
    BondCar_TWO_WHEEL_TENSOR_SCALE = t.twoWheelTensorScale;
    BondCar_SKID_AUDIO_SCALE = t.skidAudioScale;
    BondCar_ROLLING_RESISTANCE = t.rollingResistance;
    BondCar_MIN_BUTTON_VALUE = t.minButtonValue;
    BondCar_WHEEL_SPIN_EXTRA_RPM = t.wheelSpinExtraRpm;
    BondCar_DAMAGE_SCALE_COLLISION = t.damageScaleCollision;
    BondCar_EMP_LIFETIME = t.empLifetime;
    BondCar_TYRE_DAMAGE_RADIUS = t.tyreDamageRadius;
    BondCar_MAX_WHEEL_SPIN_RATE = t.maxWheelSpinRate;
    BondCar_MAX_WHEEL_SPIN_RATE_AI = t.maxWheelSpinRateAI;
    BondCar_POST_BRAKE_ACCEL_COUNT = t.postBrakeAccelCount;
    BondCar_POST_BRAKE_ACCEL_SCALE = t.postBrakeAccelScale;
    BondCar_POST_BRAKE_ACCEL_MIN = t.postBrakeAccelMin;
    for (int i = 0; i < kCarSurfaces; i++) {
        BondCar_friction[i] = t.friction[i];
        BondCar_lateralLoss[i] = t.lateralLoss[i];
    }
}

// Both copies the same: ours lent to the originals
inline void LendBondCarTuning() {
    OrigBondCarTuning = OurBondCarTuning();
    OrigBondCar_FarDistanceSq = BondCar_FarDistanceSq;
}

#endif // DRIVING_DEVTOOLS_BONDCARORIGTUNING_H_
