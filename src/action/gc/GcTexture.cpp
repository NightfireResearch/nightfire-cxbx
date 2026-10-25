#include "GcTexture.h"
#include "GcBytes.h"
#include "../engine/psiGraphics.h"   // TextureInfo

#include <string.h>

#define KXT_FRAME_SLACK 0x7f        // the Xbox's frames leave this much for RegisterTexture's move to 128 bytes
#define DXT1_BLOCK_BYTES 8

// A level's size: halved from the top level's, never under one texel
static int LevelSize(int size, int level) {
    size >>= level;
    return size < 1 ? 1 : size;
}

static int RoundUp(int value, int to) {
    return (value + to - 1) / to * to;
}

uint32_t GcTexture_LevelBytes(int format, int width, int height) {
    switch (format) {
    case GC_TEX_CMPR: return (uint32_t)RoundUp(width, 8) * RoundUp(height, 8) / 2;
    case GC_TEX_RGB5A3: return (uint32_t)RoundUp(width, 4) * RoundUp(height, 4) * 2;
    case GC_TEX_RGBA8: return (uint32_t)RoundUp(width, 4) * RoundUp(height, 4) * 4;
    default: return 0;
    }
}

// A CMPR block (8 bytes) as DXT1's: the two colours little-endian, and in each row's byte the four texels' two bits
// in the other order (the GX puts the first texel in the top bits, DXT1 in the bottom)
static void CmprBlockToDxt1(const uint8_t *cmpr, uint8_t *dxt) {
    dxt[0] = cmpr[1];
    dxt[1] = cmpr[0];
    dxt[2] = cmpr[3];
    dxt[3] = cmpr[2];
    for (int row = 0; row < 4; row++) {
        uint8_t b = cmpr[4 + row];
        dxt[4 + row] = (uint8_t)((b >> 6) | ((b >> 2) & 0x0c) | ((b << 2) & 0x30) | (b << 6));
    }
}

// A CMPR level as DXT1's rows of blocks (at least one block each way)
static void CmprLevelToDxt1(const uint8_t *texels, int width, int height, uint8_t *out) {
    int blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
    int tilesX = (width + 7) / 8;
    for (int by = 0; by < blocksY; by++) {
        for (int bx = 0; bx < blocksX; bx++) {
            // Tiles in rows, each tile's four blocks in rows
            int tile = (by / 2) * tilesX + bx / 2;
            int sub = (by & 1) * 2 + (bx & 1);
            CmprBlockToDxt1(texels + (tile * 4 + sub) * DXT1_BLOCK_BYTES, out + (by * blocksX + bx) * DXT1_BLOCK_BYTES);
        }
    }
}

// A DXT1 block's 16 texels, bytes B, G, R, A
static void DecodeDxt1Block(const uint8_t *dxt, uint8_t texels[16][4]) {
    uint16_t c0 = (uint16_t)(dxt[0] | dxt[1] << 8), c1 = (uint16_t)(dxt[2] | dxt[3] << 8);
    int colour[4][4];
    for (int k = 0; k < 2; k++) {
        uint16_t c = k == 0 ? c0 : c1;
        colour[k][2] = ((c >> 11) & 31) * 255 / 31;
        colour[k][1] = ((c >> 5) & 63) * 255 / 63;
        colour[k][0] = (c & 31) * 255 / 31;
        colour[k][3] = 255;
    }
    for (int i = 0; i < 3; i++) {
        if (c0 > c1) {
            colour[2][i] = (2 * colour[0][i] + colour[1][i]) / 3;
            colour[3][i] = (colour[0][i] + 2 * colour[1][i]) / 3;
        } else {
            colour[2][i] = (colour[0][i] + colour[1][i]) / 2;
            colour[3][i] = 0;
        }
    }
    colour[2][3] = 255;
    colour[3][3] = c0 > c1 ? 255 : 0;
    for (int i = 0; i < 16; i++) {
        int index = (dxt[4 + i / 4] >> ((i % 4) * 2)) & 3;
        for (int c = 0; c < 4; c++)
            texels[i][c] = (uint8_t)colour[index][c];
    }
}

static uint8_t Expand(int value, int bits) {
    return (uint8_t)(bits == 3 ? value * 255 / 7 : bits == 4 ? value * 17 : (value << 3) | (value >> 2));
}

void GcTexture_DecodeLevel(const uint8_t *texels, int format, int width, int height, uint8_t *bgra) {
    if (format == GC_TEX_CMPR) {
        int blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
        std::vector<uint8_t> dxt((size_t)blocksX * blocksY * DXT1_BLOCK_BYTES);
        CmprLevelToDxt1(texels, width, height, dxt.data());
        for (int by = 0; by < blocksY; by++) {
            for (int bx = 0; bx < blocksX; bx++) {
                uint8_t block[16][4];
                DecodeDxt1Block(&dxt[(by * blocksX + bx) * DXT1_BLOCK_BYTES], block);
                for (int i = 0; i < 16; i++) {
                    int x = bx * 4 + i % 4, y = by * 4 + i / 4;
                    if (x < width && y < height)
                        memcpy(bgra + ((size_t)y * width + x) * 4, block[i], 4);
                }
            }
        }
        return;
    }
    int tilesX = (width + 3) / 4;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int tile = (y / 4) * tilesX + x / 4, at = (y % 4) * 4 + x % 4;
            uint8_t *out = bgra + ((size_t)y * width + x) * 4;
            if (format == GC_TEX_RGBA8) {
                const uint8_t *t = texels + tile * 64;
                out[3] = t[at * 2];
                out[2] = t[at * 2 + 1];
                out[1] = t[32 + at * 2];
                out[0] = t[32 + at * 2 + 1];
            } else {
                uint16_t c = GcBe16(texels + (tile * 16 + at) * 2);
                if (c & 0x8000) {
                    out[3] = 0xff;
                    out[2] = Expand((c >> 10) & 31, 5);
                    out[1] = Expand((c >> 5) & 31, 5);
                    out[0] = Expand(c & 31, 5);
                } else {
                    out[3] = Expand((c >> 12) & 7, 3);
                    out[2] = Expand((c >> 8) & 15, 4);
                    out[1] = Expand((c >> 4) & 15, 4);
                    out[0] = Expand(c & 15, 4);
                }
            }
        }
    }
}

// Linear 32-bit texels in the Xbox's swizzled order: x and y bits interleaved from bit 0, x first, the larger
// dimension's surplus bits on top (the inverse of src/common/gfx/xboxTexture.h's Unswizzle)
static void Swizzle(const uint32_t *linear, uint32_t *swizzled, int width, int height) {
    uint32_t maskX = 0, maskY = 0, bit = 1, size = 1;
    for (;;) {
        bool any = false;
        if (size < (uint32_t)width)  { maskX |= bit; bit <<= 1; any = true; }
        if (size < (uint32_t)height) { maskY |= bit; bit <<= 1; any = true; }
        size <<= 1;
        if (!any)
            break;
    }
    uint32_t yOff = 0;
    for (int y = 0; y < height; y++) {
        uint32_t xOff = 0;
        for (int x = 0; x < width; x++) {
            swizzled[yOff | xOff] = linear[y * width + x];
            xOff = (xOff - maskX) & maskX;
        }
        yOff = (yOff - maskY) & maskY;
    }
}

static bool PowerOfTwoSide(int side) {
    return side >= 4 && side <= 512 && (side & (side - 1)) == 0;
}

static const GcTextureHeader *Header(const uint8_t *block, GcTextureHeader *out) {
    memcpy(out, block, sizeof(*out));
    out->magic = GcBe32(block + 0x00);
    out->pixelBytes = GcBe32(block + 0x18);
    out->width = GcBe16(block + 0x1c);
    out->height = GcBe16(block + 0x1e);
    // GC psiCreateMapTextures makes a texture 32x32 when either side is not a power of two from 4 to 512 (one on the
    // disc is 128x132), and reads that much of its texels
    if (!PowerOfTwoSide(out->width) || !PowerOfTwoSide(out->height))
        out->width = out->height = 32;
    out->format = GcBe32(block + 0x20);
    out->animSpeed = GcBe32(block + 0x2c);
    out->levels = GcBe32(block + 0x38);
    return out;
}

// The GameCube's bytes for one frame: its levels' tiles
static uint32_t GcFrameBytes(const GcTextureHeader *h) {
    uint32_t bytes = 0;
    for (uint32_t level = 0; level < h->levels; level++)
        bytes += GcTexture_LevelBytes((int)h->format, LevelSize(h->width, level), LevelSize(h->height, level));
    return bytes;
}

// The Xbox's bytes for one level: DXT1 blocks (at least 4x4 texels), or 32-bit texels
static uint32_t XboxLevelBytes(bool dxt, int width, int height) {
    if (dxt)
        return (uint32_t)(width < 4 ? 4 : width) * (height < 4 ? 4 : height) / 2;
    return (uint32_t)width * height * 4;
}

bool GcTexture_Valid(const uint8_t *block, uint32_t blockBytes, int frames) {
    if (blockBytes < sizeof(GcTextureHeader))
        return false;
    GcTextureHeader h;
    Header(block, &h);
    if ((h.magic >> 16) == GC_TEX_DUPLICATE)
        return true;
    if (h.magic != 0 || h.levels < 1 || h.levels > 12 || h.width == 0 || h.height == 0 || frames < 1)
        return false;
    if (h.format != GC_TEX_CMPR && h.format != GC_TEX_RGB5A3 && h.format != GC_TEX_RGBA8)
        return false;
    return GcFrameBytes(&h) * (uint32_t)frames <= h.pixelBytes && sizeof(h) + h.pixelBytes <= blockBytes;
}

bool GcTexture_ToKxt(const uint8_t *block, uint32_t blockBytes, int frames, int rate, std::vector<uint8_t> &kxt) {
    if (frames < 1)
        frames = 1;
    if (!GcTexture_Valid(block, blockBytes, frames))
        return false;
    GcTextureHeader h;
    Header(block, &h);

    TextureInfo info;
    memset(&info, 0, sizeof(info));
    info.numFrames = frames;
    info.animSpeed = rate;
    if ((h.magic >> 16) == GC_TEX_DUPLICATE) {
        // GC psiCreateMapTextures copies texture object [index]; the Xbox takes Tex[index + 1], its Tex[0] being the
        // default texture, which the GameCube's table has not
        info.magic = TEXINFO_DUPLICATE | (h.magic & 0xffff);
        kxt.assign(offsetof(TextureInfo, baseIdx) + frames * sizeof(int), 0);
        memcpy(kxt.data(), &info, offsetof(TextureInfo, baseIdx));
        return true;
    }

    bool dxt = h.format == GC_TEX_CMPR;
    uint32_t chain = 0;
    for (uint32_t level = 0; level < h.levels; level++)
        chain += XboxLevelBytes(dxt, LevelSize(h.width, level), LevelSize(h.height, level));
    info.magic = TEXINFO_MAGIC;
    info.unknown4 = 0xffffffff;
    info.frameBytes = chain + KXT_FRAME_SLACK;
    info.width = h.width;
    info.height = h.height;
    info.levels = h.levels;
    info.formatType = dxt ? 1 : 0;                  // 1: DXT1; 0 with 8 bits: A8R8G8B8, swizzled
    info.bitsPerPixel = dxt ? 0 : 8;
    memcpy((char *)&info + 0x34, h.name, sizeof(h.name));   // where the Xbox's headers keep the name

    size_t header = offsetof(TextureInfo, baseIdx) + frames * sizeof(int);
    kxt.assign(header + (size_t)frames * info.frameBytes, 0);
    memcpy(kxt.data(), &info, offsetof(TextureInfo, baseIdx));

    const uint8_t *texels = block + sizeof(GcTextureHeader);
    uint8_t *pixels = kxt.data() + header;
    std::vector<uint32_t> linear, swizzled;
    for (int f = 0; f < frames; f++) {
        uint8_t *out = pixels + (size_t)f * info.frameBytes;
        for (uint32_t level = 0; level < h.levels; level++) {
            int w = LevelSize(h.width, level), ht = LevelSize(h.height, level);
            if (dxt) {
                // A level under 4x4 texels is still one block on both
                CmprLevelToDxt1(texels, w < 4 ? 4 : w, ht < 4 ? 4 : ht, out);
            } else {
                linear.resize((size_t)w * ht);
                swizzled.resize((size_t)w * ht);
                GcTexture_DecodeLevel(texels, (int)h.format, w, ht, (uint8_t *)linear.data());
                Swizzle(linear.data(), swizzled.data(), w, ht);
                memcpy(out, swizzled.data(), swizzled.size() * 4);
            }
            texels += GcTexture_LevelBytes((int)h.format, w, ht);
            out += XboxLevelBytes(dxt, w, ht);
        }
    }
    return true;
}
