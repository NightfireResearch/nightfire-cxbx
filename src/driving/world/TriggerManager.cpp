#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // (the build defines it; for a file compiled alone)
#endif

#pragma fp_contract(off)

#include "TriggerManager.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Grid.h"
#include "World.h"
#include "WorldMath.h"
#include "../../helpers.h"
#include "../engine/CoreFoundation.h"     // NullFunction
#include "../engine/OBB.h"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../platform/FileSys.h"          // FILE_exists
#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"        // MEM_copy

// ---------------------------------------------------------------------------------------------------------------
// WTriggerManager (0x000cf520-0x000d0d50), ported from the listing: the triggers' lifecycle, the collision tests
// against what can touch them, and the frame's processing.
//
// The tests are the original's x87 arithmetic: chains in double in the original's order, rounded where the original
// stores to a float; the comparisons keep the original's sense, so a NaN fails each test as it did.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define Crt_sprintf ((int (*)(char *, const char *, ...))0x00132767)
#define Crt_stricmp ((int (*)(const char *, const char *))0x00134537)
#define Crt_printf ((int (*)(const char *, ...))0x00132192)
#define ASoundManager_ReportFailure ((void (*)(const char *name))0x00121e40)
// the y size packed in an instance's dimensions (unnamed in Ghidra; the name is ours)
#define Instance_SizeY ((double (__fastcall *)(const CARP::Instance *, int))0x0008d640)
#define Simulation_GetRigidBody ((RigidBody *(__fastcall *)(void *, int, int index))0x000b2700)
#define Simulation_GetSimpleRigidBody ((SimpleRigidBody *(__fastcall *)(void *, int, int index))0x000b2730)
#define Simulation_GetPlayerObject ((PhysicsObject *(__fastcall *)(void *, int))0x000b2d30)
#define Simulation_FindPhysicsObjectSignature ((void *(__fastcall *)(void *, int, uint32_t signature))0x000b27d0)
#define RayShell_GetNumActiveRayShells ((int (*)())0x00071930)
#define RayShell_GetActiveRayShell ((ActiveRayShell *(*)(int index))0x00071940)
#define RayShell_ClearActiveRayShells ((void (*)())0x00071960)
#define RayShell_SetTriggerHittingRayShell ((void (*)(int index))0x00071970)
#define RPathEngine_GetFirstPathHandle ((RPathHandle *(*)())0x0007ffa0)
#define RPathEngine_GetNextPathHandle ((RPathHandle *(*)())0x0007ffc0)

// ---- globals
#define TriggerDataSize U32_AT(0x0023e264)
#define TriggerData (*(WTrigger **)0x0023e268)     // the track's triggers (fgTriggerManager->triggers)
#define TriggerDataCopy PTR_AT(0x0023e26c)         // their state at Init, put back by Restart
#define QueryStamp U32_AT(0x0023e270)              // the triggers' queryStamp of the current Process
#define ASystem_fgSystem PTR_AT(0x00243b34)
#define Sim ((void *)0x00233ff0)                    // the Simulation

namespace {

constexpr uint32_t kMapTag = 0x4d617020;     // 'Map '
constexpr uint32_t kRuleTag = 0x52756c65;    // 'Rule'

constexpr int kRigidBodies = 0x40;
constexpr int kSimpleBodies = 0x60;
constexpr uint32_t kCellsReserved = 0x40;

constexpr uint32_t kDimensionsSeparate = 0x80000000;   // CARP::Instance::packedDimensions
constexpr int kDimensionUnitShift = 30;
constexpr uint32_t kDimensionMask = 0x3ff;
constexpr float kDimensionUnit[2] = { 0.25f, 16.0f };

constexpr float kMinimumBoxRayRadius = 0.01f;   // 0x3c23d70a
constexpr float kParallel = 0.0001f;            // 0x38d1b717
constexpr float kLevel = 0.01f;                 // 0x3c23d70a
constexpr float kAlongX = 0.001f;               // 0x3a83126f
constexpr float kMinimumBodyRadius = 1.5f;

// A mission rule ('Rule' data, 0x20 bytes): only its events are read here.
struct MissionRule {
    uint8_t unknown00[0xc];
    TriggerEvents *events;      // +0x0c
    uint8_t unknown10[0x10];
};
static_assert(sizeof(MissionRule) == 0x20, "a mission rule is 32 bytes");

void ReportShape(uint8_t shape) {
    Crt_printf("WTrigger - Unsupported trigger Shape: %d\n", shape);
}

// Checks that the files of the stream events exist. The line is built but not printed in this build.
void ValidateStreams(TriggerEvents *events, bool notePreBuffer) {
    if (events == NULL)
        return;
    TriggerEvent *event = events->Events();
    for (int i = 0; i < events->count; i++, event++) {
        if (event->type != kEventStream)
            continue;
        StreamEventData *stream = reinterpret_cast<StreamEventData *>(event->Data());
        if (stream == NULL || stream->type == NULL || stream->name == NULL)
            continue;
        char path[64];
        char line[64];
        Crt_sprintf(path, "%s.asf", stream->name);
        Crt_sprintf(line, "Validating stream [%s] (%s): ", path, stream->type);
        if (Crt_stricmp(stream->type, "music") == 0 || Crt_stricmp(stream->type, "speech") == 0 ||
            Crt_stricmp(stream->type, "nis") == 0) {
            if (FILE_exists(path)) {
                strcat(line, "OK ");
            } else {
                strcat(line, "FAILED ");
                ASoundManager_ReportFailure(stream->name);
            }
        } else {
            strcat(line, "N/A ");
        }
        if (notePreBuffer && stream->preBuffer > 0)
            strcat(line, "preBuf");
    }
}

// A box trigger in its own frame: x across (width), y up from 0 (height), z along (depth).
struct LocalBox {
    float halfWidth;
    float height;
    float halfDepth;

    bool Contains(const Coord4 &p) const {
        return p.x > -halfWidth && p.x < halfWidth && p.y > 0.0f && p.y < height && p.z > -halfDepth &&
               p.z < halfDepth;
    }

    // Whether the point at t along the segment, on one of the faces across x, is within that face
    bool OnXFace(float t, const Coord4 &start, const Coord4 &direction) const {
        if (!(t >= 0.0f && t <= 1.0f))
            return false;
        double y = double(direction.y) * t + start.y;
        if (!(y >= 0.0 && y < height))
            return false;
        return fabs(double(direction.z) * t + start.z) < halfDepth;
    }

    // The same on a face across z
    bool OnZFace(float t, const Coord4 &start, const Coord4 &direction) const {
        if (!(t >= 0.0f && t <= 1.0f))
            return false;
        double y = double(t) * direction.y + start.y;
        if (!(fabs(double(t) * direction.x + start.x) < halfWidth))
            return false;
        return y >= 0.0 && y < height;
    }
};

}  // namespace

// Whether the trigger is one the current Process looks at: not seen yet (it is marked so), enabled, not ignored,
// active if it has to be, and touched by what is tested.
inline bool WTriggerManager::Considers(WTrigger *trigger, unsigned touch) {
    if (trigger->queryStamp == QueryStamp)
        return false;
    trigger->queryStamp = QueryStamp;
    unsigned flags = trigger->flags;
    if (!(flags & WTrigger::kEnabled) || (flags & WTrigger::kIgnored))
        return false;
    if ((flags & WTrigger::kWhileActive) && !active)
        return false;
    return (flags & touch) != 0;
}

// ---- the lifecycle

// FUNC_AT(0x000d03b0)
void WTriggerManager::Init(UData *triggerData) {
    int count = 0;
    WTrigger *triggers = NULL;
    if (triggerData != NULL) {
        TriggerDataSize = triggerData->Size();
        TriggerData = reinterpret_cast<WTrigger *>(triggerData->Data());
        TriggerDataCopy = UMemory::Alloc(TriggerDataSize, 0, "Triggers");
        MEM_copy(TriggerDataCopy, TriggerData, TriggerDataSize);
        count = triggerData->count;
        triggers = TriggerData;
    }
    WTriggerManager *manager = static_cast<WTriggerManager *>(OperatorNew(sizeof(WTriggerManager)));
    if (manager != NULL) {
        manager->count = count;
        manager->triggers = triggers;
        manager->active = true;
    }
    fgTriggerManager = manager;

    Restart();
    for (int i = 0; i < fgTriggerManager->count; i++) {
        WTrigger *trigger = &fgTriggerManager->triggers[i];
        if (trigger->flags & WTrigger::kInstances)
            trigger->flags &= ~WTrigger::kWhileActive;
    }
    QueryStamp = 0;
}

// Puts the triggers back as Init found them, fires those that start the mission, and (with the sound system up)
// checks the streams the triggers and the mission rules play.
// FUNC_AT(0x000cff90)
void WTriggerManager::Restart() {
    UGroup *map = fgWorld->group->GroupLocateTag(kMapTag);
    UData *rules = map->DataLocateTag(kRuleTag);
    MEM_copy(TriggerData, TriggerDataCopy, TriggerDataSize);
    fgTriggerManager->active = true;

    for (int i = 0; i < fgTriggerManager->count; i++) {
        WTrigger *trigger = &fgTriggerManager->triggers[i];
        if (trigger->events != NULL && trigger->events->Has(kEventStartMission))
            trigger->FireEvents(true, -1, NULL);
        trigger = &fgTriggerManager->triggers[i];
        if (trigger->events != NULL && trigger->events->Has(kEventStream) && ASystem_fgSystem != NULL)
            ValidateStreams(trigger->events, true);
    }

    if (rules != map->DataEnd()) {
        MissionRule *rule = reinterpret_cast<MissionRule *>(rules->Data());
        for (int i = 0; i < int(rules->count); i++, rule++)
            ValidateStreams(rule->events, false);
    }
}

// ---- the tests

// A ray shell's segment, thickened by its radius. The segment's middle has to be near the trigger in x/z first.
// FUNC_AT(0x000cf520)
bool WTriggerManager::CheckCollide(const Coord4 *segment, float radius, WTrigger *trigger) {
    double reach = double(radius) + trigger->radius;
    double middleX = (double(segment[0].x) + segment[1].x) * 0.5;
    double middleZ = (double(segment[0].z) + segment[1].z) * 0.5;
    double dz = trigger->position.z - middleZ;
    double dx = trigger->position.x - middleX;
    if (!(reach * reach > dx * dx + dz * dz))
        return false;
    if ((trigger->flags & WTrigger::kDirectional) && !trigger->TestDirection(segment))
        return false;

    switch (trigger->shape) {
    case WTrigger::kCylinder:
    case WTrigger::kSphere: {
        alignas(16) Coord4 centre = { trigger->position.x, trigger->position.y, trigger->position.z, 0.0f };
        alignas(16) Coord4 nearest;
        if (trigger->shape == WTrigger::kSphere) {
            WWorldMath::NearestPointLine3D(&centre, segment, &nearest);
            float triggerRadius = trigger->radius;
            return double(triggerRadius) * triggerRadius > VU0_v3distancesquare(&nearest, &centre);
        }
        WWorldMath::NearestPointLine2D(&centre, segment, &nearest);
        float triggerRadius = trigger->radius;
        if (!(double(triggerRadius) * triggerRadius > VU0_v3distancesquarexz(&nearest, &centre)))
            return false;
        // the height of the segment where it passes nearest the axis
        double dy = double(segment[1].y) - segment[0].y;
        if (fabs(dy) < kLevel)
            return trigger->position.y <= segment[0].y &&
                   double(trigger->height) + trigger->position.y > segment[0].y;
        double t;
        double alongX = double(segment[1].x) - segment[0].x;
        if (fabs(alongX) > kAlongX)
            t = (double(segment[0].x) - nearest.x) / alongX;
        else
            t = (double(segment[0].z) - nearest.z) / (double(segment[1].z) - segment[0].z);
        double y = -t * dy + segment[0].y;
        return y >= trigger->position.y && y <= double(trigger->height) + trigger->position.y;
    }

    case WTrigger::kBox: {
        if (radius < kMinimumBoxRayRadius)
            return false;
        alignas(16) MATRIX4 frame;
        trigger->MakeMatrix(&frame, true);
        OrthoInverse(&frame);
        alignas(16) Coord4 start;
        alignas(16) Coord4 end;
        alignas(16) Coord4 direction;
        TransformPoint(&frame, &segment[0], &start);
        TransformPoint(&frame, &segment[1], &end);
        VU0_v4sub4(&end, &start, &direction);
        LocalBox box = { trigger->width * 0.5f, trigger->height, trigger->depth * 0.5f };

        if (box.Contains(start) || box.Contains(end))
            return true;
        if (fabsf(direction.x) > kParallel) {
            double inverse = 1.0 / direction.x;
            if (box.OnXFace(float(-((double(start.x) - box.halfWidth) * inverse)), start, direction))
                return true;
            if (box.OnXFace(float(-((double(start.x) + box.halfWidth) * inverse)), start, direction))
                return true;
        }
        if (fabsf(direction.z) > kParallel) {
            double inverse = 1.0 / direction.z;
            if (box.OnZFace(float(-((double(start.z) - box.halfDepth) * inverse)), start, direction))
                return true;
            if (box.OnZFace(float(-((double(start.z) + box.halfDepth) * inverse)), start, direction))
                return true;
        }
        return false;
    }

    default:
        ReportShape(trigger->shape);
        return false;
    }
}

// A rigid body: its sphere against a sphere, its circle and height against a cylinder, its box against a box.
// FUNC_AT(0x000cfae0)
bool WTriggerManager::CheckCollide(RigidBody *body, WTrigger *trigger) {
    double reach = double(body->radius) + trigger->radius;
    float reachSquared = float(reach * reach);
    alignas(16) Coord4 centre = { trigger->position.x, trigger->position.y, trigger->position.z, 0.0f };

    if (trigger->shape == WTrigger::kSphere) {
        if (!(VU0_v3distancesquare(&centre, &body->position) < reachSquared))
            return false;
        if ((trigger->flags & WTrigger::kDirectional) && v3dotprod(&trigger->forward, &body->velocity) < 0.0f)
            return false;
        return true;
    }

    if (!(VU0_v3distancesquarexz(&centre, &body->position) < reachSquared))
        return false;
    if ((trigger->flags & WTrigger::kDirectional) && v3dotprod(&trigger->forward, &body->velocity) < 0.0f)
        return false;

    if (trigger->shape == WTrigger::kCylinder) {
        return double(body->radius) + body->position.y >= trigger->position.y &&
               double(trigger->height) + trigger->position.y > double(body->position.y) - body->radius;
    }

    if (trigger->shape == WTrigger::kBox) {
        alignas(16) Coord4 bodyCentre = { body->position.x, body->position.y, body->position.z, 1.0f };
        alignas(16) Coord4 bodyExtents = body->info->halfExtents;
        alignas(16) OBB bodyBox;
        bodyBox.Construct(&body->info->orientation, &bodyCentre, &bodyExtents);

        // the trigger's box: centred half its height up its up axis
        alignas(16) MATRIX4 frame;
        trigger->MakeMatrix(&frame, false);
        double halfHeight = double(trigger->height) * 0.5;
        alignas(16) Coord4 triggerCentre = *trigger->Bounds();
        triggerCentre.x = float(frame.mtx[1][0] * halfHeight + triggerCentre.x);
        triggerCentre.y = float(frame.mtx[1][1] * halfHeight + triggerCentre.y);
        triggerCentre.z = float(frame.mtx[1][2] * halfHeight + triggerCentre.z);
        triggerCentre.w = 1.0f;
        alignas(16) Coord4 triggerExtents = { trigger->width * 0.5f, float(halfHeight), trigger->depth * 0.5f, 1.0f };
        alignas(16) OBB triggerBox;
        triggerBox.Construct(&frame, &triggerCentre, &triggerExtents);

        bool hit = triggerBox.CheckOBBOverlap(&bodyBox);
        NullFunction();   // ~OBB
        NullFunction();   // ~OBB
        return hit;
    }

    ReportShape(trigger->shape);
    return false;
}

// A simple rigid body: its circle and height against the trigger's, whatever the shape.
// FUNC_AT(0x000cfdf0)
bool WTriggerManager::CheckCollide(SimpleRigidBody *body, WTrigger *trigger) {
    float radius = body->radius;
    double reach = double(radius) + trigger->radius;
    double dz = double(trigger->position.z) - body->position.z;
    double dx = double(trigger->position.x) - body->position.x;
    if (!(reach * reach > dx * dx + dz * dz))
        return false;
    if ((trigger->flags & WTrigger::kDirectional) && v3dotprod(&trigger->forward, &body->velocity) < 0.0f)
        return false;
    return double(radius) + body->position.y >= trigger->position.y &&
           double(trigger->height) + trigger->position.y > double(body->position.y) - radius;
}

// FUNC_AT(0x000cfeb0)
bool WTriggerManager::CheckCollide(const Coord3 *point, float radius, WTrigger *trigger) {
    double dz = double(point->z) - trigger->position.z;
    double dx = double(point->x) - trigger->position.x;
    if (!(double(radius) * radius > dx * dx + dz * dz))
        return false;
    return double(radius) + point->y >= trigger->position.y &&
           double(trigger->height) + trigger->position.y > double(point->y) - radius;
}

// FUNC_AT(0x000cff20)
bool WTriggerManager::CheckCollide(const Coord3 *point, float radius, float above, float below, WTrigger *trigger) {
    double dz = double(point->z) - trigger->position.z;
    double dx = double(point->x) - trigger->position.x;
    if (!(double(radius) * radius > dx * dx + dz * dz))
        return false;
    return double(above) + point->y >= trigger->position.y &&
           double(trigger->height) + trigger->position.y > double(point->y) - below;
}

// ---- the frame's processing: each thing against the triggers in the grid cells round it

// FUNC_AT(0x000d04a0)
void WTriggerManager::Process(int index, RigidBody *body) {
    QueryStamp++;
    unsigned touch;
    if (body->kind == 1)
        touch = WTrigger::kRigidType1;
    else if (body->kind == 2)
        touch = WTrigger::kRigidType2;
    else
        touch = WTrigger::kOthers;
    float radius = body->radius;

    WGridCellList cells;
    cells.first = NULL;
    cells.last = NULL;
    cells.end = NULL;
    cells.Reserve(kCellsReserved);
    WGrid *grid = TheGrid;
    grid->FindNodes(&body->position, radius, &cells);
    for (uint32_t *cell = cells.first; cell != cells.last; cell++) {
        WGridNode *node = grid->nodes[*cell];
        if (node == NULL)
            continue;
        GridCellIterator it;
        it.Construct(node, kGridTrigger);
        for (const uint16_t *element = it.Next(); element != NULL; element = it.Next()) {
            WTrigger *trigger = &triggers[*element];
            if (Considers(trigger, touch) && CheckCollide(body, trigger))
                trigger->FireEvents(false, index, NULL);
        }
    }
    ColStl::Tidy(&cells);
}

// A path's instance: an upright cylinder of its x size, up and down by its y and z sizes when they were given
// separately, else by its x size.
// FUNC_AT(0x000d0630)
void WTriggerManager::Process(CARP::Instance *instance) {
    uint32_t packed = instance->packedDimensions;
    float radius = float(packed & kDimensionMask) * kDimensionUnit[(packed >> kDimensionUnitShift) & 1];
    const Coord3 *position = reinterpret_cast<const Coord3 *>(instance->position);
    float above = float(Instance_SizeY(instance, 0));
    // the game calls WTrigger::Size on the instance: the same packed word at +0x3c, its z size
    float below = float(reinterpret_cast<WTrigger *>(instance)->Size());
    QueryStamp++;

    WGridCellList cells;
    cells.first = NULL;
    cells.last = NULL;
    cells.end = NULL;
    cells.Reserve(kCellsReserved);
    WGrid *grid = TheGrid;
    grid->FindNodes(position, radius, &cells);
    for (uint32_t *cell = cells.first; cell != cells.last; cell++) {
        WGridNode *node = grid->nodes[*cell];
        if (node == NULL)
            continue;
        GridCellIterator it;
        it.Construct(node, kGridTrigger);
        for (const uint16_t *element = it.Next(); element != NULL; element = it.Next()) {
            WTrigger *trigger = &triggers[*element];
            if (!Considers(trigger, WTrigger::kInstances))
                continue;
            bool hit;
            if (instance->packedDimensions & kDimensionsSeparate)
                hit = CheckCollide(position, radius, above, below, trigger);
            else
                hit = CheckCollide(position, radius, trigger);
            if (hit)
                trigger->FireEvents(false, -1, instance);
        }
    }
    ColStl::Tidy(&cells);
}

// FUNC_AT(0x000d0810)
void WTriggerManager::Process(int index, SimpleRigidBody *body) {
    QueryStamp++;
    unsigned touch = 0;
    PhysicsObject *player = Simulation_GetPlayerObject(Sim, 0);
    PhysicsObject *owner = body->GetOwner();
    bool byPlayer = owner->IsOwnedBy(player);
    if (!(body->flags & SimpleRigidBody::kTouchesTriggers))
        return;
    if (byPlayer)
        touch = WTrigger::kPlayer;
    switch (body->bodyType) {
    case kSimpleExplosion:
        touch |= WTrigger::kSimpleType1;
        break;
    case kSimpleHuman:
    case kSimpleHelicopter:
        touch |= WTrigger::kRigidType2;
        break;
    case kSimpleMissile:
    case kSimpleShell:
        touch |= WTrigger::kOthers;
        break;
    }
    if (touch == 0)
        return;
    float radius = body->radius < kMinimumBodyRadius ? kMinimumBodyRadius : body->radius;

    WGridCellList cells;
    cells.first = NULL;
    cells.last = NULL;
    cells.end = NULL;
    cells.Reserve(kCellsReserved);
    WGrid *grid = TheGrid;
    grid->FindNodes(&body->position, radius, &cells);
    for (uint32_t *cell = cells.first; cell != cells.last; cell++) {
        WGridNode *node = grid->nodes[*cell];
        if (node == NULL)
            continue;
        GridCellIterator it;
        it.Construct(node, kGridTrigger);
        for (const uint16_t *element = it.Next(); element != NULL; element = it.Next()) {
            WTrigger *trigger = &triggers[*element];
            if (!Considers(trigger, touch))
                continue;
            if (!byPlayer && (trigger->flags & WTrigger::kPlayer))
                continue;
            if (CheckCollide(body, trigger))
                trigger->FireEvents(true, index, NULL);
        }
    }
    ColStl::Tidy(&cells);
}

// A queued ray shell: its segment against the triggers in the cells it crosses.
// FUNC_AT(0x000d0a30)
void WTriggerManager::Process(int rayShell) {
    ActiveRayShell *ray = RayShell_GetActiveRayShell(rayShell);
    unsigned touch = WTrigger::kOthers;
    void *owner = Simulation_FindPhysicsObjectSignature(Sim, 0, ray->ownerSignature);
    bool byPlayer = owner == Simulation_GetPlayerObject(Sim, 0);
    if (byPlayer)
        touch = WTrigger::kOthers | WTrigger::kPlayer;
    alignas(16) Coord4 segment[2] = {
        { ray->start.x, ray->start.y, ray->start.z, 1.0f },
        { ray->end.x, ray->end.y, ray->end.z, 1.0f },
    };
    QueryStamp++;

    WGridCellList cells;
    cells.first = NULL;
    cells.last = NULL;
    cells.end = NULL;
    WGrid *grid = TheGrid;
    cells.Reserve(kCellsReserved);
    grid->FindNodes(segment, &cells);
    for (uint32_t *cell = cells.first; cell != cells.last; cell++) {
        if (*cell >= grid->rows * grid->columns) {
            Crt_printf("Node out of range: %d\n", *cell);
            continue;
        }
        WGridNode *node = grid->nodes[*cell];
        if (node == NULL)
            continue;
        GridCellIterator it;
        it.Construct(node, kGridTrigger);
        for (const uint16_t *element = it.Next(); element != NULL; element = it.Next()) {
            WTrigger *trigger = &triggers[*element];
            if (!Considers(trigger, touch))
                continue;
            if (!byPlayer && (trigger->flags & WTrigger::kPlayer))
                continue;
            if (CheckCollide(segment, ray->radius, trigger)) {
                RayShell_SetTriggerHittingRayShell(rayShell);
                trigger->FireEvents(true, -1, NULL);
                RayShell_SetTriggerHittingRayShell(-1);
            }
        }
    }
    ColStl::Tidy(&cells);
}

// The frame's tests: the awake rigid bodies, the simple bodies, the instances of the paths that have none of their
// own (unknownA0), and the ray shells queued since the last frame, which it then clears.
// FUNC_AT(0x000d0c90)
void WTriggerManager::Update() {
    RayShell_SetTriggerHittingRayShell(-1);
    for (int i = 0; i < kRigidBodies; i++) {
        if (PhysicsObjects[i] == NULL)
            continue;
        RigidBody *body = Simulation_GetRigidBody(Sim, 0, i);
        if (body->sleepState == RigidBody::kAwake)
            Process(i, body);
    }
    for (int i = 0; i < kSimpleBodies; i++) {
        if (SimpleBodyOwners[i] != NULL)
            Process(i, Simulation_GetSimpleRigidBody(Sim, 0, i));
    }
    for (RPathHandle *path = RPathEngine_GetFirstPathHandle(); path != NULL; path = RPathEngine_GetNextPathHandle()) {
        if (path->unknownA0 == NULL)
            Process(path->instance);
    }
    int rayShells = RayShell_GetNumActiveRayShells();
    for (int i = 0; i < rayShells; i++)
        Process(i);
    RayShell_ClearActiveRayShells();
}
