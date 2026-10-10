#include "Targeting.h"

#include "CollisionManager.h"
#include "../../common/xbeOverload.h"
#include "../../helpers.h"
#include "../engine/CoreFoundation.h"      // ThrowLengthError
#include "../engine/GameInterfaces.hpp"    // GHud
#include "../engine/MissionManager.h"
#include "../engine/UMemory.hpp"
#include "../camera/CameraSpline.h"     // RCameraMath
#include "../camera/PlayerCamera.h"     // RCamera, RViewCamera, RPlayerCamera
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../render/Renderer.h"
#include "../render/RenderHigh.h"

#include <bit>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// WTargetable and WTargetPicker (0x000cd660..0x000cedd0), and the std::list helpers between them, ported from the
// listings. See Targeting.h.
//
// The x87 code is ported bit for bit: what the original keeps on the x87 stack is computed in double in its order,
// rounded to float where it stores a float; comparisons keep their NaN behaviour.
// ---------------------------------------------------------------------------------------------------------------

namespace {

enum AIVehicleKind : int32_t {
    kAIVehicleRigidBody = 1,    // its physics object's body is a rigid body
    kAIVehicleSimpleBody = 2,   // ... a simple one
};

// An AI vehicle (only the fields read here)
struct AIVehicleFields {
    uint32_t vtable;
    uint8_t unknown04[0xc];
    int32_t kind;               // +0x10 AIVehicleKind
    uint8_t unknown14[0x54];
    uint8_t unknown68;          // +0x68 zero: GetVelocity answers zero
};
static_assert(offsetof(AIVehicleFields, unknown68) == 0x68, "AI vehicle layout");

// An AI character (only the fields read here)
struct AICharacterFields {
    uint32_t vtable;
    uint8_t unknown04[0x3c];
    Coord3 position;            // +0x40
};

struct WeaponSlot {
    int32_t weapon;             // +0x00
    uint8_t unknown04[0x50];
};
static_assert(sizeof(WeaponSlot) == 0x54, "a weapon slot is 84 bytes");

// The weapon manager (only the fields read here)
struct SWeaponManagerFields {
    uint32_t unknown00;
    int32_t current;            // +0x04 the current slot
    uint8_t unknown08[8];
    WeaponSlot *slots;          // +0x10
};

typedef int (*TargetCompare)(const void *a, const void *b);

}  // namespace

// ---- the game's globals

#define ViewWidth I32_AT(0x001f2d7c)                             // the view's size in pixels (names ours)
#define ViewHeight I32_AT(0x001f2d80)
#define DefaultVector (*(const Coord4 *)0x001d4c00)              // (0, 0, 0, 1)
#define TargetableCount I32_AT(0x0023e174)                       // the targets in existence (name ours)
#define TargetableStagger I32_AT(0x0023e178)                     // staggers new targets' first refresh (name ours)
#define Sim ((void *)0x00233ff0)                                 // the Simulation
#define SimState I32_AT(0x00234e24)
#define SimTimeStep FLOAT_AT(0x00234e30)                         // the simulation's step, in seconds
#define SimStepCount I32_AT(0x00234e34)
#define playerPhysicsObject (*(PVehicle ***)0x00234e40)
#define WeaponManager (*(SWeaponManagerFields **)0x0023923c)

// ---- calls to originals not ported

#define AIVehicle_GetPhysicsObject ((PhysicsObject *(__fastcall *)(AIVehicle *, int))0x00035840)
#define AIVehicle_GetPosition ((const Coord3 *(__fastcall *)(AIVehicle *, int))0x000359e0)
#define ATargeting_Construct ((ATargeting *(__fastcall *)(ATargeting *, int))0x0012e0f0)
#define ATargeting_SetState ((void (__fastcall *)(ATargeting *, int, int state))0x0012e120)
#define GHud_SetTarget ((void (__fastcall *)(GHud *, int, const ScreenPos *cursor))0x000d9f50)
#define GHud_SetTargetLockState ((void (__fastcall *)(GHud *, int, int state))0x000d9fb0)
#define PointerList_BuyHead ((PointerListNode *(__fastcall *)(PointerList *, int))0x000b8490)
#define Crt_qsort ((void (*)(void *base, size_t count, size_t size, TargetCompare compare))0x00132db0)

namespace {

constexpr int32_t kTargetableBytes = 0x50;
constexpr float kSightRange = 240.0f;               // UpdateVisibility's
constexpr float kSightStep = 16.0f;                 // passed to StepCheckHitWorld, which ignores it
constexpr float kCharacterTargetHeight = 0.2f;      // above a character's position
constexpr float kSimpleVehicleTargetHeight = 1.5f;  // above a simple-bodied AI vehicle's
constexpr int32_t kVisibilityInterval = 5;          // UpdatePosition's calls between visibility checks
constexpr int32_t kMaxUpdateInterval = 6;
constexpr int kZoneShift[2] = {5, 5};               // by TargetingMode: the range from the player's zone a
                                                    // target is refreshed in, 2^5 cells either way
constexpr float kHalf = 0.5f;
constexpr float kTwo = 2.0f;
// The box round the screen's centre targets count as on screen in, as the original computes its half sizes:
// (0.66 - 0.2) of the screen's width and (0.5 - 0.1) of its height, 107 pixels wider in widescreen.
constexpr float kBoxOuterX = 0.66f;
constexpr float kBoxInnerX = 0.2f;
constexpr float kBoxInnerY = 0.1f;
constexpr float kWidescreenWidening = 107.0f;
constexpr float kDegreesToRadians = 0.0174532924f;
constexpr float kNearZ = 0.01f;                     // GetScreenPos's: nearer (or behind) is off screen
constexpr float kBehindCamera = 10000.0f;
constexpr float kWidescreenAspect = 0.75f;
constexpr float kOffScreenDistance = 10000000.0f;   // CompareDistFromScreenPos's distance for a target off screen
constexpr float kAimDistance = 1000.0f;             // the auto-drive camera's aim, ahead of it
constexpr float kCarAimDistance = 175.0f;           // the car's, ahead of it
constexpr float kLockRadius = 130.0f;               // on screen, round the aim (pixels): before the lock
constexpr float kLockedRadius = 145.0f;             // ... once it is tracking or locked
constexpr float kLockTime = 0.25f;                  // seconds until a tracked target is locked
constexpr float kAutoDriveTurnRate = 0.05f;         // the auto-drive turn's slerp step
constexpr int32_t kWeapon1C = 0x1c;                 // the weapon auto-drive mode locks with
constexpr int32_t kMissionState3 = 3;
constexpr int32_t kMissionState4 = 4;
constexpr int32_t kSimState3 = 3;

static_assert(std::bit_cast<uint32_t>(kBoxOuterX) == 0x3f28f5c3, "0x00193b34");
static_assert(std::bit_cast<uint32_t>(kBoxInnerX) == 0x3e4ccccd, "0x00193b2c");
static_assert(std::bit_cast<uint32_t>(kBoxInnerY) == 0x3dcccccd, "0x00193b30");
static_assert(std::bit_cast<uint32_t>(kCharacterTargetHeight) == 0x3e4ccccd, "0x0018a498");
static_assert(std::bit_cast<uint32_t>(kDegreesToRadians) == 0x3c8efa35, "0x0019321c");
static_assert(std::bit_cast<uint32_t>(kNearZ) == 0x3c23d70a, "0x0018a00c");
static_assert(std::bit_cast<uint32_t>(kOffScreenDistance) == 0x4b189680, "0x00193b3c");
static_assert(std::bit_cast<uint32_t>(kAutoDriveTurnRate) == 0x3d4ccccd, "0x001ca34c");

const Coord4 kIdentityQuat = {0.0f, 0.0f, 0.0f, 1.0f};

// UpdateTargets' comparisons, by TargetSortMode (the table at 0x001ca35c)
const TargetCompare kSortCompares[] = {
    WTargetPicker::CompareDistFromScreenCenter,
    WTargetPicker::CompareDistFromScreenPos,
    WTargetPicker::CompareDistFromCamera,
};

// FPTAN: the x87's own tangent, which keeps 64 bits whatever the precision.
__declspec(naked) float Tangent(double radians) {
    __asm {
        fld qword ptr [esp + 4]
        fptan
        fstp st(0)
        ret
    }
}

const AIVehicleFields *Fields(const AIVehicle *vehicle) {
    return reinterpret_cast<const AIVehicleFields *>(vehicle);
}

AICharacterFields *Fields(AICharacter *character) {
    return reinterpret_cast<AICharacterFields *>(character);
}

// The player's vehicle's body (always a rigid body)
const RigidBody *PlayerBody() {
    return (*playerPhysicsObject)->GetRigidBody();
}

const CarPhysics *PlayerPhysics() {
    return (*playerPhysicsObject)->GetPhysics();
}

// AICharacter::IsAlive (its vtable's slot 1)
bool IsAlive(AICharacter *character) {
    typedef bool (AICharacterFields::*IsAliveMethod)();
    AICharacterFields *fields = Fields(character);
    return (fields->*XbeVirtual<IsAliveMethod>(fields, 1))();
}

int CurrentWeapon() {
    return WeaponManager->slots[WeaponManager->current].weapon;
}

// A qsort comparison's answer from the difference of the two distances (-1 for NaN)
int Order(double difference) {
    if (difference == 0.0)
        return 0;
    if (difference > 0.0)
        return 1;
    return -1;
}

// What every constructor sets
void InitTargetable(WTargetable *target, TargetOwnerType type, void *owner) {
    target->position.x = 0.0f;
    target->position.y = 0.0f;
    target->position.z = 0.0f;
    target->visible = 0;
    target->unknown18 = 0;
    target->zone.x = 0;
    target->zone.y = 0;
    target->zone.z = 0;
    target->updateCountdown = TargetableStagger++ & 3;
    target->updateInterval = 0;
    target->visibilityCountdown = kVisibilityInterval;
    target->ownerType = type;
    target->isTemporary = 0;
    target->refCount = 1;
    target->enabled = 1;
    target->owner = owner;
    TargetableCount++;
}

}  // namespace

// =============================================================================================================
// std::list<T *>'s shared code
// =============================================================================================================

// FUNC_AT(0x00013540)
void PointerList::Destruct() {
    PointerListNode *erased;
    Erase(&erased, Begin(), head);
    if (head != NULL)
        UMemory::FastFree(head, sizeof(PointerListNode));
    head = NULL;
    size = 0;
}

// FUNC_AT(0x000130e0)
PointerListNode* PointerList::BuyNode(PointerListNode *next, PointerListNode *prev, void *const *value) {
    PointerListNode *node = (PointerListNode *)UMemory::FastAlloc(sizeof(PointerListNode), "STL");
    if (node != NULL) {
        node->next = next;
        node->prev = prev;
        node->value = *value;
    }
    return node;
}

// FUNC_AT(0x000ce900)
PointerListNode** PointerList::Erase(PointerListNode **result, PointerListNode *first, PointerListNode *last) {
    while (first != last) {
        PointerListNode *node = first;
        first = first->next;
        if (node != head) {
            node->prev->next = node->next;
            node->next->prev = node->prev;
            UMemory::FastFree(node, sizeof(PointerListNode));
            size--;
        }
    }
    *result = first;
    return result;
}

// FUNC_AT(0x0011c7c0)
void PointerList::Remove(void *const &value) {
    PointerListNode *end = head;
    PointerListNode *node = end != NULL ? end->next : NULL;
    while (node != end) {
        if (node->value == value) {
            PointerListNode *erased = node;
            node = node->next;
            if (erased != head) {
                erased->prev->next = erased->next;
                erased->next->prev = erased->prev;
                UMemory::FastFree(erased, sizeof(PointerListNode));
                size--;
            }
        } else {
            node = node->next;
        }
    }
}

// FUNC_AT(0x000ceca0)
void PointerList::IncreaseSize(uint32_t count) {
    if (0x3fffffff - size < count)
        ThrowLengthError("list<T> too long");
    size += count;
}

// =============================================================================================================
// WTargetable
// =============================================================================================================

// FUNC_AT(0x000cd930)
WTargetable* WTargetable::Construct(float x, float y, float z) {
    InitTargetable(this, kTargetPoint, NULL);
    position.x = x;
    position.y = y;
    position.z = z;
    zone.SetPosition(&position);
    return this;
}

// FUNC_AT(0x000cd9b0)
WTargetable* WTargetable::Construct(AICharacter *character) {
    InitTargetable(this, kTargetCharacter, character);
    zone.SetPosition(&position);
    return this;
}

// FUNC_AT(0x000cda20)
WTargetable* WTargetable::Construct(AIVehicle *vehicle) {
    InitTargetable(this, kTargetVehicle, vehicle);
    zone.SetPosition(&position);
    return this;
}

// FUNC_AT(0x000cdba0)
WTargetable* WTargetable::Construct(PhysicsObject *physics) {
    InitTargetable(this, kTargetPhysicsObject, physics);
    UpdatePosition();
    return this;
}

// FUNC_AT(0x000cd660)
void WTargetable::Destruct() {
    TargetableCount--;
}

// FUNC_AT(0x000cd920)
void WTargetable::AddReference() {
    refCount++;
}

// FUNC_AT(0x000cdb80)
void WTargetable::RemoveReference() {
    if (--refCount == 0) {
        Destruct();
        UMemory::FastFree(this, kTargetableBytes);
    }
}

// FUNC_AT(0x000cda90)
void WTargetable::UpdatePosition() {
    switch (ownerType) {
    case kTargetPhysicsObject:
        if (physics == NULL)
            break;
        position = *physics->GetPosition();
        zone.SetPosition(&position);
        break;
    case kTargetCharacter:
        position = Fields(character)->position;
        position.y += kCharacterTargetHeight;
        zone.SetPosition(&position);
        break;
    case kTargetVehicle:
        position = *AIVehicle_GetPosition(vehicle, 0);
        if (Fields(vehicle)->kind == kAIVehicleSimpleBody)
            position.y += kSimpleVehicleTargetHeight;
        zone.SetPosition(&position);
        break;
    }
    if (visibilityCountdown < 1) {
        UpdateVisibility();
        visibilityCountdown = kVisibilityInterval;
    } else {
        visibilityCountdown--;
    }
}

// FUNC_AT(0x000cd670)
void WTargetable::UpdateVisibility() {
    if (fgRenderHigh == NULL || fgRenderHigh->views[0].camera == NULL)
        return;
    Coord4 sight[2];            // a segment from the camera to the target
    sight[0] = *MatrixRow(&fgRenderHigh->views[0].camera->matrix, 3);
    sight[1].x = position.x;
    sight[1].y = position.y;
    sight[1].z = position.z;
    sight[1].w = 1.0f;
    if (vec3distance(&sight[0], &sight[1]) <= kSightRange)
        visible = !fgCollisionMgr->StepCheckHitWorld(sight, kSightStep);
    else
        visible = 0;
}

// FUNC_AT(0x000cd740)
bool WTargetable::GetVelocity(Coord3 *velocity) {
    switch (ownerType) {
    case kTargetPoint:
        break;
    case kTargetPhysicsObject:
        if (physics == NULL)
            return false;
        *velocity = *physics->GetLinearVelocity();
        return true;
    case kTargetVehicle:
        if (vehicle == NULL || !Fields(vehicle)->unknown68)
            break;
        if (Fields(vehicle)->kind == kAIVehicleRigidBody) {
            *velocity = Simulation_GetRigidBody(Sim, 0, AIVehicle_GetPhysicsObject(vehicle, 0)->rigidBodySlot)->velocity;
            return true;
        }
        if (Fields(vehicle)->kind == kAIVehicleSimpleBody) {
            *velocity =
                Simulation_GetSimpleRigidBody(Sim, 0, AIVehicle_GetPhysicsObject(vehicle, 0)->rigidBodySlot)->velocity;
            return true;
        }
        return false;
    default:
        return false;
    }
    velocity->x = 0.0f;
    velocity->y = 0.0f;
    velocity->z = 0.0f;
    return true;
}

// FUNC_AT(0x000cd840)
float WTargetable::DistFromCamera(int camera) {
    return VU0_v3distancesquare(MatrixRow(&fgRenderHigh->views[camera].camera->matrix, 3), &position);
}

// FUNC_AT(0x000cd890)
double WTargetable::DistFromScreenPos(const ScreenPos *point) {
    double dx = double(screenPos.x) - point->x;
    double dy = double(screenPos.y) - point->y;
    return dy * dy + dx * dx;
}

// FUNC_AT(0x000cd8d0)
double WTargetable::DistFromScreenCenter() {
    double dx = screenPos.x - double(fgRenderer->screenWidth) * kHalf;
    double dy = screenPos.y - double(fgRenderer->screenHeight) * kHalf;
    return dy * dy + dx * dx;
}

// =============================================================================================================
// WTargetPicker
// =============================================================================================================

// FUNC_AT(0x000cebc0)
WTargetPicker* WTargetPicker::Construct() {
    sortedCount = 0;
    selected = NULL;
    unknown78 = 0;
    unknown80 = 0;
    sortMode = kSortByScreenPos;
    active = 0;
    lockState = kLockNone;
    unknownAC = 0;
    autoDriveQuat = kIdentityQuat;
    PointerList *list = static_cast<PointerList *>(OperatorNew(sizeof(PointerList)));
    if (list != NULL) {
        list->head = PointerList_BuyHead(list, 0);
        list->size = 0;
    }
    targets = list;
    ATargeting *sound = static_cast<ATargeting *>(OperatorNew(0xc));
    targeting = sound != NULL ? ATargeting_Construct(sound, 0) : NULL;
    return this;
}

// FUNC_AT(0x000ced50)
void WTargetPicker::RegisterTarget(WTargetable *target) {
    if (target == NULL)
        return;
    PointerListNode *head = targets->head;
    for (PointerListNode *node = head != NULL ? head->next : NULL; node != head; node = node->next) {
        if (node->value == target)
            return;
    }
    target->AddReference();
    PointerList *list = targets;
    PointerListNode *end = list->head;
    void *value = target;
    PointerListNode *node = list->BuyNode(end, end->prev, &value);
    list->IncreaseSize(1);
    end->prev = node;
    node->prev->next = node;
    target->UpdatePosition();
    target->UpdateVisibility();
}

// FUNC_AT(0x000ceb60)
void WTargetPicker::UnregisterTarget(WTargetable *target) {
    if (target == NULL)
        return;
    PointerList *list = targets;
    PointerListNode *head = list->head;
    for (PointerListNode *node = head != NULL ? head->next : NULL; node != targets->head; node = node->next) {
        if (node->value == target) {
            if (selected == target)
                selected = NULL;
            void *value = target;
            list->Remove(value);
            target->RemoveReference();
            return;
        }
    }
}

// FUNC_AT(0x000ce0f0)
void WTargetPicker::ActivateTargeting(int newMode) {
    if (active && mode == newMode)
        return;
    active = 1;
    lockState = kLockNone;
    selected = NULL;
    mode = newMode;
    autoDriveQuat = kIdentityQuat;
    unknown84 = 0;
    unknown88 = 0;
    unknown94 = 0;
    unknown98 = 0;
}

// FUNC_AT(0x000ce150)
void WTargetPicker::DeactivateTargeting() {
    if (active) {
        active = 0;
        selected = NULL;
        lockState = kLockNone;
    }
}

// FUNC_AT(0x000ce170)
void WTargetPicker::CycleToNextTarget() {
    if (sortedCount <= 0) {
        selected = NULL;
        return;
    }
    if (++currentIndex >= sortedCount)
        currentIndex = 0;
    if (!sorted[currentIndex]->onScreen)
        currentIndex = 0;
    selected = sorted[currentIndex];
}

// FUNC_AT(0x000cdfb0)
WTargetable* WTargetPicker::GetSelectedTarget() {
    if (selected == NULL || !selected->onScreen)
        return NULL;
    return selected;
}

// FUNC_AT(0x000cdfd0)
WTargetable* WTargetPicker::GetWorldTarget() {
    WTargetable *target = static_cast<WTargetable *>(UMemory::FastAlloc(kTargetableBytes, "WTargetable"));
    if (target != NULL)
        target = target->Construct(worldTarget.x, worldTarget.y, worldTarget.z);
    target->isTemporary = 1;
    return target;
}

// FUNC_AT(0x000ce950)
void WTargetPicker::UpdateTargets() {
    WSimpleZone playerZone;
    playerZone.SetPosition(&PlayerBody()->position);

    int count = 0;
    PointerListNode *head = targets->head;
    for (PointerListNode *node = head != NULL ? head->next : NULL; node != targets->head; node = node->next) {
        WTargetable *target = static_cast<WTargetable *>(node->value);
        if (--target->updateCountdown < 0)
            target->updateCountdown = target->updateInterval;
        if (target->updateCountdown != 0)
            continue;
        if (target->ownerType == kTargetPhysicsObject) {
            if (!(target->physics->GetHitPoints() > 0.0))
                continue;
        } else if (target->ownerType == kTargetCharacter) {
            if (!IsAlive(target->character))
                continue;
        }
        target->UpdatePosition();

        if (!playerZone.IsZoneInRange(&target->zone, kZoneShift[mode])) {
            if (target->updateInterval < kMaxUpdateInterval)
                target->updateInterval++;
            continue;
        }
        target->updateCountdown = 0;
        target->updateInterval = 0;
        ScreenPos screen;
        target->screenPos = *GetScreenPos(&screen, &target->position);
        bool onScreen = IsPointOnScreen(&target->screenPos);
        if (target->enabled && target->visible) {
            target->onScreen = onScreen;
            if (!onScreen)
                target->screenPos = *GetOffScreenPos(&screen, &target->position);
            sorted[count++] = target;
            if (count >= kMaxSortedTargets)
                break;
        }
    }

    sortedCount = count;
    if (count > 0)
        Crt_qsort(sorted, count, sizeof(sorted[0]), kSortCompares[sortMode]);

    SMissionManager *missions = glbMissionManager;
    bool suspended = missions->unknown474 == kMissionState3 || missions->unknown474 == kMissionState4 ||
                     SimState == kSimState3 || missions->unknown4f0 != 0;
    if (active && !suspended) {
        UpdateSelection();
        MoveTargetingCursors();
        if (mode == kTargetingAutoDrive)
            UpdateAutoDriveTargeting();
    }
}

// FUNC_AT(0x000ce620)
void WTargetPicker::UpdateSelection() {
    if (mode == kTargetingAutoDrive)
        selected = NULL;
    if (selected != NULL) {
        bool listed = false;
        for (int i = 0; i < sortedCount; i++) {
            if (sorted[i] == selected) {
                listed = true;
                break;
            }
        }
        if (!listed)
            selected = NULL;
    }

    // the world target: along the auto-drive camera's aim, or ahead of the car, up to the world in the way
    Coord4 aim[2];              // a segment from the camera or the car to the target
    if (mode == kTargetingAutoDrive) {
        RPlayerCamera *camera = fgRenderHigh->views[0].camera;
        aim[0] = *MatrixRow(&camera->matrix, 3);
        VU0_v4scaleadd(camera->GetForwardAimVec4(1.0f), kAimDistance, &aim[0], &worldTarget);
    } else {
        const RigidBody *body = PlayerBody();
        aim[0].x = body->position.x;
        aim[0].y = body->position.y;
        aim[0].z = body->position.z;
        aim[0].w = 1.0f;
        VU0_v4scaleadd(MatrixRow(&body->info->orientation, 2), kCarAimDistance, &aim[0], &worldTarget);
    }
    aim[1].x = worldTarget.x;
    aim[1].y = worldTarget.y;
    aim[1].z = worldTarget.z;
    aim[1].w = 1.0f;
    if (PlayerPhysics()->subPhysics != 0) {
        WorldCollisionInfo hit = {};    // (WorldCollisionInfo's constructor, inlined)
        hit.point = DefaultVector;
        hit.segmentStart = aim[0];
        hit.segmentEnd = aim[1];
        if (fgCollisionMgr->CheckHitWorld(aim, &hit) > 0 && !(hit.faceInstance->flags & kInstanceFlag02)) {
            worldTarget.x = hit.point.x;
            worldTarget.y = hit.point.y;
            worldTarget.z = hit.point.z;
        }
    }

    if (PlayerPhysics()->isSub != 0) {
        worldTargetScreen.x = float(double(ViewWidth) * kHalf);
        worldTargetScreen.y = float(double(ViewHeight) * kHalf);
    } else {
        ScreenPos screen;
        worldTargetScreen = *GetScreenPos(&screen, &worldTarget);
    }

    if (sortedCount > 0) {
        currentIndex = 0;
        selected = sorted[0];
        if (selected != NULL && (mode == kTargetingAutoDrive || PlayerPhysics()->isSub != 0)) {
            float radius = lockState == kLockNone ? kLockRadius : kLockedRadius;
            if (double(radius) * radius < selected->DistFromScreenPos(&worldTargetScreen))
                selected = NULL;
        }
    }
}

// FUNC_AT(0x000ce410)
void WTargetPicker::MoveTargetingCursors() {
    bool canLock = mode != kTargetingAutoDrive || CurrentWeapon() == kWeapon1C;
    if (canLock && selected != NULL && selected->onScreen) {
        int32_t held = SimStepCount - lockStartTime;
        lockState = held * double(SimTimeStep) > kLockTime ? kLockLocked : kLockTracking;
        ATargeting_SetState(targeting, 0, lockState);
        cursor = selected->screenPos;
        return;
    }
    lockStartTime = SimStepCount;
    lockState = kLockNone;
    ATargeting_SetState(targeting, 0, kLockNone);
    ScreenPos screen;
    cursor = *GetScreenPos(&screen, &worldTarget);
}

// FUNC_AT(0x000ce1b0)
void WTargetPicker::UpdateAutoDriveTargeting() {
    RPlayerCamera *camera = fgRenderHigh->views[0].camera;
    if (camera->CameraAiming() == 1) {
        autoDriveQuat = kIdentityQuat;
        return;
    }
    if (lockState == kLockLocked) {
        // the turn from the camera's frame towards the target, approached a step at a time
        Coord4 target = {selected->position.x, selected->position.y, selected->position.z, 0.0f};
        Coord4 direction = {};
        VU0_v4sub(&target, MatrixRow(&camera->matrix, 3), &direction);
        MATRIX4 transposed;
        VU0_MATRIX4_transpose(&transposed, &camera->matrix);
        VU0_MATRIX4_vect3rotate(&direction, &transposed, &direction);
        Coord4 turn;
        RCameraMath::VU0_GenerateQuat(&direction, &turn);
        VU0_fastqslerp(&autoDriveQuat, &turn, &autoDriveQuat, kAutoDriveTurnRate);
    } else {
        Coord4 identity = kIdentityQuat;
        VU0_fastqslerp(&autoDriveQuat, &identity, &autoDriveQuat, kAutoDriveTurnRate);
    }
}

// FUNC_AT(0x000ce5a0)
void WTargetPicker::DrawTargetingSystem() {
    GHud_SetTargetLockState(GHud::TheApp(), 0, lockState);
    if (!active)
        return;
    if (mode == kTargetingAutoDrive) {
        GHud_SetTarget(GHud::TheApp(), 0, &cursor);
        GHud::TheApp();         // (called again, its answer unused)
    } else if (mode == kTargetingNormal) {
        ScreenPos screen;
        TargetPicker.GetScreenPos(&screen, &TargetPicker.worldTarget);
        GHud_SetTarget(GHud::TheApp(), 0, &screen);
    }
}

// FUNC_AT(0x000ce2b0)
ScreenPos* WTargetPicker::GetScreenPos(ScreenPos *result, const Coord3 *point) {
    RViewCamera *view = fgRenderHigh->views[0].view;
    RCamera camera;
    camera.ConstructCopy(view->camera);
    float tangent = Tangent(double(fgRenderer->fieldOfViewScale) * camera.fieldOfView * kDegreesToRadians);
    camera.CreateMatrix4Inv();
    Coord4 local;
    MATRIX4_TransformPoint(&camera.inverse, point, &local);
    if (local.z <= kNearZ) {
        result->x = kBehindCamera;
        result->y = kBehindCamera;
        return result;
    }
    double aspect = view->AspectRatio();
    if (fgRenderer->widescreen)
        aspect *= kWidescreenAspect;
    double depth = fabs(local.z);
    float width = float(ViewWidth);
    float x = float(local.x / (double(kTwo) / width * depth * tangent));
    float height = float(ViewHeight);
    double y = aspect * (local.y / (double(kTwo) / height * depth * tangent));
    result->x = float(width * double(kHalf) + x);
    result->y = float(height * double(kHalf) - y);
    return result;
}

// FUNC_AT(0x000cdee0)
bool WTargetPicker::IsPointOnScreen(const ScreenPos *point) {
    double width = fgRenderer->screenWidth;
    float halfWidth = float((double(kBoxOuterX) - kBoxInnerX) * width);
    float height = float(fgRenderer->screenHeight);
    float halfHeight = float((double(kHalf) - kBoxInnerY) * height);
    if (fgRenderer->widescreen)
        halfWidth = float(halfWidth + double(kWidescreenWidening));
    float dx = float(point->x - width * kHalf);
    double dy = point->y - double(height) * kHalf;
    if (dy < -halfHeight || !(dy <= halfHeight))
        return false;
    return -halfWidth <= dx && dx <= halfWidth;
}

// FUNC_AT(0x000cdc10)
ScreenPos* WTargetPicker::GetOffScreenPos(ScreenPos *result, const Coord3 *point) {
    // the point in the camera's frame: its rotation transposed, the translation carried into it
    const MATRIX4 &frame = fgRenderHigh->views[0].view->camera->matrix;
    MATRIX4 view;
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++)
            view.mtx[row][column] = frame.mtx[column][row];
        view.mtx[row][3] = 0.0f;
    }
    view.mtx[3][0] = 0.0f;
    view.mtx[3][1] = 0.0f;
    view.mtx[3][2] = 0.0f;
    view.mtx[3][3] = 1.0f;
    Coord4 origin = {-frame.mtx[3][0], -frame.mtx[3][1], -frame.mtx[3][2], 0.0f};
    VU0_MATRIX4_vect4mult(&origin, &view, view.mtx[3]);
    view.mtx[3][3] = 1.0f;
    Coord4 direction = {point->x, point->y, point->z, 1.0f};
    VU0_MATRIX4_vect4mult(&direction, &view, &direction);
    direction.z = 0.0f;
    VU0_v4unitxyz(&direction, &direction);

    // the direction from the screen's centre, then pulled in onto the box's edge
    double width = fgRenderer->screenWidth;
    double right = double(direction.x) + kHalf;
    double down = double(kHalf) - direction.y;
    float widthF = float(width);
    float x = float(width * right);
    double height = fgRenderer->screenHeight;
    float heightF = float(height);
    float y = float(height * down);
    float halfWidth = float((double(kBoxOuterX) - kBoxInnerX) * widthF);
    float centreX = float(widthF * double(kHalf));
    float halfHeight = float((double(kHalf) - kBoxInnerY) * heightF);
    float centreY = float(heightF * double(kHalf));

    double left = double(centreX) - halfWidth;
    if (x < left) {
        y = float((double(y) - centreY) * halfWidth / (double(centreX) - x) + centreY);
        x = float(left);
    } else {
        double rightEdge = double(centreX) + halfWidth;
        if (x > rightEdge) {
            y = float((double(y) - centreY) * halfWidth / (double(x) - centreX) + centreY);
            x = float(rightEdge);
        }
    }
    double top = double(centreY) - halfHeight;
    if (y < top) {
        x = float((double(x) - centreX) * halfHeight / (double(centreY) - y) + centreX);
        y = float(top);
    } else {
        double bottom = double(centreY) + halfHeight;
        if (y > bottom) {
            x = float((double(x) - centreX) * halfHeight / (double(y) - centreY) + centreX);
            y = float(bottom);
        }
    }
    result->x = x;
    result->y = y;
    return result;
}

// FUNC_AT(0x000ce050)
int WTargetPicker::CompareDistFromScreenCenter(const void *a, const void *b) {
    WTargetable *first = *static_cast<WTargetable *const *>(a);
    WTargetable *second = *static_cast<WTargetable *const *>(b);
    float distance = float(first->DistFromScreenCenter());
    return Order(distance - second->DistFromScreenCenter());
}

// FUNC_AT(0x000ce0a0)
int WTargetPicker::CompareDistFromCamera(const void *a, const void *b) {
    WTargetable *first = *static_cast<WTargetable *const *>(a);
    WTargetable *second = *static_cast<WTargetable *const *>(b);
    float distance = first->DistFromCamera(0);
    return Order(double(distance) - second->DistFromCamera(0));
}

// FUNC_AT(0x000ce4f0)
int WTargetPicker::CompareDistFromScreenPos(const void *a, const void *b) {
    WTargetable *first = *static_cast<WTargetable *const *>(a);
    WTargetable *second = *static_cast<WTargetable *const *>(b);
    ScreenPos aim;
    TargetPicker.GetScreenPos(&aim, &TargetPicker.worldTarget);
    float distance = first->onScreen ? float(first->DistFromScreenPos(&aim)) : kOffScreenDistance;
    double other = second->onScreen ? second->DistFromScreenPos(&aim) : kOffScreenDistance;
    return Order(distance - other);
}
