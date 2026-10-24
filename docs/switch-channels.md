# Switch channels

A switch channel is one byte of per-level boolean state. There are 256 of them, in plain arrays
(src/action/game/sp/SwitchChannels.cpp, each tagged with its original address):

```c
char switch_channels[256];          // 0x001df138 - the current state
char switch_channels_prev[256];     // 0x001dee38 - last frame, for edge detection
char switch_channels_hold[256];     // 0x001df238
uint32_t switch_channels_time[256]; // 0x001df428 - NumFramesUnpaused when it last changed
```

`Init_SwitchChannels` clears the lot at level load, except `switch_channels_time`, which it fills with
`GameState.NumFramesUnpaused`. `ResetMap_GameInit` calls it only when `IsWarmReset` is clear (the call at 0x000bedc2),
so a warm reset keeps every channel, and `Player_RamSave` / `Player_RamLoad` copy all four arrays across the load
of a mission's next part. They are the game's general-purpose wiring: a switch object sets one, a door reads one
and refuses to open, a light reads one and turns itself off, a mission objective reads one and ticks itself off.
Almost all of the scripting in a level is built out of them.

## Two kinds of user

**Data-driven, which is the overwhelming majority.** The channel number comes out of the map data, held
in the object: `light->switchChannel`, `sensor->alertSwitchChannel`, `spotlight->disableSwitchChannel`,
`rotor->switchChannel`, `scriptPlayer->someSwitchChannel`, `objective->completedChannel`, and so on.
Those tell you nothing about what any particular number means, because the meaning lives in the level
file rather than in the executable.

**Hard-coded in the engine**, which is what the tables below record: a number in a comparison or an index with no
explanation of where it came from. In the code they are the `SwitchChannel` enum in
src/action/game/sp/SwitchChannels.h, or numbers with a comment where no name fits.

## Channels are numbered per level

A census of every hard-coded channel against every level's placements and scripts settles the question: the
authors numbered channels per level. The same number means unrelated things in different levels - `0x01`, the
commonest channel in the data, is used for unrelated wiring in ten levels - and the per-level tables in the code
show it outright (`PowerstationLines` gives the same loudspeaker line channel 52 in PowerStationA1 and 71 in
PowerStationA2).

Channels also carry over between the parts of a mission (warm reset, `Player_RamLoad`), and the data relies on
it: a part's objective is often revealed by the previous part's completed channel (HendersonB reveals on 71,
HendersonA's).

So the engine's hard-coded numbers fall into three kinds, and the enum names them accordingly:

- **One meaning game-wide** - every data use agrees with the engine's (or the data never touches it). Plain
  names: `SWITCH_MISSION_FAILED`, `SWITCH_PLAYER_DIED`, ...
- **Used only under a level guard** - named for the level: `SWITCH_CASTLEINDOORS1_ARMED`, ... Almost all of these
  guards are needed: the number is reused for other things in other levels' data, and without the guard those
  levels would misbehave (EvilBase's stone explosions would throw debris whenever its 0x53 is on, TowerC would
  fail its mission on 0x7b, EvilSilo on 0xc7). The exception is `0xa4`: no level but CastleExterior touches it,
  so its two guards are defensive in the shipped data - except that the channel could carry into CastleCourtyard
  on a warm reset, where the guard does change behaviour.
- **Neither** - scoped only by a drone type or state, or a level-data value compared in code. These stay numbers
  at their uses, with a comment.

## Hard-coded channels in the action engine

Addresses are the absolute byte the instruction touches; the channel is that minus `0x001df138`. "W=1" means the
site only ever writes 1.

### Game-wide

| Channel | Name | Touched by | | What the code does with it |
|---|---|---|---|---|
| `0x0a` | `SWITCH_SPACE_DRAKE` | `NDrone2_DSTATE_SpaceDrake` | R | while on, the drake takes bullet impacts, and is deleted on update |
| `0x35` | `SWITCH_REMOVE_DYING` | `NDrone2_DSTATE_Death_Anim` | R | dying drones whose death channel is `0x88`, `0x29` or `0x2a` set that channel and are removed |
| `0x5e` | `SWITCH_PLAYER_DIED` | `Player_CheckForDeath` W=1 (single player), `Player_Init` W=0 | W | the player has died |
| `0x5f` | `SWITCH_PLAYER_NO_PAIN` | `Player_HandlePain` | R | pain does nothing |
| `0x60` | `SWITCH_FAIL_CIVILIAN_KILLED` | `DroneFunc_SetMissionFailReason` (reasons 1, 3, 6, 9) W; `Mission_MonitorObjectives`, `Trigger_Activate` R | RW | fails the mission (`TXT_MISSION_FAIL_KILLED_CIVILIAN`); stops a level-load trigger |
| `0x61` | `SWITCH_FAIL_ALARM` | `DroneFunc_SetMissionFailReason` (reason 16, label only), `DroneFunc_CheckAlarmRaised`, `Mission_MonitorObjectives`, `Trigger_Activate`, `Switch_Activate` | R | fails the mission (`TXT_MISSION_FAIL_ALARM_TRIGGERED`), ten seconds of alarm music, stops a level-load trigger; the player can't flip a switch on it |
| `0x62` | `SWITCH_PLAYER_FINISHED_DYING` | `Player_HandleDeath` W=1, `Player_Init` W=0; `Mission_Update`, `Trigger_Activate` R | RW | the blood pane has run and skip was pressed: the fail screen |
| `0x63` | `SWITCH_MISSION_FAILED` | `DroneFunc_SetMissionFailReason` (and so `DroneFunc_SetDeathChannel` for death channel 99), `P_CHEATMEDAL_Handler` (item 0), `Mission_MonitorObjectives` W=1; `Mission_Update`, `Trigger_Activate` R | RW | the mission fails; stops a level-load trigger |
| `0x64` | `SWITCH_MISSION_WON` | `Mission_MonitorObjectives` (every objective met), `P_CHEATMEDAL_Handler` (items 1-5) W=1; `Mission_Update` R | RW | the mission is won |
| `0x65`-`0x6e` | `SWITCH_BOND_MOMENT_FIRST` / `_LAST` | `Trigger_Calc` R, `Trigger_Update` | R | each scores its bond moment while on; a hidden multiplexer on one goes once it is set |
| `0x96` | `SWITCH_GLOBAL_ALARM` | `Drone_InitComms` R, `Drone_PostLoad_Init` W=0 (EvilBaseC only) | RW | `DRONE_MSG_GlobalAlarm` every 30 frames (once in EvilBase) |
| `0x97` | `SWITCH_ENEMY_ALERTED_NEAR_LATCHED` | `Drone_UpdateCombatChannels` | W=1 | an alerted enemy drone came within 30 m of its opponent; nothing reads it |
| `0x98` | `SWITCH_ENEMY_ALERTED_NEAR` | `Drone_UpdateCombatChannels` W (every frame), `NDrone2_PowerstationTalk` R | RW | whether one is; the Powerstation loudspeakers need it |
| `0x99` | `SWITCH_MISSION_WAIT` | `NDrone2_ReachedDestNode`, `NDrone2_DSTATE_AllyLeadMissionWait` (its default) | R | until on, a mission wait node holds the ally |
| `0x9b` | `SWITCH_ZOE_LEASH` | `NDrone2_ControlDTYPE_Zoe` | RW | set once channel `0x01` is on; while on, the player more than 10 m from Zoe fails the mission (reason 15) |
| `0xfd` | `SWITCH_MP_SCORE_REACHED` | `MP_CheckForEndCondition` | RW | multiplayer only: a score has reached the limit |
| `0xfe` | `SWITCH_MP_TIME_UP` | `MP_CheckForEndCondition` | RW | multiplayer only: the time limit has passed |

`0x63` and `0x64` are the mission's own outcome: the medal cheat (`P_CHEATMEDAL_Handler`) just sets one of them.
`0xfd` and `0xfe` are reused by EvilSilo's single-player data, which never meets the multiplayer code.

### Under a level guard

| Channel | Name | Touched by | Guard | What the code does with it |
|---|---|---|---|---|
| `0x09` | `SWITCH_SPACESTATIOND_HUD_BLINK0` | `HUD_UpdateSpacePane` R | SpaceStationD | the space pane's sprite 0 blinks |
| `0x18` | `SWITCH_SPACESTATIOND_HUD_BLINK9` | `HUD_UpdateSpacePane` R | SpaceStationD | sprite 9 blinks |
| `0x1c` | `SWITCH_SPACESTATIOND_HUD_BLINK10` | `HUD_UpdateSpacePane` R | SpaceStationD | sprite 10 blinks |
| `0x37` | `SWITCH_CASTLE_ALARM` | `DroneFunc_CheckAlarmRaised` R | CastleExterior, CastleCourtyard | ten seconds of the alarm music (generic cue) |
| `0x48` | `SWITCH_CASTLEINDOORS1_ARMED` | `Player_Move`, `Player_Weapon` (and its `_prev`), `Player_HandleJump` R | CastleIndoors1, all three | until on: 0.9 speed, unarmed weapon cycling, no jump (`SFX_FRONT_END_MENU_ITEM_ERROR`); the frame it goes on, the upgraded P2K |
| `0x4a` | `SWITCH_CASTLEINDOORS2_ZOE_LAST_LINE` | `NDrone2_HandleTalking` R | CastleIndoors2 | Zoe says line `0x9e` and stops talking |
| `0x53` | `SWITCH_CASTLEINDOORS2_STONE_DEBRIS` | `Explode_Update` R | CastleIndoors2 | explosions of materials 8, 10 and 11 throw stone debris |
| `0x76` | `SWITCH_TOWERC_SSYS_SPEAKS` | `SSys_Msg` R | TowerC | the security system is quiet until on |
| `0x7b` | `SWITCH_EVILBASEC_FAIL_KIKO` | `Mission_MonitorObjectives` R | EvilBaseC | fails the mission (`TXT_MISSION_FAIL_KIKO_ESCAPE`) |
| `0x7c` | `SWITCH_TOWER2B_FELL` | `Player_CollisionHandler` W=1 | Tower2B | the player is below `y = -10` |
| `0x7d` | `SWITCH_LEVEL7001500_TOUCH` | `Trigger_Update` | not Level7001500 | a touch trigger sets it only in `HT_Level_Level7001500` |
| `0xa4` | `SWITCH_CASTLEEXTERIOR_ALARM` | `DroneFunc_CheckAlarmRaised`, `Trigger_Calc` R | CastleExterior (both) | a second of the castle alarm music (cue 3); sets `0x6f` |
| `0xc7` | `SWITCH_SPACESTATIOND_FAIL_MISSILE` | `Mission_MonitorObjectives` R | SpaceStationD | fails the mission (`TXT_MISSION_FAIL_MISSILE_LAUNCHED`) |

Notes:

- Level 0x07001500 has no bundle, so `0x7d`'s rule means CastleExterior's touch trigger on `0x7d` never fires
  and the script player it would start never runs (nothing else writes `0x7d` there).
- `Player_Weapon`'s read of `0x48` is guarded by CastleIndoors1 like the other two.
- `Explode_Update`'s `switch` is over the explosion's material, not a channel; only the stone cases read `0x53`.

### Left as numbers

| Channel | Where | Why |
|---|---|---|
| `0x01` | `NDrone2_ControlDTYPE_Zoe` R | starts Zoe's leash (`0x9b`); scoped by `DTYPE_Zoe` only, and used for unrelated things in ten levels' data |
| `0x0a` | `Drone_EnableAll` | after `HT_Script_DronesEnableAfter` (0x0600002e), enables the drones in state 1 whose wait channel is 10 - a comparison with a drone's wait channel, not `SWITCH_SPACE_DRAKE`. The script is in none of the extracted bundles' scripts, so the rule looks dead in the shipped data |
| `0x11`, `0x19` | `NDrone2_init_DMODE_CastleChatGuard1/2`, `NDrone2_DoModeSettingsNEW` W (the drone's `chatSwitchChannel`) | `NDrone2_DSTATE_CastleChatGuard1` plays its script once that channel is on |
| `0x1a` | `NDrone2_DSTATE_CastleChatGuard1` W=1 | set on leaving with the script not played; scoped by the state, reused in four other levels |
| `0x2d`, `0x34` | `Trigger_Calc` | TowerC only: `0x34` on sets `0x2d`. The code shows nothing more |
| `0x6f` | `Trigger_Calc` W=1 | CastleExterior only: set while `0xa4` is on |
| `0x88`, `0x29`, `0x2a` | `NDrone2_DSTATE_Death_Anim` | death channels from the level data, compared with the drone's; they only matter while `0x35` is on (HendersonB has all three; Tower2C has `0x29`/`0x2a` death drones, and Tower2A sets `0x35`, which can carry over) |
| `100` | `Copter_Move`, `Copter_Update` | `copter->channel == 100` marks a copter that never attacks and goes at its path's end; not a channel use (the delay key is 0 on every copter, so `SetChannel(100)` never runs) |
| `0xff` | `SSYS_NO_CHANNEL`, an objective's `revealedChannel` | "no channel", beside 0 |

### Per-level tables in the code

- `PowerstationLines[19]` (NDrone2_Talk.cpp): each loudspeaker line's channel in PowerStationA1 and in
  PowerStationA2 - the same line has a different number in each.
- The objective tables (Mission.cpp): each objective's completed and revealed channels, per mission part. 72
  (`0x48`) is an objective channel in CastleIndoors1, HendersonB, TowerB, Tower2A and EvilBase; Tower_2A's
  objective 0 completes on 100 (`SWITCH_MISSION_WON`).

## Finding these yourself

Searching for the array's base address finds the data-driven sites, because those compile to
`[reg + 0x1df138]`. The hard-coded ones compile to an absolute address instead, so they need a search for
`[0x001df1..]` and `[0x001df2..]`.

Note the leading zeros: Ghidra prints operands zero-padded, so a search for `0x1df180` finds nothing at
all while `0x001df180` finds everything. That has cost this project real time more than once.
