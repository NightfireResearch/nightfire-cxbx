#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// The GameCube's GeoPrims as the Xbox's (docs/gc-driving.md, "GeoPrims"). A GameCube GeoPrim is {render method,
// pcode, then a {count, pointer} pair for each field the method's *_FieldNames table lists}: its counts big-endian, its
// pointers relocation sites the loader has filled. The arrays are big-endian too: positions as floats or (the "Packed"
// methods) shorts in 1/256ths, 16-bit texture coordinates, vertex colours (RGBA8, or RGB565 for the packed methods),
// and a GX display list - triangle strips (0x98) and triangles (0x90), each vertex a 16-bit index per attribute
// (position, normal, colour, texture coordinates as the method has them), every attribute's index the same on this
// disc. An Xbox GeoPrim of the mapped method is made from that: its arrays, indices joining the strips, and the
// parameters the table lists, and the draw list is pointed at it.

#include "GcData.h"
#include "GcBytes.h"
#include "GcRenderMethods.h"

#include "../data/RCARPFile.h"   // ResolveEAGLReferences
#include "../eagl/GeoPrimState.h"
#include "../eagl/Loader.h"
#include "../eagl/Model.h"
#include "../eagl/RenderMethod.h"
#include "../gfx/D3D8.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

using EAGL::GeoPrim;
using EAGL::GeoPrimState;
using EAGL::Model;
using EAGL::RenderMethod;
using EAGL::StaticVertexBuffer;
using EAGL::StreamDescription;

namespace {

// A GameCube GeoPrim: one more word than the Xbox's before its fields
struct GcGeoPrim {
    Ptr32<const void> method;    // +0x00 resolved to its GcRenderMethod entry
    Ptr32<const uint8_t> pcode;  // +0x04
    struct Field {
        uint32_t count;          // big-endian
        Ptr32<const uint8_t> data;
    } fields[1];                 // +0x08
};

// The GX's primitive commands in the display lists (the low three bits the vertex format)
enum GxCommand : uint8_t {
    kGxNop = 0x00,
    kGxTriangles = 0x90,
    kGxTriangleStrip = 0x98,
};
constexpr uint8_t kGxPrimitiveMask = 0xf8;
constexpr int kMaxAttributes = 6;

struct GxPrimitive {
    bool strip;
    std::vector<uint16_t> indices;
};

// The display list's primitives, each vertex `attributes` 16-bit indices (the first kept), or false when it does
// not parse so or an index is out of range
bool ParseDisplayList(const uint8_t *list, uint32_t bytes, int attributes, uint32_t vertices,
                      std::vector<GxPrimitive> *out) {
    out->clear();
    for (uint32_t p = 0; p < bytes;) {
        uint8_t command = list[p];
        if (command == kGxNop) {
            p++;
            continue;
        }
        uint8_t kind = command & kGxPrimitiveMask;
        if ((kind != kGxTriangleStrip && kind != kGxTriangles) || p + 3 > bytes)
            return false;
        uint32_t count = GcBe16(list + p + 1);
        p += 3;
        if (p + count * attributes * 2 > bytes)
            return false;
        GxPrimitive primitive;
        primitive.strip = kind == kGxTriangleStrip;
        for (uint32_t v = 0; v < count; v++) {
            const uint8_t *vertex = list + p + v * attributes * 2;
            for (int a = 0; a < attributes; a++)
                if (GcBe16(vertex + a * 2) >= vertices)
                    return false;
            primitive.indices.push_back(GcBe16(vertex));
        }
        p += count * attributes * 2;
        out->push_back(primitive);
    }
    return !out->empty();
}

// The index list: as triangles for a GeoPrimState whose primitive type is a list, else one strip, the primitives
// joined by repeating the last index and the first (and the first again to keep the winding on an even place)
void BuildIndices(const std::vector<GxPrimitive> &primitives, bool list, std::vector<uint16_t> *out) {
    for (const GxPrimitive &primitive : primitives) {
        const std::vector<uint16_t> &v = primitive.indices;
        if (v.size() < 3)
            continue;
        if (list) {
            if (!primitive.strip) {
                out->insert(out->end(), v.begin(), v.end() - v.size() % 3);
                continue;
            }
            for (size_t i = 0; i + 2 < v.size(); i++) {
                bool odd = (i & 1) != 0;
                out->push_back(v[i]);
                out->push_back(odd ? v[i + 2] : v[i + 1]);
                out->push_back(odd ? v[i + 1] : v[i + 2]);
            }
            continue;
        }
        size_t triangles = primitive.strip ? 1 : v.size() / 3;
        for (size_t t = 0; t < triangles; t++) {
            size_t first = primitive.strip ? 0 : t * 3;
            size_t end = primitive.strip ? v.size() : first + 3;
            if (!out->empty()) {
                out->push_back(out->back());
                out->push_back(v[first]);
                if ((out->size() & 1) != 0)
                    out->push_back(v[first]);
            }
            out->insert(out->end(), v.begin() + first, v.begin() + end);
        }
    }
}

// ---- what the conversion builds

// What opcode 9 finds 0x10 bytes into its parameter's data (the parameter's count 0): a stream source over one of
// the GeoPrim's arrays, as the Xbox's models hold them (their VertexBuffer symbols)
struct InlineStream {
    uint32_t unused00[4];            // +0x00
    StaticVertexBuffer source;       // +0x10
    D3DResource header;              // +0x24
};
static_assert(sizeof(InlineStream) == kInlineStreamStride, "a variation's stream source each 0x30 bytes");

constexpr uint32_t kVertexBufferCommon = 1;      // the Xbox's model vertex buffers' Common word
constexpr uint32_t kPrimitiveTriangleList = 5;   // a GeoPrimState's primitive type
constexpr uint32_t kWhite = 0xff808080;          // the Xbox's packed methods take 0x80 as 1.0

struct Vertex {
    float position[3];
};

// A block the GeoPrim's pieces are carved from, 16-byte aligned
class Arena {
public:
    explicit Arena(size_t size) : base_(static_cast<uint8_t *>(calloc(1, size + 16))), used_(0) {}
    template <class T> T *Take(size_t count) {
        T *p = reinterpret_cast<T *>(base_ + used_);
        used_ += (count * sizeof(T) + 15) & ~size_t(15);
        return p;
    }
    bool Valid() const { return base_ != NULL; }
private:
    uint8_t *base_;
    size_t used_;
};

// A colour of the GameCube's (1.0 as 0xff) as the Xbox packed methods' (1.0 as 0x80)
uint32_t Halve(uint32_t r, uint32_t g, uint32_t b) {
    return 0xff000000u | (r * 0x80 / 0xff) << 16 | (g * 0x80 / 0xff) << 8 | (b * 0x80 / 0xff);
}

uint32_t Expand5(uint32_t v) {
    return v << 3 | v >> 2;
}

uint32_t Expand6(uint32_t v) {
    return v << 2 | v >> 4;
}

class Converter {
public:
    explicit Converter(DynamicLoader *loader) : loader_(loader), counts_() {}
    void Run();

private:
    GeoPrim *Convert(const GcGeoPrim *gc);
    void *Resolve(const char *name);

    DynamicLoader *loader_;
    std::map<const GcGeoPrim *, GeoPrim *> converted_;
    std::set<const uint32_t *> lists_;
    std::map<std::string, void *> resolved_;
    int counts_[4];   // converted, not drawn by mapping, unknown method, unreadable
};

void *Converter::Resolve(const char *name) {
    auto it = resolved_.find(name);
    if (it != resolved_.end())
        return it->second;
    bool found = false;
    void *value = ResolveEAGLReferences(name, &found);
    if (value == NULL)
        value = DynamicLoader::GetRegisteredVar(name, &found);
    resolved_[name] = value;
    return value;
}

GeoPrim *Converter::Convert(const GcGeoPrim *gc) {
    GcRenderMethod *entry = Gc_FindRenderMethod(gc->method);
    if (entry == NULL) {
        counts_[2]++;
        return NULL;
    }
    RenderMethod *method = Gc_XboxMethod(entry);
    if (method == NULL) {
        counts_[1]++;
        return NULL;
    }
    auto field = [&](int index) -> const GcGeoPrim::Field * {
        return index >= 0 && index < entry->fieldCount ? &gc->fields[index] : NULL;
    };
    const GcGeoPrim::Field *coordinates = field(entry->coordinates), *list = field(entry->displayList);
    const GcGeoPrim::Field *uv = field(entry->uv), *colourField = field(entry->colours);
    const GcGeoPrim::Field *texture = field(entry->texture), *stateField = field(entry->state);
    if (coordinates == NULL || coordinates->data == NULL || list == NULL || list->data == NULL ||
        texture == NULL || texture->data == NULL) {
        counts_[3]++;
        return NULL;
    }
    uint32_t vertexCount = GcBe32(reinterpret_cast<const uint8_t *>(&coordinates->count));
    uint32_t listBytes = GcBe32(reinterpret_cast<const uint8_t *>(&list->count));
    std::vector<GxPrimitive> primitives;
    bool parsed = false;
    for (int attributes = 1; attributes <= kMaxAttributes && !parsed; attributes++)
        parsed = ParseDisplayList(list->data, listBytes, attributes, vertexCount, &primitives);
    GeoPrimState *state = stateField != NULL ? reinterpret_cast<GeoPrimState *>(const_cast<uint8_t *>(
                                                   static_cast<const uint8_t *>(stateField->data)))
                                             : NULL;
    if (state == NULL)
        state = static_cast<GeoPrimState *>(Resolve("STATE::texalpha=noA;primtype=tristrip"));
    if (!parsed || vertexCount == 0 || state == NULL) {
        counts_[3]++;
        return NULL;
    }
    std::vector<uint16_t> indices;
    BuildIndices(primitives, state->primitiveType == kPrimitiveTriangleList, &indices);
    if (indices.empty()) {
        counts_[3]++;
        return NULL;
    }

    size_t bytes = 4 + entry->paramCount * 8 + sizeof(StreamDescription) + indices.size() * 2 +
                   vertexCount * (sizeof(Vertex) + 8) + 3 * sizeof(InlineStream) + 16 * 8;
    Arena arena(bytes);
    if (!arena.Valid())
        return NULL;
    GeoPrim *prim = arena.Take<GeoPrim>(0);
    arena.Take<uint32_t>(1 + entry->paramCount * 2);
    StreamDescription *description = arena.Take<StreamDescription>(1);
    description->vertexCount = uint32_t(indices.size());
    uint16_t *indexData = arena.Take<uint16_t>(indices.size());
    memcpy(indexData, indices.data(), indices.size() * 2);

    Vertex *vertices = arena.Take<Vertex>(vertexCount);
    const uint8_t *src = coordinates->data;
    for (uint32_t i = 0; i < vertexCount; i++) {
        for (int k = 0; k < 3; k++) {
            if (entry->positions == kPositionsShort)
                vertices[i].position[k] = int16_t(GcBe16(src + (i * 3 + k) * 2)) / 256.0f;
            else
                vertices[i].position[k] = GcBeFloat(src + (i * 3 + k) * 4);
        }
    }
    int16_t *uvs = arena.Take<int16_t>(vertexCount * 2);
    if (uv != NULL && uv->data != NULL) {
        uint32_t uvCount = GcBe32(reinterpret_cast<const uint8_t *>(&uv->count));
        for (uint32_t i = 0; i < vertexCount && i < uvCount; i++)
            for (int k = 0; k < 2; k++)
                uvs[i * 2 + k] = int16_t(int16_t(GcBe16(uv->data + (i * 2 + k) * 2)) >> entry->uvShift);
    }
    uint32_t *colours = arena.Take<uint32_t>(vertexCount);
    uint32_t colourCount = colourField != NULL && colourField->data != NULL
                               ? GcBe32(reinterpret_cast<const uint8_t *>(&colourField->count)) : 0;
    for (uint32_t i = 0; i < vertexCount; i++) {
        colours[i] = kWhite;
        if (i >= colourCount)
            continue;
        const uint8_t *c = colourField->data;
        if (entry->colourFormat == kColoursRGB565) {
            uint16_t v = GcBe16(c + i * 2);
            colours[i] = Halve(Expand5(v >> 11), Expand6(v >> 5 & 63), Expand5(v & 31));
        } else if (entry->colourFormat == kColoursRGBA8) {
            colours[i] = Halve(c[i * 4], c[i * 4 + 1], c[i * 4 + 2]);
        }
    }
    auto streamOver = [](InlineStream *s, void *data, uint32_t size, uint32_t stride) {
        s->source.buffer = &s->header;
        s->source.size = size;
        s->source.stride = stride;
        s->source.user = 1;
        s->header.common = kVertexBufferCommon;
        s->header.data = 0;
        s->header.lock = 0;
        D3DResource_Register(&s->header, data);
    };
    InlineStream *positionStream = arena.Take<InlineStream>(1);
    streamOver(positionStream, vertices, vertexCount * sizeof(Vertex), sizeof(Vertex));
    InlineStream *texCoordStream = arena.Take<InlineStream>(1);
    streamOver(texCoordStream, uvs, vertexCount * 4, 4);
    InlineStream *colourStream = arena.Take<InlineStream>(1);
    streamOver(colourStream, colours, vertexCount * 4, 4);

    prim->method = method;
    for (int i = 0; i < entry->paramCount; i++) {
        const GcParam &p = entry->params[i];
        EAGL::GeoPrimParam &param = prim->params[i];
        switch (p.kind) {
            case kParamState:
                param.Set(1, state);
                break;
            case kParamTexture:
                param.Set(1, texture->data);
                break;
            case kParamStream:
                param.Set(1, description);
                break;
            case kParamIndices:
                param.Set(uint32_t(indices.size()), indexData);
                break;
            case kParamPositions:
                param.Set(0, positionStream);
                break;
            case kParamTexCoords:
                param.Set(0, texCoordStream);
                break;
            case kParamColours:
                param.Set(0, colourStream);
                break;
            case kParamVariable: {
                static float zero[4 * 8];
                void *value = Resolve(p.name);
                param.Set(p.count, value != NULL ? value : zero);
                break;
            }
            case kParamConstant:
                param.Set(1, p.constant);
                break;
        }
    }
    counts_[0]++;
    return prim;
}

void Converter::Run() {
    int index = 0;
    LoaderSymbol s;
    int models = 0;
    while (loader_->GetNextSymbol("Model", &index, &s)) {
        Model *model = static_cast<Model *>(s.address);
        uint32_t *p = model->drawList;
        if (p == NULL || !lists_.insert(p).second)   // models may share a list
            continue;
        models++;
        // Every entry of the list, the blocks a geometry's enable word may skip included (WalkDrawList)
        uint32_t limit = *p++ & 0xffff;
        uint32_t entry = limit;
        do {
            if (entry == 0xffff) {
                const GcGeoPrim *gc = PtrFrom32<const GcGeoPrim>(*p);
                if (gc != NULL) {
                    auto done = converted_.find(gc);
                    GeoPrim *prim = done != converted_.end() ? done->second : Convert(gc);
                    converted_[gc] = prim;
                    *p = PtrTo32(prim);
                }
                p++;
            } else {
                p += 2;
            }
            entry = *p++ & 0xffff;
        } while (entry > limit);
    }
    printf("[gc] %d models: %d GeoPrims converted, %d not drawn (mapped to none), %d of unknown methods, "
           "%d unreadable\n", models, counts_[0], counts_[1], counts_[2], counts_[3]);
}

}  // namespace

void Gc_ConvertModels(DynamicLoader *loader) {
    if (loader == NULL)
        return;
    Converter(loader).Run();
}
