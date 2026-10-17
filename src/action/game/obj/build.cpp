#include "build.h"

#include "../../engine/viewer.h"
#include "../../engine/Camera.h"
#include "../../memory.h"
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

// AUTOINJECT
viewer_tag* build_alloc_viewer(unsigned char idx) {
    viewer_tag *viewer = (viewer_tag *)Mem_Malloc(sizeof(viewer_tag), 0x504, 0);
    viewer->idx = idx;
    Viewer_Init(viewer);
    return viewer;
}

// Whether a point is in the cel's box, edges included (a NaN coordinate passes)
// AUTOINJECT
bool build_point_in_bounding_box(_VECTOR *point, cel_tag *cel) {
    if (cel == NULL)
        return false;
    if (point->x < cel->someMin.x || point->x > cel->someMax.x)
        return false;
    if (point->z < cel->someMin.z || point->z > cel->someMax.z)
        return false;
    if (point->y < cel->someMin.y || point->y > cel->someMax.y)
        return false;
    return true;
}
