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

static uint32_t FramePixelBytes(int width, int height) {
    return (uint32_t)width * height * 4;
}

uint32_t Ps2Texture_KxtBytes(int width, int height, int frames) {
    return offsetof(TextureInfo, baseIdx) + frames * 4 + frames * (FramePixelBytes(width, height) + KXT_FRAME_SLACK);
}

void Ps2Texture_BuildKxt(TextureInfo *out, uint32_t hashcode, int width, int height, int frames, int rate,
                         const uint8_t *texels, const uint8_t *palette, int colours) {
    memset(out, 0, offsetof(TextureInfo, baseIdx));
    out->magic = TEXINFO_MAGIC;
    out->unknown4 = hashcode;
    out->frameBytes = FramePixelBytes(width, height) + KXT_FRAME_SLACK;
    out->width = width;
    out->height = height;
    out->bitsPerPixel = 8;              // with formatType 0: A8R8G8B8
    out->levels = 1;
    out->registerParam6 = 0;            // swizzled
    out->formatType = 0;
    out->numFrames = frames;
    out->animSpeed = rate;

    int *slots = &out->baseIdx;
    uint8_t *pixels = (uint8_t *)(slots + frames);
    std::vector<uint32_t> linear((size_t)width * height);
    uint32_t frameTexels = Ps2Texture_TexelBytes(width, height, 1, colours);
    for (int f = 0; f < frames; f++) {
        slots[f] = 0;
        Ps2Texture_DecodeFrame(texels + f * frameTexels, palette, colours, width, height, (uint8_t *)linear.data());
        Ps2Texture_Swizzle(linear.data(), (uint32_t *)pixels, width, height);
        memset(pixels + FramePixelBytes(width, height), 0, KXT_FRAME_SLACK);
        pixels += out->frameBytes;
    }
}
