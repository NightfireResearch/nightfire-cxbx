#include "Skeleton.h"
#include "AnimUntested.h"
#include "../Transform.h"
#include "../../platform/RealMath.h"

#include <string.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// EAGLAnim::Skeleton and BoneMask (docs/driving/eagl.md 4.11). The engine's posing (ActPoser, ActSkeleton, ActIK)
// calls PoseSQTToLocal, PoseLocalToGlobal, PoseSQTToGlobal, PoseGlobalToSkin and both GetStillPose every frame a
// character is drawn; those are bit for bit and checked by src/driving/devtools/SkelShadow.cpp on live data. The
// rest - masks, mirroring, bone length scaling, the QT/T/skin variants - only the never-built FnAnim types or
// nothing at all call: provisional, with EAGL_UNTESTED.
//
// The arithmetic is single float products and a reciprocal, each one operation rounded once to float - written in
// float, which gives the x87's bits at PC=53; matrix products go through the hook at 0x001cec7c, BuildSQT and
// Transpose through EAGL::Transform.
// ---------------------------------------------------------------------------------------------------------------

typedef void (*MultiplyHook)(float *out, const float *parent, const float *child);
#define Multiply (*(MultiplyHook *)0x001cec7c)

// The engine's Transform::BuildQT (thiscall): rotation quaternion and translation
#define Transform_BuildQT ((void (__fastcall *)(Transform *, int, float, float, float, float, float, float, float))0x000162e0)

static inline bool InMask(const BoneMask *mask, int bone) {
    return (mask->bits[bone >> 5] & (1u << (bone & 31))) != 0;
}

// FUNC_AT(0x001066f0)
void EAGLAnim_MultiplyMatrices(float *out, const float *parent, const float *child) {
    VU0_MATRIX4_mult(out, child, parent);
}

// ---- BoneMask

// FUNC_AT(0x000f88a0)
bool BoneMask::GetBone(int bone) const {
    EAGL_UNTESTED("BoneMask::GetBone");
    return InMask(this, bone);
}

// FUNC_AT(0x00106330)
BoneMask* BoneMask::ConstructCopy(const BoneMask *other) {
    EAGL_UNTESTED("BoneMask::BoneMask(BoneMask &)");
    memcpy(bits, other->bits, sizeof(bits));
    return this;
}

// FUNC_AT(0x00106350)
void BoneMask::SetAll(bool on) {
    EAGL_UNTESTED("BoneMask::SetAll");
    memset(bits, on ? 0xff : 0, sizeof(bits));
}

// FUNC_AT(0x00106380)
void BoneMask::SetBone(int bone, bool on) {
    EAGL_UNTESTED("BoneMask::SetBone");
    if (on)
        bits[bone >> 5] |= 1u << (bone & 31);
    else
        bits[bone >> 5] &= ~(1u << (bone & 31));
}

// FUNC_AT(0x001063c0)
void BoneMask::AndAssign(const BoneMask *other) {
    EAGL_UNTESTED("BoneMask::operator&=");
    for (int i = 0; i < 8; i++)
        bits[i] &= other->bits[i];
}

// FUNC_AT(0x00106410)
void BoneMask::OrAssign(const BoneMask *other) {
    EAGL_UNTESTED("BoneMask::operator|=");
    for (int i = 0; i < 8; i++)
        bits[i] |= other->bits[i];
}

// FUNC_AT(0x00106460)
void BoneMask::XorAssign(const BoneMask *other) {
    EAGL_UNTESTED("BoneMask::operator^=");
    for (int i = 0; i < 8; i++)
        bits[i] ^= other->bits[i];
}

// FUNC_AT(0x001064b0)
BoneMask* BoneMask::And(BoneMask *result, const BoneMask *other) const {
    EAGL_UNTESTED("BoneMask::operator&");
    BoneMask t = *this;
    for (int i = 0; i < 8; i++)
        t.bits[i] &= other->bits[i];
    *result = t;
    return result;
}

// FUNC_AT(0x00106540)
BoneMask* BoneMask::Or(BoneMask *result, const BoneMask *other) const {
    EAGL_UNTESTED("BoneMask::operator|");
    BoneMask t = *this;
    for (int i = 0; i < 8; i++)
        t.bits[i] |= other->bits[i];
    *result = t;
    return result;
}

// FUNC_AT(0x001065d0)
BoneMask* BoneMask::Xor(BoneMask *result, const BoneMask *other) const {
    EAGL_UNTESTED("BoneMask::operator^");
    BoneMask t = *this;
    for (int i = 0; i < 8; i++)
        t.bits[i] ^= other->bits[i];
    *result = t;
    return result;
}

// FUNC_AT(0x00106660)
bool BoneMask::Equals(const BoneMask *other) const {
    EAGL_UNTESTED("BoneMask::operator==");
    for (int i = 0; i < 8; i++)
        if (bits[i] != other->bits[i])
            return false;
    return true;
}

// FUNC_AT(0x00106690)
BoneMask* BoneMask::Construct(bool on) {
    EAGL_UNTESTED("BoneMask::BoneMask(bool)");
    memset(bits, on ? 0xff : 0, sizeof(bits));
    return this;
}

// The shipped operator~ inverts nothing: it returns a copy.
// FUNC_AT(0x001066c0)
BoneMask* BoneMask::Not(BoneMask *result) const {
    EAGL_UNTESTED("BoneMask::operator~");
    BoneMask t = *this;
    *result = t;
    return result;
}

// ---- masks over the hierarchy

// The bone and its ancestors down to index 'lowest', and with 'descendants' every later bone under it.
// FUNC_AT(0x000f88c0)
void Skeleton::BuildBoneMask(int bone, bool descendants, BoneMask *mask, int lowest) {
    EAGL_UNTESTED("Skeleton::BuildBoneMask");
    memset(mask->bits, 0, sizeof(mask->bits));
    int b = bone;
    do {
        mask->bits[b >> 5] |= 1u << (b & 31);
        b = bones[b].parent;
    } while (b >= lowest);
    if (!descendants)
        return;
    for (int i = count - 1; i > bone; i--) {
        int a = bones[i].parent;
        while (a > bone)
            a = bones[a].parent;
        if (a == bone)
            mask->bits[i >> 5] |= 1u << (i & 31);
    }
}

// FUNC_AT(0x000f8940)
void Skeleton::BuildMirrorBoneMask(const BoneMask *source, BoneMask *destination) {
    EAGL_UNTESTED("Skeleton::BuildMirrorBoneMask");
    BoneMask copy = *source;
    memset(destination->bits, 0, sizeof(destination->bits));
    for (int i = 0; i < count; i++)
        if (InMask(&copy, i)) {
            int m = bones[i].mirror;
            destination->bits[m >> 5] |= 1u << (m & 31);
        }
}

// In place (source == destination), bits set on the way are read again further on, as in the original.
// FUNC_AT(0x000f8a10)
void Skeleton::BuildSymmetricBoneMask(const BoneMask *source, BoneMask *destination) {
    EAGL_UNTESTED("Skeleton::BuildSymmetricBoneMask");
    if (source == destination) {
        for (int i = 0; i < count; i++)
            if (InMask(source, i)) {
                int m = bones[i].mirror;
                destination->bits[m >> 5] |= 1u << (m & 31);
            }
        return;
    }
    memset(destination->bits, 0, sizeof(destination->bits));
    for (int i = 0; i < count; i++)
        if (InMask(source, i)) {
            int m = bones[i].mirror;
            destination->bits[m >> 5] |= 1u << (m & 31);
            destination->bits[i >> 5] |= 1u << (i & 31);
        }
}

// ---- bone length scaling

// FUNC_AT(0x000f8ad0)
void Skeleton::GetBoneLengthScale(float *out) const {
    EAGL_UNTESTED("Skeleton::GetBoneLengthScale");
    for (int i = 0; i < count; i++)
        out[i] = bones[i].scale[0];
}

// FUNC_AT(0x000f8b00)
void Skeleton::RestoreBoneLengthScale(const float *in) {
    EAGL_UNTESTED("Skeleton::RestoreBoneLengthScale");
    for (int i = 0; i < count; i++)
        bones[i].scale[0] = in[i];
    lengthScales = NULL;
}

// Scales the bone's length and gives its children the reciprocal in 'scales' (which becomes the length scales,
// set to 1 the first time).
// FUNC_AT(0x000f8b30)
void Skeleton::ScaleBoneLength(int bone, float scale, float *scales) {
    EAGL_UNTESTED("Skeleton::ScaleBoneLength");
    float reciprocal = 1.0f / scale;
    if (lengthScales != scales) {
        for (int i = 0; i < count; i++)
            scales[i] = 1.0f;
        lengthScales = scales;
    }
    bones[bone].scale[0] = scale * bones[bone].scale[0];
    for (int j = bone + 1; j < count; j++)
        if (bones[j].parent == bone)
            scales[j] = reciprocal * scales[j];
}

// ---- mirroring: a bone's pose goes to its mirror bone with the quaternion's x and y and the translation's z
// negated. Without keepRoot the root is turned round as well.

static void MirrorSwap(float *a, float *b) {
    float t;
    t = b[4]; b[4] = -a[4]; a[4] = -t;
    t = b[5]; b[5] = -a[5]; a[5] = -t;
    for (int c = 6; c < 10; c++) {
        float u = a[c], v = b[c];
        b[c] = u;
        a[c] = v;
    }
    t = b[10]; b[10] = -a[10]; a[10] = -t;
}

static void MirrorSelf(float *a) {
    a[4] = -a[4];
    a[5] = -a[5];
    a[10] = -a[10];
}

static void MirrorCopy(float *d, const float *s) {
    memcpy(&d[0], &s[0], 3 * sizeof(float));
    d[4] = -s[4];
    d[5] = -s[5];
    memcpy(&d[6], &s[6], 4 * sizeof(float));
    d[10] = -s[10];
}

// The root turned round: its quaternion (x, y, z, w) becomes (z, w, -x, -y), its translation's x and z negated.
static void MirrorRoot(float *d) {
    float x = d[4], y = d[5], z = d[6], w = d[7];
    d[6] = -x;
    d[7] = -y;
    d[4] = z;
    d[5] = w;
    d[8] = -d[8];
    d[10] = -d[10];
}

static void MirrorBone(const Skeleton *s, int i, float *source, float *destination) {
    int m = s->bones[i].mirror;
    if (source == destination) {
        if (m > i)
            MirrorSwap(&destination[i * 12], &destination[m * 12]);
        else if (m == i)
            MirrorSelf(&destination[i * 12]);
    } else {
        MirrorCopy(&destination[m * 12], &source[i * 12]);
    }
}

// FUNC_AT(0x000f8be0)
void Skeleton::MirrorPose(float *source, float *destination, bool keepRoot, const BoneMask *mask) {
    EAGL_UNTESTED("Skeleton::MirrorPose");
    for (int i = 0; i < count; i++)
        if (mask == NULL || InMask(mask, i))
            MirrorBone(this, i, source, destination);
    if (!keepRoot)
        MirrorRoot(destination);
}

// FUNC_AT(0x000f9850)
void Skeleton::MirrorPoseRange(int first, int last, float *source, float *destination, bool keepRoot) {
    EAGL_UNTESTED("Skeleton::MirrorPose(int, int, ...)");
    for (int i = first; i <= last; i++)
        MirrorBone(this, i, source, destination);
    if (!keepRoot)
        MirrorRoot(destination);
}

// ---- poses to matrices

static inline void BuildBone(Transform *t, const float *p) {
    t->BuildSQT(p[0], p[1], p[2], p[4], p[5], p[6], p[7], p[8], p[9], p[10]);
}

static inline void ScaleFirstColumn(Transform *t, const float *p) {
    t->m[0] = p[3] * t->m[0];
    t->m[4] = p[3] * t->m[4];
    t->m[8] = p[3] * t->m[8];
}

// FUNC_AT(0x000f9df0)
void Skeleton::PoseSQTToLocal(const float *pose, Transform *local, const BoneMask *mask) {
    int n = count;
    for (int i = 0; i < n; i++) {
        if (mask != NULL && !InMask(mask, i))
            continue;
        BuildBone(&local[i], &pose[i * 12]);
        ScaleFirstColumn(&local[i], &pose[i * 12]);
    }
}

// FUNC_AT(0x000f9f10)
void Skeleton::PoseLocalToGlobal(const Transform *local, Transform *global, const BoneMask *mask) {
    int n = count;
    for (int i = 0; i < n; i++) {
        if (mask != NULL && !InMask(mask, i))
            continue;
        int parent = bones[i].parent;
        if (parent >= 0)
            Multiply(global[i].m, global[parent].m, local[i].m);
        else
            memmove(&global[i], &local[i], sizeof(Transform));
    }
}

// FUNC_AT(0x000fa130)
void Skeleton::PoseSQTToGlobal(const float *pose, Transform *global, const BoneMask *mask) {
    int n = count;
    for (int i = 0; i < n; i++) {
        if (mask != NULL && !InMask(mask, i))
            continue;
        BuildBone(&global[i], &pose[i * 12]);
        ScaleFirstColumn(&global[i], &pose[i * 12]);
        int parent = bones[i].parent;
        if (parent >= 0)
            Multiply(global[i].m, global[parent].m, global[i].m);
    }
}

// FUNC_AT(0x000fa290)
void Skeleton::PoseGlobalToSkin(const Transform *global, Transform *skin, const BoneMask *mask) {
    int n = count;
    for (int i = 0; i < n; i++) {
        if (mask != NULL && !InMask(mask, i))
            continue;
        Multiply(skin[i].m, global[i].m, bones[i].inverseBind.m);
        skin[i].Transpose();
    }
}

static inline void StillPose(const Skeleton *s, int i, float *p, const float *lengthScale) {
    const SkeletonBone *b = &s->bones[i];
    memcpy(&p[0], b->scale, 12);
    if (lengthScale != NULL)
        p[3] = *lengthScale;
    else
        p[3] = 1.0f;
    memcpy(&p[4], b->rotation, 16);
    memcpy(&p[8], b->translation, 12);
    p[11] = 1.0f;
}

// FUNC_AT(0x000fa340)
void Skeleton::GetStillPose(float *pose, const BoneMask *mask) {
    int n = count;
    for (int i = 0; i < n; i++) {
        if (mask != NULL && !InMask(mask, i))
            continue;
        StillPose(this, i, &pose[i * 12], lengthScales != NULL ? &lengthScales[i] : NULL);
    }
}

// FUNC_AT(0x000fa560)
void Skeleton::GetStillPoseBone(int bone, float *pose) const {
    StillPose(this, bone, pose, NULL);
}

// FUNC_AT(0x000fa5c0)
void Skeleton::OrthoScaleBone(int bone, const Transform *scale) {
    EAGL_UNTESTED("Skeleton::OrthoScaleBone");
    bones[bone].inverseBind.PostMult(scale->m);
}

// FUNC_AT(0x000fa5e0)
void Skeleton::GetStillTrans(float *out) const {
    EAGL_UNTESTED("Skeleton::GetStillTrans");
    for (int i = 0; i < count; i++)
        memcpy(&out[i * 3], bones[i].translation, 12);
}

// The bone's global matrix alone: its chain built from the root down (at most 20 deep).
// FUNC_AT(0x000fa610)
void Skeleton::PoseBoneSQTToGlobal(int bone, const float *pose, Transform *global) {
    EAGL_UNTESTED("Skeleton::PoseBoneSQTToGlobal");
    int chain[20];
    int n = 0;
    for (int b = bone; b >= 0; b = bones[b].parent)
        chain[n++] = b;
    BuildBone(global, &pose[chain[n - 1] * 12]);
    for (int k = n - 2; k >= 0; k--) {
        Transform t;
        BuildBone(&t, &pose[chain[k] * 12]);
        Multiply(global->m, global->m, t.m);
    }
}

// FUNC_AT(0x000fa6f0)
void Skeleton::PoseQTToGlobal(int first, int last, const float *pose, Transform *global) {
    EAGL_UNTESTED("Skeleton::PoseQTToGlobal");
    for (int i = first; i <= last; i++) {
        const float *p = &pose[i * 12];
        Transform_BuildQT(&global[i], 0, p[4], p[5], p[6], p[7], p[8], p[9], p[10]);
        int parent = bones[i].parent;
        if (parent >= 0)
            Multiply(global[i].m, global[parent].m, global[i].m);
    }
}

// FUNC_AT(0x000fa770)
void Skeleton::PoseTToGlobal(int first, int last, const float *pose, Transform *global) {
    EAGL_UNTESTED("Skeleton::PoseTToGlobal");
    for (int i = first; i <= last; i++) {
        const float *p = &pose[i * 12];
        global[i].BuildTranslate(p[8], p[9], p[10]);
        int parent = bones[i].parent;
        if (parent >= 0)
            Multiply(global[i].m, global[parent].m, global[i].m);
    }
}

// FUNC_AT(0x000fa7e0)
void Skeleton::PoseQTToSkin(int first, int last, const float *pose, Transform *skin) {
    EAGL_UNTESTED("Skeleton::PoseQTToSkin");
    PoseQTToGlobal(first, last, pose, skin);
    for (int i = first; i <= last; i++) {
        Multiply(skin[i].m, skin[i].m, bones[i].inverseBind.m);
        skin[i].Transpose();
    }
}

// FUNC_AT(0x000fa850)
void Skeleton::PoseSQTToSkin(const float *pose, Transform *skin, const BoneMask *mask) {
    EAGL_UNTESTED("Skeleton::PoseSQTToSkin");
    int n = count;
    PoseSQTToGlobal(pose, skin, NULL);
    for (int i = 0; i < n; i++) {
        if (mask != NULL && !InMask(mask, i))
            continue;
        Multiply(skin[i].m, skin[i].m, bones[i].inverseBind.m);
        skin[i].Transpose();
    }
}

// FUNC_AT(0x000fa900)
void Skeleton::PoseTrans(const float *translations, Transform *skin) {
    EAGL_UNTESTED("Skeleton::PoseTrans");
    for (int i = 0; i < count; i++) {
        float *m = skin[i].m;
        static const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        memcpy(m, identity, sizeof(identity));
        memcpy(&m[12], &translations[i * 3], 12);
        m[15] = 1.0f;
        int parent = bones[i].parent;
        if (parent >= 0)
            Multiply(m, skin[parent].m, m);
    }
    PoseGlobalToSkin(skin, skin, NULL);
}
