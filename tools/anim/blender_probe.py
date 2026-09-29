"""Runs inside Blender: imports a .glb from export_gltf.py and writes what Blender made of it to a JSON file.

  blender -b --factory-startup --python blender_probe.py -- in.glb out.json [action frame [render.png|- [face]]]

The JSON holds counts (bones, meshes, shape keys, actions, materials, images) and, when an action and a frame
(1-based, in the sequence's own 60 fps frames) are given, every skinned mesh's evaluated vertex positions at that
frame, in the game's axes (Blender's z-up importer axes converted back). check_gltf.py compares them with the
game's own skinning maths. With a PNG path it also renders the posed character.
"""
import json
import math
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
src, out = argv[0], argv[1]
action_name = argv[2] if len(argv) > 2 else None
frame = int(argv[3]) if len(argv) > 3 else 1
render = argv[4] if len(argv) > 4 and argv[4] != '-' else None
face_name = argv[5] if len(argv) > 5 else None

scene = bpy.context.scene
scene.render.fps = 60
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = 60
# NF_PROBE_GUESS=0: import with guess_original_bind_pose off (the rest pose from the joints' own transforms)
guess = os.environ.get('NF_PROBE_GUESS', '1') != '0'
bpy.ops.import_scene.gltf(filepath=src, guess_original_bind_pose=guess)
character = set(scene.objects)
# NF_PROBE_LIBRARY: also import an animation library (dump_characters.py) and drive the character with its actions
library = os.environ.get('NF_PROBE_LIBRARY')
if library:
    bpy.ops.import_scene.gltf(filepath=library, guess_original_bind_pose=guess)

arm = next((o for o in character if o.type == 'ARMATURE'), None)
meshes = [o for o in character if o.type == 'MESH']
res = dict(
    bones=len(arm.data.bones) if arm else 0,
    meshes=[dict(name=o.name, verts=len(o.data.vertices), polys=len(o.data.polygons),
                 shape_keys=[k.name for k in o.data.shape_keys.key_blocks] if o.data.shape_keys else [],
                 materials=[m.name for m in o.data.materials if m], parent=o.parent.name if o.parent else None,
                 parent_bone=o.parent_bone or None)
            for o in meshes],
    actions=sorted(a.name for a in bpy.data.actions),
    materials=len(bpy.data.materials), images=len(bpy.data.images),
    empties=len([o for o in scene.objects if o.type == 'EMPTY']))

# Faces: does each face's own normal (from its winding) agree with its stored corner normals?
for o, m in zip(meshes, res['meshes']):
    me = o.data
    agree = total = 0
    for poly in me.polygons:
        cn = Vector((0.0, 0.0, 0.0))
        for li in poly.loop_indices:
            cn += me.corner_normals[li].vector if hasattr(me, 'corner_normals') else me.loops[li].normal
        total += 1
        agree += poly.normal.dot(cn) > 0
    m['faces_agreeing_with_normals'] = agree / total if total else None
# Bones: does each bone with children point at one of them?
if arm:
    aimed = with_children = 0
    for bone in arm.data.bones:
        if bone.children:
            with_children += 1
            d = bone.tail_local - bone.head_local
            best = max((d.normalized().dot((c.head_local - bone.head_local).normalized())
                        for c in bone.children if (c.head_local - bone.head_local).length > 1e-5), default=None)
            aimed += best is not None and best > 0.95
    res['bones_aimed_at_a_child'] = '%d of %d' % (aimed, with_children)

if action_name and arm:
    act = next((a for a in bpy.data.actions if a.name.startswith(action_name)), None)
    res['action'] = act.name if act else None
    if act:
        ad = arm.animation_data or arm.animation_data_create()
        # drop whatever the importer put on the NLA, so only this action drives the armature
        for t in list(ad.nla_tracks):
            ad.nla_tracks.remove(t)
        ad.action = act
        if hasattr(ad, 'action_suggested_slot') or hasattr(act, 'slots'):
            try:
                ad.action_slot = act.slots[0]
            except Exception:
                pass
        # and keep the facial animations the importer attached to the shape keys (as NLA strips) out of it
        for o in meshes:
            sk = o.data.shape_keys
            if sk is not None and sk.animation_data is not None:
                res.setdefault('shape_key_nla_tracks', {})[o.data.name] = len(sk.animation_data.nla_tracks)
                sk.animation_data_clear()
                for kb in sk.key_blocks[1:]:
                    kb.value = 0.0
                    # the game's weights run from about -0.2 to 1.15; Blender clamps to the slider range
                    kb.slider_min, kb.slider_max = -1.0, 2.0
                face = next((a for a in bpy.data.actions if face_name and a.name.startswith(face_name)), None)
                if face is not None:
                    sad = sk.animation_data_create()
                    sad.action = face
                    try:
                        sad.action_slot = face.slots[0]
                    except Exception:
                        pass
                    res['face_action'] = face.name
        start = act.frame_range[0]
        res['frame_range'] = list(act.frame_range)
        # the sequence's frame f is at time (f-1)/60 s, i.e. Blender frame start + (f-1) at 60 fps
        scene.frame_set(int(round(start + frame - 1)))
        dg = bpy.context.evaluated_depsgraph_get()
        posed, rest, vids = {}, {}, {}
        for o in meshes:
            if o.name.startswith('Icosphere'):   # the importer's display shape for empties
                continue
            ev = o.evaluated_get(dg)
            me = ev.to_mesh()
            mw = o.matrix_world
            # Blender's importer turns glTF (x, y, z) into (x, -z, y); undo it to compare in the game's axes
            posed[o.data.name] = [[p.x, p.z, -p.y] for p in (mw @ v.co for v in me.vertices)]
            # rest = the mesh's own coordinates: the game's stored vertex positions (bind space for a skinned
            # mesh, bone space for a rigid part)
            rest.setdefault(o.data.name, [[p.x, p.z, -p.y] for p in (v.co for v in o.data.vertices)])
            # the game's vertex index, when the export carries one (Exporter.vertex_ids)
            att = next((a for a in o.data.attributes if a.name.lstrip('_').upper() == 'VID'), None)
            if att is not None and att.domain == 'POINT':
                vids.setdefault(o.data.name, [int(round(x.value)) for x in att.data])
            ev.to_mesh_clear()
        res['posed'], res['rest'], res['vids'] = posed, rest, vids
        if render:
            cam_data = bpy.data.cameras.new('cam')
            cam = bpy.data.objects.new('cam', cam_data)
            scene.collection.objects.link(cam)
            pts = [Vector((p[0], -p[2], p[1])) for vs in posed.values() for p in vs]
            lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
            hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
            c = (lo + hi) / 2
            size = max((hi - lo).length, 0.5)
            # three-quarter view from the character's front (+z in the game is -y in Blender)
            cam.location = c + Vector((size * 0.8, -size * 1.6, size * 0.3))
            d = c - cam.location
            cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
            cam_data.lens = 50
            scene.camera = cam
            sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
            sun.rotation_euler = (math.radians(50), 0, math.radians(30))
            scene.collection.objects.link(sun)
            world = bpy.data.worlds.new('w')
            world.color = (0.25, 0.25, 0.28)
            scene.world = world
            scene.render.engine = 'BLENDER_WORKBENCH'
            scene.display.shading.color_type = 'TEXTURE'
            scene.display.shading.light = 'STUDIO'
            scene.display.shading.show_backface_culling = True   # an inside-out mesh shows as missing
            scene.render.resolution_x, scene.render.resolution_y = 640, 800
            scene.render.filepath = render
            bpy.ops.render.render(write_still=True)

with open(out, 'w') as f:
    json.dump(res, f)
print('probe written to', out)
