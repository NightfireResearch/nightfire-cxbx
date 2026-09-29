# Level objects: doors, pickups, triggers and other logic (group "logic")

Xbox `default.xbe` addresses. All of this was read from Ghidra (decompiler and disassembly), with no writes. Every
interpretation was checked against the placements in all 32 bundles. There are 934 distinct 01xxxxxx packs by
content. Note that 130 pack names occur with *different* content in different bundles, so deduplicate by content, not
by name. The machine-readable version is `tools/level/types_logic.json`: one entry per type id, with per-field
`seen` value ranges, a `channelRole` (in/out/inout) on every switch-channel field, and record counts split into map
pack and object packs. The scratch decoders are in the agent scratchpad (`level/logic/pl.py`, `stats.py`, `ents.py`,
`make_json.py`).

Conventions. "kN" means placement key N, which is the dword at level_tag+0x2C+4N. The header fields are:
`keys` = level_tag+2 (placement+0x40), `dur` = level_tag+4 (placement+0x42), `path` = level_tag+8
(= m_paths[placement+0x44], set only when keys != 0). The confidence tags are **C** confirmed, **L** likely and
**G** guess.

## 0. Things common to all types

### 0.1 Transform
Nearly every Create calls `control_create_object(size, &pos, &rot, NULL)` 0x2dc10. Because the quat
argument is NULL, it sets `obj->mtx = View_RotTransMatrix(rot, pos)` 0xd92e0 (row-vector D3D matrix, translation at
m[12..14]). With `cx = cos rot.x`, `sx = sin rot.x`, and likewise for y and z:

```
r0 = ( cz*cy,                 sz,      -cz*sy                )
r1 = ( sy*sx - sz*cy*cx,      cz*cx,    cy*sx + sz*sy*cx     )
r2 = ( sy*cx + sz*cy*sx,     -cz*sx,    cy*cx - sz*sy*sx     )
```
None of these types reads the placement's +0x24 quat or its +0x34 vec3. The +0x34 vec3 is (1,1,1) on every record
of these types; in parsemap_create_dynamic_objects only Drone_AIVolume uses it. **C**

The quat convention was checked against the door paths. The path quats give the same rotation as the Euler `rot`,
and they equal the *conjugate* of the placement's +0x24 quat (x, y, z, w; w last). An example: rot = (0, pi/2, 0)
gives r2 = (1,0,0); the path quat is (0,.707,0,.707) up to sign; the placement quat is (0,-.707,0,.707). **C**

### 0.2 Paths (block 0x19) and path followers
Each 0x19 block of the **map file** (bundle entry type 1) is appended to `m_paths` in file order. The handler
`parsemap_handle_block_id` case 0x19 registers it only when parsing with param 1, which is the map file. The
placement's +0x44 value indexes this array. A path is `keys` records of 0x1c bytes, `{vec3 pos, quat q (x,y,z,w)}`,
in **world space** (the block length is exactly 0x1c*keys). `Spline_Interp(path, 0, keys-1, t, &mtx, NULL, 2)`
0xd8df0 evaluates it: Catmull-style `Spline_Eval3D` on the positions, slerp on the quats, and `Quat_QuatToMat`, with
`t` in key units from 0 to keys-1. `KeyFrame_Interp` 0xd8ac0 is the linear variant. `dur` = frames for a full pass
(0 -> FRAME_RATE, which is one second).

Path users in this group are Door, SS, Hurt, Sensor, Lock, ThirdCam, and any trigger that has a path (moving
trigger volume). The placement `pos` equals path key 0 for all 387 doors that have a path. 27 path-using placements
of all types sit in object packs (bundle type 0), which never register paths; the lead should check how those
resolve.

### 0.3 Trigger volumes: where the extents come from (the answer for every "volume" type)
There is no box in level_tag, and the +0x34 vec3 is not used. The volume is the **collision geometry of the
placement's glist entity**: the entity's 0x2e/0x2f Coll_Data block, pointed to by `celglist->collData`. The chain
of evidence is below. **C**
- Trigger_Create sets `obj->objGraphics = celglist` and `obj->effectFlags = celglist->applyFlagsToObject`.
- Contacts arrive in `obj->hitList`. `Collide_PickObj` 0x2aa80 takes only objects whose `objGraphics->collData` is
  non-NULL. It does a sphere broad-phase on `obj->centrePoint/radius`. Then `Collide_Intersect` 0x2be10 inverts
  `obj->mtx` and intersects the mover against `objGraphics->collData` in **entity-local space**.
- So the volume is the entity's collision mesh, transformed by `View_RotTransMatrix(rot, pos)`, or by the path pose
  if the object has a path. The entity-params bbox (+0x1c min, +0x28 max, local space) bounds it and is a good box
  approximation. For example, `oDroneTrigger` has bbox ±0.7, ±0.7, ±1.8, and its collision block is v5 with 1 part,
  4+2 polygon records.
- In the data, every touch-type glist has a collision block. Most have entity flags 0x50: 0x10 = hidden (not drawn;
  the same bit as `FLAG_HIDDEN`), 0x40 = no-shadow/other. The pure logic glists (multiplexers, inverter, music
  triggers) have flags 0x30 and **no** collision block. Those are never touched.

### 0.4 Touch masks (shared by Trigger, SS, Hurt, ScriptPlayer)
While scanning `hitList`, each object builds a bit set: 0x10 = touched by a drone (object type 2, or 0x4a Sub),
0x08 = touched by the player (type 3), 0x02 = hit by a bullet (type 5). A separate bit, 0x04, means "the player
pressed Action on it": `Player_Activate` walks the player's hits and calls `Door_Activate`, `Trigger_Activate`,
`SS_Activate`, `Monitor_Activate`, `Lock_Activate` or `SP_Activate`. The placement's mask field is ANDed with this set.

## 1. The switch-channel system (how the objects link up)

`switch_channels` is a byte array of 256 entries; channel 0 means "none". There are three companion arrays:
`switch_channels_prev` (previous frame, used for edge detection), `switch_channels_hold`, and
`switch_channels_time[256]` (the frame when a channel last went to 1). `Init_SwitchChannels` 0x2cfd0 clears them at
level start. Every link between objects is a channel number stored in a placement key. Producers write 0/1 and
consumers poll. There is no direct object-to-object reference. So the exporter should treat each channel as a named
node per level, `ch<N>`, and draw an edge from each `out` field to the channel and from the channel to each `in`
field. The fields are named `...SwitchChannel` and tagged in the JSON with `channelRole`:

- **out**: the object writes the channel. Examples: trigger outputs, Destroy.destroyedSwitchChannel,
  Pickup.pickupSwitchChannel, Sensor.alertSwitchChannel, Lock.unlockedSwitchChannel, FuseBox, CamSubject.
- **in**: the object reads it. Examples: Door.unlockSwitchChannel, multiplexer inputs, the enable/disable/kill
  channels, SoundTrigger/MusicTrigger play channels, and ScriptPlayer.gateSwitchChannel.
- **inout**: both. Examples: Door.stateSwitchChannel (the door publishes its open state, and setting the channel
  opens it), Switch.switchChannel, SS.switchChannel, ScriptPlayer.switchChannel.

Channel values seen in these fields are 1..0xe5, all below 256. Within this group alone, 676 of 872 consumed
channels have a producer in the same level. The rest are written by other groups (drones, DroneSpawner, Rotor,
Searchlight...), by game scripts, or by hard-coded code: `Trigger_Calc` 0xd3950 turns channels 0x65..0x6e into
"Bond moments", channels 0x60..0x63 block level exits, and there are level-specific rules (castle exterior
0xa4 -> 0x6f, tower C 0x34 -> 0x2d). Door.unlockSwitchChannel = 200 on 51 doors ('Door_Locked_*'). No placement
writes channel 200; only DroneAIVolume1 k0 also holds 200. So these doors are probably permanently locked.

## 2. Per type

### 219 Door: Door_Create 0x2f640 (cdecl `(pos, rot, lvl, celglist)`), DOORINFO 0x74 bytes. **C**
**How it moves.** A door is **a path follower**, not a hinge or axis. `movement` (k0 -> obj->subState) chooses the
behaviour:
- **k0 = 0, swing** (118 doors: `keys` = 3 on 90, 2 on 4, no path on 24). Rest pose = `Spline_Interp(path, t = 0.5*(keys-1))`,
  which is the **middle key**. It opens to t = 0 or t = keys-1 depending on which side the player is on (the
  player's position in the door's closed frame, via `Door_SetupSwing` 0x2ec30). In the data, key 0 and key 2 are
  the two open poses about ±90° apart, and key 0 equals the placement pos/quat. So the placement stores an *open*
  pose, and the exporter must use the middle key for the closed door.
- **k0 = 1 or 2, slide/path** (322 doors: `keys` = 2 on 290, 3 on 3, no path on 29). Closed = key 0 (= placement pos). Open = the
  last key (`FUN_0002eb80`). The slide direction and distance are the difference between keys 0 and N-1; examples
  are 1.6-1.8 m sideways and 1.3 m up for a secret door.
- The door is animated by `Door_Interp` 0x2ee70 with t = 0..1 scaled by keys-1. Spline interpolation is used if
  flag bit 0 is set, otherwise KeyFrame (linear). Speed = 1/`dur` per frame (`dur` = 0 -> one second); seen 90,
  120, 30...
- If the flags have "starts open" (bit 2), the rest pose is t = keys-1 (slide) or 1.0 (swing).
- **No path (keys = 0, 57 doors)**: Spline_Interp returns immediately on a NULL path. The door stays at pos/rot and
  can never move (fake doors).

**What opens it.**
- The player's Action button (unless k11 is set).
- Drones (`NDrone2_OpenDoor`).
- `stateSwitchChannel` k2 becoming 1; k2 becoming 0 closes it.
- A rising edge of `unlockSwitchChannel` k3. This opens the door once, shows "objective" text (except on EvilBase
  and Tower2Elevator), and removes the lock.

While k3 is set and its channel is 0, the door is locked and plays the locked sound. `doorGroup` k1 ties doors
together: `Doors_Calc` copies the state of the moving door to every door in the same group. An open door
auto-closes after 6 s unless flag bit 5 is set (and not while someone stands in it, see `FUN_0002f850`).

| key | off | name | type | meaning / seen |
|---|---|---|---|---|
| 0 | 0x2c | movement | enum | 0 swing, 1/2 slide. Seen 2 x322, 0 x118 |
| 1 | 0x30 | doorGroup | u16 | 0 = none; 83 distinct values up to 99 |
| 2 | 0x34 | stateSwitchChannel | switch inout | 9 doors |
| 3 | 0x38 | unlockSwitchChannel | switch in | 90 doors; 200 x51 |
| 4 | 0x3c | flag_d9_8 | bool | non-zero -> obj+0xd9 bit 0x8; no reader found (**G**) |
| 5/6/7 | 0x40/44/48 | openSound / closeSound / lockedSound | u16 sound id | 0 -> none (0xffff); e.g. 0x11d/0x11c, 0x4c4/0x4c3; locked 0x140 |
| 8, 9 | 0x4c, 0x50 | flagsA, flagsB | bits, ORed | bit0 spline interpolation, bit1 always set (?), bit2 starts open, bit5 never auto-closes. k9 is 2, 0x22 or 0x24 |
| 10 | 0x54 | oneWay | u8 | 1/2 = refuses from one side (dot of obj +z with door-player); one door |
| 11 | 0x58 | playerCannotOpen | bool | the Action button does nothing; 8 doors |

Header: keys, dur, path. Visual: the placement glist (`Control_SetGList`), posed by the path as described above.

### 240 Pickup: Pickup_Create 0xa7380, cdecl `(pos, rot, NULL, celglist, (u16)k0, (u16)k1, k2, k3, k4, 1, (u16)k5, 0, k6)`. **C**
This call was checked in the disassembly at 0xa4c56.
- **k0 pickupKind**: 0 weapon, 1 ammo, 2 key (`Player_EquipKey(k1)`), 3 armour, 4 message item (shows TXT k6),
  5 special, 6 "007" Bond bonus (created only when the level's gold medal is held), 7..11 multiplayer weapon-set
  slot (`Pickup_CreateFromSet` 0xa80b0).
- **k1 weaponId**: weapon_data index (seen 2..0x52, e.g. 0x34 grenade, 0x47 armour). 0xffff means nothing is
  created.
- **k2 amount**.
- **k3 pickupSwitchChannel** (out, set when taken).
- **k4 pickupSound** (0 -> none; kinds 0/1/3/6 override it with 0xf5/0x141/0x14c).
- **k5 respawn**: 0 = gone once taken; 0xffff = infinite; n = respawns after n*10 s.
- **k6 messageText**: a TXT label id such as 0x0200004a. These look like hashcodes but are text ids.

Visual: the **placement glist mesh** (PICKUP_P99, 'Pickup 007', oPickup_Grenade...). For kinds 7..11 the glist is
the mesh-less 'PICKUP' placeholder, and the real model is `weapon_data[PickupMatrix[weaponSet][k0-7]]
.wpn3rdPersonGfx`, which depends on the multiplayer weapon set. That is not knowable offline; draw a marker.
Transform: Euler; the position is dropped to the floor with `build_PointOnFloor` (**L**, keep pos).

### 32 Breakable: Break_Create 0x1ffb0, cdecl `(pos, rot, celglist, lvl)` (glist before lvl). **C**
- k0 breakType: 2 = window/glass (Break_ShatterWindow), also 1 and 0.
- k1 health: u32 converted to float; 0 = one hit.
- k2 damageFilter: stored as ~k2, a bullet filter; seen 0x20, 0x30.

Visual: the placement glist (glass, bottles, windows); Euler transform.

### 225 Destroy: Destroy_Create 0x20040, cdecl `(pos, rot, lvl, celglist, pCelList + (celEntityNum+1))`. **C**
- k0 destroyedSwitchChannel (out).
- k1 smashSwitchChannel (in; the object destroys itself when set).
- k2 explosion: 0 none; 1/2/>=3 select explosion scripts 0x06000027 / 0x0600004f / 0x06000052, with radius
  4r / 2r / 3r.
- k3 health (10, 20, 10000).
- k4 damage filter (~k4).
- k5 explosion damage x0.01.
- k6 delay in seconds before k0 fires.

Visual: the intact placement glist. The destroyed model is the entity's LOD-block alternative (119 of the
map-pack records). Otherwise it is **the next entity in the map pack**, `pCelList[celEntityNum+1]`: 47 records,
e.g. `room_communication_tv2` -> `room_communication_tv2_damaged`, `yee1` -> `zee1`. If the glist is hidden there is
no destroyed model (23 records). Not created in multiplayer when explosive scenery is off.

### Triggers: Trigger_Create 0xd26d0 and the wrappers
TRIGGERINFO (0x3c bytes):

| offset | field |
|---|---|
| +0 | path |
| +4 | t |
| +8 | speed |
| +0xc | keys-1 |
| +0x10 | the object's up vector |
| +0x1c | **out** (u32) |
| +0x20 | disableChannel |
| +0x22..+0x2e | in[0..6] (u16) |
| +0x30 | in[7] = 0 |
| +0x32 | touchMask (0 -> 2) |
| +0x34 | kind |
| +0x36 | hideWhenDisabled |
| +0x38 | message cooldown |

`Trigger_Update` 0xd3560 does the following:
- It returns early while `disableChannel` is 1 (and, if hideWhenDisabled, sets effect flags 0x30).
- It moves the object along its path, looping, if keys != 0.
- It evaluates the touch test `FUN_000d2840`. That test returns true when `touchMask & touched`. For kinds 1/2
  with the player/drone bits, every non-zero in[i] is an extra filter: 11 = a drone must touch, 14 = the player
  must, any other value = a drone whose id (drone+0xc4) equals in[i]. With the bullet bit, the filter is a weapon
  id. touchMask bit 0 additionally requires channel in[4].

Every wrapper copies level_tag into a scratch buffer and rewrites the keys into the Trigger_Create layout. The
per-type key meanings below are after that remap.

| type | wrapper | kind | key mapping (after the remap) |
|---|---|---|---|
| 234 TriggerTouchOnce | 0xd3380 | 1 | k0 **out** (latched to 1, then the object is deleted if hidden); k1 touchMask (8 x321, 4, 0x10, 2, 0x18); k2..k7 = in[0..5] filters (in[6] = k8); **k7 disableSwitchChannel**; k8 hideWhenDisabled |
| 235 TriggerTouch | 0xd3310 | 2 | k0 **out** (1 only while touched; `Trigger_Calc` clears and re-evaluates it each frame); k1 touchMask (8, 0x18, 0x10, 4); k2..k7 in[0..5] (k2 = 11 or 14 seen); **k6 disableSwitchChannel**; k7 hideWhenDisabled |
| 220 Trigger | (direct) | k0 | k0 kind, k1 out, k2..k8 in[0..6], k9 touchMask, k10 disable, k11 hide. Data: kind 2, out 0x1b, mask 0 -> 2 = "shoot the skylight" |
| 236 TriggerMultiplexIn | 0xd33f0 | 3 | k0 **out** = AND(in); k1..k6 in[0..5]; k8 in[6]; k7 lost (always 0). The list stops at the first 0 |
| 237 TriggerMultiplexSIn | 0xd34b0 | 4 | as 236, but the inputs must have fired **in order** (each set time >= previous - 4 frames) |
| 239 TriggerMultiplexOrIn | 0xd3450 | 12 | as 236, but OR |
| 238 TriggerMultiplexOut | 0xd3500 | 5 | k0 = the **input**, copied every frame to outputs k1..k6 and k8 |
| 232 TriggerLoadLevel | 0xd3260 | 10 | k0 level hashcode (07xxxxxx; 15/16 exist, 0x07300013 does not); k1 required channel (else "objectives incomplete"); k2 auto-activate channel (also chooses the output); k3 channel set on load; k4 channel set on load when k2 = 1. Mask forced to 0xe. Fired by Action (Player_Activate -> Trigger_Activate) or by k2 |
| 244 TriggerMoviePlayer | 0xd32c0 | 13 | k0 movie hashcode, k1 mask (bit 4 = Action plays it), k2 channel that plays it (in no level) |

Visual:
- The touch triggers (234/235/220/232/244) are a **volume** (section 0.3). The glists are named 'oDroneTrigger',
  'New Trigger3 (Once)', 'laserbeam', 'FallPoly'..., and are hidden.
- Kind 10 clears effect flag 0x10. So a TriggerLoadLevel whose glist has a mesh is **drawn**: 'Door_Gate8',
  'DOOR_loadlevel', 'Locked_Door_Top_I' are fake exit doors.
- The multiplexers (236-239) are **invisible logic**. Their glists are flags 0x30 with no collision, and their
  positions carry no meaning.
- Moving triggers: 8 TouchOnce records (keys 3, dur 480) and 15 Touch records (keys 3 or 6) follow paths, for
  example sweeping laser beams.

**C** for all of the above, except 244 (**L**, no data).

### 217 ScriptPlayer: SP_CreateScriptPlayer 0xc4500, cdecl `(pos, rot, lvl, NULL, NULL, NULL)`. **L**
SCRIPTPLAYER 0x3c bytes. Not created if k0 == k1 == 0.
- **k0 script0**, **k1 script1**: game scripts, 06xxxxxx. All 172+29 references resolve to **bundle entry type 7**
  (Script_Load 0xc1550: stream-based "game scripts", not the type-5 anim scripts). 0x06000000 (17 records) is a
  placeholder and resolves to nothing.
- k2/k3 play mode of script 0/1: 1 = play once immediately, 8 = loop, 0x40 = loaded, paused at frame 0 waiting for a
  trigger, 0 = loaded only on trigger.
- k4 dieAfterPlay.
- k5 triggerMask: 0x01 channel k6 is 1, 0x02 a script object is shot (weapon k8 if non-zero), 0x04 unpaused
  (SP_UnPause, e.g. by a Switch or by damage), 0x08 the player touches a script object, 0x10 a drone does.
- **k6 switchChannel** (inout: cleared at create; a trigger source; written 1/0 when triggered).
- k7 damage threshold that unpauses it.
- k8 bullet weapon filter.
- **k9 gateSwitchChannel** (in: nothing happens while it is 0).
- k10 scale percent (100).

Visual: **no geometry of its own**. objGraphics is not set, and the placement glist ('Script Player',
'ScriptPlayer_SolarPanel'...) is an editor marker. The game script creates and animates entities.
Script_Load builds its matrix with `View_RotTransMatrix(rot, pos)`. Decoding the type-7 script streams, i.e. which
entities they spawn, is an open item.

### 226 SS (simple script): SS_Create 0xcb180, cdecl `(pos, rot, lvl, celglist)`. **C**
A **path-animated object**. It needs a path; with keys = 0 it is not created. `dur` 0 -> 75 frames.
- k0 mode: 8 = loops forever, 0x40 = waits for a trigger then plays once, 1 = playing.
- k1 rearm.
- k2 triggerMask (bit 0 = do not write the channel).
- **k3 switchChannel** (inout: a 1 triggers it; a touch writes 1).
- k4 trigger sound.
- k5 pause point (path frames).
- k6 1 = spline, 0 = keyframe interpolation.
- k7 writes the cel shearHeight (always 0).

Visual: the placement glist (barrels, lamps, 'OBJECT_CableCar') moving along the path from key 0.

### 228 Hurt: Hurt_Create 0x6c530, cdecl `(pos, rot, lvl, celglist)`. **C**
- k0 hurtType (u8; 6 on most records; returned by Hurt_GetType to the player).
- k1 damage (float; 1000 = death plate).
- **k2 enableSwitchChannel** (in; off, with effect flag 0x20, while 0).
- k3 sound per path lap (always 0xffff).
- k4 arm mask (drone/player/bullet touch) used when there is no channel; 0 = always on.
- k5 1 = one pass along the path, then inert.

Visual: a damage **volume** (section 0.3: 'Death Plate', 'Hurt_A', 'HurtPoly'...). A few Hurt glists have visible
meshes (e.g. OBJECT_MissileFlame). It moves along the path while active.

### 40 Sensor (security camera): Sensor_Create 0xd0630, cdecl `(pos, rot, lvl, celglist)`. **C**
The repo already reimplements this type in `src/action/game/obj/Sensor.cpp`.
- **k0 beamModel** hash: entity 0x02000360 'Camera_Beam', a child object scaled by k2 and hidden until active.
- k1 full cone angle in degrees (15, 5, 45).
- k2 beam length / range (5-7).
- **k3 alertSwitchChannel** (out, after the player has been seen for 2 s).
- **k4 disableSwitchChannel** (in; 0x78 on 12 records).
- k5 0 = listed for Monitors.
- k6 cameraNumber (matched by Monitor k0).
- **k7 bodyModel** hash: 0x02000652 'camera(a)-Hub', a child object kept upright.
- **k8 viewedSwitchChannel** (out, while a Monitor shows it).

All hashes resolve to entities in the same bundle. Visual: the placement glist is the **lens** ('camera(a)-lens').
The body and beam are extra entities at the same pos. The path (keys 3/5, dur 600/420) is the sweep; its quats
rotate the lens. The LOD alternative is the destroyed model.

### 41 Switch: Switch_Create 0xcfbd0, cdecl `(pos, rot, lvl, celglist)`. **C**
The repo already reimplements this type in `Switch.cpp`.
- **k0 switchChannel** (inout; initialised to k3).
- **k1 script** (06, type 7; 0x0600006f on 21 records), played through an internal `SP_Create(pos, rot, k1, 0, 8,
  0, switch, NULL, NULL, 4)`.
- k2 autoResetFrames.
- k3 restState.
- k4 oneShot.
- **k5 gateSwitchChannel** (in).
- k6 security-system item index (`SSysItems[k6+1].switchChannel = k0`).

Visual: **objGraphics = NULL**, so the placement glist ('alarmswitchplayer') is not drawn. The lever is the script's
geometry at pos/rot.

### 46 Lock: Lock_Create 0xd13f0, cdecl `(pos, rot, lvl, celglist)`. **C**
- k0 lockType: 0/2 keypad/key (code KeyCodes[k1], decryptor gadget, or key k1 held); 1 laser-cut lock (worn down by
  bullets of weapon 0x4e/0x4f).
- k1 keyId.
- k2 unused.
- k3 unlockSound, k4 deniedSound (0xffff in the data).
- **k5 unlockedSwitchChannel** (out when unlocked; a type-0 lock re-locks if the channel is cleared).
- k6 unlocked on difficulty 1.

Visual: the placement glist ('Lock - KeypadLocked', 'Lock - Laser', 'Lock - Key'). With a path, the initial matrix
is path key 0. After unlocking it runs along the path and then switches to its LOD alternative (KeypadLocked ->
KeypadUnlocked).

### 48 Monitor: Monitor_Create 0xd0f10. **C**
- k0 cameraNumber (obj->subState): using the monitor views the Sensors with that k6.
- k1 non-zero = indestructible; 0 = health 1, and shooting it explodes it and switches to the LOD alternative.

Visual: the placement glist (consoles, TV).

### 49 FuseBox: FuseBox_Create 0xcffb0. **L**
- **k0 script** (06, type 7), run through `SP_Create(pos, rot, k0, 0, 0x40, 0, obj, FUN_000cff50, info, 0)`. The
  callback puts the script's objects into two groups of up to 10 fuses.
- **k1/k3** are channels set when group 0 is fully shot out; **k2/k4** the same for group 1.
- If k2 = 0, shooting a group-1 fuse hurts the player by 5 and cancels k3.

Visual: objGraphics = NULL, so what is drawn is the script's objects. 46 of the 53 records have no keys; they all
sit in object packs (bundle type 0).

### 51 Hint: Hint_Create 0xd30e0 (only when hints are enabled). **C**
- k0 TXT label (0x02000057 and similar; text ids, not hashcodes).
- k1 sound.
- k2 difficulty filter: 0 all; n = difficulty <= n; 5/6/7 = only difficulty 1/2/3.
- **k3 enableSwitchChannel**, **k4 killSwitchChannel**.

Visual: an invisible touch **volume** ('Hint Small Plate'). It shows the message once when the player touches it.

### 65 InverterTrigger: InverterTrigger_Create 0xd2d50, cdecl `(lvl)`. **C**
No position and no glist; the object is created at the origin. While channel k0 is 1, channel k1 is forced to 0.

### 249 SoundTrigger: SoundTrigger_Create 0xd2dc0. **C**
It is **channel-driven only**: the glist is never touch-tested.
- k0 play channel.
- k1 one-shot.
- k2 sound id (not created if 0 or 0xffff).
- k3 1 = 2D sound, 0 = 3D at pos.
- k4 stop channel.
- k5 exclusive.
- k6 difficulty filter (as Hint).
- k7 keep playing.

Visual: none; a sound emitter at pos.

### 251 MusicTrigger: MusicTrigger_Create 0xd2fe0. **C**
- k0 trigger channel.
- k1 kill channel.
- k2 music event id (0xff = not created).
- k3 one-shot.

`Music_Event(k2, 2)` is sent for 30 frames. Invisible; the position is not used.

### 231 ThirdCam: ThirdCam_Create 0x253c0. **L**
- k0 camera index (table 0x1dc700).
- k1 flag.

It uses `keys` and `path` (not dur): `ThirdCam_GetPos(obj, f) = Spline_Interp(path, f*(keys-1))`. This is the camera
rail for wall-hug, hand-over-hand and zipline sections. Draw the path; the glist is a marker.

### 250 ThirdIcon: ThirdIcon_Create 0xaf440. **L**
- k0 third-person mode, written to player+0x8ef while touching: 6 shows "press Action"; 2 and 3 are also seen.
- **k1 enableSwitchChannel** (0 = active from the start).
- **k2 killSwitchChannel**.

Visual: an invisible touch **volume** ('3rd Person Icon').

### 253 CamSubject: CamSubject_Create 0x25700. **C**
- **k0 photographedSwitchChannel** (out): set, and the object deleted, when hit by a bullet of weapon 0x54 (the
  spy camera).

Visual: an invisible **volume** ('Camera_Subject').

## 3. Data notes
- Records by pack: see `count` in the JSON. The key-less records all sit in object packs (bundle types 0/2/11).
  Examples are Lock 69 of 90, Switch 46 of 71, FuseBox 46 of 53, Pickup 114 of 489, and Trigger 220 21 of 24. The
  map file (type 1) holds the real, keyed instances. `parsemap_parsemap` state 2 calls
  `parsemap_block_map_data_dynamic(..., doCreation = false)` for object packs, so whether those records ever create
  objects is for the lead to settle.
- The hashcode fields were checked. ScriptPlayer, Switch and FuseBox scripts all resolve to bundle type-7 entries
  (apart from 0x06000000). Sensor models resolve to entities in the same bundle. TriggerLoadLevel levels exist,
  except 0x07300013. Pickup k6 and Hint k0 are TXT label ids that only look like hashcodes.
- Sound ids (Door, Lock, SS, SoundTrigger, Pickup) are Action_SFX indices, not hashcodes.

## 4. Open questions
- Door k4 (obj+0xd9 bit 0x8) and door flag bit 1 (always set): no reader found.
- What the type-7 game scripts spawn (ScriptPlayer, Switch and FuseBox visuals). It needs a decoder for the
  Script_Load stream format.
- The 27 path-using placements in object packs, since only the map registers `m_paths`.
- Pickup floor-drop: whether the matrix translation follows `build_PointOnFloor`.
