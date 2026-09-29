"""Runs inside Blender: imports a level .glb from export_level.py and renders it, for checking an export.

  blender -b --factory-startup --python blender_render_level.py -- level.glb out_prefix [start_index]

Writes <out_prefix>_top.png (orthographic, from above, the whole static world) and <out_prefix>_start.png (a
perspective view from the player start: the start_index-th PlayerNewStartPos, looking the way it faces, at eye
height), both with backface culling on so inside-out faces would show as holes, plus <out_prefix>.json with counts.
"""
import json
import math
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
import os
src, prefix = os.path.abspath(argv[0]), os.path.abspath(argv[1])
start_index = int(argv[2]) if len(argv) > 2 else 0

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = 60
bpy.ops.import_scene.gltf(filepath=src)

objs = list(scene.objects)
meshes = [o for o in objs if o.type == 'MESH']
res = dict(objects=len(objs), meshes=len(meshes), empties=sum(o.type == 'EMPTY' for o in objs),
           lights=sum(o.type == 'LIGHT' for o in objs), materials=len(bpy.data.materials),
           images=len(bpy.data.images))

# hide what would clutter the renders: volumes, paths, portals, collision, lights' helper geometry
def under(o, names):
    p = o
    while p is not None:
        if any(p.name.startswith(n) for n in names):
            return True
        p = p.parent
    return False

for o in meshes:
    if under(o, ('volumes', 'paths', 'portals', 'collision', 'sky')) or o.get('hidden'):
        o.hide_render = True

world = bpy.data.worlds.new('w')
world.color = (0.05, 0.05, 0.08)
scene.world = world
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.color_type = 'TEXTURE'
scene.display.shading.light = 'FLAT'
scene.display.shading.show_backface_culling = True

cam_data = bpy.data.cameras.new('cam')
cam = bpy.data.objects.new('cam', cam_data)
scene.collection.objects.link(cam)
scene.camera = cam

# top-down over the rooms and static objects
vis = [o for o in meshes if not o.hide_render and under(o, ('rooms', 'static'))]
pts = [o.matrix_world @ Vector(c) for o in vis for c in o.bound_box]
if pts:
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    c = (lo + hi) / 2
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = max(hi.x - lo.x, hi.y - lo.y) * 1.05
    cam_data.clip_end = (hi.z - lo.z) + 100
    cam.location = (c.x, c.y, hi.z + 10)
    cam.rotation_euler = (0, 0, 0)
    res['extent'] = [list(lo), list(hi)]
    scene.render.resolution_x = scene.render.resolution_y = 1200
    scene.display.shading.show_backface_culling = False   # from above we look at roofs' undersides in places
    scene.render.filepath = prefix + '_top.png'
    bpy.ops.render.render(write_still=True)
    scene.display.shading.show_backface_culling = True

# from the player start, at eye height, looking the way the start faces
group = next((o for o in objs if o.name == 'PlayerNewStartPos' and o.children), None)
starts = sorted(group.children, key=lambda o: o.name) if group else []
res['starts'] = [o.name for o in starts]
if starts:
    s = starts[min(start_index, len(starts) - 1)]
    m = s.matrix_world
    fwd = (m.to_3x3() @ Vector((0.0, -1.0, 0.0))).normalized()   # the game's +z, in Blender's axes
    fwd.z = 0
    fwd.normalize()
    cam_data.type = 'PERSP'
    cam_data.lens_unit = 'FOV'
    cam_data.angle = math.radians(75)
    cam_data.clip_start, cam_data.clip_end = 0.05, 2000
    cam.location = m.translation + Vector((0, 0, 0.6))
    cam.rotation_euler = fwd.to_track_quat('-Z', 'Y').to_euler()
    res['start'] = dict(name=s.name, pos=list(m.translation), forward=list(fwd))
    scene.render.resolution_x, scene.render.resolution_y = 1280, 960
    scene.render.filepath = prefix + '_start.png'
    bpy.ops.render.render(write_still=True)

with open(prefix + '.json', 'w') as f:
    json.dump(res, f, indent=1)
print('rendered', prefix)
