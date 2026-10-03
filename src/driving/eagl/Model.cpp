#include "Model.h"

#include "D3D8State.h"
#include "Profiler.h"
#include "Realgraph.h"
#include "RenderMethod.h"
#include "Transform.h"
#include "View.h"
#include "../platform/RealPrint.h"

#include <intrin.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGL::Model and EAGL::DynamicModel (docs/driving/eagl.md 2.7, 4.3) and the rest of 0x000e8b40..0x000eb220.
//
// Model::Draw is the hot one: it culls the model's bounding sphere against the current viewport, builds the
// model, model-view-projection and model-view matrices into EAGL's registered globals (render methods read them
// by name), applies the model's patch, sets CurrentVariation and walks the optimised draw list, handing each
// GeoPrim to RenderMethod::Draw (the interpreter); then it draws the three child lists. The morph, instancing and
// DrawArray paths are dead in this game (8.8) but ported all the same, as is the unreferenced code in the range:
// XDK inlines compiled out of line (register arguments, so naked adapters keep the registers) and SHAPE helpers.
// The texture-format mapping 0x000eb070 and the clut finder 0x000eb200 are Tar.cpp's.
//
// The x87 arithmetic is in double, in the original's order, with a float rounding at every store.
// ---------------------------------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

using EAGL::DrawArray;
using EAGL::GeoPrim;
using EAGL::MorphRecord;
using EAGL::TAR;

namespace {

// EAGL's allocator hooks (2.13): cdecl malloc(size, name) and free(pointer, size)
#define EaglMalloc (*(void *(**)(uint32_t size, const char *name))0x001caf68)
#define EaglFree (*(void (**)(void *pointer, uint32_t size))0x001caf6c)
#define NameDrawArrayNew ((const char *)0x001cbdb4)         // "EAGL::DrawArray new"
#define NameGeoPrimList ((const char *)0x001cbd3c)          // "Dynamic Model Geoprim list"
#define NameDrawArrayList ((const char *)0x001cbd58)        // "Dynamic Model DrawArray list"
#define NameGeoPrimList2 ((const char *)0x001cbd78)         // "Dynamic Model Geoprim list" (AddGeoPrim's copy)
#define NameDrawArrayList2 ((const char *)0x001cbd94)       // "Dynamic Model Draw Array list"

// The registered view matrices (2.13) and the variation render methods index their parameters with
#define ViewMatrix ((float *)0x0023f950)                    // gpViewMatrix
#define ModelViewMatrix ((float *)0x0023f990)               // gpModelViewMatrix
#define ModelViewProjectionMatrix ((float *)0x0023f9d0)     // gpModelViewProjectionMatrix
#define ModelMatrix ((float *)0x0023fa50)                   // gpModelMatrix
#define ViewProjectionMatrix ((float *)0x0023fa90)          // gpViewProjectionMatrix
#define CurrentVariation I32_AT(0x0023ff60)                 // EAGLInternal::CurrentVariation

// REP MOVSD of 16 dwords
inline void CopyMatrix(float *destination, const float *source) {
    __movsd((unsigned long *)destination, (const unsigned long *)source, 16);
}

// The model matrix into gpModelMatrix, then gpModelViewProjectionMatrix = model * view-projection and
// gpModelViewMatrix = model * view, as Draw, DrawInstances and DynamicModel::Draw all do it inline.
void SetModelMatrices(const float *model) {
    alignas(16) Transform t;
    CopyMatrix(ModelMatrix, model);
    t.BuildMatrix(ViewProjectionMatrix);
    t.PrependMatrix(ModelMatrix);
    CopyMatrix(ModelViewProjectionMatrix, t.m);
    t.BuildMatrix(ViewMatrix);
    t.PrependMatrix(ModelMatrix);
    CopyMatrix(ModelViewMatrix, t.m);
}

// The optimised draw list (Draw and DrawInstances): the first word's low half is a limit, and is itself the first
// entry. 0xffff entries are followed by a GeoPrim*; others head a block {header, ?, n, n words} skipped whole when
// its geometry's enable word is 0. The walk ends at the first entry whose low half is not above the limit.
void WalkDrawList(const EAGL::Model *model) {
    const uint32_t *p = model->drawList;
    if (p == NULL)
        return;
    uint32_t limit = *p++ & 0xffff;
    uint32_t entry = limit;
    uint32_t block = 0;
    do {
        if (entry == 0xffff) {
            GeoPrim *geoPrim = (GeoPrim *)*p++;
            if (geoPrim != NULL)
                geoPrim->method->Draw(geoPrim);
        } else {
            if (model->enableTable[block].enable == 0)
                p = p + p[1] + 2;
            else
                p += 2;
            block++;
        }
        entry = *p++ & 0xffff;
    } while (entry > limit);
}

// The current viewport, as Model::Draw finds it: the texture render context's if one is current, else the render
// context's
EAGL::ViewPort *CurrentViewPort() {
    EAGL::TextureRenderContext *textureContext = EAGL::Device::Get()->GetCurrentTextureRenderContext();
    if (textureContext != NULL)
        return textureContext->GetCurrentViewPort();
    return EAGL::Device::Get()->GetCurrentRenderContext()->GetCurrentViewPort();
}

// One variation of a model's TAR array
TAR *VariationOf(TAR *first, int32_t variation) {
    return (TAR *)((uint8_t *)first + variation * EAGL::kTARArrayStride);
}

// A DynamicModel's DrawArray for a GeoPrim, created on first use (the prologue of every vertex-array method)
DrawArray *DrawArrayFor(EAGL::DynamicModel *model, int index) {
    if (model->drawArrays[index] == NULL) {
        void *memory = EaglMalloc(sizeof(DrawArray), NameDrawArrayNew);
        DrawArray *drawArray = memory != NULL ? static_cast<EAGL::ProfilerDrawArray *>(memory)->Construct() : NULL;
        model->drawArrays[index] = drawArray;
        model->drawArrays[index]->SetGeoPrim(model->geoPrims[index]);
    }
    return model->drawArrays[index];
}

// The morph adders' truncation: __ftol2, to 64 bits
inline int64_t Truncate(double value) {
    return (int64_t)value;
}

}  // namespace

// ---- MorphWeights

// FUNC_AT(0x000e8b50)
void* EAGL::MorphWeights::GetPointer(uint32_t index) {
    if (index > count)   // an index equal to the count passes (the original's JBE)
        return NULL;
    return pointers[index];
}

// FUNC_AT(0x000e8b70)
void EAGL::MorphWeights::SetWeight(uint32_t index, float weight) {
    if (index <= count)
        weights[index] = weight;
}

// FUNC_AT(0x000e8b90)
float EAGL::MorphWeights::GetWeight(uint32_t index) {
    if (index > count)
        return 0.0f;
    return weights[index];
}

// FUNC_AT(0x000e8bb0)
float* EAGL::MorphWeights::GetWeights() {
    return weights;
}

// FUNC_AT(0x000e8bc0)
EAGL::MorphWeights* EAGL::MorphWeights::Construct() {
    count = 0xffffffff;
    pointers = NULL;
    weights = NULL;
    return this;
}

// ---- Model

// FUNC_AT(0x000e8b40)
void EAGLInternal::ModelDestructor(void *object) {
    (void)object;   // the loader's destructor callback for "Model": nothing to do
}

// FUNC_AT(0x000e8be0)
const char* EAGL::Model::GetName() {
    return name;
}

// FUNC_AT(0x000e8bf0)
EAGL::Model* EAGL::Model::GetChild(const char *childName) {
    for (Model *child = sameMatrixChildren; child != NULL; child = child->next)
        if (child->name != NULL && strcmp(childName, child->name) == 0)
            return child;
    for (Model *child = relativeChildren; child != NULL; child = child->next)
        if (child->name != NULL && strcmp(childName, child->name) == 0)
            return child;
    for (Model *child = ownMatrixChildren; child != NULL; child = child->next)
        if (child->name != NULL && strcmp(childName, child->name) == 0)
            return child;
    return NULL;
}

// FUNC_AT(0x000e8d00)
bool EAGL::Model::AddSameMatrixChild(Model *child) {
    if (child->next != NULL)
        return false;
    child->next = sameMatrixChildren;
    sameMatrixChildren = child;
    return true;
}

// FUNC_AT(0x000e8d30)
bool EAGL::Model::AddRelativeChild(Model *child) {
    if (child->next != NULL)
        return false;
    child->next = relativeChildren;
    relativeChildren = child;
    return true;
}

// FUNC_AT(0x000e8d60)
bool EAGL::Model::AddOwnMatrixChild(Model *child) {
    if (child->next != NULL)
        return false;
    child->next = ownMatrixChildren;
    ownMatrixChildren = child;
    return true;
}

// FUNC_AT(0x000e8d90)
EAGL::Model* EAGL::Model::RemoveChild(const char *childName) {
    Model **lists[3] = { &sameMatrixChildren, &relativeChildren, &ownMatrixChildren };
    for (int list = 0; list < 3; list++) {
        Model *previous = NULL;
        for (Model *child = *lists[list]; child != NULL; child = child->next) {
            if (child->name != NULL && strcmp(childName, child->name) == 0) {
                if (previous != NULL)
                    previous->next = child->next;
                else
                    *lists[list] = child->next;
                return child;
            }
            previous = child;
        }
    }
    return NULL;
}

// FUNC_AT(0x000e8f60)
uint8_t* EAGL::Model::GetGeometry(const char *geometryName) {
    for (int i = 0; i < geometryCount; i++)
        if (strcmp(geometryName, geometryNames[i]) == 0)
            return (uint8_t *)&enableTable[i + 1];   // one entry further on than Draw's indexing
    return NULL;
}

// FUNC_AT(0x000e8fe0)
float* EAGL::Model::GetModelMatrix() {
    return matrix;
}

// FUNC_AT(0x000e8ff0)
void EAGL::Model::SetModelMatrix(const float *m) {
    CopyMatrix(matrix, m);
}

// The SHAPE image named imageName: by its long name when that is at most four characters, else by its four-
// character name with trailing blanks removed. Quirk kept: the comparison string is cut at four characters by
// writing a NUL into it - into the file's long name itself when that is used (past its end when it is shorter).
// FUNC_AT(0x000e9010)
uint8_t* EAGL::ModelFindShape(uint8_t *shapes, const char *imageName) {
    const ShapeFile *file = (const ShapeFile *)shapes;
    int32_t count = file->count;
    for (int32_t i = 0; i < count; i++) {
        uint8_t *image = shapes + file->entries[i].offset;
        char shortName[8];
        char *n = (char *)SHAPE_longname(image);
        if (n == NULL || strlen(n) > 4) {
            SHAPE_name(shapes, i, (uint32_t *)shortName);
            for (char *c = shortName + 3; *c == ' ';) {
                *c = 0;
                c--;
                if (c < shortName)
                    break;
            }
            n = shortName;
        }
        n[4] = 0;
        if (strcmp(imageName, n) == 0)
            return image;
    }
    return NULL;
}

// Points every TAR list at the image of the same name in shapes (or, with no shapes, at nothing).
// FUNC_AT(0x000e90f0)
void EAGL::Model::SwapShapes(uint8_t *shapes) {
    const uint32_t *p = tarList;
    const char *listName = (const char *)*p++;
    while (listName != NULL) {
        uint8_t *image = NULL;
        if (shapes != NULL)
            image = ModelFindShape(shapes, listName);
        uint32_t n = *p++;
        if (n != 0) {
            do {
                TAR *tar = VariationOf((TAR *)*p++, variation);
                if (image != NULL)
                    tar->SwapShape(image);
                else if (shapes == NULL)
                    tar->SwapShape(NULL);
            } while (--n != 0);
        }
        listName = (const char *)*p++;
    }
}

// FUNC_AT(0x000e9170)
EAGL::TARList* EAGL::Model::GetTARList(TARList *out, const char *listName) {
    if (listName != NULL) {
        const uint32_t *p = tarList;
        const char *entryName = (const char *)*p++;
        while (entryName != NULL) {
            if (strcmp(entryName, listName) == 0) {
                out->variation = variation;
                out->count = *p;
                out->tars = (TAR **)(p + 1);
                return out;
            }
            p = p + *p + 1;
            entryName = (const char *)*p++;
        }
    }
    out->variation = -1;
    out->count = 0;
    out->tars = NULL;
    return out;
}

// FUNC_AT(0x000e9210)
void EAGL::Model::SetTextureList(const TARList *list, uint8_t *shape) {
    for (int32_t i = 0; i < list->count; i++)
        VariationOf(list->tars[i], variation)->SwapShape(shape);   // the model's variation, not the list's
}

// FUNC_AT(0x000ea1e0)
void EAGL::Model::SetTexture(const char *listName, uint8_t *shape) {
    TARList list;
    GetTARList(&list, listName);
    SetTextureList(&list, shape);
}

// FUNC_AT(0x000e9260)
void EAGL::Model::DrawInstances(const InstanceData *instances, int first, int count) {
    if (on == 0)
        return;
    InstanceStream *streams = instances->streams;
    int32_t streamCount = instances->streamCount;
    const InstanceEnable *enable = instances->enable;
    int end = first + count;
    if (first >= end)
        return;
    const float *instanceMatrix = instances->matrices + first * 16;
    for (int i = first; i < end; i++, instanceMatrix += 16) {
        if (enable[i].enable == 0)
            continue;
        CurrentVariation = enable[i].variation;
        if (streamCount > 0) {
            InstanceStream *stream = streams;
            for (int32_t n = streamCount; n != 0; n--, stream++) {
                if (stream->source != NULL) {
                    int stride = stream->stride;
                    MEM_copy(stream->destination, stream->source + stride * i, stride);
                }
            }
        }
        SetModelMatrices(instanceMatrix);
        WalkDrawList(this);
    }
}

// Compacts the draw list in place, once (the first entry is marked 0x10000000): each GeoPrim the render method
// keeps (RenderMethod::CanOptimize, which keeps them all) is copied down, and each block's word count rewritten.
// FUNC_AT(0x000e9410)
void EAGL::Model::Optimize() {
    uint32_t *in = drawList;
    uint32_t header = *in;
    uint32_t kept = 0;
    uint32_t *out = in;
    uint32_t limit = header & 0xffff;
    int index = 0;
    in++;
    uint32_t *countSlot = NULL;
    uint32_t low = limit;
    for (;;) {
        if (low == 0xffff) {
            if ((header & 0x10000000) != 0)
                return;
            header |= 0x10000000;
            in[-1] = header;
            GeoPrim *geoPrim = (GeoPrim *)*in;
            RenderMethod *renderMethod = geoPrim->method;
            in++;
            bool keep = renderMethod->CanOptimize(geoPrim, index);
            index++;
            if (keep) {
                out[0] = in[-2];
                out[1] = in[-1];
                out += 2;
                kept += 2;
            }
        } else {
            if (countSlot != NULL)
                *countSlot = kept;
            out[0] = in[-1];
            out[1] = in[0];
            out[2] = in[1];
            countSlot = &out[2];
            out += 3;
            in += 2;
            index = 0;
            kept = 0;
        }
        header = *in;
        low = header & 0xffff;
        in++;
        if (!(low > limit))
            break;
    }
    *out = in[-1];
    if (countSlot != NULL)
        *countSlot = kept;
}

// FUNC_AT(0x000ea1d0)
void EAGLInternal::ModelConstructor(EAGL::Model *model) {
    model->Optimize();   // the loader's constructor callback for "Model" (a tail jump in the original)
}

// FUNC_AT(0x000e94e0)
bool EAGL::Model::IsOn() {
    return on != 0;
}

// FUNC_AT(0x000e94f0)
void EAGL::Model::TurnOn() {
    on = 1;
}

// FUNC_AT(0x000e9500)
void EAGL::Model::TurnOff() {
    on = 0;
}

// FUNC_AT(0x000e9510)
void EAGL::Model::Patch(const uint32_t *patchList) {
    if (patchList == NULL)
        return;
    int32_t n = *patchList;
    if (n <= 0)
        return;
    const uint32_t *p = patchList + 1;
    do {
        *(uint32_t *)p[0] = p[1];   // {address, value}
        p += 2;
    } while (--n != 0);
}

// Quirk kept: +0x08, +0x4c..+0x9b (scale, offset, sphere), +0xac and +0xc8 are not copied.
// FUNC_AT(0x000e9540)
EAGL::Model* EAGL::Model::ConstructCopy(const Model *other) {
    unknown00 = other->unknown00;
    unknown04 = other->unknown04;
    CopyMatrix(matrix, other->matrix);
    geometryCount = other->geometryCount;
    geometryNames = other->geometryNames;
    sameMatrixChildren = other->sameMatrixChildren;
    ownMatrixChildren = other->ownMatrixChildren;
    next = other->next;
    name = other->name;
    morphData = other->morphData;
    tarList = other->tarList;
    variation = other->variation;
    enableTable = other->enableTable;
    drawList = other->drawList;
    patch = other->patch;
    morphWeights = other->morphWeights;
    return this;
}

// FUNC_AT(0x000e9600)
EAGL::Model* EAGL::Model::Construct() {
    geometryNames = NULL;
    enableTable = &defaultEnable;
    unknown00 = 0;
    unknown04 = 0;
    geometryCount = 0;
    sameMatrixChildren = NULL;
    ownMatrixChildren = NULL;
    next = NULL;
    name = NULL;
    morphData = NULL;
    tarList = NULL;
    variation = 0;
    drawList = NULL;
    patch = NULL;
    morphWeights = NULL;
    enableTable->enable = 1;   // the one geometry enabled
    for (int i = 0; i < 16; i++)
        matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    return this;
}

// FUNC_AT(0x000e96a0)
void EAGL::Model::Destruct() {
}

// FUNC_AT(0x000e96b0)
void EAGL::Model::SetMorphWeights(MorphWeights *weights) {
    morphWeights = weights;
}

// Copies each morph record's rest data back over its target ({tag, n, n records} ... 0 after a word).
// FUNC_AT(0x000e96c0)
void EAGL::Model::ClearMorphModel() {
    const uint32_t *p = (const uint32_t *)(morphData + 4);
    uint32_t tag = *p++;
    if (tag == 0)
        return;
    do {
        int32_t n = *p++;
        if (n > 0) {
            do {
                const MorphRecord *record = (const MorphRecord *)p;
                p += sizeof(MorphRecord) / 4;
                uint32_t bytes = (record->strideFlags >> 1) * record->elementSize;
                const uint8_t *source = (const uint8_t *)(record->target - record->variationStride);
                uint8_t *destination = (record->strideFlags & 1) != 0
                                           ? (uint8_t *)(variation * record->variationStride + record->target)
                                           : (uint8_t *)record->target;
                __movsd((unsigned long *)destination, (const unsigned long *)source, bytes >> 2);   // REP MOVSD
                __movsb(destination + (bytes & ~3u), source + (bytes & ~3u), bytes & 3);           // REP MOVSB
            } while (--n != 0);
        }
        tag = *p++;
    } while (tag != 0);
}

// FUNC_AT(0x000ea210)
void EAGL::Model::Draw(const float *parentMatrix) {
    if (on == 0)
        return;
    ViewPort *viewPort = CurrentViewPort();
    if (viewPort->GetEnableModelSphereCull() != 0) {
        // The bounding sphere's centre through the matrix, x87 order: ((z * m2x + y * m1x) + x * m0x) + m3x
        const float *m = parentMatrix;
        double x = sphereCentre[0], y = sphereCentre[1], z = sphereCentre[2];
        float centre[3];
        centre[0] = float(z * m[8] + y * m[4] + x * m[0] + m[12]);
        centre[1] = float(z * m[9] + y * m[5] + x * m[1] + m[13]);
        centre[2] = float(z * m[10] + y * m[6] + x * m[2] + m[14]);
        if (!viewPort->IsSphereInView(centre, sphereRadius))
            return;
    }
    SetModelMatrices(parentMatrix);
    if (patch != NULL)
        Patch(patch);
    CurrentVariation = variation;
    EAGLInternal::ModelSetScale(scale, offset);
    WalkDrawList(this);
    if (ownMatrixChildren == NULL && relativeChildren == NULL && sameMatrixChildren == NULL)
        return;
    for (Model *child = sameMatrixChildren; child != NULL; child = child->next)
        child->Draw(parentMatrix);
    for (Model *child = ownMatrixChildren; child != NULL; child = child->next)
        child->Draw(child->matrix);
    alignas(16) Transform t;
    for (Model *child = relativeChildren; child != NULL; child = child->next) {
        CopyMatrix(t.m, child->matrix);
        t.PrependMatrix(parentMatrix);
        child->Draw(t.m);
    }
}

// Adds each morph target's data, scaled by its weight, into the model (dead: nothing calls Call). The search for
// a record keeps its place between items and starts again only when an id is below it, as the original's does.
// Quirk kept: an item whose element size does not match its record is skipped by its unrounded size in words.
// FUNC_AT(0x000ea480)
void EAGL::Model::MorphModel() {
    MorphWeights *weightsObject = morphWeights;
    if (weightsObject == NULL)
        return;
    uint32_t groups = weightsObject->groupCount;
    const uint32_t *s = weightsObject->groups;
    if (groups == 0)
        return;
    do {
        const char *groupName = (const char *)*s;
        const uint32_t *m = (const uint32_t *)(morphData + 4);
        const char *target = (const char *)*m++;
        s++;
        bool found = false;
        while (target != NULL) {
            if (strcmp(groupName, target) == 0) {
                found = true;
                break;
            }
            uint32_t n = *m;
            target = (const char *)m[n * 4 + 1];
            m = m + n * 4 + 2;
        }
        if (!found) {
            // Skip the group's input. Quirk kept: items are stepped as 8 bytes plus data, not 16.
            uint32_t subBlocks = *s++;
            if (subBlocks != 0) {
                do {
                    uint32_t items = s[1];
                    s += 2;
                    while (items != 0) {
                        uint32_t words = ((*s & 0x0fffffff) + 3) >> 2;
                        items--;
                        s = s + words + 2;
                    }
                } while (--subBlocks != 0);
            }
            continue;
        }
        int32_t recordCount = *m;
        const MorphRecord *records = (const MorphRecord *)(m + 1);
        const MorphRecord *record = records;
        int32_t remaining = recordCount;
        uint32_t subBlocks = *s++;
        if (subBlocks == 0)
            continue;
        do {
            uint32_t weightIndex = s[0];
            float weight = morphWeights->weights[weightIndex];
            uint32_t items = s[1];
            s += 2;
            if (items == 0)
                continue;
            do {
                // An item: {components 31..30, type 29..28, size 27..0; element size; base offset; id}, its data
                uint32_t h = s[0];
                int components = h >> 30;
                uint32_t size = h & 0x0fffffff;
                if (components == 0)
                    components = 4;
                uint32_t type = (h >> 28) & 3;
                if (type == 0)
                    type = 4;
                uint32_t baseOffset = s[2];
                uint32_t elementSize = s[1];
                uint32_t perElement = size / elementSize;
                int32_t id = s[3];
                s += 4;
                uint32_t skipWords = size;
                int32_t key = record->id;
                if (id < key) {
                    record = records;
                    remaining = recordCount;
                    key = record->id;
                }
                if (id > key) {
                    for (;;) {
                        int32_t r = remaining;
                        if (r <= 0) {
                            if (r == 0)
                                goto skipItem;
                            goto haveRecord;
                        }
                        record++;
                        key = record->id;
                        remaining = r - 1;
                        if (!(id > key))
                            break;
                    }
                }
                if (remaining == 0)
                    goto skipItem;
            haveRecord:
                if (id < record->id)
                    goto skipItem;
                {
                    int32_t baseStride = record->strideFlags >> 1;
                    int32_t recordElementSize = record->elementSize;
                    if ((int32_t)elementSize == recordElementSize) {
                        skipWords = (size + 3) >> 2;
                        uint8_t *base = (uint8_t *)(record->variationStride * variation + record->target);
                        if (type == 1)
                            ModelMorphAddBytes(base, (const uint8_t *)s, components, elementSize, baseStride, weight,
                                               baseOffset, perElement);
                        else if (type == 2)
                            ModelMorphAddShorts(base, (const uint8_t *)s, components, elementSize, baseStride, weight,
                                                baseOffset, perElement);
                        else if (type == 4)
                            ModelMorphAddFloats(base, (const uint8_t *)s, components, elementSize, baseStride, weight,
                                                baseOffset, perElement);
                    }
                    s += skipWords;
                    continue;
                }
            skipItem:
                s += (size + 3) >> 2;
            } while (--items != 0);
        } while (--subBlocks != 0);
    } while (--groups != 0);
}

// FUNC_AT(0x000ea7b0)
void EAGL::Model::Call(const char *callName) {
    EAGL_ModelCallFence("PreMorph");
    if (strcmp(callName, "Morph") == 0) {
        MorphModel();
        EAGL_ModelCallFence(callName);
        return;
    }
    if (strcmp(callName, "ClearMorph") == 0)
        ClearMorphModel();
    EAGL_ModelCallFence(callName);
}

// FUNC_AT(0x000f0e40)
void EAGLInternal::ModelSetScale(const float *scale, const float *offset) {
    (void)scale;   // a bare RET on the Xbox
    (void)offset;
}

// ---- The morph adders: count rows of components elements, base + baseOffset advancing by baseStride bytes and
// source by sourceStride bytes, unrolled by four as the originals are. The integer ones add the product truncated
// (__ftol2) and keep the low byte or half; the float one rounds (s * w + d) to float.

// FUNC_AT(0x000ea940)
void EAGL::ModelMorphAddBytes(uint8_t *base, const uint8_t *source, int components, int count, int baseStride,
                              float weight, int baseOffset, int sourceStride) {
    uint8_t *destination = base + baseOffset;
    if (count == 0)
        return;
    uint32_t rows = count;
    do {
        int i = 0;
        if (components >= 4) {
            do {
                for (int k = 0; k < 4; k++)
                    destination[i + k] += (uint8_t)Truncate(source[i + k] * double(weight));
                i += 4;
            } while (i + 3 < components);
        }
        for (; i < components; i++)
            destination[i] += (uint8_t)Truncate(source[i] * double(weight));
        destination += baseStride;
        source += sourceStride;
    } while (--rows != 0);
}

// FUNC_AT(0x000eaa90)
void EAGL::ModelMorphAddShorts(uint8_t *base, const uint8_t *source, int components, int count, int baseStride,
                               float weight, int baseOffset, int sourceStride) {
    uint8_t *destination = base + baseOffset;
    if (count == 0)
        return;
    uint32_t rows = count;
    do {
        int16_t *d = (int16_t *)destination;
        const int16_t *src = (const int16_t *)source;
        int i = 0;
        if (components >= 4) {
            do {
                for (int k = 0; k < 4; k++)
                    d[i + k] = (int16_t)(d[i + k] + (int16_t)Truncate(src[i + k] * double(weight)));
                i += 4;
            } while (i + 3 < components);
        }
        for (; i < components; i++)
            d[i] = (int16_t)(d[i] + (int16_t)Truncate(src[i] * double(weight)));
        destination += baseStride;
        source += sourceStride;
    } while (--rows != 0);
}

// FUNC_AT(0x000eabd0)
void EAGL::ModelMorphAddFloats(uint8_t *base, const uint8_t *source, int components, int count, int baseStride,
                               float weight, int baseOffset, int sourceStride) {
    uint8_t *destination = base + baseOffset;
    if (count == 0)
        return;
    uint32_t rows = count;
    double w = weight;
    do {
        float *d = (float *)destination;
        const float *src = (const float *)source;
        int i = 0;
        if (components >= 4) {
            do {
                d[i] = float(src[i] * w + d[i]);
                d[i + 1] = float(src[i + 1] * w + d[i + 1]);
                d[i + 2] = float(src[i + 2] * w + d[i + 2]);
                d[i + 3] = float(w * src[i + 3] + d[i + 3]);
                i += 4;
            } while (i + 3 < components);
        }
        for (; i < components; i++)
            d[i] = float(src[i] * w + d[i]);
        destination += baseStride;
        source += sourceStride;
    } while (--rows != 0);
}

// ---- DynamicModel

// FUNC_AT(0x000e9760)
EAGL::DynamicModel* EAGL::DynamicModel::Construct() {
    unknown40 = 0;
    capacity = 0;
    count = 0;
    geoPrims = NULL;
    drawArrays = NULL;
    for (int i = 0; i < 16; i++)
        matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    return this;
}

// Grows both arrays to newCapacity (unreferenced; AddGeoPrim has its own copy with doubling).
// FUNC_AT(0x000e97b0)
void EAGL::DynamicModel::Reserve(int newCapacity) {
    int oldCapacity = capacity;
    if (newCapacity <= oldCapacity)
        return;
    capacity = newCapacity;
    GeoPrim **prims = (GeoPrim **)EaglMalloc(newCapacity * 4, NameGeoPrimList);
    for (int i = 0; i < count; i++)
        prims[i] = geoPrims[i];
    if (geoPrims != NULL)
        EaglFree(geoPrims, oldCapacity * 4);
    geoPrims = prims;
    DrawArray **arrays = (DrawArray **)EaglMalloc(capacity * 4, NameDrawArrayList);
    memset(arrays, 0, capacity * 4);
    for (int i = 0; i < count; i++)
        arrays[i] = drawArrays[i];
    if (drawArrays != NULL)
        EaglFree(drawArrays, oldCapacity * 4);
    drawArrays = arrays;
}

// FUNC_AT(0x000e9890)
int EAGL::DynamicModel::AddGeoPrim(GeoPrim *geoPrim) {
    int oldCapacity = capacity;
    if (count >= oldCapacity) {
        capacity = oldCapacity < 0x10 ? 0x10 : oldCapacity * 2;
        GeoPrim **prims = (GeoPrim **)EaglMalloc(capacity * 4, NameGeoPrimList2);
        for (int i = 0; i < count; i++)
            prims[i] = geoPrims[i];
        if (geoPrims != NULL)
            EaglFree(geoPrims, oldCapacity * 4);
        geoPrims = prims;
        DrawArray **arrays = (DrawArray **)EaglMalloc(capacity * 4, NameDrawArrayList2);
        memset(arrays, 0, capacity * 4);
        for (int i = 0; i < count; i++)
            arrays[i] = drawArrays[i];
        if (drawArrays != NULL)
            EaglFree(drawArrays, oldCapacity * 4);
        drawArrays = arrays;
    }
    geoPrims[count] = geoPrim;
    count++;
    return count - 1;
}

// FUNC_AT(0x000e9990)
void EAGL::DynamicModel::DrawNoTransform() {
    for (int i = 0; i < count; i++) {
        DrawArray *drawArray = drawArrays[i];
        if (drawArray != NULL) {
            drawArray->SetLocalMatrix(matrix);
            drawArray = drawArrays[i];
            drawArray->Draw(drawArray->numVerts);
        } else {
            geoPrims[i]->method->Draw(geoPrims[i]);
        }
    }
}

// FUNC_AT(0x000e99e0)
void EAGL::DynamicModel::Draw() {
    SetModelMatrices(matrix);
    CurrentVariation = 0;
    DrawNoTransform();
}

// FUNC_AT(0x000e9a70)
float* EAGL::DynamicModel::GetModelMatrix() {
    return matrix;
}

// FUNC_AT(0x000e9a80)
void EAGL::DynamicModel::SetModelMatrix(const float *m) {
    CopyMatrix(matrix, m);
}

// The vertex-array API below is dead (2.9): each creates the GeoPrim's DrawArray on first use, forwards to it and
// answers false.

// FUNC_AT(0x000e9aa0)
bool EAGL::DynamicModel::SetPrimitiveType(int index, int type) {
    DrawArrayFor(this, index)->SetPrimitiveType(type);
    return false;
}

// FUNC_AT(0x000e9b40)
int EAGL::DynamicModel::GetIndexFromName(int index, const char *varName) {
    return DrawArrayFor(this, index)->GetIndexFromName(varName);
}

// FUNC_AT(0x000e9be0)
bool EAGL::DynamicModel::SetVarByName(int index, const char *varName, void *data, int size) {
    DrawArrayFor(this, index)->SetVarByName(varName, (uint8_t *)data, 0, size);
    return false;
}

// FUNC_AT(0x000e9c90)
bool EAGL::DynamicModel::SetVar(int index, int var, void *data, int size) {
    DrawArrayFor(this, index)->SetVar(var, (uint8_t *)data, 0, size);
    return false;
}

// FUNC_AT(0x000e9d40)
bool EAGL::DynamicModel::SetStreamByName(int index, const char *varName, void *data, int size) {
    DrawArrayFor(this, index)->SetVarByName(varName, (uint8_t *)data, -1, size);
    return false;
}

// FUNC_AT(0x000e9df0)
bool EAGL::DynamicModel::SetStream(int index, int var, void *data, int size) {
    DrawArrayFor(this, index)->SetVar(var, (uint8_t *)data, -1, size);
    return false;
}

// FUNC_AT(0x000e9ea0)
bool EAGL::DynamicModel::SetParamName(int index, const char *paramName) {
    DrawArrayFor(this, index)->SetParamName(paramName);
    return false;
}

// FUNC_AT(0x000e9f40)
bool EAGL::DynamicModel::Lock(int index) {
    DrawArrayFor(this, index)->Lock();
    return false;
}

// FUNC_AT(0x000e9fe0)
bool EAGL::DynamicModel::Unlock(int index) {
    DrawArrayFor(this, index)->Unlock();
    return false;
}

// FUNC_AT(0x000ea080)
bool EAGL::DynamicModel::SetNumVerts(int index, int verts) {
    DrawArrayFor(this, index)->SetNumVerts(verts);
    return false;
}

// FUNC_AT(0x000ea720)
void EAGL::DynamicModel::Destruct() {
    if (geoPrims != NULL)
        EaglFree(geoPrims, capacity * 4);
    geoPrims = NULL;
    if (drawArrays != NULL) {
        for (int i = 0; i < count; i++) {
            DrawArray *drawArray = drawArrays[i];
            if (drawArray != NULL) {
                drawArray->Destruct();
                EaglFree(drawArray, sizeof(DrawArray));
            }
            drawArrays[i] = NULL;
        }
        EaglFree(drawArrays, capacity * 4);
    }
    drawArrays = NULL;
}

// ---- Unreferenced helpers

// FUNC_AT(0x000ea120)
bool EAGL::ModelHeaderBits::TestLowBits6() {
    return (bytes[6] & 3) != 0;
}

// FUNC_AT(0x000ea130)
void __stdcall EAGL::ModelCopyBytes(void *destination, const void *source, int bytes) {
    uint32_t *d = (uint32_t *)destination;
    const uint32_t *s = (const uint32_t *)source;
    if (bytes >= 0x20) {
        uint32_t blocks = (uint32_t)bytes >> 5;
        bytes -= blocks * 0x20;
        do {
            for (int k = 0; k < 8; k++)
                d[k] = s[k];
            d += 8;
            s += 8;
        } while (--blocks != 0);
    }
    if (bytes >= 8) {
        uint32_t pairs = (uint32_t)bytes >> 3;
        bytes -= pairs * 8;
        do {
            d[0] = s[0];
            d[1] = s[1];
            d += 2;
            s += 2;
        } while (--pairs != 0);
    }
    uint8_t *db = (uint8_t *)d;
    const uint8_t *sb = (const uint8_t *)s;
    for (; bytes > 0; bytes--)
        *db++ = *sb++;
}

// An iterator to the first entry whose name starts with prefix (strncmp over the prefix's length), or the end.
// FUNC_AT(0x000ea870)
EAGL::ModelNameEntry** EAGL::ModelNameTable::Find(ModelNameEntry **out, const char *prefix) {
    ModelNameEntry *entry = begin;
    while (entry != begin + count) {
        if (strncmp(prefix, entry->name, strlen(prefix)) == 0)
            break;
        entry++;
    }
    *out = entry;
    return out;
}

// The same from the back, as a reverse iterator: one past the entry found, or the end when there is none.
// FUNC_AT(0x000ea8d0)
EAGL::ModelNameEntry** EAGL::ModelNameTable::FindLast(ModelNameEntry **out, const char *prefix) {
    ModelNameEntry *stop = begin - 1;
    for (ModelNameEntry *entry = begin + count - 1; entry != stop; entry--) {
        if (strncmp(prefix, entry->name, strlen(prefix)) == 0) {
            *out = entry + 1;
            return out;
        }
    }
    *out = begin + count;
    return out;
}

// FUNC_AT(0x000eb020)
uint8_t* EAGL::ModelShapeImage::Data0() {
    if ((flags & kShapeDataAtOffset) != 0)
        return (uint8_t *)this + (uint32_t)dataOffset;
    return (uint8_t *)&dataOffset;   // the pixels straight after the 0x10-byte header
}

// FUNC_AT(0x000eb040)
uint8_t* EAGL::ModelShapeImage::Data1() {
    if ((flags & kShapeDataAtOffset) != 0)
        return (uint8_t *)this + (uint32_t)dataOffset;
    return (uint8_t *)&dataOffset;   // the pixels straight after the 0x10-byte header
}

// FUNC_AT(0x000eaf80)
void __stdcall EAGL::ModelIgnore4a(uint32_t unused) {
    (void)unused;
}

// FUNC_AT(0x000eafa0)
void __stdcall EAGL::ModelIgnore4b(uint32_t unused) {
    (void)unused;
}

// FUNC_AT(0x000eaff0)
uint32_t __stdcall EAGL::ModelReturnZero4(uint32_t unused) {
    (void)unused;
    return 0;
}

// ---- Register-argument code. Each naked entry passes its registers to a cdecl body here, which, like the
// original, clobbers only EAX, ECX and EDX. Not in an anonymous namespace: inline assembly calls them by name.

#define D3DSimpleStateMethods ((const uint32_t *)0x0018e888)   // the push-buffer method per simple render state
#define D3DDeferredStateDirty ((const uint32_t *)0x0018e668)   // the dirty bits per deferred render state
#define D3DTextureStateDirty ((const uint32_t *)0x0018e780)    // the dirty bits per texture state from 0xc

#define D3DDevice_SetTextureState_TexCoordIndex ((void (__stdcall *)(uint32_t stage, uint32_t value))0x00167c40)
#define D3DDevice_SetTextureState_BorderColor ((void (__stdcall *)(uint32_t stage, uint32_t value))0x00167dc0)
#define D3DDevice_SetTextureState_ColorKeyColor ((void (__stdcall *)(uint32_t stage, uint32_t value))0x00167e00)
#define D3DDevice_SetTextureState_BumpEnv ((void (__stdcall *)(uint32_t stage, uint32_t state, uint32_t value))0x00167d50)
#define D3DDevice_CreatePalette2 ((void *(__stdcall *)(uint32_t size))0x0016b510)
#define D3DResource_Register ((void (__stdcall *)(void *resource, void *base))0x001693a0)
#define D3D_Get2DSurfaceDesc ((void (__stdcall *)(void *container, uint32_t level, void *desc))0x00167320)
#define D3DSurface_GetDesc ((void (__stdcall *)(void *surface, void *desc))0x00167220)
#define D3DSurface_LockRect ((void (__stdcall *)(void *surface, void *locked, void *rect, uint32_t flags))0x00167240)

const uint32_t kOutOfMemory = 0x8007000e;   // E_OUTOFMEMORY

typedef void (__stdcall *RenderStateEntry)(uint32_t value);

// D3DDevice_SetRenderState by index, inlined: the simple states through SetRenderState_Simple and D3D8's table,
// the deferred ones into the table with their dirty bits, the rest through their own entry points.
static void ModelSetRenderStateBody(uint32_t state, uint32_t value) {
    if ((int32_t)state < 0x5c) {
        D3DDevice_SetRenderState_Simple(D3DSimpleStateMethods[state], value);
        D3DRenderState[state] = value;
        return;
    }
    if ((int32_t)state < 0x88) {
        D3DDirtyFlags |= D3DDeferredStateDirty[state];
        D3DRenderState[state] = value;
        return;
    }
    uintptr_t entry;
    switch (state) {
    case 0x88: entry = 0x001673b0; break;   // PSTextureModes
    case 0x89: entry = 0x00167bf0; break;   // VertexBlend
    case 0x8a: entry = 0x00167760; break;   // FogColor
    case 0x8b: entry = 0x00167ad0; break;   // FillMode
    case 0x8c: entry = 0x00167b20; break;   // BackFillMode
    case 0x8d: entry = 0x00167b80; break;   // TwoSidedLighting
    case 0x8e: entry = 0x00167860; break;   // NormalizeNormals
    case 0x8f: entry = 0x001687f0; break;   // ZEnable
    case 0x90: entry = 0x00168880; break;   // StencilEnable
    case 0x91: entry = 0x00168910; break;   // StencilFail
    case 0x93: entry = 0x001677b0; break;   // CullMode
    case 0x92: entry = 0x00167820; break;   // FrontFace
    case 0x94: entry = 0x001678a0; break;   // TextureFactor
    case 0x95: entry = 0x001679f0; break;   // ZBias
    case 0x96: entry = 0x00167a70; break;   // LogicOp
    case 0x97: entry = 0x001676e0; break;   // EdgeAntiAlias
    case 0x98: entry = 0x00168b70; break;   // MultiSampleAntiAlias
    case 0x99: entry = 0x00168bf0; break;   // MultiSampleMask
    case 0x9a: entry = 0x00168af0; break;   // MultiSampleMode
    case 0x9b: entry = 0x00168b30; break;   // MultiSampleRenderTargetMode
    case 0x9c: entry = 0x00167720; break;   // ShadowFunc
    case 0x9d: entry = 0x00167900; break;   // LineWidth
    case 0x9e: entry = 0x00168c40; break;   // SampleAlpha
    case 0x9f: entry = 0x00167970; break;   // Dxt1NoiseEnable
    case 0xa0: entry = 0x00168980; break;   // YuvEnable
    case 0xa1: entry = 0x001689b0; break;   // OcclusionCullEnable
    case 0xa2: entry = 0x00168a20; break;   // StencilCullEnable
    case 0xa3: entry = 0x00168a90; break;   // RopZCmpAlwaysRead
    case 0xa4: entry = 0x00168ab0; break;   // RopZRead
    case 0xa5: entry = 0x00168ad0; break;   // DoNotCullUncompressed
    default: return;
    }
    ((RenderStateEntry)entry)(value);
}

// D3DDevice_SetTextureStageState, inlined: the deferred states into D3D8's texture-stage table with their dirty
// bits, texture-coordinate index, border and colour-key colours and the bump-environment states through their
// entry points.
static void ModelSetTextureStateBody(uint32_t state, uint32_t stage, uint32_t value) {
    if ((int32_t)state < 0x16) {
        uint32_t dirty = (int32_t)state < 0xc ? 1u << (stage & 31) : D3DTextureStateDirty[state];
        D3DDirtyFlags |= dirty;
        D3DTextureState[stage][state] = value;
        return;
    }
    if (state == 0x1c)
        D3DDevice_SetTextureState_TexCoordIndex(stage, value);
    else if (state == 0x1d)
        D3DDevice_SetTextureState_BorderColor(stage, value);
    else if (state == 0x1e)
        D3DDevice_SetTextureState_ColorKeyColor(stage, value);
    else if ((int32_t)state <= 0x1b)
        D3DDevice_SetTextureState_BumpEnv(stage, state, value);
}

static uint32_t ModelCreatePaletteBody(uint32_t size, void **out) {
    void *palette = D3DDevice_CreatePalette2(size);
    *out = palette;
    return palette != NULL ? 0 : kOutOfMemory;
}

static void ModelRegisterBody(void *resource, void *base) {
    D3DResource_Register(resource, base);
}

static uint32_t ModelGet2DSurfaceDescBody(void *container, uint32_t level, void *desc) {
    D3D_Get2DSurfaceDesc(container, level, desc);
    return 0;
}

static uint32_t ModelGetSurfaceLevelBody(void *texture, uint32_t level, void **out) {
    void *surface = D3DTexture_GetSurfaceLevel2(texture, level);
    *out = surface;
    return surface != NULL ? 0 : kOutOfMemory;
}

static uint32_t ModelReleaseBody(void *resource) {
    return D3DResource_Release(resource);
}

static uint32_t ModelSurfaceGetDescBody(void *surface, void *desc) {
    D3DSurface_GetDesc(surface, desc);
    return 0;
}

static uint32_t ModelLockRectBody(void *surface, void *locked, void *rect, uint32_t flags) {
    D3DSurface_LockRect(surface, locked, rect, flags);
    return 0;
}

// AUTOLTCG
__declspec(naked) void FUN_000e8b20() {
    __asm {
        mov eax, dword ptr [eax + 8]
        ret
    }
}

// AUTOLTCG
__declspec(naked) void FUN_000e8b30() {
    __asm {
        mov eax, dword ptr [ecx + eax * 8 + 0x14]
        add eax, ecx
        ret
    }
}

// FUNC_AT(0x000eaca0)
__declspec(naked) void EAGL::ModelSetRenderStateRegs() {
    __asm {
        push edi
        push esi
        call ModelSetRenderStateBody
        add esp, 8
        ret
    }
}

// FUNC_AT(0x000eaea0)
__declspec(naked) void EAGL::ModelSetTextureStateRegs() {
    __asm {
        push edx
        push ecx
        push eax
        call ModelSetTextureStateBody
        add esp, 0xc
        ret
    }
}

// FUNC_AT(0x000eaf20)
__declspec(naked) void EAGL::ModelCreatePaletteRegs() {
    __asm {
        push dword ptr [esp + 8]
        push eax
        call ModelCreatePaletteBody
        add esp, 8
        ret 8
    }
}

// FUNC_AT(0x000eaf40)
__declspec(naked) void EAGL::ModelRegisterRegs() {
    __asm {
        push eax
        push ecx
        call ModelRegisterBody
        add esp, 8
        ret
    }
}

// FUNC_AT(0x000eaf50)
__declspec(naked) void EAGL::ModelGet2DSurfaceDescRegs() {
    __asm {
        push eax
        push ecx
        push edx
        call ModelGet2DSurfaceDescBody
        add esp, 0xc
        ret
    }
}

// FUNC_AT(0x000eaf60)
__declspec(naked) void EAGL::ModelGetSurfaceLevelRegs() {
    __asm {
        push dword ptr [esp + 4]
        push eax
        push ecx
        call ModelGetSurfaceLevelBody
        add esp, 0xc
        ret 4
    }
}

// FUNC_AT(0x000eaf90)
__declspec(naked) void EAGL::ModelReleaseRegsA() {
    __asm {
        push eax
        call ModelReleaseBody
        add esp, 4
        ret
    }
}

// FUNC_AT(0x000eafb0)
__declspec(naked) void EAGL::ModelReleaseRegsB() {
    __asm {
        push eax
        call ModelReleaseBody
        add esp, 4
        ret
    }
}

// FUNC_AT(0x000eafc0)
__declspec(naked) void EAGL::ModelSurfaceGetDescRegs() {
    __asm {
        push eax
        push ecx
        call ModelSurfaceGetDescBody
        add esp, 8
        ret
    }
}

// FUNC_AT(0x000eafd0)
__declspec(naked) void EAGL::ModelLockRectRegs() {
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [esp + 0x10]
        call ModelLockRectBody
        add esp, 0x10
        ret 4
    }
}

// FUNC_AT(0x000eb000)
__declspec(naked) void EAGL::ModelWordAt4Regs() {
    __asm {
        movsx eax, word ptr [eax + 4]
        ret
    }
}

// FUNC_AT(0x000eb010)
__declspec(naked) void EAGL::ModelWordAt6Regs() {
    __asm {
        movsx eax, word ptr [eax + 6]
        ret
    }
}

// FUNC_AT(0x000eb060)
__declspec(naked) void EAGL::ModelShapeEntryRegs() {
    __asm {
        mov eax, dword ptr [ecx + eax * 8 + 0x14]
        add eax, ecx
        ret
    }
}
