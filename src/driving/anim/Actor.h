#ifndef DRIVING_ANIM_ACTOR_H_
#define DRIVING_ANIM_ACTOR_H_

// ---------------------------------------------------------------------------------------------------------------
// ActActor (0x50, "ActActor"): a placed animated character - its ActCharacter (model, textures, weapons), its
// ActPoser (skeleton pose), the ActAnimGroup it plays and the controller that drives it, and a block of matrices
// for its frames, its weapons and the ground under it. An actor either stands in the world on its own (it follows
// the ground) or rides on something that hands it a transform each frame through its callback (a car's driver or
// passenger).
// ActActorDatabase (0x0c): every actor, a std::list<ActActor *>; the culling, update and draw walk it.
// Also here: VU0_quatstoangvel. See Actor.cpp; the controllers are in Controllers.h. The STL code compiled with the
// database (std::string's, std::list's and the exceptions') is GameStd's (data/StdStreams.cpp) and PointerList's
// (world/Targeting.cpp); the animation database's private data's constructor is in AnimationDatabase.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "Controllers.h"
#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../world/CollisionTypes.h"    // MATRIX4
#include "../world/Targeting.h"         // PointerList
#include "../world/WorldPos.h"

class ActAnimGroup;
class ActCharacter;
class ActPoser;
class RViewCamera;
class Transform;
struct ActIKSolveInfo;

// What an actor that rides on something is handed each frame: its owner's transform (the actor's matrices are
// multiplied by it) and velocity (the weapons inherit it). `id` is the actor's callbackId.
typedef void (*ActActorTransformCallback)(int id, MATRIX4 *transform, Coord4 *velocity);

// An actor's matrices (0x394, operator new; the name is ours). Its constructor is WWorldPos's, its destructor
// WWorldPos's empty one.
struct ActActorMatrices {
    WWorldPos worldPos;                 // +0x000 the ground under the actor
    MATRIX4 root;                       // +0x040 the root bone (ActPoser::GetRootBonePosOri); InitializeMatrices'
                                        //        transform at first
    MATRIX4 model;                      // +0x080 scaleMatrix, through the callback's transform: the model's
    MATRIX4 rootCopy;                   // +0x0c0 root, as CalculateMatrices last took it
    MATRIX4 local;                      // +0x100 rootCopy, its position scaled and lifted to the ground
                                        //        (GetActorLocalPosOri)
    MATRIX4 world;                      // +0x140 local through the callback's transform; row 3 the actor's
                                        //        position (GetActorWorldPosition, the culling)
    MATRIX4 weaponBone[2];              // +0x180 the weapon bones (ActPoser::GetWeaponBonePosOri)
    MATRIX4 weapon[2];                  // +0x200 the weapons' matrices, as world
    MATRIX4 previousWeapon[2];          // +0x280 the weapons' matrices a frame ago
    MATRIX4 scaleMatrix;                // +0x300 the character's scale
    MATRIX4 callbackFrame;              // +0x340 rootCopy at the first CalculateMatrices; RotateActor turns it,
                                        //        the animation origin is its row 3
    MATRIX4 *animationFrame;            // +0x380 callbackFrame with a callback, else rootCopy: the controllers'
    Coord3 groundQueryPoint;            // +0x384 where the ground was last looked up
    float groundHeight;                 // +0x390
};
static_assert(sizeof(ActActorMatrices) == 0x394, "an actor's matrices are 0x394 bytes");
static_assert(offsetof(ActActorMatrices, world) == 0x140 && offsetof(ActActorMatrices, weapon) == 0x200 &&
              offsetof(ActActorMatrices, callbackFrame) == 0x340 &&
              offsetof(ActActorMatrices, groundHeight) == 0x390, "ActActorMatrices layout");

// The parts ActActor::drawFlags draws
enum ActActorDrawFlags : int32_t {
    kActorDrawCharacter = 0x1,
    kActorDrawWeapons = 0x2,
};

class ActActor {
public:
    ActCharacter *character;            // +0x00
    ActPoser *poser;                    // +0x04
    ActAnimGroup *animGroup;            // +0x08
    ActActorMatrices *matrices;         // +0x0c
    ActActorTransformCallback callback; // +0x10
    bool hasCallback;                   // +0x14
    uint8_t unknown15[3];
    int32_t callbackId;                 // +0x18
    bool hasWeapon[2];                  // +0x1c
    // Bytes, not bools: the original tests these both as flags and against 1
    uint8_t weaponVisible[2];           // +0x1e
    AnimationController *controller;    // +0x20
    bool visible;                       // +0x24
    bool unknown25;                     // +0x25 false
    bool followGround;                  // +0x26 true: CalculateMatrices looks the ground up as the actor moves
    uint8_t unknown27;
    float alpha;                        // +0x28 the fade's: SetAlpha(alpha * fadeTime * 0.75) while it runs
    float fadeTime;                     // +0x2c counted down a frame at a time; -1 when done
    bool mirrored;                      // +0x30 handed to ActPoser
    uint8_t unknown31[3];
    float scale;                        // +0x34 the character's scale factors (ActCharacter::GetScaleFactors)
    float baseScale;                    // +0x38
    float groundHeight;                 // +0x3c 0 with a callback
    bool culled;                        // +0x40 SetActorCull's
    uint8_t unknown41[3];
    int32_t drawFlags;                  // +0x44 ActActorDrawFlags
    float fovScale;                     // +0x48 the camera's field of view is multiplied by it while drawing
    uint8_t fovConversion;              // +0x4c drawn in the view's second pass, nearer and wider (a byte, as
                                        //       weaponVisible)
    uint8_t unknown4d[3];

    ActActor* Construct(int id, const char *characterName, const char *weapon1, const char *weapon2,
                        ActActorTransformCallback callback, const MATRIX4 *transform, bool mirrored);    // 0x00012ca0
    void Destruct();                                                                            // 0x00012ec0

    AnimationController* SetNewAnimation(int bank, int index);                            // 0x00012640
    // Only while the poser is not blending (mode 1); else NULL
    AnimationController* SetNewCrossFadeAnimation(int bank, int index, float blendTime);  // 0x000126e0
    bool IsAnimationDone();                                                                     // 0x00012140
    void SetTimeScale(float timeScale);                                                         // 0x00012180
    int GetBoneIndex(const char *name);                                                         // 0x000112c0
    // The arguments are not read
    void Fire(float unused1, float unused2, float unused3, float unused4);                     // 0x000112d0

    void InitializeMatrices(const MATRIX4 *transform);                                          // 0x000127a0
    void CalculateMatrices(bool initialise);                                                    // 0x000112e0
    void GetActorLocalPosOri(MATRIX4 *out);                                                     // 0x00011650
    void GetActorWorldPosition(Coord4 *out);                                                    // 0x00011670
    // The flag is not read
    void GetWeaponPosition(MATRIX4 *out, bool unused, int weapon);                             // 0x000116a0

    void ChangeAnimationOrigin(Coord3 *origin);                                                 // 0x00012190
    void GetAnimationOrigin(Coord3 *origin);                                                    // 0x000121f0
    void SetAnimationOrigin(Coord3 *origin);                                                    // 0x00012220
    void SetSuppressAnimationTranslation(bool suppress);                                        // 0x000124f0
    // Turns the poser's initial frame about the vertical through the actor's position
    void CurrentPositionRotateY(float turns);                                                   // 0x00012280
    void RotateActor(float turns);                                                              // 0x00012350
    void RotateActorX(float turns);                                                             // 0x00012420

    void CreateIKs(int count, const int *bones, const Coord4 *axes, const bool *transformTargets);   // 0x000116d0
    void SetIKInfoArray(int count, const ActIKSolveInfo *infos);                               // 0x000116e0
    void CreateGlobalPoseOverrides(int count, const int *bones, bool local);                   // 0x00011760
    void SetGlobalPoseOverride(int index, const Transform *matrix, float weight);              // 0x00011770
    void SetGlobalPoseOverrides(int count, const Transform *overrides, const float *weights);  // 0x00011780

    void Update();                                                                              // 0x00012840
    // pass: the view's second pass, which draws only the actors with fovConversion
    void Draw(RViewCamera *view, bool pass, bool drawWeapons);                                  // 0x000129c0
    void DrawWeapons(RViewCamera *view, bool pass);                                             // 0x000118a0
    void SetupFOVConversion(RViewCamera *view);                                                 // 0x00011790
    void TurnShadowsOff();                                                                      // 0x00012570

    // Weapon 0 is out below 0.95; between 0.95 and 1.95 a held weapon is thrown (SpawnWeapon); then put away
    void DropWeapon(float time);                                                                // 0x00012c30
    // Weapon 0 thrown from its matrix, with the velocity and spin it had over the last frame
    void SpawnWeapon();                                                                         // 0x00011ff0
};
static_assert(sizeof(ActActor) == 0x50, "an ActActor is 0x50 bytes");
static_assert(offsetof(ActActor, callbackId) == 0x18 && offsetof(ActActor, controller) == 0x20 &&
              offsetof(ActActor, alpha) == 0x28 && offsetof(ActActor, scale) == 0x34 &&
              offsetof(ActActor, culled) == 0x40 && offsetof(ActActor, fovConversion) == 0x4c, "ActActor layout");

// Every actor: a std::list<ActActor *> (the list code is every such list's, PointerList's); an actor's handle is its
// node
class ActActorDatabase : public PointerList {
public:
    static void StartUp();                                                                      // 0x00013790
    static void ShutDown();                                                                     // 0x00013810

    // A new actor at the front of the list; its handle through `result` (the original returns the iterator
    // through a hidden pointer). The second form takes a second weapon; the first has none. The actor is
    // mirrored when `unmirrored` is false.
    static PointerListNode** GetNewActorHandle(PointerListNode **result, int id, const char *characterName,
                                               const char *weapon1, ActActorTransformCallback callback,
                                               const MATRIX4 *transform, bool unmirrored);     // 0x000139c0
    static PointerListNode** GetNewActorHandle(PointerListNode **result, int id, const char *characterName,
                                               const char *weapon1, const char *weapon2,
                                               ActActorTransformCallback callback, const MATRIX4 *transform,
                                               bool unmirrored);                                // 0x00013ab0
    static void KillActorByHandle(PointerListNode *handle);                                     // 0x00013270

    void UpdateAll();                   // not while the simulation is paused                    // 0x00013060
    void DrawAll(RViewCamera *view, bool pass, bool drawWeapons);                               // 0x00012fd0
    // ActActor::DrawWeapons for every actor not culled (Ghidra files it under RRenderWorldCamera)
    void DrawActorWeapons(RViewCamera *view, bool pass);                                        // 0x00013020
    void SetupFOVConversions(RViewCamera *view);                                                // 0x000130a0

    // The culling's walk: each actor's position, a sphere of radius 2
    static void PrepareActorsForCulling();                                                      // 0x00012580
    static int GetNextActorCullInfo(Coord4 *sphere, float *height, bool *checkFar, float *farScale);   // 0x00012f40
    static void SetActorCull(int item, bool culled, float distance);                            // 0x0008c8d0

    // _Incsize: this list's compiled copy of PointerList::IncreaseSize
    void IncreaseSize(uint32_t count);                                                          // 0x00013880
};
static_assert(sizeof(ActActorDatabase) == 0xc, "an ActActorDatabase is 12 bytes");

#define ActorDatabase (*(ActActorDatabase **)0x001dd9a0)

// The angular velocity that turns orientation `from` into `to` over `time` seconds (Ghidra's name; x87, not VU0).
// Four forms of the same algebra, by the largest component of `from`.
void VU0_quatstoangvel(Coord3 *out, const Coord4 *from, const Coord4 *to, float time);         // 0x00011930

#endif // DRIVING_ANIM_ACTOR_H_
