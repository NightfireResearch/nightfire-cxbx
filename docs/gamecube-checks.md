# GameCube checks compiled out of the Xbox build

The GameCube build (`/GameCube_US/Nightfire.elf` in Ghidra) keeps the debug checks that the Xbox build compiled
out. This lists every one, matched to the Xbox function it belongs to, so that each can go into our
reimplementation of that function.

**How they go in.** A GameCube `printf` check becomes `NF_WARN(...)` / `NF_WARN_IF(cond, ...)`
(src/action/actionhelpers.h): print the GameCube's own message and carry on, never changing what the code does.
Each is tagged `// GC check (<GameCube address>)`. A check that guards against writing past the end of one of our
arrays is an `NF_ASSERT` instead (it halts), as for more than 2048 textures and more than 50 map sounds. `NfWarnMuted`
(main.cpp) silences the warnings for shadow tests that feed bad input on purpose.

**When reimplementing a function, look it up in section B** and add its checks.

Section A was added in one pass: all of it except #16 and #17 (ours already had them) and #33 (the GameCube's display lists have no Xbox equivalent); #32 is an NF_ASSERT at the Xbox's own limit. The one that fires in normal play is "FYI : SPRITE
HASHCODE %08x NOT FOUND", for a dozen or so sprite hashcodes per level - the game has always done without them.

## Reporters found on the GC

| Reporter | Address | Kind | Call sites |
|---|---|---|---|
| `printf` | 0x80050158 | game printf (debug output, non-fatal) | 128 |
| `platform_fatal_error_handler` | 0x801555d0 | `(__FILE__, __LINE__, fmt, ...)`: prints "in %s on line %d", backtrace, then halts (fatal) | 51 (Q:/SS/source/Gamecube/*) |
| `dbg_printf` (OSReport) | 0x80155550 | SDK/OS report | ~90, nearly all SDK (OSCheckHeap, DVD, exception handler) |
| `sprintf` | 0x8014c56c | formatting only, no diagnostics | - |

No other assert helper exists: every "error/fatal/warning/invalid/not loaded/too many" string in the GC is reached from one of the three reporters, except two strings with no references ("Warning trying to free too much memory", "ERROR : too many rewards") and the `Txt_BindLabel` return strings.

The Xbox build has none of these message strings (a string search of default.xbe finds only "Invalid Text Label" / "Not Loaded", which are return values, not messages). So the GC `printf` is a debug-only macro that was compiled out.

**How the Xbox differs.** Most sites are `if (bad) printf(...); else work();`. On the Xbox the `if` usually survives and only the printf disappears. The "Xbox" column shows which case applies:
- **silent**: the condition and its fallback are still there. Verified for GameFlow_PushState, LoaderProcess, Sound_Play, RB_Build, View_CaptureSceneSub, behaviour_util_get, Sprite_Create2, AIBounds_Link2Cel, the PlrStat loggers and AnimScriptInit's not-loaded path.
- **dropped**: the test was only there for the message, so the Xbox has no condition at all. Verified for AnimSetInit, AnimScriptInit's command counts, parsemap_parsenextblock, Drone_CoverCornerNode/Drone_AIPoint and Collide_Update.
- **?**: not checked in the Xbox body.

Confidence refers to the GC→Xbox function match: **high** = same name, or the body and callees match; **med** = same caller/callee position and similar body; **low** = subsystem and role match only.

"Ours" means tagged `// AUTOINJECT` / `// FUNC_AT` in src/action (paths are relative to Q:/nightfire-cxbx).

---

## A. Checks in functions we already reimplement (33) - added

| # | GC function | GC addr | Condition | Message | Fatal? | Xbox function | Xbox addr | Conf | Xbox | Ours (file) / state (as found; now added) |
|---|---|---|---|---|---|---|---|---|---|---|
| **Memory** |
| 1 | Mem_Malloc | 0x80056700 | `((flags>>8)&0xff) > 0x4f \|\| size == 0` (then allocates anyway) | Mem_Malloc of bad type %d size %dk | no | Mem_Malloc | 0x70ae0 | high | ? | src/action/memory.cpp:176 - missing |
| 2 | Mem_Malloc | 0x80056700 | no free block large enough → Mem_PrintAllInfo, return NULL | OUT OF MEMORY! / Trying to allocate %dk | no | Mem_Malloc | 0x70ae0 | high | silent (dump kept) | memory.cpp:176 - calls Mem_PrintAllInfo, no message |
| 3 | Mem_Free | 0x80056534 | `*ptr < heapStart \|\| *ptr > heapEnd-0xc` → only clears ptr | Trying to free outside of heap %x | no | Mem_Free | 0x705f0 | high | silent | memory.cpp:241 - silent |
| 4 | Mem_Shrink (FUN_80056e44) | 0x80056e44 | numBytes < 0xd → return | Too small to shrink | no | Mem_Shrink | 0x70850 | high | silent | memory.cpp:350 - silent |
| 5 | Mem_Shrink | 0x80056e44 | trim end runs past the block end (no branch taken) | Error in Free2End | no | Mem_Shrink | 0x70850 | high | silent | memory.cpp:350 - silent |
| 6 | Mem_Shrink | 0x80056e44 | the containing block is already free | already free | no | Mem_Shrink | 0x70850 | high | silent | memory.cpp:350 - silent |
| 7 | Mem_PrintAllInfo (FUN_80057104) | 0x80057104 | a heap block has type byte > 0x4f (corrupt header) | Mem type %d size %dk | no | Mem_PrintAllInfo | 0x709c0 | high | ? | memory.cpp:111 |
| **Game flow** |
| 8 | GameFlow_Main | 0x800515e8 | GameFlow_GetState() not a known state | Game state not valid %d | no | GameFlow_Main | 0x6aca0 | high | ? | src/action/game.cpp:657 |
| **Loader** |
| 9 | LoaderProcess | 0x8005c444 | DirFileType is 9 or > 0x10 → return true (skip) | Loader : Invalid file type %d | no | LoaderProcess | 0xbe7b0 | high | silent (switch default) | src/action/engine/Loader.cpp:305 |
| 10 | LoaderProcess → FUN_8005c13c (loadable add) | 0x8005c13c | type 0xc and LoadableIndex >= 16 → entry dropped | Too many loadable files | no | LoaderProcess (inlined) | 0xbe7b0 | high | silent | Loader.cpp:305 |
| 11 | LoaderLoad | 0x8005c5ec | state 0 and `(levelHash & 0xff000000) != 0x07000000` (continues) | Loader : Invalid level 0x%x | no | LoaderLoad | 0xbe910 | high | ? | Loader.cpp:171 |
| 12 | LoaderLoad | 0x8005c5ec | psiFileOpen(name) fails → returns 0 | File %s not found | no | LoaderLoad | 0xbe910 | high | ? | Loader.cpp:171 |
| **Level parse** |
| 13 | parsemap_block_Coll_Data_New | 0x800595dc | collision block version != 4 and != 5 (parses on) | >>>FATAL<<<< : COLLISION VERSION MIS-MATCH %d | no | parsemap_block_Coll_Data_New | 0xa63f0 | high | ? | src/action/engine/parsemap.cpp:195 |
| **Sprites / text / sound** |
| 14 | Sprite_Create2 (FUN_8006637c) | 0x8006637c | textRef == NULL and textureHashcode == 0 | Sprite With no texture set | no | Sprite_Create2 | 0xcedf0 | high | silent | src/action/gfx/Sprite.cpp:135 |
| 15 | hashtable_set_sprite (FUN_80051da4) | 0x80051da4 | hashtable_getitem(hash) == NULL → return false | FYI : SPRITE HASHCODE %08x NOT FOUND | no | hashtable_set_sprite | (ours) | high | ? | src/action/util/hashtable.cpp:157 |
| 16 | Music_Event | 0x80065f10 | id >= 0x40 | FYI::INVALID MUSIC EVENT ID = %d | no | Music_Event | 0xcbce0 | high | silent | src/action/sound/music.cpp:7 - **already prints** |
| 17 | Txt_BindLabel | 0x80051fac | index >= NumEntries / bank not loaded (return value, not a print) | "Invalid Text Label" / "Not Loaded" | no | Txt_BindLabel | 0x6d460 | high | same | src/action/engine/Text.cpp:161 - **already present** |
| **Multiplayer / player** |
| 18 | MP_RegisterSpawnPoint | 0x800d32a0 | team == 2 (no team) → spawn not registered | Spawn points MUST have team assoc. with them! | no | MP_RegisterSpawnPoint | 0x9d2b0 | high | ? | src/action/game/mp/multiplayer.cpp:125 |
| 19 | Player_Start (FUN_800debe8) | 0x800debe8 | start positions exist but none usable (none with type 1) | MAJOR PROBLEM | no | Player_Start | 0xae870 | med-high (called from ResetMap_Load, calls Player_Init) | ? | src/action/game/obj/Player.cpp:264 |
| 20 | PlarStat_LogTimerPause (FUN_800e66f8) | 0x800e66f8 | playerNum >= 10 | ERROR : Invalid Plr ID | no | PlarStat_LogTimerPause | 0xb0570 | high | silent | src/action/game/sp/PlayerStats.cpp:131 - silent |
| 21 | PlarStat_LogTimerUnpause | 0x800e6748 | playerNum >= 10 | ERROR : Invalid Plr ID | no | PlarStat_LogTimerUnpause | 0xb0590 | high | silent | PlayerStats.cpp:140 - silent |
| 22 | PlrStat_LogHealth (FUN_800e6830) | 0x800e6830 | playerNum >= 10 | ERROR : Invalid Plr ID | no | PlrStat_LogHealth | 0xb0600 | high | silent | PlayerStats.cpp:119 - silent |
| 23 | PlrStat_LogEnemyDetectedPlayer (FUN_800e6b60) | 0x800e6b60 | playerNum >= 10 | ERROR : Invalid Plr ID | no | PlrStat_LogEnemyDetectedPlayer | 0xb07e0 | high (field +0x00 timesDetected) | silent | PlayerStats.cpp:107 - silent |
| 24 | PlrStat_LogEnemyDispatched (FUN_800e68a0) | 0x800e68a0 | playerNum >= 10 | ERROR : Invalid Plr ID | no | PlrStat_LogEnemyDispatched | 0xb0630 | high | silent | PlayerStats.cpp:83 - silent |
| 25 | PlrStat_LogEnemyDisabled (FUN_800e690c) | 0x800e690c | playerNum >= 10 | ERROR : Invalid Plr ID | no | PlrStat_LogEnemyDisabled | 0xb0670 | high | silent | PlayerStats.cpp:95 - silent |
| 26 | PlrStat_LogEnemySurrender (FUN_800e6978) | 0x800e6978 | playerNum >= 10 | ERROR : Invalid Plr ID | no | PlrStat_LogEnemySurrender | 0xb06b0 | high | silent | PlayerStats.cpp:71 - silent |
| 27 | PlrStat_GetScore (FUN_800e6e14) | 0x800e6e14 | current level not among the 12 scoring tables → return NULL | ERROR : No scoring table found for level 0x%x | no | PlrStat_GetScore | 0xb0900 | high | ? | PlayerStats.cpp:260 |
| 28 | PlrStat_GetScore | 0x800e6e14 | driving level (hash 0x09xxxxxx) and shared-score pointer NULL → return NULL | ERROR : No pointer to shared scoring data for level 0x%x | no | PlrStat_GetScore | 0xb0900 | high | ? | PlayerStats.cpp:260 |
| **UI** |
| 29 | Menu_UpgradeCheat (FUN_8011be74) | 0x8011be74 | could not unlock enough rewards to reach the requested level | cheat to upgrade %d to level %d failed | no | Menu_UpgradeCheat | 0x7cfb0 | med-high | ? | src/action/ui/ui_secrets.cpp:46 |
| **Objects / collision** |
| 30 | Switch_Create | 0x800fb64c | SP_Create(...) == NULL → control_delete_object | Unable to create switch at %f,%f,%f - check switch hashcodes | no | Switch_Create | 0xcfbd0 | high | ? | src/action/game/obj/Switch.cpp:53 |
| 31 | Coll_GetFreeHit (FUN_80105350) | 0x80105350 | hit heap empty and HitAllocCnt > 1000 → return NULL | Run out of hit datas!! Didn't forget to call Collide_FreeHitList | no | Coll_GetFreeHit | 0x29500 | high | silent | src/action/engine/Collide.cpp:40 - silent |
| **Platform graphics (fatal on GC)** |
| 32 | psiCreateMapTextures (FUN_8002fd0c) | 0x8002fd0c | running texture-header count > 0x500 | Too many textures headers, change max | **yes** | psiCreateMapTextures | 0xddf90 | high | dropped (Xbox Tex[] has 2048 slots and is unchecked) | src/action/engine/psiGraphics.cpp:252 |
| 33 | psiCreateEntityGfx (FUN_8002f864) | 0x8002f864 | display-list buffer Mem_Malloc failed | DL malloc failed | **yes** | psiCreateEntityGfx | 0xde220 | med (GC display lists have no Xbox equivalent) | n/a | psiGraphics.cpp:329 |

---

## B. Checks in Xbox functions not yet reimplemented (75) - add with the function

| # | GC function | GC addr | Condition | Message | Fatal? | Xbox function | Xbox addr | Conf | Xbox |
|---|---|---|---|---|---|---|---|---|---|
| **Level parse** |
| 34 | parsemap_parsenextblock | 0x80058c94 | next block pointer == current (zero-size block) | Fatal, messed up data blocks | no | parsemap_parsenextblock | 0xa6a80 | high | dropped (would loop forever) |
| 35 | parsemap_block_hashlist | 0x80058e5c | hash not in table, not loadable, not type 3/5, not 0x06000000 | HT_Script / HT_Skin 0x%x not loaded in HT_Level %x | no | parsemap_block_hashlist | 0xa5f80 | high | ? |
| 36 | parsemap_portal_data | 0x80059370 | a portal's cel is not found (GC then dereferences it anyway) | >>>FAILED<<< Portal Link unknown<->%s | no | parsemap_block_portal_data | 0xa6260 | high | ? |
| 37 | parsemap_block_map_data_static | 0x800598ac | placement glist hash not in hashtable | Entity not loaded (or broken) Hashcode=0x%x [%f,%f,%f]! | no | parsemap_block_map_data_static | 0xa6550 | high | ? |
| 38 | parsemap_block_map_data_dynamic | 0x80059a5c | glist == -1 and entityNum >= MapEntityCount → object skipped | Dynamic HT_Entity_Type 0x%.4x With uninit glist [%f,%f,%f] | no | parsemap_block_map_data_dynamic | 0xa66f0 | high | ? |
| 39 | parsemap_block_coll_data | 0x80059774 | old-format collision block present | 0x%x has old col data | no | none named (block id probably ignored by parsemap_handle_block_id 0xa6820) | - | low | ? |
| 40 | parsemap_block_particles → FUN_800c9358 | 0x800c9358 | emitter texture hash == -1 | EMITTER TEXTURE MISSING | no | parsemap_block_particles (inlined) | 0x68020 | med-high | ? |
| 41 | FUN_8005a760 (AI path node → cel link) | 0x8005a760 | build_FindCel(node pos) == NULL | FATAL: AIPath '%s' NODE (%d) IS NOT IN A ROOM: %f, %f, %f | no | AIPath_BindNodes (probably inlined) | 0xa72a0 | med | ? |
| 42 | AIBounds link (FUN_8005a9e0) | 0x8005a9e0 | build_FindCel(node pos) == NULL | same as 41 | no | AIBounds_Link2Cel | 0xa7160 | high | silent |
| 43 | Script_Load | 0x80060338 | script placement whose hash is not loaded (param_4 == 0) | Warning : map contains script 0x%x that is not loaded / not setup correctly xyz[...] | no | Script_Load | 0xc1550 | high | ? |
| **Animation** |
| 44 | AnimLoadFile | 0x80044830 | hash type not 0x04/0x05/0x06 | Invalid hash code %p | no | AnimLoadFile | 0x14a20 | high | ? |
| 45 | AnimProcessScriptData | 0x80044310 | script command type >= 5 | Script 0x%x invalid command 0x%x data 0x%x | no | AnimProcessScriptData | 0x110c0 | high | ? |
| 46 | AnimPostLoadInit | 0x80044a38 | skin 0x05000007 (Bond combat) not loaded → datum sharing skipped | ***** FATAL : Bondcombat skin must be loaded on all levels! ***** | no | AnimPostLoadInit | 0x11200 | high | ? |
| 47 | AnimObjectNew (FUN_800452fc) | 0x800452fc | obj NULL / hash not a skin / skin not loaded / any alloc fails → return 0 | Skin 0x%x not loaded | no | AnimObjectNew | 0x11440 | high | ? |
| 48 | AnimScriptInit (FUN_8004858c) | 0x8004858c | script numCmds == 0 | Script 0x%x has no commands | no | AnimScriptInit | 0x12620 | high | dropped |
| 49 | AnimScriptInit | 0x8004858c | numCmds > 0x20 (the fired-command mask is 32 bits) | Script 0x%x has too many commands | no | AnimScriptInit | 0x12620 | high | dropped |
| 50 | AnimScriptInit | 0x8004858c | obj/animState/script NULL, hash not type 6, or not loaded | Anim script 0x%x not loaded | no | AnimScriptInit | 0x12620 | high | silent |
| 51 | AnimProcessScriptCmds (FUN_80048f98) | 0x80048f98 | command 0: sequence hash not loaded → return | Anim Seq 0x%x not loaded | no | AnimProcessScriptCmds | 0x17c50 | high | ? |
| 52 | AnimProcessScriptCmds | 0x80048f98 | command type not 0..4 | Script 0x%x invalid command 0x%x data 0x%x | no | AnimProcessScriptCmds | 0x17c50 | high | ? |
| 53 | FUN_80049d90 (start a sequence for a script) | 0x80049d90 | hash not type 4, no slot, no skin, or bind failed → return 0 | Anim sequence 0x%x not loaded | no | FUN_00017960 | 0x17960 | high | silent |
| 54 | FUN_8004dda8 (bind sequence data) | 0x8004dda8 | hash not type 4 or not in hashtable → return 0 | Can't use animation hash 0x%x | no | FUN_00014420 | 0x14420 | high | ? |
| 55 | AnimGetBoneWorldTrans | 0x8004a6e8 | datum id not found in the skin's datum table → zero vector, identity matrix | Bad Datum in Skin 0x%x %d %d | no | AnimGetBoneWorldTrans | 0x13070 | high | ? |
| 56 | FUN_8004a9e8 (unposed variant) | 0x8004a9e8 | same as 55 | Bad Datum in Skin 0x%x %d %d | no | AnimGetBoneWorldTrans2 | 0x131c0 | med-high | ? |
| 57 | AnimSetInit (FUN_8004754c) | 0x8004754c | move-anim list has < 3 entries | Too few move anims | no | AnimSetInit | 0x17860 | high | dropped |
| 58 | AnimSetInit | 0x8004754c | idle-anim list has < 3 entries | Too few idle anims | no | AnimSetInit | 0x17860 | high | dropped |
| **Physics / sound / view** |
| 59 | RB_Build (FUN_8005ea54) | 0x8005ea54 | any moment-of-inertia component < 0 → return 0 | TOI_body cannot be negative. | no | RB_Build | 0xc00a0 | high | silent |
| 60 | RB_Build | 0x8005ea54 | inertia fails the triangle inequality → return 0 | TOI_body does not follow triangle rule. | no | RB_Build | 0xc00a0 | high | silent |
| 61 | Sound_Play (FUN_800652cc) | 0x800652cc | DynamicSoundList has no free node → return NULL | Summut hasnt freed there sound handle/or samples not finnished | no | Sound_Play | 0xcb750 | high | silent |
| 62 | View_CaptureSceneSub (FUN_800714fc) | 0x800714fc | viewer == NULL | ERROR Bad View | no | View_CaptureSceneSub | 0xda130 | high | silent |
| 63 | Camera_CheckLocation (FUN_8010b688) | 0x8010b688 | camera position in no cel and no previous cel → use first cel of world | Error : Camera cant be linked to a cel! | no | Camera_CheckLocation | 0x250a0 | med-high | ? |
| **Game flow / collision / bullets** |
| 64 | GameFlow_PushState | 0x80051420 | StackIndex+1 > 0x3f → index reset to 0 | **** STACK OVERFLOW **** | no | GameFlow_PushState | 0x6abf0 | high | silent (reset kept) |
| 65 | Collide_Update | 0x801071ec | hit heap count != HitAllocCnt after freeing all hit lists (leak) | Someone hasn't freed there hitlist! | no | Collide_Update | 0x2cca0 | high | dropped |
| 66 | Bullet_init | 0x80079b64 | direction vector pointer NULL → return NULL | Oops, null bullet vector | no | Bullet_init | 0x211f0 | high | ? |
| **Objects** |
| 67 | FUN_80082a74 (swing door setup) | 0x80082a74 | door model's longer axis is not x/y | ERROR : Swing door is not modelled on the xy axis : %s | no | Door_SetupSwing | 0x2ec30 | med-high | ? |
| 68 | MiniSub_Create | 0x800d04f4 | AnimObjectNew(obj, 0x05000074) failed | Cannot init MiniSub skin | no | MiniSub_Create | 0x9bcd0 | high | ? |
| **AI / drones** |
| 69 | FUN_800847c4 (corner cover node) | 0x800847c4 | NumCoverNodes > 0xff; GC still writes the entry (overflow) | Too many cover nodes | no | Drone_CoverCornerNode | 0x2fe20 | med-high | dropped |
| 70 | FUN_80084ae8 (low cover node) | 0x80084ae8 | NumCoverNodes > 0xff (writes anyway) | Too many cover nodes | no | Drone_CoverLowNode | 0x30000 | med-high | ? (likely dropped) |
| 71 | FUN_80084d0c (AI point) | 0x80084d0c | NumAIPoints > 0xff (writes anyway) | Too many AIPoints | no | Drone_AIPoint | 0x30190 | med-high | dropped |
| 72 | FUN_80085400 (spawner init) | 0x80085400 | group slot already 0xffff | ERROR: More than one DroneSpawner has the same group | no | DroneSpawner_Init | 0x30670 | med-high | ? |
| 73 | FUN_80085400 | 0x80085400 | either Mem_Malloc for the spawner lists failed | Couldn't allocate memory for DroneSpawner (group %d) | no | DroneSpawner_Init | 0x30670 | med-high | ? |
| 74 | NDrone2_DefaultInit | 0x80097e84 | AnimObjectNew(o, skin) == 0 | Could not create AnimObjectNew(o,0x%08x) | no | NDrone2_DefaultInit | 0x417f0 | high | ? |
| 75 | FUN_80099554 | 0x80099554 | behaviours pointer NULL | NULL behaviours pointer! | no | NDrone2_DoModeSettingsNEW | 0x41230 | high (only caller of behaviour_util_get) | ? |
| 76 | FUN_80099554 | 0x80099554 | *behaviours == 0 although the placement says there is data | No behaviour data stored but placement param claims there is! | no | NDrone2_DoModeSettingsNEW | 0x41230 | high | ? |
| 77 | FUN_80099554 | 0x80099554 | behaviours pointer NULL (second test) | Drone has no behaviour data! | no | NDrone2_DoModeSettingsNEW | 0x41230 | high | ? |
| 78 | FUN_8004e46c | 0x8004e46c | mode >= 0x11, or sub-fields out of range (<6, 1..3, 1..0x5b) → reset to defaults, return 0 | Invalid behaviour data! | no | behaviour_util_get | 0x194d0 | high | silent (defaults kept) |
| 79 | FUN_800a504c (init emitter) | 0x800a504c | no cel given and build_FindCel fails (+ Euroland coords if flag) | ERROR: AIPoint or Corner Cover not in cel - adjust position | no | AINetwork_InitEmitter | 0x4a870 | med-high | ? |
| 80 | FUN_800a504c | 0x800a504c | no nav path, or path has 0 nodes, near the emitter | NO NAVIGATION PATH NEAR EMITTER (AIPOINT/COVER) + Emitter cel: %s | no | AINetwork_InitEmitter | 0x4a870 | med-high | ? |
| 81 | FUN_800a5180 (init emitter, lazy alloc) | 0x800a5180 | same two conditions as 79/80 | same messages | no | AINetwork_InitEmitter2 | 0x4a900 | med | ? |
| 82 | FUN_800a5388 (emit path) | 0x800a5388 | link lookup returns < 0 (GC keeps going with the negative index) | Link refers to non-existant AI path | no | AINetwork_EmitPath | 0x49d70 | med | ? |
| 83 | FUN_800a5738 (node at distance) | 0x800a5738 | link lookup returns < 0 | Link refers to non-existant AI path | no | AINetwork_Emitter_GetNodeAtDistance | 0x4a070 | med | ? |
| 84 | FUN_800a2eb8 (A* route) | 0x800a2eb8 | backtrack chain longer than 100000 | Likely infinite loop | no | AINetwork_DoAStarPath | 0x48720 | med | ? |
| 85 | FUN_800a2eb8 | 0x800a2eb8 | link lookup returns < 0 | Could not find path link | no | AINetwork_DoAStarPath | 0x48720 | med | ? |
| 86 | FUN_800a0870 (patrol route setup) | 0x800a0870 | a patrol path node's link count is not 2 on a looping path | Patrol Path '%s' is NOT looping | no | patrol init (caller FUN_800a0f98), probably NDrone2_AssignAIPath | 0x46ad0 | low | ? |
| 87 | FUN_800c3e14 (post AI message) | 0x800c3e14 | message pool empty (GC then writes through NULL) | Too many AI messages | no | drone state-machine message post (unnamed) | - | low | ? |
| 88 | FUN_800c3fa8 (state-machine dispatch) | 0x800c3fa8 | > 1000 state changes in one dispatch | Possible infinite loop | no | Drone_SM dispatcher (unnamed) | - | low | ? |
| 89 | FUN_80088e84 (set drone anim script) | 0x80088e84 | the drone's script slot (+0x42c) is still NULL after setup | Couldn't set script 0x%08x | no | DroneAnim_SetScript? | 0x34680 | low-med | ? |
| 90 | FUN_80089e54 (cover anim chooser) | 0x80089e54 | no corner-cover behaviour is available | Doing CornerCover with no behaviour | no | DroneAnim_CoverAnim? | 0x34750 | low-med | ? |
| 91 | FUN_80089678 (drone anim event callback) | 0x80089678 | event 2 and drone weapon count < 2 | ERROR: Drone has no second weapon | no | not matched | - | - | ? |
| 92 | FUN_800b032c (behaviour handler, table 0x801fec88) | 0x800b032c | msg 1 and drone has no second weapon | ERROR: Drone has no second weapon | no | not matched | - | - | ? |
| **Player / weapons / rewards** |
| 93 | FUN_800f6760 (give player weapon) | 0x800f6760 | weapon has no skin hash / skin not loaded → return 0 | FYI: Weapon %d has no skin / FYI: Weapon %d not loaded | no | not matched (unnamed on Xbox) | - | - | ? |
| 94 | FUN_80078620 (second give-weapon path) | 0x80078620 | same as 93 | same | no | not matched | - | - | ? |
| 95 | FUN_800f6ec4 (set upgrade) | 0x800f6ec4 | upgrade index (param/3) >= 9 | Invalid upgrade %d | no | Set_Upgrade | 0xb9e30 | med | ? |
| 96 | FUN_800e7834 (reward lookup) | 0x800e7834 | medal type >= 5 | ERROR : Invalid medal type | no | PlrStarts_ProcessRewardCounter | 0xb1740 | med-high | ? |
| 97 | FUN_800e7834 | 0x800e7834 | level hash not among the 12 reward rows | ERROR : Reward level 0x%x unknown | no | PlrStarts_ProcessRewardCounter | 0xb1740 | med-high | ? |
| 98 | FUN_800e7834 | 0x800e7834 | reward type >= 5 | ERROR : Invalid reward type | no | PlrStarts_ProcessRewardCounter | 0xb1740 | med-high | ? |
| 99 | FUN_800e7834 | 0x800e7834 | reward data >= 0x3f (bit index into a 64-bit mask) | ERROR : Invalid reward data | no | PlrStarts_ProcessRewardCounter | 0xb1740 | med-high | ? |
| 100 | FUN_800e6af4 | 0x800e6af4 | playerNum >= 10 | ERROR : Invalid Plr ID | no | PlrStat_LogShotFired | 0xb07a0 | high | silent |
| 101 | FUN_800e69e4 | 0x800e69e4 | playerNum >= 10 | ERROR : Invalid Plr ID | no | PlrStat_LogShotHitEnemy | 0xb06f0 | high | silent |
| 102 | FUN_800e6a64 | 0x800e6a64 | playerNum >= 10 | ERROR : Invalid Plr ID | no | PlrStat_LogShotHitScenery | 0xb0740 | high | silent |
| 103 | PlarStat_LogUpdateElapsedTime | 0x800e67b0 | playerNum >= 10 | ERROR : Invalid Plr ID | no | PlarStat_LogUpdateElapsedTime | 0xb05c0 | high | ? |
| 104 | PlrStat_DoneBondMoment | 0x800e664c | playerNum >= 10 | ERROR : Invalid Plr ID | no | PlrStat_DoneBondMoment | 0xb0500 | high | silent |
| 105 | FUN_80055088 (options save-block reader) | 0x80055088 | saved options version field != 4 | Saved file out-of-date - delete it | no | not matched (Xbox save/profile code) | - | - | ? |
| **Platform code with an Xbox counterpart (GC fatal reporter)** |
| 106 | psiBuildMatrixPalette | 0x8000d0b8 | skeleton bone count > 170 (GC locked-cache limit) | Can't use LC with more than 170 bones | **yes** | psiBuildMatrixPalette | 0xdd290 | high (function); the limit is GC-specific | n/a |
| 107 | FUN_8000d4dc (game heap alloc) | 0x8000d4dc | OS alloc of the 0x13b3000-byte game heap failed | Not Enough Memory for Game Heap | **yes** | psiMem_Init? | 0xdc870 | low-med | ? |
| 108 | psiLaunchDriving | 0x8002c324 | driving ELF larger than the buffer | NOT ENOUGH MEM TO LOAD NEXT EXE | **yes** | psiLaunchDriving | 0xdfb50 | high (function); the check does not apply (Xbox uses XLaunchNewImage) | n/a |

---

## C. GC-only platform checks, no Xbox counterpart (46 fatal-reporter sites)

All go through `platform_fatal_error_handler` (fatal). Listed by file:
- **N_texPalette.cpp**: FUN_80033804 "invalid version number for texture palette" (magic != 0x20af30).
- **GC ARAM.cpp**: FUN_80033ba0 "ARAM<->MRAM transfer invalid length/alignment" (not 32-aligned); FUN_80033cc0 "ARAM Full"; FUN_80033e24 "ARAM: Write full"; FUN_80033ee4 (ARAM alloc overflow, message at 0x801a1074).
- **GC Init.cpp**: FUN_8003c4c8 "Cannot allocate XFBs".
- **GC FontInc.cpp**: FUN_80035470 "Not aligned".
- **GC MemCard.cpp**: FUN_8003d70c "CARD: can't init, operation still in progress"; FUN_8003dd30 "CARD Work Area not allocated" / "CARD: pushed state undefined" / "Card Operation undefined"; FUN_8003cfa0 "GAME DATA TOO LARGE FOR DUP STRUCTURE" (> 0x180 bytes); FUN_8003da5c "CARD: No Option given".
- **GC VideoMode.cpp / Gamecubemain.cpp**: FUN_80040fbc "DEMOInit: invalid TV format"; FUN_8000dd18 "NO TV TYPE SET".
- **GC HVQM.cpp / HVQM4PlayerEx.c** (FMV): FUN_8003b530 "asend_buff alloc error !"; FUN_800417b4 "decv_open(), buff[0..2].bufv set error !".
- **GC PsiDraw.cpp**: maybe_psiDrawClonedObject (0x8002ea08) "DL Buffer not large enough". The same function also silently caps its draw list at 0x400 entries.
- **GC GX.cpp**: FUN_80036244 and FUN_80038460 "Wot?" (blend mode not 0..2); FUN_80036d40 "GCD TEV MODE NOT YET IMPLEMENTED".
- **GC Fifo.cpp**: FUN_800343ec, FUN_80034474, GcFifoMaybeSync, GcFifoDoSomething "OSHalt"; FUN_800344fc "OSHalted here."
- **GC Particles.cpp**: FUN_8002884c "PCL DL NOT ENOUGH MEM COULD NOT CREATE DL" / "PCL DL NOT ENOUGH MEM" (0x16800-byte particle DL).
- **GC File.cpp**: FUN_80034d10 "DVD Streams: Too many streams Open" (max 4); FUN_80034c6c "DVD Streams: Callback says 'huh?'".
- **GC PsiSound.cpp**: FUN_80032898 "AX Streams: Too many streams Open" (max 4); FUN_80032da4 "Stream: incorrect buffer copy size", "incorrect copy buffer offset", "Can't allocate stream buffer".
- **GC AudioStreaming.cpp**: FUN_8000e7f4 "Callback failure"; FUN_8000e87c "File not found", "Can't prepare stream".
- **GC RunElf.cpp**: FUN_8003f320 "Invalid ELF file"; FUN_8003f5ec "Elf won't fit in ARAM".
- **Dolphin SDK DVD asserts** that go through the same reporter: FUN_8016143c (DVDConvertEntrynumToPath), FUN_80161ca0 (DVDReadAsync x2), FUN_80161d90 (DVDRead x2), FUN_80162264 (DVDPrepareStreamAsync x3).

---

## D. Debug output that is not a check (skipped)

- LoaderLoad: `printf("%s\n", filename)` echoes each level bin it opens.
- LoaderProcess: file type 10 → `">>>> LOADER : %s"` (loader text message embedded in the level).
- Mem_PrintAllInfo (FUN_80057104): per-type sizes, "InUse", "Blocks", "Largest free block" dump. Mem_Info (FUN_800572d4): "Used %dMB".
- FUN_80063c28 and C_NIS_Handler: "Script 0x%x:%d loading..."; Script_Free (FUN_80062dec): "Script 0x%x unloading...".
- AINetwork_AllocEmitter (FUN_800a52b8) and AINetwork_FreeEmitter (FUN_800a5324): address traces.
- Pickup_MakeRandomWeaponSet: "Random weapon %d = %x".
- Set_Upgrade (FUN_800f6ec4): "Player %d upgrade %d set to level %d".
- FUN_80124df0: "SFXLanguage_English/French/German/Spanish".
- FUN_8012eadc (MP debrief): printf of a number format at 0x801b4ab8.
- Player_CollisionHandler: "Fall damage = %d".
- dbg_printf: FUN_8003f770 mono/stereo channel trace; FUN_8014635c "Int mode enabled"; FS_Try_Load_From_Server "File server function not available"; FUN_8003bed8 "Exception in:%s" (gc/Nightfire.map lookup); FUN_8003c0bc custom DSI register/stack dump; platform_fatal_error_handler's own "in %s on line %d" and backtrace; OS exception handlers FUN_80155718 and FUN_801550b0; SDK banners and OSCheckHeap/OSCheckActiveThreads/OSCheckAlarmQueue asserts in FUN_8015203c, FUN_80162554, FUN_8016650c, FUN_80168f00, FUN_8018ca04, FUN_8019da34, FUN_8019dd60, FUN_801620ec, FUN_801617a4 and unnamed code at 0x80154990-0x80154a50 and 0x8015c1cc/0x801466d8/0x80147cf8.
- Strings with no references: "Warning trying to free too much memory" (0x801a6c08), "ERROR : too many rewards" (0x801ab12c).
