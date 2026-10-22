#ifndef VISION_H
#define VISION_H

#include "../actionhelpers.h"

// The cels to draw this frame, linked through cel_tag::nextVisible
extern cel_tag *GfxList;

#pragma pack(push, 1)
// A cel to draw and how many portals deep it was found
struct VisCel {
    cel_tag *cel;
    ushort depth;
    ushort pad;
};

// An opening as a polygon, cut down by planes (ClipPortalWithPlane)
struct VisPortal {
    char count;
    char pad[3];
    _VECTOR v[12];
};
#pragma pack(pop)
static_assert(sizeof(VisPortal) == 0x94, "VisPortal is 0x94 bytes");

bool Vision_InView(_VECTOR *point, float radius, viewer_tag *viewer);
cel_tag* Cel_ObjectLeftCel(cel_tag *cel, _VECTOR *from, _VECTOR *to);
void Vision_Calculate_View_Fustrum(viewer_tag *viewer);
void Vision_Init_Portal_Recurse(viewer_tag *viewer);
bool ClipPortalWithPlane(plane_equ_tag *plane, VisPortal *in, VisPortal *out);
void Vision_Recurse(cel_tag *cel, int depth);
void Vision_Portal_Recurse(viewer_tag *viewer);
void Vision_AddCelToDraw(cel_tag *cel, ushort depth);
void vision_generate_display_list(viewer_tag *viewer);
void vision_GetCamPos(_VECTOR *camPos);

#endif // VISION_H
