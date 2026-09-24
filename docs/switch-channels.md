# Switch channels

A switch channel is one byte of per-level boolean state. There are 256 of them, in a plain array:

```c
#define switch_channels      (*(char(*)[256])0x001df138)   // the current state
#define switch_channels_prev (*(char(*)[256])0x001dee38)   // last frame, for edge detection
#define switch_channels_hold (*(char(*)[256])0x001df238)
#define switch_channels_time (*(uint32_t(*)[256])0x001df428) // NumFramesUnpaused when it last changed
```

`Init_SwitchChannels` clears the lot at level load, so nothing survives a level change. They are the
game's general-purpose wiring: a switch object sets one, a door reads one and refuses to open, a light
reads one and turns itself off, a mission objective reads one and ticks itself off. Almost all of the
scripting in a level is built out of them.

## Two kinds of user, and only one of them is interesting here

**Data-driven, which is the overwhelming majority.** The channel number comes out of the map data, held
in the object: `light->switchChannel`, `sensor->alertSwitchChannel`, `spotlight->disableSwitchChannel`,
`rotor->switchChannel`, `scriptPlayer->someSwitchChannel`, `objective->completedChannel`, and so on.
Those tell you nothing about what any particular number means, because the meaning lives in the level
file rather than in the executable.

**Hard-coded in the engine**, which is what the table below records. There are 70 such references in the
action executable, and they are worth knowing about because they are the ones that will surprise you: a
number appearing in a comparison with no explanation of where it came from.

## Do these numbers mean the same thing in every level?

Not necessarily, and that is the important caveat.

Nothing enforces a global meaning. The state is cleared per level, and the level data is free to assign
any channel number to any object, so in principle channel `0x48` could be a door in one level and the
engine's castle check in another.

In practice the hard-coded numbers are scattered right across the range - `0x09`, `0x0a`, `0x18`, `0x1a`,
`0x1c`, `0x2d`, ... `0xfd`, `0xfe` - rather than confined to a reserved block at one end. That only works
if the numbering was coordinated between the engine and whatever tool authored the levels, which points
at a shared enum on the toolchain side that did not survive into the shipped binary as anything but
literals.

There is direct evidence the authors did not fully rely on that, though: **the engine's own hard-coded
reads are often paired with a level check.** Both `Player_Move` and `Player_HandleJump` read `0x48` only
after testing `GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1`, and `Player_CollisionHandler`
writes `0x7c` only in `HT_Level_Tower2B`. So treat a hard-coded channel as meaning what the table says
*in the levels that check for it*, and assume nothing about it elsewhere.

## Hard-coded channels in the action engine

Addresses are the absolute byte the instruction touches; the channel is that minus `0x001df138`. "W=1"
means the site only ever writes 1.

| Channel | Address | Touched by | | Inferred meaning |
|---|---|---|---|---|
| `0x09` | `0x001df141` | `HUD_UpdateSpacePane` | R | space level HUD state |
| `0x0a` | `0x001df142` | `NDrone2_DSTATE_SpaceDrake` | R | space level, Drake behaviour |
| `0x18` | `0x001df150` | `HUD_UpdateSpacePane` | R | space level HUD state |
| `0x1a` | `0x001df152` | `NDrone2_DSTATE_CastleChatGuard1` | W=1 | castle guard conversation reached |
| `0x1c` | `0x001df154` | `HUD_UpdateSpacePane` | R | space level HUD state |
| `0x2d` | `0x001df165` | `Trigger_Calc` | W=1 | - |
| `0x34` | `0x001df16c` | `Trigger_Calc` | R | - |
| `0x35` | `0x001df16d` | `NDrone2_DSTATE_Death_Anim` | R | - |
| `0x37` | `0x001df16f` | `DroneFunc_CheckAlarmRaised` | R | alarm state |
| **`0x48`** | `0x001df180` | `Player_Move`, `Player_HandleJump`, `Player_Weapon` | R | **cover blown in the castle** - see below |
| `0x4a` | `0x001df182` | `FUN_0003c430` | R | - |
| `0x53` | `0x001df18b` | `Explode_Update` | R | - |
| `0x5e` | `0x001df196` | `Player_CheckForDeath` W=1, `Player_Init` W | RW | the player has died |
| `0x5f` | `0x001df197` | `Player_HandlePain` | R | - |
| `0x60` | `0x001df198` | `DroneFunc_SetMissionFailReason`, `DroneFunc_SetDeathChannel`, `DroneFunc_OnInitDeath`, `NDrone2_PunchImpact` W; `Mission_MonitorObjectives`, `Trigger_Activate` R | RW | a drone died / mission failure reason |
| `0x61` | `0x001df199` | `DroneFunc_CheckAlarmRaised`, `Mission_MonitorObjectives`, `Trigger_Activate` | R | alarm raised |
| `0x62` | `0x001df19a` | `Player_HandleDeath` W=1, `Player_Init` W; `Mission_Update`, `Trigger_Activate` R | RW | player death handled |
| `0x63` | `0x001df19b` | `DroneFunc_SetMissionFailReason`, `DroneFunc_OnInitDeath`, `DroneFunc_HandleImpact`, `DroneFunc_FirstAttack`, `NDrone2_PunchImpact`, `P_CHEATMEDAL_Handler`, `Mission_MonitorObjectives` W=1; `Mission_Update`, `Trigger_Activate` R | RW | stealth broken / mission compromised |
| `0x64` | `0x001df19c` | `P_CHEATMEDAL_Handler` (five sites), `Mission_MonitorObjectives` W=1; `Mission_Update` R | RW | medal or cheat condition |
| `0x6f` | `0x001df1a7` | `Trigger_Calc` | W=1 | - |
| `0x76` | `0x001df1ae` | `SSys_Msg` | R | - |
| `0x7b` | `0x001df1b3` | `Mission_MonitorObjectives` | R | - |
| `0x7c` | `0x001df1b4` | `Player_CollisionHandler` | W=1 | fell out of the world in `HT_Level_Tower2B` (`position.y < -10`) |
| `0x96` | `0x001df1ce` | `Drone_PostLoad_Init` W, `Drone_InitComms` R | RW | - |
| `0x97` | `0x001df1cf` | `FUN_000314a0` | W | - |
| `0x98` | `0x001df1d0` | `FUN_000314a0` W, `FUN_0003b880` R | RW | - |
| `0x99` | `0x001df1d1` | `NDrone2_ReachedDestNode` | R | - |
| `0xa4` | `0x001df1dc` | `DroneFunc_CheckAlarmRaised`, `Trigger_Calc` | R | alarm state |
| `0xc7` | `0x001df1ff` | `Mission_MonitorObjectives` | R | - |
| `0xfd` | `0x001df235` | `MP_CheckForEndCondition` | W | multiplayer round end |
| `0xfe` | `0x001df236` | `MP_CheckForEndCondition` | RW | multiplayer round end |

Only `0x48` is confirmed rather than inferred. The blanks are honest - the function name is all the
evidence there is so far, and guessing would be worse than leaving them empty.

### `0x48`, in detail

The one channel whose meaning is known from playing rather than from reading: it is set once Bond has
rendezvoused with Zoe in the castle, i.e. once he has been told to break his cover as a party guest.
Until then the engine holds him to what a guest would plausibly do, in three places:

- `Player_Move` multiplies the movement speed by 0.9, so he walks rather than jogs;
- `Player_HandleJump` refuses to jump and plays the menu error sound instead;
- `Player_Weapon` reads it too (`0x000bac69`), presumably to stop him drawing a gun - not yet confirmed,
  and unlike the other two its level gating has not been checked.

## Finding these yourself

Searching for the array's base address finds the data-driven sites, because those compile to
`[reg + 0x1df138]`. The hard-coded ones compile to an absolute address instead, so they need a search for
`[0x001df1..]` and `[0x001df2..]`.

Note the leading zeros: Ghidra prints operands zero-padded, so a search for `0x1df180` finds nothing at
all while `0x001df180` finds everything. That has cost this project real time more than once.
