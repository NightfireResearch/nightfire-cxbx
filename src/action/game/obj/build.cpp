#include "build.h"

#include "../../engine/Viewer.h"
// AUTOGEN
bool build_LinkToRoom(obj_tag *obj, char flags, world_tag *world);

// AUTOINJECT
void build_link_world_to_viewer(viewer_tag *viewer, world_tag *world) {
    if (viewer == NULL)
        return;
        
    viewer->world = world;
}
