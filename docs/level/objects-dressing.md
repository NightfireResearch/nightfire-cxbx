# Level objects: the "dressing" group (sky, lights, weather, particles, animated props)

Sources: the Xbox `default.xbe` in Ghidra (read only). The main function is `parsemap_create_dynamic_objects` 0xa4bc0,
plus each Create/Update/Draw function named below. Every field was checked against the placements in all 32 level
bundles: 603 placements of these types, and the counts match the census. The machine-readable form is
`tools/level/types_dressing.json`.

Conventions:
- **Key k** is the level_tag dword at `0x2c + 4*k`. Where a function reads only a byte or a u16 of it, the notes say so.
- **pos/rot/quat** are placement +0x0c, +0x18 and +0x24. Quaternions are stored as (x, y, z, w). `Quat_QuatToMat`
  0xd5800 confirms the order.
- **Object Euler.** Every Create function here passes `quat = NULL` to `control_create_object` 0x2dc10. That call builds
  the object matrix with `View_RotTransMatrix` 0xd92e0. The rows are right, up, dir, pos:
  `m0=cz*cy m1=sz m2=-cz*sy | m4=sy*sx-sz*cy*cx m5=cz*cx m6=cy*sx+sz*sy*cx | m8=sy*cx+sz*cy*sx m9=-cz*sx
  m10=cy*cx-sz*sy*sx`.
- **Sky Euler.** Sky objects use a different function, `RotMatrix` 0xd6560:
  `m0=cz*cy m4=-sz*cy m8=sy | m1=sz*cx+cz*sy*sx m5=cz*cx-sy*sx*sz m9=-cy*sx | m2=sz*sx-sy*cx*cz m6=cz*sx+sy*cx*sz
  m10=cy*cx`.
- **Glist flags.** Most Create functions copy the entity's `applyFlagsToObject` (+0x08) into the object's
  `effectFlags`. The bits that matter for drawing (`View_AddObjects` 0xda490, `View_DrawObjectGlist` 0xda750):
  - 0x1 translucent (drawn in the alpha list)
  - 0x4 random ±5% scale jitter every frame
  - 0x8 camera-facing billboard (rot.z gives a roll)
  - 0x10 hidden
  - 0x2000 drawn in the late "glare" list
  - 0x8000 never drawn
  - 0x400 night-vision only
  - 0x80 lit by the dynamic point lights

  `View_DrawSky` reads its own bits instead: 0x2 = centred on the camera, 0x8 = uses the camera rotation.
- Frame timing: `FRAME_RATE_MUL = 60 / fps`, so "per frame" below means per 1/60 s.

## The dispatch, and the unlisted types 255, 246, 247 and 221

`parsemap_create_dynamic_objects` (a cdecl function with 7 stack arguments) copies pos, rot, quat and the +0x34 vec3 to
locals. It then runs `switch(type)` through a range check and two lookup tables:
- It subtracts 15 from the type. If the result is above 0xef (so the type is below 15 or above 254), it jumps straight to
  the exit at 0xa5c3f.
- Otherwise it reads byte table 0xa5d6c[type-15] and uses that byte to index jump table 0xa5c48. Index 72 is the exit.

Results:
- **255** fails the range check (255 > 254), so it is a **no-op**.
- **221, 246 and 247** map to index 72 (the default exit), so they are also **no-ops**. The same goes for every other
  unlisted id: 16-31, 35, 60, 61, 64, 66-73, 75-77, 79-112, 114-209, 211, 214 and 215.

The loader still looks up their glist; `(0xff & 0xA000) == 0` passes, so `parsemap_create_dynamic_objects` is called and
does nothing. What these placements contain (all at pos (0,0,0), no keys):

| type | where | glist |
|---|---|---|
| 255 ×7 | HendersonA pack 010000a2; PowerStationA1/A2 010000e5/e7 (×3); EvilBase 010001c6 (×2); SubPen | `Unnamed` (empty, bbox inverted ±9999999) ×4, `A1blockingpolys` (8 tris), `Elevator_Door_Slide_A`/`_B` (152 tris each) |
| 246 | HendersonA pack 010000f3 (bundle type 0) | 0x0200063f `AI_ViewCone` (0 tris, 40×2×20 bbox) |
| 247 | HendersonA pack 010000f3 | 0x02000640 `AI_ViewDist` (0 tris) |
| 221 | HendersonA pack 01000166 (bundle type 0) | 0x02000634 `Drone` (124 tris) |

These are editor gizmos and leftovers, and the exporter should skip them. Note that packs 010000f3 and 01000166 are
object packs (bundle type 0), not map files. They also hold one Light, one Env and one Drone placed at the origin, which
look like templates.

---

## 50 RainBox: `RainBox_Create` 0xa3a40

- **Call:** `RainBox_Create(&pos, &rot, lvl, celglist)`. It reads no keys, and all 127 placements have none.
- **What it creates:** an OBJECTTYPE_GFX object that gets the glist and its flags, linked into the `RainBoxRoot` list.
  Nothing links it to a room.
- **Volume:** `RainBox_Clip` 0xa3ab0 computes a world AABB = obj pos + glist bbMin/bbMax (celglist +0x1c/+0x28). Rotation
  is ignored. The AABB is clipped to ±15 m around the drop centre (the viewer, 7.5 m ahead of the player).
- **What falls in it:** `AddDrop` 0xa3350 spawns drops only inside a clipped box. The level's `DropType` (Env key 0,
  below) decides what they are.
- **Visual:** invisible. The glists (`Rainbox*`, `Effect_Snow_*`, `SnowBox`) have 0 or 12 tris and are hidden (flags
  0x30/0x10) or empty.
- **Export:** a box volume (an empty with the AABB as its display size) tagged with the level's precipitation type.
- **Confidence:** confirmed.

## 252 Corona: `Corona_Create` 0xa3e40 / `Corona_Update` 0xa3eb0

- **Call:** `Corona_Create(&pos, &rot, lvl, celglist)`. No keys.
- **What it creates:** OBJECTTYPE_CORONA with the glist and its flags.
  - Glists: `corona_small`, `corona1`, `Corona_Glow2`, `Window_corona`, `corona_red`, `Corona_test1`.
  - Each is a 2-triangle quad (8 tris for one variant), with flags 0x2d, 0x29, 0x229 or 0x22d. These include 0x1
    translucent and 0x8 billboard, and usually 0x4 jitter.
- **Behaviour:** it updates on alternate frames (random phase).
  - While the camera has line of sight (`Collide_LineOfSight`), the alpha byte (obj+0xe2) rises by 16 per frame to 255
    and effectFlags |= 0x2000 (late glare list).
  - Otherwise the alpha falls by 16 per frame.
- **Export:** the glist quad as a camera-facing billboard at pos (texture and material from the glist), additive or
  alpha blended. Rotation is irrelevant because the quad is billboarded.
- **Data:** 112 placements in EvilSilo (76), EvilBaseC (21), CastleExterior (13) and HendersonD (2).
- **Confidence:** confirmed.

## 59 DynamicObject: `DynamicObject_Create` 0xa4000 / `_Update` 0xa4090

- **Call:** `DynamicObject_Create(&pos, &rot, lvl, celglist)`. It draws the placement glist at pos with the object Euler.

| key | off | read as | meaning | seen |
|---|---|---|---|---|
| 0 | 0x2c | s16 | appear switch: nonzero makes the object start hidden (effectFlags \|= 0x30) until `switch_channels[k0]` is set, then the glist flags come back | 0 ×44, 120 ×15, 9..140 |
| 1 | 0x30 | s16 | disappear switch: when set, k3 == 0 deletes the object (flags \|= 1); k3 != 0 hides it while set and shows it while clear | 0, 1, 43..140 |
| 2 | 0x34 | s16 | fade distance: if the camera is closer than k2, alpha = clamp((d/k2 − 0.5)·2)·255, so it fades out as you approach | 0, 9, 13 (the `*_volumetric` light shafts) |
| 3 | 0x38 | byte | toggle mode for key 1 | 0, 1 |

- **Glists:** `CONSOLE_Camera_Active/Inactive` (switch-driven pairs), `OBJECT_RopeBarrier`, `endleveldoor`, pallet
  stacks, `obj_*_volumetric` (flags 0x221 translucent), and `WallHug_Blocker*` (flag 0x10, invisible blockers).
- **Export:** a normal mesh instance. Hide it when k0 != 0 (it starts invisible).
- **Confidence:** confirmed.

## 42 SkyObject: `View_AddSkyObj` 0xd9c60 / `View_DrawSky` 0xd9d60

- **Call:** `View_AddSkyObj(u16 k0, celglist, &pos, &rot, NULL, byte k1, u16 k2, u16 k3, byte k4)`. The disassembly at
  0xa57c9 confirms the argument widths.
- **Sky table:** a table of 21 slots, 0x34 bytes each, at 0x29d7a0:

  | slot offset | contents |
  |---|---|
  | +0 | glist |
  | +4 | flash glist |
  | +8 / +0xc | object / flash object (Cloud) |
  | +0x10 | pos |
  | +0x1c | rot |
  | +0x28 | k2·60 |
  | +0x2a | k3 |
  | +0x2c..0x30 | timers |
  | +0x32 | pass |

| key | off | read as | meaning | seen |
|---|---|---|---|---|
| 0 | 0x2c | u16 | slot 0..20 (21 and above is ignored); a later placement with the same slot and the same k1 overwrites the earlier one | 0..18 |
| 1 | 0x30 | byte | 0 = normal sky glist; 1 = lightning-flash glist | 0, 1 |
| 2 | 0x34 | u16 | flash interval: the next flash comes after Rand(k2·60) frames | 24, 30 |
| 3 | 0x38 | u16 | flash length in frames (each frame of a flash shows the flash glist with 50% chance) | 5, 10 |
| 4 | 0x3c | byte | draw pass: 1 = before all objects (the backdrop behind everything); 0 = after the opaque object list | 0, 1 |

**How it is drawn (the answer for the exporter).** Each frame `Game_Draw` calls `View_DrawSky(viewer, 1)` before the
objects and `View_DrawSky(viewer, 0)` after the opaque pass. Everything is drawn unlit with a white tint. For each slot
whose pass matches:
- **Rotation:** if glist flag 0x8 is clear, the rotation is `RotMatrix(rot)` (the sky Euler). If it is set, the
  object takes the camera's rotation. No sky glist in the data has 0x8.
- **Position:** if glist flag **0x2** is clear, the position is the **placement pos**. If it is set, the position is the
  **camera position** (`viewer->viewMatrix` row 3), so the object is infinitely far and moves with the camera.

In the data:
- **Camera-centred (0x2 set):** `SkyBox1-terra` (±900, flags 0x222), `moon`, `Moon`, `skybox` (0x22/0x23), `Skyline`,
  `SKY_StarField` and `SKY_Earth` (0x222). Export them at the world origin, or parented to the camera; their size
  (±900..2000 m) is authored as seen from the camera.
- **World-anchored (0x2 clear):** `mountains`, `Mountains_skybox*`, `Sky_Tower*`, `Sky_Wall*`, `Skyline_Buildings*`,
  `tower(skyobject)`, `SKY_Rain`, the light shafts and the Underwater_Silo section. Place them at pos with
  `RotMatrix(rot)`. Most have pos (0,0,0) and are modelled in world coordinates; the skyline and mountains sets have a
  nonzero pos (for example (24.9, −127.4, 289.3) with rot.y −0.3).
- **Slot overwrites:** only the last placement per slot survives, and the exporter must reproduce this.
  - HendersonC: `moon_old` is replaced by `SkyBox1-terra` in slot 0.
  - HendersonD: `mountains` is replaced by `moon` in slot 1.
  - CastleCourtyard: `moon` is replaced by `skybox` in slot 0.
  - EvilBase: two `Light_rays` share slot 0, so only the second is drawn.
- **Lightning:** `Thunderhead` (TowerB/TowerC, slot 3, k1 = 1) has an empty mesh (0 tris). It exists to time the
  lightning flashes: thunder SFX, `SuperHappyLightningFlag` and the Lightning bolts.
- **SpaceStationD (0x0700001b):** the sky is additionally rotated about X by frame·(1/1440) rad.

Confidence: confirmed.

## 242 Emitter: `Emitter_CreatePlist` 0x68470 → `Emitter_Create` 0x68290

- **Call:** the dispatch builds `mtx = Quat_QuatToMat(quat)` and sets `mtx.pos = pos`, then calls
  `Emitter_CreatePlist(&mtx, lvl)`. That calls `Emitter_Create(hash k0, &pos, &up, duration, k2, k3, k4, k5/60)`, where
  `up` = matrix row 1 (m4..m6). **The emission axis is the quaternion's up vector.** rot is unused.
  - The common quat (1,0,0,0) is 180° about X, so it emits straight down.

| key | off | meaning | seen |
|---|---|---|---|
| 0 | 0x2c | particle config hashcode (type 0x0c) | always 0x0c000004 |
| 1 | 0x30 | duration: 0 = forever; 1 = one burst (each particle slot fires once); N = emits for N/60 s | always 0 |
| 2 | 0x34 | enable switch: emits only while set | 0, 78..201 |
| 3 | 0x38 | disable switch: emits only while clear | 0 |
| 4 | 0x3c | output switch: set to 1 while any particle is alive | 0, 141 |
| 5 | 0x40 | pulse period P = k5/60 s: emits for P/2, pauses for P, repeats | 60..600 |

**Particle configs** come from block **0x30** (`parsemap_block_particles` 0x68020). The only 0x30 block is in the shared
FX pack **0100002e**, which every bundle contains (18 configs, 0x0c000000..0x0c000011). File layout:
- u32 count
- per config:
  - u32 `4 | flag<<8`
  - u32 hash
  - u32 texture hash (-1 = none; → `Tex` index)
  - u32 glist hash (-1 = none)
  - u16 maxParticles
  - u16 newPerFrame
  - f32 size
  - f32 lifeBase, lifeRand
  - f32 gravity (× 9.8)
  - f32 speedBase, speedRand
  - f32 pitchBase, pitchRand
  - f32 yawBase, yawRand
  - u32 nKeys
  - nKeys × {f32 r, g, b, a, t} (RGB are halved on load)

In memory (0x3a bytes) the fields sit at: keys* +0, tex +4, glist +8, life +0xc/+0x10, gravity +0x14,
speed +0x18/+0x1c, pitch +0x20/+0x24, yaw +0x28/+0x2c, size +0x30, nKeys +0x34, max +0x38, rate +0x3a, flag +0x3c.

**Each spawn** (`Emitter_Update` 0x68510):
- life = base + rand·lifeRand
- dir = (cos yaw·sin pitch, cos pitch, sin yaw·sin pitch), in the emitter frame (y = up axis)
- speed = base + rand·speedRand
- vel.y −= gravity·9.8·dt each frame
- colour interpolated along the keys by age/life

**Drawing** (`Emitter_Draw` 0x68930):
- With no glist: D3D point sprites (`psiDrawParticleList`), size·100·screen scale, animated texture frames. Blend mode:
  flag == 0 passes 1 to `d3dSetupRenderStatesAndFog`.
- Otherwise: each particle is drawn as the glist, spinning.

**0x0c000004** (every placed emitter): 20 max, 4 per frame, texture 0x03000082, size 0.173, life 0.667 s, speed 1 + 2·r,
pitch ±π/2 (a hemisphere about the axis), gravity 0.612, colour (255,151,0) fading to black. These are orange sparks
falling from the emitter. The placement glist `ParticleEmmiter` (14 tris) is **not drawn**, since Emitter_Create passes
a NULL glist.

- **Data:** EvilBase (35), EvilSilo (16), HendersonB (1).
- **Export:** an empty at pos oriented by the quat. Optionally add a Blender particle system with these parameters.
- **Confidence:** confirmed. The meaning of the flag bit and the exact point-size scale are uncertain.

## 33 Ripples: `Ripples_Create` 0xce200 / `Ripples_Update` 0xce280

- **Keys:** k0 (s16) = radius and k1 (s16) = mean interval in 1/60-s frames. If both are 0, they become k0 = 1, k1 = 30.
  Seen: k0 1..7, k1 1/2/10/30.
- **Behaviour:** each frame, with probability 1/k1, it spawns a ring at pos + (U(−k0, k0), 0, U(−k0, k0)). The ring is a
  gas object using entity 0x02000129 `watereffect`: scale 0.05..0.1 and growing, grey 127, fading out, random spin.
- **Visual:** the generator itself is invisible (`RIPPLE_GEN`, 0 tris).
- **Export:** an empty or marker on the water surface.
- **Data:** HendersonB, HendersonA, TowerC, HendersonD.
- **Confidence:** confirmed.

## 34 Ladder: `Ladder_Create` 0xaed30

- **Keys:** only k1 (u16 → data+2, always 0) is read; k0 is never read. It calls `Controls_StraddleTest`.
- **Visual:** draws the placement glist at pos with the object Euler, opaque (the glist flags are not copied).
  - Glists: `Ladder` (68-310 tris), `OBJECT_Ladder`, `OBJECT_DarkLadder`, and so on.
  - About a third have 0 tris, because the ladder is in the static world geometry.
- **Climb volume:** the glist bbox (`PlayerCollLadder` 0xae930).
- **Confidence:** confirmed.

## 216 RigidBody: `RB_Create` 0xc0990

- **Call:** `RB_Create(&pos, &rot, celglist, lvl)`.
- **Keys:**
  - k0 = mass (0 → 3; seen 1, 2, 3, 8, 600).
  - k1 is never read (0).
  - k2 = `RB_Init` mode. 0 = a free body with gravity; nonzero = hanging/pendulum with no gravity; 3 = stiffer. It is
    always 1 in the data.
  - k3 (byte) = attach a dynamic light. It uses `psiLight_Create`, which returns 0 on Xbox, so it has no effect there.
  - k4 = collision SFX (0 → none; always 65535).
- **Inertia:** from the glist bbox (a cuboid).
- **Visual:** the placement glist at pos with the object Euler. Glists: `rb_redlamp`/`greenlamp`/`whitelamp`,
  `kitchen-panswing1..8`, `bucketswing`, `OBJECT_WellBucket`, `obj_ship2_concreteblock03`. The meshes hang below their
  origin (bbox max y ≈ 0), so **the origin is the pivot**.
- **Export:** a static mesh in its rest pose.
- **Confidence:** confirmed.

## 223 Env: `Env_Create` 0x68a30

- **Call:** `Env_Create(&pos, &rot, celglist, lvl)`. It allocates a 0x44-byte environment record, but only if a cel
  contains pos and that cel has no env yet (so the first one per room wins).
  - It stores the record at cel+0x2c and sets `GlobalEnv`.
  - It calls `InitDrops(k0)`. **The last Env sets the level-wide drop type.**

| key | meaning | seen |
|---|---|---|
| 0 | DropType: 0 = **snow** (4096 point sprites, texture 0x03000045, slow drifting fall; also enables breath puffs); 1 = **rain** (64 drops, 128 in Tower A-C, each drawn as entity 0x02000290 `Raindrop`, falling 0.5 units per 1/60 s; Castle levels also draw 0x02000039 dust); 2 = **bubbles**, rising (2048, texture 0x03000087); 255 = none | 0, 1, 2, 255 |
| 1 | byte env+0x40, only stored when k0 == 0 | 0, 1 |
| 2 | byte env+0x42 | 4, 10 |
| 3 | wind strength ×0.1 (env+0x24/+0x28, gusting in `Env_Update`). Wind direction = the dir row of `RotMatrix(rot)`; `Env_GetWindVel` scales it along that axis relative to the cel | 0, 6, 10 |
| 4-6 | bytes env+0x3c..0x3e | always 0 |
| 7, 8 | floats env+0x30/+0x34 | always 0 |
| 9 | sun present | 0, 1 |
| 10, 11 | sun azimuth a, elevation e, in degrees | 182/25, 302/19 |

- **Sun:** `SunVector = 1000·(sin a·cos e, −sin e, cos a·cos e)`. `HUD_UpdateLensFlarePane` puts the sun at
  head − SunVector, so SunVector points **from** the sun. It is only used for the lens flare.
  - Sun data: PowerStationA1/A2 only.
  - Export: optionally a glTF directional light pointing along SunVector.
- **Visual:** invisible marker (`Rain_generator`, `SNOW_gen`, `Bubble_gen`, `Environment`, `Weather`, `EnvMarker`).
- **Weather per level (k0 of the last Env):**
  - rain: HendersonA-D, TowerA, TowerB, TowerC
  - snow: CastleExterior, CastleCourtyard, CastleIndoors1/2
  - none: PowerStation A1/A2
  - bubbles: EvilBase (the Weather record says none, but Bubble_gen comes after it)
- **Confidence:** likely. Keys 1, 2 and 4-8 are unknown.

## 243 EnvTree: `Env_Tree_Create` 0x68fc0

- **Call:** `Env_Tree_Create(&pos, &rot, lvl)`. **No glist:** the placement glist `Tree` (12 tris) is ignored.
- **What it creates:** OBJECTTYPE_TREE with `AnimObjectNew(obj, k0)`.
  - k0 = skin hash, always **0x05000003**. The skin exists in HendersonA-D.
  - Then `AnimScriptAdd(obj, k1)`. k1 = script, always **0x0600008c**; it is swapped for 0x0600008d with 50% chance.
  - The script loops from a random frame, except in 0x07000043 (AllCharacters).
  - effectFlags: 0x80 is set (dynamic lights apply) and 0x20 is cleared.
- **Export:** skin 0x05000003 at pos with the object Euler, in bind pose or with the sway animation.
- **Data:** HendersonB (6), HendersonD (2).
- **Confidence:** confirmed.

## 113 Rotor: `rotor_init` 0xa88d0 / `rotor_update` 0xa8940

- **Call:** `rotor_init(&pos, &rot, celglist, 0, byte k0, byte k1)`. The disassembly at 0xa5263 confirms the arguments.
  - k0 is never read.
  - k1 = switch channel. When it is nonzero, the rotor spins only while `switch_channels[k1] == 0`. It never occurs in
    the data, so every rotor always spins.
- **Axis and speed:** each frame `rotation += rot · FRAME_RATE_MUL`. The **placement rot is both the initial Euler
  orientation and the angular velocity in radians per 1/60 s** (object Euler). Examples:
  - `office_N_hologram` rot.y 0.017 → 1.0 rad/s about Y.
  - `obj_office_fan` 0.052 → 3.1 rad/s.
  - `comroom_fanblades` 0.035 → 2.1 rad/s.
  - `fanblades-large` rot.x 0.349 → 20.9 rad/s about X.
  - `rotating blades` rot.z 0.349 → 20.9 rad/s about Z.
- **Export:** the glist with an Euler animation, rot + t·60·rot.
- **Confidence:** confirmed.

## 44 Lightning: `Lightning_Create` 0xa3820 / `Lightning_Update` 0xa3950

- **Call:** `Lightning_Create(&pos, &rot, lvl)`. No glist: `Lightning1` is not drawn.
- **Keys:**
  - k0 → data+0x38 (0; never read).
  - **k1 = group** (u16; 1 or 2).
  - k2 → data+0x3e (0).
- **Pairing:** a new Lightning scans every object for an earlier one with the same group. On a match it becomes the
  owner of 7 extra segment objects and a bolt from its own pos to that object's pos, and the matched one is demoted to
  plain GFX.
- **During sky flashes** (`SuperHappyLightningFlag`), the bolt is drawn as beams of entity 0x0200032c `Lightning`,
  jittered by FUN_000b82e0.
- **Data:** HendersonA only (11 points: 6 in group 1, 5 in group 2).
- **Export:** empties, optionally joined by a polyline.
- **Confidence:** likely. The chain order is not fully traced.

## 62 Wire: `Wire_Create` 0xb03b0

- **The dispatch negates rot.z first:** `rot.z = −rot.z`. So the object matrix is `View_RotTransMatrix(rot.x, rot.y,
  −rot.z)`.
- **Keys:**
  - k0 = wire id (u16 → data+2; 1..13).
  - k1 = type (data+0): 0 = hand-over-hand wire, 1 = inactive, 2 = zipline.
  - k2 = data+4 (0).
  - k3 = switch channel set while the player is on the wire (0).
- **Visual:** the placement glist, drawn normally. Glists: `Wire`, `Wire_Cable`, `wire_*` (8-10 tris), `Zipline*` and
  `OBJECT_Zip_Line_1` (58-60 tris).
- **Usable line:** centre ± (radius − 2) along the spherical direction (yaw rot.y + π/2, pitch rot.z) (`Player_CollWire`
  0xaf5a0).
- **Confidence:** confirmed.

## 78 Light: `Light_Create` 0x6d530

- **Call:** `Light_Create(&pos, r = k1, g = k2, b = k3, radius = (float)(u32)k0, switch = k4, -1, 0, 1.0, k5, k6, k7, k8)`.
  No glist: `oLightPoint` (256 tris) is only an editor marker.
- **light_tag fields:** pos +8, lifetime +0x14, switch +0x16, flicker fields +0x18..+0x20, radius +0x30, intensity +0x34
  (1.0), r/g/b +0x38/+0x39/+0x3a, type +0x3b (0), enabled +0x3c.

| key | meaning | seen |
|---|---|---|
| 0 | radius / range (m) | 6, 8 |
| 1-3 | R, G, B bytes | (255,121,103) ×4, (127,127,127) |
| 4 | on/off switch channel (0 = always on) | 0 |
| 5 | flicker duration in frames (0 = never) | 0 |
| 6 | steady frames between flickers | 0 |
| 7 | flicker bit mask | 1 |
| 8 | flicker SFX (0 → none) | 65535 / absent |

**Use in game:**
- `Lights_CalcClosestLights` 0x6d8f0 picks lights within radius + the object's radius, for objects whose effectFlags has
  0x80. The world geometry is prelit.
- `psiLight_SetLights` → `d3dSetLight` passes pos, 1/radius and colour = byte·intensity/256 for up to 4 lights.
- The falloff shape (in the vertex shader) was not traced; 1/radius suggests linear.

**glTF (KHR_lights_punctual):**
- point light at pos
- `color = (k1,k2,k3)/255`
- `range = k0`
- intensity: choose one (for example 1-5 W-equivalent)

Data: 5 placements, all in HendersonA (4 in the map, 1 template at the origin in pack 01000166).

**Most level lights are in block 0x26**, which Light_Create does not handle. They are the map's "ambient radiators"
(`Light_SetAmbientRadiators` 0x6e3b0, handled only on the map-file pass).
- Layout: u32 n, then n × 32 bytes {vec3 pos, f32 radius², f32 1/radius², f32 r, g, b (0..1)}.
- Each record makes the same kind of light_tag: radius = sqrt(+0x0c), colour = rgb·255, type 0.
- Every map file has one, with 4-163 lights of radius 1-50 m.
- The shared pack 010000cf also has one 50 m white light at the origin.

Export these as glTF point lights too. Confidence: confirmed.

## 63 CreepWall: `CreepWall_Create` 0xaf3c0

- **Keys:** k0..k3 are u16s stored at data+0/2/4/6. Seen: k0 = 0/2, k1 = 1/3, k2 = 0, k3 = 113 (always). Their meaning
  was not traced; k3 matches the WallHug_Blocker DynamicObjects' disappear switch, so it is probably a switch.
- **Visual:** invisible. The glist `Wall Hug Window/Short/Long` has 0 tris and flags 0x50 (hidden), with a flat bbox
  (for example ±5.3 × ±0.6 × 0).
- **The creep line:** through the object centre along local X (yaw rot.y + π/2), half length = object radius
  (`Player_CollCreepWall` 0xaeda0).
- **Data:** CastleExterior ×2, PowerStationA2.
- **Export:** a volume from the glist bbox with the object Euler.
- **Confidence:** likely.

## 43 LeafGen: `LeafGen_Create` 0xce180 / `LeafGen_Update` 0xce1d0

- **Keys:** none.
- **Behaviour:** about every 0.5 s (Rand(fps/2) == 0), FUN_000cdef0 spawns one leaf (entity 0x020002ca `Leaf`)
  - at a random point in a cube of half-size = object radius around pos
  - drifting horizontally along (sin rot.y, 0, cos rot.y)
- **Visual:** the generator glist `Leaf_emmit` has 0 tris and is hidden.
- **Data:** HendersonD ×2.
- **Confidence:** confirmed.

## 212 Cloud: `Cloud_Create` 0xa36f0 / `cloud_update` 0xa37a0

- **What it creates:** an OBJECTTYPE_CLOUD object with the glist, set to draw in no views. It is registered as sky slot
  k4 via `View_AddSkyObj(k4, NULL, NULL, NULL, obj, 0, 0, 0, pass = 1)`.
- **Keys:**
  - k0 = height above the camera (int). The update uses FILD, so the value really is an integer.
  - k1, k2 = x and z scroll ×0.001 units per 1/60 s.
  - k3 = wrap period (int units).
  - k4 = sky slot.
  - Data: 8 / 15 / 2 / 100 / 3.
- **Motion:** each frame the position is (player.x + offX mod k3, player.y + k0, player.z + offZ mod k3). The sky pass
  draws it at camera y + k0, with the object's rot and scale.
- **Glist:** `CloudLayerXR9`, a 400×400 plane with 32 tris and flags 0x220.
- **Data:** CastleCourtyard only.
- **Export:** the plane at pos.xz with y = ground/camera height + 8.
- **Confidence:** confirmed.

## 227 Flicker: `Flicker_Create` 0xcda40 / `Flicker_Update` 0xcdae0

- **What it creates:** draws the glist (flags copied) tinted by a psiLight's on/off state.
- **Keys:**
  - k0 (u16): nonzero means spark/smoke gas (0x020002d1..d5 `Sparkler_1..5`) comes out along the up vector while the
    light is out.
  - k1: a switch set to 1 while it is dark.
- **On Xbox it does nothing:** `psiLight_Create` returns 0, so the object always takes the "lit" branch (tint → 255, no
  smoke, k1 cleared).
- **Data:** 1 placement (SnowBlind; glist `Fuse boxs FX`, 0 tris; k0 = 0xffffffff, k1 = 126). Nothing to draw.
- **Confidence:** confirmed.

## 213 OneSided: `OneSided_Create` 0xdb3e0 / `OneSided_Update` 0xdb430

- **Call:** `OneSided_Create(&pos, &rot, celglist)`. No keys.
- **Visual:** draws the glist with the object Euler, but hides it (effectFlags 0x10) when dot(dir row, pos − camera) < 0,
  that is, whenever the camera is on the +dir side.
- **Data:** occurs in no level.
- **Confidence:** confirmed.

---

## Open questions
- **Env:** keys 1, 2 and 4-8. They are constant (1, 4, 0...) in almost all data.
- **CreepWall:** keys 0-2.
- **Wire:** key 2.
- **Emitter:** the config's flag bit (the blend mode passed to `d3dSetupRenderStatesAndFog`) and how the point-sprite size
  maps to world units.
- **Lights:** the exact attenuation curve of the vertex shader.
- **Lightning:** the pairing order when a group has more than 2 points.
- **Object packs:** the template packs 010000f3 and 01000166 (bundle type 0) carry placements at the origin. Whether their
  dynamic placements are ever instanced depends on how the object packs' 0x1a blocks are loaded; that is outside this
  group.
