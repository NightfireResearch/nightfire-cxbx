#include "Vision.h"

#include "viewer.h"
#include "Collide.h"
#include "../game/obj/Door.h"
#include "../math/X87Ops.h"
#include "../util/Sort.h"

#include <string.h>

// XBE_GLOBAL(0x0029d79c, 0x4)
cel_tag *GfxList;

// What a view sees: the cels it is in or looks into through portals, each portal narrowing the view's pyramid to
// the part of the opening still in sight.

// Up to ten planes a depth: the view's five, then a portal's edges seen from the eye and the portal itself
#define VISION_DEPTHS 48
#define VISION_PLANES 10

// The eye and the far corners of the view's pyramid (200 ahead), in the world
// XBE_GLOBAL(0x0029e828, 0x3c)
_VECTOR ViewPoints[5];
// XBE_GLOBAL(0x0029e868, 0x94)
VisPortal PortalsOut;
// XBE_GLOBAL(0x0029e900, 0x94)
VisPortal PortalsIn;
// XBE_GLOBAL(0x0029e998, 0xc0)
int NumPortalPlanes[VISION_DEPTHS];
// XBE_GLOBAL(0x0029ea58, 0x4)
uint32_t VisionRecurseCount;
// XBE_GLOBAL(0x0029ea60, 0x1e00)
plane_equ_tag PortalPlanes[VISION_DEPTHS][VISION_PLANES];
// XBE_GLOBAL(0x002a0860, 0x10)
uchar PortalPointIn[16];
// XBE_GLOBAL(0x002a0870, 0x200)
VisCel VisCelList[64];
// XBE_GLOBAL(0x002a0a70, 0x2)
uint16_t VisCelCount;

// AUTOINJECT
bool Vision_InView(_VECTOR *point, float radius, viewer_tag *viewer) {

    // Measure the distance from the point to each of the 4 frustum planes
    for(int i = 0; i < 4; i++) {

        double distance = DistancePointToPlane(point, &viewer->frustumPlanes[i]);

        if(distance < -radius) {
            return false; // Point is outside the frustum
        }

    }

    return true;
}

// The cel on the other side of a portal of cel's that the segment from 'from' to 'to' goes through, or NULL
// AUTOINJECT
cel_tag* Cel_ObjectLeftCel(cel_tag *cel, _VECTOR *from, _VECTOR *to) {
    if (cel == NULL)
        return NULL;

    _VECTOR dir, unit;
    dir.x = to->x - from->x;
    dir.y = to->y - from->y;
    dir.z = to->z - from->z;
    float length = Vec_NormaliseLen(&unit, &dir);
    _VECTOR low, high;
    Vec_MinMax(from, to, &low, &high);
    for (portal_tag *portal = cel->portals; portal != NULL; portal = portal->next) {
        if (!Intersect_BoxBox(&low, &high, &portal->boxMin, &portal->boxMax))
            continue;
        float along;
        if (Collide_RayTriangle(from, &unit, &portal->corners[0], &portal->corners[1], &portal->corners[2], &along) &&
            along >= 0.0f && along <= length)
            return portal->cel;
        if (Collide_RayTriangle(from, &unit, &portal->corners[2], &portal->corners[3], &portal->corners[0], &along) &&
            along >= 0.0f && along <= length)
            return portal->cel;
    }
    return NULL;
}

// The view's pyramid out to 200: its corners, the five planes of depth 0, and the four sides for Vision_InView
// AUTOINJECT
void Vision_Calculate_View_Fustrum(viewer_tag *viewer) {
    Extended tangent = FPtan(viewer->fovRadians * 0.5);
    float wide = (float)(tangent * Extended(viewer->AspectRatio));
    Extended high = tangent * Extended(200.0f);
    Extended across = Extended(wide) * Extended(200.0f);
    _VECTOR corners[5] = {
        {0.0f, 0.0f, 0.0f},
        {(float)-across, (float)-high, 200.0f},
        {(float)across, (float)-high, 200.0f},
        {(float)-across, (float)high, 200.0f},
        {(float)across, (float)high, 200.0f},
    };
    for (int i = 0; i < 5; i++)
        MatrixMultiplyVector(&viewer->viewMatrix, &corners[i], &ViewPoints[i]);

    NumPortalPlanes[0] = 5;
    Plane_PlaneEq(&PortalPlanes[0][0], &ViewPoints[0], &ViewPoints[1], &ViewPoints[2]);
    Plane_PlaneEq(&PortalPlanes[0][1], &ViewPoints[0], &ViewPoints[3], &ViewPoints[1]);
    Plane_PlaneEq(&PortalPlanes[0][2], &ViewPoints[0], &ViewPoints[4], &ViewPoints[3]);
    Plane_PlaneEq(&PortalPlanes[0][3], &ViewPoints[0], &ViewPoints[2], &ViewPoints[4]);
    Plane_PlaneEq(&PortalPlanes[0][4], &ViewPoints[1], &ViewPoints[3], &ViewPoints[2]);
    memcpy(viewer->frustumPlanes, PortalPlanes[0], sizeof(viewer->frustumPlanes));
}

// AUTOINJECT
void Vision_Init_Portal_Recurse(viewer_tag *viewer) {
    VisCelCount = 0;
    _VECTOR dir;
    Mat_GetDir(&dir, &viewer->viewMatrix);
    Vision_Calculate_View_Fustrum(viewer);
}

// Where the edge from 'from' to 'to' crosses the plane, into out; an edge along the plane gives zero. The edge's dot
// with the normal is tested unrounded and divided by as a float; the fraction along it is stored as a float.
static void ClipEdge(plane_equ_tag *plane, _VECTOR *from, _VECTOR *to, _VECTOR *out) {
    _VECTOR edge;
    Vec_Subtract(to, from, &edge);
    double across = ((double)edge.y * plane->b + (double)edge.z * plane->c) + (double)edge.x * plane->a;
    float acrossStored = (float)across;
    if (across == 0.0) {
        Vec_Zero(out);
        return;
    }
    float along = (float)-(DistancePointToPlane(from, plane) / acrossStored);
    auxVec_AddMulR32(from, &edge, along, out);
}

// The part of the polygon in front of the plane, into out; false (out empty) when no corner is behind it. A corner
// on the plane counts as in front, a NaN as behind.
// AUTOINJECT
bool ClipPortalWithPlane(plane_equ_tag *plane, VisPortal *in, VisPortal *out) {
    out->count = 0;
    bool behind = false;
    for (int i = 0; i < in->count; i++) {
        if (DistancePointToPlane(&in->v[i], plane) >= 0.0) {
            PortalPointIn[i] = 1;
        } else {
            PortalPointIn[i] = 0;
            behind = true;
        }
    }
    if (!behind)
        return false;

    for (int i = 1; i < in->count + 1; i++) {
        int j = i == in->count ? 0 : i;
        _VECTOR *prev = &in->v[i - 1];
        if (PortalPointIn[j] == 1) {
            if (PortalPointIn[i - 1] == 1) {
                out->v[out->count] = in->v[j];
                out->count++;
            } else if (PortalPointIn[i - 1] == 0) {
                ClipEdge(plane, &in->v[j], prev, &out->v[out->count]);
                out->count++;
                out->v[out->count] = in->v[j];
                out->count++;
            }
        } else if (PortalPointIn[j] == 0 && PortalPointIn[i - 1] == 1) {
            ClipEdge(plane, &in->v[j], prev, &out->v[out->count]);
            out->count++;
        }
    }
    return true;
}

// The planes of the next depth from a clipped portal: one through the eye and each edge, then the portal's own.
// Returns how many.
static int PortalPlanesFrom(VisPortal *polygon, plane_equ_tag *planes, portal_tag *portal) {
    for (int i = 0; i < polygon->count; i++) {
        int next = i + 1 == polygon->count ? 0 : i + 1;
        Plane_PlaneEq(&planes[i], &ViewPoints[0], &polygon->v[i], &polygon->v[next]);
    }
    planes[polygon->count] = portal->plane;
    return polygon->count + 1;
}

// Looks through the cel's portals that face the eye and have no closed door: each is cut down by the planes of this
// depth (corners closer together than about 0.0003 merged), and what is left makes its cel visible and, with three
// corners or more, the planes of the next depth to look on through it. At most 46 deep and 100 calls a frame.
// AUTOINJECT
void Vision_Recurse(cel_tag *cel, int depth) {
    portal_tag *portal = cel->portals;
    if (VisionRecurseCount > 100)
        return;
    VisionRecurseCount++;
    if (depth >= 46 || portal == NULL)
        return;

    for (; portal != NULL; portal = portal->next) {
        if (!portal->open)
            continue;
        if (DistancePointToPlane(&ViewPoints[0], &portal->plane) >= 0.0)
            continue;
        if (Door_IsClosed(portal->door))
            continue;

        PortalsIn.count = 4;
        PortalsIn.v[0] = portal->corners[0];
        PortalsIn.v[1] = portal->corners[1];
        PortalsIn.v[2] = portal->corners[2];
        PortalsIn.v[3] = portal->corners[3];
        for (int k = 0; k < NumPortalPlanes[depth]; k++) {
            if (!ClipPortalWithPlane(&PortalPlanes[depth][k], &PortalsIn, &PortalsOut))
                continue;
            PortalsIn.count = 0;
            for (int i = 0; i < PortalsOut.count; i++) {
                int next = i + 1 == PortalsOut.count ? 0 : i + 1;
                if (Vec_SqDist3D(&PortalsOut.v[i], &PortalsOut.v[next]) > 1e-07f) {
                    PortalsIn.v[PortalsIn.count] = PortalsOut.v[i];
                    PortalsIn.count++;
                }
            }
        }
        if (PortalsIn.count == 0)
            continue;

        portal->seen = 1;
        portal->partner->seen = 1;
        Vision_AddCelToDraw(portal->cel, depth + 1);
        if (PortalsIn.count > 2) {
            NumPortalPlanes[depth + 1] = PortalPlanesFrom(&PortalsIn, PortalPlanes[depth + 1], portal);
            Vision_Recurse(portal->cel, depth + 1);
            if (VisionRecurseCount > 100)
                return;
        }
    }
}

// The cel the view is in and what it sees from there
// AUTOINJECT
void Vision_Portal_Recurse(viewer_tag *viewer) {
    cel_tag *cel = viewer->cel;
    if (cel == NULL)
        return;

    Vision_AddCelToDraw(cel, 0);
    VisionRecurseCount = 0;
    Vision_Recurse(cel, 0);
    vision_generate_display_list(viewer);
}

// Adds a cel to the frame's list, once, up to 64
// AUTOINJECT
void Vision_AddCelToDraw(cel_tag *cel, ushort depth) {
    ushort count = VisCelCount;
    if (count >= 64)
        return;
    for (uint i = 0; i < count; i++) {
        if (VisCelList[i].cel == cel)
            return;
    }
    cel->addedToDraw = 1;
    VisCelList[count].cel = cel;
    VisCelList[count].depth = depth;
    VisCelCount = count + 1;
}

// The frame's cels, sorted, as GfxList; the view shows the sky if any of them does
// AUTOINJECT
void vision_generate_display_list(viewer_tag *viewer) {
    viewer->draw.skyVisible = 0;
    QuickSort(VisCelList, VisCelCount, sizeof(VisCel), Compare_DrawCels, NULL);
    uint count = VisCelCount;
    cel_tag *last = GfxList;
    for (uint i = 0; i < count; i++) {
        cel_tag *cel = VisCelList[i].cel;
        viewer->draw.skyVisible |= (cel->bitsFromPlacementTag & CEL_NO_SKY) == 0;
        if (last == NULL)
            GfxList = cel;
        else
            last->nextVisible = cel;
        last = cel;
        cel->nextVisible = NULL;
    }
}

// AUTOINJECT
void vision_GetCamPos(_VECTOR *camPos) {
    *camPos = ViewPoints[0];
}
