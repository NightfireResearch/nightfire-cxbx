#include "build.h"

#include "../../engine/viewer.h"
#include "../../engine/Camera.h"
#include "../../memory.h"
#include "../../engine/Collide.h"
#include "../../engine/celglist.h"
#include "../../math/X87Ops.h"
#include "../../util/LList.h"
#include "../sp/SwitchChannels.h"
#include "Door.h"
#include "control.h"
#include "object.h"

#include <string.h>

// Game objects which span multiple cels (control.cpp)

// AUTOINJECT
viewer_tag* build_link_world_to_viewer(viewer_tag *viewer, world_tag *world) {
    if (viewer == NULL)
        return viewer;
    viewer->world = world;
    return viewer;
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

// The cels a point is found in, kept from one call to the next
// XBE_GLOBAL(0x001dc570, 0x100)
static cel_tag *CelList[0x40];
// XBE_GLOBAL(0x001dc670, 0x2)
static ushort CelListCount;

// The main world's rooms (cels with CEL_ROOM), NULL-terminated, made on the first search of a level
extern cel_tag **gbl_room_cels;

// Lists the rooms among the world's cels in gbl_room_cels (the name is ours)
// AUTOINJECT
void build_MakeRoomList(cel_tag *first) {
    int count = 0;
    for (cel_tag *cel = first; cel != NULL; cel = cel->nextCel) {
        if (cel->bitsFromPlacementTag & CEL_ROOM)
            count++;
    }
    cel_tag **room = (cel_tag **)Mem_Malloc((count + 1) * sizeof(cel_tag *), (MallocFlags)0x904, 0);
    gbl_room_cels = room;
    for (cel_tag *cel = first; cel != NULL; cel = cel->nextCel) {
        if (cel->bitsFromPlacementTag & CEL_ROOM)
            *room++ = cel;
    }
    *room = NULL;
}

// The room a point is in. Of several whose boxes hold it (at most 0x40), the one with geometry nearest above or
// below it, by rays reaching no further than the nearest hit so far; the first when none is hit.
// AUTOINJECT
cel_tag* build_FindCel(_VECTOR *position, world_tag *world) {
    CelListCount = 0;
    if (world == glb_world) {
        if (gbl_room_cels == NULL)
            build_MakeRoomList(world->firstCel);
        for (cel_tag **room = gbl_room_cels; *room != NULL && CelListCount < 0x40; room++) {
            if (build_point_in_bounding_box(position, *room))
                CelList[CelListCount++] = *room;
        }
    } else {
        for (cel_tag *cel = world->firstCel; cel != NULL && CelListCount < 0x40; cel = cel->nextCel) {
            if ((cel->bitsFromPlacementTag & CEL_ROOM) && build_point_in_bounding_box(position, cel))
                CelList[CelListCount++] = cel;
        }
    }
    if (CelListCount == 1)
        return CelList[0];
    if (CelListCount == 0)
        return NULL;

    HITDATA_tag *above = NULL;
    HITDATA_tag *below = NULL;
    cel_tag *found = NULL;
    float nearest = 256.0f;
    float reach = 256.0f;
    for (ushort i = 0; i < CelListCount; i++) {
        _VECTOR to = {position->x, position->y + reach, position->z};
        Collide_RayIntersect(position, &to, CelList[i], NULL, NULL, &above, 1, 0x90, 0);
        if (above != NULL && !(reach < above->dist))
            reach = above->dist;
        to = {position->x, position->y - reach, position->z};
        Collide_RayIntersect(position, &to, CelList[i], NULL, NULL, &below, 1, 0x90, 0);
        if (below != NULL && !(reach < below->dist))
            reach = below->dist;
        HITDATA_tag *hit = below;
        if (above != NULL && (below == NULL || above->dist < below->dist))
            hit = above;
        if (hit != NULL && hit->dist < nearest) {
            found = CelList[i];
            nearest = hit->dist;
        }
        Collide_FreeHitList(&above);
        Collide_FreeHitList(&below);
    }
    return found != NULL ? found : CelList[0];
}

// Links an object into the room its bounding sphere's centre is in, then takes the room's ambient light and, if
// asked, tests whether it straddles rooms. One always forced goes into the forced list instead.
// AUTOINJECT
bool build_LinkToRoom(obj_tag *obj, char straddleTest, world_tag *world) {
    if (obj->effectFlags & FLAG_ALWAYS_FORCED) {
        if (!(obj->effectFlags & FLAG_FORCED_LISTED)) {
            obj->effectFlags |= FLAG_FORCED_LISTED | FLAG_STRADDLES_CELS;
            LList_Add(&ForcedList, (LLNODE_tag *)obj);
        }
        return false;
    }
    Control_BuildWorldSph(obj);
    _VECTOR centre;
    Vec_Copy(&obj->centrePoint, &centre);
    cel_tag *room = build_FindCel(&centre, world);
    if (room == NULL)
        return false;
    control_unlink_object(obj);
    control_link_object_to_cel(obj, room);
    cel_tag *cel = obj->inCel;
    uchar state = switch_channels[cel->someSwitchChannel];
    obj->ambientR = cel->ambientR[state];
    obj->ambientG = cel->ambientG[state];
    obj->ambientB = cel->ambientB[state];
    if (straddleTest)
        Controls_StraddleTest(obj);
    return true;
}

// Puts each of the cels of kinds 0x8017 to 0x8019 into a room (kinds 0xc047 and 0xc023) at least as large whose box
// holds its centre: the one whose geometry a sphere about the centre meets nearest. A cel of kind 0x8017 gives its
// room its lowest height as the water's (shearHeight).
// AUTOINJECT
void build_link_objects_to_rooms(cel_tag *first) {
    for (cel_tag *cel = first; cel != NULL; cel = cel->nextCel) {
        uint kind = cel->bitsFromPlacementTag & 0xffff;
        if (kind != 0x8019 && kind != 0x8018 && kind != 0x8017)
            continue;
        cel_tag *found = NULL;
        float nearest = 4000.0f;
        for (cel_tag *room = first; room != NULL; room = room->nextCel) {
            uint roomKind = room->bitsFromPlacementTag & 0xffff;
            if (roomKind != 0xc047 && roomKind != 0xc023)
                continue;
            if (room->sphereRadius < cel->sphereRadius)
                continue;
            if (!build_point_in_bounding_box(&cel->sphere, room))
                continue;
            _VECTOR centre;
            Vec_Copy(&cel->sphere, &centre);
            HITDATA_tag *hits = NULL;
            if (Collide_SphereIntersect(&centre, nearest + 1.0f, room, NULL, NULL, &hits, 1, 0, 0x90, 4)) {
                if (hits->dist < nearest) {
                    nearest = hits->dist;
                    found = hits->hitCel;
                }
                Collide_FreeHitList(&hits);
            }
        }
        if (found == NULL)
            continue;
        cel->parentCel = found;
        cel->childCel = found->childCel;
        found->childCel = cel;
        if ((cel->bitsFromPlacementTag & 0xffff) == 0x8017)
            found->shearHeight = cel->someMin.y;
    }
}

// Each door that asks for it (Door_Link2Portal) is set in the portal its centre lies in, in each cel its sphere
// reaches: within 0.25 of the portal's plane and inside one of its triangles. The portal and its partner keep the
// first such door, and the door counts as straddling cels.
// AUTOINJECT
void build_LinkDoors2Portals(cel_tag *, obj_tag *first) {
    for (obj_tag *obj = first; obj != NULL; obj = obj->nextObject) {
        if (obj->objectType != OBJECTTYPE_DOOR || obj->inCel == NULL || !Door_Link2Portal(obj))
            continue;
        _VECTOR centre;
        Vec_Copy(&obj->centrePoint, &centre);
        HITTEST_tag test = {};
        test.type = HITTEST_SPHERE;
        Vec_Copy(&obj->centrePoint, &test.pos);
        test.radius = obj->radius;
        test.cel = obj->inCel;
        ushort count;
        cel_tag **cels = Collide_StraddleCels(&test, &count);
        for (ushort i = 0; i < count; i++) {
            for (portal_tag *portal = cels[i]->portals; portal != NULL; portal = portal->next) {
                if (!(AbsFchs(DistancePointToPlane(&centre, &portal->plane)) < 0.25))
                    continue;
                if (!vecutil_point_on_poly(&centre, &portal->corners[0], &portal->corners[1], &portal->corners[2],
                                           &portal->plane) &&
                    !vecutil_point_on_poly(&centre, &portal->corners[2], &portal->corners[3], &portal->corners[0],
                                           &portal->plane))
                    continue;
                obj->effectFlags |= FLAG_STRADDLES_CELS;
                if (portal->door == NULL)
                    portal->door = obj;
                if (portal->partner->door == NULL)
                    portal->partner->door = obj;
                break;
            }
        }
    }
}

// A cel as it starts: empty, a box of -1 to 1, water at -500 and a grey light
// AUTOINJECT
void init_cel(cel_tag *cel, cel_tag *next) {
    cel->firstObject = NULL;
    cel->headPrev = NULL;
    cel->attachments = NULL;
    cel->parentCel = NULL;
    cel->aiNodes = NULL;
    cel->boundNodes = NULL;
    cel->boundLinks = NULL;
    cel->portals = NULL;
    cel->childCel = NULL;
    cel->nextVisible = NULL;
    cel->flags84 = 0;
    cel->bitsFromPlacementTag = 0;
    cel->position = {0.0f, 0.0f, 0.0f};
    cel->rotation = {0.0f, 0.0f, 0.0f};
    cel->glist = NULL;
    cel->env = NULL;
    cel->addedToDraw = 0;
    cel->musicEvent = 0;
    cel->someMin.x = cel->someMin.y = cel->someMin.z = -1.0f;
    cel->someMax.x = cel->someMax.y = cel->someMax.z = 1.0f;
    cel->shearHeight = -500.0f;
    cel->nextCel = next;
    cel->blink = 0x10;
    cel->ambientR[0] = 0x50;
    cel->ambientG[0] = 0x50;
    cel->ambientB[0] = 0x50;
}

// AUTOINJECT
cel_tag* build_alloc_cel(cel_tag *next) {
    cel_tag *cel = (cel_tag *)Mem_Malloc(sizeof(cel_tag), (MallocFlags)0x104, 0);
    init_cel(cel, next);
    return cel;
}

// AUTOINJECT
world_tag* build_alloc_world(void) {
    world_tag *world = (world_tag *)Mem_Malloc(sizeof(world_tag), (MallocFlags)0x604, 0);
    world->firstCel = NULL;
    return world;
}

// The lesser of each pair, then of the two, and the greater likewise; a NaN loses each comparison
static float Min4(float a, float b, float c, float d) {
    float ab = a < b ? a : b;
    float cd = c < d ? c : d;
    return ab < cd ? ab : cd;
}

static float Max4(float a, float b, float c, float d) {
    float ab = a > b ? a : b;
    float cd = c > d ? c : d;
    return ab > cd ? ab : cd;
}

// A portal of a cel from the parsed one: its corners and plane, the centre (the corners' mean, summed in the
// original's order) and the box round them grown by 0.1.
// AUTOINJECT
portal_tag* build_alloc_portal(portal_tag *from, portal_tag *next, cel_tag *cel, char open) {
    portal_tag *portal = (portal_tag *)Mem_Malloc(sizeof(portal_tag), (MallocFlags)0x804, 0);
    _VECTOR *c = portal->corners;
    memcpy(portal->corners, from->corners, sizeof(portal->corners));
    portal->plane = from->plane;
    portal->cel = cel;
    portal->next = next;
    portal->door = NULL;
    portal->unknown24 = 0xffff;
    portal->seen = 0;
    portal->open = open;
    portal->centre.x = (float)(((((double)c[3].x + c[1].x) + c[0].x) + c[2].x) * 0.25f);
    portal->centre.y = (float)(((((double)c[2].y + c[3].y) + c[1].y) + c[0].y) * 0.25f);
    portal->centre.z = (float)(((((double)c[1].z + c[0].z) + c[2].z) + c[3].z) * 0.25f);
    portal->boxMin.x = Min4(c[0].x, c[1].x, c[2].x, c[3].x) - 0.1f;
    portal->boxMin.y = Min4(c[0].y, c[1].y, c[2].y, c[3].y) - 0.1f;
    portal->boxMin.z = Min4(c[0].z, c[1].z, c[2].z, c[3].z) - 0.1f;
    portal->boxMax.x = Max4(c[0].x, c[1].x, c[2].x, c[3].x) + 0.1f;
    portal->boxMax.y = Max4(c[0].y, c[1].y, c[2].y, c[3].y) + 0.1f;
    portal->boxMax.z = Max4(c[0].z, c[1].z, c[2].z, c[3].z) + 0.1f;
    return portal;
}
