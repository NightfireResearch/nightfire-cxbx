#include "Ps2Convert.h"
#include "Ps2Mesh.h"
#include "Ps2Texture.h"
#include "Ps2XboxStandIn.h"
#include "Ps2Sound.h"
#include "../../common/sound/xadpcm.h"
#include "../memory.h"

#include <stdio.h>
#include <stdlib.h>
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
    Ps2TextureFormat format = Ps2Texture_ChooseFormat(palette, colours, width, height);
    // The pause screen's film is drawn into one texture from another, pixel by pixel (Woman.cpp): those stay linear
    // A8R8G8B8, as the Xbox's are
    if (hashcode == HT_Texture_Fire2 || hashcode == HT_Texture_WomanBack)
        format = PS2TEX_LIN_A8R8G8B8;
    TextureInfo *tex = (TextureInfo *)Mem_Malloc(Ps2Texture_KxtBytes(width, height, frames, format),
                                                 MALLOC_PS2_TEXTURE, 0);
    Ps2Texture_BuildKxt(tex, hashcode, width, height, frames, header->someDivisor, texels, palette, colours, format);
    header->textureInfo = tex;
}

// The last mesh was a skinned one left to the Xbox's: the entity params block after it stands the Xbox's in. Interim,
// for comparing: with NIGHTFIRE_PS2_XBOX_SKINS set, or when one does not convert.
static bool SkinnedMeshPending;

ModelData *Ps2_ConvertModel(const uint8_t *block, uint32_t blockBytes) {
    std::vector<uint8_t> kxe;
    Ps2MeshStats stats;
    SkinnedMeshPending = false;
    bool converted = Ps2Mesh_ConvertToKxe(block, blockBytes, kxe, &stats);
    if (stats.skinned && (!converted || getenv("NIGHTFIRE_PS2_XBOX_SKINS") != NULL)) {
        SkinnedMeshPending = true;
        return NULL;
    }
    if (!converted)
        return NULL;
    ModelData *model = (ModelData *)Mem_Malloc(kxe.size(), MALLOC_PS2_OBJECT, 0);
    memcpy(model, kxe.data(), kxe.size());
    return model;
}

void Ps2_ConvertSoundBank(SampleHeaderData *samples, int count) {
    if (count <= 0)
        return;
    size_t bytes = 0;
    for (int i = 0; i < count; i++)
        bytes += Ps2Spu_XAdpcmBytes(samples[i].Size / SPU_BLOCK_BYTES * SPU_BLOCK_SAMPLES);
    void *spu = samples[0].Address;     // the .SBF file, the samples one after another
    uint8_t *out = (uint8_t *)Mem_Malloc(bytes, MALLOC_XBOX, 0);
    std::vector<int16_t> pcm;
    for (int i = 0; i < count; i++) {
        SampleHeaderData *sample = &samples[i];
        pcm.resize((size_t)sample->Size / SPU_BLOCK_BYTES * SPU_BLOCK_SAMPLES);
        long loopStart;
        size_t n = Ps2Spu_Decode((const uint8_t *)(void *)sample->Address, sample->Size, pcm.data(), &loopStart);
        int written = (int)Ps2Spu_EncodeXAdpcm(pcm.data(), n, out);
        sample->Address = out;
        sample->Size = written;
        sample->RealSize = written;
        sample->Frequency = (int)Ps2Spu_PitchToHz(sample->Frequency);
        // The loop's start (the block the SPU flags) as the Xbox's: the byte offset of its block
        sample->LoopOffset = loopStart > 0 ? (int)(loopStart / XADPCM_SAMPLES_PER_BLOCK * XADPCM_BLOCK_BYTES_PER_CHANNEL)
                                           : 0;
        out += written;
    }
    Mem_Free(&spu);
}

void Ps2_EntityParams(celglist_tag *celglist, uint32_t hashcode) {
    if (!SkinnedMeshPending)
        return;
    SkinnedMeshPending = false;
    celglist->geom_idx = (int)PtrTo32(Ps2_XboxStandInModel(hashcode));
}
