# The action engine's architecture

How 007: Nightfire's action engine (`default.xbe`) is put together, subsystem by subsystem, with what is ours, what
is understood and where the unknowns are. It sits on top of [function-coverage.md](../function-coverage.md), which
has the numbers. These notes were written in October 2026 from the Xbox binary (with the named PS2 build as a guide)
and from our source. They mark what was read in the code and what is inferred.

| Note | Covers |
|---|---|
| [frame-and-objects.md](frame-and-objects.md) | the frame loop, GameFlow, the object framework (`control`, `obj_tag`, the type table), world objects and effects as a family |
| [rendering.md](rendering.md) | viewers, portal visibility, draw lists, a frame pass by pass, sprites and text, the boundary with `d3dSeam` |
| [player-weapons-mp.md](player-weapons-mp.md) | the player object, movement, the weapon definitions and state machine, bullets, statistics, multiplayer |
| [collision-physics-camera.md](collision-physics-camera.md) | collision queries and resolution, rigid bodies, cameras and viewers, the animation runtime's status |
| [audio-loading-saves-scripts.md](audio-loading-saves-scripts.md) | game-side audio and music, the level load sequence, save blocks, the script interpreter, FMVs |
| [../drone/README.md](../drone/README.md) | the AI characters (drones and bots) and navigation |
| [../ui/README.md](../ui/README.md) | the front end |

The file formats are documented on the `blender-exports` branch, which is not yet merged into `dev`.
`docs/anim/` covers skeletons, skins, sequences, scripts, meshes and textures. `docs/level/` covers the level
bundles, the world, placements and every object type's parameters.

## The shape of it

```
Game_Main -> mainloop -> GameFlow_Main (64-deep state stack)              ours
    Game_Run                                                              ours
        input, scripts, music
        control_movement_object_handler       per-type update / collide / delete, through control_funcs
            Player_Update, NDrone2 state machine, doors, triggers, ...    mostly original
            Collide_Update                    hit lists and push-outs
        HUD, PlrStat, MP_Update
    Game_Draw                                                             original
        psiPreDraw -> View_CaptureScene x11 (portals, sort lists) -> passes per viewer -> sprites
    psiPostDraw: blur, d3dSwap, SFXUpdate, rumble                         original
                                                    ---------- the surface ----------
    d3dSeam -> D3D9 backend, dsndSeam -> XAudio2, psiInput -> XInput, FS -> host files   ours
```

Logic runs a fixed step per frame, with no delta time. The present paces it: vsync on the Xbox, `PaceFrame` in our
backend. Objects talk through switch channels, hit lists and a few private registries. They don't call each other
directly much.

## Where each subsystem stands

Coverage by function count, with the share of bytes in brackets where it differs a lot. "Understood" is a judgement
from the notes: **good** means mapped and documented, so the main work is porting; **partial** means the shape is
known but important pieces are unread; **thin** means the shape itself is still open.

| Subsystem | Done | Understood | The main unknowns |
|---|--:|---|---|
| Frame loop, GameFlow | 53% | good | `P_NIS_Handler` runs the object update itself during in-engine cutscenes: a second entry point |
| Object framework (`control`) | 44% (19%) | good | update order is newest first, hit lists lag a frame, the post-move pass runs twice: any tidying changes gameplay; `cel_tag` and `obj_tag` share their first 0x14 bytes |
| World objects (204) | 16% | partial | about 40 small independent types; the shard pool behind Break/Destroy; four stubbed handler slots |
| Effects (56) | 0% | partial | `Emitter_Draw` changes state while drawing; frame-counted timers |
| Drones (694) | 3% | good (documented) | 192 state functions; see [drone/README.md](../drone/README.md#open-questions) |
| Bots (75), navigation (68) | 4-8% | good (documented) | as above |
| Player (174) | 22% (13%) | partial | about two-thirds of the 0x8fc-byte player struct is unnamed; the 17 weapon states are unnamed; the damage formula is unread |
| Weapons data | 0% | partial | `WeaponDataTableInit` (44 KB of code) undecoded; `Player_Init` rewrites two entries every level |
| Bullets (16) | 0% | partial | the only projectile path, with 17 callers (drones, turrets, cars, subs): port it alongside the drones |
| Multiplayer (77) | 23% | partial | the game-mode updates (about 8 KB) and spawn selection are unread; a probable original scoring bug (KOTH) |
| Collision (42) | 12% (3%) | partial | the narrow-phase maths, where movement feel comes from; several Xbox names wrong; register calling conventions; unchecked arrays |
| Rigid bodies (19) | 0% | good | only hanging lamps and the like (placement 216) use them; a probable inertia-swap bug |
| Camera (37) | 19% | partial | viewers 5-10 and five unnamed camera functions |
| Animation (80) | 5% | good (documented) | the playback ports are on `blender-exports` and need a hand merge |
| Rendering (59 + layer) | 34% (12%) | partial | the viewers' roles; a 200-unit portal far plane; silent list overflow; `Game_Draw` should go last |
| Front end (300) | 20% (35%) | good (documented) | the handlers without lists |
| HUD (45) | 42% | good | reads the unnamed player fields |
| Audio, game side (135) | 11% (5%) | partial | 21 per-level music state machines (code, not data); `MusicEventList` slots; bank, stream and marker formats |
| Level loading (58) | 43% (51%) | good | archive 0x07000500; Loadables look vestigial |
| Saves (`LS_*`, 20) | 0% | good | the save masks the menus pass; whether codename deletion works |
| Scripting (57) | 53% | partial | the data inside each command; the `SSTREAM` fields; needs a script dumper to test against |
| Eurocom's Xbox layer (272) | 55% (66%) | good | the FMV player (5 functions) and the file/save code |
| XDK libraries (1,227) | 51% | n/a | D3D, XPP done; DSOUND and XMV kept alive only by the FMV player |

## Unknowns that cut across subsystems

- **Frame-counted timing.** Weapon recharge, refire and bursts, fall damage, many object timers and the music
  streams all count frames rather than time. A change to the clock changes gameplay unevenly. Any frame-rate work
  has to come after those are found and scaled together.
- **Order dependence.** Object update order, the one-frame lag of hit lists, two-pass triggers, and music events
  that last exactly one frame. A literal port first, tidying later.
- **Unnamed struct fields.** `obj_tag` has gaps (the decal list head at 0x10, the previous centre at 0x54, a merged
  `FLAG_IN_FORCEDLIST`). Most of the player struct, the weapon definition and `viewer_tag` are unnamed too. Typing
  them is cheap and comes before porting their users.
- **Register calling conventions.** Collision, rendering and drone helpers take arguments in EAX, EBX, ESI or EDI.
  Each one needs a naked adaptor while any original caller remains, or else all its callers must be replaced together.
- **Unchecked fixed arrays.** Sort lists (0x200/0x200/0x80, halved in multiplayer), 64 visible rooms, 600 `ColBoxs`
  entries, a hit sort list smaller than the hit pool. The GameCube build's checks (`docs/gamecube-checks.md`) cover
  some of these.
- **Floating point.** The original's x87 keeps intermediates at double precision (the RecoverTime probe). Our ports
  use double where the x87 would. Replays will still drift where it matters, so shadow tests stay the reference.
- **Ghidra's view needs fixing in places.** Functions split into fragments (`Lock_Update` in seven,
  `RecurseAndDrawBoxes` ending early), duplicated names (`SFXUpdate`, `Script_Stop`, `Car_Deactivate`,
  `Debris_CreateEx`, `View_SetupRenderModes`), and misleading names (`Player_WeaponRecoil` is bob and sway,
  `Intersect_RayGeom` is the triangle gather, `maybeDecodeMpgAudio` decodes and draws video). Each note lists its
  own. Name-based tools trip on the duplicates.

## Suggested order

By payoff against risk, drawn from the notes:

1. **Cheap and self-contained, testable byte for byte.** Saves (`LS_*`, 20 functions, against the exact save
   files the original writes). The weapon table as generated data, from a dump of the running game's table, which
   retires 44 KB. Rigid bodies.
2. **Type the structs before porting their users.** The `obj_tag` gaps, the player struct, the weapon definition,
   `viewer_tag`.
3. **Leaves with shadow tests.** The collision intersection leaves, the small weapon helpers, the control helpers,
   the sprite and font drawers.
4. **Systems as literal ports, one unit at a time.** The object update loop, triggers and doors, the simple effects,
   multiplayer scoring and match end, the weapon state machine, then collision's broad phase and drivers.
5. **The drones**, by the plan in `docs/drone/README.md`, with bullets alongside.
6. **Last: the centres everything hangs off.** `Player_Update` and `Player_CollisionHandler`, and `Game_Draw` once
   the viewers' roles are settled.
7. **Separately, the FMV player.** Replacing its five functions with a host decoder (or transcoding the movies)
   retires the last of DSOUND and XMV, about 310 library functions. It works today through the seams, so it is
   only urgent once the XBE's code has to go entirely.
