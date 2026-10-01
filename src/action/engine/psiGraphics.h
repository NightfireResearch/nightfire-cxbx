#ifndef PSIGRAPHICS_H
#define PSIGRAPHICS_H

#include "../actionhelpers.h"
#include "celglist.h"

#pragma pack(push, 1)

// A texture's header, in the map file (a map_tag's texHeaderData points at each). psiCreateMapTextures registers
// its frames and keeps a pointer to it in Tex[]; Tex[0] points at the default entry maybePsiResetResources sets up.
// The frames' texture slots follow it, one per frame starting with baseIdx, then the frames' pixel data.
#define TEXINFO_MAGIC 0x0654584b          // a texture of its own
#define TEXINFO_DUPLICATE 0x44540000      // "DT" in the high half: the same frames as Tex[low half + 1]
typedef struct {
    uint magic;             // 0x00 - TEXINFO_MAGIC, or TEXINFO_DUPLICATE | index
    uint unknown4;
    uint frameBytes;        // 0x08 - pixel data per frame
    uint width;             // 0x0c
    uint height;            // 0x10
    uint bitsPerPixel;      // 0x14 - read for formatType 0 (and >5): 4, 8, or anything else
    uint levels;            // 0x18 - RegisterTexture's fourth argument
    int registerParam6;     // 0x1c - RegisterTexture's sixth
    uint formatType;        // 0x20 - 0 palettised/by depth, 1, 2..5 (see TextureInfo_Format)
    int numFrames;          // 0x24
    int animSpeed;          // 0x28 - in the file a rate; psiCreateMapTextures makes it 60 / rate (1 if out of range)
    char _pad_2[0x28];
    int baseIdx;            // 0x54 - the first frame's texture slot (0 if it has none)
} TextureInfo;
static_assert(sizeof(TextureInfo) == 0x58, "Bad size for TextureInfo");
static_assert(offsetof(TextureInfo, formatType) == 0x20, "Bad offset of TextureInfo.formatType");
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

// One entry per texture in a map_tag: its header (a TextureInfo, or NULL / -1 for none)
typedef struct {
    uchar maybeFlags;
    uchar unknown1;
    ushort widthMinusOne;       // 0x2
    ushort heightMinusOne;      // 0x4
    uchar animFrames;           // 0x6
    uchar someDivisor;          // 0x7
    TextureInfo *textureInfo;   // 0x8
} bmpHeaderEntry;
static_assert(sizeof(bmpHeaderEntry) == 0xc, "Bad size for bmpHeaderEntry");

// psiCreateMapTextures's result per texture: the Tex[] index it went in (0 if it was not usable)
typedef struct {
    int texIdx;
    short width;                // 0x4
    short height;               // 0x6
} texDataEntry;
static_assert(sizeof(texDataEntry) == 0x8, "Bad size for texDataEntry");

// A map file's texture table (Ghidra: map_tag)
struct map_tag {
    void *paletteHeaderData;
    uint numPaletteHeaderEntries;   // 0x04
    bmpHeaderEntry *texHeaderData;  // 0x08
    int numTexHeaderEntries;        // 0x0c
    texDataEntry *texData;          // 0x10
};
static_assert(sizeof(map_tag) == 0x14, "Bad size for map_tag");

// An entity's geometry, in the map file: a celglist_tag's geom_idx points at it until psiCreateEntityGfx
// registers its buffers, then holds its index in d3dGeometryObjs. The data follows the header: vertices, indices,
// primitiveCnt 12-byte primitive records (two texture references first), then the batch vertices.
#define MODELDATA_MAGIC 0x0645584b
typedef struct {
    uint magicTag;
    uint unknown4;
    int vtxCnt;             // 0x08
    int idxCnt;             // 0x0c
    int primitiveCnt;       // 0x10
    int dataSize14;         // 0x14 - with dataSize18, what psiCreateEntityGfx adds to EntityGfxStat_2adf20
    int dataSize18;         // 0x18
    int maybeSkinned;       // 0x1c
    uint unknown20;
    uint batchCnt;          // 0x24
    uint batchVtxCnt;       // 0x28
    char unknown2c[0x20];
    int vtxBuffers;         // 0x4c - d3dCreateVertexBuffers' handle
    int idxBuffer;          // 0x50
    uint primitives;        // 0x54 - address of the primitive records
    uint afterPrimitives;   // 0x58 - address just past them
    int batchVtxBuffers;    // 0x5c
    uchar data[4];          // 0x60 - the vertices start here
} ModelData;
static_assert(sizeof(ModelData) == 0x64, "Bad size for ModelData");
static_assert(offsetof(ModelData, vtxBuffers) == 0x4c, "Bad offset of ModelData.vtxBuffers");

#pragma pack(pop)

void psiPreDraw(void);
void psiDrawObjectMatrix(celglist_tag* celglist, _MATRIX* matrix);
void psiCreateMapTextures(map_tag *mapptr);
void psiCreateEntityGfx(celglist_tag *param_1,map_tag *param_2,uint param_3);
void psiAgeParticleOverlayRing(void);
void maybePsiResetResources(void);

#endif // PSIGRAPHICS_H