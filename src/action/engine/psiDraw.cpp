#include "psiDraw.h"
#include "psiGraphics.h"          // Tex, ModelData, ParticleOverlayRing, psiDrawObjectMatrix
#include "psiInput.h"             // psiInput_PollDevices, psiInput_RumbleUpdate
#include "psiSprite.h"
#include "Script.h"               // ScriptCam
#include "Direct3D/d3dSeam.h"
#include "Direct3D/xboxMatrix.h"
#include "../sound/dsndSeam.h"    // dsndUpdateVoices
#include "../memory.h"
#include "../game.h"              // GameState, Graphics_IsSomeGraphicsRegion
#include "../assets.h"            // HT_Level_*
#include "../util/hashtable.h"

#include <math.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// State shared by the functions here
// ---------------------------------------------------------------------------------------------------------------

#define d3dGeometryObjs (*(ModelData*(*)[2048])0x002a0e68)
#define ScreenBlur (*(uint8_t *)0x002adf33)                   // psiGraphics.cpp clears it at a level change
#define ParticleOverlayRingNext (*(int *)0x002adf80)          // the next ring entry psiDrawParticleList fills
#define bCharacterLight (*(uint8_t *)0x001d7824)              // the game's: characters get their own light
#define bShadowCharacter (*(uint8_t *)0x001d7825)             // the game's: draw this character's shadow
#define Morph_Count (*(uint8_t *)0x001d7826)                  // the game's morph targets for the next model
#define Morph_Weight ((const float *)0x001d6a68)
#define Morph_Index ((const uint8_t *)0x001d777c)
#define ObjectMatrix ((_MATRIX *)0x002ade8c)                  // the game's: the matrix models are drawn with

// The tint every draw is multiplied by (d3dSetColorConstant67), as bytes A, R, G, B
// XBE_GLOBAL(0x002adf2c, 0x4)
static uint8_t Tweak[4];

// The camera's projection scale (zoom), for sizing particles
// XBE_GLOBAL(0x00194814, 0x4)
static float ProjScale = 1.0f;

// The camera's view matrix, as psiUseCamera was given it: 15 floats of a 4x4, the camera's position at 12-14
// XBE_GLOBAL(0x002adec8, 0x3c)
static float ViewMatrix[15];

// The bone matrices of the character being drawn (psiDrawSkinObjectMatrix sets it): 3x4 each, 0x3c apart
// XBE_GLOBAL(0x002adf28, 0x4)
static const uint8_t *SkinPalette;

// The morph targets bound for a model's batch streams: 8 of them, index and weight
// XBE_GLOBAL(0x002ae29c, 0x20)
static int MorphIndex[8];
// XBE_GLOBAL(0x002ae2bc, 0x20)
static float MorphWeight[8];

// Statistics (indices drawn, the model's batch indices, draw calls), never read
// XBE_GLOBAL(0x002ae290, 0x4)
static int StatIndices;
// XBE_GLOBAL(0x002ae294, 0x4)
static int StatBatchIndices;
// XBE_GLOBAL(0x002ae298, 0x4)
static int StatDraws;

static float Bits(uint32_t value) {
    float f;
    memcpy(&f, &value, sizeof(f));
    return f;
}

// The frame of an animated texture showing now
static int CurrentFrameSlot(int texture) {
    const TextureInfo *info = Tex[texture];
    uint32_t frame = ((uint32_t)GameState.NumFramesUnpaused / (uint32_t)info->animSpeed) % (uint32_t)info->numFrames;
    return (&info->baseIdx)[frame];
}

// ---------------------------------------------------------------------------------------------------------------
// Models
// ---------------------------------------------------------------------------------------------------------------

// A model's primitive record: one indexed strip and the state it needs
#pragma pack(push, 1)
struct PrimitiveRecord {
    uint16_t texture0;      // 0x00 Tex[] index for stage 0
    uint16_t texture1;      // 0x02 stage 1 (when PRIM_SET_TEXTURE1)
    int indexCount;         // 0x04 the strip's length
    uint16_t state;         // 0x08 the values: see the PRIM_SET_* below
    uint8_t changes;        // 0x0a which of them to set
    uint8_t skinGroup;      // 0x0b 1-based: the bones this strip is skinned to
};
#pragma pack(pop)
static_assert(sizeof(PrimitiveRecord) == 12, "a primitive record is 12 bytes");

enum {
    PRIM_SET_MORPH = 0x01,      // bind the morph streams (state bit 0) or unbind them
    PRIM_SET_BLEND = 0x02,      // d3dSetupRenderStatesAndFog((state >> 1) & 3)
    PRIM_SET_TEXTURE1 = 0x04,
    PRIM_SET_CULL = 0x08,       // cull unless state bit 3
    PRIM_SET_ALPHAREF = 0x10,   // 0xb4 with state bit 4, else 1
    PRIM_SET_ZWRITE = 0x20,     // state bit 5
    PRIM_SET_TEXWRAP = 0x40,    // d3dSetDeferredTextureState(state bit 7, state bit 8)
};

#define SKIN_GROUP_BYTES 0x36   // a 0xff-terminated list of bone numbers
#define SKIN_BONE_BYTES 0x3c

// Draws a model: its strips one by one, each with its skin bones, state, textures and morph streams. Named for
// what Ghidra first took it to be; it is the game's model drawer (psiDrawObjectMatrix calls it).
// AUTOINJECT
void RecurseAndDrawBoxes(int gfxEntityIdx) {
    if (bCharacterLight)
        gfxSetCharacterLightIntensity(0.5f);
    ModelData *model = d3dGeometryObjs[gfxEntityIdx];
    if (model->batchVtxBuffers != 0) {
        int n = Morph_Count, i = 0;
        if (n > 8) {   // the original would run on into the next globals; ours are elsewhere
            NF_WARN("RecurseAndDrawBoxes: %d morph targets, 8 at most\n", n);
            n = 8;
        }
        for (; i < n; i++) {
            MorphWeight[i] = Morph_Weight[i];
            MorphIndex[i] = Morph_Index[i];
        }
        for (; i < 8; i++) {
            MorphWeight[i] = 0.0f;
            MorphIndex[i] = 0;
        }
    }
    d3dSetRenderState(1);
    d3dBindBuffers(model->vtxBuffers, model->idxBuffer);

    int startIndex = 0;
    const PrimitiveRecord *prim = (const PrimitiveRecord *)(uintptr_t)model->primitives;
    for (int p = 0; p < model->primitiveCnt; p++, prim++) {
        if (prim->skinGroup != 0) {
            const uint8_t *bones = (const uint8_t *)(uintptr_t)model->afterPrimitives +
                                   (prim->skinGroup - 1) * SKIN_GROUP_BYTES;
            for (int slot = 0; bones[slot] != 0xff; slot++) {
                const float *m = (const float *)(SkinPalette + bones[slot] * SKIN_BONE_BYTES);
                float skin[16] = {m[0], m[4], m[8], m[12], m[1], m[5], m[9], m[13],
                                  m[2], m[6], m[10], m[14], 0.0f, 0.0f, 0.0f, 1.0f};
                d3dSetSkinMatrix(skin, slot);
            }
        }
        uint8_t changes = prim->changes;
        if (changes != 0) {
            uint16_t state = prim->state;
            if (changes & PRIM_SET_BLEND)
                d3dSetupRenderStatesAndFog((state >> 1) & 3);
            if (changes & PRIM_SET_TEXTURE1)
                d3dSetTextureStage1(CurrentFrameSlot(prim->texture1), prim->texture0 != 0);
            if (changes & PRIM_SET_CULL)
                d3dSetCullMode(~(state >> 3) & 1);
            if (changes & PRIM_SET_ALPHAREF)
                d3dSetRenderState1((state & 0x10) ? 0xb4 : 1);
            if (changes & PRIM_SET_ZWRITE)
                d3dSetRenderState2((state >> 5) & 1);
            if (changes & PRIM_SET_TEXWRAP)
                d3dSetDeferredTextureState((state >> 7) & 1, (state >> 8) & 1);
            if (changes & PRIM_SET_MORPH) {
                if (state & 1)
                    d3dSetStreamSources(model->batchVtxBuffers, MorphIndex[0], MorphWeight[0], MorphIndex[1],
                                        MorphWeight[1], MorphIndex[2], MorphWeight[2], MorphIndex[3], MorphWeight[3],
                                        MorphIndex[4], MorphWeight[4], MorphIndex[5], MorphWeight[5], MorphIndex[6],
                                        MorphWeight[6], MorphIndex[7], MorphWeight[7]);
                else
                    d3dSetStreamSources(0, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f);
            }
        }
        d3dSetTextureStage0(CurrentFrameSlot(prim->texture0));
        d3dDrawIndexedVertices(startIndex, prim->indexCount);
        StatIndices += prim->indexCount;
        StatDraws++;
        startIndex += prim->indexCount + 2;   // each strip is followed by two degenerate indices
    }
    StatIndices -= model->dataSize18;
    StatBatchIndices += model->dataSize18;
    d3dSetTextureStage1(0, 0);
    if (model->batchVtxBuffers != 0)
        d3dSetStreamSources(0, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f);
    if (bCharacterLight)
        gfxSetCharacterLightIntensity(0.0f);
}

// A character: its skin bones are palette, and its shadow is drawn under it when the game asks
// AUTOINJECT
void psiDrawSkinObjectMatrix(celglist_tag *glist, _MATRIX *unused, void *obj, void *palette) {
    (void)unused;
    SkinPalette = (const uint8_t *)palette;
    psiDrawObjectMatrix(glist, ObjectMatrix);
    if (bShadowCharacter) {
        const char *animState = *(const char **)((const char *)obj + 0xb8);
        const char *bones = *(const char **)(animState + 0x58 + 0x40);
        maybe_psiDrawShadow(*(const float *)(bones + 0x30), *(const float *)(bones + 0xd18),
                            *(const float *)(bones + 0x38), obj, glist);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Particles
// ---------------------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
struct ParticleVertex {
    float x, y, z;
    float unused[3];
    uint32_t colour;
    float unused2;
    float pointSize;        // always 3
};
#pragma pack(pop)
static_assert(sizeof(ParticleVertex) == 0x24, "a particle vertex is 0x24 bytes");

// Draws count particles at positions (four floats each) as point sprites of texture: all colour, or each its
// own from colours. The vertices are kept in the overlay ring until the GPU is done with them
// (psiAgeParticleOverlayRing). Skipped if the ring's next entry is still in use. (0x000dcac0)
// AUTOINJECT
void psiDrawParticleList(const float *positions, int count, uint32_t colour, int texture, const uint32_t *colours,
                         char blend, float size) {
    ParticleOverlayBuffer *entry = &ParticleOverlayRing[ParticleOverlayRingNext];
    if (entry->overlaySlot != 0)
        return;
    void *vertices = Mem_Malloc(count * sizeof(ParticleVertex), 0x1204, 4);
    if (vertices == NULL)
        return;
    ParticleVertex *v = (ParticleVertex *)vertices;
    for (int i = 0; i < count; i++) {
        v[i].x = positions[i * 4 + 0];
        v[i].y = positions[i * 4 + 1];
        v[i].z = positions[i * 4 + 2];
        v[i].unused[0] = v[i].unused[1] = v[i].unused[2] = 0.0f;
        v[i].colour = colours != NULL ? colours[i] : 0xffffffffu;
        v[i].unused2 = 0.0f;
        v[i].pointSize = 3.0f;
    }
    entry->vertexData = vertices;
    entry->overlaySlot = d3dRegisterOverlayBuffer(vertices, count);
    if (entry->overlaySlot == 0) {
        Mem_Free(&vertices);
        return;
    }
    entry->framesToLive = 3;
    D3DMATRIX identity;
    d3dMatrixIdentity(&identity);
    d3dSetWorldMatrix(&identity);
    d3dSetColorConstant67(colours != NULL ? 0xffffffffu : colour);
    d3dSetupRenderStatesAndFog(blend != 0);
    d3dSetRenderState1(1);
    d3dDrawOverlayQuad(entry->overlaySlot, size * 100.0f * ProjScale, CurrentFrameSlot(texture), 0.0f, 0);
    if (++ParticleOverlayRingNext > 0x3f)
        ParticleOverlayRingNext = 0;
}

// Two stubs the effects, particles and drones call (their PS2 counterparts did work)
// FUNC_AT(000de5d0)
int psiStubMinusOne(void) {
    return -1;
}

// FUNC_AT(000de5e0)
uint8_t psiStubFalse(void) {
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// The camera, the frame, fog, blend and tint
// ---------------------------------------------------------------------------------------------------------------

// The NTSC gamma per level (a PAL console keeps the default ramp)
// AUTOINJECT
void ConfigureGammaForLevel(void) {
    if (!Graphics_IsSomeGraphicsRegion())
        return;
    switch ((uint32_t)GameState.CurrentLevelHashcode) {
    case HT_Level_HendersonA:
    case HT_Level_HendersonB: ConfigureGammaRamp(0.81f, 1.025f, 1.0f); return;
    case HT_Level_HendersonC: ConfigureGammaRamp(0.55f, 1.0f, -26.0f); return;
    case HT_Level_HendersonD: ConfigureGammaRamp(1.06f, 1.2625f, 34.0f); return;
    case HT_Level_CastleExterior:
    case HT_Level_CastleCourtyard:
    case HT_Level_AllCharacters:
    case HT_Level_AllCharacters2: ConfigureGammaRamp(0.95f, 0.9125f, 0.0f); return;
    case HT_Level_CastleIndoors1: ConfigureGammaRamp(0.93f, 0.925f, 0.0f); return;
    case HT_Level_CastleIndoors2: ConfigureGammaRamp(0.81f, 1.0f, -1.0f); return;
    case HT_Level_TowerA:
    case HT_Level_Tower2C: ConfigureGammaRamp(0.94f, 1.2625f, 31.0f); return;
    case HT_Level_TowerB:
    case HT_Level_Tower2B: ConfigureGammaRamp(0.96f, 1.225f, 27.0f); return;
    case HT_Level_TowerC:
    case HT_Level_Tower2A: ConfigureGammaRamp(0.98f, 1.2625f, 21.0f); return;
    case HT_Level_PowerStationA1: ConfigureGammaRamp(0.84f, 0.82500005f, -16.0f); return;
    case HT_Level_PowerStationA2: ConfigureGammaRamp(0.87f, 0.79999995f, -16.0f); return;
    case HT_Level_EvilBase: ConfigureGammaRamp(1.08f, 1.0125f, 12.0f); return;
    case HT_Level_EvilSilo: ConfigureGammaRamp(1.16f, 1.0625f, 15.0f); return;
    case HT_Level_EvilBaseC: ConfigureGammaRamp(1.05f, 1.0875f, 14.0f); return;
    case HT_Level_SpaceStationD: ConfigureGammaRamp(0.89f, 0.72499996f, -21.0f); return;
    case HT_Level_Atlantis:
    case HT_Level_FortKnox: ConfigureGammaRamp(1.17f, 1.1f, 17.0f); return;
    case HT_Level_SkyRail: ConfigureGammaRamp(1.14f, 1.1375f, 22.0f); return;
    case HT_Level_SubPen:
    case HT_Level_Ravine: ConfigureGammaRamp(1.01f, 1.15f, 18.0f); return;
    case HT_Level_StealthShip:
    case HT_Level_SnowBlind: ConfigureGammaRamp(1.17f, 1.0625f, 17.0f); return;
    case HT_Level_MissileSilo: ConfigureGammaRamp(0.89f, 1.4f, 10.0f); return;
    case HT_Level_RefRoom:
    case 0x0700004c: ConfigureGammaRamp(1.01f, 1.175f, 22.0f); return;
    case HT_Level_Tower2Elevator: ConfigureGammaRamp(0.93f, 1.25f, 22.0f); return;
    default: ConfigureGammaRamp(0.71f, 1.12f, 1.0f); return;
    }
}

// Ends the frame: the screen blur if one is on (not while the menus are up), the swap, the sound voices and the
// rumble
// AUTOGEN
uint __stdcall MenuManager_GetStatus(void);

// AUTOINJECT
void psiPostDraw(void) {
    d3dSetupViewportDimensions(0, 0, 640, 480);
    if (ScreenBlur != 0 && MenuManager_GetStatus() != 3)
        psiBlurScreen(ScreenBlur);
    d3dSwap();
    dsndUpdateVoices();
    psiInput_RumbleUpdate();
}

// AUTOINJECT
void psiSetUpColourBlend(int opaque) {
    d3dSetupRenderStatesAndFog(opaque == 0);
}

#pragma pack(push, 1)
struct Viewer {             // the part of the game's viewer_tag read here
    char unknown00[0xdc];
    float xMin, xMax;       // 0xdc the viewport, in pixels
    float yMin, yMax;       // 0xe4
    char unknownec[8];
    float projScaleZ;       // 0xf4 zoom
    char unknownf8[8];
    float aspectRatio;      // 0x100
    float fovRadians;       // 0x104
    float viewMatrix[15];   // 0x108
};
#pragma pack(pop)
static_assert(offsetof(Viewer, viewMatrix) == 0x108, "viewer_tag.viewMatrix is at 0x108");

// Sets up the viewport, the projection and the view from a viewer (0x000dd950)
// AUTOINJECT
void psiUseCamera(void *viewer) {
    const Viewer *v = (const Viewer *)viewer;
    ProjScale = v->projScaleZ;
    if (ProjScale < 0.001f)
        ProjScale = 0.001f;
    memcpy(ViewMatrix, v->viewMatrix, sizeof(ViewMatrix));
    d3dSetupViewportDimensions((unsigned)(int)v->xMin, (unsigned)(int)v->yMin, (unsigned)(int)(v->xMax - v->xMin),
                               (unsigned)(int)(v->yMax - v->yMin));
    D3DMATRIX projection;
    createProjectionMatrix(&projection, v->aspectRatio, 0.0f, (v->fovRadians / v->projScaleZ) * 57.2958f, 0.01f,
                           3000.0f);
    d3dSetProjectionMatrix(&projection);
    const float *c = ViewMatrix;
    D3DMATRIX view = {{{c[0], c[4], c[8], c[12], c[1], c[5], c[9], c[13], c[2], c[6], c[10], c[14],
                        0.0f, 0.0f, 0.0f, 1.0f}}};
    maybeMatrixAxisScale(&view, -1.0f, 1.0f, 1.0f, 1);
    d3dSetViewMatrixFromRigidTransform(&view);
}

// AUTOINJECT
void psiSetScreenBlur(uint8_t amount) {
    ScreenBlur = amount > 0x80 ? 0x80 : amount;
}

// The fog, which a few levels override with their own; elsewhere it is off whatever the caller asked
// AUTOINJECT
void psiFog(char enable, float nearDist, float farDist, uint8_t r, uint8_t g, uint8_t b) {
    static const struct { uint32_t level; float nearDist, farDist; uint8_t r, g, b; } levels[] = {
        {HT_Level_HendersonB, 10.0f, 240.0f, 0x3b, 0x3f, 0x80},
        {HT_Level_HendersonD, 7.0f, 55.0f, 0x0b, 0x12, 0x1d},
        {HT_Level_CastleExterior, 26.0f, 213.0f, 0x35, 0x48, 0x66},
        {HT_Level_CastleCourtyard, 12.0f, 96.0f, 0x5a, 0x75, 0x98},
        {HT_Level_CastleIndoors1, 10.0f, 150.0f, 0xc4, 0x7f, 0x60},
        {HT_Level_CastleIndoors2, 10.0f, 200.0f, 0x50, 0x64, 0x82},
        {HT_Level_TowerA, 10.0f, 114.0f, 0x79, 0x80, 0x96},
        {HT_Level_TowerB, 10.0f, 100.0f, 0x86, 0x7f, 0x84},
        {HT_Level_PowerStationA1, 45.0f, 145.0f, 0xd2, 0xd2, 0xc1},
        {HT_Level_PowerStationA2, 20.0f, 196.0f, 0x99, 0x8b, 0x70},
        {0x0700000f, 0.0f, 20.0f, 0x59, 0xab, 0x5a},
        {0x07000010, 10.0f, 86.0f, 0x89, 0x98, 0xaf},
        {HT_Level_EvilBase, 4.0f, 77.0f, 0x80, 0x80, 0xbe},
        {HT_Level_EvilBaseC, 7.0f, 151.0f, 0x80, 0x80, 0xdb},
        {HT_Level_SkyRail, 10.0f, 179.0f, 0x18, 0x2f, 0x53},
        {HT_Level_FortKnox, 17.0f, 182.0f, 0xb8, 0xab, 0xce},
        {HT_Level_MissileSilo, 10.0f, 122.0f, 0x83, 0xb6, 0x7b},
        {HT_Level_Tower2Elevator, 3.0f, 35.0f, 0x16, 0x1f, 0x34},
    };
    bool found = false;
    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++)
        if ((uint32_t)GameState.CurrentLevelHashcode == levels[i].level) {
            nearDist = levels[i].nearDist;
            farDist = levels[i].farDist;
            r = levels[i].r;
            g = levels[i].g;
            b = levels[i].b;
            found = true;
            break;
        }
    if (!found)
        enable = 0;
    d3dSetFogEnable(enable);
    if (enable) {
        d3dSetFogNear(nearDist);
        d3dSetFogFar(farDist);
        d3dSetFogColor((uint32_t)r << 16 | (uint32_t)g << 8 | b);
    }
}

static void ApplyTweak(void) {
    d3dSetColorConstant67((uint32_t)Tweak[0] << 24 | (uint32_t)Tweak[1] << 16 | (uint32_t)Tweak[2] << 8 | Tweak[3]);
}

// AUTOINJECT
void psiSetTweakARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    Tweak[0] = a;
    Tweak[1] = r;
    Tweak[2] = g;
    Tweak[3] = b;
    ApplyTweak();
}

// AUTOINJECT
void psiSetTweakA(uint8_t a) {
    Tweak[0] = a;
    ApplyTweak();
}

// Covers the screen in a colour (for fades)
// AUTOINJECT
void psiFadeView(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SPRITE_DRAW sprite;
    memset(&sprite, 0, sizeof(sprite));
    sprite.clrR = r;
    sprite.clrG = g;
    sprite.clrB = b;
    sprite.clrA = a;
    sprite.width = 640.0f;
    sprite.height = 480.0f;
    psiDrawSprites(&sprite, 1);
}

// AUTOINJECT
void psiClearZ(void) {
    d3dClear(0, false, true);
}

// The Xbox layer's thunk to the pad polling (0x000de5c0), which Input_Update calls
// FUNC_AT(000de5c0)
void psiInput_PollDevices_Thunk(void) {
    psiInput_PollDevices();
}

// ---------------------------------------------------------------------------------------------------------------
// Shards: broken glass. psiGetTriList gives the breakage code an object's triangles, psiCreateShard makes a
// three-vertex shard of them and psiDrawShard draws one.
// ---------------------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
struct TriListTriangle {
    uint32_t texture;           // the strip's stage-0 texture
    short a, b, c;
    short pad;
};
// The layout ShatterPolyRecurse works in: it copies three of these into a shard's vertex list, puts the texture
// in the first word, and psiCreateShard reads the position from +4
struct TriListVertex {
    uint32_t texture;           // unused here; ShatterPolyRecurse's
    float pos[3];               // 0x04
    float uv[2];                // 0x10
    float colour[4];            // A, R, G, B
};
struct TriList {
    int triangleCount;          // as allocated: the model's strip indices less its batch's
    int vertexCount;
    TriListTriangle *triangles;
    TriListVertex *vertices;
};
struct ShardVertex {
    float pos[3];
    float pad;
    uint32_t colour;
    float uv[2];
};
#pragma pack(pop)
static_assert(sizeof(TriListTriangle) == 0xc && sizeof(TriListVertex) == 0x28 && sizeof(ShardVertex) == 0x1c,
              "shard layouts");

// AUTOINJECT
void* psiGetTriList(void *obj) {
    if (obj == NULL)
        return NULL;
    celglist_tag *gfx = *(celglist_tag **)((char *)obj + 0xb4);   // obj_tag.objGraphics
    if (gfx == NULL || gfx->geom_idx == 0)
        return NULL;
    int idx = gfx->geom_idx;
    ModelData *model = d3dGeometryObjs[idx];
    TriList *list = (TriList *)Mem_Malloc(sizeof(TriList), 0x1204, 4);
    if (list == NULL)
        return NULL;
    int allocated = model->dataSize14 - model->dataSize18;
    list->triangleCount = allocated;
    list->vertexCount = model->vtxCnt;
    list->triangles = (TriListTriangle *)Mem_Malloc(allocated * sizeof(TriListTriangle), 0x1204, 4);
    list->vertices = (TriListVertex *)Mem_Malloc(list->vertexCount * sizeof(TriListVertex), 0x1204, 4);
    if (list->triangles == NULL || list->vertices == NULL)
        return NULL;

    const short *indices = (const short *)d3dGetIndexBufferData(model->idxBuffer);
    int kept = 0, start = 0;
    const PrimitiveRecord *prim = (const PrimitiveRecord *)(uintptr_t)model->primitives;
    for (int p = 0; p < d3dGeometryObjs[idx]->primitiveCnt; p++, prim++) {
        const short *s = indices + start;
        for (int i = 0; i < prim->indexCount; i++, s++) {
            if (s[0] != s[1] && s[0] != s[2] && s[1] != s[2]) {
                TriListTriangle *t = &list->triangles[kept++];
                t->texture = prim->texture0;
                t->a = s[0];
                t->b = s[1];
                t->c = s[2];
            }
        }
        start += 2 + prim->indexCount;
    }
    for (int i = 0; i < list->vertexCount; i++) {
        TriListVertex *v = &list->vertices[i];
        uint32_t colour = 0;
        d3dGetStreamBuffer(model->vtxBuffers, i, (uint32_t *)v->pos, (uint32_t *)v->uv, &colour);
        v->colour[0] = (float)((colour >> 24) & 0xff);
        v->colour[1] = (float)((colour >> 16) & 0xff);
        v->colour[2] = (float)((colour >> 8) & 0xff);
        v->colour[3] = (float)(colour & 0xff);
    }
    return list;
}

// AUTOINJECT
void psiDeleteShard(void **shard) {
    if (shard == NULL)
        return;
    void *data = *shard;
    if (data != NULL)
        Mem_Free(&data);
    *shard = NULL;
}

// A shard from three vertices as the breakage code keeps them (ten floats each, after a word): position, uv,
// colour A R G B (0x000df960)
// AUTOINJECT
void* psiCreateShard(const float *vertices) {
    if (vertices == NULL)
        return NULL;
    ShardVertex *shard = (ShardVertex *)Mem_Malloc(3 * sizeof(ShardVertex), 0x1204, 4);
    for (int i = 0; i < 3; i++) {
        const float *in = vertices + 1 + i * 10;
        ShardVertex *out = &shard[i];
        out->pos[0] = in[0];
        out->pos[1] = in[1];
        out->pos[2] = in[2];
        out->pad = 0.0f;
        out->uv[0] = in[3];
        out->uv[1] = in[4];
        out->colour = (uint32_t)(uint8_t)(int)in[5] << 24 | (uint32_t)(uint8_t)(int)in[6] << 16 |
                      (uint32_t)(uint8_t)(int)in[7] << 8 | (uint8_t)(int)in[8];
    }
    return shard;
}

// AUTOINJECT
void psiDrawShard(void *vtxData, _MATRIX *matrix, int texture) {
    if (vtxData == NULL || matrix == NULL)
        return;
    d3dSetTextureStage0(CurrentFrameSlot(texture));
    const float *m = (const float *)matrix;
    D3DMATRIX mtx = {{{m[0], m[4], m[8], m[12], m[1], m[5], m[9], m[13], m[2], m[6], m[10], m[14],
                       0.0f, 0.0f, 0.0f, 1.0f}}};
    d3dSetMatrix(&mtx);
    drawShard(vtxData, 1);
}

// AUTOINJECT
void psiSetShardRenderStates(void) {
    d3dSetupRenderStatesAndFog(0);
    Tweak[0] = Tweak[1] = Tweak[2] = Tweak[3] = 0xff;
    d3dSetColorConstant67(0xffffffffu);
    d3dSetDeferredTextureState(1, 1);
    d3dSetTextureStage1(0, 0);
    d3dSetRenderState1(1);
    d3dSetRenderState(1);
    d3dSetRenderState2(0);
    d3dSetCullMode(0);
}

// The PS2 build copied to the scratchpad; here it is the pointer itself
// AUTOINJECT
uint32_t psiCopyToSP(uint32_t value) {
    return value;
}

// ---------------------------------------------------------------------------------------------------------------
// A character's shadow. Within 35 units of the camera, the ground under the character (the collision triangles
// in a 2-unit drop) is drawn with a projected texture: the character itself rendered from above into a texture
// when within 20 units (blurred within 4), otherwise a generic blob. The triangles fade out over 1-2 units below
// the character.
// ---------------------------------------------------------------------------------------------------------------

// AUTOGEN
undefined4 __cdecl Collide_unknown(_VECTOR * param_1, _VECTOR * param_2, float param_3, int param_4, uint * param_5,
                                   undefined2 param_6, undefined4 param_7);

#define SHADOW_MAX_TRIANGLES 300
static ShardVertex ShadowVertices[SHADOW_MAX_TRIANGLES * 3];

// Called only by psiDrawSkinObjectMatrix, so not injected (the original takes arguments in registers)
void maybe_psiDrawShadow(float x, float y, float z, void *obj, celglist_tag *glist) {
    uint32_t level = (uint32_t)GameState.CurrentLevelHashcode;
    if (level == HT_Level_AllCharacters || level == HT_Level_AllCharacters2 || (uint32_t)ScriptCam == 0x0600068f)
        return;
    float dx = ViewMatrix[12] - x, dy = ViewMatrix[13] - y, dz = ViewMatrix[14] - z;
    float distance2 = dz * dz + dy * dy + dx * dx;
    if (distance2 > 1225.0f)
        return;

    _VECTOR from, to;
    from.x = x;
    from.y = y + 0.2f;
    from.z = z;
    to.x = x;
    to.y = from.y - 2.0f;
    to.z = z;
    uint triangles = 0;
    const float *ground = (const float *)(uintptr_t)Collide_unknown(&from, &to, 1.0f, *(int *)((char *)obj + 0x20),
                                                                    &triangles, 0x307, 4);
    if (ground == NULL || (int)triangles < 1)
        return;

    D3DMATRIX lookDown, projection;
    MatrixLookAt(&lookDown, from.x, from.y + 20.0f, from.z, from.x, from.y, from.z, 1.0f, 0.0f, 0.0f);
    createProjectionMatrix(&projection, 1.0f, 7.0f, 0.0f, 1.0f, 200.0f);
    void *blob = hashtable_getitem((HASHCODE)0x03000047);
    int blobTexture = blob != NULL ? *(int *)blob : 0;
    int shadowTexture = CurrentFrameSlot(blobTexture);
    bool rendered = false;
    if (distance2 < 400.0f) {
        rendered = true;
        shadowTexture = (int)d3dBeginEndAuxRenderPass(1, &lookDown, &projection);
        psiDrawObjectMatrix(glist, ObjectMatrix);
        d3dBeginEndAuxRenderPass(0, NULL, NULL);
        if (distance2 < 16.0f)
            psiBlurCharacterShadow();
    }

    int alpha = rendered ? 0x40 : 0x80;
    if ((int)triangles > SHADOW_MAX_TRIANGLES)
        triangles = SHADOW_MAX_TRIANGLES;
    ShardVertex *out = ShadowVertices;
    for (uint t = 0; t < triangles; t++)
        for (int k = 0; k < 3; k++, ground += 3, out++) {
            out->pos[0] = ground[0];
            out->pos[1] = ground[1];
            out->pos[2] = ground[2];
            out->pad = 0.0f;
            out->uv[0] = out->uv[1] = 0.0f;
            float below = fabsf(ground[1] - from.y);
            uint8_t a;
            if (below < 1.0f)
                a = (uint8_t)alpha;
            else if (below > 2.0f)
                a = 0;
            else
                a = (uint8_t)(int)((1.0f - (below - 1.0f)) * (float)alpha);
            out->colour = (uint32_t)a << 24;
        }

    d3dSetTextureStage0(0);
    D3DMATRIX lift;
    d3dMatrixIdentity(&lift);
    maybeMtxApplyTransform(&lift, 0.0f, 0.01f, 0.0f);
    d3dSetMatrix(&lift);
    d3dSetupRenderStatesAndFog(0);
    uint8_t saved[4] = {Tweak[0], Tweak[1], Tweak[2], Tweak[3]};
    psiSetTweakARGB(0xff, 0xff, 0xff, 0xff);
    d3dSetDeferredTextureState(1, 1);
    d3dSetTextureStage1(0, 0);
    d3dSetRenderState1(1);
    d3dSetRenderState2(0);
    d3dSetRenderState(1);
    d3dSetCullMode(0);
    d3dSetTextureWithBorderColor(shadowTexture, 0);
    createProjectionMatrix(&projection, 1.0f, (float)(rendered * 8 + 6), 0.0f, 1.0f, 200.0f);
    maybeMatrixAxisScale(&lookDown, -1.0f, 1.0f, 1.0f, 1);
    maybeBuildAndSetModelViewProjectionMtx(&lookDown, &projection);
    drawShard(ShadowVertices, triangles);
    d3dSetTextureWithBorderColor(0, 0);
    psiSetTweakARGB(saved[0], saved[1], saved[2], saved[3]);
}
