#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// The PS2's GeoPrims as the Xbox's (docs/ps2-driving.md, "GeoPrims"). A PS2 model's draw list points at GeoPrims
// {render method, 0}: the render method is a child of one of the PS2's (Ps2RenderMethods.cpp) whose packets are a
// compiled program naming only what changed since the GeoPrim before it in the draw list (TARs, the GeoPrimState)
// and, after a 0x04000002 word, a table of DMA CALL tags, one a variation. Each calls a block that unpacks that
// variation's vertex colours (V3-8, masked) and calls the geometry: a DMA chain ending in a RET whose VIF packets
// unpack a header (V4-32: the vertex count), three V4-8 words of strip starts (bytes, the method's VU quadwords a vertex, zero
// ended), the positions (V3-16 in 1/256ths offset by 0x8000, or V3-32 / V4-32 floats), then the method's other
// fields (DecodeGeometry): texture coordinates, normals, colours for methods with no colour per variation, damage.
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

// ---- what a GeoPrim's geometry gives each vertex

// The 16-bit damage fields: value - 0x8000 in 1/1024ths
constexpr float kDamageScale = 1.0f / 1024;

struct Ps2Vertices {
    uint32_t count;
    std::vector<std::pair<uint32_t, uint32_t>> strips;   // [first, end) of each
    std::vector<float> positions;          // 3 a vertex
    std::vector<int16_t> texCoords;        // 2 a vertex, in 1/256ths
    std::vector<float> normals;            // 3 a vertex, unit (or none)
    std::vector<float> damageVectors;      // 3 a vertex (or none)
    std::vector<float> damageNormals;      // 3 a vertex (or none)
    std::vector<float> damageWeights;      // 1 a vertex (or none)
    std::vector<int16_t> damageZones;      // 1 a vertex (or none)
    std::vector<std::vector<uint32_t>> colours;   // one array a variation
};

uint32_t Colour(const uint8_t *rgb) {
    return 0xff000000u | uint32_t(rgb[0]) << 16 | uint32_t(rgb[1]) << 8 | rgb[2];
}

int16_t TexCoordFromFloat(float f) {
    float scaled = floorf(f * 256 + 0.5f);
    return int16_t((std::max)(-32768.0f, (std::min)(32767.0f, scaled)));
}

int Biased16(const Unpack &u, const uint8_t *p) {
    uint16_t raw;
    memcpy(&raw, p, 2);
    return u.isUnsigned ? int(raw) - 0x8000 : int16_t(raw);
}

float Float32(const uint8_t *p) {
    float f;
    memcpy(&f, p, 4);
    return f;
}

void Normalise(float *v) {
    float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length > 0) {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}

// The packed normals' bytes: unsigned, 0x80 the zero (value - 128) / 127
constexpr int kNormalByteZero = 0x80;
constexpr float kNormalByteScale = 127.0f;
// The length the Xbox's car normals have (0.899 over the Vanquish's, against its PS2 twin's unit normals)
constexpr float kXboxNormalLength = 0.9f;

// D3DVSDT_NORMPACKED3: x and y in 11 bits, z in 10, signed
uint32_t NormPacked3(const float *n) {
    auto field = [](float f, float scale, uint32_t mask) {
        float clamped = (std::max)(-1.0f, (std::min)(1.0f, f));
        return uint32_t(int32_t(floorf(clamped * scale + 0.5f))) & mask;
    };
    return field(n[0], 1023, 0x7ff) | field(n[1], 1023, 0x7ff) << 11 | field(n[2], 511, 0x3ff) << 22;
}

// The geometry's unpacks after the header and the strip starts, by what the methods put where: positions first;
// then for the packed layouts the texture coordinates (V2), normals (V3-8 unmasked, bytes with 0x80 the zero),
// colours (V3-8, masked) and the
// damage vector and normal (the first and second V4-16: xyz, the vector's w the weight, the normal's the zone); for
// the float layout (MatCarDash: every field V4-32) normals, texture coordinates (s, t), damage vector (w the
// weight) and damage normal (w the zone), in that order.
bool DecodeGeometry(const std::vector<Unpack> &geometry, const std::vector<std::vector<Unpack>> &variationColours,
                    const Ps2RenderMethod *entry, Ps2Vertices *out) {
    if (geometry.size() < 5 || geometry[0].format != kV4_32)
        return false;
    uint32_t count;
    memcpy(&count, geometry[0].data, 4);
    const Unpack &positions = geometry[4];
    if (count == 0 || count > positions.count ||
        (positions.format != kV3_16 && positions.format != kV3_32 && positions.format != kV4_32))
        return false;
    out->count = count;

    uint8_t starts[12];
    for (int i = 0; i < 3; i++)
        memcpy(starts + i * 4, geometry[1 + i].data, 4);
    uint32_t first = 0;
    for (uint8_t start : starts) {
        if (start == 0)
            break;
        uint32_t end = (std::min)(uint32_t(start / entry->vertexQuadwords), count);
        out->strips.push_back(std::make_pair(first, end));
        first = end;
    }
    out->strips.push_back(std::make_pair(first, count));

    out->positions.resize(count * 3);
    for (uint32_t i = 0; i < count; i++) {
        for (int k = 0; k < 3; k++) {
            out->positions[i * 3 + k] = positions.format == kV3_16
                ? Biased16(positions, positions.data + i * 6 + k * 2) / 256.0f
                : Float32(positions.data + i * (positions.format == kV4_32 ? 16 : 12) + k * 4);
        }
    }

    const Unpack *texCoords = NULL, *normals = NULL, *chainColours = NULL, *damageVectors = NULL,
                 *damageNormals = NULL;
    bool floatLayout = positions.format == kV4_32 && geometry.size() >= 9 && geometry[5].format == kV4_32;
    if (floatLayout) {
        normals = &geometry[5];
        texCoords = &geometry[6];
        damageVectors = &geometry[7];
        damageNormals = &geometry[8];
    } else {
        for (size_t i = 5; i < geometry.size(); i++) {
            const Unpack &u = geometry[i];
            if (texCoords == NULL && (u.format == kV2_16 || u.format == kV2_32))
                texCoords = &u;
            else if (u.format == kV3_8 && u.masked && chainColours == NULL)
                chainColours = &u;
            else if (u.format == kV3_8 && !u.masked && normals == NULL)
                normals = &u;
            else if (u.format == kV4_16 && damageVectors == NULL)
                damageVectors = &u;
            else if (u.format == kV4_16 && damageNormals == NULL)
                damageNormals = &u;
        }
    }

    out->texCoords.assign(count * 2, 0);
    if (texCoords != NULL) {
        for (uint32_t i = 0; i < count && i < texCoords->count; i++) {
            for (int k = 0; k < 2; k++) {
                if (texCoords->format == kV2_16) {
                    int value = Biased16(*texCoords, texCoords->data + i * 4 + k * 2);
                    out->texCoords[i * 2 + k] = int16_t(value / (1 << entry->texCoordShift));
                } else {
                    int stride = texCoords->format == kV4_32 ? 16 : 8;
                    out->texCoords[i * 2 + k] = TexCoordFromFloat(Float32(texCoords->data + i * stride + k * 4));
                }
            }
        }
    }
    if (normals != NULL) {
        out->normals.resize(count * 3);
        for (uint32_t i = 0; i < count && i < normals->count; i++) {
            float *n = &out->normals[i * 3];
            for (int k = 0; k < 3; k++)
                n[k] = floatLayout ? Float32(normals->data + i * 16 + k * 4)
                                   : (normals->data[i * 3 + k] - kNormalByteZero) / kNormalByteScale;
            Normalise(n);
            for (int k = 0; k < 3; k++)
                n[k] *= kXboxNormalLength;
        }
    }
    if (damageVectors != NULL && damageNormals != NULL) {
        out->damageVectors.resize(count * 3);
        out->damageNormals.resize(count * 3);
        out->damageWeights.resize(count);
        out->damageZones.resize(count);
        for (uint32_t i = 0; i < count && i < damageVectors->count && i < damageNormals->count; i++) {
            if (floatLayout) {
                for (int k = 0; k < 3; k++) {
                    out->damageVectors[i * 3 + k] = Float32(damageVectors->data + i * 16 + k * 4);
                    out->damageNormals[i * 3 + k] = Float32(damageNormals->data + i * 16 + k * 4);
                }
                out->damageWeights[i] = Float32(damageVectors->data + i * 16 + 12);
                out->damageZones[i] = int16_t(Float32(damageNormals->data + i * 16 + 12));
            } else {
                for (int k = 0; k < 3; k++) {
                    out->damageVectors[i * 3 + k] = Biased16(*damageVectors, damageVectors->data + i * 8 + k * 2) *
                                                    kDamageScale;
                    out->damageNormals[i * 3 + k] = Biased16(*damageNormals, damageNormals->data + i * 8 + k * 2) *
                                                    kDamageScale;
                }
                out->damageWeights[i] = Biased16(*damageVectors, damageVectors->data + i * 8 + 6) * kDamageScale;
                uint16_t zone;
                memcpy(&zone, damageNormals->data + i * 8 + 6, 2);
                out->damageZones[i] = int16_t(zone);
            }
        }
    }

    // One colour array a variation, from its own block, else the chain's, else white
    size_t variations = (std::max)(variationColours.size(), size_t(1));
    out->colours.resize(variations);
    for (size_t v = 0; v < variations; v++) {
        const Unpack *u = chainColours;
        if (v < variationColours.size())
            for (const Unpack &c : variationColours[v])
                if (c.format == kV3_8 && c.masked)
                    u = &c;
        out->colours[v].resize(count);
        for (uint32_t i = 0; i < count; i++) {
            static const uint8_t kWhite[3] = { 0x80, 0x80, 0x80 };
            out->colours[v][i] = Colour(u != NULL && i < u->count ? u->data + i * 3 : kWhite);
        }
    }
    return true;
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
constexpr int kMaxTextures = 8;

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

// The index list: each strip in turn, joined to the one before by repeating the last index and the first (and the
// first again to keep the strip's winding on an even place), or as triangles for a list
void BuildIndices(const std::vector<std::pair<uint32_t, uint32_t>> &strips, bool list, std::vector<uint16_t> *out) {
    for (const auto &s : strips) {
        if (s.second < s.first + 3)
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

// Bytes a vertex of a stream parameter
uint32_t StreamStride(Ps2ParamKind kind) {
    switch (kind) {
        case kParamPositions: case kParamDamageVectors: case kParamDamageNormals: return 12;
        case kParamTexCoordsFloat: return 8;
        case kParamTexCoords: case kParamColours: case kParamNormals: case kParamDamageWeights: return 4;
        case kParamDamageZones: return 2;
        default: return 0;
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
    void *ProgramVariable(const uint32_t *program, const char *prefix);
    void *Resolve(const char *name);
    bool IsTexture(const void *p) const { return std::binary_search(textures_.begin(), textures_.end(), p); }
    bool IsState(const void *p) const { return std::binary_search(states_.begin(), states_.end(), p); }

    DynamicLoader *loader_;
    std::vector<const void *> textures_, states_;          // the object's own TARs; every GeoPrimState
    std::vector<std::pair<const void *, const char *>> variables_;   // the names it resolved from outside
    std::map<const Ps2GeoPrim *, GeoPrim *> converted_;
    std::set<const uint32_t *> lists_;
    std::map<std::string, void *> resolved_;
    int counts_[4];   // converted, not drawn by mapping, unknown method, unreadable
};

Converter::Converter(DynamicLoader *loader) : loader_(loader), counts_() {
    LoaderSymbol s;
    for (int i = 0; loader->GetSymbol(&s, i)->name != NULL; i++) {
        if (strcmp(s.className, "EAGL::TAR") == 0 && uint8_t(s.defined) != 0)
            textures_.push_back(s.address);
        else if (strcmp(s.className, "EAGL::GeoPrimState") == 0)
            states_.push_back(s.address);
        else if (uint8_t(s.defined) == 0 && s.address != NULL)
            variables_.push_back(std::make_pair(s.address, s.name));
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

// The TARs of the object's own and the GeoPrimState the program names (replacing the current ones), and its
// variation table
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

// A variable the program names whose name starts with `prefix`, or NULL
void *Converter::ProgramVariable(const uint32_t *program, const char *prefix) {
    size_t length = strlen(prefix);
    for (int i = 0; i < 256 && program[i] != kProgramVariations; i++) {
        const void *p = PtrFrom32<const void>(program[i]);
        for (const auto &v : variables_)
            if (v.first == p && strncmp(v.second, prefix, length) == 0)
                return const_cast<void *>(p);
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
    Ps2Vertices vertices;
    if (!DecodeGeometry(geometry, variationColours, entry, &vertices)) {
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
        if (entry->params[i].kind == kParamTexture && current->textureCount == 0) {
            counts_[3]++;
            return NULL;
        }
    }
    std::vector<uint16_t> indices;
    BuildIndices(vertices.strips, state->primitiveType == kPrimitiveTriangleList, &indices);
    if (indices.empty()) {
        counts_[3]++;
        return NULL;
    }

    uint32_t count = vertices.count;
    size_t variations = vertices.colours.size();
    size_t bytes = 4 + entry->paramCount * 8 + sizeof(StreamDescription) + indices.size() * 2 + 64;
    for (int i = 0; i < entry->paramCount; i++) {
        uint32_t stride = StreamStride(entry->params[i].kind);
        size_t arrays = entry->params[i].kind == kParamColours ? variations : 1;
        bytes += arrays * (count * stride + 16 + sizeof(InlineStream) + 16);
    }
    Arena arena(bytes);
    if (!arena.Valid())
        return NULL;
    GeoPrim *prim = arena.Take<GeoPrim>(0);
    arena.Take<uint32_t>(1 + entry->paramCount * 2);
    StreamDescription *description = arena.Take<StreamDescription>(1);
    description->vertexCount = uint32_t(indices.size());
    uint16_t *indexData = arena.Take<uint16_t>(indices.size());
    memcpy(indexData, indices.data(), indices.size() * 2);

    // A stream source over an array of the GeoPrim's: one for each array of `arrays`
    auto makeStreams = [&](Ps2ParamKind kind, size_t arrays) {
        uint32_t stride = StreamStride(kind);
        InlineStream *streams = arena.Take<InlineStream>(arrays);
        for (size_t a = 0; a < arrays; a++) {
            uint8_t *data = arena.Take<uint8_t>(count * stride);
            for (uint32_t i = 0; i < count; i++) {
                uint8_t *element = data + i * stride;
                switch (kind) {
                    case kParamPositions:
                        memcpy(element, &vertices.positions[i * 3], 12);
                        break;
                    case kParamTexCoords:
                        memcpy(element, &vertices.texCoords[i * 2], 4);
                        break;
                    case kParamTexCoordsFloat: {
                        float uv[2] = { vertices.texCoords[i * 2] / 256.0f, vertices.texCoords[i * 2 + 1] / 256.0f };
                        memcpy(element, uv, 8);
                        break;
                    }
                    case kParamColours:
                        memcpy(element, &vertices.colours[a][i], 4);
                        break;
                    case kParamNormals: {
                        static const float kUp[3] = { 0, 1, 0 };
                        uint32_t packed = NormPacked3(vertices.normals.empty() ? kUp : &vertices.normals[i * 3]);
                        memcpy(element, &packed, 4);
                        break;
                    }
                    case kParamDamageVectors:
                        if (!vertices.damageVectors.empty())
                            memcpy(element, &vertices.damageVectors[i * 3], 12);
                        break;
                    case kParamDamageNormals:
                        if (!vertices.damageNormals.empty())
                            memcpy(element, &vertices.damageNormals[i * 3], 12);
                        break;
                    case kParamDamageWeights:
                        if (!vertices.damageWeights.empty())
                            memcpy(element, &vertices.damageWeights[i], 4);
                        break;
                    case kParamDamageZones:
                        if (!vertices.damageZones.empty())
                            memcpy(element, &vertices.damageZones[i], 2);
                        break;
                    default:
                        break;
                }
            }
            InlineStream *s = &streams[a];
            s->source.buffer = &s->header;
            s->source.size = count * stride;
            s->source.stride = stride;
            s->source.user = 1;
            s->header.common = kVertexBufferCommon;
            s->header.data = 0;
            s->header.lock = 0;
            D3DResource_Register(&s->header, data);
        }
        return streams;
    };

    prim->method = method;
    for (int i = 0; i < entry->paramCount; i++) {
        const Ps2Param &p = entry->params[i];
        EAGL::GeoPrimParam &param = prim->params[i];
        static float zero[4 * 16];
        switch (p.kind) {
            case kParamState:
                param.Set(1, state);
                break;
            case kParamTexture:   // a program naming fewer TARs than the method takes gives its last again
                param.Set(1, current->textures[(std::min)(int(p.index), current->textureCount - 1)]);
                break;
            case kParamStream:
                param.Set(1, description);
                break;
            case kParamIndices:
                param.Set(uint32_t(indices.size()), indexData);
                break;
            case kParamVariable: {
                void *value = Resolve(p.name);
                param.Set(p.count, value != NULL ? value : zero);
                break;
            }
            case kParamProgramVariable: {
                void *value = ProgramVariable(record->program, p.name);
                if (value == NULL)
                    value = Resolve(p.fallback);
                param.Set(p.count, value != NULL ? value : zero);
                break;
            }
            case kParamConstant:
                param.Set(p.count, p.constant != NULL ? p.constant : zero);
                break;
            default:   // the streams
                param.Set(0, makeStreams(p.kind, p.kind == kParamColours ? variations : 1));
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
