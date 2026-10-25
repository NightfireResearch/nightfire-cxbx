#include "Ps2Mesh.h"
#include "../engine/psiGraphics.h"           // ModelData

#include <math.h>
#include <string.h>
#include <algorithm>

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

// DMA tags (bits 28-30 of a tag's first word): a skinned mesh's chain uses CNT, REF and END
enum {
    DMA_REFE = 0,
    DMA_CNT = 1,
    DMA_NEXT = 2,
    DMA_REF = 3,
    DMA_REFS = 4,
    DMA_CALL = 5,
    DMA_RET = 6,
    DMA_END = 7,
};
#define DMA_TAG_BYTES 16

#define VU1_QWORDS 1024
#define SKIN_BATCH_QWORDS 3             // a skinned batch vertex after the GIF tag: position, colour, normal

// The Xbox's bone palette: up to 53 bones, ended by 0xff (spec-mesh.md 5.7), and the skinned shader's constant
// register of each slot, less 96 (SkinRegTable 0x1b5208, spec-mesh.md 7.1)
#define XBOX_PALETTE_BYTES 0x36
#define XBOX_PALETTE_SLOTS 53
#define XBOX_PALETTE_END 0xff

static uint8_t SkinSlotRegister(int slot) {
    return (uint8_t)(slot < 19 ? slot * 3 : slot < 31 ? 60 + (slot - 19) * 3 : 126 + (slot - 31) * 3);
}

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
#define KXE_SKINNED_VERTEX_BYTES 32     // a KxeVertex, then the bones and weight
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
static uint32_t PackUnitNormal(double x, double y, double z) {
    int nx = (int)lround(-x * 1023.0);
    int ny = (int)lround(-y * 1023.0);
    int nz = (int)lround(-z * 511.0);
    return ((uint32_t)nx & 0x7ff) | (((uint32_t)ny & 0x7ff) << 11) | (((uint32_t)nz & 0x3ff) << 22);
}

static uint32_t PackNormal(const int8_t *n) {
    return PackUnitNormal(n[0] / 127.0, n[1] / 127.0, n[2] / 127.0);
}

static uint32_t PackSkinNormal(const Ps2SkinVertex *v) {
    double x = v->normalX, y = v->normalY, z = v->normalZ;
    double length = sqrt(x * x + y * y + z * z);
    if (length == 0)
        return 0;
    return PackUnitNormal(x / length, y / length, z / length);
}

// The skinned vertex's bones as the Xbox's vertex word (spec-mesh.md 7.1): bone A's and bone B's constant registers
// (their slots in the palette), then bone A's weight as a u16, its low byte first
static uint32_t SkinBoneWord(int slotA, int slotB, float weightA) {
    int weight = (int)lround(weightA * 65535.0);
    weight = weight < 0 ? 0 : weight > 0xffff ? 0xffff : weight;
    return SkinSlotRegister(slotA) | (uint32_t)SkinSlotRegister(slotB) << 8 | (uint32_t)weight << 16;
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
    std::vector<uint32_t> boneWords;    // a skinned mesh's, one a vertex
    std::vector<uint8_t> bones;         // a skinned mesh's bone list
    std::vector<std::vector<uint8_t>> palettes;     // and its palettes: the bone list, or parts of it if too long
    std::vector<KxePrimitive> primitives;
    std::vector<uint16_t> indices;      // every primitive's, one after another
    size_t primitiveStart = 0;          // where the open primitive's indices start
    int palette = -1;                   // the open primitive's

    // A new primitive for another texture, state or palette; one that changes the palette loads it (1-based)
    void BeginPrimitive(int texture, uint16_t stateFlags, int nextPalette = -1) {
        if (!primitives.empty() && primitives.back().tex0 == texture && primitives.back().stateFlags == stateFlags &&
            nextPalette == palette)
            return;
        EndPrimitive();
        KxePrimitive p = {};
        p.tex0 = (uint16_t)texture;
        p.tex1 = PRIM_NO_TEXTURE;
        p.stateFlags = stateFlags;
        if (nextPalette != palette)
            p.bonePalette = (uint8_t)(nextPalette + 1);
        palette = nextPalette;
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
            uint8_t load = primitives.back().bonePalette;
            primitives.pop_back();
            if (load != 0)          // the next primitive loads it instead
                palette = primitives.empty() ? -1 : PaletteAfter(primitives.size());
            return;
        }
        primitives.back().count = (uint32_t)(n - 2);
    }

    size_t Position() const { return indices.size() - primitiveStart; }

    // The palette loaded once the first count primitives are drawn
    int PaletteAfter(size_t count) const {
        for (size_t p = count; p-- > 0;)
            if (primitives[p].bonePalette != 0)
                return primitives[p].bonePalette - 1;
        return -1;
    }

    // The palette for a batch using these bones (places in the bone list): the open one if they fit, else a new one
    int PaletteFor(const std::vector<int> &used) {
        if (bones.size() <= XBOX_PALETTE_SLOTS) {
            if (palettes.empty())
                palettes.push_back(bones);
            return 0;
        }
        if (!palettes.empty()) {
            std::vector<uint8_t> &open = palettes.back();
            size_t room = XBOX_PALETTE_SLOTS - open.size();
            size_t missing = 0;
            for (int b : used)
                missing += std::find(open.begin(), open.end(), bones[b]) == open.end();
            if (missing <= room) {
                for (int b : used)
                    if (std::find(open.begin(), open.end(), bones[b]) == open.end())
                        open.push_back(bones[b]);
                return (int)palettes.size() - 1;
            }
        }
        palettes.emplace_back();
        for (int b : used)
            palettes.back().push_back(bones[b]);
        return (int)palettes.size() - 1;
    }

    int Slot(int paletteIndex, int place) const {
        const std::vector<uint8_t> &p = palettes[paletteIndex];
        return (int)(std::find(p.begin(), p.end(), bones[place]) - p.begin());
    }

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

// A skinned leaf's chain as the VIF receives it: the words of the VIF codes and their data, from the chain (CNT) or
// the skin vertices (the skin's REF tags); a texture REF instead changes the texture
struct ChainWord {
    const uint8_t *data;    // NULL: the texture changes
    int texture;
};

static bool ReadSkinChain(const uint8_t *file, uint32_t fileBytes, const Ps2GfxBox &box, const Ps2SkinRecord &skin,
                          std::vector<ChainWord> &words) {
    std::vector<int32_t> refTags;
    for (uint32_t at = skin.refTags; at + 4 <= fileBytes; at += 4) {
        int32_t offset;
        memcpy(&offset, file + at, sizeof(offset));
        if (offset == -1)
            break;
        refTags.push_back(offset);
    }
    std::sort(refTags.begin(), refTags.end());
    const uint8_t *chain = file + box.vif;
    for (uint32_t at = 0; at + DMA_TAG_BYTES <= box.vifBytes;) {
        uint32_t tag[4];
        memcpy(tag, chain + at, sizeof(tag));
        uint32_t qwc = tag[0] & 0xffff;
        int id = (tag[0] >> 28) & 7;
        bool skinRef = std::binary_search(refTags.begin(), refTags.end(), (int32_t)(at / 4));
        if (id == DMA_REF && !skinRef)
            words.push_back({ NULL, (int)tag[1] });
        words.push_back({ chain + at + 8, 0 });
        words.push_back({ chain + at + 12, 0 });
        if (id == DMA_CNT) {
            if (at + DMA_TAG_BYTES + qwc * 16 > box.vifBytes)
                return false;
            for (uint32_t w = 0; w < qwc * 4; w++)
                words.push_back({ chain + at + DMA_TAG_BYTES + w * 4, 0 });
            at += DMA_TAG_BYTES + qwc * 16;
        } else if (id == DMA_REF) {
            // A skin REF's address is from the vertices (the first can start a quadword before them); a texture's
            // registers are a VIF packet of their own, which psiCreateEntityGfx fills: not read here
            if (skinRef) {
                int64_t from = (int64_t)skin.vertices + (int32_t)tag[1];
                if (from < 4 || from + qwc * 16 > fileBytes)
                    return false;
                for (uint32_t w = 0; w < qwc * 4; w++)
                    words.push_back({ file + from + w * 4, 0 });
            }
            at += DMA_TAG_BYTES;
        } else if (id == DMA_END || id == DMA_RET) {
            break;
        } else {
            return false;
        }
    }
    return true;
}

// The batch the VU1 is about to draw, from what was unpacked where: the GIF tag, vertex k's skin vertex (its
// position, with its normal two quadwords on) and colour after the tag, its ST in an array of their own
static void AddSkinnedBatch(MeshBuilder &mesh, const uint8_t *const *vu, int tagAt, int stAt, const Ps2SkinRecord &skin,
                            const uint8_t *file, int texture, uint16_t states, Ps2MeshStats *stats) {
    uint64_t tag;
    memcpy(&tag, vu[tagAt], sizeof(tag));
    int n = (int)(tag & 0x7fff);
    int prim = (int)((tag >> 47) & 7);
    if (prim != GS_PRIM_TRIANGLE_STRIP && prim != GS_PRIM_TRIANGLE_FAN) {
        stats->otherPrims++;
        return;
    }
    const uint8_t *first = file + skin.vertices;
    const uint8_t *end = first + skin.vertexBytes / sizeof(Ps2SkinVertex) * sizeof(Ps2SkinVertex);
    std::vector<const Ps2SkinVertex *> skinned(n);
    std::vector<float> st(n * 2), xyz(n * 3);
    std::vector<uint8_t> colour(n * 4);
    std::vector<int8_t> normal(n * 4);
    for (int k = 0; k < n; k++) {
        const uint8_t *position = vu[(tagAt + 1 + SKIN_BATCH_QWORDS * k) % VU1_QWORDS];
        const uint8_t *rgba = vu[(tagAt + 2 + SKIN_BATCH_QWORDS * k) % VU1_QWORDS];
        const uint8_t *normalAt = vu[(tagAt + 3 + SKIN_BATCH_QWORDS * k) % VU1_QWORDS];
        const uint8_t *stAtK = vu[(stAt + k) % VU1_QWORDS];
        if (position == NULL || position < first || position >= end || (position - first) % sizeof(Ps2SkinVertex) != 0 ||
            normalAt != position + 16 || rgba == NULL || stAtK == NULL) {
            stats->unresolved++;
            stats->badBatches++;
            return;
        }
        const Ps2SkinVertex *v = (const Ps2SkinVertex *)position;
        skinned[k] = v;
        memcpy(&xyz[k * 3], v->position, sizeof(v->position));
        memcpy(&colour[k * 4], rgba, 4);
        memcpy(&st[k * 2], stAtK, 8);
        // Enough for the winding's vote; the vertex gets the full normal below
        double length = sqrt((double)v->normalX * v->normalX + (double)v->normalY * v->normalY +
                             (double)v->normalZ * v->normalZ);
        double scale = length > 0 ? 127.0 / length : 0;
        normal[k * 4 + 0] = (int8_t)lround(v->normalX * scale);
        normal[k * 4 + 1] = (int8_t)lround(v->normalY * scale);
        normal[k * 4 + 2] = (int8_t)lround(v->normalZ * scale);
        normal[k * 4 + 3] = (int8_t)((v->flags & PS2_SKIN_ADC) ? PS2_ADC_SKIP : 0);
    }
    std::vector<int> used;
    for (const Ps2SkinVertex *v : skinned) {
        for (int place : { v->boneA & PS2_SKIN_BONE_MASK, v->boneB & PS2_SKIN_BONE_MASK }) {
            if (place >= (int)mesh.bones.size()) {
                stats->badBatches++;
                return;
            }
            if (std::find(used.begin(), used.end(), place) == used.end())
                used.push_back(place);
        }
    }
    if (used.size() > XBOX_PALETTE_SLOTS) {
        stats->badBatches++;
        return;
    }
    Batch batch = { n, prim, st.data(), xyz.data(), colour.data(), normal.data(), n, n, n, n };
    (prim == GS_PRIM_TRIANGLE_STRIP ? stats->strips : stats->fans)++;
    stats->batches++;
    int palette = mesh.PaletteFor(used);
    size_t base = mesh.vertices.size();
    mesh.BeginPrimitive(texture, states, palette);
    AddBatch(mesh, batch, stats);
    for (int k = 0; k < n; k++) {
        const Ps2SkinVertex *v = skinned[k];
        mesh.vertices[base + k].normal = PackSkinNormal(v);
        mesh.boneWords.push_back(SkinBoneWord(mesh.Slot(palette, v->boneA & PS2_SKIN_BONE_MASK),
                                              mesh.Slot(palette, v->boneB & PS2_SKIN_BONE_MASK), v->weight));
    }
}

// A skinned leaf's chain run through the VIF into a model of VU1 memory (where each quadword's data came from),
// a batch at each MSCNT that follows a GIF tag, colours and STs
static bool ReadSkinnedLeaf(MeshBuilder &mesh, const uint8_t *file, uint32_t fileBytes, const Ps2GfxBox &box,
                            const Ps2SkinRecord &skin, Ps2MeshStats *stats) {
    std::vector<ChainWord> words;
    if (!ReadSkinChain(file, fileBytes, box, skin, words)) {
        stats->unknownVif = true;
        return false;
    }
    std::vector<const uint8_t *> vu(VU1_QWORDS, nullptr);
    int cycle = 1, write = 1;           // STCYCL's CL and WL
    int texture = PRIM_NO_TEXTURE;
    uint16_t states = PRIM_STATE_DEFAULT;
    int tagAt = -1, stAt = -1, colourAt = -1;
    for (size_t i = 0; i < words.size();) {
        if (words[i].data == NULL) {
            texture = words[i].texture;
            i++;
            continue;
        }
        uint32_t code;
        memcpy(&code, words[i].data, sizeof(code));
        i++;
        uint16_t imm = (uint16_t)code;
        uint8_t num = (uint8_t)(code >> 16);
        uint8_t cmd = (uint8_t)(code >> 24) & 0x7f;
        if (cmd >= VIF_UNPACK) {
            int count = num != 0 ? num : 256;
            size_t per = cmd == VIF_UNPACK_V4_32 ? 4 : cmd == VIF_UNPACK_V3_32 ? 3 : cmd == VIF_UNPACK_V2_32 ? 2
                       : cmd == VIF_UNPACK_V4_8 ? 1 : 0;
            if (per == 0 || i + count * per > words.size()) {
                stats->unknownVif = true;
                return false;
            }
            // With CL above WL, WL quadwords are written, then CL - WL left as they were
            int address = imm & (VU1_QWORDS - 1);
            int at = address;
            for (int k = 0; k < count; k++) {
                vu[at % VU1_QWORDS] = words[i + k * per].data;
                at++;
                if (write < cycle && (k + 1) % write == 0)
                    at += cycle - write;
            }
            if (cmd == VIF_UNPACK_V4_32 && count == 1)
                tagAt = address;
            else if (cmd == VIF_UNPACK_V2_32)
                stAt = address;
            else if (cmd == VIF_UNPACK_V4_8 && (imm & VIF_UNPACK_UNSIGNED))
                colourAt = address;
            i += count * per;
            continue;
        }
        switch (cmd) {
        case VIF_STCYCL:
            cycle = imm & 0xff;
            write = imm >> 8;
            if (write == 0 || cycle == 0)
                cycle = write = 1;
            break;
        case VIF_STROW:
        case VIF_STCOL:
            i += 4;
            break;
        case VIF_STMASK:
            i += 1;
            break;
        case VIF_MPG:
            i += (num != 0 ? num : 256) * 2;
            break;
        case VIF_DIRECT:
        case VIF_DIRECTHL: {
            uint32_t quadwords = imm != 0 ? imm : 0x10000;
            if (i + quadwords * 4 > words.size()) {
                stats->unknownVif = true;
                return false;
            }
            // The packet is the chain's own (a CNT's data), in one piece
            if (words[i + quadwords * 4 - 1].data == words[i].data + (quadwords * 4 - 1) * 4)
                states = DirectStates(words[i].data, quadwords, states);
            i += quadwords * 4;
            break;
        }
        case VIF_MSCNT:
        case VIF_MSCAL:
        case VIF_MSCALF:
            if (tagAt >= 0 && stAt >= 0 && colourAt >= 0)
                AddSkinnedBatch(mesh, vu.data(), tagAt, stAt, skin, file, texture, states, stats);
            tagAt = stAt = colourAt = -1;
            break;
        case VIF_NOP: case VIF_OFFSET: case VIF_BASE: case VIF_ITOP: case VIF_STMOD:
        case VIF_MSKPATH3: case VIF_MARK: case VIF_FLUSHE: case VIF_FLUSH: case VIF_FLUSHA:
            break;
        default:
            stats->unknownVif = true;
            return false;
        }
    }
    return true;
}

// d3dGetVertexDataSize and d3dGetIndexDataSize, which psiCreateEntityGfx steps over the sections with: the data and
// three bytes to spare
static uint32_t KxeVertexBytes(int vtxCnt, bool skinned) {
    return vtxCnt * (skinned ? KXE_SKINNED_VERTEX_BYTES : sizeof(KxeVertex)) + 3;
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
// vertices and the indices, which d3dCreateVertexBuffers and d3dCreateIndexBuffer move their data up into - and a
// skinned mesh's one bone palette after the primitives
static void WriteKxe(const MeshBuilder &mesh, std::vector<uint8_t> &out) {
    bool skinned = !mesh.palettes.empty();
    int vtxCnt = (int)mesh.vertices.size();
    int idxCnt = (int)mesh.indices.size();
    int primitiveCnt = (int)mesh.primitives.size();
    uint32_t vertexBytes = KxeVertexBytes(vtxCnt, skinned);
    uint32_t indexBytes = KxeIndexBytes(idxCnt);
    size_t size = offsetof(ModelData, data) + vertexBytes + indexBytes + primitiveCnt * sizeof(KxePrimitive) +
                  mesh.palettes.size() * XBOX_PALETTE_BYTES;
    out.assign((size + 3) & ~(size_t)3, 0xff);

    ModelData header;
    memset(&header, 0, offsetof(ModelData, data));
    header.magicTag = MODELDATA_MAGIC;
    header.unknown4 = 0xffffffff;
    header.vtxCnt = vtxCnt;
    header.idxCnt = idxCnt;
    header.primitiveCnt = primitiveCnt;
    header.skinGroupCnt = (int)mesh.palettes.size();
    int triangles = 0;
    for (const KxePrimitive &p : mesh.primitives)
        triangles += (int)p.count;
    header.dataSize14 = triangles;
    header.dataSize18 = DegenerateTriangles(mesh);
    memcpy(out.data(), &header, offsetof(ModelData, data));

    uint8_t *at = out.data() + offsetof(ModelData, data);
    if (skinned) {
        for (int v = 0; v < vtxCnt; v++) {
            memcpy(at + v * KXE_SKINNED_VERTEX_BYTES, &mesh.vertices[v], sizeof(KxeVertex));
            memcpy(at + v * KXE_SKINNED_VERTEX_BYTES + sizeof(KxeVertex), &mesh.boneWords[v], sizeof(uint32_t));
        }
    } else {
        memcpy(at, mesh.vertices.data(), vtxCnt * sizeof(KxeVertex));
    }
    at += vertexBytes;
    memcpy(at, mesh.indices.data(), idxCnt * sizeof(uint16_t));
    at += indexBytes;
    // The first primitive sets every state; the others those of theirs that differ from the one before
    KxePrimitive *prims = (KxePrimitive *)at;
    memcpy(prims, mesh.primitives.data(), primitiveCnt * sizeof(KxePrimitive));
    for (int p = 0; p < primitiveCnt; p++)
        prims[p].changeMask = p == 0 ? PRIM_CHANGE_ALL
                            : prims[p].stateFlags != prims[p - 1].stateFlags ? PRIM_CHANGE_STATES : 0;
    uint8_t *palettes = at + primitiveCnt * sizeof(KxePrimitive);
    for (size_t p = 0; p < mesh.palettes.size(); p++) {
        memset(palettes + p * XBOX_PALETTE_BYTES, XBOX_PALETTE_END, XBOX_PALETTE_BYTES);
        memcpy(palettes + p * XBOX_PALETTE_BYTES, mesh.palettes[p].data(), mesh.palettes[p].size());
    }
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
    if (footer.boxes == 0 || footer.numBoxes == 0)
        return false;

    MeshBuilder mesh;
    Ps2SkinRecord skin = {};
    if (footer.skin != 0) {
        if (footer.skin + sizeof(skin) > size)
            return false;
        memcpy(&skin, file + footer.skin, sizeof(skin));
        if (skin.vertices + skin.vertexBytes > size || skin.bones >= size)
            return false;
        for (uint32_t at = skin.bones; at < size && file[at] != XBOX_PALETTE_END; at++)
            mesh.bones.push_back(file[at]);
        stats->bones = (int)mesh.bones.size();
        if (mesh.bones.empty())
            return false;
    }
    for (uint32_t i = 0; i < footer.numBoxes; i++) {
        Ps2GfxBox box;
        memcpy(&box, file + footer.boxes + i * sizeof(Ps2GfxBox), sizeof(box));
        if (box.vif == 0)
            continue;
        stats->leaves++;
        stats->leafFlags |= box.flags;
        if (footer.skin != 0)
            ReadSkinnedLeaf(mesh, file, size, box, skin, stats);
        else
            ReadChain(mesh, file + box.vif, box.vifBytes, stats);
    }
    mesh.EndPrimitive();
    stats->primitives = (int)mesh.primitives.size();
    if (mesh.primitives.empty() || mesh.vertices.size() > 0xffff)
        return false;
    WriteKxe(mesh, out);
    return true;
}
