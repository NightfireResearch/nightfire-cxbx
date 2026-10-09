#ifndef DRIVING_RENDER_PATHENGINE_H_
#define DRIVING_RENDER_PATHENGINE_H_

// ---------------------------------------------------------------------------------------------------------------
// The path engine (0x0007f9f0..0x00080820): instances moved along CARP paths (CARP::PathInfo). WWorld::Open hands
// it the track's instances, and each whose proc-anim state names a path gets a path handle (RPathHandle, Ghidra's
// RPathEngine::RPathHandle) in one std::list, fgPathHandles; the AI's spline paths make their own
// (CreatePathHandle). Every update moves each running handle's parametric time on by its speed and throttle, wraps
// it into the next path at the end of one, and poses its instance (and scene object) at the path's matrix there.
// A handle with a master keeps a fixed time behind its master's instead.
//
// After it the linker put a few helpers the cameras call: turns brought into range, unrounded sine and tangent of
// them, a heading, an offset turned to a heading, two row shuffles of a frame and a navigator's lane count.
// See PathEngine.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/Carp.h"               // CARP::Instance, CARP::PathInfo; MATRIX4
#include "../data/CoordConvert.h"       // Coord3, Coord4

class RSceneObj;
struct ProcAnimState;                   // world/World.h: a path's is of type kPathAnimType

// The type whose two references (path, master) the CARP resolver resolves
constexpr uint8_t kPathAnimType = 2;

enum PathAnimFlags : uint8_t {
    kPathAnimRunning = 0x80,            // the path starts running
};

// ---- RPathHandle (0xd4)

struct RPathHandle {
    CARP::Instance *instance;           // +0x00 the instance the path moves
    ProcAnimState *procAnim;            // +0x04
    MATRIX4 baseMatrix;                 // +0x08 the instance's frame at construction (the fourth column cleared):
                                        //       the path's matrix is evaluated over it
    MATRIX4 applyMatrix;                // +0x48 then multiplied by this: the identity or kPathAxes
    Coord3 velocity;                    // +0x88 the last update's, per second
    float lastUpdateTime;               // +0x94 in 60ths of a second
    CARP::PathInfo *currentPath;        // +0x98
    CARP::PathInfo *nextPath;           // +0x9c taken at the end of the current one
    RPathHandle *master;                // +0xa0 the handle whose time this one keeps behind; NULL none
    float speed;                        // +0xa4 parametric time per 60th of a second at full throttle
    float timeDelta;                    // +0xa8 with a master, how far behind it; else the last update's step
    float parametricTime;               // +0xac
    uint32_t positionKey;               // +0xb0 EvaluateMatrix's search hints
    uint32_t rotationKey;               // +0xb4
    float weight;                       // +0xb8 the position channel's fourth component
    float throttle;                     // +0xbc
    float acceleration;                 // +0xc0 the throttle's step towards desiredThrottle per update
    float desiredThrottle;              // +0xc4
    int32_t accelDelay;                 // +0xc8 updates before the throttle starts to move
    bool running;                       // +0xcc
    uint8_t padCD[3];
    const void *userData;               // +0xd0 "" at construction; CreatePathHandle's, its model's word at +0x34

    // The copy constructor: every field but the search hints, which start again at 0
    RPathHandle* ConstructCopy(const RPathHandle *other);                                       // 0x0007f9f0
    RPathHandle* Construct(CARP::Instance *instance, ProcAnimState *state);                     // 0x0007fb00
    // Starts the path again at the state's start time (WWorld::Reset, AISplinePath::Reset)
    void Init(float time);                                                                      // 0x00080130
    void Update(float time);                                                                    // 0x0007fbc0
    void SetNextPath(CARP::PathInfo *path);                                                     // 0x0007fec0
    float GetParametricDuration();                                                              // 0x0007fed0
    void SetRunning(bool on);                                                                   // 0x0007fee0
    void SetThrottle(float value);                                                              // 0x0007fef0
    void SetDesiredThrottle(float value);                                                       // 0x0007ff00
    void SetAcceleration(float value);                                                          // 0x0007ff10
    void SetAccelDelay(unsigned updates);                                                       // 0x0007ff20
    void SetParametricTime(float time); // not with a master                                    // 0x0007ff30
    Coord3* GetPosition();              // the instance's                                       // 0x0007ff50
    void GetOrientMat(MATRIX4 *out);    // the instance's axes, without the position            // 0x0007ff60
};
static_assert(sizeof(RPathHandle) == 0xd4, "a path handle is 0xd4 bytes");
static_assert(offsetof(RPathHandle, velocity) == 0x88 && offsetof(RPathHandle, master) == 0xa0 &&
              offsetof(RPathHandle, positionKey) == 0xb0 && offsetof(RPathHandle, running) == 0xcc &&
              offsetof(RPathHandle, userData) == 0xd0, "path handle layout");

// ---- std::list<RPathHandle> as the game compiled it (MSVC 7): the allocator's byte, the head node (end()), the
//      size; a node is the links, then the value

struct PathListNode {
    PathListNode *next;                 // +0x00
    PathListNode *prev;                 // +0x04
    RPathHandle value;                  // +0x08
};
static_assert(sizeof(PathListNode) == 0xdc, "a path list node is 0xdc bytes");

struct PathList {
    static constexpr uint32_t kMaxSize = 0xffffffffu / sizeof(RPathHandle);

    uint8_t allocator;                  // +0x00
    uint8_t unknown01[3];
    PathListNode *head;                 // +0x04
    uint32_t size;                      // +0x08

    PathList* Construct();                                                                      // 0x000803c0
    void Destruct();                    // every node erased, the head freed                    // 0x00080370
    PathListNode* BuyHead();            // the head, linked to itself                           // 0x000800c0
    PathListNode* BuyNode(PathListNode *next, PathListNode *prev, const RPathHandle *value);    // 0x000800f0
    // erase(first, last); answers last
    PathListNode** Erase(PathListNode **result, PathListNode *first, PathListNode *last);       // 0x00080070
    void IncreaseSize(uint32_t count);  // _Incsize: length_error past kMaxSize                 // 0x00080430
    void PushBack(const RPathHandle *value);                                                    // 0x000804e0

    // begin(), as the compiled code reads it: NULL without a head
    PathListNode* Begin() const { return head != NULL ? head->next : NULL; }
};
static_assert(sizeof(PathList) == 0xc, "a list is 12 bytes");

// ---- RPathEngine: the handles, all static

class RPathEngine {
public:
    // The handles in order; the iterator is a global (fgPathIterator)
    static RPathHandle* GetFirstPathHandle();                                                   // 0x0007ffa0
    static RPathHandle* GetNextPathHandle();                                                    // 0x0007ffc0
    static void Update(float time);                                                             // 0x0007fff0
    // The handle that moves `instance`, NULL none
    static RPathHandle* GetPathHandle(const CARP::Instance *instance);                          // 0x00080030
    static void DestroyPathHandle(RPathHandle *handle);                                         // 0x000802d0
    // Every handle's Init
    static void Reset(float time);                                                              // 0x00080330
    // Every handle, and the list, freed
    static void Purge();                                                                        // 0x000803e0
    // A handle for each instance whose proc-anim state names a path: those without a master first
    static void AddInstanceList(CARP::Instance *instances, ProcAnimState *states, uint32_t count,
                                float time);                                                    // 0x00080520
    static RPathHandle* CreatePathHandle(CARP::Instance *instance, ProcAnimState *state, float time);  // 0x00080700
};

#define fgPathHandles (*(PathList **)0x001ec300)
#define fgPathIterator (*(PathListNode **)0x001ec308)

// ---- the helpers after it (the names are ours)

// The turns less one from 1 up, plus one to -1 and below, then plus one if negative; unrounded
double WrapTurns(float turns);                                                                  // 0x00080820
// sin and tan of the turns, plus one if negative: FSIN's and FPTAN's results, unrounded
double SinTurnsWrapped(float turns);                                                            // 0x00080860
double TanTurnsWrapped(float turns);                                                            // 0x00080890
// atan_turns(x, z), plus one if negative: in [0, 1), unrounded
double HeadingTurns(float x, float z);                                                          // 0x000808c0
// The offset's z along the heading flattened to the ground, its x square to it, its y kept
void RotateOffsetToHeading(const Coord4 *offset, const Coord4 *heading, Coord4 *out);           // 0x000808f0
// Row 1 = row 2, row 2 = -row 1
void TurnFrameRows(MATRIX4 *frame);                                                             // 0x00080980
// Row 0 = -row 0, row 1 = -row 2, row 2 = -row 1
void FlipFrameRows(MATRIX4 *frame);                                                             // 0x000809e0

#endif // DRIVING_RENDER_PATHENGINE_H_
