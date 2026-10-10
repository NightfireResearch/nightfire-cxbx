#include "ActionQueue.hpp"

#include "CoreFoundation.h"
#include "InputUntested.h"
#include "UMemory.hpp"
#include "../physics/RigidBodyBasics.h"   // PointerVectorDeallocate
#include "../../helpers.h"

// While this is 0, action 0x1f is not delivered to anyone (FeedQueue, 0x0004f371). Written elsewhere in the game.
#define gAllowAction31 (*(int *)0x00234e34)

// The manager, GetActionQueueManager's function-local static, and its construction guard (bit 0)
#define gActionQueueManager (*(ActionQueueManager *)0x001e23f0)
#define gActionQueueManagerMade U32_AT(0x001e2400)

// The static's destructor as the original registers it, at its address: a call of Tidy (StaticInit.cpp's
// DestroyStatic_001e23f0).
#define ActionQueueManager_AtExit ((void (*)(void))0x0015cde0)
#define CRT_atexit ((int (*)(void (*)(void)))0x00132a7b)

// The STL's helpers the vector's growth calls, at their addresses and in the original's order (the extra
// arguments are the allocator and a word the helpers ignore, as the original passes them)
#define Vector_UninitializedCopy ((ActionQueue **(*)(ActionQueue **, ActionQueue **, ActionQueue **, ActionQueueManager *, uint32_t))0x000bfeb0)
#define Vector_UninitializedFillN ((void (*)(ActionQueue **, uint32_t, ActionQueue *const *, ActionQueueManager *, uint32_t))0x000b2ec0)
#define Vector_Ucopy ((ActionQueue **(__fastcall *)(ActionQueueManager *, int, ActionQueue **, ActionQueue **, ActionQueue **))0x000c1230)
#define Vector_Ufill ((void (__fastcall *)(ActionQueueManager *, int, ActionQueue **, uint32_t, ActionQueue *const *))0x000c1290)
#define Vector_CopyBackward ((void (*)(ActionQueue ***, ActionQueue **, ActionQueue **, ActionQueue **))0x000b2e40)

constexpr uint32_t kMaxQueues = 0x3fffffff;   // max_size() of a vector of pointers

static const int kRingCapacity = 200;

// ActionQueueRing's own push (0x0004f1c0), which only ReceiveAction calls: the tail moves on, wrapping at the
// capacity, and when the ring is already full the head moves on too, so the oldest action is overwritten.
static void RingPush(ActionQueueRing *ring, const ActionData *action) {
    ring->tail++;
    if (ring->tail > ring->capacity - 1)
        ring->tail = 0;
    ring->count++;
    if (ring->count > ring->capacity) {
        ring->head++;
        if (ring->head > ring->capacity - 1)
            ring->head = 0;
        ring->count = ring->capacity;
    }
    ring->items[ring->tail] = *action;
}

// FUNC_AT(0x0004f320)
ActionQueue* ActionQueue::Construct(char *name) {
    (void)name;
    // ActionQueueRing's constructor (0x0004f2d0): every slot ActionData(0, 0, 0), and the ring empty.
    for (int i = 0; i < kRingCapacity; i++) {
        ring.items[i].action = 0;
        ring.items[i].source = 0;
        ring.items[i].value = 0.0f;
    }
    ring.count = 0;
    ring.tail = -1;
    ring.head = 0;
    ring.capacity = kRingCapacity;

    manager = ActionQueueManager::GetActionQueueManager();
    ring.count = 0;
    ring.tail = -1;
    ring.head = 0;
    manager->RegisterQueue(this);
    return this;
}

// FUNC_AT(0x0004f1b0)
void ActionQueue::Destruct() {
    manager->UnRegisterQueue(this);
}

// AUTOINJECT
bool ActionQueue::IsEmpty() {
    return ring.count == 0;
}

// AUTOINJECT
bool ActionQueue::ReceiveAction(ActionData *action) {
    RingPush(&ring, action);
    return true;
}

// AUTOINJECT
void ActionQueue::PopAction() {
    if (ring.count > 0) {
        ring.head++;
        if (ring.head > ring.capacity - 1)
            ring.head = 0;
        ring.count--;
    }
}

// AUTOINJECT
void ActionQueue::Flush() {
    ring.count = 0;
    ring.tail = -1;
    ring.head = 0;
}

// FUNC_AT(0x0004f290)
ActionRef* ActionQueue::GetAction(ActionRef *result) {
    result->data = ring.count > 0 ? &ring.items[ring.head] : nullptr;
    return result;
}

// AUTOINJECT
void ActionQueueManager::RegisterQueue(ActionQueue *queue) {
    PushBack(&queue);
}

// Removes the queue from the list. As in the original, the scan steps on after a removal without looking again
// at the entry that moved into the freed slot, so of two adjacent copies of the same queue only the first goes.
// A queue is only ever registered once (by its constructor), so this never matters.
// AUTOINJECT
void ActionQueueManager::UnRegisterQueue(ActionQueue *queue) {
    for (ActionQueue **it = queuesFirst; it < queuesLast; it++) {
        if (*it != queue)
            continue;
        for (ActionQueue **from = it + 1; from != queuesLast; from++)
            from[-1] = *from;
        queuesLast--;
    }
}

// Delivers the action to every queue, in the order they registered. Action ids run from 1 to 0x40; 0x1f is held
// back while gAllowAction31 is 0.
// AUTOINJECT
void ActionQueueManager::FeedQueue(ActionData action) {
    if (action.action >= 1 && action.action <= 0x40 && gAllowAction31 == 0 && action.action == 0x1f)
        return;
    for (ActionQueue **it = queuesFirst; it < queuesLast; it++)
        (*it)->ReceiveAction(&action);
}

// AUTOINJECT
void ActionQueueManager::FlushAllQueues() {
    for (ActionQueue **it = queuesFirst; it < queuesLast; it++)
        (*it)->Flush();
}

// ---- the static and its vector

// FUNC_AT(0x0004f7d0)
ActionQueueManager* ActionQueueManager::GetActionQueueManager() {
    if (!(gActionQueueManagerMade & 1)) {
        gActionQueueManagerMade |= 1;
        gActionQueueManager.queuesFirst = nullptr;
        gActionQueueManager.queuesLast = nullptr;
        gActionQueueManager.queuesEnd = nullptr;
        CRT_atexit(ActionQueueManager_AtExit);
    }
    return &gActionQueueManager;
}

// FUNC_AT(0x0004f830)
void ActionQueueManager::PushBack(ActionQueue *const *queue) {
    uint32_t size = queuesFirst != nullptr ? uint32_t(queuesLast - queuesFirst) : 0;
    if (queuesFirst != nullptr && size < uint32_t(queuesEnd - queuesFirst)) {
        Vector_UninitializedFillN(queuesLast, 1, queue, this, uint32_t(uintptr_t(queue)));
        queuesLast++;
        return;
    }
    InsertN(queuesLast, 1, queue);
}

// The original's catch block (free the new storage, rethrow) has nothing to catch here: copying pointers cannot
// throw.
// FUNC_AT(0x0004f510)
void ActionQueueManager::InsertN(ActionQueue **where, uint32_t count, ActionQueue *const *value) {
    ActionQueue *copy = *value;   // copied first: it may live in the vector
    uint32_t capacity = queuesFirst != nullptr ? uint32_t(queuesEnd - queuesFirst) : 0;
    if (count == 0)
        return;
    uint32_t size = queuesFirst != nullptr ? uint32_t(queuesLast - queuesFirst) : 0;
    if (kMaxQueues - size < count) {
        INPUT_UNTESTED("ActionQueueManager::InsertN's length error");
        Xlen();
        return;
    }
    if (capacity < size + count) {
        uint32_t grown = kMaxQueues - capacity / 2 < capacity ? 0 : capacity + capacity / 2;
        if (grown < size + count)
            grown = size + count;
        uint32_t bytes = grown * sizeof(ActionQueue *);
        ActionQueue **storage = static_cast<ActionQueue **>(UMemory::FastAlloc(bytes, "STL"));
        ActionQueue **at = Vector_UninitializedCopy(queuesFirst, where, storage, this, count);
        Vector_UninitializedFillN(at, count, &copy, this, count);
        Vector_UninitializedCopy(where, queuesLast, at + count, this, count);
        uint32_t newSize = size + count;
        if (queuesFirst != nullptr)
            PointerVectorDeallocate(queuesFirst, uint32_t(queuesEnd - queuesFirst));
        queuesEnd = storage + grown;
        queuesLast = storage + newSize;
        queuesFirst = storage;
    } else if (uint32_t(queuesLast - where) < count) {
        // Room enough: push_back never comes here (it only inserts when the vector is full).
        INPUT_UNTESTED("ActionQueueManager::InsertN in the middle");
        Vector_Ucopy(this, 0, where, queuesLast, where + count);
        Vector_Ufill(this, 0, queuesLast, count - uint32_t(queuesLast - where), &copy);
        queuesLast += count;
        FillQueuePointers(where, queuesLast - count, &copy);
    } else {
        INPUT_UNTESTED("ActionQueueManager::InsertN in the middle");
        ActionQueue **oldLast = queuesLast;
        queuesLast = Vector_Ucopy(this, 0, oldLast - count, oldLast, oldLast);
        ActionQueue **ignored;
        Vector_CopyBackward(&ignored, where, oldLast - count, oldLast);
        FillQueuePointers(where, where + count, &copy);
    }
}

// FUNC_AT(0x0004f490)
void ActionQueueManager::Xlen() {
    INPUT_UNTESTED("ActionQueueManager::Xlen");
    ThrowLengthError("vector<T> too long");
}

// FUNC_AT(0x0004f400)
void ActionQueueManager::Tidy() {
    if (queuesFirst != nullptr)
        UMemory::FastFree(queuesFirst, uint32_t(queuesEnd - queuesFirst) * sizeof(ActionQueue *));
    queuesFirst = nullptr;
    queuesLast = nullptr;
    queuesEnd = nullptr;
}

// FUNC_AT(0x0004f3e0)
void FillQueuePointers(ActionQueue **first, ActionQueue **last, ActionQueue *const *value) {
    for (; first != last; first++)
        *first = *value;
}
