#ifndef COMMON_GFX_TEXTUREREPLACE_H_
#define COMMON_GFX_TEXTUREREPLACE_H_

// Texture replacement and texture dumping, keyed by a hash of each texture's original data. See
// textureReplace.cpp.

#include <d3d9.h>
#include <stdint.h>

#include "d3d9Backend.h"   // D3D9FontGlyph

typedef void (*TextureReplaceLogFn)(const char *fmt, ...);

// Reads settings.ini (TextureReplacement, DumpTextures) and looks at the textures folder. Call once, when the
// device is created.
void TextureReplace_Init(TextureReplaceLogFn log);

// Whether textures need hashing at all: something to replace, or dumping on. When false, nothing else here
// has anything to do and the backend skips the hash.
bool TextureReplace_Active(void);
bool TextureReplace_Dumping(void);

// The key: the Xbox format word and size, and the source bytes of the top level as the game supplies them.
uint64_t TextureReplace_Hash(uint32_t xboxFormat, uint32_t width, uint32_t height, const uint8_t *data, size_t bytes);

// textures/<hash>.dds as a managed texture with the file's own size, format and mip chain, or NULL if there is
// no such file (or it cannot be read, which is logged).
IDirect3DTexture9 *TextureReplace_Load(IDirect3DDevice9 *device, uint64_t hash);

// Writes textures_dump/<hash>.dds from level 0, given as B, G, R, A bytes - once per hash per session.
void TextureReplace_Dump(uint64_t hash, uint32_t width, uint32_t height, const uint8_t *bgra);

// A font's glyph boxes, written beside its dumped texture as textures_dump/<hash>.font.json.
void TextureReplace_DumpFont(uint64_t hash, uint32_t width, uint32_t height, const D3D9FontGlyph *glyphs, int count);

#endif // COMMON_GFX_TEXTUREREPLACE_H_
