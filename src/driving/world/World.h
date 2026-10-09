#ifndef DRIVING_WORLD_WORLD_H_
#define DRIVING_WORLD_WORLD_H_

// ---------------------------------------------------------------------------------------------------------------
// WWorld, the loaded track (Ghidra: WWorld, one instance at fgWorld). LoadTrackFile loads the track's CARP file,
// Open resolves it and starts everything that lives on it - the scene tree, the mission rules, the world's sounds,
// the triggers, the path engine's instances, the scene objects of the proc-anim instances, the collision manager
// and grid; Reset puts it back for a restart, Close shuts it down. See World.cpp.
//
// The track's records, as the CARP file has them in its 'Map ' group: the render instances ('in  ', CARP::Instance),
// their proc-anim states ('ps  '), the environment descriptions ('Envi'), the sounds ('Audi'). An instance's
// article reference leads to what this file calls its article (WorldArticle): the model's group and its effects.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "CollisionInstance.h"           // WORLD_UNTESTED; CollisionTypes.h: MATRIX4, Coord3/Coord4, GameVector
#include "../data/AttributeSet.h"
#include "../data/Carp.h"                 // CARP::Instance
#include "../render/RPathHandle.hpp"

class RSceneObj;
class UGroup;
class WSoundGroup;
class WWorldPos;
namespace EAGL {
struct Model;
}
struct RCARPFile;
struct WMapHeader;

// ---- the track's records

// A render instance's flags (CARP::Instance::flags)
enum WorldInstanceFlags : uint8_t {
    kWorldInstanceSceneObj = 0x01,      // Open set it: its proc-anim state names an article
    kWorldInstanceProcAnim = 0x10,      // it has a proc-anim state (procAnimIndex)
    kWorldInstanceTargetable = 0x20,    // its scene object is made a target
};

// A proc-anim state's flags (ProcAnimState::flags)
enum ProcAnimFlags : uint8_t {
    kProcAnimTracked = 0x04,            // the simulation tracks its instance
};

// A render instance's proc-anim state ('ps  ' records, 0x20 bytes; Ghidra: ProcAnimState). Open makes the scene
// object of each state that names an article; the proc-anim functions (anim/ProcAnim.cpp) read its type and parameter.
struct ProcAnimState {
    uint8_t type;                       // +0x00 (2: the two references are resolved, data/Carp.cpp)
    uint8_t procAnimType;               // +0x01 ProcAnimByState's
    uint16_t parameter;                 // +0x02 the proc-anim function's (with a state)
    uint8_t unknown04;
    uint8_t flags;                      // +0x05 ProcAnimFlags
    uint8_t unknown06;
    uint8_t article;                    // +0x07 RSceneObj::UseArticle's index; 0xff none
    RSceneObj *sceneObj;                // +0x08 made by Open, again by Reset
    // A path's (type 2, RPathEngine's): the CARP resolver resolves the path and the master
    CARP::PathInfo *path;               // +0x0c
    CARP::Instance *master;             // +0x10 the instance whose path's time this one keeps behind
    float startTime;                    // +0x14 the parametric time it starts at
    float speed;                        // +0x18
    uint8_t untransformed;              // +0x1c the path's matrix is applied as it is, not through kPathAxes
    uint8_t unknown1D[3];
};
static_assert(sizeof(ProcAnimState) == 0x20, "a proc-anim state is 32 bytes");
static_assert(offsetof(ProcAnimState, sceneObj) == 8, "ProcAnimState::sceneObj");
static_assert(offsetof(ProcAnimState, path) == 0x0c && offsetof(ProcAnimState, untransformed) == 0x1c,
              "ProcAnimState layout");

constexpr uint8_t kNoArticle = 0xff;

// The per-type parts of an article effect, from its +0x18 (ArticleEffect's union)
struct ArticleGfxEffect {               // kTypeGfx
    void *reference;                    // +0x18 the effect GFX::Trigger plays
    void *triggered;                    // +0x1c what GFX::Trigger answered
    Coord4 rotation;                    // +0x20 a quaternion; the start of what GFX::Trigger is handed
    uint8_t unknown30[0x10];
};
struct ArticleLightEffect {             // kTypeLight: RHighLevelLightManager::AddDynamicLightEffect's
    int32_t priority;                   // +0x18
    uint8_t colour[4];                  // +0x1c taken as x, y, z, w from the last byte back
    uint32_t blinkStartTick;            // +0x20 the blink, as a glare's
    float blinkRate;                    // +0x24 cycles per tick
    float blinkDuty;                    // +0x28 the part of a cycle it is lit
    float blinkBase;                    // +0x2c
    float blinkAmplitude;               // +0x30
    uint8_t unknown34[0xc];
};
struct ArticleLocatorEffect {           // kTypeLocator: RSceneObj::LocateFX's
    char name[0x14];                    // +0x18
    int32_t instance;                   // +0x2c
    Coord4 rotation;                    // +0x30 a quaternion
};
struct ArticleSpringEffect {            // kTypeSpring: RSkeletalObj's springs
    Coord3 axis;                        // +0x18 the hinge
    float mass;                         // +0x24
    float limit;                        // +0x28 the angle (turns) it swings to, from 0; negative: the other way
    float torqueThreshold;              // +0x2c above it, a spring past its rest side is released
    float angularVelocity;              // +0x30
    float angle;                        // +0x34 in turns
    uint8_t unknown38[8];
};

// An effect of an article (0x40 bytes, a list ended by a zero type). RAnimEngine::Handle keeps copies of the ones
// with any of kHandleFlags (Handle::FindEffectByID).
struct ArticleEffect {
    enum Flags : uint16_t {
        kFlag01 = 0x0001,
        kFlag02 = 0x0002,
        kStartsOnInitial = 0x0004,      // RSceneObj::InitEffects(true) switches it on
        kFlag08 = 0x0008,               // Handle::effectMask28
        kStartsOn = 0x0010,             // RSceneObj::InitEffects switches it on
        kPermanent = kStartsOnInitial | kStartsOn,  // RSceneObj::DecayEffects leaves it on
        kFollowsSystem = 0x0080,        // drawn only while its instance's animation system plays
        kOnGround = 0x0100,             // its height is the world's under it
        kFlag200 = 0x0200,              // also on a mirrored instance
        kBlinks = 0x0400,               // a glare's or a light's brightness follows its blink
        kBlinkShaped = 0x0800,          // ... as a triangle wave rather than a sawtooth
        kFlag1000 = 0x1000,
        kShowsDamaged = 0x2000,         // drawn even when its zone is damaged
        kBlinkPulse = 0x4000,           // ... or, with kBlinkShaped, a pulse with a dip at its peak
        // a handle keeps the effect
        kHandleFlags = kFlag02 | kStartsOnInitial | kFlag08 | kStartsOn | kFollowsSystem | kFlag1000,
        kMask38Flags = kFlag02 | kStartsOnInitial | kStartsOn | kFollowsSystem,     // Handle::effectMask38
    };
    enum Type : uint8_t {
        kTypeEnd = 0,
        kTypeGlare = 2,                 // RGlareManager::AddModelGlare
        kTypeLight = 5,                 // RHighLevelLightManager::AddDynamicLightEffect
        kTypeGfx = 6,                   // its reference is GFX::Trigger's effect; Handle::effectMask30
        kTypeLocator = 7,               // RSceneObj::LocateFX's
        kTypeSpring = 8,                // RSkeletalObj's
    };

    Coord3 position;                    // +0x00 in the object's frame
    float unknown0C;                    // +0x0c a light's position's w
    uint16_t flags;                     // +0x10 Flags
    uint16_t bits;                      // +0x12 Handle::effectBits' initial value
    uint8_t type;                       // +0x14 Type
    uint8_t instance;                   // +0x15 the instance it belongs to (a handle's copy: the handle's instance)
    uint8_t zone;                       // +0x16 its damage zone: unless kShowsDamaged, not drawn while it is damaged
    uint8_t id;                         // +0x17 FindEffectByID's
    union {                             // +0x18 by type
        ArticleGfxEffect gfx;
        ArticleLightEffect light;
        ArticleLocatorEffect locator;
        ArticleSpringEffect spring;
    };
};
static_assert(sizeof(ArticleEffect) == 0x40, "an article effect is 64 bytes");
static_assert(offsetof(ArticleEffect, gfx.rotation) == 0x20 && offsetof(ArticleEffect, light.blinkAmplitude) == 0x30 &&
              offsetof(ArticleEffect, locator.instance) == 0x2c && offsetof(ArticleEffect, spring.angle) == 0x34,
              "ArticleEffect layout");
static_assert(offsetof(ArticleEffect, bits) == 0x12 && offsetof(ArticleEffect, instance) == 0x15 &&
              offsetof(ArticleEffect, id) == 0x17, "ArticleEffect layout");

// What a model leads to (the names of these three are ours)
struct WorldModelInfo {
    uint8_t unknown00[0x10];
    int32_t sound;                      // +0x10 a path's sound (bank << 7 | index), -1 none
    int32_t voice;                      // +0x14 its voice (the same packing), -1 none
    uint8_t unknown18[0x10];
    int32_t id;                         // +0x28 kUntrackedModel: never tracked by the simulation
};
static_assert(offsetof(WorldModelInfo, id) == 0x28, "WorldModelInfo::id");

constexpr int32_t kUntrackedModel = 0x696;

struct WorldModel {
    UGroup *group;                      // +0x00 the model's CARP data group (RSceneObj::UseArticle's)
    uint8_t unknown04[8];
    WorldModelInfo *info;               // +0x0c
    uint8_t unknown10[0x24];
    const void *pathUserData;           // +0x34 RPathEngine::CreatePathHandle's RPathHandle::userData
};

// An article's model for view distances up to `distance` (the last one's beyond it too)
struct ArticleLod {
    float distance;
    EAGL::Model *model;
};

// What a render instance's article reference (CARP::Instance::articleDesc) leads to.
struct WorldArticle {
    // renderType: the lighting setup the model draws call (render/Renderer.cpp's RenderTypeSetups)
    enum RenderType : uint8_t {
        kRenderWorld = 0,               // RRenderSharedData::SendPerWorldInstance
        kRenderCar = 1,                 // SendPerCarInstance
        kRenderObject = 2,              // SendPerObjectInstance
        kRenderType3 = 3,               // none; RVehicle::PostLoad keeps its first model
        kRenderType4 = 4,               // none; ditto
    };
    enum RenderFlags : uint8_t {
        kNoFog = 0x01,                  // drawn with the fog off
    };

    WorldModel *model;                  // +0x00
    ArticleEffect *effects;             // +0x04 NULL for none; a list ended by a zero type
    CARP::AnimInfo *animInfos;          // +0x08 its animations (RAnimEngine's), NULL for none
    uint8_t unknown0C[0xe];
    uint8_t systemId;                   // +0x1a RAnimEngine::Handle::Create's copies of animInfos[0]'s ids
    uint8_t mirroredSystemId;           // +0x1b
    uint8_t lodCount;                   // +0x1c
    uint8_t drawPass;                   // +0x1d which of WRender's passes draws its instances
    uint8_t renderType;                 // +0x1e RenderType
    uint8_t renderFlags;                // +0x1f RenderFlags
    ArticleLod lods[1];                 // +0x20 lodCount of them, nearest first

    // (the name is ours; the linker placed it among the renderer's code)
    ArticleLod* GetLods();                                                                      // 0x0007dae0
};
static_assert(offsetof(WorldArticle, animInfos) == 8 && offsetof(WorldArticle, systemId) == 0x1a &&
              offsetof(WorldArticle, lodCount) == 0x1c && offsetof(WorldArticle, drawPass) == 0x1d &&
              offsetof(WorldArticle, lods) == 0x20, "WorldArticle layout");

inline WorldArticle *ArticleOf(const CARP::Instance *instance) {
    return reinterpret_cast<WorldArticle *>(uintptr_t(instance->articleDesc.value));
}

// An environment description ('Envi' records, 0x1c bytes); a render instance's is the low half of its +0x2c word
struct EnviroDesc {
    uint8_t unknown00[0x1c];
};
static_assert(sizeof(EnviroDesc) == 0x1c, "an environment description is 28 bytes");

// A sound placed on the track ('Audi' records, 0x60 bytes). Open hands each to the world's sound group.
struct WorldSoundRecord {
    Coord3 position;                    // +0x00
    uint32_t unknown0c;
    char name[0x2f];                    // +0x10 "<bank>: <sound>" (WSoundGroup::Add's format)
    uint8_t unknown3f;                  // +0x3f WSound::unknown128 is 1 when it is 0
    float volume;                       // +0x40
    float pitch;                        // +0x44
    float unknown48;                    // +0x48 WSound::SetUnknown118's
    float unknown4c;
    uint32_t unknown50;                 // +0x50 WSound::unknown120
    float unknown54;                    // +0x54 WSound::unknown124
    float unknown58;                    // +0x58 WSound::minDistance
    float radius;                       // +0x5c
};
static_assert(sizeof(WorldSoundRecord) == 0x60, "a track sound is 96 bytes");

// ---- the scene objects Open made

// A scene object Open made for a proc-anim instance, and what Reset needs to make it again (0x4c bytes; the name is
// ours): the instance's matrix as its transform, whose rows' fourth words carry the instance's index, whether it
// is a target and its proc-anim state (the transform's last word is 1), then the first 12 bytes of that state as
// they were.
struct WorldSceneObject {
    float right[3];                     // +0x00
    uint32_t instanceIndex;             // +0x0c
    float up[3];                        // +0x10
    uint8_t targetable;                 // +0x1c
    uint8_t unknown1d[3];
    float forward[3];                   // +0x20
    ProcAnimState *procAnim;            // +0x2c
    float position[3];                  // +0x30
    float one;                          // +0x3c 1
    uint8_t savedState[0xc];            // +0x40

    const MATRIX4 *Transform() const { return reinterpret_cast<const MATRIX4 *>(this); }
};
static_assert(sizeof(WorldSceneObject) == 0x4c, "a world scene object is 0x4c bytes");
static_assert(offsetof(WorldSceneObject, procAnim) == 0x2c && offsetof(WorldSceneObject, savedState) == 0x40,
              "WorldSceneObject layout");

// std::vector<WorldSceneObject>, Dinkumware's as the game compiled it
struct WorldSceneObjectList : GameVector<WorldSceneObject> {
    static constexpr uint32_t kMaxSize = 0x35e50d7;

    uint32_t Capacity() const { return first == NULL ? 0 : uint32_t(end - first); }

    uint32_t Size() const;                                                                      // 0x000d1520
    void Deallocate(WorldSceneObject *block, uint32_t count);                                   // 0x000d1550
    WorldSceneObject* Ucopy(WorldSceneObject *from, WorldSceneObject *to, WorldSceneObject *dest);   // 0x000d1a80
    WorldSceneObject* Ufill(WorldSceneObject *dest, uint32_t count, const WorldSceneObject *value);  // 0x000d1ab0
    void Tidy();                                                                                // 0x000d1ae0
    void Xlen();            // std::length_error("vector<T> too long")                          // 0x000d1ba0
    void InsertN(WorldSceneObject *where, uint32_t count, const WorldSceneObject *value);       // 0x000d1c20
    void Reserve(uint32_t count);                                                               // 0x000d1f50
    WorldSceneObject** Insert(WorldSceneObject **result, WorldSceneObject *where, const WorldSceneObject *value);   // 0x000d20a0
    void PushBack(const WorldSceneObject *value);                                               // 0x000d2110
};
static_assert(sizeof(WorldSceneObjectList) == 0x10, "a vector is 16 bytes");

// The vector's algorithm copies: std::fill, copy_backward (the iterator answered through `result`), and the
// uninitialized copy and fill
void WorldSceneObjectFill(WorldSceneObject *from, WorldSceneObject *to, const WorldSceneObject *value);   // 0x000d16b0
WorldSceneObject** WorldSceneObjectCopyBackwardTagged(WorldSceneObject **result, WorldSceneObject *from,
                                                      WorldSceneObject *to, WorldSceneObject *destEnd);  // 0x000d16e0
WorldSceneObject* WorldSceneObjectUninitializedCopy(WorldSceneObject *from, WorldSceneObject *to,
                                                    WorldSceneObject *dest);                             // 0x000d1720
WorldSceneObject** WorldSceneObjectCopyBackward(WorldSceneObject **result, WorldSceneObject *from,
                                                WorldSceneObject *to, WorldSceneObject *destEnd);        // 0x000d1a10
void WorldSceneObjectUninitializedFill(WorldSceneObject *dest, uint32_t count,
                                       const WorldSceneObject *value);                                   // 0x000d1a50

// ---- the world

class WWorld {
public:
    AttributeSet attributes;            // +0x00 class "world", named by the track's 'DBN ' string
    char *directory;                    // +0x04 LoadTrackFile's
    char *fileName;                     // +0x08 "<track>.crp"
    char *trackName;                    // +0x0c
    char *attributeName;                // +0x10 the 'DBN ' string
    UGroup *group;                      // +0x14 the CARP file's root group
    RCARPFile *carp;                    // +0x18
    WMapHeader *map;                    // +0x1c 'Wmap': the scene tree
    CARP::Instance *instances;          // +0x20 'in  ': the render instances
    ProcAnimState *procAnims;           // +0x24 'ps  ', NULL if none
    uint32_t instanceCount;             // +0x28
    int32_t enviroCount;                // +0x2c 'Envi'
    EnviroDesc *enviroDescs;            // +0x30
    WorldSceneObjectList *sceneObjects; // +0x34 made by Open
    WSoundGroup *soundGroup;            // +0x38

    static void InitSingleton();                                                    // 0x000595c0
    static void SetTrackName(const char *name, bool copy);                          // 0x000d1210
    WWorld* Construct();                                                            // 0x000d1230
    void LoadTrackFile(const char *trackDirectory, const char *track);                  // 0x000d12d0
    EnviroDesc* GetEnviroDesc(const WWorldPos *position);                           // 0x000d1470
    RSceneObj* GetSceneObjFromInstance(const CARP::Instance *instance);             // 0x000d14a0
    ProcAnimState* GetProcAnimStateFromInstance(const CARP::Instance *instance);    // 0x000d14e0
    void Destruct();                                                                // 0x000d1610
    void Reset();                                                                   // 0x000d1750
    void Close();                                                                   // 0x000d1b30
    bool Open();                                                                    // 0x000d21a0
};
static_assert(sizeof(WWorld) == 0x3c, "WWorld is 60 bytes");
static_assert(offsetof(WWorld, group) == 0x14 && offsetof(WWorld, map) == 0x1c && offsetof(WWorld, instances) == 0x20 &&
              offsetof(WWorld, sceneObjects) == 0x34, "WWorld layout");

#define fgWorld (*(WWorld **)0x0023f310)

#endif // DRIVING_WORLD_WORLD_H_
