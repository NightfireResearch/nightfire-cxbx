#ifndef GRAPHICSSYSTEM_H_
#define GRAPHICSSYSTEM_H_

#include "d3dhelpers.h"

#include <stddef.h>
#include <stdint.h>

// Gfx, the graphics layer's one global struct. xboxInitGraphics clears it whole at boot (REP STOSD of 0xe739
// dwords from 0x002c5750), which fixes its size at 0x39ce4 bytes; Ghidra's GraphicsSystem is one byte longer.
// Everything from +0x20 to the end is accounted for below except the block at +0x1894, which nothing
// reimplemented touches yet. Fields are named for what the reimplemented code in d3dSeam.cpp does with them;
// Ghidra's names, where it has them, are in the comments.
//
// Until every function that touches it is ours, Gfx is the game's own copy (the #define at the bottom). After
// that it can become a definition here.

// One of the 2048 texture slots RegisterTexture hands out. The first 20 bytes are opaque Xbox D3D8
// texture-header internals (written by XGSetTextureHeader) that nothing here reads directly - only the
// trailing bookkeeping fields Eurocom's own code touches are named.
struct D3DTextureSlotRaw {
    uint8_t  opaqueHeader[20];
    void    *baseTexture;   // +0x14 - set to point at this same slot once registered (Xbox convention)
    uint16_t width;         // +0x18
    uint16_t height;        // +0x1a
    uint16_t refCount;      // +0x1c - written 0 by RegisterTexture; FUN_000e4f00 (the slot-release function) tests it
                             // == 0 before freeing, confirming it really is a refcount despite RegisterTexture
                             // never incrementing it - nothing traced so far increments it either
    uint16_t nonSwizzled;   // +0x1e - the "param_6 != 0" flag from the caller
    uint32_t mipChainBytes; // +0x20 - total byte size of every mip level
};
static_assert(sizeof(D3DTextureSlotRaw) == 36, "Bad size for D3DTextureSlotRaw");

// One of the 2048 vertex-buffer slots d3dCreateVertexBuffers hands out, and that d3dSetStreamSources and
// d3dBindBuffers read. See d3dCreateVertexBuffers for how its two paths fill it differently.
struct D3DVertexBufferSlotRaw {
    uint32_t header;     // +0x00 - Common; 1 once allocated. header&0x70000 never equals 0x20000 for this value,
                          // so D3DResource_Register never takes its pointer-masking branch (unlike
                          // RegisterTexture's texture headers) - no "force the pointer back" workaround needed.
    void    *dataPtr;    // +0x04 - Data; set BY D3DResource_Register itself (we zero it first, it adds data to that)
    uint32_t reserved08; // +0x08 - always 0; nothing else reads it as far as we've traced
    void    *selfPtr;    // +0x0c - Ghidra's Gfx.d3dstreamDataPtr; 0 marks the slot free
    uint32_t field10;     // +0x10 - single-path: the ORIGINAL, unaligned data pointer; multi-path: vtxCnt
    uint32_t byteSize;    // +0x14 - total byte size (both paths, same role, different formula)
    uint8_t  field18;      // +0x18 - single-path: (nonSwizzled != 0); multi-path: always 0. Ghidra's
                          // Gfx.d3dstreamStrideRelated
    uint8_t  pad19, pad1a, pad1b;
    uint32_t field1c;     // +0x1c - single-path: always 0; multi-path: streamCount (same value in every slot of the run)
    void    *alignedData; // +0x20 - single-path ONLY; never written by the multi-path
};
static_assert(sizeof(D3DVertexBufferSlotRaw) == 36, "Bad size for D3DVertexBufferSlotRaw");

// One of the 2048 index-buffer slots d3dCreateIndexBuffer hands out.
struct D3DIndexBufferSlotRaw {
    uint32_t header;     // +0x00 - always 0x10001 once allocated; 0 marks the slot free
    void    *dataPtr;    // +0x04
    uint32_t reserved08; // +0x08 - always 0; nothing else reads it as far as we've traced
    void    *selfPtr;    // +0x0c - points back to this same slot's own header (Xbox convention, same as
                          // RegisterTexture's baseTexture); Ghidra's Gfx.d3dIndexBuffers
    uint32_t indexCount; // +0x10
    uint32_t byteSize;   // +0x14 - indexCount * 2 (16-bit indices)
    void    *dataPtr2;   // +0x18 - same value as dataPtr
};
static_assert(sizeof(D3DIndexBufferSlotRaw) == 28, "Bad size for D3DIndexBufferSlotRaw");

// One of the 256 small vertex buffers d3dRegisterOverlayBuffer hands out for d3dDrawOverlayQuad. The "is this
// slot free" test is at +0xc, not +0x00: +0xc holds a copy of the caller's own data pointer (written directly
// from the value passed to D3DResource_Register as its data argument, not computed).
struct D3DOverlayQuadSlotRaw {
    uint32_t header;     // +0x00 - 1 once allocated. header&0x70000 never equals 0x20000 for this value, so
                          // D3DResource_Register never takes its pointer-masking branch here either.
    void    *dataPtr;    // +0x04 - Data; set BY D3DResource_Register itself (we zero it first, it adds data to that)
    uint32_t reserved08; // +0x08 - always 0; nothing else reads it as far as we've traced
    void    *dataPtrCopy; // +0x0c - a copy of the caller's own data pointer; doubles as the "is this slot free" test
    uint32_t vertexCount; // +0x10 - consumed by d3dDrawOverlayQuad's own D3DDevice_DrawVertices call
};
static_assert(sizeof(D3DOverlayQuadSlotRaw) == 20, "Bad size for D3DOverlayQuadSlotRaw");

#define GFX_TEXTURE_SLOTS       2048
#define GFX_VERTEX_BUFFER_SLOTS 2048
#define GFX_INDEX_BUFFER_SLOTS  2048
#define GFX_OVERLAY_QUAD_SLOTS  256
#define GFX_IMMEDIATE_ITEMS     64

// Four directional lights as the vertex shader gets them: constants 0x69..0x71, uploaded as one block.
struct GfxLightConstants {
    float position[4][4]; // view-space direction (xyz), w = 1 (d3dSetup; Ghidra saw these four w's as a float[16])
    float colour[4][4];   // r, g, b (each /256), unused w
    float invRange[4];    // 1/range, 1 when the range is not positive
};
static_assert(sizeof(GfxLightConstants) == 36 * 4, "Bad size for GfxLightConstants");

struct GraphicsSystem {
    uint32_t d3dLastError;             // +0x0
    uint8_t  swapPending;              // +0x4
    uint8_t  pad5[3];
    uint32_t frameCounter;             // +0x8
    uint32_t state0x40358LastValue;    // +0xc - the last value d3dSetZBias/d3dSetup sent through D3D8's method 0x40358
    uint8_t  isPalI;                   // +0x10 - misnamed: a copy of isNotPalI (xboxInitGraphics)
    uint8_t  isNotPalI;                // +0x11 - AV region != 3 (PAL-I)
    uint8_t  isNtscM;                  // +0x12 - AV region == 1 (NTSC-M); Ghidra's IsSomeGfxRegion
    uint8_t  isWidescreen;             // +0x13 - video mode bit 0
    uint8_t  videoModeBit3;            // +0x14 - video mode bit 3, forced to 0 for PAL-I
    uint8_t  pad15[3];
    uint32_t pushBuffersEnabled;       // +0x18
    uint32_t d3dDevice;                // +0x1c - the D3D8 device; 0 until xboxInitGraphics has created it
    float    immediateModeVertices[GFX_IMMEDIATE_ITEMS * 4][6]; // +0x20 - maybeImmediateModeFlush's quads, 4 vertices each
    uint32_t immediateModeItemCount;   // +0x1820
    uint32_t viewportX;                // +0x1824
    uint32_t viewportY;                // +0x1828
    uint32_t viewportWidth;            // +0x182c
    uint32_t viewportHeight;           // +0x1830

    // Render-state caches: d3dSetup sets all 21, currentlyLoadedTexture to colorConstant67Cache, to -1.
    uint32_t currentlyLoadedTexture;   // +0x1834 - stage 0
    uint32_t currentStreamBuffer;      // +0x1838
    uint32_t currentIndexBuffer;       // +0x183c
    uint32_t fogModeFlag;              // +0x1840 - untraced meaning; see d3dSetFogEnable/Color
    uint32_t texStage1SlotCache;       // +0x1844
    uint32_t texStage1Param2Cache;     // +0x1848
    uint32_t currentCullMode;          // +0x184c - separate from D3D8's own last-value cache
    uint32_t alphaRefCache;            // +0x1850
    uint32_t depthMaskCache;           // +0x1854
    uint32_t zFuncCache;               // +0x1858
    uint32_t deferredTexStateA;        // +0x185c
    uint32_t deferredTexStateB;        // +0x1860
    uint32_t shardUseAltShader;        // +0x1864
    uint32_t deferredTexBorderColorCache; // +0x1868 - shares its "changed?" pair check with shardUseAltShader
    uint32_t fogEnabledCache;          // +0x186c
    uint32_t fogColorMasked;           // +0x1870 - the colour D3D8 actually gets told about
    uint32_t extraBlendA;              // +0x1874 - a trio set together, driving D3D8 method 0x40358
    uint32_t extraBlendB;              // +0x1878
    uint32_t extraBlendC;              // +0x187c
    uint32_t zBiasActive;              // +0x1880
    uint32_t colorConstant67Cache;     // +0x1884 - packed 0xAARRGGBB behind shader constant 0x67

    uint32_t indexBufferBytesUsed;     // +0x1888 - running totals, informational only
    uint32_t vertexBufferBytesUsed;    // +0x188c
    uint32_t totalTextureBytesUsed;    // +0x1890
    uint8_t  unknown1894[0x5898 - 0x1894];
    D3DOverlayQuadSlotRaw overlayQuads[GFX_OVERLAY_QUAD_SLOTS];     // +0x5898
    uint32_t fallbackOverlayTexture;   // +0x6c98 - used when textureSlot == 0
    D3DTextureSlotRaw textures[GFX_TEXTURE_SLOTS];                   // +0x6c9c
    float    immediateModeItems[GFX_IMMEDIATE_ITEMS][9];             // +0x18c9c - pending quads: rect, UV rect, colour
    D3DVertexBufferSlotRaw vertexBuffers[GFX_VERTEX_BUFFER_SLOTS];   // +0x1959c
    D3DIndexBufferSlotRaw indexBuffers[GFX_INDEX_BUFFER_SLOTS];      // +0x2b59c
    float    u8ToFloat01[256];         // +0x3959c - i / 255

    D3DMATRIX projMatrixCacheA;        // +0x3999c - write-only from what is reimplemented
    D3DMATRIX projMatrixCacheB;        // +0x399dc - scaled by fogScale; feeds the depth clip plane
    D3DMATRIX secondaryBasisMatrix;    // +0x39a1c - feeds constant register 100's half-scaled 2x3 basis
    D3DMATRIX matrix39a5c;             // +0x39a5c - untraced
    D3DMATRIX viewMatrixCache;         // +0x39a9c - the base d3dSetMatrix combines the new matrix with
    D3DMATRIX activeMatrix;            // +0x39adc - Ghidra's d3dActiveMatrix
    float    fogScale;                 // +0x39b1c
    uint32_t lightDirtyMask;           // +0x39b20 - bit i = light i changed; d3dSetMatrix sets all bits
    uint32_t lightEnabledMask;         // +0x39b24 - bit i = light i enabled
    uint32_t levelDirectionDirty;      // +0x39b28 - 1 from d3dSetLevelDirectionVector, -1 from d3dSetMatrix
    GfxLightConstants lights;          // +0x39b2c
    uint32_t lightDirection[4][3];     // +0x39bbc - per light, copied verbatim by d3dSetLight
    float    fogConstant66[4];         // +0x39bec - {1/delta (or a sentinel if delta is ~0), [0]*scaledNear, 16777215, -}
    float    fogScaledNear;            // +0x39bfc
    float    fogScaledFar;             // +0x39c00
    float    fogNearFarDelta;          // +0x39c04
    float    streamStrideConstants[8]; // +0x39c08 - shader constant 0x73
    float    shaderConstant75[4];      // +0x39c28 - {0, 0, characterLightIntensity, 1 - characterLightIntensity}
    float    levelDirectionViewSpace[4]; // +0x39c38 - shader constant 0x7b
    float    levelDirectionVector[3];  // +0x39c48 - world space; no reader found anywhere in the binary
    uint32_t miscModeFlags;            // +0x39c54 - small state-flag word: 0x4/0x8 lights, 0x10, 0x20 texture stage 1
    uint32_t miscResetFlag;            // +0x39c58 - untraced; d3dBeginFrame sets it to -1
    uint32_t auxRenderPassResult;      // +0x39c5c - texture slot written by d3dInitShadowBlurTextures
    uint32_t shadowBlurTargetB;        // +0x39c60 - likewise
    D3DMATRIX auxSavedViewMatrix;      // +0x39c64 - secondaryBasisMatrix, saved across d3dBeginEndAuxRenderPass
    D3DMATRIX auxSavedProjMatrix;      // +0x39ca4 - projMatrixCacheA, likewise
};

static_assert(offsetof(GraphicsSystem, immediateModeItemCount) == 0x1820, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, currentlyLoadedTexture) == 0x1834, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, colorConstant67Cache) == 0x1884, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, totalTextureBytesUsed) == 0x1890, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, overlayQuads) == 0x5898, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, fallbackOverlayTexture) == 0x6c98, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, textures) == 0x6c9c, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, immediateModeItems) == 0x18c9c, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, vertexBuffers) == 0x1959c, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, indexBuffers) == 0x2b59c, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, u8ToFloat01) == 0x3959c, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, projMatrixCacheA) == 0x3999c, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, activeMatrix) == 0x39adc, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, fogScale) == 0x39b1c, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, lights) == 0x39b2c, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, lightDirection) == 0x39bbc, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, fogConstant66) == 0x39bec, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, streamStrideConstants) == 0x39c08, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, shaderConstant75) == 0x39c28, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, levelDirectionVector) == 0x39c48, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, miscModeFlags) == 0x39c54, "GraphicsSystem layout");
static_assert(offsetof(GraphicsSystem, auxSavedViewMatrix) == 0x39c64, "GraphicsSystem layout");
static_assert(sizeof(GraphicsSystem) == 0x39ce4, "Bad size for GraphicsSystem"); // xboxInitGraphics' clear

#define Gfx (*(GraphicsSystem *)0x002c5750)

#endif // GRAPHICSSYSTEM_H_
