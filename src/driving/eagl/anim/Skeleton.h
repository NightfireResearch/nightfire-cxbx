#ifndef DRIVING_EAGL_ANIM_SKELETON_H_
#define DRIVING_EAGL_ANIM_SKELETON_H_

// EAGLAnim::Skeleton and EAGLAnim::BoneMask (docs/driving/eagl.md 4.11). See Skeleton.cpp. Names are the PS2 symbol
// sheet's, matched by argument count and order; Ghidra has the Xbox functions unnamed.

#include "../Transform.h"

#include <stddef.h>
#include <stdint.h>

struct BoneMask {                    // a bit per bone, 256 bones
    uint32_t bits[8];

    BoneMask* ConstructCopy(const BoneMask *other);              // 0x00106330 BoneMask(BoneMask &)
    void SetAll(bool on);                                        // 0x00106350
    void SetBone(int bone, bool on);                             // 0x00106380
    void AndAssign(const BoneMask *other);                       // 0x001063c0 operator&=
    void OrAssign(const BoneMask *other);                        // 0x00106410 operator|=
    void XorAssign(const BoneMask *other);                       // 0x00106460 operator^=
    BoneMask* And(BoneMask *result, const BoneMask *other) const;   // 0x001064b0 operator&
    BoneMask* Or(BoneMask *result, const BoneMask *other) const;    // 0x00106540 operator|
    BoneMask* Xor(BoneMask *result, const BoneMask *other) const;   // 0x001065d0 operator^
    bool Equals(const BoneMask *other) const;                    // 0x00106660 operator==
    BoneMask* Construct(bool on);                                // 0x00106690 BoneMask(bool)
    BoneMask* Not(BoneMask *result) const;                       // 0x001066c0 operator~ (as built: a copy)
    bool GetBone(int bone) const;                                // 0x000f88a0 (inline in the PS2 build)

    // The bit test the callers inline (GetBone's body)
    bool Has(int bone) const {
        return (bits[bone >> 5] & (1u << (bone & 31))) != 0;
    }
};
static_assert(sizeof(BoneMask) == 32, "a BoneMask is 256 bits");

struct SkeletonBone {                // 0x70
    float scale[3];                  // +0x00 the still pose's scale (x is the bone's length scale)
    int32_t parent;                  // +0x0c -1 for a root
    float rotation[4];               // +0x10 the still pose's quaternion
    float translation[3];            // +0x20
    int32_t mirror;                  // +0x2c the bone mirroring this one (itself on the centre line)
    Transform inverseBind;           // +0x30 global -> skin
};
static_assert(sizeof(SkeletonBone) == 0x70, "a skeleton bone is 0x70 bytes");

// A pose is 12 floats a bone: scale xyz, a scale applied to the matrix's first column, quaternion xyzw,
// translation xyz, and a 1.
struct Skeleton {
    uint32_t unknown00;
    uint32_t unknown04;
    int32_t count;                   // +0x08
    float *lengthScales;             // +0x0c per bone, the 4th pose float of GetStillPose; NULL for 1
    SkeletonBone bones[1];           // +0x10 [count]

    void BuildBoneMask(int bone, bool descendants, BoneMask *mask, int lowest);           // 0x000f88c0
    void BuildMirrorBoneMask(const BoneMask *source, BoneMask *destination);             // 0x000f8940
    void BuildSymmetricBoneMask(const BoneMask *source, BoneMask *destination);          // 0x000f8a10
    void GetBoneLengthScale(float *out) const;                                           // 0x000f8ad0
    void RestoreBoneLengthScale(const float *in);                                        // 0x000f8b00
    void ScaleBoneLength(int bone, float scale, float *lengthScales);                    // 0x000f8b30
    void MirrorPose(float *source, float *destination, bool keepRoot, const BoneMask *mask);   // 0x000f8be0
    void MirrorPoseRange(int first, int last, float *source, float *destination, bool keepRoot);   // 0x000f9850
    void PoseSQTToLocal(const float *pose, Transform *local, const BoneMask *mask);       // 0x000f9df0
    void PoseLocalToGlobal(const Transform *local, Transform *global, const BoneMask *mask);   // 0x000f9f10
    void PoseSQTToGlobal(const float *pose, Transform *global, const BoneMask *mask);     // 0x000fa130
    void PoseGlobalToSkin(const Transform *global, Transform *skin, const BoneMask *mask);    // 0x000fa290
    void GetStillPose(float *pose, const BoneMask *mask);                                // 0x000fa340
    void GetStillPoseBone(int bone, float *pose) const;                                  // 0x000fa560
    void OrthoScaleBone(int bone, const Transform *scale);                               // 0x000fa5c0
    void GetStillTrans(float *out) const;                                                // 0x000fa5e0
    void PoseBoneSQTToGlobal(int bone, const float *pose, Transform *global);             // 0x000fa610
    void PoseQTToGlobal(int first, int last, const float *pose, Transform *global);      // 0x000fa6f0
    void PoseTToGlobal(int first, int last, const float *pose, Transform *global);       // 0x000fa770
    void PoseQTToSkin(int first, int last, const float *pose, Transform *skin);          // 0x000fa7e0
    void PoseSQTToSkin(const float *pose, Transform *skin, const BoneMask *mask);         // 0x000fa850
    void PoseTrans(const float *translations, Transform *skin);                          // 0x000fa900
};

static_assert(offsetof(Skeleton, bones) == 0x10, "a skeleton's bones are at +0x10");

// The default of the hierarchy multiply hook at 0x001cec7c: out = child * parent (VU0_MATRIX4_mult).
void EAGLAnim_MultiplyMatrices(float *out, const float *parent, const float *child);    // 0x001066f0

#endif // DRIVING_EAGL_ANIM_SKELETON_H_
