#ifndef DRIVING_CAMERA_DIRECTORQUEUE_H_
#define DRIVING_CAMERA_DIRECTORQUEUE_H_

// ---------------------------------------------------------------------------------------------------------------
// RDirectorQueue: the player camera's queue of mode changes (RDirectorQueueData, a std::list). The camera appends
// one for each change it is asked for (SetCameraModeByIndex, the missile and animation cameras); each update
// ProcessDirectorLogic drops the changes a later one supersedes, counts down the front one's delay and then hands it
// to the camera. Also two renderer pieces the linker placed after it (the end of this file).
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "CameraSpline.h"               // CameraList
#include "../data/CoordConvert.h"       // Coord4

class RPlayerCamera;
class RSceneObj;
struct PhysicsObject;

namespace CARP {
class Instance;
}

namespace EAGL {
struct RenderMethod;
}

// A mode change (0x28 bytes)
class RDirectorQueueData {
public:
    enum Flag : uint16_t {
        kKeep = 0x04,                   // not dropped for a change queued after it
        kOwnsData18 = 0x08,             // data18 is deleted with it
        kKeepPoints = 0x10,             // an animation mode keeps the spline's points
        kOwnsData1C = 0x20,             // data1C is deleted with it
        kFromList = 0x40,               // an animation mode plays the anchor's animation unknown24
        kKeep260 = 0x80,                // RPlayerCamera::unknown260 is left as it is
    };

    Coord4 position;                    // +0x00 (0, 0, 0, 1) unless given
    uint16_t delay;                     // +0x10 updates to wait before it is made
    uint16_t cameraMode;                // +0x12
    uint16_t unknown14;                 // +0x14
    uint16_t flags;                     // +0x16 Flag
    CARP::Instance *data18;             // +0x18 an animation mode's 'Cams' instance
    void *data1C;                       // +0x1c
    PhysicsObject *anchor;              // +0x20 non-NULL: RPlayerCamera::DirectorSetAnchor first
    uint32_t unknown24;                 // +0x24

    RDirectorQueueData* Construct(uint16_t delay, uint16_t cameraMode, uint16_t unknown14, uint16_t flags,
                                  CARP::Instance *data18, PhysicsObject *anchor, uint32_t unknown24);    // 0x0007c160
    RDirectorQueueData* ConstructCopy(const RDirectorQueueData *other);                         // 0x0007c1c0
    void Destruct();                    // every field cleared, the position (0, 0, 0, 1)        // 0x0007c220
};
static_assert(sizeof(RDirectorQueueData) == 0x28, "a director change is 40 bytes");
static_assert(offsetof(RDirectorQueueData, delay) == 0x10 && offsetof(RDirectorQueueData, data18) == 0x18 &&
              offsetof(RDirectorQueueData, unknown24) == 0x24, "director change layout");

// The queue's node: its value is destroyed by a method of the node's own
template <>
struct CameraListNode<RDirectorQueueData> {
    CameraListNode *next;               // +0x00
    CameraListNode *prev;               // +0x04
    RDirectorQueueData value;           // +0x08

    void DestroyValue();                // value.Destruct() (name ours)                        // 0x0007c250
};
typedef CameraListNode<RDirectorQueueData> DirectorQueueNode;
static_assert(sizeof(DirectorQueueNode) == 0x30, "a queue node is 48 bytes");

// The queue's nodes hold copies, destroyed as they go
inline void ConstructListValue(RDirectorQueueData *slot, const RDirectorQueueData *value) {
    slot->ConstructCopy(value);
}
inline void DestroyListValue(DirectorQueueNode *node) {
    node->DestroyValue();
}

class RDirectorQueue {
public:
    enum Flag : uint8_t {
        kHeld = 0x01,                   // nothing is taken from the queue while set
        kRestart = 0x02,                // RestartDirectorQueue's: back to the default camera
    };

    // std::list<RDirectorQueueData>
    struct Queue : CameraList<RDirectorQueueData> {
        DirectorQueueNode* BuyNode(DirectorQueueNode *next, DirectorQueueNode *prev,
                                   const RDirectorQueueData *value);                            // 0x0007c280
        DirectorQueueNode* BuyHead();                                                           // 0x0007c2c0
        DirectorQueueNode** Erase(DirectorQueueNode **result, DirectorQueueNode *first,
                                  DirectorQueueNode *last);                                     // 0x0007c2e0
        void PopFront();                                                                        // 0x0007c340
        void IncreaseSize(uint32_t count);                                                      // 0x0007c5f0
    };
    static_assert(sizeof(Queue) == 0xc, "a list is 12 bytes");

    Queue queue;                        // +0x00
    RPlayerCamera *camera;              // +0x0c
    uint8_t flags;                      // +0x10 Flag
    uint8_t pad11[3];

    RDirectorQueue* Construct(RPlayerCamera *camera);                                           // 0x0007c380
    void Destruct();                                                                            // 0x0007c3b0
    void ProcessDirectorLogic();                                                                // 0x0007c3f0
    void RestartDirectorQueue();                                                                // 0x0007c5e0
    void AppendData(const RDirectorQueueData *data);                                            // 0x0007c6a0
};
static_assert(sizeof(RDirectorQueue) == 0x14, "RDirectorQueue is 20 bytes");

// ---- placed here by the linker: renderer pieces

// The three objects at 0x001ebdb8 (0x4c bytes each; a static initialiser constructs them): a child of the
// "TextureDOF" render method (0x00241460) and the two variables it is given (name ours).
struct TextureDofState {
    struct Variable {
        uint32_t unknown00;             // 1
        void *value;                    // DynamicLoader::GetRegisteredVar's
    };

    EAGL::RenderMethod *method;         // +0x00
    uint32_t unknown04[8];              // +0x04 zero
    Variable modelViewProjection;       // +0x24 "EAGL::ViewPort::gpModelViewProjectionMatrix"
    Variable dofOffset;                 // +0x2c "dofoffset"
    uint32_t unknown34[6];              // +0x34 zero

    TextureDofState* Construct();                                                               // 0x0007c6e0
    void Destruct();                                                                            // 0x0007c7b0
};
static_assert(sizeof(TextureDofState) == 0x4c, "the DOF state is 0x4c bytes");

// RDrawGroup's reverse draw list ("RReverseDrawList", 0x10 bytes): a std::vector of the translucent instances a
// view's draw keeps for its end (DrawInstance's), with its compiled members.
struct ReverseDrawEntry {
    MATRIX4 transform;                  // +0x00
    CARP::Instance *instance;           // +0x40
    RSceneObj *sceneObj;                // +0x44
    uint32_t distance;                  // +0x48 the view distance * 65536
    uint32_t unknown4C;                 // +0x4c never written: DrawInstance's stack
};
static_assert(sizeof(ReverseDrawEntry) == 0x50, "a reverse draw entry is 80 bytes");
static_assert(offsetof(ReverseDrawEntry, instance) == 0x40 && offsetof(ReverseDrawEntry, distance) == 0x48,
              "ReverseDrawEntry layout");

struct ReverseDrawList {
    uint32_t allocator;                 // +0x00
    ReverseDrawEntry *first;            // +0x04
    ReverseDrawEntry *last;             // +0x08
    ReverseDrawEntry *end;              // +0x0c

    uint32_t Size();                                                                            // 0x0007c7d0
    void Deallocate(ReverseDrawEntry *block, uint32_t count);   // the allocator's               // 0x0007c800
    ReverseDrawEntry* Ucopy(ReverseDrawEntry *from, ReverseDrawEntry *to, ReverseDrawEntry *dest);   // 0x0007c930
    ReverseDrawEntry* Ufill(ReverseDrawEntry *dest, uint32_t count, const ReverseDrawEntry *value);   // 0x0007c960
    void Tidy();                                                                                // 0x0007c990
    void ThrowLength();                 // _Xlen: length_error("vector<T> too long")             // 0x0007ca60
    void InsertN(ReverseDrawEntry *where, uint32_t count, const ReverseDrawEntry *value);       // 0x0007cb00
    // insert(where, value), the iterator to the new element answered through `result`
    ReverseDrawEntry** Insert(ReverseDrawEntry **result, ReverseDrawEntry *where, const ReverseDrawEntry *value);
                                                                                                // 0x0007ce30
    void PushBack(const ReverseDrawEntry *value);                                               // 0x0007ceb0
};
static_assert(sizeof(ReverseDrawList) == 0x10, "a vector is 16 bytes");

// The algorithms its members use, each as compiled for the entries (the iterators answered through `result`)
void FillReverseDrawEntries(ReverseDrawEntry *from, ReverseDrawEntry *to, const ReverseDrawEntry *value);   // 0x0007c820
ReverseDrawEntry** CopyBackwardReverseDrawEntries(ReverseDrawEntry **result, ReverseDrawEntry *from,
                                                  ReverseDrawEntry *to, ReverseDrawEntry *destEnd);        // 0x0007c850
ReverseDrawEntry** CopyBackwardReverseDrawEntriesThunk(ReverseDrawEntry **result, ReverseDrawEntry *from,
                                                       ReverseDrawEntry *to, ReverseDrawEntry *destEnd);   // 0x0007c890
ReverseDrawEntry* UninitializedCopyReverseDrawEntries(ReverseDrawEntry *from, ReverseDrawEntry *to,
                                                      ReverseDrawEntry *dest);                             // 0x0007c8d0
void UninitializedFillReverseDrawEntries(ReverseDrawEntry *dest, uint32_t count, const ReverseDrawEntry *value);   // 0x0007c900

#endif // DRIVING_CAMERA_DIRECTORQUEUE_H_
