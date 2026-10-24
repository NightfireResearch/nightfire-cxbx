# Cutting-room floor - action engine, Xbox EU

This page lists content that the retail Xbox disc carries in code or data but that normal play never reaches. Each
entry says what is present, what is missing and how it was checked. Unless an entry says otherwise, "the disc" means
the Xbox EU disc in `disc/`. The level bundles were read with the anim branch's archive tools (`edl.py`, `bundle.py`
from commit 7af6075).

| Item | Code | Data on the disc | Reachable |
|---|---|---|---|
| [Subs](#the-sub-and-the-mini-sub) | yes | no skins or animation scripts | no, and it would fail if forced |
| [Creatures](#creatures) | yes | the fish skin | no level places one |
| [Movie selector](#movie-selector) | yes | yes | only by injecting a page change |
| [Debug pages](#the-debug-pages) | yes | yes, in every level's pause menu set | only by injecting a page change |
| [Test levels](#test-levels) | some special cases | no level bundles | no |

## The sub and the mini sub

- `OBJECTTYPE_SUB` ([Sub.cpp](../src/action/game/obj/Sub.cpp)) is complete code:
  - it follows its path while its switch channel is on;
  - it rams, and fires missiles and torpedoes;
  - once destroyed it sinks to the seabed.
- The sub also has a laser, but nothing ever creates its beam object, so it never fires.
- `OBJECTTYPE_MINISUB` ([MiniSub.cpp](../src/action/game/obj/MiniSub.cpp)) is also complete code. It hunts player 1
  over the AI network and fires torpedoes. Of the creation paths, the one that carries the celglist always passes 0
  for it ([drone architecture](drone/architecture/README.md)).
- **There is no geometry for either sub.** These are what the code asks for:

  | | Skin | Animation scripts |
  |---|---|---|
  | Sub | 0x05000068 | 0x060006d9 (moving), 0x0600075e (sinking) |
  | Mini sub | 0x05000074 | 0x06000741 |

  None of these is in any of the disc's 32 level bundles:
  - The 32 bundles hold 150 distinct skins between them, and these are not among them.
  - None of the hashes is a standalone file in the archive either. Hash-style names (the first 0x1000 of each type
    0x01-0x0a) were tried against the archive index (CRC of the name), and only the 32 bundles matched.
  - The raw hash bytes do occur inside the bundles, but only at unaligned offsets in texture data, which is
    coincidence.
- No level places either sub. If one were placed, it would fail when its skin was attached.
- The ports are checked by shadow tests only; there is no data to run them on.

## Creatures

- `Creature` ([Creature.cpp](../src/action/game/obj/Creature.cpp)) swims between waypoints, which it plans at random
  around the spot where it was placed. There is also a fish variant.
- No level places one.
- Unlike the subs, the creatures' fish skin (0x05000002, `fish_body1`) and its skeleton are on the disc. The skin is
  in the character export (`dump_characters/`).

## Movie selector

`P_FMVTEST` and `P_FMVPLAYER` are a developers' movie browser with 22 hard-coded entries. Both pages are in the front
end's menu data and both handlers are in the XBE. Nothing ever opens the page, so it is reachable only by injecting
`GoPage(0x4000004f)`. One entry names a movie that is not on the disc. The full study is in
[ui/movie-selector.md](ui/movie-selector.md).

## The debug pages

Every level's in-game menu set 0x80000002 also builds a developers' cheat page, 0x40000007 (our name for it is
`P_DEBUGMENU`). It holds:

- `C_LBPMMAP`, a level select. It lists the levels and loads the one picked.
- The eighteen `C_CHCH*` checkboxes: blind drones, health, coordinates, draw all, fly, zero G, unlock and so on.
- Buttons whose scripts go to `P_NIS`, `P_TWEAKS` (56 controls), `P_TWEAKS2` (47) and `P_CHEATMEDAL`.

Nothing in the code or the menu data goes to 0x40000007: no immediate value anywhere in the XBE, and no script. The
pause manager builds every page of the set, though, so the pages and their controls exist whenever the game is
paused, and `gopage 0x40000007` after pausing reaches them. See [ui/framework.md](ui/framework.md).

## Test levels

The game's hashcode list ([assets.h](../src/action/assets.h)) names some levels that have no bundle on the Xbox disc:

| Hashcode | Name |
|---|---|
| 0x0700000e-10 | `Cut_Level1`-`3` |
| 0x07000021 | `SpaceStation` |
| 0x07000022 | `Facility` |
| 0x07000041 | `RefRoom` |
| 0x07000043 | `AllCharacters` |
| 0x07000046 | `TestRoom2` |
| 0x07000049 | `AllCharacters2` |
| 0x07000090 | `MovieMap` |

These are the level bundles the disc does have:

- 0x07000001-0d, 0x07000011-16, 0x0700001b, 0x07000023-29;
- 0x07000048, the front end;
- 0x0700004a and 0x0700004b;
- 0x07000500, the shared files loaded before every level;
- 0x07000999, a bundle that holds only two type-0 files and the script 0x060005e2. Nothing has looked at it yet.

The code still special-cases some of the missing levels:

- `AllCharacters` and `AllCharacters2` have their own gamma ramp, the Castle's, and draw no blob shadows
  ([psiDraw.cpp](../src/action/engine/psiDraw.cpp)).
- `RefRoom` has its own drone combat and alert rules
  ([NDrone2_Combat.cpp](../src/action/game/drone/NDrone2_Combat.cpp),
  [NDrone2_States.cpp](../src/action/game/drone/NDrone2_States.cpp)).

The test levels (the character test room and the engine test room) are known to exist and work on PS2. On Xbox their
data was left off the disc, so even the debug level select could not load them. Which PS2 bundle matches which name
above has not been checked.
