# The action engine's levels

How 007: Nightfire's action engine (`default.xbe`) builds a level from its bundle, and how to export one to Blender.
Mapped from the Xbox binary (the named PS2 build as a guide); every offset is the Xbox one.

| Document | Covers |
|---|---|
| [spec-world.md](spec-world.md) | how a level is assembled, placement records, the static world and its transforms, rooms and portals, collision, paths, AI paths, lights, sounds, particles, fog |
| [objects-actors.md](objects-actors.md) | drones (characters), cover and AI markers, spawners, player and multiplayer starts, vehicles, turrets, the space-station weapons |
| [objects-logic.md](objects-logic.md) | doors, pickups, breakables, triggers, switch channels, scripted objects, sounds, music, cameras |
| [objects-dressing.md](objects-dressing.md) | sky, lights, weather, particle emitters, animated props, ladders, wires, the placement types the game ignores |

Machine-readable tables of every placement type (keys, meanings, value ranges seen, visuals, transforms):
`tools/level/types_actors.json`, `types_logic.json`, `types_dressing.json`.

## In short

A level bundle (`07xxxxxx.bin`) holds graphics packs (`01xxxxxx`): object packs (bundle entry types 0, 2, 11)
and one **map file** (entry type 1). Each pack's block `0x1a` is a list of **placements**, 0x4C bytes each plus
`{key, value}` parameters. When the level starts, the map's placements are turned into:

- **static cels** (type & 0x8000): rooms (0xc047, 0xc023 underwater; meshes already in world space) and objects
  (0x8019...), drawn with `T(pos) · Rz · Ry · Rx(rot)`. An object is drawn only when linked to a room, and rooms
  are drawn through their portals;
- **dynamic objects** ((type & 0xa000) == 0): `parsemap_create_dynamic_objects` builds a 0xB0-byte `level_tag`
  - a 0x2C-byte header (path frames, rate, path pointer) then each parameter as the dword at `0x2C + 4 × key` - and
  calls the type's Create function. Only the map file's placements are ever created; object packs' are parsed with
  creation off.

The placement quaternion (+0x24) is the *inverse* of its Euler rotation: every exporter must use `rot`. Placement
types 255, 246, 247 and 221, and 0x2000 placements without 0x8000, are ignored by the game.

Objects talk through **switch channels**, a 256-byte table: triggers, switches and deaths write channels; doors,
drones, spawners and scripts wait on them. Every channel field in the type tables says whether it is read or
written (`channelRole`).

## Exporting to Blender

```
cd tools/level
python export_level.py --list                         # the level bundles and what they hold
python export_level.py 07000001 -o 07000001.glb       # one level (add --collision, --portals)
blender --python blender_import_level.py -- 07000001.glb
```

The scene (one root node per level, then a group per kind):

| Group | What |
|---|---|
| `rooms`, `static` | every drawable static cel, sharing one mesh per entity; extras carry the room's ambient colour, switch channel and music event, and each object's room |
| `objects/<Type>` | dynamic objects with their placement glist as a mesh where the game draws it (doors in their closed pose along their path, breakables, pickups, ladders, rotors at their start angle, wires, coronas...) |
| `characters/<Type>` | drones as their skin's meshes in the bind pose (feet land on the floor), with the held weapon at its attachment point; the swaying trees (EnvTree) |
| `volumes/<Type>` | trigger, hint, hurt, rain and AI volumes as wire boxes: the entity's bounding box (the game tests the entity's collision mesh inside it), times the placement's scale for AI volumes |
| `markers/<Type>` | player and multiplayer starts |
| `lights` | the map's radiators (block 0x26) and Light placements as KHR_lights_punctual point lights, with their range |
| `sky` | sky objects (a later one in the same slot replaces the earlier; camera-centred ones at the origin) |
| `paths` | door and camera paths (0x19) and AI routes and bounds (0x05) as lines |
| `collision`, `portals` | with `--collision` / `--portals` |

Every object node carries its placement as flat custom properties (Blender: Object Properties > Custom
Properties): `placementType`, `placementIndex`, `glist`, then **every key as its own field, by name** (channels
and counts as integers, hashcodes as hex strings, floats as floats) and the path it uses. Objects are named after
their type, placement index and the switch channels they write (`→`) and read (`←`), e.g.
`Drone 161 → ch 96 ← ch 11` (waits for channel 11, sets 96 when it dies). `blender_import_level.py` adds a
tooltip to every field from the type tables (with a 0-255 range and "reads"/"writes" on channel fields) and a
`Switch channels` collection holding, per channel, every object that uses it - isolate one to see what triggers
what. A channel with readers but no writer is set by a script or by the game's code.

Objects whose glist the game never draws (entity flag 0x10 or 0x8000 - editor markers, like the red boxes of
Destroy volumes) are exported but marked `hidden` (and with `KHR_node_visibility`, which Blender 5.1's importer does
not apply); `blender_import_level.py` hides them.

## How it was checked

- `parse_level.py validate` decodes all 21,725 placements of the 32 bundles; every glist resolves; collision (2.0
  million triangles), portals (843) and path references check out (spec-world.md).
- All 29 real levels export (the other three bundles are front-end data and place nothing); a level takes one or
  two seconds and 2-15 MB.
- Blender renders (`blender_render_level.py <glb> <prefix>`: a top-down view and one from the player start, with
  backface culling on) of the Japanese house (07000001), the castle (07000007), the space station (0700001b) and a
  multiplayer snow map (07000024) show the rooms tiling exactly, the furniture, pickups and characters where they
  belong, and every face the right way round.

## What is not in the export yet

- **Object scripts.** Gun turrets, helicopters, the space-station missiles and laser, multiplayer objects, mines,
  switches, fuse boxes and scripted players are drawn by type-7 object scripts, whose format is not decoded; they
  come in as empties at their placement, with their keys.
- **Characters are in their bind pose.** A drone's starting animation (key 1, a script) and its idle state are not
  applied; its skin can be exported with its animations by `tools/anim/export_gltf.py <skin> --bundle <level>`.
- **Multiplayer weapon pickups** (kinds 7-11) show a weapon chosen by the game's settings at run time.
- **Particles and weather** (emitters, ripples, leaves, rain, snow) are empties with their settings.
- **Lights' brightness** is a nominal value: the game only lights flagged objects with them (the world is prelit
  through vertex colours, which the export keeps); the range is the game's.
- Room linking (which room an object belongs to) is approximated; about 60 borderline objects in the game have no
  room and the game would not draw them. They are exported regardless.

## Ghidra

`tools/level/make_ghidra_json.py` writes `driving-symbol-matching/results/structs/action-level.json` for
`ghidra/NightfireStructs.py`: `TARGET_PLACEMENT`, `PlacementParam`, the generic `level_tag` and a
`level_tag_<Type>` for every placement type with its keys named and described. `tools/level/annotate_ghidra.py`
wrote a plate comment on each of the 71 Create functions (the placement type, how it is called, its keys and what
it looks like in the world).
