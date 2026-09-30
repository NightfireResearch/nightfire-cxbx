# M_ITEM lists in the action engine (default.xbe)

Survey of every `M_ITEM` array (0x18 bytes: icon, title, description, identifier, enabled, disabled description; `src/action/ui/ui.h`).
Found by scanning `.data` for runs of item-shaped records and by following every immediate that points into them from `.text`
(tools in the survey scratchpad: `scan.py`, `refs.py`). All twelve static lists are one contiguous block, 0x17c580-0x17d540 (168 items), in the
same order as on the PS2 (`/PS2_EU_51258/ACTION.ELF` 0x2e0108-0x2e10c8, whose symbols give the names). The block ends at the `codename` save
structure (0x17d540). Nothing else in `.data` is an M_ITEM list; one more list (`mp_stuff`) is built at run time in BSS.

Text is the UK English bank (`tools/ui/text_bank.py`). Names: `assets.h` name if there is one, else the Ghidra enum name, else a proposal
(marked *). `enabled` 1 = selectable without unlocks.

## Summary

| Address | Name | Items | In the code | Modified at run time |
|---|---|---|---|---|
| 0x17c580 | `sp_level` | 12 | Menu.h #define only (data still read from the XBE) | yes |
| 0x17c6a0 | `mp_level` | 8 | ui_mp.cpp (matches) | no |
| 0x17c760 | `difficulty` | 3 | not in the code | no |
| 0x17c7a8 | `mp_scenario` | 13 | ui_mp.cpp (disabled label wrong: uses 0x1ab instead of 0x010000ab) | yes |
| 0x17c8e0 | `mp_characters` | 29 | not in the code | yes |
| 0x17cb98 | `mp_characters_small` | 29 | not in the code | yes |
| 0x17ce50 | `mp_options` | 5 | not in the code | yes |
| 0x17cec8 | `cn_options` | 7 | not in the code | yes |
| 0x17cf70 | `ds_options` | 4 | ui_dossier.cpp (matches) | no |
| 0x17cfd0 | `mp_bots` | 17 | not in the code (Ghidra Xbox names it mp_characters too; PS2 name mp_bots) | yes |
| 0x17d168 | `ds_weapons` | 27 | ui_dossier.cpp (matches) | yes |
| 0x17d3f0 | `ds_gadgets` | 14 | ui_dossier.cpp (matches) | yes |
| 0x245338 | `mp_stuff` | 29 | not in the code | yes |

The scan also turned up immediates that look like pointers into the block from `Mem_Info`, `Page_Update`, `View_CaptureSceneSub` and
`Menu_SetupCredits`; they are either neighbouring globals (below 0x17c580, above 0x17d540) or bytes of unrelated instructions (call offsets,
pushed constants), not list references.

## Problems found in existing names and code

- `MP_SCENARIO_LOCKED = 0x1ab` (assets.h) reads "Fixed Gun Emplacements" (the Enviro-Mods label). The disabled description of every `mp_scenario`
  item in the XBE is **0x010000ab** "This scenario is locked." (Ghidra: `GM_LOCKED`). `ui_mp.cpp` therefore shows the wrong text for locked
  scenarios; the label should be renamed (e.g. `MP_ENVIRO_GUNEMPLACEMENTS = 0x1ab`) and a `GM_LOCKED = 0x010000ab` added.
- `REWARD_UPGRADE_MAGNIFICATION_AND_BIOTARGET = 0x3b4` reads just "Upgrade: Increased magnification." (same text as 0x3b1); the name promises more than the string.
- `MP_CFG_OR_DOSSIER_WEAPONS = 0x19c` "Weapons": Ghidra calls it `MP_CFG_WEAPSET`.
- Ghidra names both 0x17c8e0 and 0x17cfd0 `mp_characters`; the second is the bot list (`mp_bots` on the PS2).
- `SUB_C_MPPLAYERMODS_*`, `SUB_C_MPENVIROMODS_*`, `SUB_C_MPSCENARIO_IRIS`, `SUB_C_DSGADGETS_IRIS` carry a C_ prefix but belong to pages (P_MPPLAYERMODS,
  P_MPENVIROMODS) or are shared by a page and its wheel (C_SBMPSCEN, C_SBDSGTSCROLL); harmless, but not the SUB_<handler> pattern.
- `SUB_C_MP_IRIS = 0x10000105` is only the iris of P_MPMAP / C_SBMPMAP; in the SUB_<handler> style it would be `SUB_C_SBMPMAP_IRIS`.
- Ghidra's `Action_TranslatedText` enum has no unique names for many bank-1 values, so the decompiler prints OR-combinations such as
  `QUIT_CONFIRMATION|GM_TOPAGENT_DESC` (= 0x3d4 "Map"); read label values from the disassembly.

## `sp_level` - 0x17c580, 12 items

- PS2: 0x2e0108
- In the code: Menu.h #define only (data still read from the XBE)
- Identifier: level hashcode (HT_Level_* / HT_Level_Driving_*)
- Modified at run time: yes: .enabled is the campaign unlock state. Written by Menu_SetNightfireStatus (0x762d0, from the saved bitmask), Menu_SetLevelBonus (0x7cbf0, unlocks the next mission) and Menu_SpecialCodenameCheck (0x7d110, cheat unlock); read back into the save by Menu_GetNightfireStatus (0x76240).
- Users: C_SBNFMAP_Handler (wheel, select); P_NFMAP_Handler (last unlocked item); P_DSRECORDS_Handler / P_DSREWARDS_Handler (Menu_AddItemsToControl); Menu_GetLevelIndex 0x76310; Menu_GetNightfireStatus 0x76240; Menu_SetNightfireStatus 0x762d0; Menu_SetBonus 0x7cb10; Menu_SetLevelBonus 0x7cbf0; Menu_UnlockMPSettings 0x7c9b0; Menu_UnlockMPSkins 0x7c680; Menu_GetObjectUpgradeLevel 0x7ced0; Menu_UpgradeCheat 0x7cfb0; Menu_SpecialCodenameCheck 0x7d110

| # | Icon | Title | Description | Identifier | En | Disabled text |
|---|---|---|---|---|---|---|
| 0 | 0x3000085 `ICON_SPMAP_PARISPRELUDE`* | 0x3000001 `MIS1_NAME` "Paris Prelude" | 0x3000013 `MIS1_DESC` "Prevent a rogue faction from putting a dampener on the Ne..." | HT_Level_Driving_Paris | 1 | - |
| 1 | 0x3000084 `ICON_SPMAP_EXCHANGE`* | 0x3000002 `SPMAP_EXCHANGE_NAME`* "The Exchange" | 0x3000014 `SPMAP_EXCHANGE_DESC`* "Drop in on Raphael Drake's reception and rendezvous with ..." | HT_Level_CastleExterior | 1 | - |
| 2 | 0x30000b0 `ICON_SPMAP_ALPINEESCAPE`* | 0x3000003 `SPMAP_ALPINEESCAPE_NAME`* "Alpine Escape" | 0x3000015 `SPMAP_ALPINEESCAPE_DESC`* "Make your escape with Zoe on an armoured snowmobile." | HT_Level_Driving_SnowMobile | 0 | 0x1000009 `SPMAP_LOCKED`* "Continue playing to unlock this mission." |
| 3 | 0x30000b2 `ICON_SPMAP_ENEMIESVANQUISHED`* | 0x3000004 `SPMAP_ENEMIESVANQUISHED_NAME`* "Enemies Vanquished" | 0x3000016 `SPMAP_ENEMIESVANQUISHED_DESC`* "Race to meet Q at the extraction point." | HT_Level_Driving_Alps | 0 | 0x1000009 `SPMAP_LOCKED`* "Continue playing to unlock this mission." |
| 4 | 0x300008c `ICON_SPMAP_DOUBLECROSS`* | 0x3000005 `SPMAP_DOUBLECROSS_NAME`* "Double Cross" | 0x3000017 `SPMAP_DOUBLECROSS_DESC`* "Rendezvous with Alexander Mayhew, a traitor in Drake's or..." | HT_Level_HendersonA | 0 | 0x1000009 `SPMAP_LOCKED`* "Continue playing to unlock this mission." |
| 5 | 0x300008e `ICON_SPMAP_NIGHTSHIFT`* | 0x3000006 `SPMAP_NIGHTSHIFT_NAME`* "Night Shift" | 0x3000018 `SPMAP_NIGHTSHIFT_DESC`* "Covertly access Mayhew’s headquarters and retrieve data f..." | HT_Level_TowerA | 0 | 0x1000009 `SPMAP_LOCKED`* "Continue playing to unlock this mission." |
| 6 | 0x300008d `ICON_SPMAP_CHAINREACTION`* | 0x3000007 `SPMAP_CHAINREACTION_NAME`* "Chain Reaction" | 0x3000019 `SPMAP_CHAINREACTION_DESC`* "Infiltrate a nuclear power plant in the process of being ..." | HT_Level_PowerStationA1 | 0 | 0x1000009 `SPMAP_LOCKED`* "Continue playing to unlock this mission." |
| 7 | 0x300008f `ICON_SPMAP_PHOENIXFIRE`* | 0x3000009 `SPMAP_PHOENIXFIRE_NAME`* "Phoenix Fire" | 0x300001b `SPMAP_PHOENIXFIRE_DESC`* "Escape Kiko's trap and get out of the skyscraper alive." | HT_Level_Tower2A | 0 | 0x1000009 `SPMAP_LOCKED`* "Continue playing to unlock this mission." |
| 8 | 0x30000ae `ICON_SPMAP_DEEPDESCENT`* | 0x300000c `SPMAP_DEEPDESCENT_NAME`* "Deep Descent" | 0x300001e `SPMAP_DEEPDESCENT_DESC`* "Reach Drake's private island undetected." | HT_Level_Driving_Underwater | 0 | 0x1000009 `SPMAP_LOCKED`* "Continue playing to unlock this mission." |
| 9 | 0x30000af `ICON_SPMAP_ISLANDINFILTRATION`* | 0x300000e `SPMAP_ISLANDINFILTRATION_NAME`* "Island Infiltration" | 0x3000020 `SPMAP_ISLANDINFILTRATION_DESC`* "Destroy the air defence system that protects the island f..." | HT_Level_Driving_JungleA | 0 | 0x1000009 `SPMAP_LOCKED`* "Continue playing to unlock this mission." |
| 10 | 0x3000151 `ICON_SPMAP_COUNTDOWN`* | 0x3000034 `SPMAP_COUNTDOWN_NAME`* "Countdown" | 0x3000035 `SPMAP_COUNTDOWN_DESC`* "Covertly make your way into the secret underwater base an..." | HT_Level_EvilBase | 0 | 0x1000009 `SPMAP_LOCKED`* "Continue playing to unlock this mission." |
| 11 | 0x3000142 `ICON_SPMAP_EQUINOX`* | 0x3000011 `SPMAP_EQUINOX_NAME`* "Equinox" | 0x3000023 `SPMAP_EQUINOX_DESC`* "Prevent a hostile takeover of the missile defence platfor..." | HT_Level_SpaceStationD | 0 | 0x1000009 `SPMAP_LOCKED`* "Continue playing to unlock this mission." |

## `mp_level` - 0x17c6a0, 8 items

- PS2: 0x2e0228
- In the code: ui_mp.cpp (matches)
- Identifier: level hashcode (HT_Level_*)
- Modified at run time: no
- Users: C_SBMPMAP_Handler (wheel); P_MPMAP_Handler (Menu_SelectItemInControl); C_SBMPSCEN_Handler (Quick Game: random entry of the first 7); P_MPCONFIRM_Handler (Menu_GetItemFromHash, map name)

| # | Icon | Title | Description | Identifier | En | Disabled text |
|---|---|---|---|---|---|---|
| 0 | 0x3000086 `ICON_MPMAP_SKYRAIL` | 0x3000024 `MPMAP_SKYRAIL_NAME` "Skyrail" | 0x3000026 `MPMAP_SKYRAIL_DESC` "Use the cable car for a tactical advantage at this desert..." | HT_Level_SkyRail | 1 | - |
| 1 | 0x300008b `ICON_MPMAP_FORTKNOX` | 0x3000025 `MPMAP_FORTKNOX_NAME` "Fort Knox" | 0x3000027 `MPMAP_FORTKNOX_DESC` "Life is cheap within the walls of Fort Knox, but the acti..." | HT_Level_FortKnox | 1 | - |
| 2 | 0x30000ba `ICON_MPMAP_SNOWBLIND` | 0x3000028 `MPMAP_SNOWBLIND_NAME` "Snow Blind" | 0x3000029 `MPMAP_SNOWBLIND_DESC` "Expect a cold reception in this extensive maze of medieva..." | HT_Level_SnowBlind | 1 | - |
| 3 | 0x30000b9 `ICON_MPMAP_PHOENIXBASE` | 0x300002a `MPMAP_PHOENIXBASE_NAME` "Phoenix Base" | 0x300002b `MPMAP_PHOENIXBASE_DESC` "Battle in Drake's secret underwater base." | HT_Level_StealthShip | 1 | - |
| 4 | 0x30000b8 `ICON_MPMAP_ATLANTIS` | 0x300002c `MPMAP_ATLANTIS_NAME` "Atlantis" | 0x300002d `MPMAP_ATLANTIS_DESC` "Join the feeding frenzy inside the classic Atlantis ocean..." | HT_Level_Atlantis | 1 | - |
| 5 | 0x30000b7 `ICON_MPMAP_SILO` | 0x300002e `MPMAP_SILO_NAME` "Missile Silo" | 0x300002f `MPMAP_SILO_DESC` "Blast your way through four floors of opposition inside t..." | HT_Level_MissileSilo | 1 | - |
| 6 | 0x30000b6 `ICON_MPMAP_SUBPEN` | 0x3000030 `MPMAP_SUBPEN_NAME` "Sub Pen" | 0x3000031 `MPMAP_SUBPEN_DESC` "Dive into this sub pen and the submarine itself in search..." | HT_Level_SubPen | 1 | - |
| 7 | 0x30000b5 `ICON_MPMAP_RAVINE` | 0x3000032 `MPMAP_RAVINE_NAME` "Ravine" | 0x3000033 `MPMAP_RAVINE_DESC` "Ride the cable cars between opposing cliff-top bases. (Bo..." | HT_Level_Ravine | 1 | - |

## `difficulty` - 0x17c760, 3 items

- PS2: 0x2e02e8
- In the code: not in the code
- Identifier: difficulty level 1..3, stored in GameState.difficultyModifier
- Modified at run time: no
- Users: C_SBNFDFCTY_Handler (wheel, select)

| # | Icon | Title | Description | Identifier | En | Disabled text |
|---|---|---|---|---|---|---|
| 0 | 0x30000a5 `ICON_DIFFICULTY_OPERATIVE`* | 0x14d `DIFFICULTY_OPERATIVE` "Operative" | 0x1000006 `DIFFICULTY_OPERATIVE_DESC` "For trainee agents. Enemies are fewer, weaker, and deal l..." | 1 (DIFFICULTY_OPERATIVE) | 1 | - |
| 1 | 0x30000a6 `ICON_DIFFICULTY_AGENT`* | 0x14e `DIFFICULTY_AGENT` "Agent" | 0x1000007 `DIFFICULTY_AGENT_DESC` "For skilled agents. Enemies have their usual characterist..." | 2 (DIFFICULTY_AGENT) | 1 | - |
| 2 | 0x30000a7 `ICON_DIFFICULTY_00AGENT`* | 0x14f `DIFFICULTY_00AGENT` "00 Agent" | 0x1000008 `DIFFICULTY_00AGENT_DESC` "For 00 agents only. Enemies are tougher,  deadlier, and m..." | 3 (DIFFICULTY_00AGENT) | 1 | - |

## `mp_scenario` - 0x17c7a8, 13 items

- PS2: 0x2e0330
- In the code: ui_mp.cpp (disabled label wrong: uses 0x1ab instead of 0x010000ab)
- Identifier: MultiplayerGameMode: mode bit | 0x20000000 TEAMGAME | 0x40000000 (hill/zone modes: KOTH, Uplink, Team KOTH)
- Modified at run time: yes: Menu_UnlockMPSettings (0x7c9b0) clears then sets .enabled of items 4, 6, 7, 9, 10, 12 from reward objIds 0x39, 0x3b, 0x3c, 0x3d, 0x38, 0x3a.
- Users: C_SBMPSCEN_Handler (wheel, select); P_MPSCENARIO_Handler (Menu_SelectItemInControl); P_MPCONFIRM_Handler (Menu_GetItemFromHash, scenario name); Menu_UnlockMPSettings 0x7c9b0

| # | Icon | Title | Description | Identifier | En | Disabled text |
|---|---|---|---|---|---|---|
| 0 | 0x300013a `ICON_MPSCENARIO_QUICKGAME` | 0x10002dd `MPSCENARIO_QUICKGAME_NAME` "Quick Game" | 0x10002de `MPSCENARIO_QUICKGAME_DESC` "Three enemies await you in the arena." | GM_QUICK | 1 | 0x10000ab `GM_LOCKED` "This scenario is locked." |
| 1 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x1cb `MPSCENARIO_ARENA_NAME` "Arena" | 0x1cc `MPSCENARIO_ARENA_DESC` "Death match involving free for all combat" | GM_ARENA | 1 | 0x10000ab `GM_LOCKED` "This scenario is locked." |
| 2 | 0x300009a `ICON_MPSCENARIO_TEAMARENA` | 0x1cd `MPSCENARIO_TEAMARENA_NAME` "Team Arena" | 0x1ce `MPSCENARIO_TEAMARENA_DESC` "Same as Arena except players are grouped into teams" | GM_TEAMARENA | 1 | 0x10000ab `GM_LOCKED` "This scenario is locked." |
| 3 | 0x300009b `ICON_MPSCENARIO_CTF` | 0x1cf `MPSCENARIO_CTF_NAME` "Capture The Flag" | 0x1d0 `MPSCENARIO_CTF_DESC` "Steal the enemy flag and return to base" | GM_CTF | 1 | 0x10000ab `GM_LOCKED` "This scenario is locked." |
| 4 | 0x30000a4 `ICON_MPSCENARIO_UPLINK` | 0x1d1 `MPSCENARIO_UPLINK_NAME` "Uplink" | 0x1d2 `MPSCENARIO_UPLINK_DESC` "Activate satellites around the level with your team" | GM_UPLINK | 0 | 0x10000ab `GM_LOCKED` "This scenario is locked." |
| 5 | 0x300009c `ICON_MPSCENARIO_TOPAGENT` | 0x1d3 `MPSCENARIO_TOPAGENT_NAME` "Top Agent" | 0x1d4 `MPSCENARIO_TOPAGENT_DESC` "Run out of lives and you're out of the game!" | GM_TOPAGENT | 1 | 0x10000ab `GM_LOCKED` "This scenario is locked." |
| 6 | 0x300009d `ICON_MPSCENARIO_DEMOLITION` | 0x1d7 `MPSCENARIO_DEMOLITION_NAME` "Demolition" | 0x1d8 `MPSCENARIO_DEMOLITION_DESC` "MI6 attack a site whilst Phoenix defend" | GM_DEMOLITION | 0 | 0x10000ab `GM_LOCKED` "This scenario is locked." |
| 7 | 0x300009e `ICON_MPSCENARIO_PROTECTION` | 0x1d9 `MPSCENARIO_PROTECTION_NAME` "Protection" | 0x1da `MPSCENARIO_PROTECTION_DESC` "Phoenix attack a site whilst MI6 defend" | GM_PROTECTION | 0 | 0x10000ab `GM_LOCKED` "This scenario is locked." |
| 8 | 0x300009f `ICON_MPSCENARIO_ESPIONAGE` | 0x1db `MPSCENARIO_ESPIONAGE_NAME` "Industrial Espionage" | 0x1dc `MPSCENARIO_ESPIONAGE_DESC` "Retrieve the blueprints and return to base" | GM_BLUEPRINT (Industrial Espionage) | 1 | 0x10000ab `GM_LOCKED` "This scenario is locked." |
| 9 | 0x30000a0 `ICON_MPSCENARIO_GOLDENEYE` | 0x1dd `MPSCENARIO_GOLDENEYE_NAME` "GoldenEye Strike" | 0x1de `MPSCENARIO_GOLDENEYE_DESC` "Collect the GoldenEye controls to eliminate the opposing ..." | GM_GOLDENEYE | 0 | 0x10000ab `GM_LOCKED` "This scenario is locked." |
| 10 | 0x30000a1 `ICON_MPSCENARIO_ASSASSIN` | 0x1df `MPSCENARIO_ASSASSIN_NAME` "Assassination" | 0x1e0 `MPSCENARIO_ASSASSIN_DESC` "The assassin must eliminate the random target" | GM_ASSASSIN | 0 | 0x10000ab `GM_LOCKED` "This scenario is locked." |
| 11 | 0x30000a2 `ICON_MPSCENARIO_KOTH` | 0x1e1 `MPSCENARIO_KOTH_NAME` "King of the Hill" | 0x1e2 `MPSCENARIO_KOTH_DESC` "Stay inside the designated area to earn points" | GM_KOTH | 1 | 0x10000ab `GM_LOCKED` "This scenario is locked." |
| 12 | 0x30000a3 `ICON_MPSCENARIO_TEAMKOTH` | 0x1e3 `MPSCENARIO_TEAMKOTH_NAME` "Team King of the Hill" | 0x1e4 `MPSCENARIO_TEAMKOTH_DESC` "Stay inside the designated area to earn points for your team" | GM_TEAMKOTH | 1 | 0x10000ab `GM_LOCKED` "This scenario is locked." |

## `mp_characters` - 0x17c8e0, 29 items

- PS2: 0x2e0468
- In the code: not in the code
- Identifier: character / skin index 0..28 (BOT_getDefaultStats, mpbots.bot[].skinNum)
- Modified at run time: yes: Menu_UnlockMPSkins (0x7c680) clears then sets .enabled of items 12..28 from reward objIds 0x26..0x37 (0x2c unused); copied whole into mp_stuff by P_MPBOTCHOOSE.
- Users: P_MPBOTCHOOSE_Handler / FUN_0008aa49 (copied into mp_stuff); C_SBMPBTCHOOSE_Handler (Menu_GetItemFromHash, bot name); C_SBBOTS_Handler (Menu_GetItemFromHash, bot icon); C_SBMPSCEN_Handler (Quick Game bot names); Menu_GetMPSkins 0x7f3d0 (.enabled); Menu_UnlockMPSkins 0x7c680

| # | Icon | Title | Description | Identifier | En | Disabled text |
|---|---|---|---|---|---|---|
| 0 | 0x300010a `ICON_MPCHAR_BOND`* | 0x21a `CHAR_BOND_FULLNAME` "Bond" | 0x1000038 `CHAR_BOND_DESC` "The world's greatest secret agent." | skin 0 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 1 | 0x300010e `ICON_MPCHAR_DRAKE`* | 0x21b `CHAR_DRAKE_FULLNAME` "Drake" | 0x1000039 `CHAR_DRAKE_DESC` "Corporate genius intent on world domination." | skin 1 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 2 | 0x3000119 `ICON_MPCHAR_ROOK`* | 0x21c `CHAR_ROOK_FULLNAME` "Rook" | 0x100003a `CHAR_ROOK_DESC` "Battle-scarred bodyguard." | skin 2 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 3 | 0x3000112 `ICON_MPCHAR_KIKO`* | 0x21d `CHAR_KIKO_FULLNAME` "Kiko" | 0x100003b `CHAR_KIKO_DESC` "Elegant bodyguard." | skin 3 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 4 | 0x3000107 `ICON_MPCHAR_ALURA`* | 0x21e `CHAR_ALURA_FULLNAME` "Alura" | 0x100003c `CHAR_ALURA_DESC` "Australian secret agent." | skin 4 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 5 | 0x300010d `ICON_MPCHAR_DOMINIQUE`* | 0x21f `CHAR_DOMINIQUE_FULLNAME` "Dominique" | 0x100003d `CHAR_DOMINIQUE_DESC` "French secret agent." | skin 5 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 6 | 0x300011b `ICON_MPCHAR_SNOWGUARD`* | 0x220 `CHAR_GUARD_FULLNAME` "Snow Guard" | 0x100003e `CHAR_GUARD_DESC` "Guard for Drake's private castle." | skin 6 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 7 | 0x3000109 `ICON_MPCHAR_BLACKOPS`* | 0x221 `CHAR_BLACKOPS_FULLNAME` "Black Ops" | 0x100003f `CHAR_BLACKOPS_DESC` "Phoenix Black Operations trooper." | skin 7 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 8 | 0x300011e `ICON_MPCHAR_YAKUZA`* | 0x222 `CHAR_YAKUZA_FULLNAME` "Yakuza" | 0x1000040 `CHAR_YAKUZA_DESC` "Japanese gangster." | skin 8 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 9 | 0x300010c `ICON_MPCHAR_PHOENIXCOMMANDO`* | 0x223 `CHAR_COMMANDO_FULLNAME` "Phoenix Commando" | 0x1000041 `CHAR_COMMANDO_DESC` "Phoenix Special Forces soldier." | skin 9 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 10 | 0x300016c `ICON_MPCHAR_PHOENIXSOLDIER`* | 0x224 `CHAR_SOLDIER_FULLNAME` "Phoenix Soldier" | 0x1000042 `CHAR_SOLDIER_DESC` "Phoenix soldier." | skin 10 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 11 | 0x3000115 `ICON_MPCHAR_NINJA`* | 0x225 `CHAR_NINJA_FULLNAME` "Ninja" | 0x1000043 `CHAR_NINJA_DESC` "Stealthy assassin." | skin 11 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 12 | 0x3000147 `ICON_MPCHAR_BONDTUX`* | 0x39f `CHAR_BONDTUX_FULLNAME`* "Bond Tux" | 0x10001f2 `CHAR_BONDTUX_DESC`* "The world's greatest secret agent - in a tuxedo." | skin 12 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 13 | 0x3000148 `ICON_MPCHAR_DRAKESUIT`* | 0x3a0 `CHAR_DRAKESUIT_FULLNAME`* "Drake Suit" | 0x10001f3 `CHAR_DRAKESUIT_DESC`* "Corporate genius - dressed to impress." | skin 13 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 14 | 0x3000194 `ICON_MPCHAR_BONDSPACESUIT`* | 0x10001ee `CHAR_BONDSPACE_FULLNAME`* "Bond Spacesuit" | 0x10001f0 `CHAR_BONDSPACE_DESC`* "The world's greatest secret agent - in a space suit." | skin 14 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 15 | 0x3000110 `ICON_MPCHAR_GOLDFINGER`* | 0x226 `CHAR_GOLDFINGER_FULLNAME` "Goldfinger" | 0x1000044 `CHAR_GOLDFINGER_DESC` "He loves only gold." | skin 15 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 16 | 0x3000118 `ICON_MPCHAR_RENARD`* | 0x227 `CHAR_RENARD_FULLNAME` "Renard" | 0x100004e `CHAR_RENARD_DESC` "Anarchist who feels no pain." | skin 16 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 17 | 0x300011a `ICON_MPCHAR_SCARAMANGA`* | 0x228 `CHAR_SCARAMANGA_FULLNAME` "Scaramanga" | 0x1000046 `CHAR_SCARAMANGA_DESC` "The man with the golden gun." | skin 17 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 18 | 0x3000117 `ICON_MPCHAR_PUSSYGALORE`* | 0x229 `CHAR_GALORE_FULLNAME` "Pussy Galore" | 0x1000047 `CHAR_GALORE_DESC` "Glamorous stunt pilot." | skin 18 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 19 | 0x300010b `ICON_MPCHAR_CHRISTMASJONES`* | 0x22a `CHAR_XMASJONES_FULLNAME` "Christmas Jones" | 0x100004c `CHAR_XMASJONES_DESC` "The world's sexiest nuclear physicist." | skin 19 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 20 | 0x300011c `ICON_MPCHAR_WAILIN`* | 0x22b `CHAR_WAILIN_FULLNAME` "Wai Lin" | 0x10001f4 `CHAR_WAILIN_DESC`* "Chinese secret agent." | skin 20 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 21 | 0x300011d `ICON_MPCHAR_XENIAONATOPP`* | 0x22c `CHAR_XENIA_FULLNAME` "Xenia Onatopp" | 0x1000049 `CHAR_XENIA_DESC` "Mercenary with a taste for violence." | skin 21 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 22 | 0x3000113 `ICON_MPCHAR_MAYDAY`* | 0x22d `CHAR_MAYDAY_FULLNAME` "May Day" | 0x100004a `CHAR_MAYDAY_DESC` "Exotic assassin." | skin 22 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 23 | 0x300010f `ICON_MPCHAR_ELEKTRAKING`* | 0x22e `CHAR_ELEKTRA_FULLNAME` "Elektra King" | 0x100004b `CHAR_ELEKTRA_DESC` "Corporate heiress out for revenge." | skin 23 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 24 | 0x3000111 `ICON_MPCHAR_JAWS`* | 0x22f `CHAR_JAWS_FULLNAME` "Jaws" | 0x1000048 `CHAR_JAWS_DESC` "Hulking assassin with razor-sharp teeth." | skin 24 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 25 | 0x3000108 `ICON_MPCHAR_BARONSAMEDI`* | 0x230 `CHAR_SAMEDI_FULLNAME` "Baron Samedi" | 0x100004d `CHAR_SAMEDI_DESC` "Voodoo master with uncanny powers." | skin 25 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 26 | 0x3000116 `ICON_MPCHAR_ODDJOB`* | 0x231 `CHAR_ODDJOB_FULLNAME` "Oddjob" | 0x1000045 `CHAR_ODDJOB_DESC` "Silent and fearsome bodyguard to Auric Goldfinger." | skin 26 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 27 | 0x3000114 `ICON_MPCHAR_NICKNACK`* | 0x2ff `CHAR_NICKNACK_FULLNAME` "Nick Nack" | 0x10001f5 `CHAR_NICKNACK_DESC`* "Scaramanga’s henchman." | skin 27 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 28 | 0x3000196 `ICON_MPCHAR_MAXZORIN`* | 0x10001ef `CHAR_ZORIN_FULLNAME`* "Max Zorin" | 0x10001f1 `CHAR_ZORIN_DESC`* "This former KGB agent is a psychotic genius." | skin 28 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |

## `mp_characters_small` - 0x17cb98, 29 items

- PS2: 0x2e0720
- In the code: not in the code
- Identifier: character / skin index 0..28 (same order as mp_characters, smaller icons)
- Modified at run time: yes: Menu_UnlockMPSkins (0x7c680) mirrors the mp_characters unlocks.
- Users: C_RBMPSETUP_Handler (Menu_GetItemFromHash); P_MPCONFIRM_Handler (Menu_GetItemFromHash, player icons); P_MPDEBRIEFING_Handler (Menu_GetItemFromHash); Menu_GetMPSkins 0x7f3d0 (identifier and title into the C_RBMPSETUP scroller); Menu_UnlockMPSkins 0x7c680

| # | Icon | Title | Description | Identifier | En | Disabled text |
|---|---|---|---|---|---|---|
| 0 | 0x3000154 `ICON_MPCHAR_SMALL_BOND`* | 0x21a `CHAR_BOND_FULLNAME` "Bond" | 0x1000038 `CHAR_BOND_DESC` "The world's greatest secret agent." | skin 0 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 1 | 0x3000159 `ICON_MPCHAR_SMALL_DRAKE`* | 0x21b `CHAR_DRAKE_FULLNAME` "Drake" | 0x1000039 `CHAR_DRAKE_DESC` "Corporate genius intent on world domination." | skin 1 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 2 | 0x3000165 `ICON_MPCHAR_SMALL_ROOK`* | 0x21c `CHAR_ROOK_FULLNAME` "Rook" | 0x100003a `CHAR_ROOK_DESC` "Battle-scarred bodyguard." | skin 2 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 3 | 0x300015e `ICON_MPCHAR_SMALL_KIKO`* | 0x21d `CHAR_KIKO_FULLNAME` "Kiko" | 0x100003b `CHAR_KIKO_DESC` "Elegant bodyguard." | skin 3 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 4 | 0x300016b `ICON_MPCHAR_SMALL_ALURA`* | 0x21e `CHAR_ALURA_FULLNAME` "Alura" | 0x100003c `CHAR_ALURA_DESC` "Australian secret agent." | skin 4 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 5 | 0x3000158 `ICON_MPCHAR_SMALL_DOMINIQUE`* | 0x21f `CHAR_DOMINIQUE_FULLNAME` "Dominique" | 0x100003d `CHAR_DOMINIQUE_DESC` "French secret agent." | skin 5 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 6 | 0x3000167 `ICON_MPCHAR_SMALL_SNOWGUARD`* | 0x220 `CHAR_GUARD_FULLNAME` "Snow Guard" | 0x100003e `CHAR_GUARD_DESC` "Guard for Drake's private castle." | skin 6 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 7 | 0x3000153 `ICON_MPCHAR_SMALL_BLACKOPS`* | 0x221 `CHAR_BLACKOPS_FULLNAME` "Black Ops" | 0x100003f `CHAR_BLACKOPS_DESC` "Phoenix Black Operations trooper." | skin 7 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 8 | 0x300016a `ICON_MPCHAR_SMALL_YAKUZA`* | 0x222 `CHAR_YAKUZA_FULLNAME` "Yakuza" | 0x1000040 `CHAR_YAKUZA_DESC` "Japanese gangster." | skin 8 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 9 | 0x3000157 `ICON_MPCHAR_SMALL_PHOENIXCOMMANDO`* | 0x223 `CHAR_COMMANDO_FULLNAME` "Phoenix Commando" | 0x1000041 `CHAR_COMMANDO_DESC` "Phoenix Special Forces soldier." | skin 9 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 10 | 0x300019e `ICON_MPCHAR_SMALL_PHOENIXSOLDIER`* | 0x224 `CHAR_SOLDIER_FULLNAME` "Phoenix Soldier" | 0x1000042 `CHAR_SOLDIER_DESC` "Phoenix soldier." | skin 10 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 11 | 0x3000162 `ICON_MPCHAR_SMALL_NINJA`* | 0x225 `CHAR_NINJA_FULLNAME` "Ninja" | 0x1000043 `CHAR_NINJA_DESC` "Stealthy assassin." | skin 11 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 12 | 0x3000155 `ICON_MPCHAR_SMALL_BONDTUX`* | 0x39f `CHAR_BONDTUX_FULLNAME`* "Bond Tux" | 0x10001f2 `CHAR_BONDTUX_DESC`* "The world's greatest secret agent - in a tuxedo." | skin 12 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 13 | 0x300015a `ICON_MPCHAR_SMALL_DRAKESUIT`* | 0x3a0 `CHAR_DRAKESUIT_FULLNAME`* "Drake Suit" | 0x10001f3 `CHAR_DRAKESUIT_DESC`* "Corporate genius - dressed to impress." | skin 13 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 14 | 0x3000195 `ICON_MPCHAR_SMALL_BONDSPACESUIT`* | 0x10001ee `CHAR_BONDSPACE_FULLNAME`* "Bond Spacesuit" | 0x10001f0 `CHAR_BONDSPACE_DESC`* "The world's greatest secret agent - in a space suit." | skin 14 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 15 | 0x300015c `ICON_MPCHAR_SMALL_GOLDFINGER`* | 0x226 `CHAR_GOLDFINGER_FULLNAME` "Goldfinger" | 0x1000044 `CHAR_GOLDFINGER_DESC` "He loves only gold." | skin 15 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 16 | 0x300019f `ICON_MPCHAR_SMALL_RENARD`* | 0x227 `CHAR_RENARD_FULLNAME` "Renard" | 0x100004e `CHAR_RENARD_DESC` "Anarchist who feels no pain." | skin 16 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 17 | 0x3000166 `ICON_MPCHAR_SMALL_SCARAMANGA`* | 0x228 `CHAR_SCARAMANGA_FULLNAME` "Scaramanga" | 0x1000046 `CHAR_SCARAMANGA_DESC` "The man with the golden gun." | skin 17 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 18 | 0x3000164 `ICON_MPCHAR_SMALL_PUSSYGALORE`* | 0x229 `CHAR_GALORE_FULLNAME` "Pussy Galore" | 0x1000047 `CHAR_GALORE_DESC` "Glamorous stunt pilot." | skin 18 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 19 | 0x3000156 `ICON_MPCHAR_SMALL_CHRISTMASJONES`* | 0x22a `CHAR_XMASJONES_FULLNAME` "Christmas Jones" | 0x100004c `CHAR_XMASJONES_DESC` "The world's sexiest nuclear physicist." | skin 19 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 20 | 0x3000168 `ICON_MPCHAR_SMALL_WAILIN`* | 0x22b `CHAR_WAILIN_FULLNAME` "Wai Lin" | 0x10001f4 `CHAR_WAILIN_DESC`* "Chinese secret agent." | skin 20 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 21 | 0x3000169 `ICON_MPCHAR_SMALL_XENIAONATOPP`* | 0x22c `CHAR_XENIA_FULLNAME` "Xenia Onatopp" | 0x1000049 `CHAR_XENIA_DESC` "Mercenary with a taste for violence." | skin 21 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 22 | 0x3000160 `ICON_MPCHAR_SMALL_MAYDAY`* | 0x22d `CHAR_MAYDAY_FULLNAME` "May Day" | 0x100004a `CHAR_MAYDAY_DESC` "Exotic assassin." | skin 22 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 23 | 0x300015b `ICON_MPCHAR_SMALL_ELEKTRAKING`* | 0x22e `CHAR_ELEKTRA_FULLNAME` "Elektra King" | 0x100004b `CHAR_ELEKTRA_DESC` "Corporate heiress out for revenge." | skin 23 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 24 | 0x300015d `ICON_MPCHAR_SMALL_JAWS`* | 0x22f `CHAR_JAWS_FULLNAME` "Jaws" | 0x1000048 `CHAR_JAWS_DESC` "Hulking assassin with razor-sharp teeth." | skin 24 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 25 | 0x3000152 `ICON_MPCHAR_SMALL_BARONSAMEDI`* | 0x230 `CHAR_SAMEDI_FULLNAME` "Baron Samedi" | 0x100004d `CHAR_SAMEDI_DESC` "Voodoo master with uncanny powers." | skin 25 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 26 | 0x3000163 `ICON_MPCHAR_SMALL_ODDJOB`* | 0x231 `CHAR_ODDJOB_FULLNAME` "Oddjob" | 0x1000045 `CHAR_ODDJOB_DESC` "Silent and fearsome bodyguard to Auric Goldfinger." | skin 26 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 27 | 0x3000161 `ICON_MPCHAR_SMALL_NICKNACK`* | 0x2ff `CHAR_NICKNACK_FULLNAME` "Nick Nack" | 0x10001f5 `CHAR_NICKNACK_DESC`* "Scaramanga’s henchman." | skin 27 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 28 | 0x3000197 `ICON_MPCHAR_SMALL_MAXZORIN`* | 0x10001ef `CHAR_ZORIN_FULLNAME`* "Max Zorin" | 0x10001f1 `CHAR_ZORIN_DESC`* "This former KGB agent is a psychotic genius." | skin 28 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |

## `mp_options` - 0x17ce50, 5 items

- PS2: 0x2e09d8
- In the code: not in the code
- Identifier: index 0..4 (C_SBMPOPTIONS switch)
- Modified at run time: yes: P_MPOPTIONS sets [1].enabled (AI Bots) = map is not Ravine.
- Users: C_SBMPOPTIONS_Handler (wheel, select); P_MPOPTIONS_Handler (Menu_SelectItemInControl, [1].enabled)

| # | Icon | Title | Description | Identifier | En | Disabled text |
|---|---|---|---|---|---|---|
| 0 | 0x300013a `ICON_MPSCENARIO_QUICKGAME` | 0x171 `MENU_CONTINUE`* "Continue" | 0x100001c `MP_START_DESC` "Confirm the game setup and continue the loading sequence." | index 0 | 1 | - |
| 1 | 0x3000136 `ICON_MPOPTIONS_AIBOTS`* | 0x1ad `MP_AIBOTS` "AI Bots" | 0x1000020 `MP_CFG_BOTS_DESC` "Add computer-controlled bots to your game." | index 1 | 1 | 0x10000ae `NO_BOTS_ON_RAVINE` "You can't have bots play the Ravine map." |
| 2 | 0x3000139 `ICON_MPOPTIONS_RULES`* | 0x19b `MP_CFG_RULES` "Game Rules" | 0x100001d `MP_GM_RULES_DESC` "Determine the rules by which the scenario is played." | index 2 | 1 | - |
| 3 | 0x3000138 `ICON_MPOPTIONS_PLAYERMODS`* | 0x19d `MP_CFG_PLAYERS` "Player Mods" | 0x100001e `MP_CFG_PLAYER_DESC` "Change settings that apply to participating agents." | index 3 | 1 | - |
| 4 | 0x3000137 `ICON_MPOPTIONS_ENVIROMODS`* | 0x19e `MP_CFG_ENVIRONMENT` "Enviro-Mods" | 0x100001f `MP_CFG_ENVIRONMENT_DESC` "Set options for the environment you'll play in." | index 4 | 1 | - |

## `cn_options` - 0x17cec8, 7 items

- PS2: 0x2e0a50
- In the code: not in the code
- Identifier: index 0..6 (C_SBCNOPTIONS switch)
- Modified at run time: yes: P_CNMENU rewrites [6].title: 0x14b "Codenames" or 0x1f4 "Save Codename".
- Users: C_SBCNOPTIONS_Handler (wheel, select); P_CNMENU_Handler (Menu_SelectItemInControl, [6].title)

| # | Icon | Title | Description | Identifier | En | Disabled text |
|---|---|---|---|---|---|---|
| 0 | 0x300013b `ICON_CNOPTIONS_SECRETUNLOCKS`* | 0x10002d4 `SECRET_UNLOCKS` "Secret Unlocks" | 0x10002d7 `SECRET_UNLOCKS_DESC` "Enter secret codes to unlock missions and characters." | index 0 | 1 | - |
| 1 | 0x3000122 `ICON_CNOPTIONS_CONTROLLER`* | 0x1f3 `CONTROLLER_SETUP` "Controller Setup" | 0x1000051 `CONTROL_SCHEME_ACTION_DESC` "Select the controller setup that matches your playing style." | index 1 | 1 | - |
| 2 | 0x3000122 `ICON_CNOPTIONS_CONTROLLER`* | 0x27e `CN_DRIVING_CONTROLLER`* "Driving Controller" | 0x1000052 `CONTROL_SCHEME_DRIVING_DESC` "Review the controller setup for the driving missions." | index 2 | 1 | - |
| 3 | 0x3000124 `ICON_CNOPTIONS_ADVANCED`* | 0x37a `CN_ADVANCED_OPTIONS`* "Advanced Options" | 0x1000053 `ADV_SETTINGS_DESC` "Change advanced settings for hardcore players." | index 3 | 1 | - |
| 4 | 0x3000123 `ICON_CNOPTIONS_MULTIPLAYER`* | 0x199 `CN_MULTIPLAYER_OPTIONS`* "Multiplayer Options" | 0x1000054 `MULTIPLAYER_SETTINGS_DESC` "Select options that reflect your multiplayer gaming style." | index 4 | 1 | - |
| 5 | 0x300011f `ICON_CNOPTIONS_AV`* | 0x2bb `CN_AV_OPTIONS`* "AV Options" | 0x1000055 `AV_SETTINGS_DESC` "Adjust visual settings and sound volumes to match your en..." | index 5 | 1 | - |
| 6 | 0x3000125 `ICON_CNOPTIONS_SAVE`* | 0x1f4 `SAVE_CODENAME` "Save Codename" | 0x1000056 `ACCEPT_CHANGES_DESC` "Accept any changes you've made." | index 6 | 1 | - |

## `ds_options` - 0x17cf70, 4 items

- PS2: 0x2e0af8
- In the code: ui_dossier.cpp (matches)
- Identifier: index 0..3
- Modified at run time: no
- Users: C_SBDOSSIER_Handler (wheel)

| # | Icon | Title | Description | Identifier | En | Disabled text |
|---|---|---|---|---|---|---|
| 0 | 0x300019b `ICON_DOSSIER_RECORDS` | 0x280 `DOSSIER_RECORDS_NAME` "Records" | 0x100000a `DOSSIER_RECORDS_DESC` "View your highest scores and the best medals you've earned." | index 0 | 1 | - |
| 1 | 0x300019c `ICON_DOSSIER_REWARDS` | 0x281 `DOSSIER_REWARDS_NAME` "Rewards" | 0x100000b `DOSSIER_REWARDS_DESC` "Check out the rewards and upgrades you've unlocked." | index 1 | 1 | - |
| 2 | 0x30000c4 `ICON_DOSSIER_GADGETS` | 0x282 `DOSSIER_GADGETS_NAME` "Gadgets" | 0x100000c `DOSSIER_GADGETS_DESC` "Review your gadgets and the upgrades you've discovered." | index 2 | 1 | - |
| 3 | 0x30000ee `ICON_DOSSIER_WEAPONS` | 0x19c `MP_CFG_OR_DOSSIER_WEAPONS` "Weapons" | 0x100000d `DOSSIER_WEAPONS_DESC` "Look at the weapons in the game and any upgrades you've f..." | index 3 | 1 | - |

## `mp_bots` - 0x17cfd0, 17 items

- PS2: 0x2e0b58
- In the code: not in the code (Ghidra Xbox names it mp_characters too; PS2 name mp_bots)
- Identifier: unused (0); the wheel position is the bot number (0 = Continue)
- Modified at run time: yes: C_SBBOTS writes .iconHashcode: [0] = 0x0300013a, [i] = icon of the bot's skin from mp_characters.
- Users: C_SBBOTS_Handler (wheel, icons)

| # | Icon | Title | Description | Identifier | En | Disabled text |
|---|---|---|---|---|---|---|
| 0 | 0x300013a `ICON_MPSCENARIO_QUICKGAME` | 0x171 `MENU_CONTINUE`* "Continue" | - | 0 | 1 | - |
| 1 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x284 `MP_CFG_BOT_1` "Setup Bot 1" | - | 0 | 1 | - |
| 2 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x285 `MP_CFG_BOT_2` "Setup Bot 2" | - | 0 | 1 | - |
| 3 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x286 `MP_CFG_BOT_3` "Setup Bot 3" | - | 0 | 1 | - |
| 4 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x287 `MP_CFG_BOT_4` "Setup Bot 4" | - | 0 | 1 | - |
| 5 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x288 `MP_CFG_BOT_5` "Setup Bot 5" | - | 0 | 1 | - |
| 6 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x289 `MP_CFG_BOT_6` "Setup Bot 6" | - | 0 | 1 | - |
| 7 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x28a `MP_CFG_BOT_7` "Setup Bot 7" | - | 0 | 1 | - |
| 8 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x28b `MP_CFG_BOT_8` "Setup Bot 8" | - | 0 | 1 | - |
| 9 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x28c `MP_CFG_BOT_9` "Setup Bot 9" | - | 0 | 1 | - |
| 10 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x28d `MP_CFG_BOT_10` "Setup Bot 10" | - | 0 | 1 | - |
| 11 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x28e `MP_CFG_BOT_11` "Setup Bot 11" | - | 0 | 1 | - |
| 12 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x28f `MP_CFG_BOT_12` "Setup Bot 12" | - | 0 | 1 | - |
| 13 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x290 `MP_CFG_BOT_13` "Setup Bot 13" | - | 0 | 1 | - |
| 14 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x291 `MP_CFG_BOT_14` "Setup Bot 14" | - | 0 | 1 | - |
| 15 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x292 `MP_CFG_BOT_15` "Setup Bot 15" | - | 0 | 1 | - |
| 16 | 0x3000099 `ICON_MPSCENARIO_ARENA` | 0x293 `MP_CFG_BOT_16` "Setup Bot 16" | - | 0 | 1 | - |

## `ds_weapons` - 0x17d168, 27 items

- PS2: 0x2e0cf0
- In the code: ui_dossier.cpp (matches)
- Identifier: upgrade slot for Menu_GetObjectUpgradeLevel (0x3f = no upgrades)
- Modified at run time: yes: P_DSWEAPONS rewrites [0] icon/title/description (PP7, P2K, Gold PP7, Gold P2K).
- Users: C_SBDSWPSCROLL_Handler (wheel); P_DSWEAPONS_Handler ([0])

| # | Icon | Title | Description | Identifier | En | Disabled text |
|---|---|---|---|---|---|---|
| 0 | 0x30000f3 `ICON_DS_WEAPON_PP7` | 0x33d `WEAPON_PP7_NAME` "Wolfram PP7" | 0x359 `WEAPON_PP7_DESC` "Standard-issue 7.62mm handgun with a 7-round clip. Altern..." | WEAPON_PISTOL | 1 | - |
| 1 | 0x30000ed `ICON_DS_WEAPON_GOLDENGUN` | 0x33f `WEAPON_GOLDENGUN_NAME` "Golden Gun" | 0x35b `WEAPON_GOLDENGUN_DESC` "The ultimate handgun. Devastating damage but requires loa..." | NO_UPGRADES | 1 | - |
| 2 | 0x30000ee `ICON_DOSSIER_WEAPONS` | 0x340 `WEAPON_KOWLOON_NAME` "Kowloon Type 40" | 0x35c `WEAPON_KOWLOON_DESC` "Basic sidearm employed by Drake's forces. Not very accura..." | NO_UPGRADES | 1 | - |
| 3 | 0x30000f0 `ICON_DS_WEAPON_RAPTORMAGNUM` | 0x341 `WEAPON_RAPTORMAGNUM_NAME` "Raptor Magnum" | 0x35d `WEAPON_RAPTORMAGNUM_DESC` "Powerful, large-calibre handgun with a small clip. Some m..." | NO_UPGRADES | 1 | - |
| 4 | 0x30000e9 `ICON_DS_WEAPON_DEUTSCHEM9K` | 0x342 `WEAPON_DEUTSCHEM9K_NAME` "Deutsche M9K" | 0x35e `WEAPON_DEUTSCHEM9K_DESC` "Light submachine gun firing 3-round bursts. Accurate, but..." | NO_UPGRADES | 1 | - |
| 5 | 0x30000f8 `ICON_DS_WEAPON_STORMM32` | 0x343 `WEAPON_STORMM32_NAME` "Storm M32" | 0x35f `WEAPON_STORMM32_DESC` "A fully automatic machine pistol. Its large clip makes up..." | NO_UPGRADES | 1 | - |
| 6 | 0x30000f6 `ICON_DS_WEAPON_SG5COMMANDO` | 0x344 `WEAPON_SG5COMMANDO_NAME` "SG5 Commando" | 0x360 `WEAPON_SG5COMMANDO_DESC` "Assault rifle with either telescopic sight or silencer. A..." | NO_UPGRADES | 1 | - |
| 7 | 0x30000e8 `ICON_DS_WEAPON_AIMS20` | 0x345 `WEAPON_AIMS20_NAME` "AIMS-20" | 0x361 `WEAPON_AIMS20_DESC` "Advanced weapon system with a telescopic sight. Switch be..." | NO_UPGRADES | 1 | - |
| 8 | 0x30000ec `ICON_DS_WEAPON_FRINESI` | 0x346 `WEAPON_FRINESI_NAME` "Frinesi Automatic 12" | 0x362 `WEAPON_FRINESI_DESC` "Combat shotgun, most effective at close range. Alternate ..." | NO_UPGRADES | 1 | - |
| 9 | 0x30000fe `ICON_DS_WEAPON_COVERTSNIPER` | 0x347 `WEAPON_COVERTSNIPER_NAME` "Covert Sniper Rifle" | 0x363 `WEAPON_COVERTSNIPER_DESC` "Silenced military sniper rifle with incredible lethality...." | WEAPON_SNIPER | 1 | - |
| 10 | 0x30000fa `ICON_DS_WEAPON_TACTICALSNIPER` | 0x348 `WEAPON_TACTICALSNIPER_NAME` "Tactical Sniper Rifle" | 0x364 `WEAPON_TACTICALSNIPER_DESC` "Military sniper rifle with incredible lethality. 5-round ..." | WEAPON_SNIPER | 1 | - |
| 11 | 0x30000f1 `ICON_DS_WEAPON_MILITEKLAUNCHER` | 0x349 `WEAPON_MILITEKLAUNCHER_NAME` "Militek Mark 6 MGL" | 0x365 `WEAPON_MILITEKLAUNCHER_DESC` "Drum-fed weapon firing high-explosive grenades on an arci..." | NO_UPGRADES | 1 | - |
| 12 | 0x30000f5 `ICON_DS_WEAPON_SENTINEL` | 0x34a `WEAPON_SENTINEL_NAME` "AT-420 Sentinel" | 0x366 `WEAPON_SENTINEL_DESC` "Military missile launcher that  allows you to steer the m..." | NO_UPGRADES | 1 | - |
| 13 | 0x3000191 `ICON_DS_WEAPON_SCORPION` | 0x34b `WEAPON_SCORPION_NAME` "AT-600 Scorpion" | 0x367 `WEAPON_SCORPION_DESC` "Launches heat-seeking missiles that automatically track a..." | NO_UPGRADES | 1 | - |
| 14 | 0x30000ef `ICON_DS_WEAPON_SAMURAI` | 0x34c `WEAPON_SAMURAI_NAME` "Phoenix Samurai" | 0x368 `WEAPON_SAMURAI_DESC` "Experimental Phoenix weapon capable of massive damage. We..." | NO_UPGRADES | 1 | - |
| 15 | 0x30000eb `ICON_DS_WEAPON_FRAGGRENADE` | 0x34d `WEAPON_FRAGGRENADE_NAME` "Frag Grenade" | 0x369 `WEAPON_FRAGGRENADE_DESC` "Explosive grenade that damages anyone within its blast ra..." | NO_UPGRADES | 1 | - |
| 16 | 0x30000f7 `ICON_DS_WEAPON_SMOKEGRENADE` | 0x34e `WEAPON_SMOKEGRENADE_NAME` "Smoke Grenade" | 0x36a `WEAPON_SMOKEGRENADE_DESC` "Produces a cloud of smoke that upsets enemy aim and cloak..." | NO_UPGRADES | 1 | - |
| 17 | 0x30000ea `ICON_DS_WEAPON_STUNGRENADE` | 0x34f `WEAPON_STUNGRENADE_NAME` "Stun Grenade" | 0x36b `WEAPON_STUNGRENADE_DESC` "Temporarily blinds and stuns opponents, providing you wit..." | NO_UPGRADES | 1 | - |
| 18 | 0x30000f4 `ICON_DS_WEAPON_SATCHELCHARGE` | 0x350 `WEAPON_SATCHELCHARGE_NAME` "Satchel Charge" | 0x36c `WEAPON_SATCHELCHARGE_DESC` "Contains plastic explosives which can be placed on an obj..." | NO_UPGRADES | 1 | - |
| 19 | 0x3000100 `ICON_DS_WEAPON_REMOTEMINE` | 0x351 `WEAPON_REMOTEMINE_NAME` "Remote Mine" | 0x36d `WEAPON_REMOTEMINE_DESC` "Small anti-personnel device that sticks to any surface. S..." | NO_UPGRADES | 1 | - |
| 20 | 0x30000f9 `ICON_DS_WEAPON_TRIPBOMB` | 0x352 `WEAPON_TRIPBOMB_NAME` "Laser Trip Bomb" | 0x36e `WEAPON_TRIPBOMB_DESC` "Laser-activated bomb that explodes when someone crosses i..." | NO_UPGRADES | 1 | - |
| 21 | 0x3000101 `ICON_DS_WEAPON_SNOWMOBILE` | 0x353 `WEAPON_SNOWMOBILE_NAME` "JL-7 Weapon System" | 0x36f `WEAPON_SNOWMOBILE_DESC` "Dual-weapon platform combines a rapid-fire 20mm cannon an..." | NO_UPGRADES | 1 | - |
| 22 | 0x3000102 `ICON_DS_WEAPON_CARMISSILES` | 0x354 `WEAPON_CARMISSILES_NAME` "V12 Missile System" | 0x370 `WEAPON_CARMISSILES_DESC` "Custom Q-designed infrared-guided missile system located ..." | NO_UPGRADES | 1 | - |
| 23 | 0x3000103 `ICON_DS_WEAPON_SUBTORPEDOES` | 0x355 `WEAPON_SUBTORPEDOES_NAME` "V12 Torpedo Launcher" | 0x371 `WEAPON_SUBTORPEDOES_DESC` "Advanced torpedo system for use against underwater target..." | NO_UPGRADES | 1 | - |
| 24 | 0x30000ff `ICON_DS_WEAPON_SUBMINES` | 0x356 `WEAPON_SUBMINES_NAME` "V12 Q-Charge" | 0x372 `WEAPON_SUBMINES_DESC` "A compact limpet mine that can be used to destroy underwa..." | NO_UPGRADES | 1 | - |
| 25 | 0x3000193 `ICON_DS_WEAPON_JUNGLECAR` | 0x357 `WEAPON_JUNGLECAR_NAME` "Combat Utility Vehicle" | 0x373 `WEAPON_JUNGLECAR_DESC` "Armoured sports-utility vehicle features a specially depl..." | NO_UPGRADES | 1 | - |
| 26 | 0x3000192 `ICON_DS_WEAPON_JUNGLEPLANE` | 0x358 `WEAPON_JUNGLEPLANE_NAME` "D-1400 Weapon System" | 0x374 `WEAPON_JUNGLEPLANE_DESC` "20mm twin-barrel cannon combined with an advanced heat-se..." | NO_UPGRADES | 1 | - |

## `ds_gadgets` - 0x17d3f0, 14 items

- PS2: 0x2e0f78
- In the code: ui_dossier.cpp (matches)
- Identifier: upgrade slot for Menu_GetObjectUpgradeLevel (0x3f = no upgrades)
- Modified at run time: yes: P_DSGADGETS rewrites [6].iconHashcode (branded or generic shaver).
- Users: C_SBDSGTSCROLL_Handler (wheel); P_DSGADGETS_Handler ([6].icon)

| # | Icon | Title | Description | Identifier | En | Disabled text |
|---|---|---|---|---|---|---|
| 0 | 0x30000e6 `ICON_DS_GADGET_TASER` | 0x323 `GADGET_TASER_NAME` "Stunner" | 0x331 `GADGET_TASER_DESC` "Discharges high voltage electrical current that temporari..." | GADGET_TASER | 1 | - |
| 1 | 0x30000c4 `ICON_DOSSIER_GADGETS` | 0x321 `GADGET_LASER_NAME` "Laser" | 0x32f `GADGET_LASER_DESC` "Lets you quietly cut through locks and hinges." | GADGET_LASER | 1 | - |
| 2 | 0x30000e2 `ICON_DS_GADGET_GRAPPLE` | 0x328 `GADGET_GRAPPLE_NAME` "Grapple" | 0x336 `GADGET_GRAPPLE_DESC` "Shoots a high-tension wire which can be attached to speci..." | GADGET_GRAPPLE | 1 | - |
| 3 | 0x30000df `ICON_DS_GADGET_CAMERA` | 0x329 `GADGET_CAMERA_NAME` "Micro-Camera" | 0x337 `GADGET_CAMERA_DESC` "Provides magnified surveillance, the ability to photograp..." | GADGET_CAMERA | 1 | - |
| 4 | 0x30000e1 `ICON_DS_GADGET_DECODER` | 0x326 `GADGET_DECODER_NAME` "Decryptor" | 0x334 `GADGET_DECODER_DESC` "A sophisticated micro-computer that allows you to bypass ..." | GADGET_DECODER | 1 | - |
| 5 | 0x30000e4 `ICON_DS_GADGET_QWORM` | 0x327 `GADGET_QWORM_NAME` "Q-Worm" | 0x335 `GADGET_QWORM_DESC` "Installs viral code that allows MI6 to retrieve data from..." | NO_UPGRADES | 1 | - |
| 6 | 0x30000e5 `ICON_DS_GADGET_SHAVER_BRANDED` | 0x32a `GADGET_SHAVER_NAME` "Shaver" | 0x338 `GADGET_SHAVER_DESC` "A remotely detonated grenade that temporarily blinds and ..." | NO_UPGRADES | 1 | - |
| 7 | 0x30000e7 `ICON_DS_GADGET_SENTRY` | 0x324 `GADGET_SENTRY_NAME` "Phoenix Ronin" | 0x332 `GADGET_SENTRY_DESC` "An automated gun that will target enemies itself, or can ..." | NO_UPGRADES | 1 | - |
| 8 | 0x30000e0 `ICON_DS_GADGET_DARTGUN` | 0x322 `GADGET_DARTGUN_NAME` "Korsakov K5" | 0x330 `GADGET_DARTGUN_DESC` "Fires a powerful sedative-laced dart capable of tranquill..." | GADGET_DARTGUN | 1 | - |
| 9 | 0x30000e3 `ICON_DS_GADGET_NIGHTVISION` | 0x325 `GADGET_NIGHTVISION_NAME` "V E Glasses" | 0x333 `GADGET_NIGHTVISION_DESC` "Alternate vision modes provide the ability to see opponen..." | NO_UPGRADES | 1 | - |
| 10 | 0x3000104 `ICON_DS_GADGET_SMOKESCREEN` | 0x32b `GADGET_SMOKESCREEN_NAME` "Q-Smoke" | 0x339 `GADGET_SMOKESCREEN_DESC` "Rear-deployed smoke emission. Use this to throw enemies o..." | NO_UPGRADES | 1 | - |
| 11 | 0x30000fc `ICON_DS_GADGET_TURBO` | 0x32c `GADGET_TURBO_NAME` "Q-Boost" | 0x33a `GADGET_TURBO_DESC` "Custom high-performance dual turbocharger adds significan..." | NO_UPGRADES | 1 | - |
| 12 | 0x3000190 `ICON_DS_GADGET_QWEDGE` | 0x32d `GADGET_QWEDGE_NAME` "Q-Wedge" | 0x33b `GADGET_QWEDGE_DESC` "Hydraulic lift system raises the car onto two wheels. A g..." | NO_UPGRADES | 1 | - |
| 13 | 0x30000fb `ICON_DS_GADGET_EMP` | 0x32e `GADGET_EMP_NAME` "Q-Pulse" | 0x33c `GADGET_EMP_DESC` "Produces a high-energy electro-magnetic pulse that fries ..." | NO_UPGRADES | 1 | - |

## `mp_stuff` - 0x245338, 29 items

- In the code: not in the code
- Identifier: as mp_characters
- Modified at run time: built at run time (BSS): P_MPBOTCHOOSE copies mp_characters and clears .enabled of characters a new bot may not take
- Users: P_MPBOTCHOOSE_Handler / FUN_0008aa49 (builds it); C_SBMPBTCHOOSE_Handler (wheel, .enabled)
- Initial contents are a copy of `mp_characters` (below shows that source).

| # | Icon | Title | Description | Identifier | En | Disabled text |
|---|---|---|---|---|---|---|
| 0 | 0x300010a `ICON_MPCHAR_BOND`* | 0x21a `CHAR_BOND_FULLNAME` "Bond" | 0x1000038 `CHAR_BOND_DESC` "The world's greatest secret agent." | index 0 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 1 | 0x300010e `ICON_MPCHAR_DRAKE`* | 0x21b `CHAR_DRAKE_FULLNAME` "Drake" | 0x1000039 `CHAR_DRAKE_DESC` "Corporate genius intent on world domination." | index 1 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 2 | 0x3000119 `ICON_MPCHAR_ROOK`* | 0x21c `CHAR_ROOK_FULLNAME` "Rook" | 0x100003a `CHAR_ROOK_DESC` "Battle-scarred bodyguard." | index 2 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 3 | 0x3000112 `ICON_MPCHAR_KIKO`* | 0x21d `CHAR_KIKO_FULLNAME` "Kiko" | 0x100003b `CHAR_KIKO_DESC` "Elegant bodyguard." | index 3 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 4 | 0x3000107 `ICON_MPCHAR_ALURA`* | 0x21e `CHAR_ALURA_FULLNAME` "Alura" | 0x100003c `CHAR_ALURA_DESC` "Australian secret agent." | index 4 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 5 | 0x300010d `ICON_MPCHAR_DOMINIQUE`* | 0x21f `CHAR_DOMINIQUE_FULLNAME` "Dominique" | 0x100003d `CHAR_DOMINIQUE_DESC` "French secret agent." | index 5 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 6 | 0x300011b `ICON_MPCHAR_SNOWGUARD`* | 0x220 `CHAR_GUARD_FULLNAME` "Snow Guard" | 0x100003e `CHAR_GUARD_DESC` "Guard for Drake's private castle." | index 6 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 7 | 0x3000109 `ICON_MPCHAR_BLACKOPS`* | 0x221 `CHAR_BLACKOPS_FULLNAME` "Black Ops" | 0x100003f `CHAR_BLACKOPS_DESC` "Phoenix Black Operations trooper." | index 7 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 8 | 0x300011e `ICON_MPCHAR_YAKUZA`* | 0x222 `CHAR_YAKUZA_FULLNAME` "Yakuza" | 0x1000040 `CHAR_YAKUZA_DESC` "Japanese gangster." | index 8 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 9 | 0x300010c `ICON_MPCHAR_PHOENIXCOMMANDO`* | 0x223 `CHAR_COMMANDO_FULLNAME` "Phoenix Commando" | 0x1000041 `CHAR_COMMANDO_DESC` "Phoenix Special Forces soldier." | index 9 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 10 | 0x300016c `ICON_MPCHAR_PHOENIXSOLDIER`* | 0x224 `CHAR_SOLDIER_FULLNAME` "Phoenix Soldier" | 0x1000042 `CHAR_SOLDIER_DESC` "Phoenix soldier." | index 10 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 11 | 0x3000115 `ICON_MPCHAR_NINJA`* | 0x225 `CHAR_NINJA_FULLNAME` "Ninja" | 0x1000043 `CHAR_NINJA_DESC` "Stealthy assassin." | index 11 | 1 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 12 | 0x3000147 `ICON_MPCHAR_BONDTUX`* | 0x39f `CHAR_BONDTUX_FULLNAME`* "Bond Tux" | 0x10001f2 `CHAR_BONDTUX_DESC`* "The world's greatest secret agent - in a tuxedo." | index 12 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 13 | 0x3000148 `ICON_MPCHAR_DRAKESUIT`* | 0x3a0 `CHAR_DRAKESUIT_FULLNAME`* "Drake Suit" | 0x10001f3 `CHAR_DRAKESUIT_DESC`* "Corporate genius - dressed to impress." | index 13 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 14 | 0x3000194 `ICON_MPCHAR_BONDSPACESUIT`* | 0x10001ee `CHAR_BONDSPACE_FULLNAME`* "Bond Spacesuit" | 0x10001f0 `CHAR_BONDSPACE_DESC`* "The world's greatest secret agent - in a space suit." | index 14 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 15 | 0x3000110 `ICON_MPCHAR_GOLDFINGER`* | 0x226 `CHAR_GOLDFINGER_FULLNAME` "Goldfinger" | 0x1000044 `CHAR_GOLDFINGER_DESC` "He loves only gold." | index 15 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 16 | 0x3000118 `ICON_MPCHAR_RENARD`* | 0x227 `CHAR_RENARD_FULLNAME` "Renard" | 0x100004e `CHAR_RENARD_DESC` "Anarchist who feels no pain." | index 16 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 17 | 0x300011a `ICON_MPCHAR_SCARAMANGA`* | 0x228 `CHAR_SCARAMANGA_FULLNAME` "Scaramanga" | 0x1000046 `CHAR_SCARAMANGA_DESC` "The man with the golden gun." | index 17 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 18 | 0x3000117 `ICON_MPCHAR_PUSSYGALORE`* | 0x229 `CHAR_GALORE_FULLNAME` "Pussy Galore" | 0x1000047 `CHAR_GALORE_DESC` "Glamorous stunt pilot." | index 18 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 19 | 0x300010b `ICON_MPCHAR_CHRISTMASJONES`* | 0x22a `CHAR_XMASJONES_FULLNAME` "Christmas Jones" | 0x100004c `CHAR_XMASJONES_DESC` "The world's sexiest nuclear physicist." | index 19 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 20 | 0x300011c `ICON_MPCHAR_WAILIN`* | 0x22b `CHAR_WAILIN_FULLNAME` "Wai Lin" | 0x10001f4 `CHAR_WAILIN_DESC`* "Chinese secret agent." | index 20 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 21 | 0x300011d `ICON_MPCHAR_XENIAONATOPP`* | 0x22c `CHAR_XENIA_FULLNAME` "Xenia Onatopp" | 0x1000049 `CHAR_XENIA_DESC` "Mercenary with a taste for violence." | index 21 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 22 | 0x3000113 `ICON_MPCHAR_MAYDAY`* | 0x22d `CHAR_MAYDAY_FULLNAME` "May Day" | 0x100004a `CHAR_MAYDAY_DESC` "Exotic assassin." | index 22 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 23 | 0x300010f `ICON_MPCHAR_ELEKTRAKING`* | 0x22e `CHAR_ELEKTRA_FULLNAME` "Elektra King" | 0x100004b `CHAR_ELEKTRA_DESC` "Corporate heiress out for revenge." | index 23 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 24 | 0x3000111 `ICON_MPCHAR_JAWS`* | 0x22f `CHAR_JAWS_FULLNAME` "Jaws" | 0x1000048 `CHAR_JAWS_DESC` "Hulking assassin with razor-sharp teeth." | index 24 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 25 | 0x3000108 `ICON_MPCHAR_BARONSAMEDI`* | 0x230 `CHAR_SAMEDI_FULLNAME` "Baron Samedi" | 0x100004d `CHAR_SAMEDI_DESC` "Voodoo master with uncanny powers." | index 25 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 26 | 0x3000116 `ICON_MPCHAR_ODDJOB`* | 0x231 `CHAR_ODDJOB_FULLNAME` "Oddjob" | 0x1000045 `CHAR_ODDJOB_DESC` "Silent and fearsome bodyguard to Auric Goldfinger." | index 26 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 27 | 0x3000114 `ICON_MPCHAR_NICKNACK`* | 0x2ff `CHAR_NICKNACK_FULLNAME` "Nick Nack" | 0x10001f5 `CHAR_NICKNACK_DESC`* "Scaramanga’s henchman." | index 27 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
| 28 | 0x3000196 `ICON_MPCHAR_MAXZORIN`* | 0x10001ef `CHAR_ZORIN_FULLNAME`* "Max Zorin" | 0x10001f1 `CHAR_ZORIN_DESC`* "This former KGB agent is a psychotic genius." | index 28 | 0 | 0x10000ad `CHARACTER_LOCKED` "This character is locked." |
