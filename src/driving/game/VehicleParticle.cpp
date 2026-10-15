#pragma fp_contract(off)

#include "VehicleParticle.h"

#include <bit>
#include <math.h>

#include "Simulation.h"
#include "Vehicle.h"                    // PVehicle, CarPhysics
#include "WeaponCameras.h"              // ParticleTextureRect
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "../data/AttributeContainers.h"   // StringSet: the shared node makers
#include "../data/DebugVariables.h"
#include "../data/Tree.h"               // TreeThrow
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../render/Particles.h"
#include "SimulationContainers.h"

// The system types it adds to the library (VehicleParticle.h)
// XBE_GLOBAL(0x001c50d8, 0xb0)
RParticleSystemData ParticleTypeSMRT = {
    0.0f, 0.015f, 0.0f, 30,
    0.0f, 0.0f, 0.0f, 0x8,
    0.96f, 0x736d6b35, 0, 0,   // 'smk5'
    0.0f, 0.2f, 0.7f, 60,
    60, 0, 0, -0.2f,
    0.0f, 180.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 0.25f, 0x3e800000,
    { { 0.0f, 512.0f, 512.0f, 512.0f }, { 32.0f, 512.0f, 512.0f, 512.0f }, { 0.0f, 512.0f, 512.0f, 512.0f } },
    0.0f, 0.0f, 0, 0,
};
// XBE_GLOBAL(0x001c5188, 0xb0)
RParticleSystemData ParticleTypeRDDF = {
    0.0f, 0.0f, 0.0f, 20,
    0.0f, 0.0f, 0.0f, 0x8,
    0.96f, 0x70756666, 0, 0,   // 'puff'
    0.0f, 0.3f, 0.8f, 20,
    20, 0, 0, -0.1f,
    0.01f, 180.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 1.0f, 0x3f800000,
    { { 80.0f, 55.0f, 46.75f, 26.95f }, { 80.0f, 102.3f, 95.700005f, 68.200005f }, { 0.0f, 102.3f, 95.700005f, 68.200005f } },
    0.0f, 0.0f, 0, 0,
};
// XBE_GLOBAL(0x001c5238, 0xb0)
RParticleSystemData ParticleTypeRDDR = {
    0.0f, 0.0f, 0.0f, 20,
    0.0f, 0.0f, 0.0f, 0x8,
    0.96f, 0x70756666, 0, 0,   // 'puff'
    0.0f, 0.2f, 1.5f, 15,
    15, 0, 0, -0.3f,
    0.01f, 180.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 1.0f, 0x3f800000,
    { { 80.0f, 55.0f, 46.75f, 26.95f }, { 40.0f, 102.3f, 95.700005f, 68.200005f }, { 0.0f, 102.3f, 95.700005f, 68.200005f } },
    0.0f, 0.0f, 0, 0,
};
// XBE_GLOBAL(0x001c52e8, 0xb0)
RParticleSystemData ParticleTypeWATR = {
    0.0f, 0.0f, 0.0f, 30,
    0.0f, 0.0f, 0.0f, 0x8,
    1.0f, 0x6772766c, 0, 0,   // 'grvl'
    0.0f, 0.2f, 0.7f, 80,
    80, 0, 0, 4.0f,
    0.0f, 180.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 1.0f, 0x3f800000,
    { { 128.0f, 128.0f, 128.0f, 128.0f }, { 128.0f, 128.0f, 128.0f, 128.0f }, { 0.0f, 128.0f, 128.0f, 128.0f } },
    0.0f, 0.0f, 0, 0,
};
// XBE_GLOBAL(0x001c5398, 0xb0)
RParticleSystemData ParticleTypeRDGR = {
    0.0f, 0.013f, 0.0f, 30,
    0.0f, 0.0f, 0.0f, 0x8,
    1.0f, 0x67726173, 0, 0,   // 'gras'
    0.0f, 0.4f, 0.5f, 60,
    60, 0, 0, 4.0f,
    0.0f, 180.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 1.0f, 0x3f800000,
    { { 64.0f, 35.0f, 66.0f, 20.0f }, { 128.0f, 35.0f, 66.0f, 20.0f }, { 0.0f, 35.0f, 66.0f, 20.0f } },
    0.0f, 0.0f, 0, 0,
};
// XBE_GLOBAL(0x001c5448, 0xb0)
RParticleSystemData ParticleTypeSNOW = {
    0.0f, 0.0f, 0.0f, 40,
    0.0f, 0.0f, 0.0f, 0x8,
    0.96f, 0x70756666, 0, 0,   // 'puff'
    0.0f, 0.5f, 1.5f, 15,
    15, 0, 0, -0.3f,
    0.01f, 180.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 1.0f, 0x3f800000,
    { { 60.0f, 180.0f, 180.0f, 220.0f }, { 40.0f, 180.0f, 180.0f, 220.0f }, { 0.0f, 180.0f, 180.0f, 220.0f } },
    0.0f, 0.0f, 0, 0,
};
// XBE_GLOBAL(0x001c55b0, 0xb0)
RParticleSystemData ParticleTypeSPRK = {
    0.0f, 0.013f, 0.0f, 40,
    0.1f, 0.005f, 0.1f, 0x22,
    1.0f, 0x736d6b35, 0, 0,   // 'smk5'
    0.0f, 0.0363f, 2.45f, 24,
    3, 1, 0, 30.0f,
    0.0f, 0.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 1.0f, 0x3f800000,
    { { 255.0f, 512.0f, 512.0f, 192.0f }, { 200.0f, 496.0f, 320.0f, 0.0f }, { 16.0f, 300.0f, 200.0f, 0.0f } },
    0.0f, 0.0f, 0, 0,
};
// XBE_GLOBAL(0x001c5660, 0xb0)
RParticleSystemData ParticleTypeSPKB = {
    0.0f, 0.013f, 0.0f, 60,
    0.1f, 0.005f, 0.1f, 0x22,
    1.0f, 0x736d6b35, 0, 0,   // 'smk5'
    0.0f, 0.035f, 2.6f, 24,
    3, 1, 0, 30.0f,
    0.0f, 0.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 1.0f, 0x3f800000,
    { { 255.0f, 512.0f, 512.0f, 192.0f }, { 200.0f, 496.0f, 320.0f, 0.0f }, { 16.0f, 300.0f, 200.0f, 0.0f } },
    0.0f, 0.0f, 0, 0,
};
// XBE_GLOBAL(0x001c5710, 0xb0)
RParticleSystemData ParticleTypeSPRL = {
    0.0f, 0.013f, 0.0f, 200,
    0.1f, 0.005f, 0.1f, 0x22,
    1.0f, 0x736d6b35, 0, 0,   // 'smk5'
    0.0f, 0.0363f, 2.45f, 24,
    3, 1, 0, 30.0f,
    0.0f, 0.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 1.0f, 0x3f800000,
    { { 255.0f, 512.0f, 512.0f, 192.0f }, { 200.0f, 496.0f, 320.0f, 0.0f }, { 16.0f, 300.0f, 200.0f, 0.0f } },
    0.0f, 0.0f, 0, 0,
};

// ---------------------------------------------------------------------------------------------------------------
// RVehicleParticle and its set (0x000a6db0-0x000a8460), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// The set's rotations and its iterator's decrement are the code every set with 0x14-byte nodes shares
// (SimulationContainers.h)
DeadObjectSet *SetCode(VehicleParticleSet *set) {
    return reinterpret_cast<DeadObjectSet *>(set);
}

}  // namespace

// ---- globals
extern uint32_t GameTick;
extern const char *OffOnNames[2];                                // "Off", "On"

// XBE_GLOBAL(0x00201834, 4)
VehicleParticleSet *fgVehicleParticles;
// XBE_GLOBAL(0x00201838, 4)
uint32_t VehicleParticleUpdateTick;
// XBE_GLOBAL(0x001c50d0, 1)
bool VehicleSparksEnabled = true;
#if !NIGHTFIRE_X87
// The objects UpdateCar put in the set since the last UpdateAll, in that order (ours; see UpdateAll). Past
// kMaxInOrder, UpdateAll falls back to the set's order.
static const int kMaxInOrder = 128;
static RVehicleParticle *UpdatedInOrder[kMaxInOrder];
static int UpdatedCount;
static bool UpdatedOverflow;

static void NoteUpdated(RVehicleParticle *object) {
    if (UpdatedCount < kMaxInOrder)
        UpdatedInOrder[UpdatedCount++] = object;
    else
        UpdatedOverflow = true;
}

static void ForgetUpdated(RVehicleParticle *object) {
    for (int n = 0; n < UpdatedCount; n++)
        if (UpdatedInOrder[n] == object) {
            for (UpdatedCount--; n < UpdatedCount; n++)
                UpdatedInOrder[n] = UpdatedInOrder[n + 1];
            return;
        }
}
#endif

namespace {

// System type tags ('SMRT' is 0x534d5254)
constexpr uint32_t kTagSMRT = 0x534d5254;
constexpr uint32_t kTagRDDF = 0x52444446;
constexpr uint32_t kTagRDDR = 0x52444452;
constexpr uint32_t kTagRDGR = 0x52444752;
constexpr uint32_t kTagSNOW = 0x534e4f57;
constexpr uint32_t kTagSPRK = 0x5350524b;
constexpr uint32_t kTagWATR = 0x57415452;
constexpr uint32_t kTagSPRL = 0x5350524c;
constexpr uint32_t kTagSPKB = 0x53504b42;

// Each system's type as constructed
const uint32_t kSystemTags[RVehicleParticle::kSystemCount] = {
    kTagSMRT, kTagSMRT, kTagSMRT, kTagSMRT,
    kTagRDDF, kTagRDDF, kTagRDDR, kTagRDDR,
    kTagSPRK, kTagSPRK, kTagSPRK, kTagSPRK, kTagSPRK,
    kTagSPRL,
};

// The dirt a wheel throws up on each road surface (GetWheelRoadSurface's low four bits); 0 none, and tyre smoke
const uint32_t kSurfaceDirt[16] = {
    0, 0, kTagRDDR, kTagRDGR, 0, kTagRDDR, kTagWATR, 0, 0, kTagSNOW, 0, 0, 0, 0, 0, 0,
};

// The surfaces a shredded tyre's rim sparks on
bool SurfaceSparks(int surface) {
    return surface == 1 || surface == 2 || surface == 4 || surface == 10 || surface == 12;
}

constexpr int kNoBody = 0xff;
constexpr int32_t kKeepAlive = 30;
constexpr float kRefreshed = 10.0f;                 // RParticleSystem::unknown34 while a vehicle drives the system
constexpr float kSparkLift = 0.2f;                  // a spark's upward speed by its larger sideways speed
constexpr float kSteepness = 0.1f;
constexpr float kSparkHeight = 0.0f;                // a static of the original's that nothing writes (0x0020183c):
                                                    // adding it turns a -0 height into +0
constexpr float kSmokeSlip = 0.2f;                  // the slip at which tyre smoke starts ...
constexpr float kSmokeRate = 4.0f;                  // ... and the rate it grows with over that
constexpr float kSmokeBaseRate = 0.1f;
constexpr float kDirtSlip = 0.3f;
constexpr float kDirtRise = 0.8f;
constexpr float kDirtBack = 3.66f;                  // the dirt's speed backwards along the car
constexpr float kSnowSpeed = 0.1f;                  // snow spray by speed, up to 1
constexpr float kRoadDirtSpeed = 0.0714285746f;     // road dirt by speed, up to 2
constexpr float kRoadDirtMax = 2.0f;
constexpr float kShreddedSpeedSquared = 5.0f;
constexpr float kShreddedSparkSpeed = 2.0f;
constexpr float kTireSmokeStrength = 2.0f;
static_assert(std::bit_cast<uint32_t>(kSparkLift) == 0x3e4ccccd && std::bit_cast<uint32_t>(kSteepness) == 0x3dcccccd &&
              std::bit_cast<uint32_t>(kDirtSlip) == 0x3e99999a && std::bit_cast<uint32_t>(kDirtRise) == 0x3f4ccccd &&
              std::bit_cast<uint32_t>(kDirtBack) == 0x406a3d71 &&
              std::bit_cast<uint32_t>(kRoadDirtSpeed) == 0x3d924925, "the original's constants");

const Coord3 kSideX = {1.0f, 0.0f, 0.0f};
const Coord3 kUp = {0.0f, 1.0f, 0.0f};
const Coord3 kForward = {0.0f, 0.0f, 1.0f};

RigidBody *VehicleBody(PVehicle *vehicle) {
    return fgSimulation.GetRigidBody(vehicle->rigidBodySlot);
}

// The emitter's body: the vehicle's rigid body slot, or none
int32_t EmitterBody(PVehicle *vehicle) {
    int32_t slot = vehicle->rigidBodySlot;
    return slot > -1 ? slot : kNoBody;
}

// The node makers are the string set's: the same code for any four-byte value
VehicleParticleNode *BuyNode(VehicleParticleSet *set, VehicleParticleNode *left, VehicleParticleNode *parent, VehicleParticleNode *right,
                             RVehicleParticle *const &value) {
    return reinterpret_cast<VehicleParticleNode *>(reinterpret_cast<StringSet *>(set)->Buynode(
        reinterpret_cast<StringNode *>(left), reinterpret_cast<StringNode *>(parent),
        reinterpret_cast<StringNode *>(right), reinterpret_cast<const char *const &>(value), kTreeRed));
}

VehicleParticleNode *BuyHead() {
    return reinterpret_cast<VehicleParticleNode *>(StringSet::BuyHead());
}

// ++iterator, inlined
VehicleParticleNode *Next(VehicleParticleNode *node) {
    if (node->isNil)
        return node;
    if (!node->right->isNil)
        return VehicleParticleSet::Min(node->right);
    VehicleParticleNode *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

}  // namespace

// =============================================================================================================
// The set
// =============================================================================================================

// FUNC_AT(0x000a6f30)
VehicleParticleNode* VehicleParticleSet::Max(VehicleParticleNode *node) {
    while (!node->right->isNil)
        node = node->right;
    return node;
}

// FUNC_AT(0x000a6f50)
VehicleParticleNode* VehicleParticleSet::Min(VehicleParticleNode *node) {
    while (!node->left->isNil)
        node = node->left;
    return node;
}

// FUNC_AT(0x000a72c0)
void VehicleParticleSet::EraseSubtree(VehicleParticleNode *node) {
    for (VehicleParticleNode *next; !node->isNil; node = next) {
        EraseSubtree(node->right);
        next = node->left;
        if (node != NULL)
            UMemory::FastFree(node, sizeof(VehicleParticleNode));
    }
}

// FUNC_AT(0x000a7350)
VehicleParticleSet::Range* VehicleParticleSet::EqualRange(Range *result, RVehicleParticle *const &key) {
    VehicleParticleNode *upper = head;
    for (VehicleParticleNode *node = Root(); !node->isNil;) {
        if (key < node->value) {
            upper = node;
            node = node->left;
        } else {
            node = node->right;
        }
    }
    VehicleParticleNode *lower = head;
    for (VehicleParticleNode *node = Root(); !node->isNil;) {
        if (node->value < key) {
            node = node->right;
        } else {
            lower = node;
            node = node->left;
        }
    }
    result->last.node = upper;
    result->first.node = lower;
    return result;
}

// FUNC_AT(0x000a73c0)
VehicleParticleSet::Iterator* VehicleParticleSet::Erase(Iterator *result, Iterator where) {
    if (where.node->isNil)
        TreeThrow("invalid map/set<T> iterator", kOutOfRangeVtable, kOutOfRangeThrowInfo);
    VehicleParticleNode *erased = where.node;
    where.node = Next(where.node);

    VehicleParticleNode *pnode = erased;    // the node that really leaves its place: erased, or its successor
    VehicleParticleNode *fixnode;           // the node that takes pnode's place
    VehicleParticleNode *fixnodeParent;
    if (erased->left->isNil) {
        fixnode = erased->right;
    } else if (erased->right->isNil) {
        fixnode = erased->left;
    } else {
        pnode = where.node;
        fixnode = pnode->right;
    }

    if (pnode == erased) {
        fixnodeParent = erased->parent;
        if (!fixnode->isNil)
            fixnode->parent = fixnodeParent;
        if (head->parent == erased)
            head->parent = fixnode;
        else if (fixnodeParent->left == erased)
            fixnodeParent->left = fixnode;
        else
            fixnodeParent->right = fixnode;
        if (head->left == erased)
            head->left = fixnode->isNil ? fixnodeParent : Min(fixnode);
        if (head->right == erased)
            head->right = fixnode->isNil ? fixnodeParent : Max(fixnode);
    } else {
        erased->left->parent = pnode;
        pnode->left = erased->left;
        if (pnode == erased->right) {
            fixnodeParent = pnode;
        } else {
            fixnodeParent = pnode->parent;
            if (!fixnode->isNil)
                fixnode->parent = fixnodeParent;
            fixnodeParent->left = fixnode;
            pnode->right = erased->right;
            erased->right->parent = pnode;
        }
        if (head->parent == erased)
            head->parent = pnode;
        else if (erased->parent->left == erased)
            erased->parent->left = pnode;
        else
            erased->parent->right = pnode;
        pnode->parent = erased->parent;
        uint8_t color = pnode->color;
        pnode->color = erased->color;
        erased->color = color;
    }

    if (erased->color == kTreeBlack) {
        for (; fixnode != head->parent && fixnode->color == kTreeBlack;
             fixnode = fixnodeParent, fixnodeParent = fixnodeParent->parent) {
            if (fixnode == fixnodeParent->left) {
                VehicleParticleNode *sibling = fixnodeParent->right;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixnodeParent->color = kTreeRed;
                    SetCode(this)->Lrotate(reinterpret_cast<DeadObjectNode *>(fixnodeParent));
                    sibling = fixnodeParent->right;
                }
                if (sibling->isNil)
                    continue;
                if (sibling->left->color == kTreeBlack && sibling->right->color == kTreeBlack) {
                    sibling->color = kTreeRed;
                    continue;
                }
                if (sibling->right->color == kTreeBlack) {
                    sibling->left->color = kTreeBlack;
                    sibling->color = kTreeRed;
                    SetCode(this)->Rrotate(reinterpret_cast<DeadObjectNode *>(sibling));
                    sibling = fixnodeParent->right;
                }
                sibling->color = fixnodeParent->color;
                fixnodeParent->color = kTreeBlack;
                sibling->right->color = kTreeBlack;
                SetCode(this)->Lrotate(reinterpret_cast<DeadObjectNode *>(fixnodeParent));
                break;
            } else {
                VehicleParticleNode *sibling = fixnodeParent->left;
                if (sibling->color == kTreeRed) {
                    sibling->color = kTreeBlack;
                    fixnodeParent->color = kTreeRed;
                    SetCode(this)->Rrotate(reinterpret_cast<DeadObjectNode *>(fixnodeParent));
                    sibling = fixnodeParent->left;
                }
                if (sibling->isNil)
                    continue;
                if (sibling->right->color == kTreeBlack && sibling->left->color == kTreeBlack) {
                    sibling->color = kTreeRed;
                    continue;
                }
                if (sibling->left->color == kTreeBlack) {
                    sibling->right->color = kTreeBlack;
                    sibling->color = kTreeRed;
                    SetCode(this)->Lrotate(reinterpret_cast<DeadObjectNode *>(sibling));
                    sibling = fixnodeParent->left;
                }
                sibling->color = fixnodeParent->color;
                fixnodeParent->color = kTreeBlack;
                sibling->left->color = kTreeBlack;
                SetCode(this)->Rrotate(reinterpret_cast<DeadObjectNode *>(fixnodeParent));
                break;
            }
        }
        fixnode->color = kTreeBlack;
    }

    UMemory::FastFree(erased, sizeof(VehicleParticleNode));
    if (size > 0)
        size--;
    *result = where;
    return result;
}

// FUNC_AT(0x000a7690)
VehicleParticleSet::Iterator* VehicleParticleSet::Insert(Iterator *result, bool addLeft, VehicleParticleNode *where,
                                                         RVehicleParticle *const &value) {
    if (size >= 0x3ffffffe)   // max_size() - 1
        TreeThrow("map/set<T> too long", kLengthErrorVtable, kLengthErrorThrowInfo);
    VehicleParticleNode *node = BuyNode(this, head, where, head, value);
    size++;
    if (where == head) {
        head->parent = node;
        head->left = node;
        head->right = node;
    } else if (addLeft) {
        where->left = node;
        if (where == head->left)
            head->left = node;
    } else {
        where->right = node;
        if (where == head->right)
            head->right = node;
    }

    for (VehicleParticleNode *x = node; x->parent->color == kTreeRed;) {
        VehicleParticleNode *parent = x->parent;
        VehicleParticleNode *grandparent = parent->parent;
        if (parent == grandparent->left) {
            VehicleParticleNode *uncle = grandparent->right;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->right) {
                    x = parent;
                    SetCode(this)->Lrotate(reinterpret_cast<DeadObjectNode *>(x));
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                SetCode(this)->Rrotate(reinterpret_cast<DeadObjectNode *>(x->parent->parent));
            }
        } else {
            VehicleParticleNode *uncle = grandparent->left;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->left) {
                    x = parent;
                    SetCode(this)->Rrotate(reinterpret_cast<DeadObjectNode *>(x));
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                SetCode(this)->Lrotate(reinterpret_cast<DeadObjectNode *>(x->parent->parent));
            }
        }
    }
    head->parent->color = kTreeBlack;
    result->node = node;
    return result;
}

// FUNC_AT(0x000a7870)
VehicleParticleSet::InsertResult* VehicleParticleSet::InsertUnique(InsertResult *result,
                                                                   RVehicleParticle *const &value) {
    VehicleParticleNode *where = head;
    bool addLeft = true;
    for (VehicleParticleNode *node = Root(); !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = value < node->value;
    }
    Iterator at = {where};
    if (addLeft) {
        if (where == Begin()) {
            Iterator inserted;
            result->position = *Insert(&inserted, true, where, value);
            result->inserted = true;
            return result;
        }
        reinterpret_cast<DeadObjectSet::Iterator *>(&at)->Dec();
    }
    if (at.node->value < value) {
        Iterator inserted;
        result->position = *Insert(&inserted, addLeft, where, value);
        result->inserted = true;
        return result;
    }
    result->position = at;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x000a7930)
VehicleParticleSet::Iterator* VehicleParticleSet::EraseRange(Iterator *result, Iterator first, Iterator last) {
    if (first.node == Begin() && last.node == head) {
        EraseSubtree(Root());
        head->parent = head;
        size = 0;
        head->left = head;
        head->right = head;
        result->node = Begin();
        return result;
    }
    while (first.node != last.node) {
        Iterator erased = first;
        first.node = Next(first.node);
        Iterator next;
        Erase(&next, erased);
    }
    *result = first;
    return result;
}

// FUNC_AT(0x000a81e0)
void VehicleParticleSet::Destruct() {
    Iterator erased;
    EraseRange(&erased, Iterator{Begin()}, Iterator{head});
    if (head != NULL)
        UMemory::FastFree(head, sizeof(VehicleParticleNode));
    head = NULL;
    size = 0;
}

uint32_t VehicleParticleSet::EraseKey(RVehicleParticle *key) {
    Range range;
    EqualRange(&range, key);
    uint32_t count = 0;
    for (VehicleParticleNode *node = range.first.node; node != range.last.node; node = Next(node))
        count++;
    Iterator erased;
    EraseRange(&erased, range.first, range.last);
    return count;
}

// =============================================================================================================
// RVehicleParticle
// =============================================================================================================

// FUNC_AT(0x000a6db0)
void RVehicleParticle::LoadAttributes() {
    dbattrib_bool("Enable vehicle sparks", &VehicleSparksEnabled, 0, 1, 0, -1.0f, OffOnNames);
}

// FUNC_AT(0x000a6de0)
RVehicleParticle* RVehicleParticle::Construct(PVehicle *vehicle) {
    vtable = kRVehicleParticleVtable;
    this->vehicle = vehicle;
    smokeEndTick = 0;
    smokeFade = 1.0f;
    emitters = static_cast<RParticleEmitter *>(OperatorNewArray(kSystemCount * sizeof(RParticleEmitter)));
    for (int i = 0; i < kSystemCount; i++) {
        RParticleEmitter &emitter = emitters[i];
        emitter.systemTag = kSystemTags[i];
        emitter.side = kSideX;
        emitter.direction = kUp;
        RParticleSystem *system = static_cast<RParticleSystem *>(UMemory::FastAlloc(sizeof(RParticleSystem),
                                                                                    "RParticleSystem"));
        if (system != NULL)
            system = system->Construct(0.0f, &emitter, true);
        systems[i] = system;
        system->active = 0;
        emitter.body = kNoBody;
    }
    return this;
}

// FUNC_AT(0x000a6f10)
void RVehicleParticle::TriggerTireSmoke(int ticks) {
    smokeEndTick = GameTick + ticks;
    smokeFade = float(kTireSmokeStrength / double(ticks));
}

// FUNC_AT(0x000a6f70)
void RVehicleParticle::TriggerSparks(const Coord3 *position, Coord3 velocity, float, int system) {
    if (!VehicleSparksEnabled)
        return;
    Coord3 direction = {velocity.x * -1.0f, velocity.y * -1.0f, velocity.z * -1.0f};
    RParticleEmitter &emitter = emitters[system];
    emitter.body = EmitterBody(vehicle);

    const RigidBody *body = VehicleBody(vehicle);
    emitter.position.x = position->x - body->position.x;
    emitter.position.y = float(double(position->y) - body->position.y + kSparkHeight);
    emitter.position.z = position->z - body->position.z;

    float sideways = fabsf(direction.z);
    float forwards = fabsf(direction.x);
    direction.y = float(double(forwards < sideways ? sideways : forwards) * kSparkLift + direction.y);
    emitter.direction = direction;

    // Thrown nearly straight up or down: the cross products take other axes
    Coord3 side = kUp;
    bool steepX = fabsf(direction.x) < kSteepness || fabs(double(direction.y) / direction.x) > 1.0;
    bool steepZ = fabsf(direction.z) < kSteepness || fabs(double(direction.y) / direction.z) > 1.0;
    if (steepX && steepZ) {
        side = kSideX;
        if (direction.y < kSteepness)
            direction = kForward;
    }
    VU0_v4unitcrossprodxyz(&side, &direction, &emitter.side);
    if ((emitter.side.x == 0.0f && emitter.side.y == 0.0f && emitter.side.z == 0.0f) ||
        (direction.x == 0.0f && direction.y == 0.0f && direction.z == 0.0f)) {
        emitter.side = kSideX;
        direction = kUp;
    }

    RParticleSystem *sparks = systems[system];
    VU0_v4unitcrossprodxyz(&emitter.side, &direction, &sparks->axis);
    sparks->active = 1;
    sparks->time = 0;
    sparks->keepAlive = kKeepAlive;
    sparks->unknown34 = kRefreshed;
}

// FUNC_AT(0x000a7300)
void RVehicleParticle::ResetAll() {
    for (VehicleParticleNode *node = fgVehicleParticles->Begin(); node != fgVehicleParticles->End(); node = Next(node))
        node->value->smokeEndTick = 0;
}

// FUNC_AT(0x000a79f0)
void RVehicleParticle::Destruct() {
    vtable = kRVehicleParticleVtable;
    for (int i = 0; i < kSystemCount; i++)
        if (systems[i] != NULL)
            systems[i]->vtable->destroy(systems[i], METHOD_EDX_ARG 1);
    OperatorDelete(emitters);
    emitters = NULL;
    fgVehicleParticles->EraseKey(this);
#if !NIGHTFIRE_X87
    ForgetUpdated(this);
#endif
}

// FUNC_AT(0x000a7ac0)
void RVehicleParticle::UpdateCar(bool active) {
    if (!active) {
        for (int i = 0; i < kSystemCount; i++)
            systems[i]->active = 0;
    } else {
        for (int i = kSparks; i < kSparkLine; i++)
            emitters[i].body = EmitterBody(vehicle);

        const Coord3 *velocity = &VehicleBody(vehicle)->velocity;
        const RigidBodyInfo *info = VehicleBody(vehicle)->info;
        for (int wheel = 0; wheel < kWheelCount; wheel++) {
            Coord4 point = VehicleBody(vehicle)->info->worldLevers[wheel];
            Coord4 centre = {VehicleBody(vehicle)->position.x, VehicleBody(vehicle)->position.y,
                             VehicleBody(vehicle)->position.z, 1.0f};
            VU0_v3add(&point, &centre, &point);

            int surface = vehicle->GetWheelRoadSurface(wheel) & 0xf;
            if (SurfaceSparks(surface) && vehicle->IsTyreShredded(int8_t(wheel)) &&
                vehicle->IsWheelOnGround(int8_t(wheel)) && VU0_v3lengthsquare(velocity) > kShreddedSpeedSquared) {
                Coord3 at = {point.x, point.y, point.z};
                Coord3 thrown = {kShreddedSparkSpeed * velocity->x, kShreddedSparkSpeed * velocity->y,
                                 kShreddedSparkSpeed * velocity->z};
                TriggerSparks(&at, thrown, 1.0f, kWheelSparks + wheel);
            }

            // Tyre smoke on a surface without dirt, by the wheel's slip (or TriggerTireSmoke's, while it lasts)
            uint32_t dirt = kSurfaceDirt[surface];
            RParticleSystem *smoke = systems[kTyreSmoke + wheel];
            float slip = vehicle->GetCarWheelSlip(wheel);
            if (GameTick < smokeEndTick) {
                float extra = float(double(smokeEndTick - GameTick) * smokeFade);
                if (slip < extra)
                    slip = extra;
            }
            if (dirt == 0 && slip >= kSmokeSlip) {
                if (slip > 1.0f)
                    slip = 1.0f;
                RParticleEmitter &emitter = emitters[kTyreSmoke + wheel];
                emitter.position = point;
                smoke->active = 1;
                smoke->keepAlive = kKeepAlive;
                smoke->unknown34 = kRefreshed;
                smoke->rateScale = float((double(slip) - kSmokeSlip) * kSmokeRate + kSmokeBaseRate);
                emitter.direction.x = velocity->x;
                emitter.direction.y = velocity->y + kDirtRise;
                emitter.direction.z = velocity->z;
            } else {
                smoke->active = 0;
            }

            // Dirt, by the slip; snow and road dirt by the speed instead while the wheel is on the ground
            RParticleSystem *dirtSystem = systems[kWheelDirt + wheel];
            bool throws = true;
            if (dirt == kTagSNOW) {
                if (vehicle->IsWheelOnGround(int8_t(wheel))) {
                    double spray = vehicle->GetCarSpeed() * double(kSnowSpeed);
                    float amount = spray < 1.0 ? float(spray) : 1.0f;
                    slip = 0.0f < amount ? amount : 0.0f;
                    if (vehicle->GetPhysics()->isSnowmobile != 0)
                        throws = false;
                }
            } else if (dirt == kTagRDDR) {
                if (vehicle->IsWheelOnGround(int8_t(wheel))) {
                    double spray = vehicle->GetCarSpeed() * double(kRoadDirtSpeed);
                    float amount = spray < kRoadDirtMax ? float(spray) : kRoadDirtMax;
                    slip = 0.0f < amount ? amount : 0.0f;
                }
            } else if (dirt == 0) {
                throws = false;
            }
            if (throws && slip >= kDirtSlip) {
                if (slip > 1.0f && dirt != kTagRDDR)
                    slip = 1.0f;
                RParticleEmitter &emitter = emitters[kWheelDirt + wheel];
                emitter.position = point;
                dirtSystem->rateScale = slip;
                dirtSystem->SetSystem(dirt);
                dirtSystem->active = 1;
                dirtSystem->keepAlive = kKeepAlive;
                dirtSystem->unknown34 = kRefreshed;
                emitter.direction.x = float(velocity->x - double(info->orientation.mtx[2][0]) * kDirtBack);
                emitter.direction.y = velocity->y + kDirtRise;
                emitter.direction.z = float(velocity->z - double(info->orientation.mtx[2][2]) * kDirtBack);
            } else {
                dirtSystem->active = 0;
            }
        }
    }
    VehicleParticleSet::InsertResult inserted;
    fgVehicleParticles->InsertUnique(&inserted, this);
#if !NIGHTFIRE_X87
    if (inserted.inserted)
        NoteUpdated(this);
#endif
}

// FUNC_AT(0x000a8120)
void RVehicleParticle::UpdateAll() {
    if (fgSimulation.stepCount == VehicleParticleUpdateTick)
        return;
    VehicleParticleUpdateTick = GameTick;   // the game tick, though it is compared with the step count
#if NIGHTFIRE_X87
    VehicleParticleSet *set = fgVehicleParticles;
    for (VehicleParticleNode *node = set->Begin(); node != fgVehicleParticles->End(); node = Next(node))
        for (int i = 0; i < kSystemCount; i++) {
            RParticleSystem *system = node->value->systems[i];
            system->vtable->update(system, METHOD_EDX_ARG 1);
        }
#else
    // Deliberately not the set's order, the objects' addresses, which spawns their particles into the shared cache
    // in the heap's order: the order UpdateCar put them there (docs/driving/original-quirks.md).
    if (!UpdatedOverflow) {
        for (int n = 0; n < UpdatedCount; n++)
            for (int i = 0; i < kSystemCount; i++) {
                RParticleSystem *system = UpdatedInOrder[n]->systems[i];
                system->vtable->update(system, METHOD_EDX_ARG 1);
            }
    } else {
        for (VehicleParticleNode *node = fgVehicleParticles->Begin(); node != fgVehicleParticles->End();
             node = Next(node))
            for (int i = 0; i < kSystemCount; i++) {
                RParticleSystem *system = node->value->systems[i];
                system->vtable->update(system, METHOD_EDX_ARG 1);
            }
    }
    UpdatedCount = 0;
    UpdatedOverflow = false;
#endif
    VehicleParticleSet::Iterator erased;
    fgVehicleParticles->EraseRange(&erased, VehicleParticleSet::Iterator{fgVehicleParticles->Begin()},
                                   VehicleParticleSet::Iterator{fgVehicleParticles->End()});
}

// FUNC_AT(0x000a8220)
RVehicleParticle* RVehicleParticle::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RVehicleParticle));
    return this;
}

// FUNC_AT(0x000a8240)
void RVehicleParticle::Init() {
    VehicleParticleSet *set = static_cast<VehicleParticleSet *>(OperatorNew(sizeof(VehicleParticleSet)));
    if (set != NULL) {
        set->allocator = 0;     // (the original copies an uninitialised byte)
        set->head = BuyHead();
        set->head->isNil = 1;
        set->head->parent = set->head;
        set->head->left = set->head;
        set->head->right = set->head;
        set->size = 0;
    }
    fgVehicleParticles = set;

    RParticleSystemData *textured[] = {
        &ParticleTypeSMRT, &ParticleTypeRDDF, &ParticleTypeRDDR, &ParticleTypeRDGR, &ParticleTypeSNOW,
        &ParticleTypeWATR,
    };
    for (RParticleSystemData *type : textured)
        ParticleTextureRect(type->texture, &type->u);

    struct { RParticleSystemData *type; uint32_t tag; } added[] = {
        {&ParticleTypeSMRT, kTagSMRT}, {&ParticleTypeRDDF, kTagRDDF}, {&ParticleTypeRDDR, kTagRDDR},
        {&ParticleTypeRDGR, kTagRDGR}, {&ParticleTypeSNOW, kTagSNOW}, {&ParticleTypeSPRK, kTagSPRK},
        {&ParticleTypeWATR, kTagWATR}, {&ParticleTypeSPRL, kTagSPRL}, {&ParticleTypeSPKB, kTagSPKB},
    };
    for (auto &entry : added)
        entry.type->index = fgParticleSystems->library.AddSystem(entry.type, entry.tag);
    VehicleSparksEnabled = true;
}

// FUNC_AT(0x000a8430)
void RVehicleParticle::Kill() {
    if (fgVehicleParticles != NULL) {
        fgVehicleParticles->Destruct();
        OperatorDelete(fgVehicleParticles);
        fgVehicleParticles = NULL;
#if !NIGHTFIRE_X87
        UpdatedCount = 0;
        UpdatedOverflow = false;
#endif
    }
}
