#ifndef DRIVING_GAME_VEHICLEPARTICLE_H_
#define DRIVING_GAME_VEHICLEPARTICLE_H_

#include <stddef.h>
#include "../Vtables.h"   // our copies of the game's vtables
#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../engine/RbTree.h"
#include "../../common/xbeClass.h"      // LAYOUT_32

// ---------------------------------------------------------------------------------------------------------------
// RVehicleParticle (0x4c bytes, vtable 0x001930b0: the deleting destructor only): a car's particle systems - tyre
// smoke and dirt at each wheel, sparks, one more spark system - each following an emitter of its own. RVehicle makes
// one for its vehicle (RVehicle +0x358) and calls UpdateCar as it renders; UpdateCar puts the object in a set, and
// UpdateAll steps the systems of every object in it once a simulation step and empties it. See VehicleParticle.cpp.
// ---------------------------------------------------------------------------------------------------------------

class RParticleSystem;
class RVehicleParticle;
struct PVehicle;
struct RParticleEmitter;
struct RParticleSystemData;

// The set of objects UpdateCar has seen since the last UpdateAll: std::set<RVehicleParticle *> as the game compiled
// it (engine/RbTree.h's layout; the name is ours). Its node helpers are shared with the program's other trees of
// 0x14-byte nodes: making a node or the head is the attribute system's string set's (0x00053500, 0x00053640), the
// rotations and the iterator's decrement the Simulation's (called at their addresses), and the minimum and maximum
// here are called by the others.
struct VehicleParticleNode : RbTreeNode<VehicleParticleNode, RVehicleParticle *> {};
LAYOUT_32(sizeof(VehicleParticleNode) == 0x14, "a set node is 0x14 bytes");

struct VehicleParticleSet : RbTree<VehicleParticleNode> {
    struct Iterator {
        VehicleParticleNode *node;
    };
    struct InsertResult {
        Iterator position;
        bool inserted;
        uint8_t unknown05[3];
    };
    struct Range {
        Iterator first;
        Iterator last;
    };

    static VehicleParticleNode* Max(VehicleParticleNode *node);                                 // 0x000a6f30
    static VehicleParticleNode* Min(VehicleParticleNode *node);                                 // 0x000a6f50
    // _Erase: a subtree freed, without rebalancing
    void EraseSubtree(VehicleParticleNode *node);                                               // 0x000a72c0
    Range* EqualRange(Range *result, RVehicleParticle *const &key);                             // 0x000a7350
    Iterator* Erase(Iterator *result, Iterator where);                                          // 0x000a73c0
    // _Insert: a new red node under `where`, on its left if `addLeft`, then rebalanced
    Iterator* Insert(Iterator *result, bool addLeft, VehicleParticleNode *where,
                     RVehicleParticle *const &value);                                           // 0x000a7690
    InsertResult* InsertUnique(InsertResult *result, RVehicleParticle *const &value);           // 0x000a7870
    Iterator* EraseRange(Iterator *result, Iterator first, Iterator last);                      // 0x000a7930
    // The destructor: everything erased, the head freed
    void Destruct();                                                                            // 0x000a81e0

    // erase(key), inlined in RVehicleParticle's destructor: the number erased
    uint32_t EraseKey(RVehicleParticle *key);
};
LAYOUT_32(sizeof(VehicleParticleSet) == 0xc, "a set is 12 bytes");

class RVehicleParticle {
public:
    // The systems, by index (each starts as the type its tag names, kSystemTags in VehicleParticle.cpp)
    enum System {
        kTyreSmoke = 0,                 // 0-3 a wheel's, 'SMRT'
        kWheelDirt = 4,                 // 4-7 a wheel's: 'RDDF', 'RDDR', then by surface (UpdateCar)
        kSparks = 8,                    // 8-12 'SPRK': 8 RVehicle::TriggerFX's, 9-12 a shredded tyre's
        kWheelSparks = 9,
        kSparkLine = 13,                // 'SPRL'
        kSystemCount = 14,
    };
    static const int kWheelCount = 4;

    const void *vtable;                         // +0x00
    RParticleEmitter *emitters;                 // +0x04 kSystemCount, new[]: positions relative to the car's body
                                                //       (TriggerSparks), or in the world with w 1
    RParticleSystem *systems[kSystemCount];     // +0x08 each following its emitter
    PVehicle *vehicle;                          // +0x40
    uint32_t smokeEndTick;                      // +0x44 the game tick extra tyre smoke lasts until (TriggerTireSmoke)
    float smokeFade;                            // +0x48 that smoke's strength by the ticks left

    RVehicleParticle* Construct(PVehicle *vehicle);                                             // 0x000a6de0
    void Destruct();                                                                            // 0x000a79f0
    RVehicleParticle* Delete(unsigned flags);       // vtable slot 0                             // 0x000a8220

    // Tyre smoke at every wheel for `ticks` game ticks, as strong as a slip of 2 falling to 0
    void TriggerTireSmoke(int ticks);                                                           // 0x000a6f10
    // Sparks from system `system` at `position` (in the world), thrown along `velocity`; `unknown` is not read
    void TriggerSparks(const Coord3 *position, Coord3 velocity, float unknown, int system);     // 0x000a6f70
    // The wheels' smoke, dirt and sparks for this frame (or every system stopped without `active`), and the object
    // put in the set for UpdateAll
    void UpdateCar(bool active);                                                                // 0x000a7ac0

    static void LoadAttributes();                                                               // 0x000a6db0
    // The set made, the system types' textures set and the types added to the library
    static void Init();                                                                         // 0x000a8240
    static void Kill();                                                                         // 0x000a8430
    // Every system of every object in the set stepped one tick, once a simulation step; the set emptied
    static void UpdateAll();                                                                    // 0x000a8120
    // The extra tyre smoke of every object in the set ended
    static void ResetAll();                                                                     // 0x000a7300
};
LAYOUT_32(sizeof(RVehicleParticle) == 0x4c, "RVehicleParticle is 0x4c bytes");
LAYOUT_32(offsetof(RVehicleParticle, systems) == 0x08 && offsetof(RVehicleParticle, vehicle) == 0x40 &&
          offsetof(RVehicleParticle, smokeEndTick) == 0x44 && offsetof(RVehicleParticle, smokeFade) == 0x48,
          "RVehicleParticle layout");

void **const kRVehicleParticleVtable = Vtable_RVehicleParticle;

extern VehicleParticleSet *fgVehicleParticles;  // Init's set (the name is ours)
extern uint32_t VehicleParticleUpdateTick;      // UpdateAll's last tick (the name is ours)
extern bool VehicleSparksEnabled;               // "Enable vehicle sparks"

// The system types RVehicleParticle adds to the library, by their tags (render/Particles.h's RParticleSystemData)
extern RParticleSystemData ParticleTypeSMRT;
extern RParticleSystemData ParticleTypeRDDF;
extern RParticleSystemData ParticleTypeRDDR;
extern RParticleSystemData ParticleTypeWATR;
extern RParticleSystemData ParticleTypeRDGR;
extern RParticleSystemData ParticleTypeSNOW;
extern RParticleSystemData ParticleTypeSPRK;
extern RParticleSystemData ParticleTypeSPKB;
extern RParticleSystemData ParticleTypeSPRL;

#endif // DRIVING_GAME_VEHICLEPARTICLE_H_
