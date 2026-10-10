# The player, weapons and multiplayer

This covers the human player in the action engine (`default.xbe`): the player object and its per-frame update,
movement and the special movement modes, the weapon state machine and how weapons are defined, bullets, the
mission statistics, the HUD only as far as it reads player state, and the multiplayer layer (game modes, scoring,
respawn, settings) that sits on top of it. Multiplayer bots are drones and are documented in
[../drone/bots-and-navigation/](../drone/bots-and-navigation/README.md); this file only says where they plug in.
Placement records for player starts, MP spawn points and MP objects are in `docs/level/objects-actors.md` on the
`blender-exports` branch (section 3) and are not repeated here.

Evidence is marked in place: **[D]** decompile or disassembly of the Xbox build read for this document, **[X]**
xrefs or table bytes read from the XBE, **[S]** our source (`src/action`), with its own evidence, **[I]** inference.
Addresses are Xbox. Names are Ghidra's (mostly from the PS2 `ACTION.ELF` symbols) unless marked "(invented name)".

## Status at a glance

| area | address range | funcs | ours | dead | live | notes |
|---|---|---|---|---|---|---|
| movement, camera, ladders, wires, ziplines, grapple, creep walls | 0xa8980-0xb04a0 | 70 | 7 | 0 | 63 | ours: `Player_Move`, `Player_ChangeState`, `Player_SetCamMode`, `Player_Start`, start positions, `Ladder_Create` |
| PlrStat mission statistics | 0xb04a0-0xb1a30 | 30 | 16 | 2 | 12 | `PlrStat_GetScore` (2.3 KB) is ours and shadow-tested |
| weapons, ammo, aiming, auto-aim, upgrades | 0xb6c20-0xbd830 | 58 | 14 | 0 | 44 | ours: `Player_Weapon`, `Player_InitWeapon`, ammo helpers, upgrades, laser setup |
| Bullet | 0x211f0-0x24000 | 16 | 0 | 0 | 16 | shared by every shooter, not just the player |
| **player total** | | **174** | **37** | **2** | **135** | 22% of functions, 13% of bytes (75 KB) |
| HUD (ui.hud) | 0xb1a30-0xb6c20 | 45 | 16 | 3 | 26 | `HUD_Update`, health/status panes are ours |
| **mp** | 0x9c7e0-0xa3180 (+0x1a190, 0x1a1e0) | **77** | **17** | **1** | **59** | `MP_Init`, `MP_Update`, `MP_RestartScenario`, team/object getters |
| static initialisers | 0xf5530 | 1 | 0 | 0 | 1 | `WeaponDataTableInit`, 44,880 bytes |

Counts are from the coverage run (`all_functions.txt`, `summary.txt`) [X]. The live functions include every hub:
`Player_Update`, `Player_CollisionHandler`, `Player_Init`, `Player_WeaponFiring`, `Player_SetWeaponAnimObj`,
`Player_Aiming`, `Player_PositionCamera`, all of `Bullet_*`, and every MP scenario update.

## How it works

### Creation

`ResetMap_Load` 0xbfb60 calls `Player_Start` 0xae870 (ours) and then `MP_Start` 0xa25f0 [X]. `MP_Init` (ours)
runs earlier at every level load and clears all per-match state unless the next level is the menu [S].

- Single player: `Player_Start` picks the first enabled start position whose switch channel is on or zero and calls
  `Player_Init(0, pos, rot, level_tag)` [S].
- Multiplayer: `MP_Start` creates the objective objects for the mode (a random demolition / protection /
  blueprint place, two GoldenEye spawns), then `Player_Init(i, spawn)` for each human (slots 0-3, spawn from
  `MP_GetSpawnPoint(team)`), then `BOT_init(NUM_PLAYERS + i, ...)` for up to `NUM_BOTS` bots (clamped), then the
  assassin radar sprites and the time and status sprites [D].

`Player_Init` 0xacab0 [D]:

- returns NULL if the start's key 0 switch channel is off, or if `glb_players[n]` is already set;
- clears switch channels 0x5e (player died) and 0x62 (mission failed);
- `control_create_object(0x8fc, ...)`: the player's `BLData` is **0x8fc bytes**, stored in `glb_players[4]`
  (0x1f6654) and `glb_blokes[4]` (0x2774b8). Only humans have one; bots are `Drone_tag`s;
- picks the skin: per level in single player (Henderson, Castle Indoors and Space Station D have their own), or
  `MP_skins[MPSettings.Player[n].SkinNum]` in MP;
- **writes into the weapon table**: `weapon_data[1].weaponModelHashcode` (the fists' sleeve skin) and
  `weapon_data[0x37].offsetToNextAltFireVariant` (1 in SP, 2 in MP);
- sets health to 100, plus `HealthModifier` in MP;
- calls `HUD_Init`, `HUD_Update`, `Player_InitWeapon` (ours), and `Player_InitGrapple`;
- registers `Player_PositionCamera` as the viewer's camera updater (`Camera_SetUpdator`);
- sets `GRAVITY_VECTOR` to (0, -9.8, 0);
- picks the starting movement from start key 3: walk, swim, zero-G (zero gravity) or crouch, each with an
  animation script;
- creates the body glow and unpauses the PlrStat timer.

### Per frame

`Game_Run` (ours) calls `MP_Update` (ours) once per frame before the object pass. Then
`control_movement_object_handler` calls each object's entry in `control_funcs` (0x163b98, 12 bytes per object
type: update, collide, delete) [S][X]:

| type | update | collide | delete |
|---|---|---|---|
| 3 PLAYER and 18 DEAD_PLAYER | `Player_Update` 0xad520 | `Player_CollisionHandler` 0xae1c0 | none |
| 5 BULLET | `Bullet_Update` 0x23670 | `Bullet_CollisionHandler` 0x21a00 | `Bullet_Delete` 0x22bd0 |
| 53 MPOBJECT | `MP_ObjectUpdate` 0xa1ee0 | none | 0xe0ec0 (the debug hook stub) |

A dead player keeps the same handlers. `Player_CheckForDeath` switches `objectType` from 3 to 0x12 [D].

`Player_Update` [D], in order:

1. A pause press opens the pause menu (`MenuManager_Create`). Then `HUD_Update`.
2. BLData+0x8da gates the rest: 1 runs it, 0 returns, and values above 1 count down. `Player_Init` writes 1 and
   `Player_Disable(p, 0)` writes 0 [D]. **Our header calls this byte `someNightVisionThing`; it is the
   player-active flag** [I, strong].
3. If paused for this player, it stops. Otherwise: the fade or brightness at +0x818, the cel's music event, and the
   object's `curState`:
   - 0 → 1;
   - 1 (alive): the PlrStat elapsed time; zeroes `movement` and `rotationDelta`; a debug scan-mode toggle on
     controller 3. Unless +0x8db (frozen: `Player_Disable(p, 1)`) is set, it then **dispatches on `subState`**:
     `Player_SSWalk`, `Player_Climb`, `Player_SSGrapple`, `Player_Swim`, `Player_SSCrouch`, `Player_SSScanMode`,
     `Player_Wire`, `Player_Creep`, `Player_ZeroG` (both ZeroG states), the remote-control countdown, and
     `Player_Zipline`. After that come `Player_MonitorAir`, `Player_ClampSomeAngles`, `Player_Aiming` and
     `Player_ViewClamping` (ours; the mouse-look hook). Finally the frame's movement is rotated by the object
     matrix and added to the position, and `rotationDelta` is added to the rotation;
   - 2 and 3 (dying or dead): `AnimObjectUpdate`, `Player_HandleDeath`.
4. `Player_InShadow`, `Player_UpdateBlurAndFade`, `AnimObjectUpdate`, then `Player_Collision` 0xab5d0.
5. In states 1 and 4, when not frozen:
   - breath effects in third-person camera modes;
   - **hard-coded recharge of weapons 0x33, 0x45, 0x4a, 0x4c, 0x4e and 0x4f**: their `clipOrCooldown` steps by
     `FRAME_RATE_MUL` every 2nd, 4th or 6th frame (`NumFramesUnpaused`), up to the weapon's `clipSizeOrCooldown`;
   - then `Player_WeaponRecoil` → `Player_Weapon` (ours) → `Player_WeaponFiring(p, 0)`;
   - the HUD fade at +0x84c.
6. `Camera_ApplySwing(weapon_data[current].cameraSwingAmt)`, and draw-in-other-views-only.

`Player_CollisionHandler` [D] is a second update phase, run after the collision pass, not just a collision
response:

- feet-on-floor (`PlayerNotDrone_FeetOnPoint`);
- **fall damage**: frames airborne at +0x8b8. On landing, if that is over 60, it calls `Player_HandlePain` with
  (frames - 60), but only when that is 10 or more. On Tower 2B, falling below y = -10 sets channel 0x7c;
- `Player_DealWithObjHit` for every hit-list entry;
- `Player_PositionGun` and the weapon object's `AnimObjectUpdate`;
- `Player_CheckForDeath`, `Player_LaserPointer`, muzzle flash, `Player_GrappleSetRope` and `Player_SetWatch`.

So the order within a frame is: movement, aim, weapon logic, then collision, death, gun placement and effects.

### Movement substates

`subState` holds the movement type. Our `MovementType` enum [S]: 0 walk, 1 climb (ladder), 2 grapple, 3 swim,
4 crouch, 5 scan (decoder), 6 wire, 7 creep, 8 and 9 zero-G, 10 remote control, 15 zipline, 16 Ronin.
`Player_CollisionHandler` also lists two states Ghidra calls MAYBE_FROZEN_1 and MAYBE_FROZEN_2 (11-14 region)
[D]; their meaning is unknown.

Death sets `subState` to 13 or 14 (on the camera-mode bit at animState+0x51) [D]. So 11-14 may be death or
camera substates, not movement types [I].

- `Player_ChangeSubState` 0xa9720 (live) switches substates and returns the previous one.
- `previousSubState` (+0x8d2) is what remote control returns to.
- Special-movement objects (`Ladder_Create`, `Wire_Create`, `CreepWall_Create`, `Grapple_Create`) are level
  placements. The player attaches through `PlayerCollLadder`, `Player_CollWire` and `Player_CollCreepWall`, and the
  attached object is kept at +0x814.

### The player structure (`BLData`, 0x8fc bytes)

`src/action/game/obj/Player.h` declares about 60 fields up to +0x8f8 with static_asserts [S]. Well established:

| offset | field | notes |
|---|---|---|
| 0x00 / 0x0c / 0x18 / 0x24 | last position, movement, last movement, rotationDelta | the movement pipeline above |
| 0x110 | movementDisabled | `Player_Move` takes the controls away |
| 0x114 | `short ammo[34]` | carried stock per ammo type, capped by `ammo_data` (0x18ce08, 34 x 12 bytes) |
| 0x15c | `WeaponStatus weaponStats[114]` | 12 bytes each: loaded rounds or charge, enabled, fire-mode index |
| 0x770 | hudInfo | the HUD panes |
| 0x778 / 0x77c / 0x7fc / 0x804 | weapon object, 32 weapon-related objects, sight, muzzle-flash object | created by `Player_InitWeapon` |
| 0x808 | remote-control device | Sentinel / RC car (inferred from use) |
| 0x814 | attached special-movement object | |
| 0x824 / 0x840 | health (clamped 0..500 by `Player_CheckForDeath`), armour | |
| 0x838 / 0x83c / 0xf0 | pitch (+-1 = straight up or down), auto-level target and state | |
| 0x8bc | refire countdown (invented name) | `Player_WeaponFiring` [D]; header: field_0x8bc |
| 0x8c6 | shots left in the burst (invented name) | from the weapon's per-fire-mode table, below [D] |
| 0x8da / 0x8db | active, frozen | see above; the header's names are wrong or missing |
| 0x8de | playerNum | |
| 0x8e0 | camMode | `CamMode` |
| 0x8e9 | idle-fidget state (invented name) | 0/1/3, `Player_SetWeaponAnimObj` [D] |
| 0x8eb / 0x8ec | fired this frame, bullet spawned for this shot (invented names) | [D] |

Roughly two thirds of the 0x8fc bytes have no name. The pointer to the player's `AnimState` (current, previous and
switching-to weapon ids, `animFlags` bit 0 = scoped) is on `obj_tag`, not in BLData.

### How weapons are defined

- `weapon_data` at 0x18cfa0: **115 entries of 0x10c bytes** (`weapon_definition_tag`, size asserted in `game.h`)
  [S]. Entry *n* is weapon variant *n*.
  - `weaponBaseNum` groups variants (upgrades and silenced versions).
  - `offsetToNextAltFireVariant` links a weapon to its alt-fire variant.
  - `ammoType` indexes `ammo_data` and `BLData.ammo`.
- The ids and names in `WeaponBaseNum` and the user's PS2 WeaponData spreadsheet are the reference; this document
  uses ids only.
- **Built partly at run time.** Entries 0x00-0x34 are initialised data in the image. Entry 0x4a, for example, is
  all zero in the file [X]. The rest is written by the C++ static constructor `WeaponDataTableInit` 0xf5530
  (44,880 bytes of stores) before `Game_Main` [S].
  - It is ours now: `WeaponDataTableInit` (`src/action/game/weapon_stats.cpp`) writes the whole table from
    `WeaponTable.inc`, which `tools/weapon_table.py` generates from the XBE. Every field is named and described in
    `docs/weapons.md`, which supersedes the field notes below.
  - Why the compiler emitted code for the later entries is not known. It may be non-constant initialisers such as
    function pointers or float expressions [I].
- **The table is mutable global state.** `Player_Init` rewrites two fields per level [D]. Every original function
  indexes it by address, so it has to stay at 0x18cfa0 while any of them is live.
- Fields whose role was settled for this document [D]:
  - `someFlags` (+0x68) bits:
    - 0x2: fires when not surfaced;
    - 0x10: when empty, switch to the alt-fire variant; picked-up ammo goes straight into an empty clip
      (`docs/weapons.md`);
    - 0x40: a real scope;
    - 0x100: shell-by-shell reload;
    - 0x200: alternating reload animation;
    - 0x400: continuous fire;
    - 0x800: suppressor model;
    - 0x1000: reload starts at frame 6;
    - 0x8000: the scope may stay up in state 9;
    - 0x10000: the bullet starts between the eye and the muzzle;
    - 0x20000: detonate the player's live projectiles of this base;
    - 0x100000: re-trigger the fire animation while held.
  - `flagsForSwooshAndCasing` (+0x6c) bits: 0x40 ejects a casing (SP only), 0x400 is a throw-style two-stage fire
    (states 11 and 12), 0x800 changes the collision mask.
  - The word our header calls `unk17` is the **frame of the fire animation at which the bullet spawns**: 0 means
    at once.
  - The shorts from +0x28 are read as an **array indexed by `WeaponStatus.fireModeIndex`**: the shots per trigger
    pull for each fire mode. The header names them as four unrelated fields, which is likely wrong [I, strong].
  - `cooldownTimerIncreaseAmount` is the rounds or charge used per shot.
  - `rumble`, `cameraSwingAmt` and `maxZoom` do what their names say.
  - **+0x104 is a function pointer**, called by `Player_WeaponInitBullet` after firing when `ammoType == 0`
    (gadgets). Nothing ever sets it (nor the second callback at +0x108): both are null for the whole game
    (`docs/weapons.md`).
- Player-side weapon state lives in `BLData.weaponStats[114]` and `ammo[34]`. There are 114 slots for 115 table
  entries, so entry 114 has no slot. That is harmless only if variant 114 is never selected by a player [I].

### The weapon state machine

There is no separate weapon object type logic. The state is **`BLData.weaponObject->curState`**, an `obj_tag`
field reused as a weapon state. It is advanced by `Player_SetWeaponAnimObj` 0xbcaa0, which is called only from
`Player_WeaponRecoil` 0xbd740.

`Player_WeaponRecoil` is not recoil: it computes a sinusoidal **weapon bob or sway** offset, tilts the gun
towards the crosshair, then calls `Player_SetWeaponAnimObj(p, &bob)`. That runs the state machine and places the
weapon object at the view-model offset plus the bob [D]. The state numbers are unnamed. Meanings inferred from the
transitions [D][I]:

| state | meaning | leaves to |
|---|---|---|
| 0 | idle/ready; idle fidget animations after 20 s untouched, or while no drone is alert (+0x8e9) | 1/13 on a weapon switch, 5/6 reload, 9/11 fire, 15/16 scope |
| 1, 13 | start lowering the old weapon (13 = one-shot gadget, disabled afterwards in SP) | 2, 14 |
| 2, 14 | lowering; at the end, current weapon = switching-to (bots get message 0x44) | 3 |
| 3 | `Player_SetWeaponAnim`, start raising | 4, or 0 with no raise animation |
| 4 | raising (a new switch request reverses it) | 0 |
| 5 | pre-reload | 6 |
| 6 | reload; `Player_ReloadAmmoType`; shell-by-shell loops here | 0 or 7 |
| 7 | reload end | 0 |
| 8 | post-fire / lower-from-continuous | 0 |
| 9 | firing; the bullet spawns at frame `unk17`; continuous weapons stay while fire is held and ammo lasts | 0 or 8 |
| 10 | abort raise | 2 or 0 |
| 11, 12 | throw-style fire: wind-up, then release with a delayed bullet; then re-raise or out of ammo | 3 or 0 |
| 15, 16 | scope bring-to-eye / lower (15 sets `animFlags` bit 0) | 0 |

`Player_WeaponFiring` 0xbb300 (3.8 KB) runs after `Player_Weapon` each frame [D]. It:

- updates per-weapon display models (satchel and decoder digit datums, the Q-worm display, ammo-count datums on
  certain weapons, the camera's light) and first-use hints;
- gets the aim point (`Player_GetAimingPoint`);
- handles reload (`ACTION_RELOAD`, after `Player_Activate` has had a chance to use the press);
- handles fire, gated by per-weapon conditions: the grapple (0x50/0x51) needs a grapple point or MP, the Q-worm
  (0x58) and the decoder (0x56/0x57) need a valid target within `someDistance`, the camera (0x54) needs the scope;
- consumes rounds through `Player_RoundToFire`, starts the fire animation (`Player_SetFiringAnim`), sets the refire
  countdown from a per-weapon delay, and spawns the bullet with `Player_WeaponInitBullet` at once or at frame
  `unk17` from the state machine;
- reloads automatically or calls `Player_HandleHasNoAmmo` when the gun is empty;
- in remote control, makes fire detonate the controlled device.

`Player_WeaponInitBullet` 0xbae50 [D]:

- casts a ray from the head to the aim point (`Collide_RayIntersect`) to find the real target point;
- special-cases weapons 0x4a/0x4c (a short-range beam: a near miss refunds the charge), 0x52 (deploys a mini-gun
  turret, `GT_DeployMiniGun`) and 0x53 (takes control of a turret);
- then calls `Bullet_init` once per `numBulletsPerShot`, logs `PlrStat_LogShotFired`, ejects a casing, and calls
  the +0x104 callback.

### Bullets

`Bullet_init` 0x211f0 creates an `OBJECTTYPE_BULLET` object (`BU_tag`, 0xe0 bytes; only +0x24 shooter, +0x38
weapon definition and +0xcc age are named [S]).

- It is the **single projectile path for the whole game**. Its 17 callers include `DroneWeap_FireWeapon`,
  `DroneWeap_ThrowGrenade`, `GT_Update`, `GunImp_Update`, `Car_Update`, `Copter_Fire*`, `Sub_*`, `Shooter_Update`,
  `SpaceLaser_Update` and `AnimProcessScriptCmds` [X].
- Bullets are moving objects, not hitscan: `Bullet_Update` ray- and sphere-casts each frame, does homing
  (`Bullet_homing`, `Bullet_acquiretarget`), trails and beams, and light.
- Damage is applied by the victim's collide handler reading its hit list:
  - the player: `Player_DealWithObjHit` → `Player_HandlePain`;
  - drones: `NDrone2_DoHitEffects`;
  - in MP, `MP_RegisterBulletHit` is called from those (and from `Explode_Propagate`).

### Death and respawn

`Player_CheckForDeath` 0xac390 [D] does nothing in states 2-3. Otherwise it clamps health to 0..500 (unless the
`Immortal` cheat is on). When health reaches 0:

- it tells cars, gun emplacements and turrets the player died;
- SP: sets switch channel **0x5e** and a music event;
- MP: drops the held weapon as a pickup (`Pickup_CreateSimple`);
- sets `objectType` to DEAD_PLAYER, `curState` to 2, and the death substate;
- shows the MP kill message, plays `SFX_CHR_BOND_DIE`, and calls `MP_PlayerKilled`.

`Player_HandleDeath` 0xac690 [D]:

- SP: shows the blood pane, then sets channel **0x62** (the mission-failed flow) once it has finished or been
  skipped;
- MP: calls `MP_ReSpawn` after 5 seconds, except in Top Agent once the player is out.

### Mission statistics (PlrStat)

`PlrStat_*` (0xb04a0-0xb1a30, `src/action/game/sp/PlayerStats.cpp`) count shots, hits, kills, alerts, health and
time for the single-player score and medals.

- 16 of 30 functions are ours, with `ScoreShadow` comparing `PlrStat_GetScore`.
- Still live: the elapsed-time update, shot logging (`LogShotFired`, `LogShotHitEnemy`, `LogShotHitScenery`), Bond
  moments, the reward counter and the score-table setup.

### HUD (only where it reads the player)

`HUD_Update` (ours) is called from `Player_Update` and `Player_Init`, with the BLData. The panes read BLData
directly:

- health and armour, `hudFadeIn`, `hitDirections`, `bondMomentTimer`;
- ammo: `weaponStats` and `ammo` (`HUD_UpdateAmmoPane`, live, 2 KB);
- the third-person icon (+0x8ef), night vision, zoom;
- in MP, `MPGame` (`HUD_MPUpdatePane`, `HUD_RadarUpdate`, both live).

`HUD_UpdateCrossHair` and `HUD_MonitorNightSight` are dead on Xbox [X]. Any BLData layout change touches HUD.cpp.

## Multiplayer

### Data

| global | address | size | notes |
|---|---|---|---|
| `MPSettings` | 0x25fe38 | 572 | the menu's choices: `Player[NUM_AGENTS]` (0x30 each: name, team, skin, health modifier), mode, limits, weapon set, modifiers [S] |
| `MPGame` | 0x262738 | 0x230 | `players[NUM_AGENTS]` (0x30 each), team scores, end-game flow state, timers, radar sprites [S] |
| `SpawnPoints` | 0x261d58 | 64 x 0x1c | per-team counts at 0x262968 [S] |
| `MPObjects` | 0x263640 | 64 pointers | scenario objects (`MPOBJECT` 0x50 bytes in `extraObjectData`) [S] |
| `MPpickups` | 0x260078 | 64 x 0x58 | weapon pickups with AI emitters for bots [S] |
| `GoldenEyeStruct`, flags, bases, uplinks, demolition, protection, blueprint and hill tables | 0x261790-0x2637d4 | | cleared by `MP_Init` [S] |
| `mpbots` | 0x245280 | 0x6e | the bot set-up page [S] |

Slots: 0-3 are humans and 4-9 bots, in `MPSettings.Player[]` and `MPGame.players[]`; there are 8 on PS2. The counts
are `NUM_PLAYERS` (4), `NUM_BOTS` (6) and `NUM_AGENTS` (10) in `game/mp/MPLimits.h` [S]; see "The bot limit" below.

Game modes are bit sets (`MultiplayerGameMode`): bit 29 = team game, bit 30 = KOTH, team KOTH and uplink.
`MP_Init` copies both bits into `MPSettings` (+0x1ec, +0x1f0) [S].

### Flow

- `MP_Update` (ours) runs once per frame from `Game_Run`. It counts down the friendly-fire labels, then steps
  `MPGame.EndGameFlowState` [S]:
  - 0: `MP_Pickup_Process`, `MP_CheckForEndCondition`;
  - 1: `MP_SortOutWhoWon`;
  - 2: "time up";
  - 3: a 5 s winner display;
  - 4: queue the debriefing and reload the menu;
  - 6: restart the scenario after 5 s (demolition and protection rounds).
- **End conditions are switch channels** [D]. `MP_CheckForEndCondition` 0x9cc50 sets channel **0xfd** when the
  best score (team or individual) reaches `MaxPoints`, and **0xfe** when `TimeUnpaused >= TimeLimit`. Demolition
  and protection go to state 6 on time-out instead. Top Agent ends when all but one participant are out. A debug
  switch, `switch_MP4EVER`, disables all of it. Level scripts can therefore also end a match by setting those
  channels [I].
- Scenario objects: `MP_RegisterMPObject` (placement) → `MP_CreateObject` → type 53 objects. Their update
  `MP_ObjectUpdate` [D] dispatches on `MPOBJECT.type` to:
  - `MP_FlagUpdate` (CTF);
  - `MP_UplinkUpdate`;
  - `MP_DemolitionProtectionUpdate` (two modes);
  - `MP_BluePrintUpdate`;
  - `MP_GoldenEyeUpdate` (keys and crystal; 2.9 KB, the largest; it calls `Player_Kill`);
  - `MP_KOHUpdate`.
  
  An object script on the object is moved with it. The carried-object updates call `MP_HitBy` and
  `MP_SetUpPlayerSomehow`.
- **Scoring** happens in `MP_PlayerKilled` 0xa1fc0, called from `Player_CheckForDeath` and the two bot death
  states [D][X]:
  - the victim's death count goes up (the field Ghidra calls `pointsScored` is the debriefing's Deaths);
  - the victim's hit list (`Collide_GetDamageNObjects`) is walked back through bullets, cars and turrets to the
    owner;
  - the killer's kills go up, unless it was a team kill;
  - a suicide takes one off;
  - every drone is sent message 0x43/0xc5;
  - assassin mode adds bonuses; team arena adjusts `teamScore`;
  - a carried flag, blueprint or GoldenEye key is dropped.
  
  Points (`MPGame.players[].points`) change on kills only when MPSettings+0x1f0 (mode bit 30) is clear *and* the
  mode is Arena, Team Arena, KOTH or Team KOTH. Bit 30 is always set for KOTH and Team KOTH, so the KOTH half of
  that test can never pass, and kills score points only in Arena and Team Arena [I, from the bit definitions].
  A kill is worth 2 instead of 1 when the killer is a bot in a particular state whose target was the victim; the
  fields involved (BOT_vars +0x83 == 7, +0x75f) are not decoded.
- Respawn: `MP_ReSpawn` 0x9f0b0 is called by `Player_HandleDeath`, `BOT_respawn` and `MP_RestartScenario`.
  `MP_GetSpawnPoint` chooses the point (`RespawnSelectionMode` is in the settings; the choice logic was not read).
  `MP_EquipPlayer` gives the weapon set's loadout and is also called from `Player_InitWeapon`.

### Where bots plug in

See [../drone/bots-and-navigation/](../drone/bots-and-navigation/README.md).

- `MP_Start` → `BOT_init(NUM_PLAYERS + i, spawn, NULL, &mpbots.bot[i], 0)`.
- Bots are drone objects (type 2 / 17), so `Control_Plr2Ind` and `MP_PlayerOrBotInd` map both kinds to an
  `MPGame` slot.
- Bots use their own weapon code (`BOTWEAP_*`, `DroneWeap_FireWeapon`) on the same `weapon_data` and `Bullet_init`.
- They learn about the match through `MP_sendBotMessage` and `MP_sendTeamBotMessage` (for example the player's
  weapon switch, message 0x44, from the state machine above).
- They find goals through the AI emitters on `MP_PICKUP` and `MP_OBJ_EXT`, built only when bots are active.
- `MP_ResetBotPickupTimes` and `MP_getMPpickup` serve their goal picking.

### The bot limit

The game allows 6 bots: `MP_Start` clamps `NumBots` to 6, `Menu_PrepareBots` walks 6 `MPBOT`s and the bot wheel
shows 6 [S]. The menu text has "Setup Bot 1" to "Setup Bot 16" (`mp_bots[17]`), and the PS2 build has 8 agents.

Every function that bounded a loop or an array by the bot count (6), the agent count (10) or the first bot slot (4)
is ours (October 2026), written against `NUM_PLAYERS`, `NUM_BOTS` and `NUM_AGENTS` (`game/mp/MPLimits.h`). A
program-wide scan for the bounds (0x262814, 0x262930-0x26293e, 0x260038, 0x1d98e0) finds no original left [X].
The records sized by the constants keep their size asserts, so changing a constant shows each layout that moves.
What raising `NUM_BOTS` still needs:

- **Records at the game's addresses, packed against their neighbours.** Each needs its last original referrer
  ported, then a definition of our own:
  - `MPSettings` (`Player[NUM_AGENTS]`, then `isMultiplayer`): 31 live originals reach `Player[]` by agent and
    another 142 the scalars (`tools/global_coverage.py MPSettings`). `Menu_StoreMPSettings` copies all 0x23c bytes
    to 0x2245f8, and the `PlayerInputs` backup follows at 0x224838.
  - `MPGame` (`players[NUM_AGENTS]`, then `teamScore`): live originals still index `players[]` (`HUD_MPUpdatePane`
    and `FUN_00043070` read `playerObj`; `global_coverage.py` does not parse `MPGame`'s define, so the full list
    is not taken).
  - `BOT_vars` (`NUM_BOTS` x 0x768 at 0x1d98e0; shard and cel tables follow at 0x1dc564): only `BOT_init` (ours)
    names it, the rest go through `Drone_tag.botVars`, so it can be ours now. Its entries hold
    `players[NUM_AGENTS]`, which moves every later field.
  - `mpbots` (0x6e at 0x245280): `mp_stuff` follows at 0x245338, room for 4 more `MPBOT`s; 6 live originals read
    its count.
  - `MP_PICKUP.maybeBotPickupVisitTimes[NUM_BOTS]`: 4 bytes spare in the 0x58-byte pickup; 4 live originals
    (`MP_RegisterPickup`, `MP_UnregisterPickup`, `MP_getMPpickup`, `MP_PostLoad_Init`) step the 64 pickups by that
    stride.
  - `PlrMissionStats[NUM_AGENTS]` at 0x278e70.
- **Fixed capacities elsewhere:**
  - `MPRadarObjects` (0x263220) has 22 entries before `Uplinks`; `MP_GetRadarObjects` writes up to
    `NUM_AGENTS` + 8.
  - The radar pane has 28 extra items, shared by blips and name tags (`HUD_RadarUpdate` does not bound the tags).
  - `NDrone2_SetOpponent` → `DroneFunc_AllocateTargetID` hands out 8 target slots.
  - The delayed-message pool has 1024 nodes.
  - There are 32 spawn points per team.
  - The 0xff "none" in `BOT_vars_t.preferredOpponent` is read signed: agent indices below 128.
- **Menu data and its handlers:**
  - The pause menu's score list has 10 rows (team totals on row 9).
  - The debriefing shows 4 rows (its score table is `NUM_AGENTS` long).
  - The confirm page hides 10 bot rows per side.
  - The bot wheel has 16 bot items.
  - `mp_good_bot_taken` / `mp_bond_bot_taken` encode a bot as index + 10 and a player as index + 1, in a byte.

## Connections to other subsystems

- **Input**: `Input_Action(playerNum, ACTION_*)` everywhere (engine.input, mostly ours). Mouse look enters only at
  `Player_ViewClamping` (ours), player 0 only.
- **Animation** (engine.anim, 5% ours): the player body and the weapon view-model are both anim objects. Weapon
  states advance on `AnimScriptIsStopped` and `curFrame` (`AnimListBuild(obj, 4, ...)`). Datum entity swaps
  (`AnimDatumSetEntity`) drive the digit displays and suppressors. **The weapon state machine cannot be reproduced
  without the anim-script timing being exact.**
- **Camera** (engine.camera): `Player_PositionCamera` is a camera updater callback, `Camera_ApplySwing`,
  `Player_SetCamMode` (ours).
- **Collision** (engine.collision): `Collide_RayIntersect` / `SphereIntersect`, hit lists, `Player_Collision`.
- **Switch channels**: player death 0x5e, mission fail 0x62, MP limits 0xfd / 0xfe, Tower 2B fall 0x7c, and the
  start positions' key 0. Mission scripts react to these.
- **Objects**: cars, gun turrets, GunImps and monitors call `Player_Disable` / `Player_Enable` and take over the
  controls (remote control substate). Pickups call `Player_EquipWeapon` / `Player_EquipAmmo`.
- **Drones**: the drone vision and alert code reads the player object (shadow, `Player_InShadow` visibility).
  `MP_PlayerKilled` routes messages into the drone state machine. Bullets damage both sides.
- **Level flow**: `Player_RamSave` / `Player_RamLoad` carry health, armour, weapons and ammo between the parts of a
  mission. `ReadTuningVars` (ours) sets autoaim and damage per level and difficulty.

## Well understood

- The per-frame order of `Player_Update` and `Player_CollisionHandler`, and the object-type handler wiring [D][X].
- Creation (`Player_Init`, `MP_Start`), the slot layout (4 humans, 6 bots: `MPLimits.h`), and BLData's size [D].
- The weapon state machine's transitions, and where bullets are spawned [D]. The weapon-switch and reload logic in
  `Player_Weapon` is ours [S].
- The MP end-game flow, end-condition channels, kill scoring and respawn trigger [D][S].
- Ammo storage, upgrades (shadow-tested) and the per-level loadout (`Player_InitWeapon`, ours) [S].

## Unknown or uncertain (risks)

1. **BLData is two thirds unmapped.** About 60 named fields out of 0x8fc bytes; +0x8da is misnamed and +0x8db is
   missing. Every hub (`Player_Update`, `CollisionHandler`, `WeaponFiring`, `SetWeaponAnimObj`, the special
   movement modes) touches unnamed fields, and the HUD shares the struct. A layout audit (writers and readers per
   field, as done for `Drone_tag`) is the main prerequisite.
2. **`weapon_definition_tag` is half guessed.** Many `unk`/`maybe` fields. At least two are probably misdescribed:
   +0x28 is a per-fire-mode array, and the fire-delay field Ghidra calls `someDurationRelatedToFiring` sits near
   `unk16`/`unk17`. The +0x104 callback targets are unknown. Several weapon ids are special-cased by number in code
   (0x11, 0x1a, 0x33, 0x3b, 0x45, 0x4a-0x4f, 0x50-0x58); the full list has not been collected.
3. **`WeaponDataTableInit` has not been decoded.** It is unclear which entries or fields it writes, whether it
   only fills entries 0x35+, and why it is code. Replacing it with a data table needs a dump of the table after the
   constructor has run (from a running game), checked against the spreadsheet. The current hook depends on
   injection order.
4. **The weapon state numbers are unnamed** and two of them (8, 10) are only partly understood. Weapon object
   `subState` is reused as a counter for satchel and decoder displays.
5. **Movement substates 11-14** (Ghidra's MAYBE_FROZEN_*, and the death substates 13/14) are unclear. The ZeroG
   and Ronin modes have little coverage in levels.
6. **Bullets** are shared by all shooters and `BU_tag` is almost all unnamed (0xe0 bytes). Their homing, trails,
   casings and damage hand-off have not been read in detail. Changing them affects drones, vehicles and turrets.
7. **Damage path**: `Player_DealWithObjHit` (1.2 KB) and `Player_HandlePain` decide damage by body part,
   difficulty, armour and bot trait flags (bits partly decoded in the bots doc). The exact formula has not been
   read.
8. **MP scenario updates** (flag, uplink, demolition/protection, blueprint, GoldenEye, KOH; about 8 KB) have not
   been read beyond their dispatch. `MP_GetSpawnPoint`'s selection logic, `MP_SortOutWhoWon` tie-breaking and the
   `MPGamePlayer` fields at +0x0, +0x4, +0x10 are uncertain. Ghidra's names (`pointsScored` = deaths, `field4` =
   kills, `field10` = a streak?) disagree with ours. The unnamed `MPGame.unknown_*` fields are guesses.
9. **Frame-rate coupling**: weapon recharge steps on `NumFramesUnpaused` modulo 2/4/6, refire and burst counters
   count frames, and fall damage counts frames airborne. A fixed-step loop is assumed. Any change to the clock
   changes gameplay.
10. **The camera** (`Player_PositionCamera`, 1.8 KB) and aiming (`Player_Aiming`, `Check_AutoAim`, `Check_Target`,
    `AccelFunc0`) are live and have not been read here. `AccelFunc0` is caller-cleans despite Ghidra's `__stdcall`
    [S], a trap for anyone calling it.
11. Bot kill bonus fields, the MPOBJECT fields beyond type, num and script player, and `MP_SetUpPlayerSomehow` /
    `FUN_0009daae` (882 bytes, in the MP range, unnamed) are unknown.

## Suggested reimplementation order

1. **Data first, no code paths.** Audit BLData (readers and writers per offset, fix +0x8da/+0x8db). Name the
   weapon states as an enum. Correct `weapon_definition_tag` from the WeaponData spreadsheet without renaming
   weapons. Dump the post-constructor `weapon_data` from a running game. This de-risks everything below and
   changes no behaviour.
2. **Weapon leaves**: `Player_RoundToFire`, `Player_ReloadAmmoType`, `Player_WeaponHasAmmo`,
   `Player_IsBetterWeapon`, `Player_GetBestWeapon`, `Player_EquipWeapon`, `Player_EquipKey`, `Set_Upgrade`,
   `ResetUpgrade`, `Player_HandleHasNoAmmo`, `Player_ScopeOff`, `Player_ResetZoom`. They are small, mostly pure on
   BLData and `weapon_data`, and can be shadow-tested the way upgrades are.
3. **Finish PlrStat** (12 live, small): it completes a subsystem and the score shadow test already exists.
4. **MP scoring and flow**: `MP_CheckForEndCondition`, `MP_SortOutWhoWon`, `MP_PlayerKilled`, `MP_GetSpawnPoint`,
   `MP_ReSpawn`, `MP_EquipPlayer`, `MP_Pickup_Process`, `MP_Start`, `MP_PostLoad_Init`. Their caller `MP_Update` is
   already ours, the state is in mapped globals, and they are independent of animation. Then one scenario update
   per game mode (KOH and flag first, GoldenEye last). Only the replays that reach each mode need running.
5. **The weapon state machine as one unit**: `Player_SetWeaponAnimObj`, `Player_WeaponRecoil`,
   `Player_WeaponFiring`, `Player_WeaponInitBullet`, `Player_SetFiringAnim`, `Player_SetWeaponAnim`,
   `Player_WeaponChange`, `Player_WeaponSelect`. They share the weapon object's state and the BLData counters
   (+0x8bc, +0x8c6, +0x8e9, +0x8eb, +0x8ec), so replacing one at a time means keeping hand-written layout
   agreements. Do it after step 1.
6. **Ordinary movement**: `Player_SSWalk`, `Player_SSCrouch`, `Player_HandleJump`, `Player_MonitorAir`,
   `Player_Collision`, `Player_ClampSomeAngles`, `Player_Aiming` (`Player_Move` is already ours). Then the special
   modes one at a time with the levels that use them: ladder, wire, zipline, grapple, creep, swim, zero-G.
7. **Death, pain and RamSave/RamLoad**, with the damage formula written down first.
8. **The hubs last**: `Player_Update`, `Player_CollisionHandler`, `Player_Init`, once their callees are ours. They
   are mostly ordering, and the ordering is documented above.
9. **Bullets separately**, coordinated with the drone work, since every shooter shares them.
10. **The camera** (`Player_PositionCamera`) alongside engine.camera.

Each reimplemented function should get the GameCube checks (`docs/gamecube-checks.md`, NF_WARN / NF_ASSERT).
