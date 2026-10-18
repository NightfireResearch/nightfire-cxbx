#ifndef BUILD_H_
#define BUILD_H_

#include "../../actionhelpers.h"


bool build_LinkToRoom(obj_tag *obj, char straddleTest, world_tag *world);
cel_tag* build_FindCel(_VECTOR *position, world_tag *world);
void build_link_world_to_viewer(viewer_tag *viewer, world_tag *world);
viewer_tag* build_alloc_viewer(unsigned char idx);
bool build_point_in_bounding_box(_VECTOR *point, cel_tag *cel);
void build_MakeRoomList(cel_tag *first);
void build_link_objects_to_rooms(cel_tag *first);
void build_LinkDoors2Portals(cel_tag *unused, obj_tag *first);
void init_cel(cel_tag *cel, cel_tag *next);
cel_tag* build_alloc_cel(cel_tag *next);
world_tag* build_alloc_world(void);
portal_tag* build_alloc_portal(portal_tag *from, portal_tag *next, cel_tag *cel, char open);
bool build_PointOnFloor(cel_tag *cel, obj_tag* obj, _VECTOR *position, float distance, _VECTOR *searchDirection);

#endif // BUILD_H_