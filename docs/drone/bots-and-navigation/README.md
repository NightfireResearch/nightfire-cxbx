# Multiplayer bots and AI navigation (Xbox action engine)

Scope: the `BOT_*`, `BOTSTATE_*` and `BOTWEAP_*` functions, the bot DSTATEs 195..249, and the navigation layer that
drones and bots share: `AIPath_*`, `AIBounds_*`, `AINetwork_*`, `LinkCreep_*`, emitters and routes. The NDrone2
lifecycle and state machine are covered by report A, and behaviours, perception, combat and animation by report B.
This report links to them where the areas meet.

Appendices in this folder:

- `function_inventory.csv`: 212 functions with address, size, role, callers (from `tools/xrefs_action.json`) and
  whether we already own them.
- `appendix-tables.md`: `default_bot_stats` decoded, the bot rows of the DSTATE table, and the bot state-class table.
- `appendix-ai-paths-census.md`: every AI path and bound record in the 28 map files (name, flags, node and link
  counts, node flags).
- The scripts that produced them were research tools and are not in the repository (see ../README.md).

Evidence labels: **[D]** decompiled in Ghidra (default.xbe), **[A]** checked against the disassembly, **[X]** from
the xref dump, **[L]** measured on the real level files, **[PS2]** the name or shape comes from the PS2 ELF, and
**(inferred)** means reasoning that has not been verified directly. All addresses are Xbox.

---

## 1. Overview

**Navigation.** Each map file has one block, 0x05, parsed by `AIPath_Parse`. It holds up to 50 *paths* and up to
50 *bounds*. A path is a graph of nodes and links. Its type flag says how it is used:

| path flags bit | meaning | how it is used |
|---|---|---|
| 0x1 | **BOUND**: a closed outline of the walkable area ("Boundary") | kept in its own table; tested by the movement and collision checks |
| 0x2 | **nav graph** ("Default Navigation", "NavPath - A", "BotPath2") | A* routes and emitters. `AINetwork_NavPathForPosition` accepts any path with `flags & 0xd == 0` |
| 0x4 | **patrol**: a loop ("PatrolPath", "(patrol)") | `NDrone2_AssignAIPath(mask 4)`; the route runs in loop mode |
| 0x8 | **mission**: a one-way path ("Mayhew Route", "TruckDriverPath", "AstroPath_1a") | `NDrone2_AssignAIPath(mask 8)`; the route runs in once mode |
| 0x10 | patrol modifier (5 uses) | sets route flag 0x20, which changes how the patrol order is built (`FUN_00044a20`) |
| 0x200, 0x80000000, and high bytes such as 0x3d00, 0xff00, 0xa900 | no reader found | probably editor leftovers (inferred) |

There is **no precomputed path table**. Routes are found at run time:

1. The goal is marked on the graph. `AINetwork_NodesForPosition` sets a target bit on every node from which the
   goal position can be walked to.
2. A* runs over one path's graph. It starts from the nodes the mover can walk to within 25 m, and ends at any node
   that carries the target bit (`AINetwork_DoAStarPath`).
3. The result is shortened (`OptimiseRoute`) and then followed in 0.4 m steps along each link ("link creep",
   `LinkCreep_*`). Every step is checked against the bounds.

For "how far is X from anywhere", the engine floods the graph once from X with Dijkstra (`AINetwork_EmitPath`) and
keeps one byte per node, the "emitter". MP pickups, MP objectives, cover nodes, AI points and the drones' "safety"
position each have one.

**Bots.** A bot is **an NDrone2**, not a separate object type. `BOT_init` calls `Drone_Create` with a synthetic
`level_tag`. The drone's type byte `drone+0xa9` is 30, `drone+0x974` points to a per-bot `BOT_vars_t` (0x768
bytes, 6 of them at 0x1d98e0), and the bot runs the Bot* DSTATEs (195..249) of the same state table the other
drones use. What makes a bot different:

- a veto hook in `Drone_SM_SetState` (`BOT_validateStateChange`)
- a per-frame "brain" state, `DSTATE_BotGlobal`
- a two-slot goal system: slot 1 is the game-mode objective, and slot 0 is a pickup or a preferred opponent.
  Pickups are chosen by emitter distance, the pickup's kind and the bot's needs, with per-bot "visited" times on
  every MP pickup
- stats from `default_bot_stats` or the bot setup page
- a personality that picks the preferred opponent
- its own weapon inventory (`BOTWEAP_*`)

Bots move with the same route code as drones: `NDrone2_MoveToGoalPosition` and `FUN_00046d90` over
`drone->someAiRoute`.

---

## 2. Level data: block 0x05

`parsemap_handle_block_id` (0xa6820), case 5: `AIPath_Parse(FileNextBlock)` **[D]**. The block is only in map
files. The byte layout was decoded to the end of the block in all 28 maps by `tools/level/parse_level.py`
(`decode_ai_paths`, branch blender-exports) **[L]**:

```
u32 ?; u32 version (must be 8, else the whole block is ignored); u32 count
record:
  char name[0x80]
  u32 flags                      -> entry+0x08
  u32 mid[8]                     -> entry+0x0c..+0x28 (zero in every file)
  u32 nodeCount                  -> entry+0x2c
  u32 extraBytes; u8 extra[extraBytes]   (0, 4, 8 or 12 bytes of 00 01 02 ..: padding)
  PATH : nodeCount x 0x40 file nodes, u32 linkCount, linkCount x 0x10 links
  BOUND: nodeCount x 0x20 file nodes, u32 linkCount, linkCount x 0x14 links
```

The file data **stays in place** and is patched by the parser. Runtime arrays point into it (the pointer to it lives
in the discard area of the level heap):

- **file node (path, 0x40; bound, 0x20)**: +0 u16 index (written), +2 u16 path number (written), +4 u32 node flags
  (0x2468abce is mapped to 0), +0xc cel* (written by BindNodes), +0x10 vec3 position (y += 0.4 at load), +0x1c
  1.0 ... The flags word at +4 is also the **run-time scratch word of the searches**, described in 4.1.
- **path link (0x10)**: +0 u16 index, +2 u16 path number, +4 u16 link flags (0 in the file), +6 u16 used count, +8
  u16 node A, +0xa u16 node B, +0xc f32 length (computed by `AIPath_Prepare`).
- **bound link (0x14)**: +0 u16 index, +2 path number, +4 u32 flags, +8 u16 A, +0xa u16 B, +0xc and +0x10 are
  next-in-cel links for the cels of end A and end B (`AIBounds_Prepare`).

Census **[L]** (`appendix-ai-paths-census.md`): 177 paths and 33 bounds. Only Tower2Elevator has more than one
bound (7). The single-player maps have 1 to 27 paths each. Every **multiplayer map has exactly one nav graph and one
bound**, for example FortKnox "Default Navigation" with 279 nodes and 319 links. **Ravine has no block 0x05**, which
is why P_MPOPTIONS turns bots off on Ravine (`NO_BOTS_ON_RAVINE` in ui_mp.cpp). Node flags in the files: 20152
zero, 326 = 0x2 (door nodes), 31 = 0x1, 4 = 0x4, and 0x20, 0x10000, 0x20000, 0x40000 and 0x80000 once each, all on
"Mayhew Route".

### 2.1 Load sequence

| when | function | what |
|---|---|---|
| level reset | `AIPath_Init` 0xa6c20 (from ResetMap_GameInit) | AIPathCount = AIBoundCount = 0, maxNodes = maxLinks = 0 |
| map parse | `AIPath_Parse` 0xa6c40 | fills the tables; allocates the runtime node arrays: `nodeCount*0x30` per path, `nodeCount*8` per bound (Mem tag "malloc_pathpointers") |
| after all files | `AIPath_BindNodes` 0xa72a0 (from ResetMap_Load, after build_LinkDoors2Portals) | path nodes are pushed onto their cel's list (`cel+0x24`); `AIPath_Prepare` works out link lengths and each node's link list; bound nodes go onto `cel+0x30` (`AIBounds_Link2Cel` also casts a ray upward to get the headroom, at least 2 m, into file node +8); bound links go onto `cel+0x34` (`AIBounds_Prepare`) |
| drone post-load | `Drone_PostLoad_Init` 0x31fd0 | allocates the shared "safety" emitter (maxNodes bytes); emits the cover and AI-point emitters; per-level move-test constants at 0x274cac..0x274cbc (max slope 50°, or 45° and step 1.5 in CastleIndoors2; 30.0, 900.0, 4.0); runs `AINetwork_InitPassableBoundries` |
| MP post-load / pickups | `MP_RegisterPickup`, `MP_initObjExt`, `MP_recalcObjExtPaths` | one emitter per pickup and objective, **only when bots are active** (`mpbots.numActiveBots`) |

GameCube checks (docs/gamecube-checks.md rows 41 and 79-86): `AIPath '%s' NODE (%d) IS NOT IN A ROOM` (BindNodes),
`NO NAVIGATION PATH NEAR EMITTER` (InitEmitter), `Link refers to non-existant AI path` (EmitPath and
GetNodeAtDistance), `Likely infinite loop` for an A* backtrack longer than 100000 steps, `Could not find path
link`, and `Patrol Path '%s' is NOT looping`.

---

## 3. Data structures

### 3.1 Globals **[D][X]**

| address | name (ours/Ghidra) | size | written by | read by |
|---|---|---|---|---|
| 0x274ca8 | block version | 4 | AIPath_Parse | - |
| 0x274cac..0x274cbc | move-test constants (slope angle, step height, 30, 4, 900) | 5 floats | AIPath_Init (first), Drone_PostLoad_Init | NDrone2_MoveTestToNode |
| 0x274cc0 | max nodeCount over all paths (Ghidra `AIRouteDataSizeBytes`) | 4 | AIPath_Parse | Drone_PostLoad_Init (safety emitter size), NDrone2_DefaultInit (route node buffers) |
| 0x274cc4 | max linkCount | 4 | AIPath_Parse | - |
| 0x274cc8 | **AIPathCount** | 4 | Init/Parse | AINetwork_*, BindNodes, kick nodes |
| 0x274ccc | **AIPath table**, 50 x 0x48 | 0xe10 | Parse | see 3.2 |
| 0x275adc | **AIBoundCount** | 4 | Init/Parse | bounds tests, LinkCreep, MoveTestToNode |
| 0x275ae0 | **AIBound table**, 50 x 0x40 | 0xc80 | Parse | bounds tests |
| 0x1e57f0 | the player's AITarget (NPCGlobals+0x1c0), target bit 0x1000000 | 0x30 | FUN_00044de0 each frame (Drone_InitComms, MiniSub_Update) | routes whose target is the player (`FUN_00049ab0` stores 0x1e57f0 in `route+0x48`) |
| NPCGlobals.safetyEmitter | AIEmitter | 0x28 | Drone_PostLoad_Init | DroneMove_FindSafetyFromScaryPosition |

Neither table has a count check. More than 50 records would overflow, but no level has more than 27.

### 3.2 AIPath table entry (0x48; bounds use the first 0x40 with the same meaning)

| off | type | meaning |
|---|---|---|
| +0x00 | u16 | index |
| +0x04 | ptr | raw record (its name is at +0) |
| +0x08 | u32 | flags (table in section 1) |
| +0x0c..+0x28 | u32[8] | copied from the file; always 0 |
| +0x2c | u32 | nodeCount. This is also the emitter size (Ghidra `AIPath_tag.emitterSizeBytes`) |
| +0x30 | ptr | first file node |
| +0x34 | ptr | runtime nodes (0x30 each for paths; 8 bytes each for bounds: +0 next-in-cel, +4 file node) |
| +0x38 | u32 | linkCount |
| +0x3c | ptr | first link (in the file) |
| +0x40 | u16* | node->link index lists (AIPath_Prepare, 2 x linkCount entries); paths only |
| +0x44 | u32 | **reach mask** for the target bits (set by NodesForPosition, tested by DoAStarPath); paths only |

### 3.3 Runtime path node (0x30) **[D]**

| off | meaning |
|---|---|
| +0x00 | next node in the same cel (list head at `cel+0x24`) |
| +0x04 | file node* (index at +0, flags at +4, cel at +0xc, pos at +0x10) |
| +0x08 | next in the sorted candidate list (NodeSearchCel) |
| +0x0c | 2D distance to the search origin |
| +0x10 | u8: which link of the parent led here |
| +0x11 | u8: link count |
| +0x12 | u16: first entry in the path's node->link list |
| +0x14 | g (cost so far). The emitter initialises it to 255 |
| +0x18 | h (2D distance to the destination) |
| +0x1c | f = g + h (the sort key of the open list) |
| +0x20 | parent node |
| +0x24 | next in the open or closed list |
| +0x28 | door obj* (a link between two door nodes is closed when `Door_IsLocked`) |

**File node flags (+4) at run time:** bit 0x1 and the low action bits (`& 0xf007f`) mark a "special node": when a
route reaches it, `NDrone2_ReachedDestNode` runs (report A/B). Bit 0x2 marks a door node. The target bits are set
by NodesForPosition: 0x1000000 player, 0x2000000 player (obstructed only), 0x10000000 goal, 0x20000000 goal
(obstructed only). 0x40000000 = open list and 0x80000000 = closed list, used by A*, the emitter and
GetNodeAtDistance.

### 3.4 AIRoute_tag (0xa0; `drone+0x634` for the dynamic route, `drone+0x6d8` for the patrol/mission route) **[D][PS2 layout differs: 0x100]**

| off | meaning |
|---|---|
| +0x00 | u16 flags: `&7` mode (1 once/clamp, 2 loop, 3 ping-pong), 0x8 walking backwards (ping-pong), **0x10 valid**, 0x20 from path flag 0x10, 0x40 destination moved (DROUTE_Nearest), 0x80 never walk directly |
| +0x04 | u8 status: the last result code (table below) |
| +0x08 | u32 frame when the target was built (copied from AITarget+0x2c; VerifyRouteTarget re-plans when the target is newer) |
| +0x0c | previous start node (u16, unaligned) |
| +0x0e / +0x10 | u16 start node / end node |
| +0x14 | CelPos start (the mover's feet) |
| +0x24 | CelPos destination |
| +0x34 | CelPos current waypoint (what the drone steers at) |
| +0x44 | s16 current index into the node list (-1 = before the first node) |
| +0x46 | u16 node count |
| +0x48 | AITarget* |
| +0x4c | f32 remaining distance (GetRouteDistance) |
| +0x50 | f32 arrive radius (the status becomes 3 when the remaining distance drops below it) |
| +0x58..+0x5b | creep active, special-node pending, flags |
| +0x5c | s16 link being crept (link flag 0x10 is set while crept) |
| +0x60 | AIPath* of the creep; +0x64/+0x66 u16 from/to route index |
| +0x68 / +0x6a | s16 creep step / step count (steps = int(len)*2.5) |
| +0x6c | vec3 step (0.4 m); +0x78 segment start; +0x84 segment end |
| +0x90 | u16* node list (buffer sized from 0x274cc0) |
| +0x94 | AIPath* |
| +0x98 / +0x9c | mover obj / target obj |

Status codes (route+4, and the return values of CalcRoute and FollowRoute) **[D]**:

| code | meaning |
|---|---|
| 0 | following / full route |
| 1 | partial route: only obstructed-reach nodes were found; `AlterDestFor_DROUTE_Nearest` moves the destination |
| 2 | direct: no graph needed (LinkCreep_CalcToRouteEnd) |
| 3 | arrived |
| 4 | target has no reachable nodes |
| 5 | start is not near the network (no node walkable within 25 m) |
| 6 | no route (open list exhausted) |
| 7 | no path assigned |
| 8 | no target, no network, or no nodes |
| 9 | creep failed |
| 10 | door (from NDrone2_MoveToGoalPosition while the state is OpenDoor) |
| 0xb | reset |
| 0xc | "creep inactive but was started" |

### 3.5 Search helper structs **[D]**

- **AITarget_tag (0x30)**: +0 target bit, +4 obj, +8 CelPos goal, +0x18 CelPos where the marks were last built,
  +0x28 s16 reachable-node count, +0x2a s16 obstructed-only count, +0x2c frame built. `AINetwork_BuildAITarget`
  skips the rebuild while the goal is within 2 m of +0x18.
- **AIPoint_tag (0x28)** (`drone+0x564`): +4 u8 set, +0x10 f32 distance (DistanceToAIPoint), +0x14 f32 radius,
  +0x18 vec3, +0x24 cel.
- **AINodeSearch_tag (0x30)**, on the stack: +0 CelPos* origin, +4 vec3* heuristic goal (0 means h = 0), +8
  AIPath*, +0xc u8 direction (1: can the mover walk *to* the node, used by A*/emitter/nearest node; 0: can the
  node walk *to* the origin, used when marking targets), +0x10 open-list head (optional), +0x14/+0x18 objs for the
  move test, +0x1c target bit, +0x20 radius² (15 m when 0; 625 = 25 m for A*), +0x24 s16 result node, +0x28
  found count, +0x2c obstructed count.
- **AIEmitter_tag (0x28)**: +0 obj, +4 CelPos, +0x14 AIPath*, +0x18 size (= nodeCount), +0x1c u8* data, +0x20 s16
  start node, +0x24 u8 allocated. Our `src/action/engine/AINetwork.h` has the right size but does not name +0x20
  and +0x24.

---

## 4. How a route is found and followed

### 4.1 Marking the goal: `AINetwork_NodesForPosition` 0x497d0 **[D]**

The target struct's bit `b` (0x10000000 for a drone goal, 0x1000000 for the player) is handled as follows. For every
nav path (`flags & 0xd == 0`):

1. clear `b | b<<1` on all of the path's nodes
2. `AINetwork_NodeSearch` in direction 0 from the goal CelPos: it collects nodes in the goal's cel (and in portal
   neighbour cels within ±2 m of the portal's height range) within the radius, sorts them by 2D distance and
   runs `NDrone2_MoveTestFromNode` on each
3. on the first node that can walk to the goal, set bit `b`; the first node that is only obstructed gets `b<<1`
4. update the path's reach mask (+0x44) with `b` and `b<<1`

The node flags are **global scratch state**: every target setup rewrites the marks for its bit. Any code that
depends on the marks must run straight after the setup. The engine does this: `CalcRoute` runs DoAStarPath right
after the Setup*/Verify* call. A reimplementation must keep that order.

### 4.2 A*: `AINetwork_DoAStarPath(route, obj, obj2, bitmask)` 0x48720 **[D]**

1. Clear open/closed (`& 0x3fffffff`) and g on every node of `route->path`.
2. If `target->builtFrame == 0`, return 4. If the target has no reachable node, or the path's reach mask lacks the
   bit, fall back to `bit<<1` (obstructed) and remember to return 1. If there is nothing at all, return 4.
3. Start set: `NodeSearch` in direction 1 from `route+0x14` with radius 25 m and heuristic goal `route+0x24`. Each
   walkable node goes on the open list with g = its 2D distance and h = its 2D distance to the destination. No start
   node means return 5.
4. Loop: pop the lowest f. If `node.flags & bit`, the goal is reached: backtrack the parent chain into
   `route+0x90`, set the start/end nodes, count = chain length, index = 0, remaining distance = 2D(end, dest) +
   g(end). Return 0, or 1 on the fallback.
5. Otherwise, for each link: skip it when both ends are door nodes (flag 2) and `Door_IsLocked(node+0x28)`.
   `cost = g + (link.used + 1) * link.length`. Relax it as usual (move it out of the closed list if it is cheaper,
   insert it into the open list with `FUN_00048310`). **Link flags 0x100/0x200 (danger) are not read here** (the
   decompile shows no such test).
6. An empty open list means return 6.

The **cost is congestion-weighted**. `AINetwork_UpdateLinksUsedCount` (called by every `FollowRoute`) adds 1 to each
remaining link of every route that is being followed. `AINetwork_ClearLinkFlags(0x310)` resets the counts at the
start of each NPC frame (`Drone_InitComms`). So drones and bots planning in the same frame spread over parallel
links. This makes planning depend on the order of the NPC updates. Shadow tests must snapshot the used counts.

### 4.3 Wrappers (PS2 names) **[D][PS2]**

`NDrone2_MoveToGoalPosition` → `CalcRouteToPosition` (FUN_0004ae30) → the verify function (FUN_00049c50: reuse the
route while the destination stays within 1 m and the route is valid) → `SetupRouteToPosition` (FUN_00049b30:
`ResetRoute`, `SetupGoalPosition`, feet position as the start) → `CalcRoute` (FUN_0004acb0).

`CalcRoute`:

1. Unless flag 0x80 is set, try a straight walk with `FUN_00044150(start, dest)`. If it works, the route becomes
   *direct* (LinkCreep_CalcToRouteEnd, status 2).
2. Otherwise run DoAStarPath. If the status is < 2, run `OptimiseRoute` (FUN_000480b0), which drops the first node
   when the mover can walk straight to the second and the last node when the destination is walkable from the
   second-to-last (door nodes excepted, FUN_00047ff0), then recomputes the length. Then set valid (0x10) and start
   the creep with `LinkCreep_Calc` (FUN_0004abb0).
3. On status 1, `CalcRouteToObject`/`CalcRouteToPosition` call `AlterDestFor_DROUTE_Nearest` (FUN_00047a90).

Routes to an object go through `FUN_00046d90` (combat, ally, ninja and bot attack states) → `CalcRouteToObject`
(FUN_0004adb0) → `VerifyRouteTarget` (FUN_00049bf0) → `SetupRouteToObject` (FUN_00049ab0). When the object is the
player, the route uses the shared player target at 0x1e57f0, which is rebuilt every frame, instead of its own.

### 4.4 Following: `AINetwork_FollowRoute` 0x4b370 **[D]**

Each frame:

1. `GetRouteDistance` → route+0x4c, then `UpdateLinksUsedCount`.
2. When at the end of the list (mode ≠ loop) and within the arrive radius, the status is 3.
3. Otherwise `LinkCreep_Handler`. The creep walks the current link in steps of 0.4 m: `LinkCreep_Dest` gives
   `start + step*(i+d)`.
   - `LinkCreep_Increment` moves the waypoint on when the mover is within 0.4 m of it, or when the next step passes
     `FUN_0004a3c0` (a bounds check).
   - `LinkCreep_Decrement` steps back when the bounds block the way (`AINetwork_BoundsNodeTest` radius 0.2).
   - At the end of a link, `AINetwork_SetupNextNode(±1)` advances the index with the mode's wrap
     (`RouteNodeOffset`: clamp, loop, or ping-pong with flag 0x8) and sets up the next link (`LinkCreep_ForNodes`
     FUN_0004aa90). A node with action flags sets "special pending", and `LinkCreep_AtSpecialNode` (within 0.4 m,
     or within 1 m and no other drone there) calls `NDrone2_ReachedDestNode`.
4. The drone code steers at `route+0x34` (`NDrone2_maybeCalcDistanceAndDirToTarget`, report B).

Patrol and mission paths do **not** use A*. `NDrone2_AssignAIPath(dc, typeMask)` picks the nearest node within
0.3 m in the drone's own cel, from paths whose flags match the mask. `FUN_00044a20` then builds the node order by
walking degree-2 chains from that node into `drone+0x6d8`: mode 2 (loop) for a patrol, mode 1 for a mission path.
It follows with `FUN_00046c20` (patrol) or `FUN_00046d40` (mission).

### 4.5 Emitters: a distance field over the graph **[D]**

`AINetwork_EmitPath(em, maxCost = 255)`:

1. Every node gets g = 255 and every data byte 0xff.
2. NodeSearch 25 m from the emitter position; the found node goes onto the open list with g = its distance.
3. Dijkstra with the same link cost and door rule as A* (h = 0), relaxing only while the cost is ≤ maxCost.
4. Each node's byte = `min(254, |g| * 255/256)`, or 255 when unreached.

So a byte is roughly the route length in metres from that node to the emitter.

Uses:

- `NDrone2_DistanceToEmitter(pos, path, &cachedNearestNode, em)` returns the byte at the mover's nearest node. It
  is valid only when `em->path` is the mover's current path. It is how bots rank pickups and objectives.
- `AINetwork_Emitter_GetNodeAtDistance` walks away from the emitter, only through nodes whose byte is ≥ the start's,
  until it reaches a node whose byte is ≥ d. `DroneMove_FindSafetyFromScaryPosition` uses it to flee.

The emitter runs once per pickup when it is registered, and again when an objective moves
(`MP_recalcObjExtPaths`). The used counts are not zeroed at that moment, so a pickup registered in the middle of a
frame can see congestion costs (inferred, harmless).

### 4.6 Bounds **[D]**

Bounds are outlines that movers must not cross.

- `AINetwork_BoundsTest(from, fromCel, to, toCel, hitOut, strict)` is called by `NDrone2_MoveTestToNode`. It collects
  the cels a ray straddles (`Collide_StraddleCels`), and for each bound link listed on those cels runs
  `AINetwork_LineIntersectsBoundry`: a 2D segment intersection, accepted when the crossing height is within
  [boundY - 2.4, boundY + headroom]. In strict mode it also rejects passing within 0.4 m of a bound node.
- `AINetwork_GetBoundsPushVectorForSphere` is used by `NDrone2_Collision`.
- `AINetwork_FurthestPosition` is used by flee and "closer position" searches.
- The bound link flags are set once at post-load by `AINetwork_InitPassableBoundries`: a bound edge that a nav link
  crosses gets 0x400 ("passable": nav links may cross it) unless it has 0x800 or 0x1000. The door/kick node set-up
  (`FUN_000319b0`/`FUN_00031a50`) sets the others (inferred from the callers).

### 4.7 Dynamic link flags (u16 at link+4) **[D]**

| bit | set by | cleared |
|---|---|---|
| 0x10 | link currently being crept (FUN_0004aa90) | LinkCreep_Calc, and each frame |
| 0x100 | `Drone_BuildDynamicAwarePoints`: explosives and grenades within 12 m (8 m for type 0x37) | each frame by ClearLinkFlags(0x310) |
| 0x200 | occluders (smoke) within 3 m; object type 0x36 | each frame |
| used count (+6) | UpdateLinksUsedCount | each frame |

No reader of 0x100/0x200 was found among the nav functions. They probably feed the drones' perception or decisions
(report B; inferred).

---

## 5. Bots

### 5.1 Identity and lifetime **[D]**

- Bots are MP players 4..9 (`MPGame.players[4..9]`, `MPSettings.Players[4..9]`). `BOT_vars[player-4]` lives at
  0x1d98e0 + i*0x768. There are 6 of them, which matches `mpbots.bot[6]`.
- `MP_Start` → `BOT_init(player, pos, rot, NULL, &mpbots.bot[i], 0)`. With a NULL MPBOT, a scratch MPBOT at 0x1dc550
  is filled from `default_bot_stats[1]` (Drake). BOT_init:
  1. clears BOT_vars
  2. builds a `level_tag` (param0 = the skin hashcode from `MP_skins[skinNum]`; param14 = the accuracy bucket
     3/0/1/2 from stats[0]; param15 = 0x10; two behaviour property blocks)
  3. calls `Drone_Create`
  4. sets `MPGame.players[p].playerObj`, `drone->botVars`, the bot index (+0x752), the player index (+0x750),
     `MPSettings_PerPlayer*` (+0x744), the skin (+0x758), and copies the 14 stat bytes to +0x78
  5. calls `BOTWEAP_InitWeapon`
  6. sets the team: 2 when not a team game, 0 in Assassin
  7. calls `NDrone2_PostLoad_Init` when the match has started
- `NDrone2_DefaultInit` → `BOT_postLoadInit` → `BOT_setDroneStats` copies the stats into the drone (table in 5.3).
- The initial state is `DSTATE_BotInit` → `BotIdle`.
- Death: BotGlobal sees `MPDrone_MaybeIsDyingOrDead` and goes to `BotDeathAnim`, then `BotDead` (report A/B), then
  `BotRespawn`. `BotRespawn` → `BOT_respawn`, which disables the old drone, **creates a new object** through
  `BOT_init` at the old position + 1 m y, and calls `MP_ReSpawn` to move it to a spawn point. In Top Agent a bot
  that reached MaxPoints is not respawned. `MP_RestartScenario` also calls `BOT_respawn` (already reimplemented,
  multiplayer.cpp).
- `BOT_fellOutMap` teleports to a spawn point (with `RespawnSelectionMode` forced to 1) and resets everything.

### 5.2 BOT_vars_t (0x768) **[D]** (Ghidra's type is 0x75f bytes, too short)

| off | meaning |
|---|---|
| +0x000 | goal slot 0 (0x3c): pickup / opponent / guard |
| +0x03c | goal slot 1 (0x3c): game-mode objective |
| +0x078 | stats[14], a copy of the MPBOT (5.3) |
| +0x088 | 10 x 0x10 player table (`FUN_0001a660`, each frame): +0 last time alive and visible, +4 dist², +8 how much they face me (degrees), +0xc flags (1 alive, 2 present, 4 visible: one `NDrone2_CanSeeObject` per frame, round-robin cursor at +0x75b; 8 team-mate) |
| +0x128 | opponent's last position (BOT_opponentTargetting) |
| +0x134 | 83 x 0xc weapon slots: +0 f32 sqrt(range), +4 s16 rounds in clip, +6 u8 held |
| +0x68c | 33 x s16 reserve ammo per ammo type |
| +0x6d0 | 16 x obj* attacker history (+0x760 cursor) |
| +0x710..0x718 | saved combat ranges (drone+0xd0, 0xd4, 0xe0) |
| +0x71c | f32 distraction |
| +0x720 | DSTATE to go back to after a goal |
| +0x724/+0x726 | saved state info |
| +0x728 | flags: 2 recovering, 4 objective goal active |
| +0x72c | regen timer (trait 0x10) |
| +0x734 | last impact frame |
| +0x738 | last route-failure frame |
| +0x73c | recovery end frame |
| +0x740 | weapon 0x45 ammo regen timer (skin 0x1a) |
| +0x744 | MPSettings_PerPlayer* |
| +0x748 | Drone_tag* |
| +0x74c | guard friend obj |
| +0x750 / +0x752 | s16 player index / bot index |
| +0x754 | cached nearest nav node (-1 each frame) |
| +0x756 | state that `BOT_validateStateChange` redirects to |
| +0x758 | skin |
| +0x759 | active goal slot (0xff none) |
| +0x75a | current state class |
| +0x75c | current weapon |
| +0x75d | armour |
| +0x75e | weapon to change to |
| +0x75f | preferred opponent player index (0xff none) |
| +0x761 | route-failure count |
| +0x762 | last pickup index (not picked twice in a row) |
| +0x763 | "at objective" flag |
| +0x764 | being guarded |
| +0x765 | inside the protection/demolition object |

**Goal slot (0x3c)** **[D]**:

| off | meaning |
|---|---|
| +0x00 | CelPos |
| +0x10 | f32 time budget. Slot 1: 500..1100 random, 1500 for subtypes 2/5, 250..850 (subtype 8) or 250..750 (subtype 9). +1500 for TeamPlayer in team games, 0 for Berserker |
| +0x14 | start time (MPGame.TimeIncPaused) |
| +0x18 | 5 x frame rate |
| +0x1c / +0x20 / +0x24 / +0x28 | weights: health, ammo, weapon, objective |
| +0x2c | target: MP_PICKUP*, objective obj or opponent obj |
| +0x30 | arrive radius / next state |
| +0x34 | u8 flags (1 = complete) |
| +0x35 | u8 kind: 1 pickup, 2 objective, 3 object/opponent |
| +0x36 | u8 pick flags: 2 single pass, 4, 8 avoid the opponent's path, 0x20 ignore visit times |
| +0x37 | u8 max emitter distance (0 or 0xff → 0xfe) |
| +0x38 | u8 last route status |
| +0x3a | u8 subtype: 1 enemy flag, 2 own base, 3 GoldenEye, 4 blueprint, 5 espionage base, 6 uplink, 7 hill, 8 demolition/protection, 9 opponent |

### 5.3 Stats, the setup page, and difficulty **[D]**

MPBOT / BOT_stats_t (14 bytes; `default_bot_stats` 0x163628, 29 entries, indexed by the mp_characters id; full dump
in the appendix):

| byte | meaning | values | drone field (BOT_setDroneStats) | effect |
|---|---|---|---|---|
| 0 | accuracy (lower is better) | 1 very good, 3 good, 5 average, 8 poor | +0x98 (+0xa4 as float) | level_tag param14 bucket; BOT_opponentTargetting reaction window `(acc/3+1)*FRAME_RATE/2`; BOT_getMovePossibility |
| 2 | aggression | 2 normal, 3 high, 4 very high | +0x99 | BOT_getAggressionMul 0.3/0.5/0.7/0.85/1.0 |
| 4 | u16 health | 50..300 | +0x90 health | |
| 6 | speed | 0 slow, 1 normal, 2 fast | +0x9a | BOT_getMovementSpeedMul 0.7/1.0/1.3 (the route arrive radius in gotoGoal) |
| 7 | reaction | 50..200 | +0x9c | DroneFunc_ReactionTime **[A scan]** |
| 8 | recover | 50..200 | +0x9d | BOTSTATE_startRecovery: no sight for `recover*2/3` frames after being hit; DroneFunc_RecoverTime |
| 9 | isBad (Phoenix side) | 0/1 | +0x9e | Menu_IsBotGood |
| 10 | preferred weapon class | 0 none, 1..5 | BOT_vars+0x82 | BOTSTATE_isPreferredWeapon: class 1 → weapon_data.maybeFlags == 1, 2 → flags 2, 3 → weapon ids 3/7/0x14/0x24/0x25, 4 → flags 3, 5 → flags 4 (flags 4 = explosives, the class `BOTWEAP_tooCloseForWeapon` checks). Fists (1) and weapon 6 always count as preferred |
| 11 | personality | 0 None, 1 Collector, 2 Guardian, 3 Team Player, 4 Judge, 5 Berserker, 6 Greedy, 7 Vengeful, 8 Assassin | BOT_vars+0x83 | 5.4 |
| 12 | trait flags | Scaramanga 1, Wai Lin/Xenia 2, Jaws/Oddjob 4, Baron Samedi 0x18 | BOT_vars+0x84 | 0x10: regenerates 5 hp per second (BotGlobal, timer +0x72c); 8: the "attacking" flag is set as soon as the opponent is visible (BotGlobal); 4: prefers fists at close range (BotGlobal, BotAttackBackoff), and in `NDrone2_DoHitEffects`/`Player_DealWithObjHit` bits 2 and 4 scale the damage the bot deals for particular attack types (the attacker's +0x84 is read by an [A] scan and not decoded further); 1: read in FUN_000378f0 (the aim-offset code near drone+0x1b4, meaning not decoded) |
| 13 | editable | 1 for skins 0..14 | | P_MPBOTSETUP locks skins ≥ 15: "This Bot's statistics and personality are fixed" |
| 1, 3, 5 | 0 in every entry | | | |

`P_MPBOTSETUP_Handler` (0x8ade0, not ours) reads its 8 scrolls back into `mpbots.bot[mp_editing_bot]` on message
0x4b and fills them on 0x4c: Playing, Accuracy, Aggression, Health, Speed, Personality, and the two 50..200%
scrolls REACTION2 → byte 7 and REACTION1 → byte 8. Good characters can pick None/Judge/Collector/Guardian/Team
Player; bad ones None/Berserker/Greedy/Vengeful/Assassin. The rest of the bot UI is already ours in ui_mp.cpp
(P_MPBOTS, C_SBBOTS, P_MPBOTCHOOSE, C_SBMPBTCHOOSE, Menu_PrepareBots use).

### 5.4 Personalities **[D]** (`BOTSTATE_getPreferredTraitOpponentObjIndex`, gotoGoal, FUN_0001a660)

| # | personality | effect |
|---|---|---|
| 1 | Collector | state BotCollector: `setGoalPickPrefs(0, 1,1,1,0)` + pickGoal: pickups only |
| 2 | Guardian | tracks team-mates in the player table. The nearest live team-mate that is not a guardian becomes +0x74c. Within 4.5 m, goes to state 247 BotGuardFriendIdle (and 248 Follow) |
| 3 | Team Player | +1500 on the objective time budget in team games |
| 4 | Judge | opponent = the living enemy with the highest score (players[i]+0x18) |
| 5 | Berserker | keeps the current opponent, otherwise the nearest enemy in the table; objective time budget 0 |
| 6 | Greedy | no opponent preference (by the pickup weights, inferred) |
| 7 | Vengeful | opponent = `MPGame.players[me].maybeIdxOfMyAssassin` (the last killer) |
| 8 | Assassin | opponent = the enemy with the lowest health |
| - | Assassin game mode | always targets `MP_getAssassinTarget()` when it is the assassin |

The personality states 198..206 exist in the table, but 199..206 all point to one shared stub at 0x61470 (Ghidra
calls it `BotStuck`: on enter it goes to BotIdle). The same stub is used for 211 BotAttackNoOpponent and 233
BotStuck. BotSeenOpponent and BotSeenDroneShot share 0x63760 (enter → BotAttack). Personality therefore lives in the
goal and opponent code, not in states.

### 5.5 State machine **[D]**

The bot rows of the DSTATE table (0x177ca0 + 4*state) are in the appendix. `BOTSTATE_getStateType` (u8 table
0x163990) maps each state to a class:

| class | states |
|---|---|
| 1 | Init, Respawn |
| 2 | Global |
| 3 | personalities 198..206 |
| 4 | attack 207..223 |
| 5 | Stuck, AlertToPosition, GotoGoalPosition |
| 6 | Seen*/HeardNoise |
| 7 | impacts |
| 8 | DoorOpen |
| 9 | Idle |
| 10 | death 242..244 |
| 12 | cover 224..232 |
| 13 | guard friend |

`DSTATE_BotGlobal` (0x603e0) is the brain. It handles these messages (message codes belong to report A):

| msg | handling |
|---|---|
| 3 (update, each frame) | leave on death; apply +0x720; clear the recovery flag when it expires; regen (trait 0x10); set the attacking flag (trait 8 + visible). For classes 3..9, 12 and 13: invalidate the nearest node, run `BOTSTATE_processGoals`, then the class-4 combat checks (reload when the clip is empty; change weapon; unarmed under 1.5 m when facing; backoff under combatRange1; random strafe/step/roll under 3 m (`FUN_0001c8c0`); explosive weapon too close → change; `BOTWEAP_hasLoadedExplosiveForRange`). For other classes with a recent sighting (< 5 s) → attack state via `FUN_00064910(dc, 0xec)` |
| 6 / 8 / 9 | punch, bullet or explosive impact → `NDrone2_*Impact` + `BOTSTATE_startRecovery` |
| 0x11..0x16, 0x1e | drone alerts (classes 3 and 5..9 only) → `FUN_00038ff0` |
| 0x18 | stun grenade |
| 0x2e | state entered: set the alert status, disown cover |
| 0x3a | objective changed → pick the slot-1 goal |
| 0x3b / 0x3c / 0x3d | goal complete / uninit / cancel goals on an object |
| 0x40 / 0x41 | the opponent is the objective carrier |
| 0x42 | change to weapon n |
| 0x43 | a player died: drop them as opponent/guard/preferred |
| 0x44 / 0x45 | a weapon was picked up: combatWeaponChangeChoice, ranges, missile handling |

`BOT_validateStateChange` (0x1a2d0) runs inside `Drone_SM_SetState` for every drone whose type byte (+0xa9) is 30.
It refuses, or redirects through +0x756, several requests:

- impacts 239..241 are always refused
- heard noise is refused unless there is an opponent
- attack moves (0xd0, 0xd6) are refused while the MPGame player flag 0x10 is set, and redirected to a random combat
  move
- class-4 requests are gated by distraction versus the active goal's budget (`BOTSTATE_increaseDistraction`, 1.7
  scaled by distance)
- dead states only accept dead states or 0xc4

Hand-checked; the exact case list is in the decompile.

`BotIdle` (249), class 9, on update:

1. preferred opponent from the personality → slot-0 goal to chase them
2. otherwise, when not at the protection/demolition object, `setGoalPickPrefs(1, 0,0,0,1)` + `pickGoal(1)`
   (objective)
3. on success, `BotGotoGoalPosition`

`BotGotoGoalPosition` (235) calls `NDrone2_MoveToGoalPosition` each frame. It goes to BotDoorOpen on route status 10.
When the route fails it marks the pickup visited and goes back to Idle.

### 5.6 Goal selection: `BOTSTATE_pickGoal(dc, slot)` 0x1cb20 **[D]**

**Slot 1 (objective)**, when its weight (+0x28) ≠ 0. By game mode:

| mode | objective |
|---|---|
| CTF | enemy flag, or own base when carrying |
| Blueprint / espionage | blueprint, or base when carrying |
| Uplink | a free uplink that no team-mate bot is already heading for (`isObjAlreadyAnotherTeamObjective`) |
| GoldenEye | keys |
| KOTH / team KOTH | the hill |
| Demolition / Protection | the object |

Candidates are scored `((maxDist+1) - emitterDist) * objectiveWeight`, where emitterDist is the byte at the bot's
nearest node from the objective's emitter. With no objective, it falls back to slot 0.

**Slot 0.** Flag 0 and a preferred opponent → kind 3 (chase). Otherwise **pickups**. For each registered
`MPpickups[i]` (up to NumActivePickups; not the last one taken, +0x762):

1. dist = emitter byte from the bot's nearest node (cached per call); skip when dist > maxDist (+0x37)
2. skip pickups visited in the last 45 s (`maybeBotPickupVisitTimes[bot]`, set by BOTSTATE_setPickupVisitTime,
   aged by MP_Pickup_Process, cleared by MP_ResetBotPickupTimes), unless flag 0x20. On pass 3 visited pickups are
   allowed, with dist multiplied by 50 x seconds since the visit
3. score = `((maxDist+1) - dist) * weight`, where the weight depends on the pickup's kind (pickup extra +0x1a):
   - 0 weapon: weapon weight; on pass 0 only a preferred weapon the bot does not hold, or an explosive when
     defending the object (score x2)
   - 1 ammo: ammo weight, only for weapons held on pass 0
   - 3 health/armour: health weight
4. take the best; ties go to the smaller distance

There are up to 4 passes (1 with flag 2): 0 strict, 1 any weapon or ammo, 2 all weights 1, 3 allow visited.
`setGoalPickPrefs` with all three weights 0 derives them: health weight `25*(1 - health/maxHealth)`, and ammo and
weapon weights from how empty the clip is (each clamped to 25).

After choosing: `BOTSTATE_initGoal` + `BOTSTATE_gotoGoal`, which calls `AINetwork_SetupGoalPosition(ToObj)` into
`drone->someGoalTarget/someGoalPoint`, with arrive radius = `BOT_getMovementSpeedMul`. With flag 8 it also plans the
route at once, and if the route passes within 4 m of the opponent (`isPathWithinObjectRange`, first 4 segments) the
pickup is marked dist 255 and it picks again.

`BOTSTATE_processGoals` (each frame, from BotGlobal):

- a goal whose time budget has run out is dropped, and the saved state is restored
- an objective is dropped once it is taken or captured (MPGame team flags)
- an opponent goal is dropped when they die; a Guardian switches to guard within 4.5 m
- a preferred opponent is re-picked when the personality now prefers someone else
- a route failure (`validateRoute`) → mark the pickup visited and change state
- arrival (flag 1 or route status 3) → Blueprint delivery check (`MP_BluePrintReachedBase`), pickup visited, state
  change

### 5.7 Weapons and aim **[D]**

- **Inventory**: +0x134 weapon slots and +0x68c ammo (5.2). `BOTWEAP_InitWeapon` on (re)spawn clears the inventory
  (`FUN_0001e540`), then gives fists (weapon 1, 999 rounds) and the weapon set's first weapon
  (`PickupMatrix[weaponSet][0]`) with 2 clips. It also gives weapon 0x3b to the attacking side in Protection and
  Demolition, and weapon 0x45 to skin 0x1a (Baron Samedi); BotGlobal regenerates that one every 10 s.
- **Choosing**: `BOTSTATE_combatWeaponChangeChoice`, `BOTSTATE_changeWeapon`, `FUN_0001eb70` (a fixed 60-entry
  weapon preference order; score 100 - rank), `BOTSTATE_isPreferredWeapon` (stat 10), `FUN_0001f450` (best loaded
  explosive whose blast radius is under the distance), `BOTWEAP_tooCloseForWeapon`. Unarmed under 1.5 m when facing
  the opponent (always with trait 4; otherwise 1 in 200, or when the weapon is a gadget, `FUN_0001ee60`).
  Changing weapon goes through state 222 BotAttackChangeWeapon with the target weapon in +0x75e.
- **Aim**: `BOT_opponentTargetting` (called from the attack states, report B):
  - the aim point is `drone+0x19c = opponent pos + drone+0x1b4`
  - while `now - drone+0x1c4` is inside the reaction window `(accuracy/3+1) * FRAME_RATE/2 * (1 + 0.05*(dist-2))`,
    the aim wobbles: `drone+0x1a8/0x1ac = cos/sin(frame*0.05) * 1.2/1.7`
  - the "opponent moved" flag (+0x191) with 40.0 in +0x198 is set when the target moves more than 0.025 per frame
  - the actual shot spread uses drone+0x98 and +0xa4 in the shared drone firing code (report B)
- Damage taken: `BOT_handlePain` applies the location multipliers (Plr_DMod_*), triple damage and armour (+0x75d).

---

## 6. What already exists in the repo

| item | state |
|---|---|
| `src/action/game/drone/BOT.h/.cpp` | BOT_stats_t (0xe bytes, only baseAggression/health/isBad named, and **health is really a u16 at +4**); AUTOGEN declarations of BOT_getDefaultStats and BOT_respawn (the .cpp declares BOT_respawn as void, the .h as bool) |
| `src/action/engine/AINetwork.h/.cpp` | CelPos_tag, a placeholder AIPath_tag, AIEmitter_tag (0x28, +0x20/+0x24 unnamed), AUTOGEN AINetwork_FreeEmitter |
| `game/drone/NDrone2_Bot.cpp` | empty (untracked) |
| multiplayer.cpp/.h | MP_PICKUP (0x58, emitter at +0x14, visit times at +0x3c), MPBOT (18 bytes), MPBOTS at 0x245280; MP_Init/MP_RestartScenario/MP_Update are ours and call BOT_respawn / MP_Pickup_Process |
| ui_mp.cpp, Menu.cpp | bot pages are ours except P_MPBOTSETUP_Handler; Menu_IsBotGood (inlined on Xbox) |
| functions in scope that are ours | **none** (MP_setLoadingSkins shares the address range but is not a bot function) |

---

## 7. Tables for tools, not hand-writing

| table | address | size | generate with |
|---|---|---|---|
| default_bot_stats | 0x163628 | 29 x 14 | `stats.py` pattern → a C array with character comments (as tools/uihandler.py does) |
| bot state class | 0x163990 | 55 u8 | `stype.py` → `static const uint8_t bot_state_class[55]` |
| DSTATE handler table (bot rows) | 0x177ca0+195*4 | 55 ptrs | shared with report A's full table: one generator for all 250 entries. Note the folded duplicates (0x61470 x10, 0x63760 x2) |
| BotGlobal per-class jump tables | 0x6137c (4 ptrs) + byte map 0x6138c; 0x61398 + 0x613a0 | small | the source is a switch; no table needed |
| FUN_0001eb70 weapon order | built on the stack | 60 u16 | copy from the decompile |
| move-test constants per level | code in Drone_PostLoad_Init | 5 floats | hand-write |

---

## 8. Reimplementation order

Rules that apply throughout:

1. Put new code in its own compilation units (`engine/AINetwork*.cpp`, `game/drone/BOT*.cpp`) (memory: keep game
   logic separate).
2. **Watch the register calling conventions** **[A]**:
   - AIPath_Prepare takes the entry in ESI
   - AIBounds_Prepare takes EDI
   - AIBounds_Link2Cel takes EAX
   - FUN_00047a90 and FUN_000480b0 take the route in ESI (and flag AL)
   - FUN_00047ff0 takes the route in ECX and the node in AX
   - FUN_00048310 takes EAX (list head) and EDX (node)
   - FUN_00048360 takes ECX and EDX
   - FUN_00049b30 takes ESI (route) and EDI (CelPos)
   - FUN_0004a3c0 takes EBX, ESI and EDI
   - FUN_0004a420 takes EDI (path)
   - FUN_0004acb0 takes ECX (route), EAX and EBX (objs)
   - LinkCreep_Increment takes EAX (out byte*)
   - AINetwork_GetRouteDistance and BOT_getMovementSpeedMul return in ST0
   
   Replace such a function together with all of its callers, or give it a naked shim.

### Phase N1: navigation data (leaves; shadow-testable at load)

`AIPath_Init`, `AIPath_Parse`, `AIPath_Prepare`, `AIBounds_Link2Cel`, `AIBounds_Prepare`, `AIPath_BindNodes`. Define
the AIPath/AIBound/node/link structs from section 3 with static_asserts.

Shadow test: run the original parse and ours over a copy of the block, and compare the tables, the patched file
bytes and the cel lists after BindNodes. Offline, the level exporter's `decode_ai_paths` already gives expected
counts for all 28 maps.

### Phase N2: pure route and graph helpers (leaves)

- `RouteIsValid`/`Invalidate`/`Validate`, `RouteNodeOffset` (47a10), link lookup (47b40), `ResetRoute` (482c0),
  `FUN_00049cd0`, `LinkCreep_Dest`, `GetRouteDistance`, `GetRouteDistance3D`, `GetRouteDistanceBetweenNodes`
  (47bb0), `AllocEmitter`, `FreeEmitter`.
- Link flags: `ClearLinkFlags`, `UpdateLinksUsedCount`, `SetLinksFlagInCircle` (+4a420).
- Bounds geometry: `LineIntersectsBoundry`, `BoundsNodeTest`, `BoundsTest`, `GetBoundsPushVectorForSphere`
  (+48b70), `ModIntersectedLinkFlags_Bounds`, `InitPassableBoundries`, `FurthestPosition`.

Shadow-test each one in game, as UpgradeShadow does: call both on the same inputs and compare outputs and memory.
The bound tests are deterministic given the cel lists.

### Phase N3: searches (shadow-testable with a snapshot)

- `NodeSearchCel`/`NodeSearch` (they depend on `NDrone2_MoveTestToNode`/`FromNode`, report A/B; call the originals)
- `NodesForPosition`, `BuildAITarget`, `SetupGoalPosition(ToObj)`
- the open-list helpers
- **`DoAStarPath`**, `EmitPath`, `Emitter_GetNodeAtDistance`

Shadow method: snapshot the node flags, runtime nodes, link used counts and the route/target structs; run the
original; restore; run ours; diff the route node list, status and remaining distance, and all the scratch arrays.
Every search writes global scratch (node flags, g/h/f), so snapshot and restore are required.

### Phase N4: route plan and follow

- `OptimiseRoute`, `AlterDestFor_DROUTE_Nearest`
- `SetupRouteToObject`/`ToPosition`, `VerifyRouteTarget`, `CalcRoute`, `CalcRouteToObject`/`ToPosition`
- `LinkCreep_ForNodes`/`Calc`/`CalcToRouteEnd`, `SetupNextNode`, `LinkCreep_Increment`/`Decrement`/`Handler`,
  `LinkCreep_AtSpecialNode`, `FollowRoute`
- then the NDrone2 glue (`MoveToGoalPosition`, 46a80, 46c20, 46d40, 46d90, AssignAIPath, 44a20), coordinated with
  report A

These depend on movement state over frames, so they need **replays**: MP bots on FortKnox or Atlantis, and a
single-player patrol and mission level (HendersonA "Mayhew Route", CastleCourtyard patrols) through
drive_game.ps1, comparing per-frame route structs. A per-call shadow of `CalcRoute` is still possible with snapshots.

### Phase B1: bot leaves

`BOT_getDefaultStats` (its AUTOGEN declaration is already used by our menu code), `BOT_SetHealth`,
`getMovementSpeedMul`, `getAggressionMul`, `getMovePossibility`, `BOTSTATE_getStateType` (generated table),
`setNearestNavNode`, `uninitGoal`, `initGoal`, `setGoalComplete`, `getActiveGoal`, `setStateChange`,
`setPickupVisitTime`, `validateRoute`, `startRecovery`, `increaseDistraction`, `isDistracted`, `isPreferredWeapon`,
`isObjAlreadyAnotherTeamObjective`, and the BOTWEAP leaves (hasWeapon, getWeaponClipSize/AmmoAmount, isValidWeapon,
AmmoInGun, WeaponHasAmmo, and the weapon class switches 1edb0/1ee60/1eeb0, 1eb70).

Fix BOT_stats_t / MPBOT naming first (5.3). All of these are shadow-testable one call at a time.

### Phase B2: bot state and setup

`BOT_setDroneStats`, `BOT_postLoadInit`, `BOTWEAP_EquipWeapon`/`EquipAmmo`/`InitWeapon`/`CheckWeaponsLoaded`/
`changeWeapon`/`decrRounds`, `FUN_0001e540`/`1e5b0`, `BOT_init`, `BOT_respawn`, `BOT_fellOutMap`, `BOT_handlePain`,
`BOT_soundEffect`, `P_MPBOTSETUP_Handler` (UI: it can use the MenuProbe A/B harness), the player table
`FUN_0001a660`, `BOT_handleOpponentHistory`, `BOT_opponentTargetting`.

Shadow per call with BOT_vars snapshots; BOT_init/respawn by comparing the BOT_vars and Drone_tag they produce.

### Phase B3: brain

`BOTSTATE_setGoalPickPrefs`, `gotoGoal`, `isPathWithinObjectRange`, `getPreferredTraitOpponentObjIndex`, **`pickGoal`**
(after N3, because of emitters and NearestNode), `processGoals`, `cancelGoalToObj` (1d6a0), `combatWeaponChangeChoice`,
`changeWeapon`, `pickupWeaponChangeChoice`, `BOT_validateStateChange`, then the DSTATEs: Init, Respawn, Collector,
the shared stub, Idle, GotoGoalPosition, SeenDroneShot, HeardNoise, and finally **BotGlobal**. The attack and cover
states follow report B's plan.

`pickGoal` is shadow-testable (it is deterministic apart from Rand_Rand in uplink/GoldenEye; seed or stub Rand). The
DSTATEs need replays: an MP bot match with a fixed seed. Rand_Rand is used by gotoGoal budgets and the combat moves,
so seed-locked replays are needed.

Open questions (not resolved here):

- the reader of link flags 0x100/0x200
- trait bits 1, 2 and 4 in full (readers are listed in 5.3)
- the exact list of `BOT_validateStateChange` cases
- the meaning of `weapon_data.maybeFlags` classes for stat 10
- the msg-code names (report A)
- path flag 0x200 and the high bits (no reader found)
