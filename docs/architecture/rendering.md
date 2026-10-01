# The rendering pipeline (action engine)

How a frame of the action engine is put together above our D3D seam: which viewers exist, how visibility is
worked out (rooms, portals, frustum), how the per-viewer draw lists are built and sorted, what each pass draws
and in what order, and how meshes, skinned characters, sprites, text, fog, lights, shadows, blur and the
background movies reach `d3dSeam.cpp`. Reviewed from the Xbox `default.xbe` (all addresses are Xbox ones), with
the PS2 `ACTION.ELF` for names. Written 1 October 2026.

This builds on, and does not repeat:

- `git show blender-exports:docs/anim/spec-mesh.md` - the mesh block, `ModelData`, the draw loop in
  `RecurseAndDrawBoxes` (its section 5.6), bone palettes, morph streams, handedness.
- `git show blender-exports:docs/anim/spec-pose.md` - `AnimObjectDraw` 0x16c20, `psiBuildMatrixPalette`
  0xdd290, `psiDrawSkinObjectMatrix` 0xe0960, `d3dSetSkinMatrix` and the `SkinRegTable` register map
  (its sections 2.8 and 2.9).
- `git show blender-exports:docs/level/spec-world.md` - cels, rooms, portals, which static cels are drawn,
  the fog table, sky objects (its sections 3, 4, 7.4, 7.5).
- `docs/cxbx-removal-plan.md` and the top of `src/action/engine/Direct3D/d3dSeam.cpp` - the seam itself.

Evidence is marked in place: **(decompile)**, **(disasm)**, **(source)** for our own code, **(data)** for bytes
read from the XBE, **(inference)** for anything reasoned rather than read. Names marked "(invented name)" are
suggestions of mine, not in Ghidra or any build.

## The shape of it

```
GameFlow_Main 0x6aca0 (ours) ── Game_Run, then Game_Draw 0xdac20 unless InhibitGameDraw
Game_Draw
  psiSetScreenBlur(GlobalBlur)          GlobalBlur 0x1f667c decays by 16 a frame
  psiPreDraw 0xdd150 (ours)             gamma on first frame of a level, particle ring ageing, d3dBeginFrame,
                                        d3dClear (colour only on the front end), background movie frame
  for each of the 11 viewers: View_CaptureScene 0xdabb0 (ours)     ── visibility + draw-list build
      View_CaptureSceneSub 0xda130: portal recursion from the viewer's room → GfxList (visible rooms)
      View_AddCels 0xdaa00: rooms + their linked static cels + the objects in each → 3 sort lists
      View_AddForcedObjects 0xda250: objects flagged "always consider" (effectFlags 0x8000)
  psiFog(on, ...)                       the per-level fog table (or off)
  viewers 0-3, pass by pass:  sky (behind) · list 0 opaque · sky (in front) · list 1 alpha · shards ·
                              drops · [Z clear] · list 2 (first-person) · sprites/text · fade
  viewers 4-9: the same passes, one viewer at a time
  SprCnt = 0; psiPostDraw 0xdd8e0       viewport 640x480, psiBlurScreen, d3dSwap, SFXUpdate, rumble
per object in a list: View_DrawObjList 0xda890
  cel           → View_DrawCelGlist 0xda670 → psiDrawObjectMatrix 0xdde50 (ours) → RecurseAndDrawBoxes 0xdd4c0
  rigid object  → View_DrawObjectGlist 0xda750 → psiDrawObjectMatrix
  animated      → AnimObjectDraw 0x16c20 → psiDrawSkinObjectMatrix 0xe0960 → psiDrawObjectMatrix
                                                                         └→ maybe_psiDrawShadow 0xe04e0
  emitter       → Emitter_Draw 0x68930 → psiDrawParticleList 0xdcac0 (or one glist per particle)
everything 2D: retained sprite objects linked to a viewer → Sprite_BuildList → View_DrawSprites 0xd9410
  → SprBuffList (64) → psiDrawSprites 0xde3d0 → the seam's immediate-mode quads; text glyphs take the same path
```

## 1. Viewers

`glb_viewer[11]` at 0x1f661c holds `viewer_tag*` (0x208 bytes, `build_alloc_viewer` 0x20610, **decompile**).
`Camera_CreateCameras` (ours, `engine/Camera.cpp`) creates them at each level start **(source)**:

| index | world | role | evidence |
|---|---|---|---|
| 0-3 | `glb_world` | the player views (1 to 4, split screen) | source; `Camera_ScreenCoords` per player count |
| 4 | `glb_world` | a second world view: the scripted/cutscene camera, going by `Script_Update` and `Sensor` testing `glb_viewer[4]` | source; **inference** for the role |
| 5 | its own world + one private cel (bbox ±1000, flags 0x10040000) | the first-person weapon view: its camera is used for the mid-frame Z clear | decompile of Game_Draw; **inference** |
| 7 | its own world + private cel | unknown (an overlay 3D scene) | unknown |
| 6, 8, 10 | none | 2D only (sprites, fades). `GameFlow_Main` waits on viewer 6's fade; `LoadScreen_Draw` draws viewer 10 | decompile |
| 9 | `glb_world` | "specific to Xbox?" per our comment; role unknown | unknown |

Game_Draw captures all 11 but draws only 0-9; viewer 10 is drawn only by `LoadScreen_Draw` 0xbf9f0 **(disasm)**.
The bits of `object_display_mask` (0x29e80c, `1 << viewer->idx`) select which objects a viewer sees, against
`obj->displayMask`; `View_SetDrawIn*` (ours) set those masks (0x1f = all of 0-4).

Viewer fields the pipeline uses, beyond `viewer.h`: +0x08/0x0c/0x10 the three sort-list buffers, +0x14/0x16/0x18
their counts, +0x1a/0x1c/0x1e their capacities, +0x20 "a visible room wants the sky" (Ghidra:
`flagsFromDispList`), +0x29 3D enabled, +0x2a 2D (sprites) enabled, +0x6c four frustum planes, +0x1f4 a light
reference object for `View_SetupRenderModes2`, +0x200..0x203 fade RGBA, +0x205 fade active, +0x206 night
vision mode (0 off, 1 night vision, 2 the mode that also shows effectFlags-0x400 objects) **(decompile)**.

The sort lists are allocated by `Camera_Enable` 0x24150 when a viewer's 3D is switched on: capacities from the
table at 0x163a28 = {0x200, 0x200, 0x80} entries of 12 bytes `{float key; obj_tag *obj; cel_tag *cel}`,
halved in multiplayer **(decompile, data)**. Adding an entry does `count = (cap - 1) & (count + 1)`: a full list
silently wraps to 0 and loses everything before it (**decompile**; no overflow check exists).

## 2. Visibility: rooms, portals, frustum

All per viewer, inside `View_CaptureScene` (ours: `game/view.cpp`, which also owns `View_CaptureSceneSub`
because the original passes its arguments in registers).

1. Every cel's `addedToDraw` (+0x90) is cleared, `GfxList` (0x29d79c) emptied.
2. `Vision_Init_Portal_Recurse` 0xdb940 → `Vision_Calculate_View_Fustrum` 0xdb5c0: five points (eye plus the
   four corners of a rectangle 200 units ahead, from `fovRadians` and `AspectRatio`) through the view matrix
   into `ViewPoints`, five planes into the depth-0 clip stack at 0x29ea60 (count 5 at 0x29e998); the first four
   (the sides) are copied into `viewer->frustumPlanes` **(decompile)**. The fifth is a far plane at 200 units.
3. Unless `switch_ForceDrawAll` (0x1dfa18): `Vision_Portal_Recurse` 0xdbc40 adds the viewer's own room
   (`viewer->someCel`) and calls `Vision_Recurse` 0xdb9f0. For each portal of a room that is open (+0x27), faces
   the eye, and whose door (`Door_IsClosed`) is not closed, the portal quad is clipped by
   `ClipPortalWithPlane` 0xdb7c0 against every plane of the current depth's stack (Sutherland-Hodgman, edge
   intersection in `FUN_000db730`); if three or more points survive, the far room is added
   (`Vision_AddCelToDraw` 0xdb540), both portals are marked seen (+0x26), `FUN_000db970` builds the next depth's
   stack (one plane per clipped edge through the eye, plus the portal's own plane; 10 planes of 16 bytes per
   depth) and the recursion continues. Limits: 101 calls (`VisionRecurseCount`), depth 46, 64 rooms
   (`VisCelCount`) **(decompile)**.
4. `vision_generate_display_list` 0xdb4b0 sorts the rooms (`Compare_DrawCels`, by recursion depth by the look of
   the 8-byte `{cel, depth}` entries) and chains them through `cel->maybeNextCelInDisplayList` from `GfxList`. It
   sets viewer+0x20 if any visible room lacks cel bit 0x10000000 (the private cels of viewers 5 and 7 have it, so
   they never draw the sky) **(decompile)**.
5. `vision_GetCamPos` stores the eye in `CamPos` (0x29dbf0), used for every sort key.

Objects and static cels are then tested only against the four side planes (`Vision_InView` 0xdbc80, ours: a
sphere against four planes, no near or far test). The rules for which static cels and children are added are in
spec-world 3.4-3.5; the object rules are in section 3 below.

## 3. Building the draw lists

`View_AddCels` 0xdaa00 (original, called through a register wrapper in `view.cpp`) walks `GfxList`. Each room
with a glist that lacks entity flag 0x10 goes into list 0 with an increasing key (-90000, +10 per room, so rooms
draw in portal order); then `View_AddObjects` 0xda490 for the room's objects; then each linked static child
(`cel+0x18` chain) that passes the flag rules and `Vision_InView` goes into a list by flags, keyed by squared
distance to `CamPos`, followed by its own objects **(decompile)**.

| list | chosen by (cel flags +0x84, or obj `effectFlags`) | key | drawn |
|---|---|---|---|
| 0 | default | +distance² (rooms: -90000 + 10n) | first, with colour blend 2 |
| 1 | bit 0x1 (transparent) | -distance², so sorted far to near | after the in-front sky, blend 1 |
| 2 | bit 0x2000 | +distance² | after the optional Z clear: first-person items |

Objects (`View_AddObjects`; the same test in `View_AddForcedObjects` 0xda250 for objects with effectFlags bit
0x8000, over the whole object list): drawable if `rendererType & 0x110` or a glist, visible to this viewer's
mask, not effectFlags 0x10 or 0x8000 (0x8000 ones come in through the forced pass), inside the frustum, and not
0x400 unless night-vision mode 2. `rendererType & 8` (animated) multiplies the key by 50; `& 0x10` adds 2000.
For animated objects the key also sets the animation LOD byte for this viewer (`lod[min(idx,3)] =
clamp(dist² / projScaleZ / 4, 0, 255)`), which is how distance-based animation LOD is driven by the renderer
**(decompile)**. Drones bigger than 0.5 radius in the forced pass are accepted only if `Collide_StraddleCels`
puts them in a visible room.

`View_CaptureScene` accumulates the three counts into `Tots`/0x29e804/0x29e808; a non-zero list-2 total is what
makes Game_Draw clear Z before list 2 **(source, decompile)**.

## 4. A frame, pass by pass

`View_DrawObjList` 0xda890 quicksorts a list by key (`Compare_AlphaObj`) and draws each entry, clearing
`bCharacterLight`/`bShadowCharacter` (0x1d7824/0x1d7825) per entry and setting them from `obj->flags` bits 2
and 3 **(decompile)**:

- a cel: `View_DrawCelGlist` (spec-world 3.2): `FUN_000d9bd0` (EAX = cel; the cel's alpha mode from +0x93 into
  the tweak colour, then the closest lights), the matrix, `psiDrawObjectMatrix`, then the attached sub-objects.
- an object, `rendererType & 8`: `AnimObjectDraw` (spec-pose 2.8).
- otherwise: `View_SetupRenderModes` 0xd9900 (the tweak colour: brightness, the object tint, the ambient from
  `Lights_CalcAmbientLight` when effectFlags 0x800, the "flashing objects" pulse within 4 units), the closest
  lights, then `View_DrawObjectGlist` (billboard if effectFlags 8, jitter if 4), `Emitter_Draw` (rendererType
  0x100), or for rendererType 0x10 a call to 0xe0ec0, which is a bare `RET` **(disasm)**: the PS2's
  `psiDrawParticles` hook is empty on Xbox, so those objects draw nothing on this path.

0xe0ec0 is also every other PS2 "psi hook" Ghidra labelled in Game_Draw and its callees (`psiPostSolid`,
`psiEndAlpha`, `psiEndSprites`): the linker folded them onto one empty function **(disasm)**.

Game_Draw's order for viewers 0-3, each pass looping over the four viewers before the next pass; a viewer
whose camera is not current gets `psiUseCamera` first **(decompile + disasm)**:

| # | pass | call | condition |
|---|---|---|---|
| 1 | sky objects behind the world | `View_DrawSky(v, 1)` | 3D on, viewer+0x20 |
| 2 | opaque list | `psiSetUpColourBlend(2)`, `View_DrawObjList(v, 0)` | list 0 non-empty |
| 3 | sky objects in front | `View_DrawSky(v, 0)` | as 1 |
| 4 | alpha list | `psiSetUpColourBlend(1)`, `View_DrawObjList(v, 1)` | list 1 non-empty |
| 5 | glass shards, rain/snow drops | `psiSetShardRenderStates`, `DrawAllShards` 0x1fe50, tint white, lights off, `DrawDrops` 0xa3560 | 3D on |
| 6 | Z clear | `psiUseCamera(glb_viewer[5])`, `psiClearZ` | any viewer had list-2 entries |
| 7 | first-person list | `View_DrawObjList(v, 2)` | list 2 non-empty |
| 8 | sprites and text | `Sprite_BuildList` (ours), `View_DrawSprites` | 2D on |
| 9 | fade | `psiFadeView` (one full-screen sprite) | viewer+0x205 |

Viewers 4-9 then run 1 (when 3D is on or viewer+0x20 is set), 2, 3, 4, 5 (viewer 4 only), 7, 8, 9 one viewer at a time, with no second Z clear; an extra sprite call before pass 2 never runs (12, item 7). Fog is set once
before the passes, and `View_DrawSky` turns it off for the sky and, oddly, back on with fixed values
(0-20, rgb 100,100,150) rather than the level's (**decompile**; the level's values are what the world gets only
because the per-level `psiFog` table overrides arguments for fogged levels and switches fog off for the rest).

`psiUseCamera` 0xdd950: viewport from xMin..yMax; `createProjectionMatrix(aspect, fov = fovRadians / projScaleZ
in degrees, near 0.01, far 3000)`; the view matrix copied to 0x2adec8 (15 floats; its translation at 0x2adef8 is
the camera position the shadow code reads), its x axis negated (the left-handed mirror in spec-mesh 6), and
handed to `d3dSetViewMatrixFromRigidTransform`. `projScaleZ` (the zoom) is also kept at 0x194814 for particle
sizes **(decompile)**.

`psiPostDraw`: full-screen viewport, `psiBlurScreen(ScreenBlur)` when non-zero and `MenuManager_GetStatus() !=
3`, `d3dSwap`, then `SFXUpdate` and `psiInput_RumbleUpdate` - the frame end is also the audio and rumble tick
**(decompile)**.

## 5. Meshes and skinned characters to the GPU

Every 3D draw funnels into `psiDrawObjectMatrix` (ours, `engine/psiGraphics.cpp`): transpose the engine's
`_MATRIX` to D3D, `d3dSetMatrix`, `RecurseAndDrawBoxes(celglist->geom_idx)`. Its callers **(xrefs)**:
`View_DrawCelGlist`, `View_DrawObjectGlist`, `View_DrawGlist` (ours; only from `AnimObjectDraw`),
`View_DrawSky`, the sub-object drawer 0xda1d0, `AnimObjectDraw` (datums), `psiDrawSkinObjectMatrix`,
`maybe_psiDrawShadow`, `Emitter_Draw`, `DrawDrops`.

`RecurseAndDrawBoxes` 0xdd4c0 is the per-primitive state machine (spec-mesh 5.6): one buffer bind, then per
12-byte strip record optional palette upload, render-state changes by a change mask, texture frame from `Tex[]`
(animated by `NumFramesUnpaused / animSpeed % numFrames`), and `d3dDrawIndexedVertices` (ours, `FUNC_AT`). No
recursion and no boxes, despite the name. Ghidra ends the function at 0xdd56d; the body runs to 0xdd8d2.

Skinning: `AnimObjectDraw` builds the palette (`psiBuildMatrixPalette` 0xdd290, which the coverage list files
under platform.input), then per rigged glist `psiDrawSkinObjectMatrix` sets `g_skinPalette` (0x2adf28) and draws
with the identity matrix at 0x2ade8c; the palette rows go to the vertex shader per primitive by
`d3dSetSkinMatrix`. The GPU does the skinning (two bones, one weight); the PS2's CPU `SkinCache` has no Xbox
counterpart (spec-pose). Morph weights reach the shader as streams set in the same loop.

Level data registration: `psiCreateMapTextures` and `psiCreateEntityGfx` (both ours) fill `Tex[2048]`
(0x2abe80) and `d3dGeometryObjs[2048]` (0x2a0e68); `maybePsiResetResources` (ours) clears both per level.

## 6. Lights, tint, fog, gamma

- **Tint**: `psiSetTweakARGB` 0xddf00 / `psiSetTweakA` 0xddf50 keep a packed ARGB at 0x2adf2c and push it to
  `d3dSetColorConstant67`. It carries per-object alpha, tint, room ambient and the cel alpha animation. This
  constant is the main per-draw colour input to the shaders **(decompile)**.
- **Dynamic lights**: `Lights_CalcClosestLights` 0x6d8f0 scans the active `LightList` (up to 30, into 0x215718,
  sorted by normalised distance; type 2 lights use a 2D distance within 4 units and only count when bit 0x80 is set in the flags passed in), and
  `psiLight_SetLights` 0xde610 sends the first four to `d3dSetLight` (direction/position, range, colour ×
  intensity) and disables the rest. Lights are set per draw-list entry **(decompile)**. `psiLight_Create`/
  `Delete` are 16-byte stubs.
- **Ambient**: `Lights_CalcAmbientLight` 0x6d790 eases an object's three ambient bytes (+light_related1..3)
  towards its room's ambient for the room's current switch-channel state (doubled for effectFlags 0x10000 while
  a player flag is set); `View_SetupRenderModes` multiplies the tint by it. World meshes are prelit (spec-world
  7.6).
- **Character light**: `bCharacterLight` makes `RecurseAndDrawBoxes` call `gfxSetCharacterLightIntensity(0.5)`.
- **Fog**: `psiFog` 0xddb00 - a hard-coded per-level table (spec-world 7.4); any other level gets fog off.
  `psiSetUpColourBlend(n)` is just `d3dSetupRenderStatesAndFog(n == 0)`, so the "blend mode" passed per pass is
  really the seam's fog/blend state machine **(decompile)**.
- **Gamma**: `ConfigureGammaForLevel` 0xdce50 (first frame of a level, from psiPreDraw) sets a per-level ramp,
  but only when `Graphics_IsSomeGraphicsRegion()` is true; `Graphics_Init_LowLevel` (ours) sets the boot ramp
  the same way.

## 7. Shadows, blur, particles, shards

- **Character shadows** (`maybe_psiDrawShadow` 0xe04e0, from `psiDrawSkinObjectMatrix` when
  `bShadowCharacter`): skipped in the AllCharacters levels and for script camera 0x600068f, and beyond 35 units
  from the camera. A ray down from the character (`Collide_unknown`, mask 0x307) collects up to 300 ground
  triangles. Within 20 units the character's skinned mesh is redrawn into the aux render target from 20 units
  above (`d3dBeginEndAuxRenderPass`), and blurred (`psiBlurCharacterShadow`) within 4 units; further out the
  blob texture 0x3000047 is used. The ground triangles are then drawn as a "shard" (`drawShard`) with a
  projective texture matrix (`maybeBuildAndSetModelViewProjectionMtx`), alpha fading with height difference
  **(decompile)**. All the GPU work is already in the seam; this function is the policy.
- **Screen blur**: `GlobalBlur` (0x1f667c) set by game code, clamped to 0x80 into `ScreenBlur` (0x2adf33),
  applied in `psiPostDraw` by `psiBlurScreen` (ours: a 3-slot history-texture afterimage).
- **Particles**: `psiDrawParticleList` 0xdcac0 (from `Emitter_Draw` and `DrawDrops`) copies positions into a
  36-byte-per-particle buffer, registers it with `d3dRegisterOverlayBuffer`, parks it in the 64-entry
  `ParticleOverlayRing` (0x2adf88) for three frames (the GPU may still read it; `psiAgeParticleOverlayRing`, ours,
  frees it), and draws point sprites with `d3dDrawOverlayQuad`, size `scale × 100 × zoom`. It calls `__WBINVD`
  directly (not patched like the psiPreDraw one; see 10) **(decompile)**.
- **Shards**: `psiDrawShard` 0xdf9f0 (one textured triangle per call through `drawShard`); `psiGetTriList`
  0xdf6c0 is used only by `Break_ShatterWindow` to cut a mesh into shards, so it is breakage, not drawing.

## 8. Sprites, text, fades, loading screen, movies

**Sprites** are retained objects (`sprite`, 0x44 bytes, pool of 0x200; all `Sprite_*` lifecycle functions are
ours in `gfx/Sprite.cpp`), each linked to a viewer number. The HUD, menus and load screen create and update them;
the renderer only collects them. `View_DrawSprites` (original) turns each into `SPRITE_DRAW`s (0x30 bytes) in
`SprBuffList[64]` (0x29dc00): tiling (flag 0x400), centring (0x800), flips (0x40/0x80), a black drop-shadow copy
(0x20), offset by the viewer's xMin/yMin. Text sprites go to `Font_DrawText` 0x6a340 → `__Font_DrawText` 0x69c50,
which takes one slot per glyph through `View_AddSprite` (ours). `psiDrawSprites` 0xde3d0 binds the texture frame
and fog/blend state, batching runs with the same texture and mode into `maybeImmediateModePushItem` /
`maybeImmediateModeFlush`. There is no separate 2D renderer: HUD, menus, subtitles, fades and the load screen are
all this path **(decompile)**.

**Fades**: `psiFadeView` draws one 640x480 untextured `SPRITE_DRAW` in the viewer's fade colour.

**Night vision** (inference from `ui/HUD.cpp` and the flags above): no shader effect is involved; mode 1 tints
the HUD overlay sprite and quarters the brightness of effectFlags-0x20000 objects, mode 2 also shows the
effectFlags-0x400 objects. Not traced to the end.

**Loading screen**: `ResetMap_GenLoadScreen` 0xbfa60 builds sprites on viewer number 99 (level art plus
word-wrapped hint text), draws them four times with `LoadScreen_Draw` (a self-contained frame: `psiPreDraw`,
`psiUseCamera(glb_viewer[10])`, sprites, `psiPostDraw`), then `LoadingScreenMakeVisible`. During the blocking
load `FS_AllocateAndLoadBlocking` calls `ShowLoadProgressScreen` (ours: the six dots, its own
`d3dBeginFrame`/`d3dSwap`) **(decompile, source)**.

**Background movies**: `BackgroundMoviePlayFile` 0xe8a90 creates two 640x480 YUY2 textures (format 9) in special
memory and an XMV decoder (`maybeCreateVideoDecoder`, the 60-function `lib.xmv`, all original). Each frame
`psiPreDraw` → `maybeStartBackgroundMovie` (ours) → 0xe8cf0 decodes the next frame into one surface and draws the
other as a full-screen immediate quad with `d3dSetYuvEnable(1)` - before any 3D, so the world draws over it
**(decompile)**. 0xe8cf0 is named `maybeDecodeMpgAudio` in Ghidra but decodes and draws video;
`BackgroundMovie_DecodeAndDraw` (invented name) would fit. The decoder reaches D3D only through the four entry
points the seam answers itself (`d3dGetDisplayMode`, `d3dGetRasterStatus`, `d3dGetSurfaceDesc`,
`d3dLockSurface`).

## 9. The boundary with d3dSeam

Everything named `d3d*` in 0xe3a20-0xe75f0 is ours (70 of 73 `d3d*`-named functions replaced; the two dead ones
are `d3dComputeSwizzleMasks` and `FUN_000e64c0`; `d3dMatrixIdentity` 0xe81c0 is still original), as is `Gfx`,
the graphics global (originally 0x2c5750, 0x39ce4 bytes, now defined in `d3dSeam.cpp`). Under the standalone
loader the seam drives `src/common/gfx/d3d9Backend.cpp`, translating NV2A vertex programs to HLSL
(`nv2aPixelShader.cpp` for the combiners) with optional FXAA and texture replacement. The calls the layer above
makes are few and stable:

| group | seam entry points used from above |
|---|---|
| frame | `d3dBeginFrame`, `d3dClear`, `d3dSwap`, `d3dSetupViewportDimensions` |
| camera, transforms | `createProjectionMatrix`, `d3dSetProjectionMatrix`, `d3dSetViewMatrixFromRigidTransform`, `d3dSetMatrix`, `d3dSetWorldMatrix`, `maybeBuildAndSetModelViewProjectionMtx` |
| mesh draw | `d3dBindBuffers`, `d3dSetStreamSources`, `d3dSetSkinMatrix`, `d3dDrawIndexedVertices`, `d3dsetVertexShader(Constant)` |
| state | `d3dSetupRenderStatesAndFog`, `maybeResetRenderState`, `d3dSetRenderState{,1,2}`, `d3dSetCullMode`, `d3dSetDeferredTextureState`, `d3dSetTextureStage0/1`, `d3dSetTextureWithBorderColor`, `d3dSetYuvEnable` |
| colour, light, fog | `d3dSetColorConstant67`, `d3dSetLight`, `d3dDisableLight`, `gfxSetCharacterLightIntensity`, `d3dSetFogEnable/Near/Far/Color`, `ConfigureGammaRamp` |
| 2D | `maybeImmediateModePushItem`, `maybeImmediateModeFlush` |
| special | `d3dBeginEndAuxRenderPass`, `psiBlurCharacterShadow`, `psiBlurScreen`, `d3dRegisterOverlayBuffer`, `d3dDrawOverlayQuad`, `drawShard` |
| resources | `RegisterTexture`, `d3dCreateVertexBuffers`, `d3dCreateIndexBuffer`, `d3dGetTextureSurfaceLevel0`, the slot-table readers |

The seam keeps the original's caching and dirty flags, so the calling order above it matters (redundant state
calls are cheap; missing ones leave stale state from the previous draw).

## 10. Connections to other subsystems

- **Game flow**: `GameFlow_Main` (ours) decides whether Game_Draw runs (`InhibitGameDraw`, `sloflag`); FMV and
  fade game states rely on viewer 6's fade and the movie state.
- **Animation**: the renderer sets animation LOD per viewer (section 3) and `AnimObjectDraw` builds the palette at
  draw time, skipping objects not updated in the last frame. Drawing therefore has an effect on animation state.
- **Collision**: `build_link_objects_to_rooms`, the forced-object test (`Collide_StraddleCels`) and the shadow
  ray use the collision module.
- **Objects**: `obj->rendererType`, `effectFlags`, `displayMask`, tint bytes, `inCel` and the cel object chains
  (`Cel_ObjectLeftCel` 0xd8810, `build_LinkToRoom` 0x21110) are the renderer's inputs.
- **Doors**: portals close with their door (`Door_IsClosed` in `Vision_Recurse`).
- **Audio and input**: `View_DrawSky` plays thunder; `psiPostDraw` ticks `SFXUpdate` and rumble; the movie
  path calls `DirectSoundDoWork`.
- **UI**: the HUD and menus are sprite producers; `MenuManager_GetStatus` gates the screen blur.

## 11. What is ours and what is not

From the coverage list (`tools/function_coverage.py` output, R replaced, D dead, L live):

| group | functions | R | D | L | notes |
|---|---|---|---|---|---|
| render (View_*, Vision_*, Font_*, Sprite_*, Game_Draw) | 59 | 19 | 1 | 39 | ours: matrices, `View_AddSprite`, `View_CaptureScene` (+Sub), `View_3DPoint2Screen`, `View_SetDrawIn*`, `Vision_InView`, 8 of 11 `Sprite_*`. All 8 `Font_*` live |
| engine.world (build_*, Cel) | 14 | 2 | 0 | 12 | `build_link_world_to_viewer`, `build_PointOnFloor` |
| engine.lighting | 11 | 7 | 1 | 3 | live: `Lights_CalcAmbientLight`, `Lights_CalcClosestLights`, `Light_SetAmbientRadiators` |
| platform.gfx, not `d3d*` | 59 | 13 | 2 | 44 | ours: `psiPreDraw`, `psiDrawObjectMatrix`, the two create functions, movie start/stop, loading screen, `createProjectionMatrix`. Includes misfiled input/driving/matrix helpers |
| platform.movie | 5 | 0 | 0 | 5 | plus `lib.xmv`: 60 live |
| `d3d*` seam | 73 | 70 | 2 | 1 | |
| draw functions filed elsewhere | 8 | 0 | 0 | 8 | `AnimObjectDraw`, `psiBuildMatrixPalette`, `Emitter_Draw`, `DrawDrops`, `DrawAllShards`, `Draw_MuzzleFlash`, `LoadScreen_Draw`, `ResetMap_GenLoadScreen` |

So the seam is done and the layer above it is about one quarter ours by count, much less by size: Game_Draw,
the visibility recursion, the list builders, `RecurseAndDrawBoxes`, the shadow, particle and sprite drawers and
the font code are all original.

## 12. Known, uncertain, unknown

Well understood (read from code, and partly exercised by the exporters on `blender-exports`): the pass order,
the three lists and their keys, the mesh draw loop, skinning, the tint/light/fog inputs, the shadow policy,
sprites and fades, the movie path, the room/portal data.

Uncertain or unknown, with the risk each carries:

1. **Viewer roles 4, 5, 7 and 9** are partly inferred (table in section 1). A reimplementation of Game_Draw must
   keep its odd per-viewer conditions exactly (viewers 4-9 run passes in a different order from 0-3; shards and
   drops only for viewer 4; list 2's Z clear uses viewer 5's camera) until the roles are confirmed.
2. **Most viewer fields are unnamed** (`viewer.h` is a field dump). The fields in section 1 are from this review
   and should go into the header with the evidence before Game_Draw is written.
3. **The portal far plane**: the fifth frustum plane sits 200 units out and is part of the depth-0 clip stack,
   so a room reached only through a portal more than 200 units away should be culled (decompile; not checked at
   run time). The camera far plane is 3000 and fog ends at 240 or less, so it is plausibly intended. Worth a
   runtime check on a large outdoor level before it is "fixed" by accident.
4. **Silent limits**: 64 visible rooms, 101 recursion steps, depth 46, sort lists of 0x200/0x200/0x80 that wrap
   to empty when full (halved in multiplayer), 21 sky slots, 64 sprites per flush, 4 lights per draw. None are
   checked. These are the places for GameCube-style `NF_WARN`s (docs/gamecube-checks.md) when reimplemented.
5. **Register calling conventions**: `View_CaptureScene` (EAX), `View_CaptureSceneSub` (register mask + viewer),
   `View_AddCels` (ESI), `FUN_000d9bd0` (EAX = cel), the sub-object drawer 0xda1d0 (EAX, EDI). Every one needs a
   naked wrapper while any original caller remains, and a wrong one fails silently (see the ESI note in
   `view.cpp`).
6. **Ghidra artefacts that mislead**: `RecurseAndDrawBoxes`'s function end (0xdd56d vs 0xdd8d2; coverage and
   injection tools see a short function); two different functions both named `View_SetupRenderModes` (0xd9900
   sets the tint, 0xd9af0 wraps it and adds lights - `View_SetupObjectTint` (invented name) for the first would
   remove the clash); 0xda1d0 `View_maybeDrawNightVisionExtras` draws an object chain's attached sub-objects
   (`View_DrawSubObjects` (invented name)); `maybeDecodeMpgAudio` (section 8); `psiDrawSprites`'s decompile
   reads its count from the wrong stack slot (its callers pass `(SPRITE_DRAW*, count)`).
7. **Sprite ranges**: Game_Draw draws `[lastForeground, count)` from `Sprite_BuildList` for every viewer (the
   `[0, lastForeground)` call for viewers 4-9 is gated on the start index, which is always 0, so it never runs)
   **(disasm)**. `LoadScreen_Draw` passes the start index (0) instead. `gfx/Sprite.cpp`'s comment used to say
   both callers ignore `lastForegroundOut`; it has been corrected. It only matters when two or more sorted sprites
   have priority 50 or more: all but the last of them are skipped in the game's views. Check the sort direction of
   `Compare_Sprites` before deciding whether that is a visible quirk or never happens.
8. **`d3dSetLevelDirectionVector`**: set per level by `maybePsiResetResources`; no reader found. The fog colour
   mask flag in the seam (`d3dSetFogColor`) is also untraced.
9. **Particles of rendererType 0x10** draw nothing on Xbox (the empty hook). Whether any Xbox level has such
   objects, and whether the PS2 drew something there, is unchecked.
10. **`__WBINVD`** is patched out of psiPreDraw by our startup code but still called directly in
    `psiDrawParticleList` and the movie decoder; harmless on the host if the patch covers the instruction,
    otherwise a privileged instruction. Confirm how `XboxStartup.cpp` handles the 0xdfb20 thunk.
11. **Front-end menus**: which viewer the menus' sprites and the 3D menu models use, and whether viewer 7 is the
    menu's 3D scene, is not traced. The UI pass (`docs/ui`) is the place to settle it.
12. **Night vision** (section 8) is inferred from the HUD code, not traced through the draw.
13. **Gamma** runs only when `Graphics_IsSomeGraphicsRegion()` is true; what that region test is (PAL? a video
    flag?) decides whether players outside it ever see the per-level ramps.

## 13. Suggested order

Ordered so each step is testable on its own with the existing replays and shadow tests (docs/targeted-testing),
leaves before orchestrators, and register-convention functions after their callers are ours:

1. **State leaves** (small, pure, no lists): `psiSetTweakARGB`/`A`, `psiSetUpColourBlend`, `psiSetScreenBlur`,
   `psiClearZ`, `psiFadeView`, `psiPostDraw`, `psiUseCamera`, `psiFog`, `ConfigureGammaForLevel`,
   `psiLight_SetLights`, `Lights_CalcClosestLights`, `Lights_CalcAmbientLight`, `psiSetShardRenderStates`,
   `psiDrawShard`. They own the colour constant 0x2adf2c and the camera copy 0x2adec8, which later steps need as
   globals of ours (`tools/global_clears.py`).
2. **Per-entry drawers**: `View_SetupRenderModes` (both), `View_SetupRenderModes2`, `FUN_000d9bd0`,
   `View_DrawObjectGlist`, `View_DrawCelGlist`, 0xda1d0, `View_DrawSky`. Each ends in `psiDrawObjectMatrix`
   (ours), so a frame-dump comparison (`drive_game.ps1`) checks them directly.
3. **2D**: `psiDrawSprites`, `View_DrawSprites`, then the `Font_*` set. High visibility (every menu and HUD
   frame), and the MenuProbe A/B replay harness covers it; settle item 7 first.
4. **`RecurseAndDrawBoxes`**: the hottest function; reimplement from spec-mesh 5.6 with the full 0xdd4c0-0xdd8d2
   body. With it, every mesh draw from `psiDrawObjectMatrix` down is ours.
5. **Visibility and lists**: `Vision_*` (fixing the limits as warnings, not behaviour), `View_AddObjects`,
   `View_AddForcedObjects`, `View_AddCels`, `View_DrawObjList`. Doing these together removes most of the register
   wrappers in one go. Name the viewer fields first (item 2 above).
6. **`Game_Draw`** last among the render functions: once its callees are ours it is a transcription, and keeping
   it original until then preserves the exact pass order while the parts change.
7. **Effects drawers** with their owners: `psiDrawSkinObjectMatrix` and `maybe_psiDrawShadow` with the animation
   work (`AnimObjectDraw`), `psiDrawParticleList` with `Emitter_Draw`/`DrawDrops` (effects, 0% ours).
8. **Movies**: `BackgroundMoviePlayFile` and 0xe8cf0 are short, but they sit on `lib.xmv` (60 functions, about
   157 KB). Replacing the decoder (with a host video library reading the same `.xmv` files, if one can) is a
   separate project; until then these two can stay original, since the decoder already talks only to seam
   entry points.
