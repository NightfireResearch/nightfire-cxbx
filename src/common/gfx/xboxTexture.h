#ifndef COMMON_GFX_XBOXTEXTURE_H_
#define COMMON_GFX_XBOXTEXTURE_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The Xbox resource headers and pixel formats a graphics backend reads, and the conversions of their pixels: the
// SDL GPU backend's (sdlGpuBackend.cpp). They are those of d3d9Backend.cpp, which keeps copies of its own.
// ---------------------------------------------------------------------------------------------------------------

// Xbox pixel container header (the first 20 bytes of Gfx's 36-byte texture slots and of surface objects).
struct XboxPixelContainer { uint32_t Common, Data, Lock, Format, Size; };
struct XboxSurface { uint32_t Common, Data, Lock, Format, Size, Parent; };
#define XBOX_SURFACE_COMMON 0x01050001u // D3DCOMMON_TYPE_SURFACE, refcount 1
#define XBOX_TEXTURE_COMMON 0x00040001u // D3DCOMMON_TYPE_TEXTURE, refcount 1

// Xbox X_D3DFMT_* values the games produce, plus a few neighbours.
enum {
    XFMT_L8 = 0x00, XFMT_AL8 = 0x01, XFMT_A1R5G5B5 = 0x02, XFMT_X1R5G5B5 = 0x03, XFMT_A4R4G4B4 = 0x04, XFMT_R5G6B5 = 0x05,
    XFMT_A8R8G8B8 = 0x06, XFMT_X8R8G8B8 = 0x07, XFMT_P8 = 0x0b, XFMT_DXT1 = 0x0c, XFMT_DXT3 = 0x0e, XFMT_DXT5 = 0x0f,
    XFMT_LIN_A1R5G5B5 = 0x10, XFMT_LIN_R5G6B5 = 0x11, XFMT_LIN_A8R8G8B8 = 0x12, XFMT_LIN_L8 = 0x13,
    XFMT_A8 = 0x19, XFMT_A8L8 = 0x1a, XFMT_LIN_AL8 = 0x1b, XFMT_LIN_X1R5G5B5 = 0x1c, XFMT_LIN_A4R4G4B4 = 0x1d,
    XFMT_LIN_X8R8G8B8 = 0x1e, XFMT_LIN_A8 = 0x1f, XFMT_LIN_A8L8 = 0x20, XFMT_YUY2 = 0x24, XFMT_UYVY = 0x25,
    XFMT_LIN_D24S8 = 0x2e,
};

static inline bool XboxFormatIsLinear(uint32_t f) {
    return (f >= 0x10 && f <= 0x20) || f == XFMT_YUY2 || f == XFMT_UYVY || (f >= 0x2e && f <= 0x31) || (f >= 0x35 && f <= 0x37) || f == 0x3d;
}
static inline bool XboxFormatIsDxt(uint32_t f) { return f == XFMT_DXT1 || f == XFMT_DXT3 || f == XFMT_DXT5; }
static inline uint32_t XboxFormatBitsPerPixel(uint32_t f) {
    switch (f) {
        case XFMT_L8: case XFMT_AL8: case XFMT_P8: case XFMT_LIN_L8: case XFMT_A8: case XFMT_LIN_AL8: case XFMT_LIN_A8: return 8;
        case XFMT_DXT1: return 4;
        case XFMT_DXT3: case XFMT_DXT5: return 8;
        case XFMT_A8R8G8B8: case XFMT_X8R8G8B8: case XFMT_LIN_A8R8G8B8: case XFMT_LIN_X8R8G8B8: return 32;
        default: return 16;
    }
}
static inline uint32_t Log2Floor(uint32_t v) { uint32_t n = 0; while (v > 1) { v >>= 1; n++; } return n; }

// A texture header's size: linear formats keep it in the Size word, swizzled and compressed ones as log2 sizes in
// the Format word.
static inline void XboxTextureSize(const XboxPixelContainer *h, uint32_t *width, uint32_t *height) {
    if (h->Size != 0) {
        *width = (h->Size & 0xFFF) + 1;
        *height = ((h->Size >> 12) & 0xFFF) + 1;
    } else {
        *width = 1u << ((h->Format >> 20) & 0xF);
        *height = 1u << ((h->Format >> 24) & 0xF);
    }
}

// Morton-order unswizzle of one level, bytesPerPixel wide texels, into a tightly packed linear buffer.
static inline void Unswizzle(uint8_t *dst, const uint8_t *src, uint32_t width, uint32_t height, uint32_t bytesPerPixel) {
    uint32_t maskX = 0, maskY = 0, bit = 1, size = 1;
    for (;;) {
        bool any = false;
        if (size < width)  { maskX |= bit; bit <<= 1; any = true; }
        if (size < height) { maskY |= bit; bit <<= 1; any = true; }
        size <<= 1;
        if (!any) break;
    }
    uint32_t yOff = 0;
    for (uint32_t y = 0; y < height; y++) {
        uint32_t xOff = 0;
        uint8_t *row = dst + (size_t)y * width * bytesPerPixel;
        for (uint32_t x = 0; x < width; x++) {
            memcpy(row + (size_t)x * bytesPerPixel, src + (size_t)(yOff | xOff) * bytesPerPixel, bytesPerPixel);
            xOff = (xOff - maskX) & maskX;
        }
        yOff = (yOff - maskY) & maskY;
    }
}

static inline uint8_t ClampByte(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

// YUY2 (Y0 U Y1 V, BT.601) to A8R8G8B8, one row.
static inline void ConvertYuy2Row(uint32_t *dst, const uint8_t *src, uint32_t width) {
    for (uint32_t x = 0; x < width; x += 2) {
        int y0 = src[0], u = src[1] - 128, y1 = src[2], v = src[3] - 128;
        src += 4;
        for (int k = 0; k < 2; k++) {
            int c = ((k == 0 ? y0 : y1) - 16) * 298;
            int r = (c + 409 * v + 128) >> 8;
            int g = (c - 100 * u - 208 * v + 128) >> 8;
            int b = (c + 516 * u + 128) >> 8;
            *dst++ = 0xFF000000u | ((uint32_t)ClampByte(r) << 16) | ((uint32_t)ClampByte(g) << 8) | ClampByte(b);
        }
    }
}

// One 4x4 DXT block to 16 B, G, R, A texels: DXT1, DXT3's explicit alpha, DXT5's interpolated alpha.
static inline void DecodeDxtBlock(const uint8_t *block, uint32_t format, uint8_t bgra[16][4]) {
    bool dxt1 = format == XFMT_DXT1;
    const uint8_t *colour = dxt1 ? block : block + 8;
    uint16_t c0 = (uint16_t)(colour[0] | (colour[1] << 8)), c1 = (uint16_t)(colour[2] | (colour[3] << 8));
    uint8_t p[4][3];   // R, G, B
    p[0][0] = (uint8_t)(((c0 >> 11) << 3) | (c0 >> 13)); p[0][1] = (uint8_t)((((c0 >> 5) & 63) << 2) | ((c0 >> 9) & 3)); p[0][2] = (uint8_t)(((c0 & 31) << 3) | ((c0 >> 2) & 7));
    p[1][0] = (uint8_t)(((c1 >> 11) << 3) | (c1 >> 13)); p[1][1] = (uint8_t)((((c1 >> 5) & 63) << 2) | ((c1 >> 9) & 3)); p[1][2] = (uint8_t)(((c1 & 31) << 3) | ((c1 >> 2) & 7));
    bool fourColour = !dxt1 || c0 > c1;
    for (int k = 0; k < 3; k++) {
        p[2][k] = fourColour ? (uint8_t)((2 * p[0][k] + p[1][k]) / 3) : (uint8_t)((p[0][k] + p[1][k]) / 2);
        p[3][k] = fourColour ? (uint8_t)((p[0][k] + 2 * p[1][k]) / 3) : 0;
    }
    uint32_t bits = (uint32_t)colour[4] | ((uint32_t)colour[5] << 8) | ((uint32_t)colour[6] << 16) | ((uint32_t)colour[7] << 24);
    uint8_t alphaTable[8];
    uint64_t alphaBits = 0;
    if (format == XFMT_DXT5) {
        alphaTable[0] = block[0];
        alphaTable[1] = block[1];
        for (int k = 2; k < 8; k++) {
            if (block[0] > block[1]) alphaTable[k] = (uint8_t)(((8 - k) * block[0] + (k - 1) * block[1]) / 7);
            else if (k < 6) alphaTable[k] = (uint8_t)(((6 - k) * block[0] + (k - 1) * block[1]) / 5);
            else alphaTable[k] = k == 6 ? 0 : 255;
        }
        for (int k = 0; k < 6; k++) alphaBits |= (uint64_t)block[2 + k] << (8 * k);
    }
    for (int i = 0; i < 16; i++) {
        int sel = (bits >> (2 * i)) & 3;
        bgra[i][0] = p[sel][2];
        bgra[i][1] = p[sel][1];
        bgra[i][2] = p[sel][0];
        bgra[i][3] = (dxt1 && !fourColour && sel == 3) ? 0 : 255;
        if (format == XFMT_DXT3) bgra[i][3] = (uint8_t)(((block[i / 2] >> ((i & 1) * 4)) & 15) * 17);
        if (format == XFMT_DXT5) bgra[i][3] = alphaTable[(alphaBits >> (3 * i)) & 7];
    }
}

// A 24-bit BMP, top-down, from rows of B, G, R bytes rowBytes apart.
static inline void WriteBmp24(const char *path, uint32_t width, uint32_t height, const uint8_t *bgr, size_t rowBytes) {
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return;
    uint32_t stride = (width * 3 + 3) & ~3u;
    uint32_t imageBytes = stride * height;
    uint8_t header[54] = { 'B', 'M' };
    uint32_t fileSize = 54 + imageBytes, dataOffset = 54, infoSize = 40, planesBpp = 1 | (24u << 16);
    int32_t w = (int32_t)width, h = -(int32_t)height; // negative height: top-down rows
    memcpy(header + 2, &fileSize, 4); memcpy(header + 10, &dataOffset, 4); memcpy(header + 14, &infoSize, 4);
    memcpy(header + 18, &w, 4); memcpy(header + 22, &h, 4); memcpy(header + 26, &planesBpp, 4); memcpy(header + 34, &imageBytes, 4);
    fwrite(header, 1, 54, f);
    static const uint8_t pad[4] = { 0, 0, 0, 0 };
    for (uint32_t y = 0; y < height; y++) {
        fwrite(bgr + (size_t)y * rowBytes, 1, width * 3, f);
        fwrite(pad, 1, stride - width * 3, f);
    }
    fclose(f);
}

#endif // COMMON_GFX_XBOXTEXTURE_H_
