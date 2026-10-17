#ifndef DRIVING_EAGL_TRANSFORM_H_
#define DRIVING_EAGL_TRANSFORM_H_

// EAGL::Transform, the 4x4 matrix EAGL and EAGLAnim build and combine (row-major, row vectors: rows 0..2 the axes,
// row 3 the translation). See Transform.cpp and docs/driving/eagl.md 4.7. Ghidra names the class EAGL::Transform;
// the methods are under its names, the unnamed ones under invented names.

class Transform {
public:
    float m[16];

    void PostMult(const float *matrix);                                        // this = this * matrix
    void BuildTranslate(float x, float y, float z);                            // 0x000f1590 (invented)
    void BuildRotTrans4(const float *rotation9, const float *translation4);     // 0x000f1660
    void BuildRotTrans3(const float *rotation9, const float *translation3);     // 0x000f16d0
    void BuildAimedTrans(const float *aim, const float *up, int aimRow, int upRow);
    void BuildMatrix(const float *matrix);
    void ExtractQuatTrans(float *quaternion, float *translation) const;        // Ghidra: BuildQuatTrans
    void Transpose();
    static double Invert(const float *source, float *destination);
    static double Determinant(const float *matrix, int size);
    static double ElementMinor(const float *matrix, int row, int column, int size);
    void AppendScale(float x, float y, float z, float w);
    void AppendTranslate(float x, float y, float z);
    void AppendRotTrans4(const float *rotation9, const float *translation4);   // 0x000f2ae0
    void AppendRotTrans3(const float *rotation9, const float *translation3);   // 0x000f2b30
    void AppendMatrix(const float *matrix);
    void AppendQuatTrans(const float *quaternion, const float *translation);
    void AppendAimedTrans(const float *aim, const float *up, int aimRow, int upRow);
    void AppendRotate(float degrees, float x, float y, float z);
    void PrependScale(float x, float y, float z, float w);
    void PrependTranslate(float x, float y, float z);
    void PrependRotTrans(const float *rotation9, const float *translation4);
    void PrependMatrix(const float *matrix);
    void PrependQuatTrans(const float *quaternion, const float *translation);
    void PrependRotate(float degrees, float x, float y, float z);
    void Inverse();
    void BuildSQT(float sx, float sy, float sz, float qx, float qy, float qz, float qw, float tx, float ty, float tz);
    void TransformPoint(const float *in, float *out) const;

    // The inline methods compiled beside the actors' IK (anim/IK.cpp); names ours but BuildQT's.
    // out = the vector (x, y, z, 0) through the matrix, three floats.                              0x000161a0
    void TransformVector(const float *in, float *out) const;
    // The inverse of a rotation and translation: out's 3x3 the transpose of this one's, its translation
    // -t * R^T. out's fourth column is not written.                                                0x00016250
    void GetOrthoInverse(Transform *out) const;
    // The rotation of a quaternion (x, y, z, w) and a translation; the fourth column 0, 0, 0, 1.  0x000162e0
    void BuildQT(float qx, float qy, float qz, float qw, float tx, float ty, float tz);
    // The 3x3 rotation of `degrees` about the axis (normalised here); the rest of the matrix is kept. 0x000163e0
    void BuildRotation(float degrees, float x, float y, float z);
};
static_assert(sizeof(Transform) == 64, "a Transform is a MATRIX4");

// The quaternion <-> 3x3 conversions the Transform methods use (0x000f3090, 0x000f3160; invented names).
void EAGL_QuatToRotation(const float *quaternion, float *rotation9);
void EAGL_RotationToQuat(const float *rotation9, float *quaternion);

// Compiled beside the actors' IK too, and used by EAGLAnim (names ours; quaternions x, y, z, w): atan2(|a x b|, a.b)
// in radians, left unrounded on the x87, and the product a * b.
double AngleBetweenVectors(const float *a, const float *b);                     // 0x00016530
void QuatProduct(const float *a, const float *b, float *out);                   // 0x00016820

// Transform::BuildRotate (0x000f32e0), in assembly: FSIN and FCOS results are used unrounded. thiscall, so this is
// __fastcall with the Transform in ECX and nothing in EDX.
void __fastcall EAGL_BuildRotate(Transform *self, int unusedEdx, float degrees, float x, float y, float z);

#endif // DRIVING_EAGL_TRANSFORM_H_
