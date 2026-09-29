# Level objects: actors (drones, AI data, starts, vehicles and weapons)

Scope: placement types 15 Drone, 229 DroneCoverCorner, 230 DroneCoverLow, 233 DroneAIPoint, 245/248 DroneAIVolume1/2,
241 DroneSpawner, 36 PlayerNewStartPos, 45 AddNewStartPos2, 37 MPSpawnPoint, 38 MPObject, 218 Car, 47 Copter,
39 GunTurret, 52 GunImp, 222 Searchlight, 254 Mine, 57 SpaceMissile, 56 SpaceLaser, 74 Apocalypse, 53 PCQWorm,
210 Shooter, 58 Grapple, 224 Creature, 54 Sub, 55 MiniSub.

Machine-readable version: `tools/level/types_actors.json`. It has every key with its offset, meaning, and the value
ranges seen in all 32 bundles (`seen`), plus `weapon_gfx` (weapon variant -> third-person model).

Method: read-only Ghidra on the Xbox `default.xbe`. Names come from the PS2 `ACTION.ELF`, including the drone mode
table. Every placement record of every distinct pack in `build/anim/lvl/07*.bin` was decoded to check the fields.
Hashcodes were resolved against bundle entries and pack entities. Offsets are Xbox.
"key k" = the level_tag dword at 0x2C + 4k.

## 0. Things that apply to every type here

**Rotation.** Placement `rot` (+0x18) is Euler radians. Objects build their matrix with `View_RotTransMatrix(rot,
pos)` 0xd92e0 (through `Control_GetObjMatrix`). MP objects use `RotMatrix` 0xd6560 instead, and static cels use
`RotMatrixZYX` 0xd6710. The matrices are row-vector (`m[12..14]` = translation, rows = basis vectors).

For a yaw-only rotation (all three builders agree), model +z maps to (sin y, 0, cos y). That is the ordinary
right-handed R_y(y), so the glTF node rotation is `(0, sin(y/2), 0, cos(y/2))`. Characters face +z, so a drone
with rot.y = y faces (sin y, 0, cos y). The AI uses the same convention: it measures bearings with `atan2(dx, dz)`
against rot.y.

**The placement quat (+0x24) is the inverse of the rot rotation.** For yaw-only records it is always
`(0, -sin(y/2), 0, cos(y/2))` (3,193 records confirm this). `Quat_QuatToMat` 0xd5800 has glTF semantics, so using
the quat directly would mirror every yaw.

For records with a general rotation, the inverse of the quat does not consistently match any one of the three Euler
builders. So use rot with the builder the create function uses. Of my types, only GunImp uses the quat, and its
yaws are 0 or pi, where the two agree. In AI volumes the quat is not even a unit quaternion in 109 records.

**Glists here are mostly editor markers.** The glist check in parsemap still applies: the object is only created
if the glist resolves. Most AI and logic types carry a marker glist ("oDrone", "Cover Corner Node", "AI_Point",
"MPStart", "PlayerStart"...) from the shared editor pack `010000f3` / `01000166`, which is in every bundle. Drones
never draw theirs.

**Hashcode families seen in keys.**
- 0x05 = skin (bundle entry type 3).
- 0x06 with bundle entry type **5** = animation script (docs/anim/spec-script.md): drone keys 1 and 13.
- 0x06 with bundle entry type **7** = **object script**. This is a different format, loaded by `Script_Load`
  0xc1550: a first word of 0x1c, then up to 62 elements, each shown with a glist and placed by
  `View_RotTransMatrix(rot,pos)`. GunTurret, Copter, SpaceMissile, SpaceLaser, MPObject and Mine use these. The
  object-script format is not decoded here (see the ScriptPlayer/SS group).
- 0x02 = entity (weapon models, GunImp barrels).

## 1. Drone (15) - the characters

`Drone_Create` 0x2fcf0, cdecl `(pos*, rot*, level_tag*)`. parsemap calls `Drone_Create(&pos, &rot, lvl)` and does
**not** pass the glist.

- It returns NULL if `Drone_bDisableSystem` is set, or if key2 > `GameState.difficultyModifier`.
- It copies pos, rot and 0x84 bytes from lvl+0x2C (keys 0-32) into a `DIVars_tag`: 0xa4 bytes, with the keys at
  +0x20.
- `NDrone2_CreateFromDIVars` 0x2fc50 -> `NDrone2_CreateObj` 0x3f940 creates the object: `Drone_tag` is 0x978 bytes,
  and DIVars sits at +0x874, so key k is at Drone+0x894+4k.
- The keys are read on the first frame by `NDrone2_PostLoad_Init` 0x42a90 -> `NDrone2_DefaultInit` 0x417f0, which
  calls `AnimObjectNew(obj, key0)` and then `DoModeSettingsOLD` 0x414d0 or `DoModeSettingsNEW` 0x41230.

| key | name | meaning (confidence) | seen |
|---|---|---|---|
| 0 | **skin** | the character's 0x05 skin (confirmed). 0x05000000 = template record in the shared pack: skip. EvilBase (0x07000014) remaps 0x050000b1 -> 0x0500008c, and indeed 0x050000b1 is not in that bundle | 47 skins; all others resolve in their own bundle |
| 1 | playScript | 0x06 anim script (entry type 5), 0x06000000 = none. The drone starts in DSTATE_PlayScript playing it (after key3, if set). 0x06000892/0x0600089c are special | 15 records set |
| 2 | minDifficulty | present only if <= difficulty | 1 x474, 2 x64, 3 x18 |
| 3 | waitSwitchChannel | non-zero: starts hidden and inactive (`NDrone2_Enable(0)`: effectFlags \|= 0x30) in DSTATE_WaitSwitch until the channel is on. Only checked if the channel is off at init | non-zero in 466/556 (many channels are probably set by start triggers: do not hide by default, tag it) |
| 4 | group | drone group (`NDrone2_SetupGroups`); a DroneSpawner with key1 == group takes these drones over; also matched against AI points | 0-31 |
| 5 | mode | <0x24 = fixed mode (table 0x1776f8, names in the JSON: 0 Normal, 1 Guard, 3 Sniper, 9 Hostage, 13 Civilian, ...); >=0x24 = behaviour block in keys 15-32 | 100 in 555/556 |
| 6 | modeChangeSwitchChannel | when on: key8 == 12 deletes the drone, else it goes to attack mode | |
| 7 | ? | Drone+0x12c, no reader found (guess) | mostly 0 |
| 8 | secondaryMode | mode-table index (0/101 none) | 100 x553 |
| 9 | **weapon** | held weapon: `weapon_data` variant -> `animState.currentWeaponId`. 1 = unarmed, 0 = skin default | 1,18 Smg,25 Rifle,10 Glock,26 Hkoicw,30 Sniper,20 Mp5ks,28 Spas12,22 Sg5,106 Beamlaser,... |
| 10 | drawWeapon | weapon it draws when alerted (<=1 none: civilian behaviour) | 1,10,14,16 |
| 11 | grenades | hi nibble = kind, lo = count | 0x43 = 3 of weapon 0x34 |
| 12 | deathSwitchChannel | set on death; 99 = mission fail (civilian) | |
| 13 | deathScript | 0x06 anim script for a special death/surrender (0x0600021f special) | 6 set |
| 14 | combatRange | engage-distance class 0-5 | |
| 15-25 | behaviour block | 15 class/stats (<0x11), 16 mode 1, 17 = 0x005b0003 header, 18-20 = 91 property bits; 21 mode 2, 22 header, 23-25 bits (`behaviour_util_get` 0x194d0) (likely) | |
| 26-32 | stats override | 0x80010006 + 6 words of bit-packed per-class stats, written to the global table 0x1d7830 (41 records) | |

**How to export a drone.**
- **Model:** `tools/anim/export_gltf.py <key0> --bundle <bundle>`, then place that glb.
- **Weapon:** attach `weapon_gfx[key9].gfx` (see the JSON) at the skin's datum `weapon_data[key9].whichBone`. This
  is exactly how `AnimObjectDraw` does it (docs/anim/spec-pose.md; the glb already has `datum_<id>` empties).
- **Position:** the node goes at `pos` **with no height offset**. Drone placements sit about 1.1 m above the floor:
  151 of 201 drones near a cover or AI node sit 1.1 m above it. The exported skin's bind-pose feet are at
  y = -1.16, so the object origin is hip height and the feet land on the floor. The in-game root height adds the
  animation's root translation plus `heightOffset` (spec-pose section 2.1).
- **Rotation:** yaw = rot.y. Only rot.y is ever non-zero for drones. Use `(0, sin(y/2), 0, cos(y/2))`, not the
  placement quat.
- **Starting animation:** use key1 if it is set; it is an anim script in the same bundle. Otherwise the drone plays
  anim state 0x11a (the idle stand; 0x1a5 or 0x82 for two special classes). These are anim-state ids, not
  hashcodes; resolving them needs the drone anim-set tables (not done).
- **Visibility tags worth exporting:** minDifficulty (key2), waitSwitch (key3), and spawner-owned (key4 matches a
  DroneSpawner key1). Spawner-owned drones are deleted at start and later respawn at their own placement.

## 2. AI data (no geometry)

- **DroneCoverCorner 229**: `Drone_CoverCornerNode` 0x2fe20 `(pos*, rot*, lvl*)`. It appends a 0x78-byte
  `CoverNode_tag` (type 0).
  - k0-k2 = side/stand/crouch usage flags (1, 2, other -> both).
  - k3 = cover angle in degrees (0 -> 45; 46 everywhere).
  - k4 = enable switch channel, k5 = disable switch channel (`Drone_IsCoverNodeUsable`).
  - k6, k7 = bytes (0 -> 8).
  - rot.y = the facing of the cover.
- **DroneCoverLow 230**: `Drone_CoverLowNode` 0x30000, CoverNode type 1.
  - k0 = kind (2: k1/k2 index the anim table {0x11,5,10,7,0x1d}; 1 / other: fixed anims).
  - k3 == 1 sets flag 2.
  - k4/k5 = enable/disable switch channels.
- **DroneAIPoint 233**: `Drone_AIPoint` 0x30190. It appends a 0x5c-byte point.
  - k0 = point id (u16) that drones look up.
  - k1 = radius (float).
  - k2 = group (preferred by drones of that group).
- **DroneAIVolume1 245 / DroneAIVolume2 248**: `Drone_AIVolume_Create` 0x30290, called as
  `(pos*, rot*, &placement+0x34, lvl*, celglist)`.
  - **It is an oriented box.** Centre = pos, orientation = `View_RotTransMatrix(rot)`, half extents =
    |glist bbox size| / 2 x **v34** (+0x34 is a per-axis scale here). The glists (`AI_ShadowVolume` 0x0200041a,
    `AI_SoundVolume` 0x0200063e) are 3 x 3 x 3 boxes with y from 0 to 3, but the box is centred on the object
    origin.
  - It is never drawn (effectFlags \|= 0x30).
  - k0 = visibility percent (50 = targets inside are half as visible).
  - k1 = volume type (0 visibility, 1 -> 2, 2 -> 1, 4 -> 3). Type 4 flips rot.z and raises the box by 1.5 x
    scale.y.
  - k2-k4 are stored but their use is unknown; k5 and k6 are not read.
  - rot has small x/z tilts. The quat is garbage here.
- **DroneSpawner 241**: `DroneSpawner_Create` 0x30530, invisible logic.
  - k0 = mode (2 keep topped up, 3 waves; <=1 not created).
  - k1 = the group it owns.
  - k2 = max alive, k3 = number of waves (0 = endless).
  - k4 = start switch channel, k5 = stop channel, k6 = done channel (set when finished), k7 = abort channel.
  - k8 = minimum distance from the player (0 -> 10 m).
  - Drones spawn at their own placements, out of the player's sight (`DroneSpawner_SpawnDrone` 0x323e0).

## 3. Player and multiplayer

- **PlayerNewStartPos 36**: single player only.
  - `Player_AddNewStartPos` 0xa98c0 `(pos*, rot*, enabled=1, lvl*)` copies the whole level_tag.
  - `Player_Start` picks the first start whose k0 switch channel is on or 0.
  - k3 = starting movement (0 walk, 1 swim, 2 zero-G with anim script 0x06000a00, 3 crouch with 0x060009ff).
  - k1 and k2 are not read (k2 equals k0 where set).
  - Bond is first person, so export a marker at pos, facing rot.y.
- **AddNewStartPos2 45**: the same function with `enabled=0`. Player_Start skips disabled starts and nothing else
  reads them, so on Xbox these are never used (likely).
- **MPSpawnPoint 37**: MP only. `MP_RegisterSpawnPoint` 0x9d2b0 `(pos*, rot*, team=k0)`.
  - Team 0/1 = team halves; 2 = never registered.
  - The actual eye point = pos dropped to the floor (3 m search) + 1.6.
- **MPObject 38**: MP only. `MP_RegisterMPObject` 0xa2df0 `(pos*, rot*, lvl*, celglist)`, then
  `MP_CreateObject` 0xa1710.
  - k0 = object type, and each type exists only in its game mode: 0 CTF flag, 1 CTF base, 2 uplink, 3 demolition,
    4 espionage base, 5 blueprint, 6 GoldenEye key, 7 GoldenEye crystal, 8 protection, 9 hill.
  - k1 = team.
  - The model is the placement glist (flags, bases, ...), placed with `RotMatrix(rot)` (formula in the JSON;
    rot.x is 0 or pi).
  - If k2 is non-zero, the glist is hidden and a ScriptPlayer plays object script k2, with k5/k3/k4 as its other
    arguments.

## 4. Vehicles, turrets and weapons

- **Car 218**: `Car_Create` 0x266f0 `(pos*, rot*, celglist)`; lvl is not read and k0 is always 0.
  - It is not created in MP with mini-vehicles off, in Ravine, or beyond 8 cars.
  - Glist = the RC car body (0x020003d6 "RCCar"). `Car_InitBits` adds turret/barrel/body parts.
  - Unless `MPSettings.MiniVehiclesEnabled` is 1 (or 3 with a coin flip), it becomes the heli: gfx 0x02000194
    "LittleNellie", raised 1.5 in y.
  - Found in MP maps (and 07000002). Yaw only.
- **Copter 47**: `Copter_Create` 0x6ba90 `(pos*, rot*, lvl*)`; the glist is not used.
  - It follows a path: `Copter_CreatePath` uses header +2 (placement+0x40, frame count), +4 (placement+0x42,
    duration) and +8 (m_paths[placement+0x44]), plus k1-k3 and k10 (duration override).
  - The visible helicopter is a ScriptPlayer playing object script **k4** (k5 is a second script).
  - Hidden until switch channel k6 is on. k7-k9 are stored.
  - General Euler rotation (View_RotTransMatrix).
  - 4 of the 10 records have k4 = 0, so nothing from them is visible.
- **GunTurret 39**: `GT_Create` 0x9a200 `(pos*, rot*, quat*, lvl*, celglist, NULL)`.
  - It needs both k2 and k3, which are object scripts loaded at pos/rot; they are the visible parts. The placement
    glist is hidden (0x810).
  - k0 = ceiling-mounted (aims along -up, 0xffff health) vs floor mount (200).
  - k1, k4, k5 are u16 values stored on the object; their meaning is unknown.
  - The quat is passed but unused.
- **GunImp 52**: `GunImp_Create` 0x26ba0 `(pos*, quat*, celglist)`. **It uses the placement quat**
  (`Quat_QuatToMat`) and ignores rot and the level_tag, so k0/k1 are dead data.
  - It creates two objects at one matrix: the placement glist (GunImpBase / PowerStationGunImpBase), and a barrel
    from table 0x163a3c (stride 0x1c).
  - Single player uses entry 11 = 0x02000b50 "PowerStationGunImpBarrel". MP uses entry `weaponSet` (list in the
    JSON).
- **Searchlight 222**: `Searchlight_Create` 0x6da10 `(pos*, rot*, lvl*, celglist)`; parsemap calls it even without
  doCreation.
  - Each record is one part, chosen by k0: 0 stand, 1 lamp head (master; k1 enable channel, k2 disable, k3 alert,
    k4/k5 sweep limits in degrees), 2 projected spot (a white dynamic light of range 500; its object is moved
    10000 away, so its glist is never seen), 3 beam cone, 4 corona.
  - Parts within 3 m of each other link together.
  - Draw each part's own glist at pos/rot (yaw), except part 2.
- **Mine 254**: `Mine_Create` 0x69790. The placement glist is the trip bomb (EvilSilo only), placed with a general
  Euler rotation (wall-mounted).
  - k0 = explosion strength, k1 = radius, k5 = explosion effect (object script).
  - k2-k4 = spin/bob (0 in the data, so it is static).
- **SpaceMissile 57**: `Create_SpaceMissile` 0xcc4c0; no glist.
  - The visible rig = object script **k0** at pos/rot.
  - k1/k2 = anim scripts, k3 = delay, k4 = missile number, k5-k7 = switch channels, k8-k11 = optional extra
    scripts.
  - SpaceStationD only.
- **SpaceLaser 56**: `Create_SpaceLaser` 0xcc860; no glist. k0-k2 = object scripts loaded later by
  `SpaceLaser_Update`; k3-k7 = timers and switch channels. One record.
- **Apocalypse 74**: `Apocalypse_Create` 0xa4180. A screen effect, not an object.
  - k0 = trigger channel, k1 = seconds, k2 = rumble, k3 = screen effect, k4 = flash-bang.
  - Its glist is a marker: treat it as invisible.
- **PCQWorm 53**: `PCQWorm_Create` 0xd0450. The glist is the computer to hack.
  - k0 = key code number, k1 = switch channel set when hacked, k2/k3 = bytes.
- **Shooter 210**: `Shooter_Create` 0x69910. When switch channel k0 turns on, it fires one `weapon_data[k1]`
  projectile along the matrix direction, then deletes itself. The glist "ShootyBangBang" looks like a marker
  (guess).
- **Grapple 58**: `Grapple_Create` 0xb0440. No keys; the glist is the grapple point, placed with a general Euler
  rotation.
- **No placements in any level** (covered briefly):
  - **Creature 224** `Creature_Create` 0x2e8c0: glist model; k0 kind (1 = fish), k1-k4 params.
  - **Sub 54** `Sub_Create` 0xcef20: skin 0x05000068 + looping anim script 0x060006d9; path header.
  - **MiniSub 55** `MiniSub_Create` 0x9bcd0: skin 0x05000074 + anim script 0x06000741.

## 5. Open questions

- The drone idle anim-state -> anim script mapping (the anim-set tables) was not traced.
- Drone key7 and the meanings of the 91 behaviour properties are unknown.
- The object-script (entry type 7) format: GunTurret, Copter, SpaceMissile and SpaceLaser are only visible through
  it.
- Which Euler order the editor's quat is the inverse of, for the few records with a general rotation (my types use
  rot plus the engine builder, so it does not matter here).
- The value of `MiniVehiclesEnabled` in single player, which decides the RC car vs heli look for Car.
