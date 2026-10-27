#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// The PS2's GeoPrims as the Xbox's (docs/ps2-driving.md, "GeoPrims"). A PS2 model's draw list points at GeoPrims
// {render method, 0}: the render method is a child of one of the PS2's (Ps2RenderMethods.cpp) whose packets are a
// compiled program naming only what changed since the GeoPrim before it in the draw list (TARs, the GeoPrimState)
// and, after a 0x04000002 word, a table of DMA CALL tags, one a variation. Each calls a block that unpacks that
// variation's vertex colours (V3-8, masked) and calls the geometry: a DMA chain ending in a RET whose VIF packets
// unpack a header (V4-32: the vertex count), three V4-8 words of strip starts (bytes, three to a vertex, zero
// ended), the positions (V3-16 in 1/256ths offset by 0x8000, or V3-32 / V4-32 floats), the texture coordinates
// (V2-16 in 1/256ths offset by 0x8000, or V2-32 floats) and, for methods with no colour per variation, the colours.
// An Xbox GeoPrim of the mapped method is made from that: its arrays, indices joining the strips, and the
// parameters the table lists, and the draw list is pointed at it.

#include "Ps2Data.h"
#include "Ps2RenderMethods.h"

#include "../data/RCARPFile.h"   // ResolveEAGLReferences
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

// ---- the PS2's records

struct Ps2RenderMethodRecord {   // the PS2's EAGL::RenderMethod, as a GeoPrim's child method
    Ptr32<const uint32_t> program;   // +0x00 its compiled packets
    uint32_t unknown04;              // +0x04
    Ptr32<const void> parent;        // +0x08 the PS2 render method (resolved to its Ps2RenderMethod entry)
    uint32_t unknown0c;              // +0x0c
    uint32_t unknown10;              // +0x10
    Ptr32<const char> name;          // +0x14
};

struct Ps2GeoPrim {
    Ptr32<const Ps2RenderMethodRecord> method;   // +0x00
    uint32_t unknown04;                          // +0x04
};

constexpr uint32_t kProgramVariations = 0x04000002;   // the program word before the variation table

// A DMA tag: quadwords and id in the low word, the address (relocated by the loader) in the next; two VIF codes
// fill its upper half
struct DmaTag {                      // 0x10
    uint32_t control;                // +0x00 bits 0..15 the quadwords that follow, 28..30 the id
    Ptr32<const DmaTag> address;     // +0x04
    uint32_t vif[2];                 // +0x08

    uint32_t Quadwords() const { return control & 0xffff; }
    uint32_t Id() const { return control >> 28 & 7; }
};
static_assert(sizeof(DmaTag) == 0x10, "a DMA tag is a quadword");

enum DmaTagId : uint32_t {
    kDmaCnt = 1,
    kDmaCall = 5,
    kDmaRet = 6,
};

enum VifCommand : uint32_t {
    kVifStMask = 0x20,
    kVifStRow = 0x30,
    kVifStCol = 0x31,
    kVifDirect = 0x50,
    kVifDirectHl = 0x51,
    kVifUnpack = 0x60,               // 0x60..0x7f
};

// An UNPACK's format: components - 1 in bits 2..3, the component size in bits 0..1 (32, 16, 8 bits)
enum UnpackFormat : uint8_t {
    kV2_32 = 0x4, kV2_16 = 0x5,
    kV3_32 = 0x8, kV3_16 = 0x9, kV3_8 = 0xa,
    kV4_32 = 0xc, kV4_16 = 0xd, kV4_8 = 0xe,
};

struct Unpack {
    uint8_t format;
    bool masked;                     // the command's 0x10: written through STMASK (the colours; normals are not)
    bool isUnsigned;                 // the 0x4000 flag of the address: zero-extended
    uint16_t address;                // in VU memory, quadwords
    uint32_t count;
    const uint8_t *data;
};

uint32_t UnpackBytes(uint8_t format, uint32_t count) {
    if ((format & 3) == 3)                       // V4-5: 16 bits an element
        return (count * 2 + 3) & ~3u;
    uint32_t components = (format >> 2 & 3) + 1;
    uint32_t bits = 32 >> (format & 3);
    return (count * components * bits / 8 + 3) & ~3u;
}

// The UNPACKs of a run of VIF codes
void ReadVif(const uint8_t *p, const uint8_t *end, std::vector<Unpack> *out) {
    while (p + 4 <= end) {
        uint32_t code;
        memcpy(&code, p, 4);
        p += 4;
        uint32_t command = code >> 24 & 0x7f;
        uint32_t count = code >> 16 & 0xff;
        uint32_t immediate = code & 0xffff;
        if ((command & kVifUnpack) == kVifUnpack) {
            Unpack u;
            u.format = uint8_t(command & 0xf);
            u.masked = (command & 0x10) != 0;
            u.isUnsigned = (immediate & 0x4000) != 0;
            u.address = uint16_t(immediate & 0x3ff);
            u.count = count != 0 ? count : 256;
            u.data = p;
            out->push_back(u);
            p += UnpackBytes(u.format, u.count);
        } else if (command == kVifStMask) {
            p += 4;
        } else if (command == kVifStRow || command == kVifStCol) {
            p += 16;
        } else if (command == kVifDirect || command == kVifDirectHl) {
            p += immediate * 16;
        }
    }
}

// The UNPACKs of the packets after a tag, up to its quadwords' end
void ReadTag(const DmaTag *tag, std::vector<Unpack> *out) {
    const uint8_t *start = reinterpret_cast<const uint8_t *>(tag->vif);
    ReadVif(start, start + 8 + tag->Quadwords() * 16, out);
}

// The geometry chain: its tags' UNPACKs to the RET
void ReadChain(const DmaTag *tag, std::vector<Unpack> *out) {
    for (int guard = 0; guard < 64; guard++) {
        ReadTag(tag, out);
        if (tag->Id() != kDmaCnt)
            return;
        tag += 1 + tag->Quadwords();
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
constexpr int kMaxTextures = 4;

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

uint32_t Colour(const uint8_t *rgb) {
    return 0xff000000u | uint32_t(rgb[0]) << 16 | uint32_t(rgb[1]) << 8 | rgb[2];
}

int16_t TexCoordFromFloat(float f) {
    float scaled = floorf(f * 256 + 0.5f);
    return int16_t((std::max)(-32768.0f, (std::min)(32767.0f, scaled)));
}

// The index list: each strip in turn, joined to the one before by repeating the last index and the first (and the
// first again to keep the strip's winding on an even place), or as triangles for a list
void BuildIndices(const std::vector<std::pair<uint32_t, uint32_t>> &strips, bool list, std::vector<uint16_t> *out) {
    for (const auto &s : strips) {
        if (s.second - s.first < 3)
            continue;
        if (list) {
            for (uint32_t i = s.first; i + 2 < s.second; i++) {
                bool odd = ((i - s.first) & 1) != 0;
                out->push_back(uint16_t(i));
                out->push_back(uint16_t(odd ? i + 2 : i + 1));
                out->push_back(uint16_t(odd ? i + 1 : i + 2));
            }
            continue;
        }
        if (!out->empty()) {
            out->push_back(out->back());
            out->push_back(uint16_t(s.first));
            if ((out->size() & 1) != 0)
                out->push_back(uint16_t(s.first));
        }
        for (uint32_t i = s.first; i < s.second; i++)
            out->push_back(uint16_t(i));
    }
}

class Converter {
public:
    explicit Converter(DynamicLoader *loader);
    void Run();

private:
    struct Current {
        void *textures[kMaxTextures];
        int textureCount;
        GeoPrimState *state;
    };

    GeoPrim *Convert(const Ps2GeoPrim *ps2, Current *current);
    const DmaTag *ReadProgram(const uint32_t *program, Current *current);
    void *Resolve(const char *name);
    bool IsTexture(const void *p) const { return std::binary_search(textures_.begin(), textures_.end(), p); }
    bool IsState(const void *p) const { return std::binary_search(states_.begin(), states_.end(), p); }

    DynamicLoader *loader_;
    std::vector<const void *> textures_, states_;
    std::map<const Ps2GeoPrim *, GeoPrim *> converted_;
    std::set<const uint32_t *> lists_;
    std::map<std::string, void *> resolved_;
    int counts_[4];   // converted, not drawn by mapping, unknown method, unreadable
};

Converter::Converter(DynamicLoader *loader) : loader_(loader), counts_() {
    LoaderSymbol s;
    for (int i = 0; loader->GetSymbol(&s, i)->name != NULL; i++) {
        if (strcmp(s.className, "EAGL::TAR") == 0)
            textures_.push_back(s.address);
        else if (strcmp(s.className, "EAGL::GeoPrimState") == 0)
            states_.push_back(s.address);
    }
    std::sort(textures_.begin(), textures_.end());
    std::sort(states_.begin(), states_.end());
}

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

// The TARs and GeoPrimState the program names (replacing the current ones), and its variation table
const DmaTag *Converter::ReadProgram(const uint32_t *program, Current *current) {
    int textures = 0;
    bool stateSeen = false;
    for (int i = 0; i < 256; i++) {
        uint32_t word = program[i];
        if (word == kProgramVariations)
            return PtrFrom32<const DmaTag>(program[i + 1]);
        const void *p = PtrFrom32<const void>(word);
        if (IsTexture(p)) {
            if (textures == 0)
                current->textureCount = 0;
            if (current->textureCount < kMaxTextures)
                current->textures[current->textureCount++] = const_cast<void *>(p);
            textures++;
        } else if (IsState(p) && !stateSeen) {
            current->state = static_cast<GeoPrimState *>(const_cast<void *>(p));
            stateSeen = true;
        }
    }
    return NULL;
}

GeoPrim *Converter::Convert(const Ps2GeoPrim *ps2, Current *current) {
    const Ps2RenderMethodRecord *record = ps2->method;
    Ps2RenderMethod *entry = record != NULL ? Ps2_FindRenderMethod(record->parent) : NULL;
    if (entry == NULL) {
        counts_[2]++;
        return NULL;
    }
    const DmaTag *table = ReadProgram(record->program, current);
    RenderMethod *method = Ps2_XboxMethod(entry);
    if (method == NULL || table == NULL) {
        counts_[method == NULL ? 1 : 3]++;
        return NULL;
    }

    // The variations' blocks: colours of their own, or the geometry chain itself
    std::vector<std::vector<Unpack>> variationColours;
    const DmaTag *chain = NULL;
    for (const DmaTag *t = table; t->Id() == kDmaCall && variationColours.size() < 256; t++) {
        const DmaTag *block = t->address;
        std::vector<Unpack> colours;
        if (block->Id() == kDmaCall) {
            ReadTag(block, &colours);
            if (chain == NULL)
                chain = block->address;
        } else if (chain == NULL) {
            chain = block;
        }
        variationColours.push_back(colours);
    }
    std::vector<Unpack> geometry;
    if (chain != NULL)
        ReadChain(chain, &geometry);
    if (geometry.size() < 5 || geometry[0].format != kV4_32) {
        counts_[3]++;
        return NULL;
    }
    uint32_t vertexCount;
    memcpy(&vertexCount, geometry[0].data, 4);
    uint8_t starts[12];
    for (int i = 0; i < 3; i++)
        memcpy(starts + i * 4, geometry[1 + i].data, 4);
    const Unpack *positions = NULL, *texCoords = NULL, *chainColours = NULL;
    for (size_t i = 4; i < geometry.size(); i++) {
        const Unpack &u = geometry[i];
        if (positions == NULL && (u.format == kV3_16 || u.format == kV3_32 || u.format == kV4_32))
            positions = &u;
        else if (positions != NULL && texCoords == NULL && (u.format == kV2_16 || u.format == kV2_32))
            texCoords = &u;
        else if (chainColours == NULL && u.format == kV3_8 && u.masked)
            chainColours = &u;
    }
    if (positions == NULL || vertexCount == 0 || vertexCount > positions->count) {
        counts_[3]++;
        return NULL;
    }
    GeoPrimState *state = current->state;
    if (state == NULL)
        state = static_cast<GeoPrimState *>(Resolve("STATE::texalpha=noA;primtype=tristrip"));
    if (state == NULL) {
        counts_[3]++;
        return NULL;
    }
    for (int i = 0; i < entry->paramCount; i++) {
        if (entry->params[i].kind == kParamTexture && entry->params[i].index >= current->textureCount) {
            counts_[3]++;
            return NULL;
        }
    }

    std::vector<std::pair<uint32_t, uint32_t>> strips;
    uint32_t first = 0;
    for (uint8_t start : starts) {
        if (start == 0)
            break;
        uint32_t end = (std::min)(uint32_t(start / 3), vertexCount);
        strips.push_back(std::make_pair(first, end));
        first = end;
    }
    strips.push_back(std::make_pair(first, vertexCount));
    std::vector<uint16_t> indices;
    BuildIndices(strips, state->primitiveType == kPrimitiveTriangleList, &indices);
    if (indices.empty()) {
        counts_[3]++;
        return NULL;
    }

    size_t variations = (std::max)(variationColours.size(), size_t(1));
    size_t bytes = 4 + entry->paramCount * 8 + sizeof(StreamDescription) + indices.size() * 2 +
                   vertexCount * (sizeof(Vertex) + 4 + 4 * variations) + (2 + variations) * sizeof(InlineStream) +
                   16 * (8 + variations);
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
    const uint8_t *src = positions->data;
    for (uint32_t i = 0; i < vertexCount; i++) {
        if (positions->format == kV3_16) {
            uint16_t raw[3];
            memcpy(raw, src + i * 6, 6);
            for (int k = 0; k < 3; k++)
                vertices[i].position[k] = (positions->isUnsigned ? int(raw[k]) - 0x8000 : int16_t(raw[k])) / 256.0f;
        } else {
            memcpy(vertices[i].position, src + i * (positions->format == kV4_32 ? 16 : 12), 12);
        }
    }
    int16_t *uvs = arena.Take<int16_t>(vertexCount * 2);
    if (texCoords != NULL) {
        for (uint32_t i = 0; i < vertexCount && i < texCoords->count; i++) {
            for (int k = 0; k < 2; k++) {
                if (texCoords->format == kV2_16) {
                    uint16_t raw;
                    memcpy(&raw, texCoords->data + i * 4 + k * 2, 2);
                    int value = texCoords->isUnsigned ? int(raw) - 0x8000 : int16_t(raw);
                    uvs[i * 2 + k] = int16_t(value / (1 << entry->texCoordShift));
                } else {
                    float f;
                    memcpy(&f, texCoords->data + i * 8 + k * 4, 4);
                    uvs[i * 2 + k] = TexCoordFromFloat(f);
                }
            }
        }
    }

    // One colour array a variation, from its own block, else the chain's, else white
    uint32_t *colours = arena.Take<uint32_t>(vertexCount * variations);
    for (size_t v = 0; v < variations; v++) {
        const Unpack *u = chainColours;
        if (v < variationColours.size())
            for (const Unpack &c : variationColours[v])
                if (c.format == kV3_8 && c.masked)
                    u = &c;
        for (uint32_t i = 0; i < vertexCount; i++) {
            static const uint8_t kWhite[3] = { 0x80, 0x80, 0x80 };
            const uint8_t *rgb = u != NULL && i < u->count ? u->data + i * 3 : kWhite;
            colours[v * vertexCount + i] = Colour(rgb);
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
    InlineStream *colourStreams = arena.Take<InlineStream>(variations);
    for (size_t v = 0; v < variations; v++)
        streamOver(&colourStreams[v], colours + v * vertexCount, vertexCount * 4, 4);

    prim->method = method;
    for (int i = 0; i < entry->paramCount; i++) {
        const Ps2Param &p = entry->params[i];
        EAGL::GeoPrimParam &param = prim->params[i];
        switch (p.kind) {
            case kParamState:
                param.Set(1, state);
                break;
            case kParamTexture:
                param.Set(1, current->textures[p.index]);
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
                param.Set(0, colourStreams);
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
        Current current = {};
        // Every entry of the list, the blocks a geometry's enable word may skip included (WalkDrawList)
        uint32_t limit = *p++ & 0xffff;
        uint32_t entry = limit;
        do {
            if (entry == 0xffff) {
                const Ps2GeoPrim *ps2 = PtrFrom32<const Ps2GeoPrim>(*p);
                if (ps2 != NULL) {
                    auto done = converted_.find(ps2);
                    GeoPrim *prim = done != converted_.end() ? done->second : Convert(ps2, &current);
                    converted_[ps2] = prim;
                    *p = PtrTo32(prim);
                }
                p++;
            } else {
                p += 2;
            }
            entry = *p++ & 0xffff;
        } while (entry > limit);
    }
    printf("[ps2] %d models: %d GeoPrims converted, %d not drawn (mapped to none), %d of unknown methods, "
           "%d unreadable\n", models, counts_[0], counts_[1], counts_[2], counts_[3]);
}

}  // namespace

void Ps2_ConvertModels(DynamicLoader *loader) {
    if (loader == NULL)
        return;
    Converter(loader).Run();
}
