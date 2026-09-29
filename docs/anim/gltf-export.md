# Exporting characters to Blender (glTF)

`tools/anim/export_gltf.py` writes one character - a skin, hashcode type 0x05 - from a level bundle as a binary
glTF (`.glb`): its skeleton, meshes, textures, facial shape keys, attachment points and animations. Blender's own
glTF importer reads it; `tools/anim/blender_import.py` adds the two settings these files need.

```
cd tools/anim
python extract_all.py                                  # once: the level bundles into build/anim/lvl
python export_gltf.py --list --bundle 07000001         # the level's skins, their meshes and sequence counts
python export_gltf.py 05000007 -o bond.glb             # everything for skin 05000007 (Bond, first level)
python export_gltf.py 05000007 --seq 0400000b,04000073 --in-place -o bond_two.glb
blender --python blender_import.py -- bond.glb
```

Options: `--bundle` picks the level (by default the first holding the skin - a skin's meshes and textures come
from the level they are in, and the same skin can differ slightly between levels); `--seq` limits the animations to
the listed sequence hashcodes (body or facial); `--max-anims N`, `--no-anims`, `--no-facial`; `--in-place` zeroes
the root bone's horizontal movement (the game turns it into the object's movement - kept by default, so a walk
walks).

## Every character at once

`python dump_characters.py [outdir]` (default `dump_characters/` at the repository root) writes every skin in the
game as its own `.gltf` + `.bin` in `characters/`, and the animations once each in shared libraries in
`animations/`: `skeleton_NN.gltf` holds every body sequence of skeleton NN from every level, `facial.gltf` every
facial sequence (on Bond's head). Textures go to `textures/`, written once and shared. `index.md` lists what is
where, and which characters each library fits.

The libraries exist because duplicating the animations would not fit: the 66 skins of the main human skeleton
share 785 sequences, and every character with all of its skeleton's animations came to about 42 GB (per level, still
14.5 GB). Shared, the dump is about a gigabyte. In Blender an action drives bones by name, and every character of a
skeleton names its bones alike, so any library action plays on any of them:

```
blender --python tools/anim/blender_import.py -- dump_characters/characters/<character>.gltf dump_characters/animations/skeleton_00.gltf
```

For this the characters in the dump rest their joints at the skeleton's default offsets (times the skin's scale),
where the game animates from, instead of at the skin's own bind positions, which differ from them by up to about a
centimetre; each vertex is moved by its bones' difference to match. A vertex on one bone then skins exactly as in the
game, and a vertex blended between two bones is within a few millimetres (the worst case measured is 3 mm, on a
character whose foot bone binds 9.5 mm from its default). `export_gltf.py` on its own keeps the skin's bind, and is
exact for the animations it embeds.

| Check (Blender, library action on a dump character) | Vertices | Worst difference |
|---|---|---|
| Bond (05000007), 0400000d frame 11 | 1,438 | 4.5e-5 |
| 05000036, 0400000c frame 11 | 1,688 | 3.0e-3 (single-bone vertices 5e-6) |
| 1.1-scaled 05000026, 04000045 frame 11 | 1,663 | 7.7e-4 |
| first-person arms 05000047 (sleeve and gun parts), 04000430 frame 11 | 3,739 | 1.3e-6 |

The same numbers come out with Blender's `guess_original_bind_pose` on or off, and with the animation embedded
instead: rest, bind and animation agree.

## What is in the file

| Part | How it is built |
|---|---|
| Skeleton | one joint per bone the skin uses (bone byte bit 7), named `bone_NN` after its index, parented as the skin says. Rest pose: positions minus the skin's inverse-bind translations (the pose the meshes are modelled in), each joint turned to aim at its child (see below). |
| Rigged meshes | the skin's rigged bodies (triangles reversed and normals negated, see below), skinned with two bones per vertex (JOINTS_0/WEIGHTS_0, weights from the 16-bit weight as `u16/65535`). A sleeve hashcode (bit 0x100000) becomes the level's first sleeve mesh, as the game's default sleeve would. |
| Shape keys | the 22 facial morph targets of a head, as glTF morph targets named in `extras.targetNames`: `mouth_0`-`mouth_9`, `blink`, `eyes_up`, `eyes_down`, `eyes_side_a`, `eyes_side_b`, `brows_raise`, `cheeks_*`, `brow_single`, `jaw_open` (10-14 are certain; the rest are guesses from where each target moves the face - spec-facial.md 2.3). |
| Rigid parts | gun slides, clips, gadgets: each a mesh node under the bone it follows (their vertices are bone-local). |
| Attachment points | the skin's datums, as empties `datum_<id>` under their bones (weapons use ids `whichBone + 1` and `+ 3`). |
| Materials | per strip texture and render state: base colour texture (embedded PNG), double-sided, texture wrap, and an alpha mode from the texture's alpha and the strip's blend and alpha-test flags (`extras.blend`/`alphaRef` keep the game's values). Vertex colours as COLOR_0 when any are not white. The stage-1 environment map (`tex1`) is not exported. |
| Body animations | every sequence of the skin's skeleton in the level, one glTF animation each, `seq_<hashcode>`: every joint's rotation and translation at every frame, 60 frames a second, LINEAR - the game blends linearly between whole frames, so this plays back what the game computes. Sequences whose track count does not match their skeleton (twelve in the game) are skipped. |
| Facial animations | for a skin with morph targets, every facial sequence in the level, `face_<hashcode>`, as morph-weight animations. |

No axis conversion is needed between the game and glTF: both are right-handed with y up, characters face +z, and
glTF's UV origin is the top left, like Direct3D's. Blender's importer turns y up into z up. A unit is about a metre.
The game stores its normals pointing into the mesh, with the triangles wound the same way, so the export reverses
every triangle and negates the normals.

**Bone orientation.** The game's bones all rest unrotated, which Blender draws as every bone pointing straight up.
The export gives each joint a fixed rest orientation instead, aiming its local +y - the axis Blender draws a bone
along - at its child (the next bone when that is a child - unless it is a short helper beside a child that continues the limb, as at the elbow - else the average of its children; a bone without
children keeps its parent's). The node transforms, inverse bind matrices, rigid parts, attachment points and every
animation key are written in those frames, so the deformation is unchanged (the checks below were re-run after the
change). The frames come from the skeleton alone - its default offsets and the parent table every skin of it
shares (cached in `build/anim/skeleton_hierarchy.json`) - so every character of a skeleton and its animation
library agree, and library actions still fit.

## How it was checked

`tools/anim/check_gltf.py <skin> <file.glb> <sequence> <frame> [--face <sequence>] [--render out.png]` imports
the file into Blender (headless, through `blender_probe.py`), plays a body animation - and optionally a facial
one on the shape keys - to a frame, and compares every vertex Blender deforms with the same frame skinned the
game's way: `pose_ref.py`'s palette (the maths of `psiBuildMatrixPalette`, which the reimplementation matches
bit for bit in play), the vertex shader's two-bone blend, and `pos += delta * weight / 32767` for the morphs.

| Check | Vertices | Worst difference |
|---|---|---|
| Bond (05000007), body sequence 0400000b frame 11 | 1,438 | 3.7e-7 |
| the same at frame 61 with facial sequence 04000073 | 1,438 | 3.1e-7 |
| first-person pistol arms (05000047), sequence 04000436 frame 76: sleeve and all seven gun parts | 3,739 | 2.6e-6 |
| a skin scaled 0.7 (0500007b, level 07000023), sequence 04000018 frame 75 | 962 | 4.4e-7 |
| a skin scaled 1.1 with a face (05000026, level 07000007), sequence 04000045 frame 36 | 1,663 | 4.4e-7 |

That is float rounding: Blender reproduces the game's pose exactly. After the bone orientations and the normal fix
went in, the first three were re-run with every vertex identified (`--vertex-ids`): worst 6.5e-7, 1.7e-6
(including the gun parts) and 8.9e-7 for the 1.1-scaled skin. All 150 distinct skins in the 32 levels
export without error (with two animations each, to decode every skeleton's sequences); the only things they
report are the twelve sequences that do not fit their skeleton. Some rigid parts have no geometry - placeholders
on a bone such as `Pulse_1`..`Pulse_20` or `Bolt_datum1_object` - and come in as empties.

## Things to know in Blender

- **Frame rate:** the animations are authored at 60 frames a second. `blender_import.py` sets the scene to 60 fps;
  at any other rate the keys fall between frames.
- **Shape key ranges:** the game's facial weights run from about -0.2 to 1.15, and Blender clamps shape keys to
  0..1 unless their range is widened - `blender_import.py` sets -1..2. Without it the mouth shapes that
  overshoot are cut short (the difference is up to a millimetre or so).
- **Several facial actions at once:** Blender's importer puts every facial animation on the head's shape keys as
  NLA strips, which it plays together. Pick one in the Shape Key Editor and mute or remove the rest.
- **Bone names** are numbers: the game keeps none. Bone 0 is the root (it carries the root motion), 2 the spine
  (aiming turns it), 5 the head (head tracking turns it).
- **Sleeves on the first-person arms:** bone 37 of the first-person skeletons sits about 9 units from its parent,
  so Blender draws a very long bone there; the vertices on it deform correctly.
- **Materials are approximations** of fixed-function Xbox rendering: additive and subtractive strips (muzzle
  flashes, glows) come in as alpha-blended, and there is no environment mapping.
- **What the game adds at run time is not in the file:** head tracking and aiming (which turn bones 5 and 2 towards
  a target), the blending between animations, and the facial layers (mood, blinking, eye direction) that the game
  mixes on top of speech. Each exported animation is one sequence as authored.
