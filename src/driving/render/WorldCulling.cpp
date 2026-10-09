#pragma fp_contract(off)

#include "WorldCulling.h"

#include <bit>
#include <math.h>

#include "../camera/WorldCamera.h"        // TransformPointXZ
#include "../platform/RealMath.h"

// ---------------------------------------------------------------------------------------------------------------
// RRenderWorldCulling (0x0008d180-0x0008d610), ported from the listing. The x87 chains are written in double in the
// original's order. Tested by devtools/SceneObjShadow on random frustums.
// ---------------------------------------------------------------------------------------------------------------

namespace {

constexpr float kDegreesToTurns = 1.0f / 360.0f;
constexpr float kEyePullBack = -0.2f;           // of the range, times how far the camera looks up or down
static_assert(std::bit_cast<uint32_t>(kDegreesToTurns) == 0x3b360b61 && std::bit_cast<uint32_t>(kEyePullBack) == 0xbe4ccccd,
              "the original's constants");

// sin_fractionalangle leaves FSIN's result in ST0, which InitializePlaneInfo multiplies unrounded: read as a double
double SineTurns(float turns) {
    return reinterpret_cast<double (*)(float)>(&sin_fractionalangle)(turns);
}

// A plane's distance to a point in the ground plane, less its own distance: the columns of the planes' matrix
double ColumnDistance(const MATRIX4 *planes, int column, const Coord4 *point) {
    return (double)planes->mtx[0][column] * point->x + (double)planes->mtx[2][column] * point->z -
           planes->mtx[3][column];
}

} // namespace

// FUNC_AT(0x0008d420)
RRenderWorldCulling* RRenderWorldCulling::Construct(float fov, float viewRange) {
    InitializePlaneInfo(fov, viewRange);
    return this;
}

// FUNC_AT(0x0008d370)
void RRenderWorldCulling::InitializePlaneInfo(float fov, float viewRange) {
    float turns = fov * kDegreesToTurns;
    fieldOfView = fov;
    range = viewRange;
    cosine = cos_fractionalangle(turns);
    double s = SineTurns(turns);
    sine = (float)s;
    // The edge from the eye to the far point, in (x, z): its length and direction
    Coord4 edge;
    edge.x = (float)(s * range);
    edge.y = (float)(range - (double)range * cosine);
    edge.z = 0.0f;
    edge.w = 0.0f;
    edgeLength = VU0_v3length(&edge);
    double inverse = 1.0 / edgeLength;
    centreLocal.x = 0.0f;
    centreLocal.y = 0.0f;
    edgeX = (float)(edge.x * inverse);
    edgeZ = (float)(inverse * edge.y);
    centreLocal.z = range * 0.5f;
}

// FUNC_AT(0x0008d440)
void RRenderWorldCulling::Setup2dFrustrum(const Coord4 *position, const MATRIX4 *frame, float fov, float viewRange) {
    if (range != viewRange || fieldOfView != fov)
        InitializePlaneInfo(fov, viewRange);
    eyeY = position->y;
    Coord4 forward = *MatrixRow(frame, 2);
    VU0_v4copy(position, &eye);
    float heading = atan_turns(forward.x, forward.z);

    // The planes facing the camera's heading of zero
    planes[0].x = -edgeZ;
    planes[0].y = 0.0f;
    planes[0].z = edgeX;
    planes[1].x = edgeZ;
    planes[1].y = 0.0f;
    planes[1].z = edgeX;
    planes[2].x = -cosine;
    planes[2].y = 0.0f;
    planes[2].z = -sine;
    planes[3].x = cosine;
    planes[3].y = 0.0f;
    planes[3].z = -sine;
    planes[4].x = 0.0f;
    planes[4].y = 0.0f;
    planes[4].z = -1.0f;

    MATRIX4 rotation;
    VU0_MATRIX4setyrot(&rotation, heading);
    VU0_MATRIX4_vect3rotate(&centreLocal, &rotation, &centre);
    centre.x = position->x + centre.x;
    centre.z = position->z + centre.z;
    for (int i = 0; i < 5; i++)
        VU0_MATRIX4_vect3rotate(&planes[i], &rotation, &planes[i]);

    // The eye pulled back as the camera looks up or down, the far point the range ahead of it
    Coord4 *farPoint = MatrixRow(&rotation, 2);
    VU0_v4scaleadd(farPoint, (float)(fabs(frame->mtx[2][1]) * viewRange * kEyePullBack), &eye, &eye);
    VU0_v4scaleadd(farPoint, viewRange, &eye, farPoint);
    planes[0].w = (float)((double)farPoint->x * planes[0].x + (double)farPoint->z * planes[0].z);
    planes[1].w = (float)((double)farPoint->z * planes[1].z + (double)farPoint->x * planes[1].x);
    planes[2].w = (float)((double)eye.x * planes[2].x + (double)planes[2].z * eye.z);
    planes[3].w = (float)((double)eye.x * planes[3].x + (double)planes[3].z * eye.z);
    planes[4].w = (float)((double)eye.x * planes[4].x + (double)planes[4].z * eye.z);
    VU0_MATRIX4_transpose(planes, planes);
}

// FUNC_AT(0x0008d180)
bool RRenderWorldCulling::IsInFrustum2d(const Coord4 *sphere, float radius, bool checkFar, float farScale,
                                        float *distance) {
    const Coord4 &nearPlane = planes[4];
    float eyeDistance = 0.0f;
    if (((double)nearPlane.y * sphere->y + (double)nearPlane.z * sphere->z + (double)sphere->x * nearPlane.x) -
            nearPlane.w > radius)
        return false;
    if (checkFar) {
        double dx = (double)eye.x - sphere->x;
        double dz = (double)eye.z - sphere->z;
        eyeDistance = VU0_sqrt((float)(dz * dz + dx * dx));
        if ((double)farScale * range < (double)eyeDistance - radius)
            return false;
    }
    if (ColumnDistance(Matrix(), 2, sphere) > radius || ColumnDistance(Matrix(), 3, sphere) > radius)
        return false;
    if (distance != NULL)
        *distance = eyeDistance;
    return true;
}

// FUNC_AT(0x0008d250)
int RRenderWorldCulling::QuadtreeFrustrumCheck2d(const Coord4 *box, float radius) {
    const Coord4 &nearPlane = planes[4];
    if (((double)nearPlane.z * box->z + (double)nearPlane.y * box->y + (double)box->x * nearPlane.x) -
            nearPlane.w > radius)
        return kOutside;
    Coord4 distances;
    TransformPointXZ(reinterpret_cast<const Coord3 *>(box), Matrix(), &distances);
    float negativeRadius = -radius;
    bool inside = true;
    bool crossing = false;
    const float side[4] = { distances.x, distances.y, distances.z, distances.w };
    for (int i = 0; i < 4; i++) {
        if (side[i] > 0.0f) {
            if (side[i] > radius)
                inside = false;
            else
                crossing = true;
        } else if (side[i] > negativeRadius) {
            crossing = true;
        }
    }
    if (!inside)
        return kOutside;
    return crossing ? kCrossing : kInside;
}
