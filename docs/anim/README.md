# The action engine's animation system

How 007: Nightfire's action engine (`default.xbe`) animates characters, from the files on the disc to the
matrices the vertex shader skins with. Mapped from the Xbox binary with the named PS2 build (`ACTION.ELF`) as a
guide to intent; every offset here is the Xbox one. The detail is in five specs:

| Spec | Covers |
|---|---|
| [spec-load.md](spec-load.md) | level bundles, skeletons, skins, datums, the loader functions |
| [spec-seq.md](spec-seq.md) | sequence files, keyframe decoding, interpolation, root motion, distance tables |
| [spec-pose.md](spec-pose.md) | the animated object, bone hierarchy, matrix palette, head tracking and aiming, GPU skinning |
| [spec-facial.md](spec-facial.md) | morph-target faces: weights, layers, lip sync, the morph vertex streams |
| [spec-script.md](spec-script.md) | script files, playback, blending, events, locomotion sets |
| [spec-mesh.md](spec-mesh.md) | the graphics packs (01 files): blocks, entities, meshes, vertices, strips, skinning weights, axes |
| [spec-texture.md](spec-texture.md) | textures (formats, swizzling, lookup) and the primitives' material flags |

Exporting a character with its animations to Blender is described in [gltf-export.md](gltf-export.md).

The raw decompiles and disassembly the specs cite as evidence were working files and are not kept here; the
addresses in the specs lead straight back to them in Ghidra.

## The data

Every file is named `%08x.bin` after a hashcode whose top byte is its type. The disc's `filesys.dNN` archives hold
32 level bundles (`07xxxxxx.bin`); each bundle holds everything a level loads, animation included:

- **Skeletons** (`SKELnnnn`, loader type 6), 45 of them, identical in every level: a bone count, a mask of which
  bones have animated translation, and each bone's default local offset. No hierarchy.
- **Skins** (`05xxxxxx`): per bone, one byte giving its parent (and whether this skin uses it); inverse bind poses
  (always an identity rotation and minus the bone's bind position); attachment points ("datums": weapons, gadgets,
  effects); the hashcodes of the meshes (`02xxxxxx`, defined in the level's `01xxxxxx` graphics files) and of rigid
  parts that follow a single bone; and a morph-target count (22 on faces, else 0).
- **Sequences** (`04xxxxxx`), 1,279 distinct: one track per channel. A bone has six (tx ty tz qx qy qz) if its
  skeleton bit says its translation is animated, else three. A track is a run of keys, each a polynomial of degree
  0-5 in absolute time; quaternions are stored as x y z with w derived. A sequence with header flag 8 is facial:
  one track per morph weight.
- **Scripts** (`06xxxxxx`): a frame count and loop flag, then commands - play a sequence over a frame range, play a
  sound at a frame, fire an event (footstep, callback, stop sound, effect, fire weapon) at a frame.

## At run time

An object with animation has an `AnimState` (0xF0 bytes, at `obj_tag+0xB8`) embedding its `AnimObj`
(`sAnimObject_tag`, 0x98 bytes, at +0x58). Each frame:

1. `AnimObjectUpdate` ticks the object's scripts (`sAnimScript_tag`, a linked list): time advances by
   `speed * 60 / game fps` at 60 script frames a second; commands whose frame has come fire once; locomotion sets
   (`sAnimSet_tag`) keep walk and run scripts in step and weight them by speed.
2. `AnimFrameResolve` samples each script's sequence (`sAnimSeq_tag`): `AnimFrameCopy` decodes the two whole
   frames around the current time and `AnimFrameBlend` lerps translations and slerps rotations between them. Scripts
   are then blended oldest first, a cross-fade taking 8 frames. Bone 0's translation is root motion: its change
   moves the object and is taken out of the pose. Facial sequences go through the same path into the morph weights,
   in three layers (mood, blinking, speech).
3. `psiBuildMatrixPalette` walks the hierarchy (parents come before children) to world matrices, turning the head
   (bone 5) and the spine (bone 2) towards their targets on the way, then multiplies in the inverse bind poses to get
   the palette. `AnimObjectDraw` uploads it: up to 53 bones per draw batch, two bones and one weight per vertex, and
   the eight strongest morph targets bound as extra vertex streams.

Conventions: quaternions are **x, y, z, w** (the `wxyz` comment in `math/math.h` was wrong); `_MATRIX` is 15 floats,
three rotation rows at m[0..2], m[4..6], m[8..10] and the translation at m[12..14]; vectors are rows (p * R + t).

## Reimplemented and verified

In `src/action/engine/Anim.cpp`, each checked against the original in play:

| Function | What it does |
|---|---|
| `psiBuildMatrixPalette` | pose -> world bone matrices -> skinning palette, including head tracking and aiming |
| `AnimGetBoneWorldTrans` | a bone's or datum's world position and matrix |
| `AnimFrameCopy` | decodes one frame of a sequence (bones or morph weights), unpack cache included |
| `AnimSkeletonProcess` | registers a skeleton (earlier work, now on the real layout) |

**Shadow testing** lives apart from the reimplementations, in `src/action/devtools/AnimShadow.cpp`. With
`AnimShadowTest=on` in `settings.ini`, it points each tested function's entry at a wrapper that runs the original
beside ours on the same state (`src/common/xbeOriginal.h` puts the original's entry bytes back for the call), keeps
ours, and prints a summary every power of two calls, with which paths the calls took. On the space station level: 65,536 palette
builds (1,849 with head tracking, 239 with aiming), 16,384 frame decodes (bone, facial and cached) and 16,384
bone/datum lookups, with no difference at all - `AnimFrameCopy` is compared bit for bit. The same mode writes the
first frames it decodes to `anim_capture.bin`; `tools/anim/compare_capture.py` decodes them again offline.

## Tools (`tools/anim/`)

Paths come from `nfpaths.py` (the disc, `build/anim/lvl` for extracted data, the game's `dump/` folder), each
overridable by an environment variable.

| Tool | Does |
|---|---|
| `extract_all.py` | extracts every level bundle from the disc, and their skeletons, skins and scripts (`edl.py` is the archive reader and decompressor, `bundle.py` the bundle walker) |
| `parse_skeleton.py`, `parse_skin.py` | parse and validate every skeleton (45) and skin (1,055 copies, 150 distinct) |
| `decode_seq.py` | decodes a sequence to per-frame, per-bone rotation and translation (`--csv`), and validates them all |
| `parse_script.py` | disassembles scripts |
| `parse_facial.py` | morph sequences, facial scripts, and the heads' morph targets (`--obj` writes each target as an OBJ) |
| `parse_gfx.py` | walks every graphics pack and parses its meshes: vertices, normals, UVs, bones and weights, strips as triangles, morph targets |
| `parse_tex.py` | lists and decodes textures to PNG (all 3,219 distinct ones decode), and lists a skin's textures |
| `pose_ref.py` | the palette maths in Python, checked against every rigged skin's bind pose |
| `compare_capture.py` | checks `decode_seq.py` against frames the game decoded |
| `export_gltf.py` | exports a character - skeleton, meshes, textures, shape keys, animations - as a glTF for Blender ([gltf-export.md](gltf-export.md)) |
| `dump_characters.py` | every character as its own glTF, with the animations in shared per-skeleton libraries ([gltf-export.md](gltf-export.md)) |
| `check_gltf.py`, `blender_probe.py` | import an export into Blender headlessly and compare its deformed vertices with the game's skinning |
| `blender_import.py` | imports an export into Blender at 60 fps with the shape-key ranges the facial weights need |
| `make_ghidra_json.py` | writes the layouts and prototypes for `ghidra/NightfireStructs.py` |

`decode_seq.py` agrees with the game **bit for bit** on 3,999 of 4,000 captured frames. The one exception is a
state-dependent quirk of the game's unpack cache (below), so the offline decode is the clean one.

## Open questions and original bugs

- **Unpack cache misalignment:** the cache speeding up long step-mode sequences is indexed by decoded track, and a
  decode with no skin (as root-motion helpers call it) counts every bone where one with a skin skips unused ones.
  One captured cutscene frame (`0400076c`, script `060008a5`, frame 1) came out with its rotations shifted by
  whole bones, in the original as in ours. The skin/no-skin indexing is the likely cause; not yet confirmed.
- The upper aim-yaw clamp tests the pitch variable instead of yaw (kept, since the reimplementation is faithful).
- 12 sequences have 30 more tracks than their skeleton (`04000514/516/517/519/610/784` on skeleton 26,
  `04000689/68a/68b/68c/68f/6a8` on 33) - older exports; the game would misread them.
- Several long cutscene sequences have frame ranges where many bones jump every frame (`04000185` worst); whether
  the cutscenes play those ranges is open.
- Mesh-side original bugs and loose ends are listed at the end of spec-mesh.md and spec-texture.md (an undersized
  triangle-list buffer in psiGetTriList for 253 meshes; primitive flag bit 6, set on most primitives, never read).
