#include "psiGraphics.h"

#include "Direct3D/d3dhelpers.h"
#include "Direct3D/d3dSeam.h" // d3dSetMatrix is reimplemented there now, not called through an AUTOGEN passthrough
#include "Woman.h"                // after headers.md: MemoryForWoman
#include "psiInput.h"             // after headers.md: controllerIsPresent, controller_maybeRumbleTimeout
#include "../memory.h"            // Mem_Free
#include "psiSave.h"              // after headers.md: psiInternalLoadingDataState
#include "../game.h"              // GameState, timestamp(), and (after headers.md) BackgroundMovieHashcode
#include <string.h>               // memset

// AUTOGEN
void RecurseAndDrawBoxes(int geom_idx);
// AUTOGEN
void psiCreateMapTextures(map_tag *mapptr);
// AUTOGEN
void psiCreateEntityGfx(celglist_tag *param_1,map_tag *param_2,uint param_3);


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

// Already AUTOGEN-declared in game.cpp (its stub body is generated from there): a plain forward declaration,
// because a second AUTOGEN would generate a colliding second body.
void maybeBackgroundMovieCleanup(void);

// AUTOGEN
void __stdcall maybeCleanupSystem(void);

// Set here and consumed by the first psiPreDraw after the reset: it calls ConfigureGammaForLevel, turns
// LevelLoadTime from the reset's timestamp into the load's duration, and zeroes TimeSpentLoadingFiles.
#define LevelStartPending (*(char*)0x002adf30)
// Set here and by psiCreateMapTextures/psiCreateEntityGfx; the next psiPreDraw does a WBINVD (writes the
// whole CPU cache back) so the GPU sees the freshly loaded texture and vertex data.
#define CacheFlushPending (*(char*)0x002adf31)
#define ScreenBlur (*(char*)0x002adf33)
#define TimeSpentLoadingFiles (*(double*)0x002adf38)
#define LevelLoadTime (*(double*)0x002adf40)
#define debug_scanmode (*(char*)0x001fec49)
#define debug_clipmode (*(char*)0x001fec4a)

// psiInternalLoadingDataState (0x002adf10) is psiSave.cpp's file-local define; headers.md moves it to psiSave.h.

// Level-scoped graphics bookkeeping, all reset here. The first index of Tex[] and d3dGeometryObjs[] is the
// default entry, so the counts restart at 1.
#define d3dGeometryObjs (*(void*(*)[2048])0x002a0e68) // ModelData*; no ModelData type in src/action yet
// XBE_GLOBAL(0x002ae4f8, 0x58)
static TextureInfo DefaultTextureInfo;
#define NumXboxEntityGfxsCreated (*(int*)0x00194808)
#define NumXboxTexLoaded (*(int*)0x0019480c)
#define FirstMapTexIdx (*(int*)0x00194810) // psiCreateMapTextures: NumXboxTexLoaded when the map's textures began
#define NumXboxVtxsLoaded (*(int*)0x002adf24)
#define EntityGfxStat_2adf20 (*(int*)0x002adf20) // psiCreateEntityGfx accumulates it; not yet identified

// Texture statistics psiCreateMapTextures keeps: a count and a byte total per texture format class.
#define someTypeTexCount (*(int*)0x002ade84)   // formatType 1
#define someTypeTexBytes (*(int*)0x002ade80)
#define someOtherTexCount (*(int*)0x002a0e60)  // formatType 0 or >5, 8 bits per pixel
#define someOtherTexBytes (*(int*)0x002adf04)
#define someTexCount (*(int*)0x002abe7c)       // formatType 0 or >5, other depths
#define someTexBytes (*(int*)0x002ade88)
#define someThirdTexCount (*(int*)0x002adf08)  // formatType 2..5
#define someThirdTexBytes (*(int*)0x002a0e64)

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
