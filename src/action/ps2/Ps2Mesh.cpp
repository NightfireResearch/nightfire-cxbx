#include "Ps2Mesh.h"
#include "../engine/psiGraphics.h"           // ModelData

#include <math.h>
#include <string.h>

// VIF commands (the top byte of a VIF code, less its interrupt bit)
enum {
    VIF_NOP = 0x00,
    VIF_STCYCL = 0x01,
    VIF_OFFSET = 0x02,
    VIF_BASE = 0x03,
    VIF_ITOP = 0x04,
    VIF_STMOD = 0x05,
    VIF_MSKPATH3 = 0x06,
    VIF_MARK = 0x07,
    VIF_FLUSHE = 0x10,
    VIF_FLUSH = 0x11,
    VIF_FLUSHA = 0x13,
    VIF_MSCAL = 0x14,
    VIF_MSCALF = 0x15,
    VIF_MSCNT = 0x17,
    VIF_STMASK = 0x20,
    VIF_STROW = 0x30,
    VIF_STCOL = 0x31,
    VIF_MPG = 0x4a,
    VIF_DIRECT = 0x50,
    VIF_DIRECTHL = 0x51,
    VIF_UNPACK = 0x60,          // to 0x7f: bits 2-3 the components less one, bits 0-1 the size (32, 16, 8, 5 bits)
};
#define VIF_UNPACK_V2_32 0x64
#define VIF_UNPACK_V3_32 0x68
#define VIF_UNPACK_V4_32 0x6c
#define VIF_UNPACK_V4_8 0x6e
#define VIF_UNPACK_UNSIGNED 0x4000  // in the immediate: zero-extend, not sign-extend

// The GIF tag's primitive (its PRIM field's low 3 bits)
#define GS_PRIM_TRIANGLE_STRIP 4
#define GS_PRIM_TRIANGLE_FAN 5

#define PS2_COLOUR_ONE 0x80             // the GS's 1.0, in colours and alpha
#define PS2_ADC_SKIP 0xff               // a normal's w: the triangle this vertex ends is not drawn

// The Xbox primitive's states (docs spec-mesh.md 5.5): z-write, wrap U and V, double-sided until the winding is
// settled (docs/ps2-data.md)
#define PRIM_STATE_BLEND_ADD 0x0002     // bits 1-2: 0 alpha blend, 1 additive, 2 subtractive
#define PRIM_STATE_BLEND_SUBTRACT 0x0004
#define PRIM_STATE_DOUBLE_SIDED 0x0008
#define PRIM_STATE_ALPHA_REF_180 0x0010
#define PRIM_STATE_Z_WRITE 0x0020
#define PRIM_STATE_WRAP_U 0x0080
#define PRIM_STATE_WRAP_V 0x0100
#define PRIM_STATE_DEFAULT (PRIM_STATE_DOUBLE_SIDED | PRIM_STATE_Z_WRITE | PRIM_STATE_WRAP_U | PRIM_STATE_WRAP_V)
#define PRIM_CHANGE_ALL 0x7b            // every state but the second texture, as the Xbox's first primitives
#define PRIM_CHANGE_STATES 0x7a         // the same less the morph streams
#define PRIM_NO_TEXTURE 0xffff

// The GS registers a box's DIRECT packet sets (A+D: a 64-bit value, then the register's number)
#define GS_REG_ALPHA_1 0x42
#define GS_REG_TEST_1 0x47
#define GS_REG_ZBUF_1 0x4e
#define GS_ALPHA_BLEND 0x44             // (Cs - Cd) * As + Cd
#define GS_ALPHA_ADD 0x48               // (Cs - 0) * As + Cd
#define GS_ALPHA_SUBTRACT 0x42          // (0 - Cs) * As + Cd
#define GS_ZBUF_ZMSK (1ull << 32)       // no depth writes
#define GS_TEST_AREF(test) (((test) >> 4) & 0xff)

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

// A batch as the VIF chain unpacked it
struct Batch {
    int vertices;               // the GIF tag's NLOOP
    int prim;
    const float *uv;
    const float *xyz;
    const uint8_t *colour;
    const int8_t *normal;
    int uvCount, xyzCount, colourCount, normalCount;
};

// The PS2's normals point out of the mesh, the Xbox's into it (spec-mesh.md 6): it is turned round on the way
static uint32_t PackNormal(const int8_t *n) {
    int x = (int)lround(-n[0] / 127.0 * 1023.0);
    int y = (int)lround(-n[1] / 127.0 * 1023.0);
    int z = (int)lround(-n[2] / 127.0 * 511.0);
    return ((uint32_t)x & 0x7ff) | (((uint32_t)y & 0x7ff) << 11) | (((uint32_t)z & 0x3ff) << 22);
}

// Whether triangle (a, b, c) winds the way of its vertices' PS2 normals (counter-clockwise seen from where they point)
static bool WindsWithNormals(const Batch &batch, int a, int b, int c) {
    const float *p = batch.xyz + a * 3, *q = batch.xyz + b * 3, *r = batch.xyz + c * 3;
    double u[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] };
    double v[3] = { r[0] - p[0], r[1] - p[1], r[2] - p[2] };
    double face[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
    double d = 0;
    for (int i = 0; i < 3; i++)
        d += face[i] * ((double)batch.normal[a * 4 + i] + batch.normal[b * 4 + i] + batch.normal[c * 4 + i]);
    return d > 0;
}

static uint8_t Alpha8(uint8_t ps2Alpha) {
    return ps2Alpha >= PS2_COLOUR_ONE ? 0xff : (uint8_t)(ps2Alpha * 2);
}

// The mesh being built: vertices, and per primitive (a texture) its strip of indices
struct MeshBuilder {
    std::vector<KxeVertex> vertices;
    std::vector<KxePrimitive> primitives;
    std::vector<uint16_t> indices;      // every primitive's, one after another
    size_t primitiveStart = 0;          // where the open primitive's indices start

    void BeginPrimitive(int texture, uint16_t stateFlags) {
        if (!primitives.empty() && primitives.back().tex0 == texture && primitives.back().stateFlags == stateFlags)
            return;
        EndPrimitive();
        KxePrimitive p = {};
        p.tex0 = (uint16_t)texture;
        p.tex1 = PRIM_NO_TEXTURE;
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

    // Joins the strip so far to the next index with degenerate triangles (the last index and the next each twice),
    // so that the next lands at an even position (parity 0) or an odd one
    void JoinTo(uint16_t first, int parity) {
        if (Position() != 0) {
            indices.push_back(indices.back());
            indices.push_back(first);
            indices.push_back(first);
        } else {
            indices.push_back(first);
        }
        if ((int)((Position() - 1) & 1) != parity)
            indices.push_back(first);
    }
};

// A batch's triangles into the open primitive. A strip keeps its runs of drawn triangles (those between the ADC
// flags) as strips; a fan's triangles go in one by one.
//
// The GS culls nothing, and the exporter's runs face either way: a run's triangles wind alike (the first even, as
// D3D's strips do) but 7% of runs wind against their normals (07000001). Each run, and each fan triangle, is put
// the Xbox's way round - against the PS2's normals, with the Xbox's normals (PackNormal) - by its triangles' vote.
static void AddBatch(MeshBuilder &mesh, const Batch &b, Ps2MeshStats *stats) {
    uint16_t base = (uint16_t)mesh.vertices.size();
    for (int i = 0; i < b.vertices; i++) {
        KxeVertex v;
        memcpy(v.position, b.xyz + i * 3, sizeof(v.position));
        v.normal = PackNormal(b.normal + i * 4);
        v.colour[0] = b.colour[i * 4 + 2];
        v.colour[1] = b.colour[i * 4 + 1];
        v.colour[2] = b.colour[i * 4 + 0];
        v.colour[3] = Alpha8(b.colour[i * 4 + 3]);
        memcpy(v.uv, b.uv + i * 2, sizeof(v.uv));
        mesh.vertices.push_back(v);
    }
    stats->vertices += b.vertices;

    auto drawn = [&](int k) { return (uint8_t)b.normal[k * 4 + 3] != PS2_ADC_SKIP; };
    for (int k = 2; k < b.vertices; k++) {
        if (!drawn(k)) {
            stats->skippedTriangles++;
            continue;
        }
        if (b.prim == GS_PRIM_TRIANGLE_FAN) {
            stats->triangles++;
            bool turn = WindsWithNormals(b, 0, k - 1, k);
            mesh.JoinTo(base, 0);
            mesh.indices.push_back((uint16_t)(base + (turn ? k : k - 1)));
            mesh.indices.push_back((uint16_t)(base + (turn ? k - 1 : k)));
            continue;
        }
        // A strip's run, from k to its end
        int end = k;
        while (end + 1 < b.vertices && drawn(end + 1))
            end++;
        int withNormals = 0;
        for (int t = k; t <= end; t++) {
            bool even = ((t - k) & 1) == 0;
            withNormals += WindsWithNormals(b, even ? t - 2 : t - 1, even ? t - 1 : t - 2, t);
        }
        int count = end - k + 1;
        stats->triangles += count;
        mesh.JoinTo((uint16_t)(base + k - 2), withNormals * 2 > count ? 1 : 0);
        for (int v = k - 1; v <= end; v++)
            mesh.indices.push_back((uint16_t)(base + v));
        k = end;
    }
}

// The size of an UNPACK's data after its code
static uint32_t UnpackBytes(uint8_t cmd, uint8_t num) {
    uint32_t count = num != 0 ? num : 256;
    uint32_t components = ((cmd >> 2) & 3) + 1;
    uint32_t bits = 32 >> (cmd & 3);
    if ((cmd & 3) == 3)                 // V4-5: 16 bits a vector
        return (count * 2 + 3) & ~3u;
    return (components * bits * count + 31) / 32 * 4;
}

// A DIRECT packet's register writes (a GIF tag, then A+D pairs) as the Xbox primitive's states: the blend from
// ALPHA_1, depth writes from ZBUF_1's mask, the alpha test's reference from TEST_1 (1 or 0x7f, which becomes the
// Xbox's 1 or 180). Blends other than the three are left as they were.
static uint16_t DirectStates(const uint8_t *packet, uint32_t quadwords, uint16_t states) {
    uint64_t tag;
    memcpy(&tag, packet, sizeof(tag));
    uint32_t count = (uint32_t)(tag & 0x7fff);
    for (uint32_t q = 1; q <= count && q < quadwords; q++) {
        uint64_t value, reg;
        memcpy(&value, packet + q * 16, sizeof(value));
        memcpy(&reg, packet + q * 16 + 8, sizeof(reg));
        switch (reg & 0xff) {
        case GS_REG_ALPHA_1:
            switch (value & 0xff) {
            case GS_ALPHA_BLEND: states &= ~(PRIM_STATE_BLEND_ADD | PRIM_STATE_BLEND_SUBTRACT); break;
            case GS_ALPHA_ADD: states = (states & ~PRIM_STATE_BLEND_SUBTRACT) | PRIM_STATE_BLEND_ADD; break;
            case GS_ALPHA_SUBTRACT: states = (states & ~PRIM_STATE_BLEND_ADD) | PRIM_STATE_BLEND_SUBTRACT; break;
            }
            break;
        case GS_REG_ZBUF_1:
            states = (value & GS_ZBUF_ZMSK) ? states & ~PRIM_STATE_Z_WRITE : states | PRIM_STATE_Z_WRITE;
            break;
        case GS_REG_TEST_1:
            states = GS_TEST_AREF(value) > 1 ? states | PRIM_STATE_ALPHA_REF_180 : states & ~PRIM_STATE_ALPHA_REF_180;
            break;
        }
    }
    return states;
}

// Walks a leaf's VIF chain, a batch at each MSCNT
static void ReadChain(MeshBuilder &mesh, const uint8_t *vif, uint32_t bytes, Ps2MeshStats *stats) {
    Batch batch = {};
    int texture = PRIM_NO_TEXTURE;
    uint16_t states = PRIM_STATE_DEFAULT;
    uint32_t at = 0;
    while (at + 4 <= bytes) {
        uint16_t imm = (uint16_t)(vif[at] | vif[at + 1] << 8);
        uint8_t num = vif[at + 2];
        uint8_t cmd = vif[at + 3] & 0x7f;
        const uint8_t *data = vif + at + 4;
        if (cmd >= VIF_UNPACK) {
            int count = num != 0 ? num : 256;
            switch (cmd) {
            case VIF_UNPACK_V4_32:
                if (count == 1) {               // a GIF tag: a new batch
                    uint64_t tag;
                    memcpy(&tag, data, sizeof(tag));
                    batch = {};
                    batch.vertices = (int)(tag & 0x7fff);
                    batch.prim = (int)((tag >> 47) & 7);
                }
                break;
            case VIF_UNPACK_V2_32: batch.uv = (const float *)data; batch.uvCount = count; break;
            case VIF_UNPACK_V3_32: batch.xyz = (const float *)data; batch.xyzCount = count; break;
            case VIF_UNPACK_V4_8:
                if (imm & VIF_UNPACK_UNSIGNED) {
                    batch.colour = data;
                    batch.colourCount = count;
                } else {
                    batch.normal = (const int8_t *)data;
                    batch.normalCount = count;
                }
                break;
            }
            at += 4 + UnpackBytes(cmd, num);
            continue;
        }
        switch (cmd) {
        case VIF_STROW:
            memcpy(&texture, data, sizeof(int));
            at += 20;
            break;
        case VIF_STCOL:
            at += 20;
            break;
        case VIF_STMASK:
            at += 8;
            break;
        case VIF_MPG:
            at += 4 + (num != 0 ? num : 256) * 8;
            break;
        case VIF_DIRECT:
        case VIF_DIRECTHL: {
            uint32_t quadwords = imm != 0 ? imm : 0x10000;
            if (at + 4 + quadwords * 16 <= bytes)
                states = DirectStates(data, quadwords, states);
            at += 4 + quadwords * 16;
            break;
        }
        case VIF_MSCNT:
        case VIF_MSCAL:
        case VIF_MSCALF:
            if (batch.xyzCount != 0) {
                int n = batch.vertices;
                if (batch.uvCount != n || batch.xyzCount != n || batch.colourCount != n || batch.normalCount != n) {
                    stats->badBatches++;
                } else if (batch.prim != GS_PRIM_TRIANGLE_STRIP && batch.prim != GS_PRIM_TRIANGLE_FAN) {
                    stats->otherPrims++;
                } else {
                    (batch.prim == GS_PRIM_TRIANGLE_STRIP ? stats->strips : stats->fans)++;
                    stats->batches++;
                    mesh.BeginPrimitive(texture, states);
                    AddBatch(mesh, batch, stats);
                }
            }
            batch = {};
            at += 4;
            break;
        case VIF_NOP: case VIF_STCYCL: case VIF_OFFSET: case VIF_BASE: case VIF_ITOP: case VIF_STMOD:
        case VIF_MSKPATH3: case VIF_MARK: case VIF_FLUSHE: case VIF_FLUSH: case VIF_FLUSHA:
            at += 4;
            break;
        default:
            stats->unknownVif = true;
            return;
        }
    }
}

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
        for (uint32_t k = 0; k < p.count; k++) {
            if (i[k] == i[k + 1] || i[k + 1] == i[k + 2] || i[k] == i[k + 2])
                degenerate++;
        }
        start += p.count + 2;
    }
    return degenerate;
}

// The KXE: ModelData's header, then each section where psiCreateEntityGfx looks for it - three spare bytes after the
// vertices and the indices, which d3dCreateVertexBuffers and d3dCreateIndexBuffer move their data up into
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
    // The first primitive sets every state; the others those of theirs that differ from the one before
    KxePrimitive *prims = (KxePrimitive *)at;
    memcpy(prims, mesh.primitives.data(), primitiveCnt * sizeof(KxePrimitive));
    for (int p = 0; p < primitiveCnt; p++)
        prims[p].changeMask = p == 0 ? PRIM_CHANGE_ALL
                            : prims[p].stateFlags != prims[p - 1].stateFlags ? PRIM_CHANGE_STATES : 0;
}

bool Ps2Mesh_ConvertToKxe(const uint8_t *block, uint32_t blockBytes, std::vector<uint8_t> &out, Ps2MeshStats *stats) {
    memset(stats, 0, sizeof(*stats));
    out.clear();
    const uint8_t *file = block - 4;    // offsets are from the header word
    uint32_t size;
    memcpy(&size, block, sizeof(size));
    if (size < sizeof(Ps2GfxFooter) || size > blockBytes)
        return false;
    Ps2GfxFooter footer;
    memcpy(&footer, file + size, sizeof(footer));
    stats->boxes = (int)footer.numBoxes;
    stats->skinned = footer.skin != 0;
    if (footer.boxes == 0 || footer.numBoxes == 0 || footer.skin != 0)
        return false;

    MeshBuilder mesh;
    for (uint32_t i = 0; i < footer.numBoxes; i++) {
        Ps2GfxBox box;
        memcpy(&box, file + footer.boxes + i * sizeof(Ps2GfxBox), sizeof(box));
        if (box.vif == 0)
            continue;
        stats->leaves++;
        stats->leafFlags |= box.flags;
        ReadChain(mesh, file + box.vif, box.vifBytes, stats);
    }
    mesh.EndPrimitive();
    stats->primitives = (int)mesh.primitives.size();
    if (mesh.primitives.empty() || mesh.vertices.size() > 0xffff)
        return false;
    WriteKxe(mesh, out);
    return true;
}
