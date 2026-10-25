#include "GcMap.h"
#include "GcBytes.h"
#include "GcMesh.h"
#include "GcTexture.h"

#include <math.h>
#include <string.h>

#define BLOCK_HEADER_BYTES 4

// ---- The blocks' layouts, as the Xbox's parsemap.cpp reads them

// Block 0x10: a texture header (bmpHeaderEntry) is 12 bytes: two bytes, the width and height less one, the frames and
// rate bytes, the hashcode
#define TEXHEADER_BYTES 12
static void SwapTextureHeaders(uint8_t *p, uint32_t bytes) {
    for (uint32_t o = 0; o + TEXHEADER_BYTES <= bytes; o += TEXHEADER_BYTES) {
        GcSwap16(p + o + 2);
        GcSwap16(p + o + 4);
        GcSwap32(p + o + 8);
    }
}

// Block 0x1a: placements (TARGET_PLACEMENT, 0x4c bytes) each followed by its key pairs. The entity number and the
// spline's three halfwords are 16-bit; bytes 2-3 and 0x46-0x47 are padding, whose junk differs between the discs.
#define PLACEMENT_BYTES 0x4c
static void SwapPlacements(uint8_t *p, uint32_t bytes) {
    uint32_t o = 0;
    while (o + PLACEMENT_BYTES <= bytes) {
        uint32_t keys = GcBe32(p + o + 0x48);
        GcSwap16(p + o);
        GcSwapWords(p + o + 4, 0x3c);
        GcSwap16(p + o + 0x40);
        GcSwap16(p + o + 0x42);
        GcSwap16(p + o + 0x44);
        GcSwap32(p + o + 0x48);
        uint32_t keyBytes = keys * 8;
        if (o + PLACEMENT_BYTES + keyBytes > bytes)
            break;
        GcSwapWords(p + o + PLACEMENT_BYTES, keyBytes);
        o += PLACEMENT_BYTES + keyBytes;
    }
}

// Block 0x2f: the collision (block_coll_data_header, after the header word): the version, three counts, the vertex
// blocks (64 bytes of halfwords: the planes' floats are written as two halfwords, low half first, on both), the boxes
// (COLLBOX_tag) and the triangles (four halfwords). A box's quantisation scale is a float on the Xbox and on the
// GameCube a power of two's exponent: n for 2^-n, 0 for 0.
#define COLLBOX_BYTES 0x30
#define COLL_VERTEX_BLOCK_BYTES 64
#define COLL_TRIANGLE_BYTES 8
static void SwapCollision(uint8_t *p, uint32_t bytes) {
    if (bytes < 12)
        return;
    uint32_t version = GcBe32(p);
    uint32_t boxes = GcBe16(p + 4), triangles = GcBe16(p + 6), vertexBlocks = GcBe16(p + 8);
    GcSwap32(p);
    for (int k = 0; k < 4; k++)
        GcSwap16(p + 4 + k * 2);
    uint32_t o = 12;
    if (version == 5) {             // the older layout's 16 bytes
        GcSwapWords(p + o, 16);
        o += 16;
    }
    uint32_t end = o + vertexBlocks * COLL_VERTEX_BLOCK_BYTES + boxes * COLLBOX_BYTES + triangles * COLL_TRIANGLE_BYTES;
    if (end > bytes)
        return;
    for (uint32_t i = 0; i < vertexBlocks * COLL_VERTEX_BLOCK_BYTES; i += 2)
        GcSwap16(p + o + i);
    o += vertexBlocks * COLL_VERTEX_BLOCK_BYTES;
    for (uint32_t b = 0; b < boxes; b++, o += COLLBOX_BYTES) {
        GcSwapWords(p + o, 12);             // boundMin
        GcSwap16(p + o + 0x0c);
        GcSwap16(p + o + 0x0e);
        GcSwapWords(p + o + 0x10, 12);      // boundMax
        GcSwap16(p + o + 0x1c);
        GcSwap16(p + o + 0x1e);
        GcSwapWords(p + o + 0x20, 12);      // the quantisation's offset
        uint32_t exponent = GcBe32(p + o + 0x2c);
        float scale = exponent == 0 ? 0.0f : ldexpf(1.0f, -(int)exponent);
        memcpy(p + o + 0x2c, &scale, 4);
    }
    for (uint32_t t = 0; t < triangles * 4; t++, o += 2)
        GcSwap16(p + o);
}

bool GcMap_SwapBlock(int id, uint8_t *data, uint32_t bytes) {
    switch (id) {
    case GC_BLOCK_MAP_HEADER:
    case GC_BLOCK_PATH:
    case GC_BLOCK_AMBIENT_RADIATORS:
    case GC_BLOCK_LOD:
    case GC_BLOCK_SOUNDS:
        GcSwapWords(data, bytes);
        return true;
    case GC_BLOCK_ENTITY_PARAMS:
        // Twelve words, then the name (upper-cased on the GameCube's disc)
        GcSwapWords(data, bytes < 0x30 ? bytes : 0x30);
        return true;
    case GC_BLOCK_TEXTURE_HEADER:
        SwapTextureHeaders(data, bytes);
        return true;
    case GC_BLOCK_MAP_DATA:
        SwapPlacements(data, bytes);
        return true;
    case GC_BLOCK_COLLISION:
        SwapCollision(data, bytes);
        return true;
    case GC_BLOCK_PALETTE_HEADER:   // these little-endian on the GameCube's disc too (a palette header's first word
    case GC_BLOCK_PORTALS:          // is junk, each disc's own)
    case GC_BLOCK_AI_PATHS:
    case GC_BLOCK_HASHLIST:
    case GC_BLOCK_PARTICLES:
    case GC_BLOCK_MEMORY_DISCARD:
    case GC_BLOCK_END:
        return true;
    default:
        return false;
    }
}

static void PutBlock(std::vector<uint8_t> &out, int id, const uint8_t *data, uint32_t bytes) {
    size_t at = out.size();
    out.resize(at + BLOCK_HEADER_BYTES + bytes);
    GcPutLe32(&out[at], (uint32_t)id << 24 | (BLOCK_HEADER_BYTES + bytes));
    if (bytes != 0)
        memcpy(&out[at + BLOCK_HEADER_BYTES], data, bytes);
}

void GcMap_Convert(const uint8_t *pack, uint32_t bytes, const uint8_t *bundle, uint32_t bundleBytes,
                   std::vector<uint8_t> &out, GcMapStats *stats) {
    memset(stats, 0, sizeof(*stats));
    out.clear();
    out.reserve(bytes + bytes / 4);
    if (bytes < 4)
        return;
    out.insert(out.end(), pack, pack + 4);      // the version word (0 on both)

    std::vector<uint8_t> textureHeaders;        // block 0x10, the Xbox's way round: frames and rate per texture
    int texture = 0;
    std::vector<uint8_t> block, kxt, kxe;
    uint32_t o = 4;
    while (o + BLOCK_HEADER_BYTES <= bytes) {
        uint32_t header = GcLe32(pack + o);
        uint32_t size = header & 0xffffff;
        int id = (int)(header >> 24);
        if (size < BLOCK_HEADER_BYTES || o + size > bytes)
            break;
        stats->blocks++;
        block.assign(pack + o + BLOCK_HEADER_BYTES, pack + o + size);
        uint32_t dataBytes = size - BLOCK_HEADER_BYTES;

        if (id == GC_BLOCK_ENTITY_GEOMETRY) {
            GcMeshStats mesh;
            if (GcMesh_ConvertToKxe(pack + o + BLOCK_HEADER_BYTES, bundle, bundleBytes, kxe, &mesh)) {
                stats->meshesConverted++;
                if (mesh.skinned)
                    stats->meshesSkinned++;
                PutBlock(out, GC_BLOCK_ENTITY_GEOMETRY_XBOX, kxe.data(), (uint32_t)kxe.size());
            } else {
                stats->meshesSkipped++;
            }
        } else if (id == GC_BLOCK_TEXTURE_DATA) {
            int frames = 1, rate = 0;
            if ((texture + 1) * TEXHEADER_BYTES <= (int)textureHeaders.size()) {
                frames = textureHeaders[texture * TEXHEADER_BYTES + 6];
                rate = textureHeaders[texture * TEXHEADER_BYTES + 7];
            }
            texture++;
            if (GcTexture_ToKxt(block.data(), dataBytes, frames, rate, kxt)) {
                if ((GcBe32(block.data()) >> 16) == GC_TEX_DUPLICATE)
                    stats->duplicates++;
                else
                    stats->texturesConverted++;
                PutBlock(out, GC_BLOCK_TEXTURE_DATA_XBOX, kxt.data(), (uint32_t)kxt.size());
            } else {
                // A header of zeros: psiCreateMapTextures takes it for no texture, gives it no slot and writes its
                // frames, rate and slot fields, which must be there
                stats->texturesFailed++;
                uint8_t none[0x58] = {};
                PutBlock(out, GC_BLOCK_TEXTURE_DATA_XBOX, none, sizeof(none));
            }
        } else {
            if (!GcMap_SwapBlock(id, block.data(), dataBytes))
                stats->unknownBlocks++;
            if (id == GC_BLOCK_TEXTURE_HEADER)
                textureHeaders = block;
            PutBlock(out, id, block.data(), dataBytes);
        }
        o += size;
        if (id == GC_BLOCK_END)
            break;
    }
    // The entry's length stays a multiple of 32, as the bundles' are
    while (out.size() % 32 != 0)
        out.push_back(0);
}
