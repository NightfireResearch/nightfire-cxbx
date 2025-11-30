#ifndef BUILD_H_
#define BUILD_H_

#include "../../actionhelpers.h"


bool build_LinkToRoom(obj_tag *obj, char flags, world_tag *world);
cel_tag* build_FindCel(_VECTOR *position, world_tag *world);
void build_link_world_to_viewer(viewer_tag *viewer, world_tag *world);
bool build_PointOnFloor(cel_tag *cel, obj_tag* obj, _VECTOR *position, float distance, _VECTOR *searchDirection);

#endif // BUILD_H_