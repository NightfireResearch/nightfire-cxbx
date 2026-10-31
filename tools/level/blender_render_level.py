"""Runs inside Blender: imports a level .glb from export_level.py and renders it, for checking an export.

  blender -b --factory-startup --python blender_render_level.py -- level.glb out_prefix [start_index]

Writes <out_prefix>_top.png (orthographic, from above, the whole static world) and <out_prefix>_start.png (a
perspective view from the player start: the start_index-th PlayerNewStartPos, looking the way it faces, at eye
height), both with backface culling on so inside-out faces would show as holes, plus <out_prefix>.json with counts.

A scene from export_level_diff.py gets only the top view, in its groups' colours (changed geometry in its new
version only), with the differences rendered a second time on their own and laid over it (OVERLAY opacity) so
that roofs do not hide them, and a dot on every added, removed, moved or turned object or cel.
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
OVERLAY = 0.75

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

diff = any(o.name.startswith('diff_') and o.parent is None for o in objs)
DOT = 7
DOT_COLOURS = ((' positions', (1.0, 0.95, 0.5)), ('added in ', (0.1, 0.85, 0.2)), ('removed from ', (0.95, 0.15, 0.1)),
               ('moved or changed', (1.0, 0.85, 0.0)))


def dot_colour(o):
    p = o.parent
    while p is not None:
        for name, rgba in DOT_COLOURS:
            if name in p.name:
                return rgba
        p = p.parent
    return None


for o in meshes:
    if diff:
        # the differences' volumes and paths stay: they are coloured by group
        hide = o.get('hidden') or under(o, ('sky',)) or (under(o, ('unchanged',)) and
                                                         under(o, ('volumes', 'paths')))
    else:
        hide = under(o, ('volumes', 'paths', 'portals', 'collision', 'sky')) or o.get('hidden')
    if hide:
        o.hide_render = True

world = bpy.data.worlds.new('w')
world.color = (0.05, 0.05, 0.08)
scene.world = world
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.color_type = 'TEXTURE'
if diff:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import blender_diff
    blender_diff.show_colours()
    scene.display.shading.color_type = 'MATERIAL'
    scene.display.shading.light = 'STUDIO'
    # changed geometry in its new version only (both overlap almost everywhere)
    root = next(o for o in objs if o.name.startswith('diff_') and o.parent is None)
    old_label = root.get('old', '')
    for o in meshes:
        if under(o, ('changed geometry',)) and under(o, (old_label,)):
            o.hide_render = True
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
    if diff:
        # the differences again on their own, laid over the first render at OVERLAY so roofs do not hide them
        keep = {o: o.hide_render for o in meshes}
        for o in meshes:
            if under(o, ('unchanged', 'changed geometry')):
                o.hide_render = True
        scene.render.film_transparent = True
        scene.render.filepath = prefix + '_diffonly.png'
        bpy.ops.render.render(write_still=True)
        scene.render.film_transparent = False
        for o, h in keep.items():
            o.hide_render = h
        import numpy as np
        base = bpy.data.images.load(prefix + '_top.png')
        over = bpy.data.images.load(prefix + '_diffonly.png')
        a = np.empty(len(base.pixels), dtype=np.float32)
        b = np.empty(len(over.pixels), dtype=np.float32)
        base.pixels.foreach_get(a)
        over.pixels.foreach_get(b)
        a, b = a.reshape(-1, 4), b.reshape(-1, 4)
        k = (b[:, 3:4] * OVERLAY)
        a[:, :3] = a[:, :3] * (1 - k) + b[:, :3] * k
        # a dot on each added, removed and moved object, since most are a few pixels across at this scale
        size = scene.render.resolution_x
        img = a.reshape(size, size, 4)
        yy, xx = np.mgrid[-DOT:DOT + 1, -DOT:DOT + 1]
        disc = xx * xx + yy * yy <= DOT * DOT
        ring = disc & (xx * xx + yy * yy >= (DOT - 1.5) ** 2)
        dots = 0
        for o in objs:
            col = dot_colour(o) if o.get('placementIndex') is not None or o.get('placement') is not None else None
            moved = any(w in o.get('diff', '') for w in ('moved', 'turned'))
            if col is None or (col == DOT_COLOURS[3][1] and not moved):
                continue                     # a change of keys only gets no dot
            p = o.matrix_world.translation
            px = int((p.x - c.x) / cam_data.ortho_scale * size + size / 2)
            py = int((p.y - c.y) / cam_data.ortho_scale * size + size / 2)   # Blender's rows run bottom-up
            if DOT <= px < size - DOT and DOT <= py < size - DOT:
                patch = img[py - DOT:py + DOT + 1, px - DOT:px + DOT + 1]
                patch[disc] = list(col) + [1.0]
                patch[ring] = [0.0, 0.0, 0.0, 1.0]
                dots += 1
        res['dots'] = dots
        base.pixels.foreach_set(a.ravel())
        base.filepath_raw = prefix + '_top.png'
        base.file_format = 'PNG'
        base.save()
        os.remove(prefix + '_diffonly.png')
        res['diff'] = True
    scene.display.shading.show_backface_culling = True

# from the player start, at eye height, looking the way the start faces
group = None if diff else next((o for o in objs if o.name == 'PlayerNewStartPos' and o.children), None)
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
