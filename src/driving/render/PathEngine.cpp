#pragma fp_contract(off)

#include "PathEngine.h"

#include <math.h>
#include <stdio.h>

#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "../engine/CoreFoundation.h"   // GameEmptyString, ThrowLengthError
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"
#include "../world/World.h"             // kWorldInstanceProcAnim, ArticleOf, WorldModel
#include "RSceneObj.hpp"

// ---------------------------------------------------------------------------------------------------------------
// RPathEngine and RPathHandle (0x0007f9f0..0x00080820) and the camera helpers after them (..0x00080a60), ported
// from the listing. The x87 code keeps the original's order and roundings.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's globals

#define TickSeconds FLOAT_AT(0x001f2a48)        // RTimeData::Init's: 1 / VideoModeRate
#define VideoModeRate FLOAT_AT(0x001f2a44)      // the video mode's frames per second
#define ZeroVector (*(const Coord3 *)0x00243030)

// ---- the game's code not ported yet

#define Instance_SetMatrix ((void (__fastcall *)(CARP::Instance *, int, const MATRIX4 *))0x00035610)

namespace {

void PathsUntested(const char *what) {
    printf("[paths] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define PATHS_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            PathsUntested(what); \
        } \
    } while (0)

// The handles' times are in 60ths of a second
constexpr float kPathFrameRate = 60.0f;

// The path's axes into the world's: x to -z, z to x
const MATRIX4 kPathAxes = { {
    { 0.0f, 0.0f, -1.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
} };

// The time now, in 60ths of a second from `time` ticks
double PathTime(float time) {
    return double(TickSeconds) * time * kPathFrameRate;
}

// The scene object re-posed from its instance (RSceneObj vtable slot 14, UpdatePosition)
void UpdateScenePosition(RSceneObj *sceneObj) {
    typedef void (RSceneObj::*UpdatePositionMethod)(bool);
    (sceneObj->*XbeVirtual<UpdatePositionMethod>(sceneObj, 14))(true);
}

// The path's matrix at the handle's time: evaluated over the base matrix, then through the apply matrix, and the
// instance (and its scene object) posed there
void PoseInstance(RPathHandle *handle, MATRIX4 *frame) {
    Instance_SetMatrix(handle->instance, 0, frame);
    RSceneObj *sceneObj = handle->procAnim->sceneObj;
    if (sceneObj != NULL)
        UpdateScenePosition(sceneObj);
}

// A new list (the constructor inlined), NULL if the allocation failed
PathList *NewPathList() {
    PathList *list = static_cast<PathList *>(OperatorNew(sizeof(PathList)));
    return list != NULL ? list->Construct() : NULL;
}

// erase(where), as the compiled code has it inline: the head is not erased
void EraseNode(PathList *list, PathListNode *node) {
    if (node == list->head)
        return;
    node->prev->next = node->next;
    node->next->prev = node->prev;
    UMemory::FastFree(node, sizeof(PathListNode));
    list->size--;
}

// A handle for each instance whose proc-anim state names a path, with or without a master
void AddPaths(CARP::Instance *instances, ProcAnimState *states, uint32_t count, bool withMaster) {
    for (uint32_t i = 0; i < count; i++) {
        CARP::Instance *instance = &instances[i];
        if (!(instance->flags & kWorldInstanceProcAnim))
            continue;
        ProcAnimState *state = &states[instance->procAnimIndex];
        if (state->type != kPathAnimType || state->path == NULL || (state->master != NULL) != withMaster)
            continue;
        RPathHandle handle;
        handle.Construct(instance, state);
        fgPathHandles->PushBack(&handle);
    }
}

// The sine and tangent in turns and atan_turns are assembly that leave their results unrounded in ST0
typedef double (*UnroundedTurnsFn)(float);
typedef double (*UnroundedAtanFn)(float, float);

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// RPathHandle

// FUNC_AT(0x0007f9f0)
RPathHandle* RPathHandle::ConstructCopy(const RPathHandle *other) {
    instance = other->instance;
    procAnim = other->procAnim;
    baseMatrix = other->baseMatrix;
    applyMatrix = other->applyMatrix;
    velocity = other->velocity;
    lastUpdateTime = other->lastUpdateTime;
    currentPath = other->currentPath;
    nextPath = other->nextPath;
    master = other->master;
    speed = other->speed;
    timeDelta = other->timeDelta;
    parametricTime = other->parametricTime;
    positionKey = 0;
    rotationKey = 0;
    weight = other->weight;
    throttle = other->throttle;
    acceleration = other->acceleration;
    desiredThrottle = other->desiredThrottle;
    accelDelay = other->accelDelay;
    running = other->running;
    userData = other->userData;
    return this;
}

// FUNC_AT(0x0007fb00)
RPathHandle* RPathHandle::Construct(CARP::Instance *instance, ProcAnimState *state) {
    procAnim = state;
    this->instance = instance;
    velocity = ZeroVector;
    lastUpdateTime = 0.0f;
    currentPath = NULL;
    nextPath = NULL;
    master = NULL;
    speed = state->speed;
    timeDelta = 0.0f;
    parametricTime = 0.0f;
    positionKey = 0;
    rotationKey = 0;
    weight = 0.0f;
    throttle = 0.0f;
    acceleration = 0.0f;
    desiredThrottle = 0.0f;
    accelDelay = 0;
    running = false;
    userData = GameEmptyString;
    baseMatrix = instance->Matrix();
    baseMatrix.mtx[0][3] = 0.0f;
    baseMatrix.mtx[1][3] = 0.0f;
    baseMatrix.mtx[2][3] = 0.0f;
    baseMatrix.mtx[3][3] = 1.0f;
    return this;
}

// FUNC_AT(0x0007fbc0)
void RPathHandle::Update(float time) {
    double now = PathTime(time);
    float nowRounded = float(now);
    double elapsed = now - lastUpdateTime;
    float step = float(elapsed);
    if (elapsed <= 0.0)
        return;
    if (!running) {
        lastUpdateTime = nowRounded;
        return;
    }
    if (currentPath == NULL) {
        currentPath = nextPath;
        lastUpdateTime = nowRounded;
        return;
    }

    if (master != NULL) {
        parametricTime = master->parametricTime - timeDelta;
    } else {
        // The throttle moves towards the desired one by the acceleration, once the delay has run out; a throttle
        // at rest at zero stops the path
        if (accelDelay != 0) {
            accelDelay--;
        } else if (throttle == desiredThrottle) {
            if (throttle == 0.0f)
                running = false;
        } else if (throttle < desiredThrottle) {
            if (double(desiredThrottle) - throttle < acceleration)
                throttle = desiredThrottle;
            else
                throttle = acceleration + throttle;
        } else if (throttle > desiredThrottle) {
            if (double(throttle) - desiredThrottle < acceleration)
                throttle = desiredThrottle;
            else
                throttle = throttle - acceleration;
        }
        double advance = double(throttle) * speed * step;
        timeDelta = float(advance);
        parametricTime = float(advance + parametricTime);
    }

    // Past either end the time wraps into the next path, or stops at the end there is
    bool wrapped = false;
    float duration = currentPath->duration;
    if (parametricTime > duration) {
        parametricTime = parametricTime - duration;
        wrapped = true;
    } else if (parametricTime < 0.0f) {
        wrapped = true;
    }
    if (wrapped) {
        if (nextPath != NULL) {
            currentPath = nextPath;
            float nextDuration = nextPath->duration;
            if (parametricTime < 0.0f)
                parametricTime = nextDuration + parametricTime;
            else if (parametricTime > nextDuration)
                parametricTime = nextDuration;
        } else {
            parametricTime = parametricTime < 0.0f ? 0.0f : duration;
            throttle = 0.0f;
            running = false;
        }
    }

    alignas(16) MATRIX4 frame;
    MatrixCopy(&baseMatrix, &frame);
    currentPath->EvaluateMatrix(parametricTime, &positionKey, &rotationKey, frame.mtx[0], &weight);
    VU0_MATRIX4_mult(&frame, &applyMatrix, &frame);
    // A wrap keeps the last velocity
    if (!wrapped) {
        velocity.x = float((double(frame.mtx[3][0]) - instance->position[0]) * VideoModeRate);
        velocity.y = float((double(frame.mtx[3][1]) - instance->position[1]) * VideoModeRate);
        velocity.z = float((double(frame.mtx[3][2]) - instance->position[2]) * VideoModeRate);
    }
    PoseInstance(this, &frame);
    lastUpdateTime = nowRounded;
}

// FUNC_AT(0x0007fec0)
void RPathHandle::SetNextPath(CARP::PathInfo *path) {
    nextPath = path;
}

// FUNC_AT(0x0007fed0)
float RPathHandle::GetParametricDuration() {
    return currentPath->duration;
}

// FUNC_AT(0x0007fee0)
void RPathHandle::SetRunning(bool on) {
    running = on;
}

// FUNC_AT(0x0007fef0)
void RPathHandle::SetThrottle(float value) {
    throttle = value;
}

// FUNC_AT(0x0007ff00)
void RPathHandle::SetDesiredThrottle(float value) {
    desiredThrottle = value;
}

// FUNC_AT(0x0007ff10)
void RPathHandle::SetAcceleration(float value) {
    acceleration = value;
}

// FUNC_AT(0x0007ff20)
void RPathHandle::SetAccelDelay(unsigned updates) {
    accelDelay = updates;
}

// FUNC_AT(0x0007ff30)
void RPathHandle::SetParametricTime(float time) {
    if (master == NULL)
        parametricTime = time;
}

// FUNC_AT(0x0007ff50)
Coord3* RPathHandle::GetPosition() {
    return reinterpret_cast<Coord3 *>(instance->position);
}

// FUNC_AT(0x0007ff60)
void RPathHandle::GetOrientMat(MATRIX4 *out) {
    *out = instance->Matrix();
    out->mtx[0][3] = 0.0f;
    out->mtx[1][3] = 0.0f;
    out->mtx[2][3] = 0.0f;
    out->mtx[3][0] = 0.0f;
    out->mtx[3][1] = 0.0f;
    out->mtx[3][2] = 0.0f;
    out->mtx[3][3] = 1.0f;
}

// FUNC_AT(0x00080130)
void RPathHandle::Init(float time) {
    float now = float(PathTime(time));
    ProcAnimState *state = procAnim;
    currentPath = state->path;
    nextPath = state->path;
    velocity = ZeroVector;
    lastUpdateTime = now;
    parametricTime = state->startTime;
    throttle = 1.0f;
    acceleration = 1.0f;
    desiredThrottle = 1.0f;
    speed = state->speed;
    timeDelta = 0.0f;
    accelDelay = 0;
    running = (state->flags & kPathAnimRunning) != 0;
    if (state->master != NULL) {
        master = RPathEngine::GetPathHandle(state->master);
        if (master != NULL)
            timeDelta = master->parametricTime - parametricTime;
    } else {
        master = NULL;
    }
    if (state->untransformed)
        VU0_MATRIX4Init(&applyMatrix);
    else
        MatrixCopy(&kPathAxes, &applyMatrix);

    alignas(16) MATRIX4 frame;
    MatrixCopy(&baseMatrix, &frame);
    if (currentPath != NULL)
        currentPath->EvaluateMatrix(parametricTime, &positionKey, &rotationKey, frame.mtx[0], &weight);
    VU0_MATRIX4_mult(&frame, &applyMatrix, &frame);
    PoseInstance(this, &frame);
}

// ---------------------------------------------------------------------------------------------------------------
// The list

// FUNC_AT(0x00080070)
PathListNode** PathList::Erase(PathListNode **result, PathListNode *first, PathListNode *last) {
    while (first != last) {
        PathListNode *node = first;
        first = first->next;
        if (node != head) {
            node->prev->next = node->next;
            node->next->prev = node->prev;
            UMemory::FastFree(node, sizeof(PathListNode));
            size--;
        }
    }
    *result = first;
    return result;
}

// FUNC_AT(0x000800c0)
PathListNode* PathList::BuyHead() {
    PathListNode *node = static_cast<PathListNode *>(UMemory::FastAlloc(sizeof(PathListNode), "STL"));
    if (node != NULL) {
        node->next = node;
        node->prev = node;
    }
    return node;
}

// FUNC_AT(0x000800f0)
PathListNode* PathList::BuyNode(PathListNode *next, PathListNode *prev, const RPathHandle *value) {
    PathListNode *node = static_cast<PathListNode *>(UMemory::FastAlloc(sizeof(PathListNode), "STL"));
    if (node != NULL) {
        node->next = next;
        node->prev = prev;
        node->value.ConstructCopy(value);
    }
    return node;
}

// FUNC_AT(0x00080370)
void PathList::Destruct() {
    PathListNode *end;
    Erase(&end, Begin(), head);
    if (head != NULL)
        UMemory::FastFree(head, sizeof(PathListNode));
    head = NULL;
    size = 0;
}

// FUNC_AT(0x000803c0)
PathList* PathList::Construct() {
    head = BuyHead();
    size = 0;
    return this;
}

// FUNC_AT(0x00080430)
void PathList::IncreaseSize(uint32_t count) {
    if (kMaxSize - size < count) {
        PATHS_UNTESTED("list<RPathHandle>::_Incsize (too long)");
        ThrowLengthError("list<T> too long");
    }
    size += count;
}

// FUNC_AT(0x000804e0)
void PathList::PushBack(const RPathHandle *value) {
    PathListNode *node = BuyNode(head, head->prev, value);
    IncreaseSize(1);
    head->prev = node;
    node->prev->next = node;
}

// ---------------------------------------------------------------------------------------------------------------
// RPathEngine

// FUNC_AT(0x0007ffa0)
RPathHandle* RPathEngine::GetFirstPathHandle() {
    PathList *list = fgPathHandles;
    fgPathIterator = list->Begin();
    return fgPathIterator != list->head ? &fgPathIterator->value : NULL;
}

// FUNC_AT(0x0007ffc0)
RPathHandle* RPathEngine::GetNextPathHandle() {
    PathList *list = fgPathHandles;
    if (fgPathIterator == list->head)
        return NULL;
    fgPathIterator = fgPathIterator->next;
    return fgPathIterator != list->head ? &fgPathIterator->value : NULL;
}

// FUNC_AT(0x0007fff0)
void RPathEngine::Update(float time) {
    PathListNode *end = fgPathHandles->head;
    for (PathListNode *node = fgPathHandles->Begin(); node != end; node = node->next)
        node->value.Update(time);
}

// FUNC_AT(0x00080030)
RPathHandle* RPathEngine::GetPathHandle(const CARP::Instance *instance) {
    PathListNode *end = fgPathHandles->head;
    for (PathListNode *node = fgPathHandles->Begin(); node != end; node = node->next) {
        if (node->value.instance == instance)
            return &node->value;
    }
    return NULL;
}

// FUNC_AT(0x000802d0)
void RPathEngine::DestroyPathHandle(RPathHandle *handle) {
    PathList *list = fgPathHandles;
    if (list == NULL)
        return;
    for (PathListNode *node = list->Begin(); node != list->head; node = node->next) {
        if (&node->value == handle) {
            EraseNode(list, node);
            return;
        }
    }
}

// FUNC_AT(0x00080330)
void RPathEngine::Reset(float time) {
    for (PathListNode *node = fgPathHandles->Begin(); node != fgPathHandles->head; node = node->next)
        node->value.Init(time);
}

// FUNC_AT(0x000803e0)
void RPathEngine::Purge() {
    PathList *list = fgPathHandles;
    if (list == NULL)
        return;
    PathListNode *end;
    list->Erase(&end, list->Begin(), list->head);
    list = fgPathHandles;
    if (list != NULL) {
        list->Destruct();
        OperatorDelete(list);
    }
    fgPathHandles = NULL;
}

// FUNC_AT(0x00080520)
void RPathEngine::AddInstanceList(CARP::Instance *instances, ProcAnimState *states, uint32_t count, float time) {
    if (fgPathHandles == NULL)
        fgPathHandles = NewPathList();

    // The paths without a master first, so that the others find their masters' handles
    AddPaths(instances, states, count, false);
    AddPaths(instances, states, count, true);
    Reset(time);
}

// FUNC_AT(0x00080700)
RPathHandle* RPathEngine::CreatePathHandle(CARP::Instance *instance, ProcAnimState *state, float time) {
    if (!(instance->flags & kWorldInstanceProcAnim) || state->type != kPathAnimType || state->path == NULL)
        return NULL;
    if (fgPathHandles == NULL)
        fgPathHandles = NewPathList();
    RPathHandle handle;
    handle.Construct(instance, state);
    fgPathHandles->PushBack(&handle);
    RPathHandle *created = &fgPathHandles->head->prev->value;
    created->Init(time);
    const WorldArticle *article = ArticleOf(instance);
    if (article != NULL && article->model != NULL)
        created->userData = article->model->pathUserData;
    return &fgPathHandles->head->prev->value;
}

// ---------------------------------------------------------------------------------------------------------------
// The camera helpers

// FUNC_AT(0x00080820)
double WrapTurns(float turns) {
    double wrapped = turns;
    if (wrapped >= 1.0)
        wrapped -= 1.0f;
    if (wrapped <= -1.0)
        wrapped += 1.0f;
    if (wrapped < 0.0)
        wrapped += 1.0f;
    return wrapped;
}

// FUNC_AT(0x00080860)
double SinTurnsWrapped(float turns) {
    if (turns < 0.0f)
        turns = turns + 1.0f;
    return reinterpret_cast<UnroundedTurnsFn>(&sin_fractionalangle)(turns);
}

// FUNC_AT(0x00080890)
double TanTurnsWrapped(float turns) {
    if (turns < 0.0f)
        turns = turns + 1.0f;
    return reinterpret_cast<UnroundedTurnsFn>(&tan_fractionalangle)(turns);
}

// FUNC_AT(0x000808c0)
double HeadingTurns(float x, float z) {
    double heading = reinterpret_cast<UnroundedAtanFn>(&atan_turns)(x, z);
    if (heading < 0.0)
        heading += 1.0f;
    return heading;
}

// FUNC_AT(0x000808f0)
void RotateOffsetToHeading(const Coord4 *offset, const Coord4 *heading, Coord4 *out) {
    float up = offset->y;
    alignas(16) Coord4 forward;
    VU0_v4copy(heading, &forward);
    forward.y = 0.0f;
    VU0_v4unitxyz(&forward, &forward);
    // The original leaves the side's w uninitialised; the scalings below keep each destination's w
    alignas(16) Coord4 side = { -forward.z, 0.0f, forward.x, 0.0f };
    VU0_v4scale(&side, offset->x, &side);
    VU0_v4scaleadd(&forward, offset->z, &side, out);
    out->y = up;
}

// FUNC_AT(0x00080980)
void TurnFrameRows(MATRIX4 *frame) {
    alignas(16) Coord4 row1 = *MatrixRow(frame, 1);
    *MatrixRow(frame, 1) = *MatrixRow(frame, 2);
    VU0_v4scale4(&row1, -1.0f, MatrixRow(frame, 2));
}

// FUNC_AT(0x000809e0)
void FlipFrameRows(MATRIX4 *frame) {
    VU0_v4scale4(MatrixRow(frame, 0), -1.0f, MatrixRow(frame, 0));
    alignas(16) Coord4 row2;
    VU0_v4copy(MatrixRow(frame, 2), &row2);
    VU0_v4scale4(MatrixRow(frame, 1), -1.0f, MatrixRow(frame, 2));
    VU0_v4scale4(&row2, -1.0f, MatrixRow(frame, 1));
}

