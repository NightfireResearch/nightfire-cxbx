#include "Ps2Texture.h"
#include "../engine/psiGraphics.h"

#include <string.h>
#include <vector>

#define PS2_ALPHA_OPAQUE 0x80               // the GS's alpha 1.0
#define KXT_FRAME_SLACK 128                 // RegisterTexture's alignment

int Ps2Texture_PaletteColours(uint32_t paletteBytes) {
    if (paletteBytes == 16 * 4)
        return 16;
    if (paletteBytes == 256 * 4)
        return 256;
    return 0;
}

uint32_t Ps2Texture_TexelBytes(int width, int height, int frames, int colours) {
    uint32_t texels = (uint32_t)width * height * frames;
    return colours == 16 ? texels / 2 : texels;
}

bool Ps2Texture_FitSize(int *width, int *height, int frames, int colours, uint32_t texelBytes) {
    while (Ps2Texture_TexelBytes(*width, *height, frames, colours) > texelBytes) {
        if (*width < 2 || *height < 2)
            return false;
        *width /= 2;
        *height /= 2;
    }
    return true;
}

// A 256-colour palette is stored as the GS keeps it (CSM1): in each run of 32 colours the second and third 8 are
// swapped, which is bits 3 and 4 of the index exchanged
static int PaletteSlot(int index, int colours) {
    if (colours != 256)
        return index;
    return (index & ~0x18) | ((index & 0x08) << 1) | ((index & 0x10) >> 1);
}

static uint8_t Alpha8(uint8_t ps2Alpha) {
    return ps2Alpha >= PS2_ALPHA_OPAQUE ? 0xff : (uint8_t)(ps2Alpha * 2);
}

void Ps2Texture_DecodeFrame(const uint8_t *texels, const uint8_t *palette, int colours, int width, int height,
                            uint8_t *bgra) {
    for (int i = 0; i < width * height; i++) {
        // Two 4-bit texels a byte, the first in the low half
        int index = colours == 16 ? (texels[i / 2] >> ((i & 1) * 4)) & 0xf : texels[i];
        const uint8_t *rgba = palette + PaletteSlot(index, colours) * 4;
        bgra[i * 4 + 0] = rgba[2];
        bgra[i * 4 + 1] = rgba[1];
        bgra[i * 4 + 2] = rgba[0];
        bgra[i * 4 + 3] = Alpha8(rgba[3]);
    }
}

void Ps2Texture_Swizzle(const uint32_t *linear, uint32_t *swizzled, int width, int height) {
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

// ---- DXT: the Xbox's own compressed formats, a quarter or an eighth of A8R8G8B8 (most of the Xbox's map textures are
// DXT1 or DXT5). Blocks of 4x4 texels in rows, not swizzled.

#define DXT1_BLOCK_BYTES 8
#define DXT5_BLOCK_BYTES 16

static uint16_t To565(int r, int g, int b) {
    return (uint16_t)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255));
}

static void From565(uint16_t c, int *r, int *g, int *b) {
    *r = ((c >> 11) & 31) * 255 / 31;
    *g = ((c >> 5) & 63) * 255 / 63;
    *b = (c & 31) * 255 / 31;
}

// A block's colours with two end colours (565): each texel's nearest of the four (or of three and transparent, with
// transparent set: the texels whose alpha is under half). The squared error of the block, and of its worst texel.
struct ColourBlock {
    uint16_t c0, c1;
    uint32_t indices;
    int error;
    int worst;
};

static ColourBlock FitColourBlock(const uint8_t *texels, bool transparent, uint16_t c0, uint16_t c1) {
    // Four colours want c0 > c1, three and transparent c0 <= c1
    if (transparent ? c0 > c1 : c0 < c1) {
        uint16_t t = c0;
        c0 = c1;
        c1 = t;
    }
    int palette[4][3];
    From565(c0, &palette[0][0], &palette[0][1], &palette[0][2]);
    From565(c1, &palette[1][0], &palette[1][1], &palette[1][2]);
    int colours = c0 > c1 ? 4 : 3;
    for (int c = 0; c < 3; c++) {
        if (colours == 4) {
            palette[2][c] = (2 * palette[0][c] + palette[1][c]) / 3;
            palette[3][c] = (palette[0][c] + 2 * palette[1][c]) / 3;
        } else {
            palette[2][c] = (palette[0][c] + palette[1][c]) / 2;
        }
    }
    ColourBlock block = { c0, c1, 0, 0, 0 };
    for (int i = 0; i < 16; i++) {
        const uint8_t *t = texels + i * 4;
        uint32_t index = 0;
        if (transparent && t[3] < 128) {
            index = 3;
        } else {
            int best = 1 << 30;
            for (int k = 0; k < colours; k++) {
                int dr = t[2] - palette[k][0], dg = t[1] - palette[k][1], db = t[0] - palette[k][2];
                int d = dr * dr + dg * dg + db * db;
                if (d < best) {
                    best = d;
                    index = (uint32_t)k;
                }
            }
            block.error += best;
            block.worst = best > block.worst ? best : block.worst;
        }
        block.indices |= index << (i * 2);
    }
    return block;
}

// A block's colours (bytes B, G, R, A, 16 texels in rows): the best of the box the texels lie in (pulled in by a
// sixteenth) and every pair of the block's own colours as the end colours - a palette texture's block has few, and
// the box of, say, red, white and blue is black to white. The worst texel's squared error.
static int EncodeColourBlock(const uint8_t *texels, bool transparent, uint8_t *out) {
    int lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 };
    uint16_t own[16];
    int ownCount = 0;
    for (int i = 0; i < 16; i++) {
        const uint8_t *t = texels + i * 4;
        if (transparent && t[3] < 128)
            continue;
        for (int c = 0; c < 3; c++) {       // r, g, b
            lo[c] = t[2 - c] < lo[c] ? t[2 - c] : lo[c];
            hi[c] = t[2 - c] > hi[c] ? t[2 - c] : hi[c];
        }
        uint16_t c565 = To565(t[2], t[1], t[0]);
        int k = 0;
        while (k < ownCount && own[k] != c565)
            k++;
        if (k == ownCount)
            own[ownCount++] = c565;
    }
    if (ownCount == 0)
        lo[0] = lo[1] = lo[2] = hi[0] = hi[1] = hi[2] = 0;
    for (int c = 0; c < 3; c++) {
        int inset = (hi[c] - lo[c]) / 16;
        lo[c] += inset;
        hi[c] -= inset;
    }
    ColourBlock best = FitColourBlock(texels, transparent, To565(hi[0], hi[1], hi[2]), To565(lo[0], lo[1], lo[2]));
    for (int a = 0; a < ownCount && best.error != 0; a++) {
        for (int b = a; b < ownCount; b++) {
            ColourBlock block = FitColourBlock(texels, transparent, own[a], own[b]);
            if (block.error < best.error)
                best = block;
        }
    }
    memcpy(out, &best.c0, 2);
    memcpy(out + 2, &best.c1, 2);
    memcpy(out + 4, &best.indices, 4);
    return best.worst;
}

// A block's alpha for DXT5: its highest and lowest, and each texel's nearest of the eight between
static void EncodeAlphaBlock(const uint8_t *texels, uint8_t *out) {
    int a0 = 0, a1 = 255;
    for (int i = 0; i < 16; i++) {
        int a = texels[i * 4 + 3];
        a0 = a > a0 ? a : a0;
        a1 = a < a1 ? a : a1;
    }
    out[0] = (uint8_t)a0;
    out[1] = (uint8_t)a1;
    int levels[8] = { a0, a1 };
    for (int k = 1; k < 7; k++)
        levels[k + 1] = a0 > a1 ? ((7 - k) * a0 + k * a1) / 7 : a0;
    uint64_t indices = 0;
    for (int i = 0; i < 16; i++) {
        int a = texels[i * 4 + 3], best = 1 << 30;
        uint64_t index = 0;
        for (int k = 0; k < 8; k++) {
            int d = (a - levels[k]) * (a - levels[k]);
            if (d < best) {
                best = d;
                index = (uint64_t)k;
            }
        }
        indices |= index << (i * 3);
    }
    for (int b = 0; b < 6; b++)
        out[2 + b] = (uint8_t)(indices >> (b * 8));
}

// Encodes a frame; the worst texel's squared colour error
static int EncodeDxt(const uint8_t *bgra, int width, int height, Ps2TextureFormat format, uint8_t *out) {
    int worst = 0;
    for (int by = 0; by < height; by += 4) {
        for (int bx = 0; bx < width; bx += 4) {
            uint8_t block[16 * 4];
            for (int y = 0; y < 4; y++)
                memcpy(block + y * 16, bgra + ((size_t)(by + y) * width + bx) * 4, 16);
            int error;
            if (format == PS2TEX_DXT5) {
                EncodeAlphaBlock(block, out);
                error = EncodeColourBlock(block, false, out + 8);
                out += DXT5_BLOCK_BYTES;
            } else {
                bool transparent = false;
                for (int i = 0; i < 16; i++)
                    transparent |= block[i * 4 + 3] < 128;
                error = EncodeColourBlock(block, transparent, out);
                out += DXT1_BLOCK_BYTES;
            }
            worst = error > worst ? error : worst;
        }
    }
    return worst;
}

static uint32_t FramePixelBytes(int width, int height, Ps2TextureFormat format) {
    switch (format) {
    case PS2TEX_DXT1: return (uint32_t)width * height / 16 * DXT1_BLOCK_BYTES;
    case PS2TEX_DXT5: return (uint32_t)width * height / 16 * DXT5_BLOCK_BYTES;
    default: return (uint32_t)width * height * 4;
    }
}

// A texel DXT cannot come within this of (RGB, squared) makes the texture A8R8G8B8: blocks of three or more colours far
// apart, as in a flag
#define DXT_WORST_TEXEL_ERROR (160 * 160)

Ps2TextureFormat Ps2Texture_ChooseFormat(const uint8_t *texels, const uint8_t *palette, int colours, int width,
                                         int height, int frames) {
    if (width < 4 || height < 4)
        return PS2TEX_A8R8G8B8;
    Ps2TextureFormat format = PS2TEX_DXT1;
    for (int i = 0; i < colours; i++) {
        uint8_t a = Alpha8(palette[i * 4 + 3]);
        if (a != 0 && a != 0xff)
            format = PS2TEX_DXT5;
    }
    std::vector<uint32_t> linear((size_t)width * height);
    std::vector<uint8_t> dxt(FramePixelBytes(width, height, format));
    uint32_t frameTexels = Ps2Texture_TexelBytes(width, height, 1, colours);
    for (int f = 0; f < frames; f++) {
        Ps2Texture_DecodeFrame(texels + f * frameTexels, palette, colours, width, height, (uint8_t *)linear.data());
        if (EncodeDxt((const uint8_t *)linear.data(), width, height, format, dxt.data()) > DXT_WORST_TEXEL_ERROR)
            return PS2TEX_A8R8G8B8;
    }
    return format;
}


uint32_t Ps2Texture_KxtBytes(int width, int height, int frames, Ps2TextureFormat format) {
    return offsetof(TextureInfo, baseIdx) + frames * 4 +
           frames * (FramePixelBytes(width, height, format) + KXT_FRAME_SLACK);
}

void Ps2Texture_BuildKxt(TextureInfo *out, uint32_t hashcode, int width, int height, int frames, int rate,
                         const uint8_t *texels, const uint8_t *palette, int colours, Ps2TextureFormat format) {
    memset(out, 0, offsetof(TextureInfo, baseIdx));
    out->magic = TEXINFO_MAGIC;
    out->unknown4 = hashcode;
    out->frameBytes = FramePixelBytes(width, height, format) + KXT_FRAME_SLACK;
    out->width = width;
    out->height = height;
    out->levels = 1;
    out->registerParam6 = format == PS2TEX_LIN_A8R8G8B8 ? 1 : 0;   // linear, or swizzled (DXT is in rows either way)
    switch (format) {
    case PS2TEX_DXT1: out->formatType = 1; out->bitsPerPixel = 1; break;
    case PS2TEX_DXT5: out->formatType = 5; out->bitsPerPixel = 8; break;
    default: out->formatType = 0; out->bitsPerPixel = 8; break;     // with formatType 0: A8R8G8B8
    }
    out->numFrames = frames;
    out->animSpeed = rate;

    int *slots = &out->baseIdx;
    uint8_t *pixels = (uint8_t *)(slots + frames);
    std::vector<uint32_t> linear((size_t)width * height);
    uint32_t frameTexels = Ps2Texture_TexelBytes(width, height, 1, colours);
    uint32_t frameBytes = FramePixelBytes(width, height, format);
    for (int f = 0; f < frames; f++) {
        slots[f] = 0;
        Ps2Texture_DecodeFrame(texels + f * frameTexels, palette, colours, width, height, (uint8_t *)linear.data());
        if (format == PS2TEX_A8R8G8B8)
            Ps2Texture_Swizzle(linear.data(), (uint32_t *)pixels, width, height);
        else if (format == PS2TEX_LIN_A8R8G8B8)
            memcpy(pixels, linear.data(), frameBytes);
        else
            EncodeDxt((const uint8_t *)linear.data(), width, height, format, pixels);
        memset(pixels + frameBytes, 0, KXT_FRAME_SLACK);
        pixels += out->frameBytes;
    }
}
