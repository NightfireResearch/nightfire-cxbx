#ifndef DRIVING_RENDER_WORLDCULLING_H_
#define DRIVING_RENDER_WORLDCULLING_H_

// ---------------------------------------------------------------------------------------------------------------
// RRenderWorldCulling (0xa0 bytes, one, fgWorldCulling): the culling's view of the camera in the ground plane (x,
// z). Setup2dFrustrum makes five planes from the camera's position, heading, field of view and range: two edges
// through the far point, two sides and a near plane through the eye (pulled back as the camera looks up or down).
// The first four end up in the columns of a matrix, so that one transform answers all four distances. WRender's
// tree walk and RRenderWorldCamera::CullModule test against them. See WorldCulling.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../world/CollisionTypes.h"      // MATRIX4, Coord3, Coord4

class RRenderWorldCulling {
public:
    // QuadtreeFrustrumCheck2d's answers
    enum Containment : int {
        kOutside = 0,
        kInside = 1,
        kCrossing = 2,
    };

    Coord4 planes[5];                   // +0x00 (x, 0, z, distance); 0-3 transposed into columns (Matrix())
    Coord4 eye;                         // +0x50 the camera's position, pulled back
    float edgeLength;                   // +0x60 InitializePlaneInfo's edge's length
    float fieldOfView;                  // +0x64 in degrees
    float range;                        // +0x68
    float cosine;                       // +0x6c of the field of view over 360, in turns
    Coord3 centre;                      // +0x70 the middle of the range, turned to the heading, at the eye
    float edgeZ;                        // +0x7c the edge's direction, z
    Coord3 centreLocal;                 // +0x80 (0, 0, range / 2)
    float sine;                         // +0x8c
    float edgeX;                        // +0x90 the edge's direction, x
    uint8_t unknown94[8];
    float eyeY;                         // +0x9c the camera's height

    // The first four planes as a matrix: plane n is column n
    const MATRIX4 *Matrix() const { return reinterpret_cast<const MATRIX4 *>(planes); }

    RRenderWorldCulling* Construct(float fieldOfView, float range);                             // 0x0008d420
    // The planes' shape for a field of view and range (what does not depend on the camera's frame)
    void InitializePlaneInfo(float fieldOfView, float range);                                   // 0x0008d370
    void Setup2dFrustrum(const Coord4 *position, const MATRIX4 *frame, float fieldOfView, float range);   // 0x0008d440
    // Whether a circle is inside the near plane and the two sides (and, with `checkFar`, within farScale times the
    // range of the eye); *distance (if not NULL) the distance from the eye when that was tested, else 0
    bool IsInFrustum2d(const Coord4 *sphere, float radius, bool checkFar, float farScale, float *distance);   // 0x0008d180
    // A square of the scene tree (its middle) against all five planes: Containment
    int QuadtreeFrustrumCheck2d(const Coord4 *box, float radius);                               // 0x0008d250
};
static_assert(sizeof(RRenderWorldCulling) == 0xa0, "RRenderWorldCulling is 0xa0 bytes");
static_assert(offsetof(RRenderWorldCulling, eye) == 0x50 && offsetof(RRenderWorldCulling, fieldOfView) == 0x64 &&
              offsetof(RRenderWorldCulling, centre) == 0x70 && offsetof(RRenderWorldCulling, edgeX) == 0x90 &&
              offsetof(RRenderWorldCulling, eyeY) == 0x9c, "RRenderWorldCulling layout");

#define fgWorldCulling (*(RRenderWorldCulling *)0x001f2c80)

#endif // DRIVING_RENDER_WORLDCULLING_H_
