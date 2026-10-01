# The weapon definition table

`weapon_data` (0x0018cfa0, `weapon_definition_tag[115]`, 0x10c bytes per entry) holds every weapon, gadget and
weapon-like attack in the action engine: the player's guns and gadgets, their alt-fire variants and upgrades, the
fists, the drones' close-combat attacks, the vehicle and turret guns, and a handful of empty slots. Entry *n* is
weapon variant *n* (`weaponVariantNum == n` for every entry). This document gives every byte of an entry a
meaning, a type and the code that reads it. The struct in `src/action/game.h` follows it (the "Proposed struct"
below, now applied), and the table's contents are generated from the XBE by `tools/weapon_table.py` into
`src/action/game/WeaponTable.inc`, every field by name, which our `WeaponDataTableInit`
(`src/action/game/weapon_stats.cpp`) writes at start-up in place of the original constructor. The table stays at
0x0018cfa0 while original code still reads it there.

Weapon ids and names follow `WeaponBaseNum` in `src/action/game.h` and the user's PS2 WeaponData spreadsheet; this
document only refers to entries by number and the spreadsheet's names.

Evidence marks used in the tables:

- **V** - verified: read in a decompile or the disassembly of the function cited (Xbox `default.xbe` addresses).
- **I** - inferred from the data (which weapons have a value) or from naming, without a reader that settles it.
- **U** - unknown: no reader found.

Functions marked "(invented name)" are unnamed in Ghidra (`FUN_xxxxxxxx`); the name is only a label for this
document.

## How the table is built

- **Entries 0..0x34 are initialised data** in the XBE image. **Entries 0x35..0x72 are written by the C++ static
  constructor `WeaponDataTableInit`** (0x000f5530, about 45 KB of `MOV [abs], imm` stores), run before `Game_Main`.
  `tools/weapon_table.py` rebuilds the whole table by emulating it (checked byte for byte against the running game's
  table), and `devtools/WeaponTableShadow.cpp` compares ours with the original's at start-up (MenuShadowTests=on).
- **Why the split falls at 0x35:** the constructor computes 12 stores from the frame-rate globals instead of
  storing constants, and the first of them is in entry 0x35 (53). MSVC emits the leading part of an aggregate
  initialiser as static data and everything from the first non-constant member onwards as code. The computed
  stores are:
  - `+0x07a` (an effect duration in frames, see below) = `FRAME_RATE_INT` (word at 0x17c0f4, 60) × 15 for entries
    53, 54, 89, 91 and 105, and × 60 for entry 65: 15 s and 60 s written as seconds × frame rate.
  - `+0x020` (projectile speed) = `_FRAME_RATE` (float at 0x17c0f8, 60.0) × k / 60 for entries 75, 77
    (k = 0.4375), 99-102 (k = 2.5 and 1.5): speeds written relative to the 60 Hz frame.
- The constructor's stores also show the real member sizes: it writes `+0x2e`, `+0x78` and `+0x84` as single
  bytes, `+0x44` as a word, and never writes the bytes this document lists as padding. It writes `+0x54..+0x57`
  and `+0x60..+0x63` as one dword each (constant merging; see those fields).
- **The table is mutable.** Three writers change it at run time (absolute-address xrefs, every entry, every field):
  - `Player_Init` (0x000acab0) per level: entry 1 (the fists) `weaponModelHashcode` = the level's hands/sleeve
    model (`PunchSkin_204[]` in single player, 0x050000b0 in multiplayer), and entry 55 (Remote Mine)
    `offsetToNextAltFireVariant` = 1 in single player (alt fire = entry 56, Detonate) or 2 in multiplayer
    (entry 57, the multiplayer Detonate). This is why entry 1's model hashcode differs from the PS2 dump.
  - `Upgrade_Weapon` (0x000b9bf0): entries 84 and 85 (Micro-Camera) `maxZoom` = 8 or 16 by the camera upgrade
    (`src/action/game/Upgrade.cpp`).
  - Nothing else writes it. In particular **nothing writes `+0x104` or `+0x108`**: both callbacks are NULL in
    every entry for the whole game (see the field table).
- Known PS2 differences: `+0x040` (refire delay) is one frame less on Xbox for entries 2-12, 17, 65, 66, 69; the
  Xbox countdown fires when it reaches `<= 0` after being set to the delay, so the change may compensate a
  different comparison on PS2 [I]. Entry 1's model is the per-level value above. `+0x005` is signed.

## Field table

Offsets are within one entry. "Proposed name" is a C identifier; a name in **bold** is a rename of the current
header name (the old name follows in brackets). Units: "frames" are 60 Hz frames (the code scales by
`FRAME_RATE_MUL`/`FRAME_RATE_DIV` where the real rate differs); "units" are world units (about a metre: the fists
reach 1.75, the sniper rifles 5000).

| off | size | proposed name | type | meaning, units | evidence | conf |
|---|---|---|---|---|---|---|
| 0x000 | 2 | weaponVariantNum | short | this entry's index | many `switch (wpnDef->weaponVariantNum)`: `Bullet_Update` 0x23670, `NDrone2_DealWithObjHit` 0x364f0, `DroneFunc_RecoverTime` 0x3a4d0, `Explode_Propagate` 0x69390 | V |
| 0x002 | 2 | weaponBaseNum | **short** (was `char` + pad) | variant of the group's base weapon; indexes `BLData.weaponStats` and `WeaponStatus` per base | read as a word in `Player_AmmoIndex` 0xb6ca0, `Player_SetFiringAnim` 0xb7cf0, `MP_EquipPlayer` 0x9d5b0 and ~30 more; byte 3 is never non-zero | V |
| 0x004 | 1 | isBaseWeapon | bool | the variant that owns the group's inventory slot | `Player_InitAmmoWeapons` 0xb6d30 (only base slots can be enabled), `Player_WeaponChange` 0xba280 (cycles base weapons), `Player_CheckWeaponsLoaded` 0xb9b40 / `BOTWEAP_CheckWeaponsLoaded` 0x1e920 (load base weapons' models) | V |
| 0x005 | 1 | offsetToNextAltFireVariant | **signed char** (was `uchar`) | entry offset to the alt-fire variant; the alt variant points back (-1, -2, -5) | `Player_Weapon` 0xba8f0 (ours), `Player_HandleHasNoAmmo` 0xbad40 (`MOVSX`), `Player_SetFiringAnim`, the `BOTWEAP_*` ammo functions; written by `Player_Init` | V |
| 0x006 | 1 | **botWeaponClass** (maybeFlags) | uchar | bot weapon category: 1 handgun, 2 automatic, 3 sniper rifle, 4 explosive, 6 AIMS-20, 0 other. Compared with `==`, not tested as bits | `BOTSTATE_isPreferredWeapon` 0x1c0d0 (preference 1 → class 1, 2 → 2, 4 → 3, 5 → 4; preference 3 is a fixed list of silenced variants 3, 7, 20, 36, 37), `BOTSTATE_pickGoal` 0x1cb20, `BOTSTATE_combatWeaponChangeChoice` 0x1d750, `BOTWEAP_tooCloseForWeapon` 0x1f740, `NDrone2_DSTATE_BotGlobal` 0x603e0 (`== 4`) | V (3 and 6 look like unions of 1/2 and 2/4: I) |
| 0x007 | 1 | _pad07 | | never stored | constructor | V |
| 0x008 | 4 | **explodeRadius** (maybeExplodeRange) | float | explosion size in units, passed as both `scale` and `maybeRange` of `Explode_Create`; bots keep this far from targets | `Bullet_CollisionHandler` 0x21a00, `Bullet_handle_object_destruction` 0x218f0, `Bullet_DoTrails` 0x22210 (fuse), `BOTWEAP_tooCloseForWeapon`, `BOTSTATE_combatWeaponChangeChoice`, `FUN_0001f450` (bot grenade choice, entries 42-51) | V |
| 0x00c | 4 | damage | float | damage per hit (copied to the bullet, `BU_tag+0xb8`) and explosion strength; overridden to 1.0 or 0.0 per hit for launcher/grenade variants whose damage comes from the explosion | `Bullet_Update` 0x23670, `Explode_Create` calls above, `Bullet_CollisionHandler` (`> 0`: a hit projectile detonates), `NDrone2_BodyHitEffect` 0x36430, `NDrone2_DefaultInit` 0x417f0 (a drone's health = entry 51's damage) | V |
| 0x010 | 2 | **damageClass** (unnamed) | ushort (bits) | weapon class bits, one per weapon family (table below); objects filter hits by class | `Collide_FilterBullets` 0x2b3b0 (`class & mask` → hit ignored), masks from `Destroy_CollisionHandler` 0x20310, `Break_Update` 0x20460, `Copter_Update` 0x6bfb0, `SP_Hit` 0xc4100, `SP_GetHitDamage` 0xc4370, `FuseBox_Update` 0xd0060; `Player_DealWithObjHit` 0xadd00 (bit 1 = punch); `Player_WeaponInitBullet` 0xbae50 (`& 0xf8` → `PlrStat_LogShotFired`) | V |
| 0x012 | 2 | _pad12 | | never stored | constructor | V |
| 0x014 | 4 | **autoAimStrength** (autoaimRelated) | float | auto-aim strength in percent (0-100; 150 for some) | `Check_AutoAim` 0xb9390: `× 0.01 × (1 - dist / Autoaim_Range)` × difficulty multiplier × `Autoaim_Angle_H/V` | V |
| 0x018 | 1 | numBulletsPerShot | uchar | projectiles per shot (8 for the Auto 12; 0 = fire nothing, the detonators and activators) | loops in `Player_WeaponInitBullet`, `DroneWeap_FireWeapon` 0x677f0 | V |
| 0x019 | 3 | _pad19 | | never stored | constructor | V |
| 0x01c | 4 | **range** (someDistance) | float | maximum distance a projectile travels, in units; also the use range of gadgets | `Bullet_Update` (distance travelled `> range` → bullet ends); `Player_AutoAim` 0xb9950 (auto-aim search radius); `Player_WeaponFiring` 0xbb300 (Q-Worm, Decryptor target range); `Player_WeaponInitBullet` (Stunner range); `Player_SetupTaser` 0xb8400 (entry 76's range clamps the beam); `HUD_UpdateAmmoPane` 0xb1f90 (entry 80's range for the grapple hint) | V |
| 0x020 | 4 | **projectileSpeed** (unknownPurposeMaybeFloat; Ghidra: bulletTrailTimeRelated) | float | muzzle speed in units per 60 Hz frame (16.67 = 1000 units/s); for beams also the drawn length | `Bullet_init` 0x211f0 → `BU_tag+0xd0` (a quarter of it with projectile flag 0x200); `Bullet_Update` moves `speed × FRAME_RATE_MUL` per frame and adds it to the distance travelled; `Bullet_DoTrails` (rockets accelerate by 2% of it per frame up to it); beam scale with projectile flag 0x100; computed for entries 75, 77, 99-102 | V |
| 0x024 | 4 | **baseSpread** (maybeAccuracyRelated) | float | base inaccuracy; the shot cone half-angle is a random value between `s` and `2s` times 0.0014 rad, where `s = (baseSpread + bloom) × shooter accuracy` | `Bullet_init` | V |
| 0x028 | 4 | **shotsPerTrigger[2]** (indexIntoSomePlayerAmmoArray, maybeUnused) | short[2] | shots per trigger pull for each fire mode, indexed by `WeaponStatus.fireModeIndex` (999 = automatic, 3 = three-round burst) | `Player_WeaponFiring` loads `shotsPerTrigger[mode]` into the burst counter `BLData+0x8c6` and fires while it stays `>= 0`; `Bullet_init` uses `shotsPerTrigger[mode] - counter` (shots so far) for bloom | V for [0]; [1] is only reachable for entries 96, 97 (two fire modes): I |
| 0x02c | 2 | **droneBurstLength** (droneBulletBurstTimeRelated) | short | shots per burst when a drone fires it, before the aggression multiplier (handguns 6, 10, 14-16 get random 2-4 / 1-2 in single player) | `FUN_00066f10` (DroneWeap_GetBurstLength, invented name) from `DroneWeap_NextBulletTime` 0x67400 | V |
| 0x02e | 1 | **numFireModes** (unk15) | **char** (was `short`) | number of fire modes the alt-fire button cycles when there is no alt-fire variant (2 for entries 96, 97; 1 elsewhere) | `Player_Weapon` (ours, byte compare); constructor writes a byte | V |
| 0x02f | 1 | _pad2f | | never stored | constructor | V |
| 0x030 | 8 | **fireModeName[2]** (fireModePrimary, fireModeSecondary) | Action_TranslatedText[2] | HUD fire-mode text per fire mode (bank 5; 0x02000004 = " "; TXT_NULL shows "~-~") | `HUD_UpdateAmmoPane`: `(&fireModePrimary)[fireModeIndex]` | V |
| 0x038 | 4 | **nameLong** (weaponNameLongSp) | Action_TranslatedText | full name, shown in single player ("Wolfram P2K") | `HUD_UpdateAmmoPane`, `Pickup_Handler` 0xa7710 | V |
| 0x03c | 4 | **nameShort** (weaponNameShortMp) | Action_TranslatedText | short name, shown in multiplayer ("P2K") | as above | V |
| 0x040 | 4 | **refireDelay** (unk16; Ghidra: someDurationRelatedToFiring) | int (treated as unsigned) | frames between shots (at least 1) | `Player_WeaponFiring`: countdown `BLData+0x8bc = max(delay, 1)`, decremented by `FRAME_RATE_MUL`; `FUN_00066e90` (DroneWeap_GetRefireDelay, invented name; at least 60 for entries 6, 10, 14-16) | V |
| 0x044 | 2 | **bulletSpawnFrame** (unk17) | ushort | frame of the fire (or throw) animation at which the projectile is spawned; 0 = at once | `Player_WeaponFiring`, `Player_SetWeaponAnimObj` 0xbcaa0 states 9 and 12; also the Samurai's charge display | V |
| 0x046 | 2 | _pad46 | | never stored | constructor | V |
| 0x048 | 4 | muzzleFlash1stPerson | HASHCODE (0x02) | first-person muzzle flash sprite/entity | `Draw_MuzzleFlash` 0xb7f20 (set on `BLData.muzzleFlashObj` at weapon bone 0, bone 4 for twin guns); `Player_MuzzleFlash` 0xb9fc0 (gate for light and smoke) | V |
| 0x04c | 4 | muzzleFlash3rdPerson | HASHCODE (0x02) | third-person muzzle flash entity | `AnimDatumGetWeaponInfo` 0x13630 → `AnimProcessScriptCmds` 0x17c50 script command 7 (datum `weaponDatum + 1`, 3 frames), `DroneWeap_GetWeaponInfo` 0x66850 → `DroneWeap_FireWeapon`; `Player_MuzzleFlash` in multiplayer | V |
| 0x050 | 1 | **weaponDatum** (animDatumRelated3; Ghidra: whichBone) | uchar | first datum of the weapon group on the character skeleton (weapon at +0, muzzle flash at +1, casing at +2, aim bone at +3); 0xff = not held visibly | `AnimDatumGetWeaponInfo` (returns it, -1 for 0xff), `AnimObjectAimAt` 0x15060, `AnimObjectDraw` 0x16c20, `Bullet_init_casing_ex` 0x23120 | V |
| 0x051 | 1 | **muzzleFlashAlphaMin** (Ghidra: muzzleFlashMaybeBrightnessMin) | uchar | first-person flash sprite brightness, random in [min, max) | `Draw_MuzzleFlash`: `obj->maybeBrightness = min + rand(max - min)` | V |
| 0x052 | 1 | **muzzleFlashAlphaMax** | uchar | see above | as above | V |
| 0x053 | 1 | _pad53 | | never stored | constructor | V |
| 0x054 | 1 | muzzleFlash_b | uchar | muzzle (and projectile) light, **blue** | `Light_Create(pos, r = +0x56, g = +0x55, b = +0x54, radius = +0x58, ...)` in `Player_MuzzleFlash`, `AnimProcessScriptCmds`, `DroneWeap_FireWeapon`, `Bullet_Update` (projectile flag 0x2000) | V |
| 0x055 | 1 | muzzleFlash_g | uchar | light green | as above | V |
| 0x056 | 1 | muzzleFlash_r | uchar | light red | as above | V |
| 0x057 | 1 | _pad57 | | never read; stored as 0 inside one dword store with +0x54..+0x56 | constructor | V |
| 0x058 | 4 | **muzzleLightRadius** (muzzleFlashBrightness) | float | radius of the muzzle light, in units (5-8) | 5th argument of `Light_Create`, which level light placements fill with their radius key (`docs/level/objects-dressing.md`) | V (argument), I (radius) |
| 0x05c | 4 | projectileGfx | HASHCODE (0x02) | entity drawn for the projectile (rockets, grenades, mines); 0 or 0xffffffff for none | `Bullet_Update`, first frame, when projectile flags `& 0x1010 == 0` | V |
| 0x060 | 2 | **fireSound3rdPerson** (animDatumRelated2; Ghidra: animDatumRelated) | ushort (SFX id) | sound played when a drone or an animation script fires it (`SFX_WEAPON_DRONE_*_SHOT`) | `AnimDatumGetWeaponInfo` (4th out-parameter) → `Bullet_init(..., weaponSound)` → `Sound_Play3D` in script command 7; `DroneWeap_GetWeaponInfo` → `DroneWeap_FireWeapon` (`0` → 0xffff, none) | V |
| 0x062 | 2 | _pad62 | | always 0; stored with +0x60 as one dword | constructor | V |
| 0x064 | 1 | **unk64** | uchar | values 0, 2, 3 (bits 1 and 2): 3 on most firearms and explosives, 2 on entry 21 only | no reader found | U |
| 0x065 | 3 | _pad65 | | never stored | constructor | V |
| 0x068 | 4 | **weaponFlags** (someFlags) | uint (bits) | player-side handling flags, table below | | V |
| 0x06c | 4 | **projectileFlags** (flagsForSwooshAndCasing) | uint (bits) | projectile behaviour and effects, table below | | V |
| 0x070 | 4 | **impactFlags** (someFlagsRelatedToExplosiveTimer) | uint (bits) | what happens on contact or when the fuse runs out, table below | | V |
| 0x074 | 2 | **swooshInterval** (unk18) | short | frames between new trail (swoosh) segments | `Bullet_DoTrails`: new `Swoosh_Create` when `(now - created) % swooshInterval == 0` (projectile flag 0x10000) | V |
| 0x076 | 2 | **swooshLifetime** (swooshRelated) | short | lifetime of each trail segment in frames (halved in multiplayer above 60) | 3rd argument of `Swoosh_Create` 0xa3c40, from `Bullet_init` and `Bullet_DoTrails` | V |
| 0x078 | 1 | casingDelayFrames | **uchar** (was `short`) | frames before an ejected casing appears | `Bullet_init_casing` 0x23080: `delay = (float)(byte)`; constructor writes a byte | V |
| 0x079 | 1 | _pad79 | | never stored | constructor | V |
| 0x07a | 2 | **effectDurationFrames** (unk19) | short | 15 s for Stun/Smoke Grenade (53, 54, 105) and Shaver (89, 91), 60 s for the Q-Pen (65); computed by the constructor | no reader found | U (an effect duration: I) |
| 0x07c | 4 | suppressorGfx | HASHCODE (0x02) | silencer entity attached to datum 0 of the first-person model | `Player_WeaponFiring` (weapon flag 0x800; removed in state 8) | V |
| 0x080 | 4 | **worldModelGfx** (wpn3rdPersonGfx) | HASHCODE (0x02) | the weapon as an entity: held by characters, pickups, dropped weapons | `AnimObjectDraw`, `AnimDatumGetWeaponInfo`, `Pickup_CreateSimple` 0xa7f10, `Pickup_CreateFromSet` 0xa80b0, `DroneWeap_DropWeapon` 0x67690, `Player_CheckForDeath` 0xac390 | V |
| 0x084 | 1 | **animSet** (weaponAnimationSet) | **uchar** (was `uint`) | third-person animation set: 0 unarmed, 1 handgun, 2 SMG, 3 rifle, 4 two-handed handgun, 5 launcher, 6 twin handguns (spreadsheet's AnimSet lookup) | `PlayerAnimSetInitNormal` 0xa91e0 and the crouch/stand variants (multiplayer only; single player forces 999), `AnimObjectAimAt` (multiplayer: 0 → no aiming); constructor writes a byte | V |
| 0x085 | 3 | _pad85 | | never stored | constructor | V |
| 0x088 | 4 | maxZoom | float | maximum scope zoom factor (1 = none) | `Player_ResetZoom` 0xb7100 (clamp), `Player_InitAmmoWeapons` and `FUN_0001e540` (remembered zoom starts at `sqrt(maxZoom)`), written by `Upgrade_Weapon` | V |
| 0x08c | 4 | **scopeSway** (cameraSwingAmt) | float | camera sway amplitude, scaled by (zoom - 1): only felt when zoomed | `Player_Update` 0xad520 → `Camera_ApplySwing` 0x24490 (three sines, 4 s period, `× amount × (projScaleZ - 1)`) | V |
| 0x090 | 1 | ammoType | uchar | index into `ammo_data` and `BLData.ammo` | ~25 readers (`Player_AmmoIndex`, `Player_RoundToFire`, `Player_ReloadAmmoType`, `Bullet_init_casing`, `BOTWEAP_*`...) | V |
| 0x091 | 1 | **ammoPerShot** (cooldownTimerIncreaseAmount) | uchar | rounds or charge used per shot (Samurai overcharge 100, laser burst 35) | `Player_RoundToFire` 0xb6f40 arguments in `Player_WeaponFiring`; Stunner refund in `Player_WeaponInitBullet` | V |
| 0x092 | 2 | **clipSize** (clipSizeOrCooldown) | short | clip size, or maximum charge for recharging weapons (Stunner, Laser, Oddjob's Hat, Samurai) | `Player_ReloadAmmoType`, `Player_EquipWeapon/Ammo`, `Player_Update` (recharge up to it), `HUD_UpdateAmmoPane` (percent), `Bullet_init` (bloom cap) | V |
| 0x094 | 1 | rumble | uchar | controller rumble strength (0-100) per shot | `Input_RumbleStart(pad, 5, rumble)` in `Player_WeaponFiring` | V |
| 0x095 | 3 | _pad95 | | never stored | constructor | V |
| 0x098 | 4 | **spreadPerShot** (accuracyModifierSomehow) | float | spread added per shot already fired in the current burst (bloom) | `Bullet_init`: `bloom = min(shotsSoFar, clipSize) × spreadPerShot` | V |
| 0x09c | 4 | **unk9c** (unk22) | float | 0.01 sniper rifles, 0.02 Delta Repeater and Hat, 0.04 SMGs, 0.05 handguns and grenades, 0.08 shotgun, 0.12 launchers and AIMS-20, 0 gadgets: looks like a recoil or view-kick amount | no reader found | U |
| 0x0a0 | 60 | animation script slots | HASHCODE[15] (0x06) | see "Animation slots" | `Player_SetWeaponAnimObj`, `Player_WeaponFiring`, `Player_SetFiringAnim`, `Player_Weapon`, `Player_WeaponSelect` | V |
| 0x0dc | 4 | weaponModelHashcode | HASHCODE (0x05) | first-person (view) model, an animated model; 0 = nothing in hand | `Player_SetWeaponAnim` 0xb7df0 (`AnimObjectNew`), `Player_WeaponSelect` 0xb7aa0 (same model → instant switch), `Player_SetWeaponAnimObj`, `Player_SetFiringAnim`, `Player_MuzzleFlash`, `Player_PositionCamera` 0xa8a60; written by `Player_Init` | V |
| 0x0e0 | 12 | **viewOffset[3]** (animRelated1) | float[3] | position of the first-person weapon object relative to the camera, single player standing | `Player_SetWeaponAnimObj` tail: `weaponObject.pos = viewOffset[isMP]` (+ bob, `y += 0.2`, `z -= 0.5`) | V |
| 0x0ec | 12 | **viewOffsetAlt[3]** (animRelated2) | float[3] | the same in multiplayer; in single player the target it blends to as the player crouches (`t = headCrouchOffset × 2.22`). Equal to `viewOffset` except entries 97, 113 | as above (`[isMP × 3]` indexing at 0xbd5f3) | V |
| 0x0f8 | 12 | **casingEjectVelocity[3]** (casingSpawnPos) | float[3] | casing ejection velocity in units per frame, in the ejection bone's space (y negated); not a position | `Bullet_init_casing_ex`: rotated by the bone matrix, ±0.005-0.01 random added, `× FRAME_RATE_MUL`, plus the shooter's velocity; the position is the bone's | V |
| 0x104 | 4 | **onFired** (multiplayerWeaponFiredCallback) | `void (*)(obj_tag *player)` | called after firing when `ammoType == 0` | `Player_WeaponInitBullet`; **no writer exists** (no absolute xref, no `[reg+0x104]` store in weapon code): always NULL | V |
| 0x108 | 4 | **onImpact** (field90..93 as padding; Ghidra: someCallback) | `void (*)(obj_tag *bullet, plane_equ_tag *, _VECTOR *pos)` | called for each hit, after which the bullet is deleted | `Bullet_CollisionHandler`; never written: always NULL | V |

### Hashcode families

| top byte | family | fields |
|---|---|---|
| 0x02 | graphics entities (sprites, particle and object gfx) | `+0x048`, `+0x04c`, `+0x05c`, `+0x07c`, `+0x080`; also text label 0x02000004 (" ") in `+0x030/+0x034` |
| 0x05 | animated models (skins), loaded by `AnimObjectNew`; text labels in bank 5 use the same top byte but a different namespace | `+0x0dc` (models); `+0x030..+0x03c` (labels) |
| 0x06 | scripts (animation scripts here) | `+0x0a0..+0x0d8` |

Text labels (`Action_TranslatedText`, bank << 24 | index) resolved with `tools/ui/text_bank.py` and `UKTxt.Dat`:
`+0x038` holds the full names ("Winter Tactical Sniper", "Kowloon Type 40", "Phoenix Samurai"...), `+0x03c` the
short ones ("Tactical Sniper", "K-40", "Samurai"...), `+0x030` "Semi", "Burst", "Auto", "Silenced", "Single",
"Armour Piercing", "Guided", "Heatseeker", "Detonate", "Deploy", "Activate", "Overcharge", "Beam" and the gadget
names. 0xffffffff in `+0x038/+0x03c` (21 entries) means no name.

## Flag tables

### `+0x010` damageClass

Each entry has exactly one bit. The names come from which weapons carry the bit; the code treats them as a set
(object masks in `Collide_FilterBullets`), and bit 0x1 is the only one tested on its own.

| bit | entries | meaning | evidence |
|---|---|---|---|
| 0x0001 | 1 (fists), 75, 77 (Stunner, the non-firing pair), 99-102 (drone close combat) | melee | `Player_DealWithObjHit`: punch sound and head jerk (direction from base 0x63/0x65/0x66) in single player [V]; excluded from shot statistics [V] |
| 0x0002 | 78, 79 (Laser) | laser | excluded from statistics; masked by `0x106` and kept by `0xfffd` [V] |
| 0x0004 | 74, 76 (Stunner) | stun | excluded from statistics; masked by `0x106` [V] |
| 0x0008 | 2-17, 66-69 (handguns, Delta Repeater, Golden Gun, K5, Hat) | handgun class | counted in statistics [V]; name [I] |
| 0x0010 | 50, 51, 106, 110, 111 (Samurai, lasers), 96, 97, 103, 107, 112, 113 (vehicle and emplacement guns) | energy / mounted weapons | [I] |
| 0x0020 | 27, 42-50, 52, 55, 58-65, 98, 104, 108-110 | explosive projectiles | [I] |
| 0x0040 | 22-41 (SG5, AIMS-20, Auto 12, sniper rifles) | rifle class | [I] |
| 0x0080 | 18-21 (Storm, M9K) | SMG class | [I] |
| 0x0100 | 80, 81, 84-88 (Grapple, Micro-Camera, Decryptor, Q-Worm) | gadget | excluded from statistics; masked by `0x106` [V] |

### `+0x068` weaponFlags

| bit | entries | meaning | readers |
|---|---|---|---|
| 0x00000002 | 1, 48, 49, 75, 77, 78, 79, 99-102 | can fire underwater (when not surfaced) | `Player_WeaponFiring` [V] |
| 0x00000004 | 2-9 (PP7, P2K) | unknown | no reader found [U] |
| 0x00000008 | 6-9, 15, 23, 24 | laser sight (the laser pointer dot; also checked on the alt-fire variant) | `Player_LaserPointer` 0xb8940, `Player_SetWeaponAnim` → `BLData+0x8f4` [V] |
| 0x00000010 | 26, 27, 30-41, 55-57, 89-92 | when out of ammo, switch to the alt-fire variant if it has ammo (sniper → armour piercing, mine → detonator); ammo picked up while the clip is empty goes straight into the clip. Not "has a clip": the handguns lack it | `Player_HandleHasNoAmmo` 0xbad40, `Player_EquipAmmo` 0xba790, `BOTWEAP_EquipAmmo` 0x1e820 [V] |
| 0x00000020 | 0, 1, 69-71, 80-94, 99-102 | no ammo counter on the HUD | `HUD_UpdateAmmoPane` [V] |
| 0x00000040 | 17, 26, 27, 30-41, 50, 51, 84, 85, 111 | real scope: the gun is hidden while scoped, the crosshair changes, alt fire and reload are blocked while scoped, more states drop the scope | `Player_Weapon`, `Player_PositionCamera`, `HUD_UpdateCrossHair` 0xb4b60, `Player_Aiming` 0xb7610, `Player_WeaponFiring`, `Player_WeaponInitBullet` [V] |
| 0x00000080 | 55 entries: silenced variants, grenades and mines, gadgets, Stunner, Laser, fists, close combat | unknown; the set looks like "quiet" weapons | no reader found [U] |
| 0x00000100 | 28, 29 (Auto 12) | reload one shell at a time (loop `+0x0a4`, end with `+0x0ac`) | `Player_SetWeaponAnimObj` state 6, `Player_ReloadAmmoType` 0xb6fe0, `FUN_0001e5b0` (bots) [V] |
| 0x00000200 | none | alternate reload animations `+0x0a4`/`+0x0ac` on successive reloads | `Player_WeaponFiring` [V] |
| 0x00000400 | 74, 76, 78-81 (Stunner, Laser, Grapple) | continuous fire: stays in state 9 while fire is held, then plays `+0x0b4` (state 8) | `Player_SetWeaponAnimObj`, `Player_WeaponFiring` [V] |
| 0x00000800 | 3, 5, 7, 9, 20 | silencer model: `suppressorGfx` on datum 0 (and removed when the alt variant has it) | `Player_WeaponFiring` [V] |
| 0x00001000 | 2-9, 14-16 | slide lock: last-round fire animation `+0x0b4`; reload after emptying starts at frame 6; the empty pose is kept instead of returning to idle | `Player_SetFiringAnim`, `Player_WeaponFiring`, `Player_SetWeaponAnimObj` (tested on the base entry) [V] |
| 0x00002000 | 50, 51, 65, 67, 68, 74, 76, 78-88, 93, 94, 111 | no muzzle smoke puff | `Player_MuzzleFlash`, `AnimProcessScriptCmds` [V] |
| 0x00004000 | 44-49, 52-55, 58, 104, 105, 109 | the projectile can be shot: a hit detonates it | `Bullet_handle_object_destruction` (with impact flags `0x80204`) [V] |
| 0x00008000 | 1, 17, 27, 28, 30-41, 65, 66, 69, 75, 77, 88 | single action: no refire until the fire animation ends; scope may stay up (not be raised) in state 9 | `Player_WeaponFiring` (state 9), `Player_Weapon` [V] |
| 0x00010000 | 41 entries: launchers, grenades, mines, gadgets, Samurai | projectile starts half way between the eye and the muzzle (not at the eye) | `Player_WeaponInitBullet` [V] |
| 0x00020000 | 56, 57, 90, 92 (detonators) | detonate the player's live projectiles of this base (timer -10), then switch back to the base weapon | `Player_WeaponInitBullet` [V] |
| 0x00040000 | 13 (K-40, animation set 6) | twin guns: fire animations, muzzle bone (0/4) and casing bone (1/5) alternate | `Player_SetFiringAnim`, `Draw_MuzzleFlash`, `Player_MuzzleFlash`, `Bullet_init_casing_ex` [V] |
| 0x00080000 | 1, 86, 87 | random fire animation: 50% `+0x0b4` instead of `+0x0b0` | `Player_SetFiringAnim` [V] |
| 0x00100000 | 86, 87 | restart the fire animation while fire is held | `Player_SetWeaponAnimObj`, `Player_WeaponFiring` [V] |
| 0x00400000 | 30-41 (sniper rifles) | perfect accuracy when scoped (player) and always (drones) | `Bullet_init` [V] |

### `+0x06c` projectileFlags

| bit | entries | meaning | readers |
|---|---|---|---|
| 0x00000002 | 27, 54, 105 | smoke trail above water | `Bullet_DoTrails` → `Bullet_trail_effect_smoke` [V] |
| 0x00000004 | 44 (Sentinel guided) | remote-controlled: the player steers it (sub-state REMOTECONTROL, camera mode 12; fire detonates) | `Bullet_init`, `Bullet_Update`, `Bullet_Delete` 0x22bd0, `Player_WeaponFiring` [V] |
| 0x00000008 | 17, 42, 43, 52-55, 58-64, 69, 89, 91, 105, 108 | affected by gravity | `Bullet_Update` [V] |
| 0x00000010 | 2-26, 66 | bullet: no projectile model; drawn as a tracer only when a drone fires | `Bullet_Update` (`& 0x1010`) [V] |
| 0x00000020 | 42, 43 | dust/smoke trail (MGL) | `Bullet_DoTrails` [V] |
| 0x00000040 | 2-16, 18-26, 28-41, 66 | eject a casing (single player, view model required) | `Player_WeaponInitBullet` [V] |
| 0x00000080 | 74, 76 (Stunner) | taser beam | `Player_MuzzleFlash` → `Player_SetupTaser` [V] |
| 0x00000100 | 78, 79 (Laser) | laser beam; projectile entity scaled by speed; impact effect 0x10 | `Player_MuzzleFlash` → `Player_SetupLaser`, `Bullet_Update`, `Bullet_CollisionHandler` [V] |
| 0x00000200 | 44-49, 65, 67, 68, 109 | accelerating projectile: starts at a quarter speed, +2% a frame | `Bullet_init`, `Bullet_DoTrails` [V] |
| 0x00000400 | 52-55, 58-64, 69, 82, 89, 91, 108 | throw: fire animation is a wind-up (state 11), `+0x0b4` is the throw (state 12) | `Player_WeaponFiring`, `Player_SetWeaponAnimObj` [V] |
| 0x00000800 | 34 entries (explosives, darts, Stunner, Laser, close combat) | collides with the "0x10" material and drops collision type 2 (`0x208` instead of `0x20a`) | `Player_WeaponInitBullet`, `Bullet_CollisionHandler`, `Bullet_Update` [V; material meaning U] |
| 0x00001000 | 96, 97, 113 | always drawn as a tracer | `Bullet_Update` [V] |
| 0x00002000 | 44-47, 70, 109 | dynamic light on the projectile (colour `+0x054..+0x056`) | `Bullet_Update` [V] |
| 0x00004000 | 80, 81 | grapple line: the projectile position is copied to the player each frame | `Bullet_init`, `Bullet_DoTrails` [V] |
| 0x00008000 | 45, 47, 52-55 | tumbles: rolls about its axis, slower after each bounce | `Bullet_Update` [V] |
| 0x00010000 | 44-47, 50, 51, 98, 106, 109-111 | swoosh trail (`+0x074`, `+0x076`; beam gfx for bases 0x33, 0x6a, 0x6e) | `Bullet_init`, `Bullet_DoTrails` [V] |
| 0x00020000 | 46, 48, 69, 109 | homing | `Bullet_Update` → `Bullet_homing` [V] |
| 0x00040000 | 37 bullet weapons | fly-by sound near player 1 in single player | `Bullet_DoTrails` [V] |
| 0x00080000 | 65 | second smoke/dust effect | `Bullet_DoTrails` [V] |

### `+0x070` impactFlags

| bit | entries | meaning | readers |
|---|---|---|---|
| 0x00000002 | 47 bullet weapons | impact effect flags `0x61` (marks and sparks) | `Bullet_CollisionHandler` [V] |
| 0x00000004 | 27, 42, 44-49, 65, 98, 109, 111 | explode on impact (`explodeRadius`, `damage`, script 0x6000052) | `Bullet_CollisionHandler`, `Bullet_handle_object_destruction` [V] |
| 0x00000010 | 2-26, 66 | can ricochet (up to 3 times, by `Effect_RicochetProb`) | `Bullet_CollisionHandler` [V] |
| 0x00000040 | 80, 81 | grapple: attach to a grapple object when it hits one | `Bullet_Delete` [V] |
| 0x00000080 | 43, 52-54, 104, 105 | bounce (grenade bounce sounds, up to 20 bounces) | `Bullet_CollisionHandler` [V] |
| 0x00000100 | 55, 58-64, 69, 89, 91, 108 | stick to what it hits (state 3); always registers hits on cars | `Bullet_CollisionHandler` [V] |
| 0x00000200 | 43, 52-55, 59-64, 89, 91, 104, 105, 108 | fuse: counts the timer down and goes off at zero (bases 0x37, 0x59, 0x5b wait for a detonator instead) | `Bullet_DoTrails`, `Bullet_handle_object_destruction` [V] |
| 0x00000400 | 1, 75, 77, 99-102 | unknown (the melee entries) | no reader found [U] |
| 0x00000800 | 74, 76, 78, 79 | unknown (Stunner and Laser) | no reader found [U] |
| 0x00001000 | none | impact effect 0x10 | `Bullet_CollisionHandler` [V] |
| 0x00002000 | 53, 89, 91 | goes off as a flash-bang (`Player_SetFlashBang`, drones message 0x18, turrets disabled) | `Bullet_DoTrails` [V] |
| 0x00008000 | 55 | unknown | no reader found [U] |
| 0x00010000 | 55 | starting timer 300 frames | `Bullet_init` [V] |
| 0x00020000 | 54, 105 | goes off as a smoke cloud (20 s, 40 s for base 0x69) | `Bullet_DoTrails`, `Bullet_CollisionHandler` (roll sound) [V] |
| 0x00080000 | 58 | trip-bomb: once stuck, casts a 200-unit beam and detonates when a player, drone or car crosses it | `Bullet_Update`, `Bullet_CollisionHandler`, `Bullet_Delete` [V] |
| 0x00100000 | 50, 110 | beam explosion on impact (script 0x60007c4, radius 1 → `explodeRadius`) | `Bullet_CollisionHandler` [V] |
| 0x00200000 | 107 | same as 0x100000 | `Bullet_CollisionHandler` [V] |

Other hard-coded per-weapon behaviour in the bullet code (by base): satchel charges (0x3b) get a 5/10/15/20/25/30 s
timer by variant (900 frames for variant 108); the Remote Mine (0x37) keeps 7777 (wait for the detonator) for
the player and 420 frames for drones; base 0x69 uses timer 5; Oddjob's Hat (0x45) returns after 600 frames.

## Animation slots

`+0x0a0..+0x0d8` hold animation scripts (hashcode family 0x06) played on the first-person weapon object
(`BLData.weaponObject`). The weapon state numbers are those of `docs/architecture/player-weapons-mp.md`.

| off | proposed name | current name | played | who has it | evidence |
|---|---|---|---|---|---|
| 0x0a0 | animIdle | animScriptTag | idle loop, re-added after raise, reload, fire and fidgets | almost every weapon | `Player_SetWeaponAnimObj` [V] |
| 0x0a4 | animReload | someAnimhc3 | reload (speed 1.35), state 6; the per-shell loop for the Auto 12 | weapons with reserve ammo | `Player_WeaponFiring`, `Player_SetWeaponAnimObj` [V] |
| 0x0a8 | animReloadStart | someAnimHsh | pre-reload, state 5 | 28, 29 | as above [V] |
| 0x0ac | animReloadEnd | maybeAnAnimScript | end of a shell-by-shell reload, state 7 (also the second animation of alternating reloads, flag 0x200) | 28, 29 | as above [V] |
| 0x0b0 | animFire | maybeWeaponFireAnimHashcode2 | fire, state 9 (wind-up for throws) | every firing weapon | `Player_SetFiringAnim` [V] |
| 0x0b4 | animFireAlt | maybeWeaponFireAnimHashcode | last-round fire (weapon flag 0x1000, base 0x11 when empty); every other shot (0x40000); 50% (0x80000); end of continuous fire (0x400, state 8); the throw (projectile flag 0x400, state 12); the Ronin turret hand-back (`GT_LoseControl` 0x9a830, entry 83) | 1-9, 13-17, 52-54, 74, 76, 78-81, 83, 86, 87 | `Player_SetFiringAnim`, `Player_SetWeaponAnimObj`, `Player_WeaponFiring` [V] |
| 0x0b8 | animScopeIn | someAnimHC2 | bring to the eye (speed 1.25), state 15 | 26, 27, 30-41, 84, 85 | `Player_Weapon` (ours) [V] |
| 0x0bc | animScopeOut | someAnimHC | lower from the eye, state 16 (else `animScopeIn` reversed) | 84, 85 | `Player_Weapon` [V] |
| 0x0c0 | animRaise | animationHashcode | draw (speed 1.25), state 3 → 4 | every held weapon | `Player_SetWeaponAnimObj` [V] |
| 0x0c4 | animLower | animSpeedRelated | holster (speed 1.25), states 1/13 → 2/14 | every held weapon | `Player_SetWeaponAnimObj`, `Player_WeaponSelect` [V] |
| 0x0c8 | animModeSwitch | field80_0xc8 | switching between variants of one base that share a model (fire mode, silencer on or off; speed 1.3), state 8. Played for the variant switched to: silenced and plain entries differ | 2-11, 14, 15, 18-30, 42-51, 55, 56, 59-64, 70, 84, 85, 89-92, 108, 109, 111 | `Player_WeaponSelect` [V] |
| 0x0cc | animFidget | anotherAnimScriptTag | idle fidget after 20 s without input | most weapons | `Player_SetWeaponAnimObj` [V] |
| 0x0d0 | animRelax | field82_0xd0 | gun lowered while no drone is alert, the scope is down and the crosshair centred (idle state 3) | 2-9 (PP7, P2K) | `Player_SetWeaponAnimObj` [V] |
| 0x0d4 | animRelaxEnd | field83_0xd4 | back up from the relaxed pose when a drone becomes alert or the player aims | 2-9 | as above [V] |
| 0x0d8 | animLowerRelaxed | animationScript | holster from the relaxed pose | 2-9 | as above [V] |

## Proposed struct

```c
// One entry per weapon variant; weapon_data[115] at 0x0018cfa0. Entries 0x35.. are built by
// WeaponDataTableInit (0x000f5530). See docs/weapons.md.
typedef struct {
    short weaponVariantNum;           // 0x000 this entry's index
    short weaponBaseNum;              // 0x002 base variant of the group (was char + pad)
    bool isBaseWeapon;                // 0x004 owns the group's inventory slot
    signed char offsetToNextAltFireVariant; // 0x005 entry offset to the alt-fire variant (signed)
    uchar botWeaponClass;             // 0x006 RENAMED (maybeFlags): 1 handgun 2 auto 3 sniper 4 explosive
    uchar _pad07;                     // 0x007
    float explodeRadius;              // 0x008 RENAMED (maybeExplodeRange): explosion size, units
    float damage;                     // 0x00c damage per hit / explosion strength
    ushort damageClass;               // 0x010 RENAMED (field9_0x10): weapon class bit, see doc
    uchar _pad12[2];                  // 0x012
    float autoAimStrength;            // 0x014 RENAMED (autoaimRelated): percent
    uchar numBulletsPerShot;          // 0x018 projectiles per shot
    uchar _pad19[3];                  // 0x019
    float range;                      // 0x01c RENAMED (someDistance): max travel / use range, units
    float projectileSpeed;            // 0x020 RENAMED (unknownPurposeMaybeFloat): units per 60 Hz frame
    float baseSpread;                 // 0x024 RENAMED (maybeAccuracyRelated): x0.0014 rad
    short shotsPerTrigger[2];         // 0x028 RENAMED (indexIntoSomePlayerAmmoArray, maybeUnused): per fire mode, 999 = auto
    short droneBurstLength;           // 0x02c RENAMED (droneBulletBurstTimeRelated): shots per drone burst
    char numFireModes;                // 0x02e RENAMED (unk15, was short): modes the alt-fire button cycles
    uchar _pad2f;                     // 0x02f
    Action_TranslatedText fireModeName[2]; // 0x030 RENAMED (fireModePrimary/Secondary): HUD text per fire mode
    Action_TranslatedText nameLong;   // 0x038 RENAMED (weaponNameLongSp): single-player HUD name
    Action_TranslatedText nameShort;  // 0x03c RENAMED (weaponNameShortMp): multiplayer HUD name
    int refireDelay;                  // 0x040 RENAMED (unk16): frames between shots (min 1)
    ushort bulletSpawnFrame;          // 0x044 RENAMED (unk17, was uint): fire-anim frame that spawns the bullet
    uchar _pad46[2];                  // 0x046
    HASHCODE muzzleFlash1stPerson;    // 0x048 first-person flash entity (0x02)
    HASHCODE muzzleFlash3rdPerson;    // 0x04c third-person flash entity (0x02)
    uchar weaponDatum;                // 0x050 RENAMED (animDatumRelated3): first weapon datum on the skeleton, 0xff none
    uchar muzzleFlashAlphaMin;        // 0x051 RENAMED (field33_0x51): flash sprite brightness range
    uchar muzzleFlashAlphaMax;        // 0x052 RENAMED (field34_0x52)
    uchar _pad53;                     // 0x053
    uchar muzzleFlash_b;              // 0x054 light colour, blue
    uchar muzzleFlash_g;              // 0x055 light colour, green
    uchar muzzleFlash_r;              // 0x056 light colour, red
    uchar _pad57;                     // 0x057
    float muzzleLightRadius;          // 0x058 RENAMED (muzzleFlashBrightness): Light_Create radius
    HASHCODE projectileGfx;           // 0x05c projectile entity (0x02)
    ushort fireSound3rdPerson;        // 0x060 RENAMED (animDatumRelated2): SFX id for drones/scripts
    uchar _pad62[2];                  // 0x062
    uchar unk64;                      // 0x064 RENAMED (field45_0x64): 0/2/3, no reader found
    uchar _pad65[3];                  // 0x065
    uint weaponFlags;                 // 0x068 RENAMED (someFlags): see doc
    uint projectileFlags;             // 0x06c RENAMED (flagsForSwooshAndCasing): see doc
    uint impactFlags;                 // 0x070 RENAMED (someFlagsRelatedToExplosiveTimer): see doc
    short swooshInterval;             // 0x074 RENAMED (unk18): frames between trail segments
    short swooshLifetime;             // 0x076 RENAMED (swooshRelated): trail segment lifetime, frames
    uchar casingDelayFrames;          // 0x078 frames before the casing appears (was short)
    uchar _pad79;                     // 0x079
    short effectDurationFrames;       // 0x07a RENAMED (unk19): 15 s / 60 s, no reader found
    HASHCODE suppressorGfx;           // 0x07c silencer entity (0x02)
    HASHCODE worldModelGfx;           // 0x080 RENAMED (wpn3rdPersonGfx): held/pickup entity (0x02)
    uchar animSet;                    // 0x084 RENAMED (weaponAnimationSet, was uint): third-person AnimSet
    uchar _pad85[3];                  // 0x085
    float maxZoom;                    // 0x088 max scope zoom factor
    float scopeSway;                  // 0x08c RENAMED (cameraSwingAmt): sway x (zoom - 1)
    uchar ammoType;                   // 0x090 index into ammo_data / BLData.ammo
    uchar ammoPerShot;                // 0x091 RENAMED (cooldownTimerIncreaseAmount)
    short clipSize;                   // 0x092 RENAMED (clipSizeOrCooldown): clip or max charge
    uchar rumble;                     // 0x094 rumble strength 0-100
    uchar _pad95[3];                  // 0x095 (was _pad1.._pad3)
    float spreadPerShot;              // 0x098 RENAMED (accuracyModifierSomehow): bloom per shot
    float unk9c;                      // 0x09c RENAMED (unk22): no reader found
    HASHCODE animIdle;                // 0x0a0 RENAMED (animScriptTag)
    HASHCODE animReload;              // 0x0a4 RENAMED (someAnimhc3)
    HASHCODE animReloadStart;         // 0x0a8 RENAMED (someAnimHsh)
    HASHCODE animReloadEnd;           // 0x0ac RENAMED (maybeAnAnimScript)
    HASHCODE animFire;                // 0x0b0 RENAMED (maybeWeaponFireAnimHashcode2)
    HASHCODE animFireAlt;             // 0x0b4 RENAMED (maybeWeaponFireAnimHashcode)
    HASHCODE animScopeIn;             // 0x0b8 RENAMED (someAnimHC2)
    HASHCODE animScopeOut;            // 0x0bc RENAMED (someAnimHC)
    HASHCODE animRaise;               // 0x0c0 RENAMED (animationHashcode)
    HASHCODE animLower;               // 0x0c4 RENAMED (animSpeedRelated)
    HASHCODE animModeSwitch;          // 0x0c8 RENAMED (field80_0xc8)
    HASHCODE animFidget;              // 0x0cc RENAMED (anotherAnimScriptTag)
    HASHCODE animRelax;               // 0x0d0 RENAMED (field82_0xd0)
    HASHCODE animRelaxEnd;            // 0x0d4 RENAMED (field83_0xd4)
    HASHCODE animLowerRelaxed;        // 0x0d8 RENAMED (animationScript)
    HASHCODE weaponModelHashcode;     // 0x0dc first-person model (0x05)
    float viewOffset[3];              // 0x0e0 RENAMED (animRelated1): view-model offset, SP standing
    float viewOffsetAlt[3];           // 0x0ec RENAMED (animRelated2): MP / SP crouched
    float casingEjectVelocity[3];     // 0x0f8 RENAMED (casingSpawnPos): bone space, units per frame
    void (*onFired)(obj_tag *player); // 0x104 RENAMED (multiplayerWeaponFiredCallback): always NULL
    void (*onImpact)(obj_tag *bullet, plane_equ_tag *plane, _VECTOR *pos); // 0x108 NEW (field90..93): always NULL
} weapon_definition_tag;

static_assert(sizeof(weapon_definition_tag) == 0x10c, "weapon_definition_tag size");
static_assert(offsetof(weapon_definition_tag, botWeaponClass) == 0x006, "botWeaponClass");
static_assert(offsetof(weapon_definition_tag, damageClass) == 0x010, "damageClass");
static_assert(offsetof(weapon_definition_tag, range) == 0x01c, "range");
static_assert(offsetof(weapon_definition_tag, shotsPerTrigger) == 0x028, "shotsPerTrigger");
static_assert(offsetof(weapon_definition_tag, numFireModes) == 0x02e, "numFireModes");
static_assert(offsetof(weapon_definition_tag, refireDelay) == 0x040, "refireDelay");
static_assert(offsetof(weapon_definition_tag, weaponDatum) == 0x050, "weaponDatum");
static_assert(offsetof(weapon_definition_tag, fireSound3rdPerson) == 0x060, "fireSound3rdPerson");
static_assert(offsetof(weapon_definition_tag, weaponFlags) == 0x068, "weaponFlags");
static_assert(offsetof(weapon_definition_tag, casingDelayFrames) == 0x078, "casingDelayFrames");
static_assert(offsetof(weapon_definition_tag, animSet) == 0x084, "animSet");
static_assert(offsetof(weapon_definition_tag, ammoType) == 0x090, "ammoType");
static_assert(offsetof(weapon_definition_tag, clipSize) == 0x092, "clipSize");
static_assert(offsetof(weapon_definition_tag, animIdle) == 0x0a0, "animIdle");
static_assert(offsetof(weapon_definition_tag, weaponModelHashcode) == 0x0dc, "weaponModelHashcode");
static_assert(offsetof(weapon_definition_tag, casingEjectVelocity) == 0x0f8, "casingEjectVelocity");
static_assert(offsetof(weapon_definition_tag, onImpact) == 0x108, "onImpact");
```

The struct needs `#pragma pack` only if the project packs it today; with natural alignment every member above is
already aligned (the padding is explicit). Renaming touches reimplemented code that uses the old names
(`Player.cpp`, `HUD.cpp`, `bullet.cpp`, `NDrone2.cpp`, `PCQWorm.cpp`, `ScriptPlayer.cpp`, `SpaceLaser.cpp`,
`Upgrade.cpp`, `UpgradeShadow.cpp`, `weapon_stats.cpp`) and the Ghidra type, whose names already differ from the
header in places (`maybeExplodeStrength` for `damage`, `bulletTrailTimeRelated`, `someDurationRelatedToFiring`,
`whichBone`, `muzzleFlashMaybeBrightnessMin/Max`, `animDatumRelated`, `someCallback`).

## Differences from the current header and the spreadsheet

- Header: `weaponBaseNum` is a short (the code reads a word); `+0x005` is signed; `unk15` is a byte; `unk17` is a
  word; `casingDelayFrames` and `weaponAnimationSet` are bytes; `+0x010` is a real field; `+0x108` is a callback,
  not padding; the shorts from `+0x028` are a 2-element array followed by two different fields, not four.
- Header: `muzzleFlash_b/_g/_r` are correct. The spreadsheet's `muzzleFlashR/G/B` at `+0x54..+0x56` are in the
  wrong order (its own note "Possibly BGR order?" is right): orange muzzle flashes are R 255, G 138, B 0.
- Spreadsheet: `Flags1` (`+0x006`) is an enum, not flags; `Flags2` at `+0x010` is a one-bit-per-weapon class;
  `animDatumRel` (`+0x060`) is a sound id; `whichBone` is a datum index; `animDatumPoint0/1` are view-model offsets;
  `casingSpawnPoint2` is a velocity; `maybeCooldownImpact` is ammo per shot; `accuracyMod` is spread per shot;
  the unnamed `+0x020` is the projectile speed; `ammoPerBurst?` is shots per trigger pull for fire mode 0.
- `player-weapons-mp.md` says the `+0x104` callback is "set at run time"; nothing sets it. It also calls weapon
  flag 0x10 "loads a clip"; it is the empty-switch-to-alt-variant / load-on-pickup flag above.

## Open questions

- `+0x064` (byte, 0/2/3), `+0x07a` (15 s / 60 s on the stun, smoke and Shaver grenades and the Q-Pen) and `+0x09c`
  (float, recoil-like values) have no reader in the Xbox code found by name, by absolute address or by
  `[reg + offset]` search in the weapon functions. They may be leftovers, read by code on another platform, or
  read through a pointer this search missed (the PS2 ACTION.ELF could be checked).
- Weapon flag bits 0x4 (PP7, P2K) and 0x80 (silenced, thrown and gadget weapons), and impact flag bits 0x400
  (melee), 0x800 (Stunner, Laser) and 0x8000 (Remote Mine) have no reader found. The 0x80 set suggests "does not
  alert drones", but drone hearing has not been traced.
- `shotsPerTrigger[1]` is filled for every weapon, but only entries 96 and 97 have two fire modes, and their slot
  numbering in `WeaponStatus.fireModeIndex` was not checked against a running game.
- The material "0x10" that projectile flag 0x800 changes, and what collision type 2 is.
- `muzzleFlashAlphaMin/Max` set `obj_tag.maybeBrightness` of the flash object; whether the renderer uses that byte
  as alpha or as brightness is not settled.
- Why the Xbox refire delays are one frame shorter than the PS2 ones for 16 entries (a different countdown
  comparison on PS2 is the likely reason; not checked in ACTION.ELF).
