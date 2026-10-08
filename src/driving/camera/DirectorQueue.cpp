#include "DirectorQueue.h"

#include "CameraIniLoader.h"            // fgCameraTables, fgCameraModeIndices
#include "PlayerCamera.h"
#include "../eagl/EaglGlobals.h"        // EaglMalloc, EaglFree
#include "../eagl/Loader.h"             // DynamicLoader
#include "../eagl/RenderMethod.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../../helpers.h"

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// RDirectorQueue and RDirectorQueueData, and the renderer pieces after them. See DirectorQueue.h.
// ---------------------------------------------------------------------------------------------------------------

#define TextureDofMethod ((EAGL::RenderMethod *)0x00241460)   // "HB_RM = TextureDOF", a static initialiser's

// ---- RDirectorQueueData

// FUNC_AT(0x0007c160)
RDirectorQueueData* RDirectorQueueData::Construct(uint16_t delay, uint16_t cameraMode, uint16_t unknown14,
                                                  uint16_t flags, CARP::Instance *data18, PhysicsObject *anchor,
                                                  uint32_t unknown24) {
    this->delay = delay;
    this->cameraMode = cameraMode;
    this->unknown14 = unknown14;
    this->flags = flags;
    this->data18 = data18;
    data1C = NULL;
    position.x = 0.0f;
    position.y = 0.0f;
    position.z = 0.0f;
    position.w = 1.0f;
    this->anchor = anchor;
    this->unknown24 = unknown24;
    return this;
}

// FUNC_AT(0x0007c1c0)
RDirectorQueueData* RDirectorQueueData::ConstructCopy(const RDirectorQueueData *other) {
    delay = other->delay;
    cameraMode = other->cameraMode;
    unknown14 = other->unknown14;
    flags = other->flags;
    data18 = other->data18;
    data1C = other->data1C;
    anchor = other->anchor;
    position = other->position;
    unknown24 = other->unknown24;
    return this;
}

// FUNC_AT(0x0007c220)
void RDirectorQueueData::Destruct() {
    delay = 0;
    cameraMode = 0;
    unknown14 = 0;
    flags = 0;
    data1C = NULL;
    anchor = NULL;
    position.x = 0.0f;
    position.y = 0.0f;
    position.z = 0.0f;
    position.w = 1.0f;
    unknown24 = 0;
    data18 = NULL;
}

// FUNC_AT(0x0007c250)
void DirectorQueueNode::DestroyValue() {
    value.Destruct();
}

// ---- the queue's list

// FUNC_AT(0x0007c280)
DirectorQueueNode* RDirectorQueue::Queue::BuyNode(DirectorQueueNode *next, DirectorQueueNode *prev,
                                                  const RDirectorQueueData *value) {
    return BuyNodeT(next, prev, value);
}

// FUNC_AT(0x0007c2c0)
DirectorQueueNode* RDirectorQueue::Queue::BuyHead() {
    return BuyHeadT();
}

// FUNC_AT(0x0007c2e0)
DirectorQueueNode** RDirectorQueue::Queue::Erase(DirectorQueueNode **result, DirectorQueueNode *first,
                                                 DirectorQueueNode *last) {
    return EraseT(result, first, last);
}

// FUNC_AT(0x0007c340)
void RDirectorQueue::Queue::PopFront() {
    DirectorQueueNode *node = Begin();
    if (node != head) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        DestroyListValue(node);
        UMemory::FastFree(node, sizeof(DirectorQueueNode));
        size--;
    }
}

// FUNC_AT(0x0007c5f0)
void RDirectorQueue::Queue::IncreaseSize(uint32_t count) {
    IncreaseSizeT(count);
}

// ---- RDirectorQueue

// FUNC_AT(0x0007c380)
RDirectorQueue* RDirectorQueue::Construct(RPlayerCamera *camera) {
    queue.head = queue.BuyHead();
    this->camera = camera;
    flags &= ~kHeld;
    queue.size = 0;
    return this;
}

// FUNC_AT(0x0007c3b0)
void RDirectorQueue::Destruct() {
    queue.DestructT();
}

// FUNC_AT(0x0007c3f0)
void RDirectorQueue::ProcessDirectorLogic() {
    CameraTables &tables = fgCameraTables;
    if (flags & kRestart) {
        camera->cameraMode = tables.modeCount;
        camera->previousCameraMode = tables.modeCount;
        camera->lastSelectableCameraMode = tables.modeCount;
        DirectorQueueNode *end;
        queue.Erase(&end, queue.Begin(), queue.head);
        camera->SetCameraModeByIndex(fgCameraModeIndices.defaultCamera, 0, 0, 0, 0, NULL, NULL);
    } else {
        if (tables.modes[camera->cameraMode].tumble == 1 && camera->GetAnchorResetAvailable())
            camera->SetTumbleCam(RPlayerCamera::GetMaxTumble());
        // out of a tumble or collision mode once it is over
        if (tables.modes[camera->cameraMode].type & (kCameraTumble | kCameraCollision)) {
            if (int(camera->tumbleCamIndex) < 1 || (camera->modeChangeFlags & RWorldCamera::kLookBackChanged))
                camera->SetCameraModeByIndex(camera->previousCameraMode, 0, 0, 0, 0, NULL, NULL);
        }
    }

    if (queue.size != 0 && !(flags & kHeld)) {
        // the changes queued before the last are dropped, up to one that is kept
        RDirectorQueueData *data = &queue.Begin()->value;
        while (!(data->flags & RDirectorQueueData::kKeep) && queue.size > 1) {
            queue.PopFront();
            data = &queue.Begin()->value;
        }
        if (data->delay > 0) {
            data->delay--;
            flags = 0;
            return;
        }
        if (data->anchor != NULL)
            camera->DirectorSetAnchor(data);
        camera->DirectorChangeCameraMode(data);
        if ((data->flags & RDirectorQueueData::kOwnsData18) && data->data18 != NULL) {
            OperatorDelete(data->data18);
            data->data18 = NULL;
        }
        if ((data->flags & RDirectorQueueData::kOwnsData1C) && data->data1C != NULL) {
            OperatorDelete(data->data1C);
            data->data1C = NULL;
        }
        queue.PopFront();
    }
    flags = 0;
}

// FUNC_AT(0x0007c5e0)
void RDirectorQueue::RestartDirectorQueue() {
    flags = kRestart;
    ProcessDirectorLogic();
}

// FUNC_AT(0x0007c6a0)
void RDirectorQueue::AppendData(const RDirectorQueueData *data) {
    DirectorQueueNode *head = queue.head;
    DirectorQueueNode *node = queue.BuyNode(head, head->prev, data);
    queue.IncreaseSize(1);
    head->prev = node;
    node->prev->next = node;
}

// ---- TextureDofState

// FUNC_AT(0x0007c6e0)
TextureDofState* TextureDofState::Construct() {
    void *memory = EaglMalloc(sizeof(EAGL::RenderMethod), "EAGL::Rendermethod new");
    method = memory != NULL ? static_cast<EAGL::RenderMethod *>(memory)->ConstructChild(TextureDofMethod) : NULL;
    for (int i = 0; i < 8; i++)
        unknown04[i] = 0;
    bool found;
    modelViewProjection.value = DynamicLoader::GetRegisteredVar("EAGL::ViewPort::gpModelViewProjectionMatrix", &found);
    modelViewProjection.unknown00 = 1;
    dofOffset.value = DynamicLoader::GetRegisteredVar("dofoffset", &found);
    for (int i = 0; i < 6; i++)
        unknown34[i] = 0;
    dofOffset.unknown00 = 1;
    return this;
}

// FUNC_AT(0x0007c7b0)
void TextureDofState::Destruct() {
    if (method != NULL) {
        method->Destruct();
        EaglFree(method, sizeof(EAGL::RenderMethod));
    }
}

// ---- ReverseDrawList

// FUNC_AT(0x0007c7d0)
uint32_t ReverseDrawList::Size() {
    return first == NULL ? 0 : uint32_t(last - first);
}

// FUNC_AT(0x0007c800)
void ReverseDrawList::Deallocate(ReverseDrawEntry *block, uint32_t count) {
    if (block != NULL)
        UMemory::FastFree(block, count * sizeof(ReverseDrawEntry));
}

// FUNC_AT(0x0007c820)
void FillReverseDrawEntries(ReverseDrawEntry *from, ReverseDrawEntry *to, const ReverseDrawEntry *value) {
    for (; from != to; from++)
        *from = *value;
}

// FUNC_AT(0x0007c850)
ReverseDrawEntry** CopyBackwardReverseDrawEntries(ReverseDrawEntry **result, ReverseDrawEntry *from,
                                                  ReverseDrawEntry *to, ReverseDrawEntry *destEnd) {
    while (to != from)
        *--destEnd = *--to;
    *result = destEnd;
    return result;
}

// FUNC_AT(0x0007c890)
ReverseDrawEntry** CopyBackwardReverseDrawEntriesThunk(ReverseDrawEntry **result, ReverseDrawEntry *from,
                                                       ReverseDrawEntry *to, ReverseDrawEntry *destEnd) {
    CopyBackwardReverseDrawEntries(result, from, to, destEnd);
    return result;
}

// FUNC_AT(0x0007c8d0)
ReverseDrawEntry* UninitializedCopyReverseDrawEntries(ReverseDrawEntry *from, ReverseDrawEntry *to,
                                                      ReverseDrawEntry *dest) {
    for (; from != to; from++, dest++) {
        if (dest != NULL)
            *dest = *from;
    }
    return dest;
}

// FUNC_AT(0x0007c900)
void UninitializedFillReverseDrawEntries(ReverseDrawEntry *dest, uint32_t count, const ReverseDrawEntry *value) {
    for (; count > 0; count--, dest++) {
        if (dest != NULL)
            *dest = *value;
    }
}

// FUNC_AT(0x0007c930)
ReverseDrawEntry* ReverseDrawList::Ucopy(ReverseDrawEntry *from, ReverseDrawEntry *to, ReverseDrawEntry *dest) {
    return UninitializedCopyReverseDrawEntries(from, to, dest);
}

// FUNC_AT(0x0007c960)
ReverseDrawEntry* ReverseDrawList::Ufill(ReverseDrawEntry *dest, uint32_t count, const ReverseDrawEntry *value) {
    UninitializedFillReverseDrawEntries(dest, count, value);
    return dest + count;
}

// FUNC_AT(0x0007c990)
void ReverseDrawList::Tidy() {
    if (first != NULL)
        UMemory::FastFree(first, uint32_t(end - first) * sizeof(ReverseDrawEntry));
    first = NULL;
    last = NULL;
    end = NULL;
}
