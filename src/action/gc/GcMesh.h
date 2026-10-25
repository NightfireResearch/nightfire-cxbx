#ifndef GC_GCMESH_H_
#define GC_GCMESH_H_

// The GameCube's meshes (block 0x0c, docs/gc-data.md) made the Xbox's (block 0x0d, a KXE: ModelData, its vertices,
// strip indices and primitives), which psiCreateEntityGfx registers as it does the Xbox's. No game state: the offline
// checker uses it too.
//
// Block 0x0c is a header of big-endian words (GcMeshHeader) whose offsets count from the header itself and reach past
// its pack into the bundle's "GC EXTRA" entries, which the GameCube loads in the same buffer: the vertex arrays in
// GC EXTRA STATIC (kept), the strips and the index streams in GC EXTRA DISCARD (freed once the display lists are
// built). GC psiCreateEntityGfx (0x8002f864) draws each mesh once into a display list (maybe_psiDrawClonedObject
// 0x8002ea08), which later draws call:
//
// - per layer (eight), each strip group with a texture in that layer: its strips, as GX triangle strips;
// - a strip vertex is a position-and-normal index (one serves both: the arrays interleave them), then a colour index
//   when the mesh has colours, then a texture coordinate index;
// - the position indices are walked once per layer, from the first group; a group's coordinate and colour indices
//   come once for each layer it has a texture in, the layers in order;
// - positions are signed 16-bit with 16 - positionShift fraction bits (or floats), plus the header's centre;
//   normals 16-bit with 14 fraction bits (or 8-bit with 7), pointing the other way to the Xbox's; coordinates 16-bit
//   with 16 - uvShift fraction bits; colours RGBA8;
// - a group's word bit 28 draws it with no culling, else back faces culled: the Xbox primitive's double-sided bit.
//
// A skinned mesh's positions are in bind pose and come in runs, each moved by one skeleton bone or blended with a
// second by two weights per position, which GC SkinIt (0x8000c740) applies on the CPU every frame. A head's morph
// targets are deltas in the positions' fixed point for a list of positions, which GC 0x8000c9cc adds first.

#include <stdint.h>
#include <vector>

#define GC_MESH_MAGIC 0x0f
#define GC_MESH_LAYERS 8

// The header, as GC maybe_psiDrawClonedObject reads it; big-endian in the file
#pragma pack(push, 1)
struct GcMeshHeader {
    uint32_t magic;             // 0x00 GC_MESH_MAGIC
    char name[20];              // 0x04
    uint32_t groups;            // 0x18 strip groups
    uint32_t positionIndices;   // 0x1c
    uint32_t positions;         // 0x20 position-and-normal entries
    uint32_t unknown24;
    uint32_t morphTargets;      // 0x28 a head's (22), else 0
    uint32_t morphVertexCount;  // 0x2c the positions the targets move
    uint32_t flags;             // 0x30 GC_MESH_NO_Z_WRITE
    uint32_t drawFlags;         // 0x34 bit 2: fogged
    uint32_t layerFlags[GC_MESH_LAYERS];    // 0x38 GC_LAYER_*
    int8_t floatPositions[3];   // 0x58 -1 -1 -1: float positions; else 0
    uint8_t positionShift;      // 0x5b
    uint8_t unknown5c[3];
    uint8_t uvShift;            // 0x5f
    float centre[3];            // 0x60 added to every position
    uint32_t unknown6c;
    uint32_t groupList;         // 0x70 GcStripGroup each
    uint32_t stripLengths;      // 0x74 halfwords: each strip's vertices less two
    uint32_t positionIndexList; // 0x78 halfwords
    uint32_t positionArray;     // 0x7c positions and normals, 12 bytes each (16 for floats)
    uint32_t uvIndexList;       // 0x80 halfwords
    uint32_t uvArray;           // 0x84 two halfwords each
    uint32_t colourIndexList;   // 0x88 halfwords
    uint32_t colourArray;       // 0x8c RGBA8; 0 for none
    uint32_t skinWeights;       // 0x90 two halfwords a position: 0 for a rigid mesh
    uint32_t skinRuns;          // 0x94 a bone and a count of positions each, halfwords
    uint32_t skinSecondBones;   // 0x98 each run's second bone (0xff for none) and its count again
    uint32_t morphDeltaList;    // 0x9c per morph position, morphTargets deltas of three halfwords
    uint32_t morphVertexList;   // 0xa0 the morph positions, halfwords, rising
    uint32_t displayListBytes;  // 0xa4
    uint32_t unknownA8;
};
struct GcStripGroup {
    uint32_t counts;            // GC_GROUP_*
    int16_t texture[GC_MESH_LAYERS];    // the pack's texture per layer, -1 for none
};
#pragma pack(pop)
static_assert(sizeof(GcMeshHeader) == 0xac, "GcMeshHeader is 0xac bytes");
static_assert(sizeof(GcStripGroup) == 0x14, "GcStripGroup is 0x14 bytes");

#define GC_MESH_NO_Z_WRITE 0x200
#define GC_LAYER_ENVIRONMENT 0x01   // an environment map: coordinates from the normals
#define GC_LAYER_ADD 0x02           // additive; no depth writes
#define GC_LAYER_SUBTRACT 0x04      // subtractive; no depth writes
#define GC_LAYER_TWO_TEXTURES 0x40  // the next layer's texture as a second stage, in the same pass
#define GC_GROUP_VERTICES(c) ((c) & 0xffff)         // the strips' vertices less two each
#define GC_GROUP_STRIPS(c) (((c) >> 16) & 0xfff)
#define GC_GROUP_DOUBLE_SIDED 0x10000000
#define GC_GROUP_UV_SETS(c) (((c) >> 29) + 1)       // one per layer it has a texture in

// What a conversion met, for the checker
struct GcMeshStats {
    bool skinned;
    int palettes;               // a skinned mesh's bone palettes
    int morphTargets;           // a head's: the morph streams written
    int morphVertices;          // the vertices they cover (the KXE's first)
    int groups;
    int strips;
    int vertices;               // the KXE's
    int triangles;              // drawn, degenerate ones left out
    int doubleSidedTriangles;
    int primitives;
    int layers;                 // layers drawn after the first
    bool outOfRange;            // an offset or index past the bundle: not converted
};

// The mesh whose header is at header (inside bundle, bundleBytes long) to a KXE in out. False when there is nothing
// to draw (no strips) or the data runs past the bundle.
bool GcMesh_ConvertToKxe(const uint8_t *header, const uint8_t *bundle, uint32_t bundleBytes, std::vector<uint8_t> &out,
                         GcMeshStats *stats);

// One strip vertex as the GameCube draws it, for the checker
struct GcMeshVertex {
    float position[3];
    float normal[3];            // as the GameCube has it (the Xbox's points the other way)
    uint8_t colour[4];          // R, G, B, A; white when the mesh has none
    float uv[2];
};
struct GcMeshStrip {
    int layer;
    int group;
    int texture;
    int secondTexture;          // -1 for none
    uint32_t counts;            // the group's word
    std::vector<GcMeshVertex> vertices;
};

// The mesh's strips in the order the GameCube draws them; false as GcMesh_ConvertToKxe
bool GcMesh_ReadStrips(const uint8_t *header, const uint8_t *bundle, uint32_t bundleBytes,
                       std::vector<GcMeshStrip> &strips, GcMeshStats *stats);

#endif // GC_GCMESH_H_
