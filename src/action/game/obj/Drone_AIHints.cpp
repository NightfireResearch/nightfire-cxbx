#include "Drone_AIHints.h"

#include "../../engine/Collide.h"
#include "../sp/SwitchChannels.h"
#include "../drone/DroneVision.h"
#include "../../math/math.h"

#include <bit>
#include <math.h>
#include <string.h>

// XBE_GLOBAL(0x001dfa1c, 0xc)
LLISTINFO_tag AIBoxList;

#pragma fp_contract(off)

// Drone_ProcessCoverNodes counts it down by 2 from 10
// XBE_GLOBAL(0x001dfa2c, 0x4)
int32_t iTimeToCalc;

constexpr float kDeg5 = 0.08726647f;
static_assert(std::bit_cast<uint32_t>(kDeg5) == 0x3db2b8c3);
constexpr float kDeg10 = 0.17453294f;
static_assert(std::bit_cast<uint32_t>(kDeg10) == 0x3e32b8c3);
constexpr float kDeg30 = 0.5235988f;
static_assert(std::bit_cast<uint32_t>(kDeg30) == 0x3f060a92);
constexpr float kDeg75 = 1.3089969f;
static_assert(std::bit_cast<uint32_t>(kDeg75) == 0x3fa78d36);
constexpr float kDeg80 = 1.3962635f;
static_assert(std::bit_cast<uint32_t>(kDeg80) == 0x3fb2b8c3);

// The original's compare-and-FCHS: -0 keeps its sign, and a NaN is negated too
static double AbsByCompare(double x) {
    if (x >= 0.0)
        return x;
    return std::bit_cast<double>(std::bit_cast<uint64_t>(x) ^ 0x8000000000000000ull);
}

// AUTOINJECT
void Drone_CoverCornerNode(_VECTOR *pos, _VECTOR *rot, level_tag *lvl) {
    CoverCornerPlacement *keys = (CoverCornerPlacement *)lvl;
    if (Drone_bDisableSystem)
        return;

    // GC check (0x800847c4): the GameCube prints and writes the entry anyway, past the end of the table
    NF_WARN_IF(NPCGlobals.numCoverNodes >= MAX_COVER_NODES, "Too many cover nodes\n");
    CoverNode_tag *node = &NPCGlobals.coverNodes[NPCGlobals.numCoverNodes];
    memset(node, 0, sizeof(CoverNode_tag));
    node->type = COVER_CORNER;
    node->flags = 0;
    node->cel = build_FindCel(pos, glb_world);
    node->occupant = NULL;
    node->pos = *pos;
    node->rot = *rot;

    if (keys->sideFlags == 1)
        node->flags |= COVERNODE_SIDE1;
    else if (keys->sideFlags == 2)
        node->flags |= COVERNODE_SIDE2;
    else
        node->flags |= COVERNODE_SIDE1 | COVERNODE_SIDE2;

    if (keys->standFlags == 1)
        node->flags |= COVERNODE_STAND1;
    else if (keys->standFlags == 2)
        node->flags |= COVERNODE_STAND2;
    else
        node->flags |= COVERNODE_STAND1 | COVERNODE_STAND2;

    if (keys->crouchFlags == 1)
        node->flags |= COVERNODE_CROUCH1;
    else if (keys->crouchFlags == 2)
        node->flags |= COVERNODE_CROUCH2;
    else
        node->flags |= COVERNODE_CROUCH1 | COVERNODE_CROUCH2;

    if (keys->coverAngleDeg != 0)
        node->angle = (float)(keys->coverAngleDeg * (double)DEG_TO_RAD);
    else
        node->angle = QUARTER_PI;

    node->enableSwitchChannel = keys->enableSwitchChannel;
    node->disableSwitchChannel = keys->disableSwitchChannel;
    node->unknown5 = keys->unknown6 != 0 ? keys->unknown6 : 8;
    node->unknown6 = keys->unknown7 != 0 ? keys->unknown7 : 8;

    AINetwork_InitEmitter(NULL, pos, node->cel, &node->emitter, NULL);
    NPCGlobals.numCoverNodes++;
}

// AUTOINJECT
void Drone_CoverLowNode(_VECTOR *pos, _VECTOR *rot, level_tag *lvl) {
    CoverLowPlacement *keys = (CoverLowPlacement *)lvl;
    // Not range-checked: a larger index reads the stack
    const ushort anims[5] = {0x11, 5, 10, 7, 0x1d};
    if (Drone_bDisableSystem)
        return;

    // GC check (0x80084ae8): the GameCube prints and writes the entry anyway, past the end of the table
    NF_WARN_IF(NPCGlobals.numCoverNodes >= MAX_COVER_NODES, "Too many cover nodes\n");
    CoverNode_tag *node = &NPCGlobals.coverNodes[NPCGlobals.numCoverNodes];
    memset(node, 0, sizeof(CoverNode_tag));
    node->type = COVER_LOW;
    if (keys->kind == 2) {
        node->flags = COVERNODE_LOW_ANIMS;
        node->lowAnim1 = anims[keys->anim1];
        node->lowAnim2 = anims[keys->anim2];
        if (keys->flag2 == 1)
            node->flags |= COVERNODE_LOW_FLAG2;
    } else {
        node->lowAnim1 = anims[0];
        node->lowAnim2 = anims[keys->kind == 1 ? 1 : 2];
    }
    node->cel = build_FindCel(pos, glb_world);
    node->occupant = NULL;
    node->pos = *pos;
    node->rot = *rot;
    node->enableSwitchChannel = keys->enableSwitchChannel;
    node->disableSwitchChannel = keys->disableSwitchChannel;

    AINetwork_InitEmitter(NULL, pos, node->cel, &node->emitter, NULL);
    NPCGlobals.numCoverNodes++;
}

// AUTOINJECT
void Drone_AIPoint(_VECTOR *pos, _VECTOR *rot, level_tag *lvl) {
    AIPointPlacement *keys = (AIPointPlacement *)lvl;
    if (Drone_bDisableSystem)
        return;

    // GC check (0x80084d0c): the GameCube prints and writes the entry anyway, past the end of the table
    NF_WARN_IF(NPCGlobals.numAIPoints >= MAX_AI_POINTS, "Too many AIPoints\n");
    DroneAIPoint_tag *point = &NPCGlobals.aiPoints[NPCGlobals.numAIPoints];
    point->active = 1;
    point->id = keys->pointId;
    point->unknown08 = 0;
    point->distance = 0.0f;
    point->radius = keys->radius;
    point->cel = build_FindCel(pos, glb_world);
    point->pos = *pos;
    point->rot = *rot;
    point->group = keys->group;

    AINetwork_InitEmitter(NULL, pos, point->cel, &point->emitter, NULL);
    NPCGlobals.numAIPoints++;
}

// AUTOINJECT
obj_tag* Drone_AIVolume_Create(_VECTOR *pos, _VECTOR *rot, _VECTOR *scale, level_tag *lvl, celglist_tag *glist) {
    AIVolumePlacement *keys = (AIVolumePlacement *)lvl;
    if (glist == NULL)
        return NULL;

    bool flip = keys->volumeType == 4;
    if (GameState.CurrentLevelHashcode == HT_Level_CastleExterior)
        flip = true;
    _VECTOR at, turn;
    Vec_Copy(pos, &at);
    Vec_Copy(rot, &turn);
    if (flip) {
        turn.z = -turn.z;
        at.y = (float)(scale->y * 1.5 + at.y);
    }

    obj_tag *obj = control_create_object(sizeof(DroneAIVolume_tag), &at, &turn, NULL);
    if (obj == NULL)
        return NULL;
    obj->objGraphics = glist;
    DroneAIVolume_tag *volume = (DroneAIVolume_tag *)obj->extraObjectData;
    obj->effectFlags = glist->applyFlagsToObject | 0x30;
    obj->objectType = OBJECTTYPE_DRONE_AIVOLUME;
    volume->obj = obj;
    volume->visibility = (float)(keys->visibilityPercent * (double)0.01f);

    Vec_Subtract(&glist->extentMax, &glist->extentMin, &volume->halfSize);
    volume->halfSize.x = (float)(AbsByCompare(volume->halfSize.x * 0.5) * scale->x);
    volume->halfSize.y = (float)(AbsByCompare(volume->halfSize.y * 0.5) * scale->y);
    volume->halfSize.z = (float)(AbsByCompare(volume->halfSize.z * 0.5) * scale->z);
    volume->unknown20 = keys->unknown2;
    volume->unknown24 = keys->unknown3;
    volume->unknown1e = keys->unknown4;

    switch (keys->volumeType) {
    case 0:
        volume->type = 0;
        break;
    case 1:
        volume->type = 2;
        break;
    case 2:
        volume->type = 1;
        break;
    case 4:
        volume->type = 3;
        break;
    }

    LList_Add(&AIBoxList, &volume->node);
    return obj;
}

// AUTOINJECT
void Drone_AIVolume_Delete(obj_tag *obj) {
    DroneAIVolume_tag *volume = (DroneAIVolume_tag *)obj->extraObjectData;
    LList_Remove(&AIBoxList, &volume->node);
}

// The product of the visibilities of the volumes of this type that hold pos (1 in none)
// AUTOINJECT
float Drone_VisibilityForPosition(_VECTOR *pos, ushort type) {
    float visibility = 1.0f;
    for (DroneAIVolume_tag *volume = (DroneAIVolume_tag *)AIBoxList.head; volume != NULL;
         volume = (DroneAIVolume_tag *)volume->node.next) {
        if (volume->type == type && Intersect_PointOOBox(&volume->obj->transformMatrix, &volume->halfSize, pos, 0.0f))
            visibility = visibility * volume->visibility;
    }
    return visibility;
}

// AUTOINJECT
bool Drone_PointInAnyAIBox(_VECTOR *pos, ushort type) {
    DroneAIVolume_tag *volume = (DroneAIVolume_tag *)AIBoxList.head;
    // A level without volumes counts as one everywhere
    if (GameState.CurrentLevelHashcode == HT_Level_SpaceStationD && volume == NULL)
        return true;
    for (; volume != NULL; volume = (DroneAIVolume_tag *)volume->node.next) {
        if (volume->type == type && Intersect_PointOOBox(&volume->obj->transformMatrix, &volume->halfSize, pos, 0.0f))
            return true;
    }
    return false;
}

// Whether the node gives cover from the target, recorded in node->usable[target]. True when it ran a line-of-sight
// test, which Drone_ProcessCoverNodes allows one of per target.
bool Drone_IsCoverNodeUsable(uint target, CoverNode_tag *node) {
    if (node == NULL || target >= MAX_DRONE_TARGETS)
        return false;
    DroneTarget_tag *info = &NPCGlobals.targets[target];
    node->usable[target] = 0;
    if (info->obj == NULL)
        return false;
    if (node->enableSwitchChannel != 0 && !switch_channels[node->enableSwitchChannel])
        return false;
    if (node->disableSwitchChannel != 0 && switch_channels[node->disableSwitchChannel])
        return false;

    obj_tag *occupant = node->occupant;
    if ((info->flags & 3) == 0 && (occupant == NULL || occupant->objectType != OBJECTTYPE_DRONE))
        return false;
    if (occupant != NULL && occupant->objectType == OBJECTTYPE_DRONE) {
        Drone_tag *drone = (Drone_tag *)occupant->extraObjectData;
        if (drone == NULL || drone->targetSlot != target)
            return false;
    }

    float wide, narrow;
    if (node->type == COVER_LOW) {
        if (GameState.CurrentLevelHashcode == HT_Level_PowerStationA2) {
            wide = kDeg80;
            narrow = kDeg75;
        } else {
            wide = QUARTER_PI;
            narrow = kDeg30;
        }
        if (node->flags & COVERNODE_WIDER) {
            wide += kDeg5;
            narrow += kDeg5;
        }
    } else if (!(node->flags & COVERNODE_WIDER)) {
        wide = node->angle + kDeg5;
        narrow = node->angle * 0.66f;
    } else {
        wide = node->angle + kDeg10;
        narrow = node->angle * 0.77f;
    }

    _VECTOR toTarget;
    Vec_Subtract(&info->pos, &node->pos, &toTarget);
    if (Vec_Magnitude(&toTarget) < 4.0f) {
        node->usable[target] |= COVERUSE_TOO_CLOSE;
        return false;
    }
    double angle = Vec_AngleDifference(node->rot.y, Atan2(toTarget.x, toTarget.z));

    // The bounds are not rounded to float
    if (node->type == COVER_CORNER) {
        if ((node->flags & COVERNODE_CROUCH2) && angle > (double)HALF_PI - narrow &&
            angle < (double)wide + HALF_PI) {
            node->usable[target] |= COVERUSE_CROUCH2;
            if (angle < HALF_PI)
                node->usable[target] |= COVERUSE_CROUCH2_INNER;
        } else if ((node->flags & COVERNODE_CROUCH1) &&
                   ((angle > 0.0f && angle > (double)PI - wide) ||
                    (angle < 0.0f && angle < (double)narrow - PI))) {
            node->usable[target] |= COVERUSE_CROUCH1;
            if (angle > -PI)
                node->usable[target] |= COVERUSE_CROUCH1_INNER;
        } else {
            node->usable[target] |= COVERUSE_OUTSIDE_ANGLE;
            return false;
        }
    } else {
        if (fabs(angle) > wide) {
            node->usable[target] |= COVERUSE_OUTSIDE_ANGLE;
            return false;
        }
        node->usable[target] |= COVERUSE_LOW;
    }

    _VECTOR eye;
    Vec_Copy(&node->pos, &eye);
    eye.y += 1.5f;
    if (GameState.CurrentLevelHashcode == HT_Level_Tower2A)
        eye.y -= 0.35f;
    ushort tests = NPCGlobals.visionTests;
    if (!DroneVision_CanSeePosition(info->obj, &eye, node->cel, node->occupant))
        node->usable[target] = (node->usable[target] & ~COVERUSE_INNER_MASK) | COVERUSE_SEEN;
    return tests != NPCGlobals.visionTests;
}

// Every fifth call, looks at the cover nodes for each target in turn, going on from where it left off, until one
// line-of-sight test per target
// AUTOINJECT
void Drone_ProcessCoverNodes(void) {
    iTimeToCalc -= 2;
    if (iTimeToCalc > 0)
        return;
    iTimeToCalc = 10;

    for (uint target = 0; target < MAX_DRONE_TARGETS; target++) {
        for (uint tried = 0; tried < NPCGlobals.numCoverNodes;) {
            bool tested = Drone_IsCoverNodeUsable(target, &NPCGlobals.coverNodes[NPCGlobals.coverCursor]);
            NPCGlobals.coverCursor++;
            if (NPCGlobals.coverCursor >= NPCGlobals.numCoverNodes)
                NPCGlobals.coverCursor = 0;
            tried++;
            if (tested)
                break;
        }
    }
}

// The squared distance, on the x87 stack in the original
static double DistanceSq(const _VECTOR *a, const _VECTOR *b) {
    double dx = (double)a->x - b->x;
    double dy = (double)a->y - b->y;
    double dz = (double)a->z - b->z;
    return dx * dx + dy * dy + dz * dz;
}

// Marks the links from node (as their end a) to a node with nodeFlag that cross a bound: boundFlag on the bound,
// linkFlag on the link. The original's 0x000319b0 (doors) and 0x00031a50 (kicks), which take the path in EDI.
static void MarkCrossingLinks(AIPath_tag *path, AINode_tag *node, uint nodeFlag, uint boundFlag, ushort linkFlag) {
    if (path == NULL || node == NULL)
        return;
    for (int i = 0; i < node->linkCount; i++) {
        AILink_tag *link = &path->links[path->nodeLinks[node->firstLink + i]];
        AINode_tag *a = &path->runtimeNodes[link->a];
        AINode_tag *b = &path->runtimeNodes[link->b];
        if (a == node && (b->fileNode->flags & nodeFlag) &&
            AINetwork_ModIntersectedLinkFlags_Bounds(&a->fileNode->pos, &b->fileNode->pos, boundFlag, 0, NULL, 0))
            link->flags |= linkFlag;
    }
}

// Gives each door node the nearest door, or takes the mark off a node with none
// AUTOINJECT
void NDrone_InitDoorNodes(void) {
    // Not reset per node: the first door found is always taken
    float nearest = 4.0f;
    for (uint i = 0; i < AIPathCount; i++) {
        AIPath_tag *path = &AIPaths[i];
        for (uint n = 0; n < path->nodeCount; n++) {
            AINode_tag *node = &path->runtimeNodes[n];
            node->door = NULL;
            if (!(node->fileNode->flags & AINODE_DOOR))
                continue;
            obj_tag *door = NULL;
            for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
                if (obj->objectType != OBJECTTYPE_DOOR)
                    continue;
                double distanceSq = DistanceSq(&obj->position, &node->fileNode->pos);
                if (distanceSq < nearest || door == NULL) {
                    nearest = (float)distanceSq;
                    door = obj;
                }
            }
            node->door = door;
            if (door == NULL)
                node->fileNode->flags &= ~AINODE_DOOR;
            MarkCrossingLinks(path, node, AINODE_DOOR, AIBOUND_DOOR, AILINK_DOOR);
        }
    }
}

// Gives each kick node the nearest script player running the kick script, in AINode_tag.door
// AUTOINJECT
void NDrone_InitKickNodes(void) {
    float nearest = 0.0f;
    for (uint i = 0; i < AIPathCount; i++) {
        AIPath_tag *path = &AIPaths[i];
        for (uint n = 0; n < path->nodeCount; n++) {
            AINode_tag *node = &path->runtimeNodes[n];
            if (!(node->fileNode->flags & AINODE_KICK))
                continue;
            obj_tag *kickable = NULL;
            for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
                if (obj->objectType != OBJECTTYPE_SCRIPTPLAYER)
                    continue;
                SCRIPTINFO *info = SP_getScriptInfo(obj);
                if (info == NULL || info->scriptHashcode != HT_Script_TableFall)
                    continue;
                double distanceSq = DistanceSq(&obj->position, &node->fileNode->pos);
                if (kickable == NULL || distanceSq < nearest) {
                    nearest = (float)distanceSq;
                    kickable = obj;
                }
            }
            if (kickable != NULL)
                node->door = kickable;
            MarkCrossingLinks(path, node, AINODE_KICK, AIBOUND_KICK, AILINK_KICK);
        }
    }
}
