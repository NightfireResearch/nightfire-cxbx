#include "build.h"

#include "../../engine/viewer.h"
// AUTOGEN
bool build_LinkToRoom(obj_tag *obj, char flags, world_tag *world);

// AUTOGEN
cel_tag* build_FindCel(_VECTOR *position, world_tag *world);

// AUTOINJECT
void build_link_world_to_viewer(viewer_tag *viewer, world_tag *world) {
    if (viewer == NULL)
        return;
        
    viewer->world = world;
}

// AUTOINJECT
bool build_PointOnFloor(cel_tag *cel, obj_tag* obj, _VECTOR *position, float distance, _VECTOR *searchDirection) {
  
  _VECTOR defaultDirection = {0.0f, -1.0f, 0.0f};
  if(searchDirection == NULL)
    searchDirection = &defaultDirection;

  _VECTOR endPosition = {
    .x = position->x + searchDirection->x * distance,
    .y = position->y + searchDirection->y * distance,
    .z = position->z + searchDirection->z * distance,
  };

  HITDATA_tag* hitList = NULL;

  bool intersects = Collide_RayIntersect(position, &endPosition, cel, obj, NULL, &hitList, 0, 0x70c, 0);

  if(intersects) {
    Vec_Copy(&(hitList->hitPosition), position);
    Collide_FreeHitList(&hitList);
  }

  return intersects;
}