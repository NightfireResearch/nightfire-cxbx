#ifndef PS2_PS2TEXTURE_H_
#define PS2_PS2TEXTURE_H_

// The PS2's map textures (docs/ps2-data.md): per texture a block 0x16 of 4- or 8-bit texels, every frame of an
// animated one after the other, then a block 0x12 with its palette (16 or 256 RGBA colours, alpha 0x80 opaque; 256
// in the GS's CSM1 order). They become the Xbox's own texture header, a KXT of swizzled A8R8G8B8 frames, which
// psiCreateMapTextures registers as it does the Xbox's. No game state: the offline checker uses it too.

#include <stdint.h>

#include "../engine/psiGraphics.h"   // TextureInfo

// The palette's colours from its block size: 16 (64 bytes) or 256 (1 KB); 0 for any other size
int Ps2Texture_PaletteColours(uint32_t paletteBytes);

// The texel bytes a texture of this size takes: frames of width * height texels of 4 bits (16 colours) or 8
uint32_t Ps2Texture_TexelBytes(int width, int height, int frames, int colours);

// The size to decode a texture at: its header's, or half that each way until the texels fit their block (one texture
// of all 25,082 has a 64x64 header and 32x32's texels: 0700000d, pack 010000e7). False when nothing fits.
bool Ps2Texture_FitSize(int *width, int *height, int frames, int colours, uint32_t texelBytes);

// One frame to linear 32-bit texels, bytes B, G, R, A (the Xbox's A8R8G8B8)
void Ps2Texture_DecodeFrame(const uint8_t *texels, const uint8_t *palette, int colours, int width, int height,
                            uint8_t *bgra);

// Linear 32-bit texels in the Xbox's swizzled order: x and y bits interleaved from bit 0, x first, the larger
// dimension's surplus bits on top (the inverse of src/common/gfx/xboxTexture.h's Unswizzle)
void Ps2Texture_Swizzle(const uint32_t *linear, uint32_t *swizzled, int width, int height);

// The Xbox format a texture is made: DXT1 (opaque, or each texel opaque or clear), DXT5 (alpha between), as the
// Xbox's own map textures mostly are, and swizzled A8R8G8B8 under 4 texels across or down, or where DXT would put a
// texel far from its colour (frames of the texels given). A level's textures take 3-5 MB so where A8R8G8B8 took
// 12-35, which the heap and contiguous memory cannot spare (docs/ps2-data.md). Linear A8R8G8B8 is for the textures
// the game reads and writes as pixels (Woman.cpp's), as the Xbox has them.
enum Ps2TextureFormat { PS2TEX_A8R8G8B8, PS2TEX_DXT1, PS2TEX_DXT5, PS2TEX_LIN_A8R8G8B8 };
Ps2TextureFormat Ps2Texture_ChooseFormat(const uint8_t *texels, const uint8_t *palette, int colours, int width,
                                         int height, int frames);

// The bytes Ps2Texture_BuildKxt writes for a texture of this size: the header, a texture slot per frame, and each
// frame's pixels with 128 bytes to spare (RegisterTexture moves a frame up to a 128-byte boundary in place)
uint32_t Ps2Texture_KxtBytes(int width, int height, int frames, Ps2TextureFormat format);

// The Xbox texture header (KXT, one level) and its frames, at out (Ps2Texture_KxtBytes bytes). rate is the 0x10
// header's byte 7, which psiCreateMapTextures turns into ticks per frame as it does the KXT's.
void Ps2Texture_BuildKxt(TextureInfo *out, uint32_t hashcode, int width, int height, int frames, int rate,
                         const uint8_t *texels, const uint8_t *palette, int colours, Ps2TextureFormat format);

#endif // PS2_PS2TEXTURE_H_
