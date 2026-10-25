#include "GcMesh.h"
#include "GcBytes.h"
#include "../engine/psiGraphics.h"   // ModelData

#include <math.h>
#include <string.h>
#include <map>
#include <tuple>

#define NORMAL_FRACTION 14          // 16-bit normals: 1.0 is 1 << 14
#define FLOAT_NORMAL_FRACTION 7     // a float mesh's 8-bit normals
#define POSITION_BYTES 12           // three 16-bit coordinates, then three for the normal
#define FLOAT_POSITION_BYTES 16     // three floats, then three bytes for the normal and one spare
#define UV_BYTES 4
#define COLOUR_BYTES 4
#define NO_TEXTURE -1

// The Xbox primitive's states (spec-mesh.md 5.5)
#define PRIM_STATE_BLEND_ADD 0x0002         // bits 1-2: 0 alpha blend, 1 additive, 2 subtractive
#define PRIM_STATE_BLEND_SUBTRACT 0x0004
#define PRIM_STATE_DOUBLE_SIDED 0x0008
#define PRIM_STATE_Z_WRITE 0x0020
#define PRIM_STATE_LIT 0x0040               // set on most Xbox primitives; the draw does not read it
#define PRIM_STATE_WRAP_U 0x0080
#define PRIM_STATE_WRAP_V 0x0100
#define PRIM_CHANGE_ALL 0x7b                // every state but the second texture, as the Xbox's first primitives
#define PRIM_CHANGE_STATES 0x7a
#define PRIM_CHANGE_TEXTURE1 0x04
#define PRIM_NO_TEXTURE 0xffff

#pragma pack(push, 1)
struct KxeVertex {              // 28 bytes: the rigid vertex declaration
    float position[3];
    uint32_t normal;            // NORMPACKED3: x 11 bits, y 11, z 10
    uint8_t colour[4];          // B, G, R, A
    float uv[2];
};
struct KxePrimitive {           // 12 bytes
    uint16_t tex0;
    uint16_t tex1;
    uint32_t count;             // the strip's indices less two
    uint16_t stateFlags;
    uint8_t changeMask;
    uint8_t bonePalette;
};
#pragma pack(pop)
static_assert(sizeof(KxeVertex) == 28, "KxeVertex is 28 bytes");
static_assert(sizeof(KxePrimitive) == 12, "KxePrimitive is 12 bytes");

// The header turned round to this machine's order
static void ReadHeader(const uint8_t *p, GcMeshHeader *h) {
    memcpy(h, p, sizeof(*h));
    uint32_t *words = (uint32_t *)h;
    for (size_t i = 0; i < sizeof(*h) / 4; i++) {
        if (i >= 1 && i < 6)            // the name
            continue;
        if (i == 0x58 / 4 || i == 0x5c / 4)     // bytes
            continue;
        words[i] = GcBe32(p + i * 4);
    }
    for (int i = 0; i < 3; i++) {
        uint32_t bits = GcBe32(p + 0x60 + i * 4);
        memcpy(&h->centre[i], &bits, 4);
    }
}

// A mesh's data, its offsets checked against the bundle
struct MeshData {
    const uint8_t *header;
    const uint8_t *bundle;
    uint32_t bundleBytes;
    GcMeshHeader h;
    bool outOfRange = false;

    // count items of size bytes at the header's offset, or NULL (and outOfRange) when they run past the bundle
    const uint8_t *At(uint32_t offset, uint32_t index, uint32_t size) {
        int64_t at = (int64_t)(header - bundle) + (int32_t)offset + (int64_t)index * size;
        if (at < 0 || at + size > bundleBytes) {
            outOfRange = true;
            return NULL;
        }
        return bundle + at;
    }

    uint16_t Index(uint32_t list, uint32_t i) {
        const uint8_t *p = At(list, i, 2);
        return p != NULL ? GcBe16(p) : 0;
    }

    bool Floats() const { return h.floatPositions[0] == -1; }
    bool HasColours() const { return h.colourArray != 0; }

    void Vertex(uint16_t position, uint16_t colour, uint16_t uv, GcMeshVertex *v) {
        memset(v, 0, sizeof(*v));
        const uint8_t *p = At(h.positionArray, position, Floats() ? FLOAT_POSITION_BYTES : POSITION_BYTES);
        if (p != NULL && Floats()) {
            for (int i = 0; i < 3; i++) {
                uint32_t bits = GcBe32(p + i * 4);
                memcpy(&v->position[i], &bits, 4);
                v->position[i] += h.centre[i];
                v->normal[i] = (int8_t)p[12 + i] / (float)(1 << FLOAT_NORMAL_FRACTION);
            }
        } else if (p != NULL) {
            float scale = ldexpf(1.0f, -(16 - h.positionShift));
            for (int i = 0; i < 3; i++) {
                v->position[i] = (int16_t)GcBe16(p + i * 2) * scale + h.centre[i];
                v->normal[i] = (int16_t)GcBe16(p + 6 + i * 2) / (float)(1 << NORMAL_FRACTION);
            }
        }
        const uint8_t *c = HasColours() ? At(h.colourArray, colour, COLOUR_BYTES) : NULL;
        if (c != NULL)
            memcpy(v->colour, c, COLOUR_BYTES);
        else
            memset(v->colour, 0xff, COLOUR_BYTES);
        const uint8_t *t = At(h.uvArray, uv, UV_BYTES);
        if (t != NULL) {
            float scale = ldexpf(1.0f, -(16 - h.uvShift));
            v->uv[0] = (int16_t)GcBe16(t) * scale;
            v->uv[1] = (int16_t)GcBe16(t + 2) * scale;
        }
    }
};

// A strip vertex's three indices
struct StripIndices {
    uint16_t position, colour, uv;
    bool operator<(const StripIndices &o) const {
        return std::tie(position, colour, uv) < std::tie(o.position, o.colour, o.uv);
    }
};

struct RawStrip {
    int layer, group, texture, secondTexture;
    uint32_t counts;
    std::vector<StripIndices> vertices;
};

// The strips in the order GC maybe_psiDrawClonedObject draws them (the walk over its index streams is its own)
static bool WalkStrips(MeshData &m, std::vector<RawStrip> &strips, GcMeshStats *stats) {
    memset(stats, 0, sizeof(*stats));
    ReadHeader(m.header, &m.h);
    stats->groups = (int)m.h.groups;
    stats->skinned = m.h.skin[0] != 0;
    if (m.h.magic != GC_MESH_MAGIC || stats->skinned || m.h.groups == 0 || m.h.positions == 0 ||
        m.h.positionIndices == 0)
        return false;
    std::vector<GcStripGroup> groups(m.h.groups);
    for (uint32_t g = 0; g < m.h.groups; g++) {
        const uint8_t *p = m.At(m.h.groupList, g, sizeof(GcStripGroup));
        if (p == NULL)
            return false;
        groups[g].counts = GcBe32(p);
        for (int l = 0; l < GC_MESH_LAYERS; l++)
            groups[g].texture[l] = (int16_t)GcBe16(p + 4 + l * 2);
    }
    for (int layer = 0; layer < GC_MESH_LAYERS; layer++) {
        if (layer > 0 && (m.h.layerFlags[layer - 1] & GC_LAYER_TWO_TEXTURES))
            continue;               // drawn with the layer before
        uint32_t strip = 0, position = 0, uvBase = 0;
        for (uint32_t g = 0; g < m.h.groups; g++) {
            const GcStripGroup &group = groups[g];
            uint32_t count = GC_GROUP_STRIPS(group.counts);
            uint32_t vertices = GC_GROUP_VERTICES(group.counts) + 2 * count;
            if (count == 0)
                continue;
            // This layer's coordinate and colour indices follow those of the group's layers before it
            uint32_t uv = uvBase;
            for (int l = 0; l < layer; l++)
                if (group.texture[l] != NO_TEXTURE)
                    uv += vertices;
            uvBase += vertices * GC_GROUP_UV_SETS(group.counts);
            if (group.texture[layer] == NO_TEXTURE) {
                strip += count;
                position += vertices;
                continue;
            }
            for (uint32_t s = 0; s < count; s++, strip++) {
                RawStrip raw;
                raw.layer = layer;
                raw.group = (int)g;
                raw.texture = group.texture[layer];
                raw.secondTexture = (m.h.layerFlags[layer] & GC_LAYER_TWO_TEXTURES) && layer + 1 < GC_MESH_LAYERS
                                        ? group.texture[layer + 1] : NO_TEXTURE;
                raw.counts = group.counts;
                uint32_t n = m.Index(m.h.stripLengths, strip) + 2u;
                for (uint32_t k = 0; k < n; k++, position++, uv++) {
                    StripIndices v;
                    v.position = m.Index(m.h.positionIndexList, position);
                    v.colour = m.HasColours() ? m.Index(m.h.colourIndexList, uv) : 0;
                    v.uv = m.Index(m.h.uvIndexList, uv);
                    raw.vertices.push_back(v);
                }
                if (m.outOfRange)
                    return false;
                stats->strips++;
                if (layer > 0)
                    stats->layers++;
                strips.push_back(raw);
            }
        }
    }
    stats->outOfRange = m.outOfRange;
    return !strips.empty() && !m.outOfRange;
}

bool GcMesh_ReadStrips(const uint8_t *header, const uint8_t *bundle, uint32_t bundleBytes,
                       std::vector<GcMeshStrip> &strips, GcMeshStats *stats) {
    strips.clear();
    MeshData m;
    m.header = header;
    m.bundle = bundle;
    m.bundleBytes = bundleBytes;
    std::vector<RawStrip> raw;
    if (!WalkStrips(m, raw, stats))
        return false;
    for (const RawStrip &r : raw) {
        GcMeshStrip s;
        s.layer = r.layer;
        s.group = r.group;
        s.texture = r.texture;
        s.secondTexture = r.secondTexture;
        s.counts = r.counts;
        s.vertices.resize(r.vertices.size());
        for (size_t k = 0; k < r.vertices.size(); k++)
            m.Vertex(r.vertices[k].position, r.vertices[k].colour, r.vertices[k].uv, &s.vertices[k]);
        strips.push_back(s);
    }
    return true;
}

// The GameCube's normals point the other way to the Xbox's (spec-mesh.md 6): turned round on the way
static uint32_t PackNormal(const float *n) {
    int nx = (int)lround(-n[0] * 1023.0);
    int ny = (int)lround(-n[1] * 1023.0);
    int nz = (int)lround(-n[2] * 511.0);
    return ((uint32_t)nx & 0x7ff) | (((uint32_t)ny & 0x7ff) << 11) | (((uint32_t)nz & 0x3ff) << 22);
}

// The strip's states as the Xbox primitive's: the layer's blend, depth writes, the group's culling; textures wrap.
//
// The GameCube's data leaves the double-sided bit off most blended effect quads with no depth writes (the explosions'
// Blast_frame_n, flash, HitSpark), which the Xbox's twins all have, so the GameCube culls them when they face away.
// They are made double-sided here, as the Xbox's are.
static uint16_t StripStates(const GcMeshHeader &h, const RawStrip &s) {
    uint32_t layer = h.layerFlags[s.layer];
    uint16_t states = PRIM_STATE_LIT | PRIM_STATE_WRAP_U | PRIM_STATE_WRAP_V;
    if (layer & GC_LAYER_ADD)
        states |= PRIM_STATE_BLEND_ADD;
    else if (layer & GC_LAYER_SUBTRACT)
        states |= PRIM_STATE_BLEND_SUBTRACT;
    if (!(h.flags & GC_MESH_NO_Z_WRITE) && !(layer & (GC_LAYER_ADD | GC_LAYER_SUBTRACT)))
        states |= PRIM_STATE_Z_WRITE;
    if (s.counts & GC_GROUP_DOUBLE_SIDED)
        states |= PRIM_STATE_DOUBLE_SIDED;
    if ((layer & (GC_LAYER_ADD | GC_LAYER_SUBTRACT)) && (h.flags & GC_MESH_NO_Z_WRITE))
        states |= PRIM_STATE_DOUBLE_SIDED;
    return states;
}

// The mesh being built: vertices, and per primitive (a texture and its states) one strip of indices
struct MeshBuilder {
    std::vector<KxeVertex> vertices;
    std::map<StripIndices, uint16_t> vertexOf;
    std::vector<KxePrimitive> primitives;
    std::vector<uint16_t> indices;
    size_t primitiveStart = 0;

    void BeginPrimitive(int texture, int secondTexture, uint16_t stateFlags) {
        uint16_t tex0 = texture < 0 ? PRIM_NO_TEXTURE : (uint16_t)texture;
        uint16_t tex1 = secondTexture < 0 ? PRIM_NO_TEXTURE : (uint16_t)secondTexture;
        if (!primitives.empty() && primitives.back().tex0 == tex0 && primitives.back().tex1 == tex1 &&
            primitives.back().stateFlags == stateFlags)
            return;
        EndPrimitive();
        KxePrimitive p = {};
        p.tex0 = tex0;
        p.tex1 = tex1;
        p.stateFlags = stateFlags;
        primitives.push_back(p);
        primitiveStart = indices.size();
    }

    // A primitive with no triangle is dropped: the draw loop steps over count + 2 indices whatever the count
    void EndPrimitive() {
        if (primitives.empty())
            return;
        size_t n = indices.size() - primitiveStart;
        if (n < 3) {
            indices.resize(primitiveStart);
            primitives.pop_back();
            return;
        }
        primitives.back().count = (uint32_t)(n - 2);
    }

    size_t Position() const { return indices.size() - primitiveStart; }

    // Joins the strip so far to the next with degenerate triangles, so that the next starts at an even position
    void JoinTo(uint16_t first) {
        if (Position() != 0) {
            indices.push_back(indices.back());
            indices.push_back(first);
            indices.push_back(first);
        } else {
            indices.push_back(first);
        }
        if ((Position() - 1) & 1)
            indices.push_back(first);
    }
};

// d3dGetVertexDataSize and d3dGetIndexDataSize, which psiCreateEntityGfx steps over the sections with: the data and
// three bytes to spare
static uint32_t KxeVertexBytes(int vtxCnt) {
    return vtxCnt * sizeof(KxeVertex) + 3;
}

static uint32_t KxeIndexBytes(int idxCnt) {
    return idxCnt * sizeof(uint16_t) + 3;
}

// The strip triangles with an index repeated: psiGetTriList leaves them out of the triangles it allocates for
static int DegenerateTriangles(const MeshBuilder &mesh) {
    int degenerate = 0;
    size_t start = 0;
    for (const KxePrimitive &p : mesh.primitives) {
        const uint16_t *i = &mesh.indices[start];
        for (uint32_t k = 0; k < p.count; k++)
            if (i[k] == i[k + 1] || i[k + 1] == i[k + 2] || i[k] == i[k + 2])
                degenerate++;
        start += p.count + 2;
    }
    return degenerate;
}

// The KXE: ModelData's header, then the vertices and the indices, each with three spare bytes, which
// d3dCreateVertexBuffers and d3dCreateIndexBuffer move their data up into, then the primitives
static void WriteKxe(const MeshBuilder &mesh, std::vector<uint8_t> &out) {
    int vtxCnt = (int)mesh.vertices.size();
    int idxCnt = (int)mesh.indices.size();
    int primitiveCnt = (int)mesh.primitives.size();
    uint32_t vertexBytes = KxeVertexBytes(vtxCnt);
    uint32_t indexBytes = KxeIndexBytes(idxCnt);
    size_t size = offsetof(ModelData, data) + vertexBytes + indexBytes + primitiveCnt * sizeof(KxePrimitive);
    out.assign((size + 3) & ~(size_t)3, 0xff);

    ModelData header;
    memset(&header, 0, offsetof(ModelData, data));
    header.magicTag = MODELDATA_MAGIC;
    header.unknown4 = 0xffffffff;
    header.vtxCnt = vtxCnt;
    header.idxCnt = idxCnt;
    header.primitiveCnt = primitiveCnt;
    int triangles = 0;
    for (const KxePrimitive &p : mesh.primitives)
        triangles += (int)p.count;
    header.dataSize14 = triangles;
    header.dataSize18 = DegenerateTriangles(mesh);
    memcpy(out.data(), &header, offsetof(ModelData, data));

    uint8_t *at = out.data() + offsetof(ModelData, data);
    memcpy(at, mesh.vertices.data(), vtxCnt * sizeof(KxeVertex));
    at += vertexBytes;
    memcpy(at, mesh.indices.data(), idxCnt * sizeof(uint16_t));
    at += indexBytes;
    // The first primitive sets every state; the others those that differ from the one before
    KxePrimitive *prims = (KxePrimitive *)at;
    memcpy(prims, mesh.primitives.data(), primitiveCnt * sizeof(KxePrimitive));
    for (int p = 0; p < primitiveCnt; p++) {
        if (p == 0) {
            prims[p].changeMask = PRIM_CHANGE_ALL | (prims[p].tex1 != PRIM_NO_TEXTURE ? PRIM_CHANGE_TEXTURE1 : 0);
            continue;
        }
        bool states = prims[p].stateFlags != prims[p - 1].stateFlags;
        bool texture1 = prims[p].tex1 != prims[p - 1].tex1;
        prims[p].changeMask = (states ? PRIM_CHANGE_STATES : 0) | (texture1 ? PRIM_CHANGE_TEXTURE1 : 0);
    }
}

bool GcMesh_ConvertToKxe(const uint8_t *header, const uint8_t *bundle, uint32_t bundleBytes, std::vector<uint8_t> &out,
                         GcMeshStats *stats) {
    out.clear();
    MeshData m;
    m.header = header;
    m.bundle = bundle;
    m.bundleBytes = bundleBytes;
    std::vector<RawStrip> strips;
    if (!WalkStrips(m, strips, stats))
        return false;

    MeshBuilder mesh;
    for (const RawStrip &s : strips) {
        uint16_t states = StripStates(m.h, s);
        mesh.BeginPrimitive(s.texture, s.secondTexture, states);
        std::vector<uint16_t> ids(s.vertices.size());
        for (size_t k = 0; k < s.vertices.size(); k++) {
            auto found = mesh.vertexOf.find(s.vertices[k]);
            if (found != mesh.vertexOf.end()) {
                ids[k] = found->second;
                continue;
            }
            if (mesh.vertices.size() >= 0xffff)
                return false;
            GcMeshVertex v;
            m.Vertex(s.vertices[k].position, s.vertices[k].colour, s.vertices[k].uv, &v);
            KxeVertex x;
            memcpy(x.position, v.position, sizeof(x.position));
            x.normal = PackNormal(v.normal);
            x.colour[0] = v.colour[2];
            x.colour[1] = v.colour[1];
            x.colour[2] = v.colour[0];
            x.colour[3] = v.colour[3];
            memcpy(x.uv, v.uv, sizeof(x.uv));
            ids[k] = (uint16_t)mesh.vertices.size();
            mesh.vertexOf[s.vertices[k]] = ids[k];
            mesh.vertices.push_back(x);
        }
        // GX strips wind as D3D's do: the first triangle as it lies, the next turned round
        mesh.JoinTo(ids[0]);
        for (size_t k = 1; k < ids.size(); k++)
            mesh.indices.push_back(ids[k]);
        for (size_t k = 2; k < ids.size(); k++) {
            if (ids[k] == ids[k - 1] || ids[k] == ids[k - 2] || ids[k - 1] == ids[k - 2])
                continue;
            stats->triangles++;
            if (states & PRIM_STATE_DOUBLE_SIDED)
                stats->doubleSidedTriangles++;
        }
    }
    mesh.EndPrimitive();
    stats->primitives = (int)mesh.primitives.size();
    stats->vertices = (int)mesh.vertices.size();
    if (mesh.primitives.empty())
        return false;
    WriteKxe(mesh, out);
    return true;
}
