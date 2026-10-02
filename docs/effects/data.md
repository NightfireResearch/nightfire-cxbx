# Effects: structures, tables, globals and enums

Layouts are from the Xbox `default.xbe` (disassembly and decompiles of the functions named); names come from the
PS2 build (`/PS2_EU_51258/ACTION.ELF`) where it has them. Names in *italics* are invented here. Evidence: [dis]
disassembly, [dec] decompile, [tbl] bytes read from the XBE, [xref] cross references, [PS2] PS2 symbol or struct,
[inf] inference. See [README.md](README.md) for how the pieces fit together and [functions.md](functions.md) for
each function.

## 1. The hit record: `HITDATA_tag` (0x50 bytes)

Every effect that reacts to an impact starts from a hit record (a node of an object's hit list, `obj+0xac`, from
`Collide_Update`). Ghidra's type is usable; what the effects code reads:

| offset | type | field | effects use |
|---|---|---|---|
| 0x00 | ptr | prev, next | hit list links |
| 0x10 | `plane_equ_tag` | `normalPlane` | surface normal (x, y, z) at 0x10/0x14/0x18, d at 0x1c. `Effect_Create` negates it for material 0x10 (GLASS) and restores it afterwards, but only in single player [dec] |
| 0x20 | `_VECTOR` | `someOtherVectorUsedInBreak` | **written by `Effect_Create`**: the sum of the two tangents from `Vec_MakeOrthonormalBasis(normal)`. Debris and the laser smoke use it as the "sideways" spread [dec] |
| 0x38 | `_VECTOR` | `hitPosition` | where everything is spawned |
| 0x44 | byte | `materialAndFlags` | low 6 bits: material, the `EffectInfo` index (0-22 used). 0x40 and 0x80 are bullet pass-through flags read by `Bullet_CollisionHandler`, not by the effects |
| 0x46 | ushort | `hitBoneIdx` | - |
| 0x48 | `cel_tag*` | `hitCel` | set when the hit was on room geometry |
| 0x4c | `obj_tag*` | `hitObj` | set when the hit was on an object; its cel (`obj+0x20`) is used when `hitCel` is null |

## 2. `EffectInfo`: the per-material table (`HEINFO_tag`, 0x88 bytes x 23)

`EffectInfo` is at **0x15d798** (laserGas at 0x15d7a8, the address in `Effect_Laser`'s `MOV EDX,[EDX+0x15d7a8]`)
[dis]; the PS2 copy is at 0x003046a0. 23 rows, 0 `NONE` to 22 `LAST`; the strings that follow the table at
0x15e3d0 are the debug names. `Effect_RicochetProb` clamps the material to 0x16 [dec]; every other reader masks with
0x3f and trusts the data.

| offset | type | field | read by | meaning |
|---|---|---|---|---|
| 0x00 | `char*` | `debugName` | nobody | "CARPET", "METALTHIN", ... |
| 0x04 | HASHCODE | `bulletHole` | `Effect_Bullet` | decal glist; 0 = no decal |
| 0x08 | HASHCODE | `footprintLeft` | `Effect_Player` | decal glist for flag 4 |
| 0x0c | HASHCODE | `footprintRight` | `Effect_Player` | decal glist otherwise |
| 0x10 | HASHCODE | `laserGas` | `Effect_Laser` | gas graphic for the laser hot spot; 0 = no hot spot and no sparks. 0x2000215 ("lazerhotspot") on every hard material |
| 0x14 | ushort | `impactSound` | `Effect_Bullet` | `Sound_Play3D` id (0xffff = none) |
| 0x16 | ushort | `footstepSound` | `Effect_Player`, **`Effect_Laser`** | the laser plays the *footstep* sound (`Sound_PlayExt([EAX+0x15d7ae])`, non-positional) [dis]; both builds agree |
| 0x18 | ushort | `bodyhitSound` | `Effect_Body` | |
| 0x1a | short | `ricochet` | `Effect_RicochetProb` | 0 = never; else `Rand_Rand(ricochet) & mask` (metal 0x3f, stone 0x1f, ceramic 0x0f/0x18) |
| 0x1c | float | *`bounce`* | `Bullet_CollisionHandler` (0x2201e, 0x22130) | restitution of a ricochet: the reflected direction is `v - (1+e)(v.n)n` and the bullet's speed is multiplied by `e`. 0.5 for most materials, 0 for water and `NONE`/`LAST` [dec][tbl] |
| 0x20 | *`HESMOKE`*[3] | `smokeDustGfx`... | `Effect_Bullet` | three gas puffs per bullet impact, 0x1c bytes each (below) |
| 0x74 | HASHCODE | `debrisGraphics` | `Debris_Create` | 0 = no debris |
| 0x78 | float | *`debrisSpeed`* | `Debris_Create` | 6.2 for hard surfaces, 2.2 for loose ones |
| 0x7c | float | `debrisRandomAngle` | `Debris_Create` | spin range (radians per tick, centred) |
| 0x80 | byte | - | nobody found | always 0x01 when debris is present |
| 0x81 | byte | `debrisProbabilityRelated` | `Debris_Create` | *debris lifetime* in ticks: `life = rand(n/2) + n/2` (0x37 = 55; snow 0x23) |
| 0x82 | byte | `randomDebris` | `Effect_Create` | debris enabled |
| 0x83 | byte | `randomDebrisProbability` | `Effect_Create` | *debris count*: `rand(n) + 1` pieces |
| 0x84 | HASHCODE | `particleEmitter` | `Effect_Create` | emitter config (0x0c00xxxx, a level `block 0x30` record) |

*`HESMOKE`* (0x1c bytes, at 0x20, 0x3c, 0x58) - the argument list of a `Gas_Init` call [dec of `Effect_Bullet`]:

| offset | type | meaning |
|---|---|---|
| 0x00 | HASHCODE | graphic (0 = skip this slot) |
| 0x04 | uint | gas type (section 5) |
| 0x08 | uint | start colour, 0xRRGGBBAA |
| 0x0c | uint | end colour |
| 0x10 | float | start scale |
| 0x14 | float | end scale |
| 0x18 | ushort | lifetime (ticks) |
| 0x1a | byte | *wall only*: skip if `abs(normal.y) > 0.3`; spawn 0.05 off the surface, rotated by `Vec_NormToRot(normal)` |
| 0x1b | byte | *face the shooter*: rotation from the reversed normal (atan2s), else zero rotation; ignored when 0x1a is set |

The table as shipped [tbl, read through the Ghidra HTTP endpoint]. Sounds are `Action_SFX` ids; "smoke" lists the
non-empty slots as `gfx type life`:

| # | name | hole | footprints L/R | laserGas | snd impact/step/body | ricochet | smoke slots | debris gfx, speed, life, count | emitter |
|---|---|---|---|---|---|---|---|---|---|
| 0 | NONE | - | - | - | none | 0 | - | - (count 5) | - |
| 1 | CARPET | 2000262 | 2000567/568 | - | 11e/083/12a | 0 | 2000039 t9 32; 2000039 t10 42; 2000124 t5 4 | - | - |
| 2 | PAPER | 2000262 | 2000567/568 | 2000215 | 11f/083/12b | 0 | 2000124 t5 4 | - | - |
| 3 | WOODTHIN | 2000128 | 2000567/568 | 2000215 | 00b/00e/12c | 0 | 2000124 t5 4 | 200012b, 6.2, 55, 1-5 | - |
| 4 | WOODTHICK | 2000128 | 2000567/568 | 2000215 | 00c/00f/12d | 0 | 2000124 t5 4 | 200012b, 6.2, 55, 1-5 | - |
| 5 | METALTHIN | 2000127 | 2000567/568 | 2000215 | 006/010/12e | 0x3f | 2000124 t5 4 | 20001ff, 6.2, 55, 1-5 | c00000b |
| 6 | METALTHICK | 2000127 | 2000567/568 | 2000215 | 007/011/12f | 0x3f | 2000124 t5 4 | 20001ff, 6.2, 55, 1-5 | c00000b |
| 7 | GRASS | - | 2000567/568 | - | 120/081/130 | 0 | - | 200012a, 2.2, 55, 1-5 | - |
| 8 | EARTH | 2000262 | 2000214/353 | - | 121/-/131 | 0 | - | - | c000003 |
| 9 | SAND | 2000262 | 2000214/353 | - | 122/-/132 | 0 | - | 2000200, 2.2, 55, 1-5 | c00000c |
| 10 | STONE | 2000126 | 2000567/568 | 2000215 | 008/00d/133 | 0x1f | t9 32; t10 42; t5 4 | 200012c, 6.2, 55, 1-5 | c00000f |
| 11 | GRAVEL | 2000262 | 2000567/568 | - | 008/082/134 | 0 | 2000124 t5 4 | 20001fe, 2.2, 55, 1-5 | c000008 |
| 12 | SNOW | - | 2000214/353 | - | 124/07b/135 | 0 | 2000039 t10 15 (white) | 2000220, 6.2, 35, 1-10 | c00000d |
| 13 | DEEPWATER | - | - | - | 00a/-/136 | 0 | 2000129 t1 60 | 2000220, 6.2, 35, 1-10 | c000000 |
| 14 | SHALWATER | - | - | - | 009/028/137 | 0 | 2000129 t1 60 | 2000220, 6.2, 35, 1-10 | c000000 |
| 15 | ICE | 20006fd | 2000567/568 | 2000215 | 0ed/082/138 | 0 | - | 200012d, 2.2, 55, 1-5 | c000009 |
| 16 | GLASS | 2000122 | 2000567/568 | 2000215 | 005/084/139 | 0 | 2000124 t5 4 | 200012d, 6.2, 55, 1-5 (spin pi) | - |
| 17 | CERAMICTHIN | 2000126 | 2000567/568 | 2000215 | 125/084/13a | 0x0f | t9 32; t10 42; t5 4 | 200012c, 6.2, 55, 1-5 | c000002 |
| 18 | CERAMICTHICK | 2000126 | 2000567/568 | 2000215 | 126/084/13b | 0x18 | t9 32; t10 42; t5 4 | 200012c, 6.2, 55, 1-5 (spin pi) | c000002 |
| 19 | FOLLAGE | - | - | - | 127/081/13c | 0 | - | 200012a, 2.2, 55, 1-5 | - |
| 20 | FABRIC | 2000262 | 2000567/568 | - | 128/083/13d | 0 | t9 32; t10 42; t5 4 | - | - |
| 21 | GENERIC | 2000262 | 2000567/568 | 2000215 | 129/-/13e | 0 | 2000124 t5 4 | - | - |
| 22 | LAST | - | - | - | none | 0 | - | - | - |

The t9/t10/t5 slots of rows 1, 10, 17, 18 and 20 are the same three records: dust 0x2000039 type 9 (0x7f7f7f7f,
scale 0.005-0.02, wall only), smoke 0x2000039 type 10 (0x5050507f, 0.025-0.15 or 0.03, wall only except carpet and
fabric) and the flash 0x2000124 type 5 (0x7f7f007f to 0x7f7f0040, 1.5-2.5, face the shooter). The emitter configs
(0x0c000000-0x0c000010) live in each level's data, not in the XBE, so their contents differ per level.

A dump script is easy to recreate: read 23 x 0x88 bytes at 0x15d798 from
`http://127.0.0.1:8089/read_memory?address=...&length=...&program=/Xbox_EU/default.xbe` and unpack as above.

## 3. Decals: the `EffectList` node (0x58 bytes, *`EFFECTNODE`*)

`EffectList` is a `DLISTINFO_tag` at **0x2237c0** (free list +0, active list +0xc, so `activeList.head` is
0x2237cc), 200 nodes of 0x58 bytes, set up by `Effect_InitLists` at every level start [dec]. Ghidra calls the node
type `Effect_tag` on the PS2 side; the Xbox header `Effect.h` uses that name for the *object* data of type 16
instead (section 4.1), so this document calls the node *`EFFECTNODE`*.

| offset | type | field | notes |
|---|---|---|---|
| 0x00 | `LLNODE_tag` | prev, next | `EffectList` membership (free or active) |
| 0x08 | node* | *ownerNext* | next decal on the same owner; the owner's head is `cel+0x10` or `obj+0x10` |
| 0x0c | node* | *ownerPrev* | |
| 0x10 | `celglist_tag*` | *gfx* | set by `hashtable_set_sub_object_to_entity_gfx` 0x6b6d0 (PS2 name); left unchanged when the hash does not resolve, so a recycled node can keep its old graphic [dec] |
| 0x14 | `obj_tag*` | *ownerObj* | non-null: the matrix below is in this object's local space |
| 0x18 | `cel_tag*` | *ownerCel* | |
| 0x1c | float[15] | *matrix* | 4x4 without the last float; translation at 0x4c-0x57 |

Drawn by `View_maybeDrawNightVisionExtras` 0xda1d0 (Ghidra's name; it is the decal drawer, *View_DrawSObjs*), called
at the end of `View_DrawCelGlist` and `View_DrawObjectGlist` with the owner's `+0x10` head: for each node with a
graphic, skip it if the graphic's `applyFlagsToObject & 0x400` unless the viewer is in vision mode 2, then
`psiDrawObjectMatrix(gfx, matrix)`, concatenated with `ownerObj->mtx` when `ownerObj` is set [dec]. Freed by
`Effect_RemoveSObjs(owner)` (owner deletion, `Destroy_Smash`, `FuseBox_Update`) and by recycling: when the free
list is empty the active list's head (the oldest) is unlinked and reused.

## 4. Per-object extra data (the block after the 0xe4-byte `obj_tag`)

The `obj_tag` fields the effects touch: `position` 0x24, `rotation` 0x3c (z is the billboard spin), `centrePoint`
0x60, `radius` 0x6c, `mtx` 0x70, `objGraphics` 0xb4, `extraObjectData` 0xbc, `scale` 0xc4, `creationTimeFrames`
0xc8, `effectFlags` 0xcc, `curState` 0xd0, control flags 0xd6, *visible countdown* 0xd8 (below), `flags` 0xda,
`objectType` 0xdb, tint `tweakR/G/B` 0xdf/0xe0/0xe1, `maybeBrightness` (alpha) 0xe2.

**`obj+0xd8` is a "drawn recently" countdown**: `View_AddObjects` 0xda5c9 (and `FUN_000d91d0` 0xd9207) store 2
when an object is put on a draw list, and the end of `control_movement_object_handler` decrements it [dis, search
for writes to `+0xd8`]. `Corona_Update` reads it as "was visible in the last two ticks". This settles item 3 of
`docs/architecture/frame-and-objects.md` section 4.

### 4.1 Effect / debris / spark, type 16 (0x20 bytes; `Effect_tag` in `src/action/game/obj/Effect.h`)

| offset | type | field | notes |
|---|---|---|---|
| 0x00 | - | unused | |
| 0x04 | `_VECTOR` | *velocity* | units per tick (creators multiply by `REC_FRAME_RATE`) |
| 0x10 | `_VECTOR` | *spin* | added to `rotation` each tick |
| 0x1c | short | *life* (`someThing`) | ticks left |
| 0x1e | byte | *fade* (`someOther`) | subtracted from `maybeBrightness` each tick |

`curState` = 0 means gravity applies (`Debris_Update`). Creators: `Debris_Create`, `Debris_CreateEx`,
`Effect_Laser` (sparks), `Bullet_DoTrails` (x2), `GunImp_Update`, `NDrone2_CreateNinjaEyes`,
`DroneWeap_FireWeapon`, `HUD_UpdateCameraPane` [dis, stores of 0x10 to `+0xdb`], and our `SpaceLaser.cpp`.

### 4.2 Gas, type 14 (`ObjData_Gas`: 0x3c bytes, 0x54 for gas types 9 and 10)

| offset | type | field | notes |
|---|---|---|---|
| 0x00 | `_VECTOR` | `windRate` | drift per tick; for types 9 and 10 the *spawn position* instead |
| 0x0c | float[4] | `colourChangeRateR/G/B/A` | (end - start) / lifetime |
| 0x1c | float[4] | `someColourR/G/B`, `someBrightnessOrAlpha` | current colour, clamped to 0-255 into the tint bytes each tick |
| 0x2c | float | `rotationRate` | random up to `REC_FRAME_RATE * pi/2`, either sign; type 11 a quarter of that |
| 0x30 | float | *scaleRate* (`field10_0x30`) | (end scale - start scale) / lifetime |
| 0x34 | float | `lifetime` | ticks left, counted down by `FRAME_RATE_MUL`; deleted at 0 |
| 0x38 | byte | `gasType` | section 5 |
| 0x3c | uint | `colourStart` | types 9, 10 only: what the children get |
| 0x40 | uint | `colourEnd` | |
| 0x44 | HASHCODE | `gfxHashcode` | |
| 0x48 | float | *childScaleStart* | |
| 0x4c | float | *childScaleEnd* | |
| 0x50 | float | `fLifetime` | children's lifetime |

`Flicker_Update` and `Ripples_Update` build gas objects by hand (0x3c bytes, types 4 and 1) instead of calling
`Gas_Init`; a reimplementation should route them through one helper only if it reproduces the exact field values.

### 4.3 Emitter, type 50 (`ObjData_Emitter`: allocated 0x128 + 0x2c x maxParticles)

| offset | type | field | notes |
|---|---|---|---|
| 0x00 | ptr | *velocities* | n x 0x10 (`_VECTOR` + pad), at +0x4c |
| 0x04 | ptr | *positions* | n x 0x10; handed straight to `psiDrawParticleList` (stride 16) |
| 0x08 | ptr | *colours* | n x 4, D3DCOLOR (bytes B, G, R, A) |
| 0x0c | ptr | *ages* | n x float seconds; 99999.0 = never spawned |
| 0x10 | ptr | *lifetimes* | n x float seconds |
| 0x14 | `ParticleEmitterConfig*` | config | the `block 0x30` record (4.4) |
| 0x1c | `_VECTOR` | *spin* | glist particles only: advanced by `REC_FRAME_RATE` per particle drawn, in `Emitter_Draw` |
| 0x28 | uint | *enableSwitch* | channel; `enabled = switch_channels[c]` each tick |
| 0x2c | uint | *disableSwitch* | `enabled = !switch_channels[c]` (applied after, so it wins) |
| 0x30 | uint | *aliveSwitchOut* | cleared each tick, set (with `switch_channels_time`) while any particle lives |
| 0x34 | float | *duration* | seconds left; -1 forever; 0 at creation means a single burst |
| 0x38 | float | *pulsePeriod* | seconds; on for half a period, off for a whole one |
| 0x3c | float | *pulseTimer* | |
| 0x40 | byte | *pulseOn* | starts 1 |
| 0x41 | byte | *finishing* | 0; counts 1, 2, 3 once the emitter is spent, then the object is deleted |
| 0x42 | byte | *enabled* | starts 1 |
| 0x43 | byte | *expired* | set when the duration runs out (or 0 at creation): only never-spawned slots may spawn |
| 0x44 | byte | *allDead* | `Emitter_Draw` draws nothing when set |
| 0x4c | - | arrays | `0x4c + 0x2c n` bytes are used; the remaining 0xdc of the 0x128 header are never touched [dec] |

### 4.4 `ParticleEmitterConfig` (0x40 bytes + key frames; built by `parsemap_block_particles`)

Offsets verified by following the stack writes in the disassembly of 0x68020 (the decompile's locals are
misleading) [dis]. Level data per record: a dword whose low byte must be 4 and whose upper 24 bits go to 0x3c, the
config's own hash, texture hash, glist hash, two words, ten floats, the key count, then `count` keys of 5 floats.

| offset | type | field |
|---|---|---|
| 0x00 | ptr | *keys* (points at 0x40) |
| 0x04 | int | *texture* (first dword of the texture's hash item; 0 when the hash is -1) |
| 0x08 | `celglist_tag*` | *glist* (null: point sprites) |
| 0x0c, 0x10 | float | *lifetime* base, random range (seconds) |
| 0x14 | float | *gravity* (multiplied by 9.8) |
| 0x18, 0x1c | float | *speed* base, random range |
| 0x20, 0x24 | float | *theta* base, range: angle from the emitter's up axis |
| 0x28, 0x2c | float | *phi* base, range: azimuth |
| 0x30 | float | *size* (`psiDrawParticleList` draws `size x 100 x zoom`) |
| 0x34 | uint | *keyCount* |
| 0x38 | ushort | `numberOfThings` = *maxParticles* |
| 0x3a | ushort | *perTick*: spawn budget per tick |
| 0x3c | ushort | *mode* (type dword >> 8): `psiDrawParticleList`'s blend argument is `mode == 0` |
| 0x40 | key[] | 0x14 each: R, G, B (stored halved, 0x80 = full), A, *time* (normalised age, ascending) |

The colour at a given age comes from `FUN_00067ea0` (*Emitter_KeyColour*, filed under ai.drones by address but
called only by `Emitter_Update`): find the last key with time below `t + 0.001`, interpolate linearly to the next,
results through the globals 0x1f6448-0x1f6454 into 0x1f6458-0x1f6464 [dec].

### 4.5 The small ones

| type | bytes | layout |
|---|---|---|
| Swoosh 65 | 0x20 | 0x00 start `_VECTOR`, 0x0c end, 0x18 short life left, 0x1a short life total (both `lifetime / FRAME_RATE_MUL`; halved in multiplayer when over 60), 0x1c byte *drift* |
| Corona 69 | 0 | `curState` = which frame parity it updates on (random) |
| Cloud 36 | 0x18 | 0x00 scrollX/1000, 0x04 scrollZ/1000 (floats), 0x08 offset x, 0x0c offset z, 0x10 height above player (int key), 0x14 wrap period (int key) |
| Lightning 51 | 0x40 | 0x00 self when paired else 0, 0x04-0x18 six segment GFX objects, 0x1c partner, 0x20 start, 0x2c end, 0x38 k0, 0x3c short group, 0x3e short k2 (`LIGHTNING` in Ghidra) |
| RainBox (type 1) | 0x1c | 0x00-0x14 clipped AABB min x,y,z max x,y,z, 0x18 byte active. Chained through `obj+0x1c` (the parent field) from `RainBoxRoot` |
| DynamicObject 82 | 0xc | 0x00 short appearSwitch, 0x02 short disappearSwitch, 0x04 saved `effectFlags`, 0x08 short fadeDistance, 0x0a byte toggleMode |
| Apocalypse 84 | 0x10 | 0x00 frames left (seconds x 60), 0x04 total, 0x08 short trigger channel, 0x0a short rumble, 0x0c byte screen effect, 0x0d byte flash-bang, 0x0e byte active |
| Flicker 38 | 0x18 | 0x00 `psiLight*` (always 0 on Xbox), 0x04 short smokeEnable, 0x08 up `_VECTOR`, 0x14 darkSwitchOut |
| Ripples 52 | 4 | 0x00 short radius, 0x02 short mean interval (1/60 s; both 0 -> 1 and 30) |
| Leaf 46 | 0x20 | as type 16: 0x04 velocity, 0x10 spin, 0x1c life (`256 / FRAME_RATE_MUL`), 0x1e fade (0) |
| LeafGen 49, Particles 12 | 0 | Particles' data pointer is the (PS2) particle system handle |
| Bubble 6 | >= 0x20 | 0x04 velocity, 0x1c short life |
| Sunreflection 25 | 0xc | offset from the camera |
| BodyGlow 70 | 0x44 | 0x00 owner object, 0x04-0x3c fifteen limb GFX objects, 0x40 short strength (starts 0x100) |

## 5. Gas types (`ObjData_Gas.gasType`, `gases_smoke` 0xcd560)

No enum exists in either build; these are descriptions of the code [dec], with the callers that use each.

| type | per-tick behaviour | users |
|---|---|---|
| 0 | wind damped by `0.95 x FRAME_RATE_DIV`, then as 7 | dust from `Bullet_SpawnSmokeDust_*`, `Env_Breath`, `Effect_Laser`'s smoke wisp |
| 1 | static; grows | water rings (`Env_WaterRipple`, `Ripples_Update`, `RB_Update`, water `EffectInfo` rows) |
| 2 | static; does not grow | none found |
| 3, 6 | spin, rise (gravity x -0.1), drift; at the room's ceiling clamp and stop growing that tick | `Copter_Update` (3) |
| 4 | falls under gravity (x 0.01) with x/z damping; grows | `Flicker_Update` sparks (hand-built) |
| 5 | static; grows | impact flash 0x2000124, laser hot spot "lazerhotspot", drone body hits 0x20008bc/0x200026e |
| 7 | spin, slow rise (gravity x -0.02), drift with the room wind (`Env_GetWindVel` + noise) when no wind is given; ceiling clamp; grows | muzzle smoke 0x2000039 (`Player_MuzzleFlash`, `GT_Update`, `GunImp_Update`, `AnimProcessScriptCmds`, our `Searchlight.cpp`), bullet trails, children of 10 |
| 8 | grows, sinks (gravity x 0.05) | children of 9 |
| 9 | spawner: after `20 x FRAME_RATE_MUL` frames of age, on frames where `NumFramesUnpaused & 3 == 3`, spawns a type 8 child at its stored position | bullet impact dust |
| 10 | spawner: own lifetime halved; spawns a type 7 child rising at `2 x REC_FRAME_RATE` whenever `NumFramesUnpaused & m == m`, `m = (int)(7 x FRAME_RATE_MUL)` | bullet impact smoke |
| 11 | as 7 but grows, and the alpha follows `lifetime x 8.5` for the last 30 ticks | `Bullet_trail_effect_smoke`, flash-grenade cloud in `Bullet_DoTrails` |

Every type then steps the colour, writes the clamped tint bytes, subtracts `FRAME_RATE_MUL` from `lifetime`, and
either sets control flag 0x20 (moved) or marks the object for deletion. Types 9 and 10 also force effectFlags 0x10
(hidden): spawners are never drawn.

## 6. `Effect_Create` flags (third argument; invented names)

| bit | *name* | effect |
|---|---|---|
| 0x01 | *EF_BULLET* | `Effect_Bullet`: sound, smoke slots, bullet-hole decal |
| 0x02 | *EF_STEP* | `Effect_Player`: footstep sound and footprint decal |
| 0x04 | *EF_LEFT_FOOT* | footprint from `footprintLeft`, matrix of bone 0x37 |
| 0x08 | *EF_RIGHT_FOOT* | `footprintRight`, bone 0x33 (without 4 or 8: no footprint) |
| 0x10 | *EF_LASER* | `Effect_Laser` |
| 0x20 | *EF_EMITTER* | `particleEmitter` and debris; **the only part that also runs in multiplayer** |
| 0x40 | *EF_SOUND* | play the sound of whichever handler runs |
| 0x80 | *EF_BODY* | `Effect_Body` (body-hit sound), only when none of 0x01/0x02/0x10 |

Precedence is 0x01, 0x02, 0x10, 0x80: one handler per call. Callers pass: bullets 0x61 when the weapon's
`impactFlags & 2`, overridden to 0x10 for the laser (projectile flag 0x100) or impact flag 0x1000, else 0
(nothing happens) - and the variable is not reset between hits of one bullet, so earlier hits' bits stay set;
footsteps 0x42 | (left ? 4 : 8) (`AnimObjectUpdate`, `Script_Interp`); anim script command 6: 0xc0
(`AnimProcessScriptCmds`).

## 7. `effectFlags` bits as the effects use them

From the drawing and lighting code (`View_AddObjects`, `View_DrawObjectGlist`, `View_SetupRenderModes` 0xd9900,
`Lights_CalcClosestLights` 0x6d8f0) and `docs/architecture/rendering.md` [dec]:

| bit | meaning | set by (effects) |
|---|---|---|
| 0x00000001 | transparent: alpha list 1, sorted far to near | emitters (0x21) |
| 0x00000004 | jitter (random scale 0-0.05 each draw) | from glists |
| 0x00000008 | billboard: camera-facing, spun by `rotation.z`, scaled by `scale` | BodyGlow main object (0x2408); spark glists (inf) |
| 0x00000010 | not drawn | gas spawners, hidden DynamicObjects (0x30), out-of-range BodyGlow |
| 0x00000020 | not collidable / disabled | almost every effect |
| 0x00000080 | lit by kind-0 lights and the "flashing" pulse | - |
| 0x00000100 | ignore kind-1 lights | gas (0x20020120) |
| 0x00000200 | unknown on Xbox (no reader found); set by `Gas_Init` and the hand-built gas | gas |
| 0x00000400 | drawn only in vision mode 2 (decals: graphic flag 0x400) | BodyGlow (0x2400) |
| 0x00002000 | list 2: drawn after the optional Z clear, i.e. over the world | Corona while in line of sight; BodyGlow |
| 0x00008000 | always considered (forced pass) | Lightning |
| 0x00020000 | quartered brightness in night vision (mode 1) | gas, debris (0x20020120 / 0x20000020) |
| 0x20000000 | always treated as straddling rooms | gas, debris, leaves |

`Control_SetGList` keeps only 0x30008000 of the old flags and ORs in the glist's own (`applyFlagsToObject`), so
flags set before `hashtable_set_object_to_entity_gfx` are lost; every creator ORs its flags in afterwards.

## 8. Globals

| address | name | notes |
|---|---|---|
| 0x15d798 | `EffectInfo` | 23 x 0x88, read-only |
| 0x2237c0 | `EffectList` | `DLISTINFO_tag`, decal pool |
| 0x163b98 | `control_funcs` | rows used: 6 Bubble, 12 Particles, 14 Gas, 16 Effect, 25 Sunreflection, 36 Cloud, 38 Flicker, 46 Leaf, 49 LeafGen, 50 Emitter (delete = hook 0xe0ec0), 51 Lightning, 52 Ripples, 65 Swoosh, 69 Corona, 70 BodyGlow, 82 DynamicObject, 84 Apocalypse [xref: "data at" addresses] |
| 0x1f64a8 | `i_135` | `Emitter_Draw`'s loop counter, a static |
| 0x1f646c | - | `Emitter_Draw`'s scratch matrix |
| 0x1f6448-0x1f6464 | - | key-frame interpolation scratch (indices, weights, colour out) |
| 0x2637e0 | `DropType` | byte; -1 (0xff) = no weather |
| 0x2637e4 | `WindVector` | drift per tick |
| 0x2637f0 | `PrevPos` | set to -999, never read here |
| 0x2637fc | - | wind scaled by `FRAME_RATE_MUL` this tick |
| 0x263808 | - | *drop centre*: viewer position (+7.5 ahead of player 0's yaw for viewer 0) |
| 0x263818 | `RandTable2` | 32 velocities, scaled per tick |
| 0x263998 | `DropState` | 0x1000 bytes, 1 = live |
| 0x264998 | `RandTable1` | 32 base velocities (Ghidra's label sits on +4) |
| 0x264b18 | `InitBodge` | fill the volume on the first tick (Castle exterior to courtyard) |
| 0x264b1c | `CurrentMax` | short: drops in use |
| 0x264b20 | `RainBoxRoot` | first rain box object |
| 0x264b28 | `DropVector` | 0x1000 x 0x10: position, w = floor (rain) or ceiling (bubbles) |
| 0x274b28 | `Added` | short: drops added this tick (cap 21); 999 = do not draw |
| 0x29e81a | `SuperHappyLightningFlag` | a sky flash is on (set outside this subsystem) |
| 0x29e81c | - | re-randomise the bolt this tick |
| 0x18cbdc | - | 15 bone numbers for the BodyGlow limbs |
| 0x18cdf8, 0x18cdfc | RNG state | `Rand_Random`'s two words; save/restore around shadow tests |

Frame-rate globals used throughout: `FRAME_RATE_INT` 0x17c0f4, `FRAME_RATE_DIV` 0x17c0fc, `FRAME_RATE_MUL` 0x17c100
(60 / fps), `REC_FRAME_RATE` 0x17c104 (seconds per tick); `GRAVITY_VECTOR` 0x1f6648; `GameState.NumFramesUnpaused`
0x1f65b4; `MPSettings.isMultiplayer` 0x260018; `glb_viewer` 0x1f661c; `glb_players` 0x1f6654; `glb_blokes`
0x1f6674; `switch_NO_DRONES` 0x1df99c (BodyGlow does nothing while it is set).
