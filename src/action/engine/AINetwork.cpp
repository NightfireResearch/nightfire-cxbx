// The AI navigation network: routes and their distances, the node searches and A*, the bounds tests, the link flags,
// the emitters, and following a route one creep step at a time. See docs/drone/bots-and-navigation/README.md.

#include "AINetwork.h"
#include "Collide.h"
#include "viewer.h"
#include "../memory.h"
#include "../game.h"
#include "../math/math.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/Behaviour.h"
#include "../game/obj/Door.h"
#include "../game/obj/build.h"
#include "../game/mp/multiplayer.h"

#include <bit>
#include <float.h>
#include <math.h>
#include <string.h>

#pragma fp_contract(off)

static_assert(offsetof(cel_tag, aiNodes) == 0x24, "Wrong offset for cel_tag.aiNodes");
static_assert(offsetof(cel_tag, portals) == 0x28, "Wrong offset for cel_tag.portals");
static_assert(offsetof(cel_tag, boundNodes) == 0x30, "Wrong offset for cel_tag.boundNodes");
static_assert(offsetof(cel_tag, boundLinks) == 0x34, "Wrong offset for cel_tag.boundLinks");
static_assert(sizeof(portal_tag) == 0x7c, "portal_tag is 0x7c bytes");
static_assert(offsetof(portal_tag, corners) == 0x28, "Wrong offset for portal_tag.corners");

// The originals, called by address. These return in ST0, unrounded.
#define Vec_Dist2D ((double (__cdecl *)(_VECTOR *a, _VECTOR *b))0x000d4e20)
#define Vec_SqDist2D ((double (__cdecl *)(_VECTOR *a, _VECTOR *b))0x000d4e50)
// Writes the point of the line through a and b nearest p; its return value is not used
// Where the segments a0-a1 and b0-b1 cross in plan (x and z); zero if they do not
// The cels a query's ray passes through; the count goes in *count
#define NDrone2_ReachedDestNode ((bool (__cdecl *)(AIRoute_tag *route, DCVars_tag *dcv))0x000446b0)

// The cels the last ray of AINetwork_TestRayCels and of AINetwork_BoundsTest passed through
// XBE_GLOBAL(0x001ee42c, 0x4)
#define TestRayCels_Cels (*(cel_tag ***)0x001ee42c)
// XBE_GLOBAL(0x001ee430, 0x4)
#define BoundsTest_Cels (*(cel_tag ***)0x001ee430)

// An emitter's data: allocation type malloc_bot_path3 (0x4e00), aligned to 8 (Mem_Malloc takes the alignment from
// the flags' low byte)
#define MALLOC_BOT_PATH3_ALIGN8 ((MallocFlags)0x4e08)

// How far a node search looks when it is not told: 15 m. A* and the emitters look 25 m.
#define NODE_SEARCH_RADIUS 15.0f
#define ROUTE_START_RADIUS_SQ 625.0f

// A creep step is 0.4 m
#define CREEP_STEP 0.4f

// How near (squared, in plan) a walk may pass a bound corner in AINetwork_BoundsTest: one step above 0.16f
constexpr float CORNER_CLEARANCE_SQ = 0.16000001f;
static_assert(std::bit_cast<uint32_t>(CORNER_CLEARANCE_SQ) == 0x3e23d70b, "not the original's constant");

// The behaviour property that keeps a blocked creep step from marking the drone (LinkCreep_Handler)
#define BEHAVIOUR_IGNORE_BLOCKED_CREEP 0x36

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

// ---- routes

// Whether a route is planned and still good. Returns the flag itself (0x10), not 1: callers only test it for zero.
// AUTOINJECT
uchar AINetwork_RouteIsValid(AIRoute_tag *route) {
    if (route == NULL)
        return 0;
    return (uchar)(route->flags & AIROUTE_VALID);
}

// AUTOINJECT
void AINetwork_InvalidateRoute(AIRoute_tag *route) {
    if (route != NULL)
        route->flags &= ~AIROUTE_VALID;
}

// AUTOINJECT
void AINetwork_ValidateRoute(AIRoute_tag *route) {
    if (route != NULL)
        route->flags |= AIROUTE_VALID;
}

// The route index `offset` places from `current`: past either end it wraps for a loop, turns back for a ping-pong
// route, and otherwise stops at -1 or the last node.
// AUTOINJECT
short AINetwork_RouteNodeOffset(short current, short offset, AIRoute_tag *route) {
    short index = current + offset;
    if (index >= 0 && index < route->count)
        return index;

    switch (route->flags & AIROUTE_MODE) {
    case AIROUTE_LOOP:
        return index < 0 ? index + route->count : index - route->count;
    case AIROUTE_PINGPONG:
        return index < 0 ? -index : route->count * 2 - index;
    default:
        return index < 0 ? -1 : route->count - 1;
    }
}

// What every route reset clears; the callers set the status
static void ClearRoute(AIRoute_tag *route) {
    route->flags &= ~AIROUTE_DEST_MOVED;
    route->creep.active = 0;
    route->unknown05 = 0;
    route->targetFrame = 0;
    route->startNode = 0xffff;
    route->endNode = 0xffff;
    route->current = -1;
    route->count = 0;
    route->remaining = 0.0f;
}

// AUTOINJECT
void AINetwork_ResetRoute(AIRoute_tag *route, obj_tag *mover, obj_tag *targetObj) {
    ClearRoute(route);
    route->status = AIROUTE_RESET;
    route->mover = mover;
    route->targetObj = targetObj;
}

// The link between a node and another (by index), or 0xffff when they are not linked
// FUNC_AT(0x00047b40)
ushort AINetwork_FindLink(AIPath_tag *path, ushort node, short other) {
    if (node != 0xffff && other != -1) {
        AINode_tag *from = &path->runtimeNodes[node];
        ushort *links = &path->nodeLinks[from->firstLink];
        for (int i = 0; i < from->linkCount; i++) {
            AILink_tag *link = &path->links[links[i]];
            // compared as 16 bits
            if (link->a == (ushort)other || link->b == (ushort)other)
                return links[i];
        }
    }
    return 0xffff;
}

static MAPNODE_tag *RouteNode(AIRoute_tag *route, int index) {
    return route->path->runtimeNodes[route->nodes[index]].fileNode;
}

static double Dist3D(_VECTOR *a, _VECTOR *b) {
    return Vec_Dist3D(a, b);
}

// The distance left along a route: from the mover's feet to the next node, along the links after it, and from the
// last node to the destination. Stored in remaining as well, except when the route has no nodes left.
static double RemainingDistance(AIRoute_tag *route, double (__cdecl *distance)(_VECTOR *, _VECTOR *)) {
    _VECTOR feet;
    NDrone2_FeetPos(route->mover, &feet);
    if (route->count == 0 || route->current >= route->count || route->path == NULL)
        return distance(&feet, &route->dest.pos);

    short next = route->current + 1;
    if (next < 0)
        next = 0;
    if (next > route->count - 1)
        return distance(&feet, &route->dest.pos);

    MAPNODE_tag *prev = RouteNode(route, next);
    // The sum starts from the first distance unrounded and is rounded once at the end
    double sum = distance(&feet, &prev->pos);
    float travelled = (float)sum;
    if (next + 1 < route->count) {
        for (int i = next + 1; i < route->count; i++) {
            MAPNODE_tag *node = RouteNode(route, i);
            ushort link = AINetwork_FindLink(route->path, prev->index, node->index);
            if (link != 0xffff)
                sum += route->path->links[link].length;
            prev = node;
        }
        travelled = (float)sum;
    }
    double total = distance(&route->dest.pos, &RouteNode(route, route->count - 1)->pos) + travelled;
    route->remaining = (float)total;
    return total;
}

// In plan
// AUTOINJECT
double AINetwork_GetRouteDistance(AIRoute_tag *route) {
    return RemainingDistance(route, Vec_Dist2D);
}

// AUTOINJECT
double AINetwork_GetRouteDistance3D(AIRoute_tag *route) {
    return RemainingDistance(route, Dist3D);
}

// The length of the links between two route indices, negative when `to` comes first. Both are clamped to 0.
// AUTOINJECT
double AINetwork_GetRouteDistanceBetweenNodes(AIRoute_tag *route, short from, short to) {
    AIPath_tag *path = route->path;
    double distance = 0.0;
    if (route->count == 0 || path == NULL || from > route->count - 1 || to > route->count - 1 || from == to)
        return 0.0;

    if (from < 0)
        from = 0;
    if (to < 0)
        to = 0;
    // Equal after the clamp counts as backwards: the result is then -0
    bool backwards = from >= to;
    short first = backwards ? to : from;
    short last = backwards ? from : to;

    MAPNODE_tag *prev = RouteNode(route, first);
    for (short i = first + 1; i <= last; i++) {
        MAPNODE_tag *node = RouteNode(route, i);
        ushort link = AINetwork_FindLink(path, prev->index, node->index);
        if (link != 0xffff)
            distance += path->links[link].length;
        prev = node;
    }
    return backwards ? -distance : distance;
}

// ---- the node lists of the searches

// Puts a node on a search's open list (the head is a dummy node), after every node whose f is not above its own, and
// marks it listed. The original takes the head in EAX and the node in EDX.
static void OpenListInsert(AINode_tag *head, AINode_tag *node) {
    AINode_tag *prev = head;
    AINode_tag *cur = head->nextInList;
    while (cur != NULL && !(node->f < cur->f)) {
        prev = cur;
        cur = cur->nextInList;
    }
    prev->nextInList = node;
    node->nextInList = cur;
    node->fileNode->flags |= AINODE_OPEN;
}

static AINode_tag *OpenListPop(AINode_tag *head) {
    AINode_tag *node = head->nextInList;
    head->nextInList = node->nextInList;
    node->nextInList = NULL;
    node->fileNode->flags &= ~AINODE_OPEN;
    return node;
}

// Takes a node off the emitter's closed list. Only for AINODE_CLOSED, which nothing sets. The original takes the node
// in ECX and the head in EDX.
static void ClosedListRemove(AINode_tag *head, AINode_tag *node) {
    AINode_tag *prev = head;
    for (AINode_tag *cur = head->nextInList; cur != NULL; cur = cur->nextInList) {
        if (cur == node) {
            prev->nextInList = node->nextInList;
            node->nextInList = NULL;
            node->fileNode->flags &= ~AINODE_CLOSED;
        }
        prev = cur;
    }
}

// The node at the other end of a node's slot'th link; -1 past its last
static int Neighbour(AIPath_tag *path, AINode_tag *node, int slot) {
    if (slot >= node->linkCount)
        return -1;
    AILink_tag *link = &path->links[path->nodeLinks[node->firstLink + slot]];
    return link->a != node->fileNode->index ? link->a : link->b;
}

// A link between two door nodes is closed while the door is locked
static bool DoorClosed(AINode_tag *from, AINode_tag *to) {
    return from->fileNode != NULL && to->fileNode != NULL && from->door != NULL
        && (from->fileNode->flags & AINODE_DOOR) && (to->fileNode->flags & AINODE_DOOR) && Door_IsLocked(from->door);
}

// ---- searches

// Looks for nodes of the search's path in its origin's cel, inside a box of the radius (2 m up and down), nearest
// first in plan. With toNode the first node the origin can be walked to is the result (it gets g, h and f, and goes
// on the open list); the first that is obstructed instead gets the target bit above and ends the search. Without it,
// the first node that can be walked to the origin gets the target bit, and the first that cannot, the bit above.
// False only without a cel or a path.
bool _AINetwork_NodeSearchCel(AINodeSearch_tag *search) {
    CelPos_tag *origin = search->origin;
    if (origin->cel == NULL || search->path == NULL)
        return false;

    float radius = NODE_SEARCH_RADIUS;
    if (search->radiusSq != 0.0f)
        radius = (float)sqrt(search->radiusSq);
    _VECTOR boxMin = {origin->pos.x - radius, origin->pos.y - 2.0f, origin->pos.z - radius};
    _VECTOR boxMax = {radius + origin->pos.x, origin->pos.y + 2.0f, radius + origin->pos.z};

    AINode_tag sorted;  // a dummy head: only nextSorted is used
    sorted.nextSorted = NULL;
    for (AINode_tag *node = origin->cel->aiNodes; node != NULL; node = node->nextInCel) {
        if (node->fileNode->path != search->path->index
            || !Intersect_SphereBox(&node->fileNode->pos, 0.0f, &boxMin, &boxMax))
            continue;
        float distance = (float)Vec_Dist2D(&node->fileNode->pos, &search->origin->pos);
        node->searchDistance = distance;
        // The distance is compared with the radius squared
        if (search->radiusSq > 0.0f && distance > search->radiusSq)
            continue;

        AINode_tag *prev = &sorted;
        AINode_tag *cur = sorted.nextSorted;
        while (cur != NULL && !(distance < cur->searchDistance)) {
            prev = cur;
            cur = cur->nextSorted;
        }
        prev->nextSorted = node;
        node->nextSorted = cur;
    }

    for (AINode_tag *node = sorted.nextSorted; node != NULL; node = node->nextSorted) {
        if (search->toNode) {
            char result = NDrone2_MoveTestToNode(search->origin, node->fileNode, search->objA, search->objB, NULL, 0, 1);
            if (result == 1) {
                node->g = (float)Vec_Dist2D(&search->origin->pos, &node->fileNode->pos);
                if (search->heuristicGoal != NULL)
                    node->h = (float)Vec_Dist2D(&node->fileNode->pos, search->heuristicGoal);
                else
                    node->h = 0.0f;
                node->nextInList = NULL;
                node->parent = NULL;
                node->f = node->h + node->g;
                search->resultNode = node->fileNode->index;
                if (search->openList != NULL)
                    OpenListInsert(search->openList, node);
                search->found++;
                return true;
            }
            if (result == -1) {
                node->fileNode->flags |= search->targetBit << 1;
                search->obstructed++;
                return true;
            }
        } else {
            if (NDrone2_MoveTestFromNode(node->fileNode, search->origin, search->objA, search->objB, NULL, 0, 1) == 1) {
                node->h = node->searchDistance;
                node->fileNode->flags |= search->targetBit;
                search->found++;
                return true;
            }
            if (search->obstructed == 0) {
                node->h = node->searchDistance;
                node->fileNode->flags |= search->targetBit << 1;
                search->obstructed++;
            }
        }
    }
    return true;
}

// The original takes the search in ESI.
// AUTOLTCG
void __declspec(naked) AINetwork_NodeSearchCel(void) {
    _asm {
        push esi
        call _AINetwork_NodeSearchCel
        add esp, 4
        ret
    }
}

// AINetwork_NodeSearchCel, and while nothing is found, once more for each of the cel's portals whose height range
// (2 m either way) holds the origin. The search stays in the origin's cel each time. False only without a cel.
// AUTOINJECT
bool AINetwork_NodeSearch(AINodeSearch_tag *search) {
    search->obstructed = 0;
    search->found = 0;
    _AINetwork_NodeSearchCel(search);
    if (search->found != 0)
        return true;

    cel_tag *cel = search->origin->cel;
    if (cel == NULL)
        return false;
    for (portal_tag *portal = cel->portals; portal != NULL; portal = portal->next) {
        if (portal->cel == NULL)
            continue;
        float low = portal->corners[0].y;
        if (low > portal->corners[1].y)
            low = portal->corners[1].y;
        if (low > portal->corners[2].y)
            low = portal->corners[2].y;
        if (low > portal->corners[3].y)
            low = portal->corners[3].y;
        float high = portal->corners[0].y;
        if (portal->corners[1].y > high)
            high = portal->corners[1].y;
        if (portal->corners[2].y > high)
            high = portal->corners[2].y;
        if (portal->corners[3].y > high)
            high = portal->corners[3].y;

        float y = search->origin->pos.y;
        if (y > low - 2.0 && high + 2.0 > y)
            _AINetwork_NodeSearchCel(search);
        if (search->found != 0)
            break;
    }
    return true;
}

// A* over the route's path, from the nodes the route's start can be walked to within 25 m, to any node carrying the
// target bit (or the bit above when no node reaches the goal unobstructed: then the result is AIROUTE_PARTIAL). The
// cost of a link is its length times one more than the routes on it this frame. A node already listed is updated
// when a cheaper way to it is found, but an expanded node is not opened again. Fills in the route's nodes.
// AUTOINJECT
uint AINetwork_DoAStarPath(AIRoute_tag *route, obj_tag *objA, obj_tag *objB, uint targetBit) {
    AINode_tag open, closed;    // dummy heads: only nextInList is used
    open.nextInList = NULL;
    closed.nextInList = NULL;
    uint result = AIROUTE_FOLLOWING;
    for (int i = 0; i < (int)route->path->nodeCount; i++) {
        route->path->runtimeNodes[i].fileNode->flags &= AINODE_LIST_MASK;
        route->path->runtimeNodes[i].g = 0.0f;
    }

    AITarget_tag *target = route->target;
    if (target->builtFrame == 0)
        return AIROUTE_NO_GOAL_NODES;
    if (target->reachable == 0 || !(target->targetBit & route->path->reachMask)) {
        if (target->obstructedOnly == 0)
            return AIROUTE_NO_GOAL_NODES;
        targetBit <<= 1;
        result = AIROUTE_PARTIAL;
    }

    AINodeSearch_tag search;
    search.origin = &route->start;
    search.heuristicGoal = &route->dest.pos;
    search.path = route->path;
    search.toNode = 1;
    search.openList = &open;
    search.objA = objA;
    search.objB = objB;
    search.targetBit = 0;
    search.radiusSq = ROUTE_START_RADIUS_SQ;
    search.found = 0;
    search.obstructed = 0;
    AINetwork_NodeSearch(&search);
    if (search.found <= 0)
        return AIROUTE_OFF_NETWORK;

    AIPath_tag *path = route->path;
    while (open.nextInList != NULL) {
        AINode_tag *node = OpenListPop(&open);

        if (node->fileNode->flags & targetBit) {
            route->endNode = node->fileNode->index;
            route->remaining = (float)(Vec_Dist2D(&node->fileNode->pos, &route->dest.pos) + node->g);
            uint count = 1;
            AINode_tag *first = node;
            while (first->parent != NULL) {
                first = first->parent;
                count++;
                NF_WARN_IF(count > 100000, "Likely infinite loop\n"); // GC check (0x800a2eb8)
            }
            route->prevStartNode = route->startNode;
            route->startNode = first->fileNode->index;
            for (int i = count - 1; i >= 0; i--) {
                route->nodes[i] = node->fileNode->index;
                node = node->parent;
            }
            route->count = count;
            if (route->count != 0)
                route->current = 0;
            return result;
        }

        ushort *links = &path->nodeLinks[node->firstLink];
        for (int i = 0; i < node->linkCount; i++) {
            int index = Neighbour(path, node, i);
            NF_WARN_IF(index < 0, "Could not find path link\n"); // GC check (0x800a2eb8)
            AINode_tag *next = &path->runtimeNodes[index];
            if (DoorClosed(node, next))
                continue;
            AILink_tag *link = &path->links[links[i]];
            double cost = (double)(link->used + 1) * link->length + node->g;
            if ((next->fileNode->flags & AINODE_LISTED) && cost >= next->g)
                continue;

            next->g = (float)cost;
            next->parent = node;
            double h = Vec_Dist2D(&next->fileNode->pos, &route->dest.pos);
            next->h = (float)h;
            next->f = (float)(h + next->g);
            next->parentLink = i;
            if (next->fileNode->flags & AINODE_CLOSED)
                ClosedListRemove(&closed, next);
            if (!(next->fileNode->flags & AINODE_OPEN))
                OpenListInsert(&open, next);
        }
        node->nextInList = closed.nextInList;
        closed.nextInList = node;
        node->fileNode->flags |= AINODE_OPEN;
    }
    return AIROUTE_NO_ROUTE;
}

// The nav path a position is on: the one with the nearest node in its cel (optionally only nodes it can be walked to),
// otherwise the one with the nearest node anywhere
// AUTOINJECT
AIPath_tag* AINetwork_NavPathForPosition(_VECTOR *pos, cel_tag *cel, char moveTest) {
    if (cel == NULL)
        return NULL;

    float nearest = FLT_MAX;
    ushort best = 0xffff;
    for (AINode_tag *node = cel->aiNodes; node != NULL; node = node->nextInCel) {
        MAPNODE_tag *n = node->fileNode;
        if (AIPaths[n->path].flags & AIPATH_NOT_NAV)
            continue;
        double distance = Vec_Dist2D(pos, &n->pos);
        if (!(distance < nearest))
            continue;
        if (moveTest && NDrone2_MoveTest(pos, cel, &n->pos, n->cel, NULL, NULL, NULL, 0, 1) != 1)
            continue;
        best = n->path;
        nearest = (float)distance;
    }
    if (best != 0xffff)
        return &AIPaths[best];

    for (ushort p = 0; p < AIPathCount; p++) {
        AIPath_tag *path = &AIPaths[p];
        if (path->flags & AIPATH_NOT_NAV)
            continue;
        for (ushort i = 0; i < path->nodeCount; i++) {
            MAPNODE_tag *n = path->runtimeNodes[i].fileNode;
            double distance = Vec_Dist2D(pos, &n->pos);
            if (distance < nearest) {
                nearest = (float)distance;
                best = n->path;
            }
        }
    }
    return best != 0xffff ? &AIPaths[best] : NULL;
}

// Marks, on every nav path, the node nearest the goal that can be walked to it with the target's bit (and the
// nearest obstructed one with the bit above), and notes on each path which bits it has (reachMask)
// AUTOINJECT
void AINetwork_NodesForPosition(AITarget_tag *target, CelPos_tag *celPos, obj_tag *obj, float radius) {
    AINodeSearch_tag search;
    search.radiusSq = radius * radius;
    target->built = target->goal;
    target->reachable = 0;
    target->obstructedOnly = 0;
    search.objA = target->obj;
    search.objB = obj;
    search.origin = celPos;
    search.targetBit = target->targetBit;
    search.heuristicGoal = NULL;
    search.toNode = 0;
    search.openList = NULL;

    for (uint p = 0; p < AIPathCount; p++) {
        AIPath_tag *path = &AIPaths[p];
        if (path->flags & AIPATH_NOT_NAV)
            continue;
        for (uint i = 0; i < path->nodeCount; i++)
            path->runtimeNodes[i].fileNode->flags &= ~(target->targetBit << 1 | target->targetBit);

        search.path = path;
        search.obstructed = 0;
        search.found = 0;
        AINetwork_NodeSearch(&search);
        target->obstructedOnly += search.obstructed;
        target->reachable += search.found;
        if (search.found != 0)
            path->reachMask |= target->targetBit;
        else
            path->reachMask &= ~target->targetBit;
        if (search.obstructed != 0)
            path->reachMask |= target->targetBit << 1;
        else
            path->reachMask &= ~(target->targetBit << 1);
    }
    target->builtFrame = GameState.NumFramesUnpaused;
}

// Marks the target's nodes again, unless it was marked before and has moved less than 2 m since
// AUTOINJECT
void AINetwork_BuildAITarget(AITarget_tag *target, obj_tag *obj, float radius) {
    if (target->builtFrame != 0 && Vec_Dist3D(&target->built.pos, &target->goal.pos) < 2.0f)
        return;
    AINetwork_NodesForPosition(target, &target->goal, obj, radius);
}

// Makes a drone's goal target the point
static void TargetPoint(AITarget_tag *target, AIPoint_tag *point) {
    target->targetBit = AITARGET_GOAL;
    target->obj = NULL;
    Vec_Copy(&point->pos, &target->goal.pos);
    target->goal.cel = point->cel;
    target->builtFrame = 0;
    AINetwork_NodesForPosition(target, &target->goal, NULL, 0.0f);
}

// Points the goal at a position (finding its cel if it has none), with an arrive radius (2 m for 0)
// AUTOINJECT
void AINetwork_SetupGoalPosition(AITarget_tag *target, AIPoint_tag *point, CelPos_tag *celPos, float radius) {
    if (radius == 0.0f)
        radius = 2.0f;
    if (celPos->cel == NULL)
        celPos->cel = NDrone2_FindCel(&celPos->pos);
    if (&point->pos != &celPos->pos) {
        Vec_Copy(&celPos->pos, &point->pos);
        point->cel = celPos->cel;
    }
    point->radius = radius;
    point->set = 1;
    TargetPoint(target, point);
}

// Points the goal at an object's position. The target does not keep the object.
// AUTOINJECT
bool AINetwork_SetupGoalPositionToObj(AITarget_tag *target, AIPoint_tag *point, obj_tag *obj, float radius) {
    if (radius == 0.0f)
        radius = 2.0f;
    if (obj == NULL)
        return false;
    Vec_Copy(&obj->position, &point->pos);
    point->cel = obj->inCel;
    point->radius = radius;
    point->set = 1;
    TargetPoint(target, point);
    return true;
}

// ---- bounds

// Whether a ray from one cel reaches another through the portals
// AUTOINJECT
bool AINetwork_TestRayCels(_VECTOR *from, cel_tag *fromCel, _VECTOR *to, cel_tag *toCel) {
    HITTEST_tag test;
    memset(&test, 0, sizeof(test));
    Vec_Copy(from, &test.rayStart);
    Vec_Copy(to, &test.rayEnd);
    test.radius = Vec_Dist3D(from, to);
    test.cel = fromCel;
    test.type = HITTEST_RAY;

    ushort count = 1;
    if (fromCel == NULL || toCel == NULL)
        return false;

    TestRayCels_Cels = Collide_StraddleCels(&test, &count);
    for (uint i = 0; i < count; i++) {
        if (TestRayCels_Cels[i] == toCel)
            return true;
    }
    return false;
}

static MAPNODE_tag *BoundNode(AIBoundLink_tag *link, ushort node) {
    return AIBounds[link->bound].runtimeNodes[node].fileNode;
}

// Whether a position touches a bound link: the nearest point of the link (in *closest; its squared distance in plan
// in *distSq) is within reach, the position is no higher than the outline's headroom there and no more than 0.4 m
// below the link. Links with any of skipFlags are passed over.
// AUTOINJECT
bool AINetwork_HitBoundry(_VECTOR *closest, AIBoundLink_tag *link, _VECTOR *pos, float radiusSq, float *distSq,
                          uint skipFlags) {
    if (skipFlags & link->flags)
        return false;
    MAPNODE_tag *a = BoundNode(link, link->a);
    MAPNODE_tag *b = BoundNode(link, link->b);
    _VECTOR edge;
    Vec_Subtract(&b->pos, &a->pos, &edge);
    double lengthSq = (double)edge.z * edge.z + (double)edge.y * edge.y + (double)edge.x * edge.x;
    float t = 0.0f;
    if (!((lengthSq < 0.0 ? -lengthSq : lengthSq) > 1e-20f)) {
        Vec_Copy(&a->pos, closest);
    } else {
        _VECTOR offset;
        Vec_Subtract(pos, &a->pos, &offset);
        double along = ((double)offset.z * edge.z + (double)offset.y * edge.y + (double)offset.x * edge.x)
                       / (float)lengthSq;
        t = (float)along;
        if (along > 1.0) {
            t = 1.0f;
            Vec_Copy(&b->pos, closest);
        } else if (t < 0.0f) {
            t = 0.0f;
            Vec_Copy(&a->pos, closest);
        } else {
            auxVec_AddMulR32(&a->pos, &edge, t, closest);
        }
    }

    double squared = Vec_SqDist2D(pos, closest);
    *distSq = (float)squared;
    if (squared > radiusSq)
        return false;
    double top = ((double)b->headroom - a->headroom) * t + a->headroom + closest->y;
    if (pos->y > top)
        return false;
    return !((double)closest->y - 0.4f > pos->y);
}

// Whether the segment from-to crosses a bound link in plan, at a height between 2.4 m below the outline and its
// headroom there. The crossing goes in *hit (when given) at the outline's height; with `nearest`, only a crossing
// nearer `from` in plan than *nearest counts as a new hit, and then *hit gets node a's height. With raiseHeadroom,
// a crossing more than 2 m above the outline sets both nodes' headroom to its height less 0.5.
// AUTOINJECT
bool AINetwork_LineIntersectsBoundry(AIBoundLink_tag *link, _VECTOR *from, _VECTOR *to, _VECTOR *hit, float *nearest,
                                     char raiseHeadroom) {
    MAPNODE_tag *a = BoundNode(link, link->a);
    MAPNODE_tag *b = BoundNode(link, link->b);
    _VECTOR crossing;
    crossing.y = 0.0f;
    if (!vecutil_intersect_line_line_2d(from, to, &a->pos, &b->pos, &crossing))
        return false;

    // How far along each segment the crossing is, in plan
    float fromDistance = (float)Vec_Dist2D(from, &crossing);
    double t = fromDistance / Vec_Dist2D(from, to);
    float rayY = (float)(((double)to->y - from->y) * t + from->y);
    float aDistance = (float)Vec_Dist2D(&a->pos, &crossing);
    double s = aDistance / Vec_Dist2D(&a->pos, &b->pos);
    float boundY = (float)(((double)b->pos.y - a->pos.y) * s + a->pos.y);
    crossing.y = boundY;

    double top = ((double)b->headroom - a->headroom) * s + a->headroom + boundY;
    if (rayY > top || (double)boundY - 2.4f > rayY)
        return false;
    if (raiseHeadroom && (double)rayY - boundY > 2.0) {
        float headroom = rayY - 0.5f;
        b->headroom = headroom;
        a->headroom = headroom;
    }

    if (nearest != NULL) {
        _VECTOR delta;
        crossing.y = from->y;
        Vec_Subtract(&crossing, from, &delta);
        float distance = Vec_Magnitude(&delta);
        if (!(distance < *nearest))
            return true;
        *nearest = distance;
        crossing.y = a->pos.y;
    }
    if (hit != NULL)
        Vec_Copy(&crossing, hit);
    return true;
}

// Pushes a sphere out of the bound links of its cel, one after another; the total push goes in *push. Door links
// (AIBOUND_DOOR) count at twice the radius unless flags has AIBOUND_DOOR; links with any of flags are passed over.
// AUTOINJECT
bool AINetwork_GetBoundsPushVectorForSphere(_VECTOR *pos, cel_tag *cel, float radius, _VECTOR *push, uint flags) {
    bool pushed = false;
    _VECTOR moved;
    Vec_Copy(pos, &moved);
    float radiusSq = radius * radius;

    AIBoundLink_tag *link = cel->boundLinks;
    while (link != NULL) {
        float reach = radius;
        float reachSq = radiusSq;
        MAPNODE_tag *a = BoundNode(link, link->a);
        MAPNODE_tag *b = BoundNode(link, link->b);
        if (!(flags & AIBOUND_DOOR) && (link->flags & AIBOUND_DOOR)) {
            reach = radius + radius;
            reachSq = reach * reach;
        }

        _VECTOR closest;
        float distSq;
        if (AINetwork_HitBoundry(&closest, link, &moved, reachSq, &distSq, flags)) {
            _VECTOR away;
            Vec_Subtract(&moved, &closest, &away);
            away.y = 0.0f;
            float depth = reach - Vec_NormaliseLen(&away, &away);
            auxVec_AddMulR32(&moved, &away, depth, &moved);
            pushed = true;
        }

        if (a->cel == cel)
            link = link->nextA;
        else if (b->cel == cel)
            link = link->nextB;
        else
            break;
    }
    Vec_Subtract(&moved, pos, push);
    return pushed;
}

// The height test of the bound corner checks: within 2 m
static bool NearHeight(float y, float nodeY) {
    double difference = (double)y - nodeY;
    if (difference < 0.0)
        difference = -difference;
    return difference < 2.0f;
}

// Whether a straight walk from one cel to another crosses no bound link: 0 when it does, or when the ray never
// reaches toCel. With hit, every link is tried, the nearest crossing goes in *hit and the result is 0xff. Strict
// (without hit) also refuses passing within 0.4 m of a link's corner (unless the walk starts or ends there).
// AUTOINJECT
uchar AINetwork_BoundsTest(_VECTOR *from, cel_tag *fromCel, _VECTOR *to, cel_tag *toCel, _VECTOR *hit, char strict) {
    HITTEST_tag test;
    memset(&test, 0, sizeof(test));
    float nearest = 100000.0f;
    float *nearestOut = NULL;
    Vec_Copy(from, &test.rayStart);
    Vec_Copy(to, &test.rayEnd);
    test.rayStart.y += 1.0f;
    test.rayEnd.y += 1.0f;
    test.radius = Vec_Dist3D(from, to);
    test.cel = fromCel;
    test.type = HITTEST_RAY;

    ushort count = 1;
    if (fromCel == NULL)
        return 0;

    BoundsTest_Cels = Collide_StraddleCels(&test, &count);
    uint reached = 0;
    while (reached < count && BoundsTest_Cels[reached] != toCel)
        reached++;
    if (reached == count)
        return 0;

    _VECTOR closest;
    closest.y = 0.0f;
    uchar result = 1;
    if (hit != NULL)
        nearestOut = &nearest;
    for (ushort i = 0; i < count; i++) {
        cel_tag *cel = BoundsTest_Cels[i];
        AIBoundLink_tag *link = cel->boundLinks;
        while (link != NULL) {
            MAPNODE_tag *a = BoundNode(link, link->a);
            MAPNODE_tag *b = BoundNode(link, link->b);
            if (AINetwork_LineIntersectsBoundry(link, from, to, hit, nearestOut, 0)) {
                if (hit == NULL)
                    return 0;
                result = 0xff;
            }
            if (strict && hit == NULL && Vec_SqDist2D(&a->pos, to) > CORNER_CLEARANCE_SQ && Vec_SqDist2D(&b->pos, to) > CORNER_CLEARANCE_SQ
                && Vec_SqDist2D(&a->pos, from) > CORNER_CLEARANCE_SQ && Vec_SqDist2D(&b->pos, from) > CORNER_CLEARANCE_SQ) {
                vecutil_calculate_closest_point_on_line(&closest, from, to, &a->pos);
                if (Vec_SqDist2D(&closest, &a->pos) < CORNER_CLEARANCE_SQ && NearHeight(closest.y, a->pos.y))
                    return 0;
                // Node b's test takes the distance rather than its square, and node a's height
                vecutil_calculate_closest_point_on_line(&closest, from, to, &b->pos);
                if (Vec_Dist2D(&closest, &b->pos) < CORNER_CLEARANCE_SQ && NearHeight(closest.y, a->pos.y))
                    return 0;
            }

            if (a->cel == cel)
                link = link->nextA;
            else if (b->cel == cel)
                link = link->nextB;
            else
                break;
        }
    }
    return result;
}

// Whether the segment from-to keeps clear of a bound corner: closer than the limit in plan, and within 2 m of the
// height of the link's node a. The limit is the radius squared, halved near either end of the segment.
static bool PassesCorner(_VECTOR *from, _VECTOR *to, MAPNODE_tag *corner, MAPNODE_tag *a, float radiusSq, float nearEnd,
                         _VECTOR *closest) {
    float limit = radiusSq;
    if (Vec_SqDist2D(&corner->pos, to) < nearEnd || Vec_SqDist2D(&corner->pos, from) < nearEnd)
        limit = radiusSq * 0.5f;
    vecutil_calculate_closest_point_on_line(closest, from, to, &corner->pos);
    return !(Vec_SqDist2D(closest, &corner->pos) < limit && NearHeight(closest->y, a->pos.y));
}

// Whether a step from one cel to another passes no bound corner within the radius (see PassesCorner). False when the
// ray never reaches toCel (when one is given).
// AUTOINJECT
bool AINetwork_BoundsNodeTest(_VECTOR *from, cel_tag *fromCel, _VECTOR *to, cel_tag *toCel, float radius) {
    HITTEST_tag test;
    memset(&test, 0, sizeof(test));
    Vec_Copy(from, &test.rayStart);
    Vec_Copy(to, &test.rayEnd);
    test.radius = Vec_Dist3D(from, to);
    test.cel = fromCel;
    test.type = HITTEST_RAY;

    ushort count = 1;
    if (fromCel == NULL)
        return false;

    cel_tag **cels = Collide_StraddleCels(&test, &count);
    if (toCel != NULL) {
        uint reached = 0;
        while (reached < count && cels[reached] != toCel)
            reached++;
        if (reached == count)
            return false;
    }

    double squared = (double)radius * radius;
    float radiusSq = (float)squared;
    float nearEnd = (float)(squared * 1.3f);
    _VECTOR closest;
    closest.y = 0.0f;
    for (ushort i = 0; i < count; i++) {
        cel_tag *cel = cels[i];
        AIBoundLink_tag *link = cel->boundLinks;
        while (link != NULL) {
            MAPNODE_tag *a = BoundNode(link, link->a);
            MAPNODE_tag *b = BoundNode(link, link->b);
            if (!PassesCorner(from, to, a, a, radiusSq, nearEnd, &closest)
                || !PassesCorner(from, to, b, a, radiusSq, nearEnd, &closest))
                return false;

            if (a->cel == cel)
                link = link->nextA;
            else if (b->cel == cel)
                link = link->nextB;
            else
                break;
        }
    }
    return true;
}

// How far from-to goes before the first bound link it crosses: *furthest gets the crossing nearest `from` (or `to`)
// AUTOINJECT
bool AINetwork_FurthestPosition(_VECTOR *from, _VECTOR *to, _VECTOR *furthest) {
    if (AIBoundCount == 0)
        return false;
    float nearest = FLT_MAX;
    bool crossed = false;
    Vec_Copy(to, furthest);
    for (uint b = 0; b < AIBoundCount; b++) {
        for (uint i = 0; i < AIBounds[b].linkCount; i++) {
            if (AINetwork_LineIntersectsBoundry(&AIBounds[b].links[i], from, to, furthest, &nearest, 0))
                crossed = true;
        }
    }
    return crossed;
}

// Gives setFlags to every bound link from-to crosses, except those with any of skipFlags or that have them already
// AUTOINJECT
bool AINetwork_ModIntersectedLinkFlags_Bounds(_VECTOR *from, _VECTOR *to, uint setFlags, uint skipFlags,
                                              _VECTOR *hit, char raiseHeadroom) {
    bool changed = false;
    for (uint b = 0; b < AIBoundCount; b++) {
        for (uint i = 0; i < AIBounds[b].linkCount; i++) {
            AIBoundLink_tag *link = &AIBounds[b].links[i];
            if ((skipFlags & link->flags) || (link->flags & setFlags) == setFlags)
                continue;
            if (AINetwork_LineIntersectsBoundry(link, from, to, hit, NULL, raiseHeadroom)) {
                link->flags |= setFlags;
                changed = true;
            }
        }
    }
    return changed;
}

// Marks the bound links that nav links cross as passable, raising their headroom over high crossings
// AUTOINJECT
bool AINetwork_InitPassableBoundries(void) {
    for (uint p = 0; p < AIPathCount; p++) {
        AIPath_tag *path = &AIPaths[p];
        for (uint i = 0; i < path->linkCount; i++) {
            AILink_tag *link = &path->links[i];
            AINetwork_ModIntersectedLinkFlags_Bounds(&path->runtimeNodes[link->a].fileNode->pos,
                                                     &path->runtimeNodes[link->b].fileNode->pos, AIBOUND_PASSABLE,
                                                     AIBOUND_DOOR | AIBOUND_KICK, NULL, 1);
        }
    }
    return true;
}

// ---- link flags

// Clears flags on every link and its count of routes (each NPC frame, from Drone_InitComms)
// AUTOINJECT
void AINetwork_ClearLinkFlags(ushort flags) {
    for (uint p = 0; p < AIPathCount; p++) {
        for (uint i = 0; i < AIPaths[p].linkCount; i++) {
            AIPaths[p].links[i].flags &= ~flags;
            AIPaths[p].links[i].used = 0;
        }
    }
}

// Counts the route on each link it has still to follow (not on patrol and mission paths). A missing link counts on
// link 0xffff, as in the original.
// AUTOINJECT
void AINetwork_UpdateLinksUsedCount(AIRoute_tag *route) {
    AIPath_tag *path = route->path;
    if (path == NULL || (path->flags & (AIPATH_PATROL | AIPATH_MISSION)))
        return;

    short first = route->current - 1;
    if (first < 0)
        first = 0;
    ushort prev = route->nodes[first];
    for (ushort i = first + 1; i < route->count; i++) {
        ushort node = route->nodes[i];
        route->path->links[AINetwork_FindLink(route->path, node, prev)].used++;
        prev = node;
    }
}

// One path's part of AINetwork_SetLinksFlagInCircle. The original takes the path in EDI.
static void SetPathLinksFlagInCircle(AIPath_tag *path, _VECTOR *centre, float radius, ushort setFlags,
                                     ushort clearFlags) {
    float radiusSq = radius * radius;
    for (uint i = 0; i < path->linkCount; i++) {
        AILink_tag *link = &path->links[i];
        if (clearFlags != 0)
            link->flags &= ~clearFlags;
        MAPNODE_tag *a = path->runtimeNodes[link->a].fileNode;
        MAPNODE_tag *b = path->runtimeNodes[link->b].fileNode;
        float low = a->pos.y < b->pos.y ? a->pos.y : b->pos.y;
        float high = a->pos.y > b->pos.y ? a->pos.y : b->pos.y;
        if (low > centre->y + 2.0 || centre->y - 2.0 > high)
            continue;
        _VECTOR closest;
        vecutil_calculate_closest_point_on_line(&closest, &a->pos, &b->pos, centre);
        if (Vec_SqDist2D(&closest, centre) <= radiusSq)
            link->flags |= setFlags;
    }
}

// Clears clearFlags on every link, and gives setFlags to those that pass within the radius of the centre in plan
// (with an end within 2 m of its height). The cel is not used.
// AUTOINJECT
void AINetwork_SetLinksFlagInCircle(_VECTOR *centre, cel_tag *cel, float radius, ushort setFlags, ushort clearFlags) {
    for (uint p = 0; p < AIPathCount; p++)
        SetPathLinksFlagInCircle(&AIPaths[p], centre, radius, setFlags, clearFlags);
}

// ---- emitters

// Gives an emitter a byte per node of its path (`size` is the path's node count, or the largest over all paths for
// the drones' shared safety emitter). Marked allocated before the allocation, and even if it fails; the data is left
// as Mem_Malloc gives it (AINetwork_EmitPath fills it).
// AUTOINJECT
void AINetwork_AllocEmitter(AIEmitter_tag *emitter, uint size) {
    emitter->allocated = 1;
    emitter->size = size;
    emitter->data = (uchar *)Mem_Malloc(size, MALLOC_BOT_PATH3_ALIGN8, 0);
}

// Frees an emitter's data and clears the whole emitter. An emitter without data (never allocated, already freed, or
// whose allocation failed) is left exactly as it is - including its allocated flag.
// AUTOINJECT
void AINetwork_FreeEmitter(AIEmitter_tag *emitter) {
    if (emitter == NULL || emitter->data == NULL)
        return;
    Mem_Free((void **)&emitter->data);
    memset(emitter, 0, sizeof(AIEmitter_tag));
}

// Floods the emitter's path from the node nearest its position (Dijkstra, with A*'s link costs, up to maxCost; 255
// for 0) and stores each node's cost: cost * 255/256, at most 254, or 255 where the flood did not reach. Returns 1, or 0
// without data, a path or a start node, in the whole of EAX (DroneMove_FindSafetyFromScaryPosition tests it all).
// AUTOINJECT
uint AINetwork_EmitPath(AIEmitter_tag *emitter, float maxCost) {
    if (emitter == NULL || emitter->data == NULL || emitter->path == NULL)
        return 0;

    AINode_tag open, closed;    // dummy heads: only nextInList is used
    open.nextInList = NULL;
    closed.nextInList = NULL;
    if (maxCost == 0.0f)
        maxCost = 255.0f;
    for (int i = 0; i < (int)emitter->path->nodeCount; i++) {
        emitter->path->runtimeNodes[i].fileNode->flags &= AINODE_LIST_MASK;
        emitter->path->runtimeNodes[i].g = 255.0f;
        emitter->data[i] = 0xff;
    }

    AINodeSearch_tag search;
    search.origin = &emitter->celPos;
    search.heuristicGoal = NULL;
    search.path = emitter->path;
    search.toNode = 1;
    search.openList = &open;
    search.objA = emitter->obj;
    search.objB = NULL;
    search.targetBit = 0;
    search.radiusSq = ROUTE_START_RADIUS_SQ;
    search.found = 0;
    search.obstructed = 0;
    AINetwork_NodeSearch(&search);
    if (search.found <= 0)
        return 0;
    emitter->startNode = search.resultNode;

    AIPath_tag *path = emitter->path;
    while (open.nextInList != NULL) {
        AINode_tag *node = OpenListPop(&open);
        ushort *links = &path->nodeLinks[node->firstLink];
        for (int i = 0; i < node->linkCount; i++) {
            int index = Neighbour(path, node, i);
            NF_WARN_IF(index < 0, "Link refers to non-existant AI path\n"); // GC check (0x800a5388)
            AINode_tag *next = &path->runtimeNodes[index];
            if (DoorClosed(node, next))
                continue;
            AILink_tag *link = &path->links[links[i]];
            double cost = (double)(link->used + 1) * link->length + node->g;
            if (cost > maxCost)
                continue;
            if ((next->fileNode->flags & AINODE_LISTED) && cost >= next->g)
                continue;

            next->g = (float)cost;
            next->parent = node;
            next->f = (float)cost;
            next->h = 0.0f;
            next->parentLink = i;
            if (next->fileNode->flags & AINODE_CLOSED)
                ClosedListRemove(&closed, next);
            if (!(next->fileNode->flags & AINODE_OPEN))
                OpenListInsert(&open, next);
        }
        node->nextInList = closed.nextInList;
        closed.nextInList = node;
        node->fileNode->flags |= AINODE_OPEN;
    }

    if (emitter->data != NULL) {
        for (int i = 0; i < (int)emitter->path->nodeCount; i++) {
            float cost = emitter->path->runtimeNodes[i].g;
            uint value = 0xff;
            if (cost != 255.0f) {
                double scaled = (double)cost * (1.0f / 256.0f);
                if (scaled < 0.0)
                    scaled = -scaled;
                value = Ftol(scaled * 255.0f);
                if (value > 0xfe)
                    value = 0xfe;
            }
            emitter->data[i] = value;
        }
    }
    return 1;
}

// Walks away from an emitter, from `node` (or the emitter's own node, or the one nearest it), through the path until
// it reaches a node whose byte is at least `distance`; that node goes in *found. Nodes already visited are passed
// over. False when the walk runs out first.
// AUTOINJECT
bool AINetwork_Emitter_GetNodeAtDistance(short node, float distance, AIEmitter_tag *emitter, MAPNODE_tag **found) {
    if (node < 0) {
        node = emitter->startNode;
        if (node < 0) {
            node = NDrone2_NearestNode(&emitter->celPos, emitter->path, emitter->obj);
            if (node < 0)
                return false;
        }
    }

    AIPath_tag *path = emitter->path;
    AINode_tag *start = &path->runtimeNodes[node];
    uchar startValue = emitter->data[node];
    uchar wanted = Ftol(distance);
    for (int i = 0; i < (int)emitter->path->nodeCount; i++)
        emitter->path->runtimeNodes[i].fileNode->flags &= AINODE_LIST_MASK;

    AINode_tag open, closed;    // dummy heads: only nextInList is used
    closed.nextInList = NULL;
    start->nextInList = NULL;
    start->fileNode->flags |= AINODE_OPEN;
    open.nextInList = start;
    path = emitter->path;
    while (open.nextInList != NULL) {
        AINode_tag *current = OpenListPop(&open);
        for (int i = 0; i < current->linkCount; i++) {
            int index = Neighbour(path, current, i);
            NF_WARN_IF(index < 0, "Link refers to non-existant AI path\n"); // GC check (0x800a5738)
            uchar value = emitter->data[index];
            AINode_tag *next = &path->runtimeNodes[index];
            if (DoorClosed(current, next))
                continue;
            if (value >= wanted) {
                if (found != NULL)
                    *found = next->fileNode;
                return true;
            }
            uint flags = next->fileNode->flags;
            if ((flags & AINODE_LISTED) && value < startValue)
                continue;
            if (flags & AINODE_CLOSED)
                ClosedListRemove(&closed, next);
            if (!(next->fileNode->flags & AINODE_OPEN))
                OpenListInsert(&open, next);
        }
        current->nextInList = closed.nextInList;
        closed.nextInList = current;
        current->fileNode->flags |= AINODE_OPEN;
    }
    return false;
}

// An emitter at a position: its cel (looked up when not given), its path (the nav path there when not given), and
// its data allocated
// AUTOINJECT
bool AINetwork_InitEmitter(obj_tag *obj, _VECTOR *pos, cel_tag *cel, AIEmitter_tag *emitter, AIPath_tag *path) {
    if (emitter == NULL)
        return false;
    if (cel == NULL) {
        cel = build_FindCel(pos, glb_world);
        if (cel == NULL) {
            NF_WARN("ERROR: AIPoint or Corner Cover not in cel - adjust position\n"); // GC check (0x800a504c)
            return false;
        }
    }
    emitter->data = NULL;
    emitter->obj = obj;
    Vec_Copy(pos, &emitter->celPos.pos);
    emitter->celPos.cel = cel;
    if (path == NULL)
        path = AINetwork_NavPathForPosition(&emitter->celPos.pos, cel, 0);
    emitter->path = path;
    if (path == NULL || path->nodeCount == 0) {
        NF_WARN("NO NAVIGATION PATH NEAR EMITTER (AIPOINT/COVER)\n"); // GC check (0x800a504c)
        return false;
    }
    AINetwork_AllocEmitter(emitter, path->nodeCount);
    return true;
}

// The same, keeping the data of an emitter already allocated
// AUTOINJECT
bool AINetwork_InitEmitter2(obj_tag *obj, _VECTOR *pos, cel_tag *cel, AIEmitter_tag *emitter, AIPath_tag *path) {
    if (emitter == NULL)
        return false;
    if (cel == NULL) {
        cel = build_FindCel(pos, glb_world);
        if (cel == NULL) {
            NF_WARN("ERROR: AIPoint or Corner Cover not in cel - adjust position\n"); // GC check (0x800a5180)
            return false;
        }
    }
    emitter->obj = obj;
    Vec_Copy(pos, &emitter->celPos.pos);
    emitter->celPos.cel = cel;
    if (path == NULL)
        path = AINetwork_NavPathForPosition(&emitter->celPos.pos, cel, 0);
    emitter->path = path;
    if (path == NULL || path->nodeCount == 0) {
        NF_WARN("NO NAVIGATION PATH NEAR EMITTER (AIPOINT/COVER)\n"); // GC check (0x800a5180)
        return false;
    }
    if (!emitter->allocated)
        AINetwork_AllocEmitter(emitter, path->nodeCount);
    return true;
}

// ---- planning a route

// For a partial route: when the walk from the last node to the destination crosses a bound, the destination moves
// back to 1 m short of the crossing
bool _AINetwork_AlterDestFor_DROUTE_Nearest(AIRoute_tag *route) {
    MAPNODE_tag *last = RouteNode(route, route->count - 1);
    _VECTOR hit, back;
    if (NDrone2_MoveTestFromNode(last, &route->dest, NULL, NULL, &hit, 0, 1) != -1)
        return false;
    Vec_Subtract(&hit, &last->pos, &back);
    Vec_Normalise(&back, &back);
    Vec_Subtract(&hit, &back, &hit);
    Vec_Copy(&hit, &route->dest.pos);
    route->dest.cel = NDrone2_FindCel(&route->dest.pos);
    route->flags |= AIROUTE_DEST_MOVED;
    return true;
}

// The original takes the route in ESI.
// AUTOLTCG
void __declspec(naked) AINetwork_AlterDestFor_DROUTE_Nearest(void) {
    _asm {
        push esi
        call _AINetwork_AlterDestFor_DROUTE_Nearest
        add esp, 4
        ret
    }
}

// Whether a route may skip a node, going from the one before to the one after (-1 for none): not when the node and
// either neighbour are both door nodes, nor both kick nodes. A node out of the path counts as one without flags, and
// a skipped node out of the path is never skipped. The original takes the route in ECX, the node in AX and the one
// before in DX.
static bool CanSkipNode(AIRoute_tag *route, short node, short before, short after) {
    AIPath_tag *path = route->path;
    short count = path->nodeCount;  // compared as 16 bits
    if (node < 0 || node >= count)
        return false;
    MAPNODE_tag *skipped = path->runtimeNodes[node].fileNode;
    MAPNODE_tag *prev = before >= 0 && before < count ? path->runtimeNodes[before].fileNode : NULL;
    MAPNODE_tag *next = after >= 0 && after < count ? path->runtimeNodes[after].fileNode : NULL;
    uint skippedFlags = skipped != NULL ? skipped->flags : 0;
    uint prevFlags = prev != NULL ? prev->flags : 0;
    uint nextFlags = next != NULL ? next->flags : 0;
    if ((skippedFlags & AINODE_DOOR) && ((prevFlags & AINODE_DOOR) || (nextFlags & AINODE_DOOR)))
        return false;
    if ((skippedFlags & AINODE_KICK) && ((prevFlags & AINODE_KICK) || (nextFlags & AINODE_KICK)))
        return false;
    return true;
}

// Drops a planned route's first node when the start can be walked straight to the second, and its last when the
// destination can be walked to the one before (door and kick nodes excepted), then works out the distance again.
// Returns the status it is given. The distance adds, for each node after the first, the length of the link whose
// index is the node's parentLink - a slot in its parent's link list, not a link index - as the original does.
uint _AINetwork_OptimiseRoute(AIRoute_tag *route, char dropFirst, uint status, char dropLast) {
    AINode_tag *nodes = route->path->runtimeNodes;
    ushort dropped = 0;
    if (dropFirst && route->count > 1) {
        ushort second = route->nodes[1];
        if (CanSkipNode(route, route->nodes[0], -1, second)
            && NDrone2_MoveTestToNode(&route->start, nodes[second].fileNode, route->targetObj, route->mover, NULL, 1,
                                      1) == 1) {
            dropped = 1;
            route->startNode = route->nodes[1];
        }
    }
    if (dropLast && route->count > dropped + 1) {
        ushort beforeLast = route->nodes[route->count - 2];
        if (CanSkipNode(route, route->nodes[route->count - 1], beforeLast, -1)
            && NDrone2_MoveTestToNode(&route->dest, nodes[beforeLast].fileNode, route->targetObj, route->mover, NULL,
                                      1, 1) == 1) {
            route->endNode = route->nodes[route->count - 2];
            route->count--;
        }
    }
    if (dropped > 0) {
        route->count -= dropped;
        for (short i = 0; i < route->count; i++)
            route->nodes[i] = route->nodes[i + dropped];
    }

    double distance = Vec_Dist2D(&route->start.pos, &nodes[route->nodes[0]].fileNode->pos);
    float travelled = (float)distance;
    if (route->count > 1) {
        for (short i = 1; i < route->count; i++)
            distance += route->path->links[nodes[route->nodes[i]].parentLink].length;
        travelled = (float)distance;
    }
    route->remaining =
        (float)(Vec_Dist2D(&route->dest.pos, &nodes[route->nodes[route->count - 1]].fileNode->pos) + travelled);
    return status;
}

// The original takes the route in ESI and dropFirst in AL, then the status and dropLast on the stack.
// AUTOLTCG
void __declspec(naked) AINetwork_OptimiseRoute(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push esi
        call _AINetwork_OptimiseRoute
        add esp, 16
        ret
    }
}

// Sets a route up to an object: the target is the shared player target when the object is the player, otherwise
// `target`, which is pointed at the object either way
// AUTOINJECT
bool AINetwork_SetupRouteToObject(obj_tag *mover, obj_tag *targetObj, AITarget_tag *target, AIPoint_tag *point,
                                  float radius, AIRoute_tag *route) {
    if (targetObj == NULL || route == NULL)
        return false;
    AINetwork_ResetRoute(route, mover, targetObj);
    route->flags &= ~AIROUTE_VALID;
    route->target = targetObj == NDrone2_Player() ? &NPCGlobals.playerTarget : target;
    AINetwork_SetupGoalPositionToObj(target, point, targetObj, radius);
    NDrone2_FeetPos(mover, &route->start.pos);
    route->start.cel = mover->inCel;
    NDrone2_FeetPos(targetObj, &route->dest.pos);
    route->dest.cel = targetObj->inCel;
    return true;
}

// Sets a route up to a position (finding its cel when it has none; false when there is none)
bool _AINetwork_SetupRouteToPosition(AIRoute_tag *route, CelPos_tag *dest, obj_tag *mover, AITarget_tag *target,
                                     AIPoint_tag *point, float radius) {
    if (dest == NULL || route == NULL)
        return false;
    ClearRoute(route);
    route->flags &= ~AIROUTE_VALID;
    route->status = AIROUTE_RESET;
    route->mover = mover;
    route->targetObj = NULL;
    if (dest->cel == NULL) {
        dest->cel = NDrone2_FindCel(&dest->pos);
        if (dest->cel == NULL)
            return false;
    }
    route->target = target;
    AINetwork_SetupGoalPosition(target, point, dest, radius);
    NDrone2_FeetPos(mover, &route->start.pos);
    route->start.cel = mover->inCel;
    Vec_Copy(&dest->pos, &route->dest.pos);
    route->dest.cel = dest->cel;
    route->arriveRadius = radius;
    return true;
}

// The original takes the route in ESI and the destination in EDI, the rest on the stack.
// AUTOLTCG
void __declspec(naked) AINetwork_SetupRouteToPosition(void) {
    _asm {
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push edi
        push esi
        call _AINetwork_SetupRouteToPosition
        add esp, 24
        ret
    }
}

// Keeps a route to an object while it is still good - the same object (any object in multiplayer), a target not
// rebuilt since, and valid - and otherwise sets it up again
// AUTOINJECT
bool AINetwork_VerifyRouteTarget(obj_tag *mover, obj_tag *targetObj, AITarget_tag *target, AIPoint_tag *point,
                                 float radius, AIRoute_tag *route) {
    AITarget_tag *current = route->target;
    if (current != NULL && (MPSettings.maybeDroneAIEnabled != 0 || targetObj == current->obj)
        && current->builtFrame <= route->targetFrame && (route->flags & AIROUTE_VALID))
        return true;
    return AINetwork_SetupRouteToObject(mover, targetObj, target, point, radius, route);
}

// Keeps a route to a position while it is still good - a target without an object within 1 m of the position, and
// valid - and otherwise sets it up again
// FUNC_AT(0x00049c50)
bool AINetwork_VerifyRouteToPosition(obj_tag *mover, CelPos_tag *dest, AITarget_tag *target, AIPoint_tag *point,
                                     float radius, AIRoute_tag *route) {
    if (dest == NULL || route == NULL)
        return false;
    AITarget_tag *current = route->target;
    if (current != NULL && current->obj == NULL && !(Vec_SqDist2D(&dest->pos, &current->goal.pos) > 1.0f)
        && AINetwork_RouteIsValid(route))
        return true;
    return _AINetwork_SetupRouteToPosition(route, dest, mover, target, point, radius);
}

// Plans a set-up route: straight there when the mover can walk it (unless AIROUTE_NO_DIRECT), otherwise A* over the
// route's path, shortened, and the first link's creep started. Returns the status.
uint _AINetwork_CalcRoute(AIRoute_tag *route, obj_tag *mover, obj_tag *targetObj) {
    ClearRoute(route);
    route->status = AIROUTE_RESET;
    route->mover = mover;
    route->targetObj = targetObj;
    if (route->target == NULL)
        return AIROUTE_NO_NETWORK;

    NDrone2_FeetPos(mover, &route->start.pos);
    route->start.cel = mover->inCel;
    route->targetFrame = route->target->builtFrame;
    if (!(route->flags & AIROUTE_NO_DIRECT)
        && NDrone2_MoveTest(&route->start, &route->dest, targetObj, mover, NULL, 1, 1) == 1) {
        LinkCreep_CalcToRouteEnd(route);
        return route->status;
    }

    if (route->path == NULL || route->path->nodeCount == 0) {
        route->status = AIROUTE_NO_NETWORK;
        return AIROUTE_NO_NETWORK;
    }
    uint result = AINetwork_DoAStarPath(route, targetObj, mover, route->target->targetBit);
    route->status = result;
    if (result > AIROUTE_PARTIAL)
        return result;
    result = _AINetwork_OptimiseRoute(route, 1, result, 1);
    route->flags |= AIROUTE_VALID;
    LinkCreep_Calc(route, 0);
    return result;
}

// The original takes the route in ECX, the mover in EAX and the target object in EBX; the argument its callers push
// is not read.
// AUTOLTCG
void __declspec(naked) AINetwork_CalcRoute(void) {
    _asm {
        push ebx
        push eax
        push ecx
        call _AINetwork_CalcRoute
        add esp, 12
        ret
    }
}

// Plans a route to an object, keeping the one there is while it is good. A partial route has its destination moved
// short of the bound in the way. The last argument is not read.
// AUTOINJECT
uint AINetwork_CalcRouteToObject(obj_tag *mover, obj_tag *targetObj, AIRoute_tag *route, AITarget_tag *target,
                                 AIPoint_tag *point, float radius, uint unused) {
    if (!AINetwork_VerifyRouteTarget(mover, targetObj, target, point, radius, route))
        return AIROUTE_NO_NETWORK;
    uint result = _AINetwork_CalcRoute(route, mover, targetObj);
    if (result == AIROUTE_PARTIAL)
        _AINetwork_AlterDestFor_DROUTE_Nearest(route);
    return result;
}

// The same to a position
// AUTOINJECT
uint AINetwork_CalcRouteToPosition(obj_tag *mover, CelPos_tag *dest, AIRoute_tag *route, AITarget_tag *target,
                                   AIPoint_tag *point, float radius, uint unused) {
    if (!AINetwork_VerifyRouteToPosition(mover, dest, target, point, radius, route))
        return AIROUTE_NO_NETWORK;
    uint result = _AINetwork_CalcRoute(route, mover, NULL);
    if (result == AIROUTE_PARTIAL)
        _AINetwork_AlterDestFor_DROUTE_Nearest(route);
    return result;
}

// ---- following a route

// Steers a route at a position (finding its cel first when it has none). The original takes the position in ESI and
// the route in EDI.
static void SetWaypoint(AIRoute_tag *route, CelPos_tag *pos) {
    route->waypoint.pos = pos->pos;
    if (pos->cel == NULL)
        pos->cel = NDrone2_FindCel(&pos->pos);
    route->waypoint.cel = pos->cel;
}

// One step along the creep's link
// AUTOINJECT
bool LinkCreep_Dest(AIRoute_tag *route, short offset, CelPos_tag *out) {
    int step = route->creep.step + offset;
    Vec_MulR32(&out->pos, &route->creep.delta, (float)step);
    Vec_Add2(&route->creep.segmentStart, &out->pos, &out->pos);
    out->cel = build_FindCel(&out->pos, glb_world);
    return true;
}

// Starts creeping along a segment in 0.4 m steps (one step for a segment no longer than that), from its start or
// from its last step, and steers at it
// FUNC_AT(0x0004a990)
bool LinkCreep_Setup(AIRoute_tag *route, _VECTOR *from, _VECTOR *to, char fromEnd) {
    Vec_Copy(from, &route->creep.segmentStart);
    Vec_Copy(to, &route->creep.segmentEnd);
    Vec_Subtract(to, from, &route->creep.delta);
    float length = Vec_Magnitude(&route->creep.delta);
    if (length > CREEP_STEP) {
        // int(length) * 2.5 steps, each truncated as __ftol2 does
        route->creep.steps = Ftol((double)(ushort)Ftol(length) * 2.5f);
        Vec_MulR32(&route->creep.delta, &route->creep.delta, (float)(CREEP_STEP / (double)length));
    } else {
        route->creep.steps = 1;
    }
    route->creep.path = route->path;
    route->creep.step = 0;
    route->creep.active = 1;
    if (fromEnd)
        route->creep.step = route->creep.steps - 1;
    LinkCreep_Dest(route, 0, &route->waypoint);
    return route->creep.active;
}

// Starts creeping from one route index to another: from < 0 starts at the route's start, to < 0 ends at its
// destination, and the same index twice (except on a loop) goes from that node to the destination. The link crept is
// marked AILINK_CREEPING; a start at a special node, or the end of a once-only route, sets specialPending.
// AUTOINJECT
bool LinkCreep_ForNodes(AIRoute_tag *route, int from, int to, char fromEnd) {
    route->creep.unknown02 = 0;
    route->creep.specialPending = 0;
    _VECTOR *start, *end;
    if (from < 0) {
        start = &route->start.pos;
        end = to >= 0 ? &RouteNode(route, to)->pos : &route->dest.pos;
    } else {
        MAPNODE_tag *fromNode = RouteNode(route, from);
        if (fromNode->flags & AINODE_SPECIAL)
            route->creep.specialPending = 1;
        uint mode = route->flags & AIROUTE_MODE;
        if (from == to && mode != AIROUTE_LOOP) {
            if (mode == AIROUTE_ONCE)
                route->creep.specialPending = 1;
            start = &fromNode->pos;
            end = &route->dest.pos;
        } else {
            route->creep.link = AINetwork_FindLink(route->path, route->nodes[from], route->nodes[to]);
            if (route->creep.link >= 0)
                route->path->links[route->creep.link].flags |= AILINK_CREEPING;
            start = &fromNode->pos;
            end = &RouteNode(route, to)->pos;
        }
    }
    route->creep.from = from;
    route->creep.to = to;
    route->creep.unknown03 = 0;
    LinkCreep_Setup(route, start, end, fromEnd);
    return route->creep.active;
}

// Starts creeping along the route's current link, releasing the last one (link 0 is never released). Past the end of
// the route it clears the creep instead and returns false.
// AUTOINJECT
bool LinkCreep_Calc(AIRoute_tag *route, char fromEnd) {
    if (route->current > route->count) {
        memset(&route->creep, 0, sizeof(route->creep));
        return false;
    }
    if (route->creep.link > 0)
        route->path->links[route->creep.link].flags &= ~AILINK_CREEPING;
    short from = AINetwork_RouteNodeOffset(route->current, 0, route);
    short to = AINetwork_RouteNodeOffset(route->current, 1, route);
    return LinkCreep_ForNodes(route, from, to, fromEnd);
}

// A direct route: creeps from the start straight to the destination
// AUTOINJECT
uint LinkCreep_CalcToRouteEnd(AIRoute_tag *route) {
    ClearRoute(route);
    route->status = AIROUTE_DIRECT;
    route->targetFrame = route->target->builtFrame;
    bool started = LinkCreep_Setup(route, &route->start.pos, &route->dest.pos, 0);
    route->flags |= AIROUTE_VALID;
    if (started) {
        route->creep.specialPending = 0;
        route->creep.unknown02 = 1;
        route->creep.from = 0xffff;
        route->creep.to = 0xffff;
        route->creep.unknown03 = 1;
    }
    return AIROUTE_DIRECT;
}

// Whether the mover has reached the special node it is creeping from: within 0.4 m of it, or within 1 m with someone
// else already there
// AUTOINJECT
bool LinkCreep_AtSpecialNode(AIRoute_tag *route) {
    short index = AINetwork_RouteNodeOffset(route->current, 0, route);
    _VECTOR *pos = &RouteNode(route, index)->pos;
    double distance = Vec_Dist2D(pos, &route->mover->position);
    if (distance < 0.4f)
        return true;
    return distance < 1.0f && NDrone2_DroneNearPos(pos, 0.4f, route->mover);
}

// Moves a route on to its next (or, with direction < 0, its previous) node, with the route's mode at the ends, and
// starts creeping along the link from there
bool _AINetwork_SetupNextNode(AIRoute_tag *route, short direction) {
    if (direction >= 0 || route->current >= -1)
        route->current += route->flags & AIROUTE_BACKWARDS ? -direction : direction;

    ushort count = route->count;
    short current = route->current;
    if (current >= count) {
        switch (route->flags & AIROUTE_MODE) {
        case AIROUTE_LOOP:
            route->current = 0;
            break;
        case AIROUTE_PINGPONG:
            if (route->flags & AIROUTE_BACKWARDS) {
                route->current = current + 2;
                route->flags &= ~AIROUTE_BACKWARDS;
            } else {
                route->current = current - 2;
                route->flags |= AIROUTE_BACKWARDS;
            }
            break;
        default:
            // The end: steer at the destination
            if (current == count) {
                SetWaypoint(route, &route->dest);
                return true;
            }
            route->current = count;
            SetWaypoint(route, &route->dest);
            route->unknown06 = route->current;
            return true;
        }
    }

    if (!LinkCreep_Calc(route, direction > 0 ? 0 : 1))
        return true;
    CelPos_tag next;
    LinkCreep_Dest(route, 0, &next);
    route->waypoint.pos = next.pos;
    route->waypoint.cel = next.cel != NULL ? next.cel : NDrone2_FindCel(&next.pos);
    return true;
}

// The original takes the route in EAX and the direction on the stack (its callers push one more word, not read).
// AUTOLTCG
void __declspec(naked) AINetwork_SetupNextNode(void) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        call _AINetwork_SetupNextNode
        add esp, 8
        ret
    }
}

// Whether a creep step keeps clear of the bound corners: always without bounds or for a step under 1 m. The original
// takes the step's ends in EBX and EDI and the blocked flag in ESI, which it clears.
static bool CreepStepClear(CelPos_tag *from, CelPos_tag *to, uchar *blocked, float radius) {
    if (blocked != NULL)
        *blocked = 0;
    if (AIBoundCount == 0 || Vec_SqDist2D(&from->pos, &to->pos) < 1.0f)
        return true;
    return AINetwork_BoundsNodeTest(&from->pos, from->cel, &to->pos, to->cel, radius * 0.4f);
}

// The creep's next step: at the end of the link, the next node. The waypoint moves on to the step while it is within
// creepReach of the mover's feet, if it is within 0.4 m or clear of the bound corners; a blocked step within 0.3 m
// returns false.
bool _LinkCreep_Increment(AIRoute_tag *route, uchar *blocked) {
    obj_tag *mover = route->mover;
    if (!route->creep.active)
        return true;
    if (route->creep.step >= route->creep.steps)
        return _AINetwork_SetupNextNode(route, 1);

    CelPos_tag feet, next;
    NDrone2_FeetPos(mover, &feet.pos);
    feet.cel = mover->inCel;
    LinkCreep_Dest(route, 1, &next);
    float distance = (float)Vec_Dist2D(&feet.pos, &next.pos);
    bool clear = CreepStepClear(&feet, &next, blocked, 1.0f);
    if (blocked != NULL && *blocked)
        return false;
    if (!(route->creepReach > distance))
        return true;
    if (distance < CREEP_STEP || clear) {
        SetWaypoint(route, &next);
        route->creep.step++;
        return true;
    }
    return !(distance < 0.3f);
}

// The original takes the blocked flag in EAX and the route on the stack.
// AUTOLTCG
void __declspec(naked) LinkCreep_Increment(void) {
    _asm {
        push eax
        push dword ptr [esp + 8]
        call _LinkCreep_Increment
        add esp, 8
        ret
    }
}

// The creep's step back: at the start of the link, the previous node. The waypoint moves back if the step is clear of
// the bound corners (or under 1 m). False only before the route's first node.
bool _LinkCreep_Decrement(AIRoute_tag *route) {
    obj_tag *mover = route->mover;
    if (!route->creep.active || route->creep.step <= 0) {
        if (route->current <= -1)
            return false;
        _AINetwork_SetupNextNode(route, -1);
        return true;
    }

    CelPos_tag feet, back;
    NDrone2_FeetPos(mover, &feet.pos);
    feet.cel = mover->inCel;
    LinkCreep_Dest(route, -1, &back);
    if (AIBoundCount != 0 && !(Vec_SqDist2D(&feet.pos, &back.pos) < 1.0f)
        && !AINetwork_BoundsNodeTest(&feet.pos, feet.cel, &back.pos, back.cel, 0.1f))
        return true;
    SetWaypoint(route, &back);
    route->creep.step--;
    return true;
}

// The original takes the route in EAX (its caller pushes one more word, not read).
// AUTOLTCG
void __declspec(naked) LinkCreep_Decrement(void) {
    _asm {
        push eax
        call _LinkCreep_Decrement
        add esp, 4
        ret
    }
}

// One frame of creeping: tells the drone when it reaches a special node, steps back when the mover's way to the
// current step passes a bound corner, and otherwise steps on. A blocked step marks the drone (unless its behaviour
// says not to). The drone is used without a check.
bool _LinkCreep_Handler(AIRoute_tag *route, DCVars_tag *dcv) {
    obj_tag *mover = route->mover;
    if (!route->creep.active)
        return true;

    CelPos_tag feet, here;
    NDrone2_FeetPos(mover, &feet.pos);
    LinkCreep_Dest(route, 0, &here);
    feet.cel = mover->inCel;
    if (route->creep.specialPending) {
        if (!LinkCreep_AtSpecialNode(route))
            return true;
        NDrone2_ReachedDestNode(route, dcv);
        route->creep.specialPending = 0;
    }

    if ((dcv == NULL || dcv->drone->door == NULL) && AIBoundCount != 0) {
        bool clear = AINetwork_BoundsNodeTest(&feet.pos, feet.cel, &here.pos, here.cel, 0.2f);
        if (!(Vec_SqDist2D(&feet.pos, &here.pos) < 1.0f) && !clear)
            return _LinkCreep_Decrement(route);
    }

    if (behaviour_util_getProperty(BEHAVIOUR_IGNORE_BLOCKED_CREEP, dcv->drone->currentBehaviour) == 0) {
        uchar blocked = 0;
        bool result = _LinkCreep_Increment(route, &blocked);
        if (blocked)
            dcv->drone->bunchFlags |= 0x1e;
        return result;
    }
    return _LinkCreep_Increment(route, NULL);
}

// The original takes the route in ESI and the DCVars in EAX.
// AUTOLTCG
void __declspec(naked) LinkCreep_Handler(void) {
    _asm {
        push eax
        push esi
        call _LinkCreep_Handler
        add esp, 8
        ret
    }
}

// One frame of following a route: the distance left, the links counted as used, arrival (the end of a route that does
// not loop, within its arrive radius) and otherwise a creep step. Returns the status.
// AUTOINJECT
uint AINetwork_FollowRoute(AIRoute_tag *route, DCVars_tag *dcv) {
    if (route->path == NULL)
        return AIROUTE_NO_PATH;
    route->remaining = (float)AINetwork_GetRouteDistance(route);
    AINetwork_UpdateLinksUsedCount(route);
    if (route->current >= route->count && (route->flags & AIROUTE_MODE) != AIROUTE_LOOP
        && route->remaining < route->arriveRadius) {
        route->status = AIROUTE_ARRIVED;
        return AIROUTE_ARRIVED;
    }
    if (!_LinkCreep_Handler(route, dcv)) {
        route->status = route->unknown02 ? AIROUTE_CREEP_FAILED2 : AIROUTE_CREEP_FAILED;
        return route->status;
    }
    route->status = AIROUTE_FOLLOWING;
    return AIROUTE_FOLLOWING;
}
