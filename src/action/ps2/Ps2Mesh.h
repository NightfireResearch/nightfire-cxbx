#ifndef PS2_PS2MESH_H_
#define PS2_PS2MESH_H_

// The PS2's meshes (block 0x2d, docs/ps2-data.md) made the Xbox's (block 0x0d, a KXE: ModelData, its vertices, strip
// indices, primitives and bone palettes), which psiCreateEntityGfx registers as it does the Xbox's. No game state:
// the offline checker uses it too.
//
// A 0x2d block, offsets from the block's header word:
//   +0x04 u32 size (the footer is at +size), two zero words, the data, then the footer (Ps2GfxFooter)
//   a box tree (Ps2GfxBox); the leaves hold a VIF chain each, which unpacks batches for the VU1 to draw: a GIF tag
//   (NLOOP vertices, a triangle strip or fan), the vertices' ST (V2-32), XYZ (V3-32), colour (V4-8 unsigned, 0x80 is
//   1.0) and normal (V4-8 signed, its w -1 to skip the triangle the vertex ends: the GS's ADC), each batch run by an
//   MSCNT. A STROW before the batches it applies to holds their texture in its first word: an index into the
//   pack's texture list (the box's texture list holds the offsets of those STROWs, which PS2 psiCreateEntityGfx
//   0x1173a8 patches with the GS's texture).
//
// A skinned mesh (the footer's skin record, Ps2SkinRecord) keeps its vertices apart, in bind pose (Ps2SkinVertex),
// which the PS2 skins every frame on the VU0 (SkinIt 0x10c128, into a copy CacheSkin 0x1114b8 makes). Its leaf
// holds a DMA chain instead: CNT tags carry the VIF codes, GIF tags, colours and STs; REF tags whose word offsets the
// skin record lists pull runs of skin vertices into the batch (with STCYCL so that vertex k's position and normal
// land beside its colour); other REF tags pull a texture's registers, their address the pack's texture index.
//
// A skinned head's morph targets come in block 0x2b, before its 0x2d (Ps2MorphHeader): float3 deltas in rows of 12
// bytes from the block's start, each morph vertex's targets in consecutive rows from the row its skin vertex names.
// SkinIt adds weight * row (index + target) to the bind position before skinning.
#include <stdint.h>
#include <vector>

#pragma pack(push, 1)
struct Ps2GfxFooter {
    uint32_t numBoxes;
    uint32_t boxes;         // offset of the box list, 0 for none
    uint32_t skin;          // offset of the skin record, 0 for a rigid mesh
    uint32_t unknownC;
};
struct Ps2GfxBox {
    float boundMin[3];
    float boundMax[3];
    uint32_t childA;        // 0x18 - box indices, -1 for none
    uint32_t childB;        // 0x1c
    uint32_t vif;           // 0x20 - offset of the VIF chain, 0 for a box that only holds others
    uint32_t textures;      // 0x24 - offset of the STROW offsets, ending -1
    uint32_t vifBytes;      // 0x28
    uint32_t stripCount;    // 0x2c
    uint32_t vertexCount;   // 0x30
    uint32_t flags;         // 0x34
};
struct Ps2SkinRecord {
    uint32_t vertices;      // offset of the Ps2SkinVertex array
    uint32_t vertexBytes;
    uint32_t refTags;       // offset of the REF tags' word offsets in the leaf's chain, ending -1
    uint32_t bones;         // offset of the bone list: skin bone numbers, ending 0xff
};
struct Ps2SkinVertex {      // 32 bytes
    float position[3];      // model space
    float weight;           // bone A's; bone B has the rest
    uint16_t boneA;         // bits 0-14: the bone's place in the bone list (the VU0's matrix)
    int16_t normalX;        // 64ths, pointing out of the mesh
    uint16_t boneB;
    int16_t normalY;
    uint16_t unused18;
    int16_t normalZ;
    uint16_t flags;         // PS2_SKIN_ADC: the triangle this vertex ends is not drawn
    int16_t morph;          // -1, or the vertex's first morph delta
};
struct Ps2MorphHeader {     // block 0x2b
    uint32_t vertices;      // morph vertices, each with a word in the list after this header
    uint32_t targets;       // 22 on every head
    uint32_t firstRow;      // the rows (12 bytes, from here) where the deltas start, past the list
};
#pragma pack(pop)
#define PS2_MORPH_ROW_BYTES 12
#define PS2_SKIN_NO_MORPH -1
#define PS2_SKIN_BONE_MASK 0x7fff
#define PS2_SKIN_ADC 0x8000
static_assert(sizeof(Ps2SkinRecord) == 0x10, "Ps2SkinRecord is 0x10 bytes");
static_assert(sizeof(Ps2SkinVertex) == 0x20, "Ps2SkinVertex is 0x20 bytes");
static_assert(sizeof(Ps2GfxFooter) == 0x10, "Ps2GfxFooter is 0x10 bytes");
static_assert(sizeof(Ps2GfxBox) == 0x38, "Ps2GfxBox is 0x38 bytes");

// What a conversion met, for the checker
struct Ps2MeshStats {
    int boxes;
    int leaves;
    int batches;            // drawn
    int strips;
    int fans;
    int otherPrims;         // GIF tags of another primitive, not drawn
    int badBatches;         // counts that disagree with the GIF tag, not drawn
    int vertices;
    int triangles;          // drawn (the ADC flag clear)
    int skippedTriangles;   // the ADC flag set
    int primitives;
    bool skinned;
    int bones;              // a skinned mesh's bone list
    int morphTargets;       // a head's: the morph streams written
    int morphVertices;      // the vertices they cover (the KXE's first)
    int unresolved;         // batch vertices with no skin vertex where the VU1 reads it, not drawn
    bool unknownVif;        // a VIF command this reader does not know stopped a chain
    uint32_t leafFlags;     // the leaves' flags, or'd together
};

// Converts the block (its data after the header word, blockBytes of it) to a KXE in out, with a skinned head's morph
// targets from the 0x2b block before it when given (else NULL). False when there is nothing to draw: no boxes or no
// batches.
bool Ps2Mesh_ConvertToKxe(const uint8_t *block, uint32_t blockBytes, const uint8_t *morph, uint32_t morphBytes,
                          std::vector<uint8_t> &out, Ps2MeshStats *stats);

#endif // PS2_PS2MESH_H_
