#include "DirectorQueue.h"

#include "CameraIniLoader.h"            // fgCameraTables, fgCameraModeIndices
#include "PlayerCamera.h"
#include "../eagl/EaglGlobals.h"        // EaglMalloc, EaglFree
#include "../eagl/Loader.h"             // DynamicLoader
#include "../eagl/RenderMethod.h"
#include "../engine/CoreFoundation.h"   // ThrowLengthError
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../../helpers.h"

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// RDirectorQueue and RDirectorQueueData, and the renderer pieces after them. See DirectorQueue.h.
// ---------------------------------------------------------------------------------------------------------------

// The render method's compiled data (its .rm): the shaders, the packets the interpreter runs, the parameters' names
// XBE_GLOBAL(0x001ce7c8, 0x98)
const std::array<uint32_t, 38> TextureDofVSFunction = [] {
    using namespace Vsh;
    return Function<38>({
        Dp4(oPos.x(), v(0), c(-96)),
        Dp4(oPos.y(), v(0), c(-95)),
        Dp4(oPos.z(), v(0), c(-94)),
        Dp4(oPos.w(), v(0), c(-93)),
        Mov(oT0, v(1)),
        Mov(oD0, v(2)),
        (Mul(oPos.xyz(), r(12), c(-38)) + Rcc(r1.x(), r(12).w())).TempRegister(1),
        Mad(oPos.xyz(), r(12), r(1).x(), c(-37)),
        Add(oPos, c(-92), r(12)),
    });
}();

// XBE_GLOBAL(0x001ce860, 0xf0)
const D3DPixelShaderDef TextureDofPSDefinition = {
    .alphaInputs = { PS_COMBINERINPUTS(PS_REGISTER_V0 | PS_CHANNEL_ALPHA | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                       PS_REGISTER_T0 | PS_CHANNEL_ALPHA | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                       PS_REGISTER_ZERO | PS_CHANNEL_ALPHA, PS_REGISTER_ZERO | PS_CHANNEL_ALPHA) },
    .alphaOutputs = { PS_COMBINEROUTPUTS(PS_REGISTER_R0, PS_REGISTER_DISCARD, PS_REGISTER_DISCARD,
                                         PS_COMBINEROUTPUT_IDENTITY) },
    .rgbInputs = { PS_COMBINERINPUTS(PS_REGISTER_V0 | PS_CHANNEL_RGB | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                     PS_REGISTER_T0 | PS_CHANNEL_RGB | PS_INPUTMAPPING_SIGNED_IDENTITY,
                                     PS_REGISTER_ZERO, PS_REGISTER_ZERO) },
    .rgbOutputs = { PS_COMBINEROUTPUTS(PS_REGISTER_R0, PS_REGISTER_DISCARD, PS_REGISTER_DISCARD,
                                       PS_COMBINEROUTPUT_IDENTITY) },
    .combinerCount = PS_COMBINERCOUNT(1, PS_COMBINERCOUNT_MUX_MSB | PS_COMBINERCOUNT_UNIQUE_C0 |
                                             PS_COMBINERCOUNT_UNIQUE_C1),
    .textureModes = PS_TEXTUREMODES(PS_TEXTUREMODES_PROJECT2D, PS_TEXTUREMODES_NONE, PS_TEXTUREMODES_NONE,
                                    PS_TEXTUREMODES_NONE),
    .c0Mapping = PS_CONSTANTMAPPING(15, 15, 15, 15, 15, 15, 15, 15),
    .c1Mapping = PS_CONSTANTMAPPING(15, 15, 15, 15, 15, 15, 15, 15),
    .finalCombinerConstants = PS_FINALCOMBINERCONSTANTS(15, 15, PS_GLOBALFLAGS_TEXMODE_ADJUST),
};

// XBE_GLOBAL(0x001ce950, 4)
const void *TextureDofVSFunctions[1] = { TextureDofVSFunction.data() };
// XBE_GLOBAL(0x001ce954, 4)
const uint8_t *TextureDofPSDefinitions[1] = {
    reinterpret_cast<const uint8_t *>(&TextureDofPSDefinition),
};
// XBE_GLOBAL(0x001ce958, 0xac)
uint32_t TextureDofPackets[43] = {
    EAGL::PacketHeader(EAGL::kOpDefaultShaders, 2), 0,
    EAGL::PacketHeader(EAGL::kOpGeoPrimState, 2), 0,   // State
    EAGL::PacketHeader(EAGL::kOpTAR, 3), 0, 0,   // Texture
    EAGL::PacketHeader(EAGL::kOpStream, 2), 0,   // Parameters
    EAGL::PacketHeader(EAGL::kOpIndexData, 2), 0,   // Indices
    EAGL::PacketHeader(EAGL::kOpVSMatrix20, 3), 0, uint32_t(-96),   // MyMatrix
    EAGL::PacketHeader(EAGL::kOpVSConstants5, 4), 0, uint32_t(-92), 4,   // DOFOffset
    // Coordinates
    EAGL::PacketHeader(EAGL::kOpDynamicStream, 7), 0, 0, 0xc, EAGL::kNoBuffer, EAGL::kNoBuffer, EAGL::kNoBuffer,
    // UV
    EAGL::PacketHeader(EAGL::kOpDynamicStream, 7), 0, 1, 8, EAGL::kNoBuffer, EAGL::kNoBuffer, EAGL::kNoBuffer,
    // Colours
    EAGL::PacketHeader(EAGL::kOpDynamicStream, 7), 0, 2, 4, EAGL::kNoBuffer, EAGL::kNoBuffer, EAGL::kNoBuffer,
    EAGL::PacketHeader(EAGL::kOpDrawIndexedVertices, 3), 0, 3,
    0,
};
// XBE_GLOBAL(0x001cea04, 0x1c)
const uint32_t TextureDofVSDeclaration[7] = {
    D3DVSD_STREAM(0),
    D3DVSD_REG(0, D3DVSDT_FLOAT3),
    D3DVSD_STREAM(1),
    D3DVSD_REG(1, D3DVSDT_FLOAT2),
    D3DVSD_STREAM(2),
    D3DVSD_REG(2, D3DVSDT_D3DCOLOR),
    D3DVSD_END(),
};
// XBE_GLOBAL(0x001cea74, 0x28)
const char *TextureDofParamNames[10] = {
    "State", "Texture", "Parameters", "Indices", "MyMatrix", "DOFOffset", "Coordinates", "UV", "Colours", NULL,
};

// XBE_GLOBAL(0x00241460, 0x34)
alignas(16) EAGL::RenderMethod TextureDofMethod;

// Objects the static initialisers construct and destroy, which nothing else uses (names ours)
// XBE_GLOBAL(0x001ebdb8, 0xe4)
alignas(16) TextureDofState InitialiserDofStates[3];
// XBE_GLOBAL(0x001ebea0, 0x108)
alignas(16) EAGL::DynamicModel InitialiserModels[3];
// XBE_GLOBAL(0x001ebfa8, 0x4c)
alignas(16) EAGL::GeoPrimState InitialiserGeoPrimState;   // "HB_RM = TextureDOF", a static initialiser's

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
    method = memory != NULL ? static_cast<EAGL::RenderMethod *>(memory)->ConstructChild(&TextureDofMethod) : NULL;
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

// The largest vector of 80-byte entries
constexpr uint32_t kMaxReverseDrawEntries = 0x3333333;

// FUNC_AT(0x0007ca60)
void ReverseDrawList::ThrowLength() {
    CAMERA_UNTESTED("vector<ReverseDrawEntry>::_Xlen");
    ThrowLengthError("vector<T> too long");
}

// FUNC_AT(0x0007cb00)
void ReverseDrawList::InsertN(ReverseDrawEntry *where, uint32_t count, const ReverseDrawEntry *value) {
    ReverseDrawEntry copy = *value;
    uint32_t capacity = first == NULL ? 0 : uint32_t(end - first);
    if (count == 0)
        return;
    uint32_t size = first == NULL ? 0 : uint32_t(last - first);
    if (kMaxReverseDrawEntries - size < count) {
        ThrowLength();
    } else if (capacity < size + count) {
        uint32_t newCapacity = kMaxReverseDrawEntries - capacity / 2 < capacity ? 0 : capacity + capacity / 2;
        if (newCapacity < size + count)
            newCapacity = Size() + count;
        ReverseDrawEntry *block =
            static_cast<ReverseDrawEntry *>(UMemory::FastAlloc(newCapacity * sizeof(ReverseDrawEntry), "STL"));
        ReverseDrawEntry *next = UninitializedCopyReverseDrawEntries(first, where, block);
        UninitializedFillReverseDrawEntries(next, count, &copy);
        UninitializedCopyReverseDrawEntries(where, last, next + count);
        uint32_t newSize = count + (first == NULL ? 0 : uint32_t(last - first));
        if (first != NULL)
            Deallocate(first, uint32_t(end - first));
        end = block + newCapacity;
        last = block + newSize;
        first = block;
    } else if (uint32_t(last - where) < count) {
        Ucopy(where, last, where + count);
        Ufill(last, count - uint32_t(last - where), &copy);
        last += count;
        FillReverseDrawEntries(where, last - count, &copy);
    } else {
        ReverseDrawEntry *oldLast = last;
        last = Ucopy(oldLast - count, oldLast, oldLast);
        ReverseDrawEntry *unused;
        CopyBackwardReverseDrawEntriesThunk(&unused, where, oldLast - count, oldLast);
        FillReverseDrawEntries(where, where + count, &copy);
    }
}

// FUNC_AT(0x0007ce30)
ReverseDrawEntry** ReverseDrawList::Insert(ReverseDrawEntry **result, ReverseDrawEntry *where,
                                           const ReverseDrawEntry *value) {
    uint32_t size = first == NULL ? 0 : uint32_t(last - first);
    uint32_t offset = size == 0 ? 0 : uint32_t(where - first);
    InsertN(where, 1, value);
    *result = first + offset;
    return result;
}

// FUNC_AT(0x0007ceb0)
void ReverseDrawList::PushBack(const ReverseDrawEntry *value) {
    uint32_t size = first == NULL ? 0 : uint32_t(last - first);
    if (first != NULL && size < uint32_t(end - first)) {
        UninitializedFillReverseDrawEntries(last, 1, value);
        last++;
    } else {
        ReverseDrawEntry *inserted;
        Insert(&inserted, last, value);
    }
}
