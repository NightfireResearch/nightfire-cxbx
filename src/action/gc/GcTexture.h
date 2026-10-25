#ifndef GC_GCTEXTURE_H_
#define GC_GCTEXTURE_H_

// The GameCube's map textures (docs/gc-data.md): block 0x17, a big-endian header of 0x5c bytes and then the texels
// in one of the GX's tiled formats, every mip level and every frame of an animated texture one after the other. They
// become the Xbox's texture header (a KXT), which psiCreateMapTextures registers as it does the Xbox's: CMPR as DXT1
// (the same blocks, reordered: nothing is lost), RGB5A3 and RGBA8 as swizzled A8R8G8B8. A "DT" header (another
// texture's frames) keeps its index. No game state: the offline checker uses it too.

#include <stdint.h>
#include <vector>

// The header, as GC psiCreateMapTextures (0x8002fd0c) reads it; big-endian in the file
#pragma pack(push, 1)
struct GcTextureHeader {
    uint32_t magic;         // 0x00 0 for a texture of its own, or 0x4454 << 16 | index (another's frames)
    char name[16];          // 0x04
    uint32_t pixelOffset;   // 0x14 written at load: where the texels went
    uint32_t pixelBytes;    // 0x18 all the texels, every level and frame
    uint16_t width;         // 0x1c
    uint16_t height;        // 0x1e
    uint32_t format;        // 0x20 GC_TEX_*: an index into the GC's table of GX formats (0x801c74ac)
    uint32_t unknown24;
    uint32_t unknown28;     // 1 in every texture
    uint32_t animSpeed;     // 0x2c the rate, made ticks per frame at load as the Xbox's
    uint32_t unknown30;
    uint32_t unknown34;
    uint32_t levels;        // 0x38 mip levels (GXInitTexObj takes its low byte)
    uint32_t unknown3c;
    uint8_t texObj[0x1c];   // 0x40 room for the GX texture object, written at load
};
#pragma pack(pop)
static_assert(sizeof(GcTextureHeader) == 0x5c, "GcTextureHeader is 0x5c bytes");

enum GcTextureFormat {
    GC_TEX_RGB5A3 = 6,      // 16 bits a texel in 4x4 tiles: RGB555, or (top bit clear) A3 RGB444
    GC_TEX_RGBA8 = 7,       // 32 bits a texel in 4x4 tiles, each 32 bytes of AR then 32 of GB
    GC_TEX_CMPR = 8,        // DXT1's blocks, four to an 8x8 tile, colours big-endian, texels first in the high bits
};

#define GC_TEX_DUPLICATE 0x4454     // "DT", the magic's high half

// The texture's frames, levels and bytes are what the header says, and the block holds them
bool GcTexture_Valid(const uint8_t *block, uint32_t blockBytes, int frames);

// One level of one frame to linear 32-bit texels, bytes B, G, R, A (the Xbox's A8R8G8B8). texels points at the level.
void GcTexture_DecodeLevel(const uint8_t *texels, int format, int width, int height, uint8_t *bgra);

// The bytes one level takes in the GameCube's tiles
uint32_t GcTexture_LevelBytes(int format, int width, int height);

// Block 0x17 to the Xbox's texture header and frames (a KXT): CMPR as DXT1, the others swizzled A8R8G8B8, with every
// level the GameCube has. frames and rate are the texture header's (block 0x10: its animFrames and someDivisor). A
// "DT" header becomes the Xbox's, with room for its frames' slots. False when the block is not one this reads.
bool GcTexture_ToKxt(const uint8_t *block, uint32_t blockBytes, int frames, int rate, std::vector<uint8_t> &kxt);

#endif // GC_GCTEXTURE_H_
