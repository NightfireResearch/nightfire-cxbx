"""Imports a character exported by export_gltf.py into Blender, set up the way the game plays it.

Run it from Blender's Scripting workspace (edit PATHS below), or from a shell:
  blender --python blender_import.py -- 05000007.glb
  blender --python blender_import.py -- characters/05000036_polySurface1.gltf animations/skeleton_00.gltf

With several files (a character from dump_characters.py and its skeleton's animation library), the first is the
character: the first body animation found is put on its armature. Library actions drive bones by name, so any of
them can be picked for it in the Action Editor (in Blender 4.4 and later, choose the action's slot too).

Blender's glTF importer does the real work; this only fixes two things it gets wrong for these files:
- the scene runs at 60 fps, the rate the game's animation frames are authored at (otherwise every animation
  plays at the importer's default rate and its keys land between frames);
- shape keys are clamped to 0..1 by default, but the game's facial weights run from about -0.2 to 1.15, so each
  key's range is widened to -1..2.
Every animation comes in as an action (seq_<hashcode> for the body, face_<hashcode> for facial ones); pick one
in the Action Editor. The first body animation is made active.
"""
import sys

import bpy

PATHS = []   # or list the files here when running from the Scripting workspace

if '--' in sys.argv:
    PATHS = sys.argv[sys.argv.index('--') + 1:]

scene = bpy.context.scene
scene.render.fps = 60
scene.render.fps_base = 1.0
new, first = [], None
for path in PATHS:
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    added = [o for o in bpy.data.objects if o not in before]
    if first is None:
        first = added
    new += added

for o in new:
    sk = o.data.shape_keys if o.type == 'MESH' else None
    if sk is not None:
        for kb in sk.key_blocks[1:]:
            kb.slider_min, kb.slider_max = -1.0, 2.0

arm = next((o for o in first or [] if o.type == 'ARMATURE'), None)
body = sorted(a.name for a in bpy.data.actions if a.name.startswith('seq_'))
if arm is not None and body:
    ad = arm.animation_data or arm.animation_data_create()
    act = bpy.data.actions[body[0]]
    ad.action = act
    try:
        ad.action_slot = act.slots[0]
    except Exception:
        pass
    scene.frame_start, scene.frame_end = int(act.frame_range[0]), int(act.frame_range[1])
print('imported %s: %d objects, %d body and %d facial actions' % (
    ', '.join(PATHS), len(new), len(body), sum(1 for a in bpy.data.actions if a.name.startswith('face_'))))
