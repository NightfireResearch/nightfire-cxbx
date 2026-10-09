#ifndef DRIVING_RENDER_PARTICLES_H_
#define DRIVING_RENDER_PARTICLES_H_

// ---------------------------------------------------------------------------------------------------------------
// Particle systems: the library of system types (RParticleLibrary, filled at load by the vehicles' and effects'
// AddSystem calls), the systems that follow an emitter and spawn particles (RParticleSystem, under
// RMovableParticleSystem's vtable), and their manager (RParticleSystemManager, one, fgParticleSystems), which keeps
// the live systems in a map by key, steps them each frame and holds the particle cache (ParticleCache.h). Effects
// and vehicles start systems through AddOrRefresh and the RParticleSystem constructor; a system nobody refreshes
// for 30 updates is deleted. See Particles.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "ParticleCache.h"
#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../data/Tree.h"               // Tree, TreeNode, TreePair

class RParticleSystem;
struct RParticleCreatorVtable;

// A particle system type (0xb0 bytes; a record of the library, copied in by AddSystem and munged there). The
// colours are alpha, red, green and blue, 0-255 until munged to 0-1. (Field names ours.)
struct RParticleSystemData {
    float spreadX;                      // +0x00 a particle starts up to this far either side of the emitter in x
    float speed;                        // +0x04 along the emitter's direction
    float spreadZ;                      // +0x08 ... and in z
    int32_t count;                      // +0x0c particles a cycle; halved by MungeDataForPlatform
    float sideSpeed;                    // +0x10 up to this either way along the emitter's side
    float speedSpread;                  // +0x14 speed varies by up to this either way
    float axisSpeed;                    // +0x18 up to this either way along the spawner's axis
    uint32_t flags;                     // +0x1c ParticleFlags
    float drag;                         // +0x20 velocity's factor each tick
    uint32_t unknown24;
    uint32_t unknown28;
    uint32_t unknown2C;                 // +0x2c a particle's alphaSize unless unknownA0 or alphaSpread is set
    float invLife;                      // +0x30 1 / life
    float size;                         // +0x34 at spawn; munged, 0.75 times the size at death
    float sizeChange;                   // +0x38 at death; munged, 0.75 times (at spawn - at death)
    uint32_t life;                      // +0x3c ticks, at most 255
    uint32_t onTime;                    // +0x40 ticks a cycle a system spawns ...
    uint32_t offTime;                   // +0x44 ... and then does not; 0 no cycle
    uint32_t unknown48;                 // +0x48 times the rate, spawns owed when a system starts
    float gravity;                      // +0x4c a particle's gravity, give or take ...
    float gravitySpread;                // +0x50 ... this
    float spin;                         // +0x54 degrees until munged, then radians
    float unknown58;
    float maxSize;                      // +0x5c the larger of the two sizes, as loaded
    float u;                            // +0x60 the sprite's corner in the texture ...
    float v;                            // +0x64
    float uvSize;                       // +0x68 ... and its size
    uint32_t unknown6C;
    Coord4 colours[3];                  // +0x70 ARGB at spawn, mid-life and death (munged: the other way round)
    float unknownA0;                    // +0xa0 the size nibble's spread
    float alphaSpread;                  // +0xa4 the alpha nibble's spread
    uint32_t unknownA8;
    uint32_t index;                     // +0xac its place in the library
};
static_assert(sizeof(RParticleSystemData) == 0xb0, "a particle system type is 0xb0 bytes");
static_assert(offsetof(RParticleSystemData, flags) == 0x1c && offsetof(RParticleSystemData, life) == 0x3c &&
              offsetof(RParticleSystemData, maxSize) == 0x5c && offsetof(RParticleSystemData, colours) == 0x70 &&
              offsetof(RParticleSystemData, index) == 0xac, "RParticleSystemData layout");

// What a system follows (name ours): an effect's or a vehicle's record, of which the particle code reads these.
struct RParticleEmitter {
    Coord3 position;                    // +0x00
    uint8_t unknown0C[0xc];
    RParticleSystem *system;            // +0x18 its system; the system's destructor clears it
    uint8_t unknown1C[4];
    Coord3 side;                        // +0x20
    uint32_t systemTag;                 // +0x2c its system type's tag in the library
    Coord3 direction;                   // +0x30
    uint8_t body;                       // +0x3c handed to its particles (RParticle::body)
    uint8_t unknown3D[3];
};
static_assert(offsetof(RParticleEmitter, system) == 0x18 && offsetof(RParticleEmitter, side) == 0x20 &&
              offsetof(RParticleEmitter, systemTag) == 0x2c && offsetof(RParticleEmitter, direction) == 0x30 &&
              offsetof(RParticleEmitter, body) == 0x3c, "RParticleEmitter layout");

// The two maps of the particle code, std::map<uint32_t, T *> as the game compiled it (data/Tree.h's layout): the
// manager's systems by key, and the library's types by tag. Most of their code is one compiled copy for both; the
// subtree and range erases are a copy each, and find is the CARP resolver map's (0x00118f20), the linker having
// folded the identical code.
class ParticleMap : public Tree {
public:
    TreeNode** InsertAt(TreeNode **result, bool addLeft, TreeNode *where, const TreePair *value);   // 0x000a23c0
    TreeNode** EraseAt(TreeNode **result, TreeNode *where);                                       // 0x000a2660
    TreeInsertResult* InsertUnique(TreeInsertResult *result, const TreePair *value);               // 0x000a2a70
    void EraseSystemSubtree(TreeNode *node);                                                      // 0x000a2080
    TreeNode** EraseSystemRange(TreeNode **result, TreeNode *first, TreeNode *last);               // 0x000a25a0
    void EraseTypeSubtree(TreeNode *node);                                                        // 0x000a20c0
    TreeNode** EraseTypeRange(TreeNode **result, TreeNode *first, TreeNode *last);                 // 0x000a2b30

    TreeNode** Find(TreeNode **result, const uint32_t *key);
};
static_assert(sizeof(ParticleMap) == 12, "a map is 12 bytes");

// One pointer, constructed from it: the constructor every such class shares, the linker having folded them (Ghidra
// files it under ActionRef, engine/ActionQueue.hpp's). The original manager's destructor, which inlines the
// library's, makes map iterators with it.
struct PointerHolder {
    void *pointer;

    PointerHolder* Construct(void *value);                                                      // 0x000a1ec0
};

// The library of system types (8 bytes, inside the manager).
class RParticleLibrary {
public:
    // The types (0xb010 bytes; allocated as "RParticleLibrary::RPartLibraryList"): up to 256, and a map from
    // their tags to them.
    class RPartLibraryData {
    public:
        RParticleSystemData systems[256];   // +0x0000
        uint32_t count;                     // +0xb000
        ParticleMap types;                  // +0xb004

        RPartLibraryData* Construct();                                                          // 0x000a2e20
        void Destruct();        // the map erased and its head freed                            // 0x000a2de0
        // A copy of `data` under `tag`, its index set; the index, or -1 when the library is full.
        int AddSystem(RParticleSystemData *data, uint32_t tag);                                 // 0x000a2c80
    };

    RPartLibraryData *data;             // +0x00
    uint32_t unknown04;

    RParticleLibrary* Construct();                                                              // 0x000a2e70
    // The types freed (only exception unwinds call it; the manager's destructor inlines it).
    void Destruct();                                                                            // 0x000a2ee0
    // Back to the one default type, 'dflt'.
    void Reload();                                                                              // 0x000a2d50
    // AddSystem, then the copy munged; its index.
    int AddSystem(RParticleSystemData *data, uint32_t tag);                                     // 0x000a2d10
    RParticleSystemData* FindSystemViaIndex(int index);                                         // 0x000a1e40
    // The type with `tag`, or the one with the lowest tag if none has it.
    RParticleSystemData* FindSystemViaTag(uint32_t tag);                                        // 0x000a2100

    // The life checked (at most 255) and the spawns owed scaled, then MungeDataForPlatform.
    static void MungeData(RParticleSystemData *source, RParticleSystemData *data);              // 0x000a1e60
    // source copied to data, then data's values put in the form the Xbox code reads.
    static void MungeDataForPlatform(RParticleSystemData *source, RParticleSystemData *data);   // 0x000ab460
};
static_assert(sizeof(RParticleLibrary) == 8, "RParticleLibrary is 8 bytes");
static_assert(offsetof(RParticleLibrary::RPartLibraryData, count) == 0xb000 &&
              offsetof(RParticleLibrary::RPartLibraryData, types) == 0xb004 &&
              sizeof(RParticleLibrary::RPartLibraryData) == 0xb010, "RPartLibraryData layout");

// RParticleSystem's vtable (RMovableParticleSystem's, 0x00192da8)
struct RParticleSystemVtable {
    RParticleSystem *(__fastcall *destroy)(RParticleSystem *system, int, unsigned flags);   // slot 0
    void (__fastcall *update)(RParticleSystem *system, int, int ticks);                      // slot 1
};

// RParticleSystem::systemFlags
enum ParticleSystemFlags : uint16_t {
    kSystemAtEmitter = 0x1,             // spawns at the emitter's position, not positionSource
    kSystemNoSize = 0x2,                // its type's size and size change are both 0
};

// A particle system (0x70 bytes): spawns its type's particles at its emitter as it moves. (Field names ours.)
class RParticleSystem {
public:
    // Makes systems for the manager's AddOrRefresh (a vtable of one slot, 0x00193c64).
    class RParticleCreator {
    public:
        const RParticleCreatorVtable *vtable;

        // A new system, or NULL if the allocation fails.
        RParticleSystem* Create(float unknown34, RParticleEmitter *emitter);                    // 0x000a2350
    };

    const RParticleSystemVtable *vtable;    // +0x00
    uint8_t unknown04[0xc];
    Coord4 axis;                        // +0x10 the emitter's side cross its direction (InitData); w 5
    Coord3 lastPosition;                // +0x20 where it spawned from last update
    int32_t keepAlive;                  // +0x2c updates left until the manager deletes it; AddOrRefresh's 30
    uint32_t time;                      // +0x30 ticks into its on and off cycle
    float unknown34;                    // +0x34 no spawning while its type's maxSize / this < 0.004
    float nextSpawn;                    // +0x38 the spawn count at which the next particle is due
    float spawns;                       // +0x3c particles owed, counted up by rate * rateScale a tick
    RParticleSystemData *data;          // +0x40 its type
    RParticleEmitter *emitter;          // +0x44
    uint8_t active;                     // +0x48
    uint8_t unknown49;
    uint16_t systemFlags;               // +0x4a ParticleSystemFlags
    Coord3 *positionSource;             // +0x4c where it spawns without kSystemAtEmitter: the emitter's position
    Coord3 *velocityPerSecond;          // +0x50 RParticle::ResetParticle's, NULL from the constructor
    float rate;                         // +0x54 particles a tick
    float rateScale;                    // +0x58
    float unknown5C;
    int32_t unknown60;
    int32_t scaledCount;                // +0x64 the type's count times ParticleDensity / 256
    int32_t index;                      // +0x68 its type's index in the library
    uint32_t unknown6C;

    // initData: also InitData, the emitter's position and axis taken and the type's owed spawns counted.
    RParticleSystem* Construct(float unknown34, RParticleEmitter *emitter, bool initData);     // 0x000a2140
    void InitData();                                                                            // 0x000a1c00
    // Another type, by tag, if it is not the emitter's already.
    void SetSystem(uint32_t tag);                                                               // 0x000a2290
    // Vtable slot 1: `ticks` ticks of spawning.
    void Update(int ticks);                                                                     // 0x000a1f00
};
static_assert(sizeof(RParticleSystem) == 0x70, "RParticleSystem is 0x70 bytes");
static_assert(offsetof(RParticleSystem, axis) == 0x10 && offsetof(RParticleSystem, keepAlive) == 0x2c &&
              offsetof(RParticleSystem, data) == 0x40 && offsetof(RParticleSystem, systemFlags) == 0x4a &&
              offsetof(RParticleSystem, rate) == 0x54 && offsetof(RParticleSystem, index) == 0x68,
              "RParticleSystem layout");

// RParticleSystem::RParticleCreator's vtable (0x00193c64)
struct RParticleCreatorVtable {
    RParticleSystem *(__fastcall *create)(RParticleSystem::RParticleCreator *creator, int, float unknown34,
                                          RParticleEmitter *emitter);
};

// The class whose vtable RParticleSystem's constructor installs (0x00192da8: the destructor, then Update).
class RMovableParticleSystem : public RParticleSystem {
public:
    // Vtable slot 0: the emitter's link cleared; bit 0 of `flags` frees the system too.
    RMovableParticleSystem* Delete(unsigned flags);                                             // 0x000a1ed0
};

class RParticleSystemManager {
public:
    uint32_t lastTick;                  // +0x00 the game tick of the last UpdateSpawnAllSystems
    ParticleMap *systems;               // +0x04 the live systems, by their callers' keys
    RParticleLibrary library;           // +0x08
    RParticleParticleCache cache;       // +0x10
    uint8_t unknown10028[8];

    RParticleSystemManager* Construct();                                                        // 0x000a2f00
    // Every system deleted, the map and the library freed (Ghidra: FUN_000a2fc0).
    void Destruct();                                                                            // 0x000a2fc0
    // The texture context `name` made, and the manager.
    static void Init(const char *name);                                                         // 0x000a3140
    static void Shutdown();                                                                     // 0x000a31e0
    // The particles cleared and every system deleted.
    void Reset();                                                                               // 0x000a29c0

    // The system under `key`, made by `creator` for `emitter` if there is none; its keep-alive and unknown34
    // set. The answer is a static holding the system.
    RParticleSystem** AddOrRefresh(uint32_t key, RParticleEmitter *emitter, RParticleSystem::RParticleCreator *creator,
                                   float unknown34);                                            // 0x000a2bf0
    // Up to three ticks for every system, by the game ticks since the last call; those whose keep-alive runs
    // out are deleted.
    void UpdateSpawnAllSystems();                                                               // 0x000a2930
    void UpdateAndRenderAllSystems();                                                           // 0x000a1e30
    // `count` particles of `data` at `position`, going `direction` (the manager is fgParticleSystems, not this).
    void CreateParticles(RParticleSystemData *data, const Coord3 *position, const Coord3 *direction, int count);  // 0x000a1c60
};
static_assert(sizeof(RParticleSystemManager) == 0x10030, "RParticleSystemManager is 0x10030 bytes");
static_assert(offsetof(RParticleSystemManager, cache) == 0x10, "RParticleSystemManager layout");

#define fgParticleSystems (*(RParticleSystemManager **)0x00201730)

#endif // DRIVING_RENDER_PARTICLES_H_
