# The driving engine's renderer

The game's renderer above EAGL ([eagl.md](eagl.md)), on top of the world, cameras and animation already ported
([world.md](world.md), [camera.md](camera.md), [anim.md](anim.md)): the renderer core and its instance draws, the
materials, lights, render states and texture contexts, scene objects and culling, the high-level renderer that
brings it all up and draws the frame, and the effects - reflections, colouring, decals, lens flares, glare,
lightning, the glow, shadows, sky, water, broken windows and particles. Ported on 9-10 October 2026 by seven
packages, then integrated (`src/driving/render/`). Every address is Driving.xbe's.

**Status:** about 560 functions ported, including entry points Ghidra had not made functions (PackedColour's
constructor 0x00076240, the state and texture context namespaces' NameLookup thunks and their managers' Kill,
RLightning's segment constructor, RShadowMap's LookupVariable, Kill and deleting destructor thunk, the particle
library's two destructors, the GIOT namespace's deleting destructor and others). `engine.render` is 100% ours by
bytes; the 31 functions the coverage tool still counts live are exception funclets. Checked by seven shadow tests
against the originals at the first tick on missions 1, 4 and 6, all with no differences, and by lockstep runs of
missions 1-8, every frame pixel-identical to the baseline.

## Layout

| File | What |
|---|---|
| `render/Renderer.h/.cpp`, `Draw.h/.cpp`, `Fog.h/.cpp`, `Materials.h/.cpp` | RRenderer (EAGL's device, render context and full-screen viewport, the frame bracket, the screen capture, the debug font, two draw groups of translucent instances deferred to the end of a view), RRenderSharedData (the lighting per view port, world instance and scene object), the instance draws (a model picked by distance, lit and drawn with its effects), view distances and the shadow-map lookups; Draw/SimpleDraw (screen-space sprites, boxes, quads, two 120-vertex batches) and ColourConvertXBoxToPS2; RFog; the simple materials (USimpleMaterial, USimpleTexturedMaterial, UVolatileMaterial, each a ring of sixteen draw requests) and TexturedGeoPrim |
| `render/RenderHigh.h/.cpp`, `WorldCulling.h/.cpp`, `RSceneObj.hpp/.cpp` | RRenderHigh (the players' views as split screens, the HUD and debug views, the frame; InitGameRender and KillGameRender make and take down the renderer's managers through their USingleton Init copies, and the per-track statics); RRenderWorldCulling (the camera's frustum in the ground plane); RSceneObj (0x40, the game's 19-slot vtable) and RAutonomousObj (0x80) as real classes |
| `render/Lights.h/.cpp`, `PathEngine.h/.cpp`, `OffscreenBuffer.h/.cpp`, `DebugView.h/.cpp` | RLightManager (the light sets per environment, the specular matrix, up to four positional lights a frame) and RHighLevelLightManager (effects' lights, explosions, missiles); the path engine (RPathHandle, `fgPathHandles`) and the camera helpers beside it; ROffscreenBuffer; the two debug views and RRandom::StartUp. `RPathHandle.hpp` only includes PathEngine.h |
| `render/StateManager.h/.cpp`, `TextureContext.h/.cpp`, `RenderTree.h`, `SkeletalObj.h/.cpp`, `TimeData.h/.cpp` | RStateManager (shared GeoPrimStates made from state strings), the texture contexts (RTextureContext, RTexList, RTextureContextManager), the red-black tree algorithms written once as templates for each compiled copy, RSkeletalObj (bones and springs), RTimeData |
| `render/Reflection.h/.cpp`, `Colorize.h/.cpp`, `Decals.h/.cpp`, `Gain.h/.cpp`, `LensFlare.h/.cpp` | FeatureManager and RReflection (the sphere map from two 256x256 views, the four nearest scene objects, the per-car lighting records); RColorize (motion blur and the SetEnabled modes); RDecalManager (a ring of 168 quads of eight types), REmp's GeoPrim and CylinderSection; RGain; RLensFlareManager |
| `render/Lightning.h/.cpp`, `PostProcessing.h/.cpp`, `ShadowMap.h/.cpp`, `SkyWater.h/.cpp`, `RGlareManager.hpp/.cpp` | RLightning (bolts as trees of segments, drawn as one strip), RPostProcessing (the glow), RShadowMap (projected and simple car shadows), RSky, RWater, RWindow (the broken panes), RGlareManager (still an overlay class) |
| `render/Particles.h/.cpp`, `ParticleCache.h/.cpp`, `Particulate.h/.cpp` | the particle library, systems and manager (`fgParticleSystems`), RParticle and the double-buffered cache, RParticulate |

Shared types the renderer brought with it now have one definition each: RLightManager and its LightBlock in
Lights.h (the characters and the world's draws light through it); TexturedGeoPrim and the three materials in
Materials.h; ArticleEffect (world/World.h) with a union of per-type views (gfx, light, locator, spring);
ReverseDrawList (camera/DirectorQueue.h) as the renderer's translucent list; the Simulation and PVehicle calls by
address in physics/Simulation.h. `render/RenderState.hpp`, which declared calls to the original EAGL and material
methods, is gone: every one had a port.

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_RENDERERSHADOW` | first tick | ColourConvertXBoxToPS2, GetTextureVal and the shadow brightness, view distances from random cameras, RFog on copies, Draw's boxes, sprites, quads and batches, the materials' draws, the draw group's vector (every insert branch) and its flush, the instance draws with random transforms and flags, RRenderSharedData; draws and render-state calls go to recording fakes |
| `NIGHTFIRE_LIGHTSHADOW` | first tick | every RLightManager query and setter on the live manager, perturbed; RHighLevelLightManager's adds, the light effect in every blink shape, Process and FinishLights; the live path handles copied and run through Init and Update; the path list on lists of our own; the camera helpers, RoadNavLaneCount, RRandom::StartUp |
| `NIGHTFIRE_SCENEOBJSHADOW` | first tick | RRenderWorldCulling on random frustums (NaN among them) and circles and squares round them; every live scene object's queries and transforms; the culling walk on WRender's list and random ones; collision info, visibility and map nodes on copies; RearrangeSplitScreens for one to four views |
| `NIGHTFIRE_TEXCONTEXTSHADOW` | first tick | the state handlers and ParseFirstStateTag on random and broken strings, RStateManager's set, RTexList and the texture context trees on scratch maps, lookups in the live contexts, the springs on synthetic bodies, RTimeData |
| `NIGHTFIRE_REFLECTIONSHADOW` | first tick | the sphere map maths and CylinderSection, RReflection's queries and its scene object slots, RColorize in every mode, the lens flares' bookkeeping round the eye, the decal ring, RGain at the identity, FeatureManager |
| `NIGHTFIRE_RENDERFXSHADOW` | first tick | RLightning on private copies (bolts added, updated, drawn into the strip, reset) with the random generators' states compared, the glare types' texture coordinates, the broken windows' batch and pane walk, the debris tables, the shadow map's queries and camera, the scorch marks' ground normal |
| `NIGHTFIRE_PARTICLESHADOW` | first tick | ResetParticle and MungeData on perturbed types, systems built, stepped and deleted on copies, CreateParticles, the cache's spawn, update and render, the library and its maps on scratch copies, RParticulate; the random generator compared after each case |

Construction, loading, the managers' Init and Kill, and everything that ends in a draw (the sky, glares, the glow,
car shadows, offscreen buffers, reflection views, broken windows, the particle manager's live systems) are covered
by the lockstep runs.

## What the port taught

The first shadow run after integration showed differences in four of the seven shadows. Two came from port bugs:

- **The positional lights' halves swapped.** RLightManager::AddPositionalLight wrote the block's two halves the wrong
  way round: the positions as four arrays at +0x00..+0x3f, the colours as Coord4s at +0x40. The lights shadow showed
  it, and it moved pixels on missions 2, 5 and 7 (the lit cars, the player car's highlights).
- **Another constant.** CreateParticles compared the direction's y with 0.0; the original compares with 0.1 (the
  constant at 0x00189ed8).

And lessons about testing:

- Between frames the renderer has no current view (EndView clears it), so a shadow whose calls reach the camera must
  lend one (`fgRenderHigh->views[0]`).
- Our builds inline functions the original calls - DrawGroupDrawInstance into FlushDrawLists, AddToTranslucentList
  into DrawInstance - so a fake planted on the inlined function's entry never fires on the port's side. Hook a call
  both sides still make (ReverseDrawList::PushBack).
- Requests and stack records keep pointers to each side's own stack arrays, and padding the original never writes
  holds stack leftovers: compare fields, not bytes.

The jump-table rule held: only RColorize::SetEnabled has a table (four entries, in listing order).

## Odd things in the original, kept

- The state manager fills a table of blend factors ("Zero", "One" ... "InvConstA", 0x001f2878) beside the blend
  operations' and never looks anything up in it; the glare manager flips a word on every DrawGlares that nothing
  reads.
- ROffscreenBuffer::MakeTexCopy has four copy offsets; asked for more, it reads past them into its own stack frame.
- RReflection::RenderView stores the float 4080.0 in the fog's mode word while it draws.
- PrivateSubmitSceneObj does not check that it found the slot ranked 3.
- ReflPrivateData's destructor frees the lighting records with delete, not delete[], and leaves their names;
  RDecalManager's frees its GeoPrims and keeps their addresses in the table; RColorize's, RDecalManager's and
  RGain's Kill leave the instance pointer set.
- RSceneObj::Destruct writes 0xabababab into its next pointer after unlinking itself.
- RLightning, joining one bolt's strip to the next, jitters a pair of points and throws them away (the random tables
  step on all the same).
- RLightManager::AddSpecularLight copies an uninitialised stack word into the specular light's w (the port writes
  zero), and Render_InitLibRender hands RRenderer::Init an uninitialised 0x70-byte stack buffer it never reads.
