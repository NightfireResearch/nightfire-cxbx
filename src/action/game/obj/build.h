#ifndef BUILD_H_
#define BUILD_H_

#include "../../actionhelpers.h"


bool build_LinkToRoom(obj_tag *obj, char flags, level_tag *level);
void build_link_world_to_viewer(viewer_tag *viewer, world_tag *world);


#endif // BUILD_H_