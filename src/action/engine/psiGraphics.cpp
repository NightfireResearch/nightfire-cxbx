#include "psiGraphics.h"

#include "Direct3D/d3dhelpers.h"
#include "Direct3D/d3dSeam.h" // d3dSetMatrix is reimplemented there now, not called through an AUTOGEN passthrough
#include "Woman.h"                // MemoryForWoman
#include "psiInput.h"             // controllerIsPresent, controller_maybeRumbleTimeout
#include "../memory.h"            // Mem_Free
#include "psiSave.h"              // psiInternalLoadingDataState
#include "Fmv.h"
#include "../game.h"              // GameState, timestamp(), BackgroundMovieHashcode
#include <string.h>               // memset
#include "XboxSystem.h"
#include "psiDraw.h"



// AUTOINJECT
void psiDrawObjectMatrix(celglist_tag* celglist, _MATRIX* matrix) {

    if (celglist == NULL || celglist->geom_idx == 0) {
        return;
    }
    D3DMATRIX d3dMatrix;
    _MATRIXtoD3DMATRIX(matrix, &d3dMatrix);
    d3dSetMatrix(&d3dMatrix);
    RecurseAndDrawBoxes(celglist->geom_idx);
}

// ---------------------------------------------------------------------------------------------------------------
// psiAgeParticleOverlayRing
// ---------------------------------------------------------------------------------------------------------------

// Frames a particle batch's overlay buffer is held before it is freed - set by psiDrawParticleList, which
// fills the ring (not reimplemented). Recorded here only for reference.
// #define PARTICLE_OVERLAY_FRAMES_TO_LIVE 3

// Called every frame from psiPreDraw, before d3dBeginFrame. The GPU may still be reading a particle batch's
// vertices for a frame or two after it was submitted, so psiDrawParticleList parks each batch's overlay slot
// and vertex memory in this ring instead of freeing them; this counts each entry down and frees it when its
// time is up. Name invented (FUN_000dcc60); Xbox only, the PS2 build has no such ring.
//
// AUTOINJECT
void psiAgeParticleOverlayRing(void) {

    for (int i = 0; i < PARTICLE_OVERLAY_RING_SIZE; i++) {
        ParticleOverlayBuffer *entry = &ParticleOverlayRing[i];

        if (entry->overlaySlot == 0)
            continue; // an empty entry: its countdown is not touched

        entry->framesToLive--;
        if (entry->framesToLive > 0)
            continue;

        d3dReleaseOverlayBuffer(entry->overlaySlot);

        if (entry->vertexData != NULL) {
            // Freed through a copy of the pointer, as the original does; the entry is cleared below anyway
            void *vertices = entry->vertexData;
            Mem_Free(&vertices);
        }

        entry->overlaySlot = 0;
        entry->vertexData = NULL;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// maybePsiResetResources
// ---------------------------------------------------------------------------------------------------------------



// Set here and consumed by the first psiPreDraw after the reset: it calls ConfigureGammaForLevel, turns
// LevelLoadTime from the reset's timestamp into the load's duration, and zeroes TimeSpentLoadingFiles.
// XBE_GLOBAL(0x002adf30, 0x1)
static char LevelStartPending;
// Set here and by psiCreateMapTextures/psiCreateEntityGfx; the next psiPreDraw did a WBINVD (writes the whole CPU
// cache back) so the GPU saw the freshly loaded texture and vertex data. Only cleared now.
// XBE_GLOBAL(0x002adf31, 0x1)
static char CacheFlushPending;
#define ScreenBlur (*(char*)0x002adf33)
#define TimeSpentLoadingFiles (*(double*)0x002adf38)
// XBE_GLOBAL(0x002adf40, 0x8)
static double LevelLoadTime;
#define debug_scanmode (*(char*)0x001fec49)
#define debug_clipmode (*(char*)0x001fec4a)

// Level-scoped graphics bookkeeping, all reset here. The first index of Tex[] and d3dGeometryObjs[] is the
// default entry, so the counts restart at 1 (and start there: 1 in the XBE's data).
#define d3dGeometryObjs (*(ModelData*(*)[2048])0x002a0e68)
// XBE_GLOBAL(0x002ae4f8, 0x58)
static TextureInfo DefaultTextureInfo;
// XBE_GLOBAL(0x00194808, 0x4)
static int NumXboxEntityGfxsCreated = 1;
// XBE_GLOBAL(0x0019480c, 0x4)
static int NumXboxTexLoaded = 1;
// XBE_GLOBAL(0x00194810, 0x4)
static int FirstMapTexIdx = 1; // psiCreateMapTextures: NumXboxTexLoaded when the map's textures began
// XBE_GLOBAL(0x002adf24, 0x4)
static int NumXboxVtxsLoaded;
// XBE_GLOBAL(0x002adf20, 0x4)
static int EntityGfxStat_2adf20; // psiCreateEntityGfx accumulates it; not yet identified

// Texture statistics psiCreateMapTextures keeps: a count and a byte total per texture format class. Nothing reads
// them (the debug output that did is compiled out).
// XBE_GLOBAL(0x002ade84, 0x4)
static int someTypeTexCount; // formatType 1
// XBE_GLOBAL(0x002ade80, 0x4)
static int someTypeTexBytes;
// XBE_GLOBAL(0x002a0e60, 0x4)
static int someOtherTexCount; // formatType 0 or >5, 8 bits per pixel
// XBE_GLOBAL(0x002adf04, 0x4)
static int someOtherTexBytes;
// XBE_GLOBAL(0x002abe7c, 0x4)
static int someTexCount; // formatType 0 or >5, other depths
// XBE_GLOBAL(0x002ade88, 0x4)
static int someTexBytes;
// XBE_GLOBAL(0x002adf08, 0x4)
static int someThirdTexCount; // formatType 2..5
// XBE_GLOBAL(0x002a0e64, 0x4)
static int someThirdTexBytes;

// Called from ResetMap_Load at every level change: returns the psi layer (the Xbox platform glue) to its
// just-booted state before the next level's data is loaded.
//
// AUTOINJECT
void maybePsiResetResources(void) {

    // Assume every controller is present, so the first psiInput_MapInputs after the load sees no "controller
    // just plugged in" edge (which would skip the attract movie) for a pad that was already there
    for (int i = 0; i < 4; i++)
        controllerIsPresent[i] = 1;

    LevelStartPending = 1;
    CacheFlushPending = 1;
    TimeSpentLoadingFiles = 0.0; // two dword stores of 0 in the original; the same bits
    LevelLoadTime = timestamp(); // the start of the load, for now

    debug_scanmode = 0;
    debug_clipmode = 0;
    psiInternalLoadingDataState = 0;
    ScreenBlur = 0;

    maybeBackgroundMovieCleanup();
    BackgroundMovieHashcode = 0; // as psiStopBackgroundMovie does after the same call

    maybeCleanupSystem(); // releases the level's D3D resources (see d3dSeam.cpp's maybeD3dShutdown notes)

    for (int i = 0; i < 4; i++)
        controller_maybeRumbleTimeout[i] = -1; // no rumble running

    // REP STOSD clears in the original
    memset(&d3dGeometryObjs, 0, sizeof(d3dGeometryObjs));
    memset(&Tex, 0, sizeof(Tex));
    memset(&ParticleOverlayRing, 0, sizeof(ParticleOverlayRing)); // dropped, not freed: the heap is about to be wiped
    memset(&DefaultTextureInfo, 0, sizeof(DefaultTextureInfo));

    MemoryForWoman = NULL; // the pause-menu background's buffer, in the heap that is about to be wiped

    // Tex[0] is a blank one-frame texture, so that texture index 0 is always safe to draw with
    DefaultTextureInfo.numFrames = 1;
    DefaultTextureInfo.animSpeed = 1;
    DefaultTextureInfo.baseIdx = 0;
    Tex[0] = &DefaultTextureInfo;

    NumXboxEntityGfxsCreated = 1;
    NumXboxTexLoaded = 1;
    FirstMapTexIdx = 1;

    someOtherTexCount = 0;
    someTexCount = 0;
    someThirdTexCount = 0;
    someTypeTexCount = 0;
    someOtherTexBytes = 0;
    someTexBytes = 0;
    someThirdTexBytes = 0;
    someTypeTexBytes = 0;
    EntityGfxStat_2adf20 = 0;
    NumXboxVtxsLoaded = 0;

    // A per-level axis for Gfx.levelDirectionVector, keyed on the level about to load. Nothing that reads it has
    // been found (see d3dSetLevelDirectionVector), so what it points along is unknown.
    switch (GameState.NextLevelHashcode) {
    case HT_Level_CastleCourtyard:
    case HT_Level_PowerStationA2:
        d3dSetLevelDirectionVector(-1.0f, 0.0f, 0.0f);
        break;
    case HT_Level_PowerStationA1:
        d3dSetLevelDirectionVector(0.0f, 0.0f, -1.0f);
        break;
    case HT_Level_Cut_Level3:
        d3dSetLevelDirectionVector(0.0f, 0.0f, 1.0f);
        break;
    default:
        d3dSetLevelDirectionVector(1.0f, 0.0f, 0.0f);
        break;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// psiPreDraw, psiCreateMapTextures, psiCreateEntityGfx
// ---------------------------------------------------------------------------------------------------------------


// The start of every frame's drawing (from mainloop). The first frame after a level load sets the level's gamma and
// turns LevelLoadTime into how long the load took. The cache flush psiCreateMapTextures and psiCreateEntityGfx ask
// for was a WBINVD on the Xbox (so the GPU saw their data); it is patched out now (XboxStartup.cpp), so only the
// request is cleared. Clears the colour only on the front end, then draws the background movie if one is playing.
// AUTOINJECT
void psiPreDraw(void) {
    if (LevelStartPending) {
        ConfigureGammaForLevel();
        LevelLoadTime = timestamp() - LevelLoadTime;
        TimeSpentLoadingFiles = 0.0;
        LevelStartPending = 0;
    }
    if (CacheFlushPending)
        CacheFlushPending = 0;
    psiAgeParticleOverlayRing();
    d3dBeginFrame();
    d3dClear(0, GameState.CurrentLevelHashcode == HT_Level_Menu_Pre, true);
    maybeStartBackgroundMovie();
}

// RegisterTexture's format for a texture: by depth for formatType 0, one of four for 2..5, else 3
static int TextureInfo_Format(const TextureInfo *tex) {
    switch (tex->formatType) {
    case 0:
        if (tex->bitsPerPixel == 4)
            return 1;
        return tex->bitsPerPixel == 8 ? 2 : 0;
    case 2: return 4;
    case 3: return 5;
    case 4: return 6;
    case 5: return 7;
    default: return 3;
    }
}

// Registers a map's textures: each header goes in the next Tex[] slot (FirstMapTexIdx is where the map's first went,
// which psiCreateEntityGfx adds to the map's texture references), each frame of a texture of its own gets a
// texture slot, and a "DT" duplicate takes the frames of the texture it names. Its animation rate becomes frames per
// step at 60 a second. The Tex[] index, or 0 for an unusable header, goes in the map's texData.
// AUTOINJECT
void psiCreateMapTextures(map_tag *mapptr) {

    texDataEntry *texData = mapptr->texData;
    CacheFlushPending = 1;
    FirstMapTexIdx = NumXboxTexLoaded;

    for (uint i = 0; i < (uint)mapptr->numTexHeaderEntries; i++, NumXboxTexLoaded++) {
        TextureInfo *tex = mapptr->texHeaderData[i].textureInfo;
        // The GameCube stops at 0x500 ("Too many textures headers, change max", fatal); here the limit is Tex[]'s
        // 2048 slots, past which this would write over whatever follows it
        NF_ASSERT(NumXboxTexLoaded < (int)ARRAY_SIZE(Tex), "Too many textures headers, change max");
        Tex[NumXboxTexLoaded] = tex;
        texData[i].texIdx = NumXboxTexLoaded;
        if (tex == NULL || tex == (TextureInfo *)-1) {
            texData[i].texIdx = 0;
            continue;
        }

        uint magic = tex->magic;
        bool duplicate = (magic & 0xffff0000) == TEXINFO_DUPLICATE;
        if (magic != TEXINFO_MAGIC && !duplicate) {
            // Not a texture: one frame, no slot
            tex->animSpeed = 1;
            tex->numFrames = 1;
            tex->baseIdx = 0;
            texData[i].texIdx = 0;
            continue;
        }

        uint rate = (uint)tex->animSpeed;
        tex->animSpeed = (rate >= 1 && rate < 60) ? (int)(60 / rate) : 1;
        if (tex->numFrames == 0)
            tex->animSpeed = 1;

        int *frames = &tex->baseIdx;
        if (duplicate) {
            // The index is taken as Tex[index + 1] whatever FirstMapTexIdx is - right for the first map loaded after
            // maybePsiResetResources, whose textures start at 1
            TextureInfo *original = Tex[(magic & 0xffff) + 1];
            for (int f = 0; f < tex->numFrames; f++)
                frames[f] = (&original->baseIdx)[f];
            continue;
        }

        int numFrames = tex->numFrames;
        uint bytes = tex->frameBytes * numFrames;
        if (tex->formatType == 1) {
            someTypeTexCount++;
            someTypeTexBytes += bytes;
        } else if (tex->formatType >= 2 && tex->formatType <= 5) {
            someThirdTexCount++;
            someThirdTexBytes += bytes;
        } else if (tex->bitsPerPixel == 8) {
            someOtherTexCount++;
            someOtherTexBytes += bytes;
        } else {
            someTexCount++;
            someTexBytes += bytes;
        }

        char *pixels = (char *)(frames + numFrames);
        for (int f = 0; f < tex->numFrames; f++) {
            frames[f] = RegisterTexture(tex->width, tex->height, TextureInfo_Format(tex), tex->levels, pixels,
                                        tex->registerParam6);
            pixels += tex->frameBytes;
        }
    }
}

// A primitive's texture reference, from the map's numbering to Tex[]: 0xffff (none) becomes 0, anything else is
// offset by where the map's textures start; a texture with no slot becomes 0 too. 16-bit, as in the original.
static ushort MapTextureRef(ushort ref, int firstMapTex) {
    ushort idx = (ref == 0xffff) ? 0 : (ushort)(ref + firstMapTex);
    return Tex[idx]->baseIdx != 0 ? idx : 0;
}

// Registers an entity's geometry (see ModelData): its vertex and index buffers and, if it has any, its batch
// vertices, then points its primitives' texture references at Tex[]. The cel's geom_idx, the ModelData's address
// until now, becomes its index in d3dGeometryObjs - or 0 if it is not usable geometry.
// AUTOINJECT
void psiCreateEntityGfx(celglist_tag *param_1, map_tag *param_2, uint param_3) {

    ModelData *model = (ModelData *)(uintptr_t)param_1->geom_idx;
    CacheFlushPending = 1;
    if (model == NULL || model->magicTag != MODELDATA_MAGIC || model->vtxCnt == 0 || model->idxCnt == 0 ||
        model->primitiveCnt == 0 || model->dataSize14 == 0) {
        param_1->geom_idx = 0;
        return;
    }

    d3dGeometryObjs[NumXboxEntityGfxsCreated] = model;

    uint data = (uint)(uintptr_t)model->data;
    model->vtxBuffers = d3dCreateVertexBuffers(model->vtxCnt, data, model->maybeSkinned != 0, 0);
    data += d3dGetVertexDataSize(model->vtxCnt, model->maybeSkinned != 0, 0);
    model->idxBuffer = d3dCreateIndexBuffer(model->idxCnt, data);
    data += d3dGetIndexDataSize(model->idxCnt);
    model->afterPrimitives = data + model->primitiveCnt * 12;
    model->primitives = data;

    if (model->batchCnt != 0 && model->batchVtxCnt != 0) {
        model->batchVtxBuffers = d3dCreateVertexBuffers(model->batchVtxCnt,
                                                        model->afterPrimitives + model->maybeSkinned * 0x36, 0,
                                                        model->batchCnt);
        d3dGetVertexDataSize(model->batchVtxCnt, 0, model->batchCnt); // the original ignores this one's result
    }

    int firstMapTex = FirstMapTexIdx;
    for (int p = 0; p < model->primitiveCnt; p++) {
        ushort *refs = (ushort *)(uintptr_t)(model->primitives + p * 12);
        refs[0] = MapTextureRef(refs[0], firstMapTex);
        refs[1] = MapTextureRef(refs[1], firstMapTex);
    }

    param_1->geom_idx = NumXboxEntityGfxsCreated;
    EntityGfxStat_2adf20 += model->dataSize14 - model->dataSize18;
    NumXboxVtxsLoaded += model->vtxCnt;
    NumXboxEntityGfxsCreated++;
}
