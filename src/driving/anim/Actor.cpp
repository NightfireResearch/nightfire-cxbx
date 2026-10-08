#pragma fp_contract(off)

#include "Actor.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "AnimationDatabase.h"         // ActAnimGroup
#include "Character.h"
#include "Events.h"
#include "IK.h"
#include "Manager.h"
#include "Poser.h"
#include "Skeleton.h"
#include "../../helpers.h"
#include "../camera/Camera.h"
#include "../engine/CoreFoundation.h"     // NullFunction, NullFunctionThunk, ThrowLengthError
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../world/Targeting.h"           // PointerList

// ---------------------------------------------------------------------------------------------------------------
// ActActor, ActActorDatabase and VU0_quatstoangvel (0x000112b0-0x00013c00, 0x0008c8d0), ported from the listing.
//
// An actor's frame each update: the poser poses the skeleton, the weapon and root bones are read back, the
// controller advances the animation, and CalculateMatrices builds the model, world and weapon matrices from the
// root - its position scaled and lifted to the ground (looked up again once the actor has moved 0.2 across it),
// or, for an actor with a callback, put through its owner's transform.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet

// std::list<T *>::_Buyheadnode
#define PointerList_BuyHead ((PointerListNode *(__fastcall *)(PointerList *, int))0x000b8490)

// ---- globals
#define CullRestart BOOL8_AT(0x001dd9a4)                // PrepareActorsForCulling's: the walk starts again
#define CullCursor (*(PointerListNode **)0x001dd9bc)    // the culling walk's node
#define UpdatingActor (*(ActActor **)0x001dd9e0)        // the actor updating (name ours)
#define SimState I32_AT(0x00234e24)                     // Sim.simState
// EAGL's pointer to the view-projection matrix (0x0023fa90)
#define ViewProjectionMatrixPointer (*(const MATRIX4 **)0x001caf5c)

constexpr int32_t kSimPaused = 3;
// The stimulus Fire plays on weapon 0
constexpr uint32_t kFireStimulus = 10;
// Moved this far across the ground (squared), an actor looks the ground up again
constexpr float kGroundQueryDistanceSquared = 0.04f;
// The ground lookup's starting point: nowhere near
constexpr float kNoGroundQuery = 1000000.0f;
// The near plane and the widest field of view while drawing the second pass
constexpr float kSecondPassNearZ = 0.3f;
constexpr float kFOVConversionNearZ = 0.4f;
constexpr float kMinFieldOfView = 2.0f;
// A thrown weapon: its velocity over the last frame (a twentieth of a second), and the share of its owner's
constexpr float kFramesPerSecond = 20.0f;
constexpr float kFrameTime = 0.05f;
constexpr float kInheritedVelocity = 0.6f;
// std::list's max_size
constexpr uint32_t kMaxListSize = 0x3fffffff;

// ---- ActActor

// FUNC_AT(0x00012ca0)
ActActor* ActActor::Construct(int id, const char *characterName, const char *weapon1, const char *weapon2,
                              ActActorTransformCallback callback, const MATRIX4 *transform, bool mirrored) {
    this->callback = callback;
    callbackId = id;
    controller = NULL;
    visible = true;
    unknown25 = false;
    followGround = true;
    alpha = 0.0f;
    fadeTime = 0.0f;
    this->mirrored = mirrored;
    groundHeight = 0.0f;
    culled = true;
    drawFlags = kActorDrawCharacter | kActorDrawWeapons;
    fovScale = 1.0f;
    fovConversion = 0;
    hasCallback = callback != NULL;

    ActActorMatrices *newMatrices = (ActActorMatrices *)OperatorNew(sizeof(ActActorMatrices));
    if (newMatrices != NULL)
        newMatrices->worldPos.Construct();
    matrices = newMatrices;

    if (weapon1 != NULL && *weapon1 == 0)
        weapon1 = NULL;
    if (weapon2 != NULL && *weapon2 == 0)
        weapon2 = NULL;
    hasWeapon[0] = weapon1 != NULL;
    weaponVisible[0] = hasWeapon[0];
    hasWeapon[1] = weapon2 != NULL;
    weaponVisible[1] = hasWeapon[1];

    ActCharacter *newCharacter = (ActCharacter *)UMemory::FastAlloc(sizeof(ActCharacter), "ActCharacter");
    if (newCharacter != NULL) {
        ActManager *manager = TheActManager;
        newCharacter = newCharacter->Construct(manager->models, manager->textures, manager->weapons, !hasCallback, characterName, weapon1, weapon2);
    }
    character = newCharacter;

    ActSkeletonDatabase *skeletons = TheActManager->skeletons;
    character->GetScaleFactors(&scale, &baseScale);
    BuildScaleUniform(&matrices->scaleMatrix, scale);
    InitializeMatrices(transform);
    ActSkeleton *skeleton = skeletons->skeletons[character->info->number];

    ActPoser *newPoser = (ActPoser *)UMemory::FastAlloc(sizeof(ActPoser), "ActPoser");
    if (newPoser != NULL)
        newPoser = newPoser->Construct(skeleton, TheActManager->events, this->mirrored, hasCallback);
    poser = newPoser;

    ActAnimGroup *newGroup = (ActAnimGroup *)UMemory::FastAlloc(sizeof(ActAnimGroup), "ActAnimGroup");
    animGroup = newGroup != NULL ? newGroup->Construct(-1, -1) : NULL;
    return this;
}

// FUNC_AT(0x00012ec0)
void ActActor::Destruct() {
    if (controller != NULL)
        controller->DeleteVirtual(1);
    ActAnimGroup *group = animGroup;
    if (group != NULL) {
        group->Destruct();
        UMemory::FastFree(group, sizeof(ActAnimGroup));
    }
    ActPoser *oldPoser = poser;
    if (oldPoser != NULL) {
        oldPoser->Destruct();
        UMemory::FastFree(oldPoser, sizeof(ActPoser));
    }
    ActCharacter *oldCharacter = character;
    if (oldCharacter != NULL) {
        oldCharacter->Destruct();
        UMemory::FastFree(oldCharacter, sizeof(ActCharacter));
    }
    ActActorMatrices *oldMatrices = matrices;
    if (oldMatrices != NULL) {
        NullFunctionThunk();            // WWorldPos's destructor
        OperatorDelete(oldMatrices);
    }
}

// FUNC_AT(0x00012640)
AnimationController* ActActor::SetNewAnimation(int bank, int index) {
    if (controller != NULL)
        controller->DeleteVirtual(1);
    StandardAnimationController *standard = (StandardAnimationController *)UMemory::FastAlloc(
        sizeof(StandardAnimationController), "AnimationController");
    if (standard != NULL)
        standard = standard->Construct(animGroup, poser, bank, index, matrices->animationFrame);
    controller = standard;
    poser->DoInitialPoses();
    return standard;
}

// FUNC_AT(0x000126e0)
AnimationController* ActActor::SetNewCrossFadeAnimation(int bank, int index, float blendTime) {
    if (poser->mode != kPoseModeNormal)
        return NULL;
    if (controller != NULL)
        controller->DeleteVirtual(1);
    CrossFadeAnimationController *crossFade = (CrossFadeAnimationController *)UMemory::FastAlloc(
        sizeof(CrossFadeAnimationController), "AnimationController");
    if (crossFade != NULL)
        crossFade = crossFade->Construct(animGroup, poser, bank, index, blendTime, false, matrices->animationFrame);
    controller = crossFade;
    poser->DoInitialPoses();
    return crossFade;
}

// FUNC_AT(0x00012140)
bool ActActor::IsAnimationDone() {
    if (poser->mode == kPoseModeCrossFade || poser->mode == kPoseModeManual)
        return false;
    double next = (double)poser->timeScale * poser->frameTime * poser->framesPerSecond + poser->time;
    if (next < poser->endTime - 1.0)
        return false;
    return true;
}

// FUNC_AT(0x00012180)
void ActActor::SetTimeScale(float timeScale) {
    poser->timeScale = timeScale;
}

// FUNC_AT(0x000112c0)
int ActActor::GetBoneIndex(const char *name) {
    return poser->GetBoneIndex(name);
}

// FUNC_AT(0x000112d0)
void ActActor::Fire(float unused1, float unused2, float unused3, float unused4) {
    character->PlayEvent(0, kFireStimulus);
}

// ---- matrices

// FUNC_AT(0x000127a0)
void ActActor::InitializeMatrices(const MATRIX4 *transform) {
    MatrixCopy(transform, &matrices->root);
    matrices->root.mtx[3][0] = baseScale * matrices->root.mtx[3][0];
    matrices->root.mtx[3][1] = matrices->root.mtx[3][1] * baseScale;
    matrices->root.mtx[3][2] = matrices->root.mtx[3][2] * baseScale;
    matrices->weaponBone[0] = matrices->root;
    matrices->weaponBone[1] = matrices->root;
    matrices->groundHeight = 0.0f;
    matrices->groundQueryPoint.z = kNoGroundQuery;
    matrices->groundQueryPoint.y = kNoGroundQuery;
    matrices->groundQueryPoint.x = kNoGroundQuery;
    CalculateMatrices(true);
}

// FUNC_AT(0x000112e0)
void ActActor::CalculateMatrices(bool initialise) {
    ActActorMatrices *m = matrices;
    float height;
    if (hasCallback) {
        height = 0.0f;
    } else {
        if (followGround && !initialise) {
            Coord3 position = {m->local.mtx[3][0], m->local.mtx[3][1], m->local.mtx[3][2]};
            double dx = (double)m->groundQueryPoint.x - position.x;
            double dz = (double)m->groundQueryPoint.z - position.z;
            if (dz * dz + dx * dx > kGroundQueryDistanceSquared) {
                m->groundQueryPoint = position;
                m->worldPos.FindClosestFace(&position, true);
                m->groundHeight = (float)m->worldPos.HeightAtPoint(&position, false);
            }
        }
        height = m->groundHeight;
    }
    groundHeight = height;

    m->model = m->scaleMatrix;
    if (!hasCallback)
        m->model.mtx[3][1] = height + m->model.mtx[3][1];
    m->rootCopy = m->root;
    if (initialise) {
        m->callbackFrame = m->rootCopy;
        m->animationFrame = hasCallback ? &m->callbackFrame : &m->rootCopy;
    }

    m->local = m->rootCopy;
    m->local.mtx[3][1] = m->local.mtx[3][1] * scale;
    m->local.mtx[3][2] = m->local.mtx[3][2] * scale;
    m->local.mtx[3][0] = scale * m->local.mtx[3][0];
    m->local.mtx[3][1] = height + m->local.mtx[3][1];
    m->world = m->local;

    for (int i = 0; i < 2; i++) {
        if (hasWeapon[i]) {
            m->previousWeapon[i] = m->weapon[i];
            m->weapon[i] = m->weaponBone[i];
            m->weapon[i].mtx[3][1] = m->weapon[i].mtx[3][1] * scale;
            m->weapon[i].mtx[3][2] = m->weapon[i].mtx[3][2] * scale;
            m->weapon[i].mtx[3][0] = m->weapon[i].mtx[3][0] * scale;
            m->weapon[i].mtx[3][1] = height + m->weapon[i].mtx[3][1];
        }
    }

    if (hasCallback) {
        alignas(16) MATRIX4 transform;
        alignas(16) Coord4 velocity;
        callback(callbackId, &transform, &velocity);
        VU0_MATRIX4_mult(&m->model, &m->model, &transform);
        VU0_MATRIX4_mult(&m->world, &m->world, &transform);
        if (hasWeapon[0])
            VU0_MATRIX4_mult(&m->weapon[0], &m->weapon[0], &transform);
        if (hasWeapon[1])
            VU0_MATRIX4_mult(&m->weapon[1], &m->weapon[1], &transform);
    }

    if (initialise) {
        Coord3 position = {m->local.mtx[3][0], m->local.mtx[3][1], m->local.mtx[3][2]};
        m->worldPos.FindClosestFace(&position, true);
    }
}

// FUNC_AT(0x00011650)
void ActActor::GetActorLocalPosOri(MATRIX4 *out) {
    *out = matrices->local;
}

// FUNC_AT(0x00011670)
void ActActor::GetActorWorldPosition(Coord4 *out) {
    const float *position = matrices->world.mtx[3];
    out->x = position[0];
    out->y = position[1];
    out->z = position[2];
    out->w = position[3];
}

// FUNC_AT(0x000116a0)
void ActActor::GetWeaponPosition(MATRIX4 *out, bool unused, int weapon) {
    *out = matrices->weapon[weapon];
}

// ---- the animation origin and the actor's frame

// FUNC_AT(0x00012190)
void ActActor::ChangeAnimationOrigin(Coord3 *origin) {
    origin->x = origin->x * baseScale;
    origin->y = origin->y * baseScale;
    origin->z = origin->z * baseScale;
    poser->ChangeAnimationOrigin(origin);
    if (hasCallback) {
        matrices->callbackFrame.mtx[3][0] = origin->x;
        matrices->callbackFrame.mtx[3][1] = origin->y;
        matrices->callbackFrame.mtx[3][2] = origin->z;
    }
}

// FUNC_AT(0x000121f0)
void ActActor::GetAnimationOrigin(Coord3 *origin) {
    poser->GetAnimationOrigin(origin);
    origin->x = scale * origin->x;
    origin->y = scale * origin->y;
    origin->z = scale * origin->z;
}

// FUNC_AT(0x00012220)
void ActActor::SetAnimationOrigin(Coord3 *origin) {
    origin->x = origin->x * baseScale;
    origin->y = origin->y * baseScale;
    origin->z = origin->z * baseScale;
    poser->SetAnimationOrigin(origin);
    if (hasCallback) {
        matrices->callbackFrame.mtx[3][0] = origin->x;
        matrices->callbackFrame.mtx[3][1] = origin->y;
        matrices->callbackFrame.mtx[3][2] = origin->z;
    }
}

// FUNC_AT(0x000124f0)
void ActActor::SetSuppressAnimationTranslation(bool suppress) {
    poser->suppressTranslation = suppress;
    const float *world = matrices->world.mtx[3];
    Coord4 position = {world[0], world[1], world[2], world[3]};
    Coord3 origin;
    poser->GetAnimationOrigin(&origin);
    origin.z = position.z;
    origin.y = origin.y * scale;
    origin.x = position.x;
    SetAnimationOrigin(&origin);
}

// FUNC_AT(0x00012280)
void ActActor::CurrentPositionRotateY(float turns) {
    alignas(16) MATRIX4 placement;
    poser->GetInitialTbOu(&placement);
    float *origin = placement.mtx[3];
    VU0_v4scale(origin, scale, origin);
    alignas(16) Coord4 position;
    VU0_v4copy(matrices->world.mtx[3], &position);
    VU0_v4sub4(origin, &position, origin);
    alignas(16) MATRIX4 rotation;
    BuildRotate(&rotation, turns * 360.0f, 0.0f, 1.0f, 0.0f);
    VU0_MATRIX4_mult(&placement, &placement, &rotation);
    VU0_v4add4(origin, &position, origin);
    VU0_v4scale(origin, baseScale, origin);
    poser->SetInitialTbOu(&placement);
}

// FUNC_AT(0x00012350)
void ActActor::RotateActor(float turns) {
    poser->Rotate(turns);
    if (hasCallback) {
        float c = cos_fractionalangle(turns);
        float s = sin_fractionalangle(turns);
        alignas(16) MATRIX4 rotation = {{
            {c, 0.0f, -s, 0.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            {s, 0.0f, c, 0.0f},
            {0.0f, 0.0f, 0.0f, 1.0f},
        }};
        VU0_MATRIX4_mult(&matrices->callbackFrame, &rotation, &matrices->callbackFrame);
    }
}

// FUNC_AT(0x00012420)
void ActActor::RotateActorX(float turns) {
    poser->RotateX(turns);
    if (hasCallback) {
        float c = cos_fractionalangle(turns);
        float s = sin_fractionalangle(turns);
        alignas(16) MATRIX4 rotation = {{
            {1.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, c, s, 0.0f},
            {0.0f, -s, c, 0.0f},
            {0.0f, 0.0f, 0.0f, 1.0f},
        }};
        VU0_MATRIX4_mult(&matrices->callbackFrame, &rotation, &matrices->callbackFrame);
    }
}

// ---- IK and pose overrides

// FUNC_AT(0x000116d0)
void ActActor::CreateIKs(int count, const int *bones, const Coord4 *axes, const bool *transformTargets) {
    poser->ikSolvers->CreateIKs(count, bones, axes, transformTargets);
}

// FUNC_AT(0x000116e0)
void ActActor::SetIKInfoArray(int count, const ActIKSolveInfo *infos) {
    for (int i = 0; i < count; i++) {
        ActIKSolveInfo info = infos[i];
        info.target.x = info.target.x * baseScale;
        info.target.y = info.target.y * baseScale;
        info.target.z = info.target.z * baseScale;
        poser->ikSolvers->SetInfo(i, &info);
    }
}

// FUNC_AT(0x00011760)
void ActActor::CreateGlobalPoseOverrides(int count, const int *bones, bool local) {
    poser->poseOverrides->CreateGlobalPoseOverrides(count, bones, local);
}

// FUNC_AT(0x00011770)
void ActActor::SetGlobalPoseOverride(int index, const Transform *matrix, float weight) {
    poser->poseOverrides->SetGlobalPoseOverride(index, matrix, weight);
}

// FUNC_AT(0x00011780)
void ActActor::SetGlobalPoseOverrides(int count, const Transform *overrides, const float *weights) {
    poser->poseOverrides->SetGlobalPoseOverrides(count, overrides, weights);
}

// ---- update and draw

// FUNC_AT(0x00012840)
void ActActor::Update() {
    UpdatingActor = this;
    TheActManager->events->SetCurrentActor(this);
    if (fadeTime > 0.0f) {
        character->SetAlpha((float)((double)alpha * fadeTime * 0.75f));
        fadeTime = fadeTime - 1.0f;
        if (fadeTime <= 0.0f)
            fadeTime = -1.0f;
    }
    poser->DoSkeletonPose();
    for (int i = 0; i < 2; i++) {
        if (hasWeapon[i])
            poser->GetWeaponBonePosOri(i, &matrices->weaponBone[i]);
    }
    poser->GetRootBonePosOri(&matrices->root);
    poser->DoEventPose();
    controller->UpdateVirtual();
    CalculateMatrices(false);

    for (int i = 0; i < 2; i++) {
        if (hasWeapon[i]) {
            alignas(16) MATRIX4 bone = matrices->weapon[i];
            if (weaponVisible[i])
                character->SetWeaponBone(i, &bone);
        }
    }

    Coord3 velocity;
    if (callback != NULL) {
        alignas(16) MATRIX4 transform;
        alignas(16) Coord4 ownerVelocity;
        callback(callbackId, &transform, &ownerVelocity);
        velocity.x = ownerVelocity.x;
        velocity.y = ownerVelocity.y;
        velocity.z = ownerVelocity.z;
    } else {
        velocity.x = 0.0f;
        velocity.y = 0.0f;
        velocity.z = 0.0f;
    }
    if (character != NULL)
        character->SetWeaponVelocity(&velocity);
}

// FUNC_AT(0x000129c0)
void ActActor::Draw(RViewCamera *view, bool pass, bool drawWeapons) {
    if (!visible)
        return;
    if (pass ? !fovConversion : fovConversion == 1)
        return;

    if (character->hasShadow) {
        Coord3 *triangle = character->GetShadowTriangle();
        triangle[0].x = matrices->world.mtx[3][0];
        triangle[0].y = matrices->groundHeight + 0.1f;
        triangle[0].z = matrices->world.mtx[3][2] + 4.0f;
        Coord3 point;
        point.y = 0.0f;
        point.x = triangle[1].x = matrices->world.mtx[3][0] + 2.0f;
        point.z = triangle[1].z = matrices->world.mtx[3][2] - 2.0f;
        triangle[1].y = (float)(matrices->worldPos.HeightAtPoint(&point, false) + 0.1f);
        point.x = triangle[2].x = matrices->world.mtx[3][0] - 2.0f;
        point.z = triangle[2].z = matrices->world.mtx[3][2] - 2.0f;
        triangle[2].y = (float)(matrices->worldPos.HeightAtPoint(&point, false) + 0.1f);
    }

    // The second pass draws nearer and, for an actor with a wider fovScale, wider; restored after
    float savedNearZ = 0.0f;
    float savedFieldOfView = 0.0f;
    if (fovConversion == 1) {
        view->SetWorldTransformMode();
        savedNearZ = view->nearZ;
        alignas(16) MATRIX4 inverse = *ViewProjectionMatrixPointer;
        Inverse(&inverse);
        savedFieldOfView = view->camera->fieldOfView;
        view->nearZ = kSecondPassNearZ;
        NullFunction();
        double fieldOfView = (double)savedFieldOfView * fovScale;
        if (fieldOfView > kMinFieldOfView)
            view->camera->fieldOfView = (float)fieldOfView;
        view->SetWorldTransformMode();
        alignas(16) MATRIX4 viewProjection = *ViewProjectionMatrixPointer;
        character->SetWeaponsTransformToWorldSpace(&viewProjection, &inverse);
        character->InheritWeaponLightingFromCar();
    }

    character->CalculateMuzzleFlashIntensity(-1);
    if (weaponVisible[0])
        character->CalculateMuzzleFlashIntensity(0);
    if (weaponVisible[1])
        character->CalculateMuzzleFlashIntensity(1);
    if (drawFlags & kActorDrawCharacter) {
        poser->Skin();
        character->Draw(&matrices->world, &matrices->model);
    }
    if (drawWeapons)
        DrawWeapons(view, pass);

    if (fovConversion == 1) {
        view->nearZ = savedNearZ;
        NullFunction();
        if (savedFieldOfView > kMinFieldOfView)
            view->camera->fieldOfView = savedFieldOfView;
        view->SetWorldTransformMode();
    }
}

// FUNC_AT(0x000118a0)
void ActActor::DrawWeapons(RViewCamera *view, bool pass) {
    EAGL::ViewPort *viewPort = view->viewPort;
    if (!visible)
        return;
    if (pass ? !fovConversion : fovConversion == 1)
        return;
    for (int i = 0; i < 2; i++) {
        if (weaponVisible[i] == 1 && (drawFlags & kActorDrawWeapons)) {
            character->ShowWeapon(i);
            character->DrawWeapon(i, viewPort);
        } else {
            character->HideWeapon(i);
        }
    }
}

// FUNC_AT(0x00011790)
void ActActor::SetupFOVConversion(RViewCamera *view) {
    if (!visible || !fovConversion)
        return;
    view->SetWorldTransformMode();
    alignas(16) MATRIX4 inverse = *ViewProjectionMatrixPointer;
    float savedNearZ = view->nearZ;
    Inverse(&inverse);
    float savedFieldOfView = view->camera->fieldOfView;
    view->nearZ = kFOVConversionNearZ;
    NullFunction();
    double fieldOfView = (double)savedFieldOfView * fovScale;
    if (fieldOfView > kMinFieldOfView)
        view->camera->fieldOfView = (float)fieldOfView;
    view->SetWorldTransformMode();
    alignas(16) MATRIX4 viewProjection = *ViewProjectionMatrixPointer;
    character->SetWeaponsTransformToWorldSpace(&viewProjection, &inverse);
    view->nearZ = savedNearZ;
    NullFunction();
    if (savedFieldOfView > kMinFieldOfView)
        view->camera->fieldOfView = savedFieldOfView;
    view->SetWorldTransformMode();
}

// FUNC_AT(0x00012570)
void ActActor::TurnShadowsOff() {
    character->hasShadow = false;
}

// ---- weapons

// FUNC_AT(0x00012c30)
void ActActor::DropWeapon(float time) {
    if (time < 0.95f) {
        weaponVisible[0] = 1;
        return;
    }
    if (time < 1.95f && weaponVisible[0] == 1)
        SpawnWeapon();
    weaponVisible[0] = 0;
}

// FUNC_AT(0x00011ff0)
void ActActor::SpawnWeapon() {
    ActActorMatrices *m = matrices;
    const MATRIX4 *weapon = &m->weapon[0];
    const MATRIX4 *previous = &m->previousWeapon[0];
    Coord3 position = {weapon->mtx[3][0], weapon->mtx[3][1], weapon->mtx[3][2]};
    Coord3 direction = {weapon->mtx[2][0], weapon->mtx[2][1], weapon->mtx[2][2]};

    // z's difference is scaled unrounded (the original keeps it on the x87 stack)
    Coord3 velocity;
    velocity.x = weapon->mtx[3][0] - previous->mtx[3][0];
    velocity.y = weapon->mtx[3][1] - previous->mtx[3][1];
    double dz = (double)weapon->mtx[3][2] - previous->mtx[3][2];
    velocity.x = velocity.x * kFramesPerSecond;
    velocity.y = velocity.y * kFramesPerSecond;
    velocity.z = (float)(dz * kFramesPerSecond);
    if (callback != NULL) {
        alignas(16) MATRIX4 transform;
        alignas(16) Coord4 ownerVelocity;
        callback(callbackId, &transform, &ownerVelocity);
        velocity.x = (float)((double)ownerVelocity.x * kInheritedVelocity + velocity.x);
        velocity.y = (float)((double)ownerVelocity.y * kInheritedVelocity + velocity.y);
        velocity.z = (float)((double)ownerVelocity.z * kInheritedVelocity + velocity.z);
    }

    alignas(16) Coord4 from;
    alignas(16) Coord4 to;
    VU0_m4toquat(&from, &matrices->previousWeapon[0]);
    VU0_m4toquat(&to, &matrices->weapon[0]);
    Coord3 spin;
    VU0_quatstoangvel(&spin, &from, &to, kFrameTime);
    character->SpawnWeapon(0, &position, &direction, &velocity, &spin);
}

// ---- VU0_quatstoangvel

// FUNC_AT(0x00011930)
void VU0_quatstoangvel(Coord3 *out, const Coord4 *from, const Coord4 *to, float time) {
    int largest = 0;
    float size = fabsf(from->x);
    if (fabsf(from->y) > size) {
        largest = 1;
        size = fabsf(from->y);
    }
    if (fabsf(from->z) > size) {
        largest = 2;
        size = fabsf(from->z);
    }
    if (fabsf(from->w) > size)
        largest = 3;

    // In the x87's precision, each form's terms in the original's order; the floats are its stored temporaries
    // (a difference of two of them is still taken in double)
    double x0 = from->x, y0 = from->y, z0 = from->z, w0 = from->w;
    double x1 = to->x, y1 = to->y, z1 = to->z, w1 = to->w;
    double dt = time;
    switch (largest) {
    case 0: {
        float dot = (float)(y0 * y1 + w0 * w1 + z0 * z1);
        float step = (float)(x1 * dt);
        double k = w1 * w1 - dot + z1 * z1 + y1 * y1;
        out->x = (float)((k * y1 + (w0 * z1 - z0 * w1) * x1 + (y1 - y0) * x1 * x1) * -2.0 / step);
        out->y = (float)((k * z1 + (y0 * w1 - w0 * y1) * x1 + (z1 - z0) * x1 * x1) * -2.0 / step);
        out->z = (float)((k * w1 + (z0 * y1 - y0 * z1) * x1 + (w1 - w0) * x1 * x1) * -2.0 / step);
        break;
    }
    case 1: {
        float dot = (float)(z0 * z1 + w0 * w1);
        float step = (float)(y1 * dt);
        out->x = (float)((x1 * x1 * x1 - (x0 * y1 - z0 * w1 + w0 * z1) * y1 - x0 * x1 * x1 +
                          (w1 * w1 - dot + z1 * z1 + y1 * y1) * x1) * 2.0 / step);
        float xz = (float)(x0 * z1);
        float xw = (float)(x0 * w1);
        out->y = (float)(((w1 * w1 - dot + z1 * z1) * w1 + (xz * y1 - (z0 * y1 + xw) * x1) +
                          (w1 - w0) * y1 * y1 + w1 * x1 * x1) * -2.0 / step);
        out->z = (float)(((w0 * y1 - xz) * x1 - xw * y1 + (w1 * w1 - dot + z1 * z1) * z1 + (z1 - z0) * y1 * y1 +
                          z1 * x1 * x1) * 2.0 / step);
        break;
    }
    case 2: {
        float yz = (float)(y0 * z1);
        float xw = (float)(x0 * w1);
        float cross = (float)(y0 * w1 + x0 * z1);
        float step = (float)(z1 * dt);
        out->x = (float)(((w1 * w1 + z1 * z1) * (w1 - w0) + (((double)yz - xw) * x1 - cross * y1) +
                          (y1 * y1 + x1 * x1) * w1) * 2.0 / step);
        float ww = (float)(w0 * w1);
        out->y = (float)((x1 * x1 * x1 - (cross - w0 * y1) * z1 - x0 * x1 * x1 +
                          (w1 * w1 - (y0 * y1 + ww) + z1 * z1 + y1 * y1) * x1) * 2.0 / step);
        out->z = (float)((((double)xw - yz) * z1 - (w0 * z1 + x0 * y1) * x1 + y1 * x1 * x1 + y1 * y1 * y1 -
                          y0 * y1 * y1 + (w1 * w1 - ww + z1 * z1) * y1) * -2.0 / step);
        break;
    }
    case 3: {
        float xwyz = (float)(x0 * w1 - y0 * z1);
        float cross = (float)(y0 * w1 + x0 * z1);
        float step = (float)(w1 * dt);
        out->x = (float)(((w1 * w1 + z1 * z1) * (z1 - z0) + (y1 * y1 + x1 * x1) * z1 + (xwyz * y1 - cross * x1)) *
                         -2.0 / step);
        float zz = (float)(z0 * z1);
        out->y = (float)(((z0 * w1 - x0 * y1) * x1 - cross * w1 + y1 * x1 * x1 + y1 * y1 * y1 - y0 * y1 * y1 +
                          (w1 * w1 - zz + z1 * z1) * y1) * 2.0 / step);
        out->z = (float)(((w1 * w1 - (y0 * y1 + zz) + z1 * z1 + y1 * y1) * x1 +
                          (x1 * x1 * x1 - (z0 * y1 + xwyz) * w1 - x0 * x1 * x1)) * 2.0 / step);
        break;
    }
    }
}

// ---- ActActorDatabase

// FUNC_AT(0x00013790)
void ActActorDatabase::StartUp() {
    ActActorDatabase *database = (ActActorDatabase *)OperatorNew(sizeof(ActActorDatabase));
    if (database != NULL) {
        database->head = PointerList_BuyHead(database, 0);
        database->size = 0;
    }
    ActorDatabase = database;
}

// FUNC_AT(0x00013810)
void ActActorDatabase::ShutDown() {
    ActActorDatabase *database = ActorDatabase;
    if (database != NULL) {
        database->Destruct();
        OperatorDelete(database);
    }
    ActorDatabase = NULL;
}

// FUNC_AT(0x000139c0)
PointerListNode** ActActorDatabase::GetNewActorHandle(PointerListNode **result, int id, const char *characterName, const char *weapon1, ActActorTransformCallback callback, const MATRIX4 *transform, bool unmirrored) {
    ActActor *actor = (ActActor *)UMemory::FastAlloc(sizeof(ActActor), "ActActor");
    if (actor != NULL)
        actor = actor->Construct(id, characterName, weapon1, NULL, callback, transform, !unmirrored);
    ActActorDatabase *database = ActorDatabase;
    PointerListNode *first = database->Begin();
    void *value = actor;
    PointerListNode *node = database->BuyNode(first, first->prev, &value);
    database->IncreaseSize(1);
    first->prev = node;
    node->prev->next = node;
    *result = ActorDatabase->Begin();
    return result;
}

// FUNC_AT(0x00013ab0)
PointerListNode** ActActorDatabase::GetNewActorHandle(PointerListNode **result, int id, const char *characterName, const char *weapon1, const char *weapon2, ActActorTransformCallback callback, const MATRIX4 *transform, bool unmirrored) {
    ActActor *actor = (ActActor *)UMemory::FastAlloc(sizeof(ActActor), "ActActor");
    if (actor != NULL)
        actor = actor->Construct(id, characterName, weapon1, weapon2, callback, transform, !unmirrored);
    ActActorDatabase *database = ActorDatabase;
    PointerListNode *first = database->Begin();
    void *value = actor;
    PointerListNode *node = database->BuyNode(first, first->prev, &value);
    database->IncreaseSize(1);
    first->prev = node;
    node->prev->next = node;
    *result = ActorDatabase->Begin();
    return result;
}

// FUNC_AT(0x00013270)
void ActActorDatabase::KillActorByHandle(PointerListNode *handle) {
    ActActor *actor = static_cast<ActActor *>(handle->value);
    if (actor != NULL) {
        actor->Destruct();
        UMemory::FastFree(actor, sizeof(ActActor));
    }
    ActActorDatabase *database = ActorDatabase;
    if (handle != database->head) {
        handle->prev->next = handle->next;
        handle->next->prev = handle->prev;
        UMemory::FastFree(handle, sizeof(PointerListNode));
        database->size--;
    }
}

// FUNC_AT(0x00013060)
void ActActorDatabase::UpdateAll() {
    if (SimState == kSimPaused)
        return;
    for (PointerListNode *node = Begin(); node != head; node = node->next)
        static_cast<ActActor *>(node->value)->Update();
}

// FUNC_AT(0x00012fd0)
void ActActorDatabase::DrawAll(RViewCamera *view, bool pass, bool drawWeapons) {
    for (PointerListNode *node = Begin(); node != head; node = node->next) {
        ActActor *actor = static_cast<ActActor *>(node->value);
        if (!actor->culled)
            actor->Draw(view, pass, drawWeapons);
    }
}

// FUNC_AT(0x00013020)
void ActActorDatabase::DrawActorWeapons(RViewCamera *view, bool pass) {
    for (PointerListNode *node = Begin(); node != head; node = node->next) {
        ActActor *actor = static_cast<ActActor *>(node->value);
        if (!actor->culled)
            actor->DrawWeapons(view, pass);
    }
}

// FUNC_AT(0x000130a0)
void ActActorDatabase::SetupFOVConversions(RViewCamera *view) {
    for (PointerListNode *node = Begin(); node != head; node = node->next) {
        ActActor *actor = static_cast<ActActor *>(node->value);
        if (!actor->culled)
            actor->SetupFOVConversion(view);
    }
}

// FUNC_AT(0x00012580)
void ActActorDatabase::PrepareActorsForCulling() {
    CullRestart = 1;
}

// FUNC_AT(0x00012f40)
int ActActorDatabase::GetNextActorCullInfo(Coord4 *sphere, float *height, bool *checkFar, float *farScale) {
    ActActorDatabase *database = ActorDatabase;
    PointerListNode *node;
    if (CullRestart == 1) {
        CullRestart = 0;
        node = database->Begin();
    } else {
        node = CullCursor->next;
    }
    CullCursor = node;
    if (node == database->head)
        return -1;
    ActActor *actor = static_cast<ActActor *>(node->value);
    const float *position = actor->matrices->world.mtx[3];
    sphere->x = position[0];
    sphere->y = position[1];
    sphere->z = position[2];
    sphere->w = 1.0f;
    *height = 2.0f;
    *checkFar = true;
    *farScale = 1.0f;
    return (int)(intptr_t)actor;
}

// FUNC_AT(0x0008c8d0)
void ActActorDatabase::SetActorCull(int item, bool culled, float distance) {
    ((ActActor *)(intptr_t)item)->culled = culled;
}

// FUNC_AT(0x00013880)
void ActActorDatabase::IncreaseSize(uint32_t count) {
    if (kMaxListSize - size < count)
        ThrowLengthError("list<T> too long");
    size += count;
}
