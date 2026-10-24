#include "Ps2Convert.h"
#include "Ps2Mesh.h"
#include "Ps2Texture.h"
#include "../memory.h"

#include <stdio.h>
#include <string.h>

// The game's own allocation types for these (MemInfo's names)
#define MALLOC_PS2_OBJECT   ((MallocFlags)0x2804)
#define MALLOC_PS2_TEXTURE  ((MallocFlags)0x2b04)

void Ps2_ConvertMapTexture(bmpHeaderEntry *header, uint32_t hashcode, const uint8_t *palette, uint32_t paletteBytes) {
    const uint8_t *texels = (const uint8_t *)(TextureInfo *)header->textureInfo;
    int colours = Ps2Texture_PaletteColours(paletteBytes);
    if (texels == NULL || colours == 0) {
        printf("[ps2] texture 0x%08x: a palette of %u bytes, not converted\n", hashcode, paletteBytes);
        header->textureInfo = NULL;
        return;
    }
    int width = header->widthMinusOne + 1;
    int height = header->heightMinusOne + 1;
    int frames = header->animFrames != 0 ? header->animFrames : 1;
    uint32_t texelBytes = (*(const uint32_t *)(texels - 4) & 0xffffff) - 4;    // from block 0x16's header word
    if (!Ps2Texture_FitSize(&width, &height, frames, colours, texelBytes)) {
        printf("[ps2] texture 0x%08x: %u bytes of texels, not converted\n", hashcode, texelBytes);
        header->textureInfo = NULL;
        return;
    }
    TextureInfo *tex = (TextureInfo *)Mem_Malloc(Ps2Texture_KxtBytes(width, height, frames), MALLOC_PS2_TEXTURE, 0);
    Ps2Texture_BuildKxt(tex, hashcode, width, height, frames, header->someDivisor, texels, palette, colours);
    header->textureInfo = tex;
}

ModelData *Ps2_ConvertModel(const uint8_t *block, uint32_t blockBytes) {
    std::vector<uint8_t> kxe;
    Ps2MeshStats stats;
    if (!Ps2Mesh_ConvertToKxe(block, blockBytes, kxe, &stats))
        return NULL;
    ModelData *model = (ModelData *)Mem_Malloc(kxe.size(), MALLOC_PS2_OBJECT, 0);
    memcpy(model, kxe.data(), kxe.size());
    return model;
}
