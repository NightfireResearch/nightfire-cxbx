#ifndef DRIVING_WORLD_TARGETING_H_
#define DRIVING_WORLD_TARGETING_H_

// ---------------------------------------------------------------------------------------------------------------
// Targeting: WTargetable, a point the player's weapons can lock on to (a fixed point, or one following a physics
// object, an AI character or an AI vehicle), and WTargetPicker (one instance, at 0x0023e1b0), which keeps the
// registered targets, each frame sorts those near the player and on screen, picks one and drives the HUD's
// target cursor and lock. See Targeting.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "CollisionTypes.h"     // Coord3, Coord4
#include "SimpleZone.h"

class AICharacter;
class AIVehicle;
class ATargeting;
struct PhysicsObject;
struct WTargetable;

// A point on the screen, in pixels.
struct ScreenPos {
    float x, y;
};

// std::list<T *> as the game compiled it (MSVC 7): the allocator's byte, the head node (end()), the size. A node is
// 12 bytes: the links, then the value. The library code is shared by every list of pointers.
struct PointerListNode {
    PointerListNode *next;      // +0x00
    PointerListNode *prev;      // +0x04
    void *value;                // +0x08
};
static_assert(sizeof(PointerListNode) == 12, "a list node is 12 bytes");

struct PointerList {
    uint8_t allocator;          // +0x00
    uint8_t unknown01[3];
    PointerListNode *head;      // +0x04
    uint32_t size;              // +0x08

    // erase(first, last): unlinks and frees each node (never the head), answers `last`.
    PointerListNode** Erase(PointerListNode **result, PointerListNode *first, PointerListNode *last);   // 0x000ce900
    // _Incsize: throws length_error("list<T> too long") past 0x3fffffff nodes.
    void IncreaseSize(uint32_t count);                                                                  // 0x000ceca0
};
static_assert(sizeof(PointerList) == 12, "a list is 12 bytes");

// ---- WTargetable

// What a target follows (WTargetable::owner)
enum TargetOwnerType : int32_t {
    kTargetPoint = 0,           // a fixed position
    kTargetPhysicsObject = 1,
    kTargetCharacter = 2,       // an AI character
    kTargetVehicle = 3,         // an AI vehicle
};

// A target (0x50 bytes, from UMemory::FastAlloc "WTargetable"). Reference counted: freed when the last reference
// goes. WTargetPicker refreshes the registered ones every few frames (updateCountdown, updateInterval).
struct WTargetable {
    Coord3 position;            // +0x00
    uint8_t visible;            // +0x0c within 240 of the camera, nothing of the world between (UpdateVisibility)
    uint8_t unknown0d[3];
    ScreenPos screenPos;        // +0x10 on screen, or the screen's edge towards it
    uint8_t unknown18;          // +0x18 zero
    uint8_t unknown19[3];
    WSimpleZone zone;           // +0x1c its position's cell
    int32_t updateCountdown;    // +0x28 the picker refreshes the target when this comes to 0
    int32_t updateInterval;     // +0x2c the countdown's restart: 0 near the player, up to 6 away
    int32_t visibilityCountdown;    // +0x30 UpdatePosition rechecks the visibility when this comes to 0
    int32_t ownerType;          // +0x34 TargetOwnerType
    uint8_t isTemporary;        // +0x38 made by WTargetPicker::GetWorldTarget
    uint8_t unknown39[3];
    int32_t refCount;           // +0x3c
    uint8_t enabled;            // +0x40 one from construction
    uint8_t onScreen;           // +0x41
    uint8_t unknown42[2];
    union {                     // +0x44
        void *owner;
        PhysicsObject *physics;
        AICharacter *character;
        AIVehicle *vehicle;
    };
    uint8_t unknown48[8];

    WTargetable* Construct(float x, float y, float z);                          // 0x000cd930
    WTargetable* Construct(AICharacter *character);                             // 0x000cd9b0
    WTargetable* Construct(AIVehicle *vehicle);                                 // 0x000cda20
    WTargetable* Construct(PhysicsObject *physics);                             // 0x000cdba0
    void Destruct();                                                            // 0x000cd660

    void AddReference();                                                        // 0x000cd920
    void RemoveReference();     // deletes the target with its last reference  0x000cdb80

    // Follows the owner; rechecks the visibility every sixth call.
    void UpdatePosition();                                                      // 0x000cda90
    void UpdateVisibility();                                                    // 0x000cd670
    // The owner's velocity: false if the target cannot tell (a fixed point's, or an absent vehicle's, is zero).
    bool GetVelocity(Coord3 *velocity);                                         // 0x000cd740

    // The squared distance from the camera (CameraViews[camera]), answered as the x87 left it.
    float DistFromCamera(int camera);                                           // 0x000cd840
    // The squared distances on screen from a point and from the screen's centre.
    double DistFromScreenPos(const ScreenPos *point);                           // 0x000cd890
    double DistFromScreenCenter();                                              // 0x000cd8d0
};
static_assert(sizeof(WTargetable) == 0x50, "WTargetable is 80 bytes");

// ---- WTargetPicker

enum TargetingMode : int32_t {
    kTargetingNormal = 0,
    kTargetingAutoDrive = 1,    // UpdateTargets also runs UpdateAutoDriveTargeting
};

// The HUD's lock (GHud::SetTargetLockState)
enum TargetLockState : int32_t {
    kLockNone = 0,
    kLockTracking = 1,          // a target held on screen for up to 0.25 s
    kLockLocked = 2,            // held longer
};

// How UpdateTargets sorts the targets (its comparisons, by index)
enum TargetSortMode : int32_t {
    kSortByScreenCenter = 0,
    kSortByScreenPos = 1,       // from the aim's point on screen (the constructor's choice)
    kSortByCamera = 2,
};

constexpr int kMaxSortedTargets = 16;

class WTargetPicker {
public:
    Coord4 autoDriveQuat;       // +0x00 the auto-drive camera's turn towards a locked target
    Coord3 worldTarget;         // +0x10 where the aim meets the world
    int32_t lockStartTime;      // +0x1c the step the current target was taken
    ScreenPos worldTargetScreen;    // +0x20
    WTargetable *selected;      // +0x28
    PointerList *targets;       // +0x2c the registered targets (std::list<WTargetable *>)
    WTargetable *sorted[kMaxSortedTargets];   // +0x30 this frame's: near, on screen or not, sorted
    int32_t sortedCount;        // +0x70
    int32_t currentIndex;       // +0x74
    uint32_t unknown78;         // +0x78 zero
    ATargeting *targeting;      // +0x7c told each lock state (ATargeting::SetState)
    uint32_t unknown80;         // +0x80 zero
    uint32_t unknown84;
    uint32_t unknown88;
    ScreenPos cursor;           // +0x8c the HUD's target cursor
    uint32_t unknown94;
    uint32_t unknown98;
    uint8_t active;             // +0x9c
    uint8_t unknown9d[3];
    int32_t lockState;          // +0xa0 TargetLockState
    int32_t mode;               // +0xa4 TargetingMode
    int32_t sortMode;           // +0xa8 TargetSortMode
    uint32_t unknownAC;         // +0xac zero

    WTargetPicker* Construct();                                                 // 0x000cebc0

    void RegisterTarget(WTargetable *target);                                   // 0x000ced50
    void UnregisterTarget(WTargetable *target);                                 // 0x000ceb60

    void ActivateTargeting(int mode);                                           // 0x000ce0f0
    void DeactivateTargeting();                                                 // 0x000ce150
    void CycleToNextTarget();                                                   // 0x000ce170
    // The selected target if it is on screen, else NULL.
    WTargetable* GetSelectedTarget();                                           // 0x000cdfb0
    // A new temporary target at the world target.
    WTargetable* GetWorldTarget();                                              // 0x000cdfd0

    // Every frame: refreshes the registered targets, sorts this frame's, then (while active) the selection.
    void UpdateTargets();                                                       // 0x000ce950
    void UpdateSelection();                                                     // 0x000ce620
    void MoveTargetingCursors();                                                // 0x000ce410
    void UpdateAutoDriveTargeting();                                            // 0x000ce1b0
    void DrawTargetingSystem();                                                 // 0x000ce5a0

    // A world point on screen through the first camera view; (10000, 10000) for one behind the camera.
    ScreenPos* GetScreenPos(ScreenPos *result, const Coord3 *point);            // 0x000ce2b0
    // Whether a screen point is inside the box round the screen's centre targets count as on screen in.
    bool IsPointOnScreen(const ScreenPos *point);                               // 0x000cdee0
    // Where on that box's edge the direction to a world point leaves it.
    ScreenPos* GetOffScreenPos(ScreenPos *result, const Coord3 *point);         // 0x000cdc10

    // qsort's comparisons of two WTargetable pointers: nearer first.
    static int CompareDistFromScreenCenter(const void *a, const void *b);       // 0x000ce050
    static int CompareDistFromCamera(const void *a, const void *b);             // 0x000ce0a0
    static int CompareDistFromScreenPos(const void *a, const void *b);          // 0x000ce4f0
};
static_assert(sizeof(WTargetPicker) == 0xb0, "WTargetPicker is 176 bytes");

#define TargetPicker (*(WTargetPicker *)0x0023e1b0)   // the one picker (name ours)

#endif // DRIVING_WORLD_TARGETING_H_
