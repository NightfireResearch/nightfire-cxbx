#pragma fp_contract(off)

#include "Skeleton.h"

#include "Manager.h"
#include "../eagl/EaglGlobals.h"
#include "../eagl/Loader.h"
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// ActSkeleton and ActSkeletonDatabase (0x000197f0-0x00019d20), ported from the listings.
//
// The skeletons share one buffer of a matrix a bone, registered with EAGL as "EAGLAnimationBuffer" (loaded objects
// find it by that name): the first skeleton makes it, and any skeleton with more bones makes it again larger.
//
// The blends are x87: (b - a) * t + a in double, rounded at the store.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals

#define AnimationBufferBones I32_AT(0x001dda00)
#define SkeletonFiles ((const char (*)[0x5a])0x001b4ae8)   // data\actors\skeleton\0skel.o .. 8skel.o

static const char kAnimationBufferName[] = "EAGLAnimationBuffer";

// ---- ActSkeleton

// FUNC_AT(0x000198b0)
ActSkeleton* ActSkeleton::Construct(const char *path) {
    file = UFileLoader::FileLoadz(path, 0);
    size_t size = UMemory::Size(file);
    DynamicLoader *memory = static_cast<DynamicLoader *>(EaglMalloc(sizeof(DynamicLoader), "EAGL::DynamicLoader new"));
    loader = memory != NULL ? memory->Construct(file, size, ActManager::GetSymbolResolver()) : NULL;

    int index = 0;
    void *symbol;
    loader->GetNextAddr("Skeleton", &index, &symbol);
    skeleton = static_cast<Skeleton *>(symbol);

    int bones = skeleton->count;
    if (AnimationBuffer == NULL) {
        AnimationBuffer = static_cast<MATRIX4 *>(OperatorNewArray(bones * sizeof(MATRIX4)));
        DynamicLoader::RegisterVar(kAnimationBufferName, AnimationBuffer);
        AnimationBufferBones = bones;
    } else if (bones > AnimationBufferBones) {
        OperatorDelete(AnimationBuffer);
        DynamicLoader::UnRegisterVar(kAnimationBufferName);
        AnimationBuffer = static_cast<MATRIX4 *>(OperatorNewArray(bones * sizeof(MATRIX4)));
        DynamicLoader::RegisterVar(kAnimationBufferName, AnimationBuffer);
        AnimationBufferBones = bones;
    }

    stillPose = static_cast<float *>(OperatorNewArray(skeleton->count * 12 * sizeof(float)));
    pose = static_cast<float *>(OperatorNewArray(skeleton->count * 12 * sizeof(float)));
    matrices = static_cast<MATRIX4 *>(OperatorNewArray(bones * sizeof(MATRIX4)));
    skeleton->GetStillPose(stillPose, NULL);

    rootBone = GetBoneIndex("Root.Root");
    weaponBones[0] = GetBoneIndex("Root.Weapon");
    weaponBones[1] = GetBoneIndex("Root.Clip");
    if (weaponBones[0] == -1)
        weaponBones[0] = GetBoneIndex("Root.RWeapon");
    return this;
}

// FUNC_AT(0x00019c60)
void ActSkeleton::Destruct() {
    if (matrices != NULL)
        OperatorDelete(matrices);
    if (stillPose != NULL)
        OperatorDelete(stillPose);
    if (pose != NULL)
        OperatorDelete(pose);
    loader->Release();
    if (loader != NULL) {
        loader->Destruct();
        EaglFree(loader, sizeof(DynamicLoader));
    }
    UMemory::Free(file);
}

// FUNC_AT(0x000197f0)
int ActSkeleton::GetBoneIndex(const char *name) {
    void *bone;
    if (loader->GetAddr("Bone", name, &bone))
        return *static_cast<int32_t *>(bone);
    return -1;
}

// FUNC_AT(0x00019820)
void ActSkeleton::GetStillPose() {
    skeleton->GetStillPose(stillPose, NULL);
}

// FUNC_AT(0x00019830)
void ActSkeleton::GetStillPose(float *out) {
    skeleton->GetStillPose(out, NULL);
}

// FUNC_AT(0x00019ac0)
int ActSkeleton::GetNumBones() {
    return skeleton->count;
}

// FUNC_AT(0x00019870)
void LerpTranslation(float t, const float *a, const float *b, float *out) {
    out[0] = float((double(b[0]) - a[0]) * t + a[0]);
    out[1] = float((double(b[1]) - a[1]) * t + a[1]);
    out[2] = float((double(b[2]) - a[2]) * t + a[2]);
}

// FUNC_AT(0x00019850)
void ActSkeleton::BlendQ(Coord4 *rotation, const Coord4 *target, float t) {
    VU0_fastqslerp(rotation, target, rotation, t);
}

// FUNC_AT(0x00019b80)
void ActSkeleton::BlendQT(Coord4 *rotation, Coord4 *translation, const Coord4 *targetRotation,
                          const Coord4 *targetTranslation, float t) {
    VU0_fastqslerp(rotation, targetRotation, rotation, t);
    LerpTranslation(t, &translation->x, &targetTranslation->x, &translation->x);
}

// FUNC_AT(0x00019ad0)
void ActSkeleton::BlendBones(float t, const float *from, const float *to, float *out, bool allTranslations) {
    for (int bone = 0; bone < skeleton->count; bone++) {
        const float *a = from + bone * 12;
        const float *b = to + bone * 12;
        float *blended = out + bone * 12;
        VU0_fastqslerp(a + 4, b + 4, blended + 4, t);
        if (allTranslations || bone == rootBone)
            LerpTranslation(t, a + 8, b + 8, blended + 8);
    }
}

// ---- ActSkeletonDatabase

// FUNC_AT(0x00019bd0)
ActSkeletonDatabase* ActSkeletonDatabase::Construct() {
    AnimationBuffer = NULL;
    AnimationBufferBones = 0;
    for (int i = 0; i < kSkeletonCount; i++) {
        ActSkeleton *memory = static_cast<ActSkeleton *>(UMemory::FastAlloc(sizeof(ActSkeleton), "ActSkeleton"));
        skeletons[i] = memory != NULL ? memory->Construct(SkeletonFiles[i]) : NULL;
    }
    return this;
}

// FUNC_AT(0x00019cd0)
void ActSkeletonDatabase::Destruct() {
    for (int i = 0; i < kSkeletonCount; i++) {
        ActSkeleton *skeleton = skeletons[i];
        if (skeleton != NULL) {
            skeleton->Destruct();
            UMemory::FastFree(skeleton, sizeof(ActSkeleton));
        }
    }
    if (AnimationBuffer != NULL) {
        OperatorDelete(AnimationBuffer);
        DynamicLoader::UnRegisterVar(kAnimationBufferName);
    }
    AnimationBuffer = NULL;
}
