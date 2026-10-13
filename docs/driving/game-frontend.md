# The driving engine's HUD and front end

The game tier's HUD and front end: GHud (the gauges, messages, crosshairs, pause menu and cheats, the mission result
and the frame that draws them), the galleries it draws through (GGallery, the HUD's "hudNTSC.gal"; GFXGallery, the
mission's effects, started through GFX), the loading and unloading screens, the pause menu's animated figure (GGirl),
the subtitles, the HUD's orthographic view, and the system helpers beside them (the locale, colour and point maths,
the mission's gallery and particle files). Everything they call is ours: the renderer, EAGL with FONT and SHAPE, the
audio framework, the missions, events, AI and weapons (docs/driving/render.md, eagl.md, audio.md, game-missions.md,
game-events.md, game-ai.md, game-weapons.md). Ported on 12 October 2026 by four packages, then merged
(`src/driving/game/`). It is the last game-tier subsystem: with it every game and engine function is ours. Every
address is Driving.xbe's.

**Status: tested (13 October 2026).** 209 entries, all `FUNC_AT`: 62 of package A (GHud's construction,
state, gauges, screen effects and fonts), 12 of package B (GHud's large draws: the pause menu, the mission result, the
button and objective messages, the crosshairs, Render, Draw, UpdateTargets), 82 of package C (GGallery, GFXGallery,
GLoadingScreen with the string list's helpers, GGirl and GGirlieMaterial, and the launch page's IsEndState1Or4) and
52 of package D (GSystem, GLocale, the CURATOR functions, GSubtitles and its splitter, GFX, GOrthoHudView, and the
three sound methods compiled among GFX's), with GSubtitles::DrawFrame 0x000e3ab0 (the movie player's callback, which
Ghidra counts as DrawSubtitles' tail), and, from the integration, GalleryNamespace::NameLookup 0x000d3670 (the "GIOT"
symbol namespace's lookup, a vtable slot Ghidra has no function for, reached through the symbol tables). GFX::Trigger's
core 0x000d32f0 takes its effect in ESI and its offset in EDI, so no tag can name it: our three Trigger entries call
our core and the original is dead. `game.frontend` is ours by bytes: 208 replaced, 71 dead, 53 live - 52 exception
funclets (0x00154xxx-0x00155xxx) and 0x000e384a, a piece Ghidra cut off LoadSubtitles (ported inside it). Four shadow
tests compare the ports with the originals; the lockstep runs and the shadows pass (below, "What the testing
taught").

## The classes

Our classes declare no `virtual`: the vtable is the game's, the object's first word.

| Class | Size | Vtable | Header | What |
|---|---|---|---|---|
| GHud | 0x4f0 | 0x001a06c0, 1 slot (Delete) | Hud.h | the one HUD (RRenderHigh makes it; TheApp answers fHudApp): its puppets from "hudNTSC.gal", its action queue "HUDQ" and orthographic view, and the state of what it shows - the letterbox, the black fade, the messages, the timer, the crosshairs' lock, the health, the gadget, the blood curtain, the mission result and the pause menu |
| GGallery | 0xc0 | - | Gallery.h | a gallery file (a UGroup tree): its textures and canvases of named puppets, each puppet a run of keys (GalleryKey: position, time, orientation, then a sprite, text, quad, glare, sound, debris or particle record); the 2D draws take the state the last SetState copied from a key |
| GFXGallery, GFXGallery::Canvas | 0x1430, 0x90 | - | EffectsGallery.h | the mission's effects (TheEffectsGallery): 512 canvases in a slot list, each a playing effect following its scene object, drawing its particle, sound, glare and debris parts every frame; the sticky entries the particle cache reads; culling |
| GFX | statics | - | Gfx.h | the effects' entry points: Init and Shutdown, Trigger by group or name (answering a handle, the canvas's slot plus 1), the handle's Purge, SuppressEffect and SetIntensity, the frame's Update and culling; CANVAS_GetPosQuat beside them |
| GLoadingScreen | statics | - | LoadingScreen.h | the six progress dots, the mission's picture and "loading" text drawn by a sync task while the engine starts, Status's log lines, the unloading screen |
| GGirl, GGirlieMaterial | statics, 0xd0 | -, 0x0019fe78 | Girl.h | the figure behind every pause page: a run-length stream in the HUD gallery decoded into one of two 128 x 128 textures in turn, at 30 frames a second of the real clock |
| GSubtitles, GSubtitleSplitter | 0x94, 0x64 | 0x001a08b8, - | Subtitles.h | a locale string at the bottom of the screen, split into lines shown in turn: the mission's (DisplaySubtitle, timed by the simulation's steps) and the movies' (a cue file, started by the movie's frame and timed by the real clock) |
| GOrthoHudView | 0x4c (an RViewCamera) | 0x0019ff84 | OrthoHudView.h | the HUD's view: PreRender, PostRender (EExit and ERestart when the HUD asks), DoRender (the HUD drawn through a view matrix scaled by the screen's height over 480) |
| GSystem, GLocale | statics | - | FrontEndSystem.h | the locale's strings and language, the effects' data path, colour and point maths, the mission's gallery and particle files, CURATOR_Init and CURATOR_Shutdown |

## Who reaches it

RRenderHigh (the HUD's construction and Render, the particle file, the "GIOT" namespace), the world camera (culling
the effects, GFX::Update, the HUD's UpdateTargets), the scene objects and the world (their article effects through
GFX: Trigger, SetIntensity, Purge, SuppressEffect, PurgeSceneObjEffects), the particle cache (the sticky entries),
GameLoop (CURATOR, the loading screen, the locale, the movies' subtitles), the static initialisers (the colours, the
figure's size, the string list), the events (the HUD's messages, timers, gadget, letterbox, fades and results, the
effects, the subtitles, the movies), the missions (auto-drive, the fades, the letterbox, the radar radius, stylin,
pause), the weapons and Targeting (the lock and target, the lava effect, pause), the vehicles (the damage flash and
the fire buttons' pause test), the player camera (AimOn/AimOff), the IO module (the unplugged controller), the debug
audio pages (dbDrawTarget, GetScreenCoords) and the audio framework (AFader reads the screen fade; the sounds'
SetMix, IsFinished and ClearLoops). All call the ports directly.

## Layout

| File | What |
|---|---|
| `game/Hud.h` | GHud's layout (sized, static_assert'ed), every method, the colours and the HUD's globals |
| `game/Hud.cpp` | construction, Reset, the state setters, the pause menu's handler and the cheats, GetButton, dbMessage, GetScreenCoords (A) |
| `game/HudDraw.cpp`, `HudText.cpp` | the gauges and screen effects; WordWrap, the font helpers, the mission message and dbDrawTarget (A) |
| `game/HudFrame.cpp`, `HudMenus.cpp`, `HudCrossHairs.cpp` | Delete, SetControllerUnplugged, Render, Draw, UpdateTargets; the pause menu's pages, the mission result and the button and objective messages; the crosshairs (B) |
| `game/Gallery.h/.cpp`, `EffectsGallery.h/.cpp` | GGallery and GalleryKey's records; GFXGallery (C) |
| `game/LoadingScreen.h/.cpp`, `Girl.h/.cpp` | GLoadingScreen and the string list's helpers; GGirl and GGirlieMaterial (C) |
| `game/FrontEndSystem.h/.cpp`, `Subtitles.h/.cpp`, `Gfx.h/.cpp`, `OrthoHudView.h/.cpp` | GSystem, GLocale and CURATOR; the subtitles; GFX and CANVAS_GetPosQuat; the HUD's view (D) |
| elsewhere | the functions placed among the front end that belong to other classes, each a member of its class: ABaseSound::SetMix (audio/Sound, 0x000d3690), AVoice::IsFinished (audio/Voice, 0x000d36c0), ASound::ClearLoops (audio/ZoomObj, 0x000d3710), LaunchPage::IsEndState1Or4 (engine/GameLoop, 0x000d7bd0) and GalleryNamespace::NameLookup (render/RenderHigh, 0x000d3670) |

Merged at integration: the events' front-end views (engine/GameInterfaces.hpp: GHud, GFX, GLoadingScreen, GLocale,
GSubtitles) are gone, with the packages' own provisional views: the HUD's gallery view and puppets are GGallery and
GalleryKey (a puppet's position is the key's `position`, its text block the key's `text`); the HUD's views of the
mission manager, its timers, objectives and scores are SMissionManager, SMissionTimer and MissionObjective (an
objective's +0x04 is its `hint`, the locale symbol the objectives page shows); the score summary is the launch page's
`summary[27]`, rows of three; the tracked instances are Simulation.h's TrackedInstance; the article's DESCRIPTION is
PhysicsData's; the launch page's +0x998 and +0x99c are `unknown998` and `unknown99c`, its +0x53c `lives`; the figure's
views of the HUD and the launch page are GHud and LaunchPage; the three inline sound classes' methods are the audio
classes' own. An article effect's `reference` is the effect's UGroup and its `triggered` the int handle GFX::Trigger
answers (world/World.h). The gallery's record tags are `kGalleryTag*` (the engine's files keep their own `kTagName`).
Every call between the packages, from the front end into the missions, events, AI and weapons, and from the rest of
the tree into the front end, is direct; the static initialisers call LoadScreenStringList::NewHead. The one by-address
call left into the front end is the string list's erase, which the list's destructor (registered with the C runtime's
atexit, never run) takes as a function pointer, as for the engine's other lists.

The globals only our code touches are ours. The packages' (the subtitles, the locale, the data path, the figure's
state, the loading screen's, the HUD's statics, LastParticleSystem) and, from the integration: fHudApp, the HUD's
action queue and view (0x0023f448-0x0023f453), gHudOn (0x001b7055), gDAudio (0x001e4761, engine/GameLoop), the
screen fade AFader reads (0x00243a74), the loading screen's state (0x0023f574), TheEffectsGallery (0x0023f320), the
effects' generator (0x001caf34, 0x001caf38), the line material and its guard (0x0023f330, 0x0023f390), and, through
tools/static_init_driving.py, the HUD's 37 colours (0x0023f4b0-0x0023f560), the loading screen's four
(0x0023f80c-0x0023f824) and the figure's size (0x0023f3dc, 0x0023f3e0). The static initialisers convert the colours
into our variables by name, in their place in the order. global_coverage still lists TheEffectsGallery and
TheSubtitles as touched by live code: the references are from 0x000d3670 and 0x000e3ab0, which Ghidra has no function
for and which our ports replace. Still at the game's address: the dots' SHPX (0x001caa00, a shape file in the XBE's
data), the figure's 48 KB palette (0x00193e70, .rdata), AttributeSet's default vector (0x001d4c00), the colours
nothing reads (0x0023f4d4, 0x0023f524-0x0023f538, 0x0023f554, 0x0023f810, 0x0023f818, 0x0023f820, 0x0023f830,
0x0023f834) and the string list itself (0x0023f800).

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_GAMEFRONTENDSHADOWA` | first tick | about 1,800 cases on perturbed copies of the live HUD: the state, Reset, pause, PauseMenuMsgHandler (every page), CheatMsgHandler (each code, on and off its mission), the sixteen draws, the text helpers, WordWrap, GetButton, dbMessage, GetScreenCoords; about 50 recording fakes |
| `NIGHTFIRE_GAMEFRONTENDSHADOWB` | first tick | about 3,700 cases: DrawPauseMenu on every page, DrawMissionResult and the setters, the messages, DrawCrossHairs, Render and UpdateTargets, Draw; about 90 fakes |
| `NIGHTFIRE_GAMEFRONTENDSHADOWC` | first tick | all 82 of package C swapped back: the gallery queries, every SetState and draw (both aspect ratios, every video mode), Construct/FONT_Load/Destruct, GFXGallery scripts (CANVAS_Add with every flag, FRAME_Draw, culling, sticky entries, purges), DoGirl under a fake clock (the texture's pixels hashed), KillGirl, the loading and unloading screens' sequences, the list helpers; shared regions, owned globals, a fake arena and a call log compared |
| `NIGHTFIRE_GAMEFRONTENDSHADOWD` | first tick | about 8,000 cases: COLOR/MATH, FILE_FullPathname, LOCALE_Symbol, the mission-file functions, GetLanguage/GetSubDirectory over languages and video modes, the splitter, GSubtitles, the Trigger entries, the GFX wrappers, the sound methods, CANVAS_GetPosQuat, the view's Pre/Post/DoRender over every video mode; about 45 fakes |

Each shadow swaps its package's entries back for the original's run and replaces the calls out of it with recording
fakes, hooked at the original's entry and, where the entry jumps to a port, at the port, so the ports' direct calls
reach the fakes too. The globals the integration made ours are set on both copies (ours for the port, the game's for
the original) or lent through devtools/OwnedGlobals. The mutations each was checked against (by reading): A,
DrawLetterBox's `> 30` for `>= 30` and DrawHealth's `>= 0.5` for `> 0.5`; B, DrawMissionResult's `>` for `>=` and the
sniper ticks' matrix order; C, SPRITE_Draw2D's mirrored x, the figure's alpha step 27 for 28 and CULL_SetCanvas's
0x8000 test; D, GetString's `>` for `>=`, GetLanguage's 6 for 8 and DoRender's wide-PAL y scale. The shadows do not
run the constructors, destructors and shutdowns that every boot runs (GHud's, GFX and CURATOR's Init and Shutdown,
GHud::Delete), nor 0x000d3670.

In game: lockstep frame dumps of missions 1-8 draw the HUD every frame - GHud's construction and Reset at load,
Render, Draw and UpdateTargets, the health, weapon, message, fades, letterbox, stylin, timer, screen hits and
crosshairs (while the letterbox is off), the button and objective messages, GetScreenCoords, the lock; FRAME_Draw and
culling every frame, CANVAS_Add when effects fire, the HUD view's DoRender and PreRender, the sound methods, GFX::Update
and the Trigger entries; CURATOR_Init, GFX::Init and the locale's loads at boot; any lookup in the "GIOT" namespace
(through the symbol tables). Only the shadows reach: the pause menu (every page, the figure, the errors), the cheats,
the mission result (mission end only), SetControllerUnplugged, the sniper scope, the fly-by-wire overlay, AimOn/AimOff,
the blood curtain, dbMessage and dbDrawTarget, the loading screen (it runs before the first lockstep frame) and the
unloading screen, PostRender's events, the movies' subtitles, DisplaySubtitle and DrawSubtitles (unless an
ESetSubtitle runs), the list helpers, PurgeAllEffects and the shutdowns. To test specially in game: the loading and
unloading screens (each mission, both video modes), the pause menu on every page with the figure, restart and quit
from it (EExit, ERestart), a mission passed and one failed, the controller pulled out, the sniper scope and the
auto-drive aim, a movie with subtitles in each language, the cheats, and effects on scene objects (the "GIOT"
lookups, culling, sticky entries).

## What the testing taught

The first round (gfe2) had all 16 lockstep runs exact, but no shadow run finished: each died silently in
GameFrontEndShadowB, after A's summary, so B, C and D never reported. A showed 4 differences (FadeOutToBlack) and
GameEventsShadowD 4 (ESetVideo) on every mission. Run one at a time on one mission, the shadows showed more (C 14,
D about 2,470). Every difference was the harness's; the port needed no change.

- **The silent death (B).** DrawCrossHairs' remote-rocket recharge bar counts its segments unsigned (0x000e08c7,
  0x000e0917; the port the same). B's cases put the rocket's fire step up to 3,000 steps after the current one, so
  the charge went negative and both sides drew about four billion segments, appending to the call log until memory
  ran out, which ends the game with no fault report. B now fires at or before the current step, as the game does.
  `NIGHTFIRE_GAMEFRONTENDSHADOWB=2` names each case before it runs, which found it.
- **Return types (A).** The fake for SMissionManager::GoingToSkipCinematic answered a `bool`, setting AL only; the port
  declares `int` and tests all of EAX, so on a skipping answer the stale upper bytes sent the port into
  ClearSubtitles. The fakes in A and D answer `int`.
- **Function pointers (GameEventsShadowD).** ESetVideo hands the movie player GSubtitles::DrawFrame: the original its
  game address (0x000e3ab0), the port ours. The fake logs ours as the game's.
- **Globals only the port writes (D).** The game's copy of LocaleFile (0x0023f8b8) is never loaded, since our
  LOCALE_LoadLanguage loads ours: D lends ours to it for the run.
- **Folded functions (D).** The linker folds our two identical MEM_free copies (0x00113f20, 0x00117110) into one, so
  the port's calls to either reach whichever hook went in last: one fake serves both, and it logs a freed block by
  kind (heap, static, the fake file), since the fake frees nothing and the sides' blocks differ.
- **A case's own draws (D).** FillSplitter drew its random fields inside the case, so the port's side got other
  values: RunCase puts the generator back before each side, as the AI round's shadows do.
- **Clocks (C, and A and D).** The loading screen's file loads run the sync tasks whenever the real clock comes due,
  and IOModule::Update, one of them, reads TIMER_gettick: on the shadow's thread, it took a fake tick from one side
  or the other at random. C turns the input's updates off for its run, and the clock fakes in A, C and D give the
  game's other threads the tick of the shadow's start.
- **NaN payloads (C).** QUAD_Interpolate on a real key that holds no quad meets NaN corners in both inputs; the
  original's x87 keeps the larger payload, our SSE the first operand's (Known deviations). C logs any NaN alike.
- **Impossible states (C).** C called CANVAS_HasStopped on any canvas and left a stopped one in the list, its emitters
  given back, for later draws, purges and intensities to fault on: about 120 cases faulted on both sides on every
  mission (several minutes of fault reports with eight games). A stopped canvas now leaves the list as FRAME_Draw
  takes it off. The loading screen's cases that draw the picture with none loaded still fault on both sides (about
  16); letting them run on reaches Shutdown's frees of a kept screen's stale picture and font, which kills the game.
- **Time.** In gfe3, with all four front-end shadows running and C's 276 faults, the shadow runs of missions 1-4 and 6
  reached round.sh's 45 minutes before the frame dump (mission 2 before the front end's shadows). Without C's wasted
  faults a run takes 29 to 38 minutes on eight slots; round.sh gives a shadow run 60 minutes now, for room.

Other harness additions: A prints the call log round its first differences; C prints a window of its call log
round a difference with `NIGHTFIRE_GFEC_WINDOW=1`, names each case before it runs with
`NIGHTFIRE_GAMEFRONTENDSHADOWC=2`, and counts allocations its arena had no room for (none so far).

With these, the final round (gfe4) had all 16 lockstep runs exact against the baselines (frames and sound traces to
tick 9000) and every shadow run finished on missions 1-8 (a frame dumped), the four front-end shadows and
GameEventsShadowD with 0 differences; only the two known differences remain (sndsysshadow case 0, sndstreamfileshadow
case 52; both harness problems, fixed since: sound.md 9.6).

## Random and clock calls

- The effects' own generator (GfxRandomState, multiplier GfxRandomMultiplier; CURATOR_Init seeds it, the state
  987654321 through 175 rounds of x-13035 & 0xffff, the multiplier 123456789): GLARE_Draw draws three for a jittered
  glare; DEBRIS_Draw three for each piece and three more for its turn when the key has one.
- The real clock (TIMER_gettick): GGirl::UpdateTexture (scaled by the simulation's step), DrawBlinkingArrow (the pause
  menu), InitLoadScreen's hint (tick % lines), Status's time, the splitter and the subtitles of a movie, and
  SetupFMVSubtitles. The lockstep runs depend on the figure's clock behaving exactly as the original
  (docs/test-runner.md).
- The simulation's step count: the HUD's frame time, fades, lock delay, timers and messages, and the mission's
  subtitles.

## Known deviations

Places where the original reads stack it never wrote; the port uses zero: Get_InterpolatedKey's header bytes
0x22-0x33, GLARE_Draw's Glare record, DEBRIS_Draw's velocity spread w. Overflows bounded: InitLoadScreen's
"HINT_ID%d" buffer (8 bytes in the original, 24 here), the unloading screen's formatting (snprintf). Left out, none
observable: CANVAS_HasStopped's dead read of each system's type flags (which faults only on a NULL type),
FRAME_Draw's unused maximum, DoGirl's WBINVD, the splitter's Reset's NULL test (both fault), a copy of
Get_InterpolatedKey's answer in DrawMissionResult, which calls QUAD_SetState where the original inlines it. The
packages' strings, floats and tables (the cheat codes, the button markers, the languages' folders) are our
constants; the pure C runtime calls are our DLL's, the rest the game's runtime's (docs/driving/c-runtime.md: atol wraps
where ours saturates, Status's float formatting).
QUAD_Interpolate's corners: with both NaN, the original's x87 leaves the NaN of larger payload, ours (SSE, MSVC's
operand order) the first corner's; only a key that holds no quad has NaN corners.

## Odd things in the original, kept

- NULL not checked: a hint with no button marker dereferences a NULL button (DrawPauseMenu); CANVAS_Add tests the
  canvas's address rather than the slot.
- Leaks and dangling pointers: GHud's destructor leaves the action queue and view globals; KillGirl leaves the
  materials; GFX::Shutdown leaves TheEffectsGallery; the loading screen's Shutdown frees the picture and font only when
  the screen is kept and clears the screen only when it is not; the galleries' freed pointers are not cleared, and a
  gallery loaded with FileLoad is freed through UMemory::Free; the subtitles free new[] blocks with delete and free
  their font through MEM_free's second copy; DrawPauseMenu leaks the line-count array when the objective has gone.
- Arrays: DrawHealth reads armPuppets[8] (the health border) when the shield is over its maximum; GetButton's copy is
  not terminated; the splitter drops lines past ten and GetString re-times line 9.
- Branches and values: TriggerMissionMsg sets the frame's last step; the objectives page wraps asymmetrically and the
  failed page's Accept drops two lookups; DrawStylin fades by whole steps; DrawScreenFades sets 0, not 1, past 1; an
  unsigned compare in TriggerDamageFlash; the button message pulses; the score page divides unsigned;
  DrawCrossHairs compares the recharge bar's segment count unsigned (a negative charge, which play never makes, would
  draw about four billion);
  DrawMissionResult's state 2 draws key 0's quad at the interpolated key; the sniper ticks' spacing is x 0; the
  recharge goes through the sound's slot 9; Render keeps actions 33-35 from the pause menu; the texts go to
  FONT_drawtextfa as format strings; GFX::Trigger answers 0 for an effect with Flgs bit 0x01; PostRender sends EExit
  and ERestart every frame while asked; wide PAL's y is not scaled; GetSubDirectory answers NULL outside video modes
  1-3; a movie's subtitle lasts until the next cue; the cue file is read as CR LF from the seventh character of its
  first field; the figure is always mirrored, scales the real clock by the simulation's step, and its second palette
  offset never moves.
- Dead work: arguments not read (TriggerTimerOff, SetMissionPassed, dbDrawTarget's third, GET_Puppet's kind, three
  of GFXGallery's), GetSelectedTarget's answer dropped, GetFoundationVideoMode called and dropped, HudViewPort kept and
  never read, the show-symbols path, the palette speeds written and never read.

All of these are in docs/driving/original-quirks.md with their addresses.
