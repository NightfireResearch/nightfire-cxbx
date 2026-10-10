#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "Vehicle.h"

#include <bit>
#include <stdio.h>

#include "../audio/Bank.h"              // SharedTreeIterator
#include "../data/AttributeParsers.h"
#include "../data/AttributeSystem.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealPrint.h"      // MEM_fill

// ---------------------------------------------------------------------------------------------------------------
// PVehicle (0x0006f810-0x00071930) and PhysicsData's constructor, ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// The C runtime's case-insensitive comparison (the car names' order)
#define Crt_stricmp ((int (*)(const char *, const char *))0x00134537)

// ---- PhysicsData

// FUNC_AT(0x0006ec80)
PhysicsData* PhysicsData::Construct(const char *name) {
    attributes.Construct("smackable", name);
    defaultSound = -1;
    warningSound = -1;
    impactSoundLo = -1;
    impactSoundMed = -1;
    impactSoundHi = -1;
    scrapeSound = -1;
    mass = 100.0f;
    hitPoints = 500.0f;
    forceDetach = 0;
    description = 0.0f;
    return this;
}

// ---- the car's attributes

// FUNC_AT(0x0006f810)
AttributeSet* PVehicle::GetNamedAttribs(AttributeSet *result, const char *name) {
    result->Construct("pvehicle", name);
    return result;
}

// FUNC_AT(0x0006f840)
CarPhysics* PVehicle::CarPhysicsAttrib(AttributeSet *attributes) {
    return static_cast<CarPhysics *>(attributes->LookupStruct("CarPhysics", CarPhysicsType, NULL));
}

// FUNC_AT(0x0006f860)
const char* PVehicle::RenderNameAttrib(AttributeSet *attributes) {
    const char *name = attributes->LookupString("RENDER_FILENAME", NULL);
    if (name == NULL) {
        char file[256];
        sprintf(file, "%s.crp", attributes->Name());
        name = AttributeSystemInstance->MakeString(file);
    }
    return name;
}

// FUNC_AT(0x0006f8c0)
uint32_t PVehicle::NumColoursAttrib(AttributeSet *attributes) {
    uint32_t colours = attributes->LookupUInt("RENDER_NUMCOLORS", NULL);
    if (colours == 0)
        colours = 1;
    return colours;
}

// FUNC_AT(0x0006f8e0)
void InitCarPhysics(const char *className, const char *collectionName, const char *attributeName, uint32_t type,
                    void *data) {
    MEM_fill(data, 0, sizeof(CarPhysics));
}

// ---- InitializeGlobals

// Each CarPhysics field is registered, then given an editor range. ConfigEditParameters (empty in the retail
// game) takes the range's bounds as raw words, floats or ints as the field's type, then -1 for a CarPhysics field
// and 1 for a plain key of the class.
static void RegisterFloatField(const char *name, uint32_t offset, float low, float high) {
    AttributeSystemInstance->RegisterExtensionField(CarPhysicsType, name, Float_AttribByteOffsetParserFunc, offset,
                                                    1, 0, 0);
    AttributeSystemInstance->ConfigEditParameters(AttributeSystemInstance->GetExtensionTypeClass(CarPhysicsType),
                                                  name, std::bit_cast<uint32_t>(low), high, -1.0f, 0);
}

static void RegisterIntField(const char *name, uint32_t offset, int low, int high) {
    AttributeSystemInstance->RegisterExtensionField(CarPhysicsType, name, Int_AttribByteOffsetParserFunc, offset, 1,
                                                    0, 0);
    AttributeSystemInstance->ConfigEditParameters(AttributeSystemInstance->GetExtensionTypeClass(CarPhysicsType),
                                                  name, low, std::bit_cast<float>(high), -1.0f, 0);
}

static void EditFloatRange(const char *name, float low, float high) {
    AttributeSystemInstance->ConfigEditParameters("pvehicle", name, std::bit_cast<uint32_t>(low), high, 1.0f, 0);
}

static void EditIntRange(const char *name, int low, int high) {
    AttributeSystemInstance->ConfigEditParameters("pvehicle", name, low, std::bit_cast<float>(high), 1.0f, 0);
}

// FUNC_AT(0x0006f930)
void PVehicle::InitializeGlobals() {
    CarPhysicsType = AttributeSystemInstance->RegisterExtensionType("pvehicle", "CarPhysics", sizeof(CarPhysics),
                                                                    InitCarPhysics);
    RegisterFloatField("SPEED_CUTOFF_REVERSE", offsetof(CarPhysics, speedCutoffReverse), 0.0f, 200.0f);
    RegisterFloatField("SPEED_CUTOFF_RATE_REVERSE", offsetof(CarPhysics, speedCutoffRateReverse), 0.0f, 1.0f);
    RegisterFloatField("MAXACC", offsetof(CarPhysics, maxAcc), 0.0f, 30.0f);
    RegisterFloatField("MAXREVACC", offsetof(CarPhysics, maxRevAcc), 0.0f, 30.0f);
    RegisterFloatField("MAXBRAKE", offsetof(CarPhysics, maxBrake), 0.0f, 30.0f);
    RegisterFloatField("MAXSTEERING", offsetof(CarPhysics, maxSteering), 0.0f, 0.2f);
    RegisterFloatField("MODSTEERSPEED", offsetof(CarPhysics, modSteerSpeed), 0.0f, 50.0f);
    RegisterFloatField("MINSTEER", offsetof(CarPhysics, minSteer), 0.0f, 1.0f);
    RegisterFloatField("FRIC_MOD_SPEED", offsetof(CarPhysics, fricModSpeed), 0.0f, 100.0f);
    RegisterFloatField("FRIC_MOD_RANGE", offsetof(CarPhysics, fricModRange), 0.0f, 100.0f);
    RegisterFloatField("FRIC_MOD_MIN", offsetof(CarPhysics, fricModMin), 0.0f, 1.0f);
    RegisterFloatField("COUNTER_STEER_SCALE", offsetof(CarPhysics, counterSteerScale), 0.0f, 1.0f);
    RegisterFloatField("COUNTER_STEER_SCALE_LIMIT", offsetof(CarPhysics, counterSteerScaleLimit), 0.0f, 1.0f);
    RegisterFloatField("GEAR_LIMITR", offsetof(CarPhysics, gearLimit[0]), 0.0f, 100.0f);
    RegisterFloatField("GEAR_LIMIT1", offsetof(CarPhysics, gearLimit[1]), 0.0f, 100.0f);
    RegisterFloatField("GEAR_LIMIT2", offsetof(CarPhysics, gearLimit[2]), 0.0f, 100.0f);
    RegisterFloatField("GEAR_LIMIT3", offsetof(CarPhysics, gearLimit[3]), 0.0f, 100.0f);
    RegisterFloatField("GEAR_LIMIT4", offsetof(CarPhysics, gearLimit[4]), 0.0f, 100.0f);
    RegisterFloatField("GEAR_LIMIT5", offsetof(CarPhysics, gearLimit[5]), 0.0f, 100.0f);
    RegisterFloatField("GEAR_RATIOR", offsetof(CarPhysics, gearRatio[0]), 0.0f, 1.0f);
    RegisterFloatField("GEAR_RATIO1", offsetof(CarPhysics, gearRatio[1]), 0.0f, 1.0f);
    RegisterFloatField("GEAR_RATIO2", offsetof(CarPhysics, gearRatio[2]), 0.0f, 1.0f);
    RegisterFloatField("GEAR_RATIO3", offsetof(CarPhysics, gearRatio[3]), 0.0f, 1.0f);
    RegisterFloatField("GEAR_RATIO4", offsetof(CarPhysics, gearRatio[4]), 0.0f, 1.0f);
    RegisterFloatField("GEAR_RATIO5", offsetof(CarPhysics, gearRatio[5]), 0.0f, 1.0f);
    RegisterFloatField("FRICTIONLIMITFRONT", offsetof(CarPhysics, frictionLimitFront), 0.0f, 50.0f);
    RegisterFloatField("FRICTIONLIMITREAR", offsetof(CarPhysics, frictionLimitRear), 0.0f, 50.0f);
    RegisterFloatField("TYREGRIPFACTOR", offsetof(CarPhysics, tyreGripFactor), 0.0f, 16.0f);
    RegisterFloatField("NOSE_JUMP_ANGLE", offsetof(CarPhysics, noseJumpAngle), 0.0f, 0.75f);
    RegisterIntField("NUM_BLEND_STEPS", offsetof(CarPhysics, numBlendSteps), 1, 200);
    RegisterFloatField("SLOPE_SCALE", offsetof(CarPhysics, slopeScale), 0.0f, 1.0f);
    RegisterFloatField("SPRING_STIFFNESS_FRONT", offsetof(CarPhysics, springStiffnessFront), 0.0f, 1000.0f);
    RegisterFloatField("SPRING_STIFFNESS_REAR", offsetof(CarPhysics, springStiffnessRear), 0.0f, 1000.0f);
    RegisterFloatField("SPRING_DAMPING_FRONT", offsetof(CarPhysics, springDampingFront), 0.0f, 1000.0f);
    RegisterFloatField("SPRING_DAMPING_REAR", offsetof(CarPhysics, springDampingRear), 0.0f, 1000.0f);
    RegisterFloatField("SPRING_REST_LENGTH", offsetof(CarPhysics, springRestLength), 0.0f, 2.0f);
    RegisterFloatField("SPRING_COMPRESSION_LIMIT", offsetof(CarPhysics, springCompressionLimit), 0.0f, 2.0f);
    RegisterFloatField("SPRING_COMPRESSION_LIMIT_DRAW", offsetof(CarPhysics, springCompressionLimitDraw), 0.0f, 2.0f);
    RegisterFloatField("WHEEL_RADIUS", offsetof(CarPhysics, wheelRadius), 0.0f, 1.0f);
    RegisterFloatField("BRAKING_FRICTION", offsetof(CarPhysics, brakingFriction), 0.0f, 3.0f);
    RegisterFloatField("ACCEL_FRW_RATIO", offsetof(CarPhysics, accelFrwRatio), 0.1f, 0.9f);
    RegisterFloatField("COAST_FRW_RATIO", offsetof(CarPhysics, coastFrwRatio), 0.1f, 0.9f);
    RegisterFloatField("BRAKE_FRW_RATIO", offsetof(CarPhysics, brakeFrwRatio), 0.1f, 0.9f);
    RegisterFloatField("REVERSE_FRW_RATIO", offsetof(CarPhysics, reverseFrwRatio), 0.1f, 0.9f);
    RegisterFloatField("HANDBRAKE_FRW_RATIO", offsetof(CarPhysics, handbrakeFrwRatio), 0.1f, 0.9f);
    RegisterFloatField("REV_HANDBRAKE_FRW_RATIO", offsetof(CarPhysics, revHandbrakeFrwRatio), 0.0f, 1.0f);
    RegisterFloatField("HANDBRAKE_FORCE", offsetof(CarPhysics, handbrakeForce), 0.0f, 30.0f);
    RegisterFloatField("FRONT_SLIPPING_SCALE", offsetof(CarPhysics, frontSlippingScale), 0.0f, 1.0f);
    RegisterFloatField("REAR_SLIPPING_SCALE", offsetof(CarPhysics, rearSlippingScale), 0.0f, 1.0f);
    RegisterFloatField("SLIPPING_RANGE", offsetof(CarPhysics, slippingRange), 0.0f, 10.0f);
    RegisterFloatField("TOTAL_HANDBRAKE_FRICTION_SCALE", offsetof(CarPhysics, totalHandbrakeFrictionScale), 0.1f,
                       1.5f);
    RegisterFloatField("HANDBRAKE_RW_FRIC_SCALE", offsetof(CarPhysics, handbrakeRwFricScale), 0.0f, 2.0f);
    RegisterFloatField("WHEELSPINSCALE", offsetof(CarPhysics, wheelSpinScale), 0.0f, 1.0f);
    RegisterFloatField("WS_SPEED", offsetof(CarPhysics, wsSpeed), 0.0f, 20.0f);
    RegisterFloatField("WS_ACCEL", offsetof(CarPhysics, wsAccel), 0.0f, 10.0f);
    RegisterFloatField("WHEEL_FORCE_APP_SCALE", offsetof(CarPhysics, wheelForceAppScale), 0.0f, 1.0f);
    RegisterFloatField("YAW_STABILITY_FACTOR", offsetof(CarPhysics, yawStabilityFactor), 0.0f, 1.0f);
    RegisterIntField("IS_4X4", offsetof(CarPhysics, is4x4), 0, 1);
    RegisterIntField("IS_RALLY", offsetof(CarPhysics, isRally), 0, 1);
    RegisterIntField("IS_SUB", offsetof(CarPhysics, isSub), 0, 1);
    RegisterIntField("IS_SNOWMOBILE", offsetof(CarPhysics, isSnowmobile), 0, 1);
    RegisterIntField("NO_WORLD_COLLISIONS", offsetof(CarPhysics, noWorldCollisions), 0, 1);
    RegisterIntField("SUB_PHYSICS", offsetof(CarPhysics, subPhysics), 0, 1);
    RegisterIntField("IS_BOAT", offsetof(CarPhysics, isBoat), 0, 1);
    EditFloatRange("TYRE_RADIUS", 0.0f, 10.0f);
    EditFloatRange("CAR_WHEEL_X_OFFSET", -3.0f, 3.0f);
    EditFloatRange("CAR_WHEEL_ZF_OFFSET", -3.0f, 3.0f);
    EditFloatRange("CAR_WHEEL_ZR_OFFSET", -3.0f, 3.0f);
    EditFloatRange("STUB_BASE", 0.0f, 1.0f);
    EditFloatRange("MASS", 1.0f, 2000.0f);
    EditIntRange("RENDER_NUMCOLORS", 1, 16);
    EditFloatRange("TYRE_TREAD_WIDTH", 0.0f, 4.0f);
    EditFloatRange("BOOST_EXTRA_ACC", 0.0f, 1000.0f);
    EditFloatRange("BOOST_EXTRA_SPEED_LIMIT", 0.0f, 50.0f);
    EditIntRange("BOOST_TIME", 0, 1000);
    EditFloatRange("SFX_VOLUME", 0.0f, 1.0f);
    EditFloatRange("SFX_HORNPITCH", 0.0f, 1.0f);
}

// ---- the vehicle

// FUNC_AT(0x0006f900)
PVehicle* PVehicle::Construct(const char *name) {
    PhysicsObject::Construct("pvehicle", name, 1);
    missionEditorOnMask = 0;
    missionEditorOffMask = 0;
    vtable = reinterpret_cast<void **>(kPVehicleVtable);
    return this;
}

// FUNC_AT(0x00071000)
void PVehicle::MissionEditorSwitch(bool on, int which) {
    // The other mask is cleared whole: the original ands it with 0 << which
    if (on) {
        missionEditorOnMask |= 1u << which;
        missionEditorOffMask &= 0u << which;
    } else {
        missionEditorOnMask &= 0u << which;
        missionEditorOffMask |= 1u << which;
    }
}

// FUNC_AT(0x00071050)
PVehicle* PVehicle::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(PVehicle));
    return this;
}

// ---- the car name map

// FUNC_AT(0x00071070)
void CarNameMap::EraseSubtree(TreeNode *node) {
    Tree::EraseSubtree(node);
}

// FUNC_AT(0x000710b0)
TreeNode** CarNameMap::InsertAt(TreeNode **result, bool addLeft, TreeNode *where, const TreePair *value) {
    return Tree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x00071290)
TreeNode** CarNameMap::EraseAt(TreeNode **result, TreeNode *where) {
    return Tree::EraseAt(result, where);
}

// FUNC_AT(0x00071560)
TreeInsertResult* CarNameMap::InsertUnique(TreeInsertResult *result, const TreePair *value) {
    TreeNode *where = head;
    bool addLeft = true;
    for (TreeNode *node = head->parent; !node->isNil;) {
        where = node;
        addLeft = Crt_stricmp(value->name, node->value.name) < 0;
        node = addLeft ? node->left : node->right;
    }
    SharedTreeIterator before = {where};
    if (addLeft) {
        if (where == head->left) {
            TreeNode *inserted;
            Tree::InsertAt(&inserted, true, where, value);
            result->where = inserted;
            result->inserted = true;
            return result;
        }
        before.Dec();
    }
    if (Crt_stricmp(before.node->value.name, value->name) < 0) {
        TreeNode *inserted;
        Tree::InsertAt(&inserted, addLeft, where, value);
        result->where = inserted;
        result->inserted = true;
        return result;
    }
    result->where = before.node;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x00071630)
TreeNode** CarNameMap::EraseRange(TreeNode **result, TreeNode *first, TreeNode *last) {
    return Tree::EraseRange(result, first, last);
}

// FUNC_AT(0x000716f0)
void CarNameMap::Destroy() {
    Tree::Destroy();
}

int &CarNameMap::operator[](const char *name) {
    TreePair value = {};
    value.name = name;
    TreeInsertResult inserted;
    InsertUnique(&inserted, &value);
    return inserted.where->value.index;
}

// ---- the car names

// FUNC_AT(0x00071730)
void PVehicle::BuildCarNames() {
    fgCarNameCount = AttributeSystemInstance->CountClassNames("pvehicle") + 1;
    CarNameMap *map = static_cast<CarNameMap *>(OperatorNew(sizeof(CarNameMap)));
    if (map != NULL) {
        map->allocator = 0;     // the original copies an uninitialised local's byte; nothing reads it
        map->Init();
    }
    fgCarNameMap = map;
    fgCarNames = static_cast<const char **>(OperatorNewArray(fgCarNameCount * sizeof(const char *)));
    fgCarNames[0] = "default";
    (*fgCarNameMap)["default"] = 0;
    int index = 1;
    for (const char *name = AttributeSystemInstance->GetClassNextName("pvehicle", NULL); name != NULL;
         name = AttributeSystemInstance->GetClassNextName("pvehicle", name), index++) {
        fgCarNames[index] = name;
        (*fgCarNameMap)[name] = index;
    }
}

// FUNC_AT(0x00071880)
uint32_t PVehicle::GetNameCount() {
    if (fgCarNameMap == NULL)
        BuildCarNames();
    return fgCarNameCount;
}

// FUNC_AT(0x000718a0)
int PVehicle::NameToIndex(const char *name) {
    if (fgCarNameMap == NULL)
        BuildCarNames();
    return (*fgCarNameMap)[name];
}

// FUNC_AT(0x000718e0)
const char* const* PVehicle::GetCarNames() {
    if (fgCarNames == NULL)
        BuildCarNames();
    return fgCarNames;
}

// The list and the map are freed; their pointers are left as they were.
// FUNC_AT(0x00071900)
void PVehicle::Shutdown() {
    OperatorDelete(fgCarNames);
    CarNameMap *map = fgCarNameMap;
    if (map != NULL) {
        map->Destroy();
        OperatorDelete(map);
    }
}
