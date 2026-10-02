# The effects system (Xbox action engine)

Impacts, smoke, sparks, decals, particle emitters, weather, glows and the other short-lived or purely visual objects
of the action engine, as reviewed from the Xbox `default.xbe` (names from the PS2 build's symbols) ahead of
reimplementing them. None of it is ours yet: 56 functions, about 19 KB, in four address ranges
(`tools/subsystems_action.txt`). This builds on `docs/architecture/frame-and-objects.md` (the object framework)
and `docs/architecture/rendering.md` (how objects are drawn).

| document | covers |
|---|---|
| this page | the shape, the life of an effect object, how they are drawn, what is ours, the laser case, unknowns, order and testing |
| [functions.md](functions.md) | all 56 functions: inventory tables with callers and register conventions, then the important ones step by step |
| [data.md](data.md) | `HITDATA_tag`, the `EffectInfo` table (with every row dumped), decal nodes, per-type object data, emitter configs, gas types, `Effect_Create` flags, `effectFlags` bits, globals |

Evidence is marked as in the drone docs: [dis] disassembly, [dec] decompile, [xref], [tbl] bytes from the XBE,
[PS2] a PS2 symbol, [inf] inference. Invented names are in *italics*.

## 1. The shape of it

```
impact (bullet / laser / footstep / anim script / script)
   │  Effect_Create(hit, obj, flags, volume) 0x70430
   │     material = hit+0x44 & 0x3f  ──► EffectInfo[material] (0x15d798, 23 x 0x88)
   │     0x20 ─► Emitter_Create(particleEmitter)   + Debris_Create(n)        (also in multiplayer)
   │     single player only, one of:
   │       0x01 Effect_Bullet: sound, 3 x Gas_Init (smoke slots), bullet-hole decal ─► EffectList
   │       0x02 Effect_Player: footstep sound, footprint decal                     ─► EffectList
   │       0x10 Effect_Laser:  hot-spot gas, spark (type 16), smoke wisp
   │       0x80 Effect_Body:   body-hit sound
   ▼
effect objects (control_create_object, one heap block: 0xe4 obj_tag + type data)
   type 14 Gas (12 gas types)    type 16 Effect/debris/spark   type 50 Emitter (point-sprite particles)
   type 65 Swoosh  69 Corona  70 BodyGlow  46/49 Leaf/LeafGen  52 Ripples  36 Cloud  51 Lightning
   type 82 DynamicObject  84 Apocalypse  38 Flicker  (6 Bubble, 12 Particles, 25 Sunreflection: dead on Xbox)
not objects:  EffectList decals (200 nodes on cel/object chains)   weather drops (global pools, 0x1000 drops)
   │
every tick: control_movement_object_handler ─► control_funcs[type].update (newest first)
every frame: View_AddObjects ─► View_DrawObjList ─► View_DrawObjectGlist (glist, billboard if effectFlags 8)
                                                 └► Emitter_Draw (control flag 0x100) ─► psiDrawParticleList
             View_Draw{Cel,Object}Glist ─► decal drawer 0xda1d0;   Game_Draw ─► DrawDrops ─► psiDrawParticleList
```

Three things about this shape that are easy to get wrong:

- **There is no effect-type dispatch.** `Effect_Create` fans out by *flag bits* chosen by the caller (data.md
  section 6); the hit's material picks the content through `EffectInfo`. "Effect type" in the sense of the brief is
  the material index.
- **"Particles" means three different things.** Real particles are only the emitter (type 50) and the weather
  drops, both drawn by `psiDrawParticleList` as point sprites (shader 129). The type 12 "Particles" object is a PS2
  leftover that cannot be created on Xbox. Gas, sparks, debris and leaves are ordinary glist objects (mostly
  billboards) drawn one by one.
- **Decals are not objects.** Bullet holes and footprints are 0x58-byte nodes from the 200-node `EffectList`,
  hung on the hit room's or object's `+0x10` chain and drawn after the owner's own glist.

## 2. The life of an effect object

**Created** by a `*_Create` or by `Gas_Init`/`Debris_Create`/`Effect_Laser`: `control_create_object(size, pos,
rot, NULL)` (ours) zeroes the block, gives defaults (alpha and tint 0xff, scale 1, `creationTimeFrames`) and
**inserts at the list head**; the creator sets `objectType`, sets the graphic with
`hashtable_set_object_to_entity_gfx` (which replaces all `effectFlags` except 0x30008000 with the glist's own), ORs
its own effect flags afterwards (typically 0x20 not collidable, 0x20000000 always straddling, 0x20000 dim in night
vision, 0x100 ignore kind-1 lights), and links it with `build_LinkToRoom(obj, 0, glb_world)`, falling back to
`control_link_object_to_cel(obj, cel)` (the hit's room). Emitters use `Control_CreateObjEx` with control flags
0x104 and a matrix instead.

**Updated** once per tick by its `control_funcs` row (data.md section 8 lists the rows). Because creation inserts
at the head and the walk goes forward, an effect spawned during the update pass is first updated on the next
tick. Updates move the object themselves and set control flag 0x20 so the framework rebuilds the matrix, sphere and
room; they count lifetimes in ticks (`Debris`, `Swoosh`, `Leaf`), in `FRAME_RATE_MUL` units (`Gas`, `Apocalypse`)
or in seconds via `REC_FRAME_RATE` (`Emitter`).

**Drawn** if it passes `View_AddObjects` (glist or control flags 0x110, display mask, not effectFlags 0x10 or
0x8000, frustum, not 0x400 outside vision mode 2), which also stores 2 in `obj+0xd8` (the "drawn recently"
countdown Corona reads). The draw list is chosen by effectFlags 0x1 (alpha, far to near) and 0x2000 (after the Z
clear). Alpha comes from `maybeBrightness` (0xe2) and the tint bytes (0xdf-0xe1) through `View_SetupRenderModes`;
gas and debris fade by changing those bytes. Billboards (effectFlags 8) face the camera, spin by `rotation.z` and
scale by `scale`; most effects do not use the matrix at all.

**Freed** by setting `flags |= 1`; the delete pass calls `Effect_RemoveSObjs` (decals) and `control_delete_object`
(type delete slot, unlink, `Mem_Free`). No effect type has a delete function except BodyGlow, whose
`BodyGlow_Delete` is called directly (it flags its 15 helper objects too); the Emitter's delete slot is the empty
hook 0xe0ec0, harmless because the particle arrays live in the object's own block.

## 3. What is already ours, and what is left

Ours around the subsystem (verified in `src/action/autogenerated_injections.inc`):

- Creation and the framework: `control_create_object`, `control_init_object`, `control_delete_object`,
  `control_unlink_object`, `Control_SetGList` (`game/obj/control.cpp`); `hashtable_set_object_to_entity_gfx`,
  `hashtable_getitem`, `hashtable_hashcode_to_celglist` (`util/hashtable.cpp`); `parsemap_create_dynamic_objects`
  (calls `Emitter_CreatePlist`, `Corona_Create`, `Cloud_Create`, `Lightning_Create`, `RainBox_Create`,
  `DynamicObject_Create`, `Apocalypse_Create`, `Flicker_Create`, `LeafGen_Create`, `Ripples_Create`, all original).
- Drawing: `psiDrawParticleList` and the 64-entry overlay ring with `psiAgeParticleOverlayRing`
  (`engine/psiDraw.cpp`, `psiGraphics.cpp`), `psiDrawObjectMatrix`, `psiSetTweakARGB`, `psiCopyToSP`, and the two
  stubs that the PS2 particle code became, `psiStubMinusOne` 0xde5d0 (= `psiCreateParticles`) and `psiStubFalse`
  0xde5e0 (= `psiParticleUpdate`). The D3D9 backend now feeds undeclared vertex inputs the NV2A default (0,0,0,1),
  which shader 129 (point sprites) needs for w.
- Users: `SpaceLaser.cpp` builds a type 16 beam by hand (`Effect_tag`, life 5) and calls `Emitter_Create`;
  `Searchlight.cpp` calls `Gas_Init`; `Camera.cpp` reads `apocalypseEffect`.
- Headers: `game/obj/Effect.h` (`Effect_tag`, 0x20, with life and fade as `someThing`/`someOther`), and one-line
  AUTOGEN stubs in `Emitter.cpp`, `Gas.cpp`, `Corona.cpp`, `Cloud.cpp`, `Flicker.cpp`, `LeafGen.cpp`,
  `Lightning.cpp`, `Apocalypse.cpp`, `DynamicObject.cpp`, `Ripples.cpp`, `RainBox.cpp`, `Env.cpp`. There are no
  files yet for Effect (the dispatcher), Debris, Swoosh, BodyGlow, Leaf, Bubble, Particles or the drops.

Left: all 56 functions (`python tools/function_coverage.py effects`): Emitter 5 (2.5 KB), Effect/Particles 14
(2.9 KB), weather group 20 (5.5 KB), Debris/Gas group 17 (7.6 KB). Of those, five are dead on Xbox or nearly so
(`Particles_Create`/`_Update`, `Bubble_Update`, `Sunreflection_Update`, `Flicker_Update`'s dark branch), and
another eight are under 100 bytes.

## 4. Unknowns and risks

1. **The laser corona comes from `Lock_Update`'s emitters (confirmed 2 Oct 2026).** The code says the magenta point
   sprites can only come from an emitter, and `Lock_Update` creates emitter 0x0c00000a at every laser hit on a lock
   (functions.md 2.3). A `laser_safe.txt` replay with `objlog 50` and `objlog 58` showed it: before firing there are
   no emitters and the safe's two bolts ("Object_SafeBolt_1", type 58) are untinted; 30 frames into the burn there
   are 15 emitters (rendererType 0x104) at the hit point and the bolt being hit has tint `ffababff` (green and blue
   lowered: the pin's red glow). `Effect_Laser`'s sparks are not the source.
2. **Register calling conventions.** Six of the Effect functions take arguments in EAX/EBX/ECX/EDX/ESI/EDI
   (functions.md 1.2). They can only be injected together with `Effect_Create`, which is cdecl; port the module as
   a unit or keep the originals and replace from the top.
3. **Frame-rate coupling.** Lifetimes mix ticks, `FRAME_RATE_MUL` and seconds; gas type 10 spawns when
   `frames & m == m` with `m = (int)(7 x FRAME_RATE_MUL)`, which is every eighth frame at 60 fps but eight frames
   in sixteen if `FRAME_RATE_MUL` is 1.2 (50 fps); Corona and the laser smoke use frame parity; `Emitter_Draw`
   advances a spin per particle drawn. A literal port keeps all of this; any change of tick rate shows up here
   first.
4. **Random number order.** Almost every function calls `Rand_Rand`/`Float_FRand` several times; replays and shadow
   tests only match if the calls happen in the same order and count (save and restore 0x18cdf8/0x18cdfc).
5. **Shared scratch globals.** `Emitter_Draw`'s loop counter (`i_135` 0x1f64a8) and matrix (0x1f646c), the key-frame
   scratch at 0x1f6448-0x1f6464, and the drop pools are statics in the XBE; keep them at their addresses (or own
   them all at once) while original and ported code coexist.
6. **Unread corners.** `effectFlags` 0x200 has no reader found on Xbox; `EffectInfo+0x80` is never read; the
   creators of Bubble (6) and Sunreflection (25) were not found (no immediate store of those type numbers -
   a store through a register would have been missed); which drop type is rain, snow or bubbles is only partly
   established; `FUN_000b82e0` (the bolt generator) and the sky flash that sets `SuperHappyLightningFlag` are
   outside this review.
7. **Ghidra names and types to fix** (not changed here; the database was read only): `Debris_CreateEx` 0xcccf0 is
   PS2 `Debris_Create`; `FUN_000cdef0` is `Leaf_Create`; `FUN_0006feb0` is `Particles_Create`; `FUN_000de5d0`/
   `FUN_000de5e0` are `psiCreateParticles`/`psiParticleUpdate`; `FUN_0006b6d0` is
   `hashtable_set_sub_object_to_entity_gfx`; `View_maybeDrawNightVisionExtras` 0xda1d0 is the decal drawer; the
   six register-convention prototypes; `HEINFO_tag` lacks 0x1c (*bounce*), the smoke sub-record, 0x78 and 0x80;
   `ObjData_Emitter` and `ParticleEmitterConfig` have only placeholder fields.
8. **Recycled decal nodes** keep their old graphic when the new hash does not resolve, and the decal matrix of a
   node linked to an object is in object space; both have to be reproduced exactly or decals will appear in the
   wrong place after a recycle.
9. **Multiplayer side effect.** In multiplayer `Effect_Create` returns before restoring a glass hit's normal; the
   hit list is freed soon after, but anything reading it later in the same tick sees the negated normal.

## 5. Suggested reimplementation order

Each step is small enough for one change, keeps the original data and order, and has a test that reaches it.

1. **Headers first** (no behaviour change): `HITDATA_tag` and `HEINFO_tag` with the *HESMOKE* records and a
   `static_assert` on the table size, the decal node, `ObjData_Gas`, the emitter object and config, the small type
   blocks from data.md section 4, all with layout asserts. Map `EffectInfo`, `EffectList` and the drop globals with
   `XBE_GLOBAL` so ported and original code share them.
2. **Self-contained updates**, one file each under `src/action/game/obj/`: `Debris_Update`, `Leaf_Update`,
   `LeafGen_*`/`Leaf_Create`, `Ripples_*`, `Corona_*`, `Swoosh_*`, `cloud_update`/`Cloud_Create`,
   `DynamicObject_*`, `Apocalypse_*`, `Lightning_*`. Each is a table slot or a placement create, cdecl, with few
   dependencies.
3. **Gas**: `Gas_Init` and `gases_smoke` together (they share the layout and the spawner recursion). Highest
   traffic of the module: muzzle smoke, impacts, the laser.
4. **Debris creators**: `Debris_Create`, `Debris_CreateEx`.
5. **Emitter**: `parsemap_block_particles`, `Emitter_Create`, `Emitter_CreatePlist`, `Emitter_Update` and
   `Emitter_Draw` with *Emitter_KeyColour* 0x67ea0; `psiDrawParticleList` is already ours.
6. **The Effect module as a unit**: `Effect_InitLists`, `Effect_UnLink`, `Effect_RemoveSObjs`,
   `Effect_RicochetProb`, then `Effect_Create` with `Effect_Bullet`, `Effect_Player`, `Effect_Laser`,
   `Effect_Body` and the three inlined helpers as ordinary C++ functions (the register conventions disappear).
7. **Weather**: `InitDrops`, `AddDrop`, `RainBox_*`, `UpdateDrops`, `DrawDrops`, as one unit (shared pools).
8. **BodyGlow** after the anim matrix palette work (`psiBuildMatrixPalette`, skeleton tables).
9. Leave the dead ones (`Particles_*`, `Bubble_Update`, `Sunreflection_Update`, Flicker's dark branch) for last,
   ported literally for completeness, or marked dead with a GameCube-style check (`docs/gamecube-checks.md`) that
   fires if they ever run.

Following the project's rule, test code and helpers go in their own compilation units, not inside the ported
functions; each ported function gets the GameCube checks that apply (only #40, "EMITTER TEXTURE MISSING", is in
this module).

## 6. Testing

**Shadow tests** (`src/action/devtools/*Shadow.cpp`, run at start with `MenuShadowTests=on`): the effects are
mostly "create an object, then step it", which shadow-tests well:

- For an update function: build an object with `control_create_object` (or a heap copy), fill the type block with
  random but valid values, copy it, run ours on one and the original (through `XbeOriginalScope`) on the other with
  the same RNG words, and compare the `obj_tag` fields the function writes (position, rotation, scale, tint,
  alpha, flags, control flags) and the type block. `Debris_Update`, `Leaf_Update`, `gases_smoke` (one run per gas
  type, spawners included - their children land at the list head and must be compared and freed), `Swoosh_Update`,
  `Emitter_Update` (with a synthetic config) and `DynamicObject_Update`/`Apocalypse_Update` (set channels first)
  fit this.
- For creators (`Gas_Init`, `Debris_Create`, `Emitter_Create`, `Effect_Create` with each flag combination over all
  23 materials): run both, compare the new objects at the list head field by field and the `EffectList` state,
  then delete them. Link targets (room) need a real level loaded, so these belong in a probe step after `level`
  rather than at start.
- Pure helpers (`Effect_RicochetProb` over all materials and masks, *Emitter_KeyColour* over random key sets) are
  table-driven like `MatrixShadow.cpp`.

**Replays** (`tools/ui/scripts/`, steps in `src/action/devtools/MenuProbe.cpp`): `level`, `teleport`, `cheat
nodrones|immortal` to reach a spot, `hold RT` to fire, `objlog TYPE` (14 gas with type and lifetime, 16 effect, 50
emitter), `gfxinfo NAME` / `hidegfx NAME` for A/B checks of one graphic, `dump` for a frame with the draw trace,
`shot` for screenshots. `laser_safe.txt` (and `_far`, `_nohotspot`) covers `Effect_Laser`, `Gas_Init` types 0 and
5, `Debris_Update` (sparks), `Lock_Update`'s emitters and the whole emitter path.
Further scripts worth adding: a wall-shooting replay per material group (decals, smoke slots, debris, ricochets),
a weather level (the first mission's snow; a Tower level for type 1), a corona-rich level for line-of-sight fades,
and a night-vision/thermal replay with drones for BodyGlow. As per the targeted-testing rule, run the shadow tests
and the replays that reach a change, and leave the full suite to the test runner.
