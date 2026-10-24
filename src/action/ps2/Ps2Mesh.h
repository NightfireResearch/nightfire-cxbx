#ifndef PS2_PS2MESH_H_
#define PS2_PS2MESH_H_

// The PS2's meshes (block 0x2d, docs/ps2-data.md) made the Xbox's (block 0x0d, a KXE: ModelData, its vertices, strip
// indices and primitives), which psiCreateEntityGfx registers as it does the Xbox's. Rigid meshes only so far: a
// skinned one (a skin record in the footer) converts to nothing. No game state: the offline checker uses it too.
//
// A 0x2d block, offsets from the block's header word:
//   +0x04 u32 size (the footer is at +size), two zero words, the data, then the footer (Ps2GfxFooter)
//   a box tree (Ps2GfxBox); the leaves hold a VIF chain each, which unpacks batches for the VU1 to draw: a GIF tag
//   (NLOOP vertices, a triangle strip or fan), the vertices' ST (V2-32), XYZ (V3-32), colour (V4-8 unsigned, 0x80 is
//   1.0) and normal (V4-8 signed, its w -1 to skip the triangle the vertex ends: the GS's ADC), each batch run by an
//   MSCNT. A STROW before the batches it applies to holds their texture in its first word: an index into the
//   pack's texture list (the box's texture list holds the offsets of those STROWs, which PS2 psiCreateEntityGfx
//   0x1173a8 patches with the GS's texture).

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
#pragma pack(pop)
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
    bool unknownVif;        // a VIF command this reader does not know stopped a chain
    uint32_t leafFlags;     // the leaves' flags, or'd together
};

// Converts the block (its data after the header word, blockBytes of it) to a KXE in out. False when there is nothing
// to draw: no boxes, a skinned mesh, or no batches.
bool Ps2Mesh_ConvertToKxe(const uint8_t *block, uint32_t blockBytes, std::vector<uint8_t> &out, Ps2MeshStats *stats);

#endif // PS2_PS2MESH_H_
