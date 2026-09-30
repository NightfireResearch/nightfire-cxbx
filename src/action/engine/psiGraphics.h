#ifndef PSIGRAPHICS_H
#define PSIGRAPHICS_H

#include "../actionhelpers.h"
#include "celglist.h"

#pragma pack(push, 1)

// Moved from Woman.cpp: a texture's header as the psi layer keeps it. Tex[0] points at the default entry
// maybePsiResetResources sets up at 0x002ae4f8.
typedef struct {
    char _pad_1[0x24];
    int numFrames;
    int animSpeed;
    char _pad_2[0x28];
    int baseIdx;
} TextureInfo;
static_assert(sizeof(TextureInfo) == 0x58, "Bad size for TextureInfo");
static_assert(offsetof(TextureInfo, numFrames) == 0x24, "Bad offset of TextureInfo.numFrames");
static_assert(offsetof(TextureInfo, animSpeed) == 0x28, "Bad offset of TextureInfo.animSpeed");
static_assert(offsetof(TextureInfo, baseIdx) == 0x54, "Bad offset of TextureInfo.baseIdx");

#define Tex (*(TextureInfo*(*)[2048])0x002abe80)

// A particle batch's overlay buffer, held until the GPU has finished with it (psiDrawParticleList fills one,
// psiAgeParticleOverlayRing frees it when framesToLive runs out). Cleared, not freed, by maybePsiResetResources.
typedef struct {
    int overlaySlot;      // from d3dRegisterOverlayBuffer; 0 = empty entry
    void *vertexData;     // Mem_Malloc'd vertices
    int framesToLive;
} ParticleOverlayBuffer;
static_assert(sizeof(ParticleOverlayBuffer) == 0xc, "Bad size for ParticleOverlayBuffer");

#define PARTICLE_OVERLAY_RING_SIZE 64
#define ParticleOverlayRing (*(ParticleOverlayBuffer(*)[PARTICLE_OVERLAY_RING_SIZE])0x002adf88)

#pragma pack(pop)

void psiDrawObjectMatrix(celglist_tag* celglist, _MATRIX* matrix);
void psiCreateMapTextures(map_tag *mapptr);
void psiCreateEntityGfx(celglist_tag *param_1,map_tag *param_2,uint param_3);
void psiAgeParticleOverlayRing(void);
void maybePsiResetResources(void);

#endif // PSIGRAPHICS_H