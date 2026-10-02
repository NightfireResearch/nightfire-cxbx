# Effects: function by function

All 56 functions of the `effects` subsystem (`tools/subsystems_action.txt`, four ranges), none ours yet. "Size" is
Ghidra's body size from `tools/function_coverage.py effects`; "Callers" from `tools/xrefs_action.json` ("table" =
a `control_funcs` slot). Names in *italics* are invented; PS2 names were matched by body and call shape against
`/PS2_EU_51258/ACTION.ELF`. Custom register conventions (MSVC whole-program optimisation) are marked **reg** and
matter for injection: such a function can only be replaced together with its callers, or through a naked wrapper.

Structures, flags and tables referred to here are in [data.md](data.md).

## 1. Inventory

### 1.1 Emitter (0x68020-0x68a30)

| address | name | size | callers | what it does |
|---|---|---|---|---|
| 0x68020 | `parsemap_block_particles` | 624 | `parsemap_handle_block_id` (level block 0x30) | builds `ParticleEmitterConfig` records (+ key frames) on the heap and registers them in the hash table |
| 0x68290 | `Emitter_Create` | 480 | `Effect_Create`, `Emitter_CreatePlist`, `Bullet_DoTrails`, `GT_Update`, `Lock_Update`, our `SpaceLaser.cpp` | creates a type 50 object from a config hash, a position and an emission axis |
| 0x68470 | `Emitter_CreatePlist` | 160 | our `parsemap.cpp` (placement 242), `Script_CreateEntity` | placement keys to `Emitter_Create` arguments |
| 0x68510 | `Emitter_Update` | 1056 | table (type 50) | switches, pulse, duration; ages, moves and recolours particles; spawns; deletes itself when spent |
| 0x68930 | `Emitter_Draw` | 256 | `View_DrawObjList` (control flag 0x100) | point sprites through `psiDrawParticleList`, or one glist per particle |

Outside the range but part of it: `FUN_00067ea0` (*Emitter_KeyColour*, 0x67ea0, ~380 bytes, filed under ai.drones
by address) is called only by `Emitter_Update`.

### 1.2 Effect, Particles (0x6fa30-0x705f0)

| address | name | size | callers | what it does |
|---|---|---|---|---|
| 0x6fa30 | `Effect_InitLists` | 32 | `ResetMap_GameInit` | `DList_Init(&EffectList, 0x58, 200)` |
| 0x6fa50 | `Effect_RicochetProb` | 80 | `Bullet_CollisionHandler` | `ricochet ? Rand_Rand(ricochet) & mask : 0` for a material (clamped to 22) |
| 0x6faa0 | `FUN_0006faa0` (*Effect_LinkToCel*) | 48 | `Effect_Player` | **reg** (EAX = node, EDX = cel): push the decal on `cel+0x10`, owner cel set, owner object cleared. Inlined on PS2 |
| 0x6fad0 | `FUN_0006fad0` (*Effect_LinkToObject*) | 128 | `Effect_Bullet`, `Effect_Player` | **reg** (ESI = node, EDI = object): move the decal matrix into the object's local space (inverse of `obj->mtx`), push on `obj+0x10`. Inlined on PS2 |
| 0x6fb50 | `Effect_UnLink` | 80 | `FUN_0006ff70` | unlink a decal node from its owner chain (fixing the owner's head) |
| 0x6fba0 | `Effect_RemoveSObjs` | 112 | `control_movement_object_handler`, `Destroy_Smash`, `FuseBox_Update` | free every decal on an owner (`+0x10`) back to `EffectList` |
| 0x6fc10 | `Effect_Body` | 80 | `Effect_Create` | **reg** (EAX = hit; stack obj, cel, flags): body-hit sound, alertness 0.5 |
| 0x6fc60 | `Effect_Laser` | 592 | `Effect_Create` | **reg** (EAX = hit, EBX = cel; stack obj, flags): hot-spot gas, sparks, smoke wisp |
| 0x6feb0 | `FUN_0006feb0` (`Particles_Create` on PS2) | 144 | `Flicker_Update` | creates a type 12 object around a PS2 particle system; dead on Xbox (below) |
| 0x6ff40 | `Particles_Update` | 48 | table (type 12) | deletes the object when `psiParticleUpdate` (Xbox stub returning 0) says so |
| 0x6ff70 | `FUN_0006ff70` (*Effect_AllocNode*) | 64 | `Effect_Player` | **reg** (returns in EAX): take a decal node, recycling the oldest when the pool is empty. Inlined on PS2 |
| 0x6ffb0 | `Effect_Bullet` | 736 | `Effect_Create` | **reg** (ECX = hit, EAX = bullet object; stack cel, flags): impact sound, three smoke slots, bullet-hole decal |
| 0x70290 | `Effect_Player` | 416 | `Effect_Create` | **reg** (EAX = hit, EBX = walker; stack cel, flags, volume): footstep sound, footprint decal |
| 0x70430 | `Effect_Create` | 448 | `Bullet_CollisionHandler`, `AnimObjectUpdate`, `AnimProcessScriptCmds`, `Script_Interp` | the impact dispatcher (cdecl: hit, object, flags, volume) |

### 1.3 Weather, corona, swoosh, DynamicObject, Apocalypse (0xa3180-0xa4790)

| address | name | size | callers | what it does |
|---|---|---|---|---|
| 0xa3180 | `InitDrops` | 464 | `Env_Create`, `Env_Reset` | set the drop type, random velocity tables, clear the pools, wind and drop budget per type and level |
| 0xa3350 | `AddDrop` | 528 | `UpdateDrops` | place one drop in a random spot near the centre if it falls inside an active rain box |
| 0xa3560 | `DrawDrops` | 400 | `Game_Draw` | point sprites, or a glist per drop for type 1 |
| 0xa36f0 | `Cloud_Create` | 176 | our `parsemap.cpp` (212) | cloud layer that follows the player, registered as a sky object |
| 0xa37a0 | `cloud_update` | 128 | table (36) | scroll offsets modulo the wrap period, follow player 0 |
| 0xa3820 | `Lightning_Create` | 304 | our `parsemap.cpp` (44) | bolt end point; pairs with an earlier one of the same group |
| 0xa3950 | `Lightning_Update` | 224 | table (51) | during sky flashes: re-randomise and position eight beam segments |
| 0xa3a30 | `InitRainBox` | 16 | `Env_Reset` | `RainBoxRoot = 0` |
| 0xa3a40 | `RainBox_Create` | 112 | our `parsemap.cpp` (50) | invisible type 1 object, appended to the rain-box chain through `obj+0x1c` |
| 0xa3ab0 | `RainBox_Clip` | 400 | `UpdateDrops` | clip each box against a 30-unit cube round the drop centre; returns the number active |
| 0xa3c40 | `Swoosh_Create` | 176 | `Bullet_init`, `Bullet_DoTrails` (x2) | stretched glist between two points that fades out (tracers, trails) |
| 0xa3cf0 | `Swoosh_Update` | 336 | table (65) | count down, fade, optional drift, rebuild the matrix along the segment facing the camera |
| 0xa3e40 | `Corona_Create` | 112 | our `parsemap.cpp` (252) | glow sprite placement |
| 0xa3eb0 | `Corona_Update` | 176 | table (69) | line-of-sight fade in/out by 16 per update, every other frame |
| 0xa3f60 | `Sunreflection_Update` | 160 | table (25) | follows the camera, grows x1.1 to 100 then shrinks /1.1 below 2 and dies. No creator found on Xbox |
| 0xa4000 | `DynamicObject_Create` | 144 | our `parsemap.cpp` (59) | glist shown/hidden/deleted by channels, faded by distance |
| 0xa4090 | `DynamicObject_Update` | 240 | table (82) | see 2.8 |
| 0xa4180 | `Apocalypse_Create` | 128 | our `parsemap.cpp` (74) | screen shake / rumble / flash-bang event |
| 0xa4200 | `Apocalypse_Update` | 256 | table (84) | once triggered, count down driving rumble, `viewer->apocalypseEffect` and flash-bang |
| 0xa4300 | `UpdateDrops` | 1168 | `Env_Update` | the weather simulation, see 2.6 |

### 1.4 Debris, Bubble, Gas, Flicker, Leaf, Ripples, BodyGlow (0xcccf0-0xceb30)

| address | name | size | callers | what it does |
|---|---|---|---|---|
| 0xcccf0 | `Debris_CreateEx` -> **`Debris_Create`** (PS2) | 368 | `Effect_Create`, `Debris_Create_Loop` | n type 16 debris pieces from a hit and an `EffectInfo` row |
| 0xcce60 | `Debris_CreateEx` (PS2 name too) | 400 | `Explode_Update` | n pieces from a position, direction, speed and a graphics array |
| 0xccff0 | `Debris_Update` | 192 | table (16) | type 16: fade, count down, move, gravity unless `curState`, spin |
| 0xcd0b0 | `Bubble_Update` | 144 | table (6) | grow, rise with damping until the room's top. No creator found on Xbox |
| 0xcd140 | `Gas_Init` | 1056 | 17 functions (2.9) and our `Searchlight.cpp` | creates a type 14 gas sprite |
| 0xcd560 | `gases_smoke` | 1248 | table (14) | per gas type movement, spawners, colour/lifetime |
| 0xcda40 | `Flicker_Create` | 160 | our `parsemap.cpp` (227) | light-tinted glist; on Xbox permanently lit |
| 0xcdae0 | `Flicker_Update` | 1040 | table (38) | tint towards the light state, sparks and particles when dark (dead on Xbox) |
| 0xcdef0 | `FUN_000cdef0` -> **`Leaf_Create`** (PS2) | 432 | `LeafGen_Update` | n falling leaves (glist 0x20002ca) in a cube round the generator |
| 0xce0a0 | `Leaf_Update` | 224 | table (46) | flutter down, wander, spin, die after `256 / FRAME_RATE_MUL` ticks |
| 0xce180 | `LeafGen_Create` | 80 | our `parsemap.cpp` (43) | invisible generator |
| 0xce1d0 | `LeafGen_Update` | 48 | table (49) | one leaf with probability `1 / (FRAME_RATE_INT / 2)` per tick |
| 0xce200 | `Ripples_Create` | 128 | our `parsemap.cpp` (33) | invisible generator |
| 0xce280 | `Ripples_Update` | 560 | table (52) | random water rings (hand-built type 1 gas, glist 0x2000129) |
| 0xce4b0 | `BodyGlow_Create` | 240 | `Player_Init`, `NDrone2_DefaultInit`, `AnimDebugCreate` | glow object plus 15 limb segments for a character |
| 0xce5a0 | `BodyGlow_Delete` | 64 | `BodyGlow_Update`, `Drone_Delete` | mark all 16 for deletion |
| 0xce5e0 | `BodyGlow_Update` | 1360 | table (70) | vision-mode outline of the owner's skeleton, see 2.10 |

Helpers called from here that are not in the subsystem and not ours yet: `Vec_MakeOrthonormalBasis`, `DList_*`,
`Rand_Rand`/`Rand_Random` (wrapped by our `util/Random.cpp`), `Env_GetWindVel`, `Player_SetFlashBang`,
`View_AddSkyObj`, `View_DrawObjList`/`View_DrawObjectGlist`/`View_AddObjects`, `control_link_object_to_cel`,
`build_LinkToRoom`, `Control_CreateObjEx`, `psiBuildMatrixPalette`, `Lights_CalcClosestLights`, `FUN_000b82e0`
(bolt generator), `hashtable_set_sub_object_to_entity_gfx` 0x6b6d0. Already ours: `control_create_object`,
`Control_SetGList`, `hashtable_set_object_to_entity_gfx`, `hashtable_getitem`, `hashtable_hashcode_to_celglist`,
`psiDrawParticleList`, `psiDrawObjectMatrix`, `psiSetTweakARGB`, `psiCopyToSP`, the two psi stubs,
`View_RotTransMatrix`, `View_SetDrawIn*`, `Mat_Align2Dir`, `Mat_Scale3f`, `ApplyMatrixLV`, `PositionBeam`,
`Collide_LineOfSight`, `Sound_Play3D`/`Sound_PlayExt`, `Input_RumbleStart`, `psiLight_Create`.

## 2. The important ones

### 2.1 `Effect_Create` 0x70430

`void Effect_Create(HITDATA_tag *hit, obj_tag *obj, uint flags, float volume)` [dec, dis]:

1. Save the normal (hit+0x10..0x1f). Return unless `obj` is non-null and the material is non-zero.
2. `cel = hit->hitCel`, else `hit->hitObj->inCel`, else null.
3. Material 0x10 (GLASS): negate the normal.
4. `Vec_MakeOrthonormalBasis(&t1, &t2, &normal)`; `hit+0x20 = t1 + t2`.
5. If `flags & 0x20`: `Emitter_Create(EffectInfo[m].particleEmitter, &hitPosition, &normal, 0, 0, 0, 0, 0)` (a
   single burst along the normal), then if `randomDebris`, `Debris_Create(hit, &EffectInfo[m], cel,
   rand(randomDebrisProbability) + 1)`.
6. In multiplayer stop here (the glass normal stays negated in the hit record). Otherwise restore the normal and
   call one of `Effect_Bullet` (0x01), `Effect_Player` (0x02), `Effect_Laser` (0x10), `Effect_Body` (0x80), in that
   order of precedence. The 0x40 bit (sound) is passed through.

There is no dispatch on an "effect type": the material decides *what* (through `EffectInfo`) and the flags decide
*which kinds* of effect.

### 2.2 `Effect_Bullet` 0x6ffb0

1. `rot` = rotation facing back along the normal (two `maybeAtan2`), `nrot = Vec_NormToRot(normal)`.
2. `flags & 0x40`: `Sound_Play3D(impactSound, &hitPosition, 100, -1, -1, 0, 0, 0)`.
3. Smoke when the shooter (`bulletData+0x24`) is a player (type 3), otherwise with probability 1/2: for each of the
   three `HESMOKE` slots with a graphic, choose position and rotation by the slot's two bytes (data.md section 2) and
   call `Gas_Init(cel, pos, rot, NULL, s0, s1, gfx, type, c0, c1, life)`.
4. Bullet hole when `bulletHole != 0`: allocate a node (recycling the oldest, the unlink inlined),
   `Mat_Align2Dir(node+0x1c, normal, up, forward)`, translation = hit + 0.01 x normal, `ownerObj = hit->hitObj`,
   graphic from the hash; link to the cel if `hit->hitCel`, else to the hit object in its local space
   (*Effect_LinkToObject*).

### 2.3 `Effect_Laser` 0x6fc60 - and the laser "corona"

Called with flags 0x10 only (`Bullet_CollisionHandler` overwrites the flags with 0x10 for weapons 78/79), so no
sound, no emitter and no debris come through `Effect_Create` for a laser hit. Per hit per tick:

1. (`flags & 0x40`, never for the laser) `Sound_PlayExt(footstepSound)`.
2. If the material's `laserGas` is set (every hard material: 0x2000215, "lazerhotspot"):
   - `Gas_Init(cel, hit + 0.005 x normal, rot facing out along the normal, NULL, 2.0, 3.0, laserGas, type 5,
     0x7f7f64ff, 0x643c0018, life 10)`: a static hot spot that grows from 2 to 3 and fades from pale yellow to dim
     red over 10 ticks. One per tick, so about ten overlap.
   - With probability 59/100 (`Rand_Rand(100) > 40`): a **spark**, `control_create_object(0x20, &hitPosition)`, type
     16, life 1 (`data+0x1c = 1`, fade 0), scale 2-4, random `rotation.z`, nudged 0.01 along the normal, graphic one
     of 0x20002d1-0x20002d5 at random, effectFlags |= 0x20, linked to `cel`. `Debris_Update` deletes it after one
     tick (life 1 to 0, gravity applied once). The same five graphics are `Flicker_Update`'s sparks, so they are
     spark sprites; the random `rotation.z` and scale only matter on the billboard path, so their glists are
     presumably billboards (effectFlags 8) [inf].
3. On odd frames (`NumFramesUnpaused & 1`): a smoke wisp, `Gas_Init(cel, hit + randHalf(0.1) * (hit+0x20), zero
   rot, NULL, 0.005, 0.02, 0x2000039, type 0, 0xc8c8c87f, 0x20202006, life 21)`.

**What draws the magenta point sprites at the safe pin** (`tools/ui/scripts/laser_safe.txt`): not `Effect_Laser`.
Its sparks are type 16 glist objects, drawn by `View_DrawObjectGlist`; nothing in the type 16 path emits particles.
The only callers of `psiDrawParticleList` are `Emitter_Draw` and `DrawDrops` [xref]. The emitters a laser hit can
create are `Lock_Update` 0xd1577 (a laser-meltable lock: for each laser bullet in its hit list, weapon 0x4e or
0x4f, it creates `Emitter_Create(0xc00000a, &hitPosition, &normal, 0, ...)` - a one-shot burst - and lowers the
lock's green and blue tint by 3 or 10 until it breaks at under 4, which is the pin glowing red) and
`Bullet_DoTrails` 0x229b2 (the same emitter when a laser hits a bullet-type gadget). So the magenta sprites are
emitter 0x0c00000a bursts from `Lock_Update`, with the hot spot gas and sparks from `Effect_Laser` on top. Confirmed
with `objlog 50` / `objlog 58` in the replay: 15 emitters at the hit point 30 frames into the burn, none before,
and the hit bolt's tint lowered to `ffababff` (README section 4). Commit 045447f's message attributes the sprites to
the sparks; that was wrong.

### 2.4 `Emitter_Create` 0x68290, `Emitter_Update` 0x68510, `Emitter_Draw` 0x68930

`obj_tag *Emitter_Create(HASHCODE config, _VECTOR *pos, _VECTOR *axis, float duration, uint enableSw, uint
disableSw, uint aliveSwOut, float pulsePeriod)`: null if the hash is 0 or unknown. Builds a matrix with
`Vec_MakeOrthonormalBasis(axis)` (up = axis), `Control_CreateObjEx(0x128 + 0x2c n, NULL, NULL, &mtx, NULL, NULL,
1, 0x104, 1.0, 0, 0xff, 0xff, 0xff)` (control flags 0x104: fixed radius, matrix authoritative), type 50, carves the
five arrays out of the block from +0x4c, sets every age to 99999 and lifetime/colour to 0, `centrePoint = pos`,
effectFlags |= 0x21, radius = `(lifeBase + lifeRange) x (speedBase + speedRange)`.

`Emitter_CreatePlist(mtx, lvl)`: axis = the placement matrix's up row; duration = -1 for k1 = 0, 0 for k1 = 1, else
k1/60 seconds; pulse = k5/60; switches k2, k3, k4.

`Emitter_Update` per tick, in this order:

1. `centrePoint` = matrix translation; enable/disable switches into *enabled*.
2. Pulse: timer += `REC_FRAME_RATE`; on for half a period, off for a period.
3. Duration > 0: subtract `REC_FRAME_RATE`; below 0 set *expired*.
4. Clear *aliveSwitchOut*.
5. Age loop over all n: dead if `lifetime <= age` (alpha byte 0); otherwise colour = key colour at `age /
   lifetime`, `pos += vel x REC_FRAME_RATE`, `vel.y -= REC_FRAME_RATE x gravity x 9.8`, `age += REC_FRAME_RATE`,
   and set *aliveSwitchOut* (with `switch_channels_time`).
6. Spawn loop with budget *perTick*, stopping when the budget is spent, *finishing*, not *enabled* or pulse off:
   a slot qualifies if `lifetime < age` (strictly) and (not *expired* or never spawned): age 0, colour = key 0,
   direction from random theta (from the up axis) and phi, speed, `pos` = emitter position, lifetime, velocity =
   `ApplyMatrixLV(mtx, dir)`.
7. *allDead* = (all dead) | *finishing*. If (*finishing* and step 5 found all dead) or (*expired* and all dead):
   *finishing*++, and when it passes 2 the object is marked for deletion - three ticks after it is spent.

`Emitter_Draw` (from `View_DrawObjList` because of control flag 0x100) does nothing when *allDead*. With no glist
in the config: `psiDrawParticleList(positions, n, 0xffffffff, texture, colours, mode == 0, size)` - all n slots,
dead ones with alpha 0. With a glist: for each slot with alpha != 0, `psiSetTweakARGB`, spin += `REC_FRAME_RATE`,
`View_RotTransMatrix(spin, pos)`, `psiDrawObjectMatrix(glist)`. The spin is shared and advances per particle per
viewer drawn, so it is draw-rate dependent (the "draw mutates state" item in the architecture review).

### 2.5 `parsemap_block_particles` 0x68020

Reads the record count, then per record (stopping if a type byte is not 4) the fields listed in data.md 4.4,
`Mem_Malloc(0x40 + 0x14 x keys, 0x4204)`, copies the 0x40-byte header, points `keys` after it, reads keys (RGB x
0.5), `hashtable_additem(hash, config)`. The GameCube build warns "EMITTER TEXTURE MISSING" when the texture hash is
-1 (`docs/gamecube-checks.md` #40); Xbox stores 0.

### 2.6 Weather: `InitDrops`, `UpdateDrops`, `AddDrop`, `RainBox_*`, `DrawDrops`

`InitDrops(type)` from the level's Env placement: 32 random velocities (x/z +-0.02 - +-0.002 in the Castle indoor
levels - y -0.025 to -0.1; type 2 quarters them and makes y positive), clears 0x1000 drops (parked at y -1024),
wind zero except type 1 (y -0.5 per tick), budget 64 (128 in the Tower levels) for type 1, 0x1000 for type 0,
0x800 otherwise; `InitBodge` for the Castle exterior to courtyard range.

`UpdateDrops` (from `Env_Update`), unless paused or `DropType == -1`: uses viewer 4 when its byte +0x29 is set,
else viewer 0; no display list -> `Added = 999` (hidden). With `InitBodge`, the first tick fills every drop at
random within +-15 of the viewer and clears the flag. Otherwise: wind random walk clamped to +-0.01 (not type 1);
drop centre = viewer position (+7.5 along player 0's yaw for viewer 0); `RainBox_Clip` (none active -> hidden);
per-tick velocities = (`RandTable1` + wind) x `FRAME_RATE_MUL`; move every live drop by `RandTable2[i & 31]`, kill
it when it passes its floor (rain) or ceiling (type 2, rising); dead slots get `AddDrop` while `Added < 21`. The
non-rain path runs in chunks of 1000 through `psiCopyToSP` and a profiling hook, PS2 scratchpad leftovers that are
identities on Xbox.

`AddDrop` puts the drop within +-15 x/z of the centre, 7.5-15 above (type 2: +-15), accepts it if it lies in an
active box (x/z inside, above the box floor) and records the floor/ceiling in w; with wind it shifts x/z upwind so
the drop lands inside. `RainBox_Clip` intersects each box (glist bounds + position, rotation ignored) with the
cube; its emptiness test compares x min/max and z min/max but not y (the y minimum variable is reused) - keep it.

`DrawDrops` (from `Game_Draw`, after the shards): type 1 draws glist 0x2000290 at every live drop when the camera is
above its room's `shearHeight`, plus in the levels between CastleIndoors2 and PowerStationA1 (exclusive) the smoke
glist 0x2000039 tinted (0x24, 0x20, 0x20, 0x20); other types draw all `CurrentMax` drops as point sprites with
texture 0x3000045 (type 0) or 0x3000087, colour 0xff808080, size 0.2. What each type looks like (rain, snow,
bubbles) is not pinned down beyond "type 2 rises".

### 2.7 Corona 0xa3eb0, Swoosh 0xa3cf0, Cloud, Lightning, Sunreflection

- `Corona_Update`: only on frames whose parity differs from `curState` (random per corona, spreading the work).
  If `obj+0xd8` (drawn in the last two ticks): `Collide_LineOfSight(camera, corona, ..., mask 8)` -> effectFlags
  |= 0x2000 (list 2, drawn over the world) and alpha +16 to 255; else clear 0x2000 and alpha -16 to 0. Not drawn
  recently: alpha 0. The glist itself carries the billboard and transparency flags.
- `Swoosh_Create(start, end, lifetime, gfx, drift)`: life = `lifetime / FRAME_RATE_MUL` (halved in multiplayer
  when over 60), control flags |= 0x204. `Swoosh_Update`: life--, delete below 1; alpha = 128 x left/total; drift
  nudges both ends and the position by +0.001 in x and y; matrix along (end - start) facing the camera, scaled x
  by left/total and z by the length; radius = length.
- `cloud_update`: offsets += `FRAME_RATE_MUL x scroll`, modulo the wrap period; position = player 0 + offsets,
  y = player y + height. Drawn only as a sky object (`View_AddSkyObj` slot k4; `View_SetDrawInNoViews`).
- `Lightning_Create` / `_Update`: the newer of two same-group points becomes the master (six extra GFX objects,
  the older point turned into type 1 so it stops updating). While `SuperHappyLightningFlag`, each update (after
  `FUN_000b82e0` re-randomises when 0x29e81c is set) gives the seven segments and the partner glist 0x200032c,
  `PositionBeam` between consecutive points, parent = player 0; otherwise all eight are hidden.
- `Sunreflection_Update`: see the table. With no creator found (no store of 25 to `objectType`), it is probably
  dead; Bubble (6) likewise.

### 2.8 DynamicObject 0xa4090, Apocalypse 0xa4200

- `DynamicObject_Update`: with an appear switch, nothing happens until it is on, then the saved flags are restored
  every tick. Toggle mode 0: disappear switch on -> delete. Mode 1: on -> hidden (0x30), off -> restored. Fade
  distance f: within f of the camera, alpha = `max(d/f - 0.5, 0) x 2 x 255` - it fades *out* as you approach;
  beyond f the alpha is left as it was.
- `Apocalypse_Update`: the trigger channel latches *active*; then frames left -= `FRAME_RATE_MUL`, delete at 0;
  rumble `Input_RumbleStart(0, frames, rumble x frames/total)`; screen effect `glb_viewer[0]->apocalypseEffect =
  5 x left/total` (read by our `Camera.cpp`); flash-bang `Player_SetFlashBang(glb_blokes[0], left, min(382 x
  left/total, 255))`. Note the channel is read even when the key is 0.

### 2.9 `Gas_Init` 0xcd140 and `gases_smoke` 0xcd560

`obj_tag *Gas_Init(cel_tag *cel, _VECTOR *pos, _VECTOR *rot, _VECTOR *wind, float scale0, float scale1, HASHCODE
gfx, uint type, uint colour0, uint colour1, ushort life)` returns null for no cel or life 0. Size 0x54 for types 9
and 10 (which store the child parameters), else 0x3c. Sets `gasType`, `lifetime = life`, effectFlags |= 0x200,
scale = scale0, tint from colour0 (0xRRGGBBAA into 0xdf/0xe0/0xe1/0xe2), the four colour rates and the scale rate
(over `life`), a random rotation rate, `hashtable_set_object_to_entity_gfx`, effectFlags |= 0x20020120, links via
`build_LinkToRoom` or to `cel`. Wind: given -> `wind x REC_FRAME_RATE` (type 4 adds 0-0.05 noise to x and z);
none and type 7 -> the room wind (`Env_GetWindVel`) plus noise; types 9 and 10 -> the position is stored instead
(type 10 also halves its own lifetime) and effectFlags |= 0x10; otherwise (0, `REC_FRAME_RATE`, 0). The "unreachable
block" warnings in the decompile are the unsigned-to-float fix-ups of the colour bytes, harmless [dis].

Callers and what they pass (type, graphic): `Effect_Bullet` (EffectInfo slots), `Effect_Laser` (5 laserGas; 0
0x2000039), `Bullet_trail_effect_smoke` (7 and 11, 0x200001f), `Bullet_SpawnSmokeDust_*` (0 and 7, 0x2000039),
`Bullet_DoTrails` (11, flash grenade), `Player_MuzzleFlash`, `GunImp_Update`, `GT_Update` x3,
`AnimProcessScriptCmds` (7, 0x2000039 muzzle smoke), `Copter_Update` (3, 0x200001f), `NDrone2_BodyHitEffect` (5,
0x20008bc then 0x200026e; the first gets effectFlags 8), `Env_Breath` (0, 0x200021d), `Env_WaterRipple` and
`RB_Update` (1, 0x2000129), `gases_smoke` (children), our `Searchlight.cpp` (7, 0x2000039).

`gases_smoke`: data.md section 5.

### 2.10 `BodyGlow_Update` 0xce5e0

Created for the player, every drone and the anim debug object (unless `switch_NO_DRONES`): a main glow (glist
0x200025f, effectFlags 0x2408, scale 6) and 15 limb objects (glist 0x200055a, 0x2400), all hidden. Per tick, if
the owner has an anim object:

- Dead owner (drone data+0x90 <= 0, or player `BLData.health` <= 0): strength (0x100) -1 per tick; at 0
  `BodyGlow_Delete` and clear the owner's pointer (drone data+0x10, `BLData.bodyGlow`).
- For each of viewers 0-3 that is not the owner's own: bit set if the viewer is in vision mode 2, or if that
  player's HUD pane 0x11 (OICW in our `HUD_PANE_IND`) is enabled - the latter also forces "see through": distance
  0 and the limbs' 0x2400 cleared.
- If any bit and the owner is not a drone in `curState` 1: otherwise flags 0x2400 on and, beyond 10 units, hide
  everything. Alpha = `(1 - d/10) x 128 x strength/256`. The main glow goes to bone 5's position (`anim+0x98`
  matrices, +0x15c); each limb i is a beam from bone `0x18cbdc[i]` to its parent (skeleton table, bone 0's parent
  taken as 4), facing the camera, scaled (1.5, 1, length), control flags |= 0x224, drawn in the masked views plus
  0x10, re-linked to the owner's room every tick.
- No bits: hide all.

### 2.11 Debris, Leaf, Flicker, Particles

- `Debris_Create(hit, info, cel, n)` (0xcccf0): n type 16 objects at the hit, scale 0.4-0.5, alpha 255, life
  `rand(L/2) + L/2` (L = info+0x81), fade 1, velocity `(speed/2 x normal + randHalf(speed) x (hit+0x20)) x
  REC_FRAME_RATE` per component, spin `randHalf(info+0x7c)`, graphic info+0x74, effectFlags |= 0x20000020, linked
  by room or to `cel`. Returns at once if the graphic is 0.
- `Debris_CreateEx(pos, dir, side, speed, life, n, cel, gfxArray, gfxCount)` (0xcce60, Ghidra mistypes the vectors
  and speed): as above with speed factor `speed x 0.5 x (rand(1.5) + 0.1)`, spin 3pi/40, and the graphic
  `gfxArray[Rand_Rand(gfxCount - 1)]` - the last entry is never chosen (keep).
- `Debris_Update`: while life > 0 and alpha > fade: alpha -= fade, life--, `pos += vel`, gravity (`REC_FRAME_RATE`
  squared) unless `curState`, spin, control flag 0x20; else delete.
- `Leaf_Create(gen, n)`: drift along the generator's yaw (`rotation.y`), position random within its radius,
  life `256 / FRAME_RATE_MUL`; `Leaf_Update` adds a random fall and wander per tick.
- `Flicker_Update` returns when its room is not being drawn; `psiLight_Create` returns 0 on Xbox, so the light is
  never there, the tint goes to 255 and the dark branch (type 4 spark gas every other frame, `Particles_Create`)
  never runs. `Particles_Create` itself would do nothing: `psiCreateParticles` (`psiStubMinusOne`, ours) returns
  -1. Type 12 objects therefore never exist on Xbox.
