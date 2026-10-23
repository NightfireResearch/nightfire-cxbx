// Shadow tests for the AI navigation network (AINetwork.cpp, AIPath.cpp, NDrone2_Nav.cpp and the NDrone2 helpers):
// each original and ours run on the same made-up level - a map block 0x05 with a nav graph, a patrol path and a
// bound, parsed and bound to three made-up cels - with the calls that need a real level (finding cels, the cels a ray
// passes, the ceiling ray, the heap, door locks, the drone callbacks) going to logging fakes. Run at start with
// MenuShadowTests=on (MenuProbe.cpp), before any level exists.
//
// Everything the functions write lives in the regions compared: the block (patched in place), the runtime arrays
// (from a fake heap), the path and bound tables, the cels' lists, the routes, targets and emitters. The searches'
// scratch marks, g/h/f, parents and list links are in the runtime nodes, so a search that visits nodes in another
// order, or leaves its open and closed lists differently, shows up even when its answer is the same.

#include "NavShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../engine/AINetwork.h"
#include "../engine/AIPath.h"
#include "../engine/Collide.h"
#include "../engine/viewer.h"
#include "../game/drone/NDrone2.h"
#include "../game/mp/multiplayer.h"
#include "../math/math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

const int CELS = 3;
const int OBJS = 5;
const int DRONES = 3;
const int GRID_COLUMNS = 5;
const int GRID_ROWS = 4;
const int NAV_NODES = GRID_COLUMNS * GRID_ROWS;
const int PATROL_NODES = 6;
const int BOUND_NODES = 8;

struct World {
    uint8_t block[0x3000];          // map block 0x05, patched in place
    uint8_t pool[0x6000];           // the fake Mem_Malloc's
    uint32_t poolUsed;
    cel_tag cels[CELS];
    portal_tag portals[4];
    obj_tag objs[OBJS];             // 0 and 1 movers, 2 the player, 3 a door, 4 a drone's
    uint8_t anims[OBJS][0x100];
    Drone_tag drones[DRONES];
    DCVars_tag dcv;
    AITarget_tag targets[2];
    AIPoint_tag points[2];
    AIRoute_tag routes[2];
    ushort routeNodes[2][64];
    AIEmitter_tag emitters[2];
    uint8_t emitterData[2][NAV_NODES + 4];
    AINodeSearch_tag search;
    AINode_tag openHead;            // a search's open list head
    CelPos_tag celPos[4];
    _VECTOR vecs[6];
    float floats[4];
    ushort nearest;
    uchar blocked;
    MAPNODE_tag *found;
    double result;
    HITDATA_tag hit;
    cel_tag *straddle[8];           // the fake Collide_StraddleCels' answer
    uint behaviour[3];

    // What the fakes answer
    int rayHits;
    float rayDistance;
};
World world;

ShadowRegions regions;
int cases, differ;

void Compare(const char *what, int trial, uint32_t (*theirs)(void), uint32_t (*ours)(void)) {
    cases++;
    if (!ShadowCompare(regions, "nav", what, trial, theirs, ours))
        differ++;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

int WhichCel(const cel_tag *cel) {
    for (int i = 0; i < CELS; i++)
        if (cel == &world.cels[i])
            return i;
    return cel == NULL ? -1 : 99;
}

int WhichObj(const obj_tag *obj) {
    for (int i = 0; i < OBJS; i++)
        if (obj == &world.objs[i])
            return i;
    return obj == NULL ? -1 : 99;
}

// ---- callers for the originals that take their arguments in registers (and for our adaptors at the same address)

__declspec(naked) uint32_t __cdecl CallESI(uint32_t at, void *esi) {
    _asm {
        push esi
        push edi
        push ebx
        push ebp
        mov esi, [esp + 24]
        mov eax, [esp + 20]
        call eax
        pop ebp
        pop ebx
        pop edi
        pop esi
        ret
    }
}

__declspec(naked) uint32_t __cdecl CallEDI(uint32_t at, void *edi) {
    _asm {
        push esi
        push edi
        push ebx
        push ebp
        mov edi, [esp + 24]
        mov eax, [esp + 20]
        call eax
        pop ebp
        pop ebx
        pop edi
        pop esi
        ret
    }
}

// AINetwork_OptimiseRoute: route in ESI, dropFirst in AL, status and dropLast on the stack
__declspec(naked) uint32_t __cdecl CallOptimise(uint32_t at, AIRoute_tag *route, uint32_t dropFirst, uint32_t status,
                                                uint32_t dropLast) {
    _asm {
        push esi
        push edi
        push ebx
        push ebp
        mov esi, [esp + 24]
        push dword ptr [esp + 36]
        push dword ptr [esp + 36]
        mov eax, [esp + 36]
        mov edx, [esp + 28]
        call edx
        add esp, 8
        pop ebp
        pop ebx
        pop edi
        pop esi
        ret
    }
}

// AINetwork_SetupRouteToPosition: route in ESI, destination in EDI, the rest on the stack
__declspec(naked) uint32_t __cdecl CallSetupToPosition(uint32_t at, AIRoute_tag *route, CelPos_tag *dest, obj_tag *mover,
                                                       AITarget_tag *target, AIPoint_tag *point, float radius) {
    _asm {
        push esi
        push edi
        push ebx
        push ebp
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        push dword ptr [esp + 44]
        mov esi, [esp + 40]
        mov edi, [esp + 44]
        mov eax, [esp + 36]
        call eax
        add esp, 16
        pop ebp
        pop ebx
        pop edi
        pop esi
        ret
    }
}

// AINetwork_CalcRoute: route in ECX, mover in EAX, target object in EBX, and an argument not read
__declspec(naked) uint32_t __cdecl CallCalcRoute(uint32_t at, AIRoute_tag *route, obj_tag *mover, obj_tag *targetObj) {
    _asm {
        push esi
        push edi
        push ebx
        push ebp
        push 0x1234
        mov ecx, [esp + 28]
        mov eax, [esp + 32]
        mov ebx, [esp + 36]
        mov edx, [esp + 24]
        call edx
        add esp, 4
        pop ebp
        pop ebx
        pop edi
        pop esi
        ret
    }
}

// AINetwork_SetupNextNode: route in EAX, the direction on the stack, and a word not read
__declspec(naked) uint32_t __cdecl CallNextNode(uint32_t at, AIRoute_tag *route, int direction) {
    _asm {
        push esi
        push edi
        push ebx
        push ebp
        push 0x5678
        push dword ptr [esp + 32]
        mov eax, [esp + 32]
        mov edx, [esp + 28]
        call edx
        add esp, 8
        pop ebp
        pop ebx
        pop edi
        pop esi
        ret
    }
}

// LinkCreep_Increment: blocked flag in EAX, route on the stack
__declspec(naked) uint32_t __cdecl CallIncrement(uint32_t at, AIRoute_tag *route, uchar *blocked) {
    _asm {
        push esi
        push edi
        push ebx
        push ebp
        push dword ptr [esp + 24]
        mov eax, [esp + 32]
        mov edx, [esp + 24]
        call edx
        add esp, 4
        pop ebp
        pop ebx
        pop edi
        pop esi
        ret
    }
}

// LinkCreep_Decrement: route in EAX, and a word not read
__declspec(naked) uint32_t __cdecl CallDecrement(uint32_t at, AIRoute_tag *route) {
    _asm {
        push esi
        push edi
        push ebx
        push ebp
        push 0x9abc
        mov eax, [esp + 28]
        mov edx, [esp + 24]
        call edx
        add esp, 4
        pop ebp
        pop ebx
        pop edi
        pop esi
        ret
    }
}

// LinkCreep_Handler: route in ESI, DCVars in EAX
__declspec(naked) uint32_t __cdecl CallHandler(uint32_t at, AIRoute_tag *route, DCVars_tag *dcv) {
    _asm {
        push esi
        push edi
        push ebx
        push ebp
        mov esi, [esp + 24]
        mov eax, [esp + 28]
        mov edx, [esp + 20]
        call edx
        pop ebp
        pop ebx
        pop edi
        pop esi
        ret
    }
}

// AIBounds_Link2Cel: node in EAX, record on the stack
__declspec(naked) uint32_t __cdecl CallLink2Cel(uint32_t at, AIBoundNode_tag *node, uchar *record) {
    _asm {
        push esi
        push edi
        push ebx
        push ebp
        push dword ptr [esp + 28]
        mov eax, [esp + 28]
        mov edx, [esp + 24]
        call edx
        add esp, 4
        pop ebp
        pop ebx
        pop edi
        pop esi
        ret
    }
}

// ---- the fakes

int CelFor(const _VECTOR *pos) {
    if (pos->z > 40.0f || pos->z < -40.0f)
        return -1;
    int cel = (int)floorf((pos->x + 30.0f) / 20.0f);
    return cel < 0 ? 0 : cel >= CELS ? CELS - 1 : cel;
}

cel_tag *__cdecl FakeFindCel(_VECTOR *pos, world_tag *where) {
    int cel = CelFor(pos);
    ShadowLog("findcel(%08x,%08x,%08x)=%d", Bits(pos->x), Bits(pos->y), Bits(pos->z), cel);
    return cel < 0 ? NULL : &world.cels[cel];
}

// The cels between the ray's ends in x, its own first
cel_tag **__cdecl FakeStraddleCels(HITTEST_tag *test, ushort *count) {
    ShadowLog("straddle(%d,%08x,%08x,%08x,%08x,%08x,%d,%d)", WhichCel(test->cel), Bits(test->rayStart.x),
              Bits(test->rayStart.z), Bits(test->rayEnd.x), Bits(test->rayEnd.z), Bits(test->radius),
              test->type & 0xff, test->type >> 8);
    int lo = CelFor(&test->rayStart), hi = CelFor(&test->rayEnd);
    if (lo < 0)
        lo = 0;
    if (hi < 0)
        hi = CELS - 1;
    if (lo > hi) {
        int t = lo;
        lo = hi;
        hi = t;
    }
    int n = 0;
    world.straddle[n++] = test->cel;
    for (int i = lo; i <= hi; i++)
        if (&world.cels[i] != test->cel)
            world.straddle[n++] = &world.cels[i];
    *count = (ushort)n;
    return world.straddle;
}

bool __cdecl FakeRayIntersect(_VECTOR *start, _VECTOR *end, cel_tag *cel, obj_tag *obj, obj_tag *obj2,
                              HITDATA_tag **hits, char p7, uint flags, ushort p9) {
    ShadowLog("ray(%08x,%08x,%08x,%d,%p,%p,%p,%d,%x,%d)", Bits(start->x), Bits(end->y), Bits(start->z), WhichCel(cel),
              obj, obj2, hits != NULL ? *hits : NULL, p7, flags, p9);
    if (world.rayHits && start->x > -5.0f) {
        world.hit.dist = start->z > 0.0f ? world.rayDistance : 1.25f;
        *hits = &world.hit;
        return true;
    }
    return false;
}

void __cdecl FakeFreeHitList(HITDATA_tag **hits) {
    ShadowLog("freehits(%d)", *hits == &world.hit ? 1 : *hits == NULL ? 0 : 2);
    *hits = NULL;
}

void *__cdecl FakeMalloc(size_t size, MallocFlags flags, uint32_t p3) {
    uint32_t align = flags & 0xff;
    if (align == 0)
        align = 4;
    uint32_t at = (world.poolUsed + align - 1) & ~(align - 1);
    ShadowLog("malloc(%u,%x,%u)", (unsigned)size, (unsigned)(ushort)flags, p3);
    if (at + size > sizeof(world.pool))
        return NULL;
    world.poolUsed = at + (uint32_t)size;
    return world.pool + at;
}

void __cdecl FakeFree(void **p) {
    ShadowLog("free(%p)", p != NULL ? *p : NULL);
    if (p != NULL)
        *p = NULL;
}

// The whole of EAX: the callers test AX
uint __cdecl FakeDoorLocked(obj_tag *door) {
    ShadowLog("locked(%d)", WhichObj(door));
    return door->tweakA & 1;
}

bool __cdecl FakeReachedDestNode(AIRoute_tag *route, DCVars_tag *dcv) {
    ShadowLog("reached(%d,%d,%d)", route == &world.routes[0] ? 0 : 1, route->current, dcv == &world.dcv);
    return true;
}

uint __cdecl FakeGetProperty(int id, uint *words) {
    ShadowLog("property(%d,%p)", id, words);
    return words != NULL ? (words[0] >> (id & 7)) & 1 : 0;
}

void InstallFakes() {
    ShadowHook_Install(0x00020c30, (const void *)&FakeFindCel);
    ShadowHook_Install(0x0002b910, (const void *)&FakeStraddleCels);
    ShadowHook_Install(0x0002c4e0, (const void *)&FakeRayIntersect);
    ShadowHook_Install(0x000294d0, (const void *)&FakeFreeHitList);
    ShadowHook_Install(0x00070ae0, (const void *)&FakeMalloc);
    ShadowHook_Install(0x00070f40, (const void *)&FakeFree);
    ShadowHook_Install(0x0002f570, (const void *)&FakeDoorLocked);
    ShadowHook_Install(0x000446b0, (const void *)&FakeReachedDestNode);
    ShadowHook_Install(0x00019440, (const void *)&FakeGetProperty);
}

// ---- the made-up level

float RandomFloat(float lo, float hi) {
    switch (ShadowRand(24)) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    default:
        return ShadowRandFloat(lo, hi);
    }
}

void RandomVector(_VECTOR *v, float range) {
    v->x = ShadowRandFloat(-range, range);
    v->y = ShadowRand(4) == 0 ? ShadowRandFloat(-4.0f, 6.0f) : ShadowRandFloat(-0.5f, 2.0f);
    v->z = ShadowRandFloat(-range * 0.7f, range * 0.7f);
}

uint RandomNodeFlags() {
    switch (ShadowRand(20)) {
    case 0:
    case 1:
        return 0x2468abce;
    case 2:
    case 3:
        return AINODE_DOOR;
    case 4:
    case 7:
    case 8:
        return AINODE_KICK;
    case 5:
        return 0x1;
    case 6:
        return 0x20;
    default:
        return 0;
    }
}

uint8_t *AddRecord(uint8_t *at, const char *name, uint flags, uint nodeCount, uint extra) {
    AIPathRecord *record = (AIPathRecord *)at;
    memset(record, 0, sizeof(*record));
    strncpy(record->name, name, sizeof(record->name) - 1);
    record->flags = flags;
    for (int i = 0; i < 8; i++)
        record->fromFile[i] = ShadowRand(3) == 0 ? ShadowRand(100) : 0;
    record->nodeCount = nodeCount;
    record->extraBytes = extra;
    at += sizeof(AIPathRecord);
    for (uint i = 0; i < extra; i++)
        *at++ = (uint8_t)i;
    return at;
}

void FileNode(MAPNODE_tag *node, float x, float y, float z, uint flags) {
    memset(node, 0, sizeof(*node));
    node->index = (ushort)ShadowRand(0x10000);   // rewritten by the parse
    node->path = 0xeeee;
    node->flags = flags;
    node->headroom = 9.0f;
    node->cel = (cel_tag *)0x1234;
    node->pos.x = x;
    node->pos.y = y;
    node->pos.z = z;
    node->unknown1c = 1.0f;
}

uint8_t *AddLinks(uint8_t *at, int count, const ushort (*ends)[2], bool bound) {
    *(uint *)at = count;
    at += 4;
    for (int i = 0; i < count; i++) {
        if (bound) {
            AIBoundLink_tag *link = (AIBoundLink_tag *)at;
            memset(link, 0xcd, sizeof(*link));
            link->index = i;
            link->a = ends[i][0];
            link->b = ends[i][1];
            at += sizeof(*link);
        } else {
            AILink_tag *link = (AILink_tag *)at;
            memset(link, 0xcd, sizeof(*link));
            link->index = i;
            link->a = ends[i][0];
            link->b = ends[i][1];
            at += sizeof(*link);
        }
    }
    return at;
}

// A 5 x 4 grid nav graph, a six-node patrol loop and a bound (an outline with two walls)
void BuildBlock() {
    memset(world.block, 0, sizeof(world.block));
    AIPathBlock *block = (AIPathBlock *)world.block;
    block->version = 8;
    block->count = ShadowRand(3) == 0 ? 5 : 3;
    uint8_t *at = world.block + sizeof(AIPathBlock);

    at = AddRecord(at, "Default Navigation", AIPATH_NAV, NAV_NODES, 4 * ShadowRand(3));
    AIPathNodeRecord *nav = (AIPathNodeRecord *)at;
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLUMNS; c++) {
            AIPathNodeRecord *node = &nav[r * GRID_COLUMNS + c];
            memset(node, 0, sizeof(*node));
            float y = ShadowRand(8) == 0 ? ShadowRandFloat(1.5f, 3.0f) : ShadowRandFloat(0.0f, 0.6f);
            FileNode(&node->node, -24.0f + 12.0f * c + ShadowRandFloat(-2.0f, 2.0f), y,
                     -12.0f + 8.0f * r + ShadowRandFloat(-1.5f, 1.5f), RandomNodeFlags());
        }
    }
    at = (uint8_t *)(nav + NAV_NODES);
    static ushort navLinks[64][2];
    int links = 0;
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLUMNS; c++) {
            int n = r * GRID_COLUMNS + c;
            if (c + 1 < GRID_COLUMNS && ShadowRand(8) != 0) {
                navLinks[links][0] = n;
                navLinks[links++][1] = n + 1;
            }
            if (r + 1 < GRID_ROWS && ShadowRand(8) != 0) {
                navLinks[links][0] = ShadowRand(2) ? n : n + GRID_COLUMNS;
                navLinks[links][1] = navLinks[links][0] == n ? n + GRID_COLUMNS : n;
                links++;
            }
            if (c + 1 < GRID_COLUMNS && r + 1 < GRID_ROWS && ShadowRand(6) == 0) {
                navLinks[links][0] = n;
                navLinks[links++][1] = n + GRID_COLUMNS + 1;
            }
        }
    }
    at = AddLinks(at, links, navLinks, false);

    at = AddRecord(at, "PatrolPath", AIPATH_PATROL | (ShadowRand(2) ? 0x10 : 0), PATROL_NODES, 0);
    AIPathNodeRecord *patrol = (AIPathNodeRecord *)at;
    for (int i = 0; i < PATROL_NODES; i++) {
        memset(&patrol[i], 0, sizeof(patrol[i]));
        float angle = TWO_PI * i / PATROL_NODES;
        FileNode(&patrol[i].node, 14.0f * cosf(angle), 0.2f, 9.0f * sinf(angle), ShadowRand(4) == 0 ? 0x1 : 0);
    }
    at = (uint8_t *)(patrol + PATROL_NODES);
    static ushort patrolLinks[PATROL_NODES][2];
    for (int i = 0; i < PATROL_NODES; i++) {
        patrolLinks[i][0] = i;
        patrolLinks[i][1] = (i + 1) % PATROL_NODES;
    }
    at = AddLinks(at, PATROL_NODES, patrolLinks, false);

    at = AddRecord(at, "Boundary", AIPATH_BOUND, BOUND_NODES, 8);
    MAPNODE_tag *bound = (MAPNODE_tag *)at;
    static const float corners[BOUND_NODES][2] = {
        {-28.5f, -18.0f}, {28.5f, -18.0f}, {28.5f, 18.0f}, {-28.5f, 18.0f},
        {-3.0f, -20.0f}, {-3.0f, 5.0f}, {6.0f, -6.0f}, {15.0f, 10.5f},
    };
    for (int i = 0; i < BOUND_NODES; i++)
        FileNode(&bound[i], corners[i][0], ShadowRandFloat(0.0f, 0.5f), corners[i][1], RandomNodeFlags());
    at = (uint8_t *)(bound + BOUND_NODES);
    static const ushort boundLinks[6][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {6, 7}};
    at = AddLinks(at, 6, boundLinks, true);

    if (block->count == 5) {
        at = AddRecord(at, "Empty", AIPATH_NAV, 0, 0);
        at = AddLinks(at, 0, NULL, false);
        at = AddRecord(at, "EmptyBound", AIPATH_BOUND, 0, 4);
        at = AddLinks(at, 0, NULL, true);
    }
}

void BuildCels() {
    memset(world.cels, 0, sizeof(world.cels));
    memset(world.portals, 0, sizeof(world.portals));
    for (int i = 0; i < 4; i++) {
        portal_tag *p = &world.portals[i];
        int from = i < 2 ? i : i - 1;       // 0: 0->1, 1: 1->2, 2: 1->0, 3: 2->1
        p->cel = ShadowRand(5) == 0 ? NULL : &world.cels[i < 2 ? i + 1 : i - 2];
        for (int k = 0; k < 4; k++)
            p->corners[k].y = ShadowRandFloat(-1.0f, 4.0f);
        p->next = world.cels[from].portals;
        world.cels[from].portals = p;
    }
}

void RandomObj(int i) {
    obj_tag *o = &world.objs[i];
    memset(o, 0, sizeof(*o));
    memset(world.anims[i], 0, sizeof(world.anims[i]));
    RandomVector(&o->position, 26.0f);
    int cel = CelFor(&o->position);
    o->inCel = cel < 0 ? NULL : &world.cels[cel];
    AnimState *anim = (AnimState *)world.anims[i];
    anim->animObj.rootHeight = ShadowRandFloat(0.0f, 1.2f);
    o->animState = ShadowRand(6) ? anim : NULL;
    o->objectType = ShadowRand(5) ? OBJECTTYPE_DRONE : OBJECTTYPE_DEAD_DRONE;
    o->flags = (char)(ShadowRand(6) == 0);
    o->tweakA = (uchar)ShadowRand(256);
    o->extraObjectData = &world.drones[i % DRONES];
}

void RandomDrones() {
    for (int i = 0; i < DRONES; i++) {
        Drone_tag *d = &world.drones[i];
        memset(d, 0, sizeof(*d));
        d->next = i + 1 < DRONES ? &world.drones[i + 1] : NULL;
        d->gameObj = ShadowRand(6) ? &world.objs[(i * 2) % OBJS] : NULL;
        static const uint flagSets[] = {0x100, 0x100, 0x100, 0x300, 0x500, 0, 0x100100};
        d->flags = flagSets[ShadowRand(7)];
        static const float healths[] = {10.0f, 10.0f, 0.0f, -0.0f, -5.0f};
        d->health = healths[ShadowRand(5)];
        d->door = ShadowRand(4) == 0 ? &world.objs[0] : NULL;
        d->currentBehaviour = world.behaviour;
    }
    world.behaviour[0] = ShadowRand(0x10000);
    NPCGlobals.NDrone2List = &world.drones[0];
    world.dcv.gameObj = &world.objs[0];
    world.dcv.drone = &world.drones[0];
    world.dcv.cel = world.objs[0].inCel;
}

void RandomCelPos(CelPos_tag *pos, float range) {
    RandomVector(&pos->pos, range);
    int cel = CelFor(&pos->pos);
    pos->cel = cel < 0 || ShadowRand(10) == 0 ? NULL : &world.cels[cel];
}

AIPath_tag *Nav() {
    return &AIPaths[0];
}

// A walk along linked nodes of a path (stopping at a node without links)
int RandomWalk(AIPath_tag *path, ushort *nodes, int most) {
    int count = 1 + ShadowRand(most);
    ushort node = (ushort)ShadowRand(path->nodeCount);
    nodes[0] = node;
    for (int i = 1; i < count; i++) {
        AINode_tag *n = &path->runtimeNodes[node];
        if (n->linkCount == 0)
            return i;
        AILink_tag *link = &path->links[path->nodeLinks[n->firstLink + ShadowRand(n->linkCount)]];
        node = link->a == node ? link->b : link->a;
        nodes[i] = node;
    }
    return count;
}

void RandomTarget(AITarget_tag *target) {
    target->targetBit = ShadowRand(3) ? AITARGET_GOAL : AITARGET_PLAYER;
    target->obj = ShadowRand(3) == 0 ? &world.objs[2] : NULL;
    RandomCelPos(&target->goal, 26.0f);
    target->built = target->goal;
    if (ShadowRand(3) == 0)
        target->built.pos.x += ShadowRandFloat(-3.0f, 3.0f);
    target->reachable = (short)ShadowRand(3);
    target->obstructedOnly = (short)ShadowRand(3);
    target->builtFrame = ShadowRand(5) == 0 ? 0 : 1000 + ShadowRand(20);
}

void RandomCreep(AIRoute_tag *route, AIPath_tag *path) {
    AICreep_tag *creep = &route->creep;
    creep->active = (uchar)(ShadowRand(5) != 0);
    creep->specialPending = (uchar)(ShadowRand(3) == 0);
    creep->unknown02 = (uchar)ShadowRand(2);
    creep->unknown03 = (uchar)ShadowRand(2);
    creep->link = (short)((int)ShadowRand(path->linkCount + 2) - 1);
    creep->path = path;
    creep->steps = (short)(1 + ShadowRand(12));
    creep->step = (short)((int)ShadowRand(creep->steps + 3) - 1);
    RandomVector(&creep->segmentStart, 26.0f);
    creep->delta.x = ShadowRandFloat(-0.4f, 0.4f);
    creep->delta.y = ShadowRandFloat(-0.1f, 0.1f);
    creep->delta.z = ShadowRandFloat(-0.4f, 0.4f);
    creep->segmentEnd = creep->segmentStart;
}


void RandomRoute(int r) {
    AIRoute_tag *route = &world.routes[r];
    AIPath_tag *path = ShadowRand(5) == 0 ? &AIPaths[1] : Nav();
    memset(route, 0, sizeof(*route));
    route->nodes = world.routeNodes[r];
    memset(world.routeNodes[r], 0, sizeof(world.routeNodes[r]));
    route->count = (ushort)RandomWalk(path, route->nodes, 8);
    if (ShadowRand(10) == 0)
        route->count = 0;
    route->current = (short)((int)ShadowRand(route->count + 3) - 1);
    static const ushort modes[] = {1, 1, 2, 3, 3, 0, 5};
    route->flags = modes[ShadowRand(7)] | (ShadowRand(3) == 0 ? AIROUTE_BACKWARDS : 0)
                 | (ShadowRand(2) ? AIROUTE_VALID : 0) | (ShadowRand(3) == 0 ? AIROUTE_DEST_MOVED : 0)
                 | (ShadowRand(3) == 0 ? AIROUTE_NO_DIRECT : 0);
    route->unknown02 = (uchar)ShadowRand(2);
    route->status = (uchar)ShadowRand(12);
    route->unknown05 = (uchar)ShadowRand(3);
    route->targetFrame = 1000 + ShadowRand(20);
    route->startNode = (ushort)ShadowRand(NAV_NODES);
    route->endNode = (ushort)ShadowRand(NAV_NODES);
    RandomCelPos(&route->start, 26.0f);
    RandomCelPos(&route->dest, 26.0f);
    if (route->dest.cel == NULL && ShadowRand(2))
        route->dest.cel = &world.cels[1];
    RandomCelPos(&route->waypoint, 26.0f);
    route->target = ShadowRand(8) == 0 ? NULL : &world.targets[ShadowRand(2)];
    route->remaining = ShadowRandFloat(0.0f, 30.0f);
    route->arriveRadius = RandomFloat(0.0f, 4.0f);
    route->creepReach = ShadowRand(4) == 0 ? 0.2f : RandomFloat(0.0f, 8.0f);
    route->path = ShadowRand(12) == 0 ? NULL : path;
    route->mover = &world.objs[ShadowRand(2)];
    route->targetObj = ShadowRand(3) ? &world.objs[2 + ShadowRand(2)] : NULL;

    RandomCreep(route, path);
}

void RandomLinkUse() {
    for (uint p = 0; p < AIPathCount; p++) {
        for (uint i = 0; i < AIPaths[p].linkCount; i++) {
            AIPaths[p].links[i].used = (ushort)(ShadowRand(4) == 0 ? ShadowRand(ShadowRand(3) == 0 ? 12 : 3) : 0);
            AIPaths[p].links[i].flags = (ushort)(ShadowRand(5) == 0 ? AILINK_CREEPING | 0x100 : 0);
        }
    }
}

void RandomBoundFlags() {
    for (uint b = 0; b < AIBoundCount; b++) {
        for (uint i = 0; i < AIBounds[b].linkCount; i++) {
            static const uint flags[] = {0, 0, 0, AIBOUND_PASSABLE, AIBOUND_DOOR, AIBOUND_KICK, 0x800 | 0x400};
            AIBounds[b].links[i].flags = flags[ShadowRand(7)];
        }
        for (uint i = 0; i < AIBounds[b].nodeCount; i++)
            AIBounds[b].runtimeNodes[i].fileNode->headroom = ShadowRandFloat(0.5f, 4.0f);
    }
}

void RandomNodeMarks() {
    for (uint p = 0; p < AIPathCount; p++) {
        for (uint i = 0; i < AIPaths[p].nodeCount; i++) {
            AINode_tag *node = &AIPaths[p].runtimeNodes[i];
            uint &flags = node->fileNode->flags;
            flags &= 0xffff;
            if (ShadowRand(4) == 0)
                flags |= AITARGET_GOAL;
            if (ShadowRand(6) == 0)
                flags |= AITARGET_GOAL << 1;
            if (ShadowRand(5) == 0)
                flags |= AITARGET_PLAYER;
            if (ShadowRand(8) == 0)
                flags |= AINODE_OPEN;
            node->f = ShadowRandFloat(0.0f, 50.0f);   // stale, as the emitter walk finds them
            node->g = ShadowRandFloat(0.0f, 50.0f);
            node->door = (flags & AINODE_DOOR) && ShadowRand(3) ? &world.objs[3] : NULL;
        }
        AIPaths[p].reachMask = ShadowRand(2) ? AITARGET_GOAL | AITARGET_PLAYER : ShadowRand(2) ? AITARGET_GOAL << 1 : 0;
    }
}

void RandomEmitter(int e) {
    AIEmitter_tag *em = &world.emitters[e];
    memset(em, 0, sizeof(*em));
    em->obj = ShadowRand(2) ? &world.objs[ShadowRand(OBJS)] : NULL;
    RandomCelPos(&em->celPos, 26.0f);
    em->path = ShadowRand(10) == 0 ? NULL : ShadowRand(6) == 0 ? &AIPaths[1] : Nav();
    em->size = em->path != NULL ? em->path->nodeCount : 0;
    em->data = ShadowRand(10) == 0 ? NULL : world.emitterData[e];
    for (int i = 0; i < NAV_NODES + 4; i++)
        world.emitterData[e][i] = (uint8_t)(ShadowRand(4) == 0 ? 0xff : ShadowRand(60));
    em->startNode = (short)((int)ShadowRand(NAV_NODES + 2) - 2);
    em->allocated = (uchar)ShadowRand(2);
}

void MoveTestLimits() {
    MoveTest_MaxSlope = ShadowRand(4) == 0 ? 0.0f : 0.87266463f;
    MoveTest_StepHeight = ShadowRand(3) == 0 ? 0.3f : 1.5f;
    MoveTest_MaxRise = ShadowRand(3) == 0 ? 1.0f : 4.0f;
    MoveTest_MaxDistanceSq = ShadowRand(3) == 0 ? 100.0f : 900.0f;
}


// ---- the tests

// What the cases of one trial are called with (the case lambdas cannot capture)
struct Pick {
    int a, b, c;
    float f, g;
    uint bits;
    ushort node;
    int cel;
    int navNode;
} pick;

#define R0 (&world.routes[0])
#define R1 (&world.routes[1])

// Runs the original at `at` (put back for the call) and then ours (behind the jump at the same address), each from
// the same state and on the same call
#define CASE(what, at, expr) \
    Compare(what, trial, [] { XbeOriginalScope o(at); return (uint32_t)(expr); }, [] { return (uint32_t)(expr); })

// The originals' entries, by their prototypes
typedef void (__cdecl *VoidFn)(void);
typedef uchar (__cdecl *RouteByteFn)(AIRoute_tag *);
typedef void (__cdecl *RouteVoidFn)(AIRoute_tag *);
typedef double (__cdecl *RouteDoubleFn)(AIRoute_tag *);
typedef short (__cdecl *RouteNodeOffsetFn)(short, short, AIRoute_tag *);
typedef void (__cdecl *ResetRouteFn)(AIRoute_tag *, obj_tag *, obj_tag *);
typedef ushort (__cdecl *FindLinkFn)(AIPath_tag *, ushort, short);
typedef double (__cdecl *DistanceBetweenFn)(AIRoute_tag *, short, short);
typedef uchar (__cdecl *NodeSearchFn)(AINodeSearch_tag *);
typedef uint (__cdecl *AStarFn)(AIRoute_tag *, obj_tag *, obj_tag *, uint);
typedef AIPath_tag *(__cdecl *NavPathFn)(_VECTOR *, cel_tag *, char);
typedef void (__cdecl *NodesForPositionFn)(AITarget_tag *, CelPos_tag *, obj_tag *, float);
typedef void (__cdecl *BuildTargetFn)(AITarget_tag *, obj_tag *, float);
typedef void (__cdecl *GoalPositionFn)(AITarget_tag *, AIPoint_tag *, CelPos_tag *, float);
typedef uchar (__cdecl *GoalToObjFn)(AITarget_tag *, AIPoint_tag *, obj_tag *, float);
typedef uchar (__cdecl *RayCelsFn)(_VECTOR *, cel_tag *, _VECTOR *, cel_tag *);
typedef uchar (__cdecl *HitBoundryFn)(_VECTOR *, AIBoundLink_tag *, _VECTOR *, float, float *, uint);
typedef uchar (__cdecl *LineIntersectsFn)(AIBoundLink_tag *, _VECTOR *, _VECTOR *, _VECTOR *, float *, char);
typedef uchar (__cdecl *PushVectorFn)(_VECTOR *, cel_tag *, float, _VECTOR *, uint);
typedef uchar (__cdecl *BoundsTestFn)(_VECTOR *, cel_tag *, _VECTOR *, cel_tag *, _VECTOR *, char);
typedef uchar (__cdecl *BoundsNodeTestFn)(_VECTOR *, cel_tag *, _VECTOR *, cel_tag *, float);
typedef uchar (__cdecl *FurthestFn)(_VECTOR *, _VECTOR *, _VECTOR *);
typedef uchar (__cdecl *ModLinkFlagsFn)(_VECTOR *, _VECTOR *, uint, uint, _VECTOR *, char);
typedef uchar (__cdecl *BoolFn)(void);
typedef void (__cdecl *ClearLinkFlagsFn)(ushort);
typedef void (__cdecl *LinksInCircleFn)(_VECTOR *, cel_tag *, float, ushort, ushort);
// The whole of EAX: DroneMove_FindSafetyFromScaryPosition tests it
typedef uint (__cdecl *EmitPathFn)(AIEmitter_tag *, float);
typedef uchar (__cdecl *NodeAtDistanceFn)(short, float, AIEmitter_tag *, MAPNODE_tag **);
typedef uchar (__cdecl *InitEmitterFn)(obj_tag *, _VECTOR *, cel_tag *, AIEmitter_tag *, AIPath_tag *);
typedef uchar (__cdecl *SetupToObjectFn)(obj_tag *, obj_tag *, AITarget_tag *, AIPoint_tag *, float, AIRoute_tag *);
typedef uchar (__cdecl *VerifyPositionFn)(obj_tag *, CelPos_tag *, AITarget_tag *, AIPoint_tag *, float, AIRoute_tag *);
typedef uint (__cdecl *CalcToObjectFn)(obj_tag *, obj_tag *, AIRoute_tag *, AITarget_tag *, AIPoint_tag *, float, uint);
typedef uint (__cdecl *CalcToPositionFn)(obj_tag *, CelPos_tag *, AIRoute_tag *, AITarget_tag *, AIPoint_tag *, float,
                                         uint);
typedef uint (__cdecl *FollowRouteFn)(AIRoute_tag *, DCVars_tag *);
typedef uchar (__cdecl *CreepSetupFn)(AIRoute_tag *, _VECTOR *, _VECTOR *, char);
typedef uchar (__cdecl *ForNodesFn)(AIRoute_tag *, int, int, char);
typedef uchar (__cdecl *CreepCalcFn)(AIRoute_tag *, char);
typedef uint (__cdecl *RouteUintFn)(AIRoute_tag *);
typedef uchar (__cdecl *CreepDestFn)(AIRoute_tag *, short, CelPos_tag *);
typedef uchar (__cdecl *RouteBoolFn)(AIRoute_tag *);
typedef char (__cdecl *MoveTest9Fn)(_VECTOR *, cel_tag *, _VECTOR *, cel_tag *, obj_tag *, obj_tag *, _VECTOR *, char,
                                    char);
typedef char (__cdecl *MoveTest7Fn)(CelPos_tag *, CelPos_tag *, obj_tag *, obj_tag *, _VECTOR *, char, char);
typedef char (__cdecl *MoveFromNodeFn)(MAPNODE_tag *, CelPos_tag *, obj_tag *, obj_tag *, _VECTOR *, char, char);
typedef char (__cdecl *MoveToNodeFn)(CelPos_tag *, MAPNODE_tag *, obj_tag *, obj_tag *, _VECTOR *, char, char);
typedef uchar (__cdecl *FeetPosFn)(obj_tag *, _VECTOR *);
typedef cel_tag *(__cdecl *FindCelFn)(_VECTOR *);
typedef obj_tag *(__cdecl *PlayerFn)(void);
typedef short (__cdecl *NearestNodeFn)(CelPos_tag *, AIPath_tag *, obj_tag *);
typedef uchar (__cdecl *DistanceToEmitterFn)(CelPos_tag *, AIPath_tag *, ushort *, AIEmitter_tag *, obj_tag *, float *);
typedef uchar (__cdecl *DroneNearPosFn)(_VECTOR *, float, obj_tag *);

#define AT(type, address) ((type)(address))

ShadowRegions snapshot;

void LinkAllBoundNodes(uint32_t at) {
    AIBound_tag *bound = &AIBounds[AIBoundCount - 1];
    for (uint i = 0; i < bound->nodeCount; i++)
        CallLink2Cel(at, &bound->runtimeNodes[i], bound->record);
}

void TestLoad() {
    for (int trial = 0; trial < 80; trial++) {
        BuildBlock();
        BuildCels();
        world.poolUsed = 0;
        world.rayHits = ShadowRand(3) != 0;
        world.rayDistance = ShadowRand(2) ? 0.5f : 3.75f;
        AIPathCount = 5;
        AIBoundCount = 3;
        CASE("AIPath_Init", 0x000a6c20, (AT(VoidFn, 0x000a6c20)(), 0));

        // Sometimes the block twice (the second adds to the tables); sometimes the wrong version
        if (trial % 5 == 1)
            AIPath_Parse((AIPathBlock *)world.block);
        if (trial % 7 == 3)
            ((AIPathBlock *)world.block)->version = 7;
        CASE("AIPath_Parse", 0x000a6c40, (AT(void (__cdecl *)(void *), 0x000a6c40)(world.block), 0));
        if (AIPathCount < 2 || AIBoundCount < 1)
            continue;

        // The steps of BindNodes one at a time, then BindNodes on the state from before them
        snapshot.Save();
        CASE("AIPath_Prepare", 0x000a7000, (CallESI(0x000a7000, &AIPaths[AIPathCount - 2]), 0));
        CASE("AIPath_Prepare", 0x000a7000, (CallESI(0x000a7000, &AIPaths[AIPathCount - 1]), 0));
        CASE("AIBounds_Link2Cel", 0x000a7160, (LinkAllBoundNodes(0x000a7160), 0));
        CASE("AIBounds_Prepare", 0x000a7230, (CallEDI(0x000a7230, &AIBounds[AIBoundCount - 1]), 0));
        snapshot.Restore();
        CASE("AIPath_BindNodes", 0x000a72a0, (AT(VoidFn, 0x000a72a0)(), 0));
    }
}

// Leaves a level loaded, parsed and bound by our functions (already compared with the originals)
void LoadLevel() {
    do {
        BuildBlock();
        BuildCels();
        world.poolUsed = 0;
        world.rayHits = 1;
        world.rayDistance = 3.75f;
        AIPath_Init();
        AIPath_Parse((AIPathBlock *)world.block);
        AIPath_BindNodes();
    } while (Nav()->linkCount < 14);
}

void RandomScene() {
    for (int i = 0; i < OBJS; i++)
        RandomObj(i);
    RandomDrones();
    RandomTarget(&world.targets[0]);
    RandomTarget(&world.targets[1]);
    RandomRoute(0);
    RandomRoute(1);
    RandomLinkUse();
    RandomNodeMarks();
    RandomBoundFlags();
    RandomEmitter(0);
    RandomEmitter(1);
    for (int i = 0; i < 4; i++)
        RandomCelPos(&world.celPos[i], 27.0f);
    for (int i = 0; i < 6; i++)
        RandomVector(&world.vecs[i], 30.0f);
    world.floats[0] = RandomFloat(0.0f, 40.0f);
    world.floats[1] = 100000.0f;
    world.nearest = ShadowRand(3) ? 0xffff : (ushort)ShadowRand(NAV_NODES);
    world.found = NULL;
    world.blocked = 0x55;
    memset(&world.points[0], 0, sizeof(world.points));
    pick.a = (int)ShadowRand(14) - 3;
    pick.b = (int)ShadowRand(14) - 3;
    pick.c = (int)ShadowRand(4);
    pick.f = RandomFloat(0.0f, 6.0f);
    pick.g = ShadowRand(3) == 0 ? RandomFloat(250.0f, 900.0f) : RandomFloat(0.0f, 300.0f);
    static const uint bits[] = {AITARGET_GOAL, AITARGET_PLAYER, AITARGET_GOAL << 1, 0x1, 0};
    pick.bits = bits[ShadowRand(5)];
    pick.node = ShadowRand(4) == 0 ? 0xffff : (ushort)ShadowRand(NAV_NODES);
    pick.cel = (int)ShadowRand(CELS);
    pick.navNode = (int)ShadowRand(NAV_NODES);
    MoveTestLimits();
    world.rayHits = ShadowRand(3) != 0;
    NPCGlobals.dummyPlayerActive = (uchar)(ShadowRand(6) == 0);
    NPCGlobals.dummyPlayer = &world.objs[4];
    glb_players[0] = ShadowRand(8) ? &world.objs[2] : NULL;
    MPSettings.dronesAreBots = ShadowRand(4) == 0;
    GameState.NumFramesUnpaused = 1010;
}

void TestRouteState() {
    for (int trial = 0; trial < 600; trial++) {
        RandomScene();
        CASE("AINetwork_RouteIsValid", 0x000479d0, AT(RouteByteFn, 0x000479d0)(pick.c ? R0 : NULL) & 0xff);
        CASE("AINetwork_InvalidateRoute", 0x000479f0, (AT(RouteVoidFn, 0x000479f0)(pick.c ? R0 : NULL), 0));
        CASE("AINetwork_ValidateRoute", 0x00047a00, (AT(RouteVoidFn, 0x00047a00)(pick.c ? R1 : NULL), 0));
        CASE("AINetwork_RouteNodeOffset", 0x00047a10,
             AT(RouteNodeOffsetFn, 0x00047a10)(R0->current, (short)pick.a, R0) & 0xffff);
        CASE("AINetwork_ResetRoute", 0x000482c0,
             (AT(ResetRouteFn, 0x000482c0)(R1, &world.objs[1], pick.c ? &world.objs[pick.c] : NULL), 0));
        CASE("AINetwork_FindLink", 0x00047b40,
             AT(FindLinkFn, 0x00047b40)(Nav(), pick.node, pick.a == -3 ? -1 : (short)(pick.b + 3)) & 0xffff);
        if (pick.c == 0)
            R0->current = -2;
        if (R0->path != NULL || R0->count == 0 || R0->current >= R0->count) {
            CASE("AINetwork_GetRouteDistance", 0x00047cd0, (world.result = AT(RouteDoubleFn, 0x00047cd0)(R0), 0));
            CASE("AINetwork_GetRouteDistance3D", 0x00047e60, (world.result = AT(RouteDoubleFn, 0x00047e60)(R0), 0));
        }
        CASE("AINetwork_GetRouteDistanceBetweenNodes", 0x00047bb0,
             (world.result = AT(DistanceBetweenFn, 0x00047bb0)(R0, (short)pick.a, (short)pick.b), 0));
        CASE("AINetwork_ClearLinkFlags", 0x0004a530,
             (AT(ClearLinkFlagsFn, 0x0004a530)((ushort)(pick.c ? 0x310 : 0xffff)), 0));
        if (R1->path != NULL && R1->count != 0)
            CASE("AINetwork_UpdateLinksUsedCount", 0x0004a5a0, (AT(RouteVoidFn, 0x0004a5a0)(R1), 0));
        CASE("AINetwork_SetLinksFlagInCircle", 0x0004a620,
             (AT(LinksInCircleFn, 0x0004a620)(&world.vecs[0], NULL, pick.f * 2.0f, (ushort)(0x100 << (pick.c & 1)),
                                              (ushort)(pick.c > 1 ? 0x300 : 0)),
              0));
    }
}

void SetupSearch(bool toNode) {
    AINodeSearch_tag *s = &world.search;
    memset(s, 0, sizeof(*s));
    s->origin = &world.celPos[0];
    s->heuristicGoal = ShadowRand(3) ? &world.vecs[1] : NULL;
    s->path = ShadowRand(10) == 0 ? NULL : ShadowRand(6) == 0 ? &AIPaths[1] : Nav();
    s->toNode = toNode;
    s->openList = toNode && ShadowRand(2) ? &world.openHead : NULL;
    s->objA = &world.objs[0];
    s->objB = ShadowRand(2) ? &world.objs[1] : NULL;
    s->targetBit = pick.bits;
    static const float radii[] = {0.0f, 0.0f, 625.0f, 9.0f, 36.0f, 2.0f};
    s->radiusSq = radii[ShadowRand(6)];
    s->resultNode = 0x3333;
    s->found = (int)ShadowRand(3);
    s->obstructed = (int)ShadowRand(3);
}

void TestSearches() {
    for (int trial = 0; trial < 700; trial++) {
        RandomScene();
        SetupSearch(ShadowRand(2) != 0);
        if (world.search.openList != NULL)
            world.search.openList->nextInList = NULL;
        CASE("AINetwork_NodeSearchCel", 0x00048390, CallESI(0x00048390, &world.search) & 0xff);
        SetupSearch(ShadowRand(2) != 0);
        if (world.search.openList != NULL)
            world.search.openList->nextInList = NULL;
        if (ShadowRand(3) == 0 && world.celPos[0].cel != NULL && world.celPos[0].cel->portals != NULL) {
            portal_tag *portal = world.celPos[0].cel->portals;
            float low = portal->corners[0].y;
            for (int k = 1; k < 4; k++)
                if (portal->corners[k].y < low)
                    low = portal->corners[k].y;
            world.celPos[0].pos.y = low - 2.0f + ShadowRandFloat(-0.15f, 0.15f);
            world.search.found = 0;
        }
        CASE("AINetwork_NodeSearch", 0x00048610, AT(NodeSearchFn, 0x00048610)(&world.search) & 0xff);
        CASE("NDrone2_NearestNode", 0x000445f0,
             AT(NearestNodeFn, 0x000445f0)(&world.celPos[1], pick.c ? Nav() : &AIPaths[1], &world.objs[0]) & 0xffff);
        CASE("AINetwork_NavPathForPosition", 0x00049650,
             (uint32_t)(uintptr_t)AT(NavPathFn, 0x00049650)(&world.celPos[2].pos, world.celPos[2].cel, pick.c & 1));
        CASE("AINetwork_NodesForPosition", 0x000497d0,
             (AT(NodesForPositionFn, 0x000497d0)(&world.targets[0], &world.targets[0].goal,
                                                 pick.c ? &world.objs[1] : NULL, pick.f),
              0));
        CASE("AINetwork_BuildAITarget", 0x00049930,
             (AT(BuildTargetFn, 0x00049930)(&world.targets[1], &world.objs[0], pick.f), 0));
        CASE("AINetwork_SetupGoalPosition", 0x00049980,
             (AT(GoalPositionFn, 0x00049980)(&world.targets[0], &world.points[0],
                                             pick.c == 3 ? (CelPos_tag *)&world.points[0].pos : &world.celPos[3],
                                             pick.c == 1 ? 0.0f : pick.f),
              0));
        CASE("AINetwork_SetupGoalPositionToObj", 0x00049a20,
             AT(GoalToObjFn, 0x00049a20)(&world.targets[1], &world.points[1], pick.c ? &world.objs[2] : NULL,
                                         pick.c == 2 ? 0.0f : pick.f) & 0xff);
        CASE("NDrone2_NavNodeCache", 0x00044de0, (AT(VoidFn, 0x00044de0)(), 0));
    }
}

// The A* and the emitters, from many starts and goals: the node marks, the costs, the lists and the route are
// compared, not only the answer
void TestGraph() {
    for (int trial = 0; trial < 900; trial++) {
        RandomScene();
        // Mark the goal for real most of the time, so that the search has somewhere to go
        if (ShadowRand(4)) {
            AINetwork_NodesForPosition(&world.targets[0], &world.targets[0].goal, NULL, 0.0f);
            R0->target = &world.targets[0];
        }
        R0->path = ShadowRand(8) == 0 ? &AIPaths[1] : Nav();
        R0->nodes = world.routeNodes[0];
        if (R0->target == NULL)
            R0->target = &world.targets[1];
        CASE("AINetwork_DoAStarPath", 0x00048720,
             AT(AStarFn, 0x00048720)(R0, pick.c & 1 ? &world.objs[2] : NULL, &world.objs[0],
                                     R0->target != NULL ? R0->target->targetBit : pick.bits));

        // Heavy congestion: costs past 255
        if (pick.c == 3)
            for (uint i = 0; i < Nav()->linkCount; i++)
                Nav()->links[i].used = 20;
        CASE("AINetwork_EmitPath", 0x00049d70,
             AT(EmitPathFn, 0x00049d70)(pick.c ? &world.emitters[0] : NULL, pick.c == 2 ? 0.0f : pick.g));
        if (world.emitters[1].path == Nav() && world.emitters[1].data != NULL)
            CASE("AINetwork_Emitter_GetNodeAtDistance", 0x0004a070,
                 AT(NodeAtDistanceFn, 0x0004a070)((short)(pick.a - 1), pick.g * 0.2f, &world.emitters[1],
                                                  pick.c ? &world.found : NULL) & 0xff);
        CASE("AINetwork_InitEmitter", 0x0004a870,
             AT(InitEmitterFn, 0x0004a870)(&world.objs[pick.c], &world.celPos[3].pos,
                                           pick.c == 1 ? NULL : world.celPos[3].cel,
                                           pick.a == 0 ? NULL : &world.emitters[0], pick.b > 6 ? &AIPaths[1] : NULL) &
                 0xff);
        CASE("AINetwork_InitEmitter2", 0x0004a900,
             AT(InitEmitterFn, 0x0004a900)(&world.objs[pick.c], &world.celPos[2].pos,
                                           pick.c == 2 ? NULL : world.celPos[2].cel,
                                           pick.a == 0 ? NULL : &world.emitters[1], pick.b > 6 ? &AIPaths[1] : NULL) &
                 0xff);
        CASE("NDrone2_DistanceToEmitter", 0x00046260,
             AT(DistanceToEmitterFn, 0x00046260)(&world.celPos[0], pick.c == 3 ? &AIPaths[1] : Nav(),
                                                 pick.c ? &world.nearest : NULL, &world.emitters[0], &world.objs[1],
                                                 pick.a > 0 ? &world.floats[2] : NULL) & 0xff);
    }
}

AIBoundLink_tag *RandomBoundLink() {
    AIBound_tag *bound = &AIBounds[0];
    return &bound->links[ShadowRand(bound->linkCount)];
}

AIBoundLink_tag *pickedLink;

void TestBounds() {
    for (int trial = 0; trial < 1200; trial++) {
        RandomScene();
        pickedLink = RandomBoundLink();
        // Some segments start or end near a bound corner, to reach the corner checks
        if (ShadowRand(3) == 0) {
            MAPNODE_tag *corner = AIBounds[0].runtimeNodes[ShadowRand(BOUND_NODES)].fileNode;
            world.vecs[ShadowRand(2)] = corner->pos;
            world.vecs[0].x += ShadowRandFloat(-0.5f, 0.5f);
        }
        if (ShadowRand(3) == 0) {
            MAPNODE_tag *corner = AIBounds[0].runtimeNodes[ShadowRand(BOUND_NODES)].fileNode;
            world.vecs[1].x = 2 * corner->pos.x - world.vecs[0].x + ShadowRandFloat(-0.3f, 0.3f);
            world.vecs[1].z = 2 * corner->pos.z - world.vecs[0].z + ShadowRandFloat(-0.3f, 0.3f);
        }
        // A rise of exactly the step height
        if (ShadowRand(8) == 0)
            world.vecs[1].y = world.vecs[0].y + MoveTest_StepHeight;
        int celA = CelFor(&world.vecs[0]), celB = CelFor(&world.vecs[1]);
        uint bounds = AIBoundCount;
        if (ShadowRand(10) == 0)
            AIBoundCount = 0;
        world.celPos[0].pos = world.vecs[0];
        world.celPos[0].cel = celA < 0 || ShadowRand(12) == 0 ? NULL : &world.cels[celA];
        world.celPos[1].pos = world.vecs[1];
        world.celPos[1].cel = celB < 0 || ShadowRand(12) == 0 ? NULL : &world.cels[celB];

        CASE("AINetwork_TestRayCels", 0x00048ac0,
             AT(RayCelsFn, 0x00048ac0)(&world.vecs[0], world.celPos[0].cel, &world.vecs[1],
                                       pick.c == 0 ? &world.cels[pick.cel] : world.celPos[1].cel) & 0xff);
        CASE("AINetwork_HitBoundry", 0x00048b70,
             AT(HitBoundryFn, 0x00048b70)(&world.vecs[2], pickedLink, &world.vecs[0], pick.f * pick.f + 0.5f,
                                          &world.floats[2], pick.c == 3 ? AIBOUND_DOOR : 0) & 0xff);
        CASE("AINetwork_LineIntersectsBoundry", 0x00048d10,
             AT(LineIntersectsFn, 0x00048d10)(pickedLink, &world.vecs[0], &world.vecs[1],
                                              pick.c == 1 ? NULL : &world.vecs[3], pick.c >= 2 ? &world.floats[0] : NULL,
                                              (char)(pick.a & 1)) & 0xff);
        if (world.celPos[0].cel != NULL)
            CASE("AINetwork_GetBoundsPushVectorForSphere", 0x00048ee0,
                 AT(PushVectorFn, 0x00048ee0)(&world.vecs[0], world.celPos[0].cel, pick.f, &world.vecs[4],
                                              pick.c == 2 ? AIBOUND_DOOR : pick.c == 3 ? AIBOUND_PASSABLE : 0) & 0xff);
        CASE("AINetwork_BoundsTest", 0x00049030,
             AT(BoundsTestFn, 0x00049030)(&world.vecs[0], world.celPos[0].cel, &world.vecs[1], world.celPos[1].cel,
                                          pick.c == 0 ? &world.vecs[3] : NULL, (char)(pick.a > 2)) & 0xff);
        CASE("AINetwork_BoundsNodeTest", 0x00049370,
             AT(BoundsNodeTestFn, 0x00049370)(&world.vecs[0], world.celPos[0].cel, &world.vecs[1],
                                              pick.c == 1 ? NULL : world.celPos[1].cel, pick.f * 0.3f) & 0xff);
        CASE("AINetwork_FurthestPosition", 0x0004a7b0,
             AT(FurthestFn, 0x0004a7b0)(&world.vecs[0], &world.vecs[1], &world.vecs[5]) & 0xff);
        CASE("AINetwork_ModIntersectedLinkFlags_Bounds", 0x0004a670,
             AT(ModLinkFlagsFn, 0x0004a670)(&world.vecs[0], &world.vecs[1], pick.c == 0 ? 0x400 : 0x2000,
                                            pick.c == 1 ? 0x1800 : 0, pick.c == 2 ? &world.vecs[3] : NULL,
                                            (char)(pick.a & 1)) & 0xff);
        if (trial % 20 == 0)
            CASE("AINetwork_InitPassableBoundries", 0x0004a720, AT(BoolFn, 0x0004a720)() & 0xff);

        CASE("NDrone2_MoveTest", 0x00044030,
             AT(MoveTest9Fn, 0x00044030)(&world.vecs[0], world.celPos[0].cel, &world.vecs[1], world.celPos[1].cel,
                                         &world.objs[0], NULL, pick.c == 0 ? &world.vecs[3] : NULL, (char)(pick.a > 2),
                                         (char)(pick.b & 1)) & 0xff);
        CASE("NDrone2_MoveTest (CelPos)", 0x00044150,
             AT(MoveTest7Fn, 0x00044150)(&world.celPos[0], &world.celPos[1], NULL, &world.objs[1],
                                         pick.c == 1 ? &world.vecs[3] : NULL, (char)(pick.a > 4), 1) & 0xff);
        CASE("NDrone2_MoveTestFromNode", 0x00044190,
             AT(MoveFromNodeFn, 0x00044190)(Nav()->runtimeNodes[pick.navNode].fileNode, &world.celPos[1], NULL,
                                            NULL, pick.c == 2 ? &world.vecs[3] : NULL, (char)(pick.a & 1), 1) & 0xff);
        CASE("NDrone2_MoveTestToNode", 0x000441d0,
             AT(MoveToNodeFn, 0x000441d0)(&world.celPos[0], Nav()->runtimeNodes[pick.navNode].fileNode, NULL, NULL,
                                          pick.c == 3 ? &world.vecs[3] : NULL, (char)(pick.a & 1), 0) & 0xff);
        AIBoundCount = bounds;
    }
}

// Routes: planning, and following them a creep step at a time
void TestPlanning() {
    for (int trial = 0; trial < 900; trial++) {
        RandomScene();
        if (ShadowRand(3)) {
            AINetwork_NodesForPosition(&world.targets[0], &world.targets[0].goal, NULL, 0.0f);
            world.targets[0].targetBit = AITARGET_GOAL;
        }
        R0->target = &world.targets[ShadowRand(2)];
        R0->path = Nav();
        if (ShadowRand(3) && R0->count > 0)
            AINetwork_DoAStarPath(R0, NULL, R0->mover, R0->target->targetBit);

        // Door and kick nodes at the ends, which may not be skipped
        if (R0->count > 1 && ShadowRand(2)) {
            static const uint flags[] = {0, AINODE_DOOR, AINODE_KICK, AINODE_DOOR | AINODE_KICK};
            int ends[] = {0, 1, R0->count - 2, R0->count - 1};
            for (int k = 0; k < 4; k++) {
                MAPNODE_tag *node = R0->path->runtimeNodes[R0->nodes[ends[k]]].fileNode;
                node->flags = (node->flags & ~(AINODE_DOOR | AINODE_KICK)) | flags[ShadowRand(4)];
            }
        }
        if (R0->count > 0 && R0->path != NULL) {
            CASE("AINetwork_AlterDestFor_DROUTE_Nearest", 0x00047a90, CallESI(0x00047a90, R0) & 0xff);
            CASE("AINetwork_OptimiseRoute", 0x000480b0,
                 CallOptimise(0x000480b0, R0, pick.c & 1, 0x1000 + pick.a, (pick.c >> 1) & 1));
        }
        CASE("AINetwork_SetupRouteToObject", 0x00049ab0,
             AT(SetupToObjectFn, 0x00049ab0)(&world.objs[0], pick.c == 0 ? NULL : &world.objs[pick.c],
                                             &world.targets[1], &world.points[0], pick.f, pick.a == 0 ? NULL : R1) &
                 0xff);
        CASE("AINetwork_SetupRouteToPosition", 0x00049b30,
             CallSetupToPosition(0x00049b30, pick.a == 0 ? NULL : R1, pick.b == 0 ? NULL : &world.celPos[pick.c],
                                 &world.objs[1], &world.targets[0], &world.points[1], pick.f) & 0xff);
        CASE("AINetwork_VerifyRouteTarget", 0x00049bf0,
             AT(SetupToObjectFn, 0x00049bf0)(&world.objs[1], &world.objs[2 + (pick.c & 1)], &world.targets[1],
                                             &world.points[0], pick.f, R0) & 0xff);
        CASE("AINetwork_VerifyRouteToPosition", 0x00049c50,
             AT(VerifyPositionFn, 0x00049c50)(&world.objs[0], pick.b == 0 ? NULL : &world.celPos[3], &world.targets[0],
                                              &world.points[1], pick.f, pick.a == 1 ? NULL : R0) & 0xff);
        RandomRoute(1);
        R1->target = &world.targets[pick.c & 1];
        if (R1->path == NULL)
            R1->path = Nav();
        CASE("AINetwork_CalcRoute", 0x0004acb0,
             CallCalcRoute(0x0004acb0, R1, &world.objs[1], pick.c == 3 ? NULL : &world.objs[2]));
        CASE("AINetwork_CalcRouteToObject", 0x0004adb0,
             AT(CalcToObjectFn, 0x0004adb0)(&world.objs[0], &world.objs[2 + (pick.c & 1)], R0, &world.targets[0],
                                            &world.points[0], pick.f, 0x77));
        CASE("AINetwork_CalcRouteToPosition", 0x0004ae30,
             AT(CalcToPositionFn, 0x0004ae30)(&world.objs[1], &world.celPos[pick.c], R1, &world.targets[1],
                                              &world.points[1], pick.f, 0x88));
    }
}

void TestFollowing() {
    for (int trial = 0; trial < 1500; trial++) {
        RandomScene();
        R0->target = &world.targets[0];
        if (R0->path == NULL)
            R0->path = Nav();
        if (ShadowRand(2) && R0->count > 0) {
            AINetwork_DoAStarPath(R0, NULL, R0->mover, R0->target->targetBit);
            R0->current = (short)((int)ShadowRand(R0->count + 3) - 1);
            RandomCreep(R0, R0->path);
        }
        // Movers near the next step, so that the creep moves on
        if (ShadowRand(2)) {
            CelPos_tag next;
            LinkCreep_Dest(R0, (short)(ShadowRand(3) - 1), &next);
            R0->mover->position = next.pos;
            R0->mover->position.x += ShadowRandFloat(-0.6f, 0.6f);
            R0->mover->position.z += ShadowRandFloat(-0.6f, 0.6f);
            R0->mover->inCel = next.cel;
        }

        if (ShadowRand(4) == 0) {
            CelPos_tag next;
            LinkCreep_Dest(R0, 1, &next);
            float angle = ShadowRandFloat(0.0f, 6.28f), reach = ShadowRandFloat(0.25f, 0.45f);
            R0->mover->position = next.pos;
            R0->mover->position.x += reach * cosf(angle);
            R0->mover->position.z += reach * sinf(angle);
            if (R0->mover->animState != NULL)
                R0->mover->position.y += ((AnimState *)R0->mover->animState)->animObj.rootHeight - 0.4f;
        }
        CASE("LinkCreep_Dest", 0x0004a2e0, AT(CreepDestFn, 0x0004a2e0)(R0, (short)(pick.a - 3), &world.celPos[0]) & 0xff);
        if (R0->count > 0 && R0->path != NULL && R0->current >= 0)
            CASE("LinkCreep_AtSpecialNode", 0x0004a330, AT(RouteBoolFn, 0x0004a330)(R0) & 0xff);
        CASE("LinkCreep_Setup", 0x0004a990,
             AT(CreepSetupFn, 0x0004a990)(R1, &world.vecs[0], pick.c == 0 ? &world.vecs[0] : &world.vecs[1],
                                          (char)(pick.a & 1)) & 0xff);
        if (R0->path != NULL && R0->count > 0) {
            CASE("LinkCreep_ForNodes", 0x0004aa90,
                 AT(ForNodesFn, 0x0004aa90)(R0, pick.a < 0 ? -1 : pick.a % R0->count,
                                            pick.b < 0 && pick.a < 0 ? -1 : (pick.c == 0 && pick.a >= 0 ? pick.a : pick.b + 3) % R0->count,
                                            (char)(pick.c & 1)) & 0xff);
            CASE("LinkCreep_Calc", 0x0004abb0, AT(CreepCalcFn, 0x0004abb0)(R0, (char)(pick.c & 1)) & 0xff);
        }
        if (R1->target != NULL)
            CASE("LinkCreep_CalcToRouteEnd", 0x0004ac30, AT(RouteUintFn, 0x0004ac30)(R1));
        if (R0->path != NULL && R0->count > 0 && R0->current >= 0) {
            CASE("AINetwork_SetupNextNode", 0x0004ae90, CallNextNode(0x0004ae90, R0, pick.a > 2 ? 1 : -1) & 0xff);
            CASE("LinkCreep_Increment", 0x0004b010,
                 CallIncrement(0x0004b010, R0, pick.c ? &world.blocked : NULL) & 0xff);
            CASE("LinkCreep_Decrement", 0x0004b110, CallDecrement(0x0004b110, R0) & 0xff);
            CASE("LinkCreep_Handler", 0x0004b200, CallHandler(0x0004b200, R0, &world.dcv) & 0xff);
            CASE("AINetwork_FollowRoute", 0x0004b370, AT(FollowRouteFn, 0x0004b370)(R0, &world.dcv));
        }
        if (R0->current == -1 && R0->path != NULL)
            CASE("LinkCreep_Decrement", 0x0004b110, CallDecrement(0x0004b110, R0) & 0xff);
        if (pick.c == 3) {
            R1->path = NULL;
            CASE("AINetwork_FollowRoute", 0x0004b370, AT(FollowRouteFn, 0x0004b370)(R1, &world.dcv));
        }
    }
}

void TestDrones() {
    for (int trial = 0; trial < 500; trial++) {
        RandomScene();
        CASE("NDrone2_Player", 0x00035f90, (uint32_t)WhichObj(AT(PlayerFn, 0x00035f90)()));
        CASE("NDrone2_FindCel", 0x00036b80, (uint32_t)WhichCel(AT(FindCelFn, 0x00036b80)(&world.vecs[pick.c])));
        CASE("NDrone2_FeetPos", 0x00036dc0, AT(FeetPosFn, 0x00036dc0)(&world.objs[pick.c], &world.vecs[5]) & 0xff);
        // Points near the drones and the player
        world.vecs[0] = world.objs[pick.c].position;
        world.vecs[0].x += ShadowRandFloat(-1.0f, 1.0f);
        world.vecs[0].y += ShadowRandFloat(-2.5f, 2.5f);
        CASE("NDrone2_DroneNearPos", 0x00039870,
             AT(DroneNearPosFn, 0x00039870)(&world.vecs[0], pick.f * 0.3f,
                                            pick.a > 4 ? NULL : &world.objs[pick.a & 3]) & 0xff);
    }
}

} // namespace

void NavShadow_Run(void) {
    static uint8_t paths[sizeof(AIPaths)], bounds[sizeof(AIBounds)], globals[0x24], npc[sizeof(NPCGlobals_t)];
    static uint8_t mpSettings[sizeof(MPSettings_t)], gameState[sizeof(GameState_t)], cels[8];
    static obj_tag *players[4];
    memcpy(paths, &AIPaths, sizeof(paths));
    memcpy(bounds, &AIBounds, sizeof(bounds));
    memcpy(globals, (void *)XbeOurs(0x00274ca8), sizeof(globals));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(mpSettings, &MPSettings, sizeof(mpSettings));
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(cels, (void *)XbeOurs(0x001ee42c), sizeof(cels));
    memcpy(players, glb_players, sizeof(players));
    uint32_t boundCount = AIBoundCount;

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&AIPaths, sizeof(AIPaths));
    regions.Add(&AIBounds, sizeof(AIBounds));
    regions.Add(&AIBoundCount, 4);
    regions.Add((void *)XbeOurs(0x00274ca8), sizeof(globals));     // the block version to the path count
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(&MPSettings, sizeof(MPSettings_t));
    regions.Add((void *)XbeOurs(0x001ee42c), sizeof(cels));
    regions.Add(glb_players, sizeof(players));
    snapshot.Clear();
    snapshot.Add(&world, sizeof(world));
    snapshot.Add(&AIPaths, sizeof(AIPaths));
    snapshot.Add(&AIBounds, sizeof(AIBounds));
    snapshot.Add(&AIBoundCount, 4);
    snapshot.Add((void *)XbeOurs(0x00274ca8), sizeof(globals));

    InstallFakes();
    NfWarnMuted++;
    cases = differ = 0;

    TestLoad();
    int loadCases = cases, loadDiffer = differ;
    LoadLevel();
    TestRouteState();
    TestSearches();
    int searchCases = cases, searchDiffer = differ;
    TestGraph();
    int graphCases = cases, graphDiffer = differ;
    TestBounds();
    int boundCases = cases, boundDiffer = differ;
    TestPlanning();
    TestFollowing();
    TestDrones();

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&AIPaths, paths, sizeof(paths));
    memcpy(&AIBounds, bounds, sizeof(bounds));
    memcpy((void *)XbeOurs(0x00274ca8), globals, sizeof(globals));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(&MPSettings, mpSettings, sizeof(mpSettings));
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy((void *)XbeOurs(0x001ee42c), cels, sizeof(cels));
    memcpy(glb_players, players, sizeof(players));
    AIBoundCount = boundCount;

    printf("[nav] AIPath_*, AIBounds_*: %d cases, %d differ\n", loadCases, loadDiffer);
    printf("[nav] routes, link flags, node searches, targets: %d cases, %d differ\n", searchCases - loadCases,
           searchDiffer - loadDiffer);
    printf("[nav] A*, emitters: %d cases, %d differ\n", graphCases - searchCases, graphDiffer - searchDiffer);
    printf("[nav] bounds, move tests: %d cases, %d differ\n", boundCases - graphCases, boundDiffer - graphDiffer);
    printf("[nav] route planning, link creep, following, drones: %d cases, %d differ\n", cases - boundCases,
           differ - boundDiffer);
}
