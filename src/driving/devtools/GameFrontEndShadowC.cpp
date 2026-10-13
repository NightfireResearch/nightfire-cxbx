#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameFrontEndShadowC.h"
#include "FpControl.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <initializer_list>
#include <limits>
#include <vector>

#include "../audio/Bank.h"
#include "../audio/Index.h"
#include "../audio/Mix.h"
#include "../audio/Sound.h"
#include "../audio/Voice.h"
#include "../audio/ZoomObj.h"
#include "../eagl/EaglFont.h"
#include "../eagl/EaglGlobals.h"
#include "../eagl/Realgraph.h"
#include "../eagl/RenderContext.h"
#include "../eagl/RenderMethod.h"
#include "../eagl/Tar.h"
#include "../eagl/View.h"
#include "../engine/GameLoop.h"            // LaunchPage
#include "../engine/IOModule.hpp"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../game/Debris.h"
#include "../game/EffectsGallery.h"
#include "../game/Gallery.h"
#include "../game/Girl.h"
#include "../game/LoadingScreen.h"
#include "../platform/RealMemory.h"
#include "../platform/RealSystem.h"         // SyncTaskFn
#include "../render/Draw.h"
#include "../render/Materials.h"
#include "../render/Particles.h"
#include "../render/RGlareManager.hpp"
#include "../render/Renderer.h"
#include "../logging.h"                   // dbg_printf
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.frontend's package C, run once from the first simulation tick when
// NIGHTFIRE_GAMEFRONTENDSHADOWC is set. Each case runs the ORIGINAL (every entry of the package swapped back
// together) and then the PORT from the same state, and compares what they leave - the game's memory both use, the
// game's copies of the globals we own (the original's side) against ours, a fake arena both sides allocate from in
// the same order - and a log of every call into the fakes, with what each side reads back afterwards:
//
//   - GGallery's queries on the live HUD gallery: GET_Canvas (every canvas, in other cases, missing, no gallery),
//     GET_Puppet (every puppet, missing), GET_Key, Get_InterpolatedKey (the 0x22 bytes it sets; t out of range,
//     NaN), QUAD_Interpolate, TRANSFORM_Vertices, MGMT_MainShapeIndex (the texture files and random directories),
//     GET_SomeThing, FONT_Width and FONT_Height;
//   - its states and draws on a copy of the HUD's gallery with random keys and state (keepRenderState either way),
//     the screen 4:3 and 16:9 in each video mode: every SetState, FONT_Draw (all three), BITMAP_Draw2D,
//     LINE_Draw2DStrip, QUAD_Draw (both), QUAD_Draw2D, SPRITE_Draw2D (both; mirrored, repeated), SPRITE_Flush;
//     Construct, FONT_Load and Destruct of a second "hudNTSC.gal";
//   - GFXGallery on a synthetic gallery over the live effects: scripts of CANVAS_Add (every flag, the effects of
//     the mission's gallery, a fake scene object), FRAME_Draw (and so CANVAS_Draw, PARTICLE_Draw,
//     PARTICLE_GetSystem, PARTICLE_Add, GLARE_Draw, DEBRIS_Draw with fake systems, sounds that finish or not),
//     culling, the sticky entries, suppress, intensity, purges, CANVAS_HasStopped, PurgeAllEffects; GLARE_Draw and
//     DEBRIS_Draw on random keys and generator states; GET_Keys; Construct and Destruct;
//   - GGirl: DoGirl sequences from nothing (the textures' pixels hashed) under a fake clock that steps, jumps and
//     wraps; KillGirl; DrawTexture; GGirlieMaterial's Construct and Delete through them; FUN_000d7bd0;
//   - GLoadingScreen: InitLoadDots, Update under a fake clock, DrawDots, InitLoadScreen and DrawBackground (the
//     first mission's picture and default.ini), ClearToBlack, Shutdown, the unloading screen, the sync task calls,
//     Status; the string list's helpers on a synthetic list.
//
// Fakes (5-byte jumps over our implementations, which both sides reach): Draw's sprites, quads, batches and blend
// mode, FONT_drawtextfa, the simple materials' Draw, the render context's and view port's frame calls,
// EnableAlphaWrites, AddGlare, RDebris::Add, the particle library's AddSystem and the manager's AddOrRefresh,
// UMemory's FastAlloc, FastFree, Alloc, Free and Size, operator new and delete, ABaseSound::OperatorNew,
// ASound::Construct, AMix::Add and Remove, AIndex::Lookup, AVoice::Set, TIMER_gettick, TIMER_getfrequency,
// SYNCTASK_add and _del, and the log (dbg_printf); EaglMalloc and EaglFree are pointed at the arena for the figure
// and the loading screen.
//
// Mutations the test sees: SPRITE_Draw2D's mirrored-in-x branch drawn at x rather than x - width (the sprite fake
// logs a different x); GGirl::UpdateTexture's alpha step 27 made 28 (every lit pixel differs, so the texture's
// hash); GFXGallery::CULL_SetCanvas's kFlag8000 test dropped (the canvases' flags differ after a culling step).
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

const unsigned kEntries[] = {
    0x000d3470, 0x000d3840, 0x000d3850, 0x000d38f0, 0x000d39d0, 0x000d39f0, 0x000d3aa0, 0x000d3ab0, 0x000d3b30,
    0x000d3bc0, 0x000d3c30, 0x000d3ce0, 0x000d3d90, 0x000d3ed0, 0x000d4010, 0x000d4030, 0x000d4050, 0x000d4160,
    0x000d42a0, 0x000d43a0, 0x000d44b0, 0x000d45f0, 0x000d47a0, 0x000d4b30, 0x000d4b90, 0x000d4f90, 0x000d5170,
    0x000d51e0, 0x000d55f0, 0x000d57a0, 0x000d5800, 0x000d5810, 0x000d58f0, 0x000d5910, 0x000d59c0, 0x000d5a00,
    0x000d5a80, 0x000d5ad0, 0x000d5b00, 0x000d5b30, 0x000d5b70, 0x000d5c60, 0x000d5d20, 0x000d5f50, 0x000d61e0,
    0x000d6200, 0x000d63a0, 0x000d6520, 0x000d66a0, 0x000d6750, 0x000d68b0, 0x000d68c0, 0x000d69a0, 0x000d6d70,
    0x000d6da0, 0x000d6e30, 0x000d6eb0, 0x000d6f20, 0x000d6f60, 0x000d7690, 0x000d7770, 0x000d7990, 0x000d7a50,
    0x000d7b40, 0x000d7bb0, 0x000d7bd0, 0x000e1f90, 0x000e2110, 0x000e2390, 0x000e2610, 0x000e2710, 0x000e2830,
    0x000e28a0, 0x000e2a70, 0x000e2b60, 0x000e2b70, 0x000e2b90, 0x000e2ba0, 0x000e2c20, 0x000e2c40, 0x000e2c80,
    0x000e2ff0,
};

// GGallery
#define Orig_GetGallery ((UGroup *(__fastcall *)(GGallery *, int))0x000d5800)
#define Orig_MainShape ((int (__fastcall *)(GGallery *, int, const ShapeFile *))0x000d57a0)
#define Orig_GetSomeThing ((void *(__fastcall *)(GGallery *, int, int))0x000d59c0)
#define Orig_GetCanvas ((UGroup *(__fastcall *)(GGallery *, int, UGroup *, const char *))0x000d3850)
#define Orig_GetPuppet ((GalleryKey *(__fastcall *)(GGallery *, int, UGroup *, const char *, int, int))0x000d5810)
#define Orig_GetKey ((GalleryKey *(__fastcall *)(GGallery *, int, GalleryKey *, int))0x000d58f0)
#define Orig_Interpolate ((void (__fastcall *)(GGallery *, int, GalleryKey *, const GalleryKey *, const GalleryKey *, float))0x000d5910)
#define Orig_QuadInterpolate ((void (__fastcall *)(GGallery *, int, const GalleryQuad *, const GalleryQuad *, GalleryQuad *, float))0x000d5b70)
#define Orig_Transform ((void (__fastcall *)(GGallery *, int, Coord4 *, int, const Coord4 *, const Coord4 *, const float *))0x000d5c60)
#define Orig_SpriteSetState ((void (__fastcall *)(GGallery *, int, const GalleryKey *))0x000d5a00)
#define Orig_QuadSetState ((void (__fastcall *)(GGallery *, int, const GalleryKey *))0x000d5b30)
#define Orig_FontSetState ((void (__fastcall *)(GGallery *, int, const GalleryKey *))0x000d6eb0)
#define Orig_FontSetScale ((void (__fastcall *)(GGallery *, int, float, float))0x000d61e0)
#define Orig_FontLoad ((void (__fastcall *)(GGallery *, int, const char *))0x000d5a80)
#define Orig_FontWidth ((float (__fastcall *)(GGallery *, int, const char *))0x000d5ad0)
#define Orig_FontHeight ((float (__fastcall *)(GGallery *, int, const char *))0x000d5b00)
#define Orig_FontDrawKey ((void (__fastcall *)(GGallery *, int, const GalleryText *, const char *))0x000d6200)
#define Orig_FontDrawColour ((void (__fastcall *)(GGallery *, int, const char *, float, float, uint32_t))0x000d63a0)
#define Orig_FontDraw ((void (__fastcall *)(GGallery *, int, const char *, float, float))0x000d6520)
#define Orig_BitmapDraw ((void (__fastcall *)(GGallery *, int, const GalleryKey *))0x000d5f50)
#define Orig_LineDraw ((void (__fastcall *)(GGallery *, int, const Coord4 *, const uint32_t *, int))0x000d66a0)
#define Orig_QuadDraw ((void (__fastcall *)(GGallery *, int, const GalleryQuad *, float))0x000d6750)
#define Orig_QuadDrawKey ((void (__fastcall *)(GGallery *, int, const GalleryKey *, float))0x000d68b0)
#define Orig_QuadDraw2D ((void (__fastcall *)(GGallery *, int, float, float, float, float, uint32_t))0x000d68c0)
#define Orig_SpriteDraw ((void (__fastcall *)(GGallery *, int, int, int, const GalleryKey *, const GallerySprite *))0x000d69a0)
#define Orig_SpriteDrawKey ((void (__fastcall *)(GGallery *, int, const GalleryKey *))0x000d6d70)
#define Orig_SpriteFlush ((void (__fastcall *)(GGallery *, int))0x000d6da0)
#define Orig_GalleryConstruct ((GGallery *(__fastcall *)(GGallery *, int, const char *))0x000d5d20)
#define Orig_GalleryDestruct ((void (__fastcall *)(GGallery *, int))0x000d6e30)
#define Orig_CullSetCanvasStatic ((void (*)(int, bool, float))0x000d3470)

// GFXGallery
#define Orig_FxConstruct ((GFXGallery *(__fastcall *)(GFXGallery *, int, const char *))0x000d44b0)
#define Orig_FxDestruct ((void (__fastcall *)(GFXGallery *, int))0x000d5170)
#define Orig_FxGetGallery ((UGroup *(__fastcall *)(GFXGallery *, int))0x000d3840)
#define Orig_FxPurgeAll ((void (__fastcall *)(GFXGallery *, int))0x000d45f0)
#define Orig_CanvasAdd ((int (__fastcall *)(GFXGallery *, int, UGroup *, RSceneObj *, int, const Coord4 *, const Coord4 *, const Coord3 *, const Coord4 *, const Coord3 *, bool, bool, bool))0x000d47a0)
#define Orig_CanvasPurge ((void (__fastcall *)(GFXGallery *, int, int))0x000d4010)
#define Orig_CanvasSuppress ((void (__fastcall *)(GFXGallery *, int, int))0x000d4030)
#define Orig_CanvasSetIntensity ((void (__fastcall *)(GFXGallery *, int, int, float))0x000d4050)
#define Orig_CanvasPurgeSceneObj ((void (__fastcall *)(GFXGallery *, int, int))0x000d3ed0)
#define Orig_CanvasPurgeObjEffects ((void (__fastcall *)(GFXGallery *, int, RSceneObj *))0x000d4b30)
#define Orig_CanvasHasStopped ((bool (__fastcall *)(GFXGallery *, int, GFXGallery::Canvas *))0x000d3d90)
#define Orig_CullStart ((void (__fastcall *)(GFXGallery *, int))0x000d3aa0)
#define Orig_CullNext ((int (__fastcall *)(GFXGallery *, int, Coord4 *, float *, bool *, float *))0x000d3ab0)
#define Orig_CullSet ((void (__fastcall *)(GFXGallery *, int, int, bool, float))0x000d3b30)
#define Orig_StickyNew ((int (__fastcall *)(GFXGallery *, int, int))0x000d3bc0)
#define Orig_StickyUpdate ((void (__fastcall *)(GFXGallery *, int))0x000d3c30)
#define Orig_StickyPos ((bool (__fastcall *)(GFXGallery *, int, unsigned, Coord4 *))0x000d3ce0)
#define Orig_PosQuat ((bool (__fastcall *)(GFXGallery *, int, int, Coord4 *, Coord4 *))0x000d39f0)
#define Orig_GetKeys ((void (__fastcall *)(GFXGallery *, int, GalleryKey *, int, int, GalleryKey **, int *, GalleryKey **, int *))0x000d38f0)
#define Orig_FirstIdleKey ((GalleryKey *(__fastcall *)(GFXGallery *, int, UGroup *))0x000d39d0)
#define Orig_FrameDraw ((void (__fastcall *)(GFXGallery *, int))0x000d55f0)
#define Orig_ParticleSystem ((uint32_t (__fastcall *)(GFXGallery *, int, UGroup *, const RParticleSystemData *))0x000d42a0)
#define Orig_SoundAdd ((void (__fastcall *)(GFXGallery *, int, UGroup *, const GallerySound *, ASound *, const Coord4 *, const Coord3 *, int))0x000d43a0)
#define Orig_GlareDraw ((void (__fastcall *)(GFXGallery *, int, UGroup *, const GalleryGlare *, const Coord4 *))0x000d4160)
#define Orig_DebrisDraw ((void (__fastcall *)(GFXGallery *, int, const GalleryKey *, const Coord4 *, const Coord4 *, const Coord3 *, int))0x000d4b90)

// GGirl
#define Orig_KillGirl ((void (*)(void))0x000d6f20)
#define Orig_DoGirl ((void (*)(void))0x000d7a50)
#define Orig_DrawTexture ((void (*)(int, int))0x000d7690)
#define Orig_EndState ((bool (__fastcall *)(LaunchPage *, int))0x000d7bd0)

// GLoadingScreen
#define Orig_InitLoadDots ((void (*)(bool))0x000e1f90)
#define Orig_InitLoadScreen ((void (*)(const char *, const char *))0x000e2390)
#define Orig_DrawBackground ((void (*)(void))0x000e2610)
#define Orig_DrawDots ((void (*)(void))0x000e2710)
#define Orig_ClearToBlack ((void (*)(void))0x000e2830)
#define Orig_Update ((void (*)(void))0x000e28a0)
#define Orig_Shutdown ((void (*)(void))0x000e2a70)
#define Orig_TaskCallback ((unsigned (*)(int, int))0x000e2b60)
#define Orig_AddTask ((void (*)(void))0x000e2b70)
#define Orig_DeleteTask ((void (*)(void))0x000e2b90)
#define Orig_Unloading ((void (*)(void))0x000e2110)
#define Orig_FreeUnloading ((void (*)(void))0x000e2ba0)
#define Orig_Status ((void (*)(const char *, ...))0x000e2ff0)
#define Orig_NewHead ((LoadScreenStringNode *(__fastcall *)(LoadScreenStringList *, int))0x000e2c20)
#define Orig_Tidy ((void (__fastcall *)(LoadScreenStringNode *, int))0x000e2c40)
#define Orig_Erase ((LoadScreenStringNode **(__fastcall *)(LoadScreenStringList *, int, LoadScreenStringNode **, LoadScreenStringNode *, LoadScreenStringNode *))0x000e2c80)

// The game's code the cases need, not ours
#define GHud_TheApp ((uint8_t *(*)())0x000d7c10)
#define GSystem_GetGalleryName ((const char *(*)())0x000e3e20)
#define GSystem_LOCALE_Symbol ((const char *(*)(int))0x000e3af0)

// ---- the callees faked (their entries jump to ours)

const uint32_t kDrawSprite = 0x00075f60;
const uint32_t kDrawSpriteWhole = 0x00076250;
const uint32_t kDrawQuadC = 0x000761c0;
const uint32_t kDrawQuad = 0x000762d0;
const uint32_t kNormalBlend = 0x00076200;
const uint32_t kBatchedQuad = 0x00076330;
const uint32_t kBatchedSprite = 0x000765e0;
const uint32_t kDrawText = 0x001073a0;
const uint32_t kSimpleDraw = 0x0011bbf0;
const uint32_t kTexturedDraw = 0x0011bd50;
const uint32_t kBeginFrame = 0x000e6610;
const uint32_t kEndFrame = 0x000e6640;
const uint32_t kBeginView = 0x000e4be0;
const uint32_t kEndView = 0x000e49a0;
const uint32_t kClearView = 0x000e49d0;
const uint32_t kViewMatrix = 0x000e4ef0;
const uint32_t kBackground = 0x000f3970;
const uint32_t kAlphaWrites = 0x0007d0b0;
const uint32_t kAddGlare = 0x000a9c10;
const uint32_t kAddDebris = 0x000a9260;
const uint32_t kAddSystem = 0x000a2d10;
const uint32_t kAddOrRefresh = 0x000a2bf0;
const uint32_t kFastAlloc = 0x00114750;
const uint32_t kFastFree = 0x001147d0;
const uint32_t kAlloc = 0x00114470;
const uint32_t kFree = 0x001144b0;
const uint32_t kSize = 0x001143f0;
const uint32_t kNew = 0x001146a0;
const uint32_t kDelete = 0x001146e0;
const uint32_t kNewArray = 0x00114710;
const uint32_t kSoundNew = 0x0011c910;
const uint32_t kSoundConstruct = 0x00127bf0;
const uint32_t kMixAdd = 0x0011d6a0;
const uint32_t kMixRemove = 0x0011d750;
const uint32_t kIndexLookup = 0x00126d40;
const uint32_t kVoiceSet = 0x00123870;
const uint32_t kGetTick = 0x0010a6c0;
const uint32_t kGetFrequency = 0x0010a6d0;
const uint32_t kTaskAdd = 0x0010aa50;
const uint32_t kTaskDel = 0x0010abc0;
const uint32_t kPrint = 0x000e2e30;

// ---- the game's state

#define ShadowActiveViews U32_AT(0x00243940)
#define ShadowSimTimeStep FLOAT_AT(0x00234e30)
#define ShadowDotsFile 0x001caa00
#define ShadowEaglMalloc U32_AT(0x001caf68)
#define ShadowEaglFree U32_AT(0x001caf6c)

// The owned globals' game copies, by name
const uint32_t kGameLastParticleSystem = 0x0023f328;
const uint32_t kGameEffectsGallery = 0x0023f320;
const uint32_t kGameLineMaterial = 0x0023f330;
const uint32_t kGameLineMaterialMade = 0x0023f390;
const uint32_t kGameGfxRandomState = 0x001caf34;
const uint32_t kGameGfxRandomMultiplier = 0x001caf38;
const uint32_t kGameLoadState = 0x0023f574;
const uint32_t kGameGirlMaterials = 0x0023f398, kGameGirlCurrent = 0x0023f3a0, kGameGirlStream = 0x0023f3a4,
               kGameGirlActive = 0x0023f3ac, kGameGirlUnknownB0 = 0x0023f3b0, kGameGirlFps = 0x0023f3bc,
               kGamePaletteX = 0x0023f3c0, kGamePaletteY = 0x0023f3c4, kGamePaletteSpeedX = 0x0023f3c8,
               kGamePaletteSpeedY = 0x001ca3f0, kGamePaletteX2 = 0x0023f3cc, kGamePaletteY2 = 0x0023f3d0,
               kGamePaletteSpeedX2 = 0x0023f3d4, kGamePaletteSpeedY2 = 0x001ca3f4, kGameGirlUnmirrored = 0x0023f3e4,
               kGameGirlFrameStart = 0x0023f3e8, kGameGirlCursor = 0x0023f3ec, kGameGirlTime = 0x0023f3f0,
               kGameGirlTick = 0x0023f3f4, kGameGirlData = 0x0023f3f8, kGameGirlFrames = 0x0023f3fc,
               kGameGirlFrame = 0x0023f400;
const uint32_t kGameKeepLoadScreen = 0x001caee0, kGameDotDirection = 0x001caee4, kGameBackgroundFile = 0x0023f570,
               kGameLoadingFont = 0x0023f578, kGameLoadingFontFile = 0x0023f57c, kGameTitleId = 0x0023f580,
               kGameLoadScale = 0x0023f590, kGameDots = 0x0023f594, kGameDotTime = 0x0023f598,
               kGameLastTick = 0x0023f59c, kGameLitDots = 0x0023f5a0, kGameDotCount = 0x0023f5a4,
               kGameBackgroundCount = 0x0023f5a8, kGameUnloadingFile = 0x0023f5ac, kGameDotTextures = 0x0023f5b0,
               kGameBackgroundTextures = 0x0023f5f0, kGameUnloadingTexture = 0x0023f7f0,
               kGameErrorFontFile = 0x0023f7f4, kGameErrorFont = 0x0023f7f8;

}  // namespace

namespace {

// ---- results

bool g_original;
long g_cases, g_checks, g_differ, g_faults, g_bothFaulted, g_skipped;
int g_reported;
bool g_verbose;     // NIGHTFIRE_GAMEFRONTENDSHADOWC=2: each case named before it runs
unsigned g_x87, g_sse;

void Report(const char *what, long index, const char *detail) {
    g_differ++;
    if (g_reported++ < 40)
        printf("[gamefrontendshadowc]   %s #%ld: %s\n", what, index, detail);
}

void CompareBytes(const char *what, long index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    char detail[112];
    snprintf(detail, sizeof(detail), "byte 0x%x of 0x%x: original %02x, port %02x", unsigned(at), unsigned(bytes),
             x[at], y[at]);
    Report(what, index, detail);
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

// ---- random inputs (a generator of our own)

uint32_t g_seed = 0x3c1f9e27;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f;
}

int Range(int lo, int hi) {
    return lo + int(Next() % uint32_t(hi - lo + 1));
}

bool Chance(int percent) {
    return Range(0, 99) < percent;
}

Coord4 RandomQuaternion() {
    for (;;) {
        Coord4 q = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f) };
        float length = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (length > 0.1f) {
            q.x /= length;
            q.y /= length;
            q.z /= length;
            q.w /= length;
            return q;
        }
    }
}

// An awkward float now and then: NaN, an infinity, a huge or tiny value, -0
float Awkward(float usual) {
    switch (Range(0, 40)) {
    case 0: return std::numeric_limits<float>::quiet_NaN();
    case 1: return std::numeric_limits<float>::infinity();
    case 2: return -1.0e30f;
    case 3: return 3.0e-39f;
    case 4: return -0.0f;
    default: return usual;
    }
}

// ---- the fake arena both sides allocate from, reset before each side

const uint32_t kArenaBytes = 0x100000;
alignas(16) uint8_t g_arena[kArenaBytes];
uint32_t g_arenaUsed;
uint32_t g_arenaHigh;
long g_arenaFull;          // allocations the arena had no room for
struct ArenaBlock {
    uint32_t offset, size;
};
std::vector<ArenaBlock> g_arenaBlocks;

bool InArena(const void *p) {
    return p >= g_arena && p < g_arena + kArenaBytes;
}

uint32_t ArenaWord(const void *p) {
    return InArena(p) ? 0xa0000000u | uint32_t(static_cast<const uint8_t *>(p) - g_arena) : uint32_t(uintptr_t(p));
}

void *Bump(uint32_t size) {
    uint32_t rounded = (size + 15) & ~15u;
    if (g_arenaUsed + rounded > kArenaBytes) {
        g_arenaFull++;
        return NULL;
    }
    uint8_t *p = g_arena + g_arenaUsed;
    memset(p, 0, rounded);
    g_arenaBlocks.push_back({ g_arenaUsed, size });
    g_arenaUsed += rounded;
    if (g_arenaUsed > g_arenaHigh)
        g_arenaHigh = g_arenaUsed;
    return p;
}

void ResetArena() {
    memset(g_arena, 0xcd, g_arenaHigh);
    g_arenaUsed = 0;
    g_arenaBlocks.clear();
}

// ---- the regions a case may change: the game's addresses both sides use, and our globals with their game copies

struct Region {
    uint8_t *game;
    uint8_t *ours;          // NULL: both sides use the game's address
    uint32_t size;
    bool compare;
    const char *name;
};

std::vector<Region> g_regions;

void AddShared(const void *game, uint32_t size, const char *name, bool compare = true) {
    if (game != NULL)
        g_regions.push_back({ (uint8_t *)game, NULL, size, compare, name });
}

void AddOwned(void *ours, uint32_t game, uint32_t size, const char *name, bool compare = true) {
    g_regions.push_back({ reinterpret_cast<uint8_t *>(uintptr_t(game)), static_cast<uint8_t *>(ours), size, compare,
                          name });
}

// An address in one of our globals as the same byte of the game's copy; arena addresses by offset
uint32_t Canon(const void *pointer) {
    const uint8_t *p = static_cast<const uint8_t *>(pointer);
    for (const Region &r : g_regions)
        if (r.ours != NULL && p >= r.ours && p < r.ours + r.size)
            return uint32_t(uintptr_t(r.game + (p - r.ours)));
    return ArenaWord(pointer);
}

struct Snapshot {
    std::vector<uint8_t> bytes;
};

void Capture(Snapshot *s) {
    s->bytes.clear();
    for (const Region &r : g_regions) {
        s->bytes.insert(s->bytes.end(), r.game, r.game + r.size);
        if (r.ours != NULL)
            s->bytes.insert(s->bytes.end(), r.ours, r.ours + r.size);
    }
}

void Restore(const Snapshot &s) {
    size_t at = 0;
    for (const Region &r : g_regions) {
        memcpy(r.game, &s.bytes[at], r.size);
        at += r.size;
        if (r.ours != NULL) {
            memcpy(r.ours, &s.bytes[at], r.size);
            at += r.size;
        }
    }
}

// Our globals into the game's copies, so an original starts where the port does
void Lend() {
    for (const Region &r : g_regions)
        if (r.ours != NULL)
            memcpy(r.game, r.ours, r.size);
}

void CompareResults(const char *what, long index, const Snapshot &original, const Snapshot &port) {
    size_t at = 0;
    for (const Region &r : g_regions) {
        const uint8_t *a = &original.bytes[at];
        const uint8_t *b = r.ours != NULL ? &port.bytes[at + r.size] : &port.bytes[at];
        at += r.ours != NULL ? 2 * r.size : r.size;
        if (!r.compare)
            continue;
        char label[96];
        snprintf(label, sizeof(label), "%s: %s", what, r.name);
        CompareBytes(label, index, a, b, r.size);
    }
}

// ---- the call logs (and what each side reads back after its run)

std::vector<uint32_t> g_logs[2];

void Log(uint32_t word) {
    g_logs[g_original ? 0 : 1].push_back(word);
}

void LogFloat(float value) {
    uint32_t word;
    memcpy(&word, &value, 4);
    Log(word);
}

void LogBytes(const void *bytes, uint32_t size) {
    if (bytes == NULL) {
        Log(0xdeadbeef);
        return;
    }
    const uint8_t *p = static_cast<const uint8_t *>(bytes);
    for (uint32_t i = 0; i < size; i += 4) {
        uint32_t word = 0;
        memcpy(&word, p + i, size - i < 4 ? size - i : 4);
        Log(word);
    }
}

void LogXyz(const void *v) {
    if (v == NULL) {
        Log(0xdeadbeef);
        return;
    }
    LogBytes(v, 12);
}

uint32_t Hash(const void *bytes, size_t size) {
    uint32_t hash = 2166136261u;
    const uint8_t *p = static_cast<const uint8_t *>(bytes);
    for (size_t i = 0; i < size; i++)
        hash = (hash ^ p[i]) * 16777619u;
    return hash;
}

void LogString(const char *text) {
    Log(text == NULL ? 0 : Hash(text, strlen(text)));
}

void CompareLogs(const char *what, long index) {
    g_checks++;
    const std::vector<uint32_t> &a = g_logs[0], &b = g_logs[1];
    if (a == b)
        return;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at])
        at++;
    char detail[128];
    snprintf(detail, sizeof(detail), "call log word %u of %u/%u: original %08x, port %08x", unsigned(at),
             unsigned(a.size()), unsigned(b.size()), at < a.size() ? a[at] : 0, at < b.size() ? b[at] : 0);
    Report(what, index, detail);
    if (getenv("NIGHTFIRE_GFEC_WINDOW") != NULL) {
        for (size_t i = at > 24 ? at - 24 : 0; i < at + 8 && (i < a.size() || i < b.size()); i++)
            printf("    %5u: %08x %08x\n", unsigned(i), i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
    }
}

// A global of ours or its game copy, on the side running
template <class T> T &Side(T &ours, uint32_t game) {
    return g_original ? *reinterpret_cast<T *>(uintptr_t(game)) : ours;
}

// ---- 5-byte jumps over our implementations

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};

uint32_t JumpTarget(uint32_t at) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    if (code[0] != 0xe9)
        return 0;
    int32_t relative;
    memcpy(&relative, code + 1, 4);
    return at + 5 + uint32_t(relative);
}

bool HookInstall(Hook *hook, uint32_t entry, const void *to) {
    hook->on = false;
    uint32_t at = JumpTarget(entry);
    if (at == 0)
        return false;
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    for (int i = 0; i < 5; i++)
        if (code[i] == 0xc3 || code[i] == 0xc2 || code[i] == 0xcc)     // too short to take a jump
            return false;
    hook->at = at;
    DWORD old;
    if (!VirtualProtect((void *)uintptr_t(at), 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(hook->saved, (void *)uintptr_t(at), 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t relative = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &relative, 4);
    memcpy((void *)uintptr_t(at), jump, 5);
    VirtualProtect((void *)uintptr_t(at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(at), 5);
    hook->on = true;
    return true;
}

void HookRemove(Hook *hook) {
    if (!hook->on)
        return;
    DWORD old;
    VirtualProtect((void *)uintptr_t(hook->at), 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)uintptr_t(hook->at), hook->saved, 5);
    VirtualProtect((void *)uintptr_t(hook->at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(hook->at), 5);
    hook->on = false;
}

// A function pointer of ours as the game's entry that jumps to it
uint32_t CanonCode(const void *function) {
    for (unsigned at : kEntries)
        if (JumpTarget(at) == uint32_t(uintptr_t(function)))
            return at;
    return uint32_t(uintptr_t(function));
}

// ---- the fakes

enum : uint32_t {
    kTagSprite = 0x53505254, kTagSpriteWhole, kTagQuadC, kTagQuad, kTagBlend, kTagBatchQuad, kTagBatchSprite,
    kTagText, kTagSimpleDraw, kTagTexturedDraw, kTagFrame, kTagView, kTagAlpha, kTagGlare, kTagDebris, kTagSystem,
    kTagRefresh, kTagFastAlloc, kTagFastFree, kTagAlloc, kTagFree, kTagNew, kTagDelete, kTagSoundNew, kTagSound,
    kTagMix, kTagLookup, kTagVoice, kTagSoundDelete, kTagTask, kTagPrint, kTagEagl,
};

const uint32_t kMaxLoggedVertices = 64;

// The inputs the fakes answer with, set per case
struct FakeInputs {
    int32_t ticks[64];
    int tickCount;
    int tickIndex;
    int32_t frequency;
    uint8_t soundFinished;      // a bit per view
    uint8_t systemsActive;      // the fake systems' active, by their order
    uint32_t systemFlags;       // the fake systems' types' flags, a bit 1 per system by order
} g_fake;

void FakeDrawSprite(float x, float y, float width, float height, uint32_t colour, const SpriteTexCoords *texCoords,
                    EAGL::TAR *texture) {
    Log(kTagSprite);
    LogFloat(x);
    LogFloat(y);
    LogFloat(width);
    LogFloat(height);
    Log(colour);
    LogBytes(texCoords, texCoords != NULL ? sizeof(SpriteTexCoords) : 0);
    Log(Canon(texture));
}

void FakeDrawSpriteWhole(float x, float y, float width, float height, uint32_t colour, EAGL::TAR *texture) {
    Log(kTagSpriteWhole);
    LogFloat(x);
    LogFloat(y);
    LogFloat(width);
    LogFloat(height);
    Log(colour);
    Log(Canon(texture));
}

void FakeDrawQuadC(const Coord4 *positions, const uint32_t *colours) {
    Log(kTagQuadC);
    LogBytes(positions, 4 * sizeof(Coord4));
    LogBytes(colours, 16);
}

void FakeDrawQuad(const Coord4 *positions, uint32_t colour) {
    Log(kTagQuad);
    LogBytes(positions, 4 * sizeof(Coord4));
    Log(colour);
}

void FakeNormalBlend(EAGL::GeoPrimState *material) {
    Log(kTagBlend);
    Log(Canon(material));
}

void FakeBatchedQuad(const Coord4 *p0, const Coord4 *p1, const Coord4 *p2, const Coord4 *p3, uint32_t colour,
                     const Coord4 *texCoords, EAGL::TAR *texture) {
    Log(kTagBatchQuad);
    LogBytes(p0, p0 != NULL ? sizeof(Coord4) : 0);
    LogBytes(p1, p1 != NULL ? sizeof(Coord4) : 0);
    LogBytes(p2, p2 != NULL ? sizeof(Coord4) : 0);
    LogBytes(p3, p3 != NULL ? sizeof(Coord4) : 0);
    Log(colour);
    LogBytes(texCoords, texCoords != NULL ? 4 * sizeof(Coord4) : 0);
    Log(Canon(texture));
}

void FakeBatchedSprite(float x, float y, float width, float height, uint32_t colour, const SpriteTexCoords *texCoords,
                       EAGL::TAR *texture) {
    Log(kTagBatchSprite);
    LogFloat(x);
    LogFloat(y);
    LogFloat(width);
    LogFloat(height);
    Log(colour);
    LogBytes(texCoords, texCoords != NULL ? sizeof(SpriteTexCoords) : 0);
    Log(Canon(texture));
}

void FakeDrawText(const uint8_t *font, float x, float y, const char *format, ...) {
    Log(kTagText);
    const FNTXFont *f = reinterpret_cast<const FNTXFont *>(font);
    LogFloat(f->scaleX);
    LogFloat(f->scaleY);
    Log(f->colour);
    LogFloat(x);
    LogFloat(y);
    LogString(format);
}

void __fastcall FakeSimpleDraw(USimpleMaterial *material, int, int primitive, int count, MATRIX4 *transform) {
    Log(kTagSimpleDraw);
    Log(Canon(material));
    Log(uint32_t(primitive));
    Log(uint32_t(count));
    Log(transform != NULL);
    const SimpleGeoPrim *request = SimpleRequests[SimpleRequestIndex];
    uint32_t n = uint32_t(count) > kMaxLoggedVertices ? kMaxLoggedVertices : uint32_t(count);
    if (request != NULL) {
        LogBytes(request->positions.data, n * sizeof(Coord4));
        LogBytes(request->colours.data, n * 4);
    }
}

void __fastcall FakeTexturedDraw(USimpleTexturedMaterial *material, int, int primitive, int count,
                                 MATRIX4 *transform) {
    Log(kTagTexturedDraw);
    Log(Canon(material));
    Log(uint32_t(primitive));
    Log(uint32_t(count));
    Log(transform != NULL);
    const TexturedGeoPrim *request = TexturedRequests[TexturedRequestIndex];
    uint32_t n = uint32_t(count) > kMaxLoggedVertices ? kMaxLoggedVertices : uint32_t(count);
    if (request != NULL) {
        Log(Canon(request->texture.data));
        LogBytes(request->positions.data, n * sizeof(Coord4));
        LogBytes(request->colours.data, n * 4);
        LogBytes(request->texCoords.data, n * sizeof(Coord4));
    }
}

void __fastcall FakeBeginFrame(EAGL::RenderContext *context, int) {
    Log(kTagFrame);
    Log(uint32_t(uintptr_t(context)));
}

void *__fastcall FakeEndFrame(EAGL::RenderContext *context, int) {
    Log(kTagFrame + 0x100);
    Log(uint32_t(uintptr_t(context)));
    return NULL;
}

void __fastcall FakeBeginView(EAGL::ViewPort *view, int) {
    Log(kTagView);
    Log(uint32_t(uintptr_t(view)));
}

void __fastcall FakeEndView(EAGL::ViewPort *view, int) {
    Log(kTagView + 0x100);
    Log(uint32_t(uintptr_t(view)));
}

void __fastcall FakeClearView(EAGL::ViewPort *view, int, uint32_t flags) {
    Log(kTagView + 0x200);
    Log(uint32_t(uintptr_t(view)));
    Log(flags);
}

void __fastcall FakeViewMatrix(EAGL::ViewPort *view, int, const float *matrix) {
    Log(kTagView + 0x300);
    Log(uint32_t(uintptr_t(view)));
    LogBytes(matrix, 64);
}

void __fastcall FakeBackground(EAGL::ViewPort *view, int, uint32_t colour) {
    Log(kTagView + 0x400);
    Log(uint32_t(uintptr_t(view)));
    Log(colour);
}

bool __fastcall FakeAlphaWrites(RRenderer *renderer, int) {
    Log(kTagAlpha);
    Log(uint32_t(uintptr_t(renderer)));
    return true;
}

void __fastcall FakeAddGlare(RGlareManager *manager, int, Glare *glare, int unused) {
    Log(kTagGlare);
    Log(uint32_t(uintptr_t(manager)));
    LogBytes(glare, 0x16);                  // position, w, flags and the two bytes after them
    LogBytes(&glare->type, 8);
    Log(uint32_t(unused));
}

void __fastcall FakeAddDebris(RDebris *debris, int, int kind, const Coord4 *position, const Coord4 *velocity,
                              uint32_t colour, float gravity, uint32_t lifetime) {
    Log(kTagDebris);
    Log(uint32_t(uintptr_t(debris)));
    Log(uint32_t(kind));
    LogBytes(position, sizeof(Coord4));
    LogXyz(velocity);
    Log(colour);
    LogFloat(gravity);
    Log(lifetime);
}

int g_systemsAdded;

int __fastcall FakeAddSystem(RParticleLibrary *library, int, RParticleSystemData *data, uint32_t tag) {
    Log(kTagSystem);
    Log(uint32_t(uintptr_t(library)));
    LogBytes(data, sizeof(RParticleSystemData));
    Log(tag);
    return 100 + g_systemsAdded++;
}

struct FakeSystemEntry {
    uint32_t key;
    RParticleSystem *system;
};
std::vector<FakeSystemEntry> g_systems;
RParticleSystem *g_systemHolder;

RParticleSystem **__fastcall FakeAddOrRefresh(RParticleSystemManager *manager, int, uint32_t key,
                                              RParticleEmitter *emitter, RParticleSystem::RParticleCreator *creator,
                                              float unknown34) {
    Log(kTagRefresh);
    Log(uint32_t(uintptr_t(manager)));
    Log(ArenaWord(reinterpret_cast<const void *>(uintptr_t(key))));
    Log(ArenaWord(emitter));
    Log(uint32_t(uintptr_t(creator != NULL ? creator->vtable : NULL)));
    LogFloat(unknown34);
    for (const FakeSystemEntry &entry : g_systems) {
        if (entry.key == key) {
            g_systemHolder = entry.system;
            return &g_systemHolder;
        }
    }
    int order = int(g_systems.size());
    RParticleSystem *system = static_cast<RParticleSystem *>(Bump(sizeof(RParticleSystem)));
    RParticleSystemData *data = static_cast<RParticleSystemData *>(Bump(sizeof(RParticleSystemData)));
    if (system == NULL || data == NULL)
        return &g_systemHolder;
    data->flags = (g_fake.systemFlags >> (order & 31)) & 1 ? kParticleFlag02 : 0;
    system->data = data;
    system->emitter = emitter;
    system->active = (g_fake.systemsActive >> (order & 7)) & 1;
    g_systems.push_back({ key, system });
    g_systemHolder = system;
    return &g_systemHolder;
}

void *FakeFastAlloc(unsigned size, const char *name) {
    Log(kTagFastAlloc);
    Log(size);
    LogString(name);
    return Bump(size);
}

void FakeFastFree(void *block, unsigned size) {
    Log(kTagFastFree);
    Log(ArenaWord(block));
    Log(size);
}

void *FakeAlloc(unsigned size, unsigned flags, const char *name) {
    Log(kTagAlloc);
    Log(size);
    Log(flags);
    LogString(name);
    return Bump(size);
}

// Blocks outside the arena (files the cases load) go back to the allocator as UMemory::Free would
void FakeFree(void *block) {
    Log(kTagFree);
    Log(InArena(block) ? ArenaWord(block) : 1);
    if (!InArena(block) && block != NULL) {
        memset(block, 0, MEM_size(block));
        MEM_free(block);
    }
}

size_t FakeSize(void *block) {
    if (InArena(block)) {
        uint32_t offset = uint32_t(static_cast<uint8_t *>(block) - g_arena);
        for (const ArenaBlock &b : g_arenaBlocks)
            if (b.offset == offset)
                return b.size;
        return 0;
    }
    return MEM_size(block);
}

void *FakeNew(unsigned size) {
    Log(kTagNew);
    Log(size);
    return Bump(size);
}

void *FakeNewArray(unsigned size) {
    Log(kTagNew + 0x100);
    Log(size);
    return Bump(size);
}

void FakeDelete(void *block) {
    Log(kTagDelete);
    Log(ArenaWord(block));
}

void *FakeEaglMalloc(uint32_t size, const char *name) {
    Log(kTagEagl);
    Log(size);
    LogString(name);
    return Bump(size);
}

void FakeEaglFree(void *block, uint32_t size) {
    Log(kTagEagl + 0x100);
    Log(ArenaWord(block));
    Log(size);
}

ASound *__fastcall FakeSoundDelete(ASound *sound, int, unsigned flags) {
    Log(kTagSoundDelete);
    Log(ArenaWord(sound));
    Log(flags);
    return sound;
}

const void *const g_fakeSoundVtable[4] = { (const void *)&FakeSoundDelete, NULL, NULL, NULL };

void *FakeSoundNew(unsigned size, const char *name) {
    Log(kTagSoundNew);
    Log(size);
    LogString(name);
    return Bump(size);
}

ASound *__fastcall FakeSoundConstruct(ASound *sound, int, const char *mixName) {
    Log(kTagSound);
    Log(ArenaWord(sound));
    LogString(mixName);
    sound->vtable = uint32_t(uintptr_t(g_fakeSoundVtable));
    sound->mix = reinterpret_cast<AMix *>(Bump(sizeof(AMix)));
    for (int view = 0; view < kSoundViewCount; view++) {
        sound->voice.views[view].finished = (g_fake.soundFinished >> view) & 1;
        sound->voice.views[view].loop = 1;
    }
    return sound;
}

AMix *FakeMixAdd(const char *name) {
    Log(kTagMix);
    LogString(name);
    return reinterpret_cast<AMix *>(Bump(sizeof(AMix)));
}

void __fastcall FakeMixRemove(AMix *mix, int) {
    Log(kTagMix + 0x100);
    Log(ArenaWord(mix));
}

int __fastcall FakeLookup(AIndex *index, int, const char *name) {
    Log(kTagLookup);
    Log(uint32_t(uintptr_t(index)));
    LogString(name);
    return int(Hash(name, strlen(name)) & 0xff);
}

void __fastcall FakeVoiceSet(AVoice *voice, int, int bank, int patch) {
    Log(kTagVoice);
    Log(ArenaWord(voice));
    Log(uint32_t(bank));
    Log(uint32_t(patch));
}

// The game's other threads (the sound's) read the clock too: they get the tick of the shadow's start, so the cases'
// ticks go only to the shadow's thread
DWORD g_shadowThread;
int g_otherThreadsTick;

int FakeGetTick() {
    if (GetCurrentThreadId() != g_shadowThread)
        return g_otherThreadsTick;
    if (g_fake.tickCount == 0)
        return 0;
    int tick = g_fake.ticks[g_fake.tickIndex % g_fake.tickCount];
    g_fake.tickIndex++;
    return tick;
}

int FakeGetFrequency() {
    return g_fake.frequency;
}

void FakeTaskAdd(SyncTaskFn task, int interval, int delay) {
    Log(kTagTask);
    Log(CanonCode((const void *)task));
    Log(uint32_t(interval));
    Log(uint32_t(delay));
}

void FakeTaskDel(SyncTaskFn task) {
    Log(kTagTask + 0x100);
    Log(CanonCode((const void *)task));
}

void FakePrint(char *format, ...) {
    Log(kTagPrint);
    // The clock (a fake tick) and the message; the memory free between them differs with the arena's use
    const char *dash = strstr(format, " - ");
    size_t clock = strcspn(format, " ");
    Log(Hash(format, clock));
    LogString(dash != NULL ? dash : format);
}

enum HookIndex {
    kHookSprite, kHookSpriteWhole, kHookQuadC, kHookQuad, kHookBlend, kHookBatchQuad, kHookBatchSprite, kHookText,
    kHookSimpleDraw, kHookTexturedDraw, kHookBeginFrame, kHookEndFrame, kHookBeginView, kHookEndView, kHookClearView,
    kHookViewMatrix, kHookBackground, kHookAlphaWrites, kHookAddGlare, kHookAddDebris, kHookAddSystem,
    kHookAddOrRefresh, kHookFastAlloc, kHookFastFree, kHookAlloc, kHookFree, kHookSize, kHookNew, kHookDelete,
    kHookNewArray, kHookSoundNew, kHookSoundConstruct, kHookMixAdd, kHookMixRemove, kHookLookup, kHookVoiceSet, kHookGetTick,
    kHookGetFrequency, kHookTaskAdd, kHookTaskDel, kHookPrint, kHookCount
};
Hook g_hooks[kHookCount];

bool Hooked(std::initializer_list<int> which) {
    for (int i : which)
        if (!g_hooks[i].on)
            return false;
    return true;
}

void InstallHooks() {
    HookInstall(&g_hooks[kHookSprite], kDrawSprite, (const void *)&FakeDrawSprite);
    HookInstall(&g_hooks[kHookSpriteWhole], kDrawSpriteWhole, (const void *)&FakeDrawSpriteWhole);
    HookInstall(&g_hooks[kHookQuadC], kDrawQuadC, (const void *)&FakeDrawQuadC);
    HookInstall(&g_hooks[kHookQuad], kDrawQuad, (const void *)&FakeDrawQuad);
    HookInstall(&g_hooks[kHookBlend], kNormalBlend, (const void *)&FakeNormalBlend);
    HookInstall(&g_hooks[kHookBatchQuad], kBatchedQuad, (const void *)&FakeBatchedQuad);
    HookInstall(&g_hooks[kHookBatchSprite], kBatchedSprite, (const void *)&FakeBatchedSprite);
    HookInstall(&g_hooks[kHookText], kDrawText, (const void *)&FakeDrawText);
    HookInstall(&g_hooks[kHookSimpleDraw], kSimpleDraw, (const void *)&FakeSimpleDraw);
    HookInstall(&g_hooks[kHookTexturedDraw], kTexturedDraw, (const void *)&FakeTexturedDraw);
    HookInstall(&g_hooks[kHookBeginFrame], kBeginFrame, (const void *)&FakeBeginFrame);
    HookInstall(&g_hooks[kHookEndFrame], kEndFrame, (const void *)&FakeEndFrame);
    HookInstall(&g_hooks[kHookBeginView], kBeginView, (const void *)&FakeBeginView);
    HookInstall(&g_hooks[kHookEndView], kEndView, (const void *)&FakeEndView);
    HookInstall(&g_hooks[kHookClearView], kClearView, (const void *)&FakeClearView);
    HookInstall(&g_hooks[kHookViewMatrix], kViewMatrix, (const void *)&FakeViewMatrix);
    HookInstall(&g_hooks[kHookBackground], kBackground, (const void *)&FakeBackground);
    HookInstall(&g_hooks[kHookAlphaWrites], kAlphaWrites, (const void *)&FakeAlphaWrites);
    HookInstall(&g_hooks[kHookAddGlare], kAddGlare, (const void *)&FakeAddGlare);
    HookInstall(&g_hooks[kHookAddDebris], kAddDebris, (const void *)&FakeAddDebris);
    HookInstall(&g_hooks[kHookAddSystem], kAddSystem, (const void *)&FakeAddSystem);
    HookInstall(&g_hooks[kHookAddOrRefresh], kAddOrRefresh, (const void *)&FakeAddOrRefresh);
    HookInstall(&g_hooks[kHookFastAlloc], kFastAlloc, (const void *)&FakeFastAlloc);
    HookInstall(&g_hooks[kHookFastFree], kFastFree, (const void *)&FakeFastFree);
    HookInstall(&g_hooks[kHookAlloc], kAlloc, (const void *)&FakeAlloc);
    HookInstall(&g_hooks[kHookFree], kFree, (const void *)&FakeFree);
    HookInstall(&g_hooks[kHookSize], kSize, (const void *)&FakeSize);
    HookInstall(&g_hooks[kHookNew], kNew, (const void *)&FakeNew);
    HookInstall(&g_hooks[kHookDelete], kDelete, (const void *)&FakeDelete);
    HookInstall(&g_hooks[kHookNewArray], kNewArray, (const void *)&FakeNewArray);
    HookInstall(&g_hooks[kHookSoundNew], kSoundNew, (const void *)&FakeSoundNew);
    HookInstall(&g_hooks[kHookSoundConstruct], kSoundConstruct, (const void *)&FakeSoundConstruct);
    HookInstall(&g_hooks[kHookMixAdd], kMixAdd, (const void *)&FakeMixAdd);
    HookInstall(&g_hooks[kHookMixRemove], kMixRemove, (const void *)&FakeMixRemove);
    HookInstall(&g_hooks[kHookLookup], kIndexLookup, (const void *)&FakeLookup);
    HookInstall(&g_hooks[kHookVoiceSet], kVoiceSet, (const void *)&FakeVoiceSet);
    HookInstall(&g_hooks[kHookGetTick], kGetTick, (const void *)&FakeGetTick);
    HookInstall(&g_hooks[kHookGetFrequency], kGetFrequency, (const void *)&FakeGetFrequency);
    HookInstall(&g_hooks[kHookTaskAdd], kTaskAdd, (const void *)&FakeTaskAdd);
    HookInstall(&g_hooks[kHookTaskDel], kTaskDel, (const void *)&FakeTaskDel);
    HookInstall(&g_hooks[kHookPrint], kPrint, (const void *)&FakePrint);
    int missing = 0;
    for (const Hook &hook : g_hooks)
        missing += hook.on ? 0 : 1;
    if (missing != 0) {
        printf("[gamefrontendshadowc] %d of the %d fakes could not be installed - the cases needing them skipped:",
               missing, int(kHookCount));
        for (int i = 0; i < kHookCount; i++)
            if (!g_hooks[i].on)
                printf(" %d", i);
        printf("\n");
    }
}

// The allocator fakes every case may reach
bool MemoryHooked() {
    return Hooked({ kHookFastAlloc, kHookFastFree, kHookAlloc, kHookFree, kHookSize, kHookNew, kHookDelete,
                    kHookNewArray });
}

bool DrawHooked() {
    return Hooked({ kHookSprite, kHookSpriteWhole, kHookQuadC, kHookQuad, kHookBlend, kHookBatchQuad,
                    kHookBatchSprite, kHookText, kHookSimpleDraw, kHookTexturedDraw });
}

bool FrameHooked() {
    return Hooked({ kHookBeginFrame, kHookEndFrame, kHookBeginView, kHookEndView, kHookClearView, kHookViewMatrix,
                    kHookBackground, kHookAlphaWrites });
}

// ---- running a case

typedef void (*SideFn)(void);

const char *g_caseName = "";
long g_caseIndex;
bool g_eaglArena;           // EaglMalloc and EaglFree pointed at the arena for the case
bool g_compareArena = true; // false: the arena holds pointers into each side's own loads; the logs cover it

void Window(bool original) {
    for (unsigned at : kEntries)
        XbeOriginal_Restore(at, original);
}

void RunSide(SideFn run, bool original) {
    g_original = original;
    ResetArena();
    g_systems.clear();
    g_systemsAdded = 0;
    g_fake.tickIndex = 0;
    uint32_t mallocWas = ShadowEaglMalloc, freeWas = ShadowEaglFree;
    if (g_eaglArena) {
        ShadowEaglMalloc = uint32_t(uintptr_t(&FakeEaglMalloc));
        ShadowEaglFree = uint32_t(uintptr_t(&FakeEaglFree));
    }
    if (original)
        Window(true);
#ifdef _MSC_VER
    __try {
        run();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_faults++ < 10)
            printf("[gamefrontendshadowc]   %s #%ld: the %s faulted\n", g_caseName, g_caseIndex,
                   original ? "original" : "port");
    }
#else
    run();
#endif
    if (original)
        Window(false);
    ShadowEaglMalloc = mallocWas;
    ShadowEaglFree = freeWas;
    ResetFpu();
}

std::vector<uint8_t> g_arenaAfter[2];

// The inputs are in place (ours): both sides from them, the results compared, everything put back
void RunCase(const char *what, long index, SideFn run, const Snapshot &before) {
    if (g_verbose) {
        printf("[gamefrontendshadowc] case %s #%ld\n", what, index);
        fflush(stdout);
    }
    g_caseName = what;
    g_caseIndex = index;
    Lend();
    Snapshot inputs, original, port;
    Capture(&inputs);
    g_logs[0].clear();
    g_logs[1].clear();
    long faults = g_faults;
    RunSide(run, true);
    bool originalFaulted = g_faults != faults;
    Capture(&original);
    g_arenaAfter[0].assign(g_arena, g_arena + g_arenaUsed);
    Restore(inputs);
    faults = g_faults;
    RunSide(run, false);
    bool portFaulted = g_faults != faults;
    Capture(&port);
    g_arenaAfter[1].assign(g_arena, g_arena + g_arenaUsed);
    g_cases++;
    if (originalFaulted && portFaulted) {
        g_bothFaulted++;
    } else {
        if (originalFaulted != portFaulted)
            Report(what, index, originalFaulted ? "only the original faulted" : "only the port faulted");
        CompareResults(what, index, original, port);
        CompareLogs(what, index);
        char label[96];
        snprintf(label, sizeof(label), "%s: the arena", what);
        if (g_arenaAfter[0].size() != g_arenaAfter[1].size()) {
            g_checks++;
            Report(label, index, "the sides allocated differently");
        } else if (g_compareArena && !g_arenaAfter[0].empty()) {
            CompareBytes(label, index, g_arenaAfter[0].data(), g_arenaAfter[1].data(), g_arenaAfter[0].size());
        }
    }
    Restore(before);
}

// ---- the inputs of a case

alignas(16) uint8_t g_galleryBlock[sizeof(GGallery)];
GGallery *const g_gallery = reinterpret_cast<GGallery *>(g_galleryBlock);
alignas(16) uint8_t g_newGalleryBlock[sizeof(GGallery)];
GGallery *const g_newGallery = reinterpret_cast<GGallery *>(g_newGalleryBlock);
alignas(16) uint8_t g_keyBlock[2][0x200];
GalleryKey *const g_key = reinterpret_cast<GalleryKey *>(g_keyBlock[0]);
GalleryKey *const g_key2 = reinterpret_cast<GalleryKey *>(g_keyBlock[1]);
alignas(16) uint8_t g_outBlock[0x200];
alignas(16) Coord4 g_vertices[16];
uint32_t g_colours[16];
alignas(16) uint8_t g_shapeDirectory[0x10 + 8 * 16];

struct Inputs {
    int op;
    int x, y, count, index;
    float a, b, c, d, t;
    uint32_t colour;
    const char *text;
    bool flag;
    UGroup *group;
    GalleryKey *keys;
    alignas(16) Coord4 position, rotation, scale;
} g_in;

const char *const kTexts[] = {
    "SCORE", "00:00", "MISSION OBJECTIVES", "%s %d", "", "W", "123456789 abcdefghijklmnopqrstuvwxyz",
};
const int kLocaleTexts[] = { 0x2278, 0x18576, 0x18577 };

// The live HUD's gallery and its puppets
GGallery *g_hudGallery;
struct Puppet {
    UGroup *canvas;
    const char *name;
    GalleryKey *keys;
    int count;
};
std::vector<UGroup *> g_canvases;
std::vector<Puppet> g_puppets;

void FindHudPuppets() {
    uint8_t *hud = GHud_TheApp();
    if (hud == NULL)
        return;
    g_hudGallery = *reinterpret_cast<GGallery **>(hud + 0x18);
    if (g_hudGallery == NULL || g_hudGallery->gallery == NULL)
        return;
    UGroup *root = g_hudGallery->gallery;
    for (UGroup *canvas = root->GetArray(); canvas != root->GetArray() + root->GroupCount(); canvas++) {
        g_canvases.push_back(canvas);
        for (UGroup *puppet = canvas->GetArray(); puppet != canvas->GetArray() + canvas->GroupCount(); puppet++) {
            UData *name = puppet->DataLocateTag(kGalleryTagName);
            UData *keys = puppet->DataLocateTag(kGalleryTagKeys);
            if (name == puppet->DataEnd() || keys == puppet->DataEnd())
                continue;
            g_puppets.push_back({ canvas, reinterpret_cast<const char *>(name->Data()),
                                  reinterpret_cast<GalleryKey *>(keys->Data()), int(keys->count) });
        }
    }
}

// A random key of each kind's data, sane enough to draw
void RandomKey(GalleryKey *key, int shapes) {
    memset(key, 0, 0x100);
    key->position = { Awkward(Uniform(-50.0f, 700.0f)), Awkward(Uniform(-50.0f, 520.0f)), Uniform(-2.0f, 2.0f) };
    key->time = Range(-5, 100);
    key->orientation = RandomQuaternion();
    key->unknown20 = uint8_t(Next());
    key->extraSize = uint8_t(Chance(70) ? Range(0x10, 0xb0) : Next());
    GallerySprite &sprite = key->sprite;
    for (float &edge : sprite.texture)
        edge = Awkward(Uniform(0.0f, 1.0f));
    sprite.width = Awkward(Uniform(0.0f, 96.0f));
    sprite.height = Awkward(Uniform(0.0f, 96.0f));
    sprite.colour = Next() ^ Next() << 8;
    sprite.shape = Range(0, shapes > 0 ? shapes - 1 : 0);
    sprite.scaleX = Chance(80) ? Range(250, 2500) : Range(-3000, 30000000);
    sprite.scaleY = Chance(80) ? Range(250, 2500) : Range(-3000, 30000000);
    sprite.flags = Range(0, 3);
    if (Chance(60)) {
        sprite.repeatLeft = int8_t(Range(-2, 3));
        sprite.repeatRight = int8_t(Range(-2, 3));
        sprite.repeatUp = int8_t(Range(-2, 3));
        sprite.repeatDown = int8_t(Range(-2, 3));
    }
}

void RandomTextKey(GalleryKey *key, int shapes) {
    RandomKey(key, shapes);
    memset(&key->text, 0, sizeof(GalleryText));
    key->text.colour = Next() ^ Next() << 8;
    key->text.text = kLocaleTexts[Range(0, 2)];
    key->text.scaleX = Chance(80) ? Range(250, 2500) : Range(-3000, 30000000);
    key->text.scaleY = Chance(80) ? Range(250, 2500) : Range(-3000, 30000000);
    key->text.shape = Range(0, shapes > 0 ? shapes - 1 : 0);
}

void RandomQuad(GalleryQuad *quad) {
    for (uint32_t &c : quad->colours)
        c = Chance(10) ? 0 : (Next() ^ Next() << 8);
    for (auto &corner : quad->corners) {
        corner[0] = Awkward(Uniform(-100.0f, 100.0f));
        corner[1] = Awkward(Uniform(-100.0f, 100.0f));
    }
}

void RandomScreen() {
    static const int kSizes[][2] = { { 640, 480 }, { 720, 480 }, { 720, 576 }, { 1280, 720 }, { 1920, 1080 } };
    int size = Range(0, 4);
    fgRenderer->screenWidth = kSizes[size][0];
    fgRenderer->screenHeight = kSizes[size][1];
    fgRenderer->widescreen = uint8_t(Chance(50));
    fgRenderer->videoMode = Range(0, 3);
}

// ---- GGallery: queries on the live gallery

void SideQuery() {
    GGallery *gallery = g_hudGallery;
    switch (g_in.op) {
    case 0: {
        UGroup *found = g_original ? Orig_GetCanvas(g_gallery, 0, g_in.group, g_in.text)
                                   : g_gallery->GET_Canvas(g_in.group, g_in.text);
        Log(uint32_t(uintptr_t(found)));
        break;
    }
    case 1: {
        GalleryKey *found = g_original ? Orig_GetPuppet(g_gallery, 0, g_in.group, g_in.text, g_in.x, g_in.y)
                                       : g_gallery->GET_Puppet(g_in.group, g_in.text, g_in.x, g_in.y);
        Log(uint32_t(uintptr_t(found)));
        break;
    }
    case 2: {
        GalleryKey *found = g_original ? Orig_GetKey(g_gallery, 0, g_in.keys, g_in.index)
                                       : g_gallery->GET_Key(g_in.keys, g_in.index);
        Log(uint32_t(uintptr_t(found)));
        break;
    }
    case 3: {
        GalleryKey *out = reinterpret_cast<GalleryKey *>(g_outBlock);
        memset(g_outBlock, 0x5a, sizeof(g_outBlock));
        if (g_original)
            Orig_Interpolate(g_gallery, 0, out, g_key, g_key2, g_in.t);
        else
            g_gallery->Get_InterpolatedKey(out, g_key, g_key2, g_in.t);
        LogBytes(g_outBlock, 0x22);         // the rest is the original's stack garbage
        LogBytes(g_outBlock + 0x34, 0x40);
        break;
    }
    case 4: {
        GalleryQuad *out = reinterpret_cast<GalleryQuad *>(g_outBlock);
        if (g_original)
            Orig_QuadInterpolate(g_gallery, 0, &g_key->quad, &g_key2->quad, out, g_in.t);
        else
            g_gallery->QUAD_Interpolate(&g_key->quad, &g_key2->quad, out, g_in.t);
        LogBytes(out->colours, sizeof(out->colours));
        // Two NaN corners (a real key that holds no quad) leave the x87's larger payload in the original, the
        // operand MSVC put first in the port: any NaN is logged alike
        for (const auto &corner : out->corners)
            for (float c : corner)
                Log(c != c ? 0x7fc00000u : std::bit_cast<uint32_t>(c));
        break;
    }
    case 5: {
        float scaling[3] = { g_in.scale.x, g_in.scale.y, g_in.scale.z };
        if (g_original)
            Orig_Transform(g_gallery, 0, g_vertices, g_in.count, &g_in.position, &g_in.rotation, scaling);
        else
            g_gallery->TRANSFORM_Vertices(g_vertices, g_in.count, &g_in.position, &g_in.rotation, scaling);
        LogBytes(g_vertices, sizeof(g_vertices));
        break;
    }
    case 6: {
        const ShapeFile *shapes = g_in.group != NULL ? reinterpret_cast<const ShapeFile *>(g_in.group)
                                                     : reinterpret_cast<const ShapeFile *>(g_shapeDirectory);
        int index = g_original ? Orig_MainShape(g_gallery, 0, shapes) : g_gallery->MGMT_MainShapeIndex(shapes);
        Log(uint32_t(index));
        break;
    }
    case 7: {
        void *data = g_original ? Orig_GetSomeThing(gallery, 0, g_in.index) : gallery->GET_SomeThing(g_in.index);
        Log(uint32_t(uintptr_t(data)));
        UGroup *root = g_original ? Orig_GetGallery(gallery, 0) : gallery->GET_Gallery();
        Log(uint32_t(uintptr_t(root)));
        break;
    }
    case 8: {
        float width = g_original ? Orig_FontWidth(gallery, 0, g_in.text) : gallery->FONT_Width(g_in.text);
        float height = g_original ? Orig_FontHeight(gallery, 0, g_in.text) : gallery->FONT_Height(g_in.text);
        LogFloat(width);
        LogFloat(height);
        break;
    }
    }
}

void TestGalleryQueries(const Snapshot &before) {
    if (g_hudGallery == NULL)
        return;
    memcpy(g_galleryBlock, g_hudGallery, sizeof(GGallery));
    long index = 0;
    // GET_Canvas
    char name[128];
    for (UGroup *canvas : g_canvases) {
        const char *canvasName = reinterpret_cast<const char *>(canvas->DataLocateTag(kGalleryTagName)->Data());
        for (int variant = 0; variant < 3; variant++) {
            snprintf(name, sizeof(name), "%s", canvasName);
            for (char *c = name; *c != '\0'; c++) {
                if (variant == 1 && *c >= 'a' && *c <= 'z')
                    *c = char(*c - 32);
                if (variant == 2 && *c >= 'A' && *c <= 'Z')
                    *c = char(*c + 32);
            }
            g_in.op = 0;
            g_in.group = g_hudGallery->gallery;
            g_in.text = name;
            RunCase("GGallery::GET_Canvas", index++, SideQuery, before);
        }
    }
    g_in.op = 0;
    g_in.text = "no such canvas";
    RunCase("GGallery::GET_Canvas", index++, SideQuery, before);
    g_in.group = NULL;
    RunCase("GGallery::GET_Canvas", index++, SideQuery, before);
    // GET_Puppet, GET_Key
    index = 0;
    for (const Puppet &puppet : g_puppets) {
        g_in.op = 1;
        g_in.group = puppet.canvas;
        g_in.text = puppet.name;
        g_in.x = Range(-1, 3);
        g_in.y = Range(-1, 3);
        RunCase("GGallery::GET_Puppet", index++, SideQuery, before);
        g_in.op = 2;
        g_in.keys = puppet.keys;
        g_in.index = Range(0, puppet.count + 1);
        RunCase("GGallery::GET_Key", index, SideQuery, before);
    }
    if (!g_canvases.empty()) {
        g_in.op = 1;
        g_in.group = g_canvases[0];
        g_in.text = "no such puppet";
        RunCase("GGallery::GET_Puppet", index++, SideQuery, before);
    }
    // Get_InterpolatedKey and QUAD_Interpolate on real keys and random ones
    for (long i = 0; i < 300; i++) {
        if (!g_puppets.empty() && Chance(60)) {
            const Puppet &a = g_puppets[Range(0, int(g_puppets.size()) - 1)];
            const Puppet &b = g_puppets[Range(0, int(g_puppets.size()) - 1)];
            memcpy(g_keyBlock[0], a.keys, 0x80);
            memcpy(g_keyBlock[1], b.keys, 0x80);
        } else {
            RandomKey(g_key, 15);
            RandomKey(g_key2, 15);
            RandomQuad(&g_key->quad);
            RandomQuad(&g_key2->quad);
        }
        g_in.t = Awkward(Uniform(-0.5f, 1.5f));
        g_in.op = Chance(50) ? 3 : 4;
        RunCase(g_in.op == 3 ? "GGallery::Get_InterpolatedKey" : "GGallery::QUAD_Interpolate", i, SideQuery, before);
    }
    // TRANSFORM_Vertices
    for (long i = 0; i < 200; i++) {
        for (Coord4 &v : g_vertices)
            v = { Awkward(Uniform(-100.0f, 100.0f)), Uniform(-100.0f, 100.0f), Uniform(-2.0f, 2.0f), Uniform(0.0f, 2.0f) };
        g_in.count = Chance(10) ? Range(-2, 0) : Range(1, 16);
        g_in.position = { Uniform(-500.0f, 500.0f), Uniform(-500.0f, 500.0f), Uniform(-5.0f, 5.0f), 1.0f };
        g_in.rotation = RandomQuaternion();
        g_in.scale = { Awkward(Uniform(-2.0f, 3.0f)), Uniform(-2.0f, 3.0f), Uniform(-2.0f, 3.0f), 1.0f };
        g_in.op = 5;
        RunCase("GGallery::TRANSFORM_Vertices", i, SideQuery, before);
    }
    // MGMT_MainShapeIndex: the texture files, then random directories
    index = 0;
    UGroup *root = g_hudGallery->gallery;
    for (int i = 0; i < kGalleryShapes; i++) {
        UData *textures = root->DataLocateTag(GalleryIndexedTag(kGalleryTagTextures, i));
        if (textures == root->DataEnd())
            break;
        g_in.op = 6;
        g_in.group = reinterpret_cast<UGroup *>(textures->Data());
        RunCase("GGallery::MGMT_MainShapeIndex", index++, SideQuery, before);
    }
    for (long i = 0; i < 100; i++) {
        ShapeFile *file = reinterpret_cast<ShapeFile *>(g_shapeDirectory);
        file->count = Chance(10) ? Range(-1, 0) : Range(1, 16);
        static const char *const kNames[] = { "MAIN", "MAIM", "main", "MAI\0", "AAAA" };
        for (int e = 0; e < 16; e++)
            memcpy(&file->entries[e].name, kNames[Range(0, 4)], 4);
        g_in.op = 6;
        g_in.group = NULL;
        RunCase("GGallery::MGMT_MainShapeIndex", index++, SideQuery, before);
    }
    // GET_SomeThing, GET_Gallery
    g_in.op = 7;
    g_in.index = 0;
    RunCase("GGallery::GET_SomeThing", 0, SideQuery, before);
    // FONT_Width, FONT_Height
    if (g_hudGallery->font != NULL) {
        for (long i = 0; i < 40; i++) {
            g_in.op = 8;
            g_in.text = Chance(70) ? kTexts[Range(0, 6)] : GSystem_LOCALE_Symbol(kLocaleTexts[Range(0, 2)]);
            RunCase("GGallery::FONT_Width/Height", i, SideQuery, before);
        }
    }
}

// ---- GGallery: states and draws on a copy of the HUD's gallery

void SideGalleryDraw() {
    GGallery *g = g_gallery;
    switch (g_in.op) {
    case 0:
        if (g_original) Orig_SpriteSetState(g, 0, g_key); else g->SPRITE_SetState(g_key);
        break;
    case 1:
        if (g_original) Orig_QuadSetState(g, 0, g_key); else g->QUAD_SetState(g_key);
        break;
    case 2:
        if (g_original) Orig_FontSetState(g, 0, g_key); else g->FONT_SetState(g_key);
        break;
    case 3:
        if (g_original) Orig_FontSetScale(g, 0, g_in.a, g_in.b); else g->FONT_SetScale(g_in.a, g_in.b);
        break;
    case 4:
        if (g_original) Orig_FontDrawKey(g, 0, &g_key->text, g_in.text); else g->FONT_Draw(&g_key->text, g_in.text);
        break;
    case 5:
        if (g_original)
            Orig_FontDrawColour(g, 0, g_in.text, g_in.a, g_in.b, g_in.colour);
        else
            g->FONT_Draw(g_in.text, g_in.a, g_in.b, g_in.colour);
        break;
    case 6:
        if (g_original) Orig_FontDraw(g, 0, g_in.text, g_in.a, g_in.b); else g->FONT_Draw(g_in.text, g_in.a, g_in.b);
        break;
    case 7:
        if (g_original) Orig_BitmapDraw(g, 0, g_key); else g->BITMAP_Draw2D(g_key);
        break;
    case 8:
        if (g_original)
            Orig_LineDraw(g, 0, g_vertices, g_colours, g_in.count);
        else
            g->LINE_Draw2DStrip(g_vertices, g_colours, g_in.count);
        break;
    case 9:
        if (g_original) Orig_QuadDraw(g, 0, &g_key->quad, g_in.t); else g->QUAD_Draw(&g_key->quad, g_in.t);
        break;
    case 10:
        if (g_original) Orig_QuadDrawKey(g, 0, g_key, g_in.t); else g->QUAD_Draw(g_key, g_in.t);
        break;
    case 11:
        if (g_original)
            Orig_QuadDraw2D(g, 0, g_in.a, g_in.b, g_in.c, g_in.d, g_in.colour);
        else
            g->QUAD_Draw2D(g_in.a, g_in.b, g_in.c, g_in.d, g_in.colour);
        break;
    case 12: {
        const GallerySprite *sprite = g_in.flag ? &g_key2->sprite : NULL;
        if (g_original)
            Orig_SpriteDraw(g, 0, g_in.x, g_in.y, g_key, sprite);
        else
            g->SPRITE_Draw2D(g_in.x, g_in.y, g_key, sprite);
        break;
    }
    case 13:
        if (g_original) Orig_SpriteDrawKey(g, 0, g_key); else g->SPRITE_Draw2D(g_key);
        break;
    case 14:
        if (g_original) Orig_SpriteFlush(g, 0); else g->SPRITE_Flush();
        break;
    case 15:
        if (g_original) Orig_CullSetCanvasStatic(g_in.index, g_in.flag, g_in.a);
        else GGallery::CULL_SetCanvas(g_in.index, g_in.flag, g_in.a);
        break;
    }
}

const char *const kGalleryOps[] = {
    "GGallery::SPRITE_SetState", "GGallery::QUAD_SetState", "GGallery::FONT_SetState", "GGallery::FONT_SetScale",
    "GGallery::FONT_Draw (key)", "GGallery::FONT_Draw (colour)", "GGallery::FONT_Draw", "GGallery::BITMAP_Draw2D",
    "GGallery::LINE_Draw2DStrip", "GGallery::QUAD_Draw", "GGallery::QUAD_Draw (key)", "GGallery::QUAD_Draw2D",
    "GGallery::SPRITE_Draw2D", "GGallery::SPRITE_Draw2D (key)", "GGallery::SPRITE_Flush",
};

void TestGalleryDraws(const Snapshot &before) {
    if (g_hudGallery == NULL || g_hudGallery->font == NULL || !DrawHooked())
        return;
    int shapes = g_hudGallery->shapeCount;
    for (long i = 0; i < 1500; i++) {
        memcpy(g_galleryBlock, g_hudGallery, sizeof(GGallery));
        g_gallery->position = { Awkward(Uniform(-50.0f, 700.0f)), Awkward(Uniform(-50.0f, 520.0f)), Uniform(-2.0f, 2.0f),
                                1.0f };
        g_gallery->orientation = RandomQuaternion();
        g_gallery->scale = { Awkward(Uniform(-1.0f, 3.0f)), Uniform(-1.0f, 3.0f), 1.0f, 1.0f };
        g_gallery->colour = Chance(15) ? 0 : (Next() ^ Next() << 8);
        g_gallery->keepRenderState = Chance(30);
        g_gallery->font->scaleX = Uniform(0.2f, 2.0f);
        g_gallery->font->scaleY = Uniform(0.2f, 2.0f);
        RandomScreen();
        g_in.op = Range(0, 14);
        if (g_in.op == 2 || g_in.op == 4)
            RandomTextKey(g_key, shapes);
        else
            RandomKey(g_key, shapes);
        RandomKey(g_key2, shapes);
        if (g_in.op == 9 || g_in.op == 10)
            RandomQuad(&g_key->quad);
        if (!g_puppets.empty() && (g_in.op == 0 || g_in.op == 7 || g_in.op == 13) && Chance(40)) {
            const Puppet &puppet = g_puppets[Range(0, int(g_puppets.size()) - 1)];
            if (puppet.keys->sprite.shape >= 0 && puppet.keys->sprite.shape < shapes)
                memcpy(g_keyBlock[0], puppet.keys, 0x80);
        }
        g_in.text = g_in.op == 4 && Chance(40) ? NULL : kTexts[Range(0, 6)];
        g_in.a = Awkward(Uniform(-50.0f, 700.0f));
        g_in.b = Awkward(Uniform(-50.0f, 520.0f));
        g_in.c = Uniform(-10.0f, 200.0f);
        g_in.d = Uniform(-10.0f, 200.0f);
        if (g_in.op == 3) {
            g_in.a = Uniform(0.0f, 3.0f);
            g_in.b = Uniform(0.0f, 3.0f);
        }
        g_in.t = Chance(10) ? Uniform(-1.0f, 2.0f) : Uniform(0.0f, 1.0f);
        g_in.colour = Next() ^ Next() << 8;
        g_in.x = Range(-50, 700);
        g_in.y = Range(-50, 520);
        g_in.flag = Chance(30);
        g_in.count = Range(0, 16);
        for (int v = 0; v < 16; v++) {
            g_vertices[v] = { Uniform(0.0f, 640.0f), Uniform(0.0f, 480.0f), 0.0f, 1.0f };
            g_colours[v] = Next() ^ Next() << 8;
        }
        RunCase(kGalleryOps[g_in.op], i, SideGalleryDraw, before);
    }
}

// Construct, FONT_Load and Destruct of a second HUD gallery
void SideGalleryConstruct() {
    GGallery *g = g_newGallery;
    memset(g_newGalleryBlock, 0xcd, sizeof(g_newGalleryBlock));
    if (g_original)
        Orig_GalleryConstruct(g, 0, "hudNTSC.gal");
    else
        g->Construct("hudNTSC.gal");
    Log(g->gallery != NULL);
    Log(g->file != NULL);
    LogBytes(&g->shapeCount, 8);
    LogBytes(&g->position, 0x30);
    Log(Canon(g->texture));
    Log(g->unknownA0);
    Log(g->unknownA4);
    Log(g->font != NULL);
    Log(g->fontFile != NULL);
    Log(g->keepRenderState);
    for (int i = 0; i < g->shapeCount && i < kGalleryShapes; i++) {
        Log(g->shapes[i] != NULL);
        if (g->shapes[i] != NULL)
            LogBytes(g->shapes[i], 0x38);   // the fields before the pointers
    }
    for (int load = 0; load < 2; load++) {
        if (g_original)
            Orig_FontLoad(g, 0, "data\\loading\\loading.xfn");
        else
            g->FONT_Load("data\\loading\\loading.xfn");
        Log(g->font != NULL);
        if (g->font != NULL)
            LogFloat(g_original ? Orig_FontWidth(g, 0, "SCORE 123") : g->FONT_Width("SCORE 123"));
    }
    if (g_original)
        Orig_GalleryDestruct(g, 0);
    else
        g->Destruct();
    Log(g->file != NULL);
}

void TestGalleryConstruct(const Snapshot &before) {
    if (!MemoryHooked())
        return;
    g_eaglArena = true;
    g_compareArena = false;
    RunCase("GGallery::Construct/FONT_Load/Destruct", 0, SideGalleryConstruct, before);
    g_eaglArena = false;
    g_compareArena = true;
}

// ---- GFXGallery on a synthetic gallery

alignas(16) uint8_t g_fxBlock[sizeof(GFXGallery)];
GFXGallery *const g_fx = reinterpret_cast<GFXGallery *>(g_fxBlock);
alignas(16) uint8_t g_fxNewBlock[sizeof(GFXGallery)];
GFXGallery *const g_fxNew = reinterpret_cast<GFXGallery *>(g_fxNewBlock);
alignas(16) GFXGallery::Canvas g_fxCanvases[kGfxCanvases];
GfxParticleSystem g_fxSystems[kGfxParticleSystems];

std::vector<UGroup *> g_effects;
std::vector<UGroup *> g_particleParts;

// A scene object of our own: its vtable's slot 13 (GetVelocity) answers g_fakeVelocity or NULL
alignas(16) Coord3 g_fakeVelocity;
bool g_fakeVelocityNull;
Coord3 *__fastcall FakeGetVelocity(void *, int) {
    Log(0x56454c4f);
    return g_fakeVelocityNull ? NULL : &g_fakeVelocity;
}
const void *g_fakeObjectVtable[19];
alignas(16) uint8_t g_fakeObjectBlock[0x100];
RSceneObj *const g_fakeObject = reinterpret_cast<RSceneObj *>(g_fakeObjectBlock);

void FindEffects() {
    GFXGallery *live = TheEffectsGallery;
    if (live == NULL || live->gallery == NULL)
        return;
    UGroup *root = live->gallery;
    for (UGroup *effect = root->GetArray(); effect != root->GetArray() + root->GroupCount(); effect++) {
        if (effect->DataLocateTag(kTagFlags) == effect->DataEnd() ||
            effect->DataLocateTag(kTagParticleCount) == effect->DataEnd() ||
            effect->DataLocateTag(kTagLife) == effect->DataEnd())
            continue;
        g_effects.push_back(effect);
        for (UGroup *part = effect->GetArray(); part != effect->GetArray() + effect->GroupCount(); part++)
            if (part->MatchTag() == kTagParticlePart && part->DataLocateTag(kGalleryTagKeys) != part->DataEnd())
                g_particleParts.push_back(part);
    }
}

void ResetFx(GFXGallery *fx) {
    memset(fx, 0, sizeof(GFXGallery));
    GFXGallery *live = TheEffectsGallery;
    fx->gallery = live != NULL ? live->gallery : NULL;
    fx->particleSystems = g_fxSystems;
    fx->canvases = g_fxCanvases;
    fx->freeCount = kGfxCanvases;
    for (int i = 0; i < kGfxCanvases; i++)
        fx->freeCanvases[i] = kGfxCanvases - 1 - i;
    memset(g_fxCanvases, 0, sizeof(g_fxCanvases));
    memset(g_fxSystems, 0, sizeof(g_fxSystems));
}

// A script of operations, chosen before the sides run
enum FxOp {
    kFxAdd, kFxFrame, kFxCull, kFxStickyPos, kFxPosQuat, kFxSuppress, kFxIntensity, kFxPurge, kFxStopped,
    kFxPurgeAll, kFxPurgeObject, kFxPurgeIndex, kFxCullSet, kFxStickyNew, kFxCullStatic, kFxCount
};

struct FxStep {
    int op;
    int effect;
    bool object;
    int instance, index;
    bool flag800, takeVelocity, flag8000, culled, nullVelocity;
    float value;
    alignas(16) Coord4 position, worldOrientation, offset, orientation, velocity;
};
const int kMaxFxSteps = 48;
FxStep g_script[kMaxFxSteps];
int g_scriptLength;

void LogFx() {
    LogBytes(g_fx, sizeof(GFXGallery));
}

void SideFxScript() {
    GFXGallery *fx = g_fx;
    for (int s = 0; s < g_scriptLength; s++) {
        const FxStep &step = g_script[s];
        Log(0x53544550 + step.op);
        switch (step.op) {
        case kFxAdd: {
            UGroup *effect = g_effects[step.effect];
            RSceneObj *object = step.object ? g_fakeObject : NULL;
            const Coord3 *velocity = step.nullVelocity ? NULL : Xyz(&step.velocity);
            int handle = g_original
                ? Orig_CanvasAdd(fx, 0, effect, object, step.instance, &step.position, &step.worldOrientation,
                                 Xyz(&step.offset), &step.orientation, velocity, step.flag800, step.takeVelocity,
                                 step.flag8000)
                : fx->CANVAS_Add(effect, object, step.instance, &step.position, &step.worldOrientation,
                                 Xyz(&step.offset), &step.orientation, velocity, step.flag800, step.takeVelocity,
                                 step.flag8000);
            Log(uint32_t(handle));
            break;
        }
        case kFxFrame:
            if (g_original) Orig_FrameDraw(fx, 0); else fx->FRAME_Draw();
            break;
        case kFxCull: {
            if (g_original) Orig_CullStart(fx, 0); else fx->CULL_Start();
            for (int guard = 0; guard < kGfxCanvases; guard++) {
                alignas(16) Coord4 sphere;
                float height, farScale;
                bool checkFar;
                int item = g_original ? Orig_CullNext(fx, 0, &sphere, &height, &checkFar, &farScale)
                                      : fx->CULL_GetNextCanvas(&sphere, &height, &checkFar, &farScale);
                Log(uint32_t(item));
                if (item < 0)
                    break;
                LogBytes(&sphere, 16);
                LogFloat(height);
                Log(checkFar);
                LogFloat(farScale);
                bool culled = ((step.index >> (item & 15)) & 1) != 0;
                if (g_original) Orig_CullSet(fx, 0, item, culled, step.value); else fx->CULL_SetCanvas(item, culled, step.value);
            }
            break;
        }
        case kFxStickyPos: {
            alignas(16) Coord4 where;
            bool found = g_original ? Orig_StickyPos(fx, 0, unsigned(step.index), &where)
                                    : fx->GET_StickyEffectPos(unsigned(step.index), &where);
            Log(found);
            LogBytes(&where, 16);
            break;
        }
        case kFxPosQuat: {
            alignas(16) Coord4 where, rotation;
            bool found = g_original ? Orig_PosQuat(fx, 0, step.index, &where, &rotation)
                                    : fx->GET_EffectPosQuat(step.index, &where, &rotation);
            Log(found);
            LogBytes(&where, 16);
            LogBytes(&rotation, 16);
            break;
        }
        case kFxSuppress:
            if (fx->canvasCount > 0) {
                int handle = fx->canvasSlots[step.index % fx->canvasCount] + 1;
                if (g_original) Orig_CanvasSuppress(fx, 0, handle); else fx->CANVAS_Suppress(handle);
            }
            break;
        case kFxIntensity:
            if (fx->canvasCount > 0) {
                int handle = fx->canvasSlots[step.index % fx->canvasCount] + 1;
                if (g_original)
                    Orig_CanvasSetIntensity(fx, 0, handle, step.value);
                else
                    fx->CANVAS_SetIntensity(handle, step.value);
            }
            break;
        case kFxPurge: {
            int handle = fx->canvasCount > 0 && step.index >= 0 ? fx->canvasSlots[step.index % fx->canvasCount] + 1 : 0;
            if (g_original) Orig_CanvasPurge(fx, 0, handle); else fx->CANVAS_Purge(handle);
            break;
        }
        case kFxStopped:
            if (fx->canvasCount > 0) {
                int slot = step.index % fx->canvasCount;
                GFXGallery::Canvas *canvas = &fx->canvases[fx->canvasSlots[slot]];
                bool stopped = g_original ? Orig_CanvasHasStopped(fx, 0, canvas) : fx->CANVAS_HasStopped(canvas);
                Log(stopped);
                // A stopped canvas has given its emitters back: taken off the list as FRAME_Draw does, or a later
                // draw or purge faults on both sides on what the game never leaves
                if (stopped) {
                    if (canvas->sticky < kGfxStickies)
                        fx->stickyUsed[canvas->sticky >> 5] &= ~(1u << (canvas->sticky & 31));
                    fx->freeCanvases[fx->freeCount] = fx->canvasSlots[slot];
                    fx->canvasSlots[slot] = fx->canvasSlots[fx->canvasCount - 1];
                    fx->freeCount++;
                    fx->canvasCount--;
                }
            }
            break;
        case kFxPurgeAll:
            if (g_original) Orig_FxPurgeAll(fx, 0); else fx->PurgeAllEffects();
            break;
        case kFxPurgeObject:
            g_fakeVelocityNull = step.nullVelocity;
            if (g_original) Orig_CanvasPurgeObjEffects(fx, 0, g_fakeObject); else fx->CANVAS_PurgeSceneObjEffects(g_fakeObject);
            break;
        case kFxPurgeIndex:
            if (fx->canvasCount > 0) {
                g_fakeVelocityNull = step.nullVelocity;
                int index = fx->canvasSlots[step.index % fx->canvasCount];
                if (g_original) Orig_CanvasPurgeSceneObj(fx, 0, index); else fx->CANVAS_PurgeSceneObj(index);
            }
            break;
        case kFxCullSet:
            if (fx->canvasCount > 0) {
                int item = step.index % fx->canvasCount;
                if (g_original) Orig_CullSet(fx, 0, item, step.culled, step.value); else fx->CULL_SetCanvas(item, step.culled, step.value);
            }
            break;
        case kFxStickyNew: {
            int sticky = g_original ? Orig_StickyNew(fx, 0, step.index) : fx->STICKY_GetNewIndex(step.index);
            Log(uint32_t(sticky));
            break;
        }
        case kFxCullStatic:
            if (fx->canvasCount > 0) {
                int item = step.index % fx->canvasCount;
                Side(TheEffectsGallery, kGameEffectsGallery) = fx;
                if (g_original) Orig_CullSetCanvasStatic(item, step.culled, step.value);
                else GGallery::CULL_SetCanvas(item, step.culled, step.value);
            }
            break;
        }
    }
    UGroup *root = g_original ? Orig_FxGetGallery(fx, 0) : fx->GET_EffectsGallery();
    Log(uint32_t(uintptr_t(root)));
}

void RandomFxStep(FxStep *step, bool objects) {
    static const int kPlain[] = { kFxAdd, kFxAdd, kFxAdd, kFxFrame, kFxFrame, kFxFrame, kFxCull, kFxStickyPos,
                                  kFxPosQuat, kFxSuppress, kFxIntensity, kFxPurge, kFxStopped, kFxPurgeIndex,
                                  kFxCullSet, kFxCullStatic };
    static const int kObjects[] = { kFxAdd, kFxAdd, kFxAdd, kFxStopped, kFxPurgeObject, kFxPurgeIndex, kFxCullSet,
                                    kFxStickyNew, kFxStickyPos, kFxSuppress, kFxIntensity, kFxCullStatic };
    step->op = objects ? kObjects[Range(0, int(sizeof(kObjects) / sizeof(int)) - 1)]
                       : kPlain[Range(0, int(sizeof(kPlain) / sizeof(int)) - 1)];
    step->effect = Range(0, int(g_effects.size()) - 1);
    step->object = objects && Chance(70);
    step->instance = Range(-1, 3);
    step->index = Chance(10) ? Range(-3, 600) : Range(0, 70);
    step->flag800 = Chance(50);
    step->takeVelocity = Chance(40);
    step->flag8000 = Chance(30);
    if (step->object) {     // no transform by the fake object's instance
        step->takeVelocity = false;
        uint8_t effectFlags = *g_effects[step->effect]->DataLocateTag(kTagFlags)->Data();
        if (!(effectFlags & kEffectSticky))
            step->flag800 = false;
    }
    step->culled = Chance(50);
    step->nullVelocity = Chance(30);
    step->value = Uniform(-1.0f, 2.0f);
    step->position = { Uniform(-500.0f, 500.0f), Uniform(-50.0f, 50.0f), Uniform(-500.0f, 500.0f), Uniform(0.5f, 1.5f) };
    step->worldOrientation = RandomQuaternion();
    step->offset = { Uniform(-5.0f, 5.0f), Uniform(-5.0f, 5.0f), Uniform(-5.0f, 5.0f), 1.0f };
    step->orientation = RandomQuaternion();
    step->velocity = { Uniform(-20.0f, 20.0f), Uniform(-20.0f, 20.0f), Uniform(-20.0f, 20.0f), 0.0f };
}

void TestEffectScripts(const Snapshot &before) {
    if (g_effects.empty() || !MemoryHooked() || !DrawHooked() ||
        !Hooked({ kHookAddGlare, kHookAddDebris, kHookAddSystem, kHookAddOrRefresh, kHookSoundNew,
                  kHookSoundConstruct, kHookMixAdd, kHookMixRemove, kHookLookup, kHookVoiceSet }))
        return;
    for (int i = 0; i < 19; i++)
        g_fakeObjectVtable[i] = NULL;
    g_fakeObjectVtable[13] = (const void *)&FakeGetVelocity;
    for (long i = 0; i < 400; i++) {
        bool objects = Chance(25);
        ResetFx(g_fx);
        memset(g_fakeObjectBlock, 0, sizeof(g_fakeObjectBlock));
        *reinterpret_cast<const void ***>(g_fakeObjectBlock) = g_fakeObjectVtable;
        g_fakeVelocity = { Uniform(-30.0f, 30.0f), Uniform(-30.0f, 30.0f), Uniform(-30.0f, 30.0f) };
        g_scriptLength = Range(4, kMaxFxSteps);
        for (int s = 0; s < g_scriptLength; s++)
            RandomFxStep(&g_script[s], objects);
        g_script[0].op = kFxAdd;
        if (Chance(20))
            g_script[g_scriptLength - 1].op = kFxPurgeAll;
        g_fake.soundFinished = uint8_t(Next());
        g_fake.systemsActive = uint8_t(Next());
        g_fake.systemFlags = Next() ^ Next() << 8;
        ShadowActiveViews = Next() & 7;
        GfxRandomState = Next() & 0xffff;
        if (Chance(30))
            GfxRandomMultiplier = Next() | 1;
        LastParticleSystem = Range(-5, 300);
        RunCase(objects ? "GFXGallery script (scene object)" : "GFXGallery script", i, SideFxScript, before);
    }
}

// GLARE_Draw, DEBRIS_Draw, PARTICLE_GetSystem, PARTICLE_Add, GET_Keys and GET_FirstIdleKey, called directly
alignas(16) uint8_t g_soundBlock[sizeof(ASound)];

void SideFxDirect() {
    GFXGallery *fx = g_fx;
    switch (g_in.op) {
    case 0: {
        const GalleryGlare *glare = &g_key->glare;
        if (g_original) Orig_GlareDraw(fx, 0, g_in.group, glare, &g_in.position); else fx->GLARE_Draw(g_in.group, glare, &g_in.position);
        break;
    }
    case 1: {
        const Coord3 *offset = g_in.flag ? Xyz(&g_in.scale) : NULL;
        if (g_original)
            Orig_DebrisDraw(fx, 0, g_key, &g_in.position, &g_in.rotation, offset, g_in.index);
        else
            fx->DEBRIS_Draw(g_key, &g_in.position, &g_in.rotation, offset, g_in.index);
        break;
    }
    case 2: {
        GalleryKey *keys = reinterpret_cast<GalleryKey *>(g_in.group->DataLocateTag(kGalleryTagKeys)->Data());
        uint32_t tag = g_original ? Orig_ParticleSystem(fx, 0, g_in.group, &keys->particle)
                                  : fx->PARTICLE_GetSystem(g_in.group, &keys->particle);
        Log(tag);
        break;
    }
    case 3: {
        ASound *sound = reinterpret_cast<ASound *>(g_soundBlock);
        const GallerySound *key = g_in.flag ? &g_key->sound : NULL;
        if (g_original)
            Orig_SoundAdd(fx, 0, NULL, key, sound, &g_in.position, Xyz(&g_in.scale), g_in.index);
        else
            fx->PARTICLE_Add(NULL, key, sound, &g_in.position, Xyz(&g_in.scale), g_in.index);
        break;
    }
    case 4: {
        GalleryKey *before = NULL, *after = NULL;
        int beforeTime = -7, afterTime = -7;
        if (g_original)
            Orig_GetKeys(fx, 0, g_in.keys, g_in.x, g_in.count, &before, &beforeTime, &after, &afterTime);
        else
            fx->GET_Keys(g_in.keys, g_in.x, g_in.count, &before, &beforeTime, &after, &afterTime);
        Log(uint32_t(uintptr_t(before)));
        Log(uint32_t(beforeTime));
        Log(uint32_t(uintptr_t(after)));
        Log(uint32_t(afterTime));
        GalleryKey *first = g_original ? Orig_FirstIdleKey(fx, 0, g_in.group) : fx->GET_FirstIdleKey(g_in.group);
        Log(uint32_t(uintptr_t(first)));
        break;
    }
    }
}

void TestEffectDirect(const Snapshot &before) {
    if (!MemoryHooked() || !Hooked({ kHookAddGlare, kHookAddDebris, kHookAddSystem, kHookMixAdd, kHookMixRemove,
                                     kHookLookup, kHookVoiceSet }))
        return;
    static const char *const kOps[] = { "GFXGallery::GLARE_Draw", "GFXGallery::DEBRIS_Draw",
                                        "GFXGallery::PARTICLE_GetSystem", "GFXGallery::PARTICLE_Add",
                                        "GFXGallery::GET_Keys" };
    for (long i = 0; i < 800; i++) {
        ResetFx(g_fx);
        g_in.op = Range(0, 4);
        if ((g_in.op == 2 || g_in.op == 4) && g_particleParts.empty())
            continue;
        RandomKey(g_key, 15);
        g_key->glare.type = Range(0, 80);
        g_key->glare.intensity = Uniform(-2.0f, 2.0f);
        g_key->glare.jitter = Chance(50) ? 0 : Range(1, 3);
        GalleryDebris &debris = g_key->debris;
        debris.size = Chance(20) ? Uniform(15.0f, 40.0f) : Awkward(Uniform(0.0f, 15.0f));
        debris.lift = Uniform(-1.0f, 3.0f);
        debris.spreadY = Uniform(0.0f, 3.0f);
        debris.spreadXZ = Uniform(0.0f, 3.0f);
        debris.colour = Next();
        debris.kind = Chance(20) ? 0 : (Chance(10) ? Next() : uint32_t(Range(1, 21)));
        debris.turnX = Chance(30) ? 0.0f : Uniform(-2.0f, 2.0f);
        debris.turnZ = Chance(30) ? 0.0f : Uniform(-2.0f, 2.0f);
        debris.turnY = Chance(30) ? 0.0f : Uniform(-2.0f, 2.0f);
        debris.gravity = Uniform(-0.1f, 0.1f);
        debris.lifetime = uint32_t(Range(0, 200));
        if (g_in.op == 3) {
            GallerySound &sound = g_key->sound;
            sound.volume = Range(-20, 150);
            sound.bank = Range(0, kBankSlotCount - 1);
            snprintf(sound.patch, sizeof(sound.patch), "SFX_%u", unsigned(Next() & 0xff));
            sound.pitch = Range(0, 300);
            sound.minDistance = Range(0, 100);
            sound.maxDistance = Chance(5) ? Range(0x1000000, 0x7fffffff) : Range(0, 1000);
            memset(g_soundBlock, 0, sizeof(g_soundBlock));
            reinterpret_cast<ASound *>(g_soundBlock)->mix = reinterpret_cast<AMix *>(g_arena + 0x1000);
        }
        g_in.position = { Awkward(Uniform(-500.0f, 500.0f)), Uniform(-50.0f, 50.0f), Uniform(-500.0f, 500.0f),
                          Uniform(0.0f, 2.0f) };
        g_in.rotation = RandomQuaternion();
        g_in.scale = { Uniform(-3.0f, 3.0f), Uniform(-3.0f, 3.0f), Uniform(-3.0f, 3.0f), 1.0f };
        g_in.flag = Chance(60);
        g_in.index = Range(-2, 100);
        if (!g_particleParts.empty()) {
            g_in.group = g_particleParts[Range(0, int(g_particleParts.size()) - 1)];
            UData *keys = g_in.group->DataLocateTag(kGalleryTagKeys);
            g_in.keys = reinterpret_cast<GalleryKey *>(keys->Data());
            g_in.count = Chance(10) ? Range(-1, 1) : int(keys->count);
            g_in.x = Chance(10) ? 0 : Range(-5, 120);
        }
        if (g_in.op == 2) {     // a library with the part in it already, now and then
            int prefill = Range(0, 6);
            for (int p = 0; p < prefill && !g_particleParts.empty(); p++) {
                g_fx->particleSystems[p].part = g_particleParts[Range(0, int(g_particleParts.size()) - 1)];
                g_fx->particleSystems[p].tag = Next() & 0xff;
            }
            g_fx->particleSystemCount = prefill;
        }
        GfxRandomState = Next() & 0xffff;
        if (Chance(30))
            GfxRandomMultiplier = Next() | 1;
        LastParticleSystem = Range(-5, 300);
        RunCase(kOps[g_in.op], i, SideFxDirect, before);
    }
}

// Construct and Destruct on a gallery of their own
void SideFxConstruct() {
    memset(g_fxNewBlock, 0xcd, sizeof(g_fxNewBlock));
    if (g_original)
        Orig_FxConstruct(g_fxNew, 0, GSystem_GetGalleryName());
    else
        g_fxNew->Construct(GSystem_GetGalleryName());
    Log(g_fxNew->gallery != NULL);
    Log(g_fxNew->galleryFile != NULL);
    LogBytes(g_fxNew, offsetof(GFXGallery, gallery));
    Log(Canon(g_fxNew->particleSystems));
    Log(Canon(g_fxNew->canvases));
    LogBytes(&g_fxNew->particleSystemCount, 0x18);
    if (g_original)
        Orig_FxDestruct(g_fxNew, 0);
    else
        g_fxNew->Destruct();
    LogBytes(&g_fxNew->galleryFile, 0xc);
    LogBytes(g_fxNew->stickyUsed, 8);
}

void TestEffectConstruct(const Snapshot &before) {
    if (!MemoryHooked())
        return;
    RunCase("GFXGallery::Construct/Destruct", 0, SideFxConstruct, before);
}

// ---- GGirl

alignas(16) uint8_t g_pageBlock[sizeof(LaunchPage)];
int g_girlCalls;
bool g_girlKill;

uint32_t HashShape(const uint8_t *shape) {
    if (shape == NULL)
        return 0;
    const EAGL::ShapeImage *image = reinterpret_cast<const EAGL::ShapeImage *>(shape);
    const uint8_t *pixels = (image->flags & EAGL::kShapeDataAtOffset) ? shape + image->dataOffset : shape + 0x10;
    return Hash(pixels, size_t(SHAPE_rowbytes(shape)) * image->height);
}

void LogGirlState() {
    Log(uint32_t(Side(GirlCurrent, kGameGirlCurrent)));
    Log(uint32_t(Side(GirlActive, kGameGirlActive)));
    Log(uint32_t(Side(GirlFrame, kGameGirlFrame)));
    Log(uint32_t(Side(GirlFrames, kGameGirlFrames)));
    Log(uint32_t(Side(GirlFrameStart, kGameGirlFrameStart)));
    Log(uint32_t(Side(GirlCursor, kGameGirlCursor)));
    LogFloat(Side(GirlTime, kGameGirlTime));
    Log(uint32_t(Side(GirlTick, kGameGirlTick)));
    Log(uint32_t(uintptr_t(Side(GirlData, kGameGirlData))));
    LogFloat(Side(PaletteX, kGamePaletteX));
    LogFloat(Side(PaletteY, kGamePaletteY));
}

void SideGirl() {
    for (int call = 0; call < g_girlCalls; call++) {
        if (g_original)
            Orig_DoGirl();
        else
            GGirl::DoGirl();
        LogGirlState();
    }
    GGirlieMaterial **materials = &Side(GirlMaterials[0], kGameGirlMaterials);
    if (Side(GirlActive, kGameGirlActive) != 0) {
        for (int i = 0; i < 2; i++) {
            GGirlieMaterial *material = materials[i];
            Log(ArenaWord(material));
            if (material == NULL)
                continue;
            Log(HashShape(material->shape));
            LogBytes(material->positions, sizeof(material->positions) + sizeof(material->colours) +
                                              sizeof(material->texCoords));
            Log(ArenaWord(material->tar));
            Log(ArenaWord(material->material));
        }
        if (g_in.flag) {
            if (g_original)
                Orig_DrawTexture(g_in.x, g_in.y);
            else
                GGirl::DrawTexture(g_in.x, g_in.y);
        }
    }
    // Killed at the end of every case: the textures' pixels are each side's own allocations
    for (int kill = 0; kill < (g_girlKill ? 2 : 1); kill++) {
        if (g_original)
            Orig_KillGirl();
        else
            GGirl::KillGirl();
        LogGirlState();
    }
    LaunchPage *page = reinterpret_cast<LaunchPage *>(g_pageBlock);
    Log(g_original ? Orig_EndState(page, 0) : page->IsEndState1Or4());
}

void TestGirl(const Snapshot &before) {
    if (g_hudGallery == NULL || !MemoryHooked() || !Hooked({ kHookGetTick, kHookTexturedDraw }))
        return;
    if (GirlActive != 0) {      // the pause menu is up: leave its figure alone
        g_skipped++;
        return;
    }
    g_eaglArena = true;
    g_compareArena = false;
    for (long i = 0; i < 60; i++) {
        g_girlCalls = Range(1, 14);
        g_girlKill = Chance(30);    // killed twice
        g_fake.tickCount = Range(1, 64);
        int32_t tick = Chance(10) ? int32_t(0x7fffff00 + Range(0, 0xff)) : Range(0, 100000);
        for (int t = 0; t < g_fake.tickCount; t++) {
            g_fake.ticks[t] = tick;
            int step = Range(0, 99);
            tick += step < 50 ? Range(0, 3) : step < 85 ? Range(1, 40) : step < 95 ? Range(100, 5000) : -Range(1, 100);
        }
        PaletteX = Uniform(0.0f, 128.0f);
        PaletteY = Chance(10) ? 128.0f - Uniform(0.0f, 0.3f) : Uniform(0.0f, 128.0f);
        if (Chance(20)) {
            PaletteX2 = Uniform(0.0f, 128.0f);
            PaletteY2 = Uniform(0.0f, 128.0f);
        } else {
            PaletteX2 = 0.0f;
            PaletteY2 = 0.0f;
        }
        GirlCurrent = Range(0, 1);
        GirlFrame = 0;
        GirlFrames = 0;
        GirlData = NULL;
        GirlTime = 0.0f;
        GirlUnmirrored = Chance(15) ? 1 : 0;
        ShadowSimTimeStep = Chance(70) ? 1.0f / 60.0f : Uniform(0.001f, 0.2f);
        g_in.flag = Chance(50);
        g_in.x = Range(-50, 700);
        g_in.y = Range(-50, 500);
        reinterpret_cast<LaunchPage *>(g_pageBlock)->unknown978 = Range(0, 5);
        RunCase("GGirl::DoGirl sequence", i, SideGirl, before);
    }
    g_eaglArena = false;
    g_compareArena = true;
}

// ---- GLoadingScreen

bool g_loadPicture, g_loadKeep, g_loadUnloading;
int g_loadUpdates;

void LogLoadState() {
    Log(Side(KeepLoadScreen, kGameKeepLoadScreen));
    LogFloat(Side(DotDirection, kGameDotDirection));
    Log(Side(BackgroundFile, kGameBackgroundFile) != NULL);
    Log(Side(LoadingFont, kGameLoadingFont) != NULL);
    Log(Side(LoadingFontFile, kGameLoadingFontFile) != NULL);
    LogBytes(g_original ? reinterpret_cast<int32_t *>(uintptr_t(kGameTitleId)) : &TitleId, 4);
    LogBytes(g_original ? reinterpret_cast<int32_t *>(uintptr_t(kGameTitleId + 4)) : &DescId, 4);
    LogBytes(g_original ? reinterpret_cast<int32_t *>(uintptr_t(kGameTitleId + 8)) : &DescLines, 4);
    LogBytes(g_original ? reinterpret_cast<int32_t *>(uintptr_t(kGameTitleId + 12)) : &HintId, 4);
    LogFloat(Side(LoadScale, kGameLoadScale));
    Log(uint32_t(uintptr_t(Side(Dots, kGameDots))));
    LogFloat(Side(DotTime, kGameDotTime));
    Log(uint32_t(Side(LastTick, kGameLastTick)));
    Log(uint32_t(Side(LitDots, kGameLitDots)));
    Log(uint32_t(Side(DotCount, kGameDotCount)));
    Log(uint32_t(Side(BackgroundCount, kGameBackgroundCount)));
    Log(Side(UnloadingFile, kGameUnloadingFile) != NULL);
    Log(ArenaWord(Side(UnloadingTexture, kGameUnloadingTexture)));
    Log(Side(ErrorFont, kGameErrorFont) != NULL);
    Log(Side(ErrorFontFile, kGameErrorFontFile) != NULL);
    EAGL::TAR **dots = g_original ? reinterpret_cast<EAGL::TAR **>(uintptr_t(kGameDotTextures)) : DotTextures;
    EAGL::TAR **backgrounds =
        g_original ? reinterpret_cast<EAGL::TAR **>(uintptr_t(kGameBackgroundTextures)) : BackgroundTextures;
    for (int i = 0; i < 16; i++) {
        Log(ArenaWord(dots[i]));
        if (dots[i] != NULL && InArena(dots[i]))
            LogBytes(dots[i], 0x38);        // the fields before the pointers
    }
    EAGL::TAR *unloading = Side(UnloadingTexture, kGameUnloadingTexture);
    if (unloading != NULL && InArena(unloading))
        LogBytes(unloading, 0x38);
    for (int i = 0; i < 128; i++)
        Log(ArenaWord(backgrounds[i]));
    Log(uint32_t(Side(LoadState, kGameLoadState)));
}

void SideLoadingScreen() {
    if (g_original) Orig_InitLoadDots(g_loadKeep); else GLoadingScreen::InitLoadDots(g_loadKeep);
    LogLoadState();
    if (g_loadPicture) {
        if (g_original)
            Orig_InitLoadScreen("data\\loading\\paris_mis01.xsh", "data\\loading\\default.ini");
        else
            GLoadingScreen::InitLoadScreen("data\\loading\\paris_mis01.xsh", "data\\loading\\default.ini");
        LogLoadState();
    }
    for (int u = 0; u < g_loadUpdates; u++) {
        if (u % 3 == 2)
            g_original ? Orig_TaskCallback(u, 1) : GLoadingScreen::LoadScreenSystemTaskCallback(u, 1);
        else if (g_original)
            Orig_Update();
        else
            GLoadingScreen::Update();
        LogLoadState();
    }
    if (g_original) Orig_DrawDots(); else GLoadingScreen::DrawDots();
    if (g_original) Orig_DrawBackground(); else GLoadingScreen::DrawBackground();
    if (g_original) Orig_ClearToBlack(); else GLoadingScreen::ClearToBlack();
    if (g_original) Orig_AddTask(); else GLoadingScreen::AddLoadingScreenSyncTask();
    if (g_original) Orig_DeleteTask(); else GLoadingScreen::DeleteLoadingScreenSyncTask();
    if (g_original) Orig_Shutdown(); else GLoadingScreen::Shutdown();
    LogLoadState();
    if (g_original) Orig_ClearToBlack(); else GLoadingScreen::ClearToBlack();
    if (g_loadUnloading) {
        if (g_original) Orig_Unloading(); else GLoadingScreen::LoadAndDrawUnLoadingScreen();
        LogLoadState();
        if (g_original) Orig_FreeUnloading(); else GLoadingScreen::FreeUnloadingScreen();
        LogLoadState();
    }
    if (g_original)
        Orig_Status("Opening %s.viv (%d)", "misc", g_loadUpdates);
    else
        GLoadingScreen::Status("Opening %s.viv (%d)", "misc", g_loadUpdates);
}

void TestLoadingScreen(const Snapshot &before) {
    if (fgRenderer == NULL || LoadState != kLoadScreenOff || !MemoryHooked() || !DrawHooked() || !FrameHooked() ||
        !Hooked({ kHookGetTick, kHookGetFrequency, kHookTaskAdd, kHookTaskDel, kHookPrint }))
        return;
    g_eaglArena = true;
    g_compareArena = false;
    for (long i = 0; i < 24; i++) {
        g_loadPicture = Chance(50);
        g_loadKeep = g_loadPicture || Chance(50);   // a kept screen frees the picture at Shutdown
        g_loadUnloading = Chance(40);
        g_loadUpdates = Range(0, 12);
        g_fake.tickCount = Range(1, 40);
        int32_t tick = Range(0, 1000000);
        for (int t = 0; t < g_fake.tickCount; t++) {
            g_fake.ticks[t] = tick;
            tick += Chance(10) ? -Range(1, 50) : Range(0, 400);
        }
        g_fake.frequency = Chance(80) ? 1000 : Range(1, 100000);
        LitDots = Range(-1, 7);
        DotTime = Uniform(-0.1f, 0.3f);
        DotDirection = Chance(50) ? 1.0f : -1.0f;
        RunCase("GLoadingScreen sequence", i, SideLoadingScreen, before);
    }
    g_eaglArena = false;
    g_compareArena = true;
}

// ---- the string list's helpers

alignas(16) uint8_t g_listBlock[sizeof(LoadScreenStringList)];
int g_listNodes, g_eraseFirst, g_eraseLast;

void SideStringList() {
    LoadScreenStringList *list = reinterpret_cast<LoadScreenStringList *>(g_listBlock);
    LoadScreenStringNode *head = g_original ? Orig_NewHead(list, 0) : list->NewHead();
    list->head = head;
    list->size = 0;
    LoadScreenStringNode *nodes[16];
    for (int n = 0; n < g_listNodes; n++) {       // the nodes linked by hand, the same on both sides
        LoadScreenStringNode *node = static_cast<LoadScreenStringNode *>(Bump(sizeof(LoadScreenStringNode)));
        node->value.capacity = (n & 1) ? 15 : uint32_t(16 + n * 8);
        node->value.size = uint32_t(n);
        if (node->value.capacity >= 16)
            node->value.pointer = static_cast<char *>(Bump(node->value.capacity + 1));
        node->next = head;
        node->previous = head->previous;
        head->previous->next = node;
        head->previous = node;
        list->size++;
        nodes[n] = node;
    }
    LoadScreenStringNode *first = g_eraseFirst < g_listNodes ? nodes[g_eraseFirst] : head;
    LoadScreenStringNode *last = g_eraseLast < g_listNodes ? nodes[g_eraseLast] : head;
    LoadScreenStringNode *result = NULL;
    LoadScreenStringNode **answer = g_original ? Orig_Erase(list, 0, &result, first, last)
                                               : list->Erase(&result, first, last);
    Log(answer == &result);
    Log(ArenaWord(result));
    LogBytes(list, sizeof(LoadScreenStringList));
    if (g_listNodes > 0) {
        if (g_original) Orig_Tidy(nodes[0], 0); else nodes[0]->Tidy();
    }
}

void TestStringList(const Snapshot &before) {
    if (!MemoryHooked())
        return;
    for (long i = 0; i < 40; i++) {
        memset(g_listBlock, 0, sizeof(g_listBlock));
        g_listNodes = Range(0, 16);
        g_eraseFirst = Range(0, g_listNodes);
        g_eraseLast = Range(g_eraseFirst, g_listNodes);
        RunCase("the string list's NewHead/Erase/Tidy", i, SideStringList, before);
    }
}

// ---- the regions

void BuildRegions() {
    AddShared(g_galleryBlock, sizeof(g_galleryBlock), "the gallery copy");
    AddShared(g_newGalleryBlock, sizeof(g_newGalleryBlock), "the second gallery", false);
    AddShared(g_keyBlock, sizeof(g_keyBlock), "the keys");
    AddShared(g_outBlock, sizeof(g_outBlock), "the output", false);
    AddShared(g_vertices, sizeof(g_vertices), "the vertices");
    AddShared(g_fxBlock, sizeof(g_fxBlock), "the effects gallery");
    AddShared(g_fxNewBlock, sizeof(g_fxNewBlock), "the second effects gallery", false);
    AddShared(g_fxCanvases, sizeof(g_fxCanvases), "the canvases");
    AddShared(g_fxSystems, sizeof(g_fxSystems), "the particle systems");
    AddShared(g_soundBlock, sizeof(g_soundBlock), "the sound");
    AddShared(g_listBlock, sizeof(g_listBlock), "the string list");
    AddShared(&g_fakeVelocity, sizeof(g_fakeVelocity), "the fake velocity");
    if (g_hudGallery != NULL && g_hudGallery->font != NULL)
        AddShared(g_hudGallery->font, sizeof(FNTXFont), "the HUD's font");
    AddShared(SimpleMaterial, sizeof(USimpleMaterial), "the simple material");
    AddShared(SimpleTexturedMaterial, sizeof(USimpleTexturedMaterial), "the simple textured material");
    if (SimpleRequests[SimpleRequestIndex] != NULL)
        AddShared(SimpleRequests[SimpleRequestIndex], sizeof(SimpleGeoPrim), "the simple request");
    if (TexturedRequests[TexturedRequestIndex] != NULL)
        AddShared(TexturedRequests[TexturedRequestIndex], sizeof(TexturedGeoPrim), "the textured request");
    if (fgRenderer != NULL)
        AddShared(&fgRenderer->screenWidth, 0x10, "the renderer's screen");
    AddShared(reinterpret_cast<void *>(0x00243940), 4, "the active views");
    AddShared(reinterpret_cast<void *>(0x00234e30), 4, "the simulation's step");
    AddShared(reinterpret_cast<void *>(ShadowDotsFile), 0x4e0, "the dots' file", false);
    // The owned globals (pointers to each side's own loads compared in the logs instead)
    AddOwned(&LastParticleSystem, kGameLastParticleSystem, 4, "LastParticleSystem");
    AddOwned(&LineMaterial, kGameLineMaterial, sizeof(LineMaterial), "LineMaterial");
    AddOwned(&LineMaterialMade, kGameLineMaterialMade, 4, "LineMaterialMade");
    AddOwned(&GfxRandomState, kGameGfxRandomState, 4, "GfxRandomState");
    AddOwned(&GfxRandomMultiplier, kGameGfxRandomMultiplier, 4, "GfxRandomMultiplier");
    AddOwned(&TheEffectsGallery, kGameEffectsGallery, 4, "TheEffectsGallery");
    AddOwned(&LoadState, kGameLoadState, 4, "LoadState");
    AddOwned(GirlMaterials, kGameGirlMaterials, 8, "GirlMaterials");
    AddOwned(&GirlCurrent, kGameGirlCurrent, 4, "GirlCurrent");
    AddOwned(&GirlStream, kGameGirlStream, 4, "GirlStream");
    AddOwned(&GirlActive, kGameGirlActive, 4, "GirlActive");
    AddOwned(&GirlUnknownB0, kGameGirlUnknownB0, 4, "GirlUnknownB0");
    AddOwned(&GirlUnknownB4, kGameGirlUnknownB0 + 4, 4, "GirlUnknownB4");
    AddOwned(&GirlUnknownB8, kGameGirlUnknownB0 + 8, 4, "GirlUnknownB8");
    AddOwned(&GirlFps, kGameGirlFps, 4, "GirlFps");
    AddOwned(&PaletteX, kGamePaletteX, 4, "PaletteX");
    AddOwned(&PaletteY, kGamePaletteY, 4, "PaletteY");
    AddOwned(&PaletteSpeedX, kGamePaletteSpeedX, 4, "PaletteSpeedX");
    AddOwned(&PaletteSpeedY, kGamePaletteSpeedY, 4, "PaletteSpeedY");
    AddOwned(&PaletteX2, kGamePaletteX2, 4, "PaletteX2");
    AddOwned(&PaletteY2, kGamePaletteY2, 4, "PaletteY2");
    AddOwned(&PaletteSpeedX2, kGamePaletteSpeedX2, 4, "PaletteSpeedX2");
    AddOwned(&PaletteSpeedY2, kGamePaletteSpeedY2, 4, "PaletteSpeedY2");
    AddOwned(&GirlUnmirrored, kGameGirlUnmirrored, 4, "GirlUnmirrored");
    AddOwned(&GirlFrameStart, kGameGirlFrameStart, 4, "GirlFrameStart");
    AddOwned(&GirlCursor, kGameGirlCursor, 4, "GirlCursor");
    AddOwned(&GirlTime, kGameGirlTime, 4, "GirlTime");
    AddOwned(&GirlTick, kGameGirlTick, 4, "GirlTick");
    AddOwned(&GirlData, kGameGirlData, 4, "GirlData");
    AddOwned(&GirlFrames, kGameGirlFrames, 4, "GirlFrames");
    AddOwned(&GirlFrame, kGameGirlFrame, 4, "GirlFrame");
    AddOwned(&KeepLoadScreen, kGameKeepLoadScreen, 1, "KeepLoadScreen");
    AddOwned(&DotDirection, kGameDotDirection, 4, "DotDirection");
    AddOwned(&BackgroundFile, kGameBackgroundFile, 4, "BackgroundFile", false);
    AddOwned(&LoadingFont, kGameLoadingFont, 4, "LoadingFont", false);
    AddOwned(&LoadingFontFile, kGameLoadingFontFile, 4, "LoadingFontFile", false);
    AddOwned(&TitleId, kGameTitleId, 4, "TitleId");
    AddOwned(&DescId, kGameTitleId + 4, 4, "DescId");
    AddOwned(&DescLines, kGameTitleId + 8, 4, "DescLines");
    AddOwned(&HintId, kGameTitleId + 12, 4, "HintId");
    AddOwned(&LoadScale, kGameLoadScale, 4, "LoadScale");
    AddOwned(&Dots, kGameDots, 4, "Dots");
    AddOwned(&DotTime, kGameDotTime, 4, "DotTime");
    AddOwned(&LastTick, kGameLastTick, 4, "LastTick");
    AddOwned(&LitDots, kGameLitDots, 4, "LitDots");
    AddOwned(&DotCount, kGameDotCount, 4, "DotCount");
    AddOwned(&BackgroundCount, kGameBackgroundCount, 4, "BackgroundCount");
    AddOwned(&UnloadingFile, kGameUnloadingFile, 4, "UnloadingFile", false);
    AddOwned(DotTextures, kGameDotTextures, sizeof(DotTextures), "DotTextures");
    AddOwned(BackgroundTextures, kGameBackgroundTextures, sizeof(BackgroundTextures), "BackgroundTextures");
    AddOwned(&UnloadingTexture, kGameUnloadingTexture, 4, "UnloadingTexture");
    AddOwned(&ErrorFontFile, kGameErrorFontFile, 4, "ErrorFontFile", false);
    AddOwned(&ErrorFont, kGameErrorFont, 4, "ErrorFont", false);
}

}  // namespace

void GameFrontEndShadowC_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEFRONTENDSHADOWC", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    g_verbose = atoi(value) == 2;
    FpControlGet(&g_x87, &g_sse);

    FindHudPuppets();
    FindEffects();
    g_shadowThread = GetCurrentThreadId();
    g_otherThreadsTick = TIMER_gettick();
    // The file loads run the sync tasks as the real clock comes due, and the input's reads the clock: off for the run
    IOModule *io = IOModule::GetIOModule();
    bool ioUpdating = io->updatingEnabled;
    io->updatingEnabled = false;
    InstallHooks();
    // The line material is a function-local static: made here once if no draw has made it, so that neither side
    // registers its destructor in a case
    if ((LineMaterialMade & 1) == 0 && g_hudGallery != NULL && g_hooks[kHookSimpleDraw].on)
        g_hudGallery->LINE_Draw2DStrip(g_vertices, g_colours, 0);
    BuildRegions();
    Snapshot before;
    Capture(&before);

    TestGalleryQueries(before);
    TestGalleryDraws(before);
    TestGalleryConstruct(before);
    TestEffectScripts(before);
    TestEffectDirect(before);
    TestEffectConstruct(before);
    TestGirl(before);
    TestLoadingScreen(before);
    TestStringList(before);

    Restore(before);
    for (Hook &hook : g_hooks)
        HookRemove(&hook);
    ResetArena();
    ResetFpu();
    io->updatingEnabled = ioUpdating;
    printf("[gamefrontendshadowc] galleries, effects gallery, pause figure, loading screen vs originals: %ld cases, "
           "%ld checks, %ld differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[gamefrontendshadowc]   %ld calls faulted (%ld cases on both sides, not compared)\n", g_faults,
               g_bothFaulted);
    if (g_arenaFull != 0)
        printf("[gamefrontendshadowc]   %ld allocations found the arena full\n", g_arenaFull);
    if (g_skipped != 0)
        printf("[gamefrontendshadowc]   %ld tests skipped (the pause menu's figure was live)\n", g_skipped);
    fflush(stdout);
}
