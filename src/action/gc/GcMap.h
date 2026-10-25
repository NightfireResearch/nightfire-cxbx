#ifndef GC_GCMAP_H_
#define GC_GCMAP_H_

// A GameCube map pack (a bundle's type 0, 1, 2 or 11 entry, docs/gc-data.md) made the Xbox's, block by block. The
// block headers are little-endian on both; most blocks are big-endian on the GameCube's disc, and some (palette
// headers, portals, hashlists, AI paths, particles) are as the Xbox has them. Each block is turned round field by
// field, the textures (0x17) become the Xbox's (0x18, GcTexture.h), and the meshes (0x0c, whose data is in the
// bundle's "GC EXTRA" entries) are left out for now: their entities have no geometry. No game state: the offline
// checker uses it too.

#include <stdint.h>
#include <vector>

// What a conversion did, for the checker and the log
struct GcMapStats {
    int blocks;
    int texturesConverted;
    int texturesFailed;
    int duplicates;         // "DT" texture headers
    int meshesSkipped;      // 0x0c blocks left out
    int unknownBlocks;      // blocks of an id this does not know, copied as they are
};

// The block ids the GameCube's packs hold (GC parsemap_handle_block_id, 0x80058930)
enum GcMapBlock {
    GC_BLOCK_AI_PATHS = 0x05,
    GC_BLOCK_ENTITY_GEOMETRY = 0x0c,    // the GameCube's mesh header; the data is in the bundle's GC EXTRA entries
    GC_BLOCK_MAP_HEADER = 0x0e,
    GC_BLOCK_PALETTE_HEADER = 0x0f,
    GC_BLOCK_TEXTURE_HEADER = 0x10,
    GC_BLOCK_TEXTURE_DATA = 0x17,
    GC_BLOCK_TEXTURE_DATA_XBOX = 0x18,
    GC_BLOCK_PATH = 0x19,
    GC_BLOCK_MAP_DATA = 0x1a,
    GC_BLOCK_MEMORY_DISCARD = 0x1c,
    GC_BLOCK_END = 0x1d,
    GC_BLOCK_ENTITY_PARAMS = 0x1f,
    GC_BLOCK_PORTALS = 0x21,
    GC_BLOCK_AMBIENT_RADIATORS = 0x26,
    GC_BLOCK_LOD = 0x27,
    GC_BLOCK_SOUNDS = 0x28,
    GC_BLOCK_HASHLIST = 0x2c,
    GC_BLOCK_COLLISION = 0x2f,
    GC_BLOCK_PARTICLES = 0x30,
};

// One block's data (after its header word) turned round in place, for the blocks whose size does not change. False
// for an id it has no rule for.
bool GcMap_SwapBlock(int id, uint8_t *data, uint32_t bytes);

// The whole pack: pack is the entry's bytes, out gets the Xbox's
void GcMap_Convert(const uint8_t *pack, uint32_t bytes, std::vector<uint8_t> &out, GcMapStats *stats);

#endif // GC_GCMAP_H_
