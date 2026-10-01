#include "ActionQueue.hpp"

#include <stdio.h>

// While this is 0, action 0x1f is not delivered to anyone (FeedQueue, 0x0004f371). Written elsewhere in the game.
#define gAllowAction31 (*(int *)0x00234e34)

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

    // One log line the first time each action id is delivered: enough to see the input layer working (START in
    // play is 0x47, pause) without a line per frame.
    static bool seen[256];
    unsigned id = (unsigned)action.action & 0xff;
    if (!seen[id]) {
        seen[id] = true;
        printf("[input] action 0x%02x first delivered: value %.2f, source %d, to %d queues\n", action.action,
               action.value, action.source, (int)(queuesLast - queuesFirst));
    }
}

// AUTOINJECT
void ActionQueueManager::FlushAllQueues() {
    for (ActionQueue **it = queuesFirst; it < queuesLast; it++)
        (*it)->Flush();
}
